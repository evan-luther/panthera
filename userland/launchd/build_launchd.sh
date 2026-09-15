#!/bin/bash
# build_launchd.sh — Reproducible build for Panthera launchd
# This is the ONE script that produces the launchd binary. No hand-linking.
#
# Set PANTHERA_LAUNCHD_REAL=1 (default) for Apple's real runtime.c + core.c.
# Set PANTHERA_LAUNCHD_REAL=0 for the Panthera shim fallback.
# Set PANTHERA_LAUNCHD_FLAT_NAMESPACE=1 only for legacy debugging.
set -euo pipefail

PANTHERA_LAUNCHD_REAL="${PANTHERA_LAUNCHD_REAL:-1}"
PANTHERA_LAUNCHD_FLAT_NAMESPACE="${PANTHERA_LAUNCHD_FLAT_NAMESPACE:-0}"
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-14.0}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
OBJDIR="${SCRIPT_DIR}/obj"
SDK="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
LD="$(xcrun -sdk macosx -find ld)"
MIG="$(xcrun -sdk macosx -find mig 2>/dev/null || xcrun -find mig 2>/dev/null || which mig)"
TARGET="x86_64-apple-darwin23.0"

if [[ -z "${MIG:-}" || ! -x "${MIG}" ]]; then
    echo "Error: mig tool not found" >&2
    exit 1
fi

LAUNCHD_SRC="${PANTHERA_ROOT}/src/launchd-842.92.1/src"
LIBLAUNCH_SRC="${PANTHERA_ROOT}/src/launchd-842.92.1/liblaunch"
XNU_SRC="${PANTHERA_ROOT}/src/xnu-10002.41.9"
MIG_GEN_SRC="${OBJDIR}/mig_gen"
LIBBSM="${SYSROOT}/usr/lib/libbsm.0.dylib"
LIBSYSTEM="${SYSROOT}/usr/lib/libSystem.B.dylib"
OUT_BIN="${SCRIPT_DIR}/launchd"

if [[ ! -f "${LIBBSM}" ]]; then
    echo "Error: missing required library: ${LIBBSM}" >&2
    exit 1
fi

if [[ ! -f "${LIBSYSTEM}" ]]; then
    echo "Error: missing required library: ${LIBSYSTEM}" >&2
    exit 1
fi

mkdir -p "${OBJDIR}" "${MIG_GEN_SRC}"

echo "=== Building launchd ==="
echo "  Mode:    $([ "$PANTHERA_LAUNCHD_REAL" = "1" ] && echo "REAL (Apple runtime.c + core.c)" || echo "SHIM (launchd_all_stubs.c)")"
echo "  Linkage: $([ "$PANTHERA_LAUNCHD_FLAT_NAMESPACE" = "1" ] && echo "flat namespace (legacy)" || echo "two-level namespace")"
echo "  Sysroot: ${SYSROOT}"
echo "  Output:  ${OUT_BIN}"
echo ""

# Common include flags for real launchd mode
REAL_CFLAGS="-target $TARGET -mmacosx-version-min=${DEPLOYMENT_TARGET} -isysroot $SDK"
REAL_CFLAGS+=" -I ${SCRIPT_DIR}/real/shims"
REAL_CFLAGS+=" -I ${MIG_GEN_SRC}"
REAL_CFLAGS+=" -I ${PANTHERA_ROOT}/src/launchd-842.92.1/stubs"
REAL_CFLAGS+=" -I ${PANTHERA_ROOT}/src/launchd-842.92.1/liblaunch"
REAL_CFLAGS+=" -I ${LAUNCHD_SRC}"
REAL_CFLAGS+=" -include ${SCRIPT_DIR}/real/panthera_launchd_config.h"
REAL_CFLAGS+=" -include ${SCRIPT_DIR}/launchd_build_compat.h"
REAL_CFLAGS+=" -D_DARWIN_C_SOURCE -DPANTHERA=1"
REAL_CFLAGS+=" -Wno-everything -O2"

