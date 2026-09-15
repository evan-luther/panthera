/*
 * panthera_extra_stubs.c
 *
 * Consolidated stubs and placeholder implementations for libpanthera_extra.
 * The legacy sources are grouped here so relinking no longer depends on
 * prebuilt objects or an ld -r duplicate-resolution pass.
 */

/*
 * Keep the real exported environment pointer in the bridge translation unit.
 * The legacy missing-symbol source also defines environ and _NSGetEnviron;
 * rename those private copies here and re-export _NSGetEnviron explicitly.
 */
#define environ panthera_private_missing_environ
#define _NSGetEnviron panthera_private_NSGetEnviron
#include "panthera_missing.c"
#undef _NSGetEnviron
#undef environ

extern char **environ;

char ***_NSGetEnviron(void) {
    return &environ;
}

#define _sc panthera_pthread_simple_sc
#include "panthera_pthread_simple.c"
#undef _sc

/* notify stubs removed — real implementation in libsystem_notify.dylib */

#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_RDONLY
#define O_RDONLY 0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY 0x0001
#endif
#ifndef O_CREAT
#define O_CREAT 0x0200
#endif
#ifndef O_TRUNC
#define O_TRUNC 0x0400
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#define PANTHERA_DYLD_BRIDGE_MAX_IMAGES 64

void *malloc(size_t size);
void free(void *ptr);

struct panthera_dyld_bridge_image {
    const struct mach_header* header;
    long                      slide;
    const char*               path;
    unsigned char             has_objc;
};

struct panthera_dyld_bridge_state {
    unsigned int                    image_count;
    unsigned long                   shared_cache_base;
    unsigned long                   shared_cache_size;
    const struct mach_header*       prog_image_header;
    struct panthera_dyld_bridge_image images[PANTHERA_DYLD_BRIDGE_MAX_IMAGES];
};

struct panthera_dyld_bridge_state panthera_dyld_bridge_state = {0};

ssize_t panthera_dlopen_write(int fd, const void *buf, size_t count)
    __asm__("_write$UNIX2003");
ssize_t panthera_dlopen_read_nocancel(int fd, void *buf, size_t count)
    __asm__("_read$NOCANCEL");

static void
panthera_bridge_trace(const char *msg)
{
    size_t n = 0;
    while (msg[n] != '\0')
        n++;
    (void)panthera_dlopen_write(STDERR_FILENO, msg, n);
}

static void
panthera_bridge_puthex(uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    char buf[2 + 16];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 16; i++) {
        buf[2 + i] = hex[(value >> ((15 - i) * 4)) & 0xF];
    }
    (void)write(STDERR_FILENO, buf, sizeof(buf));
}

static int
panthera_bridge_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

struct panthera_dyld_objc_notify_mapped_info {
    const struct mach_header* mh;
    const char*               path;
    void*                     sectionLocationMetadata;
    int                       dyldObjCRefsOptimized;
    int                       reserved;
};

struct panthera_dyld_objc_callbacks_v1 {
    uintptr_t version;
    void (*mapped)(unsigned, const char* const[], const struct mach_header* const[]);
    void (*init)(const char*, const struct mach_header*);
    void (*unmapped)(const char*, const struct mach_header*);
    void (*patches)(const struct mach_header*, void*, const struct mach_header*, const void*);
};

struct panthera_dyld_objc_callbacks_v2 {
    uintptr_t version;
    void (*mapped)(unsigned, const struct panthera_dyld_objc_notify_mapped_info[]);
    void (*init)(const struct panthera_dyld_objc_notify_mapped_info*);
    void (*unmapped)(const char*, const struct mach_header*);
    void (*patches)(const struct mach_header*, void*, const struct mach_header*, const void*);
};

static int panthera_bridge_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static const struct segment_command_64 *
panthera_bridge_find_segment(const struct mach_header_64 *mh, const char *segname)
{
    const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            if (strncmp(seg->segname, segname, sizeof(seg->segname)) == 0)
                return seg;
        }
        cmd += lc->cmdsize;
    }
    return NULL;
}

static const struct section_64 *
panthera_bridge_find_section_any(const struct mach_header_64 *mh, const char *sectname)
{
    const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            const struct section_64 *sect =
                (const struct section_64 *)(cmd + sizeof(struct segment_command_64));
            for (uint32_t j = 0; j < seg->nsects; j++) {
                if (strncmp(sect[j].sectname, sectname, sizeof(sect[j].sectname)) == 0)
                    return &sect[j];
            }
        }
        cmd += lc->cmdsize;
    }
    return NULL;
}

static const struct panthera_dyld_bridge_image *
panthera_bridge_find_image_containing(const void *addr)
{
    unsigned long target = (unsigned long)addr;
    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct panthera_dyld_bridge_image *img = &panthera_dyld_bridge_state.images[i];
        const struct mach_header_64 *mh = (const struct mach_header_64 *)img->header;
        if (!mh || mh->magic != MH_MAGIC_64)
            continue;
        const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);
        for (uint32_t j = 0; j < mh->ncmds; j++) {
            const struct load_command *lc = (const struct load_command *)cmd;
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
                unsigned long start = (unsigned long)(seg->vmaddr + img->slide);
                unsigned long end = start + (unsigned long)seg->vmsize;
                if (target >= start && target < end)
                    return img;
            }
            cmd += lc->cmdsize;
        }
    }
    return NULL;
}

const char *_dyld_get_objc_selector(const char *selName) {
    (void)selName;
    return NULL;
}

uint32_t _dyld_objc_class_count(void) { return 0; }

void _dyld_for_each_objc_class(const char *className, void *callback) {
    (void)className;
    (void)callback;
}

void _dyld_for_each_objc_protocol(const char *protocolName, void *callback) {
    (void)protocolName;
    (void)callback;
}

const void *_dyld_for_objc_header_opt_ro(void) { return NULL; }
void *_dyld_for_objc_header_opt_rw(void) { return NULL; }

const struct mach_header *_dyld_get_dlopen_image_header(void *handle) {
    const struct panthera_dyld_bridge_image *img =
        panthera_bridge_find_image_containing(handle);
    if (img && img->header == (const struct mach_header *)handle)
        return img->header;
    return NULL;
}

const struct mach_header *_dyld_get_prog_image_header(void) {
    return panthera_dyld_bridge_state.prog_image_header;
}

size_t _dyld_get_shared_cache_range(size_t *mappedSize) {
    if (mappedSize)
        *mappedSize = (size_t)panthera_dyld_bridge_state.shared_cache_size;
    return (size_t)panthera_dyld_bridge_state.shared_cache_base;
}

const struct mach_header *dyld_image_header_containing_address(const void *addr) {
    const struct panthera_dyld_bridge_image *img = panthera_bridge_find_image_containing(addr);
    return img ? img->header : NULL;
}

const char *dyld_image_path_containing_address(const void *addr) {
    const struct panthera_dyld_bridge_image *img = panthera_bridge_find_image_containing(addr);
    return img ? img->path : NULL;
}

int dyld_program_sdk_at_least(void *version) {
    (void)version;
    return 1;
}

int dyld_shared_cache_some_image_overridden(void) { return 0; }

typedef struct { const void *buffer; size_t bufferSize; } _panthera_section_result;
_panthera_section_result _dyld_lookup_section_info(
    const struct mach_header *mh, void *info, int kind)
{
    static const char * const section_names[] = {
        "__objc_selrefs",
        "__objc_msgrefs",
        "__objc_classrefs",
        "__objc_superrefs",
        "__objc_protorefs",
        "__objc_classlist",
        "__objc_nlclslist",
        "__objc_stublist",
        "__objc_catlist",
        "__objc_catlist2",
        "__objc_nlcatlist",
        "__objc_protolist",
        "__objc_fork_ok",
        "__objc_rawisa",
        "__objc_imageinfo",
    };
    _panthera_section_result result = { NULL, 0 };
    const struct mach_header_64 *mh64 = (const struct mach_header_64 *)mh;
    const struct section_64 *sect;
    const struct segment_command_64 *text;
    long slide;
    (void)info;

    if (!mh64 || mh64->magic != MH_MAGIC_64)
        return result;
    if (kind < 0 || kind >= (int)(sizeof(section_names) / sizeof(section_names[0])))
        return result;
    sect = panthera_bridge_find_section_any(mh64, section_names[kind]);
    text = panthera_bridge_find_segment(mh64, "__TEXT");
    if (!sect || !text)
        return result;
    slide = (long)((const char *)mh64 - (const char *)(uintptr_t)text->vmaddr);
    result.buffer = (const void *)(uintptr_t)(sect->addr + slide);
    result.bufferSize = (size_t)sect->size;
    return result;
}

