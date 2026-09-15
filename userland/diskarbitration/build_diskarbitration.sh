#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

CLANG_RT="$("${CC}" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [[ -z "${CLANG_RT}" || ! -f "${CLANG_RT}" ]]; then
  CLANG_RT="$("${CC}" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [[ -z "${CLANG_RT}" || ! -f "${CLANG_RT}" ]]; then
  CLANG_RESOURCE_DIR="$("${CC}" -print-resource-dir 2>/dev/null || true)"
  CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
fi
if [[ ! -f "${CLANG_RT}" ]]; then
  echo "Unable to locate Darwin compiler runtime for ${CC}" >&2
  exit 1
fi

OBJ_DIR="${SCRIPT_DIR}/obj"
LIB_DIR="${SCRIPT_DIR}/lib"
OUT="${LIB_DIR}/libDiskArbitration.dylib"

mkdir -p "${OBJ_DIR}" "${LIB_DIR}" \
  "${SYSROOT}/usr/include/DiskArbitration" \
  "${SYSROOT}/usr/lib"

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -O2 \
  -Wall \
  -Wextra \
  -Wno-macro-redefined \
  -Wno-nullability-completeness \
  -fPIC \
  -I"${SCRIPT_DIR}/include" \
  -I"${SYSROOT}/usr/include" \
  -include "${SCRIPT_DIR}/src/panthera_da_prefix.h" \
  -c "${SCRIPT_DIR}/src/DiskArbitration.c" \
  -o "${OBJ_DIR}/DiskArbitration.o"

"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -Wl,-syslibroot,"${SYSROOT}" \
  -dynamiclib \
  -nodefaultlibs \
  -install_name /usr/lib/libDiskArbitration.dylib \
  -compatibility_version 1 \
  -current_version 1 \
  -Wl,-not_for_dyld_shared_cache \
  "${OBJ_DIR}/DiskArbitration.o" \
  "${SYSROOT}/usr/lib/libCoreFoundation.dylib" \
  "${SYSROOT}/usr/lib/libSystem.B.dylib" \
  "${CLANG_RT}" \
  -o "${OUT}"

cp -f "${SCRIPT_DIR}/include/DiskArbitration/DiskArbitration.h" \
  "${SYSROOT}/usr/include/DiskArbitration/DiskArbitration.h"
cp -f "${OUT}" "${SYSROOT}/usr/lib/libDiskArbitration.dylib"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUT}"

echo "Built ${OUT}"
