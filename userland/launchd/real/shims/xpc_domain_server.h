/*
 * xpc_domain_server.h — Stub for XPC domain MIG subsystem.
 * The xpc_domain .defs file is not in the open-source drop.
 * Provide the minimum types and functions that runtime.c needs.
 */
#ifndef _XPC_DOMAIN_SERVER_H
#define _XPC_DOMAIN_SERVER_H

#include <mach/mach.h>

/* Stub unions — runtime.c uses these for message size calculation */
union __RequestUnion__xpc_domain_xpc_domain_subsystem {
    char _pad[4096];
};

union __ReplyUnion__xpc_domain_xpc_domain_subsystem {
    char _pad[4096];
};

/* xpc_domain_server — stub that rejects all messages */
static inline boolean_t xpc_domain_server(mach_msg_header_t *request, mach_msg_header_t *reply) {
    (void)request; (void)reply;
    return FALSE;
}

#endif /* _XPC_DOMAIN_SERVER_H */
