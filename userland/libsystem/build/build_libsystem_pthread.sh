#!/bin/bash
set -e

# Build script for libsystem_pthread.dylib
# Part of the Panthera Darwin project

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
PANTHERA="${PANTHERA:-$PANTHERA_ROOT}"
SRCDIR="${SRCDIR:-$PANTHERA/src/libpthread-519}"
BUILDDIR="${BUILDDIR:-$PANTHERA/userland/libsystem/build}"
SYSROOT="${SYSROOT:-$BUILDDIR/sysroot}"
OBJDIR="${OBJDIR:-$BUILDDIR/obj/libsystem_pthread}"
OUTDIR="${OUTDIR:-$BUILDDIR/sysroot/usr/lib/system}"
SDK_PATH=$(xcrun -sdk macosx --show-sdk-path)
TARGET="x86_64-apple-darwin23.0"
BUILD_STUB_BINDER="${BUILD_STUB_BINDER:-$BUILDDIR/build_stub_binder.sh}"
DYLD_STUB="${BUILDDIR}/obj/dyld_stub_binder.o"
SHIMDIR="${SHIMDIR:-$BUILDDIR/shims/libsystem_pthread}"
AVAIL_STUB="$SHIMDIR/availability_stub.h"
PATCHDIR="${PATCHDIR:-$BUILDDIR/patches}"

# Prerequisite checks
required_inputs=(
    "$SRCDIR"
    "$SHIMDIR/panthera_pthread_aliases.s"
    "$AVAIL_STUB"
    "$BUILDDIR/compat_include/Availability.h"
    "$PANTHERA/src/xnu-10002.41.9"
    "$PANTHERA/src/libplatform-306.0.1"
    "$PANTHERA/src/Libsystem-1336"
    "$OUTDIR/libsystem_kernel.dylib"
    "$OUTDIR/libsystem_platform.dylib"
    "$OUTDIR/libsystem_malloc.dylib"
    "$OUTDIR/libsystem_c.dylib"
)

for path in "${required_inputs[@]}"; do
    if [ ! -e "$path" ]; then
        echo "missing required input: $path" >&2
        exit 1
    fi
done

if [ -f "$BUILD_STUB_BINDER" ]; then
    bash "$BUILD_STUB_BINDER"
fi

if [ ! -f "$DYLD_STUB" ]; then
    echo "missing required input: $DYLD_STUB" >&2
    exit 1
fi

