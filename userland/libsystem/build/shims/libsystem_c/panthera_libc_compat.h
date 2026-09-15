#ifndef _PANTHERA_LIBC_COMPAT_H_
#define _PANTHERA_LIBC_COMPAT_H_

#include <stdarg.h>

/*
 * Apple's FreeBSD-derived libc sources still refer to __va_list in several
 * internal prototypes. Map that spelling to the SDK's va_list exactly so the
 * declarations in local.h match the function definitions.
 */
#ifndef __va_list
#define __va_list va_list
#endif

/*
 * Several libc-private headers only expose needed declarations when __LIBC__
 * is set. The standalone rebuild should compile in that mode too.
 */
#ifndef __LIBC__
#define __LIBC__ 1
#endif

/* FreeBSD DIR struct member naming: dd_fd -> __dd_fd (Apple uses __ prefix) */
#ifndef dd_fd
#define dd_fd __dd_fd
#endif
#ifndef dd_len
#define dd_len __dd_len
#endif
#ifndef dd_size
#define dd_size __dd_size
#endif
#ifndef dd_buf
#define dd_buf __dd_buf
#endif
#ifndef dd_loc
#define dd_loc __dd_loc
#endif
#ifndef dd_seek
#define dd_seek __dd_seek
#endif
#ifndef dd_flags
#define dd_flags __dd_flags
#endif
#ifndef dd_td
#define dd_td __dd_td
#endif
#ifndef dd_lock
#define dd_lock __dd_lock
#endif

/* FLOCKFILE / FUNLOCKFILE — thread-safe stdio (single-threaded for now) */
#ifndef FLOCKFILE
#define FLOCKFILE(fp) ((void)0)
#define FUNLOCKFILE(fp) ((void)0)
#endif

/* FreeBSD namespace.h / un-namespace.h compat */
#ifndef _NAMESPACE_H_
#define _NAMESPACE_H_
#endif
#ifndef _UN_NAMESPACE_H_
#define _UN_NAMESPACE_H_
#endif

/* __FBSDID is a no-op */
#ifndef __FBSDID
#define __FBSDID(x)
#endif

/* mach_task_self without pulling in mach_init.h via the force-include path */
#ifndef mach_task_self
extern unsigned int mach_task_self_;
#define mach_task_self() mach_task_self_
#endif

/* Weak references are not used in this cross build. */
#ifndef __weak_reference
#define __weak_reference(sym, alias)
#endif

/* FreeBSD libc internal thread wrappers */
#ifndef _pthread_mutex_lock
#define _pthread_mutex_lock pthread_mutex_lock
#define _pthread_mutex_unlock pthread_mutex_unlock
#define _pthread_mutex_trylock pthread_mutex_trylock
#endif
#ifndef _pthread_rwlock_rdlock
#define _pthread_rwlock_rdlock pthread_rwlock_rdlock
#define _pthread_rwlock_wrlock pthread_rwlock_wrlock
#define _pthread_rwlock_unlock pthread_rwlock_unlock
#endif
#ifndef _pthread_mutex_destroy
#define _pthread_mutex_destroy pthread_mutex_destroy
#endif
#ifndef _pthread_getspecific
#define _pthread_getspecific pthread_getspecific
#endif
#ifndef _pthread_setspecific
#define _pthread_setspecific pthread_setspecific
#endif
#ifndef _pthread_once
#define _pthread_once pthread_once
#endif

/* getdirentries compat */
#ifndef _getdirentries
#define _getdirentries getdirentries
#endif

/* FreeBSD _write / _read wrappers */
#ifndef _write
#define _write write
#endif
#ifndef _read
#define _read read
#endif
#ifndef _close
#define _close close
#endif
#ifndef _open
#define _open open
#endif
#ifndef _fstat
#define _fstat fstat
#endif
#ifndef _fcntl
#define _fcntl fcntl
#endif
#ifndef _sigprocmask
#define _sigprocmask sigprocmask
#endif
#ifndef _sigaction
#define _sigaction sigaction
#endif
#ifndef _execve
#define _execve execve
#endif
#ifndef _writev
#define _writev writev
#endif

/* ASL stubs */
#ifndef ASL_LEVEL_CRIT
#define ASL_LEVEL_CRIT 2
#define ASL_LEVEL_ERR 3
#define ASL_LEVEL_WARNING 4
#define ASL_LEVEL_NOTICE 5
#define ASL_LEVEL_INFO 6
#define ASL_LEVEL_DEBUG 7
#endif


