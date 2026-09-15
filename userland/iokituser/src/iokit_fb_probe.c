#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mach/mach.h>

#include <CoreFoundation/CoreFoundation.h>

#include <IOKit/IOKitLib.h>

enum {
    kPantheraIOFBServerConnectType = 0,
    kPantheraIOFBSharedConnectType = 1,
    kPantheraIOFBCursorMemory = 100,
    kPantheraIOFBVRAMMemory = 110,
};

typedef struct {
    int found;
    int properties_ok;
    int open_ok;
    int cursor_map_ok;
    int vram_map_ok;
    int write_ok;
} probe_result_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bitsPerPixel;
    uint32_t bytesPerRow;
    uint32_t memorySize;
} fb_geometry_t;

typedef struct {
    bool write_checkerboard;
} probe_options_t;

static bool
cf_number_to_u32(CFTypeRef object, uint32_t *valueOut)
{
    int32_t value32;

    if (object == NULL || valueOut == NULL) {
        return false;
    }
    if (CFGetTypeID(object) != CFNumberGetTypeID()) {
        return false;
    }
    if (!CFNumberGetValue((CFNumberRef)object, kCFNumberSInt32Type, &value32)) {
        return false;
    }
    if (value32 < 0) {
        return false;
    }

    *valueOut = (uint32_t)value32;
    return true;
}

static bool
copy_u32_property(CFDictionaryRef dict, CFStringRef key, uint32_t *valueOut)
{
    const void *value;

    if (dict == NULL || key == NULL || valueOut == NULL) {
        return false;
    }

    value = CFDictionaryGetValue(dict, key);
    return cf_number_to_u32((CFTypeRef)value, valueOut);
}

static bool
extract_fb_geometry(CFDictionaryRef properties, fb_geometry_t *geometry)
{
    if (properties == NULL || geometry == NULL) {
        return false;
    }

    memset(geometry, 0, sizeof(*geometry));
    if (!copy_u32_property(properties, CFSTR(kIOFBWidthKey), &geometry->width)
        || !copy_u32_property(properties, CFSTR(kIOFBHeightKey), &geometry->height)
        || !copy_u32_property(properties, CFSTR(kIOFBBitsPerPixelKey), &geometry->bitsPerPixel)
        || !copy_u32_property(properties, CFSTR(kIOFBBytesPerRowKey), &geometry->bytesPerRow)) {
        return false;
    }
    (void)copy_u32_property(properties, CFSTR(kIOFBMemorySizeKey), &geometry->memorySize);
    return true;
}

static void
print_cf_xml(CFTypeRef object)
{
    CFDataRef xml;
    const UInt8 *bytes;

    xml = CFPropertyListCreateData(
        kCFAllocatorDefault,
        object,
        kCFPropertyListXMLFormat_v1_0,
        0,
        NULL);
    if (xml == NULL) {
        puts("  properties: <failed to format>");
        return;
    }

    bytes = CFDataGetBytePtr(xml);
    fwrite(bytes, 1, (size_t)CFDataGetLength(xml), stdout);
    if (CFDataGetLength(xml) == 0 || bytes[CFDataGetLength(xml) - 1] != '\n') {
        putchar('\n');
    }
    CFRelease(xml);
}

static void
print_kern_status(const char *label, kern_return_t kr)
{
    printf("%s: 0x%x (%d)\n", label, kr, kr);
}

static void
write_checkerboard(void *baseAddress, mach_vm_size_t mapSize, const fb_geometry_t *geometry)
{
    uint8_t *base = (uint8_t *)baseAddress;
    uint32_t y;
    const uint32_t tileSize = 64;

    if (base == NULL || geometry == NULL) {
        return;
    }

    if (geometry->bitsPerPixel != 32) {
        printf("  checkerboard skipped: unsupported depth=%" PRIu32 "\n", geometry->bitsPerPixel);
        return;
    }

    if (((uint64_t)geometry->bytesPerRow * (uint64_t)geometry->height) > mapSize) {
        printf("  checkerboard skipped: geometry exceeds map size\n");
        return;
    }

    for (y = 0; y < geometry->height; y++) {
        uint32_t *row = (uint32_t *)(base + ((size_t)y * geometry->bytesPerRow));
        uint32_t x;

        for (x = 0; x < geometry->width; x++) {
            const uint32_t tile = ((x / tileSize) ^ (y / tileSize)) & 1U;
            row[x] = tile ? 0x00ff7f00U : 0x00007fffU;
        }
    }
}

