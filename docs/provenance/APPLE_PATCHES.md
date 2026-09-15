# Panthera Apple Patch Ledger

Status: Phase 2 starter ledger.

Last updated: 2026-09-10.

## Purpose

This document records Panthera modifications to Apple open-source inputs and
Apple-derived local forks. It is the review ledger for changes that should not
remain implicit in the source tree.

This file does not claim every historical patch is fully audited yet. It
establishes the schema and records the current known patch areas from the Phase
0/1 baseline.

## Patch Rules

Apple source under `src/` should be treated as upstream input. A Panthera patch
is acceptable only when it has:

- a named Panthera reason
- a risk class
- a verification path
- a permanence decision
- a clear owner or next action

Do not add new Panthera changes to Apple source without updating this ledger in
the same change.

## Risk Classes

| Class | Meaning | Review Standard |
|---|---|---|
| `boot-critical` | Required to reach kernel, root mount, launchd, or login | Requires boot evidence |
| `runtime-foundation` | Affects dyld, libSystem, launchd, CF, Mach, bootstrap, or daemon startup | Requires symbol and boot verification |
| `driver-bringup` | Affects kext probing, hardware matching, storage, network, graphics, USB, or HID | Requires targeted boot or device evidence |
| `instrumentation` | Debug traces or diagnostics only | Must be gated or scheduled for cleanup |
| `experimental` | Opt-in subsystem work not part of the stable baseline | Must not replace the verified fallback path |
| `compatibility` | Toolchain, SDK, or missing-private-API adaptation | Requires explanation of the upstream expectation |

## R3.1 Administrative Utilities (2026-09-09)

- `userland/system_cmds/stty/`: unmodified Apple `adv_cmds-213` stty sources.
- `userland/system_cmds/ps/`: Apple `adv_cmds-213`; public `sys/proc.h`
  replaces the SDK-private include. The unsupported `prsna` column is removed
  from the registry, implementation, and manual rather than returning invented
  persona data. Process/task queries retain upstream behavior.
- `userland/mount_tools/{mount.c,vfslist.c,pathnames.h,mount.8}`: Apple
  `diskdev_cmds-593`, using native `getmntinfo` and `mount_<type>` dispatch.
  `/sbin/mount_zfs` is built from the already-pinned OpenZFS macOS helper.
- `userland/system_cmds/shutdown/`: adapted Apple `system_cmds-1039`
  shutdown source, retaining license headers. Scheduled warnings and input
  validation are retained; unsupported kextd/IOKit/reboot3 integration is
  replaced by an executable handoff to native `/sbin/reboot` or `/sbin/halt`.
  Those clients use the pinned launchd-generated root-bootstrap/reboot RPC;
  launchd owns orderly shutdown before XNU sync/unmount. Only explicit
  `reboot -q` bypasses launchd and calls `reboot(2)` directly. Relative times
  are bounded before multiplication. Shutdown `-o` is not exposed; `-n`
  requests `RB_NOSYNC`, and shutdown `-q` suppresses warnings, not teardown.
- Risk: `compatibility`; shutdown also depends on the runtime/platform
  repairs below. Permanent local ports; preserve when updating upstream.
- Source URLs/checksums: `adv_cmds-213` and `diskdev_cmds-593` archives in
  `tools/fetch_world_sources.sh` and `THIRD_PARTY_CHECKSUMS.txt`.
- Verification: `tools/smoke_system_cmds.sh` exercises PID selection, tty
  save/restore, shutdown validation/warn-only mode, failed mount-helper
  propagation, and a ZFS legacy mount/write/unmount/remount cycle.

## R3.1 Platform Selection And Reset (2026-09-10)

- Risk: `boot-critical`, `driver-bringup`, and `runtime-foundation`.
  User-authorized R3.1 repair; not a reopening of R2 storage scope.
- `src/xnu-10002.41.9/iokit/KernelConfigTables.cpp`: the generic fallback
  platform score is 1, below the real AppleI386 platform personality's
  1000 and above the panic fallback's 0. The old equal-score ordering
  selected the base class, leaving `PE_halt_restart` unset.
- `src/AppleI386GenericPlatform-5/AppleI386PlatformExpert.cpp`: enumerate
  the registered top-level array without mutating it; install nub
  properties through `IORegistryEntry::setProperty`. PCI INTx retains
  level/low/shareable flags; ISA child nubs use edge/high, matching their
  existing caller-side routing. Otherwise activating the real platform
  panicked on immutable registry data, then stalled ATA interrupt I/O.
- The platform restart callback waits for the legacy i8042 input buffer
  and writes command `0xfe` to port `0x64`. Failure is reported, not
  represented as successful reset. This qualifies the QEMU legacy-PC
  path only; ACPI-only/physical hardware remains unqualified.
- `userland/system_cmds/build_system_cmds.sh`: normal reboot/halt link
  the existing generated `jobUser` client against the native sysroot.
  A direct normal reboot syscall previously made XNU and launchd compete
  for shutdown ownership, followed by PID 1's NULL-root-manager crash.
  The client now requests launchd shutdown; no panic suppression or
  implicit quick-reboot fallback is used.
- Verification: `tools/smoke_reboot.sh` uses a disposable overlay and
  rejects panic. It requires a real EFI restart after normal reboot and
  scheduled reboot, then a scheduled halt reaching `CPU halted`.
  `artifacts/boot/r31-reset-lifecycle-{zfs,hfs}-20260910.log`: **PASS** on both
  roots. Final alpha gates: `artifacts/release/r31-final-zfs-20260910.summary.md`
  (**23/23 PASS**) and `artifacts/release/r31-final-hfs-20260910.summary.md`
  (**24/24 PASS**).
- Permanent fixes. Preserve the platform selection, registry ownership,
  IRQ semantics, and launchd shutdown ownership when updating upstream.

## Current Dirty Apple Source Patches

These paths were classified during Phase 0 in
`artifacts/boot/baseline-20260508-145804.worktree.md`.

