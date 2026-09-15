#!/bin/bash
set -euo pipefail

PANTHERA_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILDDIR="${PANTHERA_ROOT}/userland/libsystem/build"
OBJDIR="${BUILDDIR}/obj"
SOURCESDIR="${BUILDDIR}/sources"
SYSROOT="${BUILDDIR}/sysroot"
SYSDIR="${SYSROOT}/usr/lib/system"
LIBDIR="${SYSROOT}/usr/lib"
SRCROOT="${PANTHERA_ROOT}/src"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
CC="$(xcrun -sdk macosx -find clang)"
TARGET="x86_64-apple-darwin23.0"
LIBBSM_ROOT="${PANTHERA_ROOT}/userland/libbsm"
LIBBSM_OUT="${LIBBSM_ROOT}/libbsm.0.dylib"

mkdir -p "${OBJDIR}/libsystem_info" "${OBJDIR}/libsystem_kernel" "${OBJDIR}/libsystem_platform"

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

compile() {
	local src="$1"
	local out="$2"
	shift 2
	"${CC}" "${CFLAGS[@]}" "$@" -c "${src}" -o "${out}"
}

rebuild_dyld_stub_binder() {
	local src="${SOURCESDIR}/dyld_stub_binder.s"
	local out="${OBJDIR}/dyld_stub_binder.o"

	if [[ ! -f "${src}" ]]; then
		echo "missing required input: ${src}" >&2
		exit 1
	fi

	"${CC}" -target "${TARGET}" -mmacosx-version-min=14.0 \
		-c "${src}" -o "${out}"
}

link_dylib() {
	local out="$1"
	shift
	xcrun ld -arch x86_64 -dylib \
		-install_name "${out#${SYSROOT}}" \
		-o "${out}" \
		"$@" "${OBJDIR}/dyld_stub_binder.o" \
		-not_for_dyld_shared_cache \
		-undefined dynamic_lookup \
		-platform_version macos 14.0.0 26.0.0
}

link_dylib_unexported() {
	local out="$1"
	local unexported_symbol="$2"
	shift 2
	xcrun ld -arch x86_64 -dylib \
		-install_name "${out#${SYSROOT}}" \
		-o "${out}" \
		"$@" "${OBJDIR}/dyld_stub_binder.o" \
		-not_for_dyld_shared_cache \
		-undefined dynamic_lookup \
		-unexported_symbol "${unexported_symbol}" \
		-platform_version macos 14.0.0 26.0.0
}

ensure_phase5_base() {
	local dylib="$1"
	local backup="$2"
	local backup_id="$3"

	if [[ ! -f "${backup}" ]]; then
		cp "${dylib}" "${backup}"
		install_name_tool -id "${backup_id}" "${backup}"
	fi
}

augment_dylib() {
	local out="$1"
	local backup="$2"
	local backup_id="$3"
	local unexported_symbol="${4:-}"
	shift 4
	local objs=("$@")

	ensure_phase5_base "${out}" "${backup}" "${backup_id}"

	local ld_args=(
		-arch x86_64
		-dylib
		-install_name "${out#${SYSROOT}}"
		-o "${out}"
	)

	if [[ -n "${unexported_symbol}" ]]; then
		ld_args+=(-unexported_symbol "${unexported_symbol}")
	fi

	xcrun ld "${ld_args[@]}" \
		"${objs[@]}" "${OBJDIR}/dyld_stub_binder.o" \
		-reexport_library "${backup}" \
		-not_for_dyld_shared_cache \
		-undefined dynamic_lookup \
		-platform_version macos 14.0.0 26.0.0
}

echo ">>> Layer 5: libxpc"
rebuild_dyld_stub_binder
compile "${OBJDIR}/libxpc_impl.c" "${OBJDIR}/libxpc_impl.o"
link_dylib "${SYSDIR}/libxpc.dylib" "${OBJDIR}/libxpc_impl.o"

echo ">>> Layer 6: libsystem_info"
LIBINFO_SRC="${SRCROOT}/Libinfo-583.0.1"
LIBINFO_INC=(
	-I"${LIBINFO_SRC}/lookup.subproj"
	-I"${LIBINFO_SRC}/gen.subproj"
	-I"${LIBINFO_SRC}/Libinfo"
	-I"${SYSROOT}/usr/include"
	-I"${BUILDDIR}/compat_include"
	-DXPC_EXPORT=
	-DXPC_WARN_RESULT=
	-DXPC_NONNULL1=
	-DXPC_NONNULL2=
	-DXPC_NONNULL3=
	-DXPC_NONNULL4=
	-DXPC_NONNULL5=
)

