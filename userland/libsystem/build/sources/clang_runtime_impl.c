/*
 * Runtime entry points needed by the in-guest Panthera clang build.
 * These are ordinary libSystem/libm-style functions, kept in libpanthera_extra
 * until the full Darwin libm/libsyscall surface is consolidated.
 */

typedef unsigned int uint32_t;
typedef unsigned long size_t;

#ifndef AT_FDCWD
#define AT_FDCWD -2
#endif

extern double exp(double);
extern double log(double);
extern int clonefileat(int, const char *, int, const char *, uint32_t);
extern void *calloc(size_t, size_t);
extern void *memcpy(void *, const void *, size_t);
extern int pthread_key_create(unsigned long *, void (*)(void *));
extern void *pthread_getspecific(unsigned long);
extern int pthread_setspecific(unsigned long, const void *);

int
clonefile(const char *src, const char *dst, uint32_t flags)
{
    return clonefileat(AT_FDCWD, src, AT_FDCWD, dst, flags);
}

double
sqrt(double x)
{
    double result;
    __asm__ volatile("sqrtsd %1, %0" : "=x"(result) : "x"(x));
    return result;
}

double
exp2(double x)
{
    return exp(x * 0.69314718055994530942);
}

double
log2(double x)
{
    return log(x) * 1.44269504088896340736;
}

float
log2f(float x)
{
    return (float)log2((double)x);
}

double
ldexp(double x, int exp)
{
    union {
        unsigned long long u;
        double d;
    } scale;

    if (x == 0.0)
        return x;

    while (exp > 1023) {
        x *= 8.98846567431157953865e307;
        exp -= 1023;
    }
    while (exp < -1022) {
        x *= 2.22507385850720138309e-308;
        exp += 1022;
    }

    scale.u = (unsigned long long)(exp + 1023) << 52;
    return x * scale.d;
}

int
feclearexcept(int excepts)
{
    unsigned int mxcsr;

    if ((excepts & 0x3f) != 0)
        __asm__ volatile("fnclex");

    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    mxcsr &= ~((unsigned int)excepts & 0x3f);
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
    return 0;
}

int
fetestexcept(int excepts)
{
    unsigned int mxcsr;
    unsigned short status;

    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    __asm__ volatile("fnstsw %0" : "=m"(status));
    return (int)((mxcsr | status) & ((unsigned int)excepts & 0x3f));
}

#define LC_SEGMENT_64 0x19
#define S_THREAD_LOCAL_REGULAR 0x11
#define S_THREAD_LOCAL_ZEROFILL 0x12
#define S_THREAD_LOCAL_VARIABLES 0x13
#define PANTHERA_DYLD_BRIDGE_MAX_IMAGES 64
#define PANTHERA_TLV_MAX_IMAGES 64

struct mach_header_64 {
    uint32_t magic;
    int cputype;
    int cpusubtype;
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

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int maxprot;
    int initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct section_64 {
    char sectname[16];
    char segname[16];
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

struct panthera_dyld_bridge_image {
    const struct mach_header_64 *header;
    long slide;
    const char *path;
    unsigned char has_objc;
};

struct panthera_dyld_bridge_state {
    unsigned int image_count;
    unsigned long shared_cache_base;
    unsigned long shared_cache_size;
    const struct mach_header_64 *prog_image_header;
    struct panthera_dyld_bridge_image images[PANTHERA_DYLD_BRIDGE_MAX_IMAGES];
};

struct panthera_tlv_descriptor {
    void *(*thunk)(struct panthera_tlv_descriptor *);
    unsigned long key_plus_one;
    unsigned long offset;
};

struct panthera_tlv_image {
    const struct mach_header_64 *header;
    unsigned long key_plus_one;
    uint64_t template_base;
    uint64_t regular_addr;
    size_t regular_size;
    size_t total_size;
};

extern struct panthera_dyld_bridge_state panthera_dyld_bridge_state;

static struct panthera_tlv_image panthera_tlv_images[PANTHERA_TLV_MAX_IMAGES];
static unsigned int panthera_tlv_image_count;

struct dyld_unwind_sections {
    const struct mach_header_64 *mh;
    const void *dwarf_section;
    unsigned long dwarf_section_length;
    const void *compact_unwind_section;
    unsigned long compact_unwind_section_length;
};

static int
panthera_streq16(const char *a, const char *b)
{
    for (int i = 0; i < 16; i++) {
        if (b[i] == 0)
            return a[i] == 0;
        if (a[i] != b[i])
            return 0;
    }
    return b[16] == 0;
}

static uint64_t
panthera_text_vmaddr(const struct mach_header_64 *mh)
{
    const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg =
                (const struct segment_command_64 *)cmd;
            if (panthera_streq16(seg->segname, "__TEXT"))
                return seg->vmaddr;
        }
        cmd += lc->cmdsize;
    }
    return 0;
}

static void *
panthera_section_runtime(const struct mach_header_64 *mh,
                         const struct section_64 *sect,
                         uint64_t text_vmaddr)
{
    return (void *)((unsigned long)mh + (unsigned long)(sect->addr - text_vmaddr));
}

