#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${SCRIPT_DIR}/sbin"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

# Locate Darwin compiler runtime (libclang_rt.osx.a)
CLANG_RT="$("${CC}" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RT="$("${CC}" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RESOURCE_DIR="$("${CC}" -print-resource-dir 2>/dev/null || true)"
  if [ -n "${CLANG_RESOURCE_DIR}" ] && [ -f "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a" ]; then
    CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
  fi
fi

if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  echo "ERROR: Unable to locate Darwin compiler runtime archive (libclang_rt.osx.a) using ${CC}" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

build_tool() {
  local name="$1"
  "${CC}" \
    -target "${TARGET}" \
    -mmacosx-version-min="${MINVER}" \
    -isysroot "${SDKROOT}" \
    -Wall -Wextra -Werror \
    "${SCRIPT_DIR}/${name}.c" \
    -o "${OUT_DIR}/${name}" \
    -Wl,-syslibroot,"${SYSROOT}" \
    -nodefaultlibs \
    "${SYSROOT}/usr/lib/libSystem.B.dylib" \
    "${CLANG_RT}" \
    -Wl,-not_for_dyld_shared_cache
}
build_mount() {
  "${CC}" \
    -target "${TARGET}" \
    -mmacosx-version-min="${MINVER}" \
    -isysroot "${SDKROOT}" \
    -Wall -Wextra -Wno-deprecated-non-prototype -Wno-deprecated-declarations \
    -I"${SCRIPT_DIR}" \
    "${SCRIPT_DIR}/mount.c" \
    "${SCRIPT_DIR}/vfslist.c" \
    -o "${OUT_DIR}/mount" \
    -Wl,-syslibroot,"${SYSROOT}" \
    -nodefaultlibs \
    "${SYSROOT}/usr/lib/libSystem.B.dylib" \
    "${CLANG_RT}" \
    -Wl,-not_for_dyld_shared_cache
}

build_tool umount
build_mount
build_tool panthera_zfs_hybrid_mount
bash "${PANTHERA_ROOT}/tools/audit_package.sh" \
  "${OUT_DIR}/umount" \
  "${OUT_DIR}/mount" \
  "${OUT_DIR}/panthera_zfs_hybrid_mount"

echo "Built ${OUT_DIR}/umount"
echo "Built ${OUT_DIR}/mount"
echo "Built ${OUT_DIR}/panthera_zfs_hybrid_mount"