static int
panthera_bridge_has_prefix(const char *s, const char *prefix, size_t n)
{
    size_t i;

    if (!s || !prefix)
        return 0;
    for (i = 0; i < n; i++) {
        if (prefix[i] == '\0')
            return 1;
        if (s[i] != prefix[i])
            return 0;
    }
    return 1;
}

/*
 * Independent ObjC presence check — do not trust dyld's has_objc flag
 * when it says "no". This compensates for the producer's hardcoded skip.
 */
static int
_panthera_image_has_objc(const struct mach_header *mh)
{
    const uint8_t *cmd;
    uint32_t i;
    int is64;

    if (!mh)
        return 0;

    is64 = (mh->magic == MH_MAGIC_64);
    if (!is64 && mh->magic != MH_MAGIC)
        return 0;

    cmd = (const uint8_t *)mh +
        (is64 ? sizeof(struct mach_header_64) : sizeof(struct mach_header));

    for (i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;

        if (lc->cmdsize < sizeof(struct load_command))
            return 0;

        if (is64 && lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            const struct section_64 *sect;
            uint32_t j;

            if (lc->cmdsize < sizeof(struct segment_command_64))
                return 0;
            if (panthera_bridge_has_prefix(seg->segname, "__OBJC", 6))
                return 1;

            sect = (const struct section_64 *)(cmd + sizeof(struct segment_command_64));
            for (j = 0; j < seg->nsects; j++) {
                if (panthera_bridge_has_prefix(sect[j].sectname, "__objc_", 7) ||
                    panthera_bridge_has_prefix(sect[j].sectname, "__OBJC", 6))
                    return 1;
            }
        } else if (!is64 && lc->cmd == LC_SEGMENT) {
            const struct segment_command *seg = (const struct segment_command *)cmd;
            const struct section *sect;
            uint32_t j;

            if (lc->cmdsize < sizeof(struct segment_command))
                return 0;
            if (panthera_bridge_has_prefix(seg->segname, "__OBJC", 6))
                return 1;

            sect = (const struct section *)(cmd + sizeof(struct segment_command));
            for (j = 0; j < seg->nsects; j++) {
                if (panthera_bridge_has_prefix(sect[j].sectname, "__objc_", 7) ||
                    panthera_bridge_has_prefix(sect[j].sectname, "__OBJC", 6))
                    return 1;
            }
        }

        cmd += lc->cmdsize;
    }

    return 0;
}

void _dyld_objc_register_callbacks(const void *callbacks) {
    const struct panthera_dyld_objc_callbacks_v1 *v1;
    const struct panthera_dyld_objc_callbacks_v2 *v2;
    struct panthera_dyld_objc_notify_mapped_info infos[PANTHERA_DYLD_BRIDGE_MAX_IMAGES];
    const char *paths[PANTHERA_DYLD_BRIDGE_MAX_IMAGES];
    const struct mach_header *headers[PANTHERA_DYLD_BRIDGE_MAX_IMAGES];
    unsigned int count = 0;

    if (!callbacks)
        return;

    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct panthera_dyld_bridge_image *img = &panthera_dyld_bridge_state.images[i];
        if (!img->header)
            continue;
        if (img->path) {
            const char *p = img->path;
            const char *base = p;
            while (*p) {
                if (*p == '/')
                    base = p + 1;
                p++;
            }
            if (base[0] == 'l' && base[1] == 'i' && base[2] == 'b' &&
                base[3] == 'o' && base[4] == 'b' && base[5] == 'j' &&
                base[6] == 'c')
                continue;
        }
        if (!img->has_objc && !_panthera_image_has_objc(img->header))
            continue;
        infos[count].mh = img->header;
        infos[count].path = img->path;
        infos[count].sectionLocationMetadata = NULL;
        infos[count].dyldObjCRefsOptimized = 0;
        infos[count].reserved = 0;
        paths[count] = img->path;
        headers[count] = img->header;
        count++;
    }

    if (count == 0)
        return;

    if (*(const uintptr_t *)callbacks == 1) {
        v1 = (const struct panthera_dyld_objc_callbacks_v1 *)callbacks;
        if (v1->mapped)
            v1->mapped(count, paths, headers);
    } else if (*(const uintptr_t *)callbacks == 2) {
        v2 = (const struct panthera_dyld_objc_callbacks_v2 *)callbacks;
        if (v2->mapped)
            v2->mapped(count, infos);
    }
}

/* OSAtomicTestAndSetBarrier */
int OSAtomicTestAndSetBarrier(unsigned int bit, volatile void *addr) {
    volatile unsigned char *byte = (volatile unsigned char *)addr + (bit >> 3);
    unsigned char mask = (unsigned char)(0x80 >> (bit & 7));
    unsigned char old = *byte;
    *byte |= mask;
    return (old & mask) != 0;
}

/* errno / h_errno */
static int _panthera_errno_storage;
int *panthera_errno_fn(void) __asm__("_errno");
int *panthera_errno_fn(void) { return &_panthera_errno_storage; }

static int _panthera_h_errno;
int *panthera_h_errno_fn(void) __asm__("_h_errno");
int *panthera_h_errno_fn(void) { return &_panthera_h_errno; }

/* String and UUID helpers */
char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (str == NULL)
        str = *saveptr;
    if (str == NULL)
        return NULL;

    while (*str) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim)
            break;
        str++;
    }

    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *token = str;
    while (*str) {
        const char *d = delim;
        while (*d) {
            if (*str == *d) {
                *str = '\0';
                *saveptr = str + 1;
                return token;
            }
            d++;
        }
        str++;
    }

    *saveptr = NULL;
    return token;
}

typedef unsigned char uuid_t[16];

void uuid_copy(uuid_t dst, const uuid_t src) {
    for (int i = 0; i < 16; i++)
        dst[i] = src[i];
}

int uuid_compare(const uuid_t a, const uuid_t b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] < b[i])
            return -1;
        if (a[i] > b[i])
            return 1;
    }
    return 0;
}

void uuid_unparse(const uuid_t uu, char *out) {
    static const char hex[] = "0123456789abcdef";
    int p = 0;
    for (int i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out[p++] = '-';
        out[p++] = hex[uu[i] >> 4];
        out[p++] = hex[uu[i] & 0xf];
    }
    out[p] = '\0';
}

/* inet */
int inet_aton(const char *cp, void *addr) {
    unsigned int parts[4];
    const char *p = cp;

    for (int n = 0; n < 4 && *p; n++) {
        unsigned int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        parts[n] = v;
        if (n == 3)
            break;
        if (*p != '.')
            return 0;
        p++;
    }

    if (parts[0] > 255 || parts[1] > 255 || parts[2] > 255 || parts[3] > 255)
        return 0;

    if (addr) {
        unsigned int ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
        *(unsigned int *)addr = __builtin_bswap32(ip);
    }
    return 1;
}

int inet_pton(int af, const char *src, void *dst) {
    if (af == 2)
        return inet_aton(src, dst);
    return 0;
}

extern int getentropy(void *buffer, unsigned long length);

static int _panthera_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

#ifndef LC_DYLD_CHAINED_FIXUPS
#define LC_DYLD_CHAINED_FIXUPS 0x80000034
#endif
#ifndef DYLD_CHAINED_PTR_START_NONE
#define DYLD_CHAINED_PTR_START_NONE 0xFFFF
#endif
#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0x10
#endif
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)-2)
#endif

#define PANTHERA_DLOPEN_MAX_BUNDLES 8
#define PANTHERA_DLOPEN_MAX_PATH 1024

struct panthera_dlopen_bundle {
    const struct mach_header_64 *header;
    long slide;
    size_t mapping_size;
    unsigned int ref_count;
    char path[PANTHERA_DLOPEN_MAX_PATH];
};

typedef struct {
    const char *dli_fname;
    void *dli_fbase;
    const char *dli_sname;
    void *dli_saddr;
} Dl_info;

struct panthera_dyld_chained_fixups_header {
    uint32_t fixups_version;
    uint32_t starts_offset;
    uint32_t imports_offset;
    uint32_t symbols_offset;
    uint32_t imports_count;
    uint32_t imports_format;
    uint32_t symbols_format;
};

struct panthera_dyld_chained_starts_in_image {
    uint32_t seg_count;
    uint32_t seg_info_offset[1];
};

struct panthera_dyld_chained_starts_in_segment {
    uint32_t size;
    uint16_t page_size;
    uint16_t pointer_format;
    uint64_t segment_offset;
    uint32_t max_valid_pointer;
    uint16_t page_count;
    uint16_t page_start[1];
};

