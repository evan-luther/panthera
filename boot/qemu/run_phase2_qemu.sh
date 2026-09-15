#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
STAGE_SCRIPT="${PANTHERA_ROOT}/boot/efi/stage_phase2_efi.sh"

KERNEL_PATH="${PANTHERA_ROOT}/BUILD/obj/RELEASE_X86_64/kernel"
STAGING_DIR="${PANTHERA_ROOT}/boot/efi/staging"
BOOT_EFI_PATH="${PANTHERA_BOOT_EFI:-}"
RESTAGE=0
SSH_FORWARD_PORT="${PANTHERA_SSH_PORT:-2222}"
NO_REBOOT="${PANTHERA_QEMU_NO_REBOOT:-0}"
ROOT_DISK_PATH="${PANTHERA_ROOT_DISK:-}"
ROOT_DISK_FORMAT="${PANTHERA_ROOT_DISK_FORMAT:-raw}"
ROOT_DISK_SNAPSHOT="${PANTHERA_ROOT_DISK_SNAPSHOT:-off}"
ROOT_DISK_CACHE="${PANTHERA_ROOT_DISK_CACHE:-writethrough}"
ROOT_CONTROLLER="${PANTHERA_ROOT_CONTROLLER:-piix4-ide}"
ZFS_BOOT_DATASET="${PANTHERA_ZFS_BOOT:-}"
ROOTDEV_ARG="${PANTHERA_ROOTDEV:-}"
DATA_DISK_PATH="${PANTHERA_DATA_DISK:-}"
DATA_DISK_FORMAT="${PANTHERA_DATA_DISK_FORMAT:-raw}"
DATA_DISK_SNAPSHOT="${PANTHERA_DATA_DISK_SNAPSHOT:-off}"
DATA_DISK_CACHE="${PANTHERA_DATA_DISK_CACHE:-writethrough}"
DATA_DISK_POSITION="${PANTHERA_DATA_DISK_POSITION:-primary-slave}"

if [[ "${ROOT_DISK_CACHE}" != "writethrough" || "${DATA_DISK_CACHE}" != "writethrough" ]]; then
  effective_boot_args="${PANTHERA_BOOT_ARGS:-}"
  if [[ -f "${STAGING_DIR}/EFI/PANTHERA/boot-args.txt" ]]; then
    effective_boot_args+=" $(cat "${STAGING_DIR}/EFI/PANTHERA/boot-args.txt")"
  fi
  if [[ " ${effective_boot_args} " != *" panthera_ata_noflush=0 "* ]]; then
    echo "Error: QEMU disk cache must be 'writethrough' unless panthera_ata_noflush=0 is set in boot args (root: ${ROOT_DISK_CACHE}, data: ${DATA_DISK_CACHE})" >&2
    exit 2
  fi
fi
COCOA_DISPLAY_OPTS="${PANTHERA_QEMU_COCOA_OPTS:-cocoa,full-grab=off,show-cursor=on,left-command-key=on}"
DISPLAY_BACKEND="${PANTHERA_QEMU_DISPLAY_BACKEND:-cocoa}"
VNC_DISPLAY="${PANTHERA_QEMU_VNC_DISPLAY:-127.0.0.1:1}"
VNC_OPTS="${PANTHERA_QEMU_VNC_OPTS:-}"
MONITOR_PATH="${PANTHERA_QEMU_MONITOR_PATH:-}"
ENABLE_USB_HID="${PANTHERA_ENABLE_USB_HID:-0}"
GRAPHICAL_STDIO_SERIAL="${PANTHERA_QEMU_GRAPHICAL_STDIO_SERIAL:-0}"

