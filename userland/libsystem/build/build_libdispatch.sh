#!/bin/bash
set -euo pipefail

# Build script for libdispatch.dylib
# Part of the Panthera Darwin project

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj/libdispatch"
SOURCESDIR="${BUILDDIR}/sources"
SHIMDIR="${BUILDDIR}/shims/libdispatch"
SYSROOT="${BUILDDIR}/sysroot"
OUTDIR="${SYSROOT}/usr/lib/system"
OUT="${OUTDIR}/libdispatch.dylib"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"

SRCDIR="${PANTHERA_ROOT}/src/libdispatch-1462.0.4"
XNU_SRC="${PANTHERA_ROOT}/src/xnu-10002.41.9"
LIBC_SRC="${PANTHERA_ROOT}/src/Libc-1583.40.7"
LIBPLATFORM_SRC="${PANTHERA_ROOT}/src/libplatform-306.0.1"
LIBPTHREAD_SRC="${PANTHERA_ROOT}/src/libpthread-519"
LIBCLOSURE_SRC="${PANTHERA_ROOT}/src/libclosure-90"
OBJC4_SRC="${PANTHERA_ROOT}/src/objc4-906"
OBJC4_LIB="${PANTHERA_ROOT}/userland/objc4/lib/libobjc.A.dylib"

SDK_PATH="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
CXX="$(xcrun -sdk macosx -find clang++)"
MIG="$(xcrun -sdk macosx -find mig)"
TARGET="x86_64-apple-darwin23.0"

required=(
    "${SRCDIR}"
    "${SRCDIR}/src/allocator.c"
    "${SRCDIR}/src/block.cpp"
    "${SRCDIR}/src/protocol.defs"
    "${SRCDIR}/xcodeconfig/libdispatch.aliases"
    "${XNU_SRC}"
    "${LIBC_SRC}"
    "${LIBPLATFORM_SRC}"
    "${LIBPTHREAD_SRC}"
    "${LIBCLOSURE_SRC}"
    "${SHIMDIR}/panthera_workgroup_stub.c"
    "${SHIMDIR}/event_kevent.c"
    "${SHIMDIR}/object_internal.h"
    "${SHIMDIR}/inline_internal.h"
    "${SHIMDIR}/object.c"
    "${SHIMDIR}/queue.c"
    "${SHIMDIR}/source.c"
    "${SHIMDIR}/panthera_dispatch_compat.h"
    "${SHIMDIR}/config/config.h"
    "${SHIMDIR}/patch_sources.py"
    "${BUILDDIR}/build_stub_binder.sh"
    "${BUILDDIR}/compat_include/Availability.h"
    "${OUTDIR}/libsystem_kernel.dylib"
    "${OUTDIR}/libsystem_platform.dylib"
    "${OUTDIR}/libsystem_pthread.dylib"
    "${OUTDIR}/libsystem_c.dylib"
    "${OBJC4_LIB}"
)

for path in "${required[@]}"; do
    if [[ ! -e "${path}" ]]; then
        echo "missing required input: ${path}" >&2
        exit 1
    fi
done

"${BUILDDIR}/build_stub_binder.sh"

if [[ ! -f "${DYLD_STUB}" ]]; then
    echo "missing required input: ${DYLD_STUB}" >&2
    exit 1
fi

mkdir -p "${OBJDIR}" "${OUTDIR}" "${SYSROOT}/usr/include/dispatch" "${SYSROOT}/usr/include/os"

# Ensure public headers are in sysroot
cp -R "${SRCDIR}/dispatch/"*.h "${SYSROOT}/usr/include/dispatch/" 2>/dev/null || true
cp -R "${SRCDIR}/os/"*.h "${SYSROOT}/usr/include/os/" 2>/dev/null || true

# Generate module maps if missing
cp -f "${SRCDIR}/dispatch/darwin/module.modulemap" "${SRCDIR}/dispatch/module.modulemap" 2>/dev/null || true
cp -f "${SRCDIR}/private/darwin/module.modulemap" "${SRCDIR}/private/module.modulemap" 2>/dev/null || true

# Stage Panthera-patched sources over pinned source tree
cp -f "${SHIMDIR}/object_internal.h" "${SRCDIR}/src/object_internal.h"
cp -f "${SHIMDIR}/inline_internal.h" "${SRCDIR}/src/inline_internal.h"
cp -f "${SHIMDIR}/object.c" "${SRCDIR}/src/object.c"
cp -f "${SHIMDIR}/queue.c" "${SRCDIR}/src/queue.c"
cp -f "${SHIMDIR}/source.c" "${SRCDIR}/src/source.c"
cp -f "${SHIMDIR}/event_kevent.c" "${SRCDIR}/src/event/event_kevent.c"
python3 "${SHIMDIR}/patch_sources.py" "${SRCDIR}/src/queue_internal.h"

echo ">>> Building libdispatch"

# Step 1: Run MIG to generate protocol files
"${MIG}" -arch x86_64 \
    -I"${XNU_SRC}/osfmk" \
    -I"${XNU_SRC}/EXTERNAL_HEADERS" \
    -header "${OBJDIR}/protocol.h" \
    -sheader "${OBJDIR}/protocolServer.h" \
    -user "${OBJDIR}/protocolUser.c" \
    -server "${OBJDIR}/protocolServer.c" \
    "${SRCDIR}/src/protocol.defs"

