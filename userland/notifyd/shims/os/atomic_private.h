/* Panthera shim: os/atomic_private.h */
#ifndef _OS_ATOMIC_PRIVATE_H
#define _OS_ATOMIC_PRIVATE_H

#include <stdatomic.h>
#include <stdint.h>

/* Memory order keywords used by Apple's os_atomic macros */
#define os_atomic_or(p, v, m)   atomic_fetch_or_explicit(p, v, memory_order_relaxed)
#define os_atomic_xchg(p, v, m) atomic_exchange_explicit(p, v, memory_order_relaxed)
#define os_atomic_store(p, v, m) atomic_store_explicit(p, v, memory_order_relaxed)
#define os_atomic_load(p, m)    atomic_load_explicit(p, memory_order_relaxed)
#define os_atomic_inc(p, m)     (atomic_fetch_add_explicit(p, 1, memory_order_relaxed) + 1)
#define os_atomic_dec(p, m)     (atomic_fetch_sub_explicit(p, 1, memory_order_relaxed) - 1)
#define os_atomic_cmpxchg(p, e, v, m) ({ \
    __typeof__(e) _e = (e); \
    atomic_compare_exchange_strong_explicit(p, &_e, v, memory_order_relaxed, memory_order_relaxed); \
})
#define os_atomic_cmpxchgv(p, e, v, g, m) ({ \
    __typeof__(e) _e = (e); \
    bool _r = atomic_compare_exchange_strong_explicit(p, &_e, v, memory_order_relaxed, memory_order_relaxed); \
    *(g) = _e; \
    _r; \
})
#define os_atomic_thread_fence(m) atomic_thread_fence(memory_order_acquire)

/* Compiler hints */
#ifndef os_unlikely
#define os_unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef os_likely
#define os_likely(x) __builtin_expect(!!(x), 1)
#endif

#endif /* _OS_ATOMIC_PRIVATE_H */
