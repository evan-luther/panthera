#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/libarchive-160.60.3/libarchive"
OUT_LIB="${PANTHERA_ROOT}/userland/libarchive/lib"
OUT_BIN="${PANTHERA_ROOT}/userland/libarchive/bin"
OUT_INC="${PANTHERA_ROOT}/userland/libarchive/include"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
ZLIB_STAGE="${PANTHERA_ROOT}/userland/zlib/stage"
BZIP2_SRCDIR="${PANTHERA_ROOT}/src/bzip2/bzip2"
EXPAT_LIB="${PANTHERA_ROOT}/userland/expat/lib"
EXPAT_INC="${PANTHERA_ROOT}/userland/expat/include"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

mkdir -p "${OUT_LIB}" "${OUT_BIN}" "${OUT_INC}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -f "${OUT_BIN}/bsdtar" ]]; then
    echo "libarchive already built"
    exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-libarchive-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

# Build a static libbz2.a for libarchive to link against
BZIP2_BUILD="${WORKDIR}/bzip2"
mkdir -p "${BZIP2_BUILD}"
BZ2_SRCS=(blocksort.c huffman.c crctable.c randtable.c compress.c decompress.c bzlib.c)
BZ2_SHIMDIR="${WORKDIR}/bzip2-shims"
mkdir -p "${BZ2_SHIMDIR}/System/machine"
cat > "${BZ2_SHIMDIR}/System/machine/cpu_capabilities.h" <<'SHIM'
#ifndef _CPU_CAPABILITIES_H
#define _CPU_CAPABILITIES_H
#define kHasAES 0
static inline __attribute__((unused)) unsigned long long
_get_cpu_capabilities(void) { return 0; }
#endif
SHIM

for src in "${BZ2_SRCS[@]}"; do
    "${CC}" -target "${TARGET}" -isysroot "${SDKROOT}" -O2 \
        -I"${BZ2_SHIMDIR}" -D_FILE_OFFSET_BITS=64 \
        -c "${BZIP2_SRCDIR}/${src}" -o "${BZIP2_BUILD}/${src%.c}.o"
done
ar rcs "${BZIP2_BUILD}/libbz2.a" "${BZIP2_BUILD}"/*.o

# Create shim headers
SHIMDIR="${WORKDIR}/shims"
mkdir -p "${SHIMDIR}"
mkdir -p "${SHIMDIR}/pthread" "${SHIMDIR}/System/sys"
cat > "${SHIMDIR}/System/sys/fsctl.h" <<'SHIM'
/* System/sys/fsctl.h — stub for Panthera */
#ifndef _PANTHERA_SYS_FSCTL_H_
#define _PANTHERA_SYS_FSCTL_H_
#include <sys/types.h>
#ifndef FSIOC_CAS_BSDFLAGS
#define FSIOC_CAS_BSDFLAGS 0
#endif
struct fsioc_cas_bsdflags {
    uint32_t expected_flags;
    uint32_t new_flags;
    uint32_t actual_flags;
};
/* ffsctl is already declared in unistd.h, don't redeclare */
#endif
SHIM

mkdir -p "${SHIMDIR}/os"
cat > "${SHIMDIR}/os/variant_private.h" <<'SHIM'
/* os/variant_private.h — stub for Panthera */
#ifndef _OS_VARIANT_PRIVATE_H_
#define _OS_VARIANT_PRIVATE_H_
#include <stdbool.h>
static inline __attribute__((unused)) bool os_variant_has_internal_content(const char *s) { (void)s; return false; }
#endif
SHIM
cat > "${SHIMDIR}/pthread/private.h" <<'SHIM'
/* pthread/private.h — stub for Panthera */
#ifndef _PTHREAD_PRIVATE_H_
#define _PTHREAD_PRIVATE_H_
#include <pthread.h>
/* pthread_fchdir_np: per-thread working directory. Stub returns -1 (not supported). */
static inline __attribute__((unused)) int pthread_fchdir_np(int fd) { (void)fd; return -1; }
#endif
SHIM

