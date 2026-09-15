# Panthera — Build the Real Darwin Foundation

## Read First
Read `CLAUDE.md` and `OVERNIGHT_TASK.md` for full project context.

## Mission

Build Panthera's userland foundation properly — the way Apple designed it — so we can stop patching stubs and start building the actual OS. The end goal is **real Apple launchd as PID 1**, managing services via fork/exec, with Mach IPC and kqueue event loops. Everything else (networking, SSH, package management) depends on this being right.

## Current State

Panthera boots XNU in QEMU, mounts HFS+ root, and reaches a `panthera#` shell prompt via static binaries. The dynamic library stack exists but has gaps:

### Bring-Up Update (2026-03-30)

- PID 1 no longer panics the system when a child shell crashes. `mini_launchd` now supervises a child process and respawns it.
- The dynamic shell path now reaches `kernel -> /sbin/launchd -> /bin/zsh +m -> #` over `/dev/console`.
- `/bin/sh` remains the stable fallback shell and recovery path if zsh exits.
- `dyld` and `libsystem_malloc` have been pushed well past the original zsh crash boundary: early malloc, libSystem init, libc init, libdispatch init, and constructor processing now complete during bring-up.
- Layer 2's zsh bring-up blocker turned out to be runtime environment and terminal setup rather than constructor failure: `ttyname(0)` needed a Panthera-safe path during init, and exporting `TERM` without a staged terminfo database caused zsh to hang while importing environment parameters.
- Layer 3 is now complete enough to advance: `libsystem_kernel.dylib` exports the Mach trap and MIG client surface needed for launchd-adjacent bring-up, including `mach_msg`, `mach_port_*`, `mach_vm_*`, semaphore wrappers, clock RPCs, and the core `task` / `thread` / `host` paths.
- `dyld` now seeds `libsystem_kernel`'s Mach globals before malloc/libSystem bring-up and resolves imports with ordinal-aware, reexport-aware binding so the main executable no longer binds `_malloc` to weak `libsystem_kernel` stubs.
- The live `libpanthera_extra.dylib` no longer exports the promoted Mach symbol set, so Mach IPC now lives in `libsystem_kernel` instead of Panthera fallback stubs.
- Latest clean-stack verification: a 35-second QEMU boot reaches `zsh: setupvals done`, `zsh: init_misc done`, then the `#` prompt without a `mini_launchd` respawn.
- Layer 4 is now complete enough to advance: Panthera now builds a real `/usr/lib/system/libdispatch.dylib` from `src/libdispatch-1462.0.4/`, stages it onto the root image, and still reaches the zsh `#` prompt in QEMU.
- The current Layer 4 build keeps the foundation scope tight by compiling out newer non-essential branches for now: kevent workqueue/workloop support, Mach vouchers, memory-pressure integration, and the newer workgroup path remain deferred while the core queue/source/semaphore/event-loop surface is brought up from Apple source.
- Layers 5-7 are now complete enough to move forward as well:
  - Panthera now builds and stages a launchd-focused `libxpc.dylib` stub library and reexports it from `libSystem.B.dylib`.
  - Panthera now builds `libsystem_info.dylib` from Apple `Libinfo` sources, stages the required passwd/group database files into the root image, and exports the `si_search_module_set_flags` / passwd / group / `getifaddrs` surface launchd expects.
  - The remaining launchd symbols now live in a dedicated `libpanthera_launchd.dylib`, covering `proc_*`, `__udivti3`, `voucher_mach_msg_set`, `OSAtomicAdd32`, `audit_token_to_au32`, the needed `posix_spawnattr_*_np` functions, and the remaining syscall wrappers.
  - A direct `nm -gu userland/launchd/launchd` check against the full Panthera sysroot now reports `MISSING 0`.
  - After staging the rebuilt umbrella and new Layer 5-7 dylibs onto `images/qemu/panthera-root.img`, the guest still reaches the zsh `#` prompt in QEMU.
