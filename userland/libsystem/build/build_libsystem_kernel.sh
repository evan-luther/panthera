#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj"
SOURCESDIR="${BUILDDIR}/sources"
SYSROOT="${BUILDDIR}/sysroot"
SYSDIR="${SYSROOT}/usr/lib/system"
SRCDIR="${PANTHERA_ROOT}/src"
XNUSRC="${SRCDIR}/xnu-10002.41.9"
TARGET="x86_64-apple-darwin23.0"

CC="$(xcrun -sdk macosx -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
MIG="$(xcrun -sdk macosx -find mig)"

# Verify required tracked source inputs exist
REQUIRED_SOURCES=(
    "${SOURCESDIR}/dyld_stub_binder.s"
    "${SOURCESDIR}/panthera_kernel_phase5_syscalls.c"
    "${SOURCESDIR}/panthera_kernel_phase5_voucher.c"
    "${SOURCESDIR}/panthera_kernel_sigprocmask.c"
    "${SOURCESDIR}/panthera_kernel_unix2003_aliases.s"
    "${XNUSRC}/libsyscall/xcodescripts/create-syscalls.pl"
    "${XNUSRC}/bsd/kern/syscalls.master"
    "${XNUSRC}/libsyscall/custom/SYS.h"
    "${XNUSRC}/libsyscall/mach/mach_msg.c"
    "${XNUSRC}/libsyscall/mach/mach_port.c"
    "${XNUSRC}/libsyscall/mach/mach_vm.c"
    "${XNUSRC}/libsyscall/mach/mach_init.c"
    "${XNUSRC}/libsyscall/mach/mach_error_string.c"
    "${XNUSRC}/libsyscall/mach/mach_legacy.c"
    "${XNUSRC}/libsyscall/mach/mach_traps.s"
    "${XNUSRC}/libsyscall/mach/mig_allocate.c"
    "${XNUSRC}/libsyscall/mach/mig_deallocate.c"
    "${XNUSRC}/libsyscall/mach/mig_reply_port.c"
    "${XNUSRC}/libsyscall/mach/mig_strncpy.c"
    "${XNUSRC}/libsyscall/wrappers/mach_absolute_time.s"
    "${XNUSRC}/libsyscall/wrappers/mach_timebase_info.c"
    "${XNUSRC}/libsyscall/wrappers/libproc/libproc.c"
    "${XNUSRC}/libsyscall/wrappers/spawn/posix_spawn.c"
    "${XNUSRC}/libsyscall/wrappers/getiopolicy_np.c"
    "${XNUSRC}/libsyscall/os/alloc_once.c"
)

for src in "${REQUIRED_SOURCES[@]}"; do
    if [[ ! -f "${src}" ]]; then
        echo "missing required source input: ${src}" >&2
        exit 1
    fi
done

# Output directories
SYSCALL_DIR="${OBJDIR}/libsystem_kernel_syscalls"
MIG_HDR_DIR="${OBJDIR}/libsystem_kernel_include/mach"
MIG_INT_HDR_DIR="${OBJDIR}/libsystem_kernel_include_int/mach"
MIG_DIR="${OBJDIR}/libsystem_kernel_mig"
MIG_INT_DIR="${OBJDIR}/libsystem_kernel_mig_int"
KERNEL_OBJ_DIR="${OBJDIR}/libsystem_kernel"

mkdir -p "${SYSDIR}" "${SYSCALL_DIR}" "${MIG_HDR_DIR}" "${MIG_INT_HDR_DIR}" \
    "${MIG_DIR}" "${MIG_INT_DIR}" "${KERNEL_OBJ_DIR}"

# Build dyld_stub_binder.o idempotently
"${BUILDDIR}/build_stub_binder.sh"

# Phase 1: Generate BSD syscall stubs using create-syscalls.pl
ARCHS="x86_64" perl "${XNUSRC}/libsyscall/xcodescripts/create-syscalls.pl" \
    "${XNUSRC}/bsd/kern/syscalls.master" \
    "${XNUSRC}/libsyscall/custom" \
    "${XNUSRC}/libsyscall/Platforms" \
    "MacOSX" \
    "${SYSCALL_DIR}"

# Remove raw _sigprocmask.s (replaced by panthera_kernel_sigprocmask.c)
rm -f "${SYSCALL_DIR}/_sigprocmask.s"