struct panthera_dyld_chained_import {
    uint32_t lib_ordinal :  8;
    uint32_t weak_import :  1;
    uint32_t name_offset : 23;
};

static int _panthera_dlopen_sentinel = 1;
static struct panthera_dlopen_bundle panthera_dlopen_bundles[PANTHERA_DLOPEN_MAX_BUNDLES];
static unsigned int panthera_dlopen_bundle_count;
static char panthera_dlopen_error[160];

const char *
panthera_dlopen_last_error(void)
{
    return panthera_dlopen_error[0] ? panthera_dlopen_error : NULL;
}

static void
panthera_dlopen_set_error(const char *message)
{
    if (message == NULL) {
        panthera_dlopen_error[0] = '\0';
        return;
    }
    strlcpy(panthera_dlopen_error, message, sizeof(panthera_dlopen_error));
}

static const char *
panthera_basename(const char *path)
{
    const char *base = path;

    if (path == NULL)
        return "";
    while (*path) {
        if (*path == '/')
            base = path + 1;
        path++;
    }
    return base;
}

static void
panthera_dlopen_trace_path(const char *prefix, const char *path)
{
    const char *key = "PANTHERA_DLOPEN_TRACE=1";
    for (char **env = environ; env && *env; env++) {
        const char *a = *env;
        const char *b = key;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (*b == '\0' && *a == '\0')
            goto enabled;
    }
    return;

enabled:
    if (prefix != NULL)
        (void)panthera_dlopen_write(STDERR_FILENO, prefix, strlen(prefix));
    if (path != NULL)
        (void)panthera_dlopen_write(STDERR_FILENO, path, strlen(path));
    (void)panthera_dlopen_write(STDERR_FILENO, "\n", 1);
}

static const struct panthera_dlopen_bundle *
panthera_find_dlopen_bundle(const char *path)
{
    const char *want_base = panthera_basename(path);

    for (unsigned int i = 0; i < panthera_dlopen_bundle_count; i++) {
        const struct panthera_dlopen_bundle *bundle = &panthera_dlopen_bundles[i];
        if (_panthera_strcmp(bundle->path, path) == 0 ||
            _panthera_strcmp(panthera_basename(bundle->path), want_base) == 0) {
            return bundle;
        }
    }
    return NULL;
}

static const struct panthera_dlopen_bundle *
panthera_find_dlopen_bundle_by_handle(const void *handle)
{
    for (unsigned int i = 0; i < panthera_dlopen_bundle_count; i++) {
        if (panthera_dlopen_bundles[i].header == (const struct mach_header_64 *)handle)
            return &panthera_dlopen_bundles[i];
    }
    return NULL;
}

static struct panthera_dlopen_bundle *
panthera_find_mutable_dlopen_bundle_by_handle(const void *handle)
{
    for (unsigned int i = 0; i < panthera_dlopen_bundle_count; i++) {
        if (panthera_dlopen_bundles[i].header == (const struct mach_header_64 *)handle)
            return &panthera_dlopen_bundles[i];
    }
    return NULL;
}

static void
panthera_forget_bridge_image_by_header(const struct mach_header_64 *header)
{
    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        if (panthera_dyld_bridge_state.images[i].header != (const struct mach_header *)header)
            continue;
        for (unsigned int j = i + 1; j < panthera_dyld_bridge_state.image_count; j++)
            panthera_dyld_bridge_state.images[j - 1] = panthera_dyld_bridge_state.images[j];
        panthera_dyld_bridge_state.image_count--;
        return;
    }
}

static void
panthera_refresh_dlopen_bridge_paths(void)
{
    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        struct panthera_dyld_bridge_image *img = &panthera_dyld_bridge_state.images[i];
        for (unsigned int j = 0; j < panthera_dlopen_bundle_count; j++) {
            const struct panthera_dlopen_bundle *bundle = &panthera_dlopen_bundles[j];
            if (img->header == (const struct mach_header *)bundle->header) {
                img->slide = bundle->slide;
                img->path = bundle->path;
                break;
            }
        }
    }
}

static const struct panthera_dyld_bridge_image *
panthera_find_bridge_image_by_path(const char *path)
{
    const char *want_base = panthera_basename(path);

    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct panthera_dyld_bridge_image *img = &panthera_dyld_bridge_state.images[i];
        if (img->path != NULL &&
            (_panthera_strcmp(img->path, path) == 0 ||
             _panthera_strcmp(panthera_basename(img->path), want_base) == 0)) {
            return img;
        }
    }
    return NULL;
}

static const struct panthera_dyld_bridge_image *
panthera_find_bridge_image_by_handle(const void *handle)
{
    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct panthera_dyld_bridge_image *img = &panthera_dyld_bridge_state.images[i];
        if (img->header == (const struct mach_header *)handle)
            return img;
    }
    return NULL;
}

static int
panthera_image_contains_address(const struct mach_header_64 *mh, long slide,
                                const void *addr)
{
    unsigned long target = (unsigned long)addr;
    const unsigned char *cmd;

    if (mh == NULL || mh->magic != MH_MAGIC_64 || addr == NULL)
        return 0;

    cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            unsigned long start = (unsigned long)(seg->vmaddr + slide);
            unsigned long end = start + (unsigned long)seg->vmsize;
            if (target >= start && target < end)
                return 1;
        }
        cmd += lc->cmdsize;
    }
    return 0;
}

static void
panthera_symbol_name(const char *symbol, char *buffer, size_t size)
{
    if (size == 0)
        return;
    if (symbol == NULL) {
        buffer[0] = '\0';
        return;
    }
    if (symbol[0] == '_') {
        strlcpy(buffer, symbol, size);
    } else {
        buffer[0] = '_';
        strlcpy(buffer + 1, symbol, size - 1);
    }
}

static void *
panthera_find_symbol_in_macho(const struct mach_header_64 *mh, long slide,
                              const char *symbol)
{
    const struct segment_command_64 *text;
    const struct segment_command_64 *linkedit;
    const struct nlist_64 *symtab = NULL;
    const char *strtab = NULL;
    uint32_t nsyms = 0;
    const unsigned char *cmd;
    char underscored[256];

    if (mh == NULL || mh->magic != MH_MAGIC_64 || symbol == NULL)
        return NULL;

    panthera_symbol_name(symbol, underscored, sizeof(underscored));
    text = panthera_bridge_find_segment(mh, SEG_TEXT);
    linkedit = panthera_bridge_find_segment(mh, SEG_LINKEDIT);
    if (text == NULL || linkedit == NULL)
        return NULL;

    uintptr_t linkedit_base = (uintptr_t)mh
        + (uintptr_t)(linkedit->vmaddr - text->vmaddr)
        - (uintptr_t)linkedit->fileoff;

    cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SYMTAB) {
            const struct symtab_command *sc = (const struct symtab_command *)cmd;
            symtab = (const struct nlist_64 *)(linkedit_base + sc->symoff);
            strtab = (const char *)(linkedit_base + sc->stroff);
            nsyms = sc->nsyms;
            break;
        }
        cmd += lc->cmdsize;
    }

    if (symtab == NULL || strtab == NULL)
        return NULL;

    for (uint32_t i = 0; i < nsyms; i++) {
        const struct nlist_64 *sym = &symtab[i];
        const char *name;

        if ((sym->n_type & N_TYPE) == N_UNDF)
            continue;
        if ((sym->n_type & N_EXT) == 0)
            continue;
        name = strtab + sym->n_un.n_strx;
        if (_panthera_strcmp(name, underscored) == 0)
            return (void *)(uintptr_t)(sym->n_value + slide);
    }
    return NULL;
}