| Component | Version | Files | Current Reason | Risk | Verification | Next Action |
|---|---:|---|---|---|---|---|
| XNU | `10002.41.9` | `src/xnu-10002.41.9/iokit/Kernel/IOPlatformExpert.cpp` | Panthera platform/root-device bring-up and current USB/HID/IOKit expansion work. Exact diff still needs review. | `boot-critical` | Baseline boot reaches `login:` with this worktree. | Split stable boot requirement from experimental expansion and document the exact behavior. |
| AppleI386GenericPlatform | `5` | `src/AppleI386GenericPlatform-5/AppleI386PlatformExpert.cpp` | x86 platform expert compatibility for QEMU/Panthera. | `driver-bringup` | Baseline boot reaches root mount and login. | Record exact matching/platform changes after diff review. |
| AppleRTL8139Ethernet | `153` | `src/AppleRTL8139Ethernet-153/RTL8139.cpp` | QEMU RTL8139 runtime/network bring-up. | `driver-bringup` | Phase 0 did not run a live network smoke test. | Verify `netbringup`, ping, DNS/TCP before treating as stable. |
| IOGraphics | `598` | `src/IOGraphics-598/IOGraphicsFamily/IOBootFramebuffer.cpp`, `src/IOGraphics-598/IOGraphicsFamily/IOFramebuffer.cpp` | Graphics/framebuffer expansion work beyond the boot-to-login baseline. | `experimental` | Baseline boot-to-login only. | Keep opt-in until graphical/HID evidence exists. |
| IOHIDFamily | `2008.40.6` | `IOHIDDevice.cpp`, `IOHIDEventDriverCompat.cpp`, `IOHIDEventService.cpp`, `IOHIDKeyboard.cpp`, `IOBSDConsole.cpp` | HID, keyboard, console, and event service compatibility work. | `driver-bringup` | Baseline reaches `login:`; input was not interactively tested in Phase 0. | Add serial and graphical input smoke tests. |
| IOUSBFamily | `630.4.5` | `AppleUSBUHCI.cpp`, `AppleUSBUHCI_PwrMgmt.cpp`, `AppleUSBUHCI_UIM.cpp`, `IOUSBController.cpp`, `IOUSBControllerV3.cpp`, `IOUSBHIDDriver.cpp` | USB/HID expansion work beyond the stable default path. | `experimental` | Baseline boot-to-login only. | Keep behind opt-in staging until USB/HID boot evidence exists. |

### XNU-10002.41.9: Phase 7A ARP Transmit Handoff

- Files:
  - `src/xnu-10002.41.9/bsd/netinet/in_arp.c`
  - `src/xnu-10002.41.9/bsd/net/dlil.c`
  - `src/xnu-10002.41.9/bsd/net/ether_inet_pr_module.c`
- Upstream expectation: unresolved Ethernet ARP routes should emit ARP requests
  through DLIL and the interface output path.
- Panthera reason: the corrected static IPv4 probe needed unresolved non-static
  ARP routes to enter the stock retry/send path. Panthera initializes the
  unresolved route expiry before that block and adds bounded ARP/DLIL
  diagnostics.
- Risk class: `driver-bringup` plus temporary `instrumentation`.
- Stable baseline impact: boot/login/bootstrap still pass. After the paired
  IONetworking/RTL fixes below, strict static networking passes gateway ping
  and DNS-over-TCP.
- Verification:
  - `artifacts/boot/phase7a-arp-dlil-trace-20260509.summary.md`
  - `artifacts/boot/phase7a-static-arp-after-finalize-20260509.summary.md`
  - `artifacts/boot/phase7a-arptrace-gated-strict-network-20260509.summary.md`
- Permanence: the route-expiry fix may become permanent. The `PANTHERA:ARP`
  print instrumentation is temporary and is now gated behind
  `panthera_arptrace=1`.
- Replacement or cleanup path: keep the route-expiry behavior only if it remains
  required after driver diagnostics are reduced; remove the temporary ARP/DLIL
  trace lines when the interrupt/network path is stable enough.

### IONetworkingFamily-177 and AppleRTL8139Ethernet-153: Phase 7A Static Network Gate

- Files:
  - `src/IONetworkingFamily-177/IONetworkInterface.cpp`
  - `src/IONetworkingFamily-177/IOOutputQueue.cpp`
  - `src/AppleRTL8139Ethernet-153/RTL8139.cpp`
  - `src/AppleRTL8139Ethernet-153/RTL8139PHY.cpp`
  - `src/AppleRTL8139Ethernet-153/RTL8139Private.cpp`
- Upstream expectation: the normal user-space IOKit open/enable path and RTL
  hardware interrupts enable the controller, drain transmit descriptors, and
  deliver receive packets.
- Panthera reason: the minimal boot/probe path configures BSD networking
  directly. The RTL transmit queue stayed stopped until attach-time controller
  enablement. Enabling RTL hardware interrupts at attach time stalled boot, so
  Panthera currently defers those interrupts, starts the queue on link-up, and
  uses timer polling for RX and TX descriptor reclaim. Temporary queue and
  packet diagnostics record the handoff.
- Risk class: `driver-bringup` plus temporary `instrumentation`.
- Stable baseline impact: boot/login/bootstrap still pass. Static networking
  now passes strict probe coverage for address setup, route publication, ARP,
  gateway ping, and DNS-over-TCP.
- Verification:
  - `artifacts/boot/phase7a-txq-drain-trace-20260509.summary.md`
  - `artifacts/boot/phase7a-attach-enable-noirq-20260509.summary.md`
  - `artifacts/boot/phase7a-polled-rx-20260509.summary.md`
  - `artifacts/boot/phase7a-tx-reclaim-poll-20260509.summary.md`
  - `artifacts/boot/phase7a-strict-network-clean-20260509.summary.md`
  - `artifacts/boot/phase7a-trace-clean-strict-network-20260509.summary.md`
  - `artifacts/boot/phase7a-default-strict-network-20260509.summary.md`
  - `artifacts/boot/phase7a-rtlintr-restaged-20260509.summary.md`
  - `artifacts/boot/phase7a-default-after-rtlintr-gate-20260509.summary.md`
- Permanence: the historical timer-polled QEMU RTL8139 mode has been superseded
  by the 2026-05-23 APIC/PIIX/RTL interrupt fixes. RTL hardware interrupts are
  now the default networking path; `panthera_rtlpoll=1` is diagnostic only.
  Queue and packet traces remain gated behind `panthera_rtlpkt=1`.
- Replacement or cleanup path: keep the default OpenSSH/SFTP upload smokes and
  strict network gates green; do not reintroduce packet-trace or timer-poll
  timing dependence for upload stability.

## Apple-Derived Local Forks And Adapters

These areas are not always direct modifications inside `src/`, but they are
Apple-derived runtime work and must be treated with the same review discipline.

