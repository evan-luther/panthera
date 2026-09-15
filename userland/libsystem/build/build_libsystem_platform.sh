#!/bin/bash
set -euo pipefail

# Build script for libsystem_platform.dylib
# Part of the Panthera Darwin project

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
SRCDIR="${PANTHERA_ROOT}/src/libplatform-306.0.1"
XNUDIR="${PANTHERA_ROOT}/src/xnu-10002.41.9"
PTHREADDIR="${PANTHERA_ROOT}/src/libpthread-519/private"
SOURCESDIR="${BUILDDIR}/sources"
OBJDIR="${BUILDDIR}/obj/libsystem_platform"
SYSROOT="${BUILDDIR}/sysroot"
OUTDIR="${SYSROOT}/usr/lib/system"
OUT="${OUTDIR}/libsystem_platform.dylib"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
SHIMS="${SOURCESDIR}/panthera_platform_shims.h"
SIGACTION_SRC="${SOURCESDIR}/panthera_platform_sigaction.c"

SDK_PATH="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
TARGET="x86_64-apple-darwin23.0"

# Ensure dyld_stub_binder.o is built
"${BUILDDIR}/build_stub_binder.sh"

required=(
    "${SHIMS}"
    "${SIGACTION_SRC}"
    "${PTHREADDIR}/pthread/tsd_private.h"
    "${XNUDIR}/EXTERNAL_HEADERS/architecture/i386/asm_help.h"
    "${DYLD_STUB}"
    "${SRCDIR}/src/os/alloc_once.c"
    "${SRCDIR}/src/os/lock.c"
    "${SRCDIR}/src/os/atomic.c"
    "${SRCDIR}/src/os/semaphore.c"
    "${SRCDIR}/src/cachecontrol/generic/cache.c"
    "${SRCDIR}/src/cachecontrol/x86_64/cache.s"
    "${SRCDIR}/src/setjmp/x86_64/_sigtramp.s"
    "${SRCDIR}/src/setjmp/x86_64/setjmp.s"
    "${SRCDIR}/src/setjmp/x86_64/_setjmp.s"
    "${SRCDIR}/src/simple/getenv.c"
    "${SRCDIR}/src/string/generic/bzero.c"
    "${SRCDIR}/src/string/generic/ffsll.c"
    "${SRCDIR}/src/string/generic/flsll.c"
    "${SRCDIR}/src/string/generic/memccpy.c"
    "${SRCDIR}/src/string/generic/memchr.c"
    "${SRCDIR}/src/string/generic/memcmp.c"
    "${SRCDIR}/src/string/generic/memcmp_zero.c"
    "${SRCDIR}/src/string/generic/memmove.c"
    "${SRCDIR}/src/string/generic/memset_pattern.c"
    "${SRCDIR}/src/string/generic/strchr.c"
    "${SRCDIR}/src/string/generic/strcmp.c"
    "${SRCDIR}/src/string/generic/strcpy.c"
    "${SRCDIR}/src/string/generic/strlcat.c"
    "${SRCDIR}/src/string/generic/strlcpy.c"
    "${SRCDIR}/src/string/generic/strlen.c"
    "${SRCDIR}/src/string/generic/strncmp.c"
    "${SRCDIR}/src/string/generic/strncpy.c"
    "${SRCDIR}/src/string/generic/strnlen.c"
    "${SRCDIR}/src/string/generic/strstr.c"
    "${SRCDIR}/src/ucontext/generic/setcontext.c"
    "${SRCDIR}/src/ucontext/generic/swapcontext.c"
    "${SRCDIR}/src/ucontext/x86_64/getcontext.s"
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
    -std=gnu11
    -c -fPIC -O2
    -DPRIVATE
    -D_FORTIFY_SOURCE=0
    -fno-stack-protector
    -include "${BUILDDIR}/compat_include/Availability.h"
    -include "${SHIMS}"
    -I"${BUILDDIR}/compat_include"
    -I"${SRCDIR}/include"
    -I"${PTHREADDIR}"
    -I"${SRCDIR}/private"
    -I"${SRCDIR}/internal"
    -I"${SRCDIR}/src"
    -I"${SRCDIR}/src/os/resolver"
    -I"${XNUDIR}/osfmk"
    -I"${XNUDIR}/libkern"
    -I"${XNUDIR}/libsyscall"
    -I"${SYSROOT}/usr/include"
    -I"${XNUDIR}/bsd"
    -I"${SDK_PATH}/usr/include"
    -DOSATOMIC_USE_INLINED=0
    -DOSATOMIC_DEPRECATED=0
    -DOSSPINLOCK_USE_INLINED=0
    -DOSSPINLOCK_DEPRECATED=0
    -DOS_UNFAIR_LOCK_INLINE=0
    -Wno-everything
)

