/*
 * test_pthread_raw.c — Verify raw bsdthread_create works on Panthera.
 * Bypasses libpthread entirely — tests the kernel thread creation path directly.
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

/* Direct syscall wrappers */
static int bsdthread_register(void *tstart, void *wqstart,
    uint32_t flags, void *stack_hint, void *targetconc,
    uint64_t dq_offset, uint32_t tsd_offset)
{
    int ret;
    __asm__ __volatile__(
        "movl $0x200016e, %%eax\n"
        "syscall\n"
        "movl %%eax, %0\n"
        : "=r"(ret)
        : "D"(tstart), "S"(wqstart), "d"(flags),
          /* r10=stack_hint, r8=targetconc, r9=dq_offset */
          /* 7th arg on stack: tsd_offset */
          "r"((uint64_t)stack_hint), "r"((uint64_t)targetconc), "r"((uint64_t)dq_offset)
        : "rax", "rcx", "r10", "r11", "memory"
    );
    return ret;
}

/* Shared state */
static volatile int child_done = 0;
static volatile int child_value = 0;

/* Raw thread entry — called directly by the kernel, NOT through _thread_start.
 * This function must not use TLS, printf, or anything that needs pthread_self().
 * Args come through registers set by the kernel:
 *   rdi = user_pthread (we use as our arg pointer)
 *   rsi = mach port
 *   rdx = user_func (our actual function)
 *   rcx = user_funcarg
 *   r8  = user_stack
 *   r9  = flags
 */
__attribute__((naked)) void raw_thread_start(void) {
    /* rcx = user_funcarg (our int* arg) */
    __asm__ __volatile__(
        "movq %%rcx, %%rdi\n"   /* rdi = user_funcarg */
        "callq *%%rdx\n"        /* call user_func(user_funcarg) */
        /* Thread terminate: bsdthread_terminate syscall */
        "movq $0, %%rdi\n"      /* stackaddr = 0 */
        "movq $0, %%rsi\n"      /* freesize = 0 */
        "movl $0, %%edx\n"      /* kthport = 0 */
        "movq $0, %%r10\n"      /* sem = 0 */
        "movl $0x2000169, %%eax\n" /* __bsdthread_terminate */
        "syscall\n"
        "ud2\n"
        ::: "memory"
    );
}

/* The actual worker function */
static void worker(int *valp) {
    *valp = 42;
    child_value = 42;
    /* Use write() directly — no printf (needs TLS) */
    const char msg[] = "[raw thread] running, setting value=42\n";
    write(1, msg, sizeof(msg) - 1);
    child_done = 1;
}

int main(void) {
    printf("test_pthread_raw: starting\n");

    /* Register our raw_thread_start as the thread entry */
    int rc = bsdthread_register(raw_thread_start, raw_thread_start,
                                 0, NULL, NULL, 0, 0);
    printf("bsdthread_register returned %d\n", rc);

    /* Allocate a stack for the child thread (512KB) */
    size_t stacksz = 512 * 1024;
    void *stack = mmap(NULL, stacksz, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
    if (stack == MAP_FAILED) {
        printf("FAIL: mmap for stack failed\n");
        return 1;
    }
    void *stack_top = (char *)stack + stacksz;
    printf("stack=%p top=%p\n", stack, stack_top);

    /* Call __bsdthread_create directly:
     * func=worker, funcarg=&child_value, stack=stack_top, pthread=stack_top, flags=0 */
    uint64_t result;
    __asm__ __volatile__(
        "movl $0x2000168, %%eax\n" /* __bsdthread_create */
        "movq %1, %%rdi\n"       /* func = worker */
        "movq %2, %%rsi\n"       /* funcarg = &child_value */
        "movq %3, %%rdx\n"       /* stack = stack_top */
        "movq %3, %%r10\n"       /* pthread = stack_top */
        "movl $0, %%r8d\n"       /* flags = 0 */
        "syscall\n"
        "jc 1f\n"
        "movq %%rax, %0\n"
        "jmp 2f\n"
        "1: negq %%rax\n"
        "movq %%rax, %0\n"
        "2:\n"
        : "=r"(result)
        : "r"((uint64_t)(void*)worker), "r"((uint64_t)(void*)&child_value),
          "r"((uint64_t)stack_top)
        : "rax", "rcx", "rdx", "rdi", "rsi", "r8", "r10", "r11", "memory"
    );
    printf("bsdthread_create returned %p\n", (void*)result);

    /* Wait for child */
    for (int i = 0; i < 50; i++) {
        if (child_done) break;
        usleep(100000); /* 100ms */
    }

    if (child_done && child_value == 42) {
        printf("test_pthread_raw: ALL PASS\n");
        return 0;
    } else {
        printf("FAIL: child_done=%d child_value=%d\n", child_done, child_value);
        return 1;
    }
}
