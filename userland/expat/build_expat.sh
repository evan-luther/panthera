#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/expat-45/expat/lib"
CONFIG_H="${PANTHERA_ROOT}/src/expat-45/expat_config.h"
OUT_LIB="${PANTHERA_ROOT}/userland/expat/lib"
OUT_INC="${PANTHERA_ROOT}/userland/expat/include"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${OUT_LIB}" "${OUT_INC}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -f "${OUT_LIB}/libexpat.1.dylib" ]]; then
    echo "expat already built"
    exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-expat-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -O2"
CFLAGS="${CFLAGS} -DHAVE_EXPAT_CONFIG_H -I$(dirname "${CONFIG_H}")"
CFLAGS="${CFLAGS} -DXML_ENABLE_VISIBILITY=1"

# Compile source files
for src in xmlparse.c xmlrole.c xmltok.c; do
    "${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/${src}" -o "${WORKDIR}/${src%.c}.o"
done

# Link as shared library
"${CC}" -target "${TARGET}" -isysroot "${SDKROOT}" \
    -dynamiclib -install_name /usr/lib/libexpat.1.dylib \
    -compatibility_version 7.0.0 -current_version 7.2.0 \
    -o "${WORKDIR}/libexpat.1.dylib" \
    "${WORKDIR}/xmlparse.o" "${WORKDIR}/xmlrole.o" "${WORKDIR}/xmltok.o" \
    -lSystem

# Stage outputs
cp "${WORKDIR}/libexpat.1.dylib" "${OUT_LIB}/libexpat.1.dylib"
ln -sf libexpat.1.dylib "${OUT_LIB}/libexpat.dylib"

# Copy headers
cp "${SRCDIR}/expat.h" "${OUT_INC}/"
cp "${SRCDIR}/expat_external.h" "${OUT_INC}/"

# Stage to sysroot
cp "${OUT_LIB}/libexpat.1.dylib" "${SYSROOT}/usr/lib/libexpat.1.dylib"
ln -sf libexpat.1.dylib "${SYSROOT}/usr/lib/libexpat.dylib"
cp "${OUT_INC}/expat.h" "${SYSROOT}/usr/include/"
cp "${OUT_INC}/expat_external.h" "${SYSROOT}/usr/include/"

# Audit
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUT_LIB}/libexpat.1.dylib"

echo "Built expat: ${OUT_LIB}/libexpat.1.dylib ($(wc -c < "${OUT_LIB}/libexpat.1.dylib") bytes)"
