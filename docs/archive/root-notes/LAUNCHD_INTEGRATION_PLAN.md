# Panthera — Full launchd Integration Plan

Read `OS_BUILD_ROADMAP.md` for foundation rules. This document is the master plan for evolving Panthera's launchd from a minimal fork/exec stub into a real Mach bootstrap server with plist-driven job management.

## Current State

Panthera's launchd (`userland/launchd/obj/launchd_all_stubs.c`) replaces Apple's entire `runtime.c` and `core.c` with a simple loop:
- Hardcoded fork/exec of `/bin/zsh` in `panthera_spawn_shell()`
- Hardcoded fork/exec of `netbringup` in `panthera_spawn_network_bootstrap()`
- No plist parsing, no Mach IPC, no bootstrap server, no job management
- The real Apple source is at `src/launchd-842.92.1/src/` (core.c, runtime.c, ipc.c, launchd.c)

Adding a new service currently requires editing C code and recompiling. The goal is: drop a `.plist` file, reboot, service runs.

## Architecture Overview

```
Client Process                    launchd (PID 1)                   Service Process
     |                                  |                                  |
     |                           [reads plists at boot]                    |
     |                           [fork/execs services]-------------------->|
     |                                  |                                  |
     |                                  |    <---bootstrap_check_in()------|
     |                                  |    "I am com.panthera.dropbear"  |
     |                           [stores name → port]                      |
     |                                  |                                  |
     |---bootstrap_look_up()------>     |                                  |
     |   "find com.panthera.dropbear"   |                                  |
     |  <--[returns send right]---------+                                  |
     |                                                                     |
     |==================== direct Mach IPC (mach_msg) =====================|
```

launchd is both an init system (spawns processes) AND a service directory (processes find each other by name through the Mach bootstrap port). Every process inherits a bootstrap port from its parent, which points back to launchd. Services register by name, clients look up by name, and the kernel handles the actual message transport.

## What Exists vs What's Missing

| Component | Status |
|-----------|--------|
| Kernel Mach IPC (ports, messages, rights, traps) | Full implementation in XNU |
| MIG-generated stubs for launchd | Pre-compiled in `src/launchd-842.92.1/src/mig_gen/` (32 files) |
| `mach_msg()` syscall wrapper | **MISSING** from sysroot |
| `mach_port_allocate/insert_right/move_member` | **MISSING** (stubbed to return 0) |
| `task_get/set_special_port` | **MISSING** |
| `bootstrap_check_in/look_up/register` | **MISSING** |
| kqueue/kevent | Kernel supports it, `libsystem_kernel` exports it |
| libxpc | **Closed source** — 12KB stub dylib, all functions return NULL/0 |
| launchd `runtime.c` | Fully stubbed — replaced by simple fork/exec loop |
| launchd `core.c` (~8000 lines) | Fully stubbed — `job_s` has 170+ flags, complex lifecycle |
| Plist parsing | Not implemented |

## Apple's launchd Architecture (Reference)

### Key Data Structures (from core.c)

**`job_s` (job_t)** — A single managed process:
- Identity: `label`, `p` (PID), `uniqueid`
- Execution: `argv`/`argc`, `prog`, `rootdir`, `workingdir`, `username`, `groupname`
- Lifecycle: `start_time`, `nruns`, `last_exit_status`, `min_run_time`, `timeout`, `exit_timeout`
- I/O: `stdin_fd`, `stdoutpath`, `stderrpath`
- Mach ports: `j_port` (job port)
- Service list: `SLIST_HEAD(, machservice) machservices`
- Socket list: `SLIST_HEAD(, socketgroup) sockets`
- Dependencies: `SLIST_HEAD(, semaphoreitem) semaphores` (KeepAlive conditions)
- Environment: `SLIST_HEAD(, envitem) env`
- 170+ boolean flags for specialized behaviors

**`jobmgr_s` (jobmgr_t)** — A container of jobs (also the bootstrap server):
- `jm_port` — the Mach bootstrap port for this manager
- `LIST_HEAD(, job_s) jobs` — all jobs
- `LIST_HEAD(, job_s) active_jobs[ACTIVE_JOB_HASH_SIZE]` — running jobs by PID
- `LIST_HEAD(, machservice) ms_hash[MACHSERVICE_HASH_SIZE]` — service name lookup
- `jobmgr_t parentmgr` — hierarchy for cascading lookups
- `SLIST_HEAD(, jobmgr_s) submgrs` — child managers (XPC domains)

