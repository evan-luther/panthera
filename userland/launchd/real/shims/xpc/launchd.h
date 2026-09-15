/*
 * xpc/launchd.h — XPC internal API shim for Panthera launchd.
 *
 * On real macOS, xpc_pipe_try_receive does XPC deserialization.
 * On Panthera, it's a thin wrapper around mach_msg + MIG demux.
 * The implementation is in panthera_xpc_pipe.c.
 */
#ifndef _XPC_LAUNCHD_H
#define _XPC_LAUNCHD_H

#include <xpc/xpc.h>
#include <mach/mach.h>

/* xpc_pipe_try_receive — receive a message on a Mach port set.
 * If the message is an XPC message, deserialize it into msg_out.
 * If it's a MIG message, dispatch through demux.
 * Returns 0 on success (msg_out may be NULL if MIG handled it).
 * Returns EINVAL on invalid message.
 */
int xpc_pipe_try_receive(mach_port_t port, xpc_object_t *msg_out,
    mach_port_t *reply_port_out,
    boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
    mach_msg_size_t max_msg_size, uint64_t flags);

/* xpc_pipe_routine_reply — send a reply to an XPC message. */
int xpc_pipe_routine_reply(xpc_object_t reply);

/* XPC domain management — not implemented on Panthera */
#if HAVE_XPC_DOMAINS
xpc_object_t xpc_domain_import_services(void *mgr, xpc_object_t services);
int xpc_domain_check_in(void);
const char *xpc_domain_get_service_name(void);
#endif

#endif /* _XPC_LAUNCHD_H */
