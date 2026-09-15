#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/mach_vm.h>
#include <mach/ndr.h>

#include <CoreFoundation/CoreFoundation.h>

#include <IOKit/IOCFSerialize.h>
#include <IOKit/IOCFUnserialize.h>
#include <IOKit/IOKitLib.h>

#include "device_iokit.h"

const mach_port_t kIOMasterPortDefault = MACH_PORT_NULL;
const mach_port_t kIOMainPortDefault = MACH_PORT_NULL;

static void
panthera_iokit_trace(const char *message)
{
    const char *trace = getenv("PANTHERA_IOKIT_TRACE");

    if (trace == NULL || trace[0] == '\0' || strcmp(trace, "0") == 0) {
        return;
    }
    if (message != NULL) {
        write(STDERR_FILENO, message, strlen(message));
    }
}

static mach_port_t
panthera_get_default_main_port(void)
{
    kern_return_t kr;
    mach_port_t hostPort;
    mach_port_t mainPort = MACH_PORT_NULL;

    panthera_iokit_trace("PANTHERA:libIOKit default main port enter\n");
    hostPort = mach_host_self();
    panthera_iokit_trace("PANTHERA:libIOKit mach_host_self returned\n");
    if (hostPort == MACH_PORT_NULL) {
        panthera_iokit_trace("PANTHERA:libIOKit mach_host_self null\n");
        return MACH_PORT_NULL;
    }

    panthera_iokit_trace("PANTHERA:libIOKit host_get_io_main enter\n");
    kr = host_get_io_main(hostPort, &mainPort);
    panthera_iokit_trace("PANTHERA:libIOKit host_get_io_main returned\n");
    mach_port_deallocate(mach_task_self(), hostPort);
    if (kr != KERN_SUCCESS) {
        panthera_iokit_trace("PANTHERA:libIOKit host_get_io_main failed\n");
        return MACH_PORT_NULL;
    }

    panthera_iokit_trace("PANTHERA:libIOKit default main port ok\n");
    return mainPort;
}

static void
panthera_release_temp_main_port(mach_port_t requested, mach_port_t actual)
{
    if (requested == MACH_PORT_NULL && actual != MACH_PORT_NULL) {
        mach_port_deallocate(mach_task_self(), actual);
    }
}

static kern_return_t
panthera_unserialize_cf_buffer(
    const char *buffer,
    size_t size,
    CFAllocatorRef allocator,
    CFTypeRef *objectOut)
{
    CFStringRef errorString = NULL;

    if (objectOut == NULL) {
        return kIOReturnBadArgument;
    }

    *objectOut = IOCFUnserializeWithSize(buffer, size, allocator, 0, &errorString);
    if (errorString != NULL) {
        CFRelease(errorString);
    }

    return (*objectOut != NULL) ? KERN_SUCCESS : kIOReturnInternalError;
}

static kern_return_t
panthera_copy_registry_property_xml(
    io_registry_entry_t entry,
    const char *propertyName,
    CFAllocatorRef allocator,
    CFTypeRef *objectOut)
{
    kern_return_t kr;
    io_buf_ptr_t outBuffer = NULL;
    mach_msg_type_number_t outSize = 0;

    if (propertyName == NULL || objectOut == NULL) {
        return kIOReturnBadArgument;
    }

    *objectOut = NULL;
    kr = io_registry_entry_get_property(entry, (char *)propertyName, &outBuffer, &outSize);
    if (kr != KERN_SUCCESS) {
        return kr;
    }
    if (outBuffer == NULL || outSize == 0) {
        return kIOReturnInternalError;
    }

    kr = panthera_unserialize_cf_buffer((const char *)outBuffer, (size_t)outSize, allocator, objectOut);
    vm_deallocate(mach_task_self(), (vm_address_t)outBuffer, outSize);

    return kr;
}