static void
probe_open_and_map(
    io_service_t service,
    probe_result_t *result,
    const probe_options_t *options,
    const fb_geometry_t *geometry)
{
    kern_return_t kr;
    io_connect_t connect = IO_OBJECT_NULL;
    mach_vm_address_t mapAddress = 0;
    mach_vm_size_t mapSize = 0;

    kr = IOServiceOpen(service, mach_task_self(), kPantheraIOFBSharedConnectType, &connect);
    print_kern_status("  IOServiceOpen(shared)", kr);
    if (kr != KERN_SUCCESS) {
        kr = IOServiceOpen(service, mach_task_self(), kPantheraIOFBServerConnectType, &connect);
        print_kern_status("  IOServiceOpen(server)", kr);
    }
    if (kr != KERN_SUCCESS) {
        return;
    }
    result->open_ok = 1;

    kr = IOConnectMapMemory64(
        connect,
        kPantheraIOFBCursorMemory,
        mach_task_self(),
        &mapAddress,
        &mapSize,
        kIOMapAnywhere);
    if (kr == KERN_SUCCESS) {
        result->cursor_map_ok = 1;
        printf("  IOConnectMapMemory(cursor): 0x%x (%d) addr=0x%llx size=0x%llx\n",
            kr,
            kr,
            (unsigned long long)mapAddress,
            (unsigned long long)mapSize);
        IOConnectUnmapMemory(connect, kPantheraIOFBCursorMemory, mach_task_self(), (vm_address_t)mapAddress);
    } else {
        print_kern_status("  IOConnectMapMemory(cursor)", kr);
    }

    mapAddress = 0;
    mapSize = 0;
    kr = IOConnectMapMemory64(
        connect,
        kPantheraIOFBVRAMMemory,
        mach_task_self(),
        &mapAddress,
        &mapSize,
        kIOMapAnywhere);
    if (kr == KERN_SUCCESS) {
        result->vram_map_ok = 1;
        printf("  IOConnectMapMemory(vram): 0x%x (%d) addr=0x%llx size=0x%llx\n",
            kr,
            kr,
            (unsigned long long)mapAddress,
            (unsigned long long)mapSize);
        if (options != NULL && options->write_checkerboard) {
            if (geometry != NULL && geometry->width != 0 && geometry->height != 0
                && geometry->bitsPerPixel != 0 && geometry->bytesPerRow != 0) {
                write_checkerboard((void *)(uintptr_t)mapAddress, mapSize, geometry);
                printf("  checkerboard: wrote %" PRIu32 "x%" PRIu32 " depth=%" PRIu32 " rowBytes=%" PRIu32 "\n",
                    geometry->width,
                    geometry->height,
                    geometry->bitsPerPixel,
                    geometry->bytesPerRow);
                result->write_ok = 1;
            } else {
                puts("  checkerboard: skipped because framebuffer geometry properties are missing");
            }
        }
        IOConnectUnmapMemory(connect, kPantheraIOFBVRAMMemory, mach_task_self(), (vm_address_t)mapAddress);
    } else {
        print_kern_status("  IOConnectMapMemory(vram)", kr);
    }

    IOServiceClose(connect);
}

static void
probe_class(
    const char *matchClass,
    bool attemptOpen,
    probe_result_t *result,
    const probe_options_t *options)
{
    kern_return_t kr;
    io_iterator_t iter = IO_OBJECT_NULL;
    io_service_t service;
    int index = 0;

    printf("MATCH class=%s\n", matchClass);
    kr = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(matchClass), &iter);
    print_kern_status("IOServiceGetMatchingServices", kr);
    if (kr != KERN_SUCCESS) {
        return;
    }

    while ((service = IOIteratorNext(iter)) != IO_OBJECT_NULL) {
        io_name_t name = "";
        io_name_t className = "";
        io_string_t path = "";
        CFMutableDictionaryRef properties = NULL;
        fb_geometry_t geometry = {0};

        result->found = 1;
        IORegistryEntryGetName(service, name);
        IOObjectGetClass(service, className);
        IORegistryEntryGetPath(service, kIOServicePlane, path);

        printf("SERVICE[%d] name=%s class=%s path=%s\n", index, name, className, path);

        kr = IORegistryEntryCreateCFProperties(service, &properties, kCFAllocatorDefault, 0);
        print_kern_status("  IORegistryEntryCreateCFProperties", kr);
        if (kr == KERN_SUCCESS && properties != NULL) {
            result->properties_ok = 1;
            print_cf_xml(properties);
            if (extract_fb_geometry(properties, &geometry)) {
                printf("  geometry: width=%" PRIu32 " height=%" PRIu32
                       " depth=%" PRIu32 " rowBytes=%" PRIu32 " memorySize=%" PRIu32 "\n",
                    geometry.width,
                    geometry.height,
                    geometry.bitsPerPixel,
                    geometry.bytesPerRow,
                    geometry.memorySize);
            }
            CFRelease(properties);
        }

        if (attemptOpen) {
            probe_open_and_map(service, result, options, &geometry);
        }

        IOObjectRelease(service);
        index++;
    }

    IOObjectRelease(iter);
}

int
main(int argc, char **argv)
{
    probe_result_t result = {0};
    probe_options_t options = {0};
    int argi;

    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--write-checkerboard") == 0) {
            options.write_checkerboard = true;
        } else {
            fprintf(stderr, "usage: %s [--write-checkerboard]\n", argv[0]);
            return 64;
        }
    }

    puts("Panthera IOKit framebuffer probe");
    probe_class("IOFramebuffer", true, &result, &options);
    probe_class("IODisplayConnect", false, &result, &options);

    printf(
        "RESULT found=%d properties=%d open=%d cursor_map=%d vram_map=%d write=%d\n",
        result.found,
        result.properties_ok,
        result.open_ok,
        result.cursor_map_ok,
        result.vram_map_ok,
        result.write_ok);

    if (!result.found) {
        return 12;
    }
    if (!result.properties_ok) {
        return 11;
    }
    if (!result.open_ok) {
        return 10;
    }
    if (!(result.cursor_map_ok || result.vram_map_ok)) {
        return 9;
    }
    if (options.write_checkerboard && !result.write_ok) {
        return 8;
    }
    return 0;
}
