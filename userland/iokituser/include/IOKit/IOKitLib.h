/*
 * Minimal public libIOKit header for Panthera Wayland roadmap Phase 1.
 */

#ifndef _IOKIT_IOKITLIB_H
#define _IOKIT_IOKITLIB_H

#ifdef KERNEL
#error This header is not for kernel use
#endif

#include <mach/mach_types.h>
#include <mach/mach_init.h>

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFDictionary.h>

#include <IOKit/IOTypes.h>
#include <IOKit/IOKitKeys.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const mach_port_t kIOMainPortDefault;
extern const mach_port_t kIOMasterPortDefault;

kern_return_t IOMainPort(mach_port_t bootstrapPort, mach_port_t *mainPort);
kern_return_t IOMasterPort(mach_port_t bootstrapPort, mach_port_t *mainPort);

kern_return_t IOObjectRelease(io_object_t object);
kern_return_t IOObjectGetClass(io_object_t object, io_name_t className);
boolean_t IOObjectConformsTo(io_object_t object, const io_name_t className);

io_object_t IOIteratorNext(io_iterator_t iterator);

CFMutableDictionaryRef IOServiceMatching(const char *name);
CFMutableDictionaryRef IOServiceNameMatching(const char *name);
CFMutableDictionaryRef IOBSDNameMatching(mach_port_t mainPort, uint32_t options, const char *name);

io_service_t IOServiceGetMatchingService(mach_port_t mainPort, CFDictionaryRef matching);
kern_return_t IOServiceGetMatchingServices(
    mach_port_t mainPort,
    CFDictionaryRef matching,
    io_iterator_t *existing);

kern_return_t IORegistryEntryGetName(io_registry_entry_t entry, io_name_t name);
kern_return_t IORegistryEntryGetPath(io_registry_entry_t entry, const io_name_t plane, io_string_t path);
kern_return_t IORegistryEntryCreateIterator(
    io_registry_entry_t entry,
    const io_name_t plane,
    IOOptionBits options,
    io_iterator_t *iterator);
kern_return_t IORegistryEntryGetNameInPlane(
    io_registry_entry_t entry,
    const io_name_t plane,
    io_name_t name);
kern_return_t IORegistryEntryGetLocationInPlane(
    io_registry_entry_t entry,
    const io_name_t plane,
    io_name_t location);
kern_return_t IORegistryEntryGetRegistryEntryID(io_registry_entry_t entry, uint64_t *entryID);
kern_return_t IOServiceWaitQuiet(io_service_t service, mach_timespec_t *waitTime);
kern_return_t IOKitWaitQuietWithOptions(
    mach_port_t mainPort,
    mach_timespec_t *waitTime,
    IOOptionBits options);
kern_return_t IOKitWaitQuiet(mach_port_t mainPort, mach_timespec_t *waitTime);
io_registry_entry_t IORegistryEntryFromPath(mach_port_t mainPort, const io_string_t path);
kern_return_t IORegistryEntryCreateCFProperties(
    io_registry_entry_t entry,
    CFMutableDictionaryRef *properties,
    CFAllocatorRef allocator,
    IOOptionBits options);
CFTypeRef IORegistryEntryCreateCFProperty(
    io_registry_entry_t entry,
    CFStringRef key,
    CFAllocatorRef allocator,
    IOOptionBits options);
CFTypeRef IORegistryEntrySearchCFProperty(
    io_registry_entry_t entry,
    const io_name_t plane,
    CFStringRef key,
    CFAllocatorRef allocator,
    IOOptionBits options);
kern_return_t IORegistryEntryGetParentEntry(
    io_registry_entry_t entry,
    const io_name_t plane,
    io_registry_entry_t *parent);

enum {
    kIORegistryIterateRecursively = 0x00000001,
    kIORegistryIterateParents = 0x00000002
};

kern_return_t IOServiceOpen(
    io_service_t service,
    task_port_t owningTask,
    uint32_t type,
    io_connect_t *connect);
kern_return_t IOServiceClose(io_connect_t connect);

kern_return_t IOConnectMapMemory(
    io_connect_t connect,
    uint32_t memoryType,
    task_port_t intoTask,
    vm_address_t *atAddress,
    vm_size_t *ofSize,
    IOOptionBits options);
kern_return_t IOConnectMapMemory64(
    io_connect_t connect,
    uint32_t memoryType,
    task_port_t intoTask,
    mach_vm_address_t *atAddress,
    mach_vm_size_t *ofSize,
    IOOptionBits options);
kern_return_t IOConnectUnmapMemory(
    io_connect_t connect,
    uint32_t memoryType,
    task_port_t fromTask,
    vm_address_t atAddress);

#ifdef __cplusplus
}
#endif

#endif