#ifndef OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION
#define OS_UNFAIR_LOCK_DATA_SYNCHRONIZATION 0u
#endif
#ifndef os_unfair_lock_lock_with_options
#define os_unfair_lock_lock_with_options(lock, options) \
    os_unfair_lock_lock(lock)
#endif

/* Build libc itself with fortify wrappers disabled. */
#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif
#define _FORTIFY_SOURCE 0

/* TRE regex: tre.h checks #ifdef HAVE_LIBUTF8_H — must not be defined */
#ifdef HAVE_LIBUTF8_H
#undef HAVE_LIBUTF8_H
#endif

/* FreeBSD __isthreaded — provided by libc init */
#ifndef __isthreaded
extern int __isthreaded;
#endif

#include <stddef.h>
#include <stdint.h>

/* OSAtomic stubs (single-threaded; real atomics come with pthread) */
#ifndef OSAtomicDecrement32Barrier
static inline int32_t OSAtomicDecrement32Barrier(volatile int32_t *p) { return --(*p); }
static inline int32_t OSAtomicIncrement32Barrier(volatile int32_t *p) { return ++(*p); }
static inline int64_t OSAtomicDecrement64(volatile int64_t *p) { return --(*p); }
static inline int64_t OSAtomicIncrement64(volatile int64_t *p) { return ++(*p); }
static inline int32_t OSAtomicAdd32(int32_t amount, volatile int32_t *p) { return (*p += amount); }
static inline int32_t OSAtomicCompareAndSwap32(int32_t old, int32_t new_val, volatile int32_t *p) {
    if (*p == old) {
        *p = new_val;
        return 1;
    }
    return 0;
}
static inline int32_t OSAtomicCompareAndSwapPtrBarrier(void *old, void *new_val, void *volatile *p) {
    if (*p == old) {
        *p = new_val;
        return 1;
    }
    return 0;
}
#endif

/* db subsystem: byte-swap helpers */
#ifndef M_32_SWAP
#define M_32_SWAP(x) __builtin_bswap32(x)
#define M_16_SWAP(x) __builtin_bswap16(x)
#define P_32_SWAP(x) do { (x) = M_32_SWAP(x); } while (0)
#define P_16_SWAP(x) do { (x) = M_16_SWAP(x); } while (0)
#define P_32_COPY(src, dst) do { (dst) = (src); } while (0)
#define P_16_COPY(src, dst) do { (dst) = (src); } while (0)
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef memory_order_seq_cst
#define memory_order_seq_cst __ATOMIC_SEQ_CST
#endif
#ifndef atomic_thread_fence
#define atomic_thread_fence(order) __c11_atomic_thread_fence(order)
#endif

/* dyld functions used by chk_fail.c */
#ifndef dyld_get_active_platform
static inline unsigned int dyld_get_active_platform(void) { return 1; }
static inline int dyld_sdk_at_least(unsigned int p, unsigned int v) { (void)p; (void)v; return 1; }
#endif
#ifndef dyld_platform_version_macOS_10_9
#define dyld_platform_version_macOS_10_9 0x000a0900
#endif

/* os/transaction_private.h stub (used by libdarwin) */
#ifndef _OS_TRANSACTION_PRIVATE_H_
#define _OS_TRANSACTION_PRIVATE_H_
typedef void *os_transaction_t;
#define os_transaction_create(label) ((os_transaction_t)0)
static inline void _panthera_os_release_stub(void *x) { (void)x; }
#define os_release_transaction(x) _panthera_os_release_stub(x)
#endif

/* Locale: __ct_rune_t typedef */
#ifndef __ct_rune_t
typedef int __ct_rune_t;
#endif

/* TRE regex: TRE_REGEX_T_FIELD maps to Apple's regex_t::re_g */
#ifndef TRE_REGEX_T_FIELD
#define TRE_REGEX_T_FIELD re_g
#endif

#ifndef FTS_NOSTAT_TYPE
#define FTS_NOSTAT_TYPE 0x800
#endif

/* dyld_image_uuid_offset for backtrace.c */
#ifndef _DYLD_IMAGE_UUID_OFFSET_DEFINED
#define _DYLD_IMAGE_UUID_OFFSET_DEFINED
struct dyld_image_uuid_offset {
    const void *image;
    unsigned long long uuid[2];
    unsigned long long offsetInImage;
};
extern void _dyld_images_for_addresses(unsigned count, const void **addresses, struct dyld_image_uuid_offset *infos);
#endif


#ifndef __PTK_FRAMEWORK_SWIFT_KEY3
#define __PTK_FRAMEWORK_SWIFT_KEY3 103
#endif

#endif
