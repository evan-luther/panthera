#ifndef _PANTHERA_LIBDISPATCH_PRIORITY_PRIVATE_H
#define _PANTHERA_LIBDISPATCH_PRIORITY_PRIVATE_H

#ifndef __PTHREAD_EXPOSE_INTERNALS__
#define __PTHREAD_EXPOSE_INTERNALS__ 1
#endif

#include <bsd/pthread/priority_private.h>

#ifndef _pthread_priority_has_qos
#define _pthread_priority_has_qos _pthread_priority_has_qos
#endif
#ifndef _pthread_priority_relpri
#define _pthread_priority_relpri _pthread_priority_relpri
#endif

#endif
