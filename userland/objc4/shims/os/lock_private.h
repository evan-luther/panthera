/* Panthera shim: os/lock_private.h
 * Provides private os_unfair_lock APIs needed by objc4.
 * When using PTHREADS threading, these are not called at runtime.
 */
#ifndef _OS_LOCK_PRIVATE_H
#define _OS_LOCK_PRIVATE_H

#include <os/lock.h>
#include <stdint.h>

/* Options for os_unfair_lock_lock_with_options */
#define OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION (0x00010000)
#define OS_UNFAIR_LOCK_ADAPTIVE_SPIN        (0x00040000)

typedef uint32_t os_unfair_lock_options_t;

static inline void
os_unfair_lock_lock_with_options_inline(os_unfair_lock_t lock,
                                        os_unfair_lock_options_t options)
{
    (void)options;
    os_unfair_lock_lock(lock);
}

static inline void
os_unfair_lock_unlock_inline(os_unfair_lock_t lock)
{
    os_unfair_lock_unlock(lock);
}

/* Recursive unfair lock */
typedef struct os_unfair_recursive_lock_s {
    os_unfair_lock ourl_lock;
    uint32_t       ourl_count;
} os_unfair_recursive_lock, *os_unfair_recursive_lock_t;

#define OS_UNFAIR_RECURSIVE_LOCK_INIT {OS_UNFAIR_LOCK_INIT, 0}

static inline void
os_unfair_recursive_lock_lock(os_unfair_recursive_lock_t lock)
{
    os_unfair_lock_lock(&lock->ourl_lock);
}

static inline bool
os_unfair_recursive_lock_trylock(os_unfair_recursive_lock_t lock)
{
    return os_unfair_lock_trylock(&lock->ourl_lock);
}

static inline void
os_unfair_recursive_lock_unlock(os_unfair_recursive_lock_t lock)
{
    os_unfair_lock_unlock(&lock->ourl_lock);
}

static inline bool
os_unfair_recursive_lock_tryunlock4objc(os_unfair_recursive_lock_t lock)
{
    os_unfair_lock_unlock(&lock->ourl_lock);
    return true;
}

static inline void
os_unfair_recursive_lock_unlock_forked_child(os_unfair_recursive_lock_t lock)
{
    lock->ourl_lock = (os_unfair_lock)OS_UNFAIR_LOCK_INIT;
    lock->ourl_count = 0;
}

#endif /* _OS_LOCK_PRIVATE_H */