static kern_return_t
panthera_copy_registry_property_bin(
    io_registry_entry_t entry,
    const char *propertyName,
    CFAllocatorRef allocator,
    CFTypeRef *objectOut)
{
    kern_return_t kr;
    uint32_t inlineWords[4096 / sizeof(uint32_t)] = {0};
    mach_vm_size_t inlineSize = sizeof(inlineWords);
    io_buf_ptr_t outBuffer = NULL;
    mach_msg_type_number_t outSize = 0;
    const char *decodeBuffer;
    size_t decodeSize;

    if (propertyName == NULL || objectOut == NULL) {
        return kIOReturnBadArgument;
    }

    *objectOut = NULL;
    kr = io_registry_entry_get_property_bin_buf(
        entry,
        "",
        (char *)propertyName,
        0,
        (mach_vm_address_t)inlineWords,
        &inlineSize,
        &outBuffer,
        &outSize);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    if (outBuffer != NULL && outSize != 0) {
        decodeBuffer = (const char *)outBuffer;
        decodeSize = outSize;
    } else {
        decodeBuffer = (const char *)inlineWords;
        decodeSize = (size_t)inlineSize;
    }

    kr = panthera_unserialize_cf_buffer(decodeBuffer, decodeSize, allocator, objectOut);
    if (kr != KERN_SUCCESS
        && strcmp(propertyName, kIORegistryEntryPropertyKeysKey) == 0) {
        size_t limit = (decodeSize < 64) ? decodeSize : 64;
        fprintf(stderr, "libIOKit: %s size=%zu", propertyName, decodeSize);
        for (size_t i = 0; i < limit; i++) {
            fprintf(stderr, "%s%02x", (i == 0) ? " bytes=" : " ", (unsigned char)decodeBuffer[i]);
        }
        if (limit < decodeSize) {
            fprintf(stderr, " ...");
        }
        fputc('\n', stderr);
    }

    if (outBuffer != NULL && outSize != 0) {
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)outBuffer, outSize);
    }

    return kr;
}

static kern_return_t
panthera_copy_registry_properties_fallback(
    io_registry_entry_t entry,
    CFAllocatorRef allocator,
    CFMutableDictionaryRef *properties)
{
    kern_return_t kr;
    CFTypeRef keysObject = NULL;
    CFArrayRef keys = NULL;
    CFMutableDictionaryRef dict = NULL;
    CFIndex count;

    if (properties == NULL) {
        return kIOReturnBadArgument;
    }

    *properties = NULL;
    kr = panthera_copy_registry_property_bin(
        entry,
        kIORegistryEntryPropertyKeysKey,
        allocator,
        &keysObject);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "libIOKit: property-keys fetch failed kr=0x%x\n", kr);
        return kr;
    }

    keys = (CFArrayRef)keysObject;
    if (CFGetTypeID(keys) != CFArrayGetTypeID()) {
        CFRelease(keysObject);
        return kIOReturnInternalError;
    }

    count = CFArrayGetCount(keys);
    dict = CFDictionaryCreateMutable(
        allocator,
        count,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (dict == NULL) {
        CFRelease(keysObject);
        return kIOReturnNoMemory;
    }

    for (CFIndex i = 0; i < count; i++) {
        CFStringRef key;
        char keyCString[128];
        CFTypeRef value = NULL;

        key = (CFStringRef)CFArrayGetValueAtIndex(keys, i);
        if (key == NULL || CFGetTypeID(key) != CFStringGetTypeID()) {
            fprintf(stderr, "libIOKit: property-keys decode produced non-string entry\n");
            kr = kIOReturnInternalError;
            break;
        }
        if (!CFStringGetCString(key, keyCString, sizeof(keyCString), kCFStringEncodingUTF8)) {
            fprintf(stderr, "libIOKit: property key UTF-8 conversion failed\n");
            kr = kIOReturnNoMemory;
            break;
        }

        kr = panthera_copy_registry_property_xml(entry, keyCString, allocator, &value);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "libIOKit: property fetch failed key=%s kr=0x%x\n", keyCString, kr);
            break;
        }

        CFDictionarySetValue(dict, key, value);
        CFRelease(value);
    }

    CFRelease(keysObject);

    if (kr != KERN_SUCCESS) {
        CFRelease(dict);
        return kr;
    }

    *properties = dict;
    return KERN_SUCCESS;
}

static CFMutableDictionaryRef
panthera_make_one_string_prop(CFStringRef key, const char *value)
{
    CFMutableDictionaryRef dict;
    CFStringRef string;

    if (value == NULL) {
        return NULL;
    }

    dict = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (dict == NULL) {
        return NULL;
    }

    string = CFStringCreateWithCString(kCFAllocatorDefault, value, kCFStringEncodingUTF8);
    if (string == NULL) {
        CFRelease(dict);
        return NULL;
    }

    CFDictionarySetValue(dict, key, string);
    CFRelease(string);
    return dict;
}

