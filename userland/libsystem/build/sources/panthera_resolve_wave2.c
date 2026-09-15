/*
 * panthera_resolve_wave2.c — Second wave of symbol resolutions
 *
 * These are symbols needed by libsystem_malloc, libsystem_pthread,
 * libdispatch, and libncurses that weren't in our first pass.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/*
 * These names are now exported by real Apple objects. Keep Panthera's fallback
 * implementations private so they cannot override the real code from
 * libsystem_c.dylib at runtime.
 */
#define backtrace panthera_private_backtrace
#define backtrace_image_offsets panthera_private_backtrace_image_offsets
#define thread_stack_pcs panthera_private_thread_stack_pcs
#define _ctx_done panthera_private_ctx_done
#define _sigunaltstack panthera_private_sigunaltstack
#define ttyname panthera_private_ttyname
#define __unix_conforming panthera_private_unix_conforming_wave2
#define pthread_key_init_np panthera_private_pthread_key_init_np_wave2
#define pthread_kill panthera_private_pthread_kill_wave2
#define uuid_compare panthera_private_uuid_compare
#define uuid_unparse panthera_private_uuid_unparse
#define mach_task_is_self panthera_private_mach_task_is_self
#define mach_port_construct panthera_private_mach_port_construct
#define mach_port_mod_refs panthera_private_mach_port_mod_refs
#define mach_thread_self panthera_private_mach_thread_self
#define mig_dealloc_reply_port panthera_private_mig_dealloc_reply_port
#define host_info panthera_private_host_info
#define thread_switch panthera_private_thread_switch
#define swtch_pri panthera_private_swtch_pri
#define thread_info panthera_private_thread_info
#define thread_policy panthera_private_thread_policy

/* ═══════════════════════════════════════════════════════════
 * Mach VM functions — REPLACED by real Apple mach_vm.c
 * (mach_vm_traps.s + mach_vm_mig_stubs.c + mach_vm.o)
 * Only vm_copy remains here (not in Apple's mach_vm.c).
 * ═══════════════════════════════════════════════════════════ */

static long _bsd_sc(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(num | 0x2000000), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    return ret;
}

/* ═══════════════════════════════════════════════════════════
 * VM page size globals — normally in libsystem_kernel.dylib
 * x86_64 page size is always 4096
 * ═══════════════════════════════════════════════════════════ */

unsigned long vm_page_size = 4096;
unsigned long vm_page_mask = 4095;
int vm_page_shift = 12;

unsigned long vm_kernel_page_size = 4096;
unsigned long vm_kernel_page_mask = 4095;
int vm_kernel_page_shift = 12;

/* mach_task_is_self — check if a given port is the current task */
int mach_task_is_self(unsigned int task_port) {
    /* Get current task port via Mach trap */
    unsigned int self_port;
    __asm__ volatile(
        "movl $0x0100001c, %%eax\n\t"
        "syscall"
        : "=a"(self_port) : : "rcx", "r11", "memory");
    return task_port == self_port;
}

/* gCRAnnotations — CrashReporter annotations (stub) */
uint64_t gCRAnnotations[8] = {0};

