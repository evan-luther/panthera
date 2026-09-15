# Panthera Current State

Status: Authoritative project status. Older status, shim, launchd, and roadmap documents are historical or planning material unless cited as evidence by this document.

## Overview & Architecture

Panthera is a standalone, bootable Darwin operating system constructed from Apple open-source components, continuing where PureDarwin left off. The system targets `x86_64` virtual machines under QEMU with EDK2/OVMF UEFI firmware.

### Architecture Stack

| Subsystem | Source Origin / Implementation | Status |
|---|---|---|
| **Target Machine** | `x86_64` under QEMU with EDK2/OVMF firmware | Stable boot baseline |
| **Kernel** | Apple XNU `xnu-10002.41.9` (macOS 14 Sonoma OSS) with Panthera platform, VFS, and driver bring-up patches | Builds via `build/build_xnu.sh`; boots to BSD init |
| **Bootloader** | Panthera custom UEFI loader (`boot/efi/loader/` -> `BOOTX64.EFI`) | Hands off kernel & stages boot kexts via XNU booter-kext ABI |
| **Driver Stack** | OpenIOKit drivers (`kexts/OpenIOKit/`), `PantheraATAStorage`, OpenZFS/SPL kexts, Apple HFS kexts | Builds via `kexts/OpenIOKit/build_all.sh` and kext scripts |
| **Runtime & LibSystem** | Custom `dyld` (`userland/dyld/panthera_dyld.cpp`), Libc (`Libc-1583.40.7`), libdispatch (`libdispatch-1462.0.4`), libmalloc (`libmalloc-474.0.13`), libsystem_info (`Libinfo-583.0.1`) | Rebuildable via sysroot scripts; exports verified clean |
| **Shared Cache** | Split-region dyld shared cache (`images/shared_cache/dyld_shared_cache_x86_64`) | Built via `tools/build_shared_cache.sh` |
| **PID 1 / Supervisor** | Apple `launchd-842.92.1` with Panthera plist runtime | Supervises system daemons from `/System/Library/LaunchDaemons/` |
| **Networking & Access** | Apple `configd` + `IPConfiguration` (DHCP on `en0`/RTL8139), OpenSSH `sshd`/`sftp` | Automated DHCP, DNS-over-TCP, remote SSH/SFTP on host port 2222 |
| **Userland & SDK** | `zsh-108`, BSD coreutils (`file_cmds-475`, `shell_cmds-326`, `text_cmds-197`), Panthera SDK | Native SDK & guest clang/cctools compiler toolchain verified |

### Repository Map

| Path | Ownership |
|---|---|
| `boot/` | UEFI loader, EFI staging, and QEMU boot harness |
| `src/` | Pinned Apple/upstream source trees with Panthera patches |
| `kexts/` | OpenIOKit, corecrypto, SPL, and ZFS kext build glue |
| `userland/` | Runtime, libSystem, launchd, services, tools, and SDK |
| `manifests/` | Declarative rootfs payload selection |
| `rootfs/` | Filesystem-neutral staging plus HFS/ZFS image adapters |
| `tools/` | World orchestration, audits, release gates, and probes |
| `docs/` | Current state, provenance, debt ledgers, and roadmaps |
| `artifacts/` | Ignored verification evidence and logs |
| `images/`, `BUILD/`, colocated `bin/`/`lib/`/`obj/` | Ignored generated outputs |

---

## Storage Architecture: Default ZFS Path vs Verified HFS Alpha Baseline

Panthera supports two root storage tracks with distinct roles:

### 1. Active ZFS Root Track (Current Default Development Path)
- **Role:** Primary active development and standard boot target.
- **Root Image:** `images/qemu/panthera-zfs-root.img` (GPT disk with ESP and ZFS pool `tank/ROOT/panthera`).
- **Builder:** `rootfs/create_zfs_root_image.sh` (consumes staged payload from `rootfs/create_rootfs_payload_tar.sh`).
- **Kernel / Kext Stack:** `net.lundman.spl` and `org.openzfsonosx.zfs`, both built from pinned OpenZFS Darwin sources through `kexts/zfs/build_spl.sh` and `kexts/zfs/build_zfs.sh`.
- **Userland Tools:** `/sbin/zpool`, `/sbin/zfs`, `/usr/sbin/zsysctl`, `libDiskArbitration.dylib` staged via `manifests/zfs.system`.
- **Default Status:** Active default in release gates, rootfs tooling, and storage integration work.
- **Native boot matching:** EFI staging uses `rootdev=uuid` and OpenZFS's dataset UUID; XNU waits for the published bootfs media instead of opening an unstable physical disk number. Native builders identify the source vdev through `zpool status -P`.

### 2. Verified HFS Alpha Baseline (Recovery / Compatibility Path)
- **Role:** Known-good reference baseline, historical release milestone, and recovery path.
- **Root Image:** `images/qemu/panthera-root.img` (2GB GPT raw HFS+ disk image).
- **Builder:** `rootfs/create_hfs_root_image.sh`.
- **Kernel / Kext Stack:** `HFS.kext` and `HFSEncodings.kext` built from Apple `hfs-650.0.2` sources.
- **Default Status:** Retained for compatibility, disaster recovery, and regression comparison; not the active default root.

---

## Verified Gate Summaries

### R1 Clean-World Acceptance (2026-09-06)

- **Result:** **PASS**, eight of eight clean-world steps, commit `b50c6691f8a8c120f2006d933cee912afe8c4225`, 4,197 seconds.
- **Generated summary:** `artifacts/release/r1-clean-world-20260905-pass13.summary.md` (historical R1 copy; superseded by R2 run in `artifacts/release/latest-clean-world.summary.md`).
- **HFS gate:** `artifacts/release/r1-clean-world-20260905-pass13-gate-hfs.summary.md` — 23 checks PASS, including the `sigsuspend` mask/handler regression, SSH/SFTP, networking, mDNS, SDK, and guest toolchain.
- **ZFS gate:** `artifacts/release/r1-clean-world-20260905-pass13-gate-zfs.summary.md` — 19 checks PASS; host OpenZFS audit intentionally skipped because the builder runs inside Panthera, not through host ZFS. Includes data-pool persistence, hybrid layout, fresh control/root creation, root `statfs` and writes, 32KB SFTP, mDNS, SDK, and guest toolchain.
- **Retained clean clone:** `/tmp/panthera-r1-clean-world-pass13`. Both full build graphs ran from bootstrapped repository sources; the ZFS seed was generated by this run, not copied from the historical recovery image.
- **Seed integrity:** SHA-256 before and after the gate: `ffd7276de330e283ccebd99a73fd0a9987c72cfee802a24b37fb9567179c6cae`.
- **R1 acceptance:** Earlier three consecutive fresh ZFS builds are recorded in `artifacts/release/r1-zfs-determinism-pass-20260828.summary.md`; pinned libunwind and the complete repository-only build are proven by this clean-world run. R1 is complete; R2 remains next.
- **Compatibility limits:** At R1 acceptance ATA used polled PIO; superseded by R2.1 below (polled PIO and explicit `panthera_ata_noflush=1` were compatibility defaults; QEMU root/data drives default to `cache=writethrough`). Gate boot images use temporary overlays, while builder output disks remain persistent. This is local QEMU evidence, not CI or physical-hardware qualification.

### R2.2 ZFS reclaim (2026-09-07)