static void
panthera_find_nearest_symbol_in_macho(const struct mach_header_64 *mh, long slide,
                                      const void *addr, const char **name_out,
                                      void **addr_out)
{
    const struct segment_command_64 *text;
    const struct segment_command_64 *linkedit;
    const struct nlist_64 *symtab = NULL;
    const char *strtab = NULL;
    uint32_t nsyms = 0;
    uint32_t strsize = 0;
    const unsigned char *cmd;
    unsigned long target = (unsigned long)addr;
    unsigned long best = 0;
    const char *best_name = NULL;

    if (name_out)
        *name_out = NULL;
    if (addr_out)
        *addr_out = NULL;
    if (mh == NULL || mh->magic != MH_MAGIC_64 || addr == NULL)
        return;

    text = panthera_bridge_find_segment(mh, SEG_TEXT);
    linkedit = panthera_bridge_find_segment(mh, SEG_LINKEDIT);
    if (text == NULL || linkedit == NULL)
        return;

    uintptr_t linkedit_base = (uintptr_t)mh
        + (uintptr_t)(linkedit->vmaddr - text->vmaddr)
        - (uintptr_t)linkedit->fileoff;

    cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SYMTAB) {
            const struct symtab_command *sc = (const struct symtab_command *)cmd;
            symtab = (const struct nlist_64 *)(linkedit_base + sc->symoff);
            strtab = (const char *)(linkedit_base + sc->stroff);
            nsyms = sc->nsyms;
            strsize = sc->strsize;
            break;
        }
        cmd += lc->cmdsize;
    }

    if (symtab == NULL || strtab == NULL || strsize == 0)
        return;

    for (uint32_t i = 0; i < nsyms; i++) {
        const struct nlist_64 *sym = &symtab[i];
        unsigned long value;
        const char *name;

        if ((sym->n_type & N_STAB) != 0)
            continue;
        if ((sym->n_type & N_TYPE) == N_UNDF)
            continue;
        if (sym->n_un.n_strx == 0 || sym->n_un.n_strx >= strsize)
            continue;

        value = (unsigned long)(sym->n_value + slide);
        if (value == 0 || value > target || value < best)
            continue;

        name = strtab + sym->n_un.n_strx;
        if (name[0] == '\0')
            continue;

        best = value;
        best_name = name;
    }

    if (best_name != NULL) {
        if (name_out)
            *name_out = best_name;
        if (addr_out)
            *addr_out = (void *)best;
    }
}

static void
panthera_run_mod_func_section(const struct mach_header_64 *mh, long slide,
                              const char *sectname)
{
    const unsigned char *cmd;

    if (mh == NULL || mh->magic != MH_MAGIC_64 || sectname == NULL)
        return;

    cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            const struct section_64 *section =
                (const struct section_64 *)(cmd + sizeof(*seg));
            for (uint32_t j = 0; j < seg->nsects; j++, section++) {
                if (_panthera_strcmp(section->sectname, sectname) != 0)
                    continue;
                void (**funcs)(void) =
                    (void (**)(void))(uintptr_t)(section->addr + slide);
                uint64_t count = section->size / sizeof(void *);
                for (uint64_t k = 0; k < count; k++) {
                    if (funcs[k] != NULL)
                        funcs[k]();
                }
                return;
            }
        }
        cmd += lc->cmdsize;
    }
}

static void *
panthera_dlsym_default(const char *symbol)
{
    if (symbol != NULL && _panthera_strcmp(symbol, "getentropy") == 0)
        return (void *)getentropy;

    for (unsigned int i = 0; i < panthera_dlopen_bundle_count; i++) {
        void *addr = panthera_find_symbol_in_macho(panthera_dlopen_bundles[i].header,
                                                   panthera_dlopen_bundles[i].slide,
                                                   symbol);
        if (addr != NULL)
            return addr;
    }

    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct panthera_dyld_bridge_image *img = &panthera_dyld_bridge_state.images[i];
        void *addr = panthera_find_symbol_in_macho((const struct mach_header_64 *)img->header,
                                                   img->slide,
                                                   symbol);
        if (addr != NULL)
            return addr;
    }
    return NULL;
}

static int
panthera_apply_chained_fixups(const struct mach_header_64 *mh, long slide)
{
    const struct segment_command_64 *text = panthera_bridge_find_segment(mh, SEG_TEXT);
    const unsigned char *cmd;

    if (text == NULL)
        return 0;

    cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_DYLD_CHAINED_FIXUPS) {
            const struct linkedit_data_command *ldc = (const struct linkedit_data_command *)cmd;
            const struct segment_command_64 *linkedit = panthera_bridge_find_segment(mh, SEG_LINKEDIT);
            const struct panthera_dyld_chained_fixups_header *header;
            const struct panthera_dyld_chained_starts_in_image *starts;
            const unsigned char *imports_base;
            const char *symbol_strings;
            uintptr_t linkedit_base;

            if (linkedit == NULL)
                return 0;
            linkedit_base = (uintptr_t)mh + (linkedit->vmaddr - text->vmaddr) - linkedit->fileoff;
            header = (const struct panthera_dyld_chained_fixups_header *)(linkedit_base + ldc->dataoff);
            starts = (const struct panthera_dyld_chained_starts_in_image *)
                ((const unsigned char *)header + header->starts_offset);
            imports_base = (const unsigned char *)header + header->imports_offset;
            symbol_strings = (const char *)header + header->symbols_offset;

            for (uint32_t seg = 0; seg < starts->seg_count; seg++) {
                const struct panthera_dyld_chained_starts_in_segment *seg_info;
                uintptr_t seg_base;

                if (starts->seg_info_offset[seg] == 0)
                    continue;
                seg_info = (const struct panthera_dyld_chained_starts_in_segment *)
                    ((const unsigned char *)starts + starts->seg_info_offset[seg]);
                seg_base = (uintptr_t)mh + seg_info->segment_offset;

                for (uint16_t page = 0; page < seg_info->page_count; page++) {
                    uint16_t page_start = seg_info->page_start[page];
                    uintptr_t loc;

                    if (page_start == DYLD_CHAINED_PTR_START_NONE)
                        continue;
                    loc = seg_base + ((uintptr_t)page * seg_info->page_size) + page_start;

                    for (;;) {
                        uint64_t raw = *(uint64_t *)loc;
                        uint32_t next = (uint32_t)((raw >> 51) & 0xFFF);

                        if ((raw >> 63) & 1) {
                            uint32_t ordinal = (uint32_t)(raw & 0xFFFFFF);
                            int8_t addend = (int8_t)((raw >> 24) & 0xFF);

                            if (ordinal < header->imports_count) {
                                const struct panthera_dyld_chained_import *imp =
                                    (const struct panthera_dyld_chained_import *)
                                    (imports_base + ordinal * sizeof(*imp));
                                const char *sym_name = symbol_strings + imp->name_offset;
                                void *addr = panthera_dlsym_default(sym_name);
                                if (addr == NULL && !imp->weak_import) {
                                    panthera_dlopen_set_error("unresolved bundle import: ");
                                    strlcat(panthera_dlopen_error, sym_name,
                                            sizeof(panthera_dlopen_error));
                                    return -1;
                                }
                                *(uint64_t *)loc = addr ? ((uint64_t)(uintptr_t)addr + addend) : 0;
                            }
                        } else {
                            uint64_t target = raw & 0xFFFFFFFFFULL;
                            uint8_t high8 = (uint8_t)((raw >> 36) & 0xFF);
                            uint64_t new_addr;

                            if (seg_info->pointer_format == 2) {
                                new_addr = target + (uint64_t)slide;
                            } else {
                                new_addr = (uint64_t)(uintptr_t)mh + target;
                            }
                            new_addr |= ((uint64_t)high8 << 56);
                            *(uint64_t *)loc = new_addr;
                        }

                        if (next == 0)
                            break;
                        loc += (uintptr_t)next * 4;
                    }
                }
            }
        }
        cmd += lc->cmdsize;
    }
    return 0;
}

static uint64_t
panthera_read_uleb128(const uint8_t **cursor, const uint8_t *end)
{
    uint64_t result = 0;
    unsigned int bit = 0;
    const uint8_t *p = *cursor;

    while (p < end) {
        uint8_t byte = *p++;
        result |= (uint64_t)(byte & 0x7f) << bit;
        if ((byte & 0x80) == 0)
            break;
        bit += 7;
    }
    *cursor = p;
    return result;
}

static void
panthera_apply_rebases(const struct mach_header_64 *mh, long slide)
{
    const struct segment_command_64 *text = panthera_bridge_find_segment(mh, SEG_TEXT);
    const struct segment_command_64 *linkedit = panthera_bridge_find_segment(mh, SEG_LINKEDIT);
    const struct segment_command_64 *segs[16];
    int nsegs = 0;
    const unsigned char *cmd;

    if (text == NULL || linkedit == NULL)
        return;

    cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64 && nsegs < 16)
            segs[nsegs++] = (const struct segment_command_64 *)cmd;
        cmd += lc->cmdsize;
    }

    uintptr_t linkedit_base = (uintptr_t)mh + (linkedit->vmaddr - text->vmaddr) - linkedit->fileoff;
    cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
            const struct dyld_info_command *di = (const struct dyld_info_command *)cmd;
            const uint8_t *p = (const uint8_t *)(linkedit_base + di->rebase_off);
            const uint8_t *end = p + di->rebase_size;
            uint64_t seg_offset = 0;
            int seg_index = 0;

            while (p < end) {
                uint8_t byte = *p++;
                uint8_t opcode = byte & 0xF0;
                uint8_t imm = byte & 0x0F;
                switch (opcode) {
                case 0x00:
                    goto rebase_done;
                case 0x10:
                    break;
                case 0x20:
                    seg_index = imm;
                    seg_offset = 0;
                    seg_offset = panthera_read_uleb128(&p, end);
                    break;
                case 0x50:
                    for (uint8_t j = 0; j < imm && seg_index < nsegs; j++) {
                        uintptr_t *loc = (uintptr_t *)((uintptr_t)mh + segs[seg_index]->vmaddr - text->vmaddr + seg_offset);
                        *loc += slide;
                        seg_offset += sizeof(uintptr_t);
                    }
                    break;
                default:
                    goto rebase_done;
                }
            }