| Area | Upstream Basis | Local Path | Current Role | Risk | Verification | Next Action |
|---|---|---|---|---|---|---|
| launchd runtime | `src/launchd-842.92.1` | `userland/launchd/real/` | Panthera-adapted launchd runtime, boot init, and current minimal service ordering. | `runtime-foundation` | Baseline shows launchd loads 7 jobs, enters runtime, and reaches `login:`. | Keep current boot path stable; document each divergence from Apple launchd. |
| CoreFoundation build | `src/CF-1153.18` | `userland/corefoundation/` | Builds/stages CoreFoundation for Panthera daemon work. | `runtime-foundation` | Phase 7B proves dictionary creation in the guest verifier and `configd` dynamic-store creation. | Treat CF/configd as foundation hardening; keep the strict configd gate green. |
| configd/SystemConfiguration | `src/configd-1296.40.6` | `userland/configd/` | Experimental SystemConfiguration/configd build and Panthera shims. | `experimental` | Phase 7G proves `configd` reaches `main`, creates the store, checks in with bootstrap, serves Setup-service multi-get, and can publish a Setup-only `en0` DHCP seed for isolated IPConfiguration starts. | Do not replace `netbringup`; use configd as the stable staging base for IPConfiguration. |
| bootp/IPConfiguration | `src/bootp-531.80.4` | `userland/bootp/` | Experimental IPConfiguration build/staging for DHCP. | `experimental` | Phase 7K proves staged launchd exec, configd readiness, IPv4 attach, daemon-owned DHCP DISCOVER/REQUEST transmit through BPF, OFFER/ACK receive, address assignment, DHCP service storage, synchronous IPv4/DNS dynamic-store publication, default route installation, gateway ping, DNS-over-TCP, server init, `handle_prime()` return, and boot-to-login stability with `netbringup` skipped. Phase 7L tracks the remaining temporary fallbacks in a reproducible audit. | Retire or justify temporary Panthera fallbacks before considering replacement of `netbringup`. |
| mDNSResponder | `src/mDNSResponder-1790.80.10` commit `8769ab51605e465425d33d757f602ce5905ca639` | `userland/mdnsresponder/` | Apple POSIX mDNSResponder daemon, `dns-sd`, and `libdns_sd.dylib` staging. | `experimental` | `artifacts/release/alpha-release-20260518-r2.summary.md` proves mDNSResponder launch plus a guest DNS-SD listener probe over OpenSSH; earlier launch-only evidence remains in `artifacts/boot/mdns-openssh-install-runatload.summary.md`. | Keep `RunAtLoad` without `KeepAlive` until Panthera launchd handles non-MachService daemon keepalive without restart storms; next work is resolver ownership cleanup, not first functional proof. |
| notifyd/libnotify | `src/Libnotify-317` | `userland/notifyd/` | notify client/daemon build and MIG-generated support. | `runtime-foundation` | Baseline shows bootstrap check-in for notification service. | Resolve duplicate exports against `libpanthera_extra`. |

### Libc-1583.40.7: err(3) Formatter Bring-Up

- Files:
  - `src/Libc-1583.40.7/gen/FreeBSD/err.c`
  - `userland/libsystem/build/build_libsystem_c.sh`
  - `userland/libsystem/build/obj/panthera_extra_bridge.c`
  - `userland/libsystem/build/shims/libsystem_c/panthera_libc_aliases.s`
- Upstream expectation: `warnx(3)` and the rest of the `err(3)` family print
  through libc stdio, `_getprogname()` is declared with the correct pointer
  ABI, `utimensat(2)` is available as a normal Darwin timestamp API, and both
  legacy and `$UNIX2003` libc entrypoints can resolve where Apple tools expect
  them.
- Panthera reason: Apple cctools `ar` exposed several libc/runtime gaps during
  package-bootstrap proof. `warnx("creating archive ...")` first exposed an
  undeclared `_getprogname()` pointer truncation and then faulted through the
  current stdio string-stream/`vasprintf` path. Cctools `ranlib` could not set
  archive output timestamps through the existing stubbed `utimensat`, and zsh's
  current export check still needed the legacy `_fchmod` alias. Panthera now
  declares `_getprogname()` in the patched Apple `err.c`, compiles `err.c` with
  a bounded `vfprintf` path for `_e_visprintf()`, supplies a `utimensat` bridge
  over the existing `utimes` syscall path, and aliases `_fchmod` to
  `_fchmod$UNIX2003`.
- Risk class: `runtime-foundation` plus `compatibility`.
- Verification:
  - `bash userland/libsystem/verify_exports.sh` passes with no duplicate
    exports and all critical symbols resolved.
  - `artifacts/boot/cctools-ar-auto-ranlib-direct-smoke-20260518.ssh.log`
  - `artifacts/boot/cctools-ar-ranlib-final-smoke-20260518.ssh.log`
- Permanence: the `utimensat` bridge should remain until libsyscall owns the
  generated Darwin wrapper. The `PANTHERA_ERR_USE_VFPRINTF` compile path should
  be retired after libc string-stream stdio is proven by a dedicated
  `vasprintf`/`open_memstream` test. The `_getprogname()` prototype and
  `_fchmod` ABI alias should remain unless upstream declarations/export
  ownership move to their canonical generated locations.

### Libc-1583.40.7: R3.1 Sleep And Fork Primitives

- **Source overlay:** `src/Libc-1583.40.7/gen/nanosleep.c` validates the full
  signed nanosecond value before narrowing, including negative multiples of
  2^32. Its upstream Mach-clock/semaphore implementation remains in use.
- **Build ownership:** `build_libsystem_c.sh` and `Makefile.libsystem_c`
  compile upstream nanosleep/sleep/usleep with consistent internal names and
  cancellation flags. Separate upstream variants preserve `$NOCANCEL`.
  The old retry-on-every-error sleep bridge and bogus private nanosleep
  syscall alias are removed from `libpanthera_extra`.
- **Fork boundary:** `panthera_libc_bridges.c` runs the native pthread
  prepare/parent/child hooks and resets child Mach and clock state.
  `sources/panthera_mach_init.c` keeps pthread TLS setup in startup rather
  than the child Mach-port refresh. This is not a complete libSystem-wide
  atfork implementation; unrelated subsystem hooks are unchanged.
- **Risk:** runtime foundation. The user explicitly authorized this repair
  after native Vim power evaluation hung and scheduled shutdown failed
  inside sleep. No shutdown-specific delay workaround is retained.
- **Regression:** `tools/libsystem_primitives_probe.c` verifies invalid
  timespecs, real fork-child delay, signal interruption/remaining time, and
  cancelable versus noncancelable sleep. The alpha gate executes it three
  times through its existing SSH return/SFTP check.
- **Observed proof:** `artifacts/runtime/r31-primitives-20260910.summary.md`
  and `artifacts/release/r31-primitives-zfs-20260910.openssh_return.log`.

### bootp-531.80.4: Phase 7C-7K IPConfiguration Startup, DHCP, Network, And Boot Gate

