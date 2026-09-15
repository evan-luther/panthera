/*
 * panthera_extra_aliases.s
 *
 * Consolidated assembly aliases for libpanthera_extra.
 */

.text

.globl dyld_stub_binder
.globl _dyld_stub_binder
dyld_stub_binder:
_dyld_stub_binder:
    ret

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
.globl _iconv
_iconv: jmp _libiconv
.globl _iconv_close
_iconv_close: jmp _libiconv_close
.globl _iconv_open
_iconv_open: jmp _libiconv_open
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
.globl "_read$NOCANCEL$UNIX2003"
"_read$NOCANCEL$UNIX2003": jmp "_read$NOCANCEL"
.globl _setrlimit$UNIX2003
_setrlimit$UNIX2003: jmp ___setrlimit
.globl _sigsuspend$UNIX2003
_sigsuspend$UNIX2003: jmp _sigsuspend
.globl _writev$UNIX2003
_writev$UNIX2003: jmp _writev
.globl _getattrlist$UNIX2003
_getattrlist$UNIX2003: jmp ___getattrlist
.globl _send$UNIX2003
_send$UNIX2003: jmp ___sendto
.globl _pthread_sigmask$UNIX2003
_pthread_sigmask$UNIX2003: jmp ___pthread_sigmask
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
.globl __fixtelldir$INODE64
__fixtelldir$INODE64: ret
.globl _regcomp$UNIX2003
_regcomp$UNIX2003: jmp _regcomp
.globl __waitpid
__waitpid:
    xorq %rcx, %rcx
    movq %rcx, %r10
    movl $0x2000007, %eax
    syscall
    jnc 1f
    negq %rax
1:  ret
.globl __sigaction_nobind
__sigaction_nobind: jmp ___sigaction
.globl _openat
_openat: jmp ___openat

/* Included from panthera_resolve_wave2_asm.s */
.globl _mprotect$UNIX2003
_mprotect$UNIX2003: jmp ___mprotect
.globl _write$UNIX2003
_write$UNIX2003: jmp _write
.globl "_write$NOCANCEL$UNIX2003"
"_write$NOCANCEL$UNIX2003": jmp "_write$NOCANCEL"
.globl _pthread_testcancel$UNIX2003
_pthread_testcancel$UNIX2003:
    ret
.globl _ttyname_r$UNIX2003
_ttyname_r$UNIX2003:
    pushq   %rbx
    movq    %rsi, %rbx
    leaq    _ttyname_console_str(%rip), %rsi
    movq    %rdx, %rcx
    xorq    %rax, %rax
1:  testq   %rcx, %rcx
    jz      2f
    movb    (%rsi, %rax), %dl
    testb   %dl, %dl
    jz      2f
    movb    %dl, (%rbx, %rax)
    incq    %rax
    decq    %rcx
    jmp     1b
2:  movb    $0, (%rbx, %rax)
    xorl    %eax, %eax
    popq    %rbx
    ret

.data
_ttyname_console_str:
    .asciz "/dev/console"

.text
.globl ____chkstk_darwin
____chkstk_darwin:
    pushq   %rcx
    pushq   %rdx
    movq    %rsp, %rcx
    subq    $24, %rcx
    movq    %rax, %rdx
1:  cmpq    $0x1000, %rdx
    jb      2f
    subq    $0x1000, %rcx
    testb   $0, (%rcx)
    subq    $0x1000, %rdx
    jmp     1b
2:  subq    %rdx, %rcx
    testb   $0, (%rcx)
    popq    %rdx
    popq    %rcx
    ret

.globl _closedir
_closedir:
    jmp _closedir$UNIX2003
.globl _opendir
_opendir:
    jmp _opendir$INODE64$UNIX2003
.globl _opendir$INODE64
_opendir$INODE64:
    jmp _opendir$INODE64$UNIX2003
.globl _fdopendir
_fdopendir:
    jmp _fdopendir$INODE64$UNIX2003
.globl _fdopendir$INODE64
_fdopendir$INODE64:
    jmp _fdopendir$INODE64$UNIX2003
.globl _readdir
_readdir:
    jmp _readdir$INODE64
.globl _readdir_r
_readdir_r:
    jmp _readdir_r$INODE64
.globl _mktime
_mktime:
    jmp _mktime$UNIX2003
.globl _ttyname_r
_ttyname_r:
    jmp _ttyname_r$UNIX2003
.globl _killpg
_killpg:
    jmp _killpg$UNIX2003
.globl ___opendir2
___opendir2:
    jmp ___opendir2$INODE64$UNIX2003
.globl ___opendir2$INODE64
___opendir2$INODE64:
    jmp ___opendir2$INODE64$UNIX2003

/* Plain aliases that were previously coming from unix2003_aliases.s. */
.globl _pread
_pread:
    jmp _pread$UNIX2003
.globl _pwrite
_pwrite:
    jmp _pwrite$UNIX2003

/* Legacy export kept to preserve the old surface during the transition. */
.globl __select_1050
__select_1050:
    jmp _select

/* _strnlen: provided by panthera_extra_stubs.o (removed from strip list) */

/* Reverse aliases: base name -> $UNIX2003 variant (for non-Apple code that calls base names) */
.globl _strtof
_strtof:
    jmp _strtof$UNIX2003

.globl _strtof_l
_strtof_l:
    jmp _strtof_l$UNIX2003

.globl _strtod_l
_strtod_l:
    jmp _strtod_l$UNIX2003

.globl _strftime_l
_strftime_l:
    jmp _strftime_l$UNIX2003

/* Darwin base-name aliases used by third-party userland built against SDK headers. */
.globl _pause
_pause:
    jmp ___pause

.globl _pthread_cancel
_pthread_cancel:
    jmp _pthread_cancel$UNIX2003

.globl _pthread_setcancelstate
_pthread_setcancelstate:
    jmp _pthread_setcancelstate$UNIX2003

.globl _pthread_setcanceltype
_pthread_setcanceltype:
    jmp _pthread_setcanceltype$UNIX2003

.globl _pthread_cond_timedwait
_pthread_cond_timedwait:
    jmp _pthread_cond_timedwait$UNIX2003

/* __cxa_init_primary_exception: stub for LLVM 19+ libc++ (not in our older libc++abi) */
.globl ___cxa_init_primary_exception
___cxa_init_primary_exception:
    xorl %eax, %eax
    ret

/* panthera_patch.sh unix2003 alias: _waitpid$UNIX2003 -> _waitpid */
.globl _waitpid$UNIX2003
_waitpid$UNIX2003:
    jmp _waitpid
