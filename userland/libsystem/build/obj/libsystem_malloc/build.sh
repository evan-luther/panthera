#!/bin/bash
# Build script for libsystem_malloc.dylib - Panthera Darwin project
set -e

SDK=$(/usr/bin/xcrun -sdk macosx --show-sdk-path)
SYSROOT=/Users/admin/panthera/userland/libsystem/build/sysroot
SRC=/Users/admin/panthera/src/libmalloc-474.0.13
XNU=/Users/admin/panthera/src/xnu-10002.41.9
OBJDIR=/Users/admin/panthera/userland/libsystem/build/obj/libsystem_malloc
OUTDIR=/Users/admin/panthera/userland/libsystem/build/sysroot/usr/lib/system
SHIMS=${OBJDIR}/panthera_malloc_shims.h
STUB_BINDER=/Users/admin/panthera/userland/libsystem/build/obj/dyld_stub_binder.o

CC="clang"
TARGET="x86_64-apple-darwin23.0"

CFLAGS="-target ${TARGET} -c -fPIC -O2 -fno-stack-protector"
CFLAGS="${CFLAGS} -include ${SHIMS}"
CFLAGS="${CFLAGS} -I${SRC}/src"
CFLAGS="${CFLAGS} -I${SRC}/include"
CFLAGS="${CFLAGS} -I${SRC}/private"
CFLAGS="${CFLAGS} -I${SRC}/resolver"
CFLAGS="${CFLAGS} -I${SYSROOT}/usr/include"
CFLAGS="${CFLAGS} -I${XNU}/osfmk"
CFLAGS="${CFLAGS} -I${XNU}/EXTERNAL_HEADERS"
CFLAGS="${CFLAGS} -I${SDK}/usr/include"
CFLAGS="${CFLAGS} -DPRIVATE -D__DARWIN_UNIX03=1"
if [[ "${PANTHERA_MALLOC_TRACE:-0}" == "1" ]]; then
    CFLAGS="${CFLAGS} -DPANTHERA_MALLOC_TRACE=1"
fi
CFLAGS="${CFLAGS} -Wno-everything"

echo "=== Building libsystem_malloc.dylib ==="
echo "SDK: ${SDK}"
echo "Sysroot: ${SYSROOT}"
echo "Source: ${SRC}"
echo ""

# List of source files to compile (skip xzone - empty files, skip knrheap - separate allocator)
SOURCES=(
    "${SRC}/src/malloc.c"
    "${SRC}/src/malloc_common.c"
    "${SRC}/src/malloc_printf.c"
    "${SRC}/src/malloc_type.c"
    "${SRC}/src/magazine_malloc.c"
    "${SRC}/src/magazine_rack.c"
    "${SRC}/src/magazine_large.c"
    "${SRC}/src/magazine_small.c"
    "${SRC}/src/magazine_tiny.c"
    "${SRC}/src/magazine_medium.c"
    "${SRC}/src/nano_malloc_common.c"
    "${SRC}/src/nanov2_malloc.c"
    "${SRC}/src/purgeable_malloc.c"
    "${SRC}/src/frozen_malloc.c"
    "${SRC}/src/legacy_malloc.c"
    "${SRC}/src/vm.c"
    "${SRC}/src/bitarray.c"
    "${SRC}/src/pgm_malloc.c"
    "${SRC}/src/sanitizer_malloc.c"
    "${SRC}/src/stack_trace.c"
    "${SRC}/src/early_malloc.c"
    "${SRC}/src/has_section.c"
    "${SRC}/src/msl_lite_support.c"
)

OBJECTS=()
FAILED=()
SUCCEEDED=()

for src in "${SOURCES[@]}"; do
    base=$(basename "$src" .c)
    obj="${OBJDIR}/${base}.o"
    echo -n "  Compiling ${base}.c ... "
    if ${CC} ${CFLAGS} "$src" -o "$obj" 2>/tmp/malloc_build_${base}.err; then
        echo "OK"
        OBJECTS+=("$obj")
        SUCCEEDED+=("$base")
    else
        echo "FAILED"
        FAILED+=("$base")
        cat /tmp/malloc_build_${base}.err | head -20
        echo "  ---"
    fi
done

echo ""
echo "=== Compilation Summary ==="
echo "Succeeded: ${#SUCCEEDED[@]} files"
echo "Failed: ${#FAILED[@]} files"
if [ ${#FAILED[@]} -gt 0 ]; then
    echo "Failed files: ${FAILED[*]}"
fi
echo ""

if [ ${#OBJECTS[@]} -eq 0 ]; then
    echo "ERROR: No object files produced, cannot link"
    exit 1
fi

# Link into dylib
echo "=== Linking libsystem_malloc.dylib ==="
echo "Objects: ${#OBJECTS[@]}"

${CC} -target ${TARGET} \
    -dynamiclib \
    -install_name /usr/lib/system/libsystem_malloc.dylib \
    -nostdlib \
    -undefined dynamic_lookup \
    -Wl,-not_for_dyld_shared_cache \
    -L${OUTDIR} \
    ${STUB_BINDER} \
    "${OBJECTS[@]}" \
    -o ${OUTDIR}/libsystem_malloc.dylib 2>&1

echo ""
echo "=== Results ==="
file ${OUTDIR}/libsystem_malloc.dylib
ls -la ${OUTDIR}/libsystem_malloc.dylib
echo ""
echo "Exported symbols:"
nm -gU ${OUTDIR}/libsystem_malloc.dylib | wc -l
echo ""
echo "First 30 exported symbols:"
nm -gU ${OUTDIR}/libsystem_malloc.dylib | head -30
echo ""
echo "Size:"
size ${OUTDIR}/libsystem_malloc.dylib
