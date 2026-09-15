# Panthera Reproducibility Plan

> **Status: PROVENANCE & REPRODUCIBILITY LEDGER.**
> For the authoritative present-state summary, see **`docs/CURRENT_STATE.md`**.
> For source assembly and component origins, see **`docs/provenance/PROVENANCE.md`**.
> For the generated-artifact policy, see **`docs/provenance/GENERATED_ARTIFACTS.md`**.

---

## Purpose

This document defines the intended path from a clean checkout to a bootable Panthera guest and records the current engineering state and gaps affecting reproducible builds.

---

## Verified Baseline Evidence

Panthera has verified release gates for both its recovery baseline and active development storage paths:

### 1. Verified HFS Alpha Release Gate (Recovery Baseline)
- **Primary Summary:** `artifacts/release/alpha-release-20260521-weak-coalesce-r16.summary.md` (PASS on 2026-05-21; 16/16 checks passing)
- **Verified Capabilities:**
  - EFI handoff, XNU kernel start, raw HFS+ root mount on `images/qemu/panthera-root.img`.
  - `/sbin/launchd` as PID 1, plist job supervision, login prompt, root shell prompt (`/bin/zsh`).
  - Dynamic symbol exports clean (`verify_exports.sh`), bootstrap Mach IPC smoke (`test_bootstrap_simple`).
  - `configd` + `IPConfiguration` automated DHCP (`10.0.2.15`), default gateway ping, DNS-over-TCP.
  - OpenSSH host access on `127.0.0.1:2222`, remote command execution, SFTP file transfer.
  - `pselect()` / `SIGCHLD` signal handling and signal trampoline delivery.
  - `mDNSResponder` / `dns-sd` registration and query smoke over SSH.
  - In-guest Panthera SDK and guest `clang`/`cctools` compilation smoke tests.

### 2. Verified ZFS Storage Release Gate (Active Default Path)
- **Primary Summary:** `artifacts/release/zfs-full-post-upload-20260523-r2.summary.md` (PASS on 2026-05-23; 20/20 checks passing)
- **Verified Capabilities:**
  - OpenZFS SPL (`net.lundman.spl`) and ZFS (`org.openzfsonosx.zfs`) kext loading.
  - Native `/sbin/zpool`, `/sbin/zfs`, `/usr/sbin/zsysctl`, and `libDiskArbitration.dylib` execution.
  - Z4 secondary data pool creation, mounting on `/z`, persistence across reboot, and clean export.
  - Z5 hybrid writable-state layout with `/var`, `/tmp`, `/Users`, and `/Library` on ZFS datasets.
  - Z6/Z7 native ZFS root bootstrap (`/` on `zfs` pool `tank/ROOT/panthera`) on `images/qemu/panthera-zfs-root.img`.
  - 32KB SFTP upload under RTL8139 hardware interrupt mode, mDNS, Panthera SDK, and guest toolchain on ZFS root.

### 3. Recovery Integration Evidence (2026-08-28)
- `artifacts/release/unify-20260828-hfs-verified.summary.md`: complete HFS release gate PASS after rebuilding the root image.
- `artifacts/release/unify-20260828-zfs-verified.summary.md`: Z4/Z5 storage, mDNS, SDK, and toolchain checks PASS; the first freshly-created ZFS control-image boot failed on the nondeterministic PID 1 signal-30 path.
- `artifacts/release/unify-20260828-zfs-root-retry.summary.md`: ZFS root bootstrap, `statfs`, 32KB SFTP, mDNS, SDK, and toolchain checks PASS on the immediate reuse retry.

### 4. Third-Party Input Checksum Verification
- Command:
  ```bash
  tools/verify_third_party_checksums.sh
  ```
- Result: 24 files checked, 0 failed, 0 missing (against `docs/provenance/THIRD_PARTY_CHECKSUMS.txt`).
- Fetch-at-build scripts for Bash, bmake, cctools, curl, Nano, OpenSSH, OpenSSL, and zlib enforce checksum validation via `tools/fetch_with_checksum.sh` before archive extraction.

### 5. R1 Clean-World Acceptance (2026-09-06)

- `artifacts/release/r1-clean-world-20260905-pass13.summary.md`: **PASS**,
  commit `b50c6691f8a8c120f2006d933cee912afe8c4225`, eight steps, zero failures,
  4,197 seconds.
- Fresh source bootstrap, full HFS build and gate, full ZFS build and gate,
  and generated seed integrity all passed in one command.
