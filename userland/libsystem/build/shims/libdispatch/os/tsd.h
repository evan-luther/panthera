/*
 * Copyright (c) 2012 Apple Inc. All rights reserved.
 * Panthera Darwin - libdispatch shadow os/tsd.h
 */

#ifndef OS_TSD_H
#define OS_TSD_H

#ifndef _PANTHERA_LIBDISPATCH_OS_TSD_H
#define _PANTHERA_LIBDISPATCH_OS_TSD_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>

/* The low nine slots of the TSD are reserved for libsyscall usage. */
#define __TSD_RESERVED_BASE 0
#define __TSD_RESERVED_MAX 9

#define __TSD_THREAD_SELF 0
#define __TSD_ERRNO 1
#define __TSD_MIG_REPLY 2
#define __TSD_MACH_THREAD_SELF 3
#define __TSD_THREAD_QOS_CLASS 4
#define __TSD_RETURN_TO_KERNEL 5
/* slot 6 is reserved for Windows/WINE compatibility reasons */
#define __TSD_PTR_MUNGE 7
#define __TSD_MACH_SPECIAL_REPLY 8
#define __TSD_SEMAPHORE_CACHE 9

#define __TSD_MACH_MSG_AUX 123

#define __TPIDR_CPU_NUM_SHIFT 0
#define __TPIDR_CPU_NUM_MASK 0x0000000000000fff
#define __TPIDR_CPU_CLUSTER_ID_SHIFT 12
#define __TPIDR_CPU_CLUSTER_ID_MASK 0x00000000000ff000

#if !defined(OS_GS_RELATIVE) && (defined(__i386__) || defined(__x86_64__))
#define OS_GS_RELATIVE __attribute__((address_space(256)))
#endif

__attribute__((always_inline))
static __inline__ unsigned int
_os_cpu_number(void)
{
#if defined(__arm__)
	uintptr_t p;
	__asm__ __volatile__ ("mrc	p15, 0, %[p], c13, c0, 3" : [p] "=&r" (p));
	return (unsigned int)(p & 0x3ul);
#elif defined(__arm64__)
	uint64_t p;
	__asm__ __volatile__ ("mrs %0, TPIDR_EL0" : "=r" (p));
	return (p & __TPIDR_CPU_NUM_MASK) >> __TPIDR_CPU_NUM_SHIFT;
#elif defined(__x86_64__) || defined(__i386__)
	struct { uintptr_t p1, p2; } p;
	__asm__ __volatile__ ("sidt %[p]" : [p] "=&m" (p));
	return (unsigned int)(p.p1 & 0xfff);
#else
	return 0;
#endif
}

__attribute__((always_inline))
static __inline__ unsigned int
_os_cpu_cluster_number(void)
{
#if defined(__arm64__)
	uint64_t p;
	__asm__ __volatile__ ("mrs %0, TPIDR_EL0" : "=r" (p));
	return (p & __TPIDR_CPU_CLUSTER_ID_MASK) >> __TPIDR_CPU_CLUSTER_ID_SHIFT;
#else
	return 0;
#endif
}

__attribute__((always_inline))
static __inline__ void*
_os_tsd_get_direct(unsigned long slot)
{
#if defined(__x86_64__)
	void *ret;
	__asm__("mov %%gs:%1, %0" : "=r" (ret) : "m" (*(void * OS_GS_RELATIVE *)(slot * sizeof(void *))));
	return ret;
#elif defined(__i386__)
	void *ret;
	__asm__("mov %%gs:%1, %0" : "=r" (ret) : "m" (*(void * OS_GS_RELATIVE *)(slot * sizeof(void *))));
	return ret;
#elif defined(__arm64__)
	void **tsd;
	uintptr_t base;
	__asm__("mrs %0, TPIDRRO_EL0" : "=r" (base));
	tsd = (void **)(base & ~0x7ULL);
	return tsd[slot];
#elif defined(__arm__)
	void **tsd;
	uintptr_t base;
	__asm__("mrc p15, 0, %0, c13, c0, 3" : "=r" (base));
	tsd = (void **)(base & ~0x3ULL);
	return tsd[slot];
#else
	return (void *)0;
#endif
}

__attribute__((always_inline))
static __inline__ int
_os_tsd_set_direct(unsigned long slot, void *val)
{
#if defined(__x86_64__)
	__asm__("mov %1, %%gs:%0" : "=m" (*(void * OS_GS_RELATIVE *)(slot * sizeof(void *))) : "r" (val));
	return 0;
#elif defined(__i386__)
	__asm__("mov %1, %%gs:%0" : "=m" (*(void * OS_GS_RELATIVE *)(slot * sizeof(void *))) : "r" (val));
	return 0;
#elif defined(__arm64__)
	void **tsd;
	uintptr_t base;
	__asm__("mrs %0, TPIDRRO_EL0" : "=r" (base));
	tsd = (void **)(base & ~0x7ULL);
	tsd[slot] = val;
	return 0;
#elif defined(__arm__)
	void **tsd;
	uintptr_t base;
	__asm__("mrc p15, 0, %0, c13, c0, 3" : "=r" (base));
	tsd = (void **)(base & ~0x3ULL);
	tsd[slot] = val;
	return 0;
#else
	return 0;
#endif
}

__attribute__((always_inline, const))
static __inline__ uintptr_t
_os_ptr_munge_token(void)
{
	return (uintptr_t)0;
}

__attribute__((always_inline, const))
static __inline__ uintptr_t
_os_ptr_munge(uintptr_t ptr)
{
	return ptr;
}
#define _OS_PTR_MUNGE(_ptr) _os_ptr_munge((uintptr_t)(_ptr))
#define _OS_PTR_UNMUNGE(_ptr) _os_ptr_munge((uintptr_t)(_ptr))

#endif /* _PANTHERA_LIBDISPATCH_OS_TSD_H */
#endif /* OS_TSD_H */
