#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj"
SOURCESDIR="${BUILDDIR}/sources"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
LIBDIR="${PANTHERA_ROOT}/userland/libsystem/build/sysroot/usr/lib"
SYSDIR="${LIBDIR}/system"
LIBSYSTEM_INIT_SRC="${SOURCESDIR}/libSystem_init.c"
LIBSYSTEM_INIT_OBJ="${OBJDIR}/libSystem_init.o"
COMPILER_RT_STUBS_SRC="${SOURCESDIR}/compiler_rt_stubs.c"
COMPILER_RT_STUBS_OBJ="${OBJDIR}/compiler_rt_stubs.o"
DYLD_STUB_BINDER_SRC="${SOURCESDIR}/dyld_stub_binder.s"
DYLD_STUB_BINDER_OBJ="${OBJDIR}/dyld_stub_binder.o"
COMPAT_SRC="${SOURCESDIR}/libSystem_compat_aliases.s"
COMPAT_OBJ="${OBJDIR}/libSystem_compat_aliases.o"
CLANG_RT_ARCHIVE="$(xcrun clang -target x86_64-apple-darwin23.0 -print-file-name=libclang_rt.osx.a)"
CLANG_RT_MEMBER="udivmodti4.c.o"
CLANG_RT_TMPDIR="${OBJDIR}/compiler_rt_extract"
CLANG_RT_THIN_ARCHIVE="${CLANG_RT_TMPDIR}/libclang_rt.osx.x86_64.a"
CLANG_RT_OBJ="${OBJDIR}/compiler_rt_udivmodti4.o"
CLANG_RT_WRAPPER_SRC="${SOURCESDIR}/compiler_rt_udivti3_wrapper.c"
CLANG_RT_WRAPPER_OBJ="${OBJDIR}/compiler_rt_udivti3_wrapper.o"

source_inputs=(
    "${LIBSYSTEM_INIT_SRC}"
    "${COMPILER_RT_STUBS_SRC}"
    "${DYLD_STUB_BINDER_SRC}"
    "${COMPAT_SRC}"
    "${CLANG_RT_WRAPPER_SRC}"
)

for path in "${source_inputs[@]}"; do
    if [[ ! -e "${path}" ]]; then
        echo "missing required input: ${path}" >&2
        exit 1
    fi
done

mkdir -p "${OBJDIR}" "${LIBDIR}" "${CLANG_RT_TMPDIR}"
rm -f "${LIBSYSTEM_INIT_OBJ}" "${COMPILER_RT_STUBS_OBJ}" "${DYLD_STUB_BINDER_OBJ}" "${COMPAT_OBJ}" "${CLANG_RT_WRAPPER_OBJ}"
xcrun -sdk macosx clang -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 \
    -c "${LIBSYSTEM_INIT_SRC}" -o "${LIBSYSTEM_INIT_OBJ}"
xcrun -sdk macosx clang -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 \
    -c "${COMPILER_RT_STUBS_SRC}" -o "${COMPILER_RT_STUBS_OBJ}"
xcrun -sdk macosx clang -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 \
    -c "${DYLD_STUB_BINDER_SRC}" -o "${DYLD_STUB_BINDER_OBJ}"
xcrun -sdk macosx clang -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 \
    -c "${COMPAT_SRC}" -o "${COMPAT_OBJ}"
xcrun -sdk macosx clang -target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 \
    -c "${CLANG_RT_WRAPPER_SRC}" -o "${CLANG_RT_WRAPPER_OBJ}"

