/* Panthera shim: mach-o/dyld_priv.h
 * Private dyld API declarations needed by the ObjC runtime.
 * These functions are resolved at link time against Panthera's libSystem.
 */
#ifndef _MACH_O_DYLD_PRIV_H
#define _MACH_O_DYLD_PRIV_H

#include <mach-o/loader.h>
#include <mach-o/dyld.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <uuid/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Section location API */
struct _dyld_section_location_info_s;
typedef struct _dyld_section_location_info_s* _dyld_section_location_info_t;

typedef enum {
    _dyld_section_location_data_sel_refs = 0,
    _dyld_section_location_data_msg_refs,
    _dyld_section_location_data_class_refs,
    _dyld_section_location_data_super_refs,
    _dyld_section_location_data_protocol_refs,
    _dyld_section_location_data_class_list,
    _dyld_section_location_data_non_lazy_class_list,
    _dyld_section_location_data_stub_list,
    _dyld_section_location_data_category_list,
    _dyld_section_location_data_category_list2,
    _dyld_section_location_data_non_lazy_category_list,
    _dyld_section_location_data_protocol_list,
    _dyld_section_location_data_objc_fork_ok,
    _dyld_section_location_data_raw_isa,
    _dyld_section_location_objc_image_info,
} _dyld_section_location_kind;

typedef struct {
    const void* buffer;
    size_t      bufferSize;
} _dyld_section_info_result;

extern _dyld_section_info_result
_dyld_lookup_section_info(const struct mach_header *mh,
                          struct _dyld_section_location_info_s * _Nonnull info,
                          _dyld_section_location_kind kind);

/* Image information */
extern const struct mach_header* dyld_image_header_containing_address(const void* addr);
extern const char* dyld_image_path_containing_address(const void* addr);
extern bool _dyld_is_memory_immutable(const void* addr, size_t length);
extern const struct mach_header* _dyld_get_dlopen_image_header(void* handle);
extern const struct mach_header* _dyld_get_prog_image_header(void);
extern size_t _dyld_get_shared_cache_range(size_t *mappedSize);
extern bool _dyld_get_image_uuid(const struct mach_header* mh, uuid_t uuid);

/* ObjC notification callbacks */
typedef void (*_dyld_objc_notify_mapped)(unsigned count,
                                         const char* const paths[],
                                         const struct mach_header* const mhdrs[]);
typedef void (*_dyld_objc_notify_init)(const char* path,
                                       const struct mach_header* mh);
typedef void (*_dyld_objc_notify_unmapped)(const char* path,
                                           const struct mach_header* mh);

extern void _dyld_objc_notify_register(_dyld_objc_notify_mapped mapped,
                                        _dyld_objc_notify_init init,
                                        _dyld_objc_notify_unmapped unmapped);

/* ObjC callbacks struct — used by _dyld_objc_register_callbacks */
typedef struct _dyld_objc_callbacks {
    uintptr_t version;
} _dyld_objc_callbacks;

typedef struct _dyld_objc_callbacks_v2 {
    uintptr_t version;  /* = 2 */
    void (*map_images)(unsigned count,
                       const struct _dyld_objc_notify_mapped_info infos[]);
    void (*load_image)(const struct _dyld_objc_notify_mapped_info* info);
    void (*unmap_image)(const char* path, const struct mach_header* mh);
    void (*patch_root)(const struct mach_header* originalMH, void* originalClass,
                       const struct mach_header* replacementMH, const void* replacementClass);
} _dyld_objc_callbacks_v2;

extern void _dyld_objc_register_callbacks(const _dyld_objc_callbacks *callbacks);

struct _dyld_objc_notify_mapped_info {
    const struct mach_header* mh;
    const char*              path;
    _dyld_section_location_info_t sectionLocationMetadata;
    bool                     dyldObjCRefsOptimized;
    bool                     _reserved;
};

/* ObjC selector and class lookup */
extern const char* _dyld_get_objc_selector(const char* selName);
extern uint32_t _dyld_objc_class_count(void);

extern void _dyld_for_each_objc_class(const char* className,
    void (^callback)(void* classPtr, bool isLoaded, bool* stop));
extern void _dyld_for_each_objc_protocol(const char* protocolName,
    void (^callback)(void* protocolPtr, bool isLoaded, bool* stop));

/* dyld header optimization info — returns pointers to shared cache data */
extern const void* _dyld_for_objc_header_opt_ro(void);
extern void* _dyld_for_objc_header_opt_rw(void);

/* SDK version checking */
typedef uint32_t dyld_platform_t;
typedef struct {
    dyld_platform_t platform;
    uint32_t        version;
} dyld_build_version_t;

