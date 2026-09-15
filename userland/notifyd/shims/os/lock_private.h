/* Panthera shim: os/lock_private.h */
#ifndef _OS_LOCK_PRIVATE_H
#define _OS_LOCK_PRIVATE_H

#include <os/lock.h>
#include <stdint.h>

#define OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION (0x00010000)
#define OS_UNFAIR_LOCK_ADAPTIVE_SPIN        (0x00040000)

typedef uint32_t os_unfair_lock_options_t;

static inline void
os_unfair_lock_lock_with_options(os_unfair_lock_t lock,
                                 os_unfair_lock_options_t options)
{
    (void)options;
    os_unfair_lock_lock(lock);
}

#endif /* _OS_LOCK_PRIVATE_H */
