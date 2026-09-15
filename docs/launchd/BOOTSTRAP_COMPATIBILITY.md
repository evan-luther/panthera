# Panthera — Bootstrap Compatibility Specification

> Status: roadmap/specification. Current authoritative project status is
> `docs/CURRENT_STATE.md`; use this file as target behavior, not live status.

## Purpose

This document defines what Panthera must implement to claim authentic Darwin bootstrap behavior.

It is not a general overview. It is an execution specification for future work on:

- Mach IPC
- bootstrap-port semantics
- `launchd`
- job manager integration
- `libbootstrap` compatibility

The target audience is a coding agent or engineer who will continue launchd/bootstrap work and needs an explicit definition of done.

## Scope

This document is specifically about bootstrap compatibility:

- `bootstrap_port`
- `bootstrap_check_in()`
- `bootstrap_look_up()`
- related `vproc_mig_*` request/reply behavior
- launchd-owned service registration and lookup semantics
- bootstrap-port inheritance and forwarding

It is not a full XPC specification.

## Why This Matters

An authentic Darwin OS needs more than a plist spawner.

The real Darwin service model is:

1. launchd owns a bootstrap namespace
2. processes inherit a bootstrap port
3. services register themselves with launchd by name
4. clients resolve service names through launchd
5. launchd returns Mach rights that enable direct IPC

If Panthera does not implement this behavior correctly, it may still boot, but it will not have an authentic Darwin service model.

## Source References

Primary behavioral references in the current tree:

- Apple `libbootstrap` client logic in [src/launchd-842.92.1/liblaunch/libbootstrap.c](/Users/admin/panthera/src/launchd-842.92.1/liblaunch/libbootstrap.c)
- Apple launchd job manager behavior in [src/launchd-842.92.1/src/core.c](/Users/admin/panthera/src/launchd-842.92.1/src/core.c)
- Apple launchd runtime receive path in [src/launchd-842.92.1/src/runtime.c](/Users/admin/panthera/src/launchd-842.92.1/src/runtime.c)
- Panthera current launchd runtime stubs in [userland/launchd/obj/launchd_all_stubs.c](/Users/admin/panthera/userland/launchd/obj/launchd_all_stubs.c)

## Terminology

### Bootstrap Port

The Mach port used by a process to communicate with its bootstrap server, normally launchd.

### Bootstrap Server

The process that receives bootstrap requests and resolves or registers named services. In Panthera, this should be launchd.

### Check-In

Service-side operation: “I am service X; give me the appropriate right or bind me to this service identity.”

### Look-Up

Client-side operation: “Find service X and give me a send right to communicate with it.”

### Inherited Bootstrap Port

The bootstrap port a child process inherits from launchd or another launchd-owned context.

## Current Repo State

As of this audit:

- Panthera claims real Mach IPC is now live in the sysroot foundation.
- Panthera still describes `libxpc` as a stub surface.
- `launchd_all_stubs.c` currently implements plist scanning and process supervision, but all `job_mig_*` bootstrap handlers remain stubbed to return zero/null.

Important consequences:

1. The service-spawning path has advanced further than the older planning docs suggest.
2. Bootstrap compatibility is still incomplete at the actual service-directory layer.
3. Link completeness is not the same as behavioral compatibility.

## Design Goal

Panthera should support the following minimal but authentic Darwin bootstrap model:

1. launchd owns a receive right representing the bootstrap server
2. child jobs inherit a bootstrap port that points back to launchd
3. a service can call `bootstrap_check_in()` successfully
4. a client can call `bootstrap_look_up()` successfully
5. the returned right enables direct Mach IPC with the service
6. launchd, not the client or service, remains the authority for service-name resolution

## Required Behavioral Compatibility

### B1. `bootstrap_port` Must Be Real

Requirements:

- the process-global `bootstrap_port` must resolve from the task special port path
- `bootstrap_init()` style logic must work
- launchd must be able to assign or manage bootstrap special ports for children where required

Reference:

- [libbootstrap.c](/Users/admin/panthera/src/launchd-842.92.1/liblaunch/libbootstrap.c#L39)

Acceptance:

- a child process can obtain its bootstrap port without Panthera-specific hacks

### B2. `bootstrap_check_in()` Must Work

Requirements:

- a launched service can ask launchd to check in under a declared service name
- launchd validates the request against a registered job/service definition
- the result right is meaningful and usable
- duplicate or invalid registrations are rejected consistently

Reference:

- [libbootstrap.c](/Users/admin/panthera/src/launchd-842.92.1/liblaunch/libbootstrap.c#L135)
- [core.c](/Users/admin/panthera/src/launchd-842.92.1/src/core.c#L8877)

Acceptance:

- a test service calls `bootstrap_check_in("com.panthera.test", &port)` and receives a valid right

### B3. `bootstrap_look_up()` Must Work

Requirements:

- a client can resolve a named service through launchd
- launchd returns a usable send right
- the service can receive a Mach message sent to that right
- not-found behavior is distinguishable from success

Reference:

- [libbootstrap.c](/Users/admin/panthera/src/launchd-842.92.1/liblaunch/libbootstrap.c#L181)
- [core.c](/Users/admin/panthera/src/launchd-842.92.1/src/core.c#L9033)

Acceptance:

- a client resolves `com.panthera.test`, sends a Mach message, and the checked-in service receives it

### B4. Launchd Must Own Service Registration

Requirements:

- service registration must belong to the launchd job/service model
- a flat global “name to port” cache is acceptable only as a temporary bring-up aid
- the final model must tie registrations to jobs or launchd-managed service records

Reason:

- Apple launchd semantics are not just a dictionary; they are part of job lifecycle and namespace management

Acceptance:

- service entries are associated with jobs or launchd-managed service objects, not anonymous global state

### B5. Bootstrap-Port Forwarding Must Be Accounted For

Requirements:

- if Panthera supports inherited or forwarded bootstrap contexts, the behavior must be explicit and documented
- per-user forwarding can be deferred, but the implementation must not accidentally block future correctness

Reference:

- inherited/bootstrap forwarding logic in [core.c](/Users/admin/panthera/src/launchd-842.92.1/src/core.c#L6891)
- forwarding lookup path in [core.c](/Users/admin/panthera/src/launchd-842.92.1/src/core.c#L9155)

Acceptance:

- root/global bootstrap works correctly
- any deferred per-user forwarding behavior is clearly marked as deferred rather than silently broken

## Temporary Simplifications Allowed

The following are acceptable in an intermediate Panthera stage:

- single root bootstrap namespace only
- no per-user launchd namespace
- no GUI/bootstrap subsets
- no advanced `bootstrap_subset` or session switching
- no XPC domain integration
- no Apple-private launchd features

These are not acceptable as permanent substitutes:

- no-op `job_mig_*` handlers
- fake success from check-in or lookup paths
- bootstrap APIs that link but do not route through launchd authority

## Required Runtime Shape

### launchd Side

Panthera launchd must eventually provide:

- a receive path for bootstrap requests
- MIG demux for relevant request IDs
- a service table owned by launchd
- job-to-service association
- rights handling for registration and lookup

The exact internal data structure may differ from Apple source, but the semantics must not.

### Service Side

A service must be able to:

- inherit a bootstrap port
- call `bootstrap_check_in()`
- obtain the right launchd provides
- receive direct Mach traffic on the resulting endpoint

### Client Side

A client must be able to:

- inherit or initialize `bootstrap_port`
- call `bootstrap_look_up()`
- obtain a valid send right
- communicate with the service directly over Mach

## Interface Priorities

### Priority 1: Mandatory

- `bootstrap_port`
- `bootstrap_check_in`
- `bootstrap_look_up`
- `vproc_mig_check_in2`
- `vproc_mig_look_up2`
- launchd-side MIG dispatch for the matching requests

### Priority 2: Useful But Deferrable

- `bootstrap_register`
- `bootstrap_parent`
- `bootstrap_get_root`
- per-user lookup forwarding
- child/bootstrap subset behaviors

### Priority 3: Explicitly Out Of Scope For Now

- XPC domains
- launchproxy/xpcproxy
- Apple GUI/bootstrap session hierarchy

## Suggested Implementation Strategy

### Step 1: Confirm Mach Primitive Integrity

Before touching bootstrap behavior, verify:

- `mach_msg`
- `mach_port_allocate`
- `mach_port_insert_right`
- `mach_port_move_member`
- `task_get_special_port`
- `task_set_special_port`
- reply-port handling

If any of these are unreliable, bootstrap work will produce false positives.

### Step 2: Build Or Verify `libbootstrap` Client Path

Use Apple `libbootstrap` semantics as the client reference.

Questions to resolve:

- are the `vproc_mig_*` client wrappers built and actually routed correctly?
- does `bootstrap_port` come from the real special-port path?
- do replies carry valid rights and return codes?

### Step 3: Implement launchd-Owned Bootstrap Receive Path

Replace launchd bootstrap no-ops with:

- a real receive port or port set
- real request receive logic
- real request demux
- real check-in and lookup handlers

This can still be Panthera-owned runtime code. It does not need full stock Apple `core.c` parity on day one.

### Step 4: Tie Services To Jobs

Do not stop at “lookup works.”

Tie service registration to:

- plist-loaded job records
- declared Mach service names
- process lifecycle and cleanup

### Step 5: Add Explicit Failure Behavior

Bootstrap compatibility is not only success-path behavior.

Implement and test:

- not found
- duplicate check-in
- malformed requests
- dead service behavior
- invalid or stale rights

## Acceptance Test Matrix

These tests define the minimum behavior Panthera should pass.

### T1. Bootstrap Port Initialization

Test:

- start a child process
- child resolves its bootstrap port

Pass:

- bootstrap port is non-null and usable

### T2. Service Check-In

Test:

- launch a service job with declared service name
- service calls `bootstrap_check_in()`

Pass:

- call succeeds
- service receives a valid right

### T3. Client Look-Up

Test:

- client calls `bootstrap_look_up()` for that service

Pass:

- lookup succeeds
- client receives a send right

### T4. Direct Mach Round Trip

Test:

- client sends a Mach message to the looked-up service
- service receives and replies

Pass:

- message flow succeeds without Panthera-private side channels

### T5. Duplicate Registration Handling

Test:

- second service or repeated registration attempts same service name improperly

Pass:

- launchd returns a meaningful failure, not false success

### T6. Missing Service Lookup

Test:

- client looks up unknown service name

Pass:

- launchd returns not-found behavior, not a null right disguised as success

### T7. Service Exit / Cleanup

Test:

- service checks in
- service exits
- client attempts new lookup or use of stale path

Pass:

- launchd and/or port state behaves predictably and does not leave silent corruption

## Current Panthera Gaps To Close

Based on repo state, likely gaps include:

- `job_mig_*` bootstrap handlers in [launchd_all_stubs.c](/Users/admin/panthera/userland/launchd/obj/launchd_all_stubs.c#L994) are still no-ops
- current launchd runtime is still centered on plist spawning and `waitpid`, not a bootstrap receive path
- `libxpc` remains a stub and should not be relied on for bootstrap correctness
- older planning documents still describe bootstrap work in oversimplified terms

## Definition Of Done

Bootstrap compatibility is good enough when:

- Apple-style `libbootstrap` client behavior works for the supported root namespace
- launchd actually services bootstrap requests
- service names resolve to usable Mach communication endpoints
- jobs and services are associated structurally
- Panthera no longer relies on bootstrap no-op shims for normal service behavior

## Explicit Non-Goals

This document does not require:

- full Apple `core.c` port
- full XPC parity
- per-user domains immediately
- GUI session bootstrap
- all Apple-private launchd features

Those can come later. Bootstrap correctness cannot.

## Execution Prompt

Use this prompt for future bootstrap work:

```text
You are implementing Panthera bootstrap compatibility.

Goal:
Make Panthera behave like a real Darwin bootstrap environment for the supported root namespace.

Priorities:
1. real Mach IPC primitives
2. real bootstrap-port behavior
3. real launchd-owned check-in and lookup semantics
4. correct job-to-service association

Rules:
- Do not treat successful linking as proof of bootstrap correctness.
- Do not leave `job_mig_*` handlers as fake success paths.
- Do not replace bootstrap semantics with a flat global map unless it is a clearly temporary bring-up step.
- Keep Apple `libbootstrap.c`, `core.c`, and `runtime.c` as semantic references.
- Keep XPC out of scope unless it is directly required for the chosen bootstrap path.

Deliverables must state:
- what request path is now real
- which bootstrap operations are supported
- which are deferred
- how rights are created, returned, and cleaned up
- which acceptance tests now pass

Success:
A service can check in with launchd by name, a client can look it up by name, and they can communicate through real Mach IPC using launchd-managed bootstrap semantics.
```