# Apply Panthera patches idempotently to libpthread source
if [ -d "${PATCHDIR}" ]; then
    for patchfile in "${PATCHDIR}"/*.patch; do
        [ -f "${patchfile}" ] || continue
        patchname="$(basename "${patchfile}")"
        markerfile="${patchfile%.patch}.marker"

        total_markers=0
        matched_markers=0

        if [ -f "${markerfile}" ]; then
            while IFS=: read -r target_rel marker_str || [ -n "${target_rel}" ]; do
                [ -z "${target_rel}" ] && continue
                [[ "${target_rel}" =~ ^[[:space:]]*# ]] && continue
                total_markers=$((total_markers + 1))
                if [ -f "${SRCDIR}/${target_rel}" ] && grep -Fq "${marker_str}" "${SRCDIR}/${target_rel}"; then
                    matched_markers=$((matched_markers + 1))
                fi
            done < "${markerfile}"
        fi
        if [ "${total_markers}" -gt 0 ]; then
            if [ "${matched_markers}" -eq "${total_markers}" ]; then
                echo "Patch ${patchname} already applied, skipping."
                continue
            elif [ "${matched_markers}" -gt 0 ]; then
                echo "ERROR: Patch ${patchname} is partially applied to libpthread source at ${SRCDIR} (${matched_markers}/${total_markers} markers found)." >&2
                echo "Source tree is in an inconsistent partial patch state." >&2
                echo "To recover, reset libpthread source before rebuilding:" >&2
                echo "  rm -rf src/libpthread-519 && bash tools/fetch_world_sources.sh" >&2
                exit 1
            fi
            # matched_markers == 0: patch is absent (pristine), continue to dry-run and apply
        fi

        # Require clean forward dry-run before applying
        if ! patch_out="$(patch -p0 -d "${SRCDIR}" -f --dry-run < "${patchfile}" 2>&1)"; then
            echo "ERROR: Patch ${patchname} does not apply cleanly to libpthread source at ${SRCDIR}:" >&2
            echo "${patch_out}" >&2
            echo "To recover, reset libpthread source before rebuilding:" >&2
            echo "  rm -rf src/libpthread-519 && bash tools/fetch_world_sources.sh" >&2
            exit 1
        fi

        echo "Applying ${patchname} to libpthread source..."
        if ! patch -p0 -d "${SRCDIR}" -f -s < "${patchfile}"; then
            echo "ERROR: Failed to apply patch ${patchname} to libpthread source at ${SRCDIR}" >&2
            exit 1
        fi
    done
fi

rm -rf "$OBJDIR"
mkdir -p "$OBJDIR" "$OUTDIR"

# Common flags
CFLAGS=(
    -target "$TARGET"
    -mmacosx-version-min=14.0
    -std=gnu11
    -c -fPIC -O2
    -include "$BUILDDIR/compat_include/Availability.h"
    -include "$AVAIL_STUB"
    -I"$BUILDDIR/compat_include"
    -I"$SHIMDIR"
    -I"$SRCDIR/include"
    -I"$SRCDIR/private"
    -I"$SRCDIR/src"
    -I"$SRCDIR"
    -I"$SRCDIR/src/resolver"
    -I"$SRCDIR/kern"
    -I"$SYSROOT/usr/include"
    -I"$PANTHERA/src/xnu-10002.41.9/osfmk"
    -I"$PANTHERA/src/xnu-10002.41.9/EXTERNAL_HEADERS"
    -I"$PANTHERA/src/xnu-10002.41.9/libkern"
    -I"$PANTHERA/src/xnu-10002.41.9/bsd"
    -I"$PANTHERA/src/xnu-10002.41.9/libsyscall"
    -I"$PANTHERA/src/xnu-10002.41.9/libsyscall/mach"
    -I"$PANTHERA/src/xnu-10002.41.9/libsyscall/wrappers/spawn"
    -I"$PANTHERA/src/libplatform-306.0.1/include"
    -I"$PANTHERA/src/libplatform-306.0.1/private"
    -I"$PANTHERA/src/Libsystem-1336"
    -I"$SDK_PATH/usr/include"
    -DPRIVATE
    -D__LIBC__
    -D__DARWIN_UNIX03=1
    -D__DARWIN_64_BIT_INO_T=1
    -D__DARWIN_NON_CANCELABLE=1
    -D__DARWIN_VERS_1050=1
    -D__PTHREAD_BUILDING_PTHREAD__=1
    -D__PTHREAD_EXPOSE_INTERNALS__=1
    -DPANTHERA=1
    -D__POSIX_LIB__
    -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1
    '-D__SPI_AVAILABLE(...)='
    '-DAPI_AVAILABLE(...)='
    '-DAPI_DEPRECATED(...)='
    '-DAPI_UNAVAILABLE(...)='
    '-DAPI_DEPRECATED_WITH_REPLACEMENT(...)='
    -Wno-error
    -Wno-implicit-function-declaration
    -Wno-macro-redefined
    -Wno-int-conversion
    -Wno-incompatible-pointer-types
    -Wno-deprecated-declarations
    -Wno-format
    -Wno-unused-variable
    -Wno-shorten-64-to-32
    -Wno-sign-conversion
    -Wno-nullability-completeness
    -Wno-expansion-to-defined
    -Wno-gnu-conditional-omitted-operand
    -Wno-undef
    -Wno-missing-field-initializers
    -Wno-cast-qual
    -Wno-extra-semi
    -Wno-unused-parameter
    -Wno-typedef-redefinition
    -fno-stack-protector
    -fno-stack-check
    -fno-builtin
)

# Source files - all .c files in src/ (excluding kern/ which is kernel-side)
C_SOURCES=(
    "$SRCDIR/src/pthread.c"
    "$SRCDIR/src/pthread_mutex.c"
    "$SRCDIR/src/pthread_cond.c"
    "$SRCDIR/src/pthread_rwlock.c"
    "$SRCDIR/src/pthread_tsd.c"
    "$SRCDIR/src/pthread_atfork.c"
    "$SRCDIR/src/pthread_cancelable.c"
    "$SRCDIR/src/pthread_cwd.c"
    "$SRCDIR/src/pthread_dependency.c"
    "$SRCDIR/src/qos.c"
    "$SRCDIR/src/resolver/resolver.c"
    "$SRCDIR/src/variants/pthread_cancelable_cancel.c"
)

ASM_SOURCES=(
    "$SRCDIR/src/pthread_asm.s"
    "$SHIMDIR/panthera_pthread_aliases.s"
)

OBJECTS=()
FAILED=0

echo "=== Building libsystem_pthread.dylib ==="
echo "SDK: $SDK_PATH"
echo "Target: $TARGET"
echo ""

# Compile C sources
for src in "${C_SOURCES[@]}"; do
    basename=$(basename "$src" .c)
    # Handle subdirectory naming to avoid collisions
    if [[ "$src" == *"/resolver/"* ]]; then
        objname="resolver_${basename}.o"
    elif [[ "$src" == *"/variants/"* ]]; then
        objname="variants_${basename}.o"
    else
        objname="${basename}.o"
    fi

    echo "CC  $basename.c -> $objname"
    if clang "${CFLAGS[@]}" "$src" -o "$OBJDIR/$objname" 2>&1; then
        OBJECTS+=("$OBJDIR/$objname")
    else
        echo "FAILED: $src"
        FAILED=$((FAILED + 1))
    fi
    echo ""
done

# Compile assembly sources
for src in "${ASM_SOURCES[@]}"; do
    basename=$(basename "$src" .s)
    objname="${basename}.o"
    ppname="${basename}_pp.s"

    echo "ASM $basename.s -> $objname (preprocess + assemble)"
    if [[ "$src" == "$SRCDIR/src/pthread_asm.s" ]]; then
        # Preprocess then assemble, converting ; to newlines for compatibility.
        if clang -E -x assembler-with-cpp \
            -target "$TARGET" \
            -mmacosx-version-min=14.0 \
            -I"$SHIMDIR" \
            -I"$SRCDIR/include" \
            -I"$SRCDIR/private" \
            -I"$SRCDIR/src" \
            -I"$SRCDIR" \
            -I"$SRCDIR/src/resolver" \
            -I"$SRCDIR/kern" \
            -I"$SYSROOT/usr/include" \
            -I"$PANTHERA/src/xnu-10002.41.9/osfmk" \
            -I"$PANTHERA/src/xnu-10002.41.9/EXTERNAL_HEADERS" \
            -I"$PANTHERA/src/xnu-10002.41.9/libkern" \
            -I"$PANTHERA/src/xnu-10002.41.9/bsd" \
            -I"$PANTHERA/src/xnu-10002.41.9/libsyscall" \
            -I"$PANTHERA/src/xnu-10002.41.9/libsyscall/mach" \
            -I"$PANTHERA/src/libplatform-306.0.1/include" \
            -I"$PANTHERA/src/libplatform-306.0.1/private" \
            -I"$SDK_PATH/usr/include" \
            -DPRIVATE \
            -D__LIBC__ \
            -D__DARWIN_UNIX03=1 \
            -D__DARWIN_64_BIT_INO_T=1 \
            -D__DARWIN_NON_CANCELABLE=1 \
            -D__DARWIN_VERS_1050=1 \
            -D__PTHREAD_BUILDING_PTHREAD__=1 \
            -D__PTHREAD_EXPOSE_INTERNALS__=1 \
            -D__POSIX_LIB__ \
            -DOS_ATOMIC_CONFIG_MEMORY_ORDER_DEPENDENCY=1 \
            "$src" 2>&1 | sed 's/ ; /\n/g' > "$OBJDIR/$ppname"; then

            if clang -target "$TARGET" -mmacosx-version-min=14.0 -c "$OBJDIR/$ppname" -o "$OBJDIR/$objname" 2>&1; then
                OBJECTS+=("$OBJDIR/$objname")
            else
                echo "FAILED assembly: $src"
                FAILED=$((FAILED + 1))
            fi
        else
            echo "FAILED preprocessing: $src"
            FAILED=$((FAILED + 1))
        fi
    else
        if clang -target "$TARGET" -mmacosx-version-min=14.0 -c "$src" -o "$OBJDIR/$objname" 2>&1; then
            OBJECTS+=("$OBJDIR/$objname")
        else
            echo "FAILED assembly: $src"
            FAILED=$((FAILED + 1))
        fi
    fi
    echo ""
done

if [ ${#OBJECTS[@]} -eq 0 ]; then
    echo "ERROR: No objects compiled successfully."
    exit 1
fi

echo "=== Compiled ${#OBJECTS[@]} objects, $FAILED failures ==="
echo ""

if [ "$FAILED" -gt 0 ]; then
    echo "ERROR: refusing to link incomplete libsystem_pthread.dylib" >&2
    exit 1
fi

# Link
echo "LINK -> libsystem_pthread.dylib"
clang -target "$TARGET" -mmacosx-version-min=14.0 -dynamiclib \
    -install_name /usr/lib/system/libsystem_pthread.dylib \
    -Wl,-compatibility_version,1.0.0 \
    -Wl,-current_version,519.0.0 \
    -o "$OUTDIR/libsystem_pthread.dylib" \
    "${OBJECTS[@]}" \
    "$OUTDIR/libsystem_kernel.dylib" \
    "$OUTDIR/libsystem_platform.dylib" \
    "$OUTDIR/libsystem_malloc.dylib" \
    "$OUTDIR/libsystem_c.dylib" \
    "$DYLD_STUB" \
    -undefined dynamic_lookup \
    -Wl,-not_for_dyld_shared_cache \
    -nostdlib \
    -nodefaultlibs \
    2>&1

echo ""
echo "=== Result ==="
file "$OUTDIR/libsystem_pthread.dylib"
ls -la "$OUTDIR/libsystem_pthread.dylib"
echo ""
echo "Exported symbols:"
nm -gU "$OUTDIR/libsystem_pthread.dylib" | wc -l
echo "total exported symbols"
echo ""
echo "Sample exports:"
nm -gU "$OUTDIR/libsystem_pthread.dylib" | head -20
