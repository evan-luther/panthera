/*
 * panthera_dyld.cpp — Functional dynamic linker for Panthera Darwin
 *
 * Uses Apple's open-source Mach-O parsing library (mach_o/ and common/)
 * but implements the loader/binder logic directly for x86_64.
 *
 * This dyld can:
 *   1. Parse the main executable's Mach-O header
 *   2. Map dependent dylibs from the filesystem
 *   3. Process rebase opcodes (ASLR pointer fixups)
 *   4. Process bind opcodes (symbol resolution)
 *   5. Call initializers
 *   6. Jump to LC_MAIN entry point
 *
 * Compiled with -nostdlib, uses raw syscalls only.
 * Entry point: __dyld_start (from dyldStartup.s)
 */

// Use Apple's Mach-O headers
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>

// Minimal string/memory (can't use libc)
extern "C" {

static size_t _strlen(const char* s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}

static size_t _strnlen(const char* s, size_t max) {
    size_t n = 0;
    if (!s) return 0;
    while (n < max && s[n]) n++;
    return n;
}

static int _strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

static int _strncmp(const char* a, const char* b, size_t n) {
    while (n-- && *a && *a == *b) { a++; b++; }
    return n == (size_t)-1 ? 0 : *(unsigned char*)a - *(unsigned char*)b;
}

static bool _streq_bounded(const char* a, const char* b, size_t bMax) {
    if (!a || !b) return false;
    size_t i = 0;
    while (i < bMax && a[i] && b[i] && a[i] == b[i]) i++;
    bool bTerminated = (i < bMax && b[i] == '\0');
    return a[i] == '\0' && bTerminated;
}

static bool _contains(const char* s, const char* needle) {
    if (!s || !needle || !*needle) return false;
    size_t nlen = _strlen(needle);
    for (const char* p = s; *p; p++) {
        if (_strncmp(p, needle, nlen) == 0)
            return true;
    }
    return false;
}

static const char* _basename(const char* path) {
    const char* base = path;
    while (*path) {
        if (*path == '/')
            base = path + 1;
        path++;
    }
    return base;
}

static void* _memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;

    while (n && (((uintptr_t)d | (uintptr_t)s) & (sizeof(uint64_t) - 1))) {
        *d++ = *s++;
        n--;
    }

    uint64_t* dw = (uint64_t*)d;
    const uint64_t* sw = (const uint64_t*)s;
    while (n >= sizeof(uint64_t)) {
        *dw++ = *sw++;
        n -= sizeof(uint64_t);
    }

    d = (unsigned char*)dw;
    s = (const unsigned char*)sw;
    while (n--) *d++ = *s++;
    return dst;
}

static void* _memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void* memset(void* s, int c, size_t n) {
    return _memset(s, c, n);
}