- To avoid regressing early shell startup, `libsystem_info.dylib` is currently reexported after `libsystem_c.dylib` in `libSystem.B.dylib`. That keeps zsh on the already-stable libc passwd/group path during bring-up while the real libinfo surface is present for Layer 8 launchd work.
- Layer 8 is now in progress: the guest boots the real Apple `launchd` binary as PID 1, enters a Panthera runtime path inside launchd, and supervises a `/bin/sh` console shell that exits cleanly and respawns.

**What works:**
- XNU kernel with 21 boot kexts (PCI, ATA, HFS, networking family, etc.)
- Custom dyld loads 10 dylib images, resolves symbols
- libsystem_kernel.dylib: **527 BSD syscall wrappers** (open, read, write, socket, kevent, kqueue, fork, wait4, etc.)
- libsystem_kernel.dylib: **real Mach IPC surface now live** (`mach_msg`, `mach_port_*`, `mach_vm_*`, semaphores, clocks, MIG runtime/client support)
- libsystem_platform.dylib: atomics, setjmp
- libsystem_malloc.dylib: **Real Apple magazine allocator** (240KB, 130 symbols) — malloc/free work
- libsystem_c.dylib: **1,529 symbols** from 561 compiled Apple Libc objects — stdio, string, stdlib, locale, regex, gen all partially working
- libsystem_pthread.dylib: **Real Apple pthread** (83KB, 204 symbols) — TSD, mutexes, condvars
- libdispatch.dylib: **Real Apple build now live** — `dispatch_async_f`, `dispatch_sync_f`, `dispatch_once_f`, `dispatch_queue_create`, `dispatch_source_create`, `dispatch_source_get_handle`, `dispatch_source_set_event_handler`, `dispatch_resume`, and `DISPATCH_SOURCE_TYPE_PROC` export correctly
- libbsm.0.dylib: audit syscall wrappers (exists, 9 symbols)
- Static mini_launchd supervision + dynamic `/bin/zsh` prompt on `/dev/console`
- `/bin/sh` fallback → `panthera#` recovery prompt
- dyld scratch-stack handoff works during early process startup
- libSystem constructor emulation in dyld reaches the end of libc and libdispatch bring-up
- `dyld` reaches zsh `main()`, zsh completes early parameter setup, and an interactive prompt appears

**What's broken:**
- external command exec from zsh is still unstable; builtins work, but `/bin/sh -c '...'` currently kills zsh and triggers PID 1 respawn
- `setlocale()` is no longer the first zsh bring-up blocker; the current remaining shell problem is the external exec path, not locale initialization
- `libsystem_malloc` still carries bring-up-only patches around env parsing, zone registration, and one-shot init
- 4,194 lines of stub code across 12 `panthera_*.c` files in `libpanthera_extra.dylib`
- real `launchd` is now booted as PID 1, but Panthera is still using a minimal in-launchd runtime path instead of a full plist/Mach service bootstrap
- external command exec from zsh is still unstable and separate from the launchd link-surface work

## The Real Launchd — What It Needs

A compiled `launchd` binary (282KB) exists at `userland/launchd/launchd`. It links against only `libSystem.B.dylib` + `libbsm.0.dylib`. Its 235 undefined symbols fall into these categories:

### Already satisfied (by existing dylibs):
- **Basic libc** (stdio, string, stdlib, memory): `fprintf`, `fopen`, `snprintf`, `strlen`, `strcmp`, `memcpy`, `malloc`, `free`, etc. → libsystem_c + libsystem_malloc
- **BSD syscalls**: `open`, `close`, `read`, `write`, `fork`, `kevent`, `kqueue`, `socket`, `wait4`, etc. → libsystem_kernel (527 symbols)
- **pthread**: `pthread_create`, `pthread_mutex_*`, `pthread_once` → libsystem_pthread
- **Audit**: `getaudit_addr`, `setaudit_addr`, `audit_session_*` → libbsm.0.dylib

### NOT yet satisfied — must be built:

**Mach IPC (35 symbols) — CRITICAL, launchd's core communication mechanism:**
```
mach_msg                        — send/receive Mach messages
mach_msg_destroy                — destroy message contents
mach_msg_server_once            — receive and dispatch one message
mach_port_allocate              — create a new port
mach_port_deallocate            — release a port reference
mach_port_insert_right          — insert send right into task
mach_port_extract_right         — extract right from task
mach_port_mod_refs              — modify port reference counts
mach_port_move_member           — add/remove port from port set
mach_port_request_notification  — request port death notification
mach_port_get_attributes        — query port attributes
mach_port_set_attributes        — set port attributes
mach_port_get_context           — get port context
mach_port_set_context           — set port context
mach_port_set_mscount           — set make-send count
mach_port_get_set_status        — list members of port set
mach_task_self_                 — current task port
task_self_trap                  — Mach trap for task self
task_get_special_port           — get bootstrap port
task_set_special_port           — set bootstrap port
task_set_exception_ports        — register exception handler
task_name_for_pid               — get task port for PID
task_policy_set                 — set task scheduling policy
host_reboot                     — reboot system
host_set_special_port           — set host special port
host_set_exception_ports        — set host exception ports
host_set_UNDServer              — set user notification daemon port
host_statistics                 — get host stats (memory, CPU)
mach_host_self                  — host port
mach_absolute_time              — high-res timestamp
mach_timebase_info              — timestamp conversion info
mach_error_string               — error code to string
NDR_record                      — Network Data Representation record
pid_for_task                    — PID from task port
```

**MIG runtime (7 symbols) — Mach Interface Generator support:**
```
mig_allocate                    — allocate MIG buffer
mig_deallocate                  — deallocate MIG buffer
mig_get_reply_port              — get per-thread reply port
mig_put_reply_port              — return reply port
mig_dealloc_reply_port          — destroy reply port
mig_strncpy                     — copy string in MIG message
mig_strncpy_zerofill            — copy + zero-fill in MIG message
```

**libdispatch (12 symbols) — launchd's event loop:**
```
dispatch_async_f                — async execution on queue
dispatch_sync_f                 — sync execution on queue
dispatch_once_f                 — one-time initialization
dispatch_get_global_queue       — get global concurrent queue
dispatch_queue_create           — create serial queue
dispatch_source_create          — create event source (process, signal, etc.)
dispatch_source_get_handle      — get source's monitored handle
dispatch_source_set_event_handler — set handler for events
dispatch_resume                 — resume a suspended source
dispatch_retain/release         — reference counting
_dispatch_source_type_proc      — process event source type constant
```

**XPC (30 symbols) — modern service management interface:**
```
xpc_dictionary_create/get_*/set_*  — dictionary operations
xpc_array_create/set_*/append_*    — array operations
xpc_copy_description               — debug description
xpc_copy_entitlements_for_pid      — entitlement checking
xpc_get_type/bool_get_value        — type inspection
xpc_retain/release                 — reference counting
xpc_strerror                       — error strings
xpc_dictionary_*_mach_send/recv    — Mach port passing over XPC
```

**Libinfo (5 symbols) — user/group database:**
```
getpwnam, getpwuid               — password database lookup
getgrnam                         — group database lookup
initgroups                       — initialize group list
si_search_module_set_flags       — search module configuration
```

**Process inspection (8 symbols):**
```
proc_pidinfo, proc_listallpids, proc_listchildpids, proc_listpgrppids
proc_get_dirty, proc_set_dirty, proc_track_dirty, proc_terminate
proc_setpcontrol
```

**Misc (scattered across libc/libSystem):**
```
bootstrap_port                  — global variable: inherited bootstrap Mach port
environ                         — global variable: environment pointer
fileport_makefd/makeport        — file descriptor ↔ Mach port conversion
login_tty                       — set up controlling terminal
voucher_mach_msg_set            — voucher for Mach messages
_NSConcreteStackBlock            — Blocks runtime
__udivti3                       — compiler-rt 128-bit division
confstr                         — get system configuration string
glob$INODE64                    — pathname pattern matching
issetugid                       — check if process is tainted
```

