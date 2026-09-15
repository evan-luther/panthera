/*
 * build_shared_cache_real.c — Panthera real shared cache builder
 *
 * Builds a dyld shared cache with Apple-format split regions:
 *   TEXT     at 0x7FFF20000000
 *   DATA     at 0x7FFF80000000
 *   LINKEDIT at 0x7FFFC0000000
 *
 * Features:
 *   - Split TEXT/DATA/LINKEDIT regions per Apple dyld_cache_format.h
 *   - Cross-segment RIP-relative reference patching (stubs, GOT loads)
 *   - Pre-resolved rebases and binds (build-time fixup resolution)
 *   - Slide info v2 for runtime ASLR
 *   - Proper Apple cache headers parseable by xcrun dyld_shared_cache_util
 *
 * Host-side tool (macOS) — reads Panthera x86_64 dylibs, outputs cache file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <uuid/uuid.h>

/* ── Constants ──────────────────────────────────────────────────── */

#define PAGE_SZ    4096
#define PAGE_ALIGN(x) (((x) + PAGE_SZ - 1) & ~(uint64_t)(PAGE_SZ - 1))
#define MAX_DYLIBS 32
#define MAX_SEGS   8

/* Apple single-cache x86_64 mapping layout (src/dyld-1122.1.2/doc/CacheLayout.md) */
#define SHARED_REGION_BASE  0x00007FFF20000000ULL
#define TEXT_REGION_BASE    0x00007FFF20000000ULL
#define DATA_REGION_BASE    0x00007FFF80000000ULL
#define LINK_REGION_BASE    0x00007FFFC0000000ULL

/* ── Cache format structures (matching Apple's dyld_cache_format.h) ── */

struct dyld_cache_header {
    char        magic[16];
    uint32_t    mappingOffset;
    uint32_t    mappingCount;
    uint32_t    imagesOffsetOld;
    uint32_t    imagesCountOld;
    uint64_t    dyldBaseAddress;
    uint64_t    codeSignatureOffset;
    uint64_t    codeSignatureSize;
    uint64_t    slideInfoOffsetUnused;
    uint64_t    slideInfoSizeUnused;
    uint64_t    localSymbolsOffset;
    uint64_t    localSymbolsSize;
    uint8_t     uuid[16];
    uint64_t    cacheType;
    uint32_t    branchPoolsOffset;
    uint32_t    branchPoolsCount;
    uint64_t    dyldInCacheMH;
    uint64_t    dyldInCacheEntry;
    uint64_t    imagesTextOffset;
    uint64_t    imagesTextCount;
    uint64_t    patchInfoAddr;
    uint64_t    patchInfoSize;
    uint64_t    otherImageGroupAddrUnused;
    uint64_t    otherImageGroupSizeUnused;
    uint64_t    progClosuresAddr;
    uint64_t    progClosuresSize;
    uint64_t    progClosuresTrieAddr;
    uint64_t    progClosuresTrieSize;
    uint32_t    platform;
    uint32_t    formatBits;
    uint64_t    sharedRegionStart;
    uint64_t    sharedRegionSize;
    uint64_t    maxSlide;
    uint64_t    dylibsImageArrayAddr;
    uint64_t    dylibsImageArraySize;
    uint64_t    dylibsTrieAddr;
    uint64_t    dylibsTrieSize;
    uint64_t    otherImageArrayAddr;
    uint64_t    otherImageArraySize;
    uint64_t    otherTrieAddr;
    uint64_t    otherTrieSize;
    uint32_t    mappingWithSlideOffset;
    uint32_t    mappingWithSlideCount;
    uint64_t    dylibsPBLStateArrayAddrUnused;
    uint64_t    dylibsPBLSetAddr;
    uint64_t    programsPBLSetPoolAddr;
    uint64_t    programsPBLSetPoolSize;
    uint64_t    programTrieAddr;
    uint32_t    programTrieSize;
    uint32_t    osVersion;
    uint32_t    altPlatform;
    uint32_t    altOsVersion;
    uint64_t    swiftOptsOffset;
    uint64_t    swiftOptsSize;
    uint32_t    subCacheArrayOffset;
    uint32_t    subCacheArrayCount;
    uint8_t     symbolFileUUID[16];
    uint64_t    rosettaReadOnlyAddr;
    uint64_t    rosettaReadOnlySize;
    uint64_t    rosettaReadWriteAddr;
    uint64_t    rosettaReadWriteSize;
    uint32_t    imagesOffset;
    uint32_t    imagesCount;
    uint32_t    cacheSubType;
    uint32_t    _pad0;
    uint64_t    objcOptsOffset;
    uint64_t    objcOptsSize;
    uint64_t    cacheAtlasOffset;
    uint64_t    cacheAtlasSize;
    uint64_t    dynamicDataOffset;
    uint64_t    dynamicDataMaxSize;
};

struct dyld_cache_mapping_info {
    uint64_t address; uint64_t size; uint64_t fileOffset;
    uint32_t maxProt; uint32_t initProt;
};

struct dyld_cache_mapping_and_slide_info {
    uint64_t address; uint64_t size; uint64_t fileOffset;
    uint64_t slideInfoFileOffset; uint64_t slideInfoFileSize;
    uint64_t flags; uint32_t maxProt; uint32_t initProt;
};

struct dyld_cache_image_info {
    uint64_t address; uint64_t modTime; uint64_t inode;
    uint32_t pathFileOffset; uint32_t pad;
};

struct dyld_cache_slide_info2 {
    uint32_t version;
    uint32_t page_size;
    uint32_t page_starts_offset;
    uint32_t page_starts_count;
    uint32_t page_extras_offset;
    uint32_t page_extras_count;
    uint64_t delta_mask;
    uint64_t value_add;
};

#define DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE  0x4000
#define DYLD_CACHE_SLIDE_PAGE_ATTR_EXTRA      0x8000
#define DYLD_CACHE_SLIDE_PAGE_ATTR_END        0x8000

/* ── Per-segment info ─────────────────────────────────────────── */

struct SegInfo {
    char     name[16];
    uint64_t origVmaddr;
    uint64_t vmsize;
    uint64_t origFileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    int      region;          /* 0=TEXT, 1=DATA, 2=LINKEDIT */
    uint64_t cacheFileOff;    /* file offset in cache */
    uint64_t cacheVmaddr;     /* VM address in cache */
};

/* ── Per-dylib info ───────────────────────────────────────────── */

struct DylibInfo {
    char        path[512];
    char        installName[256];
    uint8_t    *mapped;
    size_t      fileSize;
    struct stat st;

    struct SegInfo segs[MAX_SEGS];
    int nseg;
    int textSegIdx;
    int linkSegIdx;
    int dataSegIdx;       /* first DATA-region segment */

    uint64_t origLinkFileoff;

    /* Per-segment slide: cacheVmaddr - origVmaddr */
    int64_t segSlides[MAX_SEGS];
};

static struct DylibInfo gDylibs[MAX_DYLIBS];
static int              gNumDylibs;

/* Cache buffer */
static uint8_t *gBuf;
static uint64_t gBufSize;

/* Region file offsets and sizes in cache */
static uint64_t gMetaSize;
static uint64_t gTextFileOff, gTextSize;
static uint64_t gDataFileOff, gDataSize;
static uint64_t gLinkFileOff, gLinkSize;
static uint64_t gSlideInfoFileOff, gSlideInfoSize;

/* Slide info: track which DATA bytes are pointers (for slide info v2) */
#define MAX_REBASE_LOCS (1 << 20)  /* 1M locations */
static uint64_t gRebaseLocs[MAX_REBASE_LOCS]; /* file offsets within DATA region */
static int      gNumRebaseLocs;

static void record_rebase(uint64_t cacheFileOff) {
    if (gNumRebaseLocs < MAX_REBASE_LOCS)
        gRebaseLocs[gNumRebaseLocs++] = cacheFileOff;
}

/* ── Helpers ──────────────────────────────────────────────────── */

static void die(const char *msg) { fprintf(stderr, "FATAL: %s\n", msg); exit(1); }

static bool add_disp32_checked(int32_t disp, int64_t delta, int32_t *out) {
    int64_t adjusted = (int64_t)disp + delta;
    if (adjusted < INT32_MIN || adjusted > INT32_MAX)
        return false;
    *out = (int32_t)adjusted;
    return true;
}

static int seg_region(const char *name) {
    if (strcmp(name, "__TEXT") == 0)       return 0;
    if (strcmp(name, "__LINKEDIT") == 0)   return 2;
    return 1; /* __DATA, __DATA_CONST, __AUTH, etc. */
}

/* ── ULEB/SLEB128 ────────────────────────────────────────────── */

static uint64_t read_uleb128(const uint8_t **p) {
    uint64_t r = 0; int s = 0;
    do { r |= (uint64_t)(**p & 0x7f) << s; s += 7; } while (*(*p)++ & 0x80);
    return r;
}

