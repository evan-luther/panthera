/* pthread/private.h — Panthera shim for CoreFoundation build */
#ifndef _PTHREAD_PRIVATE_SHIM_H
#define _PTHREAD_PRIVATE_SHIM_H

#include <pthread.h>

/* Private pthread API used by CFRunLoop for workqueue integration */
extern int _pthread_workqueue_init(void *func, int offset, int flags);

/* pthread QoS private API */
#ifndef PTHREAD_OVERRIDE_QOS_USERINTERACTIVE
#define PTHREAD_OVERRIDE_QOS_USERINTERACTIVE 6
#endif

#endif /* _PTHREAD_PRIVATE_SHIM_H */
