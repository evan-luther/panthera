/*
 * panthera_cache_bridge_stubs.c
 *
 * Stub implementations for symbols needed by shared-cache-resident libraries
 * whose canonical providers (libclosure, libsystem_trace, libsystem_notify, etc.)
 * are NOT in the cache. These stubs live in libpanthera_extra (which IS in the
 * cache) so the cache builder can resolve bindings at build time.
 *
 * At runtime, disk-loaded binaries get the real implementations via libSystem
 * re-exports. Cache-resident binaries get these stubs. For most of these
 * symbols the stubs are functionally adequate (the cache libraries don't
 * heavily exercise the full implementations).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

/* ── os_log (libsystem_trace) ──────────────────────────────────── */

typedef struct os_log_s { int _internal; } *os_log_t;

static struct os_log_s _os_log_default_obj = { 0 };
os_log_t _os_log_default_ptr __asm__("__os_log_default") = &_os_log_default_obj;

void _os_log_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) __asm__("__os_log_impl");
void _os_log_debug_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) __asm__("__os_log_debug_impl");
void _os_log_error_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) __asm__("__os_log_error_impl");
void _os_log_fault_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) __asm__("__os_log_fault_impl");
os_log_t os_log_create_stub(const char *subsystem, const char *category) __asm__("_os_log_create");
int os_log_type_enabled_stub(os_log_t log, int type) __asm__("_os_log_type_enabled");

void _os_log_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) { (void)dso; (void)log; (void)type; (void)fmt; (void)buf; (void)sz; }
void _os_log_debug_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) { (void)dso; (void)log; (void)type; (void)fmt; (void)buf; (void)sz; }
void _os_log_error_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) { (void)dso; (void)log; (void)type; (void)fmt; (void)buf; (void)sz; }
void _os_log_fault_impl_stub(void *dso, os_log_t log, int type, const char *fmt, void *buf, uint32_t sz) { (void)dso; (void)log; (void)type; (void)fmt; (void)buf; (void)sz; }
os_log_t os_log_create_stub(const char *subsystem, const char *category) { (void)subsystem; (void)category; return &_os_log_default_obj; }
int os_log_type_enabled_stub(os_log_t log, int type) { (void)log; (void)type; return 0; }

/* ── fpclassify (libsystem_c internals) ────────────────────────── */

int __fpclassifyd_stub(double x) __asm__("___fpclassifyd");
int __fpclassifyl_stub(long double x) __asm__("___fpclassifyl");

int __fpclassifyd_stub(double x) { return fpclassify(x); }
int __fpclassifyl_stub(long double x) { return fpclassify(x); }

/* ── tre regex internals ───────────────────────────────────────── */

int tre_tnfa_run_approx_stub(void) __asm__("_tre_tnfa_run_approx");
int tre_tnfa_run_approx_stub(void) { return -1; }

/* ── Semaphore (Mach) ──────────────────────────────────────────── */

typedef unsigned int mach_port_t;
typedef int kern_return_t;
typedef unsigned int mach_msg_timeout_t;
typedef struct { uint32_t tv_sec; uint32_t tv_nsec; } mach_timespec_t;

/* These call through to the Mach trap via syscall_thread_switch etc.
 * For cache binding purposes, provide forwarding stubs. */
extern kern_return_t semaphore_signal(mach_port_t sem) __asm__("_semaphore_signal");
extern kern_return_t semaphore_wait(mach_port_t sem) __asm__("_semaphore_wait");
extern kern_return_t semaphore_timedwait(mach_port_t sem, mach_timespec_t ts) __asm__("_semaphore_timedwait");

kern_return_t semaphore_signal(mach_port_t sem) { (void)sem; return 0; }
kern_return_t semaphore_wait(mach_port_t sem) { (void)sem; return 0; }
kern_return_t semaphore_timedwait(mach_port_t sem, mach_timespec_t ts) { (void)sem; (void)ts; return 0; }

/* ── Thread (Mach) ─────────────────────────────────────────────── */

extern kern_return_t thread_resume(mach_port_t thread) __asm__("_thread_resume");
extern kern_return_t thread_suspend(mach_port_t thread) __asm__("_thread_suspend");

kern_return_t thread_resume(mach_port_t thread) { (void)thread; return 0; }
kern_return_t thread_suspend(mach_port_t thread) { (void)thread; return 0; }

/* ── Thread (Mach) — wrappers to syscall stubs ─────────────────── */

extern kern_return_t syscall_thread_switch(mach_port_t, int, int);
extern kern_return_t thread_info_stub(mach_port_t t, int f, void *i, unsigned *c) __asm__("_thread_info");
extern kern_return_t thread_policy_stub(mach_port_t t, int p, void *b, unsigned c, int s) __asm__("_thread_policy");
extern kern_return_t thread_switch_stub(mach_port_t t, int opt, int time) __asm__("_thread_switch");

