# Panthera — XNU Kernel Upgrade Plan (xnu-10002 → xnu-12377)

Read `OS_BUILD_ROADMAP.md` for foundation rules.

## Why Upgrade

Panthera currently runs xnu-10002.41.9 (macOS Sonoma 14.2, Darwin 23.2). The latest available XNU source is **xnu-12377.81.4** (macOS Tahoe 26.3, Darwin 25.3, February 2026).

**macOS Tahoe is the last macOS to support Intel x86_64.** macOS 27 (expected fall 2026) drops Intel entirely. This means xnu-12377.x is the **final XNU series** with x86_64 support. Upgrading to it gives Panthera the most modern, most secure, and final-generation Intel kernel Apple will ever release.

The upgrade also picks up 2+ years of Apple kernel development: security fixes, stability improvements, performance optimizations, and driver updates.

## Version Map

| Component | Current (Sonoma 14.2) | Target (Tahoe 26.3) | GitHub Tag |
|-----------|----------------------|---------------------|------------|
| XNU | xnu-10002.41.9 | xnu-12377.81.4 | `xnu-12377.81.4` |
| Libc | Libc-1583.40.7 | Libc-1725.40.4 | `Libc-1725.40.4` |
| libmalloc | libmalloc-474.0.13 | libmalloc-792.80.2 | `libmalloc-792.80.2` |
| libdispatch | libdispatch-1462.0.4 | libdispatch-1542.0.4 | `libdispatch-1542.0.4` |
| libpthread | libpthread-518 | libpthread-539.80.3 | `libpthread-539.80.3` |
| dyld | dyld-1122.1.2 | ~dyld-1340 | Check GitHub |

Source: https://github.com/apple-oss-distributions/

## What Changed (xnu-10002 → xnu-12377)

### Manageable Changes (won't block the upgrade)

**New syscalls (~5-7):**
- Syscall 67: `oslog_coproc_reg()` — coprocessor logging
- Syscall 68: `oslog_coproc()` — coprocessor logging
- Syscall 164: `funmount(int fd, int flags)` — fd-based unmount
- Syscall 535: `objc_bp_assist_cfg_np()` — ObjC breakpoint assist
- Syscalls 556-557: `sys_coalition_policy_set/get()` — coalition management

**VM type changes:**
- `mmap`, `munmap`, `mprotect`, `madvise`, `mlock`, `munlock`, `mincore`, `minherit`, `msync`, `mremap_encrypted` changed parameter types from `caddr_t`/`size_t` to `caddr_ut`/`size_ut`. This is a type abstraction — the underlying ABI is the same for x86_64.

**Codebase growth:**
- 5,080 → 5,771 files (+14%)
- `osfmk/` grew from 1,335 to 1,486 files
- `tests/` nearly doubled (645 → 1,017)

**x86_64 fully intact:**
- 324 x86_64/i386-specific file paths (unchanged from Sonoma)
- `bsd/conf/Makefile.x86_64`, `bsd/conf/files.x86_64` present
- Full `bsd/dev/i386/` and `osfmk/i386/` trees
- New: `EXTERNAL_HEADERS/image4/coprocessor/x86.h`

### Big New Stuff (can be disabled for Panthera)

**Exclaves (49 new files):**
- Isolated security domains protected from the kernel
- Requires Apple Silicon hardware (A15+ Secure Page Table Monitor)
- On x86_64: code compiles but feature is hardware-gated, never activates
- Files: `osfmk/kern/exclaves_*.c/.h`, `bsd/vfs/vfs_exclave_fs.*`, `iokit/Exclaves/`
- **Action:** Compile with stubs or `#ifdef CONFIG_EXCLAVES 0` to disable

**Image4 Trust Framework (25 new headers):**
- Code signing / trust evaluation for coprocessors
- Files: `EXTERNAL_HEADERS/image4/` (25 headers covering AP, SEP, x86, VMA2, VMA3, Cryptex1)
- Depends on corecrypto library headers
- **Action:** Provide stub headers that define the types but stub the functions

