#!/bin/bash
# Build libc++abi from Apple's libcppabi-26 for Panthera x86_64
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${PANTHERA_ROOT}/src/libcppabi-26"
BUILDDIR="${PANTHERA_ROOT}/userland/libcxxabi"
OBJDIR="${BUILDDIR}/obj"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
CXX="$(xcrun -find clang++)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"

# Output
OUTPUT="${BUILDDIR}/libc++abi.dylib"

if [ -f "$OUTPUT" ] && [ "${PANTHERA_FORCE_REBUILD:-}" != "1" ]; then
    echo "libc++abi.dylib already exists. Set PANTHERA_FORCE_REBUILD=1 to rebuild."
    exit 0
fi

echo "=== Building libc++abi from libcppabi-26 ==="

# --- Shim: CrashReporterClient.h ---
SHIMDIR="${BUILDDIR}/shims"
mkdir -p "$SHIMDIR"
cat > "$SHIMDIR/CrashReporterClient.h" <<'SHIMEOF'
#ifndef _CRASHREPORTERCLIENT_H_
#define _CRASHREPORTERCLIENT_H_
#define CRSetCrashLogMessage(msg) ((void)0)
#endif
SHIMEOF

# --- Prepare obj dir ---
rm -rf "$OBJDIR"
mkdir -p "$OBJDIR"

# Common flags
COMMON_CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -O2 -fPIC"

# Include paths: shims first (contains patched unwind-cxx.h), then source headers
INC="-I${SHIMDIR} -I${SRC}/include -I${SRC}/src"

# C++ flags (no -fno-rtti: tinfo.cc/tinfo2.cc use typeid)
CXXFLAGS="${COMMON_CFLAGS} ${INC} -std=gnu++11 -DHAVE_GETIPINFO -Wno-deprecated"

# C flags
CFLAGS="${COMMON_CFLAGS} ${INC}"

# --- Source files ---
# Apple's cxa_apple.cxx provides handler variables and Apple extensions.
# We exclude GCC handler-variable files and files that conflict with cxa_apple.cxx.
#
# Excluded:
#   eh_arm.cc           - ARM only
#   eh_terminate.cc     - conflicts with cxa_apple.cxx (std::terminate, __terminate, __unexpected)
#   eh_term_handler.cc  - conflicts with cxa_apple.cxx (__cxa_terminate_handler variable)
#   eh_unex_handler.cc  - conflicts with cxa_apple.cxx (__cxa_unexpected_handler variable)
#   new_handler.cc      - conflicts with cxa_apple.cxx (__cxa_new_handler variable)
#   eh_aux_runtime.cc   - conflicts with cxa_apple.cxx (__cxa_bad_cast, __cxa_bad_typeid)
#   pure.cc             - conflicts with cxa_pure_virtual.cxx

CXX_SOURCES=(
    cxa_apple.cxx
    cxa_demangle.cpp
    cxa_guard.cxx
    cxa_pure_virtual.cxx
    del_op.cc
    del_opnt.cc
    del_opv.cc
    del_opvnt.cc
    eh_alloc.cc
    eh_call.cc
    eh_catch.cc
    eh_exception.cc
    eh_extensions.cc
    eh_globals.cc
    eh_personality.cc
    eh_throw.cc
    eh_type.cc
    new_op.cc
    new_opnt.cc
    new_opv.cc
    new_opvnt.cc
    tinfo.cc
    tinfo2.cc
    vec.cc
    vterminate.cc
)

C_SOURCES=(
    abort_message.c
)

# --- Compile C sources ---
echo "Compiling C sources..."
for src in "${C_SOURCES[@]}"; do
    obj="${OBJDIR}/$(basename "$src" .c).o"
    echo "  CC  $src"
    $CC $CFLAGS -c "${SRC}/src/${src}" -o "$obj"
done

# --- Compile C++ sources ---
echo "Compiling C++ sources..."
FAIL=0
for src in "${CXX_SOURCES[@]}"; do
    base="${src%.*}"
    obj="${OBJDIR}/${base}.o"
    EXTRA=""
    # cxa_demangle.cpp: the old demangler uses "class __int128" as a class name,
    # but modern clang treats __int128 as a builtin type keyword. Rename it.
    if [ "$src" = "cxa_demangle.cpp" ]; then
        EXTRA="-D__int128=__panthera_int128_cls -D__float128=__panthera_float128_cls"
    fi
    echo "  CXX $src"
    if ! $CXX $CXXFLAGS $EXTRA -c "${SRC}/src/${src}" -o "$obj" 2>&1; then
        echo "  FAILED: $src"
        FAIL=1
    fi
done

if [ "$FAIL" -ne 0 ]; then
    echo "ERROR: Some files failed to compile"
    exit 1
fi

# --- Link ---
echo "Linking libc++abi.dylib..."
$CXX -target ${TARGET} -isysroot ${SDKROOT} \
    -dynamiclib \
    -install_name /usr/lib/libc++abi.dylib \
    -compatibility_version 1.0.0 \
    -current_version 26.0.0 \
    -exported_symbols_list "${SRC}/exports/libcppabi-.exp" \
    -lSystem \
    ${OBJDIR}/*.o \
    -o "$OUTPUT"

echo "Built: $OUTPUT"

# --- Verify ---
echo ""
echo "=== Verification ==="
echo "File type:"
file "$OUTPUT"
echo ""
echo "Architecture:"
lipo -info "$OUTPUT"
echo ""
echo "Install name:"
otool -D "$OUTPUT"
echo ""
EXPORTED=$(nm -gU "$OUTPUT" | wc -l | tr -d ' ')
echo "Exported symbols: $EXPORTED"
echo ""
echo "Undefined symbols:"
nm -mu "$OUTPUT" | grep '(undefined)' | head -20
echo ""

# --- Stage to sysroot ---
echo "=== Staging to sysroot ==="
cp "$OUTPUT" "${SYSROOT}/usr/lib/libc++abi.dylib"
echo "Staged: ${SYSROOT}/usr/lib/libc++abi.dylib"

# --- Run audit ---
echo ""
echo "=== Running audit ==="
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "$OUTPUT"

echo ""
echo "=== libc++abi build complete ==="
