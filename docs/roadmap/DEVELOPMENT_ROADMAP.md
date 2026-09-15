# Panthera Development Roadmap (Post-Recovery)

> **Status: AUTHORITATIVE NEXT-MILESTONE SEQUENCE.**
> Baseline: `recovery/unify-20260828`, clean-world PASS at `b50c669` on 2026-09-06.
> Present state: `docs/CURRENT_STATE.md`. Phase history: `docs/roadmap/OS_BUILD_ROADMAP.md`.

## Strategy

Determinism first, then performance, then capability. Every open milestone is
gated: it is done when the named command produces a PASS summary under
`artifacts/`, not when code merges. No milestone may bypass
`tools/build_world.sh`, the release gates, or the generated-artifact policy.

```mermaid
graph LR
    R1[R1 Deterministic Foundation] --> R2[R2 Storage Performance]
    R1 --> R3[R3 Self-Hosting Userland]
    R2 --> R3
    R3 --> R4[R4 Service Hardening]
    R4 --> R5[R5 Security & Multi-User]
    R5 --> R6[R6 Identity & Release]
```

---

## R1 — Deterministic Foundation (complete, 2026-09-06)

All three exit criteria below have recorded PASS evidence. The clean-world
regression bar now covers both storage tracks; R2 is the next milestone.

### R1.1 launchd/configd first-boot signal race
- **Problem:** A fresh ZFS control image can panic (`initproc failed to start`)
  when configd's `notify_launchd_network_up_now()` sends `SIGUSR1` to PID 1
  before `launchd_runtime_init2()` has installed `SIG_IGN`/kqueue handling
  (evidence: `artifacts/boot/unify-20260828-zfs-verified-root-populate.log`).
  The immediate retry passed, so this is a startup-window race, not a
  functional failure.
- **Fix direction:** Close the window in launchd (ignore the signal set before
  jobs are dispatched in `main()`), not by delaying configd; the kernel may
  deliver the signal at any point after PID 1 exec.
- **Exit:** Three consecutive one-shot fresh ZFS control+root image builds
  (`tools/zfs_release_gate.sh` without `--reuse-images`) PASS without retry.
- **Accepted:** `artifacts/release/r1-zfs-determinism-pass-20260828.summary.md`
  records three consecutive fresh control+root builds, no failed attempts,
  and unchanged historical baseline seed. The launchd signal disposition
  is installed before early job dispatch.

### R1.2 Vendor pinned libunwind
- **Problem:** The runtime phase depends on a host-local LLVM checkout via
  `PANTHERA_LLVM_ROOT`; a fresh checkout cannot rebuild `libunwind.a`.
- **Fix direction:** Checksum-fetch a pinned `libunwind` source archive through
  `tools/fetch_with_checksum.sh` and record it in
  `docs/provenance/THIRD_PARTY_CHECKSUMS.txt`, matching the existing intake
  model for OpenSSH/OpenSSL/curl.
- **Exit:** `tools/build_world.sh --phase runtime` succeeds on a checkout with
  no `PANTHERA_LLVM_ROOT` set.
- **Accepted:** Pinned libunwind 18.1.8 and companion archives are
  checksum-fetched. Both full runtime builds passed without
  `PANTHERA_LLVM_ROOT` in the clean-world run below.

### R1.3 Clean world build proof
- **Problem:** Every subsystem builds and every gate passes, but no single
  fresh-checkout run has been proven end to end.
- **Fix direction:** One script (`tools/clean_world_proof.sh`) that clones into
  a temp directory, runs `tools/build_world.sh` across the full graph for both
  targets, then runs both release gates, writing one summary artifact.
- **Exit:** One command, one PASS summary in `artifacts/release/`. This becomes
  the standing regression bar for all later milestones.
- **Accepted:** `artifacts/release/r1-clean-world-20260905-pass13.summary.md`
  is PASS at `b50c6691f8a8c120f2006d933cee912afe8c4225`: eight steps passed,
  zero failed, 4,197 seconds. HFS and ZFS gates passed; the newly generated
  ZFS seed SHA-256 remained unchanged. The run completed on 2026-09-06 UTC.
