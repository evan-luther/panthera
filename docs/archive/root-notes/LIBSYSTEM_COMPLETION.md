# Panthera — Complete Apple libSystem Stack: Agent Task

## Mission

Complete Panthera's libSystem stack using Apple's own open-source implementations.
Replace all stub/shim libraries with real Apple code. This is the foundation that
everything else (networking, SSH, user management, package management) depends on.

**Read CLAUDE.md and OVERNIGHT_TASK.md before starting.**

## Why This Task Exists

Panthera currently boots to an interactive shell, but the userland libraries are
held together with stubs:

- `libsystem_malloc.dylib`: 13KB bump allocator. Does not free memory. Any
  long-running process (daemon, shell session) will OOM.
- `libsystem_pthread.dylib`: 6.6KB single-threaded stubs. No real thread creation.
  Any program that creates threads will silently fail or crash.
- `libdispatch.dylib`: 9.5KB stub. GCD doesn't work.
- `libsystem_c.dylib`: 379 of ~595 Apple Libc source files compiled. The remaining
  ~216 files are papered over by `panthera_resolve_impl.c` (1164 lines of naive
  stubs). The stubs include a broken `__dtoa` (float-to-string), no-op regex, no-op
  glob, ASCII-only locale, and minimal stdio internals.

**Every higher-level component depends on these being real:**
- SSH daemons need malloc that frees and pthread that creates threads
- Networking tools need printf that formats correctly and DNS resolver functions
- User management needs getpwent/getgrent (from Libinfo, which needs real libc)
- The zsh crash is caused by these stubs returning garbage

All the Apple source code is already in `src/`. The task is compilation, not porting.

## Current State

### What's compiled and working
- XNU xnu-10002.41.9 boots with 15 kexts
- Custom dyld loads 10 images, resolves all symbols
- libsystem_kernel.dylib: syscall wrappers (working)
- libsystem_platform.dylib: atomics, setjmp (working)
- libsystem_c.dylib: 1270 exported symbols, 379 Apple .o files + 11 stub .o files
- Static mini_launchd + dash shell → `panthera#` prompt
- Root HFS+ image boots from QEMU IDE

### What's stubbed (to be replaced by this task)
- libsystem_malloc.dylib → replace with Apple libmalloc-474.0.13
- libsystem_pthread.dylib → replace with Apple libpthread-519
- libdispatch.dylib → replace with Apple libdispatch-1462.0.4
- ~216 uncompiled Apple Libc source files → compile them
- panthera_resolve_impl.c stubs → eliminate by compiling real Apple code

### Source available in-tree

| Component | Source Path | Files | Purpose |
|-----------|-----------|-------|---------|
| Apple Libc | src/Libc-1583.40.7/ | 595 .c | C library (379 already compiled) |
| Apple malloc | src/libmalloc-474.0.13/ | 226 files | Zone-based allocator |
| Apple pthread | src/libpthread-519/ | 115 files | POSIX threads via Mach |
| Apple libdispatch | src/libdispatch-1462.0.4/ | 129 files | Grand Central Dispatch |
| Apple libplatform | src/libplatform-306.0.1/ | 74 files | Platform primitives |
| Apple Libinfo | src/Libinfo-583.0.1/ | 150 files | getpwent, DNS, RPC |
| Apple launchd | src/launchd-842.92.1/ | 80 files | Init system |
| FreeBSD libc (ref) | src/freebsd-libc/ | ~400 files | Reference only, not primary |
| Compat shim | userland/libsystem/build/obj/freebsd_libc/fbsd_compat.h | 1 file | FreeBSD compat header |

## Build System

**Toolchain:**
```bash
CC="$(xcrun -sdk macosx -find clang)"
CXX="$(xcrun -sdk macosx -find clang++)"
SDKROOT="$(xcrun -sdk macosx -show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
```

**Common compile flags:**
```bash
CFLAGS=(
    -target x86_64-apple-darwin23.0
    -mmacosx-version-min=14.0
    -fPIC -O2 -g
    -D__DARWIN_UNIX03=1 -DPRIVATE=1 -D__APPLE__=1
    -Wno-error -Wno-unused-parameter -Wno-sign-conversion
    -Wno-shorten-64-to-32 -Wno-missing-field-initializers
    -Wno-implicit-function-declaration -Wno-format
    -Wno-deprecated-declarations -Wno-nullability-completeness
    -Wno-expansion-to-defined
)
```

**Key paths:**
```
Project root:     /Users/admin/panthera/
Build script:     userland/libsystem/build_libsystem.sh
Sysroot:          userland/libsystem/build/sysroot/
  Headers:        sysroot/usr/include/
  Libs:           sysroot/usr/lib/ and sysroot/usr/lib/system/
Object dir:       userland/libsystem/build/obj/
  libc objects:   build/obj/libsystem_c/  (379 .o files currently)
  stubs:          build/obj/panthera_*.o  (11 files, to be eliminated)
Kernel source:    src/xnu-10002.41.9/
Apple Libc:       src/Libc-1583.40.7/
```

**Link commands (current, for reference):**
```bash
# libsystem_c.dylib
xcrun ld -arch x86_64 -dylib -install_name /usr/lib/system/libsystem_c.dylib \
    -o sysroot/usr/lib/system/libsystem_c.dylib \
    -all_load all_libc.a \
    -not_for_dyld_shared_cache -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0

# libsystem_malloc.dylib
xcrun ld -arch x86_64 -dylib -install_name /usr/lib/system/libsystem_malloc.dylib \
    -o sysroot/usr/lib/system/libsystem_malloc.dylib \
    <objects> \
    -not_for_dyld_shared_cache -undefined dynamic_lookup \
    -platform_version macos 14.0.0 26.0.0

# libSystem.B.dylib (umbrella)
xcrun ld -arch x86_64 -dylib -install_name /usr/lib/libSystem.B.dylib \
    -o sysroot/usr/lib/libSystem.B.dylib \
    libSystem_init.o compiler_rt_stubs.o dyld_stub_binder.o \
    -reexport_library sysroot/usr/lib/system/libsystem_kernel.dylib \
    -reexport_library sysroot/usr/lib/system/libsystem_platform.dylib \
    -reexport_library sysroot/usr/lib/system/libsystem_malloc.dylib \
    -reexport_library sysroot/usr/lib/system/libsystem_c.dylib \
    -reexport_library sysroot/usr/lib/system/libsystem_pthread.dylib \
    -reexport_library sysroot/usr/lib/system/libdispatch.dylib \
    -not_for_dyld_shared_cache -platform_version macos 14.0.0 26.0.0
```

**Root image workflow:**
```bash
# Mount
raw_device="$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage \
  images/qemu/panthera-root.img | awk 'NR==1 {print $1}')"
sleep 1; diskutil mount "${raw_device}s1" 2>/dev/null; sleep 1

# Copy files
cp -f <library> /Volumes/PantheraRoot/usr/lib/system/

# Unmount
diskutil unmount /Volumes/PantheraRoot; hdiutil detach "$raw_device" -force
```

**Boot test:**
```bash
timeout 90 boot/qemu/run_phase2_qemu.sh --restage --no-reboot --ssh-port 0 \
    --root-disk images/qemu/panthera-root.img > /tmp/panthera_qemu_console.log 2>&1
```

## Phase 1: Complete Apple Libc (libsystem_c.dylib)

### 1A: Audit what's missing

379 of ~595 source files are compiled. Identify exactly which files failed and why.

1. List all `.c` files in `src/Libc-1583.40.7/` (excluding tests/)
2. Cross-reference against compiled `.o` files in `build/obj/libsystem_c/`
3. Categorize the ~216 missing files by subsystem and failure reason

**Known failure categories** (from OVERNIGHT_TASK.md):
- `__va_list` type: some files use `__va_list` which isn't defined. Fix:
  `typedef __builtin_va_list __va_list;` in a compat header