## Build Plan — Proper Foundation in Dependency Order

### Layer 1: Fix PID 1 Architecture (immediate)

**Goal:** Stop the crash-panic loop. Make mini_launchd a proper PID 1 that fork/execs children.

Right now mini_launchd calls `execve("/bin/zsh")` — zsh replaces it as PID 1. When zsh crashes, the kernel panics. This is not how Apple designed it.

Fix `userland/launchd/mini_launchd.c`:
1. After opening /dev/console, call `fork()`
2. **Child:** `execve` the shell (zsh, then fallback to dash/sh)
3. **Parent (PID 1):** `wait4()` in a loop. If child dies, respawn it.
4. Never exit — PID 1 must stay alive.
5. Add `SYS_fork` (2) and `SYS_wait4` (7) to the syscall wrappers.

This is a 20-line change. It immediately makes crashes diagnosable instead of causing kernel panics, and it matches Apple's architecture (PID 1 supervises children).

**Status now:** complete enough for bring-up. PID 1 stays alive, supervises children, and the guest now reliably reaches `/bin/sh` as a recovery shell even though zsh still crashes later in dynamic startup.

### Layer 2: Fix libsystem_c Stub Conflicts (gets zsh running)

**Goal:** zsh boots to an interactive prompt.

The zsh SIGSEGV is caused by symbol conflicts between `libpanthera_extra.dylib` (stubs) and `libsystem_c.dylib` (real Apple code). Both export `__sfp`, `__dtoa`, collation functions, etc.

1. Find all duplicate symbols:
```bash
comm -12 \
  <(nm -gU sysroot/usr/lib/system/libsystem_c.dylib | awk '{print $3}' | sort) \
  <(nm -gU sysroot/usr/lib/system/libpanthera_extra.dylib | awk '{print $3}' | sort)
```

2. Remove every duplicate from the `panthera_*.c` source files. The real Apple code in libsystem_c is correct.

3. Rebuild `libpanthera_extra.dylib` without the duplicates.

4. Install to root image and boot test. zsh should now use the real `__sfp` from findfp.c instead of the broken mini-pool stub.

**Status now:** complete enough to advance. Duplicate-export cleanup and runtime bring-up fixes moved zsh past the old early-libc crash path. zsh now reaches a live interactive prompt on `/dev/console`; the remaining external-exec instability is a follow-on shell/runtime bug, not the Layer 2 foundation gate.

### Layer 3: Mach Trap Wrappers in libsystem_kernel (enables Mach IPC)

**Goal:** `mach_msg`, `mach_port_*`, `task_*`, `host_*` work.

**Status now:** complete enough to advance. Panthera's `libsystem_kernel.dylib` now exports the Mach trap/wrapper surface that Layer 3 needed, and the guest still reaches a dynamic zsh prompt on the Mach-enabled stack. The remaining launchd blockers are now higher-layer userland pieces, especially real `libdispatch`, XPC, and libinfo.

This was the single biggest blocker for real launchd. The baseline `libsystem_kernel` started with 527 BSD syscall wrappers but no usable Mach trap surface; the work below is what moved Panthera past that boundary.

