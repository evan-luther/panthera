#!/bin/bash
# Build notifyd daemon, libsystem_notify.dylib client library, and notifyutil
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${PANTHERA_ROOT}/src/Libnotify-317"
BUILDDIR="${PANTHERA_ROOT}/userland/notifyd"
OBJDIR="${BUILDDIR}/obj"
BINDIR="${BUILDDIR}/bin"
LIBDIR="${BUILDDIR}/lib"
MIGDIR="${BUILDDIR}/mig_gen"
SHIMDIR="${BUILDDIR}/shims"
SYSROOT="${SYSROOT:-${PANTHERA_ROOT}/userland/libsystem/build/sysroot}"
SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path 2>/dev/null || xcrun --show-sdk-path 2>/dev/null || true)}"
MIG="${MIG:-$(xcrun -sdk macosx -find mig 2>/dev/null || xcrun -find mig 2>/dev/null || which mig)}"
CANONICAL_NOTIFY_LIB="${SYSROOT}/usr/lib/system/libsystem_notify.dylib"
MACH_NOTIFY_DEFS="${MACH_NOTIFY_DEFS:-${SDKROOT}/usr/include/mach/notify.defs}"
LIBSYSTEM_DYLIB="${SYSROOT}/usr/lib/libSystem.B.dylib"

CC="${CC:-$(xcrun -sdk macosx -find clang 2>/dev/null || xcrun -find clang 2>/dev/null || which clang)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"

# Locate Darwin compiler runtime (libclang_rt.osx.a)
CLANG_RT="$("${CC}" -target "${TARGET}" -print-libgcc-file-name 2>/dev/null || true)"
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RT="$("${CC}" -print-file-name=libclang_rt.osx.a 2>/dev/null || true)"
fi
if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  CLANG_RESOURCE_DIR="$("${CC}" -print-resource-dir 2>/dev/null || true)"
  if [ -n "${CLANG_RESOURCE_DIR}" ] && [ -f "${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a" ]; then
    CLANG_RT="${CLANG_RESOURCE_DIR}/lib/darwin/libclang_rt.osx.a"
  fi
fi

if [ -z "${CLANG_RT}" ] || [ ! -f "${CLANG_RT}" ]; then
  echo "ERROR: Unable to locate Darwin compiler runtime archive (libclang_rt.osx.a) using ${CC}" >&2
  exit 1
fi
# Validate pinned source inputs
required_sources=(
    "${SRC}/notifyd/notifyd.c"
    "${SRC}/notifyd/notify_proc.c"
    "${SRC}/notifyd/service.c"
    "${SRC}/notifyd/pathwatch.c"
    "${SRC}/notifyutil/notifyutil.c"
    "${SRC}/libnotify.c"
    "${SRC}/table.c"
    "${BUILDDIR}/panthera_notifyd_compat.c"
)
for src_path in "${required_sources[@]}"; do
    if [[ ! -f "${src_path}" ]]; then
        echo "Error: missing required source: ${src_path}" >&2
        exit 1
    fi
done

if [[ ! -f "${MACH_NOTIFY_DEFS}" ]]; then
    echo "Error: missing required MIG def: ${MACH_NOTIFY_DEFS}" >&2
    exit 1
fi
CFLAGS_COMMON=(
    -target "$TARGET"
    -mmacosx-version-min="${MINVER}"
    -isysroot "$SDKROOT"
    -O2
    -I"${SRC}"
    -I"${MIGDIR}"
    -I"${SHIMDIR}"
    -DPANTHERA=1
    -DTARGET_OS_OSX=1
    -DTARGET_OS_SIMULATOR=0
    -D__BLOCKS__=1
    -Wno-deprecated-declarations
    -Wno-nullability-completeness
    -Wno-availability
    -Wno-format
    -Wno-unused-function
    -Wno-implicit-function-declaration
    -fblocks
    -include "${SHIMDIR}/panthera_fileport.h"
    -include "errno.h"
)

