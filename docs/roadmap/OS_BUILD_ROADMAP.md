# Panthera — OS Build Roadmap

> **Status: ROADMAP / FUTURE DIRECTION.**
> For the authoritative present-state summary, see **`docs/CURRENT_STATE.md`**.
> For historical bring-up context, see **`docs/STATUS.md`**.
> For source provenance and reproducibility, see **`docs/provenance/`**.

---

## Current Architecture & State Overview

Panthera is a bootable Darwin OS built from Apple open-source components targeting `x86_64` under QEMU with EDK2/OVMF UEFI firmware.

### Key Components Built from Source
- **Kernel:** Apple XNU `xnu-10002.41.9` (Sonoma-era OSS) with Panthera platform, VFS, and driver bring-up patches
- **Bootloader:** Custom UEFI bootloader (`boot/efi/loader/` -> `BOOTX64.EFI`) staging kernel and boot kexts
- **Dynamic Linker:** Custom `dyld` (`userland/dyld/panthera_dyld.cpp`)
- **LibSystem Stack:** Rebuilt `libsystem_c` (`Libc-1583.40.7`), `libsystem_malloc` (`libmalloc-474.0.13`), `libdispatch` (`libdispatch-1462.0.4`), `libsystem_info` (`Libinfo-583.0.1`)
- **Service Management:** Apple `launchd-842.92.1` with Panthera plist runtime supervising `/System/Library/LaunchDaemons/`
- **Shared Cache:** Split-region dyld shared cache (`tools/build_shared_cache.sh`)
- **Storage Tracks:**
  - **Active Default Development Path:** ZFS root pool `tank/ROOT/panthera` on `images/qemu/panthera-zfs-root.img` (`spl.kext`, `zfs.kext`, native `zpool`/`zfs`/`zsysctl`).
  - **Verified Alpha Recovery Baseline:** Raw HFS+ root on `images/qemu/panthera-root.img` (`HFS.kext`, `HFSEncodings.kext`).
- **Networking & Access:** Apple `configd` + `IPConfiguration` (DHCP on `en0`/RTL8139), OpenSSH `sshd`/`sftp` on host port 2222
- **Userland & SDK:** `zsh-108`, BSD coreutils (`file_cmds-475`, `shell_cmds-326`, `text_cmds-197`), Panthera SDK (`userland/panthera_sdk/`)

---

## FOUNDATION RULES — READ THIS FIRST

The foundation (dylibs, sysroot, libSystem) is the most fragile part of Panthera. **Every agent working on this project must follow these rules.**

### What You Must Never Do

1. **NEVER modify frozen dylibs by hand.** The frozen dylibs are: `libsystem_kernel`, `libsystem_platform`, `libsystem_pthread`, `libxpc`, `libpanthera_launchd`, `libsystem_malloc_simple`.
2. **NEVER link .o files manually with one-off `ld` commands.** Use the provided build scripts.
3. **NEVER add debug traces, printf statements, or logging to dylibs** unless explicitly asked.
4. **NEVER relink libSystem.B.dylib by hand.** Use `userland/libsystem/build/relink_libSystem.sh`.
5. **NEVER modify Apple source in `src/`** without logging the modification in `docs/provenance/APPLE_PATCHES.md`.

### What You Must Always Do

1. **Backup disk images before staging library changes.**
2. **Run `verify_exports.sh` after any library change:**
   ```bash
   bash userland/libsystem/verify_exports.sh
   ```
3. **Regenerate the shared cache after any dylib change:**
   ```bash
   bash tools/build_shared_cache.sh
   ```
4. **Ensure generated artifacts remain untracked** in accordance with `docs/provenance/GENERATED_ARTIFACTS.md`.

---

## Canonical Build System & Scripts

### Canonical World Build Entrypoint: `tools/build_world.sh`

`tools/build_world.sh` is the canonical entrypoint to build Panthera. It orchestrates existing scripts in topological order:

```bash
# List all build phases:
tools/build_world.sh --list

# Perform a dry-run check of the build graph:
tools/build_world.sh --dry-run

# Run a specific build phase:
tools/build_world.sh --phase <phase-name>
```

*Note: A repository-only clean world build was proven locally on 2026-09-06 (`artifacts/release/r1-clean-world-20260905-pass13.summary.md`). CI qualification is tracked under R6 in `docs/roadmap/DEVELOPMENT_ROADMAP.md`.*

### Key Subsystem Build Scripts

| Script | Purpose |
|---|---|
| `tools/build_world.sh` | **Canonical build entrypoint** (orchestrates full build graph) |
| `tools/verify_third_party_checksums.sh` | Verify third-party input tarballs against manifest |
| `build/build_xnu.sh` | Rebuild XNU kernel |
| `boot/efi/build_bootx64.sh` | Rebuild EFI bootloader |
| `kexts/OpenIOKit/build_all.sh` | Rebuild OpenIOKit kexts |
| `kexts/zfs/build_spl.sh` | Build OpenZFS SPL kext (`net.lundman.spl`) |
| `kexts/zfs/build_zfs.sh` | Build OpenZFS kext (`org.openzfsonosx.zfs`) |
| `userland/libsystem/build/relink_libpanthera_extra.sh` | Rebuild `libpanthera_extra.dylib` |
| `userland/libsystem/build/relink_libSystem.sh` | Rebuild `libSystem.B.dylib` umbrella |
| `userland/libsystem/build/panthera_patch.sh` | Add symbols or stubs to `libpanthera_extra` |
| `userland/libsystem/verify_exports.sh` | Check for unresolved or duplicate exports |
| `userland/libsystem/build/build_libsystem_c.sh` | Rebuild Libc (440 files) |
| `userland/libsystem/build/obj/libsystem_malloc/build.sh` | Rebuild libmalloc |
| `userland/launchd/build_launchd.sh` | Rebuild launchd |
| `rootfs/scripts/build_components.sh` | Build manifest-selected userland components |
| `userland/panthera_sdk/build_panthera_sdk.sh` | Build Panthera SDK sysroot |
| `tools/build_shared_cache.sh` | Build & stage dyld shared cache |
| `rootfs/create_rootfs_tree.sh` | Manifest-neutral rootfs directory tree staging |
| `rootfs/create_rootfs_payload_tar.sh` | Manifest-neutral rootfs payload tarball staging |
| `rootfs/create_zfs_root_image.sh` | **Generate active default ZFS root image** |
| `rootfs/create_hfs_root_image.sh` | **Generate verified HFS+ alpha recovery image** |
| `tools/alpha_release_gate.sh` | Execute HFS alpha release verification gate |
| `tools/zfs_release_gate.sh` | Execute ZFS release verification gate |
| `tools/boot_verify.sh` | QEMU boot and runtime verification harness |

---

## Root Image Workflows

### 1. Active Default: ZFS Root Image (`images/qemu/panthera-zfs-root.img`)
- Generated via `rootfs/create_zfs_root_image.sh`.
- Boots native ZFS root pool `tank/ROOT/panthera` with SPL & ZFS kexts.
- Tested via:
  ```bash
  boot/qemu/run_phase2_qemu.sh --root-disk images/qemu/panthera-zfs-root.img --zfs
  ```

### 2. Recovery Baseline: HFS+ Root Image (`images/qemu/panthera-root.img`)
- Generated via `rootfs/create_hfs_root_image.sh`.
- Tested via:
  ```bash
  boot/qemu/run_phase2_qemu.sh --root-disk images/qemu/panthera-root.img
  ```

---

## Completed Phases (Summary)