# Remove duplicate _syscall alias from ___syscall.s so panthera_kernel_phase5_syscalls.c provides syscall()
if [[ -f "${SYSCALL_DIR}/___syscall.s" ]]; then
    sed -i '' '/\.set[[:space:]]\{1,\}_syscall,[[:space:]]\{1,\}___syscall/d' "${SYSCALL_DIR}/___syscall.s" 2>/dev/null || \
    sed -i '/\.set[[:space:]]\{1,\}_syscall,[[:space:]]\{1,\}___syscall/d' "${SYSCALL_DIR}/___syscall.s" 2>/dev/null || true
fi

# Phase 2: Generate Mach MIG headers and user stubs
MIG_INCFLAGS=("-I${XNUSRC}/osfmk")

PUBLIC_HDR_DEFS=(
    clock.defs
    clock_priv.defs
    clock_reply.defs
    exc.defs
    host_priv.defs
    host_security.defs
    mach_eventlink.defs
    mach_host.defs
    mach_port.defs
    mach_voucher.defs
    memory_entry.defs
    processor.defs
    processor_set.defs
    task.defs
    thread_act.defs
    vm_map.defs
    mach_vm.defs
)

for mig_def in "${PUBLIC_HDR_DEFS[@]}"; do
    base="${mig_def%.defs}"
    "${MIG}" -novouchers -arch x86_64 -cc "${CC}" \
        -header "${MIG_HDR_DIR}/${base}.h" \
        -user /dev/null -server /dev/null -sheader /dev/null \
        -DLIBSYSCALL_INTERFACE "${MIG_INCFLAGS[@]}" \
        "${XNUSRC}/libsyscall/mach/${mig_def}"
done

INTERNAL_HDR_DEFS=(
    mach_port.defs
    mach_vm.defs
    task.defs
    thread_act.defs
    vm_map.defs
)

for mig_def in "${INTERNAL_HDR_DEFS[@]}"; do
    base="${mig_def%.defs}"
    "${MIG}" -novouchers -arch x86_64 -cc "${CC}" \
        -header "${MIG_INT_HDR_DIR}/${base}_internal.h" \
        -user /dev/null -server /dev/null -sheader /dev/null \
        "${MIG_INCFLAGS[@]}" \
        "${XNUSRC}/libsyscall/mach/${mig_def}"
done

PUBLIC_STUB_DEFS=(
    clock.defs
    clock_priv.defs
    host_priv.defs
    mach_host.defs
    task.defs
    thread_act.defs
)

for mig_def in "${PUBLIC_STUB_DEFS[@]}"; do
    base="${mig_def%.defs}"
    "${MIG}" -novouchers -arch x86_64 -cc "${CC}" \
        -user "${MIG_DIR}/${base}User.c" \
        -header "${MIG_DIR}/${base}.h" \
        -server /dev/null -sheader /dev/null \
        -DLIBSYSCALL_INTERFACE "${MIG_INCFLAGS[@]}" \
        "${XNUSRC}/libsyscall/mach/${mig_def}"
done

INTERNAL_STUB_DEFS=(
    mach_port.defs
    mach_vm.defs
    vm_map.defs
    mach_voucher.defs
)

for mig_def in "${INTERNAL_STUB_DEFS[@]}"; do
    base="${mig_def%.defs}"
    "${MIG}" -novouchers -arch x86_64 -cc "${CC}" \
        -user "${MIG_INT_DIR}/${base}User.c" \
        -header "${MIG_INT_DIR}/${base}.h" \
        -server /dev/null -sheader /dev/null \
        "${MIG_INCFLAGS[@]}" \
        "${XNUSRC}/libsyscall/mach/${mig_def}"
done

# Phase 3: Compile all components
CFLAGS=(
    -target "${TARGET}"
    -mmacosx-version-min=14.0
    -fPIC
    -fblocks
    -O2
    -Wno-everything
    -D__DARWIN_UNIX03=1
    -DPRIVATE=1
    -D__APPLE__=1
    -isysroot "${SDKROOT}"
)

INCLUDES=(
    -I"${BUILDDIR}/compat_include"
    -I"${BUILDDIR}/shims/libsystem_c"
    -I"${SRCDIR}/Libc-1583.40.7/include"
    -I"${SRCDIR}/Libc-1583.40.7/gen"
    -I"${SRCDIR}/Libc-1583.40.7/gen/FreeBSD"
    -I"${SRCDIR}/Libc-1583.40.7/string"
    -I"${SRCDIR}/Libc-1583.40.7/string/FreeBSD"
    -D_LIBC_NO_FEATURE_VERIFICATION=1
    -D_FORTIFY_SOURCE=0
    -I"${SYSROOT}/usr/include"
    -I"${XNUSRC}/libsyscall/wrappers"
    -I"${XNUSRC}/libsyscall/wrappers/libproc"
    -I"${XNUSRC}/libsyscall/wrappers/spawn"
    -I"${XNUSRC}/libsyscall/mach"
    -I"${XNUSRC}/libsyscall"
    -I"${XNUSRC}/libsyscall/os"
    -I"${XNUSRC}/osfmk"
    -I"${XNUSRC}/EXTERNAL_HEADERS"
    -I"${XNUSRC}/bsd"
    -I"${XNUSRC}/libkern"
    -I"${OBJDIR}/libsystem_kernel_include"
    -I"${OBJDIR}/libsystem_kernel_include_int"
)

