#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/boot"

ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
ROOT_SIZE="${PANTHERA_ZFS_ROOT_SIZE:-4g}"
DATASET="${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}"
TAG="${PANTHERA_ZFS_ROOT_TAG:-zfs-root-host-$(date +%Y%m%d-%H%M%S)}"
PAYLOAD_TAR="${PANTHERA_ZFS_ROOT_PAYLOAD_TAR:-}"
CREATE_ROOT=1
FORCE=0
ALLOW_PRIVILEGE_ESCALATION=1
OUTPUT_UID="${PANTHERA_ZFS_ROOT_OUTPUT_UID:-}"
OUTPUT_GID="${PANTHERA_ZFS_ROOT_OUTPUT_GID:-}"
REEXEC_MODE="${PANTHERA_ZFS_HOST_REEXEC:-0}"
HOST_ZFS_PATHS=(
  /usr/local/zfs/bin
  /opt/openzfs/bin
  /opt/homebrew/bin
  /opt/homebrew/sbin
  /usr/local/bin
  /usr/local/sbin
  /opt/local/bin
  /opt/local/sbin
)

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Creates and populates a Panthera ZFS root image directly from the host using
real host OpenZFS tools. This path does not boot or use an HFS installer image.

Options:
  --tag NAME          Artifact tag prefix (default: ${TAG})
  --artifacts DIR     Artifact directory (default: ${ARTIFACT_DIR})
  --root-disk PATH    ZFS root image to create/populate
  --root-size SIZE    Size for a newly-created ZFS root disk (default: ${ROOT_SIZE})
  --dataset DATASET   ZFS boot dataset (default: ${DATASET})
  --payload-tar PATH  Host-built rootfs payload archive to extract into ZFS
  --reuse-root-disk   Do not recreate the ZFS root disk
  --no-privilege-escalation
                      Do not re-exec through macOS administrator authorization
                      when root is required for host zpool operations
  --output-uid UID    Ownership UID to apply to the generated root image
  --output-gid GID    Ownership GID to apply to the generated root image
  --force             Replace existing generated image/payload
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) TAG="$2"; shift 2 ;;
    --artifacts) ARTIFACT_DIR="$2"; shift 2 ;;
    --root-disk) ROOT_DISK="$2"; shift 2 ;;
    --root-size) ROOT_SIZE="$2"; shift 2 ;;
    --dataset) DATASET="$2"; shift 2 ;;
    --payload-tar) PAYLOAD_TAR="$2"; shift 2 ;;
    --reuse-root-disk) CREATE_ROOT=0; shift ;;
    --no-privilege-escalation) ALLOW_PRIVILEGE_ESCALATION=0; shift ;;
    --output-uid) OUTPUT_UID="$2"; shift 2 ;;
    --output-gid) OUTPUT_GID="$2"; shift 2 ;;
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
POOL="${DATASET%%/*}"