C_SRCS=(
    "${SRCDIR}/src/os/alloc_once.c"
    "${SRCDIR}/src/os/lock.c"
    "${SRCDIR}/src/os/atomic.c"
    "${SRCDIR}/src/os/semaphore.c"
    "${SRCDIR}/src/cachecontrol/generic/cache.c"
    "${SRCDIR}/src/simple/getenv.c"
    "${SRCDIR}/src/string/generic/bzero.c"
    "${SRCDIR}/src/string/generic/ffsll.c"
    "${SRCDIR}/src/string/generic/flsll.c"
    "${SRCDIR}/src/string/generic/memccpy.c"
    "${SRCDIR}/src/string/generic/memchr.c"
    "${SRCDIR}/src/string/generic/memcmp.c"
    "${SRCDIR}/src/string/generic/memcmp_zero.c"
    "${SRCDIR}/src/string/generic/memmove.c"
    "${SRCDIR}/src/string/generic/memset_pattern.c"
    "${SRCDIR}/src/string/generic/strchr.c"
    "${SRCDIR}/src/string/generic/strcmp.c"
    "${SRCDIR}/src/string/generic/strcpy.c"
    "${SRCDIR}/src/string/generic/strlcat.c"
    "${SRCDIR}/src/string/generic/strlcpy.c"
    "${SRCDIR}/src/string/generic/strlen.c"
    "${SRCDIR}/src/string/generic/strncmp.c"
    "${SRCDIR}/src/string/generic/strncpy.c"
    "${SRCDIR}/src/string/generic/strnlen.c"
    "${SRCDIR}/src/string/generic/strstr.c"
    "${SRCDIR}/src/ucontext/generic/setcontext.c"
    "${SRCDIR}/src/ucontext/generic/swapcontext.c"
    "${SIGACTION_SRC}"
)

S_SRCS=(
    "${SRCDIR}/src/cachecontrol/x86_64/cache.s"
    "${SRCDIR}/src/setjmp/x86_64/_sigtramp.s"
    "${SRCDIR}/src/setjmp/x86_64/setjmp.s"
    "${SRCDIR}/src/setjmp/x86_64/_setjmp.s"
    "${SRCDIR}/src/ucontext/x86_64/getcontext.s"
)

echo ">>> Building libsystem_platform"

OBJS=()
for src in "${C_SRCS[@]}"; do
    base="$(basename "${src}" .c)"
    if [[ "${src}" == *"/cachecontrol/"* ]]; then
        objname="cache_c.o"
    else
        objname="${base}.o"
    fi
    "${CC}" "${CFLAGS[@]}" "${src}" -o "${OBJDIR}/${objname}"
    OBJS+=("${OBJDIR}/${objname}")
done

for src in "${S_SRCS[@]}"; do
    base="$(basename "${src}" .s)"
    if [[ "${src}" == *"/cachecontrol/"* ]]; then
        objname="cache_s.o"
    else
        objname="${base}.o"
    fi
    "${CC}" -target "${TARGET}" -mmacosx-version-min=14.0 -c \
        -I"${SYSROOT}/usr/include" \
        -I"${SRCDIR}/include" \
        -I"${SRCDIR}/private" \
        -I"${SRCDIR}/internal" \
        -I"${XNUDIR}/EXTERNAL_HEADERS" \
        -I"${XNUDIR}/libsyscall" \
        -I"${XNUDIR}/osfmk" \
        "${src}" -o "${OBJDIR}/${objname}"
    OBJS+=("${OBJDIR}/${objname}")
done

xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libsystem_platform.dylib \
    -compatibility_version 0.0.0 \
    -current_version 0.0.0 \
    -unexported_symbol __setjmp \
    -unexported_symbol __longjmp \
    -o "${OUT}" \
    "${OBJS[@]}" \
    "${DYLD_STUB}" \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0

echo "Output: ${OUT}"
