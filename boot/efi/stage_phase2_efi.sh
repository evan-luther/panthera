#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

KERNEL_PATH="${PANTHERA_ROOT}/BUILD/obj/RELEASE_X86_64/kernel"
STAGING_DIR="${PANTHERA_ROOT}/boot/efi/staging"
BOOT_EFI_PATH="${PANTHERA_BOOT_EFI:-}"
DEFAULT_BOOT_EFI="${PANTHERA_ROOT}/boot/efi/loader/out/BOOTX64.EFI"
KEXT_BUILD_ROOT="${PANTHERA_ROOT}/kexts/OpenIOKit/build"
SYSTEM_KEXT_BUILD_ROOT="${PANTHERA_ROOT}/BUILD/obj/RELEASE_X86_64/config/System.kext"
# Fallback for ATA IRQ problems: append panthera_ata_nodma=1 panthera_ata_polled=1 via PANTHERA_BOOT_ARGS.
DEFAULT_BOOT_ARGS="serial=3 dataconstro=0 kernelmanagerd=0 panthera_ata_noflush=1"
ENABLE_IOGRAPHICS="${PANTHERA_ENABLE_IOGRAPHICS:-0}"
ENABLE_USB_HID="${PANTHERA_ENABLE_USB_HID:-1}"
ENABLE_ZFS_SPL="${PANTHERA_ENABLE_ZFS_SPL:-0}"
ENABLE_ZFS="${PANTHERA_ENABLE_ZFS:-0}"
ZFS_BOOT_DATASET="${PANTHERA_ZFS_BOOT:-}"
ROOTDEV_ARG="${PANTHERA_ROOTDEV:-}"
CUSTOM_BOOT_ARGS="${PANTHERA_BOOT_ARGS:-}"
BOOT_KEXTS=(
  AppleAPIC
  AppleSMBIOS
  AppleI386PCI
  AppleI386GenericPlatform
  IOACPIFamily
  IOPCIFamily
  IONetworkingFamily
  AppleRTL8139Ethernet
  IOHIDFamily
  IOHIDSystem
  IOStorageFamily
  HFSEncodings
  HFS
  IOATAFamily
  AppleIntelPIIXATA
  PantheraATAStorage
)

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --kernel PATH    Path to the built XNU kernel
  --staging DIR    EFI staging directory
  --boot-efi PATH  Path to a Darwin-capable BOOTX64.EFI
  --kext-build DIR Path to built OpenIOKit kext bundles
  --system-kext DIR Path to generated XNU System.kext pseudo-kext tree
  --enable-iographics
                   Stage IOGraphicsFamily and write opt-in EFI/PANTHERA/boot-args.txt
  --enable-usb-hid
                   Stage the USB/HID keyboard stack and write boot args
  --enable-zfs-spl
                   Stage spl.kext only. This is the first ZFS bring-up gate;
                   it does not stage zfs.kext or change the root filesystem.
  --enable-zfs
                   Stage spl.kext and zfs.kext, and write opt-in loader boot
                   args. This does not change the root filesystem.
  --zfs-root DATASET
                   Stage ZFS and add the OpenZFS/Darwin zfs_boot=DATASET
                   boot argument for root dataset selection.
  --rootdev NAME
                   Override XNU root matching. ZFS defaults to uuid (the
                   published bootfs media), not an unstable disk number.
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
    --kext-build)
      KEXT_BUILD_ROOT="$2"
      shift 2
      ;;
    --system-kext)
      SYSTEM_KEXT_BUILD_ROOT="$2"
      shift 2
      ;;
    --enable-iographics)
      ENABLE_IOGRAPHICS=1
      shift
      ;;
    --enable-usb-hid)
      ENABLE_USB_HID=1
      shift
      ;;
    --enable-zfs-spl)
      ENABLE_ZFS_SPL=1
      shift
      ;;
    --enable-zfs)
      ENABLE_ZFS=1
      ENABLE_ZFS_SPL=1
      shift
      ;;
    --zfs-root)
      ZFS_BOOT_DATASET="$2"
      ENABLE_ZFS=1
      ENABLE_ZFS_SPL=1
      shift 2
      ;;
    --rootdev)
      ROOTDEV_ARG="$2"
      shift 2
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

