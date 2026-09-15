#!/usr/bin/env bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src/libffi-40"
OUT_LIB="${PANTHERA_ROOT}/userland/libffi/lib"
OUT_INC="${PANTHERA_ROOT}/userland/libffi/include"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"

mkdir -p "${OUT_LIB}" "${OUT_INC}"

if [[ "${PANTHERA_FORCE_REBUILD:-0}" != "1" && -f "${OUT_LIB}/libffi.dylib" ]]; then
    echo "libffi already built"
    exit 0
fi

WORKDIR="$(mktemp -d /tmp/panthera-libffi-build.XXXXXX)"
trap 'rm -rf "${WORKDIR}"' EXIT

CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -O2"
# Apple's darwin/ dir has the pregenerated config headers
# Create shim to neutralize Apple availability macros
SHIMDIR="${WORKDIR}/shims"
mkdir -p "${SHIMDIR}"
cat > "${SHIMDIR}/ffi_panthera_shim.h" <<'SHIM'
/* Neutralize Apple availability macros for Panthera */
#undef FFI_AVAILABLE_APPLE
#undef FFI_AVAILABLE_APPLE_2019
#define FFI_AVAILABLE_APPLE
#define FFI_AVAILABLE_APPLE_2019
SHIM

CFLAGS="${CFLAGS} -I${SRCDIR}/darwin/include -I${SRCDIR}/include -I${SRCDIR}/src/x86"
CFLAGS="${CFLAGS} -DFFI_BUILDING -DHAVE_CONFIG_H"
CFLAGS="${CFLAGS} -DSPI_AVAILABLE(...)= -Diosmac=macCatalyst"

ASFLAGS="-target ${TARGET} -isysroot ${SDKROOT}"
ASFLAGS="${ASFLAGS} -I${SRCDIR}/darwin/include -I${SRCDIR}/include"

# C source files
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/closures.c" -o "${WORKDIR}/closures.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/java_raw_api.c" -o "${WORKDIR}/java_raw_api.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/prep_cif.c" -o "${WORKDIR}/prep_cif.o" 2>/dev/null || \
    "${CC}" ${CFLAGS} -fPIC -Wno-error -c "${SRCDIR}/src/prep_cif.c" -o "${WORKDIR}/prep_cif.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/raw_api.c" -o "${WORKDIR}/raw_api.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/types.c" -o "${WORKDIR}/types.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/x86/ffi64.c" -o "${WORKDIR}/ffi64.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/x86/ffi.c" -o "${WORKDIR}/ffi.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/x86/ffiw64.c" -o "${WORKDIR}/ffiw64.o"
"${CC}" ${CFLAGS} -fPIC -c "${SRCDIR}/src/tramp.c" -o "${WORKDIR}/tramp.o"

# Assembly files (x86_64)
"${CC}" ${ASFLAGS} -c "${SRCDIR}/src/x86/unix64.S" -o "${WORKDIR}/unix64.o"
"${CC}" ${ASFLAGS} -c "${SRCDIR}/src/x86/sysv.S" -o "${WORKDIR}/sysv.o"
"${CC}" ${ASFLAGS} -c "${SRCDIR}/src/x86/win64.S" -o "${WORKDIR}/win64.o"

# Link
"${CC}" -target "${TARGET}" -isysroot "${SDKROOT}" \
    -dynamiclib -install_name /usr/lib/libffi.dylib \
    -compatibility_version 1.0.0 -current_version 1.0.0 \
    -Wl,-not_for_dyld_shared_cache \
    -o "${WORKDIR}/libffi.dylib" \
    "${WORKDIR}"/*.o \
    -lSystem

# Stage outputs
cp "${WORKDIR}/libffi.dylib" "${OUT_LIB}/libffi.dylib"

# Copy headers
cp "${SRCDIR}/darwin/include/ffi.h" "${OUT_INC}/"
cp "${SRCDIR}/darwin/include/ffitarget.h" "${OUT_INC}/"
cp "${SRCDIR}/darwin/include/tramp.h" "${OUT_INC}/" 2>/dev/null || true

# Stage to sysroot
cp "${OUT_LIB}/libffi.dylib" "${SYSROOT}/usr/lib/libffi.dylib"
mkdir -p "${SYSROOT}/usr/include/ffi"
cp "${OUT_INC}/ffi.h" "${SYSROOT}/usr/include/"
cp "${OUT_INC}/ffitarget.h" "${SYSROOT}/usr/include/"
cp "${OUT_INC}/ffi.h" "${SYSROOT}/usr/include/ffi/"
cp "${OUT_INC}/ffitarget.h" "${SYSROOT}/usr/include/ffi/"

# Audit
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUT_LIB}/libffi.dylib"

echo "Built libffi: ${OUT_LIB}/libffi.dylib ($(wc -c < "${OUT_LIB}/libffi.dylib") bytes)"