if [[ "${ARTIFACT_DIR}" != /* ]]; then
  ARTIFACT_DIR="${PWD}/${ARTIFACT_DIR}"
fi
if [[ "${ROOT_DISK}" != /* ]]; then
  ROOT_DISK="${PWD}/${ROOT_DISK}"
fi
if [[ -z "${PAYLOAD_TAR}" ]]; then
  PAYLOAD_TAR="${ARTIFACT_DIR}/${TAG}-rootfs-payload.tar.gz"
elif [[ "${PAYLOAD_TAR}" != /* ]]; then
  PAYLOAD_TAR="${PWD}/${PAYLOAD_TAR}"
fi

for zfs_path in "${HOST_ZFS_PATHS[@]}"; do
  if [[ -d "${zfs_path}" ]]; then
    PATH="${zfs_path}:${PATH}"
  fi
done
export PATH

for tool in ruby hdiutil diskutil zpool zfs bsdtar; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "Direct ZFS root image creation requires host tool '${tool}'." >&2
    echo "Install/load host OpenZFS or run rootfs/create_zfs_root_image.sh --builder-mode installer explicitly." >&2
    exit 1
  fi
done

if pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >/dev/null 2>&1; then
  echo "A Panthera/QEMU process is already running; stop it before creating a ZFS root image." >&2
  pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >&2 || true
  exit 1
fi

mkdir -p "${ARTIFACT_DIR}" "$(dirname "${ROOT_DISK}")" "$(dirname "${PAYLOAD_TAR}")"

if [[ ! -f "${PAYLOAD_TAR}" || ( "${FORCE}" == "1" && "${REEXEC_MODE}" != "1" ) ]]; then
  env \
    PANTHERA_ROOT_SHELL="${PANTHERA_ROOT_SHELL:-/bin/zsh}" \
    PANTHERA_DEFAULT_NETWORK_OWNER="${PANTHERA_DEFAULT_NETWORK_OWNER:-ipconfiguration}" \
    PANTHERA_STAGE_ZFS=1 \
    bash "${PANTHERA_ROOT}/rootfs/create_rootfs_payload_tar.sh" \
      --output "${PAYLOAD_TAR}" \
      --force \
      --no-build-components
fi
if [[ ! -f "${PAYLOAD_TAR}" ]]; then
  echo "Payload archive not found: ${PAYLOAD_TAR}" >&2
  exit 1
fi

if [[ "${EUID}" != "0" ]]; then
  if [[ "${ALLOW_PRIVILEGE_ESCALATION}" != "1" ]]; then
    echo "Direct ZFS root image creation requires root for host zpool operations." >&2
    exit 1
  fi
  reexec_cmd="$(printf "%q " "${BASH}" "$0" \
    --tag "${TAG}" \
    --artifacts "${ARTIFACT_DIR}" \
    --root-disk "${ROOT_DISK}" \
    --root-size "${ROOT_SIZE}" \
    --dataset "${DATASET}" \
    --payload-tar "${PAYLOAD_TAR}" \
    --output-uid "${OUTPUT_UID:-$(id -u)}" \
    --output-gid "${OUTPUT_GID:-$(id -g)}" \
    --no-privilege-escalation)"
  if [[ "${CREATE_ROOT}" != "1" ]]; then
    reexec_cmd+=" --reuse-root-disk"
  fi
  if [[ "${FORCE}" == "1" ]]; then
    reexec_cmd+=" --force"
  fi
  reexec_cmd="PANTHERA_ZFS_HOST_REEXEC=1 ${reexec_cmd}"
  exec osascript \
    -e 'on run argv' \
    -e 'do shell script (item 1 of argv) with administrator privileges' \
    -e 'end run' \
    -- "${reexec_cmd}"
fi

if [[ "${CREATE_ROOT}" == "1" ]]; then
  if [[ -f "${ROOT_DISK}" && "${FORCE}" != "1" ]]; then
    echo "ZFS root image already exists: ${ROOT_DISK}" >&2
    echo "Use --force or --reuse-root-disk." >&2
    exit 1
  fi
  ruby "${PANTHERA_ROOT}/tools/create_zfs_gpt_image.rb" \
    --image "${ROOT_DISK}" \
    --size "${ROOT_SIZE}" \
    --label PantheraZFSRoot >/dev/null
elif [[ ! -f "${ROOT_DISK}" ]]; then
  echo "ZFS root image not found: ${ROOT_DISK}" >&2
  exit 1
fi

raw_device=""
partition_device=""
mount_root="${ARTIFACT_DIR}/${TAG}-host-zroot"
pool_created=0

cleanup() {
  set +e
  if [[ "${pool_created}" == "1" ]] && command -v zpool >/dev/null 2>&1; then
    if command -v zfs >/dev/null 2>&1; then
      zfs unmount -f "${DATASET}" >/dev/null 2>&1 || true
    fi
    if command -v diskutil >/dev/null 2>&1 && [[ -d "${mount_root}" ]]; then
      diskutil unmount force "${mount_root}" >/dev/null 2>&1 || true
    fi
    zpool export "${POOL}" >/dev/null 2>&1 || true
  fi
  if [[ -n "${raw_device}" ]]; then
    hdiutil detach "${raw_device}" -force >/dev/null 2>&1 || true
  fi
  rm -rf "${mount_root}"
}
trap cleanup EXIT

if zpool list -H -o name "${POOL}" >/dev/null 2>&1; then
  echo "Host already has an imported ZFS pool named '${POOL}'; refusing to create Panthera root pool with the same name." >&2
  exit 1
fi

attach_output="$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage "${ROOT_DISK}" 2>&1)"
raw_device="$(printf '%s\n' "${attach_output}" | awk '/^\/dev\// {print $1; exit}')"
if [[ -z "${raw_device}" ]]; then
  echo "Failed to attach ZFS root image: ${ROOT_DISK}" >&2
  echo "${attach_output}" >&2
  exit 1
fi

while IFS= read -r candidate; do
  [[ "${candidate}" == "${raw_device}" ]] && continue
  if diskutil info "${candidate}" 2>/dev/null | grep -Eq '6A898CC3-1DD2-11B2-99A6-080020736631|ZFS|PantheraZFSRoot'; then
    partition_device="${candidate}"
    break
  fi
done < <(printf '%s\n' "${attach_output}" | awk '/^\/dev\// {print $1}')
if [[ -z "${partition_device}" ]]; then
  echo "Failed to find ZFS partition in image: ${ROOT_DISK}" >&2
  echo "${attach_output}" >&2
  exit 1
fi

rm -rf "${mount_root}"
mkdir -p "${mount_root}"

zpool create -f -o cachefile=none -R "${mount_root}" -m none "${POOL}" "${partition_device}"
pool_created=1
zfs create -p -o mountpoint=/ -o atime=off "${DATASET}"
zpool set bootfs="${DATASET}" "${POOL}"
zfs mount "${DATASET}" >/dev/null 2>&1 || true

if [[ ! -d "${mount_root}" ]]; then
  echo "ZFS dataset did not mount at expected altroot: ${mount_root}" >&2
  exit 1
fi

if command -v mdutil >/dev/null 2>&1; then
  mdutil -i off "${mount_root}" >/dev/null 2>&1 || true
fi

bsdtar -C "${mount_root}" --no-xattrs --no-acls --no-mac-metadata -xpf "${PAYLOAD_TAR}"
mkdir -p "${mount_root}/dev" "${mount_root}/tmp" "${mount_root}/var/run" "${mount_root}/var/tmp"
chmod 1777 "${mount_root}/tmp" "${mount_root}/var/tmp"
test -x "${mount_root}/sbin/launchd"
test -x "${mount_root}/bin/zsh"
test -d "${mount_root}/System/Library/LaunchDaemons"

sync
rm -rf "${mount_root}/.Spotlight-V100" "${mount_root}/.fseventsd" "${mount_root}/.Trashes" >/dev/null 2>&1 || true
zfs unmount -f "${DATASET}" >/dev/null 2>&1 || diskutil unmount force "${mount_root}" >/dev/null 2>&1 || true
zpool export "${POOL}"
pool_created=0
hdiutil detach "${raw_device}" -force >/dev/null
raw_device=""
rm -rf "${mount_root}"

if [[ -n "${OUTPUT_UID}" && -n "${OUTPUT_GID}" ]]; then
  chown "${OUTPUT_UID}:${OUTPUT_GID}" "${ROOT_DISK}" 2>/dev/null || true
fi

cat <<EOF
Created Panthera ZFS root image directly from host OpenZFS:
  root image:  ${ROOT_DISK}
  payload tar: ${PAYLOAD_TAR}
  dataset:     ${DATASET}
EOF
