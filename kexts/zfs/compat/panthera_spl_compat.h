/*
 * Panthera-only SPL compile compatibility.
 *
 * Some XNU exported inline helpers call panic() before kern/debug.h's
 * declaration is visible through the OpenZFS SPL include order. Provide the
 * canonical declaration early for SPL translation units.
 */
#ifndef PANTHERA_SPL_COMPAT_H
#define PANTHERA_SPL_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef XXH64
#undef XXH64
#endif

#ifndef _INT8_T
#define _INT8_T
typedef signed char int8_t;
#endif
#ifndef _INT16_T
#define _INT16_T
typedef short int16_t;
#endif
#ifndef _INT32_T
#define _INT32_T
typedef int int32_t;
#endif
#ifndef _INT64_T
#define _INT64_T
typedef long long int64_t;
#endif
#ifndef _UINT8_T
#define _UINT8_T
typedef unsigned char uint8_t;
#endif
#ifndef _UINT16_T
#define _UINT16_T
typedef unsigned short uint16_t;
#endif
#ifndef _UINT32_T
#define _UINT32_T
typedef unsigned int uint32_t;
#endif
#ifndef _UINT64_T
#define _UINT64_T
typedef unsigned long long uint64_t;
#endif
#ifndef _UINTPTR_T
#define _UINTPTR_T
typedef unsigned long uintptr_t;
#endif
#ifndef _INTPTR_T
#define _INTPTR_T
typedef long intptr_t;
#endif

#include <sys/types.h>
#include <libkern/OSKextLib.h>

#if defined(XNU_KERNEL_PRIVATE) && XNU_KERNEL_PRIVATE
extern OSKextLoadTag OSKextGetCurrentLoadTag(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef panic
extern void panic(const char *string, ...);
#endif
extern int sysctlbyname(const char *name, void *oldp, unsigned long *oldlenp,
    void *newp, unsigned long newlen);

#ifdef __cplusplus
}
#endif

#include <kern/thread.h>

#ifdef panic
#undef panic
extern void panic(const char *string, ...);
#endif

#endif /* PANTHERA_SPL_COMPAT_H */
