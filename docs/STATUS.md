# Panthera — Project Status (Historical Overview)

> **Status: HISTORICAL / BACKGROUND CONTEXT ONLY.**
> For the current authoritative status of the project, see **`docs/CURRENT_STATE.md`**.
> For active architectural plans and roadmaps, see **`docs/roadmap/`**.
> For source provenance and reproducibility ledgers, see **`docs/provenance/`**.

---

## Executive Summary

Panthera is an open-source operating system project that builds a fully functional, bootable Darwin operating system targeting `x86_64` under QEMU with EDK2/OVMF firmware, continuing where PureDarwin left off.

### High-Level Milestones Achieved

1. **Kernel & EFI Boot (Phases 1-2):**
   - Built Apple XNU `xnu-10002.41.9` kernel from source with Panthera platform/boot bring-up patches.
   - Implemented custom UEFI bootloader (`boot/efi/loader/` -> `BOOTX64.EFI`) staging the kernel and booter-kexts via XNU's booter-kext ABI into `/chosen/memory-map`.

2. **Driver Framework & Storage Bring-Up (Phase 3):**
   - Built OpenIOKit drivers (`kexts/OpenIOKit/`), `PantheraATAStorage`, and Apple HFS filesystem kexts (`hfs-650.0.2`).
   - Reached working root attach and read-write HFS+ filesystem mounting.

3. **Userland Foundation & Service Management (Phases 4-6):**
   - Rebuilt Apple Libc (`Libc-1583.40.7`), libmalloc (`libmalloc-474.0.13`), libdispatch (`libdispatch-1462.0.4`), and libsystem_info (`Libinfo-583.0.1`).
   - Implemented custom `dyld` (`userland/dyld/panthera_dyld.cpp`) and generated split-region dyld shared cache (`tools/build_shared_cache.sh`).
   - Ported Apple `launchd-842.92.1` with Panthera plist runtime to supervise system services (`/System/Library/LaunchDaemons/`).

4. **Networking, Security, & Verified HFS Alpha Release (Phases 7-8):**
   - Brought up RTL8139 networking with Apple `configd` + `IPConfiguration` managing automated DHCP (`10.0.2.15`).
   - Staged and verified OpenSSH `sshd`/`sftp` over QEMU host forward (`127.0.0.1:2222`).
   - Achieved verified HFS Alpha Release Gate PASS (`artifacts/release/alpha-release-20260521-weak-coalesce-r16.summary.md`).

5. **ZFS Root Migration & Storage Track (Active Development Track):**
   - Built `spl.kext` and `zfs.kext` from pinned OpenZFS Darwin sources; built native `/sbin/zpool`, `/sbin/zfs`, `/usr/sbin/zsysctl`, and `libDiskArbitration.dylib`.
   - Achieved complete native ZFS root boot (`/` on `zfs`), launchd PID 1 bootstrap, and verified ZFS full release gate PASS (`artifacts/release/zfs-full-post-upload-20260523-r2.summary.md`).
   - Established ZFS as the active default development root while retaining HFS+ as the recovery baseline.

6. **Unified Build Orchestration:**
   - Canonical build entrypoint established at `tools/build_world.sh`.
   - Clean world build proven locally on 2026-09-06 (`artifacts/release/r1-clean-world-20260905-pass13.summary.md`, PASS at `b50c669`); CI qualification remains open (R6).

---

## Historical Bring-Up Narrative (Archive)

The sections below are preserved for historical engineering context.

### Early Userland Foundation Bring-Up (2026-03-30)

- Initial PID 1 supervision was bootstrapped via `userland/launchd/mini_launchd.c` to open `/dev/console` and reap child processes.
- Dynamic `zsh` reached interactive prompt on `/dev/console` after dyld early malloc/libSystem initialization was resolved.
- Layer 3 promoted Mach IPC surface into `libsystem_kernel.dylib`.
- Layer 4 rebuilt `libdispatch.dylib` from `src/libdispatch-1462.0.4/` with launchd-facing queue and process-monitor exports.
- Layers 5-7 staged `libxpc`, `libsystem_info`, and `libpanthera_launchd`.
- Layer 8 transitioned to real Apple `launchd` as PID 1 supervising `/bin/sh` and plist-defined services.

### Early Storage & Kext Bring-Up

- OpenIOKit bring-up built 15 early kext targets (`Apple16X50Serial`, `AppleAPIC`, `AppleI386GenericPlatform`, `AppleI386PCI`, `AppleIntelPIIXATA`, `ApplePS2Controller`, `AppleRTL8139Ethernet`, `AppleSMBIOS`, `HFS`, `HFSEncodings`, `IOACPIFamily`, `IOATAFamily`, `IONetworkingFamily`, `IOPCIFamily`, `IOStorageFamily`).
- Transitioned from ATA controller probing to root disk attach, catalog B-tree fixups, and HFS+ read-write root mounting.

---

## Canonical Documentation References

- **Current State:** `docs/CURRENT_STATE.md`
- **Build Graph & Roadmap:** `docs/roadmap/OS_BUILD_ROADMAP.md`
- **ZFS Migration Plan:** `docs/roadmap/ZFS_INTEGRATION_PLAN.md`
- **Source & Artifact Provenance:** `docs/provenance/PROVENANCE.md`