FINAL_BOOT_ARGS="${CUSTOM_BOOT_ARGS:-${DEFAULT_BOOT_ARGS}}"
if [[ "${ENABLE_IOGRAPHICS}" == "1" ]]; then
  BOOT_KEXTS+=(IOGraphicsFamily)
  if [[ " ${FINAL_BOOT_ARGS} " != *" panthera_iog=1 "* ]]; then
    FINAL_BOOT_ARGS+=" panthera_iog=1"
  fi
  if [[ " ${FINAL_BOOT_ARGS} " != *" iogdelay_modstart=1 "* ]]; then
    FINAL_BOOT_ARGS+=" iogdelay_modstart=1"
  fi
  if [[ " ${FINAL_BOOT_ARGS} " != *" iogdelay_fbstart=1 "* ]]; then
    FINAL_BOOT_ARGS+=" iogdelay_fbstart=1"
  fi
fi

if [[ "${ENABLE_USB_HID}" == "1" ]]; then
  BOOT_KEXTS+=(
    IOUSBFamily
    AppleUSBEHCI
    AppleUSBUHCI
    AppleUSBHub
    IOUSBCompositeDriver
    IOUSBHIDDriver
  )
  if [[ " ${FINAL_BOOT_ARGS} " != *" panthera_usbhid=1 "* ]]; then
    FINAL_BOOT_ARGS+=" panthera_usbhid=1"
  fi
fi

if [[ "${ENABLE_ZFS_SPL}" == "1" ]]; then
  BOOT_KEXTS+=(corecrypto spl)
  if [[ " ${FINAL_BOOT_ARGS} " != *" panthera_zfs_spl=1 "* ]]; then
    FINAL_BOOT_ARGS+=" panthera_zfs_spl=1"
  fi
fi

if [[ "${ENABLE_ZFS}" == "1" ]]; then
  BOOT_KEXTS+=(zfs)
  if [[ " ${FINAL_BOOT_ARGS} " != *" panthera_zfs=1 "* ]]; then
    FINAL_BOOT_ARGS+=" panthera_zfs=1"
  fi
fi

if [[ -n "${ZFS_BOOT_DATASET}" ]]; then
  if [[ " ${FINAL_BOOT_ARGS} " != *" zfs_boot="* ]]; then
    FINAL_BOOT_ARGS+=" zfs_boot=${ZFS_BOOT_DATASET}"
  fi
  if [[ -z "${ROOTDEV_ARG}" ]]; then
    ROOTDEV_ARG=uuid
  fi
  if [[ "${ROOTDEV_ARG}" == uuid && " ${FINAL_BOOT_ARGS} " != *" boot-uuid="* ]]; then
    # OpenZFS zfs_vfs_uuid_gen uses UUIDv3 with this namespace.
    BOOT_UUID="$(python3 -c 'import sys, uuid; print(uuid.uuid3(uuid.UUID("50670853-FBD2-4EC3-9802-73D847BF7E62"), sys.argv[1]))' "${ZFS_BOOT_DATASET}")"
    FINAL_BOOT_ARGS+=" boot-uuid=${BOOT_UUID}"
  fi
fi

if [[ -n "${ROOTDEV_ARG}" ]]; then
  if [[ " ${FINAL_BOOT_ARGS} " != *" rootdev="* ]]; then
    FINAL_BOOT_ARGS+=" rootdev=${ROOTDEV_ARG}"
  fi
fi

if [[ -z "${BOOT_EFI_PATH}" && -f "${DEFAULT_BOOT_EFI}" ]]; then
  BOOT_EFI_PATH="${DEFAULT_BOOT_EFI}"
fi

if [[ ! -f "${KERNEL_PATH}" ]]; then
  echo "Kernel not found: ${KERNEL_PATH}" >&2
  exit 1
fi