static int64_t read_sleb128(const uint8_t **p) {
    int64_t r = 0; int s = 0; uint8_t b;
    do { b = *(*p)++; r |= (int64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80);
    if ((s < 64) && (b & 0x40)) r |= -(1LL << s);
    return r;
}

/* ── Load dylib ───────────────────────────────────────────────── */

static int load_dylib(const char *path) {
    if (gNumDylibs >= MAX_DYLIBS) die("too many dylibs");
    struct DylibInfo *d = &gDylibs[gNumDylibs];
    memset(d, 0, sizeof(*d));
    snprintf(d->path, sizeof(d->path), "%s", path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return -1; }
    if (fstat(fd, &d->st) < 0) { perror("fstat"); close(fd); return -1; }
    d->fileSize = d->st.st_size;
    d->mapped = mmap(NULL, d->fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (d->mapped == MAP_FAILED) { perror("mmap"); return -1; }

    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    if (mh->magic != MH_MAGIC_64 || mh->cputype != CPU_TYPE_X86_64) {
        fprintf(stderr, "%s: not x86_64 Mach-O\n", path);
        munmap(d->mapped, d->fileSize); return -1;
    }

    d->textSegIdx = -1;
    d->linkSegIdx = -1;
    d->dataSegIdx = -1;
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64 && d->nseg < MAX_SEGS) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            struct SegInfo *si = &d->segs[d->nseg];
            memcpy(si->name, seg->segname, 16);
            si->origVmaddr = seg->vmaddr;
            si->vmsize     = seg->vmsize;
            si->origFileoff= seg->fileoff;
            si->filesize   = seg->filesize;
            si->maxprot    = seg->maxprot;
            si->initprot   = seg->initprot;
            si->region     = seg_region(si->name);
            if (strcmp(si->name, "__TEXT") == 0)     d->textSegIdx = d->nseg;
            if (strcmp(si->name, "__LINKEDIT") == 0) d->linkSegIdx = d->nseg;
            if (si->region == 1 && d->dataSegIdx < 0) d->dataSegIdx = d->nseg;
            d->nseg++;
        }
        if (lc->cmd == LC_ID_DYLIB) {
            const struct dylib_command *dc = (const struct dylib_command *)cmd;
            snprintf(d->installName, sizeof(d->installName), "%s",
                     (const char *)cmd + dc->dylib.name.offset);
        }
        cmd += lc->cmdsize;
    }
    if (d->textSegIdx < 0 || d->installName[0] == '\0') {
        munmap(d->mapped, d->fileSize); return -1;
    }
    /* Deduplicate by install name */
    for (int i = 0; i < gNumDylibs; i++) {
        if (strcmp(gDylibs[i].installName, d->installName) == 0) {
            munmap(d->mapped, d->fileSize); return -1;
        }
    }
    if (d->linkSegIdx >= 0)
        d->origLinkFileoff = d->segs[d->linkSegIdx].origFileoff;

    gNumDylibs++;
    return 0;
}

/* ── Assign addresses & allocate buffer ───────────────────────── */

static void assign_addresses(void) {
    uint64_t tCur = 0, dCur = 0, lCur = 0;

    /* First pass: compute region sizes */
    for (int i = 0; i < gNumDylibs; i++) {
        struct DylibInfo *d = &gDylibs[i];
        for (int s = 0; s < d->nseg; s++) {
            uint64_t asz = PAGE_ALIGN(d->segs[s].vmsize);
            if (asz == 0) asz = PAGE_SZ;
            switch (d->segs[s].region) {
            case 0: tCur += asz; break;
            case 1: dCur += asz; break;
            case 2: lCur += asz; break;
            }
        }
    }

    /* Metadata: header + mappings + images + paths */
    uint64_t pathsSize = 0;
    for (int i = 0; i < gNumDylibs; i++)
        pathsSize += strlen(gDylibs[i].installName) + 1;
    gMetaSize = PAGE_ALIGN(sizeof(struct dyld_cache_header)
        + 3 * sizeof(struct dyld_cache_mapping_info)
        + 3 * sizeof(struct dyld_cache_mapping_and_slide_info)
        + gNumDylibs * sizeof(struct dyld_cache_image_info)
        + pathsSize
        + 256 /* padding */);

    /* File layout: [meta] [TEXT region] [DATA region] [LINKEDIT region] [slide info] */
    gTextFileOff = gMetaSize;
    gTextSize    = PAGE_ALIGN(tCur);
    gDataFileOff = gTextFileOff + gTextSize;
    gDataSize    = PAGE_ALIGN(dCur);
    gLinkFileOff = gDataFileOff + gDataSize;
    gLinkSize    = PAGE_ALIGN(lCur);

    /* Slide info goes after LINKEDIT (allocated later) */
    gSlideInfoFileOff = gLinkFileOff + gLinkSize;

    /* Estimate slide info size: header + 2 bytes per DATA page + extras */
    uint64_t dataPages = (gDataSize + PAGE_SZ - 1) / PAGE_SZ;
    gSlideInfoSize = PAGE_ALIGN(sizeof(struct dyld_cache_slide_info2)
                                + dataPages * sizeof(uint16_t) * 2
                                + 4096);

    gBufSize = gSlideInfoFileOff + gSlideInfoSize;
    gBuf = calloc(1, gBufSize);
    if (!gBuf) die("calloc");

    /* Second pass: assign per-segment file offsets and vmaddrs */
    tCur = dCur = lCur = 0;
    for (int i = 0; i < gNumDylibs; i++) {
        struct DylibInfo *d = &gDylibs[i];
        for (int s = 0; s < d->nseg; s++) {
            struct SegInfo *si = &d->segs[s];
            uint64_t asz = PAGE_ALIGN(si->vmsize);
            if (asz == 0) asz = PAGE_SZ;
            switch (si->region) {
            case 0:
                si->cacheFileOff = gTextFileOff + tCur;
                si->cacheVmaddr  = TEXT_REGION_BASE + tCur;
                tCur += asz;
                break;
            case 1:
                si->cacheFileOff = gDataFileOff + dCur;
                si->cacheVmaddr  = DATA_REGION_BASE + dCur;
                dCur += asz;
                break;
            case 2:
                si->cacheFileOff = gLinkFileOff + lCur;
                si->cacheVmaddr  = LINK_REGION_BASE + lCur;
                lCur += asz;
                break;
            }
            d->segSlides[s] = (int64_t)(si->cacheVmaddr - si->origVmaddr);
        }
    }

    printf("  Metadata:  %llu bytes\n", (unsigned long long)gMetaSize);
    printf("  TEXT:      %llu KB @ 0x%llx (file off 0x%llx)\n",
           (unsigned long long)gTextSize/1024, (unsigned long long)TEXT_REGION_BASE,
           (unsigned long long)gTextFileOff);
    printf("  DATA:      %llu KB @ 0x%llx (file off 0x%llx)\n",
           (unsigned long long)gDataSize/1024, (unsigned long long)DATA_REGION_BASE,
           (unsigned long long)gDataFileOff);
    printf("  LINKEDIT:  %llu KB @ 0x%llx (file off 0x%llx)\n",
           (unsigned long long)gLinkSize/1024, (unsigned long long)LINK_REGION_BASE,
           (unsigned long long)gLinkFileOff);
    printf("  Total:     %.1f MB\n", gBufSize / (1024.0 * 1024.0));
}

/* ── Copy segments into cache buffer ──────────────────────────── */

static void copy_segments(void) {
    for (int i = 0; i < gNumDylibs; i++) {
        struct DylibInfo *d = &gDylibs[i];
        for (int s = 0; s < d->nseg; s++) {
            struct SegInfo *si = &d->segs[s];
            if (si->filesize == 0) continue;
            if (si->cacheFileOff + si->filesize > gBufSize) {
                fprintf(stderr, "WARN: segment overflow for %s %s\n",
                        d->installName, si->name);
                continue;
            }
            memcpy(gBuf + si->cacheFileOff,
                   d->mapped + si->origFileoff,
                   si->filesize);
        }
    }
}

/* ── Rewrite load commands in cached mach_headers ─────────────── */

