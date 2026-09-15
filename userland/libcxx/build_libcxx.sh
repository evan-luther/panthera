#!/bin/bash
# Build libc++ from LLVM 19 source for Panthera x86_64
# Step 2 of the CoreFoundation dependency chain
# Dependencies: libc++abi (must be built first)
set -euo pipefail

PANTHERA_ROOT="${PANTHERA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
SRC="${PANTHERA_ROOT}/src/llvm-libcxx-19/libcxx"
SRCDIR="${SRC}/src"
INCDIR="${SRC}/include"
BUILDDIR="${PANTHERA_ROOT}/userland/libcxx"
OBJDIR="${BUILDDIR}/obj"
SHIMDIR="${BUILDDIR}/shims"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"

CC="$(xcrun -find clang)"
CXX="$(xcrun -find clang++)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
CLANG_RT="$("$CC" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
    CLANG_RT="$("$CC" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
    CLANG_RESOURCE_DIR="$("$CC" -print-resource-dir 2>/dev/null || true)"
    if [ -n "${CLANG_RESOURCE_DIR}" ] && [ -f "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a" ]; then
        CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
    fi
fi

if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
    echo "ERROR: Unable to locate Darwin compiler runtime archive (libclang_rt.osx.a) using ${CC}" >&2
    exit 1
fi

OUTPUT="${BUILDDIR}/libc++.1.dylib"

if [ -f "$OUTPUT" ] && [ "${PANTHERA_FORCE_REBUILD:-}" != "1" ]; then
    echo "libc++.1.dylib already exists. Set PANTHERA_FORCE_REBUILD=1 to rebuild."
    exit 0
fi

# Verify libc++abi is available
if [ ! -f "${SYSROOT}/usr/lib/libc++abi.dylib" ]; then
    echo "ERROR: libc++abi.dylib not found in sysroot. Build it first (Step 1)."
    exit 1
fi

echo "=== Building libc++ from LLVM 19 source ==="

# --- Prepare dirs ---
rm -rf "$OBJDIR"
mkdir -p "$OBJDIR" "$OBJDIR/filesystem" "$OBJDIR/ryu" "$SHIMDIR"

# --- Generate __config_site ---
cat > "${SHIMDIR}/__config_site" <<'EOF'
#ifndef _LIBCPP___CONFIG_SITE
#define _LIBCPP___CONFIG_SITE

#define _LIBCPP_ABI_VERSION 1
#define _LIBCPP_ABI_NAMESPACE __1
#define _LIBCPP_HAS_THREAD_API_PTHREAD
#define _LIBCPP_PSTL_BACKEND_SERIAL
// Hardening mode: none = (1 << 1)
#define _LIBCPP_HARDENING_MODE_DEFAULT (1 << 1)
// Disable vendor availability annotations for Panthera
#define _LIBCPP_HAS_NO_VENDOR_AVAILABILITY_ANNOTATIONS

#endif // _LIBCPP___CONFIG_SITE
EOF

# --- Generate __assertion_handler ---
cat > "${SHIMDIR}/__assertion_handler" <<'EOF'
#ifndef _LIBCPP___ASSERTION_HANDLER
#define _LIBCPP___ASSERTION_HANDLER

#include <__config>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if __has_builtin(__builtin_verbose_trap)
#  define _LIBCPP_ASSERTION_HANDLER(message) __builtin_verbose_trap("libc++", message)
#else
#  define _LIBCPP_ASSERTION_HANDLER(message) ((void)message, __builtin_trap())
#endif

#endif // _LIBCPP___ASSERTION_HANDLER
EOF

# --- Source files ---
# Core (always built)
CORE_SOURCES=(
    algorithm.cpp
    any.cpp
    bind.cpp
    call_once.cpp
    charconv.cpp
    chrono.cpp
    error_category.cpp
    exception.cpp
    filesystem/filesystem_clock.cpp
    filesystem/filesystem_error.cpp
    filesystem/path.cpp
    functional.cpp
    hash.cpp
    legacy_pointer_safety.cpp
    memory.cpp
    memory_resource.cpp
    new_handler.cpp
    new_helpers.cpp
    optional.cpp
    print.cpp
    random_shuffle.cpp
    ryu/d2fixed.cpp
    ryu/d2s.cpp
    ryu/f2s.cpp
    stdexcept.cpp
    string.cpp
    system_error.cpp
    typeinfo.cpp
    valarray.cpp
    variant.cpp
    vector.cpp
    verbose_abort.cpp
)

