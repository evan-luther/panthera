/* Panthera shim: os/bsd.h */
#ifndef _OS_BSD_H
#define _OS_BSD_H

#include <stddef.h>
#include <stdint.h>

static inline size_t os_proc_available_memory(void) {
    return (size_t)512 * 1024 * 1024;  /* 512MB default */
}

#endif /* _OS_BSD_H */
