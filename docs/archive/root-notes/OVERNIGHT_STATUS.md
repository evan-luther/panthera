# Overnight Task Status — 2026-03-29

## Summary
**Panthera Darwin now boots to an interactive shell prompt.** This is the first time
a userspace process has run on our custom XNU build.

## Phase 1: Fix libsystem_c → get shell running
### Status: COMPLETE (with caveats)

#### What was done:
1. **Resolved all 288 unresolved lazy-bind symbols in libsystem_c**
   - Created `panthera_resolve_impl.c` (1164 lines): gdtoa, printf internals,
     stdio internals, locale/collation stubs, mach functions, os_unfair_lock,
     TLV stubs, ASL/ACL/corecrypto stubs, posix_spawn, regex, sprintf_l variants
   - Created `panthera_resolve_aliases.s`: assembly trampolines for 50+ symbol
     name mismatches between platform/kernel/pthread dylibs
   - Created `panthera_resolve_wave2.c/asm`: second-wave symbols needed by
     malloc/pthread/dispatch

2. **Replaced Apple libsystem_malloc** with simple mmap bump allocator
   - Apple's malloc needs complex zone initialization (sentinel 0xdeaddeaddeaddead)
   - Our `panthera_malloc_override.c` uses mmap + arena allocator
   - 13K dylib vs 244K Apple original

3. **Replaced Apple libsystem_pthread** with single-threaded implementation
   - TSD (Thread-Specific Data), mutex, once, rwlock, cond vars
   - Sufficient for single-threaded shell operation

4. **Created proper stdio initialization** (`panthera_stdio_init.c`)
   - Proper FILE structures for stdin/stdout/stderr with read/write callbacks

5. **Key dyld improvements:**
   - Added `__mod_init_func` and `__init_offsets` initializer support
   - Now processes both legacy binds AND chained fixups

6. **Other fixes:**
   - `__thread` errno → static errno (TLV not supported)
   - `ttyname` override (Apple version crashes on empty /dev/)
   - `___chkstk_darwin` stack probe implementation
   - Fixed `-force_load` for wave2 objects (linker was dead-stripping them)

#### Boot result:
- XNU boots, mounts HFS+ root filesystem
- dyld loads 10 images with **zero unresolved symbols**
- Custom init opens /dev/console, prints banner
- Execs `/bin/sh` (custom mini shell) → **interactive shell prompt!**
- System stays up indefinitely with no panics

#### Known issues:
- **zsh crashes** in `_createparamtable` → `_getintvalue` with invalid pointer
  (RDI=0x10). Under investigation — likely struct layout mismatch or
  missing libc function returning wrong value.
- `/bin/sh` is our custom mini shell (echo, cat, ls, uname built-ins only)
- Malloc is a simple bump allocator (doesn't free small allocations)
- Pthread is single-threaded only

## Phase 2: Real launchd
### Status: NOT STARTED
- The custom init works well enough for now
- Real launchd needs libbsm and proper libc support

## Phase 3: Networking
### Status: NOT STARTED
- IONetworkingFamily headers are present but kext not compiled
- Need network driver (e1000 or virtio-net) for QEMU

## Phase 4: SSH
### Status: NOT STARTED
- Depends on networking
- Plan: dropbear (lighter than OpenSSH)

## Files Modified/Created

### New source files:
- `userland/libsystem/build/obj/panthera_resolve_impl.c`
- `userland/libsystem/build/obj/panthera_resolve_aliases.s`
- `userland/libsystem/build/obj/panthera_resolve_wave2.c`
- `userland/libsystem/build/obj/panthera_resolve_wave2_asm.s`
- `userland/libsystem/build/obj/panthera_stdio_init.c`
- `userland/libsystem/build/obj/panthera_malloc_override.c`
- `userland/libsystem/build/obj/panthera_pthread_simple.c`

### Modified files:
- `userland/libsystem/build/obj/panthera_missing.c` (fixed __thread errno)
- `userland/libsystem/build/obj/panthera_printf.c` (moved stdio pointers)
- `userland/libsystem/build/obj/libSystem_init.c` (simplified init)
- `userland/dyld/panthera_dyld.cpp` (added initializer support)

### Rebuilt binaries on root image:
- `/usr/lib/system/libsystem_c.dylib` (500K, 1300+ exported symbols)
- `/usr/lib/system/libsystem_malloc.dylib` (13K, simple allocator)
- `/usr/lib/system/libsystem_pthread.dylib` (13K, single-threaded)
- `/usr/lib/libSystem.B.dylib` (umbrella)
- `/usr/lib/dyld` (with initializer support)
- `/sbin/launchd` (custom init that execs /bin/sh)