# Thread sources
THREAD_SOURCES=(
    atomic.cpp
    barrier.cpp
    condition_variable_destructor.cpp
    condition_variable.cpp
    future.cpp
    mutex_destructor.cpp
    mutex.cpp
    shared_mutex.cpp
    thread.cpp
)

# Random
RANDOM_SOURCES=(
    random.cpp
)

# Localization
LOCALE_SOURCES=(
    fstream.cpp
    ios.cpp
    ios.instantiations.cpp
    iostream.cpp
    locale.cpp
    ostream.cpp
    regex.cpp
    strstream.cpp
)

# Filesystem operations + builtins
FS_SOURCES=(
    filesystem/directory_entry.cpp
    filesystem/directory_iterator.cpp
    filesystem/operations.cpp
    filesystem/int128_builtins.cpp
)

# new/delete
NEW_SOURCES=(
    new.cpp
)

ALL_SOURCES=(
    "${CORE_SOURCES[@]}"
    "${THREAD_SOURCES[@]}"
    "${RANDOM_SOURCES[@]}"
    "${LOCALE_SOURCES[@]}"
    "${FS_SOURCES[@]}"
    "${NEW_SOURCES[@]}"
)

# --- Common flags ---
# NOT defining LIBCXX_BUILDING_LIBCXXABI — our libc++abi (libcppabi-26) is too old
# for LLVM 19's expectations (missing get_new_handler, __cxa_init_primary_exception).
# libc++ will provide its own new_handler and use fallback exception_ptr support.
COMMON_CXXFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -O2 \
    -std=c++20 -nostdinc++ \
    -I${SHIMDIR} \
    -I${INCDIR} \
    -I${SRCDIR} \
    -D_LIBCPP_BUILDING_LIBRARY \
    -fvisibility-inlines-hidden \
    -fvisibility=hidden"

# --- Compile ---
echo "Compiling ${#ALL_SOURCES[@]} source files..."
FAIL=0
COMPILED=0
for src in "${ALL_SOURCES[@]}"; do
    base="${src%.cpp}"
    obj="${OBJDIR}/${base}.o"
    mkdir -p "$(dirname "$obj")"

    echo "  CXX $src"
    if ! $CXX $COMMON_CXXFLAGS -c "${SRCDIR}/${src}" -o "$obj" 2>&1; then
        echo "  FAILED: $src"
        FAIL=1
    else
        COMPILED=$((COMPILED + 1))
    fi
done

echo ""
echo "Compiled: $COMPILED / ${#ALL_SOURCES[@]}"

if [ "$FAIL" -ne 0 ]; then
    echo "ERROR: Some files failed to compile"
    exit 1
fi

# --- Collect all .o files ---
OBJ_FILES=$(find "$OBJDIR" -name "*.o" -type f)

# --- Link ---
echo ""
echo "Linking libc++.1.dylib..."
$CXX -target ${TARGET} -isysroot ${SDKROOT} \
    -Wl,-syslibroot,${SYSROOT} \
    -dynamiclib \
    -nodefaultlibs \
    -install_name /usr/lib/libc++.1.dylib \
    -compatibility_version 1 \
    -current_version 1.0.0 \
    -Wl,-reexport_library,${SYSROOT}/usr/lib/libc++abi.dylib \
    -L${SYSROOT}/usr/lib \
    -L${SYSROOT}/usr/lib/system \
    -lpanthera_extra \
    ${SYSROOT}/usr/lib/libSystem.B.dylib \
    ${CLANG_RT} \
    $OBJ_FILES \
    -o "$OUTPUT"

echo "Built: $OUTPUT"

# --- Create symlink ---
ln -sf libc++.1.dylib "${BUILDDIR}/libc++.dylib"

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
echo "Key symbol check (std::bad_alloc from re-exported libc++abi):"
BAD_ALLOC=$(nm -gU "$OUTPUT" | grep -c 'bad_alloc' || true)
echo "  $BAD_ALLOC bad_alloc symbols"
echo ""

# --- Stage to sysroot ---
echo "=== Staging to sysroot ==="
cp "$OUTPUT" "${SYSROOT}/usr/lib/libc++.1.dylib"
ln -sf libc++.1.dylib "${SYSROOT}/usr/lib/libc++.dylib"
echo "Staged: ${SYSROOT}/usr/lib/libc++.1.dylib"
echo "Symlink: ${SYSROOT}/usr/lib/libc++.dylib -> libc++.1.dylib"

# --- Run audit ---
echo ""
echo "=== Running audit ==="
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "$OUTPUT"

echo ""
echo "=== libc++ build complete ==="
