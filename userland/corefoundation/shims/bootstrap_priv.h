/* bootstrap_priv.h — Panthera shim for CoreFoundation build */
#ifndef _BOOTSTRAP_PRIV_SHIM_H
#define _BOOTSTRAP_PRIV_SHIM_H

#include <mach/mach.h>

/* Bootstrap subset operations used by CFMessagePort and CFUserNotification */
#define BOOTSTRAP_SUBSET_PID 1
#define BOOTSTRAP_PER_PID_SERVICE 1

static inline kern_return_t bootstrap_look_up2(mach_port_t bp, const char *name,
    mach_port_t *sp, pid_t target_pid, unsigned int flags) {
    (void)bp; (void)name; (void)sp; (void)target_pid; (void)flags;
    return KERN_FAILURE;
}

static inline kern_return_t bootstrap_check_in(mach_port_t bp, const char *name, mach_port_t *sp) {
    (void)bp; (void)name; (void)sp;
    return KERN_FAILURE;
}

static inline kern_return_t bootstrap_register2(mach_port_t bp, const char *name, mach_port_t sp, unsigned int flags) {
    (void)bp; (void)name; (void)sp; (void)flags;
    return KERN_FAILURE;
}

#endif /* _BOOTSTRAP_PRIV_SHIM_H */