# Common include flags for shim launchd mode
SHIM_CFLAGS="-target $TARGET -mmacosx-version-min=${DEPLOYMENT_TARGET} -isysroot $SDK"
SHIM_CFLAGS+=" -I ${PANTHERA_ROOT}/src/launchd-842.92.1/stubs"
SHIM_CFLAGS+=" -I ${PANTHERA_ROOT}/src/launchd-842.92.1/liblaunch"
SHIM_CFLAGS+=" -I ${MIG_GEN_SRC}"
SHIM_CFLAGS+=" -I ${LAUNCHD_SRC}"
SHIM_CFLAGS+=" -I ${SYSROOT}/usr/include"
SHIM_CFLAGS+=" -include ${SCRIPT_DIR}/launchd_build_compat.h"
SHIM_CFLAGS+=" -D_DARWIN_C_SOURCE"
SHIM_CFLAGS+=" -DAVAILABLE_MAC_OS_X_VERSION_10_0_AND_LATER_BUT_DEPRECATED_IN_MAC_OS_X_VERSION_10_5="
SHIM_CFLAGS+=" -Wno-everything -O2"
# Validate pinned definition inputs
LAUNCHD_DEFS=(helper internal job job_forward job_reply)
MACH_DEFS=(exc mach_exc notify)
XNU_HEADERS=(sys/kern_memorystatus.h sys/reason.h)
for def in "${LAUNCHD_DEFS[@]}"; do
    if [[ ! -f "${LAUNCHD_SRC}/${def}.defs" ]]; then
        echo "Error: missing pinned MIG definition file: ${LAUNCHD_SRC}/${def}.defs" >&2
        exit 1
    fi
done

for def in "${MACH_DEFS[@]}"; do
    if [[ ! -f "${SDK}/usr/include/mach/${def}.defs" ]]; then
        echo "Error: missing SDK Mach MIG definition file: ${SDK}/usr/include/mach/${def}.defs" >&2
        exit 1
    fi
done
for hdr in "${XNU_HEADERS[@]}"; do
    if [[ ! -f "${XNU_SRC}/bsd/${hdr}" ]]; then
        echo "Error: missing pinned XNU header: ${XNU_SRC}/bsd/${hdr}" >&2
        exit 1
    fi
done

echo "=== Generating MIG stubs ==="
for def in "${LAUNCHD_DEFS[@]}"; do
    echo "  [MIG] ${def}.defs"
    "$MIG" -arch x86_64 -isysroot "$SDK" \
        -I"${LAUNCHD_SRC}" \
        -I"${LIBLAUNCH_SRC}" \
        -I"${PANTHERA_ROOT}/src/launchd-842.92.1/stubs" \
        -I"${SCRIPT_DIR}/real/shims" \
        -header "${MIG_GEN_SRC}/${def}.h" \
        -user "${MIG_GEN_SRC}/${def}User.c" \
        -sheader "${MIG_GEN_SRC}/${def}Server.h" \
        -server "${MIG_GEN_SRC}/${def}Server.c" \
        "${LAUNCHD_SRC}/${def}.defs"
done

for def in "${MACH_DEFS[@]}"; do
    echo "  [MIG] mach/${def}.defs"
    "$MIG" -arch x86_64 -isysroot "$SDK" \
        -I"${LAUNCHD_SRC}" \
        -I"${LIBLAUNCH_SRC}" \
        -I"${PANTHERA_ROOT}/src/launchd-842.92.1/stubs" \
        -I"${SCRIPT_DIR}/real/shims" \
        -header "${MIG_GEN_SRC}/${def}.h" \
        -user "${MIG_GEN_SRC}/${def}User.c" \
        -sheader "${MIG_GEN_SRC}/${def}Server.h" \
        -server "${MIG_GEN_SRC}/${def}Server.c" \
        "${SDK}/usr/include/mach/${def}.defs"
