/* dispatch/private.h — Panthera shim for CoreFoundation build */
#ifndef _DISPATCH_PRIVATE_SHIM_H
#define _DISPATCH_PRIVATE_SHIM_H

#include <dispatch/dispatch.h>
#include <mach/mach.h>

#ifndef DISPATCH_QUEUE_OVERCOMMIT
#define DISPATCH_QUEUE_OVERCOMMIT 2
#endif

/* Dispatch private APIs used by CoreFoundation's run loop integration.
 * These are declared extern here and resolved at link time from libdispatch. */

extern dispatch_queue_t _dispatch_runloop_root_queue_create_4CF(const char *label, unsigned long flags);
extern mach_port_t _dispatch_runloop_root_queue_get_port_4CF(dispatch_queue_t queue);
extern void _dispatch_source_set_runloop_timer_4CF(dispatch_source_t source, dispatch_time_t start, uint64_t interval, uint64_t leeway);
extern bool _dispatch_runloop_root_queue_perform_4CF(dispatch_queue_t queue);
extern mach_port_t _dispatch_get_main_queue_port_4CF(void);
extern void _dispatch_main_queue_callback_4CF(mach_msg_header_t *msg);

#endif /* _DISPATCH_PRIVATE_SHIM_H */
