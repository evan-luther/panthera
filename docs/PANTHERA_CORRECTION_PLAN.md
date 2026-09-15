# Panthera Correction Plan

## Purpose

This document is an agent handoff plan for correcting the course of the current
Panthera repository without restarting the project.

The goal is to turn Panthera from a successful bring-up workspace into a
governed, reproducible, reviewable Darwin OS build. The plan intentionally
prioritizes consolidation over new feature work.

## Mission

Bring Panthera to a state where:

- the current working system has one authoritative status source
- source, local patches, third-party inputs, and generated artifacts are clearly
  separated
- all Panthera shim and stub behavior is documented and managed as explicit debt
- root filesystem images are assembled from declared components
- QEMU boot verification gates risky changes
- future subsystem work follows the authentic Darwin dependency order:
  Mach IPC, bootstrap, launchd, daemons, networking, login, packaging

## Non-Goals

Do not rewrite Panthera from scratch.

Do not start by replacing large working subsystems.

Do not add new daemons, package ports, desktop features, filesystem experiments,
or broad compatibility layers during the consolidation phases.

Do not remove working fallbacks until their replacements are verified in a live
boot.

## Operating Rules

Follow these rules for every phase:

1. Preserve the current working boot path until a better path is verified.
2. Treat user or existing worktree changes as owned by someone else unless you
   made them.
3. Do not revert unrelated changes.
4. Prefer small, reviewable commits or change groups.
5. Keep generated outputs out of source review unless explicitly justified.
6. Update documentation in the same change that changes behavior or policy.
7. Every risky change needs either a boot log, a verification script result, or
   a clearly documented reason it could not be tested.

## Panthera-Specific Guardrails

These are repository-specific safety rules. They should be treated as hard
constraints unless the project owner explicitly overrides them.

- Do not hand-edit frozen dylibs.
- Do not relink `libSystem.B.dylib` by hand. Use
  `userland/libsystem/build/relink_libSystem.sh`.
- Do not add one-off `ld` commands for libSystem work.
- Run `userland/libsystem/verify_exports.sh` after library changes.
- Rebuild the shared cache after dylib changes using
  `tools/build_shared_cache.sh`.
- Do not modify Apple source under `src/` unless the change is a documented
  Panthera patch with a clear reason and verification path.
- Keep recovery/debug fallback paths available until the replacement daemon path
  is proven in QEMU; once promoted, keep `netbringup` on disk as an explicit
  override rather than the boot default.

## Experimental Networking Checkpoint

Status as of 2026-05-16:

- configd-owned IPConfiguration is now the default stable networking path.
- `netbringup` remains staged on disk as an explicit recovery/debug override
  via `PANTHERA_DEFAULT_NETWORK_OWNER=netbringup`.
- Apple `configd` and IPConfiguration are part of the default rootfs path;
  KernelEventMonitor remains experimental.
- Evidence is preserved at `artifacts/ipconfiguration_ordered_boot2.log` and
  `artifacts/networking/ipconfiguration_ordered_boot2.log`.
- That older evidence stopped in `configd` dynamic-store creation, but Phase 7B
  has since cleared the current `configd` store gate:
  `artifacts/boot/phase7b-configd-store-gate-20260509.summary.md` and
  `artifacts/boot/phase7b-configd-strict-network-20260509.summary.md` both
  pass.
- The current Apple-daemon blocker is now IPConfiguration startup/load under a
  stable staged `configd`, not CoreFoundation dictionary creation. Phase 7C
  has since cleared the load/start/prime startup gate:
  `artifacts/boot/p7c-ipcfg-primep-20260509.summary.md` passes.
- Phase 7I has since cleared the isolated IPConfiguration daemon-owned DHCP
  bound path:
  `artifacts/boot/p7i-dhcp-bound-deferred-async-20260509.summary.md` passes
  after IPv4 attach, BPF DHCP DISCOVER/REQUEST transmit, OFFER/ACK receive,
  address assignment, service storage, control-server initialization, and
  `handle_prime()` return.
- Phase 7J has since cleared daemon-owned IPConfiguration network usability:
  `artifacts/boot/p7j-ipconfiguration-network-20260509.summary.md` passes
  after DHCP address assignment, synchronous IPv4/DNS dynamic-store
  publication, DHCP default route installation, userland address/route
  visibility, gateway ping, and DNS-over-TCP.
- Phase 7K has since cleared staged IPConfiguration boot-to-login stability:
  `artifacts/boot/p7k-ipconfiguration-boot-login-20260509.summary.md` passes
  with normal `/bin/mini_sh`, `netbringup` skipped, DHCP ACK/address
  assignment, IPv4/DNS dynamic-store publication, and default route
  installation recorded in the boot log.
- Phase 7L has since made the remaining staged-path fallback debt
  reproducible:
  the initial `artifacts/runtime/p7l-ipconfiguration-fallback-audit-20260509.md`
  passed with 23 tracked fallback anchors: 13 runtime gaps and 10 transition
  items. After retiring the timer, BOOTP read-source, and Mach receive-source
  fallbacks, then restoring queued prime work, the current audit tracks
  18 anchors: 8 runtime gaps and 10 transition items.
  The first retired item is the unused temporary BOOTP transmit socket fallback,
  verified by
  `artifacts/boot/p7l-retire-transmit-socket-fallback-20260509.summary.md`.
  A follow-up no-inline delayed-start experiment failed at the timer callback
  boundary, proving `dhcp_delayed_start_inline` was covering a real
  timer/dispatch callback gap:
  `artifacts/boot/p7l-no-inline-dhcp-start-experiment-20260509.summary.md`.
  The DHCP-specific inline call was later retired and replaced by a generic
  zero-delay timer fallback in `timer.c`; the staged boot gate passed with that
  fallback, while disabling it still reproduced the timer callback failure:
  `artifacts/boot/p7l-zero-timer-inline-20260509.summary.md` and
  `artifacts/boot/p7l-zero-timer-disabled-experiment-20260509.summary.md`.
  After the Darwin-aligned dispatch timer correction, the no-inline gate passes
  through DHCP bind/publish/default-route and the generic zero-delay timer
  fallback has also been removed:
  `artifacts/boot/p7l-no-inline-after-dispatch-timer-wait-20260509.summary.md`,
  `artifacts/boot/p7l-retired-zero-delay-fallback-20260509.summary.md`, and
  `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-timer-retire-20260509.md`.
  A follow-up BOOTP receive cleanup restored `FDCallout` dispatch read sources
  and removed post-transmit receive polling:
  `artifacts/boot/p7l-bootp-read-source-20260509.summary.md`,
  `artifacts/boot/p7l-bootp-read-source-network-20260509.summary.md`, and
  `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-bootp-read-source-20260509.md`.
  A further control-plane cleanup restored IPConfiguration's
  `DISPATCH_SOURCE_TYPE_MACH_RECV` source and added a verifier-owned
  `ipconfig_if_count` MIG probe:
  `artifacts/boot/p7l-mach-receive-source-network-20260509.summary.md` and
  `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-mach-receive-source-20260509.md`.
  A follow-up async cleanup restored `prime()` to use
  `dispatch_async(IPConfigurationAgentQueue(), ...)`:
  `artifacts/boot/p7l-prime-dispatch-async-network3-20260509.summary.md` and
  `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-prime-dispatch-async-20260509.md`.
  A random DHCP XID experiment initially failed before delayed-start scheduling:
  `artifacts/boot/p7l-random-dhcp-xid-20260509.summary.md`.
  `libsystem_c` now compiles Apple's getentropy-backed `arc4random()` fallback
  instead of the unresolved corecrypto `ccrng` path, and the random-XID staged
  boot gate passes:
  `artifacts/runtime/p7l-arc4random-fallback2-20260509.summary.md` and
  `artifacts/boot/p7l-random-dhcp-xid-after-arc4-fallback-20260509.summary.md`.
  The default staged rootfs was restored and revalidated after that experiment:
  `artifacts/boot/p7l-default-rootfs-restore4-20260509.summary.md`.

