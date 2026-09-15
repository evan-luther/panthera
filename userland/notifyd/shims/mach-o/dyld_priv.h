/* Panthera shim: mach-o/dyld_priv.h */
#ifndef _DYLD_PRIV_H
#define _DYLD_PRIV_H

#include <stdbool.h>
#include <stddef.h>

static inline bool
_dyld_is_memory_immutable(const void *addr, size_t length) {
    (void)addr; (void)length;
    return false;
}

#endif /* _DYLD_PRIV_H */