- Files:
  - `src/bootp-531.80.4/IPConfiguration.bproj/ipconfigd.c`
  - `src/bootp-531.80.4/IPConfiguration.bproj/dhcp.c`
  - `src/bootp-531.80.4/IPConfiguration.bproj/bootp_session.c`
  - `src/bootp-531.80.4/IPConfiguration.bproj/arp_session.c`
  - `src/bootp-531.80.4/IPConfiguration.bproj/FDSet.c`
  - `src/bootp-531.80.4/IPConfiguration.bproj/server.c`
  - `src/bootp-531.80.4/bootplib/interfaces.c`
  - `src/bootp-531.80.4/bootplib/udp_transmit.c`
  - `userland/configd/panthera_configd.c`
  - `userland/configd/panthera_network_state_publisher.c`
  - `userland/bootp/panthera_ipconfiguration_compat.c`
  - `userland/bootp/panthera_ipconfiguration_main.c`
  - `rootfs/System/Library/LaunchDaemons/com.apple.IPConfiguration.plist`
- Upstream expectation: IPConfiguration is loaded as a SystemConfiguration
  bundle inside Apple's daemon runtime, uses CoreFoundation bundle context,
  dispatch sources/queues, live SCPreferences observers, CGA, loopback setup,
  dispatch-queued prime work, full SystemConfiguration interface classification,
  power notifications, dynamic-store notifications, and an active Mach receive
  source for its control server.
- Panthera reason: the current Panthera path stages IPConfiguration as a
  standalone launchd job during minimal daemon bring-up. The startup and prime
  gates needed bounded Panthera-only fallbacks for missing
  bundle/runtime/SystemConfiguration/dispatch behavior before DHCP startup could
  be measured. The DHCP-start gate also needed minimal Setup data from configd,
  Setup-service pattern matching, and bounded fallbacks around early IPv4 attach,
  autoaddr ioctls, receive dispatch, OFFER gathering, ARP probing, and address
  publication so the lease path could be measured.
- Risk class: `experimental` plus temporary `instrumentation`.
- Stable baseline impact: `netbringup` remains the stable default. The
  IPConfiguration path is not allowed to replace it until the remaining
  temporary fallbacks are removed or explicitly justified and a controlled
  default-transition gate is added.
- Verification:
  - `artifacts/boot/p7c-ipcfg-primep-20260509.summary.md`
  - `artifacts/boot/p7d-final-20260509.summary.md`
  - `artifacts/boot/p7e-dhcp-gate-20260509.summary.md`
  - `artifacts/boot/p7f-dhcp-packet-send-success-20260509.summary.md`
  - `artifacts/boot/p7g-dhcp-receive-poll-20260509.summary.md`
  - `artifacts/boot/p7g-dhcp-offer-trace-20260509.summary.md`
  - `artifacts/boot/p7g-dhcp-request-ack-trace-20260509.summary.md`
  - `artifacts/boot/p7g-dhcp-bound-address-trace-20260509.summary.md`
  - `artifacts/boot/p7g-dhcp-setup-only-seed-20260509.summary.md`
  - `artifacts/boot/p7g-dhcp-ipv4-attach-20260509.summary.md`
  - `artifacts/boot/p7h-ipv4-attach-trace-20260509.summary.md`
  - `artifacts/boot/p7h-socket-open-kernel-trace-20260509.summary.md`
  - `artifacts/boot/p7h-attach-close-defer-20260509.summary.md`
  - `artifacts/boot/p7i-dhcp-bound-deferred-async-20260509.summary.md`
  - `artifacts/boot/p7j-ipconfiguration-network-20260509.summary.md`
  - `artifacts/boot/p7k-ipconfiguration-boot-login-20260509.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-20260509.md`
  - `artifacts/boot/p7l-retire-transmit-socket-fallback-20260509.summary.md`
  - `artifacts/boot/p7l-no-inline-dhcp-start-experiment-20260509.summary.md`
  - `artifacts/boot/p7l-zero-timer-inline-20260509.summary.md`
  - `artifacts/boot/p7l-zero-timer-disabled-experiment-20260509.summary.md`
  - `artifacts/boot/p7l-random-dhcp-xid-20260509.summary.md`
  - `artifacts/boot/p7l-default-rootfs-restore4-20260509.summary.md`
  - `artifacts/runtime/p7l-arc4random-fallback2-20260509.summary.md`
  - `artifacts/boot/p7l-random-dhcp-xid-after-arc4-fallback-20260509.summary.md`
  - `artifacts/boot/p7l-default-rootfs-restore5-20260509.summary.md`
  - `artifacts/boot/p7l-bpf-darwin-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-bpf-darwin-transmit-20260510.md`
  - `artifacts/boot/p7l-router-arp-park2-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-router-arp-park-20260510.md`
  - `artifacts/boot/p7l-address-arp-probe-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-address-arp-probe-20260510.md`
  - `artifacts/boot/p7l-address-normal-ioctl-net.summary.md`
  - `artifacts/boot/p7l-address-noarp-clear-net.summary.md`
  - `artifacts/boot/p7l-address-reapply-net.summary.md`
  - `artifacts/boot/p7l-address-activation-restored-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-address-activation-experiments-20260510.md`
  - `artifacts/boot/p7l-prefs-callback-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-prefs-callback-20260510.md`
  - `artifacts/boot/p7l-dynamic-store-notifier-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-notifier-restore-20260510.md`
  - `artifacts/boot/p7l-configure-cache-initial-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-configure-cache-initial-20260510.md`
  - `artifacts/boot/p7l-setup-prefs-zsh-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-setup-prefs-20260510.md`
  - `artifacts/boot/p7l-configd-poll-retired-zsh-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-configd-poll-retire-20260510.md`
  - `artifacts/boot/p7l-configd-owned-ipconfiguration-experiment.summary.md`
  - `artifacts/boot/p7l-configd-owned-ipconfiguration-default.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-configd-owned-plugin-20260510.md`
  - `artifacts/boot/p7l-real-bundle-quiet-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-real-bundle-quiet-20260510.md`
  - `artifacts/boot/p7l-cfurl-resource-read-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-cfurl-resource-read-20260510.md`
  - `artifacts/boot/p7l-async-data-add-configd-net.summary.md`
  - `artifacts/boot/p7l-async-data-add-restored-net.summary.md`
  - `artifacts/boot/p7l-async-dynamic-store-publish-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-async-data-add-20260510.md`
  - `artifacts/boot/p7l-service-global-publish-clean-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-service-global-publish-20260510.md`
  - `artifacts/boot/p7l-configd-route-manager-net-retry.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-configd-route-manager-20260510.md`
  - `artifacts/boot/p7l-arp-callback-source-net.summary.md`
  - `artifacts/boot/p7l-arp-callback-direct-restored-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-arp-callback-source-experiment-20260510.md`
  - `artifacts/boot/p7l-bootp-delayed-close-net.summary.md`
  - `artifacts/boot/p7l-bootp-source-suspend-restored-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-bootp-delayed-close-experiment-20260510.md`
  - `artifacts/boot/p7l-cga-init-restored-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-cga-init-restore-20260510.md`
  - `artifacts/boot/p7l-linklocal-elect-net.summary.md`
  - `artifacts/boot/p7l-linklocal-deferred-restored-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-linklocal-elect-experiment-20260510.md`
  - `artifacts/boot/p7l-arp-fd-release-net.summary.md`
  - `artifacts/boot/p7l-arp-source-suspend-restored2-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-arp-fd-release-experiment-20260510.md`
  - `artifacts/boot/p7l-fdcallout-cancel-handler-arp-release-net.summary.md`
  - `artifacts/boot/p7l-fdcallout-cancel-handler-arp-suspend-restored-net.summary.md`
  - `artifacts/boot/p7l-fdcallout-cancel-handler-bootp-delayed-close-net.summary.md`
  - `artifacts/boot/p7l-fdset-revert-suspend-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-fdcallout-cancel-handler-20260510.md`
  - `artifacts/boot/p7l-fdset-defer-arp-release-net.summary.md`
  - `artifacts/boot/p7l-fdset-defer-arp-suspend-r2.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-fdset-defer-free-20260510.md`
  - `artifacts/boot/p7l-arp-release-buffer-park-net.summary.md`
  - `artifacts/boot/p7l-arp-release-buffer-restored-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-arp-buffer-park-20260511.md`
  - `artifacts/boot/p7l-arp-async-callback-release-net.summary.md`
  - `artifacts/boot/p7l-arp-async-callback-restored-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-arp-async-callback-20260511.md`
  - `artifacts/boot/p7l-fdset-cancel-park-arp-release-net.summary.md`
  - `artifacts/boot/p7l-fdset-cancel-park-restored2-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-fdset-cancel-park-20260511.md`