The replacement gate has passed: `rootfs/create_hfs_root_image.sh` now defaults
to `PANTHERA_DEFAULT_NETWORK_OWNER=ipconfiguration`, and
`tools/boot_verify.sh --rebuild-rootfs --verify-root-shell /bin/zsh` runs the
IPConfiguration network gate by default when that owner is active. The green
promotion evidence is:

- `artifacts/boot/p7r-arp-darwin-callback.summary.md`
- `artifacts/boot/p7r-default-owner-promoted-boot2.summary.md`
- `artifacts/runtime/p7r-default-ipconfiguration-network-fallback-audit-after-arp-darwin-callback-20260512.md`
- `artifacts/boot/p7s-configd-store-flat-lock.summary.md`
- `artifacts/boot/p7s-known-good-restored-default-ipconfiguration.summary.md`

The latest Phase 7S pass corrects the post-publication configd crash that
appeared after the default-owner promotion. DHCP, address assignment, Global
IPv4/DNS publication, and default-route installation were already completing;
the remaining fault was configd's recursive dynamic-store mutation/publish path.
Flattening that path keeps route/global publication outside recursive store
critical sections and restores a clean default-IPConfiguration gate. A
follow-up Phase 8 pass then fixed the libdispatch deferred-delete lifetime
issue behind the remaining crash-handler dependency and revalidated the default
gate without that handler.

Current Phase 7S dispatch/configd cleanup checkpoint:

- Pure-C libdispatch avoids the rebuilt shared-cache ObjC object/vtable crash.
- CoreFoundation, bootstrap, login, and export verification remain stable under
  the rebuilt cache.
- Dyld now runs the real `__libSystem_init()` path after Panthera's libc
  bootstrap and seeds a Darwin-shaped pthread TSD area before libSystem
  constructors. `__pthread_init()` registers bsdthread/workqueue callbacks,
  libdispatch uses the kevent workqueue/workloop setup path, and
  `dispatch_main()` follows Darwin's `pthread_exit(NULL)` behavior.
- The timer failure was traced to Panthera's `mach_get_times()` bridge returning
  zero time. It now returns `mach_absolute_time()`, `mach_continuous_time()`,
  and calendar time via `gettimeofday()`.
- The strict dispatch timer probe now fires its callback and exits 0.
- The strict IPConfiguration boot verifier now waits for DHCP REQUEST, ACK,
  address assignment, IPv4/DNS dynamic-store publish, and default-route markers
  before quitting QEMU.
- The staged path no longer has a Panthera zero-delay timer inline fallback.
- IPConfiguration now creates and activates its Mach receive dispatch source;
  the strict daemon-owned network verifier proves the control service handles
  `ipconfig_if_count` after `bootstrap_look_up`.
- IPConfiguration `prime()` now queues `handle_prime()` with
  `dispatch_async`; the strict verifier requires
  `ipconfiguration_prime_async_dispatch`.
- Libdispatch's Darwin immediate event-loop drain after direct-source rearm has
  been restored and verified by the strict configd-owned IPConfiguration boot
  gate. Follow-up work retired BOOTP source parking, link-local election
  deferral, and `address_flag_prime_finalize`. A later route-manager cleanup
  moved the remaining service-to-global default-route write into a dedicated
  configd IPMonitor-shaped route-manager path. The ARP cleanup restored
  Darwin's normal DATA_ADD callback source while keeping only BPF read-source
  parking active. The latest configd cleanup moved configd's MIG service
  handling from a helper pthread to a `DISPATCH_SOURCE_TYPE_MACH_RECV` source.
  Follow-up work built and staged a real `MH_BUNDLE` IPConfiguration payload,
  captured the earlier CFBundle/dlopen failures as negative evidence, and
  restored normal ARP BPF read-source close/release under the strict
  daemon-owned network gate. The final cleanup retired the linked
  IPConfiguration plugin-loader transition: configd now keeps the real
  IPConfiguration `CFBundleRef`, opens the dyld-preloaded
  `/usr/lib/system/libIPConfiguration.dylib`, resolves `load`/`start`/`prime`
  through `dlsym`, and no longer exports those IPConfiguration entry points
  from the configd executable. The current fallback audit is green with 0
  tracked staged-path fallbacks.
- Phase 8 resolved the post-publication configd crash without restoring any
  IPConfiguration fallback. Isolation first proved the crash was not a DHCP
  regression: DHCP ACK, address assignment, Global IPv4/DNS publication, and
  default-route installation already completed before configd was reaped.
  Kernel kevent tracing then disproved the suspected LP64 `udata` truncation:
  configd was running as LP64 and the kernel copied full-width `udata` values.
  The real blocker was libdispatch direct workloop deferred-delete lifetime:
  owner-context `EV_DELETE|EV_ENABLE` unregisters were treated as complete even
  when deferred, allowing the `DUU_MUST_SUCCEED` crash path to fire before the
  delete acknowledgment. The correction keeps those owner-deferred deletes
  pending until the ack path completes. The final clean gate removes temporary
  kevent/kernel diagnostics, removes configd's crash-handler dependency, keeps
  the fallback audit at zero, and passes daemon-owned DHCP/network verification:
  `artifacts/boot/p8-final-clean.summary.md`.
- Evidence:
  `artifacts/boot/p7l-pthread-wq-setup-markers-20260509.summary.md` and
  `artifacts/boot/p7l-mach-get-times-dispatch-timer-clean-20260509.summary.md`,
  `artifacts/boot/p7l-no-inline-after-dispatch-timer-wait-20260509.summary.md`,
  `artifacts/boot/p7l-retired-zero-delay-fallback-20260509.summary.md`,
  `artifacts/boot/p7l-mach-receive-source-network-20260509.summary.md`,
  `artifacts/boot/p7l-prime-dispatch-async-network3-20260509.summary.md`,
  `artifacts/boot/p7l-dispatch-rearm-drain-restored-boot.summary.md`,
  `artifacts/boot/p7l-after-immediate-enable-reverted-boot.summary.md`, and
  `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-immediate-enable-retry-20260511.md`,
  plus `artifacts/boot/p7l-final-4-fallbacks-20260511.summary.md`,
  `artifacts/boot/p7l-address-running-kernel-boot-usb.summary.md`,
  `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-address-running-20260511.md`,
  `artifacts/boot/p7m-ipmonitor-route-manager-unscoped-boot.summary.md`,
  `artifacts/boot/p7m-ipmonitor-route-manager-network.summary.md`, and
  `artifacts/runtime/p7m-ipconfiguration-fallback-audit-after-ipmonitor-route-manager-20260512.md`,
  plus `artifacts/boot/p7m-arp-data-add-source-park-network.summary.md`,
  `artifacts/runtime/p7m-ipconfiguration-fallback-audit-after-arp-data-add-20260512.md`,
  `artifacts/boot/p7n-configd-dispatch-server-network.summary.md`, and
  `artifacts/runtime/p7n-ipconfiguration-fallback-audit-after-configd-dispatch-server-20260512.md`,
  plus `artifacts/boot/p7o-bundle-artifact-net.summary.md`,
  `artifacts/boot/p7o-dynamic-ipconfiguration-bundle-network.log`,
  `artifacts/boot/p7o-dlopen-ipconfiguration-bundle-network.log`, and
  `artifacts/runtime/p7o-ipconfiguration-fallback-audit-after-bundle-artifact-20260512.md`,
  plus `artifacts/boot/p7p-arp-normal-release-net.summary.md`,
  `artifacts/runtime/p7p-ipconfiguration-fallback-audit-after-arp-normal-release-20260512.md`,
  `artifacts/boot/p7q-ipconfiguration-dyld-dependency-classic-fixups.summary.md`,
  and `artifacts/runtime/p7q-ipconfiguration-fallback-audit-after-dynamic-loader-20260512.md`,
  plus `artifacts/boot/p7s-configd-store-flat-lock.summary.md` and
  `artifacts/boot/p7s-known-good-restored-default-ipconfiguration.summary.md`.
  Cleanup regression logs are
  `artifacts/boot/p7s-cleanup-arp-owner-default-ipconfiguration.log` and
  `artifacts/boot/p7s-cleanup-boundary-markers-default-ipconfiguration.log`.
  The Phase 8 signal-handler isolation artifacts are
  `artifacts/boot/p8-configd-no-crash-handler-default-ipconfiguration.summary.md`
  and `artifacts/boot/p8-restore-cfgsig.summary.md`. Follow-up Phase 8
  hardening artifacts are `artifacts/boot/p8-sigpipe.summary.md`,
  `artifacts/boot/p8-dispatch-cancel-hold.summary.md`,
  `artifacts/boot/p8-arp-defer-release.summary.md`,
  `artifacts/boot/p8-sigsegv-default.summary.md`,
  `artifacts/boot/p8-kevent-trace-default.log`,
  `artifacts/boot/p8-dispatch-deferred-owner-default.summary.md`, and the
  final clean gate `artifacts/boot/p8-final-clean.summary.md`.