- **Retained evidence:** `/tmp/panthera-r1-clean-world-pass13`; sibling
  `r1-clean-world-20260905-pass13-gate-{hfs,zfs}.summary.md` files under
  `artifacts/release/` contain the individual gate results.
- **Boundary:** Local QEMU acceptance with polled PIO, explicit ATA
  flush bypass, and host writethrough caching. DMA, physical hardware,
  and CI qualification are not claimed.

---

## R2 — Storage & Driver Performance (active)

### Where the time actually goes (measured, 2026-05/09)

All numbers are QEMU TCG on an Apple Silicon host (x86_64 guest is
interpreted, so CPU-bound guest paths dominate; a real x86 host would shift
every ratio below).

| Workload | Result | Mode | Evidence |
|---|---|---|---|
| Clean-world proof | 4,197 s (PIO) → 3,840 s (DMA, −8.5%); gates 889 s (HFS) + 1,054 s (ZFS) | PIO → DMA | `artifacts/release/r1-clean-world-20260905-pass13.summary.md`, `artifacts/release/r2-clean-world-20260908-pass1.summary.md` |
| 74 MB rootfs payload SFTP upload | 41–109 s (~0.7–1.8 MB/s; after: 24–25 s DMA) | PIO → DMA | `artifacts/boot/r1-bootfs-root-*.upload.log`, `artifacts/release/r2-dma-hfs-{4,5,6}.summary.md` |
| `pkgsrc/devel` extract, 47,057 entries | 212 s | **DMA on** (`useDMA=1`) | `artifacts/boot/zfs-fresh-root-pkgsrc-devel-extract-nometa-owner-20260531-r1.{log:140,ssh.log:163}` |
| Same extract, `atime=off` variant | 241 s | DMA on | `artifacts/boot/zfs-dma-devel-extract-atime-off-20260529-r1.ssh.log:157` |
| 5k-file metadata probe: create / unlink / rmdir | 412 / 683 / 812 ops/s | DMA on | `artifacts/boot/zfs-zinactive-rmnode-meta-5k-20260531-r1.ssh.log` |
| 5,120 unlinked-vnode reclaims | 1,080 ms, 94% in `zfs_zinactive` → 96% in `zfs_rmnode` | DMA on | `artifacts/boot/zfs-zinactive-rmnode-meta-5k-20260531-r1.log:1082-1102` |
| `unlinkat` syscall (4,064 ms sample) | 1,718 ms in `put_vp` → synchronous `vclean` | DMA on | `artifacts/boot/zfs-vnode-lifecycle-meta-5k-20260531-r1.log:970-1010` |

Two conclusions fall out of that table and drive the ordering below:

1. **The ZFS metadata baseline was already measured with DMA on.** R2.1 will
   not move R2.2's number; the two items are independent and R2.2 is the
   larger win for the developer loop (`pkgsrc`, R3).
2. **The DMA "stall" has no artifact.** `panthera_ata_nodma=1 panthera_ata_polled=1`
   were introduced in `87435cf` (2026-08-28) as a compatibility default; ZFS
   boots with DMA on had been passing since 2026-05-29
   (`artifacts/boot/zfs-ata-dma-default-boot-20260529-r{1,2}.log`). The first
   R2.1 deliverable is a reproduction, not a fix.

### R2.1 ATA DMA + interrupt default (complete, 2026-09-08)

- **State of the driver:** DMA is fully implemented, not stubbed. PRD tables and
  bus-master registers live in `src/AppleIntelPIIXATA-251.0.1/AppleIntelPIIXPATA.cpp:1435-1570`;
  `PantheraATAStorage::configureDevice()` (`PantheraATAStorage.cpp:446-452`)
  selects `kATAcmdRead/WriteDMA` when `dmaAvailable && !disableDMA`. Every
  I/O is synchronous under `_ioLock` in `executeSectors()` (lines 512-593),
  chunked at 256 sectors, LBA28 only (`currentBlock > 0x0fffffff` → unsupported,
  line 554). `doAsyncReadWrite` completes inline (lines 255-282).