done
echo ""
if [ "$PANTHERA_LAUNCHD_REAL" = "1" ]; then
    # ===== REAL LAUNCHD: Apple's runtime.c + core.c =====

    # Step 1: Compile launchd.c
    echo "  [CC] launchd.c (real)"
    $CC $REAL_CFLAGS \
        -c -o "${OBJDIR}/src_launchd.o" \
        "${LAUNCHD_SRC}/launchd.c"

    # Step 2: Compile runtime.c (from real/ with Panthera patches)
    echo "  [CC] runtime.c (real)"
    $CC $REAL_CFLAGS \
        -c -o "${OBJDIR}/src_runtime.o" \
        "${SCRIPT_DIR}/real/runtime.c"

    # Step 3: Compile core.c (from real/ with #if HAVE_* guards)
    echo "  [CC] core.c (real)"
    $CC $REAL_CFLAGS \
        -c -o "${OBJDIR}/src_core.o" \
        "${SCRIPT_DIR}/real/core.c"

    # Step 4: Compile panthera_boot.c
    echo "  [CC] panthera_boot.c"
    $CC -target $TARGET -mmacosx-version-min="${DEPLOYMENT_TARGET}" -isysroot "$SDK" \
        -D_DARWIN_C_SOURCE -Wno-everything -O2 \
        -c -o "${OBJDIR}/panthera_boot.o" \
        "${SCRIPT_DIR}/real/panthera_boot.c"

    # Step 5: Compile panthera_xpc_pipe.c
    echo "  [CC] panthera_xpc_pipe.c"
    $CC -target $TARGET -mmacosx-version-min="${DEPLOYMENT_TARGET}" -isysroot "$SDK" \
        -I "${SCRIPT_DIR}/real/shims" \
        -D_DARWIN_C_SOURCE -Wno-everything -O2 \
        -c -o "${OBJDIR}/panthera_xpc_pipe.o" \
        "${SCRIPT_DIR}/real/panthera_xpc_pipe.c"

    # Step 5b: Compile panthera_link_stubs.c
    echo "  [CC] panthera_link_stubs.c"
    $CC -target $TARGET -mmacosx-version-min="${DEPLOYMENT_TARGET}" -isysroot "$SDK" \
        -D_DARWIN_C_SOURCE -Wno-everything -O2 \
        -c -o "${OBJDIR}/panthera_link_stubs.o" \
        "${SCRIPT_DIR}/real/panthera_link_stubs.c"

    # Step 6: Compile launchd support sources
    echo "  [CC] ipc.c, log.c, kill2.c, ktrace.c"
    $CC $REAL_CFLAGS -c -o "${OBJDIR}/src_ipc.o" "${LAUNCHD_SRC}/ipc.c"
    $CC $REAL_CFLAGS -c -o "${OBJDIR}/src_log.o" "${LAUNCHD_SRC}/log.c"
    $CC $REAL_CFLAGS -c -o "${OBJDIR}/src_kill2.o" "${LAUNCHD_SRC}/kill2.c"
    $CC $REAL_CFLAGS -c -o "${OBJDIR}/src_ktrace.o" "${LAUNCHD_SRC}/ktrace.c"

    # Step 7: Compile liblaunch sources
    echo "  [CC] libbootstrap.c, liblaunch.c, libvproc.c"
    $CC $REAL_CFLAGS -c -o "${OBJDIR}/liblaunch_libbootstrap.o" "${LIBLAUNCH_SRC}/libbootstrap.c"
    $CC $REAL_CFLAGS -c -o "${OBJDIR}/liblaunch_liblaunch.o" "${LIBLAUNCH_SRC}/liblaunch.c"
    $CC $REAL_CFLAGS -c -o "${OBJDIR}/liblaunch_libvproc.o" "${LIBLAUNCH_SRC}/libvproc.c"

    # Step 8: Compile MIG generated stubs
    echo "  [CC] MIG generated stubs"
    for mig_name in excServer excUser helperServer helperUser internalServer internalUser \
                    job_forwardUser job_replyUser jobServer jobUser \
                    mach_excServer mach_excUser notifyServer notifyUser; do
        $CC $REAL_CFLAGS -c -o "${OBJDIR}/mig_gen_${mig_name}.o" "${MIG_GEN_SRC}/${mig_name}.c"
    done

    # Step 9: Compile panthera_fork.s
    echo "  [AS] panthera_fork.s"
    $CC -target $TARGET -mmacosx-version-min="${DEPLOYMENT_TARGET}" -isysroot "$SDK" \
        -c -o "${OBJDIR}/panthera_fork.o" \
        "${SCRIPT_DIR}/panthera_fork.s"

    # Step 10: Link everything
    echo "  [LD] launchd (real)"
    OBJECTS=(
        "${OBJDIR}/src_launchd.o"
        "${OBJDIR}/src_runtime.o"
        "${OBJDIR}/src_core.o"
        "${OBJDIR}/panthera_boot.o"
        "${OBJDIR}/panthera_xpc_pipe.o"
        "${OBJDIR}/panthera_link_stubs.o"
        "${OBJDIR}/src_ipc.o"
        "${OBJDIR}/src_log.o"
        "${OBJDIR}/src_kill2.o"
        "${OBJDIR}/src_ktrace.o"
        "${OBJDIR}/liblaunch_libbootstrap.o"
        "${OBJDIR}/liblaunch_liblaunch.o"
        "${OBJDIR}/liblaunch_libvproc.o"
        "${OBJDIR}/mig_gen_excServer.o"
        "${OBJDIR}/mig_gen_excUser.o"
        "${OBJDIR}/mig_gen_helperServer.o"
        "${OBJDIR}/mig_gen_helperUser.o"
        "${OBJDIR}/mig_gen_internalServer.o"
        "${OBJDIR}/mig_gen_internalUser.o"
        "${OBJDIR}/mig_gen_job_forwardUser.o"
        "${OBJDIR}/mig_gen_job_replyUser.o"
        "${OBJDIR}/mig_gen_jobServer.o"
        "${OBJDIR}/mig_gen_jobUser.o"
        "${OBJDIR}/mig_gen_mach_excServer.o"
        "${OBJDIR}/mig_gen_mach_excUser.o"
        "${OBJDIR}/mig_gen_notifyServer.o"
        "${OBJDIR}/mig_gen_notifyUser.o"
        "${OBJDIR}/panthera_fork.o"
    )