// Syscall wrappers
static long _syscall(long number, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    unsigned char carry;
    register long r10 asm("r10") = a4;
    register long r8  asm("r8")  = a5;
    register long r9  asm("r9")  = a6;
    asm volatile("syscall"
        : "=a"(ret), "=@ccc"(carry)
        : "a"(number | 0x2000000), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    return carry ? -ret : ret;
}

#define sys_write(fd,b,n)  _syscall(4,(fd),(long)(b),(n),0,0,0)
#define sys_open(p,f,m)    _syscall(5,(long)(p),(f),(m),0,0,0)
#define sys_close(fd)      _syscall(6,(fd),0,0,0,0,0)
#define sys_read(fd,b,n)   _syscall(3,(fd),(long)(b),(n),0,0,0)
#define sys_mmap(a,l,p,f,fd,o) _syscall(197,(long)(a),(l),(p),(f),(fd),(o))
#define sys_munmap(a,l)    _syscall(73,(long)(a),(l),0,0,0,0)
#define sys_mprotect(a,l,p) _syscall(74,(long)(a),(l),(p),0,0,0)
#define sys_lseek(fd,o,w)  _syscall(199,(fd),(long)(o),(w),0,0,0)
#define sys_exit(c)        _syscall(1,(c),0,0,0,0,0)
#define sys_stat(p,b)      _syscall(338,(long)(p),(long)(b),0,0,0,0)
#define sys_fstat(fd,b)    _syscall(339,(fd),(long)(b),0,0,0,0)

static void _puts(const char* s) { sys_write(2, s, _strlen(s)); }

static void _trace_write(const char* s, size_t n) {
    long wrote = sys_write(2, s, n);
    if (wrote >= 0) {
        return;
    }
    static int consoleFD = -2;
    if (consoleFD == -2) {
        consoleFD = (int)sys_open("/dev/console", O_WRONLY, 0);
    }
    if (consoleFD >= 0) {
        sys_write(consoleFD, s, n);
    }
}

static bool readFileFully(int fd, void* buffer, long size) {
    char* readPtr = (char*)buffer;
    long remaining = size;
    while (remaining > 0) {
        long want = remaining > (64 * 1024) ? (64 * 1024) : remaining;
        long chunk = sys_read(fd, readPtr, want);
        if (chunk <= 0) {
            return false;
        }
        readPtr += chunk;
        remaining -= chunk;
    }
    return true;
}

static bool readFileFullyAt(int fd, uint64_t offset, void* buffer, long size) {
    if (sys_lseek(fd, offset, 0) < 0) {
        return false;
    }
    return readFileFully(fd, buffer, size);
}

static void _puthex(uint64_t v) {
    char buf[19] = "0x";
    for (int i = 17; i >= 2; i--) {
        int d = v & 0xf;
        buf[i] = d < 10 ? '0' + d : 'a' + d - 10;
        v >>= 4;
    }
    buf[18] = 0;
    _puts(buf);
}

static void _putdec(int v) {
    char buf[32];
    int i = 0;
    unsigned int x;
    if (v < 0) {
        buf[i++] = '-';
        x = (unsigned int)(-v);
    } else {
        x = (unsigned int)v;
    }
    char digits[16];
    int n = 0;
    do {
        digits[n++] = '0' + (x % 10);
        x /= 10;
    } while (x);
    while (n--)
        buf[i++] = digits[n];
    buf[i] = 0;
    _puts(buf);
}

// ─── Microsecond timing via gettimeofday syscall ─────────────
struct _timeval { long tv_sec; long tv_usec; };

#define sys_gettimeofday(tv) _syscall(116, (long)(tv), 0, 0, 0, 0, 0)

static uint64_t _now_us() {
    struct _timeval tv;
    sys_gettimeofday(&tv);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static uint64_t sStartupT0 = 0;  // set at very beginning of start()
static bool sDyldVerbose = false; // set from DYLD_PRINT_LIBRARIES env var
static bool sPantheraDyldPhaseTrace = false; // set from PANTHERA_DYLD_PHASE_TRACE env var
static bool sPantheraDyldTrace = false; // set from PANTHERA_DYLD_TRACE env var
static bool sPantheraDyldResolveTrace = false; // set from PANTHERA_DYLD_RESOLVE_TRACE env var
static bool sPantheraDyldTraceBinds = false; // set from PANTHERA_DYLD_TRACE_BINDS env var
static bool sPantheraDyldProgressTrace = false; // set from PANTHERA_DYLD_PROGRESS_TRACE env var
static bool sPantheraDyldKernelSharedRegion = false; // opt-in until shared-region DATA COW is complete

static void _phase(const char* msg) {
    if (!sDyldVerbose) {
        return;
    }
    _puts("dyld: ");
    _puts(msg ? msg : "<null>");
    _puts("\n");
}

static void _panthera_phase(const char* msg) {
    if (!sPantheraDyldPhaseTrace && !sPantheraDyldTrace) {
        return;
    }
    _puts("PANTHERA:dyld ");
    _puts(msg ? msg : "<null>");
    _puts("\n");
}

static void _panthera_phase_path(const char* msg, const char* path) {
    if (!sPantheraDyldPhaseTrace && !sPantheraDyldTrace) {
        return;
    }
    _puts("PANTHERA:dyld ");
    _puts(msg ? msg : "<null>");
    _puts(" ");
    _puts(path ? path : "<null>");
    _puts("\n");
}

static void _panthera_progress(const char* msg) {
    if (!sPantheraDyldProgressTrace) {
        return;
    }
    _puts("PANTHERA:dyld progress ");
    _puts(msg ? msg : "<null>");
    _puts("\n");
}

static void _panthera_progress_path(const char* msg, const char* path) {
    if (!sPantheraDyldProgressTrace) {
        return;
    }
    _puts("PANTHERA:dyld progress ");
    _puts(msg ? msg : "<null>");
    _puts(" ");
    _puts(path ? path : "<null>");
    _puts("\n");
}

static bool _panthera_trace_chained_symbol(const char* name) {
    if ((!sPantheraDyldTrace && !sPantheraDyldResolveTrace) || !name) {
        return false;
    }
    return _strcmp(name, "__DefaultRuneLocale") == 0 ||
           _strcmp(name, "____chkstk_darwin") == 0 ||
           _strcmp(name, "_OPENSSL_init_crypto") == 0 ||
           _strcmp(name, "_OpenSSL_version") == 0 ||
           _strcmp(name, "_OpenSSL_version_num") == 0 ||
           _strcmp(name, "_arc4random_stir") == 0 ||
           _strcmp(name, "_RAND_bytes") == 0 ||
           _strcmp(name, "_RAND_poll") == 0 ||
           _strcmp(name, "_RAND_seed") == 0 ||
           _strcmp(name, "_RAND_status") == 0;
}

static bool _panthera_trace_resolve_symbol(const char* name) {
    return (sPantheraDyldTrace || sPantheraDyldResolveTrace) && name &&
        (_strcmp(name, "____chkstk_darwin") == 0 ||
         _strcmp(name, "__DefaultRuneLocale") == 0 ||
         _strcmp(name, "_OPENSSL_init_crypto") == 0 ||
         _strcmp(name, "_RAND_status") == 0);
}

static bool _envHas(const char** envp, const char* name) {
    if (!envp || !name) return false;
    size_t nlen = _strlen(name);
    for (const char** e = envp; *e; e++) {
        if (_strncmp(*e, name, nlen) == 0 && (*e)[nlen] == '=')
            return true;
    }
    return false;
}

static bool _envEquals(const char** envp, const char* name, const char* value) {
    if (!envp || !name || !value) return false;
    size_t nlen = _strlen(name);
    for (const char** e = envp; *e; e++) {
        if (_strncmp(*e, name, nlen) == 0 && (*e)[nlen] == '=') {
            return _strcmp(*e + nlen + 1, value) == 0;
        }
    }
    return false;
}

// Format timing into a single buffer and write once to minimize serial overhead
static void _trace_time(const char* label) {
    char buf[128];
    int pos = 0;
    uint64_t delta = _now_us() - sStartupT0;
    uint64_t ms = delta / 1000;
    uint64_t us_frac = delta % 1000;

    // "dyld[T+"
    buf[pos++]='d';buf[pos++]='y';buf[pos++]='l';buf[pos++]='d';
    buf[pos++]='[';buf[pos++]='T';buf[pos++]='+';

    // ms number
    char digits[16]; int nd = 0;
    uint64_t v = ms;
    do { digits[nd++] = '0' + (v % 10); v /= 10; } while (v);
    while (nd > 0) buf[pos++] = digits[--nd];
    buf[pos++] = '.';

    // us_frac 3-digit
    buf[pos++] = '0' + (us_frac / 100) % 10;
    buf[pos++] = '0' + (us_frac / 10) % 10;
    buf[pos++] = '0' + us_frac % 10;
    buf[pos++]='m';buf[pos++]='s';buf[pos++]=']';buf[pos++]=' ';

    // label
    for (const char* p = label; *p && pos < 120; ) buf[pos++] = *p++;
    buf[pos++] = '\n';
    sys_write(2, buf, pos);
}

// Set to 1 for verbose per-image traces (adds ~1s of serial I/O overhead)
#define DYLD_VERBOSE_TRACE 0

static void _trace_ctor_event(const char* phase, const char* imagePath) {
#if DYLD_VERBOSE_TRACE
    _trace_time_path(phase, imagePath);
#else
    if (sDyldVerbose) {
        _puts("dyld: ctor ");
        _puts(phase ? phase : "<null>");
        _puts(" ");
        _puts(imagePath ? imagePath : "<null>");
        _puts("\n");
    }
#endif
}

static void _trace_image_phase(const char* phase, const char* imagePath) {
#if DYLD_VERBOSE_TRACE
    _trace_time_path(phase, imagePath);
#else
    if (sDyldVerbose || sPantheraDyldTrace || sPantheraDyldPhaseTrace) {
        if (!_contains(phase, "main dependency") &&
            !_contains(phase, "load") &&
            !_contains(phase, "cache") &&
            !_contains(phase, "dependencies") &&
            !_contains(phase, "dependency") &&
            !_contains(phase, "fixups") &&
            !_contains(imagePath, "libsandbox") &&
            !_contains(imagePath, "libcrypto") &&
            !_contains(imagePath, "libssl") &&
            !_contains(imagePath, "libz") &&
            !_contains(imagePath, "libresolv") &&
            !_contains(imagePath, "sshd")) {
            return;
        }
        _puts((sPantheraDyldTrace || sPantheraDyldPhaseTrace) ?
            "PANTHERA:dyld image " : "dyld: image ");
        _puts(phase ? phase : "<null>");
        _puts(" ");
        _puts(imagePath ? imagePath : "<null>");
        _puts("\n");
    }
#endif
}

} // extern "C"

// ─── Shared cache support ────────────────────────────────────
// Supports two formats:
//   1. Phase A (flat): mappingCount==0, verbatim dylibs, copy+fixup at runtime
//   2. Real split-region: mappingCount==3, TEXT/DATA/LINKEDIT at Apple addresses,
//      pre-resolved fixups, slide info v2
#define CACHE_PATH_PRIMARY  "/System/Library/dyld/dyld_shared_cache_x86_64"
#define CACHE_PATH_FALLBACK "/var/db/dyld/dyld_shared_cache_x86_64"

struct PantheraCacheImageInfo {
    uint64_t address;
    uint64_t modTime;
    uint64_t inode;
    uint32_t pathFileOffset;
    uint32_t pad;
};

struct PantheraCacheMappingInfo {
    uint64_t address;
    uint64_t size;
    uint64_t fileOffset;
    uint32_t maxProt;
    uint32_t initProt;
};

struct PantheraCacheMappingAndSlideInfo {
    uint64_t address;
    uint64_t size;
    uint64_t fileOffset;
    uint64_t slideInfoFileOffset;
    uint64_t slideInfoFileSize;
    uint64_t flags;
    uint32_t maxProt;
    uint32_t initProt;
};

struct PantheraCacheHeader {
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

struct PantheraSharedFileMappingSlideNP {
    uint64_t sms_address;
    uint64_t sms_size;
    uint64_t sms_file_offset;
    uint64_t sms_slide_size;
    uint64_t sms_slide_start;
    int32_t  sms_max_prot;
    int32_t  sms_init_prot;
};

struct PantheraSharedFileNP {
    int32_t  sf_fd;
    uint32_t sf_mappings_count;
    uint32_t sf_slide;
};

#ifndef VM_PROT_COW
#define VM_PROT_COW   0x08
#endif
#ifndef VM_PROT_SLIDE
#define VM_PROT_SLIDE 0x20
#endif
#define sys_shared_region_check_np(startAddr) \
    _syscall(294, (long)(startAddr), 0, 0, 0, 0, 0)
#define sys_shared_region_map_and_slide_2_np(filesCount, files, mappingsCount, mappings) \
    _syscall(536, (filesCount), (long)(files), (mappingsCount), (long)(mappings), 0, 0)

static bool                         sCacheValid = false;
static bool                         sCacheSplitRegion = false;
static bool                         sCachePreResolved = false;
static uint8_t                     *sCacheMap = nullptr;
static uint64_t                     sCacheFileSize = 0;
static uint64_t                     sCacheBase = 0;
static intptr_t                     sCacheSlide = 0;
static const PantheraCacheImageInfo*sCacheImages = nullptr;
static uint32_t                     sCacheImageCount = 0;

// For split-region: pointers to mapped regions
static uint8_t                     *sCacheTextBase = nullptr;
static uint8_t                     *sCacheDataBase = nullptr;
static uint8_t                     *sCacheLinkBase = nullptr;
static uint64_t                     sCacheTextAddr = 0;
static uint64_t                     sCacheDataAddr = 0;
static uint64_t                     sCacheLinkAddr = 0;
static uint64_t                     sCacheTextSize = 0;
static uint64_t                     sCacheDataSize = 0;
static uint64_t                     sCacheLinkSize = 0;
static uint64_t                     sCacheLinkFileOffset = 0;

// Slide info v2
#define SLIDE_PAGE_NO_REBASE 0x4000
#define SLIDE_PAGE_EXTRA     0x8000
#define SLIDE_PAGE_END       0x8000
#define SLIDE_PAGE_VALUE     0x3FFF
#define SLIDE_PAGE_OFFSET_SHIFT 2

static void applySlidInfoV2(const uint8_t* slideInfo, uint8_t* dataRegion, intptr_t slide) {
    uint32_t version = *(const uint32_t*)slideInfo;
    if (version != 2) return;

    uint32_t pageSize = *(const uint32_t*)(slideInfo + 4);
    uint32_t startsOff = *(const uint32_t*)(slideInfo + 8);
    uint32_t startsCnt = *(const uint32_t*)(slideInfo + 12);
    uint32_t extrasOff = *(const uint32_t*)(slideInfo + 16);
    uint32_t extrasCnt = *(const uint32_t*)(slideInfo + 20);
    uint64_t deltaMask = *(const uint64_t*)(slideInfo + 24);
    uint64_t valueAdd  = *(const uint64_t*)(slideInfo + 32);

    uint64_t valueMask = ~deltaMask;
    int deltaShift = 0;
    { uint64_t m = deltaMask; while (m && !(m & 1)) { deltaShift++; m >>= 1; } }
    deltaShift -= 2;

    const uint16_t* pageStarts = (const uint16_t*)(slideInfo + startsOff);
    const uint16_t* pageExtras = (const uint16_t*)(slideInfo + extrasOff);

    for (uint32_t p = 0; p < startsCnt; p++) {
        uint16_t startOff = pageStarts[p];
        if (startOff == SLIDE_PAGE_NO_REBASE)
            continue;

        uint8_t* pageBase = dataRegion + (uint64_t)p * pageSize;
        if (startOff & SLIDE_PAGE_EXTRA) {
            uint32_t chainIdx = startOff & SLIDE_PAGE_VALUE;
            while (chainIdx < extrasCnt) {
                uint16_t chainEntry = pageExtras[chainIdx++];
                uint32_t pageOffset = (chainEntry & SLIDE_PAGE_VALUE) << SLIDE_PAGE_OFFSET_SHIFT;
                uint32_t delta = 1;
                while (delta != 0) {
                    uint8_t* loc = pageBase + pageOffset;
                    uint64_t rawValue = *(uint64_t*)loc;
                    delta = (uint32_t)((rawValue & deltaMask) >> deltaShift);
                    uint64_t newValue = rawValue & valueMask;
                    if (newValue != 0) {
                        newValue += valueAdd;
                        newValue += slide;
                    }
                    *(uint64_t*)loc = newValue;
                    pageOffset += delta;
                }
                if (chainEntry & SLIDE_PAGE_END)
                    break;
            }
            continue;
        }

        uint32_t pageOffset = (startOff & SLIDE_PAGE_VALUE) << SLIDE_PAGE_OFFSET_SHIFT;
        uint32_t delta = 1;
        while (delta != 0) {
            uint8_t* loc = pageBase + pageOffset;
            uint64_t rawValue = *(uint64_t*)loc;
            delta = (uint32_t)((rawValue & deltaMask) >> deltaShift);
            uint64_t newValue = rawValue & valueMask;
            if (newValue != 0) {
                newValue += valueAdd;
                newValue += slide;
            }
            *(uint64_t*)loc = newValue;
            pageOffset += delta;
        }
    }
}

static bool mapSplitCacheWithKernel(int fd,
                                    const PantheraCacheMappingInfo* maps,
                                    const PantheraCacheMappingAndSlideInfo* smaps,
                                    uint32_t mappingCnt,
                                    uint64_t maxSlide) {
    if (!maps || mappingCnt < 3) {
        return false;
    }

    uint64_t mappedStart = 0;
    long checkRet = sys_shared_region_check_np(&mappedStart);
    if (checkRet == 0) {
        intptr_t slide = (intptr_t)(mappedStart - maps[0].address);
        const mach_header_64* firstMH = (const mach_header_64*)(maps[0].address + slide);
        if (firstMH->magic == MH_MAGIC_64) {
            sCacheTextBase = (uint8_t*)firstMH;
            sCacheDataBase = (uint8_t*)(maps[1].address + slide);
            sCacheLinkBase = (uint8_t*)(maps[2].address + slide);
            sCacheSlide = slide;
            return true;
        }
        sys_shared_region_check_np(0);
    }

    PantheraSharedFileNP sharedFile = {};
    sharedFile.sf_fd = fd;
    sharedFile.sf_mappings_count = 3;
    sharedFile.sf_slide = (uint32_t)maxSlide;

    PantheraSharedFileMappingSlideNP sharedMaps[3] = {};
    for (uint32_t i = 0; i < 3; i++) {
        sharedMaps[i].sms_address = maps[i].address;
        sharedMaps[i].sms_size = maps[i].size;
        sharedMaps[i].sms_file_offset = maps[i].fileOffset;
        sharedMaps[i].sms_slide_size = 0;
        sharedMaps[i].sms_slide_start = 0;
        sharedMaps[i].sms_max_prot = (int32_t)maps[i].maxProt;
        sharedMaps[i].sms_init_prot = (int32_t)maps[i].initProt;
    }
    if (smaps && smaps[1].slideInfoFileOffset != 0 && smaps[1].slideInfoFileSize != 0) {
        sharedMaps[1].sms_slide_size = smaps[1].slideInfoFileSize;
        sharedMaps[1].sms_slide_start = (uint64_t)(uintptr_t)(sCacheMap + smaps[1].slideInfoFileOffset);
        sharedMaps[1].sms_max_prot |= VM_PROT_COW | VM_PROT_SLIDE;
    }

    long mapRet = sys_shared_region_map_and_slide_2_np(1, &sharedFile, 3, sharedMaps);
    if (mapRet != 0) {
        return false;
    }

    mappedStart = 0;
    if (sys_shared_region_check_np(&mappedStart) != 0) {
        sys_shared_region_check_np(0);
        return false;
    }

    sCacheSlide = (intptr_t)(mappedStart - maps[0].address);
    sCacheTextBase = (uint8_t*)(maps[0].address + sCacheSlide);
    sCacheDataBase = (uint8_t*)(maps[1].address + sCacheSlide);
    sCacheLinkBase = (uint8_t*)(maps[2].address + sCacheSlide);
    return (((mach_header_64*)sCacheTextBase)->magic == MH_MAGIC_64);
}

static bool mapSplitCacheWithMmap(int fd,
                                  const PantheraCacheMappingInfo* maps,
                                  const PantheraCacheMappingAndSlideInfo* smaps) {
    void* textMap = (void*)sys_mmap((void*)maps[0].address, maps[0].size,
        PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_FIXED, fd, maps[0].fileOffset);
    void* dataMap = (void*)sys_mmap((void*)maps[1].address, maps[1].size,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, maps[1].fileOffset);
    void* linkMap = (void*)sys_mmap((void*)maps[2].address, maps[2].size,
        PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, maps[2].fileOffset);

    if ((long)textMap <= 0 || (long)dataMap <= 0 || (long)linkMap <= 0) {
        if ((long)textMap > 0) sys_munmap(textMap, maps[0].size);
        if ((long)dataMap > 0) sys_munmap(dataMap, maps[1].size);
        if ((long)linkMap > 0) sys_munmap(linkMap, maps[2].size);
        return false;
    }

    sCacheTextBase = (uint8_t*)textMap;
    sCacheDataBase = (uint8_t*)dataMap;
    sCacheLinkBase = (uint8_t*)linkMap;
    sCacheSlide = 0;

    // The mmap fallback maps DATA directly from the cache file, so pointers are still
    // in slide-info encoded form and must be decoded even when the cache slide is 0.
    // The kernel shared-region syscall performs this decode itself, so only do it here.
    if (smaps && smaps[1].slideInfoFileSize > 0 && smaps[1].slideInfoFileOffset > 0) {
        const uint8_t* slideInfo = sCacheMap + smaps[1].slideInfoFileOffset;
        applySlidInfoV2(slideInfo, sCacheDataBase, sCacheSlide);
    }
    return true;
}

static int openSharedCacheFile(const char** pathUsed) {
    int fd = (int)sys_open(CACHE_PATH_PRIMARY, O_RDONLY, 0);
    if (fd >= 0) {
        if (pathUsed) *pathUsed = CACHE_PATH_PRIMARY;
        return fd;
    }

    fd = (int)sys_open(CACHE_PATH_FALLBACK, O_RDONLY, 0);
    if (fd >= 0 && pathUsed) {
        *pathUsed = CACHE_PATH_FALLBACK;
    }
    return fd;
}

static void initSharedCache() {
    const char* cachePath = nullptr;
    int fd = openSharedCacheFile(&cachePath);
    if (fd < 0) return;

    struct stat st;
    sys_fstat(fd, &st);
    sCacheFileSize = st.st_size;

    // First, mmap the entire file to read the header
    void *mapped = (void*)sys_mmap(0, sCacheFileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    if ((long)mapped <= 0) { sys_close(fd); return; }

    const uint8_t *buf = (const uint8_t *)mapped;
    if (buf[0]!='d'||buf[1]!='y'||buf[2]!='l'||buf[3]!='d'||
        buf[4]!='_'||buf[5]!='v'||buf[6]!='1'||buf[7]!=' ') {
        sys_munmap(mapped, sCacheFileSize);
        sys_close(fd);
        return;
    }

    if (sCacheFileSize < 232) {
        sys_munmap(mapped, sCacheFileSize);
        sys_close(fd);
        return;
    }

    uint32_t mappingOff = *(const uint32_t*)(buf + 16);
    uint32_t mappingCnt = *(const uint32_t*)(buf + 20);
    uint32_t imagesOff  = *(const uint32_t*)(buf + 24);
    uint32_t imagesCnt  = *(const uint32_t*)(buf + 28);
    sCacheBase = *(const uint64_t*)(buf + 224);

    const PantheraCacheHeader* hdr = nullptr;
    if (sCacheFileSize >= sizeof(PantheraCacheHeader)) {
        hdr = (const PantheraCacheHeader*)buf;
        if (hdr->imagesOffset != 0) {
            imagesOff = hdr->imagesOffset;
            imagesCnt = hdr->imagesCount;
        }
        sCacheBase = hdr->sharedRegionStart;
    }

    if ((uint64_t)imagesOff +
        (uint64_t)imagesCnt * sizeof(PantheraCacheImageInfo) > sCacheFileSize) {
        sys_munmap(mapped, sCacheFileSize);
        sys_close(fd);
        return;
    }

    if (mappingCnt >= 3 && mappingOff > 0 &&
        mappingOff + 3 * sizeof(PantheraCacheMappingInfo) <= sCacheFileSize) {
        // Real split-region cache format
        sCacheSplitRegion = true;
        sCachePreResolved = true;

        const PantheraCacheMappingInfo* maps =
            (const PantheraCacheMappingInfo*)(buf + mappingOff);

        sCacheTextAddr = maps[0].address;
        sCacheTextSize = maps[0].size;
        sCacheDataAddr = maps[1].address;
        sCacheDataSize = maps[1].size;
        sCacheLinkAddr = maps[2].address;
        sCacheLinkSize = maps[2].size;
        sCacheLinkFileOffset = maps[2].fileOffset;

        const PantheraCacheMappingAndSlideInfo* smaps = nullptr;
        uint32_t slideMapOff = 0;
        uint32_t slideMapCnt = 0;
        if (hdr) {
            slideMapOff = hdr->mappingWithSlideOffset;
            slideMapCnt = hdr->mappingWithSlideCount;
        }
        if (slideMapCnt >= 3 &&
            slideMapOff + 3 * sizeof(PantheraCacheMappingAndSlideInfo) <= sCacheFileSize) {
            smaps = (const PantheraCacheMappingAndSlideInfo*)(buf + slideMapOff);
        }

        sCacheMap = (uint8_t*)mapped;
        sCacheImages = (const PantheraCacheImageInfo*)(buf + imagesOff);
        sCacheImageCount = imagesCnt;

        bool mapOk = false;
        bool usedKernelSharedRegion = false;
        if (sPantheraDyldKernelSharedRegion) {
            mapOk = mapSplitCacheWithKernel(fd, maps, smaps, mappingCnt,
                                            hdr ? hdr->maxSlide : 0);
            usedKernelSharedRegion = mapOk;
        }
        if (!mapOk) {
            mapOk = mapSplitCacheWithMmap(fd, maps, smaps);
        }
        if (!mapOk) {
            _puts("dyld: split-region cache map failed, ignoring cache\n");
            sCacheValid = false;
            sCacheSplitRegion = false;
            sCachePreResolved = false;
            sCacheMap = nullptr;
            sCacheImages = nullptr;
            sCacheImageCount = 0;
            sys_munmap(mapped, sCacheFileSize);
            sys_close(fd);
            return;
        } else {
            sCacheValid = true;

            sys_close(fd);
            if (sDyldVerbose) {
                _puts(usedKernelSharedRegion ?
                      "dyld: cache OK (shared_region, " :
                      "dyld: cache OK (mmap, ");
                _putdec(sCacheImageCount);
                _puts(" images)\n");
            }
            return;
        }
    }

    // Phase A: flat mmap (mappingCount == 0)
    sys_close(fd);
    sCacheSplitRegion = false;
    sCachePreResolved = false;
    sCacheMap = (uint8_t*)mapped;
    sCacheImages = (const PantheraCacheImageInfo*)(buf + imagesOff);
    sCacheImageCount = imagesCnt;
    sCacheValid = true;

    if (sDyldVerbose) {
        _puts("dyld: cache OK (flat, ");
        _putdec(sCacheImageCount);
        _puts(" images)\n");
    }
}

// Find a dylib in the cache.
// For split-region: returns pointer to mach_header in mapped TEXT region.
// For flat (Phase A): returns pointer to verbatim dylib data.
static const void* findDylibInCache(const char* path, size_t* sizeOut) {
    if (!sCacheValid) return nullptr;

    for (uint32_t i = 0; i < sCacheImageCount; i++) {
        const char *imgPath = (const char *)(sCacheMap + sCacheImages[i].pathFileOffset);
        if (_strcmp(imgPath, path) == 0 ||
            _strcmp(_basename(imgPath), _basename(path)) == 0) {

            if (sCacheSplitRegion) {
                // Image address is a VM address in the TEXT region
                uint64_t imgAddr = sCacheImages[i].address;
                if (imgAddr < sCacheTextAddr ||
                    imgAddr >= sCacheTextAddr + sCacheTextSize)
                    continue;
                const void* mh = (const void*)(imgAddr + sCacheSlide);
                // Size: distance to next image's TEXT or end of TEXT region
                uint64_t nextAddr = sCacheTextAddr + sCacheTextSize;
                if (i + 1 < sCacheImageCount) {
                    uint64_t na = sCacheImages[i + 1].address;
                    if (na > imgAddr && na <= sCacheTextAddr + sCacheTextSize)
                        nextAddr = na;
                }
                if (sizeOut) *sizeOut = (size_t)(nextAddr - imgAddr);
                return mh;
            } else {
                // Phase A: flat layout
                uint64_t fileOff = sCacheImages[i].address - sCacheBase;
                if (fileOff >= sCacheFileSize) continue;
                uint64_t nextOff = sCacheFileSize;
                if (i + 1 < sCacheImageCount) {
                    uint64_t n = sCacheImages[i + 1].address - sCacheBase;
                    if (n > fileOff && n <= sCacheFileSize) nextOff = n;
                }
                if (sizeOut) *sizeOut = (size_t)(nextOff - fileOff);
                return sCacheMap + fileOff;
            }
        }
    }
    return nullptr;
}

// ─── Image tracking ───────────────────────────────────────────
#define MAX_IMAGES 64

struct LoadedImage {
    const mach_header_64* header;
    intptr_t              slide;
    const char*           path;
    // Symbol table
    const nlist_64*       symtab;
    const char*           strtab;
    uint32_t              nsyms;
    uint32_t              strsize;
    // Indirect symbol table
    const uint32_t*       indirectSymtab;
    uint32_t              nindirectsyms;
};

#define PANTHERA_DYLD_BRIDGE_MAX_IMAGES 64
struct PantheraDyldBridgeImage {
    const mach_header* header;
    long               slide;
    const char*        path;
    unsigned char      has_objc;
};

struct PantheraDyldBridgeState {
    unsigned int             image_count;
    unsigned long            shared_cache_base;
    unsigned long            shared_cache_size;
    const mach_header*       prog_image_header;
    PantheraDyldBridgeImage  images[PANTHERA_DYLD_BRIDGE_MAX_IMAGES];
};

static LoadedImage sImages[MAX_IMAGES];
static int sNumImages = 0;
static char sConsoleTTYName[] = "/dev/console";

static int loadedImageIndex(const LoadedImage& img) {
    int imageCount = sNumImages;
    if (imageCount < 0) {
        return -1;
    }
    if (imageCount > MAX_IMAGES) {
        imageCount = MAX_IMAGES;
    }
    for (int i = 0; i < imageCount; i++) {
        if (&sImages[i] == &img) {
            return i;
        }
    }
    return -1;
}

static bool isImageFromCache(const LoadedImage* img) {
    if (!sCachePreResolved || !img || !img->header) return false;
    uintptr_t addr = (uintptr_t)img->header;
    // Check if the image header is within the cache TEXT region
    if (sCacheSplitRegion) {
        return (addr >= (uintptr_t)sCacheTextBase &&
                addr < (uintptr_t)sCacheTextBase + sCacheTextSize);
    }
    return false;
}


static LoadedImage* findLoadedImageByPath(const char* path) {
    if (!path) {
        return nullptr;
    }
    for (int i = 0; i < sNumImages; i++) {
        if (sImages[i].path && _strcmp(sImages[i].path, path) == 0) {
            return &sImages[i];
        }
    }
    const char* wantBase = _basename(path);
    for (int i = 0; i < sNumImages; i++) {
        if (sImages[i].path && _strcmp(_basename(sImages[i].path), wantBase) == 0) {
            return &sImages[i];
        }
    }
    return nullptr;
}

static LoadedImage* findLoadedImageByPathBounded(const char* path, size_t maxLen) {
    if (!path || maxLen == 0) {
        return nullptr;
    }

    for (int i = 0; i < sNumImages; i++) {
        if (sImages[i].path && _streq_bounded(sImages[i].path, path, maxLen)) {
            return &sImages[i];
        }
    }

    return nullptr;
}

static char* panthera_ttyname_shim(int fd) {
    (void)fd;
    return sConsoleTTYName;
}

static int panthera_ttyname_r_shim(int fd, char* buf, size_t len) {
    (void)fd;
    const size_t need = sizeof("/dev/console");
    if (!buf || len < need) {
        return 34; // ERANGE
    }
    _memcpy(buf, sConsoleTTYName, need);
    return 0;
}


struct ProgramVars {
    void*   mh;
    int*    NXArgcPtr;
    char*** NXArgvPtr;
    char*** environPtr;
    char**  __prognamePtr;
};

static int        sArgcStorage = 0;
static char**     sArgvStorage = nullptr;
static char**     sEnvpStorage = nullptr;
static char*      sPrognameStorage = nullptr;
static ProgramVars sProgramVars = {};

static uint64_t resolveSymbol(const char* name, int ordinal = -1, bool includeLocals = false,
                              const LoadedImage* importer = nullptr);

static void panthera_seed_libkernel_state() {
    const uint8_t* commpage = (const uint8_t*)0x00007fffffe00000ULL;
    const uint16_t version = *(const uint16_t*)(commpage + 0x01E);

    uint32_t kernelPageShift = 12;
    uint32_t userPageShift = 12;
    if (version >= 14) {
        kernelPageShift = commpage[0x04D];
        userPageShift = commpage[0x04E];
    } else {
        userPageShift = kernelPageShift;
    }

    const uint32_t kernelPageSize = 1u << kernelPageShift;
    const uint32_t userPageSize = 1u << userPageShift;

    {
        uint64_t taskSelfTrapAddr = resolveSymbol("_task_self_trap", -1, false);
        uint64_t taskSelfGlobalAddr = resolveSymbol("_mach_task_self_", -1, false);
        if (taskSelfTrapAddr && taskSelfGlobalAddr) {
            typedef uint32_t (*TaskSelfTrapFunc)(void);
            *(uint32_t*)taskSelfGlobalAddr = ((TaskSelfTrapFunc)taskSelfTrapAddr)();
        }
    }
    {
        uint64_t replyPortAddr = resolveSymbol("_mach_reply_port", -1, false);
        uint64_t replyPortGlobalAddr = resolveSymbol("__task_reply_port", -1, true);
        if (replyPortAddr && replyPortGlobalAddr) {
            typedef uint32_t (*ReplyPortFunc)(void);
            *(uint32_t*)replyPortGlobalAddr = ((ReplyPortFunc)replyPortAddr)();
        }
    }
    {
        uint64_t taskSelfTrapAddr = resolveSymbol("_task_self_trap", -1, false);
        uint64_t taskGetSpecialPortAddr = resolveSymbol("_task_get_special_port", -1, false);
        uint64_t bootstrapPortGlobalAddr = resolveSymbol("_bootstrap_port", -1, false);
        if (taskSelfTrapAddr && taskGetSpecialPortAddr && bootstrapPortGlobalAddr) {
            typedef uint32_t (*TaskSelfTrapFunc)(void);
            typedef int (*TaskGetSpecialPortFunc)(uint32_t, int, uint32_t*);
            uint32_t bp = 0;
            uint32_t self = ((TaskSelfTrapFunc)taskSelfTrapAddr)();
            (void)((TaskGetSpecialPortFunc)taskGetSpecialPortAddr)(self, 4, &bp);
            *(uint32_t*)bootstrapPortGlobalAddr = bp;
        }
    }
    {
        uint64_t addr = resolveSymbol("_vm_kernel_page_shift", -1, false);
        if (addr) *(uint32_t*)addr = kernelPageShift;
    }
    {
        uint64_t addr = resolveSymbol("_vm_kernel_page_size", -1, false);
        if (addr) *(uint32_t*)addr = kernelPageSize;
    }
    {
        uint64_t addr = resolveSymbol("_vm_kernel_page_mask", -1, false);
        if (addr) *(uint32_t*)addr = kernelPageSize - 1;
    }
    {
        uint64_t addr = resolveSymbol("_vm_page_shift", -1, false);
        if (addr) *(uint32_t*)addr = userPageShift;
    }
    {
        uint64_t addr = resolveSymbol("_vm_page_size", -1, false);
        if (addr) *(uint32_t*)addr = userPageSize;
    }
    {
        uint64_t addr = resolveSymbol("_vm_page_mask", -1, false);
        if (addr) *(uint32_t*)addr = userPageSize - 1;
    }

}

// ─── ULEB/SLEB128 ────────────────────────────────────────────
static uint64_t readULEB128(const uint8_t*& p) {
    uint64_t result = 0;
    int shift = 0;
    do {
        result |= (uint64_t)(*p & 0x7f) << shift;
        shift += 7;
    } while (*p++ & 0x80);
    return result;
}

static int64_t readSLEB128(const uint8_t*& p) {
    int64_t result = 0;
    int shift = 0;
    uint8_t byte;
    do {
        byte = *p++;
        result |= (int64_t)(byte & 0x7f) << shift;
        shift += 7;
    } while (byte & 0x80);
    if ((shift < 64) && (byte & 0x40))
        result |= -(1LL << shift);
    return result;
}

// ─── Find segment by name ────────────────────────────────────
static const segment_command_64* findSegment(const mach_header_64* mh, const char* name) {
    const uint8_t* cmd = (const uint8_t*)mh + sizeof(mach_header_64);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const segment_command_64* seg = (const segment_command_64*)cmd;
            if (_strcmp(seg->segname, name) == 0)
                return seg;
        }
        cmd += lc->cmdsize;
    }
    return nullptr;
}

static const segment_command_64* findSegmentByImageOffset(const LoadedImage& img,
                                                          uint64_t imageOffset) {
    const segment_command_64* textSeg = findSegment(img.header, SEG_TEXT);
    if (!textSeg) {
        return nullptr;
    }

    const uint8_t* cmd = (const uint8_t*)img.header + sizeof(mach_header_64);
    const uint8_t* cmdsEnd = cmd + img.header->sizeofcmds;
    for (uint32_t i = 0; i < img.header->ncmds; i++) {
        if (cmd + sizeof(load_command) > cmdsEnd) {
            break;
        }
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmdsize < sizeof(load_command) || cmd + lc->cmdsize > cmdsEnd) {
            break;
        }
        if (lc->cmd == LC_SEGMENT_64) {
            const segment_command_64* seg = (const segment_command_64*)cmd;
            if (seg->vmaddr >= textSeg->vmaddr &&
                seg->vmaddr - textSeg->vmaddr == imageOffset) {
                return seg;
            }
        }
        cmd += lc->cmdsize;
    }
    return nullptr;
}

