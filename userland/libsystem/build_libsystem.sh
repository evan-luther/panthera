#!/bin/bash
# build_libsystem.sh — Build libSystem.B.dylib for Panthera Darwin (x86_64)
#
# Builds the full dynamic library stack:
#   1. Generate syscall stubs from XNU's syscalls.master
#   2. Build libsystem_kernel.dylib (syscall wrappers + Mach interfaces)
#   3. Build libsystem_platform.dylib (atomics, setjmp, OS primitives)
#   4. Build libsystem_malloc.dylib (memory allocator)
#   5. Build libsystem_c.dylib (C library)
#   6. Build libsystem_pthread.dylib (POSIX threads)
#   7. Build libdyld.dylib (dynamic linker library)
#   8. Build libdispatch.dylib (GCD)
#   9. Assemble libSystem.B.dylib (umbrella)
#  10. Build dyld (dynamic linker executable)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src"
XNUSRC="${SRCDIR}/xnu-10002.41.9"

# Build output
BUILDDIR="${SCRIPT_DIR}/build"
SYSROOT="${BUILDDIR}/sysroot"
OBJDIR="${BUILDDIR}/obj"

# Target
TARGET="x86_64-apple-darwin23.0"
DEPLOYMENT_TARGET="14.0"

# Tools
CC="$(xcrun -sdk macosx -find clang)"
CXX="$(xcrun -sdk macosx -find clang++)"
SDKROOT="$(xcrun -sdk macosx -show-sdk-path)"

# Sysroot directories (where built libs and headers go)
if [[ "${PANTHERA_FORCE_REBUILD:-0}" == "1" ]]; then
    rm -rf "${SYSROOT}" "${OBJDIR}"
fi
mkdir -p "${SYSROOT}/usr/lib/system" "${SYSROOT}/usr/lib" "${SYSROOT}/usr/include"
mkdir -p "${OBJDIR}"

echo "============================================"
echo "Panthera libSystem Build"
echo "============================================"
echo "Target:  ${TARGET}"
echo "SDK:     ${SDKROOT}"
echo "Output:  ${SYSROOT}"
echo ""

# Common flags for userland dynamic libraries
CFLAGS_COMMON=(
    -target "${TARGET}"
    -mmacosx-version-min="${DEPLOYMENT_TARGET}"
    -fPIC
    -O2 -g
    -Wall
    -Wno-error
    -Wno-unused-parameter
    -Wno-sign-conversion
    -Wno-shorten-64-to-32
    -Wno-missing-field-initializers
    -Wno-implicit-function-declaration
    -Wno-format
    -Wno-deprecated-declarations
    -Wno-nullability-completeness
    -Wno-expansion-to-defined
    -D__DARWIN_UNIX03=1
    -DPRIVATE=1
    -D__APPLE__=1
)

LDFLAGS_COMMON=(
    -target "${TARGET}"
    -mmacosx-version-min="${DEPLOYMENT_TARGET}"
    -dynamiclib
    -install_name /usr/lib/system/LIBNAME.dylib
)

# ──────────────────────────────────────────────
# Phase 1: Generate syscall stubs
# ──────────────────────────────────────────────
echo ">>> Phase 1: Generating syscall stubs"

SYSCALL_DIR="${OBJDIR}/syscalls"
mkdir -p "${SYSCALL_DIR}"

perl "${XNUSRC}/libsyscall/xcodescripts/create-syscalls.pl" \
    "${XNUSRC}/bsd/kern/syscalls.master" \
    "${XNUSRC}/libsyscall/custom" \
    "${XNUSRC}/libsyscall/Platforms" \
    "MacOSX" \
    "${SYSCALL_DIR}" 2>&1 | tail -3

STUB_COUNT=$(find "${SYSCALL_DIR}" -name '*.s' | wc -l | tr -d ' ')
echo "  Generated ${STUB_COUNT} syscall stubs"

# ──────────────────────────────────────────────
# Phase 2: Install headers into sysroot
# ──────────────────────────────────────────────
echo ""
echo ">>> Phase 2: Installing headers"
# Stale sysroot xpc directory shadows SDK <xpc/base.h>
rm -rf "${SYSROOT}/usr/include/xpc"