static void rewrite_load_commands(void) {
    for (int i = 0; i < gNumDylibs; i++) {
        struct DylibInfo *d = &gDylibs[i];
        if (d->textSegIdx < 0) continue;

        uint64_t mhOff = d->segs[d->textSegIdx].cacheFileOff;
        if (mhOff + sizeof(struct mach_header_64) > gBufSize) continue;
        uint8_t *mhBuf = gBuf + mhOff;
        struct mach_header_64 *mh = (struct mach_header_64 *)mhBuf;
        if (mh->magic != MH_MAGIC_64) {
            fprintf(stderr, "  WARN: dylib %d bad magic after copy\n", i);
            continue;
        }

        uint64_t origLinkFO = d->origLinkFileoff;
        uint64_t newLinkFO  = (d->linkSegIdx >= 0) ? d->segs[d->linkSegIdx].cacheFileOff : 0;

        uint8_t *cmd = mhBuf + sizeof(struct mach_header_64);
        uint8_t *cmdEnd = mhBuf + d->segs[d->textSegIdx].filesize;
        if (d->segs[d->textSegIdx].filesize < sizeof(struct mach_header_64) + mh->sizeofcmds) {
            fprintf(stderr, "  WARN: TEXT too small for load commands in dylib %d\n", i);
            continue;
        }
        cmdEnd = mhBuf + sizeof(struct mach_header_64) + mh->sizeofcmds;
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            if (cmd + sizeof(struct load_command) > cmdEnd) break;
            struct load_command *lc = (struct load_command *)cmd;
            if (lc->cmdsize == 0 || cmd + lc->cmdsize > cmdEnd) break;

            if (lc->cmd == LC_SEGMENT_64) {
                struct segment_command_64 *seg = (struct segment_command_64 *)cmd;
                for (int s = 0; s < d->nseg; s++) {
                    if (strncmp(d->segs[s].name, seg->segname, 16) == 0) {
                        int64_t vmDelta = (int64_t)(d->segs[s].cacheVmaddr - d->segs[s].origVmaddr);
                        int64_t foDelta = (int64_t)(d->segs[s].cacheFileOff - d->segs[s].origFileoff);
                        seg->vmaddr  = d->segs[s].cacheVmaddr;
                        seg->fileoff = d->segs[s].cacheFileOff;
                        /* Update sections */
                        struct section_64 *sect = (struct section_64 *)(cmd + sizeof(struct segment_command_64));
                        for (uint32_t ns = 0; ns < seg->nsects; ns++) {
                            sect[ns].addr   += vmDelta;
                            sect[ns].offset += (uint32_t)foDelta;
                        }
                        break;
                    }
                }
            }

            /* Adjust LINKEDIT-relative file offsets */
            #define ADJ(field) do { \
                if ((field) && (uint64_t)(field) >= origLinkFO) \
                    (field) = (uint32_t)(newLinkFO + ((uint64_t)(field) - origLinkFO)); \
            } while(0)
            if (d->linkSegIdx >= 0 && origLinkFO > 0) {
                if (lc->cmd == LC_SYMTAB) {
                    struct symtab_command *sc = (struct symtab_command *)cmd;
                    ADJ(sc->symoff); ADJ(sc->stroff);
                }
                if (lc->cmd == LC_DYSYMTAB) {
                    struct dysymtab_command *dc = (struct dysymtab_command *)cmd;
                    ADJ(dc->tocoff); ADJ(dc->modtaboff); ADJ(dc->extrefsymoff);
                    ADJ(dc->indirectsymoff); ADJ(dc->extreloff); ADJ(dc->locreloff);
                }
                if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
                    struct dyld_info_command *di = (struct dyld_info_command *)cmd;
                    ADJ(di->rebase_off); ADJ(di->bind_off); ADJ(di->weak_bind_off);
                    ADJ(di->lazy_bind_off); ADJ(di->export_off);
                }
                if (lc->cmd == LC_FUNCTION_STARTS || lc->cmd == LC_DATA_IN_CODE ||
                    lc->cmd == 0x80000034 /* LC_DYLD_CHAINED_FIXUPS */ ||
                    lc->cmd == 0x80000033 /* LC_DYLD_EXPORTS_TRIE */) {
                    struct linkedit_data_command *ld = (struct linkedit_data_command *)cmd;
                    ADJ(ld->dataoff);
                }
            }
            #undef ADJ

            cmd += lc->cmdsize;
        }
    }
}

/* ── Patch cross-segment RIP-relative references ──────────────── */

/*
 * On x86_64, RIP-relative instructions use a 32-bit signed displacement
 * from the end of the instruction. When TEXT and DATA are split into
 * separate regions, these displacements must be adjusted by:
 *   codeToDataDelta = dataSlide - textSlide
 *
 * We handle:
 * 1. __stubs: ff 25 XX XX XX XX  (jmp qword ptr [rip+disp32])
 * 2. __stub_helper header: lea r11 + jmp patterns
 * 3. General TEXT→DATA references: scan all executable TEXT for
 *    32-bit displacements that target DATA segment addresses
 */

/* Find a section in the ORIGINAL mach_header */
static const struct section_64 *find_section_orig(struct DylibInfo *d,
                                                   const char *segname,
                                                   const char *sectname) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            if (strncmp(seg->segname, segname, 16) == 0) {
                const struct section_64 *sect =
                    (const struct section_64 *)(cmd + sizeof(struct segment_command_64));
                for (uint32_t s = 0; s < seg->nsects; s++) {
                    if (strncmp(sect[s].sectname, sectname, 16) == 0)
                        return &sect[s];
                }
            }
        }
        cmd += lc->cmdsize;
    }
    return NULL;
}

/* Check if an original vmaddr falls within any DATA segment of this dylib */
static bool is_in_data_region(struct DylibInfo *d, uint64_t addr) {
    for (int s = 0; s < d->nseg; s++) {
        if (d->segs[s].region != 1) continue;
        if (addr >= d->segs[s].origVmaddr &&
            addr < d->segs[s].origVmaddr + d->segs[s].vmsize)
            return true;
    }
    return false;
}

/* Get codeToDataDelta: use first DATA segment's slide minus TEXT slide */
static int64_t get_code_to_data_delta(struct DylibInfo *d) {
    int64_t textSlide = d->segSlides[d->textSegIdx];
    for (int s = 0; s < d->nseg; s++) {
        if (d->segs[s].region == 1)
            return d->segSlides[s] - textSlide;
    }
    return 0; /* no DATA segment */
}

static bool is_legacy_prefix(uint8_t byte) {
    return byte == 0x66 || byte == 0xf2 || byte == 0xf3;
}

static bool is_onebyte_riprel_opcode(uint8_t opc, uint8_t modrm,
                                     bool operand16,
                                     uint64_t pos, uint64_t sectSize,
                                     uint32_t *trailingImm) {
    if ((modrm & 0xC7) != 0x05)
        return false;

    switch (opc) {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x08: case 0x09: case 0x0a: case 0x0b:
    case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x18: case 0x19: case 0x1a: case 0x1b:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x28: case 0x29: case 0x2a: case 0x2b:
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x38: case 0x39: case 0x3a: case 0x3b:
    case 0x63: case 0x84: case 0x85: case 0x86:
    case 0x87: case 0x88: case 0x89: case 0x8a:
    case 0x8b: case 0x8d: case 0xfe: case 0xff:
        return true;
    case 0x80:
    case 0x83:
    case 0xc6:
        *trailingImm = 1;
        return pos + 4 + *trailingImm <= sectSize;
    case 0x81:
    case 0xc7:
        *trailingImm = operand16 ? 2 : 4;
        return pos + 4 + *trailingImm <= sectSize;
    case 0xf6:
        *trailingImm = (((modrm >> 3) & 7) <= 1) ? 1 : 0;
        return pos + 4 + *trailingImm <= sectSize;
    case 0xf7:
        *trailingImm = (((modrm >> 3) & 7) <= 1) ? (operand16 ? 2 : 4) : 0;
        return pos + 4 + *trailingImm <= sectSize;
    default:
        return false;
    }
}

static bool is_0f_riprel_opcode(uint8_t opc, uint8_t modrm) {
    (void)opc;
    return (modrm & 0xC7) == 0x05;
}

static bool is_0f38_riprel_opcode(uint8_t opc, uint8_t modrm) {
    (void)opc;
    return (modrm & 0xC7) == 0x05;
}