static uintptr_t pageRoundDown(uintptr_t value) {
    return value & ~(uintptr_t)0xfff;
}

static uintptr_t pageRoundUp(uintptr_t value) {
    return (value + 0xfff) & ~(uintptr_t)0xfff;
}

static uintptr_t linkeditBaseForImage(const LoadedImage& img,
                                      const segment_command_64* textSeg,
                                      const segment_command_64* linkSeg) {
    if (!textSeg || !linkSeg) {
        return 0;
    }
    if (isImageFromCache(&img) && sCacheSplitRegion) {
        return (uintptr_t)sCacheLinkBase - sCacheLinkFileOffset;
    }
    return (uintptr_t)img.header + (linkSeg->vmaddr - textSeg->vmaddr) - linkSeg->fileoff;
}

// ─── Symbol lookup ───────────────────────────────────────────
static LoadedImage* loadDependencyImage(const char* depPath);
static LoadedImage* findOrdinalImage(const LoadedImage* importer, int ordinal);

#ifndef LC_DYLD_EXPORTS_TRIE
#define LC_DYLD_EXPORTS_TRIE 0x80000033
#endif
#define EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION 0x04
#define EXPORT_SYMBOL_FLAGS_REEXPORT        0x08

static bool readULEB128Bounded(const uint8_t*& p, const uint8_t* end, uint64_t* out) {
    uint64_t result = 0;
    int shift = 0;
    while (p < end) {
        uint8_t byte = *p++;
        result |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            *out = result;
            return true;
        }
        shift += 7;
        if (shift >= 64) {
            break;
        }
    }
    return false;
}

static const uint8_t* exportTrieForImage(const LoadedImage& img, uint32_t* sizeOut) {
    const segment_command_64* textSeg = findSegment(img.header, SEG_TEXT);
    const segment_command_64* linkSeg = findSegment(img.header, SEG_LINKEDIT);
    if (!textSeg || !linkSeg) {
        return nullptr;
    }

    uintptr_t linkeditBase = linkeditBaseForImage(img, textSeg, linkSeg);
    if (linkeditBase == 0) {
        return nullptr;
    }

    const uint8_t* cmd = (const uint8_t*)img.header + sizeof(mach_header_64);
    for (uint32_t i = 0; i < img.header->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_DYLD_EXPORTS_TRIE) {
            const linkedit_data_command* ed = (const linkedit_data_command*)cmd;
            if (ed->datasize > 0) {
                if (ed->dataoff < linkSeg->fileoff ||
                    (uint64_t)ed->dataoff + ed->datasize >
                        (uint64_t)linkSeg->fileoff + linkSeg->filesize) {
                    return nullptr;
                }
                if (sizeOut) {
                    *sizeOut = ed->datasize;
                }
                return (const uint8_t*)(linkeditBase + ed->dataoff);
            }
        } else if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
            const dyld_info_command* di = (const dyld_info_command*)cmd;
            if (di->export_size > 0) {
                if (di->export_off < linkSeg->fileoff ||
                    (uint64_t)di->export_off + di->export_size >
                        (uint64_t)linkSeg->fileoff + linkSeg->filesize) {
                    return nullptr;
                }
                if (sizeOut) {
                    *sizeOut = di->export_size;
                }
                return (const uint8_t*)(linkeditBase + di->export_off);
            }
        }
        cmd += lc->cmdsize;
    }
    return nullptr;
}

static uint64_t findExportTrieSymbolAtNode(const LoadedImage& img,
                                           const uint8_t* trie,
                                           const uint8_t* trieEnd,
                                           uint64_t nodeOffset,
                                           const char* originalName,
                                           const char* remainingName,
                                           bool* isWeakOut,
                                           int depth,
                                           int* budget) {
    if (!budget || --(*budget) <= 0 ||
        depth > 32 || nodeOffset >= (uint64_t)(trieEnd - trie)) {
        return 0;
    }

    const uint8_t* p = trie + nodeOffset;
    uint64_t terminalSize = 0;
    if (!readULEB128Bounded(p, trieEnd, &terminalSize)) {
        return 0;
    }
    if (terminalSize > (uint64_t)(trieEnd - p)) {
        return 0;
    }

    const uint8_t* terminalEnd = p + terminalSize;
    if (*remainingName == '\0' && terminalSize > 0) {
        uint64_t flags = 0;
        if (!readULEB128Bounded(p, terminalEnd, &flags)) {
            return 0;
        }
        if (isWeakOut) {
            *isWeakOut = ((flags & EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION) != 0);
        }

        if (flags & EXPORT_SYMBOL_FLAGS_REEXPORT) {
            uint64_t ordinal = 0;
            if (!readULEB128Bounded(p, terminalEnd, &ordinal)) {
                return 0;
            }
            const char* importName = (p < terminalEnd && *p) ? (const char*)p : originalName;
            if (LoadedImage* dep = findOrdinalImage(&img, (int)ordinal)) {
                uint32_t depTrieSize = 0;
                const uint8_t* depTrie = exportTrieForImage(*dep, &depTrieSize);
                if (depTrie && depTrieSize > 0) {
                    return findExportTrieSymbolAtNode(*dep, depTrie, depTrie + depTrieSize,
                        0, importName, importName, isWeakOut, depth + 1, budget);
                }
            }
            return 0;
        }

        uint64_t address = 0;
        if (!readULEB128Bounded(p, terminalEnd, &address)) {
            return 0;
        }
        return (uintptr_t)img.header + address;
    }

    p = terminalEnd;
    if (p >= trieEnd) {
        return 0;
    }
    uint8_t childCount = *p++;
    for (uint8_t child = 0; child < childCount && p < trieEnd; child++) {
        if (--(*budget) <= 0) {
            return 0;
        }
        const char* edge = (const char*)p;
        size_t edgeLen = 0;
        while (p < trieEnd && *p != '\0') {
            if (--(*budget) <= 0) {
                return 0;
            }
            p++;
            edgeLen++;
        }
        if (p >= trieEnd) {
            return 0;
        }
        p++;

        uint64_t childOffset = 0;
        if (!readULEB128Bounded(p, trieEnd, &childOffset)) {
            return 0;
        }
        if (_strncmp(remainingName, edge, edgeLen) == 0) {
            return findExportTrieSymbolAtNode(img, trie, trieEnd, childOffset,
                originalName, remainingName + edgeLen, isWeakOut, depth + 1,
                budget);
        }
    }
    return 0;
}

static uint64_t findExportTrieSymbolInImage(const LoadedImage& img, const char* name,
                                            bool* isWeakOut) {
    uint32_t trieSize = 0;
    const uint8_t* trie = exportTrieForImage(img, &trieSize);
    if (!trie || trieSize == 0) {
        return 0;
    }
    bool traceResolve = _panthera_trace_resolve_symbol(name);
    if (traceResolve) {
        _puts("PANTHERA:dyld trie-enter name=");
        _puts(name);
        _puts(" image=");
        _puts(img.path ? img.path : "<null>");
        _puts(" size=");
        _putdec((int)trieSize);
        _puts("\n");
    }
    if (isWeakOut) {
        *isWeakOut = false;
    }
    int budget = (trieSize > 16000) ? 65536 : (int)(trieSize * 4 + 1024);
    uint64_t addr = findExportTrieSymbolAtNode(img, trie, trie + trieSize, 0,
        name, name, isWeakOut, 0, &budget);
    if (traceResolve) {
        _puts("PANTHERA:dyld trie-exit name=");
        _puts(name);
        _puts(" image=");
        _puts(img.path ? img.path : "<null>");
        _puts(" addr=");
        _puthex(addr);
        _puts(" budget=");
        _putdec(budget);
        _puts("\n");
    }
    return addr;
}

static bool shouldSearchExportTrieForImage(const LoadedImage& img) {
    return img.symtab == nullptr || img.strtab == nullptr;
}

#define SYMBOL_INDEX_CAPACITY 131071

struct SymbolIndexEntry {
    const LoadedImage* img;
    const char* name;
    uint32_t nameLen;
    uint32_t hash;
    uint8_t isWeak;
    uint8_t used;
    uint64_t addr;
};

static SymbolIndexEntry sSymbolIndex[SYMBOL_INDEX_CAPACITY];
static const LoadedImage* sSymbolIndexedImages[MAX_IMAGES];
static int sSymbolIndexedImageCount = 0;
static uint32_t sSymbolIndexUsed = 0;
static bool sSymbolIndexOverflow = false;

static uint32_t symbolHashN(const char* name, uint32_t nameLen) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < nameLen; i++) {
        h ^= (unsigned char)name[i];
        h *= 16777619u;
    }
    return h ? h : 1;
}

static uint32_t symbolHash(const char* name) {
    return symbolHashN(name, (uint32_t)_strlen(name));
}

static bool symbolNameLen(const char* name, uint32_t maxLen, uint32_t* lenOut) {
    if (!name || !lenOut) {
        return false;
    }
    for (uint32_t i = 0; i < maxLen; i++) {
        if (name[i] == '\0') {
            *lenOut = i;
            return true;
        }
    }
    return false;
}

