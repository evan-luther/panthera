/*
 * Panthera pthread build shim for os/atomic_private.h.
 * The sysroot copy is intentionally simplified; for pthread we also need the
 * compare-exchange helpers to tolerate pointers to _Atomic(T) objects.
 */

#ifndef _PANTHERA_PTHREAD_OS_ATOMIC_PRIVATE_H
#define _PANTHERA_PTHREAD_OS_ATOMIC_PRIVATE_H

#include <os/base.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef os_atomic
#define os_atomic(type) type volatile _Atomic
#endif

#define _os_atomic_mo_relaxed            __ATOMIC_RELAXED
#define _os_atomic_mo_consume            __ATOMIC_CONSUME
#define _os_atomic_mo_acquire            __ATOMIC_ACQUIRE
#define _os_atomic_mo_release            __ATOMIC_RELEASE
#define _os_atomic_mo_acq_rel            __ATOMIC_ACQ_REL
#define _os_atomic_mo_seq_cst            __ATOMIC_SEQ_CST
#define _os_atomic_mo_dependency         __ATOMIC_ACQUIRE
#define _os_atomic_mo_compiler_acquire   __ATOMIC_RELAXED
#define _os_atomic_mo_compiler_release   __ATOMIC_RELAXED
#define _os_atomic_mo_compiler_acq_rel   __ATOMIC_RELAXED
#define _os_atomic_mo_relaxed_smp        __ATOMIC_RELAXED
#define _os_atomic_mo_consume_smp        __ATOMIC_CONSUME
#define _os_atomic_mo_acquire_smp        __ATOMIC_ACQUIRE
#define _os_atomic_mo_release_smp        __ATOMIC_RELEASE
#define _os_atomic_mo_acq_rel_smp        __ATOMIC_ACQ_REL
#define _os_atomic_mo_seq_cst_smp        __ATOMIC_SEQ_CST
#define _os_atomic_mo_dependency_smp     __ATOMIC_ACQUIRE
#define _os_atomic_mo_compiler_acquire_smp __ATOMIC_RELAXED
#define _os_atomic_mo_compiler_release_smp __ATOMIC_RELAXED
#define _os_atomic_mo_compiler_acq_rel_smp __ATOMIC_RELAXED

#define os_compiler_barrier(b...) __asm__ __volatile__("" ::: "memory")

#define os_atomic_thread_fence(m) \
	__atomic_thread_fence(_os_atomic_mo_##m)

#define os_atomic_barrier_before_lock_acquire() ((void)0)
#define os_atomic_init(p, v) ({ *(p) = (v); })

/*
 * Strip the _Atomic qualifier from simple scalar atomic objects before handing
 * them to the __atomic_* builtins. libpthread mutex paths use _Atomic(uint64_t)
 * fields directly, and the raw builtin rejects those pointers.
 */
#define _os_atomic_scalar_typeof(p) __typeof__((*(p)) + 0)
#define _os_atomic_scalar_ptr(p) ((_os_atomic_scalar_typeof(p) *)(void *)(p))

#define os_atomic_load(p, m) \
	__atomic_load_n(_os_atomic_scalar_ptr(p), _os_atomic_mo_##m)

#define os_atomic_store(p, v, m) \
	__atomic_store_n(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m)

#define os_atomic_xchg(p, v, m) \
	__atomic_exchange_n(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m)

#define os_atomic_cmpxchg(p, e, v, m) ({ \
	_os_atomic_scalar_typeof(p) _e = (e); \
	__atomic_compare_exchange_n(_os_atomic_scalar_ptr(p), &_e, v, 0, \
			_os_atomic_mo_##m, __ATOMIC_RELAXED); \
})

#define os_atomic_cmpxchgv(p, e, v, g, m) ({ \
	_os_atomic_scalar_typeof(p) _e = (e); \
	_Bool _r = __atomic_compare_exchange_n(_os_atomic_scalar_ptr(p), &_e, v, 0, \
			_os_atomic_mo_##m, __ATOMIC_RELAXED); \
	*(g) = _e; _r; \
})

#define os_atomic_cmpxchgvw(p, e, v, g, m) ({ \
	_os_atomic_scalar_typeof(p) _e = (e); \
	_Bool _r = __atomic_compare_exchange_n(_os_atomic_scalar_ptr(p), &_e, v, 1, \
			_os_atomic_mo_##m, __ATOMIC_RELAXED); \
	*(g) = _e; _r; \
})

#define os_atomic_add_orig(p, v, m)  __atomic_fetch_add(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m)
#define os_atomic_add(p, v, m)       (__atomic_fetch_add(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m) + (v))
#define os_atomic_inc_orig(p, m)     __atomic_fetch_add(_os_atomic_scalar_ptr(p), 1, _os_atomic_mo_##m)
#define os_atomic_inc(p, m)          (__atomic_fetch_add(_os_atomic_scalar_ptr(p), 1, _os_atomic_mo_##m) + 1)
#define os_atomic_sub_orig(p, v, m)  __atomic_fetch_sub(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m)
#define os_atomic_sub(p, v, m)       (__atomic_fetch_sub(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m) - (v))
#define os_atomic_dec_orig(p, m)     __atomic_fetch_sub(_os_atomic_scalar_ptr(p), 1, _os_atomic_mo_##m)
#define os_atomic_dec(p, m)          (__atomic_fetch_sub(_os_atomic_scalar_ptr(p), 1, _os_atomic_mo_##m) - 1)

#define os_atomic_and_orig(p, v, m)  __atomic_fetch_and(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m)
#define os_atomic_and(p, v, m)       (__atomic_fetch_and(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m) & (v))
#define os_atomic_or_orig(p, v, m)   __atomic_fetch_or(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m)
#define os_atomic_or(p, v, m)        (__atomic_fetch_or(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m) | (v))
#define os_atomic_xor_orig(p, v, m)  __atomic_fetch_xor(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m)
#define os_atomic_xor(p, v, m)       (__atomic_fetch_xor(_os_atomic_scalar_ptr(p), v, _os_atomic_mo_##m) ^ (v))

#define os_atomic_rmw_loop(p, _ov, _nv, m, ...) ({ \
	__typeof__(p) _p = (p); \
	_ov = os_atomic_load(_p, relaxed); \
	_Bool _result; \
	do { __VA_ARGS__; \
		_result = os_atomic_cmpxchgvw(_p, _ov, _nv, &_ov, m); \
	} while (__builtin_expect(!_result, 0)); \
	_result; \
})

#define os_atomic_rmw_loop_give_up(expr) ({ expr; __builtin_unreachable(); })

#define os_atomic_load_wide(p, m) os_atomic_load(p, m)
#define os_atomic_store_wide(p, v, m) os_atomic_store(p, v, m)

typedef unsigned long os_atomic_dependency_t;
#define OS_ATOMIC_DEPENDENCY_NONE ((os_atomic_dependency_t)0)
#define os_atomic_make_dependency(v) ((os_atomic_dependency_t)(uintptr_t)(v))
#define os_atomic_inject_dependency(p, d) (p)
#define os_atomic_load_with_dependency_on(p, e) os_atomic_load(p, relaxed)

#define OS_ATOMIC_HAS_LLSC  0
#define OS_ATOMIC_USE_LLSC  0
#define OS_ATOMIC_HAS_STARVATION_FREE_RMW 1

#endif /* _PANTHERA_PTHREAD_OS_ATOMIC_PRIVATE_H */