- **Result:** **PASS**, commit `1f1d7df` (`kexts/zfs/patches/openzfs-async-rmnode.patch`), ZFS release gate `artifacts/release/r2-async-rmnode-2.summary.md`.
- **Mechanism:** Defers `zfs_rmnode` off the synchronous `vnode_put → vclean → VNOP_RECLAIM` path to the unlinked-drain taskq kicked from `zfs_sync` + high-water threshold in `zfs_zinactive`.
- **Measurements (quiet host, identical QEMU/image config):**
  - `rm -rf` cleanup: 77 s → 69 s
  - `unlink`: 810 → 1,378 ops/s (target ≥1,200 met)
  - `rmdir`: 995 → 2,864 ops/s (target ≥1,400 met)
  - `create`: 475 → 832 ops/s
  - `pkgsrc/devel` extract: 212 s → 212 s (unchanged; extraction does zero unlinks; ≤150 s exit criterion retired as mis-specified)
- **Persistence & Gate:** Z4 secondary data-pool export/import persistence and full ZFS root bootstrap/runtime gate verified PASS. Evidence: `artifacts/release/r2-async-rmnode-2.summary.md`, `artifacts/boot/r2-vfs-ab-{patched,unpatched,patched-2}.ssh.log`.

### R2.1 ATA DMA + interrupts (2026-09-08)

- **Result:** **PASS**, commits `5571cb5`, `e1826fb`, `18bec26`, `86ebc56`. Release gates `artifacts/release/r2-dma-hfs-{4,5,6}.summary.md` (23/23 PASS each), determinism `artifacts/release/r2-dma-determinism-3.summary.md` (3/3 fresh ZFS builds PASS).
- **Reproduction:** `artifacts/boot/r2-dma-hfs-repro-{dma-irq,dma-polled,pio-irq}.log` (in retained clone `/tmp/panthera-r1-clean-world-pass13/`): all three IRQ-mode variants hung in "Still waiting for root device"; PIO+IRQ hung too, proving DMA was innocent and interrupt delivery was the defect.
- **Root Causes & Fixes:**
  1. `IOATABusCommand::executeCallback` signaled `IOSyncer` before clearing the `syncer` field; caller command reuse allowed stale `syncer = 0L` to null the subsequent command's syncer (`kexts/OpenIOKit/patches/IOATAFamily-261.patch`).
  2. `IOATAController::handleDeviceInterrupt` returned on `!_currentCommand` before reading the status register, leaving INTRQ asserted and edge-triggered IOAPIC pin blocked (`src/IOATAFamily-261/IOATAController.cpp`).
  3. `AppleIntelPIIXPATA::interruptOccurred` never acked the bus-master `BMISX` latch, and `start()` did not drain a latched INTRQ before the first commanded I/O (`src/AppleIntelPIIXATA-251.0.1/AppleIntelPIIXPATA.cpp`).
  4. AppleAPIC registered as an `IOInterruptController` without CPU nub registration and `lapic_end_of_interrupt`; interim `ml_install_interrupt_handler` hook hijacked the platform handler and left the LAPIC timer vector pending (`IRR 221`, QEMU monitor); replaced by upstream CPU-nub registration + EOI + edge-only unmask (`kexts/OpenIOKit/patches/AppleAPIC-13.patch`; level-triggered PCI lines stay masked because RTL8139 timer-polls).
- **Configuration & Storage:** `boot/efi/stage_phase2_efi.sh` sets `DEFAULT_BOOT_ARGS="serial=3 dataconstro=0 kernelmanagerd=0 panthera_ata_noflush=1"`; polled PIO remains fallback via `panthera_ata_nodma=1 panthera_ata_polled=1`. Flush: `doSynchronize` skips FLUSH CACHE unless `panthera_ata_noflush=0`; `boot/qemu/run_phase2_qemu.sh` enforces `cache=writethrough` (commit `86ebc56`). LBA48 addressing added to `PantheraATAStorage` (48-bit commands verified in use: `PANTHERA:ATAST identified ... supports48=1`).
- **Measurements & Boots:** 74 MB rootfs payload SFTP upload reduced from 41–109 s (PIO) to 24–25 s (`PANTHERA_OPENSSH_UPLOAD_SECONDS`). 8/8 fresh-HFS+ZFS boots verified in `artifacts/boot/r2-apic-child-{1..6,z1,z2}.log`.
- **Boundary:** Initial HFS gate runs `r2-dma-hfs-{1,2,3}` failed on `openssh_signal_trampoline_sftp` due to an incremental sysroot `libsystem_c` staleness defect (commit `83f992e`), not an ATA regression.