COMMON_INCLUDES=(
    -I"${OBJDIR}"
    -I"${BUILDDIR}/compat_include"
    -I"${SHIMDIR}"
    -I"${SRCDIR}"
    -I"${SRCDIR}/src"
    -I"${SRCDIR}/private"
    -I"${LIBPTHREAD_SRC}"
    -I"${LIBPTHREAD_SRC}/private"
    -I"${LIBPLATFORM_SRC}/include"
    -I"${LIBPLATFORM_SRC}/private"
    -I"${LIBCLOSURE_SRC}"
    -I"${OBJC4_SRC}/runtime"
    -I"${XNU_SRC}"
    -I"${XNU_SRC}/libsyscall/mach"
    -I"${XNU_SRC}/osfmk"
    -I"${XNU_SRC}/bsd"
    -I"${XNU_SRC}/libsyscall"
    -I"${XNU_SRC}/libkern"
    -I"${XNU_SRC}/EXTERNAL_HEADERS"
    -I"${SYSROOT}/usr/include"
    -isysroot "${SDK_PATH}"
)

COMMON_DEFINES=(
    -D__DARWIN_LITTLE_ENDIAN=1234
    -D__DARWIN_BIG_ENDIAN=4321
    -D__DARWIN_BYTE_ORDER=1234
    -DHAVE_CONFIG_H=1
    -DPRIVATE=1
    -D__PTHREAD_EXPOSE_INTERNALS__=1
    -D_pthread_priority_has_qos=_pthread_priority_has_qos
    -Ddispatch_EXPORTS=1
    -DDISPATCH_USE_DTRACE=0
    -DDISPATCH_USE_KEVENT_WORKLOOP=1
    -DDISPATCH_USE_KEVENT_WORKQUEUE=1
    -DDISPATCH_USE_MEMORYPRESSURE_SOURCE=0
    -DPANTHERA_LIBDISPATCH_ENABLE_WORKGROUP_OBJECTS=0
    -DUSE_OBJC=0
    -DVOUCHER_USE_MACH_VOUCHER=0
    -DOS_EVENTLINK_USE_MACH_EVENTLINK=0
    -DDISPATCH_SEND_ACTIVITY_IN_MSGV=0
)

CFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -arch x86_64
    -std=gnu11
    -include "${BUILDDIR}/compat_include/Availability.h"
    -include "${SHIMDIR}/panthera_dispatch_compat.h"
    -fPIC
    -fvisibility=hidden
    -fblocks
    -fno-exceptions
    -O2
    -Wno-everything
    "${COMMON_DEFINES[@]}"
    "${COMMON_INCLUDES[@]}"
)

CXXFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -arch x86_64
    -nostdinc++
    -std=gnu++11
    -I"${SDK_PATH}/usr/include/c++/v1"
    -include "${BUILDDIR}/compat_include/Availability.h"
    -include "${SHIMDIR}/panthera_dispatch_compat.h"
    -fPIC
    -fvisibility=hidden
    -fblocks
    -fno-exceptions
    -O2
    -Wno-everything
    "${COMMON_DEFINES[@]}"
    "${COMMON_INCLUDES[@]}"
)

C_SOURCES=(
    "${SRCDIR}/src/allocator.c"
    "${SRCDIR}/src/apply.c"
    "${SRCDIR}/src/benchmark.c"
    "${SRCDIR}/src/data.c"
    "${SRCDIR}/src/init.c"
    "${SRCDIR}/src/introspection.c"
    "${SRCDIR}/src/io.c"
    "${SRCDIR}/src/mach.c"
    "${SHIMDIR}/object.c"
    "${SRCDIR}/src/once.c"
    "${SHIMDIR}/queue.c"
    "${SRCDIR}/src/semaphore.c"
    "${SHIMDIR}/source.c"
    "${SRCDIR}/src/time.c"
    "${SRCDIR}/src/transform.c"
    "${SHIMDIR}/panthera_workgroup_stub.c"
    "${SRCDIR}/src/voucher.c"
    "${SRCDIR}/src/shims.c"
    "${SRCDIR}/src/event/event.c"
    "${SRCDIR}/src/event/event_epoll.c"
    "${SHIMDIR}/event_kevent.c"
    "${SRCDIR}/src/event/event_windows.c"
    "${SRCDIR}/src/event/workqueue.c"
    "${SRCDIR}/src/shims/lock.c"
    "${SRCDIR}/src/shims/yield.c"
    "${OBJDIR}/protocolUser.c"
    "${OBJDIR}/protocolServer.c"
)

OBJECTS=()

for src in "${C_SOURCES[@]}"; do
    base="$(basename "${src}" .c)"
    obj="${OBJDIR}/${base}.o"
    "${CC}" "${CFLAGS[@]}" -c "${src}" -o "${obj}"
    OBJECTS+=("${obj}")
done

# Compile C++ sources
"${CXX}" "${CXXFLAGS[@]}" -c "${SRCDIR}/src/block.cpp" -o "${OBJDIR}/block.o"
OBJECTS+=("${OBJDIR}/block.o")

# Step 3: Link libdispatch.dylib
xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libdispatch.dylib \
    -compatibility_version 1.0.0 \
    -current_version 1.3.0 \
    -dead_strip \
    -alias_list "${SRCDIR}/xcodeconfig/libdispatch.aliases" \
    -o "${OUT}" \
    "${OBJECTS[@]}" \
    "${DYLD_STUB}" \
    "${OBJC4_LIB}" \
    "${OUTDIR}/libsystem_kernel.dylib" \
    "${OUTDIR}/libsystem_platform.dylib" \
    "${OUTDIR}/libsystem_pthread.dylib" \
    "${OUTDIR}/libsystem_c.dylib" \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0

chmod +x "${OUT}" 2>/dev/null || true

echo "Output: ${OUT}"