cat > "${SHIMDIR}/quarantine.h" <<'SHIM'
/* quarantine.h — stub for Panthera (no quarantine support) */
#ifndef _QUARANTINE_H_
#define _QUARANTINE_H_
typedef void* qtn_file_t;
static inline __attribute__((unused)) qtn_file_t qtn_file_alloc(void) { return (void*)0; }
static inline __attribute__((unused)) void qtn_file_free(qtn_file_t f) { (void)f; }
static inline __attribute__((unused)) int qtn_file_init_with_fd(qtn_file_t f, int fd) { (void)f; (void)fd; return 0; }
static inline __attribute__((unused)) int qtn_file_init_with_path(qtn_file_t f, const char *p) { (void)f; (void)p; return 0; }
static inline __attribute__((unused)) int qtn_file_apply_to_path(qtn_file_t f, const char *p) { (void)f; (void)p; return 0; }
static inline __attribute__((unused)) qtn_file_t qtn_file_clone(qtn_file_t f) { (void)f; return (void*)0; }
static inline __attribute__((unused)) const char* qtn_error(int e) { (void)e; return "quarantine not supported"; }
#endif
SHIM

# Now configure + build libarchive
BUILDDIR="${WORKDIR}/build"
mkdir -p "${BUILDDIR}"
cd "${BUILDDIR}"

TARGET_CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT}"

# Remove flat_namespace from configure
sed -i '' 's/-flat_namespace//g; s/-undefined suppress/-undefined error/g' "${SRCDIR}/configure"

# Create stub implementations for Apple-specific functions
cat > "${WORKDIR}/panthera_libarchive_stubs.c" <<'STUB'
#include <stdbool.h>
struct archive;
/* archive_check_entitlement.c stubs — allow everything */
bool archive_allow_entitlement_format(const char *f) { (void)f; return true; }
bool archive_allow_entitlement_filter(const char *f) { (void)f; return true; }
void archive_entitlement_cleanup(void) {}
/* archive_mac.c stub — quarantine is a no-op */
void archive_read_get_quarantine_from_fd(struct archive *a, int fd) { (void)a; (void)fd; }
STUB
"${CC}" ${TARGET_CFLAGS} -O2 -c "${WORKDIR}/panthera_libarchive_stubs.c" -o "${WORKDIR}/panthera_libarchive_stubs.o"

"${SRCDIR}/configure" \
    --host="${TARGET}" \
    --build="${TARGET}" \
    --prefix=/usr \
    --with-zlib \
    --with-bz2lib \
    --with-expat \
    --without-xml2 \
    --without-lzma \
    --without-lz4 \
    --without-zstd \
    --without-openssl \
    --without-nettle \
    --disable-acls \
    --disable-xattr \
    --disable-static \
    --enable-bsdtar=shared \
    --enable-bsdcpio=shared \
    CC="${CC} ${TARGET_CFLAGS}" \
    CFLAGS="-O2 -I${SHIMDIR} -I${EXPAT_INC} -I${ZLIB_STAGE}/include -I${BZIP2_SRCDIR} -Wno-deprecated-declarations" \
    CPPFLAGS="-I${SHIMDIR} -I${EXPAT_INC} -I${ZLIB_STAGE}/include -I${BZIP2_SRCDIR}" \
    LDFLAGS="${TARGET_CFLAGS} -L${EXPAT_LIB} -L${ZLIB_STAGE}/lib -L${BZIP2_BUILD} -Wl,-not_for_dyld_shared_cache" \
    LIBS="-lSystem -lexpat -lz -lbz2 ${WORKDIR}/panthera_libarchive_stubs.o"

make -j"${JOBS}" AUTOHEADER=true

# Collect outputs
LIBARCHIVE_DYLIB="${BUILDDIR}/.libs/libarchive.13.dylib"
if [[ ! -f "${LIBARCHIVE_DYLIB}" ]]; then
    LIBARCHIVE_DYLIB="${BUILDDIR}/.libs/libarchive.dylib"
fi