- Current adaptations: strict staged IPConfiguration now starts inside configd
  rather than through a separate launch daemon. Panthera configd advertises the
  IPConfiguration Mach service, runs its SCDynamicStore MIG server on a helper
  thread, and runs IPConfiguration on configd's main run loop. IPConfiguration
  now has a staged SystemConfiguration bundle and receives a real `CFBundleRef`
  from configd before `load()`; normal CGA initialization now runs during
  IPConfiguration start; link-local election remains deferred after the
  p7l-linklocal-elect-net experiment reached LINKLOCAL service creation but
  caused configd SIGILL respawns before network publication/probes could pass;
  configd derives
  Global IPv4/DNS from
  IPConfiguration's service IPv4/DNS state; configd now owns a minimal
  route-manager PF_ROUTE write for the DHCP default route, while full Apple
  configd/IPMonitor route publication remains pending; DHCP address assignment
  still primes/finalizes interface flags because narrower Phase 7L experiments
  left `en0` without `IFF_RUNNING` and the kernel send path returned `Network is
  down`; BOOTP/ARP read sources are parked on close; and ARP completion is
  delivered directly until Panthera's DATA_ADD source and dispatch-source
  cancellation behavior are stable. The Phase 7L ARP DATA_ADD callback-source
  restoration experiment reached DHCP ACK/address assignment, but then reaped
  configd/IPConfiguration with SIGILL before dynamic-store publication, Global
  IPv4/DNS, route publication, or daemon network probes could pass; restoring
  the direct callback/source-park fallback returns the strict gate to green.
  The Phase 7L ARP FDCalloutRelease restoration experiment kept direct callback
  delivery but restored normal ARP read-source release; configd still reaped
  with SIGILL shortly after DHCP address assignment, before publication or
  daemon network probes could pass, so the ARP source-suspend fallback remains
  audited.
  The Phase 7L FDCallout cancel-handler ownership experiment moved final
  dispatch-source release/free into the cancel handler. It was safe with the
  already-audited ARP source-suspend fallback, but did not make normal ARP
  read-source release safe and did not make BOOTP delayed close safe; both
  experiments still reaped configd before the strict daemon network gate could
  pass. Panthera reverted that FDCallout ownership experiment and preserved the
  green source-suspend fallbacks. Panthera now keeps a narrower FDSet lifetime
  correction: if a callout is released from inside its own event handler, FDSet
  defers the final free until the handler returns. The
  p7l-fdset-defer-arp-release-net experiment proves that this local lifetime
  fix is not enough to restore normal ARP FDCalloutRelease; configd still reaps
  after DHCP address assignment and before stable publication/probes. The
  p7l-fdset-defer-arp-suspend-r2 gate proves the FDSet fix is safe with the
  audited ARP source-suspend fallback. The p7l-arp-release-buffer-park-net
  experiment kept ARP's receive buffer allocated while restoring normal
  FDCalloutRelease; it still reaped configd after DHCP address assignment, so
  immediate receive-buffer free is not the active cause. The restored
  p7l-arp-release-buffer-restored-net gate keeps the source-suspend path green.
  The p7l-arp-async-callback-release-net experiment deferred ARP completion
  with `dispatch_async()` on the IPConfiguration agent queue while restoring
  normal FDCalloutRelease and receive-buffer free; it still reaped configd
  after address assignment, so callback deferral alone is not enough to retire
  the ARP source-suspend fallback. The restored
  p7l-arp-async-callback-restored-net gate keeps the source-suspend path green.
  The p7l-fdset-cancel-park-arp-release-net experiment restored ARP close far
  enough to call `dispatch_source_cancel()`, then parked the dispatch source
  and FDCallout instead of releasing/freeing them. It still reaped configd after
  address assignment, which narrows the active ARP close blocker to dispatch
  source cancel/unregistration or cancel-handler delivery rather than
  caller-side source release or FDSet free. The restored
  p7l-fdset-cancel-park-restored2-net gate keeps the source-suspend path green.
  The p7l-dispatch-cancel-trace-arp-release-net diagnostic restored normal ARP
  FDCalloutRelease and receive-buffer free with libdispatch cancel tracing. It
  reached source cancel, immediate kevent direct unregister, source deletion,
  and cancel-handler pop before configd was reaped by SIGILL. That points at
  cancel-handler delivery/continuation teardown rather than kevent delete or
  caller-side release/free. The restored p7l-dispatch-cancel-trace-restored-boot
  gate passes through DHCP publication, Global IPv4/DNS, and default-route
  evidence with the source-suspend fallback restored.
  The Phase 7L BOOTP delayed-close restoration experiment reached DHCP
  DISCOVER/OFFER, but repeatedly reaped configd/IPConfiguration with SIGILL
  before DHCP REQUEST/ACK, address publication, route publication, or daemon
  network probes could pass; restoring the BOOTP source-suspend fallback
  returns the strict gate to green.
  Panthera configd still implements the minimal `[^/]+` path-component matching
  needed by Setup-service `SCDynamicStoreCopyMultiple()`, configd imports
  persistent SystemConfiguration Setup preferences before service check-in, and
  DHCP startup tracing records the current bring-up path. Phase 7L has retired
  the earlier loopback setup skip, autoaddr ioctl skip, IPv4 attach skip, direct
  receive polling, local socket wrappers, deterministic UDP IDs, BPF-failure
  socket retry, router-ARP skip, post-ACK address-conflict ARP skip,
  setup-only DHCP seed, configd-ready polling loop, active standalone
  IPConfiguration launchd ownership, and async-work queue coalescer.
  Phase 7K adds a verifier-only boot gate that stages the same path under the
  normal `/bin/mini_sh` root shell, skips `netbringup`, and requires the DHCP
  ACK/address assignment, dynamic-store publish, and default route traces before
  accepting boot-to-login stability. Phase 7L adds
  `tools/audit_ipconfiguration_fallbacks.py`, a source-anchor audit that tracks
  the remaining temporary staged-path fallbacks and records each owner,
  replacement gate, and risk class. Its first cleanup retired the unused
  temporary BOOTP transmit socket fallback from `bootp_session.c`; the staged
  boot gate still passes because DHCP transmit uses the receive socket plus
  BPF path. The follow-up no-inline delayed-start experiment added a verifier
  switch that injects `PANTHERA_IPCONFIGURATION_NO_INLINE_DHCP_START=1` into
  the staged launchd environment. It proved the inline delayed-start fallback
  was covering a real timer/dispatch gap because the scheduled timer callback
  does not fire. The DHCP-specific inline call was then retired and replaced
  with a generic zero-delay timer fallback in `timer.c`. The staged boot gate
  passes with the generic fallback, while the disabled-fallback diagnostic
  still fails at the dispatch timer callback boundary. A later attempt to
  retire deterministic DHCP XID by using `arc4random()` stopped after DHCP
  lease-list initialization and before XID tracing, exposing libsystem_c's
  unresolved corecrypto `ccrng` dependency. `build_libsystem_c.sh` now compiles
  `gen/FreeBSD/arc4random.c` through Apple's getentropy-backed fallback, and the
  random-XID staged boot gate passes. Phase 7L then retired the separate
  deterministic UDP IP ID in `udp_transmit.c` and removed Panthera's UDP socket
  retry after BPF broadcast failure while preserving the Apple BPF
  header-complete transmit path. Phase 7L then restored the post-bind router
  ARP resolution path: DHCP now calls `service_resolve_router()`, receives the
  router ARP reply through the ARP BPF read source, and completes publication
  after the router callback returns success. Phase 7L then restored the DHCP
  address-conflict ARP probe before assignment; the strict gate proves the
  no-conflict callback, address assignment, router ARP callback, and no
  IPConfiguration SIGILL reap. Phase 7L then tried to retire the address
  flag/finalize sequence, but the single-ioctl, NOARP-clear-only, and
  reapply-only experiments all left `en0` at `0xffff8823` and failed gateway
  ping/DNS; restoring the full sequence reached `0xffff8863` and passed the
  strict daemon-owned network gate. Phase 7L then restored live control and
  DHCP SCPreferences callbacks; both `SCPreferencesSetCallback()` and
  `SCPreferencesSetDispatchQueue()` return success for the control and DHCP
  preference sessions, and the strict daemon-owned network gate still passes
  through DHCP, gateway ping, DNS-over-TCP, and IPConfiguration control MIG.
  Phase 7L then restored direct IPConfiguration dynamic-store notification
  registration; `SCDynamicStoreSetNotificationKeys()` and
  `SCDynamicStoreSetDispatchQueue()` both return success under the strict
  daemon-owned network gate.
  Phase 7L then retired the duplicate Panthera prime-time
  `configuration_changed(S_scd_session)` call; the normal
  `configure_from_cache()` pass now discovers the Setup service and starts DHCP
  while the strict daemon-owned network gate stays green.
  Phase 7L then retired configd's setup-only `address=pending` DHCP seed by
  staging `/Library/Preferences/SystemConfiguration/preferences.plist` and
  loading its `NetworkServices` entries into `Setup:/Network/...` before
  configd checks in with launchd. The zsh strict daemon-owned network gate
  proves DHCP, dynamic-store publication, default route, gateway ping,
  DNS-over-TCP, and IPConfiguration control MIG still pass. Phase 7L then
  retired the launcher's configd-ready polling loop; ordered launch plus the
  one-shot dynamic-store preflight is enough for the same zsh strict
  daemon-owned network gate. Phase 7L then moved strict staged IPConfiguration
  under configd ownership: configd advertises `com.apple.network.IPConfiguration`,
  starts a helper thread for its SCDynamicStore MIG server, and runs
  IPConfiguration on the configd main run loop while skipping the standalone
  launch daemon. The default zsh strict daemon-owned network gate passes in
  that mode. Phase 7L then retired null-bundle toleration by staging
  `/System/Library/SystemConfiguration/IPConfiguration.bundle`, passing that
  bundle context to IPConfiguration, and temporarily reading the bundle
  `Contents/Info.plist` through a bounded CoreFoundation compatibility path.
  Phase 7L then moved that compatibility down into
  `CFURLCreateDataAndPropertiesFromResource()` for file URLs, so CFBundle uses
  the normal deprecated CFURL resource API again. The strict daemon-owned
  network gate passes through DHCP bind, dynamic-store publication, default
  route, gateway ping, DNS-over-TCP, and IPConfiguration control MIG with the
  real bundle context. Phase 7L then restored IPConfiguration's DATA_ADD
  async-work source after the libdispatch timer/workqueue corrections; the
  strict configd-owned network gate proves source creation, merge-data
  scheduling, async maintenance drain, DHCP, dynamic-store publication, gateway
  ping, DNS-over-TCP, and IPConfiguration control MIG without a SIGILL reap. The
  follow-up async-only dynamic-store publication experiment was not kept:
  removing the synchronous primary IPv4/DNS write timed out before the
  daemon-owned network gate and left Global IPv4/DNS publication unproven. That
  fallback now points at the missing configd/SystemConfiguration
  service-to-global publisher rather than the async-work source itself. The
  service-to-global publisher now runs in configd's `SCDynamicStoreSetMultiple()`
  path: IPConfiguration schedules async dynamic-store publication, and configd
  derives interface IPv4 plus Global IPv4/DNS from the service IPv4/DNS
  dictionaries. The strict daemon-owned network gate proves DHCP, derived
  Global IPv4/DNS, route, gateway ping, DNS-over-TCP, and control MIG. The
  default-route writer has also moved out of IPConfiguration's DHCP success
  path and into configd's service-to-global publisher as a minimal
  route-manager transition. The strict configd-owned network gate proves the
  configd route-manager write, route visibility, gateway ping, DNS-over-TCP,
  and control MIG. Normal CGA initialization has also been restored during
  IPConfiguration start; the strict gate verifies `CGAInit()` returns before
  DHCP continues. The fallback audit now tracks 6 items, with the
  plugin-loader transition represented by the minimal configd-owned path and
  the former direct IPConfiguration route write represented by the narrower
  configd route-manager transition.
  The remaining Panthera ARP adaptation is narrower and audited: ARP completion
  is delivered directly and the ARP BPF read source is suspended/parked on close
  because the DATA_ADD callback source and dispatch-source cancellation from
  the read handler still SIGILL under the current libdispatch path. The
  `p7l-arp-callback-source-net` experiment is preserved as the failed boundary,
  and `p7l-arp-callback-direct-restored-net` is the restored green gate.
  `p7l-arp-fd-release-net` separately proves normal ARP FDCalloutRelease still
  triggers a configd SIGILL after DHCP address assignment, while
  `p7l-arp-source-suspend-restored2-net` is the restored green gate. The
  `p7l-fdcallout-cancel-handler-arp-release-net` and
  `p7l-fdcallout-cancel-handler-bootp-delayed-close-net` experiments prove
  cancel-handler-owned FDCallout teardown is not sufficient to retire the ARP
  or BOOTP source-suspend fallbacks; `p7l-fdset-revert-suspend-net` is the
  restored green gate after reverting that experiment.
  `p7l-fdset-defer-arp-release-net` proves FDSet in-handler deferred free alone
  still cannot retire normal ARP FDCalloutRelease, while
  `p7l-fdset-defer-arp-suspend-r2` proves that narrower FDSet lifetime fix is
  safe on the current audited source-suspend path.
  `p7l-arp-release-buffer-park-net` proves keeping the ARP receive buffer alive
  is also insufficient; normal ARP read-source release/cancellation remains the
  failing boundary. `p7l-arp-async-callback-release-net` proves that deferring
  ARP completion with `dispatch_async()` before normal FDCalloutRelease is also
  insufficient; `p7l-arp-async-callback-restored-net` is the restored green
  gate. `p7l-fdset-cancel-park-arp-release-net` proves canceling the read source
  while parking caller-side source release/free is still insufficient;
  `p7l-fdset-cancel-park-restored2-net` is the restored green gate. The
  BOOTP read-source lifecycle remains similarly audited:
  `p7l-bootp-delayed-close-net` is the failed delayed-close boundary, and
  `p7l-bootp-source-suspend-restored-net` is the restored green gate.