### R2.3 ZFS alpha-default verification (2026-09-09)

- **Default alpha gate:** **PASS**, 23/23 checks, using
  `tools/alpha_release_gate.sh --tag r23-alpha-zfs-20260909-final --timeout 900 --port 2227`
  without a `--root-kind` override.
- **Evidence:** `artifacts/release/r23-alpha-zfs-20260909-final.summary.md`.
  The gate rebuilt ZFS control/root images through the native guest builder
  and passed boot, SSH signal/exit/SFTP, mDNS, SDK, and guest toolchain checks.
- **ZFS-native pkgsrc extraction:** **PASS**,
  `artifacts/boot/r23-pkgsrc-native-20260909-final.ssh.log`.
  SFTP staged the archive in 29 seconds, without host `hdiutil`. Extraction
  of `pkgsrc/devel` accounted for all 47,057 entries; reported extraction
  time was 241 seconds (30-second polling), cleanup 76 seconds, pool sync
  6 seconds. This is a staging/functional check, not a new performance claim.
  Command: `PANTHERA_ROOT_DISK_SNAPSHOT=on bash tools/smoke_pkgsrc_extract_probe.sh --root-kind zfs --member pkgsrc/devel --cleanup --timeout 900 --remote-timeout 1200 --port 2227 --tag <tag>`.
- **Recovery cutover defect found:** the first explicit HFS run inherited
  ZFS EFI boot arguments and waited for the ZFS boot UUID
  (`artifacts/boot/r23-alpha-hfs-20260909-final-boot.log`); it was stopped.
  The alpha gate now explicitly stages HFS EFI before recovery smokes.
  Low-level data/hybrid smokes retain their caller-provided staging.
- **Explicit HFS recovery:** **PASS**, 24/24 checks,
  `artifacts/release/r23-alpha-hfs-20260909-restage.summary.md`.
- **Timing scope:** This verifies the default cutover, not the R2.3 clean-world
  target. The latest full clean-world result remains 3,840 seconds; the
  ≤45-minute exit criterion is still unmet.

### R3.1 Core utility integration (2026-09-10)

- **Accepted core utility scope:** native `ps`, `mount`, `umount`, `sysctl`,
  `stty`, `dmesg`, `reboot`, `shutdown`, `less`, `bsdtar`, `vim`, and `curl`.
  Added pinned `ps`/`stty`/`shutdown`/`mount` sources and built the existing
  OpenZFS `mount_zfs` helper. Required payload entries reject missing binaries.
- **Runtime repairs:** user-authorized native `pow`, sleep/nanosleep,
  cancellation, and fork-child Mach/pthread initialization fixes. Scalar
  LLVM power sources are vendored with verified hashes/license. Vim links
  against the native sysroot rather than host SDK math exports.
- **Orderly shutdown/reset:** reboot and halt request launchd's existing
  root-bootstrap RPC; only explicit `reboot -q` takes the raw syscall path.
  The real platform expert now wins over the generic fallback, respects
  immutable registry ownership, distinguishes PCI/ISA IRQ semantics, and
  implements the legacy i8042 reset.
- **Final alpha gates:** **ZFS 23/23 PASS**, **HFS 24/24 PASS**:
  `artifacts/release/r31-final-zfs-20260910.summary.md` and
  `artifacts/release/r31-final-hfs-20260910.summary.md`.
  Both include three successful native primitive probes over SSH.
