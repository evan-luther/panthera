#!/bin/bash
# build_xnu.sh — Build XNU kernel for x86_64 from Apple open-source
#
# This script:
# 1. Installs headers from dependency projects into a staging sysroot
# 2. Creates stubs for unavailable tools (ctfconvert, ctfmerge, tightbeamc)
# 3. Invokes XNU's own build system for x86_64 RELEASE
#
# Requirements: Xcode with command line tools

set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src"
XNUSRC="${SRCDIR}/xnu-10002.41.9"

# Build directories
BUILDDIR="${PANTHERA_ROOT}/BUILD"
OBJROOT="${BUILDDIR}/obj"
DSTROOT="${BUILDDIR}/dst"
SYMROOT="${BUILDDIR}/sym"

# Header staging area / SDK overlay
# XNU's build reads from SDKROOT, so we overlay onto a writable copy of the host SDK
HDR_STAGING="${BUILDDIR}/header_staging"
SDK_OVERLAY="${HDR_STAGING}/PantheraMacOSX.sdk"
FAKEROOT="${SDK_OVERLAY}"

# Tool stubs directory
TOOLSTUBS="${BUILDDIR}/toolstubs"

# Select complete Xcode developer directory containing required tools (clang, mig, migcom, iig)
REQUIRED_TOOLS=("clang" "mig" "migcom" "iig")

check_dev_dir() {
    local dir="$1"
    [ -d "$dir" ] || return 1
    for tool in "${REQUIRED_TOOLS[@]}"; do
        DEVELOPER_DIR="$dir" xcrun -find "$tool" >/dev/null 2>&1 || return 1
    done
    return 0
}

dev_candidates=()
if [ -n "${PANTHERA_DEVELOPER_DIR:-}" ]; then
    dev_candidates+=("${PANTHERA_DEVELOPER_DIR}")
