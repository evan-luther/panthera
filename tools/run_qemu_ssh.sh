#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ROOT_KIND="${PANTHERA_ROOT_KIND:-zfs}"
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
else
  ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
fi
ROOT_DISK_SET=0
ZFS_DATASET="${PANTHERA_ZFS_BOOT:-${PANTHERA_ZFS_ROOT_DATASET:-tank/ROOT/panthera}}"
ROOTDEV="${PANTHERA_ROOTDEV:-uuid}"
PORT="${PANTHERA_QEMU_SSH_PORT:-2222}"
MONITOR_PATH="${PANTHERA_QEMU_MONITOR_PATH:-${PANTHERA_ROOT}/artifacts/qemu/panthera-ssh.monitor.sock}"
REBUILD_ROOTFS=0
RESTAGE=0
NO_REBOOT=1
DRY_RUN=0

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Starts Panthera in QEMU with host SSH access enabled.

Defaults:
  root disk: ${ROOT_DISK}
  root kind: ${ROOT_KIND}
  SSH:       ssh -p ${PORT} root@127.0.0.1
  monitor:   ${MONITOR_PATH}

Options:
  --root-disk PATH    Root disk image to boot
  --root-kind KIND    Root filesystem kind: zfs or hfs (default: ${ROOT_KIND})
  --zfs-root DATASET  ZFS boot dataset (default: ${ZFS_DATASET})
  --rootdev NAME      XNU root device for ZFS root (default: ${ROOTDEV})
  --port PORT         Host TCP port forwarded to guest port 22 (default: ${PORT})
  --monitor PATH      QEMU monitor socket path
  --rebuild-rootfs    Rebuild the default configd-owned root image first
  --restage           Restage EFI before launching QEMU
  --reboot            Allow guest reboot instead of QEMU exiting on reset
  --dry-run           Print the launch command without starting QEMU
  --help              Show this help
EOF
}

quote_cmd() {
  local arg
  for arg in "$@"; do
    printf '%q ' "${arg}"
  done
  printf '\n'
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --root-disk)
      ROOT_DISK="$2"
      ROOT_DISK_SET=1
      shift 2
      ;;
    --root-kind)
      ROOT_KIND="$2"
      shift 2
      ;;
    --zfs-root)
      ZFS_DATASET="$2"
      shift 2
      ;;
    --rootdev)
      ROOTDEV="$2"
      shift 2
      ;;
    --port)
      PORT="$2"
      shift 2
      ;;
    --monitor)
      MONITOR_PATH="$2"
      shift 2
      ;;
    --rebuild-rootfs)
      REBUILD_ROOTFS=1
      shift
      ;;
    --restage)
      RESTAGE=1
      shift
      ;;
    --reboot)
      NO_REBOOT=0
      shift
      ;;
    --dry-run)
      DRY_RUN=1
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

case "${ROOT_KIND}" in
  zfs|hfs)
    ;;
  *)
    echo "--root-kind must be zfs or hfs" >&2
    exit 2
    ;;
esac

if [[ "${ROOT_DISK_SET}" == "0" ]]; then
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    ROOT_DISK="${PANTHERA_ZFS_ROOT_DISK:-${PANTHERA_ROOT}/images/qemu/panthera-zfs-root.img}"
  else
    ROOT_DISK="${PANTHERA_ROOT}/images/qemu/panthera-root.img"
  fi
fi

if [[ ! -f "${ROOT_DISK}" && "${REBUILD_ROOTFS}" != "1" ]]; then
  echo "root disk not found: ${ROOT_DISK}" >&2
  echo "Use --rebuild-rootfs to create it." >&2
  exit 1
fi

if [[ "${PORT}" != "0" ]] && nc -z 127.0.0.1 "${PORT}" >/dev/null 2>&1; then
  echo "port ${PORT} is already in use" >&2
  exit 1
fi

mkdir -p "$(dirname "${MONITOR_PATH}")"

if [[ "${REBUILD_ROOTFS}" == "1" ]]; then
  if [[ "${ROOT_KIND}" == "zfs" ]]; then
    rebuild_cmd=(
      env
      "PATH=/usr/local/zfs/bin:${PATH}"
      PANTHERA_ROOT_SHELL=/bin/zsh
      PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration
      bash rootfs/create_zfs_root_image.sh
        --root-disk "${ROOT_DISK}"
        --dataset "${ZFS_DATASET}"
        --force
    )
  else
    rebuild_cmd=(
      env
      PANTHERA_ROOT_SHELL=/bin/zsh
      PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration
      bash rootfs/create_hfs_root_image.sh --image "${ROOT_DISK}" --force --no-build-components
    )
  fi
  if [[ "${DRY_RUN}" == "1" ]]; then
    echo "Rebuild command:"
    quote_cmd "${rebuild_cmd[@]}"
  else
    (cd "${PANTHERA_ROOT}" && "${rebuild_cmd[@]}")
  fi
fi

qemu_args=(--root-disk "${ROOT_DISK}")
if [[ "${NO_REBOOT}" == "1" ]]; then
  qemu_args+=(--no-reboot)
fi
qemu_args+=(--ssh-port "${PORT}")
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  qemu_args+=(--restage --zfs-root "${ZFS_DATASET}" --rootdev "${ROOTDEV}")
elif [[ "${RESTAGE}" == "1" ]]; then
  qemu_args+=(--restage)
fi

launch_env=(
  env
  "PANTHERA_QEMU_MONITOR_PATH=${MONITOR_PATH}"
  "PANTHERA_ROOT_DISK=${ROOT_DISK}"
  "PANTHERA_FIX_CACHE_OWNERSHIP=$([[ "${ROOT_KIND}" == "zfs" ]] && echo 0 || echo 1)"
)
if [[ "${ROOT_KIND}" == "zfs" ]]; then
  launch_env+=(
    "PANTHERA_ZFS_BOOT=${ZFS_DATASET}"
    "PANTHERA_ROOTDEV=${ROOTDEV}"
  )
fi

launch_cmd=(
  "${launch_env[@]}"
  boot/qemu/run_phase2_serial.sh
  "${qemu_args[@]}"
)

cat <<EOF
Panthera QEMU SSH launcher
  root disk: ${ROOT_DISK}
  root kind: ${ROOT_KIND}
  monitor:   ${MONITOR_PATH}
  ssh:       ssh -p ${PORT} root@127.0.0.1

Password: press Enter at the password prompt.
EOF

if [[ "${DRY_RUN}" == "1" ]]; then
  echo
  echo "Launch command:"
  quote_cmd "${launch_cmd[@]}"
  exit 0
fi

cd "${PANTHERA_ROOT}"
exec "${launch_cmd[@]}"