rebase_done:
            ;
        }
        cmd += lc->cmdsize;
    }
}

void *
dlopen(const char *path, int mode)
{
    int fd;
    off_t file_size;
    void *mapped;
    const struct mach_header_64 *file_mh;
    uint64_t lo = ~0ULL;
    uint64_t hi = 0;
    uint64_t total_size;
    void *base;
    long slide;
    const unsigned char *cmd;
    struct panthera_dlopen_bundle *bundle;

    panthera_dlopen_set_error(NULL);
    panthera_dlopen_trace_path("PANTHERA:dlopen enter ", path);
    if (path == NULL) {
        panthera_dlopen_trace_path("PANTHERA:dlopen main-program handle", NULL);
        return (void *)&_panthera_dlopen_sentinel;
    }

    struct panthera_dlopen_bundle *existing =
        (struct panthera_dlopen_bundle *)panthera_find_dlopen_bundle(path);
    if (existing != NULL) {
        if (existing->ref_count != ~0U)
            existing->ref_count++;
        panthera_dlopen_trace_path("PANTHERA:dlopen existing ", path);
        return (void *)existing->header;
    }
    const struct panthera_dyld_bridge_image *bridged = panthera_find_bridge_image_by_path(path);
    if (bridged != NULL) {
        panthera_dlopen_trace_path("PANTHERA:dlopen bridged ", path);
        return (void *)bridged->header;
    }
    if (mode & RTLD_NOLOAD) {
        panthera_dlopen_set_error("image not loaded");
        panthera_dlopen_trace_path("PANTHERA:dlopen noload miss ", path);
        return NULL;
    }
    if (panthera_dlopen_bundle_count >= PANTHERA_DLOPEN_MAX_BUNDLES) {
        panthera_dlopen_set_error("too many dlopen images");
        return NULL;
    }

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        panthera_dlopen_set_error("open failed");
        return NULL;
    }
    panthera_dlopen_trace_path("PANTHERA:dlopen open ok ", path);
    panthera_dlopen_trace_path("PANTHERA:dlopen size enter ", path);
    file_size = lseek(fd, 0, SEEK_END);
    if (file_size <= 0 || lseek(fd, 0, SEEK_SET) < 0) {
        close(fd);
        panthera_dlopen_set_error("size failed");
        return NULL;
    }
    panthera_dlopen_trace_path("PANTHERA:dlopen size ok ", path);
    mapped = mmap(NULL, (size_t)file_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mapped == MAP_FAILED) {
        close(fd);
        panthera_dlopen_set_error("mmap failed");
        return NULL;
    }
    panthera_dlopen_trace_path("PANTHERA:dlopen anonymous map ok ", path);
    memset(mapped, 0, (size_t)file_size);
    panthera_dlopen_trace_path("PANTHERA:dlopen anonymous map touched ", path);
    {
        char *dst = (char *)mapped;
        off_t remaining = file_size;
        panthera_dlopen_trace_path("PANTHERA:dlopen read enter ", path);
        while (remaining > 0) {
            size_t chunk = remaining > 16384 ? 16384 : (size_t)remaining;
            ssize_t nread = panthera_dlopen_read_nocancel(fd, dst, chunk);
            if (nread <= 0) {
                close(fd);
                munmap(mapped, (size_t)file_size);
                panthera_dlopen_set_error("read failed");
                return NULL;
            }
            dst += nread;
            remaining -= nread;
        }
    }
    close(fd);
    panthera_dlopen_trace_path("PANTHERA:dlopen read ok ", path);
    panthera_dlopen_trace_path("PANTHERA:dlopen file mapped ", path);

    file_mh = (const struct mach_header_64 *)mapped;
    if (file_mh->magic != MH_MAGIC_64 || file_mh->filetype != MH_BUNDLE) {
        munmap(mapped, (size_t)file_size);
        panthera_dlopen_set_error("not an MH_BUNDLE");
        return NULL;
    }
    panthera_dlopen_trace_path("PANTHERA:dlopen bundle header ok ", path);

    cmd = (const unsigned char *)file_mh + sizeof(*file_mh);
    for (uint32_t i = 0; i < file_mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            if (seg->vmsize != 0) {
                if (seg->vmaddr < lo)
                    lo = seg->vmaddr;
                if (seg->vmaddr + seg->vmsize > hi)
                    hi = seg->vmaddr + seg->vmsize;
            }
        }
        cmd += lc->cmdsize;
    }
    if (lo == ~0ULL || hi <= lo) {
        munmap(mapped, (size_t)file_size);
        panthera_dlopen_set_error("empty image");
        return NULL;
    }
    panthera_dlopen_trace_path("PANTHERA:dlopen segments ok ", path);

    total_size = (hi - lo + 0xfffULL) & ~0xfffULL;
    base = mmap(NULL, (size_t)total_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) {
        munmap(mapped, (size_t)file_size);
        panthera_dlopen_set_error("vm allocate failed");
        return NULL;
    }
    panthera_dlopen_trace_path("PANTHERA:dlopen vm ok ", path);
    slide = (long)((uintptr_t)base - (uintptr_t)lo);

    cmd = (const unsigned char *)file_mh + sizeof(*file_mh);
    for (uint32_t i = 0; i < file_mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            if (seg->filesize != 0) {
                memcpy((char *)base + (seg->vmaddr - lo),
                       (const char *)mapped + seg->fileoff,
                       (size_t)seg->filesize);
            }
        }
        cmd += lc->cmdsize;
    }
    munmap(mapped, (size_t)file_size);
    panthera_dlopen_trace_path("PANTHERA:dlopen segments copied ", path);

    bundle = &panthera_dlopen_bundles[panthera_dlopen_bundle_count++];
    bundle->header = (const struct mach_header_64 *)base;
    bundle->slide = slide;
    bundle->mapping_size = (size_t)total_size;
    bundle->ref_count = 1;
    strlcpy(bundle->path, path, sizeof(bundle->path));

    panthera_dlopen_trace_path("PANTHERA:dlopen fixups enter ", path);
    if (panthera_apply_chained_fixups(bundle->header, bundle->slide) != 0) {
        panthera_dlopen_trace_path("PANTHERA:dlopen fixups failed ", path);
        panthera_dlopen_bundle_count--;
        munmap(base, (size_t)total_size);
        return NULL;
    }
    panthera_apply_rebases(bundle->header, bundle->slide);
    panthera_dlopen_trace_path("PANTHERA:dlopen fixups ok ", path);
    panthera_run_mod_func_section(bundle->header, bundle->slide, "__mod_init_func");

    if (panthera_dyld_bridge_state.image_count < PANTHERA_DYLD_BRIDGE_MAX_IMAGES) {
        struct panthera_dyld_bridge_image *img =
            &panthera_dyld_bridge_state.images[panthera_dyld_bridge_state.image_count++];
        img->header = (const struct mach_header *)bundle->header;
        img->slide = bundle->slide;
        img->path = bundle->path;
        img->has_objc = 0;
    }

    panthera_dlopen_trace_path("PANTHERA:dlopen return ", path);
    return (void *)bundle->header;
}

