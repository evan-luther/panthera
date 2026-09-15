/*
 * panthera_symbol_bridge.s — Symbol aliases and trampolines for libsystem_c
 *
 * Provides standard C name aliases for platform-optimized functions,
 * $NOCANCEL/$UNIX2003 variant aliases, and other missing symbol bridges.
 */

.text

/* ═══════════════════════════════════════════════════════════
 * Standard C string/memory function aliases
 * Map _strlen → __platform_strlen etc.
 * These are the PUBLIC C names that libc consumers expect.
 * ═══════════════════════════════════════════════════════════ */

.globl _strlen
_strlen: jmp __platform_strlen

.globl _strcmp
_strcmp: jmp __platform_strcmp

.globl _strcpy
_strcpy: jmp __platform_strcpy

.globl _strncpy
_strncpy: jmp __platform_strncpy

.globl _strncmp
_strncmp: jmp __platform_strncmp

.globl _strchr
_strchr: jmp __platform_strchr

.globl _strstr
_strstr: jmp __platform_strstr

.globl _strlcpy
_strlcpy: jmp __platform_strlcpy

.globl _strlcat
_strlcat: jmp __platform_strlcat

.globl _strnlen
_strnlen: jmp __platform_strnlen

.globl _memcpy
_memcpy: jmp __platform_memmove  /* memcpy = memmove on Darwin (always safe) */

.globl _memmove
_memmove: jmp __platform_memmove

.globl _memset
_memset: jmp __platform_memset

.globl _memcmp
_memcmp: jmp __platform_memcmp

.globl _memchr
_memchr: jmp __platform_memchr

.globl _memset_pattern16
_memset_pattern16: jmp __platform_memset_pattern16

.globl ___bzero
___bzero: jmp __platform_bzero

/* ═══════════════════════════════════════════════════════════
 * Syscall $NOCANCEL and $UNIX2003 variant aliases
 * Map _close$NOCANCEL$UNIX2003 → _close$NOCANCEL etc.
 * ═══════════════════════════════════════════════════════════ */

/* These map the POSIX conformance variant names to the base syscall names.
 * On Panthera we don't have cancellation points yet, so NOCANCEL = regular. */

.globl "_close$NOCANCEL$UNIX2003"
"_close$NOCANCEL$UNIX2003": jmp "_close$NOCANCEL"

.globl "_read$NOCANCEL$UNIX2003"
"_read$NOCANCEL$UNIX2003": jmp "_read$NOCANCEL"

.globl "_write$NOCANCEL$UNIX2003"
"_write$NOCANCEL$UNIX2003": jmp "_write$NOCANCEL"

.globl "_open$NOCANCEL$UNIX2003"
"_open$NOCANCEL$UNIX2003": jmp "_open$NOCANCEL"

.globl "_fcntl$NOCANCEL$UNIX2003"
"_fcntl$NOCANCEL$UNIX2003": jmp "_fcntl$NOCANCEL"

.globl "_pread$NOCANCEL$UNIX2003"
"_pread$NOCANCEL$UNIX2003": jmp "_pread$NOCANCEL"

.globl "_pwrite$NOCANCEL$UNIX2003"
"_pwrite$NOCANCEL$UNIX2003": jmp "_pwrite$NOCANCEL"

.globl "_fsync$NOCANCEL$UNIX2003"
"_fsync$NOCANCEL$UNIX2003": jmp "_fsync$NOCANCEL"

.globl "_usleep$NOCANCEL$UNIX2003"
"_usleep$NOCANCEL$UNIX2003": jmp _usleep

.globl "_sigsuspend$NOCANCEL$UNIX2003"
"_sigsuspend$NOCANCEL$UNIX2003": jmp _sigsuspend

.globl "_writev$NOCANCEL$UNIX2003"
"_writev$NOCANCEL$UNIX2003": jmp _writev

.globl "_mmap$UNIX2003"
"_mmap$UNIX2003": jmp _mmap

.globl "_munmap$UNIX2003"
"_munmap$UNIX2003": jmp _munmap

.globl "_socketpair$UNIX2003"
"_socketpair$UNIX2003": jmp _socketpair

.globl "_setattrlist$UNIX2003"
"_setattrlist$UNIX2003": jmp _setattrlist

.globl "_openat$NOCANCEL"
"_openat$NOCANCEL": jmp _openat

/* $DARWIN_EXTSN variants — provided by panthera_remaining.o stubs */

/* ═══════════════════════════════════════════════════════════
 * Missing syscall aliases (should be in kernel dylib but
 * may use triple-underscore internally)
 * ═══════════════════════════════════════════════════════════ */

.globl _getpid
_getpid: jmp ___getpid

.globl _lseek
_lseek: jmp ___lseek

.globl _ioctl
_ioctl: jmp ___ioctl

.globl _unlink
_unlink: jmp ___unlink

.globl _rmdir
_rmdir: jmp ___rmdir

.globl _pipe
_pipe: jmp ___pipe

.globl _execve
_execve: jmp ___execve

/* ═══════════════════════════════════════════════════════════
 * Syscall public name → kernel triple-underscore aliases
 * The kernel dylib exports ___mmap, we need _mmap
 * ═══════════════════════════════════════════════════════════ */

.globl _mmap
_mmap: jmp ___mmap

.globl _munmap
_munmap: jmp ___munmap

.globl _sigsuspend
_sigsuspend: jmp ___sigsuspend

/* $NOCANCEL variants → kernel ___name_nocancel */
.globl "_fcntl$NOCANCEL"
"_fcntl$NOCANCEL": jmp ___fcntl_nocancel

.globl "_open$NOCANCEL"
"_open$NOCANCEL": jmp ___open_nocancel

.globl "_close$NOCANCEL"
"_close$NOCANCEL": jmp ___close_nocancel

.globl "_read$NOCANCEL"
"_read$NOCANCEL": jmp ___read_nocancel

.globl "_write$NOCANCEL"
"_write$NOCANCEL": jmp ___write_nocancel

.globl "_pread$NOCANCEL"
"_pread$NOCANCEL": jmp ___pread_nocancel

.globl "_pwrite$NOCANCEL"
"_pwrite$NOCANCEL": jmp ___pwrite_nocancel

.globl "_fsync$NOCANCEL"
"_fsync$NOCANCEL": jmp ___fsync_nocancel

.globl "_writev$NOCANCEL"
"_writev$NOCANCEL": jmp ___writev_nocancel

/* ═══════════════════════════════════════════════════════════
 * errno — thread-local error number
 * For single-threaded: just a global variable
 * ═══════════════════════════════════════════════════════════ */
.data
.globl _errno
_errno: .long 0