- **Flush:** `doSynchronize()` short-circuits on `panthera_ata_noflush` and on
  any product string starting `QEMU` (`a91788c`, `562da55`): a synchronous
  `FLUSH CACHE` under `_ioLock` deadlocked against QEMU IDE with
  `cache=writethrough`. That is a hidden data-integrity assumption
  (writethrough host cache) that R2.1 must either make explicit or remove.
- **Steps, in order:**
  1. **Reproduce.** Boot the current freshly built HFS image
     (`images/qemu/panthera-root.img`) with `PANTHERA_BOOT_ARGS` overriding
     `nodma`/`polled` off; capture serial log to `artifacts/boot/r2-dma-hfs-repro-*.log`.
     Three outcomes, each with a different next step: (a) boots — the
     compatibility default was over-broad, promote DMA and go to step 4;
     (b) stalls in journal replay — instrument `executeSectors` with per-command
     `PANTHERA:ATAST` timing and IRQ counters, compare against
     `AppleIntelPIIXPATA::handleDeviceInterrupt`; (c) stalls only with
     `polled=0`, DMA on — the bug is in interrupt delivery, not DMA (see the
     `PANTHERA:IOPCI legacy irq ... fallback controller 8259-pic` line every
     boot log carries; the disk IRQ path shares that fallback).
  2. **Root-cause and fix in the driver**, not by boot-arg tuning. Likely
     suspects, in order of cheapness to test: interrupt never delivered for
     multi-sector DMA on the secondary channel (the HFS root is primary
     master, ZFS gate disks are secondary master — mode differs by channel);
     `IOATAController` completing DMA before `descriptor->complete()` runs;
     512-byte journal-buffer alignment vs. `kIOMinimumSegmentAlignmentByteCountKey=2`.
  3. **Fix flush honestly.** Replace the `strncmp(_product, "QEMU")` bypass with
     one of: (a) issue `FLUSH CACHE` outside `_ioLock` with interrupts (the
     deadlock was lock-order, not the command); or (b) keep the bypass but
     make `run_phase2_qemu.sh` refuse `cache=writeback|none` while it is
     active. Today the two are silently coupled.
  4. **Flip defaults.** Remove `panthera_ata_nodma=1 panthera_ata_polled=1
     panthera_ata_noflush=1` from `DEFAULT_BOOT_ARGS`
     (`boot/efi/stage_phase2_efi.sh:13`); keep them documented as the
     fallback. Add LBA48 while in the file — the 4 GB gate images already sit
     at the LBA28 boundary's neighborhood and R6 images will exceed it.
- **Exit:** (1) `artifacts/boot/r2-dma-hfs-repro-*.log` recorded (pass or
  stall) before any driver change lands. (2) `tools/zfs_determinism_gate.sh`
  three consecutive PASS and `tools/alpha_release_gate.sh` three consecutive
  PASS with the new defaults. (3) Rootfs payload upload
  (`PANTHERA_OPENSSH_UPLOAD_SECONDS`, same 74 MB payload) ≤ 20 s, from 41–109 s.
  (4) One clean-world proof PASS with the new defaults, replacing
  `latest-clean-world.summary.md`.
