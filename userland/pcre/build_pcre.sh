#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_LIB="${PANTHERA_ROOT}/userland/pcre/lib"
OUT_INC="${PANTHERA_ROOT}/userland/pcre/include"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
PATCHES_DIR="${PANTHERA_ROOT}/src/pcre-21/files"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${OUT_LIB}" "${OUT_INC}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -f "${OUT_LIB}/libpcre.1.dylib" ]]; then
    echo "pcre already built"
    exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-pcre-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

# Extract
tar xjf "${PANTHERA_ROOT}/src/pcre-21/pcre-8.44.tar.bz2" -C "${WORKDIR}"
SRCDIR="${WORKDIR}/pcre-8.44"

# Apply Apple patches (removes flat_namespace from configure)
cd "${SRCDIR}"
for p in configure.diff Makefile.in.diff no-programs.diff; do
    if [[ -f "${PATCHES_DIR}/${p}" ]]; then
        patch -p0 < "${PATCHES_DIR}/${p}" || true
    fi
done

# Remove ALL remaining flat_namespace references (critical for Panthera)
sed -i '' 's/-flat_namespace//g' "${SRCDIR}/configure"
sed -i '' 's/-undefined suppress/-undefined error/g' "${SRCDIR}/configure"

BUILDDIR="${WORKDIR}/build"
mkdir -p "${BUILDDIR}"
cd "${BUILDDIR}"

TARGET_CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT}"
BUILD_TRIPLE="x86_64-apple-darwin23.0"

"${SRCDIR}/configure" \
    --host="${TARGET}" \
    --build="${BUILD_TRIPLE}" \
    --prefix=/usr \
    --enable-utf8 \
    --enable-unicode-properties \
    --disable-cpp \
    --disable-static \
    CC="${CC} ${TARGET_CFLAGS}" \
    CFLAGS="-O2" \
    LDFLAGS="${TARGET_CFLAGS} -lSystem -Wl,-not_for_dyld_shared_cache"

make -j"${JOBS}"

# Grab the built dylib
DYLIB="${BUILDDIR}/.libs/libpcre.1.dylib"
if [[ ! -f "${DYLIB}" ]]; then
    DYLIB="${BUILDDIR}/.libs/libpcre.dylib"
fi

# Fix install name
install_name_tool -id /usr/lib/libpcre.1.dylib "${DYLIB}"

cp "${DYLIB}" "${OUT_LIB}/libpcre.1.dylib"
ln -sf libpcre.1.dylib "${OUT_LIB}/libpcre.dylib"

# Copy headers (generated pcre.h is in build dir; source pcre.h.in is not usable)
if [[ -f "${BUILDDIR}/pcre.h" ]]; then
    cp "${BUILDDIR}/pcre.h" "${OUT_INC}/"
elif [[ -f "${SRCDIR}/pcre.h" ]]; then
    cp "${SRCDIR}/pcre.h" "${OUT_INC}/"
fi
cp "${SRCDIR}/pcreposix.h" "${OUT_INC}/" 2>/dev/null || true

# Stage to sysroot
cp "${OUT_LIB}/libpcre.1.dylib" "${SYSROOT}/usr/lib/libpcre.1.dylib"
ln -sf libpcre.1.dylib "${SYSROOT}/usr/lib/libpcre.dylib"
cp "${OUT_INC}/pcre.h" "${SYSROOT}/usr/include/"

# Audit
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUT_LIB}/libpcre.1.dylib"

echo "Built pcre: ${OUT_LIB}/libpcre.1.dylib ($(wc -c < "${OUT_LIB}/libpcre.1.dylib") bytes)"
