#!/bin/bash
# build_kext.sh — Compile an IOKit kernel extension from Apple open-source
#
# Usage: ./build_kext.sh <kext_name> <src_dir> <cpp_files...>
#
# Example:
#   ./build_kext.sh AppleAPIC /path/to/AppleAPIC-13 AppleAPIC.cpp
#
# Environment: expects PANTHERA_ROOT to be set, or auto-detects.

set -euo pipefail

KEXT_NAME="$1"; shift
SRC_DIR="$1"; shift

# Parse optional flags before source files.
EXTRA_INCLUDES=()
PREPEND_INCLUDES=()
FORCE_INCLUDES=()
EXTRA_DEFINES=()
UNEXPORTED_SYMBOLS=()
MODULE_START_SYMBOL="_start"
MODULE_STOP_SYMBOL="_stop"
GENERATE_MODULE_INFO=1
ALLOW_LINK_FALLBACK=1
while [[ $# -gt 0 && "$1" == --* ]]; do
    case "$1" in
        --extra-include)
            EXTRA_INCLUDES+=(-I"$2")
            shift 2
            ;;
        --prepend-include)
            PREPEND_INCLUDES+=(-I"$2")
            shift 2
            ;;
        --force-include)
            FORCE_INCLUDES+=(-include "$2")
            shift 2
            ;;
        --define)
            EXTRA_DEFINES+=(-D"$2")
            shift 2
            ;;
        --unexported-symbol)
            UNEXPORTED_SYMBOLS+=("$2")
            shift 2
            ;;
        --module-start)
            MODULE_START_SYMBOL="$2"
            shift 2
            ;;
        --module-stop)
            MODULE_STOP_SYMBOL="$2"
            shift 2
            ;;
        --no-module-info)
            GENERATE_MODULE_INFO=0
            shift
            ;;
        --no-link-fallback)
            ALLOW_LINK_FALLBACK=0
            shift
            ;;
        *)
            echo "Unknown flag: $1"
            exit 1
            ;;
    esac
done

# Remaining args are source files (relative to SRC_DIR)
SRC_FILES=("$@")

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="${PANTHERA_ROOT:-$(cd "$SCRIPT_DIR/../.." && pwd)}"

# Apply OpenIOKit source patch if present and not already applied
PATCH_FILE="${SCRIPT_DIR}/patches/$(basename "${SRC_DIR}").patch"
if [[ ! -f "${PATCH_FILE}" ]]; then
    PATCH_FILE="${SCRIPT_DIR}/patches/${KEXT_NAME}.patch"
fi
PATCH_MARKER="${SRC_DIR}/.panthera-patches-applied"
if [[ -f "${PATCH_FILE}" && ! -f "${PATCH_MARKER}" ]]; then
    echo "  [patch] Applying $(basename "${PATCH_FILE}") to $(basename "${SRC_DIR}")"
    (cd "${SRC_DIR}" && patch -p1 < "${PATCH_FILE}")
    touch "${PATCH_MARKER}"
fi

XNUSRC="${PANTHERA_ROOT}/src/xnu-10002.41.9"
BUILDDIR="${PANTHERA_ROOT}/BUILD"
OBJROOT="${BUILDDIR}/obj"
EXPORT_HDRS="${OBJROOT}/EXPORT_HDRS"
SDKROOT="$(xcrun -sdk macosx -show-sdk-path)"

KEXT_BUILDDIR="${SCRIPT_DIR}/build/${KEXT_NAME}"
KEXT_OBJDIR="${KEXT_BUILDDIR}/obj"
KEXT_BUNDLE="${KEXT_BUILDDIR}/${KEXT_NAME}.kext"
TMP_BUNDLE="${KEXT_OBJDIR}/${KEXT_NAME}.kext.tmp"
PROCESSED_PLIST="${KEXT_OBJDIR}/Info.plist"