compile "${LIBINFO_SRC}/lookup.subproj/cache_module.c" "${OBJDIR}/libsystem_info/cache_module.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/file_module.c" "${OBJDIR}/libsystem_info/file_module.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/si_module.c" "${OBJDIR}/libsystem_info/si_module.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/si_data.c" "${OBJDIR}/libsystem_info/si_data.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/libinfo.c" "${OBJDIR}/libsystem_info/libinfo.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/si_getaddrinfo.c" "${OBJDIR}/libsystem_info/si_getaddrinfo.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/thread_data.c" "${OBJDIR}/libsystem_info/thread_data.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/kvbuf.c" "${OBJDIR}/libsystem_info/kvbuf.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/search_module.c" "${OBJDIR}/libsystem_info/search_module.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/ils.c" "${OBJDIR}/libsystem_info/ils.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/lookup.subproj/si_compare.c" "${OBJDIR}/libsystem_info/si_compare.o" "${LIBINFO_INC[@]}"
compile "${LIBINFO_SRC}/gen.subproj/getifaddrs.c" "${OBJDIR}/libsystem_info/getifaddrs.o" "${LIBINFO_INC[@]}"
compile "${OBJDIR}/libinfo_module_stubs.c" "${OBJDIR}/libsystem_info/libinfo_module_stubs.o" "${LIBINFO_INC[@]}"

link_dylib_unexported "${SYSDIR}/libsystem_info.dylib" _bootstrap_port \
	"${OBJDIR}/libsystem_info/cache_module.o" \
	"${OBJDIR}/libsystem_info/file_module.o" \
	"${OBJDIR}/libsystem_info/si_module.o" \
	"${OBJDIR}/libsystem_info/si_data.o" \
	"${OBJDIR}/libsystem_info/libinfo.o" \
	"${OBJDIR}/libsystem_info/si_getaddrinfo.o" \
	"${OBJDIR}/libsystem_info/thread_data.o" \
	"${OBJDIR}/libsystem_info/kvbuf.o" \
	"${OBJDIR}/libsystem_info/search_module.o" \
	"${OBJDIR}/libsystem_info/ils.o" \
	"${OBJDIR}/libsystem_info/si_compare.o" \
	"${OBJDIR}/libsystem_info/getifaddrs.o" \
	"${OBJDIR}/libsystem_info/libinfo_module_stubs.o"

echo ">>> Phase 5 owner rebuild: libbsm"
compile "${LIBBSM_ROOT}/libbsm_stub.c" "${OBJDIR}/libbsm_stub.o" -I"${SYSROOT}/usr/include"
xcrun ld -arch x86_64 -dylib \
	-syslibroot "${SYSROOT}" \
	-install_name /usr/lib/libbsm.0.dylib \
	-o "${LIBBSM_OUT}" \
	"${OBJDIR}/libbsm_stub.o" "${OBJDIR}/dyld_stub_binder.o" "${LIBDIR}/libSystem.B.dylib" \
	-not_for_dyld_shared_cache \
	-undefined dynamic_lookup \
	-platform_version macos 14.0.0 26.0.0
	cp "${LIBBSM_OUT}" "${LIBDIR}/libbsm.0.dylib"

echo ">>> Phase 5 owner rebuild: libsystem_kernel"
PHASE5_XNU_CFLAGS=(
	-include "${BUILDDIR}/shims/libsystem_c/panthera_libc_compat.h"
	-I"${BUILDDIR}/shims/libsystem_c"
	-I"${SRCROOT}/Libc-1583.40.7/include"
	-I"${SRCROOT}/Libc-1583.40.7/gen"
	-I"${SRCROOT}/Libc-1583.40.7/gen/FreeBSD"
	-I"${SRCROOT}/Libc-1583.40.7/string"
	-I"${SRCROOT}/Libc-1583.40.7/string/FreeBSD"
	-D_LIBC_NO_FEATURE_VERIFICATION=1
	-D_FORTIFY_SOURCE=0
	-I"${SYSROOT}/usr/include"
	-I"${SRCROOT}/xnu-10002.41.9/libsyscall/wrappers"
	-I"${SRCROOT}/xnu-10002.41.9/libsyscall/wrappers/libproc"
	-I"${SRCROOT}/xnu-10002.41.9/libsyscall/wrappers/spawn"
	-I"${SRCROOT}/xnu-10002.41.9/osfmk"
	-I"${SRCROOT}/xnu-10002.41.9/EXTERNAL_HEADERS"
	-I"${SRCROOT}/xnu-10002.41.9/bsd"
	-I"${SRCROOT}/xnu-10002.41.9/libkern"
)

