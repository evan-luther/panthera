/*
 * panthera_resolve_aliases.s — Assembly aliases for symbol name mismatches
 *
 * Many symbols exist in our system dylibs but with different name mangling.
 * For example, libsystem_platform exports __platform_bzero (C: _platform_bzero)
 * but libsystem_c needs ___platform_bzero (C: __platform_bzero).
 * These aliases bridge the gap via trampolines.
 */

.text

/* ═══════════════════════════════════════════════════════════
 * Platform string/memory functions
 * Platform exports: __platform_foo  (C name: _platform_foo)
 * Libc expects:     ___platform_foo (C name: __platform_foo)
 * ═══════════════════════════════════════════════════════════ */

.globl ___platform_bzero
___platform_bzero: jmp __platform_bzero

.globl ___platform_memchr
___platform_memchr: jmp __platform_memchr

.globl ___platform_memcmp
___platform_memcmp: jmp __platform_memcmp

.globl ___platform_memmove
___platform_memmove: jmp __platform_memmove

.globl ___platform_memset
___platform_memset: jmp __platform_memset

.globl ___platform_memset_pattern16
___platform_memset_pattern16: jmp __platform_memset_pattern16

.globl ___platform_strchr
___platform_strchr: jmp __platform_strchr

.globl ___platform_strcmp
___platform_strcmp: jmp __platform_strcmp

.globl ___platform_strcpy
___platform_strcpy: jmp __platform_strcpy

.globl ___platform_strlcat
___platform_strlcat: jmp __platform_strlcat

.globl ___platform_strlcpy
___platform_strlcpy: jmp __platform_strlcpy

.globl ___platform_strlen
___platform_strlen: jmp __platform_strlen

.globl ___platform_strncmp
___platform_strncmp: jmp __platform_strncmp

.globl ___platform_strncpy
___platform_strncpy: jmp __platform_strncpy

.globl ___platform_strnlen
___platform_strnlen: jmp __platform_strnlen

.globl ___platform_strstr
___platform_strstr: jmp __platform_strstr

/* ═══════════════════════════════════════════════════════════
 * Kernel syscall aliases
 * Internal double-underscore variants → public names
 * (For a minimal system, cancellation-point = regular version)
 * ═══════════════════════════════════════════════════════════ */

/* ___getXXX → _getXXX (triple underscore → single underscore) */
.globl ___getegid
___getegid: jmp _getegid

.globl ___geteuid
___geteuid: jmp _geteuid

.globl ___getgid
___getgid: jmp _getgid

.globl ___getppid
___getppid: jmp _getppid

.globl ___getuid
___getuid: jmp _getuid

/* __foo → _foo or ___foo (cancellation-point → base) */
.globl __close
__close: jmp _close

.globl __dup2
__dup2: jmp _dup2

.globl __execve
__execve: jmp ___execve

.globl __fcntl
__fcntl: jmp ___fcntl

.globl __fstat
__fstat: jmp _fstat

.globl __ioctl
__ioctl: jmp ___ioctl

.globl __open
__open: jmp ___open

.globl __openat
__openat: jmp ___openat

.globl __read
__read: jmp _read

.globl __select
__select: jmp ___select

.globl __sigaction
__sigaction: jmp ___sigaction

.globl __sigprocmask
__sigprocmask: jmp _sigprocmask

.globl __wait4
__wait4: jmp ___wait4

.globl __write
__write: jmp _write

.globl __writev
__writev: jmp _writev

/* ═══════════════════════════════════════════════════════════
 * $UNIX2003 conformance aliases → base functions
 * ═══════════════════════════════════════════════════════════ */

.globl _close$UNIX2003
_close$UNIX2003: jmp _close

.globl _connect$UNIX2003
_connect$UNIX2003: jmp ___connect

.globl _fcntl$UNIX2003
_fcntl$UNIX2003: jmp ___fcntl

.globl _getrlimit$UNIX2003
_getrlimit$UNIX2003: jmp ___getrlimit

.globl _kill$UNIX2003
_kill$UNIX2003: jmp ___kill

.globl _open$UNIX2003
_open$UNIX2003: jmp ___open

.globl _pread$UNIX2003
_pread$UNIX2003: jmp _pread

.globl _pwrite$UNIX2003
_pwrite$UNIX2003: jmp _pwrite

.globl _read$UNIX2003
_read$UNIX2003: jmp _read

.globl _setrlimit$UNIX2003
_setrlimit$UNIX2003: jmp ___setrlimit

.globl _sigsuspend$UNIX2003
_sigsuspend$UNIX2003: jmp ___sigsuspend

.globl _writev$UNIX2003
_writev$UNIX2003: jmp _writev

.globl _getattrlist$UNIX2003
_getattrlist$UNIX2003: jmp ___getattrlist

.globl _send$UNIX2003
_send$UNIX2003: jmp ___sendto

.globl _pthread_sigmask$UNIX2003
_pthread_sigmask$UNIX2003: jmp ___pthread_sigmask

/* ═══════════════════════════════════════════════════════════
 * Pthread internal aliases
 * __pthread_foo → _pthread_foo (add one underscore)
 * ═══════════════════════════════════════════════════════════ */