## Current High-Risk Areas

The following areas need consolidation first:

- drift between `docs/STATUS.md`, `docs/roadmap/OS_BUILD_ROADMAP.md`,
  `docs/SHIM_STATUS.md`, and launchd runtime docs
- generated artifacts mixed into or near source paths
- `rootfs/create_hfs_root_image.sh` acting as image builder, package builder,
  daemon selector, staging script, and fixup script
- Panthera shim and compatibility code spread across `userland/libsystem`
- active and historical boot evidence not represented by a repeatable harness
- mixed third-party dependency intake models

## Phase 0: Freeze And Baseline

### Objective

Capture the current known-good system before cleanup begins.

### Scope

No new feature work. No daemon expansion. No package porting. No broad refactor.

### Tasks

1. Record current git state.
   - Run `git status --short`.
   - Classify changed paths as source, generated output, docs, binary artifact,
     or unknown.

2. Identify the current known-good build and boot commands.
   - Kernel build command.
   - Kext build command.
   - libSystem/shared-cache command sequence.
   - Root image assembly command.
   - QEMU boot command.

3. Capture one baseline boot log.
   - Prefer serial/nographic QEMU output.
   - Save under `artifacts/boot/baseline-YYYYMMDD-HHMMSS.log`.
   - If boot cannot be run, document the exact blocker.

4. Write a short baseline summary.
   - Suggested path: `artifacts/boot/baseline-YYYYMMDD-HHMMSS.summary.md`.
   - Include boot result, prompt reached or not, launchd status, network status,
     and known failures.

### Deliverables

- baseline boot log or explicit blocker note
- baseline summary
- worktree classification note

### Acceptance Criteria

- the project has a concrete before-cleanup reference point
- future agents can tell which state they are trying not to regress

## Phase 1: Canonical Current State

### Objective

Replace status drift with one authoritative current-state document.

### Primary Output

Create:

- `docs/CURRENT_STATE.md`

### Required Sections

`docs/CURRENT_STATE.md` must include:

- Last verified date
- Verification source, such as boot log path or command output
- Current boot path
- Kernel status
- Kext status
- Root filesystem status
- dyld and shared cache status
- libSystem status
- launchd/bootstrap status
- Networking status
- Shell/userland status
- Generated artifacts policy summary
- Frozen components
- Experimental components
- Known expected failures
- Immediate next engineering boundary

### Documentation Cleanup

Update the top of older status documents with a short banner:

- current
- historical
- roadmap
- superseded by `docs/CURRENT_STATE.md`

Do not delete older docs during this phase.

### Acceptance Criteria

- a new contributor can answer "what works right now?" from
  `docs/CURRENT_STATE.md`
- older docs no longer compete as authoritative status sources

## Phase 2: Provenance And Source Boundaries

### Objective

Make the repository reviewable by clearly separating upstream source,
Panthera-authored code, local patches, third-party dependencies, and generated
artifacts.

### Primary Outputs

Create or complete:

- `docs/provenance/APPLE_PATCHES.md`
- `docs/provenance/THIRD_PARTY.md`
- `docs/provenance/GENERATED_ARTIFACTS.md`
- `docs/provenance/REPRODUCIBILITY.md`

### Tasks

1. Classify major directories.
   - `src/`
   - `boot/`
   - `build/`
   - `kexts/`
   - `userland/`
   - `rootfs/`
   - `tools/`
   - `vendor/`
   - `images/`
   - `artifacts/`

2. Create an Apple patch ledger.
   - For each patched Apple source tree, record:
     - upstream project
     - upstream version
     - modified files
     - reason for patch
     - risk class
     - expected permanence
     - verification path

3. Create a third-party dependency ledger.
   - For each third-party input, record:
     - source URL or origin
     - version
     - license
     - intake model: vendored or fetched
     - checksum status
     - build script
     - whether it is required for default boot

4. Create a generated artifacts ledger.
   - For each generated path, record:
     - producer script
     - whether it should be tracked
     - whether it is needed for review
     - whether it belongs in release artifacts

5. Create a reproducibility document.
   - Document the intended clean-checkout flow from source to bootable guest.
   - Clearly mark any known hidden local-state dependency.

### Acceptance Criteria

- every major path has a provenance bucket
- generated outputs are identified and have a policy
- patched Apple source is no longer implicit
- third-party intake is explicit even if not yet perfect

## Phase 3: Shim Debt Register

### Objective

Convert Panthera shim and stub behavior into managed engineering debt.

### Primary Output

Create:

- `docs/SHIM_DEBT_REGISTER.md`

### Scope

Audit these areas first:

- `userland/libsystem/build/obj/panthera_*.c`
- `userland/libsystem/build/shims/`
- `userland/libsystem/build/obj/libxpc*.c`
- `userland/launchd/real/`
- `userland/launchd/obj/launchd_all_stubs.c`
- any `libpanthera_*` dylib build logic or leftovers

### Classification Model

Every shim or symbol group must be classified as one of:

- `real`: implements real behavior sufficiently for Panthera
- `safe-stub`: no-op or failure behavior is correct for optional functionality
- `visible-failure`: unsupported behavior fails clearly
- `dangerous-stub`: behavior can silently corrupt state or mislead callers
- `dead-code`: file or symbol is not active in current builds
- `transition`: temporary compatibility path with a named replacement

### Required Fields

For each entry, record:

- symbol or function group
- current provider
- expected Darwin owner
- current behavior
- classification
- risk
- replacement plan
- verification test
- owner or next action

### Priority Order

1. dangerous stubs
2. duplicate or shadowing exports
3. dead code in active-looking paths
4. safe stubs that block real daemons
5. optional long-term compatibility

### Acceptance Criteria

- no active `panthera_*.c` behavior is unknown
- no duplicate export is undocumented
- no dangerous stub remains without a blocking issue or immediate fix plan
- dead files are either removed from active paths or documented as inactive

## Phase 4: Build Graph Cleanup

### Objective

Separate component builds from root image assembly.

### Current Progress

Status as of 2026-05-08 17:11 -0400: Phase 4 rootfs assembly correction is
implemented for the current default boot path.

Implemented:

- initial rootfs input manifests under `manifests/*.system`
- expanded default rootfs manifests for base runtime, logging, package
  libraries, diagnostics, SSH, TLS/curl, and experimental configd plists
- `rootfs/scripts/build_components.sh` for the former rootfs build prelude
- `rootfs/scripts/manifest_lib.sh` for shared manifest condition parsing
- `rootfs/scripts/verify_rootfs_inputs.sh` for manifest-declared preflight
- `rootfs/create_hfs_root_image.sh --no-build-components` as the default
  assembly-only path