int vm_copy(unsigned int task, unsigned long src, unsigned long cnt, unsigned long dst) {
    (void)task;
    extern void *_platform_memmove(void *, const void *, unsigned long);
    _platform_memmove((void *)dst, (void *)src, cnt);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Mach port functions
 * ═══════════════════════════════════════════════════════════ */

#ifndef _PANTHERA_MACH_TYPES_DECLARED
#define _PANTHERA_MACH_TYPES_DECLARED
typedef int kern_return_t;
typedef uint32_t mach_port_t;
typedef uint32_t mach_port_name_t;
typedef int mach_port_right_t;
typedef int mach_msg_type_name_t;
typedef int mach_port_delta_t;
typedef uint64_t mach_port_context_t;
typedef uint64_t mach_port_guard_t;

#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_FAILURE 5

#define MACH_PORT_NULL ((mach_port_t)0)
#define MACH_PORT_DEAD ((mach_port_t)~0)

#define MACH_PORT_RIGHT_SEND 0
#define MACH_PORT_RIGHT_RECEIVE 1
#define MACH_PORT_RIGHT_SEND_ONCE 2
#define MACH_PORT_RIGHT_PORT_SET 3
#define MACH_PORT_RIGHT_DEAD_NAME 4

#define MACH_MSG_TYPE_MAKE_SEND 20
#define MACH_MSG_TYPE_MAKE_SEND_ONCE 21
#define MACH_MSG_TYPE_COPY_SEND 19

extern kern_return_t mach_port_allocate(mach_port_t task, mach_port_right_t right, mach_port_name_t *name);
extern kern_return_t mach_port_insert_right(mach_port_t task, mach_port_name_t name, mach_port_t poly, mach_msg_type_name_t polyPoly);
extern kern_return_t mach_port_destroy(mach_port_t task, mach_port_name_t name);
extern kern_return_t mach_port_deallocate(mach_port_t task, mach_port_name_t name);
extern kern_return_t mach_port_set_context(mach_port_t task, mach_port_name_t name, mach_port_context_t context) __attribute__((weak));
extern kern_return_t mach_port_get_context(mach_port_t task, mach_port_name_t name, mach_port_context_t *context);
extern kern_return_t mach_port_mod_refs(mach_port_t task, mach_port_name_t name, mach_port_right_t right, mach_port_delta_t delta);
#endif

int mach_port_construct(mach_port_t task, void *options, mach_port_context_t context, mach_port_name_t *name) {
    (void)options;
    if (!name) return KERN_INVALID_ARGUMENT;
    mach_port_t port = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &port);
    if (kr != KERN_SUCCESS) return kr;
    kr = mach_port_insert_right(task, port, port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        mach_port_destroy(task, port);
        return kr;
    }
    if (context != 0 && mach_port_set_context) {
        mach_port_set_context(task, port, context);
    }
    *name = port;
    return KERN_SUCCESS;
}

int mach_port_mod_refs(mach_port_t task, mach_port_name_t name, mach_port_right_t right, mach_port_delta_t delta) {
    (void)task; (void)name; (void)right; (void)delta;
    return KERN_SUCCESS;
}

unsigned int mach_thread_self(void) {
    long ret;
    __asm__ volatile(
        "movl $0x0100001b, %%eax\n\t"
        "syscall"
        : "=a"(ret) : : "rcx", "r11", "memory");
    return (unsigned int)ret;
}

void mig_dealloc_reply_port(unsigned int port) {
    (void)port;
}

