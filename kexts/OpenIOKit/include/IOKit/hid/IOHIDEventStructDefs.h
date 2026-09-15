/*
 * Minimal Panthera compatibility surface for the newer IOHID event stack.
 * The public OSS slice we are using does not ship the SDK's struct-def header,
 * but the current kernel build only needs the vendor-defined payload layout.
 */
#ifndef PANTHERA_IOHID_EVENT_STRUCT_DEFS_H
#define PANTHERA_IOHID_EVENT_STRUCT_DEFS_H

#include <IOKit/IOTypes.h>

typedef struct _IOHIDVendorDefinedEventData {
    uint32_t usagePage;
    uint32_t usage;
    uint32_t version;
    uint32_t length;
    UInt8    data[0];
} IOHIDVendorDefinedEventData;

#endif
