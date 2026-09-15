# Panthera — dyld Shared Cache Implementation

Read `CLAUDE.md`, `OS_BUILD_ROADMAP.md`, and `docs/STATUS.md` for project context before starting.

---

## The Problem

Every process launch in Panthera takes ~1 second. The cause: no dyld shared cache. Each `execve()` triggers Panthera's custom dyld (`userland/dyld/panthera_dyld.cpp`) to:

1. Open and read 12-15 individual dylib files from HFS+ disk
2. Parse Mach-O headers and load commands for each
3. Allocate and map VM ranges for each dylib's segments
4. Walk rebase opcodes — apply ASLR slide to every internal pointer
5. Walk bind opcodes — resolve every imported symbol across all images
6. Call `__mod_init_func` constructors for every library (libmalloc magazine init, libdispatch workqueue setup, libsystem_c stdio/locale init, etc.)
7. Repeat the entire sequence for every process fork — nothing is shared

On macOS, `update_dyld_shared_cache` pre-links all system dylibs into a single file at `/var/db/dyld/dyld_shared_cache_x86_64`. Dyld maps this with a single `mmap()`, skips parsing/rebasing/binding (it's all pre-computed), and jumps straight to constructors. Process startup drops from ~1 second to ~10-50 ms.

This task implements the real Apple shared cache approach for Panthera: a host-side cache builder that produces a proper `dyld_cache_format.h`-conformant cache file, and runtime support in Panthera's dyld to load from it.

---

## What We Have

### Panthera's custom dyld
- **File:** `userland/dyld/panthera_dyld.cpp` (1533 lines, compiled with `-nostdlib`, raw syscalls only)
- **Entry:** `__dyld_start` in `dyldStartup.s` → `start()` → `panthera_dyld_start_impl()`
- **Image tracking:** `struct LoadedImage` array (`sImages[MAX_IMAGES]`), stores header pointer, slide, symtab, strtab
- **Loading:** `loadImage()` — opens file, maps it, copies segments to anonymous VM, registers image, recurses on `LC_LOAD_DYLIB`/`LC_REEXPORT_DYLIB`/`LC_LOAD_WEAK_DYLIB` dependencies
- **Fixups:**
  - `applyChainedFixups()` — walks `LC_DYLD_CHAINED_FIXUPS` chains (DYLD_CHAINED_PTR_64 and DYLD_CHAINED_PTR_64_OFFSET formats), handles both bind (ordinal-based) and rebase (target + slide)
  - `applyRebases()` — walks legacy `LC_DYLD_INFO` rebase opcodes (SET_TYPE, SET_SEGMENT, ADD_ADDR, DO_REBASE_IMM_TIMES, etc.)
  - `applyBinds()` — walks legacy bind + lazy_bind opcodes (SET_DYLIB_ORDINAL, SET_SYMBOL, DO_BIND, DO_BIND_ADD_ADDR_ULEB, etc.)
- **Symbol resolution:** `resolveSymbol()` → ordinal-first via `findOrdinalImage()`, then `resolveSymbolFromImageGraph()` (walks re-exports up to depth 8), then flat namespace search
- **Initialization sequence:** seed `mach_task_self_`, set up TLS page (`thread_set_tsd_base` syscall), `panthera_seed_libkernel_state()`, `initializeProgramVars()`, `__stack_chk_guard`, `__malloc_init`, then `__mod_init_func` constructors, then `LC_MAIN` entry
- **NO shared cache support** — always loads from individual files, no awareness of cache format

### Apple cache-builder source (reference, not to be compiled directly)
- **Location:** `src/dyld-1122.1.2/cache-builder/`
- **Key reference files:**
  - `dyld_cache_format.h` — all struct definitions for the on-disk format
  - `SharedCacheBuilder.cpp` — build orchestration (sort → assign addresses → copy segments → adjust fixups → bind → optimize linkedit → build trie → write headers)
  - `AdjustDylibSegments.cpp` — split-seg and chained fixup adjustment
  - `OptimizerLinkedit.cpp` — LINKEDIT merging across dylibs
  - `doc/CacheLayout.md` — x86_64 mapping layout (TEXT at 0x7FFF20000000, DATA at 0x7FFF80000000, LINKEDIT at 0x7FFFC0000000)
  - `doc/CacheBuilder.md` — fixup tracking, GOT uniquing, patch tables
- **Runtime reference:** `src/dyld-1122.1.2/dyld/SharedCacheRuntime.cpp` — cache loading, slide info v2 rebase chain processing
- **Do NOT try to compile Apple's cache builder.** It depends on Objective-C++, Apple-internal APIs, Foundation, and the full dyld4 infrastructure. Use it as a reference for the format and algorithms only.

### System dylibs (14 total, ~3.4 MB)
```
usr/lib/libSystem.B.dylib                    (13 KB)  ← umbrella, re-exports all below
usr/lib/libiconv.2.dylib                     (959 KB)
usr/lib/libncurses.5.4.dylib                 (320 KB)
usr/lib/system/libsystem_kernel.dylib        (206 KB) [FROZEN]
usr/lib/system/libsystem_platform.dylib      (19 KB)  [FROZEN]
usr/lib/system/libsystem_malloc.dylib        (241 KB) [FROZEN]
usr/lib/system/libsystem_c.dylib             (615 KB) [FROZEN]
usr/lib/system/libsystem_info.dylib          (137 KB) [FROZEN]
usr/lib/system/libsystem_pthread.dylib       (84 KB)  [FROZEN]
usr/lib/system/libdispatch.dylib             (761 KB) [FROZEN]
usr/lib/system/libxpc.dylib                  (10 KB)  [FROZEN]
usr/lib/system/libpanthera_launchd.dylib     (14 KB)  [FROZEN]
usr/lib/system/libpanthera_extra.dylib       (60 KB)
usr/lib/system/libpanthera_patch.dylib       (12 KB)
usr/lib/system/libsystem_malloc_simple.dylib (13 KB)  [FROZEN]
```

All dylibs are x86_64 Mach-O. All were compiled with `-not_for_dyld_shared_cache` (advisory only — our builder ignores it). Most are FROZEN — they cannot be recompiled, but that doesn't matter because the cache builder reads them as-is.

---

## Cache Format Specification

The cache file must conform to `src/dyld-1122.1.2/cache-builder/dyld_cache_format.h`. The key structures for an x86_64 single-file cache:

### On-disk layout

```
┌─────────────────────────────────────────────┐ file offset 0
│ dyld_cache_header                           │ (832+ bytes, zero-padded to page boundary)
│   magic: "dyld_v1  x86_64\0"               │
│   mappingOffset → mappings[]                │
│   mappingCount: 3                           │
│   mappingWithSlideOffset → slide_mappings[] │
│   mappingWithSlideCount: 3                  │
│   imagesOffset → image_infos[]              │
│   imagesCount: N                            │
│   dylibsTrieAddr/Size → trie               │
│   sharedRegionStart: TEXT base address      │
│   sharedRegionSize: total VM span           │
│   platform: 1 (macOS)                       │
│   locallyBuiltCache: 1                      │
│   cacheType: 0 (development)               │
├─────────────────────────────────────────────┤
│ dyld_cache_mapping_info[3]                  │ (TEXT, DATA, LINKEDIT)
│ dyld_cache_mapping_and_slide_info[3]        │ (same + slide info ptrs)
│ dyld_cache_image_info[N]                    │ (one per cached dylib)
│ path strings (null-terminated, packed)      │
├─────────────────────────────────────────────┤ TEXT file offset
│ TEXT region: all dylibs' __TEXT segments     │ (packed contiguously, page-aligned per dylib)
│   dylib 0 __TEXT (mach_header + code)       │
│   dylib 1 __TEXT                            │
│   ...                                       │
├─────────────────────────────────────────────┤ DATA file offset
│ DATA region: all dylibs' __DATA segments    │ (packed contiguously, page-aligned per dylib)
│   Also includes __DATA_CONST, __DATA_DIRTY  │
├─────────────────────────────────────────────┤ LINKEDIT file offset
│ LINKEDIT region: merged metadata            │ (symbol tables, string tables, bind info, exports)
│   Per-dylib: symtab, dysymtab, bind/lazy,   │
│   exports, function starts, indirect syms   │
│   Shared string pool (deduplicated)         │
├─────────────────────────────────────────────┤
│ Slide info (dyld_cache_slide_info2)         │ (rebase chains for DATA pages)
│   page_starts[] → chain heads per page      │
│   page_extras[] → overflow chains           │
├─────────────────────────────────────────────┤
│ Dylib path trie                             │ (install name → image index)
└─────────────────────────────────────────────┘
```

### VM address layout (x86_64, per Apple's CacheLayout.md)

```
TEXT region:    0x7FFF20000000  (read/execute)
DATA region:   0x7FFF80000000  (read/write)
LINKEDIT:      0x7FFFC0000000  (read-only)
```

These are the unslid base addresses. Panthera will use a fixed slide of 0 (no ASLR for now — the cache maps at exactly these addresses).

### Slide info v2 format (x86_64)

The DATA region needs slide info so that at runtime, pointers in DATA can be adjusted by the ASLR slide. The format is `dyld_cache_slide_info2`:

```c
struct dyld_cache_slide_info2 {
    uint32_t version;           // 2
    uint32_t page_size;         // 4096
    uint32_t page_starts_offset;
    uint32_t page_starts_count;
    uint32_t page_extras_offset;
    uint32_t page_extras_count;
    uint64_t delta_mask;        // which bits contain delta to next rebase
    uint64_t value_add;         // added to all non-zero values
    // uint16_t page_starts[page_starts_count];
    // uint16_t page_extras[page_extras_count];
};
```

Each DATA page has an entry in `page_starts[]`:
- `DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE` (0x4000) — no rebases on this page
- Otherwise: byte offset ÷ 4 into the page where the first rebase chain starts

Each pointer in the chain encodes:
- **Value bits** (`value_mask = ~delta_mask`): the actual pointer value (minus `value_add`)
- **Delta bits** (`delta_mask`): distance in 4-byte units to the next pointer in the chain; 0 = end of chain

At runtime, the rebase loop is:
```
for each page:
  loc = page_base + (page_starts[i] * 4)
  while true:
    raw = *(uint64_t*)loc
    delta = (raw & delta_mask) >> delta_shift
    value = raw & value_mask
    if value != 0: value += value_add + slide
    *(uint64_t*)loc = value
    if delta == 0: break
    loc += delta  // delta is already in bytes after shifting
```

For Panthera with slide=0, the slide info is still required for format correctness, but the runtime rebase loop is a no-op since `slide == 0`.

---

## Implementation Plan

### Part 1: Host-Side Cache Builder (`tools/build_shared_cache.c`)

Write a C program that runs on the macOS build host. It reads all Panthera dylibs from the sysroot and produces a single `dyld_shared_cache_x86_64` file.

#### 1A: Dylib loading and sorting

1. Open each dylib from `userland/libsystem/build/sysroot/usr/lib/` and `usr/lib/system/`
2. Parse Mach-O headers, extract:
   - Install name (from `LC_ID_DYLIB`)
   - Dependencies (from `LC_LOAD_DYLIB`, `LC_REEXPORT_DYLIB`, `LC_LOAD_WEAK_DYLIB`)
   - All `LC_SEGMENT_64` commands (segment name, vmaddr, vmsize, fileoff, filesize, maxprot, initprot)
   - `LC_SYMTAB` (symbol table offset, count, string table offset, size)
   - `LC_DYSYMTAB` (indirect symbol table)
   - `LC_DYLD_INFO` or `LC_DYLD_INFO_ONLY` (rebase, bind, lazy_bind, export info)
   - `LC_DYLD_CHAINED_FIXUPS` (if present)
3. Topologically sort dylibs by dependency order (leaves first — libsystem_kernel before libsystem_c before libSystem.B)
4. Validate: all dylibs are x86_64 (`MH_MAGIC_64`, `CPU_TYPE_X86_64`)

#### 1B: Address space assignment

Assign each dylib's segments to the cache address space:

```
TEXT_BASE  = 0x7FFF20000000
DATA_BASE  = 0x7FFF80000000
LINK_BASE  = 0x7FFFC0000000
```

For each dylib (in sorted order):
1. Copy `__TEXT` to TEXT region at `TEXT_BASE + textCursor`. Advance `textCursor` by vmsize, page-aligned.
2. Copy `__DATA`, `__DATA_CONST`, `__DATA_DIRTY` to DATA region at `DATA_BASE + dataCursor`. Advance.
3. Record the original vmaddr and new cache vmaddr for each segment — this is the per-segment slide used to adjust pointers.

After all dylibs are placed, we know the total TEXT, DATA, and LINKEDIT sizes.

#### 1C: Segment copying

Allocate the output buffer. For each dylib, for each segment:
- Copy `filesize` bytes from the source dylib (at `segment.fileoff`) to the output buffer at the segment's assigned file offset
- Zero-fill from `filesize` to `vmsize`

The cache header and metadata go before TEXT. The file layout is:
```
[header + metadata pages] [TEXT segments] [DATA segments] [LINKEDIT] [slide info] [trie]
```

#### 1D: Rebase application

Walk each dylib's rebase information and apply the segment-to-cache slide. This is the same algorithm as `applyRebases()` and `applyChainedFixups()` in `panthera_dyld.cpp`, but applied to the cache buffer at build time.

For each dylib, compute per-segment slides:
```
segSlide[i] = newCacheAddr[i] - originalVmaddr[i]
```

For legacy rebase opcodes (`LC_DYLD_INFO`):
- Walk the rebase opcode stream (same as `applyRebases()` in the current dyld)
- For each rebase location: the pointer at that location contains an address in the dylib's original address space. Add the appropriate segment's slide to convert it to a cache address.
- Write the adjusted pointer into the cache buffer, encoding it in slide info v2 format:
  - Low bits: the cache address (minus `value_add`)
  - Delta bits: offset to next rebase location on this page (in 4-byte units)

For chained fixups (`LC_DYLD_CHAINED_FIXUPS`):
- Walk the fixup chains (same algorithm as `applyChainedFixups()`)
- For rebase entries: extract the target address, apply segment slide, write as a plain pointer (chained format is unwound into slide-info-v2 format in the cache)
- For bind entries: resolve the symbol (see 1E), write the resolved address

**Important:** In the output cache, DATA segment pointers are NOT in chained fixup format. They are plain 64-bit pointers with slide-info-v2 delta encoding overlaid. The chained fixup chains from the source dylibs are fully unwound during cache building.

#### 1E: Bind resolution

Walk each dylib's bind information and resolve all symbol references:

For legacy bind opcodes (`LC_DYLD_INFO`):
- Parse the bind and lazy_bind opcode streams (same as `applyBinds()`)
- For each DO_BIND: resolve the symbol name against the target dylib's export trie or symbol table
- Write the resolved cache address (+ addend) into the bind location in the cache DATA buffer
- Record this location as a rebase (it now contains a pointer that needs slide-info tracking)

For chained fixup binds:
- Walk the chains, identify bind entries (bit 63 set)
- Look up the import's symbol name, resolve against the ordinal dylib
- Write the resolved address

Symbol resolution at build time: scan the target dylib's `LC_SYMTAB` for the symbol name, same algorithm as `findSymbolInImage()`. For re-exports, follow `LC_REEXPORT_DYLIB` chains. The builder has all dylibs loaded, so resolution is straightforward.

#### 1F: Slide info v2 generation

After all rebases and binds are applied, generate the slide info for the DATA region:

1. Collect all pointer locations in the DATA region that contain addresses needing slide
2. Sort them by page
3. For each 4096-byte page:
   - If no pointers: `page_starts[i] = DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE`
   - Otherwise: build a chain through all pointer locations on the page
   - Each pointer stores: `(value - value_add) | (delta_to_next << delta_shift)`
   - `page_starts[i] = first_pointer_offset / 4`
4. Choose `delta_mask` and `value_add`:
   - Apple uses `delta_mask = 0x00FF000000000000` for x86_64 (bits 48-55 carry the delta)
   - `value_add = 0` for typical x86_64 caches
   - `delta_shift = __builtin_ctzll(delta_mask) - 2 = 46` (for 4-byte stride)

Write the `dyld_cache_slide_info2` struct followed by `page_starts[]` and `page_extras[]` arrays at the end of the cache file, after LINKEDIT.

#### 1G: LINKEDIT merging

Merge all dylibs' LINKEDIT data into a single contiguous LINKEDIT region:

1. For each dylib, in order:
   - Copy its bind info (if present — needed for potential runtime overrides)
   - Copy its lazy bind info
   - Copy its weak bind info
   - Copy its export info
   - Copy its symbol table (`nlist_64[]`), adjusting `n_value` fields by the segment slide
   - Copy its indirect symbol table
   - Copy its function starts data
2. Build a shared string pool: collect all symbol name strings, deduplicate, build a mapping from old string offsets to new pool offsets
3. Update each dylib's `nlist_64` entries to reference the shared string pool
4. Update each dylib's load commands in the cache to point to the merged LINKEDIT:
   - `LC_SEGMENT_64(__LINKEDIT)`: vmaddr, vmsize, fileoff, filesize → merged LINKEDIT region
   - `LC_SYMTAB`: symoff, nsyms, stroff, strsize → new positions in merged LINKEDIT
   - `LC_DYSYMTAB`: indirectsymoff → new position
   - `LC_DYLD_INFO`: bind_off/size, lazy_bind_off/size, export_off/size → new positions
   - `LC_DYLD_INFO`: rebase_off=0, rebase_size=0 (rebases are applied; no longer needed)

#### 1H: Dylib path trie

Build a trie mapping install name paths to image indices. This allows O(path-length) lookup at runtime instead of linear scan.

Trie format (same as export trie format in Mach-O):
- Each node has a list of edges (byte sequences) leading to child nodes
- Terminal nodes contain a ULEB128-encoded image index
- The root node branches on the first character of each install name

Write the trie into the LINKEDIT region (or after slide info). Record its address and size in the cache header's `dylibsTrieAddr` and `dylibsTrieSize`.

#### 1I: Cache header

Write the `dyld_cache_header` with all fields populated:

```c
header.magic = "dyld_v1  x86_64\0"  // exactly 16 bytes
header.mappingOffset = offsetof(metadata, mapping_infos)
header.mappingCount = 3
header.imagesOffset = offsetof(metadata, image_infos)
header.imagesCount = N  // number of cached dylibs
header.dyldBaseAddress = 0
header.codeSignatureOffset = 0
header.codeSignatureSize = 0
header.sharedRegionStart = 0x7FFF20000000  // TEXT base
header.sharedRegionSize = (LINKEDIT_end - TEXT_base)
header.maxSlide = 0  // no ASLR for now
header.platform = 1  // PLATFORM_MACOS
header.locallyBuiltCache = 1
header.cacheType = 0  // development
header.mappingWithSlideOffset = offsetof(metadata, slide_mapping_infos)
header.mappingWithSlideCount = 3
header.dylibsTrieAddr = trie_cache_address
header.dylibsTrieSize = trie_size
header.uuid = <generate random or content-hash>
// Zero all unused/ObjC/Swift/closure/Rosetta/subCache fields
```

Write three `dyld_cache_mapping_info` entries:
```
[0] TEXT:     address=TEXT_BASE,  size=textSize,  fileOffset=textFileOff, prot=r-x
[1] DATA:     address=DATA_BASE,  size=dataSize,  fileOffset=dataFileOff, prot=rw-
[2] LINKEDIT: address=LINK_BASE, size=linkSize,  fileOffset=linkFileOff, prot=r--
```

Write three `dyld_cache_mapping_and_slide_info` entries (same as above plus slide info pointers):
```
[0] TEXT:     slideInfoFileOffset=0, slideInfoFileSize=0, flags=0
[1] DATA:     slideInfoFileOffset=slideInfoOff, slideInfoFileSize=slideInfoSz, flags=0
[2] LINKEDIT: slideInfoFileOffset=0, slideInfoFileSize=0, flags=0
```

Write N `dyld_cache_image_info` entries:
```
for each dylib:
  .address = dylib's __TEXT vmaddr in cache (unslid)
  .modTime = source file mtime
  .inode = source file inode
  .pathFileOffset = file offset to install name string in header metadata
```

#### 1J: Output

Write the complete cache buffer to a file. The builder should be invoked as:
```bash
tools/build_shared_cache \
    --sysroot userland/libsystem/build/sysroot \
    --output images/shared_cache/dyld_shared_cache_x86_64
```

Print a summary: number of dylibs cached, total cache size, TEXT/DATA/LINKEDIT sizes.

---

### Part 2: Runtime Support in Panthera's dyld

Modify `userland/dyld/panthera_dyld.cpp` to detect, map, and use the shared cache.

#### 2A: Cache detection and mapping

Add to the beginning of `panthera_dyld_start_impl()`, before the existing dependency loading loop:

```cpp
#define CACHE_PATH "/var/db/dyld/dyld_shared_cache_x86_64"

static void*                    sCacheMapping = nullptr;
static uint64_t                 sCacheSize = 0;
static const dyld_cache_header* sCacheHeader = nullptr;

// Parsed from cache header for fast access:
static const dyld_cache_image_info*   sCacheImages = nullptr;
static uint32_t                       sCacheImageCount = 0;
static const dyld_cache_mapping_info* sCacheMappings = nullptr;
```

At startup, before loading any dylibs:
1. `sys_open(CACHE_PATH, O_RDONLY)` — if fails, fall back to existing per-file loading
2. `sys_fstat()` to get file size
3. `sys_mmap()` the entire file with `PROT_READ | PROT_WRITE` and `MAP_PRIVATE` (private writable copy — DATA needs to be writable, MAP_PRIVATE gives COW)
4. Validate: check `magic == "dyld_v1  x86_64"`, `mappingCount >= 3`
5. Parse the three mappings (TEXT, DATA, LINKEDIT) — store base addresses and sizes
6. Parse `imagesOffset`/`imagesCount` into `sCacheImages`/`sCacheImageCount`

**Note on mapping strategy:** Apple's kernel maps the cache with `__shared_region_map_and_slide_2_np()` which puts TEXT/DATA/LINKEDIT at their assigned VM addresses across all processes. Panthera's XNU may or may not support this syscall. The simpler approach: `mmap()` the entire cache file to any address, then compute a "cache slide" (difference between actual map address and the header's `sharedRegionStart`). All cache addresses are then adjusted by this slide. This is equivalent to ASLR.

```cpp
intptr_t sCacheSlide = (intptr_t)sCacheMapping - (intptr_t)sCacheHeader->sharedRegionStart;
```

Alternatively, try to map at the exact address:
```cpp
void* mapped = (void*)sys_mmap(
    (void*)0x7FFF20000000,  // hint: map at intended address
    fileSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, fd, 0);
```
If `MAP_FIXED` succeeds, `sCacheSlide = 0`. If it fails (address in use), fall back to any-address mapping with computed slide.

#### 2B: Apply slide info (if slide != 0)

If the cache was mapped at an address other than `sharedRegionStart`, apply slide info v2 to rebase all DATA pointers:

```cpp
static void applyCacheSlideInfo() {
    if (sCacheSlide == 0) return;
    
    // Find DATA mapping's slide info
    const dyld_cache_mapping_and_slide_info* slideMappings = 
        (const dyld_cache_mapping_and_slide_info*)((uint8_t*)sCacheHeader + sCacheHeader->mappingWithSlideOffset);
    
    for (uint32_t m = 0; m < sCacheHeader->mappingWithSlideCount; m++) {
        if (slideMappings[m].slideInfoFileSize == 0) continue;
        
        const dyld_cache_slide_info2* slideInfo = 
            (const dyld_cache_slide_info2*)((uint8_t*)sCacheMapping + slideMappings[m].slideInfoFileOffset);
        
        if (slideInfo->version != 2) continue;
        
        uint8_t* dataRegion = (uint8_t*)sCacheMapping + slideMappings[m].fileOffset;
        const uint16_t* pageStarts = (const uint16_t*)((uint8_t*)slideInfo + slideInfo->page_starts_offset);
        const uint16_t* pageExtras = (const uint16_t*)((uint8_t*)slideInfo + slideInfo->page_extras_offset);
        
        uint64_t deltaMask = slideInfo->delta_mask;
        uint64_t valueMask = ~deltaMask;
        uint64_t valueAdd = slideInfo->value_add;
        int deltaShift = __builtin_ctzll(deltaMask) - 2;
        
        for (uint32_t i = 0; i < slideInfo->page_starts_count; i++) {
            uint16_t entry = pageStarts[i];
            if (entry == DYLD_CACHE_SLIDE_PAGE_ATTR_NO_REBASE) continue;
            
            uint8_t* page = dataRegion + (i * slideInfo->page_size);
            
            if (entry & DYLD_CACHE_SLIDE_PAGE_ATTR_EXTRA) {
                // Multi-start page — walk page_extras
                uint16_t chainIndex = entry & 0x3FFF;
                do {
                    uint16_t pExtra = pageExtras[chainIndex];
                    uint32_t pageOff = (pExtra & 0x3FFF) * 4;
                    rebaseChainV2(page, pageOff, sCacheSlide, deltaMask, valueMask, valueAdd, deltaShift);
                    if (pExtra & DYLD_CACHE_SLIDE_PAGE_ATTR_END) break;
                    chainIndex++;
                } while (true);
            } else {
                rebaseChainV2(page, entry * 4, sCacheSlide, deltaMask, valueMask, valueAdd, deltaShift);
            }
        }
    }
}

static void rebaseChainV2(uint8_t* page, uint32_t startOffset, intptr_t slide,
                          uint64_t deltaMask, uint64_t valueMask, uint64_t valueAdd, int deltaShift) {
    uint32_t pageOff = startOffset;
    uint32_t delta = 1;
    while (delta != 0) {
        uint64_t* loc = (uint64_t*)(page + pageOff);
        uint64_t raw = *loc;
        delta = (uint32_t)((raw & deltaMask) >> deltaShift);
        uint64_t value = raw & valueMask;
        if (value != 0) {
            value += valueAdd;
            value += slide;
        }
        *loc = value;
        pageOff += delta;
    }
}
```

#### 2C: Image lookup from cache

When dyld needs to load a dylib (during `loadImage()` or the dependency walk), check the cache first:

```cpp
static const mach_header_64* findImageInCache(const char* path, intptr_t* slideOut) {
    if (!sCacheHeader) return nullptr;
    
    // Fast path: trie lookup
    // (Fall back to linear scan if trie not implemented yet)
    
    for (uint32_t i = 0; i < sCacheImageCount; i++) {
        const char* imgPath = (const char*)((uint8_t*)sCacheMapping + sCacheImages[i].pathFileOffset);
        if (_strcmp(imgPath, path) == 0) {
            // Image found — compute its address in our mapping
            uint64_t unslidAddr = sCacheImages[i].address;
            const mach_header_64* mh = (const mach_header_64*)((uint8_t*)sCacheMapping + 
                (unslidAddr - sCacheHeader->sharedRegionStart));
            if (slideOut) *slideOut = sCacheSlide;
            return mh;
        }
    }
    
    // Try basename match (some paths use /usr/lib/system/ vs /usr/lib/)
    const char* wantBase = _basename(path);
    for (uint32_t i = 0; i < sCacheImageCount; i++) {
        const char* imgPath = (const char*)((uint8_t*)sCacheMapping + sCacheImages[i].pathFileOffset);
        if (_strcmp(_basename(imgPath), wantBase) == 0) {
            uint64_t unslidAddr = sCacheImages[i].address;
            const mach_header_64* mh = (const mach_header_64*)((uint8_t*)sCacheMapping + 
                (unslidAddr - sCacheHeader->sharedRegionStart));
            if (slideOut) *slideOut = sCacheSlide;
            return mh;
        }
    }
    
    return nullptr;
}
```

#### 2D: Modified loadImage() with cache support

Replace the dylib loading path to check the cache first:

```cpp
static LoadedImage* loadImage(const char* path) {
    // Check if already loaded
    for (int i = 0; i < sNumImages; i++) {
        if (sImages[i].path && _strcmp(sImages[i].path, path) == 0)
            return &sImages[i];
        // Also check basename
        if (sImages[i].path && _strcmp(_basename(sImages[i].path), _basename(path)) == 0)
            return &sImages[i];
    }
    if (sNumImages >= MAX_IMAGES) {
        _puts("dyld: too many images\n");
        return nullptr;
    }

    // Try shared cache first
    intptr_t cacheSlide = 0;
    const mach_header_64* cachedMH = findImageInCache(path, &cacheSlide);
    if (cachedMH) {
        LoadedImage* img = &sImages[sNumImages++];
        img->header = cachedMH;
        img->slide = cacheSlide;
        img->path = path;
        parseSymtab(img);
        
        // Load dependencies (they should also be in the cache)
        const uint8_t* cmd = (const uint8_t*)cachedMH + sizeof(mach_header_64);
        for (uint32_t i = 0; i < cachedMH->ncmds; i++) {
            const load_command* lc = (const load_command*)cmd;
            if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_REEXPORT_DYLIB ||
                lc->cmd == LC_LOAD_WEAK_DYLIB) {
                const dylib_command* dc = (const dylib_command*)cmd;
                const char* depPath = (const char*)cmd + dc->dylib.name.offset;
                loadImage(depPath);
            }
            cmd += lc->cmdsize;
        }
        
        return img;
    }

    // Fall back to file-based loading (existing code, unchanged)
    // ... existing loadImage() file-based code ...
}
```

#### 2E: Skip fixups for cached images

The critical optimization: cached images have pre-applied rebases and binds. In the fixup loop:

```cpp
// In panthera_dyld_start_impl(), the fixup loop becomes:
for (int i = 0; i < sNumImages; i++) {
    if (isImageFromCache(&sImages[i])) {
        // Cached images have pre-resolved fixups — skip rebase/bind
        continue;
    }
    applyChainedFixups(&sImages[i]);
    applyRebases(&sImages[i]);
    applyBinds(&sImages[i]);
}

static bool isImageFromCache(const LoadedImage* img) {
    if (!sCacheMapping) return false;
    uintptr_t addr = (uintptr_t)img->header;
    uintptr_t cacheStart = (uintptr_t)sCacheMapping;
    uintptr_t cacheEnd = cacheStart + sCacheSize;
    return (addr >= cacheStart && addr < cacheEnd);
}
```

**Note:** Constructors (`__mod_init_func`) still run for cached images. Pre-applying fixups only eliminates the rebase/bind processing, not initialization.

#### 2F: Trie-based image lookup (for symbol resolution speed)

Once the trie is built in Part 1, add trie walking to `findImageInCache()`:

```cpp
static int lookupTrieIndex(const char* path) {
    if (!sCacheHeader || sCacheHeader->dylibsTrieSize == 0) return -1;
    
    const uint8_t* trie = (const uint8_t*)sCacheMapping + 
        (sCacheHeader->dylibsTrieAddr - sCacheHeader->sharedRegionStart);
    const uint8_t* trieEnd = trie + sCacheHeader->dylibsTrieSize;
    const uint8_t* node = trie;
    
    while (true) {
        // Read terminal info size
        uint64_t terminalSize = *node++;
        if (terminalSize > 127) {
            // ULEB128
            node--;
            terminalSize = readULEB128_const(node);
        }
        const uint8_t* childrenStart = node + terminalSize;
        
        if (*path == '\0' && terminalSize != 0) {
            // Terminal — read image index
            return (int)readULEB128_const(node);
        }
        
        // Search children
        uint8_t childCount = *childrenStart++;
        node = childrenStart;
        bool found = false;
        for (uint8_t c = 0; c < childCount; c++) {
            const char* edge = (const char*)node;
            // Match edge string against remaining path
            const char* p = path;
            while (*node && *node == (uint8_t)*p) { node++; p++; }
            if (*node == 0) {
                // Edge matched — follow to child
                node++;
                uint64_t childOffset = readULEB128_const(node);
                node = trie + childOffset;
                path = p;
                found = true;
                break;
            }
            // Skip rest of edge string
            while (*node) node++;
            node++;
            // Skip child offset
            readULEB128_const(node);
        }
        if (!found) return -1;
    }
}
```

#### 2G: `parseSymtab()` update for merged LINKEDIT

When loading from the cache, `parseSymtab()` must handle the merged LINKEDIT. The cache builder updates each dylib's `LC_SYMTAB` and `LC_DYSYMTAB` to point into the merged LINKEDIT region. The existing `parseSymtab()` code already computes `linkeditBase` from the `__LINKEDIT` segment's vmaddr and fileoff — this should work unchanged as long as the cache builder correctly updates these load commands.

Verify that `parseSymtab()` works by checking that `img->symtab` and `img->strtab` point to valid memory within the cache mapping.

---

### Part 3: Build Integration

#### 3A: Build script for the cache builder

Create `tools/build_shared_cache.sh` (or a Makefile):

```bash
#!/bin/bash
set -euo pipefail
CC="$(xcrun -find clang)"

# Build the cache builder for the HOST (not cross-compiled)
$CC -O2 -std=c17 \
    -I src/dyld-1122.1.2/cache-builder \
    -o tools/build_shared_cache \
    tools/build_shared_cache.c \
    -lSystem

echo "Cache builder compiled: tools/build_shared_cache"
```

#### 3B: Cache generation in staging workflow

Add to the root image staging workflow:

```bash
# Generate shared cache
tools/build_shared_cache \
    --sysroot userland/libsystem/build/sysroot \
    --output images/shared_cache/dyld_shared_cache_x86_64

# Stage to root image
mkdir -p /Volumes/PantheraRoot/var/db/dyld
cp images/shared_cache/dyld_shared_cache_x86_64 \
   /Volumes/PantheraRoot/var/db/dyld/

# Also stage updated dyld
cp userland/dyld/dyld /Volumes/PantheraRoot/usr/lib/dyld
```

#### 3C: dyld rebuild

After modifying `panthera_dyld.cpp`, rebuild the dyld binary. The existing build command is in the dyld directory (check `userland/dyld/` for a Makefile or build script). The dyld is compiled with:
- `-nostdlib` — no standard library
- `-e __dyld_start` — custom entry point
- `-target x86_64-apple-darwin23.0`
- Source files: `panthera_dyld.cpp` + `dyldStartup.s`

---

### Part 4: Validation

#### 4A: Cache builder validation

The builder should print diagnostics:
```
Panthera shared cache builder
  Dylibs: 14
  TEXT region:    0x7FFF20000000 - 0x7FFF20XXXXXX (X KB)
  DATA region:    0x7FFF80000000 - 0x7FFF80XXXXXX (X KB)
  LINKEDIT region: 0x7FFFC0000000 - 0x7FFFC0XXXXXX (X KB)
  Slide info:     X pages, Y rebase locations
  Dylib trie:     X bytes
  Total cache:    X.X MB
  Output: images/shared_cache/dyld_shared_cache_x86_64
```

Verify the cache with `dyld_shared_cache_util`:
```bash
# Apple's tool (ships with Xcode) can dump our cache:
xcrun dyld_shared_cache_util -list images/shared_cache/dyld_shared_cache_x86_64
xcrun dyld_shared_cache_util -info images/shared_cache/dyld_shared_cache_x86_64
```

If Apple's tool can parse our cache, the format is correct.

#### 4B: Boot test matrix

1. **Boot WITHOUT cache** — verify existing behavior unchanged (baseline)
2. **Boot WITH cache** — verify:
   - launchd starts, zsh reaches prompt
   - External commands work: `/bin/echo HELLO`, `/bin/ls /`, `/bin/cat /etc/passwd`, `/bin/id`
   - Signal handling: Ctrl+C (SIGINT) works
   - ZLE line editor works (cursor movement, history)
3. **Boot WITH cache, then delete cache** — verify graceful fallback (dyld opens cache, fails, falls back)

#### 4C: Performance measurement

From the zsh prompt, time commands:
```sh
time /bin/echo hello
time /bin/ls /
time /bin/cat /etc/passwd
```

Expected: measurable improvement from ~1000ms to <200ms per command.

---

## Files to Create

| File | Type | Description |
|------|------|-------------|
| `tools/build_shared_cache.c` | C source | Host-side cache builder (~1500-2500 lines) |
| `tools/build_shared_cache.sh` | Shell | Build script for the cache builder |

## Files to Modify

| File | Change |
|------|--------|
| `userland/dyld/panthera_dyld.cpp` | Add cache detection, mapping, slide info, cached image loading, skip-fixups logic (~300-400 lines added) |

## Files to Stage

| Source | Destination on root image |
|--------|--------------------------|
| `images/shared_cache/dyld_shared_cache_x86_64` | `/var/db/dyld/dyld_shared_cache_x86_64` |
| Updated `userland/dyld/dyld` binary | `/usr/lib/dyld` |

---

## Critical Constraints

1. **NEVER modify frozen dylibs.** The cache builder reads them as-is. It does not recompile anything.
2. **Panthera's dyld is `-nostdlib`.** No libc, no malloc, no printf. Only raw syscalls. All new code in `panthera_dyld.cpp` must follow this constraint.
3. **The cache format must match `dyld_cache_format.h`.** Use the exact struct layouts. Apple's `dyld_shared_cache_util` tool should be able to parse the output.
4. **Backward compatibility is mandatory.** If the cache file doesn't exist or is corrupt, dyld must fall back to the existing per-file loading path. Boot must never regress.
5. **The builder runs on the macOS HOST** (arm64 or x86_64 Mac), not inside Panthera. It's a normal macOS program using libc/libSystem.
6. **x86_64 only.** No ARM support, no multi-arch, no sub-caches, no split caches. Single file, single architecture.
7. **No ObjC optimization.** Skip all ObjC/Swift metadata processing — Panthera has no ObjC runtime.
8. **No code signing.** `codeSignatureOffset = 0`, `codeSignatureSize = 0`.
9. **No closures/PrebuiltLoaderSet.** `progClosuresAddr = 0`, `dylibsPBLSetAddr = 0`. These are dyld3/4 optimizations beyond our scope.
10. **Backup the root image** before staging any changes: `cp images/qemu/panthera-root.img images/qemu/panthera-root.img.pre-shared-cache`

---

## Build and Test Sequence

```bash
# 1. Build the cache builder (on host)
bash tools/build_shared_cache.sh

# 2. Generate the shared cache
tools/build_shared_cache \
    --sysroot userland/libsystem/build/sysroot \
    --output images/shared_cache/dyld_shared_cache_x86_64

# 3. Verify with Apple's tool
xcrun dyld_shared_cache_util -list images/shared_cache/dyld_shared_cache_x86_64

# 4. Rebuild dyld with cache support
cd userland/dyld && <rebuild command> && cd ../..

# 5. Backup root image
cp images/qemu/panthera-root.img images/qemu/panthera-root.img.pre-shared-cache

# 6. Mount and stage
raw_device="$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage images/qemu/panthera-root.img | awk 'NR==1 {print $1}')"
sleep 1
diskutil mount "${raw_device}s1" 2>/dev/null
sleep 1

mkdir -p /Volumes/PantheraRoot/var/db/dyld
cp images/shared_cache/dyld_shared_cache_x86_64 /Volumes/PantheraRoot/var/db/dyld/
cp userland/dyld/dyld /Volumes/PantheraRoot/usr/lib/dyld

diskutil unmount /Volumes/PantheraRoot
hdiutil detach "$raw_device" -force

# 7. Boot test
timeout 90 boot/qemu/run_phase2_qemu.sh --no-reboot --ssh-port 0 --root-disk images/qemu/panthera-root.img
```
