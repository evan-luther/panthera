#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/boot"
ROOT_DISK="${PANTHERA_ZFS_HYBRID_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-hybrid-root.img}"
DATA_DISK="${PANTHERA_ZFS_HYBRID_DATA_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-hybrid-data.img}"
DATA_SIZE="${PANTHERA_ZFS_HYBRID_DATA_SIZE:-512m}"
TAG="${PANTHERA_ZFS_HYBRID_TAG:-zfs-hybrid-layout-$(date +%Y%m%d-%H%M%S)}"
PORT="${PANTHERA_ZFS_HYBRID_PORT:-2225}"
TIMEOUT="${PANTHERA_ZFS_HYBRID_TIMEOUT:-360}"
REMOTE_TIMEOUT="${PANTHERA_ZFS_HYBRID_REMOTE_TIMEOUT:-220}"
POOL="${PANTHERA_ZFS_HYBRID_POOL:-pantherazhybrid}"
REBUILD_ROOTFS=1
CREATE_DATA=1
FORCE=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --tag NAME          Artifact tag prefix (default: ${TAG})
  --root-disk PATH    ZFS hybrid HFS control root image (default: ${ROOT_DISK})
  --data-disk PATH    Disposable secondary ZFS data disk (default: ${DATA_DISK})
  --data-size SIZE    Size for a newly-created data disk (default: ${DATA_SIZE})
  --port PORT         Host TCP port forwarded to guest sshd (default: ${PORT})
  --timeout SECONDS   Per-boot timeout (default: ${TIMEOUT})
  --remote-timeout S  Per-SSH-command timeout (default: ${REMOTE_TIMEOUT})
  --pool NAME         Pool name to create/import (default: ${POOL})
  --reuse-rootfs      Do not rebuild the control rootfs image
  --reuse-data-disk   Do not recreate/partition the secondary data disk
  --force             Replace existing root/data images when rebuilding
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag) TAG="$2"; shift 2 ;;
    --root-disk) ROOT_DISK="$2"; shift 2 ;;
    --data-disk) DATA_DISK="$2"; shift 2 ;;
    --data-size) DATA_SIZE="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    --remote-timeout) REMOTE_TIMEOUT="$2"; shift 2 ;;
    --pool) POOL="$2"; shift 2 ;;
    --reuse-rootfs) REBUILD_ROOTFS=0; shift ;;
    --reuse-data-disk) CREATE_DATA=0; shift ;;
    --force) FORCE=1; shift ;;
    --help) usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if ! command -v ruby >/dev/null 2>&1; then
  echo "ruby is required to create the ZFS GPT data disk image" >&2
  exit 1
fi
if pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >/dev/null 2>&1; then
  echo "A Panthera/QEMU process is already running; stop it before this smoke." >&2
  pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >&2 || true
  exit 1
fi

mkdir -p "${ARTIFACT_DIR}" "$(dirname "${ROOT_DISK}")" "$(dirname "${DATA_DISK}")"

if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  if [[ -f "${ROOT_DISK}" && "${FORCE}" != "1" ]]; then
    echo "Root hybrid image already exists: ${ROOT_DISK}" >&2
    echo "Use --force or --reuse-rootfs." >&2
    exit 1
  fi
  PANTHERA_ROOT_SHELL=/bin/zsh \
  PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  PANTHERA_STAGE_ZFS=1 \
  PANTHERA_STAGE_ZFS_HYBRID=1 \
    bash "${PANTHERA_ROOT}/rootfs/create_hfs_root_image.sh" \
      --image "${ROOT_DISK}" \
      --force \
      --no-build-components
elif [[ ! -f "${ROOT_DISK}" ]]; then
  echo "Root hybrid image not found: ${ROOT_DISK}" >&2
  exit 1
fi

if [[ "${CREATE_DATA}" == "1" ]]; then
  if [[ -f "${DATA_DISK}" && "${FORCE}" != "1" ]]; then
    echo "Data hybrid image already exists: ${DATA_DISK}" >&2
    echo "Use --force or --reuse-data-disk." >&2
    exit 1
  fi
  ruby "${PANTHERA_ROOT}/tools/create_zfs_gpt_image.rb" \
    --image "${DATA_DISK}" \
    --size "${DATA_SIZE}" \
    --label PantheraHybridZFS >/dev/null
elif [[ ! -f "${DATA_DISK}" ]]; then
  echo "Data hybrid image not found: ${DATA_DISK}" >&2
  exit 1
