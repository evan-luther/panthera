#!/bin/bash
set -euo pipefail

# Build script for libsystem_malloc.dylib
# Part of the Panthera Darwin project

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
SRCDIR="${PANTHERA_ROOT}/src/libmalloc-474.0.13"
XNUDIR="${PANTHERA_ROOT}/src/xnu-10002.41.9"
DYLDINCLUDE="${PANTHERA_ROOT}/src/dyld-1122.1.2/include"
PTHREADINCLUDE="${PANTHERA_ROOT}/src/libpthread-519/private"
LIBCGEN="${PANTHERA_ROOT}/src/Libc-1583.40.7/gen"
AVAILABILITYDIR="${PANTHERA_ROOT}/src/AvailabilityVersions-137.4/templates"
SOURCESDIR="${BUILDDIR}/sources"
OBJDIR="${BUILDDIR}/obj/libsystem_malloc"
SYSROOT="${BUILDDIR}/sysroot"
OUTDIR="${SYSROOT}/usr/lib/system"
OUT="${OUTDIR}/libsystem_malloc.dylib"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
SHIMS="${SOURCESDIR}/panthera_malloc_shims.h"

SDK_PATH="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
TARGET="x86_64-apple-darwin23.0"

# Ensure dyld_stub_binder.o is built
"${BUILDDIR}/build_stub_binder.sh"

required=(
    "${SHIMS}"
    "${BUILDDIR}/compat_include/os/feature_private.h"
    "${BUILDDIR}/compat_include/mach-o/dyld_priv.h"
    "${DYLDINCLUDE}/mach-o/dyld_priv.h"
    "${AVAILABILITYDIR}/for_dyld_priv.inc"
    "${AVAILABILITYDIR}/../availability"
    "${AVAILABILITYDIR}/../availability.dsl"
    "${XNUDIR}/libsyscall/os/tsd.h"
    "${PTHREADINCLUDE}/pthread/private.h"
    "${LIBCGEN}/thread_stack_pcs.h"
    "${DYLD_STUB}"
    "${SRCDIR}/src/malloc.c"
    "${SRCDIR}/src/malloc_common.c"
    "${SRCDIR}/src/malloc_printf.c"
    "${SRCDIR}/src/malloc_type.c"
    "${SRCDIR}/src/magazine_malloc.c"
    "${SRCDIR}/src/magazine_rack.c"
    "${SRCDIR}/src/magazine_large.c"
    "${SRCDIR}/src/magazine_small.c"
    "${SRCDIR}/src/magazine_tiny.c"
    "${SRCDIR}/src/magazine_medium.c"
    "${SRCDIR}/src/nano_malloc_common.c"
    "${SRCDIR}/src/nanov2_malloc.c"
    "${SRCDIR}/src/purgeable_malloc.c"
    "${SRCDIR}/src/frozen_malloc.c"
    "${SRCDIR}/src/legacy_malloc.c"
    "${SRCDIR}/src/vm.c"
    "${SRCDIR}/src/bitarray.c"
    "${SRCDIR}/src/pgm_malloc.c"
    "${SRCDIR}/src/sanitizer_malloc.c"
    "${SRCDIR}/src/stack_trace.c"
    "${SRCDIR}/src/early_malloc.c"
    "${SRCDIR}/src/has_section.c"
    "${SRCDIR}/src/msl_lite_support.c"
)

for path in "${required[@]}"; do
    if [[ ! -f "${path}" ]]; then
        echo "missing required input: ${path}" >&2
        exit 1
    fi
done

mkdir -p "${OBJDIR}" "${OUTDIR}"

CFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -c -fPIC -O2
    -fno-stack-protector
    -include "${BUILDDIR}/compat_include/Availability.h"
    -D__STDC_HOSTED__=0
    -include stdatomic.h
    -include "${SHIMS}"
    -I"${BUILDDIR}/compat_include"
    -I"${SRCDIR}/src"
    -I"${SRCDIR}/include"
    -I"${DYLDINCLUDE}"
    -I"${PTHREADINCLUDE}"
    -I"${LIBCGEN}"
    -I"${SRCDIR}/private"
    -I"${SRCDIR}/resolver"
    -I"${XNUDIR}/libsyscall"
    -I"${SYSROOT}/usr/include"
    -I"${XNUDIR}/osfmk"
    -I"${XNUDIR}/EXTERNAL_HEADERS"
    -I"${SDK_PATH}/usr/include"
    -DPRIVATE
    -DDYLD_EXCLAVEKIT_UNAVAILABLE=
    -D__DARWIN_UNIX03=1
)

if [[ "${PANTHERA_MALLOC_TRACE:-0}" == "1" ]]; then
    CFLAGS+=("-DPANTHERA_MALLOC_TRACE=1")
fi

CFLAGS+=("-Wno-everything")

SOURCES=(
    "${SRCDIR}/src/malloc.c"
    "${SRCDIR}/src/malloc_common.c"
    "${SRCDIR}/src/malloc_printf.c"
    "${SRCDIR}/src/malloc_type.c"
    "${SRCDIR}/src/magazine_malloc.c"
    "${SRCDIR}/src/magazine_rack.c"
    "${SRCDIR}/src/magazine_large.c"
    "${SRCDIR}/src/magazine_small.c"
    "${SRCDIR}/src/magazine_tiny.c"
    "${SRCDIR}/src/magazine_medium.c"
    "${SRCDIR}/src/nano_malloc_common.c"
    "${SRCDIR}/src/nanov2_malloc.c"
    "${SRCDIR}/src/purgeable_malloc.c"
    "${SRCDIR}/src/frozen_malloc.c"
    "${SRCDIR}/src/legacy_malloc.c"
    "${SRCDIR}/src/vm.c"
    "${SRCDIR}/src/bitarray.c"
    "${SRCDIR}/src/pgm_malloc.c"
    "${SRCDIR}/src/sanitizer_malloc.c"
    "${SRCDIR}/src/stack_trace.c"
    "${SRCDIR}/src/early_malloc.c"
    "${SRCDIR}/src/has_section.c"
    "${SRCDIR}/src/msl_lite_support.c"
)

echo ">>> Building libsystem_malloc"

OBJS=()
for src in "${SOURCES[@]}"; do
    base="$(basename "${src}" .c)"
    "${CC}" "${CFLAGS[@]}" "${src}" -o "${OBJDIR}/${base}.o"
    OBJS+=("${OBJDIR}/${base}.o")
done

xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libsystem_malloc.dylib \
    -compatibility_version 0.0.0 \
    -current_version 0.0.0 \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0 \
    "${DYLD_STUB}" \
    "${OBJS[@]}" \
    -o "${OUT}"

echo "Output: ${OUT}"
