#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
MIG="${MIG:-$(xcrun -find mig)}"
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

SRC_IOKITUSER="${PANTHERA_ROOT}/src/IOKitUser-100065.40.4"
LOCAL_INCLUDE="${SCRIPT_DIR}/include"
LOCAL_SRC="${SCRIPT_DIR}/src"
BUILD_DIR="${SCRIPT_DIR}/build"
MIG_DIR="${BUILD_DIR}/mig"
OBJ_DIR="${BUILD_DIR}/obj"
LIB_DIR="${SCRIPT_DIR}/lib"
BIN_DIR="${SCRIPT_DIR}/bin"

mkdir -p "${MIG_DIR}" "${OBJ_DIR}" "${LIB_DIR}" "${BIN_DIR}"

COMMON_CFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -O2
  -Wall
  -Wextra
  -Werror
  -Wno-deprecated-declarations
  -DIOKIT_SERVER_VERSION=20210810
  -I"${LOCAL_INCLUDE}"
  -I"${MIG_DIR}"
)

LIB_LDFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -Wl,-syslibroot,"${SYSROOT}"
  -dynamiclib
  -nodefaultlibs
  -install_name /usr/lib/libIOKit.dylib
  -compatibility_version 1
  -current_version 1
  -L"${SYSROOT}/usr/lib"
  -L"${SYSROOT}/usr/lib/system"
  -lpanthera_extra
  "${SYSROOT}/usr/lib/libCoreFoundation.dylib"
  "${SYSROOT}/usr/lib/libSystem.B.dylib"
  "${CLANG_RT}"
  -Wl,-not_for_dyld_shared_cache
)

PROBE_LDFLAGS=(
  -target "${TARGET}"
  -mmacosx-version-min="${MINVER}"
  -isysroot "${SDKROOT}"
  -Wl,-syslibroot,"${SYSROOT}"
  -nodefaultlibs
  -L"${SYSROOT}/usr/lib"
  -L"${SYSROOT}/usr/lib/system"
  -lpanthera_extra
  "${LIB_DIR}/libIOKit.dylib"
  "${SYSROOT}/usr/lib/libCoreFoundation.dylib"
  "${SYSROOT}/usr/lib/libSystem.B.dylib"
  "${CLANG_RT}"
  -Wl,-rpath,/usr/lib
)

echo "=== Generating IOKit device MIG client stubs ==="
"${MIG}" -DIOKIT=1 -isysroot "${SDKROOT}" \
  -I"${SYSROOT}/usr/include" \
  -header "${MIG_DIR}/device_iokit.h" \
  -user "${MIG_DIR}/device_iokitUser.c" \
  -sheader "${MIG_DIR}/device_iokitServer.h" \
  -server "${MIG_DIR}/device_iokitServer.c" \
  "${SYSROOT}/usr/include/device/device.defs"

echo "=== Building libIOKit.dylib ==="
"${CC}" "${COMMON_CFLAGS[@]}" -c "${LOCAL_SRC}/libIOKit.c" -o "${OBJ_DIR}/libIOKit.o"
"${CC}" "${COMMON_CFLAGS[@]}" -Wno-unused-function -Wno-unused-but-set-variable -c "${SRC_IOKITUSER}/IOCFSerialize.c" -o "${OBJ_DIR}/IOCFSerialize.o"
"${CC}" "${COMMON_CFLAGS[@]}" -Wno-unused-function -Wno-unused-but-set-variable -c "${SRC_IOKITUSER}/IOCFUnserialize.tab.c" -o "${OBJ_DIR}/IOCFUnserialize.tab.o"
"${CC}" "${COMMON_CFLAGS[@]}" -Wno-unused-variable -c "${MIG_DIR}/device_iokitUser.c" -o "${OBJ_DIR}/device_iokitUser.o"

"${CC}" \
  "${OBJ_DIR}/libIOKit.o" \
  "${OBJ_DIR}/IOCFSerialize.o" \
  "${OBJ_DIR}/IOCFUnserialize.tab.o" \
  "${OBJ_DIR}/device_iokitUser.o" \
  "${LIB_LDFLAGS[@]}" \
  -o "${LIB_DIR}/libIOKit.dylib"

echo "=== Building iokit_fb_probe ==="
"${CC}" \
  "${COMMON_CFLAGS[@]}" \
  "${LOCAL_SRC}/iokit_fb_probe.c" \
  "${PROBE_LDFLAGS[@]}" \
  -o "${BIN_DIR}/iokit_fb_probe"

mkdir -p \
  "${SYSROOT}/usr/include/IOKit" \
  "${SYSROOT}/usr/include/System/libkern" \
  "${SYSROOT}/usr/lib"

for header in \
  IOCFSerialize.h \
  IOCFUnserialize.h \
  IOReturn.h \
  IOMapTypes.h \
  IOTypes.h \
  IOKitKeys.h \
  IOKitLib.h; do
  cp "${LOCAL_INCLUDE}/IOKit/${header}" "${SYSROOT}/usr/include/IOKit/${header}"
done
cp "${LOCAL_INCLUDE}/System/libkern/OSSerializeBinary.h" "${SYSROOT}/usr/include/System/libkern/OSSerializeBinary.h"
cp "${LIB_DIR}/libIOKit.dylib" "${SYSROOT}/usr/lib/libIOKit.dylib"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${LIB_DIR}/libIOKit.dylib" "${BIN_DIR}/iokit_fb_probe"

echo "Built ${LIB_DIR}/libIOKit.dylib"
echo "Built ${BIN_DIR}/iokit_fb_probe"
