/* Panthera shim: sys/reason.h
 * OS reason codes for abort_with_reason / os_fault_with_payload
 */
#ifndef _SYS_REASON_H
#define _SYS_REASON_H

#include <stdint.h>

#define OS_REASON_OBJC              8
#define OS_REASON_LIBSYSTEM         4
#define OS_REASON_LIBSYSTEM_CODE_FAULT 4

#define OS_REASON_FLAG_NO_CRASH_REPORT          0x1
#define OS_REASON_FLAG_FROM_USERSPACE            0x4
#define OS_REASON_FLAG_ONE_TIME_FAILURE          0x8
#define OS_REASON_FLAG_CONSISTENT_FAILURE        0x10
#define OS_REASON_FLAG_PAYLOAD_TRUNCATED         0x20

#ifdef __cplusplus
extern "C" {
#endif

/* abort_with_reason — used by objc_fatal. We provide a stub that just calls abort(). */
static inline void
abort_with_reason(uint32_t reason_namespace, uint64_t reason_code,
                  const char *reason_string, uint64_t reason_flags)
    __attribute__((noreturn));

static inline void
abort_with_reason(uint32_t reason_namespace, uint64_t reason_code,
                  const char *reason_string, uint64_t reason_flags)
{
    (void)reason_namespace;
    (void)reason_code;
    (void)reason_string;
    (void)reason_flags;
    __builtin_abort();
}

#ifdef __cplusplus
}
#endif

#endif /* _SYS_REASON_H */
