#ifndef PANTHERA_DISPATCH_COMPAT_H
#define PANTHERA_DISPATCH_COMPAT_H
#define _notify_server_

#ifndef __PTHREAD_EXPOSE_INTERNALS__
#define __PTHREAD_EXPOSE_INTERNALS__ 1
#endif
#ifndef _pthread_priority_has_qos
#define _pthread_priority_has_qos _pthread_priority_has_qos
#endif
#ifndef _pthread_priority_relpri
#define _pthread_priority_relpri _pthread_priority_relpri
#endif

#include <mach/error.h>
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/mach_time_private.h>
#include <mach/mach_sync_ipc.h>
#include <firehose/firehose_types_private.h>

#endif
