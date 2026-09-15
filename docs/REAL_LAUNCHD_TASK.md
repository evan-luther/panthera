# Real launchd — Replace launchd_all_stubs.c with Apple's runtime.c + core.c

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules. Read `docs/launchd/LAUNCHD_RUNTIME_GAPS.md` and `LAUNCHD_INTEGRATION_PLAN.md` for background.

The platform layer is complete: CoreFoundation (1,562 exports), XPC (real implementation with Mach IPC), Mach bootstrap server, kqueue, libc++, ICU, objc4 — all working. The current launchd (`userland/launchd/obj/launchd_all_stubs.c`, 2,338 lines) is a Panthera-written shim that provides plist parsing, fork/exec spawning, bootstrap check-in/look-up, kqueue event loop, and job lifecycle. It works, but it's not Apple's launchd.

This task replaces it with Apple's real `runtime.c` + `core.c` from `src/launchd-842.92.1/src/`, making launchd the genuine Darwin article.

## What We're Replacing

Current `launchd_all_stubs.c` provides:
- Plist scanning + CF-based parsing (now via CFPropertyList)
- Static job table (32 slots) with fork/exec spawning
- Mach bootstrap server (check_in, look_up, register via MIG)
- kqueue event loop (EVFILT_MACHPORT, EVFILT_PROC, EVFILT_TIMER)
- Job lifecycle (reap, respawn, KeepAlive, service cleanup)
- Boot infrastructure (remount root, fix shared cache, set hostname)
- Inline Mach trap wrappers for PID 1

## What Apple's Real launchd Provides (that we don't have)

- Full `job_s` struct with proper lifecycle states (170+ fields, but most are boolean flags)
- `jobmgr_s` hierarchy (root → per-user → XPC domain managers)
- Socket activation (launchd listens on sockets, passes fd to service on demand)
- `StartInterval` / `StartCalendarInterval` timer-based scheduling
- Complex `KeepAlive` policy evaluation (SuccessfulExit, NetworkState, PathState, OtherJobEnabled)
- `WatchPaths` / `QueueDirectories` — file-system triggered job start
- Proper `job_import()` with full plist key support
- `runtime_fork()` with proper bootstrap port handoff
- MIG-based job management through `xpc_pipe_try_receive()` → `launchd_mig_demux()`
- Process group management, resource limits, environment setup

## Source Files

From `src/launchd-842.92.1/src/`:

| File | Lines | Purpose | Action |
|------|-------|---------|--------|
| `launchd.c` | 641 | Main entry, PID 1 bootstrap, signal handling | COMPILE — moderate stripping |
| `runtime.c` | 1,457 | kqueue event loop, Mach message dispatch, port management | COMPILE — core event infrastructure |
| `core.c` | 11,984 | Job manager, plist import, job lifecycle, MachServices, bootstrap | COMPILE — heavy stripping of unused subsystems |
| `ipc.c` | 537 | XPC pipe receive, message demux | COMPILE — replace xpc_pipe with raw mach_msg |
| `log.c` | ~200 | Logging infrastructure | COMPILE or STUB |
| `kill2.c` | ~100 | Extended kill semantics | COMPILE |
| `ktrace.c` | ~100 | Kernel trace points | STUB (no-op) |

Plus 32 pre-generated MIG files in `mig_gen/` — these compile as-is.

Plus headers from `liblaunch/`: `bootstrap.h`, `bootstrap_priv.h`, `launch.h`, `launch_priv.h`, `launch_internal.h`, `vproc.h`, `vproc_priv.h`, `vproc_internal.h`, `reboot2.h`.

## What to Strip from core.c

core.c is 11,984 lines but ~30% is macOS-specific subsystems that Panthera doesn't need. Strip these by wrapping in `#if 0` or `#ifdef PANTHERA_STRIP` blocks:

| Subsystem | References | Action |
|-----------|-----------|--------|
| **Jetsam / memory pressure** | 100 lines | `#if 0` — iOS/macOS memory management |
| **Sandbox** | 60 lines | `#if 0` — no sandbox on Panthera |
| **BSM Audit** | 31 lines | `#if 0` — no audit framework |
| **XPC domains / xpc_bootstrapper** | 59 lines | `#if 0` — Apple-private XPC domain management. Use simple flat namespace instead. |
| **XPC event channels** | 32 lines | `#if 0` — event-based job triggers |
| **Responsibility tracking** | ~20 lines | `#if 0` — Apple-private |
| **Embedded/OSInstaller paths** | ~30 lines | `#if 0` — not applicable |

**Keep everything else** — especially:
- `job_new()`, `job_import()`, `job_import_keys()` — job creation from plist
- `job_dispatch()` — start/watch/ignore decision engine
- `job_start()`, `runtime_fork()`, `job_start_child()` — process spawning with bootstrap port setup
- `job_reap()` — exit handling, respawn logic
- `job_remove()`, `job_stop()`, `job_kill()` — lifecycle management
- `machservice_new()`, `machservice_delete()` — Mach service registration
- `jobmgr_init()`, `jobmgr_new()` — manager initialization
- `jobmgr_find_by_name()` — service lookup
- `job_mig_check_in2()`, `job_mig_look_up2()`, `job_mig_register2()` — MIG bootstrap handlers
- `job_mig_intran()`, `job_mig_destructor()` — MIG context conversion
- Socket activation infrastructure
- KeepAlive condition evaluation
- Timer-based scheduling

## Private Headers Needed

Apple's launchd uses private headers that aren't in the SDK. Create shims:

### `<xpc/launchd.h>` — XPC internal API for launchd

This defines the XPC pipe API that launchd uses internally. The key functions:

```c
/* Shim: xpc/launchd.h */
#ifndef _XPC_LAUNCHD_H
#define _XPC_LAUNCHD_H

#include <xpc/xpc.h>
#include <mach/mach.h>

/* xpc_pipe — represents a Mach-based message channel.
 * In Panthera, we implement this using raw mach_msg(). */
typedef struct xpc_pipe_s *xpc_pipe_t;

/* Create a pipe from a Mach receive right */
xpc_pipe_t xpc_pipe_create_from_port(mach_port_t port, uint64_t flags);

/* Receive a message on the pipe. On real macOS this does
 * mach_msg receive + XPC deserialization. */
int xpc_pipe_try_receive(mach_port_t port, xpc_object_t *msg_out,
    mach_port_t *reply_port_out, boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
    mach_msg_size_t max_msg_size, uint64_t flags);

/* Send a reply */
int xpc_pipe_routine_reply(xpc_object_t reply);

/* Domain management — stub these */
xpc_object_t xpc_domain_import_services(void *mgr, xpc_object_t services);
int xpc_domain_check_in(void);
const char *xpc_domain_get_service_name(void);

#endif
```

### `<xpc/private.h>` — XPC private types

```c
/* Types used by core.h */
typedef void *xpc_service_type_t;
typedef uint32_t xpc_jetsam_band_t;
/* Stub defines */
#define XPC_SERVICE_JETSAM_BAND_NONE 0
```

### `<os/assumes.h>` — Assertion macros

```c
#define os_assert(e) do { if (!(e)) { abort(); } } while(0)
#define os_assumes(e) (e)
#define posix_assert(e, err) do { if (!(e)) { abort(); } } while(0)
#define os_assert_zero(e) do { if ((e) != 0) { abort(); } } while(0)
```

### `<bsm/libbsm.h>` — Audit stubs

```c
typedef struct { uint32_t val[8]; } audit_token_t;
#define AU_DEFAUDITID 0
static inline uid_t audit_token_to_euid(audit_token_t t) { return t.val[1]; }
static inline pid_t audit_token_to_pid(audit_token_t t) { return (pid_t)t.val[5]; }
```

### `<_simple.h>` — Simple string formatting

```c
typedef char *_SIMPLE_STRING;
static inline _SIMPLE_STRING _simple_salloc(void) { return NULL; }
static inline void _simple_sfree(_SIMPLE_STRING s) { (void)s; }
static inline int _simple_sprintf(_SIMPLE_STRING s, const char *fmt, ...) { return 0; }
static inline const char *_simple_string(_SIMPLE_STRING s) { return s ? s : ""; }
```

