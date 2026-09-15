/* Panthera shim: os/alloc_once_private.h
 * Provides os_alloc_once used by notify_client.c for global state.
 */
#ifndef _OS_ALLOC_ONCE_PRIVATE_H
#define _OS_ALLOC_ONCE_PRIVATE_H

#include <stdlib.h>
#include <string.h>
#include <dispatch/dispatch.h>

/* Apple's os_alloc_once uses predefined slots. Libnotify uses slot 1. */
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY 1
#define OS_ALLOC_ONCE_KEY_MAX 16

typedef void (*os_function_t)(void *);

static void *_os_alloc_once_table[OS_ALLOC_ONCE_KEY_MAX];
static dispatch_once_t _os_alloc_once_preds[OS_ALLOC_ONCE_KEY_MAX];

static inline void *
os_alloc_once(unsigned int slot, size_t sz, os_function_t init)
{
    if (slot >= OS_ALLOC_ONCE_KEY_MAX) return NULL;
    dispatch_once(&_os_alloc_once_preds[slot], ^{
        _os_alloc_once_table[slot] = calloc(1, sz);
        if (init) init(_os_alloc_once_table[slot]);
    });
    return _os_alloc_once_table[slot];
}

#endif /* _OS_ALLOC_ONCE_PRIVATE_H */