- Permanence: temporary. These changes are startup gates, not the final Darwin
  service model.
- Replacement or cleanup path: prove boot-to-login stability with the staged
  Apple daemon path, then remove each fallback as the underlying
  CoreFoundation, SystemConfiguration, dispatch, ioctl, receive, ARP, route, or
  network-service runtime gap is corrected.

### IPConfiguration Panthera Compat: Retired UNIX2003 Socket Wrappers

- Files:
  - `userland/bootp/panthera_ipconfiguration_compat.c`
- Upstream expectation: UNIX2003 suffixed socket entry points used by the Apple
  drop should forward to the base libSystem socket calls.
- Panthera reason: Phase 7G needed receive-side clarity after the send path was
  proven, so Panthera temporarily forwarded `_recvmsg$UNIX2003` and
  `_sendmsg$UNIX2003` from the IPConfiguration staging binary. Phase 7L moved
  socket UNIX2003 syscall aliases to `libsystem_kernel`, rebuilt the shared
  cache, restored normal BOOTP `bind()`, `recvmsg()`, and `sendto()` calls, and
  removed the local IPConfiguration socket wrappers.
- Risk class: `experimental`.
- Stable baseline impact: none to stable `netbringup`; this only affects the
  staged IPConfiguration binary.
- Verification:
  - `artifacts/boot/p7g-dhcp-receive-poll-20260509.summary.md`
  - `artifacts/boot/p7g-dhcp-offer-trace-20260509.summary.md`
  - `artifacts/boot/p7l-nosocklocal-cache-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-local-socket-wrapper-retire-20260510.md`