static bool decode_riprel_disp32(const uint8_t *sectBuf, uint64_t pos,
                                 uint64_t sectSize, uint32_t *trailingImm) {
    *trailingImm = 0;

    if (pos >= 2) {
        uint8_t opc = sectBuf[pos - 2];
        uint8_t modrm = sectBuf[pos - 1];

        /* FF /2,/4,/6 with mod=00 rm=101 => call/jmp/push [rip+disp32]. */
        if (opc == 0xff && (modrm == 0x15 || modrm == 0x25 || modrm == 0x35))
            return true;

        if (is_onebyte_riprel_opcode(opc, modrm, false, pos, sectSize, trailingImm))
            return true;
    }

    if (pos >= 3) {
        uint8_t b0 = sectBuf[pos - 3];
        uint8_t opc = sectBuf[pos - 2];
        uint8_t modrm = sectBuf[pos - 1];

        if (((b0 & 0xF0) == 0x40 || is_legacy_prefix(b0)) &&
            is_onebyte_riprel_opcode(opc, modrm, b0 == 0x66,
                                     pos, sectSize, trailingImm))
            return true;

        if (b0 == 0x0f && is_0f_riprel_opcode(opc, modrm))
            return true;
    }

    if (pos >= 4) {
        uint8_t b0 = sectBuf[pos - 4];
        uint8_t b1 = sectBuf[pos - 3];
        uint8_t opc = sectBuf[pos - 2];
        uint8_t modrm = sectBuf[pos - 1];

        if (((b0 & 0xF0) == 0x40 && b1 == 0x0f) ||
            (is_legacy_prefix(b0) && b1 == 0x0f)) {
            if (is_0f_riprel_opcode(opc, modrm))
                return true;
        }

        if (is_legacy_prefix(b0) && (b1 & 0xF0) == 0x40 &&
            is_onebyte_riprel_opcode(opc, modrm, b0 == 0x66,
                                     pos, sectSize, trailingImm))
            return true;

        if (b0 == 0x0f && b1 == 0x38 && is_0f38_riprel_opcode(opc, modrm))
            return true;
    }

    if (pos >= 5) {
        uint8_t b0 = sectBuf[pos - 5];
        uint8_t b1 = sectBuf[pos - 4];
        uint8_t b2 = sectBuf[pos - 3];
        uint8_t opc = sectBuf[pos - 2];
        uint8_t modrm = sectBuf[pos - 1];

        if (is_legacy_prefix(b0) && (b1 & 0xF0) == 0x40 && b2 == 0x0f &&
            is_0f_riprel_opcode(opc, modrm))
            return true;

        if (((b0 & 0xF0) == 0x40 && b1 == 0x0f && b2 == 0x38) ||
            (is_legacy_prefix(b0) && b1 == 0x0f && b2 == 0x38)) {
            if (is_0f38_riprel_opcode(opc, modrm))
                return true;
        }
    }

    if (pos >= 6) {
        uint8_t pfx = sectBuf[pos - 6];
        uint8_t rex = sectBuf[pos - 5];
        uint8_t b0f = sectBuf[pos - 4];
        uint8_t b38 = sectBuf[pos - 3];
        uint8_t opc = sectBuf[pos - 2];
        uint8_t modrm = sectBuf[pos - 1];

        if (is_legacy_prefix(pfx) && (rex & 0xF0) == 0x40 &&
            b0f == 0x0f && b38 == 0x38 &&
            is_0f38_riprel_opcode(opc, modrm))
            return true;
    }

    return false;
}

static void patch_stubs(struct DylibInfo *d) {
    int64_t delta = get_code_to_data_delta(d);
    if (delta == 0) return;

    /* Patch __stubs: each entry is ff 25 XX XX XX XX (6 bytes) */
    const struct section_64 *stubs = find_section_orig(d, "__TEXT", "__stubs");
    if (stubs && stubs->size > 0) {
        /* Find the cache location of __stubs */
        uint64_t stubsCacheOff = d->segs[d->textSegIdx].cacheFileOff +
                                 (stubs->addr - d->segs[d->textSegIdx].origVmaddr);
        uint8_t *stubBuf = gBuf + stubsCacheOff;
        uint32_t stubSize = stubs->reserved2 ? stubs->reserved2 : 6;
        uint32_t numStubs = (uint32_t)(stubs->size / stubSize);

        int patched = 0;
        for (uint32_t i = 0; i < numStubs; i++) {
            uint8_t *entry = stubBuf + i * stubSize;
            if (entry[0] == 0xff && entry[1] == 0x25) {
                /* jmp [rip+disp32]: displacement at entry+2 */
                int32_t disp;
                memcpy(&disp, entry + 2, 4);
                int32_t newDisp;
                if (!add_disp32_checked(disp, delta, &newDisp)) {
                    fprintf(stderr, "FATAL: __stubs displacement overflow in %s\n",
                            d->installName);
                    exit(1);
                }
                memcpy(entry + 2, &newDisp, 4);
                patched++;
            }
        }
        if (patched > 0)
            printf("    __stubs: patched %d/%d entries (delta=0x%llx)\n",
                   patched, numStubs, (unsigned long long)delta);
    }

    /* Patch __stub_helper header: first ~16 bytes have two RIP-relative refs to DATA */
    const struct section_64 *stubHelper = find_section_orig(d, "__TEXT", "__stub_helper");
    if (stubHelper && stubHelper->size >= 16) {
        uint64_t shCacheOff = d->segs[d->textSegIdx].cacheFileOff +
                              (stubHelper->addr - d->segs[d->textSegIdx].origVmaddr);
        uint8_t *sh = gBuf + shCacheOff;

        /* Pattern 1: 4c 8d 1d XX XX XX XX  (lea r11, [rip+disp32]) */
        if (sh[0] == 0x4c && sh[1] == 0x8d && sh[2] == 0x1d) {
            int32_t disp;
            memcpy(&disp, sh + 3, 4);
            int32_t newDisp;
            if (!add_disp32_checked(disp, delta, &newDisp)) {
                fprintf(stderr, "FATAL: __stub_helper LEA displacement overflow in %s\n",
                        d->installName);
                exit(1);
            }
            memcpy(sh + 3, &newDisp, 4);
        }
        /* Pattern 2: 41 53 ff 25 XX XX XX XX  (push r11; jmp [rip+disp32]) */
        if (sh[7] == 0x41 && sh[8] == 0x53 && sh[9] == 0xff && sh[10] == 0x25) {
            int32_t disp;
            memcpy(&disp, sh + 11, 4);
            int32_t newDisp;
            if (!add_disp32_checked(disp, delta, &newDisp)) {
                fprintf(stderr, "FATAL: __stub_helper JMP displacement overflow in %s\n",
                        d->installName);
                exit(1);
            }
            memcpy(sh + 11, &newDisp, 4);
        }
    }
}

/*
 * Scan all executable TEXT sections for RIP-relative references to DATA.
 * For each 4-byte window in TEXT, interpret it as a signed displacement,
 * compute the target address, and check if it lands in a DATA segment.
 * If so, adjust the displacement by codeToDataDelta.
 *
 * This catches GOT loads, data references, and any other cross-segment
 * RIP-relative instruction that wasn't caught by the section-specific
 * patchers above.
 */
static void patch_text_rip_refs(struct DylibInfo *d) {
    int64_t delta = get_code_to_data_delta(d);
    if (delta == 0) return;

    /* Get TEXT segment info */
    struct SegInfo *textSeg = &d->segs[d->textSegIdx];

    /* Scan sections within TEXT that contain code (skip __cstring, __unwind_info, etc.) */
    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);
    int totalPatched = 0;

    for (uint32_t ci = 0; ci < mh->ncmds; ci++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)cmd;
            if (strcmp(seg->segname, "__TEXT") != 0) { cmd += lc->cmdsize; continue; }

            const struct section_64 *sect =
                (const struct section_64 *)(cmd + sizeof(struct segment_command_64));
            for (uint32_t si = 0; si < seg->nsects; si++) {
                /* Only scan executable code sections */
                uint8_t sectType = sect[si].flags & 0xFF;
                bool isCode = (strcmp(sect[si].sectname, "__text") == 0) ||
                              (sectType == 0x08 /* S_SYMBOL_STUBS */);
                /* Skip __stubs and __stub_helper (already patched above) */
                if (strcmp(sect[si].sectname, "__stubs") == 0 ||
                    strcmp(sect[si].sectname, "__stub_helper") == 0)
                    continue;
                if (!isCode) continue;

                uint64_t sectOff = sect[si].addr - textSeg->origVmaddr;
                uint8_t *sectBuf = gBuf + textSeg->cacheFileOff + sectOff;
                uint64_t sectSize = sect[si].size;

                if (sectSize < 4) continue;

                /* Scan every byte position for potential RIP-relative displacements */
                for (uint64_t pos = 0; pos + 4 <= sectSize; pos++) {
                    int32_t disp;
                    memcpy(&disp, sectBuf + pos, 4);
                    uint32_t trailingImm = 0;
                    if (!decode_riprel_disp32(sectBuf, pos, sectSize, &trailingImm))
                        continue;

                    /* RIP points after disp32 and any trailing immediate. */
                    uint64_t instrDispAddr = textSeg->origVmaddr + sectOff + pos;
                    uint64_t target = instrDispAddr + 4 + trailingImm + (int64_t)disp;

                    if (is_in_data_region(d, target)) {
                        int32_t newDisp;
                        if (!add_disp32_checked(disp, delta, &newDisp)) {
                            fprintf(stderr, "FATAL: __TEXT RIP-rel displacement overflow in %s\n",
                                    d->installName);
                            exit(1);
                        }
                        memcpy(sectBuf + pos, &newDisp, 4);
                        totalPatched++;
                    }
                }
            }
        }
        cmd += lc->cmdsize;
    }

    if (totalPatched > 0)
        printf("    __text scan: patched %d RIP-rel references\n", totalPatched);
}

static void patch_cross_segment_refs(void) {
    printf("\nPatching cross-segment references:\n");
    for (int i = 0; i < gNumDylibs; i++) {
        struct DylibInfo *d = &gDylibs[i];
        if (d->dataSegIdx < 0) continue;  /* no DATA segments, nothing to patch */

        printf("  [%2d] %s\n", i, d->installName);
        patch_stubs(d);
        patch_text_rip_refs(d);
    }
}

