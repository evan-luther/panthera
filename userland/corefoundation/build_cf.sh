#!/bin/bash
# build_cf.sh — Build libCoreFoundation.dylib from Apple CF-1153.18 for Panthera
# Step 5 of 5 in the CoreFoundation build chain.
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CF_SRC="${PANTHERA_ROOT}/src/CF-1153.18"
SHIMDIR="${PANTHERA_ROOT}/userland/corefoundation/shims"
BUILDDIR="${PANTHERA_ROOT}/userland/corefoundation/build"
OUTDIR="${PANTHERA_ROOT}/userland/corefoundation"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
PATCHDIR="${PANTHERA_ROOT}/userland/corefoundation/patches"

CC="$(xcrun -find clang)"
TARGET="x86_64-apple-darwin23.0"
NCPU="$(sysctl -n hw.ncpu)"
# Locate Darwin compiler runtime (libclang_rt.osx.a)
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

# Skip rebuild if already built (unless forced)
if [ -f "${OUTDIR}/libCoreFoundation.dylib" ] && [ "${PANTHERA_FORCE_REBUILD:-0}" != "1" ]; then
    echo "libCoreFoundation.dylib already built. Set PANTHERA_FORCE_REBUILD=1 to rebuild."
    exit 0
fi
if [ ! -d "${CF_SRC}" ]; then
    echo "ERROR: CoreFoundation source not found at ${CF_SRC}" >&2
    exit 1
fi

# Apply Panthera patches idempotently to CF source
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
                if [ -f "${CF_SRC}/${target_rel}" ] && grep -Fq "${marker_str}" "${CF_SRC}/${target_rel}"; then
                    matched_markers=$((matched_markers + 1))
                fi
            done < "${markerfile}"
        else
            case "${patchname}" in
                panthera-cfurl-file-read.patch)
                    total_markers=2
                    if [ -f "${CF_SRC}/CFURLAccess.c" ]; then
                        if grep -Fq "static CFDataRef _CFFileURLPantheraCreateData(CFAllocatorRef alloc, CFURLRef url)" "${CF_SRC}/CFURLAccess.c"; then
                            matched_markers=$((matched_markers + 1))
                        fi
                        if grep -Fq "*fetchedData = _CFFileURLPantheraCreateData(alloc, url);" "${CF_SRC}/CFURLAccess.c"; then
                            matched_markers=$((matched_markers + 1))
                        fi
                    fi
                    ;;
            esac
        fi

        if [ "${total_markers}" -gt 0 ]; then
            if [ "${matched_markers}" -eq "${total_markers}" ]; then
                echo "Patch ${patchname} already applied, skipping."
                continue
            elif [ "${matched_markers}" -gt 0 ]; then
                echo "ERROR: Patch ${patchname} is partially applied to CF source at ${CF_SRC} (${matched_markers}/${total_markers} markers found)." >&2
                echo "Source tree is in an inconsistent partial patch state." >&2
                echo "To recover, reset CF source before rebuilding:" >&2
                echo "  rm -rf src/CF-1153.18 && bash tools/fetch_world_sources.sh" >&2
                exit 1
            fi
            # matched_markers == 0: patch is absent (pristine), continue to dry-run and apply
        fi

        # Require clean forward dry-run before applying
        if ! patch_out="$(patch -p0 -d "${CF_SRC}" -f --dry-run < "${patchfile}" 2>&1)"; then
            echo "ERROR: Patch ${patchname} does not apply cleanly to CF source at ${CF_SRC}:" >&2
            echo "${patch_out}" >&2
            echo "To recover, reset CF source before rebuilding:" >&2
            echo "  rm -rf src/CF-1153.18 && bash tools/fetch_world_sources.sh" >&2
            exit 1
        fi

        echo "Applying ${patchname} to CF source..."
        if ! patch -p0 -d "${CF_SRC}" -f -s < "${patchfile}"; then
            echo "ERROR: Failed to apply patch ${patchname} to CF source at ${CF_SRC}" >&2
            exit 1
        fi
    done
fi


rm -rf "${BUILDDIR}"
mkdir -p "${BUILDDIR}"

# Set up include directory so <CoreFoundation/CFBase.h> resolves to the CF source
CF_INCLUDE="${BUILDDIR}/include"
mkdir -p "${CF_INCLUDE}"
ln -sf "${CF_SRC}" "${CF_INCLUDE}/CoreFoundation"

echo "=== Building CoreFoundation from CF-1153.18 ==="
echo "  Source: ${CF_SRC}"
echo "  Output: ${OUTDIR}/libCoreFoundation.dylib"

