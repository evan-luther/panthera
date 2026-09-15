#include <DiskArbitration/DiskArbitration.h>

#include <CoreFoundation/CoreFoundation.h>
#include <string.h>

const CFStringRef kDADiskDescriptionDeviceProtocolKey =
    CFSTR("DAMediaProtocol");
const CFStringRef kDADiskDescriptionMediaPathKey =
    CFSTR("DAMediaPath");
const CFStringRef kDADiskDescriptionVolumePathKey =
    CFSTR("DAVolumePath");
const CFStringRef kDADiskDescriptionVolumeUUIDKey =
    CFSTR("DAVolumeUUID");
const CFStringRef kDADiskDescriptionMediaContentKey =
    CFSTR("DAMediaContent");
const CFStringRef kDADiskDescriptionMediaWritableKey =
    CFSTR("DAMediaWritable");
const CFStringRef kDADiskDescriptionVolumeNetworkKey =
    CFSTR("DAVolumeNetwork");

static const CFStringRef kPantheraDADiskBSDNameKey =
    CFSTR("PantheraDABSDName");

DASessionRef
DASessionCreate(CFAllocatorRef allocator)
{
    CFMutableDictionaryRef session;

    session = CFDictionaryCreateMutable(allocator, 0,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    return (DASessionRef)session;
}

DADiskRef
DADiskCreateFromBSDName(CFAllocatorRef allocator, DASessionRef session,
    const char *name)
{
    CFMutableDictionaryRef disk;
    CFStringRef bsd_name;
    const char *trimmed_name;

    (void)session;

    if (name == NULL)
        return NULL;

    trimmed_name = name;
    if (strncmp(trimmed_name, "/dev/", 5) == 0)
        trimmed_name += 5;

    bsd_name = CFStringCreateWithCString(allocator, trimmed_name,
        kCFStringEncodingUTF8);
    if (bsd_name == NULL)
        return NULL;

    disk = CFDictionaryCreateMutable(allocator, 0,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (disk != NULL)
        CFDictionarySetValue(disk, kPantheraDADiskBSDNameKey, bsd_name);

    CFRelease(bsd_name);
    return (DADiskRef)disk;
}

CFDictionaryRef
DADiskCopyDescription(DADiskRef disk)
{
    CFMutableDictionaryRef description;
    CFStringRef bsd_name;
    CFStringRef media_path;

    if (disk == NULL)
        return NULL;

    description = CFDictionaryCreateMutable(NULL, 0,
        &kCFCopyStringDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (description == NULL)
        return NULL;

    if (CFDictionaryGetValueIfPresent((CFDictionaryRef)disk,
        kPantheraDADiskBSDNameKey, (const void **)&bsd_name)) {
        CFDictionarySetValue(description, kPantheraDADiskBSDNameKey,
            bsd_name);
        media_path = CFStringCreateWithFormat(NULL, NULL,
            CFSTR("/IOService:/PantheraBlockStorage/%@"), bsd_name);
        if (media_path != NULL) {
            CFDictionarySetValue(description,
                kDADiskDescriptionMediaPathKey, media_path);
            CFRelease(media_path);
        }
    }

    CFDictionarySetValue(description,
        kDADiskDescriptionDeviceProtocolKey,
        CFSTR("Panthera Block Storage"));
    return description;
}
