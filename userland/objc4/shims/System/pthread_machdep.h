/* Panthera shim: System/pthread_machdep.h
 * Provides direct TSD key definitions and access functions.
 * When using PTHREADS threading, these are not called at runtime.
 */
#ifndef _SYSTEM_PTHREAD_MACHDEP_H
#define _SYSTEM_PTHREAD_MACHDEP_H

#include <pthread.h>

/* Direct TSD slot numbers for ObjC framework keys */
#define __PTK_FRAMEWORK_OBJC_KEY0  110
#define __PTK_FRAMEWORK_OBJC_KEY1  111
#define __PTK_FRAMEWORK_OBJC_KEY2  112
#define __PTK_FRAMEWORK_OBJC_KEY3  113
#define __PTK_FRAMEWORK_OBJC_KEY4  114
#define __PTK_FRAMEWORK_OBJC_KEY5  115

#define _PTHREAD_TSD_SLOT_PTHREAD_SELF 0

static inline int _pthread_has_direct_tsd(void) { return 0; }

static inline void *_pthread_getspecific_direct(unsigned long slot)
{
    (void)slot;
    return NULL;
}

static inline void _pthread_setspecific_direct(unsigned long slot, void *val)
{
    (void)slot;
    (void)val;
}

static inline void pthread_key_init_np(int key, void (*destructor)(void *))
{
    (void)key;
    (void)destructor;
}

#endif /* _SYSTEM_PTHREAD_MACHDEP_H */