On XNU, Mach traps use a different syscall class (class 1, ORed with `0x1000000` instead of BSD's `0x2000000`). Apple generates these from `osfmk/mach/mach_traps.h`.

**Source:** XNU's `libsyscall/` already has the machinery:
- `libsyscall/mach/` contains Mach trap wrappers
- `libsyscall/mach/mach_msg.c` — the critical `mach_msg()` implementation
- `libsyscall/mach/mach_init.c` — `mach_task_self_`, `mach_host_self_`
- Mach trap stubs are generated from `osfmk/mach/mach_traps.h` entries

**What to build:**
1. Generate or write x86_64 Mach trap assembly stubs for each needed trap:
   ```asm
   .globl _mach_msg_trap
   _mach_msg_trap:
       movl $-31, %eax        ; Mach trap number (negative)
       movq %rcx, %r10        ; arg4 in r10 (syscall ABI)
       syscall
       ret
   ```
2. Compile `mach_msg.c` (wraps `mach_msg_trap` with retry logic)
3. Compile MIG runtime (`mig_allocate`, `mig_deallocate`, `mig_get_reply_port`, etc.)
4. Compile `mach_init.c` for `mach_task_self_` and `mach_host_self_` globals
5. Compile `mach_port.c` — MIG-generated stubs that call `mach_msg` to talk to the kernel's port management subsystem
6. Add `NDR_record` (static data structure for MIG encoding)
7. Add to libsystem_kernel.dylib and rebuild

**Key files in XNU source:**
- `src/xnu-10002.41.9/libsyscall/mach/` — Mach wrappers
- `src/xnu-10002.41.9/osfmk/mach/mach_traps.h` — trap number definitions
- `src/xnu-10002.41.9/libsyscall/mach/mach_msg.c` — mach_msg implementation
- `src/xnu-10002.41.9/libsyscall/mach/mach_init.c` — global port variables
- `src/xnu-10002.41.9/libsyscall/mach/headers/mach/` — MIG-generated headers

**Trap number reference (x86_64):**
Mach traps are negative numbers. On x86_64, the convention is `syscall` with `(0x1000000 | (-trap_number))` in RAX, but some use the direct negative encoding. Check `osfmk/mach/syscall_sw.h` for the exact ABI.

### Layer 4: Real libdispatch (enables launchd's event loop)

**Goal:** `dispatch_source_create`, `dispatch_async_f`, `dispatch_queue_create` work.

**Status now:** complete enough to advance. Panthera now builds and boots with a real `libdispatch.dylib` from Apple source, and the guest still reaches a live zsh prompt after the rebuilt dylib is staged onto the root image. The remaining launchd blockers are now higher-level userland pieces: XPC, libinfo, and the still-separate external-exec bug.

launchd uses libdispatch for its main event loop — `dispatch_source_create` with `DISPATCH_SOURCE_TYPE_PROC` to monitor child processes, plus dispatch queues for async work.

The old 9.5KB stub only had dispatch_once/sync/async. The Layer 4 bring-up replaced it with a real build that exports the launchd-facing queue/source/process-monitor surface while keeping newer non-foundation branches compiled out for now.

Real libdispatch needed:
- Real pthread (have it)
- Real malloc (have it)
- Mach ports (Layer 3)
- kqueue (already in libsystem_kernel)

**Source:** `src/libdispatch-1462.0.4/`

libdispatch has its own build system. Two options:

**Option A: CMake build (preferred)**
```bash
cd src/libdispatch-1462.0.4
mkdir build && cd build
cmake .. \
    -DCMAKE_C_COMPILER=$(xcrun -find clang) \
    -DCMAKE_SYSTEM_NAME=Darwin \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_C_FLAGS="-target x86_64-apple-darwin23.0"
make
```

**Option B: Manual compilation**
Compile the core files individually:
- `src/queue.c` — queue management
- `src/source.c` — event sources (process, signal, timer, etc.)
- `src/semaphore.c` — dispatch semaphores
- `src/once.c` — dispatch_once
- `src/apply.c` — dispatch_apply
- `src/init.c` — initialization
- `src/shims/` — platform shim layer

Link into `libdispatch.dylib` replacing the current stub.

### Layer 5: XPC Library (enables launchd's modern interface)

**Goal:** launchd can use XPC for service management.

launchd-842.92.1 uses 30 XPC functions. XPC is normally part of `libxpc.dylib` (closed-source on macOS). Options:

**Option A: Stub XPC (recommended for bring-up)**
Create a `libxpc.dylib` with minimal stubs that satisfy launchd's link requirements. Most XPC in launchd-842 is for the "new-style" plist interface — the core job management still works over MIG/Mach IPC. If we stub XPC to return errors/no-ops, launchd should still manage basic jobs.

```c
// Stub xpc_dictionary_create returns NULL — forces launchd to use legacy path
xpc_object_t xpc_dictionary_create(const char *const *keys,
                                    const xpc_object_t *values, size_t count) {
    return NULL;
}
```

**Option B: Build Apple's libxpc**
Apple's XPC source is partially open in `libdispatch` (the transport layer) and `launchd` (the server side). The client library is closed-source. Building a real one is a significant effort — defer to later.

### Layer 6: Libinfo (user/group database)

**Goal:** `getpwnam("root")` returns the root user entry.

**Source:** `src/Libinfo-583.0.1/`

Build with file-module-only (reads flat `/etc/passwd` and `/etc/group`):
1. Compile `lookup.subproj/file_module.c` + supporting lookup files
2. Compile `gen.subproj/` (getifaddrs, if_nametoindex)
3. Skip: ds_module.c (Open Directory), mdns_module.c (mDNS), cache_module.c
4. Link into `libsystem_info.dylib`
5. Re-export from libSystem.B.dylib

Create `/etc/passwd` and `/etc/group` on root image:
```
root:*:0:0:System Administrator:/var/root:/bin/zsh
daemon:*:1:1:System Services:/var/root:/usr/bin/false
nobody:*:-2:-2:Unprivileged User:/var/empty:/usr/bin/false
```

### Layer 7: proc_* Functions and Remaining Symbols

**Goal:** Satisfy launchd's remaining 20-odd symbols.

- `proc_*` functions: These are `libproc` wrappers around the `proc_info` syscall (SYS_proc_info = 336). Compile from `src/Libc-1583.40.7/gen/` or write wrappers.
- `bootstrap_port`: Global `mach_port_t` variable — initialized by dyld or libSystem init. Set it from the kernel-provided bootstrap port via `task_get_special_port`.
- `environ`: Global `char **` — set during process init from the stack.
- `login_tty`: Small function from `src/Libc-1583.40.7/util/` — setsid + set controlling terminal.
- `_NSConcreteStackBlock`: Blocks runtime — compile from `src/libclosure-90/`.
- `__udivti3`: Compiler-rt 128-bit integer division — compile from compiler-rt or extract from Xcode.
- `fileport_makefd/makeport`: Mach trap wrappers — add to Layer 3.
- `voucher_mach_msg_set`: Mach voucher support — can stub initially.

### Layer 8: Switch to Real Launchd

**Goal:** Real Apple launchd runs as PID 1, forks children, manages services.

**Status now:** in progress and over the first real boundary. The guest now boots the real Apple `launchd` binary as PID 1, launchd enters a Panthera-specific minimal runtime path, forks `/bin/sh` on `/dev/console`, and reaps + respawns that shell when it exits. The verified serial sequence is: `panthera#` -> `exit` -> `launchd: shell exited, restarting` -> new `panthera#` prompt.

Completed bring-up steps:
1. Replace `/sbin/launchd` on the root image with the real launchd binary and keep its link surface clean against the staged Panthera sysroot.
2. Skip launchd's early update thread in PID 1 for now; that path still panics in `bsdthread_create` during bootstrap on Panthera.
3. Route launchd through a Panthera minimal runtime loop that opens `/dev/console`, raw-forks a shell child, waits for exit, and restarts it.
4. Fix `mini_sh` so `exit` calls `sys_exit(0)` instead of halting the machine, allowing launchd to reap and respawn it.

Still to do for full Layer 8 completion:
1. Replace the Panthera minimal runtime path with launchd's real plist/job bootstrap path.
2. Move the supervised child back to a dynamic shell or service path once exec/runtime stability is good enough.
3. Prove stable long-running supervision under real launchd, not just the single-shell recovery loop.

Original target plist shape for the full handoff:
1. Create minimal `/System/Library/LaunchDaemons/` plist for a login shell:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.panthera.console</string>
    <key>ProgramArguments</key>
    <array>
        <string>/bin/zsh</string>
        <string>-l</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardInPath</key>
    <string>/dev/console</string>
    <key>StandardOutPath</key>
    <string>/dev/console</string>
    <key>StandardErrorPath</key>
    <string>/dev/console</string>
</dict>
</plist>
```
2. Boot test — launchd should initialize, read the plist, fork/exec zsh, and supervise it.

## Build Reference

**Toolchain:**
```bash
CC="$(xcrun -sdk macosx -find clang)"
SDKROOT="$(xcrun -sdk macosx -show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
```

**Current library locations:**
```
userland/libsystem/build/sysroot/usr/lib/system/
    libsystem_kernel.dylib      527 symbols (BSD syscalls only — NO Mach traps)
    libsystem_platform.dylib    atomics, setjmp
    libsystem_malloc.dylib      130 symbols (real Apple magazine allocator)
    libsystem_c.dylib           1,529 symbols (561 Apple .o files)
    libsystem_pthread.dylib     204 symbols (real Apple pthread)
    libdispatch.dylib           24 symbols (stub — needs replacement)
    libpanthera_extra.dylib     stubs (source of symbol conflicts)
userland/libsystem/build/sysroot/usr/lib/
    libSystem.B.dylib           umbrella re-exporting all 7 above
    libbsm.0.dylib              9 audit syscall wrappers
```

**Root image workflow:**
```bash
raw_device="$(hdiutil attach -nomount -imagekey diskimage-class=CRawDiskImage \
    images/qemu/panthera-root.img | awk 'NR==1 {print $1}')"
sleep 1; diskutil mount "${raw_device}s1" 2>/dev/null; sleep 1
# ... copy files to /Volumes/PantheraRoot/ ...
diskutil unmount /Volumes/PantheraRoot; hdiutil detach "$raw_device" -force
```

**Boot test:**
```bash
timeout 90 boot/qemu/run_phase2_qemu.sh --restage --no-reboot --ssh-port 0 \
    --root-disk images/qemu/panthera-root.img > /tmp/panthera_qemu_console.log 2>&1
tail -40 /tmp/panthera_qemu_console.log
```

**Key source directories:**
```
src/xnu-10002.41.9/libsyscall/mach/     Mach trap wrappers (Layer 3)
src/xnu-10002.41.9/osfmk/mach/          Mach trap definitions
src/libdispatch-1462.0.4/               GCD source (Layer 4)
src/Libinfo-583.0.1/                    User/group/DNS (Layer 6)
src/Libc-1583.40.7/                     C library source
src/libclosure-90/                      Blocks runtime
src/launchd-842.92.1/                   launchd source
```

## Success Criteria

| Layer | Done When |
|-------|-----------|
| 1. Fix PID 1 | mini_launchd fork/execs shell, stays alive, shell crash doesn't panic kernel |
| 2. Fix stubs | No duplicate symbols between dylibs. zsh reaches `%` prompt |
| 3. Mach IPC | `mach_msg` sends/receives. `mach_port_allocate` creates ports. ~35 Mach symbols exported from libsystem_kernel |
| 4. libdispatch | `dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, ...)` works. dispatch_queue_create works. Real event loop |
| 5. XPC | launchd links and initializes (stubs OK — just needs to not crash) |
| 6. Libinfo | `getpwnam("root")` returns entry from /etc/passwd |
| 7. Remaining | `bootstrap_port` initialized, `proc_pidinfo` works, Blocks runtime works |
| 8. Real launchd | Apple launchd runs as PID 1, reads plist, fork/execs zsh, supervises it, system stable for 10+ minutes |

## What Comes After

Once real launchd manages PID 1:
- Add a getty/console service plist
- Add SSH (dropbear, already in `src/`) as a launchd service
- Add networking via configd plist
- Each new daemon is just a launchd plist — the architecture scales
- This is how Apple intended it. Every macOS service is a launchd job.
