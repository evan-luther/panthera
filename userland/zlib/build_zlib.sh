#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="1.3.1"
DISTDIR="${PANTHERA_ROOT}/build/distfiles"
TARBALL="${DISTDIR}/zlib-${VERSION}.tar.gz"
URL="https://github.com/madler/zlib/releases/download/v${VERSION}/zlib-${VERSION}.tar.gz"

STAGE_DIR="${PANTHERA_ROOT}/userland/zlib/stage"
SYSROOT_LIB_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
SYSROOT_INC_DIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/include"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -find clang)"
TARGET="x86_64-apple-darwin23.0"
MINVER="14.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${DISTDIR}" "${STAGE_DIR}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" \
   && -f "${SYSROOT_LIB_DIR}/libz.1.dylib" ]]; then
  echo "zlib ${VERSION} already staged"
  exit 0
fi

"${PANTHERA_ROOT}/tools/fetch_with_checksum.sh" "${URL}" "${TARBALL}"

WORKDIR="$(mktemp -d /tmp/panthera-zlib-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

tar -C "${WORKDIR}" -xf "${TARBALL}"
SRCDIR="${WORKDIR}/zlib-${VERSION}"

cd "${SRCDIR}"

CC="${CC} -target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}" \
  CFLAGS="-O2" \
  LDFLAGS="-target ${TARGET} -mmacosx-version-min=${MINVER} -isysroot ${SDKROOT}" \
  ./configure --prefix="${STAGE_DIR}"

make -j"${JOBS}"
make install

# Stage into sysroot
cp -f "${STAGE_DIR}/lib/libz.1.3.1.dylib" "${SYSROOT_LIB_DIR}/libz.1.3.1.dylib"
install_name_tool -id /usr/lib/libz.1.dylib "${SYSROOT_LIB_DIR}/libz.1.3.1.dylib"
ln -sfn libz.1.3.1.dylib "${SYSROOT_LIB_DIR}/libz.1.dylib"
ln -sfn libz.1.dylib "${SYSROOT_LIB_DIR}/libz.dylib"

cp -f "${STAGE_DIR}/include/zlib.h" "${SYSROOT_INC_DIR}/zlib.h"
cp -f "${STAGE_DIR}/include/zconf.h" "${SYSROOT_INC_DIR}/zconf.h"
cp -f "${STAGE_DIR}/lib/libz.a" "${SYSROOT_LIB_DIR}/libz.a"

echo "Built zlib ${VERSION}: ${SYSROOT_LIB_DIR}/libz.1.dylib"
