/*
 * Minimal public Phase 1 header staged for Panthera's libIOKit bring-up.
 */

#ifndef __IOKIT_IOCFSERIALIZE_H
#define __IOKIT_IOCFSERIALIZE_H

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFData.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum {
    kIOCFSerializeToBinary = 0x00000001
};

CF_RETURNS_RETAINED
CFDataRef IOCFSerialize(CFTypeRef object, CFOptionFlags options);

#if defined(__cplusplus)
}
#endif

#endif