LDFLAGS_COMMON=(
    -target "$TARGET"
    -mmacosx-version-min="${MINVER}"
    -isysroot "$SDKROOT"
    -Wl,-syslibroot,"${SYSROOT}"
    -nodefaultlibs
)

mkdir -p "$OBJDIR/daemon" "$BINDIR" "$LIBDIR" "$(dirname "$CANONICAL_NOTIFY_LIB")"

# Build libsystem_notify.dylib client library via decoupled builder
"${BUILDDIR}/build_notify_client.sh"

if [[ ! -f "${CANONICAL_NOTIFY_LIB}" ]]; then
    echo "Error: missing required canonical notify library: ${CANONICAL_NOTIFY_LIB}" >&2
    exit 1
fi

if [[ ! -f "${LIBSYSTEM_DYLIB}" ]]; then
    echo "Error: missing required libSystem library: ${LIBSYSTEM_DYLIB}" >&2
    exit 1
fi

echo ""
echo "=== Generating daemon MIG stubs ==="
"$MIG" -arch x86_64 -isysroot "$SDKROOT" \
    -header "${MIGDIR}/mach_notify.h" \
    -user "${MIGDIR}/mach_notifyUser.c" \
    -sheader "${MIGDIR}/notifyServer.h" \
    -server "${MIGDIR}/notifyServer.c" \
    "${MACH_NOTIFY_DEFS}"

echo ""
echo "=== Building daemon compat shim ==="
echo "  CC panthera_notifyd_compat.c (daemon)"
"$CC" "${CFLAGS_COMMON[@]}" \
    -DPANTHERA_DAEMON_BUILD=1 \
    -I"${SRC}/notifyd" \
    -c "${BUILDDIR}/panthera_notifyd_compat.c" \
    -o "${OBJDIR}/panthera_compat_daemon.o"
echo ""
echo "=== Building notifyd daemon ==="

# Daemon objects
for src_file in notifyd/notifyd.c notifyd/notify_proc.c notifyd/service.c notifyd/pathwatch.c; do
    obj_name="$(basename "${src_file%.c}").o"
    echo "  CC $src_file"
    "$CC" "${CFLAGS_COMMON[@]}" \
        -I"${SRC}/notifyd" \
        -DDISPATCH_MACH_SPI=1 \
        -Ddispatch_main=panthera_dispatch_main \
        -Dshm_open=panthera_shm_open \
        -DNDEBUG=1 \
        -DPANTHERA_NOOP_DISPATCH_SOURCES=1 \
        -c "${SRC}/${src_file}" \
        -o "${OBJDIR}/daemon/${obj_name}"
done

# Shared source files for daemon
echo "  CC table.c (daemon)"
"$CC" "${CFLAGS_COMMON[@]}" \
    -c "${SRC}/table.c" \
    -o "${OBJDIR}/daemon/table.o"

echo "  CC libnotify.c (daemon)"
"$CC" "${CFLAGS_COMMON[@]}" \
    -c "${SRC}/libnotify.c" \
    -o "${OBJDIR}/daemon/libnotify.o"

# MIG server stubs
echo "  CC notify_ipcServer.c"
"$CC" "${CFLAGS_COMMON[@]}" \
    -Wno-unused-variable \
    -D__notify_server_register_plain_2=panthera_register_plain_2 \
    -D__notify_server_register_check_2=panthera_register_check_2 \
    -c "${MIGDIR}/notify_ipcServer.c" \
    -o "${OBJDIR}/daemon/notify_ipcServer.o"

# Mach notification server stubs (for do_notify_subsystem)
echo "  CC notifyServer.c"
"$CC" "${CFLAGS_COMMON[@]}" \
    -Wno-unused-variable \
    -c "${MIGDIR}/notifyServer.c" \
    -o "${OBJDIR}/daemon/notifyServer.o"