else
    if current_dir="$(xcode-select -p 2>/dev/null)" && [ -n "${current_dir}" ]; then
        dev_candidates+=("${current_dir}")
    fi
    shopt -s nullglob
    xcode_apps=(/Applications/Xcode*.app)
    shopt -u nullglob
    if [ ${#xcode_apps[@]} -gt 0 ]; then
        while IFS= read -r app; do
            if [ -n "${app}" ] && [ -d "${app}/Contents/Developer" ]; then
                dev_candidates+=("${app}/Contents/Developer")
            fi
        done < <(printf "%s\n" "${xcode_apps[@]}" | LC_ALL=C sort)
    fi
fi

seen_candidates=()
unique_candidates=()
for cand in "${dev_candidates[@]}"; do
    cand_real="$(cd "${cand}" 2>/dev/null && pwd -P || echo "${cand}")"
    if [[ ! " ${seen_candidates[*]:-} " =~ " ${cand_real} " ]]; then
        seen_candidates+=("${cand_real}")
        unique_candidates+=("${cand}")
    fi
done

SELECTED_DEV_DIR=""
for cand in "${unique_candidates[@]}"; do
    if check_dev_dir "${cand}"; then
        SELECTED_DEV_DIR="${cand}"
        break
    fi
done

if [ -z "${SELECTED_DEV_DIR}" ]; then
    echo "ERROR: No complete Xcode developer directory found containing required tools: ${REQUIRED_TOOLS[*]}" >&2
    if [ ${#unique_candidates[@]} -eq 0 ]; then
        echo "  No developer directory candidates found." >&2
    else
        echo "  Checked candidate developer directories:" >&2
        for cand in "${unique_candidates[@]}"; do
            if [ ! -d "$cand" ]; then
                echo "    - ${cand}: directory does not exist" >&2
            else
                missing=()
                for tool in "${REQUIRED_TOOLS[@]}"; do
                    if ! DEVELOPER_DIR="$cand" xcrun -find "$tool" >/dev/null 2>&1; then
                        missing+=("${tool}")
                    fi
                done
                echo "    - ${cand}: missing required tools (${missing[*]})" >&2
            fi
        done
    fi
    echo "Set PANTHERA_DEVELOPER_DIR to an Xcode developer directory with full toolchain support." >&2
    exit 1
fi

export DEVELOPER_DIR="${SELECTED_DEV_DIR}"

# Host macOS SDK (immutable) and resolved real path for APFS cloning
HOST_SDKROOT="$(xcrun -sdk macosx -show-sdk-path)"
HOST_SDK_REAL="$(cd "${HOST_SDKROOT}" && pwd -P)"
SDKROOT="${HOST_SDKROOT}"
NPROC="$(sysctl -n hw.logicalcpu)"

echo "============================================"
echo "Panthera XNU Build — x86_64 RELEASE"
echo "============================================"
echo "XNU source:    ${XNUSRC}"
echo "Developer Dir: ${DEVELOPER_DIR}"
echo "Host SDK:      ${HOST_SDKROOT}"
echo "SDK Overlay:   ${SDK_OVERLAY}"
echo "Build output:  ${BUILDDIR}"
echo "Parallelism:   ${NPROC} jobs"
echo ""

# ──────────────────────────────────────────────
# Step 0: Clean setup
# ──────────────────────────────────────────────
echo ">>> Step 0: Setting up build directories"
if [[ "${PANTHERA_FORCE_REBUILD:-0}" == "1" ]]; then
    echo "  [clean] Force rebuild requested — removing stale build directories..."
    rm -rf "${OBJROOT}" "${DSTROOT}" "${SYMROOT}" "${TOOLSTUBS}" "${HDR_STAGING}"
fi
mkdir -p "${OBJROOT}" "${DSTROOT}" "${SYMROOT}" "${TOOLSTUBS}" "${HDR_STAGING}"

# Recreate the build-local SDK overlay via APFS copy-on-write clone from host SDK
echo "  [overlay] Cloning host SDK into ${SDK_OVERLAY}..."
rm -rf "${SDK_OVERLAY}"
if ! cp -cR "${HOST_SDK_REAL}" "${SDK_OVERLAY}"; then
    echo "ERROR: Failed to clone host SDK from ${HOST_SDK_REAL} to ${SDK_OVERLAY}" >&2
    exit 1
fi

mkdir -p "${FAKEROOT}/usr/local/include"
mkdir -p "${FAKEROOT}/usr/include"
mkdir -p "${FAKEROOT}/System/Library/Frameworks/System.framework/PrivateHeaders"
mkdir -p "${FAKEROOT}/System/Library/Frameworks/Kernel.framework/PrivateHeaders"
# ──────────────────────────────────────────────
# Step 1: Install dependency headers
# ──────────────────────────────────────────────
echo ""
echo ">>> Step 1: Installing dependency headers"

# --- AvailabilityVersions ---
echo "  [hdrs] AvailabilityVersions"
AVDIR="${SRCDIR}/AvailabilityVersions-137.4"
if [ -d "$AVDIR" ]; then
    mkdir -p "${FAKEROOT}/usr/local/libexec"
    # Install via its Makefile
    if [ -f "${AVDIR}/Makefile" ]; then
        make -C "${AVDIR}" install \
            DSTROOT="${FAKEROOT}" || echo "  [warn] AvailabilityVersions make install reported non-zero status"
    fi
    # Ensure availability.pl exists and is executable in the SDK overlay
    if [ ! -f "${FAKEROOT}/usr/local/libexec/availability.pl" ] && [ -f "${AVDIR}/availability" ]; then
        cp "${AVDIR}/availability" "${FAKEROOT}/usr/local/libexec/availability.pl"
        chmod +x "${FAKEROOT}/usr/local/libexec/availability.pl"
    fi
fi

# --- libplatform ---
echo "  [hdrs] libplatform"
LPDIR="${SRCDIR}/libplatform-306.0.1"
if [ -d "${LPDIR}/include" ]; then
    cp -R "${LPDIR}/include/"* "${FAKEROOT}/usr/local/include/" 2>/dev/null || true
fi
if [ -d "${LPDIR}/private" ]; then
    cp -R "${LPDIR}/private/"* "${FAKEROOT}/usr/local/include/" 2>/dev/null || true
fi

# --- libdispatch ---
echo "  [hdrs] libdispatch"
LDDIR="${SRCDIR}/libdispatch-1462.0.4"
if [ -d "${LDDIR}/dispatch" ]; then
    mkdir -p "${FAKEROOT}/usr/include/dispatch"
    cp -R "${LDDIR}/dispatch/"* "${FAKEROOT}/usr/include/dispatch/" 2>/dev/null || true
fi
if [ -d "${LDDIR}/os" ]; then
    mkdir -p "${FAKEROOT}/usr/include/os"
    cp -R "${LDDIR}/os/"* "${FAKEROOT}/usr/include/os/" 2>/dev/null || true
fi
if [ -d "${LDDIR}/private" ]; then
    mkdir -p "${FAKEROOT}/usr/local/include/dispatch"
    cp -R "${LDDIR}/private/"*.h "${FAKEROOT}/usr/local/include/dispatch/" 2>/dev/null || true
fi

# --- dtrace ---
echo "  [hdrs] dtrace"
DTDIR="${SRCDIR}/dtrace-401"
if [ -d "${DTDIR}/lib/libdtrace" ]; then
    mkdir -p "${FAKEROOT}/usr/include/sys"
    # dtrace provides sys/dtrace.h and related headers
    find "${DTDIR}" -name "*.h" -path "*/include/*" -exec cp {} "${FAKEROOT}/usr/include/" \; 2>/dev/null || true
    find "${DTDIR}" -name "dtrace.h" -exec cp {} "${FAKEROOT}/usr/include/sys/" \; 2>/dev/null || true
fi

# --- Libc ---
echo "  [hdrs] Libc"
LCDIR="${SRCDIR}/Libc-1583.40.7"
if [ -d "${LCDIR}/include" ]; then
    cp -R "${LCDIR}/include/"* "${FAKEROOT}/usr/include/" 2>/dev/null || true
fi

# --- libpthread ---
echo "  [hdrs] libpthread"
PTDIR="${SRCDIR}/libpthread-519"
if [ -d "${PTDIR}/include" ]; then
    cp -R "${PTDIR}/include/"* "${FAKEROOT}/usr/include/" 2>/dev/null || true
fi
if [ -d "${PTDIR}/private" ]; then
    cp -R "${PTDIR}/private/"* "${FAKEROOT}/usr/local/include/" 2>/dev/null || true
fi
# pthread kernel-private headers
if [ -d "${PTDIR}/kern" ]; then
    mkdir -p "${FAKEROOT}/usr/local/include/kern"
    cp -R "${PTDIR}/kern/"*.h "${FAKEROOT}/usr/local/include/kern/" 2>/dev/null || true
fi

# --- libmalloc ---
echo "  [hdrs] libmalloc"
LMDIR="${SRCDIR}/libmalloc-474.0.13"
if [ -d "${LMDIR}/include" ]; then
    cp -R "${LMDIR}/include/"* "${FAKEROOT}/usr/include/" 2>/dev/null || true
fi
if [ -d "${LMDIR}/private" ]; then
    cp -R "${LMDIR}/private/"* "${FAKEROOT}/usr/local/include/" 2>/dev/null || true
fi

# --- libclosure ---
echo "  [hdrs] libclosure"
CLDIR="${SRCDIR}/libclosure-90"
if [ -d "${CLDIR}" ]; then
    find "${CLDIR}" -name "Block*.h" -exec cp {} "${FAKEROOT}/usr/local/include/" \; 2>/dev/null || true
fi

# --- dyld ---
echo "  [hdrs] dyld"
DYDIR="${SRCDIR}/dyld-1122.1.2"
if [ -d "${DYDIR}/include" ]; then
    cp -R "${DYDIR}/include/"* "${FAKEROOT}/usr/include/" 2>/dev/null || true
fi

# --- objc4 ---
echo "  [hdrs] objc4"
OBJCDIR="${SRCDIR}/objc4-906"
if [ -d "${OBJCDIR}/runtime" ]; then
    mkdir -p "${FAKEROOT}/usr/include/objc"
    find "${OBJCDIR}/runtime" -name "*.h" -exec cp {} "${FAKEROOT}/usr/include/objc/" \; 2>/dev/null || true
fi

# --- architecture ---
echo "  [hdrs] architecture"
ARCHDIR="${SRCDIR}/architecture-282"
if [ -d "${ARCHDIR}" ]; then
    mkdir -p "${FAKEROOT}/usr/include/architecture"
    mkdir -p "${FAKEROOT}/usr/include/architecture/i386"
    find "${ARCHDIR}" -maxdepth 1 -name "*.h" -exec cp {} "${FAKEROOT}/usr/include/architecture/" \; 2>/dev/null || true
    if [ -d "${ARCHDIR}/i386" ]; then
        cp -R "${ARCHDIR}/i386/"*.h "${FAKEROOT}/usr/include/architecture/i386/" 2>/dev/null || true
    fi
fi

# --- Libsystem ---
echo "  [hdrs] Libsystem"
LSDIR="${SRCDIR}/Libsystem-1336"
if [ -d "${LSDIR}" ]; then
    find "${LSDIR}" -name "*.h" -path "*/include/*" -exec cp {} "${FAKEROOT}/usr/include/" \; 2>/dev/null || true
fi

# --- IOKitUser (IOKit userland headers) ---
echo "  [hdrs] IOKitUser"
IOUDIR="${SRCDIR}/IOKitUser-100065.40.4"
if [ -d "${IOUDIR}" ]; then
    mkdir -p "${FAKEROOT}/System/Library/Frameworks/IOKit.framework/Headers"
    find "${IOUDIR}" -name "*.h" -exec cp {} "${FAKEROOT}/System/Library/Frameworks/IOKit.framework/Headers/" \; 2>/dev/null || true
fi

echo "  [done] Headers installed to ${FAKEROOT}"

# ──────────────────────────────────────────────
# Step 2: Create tool stubs
# ──────────────────────────────────────────────
echo ""
echo ">>> Step 2: Creating tool stubs"

# ctfconvert — DTrace compact type format converter (not needed without DTrace)
cat > "${TOOLSTUBS}/ctfconvert" << 'STUB'
#!/bin/bash
set -euo pipefail

outfile=""
while [ $# -gt 0 ]; do
    case "$1" in
        -o)
            outfile="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

if [ -n "$outfile" ]; then
    mkdir -p "$(dirname "$outfile")"
    : > "$outfile"
fi

exit 0
STUB
chmod +x "${TOOLSTUBS}/ctfconvert"

# ctfmerge — DTrace type merge
cat > "${TOOLSTUBS}/ctfmerge" << 'STUB'
#!/bin/bash
set -euo pipefail

outfile=""
ctfdata=""
while [ $# -gt 0 ]; do
    case "$1" in
        -o)
            outfile="$2"
            shift 2
            ;;
        -Z)
            ctfdata="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

if [ -n "$outfile" ] && [ ! -e "$outfile" ]; then
    mkdir -p "$(dirname "$outfile")"
    : > "$outfile"
fi

if [ -n "$ctfdata" ]; then
    mkdir -p "$(dirname "$ctfdata")"
    printf 'stub ctf data\n' > "$ctfdata"
fi

exit 0
STUB
chmod +x "${TOOLSTUBS}/ctfmerge"

# ctf_insert
cat > "${TOOLSTUBS}/ctf_insert" << 'STUB'
#!/bin/bash
exit 0
STUB
chmod +x "${TOOLSTUBS}/ctf_insert"

# ctfdump
cat > "${TOOLSTUBS}/ctfdump" << 'STUB'
#!/bin/bash
exit 0
STUB
chmod +x "${TOOLSTUBS}/ctfdump"

# tightbeamc — IOKit IIG compiler (ExclaveKit, not needed for x86_64)
cat > "${TOOLSTUBS}/tightbeamc" << 'STUB'
#!/bin/bash
exit 0
STUB
chmod +x "${TOOLSTUBS}/tightbeamc"

echo "  [done] Tool stubs in ${TOOLSTUBS}"

# ──────────────────────────────────────────────
# Step 3: Build XNU
# ──────────────────────────────────────────────
echo ""
echo ">>> Step 3: Building XNU kernel (x86_64 RELEASE)"
echo "    This will take a while..."
echo ""

# Put our tool stubs first in PATH so they shadow missing xcrun tools
export PATH="${TOOLSTUBS}:${PATH}"

# Override ctf tools explicitly
export CTFCONVERT="${TOOLSTUBS}/ctfconvert"
export CTFMERGE="${TOOLSTUBS}/ctfmerge"
export CTFINSERT="${TOOLSTUBS}/ctf_insert"
export CTFDUMP="${TOOLSTUBS}/ctfdump"
export TIGHTBEAMC="${TOOLSTUBS}/tightbeamc"

# XNU version — xnu-10002.41.9 corresponds to Darwin 23.1.0 (macOS 14.1 Sonoma)
export RC_DARWIN_KERNEL_VERSION="23.1.0"

# Suppress noisy xcrun lookup notes
export XCRUN_NO_NOTE=1

cd "${XNUSRC}"

# Resolve tool paths explicitly — XNU's auto-detection can fail
# with SDK version mismatches (we're building xnu-10002/Sonoma with a macOS 26 SDK)
CLANG="$(xcrun -sdk macosx -find clang)"
CLANGXX="$(xcrun -sdk macosx -find clang++)"

# Common make args
# Xcode 27 diagnostics for XNU 10002 compatibility (-Werror retained)
XNU_EXTRA_CFLAGS=(
    "-Wno-missing-include-dirs"
    "-Wno-reserved-identifier"
    "-Wno-missing-designated-field-initializers"
    "-Wno-ms-bitfield-padding"
    "-Wno-pre-c11-compat"
    "-Wno-format"
    "-Wno-switch-default"
    "-Wno-unused-but-set-variable"
    "-Wno-suggest-override"
    "-Wno-suggest-destructor-override"
    "-Wno-unnecessary-virtual-specifier"
    "-Wno-nrvo"
    "-Wno-four-char-constants"
    "-Wno-unterminated-string-initialization"
    "-Wno-void-pointer-to-int-cast"
    "-Wno-nontrivial-memcall"
    "-Wno-implicit-int-float-conversion"
    "-Wno-implicit-fallthrough"
    "-Wno-global-constructors"
)
if [[ "${PANTHERA_XNU_VFS_PHASE_TRACE:-0}" == "1" ]]; then
    XNU_EXTRA_CFLAGS+=("-DPANTHERA_VFS_PHASE_TRACE=1")
fi

MAKE_ARGS=(
    SDKROOT="${HOST_SDKROOT}"
    ARCH_CONFIGS="X86_64"
    KERNEL_CONFIGS="RELEASE"
    MACHINE_CONFIGS="NONE"
    OBJROOT="${OBJROOT}"
    DSTROOT="${DSTROOT}"
    SYMROOT="${SYMROOT}"
    SRCROOT="${XNUSRC}"
    PLATFORM="MacOSX"
    CC="${CLANG}"
    CXX="${CLANGXX}"
    RC_NONARCH_CFLAGS="${XNU_EXTRA_CFLAGS[*]}"
    CONCISE=1
)

# Assert required availability tooling exists in SDK overlay before XNU setup
if [ ! -x "${SDK_OVERLAY}/usr/local/libexec/availability.pl" ]; then
    echo "ERROR: Required availability tooling not found or not executable at ${SDK_OVERLAY}/usr/local/libexec/availability.pl" >&2
    exit 1
fi
export PANTHERA_AVAILABILITY_PL="${SDK_OVERLAY}/usr/local/libexec/availability.pl"


# Phase 1: Build host setup tools (setsegname, kextsymboltool, etc.)
echo "  [3a] Building setup tools..."
set +e
make "${MAKE_ARGS[@]}" setup -j"${NPROC}" > "${BUILDDIR}/build_setup.log" 2>&1
SETUP_EXIT=$?
set -e
if [ ${SETUP_EXIT} -ne 0 ]; then
    echo "SETUP FAILED (exit code: ${SETUP_EXIT}) — see ${BUILDDIR}/build_setup.log" >&2
    echo "Last 200 lines of setup log:" >&2
    tail -n 200 "${BUILDDIR}/build_setup.log" >&2 || true
    echo "Full setup log: ${BUILDDIR}/build_setup.log" >&2
    exit ${SETUP_EXIT}
fi

# Phase 2: Export headers from all components
echo "  [3b] Exporting headers..."
set +e
make "${MAKE_ARGS[@]}" exporthdrs -j"${NPROC}" > "${BUILDDIR}/build_exporthdrs.log" 2>&1
EXPORTHDRS_EXIT=$?
set -e
if [ ${EXPORTHDRS_EXIT} -ne 0 ]; then
    echo "EXPORTHDRS FAILED (exit code: ${EXPORTHDRS_EXIT}) — see ${BUILDDIR}/build_exporthdrs.log" >&2
    echo "Last 200 lines of exporthdrs log:" >&2
    tail -n 200 "${BUILDDIR}/build_exporthdrs.log" >&2 || true
    echo "Full exporthdrs log: ${BUILDDIR}/build_exporthdrs.log" >&2
    exit ${EXPORTHDRS_EXIT}
fi

# Phase 2.5: Create any missing EXPORT_HDRS directories and fix include paths
# Some components (e.g. libsa) are in COMPONENT_LIST but export no headers,
# yet -Wmissing-include-dirs is -Werror'd. Create empty dirs for all components.
echo "  [3b+] Ensuring EXPORT_HDRS directories exist and fixing includes..."
for comp in osfmk bsd libkern iokit pexpert libsa security san; do
    mkdir -p "${OBJROOT}/EXPORT_HDRS/${comp}"
done
# Export generated sys sidecar headers consumed by sys/cdefs.h.
mkdir -p "${OBJROOT}/EXPORT_HDRS/bsd/sys"
for generated_header in _symbol_aliasing.h _posix_availability.h; do
    generated_source="${OBJROOT}/bsd/sys/${generated_header}"
    if [ ! -f "${generated_source}" ]; then
        echo "ERROR: Required generated XNU header missing: ${generated_source}" >&2
        exit 1
    fi
    cp -f "${generated_source}" "${OBJROOT}/EXPORT_HDRS/bsd/sys/${generated_header}"
done
# Export canonical userland Mach headers into EXPORT_HDRS for mach consumers.
mkdir -p "${OBJROOT}/EXPORT_HDRS/osfmk/mach"
for mach_header in vm_page_size.h mach_error.h; do
    mach_source="${XNUSRC}/libsyscall/mach/mach/${mach_header}"
    if [ ! -f "${mach_source}" ]; then
        echo "ERROR: Required canonical Mach header missing: ${mach_source}" >&2
        exit 1
    fi
    cp -f "${mach_source}" "${OBJROOT}/EXPORT_HDRS/osfmk/mach/${mach_header}"
done
# Keep XNU's own kernel-compatible os headers emitted by exporthdrs.
# Userland os/object*.h are staged separately by build_panthera_sdk.sh.
# osfmk/kern/misc_protos.h is needed by cross-component includes (iokit includes kern_types.h)
cp "${XNUSRC}/osfmk/kern/misc_protos.h" "${OBJROOT}/EXPORT_HDRS/osfmk/kern/" 2>/dev/null || true
# Copy libsa type headers for cross-component string.h resolution (but NOT the conflicting root types.h)
mkdir -p "${OBJROOT}/EXPORT_HDRS/osfmk/libsa/machine" "${OBJROOT}/EXPORT_HDRS/osfmk/libsa/i386"
cp "${XNUSRC}/osfmk/libsa/machine/types.h" "${OBJROOT}/EXPORT_HDRS/osfmk/libsa/machine/" 2>/dev/null || true
cp "${XNUSRC}/osfmk/libsa/i386/types.h" "${OBJROOT}/EXPORT_HDRS/osfmk/libsa/i386/" 2>/dev/null || true

# Phase 3: Build the kernel
echo "  [3c] Compiling kernel..."
set +e
make "${MAKE_ARGS[@]}" build -j"${NPROC}" > "${BUILDDIR}/build.log" 2>&1
BUILD_EXIT=$?
set -e

if [ $BUILD_EXIT -ne 0 ]; then
    echo ""
    echo "  [3c-retry] Retrying failed targets with -j1 for clean error output..."
    set +e
    make "${MAKE_ARGS[@]}" build -j1 > "${BUILDDIR}/build_retry.log" 2>&1
    BUILD_STATUS=$?
    set -e
else
    BUILD_STATUS=$BUILD_EXIT
fi

echo ""
echo "============================================"
if [ $BUILD_STATUS -eq 0 ]; then
    echo "BUILD SUCCEEDED"
    echo ""
    echo "Kernel binary:"
    find "${OBJROOT}" "${SYMROOT}" -name "mach_kernel" -o -name "kernel" 2>/dev/null || echo "(searching...)"
    find "${OBJROOT}" "${SYMROOT}" \( -name "*.development" -o -name "*.release" \) 2>/dev/null || true
    echo "============================================"
    echo "Full build log: ${BUILDDIR}/build.log"
else
    echo "BUILD FAILED (exit code: ${BUILD_STATUS})" >&2
    echo "" >&2
    if [ -f "${BUILDDIR}/build_retry.log" ]; then
        echo "Last 200 lines of serial retry log:" >&2
        tail -n 200 "${BUILDDIR}/build_retry.log" >&2 || true
        echo "============================================" >&2
        echo "Full build log: ${BUILDDIR}/build.log" >&2
        echo "Full retry log: ${BUILDDIR}/build_retry.log" >&2
    else
        echo "Last 200 lines of build log:" >&2
        tail -n 200 "${BUILDDIR}/build.log" >&2 || true
        echo "============================================" >&2
        echo "Full build log: ${BUILDDIR}/build.log" >&2
    fi
    exit $BUILD_STATUS
fi