- `rootfs/create_hfs_root_image.sh --build-components` as the explicit
  compatibility path for rebuilding inputs before assembly
- `rootfs/scripts/populate_skeleton.sh` for directory skeleton and `fstab`
- `rootfs/scripts/stage_users.sh` for passwd/group/master.passwd generation
- `rootfs/scripts/stage_etc.sh` for manifest-gated static `/etc` and
  `SystemVersion.plist`
- `rootfs/scripts/stage_launchdaemons.sh` for manifest-selected launch daemon
  staging, with compatibility env overrides retained
- `rootfs/scripts/stage_shared_cache.sh` for manifest-gated dyld shared-cache
  staging
- `rootfs/scripts/fix_ownership.sh` for post-build shared-cache ownership fixup
- `rootfs/scripts/stage_user_templates.sh` for manifest-gated guest user
  template materialization
- `rootfs/stage_terminfo.sh` as manifest-gated terminfo staging
- `rootfs/scripts/stage_base_payload.sh` for base binaries, diagnostics, and
  core runtime libraries, now gated by selected manifest sources
- `rootfs/scripts/stage_package_payload.sh` for package libraries, package
  tools, and staged daemons, now gated by selected manifest sources
- `rootfs/scripts/verify_rootfs.sh` for mounted-root required-path checks and
  active manifest destination checks
- generated-output manifest entries for compatibility symlinks, framework
  symlinks, compiled terminfo records, and package alias paths
- staging of declared `tests/test_xpc_service` to match the debug manifest

Verification:

- `bash rootfs/scripts/verify_rootfs_inputs.sh manifests/base.system manifests/networking.system manifests/ssh.system manifests/debug.system`
- `bash rootfs/create_hfs_root_image.sh --force --no-build-components`
- QEMU boot to `login:`:
  `artifacts/boot/phase4-rootfs-generated-20260508-171031.log`
- Link verification:
  `artifacts/boot/phase4-rootfs-generated-20260508-171031.verify_exports.log`

Residual Phase 4 follow-up:

- continue replacing broad directory/glob destinations with file-level
  declarations where that improves review value
- keep the top-level wrapper stable while later phases add a higher-level
  verification harness

### Problem Statement

`rootfs/create_hfs_root_image.sh` currently performs too many roles:

- builds or triggers builds for packages
- creates the image
- creates filesystem layout
- stages binaries and libraries
- stages launchd plists
- applies ownership and cache fixups
- controls optional daemons through environment variables
- stages tests and diagnostics

This makes the build hard to reason about and easy to regress.

### Target Model

Component build scripts produce installable artifacts.

Rootfs assembly copies declared artifacts into a filesystem image.

Optional components are selected through manifests, not hidden environment
behavior.

### Suggested New Structure

Add:

- `manifests/base.system`
- `manifests/debug.system`
- `manifests/networking.system`
- `manifests/ssh.system`
- `manifests/experimental-configd.system`

Add rootfs helper scripts as needed:

- `rootfs/scripts/create_image.sh`
- `rootfs/scripts/populate_skeleton.sh`
- `rootfs/scripts/stage_users.sh`
- `rootfs/scripts/stage_libs.sh`
- `rootfs/scripts/stage_bins.sh`
- `rootfs/scripts/stage_launchdaemons.sh`
- `rootfs/scripts/stage_shared_cache.sh`
- `rootfs/scripts/fix_ownership.sh`
- `rootfs/scripts/verify_rootfs.sh`

The exact names may change, but responsibilities must be separated.

### Manifest Requirements

Each manifest entry should include enough information to answer:

- what artifact is staged
- where it comes from
- where it lands in the guest
- whether it is required or optional
- what build script produces it
- what verification proves it works

### Migration Strategy

Do not rewrite the whole rootfs script at once.

Recommended order:

1. Extract image creation.
2. Extract filesystem skeleton creation.
3. Extract user/group staging.
4. Extract dylib/shared-cache staging.
5. Extract launch daemon staging.
6. Extract package binary staging.
7. Convert optional env-controlled staging to manifests.
8. Leave a compatibility wrapper at `rootfs/create_hfs_root_image.sh`.

### Acceptance Criteria

- root image assembly does not unexpectedly rebuild unrelated packages
- missing required artifacts fail early with clear errors
- component inclusion is visible before image creation
- the old top-level command can remain as a wrapper during transition

## Phase 5: Boot Verification Harness

Status as of 2026-05-08 19:51 -0400: Phase 5 default boot verification is in
place and passing.

Current command:

```bash
tools/boot_verify.sh --timeout 220 --rebuild-rootfs --tag phase5-final-gate-20260508
```

Current evidence:

- `artifacts/boot/phase5-final-gate-20260508.log`
- `artifacts/boot/phase5-final-gate-20260508.summary.md`
- `artifacts/boot/phase5-final-gate-20260508.verify_exports.log`

The default gate passes EFI handoff, kernel start, root mount, launchd PID 1,
plist import, login prompt, root `mini_sh` prompt, external command execution,
root identity, bootstrap Mach smoke via `test_bootstrap_simple`, and
`verify_exports.sh`. Network checks are recorded as expected failures because
the current network ioctl path can block in-guest; `--strict-network` is
reserved for the follow-up that makes those probes bounded.

### Objective

Make QEMU boot evidence a normal part of development.

### Primary Outputs

Add a boot verification harness under one of:

- `tools/boot_verify.sh`
- `tests/boot/`
- `tools/verify_boot_log.py`

Pick the simplest implementation that works reliably.

### Minimum Boot Checks

The harness should detect:

- kernel handoff occurred
- launchd started as PID 1
- root filesystem mounted or remounted read-write
- shell prompt reached
- external command execution works
- `/bin/id` works
- network interface configured
- gateway ping works
- DNS/TCP status is pass or expected-fail
- bootstrap Mach round-trip test passes if the test binary is staged

### Artifacts

Write outputs under:

- `artifacts/boot/latest.log`
- `artifacts/boot/latest.summary`

Optional:

- `artifacts/boot/latest.junit.xml`

### Implementation Notes

- Prefer serial/nographic QEMU for repeatability.
- Avoid relying on visual inspection.
- Timeouts should be explicit.
- Expected failures must be named in the summary.
- Keep raw logs even when parsing fails.

### Acceptance Criteria

- one command can boot and summarize the result
- regressions are detectable from script output
- risky changes cite a boot verification artifact

## Phase 6: libSystem And Runtime Hardening

Status as of 2026-05-08 20:08 -0400: Phase 6 runtime-foundation gate is in
place and passing for the relink/cache/export workflow, launchd linkage audit,
runtime debt audit, dead shim cleanup, and boot evidence through the Phase 5
boot verifier. Launchd now defaults to two-level namespace linking; flat
namespace is an explicit legacy debugging mode only.

Current commands:

```bash
tools/runtime_foundation_gate.sh --tag phase6-dead-shim-cleanup-20260508
tools/runtime_foundation_gate.sh --verify-only --boot --tag phase6-dead-shim-cleanup-boot-20260508
```

Current evidence:

- `artifacts/runtime/phase6-dead-shim-cleanup-20260508.summary.md`
- `artifacts/runtime/phase6-dead-shim-cleanup-boot-20260508.summary.md`
- `artifacts/runtime/phase6-dead-shim-cleanup-boot-20260508.runtime_debt.md`
- `artifacts/boot/phase6-dead-shim-cleanup-boot-20260508-boot.summary.md`

The gate makes the shared-cache rebuild, export check, and launchd linkage
audit part of the canonical runtime-foundation workflow. It also records a
runtime debt audit and proved removal of tracked inactive shim files:
`panthera_layer7.c`, `panthera_printf.c`, `panthera_remaining.c`, and
`panthera_stdio_init.c`. The core Phase 6 acceptance bar is met. Residual
runtime work remains as longer-tail cleanup: reduce shim debt, move behavior to
real owners where feasible, name PID 1 special cases, and keep removing flat
namespace or dynamic lookup usage where it can be done safely.