static bool symbolNameEquals(const char* a, uint32_t aLen, const char* b, uint32_t bLen) {
    if (aLen != bLen) {
        return false;
    }
    for (uint32_t i = 0; i < aLen; i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static bool symbolIndexHasImage(const LoadedImage& img) {
    for (int i = 0; i < sSymbolIndexedImageCount; i++) {
        if (sSymbolIndexedImages[i] == &img) {
            return true;
        }
    }
    return false;
}

static bool symbolIndexInsert(const LoadedImage& img, const char* name, uint32_t nameLen,
                              uint64_t addr, bool isWeak) {
    if (sSymbolIndexOverflow || sSymbolIndexUsed >= SYMBOL_INDEX_CAPACITY) {
        sSymbolIndexOverflow = true;
        return false;
    }

    uint32_t h = symbolHashN(name, nameLen);
    uint32_t slot = h % SYMBOL_INDEX_CAPACITY;
    for (uint32_t probe = 0; probe < SYMBOL_INDEX_CAPACITY; probe++) {
        SymbolIndexEntry& entry = sSymbolIndex[slot];
        if (!entry.used) {
            entry.img = &img;
            entry.name = name;
            entry.nameLen = nameLen;
            entry.hash = h;
            entry.isWeak = isWeak ? 1 : 0;
            entry.used = 1;
            entry.addr = addr;
            sSymbolIndexUsed++;
            return true;
        }
        if (entry.img == &img && entry.hash == h &&
            symbolNameEquals(entry.name, entry.nameLen, name, nameLen)) {
            if (entry.isWeak && !isWeak) {
                entry.isWeak = 0;
                entry.addr = addr;
            }
            return true;
        }
        slot++;
        if (slot == SYMBOL_INDEX_CAPACITY) {
            slot = 0;
        }
    }
    sSymbolIndexOverflow = true;
    return false;
}

static void symbolIndexImage(const LoadedImage& img) {
    if (symbolIndexHasImage(img)) {
        return;
    }
    if (sSymbolIndexedImageCount < MAX_IMAGES) {
        sSymbolIndexedImages[sSymbolIndexedImageCount++] = &img;
    }
    if (sSymbolIndexOverflow) {
        return;
    }
    bool traceIndex = sPantheraDyldTrace && img.path &&
        _contains(img.path, "libpanthera_extra");
    if (traceIndex) {
        _puts("PANTHERA:dyld symbol-index begin image=");
        _puts(img.path);
        _puts(" nsyms=");
        _putdec((int)img.nsyms);
        _puts(" strsize=");
        _putdec((int)img.strsize);
        _puts("\n");
    }
    if (!img.symtab || !img.strtab) {
        if (traceIndex) {
            _puts("PANTHERA:dyld symbol-index no-table image=");
            _puts(img.path);
            _puts("\n");
        }
        return;
    }
    for (uint32_t i = 0; i < img.nsyms; i++) {
        const nlist_64& sym = img.symtab[i];
        if (traceIndex && ((i & 0x7f) == 0)) {
            _puts("PANTHERA:dyld symbol-index progress image=");
            _puts(img.path);
            _puts(" i=");
            _putdec((int)i);
            _puts("\n");
        }
        if ((sym.n_type & N_TYPE) == N_UNDF) continue;
        if ((sym.n_type & N_EXT) == 0) continue;
        if (sym.n_un.n_strx == 0 || sym.n_un.n_strx >= img.strsize) continue;
        const char* sname = img.strtab + sym.n_un.n_strx;
        uint32_t snameLen = 0;
        if (!symbolNameLen(sname, img.strsize - sym.n_un.n_strx, &snameLen) || snameLen == 0) {
            continue;
        }
        uint64_t addr = sym.n_value + img.slide;
        bool isWeak = (sym.n_desc & N_WEAK_DEF) != 0;
        if (!symbolIndexInsert(img, sname, snameLen, addr, isWeak)) {
            if (traceIndex) {
                _puts("PANTHERA:dyld symbol-index overflow image=");
                _puts(img.path);
                _puts(" used=");
                _putdec((int)sSymbolIndexUsed);
                _puts("\n");
            }
            break;
        }
    }
    if (traceIndex) {
        _puts("PANTHERA:dyld symbol-index done image=");
        _puts(img.path);
        _puts("\n");
    }
}

static bool symbolIndexLookup(const LoadedImage& img, const char* name,
                              uint64_t* addrOut, bool* isWeakOut) {
    symbolIndexImage(img);

    uint32_t nameLen = (uint32_t)_strlen(name);
    uint32_t h = symbolHashN(name, nameLen);
    uint32_t slot = h % SYMBOL_INDEX_CAPACITY;
    for (uint32_t probe = 0; probe < SYMBOL_INDEX_CAPACITY; probe++) {
        const SymbolIndexEntry& entry = sSymbolIndex[slot];
        if (!entry.used) {
            return false;
        }
        if (entry.img == &img && entry.hash == h &&
            symbolNameEquals(entry.name, entry.nameLen, name, nameLen)) {
            if (addrOut) {
                *addrOut = entry.addr;
            }
            if (isWeakOut) {
                *isWeakOut = entry.isWeak != 0;
            }
            return true;
        }
        slot++;
        if (slot == SYMBOL_INDEX_CAPACITY) {
            slot = 0;
        }
    }
    return false;
}

static uint64_t findSymbolInImage(const LoadedImage& img, const char* name, bool includeLocals = false,
                                  bool* isWeakOut = nullptr) {
    bool traceResolve = _panthera_trace_resolve_symbol(name);
    if (traceResolve) {
        _puts("PANTHERA:dyld find-symbol enter name=");
        _puts(name);
        _puts(" image=");
        _puts(img.path ? img.path : "<null>");
        _puts(" includeLocals=");
        _putdec(includeLocals ? 1 : 0);
        _puts("\n");
    }
    if (!includeLocals) {
        uint64_t indexedAddr = 0;
        bool indexedWeak = false;
        if (symbolIndexLookup(img, name, &indexedAddr, &indexedWeak)) {
            if (traceResolve) {
                _puts("PANTHERA:dyld find-symbol indexed-hit name=");
                _puts(name);
                _puts(" image=");
                _puts(img.path ? img.path : "<null>");
                _puts(" addr=");
                _puthex(indexedAddr);
                _puts(" weak=");
                _putdec(indexedWeak ? 1 : 0);
                _puts("\n");
            }
            if (isWeakOut) *isWeakOut = indexedWeak;
            return indexedAddr;
        }
        if (!sSymbolIndexOverflow) {
            if (traceResolve) {
                _puts("PANTHERA:dyld find-symbol indexed-miss name=");
                _puts(name);
                _puts(" image=");
                _puts(img.path ? img.path : "<null>");
                _puts("\n");
            }
            return 0;
        }
    }

    if (!img.symtab || !img.strtab) return 0;
    uint64_t weakAddr = 0;
    for (uint32_t i = 0; i < img.nsyms; i++) {
        const nlist_64& sym = img.symtab[i];
        if ((sym.n_type & N_TYPE) == N_UNDF) continue;
        if (!includeLocals && ((sym.n_type & N_EXT) == 0)) continue;
        if (sym.n_un.n_strx == 0 || sym.n_un.n_strx >= img.strsize) continue;
        const char* sname = img.strtab + sym.n_un.n_strx;
        if (_strcmp(sname, name) == 0) {
            uint64_t addr = sym.n_value + img.slide;
            bool isWeak = (sym.n_desc & N_WEAK_DEF) != 0;
            if (!isWeak) {
                if (traceResolve) {
                    _puts("PANTHERA:dyld find-symbol scan-hit name=");
                    _puts(name);
                    _puts(" image=");
                    _puts(img.path ? img.path : "<null>");
                    _puts(" addr=");
                    _puthex(addr);
                    _puts("\n");
                }
                if (isWeakOut) *isWeakOut = false;
                return addr;
            }
            if (!weakAddr) {
                weakAddr = addr;
            }
        }
    }
    if (weakAddr && isWeakOut) {
        *isWeakOut = true;
    }
    if (traceResolve) {
        _puts("PANTHERA:dyld find-symbol exit name=");
        _puts(name);
        _puts(" image=");
        _puts(img.path ? img.path : "<null>");
        _puts(" addr=");
        _puthex(weakAddr);
        _puts("\n");
    }
    return weakAddr;
}

static bool isLibSystemInitCtor(const LoadedImage& img, uint64_t ctorAddr) {
    if (_strcmp(img.path, "/usr/lib/libSystem.B.dylib") != 0) {
        return false;
    }
    uint64_t libSystemInitAddr = findSymbolInImage(img, "___libSystem_init", false);
    return libSystemInitAddr && (ctorAddr == libSystemInitAddr);
}

static uint64_t resolveSymbolFromImageGraphInternal(const LoadedImage& img, const char* name,
                                                    bool includeLocals, bool* isWeakOut,
                                                    bool visited[MAX_IMAGES],
                                                    int depth) {
    bool traceResolve = _panthera_trace_resolve_symbol(name);
    if (traceResolve) {
        _puts("PANTHERA:dyld graph-enter name=");
        _puts(name);
        _puts(" image=");
        _puts(img.path ? img.path : "<null>");
        _puts(" depth=");
        _putdec(depth);
        _puts("\n");
    }
    if (depth > 8) {
        if (isWeakOut) {
            *isWeakOut = false;
        }
        if (traceResolve) {
            _puts("PANTHERA:dyld graph-depth-limit name=");
            _puts(name);
            _puts("\n");
        }
        return 0;
    }

    int imageIndex = loadedImageIndex(img);
    if (imageIndex >= 0) {
        if (visited[imageIndex]) {
            if (isWeakOut) {
                *isWeakOut = false;
            }
            if (traceResolve) {
                _puts("PANTHERA:dyld graph-cycle-skip name=");
                _puts(name);
                _puts(" image=");
                _puts(img.path ? img.path : "<null>");
                _puts("\n");
            }
            return 0;
        }
        visited[imageIndex] = true;
    }

    bool isWeak = false;
    if (traceResolve) {
        _puts("PANTHERA:dyld graph-before-symbol name=");
        _puts(name);
        _puts(" image=");
        _puts(img.path ? img.path : "<null>");
        _puts(" imageIndex=");
        _putdec(imageIndex);
        _puts("\n");
    }
    uint64_t addr = findSymbolInImage(img, name, includeLocals, &isWeak);
    if (!addr && !includeLocals && shouldSearchExportTrieForImage(img)) {
        addr = findExportTrieSymbolInImage(img, name, &isWeak);
        if (addr && !isWeak && traceResolve) {
            _puts("PANTHERA:dyld graph-trie-hit name=");
            _puts(name);
            _puts(" image=");
            _puts(img.path ? img.path : "<null>");
            _puts(" addr=");
            _puthex(addr);
            _puts("\n");
        }
    }
    uint64_t weakAddr = isWeak ? addr : 0;
    if (addr && !isWeak) {
        if (isWeakOut) {
            *isWeakOut = false;
        }
        if (traceResolve) {
            _puts("PANTHERA:dyld graph-direct-hit name=");
            _puts(name);
            _puts(" image=");
            _puts(img.path ? img.path : "<null>");
            _puts(" addr=");
            _puthex(addr);
            _puts("\n");
        }
        return addr;
    }

    const uint8_t* cmd = (const uint8_t*)img.header + sizeof(mach_header_64);
    const uint8_t* cmdsEnd = cmd + img.header->sizeofcmds;
    for (uint32_t i = 0; i < img.header->ncmds; i++) {
        if (cmd + sizeof(load_command) > cmdsEnd) {
            break;
        }
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmdsize < sizeof(load_command) || cmd + lc->cmdsize > cmdsEnd) {
            break;
        }
        if (lc->cmd == LC_REEXPORT_DYLIB) {
            const dylib_command* dc = (const dylib_command*)cmd;
            if (dc->dylib.name.offset >= lc->cmdsize) {
                cmd += lc->cmdsize;
                continue;
            }
            const char* depPath = (const char*)cmd + dc->dylib.name.offset;
            size_t depPathMax = lc->cmdsize - dc->dylib.name.offset;
            if (_strnlen(depPath, depPathMax) == depPathMax) {
                cmd += lc->cmdsize;
                continue;
            }
            LoadedImage* dep = findLoadedImageByPathBounded(depPath, depPathMax);
            if (!dep) {
                dep = loadDependencyImage(depPath);
            }
            if (dep) {
                if (traceResolve) {
                    _puts("PANTHERA:dyld graph-reexport name=");
                    _puts(name);
                    _puts(" from=");
                    _puts(img.path ? img.path : "<null>");
                    _puts(" target=");
                    _puts(dep->path ? dep->path : depPath);
                    _puts("\n");
                }
                bool depWeak = false;
                addr = resolveSymbolFromImageGraphInternal(*dep, name, includeLocals,
                    &depWeak, visited, depth + 1);
                if (addr && !depWeak) {
                    if (isWeakOut) {
                        *isWeakOut = false;
                    }
                    if (traceResolve) {
                        _puts("PANTHERA:dyld graph-reexport-hit name=");
                        _puts(name);
                        _puts(" from=");
                        _puts(img.path ? img.path : "<null>");
                        _puts(" addr=");
                        _puthex(addr);
                        _puts("\n");
                    }
                    return addr;
                }
                if (addr && depWeak && !weakAddr) {
                    weakAddr = addr;
                }
            } else if (traceResolve) {
                _puts("PANTHERA:dyld graph-reexport-missing name=");
                _puts(name);
                _puts(" from=");
                _puts(img.path ? img.path : "<null>");
                _puts("\n");
            }
        }
        cmd += lc->cmdsize;
    }

    if (isWeakOut) {
        *isWeakOut = (weakAddr != 0);
    }
    if (traceResolve) {
        _puts("PANTHERA:dyld graph-exit name=");
        _puts(name);
        _puts(" image=");
        _puts(img.path ? img.path : "<null>");
        _puts(" addr=");
        _puthex(weakAddr);
        _puts("\n");
    }
    return weakAddr;
}

static uint64_t resolveSymbolFromImageGraph(const LoadedImage& img, const char* name,
                                            bool includeLocals, bool* isWeakOut) {
    bool visited[MAX_IMAGES];
    _memset(visited, 0, sizeof(visited));
    return resolveSymbolFromImageGraphInternal(img, name, includeLocals, isWeakOut,
        visited, 0);
}

static bool isLibSystemUmbrella(const LoadedImage& img) {
    return img.path && _strcmp(img.path, "/usr/lib/libSystem.B.dylib") == 0;
}

static uint64_t resolveDirectReexportSymbol(const LoadedImage& img, const char* name,
                                            bool includeLocals, bool* isWeakOut) {
    bool traceResolve = _panthera_trace_resolve_symbol(name);
    uint64_t weakAddr = 0;

    const uint8_t* cmd = (const uint8_t*)img.header + sizeof(mach_header_64);
    const uint8_t* cmdsEnd = cmd + img.header->sizeofcmds;
    for (uint32_t i = 0; i < img.header->ncmds; i++) {
        if (cmd + sizeof(load_command) > cmdsEnd) {
            break;
        }
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmdsize < sizeof(load_command) || cmd + lc->cmdsize > cmdsEnd) {
            break;
        }
        if (lc->cmd == LC_REEXPORT_DYLIB) {
            const dylib_command* dc = (const dylib_command*)cmd;
            if (dc->dylib.name.offset >= lc->cmdsize) {
                cmd += lc->cmdsize;
                continue;
            }
            const char* depPath = (const char*)cmd + dc->dylib.name.offset;
            size_t depPathMax = lc->cmdsize - dc->dylib.name.offset;
            size_t depPathLen = _strnlen(depPath, depPathMax);
            if (depPathLen == depPathMax) {
                cmd += lc->cmdsize;
                continue;
            }
            if (traceResolve || sPantheraDyldProgressTrace) {
                _puts("PANTHERA:dyld direct-reexport-target name=");
                _puts(name ? name : "<null>");
                _puts(" from=");
                _puts(img.path ? img.path : "<null>");
                _puts(" target=");
                _trace_write(depPath, depPathLen);
                _puts("\n");
            }

            LoadedImage* dep = findLoadedImageByPathBounded(depPath, depPathMax);
            if (!dep) {
                dep = loadDependencyImage(depPath);
            }
            if (!dep) {
                if (traceResolve) {
                    _puts("PANTHERA:dyld direct-reexport-missing name=");
                    _puts(name);
                    _puts(" from=");
                    _puts(img.path ? img.path : "<null>");
                    _puts(" target=");
                    _puts(depPath);
                    _puts("\n");
                }
                cmd += lc->cmdsize;
                continue;
            }

            if (traceResolve) {
                _puts("PANTHERA:dyld direct-reexport-check name=");
                _puts(name);
                _puts(" from=");
                _puts(img.path ? img.path : "<null>");
                _puts(" target=");
                _puts(dep->path ? dep->path : depPath);
                _puts("\n");
            }

            bool depWeak = false;
            if (traceResolve || sPantheraDyldProgressTrace) {
                _puts("PANTHERA:dyld direct-reexport-symbol-check name=");
                _puts(name ? name : "<null>");
                _puts(" target=");
                _puts(dep->path ? dep->path : depPath);
                _puts("\n");
            }
            uint64_t addr = findSymbolInImage(*dep, name, includeLocals, &depWeak);
            if (!addr && !includeLocals && shouldSearchExportTrieForImage(*dep)) {
                if (traceResolve || sPantheraDyldProgressTrace) {
                    _puts("PANTHERA:dyld direct-reexport-trie-check name=");
                    _puts(name ? name : "<null>");
                    _puts(" target=");
                    _puts(dep->path ? dep->path : depPath);
                    _puts("\n");
                }
                addr = findExportTrieSymbolInImage(*dep, name, &depWeak);
            }
            if (addr && !depWeak) {
                if (isWeakOut) {
                    *isWeakOut = false;
                }
                if (traceResolve) {
                    _puts("PANTHERA:dyld direct-reexport-hit name=");
                    _puts(name);
                    _puts(" target=");
                    _puts(dep->path ? dep->path : depPath);
                    _puts(" addr=");
                    _puthex(addr);
                    _puts("\n");
                }
                return addr;
            }
            if (addr && depWeak && !weakAddr) {
                weakAddr = addr;
            }
        }
        cmd += lc->cmdsize;
    }

    if (isWeakOut) {
        *isWeakOut = (weakAddr != 0);
    }
    return weakAddr;
}

static uint64_t resolveSymbolInSingleImage(const LoadedImage& img, const char* name,
                                           bool includeLocals, bool* isWeakOut) {
    bool isWeak = false;
    uint64_t addr = findSymbolInImage(img, name, includeLocals, &isWeak);
    if (!addr && !includeLocals && shouldSearchExportTrieForImage(img)) {
        addr = findExportTrieSymbolInImage(img, name, &isWeak);
    }
    if (isWeakOut) {
        *isWeakOut = isWeak && addr != 0;
    }
    return addr;
}

static LoadedImage* findOrdinalImage(const LoadedImage* importer, int ordinal) {
    if (!importer || ordinal <= 0) {
        return nullptr;
    }

    bool traceResolve = false;
    if (traceResolve) {
        _puts("PANTHERA:dyld find-ordinal enter ordinal=");
        _putdec(ordinal);
        _puts(" importer=");
        _puts(importer->path ? importer->path : "<null>");
        _puts(" ncmds=");
        _putdec((int)importer->header->ncmds);
        _puts(" sizeofcmds=");
        _putdec((int)importer->header->sizeofcmds);
        _puts("\n");
    }

    int current = 0;
    const uint8_t* cmd = (const uint8_t*)importer->header + sizeof(mach_header_64);
    const uint8_t* cmdsEnd = cmd + importer->header->sizeofcmds;
    for (uint32_t i = 0; i < importer->header->ncmds; i++) {
        if (cmd + sizeof(load_command) > cmdsEnd) {
            if (traceResolve) {
                _puts("PANTHERA:dyld find-ordinal malformed-short\n");
            }
            return nullptr;
        }
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmdsize < sizeof(load_command) || cmd + lc->cmdsize > cmdsEnd) {
            if (traceResolve) {
                _puts("PANTHERA:dyld find-ordinal malformed-cmdsize index=");
                _putdec((int)i);
                _puts(" cmdsize=");
                _putdec((int)lc->cmdsize);
                _puts("\n");
            }
            return nullptr;
        }
        if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_REEXPORT_DYLIB ||
            lc->cmd == LC_LOAD_WEAK_DYLIB) {
            current++;
            if (current == ordinal) {
                const dylib_command* dc = (const dylib_command*)cmd;
                if (dc->dylib.name.offset >= lc->cmdsize) {
                    if (traceResolve) {
                        _puts("PANTHERA:dyld find-ordinal malformed-name-offset\n");
                    }
                    return nullptr;
                }
                const char* depPath = (const char*)cmd + dc->dylib.name.offset;
                size_t depPathMax = lc->cmdsize - dc->dylib.name.offset;
                if (_strnlen(depPath, depPathMax) == depPathMax) {
                    if (traceResolve) {
                        _puts("PANTHERA:dyld find-ordinal unterminated-name\n");
                    }
                    return nullptr;
                }
                LoadedImage* dep = findLoadedImageByPathBounded(depPath, depPathMax);
                if (!dep) {
                    dep = loadDependencyImage(depPath);
                }
                if (traceResolve) {
                    _puts("PANTHERA:dyld find-ordinal return dep=");
                    _puts(dep && dep->path ? dep->path : "<null>");
                    _puts("\n");
                }
                return dep;
            }
        }
        cmd += lc->cmdsize;
    }

    if (traceResolve) {
        _puts("PANTHERA:dyld find-ordinal miss\n");
    }
    return nullptr;
}

static uint64_t resolveFlatNamespaceSymbol(const char* name, bool includeLocals,
                                           uint64_t weakAddr, bool* foundWeakOut = nullptr) {
    for (int i = 0; i < sNumImages; i++) {
        bool isWeak = false;
        uint64_t addr = findSymbolInImage(sImages[i], name, includeLocals, &isWeak);
        if (addr && !isWeak) {
            if (foundWeakOut) {
                *foundWeakOut = false;
            }
            return addr;
        }
        if (addr && isWeak && !weakAddr) {
            weakAddr = addr;
        }
    }
    if (foundWeakOut) {
        *foundWeakOut = (weakAddr != 0);
    }
    return weakAddr;
}

static uint64_t resolveSymbol(const char* name, int ordinal, bool includeLocals,
                              const LoadedImage* importer) {
    // Darwin's lazy-binding helper is exported by libSystem without the
    // leading C symbol underscore used by normal external symbols.
    if (_strcmp(name, "dyld_stub_binder") != 0) {
        // Skip any non-underscore prefix (bind opcode artifacts)
        while (name[0] && name[0] != '_') name++;
    }

    // Panthera shims — override broken/missing libc functions
    if (_strcmp(name, "_ttyname") == 0) {
        return (uint64_t)(uintptr_t)&panthera_ttyname_shim;
    }
    if (_strcmp(name, "_ttyname_r") == 0 || _strcmp(name, "_ttyname_r$UNIX2003") == 0) {
        return (uint64_t)(uintptr_t)&panthera_ttyname_r_shim;
    }

    uint64_t weakAddr = 0;
    if (ordinal == BIND_SPECIAL_DYLIB_FLAT_LOOKUP ||
        ordinal == BIND_SPECIAL_DYLIB_WEAK_LOOKUP) {
        return resolveFlatNamespaceSymbol(name, includeLocals, weakAddr);
    }
    if (ordinal < BIND_SPECIAL_DYLIB_WEAK_LOOKUP) {
        return 0;
    }
    if (ordinal > 0 && importer) {
        LoadedImage* preferred = findOrdinalImage(importer, ordinal);
        if (!preferred) {
            return 0;
        }
        if (_panthera_trace_resolve_symbol(name)) {
            _puts("PANTHERA:dyld resolve-ordinal name=");
            _puts(name);
            _puts(" ordinal=");
            _putdec(ordinal);
            _puts(" importer=");
            _puts(importer->path ? importer->path : "<null>");
            _puts(" preferred=");
            _puts(preferred->path ? preferred->path : "<null>");
            _puts("\n");
        }

        bool preferredWeak = false;
        uint64_t addr = 0;
        if (isLibSystemUmbrella(*preferred)) {
            addr = resolveDirectReexportSymbol(*preferred, name, includeLocals, &preferredWeak);
            if (addr && preferredWeak) {
                weakAddr = addr;
            }
        }
        if (!addr || preferredWeak) {
            bool graphWeak = false;
            uint64_t graphAddr = resolveSymbolFromImageGraph(*preferred, name, includeLocals, &graphWeak);
            if (graphAddr && !graphWeak) {
                addr = graphAddr;
                preferredWeak = false;
            } else if (graphAddr && graphWeak && !weakAddr) {
                weakAddr = graphAddr;
                preferredWeak = true;
            }
        }
        if (addr && !preferredWeak) {
            if (_panthera_trace_resolve_symbol(name)) {
                _puts("PANTHERA:dyld resolve-ordinal-hit name=");
                _puts(name);
                _puts(" addr=");
                _puthex(addr);
                _puts("\n");
            }
            return addr;
        }
        if (addr && preferredWeak) {
            weakAddr = addr;
        }
        if (_panthera_trace_resolve_symbol(name)) {
            _puts("PANTHERA:dyld resolve-ordinal-exit name=");
            _puts(name);
            _puts(" addr=");
            _puthex(weakAddr);
            _puts(" weak=");
            _putdec(preferredWeak ? 1 : 0);
            _puts("\n");
        }
        return weakAddr;
    }

    return resolveFlatNamespaceSymbol(name, includeLocals, weakAddr);
}

static void publishDyldBridgeState(const mach_header_64* mainMH) {
    uint64_t stateAddr = resolveSymbol("_panthera_dyld_bridge_state", -1, true, nullptr);
    if (!stateAddr) {
        return;
    }

    PantheraDyldBridgeState* state = (PantheraDyldBridgeState*)(uintptr_t)stateAddr;
    state->image_count = 0;
    state->prog_image_header = (const mach_header*)mainMH;

    if (sCacheSplitRegion) {
        state->shared_cache_base = (unsigned long)(uintptr_t)sCacheTextBase;
        state->shared_cache_size = (unsigned long)(sCacheTextSize + sCacheDataSize + sCacheLinkSize);
    } else {
        state->shared_cache_base = (unsigned long)(uintptr_t)sCacheMap;
        state->shared_cache_size = (unsigned long)sCacheFileSize;
    }

    for (int i = 0; i < sNumImages && state->image_count < PANTHERA_DYLD_BRIDGE_MAX_IMAGES; i++) {
        PantheraDyldBridgeImage* out = &state->images[state->image_count++];
        out->header = (const mach_header*)sImages[i].header;
        out->slide = (long)sImages[i].slide;
        out->path = sImages[i].path;
        out->has_objc = 0;
    }

    if (sCacheValid && sCacheSplitRegion) {
        for (uint32_t i = 0; i < sCacheImageCount && state->image_count < PANTHERA_DYLD_BRIDGE_MAX_IMAGES; i++) {
            const char* path = (const char*)(sCacheMap + sCacheImages[i].pathFileOffset);
            uint64_t imgAddr = sCacheImages[i].address;
            if (imgAddr < sCacheTextAddr || imgAddr >= sCacheTextAddr + sCacheTextSize)
                continue;
            const mach_header_64* mh = (const mach_header_64*)(uintptr_t)(imgAddr + sCacheSlide);
            bool alreadyPublished = false;
            for (unsigned int j = 0; j < state->image_count; j++) {
                if (state->images[j].header == (const mach_header*)mh) {
                    alreadyPublished = true;
                    break;
                }
            }
            if (alreadyPublished)
                continue;
            PantheraDyldBridgeImage* out = &state->images[state->image_count++];
            out->header = (const mach_header*)mh;
            out->slide = (long)sCacheSlide;
            out->path = path;
            out->has_objc = 0;
        }
    }
}

struct PantheraLibcFunctions {
    unsigned long version;
    void (*atfork_prepare)(void);
    void (*atfork_parent)(void);
    void (*atfork_child)(void);
    char *(*dirhelper)(int, char *, size_t);
    void (*atfork_prepare_v2)(unsigned int, ...);
    void (*atfork_parent_v2)(unsigned int, ...);
    void (*atfork_child_v2)(unsigned int, ...);
};

struct PantheraAtexitFn {
    int fn_type;
    union {
        void (*std_func)(void);
        void (*cxa_func)(void*);
        void* raw;
    } fn_ptr;
    void* fn_arg;
    void* fn_dso;
};

struct PantheraAtexit {
    PantheraAtexit* next;
    int ind;
    PantheraAtexitFn fns[32];
};

static void
panthera_atfork_prepare(void)
{
}

static void
panthera_atfork_parent(void)
{
}

static void
panthera_atfork_child(void)
{
}