# XNU public headers
for dir in bsd/sys bsd/machine bsd/i386 bsd/net bsd/netinet bsd/netinet6 \
           osfmk/mach osfmk/mach/machine osfmk/mach/i386 \
           osfmk/kern; do
    src="${XNUSRC}/${dir}"
    case "$dir" in
        bsd/*) dest="${SYSROOT}/usr/include/${dir#bsd/}" ;;
        osfmk/*) dest="${SYSROOT}/usr/include/${dir#osfmk/}" ;;
    esac
    if [ -d "$src" ]; then
        mkdir -p "$dest"
        cp -n "$src"/*.h "$dest/" 2>/dev/null || true
    fi
done

# Stage libsyscall mach_init.h into sysroot/usr/include/mach with overwrite semantics
MACH_INIT_SRC="${XNUSRC}/libsyscall/mach/mach/mach_init.h"
MACH_INIT_DEST="${SYSROOT}/usr/include/mach/mach_init.h"
if [[ ! -f "${MACH_INIT_SRC}" ]]; then
    echo "Error: missing required mach_init.h source at ${MACH_INIT_SRC}" >&2
    exit 1
fi
mkdir -p "${SYSROOT}/usr/include/mach"
cp -f "${MACH_INIT_SRC}" "${MACH_INIT_DEST}"
if [[ ! -f "${MACH_INIT_DEST}" ]]; then
    echo "Error: failed to stage ${MACH_INIT_DEST}" >&2
    exit 1
fi
perl -i -0777 -pe 's/__API_AVAILABLE\([^\n]+\)\s*\n\s*extern boolean_t mach_task_is_self\(task_name_t task\);\n//g' "${MACH_INIT_DEST}"

# XNU exported headers (already built)
XNU_EXPORT="${PANTHERA_ROOT}/BUILD/obj/EXPORT_HDRS"
if [ -d "${XNU_EXPORT}" ]; then
    for comp in bsd osfmk pexpert libkern; do
        if [ -d "${XNU_EXPORT}/${comp}" ]; then
            cp -Rn "${XNU_EXPORT}/${comp}/"* "${SYSROOT}/usr/include/" 2>/dev/null || true
        fi
    done
fi

# Libc headers
cp -Rn "${SRCDIR}/Libc-1583.40.7/include/"* "${SYSROOT}/usr/include/" 2>/dev/null || true
# Stage consumer compatibility shim for xlocale_private.h
XLOCALE_PRIVATE_SHIM="${PANTHERA_ROOT}/userland/corefoundation/shims/xlocale_private.h"
XLOCALE_PRIVATE_DEST="${SYSROOT}/usr/include/xlocale_private.h"
if [[ ! -f "${XLOCALE_PRIVATE_SHIM}" ]]; then
    echo "Error: missing required xlocale_private.h compatibility shim at ${XLOCALE_PRIVATE_SHIM}" >&2
    exit 1
fi
mkdir -p "${SYSROOT}/usr/include"
cp -f "${XLOCALE_PRIVATE_SHIM}" "${XLOCALE_PRIVATE_DEST}"
if [[ ! -f "${XLOCALE_PRIVATE_DEST}" ]]; then
    echo "Error: failed to stage ${XLOCALE_PRIVATE_DEST}" >&2
    exit 1
fi

# libpthread headers
cp -Rn "${SRCDIR}/libpthread-519/include/"* "${SYSROOT}/usr/include/" 2>/dev/null || true

# libplatform headers
cp -Rn "${SRCDIR}/libplatform-306.0.1/include/"* "${SYSROOT}/usr/include/" 2>/dev/null || true
cp -Rn "${SRCDIR}/libplatform-306.0.1/private/"* "${SYSROOT}/usr/include/" 2>/dev/null || true

# libmalloc headers
cp -Rn "${SRCDIR}/libmalloc-474.0.13/include/"* "${SYSROOT}/usr/include/" 2>/dev/null || true
cp -Rn "${SRCDIR}/libmalloc-474.0.13/private/"* "${SYSROOT}/usr/include/" 2>/dev/null || true

# libdispatch headers
mkdir -p "${SYSROOT}/usr/include/dispatch" "${SYSROOT}/usr/include/os"
cp -Rf "${SRCDIR}/libdispatch-1462.0.4/dispatch/"* "${SYSROOT}/usr/include/dispatch/" 2>/dev/null || true
cp -Rf "${SRCDIR}/libdispatch-1462.0.4/os/"* "${SYSROOT}/usr/include/os/" 2>/dev/null || true

# libsyscall headers
cp -Rn "${XNUSRC}/libsyscall/wrappers/"*.h "${SYSROOT}/usr/include/" 2>/dev/null || true

echo "  Headers installed to ${SYSROOT}/usr/include"
HEADER_COUNT=$(find "${SYSROOT}/usr/include" -name '*.h' | wc -l | tr -d ' ')
echo "  ${HEADER_COUNT} header files"

echo ""
echo ">>> Phase 3: Building libSystem constituents"

builders=(
    "${BUILDDIR}/build_libsystem_kernel.sh"
    "${BUILDDIR}/build_libsystem_platform.sh"
    "${BUILDDIR}/build_libsystem_malloc.sh"
    "${BUILDDIR}/build_libsystem_c.sh"
    "${BUILDDIR}/build_libsystem_pthread.sh"
    "${BUILDDIR}/build_libsystem_trace.sh"
    "${BUILDDIR}/build_libsystem_sandbox.sh"
    "${BUILDDIR}/build_libclosure.sh"
    "${BUILDDIR}/build_libxpc.sh"
    "${BUILDDIR}/build_libsystem_info.sh"
    "${PANTHERA_ROOT}/userland/notifyd/build_notify_client.sh"
    "${PANTHERA_ROOT}/userland/objc4/build_objc4.sh"
    "${BUILDDIR}/build_libdispatch.sh"
)

for builder in "${builders[@]}"; do
    if [[ ! -x "${builder}" ]]; then
        echo "missing executable constituent builder: ${builder}" >&2
        exit 1
    fi
    echo ""
    echo ">>> Running ${builder#${PANTHERA_ROOT}/}"
    "${builder}"
done

required_outputs=(
    libsystem_kernel.dylib
    libsystem_platform.dylib
    libsystem_malloc.dylib
    libsystem_c.dylib
    libsystem_pthread.dylib
    libsystem_trace.dylib
    libsystem_sandbox.dylib
    libclosure.dylib
    libxpc.dylib
    libsystem_info.dylib
    libsystem_notify.dylib
    libdispatch.dylib
)

for output in "${required_outputs[@]}"; do
    path="${SYSROOT}/usr/lib/system/${output}"
    if [[ ! -s "${path}" ]]; then
        echo "missing or empty constituent output: ${path}" >&2
        exit 1
    fi
done

echo ""
echo "============================================"
echo "libSystem constituents complete"
echo "  Syscall stubs: ${STUB_COUNT}"
echo "  Headers: ${HEADER_COUNT}"
echo "  Libraries: ${#required_outputs[@]}"
echo "  Sysroot: ${SYSROOT}"
echo "============================================"