- **Final staged-image utility smoke:** **PASS**, with no uploaded binaries,
  `artifacts/boot/r31-final-utilities-20260910.ssh.log`. Checks include process
  selection/listing, tty save/change/restore, Vim floating-point math/curl,
  plain-file archive extraction/pager output, shutdown validation/warn-only
  mode, failed mount helpers, and legacy ZFS mount/write/unmount/remount.
- **Actual lifecycle proof:** **PASS on ZFS and HFS**,
  `artifacts/boot/r31-reset-lifecycle-{zfs,hfs}-20260910.log`.
  Each run has three EFI boots: normal reboot, scheduled reboot, then
  scheduled halt reaching `CPU halted`; no kernel panic.
- **Regression commands:** `bash tools/smoke_system_cmds.sh --root-kind zfs --timeout 900 --port 2227 --tag <tag>`;
  `bash tools/smoke_reboot.sh --root-kind <zfs|hfs> --restage --port 2227 --tag <tag>`.
  Both use disposable root-disk overlays.
- **Boundaries:** `ps` omits unsupported `prsna`. The existing bsdtar
  `Could not check extended attributes` warning is retained; xattr
  preservation is not qualified. Reset is qualified only on the QEMU
  legacy-PC path, not ACPI-only/physical hardware. The previously documented
  Vim xdiff provenance gap remains. R3.2 has not started.
- Full evidence/failure history: `artifacts/release/r23-r31-20260910.summary.md`.
  Source/license adaptations: `docs/provenance/APPLE_PATCHES.md` and
  `docs/provenance/THIRD_PARTY.md`.

### Recovery Integration Verification (2026-08-28)

- **HFS release gate:** `artifacts/release/unify-20260828-hfs-verified.summary.md` is **PASS**. It rebuilt the HFS root image and passed EFI/XNU/HFS boot, launchd, configd/IPConfiguration DHCP and DNS, three OpenSSH signal/exit paths, SFTP, mDNS, SDK, and guest toolchain checks.
- **ZFS data and hybrid gates:** `artifacts/release/unify-20260828-zfs-verified.summary.md` passed provenance, builds, Z4 data-pool persistence, Z5 hybrid layout, mDNS, SDK, and guest toolchain checks. Its first freshly-created ZFS control image hit the known nondeterministic PID 1 `SIGUSR1` compatibility failure before SSH became reachable.
- **ZFS root retry:** `artifacts/release/unify-20260828-zfs-root-retry.summary.md` is **PASS** using the generated control/root images. It passed native ZFS root bootstrap, `statfs`, 32KB SFTP upload, mDNS, SDK, and guest toolchain checks.
- **Boot compatibility mode:** Polled PIO (`panthera_ata_nodma=1 panthera_ata_polled=1`) is now an explicit fallback mode via `PANTHERA_BOOT_ARGS`. R2.1 established DMA and IOAPIC interrupts as the default.

### 1. Verified HFS Alpha Release Gate
- **Primary Artifact:** `artifacts/release/alpha-release-20260521-weak-coalesce-r16.summary.md`
- **Result:** **PASS** (16/16 checks passing on 2026-05-21)
- **Verified Checks & Evidence:**
  - `build_ssh_exit_probes`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.build_ssh_exit_probes.log`)
  - `build_mdns_dns_sd_probe`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.build_mdns_dns_sd_probe.log`)
  - `build_libiconv`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.build_libiconv.log`)
  - `build_apple_ncurses_target`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.build_apple_ncurses_target.log`)
  - `build_openssh`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.build_openssh.log`)
  - `diagnostics_audit`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.diagnostics_audit.log`)
  - `build_shared_cache`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.build_shared_cache.log`)
  - `rebuild_rootfs`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.rebuild_rootfs.log`)
  - `fallback_audit`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.fallback_audit.log`)
  - `boot_verify`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.boot_verify.log`)
  - `openssh_return_exec_sftp`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.openssh_return.log`)
  - `openssh_pselect_sigchld_sftp`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.openssh_pselect.log`)
  - `openssh_signal_trampoline_sftp`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.openssh_signal.log`)
  - `mdns_dns_sd_over_openssh_sftp`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.mdns.log`)
  - `panthera_sdk_smoke`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.panthera_sdk.log`)
  - `guest_toolchain_smoke`: PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.guest_toolchain.log`)