## XPC Pipe Implementation

runtime.c uses `xpc_pipe_try_receive()` to receive messages on the bootstrap port. On real macOS, this does XPC deserialization. On Panthera, implement it as a thin wrapper around `mach_msg()` + MIG demux:

```c
int xpc_pipe_try_receive(mach_port_t port, xpc_object_t *msg_out,
    mach_port_t *reply_port_out,
    boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
    mach_msg_size_t max_msg_size, uint64_t flags)
{
    /* Try to receive a Mach message with timeout */
    mach_msg_return_t kr;
    struct {
        mach_msg_header_t header;
        char body[4096];
    } request, reply;
    
    kr = mach_msg(&request.header, MACH_RCV_MSG | MACH_RCV_TIMEOUT,
        0, sizeof(request), port, 0, MACH_PORT_NULL);
    
    if (kr == MACH_RCV_TIMED_OUT)
        return 0; /* no message pending */
    if (kr != MACH_MSG_SUCCESS)
        return -1;
    
    /* Dispatch through MIG demux */
    memset(&reply, 0, sizeof(reply));
    if (demux(&request.header, &reply.header)) {
        /* MIG handled it — send reply if needed */
        if (reply.header.msgh_remote_port != MACH_PORT_NULL) {
            mach_msg(&reply.header, MACH_SEND_MSG,
                reply.header.msgh_size, 0,
                MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        }
    }
    
    return 1; /* message handled */
}
```

## Panthera Boot Infrastructure

The current `launchd_all_stubs.c` has Panthera-specific boot code that Apple's launchd doesn't have:
- `panthera_remount_root_rw()` — remount / read-write
- `panthera_fix_shared_cache_ownership()` — fix dyld cache permissions
- `panthera_fix_ping_permissions()` — setuid on ping
- `panthera_stub_console_setup()` — open /dev/console
- Hostname setting from `/etc/hostname`
- Inline Mach trap wrappers (for PID 1 before dyld resolves)

**These must be preserved.** Create a `panthera_boot.c` file that contains all Panthera-specific boot logic, compiled alongside Apple's source files. Call `panthera_boot_init()` from `launchd_main()` before any Apple initialization.

## Build Approach

### Step 1: Prepare the source tree

Create `userland/launchd/real/` with:
- Symlinks or copies of Apple source files from `src/launchd-842.92.1/src/`
- Shim headers in `userland/launchd/real/shims/`
- `panthera_boot.c` with Panthera-specific boot code
- `panthera_xpc_pipe.c` with the xpc_pipe implementation

### Step 2: Strip macOS-only subsystems

Create a `panthera_launchd_config.h` that `#define`s what to strip:

```c
/* panthera_launchd_config.h — feature flags for Panthera launchd */
#define HAVE_SANDBOX 0
#define HAVE_JETSAM 0
#define HAVE_AUDIT 0
#define HAVE_XPC_DOMAINS 0
#define HAVE_XPC_EVENTS 0
#define HAVE_RESPONSIBILITY 0
#define HAVE_QUARANTINE 0
#define HAVE_EMBEDDED 0
```

Then wrap stripped sections in core.c with:
```c
#if HAVE_SANDBOX
    // sandbox code
#endif
```

This is better than deleting code — it's clear what was stripped and can be re-enabled later.

### Step 3: Compile