int
dladdr(const void *addr, Dl_info *info)
{
    const char *name = NULL;
    void *symbol_addr = NULL;

    if (info != NULL) {
        info->dli_fname = NULL;
        info->dli_fbase = NULL;
        info->dli_sname = NULL;
        info->dli_saddr = NULL;
    }
    if (addr == NULL || info == NULL)
        return 0;

    for (unsigned int i = 0; i < panthera_dlopen_bundle_count; i++) {
        const struct panthera_dlopen_bundle *bundle = &panthera_dlopen_bundles[i];
        if (!panthera_image_contains_address(bundle->header, bundle->slide, addr))
            continue;

        panthera_find_nearest_symbol_in_macho(bundle->header, bundle->slide,
                                              addr, &name, &symbol_addr);
        info->dli_fname = bundle->path;
        info->dli_fbase = (void *)bundle->header;
        info->dli_sname = name;
        info->dli_saddr = symbol_addr;
        return 1;
    }

    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct panthera_dyld_bridge_image *img = &panthera_dyld_bridge_state.images[i];
        const struct mach_header_64 *mh = (const struct mach_header_64 *)img->header;
        if (!panthera_image_contains_address(mh, img->slide, addr))
            continue;

        panthera_find_nearest_symbol_in_macho(mh, img->slide, addr,
                                              &name, &symbol_addr);
        info->dli_fname = img->path;
        info->dli_fbase = (void *)img->header;
        info->dli_sname = name;
        info->dli_saddr = symbol_addr;
        return 1;
    }

    return 0;
}

void *
dlsym(void *handle, const char *symbol)
{
    const struct panthera_dlopen_bundle *bundle;
    const struct panthera_dyld_bridge_image *bridged;

    if (symbol == NULL)
        return NULL;
    if (handle == NULL || handle == RTLD_DEFAULT ||
        handle == (void *)&_panthera_dlopen_sentinel)
        return panthera_dlsym_default(symbol);

    bundle = panthera_find_dlopen_bundle_by_handle(handle);
    if (bundle != NULL)
        return panthera_find_symbol_in_macho(bundle->header, bundle->slide, symbol);

    bridged = panthera_find_bridge_image_by_handle(handle);
    if (bridged != NULL)
        return panthera_find_symbol_in_macho((const struct mach_header_64 *)bridged->header,
                                             bridged->slide,
                                             symbol);

    return panthera_find_symbol_in_macho((const struct mach_header_64 *)handle, 0, symbol);
}

int
dlclose(void *handle)
{
    struct panthera_dlopen_bundle *bundle;

    panthera_dlopen_set_error(NULL);
    if (handle == NULL || handle == RTLD_DEFAULT ||
        handle == (void *)&_panthera_dlopen_sentinel)
        return 0;

    bundle = panthera_find_mutable_dlopen_bundle_by_handle(handle);
    if (bundle != NULL) {
        if (bundle->ref_count > 1) {
            bundle->ref_count--;
            return 0;
        }

        unsigned int index = (unsigned int)(bundle - panthera_dlopen_bundles);
        const struct mach_header_64 *header = bundle->header;
        size_t mapping_size = bundle->mapping_size;

        panthera_run_mod_func_section(header, bundle->slide, "__mod_term_func");
        panthera_forget_bridge_image_by_header(header);
        (void)munmap((void *)header, mapping_size);

        for (unsigned int i = index + 1; i < panthera_dlopen_bundle_count; i++)
            panthera_dlopen_bundles[i - 1] = panthera_dlopen_bundles[i];
        panthera_dlopen_bundle_count--;
        panthera_refresh_dlopen_bridge_paths();
        return 0;
    }

    if (panthera_find_bridge_image_by_handle(handle) != NULL)
        return 0;

    panthera_dlopen_set_error("invalid dlclose handle");
    return -1;
}

/* Minimal TLS key support */
static int _panthera_tls_next = 0;
int panthera_pthread_key_create(unsigned long *key, void (*destructor)(void *))
    __asm__("__pthread_key_create");
int panthera_pthread_key_create(unsigned long *key, void (*destructor)(void *)) {
    (void)destructor;
    if (_panthera_tls_next >= 64)
        return 11;
    if (key)
        *key = (unsigned long)_panthera_tls_next++;
    return 0;
}

/* Mach time helpers — forward to real mach_absolute_time in libsystem_kernel */
extern uint64_t mach_absolute_time(void);
uint64_t mach_approximate_time(void) { return mach_absolute_time(); }
uint64_t mach_continuous_time(void) { return mach_absolute_time(); }
uint64_t mach_continuous_approximate_time(void) { return mach_absolute_time(); }

int regcomp(void *preg, const char *pattern, int cflags) {
    (void)preg;
    (void)pattern;
    (void)cflags;
    return 1;
}

int regexec(const void *preg, const char *str, unsigned long nmatch, void *pmatch, int eflags) {
    (void)preg;
    (void)str;
    (void)nmatch;
    (void)pmatch;
    (void)eflags;
    return 1;
}

void regfree(void *preg) {
    (void)preg;
}

struct panthera_acl_entry {
    uint32_t ae_magic;
    uint32_t ae_tag;
    unsigned char ae_applicable[16];
    uint32_t ae_flags;
    uint32_t ae_perms;
};

struct panthera_acl {
    uint32_t a_magic;
    unsigned int a_entries;
    int a_last_get;
    uint32_t a_flags;
    struct panthera_acl_entry a_ace[128];
};

struct panthera_acl_flagset {
    uint32_t af_flags;
};

struct panthera_acl_permset {
    uint32_t ap_perms;
};

extern int __chmod_extended(char *, uid_t, gid_t, int, void *);
extern int __fchmod_extended(int, uid_t, gid_t, int, void *);
extern int filesec_get_property(void *, int, void *);
extern int chmod(const char *, unsigned short);
extern int fchmod(int, unsigned short);
extern void *malloc(unsigned long);

static int panthera_acl_valid_entry(const struct panthera_acl_entry *entry) {
    return ((intptr_t)entry > 16 || (intptr_t)entry < -16) &&
        entry->ae_magic == 0xac1ac101U;
}

static int panthera_acl_valid_acl(const struct panthera_acl *acl) {
    return ((intptr_t)acl > 16 || (intptr_t)acl < -16) &&
        acl->a_magic == 0xac1ac102U;
}

int acl_copy_entry(void *dest, void *src) {
    struct panthera_acl_entry *d = dest;
    struct panthera_acl_entry *s = src;

    if (d == NULL || s == NULL || d == s ||
        !panthera_acl_valid_entry(d) || !panthera_acl_valid_entry(s)) {
        errno = 22;
        return -1;
    }

    __builtin_memcpy(d, s, sizeof(*d));
    return 0;
}

int acl_create_entry_np(void *acl_p, void *entry_p, int index) {
    struct panthera_acl **aclpp = acl_p;
    struct panthera_acl_entry **entrypp = entry_p;
    struct panthera_acl *acl;

    if (aclpp == NULL || entrypp == NULL || !panthera_acl_valid_acl(*aclpp)) {
        errno = 22;
        return -1;
    }
    acl = *aclpp;
    if (acl->a_entries >= 128) {
        errno = 12;
        return -1;
    }
    if (index == -1)
        index = (int)acl->a_entries;
    if (index < 0 || (unsigned int)index > acl->a_entries) {
        errno = 34;
        return -1;
    }
    for (unsigned int i = acl->a_entries; i > (unsigned int)index; i--)
        acl->a_ace[i] = acl->a_ace[i - 1];
    acl->a_entries++;
    __builtin_memset(&acl->a_ace[index], 0, sizeof(acl->a_ace[index]));
    acl->a_ace[index].ae_magic = 0xac1ac101U;
    *entrypp = &acl->a_ace[index];
    return 0;
}

int acl_delete_entry(void *acl_arg, void *entry_arg) {
    struct panthera_acl *acl = acl_arg;
    struct panthera_acl_entry *entry = entry_arg;
    unsigned int index;

    if (!panthera_acl_valid_acl(acl) || !panthera_acl_valid_entry(entry) ||
        entry < &acl->a_ace[0] || entry >= &acl->a_ace[128]) {
        errno = 22;
        return -1;
    }
    index = (unsigned int)(entry - &acl->a_ace[0]);
    if (index >= acl->a_entries) {
        errno = 22;
        return -1;
    }
    acl->a_entries--;
    for (unsigned int i = index; i < acl->a_entries; i++)
        acl->a_ace[i] = acl->a_ace[i + 1];
    acl->a_ace[acl->a_entries].ae_magic = 0;
    if (acl->a_last_get >= (int)index)
        acl->a_last_get--;
    return 0;
}

int acl_delete_flag_np(void *flagset_arg, int flag) {
    struct panthera_acl_flagset *flagset = flagset_arg;
    if (flagset == NULL) {
        errno = 22;
        return -1;
    }
    flagset->af_flags &= ~(uint32_t)flag;
    return 0;
}

int acl_delete_perm(void *permset_arg, int perm) {
    struct panthera_acl_permset *permset = permset_arg;
    if (permset == NULL) {
        errno = 22;
        return -1;
    }
    permset->ap_perms &= ~(uint32_t)perm;
    return 0;
}