- **Phase 0 (Console & Shell):** PS/2 keyboard, ncurses 6.5, terminfo, dynamic zsh with line editing and history.
- **Phase 1 (Core Utilities):** 69 BSD core utilities built from `file_cmds`, `shell_cmds`, `text_cmds`.
- **Phase 2 (User Management Foundation):** `/etc/passwd`, `/etc/group`, `getpwuid()`, `id`, `whoami`.
- **Phase 3 (Networking Foundation):** RTL8139 Ethernet, `IONetworkingFamily`, `netbringup`, ICMP ping.
- **Foundation Infrastructure:** dyld shared cache, Libc/libmalloc rebuilds, HFS+ read-write mount, devfs, RTC clock, launchd plist supervision.
- **Phase 4 / Alpha Release Gate (SSH & Networking):** Automated DHCP via `configd` + `IPConfiguration`, OpenSSH `sshd`/`sftp` on host port 2222, `pselect`/`SIGCHLD` signal delivery, mDNS DNS-SD, native Panthera SDK compilation, guest toolchain compilation (Verified PASS: `artifacts/release/alpha-release-20260521-weak-coalesce-r16.summary.md`).
- **ZFS Integration (Phases Z0–Z7):** SPL and OpenZFS kexts loaded, native `zpool`/`zfs`/`zsysctl` tools, secondary data pool persistence (Z4), hybrid layout (Z5), native ZFS root bootstrap (Z6/Z7) (Verified PASS: `artifacts/release/zfs-full-post-upload-20260523-r2.summary.md`).

---

## Active & Upcoming Roadmap Phases

### Phase 5: Complete Core Utilities & Userland Expansion
- **Goal:** Comprehensive BSD/Darwin utility coverage.
- **Remaining targets:** `system_cmds` (`ps`, `top`, `mount`, `umount`, `stty`, `sysctl`, `dmesg`, `reboot`, `shutdown`), `less`, `bsdtar`, `nvi`/`vim`, `curl`.

### Phase 6: launchd Mach IPC Server (Phases L2–L6)
- **Goal:** Full Mach bootstrap server with native service registration, lookup, and kqueue-driven event loops.
- Roadmap details in `docs/launchd/BOOTSTRAP_COMPATIBILITY.md` and `docs/roadmap/AUTHENTIC_DARWIN_PLAN.md`.

### Phase 7: Package Management (pkgsrc)
- **Goal:** Self-hosting package installations via pkgsrc on ZFS root.
- **Status:** Package bootstrap and extraction performance optimization on fresh ZFS roots under active investigation.

### Phase 8: Multi-User Security & Authentication
- **Goal:** Multi-user security boundaries, password checking (`crypt`), `login`, `su`/`sudo`, `/Users` home directories.

### Phase 9: System Polish & Identity
- **Goal:** System identity (`sw_vers`, `kern.ostype`), syslog daemon, boot scripts, font/console customization.

### Phase 10: Release Distribution & World Build Proof
- **Goal:** Automated, reproducible clean world build via `tools/build_world.sh` proven in CI; release image artifact distribution.

---

## Cross-Compilation Reference

```bash
CC="$(xcrun -find clang)"
SYSROOT=/Users/admin/panthera/userland/libsystem/build/sysroot
TARGET="x86_64-apple-darwin23.0"

$CC -target $TARGET -mmacosx-version-min=14.0 \
    -isysroot $SYSROOT \
    -I $SYSROOT/usr/include \
    -L $SYSROOT/usr/lib -L $SYSROOT/usr/lib/system \
    -o <binary> <sources> \
    -lSystem
```

---

## Related Architectural Plans

- `docs/roadmap/DEVELOPMENT_ROADMAP.md` — **Authoritative next-milestone sequence (R1–R6)**
- `docs/roadmap/ZFS_INTEGRATION_PLAN.md` — Active ZFS root migration plan
- `docs/roadmap/AUTHENTIC_DARWIN_PLAN.md` — Authentic Apple Darwin alignment plan
- `docs/roadmap/XNU_UPGRADE_PLAN.md` — Kernel upgrade roadmap
- `docs/SHIM_DEBT_REGISTER.md` — Compatibility shim retirement register
- `docs/provenance/PROVENANCE.md` — Source and artifact provenance