### Objective

Stabilize the foundation before adding more daemons.

### Priorities

1. Ensure `verify_exports.sh` remains green.
2. Make shared-cache rebuild part of every dylib workflow.
3. Remove dead shim files from active-looking build paths.
4. Move behavior from Panthera shims to real Apple-source owners where possible.
5. Document any remaining intentional shim behavior.
6. Reduce PID 1 special cases when the kernel/runtime supports real behavior.
7. Eliminate flat namespace linking where feasible.

### Focus Areas

- `userland/libsystem/build/relink_libpanthera_extra.sh`
- `userland/libsystem/build/relink_libSystem.sh`
- `userland/libsystem/verify_exports.sh`
- `tools/build_shared_cache.sh`
- `userland/launchd/build_launchd.sh`
- `userland/launchd/real/`
- `userland/dyld/panthera_dyld.cpp`

### Acceptance Criteria

- dylib workflows are documented and reproducible
- duplicate exports are intentional or eliminated
- shim count trends downward
- launchd/runtime special cases are named and justified

## Phase 7: Authentic Darwin Service Path

Status as of 2026-05-09: Phase 7A corrected the static IPv4 networking path,
Phase 7B cleared the current `configd` dynamic-store gate, Phase 7C proved the
staged IPConfiguration load/start/prime startup gate, Phase 7D proved the
initial IPConfiguration prime path through interface initialization, Mach service
check-in, server initialization return, state-handler return, and
`handle_prime()` return, and Phase 7E proved DHCP startup through Setup service
discovery, `en0` DHCP service creation, DHCP thread entry, timer/BOOTP/ARP
client initialization, DHCP transaction ID assignment, delayed-start scheduling,
service storage, and prime return.
Phase 7F cleared the first outbound packet edge: the diagnostic reaches
delayed-start, link check, DHCP INIT, DISCOVER construction, BOOTP receive
setup, base `_sendto`, kernel `sendto_nocancel`, `sosend`, `udp_output`, and
`ip_output`, and the socket send returns 300 bytes. Phase 7G proved DHCP
OFFER validation, REQUEST transmit, ACK acceptance, and entry into
`dhcp_bound()` under the static-preflight diagnostic; Phase 7L later moved
BOOTP receive from direct polling to dispatch read sources. The isolated daemon
path now has configd Setup-only DHCP service seeding. The follow-up daemon-owned
network gate proves address/route visibility, dynamic-store IPv4, gateway ping,
and DNS-over-TCP on the read-source path. Phase 7H proved the
kernel returns the AF_INET datagram socket used by `inet_attach_interface()`,
deferred the Panthera socket-close callback that was entering the current
minimal dispatch async path, and moved opt-in IPv4 attach through
`SIOCPROTOATTACH` and interface-up flag setting. Phase 7I/7J proved the staged
daemon-owned path through default IPv4 attach, BPF DHCP DISCOVER/REQUEST,
OFFER/ACK receive, address assignment, synchronous IPv4/DNS dynamic-store
publication, DHCP default route installation, userland address/route
visibility, gateway ping, DNS-over-TCP, control-server initialization, and
`handle_prime()` return. Phase 7K proved the staged path can also boot to login
under the normal `/bin/mini_sh` root shell with `netbringup` skipped while the
DHCP address/publish/route evidence lands in the serial log. Phase 7L adds a
reproducible fallback audit for the staged path; after retiring the unused
temporary BOOTP transmit socket fallback, the deterministic DHCP XID fallback,
the generic zero-delay timer fallback, the BOOTP receive-polling fallback, and
the Mach receive-source skip, then restoring `dispatch_async` prime work, it
currently tracks 18 temporary anchors, split into 8 runtime gaps and 10
transition items. The no-inline DHCP delayed-start experiment confirmed the
timer gap, and the corrected `mach_get_times()` dispatch timer path now lets
delayed start proceed through DHCP bind, publication, and default-route
installation without inline execution. BOOTP OFFER/ACK receive is now delivered
through dispatch read sources, and the IPConfiguration control service now
handles a live `ipconfig_if_count` MIG request through its Mach receive source.
The prime path now submits `handle_prime()` through
`dispatch_async(IPConfigurationAgentQueue(), ...)` and still reaches DHCP
ACK/address/publish/default-route plus the control service under the strict
daemon-owned network gate. Later Phase 7L cleanup retired the BOOTP delayed
close source-parking fallback and restored link-local election. It also retired
the IPConfiguration address flag prime/finalize fallback by moving
`IFF_RUNNING` publication into the normal IONetworkingFamily/XNU address path;
the staged boot gate now passes DHCP bind, normal address ioctl return,
IPv4/DNS publication, and default-route installation without the old
IPConfiguration flag-prime/reapply block. At that point, the generated audit
tracked 3 staged-path fallbacks: the ARP direct callback/source-park runtime
gap, the then-minimal configd IPConfiguration plugin loader transition item,
and the minimal configd route-manager transition item.
The Phase 7M route-manager cleanup then moved default-route installation into a
dedicated configd IPMonitor-shaped route-manager module and retired the
`minimal_configd_route_manager` audit item. The follow-up ARP cleanup restored
the normal DATA_ADD callback source and narrowed ARP debt to BPF read-source
parking. Phase 7N moved configd's service-port receive path from a helper
pthread to a dispatch Mach receive source, matching the Darwin event-source
shape used by SystemConfiguration clients. Phase 7O prepared the
IPConfiguration bundle payload as a real Mach-O bundle and proved that staging
that artifact is compatible with the strict daemon-owned network gate. The
attempt to switch configd to dynamic loading is now blocked by runtime
`dlopen` support, not by the IPConfiguration build artifact. Phase 7P restored
normal ARP BPF read-source close/release and passed the strict daemon-owned
network gate. Phase 7Q retired the linked configd IPConfiguration
plugin-loader transition by loading a dyld-preloaded
`/usr/lib/system/libIPConfiguration.dylib` and resolving the plugin entry
points through `dlsym`; the current generated fallback audit tracks 0
staged-path fallbacks.
`tools/boot_verify.sh --strict-network` now passes static interface setup,
route publication, ARP, gateway ICMP, and DNS-over-TCP on the regular rootfs
path. The bounded `--probe-network --strict-network` path remains available for
isolating network bring-up from the default `netbringup` daemon. The remaining
`net_static_arp` expected-fail is a diagnostic PF_ROUTE link-layer route
insertion probe, not a runtime networking blocker. `tools/boot_verify.sh
--strict-configd` now proves generic CoreFoundation dictionary creation,
`configd` main entry, dynamic-store creation, bootstrap check-in, and
SystemConfiguration dynamic-store probe traffic. The active correction target
is no longer DHCP broadcast reachability; it is reducing or justifying the
temporary Panthera fallbacks before any controlled default transition away from
`netbringup`.

Current commands:

```bash
tools/boot_verify.sh --timeout 220 --rebuild-rootfs --probe-network --tag phase7a-bounded-network-probe-clean-20260508
tools/boot_verify.sh --timeout 220 --rebuild-rootfs --tag phase7a-default-boot-regression-20260508
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --probe-network --tag phase7a-static-combo-probe-20260508
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --probe-network --tag phase7a-route-table-snapshot-20260508
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --probe-network --tag phase7a-protoattach-before-ping-20260509
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --probe-network --tag phase7a-arp-dlil-trace-20260509
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --probe-network --strict-network --tag phase7a-strict-network-clean-20260509
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --probe-network --strict-network --tag phase7a-arptrace-gated-strict-network-20260509
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --strict-network --tag phase7a-default-strict-network-20260509
PANTHERA_BOOT_ARGS_APPEND='panthera_rtlintr=1 panthera_rtlpkt=1' tools/boot_verify.sh --timeout 180 --rebuild-rootfs --strict-network --tag phase7a-rtlintr-restaged-20260509
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --strict-network --tag phase7a-default-after-rtlintr-gate-20260509
tools/boot_verify.sh --timeout 180 --rebuild-rootfs --strict-configd --tag phase7b-configd-store-gate-20260509
tools/boot_verify.sh --timeout 240 --rebuild-rootfs --strict-configd --strict-network --tag phase7b-configd-strict-network-20260509
tools/boot_verify.sh --timeout 260 --rebuild-rootfs --strict-ipconfiguration --tag p7c-ipcfg-primep-20260509
tools/boot_verify.sh --timeout 280 --strict-ipconfiguration-prime --tag p7d-final-20260509
tools/boot_verify.sh --timeout 340 --rebuild-rootfs --strict-ipconfiguration-dhcp --tag p7e-dhcp-gate-20260509
tools/boot_verify.sh --timeout 360 --rebuild-rootfs --strict-ipconfiguration-dhcp-packet --tag p7f-dhcp-packet-send3-20260509
tools/boot_verify.sh --timeout 360 --rebuild-rootfs --strict-ipconfiguration-dhcp-packet --tag p7g-dhcp-receive-poll-20260509
tools/boot_verify.sh --timeout 300 --strict-ipconfiguration-dhcp --tag p7h-attach-close-defer-20260509
tools/boot_verify.sh --timeout 420 --strict-ipconfiguration-network --verify-root-shell /bin/zsh --tag p7j-ipconfiguration-network-20260509 --rebuild-rootfs
tools/boot_verify.sh --timeout 360 --strict-ipconfiguration-boot --tag p7k-ipconfiguration-boot-login-20260509 --rebuild-rootfs
python3 tools/audit_ipconfiguration_fallbacks.py --output artifacts/runtime/p7l-ipconfiguration-fallback-audit-20260509.md
# Historical expected-fail diagnostic from before the dispatch timer correction.
tools/boot_verify.sh --timeout 360 --strict-ipconfiguration-boot --ipconfiguration-no-inline-dhcp-start --tag p7l-no-inline-dhcp-start-experiment-20260509 --rebuild-rootfs
tools/boot_verify.sh --timeout 360 --strict-ipconfiguration-boot --tag p7l-zero-timer-inline-20260509 --rebuild-rootfs
# Historical expected-fail diagnostic from before the generic zero-delay fallback was retired.
tools/boot_verify.sh --timeout 360 --strict-ipconfiguration-boot --ipconfiguration-no-inline-dhcp-start --tag p7l-zero-timer-disabled-experiment-20260509 --rebuild-rootfs
# Historical expected-fail diagnostic: exposed the unresolved corecrypto ccrng path.
tools/boot_verify.sh --timeout 360 --strict-ipconfiguration-boot --tag p7l-random-dhcp-xid-20260509 --rebuild-rootfs
tools/runtime_foundation_gate.sh --tag p7l-arc4random-fallback2-20260509
tools/boot_verify.sh --timeout 360 --strict-ipconfiguration-boot --tag p7l-random-dhcp-xid-after-arc4-fallback-20260509 --rebuild-rootfs
# The no-inline flag is now accepted as a compatibility no-op; this artifact was
# captured before the flag was retired from the verifier surface.
tools/boot_verify.sh --timeout 420 --strict-ipconfiguration-boot --ipconfiguration-no-inline-dhcp-start --tag p7l-no-inline-after-dispatch-timer-wait-20260509 --rebuild-rootfs
tools/boot_verify.sh --timeout 420 --strict-ipconfiguration-boot --tag p7l-retired-zero-delay-fallback-20260509 --rebuild-rootfs
python3 tools/audit_ipconfiguration_fallbacks.py --output artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-timer-retire-20260509.md
tools/boot_verify.sh --timeout 420 --strict-ipconfiguration-boot --tag p7l-bootp-read-source-20260509 --rebuild-rootfs
tools/boot_verify.sh --timeout 420 --strict-ipconfiguration-network --verify-root-shell /bin/zsh --tag p7l-bootp-read-source-network-20260509 --rebuild-rootfs
python3 tools/audit_ipconfiguration_fallbacks.py --output artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-bootp-read-source-20260509.md
tools/boot_verify.sh --timeout 420 --strict-ipconfiguration-network --verify-root-shell /bin/zsh --tag p7l-mach-receive-source-network-20260509 --rebuild-rootfs
python3 tools/audit_ipconfiguration_fallbacks.py --output artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-mach-receive-source-20260509.md
tools/boot_verify.sh --timeout 520 --strict-ipconfiguration-network --verify-root-shell /bin/zsh --tag p7l-prime-dispatch-async-network3-20260509 --rebuild-rootfs
python3 tools/audit_ipconfiguration_fallbacks.py --output artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-prime-dispatch-async-20260509.md
```

Current evidence:

- `artifacts/boot/phase7a-bounded-network-probe-clean-20260508.summary.md`
- `artifacts/boot/phase7a-bounded-network-probe-clean-20260508.log`
- `artifacts/boot/phase7a-default-boot-regression-20260508.summary.md`
- `artifacts/boot/phase7a-noarp-flags-bypass-20260508.log`
- `artifacts/boot/phase7a-noarp-netmask-bypass-20260508.log`
- `artifacts/boot/phase7a-static-combo-probe-20260508.log`
- `artifacts/boot/phase7a-skip-domifattach-readback-20260508.summary.md`
- `artifacts/boot/phase7a-route-probe-20260508.summary.md`
- `artifacts/boot/phase7a-route-table-snapshot-20260508.summary.md`
- `artifacts/boot/phase7a-eflags-snapshot-20260509.summary.md`
- `artifacts/boot/phase7a-clear-noarp-before-ping-20260509.summary.md`
- `artifacts/boot/phase7a-protoattach-before-ping-20260509.summary.md`
- `artifacts/boot/phase7a-finalize-addr-after-proto-20260509.summary.md`
- `artifacts/boot/phase7a-arp-dlil-trace-20260509.summary.md`
- `artifacts/boot/phase7a-static-arp-after-finalize-20260509.summary.md`
- `artifacts/boot/phase7a-txq-drain-trace-20260509.summary.md`
- `artifacts/boot/phase7a-attach-enable-noirq-20260509.summary.md`
- `artifacts/boot/phase7a-polled-rx-20260509.summary.md`
- `artifacts/boot/phase7a-tcp-output-chain-20260509.summary.md`
- `artifacts/boot/phase7a-tx-reclaim-poll-20260509.summary.md`
- `artifacts/boot/phase7a-strict-network-clean-20260509.summary.md`
- `artifacts/boot/phase7a-trace-clean-strict-network-20260509.summary.md`
- `artifacts/boot/phase7a-arptrace-gated-strict-network-20260509.summary.md`
- `artifacts/boot/phase7a-default-strict-network-20260509.summary.md`
- `artifacts/boot/phase7a-rtlintr-restaged-20260509.summary.md`
- `artifacts/boot/phase7a-default-after-rtlintr-gate-20260509.summary.md`
- `artifacts/boot/phase7b-configd-store-gate-20260509.summary.md`
- `artifacts/boot/phase7b-configd-strict-network-20260509.summary.md`
- `artifacts/boot/p7c-ipcfg-primep-20260509.summary.md`
- `artifacts/boot/p7d-final-20260509.summary.md`
- `artifacts/boot/p7e-dhcp-gate-20260509.summary.md`
- `artifacts/boot/p7f-dhcp-packet-send3-20260509.summary.md`
- `artifacts/boot/p7g-dhcp-receive-poll-20260509.summary.md`
- `artifacts/boot/p7g-dhcp-offer-trace-20260509.summary.md`
- `artifacts/boot/p7g-dhcp-request-ack-trace-20260509.summary.md`
- `artifacts/boot/p7g-dhcp-bound-address-trace-20260509.summary.md`
- `artifacts/boot/p7g-dhcp-setup-only-seed-20260509.summary.md`
- `artifacts/boot/p7g-dhcp-ipv4-attach-20260509.summary.md`
- `artifacts/boot/p7h-ipv4-attach-trace-20260509.summary.md`
- `artifacts/boot/p7h-socket-open-kernel-trace-20260509.summary.md`
- `artifacts/boot/p7h-attach-close-defer-20260509.summary.md`
- `artifacts/boot/p7h-default-rootfs-restore2-20260509.summary.md`
- `artifacts/boot/p7i-dhcp-bound-deferred-async-20260509.summary.md`
- `artifacts/boot/p7i-default-rootfs-restore-20260509.summary.md`
- `artifacts/boot/p7j-ipconfiguration-network-20260509.summary.md`
- `artifacts/boot/p7j-default-rootfs-restore-20260509.summary.md`
- `artifacts/boot/p7k-ipconfiguration-boot-login-20260509.summary.md`
- `artifacts/boot/p7k-default-rootfs-restore-20260509.summary.md`
- `artifacts/runtime/p7l-ipconfiguration-fallback-audit-20260509.md`
- `artifacts/boot/p7l-retire-transmit-socket-fallback-20260509.summary.md`
- `artifacts/boot/p7l-no-inline-dhcp-start-experiment-20260509.summary.md`
- `artifacts/boot/p7l-zero-timer-inline-20260509.summary.md`
- `artifacts/boot/p7l-zero-timer-disabled-experiment-20260509.summary.md`
- `artifacts/boot/p7l-random-dhcp-xid-20260509.summary.md`
- `artifacts/runtime/p7l-arc4random-fallback2-20260509.summary.md`
- `artifacts/boot/p7l-random-dhcp-xid-after-arc4-fallback-20260509.summary.md`
- `artifacts/boot/p7l-default-rootfs-restore-20260509.summary.md`
- `artifacts/boot/p7l-default-rootfs-restore2-20260509.summary.md`
- `artifacts/boot/p7l-default-rootfs-restore3-20260509.summary.md`
- `artifacts/boot/p7l-default-rootfs-restore4-20260509.summary.md`
- `artifacts/boot/p7l-default-rootfs-restore5-20260509.summary.md`
- `artifacts/boot/p7l-pthread-wq-setup-markers-20260509.summary.md`
- `artifacts/boot/p7l-wq-enotsup-20260509.summary.md`
- `artifacts/boot/p7l-mach-get-times-dispatch-timer-clean-20260509.summary.md`
- `artifacts/boot/p7l-no-inline-after-dispatch-timer-wait-20260509.summary.md`
- `artifacts/boot/p7l-retired-zero-delay-fallback-20260509.summary.md`
- `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-timer-retire-20260509.md`
- `artifacts/boot/p7l-bootp-read-source-20260509.summary.md`
- `artifacts/boot/p7l-bootp-read-source-network-20260509.summary.md`
- `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-bootp-read-source-20260509.md`

