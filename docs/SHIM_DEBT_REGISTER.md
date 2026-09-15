# Panthera Shim Debt Register

> **Status: TECHNICAL DEBT LEDGER.**
> For the authoritative present-state summary, see **`docs/CURRENT_STATE.md`**.
> For source and patch ledgers, see **`docs/provenance/`**.

Last updated: 2026-08-28.

## Purpose

This document records Panthera compatibility shims, stubs, duplicate exports,
and temporary runtime bridges as explicit engineering debt.

The goal is not to delete every shim immediately. The goal is to make every
shim understandable: what owns it, what behavior it provides, whether it is
safe, and what must replace it.

## Classification

| Class | Meaning | Required Action |
|---|---|---|
| `real` | Implements enough real behavior for Panthera's target | Keep tested and documented |
| `safe-stub` | No-op or disabled behavior is correct for optional functionality | Keep only if documented |
| `visible-failure` | Unsupported behavior fails clearly with an error | Keep until real owner exists |
| `dangerous-stub` | Can silently mislead callers or mask real behavior | Fix or remove from export path |
| `duplicate-export` | Same symbol is exported by multiple re-exported dylibs | Remove, justify, or encode as expected-fail |
| `dead-code` | File or symbol is not part of the active build | Remove or mark inactive |
| `transition` | Temporary Panthera bridge with a named replacement | Track replacement and verification |

## Current Active libpanthera_extra Inputs

`userland/libsystem/build/relink_libpanthera_extra.sh` currently builds
`libpanthera_extra.dylib` from these active inputs:

- `userland/libsystem/build/obj/panthera_extra_core.c`
- `userland/libsystem/build/obj/panthera_extra_stubs.c`
- `userland/libsystem/build/obj/panthera_extra_bridge.c`
- `userland/libsystem/build/obj/panthera_extra_aliases.s`
- `userland/libsystem/build/panthera_fmtcheck_wrapper.c`
- `userland/libsystem/build/obj/panthera_cache_bridge_stubs.c`
- `userland/libsystem/build/obj/panthera_cache_bridge_aliases.s`
- `userland/libsystem/build/obj/panthera_tier1_syms.c`, if present
- `userland/libsystem/build/libpanthera_extra_strip_symbols.txt`

The grouped translation units include additional implementation files:

- `panthera_resolve_impl.c`
- `panthera_mach_globals.c`
- `panthera_malloc_override.c`
- `panthera_missing.c`
- `panthera_pthread_simple.c`
- `panthera_resolve_wave2.c`
- `panthera_resolve_wave3.c`

## Current Verification State

Current evidence:

- `artifacts/runtime/phase6-dead-shim-cleanup-20260508.summary.md`
- `artifacts/runtime/phase6-dead-shim-cleanup-boot-20260508.summary.md`
- `artifacts/runtime/phase6-dead-shim-cleanup-boot-20260508.runtime_debt.md`
- `artifacts/boot/phase6-dead-shim-cleanup-boot-20260508-boot.log`
- `artifacts/boot/phase6-dead-shim-cleanup-boot-20260508-boot.summary.md`
- `artifacts/boot/phase6-dead-shim-cleanup-boot-20260508-boot.verify_exports.log`

Current result:

- `launchd`, `zsh`, and `dyld` resolve all undefined symbols.
- All 58 critical symbols are present.
- No duplicate exports remain between re-exported dylibs.
- `verify_exports.sh` passes.
- `launchd` is linked with two-level namespace.
- Runtime debt audit is part of `tools/runtime_foundation_gate.sh`.

### 2026-08-28 recovery audit

- `userland/libsystem/verify_exports.sh` passed with 32,662 exported symbols,
  no checked unresolved symbols, and no duplicate exports.
- The runtime still depends on transitional resolver, early-allocation, Mach,
  pthread, dyld/ObjC, cache, sandbox, and launchd compatibility layers listed
  below; passing export checks do not prove full behavioral compatibility.
- No clean world build from a fresh checkout has been proven. Current generated
  runtime artifacts are therefore evidence and staging inputs, not proof that
  every runtime layer rebuilds from source in one pass.

Prior export-clean evidence:

- `artifacts/boot/export-clean-20260508-152011.verify_exports.log`
- `artifacts/boot/export-clean-20260508-152011.log`
- `artifacts/boot/export-clean-20260508-152011.summary.md`

Prior Phase 0 evidence:

- `artifacts/boot/baseline-20260508-145804.verify_exports.log`
- `docs/CURRENT_STATE.md`

Prior Phase 0 result:

- `launchd`, `zsh`, and `dyld` resolve all undefined symbols.
- All 58 critical symbols are present.
- `verify_exports.sh` failed because 18 duplicate exports remained.

## Resolved Duplicate-Export Debt

These were the active failures from the Phase 0 link check. They were corrected
on 2026-05-08 and verified by
`artifacts/boot/export-clean-20260508-152011.verify_exports.log`.

| Symbols | Former Providers | Canonical Owner | Class | Status |
|---|---|---|---|---|
| `_thread_resume`, `_thread_suspend` | `libsystem_kernel.dylib`, `libpanthera_extra.dylib` via `panthera_cache_bridge_stubs.c` | `libsystem_kernel.dylib` | `duplicate-export` | Stripped from `libpanthera_extra`; export check and boot pass. |
| `_bootstrap_port` | `libsystem_kernel.dylib`, `libsystem_info.dylib` | `libsystem_kernel.dylib` | `duplicate-export` | `libsystem_info.dylib` link now hides `_bootstrap_port`; export check and boot pass. |
| `__os_log_debug_impl`, `__os_log_default`, `__os_log_error_impl`, `__os_log_fault_impl`, `__os_log_impl`, `_os_log_create`, `_os_log_type_enabled` | `libpanthera_extra.dylib`, `libsystem_trace.dylib` | `libsystem_trace.dylib` | `duplicate-export` | Stripped from `libpanthera_extra`; `libsystem_trace.dylib` is included in the real shared-cache path. |
| `_bootstrap_look_up`, `_notify_cancel`, `_notify_check`, `_notify_get_state`, `_notify_peek`, `_notify_post`, `_notify_register_check`, `_notify_register_plain` | `libpanthera_extra.dylib`, `libsystem_notify.dylib` | `libsystem_notify.dylib` plus bootstrap owner | `dangerous-stub` and `duplicate-export` | Stripped from `libpanthera_extra`; `libsystem_notify.dylib` is included in the real shared-cache path. |

## Active Shim Groups

| Group | Current Location | Behavior | Class | Replacement Target | Verification |
|---|---|---|---|---|---|
| Panthera DNS resolver | `panthera_extra_bridge.c` | Standalone `getaddrinfo()` path reading `/etc/resolv.conf` and sending UDP DNS queries | `transition` | Real Libinfo plus mDNSResponder/configd path | Network smoke: resolver, TCP, fallback behavior. |
| malloc early override | `panthera_malloc_override.c` included by `panthera_extra_core.c` | Provides early allocation compatibility | `transition` | Real libmalloc and dyld/libSystem init ordering | Boot-to-login plus allocator-specific tests. |
| Mach globals and task/host wrappers | `panthera_mach_globals.c`, `panthera_resolve_impl.c` | Supplies Mach globals and early wrappers | `transition` | `libsystem_kernel.dylib` | Mach IPC and bootstrap round-trip tests. |
| pthread simple support | `panthera_pthread_simple.c` included by `panthera_extra_stubs.c` | Single-thread or limited pthread compatibility | `transition` | `libsystem_pthread.dylib` plus pthread kext/runtime support | Thread smoke tests once pthread runtime is in scope. |
| dyld/ObjC bridge stubs | `panthera_extra_stubs.c` | Dyld introspection and ObjC callbacks, many reduced behaviors | `safe-stub` or `transition` by symbol | Real dyld/objc runtime support | ObjC/CoreFoundation smoke tests. |
| cache bridge stubs | `panthera_cache_bridge_stubs.c` | cache compatibility exports; former os_log/thread/bootstrap/notify duplicates are stripped from `libpanthera_extra` | Mixed, includes historical `dangerous-stub` debt | Dedicated real owners: kernel, trace, notify, bootstrap | Continue symbol-by-symbol audit after duplicate-export cleanup. |
| trace stubs | `libsystem_trace_stub.c`, `build_libsystem_trace.sh` | Dedicated no-op os_log provider | `safe-stub` | Real libsystem_trace if needed | Export check and logging smoke. |
| sandbox stubs | `libsystem_sandbox_stub.c`, `build_libsystem_sandbox.sh` | Dedicated sandbox no-op/error provider | `safe-stub` | Real sandbox only if needed | Link and daemon behavior checks. |
| XPC implementation | `libxpc_impl.c` | Panthera XPC object/model/connection implementation | `real` or `transition` | Continue hardening against launchd/bootstrap behavior | XPC client/server smoke and boot. |
| legacy XPC stubs | `libxpc_stubs.c` | Historical reduced XPC surface | `dead-code` if not linked | `libxpc_impl.c` | Confirm active build no longer uses stubs. |

