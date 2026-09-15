/* Panthera shim: bootstrap_priv.h */
#ifndef _BOOTSTRAP_PRIV_H
#define _BOOTSTRAP_PRIV_H

#include <servers/bootstrap.h>

#ifndef BOOTSTRAP_PRIVILEGED_SERVER
#define BOOTSTRAP_PRIVILEGED_SERVER 0
#endif

/* bootstrap_look_up2 — extended lookup with flags */
static inline kern_return_t
bootstrap_look_up2(mach_port_t bp, const char *service_name,
    mach_port_t *sp, pid_t target_pid, uint64_t flags)
{
    (void)target_pid; (void)flags;
    return bootstrap_look_up(bp, service_name, sp);
}

#endif /* _BOOTSTRAP_PRIV_H */
