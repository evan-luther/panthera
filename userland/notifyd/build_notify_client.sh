#!/bin/bash
# Build libsystem_notify.dylib client library for Panthera Darwin.
# Independent constituent builder that does not require preexisting libSystem.B.
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${PANTHERA_ROOT}/src/Libnotify-317"
BUILDDIR="${PANTHERA_ROOT}/userland/notifyd"
SYSROOT="${SYSROOT:-${PANTHERA_ROOT}/userland/libsystem/build/sysroot}"
OBJDIR="${OBJDIR:-${BUILDDIR}/obj}"
LIBDIR="${LIBDIR:-${BUILDDIR}/lib}"
MIGDIR="${MIGDIR:-${BUILDDIR}/mig_gen}"
SHIMDIR="${SHIMDIR:-${BUILDDIR}/shims}"
CANONICAL_NOTIFY_LIB="${SYSROOT}/usr/lib/system/libsystem_notify.dylib"

SDKROOT="${SDKROOT:-$(xcrun -sdk macosx --show-sdk-path 2>/dev/null || xcrun --show-sdk-path 2>/dev/null || true)}"
CC="${CC:-$(xcrun -sdk macosx -find clang 2>/dev/null || xcrun -find clang 2>/dev/null || which clang)}"
LD="${LD:-$(xcrun -sdk macosx -find ld 2>/dev/null || xcrun -find ld 2>/dev/null || which ld)}"
MIG="${MIG:-$(xcrun -sdk macosx -find mig 2>/dev/null || xcrun -find mig 2>/dev/null || which mig)}"
TARGET="${TARGET:-x86_64-apple-darwin23.0}"
MINVER="${MINVER:-14.0}"
LIBSYSTEM_BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
LIBSYSTEM_SOURCESDIR="${LIBSYSTEM_BUILDDIR}/sources"
LIBSYSTEM_OBJDIR="${LIBSYSTEM_BUILDDIR}/obj"
DYLD_STUB="${LIBSYSTEM_OBJDIR}/dyld_stub_binder.o"

# Required source inputs
required_sources=(
    "${SRC}/notify_client.c"
    "${SRC}/libnotify.c"
    "${SRC}/table.c"
    "${SRC}/notify_ipc.defs"
    "${SRC}/notify_old_ipc.defs"
    "${BUILDDIR}/panthera_notifyd_compat.c"
    "${SHIMDIR}/panthera_fileport.h"
)

for src_path in "${required_sources[@]}"; do
    if [[ ! -f "${src_path}" ]]; then
        echo "Error: missing required source: ${src_path}" >&2
        exit 1
    fi
done

# Ensure stub binder object exists if needed
if [[ ! -f "${DYLD_STUB}" ]]; then
    if [[ -x "${LIBSYSTEM_BUILDDIR}/build_stub_binder.sh" ]]; then
        "${LIBSYSTEM_BUILDDIR}/build_stub_binder.sh"
    elif [[ -f "${LIBSYSTEM_SOURCESDIR}/dyld_stub_binder.s" ]]; then
        mkdir -p "${LIBSYSTEM_OBJDIR}"
        "$CC" -target "${TARGET}" -mmacosx-version-min="${MINVER}" -c "${LIBSYSTEM_SOURCESDIR}/dyld_stub_binder.s" -o "${DYLD_STUB}"
    fi
fi

mkdir -p "${OBJDIR}/client" "${LIBDIR}" "${MIGDIR}" "$(dirname "${CANONICAL_NOTIFY_LIB}")"

echo "=== Generating MIG client stubs ==="
"$MIG" -arch x86_64 -isysroot "$SDKROOT" \
    -I"$SRC" \
    -header "${MIGDIR}/notify_ipc.h" \
    -user "${MIGDIR}/notify_ipcUser.c" \
    -sheader "${MIGDIR}/notify_ipcServer.h" \
    -server "${MIGDIR}/notify_ipcServer.c" \
    "${SRC}/notify_ipc.defs"

"$MIG" -arch x86_64 -isysroot "$SDKROOT" \
    -I"$SRC" \
    -header "${MIGDIR}/notify_old_ipc.h" \
    -user "${MIGDIR}/notify_old_ipcUser.c" \
    -sheader "${MIGDIR}/notify_old_ipcServer.h" \
    -server "${MIGDIR}/notify_old_ipcServer.c" \
    "${SRC}/notify_old_ipc.defs"

CFLAGS_COMMON=(
    -target "$TARGET"
    -mmacosx-version-min="${MINVER}"
    -isysroot "$SDKROOT"
    -O2
    -fPIC
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

echo "=== Building libsystem_notify.dylib (client library) ==="

echo "  CC panthera_notifyd_compat.c (client)"
"$CC" "${CFLAGS_COMMON[@]}" \
    -c "${BUILDDIR}/panthera_notifyd_compat.c" \
    -o "${OBJDIR}/panthera_compat_client.o"

for src_file in notify_client.c libnotify.c table.c; do
    obj_name="${src_file%.c}.o"
    echo "  CC ${src_file}"
    "$CC" "${CFLAGS_COMMON[@]}" \
        -c "${SRC}/${src_file}" \
        -o "${OBJDIR}/client/${obj_name}"
done

echo "  CC notify_ipcUser.c"
"$CC" "${CFLAGS_COMMON[@]}" \
    -Wno-unused-variable \
    -c "${MIGDIR}/notify_ipcUser.c" \
    -o "${OBJDIR}/client/notify_ipcUser.o"

echo "  CC notify_old_ipcUser.c"
"$CC" "${CFLAGS_COMMON[@]}" \
    -Wno-unused-variable \
    -c "${MIGDIR}/notify_old_ipcUser.c" \
    -o "${OBJDIR}/client/notify_old_ipcUser.o"

echo "  LINK libsystem_notify.dylib"
LINK_OBJECTS=(
    "${OBJDIR}/client/notify_client.o"
    "${OBJDIR}/client/libnotify.o"
    "${OBJDIR}/client/table.o"
    "${OBJDIR}/client/notify_ipcUser.o"
    "${OBJDIR}/client/notify_old_ipcUser.o"
    "${OBJDIR}/panthera_compat_client.o"
)
if [[ -f "${DYLD_STUB}" ]]; then
    LINK_OBJECTS+=("${DYLD_STUB}")
fi

"$LD" -arch x86_64 -dylib \
    -install_name /usr/lib/system/libsystem_notify.dylib \
    -compatibility_version 1.0.0 \
    -current_version 1.0.0 \
    -unexported_symbol _os_unfair_lock_assert_owner \
    -unexported_symbol _os_unfair_lock_assert_not_owner \
    -undefined dynamic_lookup \
    -not_for_dyld_shared_cache \
    -platform_version macos "${MINVER}" 26.0.0 \
    -o "${LIBDIR}/libsystem_notify.dylib" \
    "${LINK_OBJECTS[@]}"

echo "  STAGE libsystem_notify.dylib -> ${CANONICAL_NOTIFY_LIB}"
install -m 755 "${LIBDIR}/libsystem_notify.dylib" "${CANONICAL_NOTIFY_LIB}"

echo "=== libsystem_notify.dylib build complete ==="
echo "  ${CANONICAL_NOTIFY_LIB}"