static CFMutableDictionaryRef
panthera_make_one_number_prop(CFStringRef key, uint64_t value)
{
    CFMutableDictionaryRef dict;
    CFNumberRef number;

    dict = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (dict == NULL) {
        return NULL;
    }

    number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &value);
    if (number == NULL) {
        CFRelease(dict);
        return NULL;
    }

    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
    return dict;
}

static bool
panthera_cfstring_to_cstring(CFStringRef string, char *buffer, size_t bufferSize)
{
    if (string == NULL || buffer == NULL || bufferSize == 0) {
        return false;
    }
    return CFStringGetCString(string, buffer, bufferSize, kCFStringEncodingUTF8);
}

kern_return_t
IOMainPort(mach_port_t bootstrapPort, mach_port_t *mainPort)
{
    return IOMasterPort(bootstrapPort, mainPort);
}

kern_return_t
IOMasterPort(mach_port_t bootstrapPort __unused, mach_port_t *mainPort)
{
    mach_port_t port;

    if (mainPort == NULL) {
        return KERN_INVALID_ARGUMENT;
    }

    port = panthera_get_default_main_port();
    if (port == MACH_PORT_NULL) {
        *mainPort = MACH_PORT_NULL;
        return KERN_FAILURE;
    }

    *mainPort = port;
    return KERN_SUCCESS;
}

kern_return_t
IOObjectRelease(io_object_t object)
{
    return mach_port_deallocate(mach_task_self(), object);
}

kern_return_t
IOObjectGetClass(io_object_t object, io_name_t className)
{
    return io_object_get_class(object, className);
}

boolean_t
IOObjectConformsTo(io_object_t object, const io_name_t className)
{
    boolean_t conforms = false;

    if (className == NULL) {
        return false;
    }
    if (io_object_conforms_to(object, (char *)className, &conforms) != KERN_SUCCESS) {
        return false;
    }
    return conforms;
}

io_object_t
IOIteratorNext(io_iterator_t iterator)
{
    io_object_t next = IO_OBJECT_NULL;

    if (io_iterator_next(iterator, &next) != KERN_SUCCESS) {
        return IO_OBJECT_NULL;
    }

    return next;
}

CFMutableDictionaryRef
IOServiceMatching(const char *name)
{
    return panthera_make_one_string_prop(CFSTR(kIOProviderClassKey), name);
}

CFMutableDictionaryRef
IOServiceNameMatching(const char *name)
{
    return panthera_make_one_string_prop(CFSTR(kIONameMatchKey), name);
}

CFMutableDictionaryRef
IOBSDNameMatching(mach_port_t mainPort __unused, uint32_t options __unused, const char *name)
{
    return panthera_make_one_string_prop(CFSTR(kIOBSDNameKey), name);
}

CFMutableDictionaryRef
IORegistryEntryIDMatching(uint64_t entryID)
{
    return panthera_make_one_number_prop(CFSTR(kIORegistryEntryIDKey), entryID);
}

io_service_t
IOServiceGetMatchingService(mach_port_t requestedMainPort, CFDictionaryRef matching)
{
    kern_return_t kr = KERN_FAILURE;
    kern_return_t result = KERN_FAILURE;
    CFDataRef data;
    CFIndex length;
    mach_port_t mainPort;
    io_service_t service = IO_OBJECT_NULL;
    io_struct_inband_t inband = {0};

    if (matching == NULL) {
        return IO_OBJECT_NULL;
    }

    mainPort = (requestedMainPort == MACH_PORT_NULL) ? panthera_get_default_main_port() : requestedMainPort;
    if (mainPort == MACH_PORT_NULL) {
        CFRelease(matching);
        return IO_OBJECT_NULL;
    }

    data = IOCFSerialize(matching, 0);
    CFRelease(matching);
    if (data == NULL) {
        panthera_release_temp_main_port(requestedMainPort, mainPort);
        return IO_OBJECT_NULL;
    }

    length = CFDataGetLength(data);
    if ((size_t)length <= sizeof(io_struct_inband_t)) {
        memcpy(inband, CFDataGetBytePtr(data), (size_t)length);
        kr = io_service_get_matching_service_bin(
            mainPort,
            inband,
            (mach_msg_type_number_t)length,
            &service);
    } else {
        kr = io_service_get_matching_service_ool(
            mainPort,
            (io_buf_ptr_t)CFDataGetBytePtr(data),
            (mach_msg_type_number_t)length,
            &result,
            &service);
        if (kr == KERN_SUCCESS) {
            kr = result;
        }
    }

    CFRelease(data);
    panthera_release_temp_main_port(requestedMainPort, mainPort);

    return (kr == KERN_SUCCESS) ? service : IO_OBJECT_NULL;
}

