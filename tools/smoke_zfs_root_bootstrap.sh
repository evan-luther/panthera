#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/boot"
INSTALLER_DISK="${PANTHERA_ZFS_ROOT_INSTALLER_DISK:-${PANTHERA_ZFS_ROOT_CONTROL_DISK:-${PANTHERA_ZFS_INSTALLER_IMAGE:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-installer.img}}}"
ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
SOURCE_ROOT_DISK="${PANTHERA_ZFS_ROOT_SOURCE_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
ROOT_SIZE="${PANTHERA_ZFS_ROOT_SIZE:-4g}"
DATASET="${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}"
ROOTDEV="${PANTHERA_ZFS_ROOTDEV:-uuid}"
TAG="${PANTHERA_ZFS_ROOT_TAG:-zfs-root-bootstrap-$(date +%Y%m%d-%H%M%S)}"
PAYLOAD_TAR="${PANTHERA_ZFS_ROOT_PAYLOAD_TAR:-}"
PORT="${PANTHERA_ZFS_ROOT_PORT:-2225}"
TIMEOUT="${PANTHERA_ZFS_ROOT_TIMEOUT:-420}"
REMOTE_TIMEOUT="${PANTHERA_ZFS_ROOT_REMOTE_TIMEOUT:-900}"
BUILDER_MODE="${PANTHERA_ZFS_ROOT_BUILDER_MODE:-zfs-control}"
REBUILD_INSTALLER=1
CREATE_ROOT=1
ATTEMPT_ROOT_BOOT=1
FORCE=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --tag NAME             Artifact tag prefix (default: ${TAG})
  --source-root-disk PATH Existing immutable ZFS root image used as builder
                         source in zfs-control mode (default: ${SOURCE_ROOT_DISK})
  --installer-disk PATH  Temporary ZFS installer image used only to build the
                         ZFS root (default: ${INSTALLER_DISK})
  --control-disk PATH    Compatibility alias for --installer-disk
  --root-disk PATH       Target output ZFS root image to create and boot
                         (default: ${ROOT_DISK})
  --root-size SIZE       Size for a newly-created ZFS root disk (default: ${ROOT_SIZE})
  --dataset DATASET      ZFS boot dataset (default: ${DATASET})
  --rootdev NAME         XNU rootdev boot arg for the root attempt (default: ${ROOTDEV})
  --payload-tar PATH     Host-built rootfs payload archive to extract into ZFS
  --builder-mode MODE    zfs-control, direct, or installer (default: ${BUILDER_MODE})
  --port PORT            Host TCP port forwarded to builder guest sshd (default: ${PORT})
  --timeout SECONDS      Per-boot timeout (default: ${TIMEOUT})
  --remote-timeout S     Per-SSH-command timeout for root population (default: ${REMOTE_TIMEOUT})
  --reuse-installer      Do not rebuild the installer image
  --reuse-control        Compatibility alias for --reuse-installer
  --reuse-root-disk      Do not recreate the ZFS root disk
  --skip-root-boot       Only build/populate the ZFS root image
  --force                Replace existing images when rebuilding
  --help                 Show this help