# Compile BSD syscall .s files
for s_file in "${SYSCALL_DIR}"/*.s; do
    s_base="$(basename "${s_file}" .s)"
    "${CC}" -target "${TARGET}" -mmacosx-version-min=14.0 \
        -fPIC -O2 -DPRIVATE=1 \
        -I"${SYSCALL_DIR}" \
        -I"${XNUSRC}/osfmk" \
        -I"${XNUSRC}/EXTERNAL_HEADERS" \
        -I"${XNUSRC}/bsd" \
        -I"${SYSROOT}/usr/include" \
        -c "${s_file}" \
        -o "${KERNEL_OBJ_DIR}/syscall_${s_base}.o"
done

# Compile MIG user stubs
for c_file in "${MIG_DIR}"/*.c "${MIG_INT_DIR}"/*.c; do
    c_base="$(basename "${c_file}" .c)"
    "${CC}" "${CFLAGS[@]}" "${INCLUDES[@]}" \
        -c "${c_file}" \
        -o "${KERNEL_OBJ_DIR}/mig_${c_base}.o"
done

# Compile Mach and XNU wrapper sources
MACH_SOURCES=(
    "${XNUSRC}/libsyscall/mach/mach_msg.c"
    "${XNUSRC}/libsyscall/mach/mach_port.c"
    "${XNUSRC}/libsyscall/mach/mach_vm.c"
    "${SOURCESDIR}/panthera_mach_init.c"
    "${XNUSRC}/libsyscall/mach/mach_error_string.c"
    "${XNUSRC}/libsyscall/mach/mach_legacy.c"
    "${XNUSRC}/libsyscall/mach/mach_traps.s"
    "${XNUSRC}/libsyscall/mach/mig_allocate.c"
    "${XNUSRC}/libsyscall/mach/mig_deallocate.c"
    "${XNUSRC}/libsyscall/mach/mig_reply_port.c"
    "${XNUSRC}/libsyscall/mach/mig_strncpy.c"
    "${XNUSRC}/libsyscall/wrappers/mach_absolute_time.s"
    "${XNUSRC}/libsyscall/wrappers/mach_timebase_info.c"
    "${XNUSRC}/libsyscall/wrappers/libproc/libproc.c"
    "${XNUSRC}/libsyscall/wrappers/spawn/posix_spawn.c"
    "${XNUSRC}/libsyscall/wrappers/getiopolicy_np.c"
    "${XNUSRC}/libsyscall/os/alloc_once.c"
    "${SOURCESDIR}/panthera_kernel_phase5_syscalls.c"
    "${SOURCESDIR}/panthera_kernel_sigprocmask.c"
    "${SOURCESDIR}/panthera_kernel_unix2003_aliases.s"
    "${SOURCESDIR}/panthera_kernel_phase5_voucher.c"
)

for src_file in "${MACH_SOURCES[@]}"; do
    src_base="$(basename "${src_file}")"
    src_name="${src_base%.*}"
    if [[ "${src_file}" == *.c ]]; then
        "${CC}" "${CFLAGS[@]}" "${INCLUDES[@]}" \
            -include "${BUILDDIR}/compat_include/Availability.h" \
            -include "${SOURCESDIR}/panthera_kernel_mach_globals.h" \
            -c "${src_file}" \
            -o "${KERNEL_OBJ_DIR}/${src_name}.o"
    else
        "${CC}" "${CFLAGS[@]}" "${INCLUDES[@]}" \
            -c "${src_file}" \
            -o "${KERNEL_OBJ_DIR}/${src_name}.o"
    fi
done

# Phase 4: Link libsystem_kernel.dylib
xcrun ld -arch x86_64 -dylib \
    -install_name /usr/lib/system/libsystem_kernel.dylib \
    -o "${SYSDIR}/libsystem_kernel.dylib" \
    "${KERNEL_OBJ_DIR}"/*.o "${OBJDIR}/dyld_stub_binder.o" \
    -unexported_symbol _execve \
    -unexported_symbol _getpid \
    -unexported_symbol _lseek \
    -unexported_symbol _pipe \
    -not_for_dyld_shared_cache \
    -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0