.globl __pthread_getspecific
__pthread_getspecific: jmp _pthread_getspecific

.globl __pthread_setspecific
__pthread_setspecific: jmp _pthread_setspecific

.globl __pthread_mutex_lock
__pthread_mutex_lock: jmp _pthread_mutex_lock

.globl __pthread_mutex_unlock
__pthread_mutex_unlock: jmp _pthread_mutex_unlock

.globl __pthread_mutex_destroy
__pthread_mutex_destroy: jmp _pthread_mutex_destroy

.globl __pthread_once
__pthread_once: jmp _pthread_once

.globl __pthread_rwlock_rdlock
__pthread_rwlock_rdlock: jmp _pthread_rwlock_rdlock$UNIX2003

.globl __pthread_rwlock_unlock
__pthread_rwlock_unlock: jmp _pthread_rwlock_unlock$UNIX2003

.globl __pthread_rwlock_wrlock
__pthread_rwlock_wrlock: jmp _pthread_rwlock_wrlock$UNIX2003

/* ═══════════════════════════════════════════════════════════
 * $INODE64 dir helpers → base function stubs
 * ═══════════════════════════════════════════════════════════ */

.globl __fixtelldir$INODE64
__fixtelldir$INODE64: ret

.globl _regcomp$UNIX2003
_regcomp$UNIX2003: jmp _regcomp

/* ═══════════════════════════════════════════════════════════
 * nanosleep / waitpid syscalls (raw Mach-O BSD syscalls)
 * ═══════════════════════════════════════════════════════════ */

/* ___nanosleep — raw syscall 240 */
.globl ___nanosleep
___nanosleep:
    movl $0x20000f0, %eax   /* 240 = 0xf0, | 0x2000000 */
    movq %rcx, %r10
    syscall
    jnc 1f
    negq %rax
1:  ret

.globl __nanosleep
__nanosleep: jmp ___nanosleep

/* __waitpid — implemented via wait4 syscall (7) with rusage=NULL */
.globl __waitpid
__waitpid:
    /* waitpid(pid, stat, opts) → wait4(pid, stat, opts, NULL) */
    xorq %rcx, %rcx        /* rusage = NULL → r10 */
    movq %rcx, %r10
    movl $0x2000007, %eax   /* SYS_wait4 = 7 */
    syscall
    jnc 1f
    negq %rax
1:  ret

/* __sigaction_nobind — same as sigaction for now */
.globl __sigaction_nobind
__sigaction_nobind: jmp ___sigaction

/* _openat — same as ___openat */
.globl _openat
_openat: jmp ___openat

/* _open_dprotected_np — same as ___open_dprotected_np */
.globl _open_dprotected_np
_open_dprotected_np: jmp ___open_dprotected_np

/* _setpriority — syscall 96 */
.globl _setpriority
_setpriority:
    movl $0x2000060, %eax   /* 96 = 0x60 */
    movq %rcx, %r10
    syscall
    jnc 1f
    negq %rax
1:  ret

/* _settimeofday — syscall 122 */
.globl _settimeofday
_settimeofday:
    movl $0x200007a, %eax   /* 122 = 0x7a */
    movq %rcx, %r10
    syscall
    jnc 1f
    negq %rax
1:  ret

/* _posix_spawn — forward to ___posix_spawn */
.globl _posix_spawn
_posix_spawn: jmp ___posix_spawn

/* setjmp/longjmp aliases are in libsystem_platform */

/* ═══════════════════════════════════════════════════════════
 * Pthread internal stubs (double-underscore → function)
 * ═══════════════════════════════════════════════════════════ */

/* __pthread_mutex_trylock → pthread_mutex_trylock */
.globl __pthread_mutex_trylock
__pthread_mutex_trylock: jmp _pthread_mutex_trylock

/* __pthread_key_create → pthread_key_create */
.globl __pthread_key_create
__pthread_key_create: jmp _pthread_key_create

/* __pthread_getspecific_direct → pthread_getspecific */
.globl __pthread_getspecific_direct
__pthread_getspecific_direct: jmp _pthread_getspecific

/* __pthread_has_direct_tsd → returns 0 */
.globl __pthread_has_direct_tsd
__pthread_has_direct_tsd:
    xorl %eax, %eax
    ret

/* ═══════════════════════════════════════════════════════════
 * Extra underscore aliases (name mangling mismatch)
 * ═══════════════════════════════════════════════════════════ */

/* ____tolower_l (4 underscores) → ___tolower_l (3 underscores) */
.globl ____tolower_l
____tolower_l: jmp ___tolower_l

/* ____toupper_l (4 underscores) → ___toupper_l (3 underscores) */
.globl ____toupper_l
____toupper_l: jmp ___toupper_l

/* __vasprintf → _vasprintf */
.globl __vasprintf
__vasprintf: jmp _vasprintf

/* __vdprintf → our _vdprintf_impl */
.globl __vdprintf
__vdprintf: jmp __vdprintf_impl

/* __vsnprintf → vsnprintf (provided by libc objects) */
.globl __vsnprintf
__vsnprintf: jmp _vsnprintf