**`machservice`** — A registered Mach service:
- `job_t job` — owning job
- `mach_port_name_t port` — the actual Mach port
- `const char name[0]` — service name (flexible array)
- Flags: `isActive`, `recv`, `hide`, `per_pid`, `upfront`

### Runtime Loop (from runtime.c)

The real runtime is event-driven via kqueue + Mach port sets:
1. Create main kqueue (`mainkq`)
2. Create Mach port sets (`ipc_port_set`, `demand_port_set`)
3. Register `EVFILT_MACHPORT` on kqueue for port set
4. Main loop: `kevent()` → dispatch:
   - MACHPORT → `mach_msg()` receive → MIG demux → bootstrap handler
   - PROC/NOTE_EXIT → reap child → respawn if KeepAlive

The main loop uses `xpc_pipe_try_receive()` (closed-source libxpc) which can be bypassed with raw `mach_msg()` + MIG demux.

### MIG Interfaces (from mig_gen/)

| Interface | Subsystem ID | Purpose |
|-----------|-------------|---------|
| `job.defs` (jobServer) | 400 | Bootstrap protocol: check_in2, register2, look_up2, spawn2 |
| `job_forward.defs` | 400 | Forwarding for parent bootstrap lookups |
| `job_reply.defs` | 500 | Async reply messages |
| `internal.defs` | 137000 | `handle_kqueue()` for kqueue event forwarding |
| `helper.defs` | 4241011 | Helper downcall for wait status |
| `notify` (notifyServer) | — | Mach port death/no-senders notifications |
| `mach_exc` (mach_excServer) | — | Exception handling |

### Job Lifecycle: Plist → Running

1. **Parse**: Read plist XML → extract Label, ProgramArguments, MachServices, KeepAlive, etc.
2. **Import**: `job_import()` creates `job_s`, populates fields, registers machservices
3. **Dispatch**: `job_dispatch()` decides: start now (RunAtLoad), watch for demand, or ignore
4. **Start**: `job_start()` → `runtime_fork()` → child gets bootstrap port → `posix_spawn()`/`execve()`
5. **Supervise**: `kevent(EVFILT_PROC)` watches for NOTE_EXIT → `job_reap()`
6. **Restart**: If KeepAlive conditions met → `job_dispatch()` again

### Bootstrap Port Inheritance

1. launchd allocates `jm_port` (Mach receive right)
2. Before fork: `task_set_special_port(mach_task_self(), TASK_BOOTSTRAP_PORT, jm_port)`
3. Child inherits bootstrap port via kernel task special port mechanism
4. After fork: parent resets to `MACH_PORT_NULL`
5. Child calls `bootstrap_check_in()` / `bootstrap_look_up()` via MIG messages to `jm_port`

### Dependencies Apple's launchd Needs (that Panthera can strip)

**Keep:**
- Mach IPC (ports, messages, rights) — fundamental
- kqueue/kevent — event loop
- MIG stubs — bootstrap protocol
- POSIX: fork, exec, waitpid, signals, sockets

**Strip:**
- XPC domains / `xpc_singleton` / `xpc_bootstrapper` (Apple-only, use raw Mach instead)
- Jetsam / memory pressure (iOS-only)
- Sandbox and quarantine (`sandbox.h`, `quarantine.h`)
- BSM audit sessions (`libbsm.h`, `audit_session`)
- `vproc` transaction tracking (Apple idle-exit mechanism)
- `launchproxy` and `xpcproxy` wrappers (Apple-specific exec helpers)
- Multiple instances / dedicated instances (advanced)
- Shutdown/reboot orchestration (can add later)
- Responsibility daemon integration

---

## Phase L1: Minimal Plist Spawner

**Goal:** Drop a `.plist` file, service starts at boot. No Mach IPC.

**Implementation:**