```bash
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"

CFLAGS="-target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2"
CFLAGS+=" -I userland/launchd/real/shims"
CFLAGS+=" -I src/launchd-842.92.1/liblaunch"
CFLAGS+=" -I src/launchd-842.92.1/src"
CFLAGS+=" -I src/launchd-842.92.1/src/mig_gen"
CFLAGS+=" -include panthera_launchd_config.h"
CFLAGS+=" -DPANTHERA=1"

# Compile each source file
for src in launchd.c runtime.c core.c ipc.c log.c kill2.c ktrace.c \
           panthera_boot.c panthera_xpc_pipe.c; do
    $CC $CFLAGS -c "$src" -o "obj/${src%.c}.o"
done

# Compile MIG server stubs
for mig_src in src/launchd-842.92.1/src/mig_gen/*Server.c; do
    name=$(basename "$mig_src" .c)
    $CC $CFLAGS -c "$mig_src" -o "obj/${name}.o"
done

# Link
$CC $CFLAGS -o userland/launchd/launchd \
    obj/*.o \
    -lSystem -lCoreFoundation \
    -sectcreate __TEXT __info_plist /dev/null
```

### Step 4: Handle compilation errors

Expect 50-100 compilation errors on first attempt. Categories:

1. **Missing private headers** — create shims (see above)
2. **Missing private functions** — stub with appropriate return values
3. **Stripped subsystem references** — add `#if HAVE_*` guards
4. **Type mismatches** — Apple's types may differ from what's in the SDK headers
5. **MIG interface changes** — the pre-generated MIG stubs must match core.c's expectations

Work through errors systematically. Don't skip files — every `.c` file must compile.

### Step 5: Preserve fallback

**Keep `launchd_all_stubs.c` in the repo.** The build script should have a flag:

```bash
PANTHERA_LAUNCHD_REAL=1  # Use Apple's real launchd
PANTHERA_LAUNCHD_REAL=0  # Use the Panthera shim (fallback)
```

If the real launchd fails to boot, you can revert by setting the flag to 0.

## Testing

### Test 1: Basic boot

Boot with the real launchd. It must:
- Reach a login prompt
- Spawn all services from plists (login, netbringup, sshd, syslogd)
- SSH must work
- KeepAlive must work (kill a service, it restarts)

### Test 2: New capabilities

Test features the shim didn't have:
```bash
# Socket activation (if a plist defines Sockets)
# StartInterval
# StartCalendarInterval
# WatchPaths
# Complex KeepAlive conditions
```

### Test 3: Bootstrap operations

```bash
# From SSH session:
/usr/bin/test_bootstrap   # check-in + look-up + message round-trip
/usr/bin/test_mach_ipc    # Mach IPC primitives
```

## Risk Mitigation

This is the highest-risk change in Panthera's history. If it fails, the system doesn't boot.

1. **Backup image:** `cp images/qemu/panthera-root.img images/qemu/panthera-root.img.pre-real-launchd`
2. **Keep the shim:** `launchd_all_stubs.c` stays in the repo, build script has a toggle
3. **Incremental approach:** Get `launchd.c` + `runtime.c` compiling first (event loop). Then add `core.c` (job manager). Don't try all at once.
4. **Compare behavior:** For each plist key, verify the real launchd handles it the same as the shim

## Deliverables

Report:
1. Which Apple source files compile (all should)
2. What was stripped (with `#if HAVE_*` guards)
3. How many shim headers were created
4. What the `panthera_boot.c` contains (preserved boot logic)
5. How `xpc_pipe_try_receive` is implemented
6. Boot test results — does the system boot with real launchd?
7. Which new launchd capabilities now work (socket activation, complex KeepAlive, etc.)
8. SSH test from host machine

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- DO NOT delete `launchd_all_stubs.c` — keep as fallback
- Strip macOS subsystems with `#if HAVE_*` guards, not by deleting code
- Preserve ALL Panthera boot infrastructure (remount, cache fix, hostname)
- The Mach inline trap wrappers MUST stay (PID 1 can't use dyld for early syscalls)
- Boot test and read output — don't ask the user
- No deferred work — the system must boot and all existing services must start
- If something can't be made to work, document exactly why and keep the shim fallback

## Success Criteria

- Panthera boots with Apple's real `runtime.c` + `core.c`
- All existing plists load and services start
- SSH works from host (`ssh -p 2222 root@localhost`)
- `test_bootstrap` and `test_mach_ipc` pass
- KeepAlive respawn works
- Boot is not significantly slower
- At least one new capability works (socket activation OR StartInterval OR complex KeepAlive)