### 2. Verified ZFS Storage Release Gate
- **Primary Artifact:** `artifacts/release/zfs-full-post-upload-20260523-r2.summary.md`
- **Result:** **PASS** (20/20 checks passing on 2026-05-23)
- **Scope & Verified Milestones:**
  - **Z4 (Secondary Data Pool):** `zpool create`, mount on `/z`, write/read, umount, and `zpool export` verified (`zfs_data_pool_smoke.log`).
  - **Z5 (Hybrid Layout):** Verified writable-state layout with ZFS secondary pool (`zfs_hybrid_layout_smoke.log`, `zfs_hybrid_mdns.log`, `zfs_hybrid_sdk.log`, `zfs_hybrid_guest_toolchain.log`).
  - **Z6/Z7 (ZFS Root Bootstrap & Runtime):** Complete native ZFS root boot (`/` on `zfs`), launchd PID 1 bootstrap, login, statfs verification, 32KB SFTP upload under RTL8139 interrupt mode, mDNS, Panthera SDK, and guest toolchain (`zfs_root_bootstrap_smoke.log`, `zfs_root_statfs_ssh.log`, `zfs_root_default_32k_upload.log`, `zfs_root_mdns.log`, `zfs_root_sdk.log`, `zfs_root_guest_toolchain.log`).

### 3. Latest Storage Performance Findings (Pkgsrc on Fresh ZFS Root)
- Extraction of `pkgsrc/devel` (47,057 entries) on a fresh ZFS root (`images/qemu/panthera-zfs-root-fresh-meta-20260531-r1.img`) completed in 212 seconds using optimized tar flags (`--no-xattrs --no-acls --no-mac-metadata --no-same-owner --numeric-owner`).
- Kernel VFS and OpenZFS phase trace evidence (`artifacts/boot/zfs-vfs-meta-detail-meta-5k-20260531-r1.ssh.log`, `artifacts/boot/zfs-vnops-detail-meta-5k-20260531-r1.ssh.log`) showed OpenZFS core transactions are fast (`txwait_ms=0`), narrowing remaining performance work to Darwin VFS vnode lifecycle and unlink/rmdir overhead under TCG emulation.

---

## Canonical Build Entrypoint & Clean-Build Status

### Canonical Build Entrypoint: `tools/build_world.sh`

`tools/build_world.sh` is the canonical entrypoint for building Panthera. It orchestrates existing subsystem scripts in topological order rather than duplicating compilation logic:

1. **`provenance`**: Verify third-party checksums (`tools/verify_third_party_checksums.sh`) and validate toolchain inputs.
2. **`xnu`**: Compile XNU kernel (`build/build_xnu.sh`).
3. **`efi`**: Compile UEFI bootloader (`boot/efi/build_bootx64.sh`).
4. **`kexts`**: Build OpenIOKit and ATA kexts (`kexts/OpenIOKit/build_all.sh`) plus SPL/ZFS (`kexts/zfs/build_spl.sh`, `kexts/zfs/build_zfs.sh`) for ZFS targets.
5. **`runtime`**: Build the custom `dyld`, relink libSystem, build `launchd`, build the dyld shared cache, and verify exports.
6. **`userland`**: Build the Panthera SDK and manifest-selected components through `rootfs/scripts/build_components.sh`.
7. **`rootfs`**: Stage manifest-neutral rootfs payload tree and archive via `rootfs/create_rootfs_tree.sh` and `rootfs/create_rootfs_payload_tar.sh`.
8. **`image`**: Construct root disk images (`rootfs/create_zfs_root_image.sh` for default ZFS, `rootfs/create_hfs_root_image.sh` for HFS recovery).
9. **`gate`**: Execute release verification gates (`tools/zfs_release_gate.sh`, `tools/alpha_release_gate.sh`, or `tools/boot_verify.sh`).