if [[ -n "${BOOT_EFI_PATH}" && ! -f "${BOOT_EFI_PATH}" ]]; then
  echo "BOOTX64.EFI not found: ${BOOT_EFI_PATH}" >&2
  exit 1
fi

if [[ ! -f "${SYSTEM_KEXT_BUILD_ROOT}/Info.plist" ]]; then
  echo "System.kext Info.plist not found: ${SYSTEM_KEXT_BUILD_ROOT}/Info.plist" >&2
  exit 1
fi

rm -rf "${STAGING_DIR}"
mkdir -p \
  "${STAGING_DIR}/EFI/BOOT" \
  "${STAGING_DIR}/EFI/PANTHERA" \
  "${STAGING_DIR}/System/Library/Extensions" \
  "${STAGING_DIR}/System/Library/Kernels"

cp "${KERNEL_PATH}" "${STAGING_DIR}/EFI/PANTHERA/kernel"
cp "${KERNEL_PATH}" "${STAGING_DIR}/System/Library/Kernels/kernel"
cp "${KERNEL_PATH}" "${STAGING_DIR}/mach_kernel"

stage_kext_bundle() {
  local kext_name="$1"
  local kext_bundle="${KEXT_BUILD_ROOT}/${kext_name}/${kext_name}.kext"
  local kext_executable="${kext_bundle}/Contents/MacOS/${kext_name}"
  local staged_bundle="${STAGING_DIR}/System/Library/Extensions/${kext_name}.kext"

  if [[ ! -d "${kext_bundle}" ]]; then
    echo "Boot-critical kext bundle not found: ${kext_bundle}" >&2
    exit 1
  fi

  if [[ ! -f "${kext_executable}" ]]; then
    echo "Boot-critical kext executable not found: ${kext_executable}" >&2
    exit 1
  fi

  if [[ ! -s "${kext_executable}" ]]; then
    echo "Boot-critical kext executable is empty: ${kext_executable}" >&2
    exit 1
  fi

  cp -R "${kext_bundle}" "${STAGING_DIR}/System/Library/Extensions/"
  if [[ -f "${kext_bundle}/Contents/Info.plist" ]]; then
    cp -f "${kext_bundle}/Contents/Info.plist" "${staged_bundle}/Contents/Info.plist"
  fi
  cp -f "${kext_executable}" "${staged_bundle}/Contents/MacOS/${kext_name}"
}

for kext_name in "${BOOT_KEXTS[@]}"; do
  stage_kext_bundle "${kext_name}"
done

mkdir -p "${STAGING_DIR}/System/Library/Extensions/System.kext"
cp "${SYSTEM_KEXT_BUILD_ROOT}/Info.plist" \
  "${STAGING_DIR}/System/Library/Extensions/System.kext/Info.plist"

if [[ ! -d "${SYSTEM_KEXT_BUILD_ROOT}/PlugIns" ]]; then
  echo "System.kext PlugIns directory not found: ${SYSTEM_KEXT_BUILD_ROOT}/PlugIns" >&2
  exit 1
fi

cp -R "${SYSTEM_KEXT_BUILD_ROOT}/PlugIns" \
  "${STAGING_DIR}/System/Library/Extensions/System.kext/"

while IFS= read -r system_plist; do
  if ! plutil -remove CFBundleExecutable "${system_plist}" >/dev/null 2>&1; then
    :
  fi
done < <(find "${STAGING_DIR}/System/Library/Extensions/System.kext" -name Info.plist | sort)

if [[ -n "${BOOT_EFI_PATH}" ]]; then
  cp "${BOOT_EFI_PATH}" "${STAGING_DIR}/EFI/BOOT/BOOTX64.EFI"
fi

if [[ -f "${STAGING_DIR}/EFI/BOOT/BOOTX64.EFI" ]]; then
  cat > "${STAGING_DIR}/startup.nsh" <<'EOF'
\EFI\BOOT\BOOTX64.EFI
EOF
else
  cat > "${STAGING_DIR}/startup.nsh" <<'EOF'
