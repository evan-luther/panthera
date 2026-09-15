/*
 * panthera_missing.c — Symbols missing from libsystem_c that zsh needs
 * Kept separate from posix_wrappers.c to avoid conflicts
 */
#include <stdint.h>

struct in6_addr {
    unsigned char __u6_addr[16];
};

/* __cleanup — function pointer called by exit() to flush stdio */
static void _do_cleanup(void) {
    /* Minimal: fsync stdout and stderr */
    long _sc(long n, long a1, long a2, long a3, long a4, long a5, long a6);
    /* Just use asm directly */
    __asm__ volatile("movq $95, %%rax\n\t" /* SYS_fsync */
                     "orq $0x2000000, %%rax\n\t"
                     "movq $1, %%rdi\n\t" /* fd=stdout */
                     "syscall" ::: "rax","rdi","rcx","r11","memory");
    __asm__ volatile("movq $95, %%rax\n\t"
                     "orq $0x2000000, %%rax\n\t"
                     "movq $2, %%rdi\n\t" /* fd=stderr */
                     "syscall" ::: "rax","rdi","rcx","r11","memory");
}

void _cleanup(void) { _do_cleanup(); }
void (*__cleanup)(void) = _do_cleanup;

/* OSAtomic — atomic operations using GCC builtins */
int32_t OSAtomicDecrement32Barrier(volatile int32_t *value) {
    return __sync_sub_and_fetch(value, 1);
}
int32_t OSAtomicIncrement32Barrier(volatile int32_t *value) {
    return __sync_add_and_fetch(value, 1);
}
int32_t OSAtomicDecrement32(volatile int32_t *value) {
    return __sync_sub_and_fetch(value, 1);
}
int32_t OSAtomicIncrement32(volatile int32_t *value) {
    return __sync_add_and_fetch(value, 1);
}
int32_t OSAtomicAdd32Barrier(int32_t amount, volatile int32_t *value) {
    return __sync_add_and_fetch(value, amount);
}
int OSAtomicCompareAndSwap32Barrier(int32_t oldval, int32_t newval, volatile int32_t *value) {
    return __sync_bool_compare_and_swap(value, oldval, newval);
}
int OSAtomicCompareAndSwapPtrBarrier(void *oldval, void *newval, void * volatile *value) {
    return __sync_bool_compare_and_swap(value, oldval, newval);
}
void OSMemoryBarrier(void) { __sync_synchronize(); }

/* FLOCKFILE — stdio FILE locking (no-op until threading works) */
typedef struct __sFILE FILE;
void FLOCKFILE(FILE *fp) { (void)fp; }
void FUNLOCKFILE(FILE *fp) { (void)fp; }

/* __chk_assert_no_overlap — fortify source overlap check flag */
unsigned int __chk_assert_no_overlap = 1;
void __chk_overlap(const void *d, unsigned long dl, const void *s, unsigned long sl) {
    /* Overlap detection — abort on overlap */
    const char *dp = (const char*)d, *sp = (const char*)s;
    if ((dp >= sp && dp < sp + sl) || (sp >= dp && sp < dp + dl)) {
        __asm__ volatile("ud2"); /* crash */
    }
}

/* NSVersionOfLinkTimeLibrary — dyld API */
int NSVersionOfLinkTimeLibrary(const char *name) { return -1; }
int NSVersionOfRunTimeLibrary(const char *name) { return -1; }

typedef unsigned int NSSearchPathEnumerationState;
extern unsigned long strlcpy(char *, const char *, unsigned long);

NSSearchPathEnumerationState
NSStartSearchPathEnumeration(unsigned int directory, unsigned int domainMask) {
    (void)directory;
    (void)domainMask;
    return 1;
}

NSSearchPathEnumerationState
NSGetNextSearchPathEnumeration(NSSearchPathEnumerationState state, char *path) {
    if (path == 0) {
        return 0;
    }
    if (state == 1) {
        strlcpy(path, "/Library", 1024);
        return 2;
    }
    if (state == 2) {
        strlcpy(path, "/System/Library", 1024);
        return 0;
    }
    return 0;
}