static const struct segment_command_64 *
panthera_find_segment(const struct mach_header_64 *mh, const char *segname)
{
    const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg =
                (const struct segment_command_64 *)cmd;
            if (panthera_streq16(seg->segname, segname))
                return seg;
        }
        cmd += lc->cmdsize;
    }
    return 0;
}

static int
panthera_find_section(const struct mach_header_64 *mh,
                      const char *segname,
                      const char *sectname,
                      const struct section_64 **out)
{
    const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg =
                (const struct segment_command_64 *)cmd;
            const struct section_64 *sect =
                (const struct section_64 *)(cmd + sizeof(*seg));
            if (panthera_streq16(seg->segname, segname)) {
                for (uint32_t s = 0; s < seg->nsects; s++) {
                    if (panthera_streq16(sect[s].sectname, sectname)) {
                        *out = &sect[s];
                        return 1;
                    }
                }
            }
        }
        cmd += lc->cmdsize;
    }
    return 0;
}

int
_dyld_find_unwind_sections(void *addr, struct dyld_unwind_sections *info)
{
    if (!addr || !info)
        return 0;

    unsigned long target = (unsigned long)addr;
    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct mach_header_64 *mh = panthera_dyld_bridge_state.images[i].header;
        if (!mh)
            continue;

        const struct segment_command_64 *text = panthera_find_segment(mh, "__TEXT");
        if (!text)
            continue;

        unsigned long text_start = (unsigned long)mh;
        unsigned long text_end = text_start + (unsigned long)text->vmsize;
        if (target < text_start || target >= text_end)
            continue;

        const struct section_64 *sect = 0;
        unsigned long text_vmaddr = text->vmaddr;
        info->mh = mh;
        info->dwarf_section = 0;
        info->dwarf_section_length = 0;
        info->compact_unwind_section = 0;
        info->compact_unwind_section_length = 0;

        if (panthera_find_section(mh, "__TEXT", "__eh_frame", &sect)) {
            info->dwarf_section = panthera_section_runtime(mh, sect, text_vmaddr);
            info->dwarf_section_length = (unsigned long)sect->size;
        }
        if (panthera_find_section(mh, "__TEXT", "__unwind_info", &sect)) {
            info->compact_unwind_section = panthera_section_runtime(mh, sect, text_vmaddr);
            info->compact_unwind_section_length = (unsigned long)sect->size;
        }

        return info->dwarf_section || info->compact_unwind_section;
    }

    return 0;
}

void
_dyld_register_func_for_remove_image(void (*func)(const struct mach_header_64 *, long))
{
    /*
     * Panthera does not unload Mach-O images yet.  The Darwin contract is to
     * invoke this callback before a mapped image is removed, so registration is
     * accepted and kept inert until dlclose/image removal exists.
     */
    (void)func;
}

static struct panthera_tlv_image *
panthera_find_tlv_image(struct panthera_tlv_descriptor *desc)
{
    unsigned long desc_addr = (unsigned long)desc;

    for (unsigned int i = 0; i < panthera_tlv_image_count; i++) {
        struct panthera_tlv_image *cached = &panthera_tlv_images[i];
        const struct mach_header_64 *mh = cached->header;
        uint64_t text_vmaddr = panthera_text_vmaddr(mh);
        const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);

        for (uint32_t j = 0; j < mh->ncmds; j++) {
            const struct load_command *lc = (const struct load_command *)cmd;
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg =
                    (const struct segment_command_64 *)cmd;
                const struct section_64 *sect =
                    (const struct section_64 *)(cmd + sizeof(*seg));
                for (uint32_t s = 0; s < seg->nsects; s++) {
                    if ((sect[s].flags & 0xff) == S_THREAD_LOCAL_VARIABLES) {
                        unsigned long start = (unsigned long)panthera_section_runtime(mh, &sect[s], text_vmaddr);
                        unsigned long end = start + (unsigned long)sect[s].size;
                        if (desc_addr >= start && desc_addr < end)
                            return cached;
                    }
                }
            }
            cmd += lc->cmdsize;
        }
    }

    for (unsigned int i = 0; i < panthera_dyld_bridge_state.image_count; i++) {
        const struct mach_header_64 *mh = panthera_dyld_bridge_state.images[i].header;
        if (!mh)
            continue;
        uint64_t text_vmaddr = panthera_text_vmaddr(mh);
        if (!text_vmaddr)
            continue;

        uint64_t template_base = ~(uint64_t)0;
        uint64_t template_end = 0;
        uint64_t regular_addr = 0;
        size_t regular_size = 0;
        void *vars_start = 0;
        size_t vars_size = 0;

        const unsigned char *cmd = (const unsigned char *)mh + sizeof(*mh);
        for (uint32_t j = 0; j < mh->ncmds; j++) {
            const struct load_command *lc = (const struct load_command *)cmd;
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg =
                    (const struct segment_command_64 *)cmd;
                const struct section_64 *sect =
                    (const struct section_64 *)(cmd + sizeof(*seg));
                for (uint32_t s = 0; s < seg->nsects; s++) {
                    uint32_t type = sect[s].flags & 0xff;
                    if (type == S_THREAD_LOCAL_REGULAR || type == S_THREAD_LOCAL_ZEROFILL) {
                        if (sect[s].addr < template_base)
                            template_base = sect[s].addr;
                        if (sect[s].addr + sect[s].size > template_end)
                            template_end = sect[s].addr + sect[s].size;
                        if (type == S_THREAD_LOCAL_REGULAR) {
                            regular_addr = sect[s].addr;
                            regular_size = (size_t)sect[s].size;
                        }
                    } else if (type == S_THREAD_LOCAL_VARIABLES) {
                        vars_start = panthera_section_runtime(mh, &sect[s], text_vmaddr);
                        vars_size = (size_t)sect[s].size;
                    }
                }
            }
            cmd += lc->cmdsize;
        }

        if (!vars_start || desc_addr < (unsigned long)vars_start ||
            desc_addr >= (unsigned long)vars_start + vars_size) {
            continue;
        }

        if (template_base == ~(uint64_t)0) {
            template_base = 0;
            template_end = desc->offset + sizeof(void *);
        }

        unsigned long key = 0;
        if (pthread_key_create(&key, 0) != 0)
            return 0;

        struct panthera_tlv_image *out = &panthera_tlv_images[panthera_tlv_image_count++];
        out->header = mh;
        out->key_plus_one = key + 1;
        out->template_base = template_base;
        out->regular_addr = regular_addr;
        out->regular_size = regular_size;
        out->total_size = (size_t)(template_end - template_base);
        if (out->total_size == 0)
            out->total_size = desc->offset + sizeof(void *);

        struct panthera_tlv_descriptor *descs = (struct panthera_tlv_descriptor *)vars_start;
        size_t ndescs = vars_size / sizeof(*descs);
        for (size_t d = 0; d < ndescs; d++)
            descs[d].key_plus_one = out->key_plus_one;

        return out;
    }

    return 0;
}