static void panthera_run_libc_init(const char** apple) {
    PantheraLibcFunctions funcs = {};
    funcs.version = 1;
    funcs.atfork_prepare = panthera_atfork_prepare;
    funcs.atfork_parent = panthera_atfork_parent;
    funcs.atfork_child = panthera_atfork_child;
    {
        uint64_t prep = resolveSymbol("__pthread_atfork_prepare", -1, true);
        uint64_t par = resolveSymbol("__pthread_atfork_parent", -1, true);
        uint64_t child = resolveSymbol("__pthread_atfork_child", -1, true);

        if (prep && par && child) {
            funcs.atfork_prepare = (void (*)(void))prep;
            funcs.atfork_parent = (void (*)(void))par;
            funcs.atfork_child = (void (*)(void))child;
        }
    }
    static PantheraAtexit panthera_atexit_storage = {};

    {
        uint64_t addr = resolveSymbol("__program_vars_init", -1, true);
        if (addr) {
            typedef void (*Fn)(const ProgramVars*);
            ((Fn)addr)(&sProgramVars);
        }
    }
    {
        uint64_t addr = resolveSymbol("__libc_fork_init", -1, true);
        if (addr) {
            typedef void (*Fn)(const PantheraLibcFunctions*);
            ((Fn)addr)(&funcs);
        }
    }
    {
        uint64_t addr = resolveSymbol("___confstr_init", -1, true);
        if (addr) {
            typedef void (*Fn)(const PantheraLibcFunctions*);
            ((Fn)addr)(&funcs);
        }
    }
    {
        uint64_t atexitPtrAddr = resolveSymbol("___atexit", -1, true);
        if (atexitPtrAddr) {
            *(PantheraAtexit**)atexitPtrAddr = &panthera_atexit_storage;
        }
    }
    {
        uint64_t addr = resolveSymbol("__init_clock_port", -1, true);
        if (addr) {
            typedef void (*Fn)(void);
            ((Fn)addr)();
        }
    }
    {
        uint64_t addr = resolveSymbol("___chk_init", -1, true);
        if (addr) {
            typedef void (*Fn)(void);
            ((Fn)addr)();
        }
    }
    {
        uint64_t addr = resolveSymbol("___xlocale_init", -1, true);
        if (addr) {
            typedef void (*Fn)(void);
            ((Fn)addr)();
        }
    }
    {
        uint64_t addr = resolveSymbol("___guard_setup", -1, true);
        if (addr) {
            typedef void (*Fn)(const char**);
            ((Fn)addr)(apple);
        }
    }
    {
        uint64_t addr = resolveSymbol("__subsystem_init", -1, true);
        if (addr) {
            typedef void (*Fn)(const char**);
            ((Fn)addr)(apple);
        }
    }
    {
        uint64_t addr = resolveSymbol("___stdio_init", -1, true);
        if (addr) {
            typedef void (*Fn)(void);
            ((Fn)addr)();
        }
    }
    /*
     * Note: __bsdthread_register (syscall 366) is canonical to libpthread's
     * __pthread_init() / _pthread_bsdthread_init() during libSystem initialization.
     * dyld must not perform early duplicate registration so that libpthread
     * performs the sole full registration and properly captures returned kernel features.
     */
}

static void initializeProgramVars(const mach_header_64* mainMH, int argc, const char** argv, const char** envp) {
    sArgcStorage = argc;
    sArgvStorage = (char**)argv;
    sEnvpStorage = (char**)envp;
    sPrognameStorage = (argc > 0 && argv && argv[0]) ? (char*)_basename(argv[0]) : (char*)"";

    sProgramVars.mh = (void*)mainMH;
    sProgramVars.NXArgcPtr = &sArgcStorage;
    sProgramVars.NXArgvPtr = &sArgvStorage;
    sProgramVars.environPtr = &sEnvpStorage;
    sProgramVars.__prognamePtr = &sPrognameStorage;

    uint64_t environAddr = resolveSymbol("_environ");
    if (environAddr)
        *(char***)environAddr = sEnvpStorage;

    uint64_t nxArgcAddr = resolveSymbol("_NXArgc");
    if (nxArgcAddr)
        *(int*)nxArgcAddr = sArgcStorage;

    uint64_t nxArgvAddr = resolveSymbol("_NXArgv");
    if (nxArgvAddr)
        *(char***)nxArgvAddr = sArgvStorage;

    uint64_t progVarsInitAddr = resolveSymbol("__program_vars_init", -1, true);
    if (progVarsInitAddr) {
        typedef void (*ProgramVarsInitFunc)(const ProgramVars*);
        ProgramVarsInitFunc init = (ProgramVarsInitFunc)progVarsInitAddr;
        init(&sProgramVars);
    } else {
        // Fallback for locally scoped crt_externs state if __program_vars_init is unavailable.
        uint64_t nxArgvPtr = resolveSymbol("_NXArgv_pointer", -1, true);
        uint64_t nxArgcPtr = resolveSymbol("_NXArgc_pointer", -1, true);
        uint64_t environPtr = resolveSymbol("_environ_pointer", -1, true);
        uint64_t prognamePtr = resolveSymbol("___progname_pointer", -1, true);
        uint64_t mhPtr = resolveSymbol("__mh_execute_header_pointer", -1, true);

        if (nxArgvPtr) *(char****)nxArgvPtr = &sArgvStorage;
        if (nxArgcPtr) *(int**)nxArgcPtr = &sArgcStorage;
        if (environPtr) *(char****)environPtr = &sEnvpStorage;
        if (prognamePtr) *(char***)prognamePtr = &sPrognameStorage;
        if (mhPtr) *(void**)mhPtr = (void*)mainMH;
    }
}

// ─── Map a dylib ─────────────────────────────────────────────
static LoadedImage* loadImage(const char* path);

static LoadedImage* loadDependencyImage(const char* depPath) {
    _panthera_phase_path("load dependency enter", depPath);
    if (LoadedImage* existing = findLoadedImageByPath(depPath)) {
        _trace_image_phase("dependency already registered", depPath);
        _panthera_phase_path("load dependency already registered", depPath);
        return existing;
    }
    LoadedImage* loaded = loadImage(depPath);
    _panthera_phase_path(loaded ? "load dependency return" : "load dependency failed", depPath);
    return loaded;
}

static void parseSymtab(LoadedImage* img) {
    _panthera_progress_path("parse-symtab-enter", img->path);
    // Find the lowest vmaddr (base of our allocation)
    const segment_command_64* textSeg = findSegment(img->header, SEG_TEXT);
    const segment_command_64* linkEdit = findSegment(img->header, SEG_LINKEDIT);
    if (!textSeg || !linkEdit) {
        _panthera_progress_path("parse-symtab-no-seg", img->path);
        return;
    }

    // linkeditBase: the address where __LINKEDIT's file data starts in memory
    // We copied LINKEDIT.filesize bytes to: base + (LINKEDIT.vmaddr - TEXT.vmaddr)
    // So file offset X within LINKEDIT is at: base + (LINKEDIT.vmaddr - TEXT.vmaddr) + (X - LINKEDIT.fileoff)
    uintptr_t linkeditBase = linkeditBaseForImage(*img, textSeg, linkEdit);
    if (linkeditBase == 0) {
        _panthera_progress_path("parse-symtab-no-lbase", img->path);
        return;
    }
    _panthera_progress_path("parse-symtab-lbase-ok", img->path);

    const uint8_t* cmd = (const uint8_t*)img->header + sizeof(mach_header_64);
    for (uint32_t i = 0; i < img->header->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_SYMTAB) {
            const symtab_command* sc = (const symtab_command*)cmd;
            img->symtab = (const nlist_64*)(linkeditBase + sc->symoff);
            img->strtab = (const char*)(linkeditBase + sc->stroff);
            img->nsyms = sc->nsyms;
            img->strsize = sc->strsize;
        }
        if (lc->cmd == LC_DYSYMTAB) {
            const dysymtab_command* dc = (const dysymtab_command*)cmd;
            img->indirectSymtab = (const uint32_t*)(linkeditBase + dc->indirectsymoff);
            img->nindirectsyms = dc->nindirectsyms;
        }
        cmd += lc->cmdsize;
    }
}

static LoadedImage* loadImage(const char* path) {
    _trace_image_phase("load enter", path);
    _panthera_progress_path("load-enter", path);

    // Check if already loaded
    for (int i = 0; i < sNumImages; i++) {
        if (sImages[i].path && _strcmp(sImages[i].path, path) == 0) {
            _trace_image_phase("load already path", path);
            return &sImages[i];
        }
        // Also match basename to avoid duplicate loads
        if (sImages[i].path && _strcmp(_basename(sImages[i].path), _basename(path)) == 0) {
            _trace_image_phase("load already basename", path);
            return &sImages[i];
        }
    }
    if (sNumImages >= MAX_IMAGES) {
        _puts("dyld: too many images\n");
        return nullptr;
    }

    // ── Try shared cache ──
    size_t cachedSize = 0;
    _panthera_progress_path("find-in-cache-enter", path);
    const void* cachedData = findDylibInCache(path, &cachedSize);
    if (cachedData) {
        _panthera_progress_path("find-in-cache-hit", path);
        _trace_image_phase("cache hit", path);
        const mach_header_64* mh = (const mach_header_64*)cachedData;
        _panthera_progress_path("cache-magic-read-enter", path);
        uint32_t cachedMagic = mh->magic;
        _panthera_progress_path("cache-magic-read-done", path);
        if (cachedMagic == MH_MAGIC_64) {

            if (sCacheSplitRegion && sCachePreResolved) {
                _panthera_progress_path("cache-split-resolved-enter", path);
                // Split-region cache: segments are already mapped at their VM addresses.
                // The load commands in the cache contain the correct vmaddr values.
                // Use the image in-place — no copying needed.
                LoadedImage* img = &sImages[sNumImages++];
                img->header = mh;
                img->slide = sCacheSlide;  // 0 if mapped at intended address
                img->path = path;
                img->symtab = nullptr; img->strtab = nullptr; img->nsyms = 0; img->strsize = 0;
                img->indirectSymtab = nullptr; img->nindirectsyms = 0;
                parseSymtab(img);
                _panthera_progress_path("cache-split-resolved-symtab-done", path);
                _trace_image_phase("cache registered", path);
                // Cache images are part of a precomputed closure. Register the image
                // itself here; reexport and ordinal targets are loaded lazily by
                // symbol resolution when a bind actually requires them.
                _trace_image_phase("load done", path);
                _panthera_progress_path("load-done", path);
                return img;
            }

            // Phase A: flat cache — copy segments to contiguous VM
            uint64_t lo = ~0ULL, hi = 0;
            const uint8_t* cmd = (const uint8_t*)mh + sizeof(mach_header_64);
            for (uint32_t ci = 0; ci < mh->ncmds; ci++) {
                const load_command* lc = (const load_command*)cmd;
                if (lc->cmd == LC_SEGMENT_64) {
                    const segment_command_64* seg = (const segment_command_64*)cmd;
                    if (seg->vmsize > 0) {
                        if (seg->vmaddr < lo) lo = seg->vmaddr;
                        if (seg->vmaddr + seg->vmsize > hi) hi = seg->vmaddr + seg->vmsize;
                    }
                }
                cmd += lc->cmdsize;
            }
            uint64_t totalSz = (hi - lo + 0xfff) & ~0xfffULL;
            void* base = (void*)sys_mmap(0, totalSz, PROT_READ|PROT_WRITE|PROT_EXEC,
                                          MAP_PRIVATE|MAP_ANON, -1, 0);
            if ((long)base > 0) {
                intptr_t slide = (intptr_t)base - (intptr_t)lo;
                cmd = (const uint8_t*)mh + sizeof(mach_header_64);
                for (uint32_t ci = 0; ci < mh->ncmds; ci++) {
                    const load_command* lc = (const load_command*)cmd;
                    if (lc->cmd == LC_SEGMENT_64) {
                        const segment_command_64* seg = (const segment_command_64*)cmd;
                        if (seg->filesize > 0)
                            _memcpy((uint8_t*)base + (seg->vmaddr - lo),
                                   (const uint8_t*)cachedData + seg->fileoff, seg->filesize);
                    }
                    cmd += lc->cmdsize;
                }
                LoadedImage* img = &sImages[sNumImages++];
                img->header = (const mach_header_64*)base;
                img->slide = slide;
                img->path = path;
                img->symtab = nullptr; img->strtab = nullptr; img->nsyms = 0; img->strsize = 0;
                img->indirectSymtab = nullptr; img->nindirectsyms = 0;
                parseSymtab(img);
                _trace_image_phase("flat cache registered", path);
                _trace_image_phase("load done", path);
                return img;
            }
        }
    }

    _trace_image_phase("open begin", path);
    _panthera_progress_path("file-open-enter", path);
    int fd = (int)sys_open(path, O_RDONLY, 0);
    if (fd < 0) {
        _puts("dyld: cannot open: ");
        _puts(path);
        _puts("\n");
        return nullptr;
    }
    _trace_image_phase("open done", path);

    // Get file size
    _trace_image_phase("fstat begin", path);
    _panthera_progress_path("file-fstat-enter", path);
    struct stat st;
    long fstatResult = sys_fstat(fd, &st);
    if (sDyldVerbose && _contains(path, "libz")) {
        _puts("dyld: libz fstat result=");
        _putdec((int)fstatResult);
        _puts(" size=");
        _putdec((int)st.st_size);
        _puts("\n");
    }
    if (fstatResult < 0 || st.st_size <= 0) {
        sys_close(fd);
        _puts("dyld: fstat failed: ");
        _puts(path);
        _puts("\n");
        return nullptr;
    }
    _trace_image_phase("fstat done", path);

    bool useFileSegmentMapping = st.st_size >= (1024 * 1024);
    long snapshotSize = useFileSegmentMapping ? (64 * 1024) : st.st_size;
    if (snapshotSize > st.st_size) {
        snapshotSize = st.st_size;
    }

    // Snapshot enough of the file into anonymous memory for Mach-O inspection. The live
    // image still comes from copied or file-backed segments below; this buffer
    // is only a load-command/linkedit reader and avoids coupling parser state
    // to a temporary file mapping. Large dylibs do not need a full-file reader:
    // their live pages are mapped from the file, and partial segment tails are
    // read explicitly below.
    _trace_image_phase("file snapshot mmap begin", path);
    void* mapped = (void*)sys_mmap(0, snapshotSize, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((long)mapped < 0) {
        sys_close(fd);
        _puts("dyld: snapshot mmap failed: ");
        _puts(path);
        _puts("\n");
        return nullptr;
    }
    _trace_image_phase("file snapshot mmap done", path);

    long snapshotReadSize = useFileSegmentMapping ? 4096 : snapshotSize;
    if (snapshotReadSize > snapshotSize) {
        snapshotReadSize = snapshotSize;
    }
    _trace_image_phase("file read begin", path);
    _panthera_progress_path("file-read-enter", path);
    if (!readFileFully(fd, mapped, snapshotReadSize)) {
        _puts("dyld: read failed: ");
        _puts(path);
        _puts("\n");
        sys_munmap(mapped, snapshotSize);
        sys_close(fd);
        return nullptr;
    }
    _trace_image_phase("file read done", path);
    _panthera_progress_path("file-read-done", path);
    if (sPantheraDyldPhaseTrace &&
        path && (_contains(path, "libsandbox") || _contains(path, "libcrypto") || _contains(path, "libz"))) {
        _puts("PANTHERA:dyld file snapshot result ");
        _puts(path);
        _puts(" addr=");
        _puthex((uint64_t)(uintptr_t)mapped);
        _puts(" size=");
        _putdec((int)st.st_size);
        _puts("\n");
        _puts("PANTHERA:dyld mach header read begin ");
        _puts(path);
        _puts("\n");
    }

    const mach_header_64* mh = (const mach_header_64*)mapped;
    uint32_t mappedMagic = mh->magic;
    if (sPantheraDyldPhaseTrace &&
        path && (_contains(path, "libsandbox") || _contains(path, "libcrypto") || _contains(path, "libz"))) {
        _puts("PANTHERA:dyld mach header read done ");
        _puts(path);
        _puts(" magic=");
        _puthex(mappedMagic);
        _puts("\n");
    }
    if (mappedMagic != MH_MAGIC_64) {
        _puts("dyld: not Mach-O 64: ");
        _puts(path);
        _puts("\n");
        sys_munmap(mapped, snapshotSize);
        sys_close(fd);
        return nullptr;
    }
    uint64_t snapshotNeeded = (uint64_t)sizeof(mach_header_64) + mh->sizeofcmds;
    if (snapshotNeeded > (uint64_t)snapshotSize) {
        _puts("dyld: load commands exceed snapshot: ");
        _puts(path);
        _puts("\n");
        sys_munmap(mapped, snapshotSize);
        sys_close(fd);
        return nullptr;
    }
    if (snapshotNeeded > (uint64_t)snapshotReadSize) {
        if (!readFileFullyAt(fd, snapshotReadSize,
                (uint8_t*)mapped + snapshotReadSize,
                (long)(snapshotNeeded - (uint64_t)snapshotReadSize))) {
            _puts("dyld: load command read failed: ");
            _puts(path);
            _puts("\n");
            sys_munmap(mapped, snapshotSize);
            sys_close(fd);
            return nullptr;
        }
        snapshotReadSize = (long)snapshotNeeded;
    }
    _trace_image_phase("mach header ok", path);

    // Find total VM range
    uint64_t lo = ~0ULL, hi = 0;
    const uint8_t* cmd = (const uint8_t*)mh + sizeof(mach_header_64);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const segment_command_64* seg = (const segment_command_64*)cmd;
            if (seg->vmsize > 0) {
                if (seg->vmaddr < lo) lo = seg->vmaddr;
                if (seg->vmaddr + seg->vmsize > hi) hi = seg->vmaddr + seg->vmsize;
            }
        }
        cmd += lc->cmdsize;
    }
    _trace_image_phase("vm range done", path);

    // Allocate VM range
    uint64_t totalSize = (hi - lo + 0xfff) & ~0xfffULL;
    _trace_image_phase("vm mmap begin", path);
    void* base = (void*)sys_mmap(0, totalSize, PROT_READ | PROT_WRITE | PROT_EXEC,
                                  MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((long)base < 0) {
        sys_munmap(mapped, snapshotSize);
        sys_close(fd);
        return nullptr;
    }
    _trace_image_phase("vm mmap done", path);

    intptr_t slide = (intptr_t)base - (intptr_t)lo;

    _trace_image_phase(useFileSegmentMapping ? "segments file map begin" : "segments copy begin", path);
    _panthera_progress_path(useFileSegmentMapping ? "segments-map-enter" : "segments-copy-enter", path);
    cmd = (const uint8_t*)mh + sizeof(mach_header_64);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_SEGMENT_64) {
            const segment_command_64* seg = (const segment_command_64*)cmd;
            if (seg->filesize > 0) {
                uint64_t segOffset = seg->vmaddr - lo;
                if (useFileSegmentMapping) {
                    const uint64_t pageMask = 0xfff;
                    uint64_t filePageOffset = seg->fileoff & pageMask;
                    uint64_t vmPageOffset = segOffset & pageMask;

                    if (filePageOffset != 0 || vmPageOffset != 0) {
                        _puts("dyld: unaligned segment mapping unsupported: ");
                        _puts(path);
                        _puts("\n");
                        sys_munmap(mapped, snapshotSize);
                        sys_munmap(base, totalSize);
                        sys_close(fd);
                        return nullptr;
                    }

                    uint64_t fullFilePages = seg->filesize & ~pageMask;
                    if (fullFilePages > 0) {
                        void* want = (uint8_t*)base + segOffset;
                        void* segMap = (void*)sys_mmap(want, fullFilePages,
                            PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_FIXED, fd, seg->fileoff);
                        if ((long)segMap < 0) {
                            _puts("dyld: segment mmap failed: ");
                            _puts(path);
                            _puts("\n");
                            sys_munmap(mapped, snapshotSize);
                            sys_munmap(base, totalSize);
                            sys_close(fd);
                            return nullptr;
                        }
                    }

                    uint64_t tailSize = seg->filesize - fullFilePages;
                    if (tailSize > 0) {
                        uint64_t tailFileOffset = seg->fileoff + fullFilePages;
                        void* tailTarget = (uint8_t*)base + segOffset + fullFilePages;
                        if (tailFileOffset + tailSize <= (uint64_t)snapshotReadSize) {
                            _memcpy(tailTarget,
                                (const uint8_t*)mapped + tailFileOffset,
                                tailSize);
                        } else if (!readFileFullyAt(fd, tailFileOffset, tailTarget, (long)tailSize)) {
                            _puts("dyld: segment tail read failed: ");
                            _puts(path);
                            _puts("\n");
                            sys_munmap(mapped, snapshotSize);
                            sys_munmap(base, totalSize);
                            sys_close(fd);
                            return nullptr;
                        }
                    }

                    uint64_t zeroStart = segOffset + seg->filesize;
                    uint64_t zeroEnd = (zeroStart + pageMask) & ~pageMask;
                    uint64_t segEnd = segOffset + seg->vmsize;
                    if (zeroEnd > segEnd)
                        zeroEnd = segEnd;
                    if (zeroEnd > zeroStart)
                        _memset((uint8_t*)base + zeroStart, 0, zeroEnd - zeroStart);
                } else {
                    _memcpy((uint8_t*)base + segOffset,
                           (const uint8_t*)mapped + seg->fileoff,
                           seg->filesize);
                }
            }
        }
        cmd += lc->cmdsize;
    }
    sys_close(fd);
    _trace_image_phase(useFileSegmentMapping ? "segments file map done" : "segments copy done", path);
    _panthera_progress_path(useFileSegmentMapping ? "segments-map-done" : "segments-copy-done", path);

    // Register image
    LoadedImage* img = &sImages[sNumImages++];
    img->header = (const mach_header_64*)base;
    img->slide = slide;
    img->path = path;
    img->symtab = nullptr;
    img->strtab = nullptr;
    img->nsyms = 0;
    img->strsize = 0;
    img->indirectSymtab = nullptr;
    img->nindirectsyms = 0;

    _trace_image_phase("parse symtab begin", path);
    parseSymtab(img);
    _trace_image_phase("parse symtab done", path);

    // Load dependencies
    _trace_image_phase("dependencies begin", path);
    _panthera_progress_path("dependencies-enter", path);
    cmd = (const uint8_t*)img->header + sizeof(mach_header_64);
    for (uint32_t i = 0; i < img->header->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_REEXPORT_DYLIB ||
            lc->cmd == LC_LOAD_WEAK_DYLIB) {
            const dylib_command* dc = (const dylib_command*)cmd;
            const char* depPath = (const char*)cmd + dc->dylib.name.offset;
            _trace_image_phase("dependency", depPath);
            loadDependencyImage(depPath);
            _trace_image_phase("dependency done", depPath);
        }
        cmd += lc->cmdsize;
    }
    _trace_image_phase("dependencies done", path);
    _panthera_progress_path("dependencies-done", path);

    _trace_image_phase("file snapshot munmap begin", path);
    sys_munmap(mapped, snapshotSize);
    _trace_image_phase("file snapshot munmap done", path);

    _trace_image_phase("load done", path);
    _panthera_progress_path("load-done", path);
    return img;
}

