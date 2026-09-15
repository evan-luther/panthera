#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ARTIFACT_DIR="${PANTHERA_ROOT}/artifacts/boot"
ROOT_DISK="${PANTHERA_ZFS_SMOKE_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-smoke-root.img}"
DATA_DISK="${PANTHERA_ZFS_SMOKE_DATA_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-data-smoke.img}"
DATA_SIZE="${PANTHERA_ZFS_SMOKE_DATA_SIZE:-256m}"
TAG="${PANTHERA_ZFS_SMOKE_TAG:-zfs-data-pool-$(date +%Y%m%d-%H%M%S)}"
PORT="${PANTHERA_ZFS_SMOKE_PORT:-2225}"
TIMEOUT="${PANTHERA_ZFS_SMOKE_TIMEOUT:-320}"
REMOTE_TIMEOUT="${PANTHERA_ZFS_SMOKE_REMOTE_TIMEOUT:-180}"
POOL="${PANTHERA_ZFS_SMOKE_POOL:-pantherazboot}"
PAYLOAD="${PANTHERA_ZFS_SMOKE_PAYLOAD:-hello-panthera-zfs-${TAG}}"
REBUILD_ROOTFS=1
CREATE_DATA=1
FORCE=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --tag NAME          Artifact tag prefix (default: ${TAG})
  --root-disk PATH    ZFS smoke HFS control root image (default: ${ROOT_DISK})
  --data-disk PATH    Disposable secondary ZFS data disk (default: ${DATA_DISK})
  --data-size SIZE    Size for a newly-created data disk (default: ${DATA_SIZE})
  --port PORT         Host TCP port forwarded to guest sshd (default: ${PORT})
  --timeout SECONDS   Per-boot timeout (default: ${TIMEOUT})
  --remote-timeout S  Per-SSH-command timeout (default: ${REMOTE_TIMEOUT})
  --pool NAME         Pool name to create/import (default: ${POOL})
  --payload TEXT      File payload expected to persist across boots
  --reuse-rootfs      Do not rebuild the control rootfs image
  --reuse-data-disk   Do not recreate/partition the secondary data disk
  --force             Replace existing root/data images when rebuilding
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --tag)
      TAG="$2"
      shift 2
      ;;
    --root-disk)
      ROOT_DISK="$2"
      shift 2
      ;;
    --data-disk)
      DATA_DISK="$2"
      shift 2
      ;;
    --data-size)
      DATA_SIZE="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    --remote-timeout)
      REMOTE_TIMEOUT="$2"
      shift 2
      ;;
    --pool)
      POOL="$2"
      shift 2
      ;;
    --payload)
      PAYLOAD="$2"
      shift 2
      ;;
    --reuse-rootfs)
      REBUILD_ROOTFS=0
      shift
      ;;
    --reuse-data-disk)
      CREATE_DATA=0
      shift
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
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
if ! command -v hdiutil >/dev/null 2>&1 || ! command -v diskutil >/dev/null 2>&1; then
  echo "hdiutil and diskutil are required by the Panthera root image builder" >&2
  exit 1
fi
if pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >/dev/null 2>&1; then
  echo "A Panthera/QEMU process is already running; stop it before this destructive smoke." >&2
  pgrep -fl 'qemu-system-x86_64|run_phase2_serial.sh' >&2 || true
  exit 1
fi

mkdir -p "${ARTIFACT_DIR}" "$(dirname "${ROOT_DISK}")" "$(dirname "${DATA_DISK}")"

if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  if [[ -f "${ROOT_DISK}" && "${FORCE}" != "1" ]]; then
    echo "Root smoke image already exists: ${ROOT_DISK}" >&2
    echo "Use --force or --reuse-rootfs." >&2
    exit 1
  fi
  PANTHERA_ROOT_SHELL=/bin/zsh \
  PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  PANTHERA_STAGE_ZFS=1 \
    bash "${PANTHERA_ROOT}/rootfs/create_hfs_root_image.sh" \
      --image "${ROOT_DISK}" \
      --force \
      --no-build-components
elif [[ ! -f "${ROOT_DISK}" ]]; then
  echo "Root smoke image not found: ${ROOT_DISK}" >&2
  exit 1
fi

if [[ "${CREATE_DATA}" == "1" ]]; then
  if [[ -f "${DATA_DISK}" && "${FORCE}" != "1" ]]; then
    echo "Data smoke image already exists: ${DATA_DISK}" >&2
    echo "Use --force or --reuse-data-disk." >&2
    exit 1
  fi
  ruby "${PANTHERA_ROOT}/tools/create_zfs_gpt_image.rb" \
    --image "${DATA_DISK}" \
    --size "${DATA_SIZE}" \
    --label PantheraZFS >/dev/null