QEMU_BIN="${QEMU_BIN:-qemu-system-x86_64}"
FIRMWARE_CODE="${PANTHERA_EDK2_CODE:-/opt/homebrew/opt/qemu/share/qemu/edk2-x86_64-code.fd}"
FIRMWARE_VARS_TEMPLATE="${PANTHERA_EDK2_VARS_TEMPLATE:-/opt/homebrew/opt/qemu/share/qemu/edk2-i386-vars.fd}"
FIRMWARE_VARS="${PANTHERA_ROOT}/images/edk2-x86_64-vars.fd"
DEBUGCON_LOG="${PANTHERA_ROOT}/images/qemu/debugcon.log"
SERIAL_LOG="${PANTHERA_QEMU_SERIAL_LOG:-${PANTHERA_ROOT}/artifacts/qemu-serial.log}"
CACHE_OWNERSHIP_FIXER="${PANTHERA_ROOT}/tools/fix_cache_ownership.py"
CACHE_OWNERSHIP_PREFLIGHT="${PANTHERA_FIX_CACHE_OWNERSHIP:-1}"
QEMU_CPU="${PANTHERA_QEMU_CPU:-Haswell}"
QEMU_SMP="${PANTHERA_QEMU_SMP:-4}"
QEMU_MEM="${PANTHERA_QEMU_MEM:-4G}"
QEMU_ICOUNT="${PANTHERA_QEMU_ICOUNT:-}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --kernel PATH    Path to the built XNU kernel
  --staging DIR    EFI staging directory
  --boot-efi PATH  Path to a Darwin-capable BOOTX64.EFI
  --ssh-port PORT  Host TCP port forwarded to guest port 22 (use 0 to disable)
  --root-disk PATH Path to a QEMU root disk image
  --root-format FMT Root disk format passed to QEMU (default: raw)
  --root-controller TYPE
                  Root disk controller: piix4-ide, piix3-ide, or ich9-ahci
  --zfs-root DATASET
                  Restage EFI for a ZFS root dataset before launch
  --rootdev NAME  Root device name to pass to XNU, for example disk0s1
  --data-disk PATH Secondary data disk image attached to the root controller
  --data-format FMT
                  Secondary data disk format passed to QEMU (default: raw)
  --data-position POS
                  IDE data disk position for piix controllers:
                  primary-slave or secondary-master
                  (default: ${DATA_DISK_POSITION})
  --restage        Rebuild the EFI staging tree before launch
  --no-reboot      Exit QEMU instead of rebooting on guest reset
  --enable-usb-hid Enable opt-in USB/HID boot staging and QEMU USB keyboard hardware
  --help           Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --kernel)
      KERNEL_PATH="$2"
      shift 2
      ;;
    --staging)
      STAGING_DIR="$2"
      shift 2
      ;;
    --boot-efi)
      BOOT_EFI_PATH="$2"
      shift 2
      ;;
    --ssh-port)
      SSH_FORWARD_PORT="$2"
      shift 2
      ;;
    --root-disk)
      ROOT_DISK_PATH="$2"
      shift 2
      ;;
    --root-format)
      ROOT_DISK_FORMAT="$2"
      shift 2
      ;;
    --root-controller)
      ROOT_CONTROLLER="$2"
      shift 2
      ;;
    --zfs-root)
      ZFS_BOOT_DATASET="$2"
      shift 2
      ;;
    --rootdev)
      ROOTDEV_ARG="$2"
      shift 2
      ;;
    --data-disk)
      DATA_DISK_PATH="$2"
      shift 2
      ;;
    --data-format)
      DATA_DISK_FORMAT="$2"
      shift 2
      ;;
    --data-position)
      DATA_DISK_POSITION="$2"
      shift 2
      ;;
    --restage)
      RESTAGE=1
      shift
      ;;
    --no-reboot)
      NO_REBOOT=1
      shift
      ;;
    --enable-usb-hid)
      ENABLE_USB_HID=1
      shift
      ;;
    --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if ! command -v "${QEMU_BIN}" >/dev/null 2>&1; then
  echo "QEMU binary not found: ${QEMU_BIN}" >&2
  exit 1
fi

if [[ ! -f "${FIRMWARE_CODE}" ]]; then
  echo "edk2 code firmware not found: ${FIRMWARE_CODE}" >&2
  exit 1
fi

if [[ ! -f "${FIRMWARE_VARS_TEMPLATE}" ]]; then
  echo "edk2 vars template not found: ${FIRMWARE_VARS_TEMPLATE}" >&2
  exit 1
fi

if [[ ${RESTAGE} -eq 1 || ! -d "${STAGING_DIR}" ]]; then
  stage_args=(
    --kernel "${KERNEL_PATH}"
    --staging "${STAGING_DIR}"
  )
  if [[ -n "${BOOT_EFI_PATH}" ]]; then
    stage_args+=(--boot-efi "${BOOT_EFI_PATH}")
  fi
  if [[ "${ENABLE_USB_HID}" == "1" ]]; then
    stage_args+=(--enable-usb-hid)
  fi
  if [[ -n "${ZFS_BOOT_DATASET}" ]]; then
    stage_args+=(--zfs-root "${ZFS_BOOT_DATASET}")
  fi
  if [[ -n "${ROOTDEV_ARG}" ]]; then
    stage_args+=(--rootdev "${ROOTDEV_ARG}")
  fi
  "${STAGE_SCRIPT}" "${stage_args[@]}"
fi

if [[ ! -f "${FIRMWARE_VARS}" ]]; then
  mkdir -p "$(dirname "${FIRMWARE_VARS}")"
  cp "${FIRMWARE_VARS_TEMPLATE}" "${FIRMWARE_VARS}"