int acl_set_flagset_np(void *obj, void *flagset_arg) {
    struct panthera_acl *acl = obj;
    struct panthera_acl_entry *entry = obj;
    struct panthera_acl_flagset *flagset = flagset_arg;

    if (flagset == NULL) {
        errno = 22;
        return -1;
    }
    if (panthera_acl_valid_acl(acl)) {
        acl->a_flags = flagset->af_flags;
        return 0;
    }
    if (panthera_acl_valid_entry(entry)) {
        entry->ae_flags = flagset->af_flags;
        return 0;
    }
    errno = 22;
    return -1;
}

int acl_set_permset(void *entry_arg, void *permset_arg) {
    struct panthera_acl_entry *entry = entry_arg;
    struct panthera_acl_permset *permset = permset_arg;

    if (!panthera_acl_valid_entry(entry) || permset == NULL) {
        errno = 22;
        return -1;
    }
    entry->ae_perms = permset->ap_perms;
    return 0;
}

char *acl_to_text(void *acl, long *lenp) {
    const char *empty = "";
    char *out;

    if (acl != NULL && !panthera_acl_valid_acl(acl)) {
        errno = 22;
        return NULL;
    }
    out = malloc(1);
    if (out == NULL)
        return NULL;
    out[0] = '\0';
    if (lenp != NULL)
        *lenp = (long)__builtin_strlen(empty);
    return out;
}

int chmodx_np(const char *path, filesec_t fsec) {
    unsigned short mode;

    if (fsec != NULL && filesec_get_property(fsec, 4, &mode) == 0)
        return chmod(path, mode);
    if (errno != 2 && errno != 0)
        return -1;
    return __chmod_extended((char *)path, ((uid_t)~(uid_t)0) - 100,
        ((gid_t)~(gid_t)0) - 100, -1, NULL);
}

int fchmodx_np(int fd, filesec_t fsec) {
    unsigned short mode;

    if (fsec != NULL && filesec_get_property(fsec, 4, &mode) == 0)
        return fchmod(fd, mode);
    if (errno != 2 && errno != 0)
        return -1;
    return __fchmod_extended(fd, ((uid_t)~(uid_t)0) - 100,
        ((gid_t)~(gid_t)0) - 100, -1, NULL);
}

int mbr_identifier_to_uuid(int id_type, const void *identifier,
    unsigned long identifier_size, unsigned char uu[16]) {
    (void)identifier_size;
    if (uu == NULL || identifier == NULL) {
        return 22;
    }
    __builtin_memset(uu, 0, 16);
    switch (id_type) {
    case 4:
        if (__builtin_strcmp((const char *)identifier, "root") == 0)
            uu[15] = 1;
        else
            uu[15] = 0xff;
        break;
    case 5:
        if (__builtin_strcmp((const char *)identifier, "wheel") == 0)
            uu[15] = 1;
        else
            uu[15] = 0xfe;
        break;
    default:
        return 5;
    }
    return 0;
}

long acl_copy_ext_native(void *buf, void *acl, long size) {
    (void)buf;
    (void)acl;
    (void)size;
    return -1;
}

void *acl_copy_int_native(const void *buf) {
    (void)buf;
    return NULL;
}

long acl_size(void *acl) {
    (void)acl;
    return -1;
}

/* os_alloc_once token wrapper around the Apple-compatible slot-based entry point. */
struct _os_alloc_once_s {
    long once;
    void *ptr;
};
extern struct _os_alloc_once_s _os_alloc_once_table[];
extern void *_os_alloc_once(struct _os_alloc_once_s *, unsigned long, void (*)(void *));
void *os_alloc_once(unsigned int slot, unsigned long sz, void (*init)(void *)) {
    return _os_alloc_once(&_os_alloc_once_table[slot], sz, init);
}

void *dbopen(const char *f, int fl, int m, int t, const void *i) {
    (void)f;
    (void)fl;
    (void)m;
    (void)t;
    (void)i;
    return NULL;
}

int vm_region_64(void) { return 1; }

int setpriority(int w, unsigned int who, int prio) {
    (void)w;
    (void)who;
    (void)prio;
    return -1;
}

int settimeofday(const void *tv, const void *tz) {
    (void)tv;
    (void)tz;
    return -1;
}

void *ether_aton(const char *s) {
    (void)s;
    return NULL;
}

int getnameinfo_link(void) { return -1; }

void *panthera_gai_simple(void) __asm__("__gai_simple");
void *panthera_gai_simple(void) { return NULL; }

void *si_addrinfo(void) { return NULL; }
void *si_ipnode_byname(void) { return NULL; }
void *si_list_to_addrinfo(void) { return NULL; }
void *si_nameinfo(void) { return NULL; }

uint64_t panthera_get_cpu_cap(void) __asm__("__get_cpu_capabilities");
uint64_t panthera_get_cpu_cap(void) { return 0; }

void panthera_init_cpu_cap(void) __asm__("__init_cpu_capabilities");
void panthera_init_cpu_cap(void) {}

extern int open(const char *, int, ...);
extern int close(int);

int panthera_open_nocancel(const char *p, int f, int m) __asm__("_open$NOCANCEL$UNIX2003");
int panthera_open_nocancel(const char *p, int f, int m) { return open(p, f, m); }

int panthera_close_nocancel(int fd) __asm__("_close$NOCANCEL$UNIX2003");
int panthera_close_nocancel(int fd) { return close(fd); }

extern long recvfrom(int, void *, unsigned long, int, void *, unsigned int *);
extern long sendto(int, const void *, unsigned long, int, const void *, unsigned int);

long recv(int s, void *b, unsigned long l, int f) {
    return recvfrom(s, b, l, f, (void *)0, (unsigned int *)0);
}

long send(int s, const void *b, unsigned long l, int f) {
    return sendto(s, b, l, f, (const void *)0, 0);
}

long panthera_recv_unix2003(int s, void *b, unsigned long l, int f)
    __asm__("_recv$UNIX2003");
long panthera_recv_unix2003(int s, void *b, unsigned long l, int f) {
    return recvfrom(s, b, l, f, (void *)0, (unsigned int *)0);
}

/* connect: wrapper around connect$NOCANCEL from libsystem_kernel */
extern int panthera_connect_nocancel(int, const void *, unsigned int)
    __asm__("_connect$NOCANCEL");
int connect(int s, const void *a, unsigned int l) {
    return panthera_connect_nocancel(s, a, l);
}

/* send$UNIX2003: provided by aliases */

int panthera_socketpair(int d, int t, int p, int sv[2])
    __asm__("_socketpair$UNIX2003");
int panthera_socketpair(int d, int t, int p, int sv[2]) {
    (void)d;
    (void)t;
    (void)p;
    (void)sv;
    return -1;
}

int panthera_getgroups_extsn(int size, int list[])
    __asm__("_getgroups$DARWIN_EXTSN");
int panthera_getgroups_extsn(int size, int list[]) {
    (void)size;
    (void)list;
    return 0;
}

int open_dprotected_np(const char *p, int f, int c, int dp, int m) {
    (void)p;
    (void)f;
    (void)c;
    (void)dp;
    (void)m;
    return -1;
}

char *panthera_dtoa(double d, int m, int n, int *dp, int *s, char **re)
    __asm__("___dtoa");
char *panthera_dtoa(double d, int m, int n, int *dp, int *s, char **re) {
    (void)d;
    (void)m;
    (void)n;
    if (dp)
        *dp = 0;
    if (s)
        *s = 0;
    if (re)
        *re = NULL;
    return "0";
}

void panthera_freedtoa(char *s) __asm__("___freedtoa");
void panthera_freedtoa(char *s) { (void)s; }

char *panthera_hdtoa(void) __asm__("___hdtoa");
char *panthera_hdtoa(void) { return "0"; }

char *panthera_hldtoa(void) __asm__("___hldtoa");
char *panthera_hldtoa(void) { return "0"; }

char *panthera_ldtoa(void) __asm__("___ldtoa");
char *panthera_ldtoa(void) { return "0"; }

void panthera_dyld_get_image_uuid(void) __asm__("__dyld_get_image_uuid");
void panthera_dyld_get_image_uuid(void) {}

void panthera_dyld_images_for_addresses(void) __asm__("__dyld_images_for_addresses");
void panthera_dyld_images_for_addresses(void) {}

void panthera_ctx_done(void) __asm__("__ctx_done");
void panthera_ctx_done(void) {}

void panthera_sigunaltstack(void) __asm__("__sigunaltstack");
void panthera_sigunaltstack(void) {}

