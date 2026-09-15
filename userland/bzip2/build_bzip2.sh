#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/bzip2/bzip2"
BIN_DIR="${PANTHERA_ROOT}/userland/bzip2/bin"
LIB_DIR="${PANTHERA_ROOT}/userland/bzip2/lib"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"

mkdir -p "${BIN_DIR}" "${LIB_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" &&
      -x "${BIN_DIR}/bzip2" &&
      -f "${LIB_DIR}/libbz2.1.0.dylib" ]]; then
    echo "bzip2 already built"
    exit 0
fi

# Apple's bzip2 includes System/machine/cpu_capabilities.h for x86_64
# vectorized CRC. Create a stub header to compile without it.
SHIMDIR="$(mktemp -d /tmp/panthera-bzip2-shim.XXXXXX)"
trap 'rm -rf "${SHIMDIR}"' EXIT

mkdir -p "${SHIMDIR}/System/machine"
cat > "${SHIMDIR}/System/machine/cpu_capabilities.h" <<'SHIM'
#ifndef _CPU_CAPABILITIES_H
#define _CPU_CAPABILITIES_H
#define kHasAES 0
static inline __attribute__((unused)) unsigned long long
_get_cpu_capabilities(void) { return 0; }
#endif
SHIM

"${CC}" -target "${TARGET}" -mmacosx-version-min=14.0 \
    -isysroot "${SDKROOT}" -O2 \
    -D_FILE_OFFSET_BITS=64 \
    -I"${SHIMDIR}" \
    -o "${BIN_DIR}/bzip2" \
    "${SRCDIR}/bzip2.c" \
    "${SRCDIR}/blocksort.c" \
    "${SRCDIR}/huffman.c" \
    "${SRCDIR}/crctable.c" \
    "${SRCDIR}/randtable.c" \
    "${SRCDIR}/compress.c" \
    "${SRCDIR}/decompress.c" \
    "${SRCDIR}/bzlib.c" \
    -lSystem

"${CC}" -target "${TARGET}" -mmacosx-version-min=14.0 \
    -isysroot "${SDKROOT}" -O2 \
    -dynamiclib \
    -install_name /usr/lib/libbz2.1.0.dylib \
    -compatibility_version 1.0.0 \
    -current_version 1.0.8 \
    -D_FILE_OFFSET_BITS=64 \
    -I"${SHIMDIR}" \
    -o "${LIB_DIR}/libbz2.1.0.dylib" \
    "${SRCDIR}/blocksort.c" \
    "${SRCDIR}/huffman.c" \
    "${SRCDIR}/crctable.c" \
    "${SRCDIR}/randtable.c" \
    "${SRCDIR}/compress.c" \
    "${SRCDIR}/decompress.c" \
    "${SRCDIR}/bzlib.c" \
    -lSystem

ln -sf libbz2.1.0.dylib "${LIB_DIR}/libbz2.dylib"

bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${BIN_DIR}/bzip2"
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${LIB_DIR}/libbz2.1.0.dylib"

echo "Built bzip2: ${BIN_DIR}/bzip2"
echo "Built libbz2: ${LIB_DIR}/libbz2.1.0.dylib"