rm -rf "${KEXT_BUNDLE}" "${TMP_BUNDLE}"
mkdir -p "${KEXT_OBJDIR}" "${TMP_BUNDLE}/Contents/MacOS"
trap 'rm -rf "${TMP_BUNDLE}"' EXIT
# --- Compiler ---
CC="$(xcrun -sdk macosx -find clang)"
CXX="$(xcrun -sdk macosx -find clang++)"

# --- Target ---
TARGET="x86_64-apple-darwin23.0"
DEPLOYMENT_TARGET="14.0"

# --- Common flags for kernel code ---
KERNEL_CFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min="${DEPLOYMENT_TARGET}"
    -mkernel
    -msoft-float
    -fno-builtin
    -fno-common
    -fno-strict-aliasing
    -fapple-kext

    # Kernel defines — full XNU internal mode (same as kernel build)
    -DAPPLE
    -DKERNEL
    -DKERNEL_PRIVATE
    -DPRIVATE
    -DXNU_KERNEL_PRIVATE
    -DXNU_TARGET_OS_OSX=1
    -DXNU_PLATFORM_MacOSX=1
    -D__PRIVATE_SPI__=1
    -D__MACHO__=1
    -DLP64
    -Dx86_64
    -DX86_64
    -D__X86_64__
    -DTARGET_CPU_X86_64=1
    -DPAGE_SIZE_FIXED
    -DIOKIT
    -Dvolatile=__volatile

    # Force-include macOS 26 SDK compatibility shims
    -include "${SCRIPT_DIR}/kext_compat.h"

    # Warnings
    -Wall
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-four-char-constants
    -Wno-unknown-pragmas
    -Wno-cast-qual
    -Wno-gnu-variable-sized-type-not-at-end
    -Wno-deprecated-register
    -Wno-c99-extensions
    -Wno-address-of-packed-member
    -Wno-shorten-64-to-32
    -Wno-format
    -Wno-variadic-macros
    -Wno-nullability-extension
    -Wno-nullability-completeness
    -Wno-expansion-to-defined
    -Wno-undef
    -Wno-poison-system-directories
    -Wno-register
    -Wno-error

    # Optimization
    -O2
    -g
)