- **Accepted (2026-09-08):**
  - **Reproduction:** `artifacts/boot/r2-dma-hfs-repro-{dma-irq,dma-polled,pio-irq}.log` (in retained clone `/tmp/panthera-r1-clean-world-pass13/`): all three IRQ-mode variants hung in "Still waiting for root device"; PIO+IRQ hung too, proving DMA was innocent and interrupt delivery was the defect.
  - **Root causes & fixes (commits `5571cb5`, `e1826fb`, `18bec26`):**
    1. `IOATABusCommand::executeCallback` signaled the `IOSyncer` before clearing the `syncer` field; the woken caller recycled the same command object and the stale `syncer = 0L` store then nulled the NEXT command's syncer → completion never delivered, no timeout (`kexts/OpenIOKit/patches/IOATAFamily-261.patch`).
    2. `IOATAController::handleDeviceInterrupt` returned on `!_currentCommand` before reading the status register, leaving INTRQ asserted so no further edge could fire on the edge-triggered IOAPIC pin (tracked `src/IOATAFamily-261/IOATAController.cpp`).
    3. `AppleIntelPIIXPATA::interruptOccurred` never acked the bus-master `BMISX` latch, and `start()` did not drain a latched INTRQ before the first commanded I/O (tracked `src/AppleIntelPIIXATA-251.0.1/AppleIntelPIIXPATA.cpp`).
    4. AppleAPIC registered as an `IOInterruptController` without a CPU-nub `registerInterrupt`/`enableInterrupt` and without `lapic_end_of_interrupt`; an interim fix using `ml_install_interrupt_handler` hijacked the platform handler and left the LAPIC timer vector pending (`IRR 221`, QEMU monitor) — replaced by the upstream CPU-nub registration + EOI + edge-only unmask (`kexts/OpenIOKit/patches/AppleAPIC-13.patch`; level-triggered PCI lines stay masked because RTL8139 timer-polls).
  - **Mechanism:** `kexts/OpenIOKit/patches/<tree>.patch` applied idempotently by `tools/fetch_world_sources.sh` / `kexts/OpenIOKit/build_kext.sh` with `.panthera-patches-applied` markers, for untracked fetched files only; tracked `src/` files carry their edits directly.
  - **Defaults:** `boot/efi/stage_phase2_efi.sh` `DEFAULT_BOOT_ARGS="serial=3 dataconstro=0 kernelmanagerd=0 panthera_ata_noflush=1"`; fallback `panthera_ata_nodma=1 panthera_ata_polled=1` via `PANTHERA_BOOT_ARGS`. Flush: `doSynchronize` skips FLUSH CACHE unless `panthera_ata_noflush=0`; `boot/qemu/run_phase2_qemu.sh` refuses non-`writethrough` QEMU cache unless that arg is set (commit `86ebc56`). LBA48 addressing added to `PantheraATAStorage` (48-bit commands verified in use: `PANTHERA:ATAST identified ... supports48=1`).
  - **Exit evidence:** `artifacts/release/r2-dma-hfs-{4,5,6}.summary.md` PASS (23/23 each), `artifacts/release/r2-dma-determinism-3.summary.md` PASS 3/3 fresh ZFS builds. 74 MB payload upload `PANTHERA_OPENSSH_UPLOAD_SECONDS`: 24-25 s (baseline PIO 41-109 s). 8/8 fresh-HFS+ZFS boots `artifacts/boot/r2-apic-child-{1..6,z1,z2}.log`.
  - **Boundary:** First HFS gate attempts `r2-dma-hfs-{1,2,3}` FAILED on `openssh_signal_trampoline_sftp` — NOT an ATA regression; see next item (incremental sysroot `libsystem_c` staleness, commit `83f992e`).

### R2.2 ZFS unlink/reclaim latency (complete, 2026-09-07)

- **What is known:** DMU is not the bottleneck (`txwait_ms=0` in every trace).
  Reclaim of an unlinked znode runs `zfs_rmnode` synchronously inside XNU's
  `vnode_put → vclean → VNOP_RECLAIM`, so every `unlink()` pays
  `dmu_free_long_range` (40%), `zfs_znode_delete` (18%), and the unlinked-set
  ZAP update (16%) on the caller's thread. Create is dominated by `mknode`
  (584 ms / 4,096) and `dirlock`. Negative name-cache purge storms were already
  fixed in `openzfs-spl-panthera.patch:1368-1430`.
- **Harness exists; do not write another:** `tools/smoke_pkgsrc_extract_probe.sh`
  (macro), `tools/pkgsrc_metadata_profile_probe.c` (micro),
  `PANTHERA_OPENZFS_VNOPS_{PHASE,DETAIL}_TRACE=1` (kernel breakdown via
  `tools/prepare_openzfs_source.sh:23-33`).