else
    # ===== SHIM LAUNCHD: Panthera's launchd_all_stubs.c =====

    # Step 1: Compile launchd.c
    echo "  [CC] launchd.c (shim)"
    $CC $SHIM_CFLAGS \
        -c -o "${OBJDIR}/src_launchd.o" \
        "${LAUNCHD_SRC}/launchd.c"

    # Step 2: Compile launchd_all_stubs.c
    echo "  [CC] launchd_all_stubs.c"
    $CC -target $TARGET -mmacosx-version-min="${DEPLOYMENT_TARGET}" -isysroot "$SDK" \
        -I "${SYSROOT}/usr/include" \
        -DPRIVATE=1 -D__DARWIN_UNIX03=1 \
        -Wno-everything \
        -c -O2 -o "${OBJDIR}/launchd_all_stubs.o" \
        "${SCRIPT_DIR}/launchd_all_stubs.c"

    # Step 3: Compile launchd support sources
    echo "  [CC] ipc.c, log.c, kill2.c, ktrace.c (shim)"
    $CC $SHIM_CFLAGS -c -o "${OBJDIR}/src_ipc.o" "${LAUNCHD_SRC}/ipc.c"
    $CC $SHIM_CFLAGS -c -o "${OBJDIR}/src_log.o" "${LAUNCHD_SRC}/log.c"
    $CC $SHIM_CFLAGS -c -o "${OBJDIR}/src_kill2.o" "${LAUNCHD_SRC}/kill2.c"
    $CC $SHIM_CFLAGS -c -o "${OBJDIR}/src_ktrace.o" "${LAUNCHD_SRC}/ktrace.c"

    # Step 4: Compile liblaunch sources
    echo "  [CC] libbootstrap.c, liblaunch.c, libvproc.c (shim)"
    $CC $SHIM_CFLAGS -c -o "${OBJDIR}/liblaunch_libbootstrap.o" "${LIBLAUNCH_SRC}/libbootstrap.c"
    $CC $SHIM_CFLAGS -c -o "${OBJDIR}/liblaunch_liblaunch.o" "${LIBLAUNCH_SRC}/liblaunch.c"
    $CC $SHIM_CFLAGS -c -o "${OBJDIR}/liblaunch_libvproc.o" "${LIBLAUNCH_SRC}/libvproc.c"

    # Step 5: Compile MIG generated stubs
    echo "  [CC] MIG generated stubs (shim)"
    for mig_name in excServer excUser helperServer helperUser internalServer internalUser \
                    job_forwardUser job_replyUser jobServer jobUser \
                    mach_excServer mach_excUser notifyServer notifyUser; do
        $CC $SHIM_CFLAGS -c -o "${OBJDIR}/mig_gen_${mig_name}.o" "${MIG_GEN_SRC}/${mig_name}.c"
    done

    # Step 6: Compile panthera_fork.s
    echo "  [AS] panthera_fork.s"
    $CC -target $TARGET -mmacosx-version-min="${DEPLOYMENT_TARGET}" -isysroot "$SDK" \
        -c -o "${OBJDIR}/panthera_fork.o" \
        "${SCRIPT_DIR}/panthera_fork.s"

    # Step 7: Link everything
    echo "  [LD] launchd (shim)"
    OBJECTS=(
        "${OBJDIR}/src_launchd.o"
        "${OBJDIR}/launchd_all_stubs.o"
        "${OBJDIR}/src_ipc.o"
        "${OBJDIR}/src_log.o"
        "${OBJDIR}/src_kill2.o"
        "${OBJDIR}/src_ktrace.o"
        "${OBJDIR}/liblaunch_libbootstrap.o"
        "${OBJDIR}/liblaunch_liblaunch.o"
        "${OBJDIR}/liblaunch_libvproc.o"
        "${OBJDIR}/mig_gen_excServer.o"
        "${OBJDIR}/mig_gen_excUser.o"
        "${OBJDIR}/mig_gen_helperServer.o"
        "${OBJDIR}/mig_gen_helperUser.o"
        "${OBJDIR}/mig_gen_internalServer.o"
        "${OBJDIR}/mig_gen_internalUser.o"
        "${OBJDIR}/mig_gen_job_forwardUser.o"
        "${OBJDIR}/mig_gen_job_replyUser.o"
        "${OBJDIR}/mig_gen_jobServer.o"
        "${OBJDIR}/mig_gen_jobUser.o"
        "${OBJDIR}/mig_gen_mach_excServer.o"
        "${OBJDIR}/mig_gen_mach_excUser.o"
        "${OBJDIR}/mig_gen_notifyServer.o"
        "${OBJDIR}/mig_gen_notifyUser.o"
        "${OBJDIR}/panthera_fork.o"
    )
