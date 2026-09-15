/*
 * Minimal public Phase 1 header staged for Panthera's libIOKit bring-up.
 */

#ifndef __IOKIT_IOCFUNSERIALIZE_H
#define __IOKIT_IOCFUNSERIALIZE_H

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFString.h>

#if defined(__cplusplus)
extern "C" {
#endif

CF_RETURNS_RETAINED
CFTypeRef IOCFUnserialize(
    const char *buffer,
    CFAllocatorRef allocator,
    CFOptionFlags options,
    CFStringRef *errorString);

CF_RETURNS_RETAINED
CFTypeRef IOCFUnserializeBinary(
    const char *buffer,
    size_t bufferSize,
    CFAllocatorRef allocator,
    CFOptionFlags options,
    CFStringRef *errorString);

CF_RETURNS_RETAINED
CFTypeRef IOCFUnserializeWithSize(
    const char *buffer,
    size_t bufferSize,
    CFAllocatorRef allocator,
    CFOptionFlags options,
    CFStringRef *errorString);

#if defined(__cplusplus)
}
#endif

#endif
