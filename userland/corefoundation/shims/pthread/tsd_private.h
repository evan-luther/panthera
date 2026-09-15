/* pthread/tsd_private.h — Panthera shim for CoreFoundation build */
#ifndef _PTHREAD_TSD_PRIVATE_SHIM_H
#define _PTHREAD_TSD_PRIVATE_SHIM_H

#include <pthread.h>

/* CF uses reserved TSD slot for per-thread data */
#define __PTK_FRAMEWORK_COREFOUNDATION_KEY5 55

/* Direct TSD access mapped to Panthera pthread TSD */
static inline void *_pthread_getspecific_direct(unsigned long slot) {
    return pthread_getspecific(slot);
}

static inline int _pthread_setspecific_direct(unsigned long slot, void *val) {
    return pthread_setspecific(slot, val);
}

#endif /* _PTHREAD_TSD_PRIVATE_SHIM_H */
