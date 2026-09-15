#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj/libsystem_info"
SOURCESDIR="${BUILDDIR}/sources"
SYSROOT="${BUILDDIR}/sysroot"
OUTDIR="${SYSROOT}/usr/lib/system"
OUT="${OUTDIR}/libsystem_info.dylib"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
LIBINFO_SRC="${PANTHERA_ROOT}/src/Libinfo-583.0.1"
LIBC_SRC="${PANTHERA_ROOT}/src/Libc-1583.40.7"
PTHREAD_SRC="${PANTHERA_ROOT}/src/libpthread-519"
SDK_PATH="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
TARGET="x86_64-apple-darwin23.0"

required=(
    "${LIBINFO_SRC}/lookup.subproj/cache_module.c"
    "${LIBINFO_SRC}/lookup.subproj/file_module.c"
    "${LIBINFO_SRC}/lookup.subproj/si_module.c"
    "${LIBINFO_SRC}/lookup.subproj/si_data.c"
    "${LIBINFO_SRC}/lookup.subproj/libinfo.c"
    "${LIBINFO_SRC}/lookup.subproj/si_getaddrinfo.c"
    "${LIBINFO_SRC}/lookup.subproj/thread_data.c"
    "${LIBINFO_SRC}/lookup.subproj/kvbuf.c"
    "${LIBINFO_SRC}/lookup.subproj/search_module.c"
    "${LIBINFO_SRC}/lookup.subproj/ils.c"
    "${LIBINFO_SRC}/lookup.subproj/si_compare.c"
    "${LIBINFO_SRC}/gen.subproj/getifaddrs.c"
    "${LIBC_SRC}/locale/xlocale_private.h"
    "${PTHREAD_SRC}/private/pthread/tsd_private.h"
    "${SOURCESDIR}/libinfo_module_stubs.c"
    "${BUILDDIR}/build_stub_binder.sh"
)

for path in "${required[@]}"; do
    if [[ ! -f "${path}" ]]; then
        echo "missing required input: ${path}" >&2
        exit 1
    fi
done

"${BUILDDIR}/build_stub_binder.sh"

if [[ ! -f "${DYLD_STUB}" ]]; then
    echo "missing required input: ${DYLD_STUB}" >&2
    exit 1
fi

rm -rf "${OBJDIR}"
mkdir -p "${OBJDIR}" "${OUTDIR}"

CFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -fPIC
    -fblocks
    -O2
    -Wno-everything
    -D__DARWIN_UNIX03=1
    -DPRIVATE=1
    -D__APPLE__=1
    -include "${BUILDDIR}/compat_include/Availability.h"
    -I"${LIBINFO_SRC}/lookup.subproj"
    -I"${LIBINFO_SRC}/gen.subproj"
    -I"${LIBINFO_SRC}/Libinfo"
    -I"${LIBC_SRC}/locale"
    -I"${LIBC_SRC}/locale/FreeBSD"
    -I"${LIBC_SRC}/stdtime/FreeBSD"
    -I"${PTHREAD_SRC}/private"
    -I"${PANTHERA_ROOT}/src/xnu-10002.41.9/libsyscall"
    -I"${SYSROOT}/usr/include"
    -I"${BUILDDIR}/compat_include"
    -DXPC_EXPORT=
    -DXPC_WARN_RESULT=
    -DXPC_NONNULL1=
    -DXPC_NONNULL2=
    -DXPC_NONNULL3=
    -DXPC_NONNULL4=
    -DXPC_NONNULL5=
    -isysroot "${SDK_PATH}"
)

echo ">>> Building libsystem_info"

compile() {
    local src="$1"
    local out="$2"
    "${CC}" "${CFLAGS[@]}" -c "${src}" -o "${out}"
}

compile "${LIBINFO_SRC}/lookup.subproj/cache_module.c" "${OBJDIR}/cache_module.o"
compile "${LIBINFO_SRC}/lookup.subproj/file_module.c" "${OBJDIR}/file_module.o"
compile "${LIBINFO_SRC}/lookup.subproj/si_module.c" "${OBJDIR}/si_module.o"
compile "${LIBINFO_SRC}/lookup.subproj/si_data.c" "${OBJDIR}/si_data.o"
compile "${LIBINFO_SRC}/lookup.subproj/libinfo.c" "${OBJDIR}/libinfo.o"
compile "${LIBINFO_SRC}/lookup.subproj/si_getaddrinfo.c" "${OBJDIR}/si_getaddrinfo.o"
compile "${LIBINFO_SRC}/lookup.subproj/thread_data.c" "${OBJDIR}/thread_data.o"
compile "${LIBINFO_SRC}/lookup.subproj/kvbuf.c" "${OBJDIR}/kvbuf.o"
compile "${LIBINFO_SRC}/lookup.subproj/search_module.c" "${OBJDIR}/search_module.o"
compile "${LIBINFO_SRC}/lookup.subproj/ils.c" "${OBJDIR}/ils.o"
compile "${LIBINFO_SRC}/lookup.subproj/si_compare.c" "${OBJDIR}/si_compare.o"
compile "${LIBINFO_SRC}/gen.subproj/getifaddrs.c" "${OBJDIR}/getifaddrs.o"
compile "${SOURCESDIR}/libinfo_module_stubs.c" "${OBJDIR}/libinfo_module_stubs.o"

xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libsystem_info.dylib \
    -compatibility_version 1.0.0 \
    -current_version 1.0.0 \
    -unexported_symbol _bootstrap_port \
    -o "${OUT}" \
    "${OBJDIR}/cache_module.o" \
    "${OBJDIR}/file_module.o" \
    "${OBJDIR}/si_module.o" \
    "${OBJDIR}/si_data.o" \
    "${OBJDIR}/libinfo.o" \
    "${OBJDIR}/si_getaddrinfo.o" \
    "${OBJDIR}/thread_data.o" \
    "${OBJDIR}/kvbuf.o" \
    "${OBJDIR}/search_module.o" \
    "${OBJDIR}/ils.o" \
    "${OBJDIR}/si_compare.o" \
    "${OBJDIR}/getifaddrs.o" \
    "${OBJDIR}/libinfo_module_stubs.o" \
    "${DYLD_STUB}" \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0

echo "Output: ${OUT}"