const struct in6_addr in6addr_loopback = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}
};

/* errno support — read per-thread errno from TSD slot 1 (__TSD_ERRNO),
 * which libpthread populates via _pthread_tsd_slot(t, ERRNO) = &t->err_no.
 * Falls back to the process-global slot when TSD isn't set up (early boot
 * before pthread bootstrap, or threads that bypassed pthread init).
 *
 * In Darwin LP64 pthread layout (types_internal.h, panthera_dyld.cpp):
 * - TSD slot 0 (__TSD_THREAD_SELF) contains pthread_t (pointer to struct pthread_s)
 * - TSD slot 1 (__TSD_ERRNO) contains &self->err_no (pointer to int)
 * - offsetof(struct pthread_s, err_no) is 172
 * - offsetof(struct pthread_s, tsd) is 224
 * - maximum pthread allocation window is 16384 bytes
 */
static int _panthera_errno_global = 0;

#define _PANTHERA_PTHREAD_ERRNO_OFFSET 172
#define _PANTHERA_PTHREAD_MAX_STRUCT_SIZE 16384


static __inline__ int *_panthera_errno_tsd(void) {
    void *self_ptr = (void *)0;
    void *err_ptr = (void *)0;

#if defined(__x86_64__)
    __asm__("mov %%gs:%1, %0" : "=r" (self_ptr) : "m" (*(void **)(0 * sizeof(void *))));
    __asm__("mov %%gs:%1, %0" : "=r" (err_ptr) : "m" (*(void **)(1 * sizeof(void *))));
#elif defined(__arm64__)
    uintptr_t base;
    __asm__("mrs %0, TPIDRRO_EL0\n" : "=r" (base));
    base &= ~0x7ULL;
    if (base >= 0x1000ULL && base < 0x0000800000000000ULL) {
        void **slots = (void **)base;
        self_ptr = slots[0];
        err_ptr = slots[1];
    }
#else
    return (int *)0;
#endif

    uintptr_t u_self = (uintptr_t)self_ptr;
    uintptr_t u_err = (uintptr_t)err_ptr;

    /*
     * Validate both pointers:
     * 1. Non-null and outside unmapped low memory (>= 0x1000)
     * 2. Within canonical user space (< 0x0000800000000000)
     * 3. Naturally aligned: self (8-byte), err_no (4-byte)
     * 4. Structural consistency: err_ptr must point to self->err_no
     *    (exact offset 172 or within initialized pthread struct allocation)
     */
    if (u_self >= 0x1000ULL && u_self < 0x0000800000000000ULL &&
        (u_self & 0x7ULL) == 0 &&
        u_err >= 0x1000ULL && u_err < 0x0000800000000000ULL &&
        (u_err & 0x3ULL) == 0) {
        if (u_err == u_self + _PANTHERA_PTHREAD_ERRNO_OFFSET ||
            (u_err >= u_self && u_err < u_self + _PANTHERA_PTHREAD_MAX_STRUCT_SIZE)) {
            return (int *)err_ptr;
        }
    }

    return (int *)0;
}

int *__error(void) {
    int *p = _panthera_errno_tsd();
    if (p) return p;
    return &_panthera_errno_global;
}
int cerror(int err) {
    int *p = _panthera_errno_tsd();
    if (p) {
        *p = err;
    }
    _panthera_errno_global = err;
    return -1;
}

int cerror_nocancel(int err) {
    int *p = _panthera_errno_tsd();
    if (p) {
        *p = err;
    }
    _panthera_errno_global = err;
    return -1;
}

/* Darwin timezone alt variable */

/* __default_hash — hash function used by some libc internals */
unsigned long __default_hash(const void *key, unsigned long len) {
    const unsigned char *p = (const unsigned char *)key;
    unsigned long h = 5381;
    while (len--) h = h * 33 + *p++;
    return h;
}