/* panthera_patch.sh stub: _clock */
int clock(void) {
    return (unsigned long)-1;
}


/* panthera_patch.sh legacy stub: __Unwind_DeleteException */
int panthera_legacy_Unwind_DeleteException(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_GetIPInfo */
int panthera_legacy_Unwind_GetIPInfo(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_GetLanguageSpecificData */
int panthera_legacy_Unwind_GetLanguageSpecificData(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_GetRegionStart */
int panthera_legacy_Unwind_GetRegionStart(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_RaiseException */
int panthera_legacy_Unwind_RaiseException(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_Resume */
int panthera_legacy_Unwind_Resume(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_Resume_or_Rethrow */
int panthera_legacy_Unwind_Resume_or_Rethrow(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_SetGR */
int panthera_legacy_Unwind_SetGR(void) {
    return 0;
}

/* panthera_patch.sh legacy stub: __Unwind_SetIP */
int panthera_legacy_Unwind_SetIP(void) {
    return 0;
}

/* panthera_patch.sh stub: __ZNSt9bad_allocD1Ev */
int _ZNSt9bad_allocD1Ev(void) {
    return 0;
}

/* panthera_patch.sh stub: __ZTISt9bad_alloc */
int _ZTISt9bad_alloc(void) {
    return 0;
}

/* panthera_patch.sh stub: __ZTVSt9bad_alloc */
int _ZTVSt9bad_alloc(void) {
    return 0;
}

/* panthera_patch.sh stub: _catclose */
int catclose(void) {
    return 0;
}

/* panthera_patch.sh stub: _catgets */
int catgets(void) {
    return 0;
}

/* panthera_patch.sh stub: _catopen */
int catopen(void) {
    return 0;
}

#define PANTHERA_COPYFILE_STATE_SRC_FD 1
#define PANTHERA_COPYFILE_STATE_SRC_FILENAME 2
#define PANTHERA_COPYFILE_STATE_DST_FD 3
#define PANTHERA_COPYFILE_STATE_DST_FILENAME 4
#define PANTHERA_COPYFILE_STATE_STATUS_CB 6
#define PANTHERA_COPYFILE_STATE_STATUS_CTX 7
#define PANTHERA_COPYFILE_STATE_COPIED 8
#define PANTHERA_COPYFILE_DATA 0x00000008
#define PANTHERA_COPYFILE_COPY_DATA 4
#define PANTHERA_COPYFILE_PROGRESS 4
#define PANTHERA_COPYFILE_CONTINUE 0

typedef struct panthera_copyfile_state *copyfile_state_t;
typedef int (*panthera_copyfile_callback_t)(int, int, copyfile_state_t,
    const char *, const char *, void *);

struct panthera_copyfile_state {
    int src_fd;
    int dst_fd;
    const char *src_name;
    const char *dst_name;
    void *status_ctx;
    panthera_copyfile_callback_t status_cb;
    uint64_t copied;
};

copyfile_state_t copyfile_state_alloc(void) __asm__("_copyfile_state_alloc");
int copyfile_state_free(copyfile_state_t state) __asm__("_copyfile_state_free");
int copyfile_state_get(copyfile_state_t state, uint32_t flag, void *dst)
    __asm__("_copyfile_state_get");
int copyfile_state_set(copyfile_state_t state, uint32_t flag, const void *src)
    __asm__("_copyfile_state_set");
int fcopyfile(int from_fd, int to_fd, copyfile_state_t state,
    uint32_t flags) __asm__("_fcopyfile");
int copyfile(const char *from, const char *to, copyfile_state_t state,
    uint32_t flags) __asm__("_copyfile");

copyfile_state_t
copyfile_state_alloc(void)
{
    copyfile_state_t state = malloc(sizeof(*state));
    if (state != NULL) {
        unsigned char *bytes = (unsigned char *)state;
        size_t i;
        for (i = 0; i < sizeof(*state); i++)
            bytes[i] = 0;
        state->src_fd = -1;
        state->dst_fd = -1;
    }
    return state;
}

int
copyfile_state_free(copyfile_state_t state)
{
    free(state);
    return 0;
}

int
copyfile_state_get(copyfile_state_t state, uint32_t flag, void *dst)
{
    if (state == NULL || dst == NULL) {
        errno = EINVAL;
        return -1;
    }

    switch (flag) {
    case PANTHERA_COPYFILE_STATE_SRC_FD:
        *(int *)dst = state->src_fd;
        return 0;
    case PANTHERA_COPYFILE_STATE_DST_FD:
        *(int *)dst = state->dst_fd;
        return 0;
    case PANTHERA_COPYFILE_STATE_SRC_FILENAME:
        *(const char **)dst = state->src_name;
        return 0;
    case PANTHERA_COPYFILE_STATE_DST_FILENAME:
        *(const char **)dst = state->dst_name;
        return 0;
    case PANTHERA_COPYFILE_STATE_STATUS_CB:
        *(panthera_copyfile_callback_t *)dst = state->status_cb;
        return 0;
    case PANTHERA_COPYFILE_STATE_STATUS_CTX:
        *(void **)dst = state->status_ctx;
        return 0;
    case PANTHERA_COPYFILE_STATE_COPIED:
        *(uint64_t *)dst = state->copied;
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int
copyfile_state_set(copyfile_state_t state, uint32_t flag, const void *src)
{
    if (state == NULL) {
        errno = EINVAL;
        return -1;
    }

    switch (flag) {
    case PANTHERA_COPYFILE_STATE_SRC_FD:
        state->src_fd = src ? *(const int *)src : -1;
        return 0;
    case PANTHERA_COPYFILE_STATE_DST_FD:
        state->dst_fd = src ? *(const int *)src : -1;
        return 0;
    case PANTHERA_COPYFILE_STATE_SRC_FILENAME:
        state->src_name = (const char *)src;
        return 0;
    case PANTHERA_COPYFILE_STATE_DST_FILENAME:
        state->dst_name = (const char *)src;
        return 0;
    case PANTHERA_COPYFILE_STATE_STATUS_CB:
        state->status_cb = (panthera_copyfile_callback_t)src;
        return 0;
    case PANTHERA_COPYFILE_STATE_STATUS_CTX:
        state->status_ctx = (void *)src;
        return 0;
    case PANTHERA_COPYFILE_STATE_COPIED:
        state->copied = src ? *(const uint64_t *)src : 0;
        return 0;
    default:
        errno = EINVAL;
        return -1;
    }
}

int
fcopyfile(int from_fd, int to_fd, copyfile_state_t state, uint32_t flags)
{
    char buf[65536];
    ssize_t nread;

    if ((flags & PANTHERA_COPYFILE_DATA) == 0)
        return 0;

    if (state != NULL) {
        state->src_fd = from_fd;
        state->dst_fd = to_fd;
    }

    while ((nread = read(from_fd, buf, sizeof(buf))) > 0) {
        char *cursor = buf;
        ssize_t remaining = nread;

        while (remaining > 0) {
            ssize_t nwritten = write(to_fd, cursor, (size_t)remaining);
            if (nwritten < 0)
                return -1;
            cursor += nwritten;
            remaining -= nwritten;
        }

        if (state != NULL) {
            state->copied += (uint64_t)nread;
            if (state->status_cb != NULL &&
                state->status_cb(PANTHERA_COPYFILE_COPY_DATA,
                PANTHERA_COPYFILE_PROGRESS, state, state->src_name,
                state->dst_name, state->status_ctx) !=
                PANTHERA_COPYFILE_CONTINUE) {
                errno = ECANCELED;
                return -1;
            }
        }
    }

    return (nread < 0) ? -1 : 0;
}

int
copyfile(const char *from, const char *to, copyfile_state_t state,
    uint32_t flags)
{
    int from_fd;
    int to_fd;
    int result;

    if (from == NULL || to == NULL) {
        errno = EINVAL;
        return -1;
    }

    from_fd = open(from, O_RDONLY);
    if (from_fd < 0)
        return -1;

    to_fd = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (to_fd < 0) {
        int saved_errno = errno;
        close(from_fd);
        errno = saved_errno;
        return -1;
    }

    if (state != NULL) {
        state->src_name = from;
        state->dst_name = to;
    }
    result = fcopyfile(from_fd, to_fd, state, flags | PANTHERA_COPYFILE_DATA);

    {
        int saved_errno = errno;
        close(from_fd);
        close(to_fd);
        errno = saved_errno;
    }
    return result;
}

/* panthera_patch.sh stub: _statvfs */
int statvfs(void) {
    return 0;
}

/* panthera_patch.sh stub: _getsectiondata */
int getsectiondata(void) {
    return 0;
}