- Permanence: retired in Phase 7L. Socket UNIX2003 aliases are owned by
  `libsystem_kernel`, and `ipconfiguration` imports them from `libSystem`.

### configd Panthera Network State Publisher: Setup-Only DHCP Seed

- Files:
  - `userland/configd/panthera_network_state_publisher.c`
- Upstream expectation: real Setup network configuration already exists before
  IPConfiguration evaluates network services.
- Panthera reason: isolated daemon-owned DHCP should not depend on the static
  network verifier publishing State IPv4 address data first. When no primary
  interface/address exists yet, Panthera configd now publishes a Setup-only DHCP
  service for `en0` so IPConfiguration can discover and start the DHCP service.
- Risk class: `experimental`.
- Stable baseline impact: none to stable `netbringup`; this only affects staged
  configd/IPConfiguration experiments.
- Verification:
  - `artifacts/boot/p7g-dhcp-setup-only-seed-20260509.summary.md`
- Permanence: temporary seed until Panthera has persistent SystemConfiguration
  preferences or another correct Setup source.

### CoreFoundation CFURL Resource Reads: Panthera File-URL Compatibility

- Files:
  - `src/CF-1153.18/CFURLAccess.c`
  - `src/CF-1153.18/CFBundle_InfoPlist.c`
- Upstream expectation: CFBundle reads bundle `Contents/Info.plist` through
  `CFURLCreateDataAndPropertiesFromResource()` rather than a bundle-local file
  reader.
- Panthera reason: the first real-bundle IPConfiguration gate exposed a fault in
  CoreFoundation's deprecated file-resource data-read path. Panthera now resolves
  file URLs to POSIX paths in `CFURLAccess.c` and reads data with a bounded POSIX
  path, while CFBundle has been returned to the normal CFURL resource API.