/* _NSGetArgv / _NSGetArgc / _NSGetEnviron / _NSGetProgname */
/* Return the addresses of the live exported CRT globals. */
char **NXArgv = 0;
int NXArgc = 0;
char **environ = 0;
char *__progname = "unknown";

char ***_NSGetArgv(void) { return &NXArgv; }
int *_NSGetArgc(void) { return &NXArgc; }
char ***_NSGetEnviron(void) { return &environ; }
char **_NSGetProgname(void) { return &__progname; }

/* __lowercase_hex — hex digit lookup table used by printf-like functions */
const char __lowercase_hex[17] = "0123456789abcdef";
const char __uppercase_hex[17] = "0123456789ABCDEF";

/* _NSGetMachExecuteHeader — returns mach_header of main executable */
#include <mach-o/loader.h>
extern struct mach_header_64 _mh_execute_header;
const struct mach_header_64 *_NSGetMachExecuteHeader(void) {
    return &_mh_execute_header;
}

/* printf %n conversion registration — used internally by xprintf */
typedef int (*printf_arginfo_function)(void *, int, int *);
int __printf_arginfo_n(void *info, int n, int *argtypes) {
    (void)info; (void)n; (void)argtypes;
    return 0;
}

/* Additional xprintf extension registration stubs */
int __printf_arginfo_pct(void *info, int n, int *argtypes) { return 0; }
int __printf_render_n(void *info, void *fn, const void *s, int len, int t, void *arg) { return 0; }
int __printf_render_pct(void *info, void *fn, const void *s, int len, int t, void *arg) { return 0; }

/* __sdidinit — flag indicating stdio has been initialized */
int __sdidinit = 1; /* Set to 1: stdio is "ready" */

/* stdio glue — links FILE structures together */
/* struct glue and FILE are defined in <stdio.h> internals */
struct glue {
    struct glue *next;
    int niobs;
    void *iobs;  /* FILE* array */
};

/* Minimal __sF (stdin/stdout/stderr FILE structures) */
/* Each FILE is complex but for bootstrapping we just need the pointers */
static char __sF_storage[3 * 152]; /* 152 = sizeof(FILE) on x86_64 */

struct glue __sglue = { 0, 3, __sF_storage };

/* __sinit — stdio initialization, sets up stdin/stdout/stderr */
void __sinit(void) {
    __sdidinit = 1;
}

/* ── POSIX wrappers ─── */
/* String functions: standard name → __platform_ name */
extern unsigned long __platform_strlen(const char *);
unsigned long strlen(const char *s) { return __platform_strlen(s); }

extern int __platform_strcmp(const char *, const char *);
int strcmp(const char *a, const char *b) { return __platform_strcmp(a, b); }

extern int __platform_strncmp(const char *, const char *, unsigned long);
int strncmp(const char *a, const char *b, unsigned long n) { return __platform_strncmp(a, b, n); }

extern char *__platform_strcpy(char *, const char *);
char *strcpy(char *d, const char *s) { return __platform_strcpy(d, s); }

extern char *__platform_strncpy(char *, const char *, unsigned long);
char *strncpy(char *d, const char *s, unsigned long n) { return __platform_strncpy(d, s, n); }

extern unsigned long __platform_strlcpy(char *, const char *, unsigned long);
unsigned long strlcpy(char *d, const char *s, unsigned long n) { return __platform_strlcpy(d, s, n); }

extern unsigned long __platform_strlcat(char *, const char *, unsigned long);
unsigned long strlcat(char *d, const char *s, unsigned long n) { return __platform_strlcat(d, s, n); }

extern void *__platform_memmove(void *, const void *, unsigned long);
void *memmove(void *d, const void *s, unsigned long n) { return __platform_memmove(d, s, n); }
void *memcpy(void *d, const void *s, unsigned long n) { return __platform_memmove(d, s, n); }

extern void *__platform_memset(void *, int, unsigned long);
void *memset(void *s, int c, unsigned long n) { return __platform_memset(s, c, n); }

extern void __platform_bzero(void *, unsigned long);
void bzero(void *s, unsigned long n) { __platform_bzero(s, n); }

