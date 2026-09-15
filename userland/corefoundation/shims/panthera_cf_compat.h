/* panthera_cf_compat.h — Master compatibility header for CF build on Panthera */
#ifndef _PANTHERA_CF_COMPAT_H
#define _PANTHERA_CF_COMPAT_H

#include <Availability.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

/* Panthera sysroot headers can be newer than the host SDK Availability.h and
 * may contain bridgeOS availability annotations (e.g. bridgeos(7.0)).
 * Define the missing platform mapping for Clang availability attributes
 * if not already provided by the SDK. */
#ifndef __API_AVAILABLE_PLATFORM_bridgeos
#define __API_AVAILABLE_PLATFORM_bridgeos(x) bridgeos,introduced=x
#endif

/* Panthera's xpc/base.h is intentionally minimal and does not define the
 * attribute macros expected by newer SDK vproc.h headers. Provide the small
 * subset needed for CF builds when the real XPC headers are absent. */
#ifndef XPC_DEPRECATED
#define XPC_DEPRECATED(m) __attribute__((deprecated(m)))
#endif

#ifndef XPC_EXPORT
#define XPC_EXPORT extern __attribute__((visibility("default")))
#endif

#ifndef XPC_WARN_RESULT
#define XPC_WARN_RESULT __attribute__((warn_unused_result))
#endif

#ifndef XPC_NONNULL2
#define XPC_NONNULL2 __attribute__((nonnull(2)))
#endif

/* pthread_main_thread_np — not in SDK headers, but exists in libpthread */
extern pthread_t pthread_main_thread_np(void);

/* pthread_main_np — return 1 for single-threaded Panthera bootstrap */
extern int pthread_main_np(void);

/* pthread_mach_thread_np — return mach_thread_self for Panthera */
extern mach_port_t pthread_mach_thread_np(pthread_t t);

/* pthread_threadid_np — return fixed thread id */
extern int pthread_threadid_np(pthread_t t, uint64_t *thread_id);

/* dyld_image_path_containing_address — private dyld API */
extern const char *dyld_image_path_containing_address(const void *addr);

/* OSAtomicCompareAndSwap64Barrier — deprecated, but CF uses it directly */
extern bool OSAtomicCompareAndSwap64Barrier(int64_t oldValue, int64_t newValue,
    volatile int64_t *theValue);

/* Undo the hardened strcpy macro from Panthera's sysroot string.h.
 * Apple's CF source uses standard two-argument strcpy(dst, src).
 * Our XNU string.h redefines it to a 3-arg macro for bounds checking,
 * which breaks CF's usage. Undefine it here so the compiler builtin is used. */
#ifdef strcpy
#undef strcpy
#endif

#endif /* _PANTHERA_CF_COMPAT_H */