EOF
}
canonicalize_path() {
  local target="$1"
  if [[ -z "${target}" ]]; then
    echo ""
    return 0
  fi
  if command -v python3 >/dev/null 2>&1; then
    python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "${target}"
  elif command -v perl >/dev/null 2>&1; then
    perl -e 'use Cwd "abs_path"; use File::Basename; use File::Spec; my $p = $ARGV[0]; my $d = dirname($p); my $b = basename($p); if (-d $d) { print File::Spec->catfile(abs_path($d), $b), "\n"; } else { print File::Spec->rel2abs($p), "\n"; }' "${target}"
  else
    local dir base
    dir="$(dirname "${target}")"
    base="$(basename "${target}")"
    if [[ -d "${dir}" ]]; then
      dir="$(cd "${dir}" 2>/dev/null && pwd -P)"
      echo "${dir}/${base}"
    else
      if [[ "${target}" = /* ]]; then
        echo "${target}"
      else
        echo "${PWD}/${target}"
      fi
    fi
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) TAG="$2"; shift 2 ;;
    --source-root-disk)
      if [[ $# -lt 2 || -z "${2:-}" ]]; then
        echo "--source-root-disk requires a path argument" >&2
        exit 2
      fi
      SOURCE_ROOT_DISK="$2"
      shift 2
      ;;
    --installer-disk|--control-disk) INSTALLER_DISK="$2"; shift 2 ;;
    --root-disk) ROOT_DISK="$2"; shift 2 ;;
    --root-size) ROOT_SIZE="$2"; shift 2 ;;
    --dataset) DATASET="$2"; shift 2 ;;
    --rootdev) ROOTDEV="$2"; shift 2 ;;
    --payload-tar) PAYLOAD_TAR="$2"; shift 2 ;;
    --builder-mode) BUILDER_MODE="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    --remote-timeout) REMOTE_TIMEOUT="$2"; shift 2 ;;
    --reuse-installer|--reuse-control) REBUILD_INSTALLER=0; shift ;;
    --reuse-root-disk) CREATE_ROOT=0; shift ;;
    --skip-root-boot) ATTEMPT_ROOT_BOOT=0; shift ;;
    --force) FORCE=1; shift ;;
    --help) usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "${DATASET}" != */* ]]; then
  echo "--dataset must name a pool and filesystem, for example tank/ROOT/panthera" >&2
  exit 2
fi
case "${BUILDER_MODE}" in
  zfs-control|direct|installer)
    ;;
  *)
    echo "--builder-mode must be zfs-control, direct, or installer" >&2
    exit 2
    ;;
esac
if [[ -z "${PAYLOAD_TAR}" ]]; then
  PAYLOAD_TAR="${ARTIFACT_DIR}/${TAG}-rootfs-payload.tar.gz"