void *
_tlv_bootstrap(struct panthera_tlv_descriptor *desc)
{
    struct panthera_tlv_image *image = panthera_find_tlv_image(desc);
    unsigned long key_plus_one = image ? image->key_plus_one : desc->key_plus_one;

    if (key_plus_one == 0) {
        unsigned long key = 0;
        if (pthread_key_create(&key, 0) != 0)
            return 0;
        desc->key_plus_one = key + 1;
        key_plus_one = desc->key_plus_one;
    }

    unsigned long key = key_plus_one - 1;
    unsigned char *block = (unsigned char *)pthread_getspecific(key);
    if (!block) {
        size_t total = image ? image->total_size : desc->offset + sizeof(void *);
        block = (unsigned char *)calloc(1, total ? total : sizeof(void *));
        if (!block)
            return 0;
        if (image && image->regular_addr && image->regular_size) {
            uint64_t text_vmaddr = panthera_text_vmaddr(image->header);
            void *src = (void *)((unsigned long)image->header +
                (unsigned long)(image->regular_addr - text_vmaddr));
            memcpy(block + (image->regular_addr - image->template_base),
                   src, image->regular_size);
        }
        pthread_setspecific(key, block);
    }

    return block + desc->offset;
}

uint32_t
_dyld_image_count(void)
{
    return panthera_dyld_bridge_state.image_count;
}

const char *
_dyld_get_image_name(uint32_t index)
{
    if (index >= panthera_dyld_bridge_state.image_count)
        return 0;
    return panthera_dyld_bridge_state.images[index].path;
}

long
_dyld_get_image_vmaddr_slide(uint32_t index)
{
    if (index >= panthera_dyld_bridge_state.image_count)
        return 0;
    return panthera_dyld_bridge_state.images[index].slide;
}

int
_NSGetExecutablePath(char *buf, uint32_t *bufsize)
{
    const char *path = "/unknown";
    uint32_t need = 1;

    if (panthera_dyld_bridge_state.image_count > 0 &&
        panthera_dyld_bridge_state.images[0].path) {
        path = panthera_dyld_bridge_state.images[0].path;
    }

    while (path[need - 1] != 0)
        need++;

    if (!bufsize)
        return -1;
    if (!buf || *bufsize < need) {
        *bufsize = need;
        return -1;
    }

    for (uint32_t i = 0; i < need; i++)
        buf[i] = path[i];
    return 0;
}

int
gethostuuid(unsigned char uuid[16], const void *timeout)
{
    static const unsigned char panthera_host_uuid[16] = {
        0x50, 0x41, 0x4e, 0x54, 0x48, 0x45, 0x52, 0x41,
        0x80, 0x00, 0x44, 0x41, 0x52, 0x57, 0x49, 0x4e
    };

    (void)timeout;
    if (!uuid)
        return -1;
    memcpy(uuid, panthera_host_uuid, sizeof(panthera_host_uuid));
    return 0;
}