required=(
    "${LIBSYSTEM_INIT_OBJ}"
    "${COMPAT_OBJ}"
    "${CLANG_RT_WRAPPER_OBJ}"
    "${COMPILER_RT_STUBS_OBJ}"
    "${DYLD_STUB_BINDER_OBJ}"
    "${CLANG_RT_ARCHIVE}"
    "${SYSDIR}/libsystem_kernel.dylib"
    "${SYSDIR}/libsystem_platform.dylib"
    "${SYSDIR}/libsystem_malloc.dylib"
    "${SYSDIR}/libsystem_info.dylib"
    "${SYSDIR}/libsystem_c.dylib"
    "${SYSDIR}/libsystem_pthread.dylib"
    "${SYSDIR}/libdispatch.dylib"
    "${SYSDIR}/libxpc.dylib"
    "${SYSDIR}/libpanthera_extra.dylib"
    "${SYSDIR}/libclosure.dylib"
    "${SYSDIR}/libsystem_notify.dylib"
    "${SYSDIR}/libsystem_trace.dylib"
    "${SYSDIR}/libsystem_sandbox.dylib"
)

for path in "${required[@]}"; do
    if [[ ! -e "${path}" ]]; then
        echo "missing required input: ${path}" >&2
        exit 1
    fi
done

rm -f "${CLANG_RT_TMPDIR}/${CLANG_RT_MEMBER}" "${CLANG_RT_THIN_ARCHIVE}" "${CLANG_RT_OBJ}"
xcrun lipo -thin x86_64 "${CLANG_RT_ARCHIVE}" -output "${CLANG_RT_THIN_ARCHIVE}"
(
    cd "${CLANG_RT_TMPDIR}"
    ar -x "${CLANG_RT_THIN_ARCHIVE}" "${CLANG_RT_MEMBER}"
)
mv "${CLANG_RT_TMPDIR}/${CLANG_RT_MEMBER}" "${CLANG_RT_OBJ}"
xcrun ld -arch x86_64 -dylib \
    -syslibroot "${SYSROOT}" \
    -install_name /usr/lib/libSystem.B.dylib \
    -o "${LIBDIR}/libSystem.B.dylib" \
    "${LIBSYSTEM_INIT_OBJ}" \
    "${COMPAT_OBJ}" \
    "${CLANG_RT_WRAPPER_OBJ}" \
    "${CLANG_RT_OBJ}" \
    "${COMPILER_RT_STUBS_OBJ}" \
    "${DYLD_STUB_BINDER_OBJ}" \
    -reexport_library "${SYSDIR}/libsystem_kernel.dylib" \
    -reexport_library "${SYSDIR}/libsystem_platform.dylib" \
    -reexport_library "${SYSDIR}/libsystem_malloc.dylib" \
    -reexport_library "${SYSDIR}/libpanthera_extra.dylib" \
    -reexport_library "${SYSDIR}/libsystem_c.dylib" \
    -reexport_library "${SYSDIR}/libsystem_info.dylib" \
    -reexport_library "${SYSDIR}/libsystem_pthread.dylib" \
    -reexport_library "${SYSDIR}/libclosure.dylib" \
    -reexport_library "${SYSDIR}/libsystem_trace.dylib" \
    -reexport_library "${SYSDIR}/libsystem_sandbox.dylib" \
    -reexport_library "${SYSDIR}/libdispatch.dylib" \
    -reexport_library "${SYSDIR}/libxpc.dylib" \
    -reexport_library "${SYSDIR}/libsystem_notify.dylib" \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0
if [[ ! -f "${LIBDIR}/libSystem.B.dylib" ]]; then
    echo "missing required libSystem runtime library: ${LIBDIR}/libSystem.B.dylib" >&2
    exit 1
fi
ln -sf libSystem.B.dylib "${LIBDIR}/libSystem.dylib"
if [[ ! -L "${LIBDIR}/libSystem.dylib" && ! -f "${LIBDIR}/libSystem.dylib" ]]; then
    echo "failed to create canonical link: ${LIBDIR}/libSystem.dylib" >&2
    exit 1
fi

otool -l "${LIBDIR}/libSystem.B.dylib" | grep -n -E "LC_REEXPORT_DYLIB|libpanthera_extra|libclosure" -A3 -B1