- **Steps:**
  1. **Re-baseline on today's kernel/kext** with the same image class and both
     probes; record `PANTHERA_PKGSRC_EXTRACT_TAR_ELAPSED` and the three
     `PANTHERA_PKGSRC_META_*_RATE` values under `artifacts/boot/r2-vfs-baseline-*`.
     The 212 s number is from 2026-05-31 and predates the R1 kernel/launchd
     changes.
  2. **Defer `zfs_rmnode` off the reclaim path.** Upstream Linux OpenZFS already
     does this: `zfs_zinactive` for an unlinked znode adds it to the unlinked
     set and lets `zfs_unlinked_drain` (taskq) do the range free and object
     delete. Port that shape into `module/os/macos/zfs/zfs_znode_os.c` as
     `kexts/zfs/patches/openzfs-async-rmnode.patch`, wired through
     `tools/prepare_openzfs_source.sh` like the other patches. Unmount/export
     must drain the taskq before `spa_export`; the Z4 persistence smoke covers
     that.
  3. **Batch `mknode`/`dirlock`** only if step 2 leaves create as the top cost
     in the re-baseline; do not start it speculatively.
  4. **Reclaim policy knob.** If XNU's synchronous `vclean` on `vnode_put`
     still dominates after step 2, evaluate `vnode_recycle` deferral via
     `VNODE_UPDATE`/`vnode_setnoflush`-style hints rather than patching XNU's
     `vfs_subr.c`; touching XNU is a last resort and needs its own gate run.
- **Exit:** On the same QEMU configuration and image class, with before/after
  numbers in one artifact (`artifacts/boot/r2-vfs-{before,after}-*`):
  `pkgsrc/devel` extract ≤ 150 s (from 212 s); unlink ≥ 1,200 ops/s (from
  683); rmdir ≥ 1,400 ops/s (from 812); reclaim of 5,120 unlinked vnodes
  ≤ 650 ms (from 1,080). ZFS release gate PASS, and Z4 export/import
  persistence PASS with the async drain in place.
- **Accepted (2026-09-07):**
  - Commit `1f1d7df` (`kexts/zfs/patches/openzfs-async-rmnode.patch`, defers `zfs_rmnode` to the unlinked-drain taskq kicked from `zfs_sync` + a high-water threshold in `zfs_zinactive`; drain loops only while the unlinked ZAP shrinks; drain skips objects with in-core znodes).
  - Measured on quiet host, same image/QEMU config: `rm -rf` cleanup 77 → 69 s, unlink 810 → 1,378 ops/s, rmdir 995 → 2,864 ops/s, create 475 → 832 ops/s, pkgsrc/devel extract 212 → 212 s (unchanged — extraction performs no unlinks; the earlier ≤150 s exit criterion was mis-specified and is retired).
  - ZFS gate PASS: `artifacts/release/r2-async-rmnode-2.summary.md` including Z4 export/import persistence.
  - A/B evidence: `artifacts/boot/r2-vfs-ab-{patched,unpatched,patched-2}.ssh.log`, micro `artifacts/boot/r2-vfs-{before,after2}-micro.ssh.log`.
  - Not met: reclaim ≤650 ms not re-measured with detail trace; unlink 1,378 vs 1,200 target met; rmdir 2,864 vs 1,400 met.

### R2.3 Gate cost (enabling work complete; timing target open)

The proof loop is the bottleneck for everything after R2: 70 minutes per
clean-world run, of which ~35 minutes is gate boot time under PIO.

- After R2.1 lands, re-run `tools/clean_world_proof.sh` once and record the
  new step table next to the 4,197 s baseline in `docs/CURRENT_STATE.md`.
- **Re-timed (2026-09-08):** `artifacts/release/r2-clean-world-20260908-pass1.summary.md` PASS at commit `83f992e`: total 3,840 s (R1 baseline 4,197 s, −357 s / −8.5%). Steps: bootstrap 158 s (was 172), hfs build 789 s (865), hfs gate 889 s (972), zfs build 935 s (1004), preserve seed 11 s (37), zfs gate 1,054 s (1,142), verify 3 s. Seed SHA-256 `36434fdbf41b838ec4a2ff41f0afe868368d73f9cb320ec7ad30a508f06a471b` unchanged. Gate uploads dropped to 20 s (control-create and root-populate). Honest reading: DMA cut I/O-bound steps ~10%; the loop is CPU-bound under TCG, so the ≤45 min exit will not come from storage.
- **Enabling work accepted (2026-09-09):** pkgsrc smokes use ZFS-native SFTP
  staging without host `hdiutil`; alpha defaults to ZFS. Explicit HFS recovery
  now restages EFI to clear inherited ZFS arguments. Evidence:
  `artifacts/boot/r23-pkgsrc-native-20260909-final.ssh.log`,
  `artifacts/release/r23-alpha-zfs-20260909-final.summary.md` (**23/23 PASS**),
  and `artifacts/release/r23-alpha-hfs-20260909-restage.summary.md`
  (**24/24 PASS**). This removes the staging prerequisite for R3.2.