**New subsystems (minor):**
- `bsd/kern/mem_acct.c/.h` — memory accounting
- `bsd/kern/uipc_mbuf_mcache.c` — mbuf memory cache
- Socket layer refactoring (`uipc_socket.h`, `uipc_domain.h`)
- Ariadne plists — plist-based configuration for exclaves, epoch sync

### Nothing Removed

No x86_64 paths, VFS interfaces, Mach IPC APIs, IOKit interfaces, or BSD syscalls that Panthera depends on were removed. The upgrade is additive.

## Upgrade Steps

### Step 1: Download New Source

```bash
cd src/

# XNU
git clone --depth 1 --branch xnu-12377.81.4 https://github.com/apple-oss-distributions/xnu.git xnu-12377.81.4

# Libc
git clone --depth 1 --branch Libc-1725.40.4 https://github.com/apple-oss-distributions/Libc.git Libc-1725.40.4

# libmalloc
git clone --depth 1 --branch libmalloc-792.80.2 https://github.com/apple-oss-distributions/libmalloc.git libmalloc-792.80.2

# libdispatch
git clone --depth 1 --branch libdispatch-1542.0.4 https://github.com/apple-oss-distributions/libdispatch.git libdispatch-1542.0.4

# libpthread
git clone --depth 1 --branch libpthread-539.80.3 https://github.com/apple-oss-distributions/libpthread.git libpthread-539.80.3

# dyld
git clone --depth 1 --branch dyld-1340.x.x https://github.com/apple-oss-distributions/dyld.git dyld-1340.x.x
# (check actual tag name on GitHub)
```

### Step 2: Inventory Panthera's Kernel Patches

List every modification made to `src/xnu-10002.41.9/`. These need to be ported to the new source:

| File | Patch | Purpose |
|------|-------|---------|
| `bsd/vm/vm_unix.c` | CS bypass for shared cache | Allow unsigned shared cache mapping |
| `bsd/vfs/hfs_catalog.c` (or `src/hfs-650.0.2/`) | bcopy fix | Fix __memmove_chk overflow on R/W remount |
| `osfmk/console/video_console.c` | Tamzen font include | `#include "tamzen_font.c"` instead of `iso_font.c` |
| `osfmk/console/tamzen_font.c` | Font data file | Tamzen 8x16 bitmap font |
| Various `PANTHERA:` traces | Debug flag gating | `PANTHERA_DBG()` macro behind `panthera_verbose` boot-arg |
| Boot-args in bootx64.c | Quiet boot | Removed `-v debug=0x144` |
| Kernel config | Build config | Any PANTHERA-specific config options |

Run `grep -r "PANTHERA\|panthera" src/xnu-10002.41.9/ --include="*.c" --include="*.h" -l` to find all modified files.

### Step 3: Port Patches to New Source

For each modified file:
1. Find the corresponding file in `src/xnu-12377.81.4/`
2. Check if the surrounding code changed (context diff)
3. Apply the same patch, adjusting line numbers if needed
4. Most patches are small (1-10 lines) and the surrounding code is unlikely to have changed

### Step 4: Stub New Dependencies

**Image4 headers (25 files):**
Create stub headers in a shim directory that provide type definitions but no real implementation. Image4 is used for code signing verification — Panthera doesn't need it.

```bash
mkdir -p build/shims/image4
# For each header in EXTERNAL_HEADERS/image4/:
# Create a stub that defines the types as empty structs and functions as no-ops
```

**Exclaves:**
The cleanest approach is to ensure `CONFIG_EXCLAVES` is 0 in the kernel config. If it's not a config option, add `#define CONFIG_EXCLAVES 0` to the build flags. This should `#ifdef` out all 49 exclave files.

**Corecrypto:**
Check if the new XNU has additional corecrypto dependencies beyond what Sonoma had. If so, add stubs to the existing corecrypto shim headers.

### Step 5: Update Build Script

Modify `build/build_xnu.sh` to point at `src/xnu-12377.81.4/` instead of `src/xnu-10002.41.9/`. Update:
- Source path
- Target triple (darwin23 → darwin25)
- Any new include paths for image4/exclave headers
- Any new compile flags needed

