/*
 * Minimal userland IOReturn surface for Panthera Phase 1.
 */

#ifndef __IOKIT_IORETURN_H
#define __IOKIT_IORETURN_H

#include <mach/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef kern_return_t IOReturn;

#ifndef sys_iokit
#define sys_iokit err_system(0x38)
#endif
#define sub_iokit_common err_sub(0)
#define iokit_common_err(ret) (sys_iokit | sub_iokit_common | (ret))

#define kIOReturnSuccess         KERN_SUCCESS
#define kIOReturnError           iokit_common_err(0x2bc)
#define kIOReturnNoMemory        iokit_common_err(0x2bd)
#define kIOReturnNoResources     iokit_common_err(0x2be)
#define kIOReturnIPCError        iokit_common_err(0x2bf)
#define kIOReturnNoDevice        iokit_common_err(0x2c0)
#define kIOReturnNotPrivileged   iokit_common_err(0x2c1)
#define kIOReturnBadArgument     iokit_common_err(0x2c2)
#define kIOReturnBadMessageID    iokit_common_err(0x2c6)
#define kIOReturnUnsupported     iokit_common_err(0x2c7)
#define kIOReturnInternalError   iokit_common_err(0x2c9)
#define kIOReturnIOError         iokit_common_err(0x2ca)
#define kIOReturnNotOpen         iokit_common_err(0x2cd)
#define kIOReturnBusy            iokit_common_err(0x2d5)
#define kIOReturnTimeout         iokit_common_err(0x2d6)
#define kIOReturnNotFound        iokit_common_err(0x2f0)

#ifdef __cplusplus
}
#endif

#endif
