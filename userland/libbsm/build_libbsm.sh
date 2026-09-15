#!/usr/bin/env bash
# build_libbsm.sh — Source builder for libbsm.0.dylib in Panthera Darwin.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SYSROOT_LIB_DIR="${SYSROOT}/usr/lib"

SRC="${SCRIPT_DIR}/libbsm_stub.c"
OUT_DYLIB="${SCRIPT_DIR}/libbsm.0.dylib"
SYSROOT_DYLIB="${SYSROOT_LIB_DIR}/libbsm.0.dylib"
OBJ_DIR="${SCRIPT_DIR}/obj"
OBJ="${OBJ_DIR}/libbsm_stub.o"

SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path)}"
CC="${CC:-$(xcrun -find clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

echo "=== Building libbsm.0.dylib ==="
echo "  Source:  ${SRC}"
echo "  Target:  ${TARGET} (macOS ${MINVER})"
echo "  Sysroot: ${SYSROOT}"
echo "  Output:  ${OUT_DYLIB}"

# Validate required inputs
if [[ ! -f "${SRC}" ]]; then
  echo "Error: required source input not found: ${SRC}" >&2
  exit 1
fi

if [[ ! -f "${SYSROOT_LIB_DIR}/libSystem.B.dylib" ]]; then
  echo "Error: sysroot libSystem not found: ${SYSROOT_LIB_DIR}/libSystem.B.dylib" >&2
  exit 1
fi

if [[ -z "${SDKROOT}" || ! -d "${SDKROOT}" ]]; then
  echo "Error: macOS SDK root not found: ${SDKROOT}" >&2
  exit 1
fi

if [[ -z "${CC}" || ! -x "${CC}" ]]; then
  echo "Error: clang compiler not found: ${CC}" >&2
  exit 1
fi

mkdir -p "${OBJ_DIR}" "${SYSROOT_LIB_DIR}"

# Compile libbsm_stub.c
echo "  CC libbsm_stub.c"
"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -O2 \
  -Wall \
  -Wextra \
  -fPIC \
  -c "${SRC}" \
  -o "${OBJ}"

# Link libbsm.0.dylib
echo "  LINK libbsm.0.dylib"
"${CC}" \
  -target "${TARGET}" \
  -mmacosx-version-min="${MINVER}" \
  -isysroot "${SDKROOT}" \
  -dynamiclib \
  -install_name /usr/lib/libbsm.0.dylib \
  -compatibility_version 1.0.0 \
  -current_version 1.0.0 \
  -L"${SYSROOT_LIB_DIR}" \
  -lSystem \
  -Wl,-not_for_dyld_shared_cache \
  "${OBJ}" \
  -o "${OUT_DYLIB}"

# Create local symlink libbsm.dylib -> libbsm.0.dylib
ln -sfn libbsm.0.dylib "${SCRIPT_DIR}/libbsm.dylib"

# Validate output architecture and Mach-O header
echo "  Verifying architecture for ${OUT_DYLIB}..."
if ! otool -hv "${OUT_DYLIB}" | grep -q "X86_64"; then
  echo "Error: ${OUT_DYLIB} is not x86_64" >&2
  exit 1
fi

if ! otool -hv "${OUT_DYLIB}" | grep -q "DYLIB"; then
  echo "Error: ${OUT_DYLIB} is not a valid Mach-O dylib" >&2
  exit 1
fi

if ! otool -hv "${OUT_DYLIB}" | grep -q "TWOLEVEL"; then
  echo "Error: ${OUT_DYLIB} is not two-level namespace" >&2
  exit 1
fi

# Stage into sysroot
echo "  STAGE libbsm.0.dylib -> ${SYSROOT_DYLIB}"
cp -f "${OUT_DYLIB}" "${SYSROOT_DYLIB}"
ln -sfn libbsm.0.dylib "${SYSROOT_LIB_DIR}/libbsm.dylib"

echo "=== libbsm.0.dylib build complete ==="
