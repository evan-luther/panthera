/*
 * xpc/private.h — XPC private types and APIs for Panthera launchd.
 */
#ifndef _XPC_PRIVATE_H
#define _XPC_PRIVATE_H

#include <xpc/xpc.h>
#include <stdint.h>

/* Private XPC APIs used by launchd */
#define XPC_PIPE_FLAG_PRIVILEGED 0x2

typedef void *xpc_pipe_t;
typedef void *xpc_service_type_t;
typedef uint32_t xpc_jetsam_band_t;

/* Jetsam band constants */
#define XPC_SERVICE_JETSAM_BAND_NONE 0

static inline xpc_pipe_t xpc_pipe_create(const char *name, uint64_t flags) {
    (void)name; (void)flags; return NULL;
}

static inline int xpc_pipe_routine(xpc_pipe_t pipe, xpc_object_t msg, xpc_object_t *reply) {
    (void)pipe; (void)msg; (void)reply; return 0;
}

#endif /* _XPC_PRIVATE_H */