kern_return_t thread_info_stub(mach_port_t t, int f, void *i, unsigned *c) { (void)t; (void)f; (void)i; (void)c; return 0; }
kern_return_t thread_policy_stub(mach_port_t t, int p, void *b, unsigned c, int s) { (void)t; (void)p; (void)b; (void)c; (void)s; return 0; }
kern_return_t thread_switch_stub(mach_port_t t, int opt, int time) { return syscall_thread_switch(t, opt, time); }

/* ── Mach misc ─────────────────────────────────────────────────── */

extern kern_return_t clock_get_time(mach_port_t clock, mach_timespec_t *ts) __asm__("_clock_get_time");
kern_return_t clock_get_time(mach_port_t clock, mach_timespec_t *ts) {
    (void)clock;
    if (ts) { ts->tv_sec = 0; ts->tv_nsec = 0; }
    return 0;
}

extern uint64_t mach_absolute_time(void);
extern uint64_t mach_continuous_time(void);
extern kern_return_t mach_get_times(uint64_t *abs, uint64_t *cont,
    struct timespec *cal) __asm__("_mach_get_times");
kern_return_t mach_get_times(uint64_t *abs, uint64_t *cont, struct timespec *cal) {
    if (abs) *abs = mach_absolute_time();
    if (cont) *cont = mach_continuous_time();
    if (cal) {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) == 0) {
            cal->tv_sec = tv.tv_sec;
            cal->tv_nsec = tv.tv_usec * 1000;
        } else {
            cal->tv_sec = 0;
            cal->tv_nsec = 0;
        }
    }
    return 0;
}

extern int mach_task_is_self(mach_port_t task) __asm__("_mach_task_is_self");
int mach_task_is_self(mach_port_t task) { (void)task; return 1; }

/* ── Voucher (libdispatch) ─────────────────────────────────────── */

typedef int boolean_t;
typedef unsigned int mach_msg_size_t;
typedef struct mach_msg_header_s mach_msg_header_t;
typedef struct mach_msg_aux_header_s {
    mach_msg_size_t msgdh_size;
    uint32_t msgdh_reserved;
} mach_msg_aux_header_t;

typedef struct voucher_mach_msg_state_s *voucher_mach_msg_state_t;
#define VOUCHER_MACH_MSG_STATE_UNCHANGED ((voucher_mach_msg_state_t)~0ul)

extern voucher_mach_msg_state_t voucher_mach_msg_adopt(mach_msg_header_t *msg)
    __asm__("_voucher_mach_msg_adopt");
extern mach_msg_size_t voucher_mach_msg_fill_aux(mach_msg_aux_header_t *aux, mach_msg_size_t len)
    __asm__("_voucher_mach_msg_fill_aux");
extern boolean_t voucher_mach_msg_fill_aux_supported(void)
    __asm__("_voucher_mach_msg_fill_aux_supported");
extern void voucher_mach_msg_revert(voucher_mach_msg_state_t state)
    __asm__("_voucher_mach_msg_revert");

voucher_mach_msg_state_t voucher_mach_msg_adopt(mach_msg_header_t *msg) {
    (void)msg;
    return VOUCHER_MACH_MSG_STATE_UNCHANGED;
}

mach_msg_size_t voucher_mach_msg_fill_aux(mach_msg_aux_header_t *aux, mach_msg_size_t len) {
    (void)aux;
    (void)len;
    return 0;
}

boolean_t voucher_mach_msg_fill_aux_supported(void) {
    return 0;
}

void voucher_mach_msg_revert(voucher_mach_msg_state_t state) {
    (void)state;
}

/* ── Bootstrap / notify shims for libsystem_asl ────────────────── */

extern kern_return_t bootstrap_look_up_stub(unsigned int bp, const char *name, unsigned int *sp)
    __asm__("_bootstrap_look_up");
extern uint32_t notify_get_state_stub(int token, uint64_t *state)
    __asm__("_notify_get_state");
extern uint32_t notify_register_plain_stub(const char *name, int *token)
    __asm__("_notify_register_plain");