1. **Minimal XML plist parser** in `launchd_all_stubs.c` (or a new file linked into the build). Only needs to handle:
   - `Label` (string) — service identifier
   - `ProgramArguments` (array of strings) — argv for execve
   - `Program` (string) — alternative to ProgramArguments[0]
   - `RunAtLoad` (boolean) — spawn at boot
   - `KeepAlive` (boolean) — restart if it exits
   - `StandardOutPath` / `StandardErrorPath` (string) — optional log redirect
   - Apple plists are XML. A simple hand-rolled parser (~200-300 lines) is fine. No libxml2 or CoreFoundation.

2. **Job scanner** — On boot, scan `/System/Library/LaunchDaemons/*.plist`. Parse each. Build a simple job table (static array, max 32 jobs).

3. **Job spawner** — For each job with `RunAtLoad=true`, fork/exec with the specified `ProgramArguments`. Set up console fd (stdin/stdout/stderr to `/dev/console`) same as `panthera_spawn_shell` does now. Store child PID in job table.

4. **Job supervisor** — Main loop calls `waitpid(-1, ...)` to reap any child. When a child exits, find it in the job table. If `KeepAlive=true`, respawn. If not, mark done.

5. **Create plist files:**
   - `/System/Library/LaunchDaemons/com.panthera.shell.plist` — [/bin/zsh, +m], RunAtLoad: true, KeepAlive: true
   - `/System/Library/LaunchDaemons/com.panthera.netbringup.plist` — [/sbin/netbringup, en0, 10.0.2.15, 255.255.255.0, 10.0.2.2, 10.0.2.2], RunAtLoad: true, KeepAlive: false
   - `/System/Library/LaunchDaemons/com.panthera.dropbear.plist` — [/usr/sbin/dropbear, -F, -E], RunAtLoad: true, KeepAlive: true

6. **Remove hardcoded spawning** — Delete `panthera_spawn_shell()`, `panthera_spawn_network_bootstrap()`, `panthera_run_network_bootstrap()`, and hardcoded argv arrays. Keep `panthera_boot_envp`.

7. **Stage plists** via `rootfs/create_hfs_root_image.sh` to `/System/Library/LaunchDaemons/` on the root image.

8. **Rebuild, restage, boot test.**

**Success:** Boot reaches zsh prompt AND netbringup runs AND both were spawned from plists. Serial log shows launchd reading each plist and spawning by label. Missing binaries (dropbear) log a warning and continue.

**What NOT to do:**
- Don't implement Mach IPC, XPC, or the real Apple job manager
- Don't use external libraries (libxml2, CoreFoundation)
- Don't modify Apple source in `src/launchd-842.92.1/`
- Don't touch the frozen dylibs or libSystem chain

---

## Phase L2: Mach IPC Primitives

**Goal:** The sysroot provides working `mach_msg()`, `mach_port_allocate()`, and related primitives.

**Implementation:**

These are all syscall wrappers that invoke Mach traps. The kernel already implements them. We need the userspace side.

**Required symbols:**
- `mach_msg()` / `mach_msg_overwrite_trap()` — trap -31, the fundamental send/receive
- `mach_port_allocate()` — create ports and port sets
- `mach_port_insert_right()` — make send rights from receive rights
- `mach_port_move_member()` — add ports to port sets
- `mach_port_deallocate()` — real implementation (replace current stub)
- `mach_port_request_notification()` — dead-name and no-senders notifications
- `mach_port_get_set_status()` — enumerate port set members
- `mach_port_mod_refs()` — adjust reference counts
- `task_get_special_port()` / `task_set_special_port()` — bootstrap port management
- `mach_reply_port()` — allocate reply ports for MIG calls
- `mig_get_reply_port()` / `mig_dealloc_reply_port()` — MIG reply port cache

**Approach:** Check if MIG-generated stubs for `mach_port` subsystem (3200) and `task` subsystem (3400) exist in `src/xnu-10002.41.9/`. If so, compile them. If not, write thin C/assembly wrappers that invoke the correct Mach traps.

**Where to put them:** Either in `libsystem_kernel` (rebuild from source, same approach as libc rebuild) or in `libpanthera_extra` (simpler, via `panthera_patch.sh`).

