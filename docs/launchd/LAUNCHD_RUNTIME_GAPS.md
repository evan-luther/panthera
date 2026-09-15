# Panthera — launchd Runtime Status

> Status: historical subsystem status. Current authoritative project status is
> `docs/CURRENT_STATE.md`; use this file as launchd context only.

## Purpose

This document tracks the implementation status of Panthera's `launchd` runtime against the target Darwin bootstrap/job-manager model.

## Primary Files

- Panthera runtime: `userland/launchd/obj/launchd_all_stubs.c`
- Apple reference runtime: `src/launchd-842.92.1/src/runtime.c`
- Apple reference job manager: `src/launchd-842.92.1/src/core.c`
- Apple bootstrap client: `src/launchd-842.92.1/liblaunch/libbootstrap.c`

## Current Runtime: What Works

### Plist-Driven Service Loading — DONE
- XML plist parser handles Label, Program, ProgramArguments, RunAtLoad, KeepAlive, StandardOutPath, StandardErrorPath, MachServices
- Sorted plist scanning from `/System/Library/LaunchDaemons`
- Job records stored in static table (32 slots)

### Process Supervision — DONE
- fork/exec via `panthera_fork_child_aware` (raw fork syscall)
- RunAtLoad and KeepAlive semantics
- stdout/stderr file redirection
- Shell job detection with controlling terminal setup
- Delayed job spawning via kqueue timer (non-blocking)

### Mach IPC Primitives — DONE
- Inline assembly Mach traps for PID 1 (bypass dyld/shared cache issues)
- `mach_port_allocate`, `mach_port_insert_right`, `mach_port_deallocate`, `mach_reply_port`, `task_self`
- `mig_get_reply_port` / `mig_dealloc_reply_port` overrides for PID 1 (TLS broken without pthread kext)

### Bootstrap Server — DONE
- launchd owns a receive right for the bootstrap port
- Send right handed to children via `task_for_pid` + `task_set_special_port` (parent-side, with pipe synchronization)
- MachServices declarations parsed from plists and pre-registered
- Service registry: 64-slot static table with name, port, job association, checked_in state

### Bootstrap MIG Handlers — DONE
- `job_mig_check_in2`: allocates port, stores send right, returns receive right via MOVE_RECEIVE. Requests dead-name notification.
- `job_mig_look_up2`: finds service by name, returns send right via COPY_SEND
- `job_mig_register2`: accepts externally provided port for legacy registration
- `job_mig_intran` / `job_mig_destructor`: real implementations for MIG context management
- Combined MIG demux via `panthera_mig_demux` (routes to both `job_server` and `notify_server`)

### Event-Driven Runtime — DONE
- Main kqueue with `EVFILT_MACHPORT` for bootstrap messages
- `EVFILT_PROC` with `NOTE_EXIT` for child process reaping
- `EVFILT_TIMER` for delayed job spawning and periodic MIG message drain
- `kevent_mod()` is a real implementation
- Zero busy-wait: the main loop blocks on `kevent()`

### Job Manager — DONE
- Job lifecycle states: IDLE, RUNNING, EXITED
- `job_find()`: look up by label in Panthera's job table
- `job_dispatch()`: returns job context (Panthera uses its own spawn path)
- `job_stop()`: sends SIGTERM to running job
- `job_remove()`: stops job, cleans up services, unloads
- `panthera_job_reap()`: handles child exit, updates state, cleans up services, respawns if KeepAlive
- `panthera_job_cleanup_services()`: deallocates ports and resets checked_in state on job exit
- Dead-name notifications via `do_mach_notify_dead_name()`: cleans up service registry when ports die
- `launchd_mport_notify_req()`: real implementation for requesting port notifications

### Notification Handlers — DONE
- `do_mach_notify_dead_name`: real handler, cleans up service registry
- `do_mach_notify_no_senders`: acknowledges (KERN_SUCCESS)
- `do_mach_notify_port_deleted`: acknowledges
- `do_mach_notify_port_destroyed`: acknowledges
- `do_mach_notify_send_once`: acknowledges

### Stub Functions — DONE (proper error codes)
- All `job_mig_*` stubs return `BOOTSTRAP_NOT_PRIVILEGED` (not zero)
- Exception handlers return `KERN_FAILURE`
- `job_mig_post_fork_ping` and `job_mig_log` return `KERN_SUCCESS` (benign operations)

## Verified by Integration Tests

### test_mach_ipc — PASS
- Port allocate, send right insert, message send/receive, port deallocate

### test_bootstrap — PASS
- Bootstrap port inheritance from launchd to child
- Service check-in via raw MIG (message ID 402)
- Service look-up via raw MIG (message ID 404)
- End-to-end message round-trip through launchd-managed service port

## Explicitly Out of Scope

### XPC — OUT OF SCOPE
XPC is not implemented. All bootstrap behavior uses raw Mach IPC. This is acceptable because XPC is an Apple-private framework not required for basic Darwin service semantics.

### Per-User Domains — OUT OF SCOPE
Only a single root bootstrap namespace exists. Per-user launchd instances and domain forwarding are not implemented. This is acceptable for Panthera's headless server target.

### GUI/Bootstrap Sessions — OUT OF SCOPE
No GUI session management, no `bootstrap_subset`, no session switching. Not needed for server use.

### Socket Activation — OUT OF SCOPE
`Sockets` plist key and on-demand socket activation are not implemented. Services must manage their own sockets.

### Advanced Job Lifecycle — OUT OF SCOPE
`StartInterval`, `StartCalendarInterval`, `WatchPaths`, `QueueDirectories` conditions are not implemented. Only `RunAtLoad` and `KeepAlive` are supported.

### Jetsam / Resource Management — OUT OF SCOPE
No memory pressure handling, no jetsam, no idle-exit. Not applicable to Panthera's server environment.

### Shutdown Orchestration — OUT OF SCOPE
No coordinated shutdown sequence. PID 1 runs indefinitely.

## Known Limitations

### Shared Cache for PID 1
PID 1's dyld cannot map the shared cache because the ownership fix (chown to root) happens after PID 1 starts. Inline Mach trap assembly and `mig_get_reply_port` overrides work around this. Child processes benefit from the ownership fix.

### pthread Kext Not Loaded
The pthread kernel extension is not loaded, so TLS-based functions (including the library's `mig_get_reply_port`) don't work for PID 1. The `mig_get_reply_port` override in launchd uses a simple static variable and inline Mach reply port trap.

### Bootstrap Port Inheritance
After raw fork, MIG calls fail in the child (broken reply port). The parent sets the child's bootstrap port via `task_for_pid` + `task_set_special_port` with pipe synchronization.

### Single-Threaded Runtime
launchd runs single-threaded. The kqueue event loop handles all events sequentially. This is sufficient for Panthera's workload but differs from Apple's multi-threaded runtime.