echo Panthera Phase 2 staging is present.
echo Missing EFI\BOOT\BOOTX64.EFI.
echo Provide a Darwin-capable loader and rerun the staging step.
EOF
fi

if [[ "${FINAL_BOOT_ARGS}" != "${DEFAULT_BOOT_ARGS}" || -n "${CUSTOM_BOOT_ARGS}" ]]; then
  printf '%s\n' "${FINAL_BOOT_ARGS}" > "${STAGING_DIR}/EFI/PANTHERA/boot-args.txt"
fi

cat > "${STAGING_DIR}/EFI/PANTHERA/README.txt" <<EOF
Panthera Phase 2 EFI staging

Kernel sources:
- EFI/PANTHERA/kernel
- System/Library/Kernels/kernel
- mach_kernel

Boot-critical kext staging:
- System/Library/Extensions/AppleAPIC.kext
- System/Library/Extensions/AppleSMBIOS.kext
- System/Library/Extensions/AppleI386PCI.kext
- System/Library/Extensions/AppleI386GenericPlatform.kext
- System/Library/Extensions/IOACPIFamily.kext
- System/Library/Extensions/IOPCIFamily.kext
- System/Library/Extensions/IONetworkingFamily.kext
- System/Library/Extensions/AppleRTL8139Ethernet.kext
- System/Library/Extensions/IOHIDFamily.kext
- System/Library/Extensions/IOHIDSystem.kext
- System/Library/Extensions/IOStorageFamily.kext
- System/Library/Extensions/HFSEncodings.kext
- System/Library/Extensions/HFS.kext
- System/Library/Extensions/IOATAFamily.kext
- System/Library/Extensions/AppleIntelPIIXATA.kext
- System/Library/Extensions/PantheraATAStorage.kext
- System/Library/Extensions/corecrypto.kext (only when ZFS SPL staging is enabled)
- System/Library/Extensions/spl.kext (only when ZFS SPL staging is enabled)
- System/Library/Extensions/zfs.kext (only when full ZFS staging is enabled)
- System/Library/Extensions/System.kext
- System/Library/Extensions/System.kext/PlugIns/BSDKernel.kext
- System/Library/Extensions/System.kext/PlugIns/IOKit.kext
- System/Library/Extensions/System.kext/PlugIns/Libkern.kext
- System/Library/Extensions/System.kext/PlugIns/Mach.kext
- System/Library/Extensions/System.kext/PlugIns/Private.kext
- System/Library/Extensions/System.kext/PlugIns/Unsupported.kext

Status:
- Kernel build is complete.
- The staging tree includes the current boot-critical platform, network, input, and storage kext bundles.
- Optional boot args file: $(if [[ -f "${STAGING_DIR}/EFI/PANTHERA/boot-args.txt" ]]; then echo "EFI/PANTHERA/boot-args.txt"; else echo "not staged"; fi)
- Optional IOGraphicsFamily staging: $(if [[ "${ENABLE_IOGRAPHICS}" == "1" ]]; then echo "enabled"; else echo "disabled"; fi)
- Optional USB/HID staging: $(if [[ "${ENABLE_USB_HID}" == "1" ]]; then echo "enabled"; else echo "disabled"; fi)
- Optional ZFS SPL staging: $(if [[ "${ENABLE_ZFS_SPL}" == "1" ]]; then echo "enabled"; else echo "disabled"; fi)
- Optional ZFS staging: $(if [[ "${ENABLE_ZFS}" == "1" ]]; then echo "enabled"; else echo "disabled"; fi)
- Optional ZFS root dataset: $(if [[ -n "${ZFS_BOOT_DATASET}" ]]; then echo "${ZFS_BOOT_DATASET}"; else echo "not selected"; fi)
- Optional rootdev override: $(if [[ -n "${ROOTDEV_ARG}" ]]; then echo "${ROOTDEV_ARG}"; else echo "not selected"; fi)
- A Darwin-capable EFI/BOOT/BOOTX64.EFI loader is still required to boot it.
EOF

echo "Staged Panthera EFI tree at: ${STAGING_DIR}"