/* ── Pre-resolve fixups ──────────────────────────────────────── */

static struct SegInfo* find_seg_by_origidx(struct DylibInfo *d, int idx) {
    if (idx >= 0 && idx < d->nseg) return &d->segs[idx];
    return NULL;
}

static uint8_t* cache_loc(struct DylibInfo *d, int segIdx, uint64_t segOff) {
    struct SegInfo *si = find_seg_by_origidx(d, segIdx);
    if (!si) return NULL;
    uint64_t off = si->cacheFileOff + segOff;
    if (off + 8 > gBufSize) return NULL;
    return gBuf + off;
}

/* Apply rebase: read original pointer, find target segment, apply its slide */
static void do_rebase(struct DylibInfo *d, int segIdx, uint64_t segOff) {
    uint8_t *loc = cache_loc(d, segIdx, segOff);
    if (!loc) return;
    uint64_t val = *(uint64_t *)loc;
    if (val == 0) return;

    for (int s = 0; s < d->nseg; s++) {
        uint64_t sStart = d->segs[s].origVmaddr;
        uint64_t sEnd = sStart + d->segs[s].vmsize;
        if (val >= sStart && val < sEnd) {
            uint64_t newVal = val + d->segSlides[s];
            *(uint64_t *)loc = newVal;
            /* Record for slide info if location is in DATA */
            struct SegInfo *locSeg = find_seg_by_origidx(d, segIdx);
            if (locSeg && locSeg->region == 1)
                record_rebase(locSeg->cacheFileOff + segOff);
            return;
        }
    }
    /* Only opcode-selected locations whose current value maps back into a
     * known source segment are rewritten. Anything else must be left alone. */
}

static void apply_rebases(struct DylibInfo *d) {
    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);

    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
            const struct dyld_info_command *di = (const struct dyld_info_command *)cmd;
            if (di->rebase_size == 0) break;

            const uint8_t *p = d->mapped + di->rebase_off;
            const uint8_t *end = p + di->rebase_size;
            int segIdx = 0; uint64_t segOff = 0;

            while (p < end) {
                uint8_t byte = *p++;
                uint8_t opcode = byte & 0xF0;
                uint8_t imm = byte & 0x0F;
                switch (opcode) {
                case 0x00: goto reb_done;
                case 0x10: break;
                case 0x20: segIdx = imm; segOff = read_uleb128(&p); break;
                case 0x30: segOff += read_uleb128(&p); break;
                case 0x40: segOff += imm * 8; break;
                case 0x50:
                    for (int j = 0; j < imm; j++) { do_rebase(d, segIdx, segOff); segOff += 8; }
                    break;
                case 0x60: { uint64_t cnt = read_uleb128(&p);
                    for (uint64_t j = 0; j < cnt; j++) { do_rebase(d, segIdx, segOff); segOff += 8; }
                    break; }
                case 0x70: do_rebase(d, segIdx, segOff); segOff += read_uleb128(&p) + 8; break;
                case 0x80: { uint64_t cnt = read_uleb128(&p); uint64_t skip = read_uleb128(&p);
                    for (uint64_t j = 0; j < cnt; j++) { do_rebase(d, segIdx, segOff); segOff += skip + 8; }
                    break; }
                }
            }
            reb_done:;
        }
        cmd += lc->cmdsize;
    }
}

/* ── Symbol resolution (build time) ───────────────────────────── */

static uint64_t find_symbol_in_dylib(int dylibIdx, const char *name) {
    struct DylibInfo *d = &gDylibs[dylibIdx];
    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);

    uint32_t symoff = 0, nsyms = 0, stroff = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_SYMTAB) {
            const struct symtab_command *sc = (const struct symtab_command *)cmd;
            symoff = sc->symoff; nsyms = sc->nsyms; stroff = sc->stroff;
        }
        cmd += lc->cmdsize;
    }
    if (!symoff) return 0;

    const struct nlist_64 *syms = (const struct nlist_64 *)(d->mapped + symoff);
    const char *strtab = (const char *)(d->mapped + stroff);

    for (uint32_t i = 0; i < nsyms; i++) {
        if ((syms[i].n_type & 0x0e) == 0) continue;
        if (!(syms[i].n_type & 0x01)) continue;
        const char *sname = strtab + syms[i].n_un.n_strx;
        if (strcmp(sname, name) == 0) {
            uint64_t val = syms[i].n_value;
            for (int s = 0; s < d->nseg; s++) {
                uint64_t sS = d->segs[s].origVmaddr;
                uint64_t sE = sS + d->segs[s].vmsize;
                if (val >= sS && val < sE)
                    return val + d->segSlides[s];
            }
            if (d->textSegIdx >= 0)
                return val + d->segSlides[d->textSegIdx];
            return val;
        }
    }

    /* Check re-exports */
    cmd = d->mapped + sizeof(struct mach_header_64);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_REEXPORT_DYLIB) {
            const struct dylib_command *dc = (const struct dylib_command *)cmd;
            const char *depPath = (const char *)cmd + dc->dylib.name.offset;
            for (int j = 0; j < gNumDylibs; j++) {
                if (strcmp(gDylibs[j].installName, depPath) == 0 ||
                    strcmp(strrchr(gDylibs[j].installName, '/') ?: "",
                           strrchr(depPath, '/') ?: "") == 0) {
                    uint64_t addr = find_symbol_in_dylib(j, name);
                    if (addr) return addr;
                }
            }
        }
        cmd += lc->cmdsize;
    }
    return 0;
}

static int find_dylib_index_by_install_name(const char *installName) {
    if (!installName) return -1;
    for (int i = 0; i < gNumDylibs; i++) {
        if (strcmp(gDylibs[i].installName, installName) == 0)
            return i;
    }
    return -1;
}

static const char *preferred_provider_for_symbol(const char *name) {
    if (!name) return NULL;

    if (strcmp(name, "_malloc") == 0 ||
        strcmp(name, "_calloc") == 0 ||
        strcmp(name, "_free") == 0 ||
        strcmp(name, "_valloc") == 0 ||
        strcmp(name, "_aligned_alloc") == 0 ||
        strcmp(name, "_posix_memalign") == 0 ||
        strcmp(name, "_malloc_size") == 0 ||
        strcmp(name, "_malloc_good_size") == 0 ||
        strcmp(name, "_malloc_default_zone") == 0 ||
        strcmp(name, "_malloc_make_nonpurgeable") == 0 ||
        strcmp(name, "_malloc_make_purgeable") == 0 ||
        strncmp(name, "_malloc_zone_", 13) == 0) {
        return "/usr/lib/system/libsystem_malloc.dylib";
    }

    /* Darwin keeps BSD syscall veneers in libsystem_kernel.  Some Libc
     * objects, including exit.c, import the private assembler entry points
     * through flat-namespace lazy binds, so cache construction must not let
     * an earlier re-export or compatibility image satisfy these slots. */
    if (strcmp(name, "___exit") == 0 ||
        strcmp(name, "__exit") == 0) {
        return "/usr/lib/system/libsystem_kernel.dylib";
    }

    /* libobjc expects Panthera's dyld bridge entry points from
     * libpanthera_extra, not whichever re-export or flat-namespace match
     * happens to appear first during cache construction. */
    if (strcmp(name, "__dyld_for_each_objc_class") == 0 ||
        strcmp(name, "__dyld_for_each_objc_protocol") == 0 ||
        strcmp(name, "__dyld_for_objc_header_opt_ro") == 0 ||
        strcmp(name, "__dyld_for_objc_header_opt_rw") == 0 ||
        strcmp(name, "__dyld_get_dlopen_image_header") == 0 ||
        strcmp(name, "__dyld_get_image_uuid") == 0 ||
        strcmp(name, "__dyld_get_objc_selector") == 0 ||
        strcmp(name, "__dyld_get_prog_image_header") == 0 ||
        strcmp(name, "__dyld_get_shared_cache_range") == 0 ||
        strcmp(name, "__dyld_is_memory_immutable") == 0 ||
        strcmp(name, "__dyld_lookup_section_info") == 0 ||
        strcmp(name, "__dyld_objc_class_count") == 0 ||
        strcmp(name, "__dyld_objc_register_callbacks") == 0) {
        return "/usr/lib/system/libpanthera_extra.dylib";
    }

    return NULL;
}