- Risk class: `runtime-foundation`.
- Stable baseline impact: configd-owned IPConfiguration still reaches DHCP bind,
  publishes IPv4/DNS dynamic-store state, installs the default route, passes
  gateway ping and DNS-over-TCP, and answers IPConfiguration control MIG.
- Verification:
  - `artifacts/boot/p7l-real-bundle-quiet-net.summary.md`
  - `artifacts/boot/p7l-cfurl-resource-read-net.summary.md`
  - `artifacts/runtime/p7l-ipconfiguration-fallback-audit-after-cfurl-resource-read-20260510.md`
- Permanence: compatibility until the underlying `_CFReadBytesFromFile()` /
  file-URL representation path is corrected broadly enough to remove the
  Panthera-specific data-read branch.

### CoreFoundation Panthera Support: Retired UNIX2003 Socket Wrappers

- Files:
  - `userland/corefoundation/cf_panthera_support.c`
- Upstream expectation: UNIX2003 suffixed symbols should forward to the base
  libSystem/libsystem_kernel functions rather than recursively entering their
  own suffixed symbol bodies.
- Panthera reason: the Panthera CoreFoundation build could compile socket
  wrappers such as `_sendto$UNIX2003`, `_bind$UNIX2003`, and
  `_recvfrom$UNIX2003` into self-jumps when public names macro-remapped to the
  suffixed symbols. Phase 7F used explicit CoreFoundation base-symbol
  forwarding to unblock DHCP packet diagnostics. Phase 7L then moved socket
  UNIX2003 syscall aliases to `libsystem_kernel`, removed CoreFoundation's
  Panthera socket exports, rebuilt the dyld shared cache, and proved the staged
  daemon-owned network path still passes.
- Risk class: `runtime-foundation`.
- Stable baseline impact: stable `netbringup` remains unchanged; configd,
  CoreFoundation smoke tests, Phase 7F packet-send gates, and Phase 7L
  daemon-owned network gates pass after the ownership correction and
  shared-cache rebuild.
- Verification:
  - `artifacts/boot/p7f-dhcp-packet-send-success-20260509.summary.md`
  - `artifacts/boot/p7l-kern-sockalias-cache-net.summary.md`
  - `artifacts/boot/p7l-nosocklocal-cache-net.summary.md`
- Permanence: retired for socket wrappers in Phase 7L. Keep auditing any
  remaining non-socket UNIX2003 compatibility aliases by owner and move them
  under the matching libSystem component instead of CoreFoundation where
  possible.

### XNU-10002.41.9: Phase 7F DHCP Send-Path Instrumentation

- Files:
  - `src/xnu-10002.41.9/bsd/kern/uipc_syscalls.c`
  - `src/xnu-10002.41.9/bsd/kern/uipc_socket.c`
  - `src/xnu-10002.41.9/bsd/netinet/udp_usrreq.c`
- Upstream expectation: UDP broadcast sends should enter `sendto_nocancel`,
  `sosend`, `udp_output`, and `ip_output`, then return the byte count or a
  normal errno.
- Panthera reason: Phase 7F needed to distinguish a userland symbol/cache
  failure from a kernel UDP/IP failure while measuring DHCP DISCOVER transmit.
- Risk class: `instrumentation`.
- Stable baseline impact: stable default networking remains green; the
  instrumentation is diagnostic and should be removed or gated after the DHCP
  lease path is understood.
- Verification:
  - `artifacts/boot/p7f-dhcp-packet-send-success-20260509.summary.md`
- Permanence: temporary.
- Replacement or cleanup path: delete or gate these `PANTHERA:KERN` send-path
  traces once receive/lease work has separate evidence.

### XNU-10002.41.9: Phase 7H AF_INET Socket Creation Instrumentation

- Files:
  - `src/xnu-10002.41.9/bsd/kern/uipc_syscalls.c`
  - `src/xnu-10002.41.9/bsd/kern/uipc_socket.c`
- Upstream expectation: `socket(AF_INET, SOCK_DGRAM, 0)` should return a file
  descriptor to IPConfiguration's `inet_attach_interface()` helper before the
  daemon issues `SIOCPROTOATTACH`.
- Panthera reason: Phase 7H needed to distinguish a kernel socket creation
  blocker from a userland/runtime blocker while re-enabling opt-in IPv4 attach
  in the isolated daemon path.
- Risk class: `instrumentation`.
- Stable baseline impact: stable default networking remains green; the trace is
  gated behind boot arg `panthera_socktrace=1`.
- Verification:
  - `artifacts/boot/p7h-socket-open-kernel-trace-20260509.summary.md`
  - `artifacts/boot/p7h-attach-close-defer-20260509.summary.md`
- Permanence: temporary.
- Replacement or cleanup path: keep only while isolating the daemon-owned DHCP
  `ENETUNREACH` boundary, then remove the socket creation prints or replace
  them with a narrower diagnostic.

## Historical Known Patch Areas

These are known from existing status/provenance docs and require detailed
file-by-file expansion before external review.

| Component | Known Patch Theme | Required Follow-Up |
|---|---|---|
| XNU | platform nub seeding, CMOS RTC, kext loader tracing, IOKit/boot diagnostics, compatibility workarounds | Convert to file-by-file entries with risk and permanence. |
| launchd | PID 1 bring-up path, runtime shortcuts, job loading, bootstrap/MIG adaptation, signal/audit/syslog gaps | Map each Panthera divergence against Apple `runtime.c` and `core.c`. |
| zsh | early shell/runtime tracing and bring-up adjustments | Decide what remains necessary and what should be removed. |
| shell_cmds | utility bring-up fixes such as `id` behavior | Verify each patch still applies to current libinfo/libc state. |
| libSystem-related Apple drops | local build and export visibility patches | Move symbol ownership decisions into `docs/SHIM_DEBT_REGISTER.md`. |

## Patch Entry Template

Use this template for new entries:

```md
### Component-Version: short patch name

- Files:
- Upstream expectation:
- Panthera reason:
- Risk class:
- Stable baseline impact:
- Verification:
- Permanence:
- Replacement or cleanup path:
```

## Phase 2 Next Actions

1. Review diffs for every path in the current dirty Apple source list.
2. Split stable baseline requirements from experimental USB/HID/graphics work.
3. Add detailed launchd divergence entries for `userland/launchd/real/`.
4. Cross-link shim/export fixes to `docs/SHIM_DEBT_REGISTER.md` as Phase 3
   expands the register.