cat > "${OBJDIR}/panthera_kernel_phase5_mach_globals.c" <<'EOF'
/* Provide the canonical Mach task-self data symbol from libsystem_kernel. */
unsigned int mach_task_self_ = 0;
EOF

cat > "${OBJDIR}/panthera_kernel_phase5_ndr.c" <<'EOF'
#include <mach/ndr.h>

NDR_record_t NDR_record = {
    0,
    0,
    0,
    NDR_PROTOCOL_2_0,
    NDR_INT_LITTLE_ENDIAN,
    NDR_CHAR_ASCII,
    NDR_FLOAT_IEEE,
    0,
};
EOF

compile "${SRCROOT}/xnu-10002.41.9/libsyscall/wrappers/libproc/libproc.c" \
	"${OBJDIR}/libsystem_kernel/panthera_phase5_libproc.o" "${PHASE5_XNU_CFLAGS[@]}"
compile "${SRCROOT}/xnu-10002.41.9/libsyscall/wrappers/spawn/posix_spawn.c" \
	"${OBJDIR}/libsystem_kernel/panthera_phase5_spawn.o" "${PHASE5_XNU_CFLAGS[@]}"
compile "${SRCROOT}/xnu-10002.41.9/libsyscall/wrappers/getiopolicy_np.c" \
	"${OBJDIR}/libsystem_kernel/panthera_phase5_getiopolicy.o" "${PHASE5_XNU_CFLAGS[@]}"
compile "${OBJDIR}/panthera_kernel_phase5_syscalls.c" \
	"${OBJDIR}/libsystem_kernel/panthera_phase5_syscalls.o" -I"${SYSROOT}/usr/include"
cat > "${OBJDIR}/panthera_kernel_sigprocmask.c" <<'EOF'
#include <signal.h>

extern int __pthread_sigmask(int, const sigset_t *, sigset_t *);

int
sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
	return __pthread_sigmask(how, set, oset);
}
EOF
rm -f "${OBJDIR}/libsystem_kernel/_sigprocmask.o"
compile "${OBJDIR}/panthera_kernel_sigprocmask.c" \
	"${OBJDIR}/libsystem_kernel/panthera_kernel_sigprocmask.o" -I"${SYSROOT}/usr/include"
cat > "${OBJDIR}/panthera_kernel_unix2003_aliases.s" <<'EOF'
/* POSIX/UNIX2003 syscall names are owned by libsystem_kernel. */
.text
.globl _accept$UNIX2003
.set _accept$UNIX2003, ___accept
.globl _bind$UNIX2003
.set _bind$UNIX2003, ___bind
.globl _listen$UNIX2003
.set _listen$UNIX2003, ___listen
.globl _getpeername$UNIX2003
.set _getpeername$UNIX2003, ___getpeername
.globl _getsockname$UNIX2003
.set _getsockname$UNIX2003, ___getsockname
.globl _recvfrom$UNIX2003
.set _recvfrom$UNIX2003, ___recvfrom
.globl _recvmsg$UNIX2003
.set _recvmsg$UNIX2003, ___recvmsg
.globl _sendto$UNIX2003
.set _sendto$UNIX2003, ___sendto
.globl _sendmsg$UNIX2003
.set _sendmsg$UNIX2003, ___sendmsg
.globl _fsync$UNIX2003
.set _fsync$UNIX2003, _fsync
.globl _read$UNIX2003
.set _read$UNIX2003, _read
.globl _write$UNIX2003
.set _write$UNIX2003, _write
.globl _pread$UNIX2003
.set _pread$UNIX2003, _pread
.globl _pwrite$UNIX2003
.set _pwrite$UNIX2003, _pwrite

/* pselect is generated as the private syscall veneer ___pselect.  The
 * public Darwin header maps most LP64 userland callers to _pselect$1050,
 * so libsystem_kernel must export the public aliases too. */
