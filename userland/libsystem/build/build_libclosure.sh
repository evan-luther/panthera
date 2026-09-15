#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
SRCDIR="${PANTHERA_ROOT}/src/libclosure-90"
OBJDIR="${BUILDDIR}/obj/libclosure"
SYSROOT="${BUILDDIR}/sysroot"
OUTDIR="${SYSROOT}/usr/lib/system"
OUT="${OUTDIR}/libclosure.dylib"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
SDK_PATH="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
CXX="$(xcrun -sdk macosx -find clang++)"
TARGET="x86_64-apple-darwin23.0"

required=(
    "${SRCDIR}/runtime.cpp"
    "${SRCDIR}/data.c"
    "${SRCDIR}/generic_helpers.c"
    "${SRCDIR}/Block.h"
    "${SRCDIR}/Block_private.h"
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

# Panthera has no libcompiler_rt, so disable HAVE_UNWIND to avoid needing
# ___gcc_personality_v0.  The HAVE_UNWIND=0 path uses custom copy/dispose
# helpers only (same path Apple uses for DriverKit).
COMMON_FLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -fPIC
    -DHAVE_OBJC=0
    -DHAVE_UNWIND=0
    -I"${SRCDIR}"
    -isysroot "${SDK_PATH}"
)

echo ">>> Building libclosure"

"${CXX}" "${COMMON_FLAGS[@]}" -std=c++11 -fno-exceptions \
    -c "${SRCDIR}/runtime.cpp" -o "${OBJDIR}/runtime.o"
"${CC}" "${COMMON_FLAGS[@]}" \
    -c "${SRCDIR}/data.c" -o "${OBJDIR}/data.o"
"${CC}" "${COMMON_FLAGS[@]}" \
    -c "${SRCDIR}/generic_helpers.c" -o "${OBJDIR}/generic_helpers.o"

xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libclosure.dylib \
    -compatibility_version 1.0.0 \
    -current_version 90.0.0 \
    -o "${OUT}" \
    "${OBJDIR}/runtime.o" \
    "${OBJDIR}/data.o" \
    "${OBJDIR}/generic_helpers.o" \
    "${DYLD_STUB}" \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0

echo "Output: ${OUT}"