COMMON_CFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -isysroot "${SDKROOT}"
    -include "${CF_SRC}/CoreFoundation_Prefix.h"
    -include "${SHIMDIR}/panthera_cf_compat.h"
    -I"${SHIMDIR}"
    -I"${CF_INCLUDE}"
    -I"${SYSROOT}/usr/include"
    -I"${SYSROOT}/usr/include/libxml2"
    -DCF_BUILDING_CF=1
    -DPANTHERA=1
    -DDEPLOYMENT_TARGET_MACOSX=1
    -DINCLUDE_OBJC=1
    -Wno-implicit-function-declaration
    -w
    -O2
)

# Compile all 77 .c files
echo "Compiling 77 source files..."
FAIL=0
for src in "${CF_SRC}"/*.c; do
    base=$(basename "$src" .c)
    $CC "${COMMON_CFLAGS[@]}" -c "$src" -o "${BUILDDIR}/${base}.o" &
    # Limit parallelism
    if (( $(jobs -r | wc -l) >= NCPU )); then
        wait -n || FAIL=$((FAIL+1))
    fi
done
wait || true

# Check for failures
for src in "${CF_SRC}"/*.c; do
    base=$(basename "$src" .c)
    if [ ! -f "${BUILDDIR}/${base}.o" ]; then
        echo "FAIL: ${base}.c did not compile"
        FAIL=$((FAIL+1))
    fi
done

if [ $FAIL -ne 0 ]; then
    echo "ERROR: $FAIL files failed to compile"
    exit 1
fi
echo "  77/77 source files compiled"

# Compile the Panthera support file (missing symbol implementations)
echo "Compiling support files..."
$CC "${COMMON_CFLAGS[@]}" \
    -c "${OUTDIR}/cf_panthera_support.c" \
    -o "${BUILDDIR}/cf_panthera_support.o"

# Assemble symbol aliases (Mach-O doesn't support __attribute__((alias)))
$CC -target "${TARGET}" \
    -isysroot "${SDKROOT}" \
    -c "${OUTDIR}/cf_aliases.s" \
    -o "${BUILDDIR}/cf_aliases.o"

# Link
echo "Linking libCoreFoundation.dylib..."
$CC -target "${TARGET}" -mmacosx-version-min=14.0 \
    -isysroot "${SDKROOT}" \
    -Wl,-syslibroot,"${SYSROOT}" \
    -dynamiclib \
    -nodefaultlibs \
    -install_name /usr/lib/libCoreFoundation.dylib \
    -compatibility_version 150.0 -current_version 1153.18 \
    -w \
    "${BUILDDIR}"/*.o \
    -L"${SYSROOT}/usr/lib" \
    -L"${SYSROOT}/usr/lib/system" \
    -lpanthera_extra \
    "${PANTHERA_ROOT}/userland/objc4/lib/libobjc.A.dylib" \
    "${SYSROOT}/usr/lib/libicucore.A.dylib" \
    "${SYSROOT}/usr/lib/libxml2.2.dylib" \
    "${SYSROOT}/usr/lib/libSystem.B.dylib" \
    "${CLANG_RT}" \
    -o "${OUTDIR}/libCoreFoundation.dylib"

echo "  Linked: $(ls -lh "${OUTDIR}/libCoreFoundation.dylib" | awk '{print $5}')"

# Count exports
NEXPORTS=$(nm -gU "${OUTDIR}/libCoreFoundation.dylib" | wc -l | tr -d ' ')
echo "  Exports: ${NEXPORTS}"

# Stage to sysroot
echo ""
echo "=== Staging to sysroot ==="
mkdir -p "${SYSROOT}/usr/lib"
cp "${OUTDIR}/libCoreFoundation.dylib" "${SYSROOT}/usr/lib/libCoreFoundation.dylib"
if [ ! -f "${SYSROOT}/usr/lib/libCoreFoundation.dylib" ]; then
    echo "ERROR: Failed to stage libCoreFoundation.dylib to ${SYSROOT}/usr/lib" >&2
    exit 1
fi
echo "  Staged: ${SYSROOT}/usr/lib/libCoreFoundation.dylib"

CF_SYSROOT_INC="${SYSROOT}/usr/include/CoreFoundation"
rm -rf "${CF_SYSROOT_INC}"
mkdir -p "${CF_SYSROOT_INC}"
cp "${CF_SRC}"/*.h "${CF_SYSROOT_INC}/"

if [ ! -f "${CF_SYSROOT_INC}/CoreFoundation.h" ]; then
    echo "ERROR: CoreFoundation.h missing from staged headers in ${CF_SYSROOT_INC}" >&2
    exit 1
fi
CF_HDR_COUNT=$(ls -1 "${CF_SYSROOT_INC}"/*.h 2>/dev/null | wc -l | tr -d ' ')
echo "  Staged: ${CF_HDR_COUNT} headers to ${CF_SYSROOT_INC}"

# Run audit
echo ""
echo "=== Running symbol audit ==="
bash "${PANTHERA_ROOT}/tools/audit_package.sh" "${OUTDIR}/libCoreFoundation.dylib"

echo ""
echo "=== CoreFoundation build complete ==="