.globl _pselect
.set _pselect, ___pselect
.globl _pselect$1050
.set _pselect$1050, ___pselect
.globl _pselect$DARWIN_EXTSN
.set _pselect$DARWIN_EXTSN, ___pselect
.globl _pselect$NOCANCEL
.set _pselect$NOCANCEL, ___pselect_nocancel
.globl _pselect$1050$NOCANCEL
.set _pselect$1050$NOCANCEL, ___pselect_nocancel
.globl _pselect$DARWIN_EXTSN$NOCANCEL
.set _pselect$DARWIN_EXTSN$NOCANCEL, ___pselect_nocancel
EOF
"${CC}" -target "${TARGET}" -mmacosx-version-min=14.0 \
	-c "${OBJDIR}/panthera_kernel_unix2003_aliases.s" \
	-o "${OBJDIR}/libsystem_kernel/panthera_kernel_unix2003_aliases.o"
compile "${OBJDIR}/panthera_kernel_phase5_voucher.c" \
	"${OBJDIR}/libsystem_kernel/panthera_phase5_voucher.o" \
	-I"${SYSROOT}/usr/include" \
	-I"${SRCROOT}/xnu-10002.41.9/osfmk" \
	-I"${SRCROOT}/xnu-10002.41.9/EXTERNAL_HEADERS" \
	-I"${SRCROOT}/xnu-10002.41.9/bsd"
compile "${OBJDIR}/panthera_kernel_phase5_mach_globals.c" \
	"${OBJDIR}/libsystem_kernel/panthera_phase5_mach_globals.o"
compile "${OBJDIR}/panthera_kernel_phase5_ndr.c" \
	"${OBJDIR}/libsystem_kernel/panthera_phase5_ndr.o" \
	-I"${SYSROOT}/usr/include"

# mach_init.o supersedes the Phase 5 shims for _NDR_record and _mach_task_self_
rm -f "${OBJDIR}/libsystem_kernel/panthera_phase5_ndr.o"
rm -f "${OBJDIR}/libsystem_kernel/panthera_phase5_mach_globals.o"

KERNEL_OBJECTS=(
	"${OBJDIR}/libsystem_kernel/"*.o
	"${OBJDIR}/mach_layer3/host_privUser.o"
	"${OBJDIR}/mach_layer3/mach_absolute_time.o"
	"${OBJDIR}/mach_layer3/mach_error_string.o"
	"${OBJDIR}/mach_layer3/mach_hostUser.o"
	"${OBJDIR}/mach_layer3/mach_timebase_info.o"
	"${OBJDIR}/mach_layer3/mach_msg.o"
	"${OBJDIR}/mach_layer3/mach_port.o"
	"${OBJDIR}/mach_layer3/mach_legacy.o"
	"${OBJDIR}/mach_layer3/mach_traps.o"
	"${OBJDIR}/mach_layer3/mach_vm.o"
	"${OBJDIR}/mach_layer3/mig_allocate.o"
	"${OBJDIR}/mach_layer3/mig_deallocate.o"
	"${OBJDIR}/mach_layer3/mig_reply_port.o"
	"${OBJDIR}/mach_layer3/mig_strncpy.o"
	"${OBJDIR}/mach_layer3/taskUser.o"
	"${OBJDIR}/mach_layer3/thread_actUser.o"
	"${OBJDIR}/mach_layer3/mach_init.o"
)

xcrun ld -arch x86_64 -dylib \
	-install_name /usr/lib/system/libsystem_kernel.dylib \
	-o "${SYSDIR}/libsystem_kernel.dylib" \
	"${KERNEL_OBJECTS[@]}" "${OBJDIR}/dyld_stub_binder.o" \
	-unexported_symbol _execve \
	-not_for_dyld_shared_cache \
	-undefined dynamic_lookup \
	-platform_version macos 14.0.0 26.0.0

echo ">>> Phase 5 owner rebuild: libsystem_platform"
compile "${OBJDIR}/panthera_platform_phase5.c" \
	"${OBJDIR}/libsystem_platform/panthera_phase5_platform.o"

PLATFORM_OBJECTS=(
	"${OBJDIR}/libsystem_platform/"*.o
)

link_dylib "${SYSDIR}/libsystem_platform.dylib" "${PLATFORM_OBJECTS[@]}"

rm -f "${OBJDIR}/panthera_layer7.o" "${SYSDIR}/libpanthera_launchd.dylib"

"${BUILDDIR}/relink_libSystem.sh"

echo ">>> Layer 5-7 build complete"