Resolved blockers: the explicit probe no longer stops at generic interface
settling. The static path now has constrained ordering for interface flags,
address publication/readback, default route publication, NOARP clearing,
explicit IPv4 protocol attachment, and normal address finalization after
ARP/protocol attach. `en0` is confirmed to use the legacy output model
(`PANTHERA_NETPROBE_EFLAGS:0x1000000`, no `IFEF_TXSTART`). ARP generation
reaches DLIL and IONetworking; attach-time controller enablement starts the
RTL transmit queue; deferred hardware interrupts avoid the early boot stall;
timer RX polling consumes ARP, ICMP, and TCP replies; and timer TX reclaim
services descriptors while interrupts are deferred. The clean strict probe and
the regular strict network gate both resolve `example.com` over TCP DNS via
`10.0.2.3`. Packet/queue diagnostics are now gated behind `panthera_rtlpkt=1`;
XNU ARP/DLIL diagnostics are gated behind `panthera_arptrace=1`. This
historical Phase 7A note was superseded on 2026-05-23: the APIC/PIIX/RTL
interrupt path now boots ZFS root and passes default OpenSSH/SFTP upload smokes
without packet tracing; `panthera_rtlpoll=1` is diagnostic only.

Phase 7B resolved the current `configd` dynamic-store blocker: the store is
created, `bootstrap_check_in` succeeds, and the SystemConfiguration probe can
set/get a dynamic-store key. The combined configd plus strict-network gate
passes with `net_static_arp` as the only expected failure.

Phase 7C resolved the current IPConfiguration startup blocker: launchd execs
the staged `/usr/libexec/ipconfiguration`, the launcher connects to the configd
dynamic store, Apple `load()` enters and returns, Apple `start()` returns, and
prime work is queued. This gate required Panthera-only runtime adaptations:
skip `CFBundleGetMainBundle()` in the standalone launcher, tolerate a null
bundle in `load()`, use the existing async-source fallback, skip live
SCPreferences observer registration, skip CGA initialization, skip loopback
setup, and avoid normal dispatch queue submission for early prime work at that
time because dispatch queue submission blocked in the minimal runtime.

Phase 7D resolved the next prime-path blockers. IPConfiguration now declares and
checks in the Apple Mach service name `com.apple.network.IPConfiguration`,
enumerates interfaces through the initial list creation path, returns from
`start_initialization()`, returns from `server_init()`, runs the initial
`handle_change(NULL)` state handler, returns from `handle_prime()`, and reaches
the launcher's `prime queued` marker. This gate required temporary Panthera-only
debt: skip live dynamic-store notifications, skip power notifications, skip
SystemConfiguration interface classification during `en0` enumeration, run
`prime()` synchronously under Panthera, and check in the IPConfiguration Mach
service while skipping the receive dispatch source because `dispatch_activate()`
does not return on the current minimal dispatch path.

Phase 7E resolved the next DHCP-start blockers. `configd` now seeds the minimal
Setup network keys, Panthera configd has enough path-component pattern matching
for IPConfiguration's Setup-service `SCDynamicStoreCopyMultiple()` call,
IPConfiguration performs an explicit initial `configuration_changed()` pass from
prime, discovers the `en0` IPv4 DHCP service, adds and stores the service,
enters the DHCP method and DHCP thread, initializes timer, BOOTP, and ARP client
state, assigns a DHCP transaction ID, schedules delayed start, and returns
through the prime path. This gate required temporary Panthera-only debt around
early IPv4 attach, autoaddr ioctls, and DHCP startup tracing.

Phase 7F resolved the outbound send blocker. The work fixed CoreFoundation
UNIX2003 socket/fsync wrapper self-jumps, bypassed the remaining shared-cache
symbol ambiguity in IPConfiguration with a Panthera-only base `_sendto` call,
normalized the Darwin `-1` send return, and tightened the packet verifier so
the DHCP packet gate requires a real successful socket/kernel send. Current
evidence is
`artifacts/boot/p7f-dhcp-packet-send-success-20260509.summary.md`.

Phase 7G resolved the receive and lease-negotiation diagnostic boundary.
IPConfiguration now opens and binds the BOOTP receive socket, validates the QEMU
DHCP OFFER, sends the REQUEST, accepts the ACK, and enters `dhcp_bound()` under
the static-preflight path. Phase 7L later retired the direct receive polling
fallback by restoring BOOTP dispatch read sources. Configd also now publishes a Setup-only DHCP service
for `en0`, so isolated daemon starts no longer depend on pre-existing IPv4
State data to discover a DHCP service.

Phase 7H resolved the opt-in IPv4 attach socket-open blocker in the isolated
daemon path. Kernel socket tracing proved `socket(AF_INET, SOCK_DGRAM, 0)`
returns successfully; the hang was the Panthera socket-close callback entering
the current minimal dispatch async fallback after socket creation. Deferring
`sockets_need_close()` under Panthera lets `inet_attach_interface()` return from
`SIOCPROTOATTACH` and interface-up flag setting.