fi

if [[ ! -f "${STAGING_DIR}/EFI/BOOT/BOOTX64.EFI" ]]; then
  echo "Missing ${STAGING_DIR}/EFI/BOOT/BOOTX64.EFI" >&2
  echo "Stage a Darwin-capable loader with --boot-efi PATH or PANTHERA_BOOT_EFI." >&2
  exit 1
fi

if [[ -n "${ROOT_DISK_PATH}" && ! -f "${ROOT_DISK_PATH}" ]]; then
  echo "Root disk not found: ${ROOT_DISK_PATH}" >&2
  exit 1
fi

if [[ -n "${DATA_DISK_PATH}" && ! -f "${DATA_DISK_PATH}" ]]; then
  echo "Data disk not found: ${DATA_DISK_PATH}" >&2
  exit 1
fi

if [[ -n "${ROOT_DISK_PATH}" \
   && "${ROOT_DISK_FORMAT}" == "raw" \
   && "${CACHE_OWNERSHIP_PREFLIGHT}" != "0" \
   && "${ZFS_BOOT_DATASET}" == "" \
   && -f "${CACHE_OWNERSHIP_FIXER}" \
   && "$(command -v python3 || true)" != "" ]]; then
  python3 "${CACHE_OWNERSHIP_FIXER}" --quiet "${ROOT_DISK_PATH}"
fi

mkdir -p "$(dirname "${DEBUGCON_LOG}")"

if [[ -n "${MONITOR_PATH}" ]]; then
  mkdir -p "$(dirname "${MONITOR_PATH}")"
  rm -f "${MONITOR_PATH}"
fi

if [[ -z "${PANTHERA_NOGRAPHIC:-}" ]]; then
  mkdir -p "$(dirname "${SERIAL_LOG}")"
  rm -f "${SERIAL_LOG}"
fi

netdev_args=(-netdev user,id=net0)
if [[ "${SSH_FORWARD_PORT}" != "0" ]]; then
  netdev_args=(-netdev "user,id=net0,hostfwd=tcp::${SSH_FORWARD_PORT}-:22")
fi

qemu_extra_args=()
if [[ "${NO_REBOOT}" == "1" ]]; then
  qemu_extra_args=(-no-reboot)
fi
if [[ -n "${PANTHERA_PCAP:-}" ]]; then
  qemu_extra_args+=(-object "filter-dump,id=f0,netdev=net0,file=${PANTHERA_PCAP}")
fi

display_args=()
if [[ -z "${PANTHERA_NOGRAPHIC:-}" ]]; then
  case "${DISPLAY_BACKEND}" in
    cocoa)
      display_args=(-display "${COCOA_DISPLAY_OPTS}")
      ;;
    vnc)
      if [[ -n "${VNC_OPTS}" ]]; then
        display_args=(-display "vnc=${VNC_DISPLAY},${VNC_OPTS}")
      else
        display_args=(-display "vnc=${VNC_DISPLAY}")
      fi
      ;;
    none)
      display_args=(-display none)
      ;;
    *)
      echo "Unsupported display backend: ${DISPLAY_BACKEND}" >&2
      exit 1
      ;;
  esac
fi

root_disk_args=()
if [[ -n "${ROOT_DISK_PATH}" ]]; then
  case "${ROOT_CONTROLLER}" in
    piix4-ide)
      root_disk_args=(
        -device piix4-ide,id=rootide
        -drive "if=none,id=rootdisk,format=${ROOT_DISK_FORMAT},cache=${ROOT_DISK_CACHE},snapshot=${ROOT_DISK_SNAPSHOT},file=${ROOT_DISK_PATH}"
        -device ide-hd,drive=rootdisk,bus=rootide.0,unit=0
      )
      ;;
    piix3-ide)
      root_disk_args=(
        -device piix3-ide,id=rootide
        -drive "if=none,id=rootdisk,format=${ROOT_DISK_FORMAT},cache=${ROOT_DISK_CACHE},snapshot=${ROOT_DISK_SNAPSHOT},file=${ROOT_DISK_PATH}"
        -device ide-hd,drive=rootdisk,bus=rootide.0,unit=0
      )
      ;;
    ich9-ahci)
      root_disk_args=(
        -device ich9-ahci,id=rootahci
        -drive "if=none,id=rootdisk,format=${ROOT_DISK_FORMAT},cache=${ROOT_DISK_CACHE},snapshot=${ROOT_DISK_SNAPSHOT},file=${ROOT_DISK_PATH}"
        -device ide-hd,drive=rootdisk,bus=rootahci.0
      )
      ;;
    *)
      echo "Unsupported root controller: ${ROOT_CONTROLLER}" >&2
      exit 1
      ;;
  esac