# Link daemon
# Ensure libbsm exists in sysroot or userland
LIBBSM_DYLIB="${SYSROOT}/usr/lib/libbsm.0.dylib"
if [[ ! -f "${LIBBSM_DYLIB}" ]]; then
    LIBBSM_DYLIB="${PANTHERA_ROOT}/userland/libbsm/libbsm.0.dylib"
fi
if [[ ! -f "${LIBBSM_DYLIB}" ]]; then
    if [[ -x "${PANTHERA_ROOT}/userland/libbsm/build_libbsm.sh" ]]; then
        "${PANTHERA_ROOT}/userland/libbsm/build_libbsm.sh"
        if [[ -f "${SYSROOT}/usr/lib/libbsm.0.dylib" ]]; then
            LIBBSM_DYLIB="${SYSROOT}/usr/lib/libbsm.0.dylib"
        elif [[ -f "${PANTHERA_ROOT}/userland/libbsm/libbsm.0.dylib" ]]; then
            LIBBSM_DYLIB="${PANTHERA_ROOT}/userland/libbsm/libbsm.0.dylib"
        fi
    fi
fi
if [[ ! -f "${LIBBSM_DYLIB}" ]]; then
    echo "Error: missing required libbsm library: ${LIBBSM_DYLIB}" >&2
    exit 1
fi

echo "  LINK notifyd"
"$CC" "${LDFLAGS_COMMON[@]}" \
    -o "${BINDIR}/notifyd" \
    "${OBJDIR}/daemon/notifyd.o" \
    "${OBJDIR}/daemon/notify_proc.o" \
    "${OBJDIR}/daemon/service.o" \
    "${OBJDIR}/daemon/pathwatch.o" \
    "${OBJDIR}/daemon/table.o" \
    "${OBJDIR}/daemon/libnotify.o" \
    "${OBJDIR}/daemon/notify_ipcServer.o" \
    "${OBJDIR}/daemon/notifyServer.o" \
    "${OBJDIR}/panthera_compat_daemon.o" \
    "${LIBBSM_DYLIB}" \
    "${LIBSYSTEM_DYLIB}" \
    "${CLANG_RT}"
echo ""
echo "=== Building notifyutil ==="

echo "  CC notifyutil.c"
"$CC" "${CFLAGS_COMMON[@]}" \
    -c "${SRC}/notifyutil/notifyutil.c" \
    -o "${OBJDIR}/notifyutil.o"

echo "  LINK notifyutil"
"$CC" "${LDFLAGS_COMMON[@]}" \
    -o "${BINDIR}/notifyutil" \
    "${OBJDIR}/notifyutil.o" \
    "${CANONICAL_NOTIFY_LIB}" \
    "${LIBSYSTEM_DYLIB}" \
    "${CLANG_RT}"

if [[ -f "${BUILDDIR}/test_notify.c" ]]; then
    echo ""
    echo "=== Building test_notify ==="
    echo "  CC test_notify.c"
    "$CC" "${CFLAGS_COMMON[@]}" \
        -c "${BUILDDIR}/test_notify.c" \
        -o "${OBJDIR}/test_notify.o"

    echo "  LINK test_notify"
    "$CC" "${LDFLAGS_COMMON[@]}" \
        -o "${BINDIR}/test_notify" \
        "${OBJDIR}/test_notify.o" \
        "${CANONICAL_NOTIFY_LIB}" \
        "${LIBSYSTEM_DYLIB}" \
        "${CLANG_RT}"
fi

echo ""
echo "=== Build complete ==="
echo "  ${CANONICAL_NOTIFY_LIB}"
echo "  ${LIBDIR}/libsystem_notify.dylib"
echo "  ${BINDIR}/notifyd"
echo "  ${BINDIR}/notifyutil"

# Show key exports
echo ""
echo "=== Key notify exports ==="
nm -gU "${CANONICAL_NOTIFY_LIB}" | grep -E '_notify_(post|register_check|register_dispatch|register_signal|register_mach_port|register_file_descriptor|check|cancel|peek|suspend|resume|get_state|set_state|is_valid_token)$' | sort