static uint64_t resolve_symbol(const char *name, int fromDylib, int ordinal) {
    /* Check preferred provider first — overrides ordinal-based lookup to
     * ensure symbols like _malloc resolve to the real implementation
     * (libsystem_malloc) rather than stubs in other sub-libraries
     * (libsystem_kernel) that happen to appear earlier in the re-export chain. */
    const char *preferred = preferred_provider_for_symbol(name);
    if (preferred) {
        int idx = find_dylib_index_by_install_name(preferred);
        if (idx >= 0) {
            uint64_t addr = find_symbol_in_dylib(idx, name);
            if (addr) return addr;
        }
    }

    if (ordinal > 0 && fromDylib >= 0) {
        struct DylibInfo *d = &gDylibs[fromDylib];
        const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
        const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);
        int cur = 0;
        for (uint32_t i = 0; i < mh->ncmds; i++) {
            const struct load_command *lc = (const struct load_command *)cmd;
            if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_REEXPORT_DYLIB ||
                lc->cmd == LC_LOAD_WEAK_DYLIB) {
                cur++;
                if (cur == ordinal) {
                    const struct dylib_command *dc = (const struct dylib_command *)cmd;
                    const char *depPath = (const char *)cmd + dc->dylib.name.offset;
                    for (int j = 0; j < gNumDylibs; j++) {
                        if (strcmp(gDylibs[j].installName, depPath) == 0 ||
                            strcmp(strrchr(gDylibs[j].installName, '/') ?: "",
                                   strrchr(depPath, '/') ?: "") == 0) {
                            uint64_t addr = find_symbol_in_dylib(j, name);
                            if (addr) return addr;
                        }
                    }
                }
            }
            cmd += lc->cmdsize;
        }
    }

    /* Flat namespace */
    for (int j = 0; j < gNumDylibs; j++) {
        uint64_t addr = find_symbol_in_dylib(j, name);
        if (addr) return addr;
    }
    return 0;
}

/* ── Pre-resolve binds ────────────────────────────────────────── */

static int gUnresolved;

static void apply_binds(int dylibIdx) {
    struct DylibInfo *d = &gDylibs[dylibIdx];
    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);

    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
            const struct dyld_info_command *di = (const struct dyld_info_command *)cmd;

            for (int pass = 0; pass < 2; pass++) {
                uint32_t off = (pass == 0) ? di->bind_off : di->lazy_bind_off;
                uint32_t sz  = (pass == 0) ? di->bind_size : di->lazy_bind_size;
                if (!sz) continue;

                const uint8_t *p = d->mapped + off;
                const uint8_t *end = p + sz;
                int segIdx = 0; uint64_t segOff = 0;
                const char *symName = ""; int ordinal = 0; int64_t addend = 0;
                uint8_t symFlags = 0;

                while (p < end) {
                    uint8_t byte = *p++;
                    uint8_t opcode = byte & 0xF0;
                    uint8_t imm = byte & 0x0F;

                    switch (opcode) {
                    case 0x00:
                        if (pass == 0 && p >= end) goto bind_done;
                        break;
                    case 0x10: ordinal = imm; break;
                    case 0x20: ordinal = (int)read_uleb128(&p); break;
                    case 0x30:
                        if (imm == 0) ordinal = 0;
                        else ordinal = (int8_t)(0xF0 | imm);
                        break;
                    case 0x40:
                        symFlags = imm;
                        symName = (const char *)p;
                        while (p < end && *p) p++; if (p < end) p++;
                        break;
                    case 0x50: break;
                    case 0x60: addend = read_sleb128(&p); break;
                    case 0x70: segIdx = imm; segOff = read_uleb128(&p); break;
                    case 0x80: segOff += read_uleb128(&p); break;
                    case 0x90: case 0xa0: case 0xb0: case 0xc0: {
                        uint64_t addr = resolve_symbol(symName, dylibIdx, ordinal);
                        uint64_t count = 1, skip = 0;
                        if (opcode == 0xa0) skip = read_uleb128(&p);
                        else if (opcode == 0xb0) skip = imm * 8;
                        else if (opcode == 0xc0) { count = read_uleb128(&p); skip = read_uleb128(&p); }

                        for (uint64_t j = 0; j < count; j++) {
                            uint8_t *loc = cache_loc(d, segIdx, segOff);
                            if (loc) {
                                if (addr) {
                                    *(uint64_t *)loc = addr + addend;
                                } else if (symFlags & 0x01 /* BIND_SYMBOL_FLAGS_WEAK_IMPORT */) {
                                    *(uint64_t *)loc = 0;
                                } else {
                                    *(uint64_t *)loc = 0;
                                    gUnresolved++;
                                    fprintf(stderr, "  UNRESOLVED: %s needs '%s' (ord %d)\n",
                                            d->installName, symName, ordinal);
                                }
                                /* Record for slide info */
                                struct SegInfo *locSeg = find_seg_by_origidx(d, segIdx);
                                if (locSeg && locSeg->region == 1 && addr)
                                    record_rebase(locSeg->cacheFileOff + segOff);
                            }
                            segOff += (opcode == 0x90) ? 8 : (skip + 8);
                            if (opcode == 0xa0 || opcode == 0xb0) break;
                        }
                        if (opcode == 0x90) segOff += 0; /* already advanced */
                        break;
                    }
                    }
                }
                bind_done:;
            }
        }
        cmd += lc->cmdsize;
    }
}

/* ── Pre-resolve chained fixups ───────────────────────────────── */

struct chained_fixups_hdr { uint32_t ver, starts_off, imports_off, syms_off, imports_cnt, imports_fmt, syms_fmt; };
struct chained_starts_img { uint32_t seg_count; uint32_t seg_info_offset[1]; };
struct chained_starts_seg { uint32_t size; uint16_t page_size, ptr_fmt; uint64_t seg_off; uint32_t max_valid; uint16_t page_count; uint16_t page_start[1]; };

static void apply_chained(int dylibIdx) {
    struct DylibInfo *d = &gDylibs[dylibIdx];
    const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
    const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);

    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cmd;
        if (lc->cmd == 0x80000034 /* LC_DYLD_CHAINED_FIXUPS */) {
            const struct linkedit_data_command *ldc = (const struct linkedit_data_command *)cmd;
            const uint8_t *fixData = d->mapped + ldc->dataoff;
            const struct chained_fixups_hdr *hdr = (const struct chained_fixups_hdr *)fixData;
            const struct chained_starts_img *starts = (const struct chained_starts_img *)(fixData + hdr->starts_off);
            const uint8_t *importsBase = fixData + hdr->imports_off;
            const char *symStrs = (const char *)(fixData + hdr->syms_off);

            for (uint32_t seg = 0; seg < starts->seg_count; seg++) {
                if (starts->seg_info_offset[seg] == 0) continue;
                const struct chained_starts_seg *segInfo =
                    (const struct chained_starts_seg *)((const uint8_t *)starts + starts->seg_info_offset[seg]);

                uint64_t textOrigVA = d->segs[d->textSegIdx].origVmaddr;
                uint64_t targetVA = textOrigVA + segInfo->seg_off;
                int matchSeg = -1;
                for (int s = 0; s < d->nseg; s++) {
                    if (d->segs[s].origVmaddr == targetVA) { matchSeg = s; break; }
                }
                if (matchSeg < 0) continue;

                for (uint16_t page = 0; page < segInfo->page_count; page++) {
                    uint16_t ps = segInfo->page_start[page];
                    if (ps == 0xFFFF) continue;

                    uint8_t *pageBase = gBuf + d->segs[matchSeg].cacheFileOff + page * segInfo->page_size;
                    uint8_t *loc = pageBase + ps;

                    while (1) {
                        uint64_t raw = *(uint64_t *)loc;
                        int isBind = (raw >> 63) & 1;
                        uint32_t next = (raw >> 51) & 0xFFF;

                        if (isBind) {
                            uint32_t ordIdx = raw & 0xFFFFFF;
                            int8_t addend = (raw >> 24) & 0xFF;
                            if (ordIdx < hdr->imports_cnt) {
                                uint32_t impBits = *(const uint32_t *)(importsBase + ordIdx * 4);
                                int libOrd = (int)(int8_t)(impBits & 0xFF);
                                uint32_t nameOff = impBits >> 9;
                                const char *symName = symStrs + nameOff;
                                uint64_t addr = resolve_symbol(symName, dylibIdx, libOrd);
                                *(uint64_t *)loc = addr ? (addr + addend) : 0;
                                if (!addr) {
                                    gUnresolved++;
                                    fprintf(stderr, "  UNRESOLVED(chained): %s needs '%s' (ord %d)\n",
                                            gDylibs[dylibIdx].installName, symName, libOrd);
                                }
                            } else {
                                *(uint64_t *)loc = 0;
                            }
                        } else {
                            uint64_t target = raw & 0xFFFFFFFFFULL;
                            uint8_t high8 = (raw >> 36) & 0xFF;
                            uint16_t ptrFmt = segInfo->ptr_fmt;

                            /*
                             * DYLD_CHAINED_PTR_64_OFFSET stores an offset from the
                             * original image's __TEXT base, not from the split-cache
                             * TEXT region. Reconstruct the original target VA first,
                             * then apply the slide of the segment that actually owns
                             * that target in the split layout.
                             */
                            uint64_t sourceTarget = target;
                            if (ptrFmt == 6 /* DYLD_CHAINED_PTR_64_OFFSET */)
                                sourceTarget = d->segs[d->textSegIdx].origVmaddr + target;

                            uint64_t newAddr;
                            if (ptrFmt == 6)
                                newAddr = d->segs[d->textSegIdx].cacheVmaddr + target;
                            else
                                newAddr = sourceTarget;

                            for (int s = 0; s < d->nseg; s++) {
                                uint64_t sS = d->segs[s].origVmaddr;
                                uint64_t sE = sS + d->segs[s].vmsize;
                                if (sourceTarget >= sS && sourceTarget < sE) {
                                    newAddr = sourceTarget + d->segSlides[s];
                                    break;
                                }
                            }
                            newAddr |= ((uint64_t)high8 << 56);
                            *(uint64_t *)loc = newAddr;
                        }

                        /* Record for slide info (if in DATA region) */
                        uint64_t locOff = (uint64_t)(loc - gBuf);
                        if (locOff >= gDataFileOff && locOff < gDataFileOff + gDataSize)
                            record_rebase(locOff);

                        if (next == 0) break;
                        loc += next * 4;
                    }
                }
            }
        }
        cmd += lc->cmdsize;
    }
}