The entrypoint supports dry-run inspection (`--dry-run`), phase listing (`--list`), bounded phase selection (`--phase <name>`, `--from-phase <name>`, `--to-phase <name>`), and fails fast. Gates require explicit `--gate`; ZFS image modes that boot a builder require `--enable-qemu`.

### Development Workflow

```bash
# Inspect the graph without changing outputs.
tools/build_world.sh --list
tools/build_world.sh --dry-run --target hfs
tools/build_world.sh --dry-run --target zfs

# Rebuild a bounded subsystem and everything after it.
tools/build_world.sh --from-phase efi --to-phase rootfs --target hfs

# Build images. ZFS control-image construction boots QEMU and requires opt-in.
tools/build_world.sh --phase image --target hfs
tools/build_world.sh --phase image --target zfs --enable-qemu

# Run authoritative release gates and retain evidence under artifacts/.
tools/alpha_release_gate.sh --tag <zfs-alpha-tag> --timeout 900
tools/alpha_release_gate.sh --root-kind hfs --tag <hfs-recovery-tag> --timeout 900
tools/zfs_release_gate.sh --tag <zfs-tag> --timeout 360
```

Source changes belong in the owning subsystem, manifests define payload membership, and image builders consume the shared staged payload contract. Generated binaries, disk images, staging trees, and gate logs remain ignored; preserve verification evidence under `artifacts/` rather than committing it.

### Clean World Build Status

- **Status:** **Repository-only clean world PASS** on 2026-09-08 at `83f992e`, 3,840 seconds (`artifacts/release/r2-clean-world-20260908-pass1.summary.md`; identical latest copy at `artifacts/release/latest-clean-world.summary.md`). Prior R1 baseline: 4,197 seconds on 2026-09-06 at `b50c669` (`artifacts/release/r1-clean-world-20260905-pass13.summary.md`).
- **Scope:** Fresh clone, source bootstrap, full HFS and ZFS builds, both release gates, and immutable generated seed verification, without manual intervention within the successful run.
- **Runtime sources:** LLVM libunwind 18.1.8 and its CMake/runtime companions are checksum-fetched; `PANTHERA_LLVM_ROOT` is an optional local override, not a prerequisite.
- **Regression command:** From a clean committed checkout, run `bash tools/clean_world_proof.sh --tag <tag> --retained-temp-dir /tmp/<fresh-clone> --boot-timeout 900 --remote-timeout 900`. Preserve the generated summary and nested gate evidence. Earlier failed runs remain failures; the PASS does not rewrite their results.
- **Incremental sysroot hygiene (commit `83f992e`):** `sigsuspend(const sigset_t*)` must dereference before syscall 111 (mask by value in `%edi`); the shim from `5e65da2` is correct and was built by the clean-world run. Main's incremental sysroot had a stale untracked `userland/libsystem/build/sysroot/usr/include/xpc/` (April) shadowing the SDK `<xpc/base.h>`, causing `build_libsystem_c.sh` to fail and leaving `libsystem_c.dylib` stuck on a May 23 binary without the fix. `runtime_foundation_gate.sh` relinks without recompiling constituents, masking this. Fixed by purging that directory in `build_libsystem_c.sh`/`build_libsystem.sh` and rebuilding. **Lesson:** `runtime_foundation_gate.sh` is not a substitute for `tools/build_world.sh --phase runtime`. Bisect evidence: `artifacts/boot/r2-sig-{clone-baseline,main-staging,main-image}.log` (2×2 matrix: image, not staging).

---

## Host & Toolchain Dependencies

Panthera builds on a macOS host environment with the following dependencies:

