#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/libxml2-39.10/libxml2"
PREGEN="${PANTHERA_ROOT}/src/libxml2-39.10/Pregenerated Files"
OUT_LIB="${PANTHERA_ROOT}/userland/libxml2/lib"
OUT_BIN="${PANTHERA_ROOT}/userland/libxml2/bin"
OUT_INC="${PANTHERA_ROOT}/userland/libxml2/include"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${OUT_LIB}" "${OUT_BIN}" "${OUT_INC}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -f "${OUT_LIB}/libxml2.2.dylib" && -f "${OUT_BIN}/xmllint" ]]; then
    echo "libxml2 already built"
    exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-libxml2-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

# Symlink "Pregenerated Files" to avoid space-in-path issues
ln -sf "${PREGEN}" "${WORKDIR}/pregen"
PREGEN_LINK="${WORKDIR}/pregen"

# Create shim headers
SHIMDIR="${WORKDIR}/shims"
mkdir -p "${SHIMDIR}/mach-o"
cat > "${SHIMDIR}/mach-o/dyld_priv.h" <<'SHIM'
#ifndef _DYLD_PRIV_H_
#define _DYLD_PRIV_H_
#include <stdbool.h>
#define DYLD_MACOSX_VERSION_10_9 0x000A0900
typedef struct { unsigned int macos, ios, watchos, tvos, bridgeos; } dyld_build_version_t;
static const dyld_build_version_t dyld_fall_2022_os_versions = {0x000D0000, 0x00100000, 0x00090000, 0x00100000, 0};
static const dyld_build_version_t dyld_2024_SU_E_os_versions = {0x000F0400, 0x00120400, 0x000B0400, 0x00120400, 0};
static inline __attribute__((unused)) unsigned int
dyld_get_program_sdk_version(void) { return 0x000E0000; }
static inline __attribute__((unused)) bool
dyld_program_minos_at_least(dyld_build_version_t v) { (void)v; return true; }
static inline __attribute__((unused)) bool
dyld_program_sdk_at_least(dyld_build_version_t v) { (void)v; return true; }
static inline __attribute__((unused)) const char*
_dyld_get_image_name(unsigned int i) { (void)i; return "panthera"; }
/* Apple's __progname */
extern const char *__progname __asm("___progname");
#endif
SHIM

# Create a modified xmlversion.h with ICU disabled
mkdir -p "${SHIMDIR}/libxml"
sed 's/^#define LIBXML_ICU_ENABLED/\/* #undef LIBXML_ICU_ENABLED *\//' \
    "${PREGEN}/include/libxml/xmlversion.h" > "${SHIMDIR}/libxml/xmlversion.h"

# Library source files (exclude test*, run*, xmlcatalog.c, xmllint.c, trio*)
# Exclude rngparser.c (compact RELAX NG — has a bug, not needed)
LIB_SRCS=(
    buf.c c14n.c catalog.c chvalid.c debugXML.c dict.c DOCBparser.c
    encoding.c entities.c error.c globals.c hash.c HTMLparser.c HTMLtree.c
    legacy.c list.c nanoftp.c nanohttp.c parser.c parserInternals.c
    pattern.c relaxng.c SAX.c SAX2.c schematron.c threads.c
    tree.c uri.c valid.c xinclude.c xlink.c xmlIO.c xmlmemory.c
    xmlmodule.c xmlreader.c xmlregexp.c xmlsave.c xmlschemas.c
    xmlschemastypes.c xmlstring.c xmlunicode.c xmlversion.c xmlwriter.c
    xpath.c xpointer.c xzlib.c
)

CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -O2"
# Shim dir first (has modified xmlversion.h + dyld_priv.h), then pregenerated config.h, then source
CFLAGS="${CFLAGS} -I${SHIMDIR} -I${PREGEN_LINK}/include -I${SRCDIR}/include -I${SRCDIR} -DHAVE_CONFIG_H"
# Suppress known warnings in Apple's libxml2 code
CFLAGS="${CFLAGS} -Wno-shorten-64-to-32 -Wno-format -Wno-deprecated-declarations"
CFLAGS="${CFLAGS} -fPIC"

echo "Compiling libxml2 sources..."
OBJS=()
for src in "${LIB_SRCS[@]}"; do
    obj="${WORKDIR}/${src%.c}.o"
    "${CC}" ${CFLAGS} -c "${SRCDIR}/${src}" -o "${obj}" &
    OBJS+=("${obj}")
done
wait

echo "Linking libxml2.2.dylib..."
"${CC}" -target "${TARGET}" -isysroot "${SDKROOT}" \
    -dynamiclib -install_name /usr/lib/libxml2.2.dylib \
    -compatibility_version 10.0.0 -current_version 10.9.0 \
    -exported_symbols_list "${PREGEN_LINK}/libxml2.exp" \
    -o "${WORKDIR}/libxml2.2.dylib" \
    "${OBJS[@]}" \
    -L"${PANTHERA_ROOT}/userland/zlib/stage/lib" -lz \
    -Wl,-not_for_dyld_shared_cache \
    -lSystem

# Build xmllint
echo "Building xmllint..."
"${CC}" ${CFLAGS} -c "${SRCDIR}/xmllint.c" -o "${WORKDIR}/xmllint.o"
"${CC}" -target "${TARGET}" -isysroot "${SDKROOT}" \
    -o "${WORKDIR}/xmllint" \
    "${WORKDIR}/xmllint.o" \
    -L"${WORKDIR}" -lxml2.2 \
    -Wl,-not_for_dyld_shared_cache \
    -lSystem

# Fix xmllint's reference to the build-dir dylib
install_name_tool -change "${WORKDIR}/libxml2.2.dylib" /usr/lib/libxml2.2.dylib "${WORKDIR}/xmllint"

# Stage outputs
cp "${WORKDIR}/libxml2.2.dylib" "${OUT_LIB}/libxml2.2.dylib"
ln -sf libxml2.2.dylib "${OUT_LIB}/libxml2.dylib"
cp "${WORKDIR}/xmllint" "${OUT_BIN}/xmllint"

# Copy headers
mkdir -p "${OUT_INC}/libxml"
cp "${SRCDIR}"/include/libxml/*.h "${OUT_INC}/libxml/"
cp "${PREGEN}/include/libxml/xmlversion.h" "${OUT_INC}/libxml/"

# Stage to sysroot
cp "${OUT_LIB}/libxml2.2.dylib" "${SYSROOT}/usr/lib/libxml2.2.dylib"
ln -sf libxml2.2.dylib "${SYSROOT}/usr/lib/libxml2.dylib"
mkdir -p "${SYSROOT}/usr/include/libxml2/libxml"
cp "${OUT_INC}/libxml/"*.h "${SYSROOT}/usr/include/libxml2/libxml/"
# Also provide headers at /usr/include/libxml/ for packages that expect that
mkdir -p "${SYSROOT}/usr/include/libxml"
cp "${OUT_INC}/libxml/"*.h "${SYSROOT}/usr/include/libxml/"

# Audit
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUT_LIB}/libxml2.2.dylib" "${OUT_BIN}/xmllint"

echo "Built libxml2: ${OUT_LIB}/libxml2.2.dylib ($(wc -c < "${OUT_LIB}/libxml2.2.dylib") bytes)"
echo "Built xmllint: ${OUT_BIN}/xmllint ($(wc -c < "${OUT_BIN}/xmllint") bytes)"