/* ── Adjust symtab values in LINKEDIT ─────────────────────────── */

static void adjust_symtab_values(void) {
    for (int i = 0; i < gNumDylibs; i++) {
        struct DylibInfo *d = &gDylibs[i];
        if (d->linkSegIdx < 0) continue;

        const struct mach_header_64 *mh = (const struct mach_header_64 *)d->mapped;
        const uint8_t *cmd = d->mapped + sizeof(struct mach_header_64);
        uint32_t origSymoff = 0, nsyms = 0;
        for (uint32_t c = 0; c < mh->ncmds; c++) {
            const struct load_command *lc = (const struct load_command *)cmd;
            if (lc->cmd == LC_SYMTAB) {
                const struct symtab_command *sc = (const struct symtab_command *)cmd;
                origSymoff = sc->symoff; nsyms = sc->nsyms;
            }
            cmd += lc->cmdsize;
        }
        if (!origSymoff || !nsyms) continue;

        uint64_t symCacheOff = d->segs[d->linkSegIdx].cacheFileOff +
                               (origSymoff - d->origLinkFileoff);
        if (symCacheOff + nsyms * sizeof(struct nlist_64) > gBufSize) continue;
        struct nlist_64 *syms = (struct nlist_64 *)(gBuf + symCacheOff);

        for (uint32_t s = 0; s < nsyms; s++) {
            if ((syms[s].n_type & 0x0e) == 0) continue;
            uint64_t val = syms[s].n_value;
            if (val == 0) continue;
            for (int seg = 0; seg < d->nseg; seg++) {
                uint64_t sS = d->segs[seg].origVmaddr;
                uint64_t sE = sS + d->segs[seg].vmsize;
                if (val >= sS && val < sE) {
                    syms[s].n_value = val + d->segSlides[seg];
                    break;
                }
            }
        }
    }
}

/* ── Generate slide info v2 ──────────────────────────────────── */

static int compare_u64(const void *a, const void *b) {
    uint64_t va = *(const uint64_t *)a, vb = *(const uint64_t *)b;
    return (va > vb) - (va < vb);
}

static void generate_slide_info_v2(void) {
    if (gNumRebaseLocs == 0) {
        printf("  No rebase locations recorded — skipping slide info\n");
        return;
    }

    /* Sort and deduplicate rebase locations */
    qsort(gRebaseLocs, gNumRebaseLocs, sizeof(uint64_t), compare_u64);
    int unique = 1;
    for (int i = 1; i < gNumRebaseLocs; i++) {
        if (gRebaseLocs[i] != gRebaseLocs[i-1])
            gRebaseLocs[unique++] = gRebaseLocs[i];
    }
    gNumRebaseLocs = unique;

    printf("  %d unique rebase locations in DATA region\n", gNumRebaseLocs);

    uint32_t dataPages = (uint32_t)((gDataSize + PAGE_SZ - 1) / PAGE_SZ);

    /*
     * Slide info v2 format for x86_64:
     *   delta_mask = 0x00FFFF0000000000 (bits 40-55 hold delta)
     *   value_add  = SHARED_REGION_BASE (base address of the cache)
     *
     * Each pointer in DATA is encoded as:
     *   bits [0-39]  = value - value_add  (40-bit cache-relative offset)
     *   bits [40-55] = delta to next pointer / 8  (16-bit, 8-byte stride)
     *   bits [56-63] = unused
     *
     * page_starts[pageIndex] = first rebase location's page offset in 4-byte units.
     */
    uint64_t deltaMask = 0x00FFFF0000000000ULL;
    uint64_t valueMask = ~deltaMask;
    uint64_t valueAdd  = SHARED_REGION_BASE;
    int deltaShift = __builtin_ctzll(deltaMask) - 2; /* 40 - 2 = 38 */

    /* Allocate page_starts array */
    uint16_t *pageStarts = calloc(dataPages, sizeof(uint16_t));
    if (!pageStarts) die("calloc page_starts");

    /* Initialize all pages as no-rebase */
    for (uint32_t p = 0; p < dataPages; p++)
        pageStarts[p] = DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE;

    /* Process each page */
    int locIdx = 0;
    for (uint32_t p = 0; p < dataPages && locIdx < gNumRebaseLocs; p++) {
        uint64_t pageFileStart = gDataFileOff + (uint64_t)p * PAGE_SZ;
        uint64_t pageFileEnd   = pageFileStart + PAGE_SZ;

        /* Find all rebase locations in this page */
        int firstInPage = -1;
        while (locIdx < gNumRebaseLocs && gRebaseLocs[locIdx] < pageFileStart)
            locIdx++;

        int savedIdx = locIdx;
        while (locIdx < gNumRebaseLocs && gRebaseLocs[locIdx] < pageFileEnd) {
            uint64_t locOff = gRebaseLocs[locIdx] - pageFileStart; /* offset within page */

            if (firstInPage < 0) {
                firstInPage = (int)locOff;
                pageStarts[p] = (uint16_t)(locOff / 4);
            }

            locIdx++;
        }

        if (firstInPage < 0) continue; /* no rebases in this page */

        /* Now encode the chain: walk through locations in this page and encode deltas */
        locIdx = savedIdx;
        while (locIdx < gNumRebaseLocs && gRebaseLocs[locIdx] < pageFileEnd) {
            uint64_t locFileOff = gRebaseLocs[locIdx];
            uint8_t *ptr = gBuf + locFileOff;
            uint64_t rawValue = *(uint64_t *)ptr;

            /* Encode this pointer: strip base, store as value + delta */
            uint64_t value = 0;
            if (rawValue != 0) {
                if (rawValue < valueAdd) {
                    fprintf(stderr, "FATAL: slide-info pointer below shared-region base: 0x%llx\n",
                            (unsigned long long)rawValue);
                    exit(1);
                }
                value = rawValue - valueAdd;
                if (value & deltaMask) {
                    fprintf(stderr, "FATAL: slide-info pointer does not fit value mask: 0x%llx\n",
                            (unsigned long long)rawValue);
                    exit(1);
                }
            }
            value &= valueMask;

            /* Compute delta to next location */
            uint64_t deltaToNext = 0;
            if (locIdx + 1 < gNumRebaseLocs && gRebaseLocs[locIdx + 1] < pageFileEnd) {
                deltaToNext = gRebaseLocs[locIdx + 1] - locFileOff;
            }

            /* Encode: value in low bits, delta in delta_mask bits */
            uint64_t encodedDelta = (deltaToNext << deltaShift) & deltaMask;
            uint64_t encoded = value | encodedDelta;
            *(uint64_t *)ptr = encoded;

            locIdx++;
        }
    }

    /* Write slide info v2 header */
    struct dyld_cache_slide_info2 *si = (struct dyld_cache_slide_info2 *)(gBuf + gSlideInfoFileOff);
    si->version = 2;
    si->page_size = PAGE_SZ;
    si->page_starts_offset = sizeof(struct dyld_cache_slide_info2);
    si->page_starts_count = dataPages;
    si->page_extras_offset = si->page_starts_offset + dataPages * sizeof(uint16_t);
    si->page_extras_count = 0;
    si->delta_mask = deltaMask;
    si->value_add = valueAdd;

    /* Copy page_starts */
    uint16_t *dst = (uint16_t *)(gBuf + gSlideInfoFileOff + si->page_starts_offset);
    memcpy(dst, pageStarts, dataPages * sizeof(uint16_t));

    gSlideInfoSize = PAGE_ALIGN(si->page_starts_offset + dataPages * sizeof(uint16_t));

    free(pageStarts);
    printf("  Slide info v2: %u pages, %llu bytes\n", dataPages,
           (unsigned long long)gSlideInfoSize);
}

