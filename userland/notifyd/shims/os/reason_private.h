/* Panthera shim: os/reason_private.h */
#ifndef _OS_REASON_PRIVATE_H
#define _OS_REASON_PRIVATE_H

#include <stdint.h>

#define OS_REASON_LIBSYSTEM 4
#define OS_REASON_FLAG_NO_CRASH_REPORT 0x1

static inline void
os_fault_with_payload(uint32_t reason_namespace, uint64_t reason_code,
                      void *payload, uint32_t payload_size,
                      const char *reason_string, uint64_t flags)
{
    (void)reason_namespace; (void)reason_code;
    (void)payload; (void)payload_size;
    (void)reason_string; (void)flags;
}

#endif /* _OS_REASON_PRIVATE_H */
