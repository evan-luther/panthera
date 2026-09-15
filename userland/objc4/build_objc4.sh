#!/bin/bash
# build_objc4.sh — Build libobjc.A.dylib from Apple objc4-906 for Panthera
# Step 4 of 5 in the CoreFoundation build chain.
set -euo pipefail

PANTHERA_ROOT="${PANTHERA_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
SRC="${PANTHERA_ROOT}/src/objc4-906/runtime"
BUILDDIR="${PANTHERA_ROOT}/userland/objc4/build"
SHIMDIR="${PANTHERA_ROOT}/userland/objc4/shims"
OUTDIR="${PANTHERA_ROOT}/userland/objc4/lib"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"

CC="$(xcrun -find clang)"
CXX="$(xcrun -find clang++)"
TARGET="x86_64-apple-darwin23.0"

# Skip rebuild if already built (unless forced)
if [ -f "${OUTDIR}/libobjc.A.dylib" ] && [ "${PANTHERA_FORCE_REBUILD:-0}" != "1" ]; then
    echo "libobjc.A.dylib already built. Set PANTHERA_FORCE_REBUILD=1 to rebuild."
    exit 0
fi

mkdir -p "${BUILDDIR}" "${OUTDIR}"

# Common flags
# Use PTHREADS threading to avoid needing os_unfair_lock private APIs and direct TSD
COMMON_CFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -isysroot "${SDKROOT}"
    -O2
    -fno-stack-protector
    -DNDEBUG=1

    # Shim headers take priority (searched before SDK)
    -I"${SHIMDIR}"

    # Source tree headers
    -I"${SRC}"
    -I"${SRC}/Messengers.subproj"

    # Use PTHREADS threading package instead of Darwin-specific
    -DOBJC_THREADING_PACKAGE=2

    # Disable restartable ranges (needs kernel support we don't have)
    -DHAVE_TASK_RESTARTABLE_RANGES=0

    # Fix undefined macros
    -DTARGET_OS_EXCLAVEKIT=0
    -D__APPLE_BLEACH_SDK__=1
    -D__BUILDING_OBJCDT__=1

    # Provide __progname declaration (BSD global, defined by crt)
    -include "${SHIMDIR}/panthera_objc_compat.h"

    # Suppress warnings that are noise for this codebase
    -Wno-deprecated-objc-isa-usage
    -Wno-cast-of-sel-type
    -Wno-deprecated-declarations
    -Wno-unused-function
    -Wno-unused-variable
    -Wno-nullability-completeness
    -Wno-implicit-function-declaration
    -Wno-format
    -Wno-macro-redefined
    -Wno-undef
)

CXX_FLAGS=(
    "${COMMON_CFLAGS[@]}"
    -std=gnu++20
    -I"${SDKROOT}/usr/include/c++/v1"
    -fno-exceptions
    -fno-rtti
)

OBJCXX_FLAGS=(
    "${CXX_FLAGS[@]}"
    -x objective-c++
)

echo "=== Building objc4-906 (Objective-C Runtime) ==="
echo "Source: ${SRC}"
echo "Build:  ${BUILDDIR}"

# .mm files (Objective-C++)
MM_FILES=(
    NSObject.mm
    hashtable2.mm
    maptable.mm
    objc-accessors.mm
    objc-auto.mm
    objc-block-trampolines.mm
    objc-cache.mm
    objc-class.mm
    objc-errors.mm
    objc-exception.mm
    objc-file.mm
    objc-initialize.mm
    objc-layout.mm
    objc-load.mm
    objc-loadmethod.mm
    objc-lockdebug.mm
    objc-opt.mm
    objc-os.mm
    objc-references.mm
    objc-runtime-new.mm
    objc-runtime.mm
    objc-sel.mm
    objc-sync.mm
    objc-typeencoding.mm
    objc-weak.mm
    objc-zalloc.mm
    Object.mm
    Protocol.mm
)

# .c files
C_FILES=(
    objc-test-env.c
)

# .m files
M_FILES=(
    objc-magicsel.m
)