fi

bash "${PANTHERA_ROOT}/boot/efi/stage_phase2_efi.sh" --enable-zfs \
  > "${ARTIFACT_DIR}/${TAG}.stage_phase2.log" 2>&1

read -r -d '' CREATE_TARGET_SNIPPET <<'REMOTE' || true
rootdev='__PANTHERA_BSD_ROOTDEV__'
root_path="/dev/$rootdev"
target=
candidate_count=0
for d in /dev/disk*s1; do
  [ "$d" = "$root_path" ] && continue
  [ -e "$d" ] || continue
  target="$d"
  candidate_count=$((candidate_count + 1))
done
/bin/echo PANTHERA_ZFS_HYBRID_ROOTDEV:$rootdev
/bin/echo PANTHERA_ZFS_HYBRID_CANDIDATE_COUNT:$candidate_count
/bin/echo PANTHERA_ZFS_HYBRID_TARGET:$target
[ "$candidate_count" -eq 1 ] || exit 42
[ -n "$target" ] || exit 43
[ "$target" != "$root_path" ] || exit 44
REMOTE

read -r -d '' CREATE_COMMAND <<REMOTE || true
set -e
pool='${POOL}'
/bin/echo PANTHERA_ZFS_HYBRID_CREATE_BEGIN
${CREATE_TARGET_SNIPPET}
/sbin/zpool create -f -m none "\$pool" "\$target"
/bin/echo PANTHERA_ZFS_HYBRID_CREATE_RC:\$?
/sbin/zfs create -u -o mountpoint=/var "\$pool/var"
/sbin/zfs create -u -o mountpoint=/tmp "\$pool/tmp"
/sbin/zfs create -u -o mountpoint=/Users "\$pool/Users"
/sbin/zfs create -u -o mountpoint=/Library "\$pool/Library"
/bin/echo PANTHERA_ZFS_HYBRID_DATASETS_CREATED
/sbin/sync
/bin/echo PANTHERA_ZFS_HYBRID_EXPORT_ENTER
/sbin/zpool export "\$pool"
/bin/echo PANTHERA_ZFS_HYBRID_CREATE_OK
REMOTE

read -r -d '' VERIFY_COMMAND <<REMOTE || true
set -e
pool='${POOL}'
/bin/echo PANTHERA_ZFS_HYBRID_VERIFY_BEGIN
/bin/cat /var/run/panthera_zfs_hybrid_mounted
/sbin/zpool status "\$pool"
/sbin/zfs list -H -o name,mountpoint,mounted "\$pool/var" "\$pool/tmp" "\$pool/Users" "\$pool/Library"
/bin/test -d /var/run
/bin/test -d /var/log
/bin/test -d /tmp
/bin/test -d /Users/Shared
/bin/test -d /Library/Preferences/SystemConfiguration
/bin/echo zfs-var > /var/run/zfs_hybrid_var_probe
/bin/echo zfs-tmp > /tmp/zfs_hybrid_tmp_probe
/bin/cat /var/run/zfs_hybrid_var_probe
/bin/cat /tmp/zfs_hybrid_tmp_probe
/bin/echo PANTHERA_ZFS_HYBRID_VERIFY_OK
REMOTE

PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" \
    --root-disk "${ROOT_DISK}" \
    --root-kind hfs \
    --data-disk "${DATA_DISK}" \
    --timeout "${TIMEOUT}" \
    --port "${PORT}" \
    --tag "${TAG}-create" \
    --command "${CREATE_COMMAND}" \
    --marker "PANTHERA_ZFS_HYBRID_CREATE_OK"

PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" \
    --root-disk "${ROOT_DISK}" \
    --root-kind hfs \
    --data-disk "${DATA_DISK}" \
    --timeout "${TIMEOUT}" \
    --port "${PORT}" \
    --tag "${TAG}-verify" \
    --command "${VERIFY_COMMAND}" \
    --marker "PANTHERA_ZFS_HYBRID_VERIFY_OK"

cat <<EOF
ZFS hybrid layout smoke passed:
  root image: ${ROOT_DISK}
  data image: ${DATA_DISK}
  create serial log: ${ARTIFACT_DIR}/${TAG}-create.log
  create ssh log:    ${ARTIFACT_DIR}/${TAG}-create.ssh.log
  verify serial log: ${ARTIFACT_DIR}/${TAG}-verify.log
  verify ssh log:    ${ARTIFACT_DIR}/${TAG}-verify.ssh.log
EOF
