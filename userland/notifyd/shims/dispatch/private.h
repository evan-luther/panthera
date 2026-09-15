/* Panthera shim: dispatch/private.h
 * Provides dispatch SPI types needed by notifyd and libnotify.
 * Only defines what's NOT already in the SDK public headers.
 */
#ifndef _DISPATCH_PRIVATE_PANTHERA_H
#define _DISPATCH_PRIVATE_PANTHERA_H

#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <mach/mig.h>

/* ---- Dispatch Mach SPI ---- */

#ifndef __DISPATCH_MACH_SPI__
#define __DISPATCH_MACH_SPI__ 1

typedef struct dispatch_mach_s *dispatch_mach_t;
typedef struct dispatch_mach_msg_s *dispatch_mach_msg_t;

typedef unsigned long dispatch_mach_reason_t;
#define DISPATCH_MACH_CONNECTED         1
#define DISPATCH_MACH_MESSAGE_RECEIVED  2
#define DISPATCH_MACH_MESSAGE_SENT      3
#define DISPATCH_MACH_MESSAGE_SEND_FAILED 4
#define DISPATCH_MACH_CANCELED          5
#define DISPATCH_MACH_BARRIER_COMPLETED 6

typedef void (*dispatch_mach_handler_function_t)(void *context,
    dispatch_mach_reason_t reason, dispatch_mach_msg_t message,
    mach_error_t error);

extern dispatch_mach_t
dispatch_mach_create_f(const char *label, dispatch_queue_t queue,
    void *context, dispatch_mach_handler_function_t handler);

extern void
dispatch_mach_connect(dispatch_mach_t channel, mach_port_t receive,
    mach_port_t send, dispatch_mach_msg_t checkin);

extern mach_msg_header_t *
dispatch_mach_msg_get_msg(dispatch_mach_msg_t message, size_t *size);

extern bool
dispatch_mach_mig_demux(void *context,
    const struct mig_subsystem *const *subsystems,
    size_t count, dispatch_mach_msg_t message);

#endif /* __DISPATCH_MACH_SPI__ */

/* ---- Memory pressure ---- */

#ifndef DISPATCH_MEMORYPRESSURE_PROC_LIMIT_WARN
#define DISPATCH_MEMORYPRESSURE_PROC_LIMIT_WARN  0x04
#endif

/* ---- Panthera no-op dispatch source stubs ---- */
/* When PANTHERA_NOOP_DISPATCH_SOURCES is set, signal/timer/memory-pressure
 * dispatch sources are no-op'd because Panthera's kqueue can't handle them.
 * The mach_msg port-set loop in panthera_dispatch_main handles all real IPC. */
#ifdef PANTHERA_NOOP_DISPATCH_SOURCES

/* Disable shared memory slots — shm_open/mmap don't work reliably.
 * getpagesize() is used to compute nslots; make it return 0 so nslots=0. */
#undef getpagesize
#define getpagesize() (0)

#undef dispatch_activate
#define dispatch_activate(obj) ((void)0)

#undef dispatch_set_context
#define dispatch_set_context(obj, ctx) ((void)0)

#undef dispatch_source_set_event_handler_f
#define dispatch_source_set_event_handler_f(obj, fn) ((void)0)

#undef dispatch_source_set_event_handler
#define dispatch_source_set_event_handler(obj, block) ((void)0)

#undef dispatch_source_set_timer
#define dispatch_source_set_timer(src, start, interval, leeway) ((void)0)

/* dispatch_source_create: return non-NULL dummy to satisfy assert() */
#undef dispatch_source_create
#define dispatch_source_create(type, handle, mask, queue) ((dispatch_source_t)(uintptr_t)1)

/* dispatch_async on workloop: run block synchronously */
#undef dispatch_async
#define dispatch_async(q, block) do { block(); } while(0)

#endif /* PANTHERA_NOOP_DISPATCH_SOURCES */

/* ---- Private dispatch APIs used by libnotify client ---- */

/* Return false: Panthera's kqueue can't handle EVFILT_PROC or
 * DISPATCH_SOURCE_TYPE_MACH_RECV, so the client must use the simple
 * polling path (no dispatch sources, no common port). */
static inline bool _dispatch_is_multithreaded(void) { return false; }
static inline bool _dispatch_is_fork_of_multithreaded_parent(void) { return false; }

#ifndef DISPATCH_QUEUE_OVERCOMMIT
#define DISPATCH_QUEUE_OVERCOMMIT 0x2ull
#endif

#endif /* _DISPATCH_PRIVATE_PANTHERA_H */