Phase 7I/7J resolved the daemon-owned DHCP and network usability boundary.
IPConfiguration now attaches IPv4 by default in the staged path, transmits DHCP
through BPF, receives OFFER/ACK, assigns the daemon-owned address, publishes
IPv4/DNS state synchronously, installs the DHCP default route, and passes
delayed userland address, route, dynamic-store, gateway ping, and DNS-over-TCP
checks under the strict daemon network verifier. Phase 7K then resolved the
staged boot-to-login stability boundary: the normal `/bin/mini_sh` boot path
reaches login with `netbringup` skipped while the daemon DHCP address,
publication, and default-route traces are present. Phase 7L does not remove
fallbacks in bulk; it turns the remaining fallback set into a generated audit
with owners, risks, replacement gates, and source anchors. Its first cleanup
removed the temporary transmit socket fallback from `bootp_session.c` after
the passing logs proved DHCP transmit uses the receive socket plus BPF path.
The next cleanup experiment disabled the inline DHCP delayed-start fallback by
injecting `PANTHERA_IPCONFIGURATION_NO_INLINE_DHCP_START=1` into the staged
IPConfiguration launchd environment. That experiment reached DHCP timer
scheduling but did not reach the delayed-start callback, DISCOVER transmit,
lease negotiation, address assignment, dynamic-store publication, or default
route installation. The DHCP-specific inline call was then replaced with a
generic Panthera zero-delay timer fallback in `timer.c`, keeping the debt at
the timer abstraction boundary. The staged boot gate passes with the generic
fallback. The disabled-fallback diagnostic is now historical: generic dispatch
timer delivery is fixed by the `mach_get_times()` correction, the staged
no-inline gate now passes, and the generic zero-delay timer fallback has been
removed from `timer.c`. The next cleanup restored `FDCallout` read sources and
removed the BOOTP post-transmit receive poll; DHCP OFFER and ACK still reach the
strict staged boot gate through dispatch callbacks. The next cleanup restored
IPConfiguration's Mach receive dispatch source and added a strict verifier
probe that proves `bootstrap_look_up("com.apple.network.IPConfiguration")` and
`ipconfig_if_count` succeed through the service. An attempted
deterministic-XID cleanup switched DHCP to
early `arc4random()`, but the boot stopped after DHCP lease-list initialization
and before XID tracing or delayed-start scheduling. That exposed
`libsystem_c`'s unresolved corecrypto `ccrng` dependency. Rebuilding
`arc4random.c` through Apple's getentropy-backed fallback fixed that owner, and
the random-XID staged boot gate now passes. The Phase 7C/7D dispatch-source and
synchronous-prime constraints are historical: Phase 7L has since restored the
Mach receive source and queued prime work under strict daemon-owned network
gates.

Next correction target: keep the promoted default path green while reducing
residual bring-up noise and provenance gaps. The default networking regression
gate is now the IPConfiguration network verifier, with
`artifacts/boot/p8-final-clean.summary.md` as the current clean baseline. The
immediate technical boundary is no longer `netbringup` replacement or
IPConfiguration fallback retirement; it is documentation/provenance cleanup,
trace-noise reduction, and preserving the verified boot/login/bootstrap/network
path while later Apple daemon replacements are attempted.

### Objective

Resume feature work only after the consolidation baseline is green.

### Required Order

Do daemon and service work in this order:

1. Mach IPC correctness
2. bootstrap service semantics
3. launchd job manager/runtime
4. notifyd
5. syslogd
6. IOKitUser and SystemConfiguration
7. configd
8. mDNSResponder and real DNS
9. SSH/login/PAM/getty
10. package manager

### Rules For New Daemons

Every daemon integration must include:

- source/provenance entry
- build script
- symbol audit using `tools/audit_package.sh`
- rootfs manifest entry
- launchd plist
- boot verification
- fallback behavior if replacing a working bootstrap script
- shim debt entries for any new stubs

### Acceptance Criteria

- daemons integrate through launchd/bootstrap rather than one-off boot hacks
- fallback scripts remain until replacement daemons are live-boot verified
- no new subsystem expands shim debt without a register entry

## Phase 8: Release Readiness

Current status as of 2026-05-16: Phase 8 runtime hardening has a clean
default-IPConfiguration gate with zero tracked IPConfiguration fallbacks:
`artifacts/boot/p8-final-clean.summary.md`. The gate verifies EFI handoff,
kernel/rootfs/launchd/login, CoreFoundation/bootstrap, configd dynamic-store
creation, configd-owned IPConfiguration load/start/prime, DHCP
DISCOVER/OFFER/REQUEST/ACK, daemon-owned address and default route visibility,
Global IPv4/DNS dynamic-store publication, gateway ping, DNS-over-TCP,
IPConfiguration control MIG `if_count`, no configd/IPConfiguration crash reaps,
and export verification. Remaining Phase 8 work is release-readiness work:
provenance completeness, generated-artifact policy enforcement, trace-noise
reduction, and clean-checkout reproducibility.

### Objective

Prepare Panthera for external review and reproducible release artifacts.

### Definition Of Done

Panthera is release-ready for the next milestone when:

- a clean checkout can rebuild the kernel, kexts, userland, root image, and boot
  target
- generated artifacts are separated from source
- provenance docs explain upstream, local patch, third-party, and generated
  paths
- boot verification passes from scripts
- SSH login works, if SSH is included in the milestone
- networking and DNS behavior are stable or explicitly scoped
- shim debt is known, bounded, and documented
- release images are produced as artifacts, not source-tree state

## Suggested Execution Order

Run the program in this order:

1. Phase 0: Freeze and baseline
2. Phase 1: Canonical current state
3. Phase 2: Provenance and source boundaries
4. Phase 3: Shim debt register
5. Phase 4: Build graph cleanup
6. Phase 5: Boot verification harness
7. Phase 6: libSystem/runtime hardening
8. Phase 7: Authentic Darwin service path
9. Phase 8: Release readiness

Do not skip directly to daemon work unless the project owner explicitly asks.

## Recommended Agent Work Packages

### Work Package A: Status Consolidation

Phases:

- Phase 0
- Phase 1

Expected output:

- baseline boot artifact or blocker note
- `docs/CURRENT_STATE.md`
- supersession banners in old status docs

### Work Package B: Provenance

Phases:

- Phase 2

Expected output:

- complete provenance docs
- generated artifact policy
- third-party ledger
- Apple patch ledger starter

### Work Package C: Shim Registry

Phases:

- Phase 3

Expected output:

- `docs/SHIM_DEBT_REGISTER.md`
- prioritized list of dangerous or ambiguous shims
- list of safe stubs and their permanence decisions

### Work Package D: Rootfs Build Graph

Phases:

- Phase 4

Expected output:

- split rootfs scripts
- initial manifests
- compatibility wrapper preserving old command behavior

### Work Package E: Boot Verification

Phases:

- Phase 5

Expected output:

- repeatable boot verification command
- parsed summaries
- latest boot artifact paths

### Work Package F: Foundation Hardening

Phases:

- Phase 6

Expected output:

- reduced shim ambiguity
- documented dylib workflow
- cleaner launchd/runtime assumptions

## Progress Report Template

Agents should report progress using this shape:

```md
## Summary

Short description of what changed.

## Files Changed

- path: reason

## Verification

- command: result
- boot log: path, if applicable

## Risks

- risk or none

## Remaining Work

- next concrete task
```

## Stop Conditions

Stop and ask for direction if:

- the current known-good boot path is lost and cannot be recovered quickly
- a required generated artifact appears to be untracked but not reproducible
- source and generated output cannot be separated without deleting uncertain
  state
- a proposed cleanup requires removing working fallback behavior
- a frozen dylib appears to require manual modification
- Apple source needs a functional patch that is not clearly Panthera-specific

## Final Principle

Cleanup is not separate from progress for Panthera anymore. The path forward is
to preserve the working system, make the build explainable, make debt visible,
and require each future subsystem to prove itself through reproducible build and
boot evidence.