fi

LD_NAMESPACE_FLAGS=()
if [ "$PANTHERA_LAUNCHD_FLAT_NAMESPACE" = "1" ]; then
    LD_NAMESPACE_FLAGS=(-flat_namespace)
fi

$LD -arch x86_64 \
    -platform_version macos "${DEPLOYMENT_TARGET}" 14.0.0 \
    -syslibroot "${SYSROOT}" \
    "${LD_NAMESPACE_FLAGS[@]}" \
    -o "${OUT_BIN}" \
    "${OBJECTS[@]}" \
    "${LIBBSM}" \
    "${LIBSYSTEM}"

echo ""
echo "=== Verification ==="
file "${OUT_BIN}"
ls -la "${OUT_BIN}"

echo ""
echo "Exported symbols: $(nm -gU "${OUT_BIN}" 2>/dev/null | wc -l | tr -d ' ')"

echo ""
echo "Checking undefined symbols against sysroot..."

# Build combined export table from all dylibs (fast, do it once)
EXPORT_TABLE=$(mktemp)
trap "rm -f '$EXPORT_TABLE'" EXIT
for lib in "${SYSROOT}"/usr/lib/system/*.dylib "${SYSROOT}"/usr/lib/*.dylib; do
    [ -f "$lib" ] || continue
    nm -gU "$lib" 2>/dev/null | awk '{print $NF}' >> "$EXPORT_TABLE"
done
sort -u "$EXPORT_TABLE" -o "$EXPORT_TABLE"
echo "  Sysroot exports: $(wc -l < "$EXPORT_TABLE" | tr -d ' ') symbols"

# Check each undefined symbol against the table
nm -gu "${OUT_BIN}" 2>/dev/null | awk '{print $NF}' | while IFS= read -r sym; do
    [ -z "$sym" ] && continue
    if ! grep -qFx "$sym" "$EXPORT_TABLE"; then
        echo "  UNRESOLVED: $sym"
    fi
done > /tmp/panthera_unresolved.txt 2>&1

if [ -s /tmp/panthera_unresolved.txt ]; then
    cat /tmp/panthera_unresolved.txt
    MISSING=$(grep -c UNRESOLVED /tmp/panthera_unresolved.txt || true)
    echo "  WARNING: $MISSING unresolved symbol(s)"
    echo "  launchd may crash at runtime due to missing symbols."
    rm -f /tmp/panthera_unresolved.txt
    exit 1
else
    echo "  All symbols resolved against sysroot."
    rm -f /tmp/panthera_unresolved.txt
fi

echo ""
echo "=== Build complete: ${OUT_BIN} ==="
