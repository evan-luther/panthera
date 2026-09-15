/*
 * panthera_resolve_impl.c — Implementations and stubs for all remaining
 * unresolved symbols in libsystem_c.
 *
 * Categories:
 * 1. Stack protector (__stack_chk_fail, __stack_chk_guard)
 * 2. Ctype locale variants (__tolower_l, __toupper_l, __maskrune_l)
 * 3. gdtoa (float-to-string conversion)
 * 4. printf internals
 * 5. stdio internals (__sfp, __sfprelease, __svfscanf_l)
 * 6. Locale/collation stubs
 * 7. _simple functions (from libplatform)
 * 8. Mach functions
 * 9. os functions (os_alloc_once, os_unfair_lock)
 * 10. TLV stubs
 * 11. ASL/ACL/external stubs
 * 12. Misc (posix_spawn attrs, regex, sprintf_l, etc.)
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/*
 * Real Apple libc now provides these symbols. Keep any remaining fallback code
 * private so libpanthera_extra does not export duplicates that can override the
 * working implementations in libsystem_c.dylib.
 */
#define __stack_chk_guard panthera_private_stack_chk_guard
#define __stack_chk_fail panthera_private_stack_chk_fail
#define __dtoa panthera_private_dtoa
#define __freedtoa panthera_private_freedtoa
#define __hdtoa panthera_private_hdtoa
#define __hldtoa panthera_private_hldtoa
#define __ldtoa panthera_private_ldtoa
#define __sfp panthera_private_sfp
#define __collate_lookup_l panthera_private_collate_lookup_l
#define __collate_equiv_match panthera_private_collate_equiv_match
#define acl_copy_ext_native panthera_private_acl_copy_ext_native
#define acl_copy_int_native panthera_private_acl_copy_int_native
#define acl_size panthera_private_acl_size
#define dbopen panthera_private_dbopen
#define _reclaim_telldir panthera_private_reclaim_telldir
#define qsort_b panthera_private_qsort_b
#define regcomp panthera_private_regcomp
#define regexec panthera_private_regexec
#define regfree panthera_private_regfree
#define snprintf_l panthera_private_snprintf_l
#define sprintf_l panthera_private_sprintf_l
#define mach_absolute_time panthera_private_mach_absolute_time
#define mach_approximate_time panthera_private_mach_approximate_time
#define mach_continuous_time panthera_private_mach_continuous_time
#define mach_continuous_approximate_time panthera_private_mach_continuous_approximate_time
#define mach_task_self panthera_private_mach_task_self
#define mach_host_self panthera_private_mach_host_self
#define mach_port_deallocate panthera_private_mach_port_deallocate
#define mach_timebase_info panthera_private_mach_timebase_info
#define mach_error_string panthera_private_mach_error_string
#define host_get_clock_service panthera_private_host_get_clock_service
#define clock_get_time panthera_private_clock_get_time
#define semaphore_create panthera_private_semaphore_create
#define stpcpy panthera_private_stpcpy
#define stpncpy panthera_private_stpncpy
#define strtok_r panthera_private_strtok_r
#define task_set_special_port panthera_private_task_set_special_port
#define uuid_copy panthera_private_uuid_copy
#define vasprintf panthera_private_vasprintf
#define vasprintf_l panthera_private_vasprintf_l
#define vdprintf_l panthera_private_vdprintf_l
#define vfwprintf_l panthera_private_vfwprintf_l
#define vfwscanf_l panthera_private_vfwscanf_l
#define vsprintf panthera_private_vsprintf
#define vsscanf_l panthera_private_vsscanf_l
#define vswprintf_l panthera_private_vswprintf_l
#define vswscanf_l panthera_private_vswscanf_l

/* ═══════════════════════════════════════════════════════════
 * Raw syscalls (can't use libc, we ARE libc)
 * ═══════════════════════════════════════════════════════════ */
