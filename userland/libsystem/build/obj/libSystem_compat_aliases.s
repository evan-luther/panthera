/* libSystem_compat_aliases.s — real exported trampolines for early userland */

.text

.globl _fnmatch
.p2align 4, 0x90
_fnmatch:
    jmp _fnmatch$UNIX2003

.globl _putp$NCURSES60
.p2align 4, 0x90
_putp$NCURSES60:
    jmp _putp

.globl _sigsuspend$UNIX2003
.p2align 4, 0x90
_sigsuspend$UNIX2003:
    jmp _sigsuspend

.globl _getrlimit
.p2align 4, 0x90
_getrlimit:
    jmp _getrlimit$UNIX2003

.globl _setrlimit
.p2align 4, 0x90
_setrlimit:
    jmp _setrlimit$UNIX2003

.globl _pthread_sigmask$UNIX2003
.p2align 4, 0x90
_pthread_sigmask$UNIX2003:
    jmp _pthread_sigmask

.globl __sigaction
.p2align 4, 0x90
__sigaction:
    jmp _sigaction

.globl ___commpage_gettimeofday
.p2align 4, 0x90
___commpage_gettimeofday:
    xorl %esi, %esi
    jmp ___gettimeofday
