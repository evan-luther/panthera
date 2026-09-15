/* os/voucher_private.h — Panthera shim for CoreFoundation build */
#ifndef _OS_VOUCHER_PRIVATE_SHIM_H
#define _OS_VOUCHER_PRIVATE_SHIM_H

#include <mach/mach.h>

/* Voucher Mach message state tracking for run loop */
typedef unsigned int voucher_mach_msg_state_t;
#define VOUCHER_MACH_MSG_STATE_UNCHANGED 0

typedef void *voucher_t;
#define VOUCHER_CURRENT ((voucher_t)~0ul)

static inline voucher_mach_msg_state_t voucher_mach_msg_adopt(mach_msg_header_t *msg) {
    (void)msg;
    return VOUCHER_MACH_MSG_STATE_UNCHANGED;
}
static inline void voucher_mach_msg_revert(voucher_mach_msg_state_t s) { (void)s; }
static inline voucher_t voucher_copy(void) { return (voucher_t)0; }

#endif /* _OS_VOUCHER_PRIVATE_SHIM_H */