## Dead Or Inactive-Looking Shim Files

`docs/SYSROOT_STUB_AUDIT.md` previously identified several files as not
compiled into active libraries. Phase 6 rechecked these with
`tools/audit_runtime_debt.sh`; the audit found no active userland/tool build
references outside the candidate files and the audit script itself.

Tracked inactive files removed on 2026-05-08:

- `userland/libsystem/build/obj/panthera_layer7.c`
- `userland/libsystem/build/obj/panthera_printf.c`
- `userland/libsystem/build/obj/panthera_remaining.c`
- `userland/libsystem/build/obj/panthera_stdio_init.c`

The remaining entries are inactive-looking local/generated candidates if they
are present in a worktree. They are not active build inputs.

| File | Current Class | Required Action |
|---|---|---|
| `panthera_runtime_bridge.c` | `dead-code` inactive by Phase 6 audit | Remove if tracked or generated again without an owner. |
| `panthera_boot_bridge.c` | `dead-code` inactive by Phase 6 audit | Remove if tracked or generated again without an owner. |
| `panthera_layer7.c` | `dead-code` removed | Removed from tracked source after Phase 6 audit. |
| `panthera_mach_funcs.c` | `dead-code` inactive by Phase 6 audit | Remove if tracked or generated again without an owner. |
| `panthera_patch.c` | `dead-code` inactive by Phase 6 audit | Remove if tracked or generated again without an owner. |
| `panthera_printf.c` | `dead-code` removed | Removed from tracked source after Phase 6 audit. |
| `panthera_remaining.c` | `dead-code` removed | Removed from tracked source after Phase 6 audit. |
| `panthera_stdio_init.c` | `dead-code` removed | Removed from tracked source after Phase 6 audit. |
| `panthera_if_nametoindex.c` | `dead-code` inactive by Phase 6 audit | Remove if tracked or generated again without an owner. |
| `panthera_inet_ntop.c` | `dead-code` inactive by Phase 6 audit | Remove if tracked or generated again without an owner. |

## launchd Runtime Shims

| Area | Current Location | Behavior | Class | Next Action |
|---|---|---|---|---|
| Real-mode Panthera launchd adaptations | `userland/launchd/real/` | PID 1 boot init, job ordering, Panthera runtime adjustments | `transition` | Add a launchd divergence ledger against Apple `runtime.c` and `core.c`. |
| Shim fallback launchd | `userland/launchd/obj/launchd_all_stubs.c` | Fallback runtime selected by `PANTHERA_LAUNCHD_REAL=0` | `dead-code` for default boot, `transition` for recovery | Keep only if it is an intentional recovery path; otherwise archive. |
| Parent-side bootstrap inheritance workarounds | `userland/launchd/real/core.c`, `panthera_boot.c` | Runtime workaround for current fork/TLS/MIG limitations | `transition` | Replace as Mach/TLS/pthread foundation improves. |

## Completed Duplicate-Export Fix Order

Completed on 2026-05-08:

1. Removed notify exports from `libpanthera_extra`.
2. Removed os_log exports from `libpanthera_extra`.
3. Removed `_thread_resume` and `_thread_suspend` exports from
   `libpanthera_extra`.
4. Resolved `_bootstrap_port` ownership by hiding the copy emitted through
   `libsystem_info.dylib`.
5. Re-ran:

```bash
bash userland/libsystem/build/relink_libpanthera_extra.sh
bash userland/libsystem/build/relink_libSystem.sh
bash tools/build_shared_cache.sh
bash userland/libsystem/verify_exports.sh
```

6. Rebuilt the HFS root image and boot-tested against the Phase 0 baseline path.

Verification:

- `artifacts/boot/export-clean-20260508-152011.verify_exports.log`
- `artifacts/boot/export-clean-20260508-152011.log`

## Entry Template

Use this template for new shim entries:

```md
### Symbol or Group

- Current provider:
- Expected Darwin owner:
- Current behavior:
- Classification:
- Risk:
- Replacement plan:
- Verification:
- Status:
```
