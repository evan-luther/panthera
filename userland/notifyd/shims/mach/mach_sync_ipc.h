/* Panthera shim: mach/mach_sync_ipc.h
 * Provides mach_msg2 and related sync IPC declarations.
 * MIG-generated stubs may reference this header.
 */
#ifndef _MACH_MACH_SYNC_IPC_H
#define _MACH_MACH_SYNC_IPC_H

#include <mach/mach.h>

/* mach_msg2 is a newer kernel trap. Fall back to mach_msg. */
#ifndef MACH_SEND_FILTER_NONFATAL
#define MACH_SEND_FILTER_NONFATAL 0x00010000
#endif

#ifndef MACH_SEND_PROPAGATE_QOS
#define MACH_SEND_PROPAGATE_QOS 0x00400000
#endif

#endif /* _MACH_MACH_SYNC_IPC_H */
