# dyldStartup.s — Panthera dyld entry point
#
# The kernel sets RSP to the user stack containing:
#   argc, argv[0..argc-1], NULL, envp[0..n], NULL, apple[0..n], NULL
# The mach_header of the main executable is at RSP[0] if the kernel
# puts it there, or in the apple[] strings otherwise.
#
# We pass RSP as the sole argument (in RDI) to our C start() function.

.text
.globl __dyld_start
.globl start
.p2align 4

__dyld_start:
    movq    %rsp, %rdi          # RDI = kernel stack pointer (argc/argv/envp/apple)
    andq    $-16, %rsp          # 16-byte align the stack
    xorq    %rbp, %rbp          # clear frame pointer for clean backtraces
    pushq   $0                  # fake return address (dyld never returns)
    jmp     _start              # → start(kernArgs) in panthera_dyld.cpp

# gotoAppStart — called by dyld after initialization to transfer to the app.
# RDI = entry point address, RSI = original kernel stack pointer
.globl _gotoAppStart
.p2align 4
_gotoAppStart:
    movq    %rsi, %rsp          # restore original kernel stack
    addq    $8, %rsp            # skip mach_header* (or adjust for stack layout)
    jmpq    *%rdi               # jump to app entry point

start:
    jmp     _start