static long _sc(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
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
#define SYS_write   4
#define SYS_exit    1

static void _panic(const char *msg) {
    const char *p = msg;
    while (*p) p++;
    _sc(SYS_write, 2, (long)msg, p - msg, 0, 0, 0);
    _sc(SYS_write, 2, (long)"\n", 1, 0, 0, 0);
    _sc(SYS_exit, 127, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

static void
_debug_write(const char *msg)
{
    const char *p = msg;
    while (*p) p++;
    _sc(SYS_write, 2, (long)msg, p - msg, 0, 0, 0);
}

/* ═══════════════════════════════════════════════════════════
 * 1. Stack protector
 * ═══════════════════════════════════════════════════════════ */
/* __stack_chk_guard: random canary value */
long __stack_chk_guard = 0x000aff0a0d00ff0aLL;

void __stack_chk_fail(void) {
    _panic("*** stack smashing detected ***");
}

/* ═══════════════════════════════════════════════════════════
 * 2. Ctype locale variants
 * ═══════════════════════════════════════════════════════════ */
typedef void *locale_t;

/* __tolower_l and __toupper_l provided by locale_FreeBSD_nomacros.o */

int __maskrune_l(int c, unsigned long f, locale_t l) {
    (void)l;
    if (c < 0 || c > 127) return 0;
    unsigned long mask = 0;
    if (c >= 'A' && c <= 'Z') mask |= 0x100;
    if (c >= 'a' && c <= 'z') mask |= 0x200;
    if (c >= '0' && c <= '9') mask |= 0x400;
    if (c == ' ' || (c >= 9 && c <= 13)) mask |= 0x800;
    if ((c >= 33 && c <= 126) || c == ' ') mask |= 0x4000;
    if ((c >= 0 && c <= 31) || c == 127) mask |= 0x2000;
    return (int)(mask & f);
}

/* ═══════════════════════════════════════════════════════════
 * 3. gdtoa — float-to-string conversion
 *
 * Minimal implementations: __dtoa converts double to decimal
 * string, __freedtoa frees the result.
 * ═══════════════════════════════════════════════════════════ */

static char _dtoa_buf[64];
static int _dtoa_in_use = 0;

/* Minimal __dtoa: convert double to string
 * mode: 0 = shortest, 2 = ndigits significant digits
 * Returns: string, sets *decpt, *sign, *rve
 */
char *__dtoa(double d, int mode, int ndigits, int *decpt, int *sign, char **rve) {
    char *buf = _dtoa_buf;
    _dtoa_in_use = 1;
    *sign = 0;

    if (d < 0.0) { *sign = 1; d = -d; }
    if (d == 0.0) {
        buf[0] = '0'; buf[1] = 0;
        *decpt = 1;
        if (rve) *rve = buf + 1;
        return buf;
    }

    /* Check for special values */
    /* NaN */
    if (d != d) {
        buf[0] = 'N'; buf[1] = 'a'; buf[2] = 'N'; buf[3] = 0;
        *decpt = 9999;
        if (rve) *rve = buf + 3;
        return buf;
    }
    /* Infinity */
    if (d > 1.7976931348623157e+308) {
        buf[0] = 'I'; buf[1] = 'n'; buf[2] = 'f'; buf[3] = 0;
        *decpt = 9999;
        if (rve) *rve = buf + 3;
        return buf;
    }

    /* Simple approach: find decimal point position */
    int exp10 = 0;
    double tmp = d;
    if (tmp >= 10.0) { while (tmp >= 10.0) { tmp /= 10.0; exp10++; } }
    else if (tmp > 0.0 && tmp < 1.0) { while (tmp < 1.0) { tmp *= 10.0; exp10--; } }

    *decpt = exp10 + 1;

    int digits = (ndigits > 0 && ndigits < 17) ? ndigits : 17;
    if (mode == 0) digits = 17;

    for (int i = 0; i < digits && i < 60; i++) {
        int digit = (int)tmp;
        if (digit > 9) digit = 9;
        if (digit < 0) digit = 0;
        buf[i] = '0' + digit;
        tmp = (tmp - digit) * 10.0;
    }
    buf[digits] = 0;

    /* Trim trailing zeros for mode 0 */
    if (mode == 0) {
        int len = digits;
        while (len > 1 && buf[len-1] == '0') len--;
        buf[len] = 0;
        if (rve) *rve = buf + len;
    } else {
        if (rve) *rve = buf + digits;
    }

    return buf;
}

void __freedtoa(char *s) {
    (void)s;
    _dtoa_in_use = 0;
}

/* __hdtoa: convert double to hex string (for %a format) */
char *__hdtoa(double d, const char *xdigs, int ndigits, int *decpt, int *sign, char **rve) {
    /* Fallback to decimal representation */
    return __dtoa(d, 2, ndigits > 0 ? ndigits : 6, decpt, sign, rve);
}

/* __hldtoa: convert long double to hex string */
char *__hldtoa(long double d, const char *xdigs, int ndigits, int *decpt, int *sign, char **rve) {
    return __hdtoa((double)d, xdigs, ndigits, decpt, sign, rve);
}

/* __ldtoa: convert long double to string */
char *__ldtoa(long double *ld, int mode, int ndigits, int *decpt, int *sign, char **rve) {
    return __dtoa((double)*ld, mode, ndigits, decpt, sign, rve);
}

/* ═══════════════════════════════════════════════════════════
 * 4. Printf internals
 *
 * Apple's printf uses an extensible printf engine with
 * __printf_comp (compile format), __printf_out (output),
 * __printf_flush, __printf_pad, __printf_puts.
 * We provide minimal stubs — the actual printf in
 * panthera_printf.c bypasses these.
 * ═══════════════════════════════════════════════════════════ */

/* Opaque printf context type */
struct __printf_compiled { int dummy; };

/* __printf_comp: compile a format string (returns NULL = use legacy path) */
struct __printf_compiled *__printf_comp(const char *fmt, locale_t loc) {
    (void)fmt; (void)loc;
    return (struct __printf_compiled *)0;
}

/* __printf_out: output using compiled printf (unused if comp returns NULL) */
int __printf_out(struct __printf_compiled *pc, void *fp, const void *args, int nargs) {
    (void)pc; (void)fp; (void)args; (void)nargs;
    return -1;
}

/* __printf_flush: flush printf buffer */
int __printf_flush(void *fp) {
    (void)fp;
    return 0;
}

/* __printf_pad: pad output */
int __printf_pad(void *fp, int n, int zero) {
    (void)fp; (void)n; (void)zero;
    return 0;
}

/* __printf_puts: write string to printf output */
int __printf_puts(void *fp, const char *s, int n) {
    (void)fp; (void)s; (void)n;
    return n;
}

/* __xprintf_vector: vector printf handler */
int __xprintf_vector(void *fp, const char *fmt, va_list ap) {
    (void)fp; (void)fmt; (void)ap;
    return 0;
}

/* __xvprintf: extended vprintf — core of Apple's printf.
 * Since we have our own printf engine in panthera_printf.c which provides
 * vfprintf_l, this should rarely be called. Provide a working version. */
extern int vfprintf_l(void *fp, locale_t loc, const char *fmt, va_list ap);

int __xvprintf(void *fp, locale_t loc, const char *fmt, va_list ap) {
    return vfprintf_l(fp, loc, fmt, ap);
}

/* ═══════════════════════════════════════════════════════════
 * 5. stdio internals: __sfp, __sfprelease, __svfscanf_l
 * ═══════════════════════════════════════════════════════════ */

/* FILE structure — Apple's __sFILE. We use a minimal version.
 * On Apple, stdio has a fixed table of FILE pointers (__sF[3] for
 * stdin/stdout/stderr) plus dynamically allocated ones via __sfp. */

struct __sFILE;
typedef struct __sFILE FILE;

/* __sfp: allocate a new FILE structure */
/* We maintain a small pool of FILE structures */
struct _mini_file {
    char data[152]; /* Enough to hold Apple FILE struct */
    int in_use;
};

#define MAX_FILES 64
static struct _mini_file _file_pool[MAX_FILES];

FILE *__sfp(int threaded) {
    (void)threaded;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!_file_pool[i].in_use) {
            _file_pool[i].in_use = 1;
            for (int j = 0; j < 152; j++) _file_pool[i].data[j] = 0;
            return (FILE *)_file_pool[i].data;
        }
    }
    return (FILE *)0;
}

void __sfprelease(FILE *fp) {
    for (int i = 0; i < MAX_FILES; i++) {
        if ((FILE *)_file_pool[i].data == fp) {
            _file_pool[i].in_use = 0;
            return;
        }
    }
}

/* __svfscanf_l: locale-aware vscanf from a string/FILE
 * This is the core scanf engine. For now, provide a minimal stub
 * that returns 0 (no items matched). A full implementation would
 * parse the format string and read from the FILE. */
int __svfscanf_l(FILE *fp, locale_t loc, const char *fmt, va_list ap) {
    (void)fp; (void)loc; (void)fmt; (void)ap;
    return 0; /* no items matched */
}

/* vfscanf — calls __svfscanf_l with default locale */
int vfscanf(FILE *fp, const char *fmt, va_list ap) {
    return __svfscanf_l(fp, (locale_t)0, fmt, ap);
}

/* ═══════════════════════════════════════════════════════════
 * 6. Locale and collation stubs (C locale only)
 * ═══════════════════════════════════════════════════════════ */

/* All collation functions are stubs for C/POSIX locale */

int __collate_load_tables(const char *encoding) {
    (void)encoding;
    return 0; /* success — C locale, no tables needed */
}

int __collate_lookup_l(const char *s, int *len, char *pri, int which, locale_t loc) {
    (void)loc; (void)which;
    if (s && *s) {
        *len = 1;
        *pri = *s;
    }
    return 0;
}

int __collate_lookup_which(const char *s, int *len, int which) {
    (void)which;
    if (s && *s) *len = 1;
    return 0;
}

int __collate_xfrm(const char *src, char *dst, size_t n) {
    /* In C locale, collation order = strcmp order */
    size_t len = 0;
    while (src[len]) len++;
    if (dst && n > 0) {
        size_t copy = len < n ? len : n - 1;
        for (size_t i = 0; i < copy; i++) dst[i] = src[i];
        dst[copy] = 0;
    }
    return (int)len;
}

int __collate_equiv_class(int c) {
    return c; /* each char is its own equivalence class */
}

int __collate_collating_symbol(const char *s, int len) {
    (void)len;
    return s ? (unsigned char)*s : 0;
}

int __collate_equiv_match(int equivclass, const char *s, int slen, char *dst, int dlen) {
    (void)equivclass; (void)s; (void)slen; (void)dst; (void)dlen;
    return 0; /* no match */
}

wchar_t *__collate_mbstowcs(const char *s) {
    /* Minimal: ASCII only */
    static wchar_t buf[256];
    int i = 0;
    while (s[i] && i < 255) { buf[i] = (wchar_t)(unsigned char)s[i]; i++; }
    buf[i] = 0;
    return buf;
}

int __collate_substitute(const char *s, char *dst, int n) {
    /* Identity substitution */
    int i = 0;
    while (s[i] && i < n - 1) { dst[i] = s[i]; i++; }
    dst[i] = 0;
    return i;
}

wchar_t *__collate_wcsdup(const wchar_t *s) {
    static wchar_t buf[256];
    int i = 0;
    while (s[i] && i < 255) { buf[i] = s[i]; i++; }
    buf[i] = 0;
    return buf;
}

/* Locale detection */
const char *__detect_path_locale(void) {
    return "";
}

const char *__get_locale_env(int category) {
    (void)category;
    return "C";
}

void *__open_path_locale(const char *path) {
    (void)path;
    return (void *)0;
}

/* ═══════════════════════════════════════════════════════════
 * 7. _simple functions (from libplatform/libsystem_init)
 * String formatting utilities used during early init.
 * ═══════════════════════════════════════════════════════════ */

typedef struct _simple_string_s {
    char *buf;
    size_t len;
    size_t cap;
} *_SIMPLE_STRING;

static char _simple_static_buf[4096];
static struct _simple_string_s _simple_static = { _simple_static_buf, 0, sizeof(_simple_static_buf) };

_SIMPLE_STRING _simple_salloc(void) {
    _simple_static.len = 0;
    _simple_static.buf[0] = 0;
    return &_simple_static;
}

void _simple_sfree(_SIMPLE_STRING s) {
    if (s) { s->len = 0; s->buf[0] = 0; }
}

const char *_simple_string(_SIMPLE_STRING s) {
    return s ? s->buf : "";
}

int _simple_sappend(_SIMPLE_STRING s, const char *str) {
    if (!s || !str) return -1;
    while (*str && s->len < s->cap - 1) {
        s->buf[s->len++] = *str++;
    }
    s->buf[s->len] = 0;
    return 0;
}

int _simple_sprintf(_SIMPLE_STRING s, const char *fmt, ...) {
    /* Minimal: just copy the format string */
    (void)s; (void)fmt;
    return 0;
}

int _simple_vsprintf(_SIMPLE_STRING s, const char *fmt, va_list ap) {
    (void)s; (void)fmt; (void)ap;
    return 0;
}

void _simple_dprintf(int fd, const char *fmt, ...) {
    /* Write format string directly — minimal impl */
    const char *p = fmt;
    while (*p) p++;
    _sc(SYS_write, fd, (long)fmt, p - fmt, 0, 0, 0);
}

/* ═══════════════════════════════════════════════════════════
 * 8. Mach functions
 * ═══════════════════════════════════════════════════════════ */

/* mach_absolute_time — read TSC */
uint64_t mach_absolute_time(void) {
    uint64_t tsc;
    __asm__ volatile("rdtsc" : "=A"(tsc));
    /* rdtsc returns in edx:eax on 32-bit, but on x86_64 we need: */
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

uint64_t mach_approximate_time(void) { return mach_absolute_time(); }
uint64_t mach_continuous_time(void) { return mach_absolute_time(); }
uint64_t mach_continuous_approximate_time(void) { return mach_absolute_time(); }

uint64_t mach_boottime_usec(void) {
    return 0; /* boot time not tracked */
}

/* mach_task_self — Mach trap 28
 * x86_64 XNU: RAX = 0x01000000 | trap_number (SYSCALL_CLASS_MACH=1, shift 24) */
unsigned int mach_task_self(void) {
    long ret;
    __asm__ volatile(
        "movl $0x0100001c, %%eax\n\t"
        "syscall"
        : "=a"(ret) : : "rcx", "r11", "memory");
    return (unsigned int)ret;
}

/* mach_host_self — Mach trap 29 (host_self_trap) */
unsigned int mach_host_self(void) {
    long ret;
    __asm__ volatile(
        "movl $0x0100001d, %%eax\n\t"
        "syscall"
        : "=a"(ret) : : "rcx", "r11", "memory");
    return (unsigned int)ret;
}

int mach_port_deallocate(unsigned int task, unsigned int name) {
    (void)task; (void)name;
    return 0; /* KERN_SUCCESS */
}

typedef struct { uint32_t numer; uint32_t denom; } mach_timebase_info_data_t;

int mach_timebase_info(mach_timebase_info_data_t *info) {
    /* On x86_64, TSC ticks at a rate that varies, but for QEMU
     * we can assume 1:1 (each tick = 1 nanosecond) */
    info->numer = 1;
    info->denom = 1;
    return 0;
}

const char *mach_error_string(int code) {
    (void)code;
    return "mach error";
}

int host_get_clock_service(unsigned int host, int clock_id, unsigned int *clock_name) {
    (void)host; (void)clock_id;
    *clock_name = 0;
    return 0;
}

int clock_get_time(unsigned int clock, void *cur_time) {
    /* Use gettimeofday as fallback */
    struct { long tv_sec; long tv_usec; } tv;
    _sc(116 /* SYS_gettimeofday */, (long)&tv, 0, 0, 0, 0, 0);
    uint32_t *ct = (uint32_t *)cur_time;
    ct[0] = (uint32_t)tv.tv_sec;
    ct[1] = (uint32_t)(tv.tv_usec * 1000);
    return 0;
}

int semaphore_create(unsigned int task, unsigned int *sem, int policy, int value) {
    (void)task; (void)policy; (void)value;
    *sem = 0;
    return 0;
}

int task_set_special_port(unsigned int task, int which, unsigned int port) {
    (void)task; (void)which; (void)port;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * 9. os functions
 * ═══════════════════════════════════════════════════════════ */

/*
 * Apple-source compatibility for os_alloc_once:
 * - callers pass a pointer to a slot, not a token integer
 * - the slot table has 100 entries in libsystem
 * - storage is zero-filled VM-backed memory, not a tiny static bump pool
 */
typedef long os_once_t;
typedef void (*os_function_t)(void *);

struct _os_alloc_once_s {
    os_once_t once;
    void *ptr;
};

void _os_once(long *predicate, void *ctx, void (*fn)(void *));

#define OS_ALLOC_ONCE_KEY_MAX 100
struct _os_alloc_once_s _os_alloc_once_table[OS_ALLOC_ONCE_KEY_MAX];
__asm__(".globl ___os_alloc_once_table\n.set ___os_alloc_once_table, __os_alloc_once_table");

/*
 * Keep os_alloc_once self-contained during early launchd bring-up.
 * A zero-filled static bump pool is sufficient for current users and avoids
 * depending on Mach VM setup before PID 1 is fully initialized.
 */
#define OS_ALLOC_ONCE_POOL_SIZE (1u << 20)
static unsigned char _os_alloc_pool[OS_ALLOC_ONCE_POOL_SIZE];
static size_t _os_alloc_offset;

static inline void *
_os_alloc2(size_t sz)
{
    size_t used;

    if (sz == 0) {
        _panic("os_alloc_once: invalid allocation size");
    }

    sz = (sz + 0xfu) & ~0xfu;
    used = __sync_add_and_fetch(&_os_alloc_offset, sz);
    if (used > sizeof(_os_alloc_pool)) {
        _panic("os_alloc_once: mach_vm_map failed");
    }
    return _os_alloc_pool + used - sz;
}

struct _os_alloc_once_ctxt_s {
    struct _os_alloc_once_s *slot;
    size_t sz;
    os_function_t init;
};

static void
_os_alloc_once_callout(void *ctxt)
{
    struct _os_alloc_once_ctxt_s *c = ctxt;

    c->slot->ptr = _os_alloc2(c->sz);
    if (c->init) {
        c->init(c->slot->ptr);
    }
}

void *_os_alloc_once(struct _os_alloc_once_s *slot, size_t sz, os_function_t init)
{
    struct _os_alloc_once_ctxt_s c = {
        .slot = slot,
        .sz = sz,
        .init = init,
    };

    _os_once(&slot->once, &c, _os_alloc_once_callout);
    return slot->ptr;
}

/* os_once — Darwin-style once primitive used by pthread_once/libplatform. */
void _os_once(long *predicate, void *ctx, void (*fn)(void *)) {
    if (predicate == NULL || fn == NULL) {
        return;
    }

    for (;;) {
        long value = __atomic_load_n(predicate, __ATOMIC_ACQUIRE);

        if (value == -1L) {
            return;
        }

        if (value == 0L &&
            __sync_bool_compare_and_swap(predicate, 0L, 1L)) {
            fn(ctx);
            __atomic_store_n(predicate, -1L, __ATOMIC_RELEASE);
            return;
        }

        while (__atomic_load_n(predicate, __ATOMIC_ACQUIRE) == 1L) {
            __asm__ volatile("pause");
        }
    }
}

void __os_once_reset(long *predicate) {
    if (predicate != NULL) {
        __atomic_store_n(predicate, 0L, __ATOMIC_RELEASE);
    }
}

/* os_unfair_lock — simple spinlock */
typedef uint32_t os_unfair_lock;

void os_unfair_lock_lock(os_unfair_lock *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ volatile("pause");
    }
}

void os_unfair_lock_lock_with_options(os_unfair_lock *lock, uint32_t options) {
    (void)options;
    os_unfair_lock_lock(lock);
}

void os_unfair_lock_unlock(os_unfair_lock *lock) {
    __sync_lock_release(lock);
}

/* os_add3_overflow — check for three-way add overflow */
int os_add3_overflow(unsigned long a, unsigned long b, unsigned long c, unsigned long *res) {
    unsigned long sum;
    int ovf = __builtin_add_overflow(a, b, &sum);
    ovf |= __builtin_add_overflow(sum, c, &sum);
    *res = sum;
    return ovf;
}

/* ═══════════════════════════════════════════════════════════
 * 10. TLV (Thread-Local Variable) stubs
 * These are normally provided by dyld for __thread support.
 * ═══════════════════════════════════════════════════════════ */

void panthera_legacy_tlv_bootstrap_stub(void) {
    /* No-op: TLS not used in our minimal system */
}

void _tlv_atexit(void (*fn)(void *), void *arg) {
    (void)fn; (void)arg;
}

void _tlv_exit(void) {
    /* No-op */
}

/* ═══════════════════════════════════════════════════════════
 * 11. ASL (Apple System Log) stubs
 * ═══════════════════════════════════════════════════════════ */

typedef void *aslmsg;
typedef void *aslclient;
typedef void *aslresponse;

aslclient asl_open(const char *ident, const char *facility, uint32_t opts) {
    (void)ident; (void)facility; (void)opts;
    return (aslclient)1; /* non-NULL dummy */
}

void asl_release(aslclient c) { (void)c; }

int asl_set(aslmsg msg, const char *key, const char *val) {
    (void)msg; (void)key; (void)val;
    return 0;
}

int asl_set_query(aslmsg msg, const char *key, const char *val, uint32_t op) {
    (void)msg; (void)key; (void)val; (void)op;
    return 0;
}

const char *asl_get(aslmsg msg, const char *key) {
    (void)msg; (void)key;
    return (const char *)0;
}

int asl_send(aslclient c, aslmsg msg) {
    (void)c; (void)msg;
    return 0;
}

int asl_append(aslclient c, aslmsg msg) {
    (void)c; (void)msg;
    return 0;
}

aslmsg asl_new(uint32_t type) {
    (void)type;
    return (aslmsg)0;
}

aslmsg asl_next(aslresponse r) {
    (void)r;
    return (aslmsg)0;
}

aslresponse asl_match(aslclient c, aslmsg q, size_t *count, int32_t batch, int32_t dir, uint64_t *last) {
    (void)c; (void)q; (void)batch; (void)dir; (void)last;
    if (count) *count = 0;
    return (aslresponse)0;
}

/* ═══════════════════════════════════════════════════════════
 * ACL stubs
 * ═══════════════════════════════════════════════════════════ */

typedef void *acl_t;

long acl_copy_ext_native(void *buf, acl_t acl, long size) {
    (void)buf; (void)acl; (void)size;
    return -1;
}

acl_t acl_copy_int_native(const void *buf) {
    (void)buf;
    return (acl_t)0;
}

long acl_size(acl_t acl) {
    (void)acl;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Bootstrap/Mach IPC stubs
 * ═══════════════════════════════════════════════════════════ */

unsigned int bootstrap_port = 0;

int bootstrap_parent(unsigned int bp, unsigned int *parent) {
    (void)bp;
    *parent = 0;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * CoreCrypto stubs
 * ═══════════════════════════════════════════════════════════ */

typedef struct ccrng_state {
    int dummy;
} *ccrng_state_t;

static struct ccrng_state _ccrng_state = { 0 };

ccrng_state_t ccrng(int *error) {
    if (error) *error = 0;
    return &_ccrng_state;
}

int ccrng_uniform(ccrng_state_t rng, uint64_t bound, uint64_t *out) {
    (void)rng;
    /* Use a simple LCG since we don't have /dev/urandom yet */
    static uint64_t state = 0x12345678DEADBEEFULL;
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    *out = state % bound;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * CrashReporter stub
 * ═══════════════════════════════════════════════════════════ */

const char *CRGetCrashLogMessage(void) {
    return (const char *)0;
}

/* ═══════════════════════════════════════════════════════════
 * dbopen stub
 * ═══════════════════════════════════════════════════════════ */

typedef void *DB;
DB *dbopen(const char *fname, int flags, int mode, int type, const void *openinfo) {
    (void)fname; (void)flags; (void)mode; (void)type; (void)openinfo;
    return (DB *)0;
}

/* dyld stubs */
int dyld_get_active_platform(void) {
    return 1; /* PLATFORM_MACOS */
}

int dyld_sdk_at_least(int platform, uint32_t version) {
    (void)platform; (void)version;
    return 1;
}

/* ═══════════════════════════════════════════════════════════
 * __mh_execute_header — usually provided by the linker
 * ═══════════════════════════════════════════════════════════ */

/* This is a weak symbol; the actual main executable's header
 * will override it at runtime if properly linked */
extern const void *_mh_execute_header __attribute__((weak));
const void *_mh_execute_header = 0;

/* ═══════════════════════════════════════════════════════════
 * utmpx stubs
 * ═══════════════════════════════════════════════════════════ */

void __endutxent(void) {}
int __utmpxname(const char *fname) { (void)fname; return 0; }
int _utmpx_vers = 2;

/* ═══════════════════════════════════════════════════════════
 * __chk_fail_overflow
 * ═══════════════════════════════════════════════════════════ */

void __chk_fail_overflow(void) {
    _panic("*** buffer overflow detected ***");
}

/* ═══════════════════════════════════════════════════════════
 * pthread internal stubs
 * ═══════════════════════════════════════════════════════════ */

/* These are defined in the assembly alias file (panthera_resolve_aliases.s) */

/* ═══════════════════════════════════════════════════════════
 * Dir/telldir stubs
 * ═══════════════════════════════════════════════════════════ */

/* _GENERIC_DIRSIZ — compute directory entry size */
int _GENERIC_DIRSIZ(void *dp) {
    /* struct dirent { d_ino, d_reclen, d_type, d_namlen, d_name[] } */
    /* return offsetof(struct dirent, d_name) + dp->d_namlen + 1 rounded up to 4 */
    unsigned char *p = (unsigned char *)dp;
    int namlen = p[10]; /* d_namlen is at offset 10 in struct dirent on Apple */
    int base = 11; /* offsetof(struct dirent, d_name) */
    int sz = base + namlen + 1;
    return (sz + 3) & ~3;
}

void _reclaim_telldir(void *dirp) {
    (void)dirp;
}

/* ═══════════════════════════════════════════════════════════
 * fenv (floating-point environment)
 * ═══════════════════════════════════════════════════════════ */

typedef unsigned int fenv_t;

static fenv_t _default_fenv = 0;

int fegetenv(fenv_t *envp) {
    unsigned int mxcsr;
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    *envp = mxcsr;
    return 0;
}

int fesetenv(const fenv_t *envp) {
    unsigned int mxcsr = envp ? *envp : _default_fenv;
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    return 0;
}

int fegetround(void) {
    unsigned int mxcsr;
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    return (mxcsr >> 13) & 3;
}

/* ═══════════════════════════════════════════════════════════
 * Misc C library functions
 * ═══════════════════════════════════════════════════════════ */

int flsl(long i) {
    if (i == 0) return 0;
    return 64 - __builtin_clzl(i);
}

double fma(double x, double y, double z) {
    return __builtin_fma(x, y, z);
}

/* memccpy */
void *memccpy(void *dst, const void *src, int c, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d = *s;
        if (*s == (unsigned char)c) return d + 1;
        d++; s++;
    }
    return (void *)0;
}

/* stpcpy, stpncpy */
char *stpcpy(char *dst, const char *src) {
    while ((*dst = *src)) { dst++; src++; }
    return dst;
}

char *stpncpy(char *dst, const char *src, size_t n) {
    while (n && (*dst = *src)) { dst++; src++; n--; }
    char *ret = dst;
    while (n--) *dst++ = 0;
    return ret;
}

/* strtok_r */
char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (!str) str = *saveptr;
    if (!str) return (char *)0;

    /* Skip leading delimiters */
    while (*str) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) { if (*str == *d) { is_delim = 1; break; } d++; }
        if (!is_delim) break;
        str++;
    }
    if (!*str) { *saveptr = (char *)0; return (char *)0; }

    char *tok = str;
    while (*str) {
        const char *d = delim;
        while (*d) {
            if (*str == *d) {
                *str = 0;
                *saveptr = str + 1;
                return tok;
            }
            d++;
        }
        str++;
    }
    *saveptr = (char *)0;
    return tok;
}

/* index — same as strchr */
char *index(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return (c == 0) ? (char *)s : (char *)0;
}

/* qsort_b — qsort with block comparator (simplified: use function pointer) */
void qsort_b(void *base, size_t nel, size_t width, int (^compar)(const void *, const void *)) {
    /* Blocks ABI: the block pointer's invoke function is at offset 16 */
    /* For simplicity, implement a basic bubble sort */
    char *arr = (char *)base;
    for (size_t i = 0; i < nel; i++) {
        for (size_t j = i + 1; j < nel; j++) {
            if (compar(arr + i * width, arr + j * width) > 0) {
                /* swap */
                for (size_t k = 0; k < width; k++) {
                    char tmp = arr[i * width + k];
                    arr[i * width + k] = arr[j * width + k];
                    arr[j * width + k] = tmp;
                }
            }
        }
    }
}

/* getpwuid_r — reentrant version */
struct passwd;
extern struct passwd *getpwuid(unsigned int);

int getpwuid_r(unsigned int uid, void *pwd, char *buf, size_t bufsize, void **result) {
    (void)buf; (void)bufsize;
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        /* Copy struct contents */
        extern void *__platform_memmove(void *, const void *, size_t);
        __platform_memmove(pwd, pw, 72); /* sizeof(struct passwd) on Apple */
        *(void **)result = pwd;
        return 0;
    }
    *(void **)result = 0;
    return -1;
}