fi

data_disk_args=()
if [[ -n "${DATA_DISK_PATH}" ]]; then
  if [[ -z "${ROOT_DISK_PATH}" ]]; then
    echo "--data-disk requires --root-disk so the storage controller is defined" >&2
    exit 1
  fi
  case "${ROOT_CONTROLLER}" in
    piix4-ide|piix3-ide)
      case "${DATA_DISK_POSITION}" in
        primary-slave)
          data_disk_args=(
            -drive "if=none,id=datadisk,format=${DATA_DISK_FORMAT},cache=${DATA_DISK_CACHE},snapshot=${DATA_DISK_SNAPSHOT},file=${DATA_DISK_PATH}"
            -device ide-hd,drive=datadisk,bus=rootide.0,unit=1
          )
          ;;
        secondary-master)
          data_disk_args=(
            -drive "if=none,id=datadisk,format=${DATA_DISK_FORMAT},cache=${DATA_DISK_CACHE},snapshot=${DATA_DISK_SNAPSHOT},file=${DATA_DISK_PATH}"
            -device ide-hd,drive=datadisk,bus=rootide.1,unit=0
          )
          ;;
        *)
          echo "Unsupported piix data disk position: ${DATA_DISK_POSITION}" >&2
          echo "Supported values: primary-slave, secondary-master" >&2
          exit 1
          ;;
      esac
      ;;
    ich9-ahci)
      data_disk_args=(
        -drive "if=none,id=datadisk,format=${DATA_DISK_FORMAT},cache=${DATA_DISK_CACHE},snapshot=${DATA_DISK_SNAPSHOT},file=${DATA_DISK_PATH}"
        -device ide-hd,drive=datadisk,bus=rootahci.1
      )
      ;;
    *)
      echo "Unsupported root controller for data disk: ${ROOT_CONTROLLER}" >&2
      exit 1
      ;;
  esac
fi

machine_arg="q35"
usb_input_args=()
if [[ "${ENABLE_USB_HID}" == "1" ]]; then
  machine_arg="q35,i8042=off"
  usb_input_args=(
    -device ich9-usb-ehci1,id=usb
    -device ich9-usb-uhci1,masterbus=usb.0,firstport=0,multifunction=on
    -device ich9-usb-uhci2,masterbus=usb.0,firstport=2
    -device ich9-usb-uhci3,masterbus=usb.0,firstport=4
    -device usb-kbd,bus=usb.0
  )
fi

monitor_args=()
if [[ -n "${PANTHERA_NOGRAPHIC:-}" ]]; then
  monitor_args=(-serial mon:stdio)
  if [[ -n "${MONITOR_PATH}" ]]; then
    monitor_args=(
      -monitor "unix:${MONITOR_PATH},server=on,wait=off"
      -serial stdio
    )
  fi
else
  if [[ "${GRAPHICAL_STDIO_SERIAL}" == "1" ]]; then
    monitor_args=(-serial mon:stdio)
  else
    monitor_args=(-monitor none -serial "file:${SERIAL_LOG}")
  fi
  if [[ -n "${MONITOR_PATH}" ]]; then
    monitor_args=(
      -monitor "unix:${MONITOR_PATH},server=on,wait=off"
      -serial "file:${SERIAL_LOG}"
    )
  fi
fi

exec "${QEMU_BIN}" \
  -M "${machine_arg}" \
  -cpu "${QEMU_CPU}" \
  -smp "${QEMU_SMP}" \
  -m "${QEMU_MEM}" \
  ${QEMU_ICOUNT:+-icount "${QEMU_ICOUNT}"} \
  -drive if=pflash,format=raw,readonly=on,file="${FIRMWARE_CODE}" \
  -drive if=pflash,format=raw,file="${FIRMWARE_VARS}" \
  -drive if=virtio,format=raw,file=fat:rw:"${STAGING_DIR}" \
  ${root_disk_args[@]+"${root_disk_args[@]}"} \
  ${data_disk_args[@]+"${data_disk_args[@]}"} \
  ${netdev_args[@]+"${netdev_args[@]}"} \
  ${display_args[@]+"${display_args[@]}"} \
  ${usb_input_args[@]+"${usb_input_args[@]}"} \
  -device rtl8139,netdev=net0 \
  -debugcon "file:${DEBUGCON_LOG}" \
  ${monitor_args[@]+"${monitor_args[@]}"} \
  ${qemu_extra_args[@]+"${qemu_extra_args[@]}"} \
  ${PANTHERA_NOGRAPHIC:+-nographic}
