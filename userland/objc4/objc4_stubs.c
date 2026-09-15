/*
 * objc4_stubs.c — Stub implementations for symbols needed by libobjc.A.dylib
 * These are added to libpanthera_extra.dylib via panthera_patch.sh impl.
 * Only includes symbols NOT already in the sysroot.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Minimal Mach-O struct definitions (avoid mach/mach.h conflicts) */
struct mach_header {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
};

struct mach_header_64 {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

#define LC_SEGMENT_64 0x19

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct section_64 {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

/* ================================================================
 * Mach-O section lookup — getsegmentdata only (getsectiondata exists)
 * ================================================================ */

uint8_t *getsegmentdata(const struct mach_header_64 *mhp,
                        const char *segname,
                        unsigned long *size)
{
    if (!mhp || !size) return NULL;
    *size = 0;

    const uint8_t *ptr = (const uint8_t *)(mhp + 1);
    intptr_t text_slide = 0;

    for (uint32_t i = 0; i < mhp->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)ptr;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (strncmp(seg->segname, "__TEXT", 16) == 0) {
                text_slide = (intptr_t)mhp - (intptr_t)seg->vmaddr;
                break;
            }
        }
        ptr += lc->cmdsize;
    }

    ptr = (const uint8_t *)(mhp + 1);
    for (uint32_t i = 0; i < mhp->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)ptr;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (strncmp(seg->segname, segname, 16) == 0) {
                *size = (unsigned long)seg->vmsize;
                return (uint8_t *)((intptr_t)seg->vmaddr + text_slide);
            }
        }
        ptr += lc->cmdsize;
    }
    return NULL;
}

/* ================================================================
 * Unwind / exception handling stubs
 * ================================================================ */

typedef int unw_cursor_t[128];
typedef int unw_context_t[128];
typedef int unw_regnum_t;
typedef struct { uintptr_t start_ip; uintptr_t end_ip; } unw_proc_info_t;

int unw_getcontext(unw_context_t *ctx) { (void)ctx; return -1; }
int unw_init_local(unw_cursor_t *c, unw_context_t *ctx) { (void)c; (void)ctx; return -1; }
int unw_step(unw_cursor_t *c) { (void)c; return 0; }
int unw_get_reg(unw_cursor_t *c, unw_regnum_t r, uintptr_t *v) { (void)c; (void)r; if(v)*v=0; return -1; }
int unw_get_proc_info(unw_cursor_t *c, unw_proc_info_t *p) { (void)c; if(p) memset(p,0,sizeof(*p)); return -1; }

typedef void *_Unwind_Context;
uintptr_t _Unwind_GetCFA(_Unwind_Context *ctx) { (void)ctx; return 0; }
uintptr_t _Unwind_GetIP(_Unwind_Context *ctx) { (void)ctx; return 0; }

/* ================================================================
 * dyld private API stubs
 * ================================================================ */

const struct mach_header *dyld_image_header_containing_address(const void *addr) {
    (void)addr; return NULL;
}

const char *dyld_image_path_containing_address(const void *addr) {
    (void)addr; return NULL;
}

const struct mach_header *_dyld_get_dlopen_image_header(void *handle) {
    (void)handle; return NULL;
}

const struct mach_header *_dyld_get_prog_image_header(void) {
    return NULL;
}

size_t _dyld_get_shared_cache_range(size_t *mappedSize) {
    if (mappedSize) *mappedSize = 0;
    return 0;
}

/* _dyld_get_image_uuid — already exists as stub in panthera_extra_stubs.c */

const char *_dyld_get_objc_selector(const char *selName) {
    (void)selName; return NULL;
}

uint32_t _dyld_objc_class_count(void) { return 0; }

void _dyld_for_each_objc_class(const char *className,
    void (^callback)(void *, int, int *)) {
    (void)className; (void)callback;
}

void _dyld_for_each_objc_protocol(const char *protocolName,
    void (^callback)(void *, int, int *)) {
    (void)protocolName; (void)callback;
}

const void *_dyld_for_objc_header_opt_ro(void) { return NULL; }
void *_dyld_for_objc_header_opt_rw(void) { return NULL; }

typedef struct { const void *buffer; size_t bufferSize; } _panthera_section_result;
_panthera_section_result _dyld_lookup_section_info(
    const struct mach_header *mh, void *info, int kind)
{
    (void)mh; (void)info; (void)kind;
    _panthera_section_result result = {NULL, 0};
    return result;
}

void _dyld_objc_register_callbacks(const void *callbacks) {
    (void)callbacks;
}

int dyld_program_sdk_at_least(void *version) {
    (void)version; return 1;
}

int dyld_shared_cache_some_image_overridden(void) { return 0; }

uint32_t dyld_get_active_platform(void) { return 1; /* PLATFORM_MACOS */ }

/* is_root_ramdisk — defined in objc-runtime-new.mm */