# .s files (x86_64 only)
ASM_FILES=(
    Messengers.subproj/objc-msg-x86_64.s
    objc-blocktramps-x86_64.s
    objc-sel-table.s
)

TOTAL=$(( ${#MM_FILES[@]} + ${#C_FILES[@]} + ${#M_FILES[@]} + ${#ASM_FILES[@]} ))
COUNT=0
FAILED=0

compile_one() {
    local src="$1"
    local obj="$2"
    shift 2
    local flags=("$@")

    COUNT=$((COUNT + 1))
    local base
    base=$(basename "$src")
    printf "[%2d/%2d] %s\n" "$COUNT" "$TOTAL" "$base"

    if ! "$CXX" "${flags[@]}" -c "${SRC}/${src}" -o "${BUILDDIR}/${obj}" 2>&1; then
        echo "FAILED: ${base}"
        FAILED=$((FAILED + 1))
        return 1
    fi
    return 0
}

# Compile .mm files
for f in "${MM_FILES[@]}"; do
    obj="${f%.mm}.o"
    compile_one "$f" "$obj" "${OBJCXX_FLAGS[@]}" || true
done

# Compile .c files
for f in "${C_FILES[@]}"; do
    obj="${f%.c}.o"
    compile_one "$f" "$obj" "${COMMON_CFLAGS[@]}" || true
done

# Compile .m files
for f in "${M_FILES[@]}"; do
    obj="${f%.m}.o"
    compile_one "$f" "$obj" "${COMMON_CFLAGS[@]}" -x objective-c || true
done

# Compile .s files
for f in "${ASM_FILES[@]}"; do
    obj="$(basename "${f%.s}.o")"
    COUNT=$((COUNT + 1))
    base=$(basename "$f")
    printf "[%2d/%2d] %s\n" "$COUNT" "$TOTAL" "$base"
    if ! "$CC" -target "${TARGET}" -mmacosx-version-min=14.0 -isysroot "${SDKROOT}" \
         -I"${SRC}" -DOBJC_THREADING_PACKAGE=2 \
         -c "${SRC}/${f}" -o "${BUILDDIR}/${obj}" 2>&1; then
        echo "FAILED: ${base}"
        FAILED=$((FAILED + 1))
    fi
done

if [ "$FAILED" -gt 0 ]; then
    echo ""
    echo "=== ${FAILED} files failed to compile ==="
    exit 1
fi

echo ""
echo "=== Compiling libobjc stubs ==="
"$CC" -target "${TARGET}" -mmacosx-version-min=14.0 -isysroot "${SDKROOT}" -O2 \
    -fno-stack-protector \
    -c "${PANTHERA_ROOT}/userland/objc4/objc4_stubs.c" \
    -o "${BUILDDIR}/objc4_stubs.o"

echo ""
echo "=== Linking libobjc.A.dylib ==="

# Collect all .o files
OBJ_FILES=()
for f in "${BUILDDIR}"/*.o; do
    [ -f "$f" ] && OBJ_FILES+=("$f")
done

"$CXX" -target "${TARGET}" -isysroot "${SDKROOT}" \
    -dynamiclib \
    -install_name /usr/lib/libobjc.A.dylib \
    -compatibility_version 1.0 \
    -current_version 228.0 \
    -L"${SYSROOT}/usr/lib" \
    -L"${SYSROOT}/usr/lib/system" \
    -lSystem \
    -lc++ \
    -lc++abi \
    "${OBJ_FILES[@]}" \
    -o "${OUTDIR}/libobjc.A.dylib"

echo "=== libobjc.A.dylib built successfully ==="
echo "Output: ${OUTDIR}/libobjc.A.dylib"

# Create symlink
ln -sf libobjc.A.dylib "${OUTDIR}/libobjc.dylib"

# Run audit
if [ -f "${PANTHERA_ROOT}/tools/audit_package.sh" ]; then
    echo ""
    echo "=== Running symbol audit ==="
    bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUTDIR}/libobjc.A.dylib"
fi

echo ""
echo "=== Done ==="