// ─── Apply rebases ───────────────────────────────────────────
static void applyRebases(LoadedImage* img) {
    const uint8_t* cmd = (const uint8_t*)img->header + sizeof(mach_header_64);
    for (uint32_t i = 0; i < img->header->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
            const dyld_info_command* di = (const dyld_info_command*)cmd;
            if (di->rebase_size == 0) break;

            // rebase_off is a FILE offset — translate via linkeditBase
            const segment_command_64* _textSeg = findSegment(img->header, SEG_TEXT);
            const segment_command_64* _linkSeg = findSegment(img->header, SEG_LINKEDIT);
            if (!_textSeg || !_linkSeg) break;
            uintptr_t _lbase = linkeditBaseForImage(*img, _textSeg, _linkSeg);
            if (_lbase == 0) break;
            const uint8_t* p = (const uint8_t*)(_lbase + di->rebase_off);
            const uint8_t* end = p + di->rebase_size;

            uint64_t segOffset = 0;
            int segIndex = 0;

            // Find segment base addresses
            const segment_command_64* segs[16] = {};
            int nseg = 0;
            const uint8_t* sc = (const uint8_t*)img->header + sizeof(mach_header_64);
            for (uint32_t j = 0; j < img->header->ncmds; j++) {
                const load_command* slc = (const load_command*)sc;
                if (slc->cmd == LC_SEGMENT_64 && nseg < 16)
                    segs[nseg++] = (const segment_command_64*)sc;
                sc += slc->cmdsize;
            }

            while (p < end) {
                uint8_t byte = *p++;
                uint8_t opcode = byte & 0xF0;
                uint8_t imm = byte & 0x0F;

                switch (opcode) {
                case 0x10: (void)imm; break;
                case 0x20:
                    segIndex = imm;
                    segOffset = readULEB128(p);
                    break;
                case 0x30:
                    segOffset += readULEB128(p);
                    break;
                case 0x40:
                    segOffset += imm * sizeof(uintptr_t);
                    break;
                case 0x50: // DO_REBASE_IMM_TIMES
                    for (uint8_t j = 0; j < imm; j++) {
                        uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                            segs[segIndex]->vmaddr - _textSeg->vmaddr + segOffset);
                        *loc += img->slide;
                        segOffset += sizeof(uintptr_t);
                    }
                    break;
                case 0x60: { // DO_REBASE_ULEB_TIMES
                    uint64_t count = readULEB128(p);
                    for (uint64_t j = 0; j < count; j++) {
                        uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                            segs[segIndex]->vmaddr - _textSeg->vmaddr + segOffset);
                        *loc += img->slide;
                        segOffset += sizeof(uintptr_t);
                    }
                    break;
                }
                case 0x70: { // DO_REBASE_ADD_ADDR_ULEB
                    uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                        segs[segIndex]->vmaddr - _textSeg->vmaddr + segOffset);
                    *loc += img->slide;
                    segOffset += readULEB128(p) + sizeof(uintptr_t);
                    break;
                }
                case 0x80: { // DO_REBASE_ULEB_TIMES_SKIPPING_ULEB
                    uint64_t count = readULEB128(p);
                    uint64_t skip = readULEB128(p);
                    for (uint64_t j = 0; j < count; j++) {
                        uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                            segs[segIndex]->vmaddr - _textSeg->vmaddr + segOffset);
                        *loc += img->slide;
                        segOffset += skip + sizeof(uintptr_t);
                    }
                    break;
                }
                case 0x00: goto rebase_done;
                }
            }
            rebase_done:;
        }
        cmd += lc->cmdsize;
    }
}

// ─── Apply binds ─────────────────────────────────────────────
static int sUnresolvedBindLogCount = 0;

static void reportUnresolvedBind(const LoadedImage* importer, const char* symbolName, int ordinal) {
    if (sUnresolvedBindLogCount >= 64) {
        return;
    }
    sUnresolvedBindLogCount++;
    _puts("dyld: unresolved bind ");
    _puts(symbolName ? symbolName : "<null>");
    _puts(" ordinal=");
    _putdec(ordinal);
    _puts(" importer=");
    _puts((importer && importer->path) ? importer->path : "<null>");
    _puts("\n");
}

static void applyBinds(LoadedImage* img) {
    const uint8_t* cmd = (const uint8_t*)img->header + sizeof(mach_header_64);
    const bool traceBinds = sDyldVerbose && img->path && _contains(img->path, "libcrypto");
    for (uint32_t i = 0; i < img->header->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_DYLD_INFO || lc->cmd == LC_DYLD_INFO_ONLY) {
            const dyld_info_command* di = (const dyld_info_command*)cmd;

            // Process both bind and lazy_bind
            for (int pass = 0; pass < 2; pass++) {
                uint32_t off = (pass == 0) ? di->bind_off : di->lazy_bind_off;
                uint32_t sz  = (pass == 0) ? di->bind_size : di->lazy_bind_size;
                if (sz == 0) continue;

                // bind offsets are FILE offsets — translate via linkeditBase
                const segment_command_64* _btextSeg = findSegment(img->header, SEG_TEXT);
                const segment_command_64* _blinkSeg = findSegment(img->header, SEG_LINKEDIT);
                if (!_btextSeg || !_blinkSeg) continue;
                uintptr_t _blbase = linkeditBaseForImage(*img, _btextSeg, _blinkSeg);
                if (_blbase == 0) continue;
                const uint8_t* p = (const uint8_t*)(_blbase + off);
                const uint8_t* end = p + sz;

                // Find segments
                const segment_command_64* segs[16] = {};
                int nseg = 0;
                const uint8_t* sc = (const uint8_t*)img->header + sizeof(mach_header_64);
                for (uint32_t j = 0; j < img->header->ncmds; j++) {
                    const load_command* slc = (const load_command*)sc;
                    if (slc->cmd == LC_SEGMENT_64 && nseg < 16)
                        segs[nseg++] = (const segment_command_64*)sc;
                    sc += slc->cmdsize;
                }

                int segIndex = 0;
                uint64_t segOffset = 0;
                const char* symbolName = "";
                uint8_t symbolFlags = 0;
                int64_t addend = 0;
                int ordinal = 0;
                uint32_t bindCount = 0;
                uint32_t opcodeCount = 0;

                if (traceBinds) {
                    _puts("dyld: bind pass begin pass=");
                    _putdec(pass);
                    _puts(" size=");
                    _putdec((int)sz);
                    _puts(" image=");
                    _puts(img->path);
                    _puts("\n");
                }

                while (p < end) {
                    opcodeCount++;
                    if (opcodeCount > 1000000) {
                        _puts("dyld: malformed bind opcode loop image=");
                        _puts(img->path ? img->path : "<null>");
                        _puts(" pass=");
                        _putdec(pass);
                        _puts("\n");
                        break;
                    }
                    uint8_t byte = *p++;
                    uint8_t opcode = byte & 0xF0;
                    uint8_t imm = byte & 0x0F;

                    switch (opcode) {
                    case 0x00: // BIND_OPCODE_DONE
                        if (pass == 0) {
                            // Non-lazy: DONE separates entries. Keep going
                            // until we reach the end of the bind data.
                            if (p >= end) goto bind_done;
                            break; // continue processing next entry
                        }
                        break; // lazy bind: DONE = next symbol, keep going
                    case 0x10: ordinal = imm; break;
                    case 0x20: ordinal = (int)readULEB128(p); break;
                    case 0x30: // SET_DYLIB_SPECIAL_IMM
                        if (imm == 0) ordinal = 0;
                        else ordinal = (int8_t)(0xF0 | imm); // sign-extend
                        break;
                    case 0x40: // BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM
                        symbolFlags = imm;
                        symbolName = (const char*)p;
                        while (p < end && *p) p++;
                        if (p < end) p++; // skip null terminator
                        break;
                    case 0x50: break; // BIND_OPCODE_SET_TYPE_IMM — type in imm, no data
                    case 0x60: addend = readSLEB128(p); break; // BIND_OPCODE_SET_ADDEND_SLEB
                    case 0x70:
                        segIndex = imm;
                        segOffset = readULEB128(p);
                        break;
                    case 0x80: segOffset += readULEB128(p); break;
                    case 0x90: { // DO_BIND
                        bindCount++;
                        if (traceBinds && (bindCount <= 16 || (bindCount % 64) == 0)) {
                            _puts("dyld: bind resolve pass=");
                            _putdec(pass);
                            _puts(" count=");
                            _putdec((int)bindCount);
                            _puts(" ordinal=");
                            _putdec(ordinal);
                            _puts(" symbol=");
                            _puts(symbolName ? symbolName : "<null>");
                            _puts("\n");
                        }
                        uint64_t addr = resolveSymbol(symbolName, ordinal, false, img);
                        uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                            segs[segIndex]->vmaddr - _btextSeg->vmaddr + segOffset);
                        if (addr) {
                            *loc = addr + addend;
                        } else if (symbolFlags & BIND_SYMBOL_FLAGS_WEAK_IMPORT) {
                            *loc = 0;
                        } else {
                            reportUnresolvedBind(img, symbolName, ordinal);
                        }
                        segOffset += sizeof(uintptr_t);
                        break;
                    }
                    case 0xa0: { // DO_BIND_ADD_ADDR_ULEB
                        bindCount++;
                        if (traceBinds && (bindCount <= 16 || (bindCount % 64) == 0)) {
                            _puts("dyld: bind resolve pass=");
                            _putdec(pass);
                            _puts(" count=");
                            _putdec((int)bindCount);
                            _puts(" ordinal=");
                            _putdec(ordinal);
                            _puts(" symbol=");
                            _puts(symbolName ? symbolName : "<null>");
                            _puts("\n");
                        }
                        uint64_t addr = resolveSymbol(symbolName, ordinal, false, img);
                        uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                            segs[segIndex]->vmaddr - _btextSeg->vmaddr + segOffset);
                        if (addr) {
                            *loc = addr + addend;
                        } else if (symbolFlags & BIND_SYMBOL_FLAGS_WEAK_IMPORT) {
                            *loc = 0;
                        } else {
                            reportUnresolvedBind(img, symbolName, ordinal);
                        }
                        segOffset += readULEB128(p) + sizeof(uintptr_t);
                        break;
                    }
                    case 0xb0: { // DO_BIND_ADD_ADDR_IMM_SCALED
                        bindCount++;
                        if (traceBinds && (bindCount <= 16 || (bindCount % 64) == 0)) {
                            _puts("dyld: bind resolve pass=");
                            _putdec(pass);
                            _puts(" count=");
                            _putdec((int)bindCount);
                            _puts(" ordinal=");
                            _putdec(ordinal);
                            _puts(" symbol=");
                            _puts(symbolName ? symbolName : "<null>");
                            _puts("\n");
                        }
                        uint64_t addr = resolveSymbol(symbolName, ordinal, false, img);
                        uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                            segs[segIndex]->vmaddr - _btextSeg->vmaddr + segOffset);
                        if (addr) {
                            *loc = addr + addend;
                        } else if (symbolFlags & BIND_SYMBOL_FLAGS_WEAK_IMPORT) {
                            *loc = 0;
                        } else {
                            reportUnresolvedBind(img, symbolName, ordinal);
                        }
                        segOffset += (imm + 1) * sizeof(uintptr_t);
                        break;
                    }
                    case 0xc0: { // DO_BIND_ULEB_TIMES_SKIPPING_ULEB
                        uint64_t count = readULEB128(p);
                        uint64_t skip = readULEB128(p);
                        bindCount += (uint32_t)count;
                        if (traceBinds) {
                            _puts("dyld: bind repeated pass=");
                            _putdec(pass);
                            _puts(" total=");
                            _putdec((int)bindCount);
                            _puts(" count=");
                            _putdec((int)count);
                            _puts(" ordinal=");
                            _putdec(ordinal);
                            _puts(" symbol=");
                            _puts(symbolName ? symbolName : "<null>");
                            _puts("\n");
                        }
                        uint64_t addr = resolveSymbol(symbolName, ordinal, false, img);
                        for (uint64_t j = 0; j < count; j++) {
                            uintptr_t* loc = (uintptr_t*)((uintptr_t)img->header +
                                segs[segIndex]->vmaddr - _btextSeg->vmaddr + segOffset);
                            if (addr) {
                                *loc = addr + addend;
                            } else if (symbolFlags & BIND_SYMBOL_FLAGS_WEAK_IMPORT) {
                                *loc = 0;
                            } else {
                                reportUnresolvedBind(img, symbolName, ordinal);
                            }
                            segOffset += skip + sizeof(uintptr_t);
                        }
                        break;
                    }
                    }
                }
                bind_done:;
                if (traceBinds) {
                    _puts("dyld: bind pass done pass=");
                    _putdec(pass);
                    _puts(" binds=");
                    _putdec((int)bindCount);
                    _puts(" image=");
                    _puts(img->path);
                    _puts("\n");
                }
            }
        }
        cmd += lc->cmdsize;
    }
}

// ─── Apply chained fixups (modern format) ────────────────────
#ifndef LC_DYLD_CHAINED_FIXUPS
#define LC_DYLD_CHAINED_FIXUPS 0x80000034
#endif
#define DYLD_CHAINED_PTR_64         2
#define DYLD_CHAINED_PTR_64_OFFSET  6
#define DYLD_CHAINED_PTR_START_NONE 0xFFFF
#define DYLD_CHAINED_IMPORT        1
#define DYLD_CHAINED_IMPORT_ADDEND 2

struct dyld_chained_fixups_header {
    uint32_t fixups_version;
    uint32_t starts_offset;
    uint32_t imports_offset;
    uint32_t symbols_offset;
    uint32_t imports_count;
    uint32_t imports_format;
    uint32_t symbols_format;
};

struct dyld_chained_starts_in_image {
    uint32_t seg_count;
    uint32_t seg_info_offset[1];
};

struct dyld_chained_starts_in_segment {
    uint32_t size;
    uint16_t page_size;
    uint16_t pointer_format;
    uint64_t segment_offset;
    uint32_t max_valid_pointer;
    uint16_t page_count;
    uint16_t page_start[1];
};

struct dyld_chained_import {
    uint32_t lib_ordinal :  8;
    uint32_t weak_import :  1;
    uint32_t name_offset : 23;
};

static int chainedImportOrdinal(uint8_t libOrdinal) {
    return libOrdinal > 0xF0 ? (int)(int8_t)libOrdinal : (int)libOrdinal;
}

static bool rangeWithin(const uint8_t* base, const uint8_t* end,
                        const void* ptr, uint64_t size) {
    const uint8_t* p = (const uint8_t*)ptr;
    return p >= base && p <= end && size <= (uint64_t)(end - p);
}

