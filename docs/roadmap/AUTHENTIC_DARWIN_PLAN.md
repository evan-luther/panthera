# Panthera — Authentic Darwin Plan

## Purpose

This document defines the recommended technical path for Panthera if the goal is to become an authentic Darwin operating system rather than a collection of bootable stubs.

The core recommendation is:

- prioritize real Mach IPC
- prioritize authentic bootstrap semantics
- prioritize a real `launchd` job manager model
- treat XPC as a later compatibility layer, not the foundation

Panthera does not need full macOS parity to be authentic Darwin. It does need the real Darwin service model.

## What "Authentic Darwin" Means Here

Panthera should aim to satisfy the following statement:

> A bootable Darwin OS built primarily from Apple open-source components, using real XNU Mach IPC, real bootstrap-port semantics, and a real `launchd`-style job/service manager, with Panthera-owned integration only where Apple's open source is incomplete or too coupled to closed components.

This is not the same as:

- full macOS compatibility
- full Apple private framework compatibility
- full modern Apple XPC/domain/runtime parity

## Definitions

### Mach IPC

Mach IPC is Darwin's kernel-level message-passing system:

- ports
- rights
- `mach_msg()`
- `mach_port_*`
- task special ports
- MIG-generated request/reply interfaces

This is the substrate for bootstrap, service discovery, notifications, and many userland coordination paths.

### Bootstrap Semantics

Bootstrap is the Darwin service directory model:

- a process inherits a bootstrap port
- services register names with launchd
- clients resolve names through launchd
- launchd returns the relevant Mach send rights

This is the authentic Darwin service-discovery layer and matters more than XPC.

### Apple Job Manager

The job manager is the heart of `launchd`:

- parses plists
- stores job records
- spawns and supervises jobs
- owns bootstrap namespaces
- tracks Mach services
- applies restart and activation policy

In Apple source, this mostly lives in `src/launchd-842.92.1/src/core.c` plus `runtime.c`.

### XPC

XPC is Apple's higher-level structured IPC system layered on Mach:

- dictionaries
- arrays
- typed values
- fd and Mach port passing
- service and domain management helpers

Useful, but not the first thing Panthera needs for authenticity.

## Strategic Position

The right architecture priority is:

1. XNU + Mach IPC correctness
2. bootstrap correctness
3. launchd job-manager correctness
4. service usability and packaging
5. XPC compatibility where genuinely needed

If this order is reversed, Panthera risks becoming a bootable demo with permanent stubs instead of a credible Darwin system.

## Non-Negotiable Design Rules

1. Real kernel-backed Mach IPC must remain the source of truth.
2. Bootstrap lookups must behave like Darwin bootstrap lookups, not a fake local registry with incompatible semantics.
3. `launchd` must evolve toward a real job manager and namespace owner, not remain a hardcoded plist spawner forever.
4. XPC must not be allowed to drag the design away from the Mach/bootstrap core.
5. If Apple source is too coupled to private components, Panthera should reimplement behavior, not fake the interface and declare victory.

## Scope: Must Have, Nice To Have, Not Required

### Must Have For Authentic Darwin

- real `mach_msg()` path
- real `mach_port_*` primitives
- real MIG runtime and reply-port handling
- correct bootstrap-port inheritance and management
- `bootstrap_check_in()` / `bootstrap_look_up()` compatibility
- real `launchd` PID 1
- real job table / job lifecycle / process supervision
- kqueue-driven runtime loop
- Mach service registration and lookup
- stable plist-driven service management

### Nice To Have

- socket activation
- per-user bootstrap namespaces
- timer-based job activation
- better process and crash policy
- richer `launchctl` behavior
- partial XPC compatibility for launchd-adjacent tools

### Not Required For Authenticity

- full modern Apple XPC domains
- Apple-only launchproxy/xpcproxy behavior
- jetsam, quarantine, sandbox, Apple responsibility daemons
- full macOS private framework coverage
- GUI loginwindow stack

## Current Recommended Direction

Panthera should target:

- **real Mach IPC**
- **real Apple-style bootstrap semantics**
- **real Apple `launchd` binary where practical**
- **Panthera-owned job-manager/runtime subset where Apple OSS is too entangled**
- **minimal XPC compatibility only when required by the chosen launchd/runtime path**

