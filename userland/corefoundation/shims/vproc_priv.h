/* vproc_priv.h — Panthera shim for CoreFoundation build */
#ifndef _VPROC_PRIV_SHIM_H
#define _VPROC_PRIV_SHIM_H

/* vproc.h from SDK already declares vproc_transaction_begin/end.
 * This header provides the private-only extensions CF needs. */

typedef enum {
    _vproc_post_fork_ping_type_exec = 0,
} _vproc_post_fork_ping_type_t;

static inline void _vproc_post_fork_ping(void) {}

#endif /* _VPROC_PRIV_SHIM_H */
