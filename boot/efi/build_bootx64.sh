#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

OUT_DIR="${PANTHERA_ROOT}/boot/efi/loader/out"
EFI_PATH="${OUT_DIR}/BOOTX64.EFI"
STAGING_DIR=""

CC="${CC:-clang}"
LLD_LINK="${PANTHERA_LLD_LINK:-}"

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Options:
  --out-dir DIR    Output directory for BOOTX64.obj / BOOTX64.EFI
  --staging DIR    Copy BOOTX64.EFI into DIR/EFI/BOOT/BOOTX64.EFI after linking
  --help           Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out-dir)
      OUT_DIR="$2"
      shift 2
      ;;
    --staging)
      STAGING_DIR="$2"
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

BOOTX64_OBJ_PATH="${OUT_DIR}/BOOTX64.obj"
LOADER_OBJ_PATH="${OUT_DIR}/panthera_loader.obj"
HANDOFF_OBJ_PATH="${OUT_DIR}/panthera_handoff.obj"
EFI_PATH="${OUT_DIR}/BOOTX64.EFI"

mkdir -p "${OUT_DIR}"

compile_source() {
  local source_path="$1"
  local object_path="$2"

  "${CC}" \
    -target x86_64-unknown-windows \
    -std=c11 \
    -ffreestanding \
    -fshort-wchar \
    -fno-stack-protector \
    -fno-builtin \
    -mno-red-zone \
    -Wall \
    -Wextra \
    -Werror \
    -c "${source_path}" \
    -o "${object_path}"

  echo "Built COFF object: ${object_path}"
}

compile_assembly() {
  local source_path="$1"
  local object_path="$2"

  "${CC}" \
    -target x86_64-unknown-windows \
    -ffreestanding \
    -fshort-wchar \
    -fno-stack-protector \
    -fno-builtin \
    -mno-red-zone \
    -Wall \
    -Wextra \
    -Werror \
    -c "${source_path}" \
    -o "${object_path}"

  echo "Built COFF object: ${object_path}"
}

compile_source "${PANTHERA_ROOT}/boot/efi/loader/src/bootx64.c" "${BOOTX64_OBJ_PATH}"
compile_source "${PANTHERA_ROOT}/boot/efi/loader/src/panthera_loader.c" "${LOADER_OBJ_PATH}"
compile_assembly "${PANTHERA_ROOT}/boot/efi/loader/src/panthera_handoff.S" "${HANDOFF_OBJ_PATH}"

if [[ -z "${LLD_LINK}" ]]; then
  if command -v lld-link >/dev/null 2>&1; then
    LLD_LINK="$(command -v lld-link)"
  fi
fi

if [[ -z "${LLD_LINK}" ]]; then
  echo "Missing lld-link; cannot produce BOOTX64.EFI on this host yet." >&2
  echo "Set PANTHERA_LLD_LINK to a usable linker path or install LLVM with lld-link." >&2
  exit 1
fi

"${LLD_LINK}" \
  /machine:x64 \
  /subsystem:efi_application \
  /entry:EfiMain \
  /fixed:no \
  /nodefaultlib \
  /out:"${EFI_PATH}" \
  "${BOOTX64_OBJ_PATH}" \
  "${LOADER_OBJ_PATH}" \
  "${HANDOFF_OBJ_PATH}"

echo "Linked EFI application: ${EFI_PATH}"

if [[ -n "${STAGING_DIR}" ]]; then
  mkdir -p "${STAGING_DIR}/EFI/BOOT"
  cp "${EFI_PATH}" "${STAGING_DIR}/EFI/BOOT/BOOTX64.EFI"
  echo "Staged EFI loader at: ${STAGING_DIR}/EFI/BOOT/BOOTX64.EFI"
fi