- Gate details: `artifacts/release/r1-clean-world-20260905-pass13-gate-hfs.summary.md`
  and `artifacts/release/r1-clean-world-20260905-pass13-gate-zfs.summary.md`.
- Retained clone: `/tmp/panthera-r1-clean-world-pass13`.
- Seed SHA-256 before and after:
  `ffd7276de330e283ccebd99a73fd0a9987c72cfee802a24b37fb9567179c6cae`.
- Native ZFS boots use the published bootfs UUID media. Gate boot disks use
  temporary QEMU overlays; native builders preserve their input seed and
  persist only the output disk. ATA remains polled PIO with explicit flush
  bypass and QEMU writethrough caching.
- This proves the local macOS/QEMU path, not CI, DMA, or physical hardware.


### 6. R2 Clean-World Acceptance (2026-09-08)

- `artifacts/release/r2-clean-world-20260908-pass1.summary.md`: **PASS**,
  commit `83f992e`, eight steps, zero failures, 3,840 seconds (−357 s / −8.5% vs R1 baseline 4,197 s).
- Fresh source bootstrap, full HFS build and gate, full ZFS build and gate,
  and generated seed integrity all passed in one command.
- Steps: bootstrap 158 s (was 172), hfs build 789 s (865), hfs gate 889 s (972),
  zfs build 935 s (1004), preserve seed 11 s (37), zfs gate 1,054 s (1,142), verify 3 s.
- Gate details: `artifacts/release/r2-clean-world-20260908-pass1-gate-hfs.summary.md`
  and `artifacts/release/r2-clean-world-20260908-pass1-gate-zfs.summary.md`;
  latest summary at `artifacts/release/latest-clean-world.summary.md`.
- Seed SHA-256 before and after:
  `36434fdbf41b838ec4a2ff41f0afe868368d73f9cb320ec7ad30a508f06a471b`.
- Native ZFS and HFS release gates ran with ATA DMA and IOAPIC interrupt defaults;
  gate SFTP uploads completed in 20 s (control-create and root-populate). Flush
  bypass explicit via `panthera_ata_noflush=1` with QEMU `cache=writethrough` enforced.
- This proves the full repository-only clean world build and dual release gates
  with ATA DMA and interrupts active by default.
---

## Host & Toolchain Dependencies

Building and validating Panthera requires a macOS host with:

| Dependency | Required Tools | Role |
|---|---|---|
| **Host OS** | macOS Sonoma 14+ or compatible Darwin environment | Host build platform |
| **Toolchain** | Xcode Command Line Tools (`clang`, `ld`, `xcrun`, `mig`, `make`, `ar`, `ranlib`, `lipo`) | Building kernels, dylibs, binaries, and MIG headers |
| **Virtualization** | QEMU (`qemu-system-x86_64`, `qemu-img`), EDK2/OVMF x86_64 UEFI firmware | Guest VM emulation and automated boot testing |
| **Host Disk Tools** | `hdiutil`, `diskutil` | Partitioning, formatting, and mounting raw disk images |
| **Utilities** | `bash` (4+), `ruby`, `python3`, `perl`, `tar`, `curl`, `patch` | Script execution and manifest verification |

---

## Canonical Build System: `tools/build_world.sh`

`tools/build_world.sh` is the canonical entrypoint to orchestrate Panthera's build graph in topological order:

1. **`provenance`**: Verify third-party checksums (`tools/verify_third_party_checksums.sh`).
2. **`xnu`**: Build XNU kernel (`build/build_xnu.sh`).
3. **`efi`**: Build UEFI bootloader (`boot/efi/build_bootx64.sh`).
4. **`kexts`**: Build OpenIOKit kexts (`kexts/OpenIOKit/build_all.sh`) and SPL/ZFS kexts (`kexts/zfs/build_spl.sh`, `kexts/zfs/build_zfs.sh`) for ZFS targets.
5. **`runtime`**: Build the custom `dyld`, relink libSystem, build `launchd`, generate the dyld shared cache, and verify exports.
6. **`userland`**: Build the Panthera SDK and manifest-selected components through `rootfs/scripts/build_components.sh`.
7. **`rootfs`**: Stage manifest-neutral rootfs payload tree and archive (`rootfs/create_rootfs_tree.sh`, `rootfs/create_rootfs_payload_tar.sh`).
8. **`image`**: Construct root disk images (`rootfs/create_zfs_root_image.sh` for default ZFS, `rootfs/create_hfs_root_image.sh` for HFS recovery).
9. **`gate`**: Execute release verification gates (`tools/zfs_release_gate.sh`, `tools/alpha_release_gate.sh`, `tools/boot_verify.sh`).