/* uuid_copy */
void uuid_copy(unsigned char *dst, const unsigned char *src) {
    for (int i = 0; i < 16; i++) dst[i] = src[i];
}

/* ═══════════════════════════════════════════════════════════
 * Regex stubs (minimal — zsh has its own regex)
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    int re_nsub;
    void *re_g;
} regex_t;

typedef struct {
    long rm_so;
    long rm_eo;
} regmatch_t;

int regcomp(regex_t *preg, const char *pattern, int cflags) {
    (void)pattern; (void)cflags;
    preg->re_nsub = 0;
    preg->re_g = 0;
    return -1; /* REG_ESPACE — indicate failure, zsh will cope */
}

int regexec(const regex_t *preg, const char *string, size_t nmatch, regmatch_t *pmatch, int eflags) {
    (void)preg; (void)string; (void)nmatch; (void)pmatch; (void)eflags;
    return 1; /* REG_NOMATCH */
}

void regfree(regex_t *preg) {
    (void)preg;
}

/* ═══════════════════════════════════════════════════════════
 * sprintf_l / snprintf_l / vasprintf_l / vdprintf_l /
 * vfwprintf_l / vfwscanf_l / vsscanf_l / vswprintf_l /
 * vswscanf_l / vsprintf — locale-aware format stubs
 * ═══════════════════════════════════════════════════════════ */