This means Panthera can be authentic Darwin without pretending to have full Apple XPC parity.

## Architecture Target

### Layer A: Kernel / Mach Foundation

Goal:

- userspace Mach primitives are fully real and verified against the kernel

Required capabilities:

- `mach_msg`
- `mach_port_allocate`
- `mach_port_insert_right`
- `mach_port_move_member`
- `mach_port_request_notification`
- `task_get_special_port`
- `task_set_special_port`
- MIG reply-port lifecycle
- `mach_msg_server_once` where needed

Acceptance:

- self-send/self-receive test passes
- reply-port MIG call path passes
- bootstrap-port manipulation works without stubs

### Layer B: Bootstrap Compatibility

Goal:

- Panthera can support authentic bootstrap check-in and lookup behavior

Reference:

- `docs/launchd/BOOTSTRAP_COMPATIBILITY.md`

Required capabilities:

- `bootstrap_port` global behaves correctly
- `bootstrap_check_in()`
- `bootstrap_look_up()`
- `bootstrap_register()` if retained
- inherited bootstrap forwarding where required
- correct send/receive right ownership rules

Acceptance:

- a service registers by name
- a client resolves the name
- the returned right is usable for direct Mach IPC

### Layer C: Real launchd Runtime Core

Goal:

- `launchd` is no longer just a shell supervisor; it becomes the actual service manager

Required capabilities:

- launchd runs as PID 1
- owns bootstrap port / manager port
- owns job table
- supervises jobs with a real runtime loop
- uses kqueue for job/process events
- uses Mach receive paths for bootstrap traffic

Acceptance:

- launchd manages multiple jobs concurrently
- child exits are detected and handled by policy
- Mach bootstrap requests are serviced by launchd itself

### Layer D: Authentic Job Manager Subset

Goal:

- Panthera implements the real shape of the `launchd` job model, even if not every Apple feature is present

Required capabilities:

- parsed job identity
- program / argv / working directory / uid-gid basics
- `RunAtLoad`
- `KeepAlive`
- stdout/stderr path handling
- registered Mach services per job
- state transitions: loaded, running, exited, restartable, disabled

Acceptance:

- jobs loaded from plists behave predictably without hardcoded launch logic
- service registration belongs to jobs, not a flat global hack

### Layer E: Service Usability

Goal:

- Panthera becomes a real operating environment, not only a bootstrap demo

Required capabilities:

- networking service launch
- SSH daemon launch
- stable shell/login job
- package-management bootstrap after the service model is stable

Acceptance:

- service additions happen by staging a binary and plist, not editing launchd code

### Layer F: XPC Compatibility

Goal:

- add the minimum XPC support genuinely needed by selected userland pieces

Required capabilities:

- only the subset required by chosen launchd/runtime/client paths
- no claim of full Apple XPC parity unless it actually exists

Acceptance:

- XPC support is documented as partial, scoped, and verified

## What To Avoid

### 1. Do not make full XPC the first milestone

That would pull effort into a large Apple-specific compatibility surface before the authentic Darwin substrate is stable.

### 2. Do not freeze a fake bootstrap model

A simple name-to-port table is acceptable as a temporary bring-up aid, but not as the final bootstrap implementation if it diverges from Apple `libbootstrap` semantics.

### 3. Do not equate `MISSING 0` with runtime correctness

A binary that links is not the same thing as a subsystem that behaves correctly.

### 4. Do not let permanent stubs accumulate around core service-management paths

For launchd/bootstrap/Mach, stubs should be considered temporary unless they only cover truly nonessential Apple-private behavior.

## Recommended Phase Plan

### Phase 1: Mach IPC Audit And Lockdown

Deliverables:

- inventory of all Mach entry points Panthera exports today
- runtime tests for self-messaging, reply ports, notifications, and task special ports
- list of any remaining stubbed or behaviorally suspect Mach functions

Definition of done:

- Mach IPC is considered trusted infrastructure

### Phase 2: Bootstrap Semantics Plan

Deliverables:

- precise list of bootstrap operations Panthera must support
- mapping from Apple `libbootstrap` / `vproc_mig_*` client calls to Panthera runtime behavior
- decision on which Apple bootstrap behaviors are mandatory vs deferred
- concrete execution spec in `docs/launchd/BOOTSTRAP_COMPATIBILITY.md`

Definition of done:

- Panthera knows exactly what "bootstrap compatible" means

### Phase 3: launchd Runtime Rebase

Deliverables:

- reconcile current `launchd_all_stubs.c` behavior with the new target architecture
- remove obsolete hardcoded assumptions
- document temporary stubs that remain
- concrete current-vs-target runtime delta in `docs/launchd/LAUNCHD_RUNTIME_GAPS.md`

Definition of done:

- current runtime code is an explicit stepping stone, not an accidental fork

### Phase 4: Real Bootstrap Server

Deliverables:

- launchd-owned receive path for bootstrap requests
- registration and lookup by service name
- rights handling consistent with Apple expectations

Definition of done:

- a service can check in and a client can look it up through launchd

### Phase 5: Job Manager Maturation

Deliverables:

- job records tied to Mach services
- restart and supervision policy
- kqueue-driven runtime
- clear lifecycle transitions

Definition of done:

- Panthera has a real service manager, not just a plist spawner

### Phase 6: Service Platform

Deliverables:

- shell/login job
- network bring-up job
- SSH job
- package bootstrap path

Definition of done:

- Panthera is useful through the service model, not despite it

### Phase 7: Scoped XPC Compatibility

Deliverables:

- explicit minimal XPC implementation plan
- exact features needed
- exact features intentionally not implemented

Definition of done:

- XPC support is honest, bounded, and verified

## Authenticity Checklist

Panthera can credibly call itself an authentic Darwin OS when:

- the kernel is real XNU
- Mach IPC is real and tested
- bootstrap behavior is real and usable
- launchd is the true service authority
- jobs are launched and supervised through plists and job state, not hardcoded runtime hacks
- services communicate through real Darwin IPC/service-discovery paths
- the remaining divergences from Apple behavior are documented, narrow, and honest

## Recommended Messaging

Use language like:

- "real Mach IPC"
- "real bootstrap semantics"
- "Apple launchd-derived service model"
- "Panthera-owned compatibility/runtime layer where Apple OSS is incomplete"

Avoid language like:

- "full Apple XPC"
- "full macOS service stack"
- "stock launchd parity"

unless those things are truly implemented and verified.

## Execution Prompt

Use the following prompt for future work on this area:

```text
You are working on Panthera's authentic Darwin service stack.

Goal:
Advance Panthera toward a real Darwin OS by prioritizing:
1. real Mach IPC
2. real bootstrap semantics
3. a real launchd job/service manager
4. scoped XPC compatibility only when necessary

Rules:
- Do not treat XPC as the foundation.
- Do not add permanent stubs in core Mach/bootstrap/launchd paths unless they are clearly temporary and documented.
- Do not confuse link completeness with runtime correctness.
- Do not replace Apple bootstrap semantics with a simplified model unless it is explicitly marked as a temporary bring-up stage.
- Prefer behaviorally authentic Darwin design over superficially satisfying symbol tables.

When evaluating or implementing changes:
- first identify whether the change improves Mach correctness, bootstrap correctness, launchd correctness, or only cosmetic compatibility
- prioritize changes that make launchd a real service authority
- keep Apple source as reference for semantics
- keep Panthera-specific reimplementation honest and scoped

Deliverables should always state:
- what part is real Apple behavior
- what part is Panthera-owned behavior
- what remains stubbed or deferred
- whether the change improves authenticity or only temporary usability

Success condition:
Panthera becomes the easiest way to boot and inspect a modern Darwin system with real Mach IPC and real launchd/bootstrap behavior, even if some Apple-private XPC features remain out of scope.
```

## Next Documents To Create

- `docs/provenance/APPLE_PATCHES.md`
- `docs/launchd/XPC_SCOPE.md`
- `docs/tests/MACH_IPC_TEST_PLAN.md`

Those should turn this strategy into component-level engineering work.
