#!/bin/bash
# Build ICU (International Components for Unicode) for Panthera x86_64
# Step 3 of the CoreFoundation dependency chain
# Dependencies: libc++ and libc++abi (must be built and in sysroot first)
#
# Apple ships ICU as a single libicucore.A.dylib that merges all ICU components
# (common, i18n, io, stubdata). This script follows Apple's approach:
#   1. Build host ICU natively (for data-generation tools + data file)
#   2. Cross-compile target ICU for x86_64 using --with-cross-build
#   3. Combine all target .o files into libicucore.A.dylib
#
# Source: apple-oss-distributions/ICU tag ICU-76142.3.1.1
set -euo pipefail

PANTHERA_ROOT="${PANTHERA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
ICU_APPLE_SRC="${PANTHERA_ROOT}/src/ICU-76142.3.1.1"
ICU_SRC="${ICU_APPLE_SRC}/icu/icu4c/source"
BUILDDIR="${PANTHERA_ROOT}/userland/icu"
HOST_BUILD="${BUILDDIR}/build-host"
TARGET_BUILD="${BUILDDIR}/build-target"
SHIMDIR="${BUILDDIR}/shims"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
CXX="$(xcrun -find clang++)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
CLANG_RT="$("$CC" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
    CLANG_RT="$("$CC" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
    CLANG_RESOURCE_DIR="$("$CC" -print-resource-dir 2>/dev/null || true)"
    if [ -n "${CLANG_RESOURCE_DIR}" ] && [ -f "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a" ]; then
        CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
    fi
fi

if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
    echo "ERROR: Unable to locate Darwin compiler runtime archive (libclang_rt.osx.a) using ${CC}" >&2
    exit 1
fi

OUTPUT="${BUILDDIR}/libicucore.A.dylib"
DATA_OUTPUT="${BUILDDIR}/data/icudt76l.dat"

if [ -f "$OUTPUT" ] && [ -f "$DATA_OUTPUT" ] && [ "${PANTHERA_FORCE_REBUILD:-}" != "1" ]; then
    echo "libicucore.A.dylib and ICU data already exist. Set PANTHERA_FORCE_REBUILD=1 to rebuild."
    exit 0
fi

# Verify source exists
if [ ! -d "$ICU_SRC" ]; then
    echo "ERROR: ICU source not found at $ICU_SRC"
    echo "Download with: cd src && git clone --depth 1 --branch ICU-76142.3.1.1 https://github.com/apple-oss-distributions/ICU.git ICU-76142.3.1.1"
    exit 1
fi

# Verify libc++ and libc++abi are available
if [ ! -f "${SYSROOT}/usr/lib/libc++.1.dylib" ]; then
    echo "ERROR: libc++.1.dylib not found in sysroot. Build it first (Step 2)."
    exit 1
fi
if [ ! -f "${SYSROOT}/usr/lib/libc++abi.dylib" ]; then
    echo "ERROR: libc++abi.dylib not found in sysroot. Build it first (Step 1)."
    exit 1
fi

echo "=== Building ICU from Apple ICU-76142.3.1.1 ==="

# --- Create shim for os/feature_private.h (Apple private header) ---
mkdir -p "${SHIMDIR}/os"
cat > "${SHIMDIR}/os/feature_private.h" << 'SHIMEOF'
/* Shim for os/feature_private.h — Apple feature flags not available on Panthera */
#ifndef _OS_FEATURE_PRIVATE_H
#define _OS_FEATURE_PRIVATE_H
#define os_feature_enabled(domain, feature) (false)
#endif
SHIMEOF

# --- Common ICU defines matching Apple's build ---
# Key defines from Apple's makefile: U_DISABLE_RENAMING, UCONFIG_NO_MF2, U_TIMEZONE, etc.
COMMON_ICU_DEFINES="-DU_SHOW_CPLUSPLUS_API=1 -DU_SHOW_INTERNAL_API=1 -DU_TIMEZONE=timezone -DICU_DATA_DIR=\\\"/usr/share/icu\\\" -DU_DISABLE_RENAMING=1 -DUCONFIG_NO_MF2=1"

# =========================================
# Step 1: Build host ICU (native, for data tools + data file)
# =========================================
echo ""
echo "=== Step 1: Building host ICU (native) ==="

rm -rf "$HOST_BUILD"
mkdir -p "$HOST_BUILD"
cd "$HOST_BUILD"

CC="$CC" CXX="$CXX" \
CFLAGS="-isysroot $SDKROOT -I$SHIMDIR -Os -fno-exceptions -fvisibility=hidden $COMMON_ICU_DEFINES" \
CXXFLAGS="--std=c++17 -isysroot $SDKROOT -I$SHIMDIR -Os -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden $COMMON_ICU_DEFINES" \
CPPFLAGS="-isysroot $SDKROOT -I$SHIMDIR" \
"$ICU_SRC/runConfigureICU" MacOSX \
    --srcdir="$ICU_SRC" \
    --disable-renaming --disable-extras --disable-layout --disable-samples \
    --with-data-packaging=archive \
    --prefix=/tmp/icu-host

# Copy Apple's Makefile.local (adds Apple-specific source files)
cp "$ICU_SRC/common/Makefile.local" "$HOST_BUILD/common/"
cp "$ICU_SRC/i18n/Makefile.local" "$HOST_BUILD/i18n/"

make -j"$(sysctl -n hw.ncpu)"

echo "Host ICU built successfully"
echo "Data file: $(ls -la "$HOST_BUILD/data/out/icudt76l.dat" | awk '{print $5}') bytes"

# =========================================
# Step 2: Cross-compile target ICU for x86_64
# =========================================
echo ""
echo "=== Step 2: Cross-compiling target ICU for $TARGET ==="