elif [[ ! -f "${DATA_DISK}" ]]; then
  echo "Data smoke image not found: ${DATA_DISK}" >&2
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
echo PANTHERA_ZFS_DATA_ROOTDEV:$rootdev
echo PANTHERA_ZFS_DATA_CANDIDATE_COUNT:$candidate_count
echo PANTHERA_ZFS_DATA_TARGET:$target
if [ "$candidate_count" -ne 1 ] || [ -z "$target" ] || [ "$target" = "$root_path" ]; then
  echo PANTHERA_ZFS_DATA_DEVICES_BEGIN
  print -l /dev/disk*
  echo PANTHERA_ZFS_DATA_DEVICES_END
  exit 42
fi
REMOTE

read -r -d '' IMPORT_DEVICE_SNIPPET <<'REMOTE' || true
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
echo PANTHERA_ZFS_DATA_ROOTDEV:$rootdev
echo PANTHERA_ZFS_DATA_CANDIDATE_COUNT:$candidate_count
echo PANTHERA_ZFS_DATA_IMPORT_TARGET:$target
if [ "$candidate_count" -ne 1 ] || [ -z "$target" ] || [ "$target" = "$root_path" ]; then
  echo PANTHERA_ZFS_DATA_DEVICES_BEGIN
  print -l /dev/disk*
  echo PANTHERA_ZFS_DATA_DEVICES_END
  exit 45
fi
REMOTE

read -r -d '' CREATE_COMMAND <<REMOTE || true
set -e
pool='${POOL}'
payload='${PAYLOAD}'
echo PANTHERA_ZFS_DATA_CREATE_BEGIN
${CREATE_TARGET_SNIPPET}
echo PANTHERA_ZFS_DATA_ZPOOL_CREATE_ENTER
/sbin/zpool create -f -m /z "\$pool" "\$target"
echo PANTHERA_ZFS_DATA_CREATE_RC:\$?
echo "\$payload" > /z/hello.txt
/bin/cat /z/hello.txt
/sbin/sync
/sbin/umount /z
echo PANTHERA_ZFS_DATA_UNMOUNT_RC:\$?
echo PANTHERA_ZFS_DATA_ZPOOL_EXPORT_ENTER
/sbin/zpool export "\$pool"
echo PANTHERA_ZFS_DATA_EXPORT_RC:\$?
echo PANTHERA_ZFS_DATA_CREATE_OK
REMOTE

read -r -d '' IMPORT_COMMAND <<REMOTE || true
set -e
pool='${POOL}'
payload='${PAYLOAD}'
echo PANTHERA_ZFS_DATA_IMPORT_BEGIN
${IMPORT_DEVICE_SNIPPET}
echo PANTHERA_ZFS_DATA_ZPOOL_IMPORT_ENTER
/sbin/zpool import -N -d /dev "\$pool"
echo PANTHERA_ZFS_DATA_IMPORT_RC:\$?
echo PANTHERA_ZFS_DATA_ZPOOL_STATUS_ENTER
/sbin/zpool status "\$pool"
echo PANTHERA_ZFS_DATA_ZPOOL_STATUS_RC:\$?
echo PANTHERA_ZFS_DATA_ZFS_MOUNT_ENTER
/sbin/zfs mount "\$pool"
echo PANTHERA_ZFS_DATA_MOUNT_RC:\$?
echo PANTHERA_ZFS_DATA_READ_ENTER
/bin/cat /z/hello.txt
/usr/bin/grep -F -x "\$payload" /z/hello.txt
echo PANTHERA_ZFS_DATA_READ_OK
/sbin/sync
/sbin/umount /z
echo PANTHERA_ZFS_DATA_UNMOUNT_RC:\$?
echo PANTHERA_ZFS_DATA_ZPOOL_EXPORT_ENTER
/sbin/zpool export "\$pool"
echo PANTHERA_ZFS_DATA_EXPORT_RC:\$?
echo PANTHERA_ZFS_DATA_IMPORT_OK
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
    --marker "PANTHERA_ZFS_DATA_CREATE_OK"

PANTHERA_OPENSSH_REMOTE_TIMEOUT="${REMOTE_TIMEOUT}" \
PANTHERA_ROOT_SHELL=/bin/zsh \
PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration \
  bash "${PANTHERA_ROOT}/tools/smoke_openssh.sh" \
    --root-disk "${ROOT_DISK}" \
    --root-kind hfs \
    --data-disk "${DATA_DISK}" \
    --timeout "${TIMEOUT}" \
    --port "${PORT}" \
    --tag "${TAG}-import" \
    --command "${IMPORT_COMMAND}" \
    --marker "PANTHERA_ZFS_DATA_IMPORT_OK"

cat <<EOF
ZFS data-pool smoke passed:
  root image: ${ROOT_DISK}
  data image: ${DATA_DISK}
  create serial log: ${ARTIFACT_DIR}/${TAG}-create.log
  create ssh log:    ${ARTIFACT_DIR}/${TAG}-create.ssh.log
  import serial log: ${ARTIFACT_DIR}/${TAG}-import.log
  import ssh log:    ${ARTIFACT_DIR}/${TAG}-import.ssh.log
EOF