The entrypoint supports dry-run inspection (`--dry-run`), phase listing (`--list`), bounded phase selection (`--phase <name>`, `--from-phase <name>`, `--to-phase <name>`), and fails fast. Gates require explicit `--gate`; ZFS image modes that boot a builder require `--enable-qemu`.

---

## Current Reproducibility Gaps

| Gap | Impact | Current Status / Remediation |
|---|---|---|
| **Clean World Build CI Proof** | CI has not yet been qualified | Local clean-world acceptance passed at `b50c669`; use the same `tools/clean_world_proof.sh` regression command in CI. |
| **Runtime Source Intake** | Host-local LLVM source is no longer required | Pinned libunwind 18.1.8 and CMake/runtime companions are checksum-fetched; both full clean-world runtime builds passed without `PANTHERA_LLVM_ROOT`. |
| **ZFS Control First-Boot Determinism** | R1 signal-race acceptance is closed | `r1-zfs-determinism-pass-20260828.summary.md` records three consecutive fresh builds; the clean-world PASS independently built its own seed, control, and root images. |
| **Panthera ATA DMA / Flush Reliability** | Closed in R2.1 (2026-09-08) | DMA and IOAPIC interrupts are now default; 3× gates PASS (`artifacts/release/r2-dma-hfs-{4,5,6}.summary.md`, `artifacts/release/r2-dma-determinism-3.summary.md`). Flush bypass explicit via `panthera_ata_noflush=1`; QEMU writethrough cache enforced by `run_phase2_qemu.sh` (commit `86ebc56`). Polled PIO preserved as fallback. |
| **Incremental sysroot hygiene** | Stale headers in incremental sysroot can shadow SDK and prevent constituent dylib recompilation | Fixed in commit `83f992e`. Untracked `xpc/` headers in `userland/libsystem/build/sysroot/usr/include/` shadowed SDK `<xpc/base.h>`, causing `build_libsystem_c.sh` to fail and leaving `libsystem_c.dylib` at a May 23 binary missing the `sigsuspend` fix (`5e65da2`). `runtime_foundation_gate.sh` relinks without recompiling and masked this. Fixed by purging stale headers in `build_libsystem_c.sh`/`build_libsystem.sh`. `runtime_foundation_gate.sh` is not a substitute for `tools/build_world.sh --phase runtime`. Bisect: `artifacts/boot/r2-sig-{clone-baseline,main-staging,main-image}.log`. |
| **Colocated Generated Outputs** | Some build scripts deposit intermediate objects near source directories | Colocated outputs are ignored in `.gitignore`; `docs/provenance/GENERATED_ARTIFACTS.md` establishes untracked policy. |
| **Rootfs Payload Decoupling** | Historical rootfs assembly mixed component compilation with image creation | `rootfs/create_rootfs_tree.sh` and `rootfs/create_rootfs_payload_tar.sh` decouple payload staging from disk image creation. |
| **Root Image Mutability** | Direct VM boots can modify disk images | Release gates use temporary boot overlays; builders persist output disks. The clean-world proof verifies its read-only seed hash before and after the ZFS gate. |
| **Third-Party Tarball Verification** | Pinned checksum enforcement was previously incomplete across packages | `tools/verify_third_party_checksums.sh` and `tools/fetch_with_checksum.sh` enforce SHA256 checksums on all external inputs. |
| **Dual Storage Track Coordination** | Transition from HFS+ to ZFS root required avoiding regressions | Distinct roles codified: ZFS root (`images/qemu/panthera-zfs-root.img`) is active default; HFS+ (`images/qemu/panthera-root.img`) is recovery baseline. |

---

## Reproducibility Acceptance Contract

The local R1 milestone met these criteria on 2026-09-06. They remain the
regression contract for a clean checkout:

1. Fetch and verify all third-party sources via `tools/verify_third_party_checksums.sh`.
2. Build the complete OS stack via `tools/build_world.sh` with zero manual intervention or untracked state dependencies.
3. Assemble both the default ZFS root image and HFS recovery image from staged payloads.
4. Execute `tools/zfs_release_gate.sh` and `tools/alpha_release_gate.sh` under QEMU to generate PASS summaries in `artifacts/release/`.