if [[ ${#FORCE_INCLUDES[@]} -gt 0 ]]; then
    KERNEL_CFLAGS+=("${FORCE_INCLUDES[@]}")
fi
if [[ ${#EXTRA_DEFINES[@]} -gt 0 ]]; then
    KERNEL_CFLAGS+=("${EXTRA_DEFINES[@]}")
fi

# --- Include paths ---
# Order matters: local source first, then XNU exported headers, then SDK
KERNEL_INCLUDES=()
if [[ ${#PREPEND_INCLUDES[@]} -gt 0 ]]; then
    KERNEL_INCLUDES+=("${PREPEND_INCLUDES[@]}")
fi
KERNEL_INCLUDES+=(
    # Full XNU source tree includes — same paths as kernel build
    -I"${SCRIPT_DIR}/shims"
    -I"${SCRIPT_DIR}/include"
    -I"${SRC_DIR}"
    -I"${EXPORT_HDRS}/osfmk"
    -I"${EXPORT_HDRS}/bsd"
    -I"${EXPORT_HDRS}/iokit"
    -I"${EXPORT_HDRS}/pexpert"
    -I"${EXPORT_HDRS}/libkern"
    -I"${EXPORT_HDRS}/security"
    -I"${XNUSRC}/osfmk"
    -I"${XNUSRC}/osfmk/libsa"
    -I"${XNUSRC}/bsd"
    -I"${XNUSRC}/iokit"
    -I"${XNUSRC}/pexpert"
    -I"${XNUSRC}/libkern"
    -I"${XNUSRC}/EXTERNAL_HEADERS"
    -I"${XNUSRC}/EXTERNAL_HEADERS/bsd"
    -I"${BUILDDIR}/header_staging/usr/local/include"
    -I"${BUILDDIR}/header_staging/usr/include"
    -I"${SDKROOT}/usr/include"
)

if [[ ${#EXTRA_INCLUDES[@]} -gt 0 ]]; then
    KERNEL_INCLUDES+=("${EXTRA_INCLUDES[@]}")
fi

# --- C++ flags ---
KERNEL_CXXFLAGS=(
    "${KERNEL_CFLAGS[@]}"
    "${KERNEL_INCLUDES[@]}"
    -std=gnu++17
    -fno-rtti
    -fno-exceptions
    -fsized-deallocation
)

# --- C flags ---
KERNEL_CCFLAGS=(
    "${KERNEL_CFLAGS[@]}"
    "${KERNEL_INCLUDES[@]}"
    -std=gnu11
)

# --- Resolve and preprocess Info.plist before compile so we can derive kmod metadata ---
PLIST_SRC=""
if [ -f "${SCRIPT_DIR}/plists/${KEXT_NAME}.plist" ]; then
    PLIST_SRC="${SCRIPT_DIR}/plists/${KEXT_NAME}.plist"
fi
for p in "Info-${KEXT_NAME}.plist" "Info.plist"; do
    if [ -n "$PLIST_SRC" ]; then
        break
    fi
    if [ -f "${SRC_DIR}/${p}" ]; then
        PLIST_SRC="${SRC_DIR}/${p}"
        break
    fi
done

if [ -n "$PLIST_SRC" ]; then
    if grep -Eq '^[[:space:]]*#(if|elif|else|endif)' "$PLIST_SRC"; then
        "${CC}" \
            -E \
            -P \
            -x c \
            -DTARGET_OS_OSX=1 \
            -DTARGET_OS_EMBEDDED=0 \
            -o "${PROCESSED_PLIST}" \
            "$PLIST_SRC"
    else
        cp "$PLIST_SRC" "${PROCESSED_PLIST}"
    fi
fi

MODULE_BUNDLE_ID="${KEXT_NAME}"
MODULE_VERSION="1.0.0"
if [ -f "${PROCESSED_PLIST}" ]; then
    if ! MODULE_BUNDLE_ID="$(plutil -extract CFBundleIdentifier raw -o - "${PROCESSED_PLIST}" 2>/dev/null)"; then
        MODULE_BUNDLE_ID="${KEXT_NAME}"
    fi
    if ! MODULE_VERSION="$(plutil -extract CFBundleVersion raw -o - "${PROCESSED_PLIST}" 2>/dev/null)"; then
        MODULE_VERSION="1.0.0"
    fi
fi

MODULE_INFO_SRC="${KEXT_OBJDIR}/${KEXT_NAME}_module_info.c"
MODULE_INFO_OBJ="${KEXT_OBJDIR}/${KEXT_NAME}_module_info.o"
if [[ "${GENERATE_MODULE_INFO}" == "1" ]]; then
    cat > "${MODULE_INFO_SRC}" <<EOF
#include <mach/kmod.h>

extern kern_return_t ${MODULE_START_SYMBOL}(kmod_info_t *ki, void *data);
extern kern_return_t ${MODULE_STOP_SYMBOL}(kmod_info_t *ki, void *data);

KMOD_EXPLICIT_DECL(${MODULE_BUNDLE_ID}, "${MODULE_VERSION}", ${MODULE_START_SYMBOL}, ${MODULE_STOP_SYMBOL})
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
EOF
fi

KEXT_LDFLAGS=()
case "${KEXT_NAME}" in
    HFS|HFSEncodings)
        KEXT_LDFLAGS+=(
            -Wl,-unexported_symbol,_hfs_allocated
        )
        ;;
esac

LINK_FLAGS=()
if [[ ${#KEXT_LDFLAGS[@]} -gt 0 ]]; then
    LINK_FLAGS+=("${KEXT_LDFLAGS[@]}")
fi
if [[ ${#UNEXPORTED_SYMBOLS[@]} -gt 0 ]]; then
    for symbol in "${UNEXPORTED_SYMBOLS[@]}"; do
        LINK_FLAGS+=(-Wl,-unexported_symbol,"${symbol}")
    done
fi

# --- Compile ---
OBJ_FILES=()
for src in "${SRC_FILES[@]}"; do
    src_path="${SRC_DIR}/${src}"
    obj_stem="${src%.*}"
    obj_name="${obj_stem//\//__}.o"
    obj_path="${KEXT_OBJDIR}/${obj_name}"

    if [[ "$src" == *.cpp ]] || [[ "$src" == *.cc ]]; then
        echo "  CXX  ${src}"
        "${CXX}" "${KERNEL_CXXFLAGS[@]}" -c -o "${obj_path}" "${src_path}"
    elif [[ "$src" == *.c ]]; then
        echo "  CC   ${src}"
        "${CC}" "${KERNEL_CCFLAGS[@]}" -c -o "${obj_path}" "${src_path}"
    elif [[ "$src" == *.S ]] || [[ "$src" == *.s ]]; then
        echo "  AS   ${src}"
        KERNEL_ASFLAGS=()
        skip_next=0
        for flag in "${KERNEL_CCFLAGS[@]}"; do
            if [[ "${skip_next}" == "1" ]]; then
                skip_next=0
                continue
            fi
            if [[ "${flag}" == "-include" ]]; then
                skip_next=1
                continue
            fi
            KERNEL_ASFLAGS+=("${flag}")
        done
        "${CC}" "${KERNEL_ASFLAGS[@]}" -x assembler-with-cpp -c -o "${obj_path}" "${src_path}"
    else
        echo "  SKIP ${src} (unknown extension)"
        continue
    fi
    OBJ_FILES+=("${obj_path}")
done

if [[ "${GENERATE_MODULE_INFO}" == "1" ]]; then
    echo "  CC   $(basename "${MODULE_INFO_SRC}")"
    "${CC}" "${KERNEL_CCFLAGS[@]}" -c -o "${MODULE_INFO_OBJ}" "${MODULE_INFO_SRC}"
    OBJ_FILES+=("${MODULE_INFO_OBJ}")
fi

if [ ${#OBJ_FILES[@]} -eq 0 ]; then
    echo "ERROR: No object files produced"
    exit 1
fi

# --- Link as MH_KEXT_BUNDLE ---
echo "  LD   ${KEXT_NAME}"
"${CC}" \
    -target "${TARGET}" \
    -mmacosx-version-min="${DEPLOYMENT_TARGET}" \
    -nostdlib \
    -Wl,-kext \
    ${LINK_FLAGS[@]+"${LINK_FLAGS[@]}"} \
    -lkmod \
    -lcc_kext \
    -L"${SDKROOT}/usr/lib" \
    -o "${TMP_BUNDLE}/Contents/MacOS/${KEXT_NAME}" \
    "${OBJ_FILES[@]}" 2>&1 || {
        if [[ "${ALLOW_LINK_FALLBACK}" != "1" ]]; then
            echo "ERROR: ${KEXT_NAME} kext link failed"
            exit 1
        fi
        # Try simpler link if -lkmod/-lcc_kext not available
        echo "  LD   ${KEXT_NAME} (fallback: relocatable object)"
        "${CC}" \
            -target "${TARGET}" \
            -nostdlib \
            -r \
            -o "${TMP_BUNDLE}/Contents/MacOS/${KEXT_NAME}" \
            "${OBJ_FILES[@]}"
    }

if [ ! -s "${TMP_BUNDLE}/Contents/MacOS/${KEXT_NAME}" ]; then
    echo "ERROR: ${KEXT_NAME} executable is missing or empty"
    exit 1
fi

# --- Install Info.plist ---
if [ -f "${PROCESSED_PLIST}" ]; then
    cp "${PROCESSED_PLIST}" "${TMP_BUNDLE}/Contents/Info.plist"
    echo "  PLIST ${PLIST_SRC}"
else
    echo "  WARN  No Info.plist found for ${KEXT_NAME}"
fi

# --- Atomically install bundle ---
rm -rf "${KEXT_BUNDLE}"
mv "${TMP_BUNDLE}" "${KEXT_BUNDLE}"

echo "  DONE ${KEXT_BUNDLE}"