static void applyChainedFixups(LoadedImage* img) {
    const uint8_t* cmd = (const uint8_t*)img->header + sizeof(mach_header_64);
    const segment_command_64* textSeg = findSegment(img->header, SEG_TEXT);
    if (!textSeg) return;
    const bool traceChained = sDyldVerbose && img->path &&
        (_contains(img->path, "sshd") || _contains(img->path, "libcrypto"));
    const bool tracePantheraChained =
        (sPantheraDyldTrace || sPantheraDyldPhaseTrace) && img->path &&
        (_contains(img->path, "sshd") || _contains(img->path, "libcrypto"));

    for (uint32_t i = 0; i < img->header->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_DYLD_CHAINED_FIXUPS) {
            // The linkedit_data_command points to the fixup data
            struct { uint32_t cmd; uint32_t cmdsize; uint32_t dataoff; uint32_t datasize; } *ldc =
                (decltype(ldc))cmd;

            // Fixup data is at file offset dataoff — translate via linkedit
            const segment_command_64* linkSeg = findSegment(img->header, SEG_LINKEDIT);
            if (!linkSeg) break;
            uintptr_t linkeditBase = linkeditBaseForImage(*img, textSeg, linkSeg);
            if (linkeditBase == 0) break;
            if (ldc->dataoff < linkSeg->fileoff ||
                (uint64_t)ldc->dataoff + ldc->datasize >
                    (uint64_t)linkSeg->fileoff + linkSeg->filesize ||
                ldc->datasize < sizeof(dyld_chained_fixups_header)) {
                _puts("dyld: malformed chained fixups bounds image=");
                _puts(img->path ? img->path : "<null>");
                _puts("\n");
                break;
            }
            const uint8_t* fixupData = (const uint8_t*)(linkeditBase + ldc->dataoff);
            const uint8_t* fixupEnd = fixupData + ldc->datasize;

            const dyld_chained_fixups_header* header = (const dyld_chained_fixups_header*)fixupData;
            if (header->starts_offset >= ldc->datasize ||
                header->imports_offset > ldc->datasize ||
                header->symbols_offset >= ldc->datasize ||
                (uint64_t)header->imports_offset +
                    (uint64_t)header->imports_count * sizeof(dyld_chained_import) >
                        ldc->datasize) {
                _puts("dyld: malformed chained fixups header image=");
                _puts(img->path ? img->path : "<null>");
                _puts("\n");
                break;
            }
            const dyld_chained_starts_in_image* starts =
                (const dyld_chained_starts_in_image*)(fixupData + header->starts_offset);
            if (!rangeWithin(fixupData, fixupEnd, starts, sizeof(uint32_t))) {
                _puts("dyld: malformed chained starts image=");
                _puts(img->path ? img->path : "<null>");
                _puts("\n");
                break;
            }
            if (starts->seg_count > 32 ||
                !rangeWithin(fixupData, fixupEnd, starts,
                    sizeof(uint32_t) + (uint64_t)starts->seg_count * sizeof(uint32_t))) {
                _puts("dyld: malformed chained starts count image=");
                _puts(img->path ? img->path : "<null>");
                _puts("\n");
                break;
            }

            // Import table
            const uint8_t* importsBase = fixupData + header->imports_offset;
            const char* symbolStrings = (const char*)(fixupData + header->symbols_offset);
            const uint8_t* symbolStringsEnd = fixupEnd;
            if (tracePantheraChained) {
                _puts("PANTHERA:dyld chained-enter image=");
                _puts(img->path ? img->path : "<null>");
                _puts(" size=");
                _putdec((int)ldc->datasize);
                _puts(" imports=");
                _putdec((int)header->imports_count);
                _puts(" segs=");
                _putdec((int)starts->seg_count);
                _puts("\n");
            }

            // Process each segment
            for (uint32_t seg = 0; seg < starts->seg_count; seg++) {
                if (starts->seg_info_offset[seg] == 0) continue;

                const dyld_chained_starts_in_segment* segInfo =
                    (const dyld_chained_starts_in_segment*)((const uint8_t*)starts + starts->seg_info_offset[seg]);
                if (!rangeWithin(fixupData, fixupEnd, segInfo, sizeof(dyld_chained_starts_in_segment)) ||
                    segInfo->size < sizeof(dyld_chained_starts_in_segment) ||
                    !rangeWithin(fixupData, fixupEnd, segInfo, segInfo->size)) {
                    _puts("dyld: malformed chained segment info image=");
                    _puts(img->path ? img->path : "<null>");
                    _puts(" seg=");
                    _putdec((int)seg);
                    _puts("\n");
                    continue;
                }
                uint64_t pageStartsSize = offsetof(dyld_chained_starts_in_segment, page_start) +
                    (uint64_t)segInfo->page_count * sizeof(uint16_t);
                if (pageStartsSize > segInfo->size || segInfo->page_size == 0) {
                    _puts("dyld: malformed chained page starts image=");
                    _puts(img->path ? img->path : "<null>");
                    _puts(" seg=");
                    _putdec((int)seg);
                    _puts("\n");
                    continue;
                }

                // Find this segment's base address in our loaded image
                uintptr_t segBase = (uintptr_t)img->header + segInfo->segment_offset;
                const segment_command_64* fixupSeg =
                    findSegmentByImageOffset(*img, segInfo->segment_offset);
                int restoreProt = fixupSeg ? (int)fixupSeg->initprot : (PROT_READ | PROT_WRITE);
                restoreProt &= (PROT_READ | PROT_WRITE | PROT_EXEC);
                if (restoreProt == 0) {
                    restoreProt = PROT_READ;
                }
                if (tracePantheraChained) {
                    _puts("PANTHERA:dyld chained-seg image=");
                    _puts(img->path ? img->path : "<null>");
                    _puts(" seg=");
                    _putdec((int)seg);
                    _puts(" pages=");
                    _putdec((int)segInfo->page_count);
                    _puts(" fmt=");
                    _putdec((int)segInfo->pointer_format);
                    _puts("\n");
                }
                if (traceChained) {
                    _puts("dyld: chained seg=");
                    _putdec((int)seg);
                    _puts(" pages=");
                    _putdec((int)segInfo->page_count);
                    _puts(" fmt=");
                    _putdec((int)segInfo->pointer_format);
                    _puts(" image=");
                    _puts(img->path);
                    _puts("\n");
                }

                for (uint16_t page = 0; page < segInfo->page_count; page++) {
                    uint16_t pageStart = segInfo->page_start[page];
                    if (pageStart == DYLD_CHAINED_PTR_START_NONE) continue;
                    if (pageStart & 0x8000) {
                        _puts("dyld: unsupported multi-start chained fixup page image=");
                        _puts(img->path ? img->path : "<null>");
                        _puts(" seg=");
                        _putdec((int)seg);
                        _puts(" page=");
                        _putdec((int)page);
                        _puts("\n");
                        continue;
                    }
                    if ((tracePantheraChained && _contains(img->path, "libcrypto") &&
                         ((page & 0x0f) == 0)) ||
                        (traceChained && ((page & 0x0f) == 0))) {
                        _puts("dyld: chained page begin seg=");
                        _putdec((int)seg);
                        _puts(" page=");
                        _putdec((int)page);
                        _puts(" image=");
                        _puts(img->path);
                        _puts("\n");
                    }

                    uintptr_t pageAddr = segBase + (page * segInfo->page_size);
                    uintptr_t pageEnd = pageAddr + segInfo->page_size;
                    uintptr_t loc = pageAddr + pageStart;
                    uint32_t steps = 0;
                    uint32_t maxSteps = (segInfo->page_size / 4) + 1;
                    uintptr_t protectStart = pageRoundDown(pageAddr);
                    uintptr_t protectEnd = pageRoundUp(pageEnd);
                    size_t protectSize = protectEnd > protectStart ?
                        (size_t)(protectEnd - protectStart) : (size_t)segInfo->page_size;
                    int writeProt = restoreProt | PROT_WRITE;
                    long protectResult = sys_mprotect((void*)protectStart, protectSize, writeProt);
                    if (tracePantheraChained && protectResult < 0) {
                        _puts("PANTHERA:dyld chained-page-protect-failed image=");
                        _puts(img->path ? img->path : "<null>");
                        _puts(" seg=");
                        _putdec((int)seg);
                        _puts(" page=");
                        _putdec((int)page);
                        _puts(" result=");
                        _putdec((int)protectResult);
                        _puts("\n");
                    }

                    // Walk the chain
                    while (true) {
                        if (loc < pageAddr || loc + sizeof(uint64_t) > pageEnd) {
                            _puts("dyld: malformed chained fixup address image=");
                            _puts(img->path ? img->path : "<null>");
                            _puts(" seg=");
                            _putdec((int)seg);
                            _puts(" page=");
                            _putdec((int)page);
                            _puts("\n");
                            break;
                        }
                        uint64_t rawValue = *(uint64_t*)loc;
                        bool isBind = (rawValue >> 63) & 1;
                        uint32_t next = (rawValue >> 51) & 0xFFF; // 12-bit next, 4-byte stride
                        steps++;
                        if (steps > maxSteps) {
                            _puts("dyld: malformed chained fixup loop image=");
                            _puts(img->path ? img->path : "<null>");
                            _puts(" seg=");
                            _putdec((int)seg);
                            _puts(" page=");
                            _putdec((int)page);
                            _puts("\n");
                            break;
                        }

                        if (isBind) {
                            // Bind — resolve symbol
                            uint32_t ordinal = rawValue & 0xFFFFFF; // 24-bit ordinal
                            int8_t addend = (rawValue >> 24) & 0xFF;

                            if (ordinal < header->imports_count) {
                                const dyld_chained_import* imp =
                                    (const dyld_chained_import*)(importsBase + ordinal * sizeof(dyld_chained_import));
                                if (!rangeWithin(fixupData, fixupEnd, imp, sizeof(dyld_chained_import)) ||
                                    imp->name_offset >= (uint32_t)(symbolStringsEnd - (const uint8_t*)symbolStrings)) {
                                    reportUnresolvedBind(img, "<malformed chained import>", 0);
                                    *(uint64_t*)loc = 0;
                                    if (next == 0) break;
                                    loc += next * 4;
                                    continue;
                                }
                                const char* symName = symbolStrings + imp->name_offset;
                                if (_strnlen(symName, (size_t)(symbolStringsEnd - (const uint8_t*)symName)) ==
                                    (size_t)(symbolStringsEnd - (const uint8_t*)symName)) {
                                    reportUnresolvedBind(img, "<unterminated chained import>",
                                        chainedImportOrdinal((uint8_t)imp->lib_ordinal));
                                    *(uint64_t*)loc = 0;
                                    if (next == 0) break;
                                    loc += next * 4;
                                    continue;
                                }
                                int libOrdinal = chainedImportOrdinal((uint8_t)imp->lib_ordinal);
                                const bool tracePantheraSymbol =
                                    _panthera_trace_chained_symbol(symName);
                                const bool tracePantheraBind =
                                    tracePantheraSymbol ||
                                    (sPantheraDyldTraceBinds && img->path &&
                                     _contains(img->path, "sshd"));
                                const bool tracePantheraEarlyBind =
                                    tracePantheraChained && img->path &&
                                    _contains(img->path, "sshd") &&
                                    page == 0 && steps <= 16;
                                if (traceChained && page == 0 && steps <= 8) {
                                    _puts("dyld: chained bind step=");
                                    _putdec((int)steps);
                                    _puts(" ordinal=");
                                    _putdec((int)ordinal);
                                    _puts(" symbol=");
                                    _puts(symName ? symName : "<null>");
                                    _puts("\n");
                                }
                                if (tracePantheraBind || tracePantheraEarlyBind) {
                                    _puts("PANTHERA:dyld chained bind resolve-enter symbol=");
                                    _puts(symName ? symName : "<null>");
                                    _puts(" import=");
                                    _putdec((int)ordinal);
                                    _puts(" step=");
                                    _putdec((int)steps);
                                    _puts(" page=");
                                    _putdec((int)page);
                                    _puts(" libOrdinal=");
                                    _putdec(libOrdinal);
                                    _puts(" loc=");
                                    _puthex((uint64_t)loc);
                                    _puts(" raw=");
                                    _puthex(rawValue);
                                    _puts(" importer=");
                                    _puts(img->path ? img->path : "<null>");
                                    _puts("\n");
                                }
                                uint64_t addr = resolveSymbol(symName, libOrdinal, false, img);
                                if (tracePantheraBind || tracePantheraEarlyBind) {
                                    _puts("PANTHERA:dyld chained bind resolve-return symbol=");
                                    _puts(symName ? symName : "<null>");
                                    _puts(" import=");
                                    _putdec((int)ordinal);
                                    _puts(" step=");
                                    _putdec((int)steps);
                                    _puts(" page=");
                                    _putdec((int)page);
                                    _puts(" libOrdinal=");
                                    _putdec(libOrdinal);
                                    _puts(" loc=");
                                    _puthex((uint64_t)loc);
                                    _puts(" raw=");
                                    _puthex(rawValue);
                                    _puts(" resolved=");
                                    _puthex(addr);
                                    _puts(" importer=");
                                    _puts(img->path ? img->path : "<null>");
                                    _puts("\n");
                                }
                                if (tracePantheraBind || tracePantheraEarlyBind) {
                                    _puts("PANTHERA:dyld chained bind store-enter symbol=");
                                    _puts(symName ? symName : "<null>");
                                    _puts(" value=");
                                    _puthex(addr ? addr + addend : 0);
                                    _puts(" loc=");
                                    _puthex((uint64_t)loc);
                                    _puts("\n");
                                }
                                if (addr) {
                                    *(uint64_t*)loc = addr + addend;
                                } else if (imp->weak_import) {
                                    *(uint64_t*)loc = 0;
                                } else {
                                    reportUnresolvedBind(img, symName, libOrdinal);
                                    *(uint64_t*)loc = 0;
                                }
                                if (tracePantheraBind || tracePantheraEarlyBind) {
                                    _puts("PANTHERA:dyld chained bind wrote symbol=");
                                    _puts(symName ? symName : "<null>");
                                    _puts(" value=");
                                    _puthex(*(uint64_t*)loc);
                                    _puts(" loc=");
                                    _puthex((uint64_t)loc);
                                    _puts("\n");
                                }
                            }
                        } else {
                            // Rebase — adjust pointer
                            if (segInfo->pointer_format == DYLD_CHAINED_PTR_64 ||
                                segInfo->pointer_format == DYLD_CHAINED_PTR_64_OFFSET) {
                                uint64_t target = rawValue & 0xFFFFFFFFF; // 36-bit target
                                uint8_t high8 = (rawValue >> 36) & 0xFF;

                                uint64_t newAddr;
                                if (segInfo->pointer_format == DYLD_CHAINED_PTR_64) {
                                    // target is vmaddr
                                    newAddr = target + img->slide;
                                } else {
                                    // target is runtime offset from image base
                                    newAddr = (uintptr_t)img->header + target;
                                }
                                newAddr |= ((uint64_t)high8 << 56);
                                *(uint64_t*)loc = newAddr;
                            }
                        }

                        if (next == 0) break;
                        loc += next * 4; // stride is 4 bytes
                    }
                    if (protectResult >= 0 && restoreProt != writeProt) {
                        sys_mprotect((void*)protectStart, protectSize, restoreProt);
                    }
                    if ((tracePantheraChained && _contains(img->path, "libcrypto") &&
                         ((page & 0x0f) == 0)) ||
                        (traceChained && ((page & 0x0f) == 0))) {
                        _puts("dyld: chained page done seg=");
                        _putdec((int)seg);
                        _puts(" page=");
                        _putdec((int)page);
                        _puts(" steps=");
                        _putdec((int)steps);
                        _puts(" image=");
                        _puts(img->path);
                        _puts("\n");
                    }
                }
            }
        }
        cmd += lc->cmdsize;
    }
}

// ─── Entry point (called from dyldStartup.s) ────────────────
/*
 * Entry point called from dyldStartup.s
 * RDI = pointer to KernelArgs on stack:
 *   [mainExecutable mach_header*]
 *   [argc]
 *   [argv[0]] [argv[1]] ... [NULL]
 *   [envp[0]] [envp[1]] ... [NULL]
 *   [apple[0]] [apple[1]] ... [NULL]
 */
static __attribute__((noreturn)) void panthera_dyld_start_impl(const void* kernArgs);