**Verification:** Write a test program that:
1. Allocates a Mach port
2. Inserts a send right
3. Sends a message to itself
4. Receives the message
5. Deallocates the port

This confirms the entire Mach IPC path works end-to-end through the kernel.

---

## Phase L3: Bootstrap Server

**Goal:** launchd acts as the Mach bootstrap server. Services can register and clients can look up by name.

**Implementation:**

1. On boot, launchd allocates `jm_port` (Mach receive right) — this IS the bootstrap port
2. When forking children, set child's bootstrap port to `jm_port` via `task_set_special_port(task, TASK_BOOTSTRAP_PORT, jm_port)`
3. After fork, parent resets its own bootstrap port
4. Implement core bootstrap operations using the pre-generated MIG stubs from `src/launchd-842.92.1/src/mig_gen/`:
   - `job_mig_check_in2(bootstrap_port, service_name, &port)` — service registers, gets receive right
   - `job_mig_look_up2(bootstrap_port, service_name, &port)` — client gets send right
   - `job_mig_register2(bootstrap_port, service_name, port, flags)` — legacy registration
5. Maintain a simple name→port hash table (static array of {name, port} pairs, not the full machservice struct)
6. Wire up MIG dispatch: listen on `jm_port` with `mach_msg()`, route to bootstrap handlers via the generated `job_server()` demux function

**Verification:** Dropbear calls `bootstrap_check_in("com.panthera.dropbear", &port)` successfully. A test client calls `bootstrap_look_up("com.panthera.dropbear", &port)` and gets a valid send right.

**Requires:** Phase L2 (Mach IPC primitives). Also requires building `libbootstrap` from `src/launchd-842.92.1/` — thin MIG client wrappers for `bootstrap_check_in()`, `bootstrap_look_up()`.

---

## Phase L4: Event-Driven Runtime Loop

**Goal:** Replace the waitpid loop with kqueue + Mach port set event dispatch.

**Implementation:**

1. Create main kqueue (`mainkq = kqueue()`)
2. Create Mach port set (`mach_port_allocate(MACH_PORT_RIGHT_PORT_SET, &port_set)`)
3. Add `jm_port` to port set
4. Register `EVFILT_MACHPORT` on kqueue for the port set — this bridges Mach IPC into kqueue
5. Register `EVFILT_PROC` with `NOTE_EXIT` for each spawned child
6. Main loop:
   ```c
   for (;;) {
       struct kevent events[32];
       int n = kevent(mainkq, NULL, 0, events, 32, NULL);
       for (int i = 0; i < n; i++) {
           switch (events[i].filter) {
           case EVFILT_MACHPORT:
               // Receive Mach message, MIG demux, handle bootstrap request
               break;
           case EVFILT_PROC:
               // Child exited — reap, check KeepAlive, respawn if needed
               break;
           }
       }
   }
   ```
7. **Bypass XPC entirely** — use raw `mach_msg()` instead of `xpc_pipe_try_receive()`. Old launchd versions worked this way before XPC existed.

**Verification:** Multiple services running simultaneously, launchd handles process exits and Mach messages without blocking.

---

## Phase L5: Real Job Manager (Selective core.c Port)

**Goal:** Port Apple's job lifecycle, stripping macOS-specific features.

**Keep from core.c:**
- Simplified `job_s` struct (~20 fields, not 170+ flags):
  - `label`, `prog`, `argv`, `argc`, `p` (PID), `mgr`
  - `start_time`, `last_exit_status`, `nruns`
  - `ondemand`, `keepalive`, `runatload`, `disabled`
  - `machservices` list, `sockets` list
  - `stdoutpath`, `stderrpath`, `workingdir`, `username`
- `job_new()` — create job from parsed plist
- `job_import()` / `job_import_keys()` — populate job from plist key-value pairs
- `job_dispatch()` — decision engine: start, watch, or ignore
- `job_start()` → `runtime_fork()` → `job_start_child()` — spawn path with bootstrap port setup
- `job_reap()` — cleanup on exit, respawn logic
- `job_remove()` — unload job
- `machservice_new()` — register Mach service port for job
- KeepAlive conditions: always, on crash, on success/failure, other-job-active
- Socket activation (launchd listens, passes fd to service on demand)
- `StartInterval` / `StartCalendarInterval` — timer-based scheduling