/* host_info */
int host_info(unsigned int host, int flavor, void *info, unsigned int *count) {
    (void)host; (void)flavor;
    /* Return minimal info */
    if (info && count) {
        for (unsigned int i = 0; i < *count * 4 && i < 64; i++)
            ((char *)info)[i] = 0;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Thread functions
 * ═══════════════════════════════════════════════════════════ */

int thread_switch(unsigned int thread, int option, int timeout) {
    (void)thread; (void)option; (void)timeout;
    /* Yield CPU via sched_yield (not a real Mach trap, but close enough) */
    return 0;
}

int swtch_pri(int pri) {
    (void)pri;
    return 0;
}

int thread_info(unsigned int thread, int flavor, void *info, unsigned int *count) {
    (void)thread; (void)flavor;
    if (info && count) {
        for (unsigned int i = 0; i < *count * 4 && i < 64; i++)
            ((char *)info)[i] = 0;
    }
    return 0;
}

int thread_policy(unsigned int thread, int policy, void *base, unsigned int count, int set) {
    (void)thread; (void)policy; (void)base; (void)count; (void)set;
    return 0;
}

int thread_destruct_special_reply_port(unsigned int port, int reason) {
    (void)port; (void)reason;
    return 0;
}

/* backtrace */
int backtrace(void **buffer, int size) {
    (void)buffer; (void)size;
    return 0;
}

int backtrace_image_offsets(void **buffer, unsigned int *offsets, int size) {
    (void)buffer; (void)offsets; (void)size;
    return 0;
}

void *thread_stack_pcs(void) {
    return (void *)0;
}

/* kdebug_trace */
int kdebug_trace(uint32_t code, unsigned long a, unsigned long b, unsigned long c, unsigned long d) {
    (void)code; (void)a; (void)b; (void)c; (void)d;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * nanov2 — nano allocator (used by libsystem_malloc)
 * Stub these out — malloc will use the regular zone instead
 * ═══════════════════════════════════════════════════════════ */

void nanov2_init(void) {}
void *nanov2_create_zone(void *helper_zone, unsigned flags) {
    (void)helper_zone; (void)flags;
    return (void *)0; /* NULL = don't use nano zone */
}
void nanov2_configure(void) {}
void *nanov2_forked_zone(void *zone) {
    (void)zone;
    return (void *)0;
}

/* ═══════════════════════════════════════════════════════════
 * os thread restrict functions (JIT/W^X support)
 * ═══════════════════════════════════════════════════════════ */

int os_thread_self_restrict_rwx_is_supported(void) {
    return 0; /* Not supported */
}

void os_thread_self_restrict_rwx_to_rw(void) {}
void os_thread_self_restrict_rwx_to_rx(void) {}

/* os_unfair_lock_lock_no_tsd — lock without TSD (used during init) */
void os_unfair_lock_lock_no_tsd(uint32_t *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ volatile("pause");
    }
}

void os_unfair_lock_unlock_no_tsd(uint32_t *lock) {
    __sync_lock_release(lock);
}

int os_unfair_lock_trylock(uint32_t *lock) {
    return __sync_lock_test_and_set(lock, 1) == 0;
}

/* os_semaphore_dispose */
void _os_semaphore_dispose(void *sem) {
    (void)sem;
}

/* ═══════════════════════════════════════════════════════════
 * Pthread internal functions
 * ═══════════════════════════════════════════════════════════ */

/* _pthread_self_direct — fast path for pthread_self */
extern void *pthread_self(void);
void *_pthread_self_direct(void) {
    return pthread_self();
}

/* _pthread_setspecific_direct — fast TSD set */
extern int pthread_setspecific(unsigned long, const void *);
int _pthread_setspecific_direct(unsigned long key, const void *value) {
    return pthread_setspecific(key, value);
}

/* _pthread_join */
extern int pthread_join(void *, void **);
int _pthread_join(void *thread, void **retval) {
    return pthread_join(thread, retval);
}

/* Internal pthread stubs */
void _pthread_joiner_prepost_wake(void *thread) { (void)thread; }
void _pthread_markcancel_if_canceled(void *thread, int cancelstate) {
    (void)thread; (void)cancelstate;
}
void _pthread_setcancelstate_exit(void *thread, int state) {
    (void)thread; (void)state;
}

/* ═══════════════════════════════════════════════════════════
 * dyld API functions (callable by other dylibs)
 * ═══════════════════════════════════════════════════════════ */

void *_dyld_get_image_header(uint32_t index) {
    (void)index;
    return (void *)0;
}

long _dyld_get_image_slide(uint32_t index) {
    (void)index;
    return 0;
}

int _dyld_is_memory_immutable(const void *addr, unsigned long length) {
    (void)addr; (void)length;
    return 0;
}

int dyld_process_is_restricted(void) {
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Simple logging functions (libplatform)
 * ═══════════════════════════════════════════════════════════ */

void _simple_asl_log(int level, const char *facility, const char *msg) {
    (void)level; (void)facility;
    if (msg) {
        const char *p = msg;
        while (*p) p++;
        _bsd_sc(4 /* SYS_write */, 2, (long)msg, p - msg, 0, 0, 0);
        _bsd_sc(4, 2, (long)"\n", 1, 0, 0, 0);
    }
}

void _simple_put(const char *msg) {
    if (msg) {
        const char *p = msg;
        while (*p) p++;
        _bsd_sc(4, 2, (long)msg, p - msg, 0, 0, 0);
    }
}

void _simple_vdprintf(int fd, const char *fmt, va_list ap) {
    (void)ap;
    /* Just write the format string */
    if (fmt) {
        const char *p = fmt;
        while (*p) p++;
        _bsd_sc(4, fd, (long)fmt, p - fmt, 0, 0, 0);
    }
}

/* ═══════════════════════════════════════════════════════════
 * OSAtomic — 64-bit variants
 * ═══════════════════════════════════════════════════════════ */

int OSAtomicCompareAndSwapLong(long oldval, long newval, volatile long *value) {
    return __sync_bool_compare_and_swap(value, oldval, newval);
}

int64_t OSAtomicIncrement64(volatile int64_t *value) {
    return __sync_add_and_fetch(value, 1);
}

/* ═══════════════════════════════════════════════════════════
 * UUID functions
 * ═══════════════════════════════════════════════════════════ */

int uuid_compare(const unsigned char *a, const unsigned char *b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

void uuid_unparse(const unsigned char *uuid, char *out) {
    static const char hex[] = "0123456789abcdef";
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[p++] = '-';
        out[p++] = hex[uuid[i] >> 4];
        out[p++] = hex[uuid[i] & 0xf];
    }
    out[p] = 0;
}

/* ═══════════════════════════════════════════════════════════
 * Misc stubs
 * ═══════════════════════════════════════════════════════════ */

extern char *getenv(const char *);
char *getprogname(void) {
    static char progname[] = "panthera";
    return progname;
}

void abort_with_reason(uint32_t reason_namespace, uint64_t reason_code,
                       const char *reason_string, uint64_t flags) {
    (void)reason_namespace; (void)reason_code; (void)flags;
    if (reason_string) {
        const char *p = reason_string;
        while (*p) p++;
        _bsd_sc(4, 2, (long)"ABORT: ", 7, 0, 0, 0);
        _bsd_sc(4, 2, (long)reason_string, p - reason_string, 0, 0, 0);
        _bsd_sc(4, 2, (long)"\n", 1, 0, 0, 0);
    }
    _bsd_sc(1 /* SYS_exit */, 127, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

/* _ctx_done — context switching (from ucontext) */
void _ctx_done(void) {
    _bsd_sc(1, 0, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

/* _setjmp / _longjmp / __setjmp / __longjmp — moved to wave3 assembly.
 * DO NOT define C wrappers here (creates infinite recursion). */

/* __sigunaltstack — sigaltstack syscall wrapper */
int _sigunaltstack(const void *ss, void *oss) {
    return (int)_bsd_sc(53 /* SYS_sigaltstack */, (long)ss, (long)oss, 0, 0, 0, 0);
}

/* ═══════════════════════════════════════════════════════════
 * ttyname — return terminal name (override Apple's version)
 * Apple's ttyname crashes when /dev/ is empty.
 * Return a safe default for serial console.
 * ═══════════════════════════════════════════════════════════ */

static char _ttyname_buf[] = "/dev/console";

char *ttyname(int fd) {
    (void)fd;
    return _ttyname_buf;
}

/* ttyname_r provided by symbol_aliases.o → ttyname_r$UNIX2003 */

/* isatty is provided by gen_FreeBSD_isatty.o */

/* pthread TSD moved to panthera_pthread_simple.c (separate dylib) */

/* ═══════════════════════════════════════════════════════════
 * ___chkstk_darwin — stack probe for large stack frames
 * Called by compiler when a function needs >4096 bytes of stack.
 * Probes each page to trigger guard page exceptions.
 * ═══════════════════════════════════════════════════════════ */

/* Assembly implementation in wave2_asm.s */

/* ___unix_conforming — UNIX conformance flag */
int __unix_conforming = 1;

/* __pthread_workqueue_setkill — workqueue kill handler */
void __pthread_workqueue_setkill(int enable) { (void)enable; }

/* pthread_key_init_np — Apple-specific key initialization */
int pthread_key_init_np(int key, void (*destructor)(void *)) {
    (void)key; (void)destructor;
    return 0;
}

/* pthread_kill — send signal to thread */
static long _bsd_sc2(long num, long a1, long a2) {
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(num | 0x2000000), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory", "cc");
    return ret;
}

int pthread_kill(void *thread, int sig) {
    (void)thread;
    /* For the main thread, use kill(getpid(), sig) */
    if (sig == 0) return 0;
    return (int)_bsd_sc2(37 /* SYS_kill */, 0 /* pid=0 = self */, sig);
}

/* __error and cthread_set_errno_self are in panthera_missing.c */