### Step 6: Build Kernel

```bash
bash build/build_xnu.sh
```

Expect errors. Fix them iteratively:
- Missing headers → add stubs
- New type definitions → add to shim headers
- New config options → set to disabled
- Changed function signatures → update callers

### Step 7: Rebuild Libc

Update `userland/libsystem/build/build_libsystem_c.sh` to compile from `src/Libc-1725.40.4/` instead of `src/Libc-1583.40.7/`. The shim headers will need updating for any new internal types. The same 440-file approach applies — expect some new files and some changed headers.

### Step 8: Rebuild libmalloc

Update `userland/libsystem/build/obj/libsystem_malloc/build.sh` to compile from `src/libmalloc-792.80.2/`. The shim header may need updates. libmalloc-792 is a significant jump from 474 — the allocator may have new features (zone types, hardened free lists, etc.) that need stubs.

### Step 9: Rebuild libdispatch, libpthread

Similar approach — update source paths, fix any new header dependencies with shims.

### Step 10: Rebuild dyld

The dyld source may have changed significantly (dyld-1122 → ~1340). However, Panthera uses a custom `panthera_dyld.cpp` that doesn't depend on Apple's dyld4 infrastructure. The main thing to check: did the Mach-O format change? Did `LC_DYLD_CHAINED_FIXUPS` encoding change? If so, update the parsing in `panthera_dyld.cpp`.

### Step 11: Rebuild Kexts

Rebuild all 18 kexts against the new kernel headers. The KPI may have new symbols or changed interfaces. Most kexts are stable across versions — IOPCIFamily, IOStorageFamily, IOATAFamily haven't changed fundamentally in years.

Check if Apple released newer versions of the kext source packages that match Tahoe.

### Step 12: Rebuild Shared Cache

After all dylibs are rebuilt, regenerate the shared cache:
```bash
bash tools/build_shared_cache.sh
```

### Step 13: Update Boot Chain

- Rebuild EFI bootloader (boot-args, kext manifest)
- Restage kernel to EFI staging
- Restage kexts to EFI staging
- Restage all dylibs + cache to root image

### Step 14: Boot Test

Full boot test in QEMU. Verify:
- Kernel boots (version string shows Darwin 25.x)
- All kexts load
- launchd starts, shell reaches prompt
- Shared cache maps correctly
- All existing functionality works (ls, cat, networking, getpwuid, etc.)

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| New kernel headers break kext build | High | Medium | Update kext shims, same approach as before |
| New libc internal types break shim headers | High | Medium | Update shim headers — proven approach |
| Exclaves code adds hard dependencies | Low | High | Disable via CONFIG_EXCLAVES=0 |
| Image4 framework requires real corecrypto | Medium | Medium | Stub headers, same as current approach |
| dyld format changes | Low | High | Check Mach-O changes, update parser |
| New syscall ABI breaks libsystem_kernel | Low | Low | The frozen binary still works; new syscalls are additive |

## When to Do This

**After Phase 4 (SSH) is working.** The upgrade doesn't add functionality that's blocked — it's about getting the most modern and final Intel kernel. SSH, remaining coreutils, and the current foundation work should complete first so there's a stable baseline to upgrade from.

## Fallback

Keep `src/xnu-10002.41.9/` intact. If the upgrade breaks something that can't be fixed quickly, revert to the Sonoma kernel and try again later. The upgrade is isolated to the `src/` directory and build scripts — no runtime state changes until the new kernel is staged.

## References

- Apple OSS Distributions (XNU): https://github.com/apple-oss-distributions/xnu
- Apple OSS Distributions (all): https://github.com/apple-oss-distributions
- Apple Open Source Releases: https://opensource.apple.com/releases/
- The Apple Wiki (kernel versions): https://theapplewiki.com/wiki/Kernel
- Exclaves architecture: https://eclecticlight.co/2024/08/20/sonomas-unfinished-business-exclaves-conclaves-and-the-kernel/