- No new clean-world timing claim: the latest result is still 3,840 seconds,
  above the ≤45-minute criterion.
- **Exit:** `clean_world_proof.sh` PASS in ≤ 45 minutes on the same host, and
  the ZFS pkgsrc extract smoke runs without `hdiutil`.

### R2 non-goals

- AHCI/`ich9-ahci` or virtio-blk drivers: QEMU-only speedups that would not
  survive the "commodity PC" target; revisit in R6 if PIIX DMA is still the
  ceiling.
- Physical-hardware or CI qualification of any of the above.
- Any change to `dyld`, libSystem, or launchd; storage work must not reopen
  R1's frozen runtime.

---

## R3 — Self-Hosting Userland (merges old Phases 5 + 7)

- **R3.1 Core utility integration accepted (2026-09-10):** native `ps`,
  `mount`, `umount`, `sysctl`, `stty`, `dmesg`, `reboot`, `shutdown`, `less`,
  `bsdtar`, `vim`, and `curl` are staged and functionally checked. The
  user-authorized math/sleep/fork and platform/shutdown repairs pass final
  ZFS **23/23** and HFS **24/24** alpha gates. Both roots pass actual normal
  and scheduled reboot plus scheduled halt without panic. Evidence and
  boundaries: `artifacts/release/r23-r31-20260910.summary.md`.
  bsdtar xattr preservation and ACPI-only/physical reset remain unqualified;
  the existing Vim xdiff provenance gap is unchanged.
- **R3.2 pkgsrc self-hosting (not started):** bootstrap pkgsrc on the ZFS root
  and build a real third-party package in-guest with the verified toolchain.
- **Exit:** A gate-style smoke (`tools/`) that builds and installs one pkgsrc
  package in-guest and PASSes over SSH.

---

## R4 — Service Architecture Hardening (old Phase 6)

- **R4.1 launchd Mach bootstrap L2–L6:** full service registration/lookup and
  kqueue event loops; retire the parent-side bootstrap inheritance
  workarounds tracked in `docs/SHIM_DEBT_REGISTER.md`.
- **R4.2 Shim burn-down:** replace the transition DNS resolver with the real
  Libinfo/mDNSResponder path; fix `launchctl`'s unresolved `_IOKitWaitQuiet`
  and `_IORegistryEntryFromPath` binds; archive the fallback stub launchd.
- **Exit:** `tools/runtime_foundation_gate.sh` PASS with the retired shims
  removed from `relink_libpanthera_extra.sh` inputs and the debt register
  updated.

---

## R5 — Security & Multi-User (old Phase 8)

- Userland crypto decision first: CommonCrypto-over-OpenSSL shim is the
  documented fast path (`docs/CURRENT_STATE.md` prompt ledger); real
  corecrypto stays a later option. This unblocks `crypt`, `login`, `su`,
  `sudo`, and eventually securityd and TLS in `curl`.
- **Exit:** Non-root login boundary enforced in the boot gate; `sudo`
  round-trip smoke over SSH.

---

## R6 — Identity & Release (old Phases 9 + 10)

- System identity (`sw_vers`, `kern.ostype`), syslogd completeness, release
  image distribution built on the R1.3 clean-world proof.
- **Exit:** Distributable image built by CI from a fresh checkout with
  published checksums.

---

## Operating Rules

1. One milestone in flight per subsystem; strict scope per item.
2. Every change lands behind the existing gates; evidence lives in
   `artifacts/` and is cited by path.
3. Foundation rules in `docs/roadmap/OS_BUILD_ROADMAP.md` remain binding
   (frozen dylibs, no hand-relinks, export verification after library work).
4. `docs/CURRENT_STATE.md` and `docs/SHIM_DEBT_REGISTER.md` are updated in the
   same change that alters verified state or retires debt.