| Category | Required Tools | Purpose |
|---|---|---|
| **Host Operating System** | macOS (Sonoma 14+ or compatible Darwin workstation) | Host build environment |
| **Toolchain & Compilers** | Xcode Command Line Tools (`clang`, `clang++`, `ld`, `xcrun`, `mig`, `make`, `ar`, `ranlib`, `lipo`) | Building Mach-O binaries, kernels, dylibs, and MIG stubs |
| **Virtualization & Emulation** | QEMU (`qemu-system-x86_64`, `qemu-img`), EDK2/OVMF x86_64 UEFI firmware | Guest emulation and boot testing |
| **Host Storage Tools** | `hdiutil`, `diskutil` | Creating, attaching, partitioning, and formatting raw HFS disk images |
| **Build & Scripting Utilities** | `bash` (4+), `ruby`, `python3`, `perl`, `tar`, `curl`, `patch` | Executing build scripts, manifests, and checksum validations |
| **Guest Toolchain** | Panthera SDK (`userland/panthera_sdk/Panthera.sdk`), `panthera-cc`, guest clang/cctools | In-guest and cross compilation |
| **Runtime Source Intake** | Checksum-fetched LLVM libunwind 18.1.8 plus CMake/runtime companions | Rebuilds `libunwind.a` and register-save/restore objects; `PANTHERA_LLVM_ROOT` is optional |

---

## Generated Artifact Policy

- **Untracked Policy:** All build outputs, compiler object trees (`BUILD/`, `build/obj/`, `build/dst/`, `build/sym/`), staging areas (`boot/efi/staging/`, `userland/libsystem/build/sysroot/`), shared caches (`images/shared_cache/`), and generated disk images (`images/qemu/*.img`) are generated artifacts and must remain ignored by git.
- **Colocated Artifacts:** Where existing scripts require colocated outputs during build stages, those outputs must be ignored via `.gitignore` and never committed as source.
- **Source Review Separation:** Source audits and code reviews must focus strictly on authored source code, upstream Apple source trees, manifests (`manifests/*.system`), and build scripts.
- **Evidence Retention:** Test logs, boot verification transcripts, and gate summaries must be stored under `artifacts/` and cited by exact repository path.

---

## Document Authority & Navigation

- **Authoritative Present State:** `docs/CURRENT_STATE.md` (this document)
- **Historical Background:** `docs/STATUS.md`
- **Shim & Technical Debt:** `docs/SHIM_DEBT_REGISTER.md`
- **Provenance & Reproducibility:**
  - `docs/provenance/PROVENANCE.md` (Assembly model and component inventory)
  - `docs/provenance/GENERATED_ARTIFACTS.md` (Classification and artifact ledger)
  - `docs/provenance/REPRODUCIBILITY.md` (Reproducibility gaps and clean-build requirements)
  - `docs/provenance/APPLE_PATCHES.md` (Ledger of modifications to Apple OSS code)
  - `docs/provenance/THIRD_PARTY.md` (Third-party dependencies and intake models)
  - `docs/provenance/THIRD_PARTY_CHECKSUMS.txt` (Pinned SHA256 checksums)
- **Roadmaps & Future Planning:**
  - `docs/roadmap/DEVELOPMENT_ROADMAP.md` (Authoritative next-milestone sequence)
  - `docs/roadmap/OS_BUILD_ROADMAP.md` (Overall OS build roadmap)
  - `docs/roadmap/ZFS_INTEGRATION_PLAN.md` (ZFS root migration plan)
  - `docs/roadmap/AUTHENTIC_DARWIN_PLAN.md` (Authentic Apple Darwin alignment plan)
  - `docs/roadmap/XNU_UPGRADE_PLAN.md` (Kernel upgrade roadmap)
- **Non-Authoritative Documents:** One-off prompts, scratch notes, and model-runner configs (e.g., `docs/LOCAL_PI_QWEN35_HERETIC_MLX.md`, `docs/*PROMPT.md`, `docs/*TASK.md`) are reference or transient task materials and are non-authoritative.