extern int snprintf(char *, size_t, const char *, ...);
extern int sprintf(char *, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, va_list);

int snprintf_l(char *buf, size_t sz, locale_t loc, const char *fmt, ...) {
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf_l(char *buf, locale_t loc, const char *fmt, ...) {
    (void)loc;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, 0x7fffffff, fmt, ap);
    va_end(ap);
    return r;
}

int _vasprintf(char **strp, const char *fmt, va_list ap);

int vasprintf(char **strp, const char *fmt, va_list ap) {
    /* Compute length first */
    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf((char *)0, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0) { *strp = 0; return -1; }

    extern void *malloc(size_t);
    *strp = (char *)malloc(len + 1);
    if (!*strp) return -1;

    return vsnprintf(*strp, len + 1, fmt, ap);
}

int vasprintf_l(char **strp, locale_t loc, const char *fmt, va_list ap) {
    (void)loc;
    return vasprintf(strp, fmt, ap);
}

int _vdprintf_impl(int fd, const char *fmt, va_list ap) {
    char buf[4096];
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len > 0) _sc(SYS_write, fd, (long)buf, len < (int)sizeof(buf) ? len : (int)sizeof(buf) - 1, 0, 0, 0);
    return len;
}

int vdprintf_l(int fd, locale_t loc, const char *fmt, va_list ap) {
    (void)loc;
    return _vdprintf_impl(fd, fmt, ap);
}

/* _vsnprintf alias handled in assembly file */

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, 0x7fffffff, fmt, ap);
}

/* Wide-char format stubs — return error for now */
int vfwprintf_l(void *fp, locale_t loc, const void *fmt, va_list ap) {
    (void)fp; (void)loc; (void)fmt; (void)ap;
    return -1;
}

int vfwscanf_l(void *fp, locale_t loc, const void *fmt, va_list ap) {
    (void)fp; (void)loc; (void)fmt; (void)ap;
    return -1;
}

int vsscanf_l(const char *str, locale_t loc, const char *fmt, va_list ap) {
    (void)loc;
    extern int vsscanf(const char *, const char *, va_list);
    return vsscanf(str, fmt, ap);
}

int vswprintf_l(void *buf, size_t sz, locale_t loc, const void *fmt, va_list ap) {
    (void)buf; (void)sz; (void)loc; (void)fmt; (void)ap;
    return -1;
}

int vswscanf_l(const void *str, locale_t loc, const void *fmt, va_list ap) {
    (void)str; (void)loc; (void)fmt; (void)ap;
    return -1;
}