kern_return_t bootstrap_look_up_stub(unsigned int bp, const char *name, unsigned int *sp) {
    enum {
        kMachSendMsg = 1,
        kMachRcvMsg = 2,
        kMachMsgTypeCopySend = 19,
        kMachMsgTypeMakeSendOnce = 21,
        kMachMsgHBitsComplex = 0x80000000u,
        kBootstrapLookUp2 = 404,
        kBootstrapLookUp2Reply = 504,
    };

    extern mach_port_t mig_get_reply_port(void);
    extern kern_return_t mach_msg(void *msg, uint32_t option, uint32_t send_size,
        uint32_t rcv_size, mach_port_t rcv_name, mach_msg_timeout_t timeout,
        mach_port_t notify);

    if (!bp || !name || !sp)
        return 4; /* KERN_INVALID_ARGUMENT */

    *sp = 0;

    /*
     * Raw vproc_mig_look_up2 request.  This bridge is used by
     * shared-cache-resident clients until Panthera carries the full Apple
     * libbootstrap/liblaunch client surface in libSystem.
     */
    uint8_t msg[256];
    memset(msg, 0, sizeof(msg));

    mach_port_t reply_port = mig_get_reply_port();
    if (!reply_port)
        return 5; /* KERN_INVALID_RIGHT */

    *(uint32_t *)(msg + 0) = kMachMsgTypeCopySend | (kMachMsgTypeMakeSendOnce << 8);
    *(uint32_t *)(msg + 4) = 188;
    *(uint32_t *)(msg + 8) = bp;
    *(uint32_t *)(msg + 12) = reply_port;
    *(uint32_t *)(msg + 16) = 0;
    *(uint32_t *)(msg + 20) = kBootstrapLookUp2;

    msg[29] = 1; /* NDR char_rep */
    strlcpy((char *)(msg + 32), name, 128);
    *(int32_t *)(msg + 160) = 0;       /* target_pid */
    *(uint64_t *)(msg + 180) = 0;      /* flags */

    kern_return_t kr = mach_msg(msg, kMachSendMsg | kMachRcvMsg,
        188, sizeof(msg), reply_port, 0, 0);
    if (kr != 0)
        return kr;

    uint32_t bits = *(uint32_t *)(msg + 0);
    uint32_t reply_id = *(uint32_t *)(msg + 20);

    if (bits & kMachMsgHBitsComplex) {
        *sp = *(uint32_t *)(msg + 28);
        return 0;
    }

    if (reply_id == kBootstrapLookUp2Reply) {
        kern_return_t ret = *(kern_return_t *)(msg + 32);
        return ret ? ret : 5;
    }

    return 5;
}

uint32_t notify_get_state_stub(int token, uint64_t *state) {
    (void)token;
    if (state)
        *state = 0;
    return 0;
}

uint32_t notify_register_plain_stub(const char *name, int *token) {
    (void)name;
    if (token)
        *token = 0;
    return 0;
}

/* ── Notify no-op stubs (safe defaults for in-cache callers like libsystem_c).
 * Real implementations live in libsystem_notify.dylib (disk-loaded).
 * Programs that need real notify must link libsystem_notify before libSystem
 * so the disk-loaded symbols win resolution. ── */

extern uint32_t notify_register_check(const char *name, int *token) __asm__("_notify_register_check");
extern uint32_t notify_check(int token, int *check) __asm__("_notify_check");
extern uint32_t notify_peek(int token, uint32_t *val) __asm__("_notify_peek");
extern uint32_t notify_post(const char *name) __asm__("_notify_post");
extern uint32_t notify_cancel(int token) __asm__("_notify_cancel");

uint32_t notify_register_check(const char *name, int *token) { (void)name; if (token) *token = 0; return 0; }
uint32_t notify_check(int token, int *check) { (void)token; if (check) *check = 0; return 0; }
uint32_t notify_peek(int token, uint32_t *val) { (void)token; if (val) *val = 0; return 0; }
uint32_t notify_post(const char *name) { (void)name; return 0; }
uint32_t notify_cancel(int token) { (void)token; return 0; }

/* ── utmpx (libsystem_c internals) ─────────────────────────────── */

extern const char __utx_magic__[] __asm__("___utx_magic__");
const char __utx_magic__[] = "utmpx";

extern void *_utmpx_asl(void *ctx) __asm__("__utmpx_asl");
extern void *_utmpx_working_copy(void *ctx) __asm__("__utmpx_working_copy");
extern void *_utmpx32_64(void *u32, void *u64) __asm__("__utmpx32_64");
extern void *_utmpx64_32(void *u64, void *u32) __asm__("__utmpx64_32");

void *_utmpx_asl(void *ctx) { (void)ctx; return NULL; }
void *_utmpx_working_copy(void *ctx) { (void)ctx; return NULL; }
void *_utmpx32_64(void *u32, void *u64) { (void)u32; return u64; }
void *_utmpx64_32(void *u64, void *u32) { (void)u64; return u32; }

/* ── Mach errors table ─────────────────────────────────────────── */

struct _mach_error_entry { const char *name; int code; };
extern const struct _mach_error_entry _mach_errors[] __asm__("__mach_errors");
const struct _mach_error_entry _mach_errors[] = { { "KERN_SUCCESS", 0 }, { NULL, 0 } };

/* ── NOCANCEL variants ─────────────────────────────────────────── */
/* These are $NOCANCEL suffix variants — assembly aliases to the real syscalls.
 * Provided here as C stubs calling the regular versions. */

extern int open(const char *path, int flags, ...);
extern int fcntl(int fd, int cmd, ...);
extern unsigned int sleep(unsigned int seconds);
extern int sigsuspend(const void *mask);

int open_nocancel(const char *path, int flags) __asm__("_open$NOCANCEL");
int fcntl_nocancel(int fd, int cmd) __asm__("_fcntl$NOCANCEL");
int sigsuspend_nocancel(const void *m) __asm__("_sigsuspend$NOCANCEL");

int open_nocancel(const char *path, int flags) { return open(path, flags); }
int fcntl_nocancel(int fd, int cmd) { return fcntl(fd, cmd); }
int sigsuspend_nocancel(const void *m) { return sigsuspend(m); }