#define dyld_platform_version_macOS_10_11   (dyld_build_version_t){1, 0x000A0B00}
#define dyld_platform_version_macOS_10_12   (dyld_build_version_t){1, 0x000A0C00}
#define dyld_platform_version_macOS_10_13   (dyld_build_version_t){1, 0x000A0D00}
#define dyld_platform_version_macOS_10_14   (dyld_build_version_t){1, 0x000A0E00}
#define dyld_platform_version_macOS_10_15   (dyld_build_version_t){1, 0x000A0F00}
#define dyld_platform_version_macOS_10_16   (dyld_build_version_t){1, 0x000A1000}
#define dyld_platform_version_macOS_11_0    (dyld_build_version_t){1, 0x000B0000}
#define dyld_platform_version_macOS_12_0    (dyld_build_version_t){1, 0x000C0000}
#define dyld_platform_version_macOS_13_0    (dyld_build_version_t){1, 0x000D0000}
#define dyld_platform_version_macOS_14_0    (dyld_build_version_t){1, 0x000E0000}

#define dyld_platform_version_iOS_9_0       (dyld_build_version_t){2, 0x00090000}
#define dyld_platform_version_iOS_10_0      (dyld_build_version_t){2, 0x000A0000}
#define dyld_platform_version_iOS_11_0      (dyld_build_version_t){2, 0x000B0000}
#define dyld_platform_version_iOS_12_0      (dyld_build_version_t){2, 0x000C0000}
#define dyld_platform_version_iOS_13_0      (dyld_build_version_t){2, 0x000D0000}
#define dyld_platform_version_iOS_14_0      (dyld_build_version_t){2, 0x000E0000}
#define dyld_platform_version_iOS_15_0      (dyld_build_version_t){2, 0x000F0000}
#define dyld_platform_version_iOS_16_0      (dyld_build_version_t){2, 0x00100000}
#define dyld_platform_version_iOS_17_0      (dyld_build_version_t){2, 0x00110000}

#define dyld_platform_version_tvOS_9_0      (dyld_build_version_t){3, 0x00090000}
#define dyld_platform_version_tvOS_10_0     (dyld_build_version_t){3, 0x000A0000}
#define dyld_platform_version_tvOS_11_0     (dyld_build_version_t){3, 0x000B0000}
#define dyld_platform_version_tvOS_12_0     (dyld_build_version_t){3, 0x000C0000}
#define dyld_platform_version_tvOS_13_0     (dyld_build_version_t){3, 0x000D0000}
#define dyld_platform_version_tvOS_14_0     (dyld_build_version_t){3, 0x000E0000}
#define dyld_platform_version_tvOS_15_0     (dyld_build_version_t){3, 0x000F0000}
#define dyld_platform_version_tvOS_16_0     (dyld_build_version_t){3, 0x00100000}
#define dyld_platform_version_tvOS_17_0     (dyld_build_version_t){3, 0x00110000}

#define dyld_platform_version_watchOS_1_0   (dyld_build_version_t){4, 0x00010000}
#define dyld_platform_version_watchOS_2_0   (dyld_build_version_t){4, 0x00020000}
#define dyld_platform_version_watchOS_3_0   (dyld_build_version_t){4, 0x00030000}
#define dyld_platform_version_watchOS_4_0   (dyld_build_version_t){4, 0x00040000}
#define dyld_platform_version_watchOS_5_0   (dyld_build_version_t){4, 0x00050000}
#define dyld_platform_version_watchOS_6_0   (dyld_build_version_t){4, 0x00060000}
#define dyld_platform_version_watchOS_7_0   (dyld_build_version_t){4, 0x00070000}
#define dyld_platform_version_watchOS_8_0   (dyld_build_version_t){4, 0x00080000}
#define dyld_platform_version_watchOS_9_0   (dyld_build_version_t){4, 0x00090000}
#define dyld_platform_version_watchOS_10_0  (dyld_build_version_t){4, 0x000A0000}

#define dyld_platform_version_bridgeOS_2_0  (dyld_build_version_t){5, 0x00020000}
#define dyld_platform_version_bridgeOS_3_0  (dyld_build_version_t){5, 0x00030000}
#define dyld_platform_version_bridgeOS_4_0  (dyld_build_version_t){5, 0x00040000}
#define dyld_platform_version_bridgeOS_5_0  (dyld_build_version_t){5, 0x00050000}
#define dyld_platform_version_bridgeOS_6_0  (dyld_build_version_t){5, 0x00060000}
#define dyld_platform_version_bridgeOS_7_0  (dyld_build_version_t){5, 0x00070000}
#define dyld_platform_version_bridgeOS_8_0  (dyld_build_version_t){5, 0x00080000}

extern bool dyld_program_sdk_at_least(dyld_build_version_t version);
extern bool dyld_shared_cache_some_image_overridden(void);
extern uint32_t dyld_get_active_platform(void);

/* Platform constants */
#ifndef PLATFORM_MACOS
#define PLATFORM_MACOS 1
#endif

/* "Fall 20xx" OS version bundles for SDK checks */
#define dyld_fall_2018_os_versions (dyld_build_version_t){1, 0x000A0E00}
#define dyld_fall_2020_os_versions (dyld_build_version_t){1, 0x000B0000}

/* Fall-through for the objc_os.h crt_externs need */
extern char ***_NSGetArgv(void);
extern int *_NSGetArgc(void);

#ifdef __cplusplus
}
#endif

#endif /* _MACH_O_DYLD_PRIV_H */