fi
if [[ "${PAYLOAD_TAR}" != /* ]]; then
  PAYLOAD_TAR="${PANTHERA_ROOT}/${PAYLOAD_TAR}"
fi
SOURCE_ROOT_DISK="$(canonicalize_path "${SOURCE_ROOT_DISK}")"
INSTALLER_DISK="$(canonicalize_path "${INSTALLER_DISK}")"
ROOT_DISK="$(canonicalize_path "${ROOT_DISK}")"
PAYLOAD_TAR="$(canonicalize_path "${PAYLOAD_TAR}")"

if [[ "${BUILDER_MODE}" == "zfs-control" ]]; then
  if [[ "${INSTALLER_DISK}" == "${ROOT_DISK}" ]]; then
    echo "Error: installer/control disk (${INSTALLER_DISK}) and output-root disk (${ROOT_DISK}) cannot refer to the same path." >&2
    exit 1
  fi
  if [[ "${REBUILD_INSTALLER}" == "1" && "${SOURCE_ROOT_DISK}" == "${INSTALLER_DISK}" ]]; then
    echo "Error: source-root disk (${SOURCE_ROOT_DISK}) and installer/control disk (${INSTALLER_DISK}) cannot refer to the same path when rebuilding installer/control image." >&2
    exit 1
  fi
  if [[ "${CREATE_ROOT}" == "1" && "${SOURCE_ROOT_DISK}" == "${ROOT_DISK}" ]]; then
    echo "Error: source-root disk (${SOURCE_ROOT_DISK}) and output-root disk (${ROOT_DISK}) cannot refer to the same path when recreating output-root image." >&2
    exit 1
  fi
fi

if pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >/dev/null 2>&1; then
  echo "A Panthera/QEMU process is already running; stop it before this destructive smoke." >&2
  pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >&2 || true
  exit 1
fi

mkdir -p "${ARTIFACT_DIR}" "$(dirname "${INSTALLER_DISK}")" "$(dirname "${ROOT_DISK}")" "$(dirname "${SOURCE_ROOT_DISK}")"

builder_args=(
  --tag "${TAG}"
  --artifacts "${ARTIFACT_DIR}"
  --source-root-disk "${SOURCE_ROOT_DISK}"
  --installer-disk "${INSTALLER_DISK}"
  --root-disk "${ROOT_DISK}"
  --root-size "${ROOT_SIZE}"
  --dataset "${DATASET}"
  --payload-tar "${PAYLOAD_TAR}"
  --builder-mode "${BUILDER_MODE}"
  --port "${PORT}"
  --timeout "${TIMEOUT}"
  --remote-timeout "${REMOTE_TIMEOUT}"
)
if [[ "${REBUILD_INSTALLER}" != "1" ]]; then
  builder_args+=(--reuse-installer)
fi
if [[ "${CREATE_ROOT}" != "1" ]]; then
  builder_args+=(--reuse-root-disk)
fi
if [[ "${FORCE}" == "1" ]]; then
  builder_args+=(--force)
fi

bash "${PANTHERA_ROOT}/rootfs/create_zfs_root_image.sh" "${builder_args[@]}"

bash "${PANTHERA_ROOT}/boot/efi/stage_phase2_efi.sh" \
  --zfs-root "${DATASET}" \
  --rootdev "${ROOTDEV}" \
  > "${ARTIFACT_DIR}/${TAG}.root.stage_phase2.log" 2>&1

ROOT_BOOT_LOG="${ARTIFACT_DIR}/${TAG}-root-boot.log"
if [[ "${ATTEMPT_ROOT_BOOT}" == "1" ]]; then
  rm -f "${ROOT_BOOT_LOG}"
  set +e
  (
    cd "${PANTHERA_ROOT}"
    PANTHERA_ROOT_DISK="${ROOT_DISK}" \
    PANTHERA_FIX_CACHE_OWNERSHIP=0 \
      boot/qemu/run_phase2_serial.sh --no-reboot --ssh-port 0
  ) > "${ROOT_BOOT_LOG}" 2>&1 &
  root_boot_pid=$!
  root_boot_status=blocked
  root_boot_deadline=$((SECONDS + TIMEOUT))
  while kill -0 "${root_boot_pid}" >/dev/null 2>&1; do
    if [[ -s "${ROOT_BOOT_LOG}" ]] && grep -q 'login:' "${ROOT_BOOT_LOG}"; then
      root_boot_status=login
      kill "${root_boot_pid}" >/dev/null 2>&1 || true
      break
    fi
    if (( SECONDS >= root_boot_deadline )); then
      kill "${root_boot_pid}" >/dev/null 2>&1 || true
      break
    fi
    sleep 1
  done
  wait "${root_boot_pid}" >/dev/null 2>&1
  root_boot_rc=$?
  set -e
  pkill -f 'qemu-system-x86_64.*panthera-zfs-root' >/dev/null 2>&1 || true
  if [[ "${root_boot_status}" != "login" ]] && grep -Eq 'zfs_vfs_mountroot|zfs_boot_publish_bootfs|BSD root:' "${ROOT_BOOT_LOG}"; then
    root_boot_status=mountroot-progress
  fi
  if [[ "${root_boot_status}" != "login" ]]; then
    echo "ZFS root image was populated, but root boot did not reach login (${root_boot_status}, rc=${root_boot_rc})." >&2
    echo "See ${ROOT_BOOT_LOG}" >&2
    exit 1
  fi
fi

cat <<EOF
ZFS root bootstrap smoke passed:
  source image:    ${SOURCE_ROOT_DISK}
  installer image: ${INSTALLER_DISK}
  root image:      ${ROOT_DISK}
  payload tar:   ${PAYLOAD_TAR}
  dataset:       ${DATASET}
  rootdev:       ${ROOTDEV}
  populate log:  ${ARTIFACT_DIR}/${TAG}-populate.log
  root boot log: ${ROOT_BOOT_LOG}
EOF