kern_return_t
IOServiceGetMatchingServices(mach_port_t requestedMainPort, CFDictionaryRef matching, io_iterator_t *existing)
{
    kern_return_t kr = KERN_FAILURE;
    kern_return_t result = KERN_FAILURE;
    CFDataRef data;
    CFIndex length;
    mach_port_t mainPort;
    io_struct_inband_t inband = {0};

    if (matching == NULL || existing == NULL) {
        return kIOReturnBadArgument;
    }

    *existing = IO_OBJECT_NULL;
    mainPort = (requestedMainPort == MACH_PORT_NULL) ? panthera_get_default_main_port() : requestedMainPort;
    if (mainPort == MACH_PORT_NULL) {
        CFRelease(matching);
        return kIOReturnNoDevice;
    }

    data = IOCFSerialize(matching, 0);
    CFRelease(matching);
    if (data == NULL) {
        panthera_release_temp_main_port(requestedMainPort, mainPort);
        return kIOReturnUnsupported;
    }

    length = CFDataGetLength(data);
    if ((size_t)length <= sizeof(io_struct_inband_t)) {
        memcpy(inband, CFDataGetBytePtr(data), (size_t)length);
        kr = io_service_get_matching_services_bin(
            mainPort,
            inband,
            (mach_msg_type_number_t)length,
            existing);
    } else {
        kr = io_service_get_matching_services_ool(
            mainPort,
            (io_buf_ptr_t)CFDataGetBytePtr(data),
            (mach_msg_type_number_t)length,
            &result,
            existing);
        if (kr == KERN_SUCCESS) {
            kr = result;
        }
    }

    CFRelease(data);
    panthera_release_temp_main_port(requestedMainPort, mainPort);
    return kr;
}

kern_return_t
IORegistryEntryGetName(io_registry_entry_t entry, io_name_t name)
{
    return io_registry_entry_get_name(entry, name);
}

kern_return_t
IORegistryEntryGetPath(io_registry_entry_t entry, const io_name_t plane, io_string_t path)
{
    return io_registry_entry_get_path(entry, (char *)plane, path);
}

kern_return_t
IORegistryEntryCreateIterator(
    io_registry_entry_t entry,
    const io_name_t plane,
    IOOptionBits options,
    io_iterator_t *iterator)
{
    return io_registry_entry_create_iterator(entry, (char *)plane, options, iterator);
}

kern_return_t
IORegistryEntryGetNameInPlane(io_registry_entry_t entry, const io_name_t plane, io_name_t name)
{
    return io_registry_entry_get_name_in_plane(entry, (char *)(plane != NULL ? plane : ""), name);
}

kern_return_t
IORegistryEntryGetLocationInPlane(io_registry_entry_t entry, const io_name_t plane, io_name_t location)
{
    return io_registry_entry_get_location_in_plane(entry, (char *)(plane != NULL ? plane : ""), location);
}

kern_return_t
IORegistryEntryGetRegistryEntryID(io_registry_entry_t entry, uint64_t *entryID)
{
    kern_return_t kr;

    if (entryID == NULL) {
        return kIOReturnBadArgument;
    }

    kr = io_registry_entry_get_registry_entry_id(entry, entryID);
    if (kr != KERN_SUCCESS) {
        *entryID = 0;
    }
    return kr;
}

kern_return_t
IOServiceWaitQuiet(io_service_t service, mach_timespec_t *waitTime)
{
    mach_timespec_t defaultWait = {0, -1};

    if (waitTime == NULL) {
        waitTime = &defaultWait;
    }

    return io_service_wait_quiet(service, *waitTime);
}

kern_return_t
IOKitWaitQuietWithOptions(mach_port_t requestedMainPort, mach_timespec_t *waitTime, IOOptionBits options)
{
    kern_return_t kr;
    const char *rootPath = kIOServicePlane ":/";
    mach_port_t mainPort;
    io_registry_entry_t root = IO_OBJECT_NULL;
    mach_timespec_t defaultWait = {0, -1};

    mainPort = (requestedMainPort == MACH_PORT_NULL) ? panthera_get_default_main_port() : requestedMainPort;
    if (mainPort == MACH_PORT_NULL) {
        return kIOReturnNoDevice;
    }

    kr = io_registry_entry_from_path(mainPort, (char *)rootPath, &root);
    if (kr == KERN_SUCCESS) {
        if (waitTime == NULL) {
            waitTime = &defaultWait;
        }
        kr = io_service_wait_quiet_with_options(root, *waitTime, options);
        IOObjectRelease(root);
    }

    panthera_release_temp_main_port(requestedMainPort, mainPort);
    return kr;
}

