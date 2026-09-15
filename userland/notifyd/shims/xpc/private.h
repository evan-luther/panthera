/* Panthera shim: xpc/private.h
 * Provides XPC event publisher types needed by notifyd.
 */
#ifndef _XPC_PRIVATE_H
#define _XPC_PRIVATE_H

#include <xpc/xpc.h>
#include <dispatch/dispatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <mach/mach.h>
#include <bsm/libbsm.h>

/* ---- XPC Event Publisher SPI ---- */

typedef struct _xpc_event_publisher_s *xpc_event_publisher_t;

typedef enum {
    XPC_EVENT_PUBLISHER_ACTION_ADD = 1,
    XPC_EVENT_PUBLISHER_ACTION_REMOVE = 2,
    XPC_EVENT_PUBLISHER_ACTION_INITIAL_BARRIER = 3,
} xpc_event_publisher_action_t;

typedef void (^xpc_event_publisher_handler_t)(
    xpc_event_publisher_action_t action,
    uint64_t event_token,
    xpc_object_t descriptor);

/* Stub implementation: just run the initial barrier immediately */
static inline xpc_event_publisher_t
xpc_event_publisher_create(const char *name, dispatch_queue_t queue) {
    (void)name; (void)queue;
    return (xpc_event_publisher_t)1; /* non-NULL sentinel */
}

static inline void
xpc_event_publisher_set_handler(xpc_event_publisher_t pub,
    xpc_event_publisher_handler_t handler) {
    (void)pub;
    /* Fire the initial barrier immediately so dispatch_mach_connect runs */
    if (handler) {
        handler(XPC_EVENT_PUBLISHER_ACTION_INITIAL_BARRIER, 0, NULL);
    }
}

static inline void
xpc_event_publisher_set_error_handler(xpc_event_publisher_t pub,
    void (^handler)(int)) {
    (void)pub; (void)handler;
}

static inline void
xpc_event_publisher_activate(xpc_event_publisher_t pub) {
    (void)pub;
}

static inline int
xpc_event_publisher_fire_noboost(xpc_event_publisher_t pub,
    uint64_t event_token, xpc_object_t payload) {
    (void)pub; (void)event_token; (void)payload;
    return 0;
}

static inline uint64_t
xpc_event_publisher_get_subscriber_asid(xpc_event_publisher_t pub,
    uint64_t event_token) {
    (void)pub; (void)event_token;
    return 0;
}

/* ---- XPC Entitlement SPI ---- */

static inline xpc_object_t
xpc_copy_entitlement_for_token(const char *entitlement, audit_token_t *token) {
    (void)entitlement; (void)token;
    return NULL; /* no entitlements on Panthera */
}

/* ---- Misc XPC private ---- */

static inline bool
_xpc_runtime_is_app_sandboxed(void) {
    return false;
}

#endif /* _XPC_PRIVATE_H */