/* ── Write header & metadata ──────────────────────────────────── */

static void write_metadata(void) {
    uint32_t mappingOff = sizeof(struct dyld_cache_header);
    uint32_t slideMapOff = mappingOff + 3 * sizeof(struct dyld_cache_mapping_info);
    uint32_t imagesOff = slideMapOff + 3 * sizeof(struct dyld_cache_mapping_and_slide_info);
    uint32_t pathsOff = imagesOff + gNumDylibs * sizeof(struct dyld_cache_image_info);

    struct dyld_cache_header *hdr = (struct dyld_cache_header *)gBuf;
    memcpy(hdr->magic, "dyld_v1  x86_64\0", 16);
    hdr->mappingOffset       = mappingOff;
    hdr->mappingCount        = 3;
    hdr->imagesOffset        = imagesOff;
    hdr->imagesCount         = gNumDylibs;
    hdr->imagesOffsetOld     = imagesOff;
    hdr->imagesCountOld      = gNumDylibs;
    hdr->sharedRegionStart   = SHARED_REGION_BASE;
    hdr->sharedRegionSize    = (LINK_REGION_BASE + gLinkSize) - SHARED_REGION_BASE;
    hdr->platform            = 1; /* macOS */
    hdr->formatBits          = (1 << 10) | (1 << 11); /* locallyBuiltCache + builtFromChainedFixups */
    hdr->cacheType           = 0; /* development */
    hdr->mappingWithSlideOffset = slideMapOff;
    hdr->mappingWithSlideCount  = 3;

    /* Generate UUID */
    uuid_generate(hdr->uuid);

    /* Mappings: TEXT (RX), DATA (RW), LINKEDIT (RO) */
    struct dyld_cache_mapping_info *maps = (struct dyld_cache_mapping_info *)(gBuf + mappingOff);
    maps[0] = (struct dyld_cache_mapping_info){
        TEXT_REGION_BASE, gTextSize, gTextFileOff, 5 /* RX */, 5
    };
    maps[1] = (struct dyld_cache_mapping_info){
        DATA_REGION_BASE, gDataSize, gDataFileOff, 3 /* RW */, 3
    };
    maps[2] = (struct dyld_cache_mapping_info){
        LINK_REGION_BASE, gLinkSize, gLinkFileOff, 1 /* RO */, 1
    };

    /* Mapping-and-slide info */
    struct dyld_cache_mapping_and_slide_info *smaps =
        (struct dyld_cache_mapping_and_slide_info *)(gBuf + slideMapOff);
    for (int i = 0; i < 3; i++) {
        smaps[i].address    = maps[i].address;
        smaps[i].size       = maps[i].size;
        smaps[i].fileOffset = maps[i].fileOffset;
        smaps[i].maxProt    = maps[i].maxProt;
        smaps[i].initProt   = maps[i].initProt;
        smaps[i].flags      = 0;
        smaps[i].slideInfoFileOffset = 0;
        smaps[i].slideInfoFileSize   = 0;
    }
    /* DATA mapping gets slide info */
    if (gSlideInfoSize > 0 && gNumRebaseLocs > 0) {
        smaps[1].slideInfoFileOffset = gSlideInfoFileOff;
        smaps[1].slideInfoFileSize   = gSlideInfoSize;
    }

    /* Image info */
    struct dyld_cache_image_info *imgs = (struct dyld_cache_image_info *)(gBuf + imagesOff);
    uint32_t pathCur = pathsOff;
    for (int i = 0; i < gNumDylibs; i++) {
        imgs[i].address        = gDylibs[i].segs[gDylibs[i].textSegIdx].cacheVmaddr;
        imgs[i].modTime        = gDylibs[i].st.st_mtime;
        imgs[i].inode          = gDylibs[i].st.st_ino;
        imgs[i].pathFileOffset = pathCur;
        size_t nl = strlen(gDylibs[i].installName) + 1;
        memcpy(gBuf + pathCur, gDylibs[i].installName, nl);
        pathCur += nl;
    }
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    const char *sysroot = NULL, *output = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sysroot") == 0 && i+1<argc) sysroot = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i+1<argc) output = argv[++i];
    }
    if (!sysroot || !output) {
        fprintf(stderr, "Usage: %s --sysroot <path> --output <path>\n", argv[0]);
        return 1;
    }

    printf("Panthera real shared cache builder\n");
    printf("  Sysroot: %s\n  Output:  %s\n", sysroot, output);
    printf("  TEXT base:    0x%llx\n", (unsigned long long)TEXT_REGION_BASE);
    printf("  DATA base:    0x%llx\n", (unsigned long long)DATA_REGION_BASE);
    printf("  LINKEDIT base: 0x%llx\n\n", (unsigned long long)LINK_REGION_BASE);

    static const char *paths[] = {
        "usr/lib/libSystem.B.dylib", "usr/lib/libiconv.2.dylib",
        "usr/lib/libncurses.5.4.dylib", "usr/lib/system/libsystem_kernel.dylib",
        "usr/lib/system/libsystem_platform.dylib", "usr/lib/system/libsystem_malloc.dylib",
        "usr/lib/system/libsystem_c.dylib", "usr/lib/system/libsystem_info.dylib",
        "usr/lib/system/libsystem_pthread.dylib", "usr/lib/system/libclosure.dylib",
        "usr/lib/system/libsystem_trace.dylib", "usr/lib/system/libsystem_sandbox.dylib",
        "usr/lib/system/libsystem_notify.dylib", "usr/lib/system/libsystem_asl.dylib",
        "usr/lib/system/libdispatch.dylib",
        "usr/lib/system/libxpc.dylib", "usr/lib/system/libpanthera_launchd.dylib",
        "usr/lib/system/libpanthera_extra.dylib", "usr/lib/system/libpanthera_patch.dylib",
        "usr/lib/libc++abi.dylib", "usr/lib/libc++.1.dylib", "usr/lib/libicucore.A.dylib", "usr/lib/libobjc.A.dylib", "usr/lib/libCoreFoundation.dylib",
        NULL
    };

    printf("Loading dylibs:\n");
    fflush(stdout);
    for (int i = 0; paths[i]; i++) {
        char fp[1024]; snprintf(fp, sizeof(fp), "%s/%s", sysroot, paths[i]);
        printf("  %-50s", paths[i]);
        printf("%s\n", load_dylib(fp) == 0 ? "OK" : "SKIP");
        fflush(stdout);
    }
    printf("\n%d dylibs loaded\n\n", gNumDylibs);
    fflush(stdout);

    printf("Assigning addresses:\n");
    fflush(stdout);
    assign_addresses();
    fflush(stdout);

    printf("\nCopying segments...\n");
    fflush(stdout);
    copy_segments();

    printf("Rewriting load commands...\n");
    fflush(stdout);
    rewrite_load_commands();

    /* Patch cross-segment references BEFORE pre-resolving fixups.
     * This ensures stub displacements are correct for the new layout,
     * and fixup resolution writes the correct absolute addresses. */
    patch_cross_segment_refs();

    printf("\nPre-resolving fixups:\n");
    gUnresolved = 0;
    for (int i = 0; i < gNumDylibs; i++) {
        printf("  [%2d] %-40s", i, gDylibs[i].installName);
        apply_rebases(&gDylibs[i]);
        apply_binds(i);
        apply_chained(i);
        printf("OK\n");
    }
    if (gUnresolved > 0)
        printf("  WARNING: %d symbols unresolved (set to 0)\n", gUnresolved);

    printf("\nAdjusting symbol table values...\n");
    adjust_symtab_values();

    printf("\nGenerating slide info v2...\n");
    generate_slide_info_v2();

    printf("\nWriting header and metadata...\n");
    write_metadata();

    /* Trim to actual size (slide info may be smaller than estimated) */
    uint64_t actualSize = gSlideInfoFileOff + gSlideInfoSize;
    if (actualSize < gBufSize) gBufSize = actualSize;

    FILE *fp = fopen(output, "wb");
    if (!fp) { perror(output); return 1; }
    fwrite(gBuf, 1, gBufSize, fp);
    fclose(fp);

    printf("\nCache: %s (%.1f MB, %d dylibs)\n", output, gBufSize / (1024.0 * 1024.0), gNumDylibs);
    for (int i = 0; i < gNumDylibs; i++) {
        printf("  [%2d] %-45s TEXT=0x%llx",
               i, gDylibs[i].installName,
               (unsigned long long)gDylibs[i].segs[gDylibs[i].textSegIdx].cacheVmaddr);
        if (gDylibs[i].dataSegIdx >= 0)
            printf("  DATA=0x%llx",
                   (unsigned long long)gDylibs[i].segs[gDylibs[i].dataSegIdx].cacheVmaddr);
        printf("\n");
    }

    for (int i = 0; i < gNumDylibs; i++) munmap(gDylibs[i].mapped, gDylibs[i].fileSize);
    free(gBuf);
    return 0;
}
