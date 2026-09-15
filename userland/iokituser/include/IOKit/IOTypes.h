/*
 * Minimal userland IOTypes surface for Panthera Phase 1.
 */

#ifndef __IOKIT_IOTYPES_H
#define __IOKIT_IOTYPES_H

#include <stdbool.h>
#include <mach/mach.h>
#include <device/device_types.h>

#include <IOKit/IOReturn.h>
#include <IOKit/IOMapTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef UInt32 IOOptionBits;
typedef SInt32 IOFixed;
typedef UInt32 IOVersion;
typedef UInt32 IOItemCount;
typedef UInt32 IOCacheMode;

#ifndef IOKIT
typedef char io_name_t[128];
typedef char io_string_t[512];
typedef char io_string_inband_t[4096];
typedef char io_struct_inband_t[4096];
#endif

typedef mach_port_t io_object_t;
typedef io_object_t io_connect_t;
typedef io_object_t io_iterator_t;
typedef io_object_t io_registry_entry_t;
typedef io_object_t io_service_t;

#define IO_OBJECT_NULL ((io_object_t)0)

#ifdef __cplusplus
}
#endif

#endif