kern_return_t
IOKitWaitQuiet(mach_port_t requestedMainPort, mach_timespec_t *waitTime)
{
    kern_return_t kr;
    const char *rootPath = kIOServicePlane ":/";
    mach_port_t mainPort;
    io_registry_entry_t root = IO_OBJECT_NULL;
    mach_timespec_t defaultWait = {0, -1};

    mainPort = (requestedMainPort == MACH_PORT_NULL) ? panthera_get_default_main_port() : requestedMainPort;
    if (mainPort == MACH_PORT_NULL) {
        return kIOReturnNoDevice;
    }

    kr = io_registry_entry_from_path(mainPort, (char *)rootPath, &root);
    if (kr == KERN_SUCCESS) {
        if (waitTime == NULL) {
            waitTime = &defaultWait;
        }
        kr = io_service_wait_quiet(root, *waitTime);
        IOObjectRelease(root);
    }

    panthera_release_temp_main_port(requestedMainPort, mainPort);
    return kr;
}

io_registry_entry_t
IORegistryEntryFromPath(mach_port_t requestedMainPort, const io_string_t path)
{
    kern_return_t kr;
    mach_port_t mainPort;
    io_registry_entry_t entry = IO_OBJECT_NULL;

    if (path == NULL) {
        return IO_OBJECT_NULL;
    }

    mainPort = (requestedMainPort == MACH_PORT_NULL) ? panthera_get_default_main_port() : requestedMainPort;
    if (mainPort == MACH_PORT_NULL) {
        return IO_OBJECT_NULL;
    }

    kr = io_registry_entry_from_path(mainPort, (char *)path, &entry);
    panthera_release_temp_main_port(requestedMainPort, mainPort);

    return (kr == KERN_SUCCESS) ? entry : IO_OBJECT_NULL;
}

kern_return_t
IORegistryEntryCreateCFProperties(
    io_registry_entry_t entry,
    CFMutableDictionaryRef *properties,
    CFAllocatorRef allocator,
    IOOptionBits options __unused)
{
    kern_return_t kr;
    uint32_t inlineWords[4096 / sizeof(uint32_t)] = {0};
    char *inlineBuffer = (char *)inlineWords;
    mach_vm_size_t inlineSize = sizeof(inlineWords);
    io_buf_ptr_t outBuffer = NULL;
    mach_msg_type_number_t outSize = 0;
    const char *decodeBuffer;
    size_t decodeSize;
    CFStringRef errorString = NULL;

    if (properties == NULL) {
        return kIOReturnBadArgument;
    }

    *properties = NULL;
    kr = io_registry_entry_get_properties_bin_buf(
        entry,
        (mach_vm_address_t)inlineBuffer,
        &inlineSize,
        &outBuffer,
        &outSize);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    if (outBuffer != NULL && outSize != 0) {
        decodeBuffer = (const char *)outBuffer;
        decodeSize = outSize;
    } else {
        decodeBuffer = inlineBuffer;
        decodeSize = (size_t)inlineSize;
    }

    *properties = (CFMutableDictionaryRef)IOCFUnserializeWithSize(
        decodeBuffer,
        decodeSize,
        allocator,
        0,
        &errorString);

    if (outBuffer != NULL && outSize != 0) {
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)outBuffer, outSize);
    }
    if (errorString != NULL) {
        CFRelease(errorString);
    }

    if (*properties != NULL) {
        return KERN_SUCCESS;
    }

    return panthera_copy_registry_properties_fallback(entry, allocator, properties);
}