**Strip from core.c:**
- XPC domains, `xpc_singleton`, `xpc_service`, `xpc_bootstrapper`
- Jetsam / `kern_memorystatus` (iOS memory pressure)
- Sandbox (`sandbox.h`), quarantine (`quarantine.h`)
- BSM audit sessions (`libbsm.h`, `audit_session`)
- `vproc` transaction tracking (Apple idle-exit)
- `launchproxy` and `xpcproxy` exec wrappers
- Multiple instances / dedicated instances
- Shutdown/reboot orchestration (add later if needed)
- `responsibility.h` integration
- Embedded/OSInstaller special paths

**Approach:** Don't copy-paste core.c wholesale. Rewrite the job lifecycle using core.c as a reference, keeping only the fields and functions listed above. This avoids pulling in hundreds of Apple-internal dependencies.

---

## Phase L6: Bootstrap Client Library + XPC Shim

**Goal:** Services can call `bootstrap_check_in()` / `bootstrap_look_up()`. Minimal XPC for any code that needs it.

**Implementation:**

1. Build `libbootstrap` from the launchd source tree. The source is in `src/launchd-842.92.1/` — look for `bootstrap.c` or the MIG-generated `jobUser.c` which contains `vproc_mig_check_in2()`, `vproc_mig_look_up2()`, `vproc_mig_register2()`. These are thin wrappers: marshal args → `mach_msg()` → unmarshal reply.

2. Either link `libbootstrap` into `libSystem.B.dylib` (via re-export) or ship as a separate dylib.

3. Expand `libxpc_stubs.c` (`userland/libsystem/build/obj/libxpc_stubs.c`) to implement:
   - `xpc_pipe_try_receive()` — thin wrapper around `mach_msg()` receive + MIG demux fallthrough
   - `xpc_pipe_routine_reply()` — thin wrapper around `mach_msg()` send
   - This is NOT full XPC — just enough for launchd's internal message loop if we ever want to use `runtime.c` closer to stock

---

## Phase Summary

| Phase | What | Depends on | Effort | Unlocks |
|-------|------|-----------|--------|---------|
| **L1** | Plist spawner (fork/exec from XML) | Nothing | 1 session | Drop-a-plist service management |
| **L2** | Mach IPC primitives in sysroot | L1 or parallel | 1-2 sessions | Any Mach IPC in userspace |
| **L3** | Bootstrap server in launchd | L2 | 2-3 sessions | Services find each other by name |
| **L4** | Event-driven runtime (kqueue + Mach) | L2, L3 | 2-3 sessions | Concurrent service supervision |
| **L5** | Real job manager (selective core.c port) | L3, L4 | 3-5 sessions | Full Apple-compatible job lifecycle |
| **L6** | Client library + XPC shim | L3 | 1-2 sessions | Services use bootstrap API natively |

**Total: ~10-16 sessions for full integration.**

## Execution Notes

- **L1 is immediately actionable.** It requires no new infrastructure and gives plist-driven service management today.
- **L2 can run in parallel with L1.** The Mach IPC primitives are independent of plist parsing.
- **L3 is the conceptual hard step.** Understanding Mach port rights and MIG dispatch is the steepest learning curve.
- **L4 and L5 build incrementally** on the bootstrap server foundation.
- **L6 is optional** — only needed if services want to use the `bootstrap_*` API directly (most services just need to be spawned and supervised, which L1 handles).
- **At every phase, the plist interface stays the same.** The internal implementation evolves, but the user-facing model (drop a plist, service runs) never changes.

## What NOT to Do at Any Phase

- Do NOT modify Apple source in `src/launchd-842.92.1/`. Use it as reference only.
- Do NOT implement full XPC. It's closed-source and unnecessary — raw Mach IPC covers everything launchd needs.
- Do NOT implement Jetsam, sandbox, quarantine, or audit. These are macOS-specific and not needed for Panthera.
- Do NOT try to compile Apple's `core.c` or `runtime.c` directly. They have too many internal dependencies. Rewrite using them as reference.
- Do NOT break the existing boot path. Each phase must leave the system bootable. If the new code fails, fall back to the previous behavior.