rm -rf "$TARGET_BUILD"
mkdir -p "$TARGET_BUILD"
cd "$TARGET_BUILD"

CC="$CC" CXX="$CXX" \
CFLAGS="-target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -I$SHIMDIR -O2 -fno-exceptions -fvisibility=hidden $COMMON_ICU_DEFINES" \
CXXFLAGS="--std=c++17 -target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -I$SHIMDIR -O2 -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden $COMMON_ICU_DEFINES" \
CPPFLAGS="-target $TARGET -isysroot $SDKROOT -I$SHIMDIR" \
LDFLAGS="-target $TARGET -isysroot $SDKROOT" \
"$ICU_SRC/runConfigureICU" MacOSX \
    --srcdir="$ICU_SRC" \
    --host="$TARGET" \
    --with-cross-build="$HOST_BUILD" \
    --disable-renaming --disable-extras --disable-layout --disable-samples \
    --with-data-packaging=archive \
    --prefix=/usr

cp "$ICU_SRC/common/Makefile.local" "$TARGET_BUILD/common/"
cp "$ICU_SRC/i18n/Makefile.local" "$TARGET_BUILD/i18n/"

make -j"$(sysctl -n hw.ncpu)"

echo "Target ICU cross-compiled"
echo "Object files: stubdata=$(ls "$TARGET_BUILD"/stubdata/*.o | wc -l | tr -d ' ') common=$(ls "$TARGET_BUILD"/common/*.o | wc -l | tr -d ' ') i18n=$(ls "$TARGET_BUILD"/i18n/*.o | wc -l | tr -d ' ') io=$(ls "$TARGET_BUILD"/io/*.o | wc -l | tr -d ' ')"

# =========================================
# Step 3: Combine all .o files into libicucore.A.dylib
# =========================================
echo ""
echo "=== Step 3: Linking libicucore.A.dylib ==="

$CXX -target "$TARGET" -mmacosx-version-min=14.0 -isysroot "$SDKROOT" \
    -Wl,-syslibroot,"$SYSROOT" \
    -dynamiclib \
    -nodefaultlibs \
    -current_version 76.1 -compatibility_version 1 \
    -install_name /usr/lib/libicucore.A.dylib \
    -O2 -fno-exceptions -fvisibility=hidden -fvisibility-inlines-hidden \
    -dead_strip \
    "$TARGET_BUILD"/stubdata/*.o \
    "$TARGET_BUILD"/common/*.o \
    "$TARGET_BUILD"/i18n/*.o \
    "$TARGET_BUILD"/io/*.o \
    -L"$SYSROOT/usr/lib" -L"$SYSROOT/usr/lib/system" \
    -lpanthera_extra \
    "$SYSROOT/usr/lib/libSystem.B.dylib" \
    "$SYSROOT/usr/lib/libc++.1.dylib" \
    "$SYSROOT/usr/lib/libc++abi.dylib" \
    "${CLANG_RT}" \
    -o "$OUTPUT"

ln -sf libicucore.A.dylib "${BUILDDIR}/libicucore.dylib"

echo "Built: $OUTPUT"

# --- Verify ---
echo ""
echo "=== Verification ==="
echo "File type:"
file "$OUTPUT"
echo ""
echo "Architecture:"
lipo -info "$OUTPUT"
echo ""
echo "Install name:"
otool -D "$OUTPUT"
echo ""
EXPORTED=$(nm -gU "$OUTPUT" | wc -l | tr -d ' ')
echo "Exported symbols: $EXPORTED"
echo ""

# =========================================
# Step 4: Stage to sysroot
# =========================================
echo "=== Staging to sysroot ==="

# Library
cp "$OUTPUT" "${SYSROOT}/usr/lib/libicucore.A.dylib"
ln -sf libicucore.A.dylib "${SYSROOT}/usr/lib/libicucore.dylib"
echo "Staged: ${SYSROOT}/usr/lib/libicucore.A.dylib"
echo "Symlink: libicucore.dylib -> libicucore.A.dylib"

# Headers
mkdir -p "${SYSROOT}/usr/include/unicode"
cp "$ICU_SRC/common/unicode/"*.h "${SYSROOT}/usr/include/unicode/"
cp "$ICU_SRC/i18n/unicode/"*.h "${SYSROOT}/usr/include/unicode/"
cp "$ICU_SRC/io/unicode/"*.h "${SYSROOT}/usr/include/unicode/"
HDR_COUNT=$(ls "${SYSROOT}/usr/include/unicode/"*.h | wc -l | tr -d ' ')
echo "Staged: $HDR_COUNT ICU headers to ${SYSROOT}/usr/include/unicode/"

# Data file
mkdir -p "${BUILDDIR}/data"
cp "$HOST_BUILD/data/out/icudt76l.dat" "${BUILDDIR}/data/"
echo "Staged: ${BUILDDIR}/data/icudt76l.dat ($(ls -la "${BUILDDIR}/data/icudt76l.dat" | awk '{print $5}') bytes)"

# =========================================
# Step 5: Run audit
# =========================================
echo ""
echo "=== Running audit ==="
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "$OUTPUT"

echo ""
echo "=== ICU build complete ==="
echo "Library:  ${OUTPUT} ($EXPORTED exports)"
echo "Headers:  ${SYSROOT}/usr/include/unicode/ ($HDR_COUNT files)"
echo "Data:     ${BUILDDIR}/data/icudt76l.dat"
echo "Symlink:  libicucore.dylib -> libicucore.A.dylib"
echo ""
echo "Root image staging is in rootfs/create_hfs_root_image.sh."
echo "After rebuilding root image, ICU data goes to /usr/share/icu/icudt76l.dat"
