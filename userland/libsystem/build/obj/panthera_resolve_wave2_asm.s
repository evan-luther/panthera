/*
 * panthera_resolve_wave2_asm.s — Assembly aliases for second wave symbols
 */

.text

/* $UNIX2003 aliases */
.globl _mprotect$UNIX2003
_mprotect$UNIX2003: jmp ___mprotect

.globl _sleep$UNIX2003
_sleep$UNIX2003: jmp _sleep

.globl _write$UNIX2003
_write$UNIX2003: jmp _write

.globl _pthread_testcancel$UNIX2003
_pthread_testcancel$UNIX2003:
    ret     /* no-op: cancellation not implemented */

/* ttyname_r$UNIX2003 — simple implementation returning /dev/console */
.globl _ttyname_r$UNIX2003
_ttyname_r$UNIX2003:
    /* rdi=fd, rsi=buf, rdx=len */
    pushq   %rbx
    movq    %rsi, %rbx          /* save buf ptr */
    leaq    _ttyname_console_str(%rip), %rsi
    movq    %rdx, %rcx          /* len → rcx for loop */
    xorq    %rax, %rax          /* index=0 */
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
    xorl    %eax, %eax          /* return 0 */
    popq    %rbx
    ret

.data
_ttyname_console_str:
    .asciz "/dev/console"

.text

/* ═══════════════════════════════════════════════════════════
 * ___chkstk_darwin — stack probe for large stack allocations
 *
 * Called with the allocation size in %rax. Probes stack pages
 * downward to ensure they're committed. On XNU, this is needed
 * to avoid SIGSEGV when jumping over guard pages.
 *
 * Must preserve all registers except %rax and flags.
 * ═══════════════════════════════════════════════════════════ */
.globl ____chkstk_darwin
____chkstk_darwin:
    /* rax = number of bytes to allocate on stack */
    /* Simply touch each page to ensure it's mapped */
    pushq   %rcx
    pushq   %rdx
    movq    %rsp, %rcx          /* current stack pointer */
    subq    $24, %rcx           /* account for our pushed regs + return addr */
    movq    %rax, %rdx          /* total bytes to probe */
1:  cmpq    $0x1000, %rdx       /* more than one page left? */
    jb      2f
    subq    $0x1000, %rcx       /* next page down */
    testb   $0, (%rcx)          /* probe the page (read) */
    subq    $0x1000, %rdx
    jmp     1b
2:  subq    %rdx, %rcx
    testb   $0, (%rcx)          /* probe final page */
    popq    %rdx
    popq    %rcx
    ret
