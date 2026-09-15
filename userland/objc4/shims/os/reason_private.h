/* Panthera shim: os/reason_private.h */
#ifndef _OS_REASON_PRIVATE_H
#define _OS_REASON_PRIVATE_H

#include <stdint.h>

#ifndef OS_REASON_LIBSYSTEM
#define OS_REASON_LIBSYSTEM 4
#endif

#ifndef OS_REASON_FLAG_FROM_USERSPACE
#define OS_REASON_FLAG_FROM_USERSPACE 0x4
#endif

#ifndef OS_REASON_FLAG_NO_CRASH_REPORT
#define OS_REASON_FLAG_NO_CRASH_REPORT 0x1
#endif

static inline void
os_fault_with_payload(uint32_t reason_namespace, uint64_t reason_code,
                      void *payload, uint32_t payload_size,
                      const char *reason_string, uint64_t reason_flags)
{
    (void)reason_namespace;
    (void)reason_code;
    (void)payload;
    (void)payload_size;
    (void)reason_string;
    (void)reason_flags;
}

#endif /* _OS_REASON_PRIVATE_H */