extern "C" __attribute__((noreturn)) void start(const void* kernArgs) {
    // kernArgs = original RSP from __dyld_start, pointing to kernel stack
    sStartupT0 = _now_us();
    _phase("dyld: start enter");

    const size_t scratchStackSize = 1 << 20;
    void* scratchStack = (void*)sys_mmap(0, scratchStackSize,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if ((long)scratchStack > 0) {
        uintptr_t stackTop = (((uintptr_t)scratchStack + scratchStackSize) - 16) & ~0xFULL;
        __asm__ volatile(
            "movq %0, %%rsp\n\t"
            "xorq %%rbp, %%rbp\n\t"
            "movq %2, %%rdi\n\t"
            "call *%1\n\t"
            :
            : "r"(stackTop), "r"(panthera_dyld_start_impl), "r"(kernArgs)
            : "rdi", "memory");
        __builtin_unreachable();
    }

    _puts("dyld: WARN: scratch stack alloc failed\n");
    panthera_dyld_start_impl(kernArgs);
}

static __attribute__((noreturn)) void panthera_dyld_start_impl(const void* kernArgs) {
    _phase("dyld: impl enter");
    const uintptr_t* sp = (const uintptr_t*)kernArgs;

    // First word is the main executable's mach_header
    const mach_header_64* mainMH = (const mach_header_64*)*sp++;
    int argc = (int)*sp++;
    const char** argv = (const char**)sp;
    sp += argc + 1; // skip argv + null
    const char** envp = (const char**)sp;
    while (*sp) sp++; sp++;
    const char** apple = (const char**)sp;
    sDyldVerbose = _envHas(envp, "DYLD_PRINT_LIBRARIES");
    sPantheraDyldPhaseTrace = _envHas(envp, "PANTHERA_DYLD_PHASE_TRACE");
    sPantheraDyldTrace = _envHas(envp, "PANTHERA_DYLD_TRACE");
    sPantheraDyldResolveTrace = _envHas(envp, "PANTHERA_DYLD_RESOLVE_TRACE");
    sPantheraDyldTraceBinds = _envHas(envp, "PANTHERA_DYLD_TRACE_BINDS");
    sPantheraDyldProgressTrace = _envHas(envp, "PANTHERA_DYLD_PROGRESS_TRACE");
    sPantheraDyldKernelSharedRegion = _envEquals(envp, "PANTHERA_DYLD_KERNEL_SHARED_REGION", "1");
    _phase("dyld: parsed initial stack");
    _panthera_phase("parsed initial stack");
    _panthera_progress("parsed-stack");

    // Verify main executable
    if (!mainMH || mainMH->magic != MH_MAGIC_64) {
        _puts("dyld: invalid main executable mach_header at ");
        _puthex((uint64_t)mainMH);
        _puts("\n");
        sys_exit(1);
    }

    // Register main executable as first image (slide = 0, kernel mapped it)
    // Compute slide: difference between runtime address and linked address
    const segment_command_64* textSeg = findSegment(mainMH, SEG_TEXT);
    intptr_t mainSlide = textSeg ? ((intptr_t)mainMH - (intptr_t)textSeg->vmaddr) : 0;

    LoadedImage* mainImg = &sImages[sNumImages++];
    mainImg->header = mainMH;
    mainImg->slide = mainSlide;
    mainImg->path = (argc > 0) ? argv[0] : "/unknown";
    mainImg->symtab = nullptr;
    mainImg->strtab = nullptr;
    mainImg->nsyms = 0;
    mainImg->strsize = 0;
    mainImg->indirectSymtab = nullptr;
    mainImg->nindirectsyms = 0;
    parseSymtab(mainImg);
    _phase("dyld: registered main image");
    _panthera_phase("registered main image");
    _panthera_progress("registered-main");

    // ── Try to load shared cache ──
    initSharedCache();
    _phase("dyld: shared cache init done");
    _panthera_phase("shared cache init done");
    _panthera_progress("shared-cache-done");

    // Load all dylib dependencies
    const uint8_t* cmd = (const uint8_t*)mainMH + sizeof(mach_header_64);
    for (uint32_t i = 0; i < mainMH->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_REEXPORT_DYLIB ||
            lc->cmd == LC_LOAD_WEAK_DYLIB) {
            const dylib_command* dc = (const dylib_command*)cmd;
            const char* depPath = (const char*)cmd + dc->dylib.name.offset;
            _phase("dyld: before load dependency");
            _panthera_phase_path("before load dependency", depPath);
            _panthera_progress_path("main-dependency-enter", depPath);
            _trace_image_phase("main dependency", depPath);
            loadDependencyImage(depPath);
            _trace_image_phase("main dependency done", depPath);
            _phase("dyld: after load dependency");
            _panthera_phase_path("after load dependency", depPath);
            _panthera_progress_path("main-dependency-done", depPath);
        }
        cmd += lc->cmdsize;
    }

    // Apply fixups to loaded images. Split-region cache images are fully
    // pre-resolved by the cache builder, so skip the entire fixup pipeline and
    // leave only non-cache images to bind against the in-place cached dylibs.
    for (int i = 0; i < sNumImages; i++) {
        bool imageFromCache = isImageFromCache(&sImages[i]);
        if (sPantheraDyldTrace) {
            _puts("PANTHERA:dyld fixups image=");
            _putdec(i);
            _puts(imageFromCache ? " cache\n" : " noncache\n");
        }
        if (sDyldVerbose) {
            _puts("dyld: fixups image ");
            _putdec(i);
            _puts(" ");
            _puts(sImages[i].path ? sImages[i].path : "<null>");
            _puts(imageFromCache ? " cache\n" : " noncache\n");
        }
        if (imageFromCache) {
            _trace_image_phase("fixups skipped (pre-resolved cache image)", sImages[i].path);
            if (sPantheraDyldTrace) {
                _puts("PANTHERA:dyld fixups skipped image=");
                _putdec(i);
                _puts("\n");
            }
            continue;
        }
        _trace_image_phase("fixups begin", sImages[i].path);
        _panthera_phase_path("fixups begin", sImages[i].path);
        _phase("dyld: before chained fixups");
        _panthera_phase_path("before chained fixups", sImages[i].path);
        _panthera_progress_path("chained-fixups-enter", sImages[i].path);
        applyChainedFixups(&sImages[i]);
        _phase("dyld: after chained fixups");
        _panthera_phase_path("after chained fixups", sImages[i].path);
        _panthera_progress_path("chained-fixups-done", sImages[i].path);
        _trace_image_phase("fixups after chained", sImages[i].path);
        applyRebases(&sImages[i]);
        _phase("dyld: after rebases");
        _panthera_phase_path("after rebases", sImages[i].path);
        _panthera_progress_path("rebases-done", sImages[i].path);
        _trace_image_phase("fixups after rebases", sImages[i].path);
        applyBinds(&sImages[i]);
        _phase("dyld: after binds");
        _panthera_phase_path("after binds", sImages[i].path);
        _panthera_progress_path("binds-done", sImages[i].path);
        if (sPantheraDyldTrace) {
            _puts("PANTHERA:dyld fixups done image=");
            _putdec(i);
            _puts("\n");
        }
    }

    publishDyldBridgeState(mainMH);
    _phase("dyld: bridge state published");
    _panthera_phase("bridge state published");
    _panthera_progress("bridge-state-published");

    // ── Initialize mach_task_self_ before malloc ──
    // mach_task_self_ is a global in libpanthera_extra set to 0.
    // Apple's malloc calls mach_vm_allocate(mach_task_self_, ...) which
    // needs a valid task port.  Call task_self_trap (Mach trap 28) directly.
    // x86_64 XNU: RAX = 0x01000000 | trap_number (class MACH=1, shift 24)
    {
        uint64_t mtsVar = resolveSymbol("_mach_task_self_");
        if (mtsVar) {
            unsigned int port;
            __asm__ volatile(
                "movl $0x0100001c, %%eax\n\t"
                "syscall"
                : "=a"(port) : : "rcx", "r11", "memory");
            *(unsigned int*)mtsVar = port;
        }
    }
    _phase("dyld: mach_task_self seeded");
    _panthera_phase("mach_task_self seeded");
    _panthera_progress("mach-task-self-seeded");

    // ── Check commpage CPU values ──
    {
        const uint8_t *commpage = (const uint8_t *)0x00007fffffe00000ULL;
        uint8_t phys = commpage[0x035];

        // If phys_cpus is 0, force it to 1 to prevent div-by-zero
        if (phys == 0) {
            // Can't write to commpage (read-only), but we can patch malloc's global
            // after __malloc_init. For now, just note it.
            sys_write(2, "commpage: phys_cpus=0! malloc will crash\n", 41);
        }
    }

    // ── Set up environ BEFORE malloc init (malloc reads env vars) ──
    initializeProgramVars(mainMH, argc, argv, envp);
    _phase("dyld: program vars initialized");
    _panthera_phase("program vars initialized");
    _panthera_progress("program-vars-initialized");

    // ── Set up GS base for TLS (pthread_self) BEFORE libkernel seeding ──
    // The libkernel seeding calls resolved dylib functions (task_self_trap,
    // mach_reply_port, task_get_special_port) that may use %gs:0 for TLS.
    // For PID 1 there is no parent to inherit TLS from, so we must set it
    // up before calling any dylib code.
    void* sTlsPage = nullptr;
    {
        enum {
            PANTHERA_PTHREAD_BOOTSTRAP_SIZE = 16384,
            PANTHERA_PTHREAD_ERRNO_OFFSET = 172,
            PANTHERA_PTHREAD_TSD_OFFSET = 224,
        };
        void* pthreadBootstrap = (void*)sys_mmap(0, PANTHERA_PTHREAD_BOOTSTRAP_SIZE,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if ((long)pthreadBootstrap > 0) {
            volatile uint64_t* vp = (volatile uint64_t*)pthreadBootstrap;
            for (int i = 0; i < PANTHERA_PTHREAD_BOOTSTRAP_SIZE / 8; i++)
                vp[i] = 0;

            /*
             * Darwin dyld's libpthread.a owns the primordial pthread_t and
             * publishes its TSD base before libSystem initializers run.  The
             * shared libpthread __pthread_init() then adopts that pthread_t.
             */
            void* tsdBase = (uint8_t*)pthreadBootstrap + PANTHERA_PTHREAD_TSD_OFFSET;
            ((void**)tsdBase)[0] = pthreadBootstrap; /* pthread_self */
            ((void**)tsdBase)[1] = (uint8_t*)pthreadBootstrap + PANTHERA_PTHREAD_ERRNO_OFFSET;
            long ret;
            __asm__ volatile(
                "movl $0x03000003, %%eax\n\t"
                "syscall"
                : "=a"(ret)
                : "D"(tsdBase)
                : "rcx", "r11", "memory");
            sTlsPage = tsdBase;
        } else {
            _puts("dyld: WARN: failed to allocate TLS page\n");
        }
    }

    // Seed the Mach globals exported by libsystem_kernel before malloc uses
    // mach_task_self() and vm_page_* during early bring-up.
    panthera_seed_libkernel_state();
    _phase("dyld: libkernel seeded");
    _panthera_phase("libkernel seeded");
    _panthera_progress("libkernel-seeded");

    // Now seed the MIG reply-port TSD slot in the TLS page
    if (sTlsPage) {
        uint64_t replyPortGlobalAddr = resolveSymbol("__task_reply_port", -1, true);
        if (replyPortGlobalAddr) {
            ((void**)sTlsPage)[2] = (void*)(uintptr_t)(*(uint32_t*)replyPortGlobalAddr);
        }
    }

    // ── Initialize __stack_chk_guard ──
    // Functions compiled with -fstack-protector check this canary value.
    // If it's zero (default), the check always fails → __stack_chk_fail → abort.
    // Set it to a fixed non-zero value. Both libpanthera_extra and libsystem_c export it.
    {
        uint64_t guardAddr = resolveSymbol("___stack_chk_guard");
        if (guardAddr) {
            *(uint64_t*)guardAddr = 0x00000afd4e9a1272ULL; // arbitrary non-zero canary
        }
        // Also set it in any other image that has its own copy
        for (int i = 0; i < sNumImages; i++) {
            uint64_t addr = findSymbolInImage(sImages[i], "___stack_chk_guard", false);
            if (addr && addr != guardAddr)
                *(uint64_t*)addr = 0x00000afd4e9a1272ULL;
        }
    }

    // ── Call __malloc_init ──
    {
        uint64_t mi = resolveSymbol("___malloc_init");
        if (mi) {
            typedef void (*MallocInitFn)(const char**);
            _phase("dyld: before malloc init");
            _panthera_phase("before malloc init");
            _panthera_progress("malloc-init-enter");
            ((MallocInitFn)mi)(apple);
            _phase("dyld: after malloc init");
            _panthera_phase("after malloc init");
            _panthera_progress("malloc-init-done");
        }
    }

    // Darwin dyld initializes libSystem before running dependent library
    // constructors.  C++ dylib constructors may use pthread-backed runtime
    // state, so discovering libSystem lazily during the generic ctor walk is
    // too late for images loaded before libSystem in Panthera's current graph.
    bool libSystemCtorDone = false;
    for (int i = 0; i < sNumImages && !libSystemCtorDone; i++) {
        const uint8_t* icmd = (const uint8_t*)sImages[i].header + sizeof(mach_header_64);
        for (uint32_t j = 0; j < sImages[i].header->ncmds && !libSystemCtorDone; j++) {
            const load_command* ilc = (const load_command*)icmd;
            if (ilc->cmd == 0x19 /* LC_SEGMENT_64 */) {
                const segment_command_64* seg = (const segment_command_64*)icmd;
                const section_64* sect = (const section_64*)(icmd + sizeof(segment_command_64));
                for (uint32_t s = 0; s < seg->nsects && !libSystemCtorDone; s++) {
                    uint8_t sectType = sect[s].flags & 0x000000FF;
                    if (sectType == 0x09 /* S_MOD_INIT_FUNC_POINTERS */) {
                        const segment_command_64* textSeg2 = findSegment(sImages[i].header, SEG_TEXT);
                        if (!textSeg2) continue;
                        uintptr_t sectAddr = (uintptr_t)sImages[i].header
                            + sect[s].addr - textSeg2->vmaddr;
                        uint64_t* funcs = (uint64_t*)sectAddr;
                        int nfuncs = sect[s].size / sizeof(uint64_t);
                        for (int f = 0; f < nfuncs; f++) {
                            if (funcs[f] && isLibSystemInitCtor(sImages[i], funcs[f])) {
                                typedef void (*InitFunc)(int, const char**, const char**, const char**, const ProgramVars*);
                                InitFunc init = (InitFunc)funcs[f];
                                _trace_ctor_event("before", sImages[i].path);
                                _phase("dyld: before libc init");
                                _panthera_phase("before libc init");
                                panthera_run_libc_init(apple);
                                _phase("dyld: after libc init");
                                _panthera_phase("after libc init");
                                _phase("dyld: before libSystem ctor");
                                _panthera_phase("before libSystem ctor");
                                _panthera_progress("libsystem-ctor-enter");
                                init(argc, argv, envp, apple, &sProgramVars);
                                _phase("dyld: after libSystem ctor");
                                _panthera_phase("after libSystem ctor");
                                _panthera_progress("libsystem-ctor-done");
                                _trace_ctor_event("after", sImages[i].path);
                                libSystemCtorDone = true;
                                break;
                            }
                        }
                    }
                    if (sectType == 0x16 /* S_INIT_FUNC_OFFSETS */) {
                        const segment_command_64* textSeg2 = findSegment(sImages[i].header, SEG_TEXT);
                        if (!textSeg2) continue;
                        uintptr_t sectAddr = (uintptr_t)sImages[i].header
                            + sect[s].addr - textSeg2->vmaddr;
                        uint32_t* offsets = (uint32_t*)sectAddr;
                        int noffsets = sect[s].size / sizeof(uint32_t);
                        for (int f = 0; f < noffsets; f++) {
                            uint64_t funcAddr = (uint64_t)sImages[i].header + offsets[f];
                            if (isLibSystemInitCtor(sImages[i], funcAddr)) {
                                typedef void (*InitFunc)(int, const char**, const char**, const char**, const ProgramVars*);
                                InitFunc init = (InitFunc)funcAddr;
                                _trace_ctor_event("before", sImages[i].path);
                                _phase("dyld: before libc init");
                                _panthera_phase("before libc init");
                                panthera_run_libc_init(apple);
                                _phase("dyld: after libc init");
                                _panthera_phase("after libc init");
                                _phase("dyld: before libSystem ctor");
                                _panthera_phase("before libSystem ctor");
                                _panthera_progress("libsystem-ctor-enter");
                                init(argc, argv, envp, apple, &sProgramVars);
                                _phase("dyld: after libSystem ctor");
                                _panthera_phase("after libSystem ctor");
                                _panthera_progress("libsystem-ctor-done");
                                _trace_ctor_event("after", sImages[i].path);
                                libSystemCtorDone = true;
                                break;
                            }
                        }
                    }
                }
            }
            icmd += ilc->cmdsize;
        }
    }

    // ── Run initializers (__mod_init_func sections) ──
    for (int i = 0; i < sNumImages; i++) {
        const uint8_t* icmd = (const uint8_t*)sImages[i].header + sizeof(mach_header_64);
        for (uint32_t j = 0; j < sImages[i].header->ncmds; j++) {
            const load_command* ilc = (const load_command*)icmd;
            if (ilc->cmd == 0x19 /* LC_SEGMENT_64 */) {
                const segment_command_64* seg = (const segment_command_64*)icmd;
                const section_64* sect = (const section_64*)(icmd + sizeof(segment_command_64));
                for (uint32_t s = 0; s < seg->nsects; s++) {
                    uint8_t sectType = sect[s].flags & 0x000000FF;
                    if (sectType == 0x09 /* S_MOD_INIT_FUNC_POINTERS */) {
                        const segment_command_64* textSeg2 = findSegment(sImages[i].header, SEG_TEXT);
                        if (!textSeg2) continue;
                        uintptr_t sectAddr = (uintptr_t)sImages[i].header
                            + sect[s].addr - textSeg2->vmaddr;
                        uint64_t* funcs = (uint64_t*)sectAddr;
                        int nfuncs = sect[s].size / sizeof(uint64_t);
                        for (int f = 0; f < nfuncs; f++) {
                            if (funcs[f]) {
                                typedef void (*InitFunc)(int, const char**, const char**, const char**, const ProgramVars*);
                                InitFunc init = (InitFunc)funcs[f];
                                _trace_ctor_event("before", sImages[i].path);
                                if (isLibSystemInitCtor(sImages[i], funcs[f])) {
                                    if (!libSystemCtorDone) {
                                        _phase("dyld: before libc init");
                                        _panthera_phase("before libc init");
                                        panthera_run_libc_init(apple);
                                        _phase("dyld: after libc init");
                                        _panthera_phase("after libc init");
                                        _phase("dyld: before libSystem ctor");
                                        _panthera_phase("before libSystem ctor");
                                        _panthera_progress("libsystem-ctor-enter");
                                        init(argc, argv, envp, apple, &sProgramVars);
                                        _phase("dyld: after libSystem ctor");
                                        _panthera_phase("after libSystem ctor");
                                        _panthera_progress("libsystem-ctor-done");
                                        libSystemCtorDone = true;
                                    }
                                    _trace_ctor_event("after", sImages[i].path);
                                    continue;
                                }
                                _phase("dyld: before ctor");
                                _panthera_progress_path("ctor-enter", sImages[i].path);
                                init(argc, argv, envp, apple, &sProgramVars);
                                _phase("dyld: after ctor");
                                _panthera_progress_path("ctor-done", sImages[i].path);
                                _trace_ctor_event("after", sImages[i].path);
                            }
                        }
                    }
                    if (sectType == 0x16 /* S_INIT_FUNC_OFFSETS */) {
                        // Modern format: 32-bit offsets from __TEXT base
                        const segment_command_64* textSeg2 = findSegment(sImages[i].header, SEG_TEXT);
                        if (!textSeg2) continue;
                        uintptr_t sectAddr = (uintptr_t)sImages[i].header
                            + sect[s].addr - textSeg2->vmaddr;
                        uint32_t* offsets = (uint32_t*)sectAddr;
                        int noffsets = sect[s].size / sizeof(uint32_t);
                        for (int f = 0; f < noffsets; f++) {
                            uint64_t funcAddr = (uint64_t)sImages[i].header + offsets[f];
                            typedef void (*InitFunc)(int, const char**, const char**, const char**, const ProgramVars*);
                            InitFunc init = (InitFunc)funcAddr;
                            _trace_ctor_event("before", sImages[i].path);
                            if (isLibSystemInitCtor(sImages[i], funcAddr)) {
                                if (!libSystemCtorDone) {
                                    _phase("dyld: before libc init");
                                    _panthera_phase("before libc init");
                                    panthera_run_libc_init(apple);
                                    _phase("dyld: after libc init");
                                    _panthera_phase("after libc init");
                                    _phase("dyld: before libSystem ctor");
                                    _panthera_phase("before libSystem ctor");
                                    _panthera_progress("libsystem-ctor-enter");
                                    init(argc, argv, envp, apple, &sProgramVars);
                                    _phase("dyld: after libSystem ctor");
                                    _panthera_phase("after libSystem ctor");
                                    _panthera_progress("libsystem-ctor-done");
                                    libSystemCtorDone = true;
                                }
                                _trace_ctor_event("after", sImages[i].path);
                                continue;
                            }
                            _phase("dyld: before ctor");
                            _panthera_progress_path("ctor-enter", sImages[i].path);
                            init(argc, argv, envp, apple, &sProgramVars);
                            _phase("dyld: after ctor");
                            _panthera_progress_path("ctor-done", sImages[i].path);
                            _trace_ctor_event("after", sImages[i].path);
                        }
                    }
                }
            }
            icmd += ilc->cmdsize;
        }
    }

    // Find entry point (LC_MAIN or LC_UNIXTHREAD)
    uint64_t entryAddr = 0;
    cmd = (const uint8_t*)mainMH + sizeof(mach_header_64);
    for (uint32_t i = 0; i < mainMH->ncmds; i++) {
        const load_command* lc = (const load_command*)cmd;
        if (lc->cmd == 0x80000028 /* LC_MAIN */) {
            const entry_point_command* ep = (const entry_point_command*)cmd;
            entryAddr = (uint64_t)mainMH + ep->entryoff + mainSlide;
            // LC_MAIN entry offset is relative to __TEXT segment start
            // But mainMH IS the __TEXT start, so entryoff is relative to it
            // Actually entryoff is from the start of __TEXT
            if (textSeg) {
                entryAddr = textSeg->vmaddr + mainSlide + ep->entryoff;
            }
            break;
        }
        if (lc->cmd == LC_UNIXTHREAD) {
            // x86_64: RIP is at offset 16*8 + 8 in the thread state
            const uint64_t* state = (const uint64_t*)((const uint8_t*)lc + 16);
            // Thread state: rax,rbx,rcx,rdx,rdi,rsi,rbp,rsp,r8-r15,rip,rflags,cs,fs,gs
            entryAddr = state[16]; // RIP
            if (mainSlide) entryAddr += mainSlide;
            break;
        }
        cmd += lc->cmdsize;
    }

    if (!entryAddr) {
        _puts("dyld: no entry point found!\n");
        sys_exit(1);
    }
    // Initialize locale: _CurrentRuneLocale must point to _DefaultRuneLocale
    uint64_t curRuneAddr = resolveSymbol("__CurrentRuneLocale");
    uint64_t defRuneAddr = resolveSymbol("__DefaultRuneLocale");
    if (curRuneAddr && defRuneAddr) {
        *(uint64_t*)curRuneAddr = defRuneAddr;
    }
    _phase("dyld: before main");
    _panthera_phase("before main");
    _panthera_progress("before-main");

    // Set up stack for main():
    // For LC_MAIN: int main(int argc, char *argv[], char *envp[], char *apple[])
    // We need to call the entry point with these args
    if (sDyldVerbose) _trace_time("ready");
    typedef int (*MainFunc)(int, const char**, const char**, const char**);
    MainFunc mainFunc = (MainFunc)entryAddr;

    int result = mainFunc(argc, argv, envp, apple);
    _phase("dyld: after main");
    _panthera_progress("after-main");

    /*
     * LC_MAIN enters the program's main function directly in Panthera's
     * simplified dyld. Darwin still terminates through libc exit(3), which
     * runs atexit handlers and flushes stdio before reaching the kernel.
     */
    if (uint64_t exitAddr = resolveSymbol("_exit", -1, true)) {
        typedef void (*ExitFunc)(int);
        ((ExitFunc)exitAddr)(result);
        __builtin_unreachable();
    }

    sys_exit(result);
    __builtin_unreachable();
}

// dyld_all_image_infos — required by debuggers
struct dyld_all_image_infos_struct {
    uint32_t version;
    uint32_t infoArrayCount;
    void*    infoArray;
};

extern "C" __attribute__((visibility("default")))
struct dyld_all_image_infos_struct dyld_all_image_infos = { 1, 0, nullptr };

// lldb notification point
extern "C" __attribute__((visibility("default")))
void __dyld_debugger_notification(void) {}

extern "C" __attribute__((visibility("default")))
void _dyld_debugger_notification(void) {}