extern int __platform_memcmp(const void *, const void *, unsigned long);
int memcmp(const void *a, const void *b, unsigned long n) { return __platform_memcmp(a, b, n); }

extern char *__platform_strchr(const char *, int);
char *strchr(const char *s, int c) { return __platform_strchr(s, c); }

extern char *__platform_strstr(const char *, const char *);
char *strstr(const char *h, const char *n) { return __platform_strstr(h, n); }

extern unsigned long __platform_strnlen(const char *, unsigned long);
unsigned long strnlen(const char *s, unsigned long m) { return __platform_strnlen(s, m); }

/* strncat — built on top of strlen + strlcpy */
char *strncat(char *dst, const char *src, unsigned long n) {
    unsigned long dlen = __platform_strlen(dst);
    unsigned long i;
    for (i = 0; i < n && src[i]; i++) dst[dlen + i] = src[i];
    dst[dlen + i] = 0;
    return dst;
}

/* strerror — minimal */
char *strerror(int err) {
    static char buf[32];
    buf[0] = 'E'; buf[1] = 'r'; buf[2] = 'r'; buf[3] = ' ';
    int i = 4;
    if (err == 0) { buf[i++] = '0'; }
    else { while (err > 0 && i < 30) { buf[i++] = '0' + err % 10; err /= 10; } }
    buf[i] = 0;
    return buf;
}

/* Syscall wrappers */
extern int __getpid(void); int getpid(void) { return __getpid(); }
extern int __getppid(void); int getppid(void) { return __getppid(); }
extern unsigned __getuid(void); unsigned getuid(void) { return __getuid(); }
extern unsigned __geteuid(void); unsigned geteuid(void) { return __geteuid(); }
extern unsigned __getgid(void); unsigned getgid(void) { return __getgid(); }
extern unsigned __getegid(void); unsigned getegid(void) { return __getegid(); }
extern int __fork(void); int fork(void) { return __fork(); }
extern int __execve(const char *, char *const *, char *const *);
int execve(const char *p, char *const a[], char *const e[]) { return __execve(p, a, e); }
extern void __exit(int) __attribute__((noreturn));
void _exit(int s) { __exit(s); }
extern int __pipe(int[2]); int pipe(int f[2]) { return __pipe(f); }
extern int __kill(int, int); int kill(int p, int s) { return __kill(p, s); }
extern int __wait4(int, int *, int, void *);
int waitpid(int p, int *s, int o) { return __wait4(p, s, o, 0); }
int wait(int *s) { return __wait4(-1, s, 0, 0); }
extern int __open(const char *, int, int);
int open(const char *p, int f, ...) { return __open(p, f, 0666); }
extern int __unlink(const char *); int unlink(const char *p) { return __unlink(p); }
extern int __rmdir(const char *); int rmdir(const char *p) { return __rmdir(p); }
extern void *__mmap(void *, unsigned long, int, int, int, long long);
void *mmap(void *a, unsigned long l, int p, int f, int fd, long long o) { return __mmap(a,l,p,f,fd,o); }
extern int __munmap(void *, unsigned long);
int munmap(void *a, unsigned long l) { return __munmap(a, l); }
extern int __ioctl(int, unsigned long, void *);
int ioctl(int fd, unsigned long r, ...) { __builtin_va_list ap; __builtin_va_start(ap,r); void *a=__builtin_va_arg(ap,void*); __builtin_va_end(ap); return __ioctl(fd,r,a); }
extern int __fcntl(int, int, long);
int fcntl(int fd, int c, ...) { __builtin_va_list ap; __builtin_va_start(ap,c); long a=__builtin_va_arg(ap,long); __builtin_va_end(ap); return __fcntl(fd,c,a); }

/* errno support */

/* sprintf/snprintf — removed format-copying stubs.
 * Real implementations are in libsystem_c. */
int vsscanf(const char *s, const char *f, __builtin_va_list ap) { return 0; }

/* __isthreaded — set to 1 when pthreads are initialized */
int __isthreaded = 0;
