/*
 * panthera_pthread_simple.c — Simple single-threaded pthread for Panthera
 *
 * Apple's libsystem_pthread requires kernel thread support (bsdthread_*)
 * and complex TLS initialization. For our single-threaded bootstrap,
 * provide simple implementations.
 */

#include <stdint.h>
#include <stddef.h>

/* ═══════════════════════════════════════════════════════════
 * Thread-Specific Data (TSD)
 * ═══════════════════════════════════════════════════════════ */

#define MAX_PTHREAD_KEYS 256
static void *_tsd_values[MAX_PTHREAD_KEYS];
static void (*_tsd_destructors[MAX_PTHREAD_KEYS])(void *);
static int _tsd_next_key = 0;

int pthread_key_create(unsigned long *key, void (*destructor)(void *)) {
    if (_tsd_next_key >= MAX_PTHREAD_KEYS) return 11; /* EAGAIN */
    int k = _tsd_next_key++;
    _tsd_values[k] = 0;
    _tsd_destructors[k] = destructor;
    *key = (unsigned long)k;
    return 0;
}

int pthread_key_delete(unsigned long key) {
    if (key >= MAX_PTHREAD_KEYS) return 22;
    _tsd_values[key] = 0;
    _tsd_destructors[key] = 0;
    return 0;
}

void *pthread_getspecific(unsigned long key) {
    if (key >= MAX_PTHREAD_KEYS) return 0;
    return _tsd_values[key];
}

int pthread_setspecific(unsigned long key, const void *value) {
    if (key >= MAX_PTHREAD_KEYS) return 22;
    _tsd_values[key] = (void *)value;
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Mutex (no-ops for single-threaded)
 * ═══════════════════════════════════════════════════════════ */

typedef struct { int locked; } pthread_mutex_t;
typedef struct { int dummy; } pthread_mutexattr_t;

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    if (mutex) mutex->locked = 0;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (mutex) mutex->locked = 1;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (mutex) mutex->locked = 0;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (mutex && !mutex->locked) { mutex->locked = 1; return 0; }
    return 16; /* EBUSY */
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) { (void)attr; return 0; }
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) { (void)attr; return 0; }
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) { (void)attr; (void)type; return 0; }

/* ═══════════════════════════════════════════════════════════
 * Once
 * ═══════════════════════════════════════════════════════════ */

int pthread_once(long *once_control, void (*init_routine)(void)) {
    if (*once_control == 0) {
        init_routine();
        *once_control = 1;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Thread identity
 * ═══════════════════════════════════════════════════════════ */

static char _main_thread_storage[1024];

void *pthread_self(void) {
    return (void *)_main_thread_storage;
}

int pthread_equal(void *t1, void *t2) {
    return t1 == t2;
}

/* ═══════════════════════════════════════════════════════════
 * Thread creation (stubs — single-threaded)
 * ═══════════════════════════════════════════════════════════ */

int pthread_create(void **thread, const void *attr, void *(*start)(void *), void *arg) {
    (void)thread; (void)attr; (void)start; (void)arg;
    return 11; /* EAGAIN — can't create threads */
}

int pthread_join(void *thread, void **retval) {
    (void)thread; (void)retval;
    return 3; /* ESRCH */
}

int pthread_detach(void *thread) {
    (void)thread;
    return 0;
}

void pthread_exit(void *retval) {
    (void)retval;
    /* Can't exit the only thread — just return */
    for(;;);
}

/* ═══════════════════════════════════════════════════════════
 * Rwlock (no-ops)
 * ═══════════════════════════════════════════════════════════ */

typedef struct { int dummy; } pthread_rwlock_t;
typedef struct { int dummy; } pthread_rwlockattr_t;

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) {
    (void)rwlock; (void)attr; return 0;
}
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) { (void)rwlock; return 0; }
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) { (void)rwlock; return 0; }
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) { (void)rwlock; return 0; }
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) { (void)rwlock; return 0; }

/* ═══════════════════════════════════════════════════════════
 * Condition variables (minimal)
 * ═══════════════════════════════════════════════════════════ */

typedef struct { int dummy; } pthread_cond_t;
typedef struct { int dummy; } pthread_condattr_t;

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)cond; (void)attr; return 0;
}
int pthread_cond_destroy(pthread_cond_t *cond) { (void)cond; return 0; }
int pthread_cond_signal(pthread_cond_t *cond) { (void)cond; return 0; }
int pthread_cond_broadcast(pthread_cond_t *cond) { (void)cond; return 0; }
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    (void)cond; (void)mutex; return 0;
}

/* ═══════════════════════════════════════════════════════════
 * Signal mask
 * ═══════════════════════════════════════════════════════════ */

static long _sc(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(num | 0x2000000), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory", "cc");
    return ret;
}

int pthread_sigmask(int how, const void *set, void *oset) {
    return (int)_sc(329 /* SYS___pthread_sigmask */, how, (long)set, (long)oset);
}

/* ═══════════════════════════════════════════════════════════
 * Attr (stubs)
 * ═══════════════════════════════════════════════════════════ */

typedef struct { int detachstate; size_t stacksize; } pthread_attr_t;

int pthread_attr_init(pthread_attr_t *attr) {
    if (attr) { attr->detachstate = 0; attr->stacksize = 524288; }
    return 0;
}
int pthread_attr_destroy(pthread_attr_t *attr) { (void)attr; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *attr, int ds) {
    if (attr) attr->detachstate = ds; return 0;
}
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *ds) {
    if (ds) *ds = attr ? attr->detachstate : 0; return 0;
}
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t ss) {
    if (attr) attr->stacksize = ss; return 0;
}
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *ss) {
    if (ss) *ss = attr ? attr->stacksize : 524288; return 0;
}

/* $UNIX2003 aliases for pthread rwlock */
__asm__(".globl _pthread_rwlock_destroy$UNIX2003\n_pthread_rwlock_destroy$UNIX2003 = _pthread_rwlock_destroy");
__asm__(".globl _pthread_rwlock_rdlock$UNIX2003\n_pthread_rwlock_rdlock$UNIX2003 = _pthread_rwlock_rdlock");
__asm__(".globl _pthread_rwlock_wrlock$UNIX2003\n_pthread_rwlock_wrlock$UNIX2003 = _pthread_rwlock_wrlock");
__asm__(".globl _pthread_rwlock_unlock$UNIX2003\n_pthread_rwlock_unlock$UNIX2003 = _pthread_rwlock_unlock");
__asm__(".globl _pthread_cond_wait$UNIX2003\n_pthread_cond_wait$UNIX2003 = _pthread_cond_wait");

/* __pthread_sigmask — used by kernel syscall stubs */
__asm__(".globl ___pthread_sigmask\n___pthread_sigmask = _pthread_sigmask");

/* fchdir stub for pthread */
int pthread_fchdir_np(int fd) { (void)fd; return 0; }
