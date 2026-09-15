#ifndef PANTHERA_DISKARBITRATION_H
#define PANTHERA_DISKARBITRATION_H

#include <CoreFoundation/CoreFoundation.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const struct __DASession *DASessionRef;
typedef const struct __DADisk *DADiskRef;
typedef const struct __DADissenter *DADissenterRef;

extern const CFStringRef kDADiskDescriptionDeviceProtocolKey;
extern const CFStringRef kDADiskDescriptionMediaPathKey;
extern const CFStringRef kDADiskDescriptionVolumePathKey;
extern const CFStringRef kDADiskDescriptionVolumeUUIDKey;
extern const CFStringRef kDADiskDescriptionMediaContentKey;
extern const CFStringRef kDADiskDescriptionMediaWritableKey;
extern const CFStringRef kDADiskDescriptionVolumeNetworkKey;

DASessionRef DASessionCreate(CFAllocatorRef allocator);
DADiskRef DADiskCreateFromBSDName(CFAllocatorRef allocator,
    DASessionRef session, const char *name);
CFDictionaryRef DADiskCopyDescription(DADiskRef disk);

#ifdef __cplusplus
}
#endif

#endif