- `.symver` directives: GNU symbol versioning not supported by Apple ld. Fix:
  strip or `#define __asm__(".symver ...")` to nothing
- `dd_fd` vs `__dd_fd`: FreeBSD-derived files use `dd_fd` but Apple's `struct DIR`
  uses `__dd_fd`. Fix: `#define dd_fd __dd_fd` in compat header
- Missing internal headers: some files include private Apple headers not in sysroot.
  Fix: extract from SDK or create minimal stubs

### 1B: Fix and compile the remaining Apple Libc files

Work through each subsystem:

**stdio/FreeBSD/** (~140 files — this is the biggest gap):
Apple's Libc stdio is derived from FreeBSD. The source is at
`src/Libc-1583.40.7/stdio/FreeBSD/`. These files use Apple's `struct __sFILE`
layout (defined in `_stdio.h` in the sysroot). Key files:
- `vfprintf.c` — the real printf engine. Critical for everything.
- `vfscanf.c` — scanf engine
- `findfp.c` — FILE* pool management (`__sfp`, `__sfprelease`)
- `local.h` — internal stdio definitions
- `fopen.c`, `fclose.c`, `fflush.c`, `fread.c`, `fwrite.c`
- `printf-pos.c` — positional printf arguments
- All the wchar variants: `fgetwc.c`, `fputwc.c`, `vfwprintf.c`, etc.

**gdtoa/FreeBSD/** (14 files):
Float-to-string conversion. Source at `src/Libc-1583.40.7/gdtoa/FreeBSD/`.
- `gdtoa-dtoa.c`, `gdtoa-gdtoa.c`, `gdtoa-misc.c`, etc.
- Also `_hdtoa.c`, `_ldtoa.c` in `src/Libc-1583.40.7/gdtoa/`
- These replace the naive `__dtoa` stub in `panthera_resolve_impl.c`
- Needs `gdtoaimp.h`, `arith.h`, `gd_qnan.h` — all present in source

**locale/FreeBSD/** (locale files):
- `collate.c`, `setlocale.c`, `lconv.c`, locale ctype functions
- Also Apple-specific: `xlocale.c`, `xlocale_private.h`
- Needs `_LOCALE_T` struct definition — check Apple headers vs FreeBSD

**regex/** (POSIX regex):
- `src/Libc-1583.40.7/regex/FreeBSD/` — regcomp.c, regexec.c, etc.
- Also `src/Libc-1583.40.7/regex/TRE/` — Apple uses TRE regex engine
- Replaces no-op regex stubs

**gen/FreeBSD/** (general utilities):
- Many already compiled (379 objects include gen_FreeBSD_*.o)
- Check which gen files are missing: glob.c, fts.c, wordexp.c, etc.

**string/FreeBSD/** and **stdlib/FreeBSD/**:
- Most should already be compiled. Check for gaps.

**secure/** (stack protector, _chk functions):
- `__chk_fail.c`, `memcpy_chk.c`, `memmove_chk.c`, etc.
- These provide `_*_chk` fortified variants

**darwin/**, **libdarwin/**, **os/**, **sys/**:
- Apple-specific subsystems. Check which are missing.

### 1C: Eliminate panthera_resolve_impl.c stubs

As real Apple implementations are compiled, remove the corresponding stubs from
`panthera_resolve_impl.c`. Track progress — the goal is to reduce this file to
zero (or near-zero, keeping only things that genuinely need custom Panthera shims
like raw syscall helpers).

For each stub category in the file:
1. **Stack protector** → keep (or move to compiler_rt)
2. **Ctype locale** → replace with Apple locale/ sources
3. **gdtoa** → replace with Apple gdtoa/FreeBSD/ sources
4. **printf internals** → replace with Apple stdio/FreeBSD/vfprintf.c
5. **stdio internals** → replace with Apple stdio/FreeBSD/findfp.c etc.
6. **Locale/collation** → replace with Apple locale/ sources
7. **_simple functions** → compile from libplatform-306.0.1
8. **Mach functions** → compile from libsyscall or create proper wrappers
9. **os functions** → compile from libplatform or libdispatch
10. **TLV stubs** → proper TLV support from dyld/libpthread
11. **ASL/ACL** → stub is fine for now (no ASL daemon)
12. **regex** → replace with Apple regex/ sources
13. **posix_spawn** → compile from Apple Libc sys/ or gen/

### 1D: Also eliminate other panthera_*.o stubs

Review and replace where possible:
- `panthera_printf.o` → replaced by real vfprintf.c
- `panthera_stdio_init.o` → replaced by real findfp.c / stdio_init.c
- `panthera_missing.o` → audit what's in it, replace with real code
- `panthera_remaining.o` → audit and replace
- `panthera_resolve_wave2.o`, `wave3.o` → audit and replace
- `panthera_malloc_override.o` → eliminated when real malloc is built

Keep only:
- `panthera_resolve_aliases.o` (symbol aliases are still needed)
- `unix2003_aliases.o` and `symbol_aliases.o` (UNIX2003 conformance aliases)
- Any assembly stubs for symbol name remapping

### 1E: Rebuild and validate libsystem_c.dylib

1. Archive all new `.o` files into `all_libc.a`
2. Link with `-all_load` as before
3. Check: `nm -gU libsystem_c.dylib | wc -l` should be notably higher than 1270
4. Check: `nm -gu libsystem_c.dylib` (undefined symbols) — should be only
   symbols expected from other dylibs (malloc, pthread, kernel, platform)
5. Install to root image
6. Boot test: dash shell should still work

## Phase 2: Real Apple Malloc (libsystem_malloc.dylib)

### 2A: Understand Apple's malloc

Source: `src/libmalloc-474.0.13/src/`

Apple's malloc is a zone-based allocator:
- `malloc_common.c` — main entry points (malloc, free, realloc, calloc)
- `magazine_malloc.c` — scalable magazine allocator (the default zone)
- `magazine_tiny.c`, `magazine_small.c`, `magazine_medium.c` — size-class allocators
- `magazine_large.c` — large allocation handler
- `magazine_rack.c` — per-CPU magazine racks
- `nano_malloc.c` or `nanov2_malloc.c` — nano allocator for tiny objects
- `frozen_malloc.c` — read-only frozen zone
- `legacy_malloc.c` — compatibility
- `early_malloc.c` — pre-initialization allocator

Key dependencies:
- Mach VM: `mach_vm_allocate`, `mach_vm_deallocate`, `mach_vm_map` — provided by
  libsystem_kernel.dylib
- pthread: `pthread_mutex_lock/unlock` — needs at least stub pthread (current is fine)
- libplatform: `os_unfair_lock` — needs real or stub implementation
- DTrace: `DTRACE_PROBE` macros — can be stubbed to no-ops

### 2B: Build strategy

Apple's malloc is complex. Two approaches:

**Approach A: Full Apple malloc (recommended if it compiles)**
1. Compile all source files in `src/libmalloc-474.0.13/src/`
2. Provide required headers from the sysroot
3. Stub DTrace probes: `#define DTRACE_PROBE(...)` and similar
4. May need to stub magazine rack per-CPU features if they need kernel support
   that isn't available (check for `commpage` dependencies)
5. Link into libsystem_malloc.dylib

**Approach B: Simplified Apple malloc (fallback)**
If the full magazine allocator has too many dependencies:
1. Use `legacy_malloc.c` + `malloc_common.c` as the core
2. This gives a working zone-based allocator without the magazine optimization
3. Still much better than the bump allocator

**Approach C: dlmalloc bridge (last resort)**
If Apple's malloc proves intractable:
1. Use Doug Lea's dlmalloc (single .c file, public domain)
2. Wrap it to provide Apple's malloc API (malloc_zone_t, etc.)
3. This is a temporary bridge, not a permanent solution

### 2C: Required exports

The new libsystem_malloc.dylib must export at minimum:
```
_malloc, _free, _realloc, _calloc
_posix_memalign, _aligned_alloc, _valloc
_malloc_size, _malloc_good_size
_reallocf, _reallocarray$DARWIN_EXTSN
_malloc_zone_malloc, _malloc_zone_free, _malloc_zone_realloc, _malloc_zone_calloc
_malloc_default_zone, _malloc_create_zone
_malloc_zone_from_ptr
_dyld_stub_binder
```

### 2D: Build, link, install

1. Compile objects
2. Link: `xcrun ld -arch x86_64 -dylib -install_name /usr/lib/system/libsystem_malloc.dylib ...`
3. Verify exports: `nm -gU libsystem_malloc.dylib`
4. Install to root image
5. Boot test: run a program that allocates and frees memory repeatedly

## Phase 3: Real Apple pthread (libsystem_pthread.dylib)

### 3A: Understand Apple's pthread

Source: `src/libpthread-519/src/`

Apple's pthread uses Mach threads underneath:
- `pthread.c` — pthread_create, pthread_join, pthread_exit
- `pthread_mutex.c` — mutexes (using Mach semaphores or `__ulock_wait`)
- `pthread_rwlock.c` — read-write locks
- `pthread_cond.c` — condition variables
- `pthread_tsd.c` — thread-specific data (TSD/TLS)
- `pthread_cancelable.c` — cancellation points
- `pthread_atfork.c` — fork handlers
- `pthread_asm.s` — architecture-specific thread setup
- `qos.c` — quality of service

Key dependencies:
- Kernel: `bsdthread_create`, `bsdthread_terminate`, `__pthread_markcancel`,
  `thread_selfid` — these are XNU syscalls, should be available
- libplatform: `os_unfair_lock` primitives
- Mach: `thread_create`, `thread_resume` — available via libsystem_kernel

### 3B: Build strategy

**Approach A: Full Apple pthread**
1. Compile all source in `src/libpthread-519/src/`
2. Need the kernel support headers from XNU's `bsd/pthread/` and `osfmk/`
3. Assembly stubs for x86_64 thread entry (`pthread_asm.s`)
4. Check for `__ulock_wait`/`__ulock_wake` syscall support — XNU should have these

**Approach B: Progressive enhancement**
1. Start with the current single-threaded stubs
2. Add real mutex/rwlock/cond (these work even single-threaded)
3. Add real TSD (needed for errno, which currently uses a stub)
4. Add pthread_create last (needs full Mach thread support)
5. This lets you incrementally validate each piece

Approach B is safer — real threading can break things if the rest of the system
isn't ready. Real mutexes and TSD are immediately useful even without threads.

### 3C: Critical first target — real TSD (Thread-Specific Data)

TSD is what makes `errno` work correctly. The current stub uses a single global.
Real TSD needs:
- A per-thread data block
- `pthread_key_create`, `pthread_getspecific`, `pthread_setspecific`
- For the main thread (which is all we have initially), this is a static array

Get TSD working first, then mutexes, then the rest.

### 3D: Build, link, install

Same pattern as malloc. Export all standard pthread symbols.

## Phase 4: Real Apple libdispatch (libdispatch.dylib)

### 4A: Understand the dependency

Source: `src/libdispatch-1462.0.4/`

libdispatch (GCD) depends on:
- pthread (for thread pool)
- Mach ports (for event sources)
- `kevent`/`kqueue` (for I/O dispatch)

This is the most complex library in the stack. However, many Darwin programs can
function with a **minimal dispatch** that handles synchronous dispatch_sync and
basic queue creation.

### 4B: Build strategy

**Approach A: Stub-level dispatch (current — may be sufficient)**
If programs only need `dispatch_once`, `dispatch_sync`, and basic queue operations,
the current 9.5KB stub may be adequate for now. Test with real programs first.

**Approach B: Build real libdispatch**
libdispatch has a CMakeLists.txt and can be built with CMake:
```bash
cd src/libdispatch-1462.0.4
mkdir build && cd build
cmake .. -DCMAKE_C_COMPILER=$(xcrun -find clang) \
  -DCMAKE_SYSTEM_NAME=Darwin \
  -DCMAKE_OSX_ARCHITECTURES=x86_64
make
```
This requires real pthread and real Mach port support.

**Recommendation:** Defer full libdispatch until after malloc and pthread are done.
The current stub is likely sufficient for shell + coreutils. Full dispatch becomes
necessary for configd and launchd.

## Phase 5: Build Apple Libinfo (getpwent, DNS, networking)

### 5A: What Libinfo provides

Source: `src/Libinfo-583.0.1/`

Libinfo is Apple's information lookup library. It provides:
- `lookup.subproj/`: getpwent, getgrent, getpwnam, getpwuid, getgrouplist,
  getaddrinfo, getnameinfo, getservbyname, getnetbyname, gethostbyname
- `dns.subproj/`: DNS resolver (herror.c, res_comp.c, res_query.c)
- `gen.subproj/`: getifaddrs, network interface utilities
- `rpc.subproj/`: RPC/XDR (needed by some system tools)
- `membership.subproj/`: group membership queries

### 5B: The DirectoryService problem

Apple's Libinfo normally queries `opendirectoryd` (Open Directory) via Mach
IPC for user/group lookups. We don't have Open Directory.

However, Libinfo has **fallback paths**:
- `lookup.subproj/file_module.c` — reads flat `/etc/passwd` and `/etc/group` files
- `lookup.subproj/ds_module.c` — DirectoryService module (skip this)
- `lookup.subproj/mdns_module.c` — mDNS module (skip for now)

**Strategy:** Build Libinfo with only the file module enabled. This makes
getpwnam/getgrent read from `/etc/passwd` and `/etc/group` directly — exactly
what we want for a standalone Darwin system.

### 5C: Build Libinfo

1. Compile `lookup.subproj/` with `-DFILE_MODULE_ONLY` or equivalent ifdef
2. Compile `gen.subproj/` (getifaddrs, etc.)
3. Compile `dns.subproj/` (resolver)
4. Skip: ds_module.c, mdns_module.c, muser_module.c, cache_module.c
5. Link into a new `libsystem_info.dylib` or add to libsystem_c.dylib
6. Add re-export to libSystem.B.dylib

### 5D: Create passwd/group files

Install on root image:
```
/etc/passwd:
root:*:0:0:System Administrator:/var/root:/bin/sh
daemon:*:1:1:System Services:/var/root:/usr/bin/false
nobody:*:-2:-2:Unprivileged User:/var/empty:/usr/bin/false

/etc/group:
wheel:*:0:root
daemon:*:1:
staff:*:20:root
nobody:*:-2:

/etc/master.passwd:
root::0:0::0:0:System Administrator:/var/root:/bin/sh
daemon:*:1:1::0:0:System Services:/var/root:/usr/bin/false
nobody:*:-2:-2::0:0:Unprivileged User:/var/empty:/usr/bin/false
```

Note: root has empty password for initial bring-up. Set a real password later.

## Phase 6: Rebuild Everything and Validate

### 6A: Full rebuild sequence

```bash
# 1. Rebuild libsystem_c.dylib with all new Apple objects
# 2. Rebuild libsystem_malloc.dylib with real Apple malloc
# 3. Rebuild libsystem_pthread.dylib with real Apple pthread
# 4. Rebuild libdispatch.dylib (keep stub or upgrade)
# 5. Build libsystem_info.dylib from Libinfo
# 6. Rebuild libSystem.B.dylib umbrella (add any new re-exports)
# 7. Rebuild dyld if any dylib changes affect loading
# 8. Install ALL libraries to root image
# 9. Boot test
```

### 6B: Validation tests

After booting, test from the dash shell:

```sh
# Basic libc
echo "hello world"            # printf works
echo "3.14159" | cat          # stdio pipeline works

# Malloc
# (any program that runs for more than a few seconds tests this)

# File operations
ls /                          # readdir, stat
cat /etc/passwd               # fopen, fread, fclose
echo test > /tmp/testfile     # fopen write mode
cat /tmp/testfile             # verify write worked

# If zsh is available, try it
/bin/zsh                      # should no longer crash
```

### 6C: Test zsh specifically

The zsh crash was the original motivator. After completing Phases 1-5:
1. zsh is already on the root image at `/bin/zsh`
2. It's dynamically linked against libSystem + libiconv + libncurses
3. If libc is now complete, zsh should get past `_createparamtable`
4. Boot and try: does `zsh` reach a `%` prompt?

If zsh still crashes, the error will now be more diagnosable — it'll be a specific
missing function or wrong behavior, not "everything is stubbed."

## Important Patterns and Gotchas

### Apple Libc internal structure
Apple's Libc is organized as `subsystem/FreeBSD/file.c` — this means Apple took
FreeBSD's libc files and adapted them. The files in `stdio/FreeBSD/` ARE Apple's
stdio, not a separate FreeBSD port. They use Apple's `struct __sFILE` layout.
Compile them with Apple's headers from the sysroot.

### $UNIX2003 symbol suffixes
Many libc functions have both `_func` and `_func$UNIX2003` variants. The `$UNIX2003`
versions have POSIX.1-2001 conformant behavior. Create aliases in assembly:
```asm
.globl _func$UNIX2003
_func$UNIX2003:
    jmp _func
```
Existing alias files handle many of these. Add more as needed.

### Header search order
When compiling Apple Libc files:
1. First: the Libc source tree's own headers (`src/Libc-1583.40.7/include/`)
2. Second: the Panthera sysroot (`userland/libsystem/build/sysroot/usr/include/`)
3. Third: the macOS SDK (for system headers not yet in sysroot)
```bash
-I src/Libc-1583.40.7/include \
-I src/Libc-1583.40.7/stdio/FreeBSD \
-I userland/libsystem/build/sysroot/usr/include \
-isysroot $(xcrun -sdk macosx -show-sdk-path)
```

### Circular dependencies
libsystem_c depends on libsystem_malloc (for malloc) and libsystem_pthread
(for thread-local errno). These in turn depend on libsystem_c (for string
functions, printf). This is resolved by:
- Using `-undefined dynamic_lookup` during linking
- dyld resolves cross-library symbols at load time
- The umbrella libSystem.B.dylib re-exports all, making them a single flat namespace

### When a file won't compile
1. Read the error message. Most failures are missing typedefs or headers.
2. Check if the needed header exists in the macOS SDK — if so, copy to sysroot.
3. If it's a private Apple header, create a minimal version with just the needed
   definitions.
4. If a function references a framework we don't have (CoreFoundation, Security),
   stub that specific call, not the whole file.
5. **Do not skip files** — every skipped file is a stub that will bite later.

### Order of operations matters
Build in this exact order:
1. libsystem_kernel (already done — syscall wrappers)
2. libsystem_platform (already done — atomics, setjmp)
3. libsystem_malloc (Phase 2 — needs kernel for mmap)
4. libsystem_c (Phase 1 — needs malloc, kernel, platform)
5. libsystem_pthread (Phase 3 — needs c, kernel, platform)
6. libdispatch (Phase 4 — needs pthread, c, kernel)
7. Libinfo (Phase 5 — needs c, pthread)
8. libSystem.B.dylib umbrella (last — re-exports all)

Phases 1 and 2 can be done in either order since they link with
`-undefined dynamic_lookup`. But test in the order above.

## Success Criteria

**Phase 1 complete when:**
- All ~595 Apple Libc source files compile (or conscious decision to skip <10)
- `panthera_resolve_impl.c` is reduced to <100 lines (only genuine Panthera shims)
- `nm -gU libsystem_c.dylib | wc -l` shows 1500+ symbols
- printf("%.17g", 3.14159265358979323846) prints accurately (real gdtoa)

**Phase 2 complete when:**
- libsystem_malloc.dylib uses Apple's allocator, not the bump allocator
- A program can malloc/free in a loop without growing memory

**Phase 3 complete when:**
- libsystem_pthread.dylib provides real TSD (errno works correctly)
- Mutex operations work (even if single-threaded)

**Phase 5 complete when:**
- getpwnam("root") returns the root user entry from /etc/passwd
- getaddrinfo("localhost", ...) resolves

**Overall complete when:**
- zsh boots to interactive prompt without crashing
- System runs for 10+ minutes without OOM or crash
- Foundation is solid enough to build networking tools, SSH, and coreutils on top

## What Comes After This

Once libSystem is complete with real Apple implementations, the next tasks are:
1. Port coreutils (from FreeBSD `bin/` and `usr.bin/` — these are standalone
   binaries, not library code, so FreeBSD is appropriate here)
2. Networking userland (ifconfig, route, ping — port from FreeBSD)
3. SSH (dropbear, already in src/)
4. Real launchd activation (already compiled)
5. User management (login, getty — port from FreeBSD)
6. Package management (pkgsrc bootstrap)

But none of those can succeed until this foundation is solid. Do this first.

---

## Current Status (2026-03-29)

### Phase 1: Apple Libc — IN PROGRESS (90% complete)

**Compilation: 562 of 595 Apple Libc source files compiled (94%)**

The remaining 200+ uncompiled files from the original 379-object build have been
compiled using a comprehensive compat header (`panthera_libc_compat.h`) and
updated build script (`build_remaining_libc.sh`). Key fixes applied:

- Added `-D__LIBC__=1` (Apple's own master define for building libc)
- Added `__MAC_OS_X_VERSION_MIN_REQUIRED=140000` to `Availability.h`
- Removed `-I EXTERNAL_HEADERS` (XNU's kernel `stdatomic.h` was shadowing clang's)
- Removed `-I exclave` (exclave's stripped `syslimits.h` was hiding `COLL_WEIGHTS_MAX`)
- Created compat header with: `__va_list` typedef, `dd_fd`→`__dd_fd` DIR struct mapping,
  `FLOCKFILE`/`FUNLOCKFILE` no-ops, `_write`→`write` FreeBSD function remapping,
  `OSAtomic*` stubs, `M_32_SWAP`/`M_16_SWAP` macros, `MAX`/`MIN`, and more

**17 files consciously skipped:**
- 11 `libdarwin/*.c` — need `bootstrap_priv.h`, `xpc/private.h`, `apfs/apfs_fsctl.h`
  (deep Apple framework dependencies; these provide convenience functions like
  `os_crash_fmt`, not core libc)
- 2 `os/assumes.c`, `os/debug_private.c` — need full private `os_log` infrastructure
  (`os_log_pack_t`, `OS_LOG_F_SEND`); shim `os/assumes.h` provides working macros
- 3 `sys/msgctl.c`, `sys/semctl.c`, `sys/shmctl.c` — legacy SysV IPC ABI compat shims
  (need both old and new struct definitions simultaneously; we target UNIX03 only)
- 1 `gen/FreeBSD/timezone.c` — `timezone` symbol conflicts with POSIX `timezone` variable

**Subsystems now compiled with real Apple code:**

| Subsystem | Files | Key Functions |
|-----------|-------|---------------|
| stdio/FreeBSD | vfprintf, vfscanf, findfp, makebuf, snprintf, sprintf, vfwprintf, all v*printf/scanf variants | Real printf engine, FILE* management |
| gdtoa/FreeBSD | gdtoa-dtoa, gdtoa-gdtoa, _hdtoa, _ldtoa, glue, etc. | Accurate float-to-string (replaces naive __dtoa stub) |
| locale/FreeBSD | collate, setlocale, runetype, tolower, toupper | Real locale support, collation |
| regex/TRE | regcomp, regexec, tre-compile, tre-parse, tre-match-*, tre-mem, tre-ast, tre-stack | Real POSIX regex |
| gen/FreeBSD | opendir, readdir, closedir, seekdir, telldir, popen, ttyname, rewinddir | Real directory operations |
| db/ | btree (13 files), hash (7 files), recno (8 files), mpool, db | Berkeley DB |
| net/FreeBSD | inet_addr, inet_ntop, inet_pton, inet_ntoa, linkaddr, recv, send, etc. | Networking address functions |
| stdlib/FreeBSD | abort, atexit, exit, getenv, psort | Process lifecycle |
| stdtime/FreeBSD | localtime | Real timezone/localtime |
| gen/ | clock_gettime, backtrace, thread_stack_pcs, sync_volume_np, utmpx | System utilities |
| sys/ | posix_spawn, fork, sigaction, crt_externs, _libc_init, gettimeofday, etc. | System call wrappers |
| uuid/ | gen_uuid, parse, unparse, clear, compare, copy, pack, unpack | UUID operations |
| util/ | pty, login_tty, opendev, fparseln, mkpath_np, login, logout, logwtmp | Terminal/utility |
| posix1e/ | acl, acl_entry, acl_file, acl_flag, acl_perm | ACL support |
| compat-43/ | creat, killpg, setpgrp, getwd, etc. | BSD compat |
| emulated/ | brk, bsd_signal, statvfs, tcgetsid, etc. | Emulated syscalls |
| string/ | stpcpy, stpncpy, strcat, strncat | String operations |
| nls/ | msgcat | Message catalogs |
| secure/ | chk_fail | Stack protection |
| gmon/ | gmon | Profiling |
| collections/ | collections_map, collections_set | Apple collections |
| stdio/ | xprintf, xprintf_comp, xprintf_domain, xprintf_exec, printf-pos | Extended printf |

### Linking: libsystem_c.dylib — 806KB, 1529 exported symbols

**Link components:**
- `all_libc.a` — 562 compiled Apple Libc objects (real implementations)
- `panthera_symbol_bridge.o` — 43 symbols: `_strlen`→`__platform_strlen` etc.,
  `$NOCANCEL$UNIX2003` aliases, syscall `_getpid`→`___getpid` mappings
- `panthera_unix2003_bridge.o` — 55 symbols: plain name→`$UNIX2003` aliases
  (`_fputs`→`_fputs$UNIX2003`, `_fopen`→`_fopen$UNIX2003`, etc.)
- `panthera_runtime_bridge.o` — 52 symbols: `__error` (errno), `__fpclassifyd`,
  blocks runtime (`_Block_copy` etc.), `os_alloc_once`, `OSAtomic*`, `vm_allocate`,
  `__maskrune`, `__tolower`, `__toupper`, `mach_task_self_`
- `panthera_boot_bridge.o` — 42 symbols: `strtol` family, `kill`, `sleep`, `open`,
  `fcntl`, `tcgetattr`/`tcsetattr`, `strerror`, `pow`, `fmod`, `getpwent` stubs,
  `iconv` stubs, `cerror`/`cerror_nocancel`, `_setjmp`/`_longjmp`, `environ`
- `panthera_flockfile.o` — 3 symbols: `FLOCKFILE`, `FUNLOCKFILE`, `__isthreaded`

### Boot Testing: FUNCTIONAL — dynamic zsh reaches interactive prompt

The system boots through: kernel → HFS mount → load_init_program → `/sbin/launchd`
(static mini_launchd) → exec `/bin/zsh` (dynamic) → dyld loads all 10 images →
interactive `zsh` prompt as PID 1.

**dyld successfully resolves:** all `__platform_str*`/`__platform_mem*` functions,
`malloc`/`free`/`calloc`/`realloc`, most pthread functions, most syscall wrappers,
`stat$INODE64`, `fstat$INODE64`, etc.

**Current bring-up workaround:** `mini_launchd` exports
`PANTHERA_SKIP_ZSH_INIT_SCRIPTS=1`, and zsh uses that to skip
`run_init_scripts()` during early bring-up. This bypasses shell startup/profile
loading only; it does not replace the remaining Phase 2 goal of running real
Apple `launchd` as PID 1.

**Meaning of the current prompt:** this is not yet macOS-style boot. The current
path is kernel → static `mini_launchd` → `execve("/bin/zsh")`, so `zsh` replaces
PID 1. The next milestone remains real `launchd` bring-up once Phase 1 cleanup
is stable.

### Remaining Phase 1 Work

The original PID 1 crash is resolved enough to reach a dynamic shell prompt.
Phase 1 is now about replacing bring-up shortcuts with cleaner behavior:
1. Re-enable zsh init scripts selectively to identify the specific startup path
   that still hangs under Panthera.
2. Continue removing temporary libc/dyld stub dependencies where real Apple code
   now exists.
3. Keep boot output quiet and deterministic while preserving enough diagnostics for
   future symbol-resolution failures.

### Phases 2-6: NOT STARTED

- **Phase 2 (malloc):** Current bump allocator still in use. Apple libmalloc-474.0.13
  source available at `src/libmalloc-474.0.13/`
- **Phase 3 (pthread):** Current single-threaded stubs. Apple libpthread-519 source
  available at `src/libpthread-519/`
- **Phase 4 (libdispatch):** Current 9.5KB stub. Defer until pthread is real.
- **Phase 5 (Libinfo):** Not started. Apple Libinfo-583.0.1 at `src/Libinfo-583.0.1/`
- **Phase 6 (rebuild + validate):** Blocked on Phase 1 boot success

## Status Update (2026-03-29, late)

### Prompt Bring-Up Cleanup: COMPLETE

The dynamic zsh prompt path was cleaned up after the original bring-up:

- Removed temporary dyld progress logging and zsh breadcrumb prints
- Kept the working zsh startup workaround, but changed it from a hardcoded
  `run_init_scripts()` return into an explicit environment-gated bring-up path
- `mini_launchd` now exports `PANTHERA_SKIP_ZSH_INIT_SCRIPTS=1`, so the shell
  startup bypass is visible and reversible instead of being buried in zsh logic
- Boot output is now reduced to kernel/mini_launchd output plus the shell prompt

### Phase 2 Handoff: IN PROGRESS

Work has started on the next milestone: replacing `zsh`-as-PID-1 with a dynamic
`launchd.real` handoff while keeping the current bring-up path recoverable.

Current in-tree work for that handoff:

- `mini_launchd` has been patched locally to try `/sbin/launchd.real` before
  falling back to `/bin/zsh`
- A minimal `libbsm.0.dylib` shim has been added locally for the audit entry
  points used by launchd bring-up (`getaudit_addr`, `setaudit_addr`, etc.)
- The existing partial `launchd_real` build was inspected and confirmed to rely
  on a local stubbed runtime rather than full upstream launchd internals
- Stub-runtime work is in progress to turn that dynamic `launchd_real` into a
  minimal PID 1 supervisor that forks `zsh` as a child process

### Important Constraint

This handoff is not yet boot-validated. The last fully verified boot remains:

`kernel -> static mini_launchd -> dynamic zsh prompt`

The next validation target is:

`kernel -> static mini_launchd -> exec launchd.real -> launchd_real supervises zsh child`

### File Inventory

```
Build artifacts:
  userland/libsystem/build/build_remaining_libc.sh    — Phase 1B build script
  userland/libsystem/build/obj/libsystem_c/            — 562 compiled .o files
  userland/libsystem/build/obj/stubs_v2/               — 13 deduped old stub .o files
  userland/libsystem/build/obj/panthera_symbol_bridge.s — string/syscall alias asm
  userland/libsystem/build/obj/panthera_unix2003_bridge.s — $UNIX2003 alias asm
  userland/libsystem/build/obj/panthera_runtime_bridge.c — compiler-rt/blocks/mach
  userland/libsystem/build/obj/panthera_boot_bridge.c   — strtol/tc*/kill/pow/etc.
  userland/libsystem/build/obj/panthera_flockfile.c     — FLOCKFILE/FUNLOCKFILE
  userland/libsystem/build/obj/libsystem_c/shims/       — compat headers
    panthera_libc_compat.h  — master compat header (-include'd for all builds)
    config.h                — TRE regex config
    os/assumes.h            — os_crash/os_assert stubs + os_redirect_t
    os/log.h, os/log_private.h, os/reason_private.h     — OS log stubs
    os/feature_private.h, os/alloc_once_private.h       — OS stubs
    os/transaction_private.h, os/variant_private.h      — OS stubs
    mach-o/dyld_priv.h     — dyld version macros
    spawn_private.h, sys/spawn_internal.h               — spawn stubs
    bootstrap_priv.h, membershipPriv.h                  — framework stubs
    struct.h, bsm/libbsm.h, crt_externs.h              — misc stubs
    uuid/uuid_types.h, _simple.h                        — type stubs

Sysroot headers modified:
  sysroot/usr/include/Availability.h    — added __MAC_OS_X_VERSION_MIN_REQUIRED
  sysroot/usr/include/mach/mach.h       — added mach_init.h + vm_map.h includes
  sysroot/usr/include/mach/mach_init.h  — fixed mach_task_is_self macro conflict
  sysroot/usr/include/os/api.h          — copied from Libc source
  sysroot/usr/include/os/debug_private.h — copied from Libc source
  sysroot/usr/include/os/collections_*.h — copied from Libc collections/
```

## Status Update (2026-03-29, pthread foundation)

### Direction Correction

After the dynamic `zsh` prompt milestone, the next critical path was re-evaluated.
`launchd.real` bring-up is no longer treated as the immediate blocker because it
would have to sit on top of:

- the current 13 KB bump allocator in `libsystem_malloc.dylib`
- the previous 6.6 KB single-threaded `libsystem_pthread.dylib` stub

That means the correct order is now:

`real pthread -> real malloc -> revalidate shell/userland -> return to launchd.real`

### Phase 3 (pthread): MAJOR PROGRESS

Panthera now has a buildable Apple-sourced `libsystem_pthread.dylib` from
`src/libpthread-519/` instead of only the old simple stub object.

Current verified state:

- All 13 selected pthread source/asm units now compile successfully
- The resulting dylib links successfully at:
  `userland/libsystem/build/sysroot/usr/lib/system/libsystem_pthread.dylib`
- Output size is now approximately 83 KB instead of the previous stub-scale
  export surface
- The new dylib exports core bring-up symbols including:
  `___pthread_init`, `___pthread_late_init`, `_pthread_self`,
  `__pthread_atfork_*`, and QoS / override entry points

Key build fixes added for this milestone:

- Introduced tracked pthread-only build shims under:
  `userland/libsystem/build/shims/libsystem_pthread/`
- Added shims for:
  `CrashReporterClient.h`, `os/thread_self_restrict.h`,
  and a pthread-specific `os/atomic_private.h`
- Aligned the pthread build flags with upstream libpthread defaults, including:
  `__DARWIN_NON_CANCELABLE=1`, `__PTHREAD_BUILDING_PTHREAD__=1`,
  `__PTHREAD_EXPOSE_INTERNALS__=1`, and related Darwin train macros
- Corrected the cancelable-variant link issue by matching upstream
  `__DARWIN_NON_CANCELABLE` behavior

### Sysroot Header Alignment Started

Panthera's sysroot still contained stub private pthread headers that would have
forced later libraries to compile against the old Panthera assumptions even after
the new pthread dylib linked.

To prevent that mismatch, the following headers were updated toward Apple's
direct-TSD model:

- `userland/libsystem/build/sysroot/usr/include/pthread/tsd_private.h`
- `userland/libsystem/build/sysroot/usr/include/pthread/private.h`

These now expose inline direct-TSD helpers instead of only stub extern
declarations, which is required for the upcoming libmalloc bring-up.

### Important Constraint

This pthread milestone is currently a build-system / ABI-foundation milestone,
not a runtime validation milestone.

Not yet done:

- `libsystem_pthread.dylib` has not yet been staged into the Panthera root image
- `libSystem.B.dylib` has not yet been rebuilt/restaged against this new pthread
- No QEMU boot has yet been performed with the new pthread in place
- `libsystem_malloc.dylib` is still the old bump allocator

### Next Objective

Proceed directly to Phase 2 foundation work with the new pthread baseline:

1. Rebuild `libsystem_malloc.dylib` against the updated pthread-private headers
   and new pthread dylib
2. Eliminate Panthera's remaining malloc bump-allocator behavior
3. Rebuild/restage `libSystem.B.dylib`
4. Re-validate dynamic `zsh`
5. Only then resume `launchd.real` bring-up

## Status Update (2026-03-29, malloc build baseline)

### Phase 2 (malloc): FIRST REAL BUILD

Panthera now has a linkable Apple-sourced `libsystem_malloc.dylib` built from
`src/libmalloc-474.0.13/`.

Current verified state:

- 23 selected libmalloc source files now compile successfully
- The resulting dylib links successfully at:
  `userland/libsystem/build/sysroot/usr/lib/system/libsystem_malloc.dylib`
- Output size is approximately 250 KB
- The linked dylib exports core allocator entry points including:
  `___malloc_init`, `___malloc_late_init`, `_malloc`, `_free`, `_calloc`,
  `_aligned_alloc`, and malloc fork hooks

### What Unblocked This

The current malloc bring-up was blocked mostly by build-environment mismatches
rather than allocator code defects.

The low-level blockers cleared in this pass were:

- CrashReporter / crashlog declarations (`gCRAnnotations`)
- missing `resolver/` include path for `nanov2_malloc.c`
- historical one-argument `mach_task_is_self()` call sites in libmalloc source
- link search path visibility for the newly built `libsystem_pthread.dylib`
- duplicate symbol overlap between `malloc.c` and `malloc_type_stubs.c`

### Important Constraint

This is still a build milestone, not a boot-validation milestone.

Not yet done:

- `libsystem_malloc.dylib` has not yet been staged into the Panthera root image
- `libSystem.B.dylib` has not yet been rebuilt/restaged against the new
  pthread + malloc pair
- No QEMU boot has yet been performed with this allocator in place
- The current malloc build recipe is still provisional bring-up infrastructure
  rather than a cleaned final tracked pipeline

### Updated Critical Path

Panthera now has both major foundation dylibs built from Apple source:

- `libsystem_pthread.dylib` — buildable and linked
- `libsystem_malloc.dylib` — buildable and linked

The next objective is to convert that into a runtime-validated foundation:

1. restage pthread + malloc into the root image
2. rebuild/restage `libSystem.B.dylib`
3. boot QEMU and revalidate dynamic `zsh`
4. only after that, resume `launchd.real`

## Status Update (2026-03-29, boot crash diagnosis)

### Boot Attempts: TWO CRASHES DIAGNOSED

The new Apple malloc + pthread dylibs were staged to the root image and booted.
Both attempts kernel-panicked with `initproc failed to start -- exit reason
namespace 2 subcode 0xb` (SIGSEGV in PID 1).

#### Crash #1: `0xdeaddeaddeaddead` sentinel

```
RAX: 0xdeaddeaddeaddead
```

**Root cause:** `libSystem_init.c` never called `__malloc_init()`. Apple's
`malloc.c:50` initializes `malloc_zones` to `0xdeaddeaddeaddead` as a sentinel.
`__malloc_init()` → `_malloc_initialize()` replaces it with real zone pointers.
Without that call, the first `malloc()` dereferences `0xdeaddeaddeaddead`.

**Fix applied:** Added `extern void __malloc_init(const char *apple[])
__attribute__((weak));` and `if (__malloc_init) __malloc_init(apple);` to
`libSystem_init.c`, before `__libc_init`. The rebuilt `libSystem_init.o` and
`libSystem.B.dylib` are in the sysroot. The weak linkage means the bump
allocator (which has no `__malloc_init`) still works — the call is skipped.

**File changed:** `userland/libsystem/build/obj/libSystem_init.c` — committed
in working tree but not yet in a git commit.

#### Crash #2: NULL pointer dereference in malloc zone init

```
RAX: 0x0000000000000000
```

After adding the `__malloc_init` call, the sentinel was cleared (no more
`0xdeaddeaddeaddead`), but malloc zone creation itself crashed with NULL.

**Root cause:** `mach_vm_map` in `libpanthera_extra.dylib` is a **broken stub**.
It lives in `userland/libsystem/build/obj/panthera_resolve_wave2.c:71-78` and
translates `mach_vm_map` into a simplified `mmap` call via `_bsd_sc(197, ...)`.
This stub:
- Ignores the `mask` parameter (alignment requirements)
- Ignores `VM_FLAGS_OVERWRITE`
- Doesn't properly translate `VM_FLAGS_ANYWHERE` to mmap flags
- Ignores `object`, `offset`, `copy`, `cur_protection`, `max_protection`,
  `inheritance` parameters

Apple's magazine allocator calls `mach_vm_map` from `mvm_allocate_pages()` in
`vm.c` with specific alignment masks, `VM_FLAGS_ANYWHERE`, guard page flags,
and VM tags. The stub returns garbage or failure, zone allocation fails,
`malloc()` crashes on the resulting NULL zone pointer.

**Fix required:** Replace the mmap-based stubs with Apple's real Mach VM
wrappers from `src/xnu-10002.41.9/libsyscall/mach/mach_vm.c`. This file
uses proper Mach traps:
- `_kernelrpc_mach_vm_map_trap()` — fast trap for common case
- `_kernelrpc_mach_vm_map()` — MIG fallback
- `_kernelrpc_mach_vm_allocate_trap()` — fast alloc trap
- `_kernelrpc_mach_vm_deallocate_trap()` — fast dealloc trap
- `_kernelrpc_mach_vm_protect_trap()` — fast protect trap

### Exact Fix Plan

**Step 1: Generate Mach trap assembly stubs**

The `_kernelrpc_mach_vm_*_trap` functions are Mach traps, not BSD syscalls.
XNU's `libsyscall/mach/` has `.defs` files that generate them. The trap numbers
are defined in `osfmk/mach/mach_traps.h`. On x86_64, a Mach trap is invoked
via `syscall` with negative trap number in `%eax`:

```asm
// Example: _kernelrpc_mach_vm_allocate_trap is trap -10
.globl __kernelrpc_mach_vm_allocate_trap
__kernelrpc_mach_vm_allocate_trap:
    movl    $(-10), %eax    // Mach trap number
    movq    %rcx, %r10      // 4th arg: x86_64 syscall ABI
    syscall
    ret
```

Trap numbers from `osfmk/mach/mach_traps.h`:
- `_kernelrpc_mach_vm_allocate_trap`: -10
- `_kernelrpc_mach_vm_deallocate_trap`: -12
- `_kernelrpc_mach_vm_protect_trap`: -14
- `_kernelrpc_mach_vm_map_trap`: -15
- `_kernelrpc_mach_vm_purgable_control_trap`: -16

Verify these numbers against `src/xnu-10002.41.9/osfmk/mach/mach_traps.h`.

**Step 2: Generate MIG fallback stubs**

The `_kernelrpc_mach_vm_map` (without `_trap`) is a MIG-generated RPC. These
are generated from `libsyscall/mach/mach_vm.defs` by the `mig` tool. For
initial bring-up, only the fast traps are needed — the MIG fallbacks can
return `MACH_SEND_INVALID_DEST` to signal "not available", and the trap path
handles the common cases.

Minimal MIG stubs:
```c
kern_return_t _kernelrpc_mach_vm_allocate(mach_port_name_t t,
    mach_vm_address_t *a, mach_vm_size_t s, int f) {
    return MACH_SEND_INVALID_DEST;  // force trap path
}
// ... same pattern for map, deallocate, protect
```

**Step 3: Compile Apple's `mach_vm.c`**

Source: `src/xnu-10002.41.9/libsyscall/mach/mach_vm.c`

This provides the real `mach_vm_allocate`, `mach_vm_deallocate`, `mach_vm_map`,
`mach_vm_protect`, `vm_allocate`, `vm_deallocate`, `vm_map`, etc.

Compile against the Panthera sysroot. Requires:
- `mach/mach.h`, `mach/mach_traps.h` — in sysroot
- `mach/vm_map_internal.h`, `mach/mach_vm_internal.h` — may need to extract
  from XNU or create minimal versions
- `stack_logging_internal.h` — stub with `malloc_logger_t *__syscall_logger = NULL;`

**Step 4: Link into libsystem_kernel.dylib or libpanthera_extra.dylib**

Replace the broken `mach_vm_*` stubs in `panthera_resolve_wave2.c` with the
real implementations. Either:
- Add to `libsystem_kernel.dylib` (where they belong in Apple's layout)
- Add to `libpanthera_extra.dylib` (simpler, fewer rebuild steps)

**Step 5: Rebuild malloc dylib**

The malloc dylib from the agent's build (`libsystem_malloc/build.sh`) had
duplicate symbol issues. Fix: exclude `malloc_type_stubs.o` (duplicates symbols
from `malloc.o`). Also add `-random_uuid` to the link command so the umbrella
linker doesn't reject it.

Corrected link (already validated):
```bash
xcrun ld -arch x86_64 -dylib \
  -install_name /usr/lib/system/libsystem_malloc.dylib \
  -undefined dynamic_lookup -not_for_dyld_shared_cache \
  -random_uuid -platform_version macos 14.0.0 26.0.0 \
  dyld_stub_binder.o \
  bitarray.o early_malloc.o frozen_malloc.o has_section.o \
  legacy_malloc.o magazine_large.o magazine_malloc.o \
  magazine_medium.o magazine_rack.o magazine_small.o \
  magazine_tiny.o malloc_common.o malloc_printf.o \
  malloc_type.o malloc.o msl_lite_support.o \
  nano_malloc_common.o nanov2_malloc.o pgm_malloc.o \
  purgeable_malloc.o sanitizer_malloc.o stack_trace.o vm.o \
  -o libsystem_malloc.dylib
```
Note: `malloc_type_stubs.o` is EXCLUDED (duplicates with `malloc.o`).

**Step 6: Rebuild libSystem.B.dylib**

Already done — the current sysroot `libSystem.B.dylib` has the `__malloc_init`
call in `__libSystem_init`. Just needs restaging after malloc/mach_vm are fixed.

**Step 7: Stage and boot test**

1. Copy new `libsystem_kernel.dylib` (or `libpanthera_extra.dylib`) with real
   Mach VM traps to root image
2. Copy new `libsystem_malloc.dylib` (250KB Apple magazine allocator) to root image
3. Copy new `libsystem_pthread.dylib` (83KB Apple pthread) to root image
4. Copy fixed `libSystem.B.dylib` to root image
5. Boot QEMU, expect zsh prompt with real malloc/free

### Current Root Image State

The root image has been **reverted to the working bump allocator** and simple
pthread stubs. Boot is stable at `kernel -> mini_launchd -> zsh prompt`.

The sysroot has the fixed `libSystem.B.dylib` (with `__malloc_init` call) and
the relinked `libsystem_malloc.dylib` (without `malloc_type_stubs.o` duplicates,
with `-random_uuid`). These are ready to be re-staged once the Mach VM trap
issue is resolved.

### Dependencies Already Satisfied

The dependency audit confirmed all malloc runtime deps are exported by existing
dylibs:
- `os_unfair_lock_*` → `libpanthera_extra.dylib`
- `_os_alloc_once`, `_os_once` → `libpanthera_extra.dylib`
- `mach_task_self` → `libpanthera_extra.dylib`
- `_pthread_getspecific_direct` → `libpanthera_extra.dylib`
- `getentropy` → `libsystem_c.dylib`
- `sysctlbyname` → `libsystem_c.dylib`
- Commpage `_COMM_PAGE_PHYSICAL_CPUS/LOGICAL_CPUS/CPU_CLUSTERS` → populated by XNU

The ONLY missing piece is real `mach_vm_map`/`mach_vm_allocate`/etc. traps.

## Status Update (2026-03-30, malloc boot debugging)

### Steps 1-6: COMPLETE

All seven steps from the Exact Fix Plan have been implemented:

1. **Mach trap assembly stubs** (`mach_vm_traps.s`) — x86_64 uses class-based
   encoding `RAX = 0x01000000 | trap_number`, NOT negative numbers. This was
   a critical discovery: the i386 negative-number convention does not apply to
   x86_64.
2. **MIG fallback stubs** (`mach_vm_mig_stubs.c`) — return `MACH_SEND_INVALID_DEST`
   to force the fast trap path. Enhanced `_kernelrpc_mach_vm_map` to use
   allocate_trap + protect_trap as fallback for guard pages.
3. **Apple's `mach_vm.c` compiled** — 17 exported symbols including `mach_vm_allocate`,
   `mach_vm_deallocate`, `mach_vm_map`, `vm_allocate`, etc.
4. **Broken stubs replaced** in `panthera_resolve_wave2.c` — removed all mmap-based
   mach_vm stubs, fixed `mach_thread_self` to use correct trap encoding.
5. **libsystem_malloc.dylib rebuilt** — 245KB Apple magazine allocator, compiled with
   `-fno-stack-protector` to avoid `__stack_chk_guard` dependency, nano allocator
   disabled via `nano_stubs.o`.
6. **libSystem.B.dylib rebuilt** — includes `__malloc_init` weak call.

### Step 7 (boot test): IN PROGRESS — multiple crashes diagnosed and fixed

Boot reaches `__malloc_init` but crashes inside it. Progress through multiple
crash/fix cycles:

#### Fixed issues

1. **x86_64 Mach trap encoding** — `movl $0x0100000a, %eax` not `movl $(-10), %eax`.
   XNU x86_64 `mach_call_munger64` dispatches on `(RAX >> 24) & 0xFF` for class,
   `RAX & 0xFFFF` for trap number.

2. **`mach_task_self_` initialization** — dyld now calls task_self_trap
   (`0x0100001c`) and writes the port to the `mach_task_self_` global before
   `__malloc_init`.

3. **`__malloc_init` called from dyld** — called directly (not via weak GOT) to
   avoid fragile bind-time resolution.

4. **`__stack_chk_guard` eliminated** — all 23 malloc objects recompiled with
   `-fno-stack-protector`. The dyld bind parser has issues with flat-namespace
   symbol resolution that left `__stack_chk_guard` GOT entries NULL.

5. **Dyld bind parser `case 0x40` (SET_TYPE_IMM) bug** — was incorrectly consuming
   subsequent bytes as a symbol name when the next byte happened to be `_` (0x5F).
   This corrupted the opcode stream and caused total bind failure for dylibs with
   certain bind data layouts. Fixed to simply store the type value and nothing else.

6. **Missing `vm_page_size` globals** — `vm_page_size`, `vm_page_mask`,
   `vm_page_shift`, `vm_kernel_page_size`, `vm_kernel_page_mask`,
   `vm_kernel_page_shift` were not exported by any Panthera dylib. Added to
   `panthera_resolve_wave2.c` in libpanthera_extra. Also added `mach_task_is_self`
   and `gCRAnnotations` stub.

7. **`initializeProgramVars` ordering** — dyld was calling `__malloc_init` BEFORE
   setting up `environ`. Apple's `__malloc_init` calls `_NSGetEnviron()` and
   `getenv()` to read `MallocGuardEdges`, `MallocScribble`, etc. Fixed by moving
   `initializeProgramVars()` before `__malloc_init()`.

#### Current blocker: lazy bind stubs jump to uninitialized `__dyld_private`

The crash is now a SIGSEGV where RIP lands in the **DATA segment** of
`libsystem_malloc.dylib` at the exact address of the `__dyld_private` symbol
(offset 0x2d470). This happens because:

1. A function called from `__malloc_init` triggers a **lazy bind stub** (PLT entry)
2. The lazy stub does `jmp *__la_symbol_ptr` → the initial value points to the
   stub helper, which pushes an index and jumps to `dyld_stub_binder`
3. `dyld_stub_binder` is resolved via `__dyld_private`, which our dyld **never
   initialized**
4. `__dyld_private` contains zero/garbage, so the jump lands in the DATA segment
   at address `&__dyld_private` itself

**Evidence:**
- Crash RIP consistently matches `malloc_base + 0x2d470` = offset of `__dyld_private`
- RAX=0xb in every crash (the lazy bind index being processed)
- Caller address is in heap memory (zone structure with function pointer table)
- `nano_common_init` diagnostic (added to nano_stubs.c) never prints, confirming
  the crash is before zone init completes

**Root cause:** The dyld's `applyBinds` function processes both `bind` and
`lazy_bind` sections. However, lazy bind entries in the `-undefined dynamic_lookup`
flat-namespace format may not be getting fully resolved. When an unresolved lazy
stub is called at runtime, it falls through to `dyld_stub_binder` which doesn't
exist.

**Fix options (in order of preference):**

1. **Ensure all lazy binds are eagerly resolved** — verify the dyld's lazy bind
   processing (pass 1) correctly resolves all entries and writes the resolved
   addresses to the `__la_symbol_ptr` section, bypassing the stub helper entirely.

2. **Provide a real `dyld_stub_binder`** — initialize `__dyld_private` to point
   to a stub binder function that can resolve lazy symbols on demand. This is
   complex because the stub binder needs full dyld context.

3. **Link malloc without lazy binds** — add `-bind_at_load` to the malloc link
   command so all binds are non-lazy. This avoids the issue entirely for malloc.

### Files changed since last status

```
Modified:
  userland/dyld/panthera_dyld.cpp
    - Fixed case 0x40 bind parser bug
    - Added initializeProgramVars before __malloc_init
    - Added image address diagnostic prints
    - Added __malloc_init pre/post diagnostic prints

  userland/libsystem/build/obj/panthera_resolve_wave2.c
    - Added vm_page_size/vm_page_mask/vm_page_shift globals
    - Added vm_kernel_page_size/vm_kernel_page_mask/vm_kernel_page_shift
    - Added mach_task_is_self (uses inline trap, no extern dependency)
    - Added gCRAnnotations stub

  userland/libsystem/build/obj/libsystem_malloc/nano_stubs.c
    - Added raw-syscall diagnostic write in nano_common_init

  userland/libsystem/build/obj/libsystem_malloc/build.sh
    - Added -fno-stack-protector to CFLAGS

Rebuilt:
  libpanthera_extra.dylib — 71KB (new symbols)
  libsystem_malloc.dylib — 245KB (no stack protector, nano stubs)
  dyld — 26KB (bind fix, ordering fix, diagnostics)

Root image staged with all above at /Volumes/PantheraRoot/
```

### Verified working

- Mach VM traps: `mach_vm_allocate_trap` returns KERN_SUCCESS, valid address
- Commpage: version=14, phys_cpus=1, logical_cpus=1
- `vm_page_size` globals resolve correctly (no more NULL deref at 0x191ab)
- `environ`/`_NSGetEnviron` set up before malloc reads env vars
- Bind parser correctly handles SET_TYPE_IMM without corrupting opcode stream
- All 10 images load successfully with valid headers
