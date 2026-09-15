<div align="center">

# Panthera

**Darwin, built to stand on its own.**

A headless Darwin system built around XNU, a native Unix userland, and ZFS.

[Current status](docs/CURRENT_STATE.md) · [Build and run](#build-and-run) · [Roadmap](docs/roadmap/DEVELOPMENT_ROADMAP.md) · [Provenance](docs/provenance/PROVENANCE.md)

</div>

---

Panthera carries forward the idea behind **PureDarwin**: a Darwin system you can boot, log into, build software on, and administer without the rest of macOS.

The goal is a useful headless server—not a macOS desktop replacement, a Linux distribution, or a collection of prebuilt macOS binaries. Today, Panthera boots XNU in QEMU, mounts a ZFS root, starts its native runtime and services, and provides a shell and SSH access. In-guest package management is the next milestone.

Named for *Panthera uncia*, the snow leopard.

> **Development system, not a production OS.** The verified target is x86_64 under QEMU. Physical hardware, production security, and general application compatibility are not qualified. This repository is a source/build checkpoint, not an installer release.

## What works today

| Area | Verified capability |
| :--- | :--- |
| **Kernel and boot** | XNU `10002.41.9` / Darwin `23.1.0`, Panthera's EFI loader, serial boot and login |
| **Native runtime** | Custom dyld, libSystem, launchd, and a generated shared cache |
| **Storage** | ZFS root, explicit HFS recovery path, PIIX ATA DMA and interrupt-driven I/O |
| **Networking** | DHCP and DNS through configd/IPConfiguration, mDNS, OpenSSH command execution and SFTP |
| **Unix tools** | Process and terminal tools, mount/unmount, shutdown/reboot, less, bsdtar, Vim, and curl |
| **Development** | Native compiler/linker and Panthera SDK; compile-and-run checks inside the guest |
| **System lifecycle** | Normal reboot, scheduled reboot, and scheduled halt on both ZFS and HFS |

### Verification, not just compilation

The last accepted core-utility checkpoint is `265de5c` (2026-09-10):

- **ZFS alpha gate: 23/23 PASS.**
- **HFS recovery gate: 24/24 PASS.**
- Each filesystem passed two actual EFI restarts followed by a scheduled halt, without a kernel panic.
- Staged-image utility checks covered process selection, tty restoration, archive roundtrips, Vim math, curl, and ZFS mount/remount persistence.

These are **local QEMU results**, not a claim of CI or physical-hardware qualification. [Current state](docs/CURRENT_STATE.md) records the evidence paths and limitations. Generated logs and disk images are not checked into Git.

## Architecture

```text
                     SSH / serial login
                              |
               launchd + native Unix userland
                              |
            dyld + libSystem + Panthera SDK
                              |
                 XNU + IOKit + OpenZFS
                              |
                   Panthera EFI loader
                              |
                       QEMU x86_64
```

ZFS is the default **alpha acceptance** and developer-launch path. HFS remains an explicit recovery and bootstrap path. The top-level build tool still defaults to HFS; select `--target zfs` when building ZFS.

## Build and run

### Host requirements

The development workflow is macOS-hosted and has been exercised on Apple Silicon while cross-building x86_64 guests. It uses:

- A complete Xcode developer toolchain with `clang`, `mig`, `migcom`, `iig`, and a macOS SDK. Command Line Tools alone may not provide every required tool.
- QEMU, including `qemu-system-x86_64`, `qemu-img`, and EDK2 firmware.
- The host build tools required by the component scripts, including Git, Python 3, Ruby, CMake/Ninja, and LLVM's `lld-link` for EFI linking.
- Free disk space for downloaded sources, intermediate builds, and multiple VM images.

Some scripts use Homebrew-style paths. `PANTHERA_DEVELOPER_DIR` selects the Xcode developer directory; `PANTHERA_EDK2_CODE` and `PANTHERA_EDK2_VARS_TEMPLATE` override firmware paths. This is not yet a one-command, arbitrary-host installation experience.

### Start from source

```sh
git clone https://github.com/evan-luther/panthera.git
cd panthera

# Fetch external sources and restore the tracked source overlays.
bash tools/fetch_world_sources.sh

# Inspect the build graph before executing it.
bash tools/build_world.sh --list
bash tools/build_world.sh --dry-run --target zfs --enable-qemu

# Build the HFS recovery/bootstrap environment first.
bash tools/build_world.sh --target hfs

# Build ZFS using the installer path; this explicitly permits QEMU.
bash tools/build_world.sh --target zfs --zfs-builder-mode installer --enable-qemu
```

`tools/build_world.sh` is the canonical entrypoint. It delegates to the existing kernel, driver, runtime, userland, and image builders. Use `--phase`, `--from-phase`, and `--to-phase` for bounded rebuilds rather than inventing a separate build sequence.

For the full isolated-checkout build procedure, see [reproducibility](docs/provenance/REPRODUCIBILITY.md) and `tools/clean_world_proof.sh`. The last full clean-world measurement was **3,840 seconds** on the development host; it predates the latest core-utility checkpoint and is not a universal build-time estimate.

### Boot a disposable guest

```sh
# Serial console only; disable SSH forwarding for the safest first boot.
PANTHERA_ROOT_DISK_SNAPSHOT=on \
  bash tools/run_qemu_ssh.sh --root-kind zfs --restage --port 0
```

Log in as `root` and press Enter at the password prompt. `PANTHERA_ROOT_DISK_SNAPSHOT=on` discards guest writes when QEMU exits. Omit it only when you deliberately want to modify the image.

**Security warning:** development images use an empty root password. The current SSH launcher forwards port `2222` by default and its QEMU forwarding rule can listen on all host interfaces. Do not expose it to an untrusted network. Keep `--port 0`, or restrict host access before enabling SSH in an isolated development environment.

With forwarding deliberately enabled:

```sh
ssh -p 2222 root@127.0.0.1
```

### Run acceptance checks

Run these **serially**: they share images and EFI staging.

```sh
# Default ZFS alpha acceptance.
bash tools/alpha_release_gate.sh --tag local-zfs --timeout 900

# Explicit HFS recovery acceptance.
bash tools/alpha_release_gate.sh --root-kind hfs --tag local-hfs --timeout 900

# Staged utilities and real reboot/halt transitions.
bash tools/smoke_system_cmds.sh --root-kind zfs --tag local-utilities --timeout 900
bash tools/smoke_reboot.sh --root-kind zfs --restage --tag local-reboot
```

The utility and lifecycle smokes use disposable overlays. Detailed results are written under `artifacts/`; preserve both failed and successful runs when reporting a problem.

## What is next

| Milestone | Status |
| :--- | :--- |
| Reproducible local world build | Accepted; CI qualification remains open |
| ATA DMA and storage improvements | Accepted for the QEMU development target |
| ZFS-native staging and ZFS-default alpha acceptance | Accepted; the ≤45-minute clean-world target remains open |
| Native core utilities and orderly reboot/halt | Accepted for the exercised scope |
| **pkgsrc self-hosting** | **Next: bootstrap, then build and install a real package in-guest** |
| Service hardening, broader hardware support, distributable images | Later milestones |

The public checkpoint does **not** claim a working pkgsrc bootstrap or package installation. See the [development roadmap](docs/roadmap/DEVELOPMENT_ROADMAP.md) for the full sequence.

### Known boundaries

- Not all macOS APIs, frameworks, services, or applications are available.
- bsdtar plain-file roundtrips pass, but extended-attribute preservation is unqualified; the existing warning remains visible.
- `ps` omits the unsupported `prsna` column instead of returning invented data.
- Reset is verified on the QEMU legacy-PC/i8042 path—not ACPI-only or physical machines. A CPU halt is not a claim of physical power-off.
- The previously documented Vim xdiff provenance gap remains open.
- There are no production-ready installer images or published CI guarantees in this checkpoint.
- Some tracked include symlinks are development-host-specific. A fresh-clone build on other hosts is not yet qualified and may require regenerating those include trees.

## Repository map

| Path | Purpose |
| :--- | :--- |
| `boot/` | EFI loader, staging, and QEMU launchers |
| `build/` | XNU build and host-toolchain integration |
| `kexts/` | IOKit drivers, ATA storage, SPL, and OpenZFS integration |
| `userland/` | Runtime libraries, services, tools, and native SDK builders |
| `rootfs/` | System configuration and filesystem assembly |
| `manifests/` | Required and optional root filesystem inputs |
| `src/` | Tracked upstream inputs and local source overlays |
| `tools/`, `tests/` | Build orchestration, probes, and acceptance checks |
| `docs/` | Current state, roadmap, architecture, and provenance |

## Contributing

Start with [current state](docs/CURRENT_STATE.md) and the [roadmap](docs/roadmap/DEVELOPMENT_ROADMAP.md). Keep changes bounded, use the canonical builders, and verify behavior in the guest—not just whether a binary links.

For a useful bug report, include the commit, host/toolchain details, root filesystem kind, exact command, and the relevant serial/SSH log. Keep generated binaries, caches, and VM images out of source commits. Review the [generated-artifact policy](docs/provenance/GENERATED_ARTIFACTS.md) before adding build outputs.

## Licensing and acknowledgments

Panthera combines components with different licenses; there is no single blanket license covering this repository. Upstream notices and component-specific terms apply. Consult the [provenance ledger](docs/provenance/PROVENANCE.md), [third-party inventory](docs/provenance/THIRD_PARTY.md), and [Apple patch ledger](docs/provenance/APPLE_PATCHES.md) before redistribution. Publication does not grant rights to proprietary Apple software or relicense third-party code.

See [NOTICE](NOTICE) and the [license text bundle](LICENSES/) for retained component notices. Panthera-authored integration code has no blanket license grant yet. Optional user-template assets and an unused font with unestablished redistribution permissions are omitted from this public snapshot; the active console font is Tamzen.

This product includes software developed by the University of California, Berkeley and its contributors.

The initial public snapshot has fresh history based on local acceptance checkpoint `265de5cfddcbd63ac7505e7cdb5594460321ed08`. That hash is a provenance reference, not an ancestor in this repository. Unfinished pkgsrc experiments and unrelated historical project data are not included.

Built on work from **Apple's open-source projects**, **PureDarwin**, **OpenZFS**, **LLVM**, the **BSD projects**, and the other upstream projects recorded in the provenance inventory.