install_name_tool -id /usr/lib/libarchive.13.dylib "${LIBARCHIVE_DYLIB}"
install_name_tool -change "${ZLIB_STAGE}/lib/libz.1.dylib" /usr/lib/libz.1.dylib "${LIBARCHIVE_DYLIB}" 2>/dev/null || true
install_name_tool -change "${ZLIB_STAGE}/lib/libz.1.3.1.dylib" /usr/lib/libz.1.dylib "${LIBARCHIVE_DYLIB}" 2>/dev/null || true

# bsdtar and bsdcpio may be in .libs/ or directly in the build dir
BSDTAR="${BUILDDIR}/bsdtar"
if [[ -f "${BUILDDIR}/tar/.libs/bsdtar" ]]; then
    BSDTAR="${BUILDDIR}/tar/.libs/bsdtar"
elif [[ -f "${BUILDDIR}/.libs/bsdtar" ]]; then
    BSDTAR="${BUILDDIR}/.libs/bsdtar"
fi

BSDCPIO="${BUILDDIR}/bsdcpio"
if [[ -f "${BUILDDIR}/cpio/.libs/bsdcpio" ]]; then
    BSDCPIO="${BUILDDIR}/cpio/.libs/bsdcpio"
elif [[ -f "${BUILDDIR}/.libs/bsdcpio" ]]; then
    BSDCPIO="${BUILDDIR}/.libs/bsdcpio"
fi

# Fix the dylib references in the binaries
for bin in "${BSDTAR}" "${BSDCPIO}"; do
    if [[ -f "${bin}" ]]; then
        install_name_tool -change "${BUILDDIR}/.libs/libarchive.13.dylib" /usr/lib/libarchive.13.dylib "${bin}" 2>/dev/null || true
        install_name_tool -change "${ZLIB_STAGE}/lib/libz.1.dylib" /usr/lib/libz.1.dylib "${bin}" 2>/dev/null || true
        install_name_tool -change "${ZLIB_STAGE}/lib/libz.1.3.1.dylib" /usr/lib/libz.1.dylib "${bin}" 2>/dev/null || true
    fi
done

# Stage outputs
cp "${LIBARCHIVE_DYLIB}" "${OUT_LIB}/libarchive.13.dylib"
ln -sf libarchive.13.dylib "${OUT_LIB}/libarchive.dylib"
cp "${BSDTAR}" "${OUT_BIN}/bsdtar" 2>/dev/null || true
cp "${BSDCPIO}" "${OUT_BIN}/bsdcpio" 2>/dev/null || true

# Copy headers
cp "${SRCDIR}/libarchive/archive.h" "${OUT_INC}/"
cp "${SRCDIR}/libarchive/archive_entry.h" "${OUT_INC}/"

# Stage to sysroot
cp "${OUT_LIB}/libarchive.13.dylib" "${SYSROOT}/usr/lib/libarchive.13.dylib"
ln -sf libarchive.13.dylib "${SYSROOT}/usr/lib/libarchive.dylib"
cp "${OUT_INC}/archive.h" "${SYSROOT}/usr/include/"
cp "${OUT_INC}/archive_entry.h" "${SYSROOT}/usr/include/"

# Audit all outputs
AUDIT_FILES=("${OUT_LIB}/libarchive.13.dylib")
[[ -f "${OUT_BIN}/bsdtar" ]] && AUDIT_FILES+=("${OUT_BIN}/bsdtar")
[[ -f "${OUT_BIN}/bsdcpio" ]] && AUDIT_FILES+=("${OUT_BIN}/bsdcpio")
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${AUDIT_FILES[@]}"

echo "Built libarchive.13.dylib: $(wc -c < "${OUT_LIB}/libarchive.13.dylib") bytes"
[[ -f "${OUT_BIN}/bsdtar" ]] && echo "Built bsdtar: $(wc -c < "${OUT_BIN}/bsdtar") bytes"
[[ -f "${OUT_BIN}/bsdcpio" ]] && echo "Built bsdcpio: $(wc -c < "${OUT_BIN}/bsdcpio") bytes"
