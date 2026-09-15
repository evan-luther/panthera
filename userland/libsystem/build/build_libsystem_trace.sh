#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj/libsystem_trace"
SOURCESDIR="${BUILDDIR}/sources"
SYSROOT="${BUILDDIR}/sysroot"
OUTDIR="${SYSROOT}/usr/lib/system"
OUT="${OUTDIR}/libsystem_trace.dylib"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
SDK_PATH="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
TARGET="x86_64-apple-darwin23.0"

SRC="${SOURCESDIR}/libsystem_trace_stub.c"
OBJ="${OBJDIR}/libsystem_trace_stub.o"

required=(
    "${SRC}"
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

mkdir -p "${OBJDIR}" "${OUTDIR}"

COMMON_FLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -fPIC
    -O2
    -Wno-everything
    -I"${SYSROOT}/usr/include"
    -I"${BUILDDIR}/compat_include"
    -isysroot "${SDK_PATH}"
)

echo ">>> Building libsystem_trace"

"${CC}" "${COMMON_FLAGS[@]}" \
    -c "${SRC}" -o "${OBJ}"

xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libsystem_trace.dylib \
    -compatibility_version 1.0.0 \
    -current_version 1.0.0 \
    -o "${OUT}" \
    "${OBJ}" \
    "${DYLD_STUB}" \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0

echo "Output: ${OUT}"