CFTypeRef
IORegistryEntrySearchCFProperty(
    io_registry_entry_t entry,
    const io_name_t plane,
    CFStringRef key,
    CFAllocatorRef allocator,
    IOOptionBits options)
{
    kern_return_t kr;
    char keyCString[128];
    uint32_t inlineWords[4096 / sizeof(uint32_t)] = {0};
    mach_vm_size_t inlineSize = sizeof(inlineWords);
    io_buf_ptr_t outBuffer = NULL;
    mach_msg_type_number_t outSize = 0;
    const char *decodeBuffer;
    size_t decodeSize;
    CFTypeRef object = NULL;

    if (!panthera_cfstring_to_cstring(key, keyCString, sizeof(keyCString))) {
        return NULL;
    }

    if ((options & kIORegistryIterateRecursively) != 0) {
        kr = io_registry_entry_get_property_recursively(
            entry,
            (char *)(plane != NULL ? plane : ""),
            keyCString,
            options,
            &outBuffer,
            &outSize);
    } else {
        kr = io_registry_entry_get_property_bin_buf(
            entry,
            "",
            keyCString,
            options,
            (mach_vm_address_t)inlineWords,
            &inlineSize,
            &outBuffer,
            &outSize);
    }
    if (kr != KERN_SUCCESS) {
        return NULL;
    }

    if (outBuffer != NULL && outSize != 0) {
        decodeBuffer = (const char *)outBuffer;
        decodeSize = outSize;
    } else {
        decodeBuffer = (const char *)inlineWords;
        decodeSize = (size_t)inlineSize;
    }

    kr = panthera_unserialize_cf_buffer(decodeBuffer, decodeSize, allocator, &object);
    if (outBuffer != NULL && outSize != 0) {
        mach_vm_deallocate(mach_task_self(), (mach_vm_address_t)outBuffer, outSize);
    }

    return (kr == KERN_SUCCESS) ? object : NULL;
}

CFTypeRef
IORegistryEntryCreateCFProperty(
    io_registry_entry_t entry,
    CFStringRef key,
    CFAllocatorRef allocator,
    IOOptionBits options)
{
    return IORegistryEntrySearchCFProperty(entry, NULL, key, allocator, options);
}

kern_return_t
IORegistryEntryGetParentEntry(
    io_registry_entry_t entry,
    const io_name_t plane,
    io_registry_entry_t *parent)
{
    kern_return_t kr;
    io_iterator_t iterator = IO_OBJECT_NULL;

    if (parent == NULL) {
        return kIOReturnBadArgument;
    }

    *parent = IO_OBJECT_NULL;
    kr = io_registry_entry_get_parent_iterator(entry, (char *)plane, &iterator);
    if (kr != KERN_SUCCESS) {
        return kr;
    }

    *parent = IOIteratorNext(iterator);
    IOObjectRelease(iterator);
    return (*parent != IO_OBJECT_NULL) ? KERN_SUCCESS : kIOReturnNoDevice;
}

kern_return_t
IOServiceOpen(io_service_t service, task_port_t owningTask, uint32_t type, io_connect_t *connect)
{
    kern_return_t kr;
    kern_return_t result = KERN_FAILURE;

    if (connect == NULL) {
        return kIOReturnBadArgument;
    }

    *connect = IO_OBJECT_NULL;
    kr = io_service_open_extended(
        service,
        owningTask,
        type,
        NDR_record,
        NULL,
        0,
        &result,
        connect);
    if (kr == KERN_SUCCESS) {
        kr = result;
    }
    return kr;
}

kern_return_t
IOServiceClose(io_connect_t connect)
{
    kern_return_t kr = io_service_close(connect);
    IOObjectRelease(connect);
    return kr;
}

kern_return_t
IOConnectMapMemory(
    io_connect_t connect,
    uint32_t memoryType,
    task_port_t intoTask,
    vm_address_t *atAddress,
    vm_size_t *ofSize,
    IOOptionBits options)
{
    return io_connect_map_memory_into_task(
        connect,
        memoryType,
        intoTask,
        (mach_vm_address_t *)atAddress,
        (mach_vm_size_t *)ofSize,
        options);
}

kern_return_t
IOConnectMapMemory64(
    io_connect_t connect,
    uint32_t memoryType,
    task_port_t intoTask,
    mach_vm_address_t *atAddress,
    mach_vm_size_t *ofSize,
    IOOptionBits options)
{
    return io_connect_map_memory_into_task(
        connect,
        memoryType,
        intoTask,
        atAddress,
        ofSize,
        options);
}

kern_return_t
IOConnectUnmapMemory(io_connect_t connect, uint32_t memoryType, task_port_t fromTask, vm_address_t atAddress)
{
    return io_connect_unmap_memory_from_task(connect, memoryType, fromTask, atAddress);
}
