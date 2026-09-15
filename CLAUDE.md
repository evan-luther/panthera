# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Panthera** is a continuation of the PureDarwin project: building a fully functional, bootable Darwin operating system from Apple's open-source components. The goal is a usable headless system with working networking, storage, package management, and SSH — a real Darwin server you can log into and administer.

Named after *Panthera uncia* (Snow Leopard's genus) — the last macOS release where Darwin felt like a standalone Unix system.

- **Kernel:** XNU xnu-10002.41.9 (Sonoma-era), latest open-source drop
- **Target:** x86_64 (QEMU/KVM, VMware, commodity PC hardware)
- **Userland:** launchd, libSystem, libdispatch, BSD layer, shell, core utils
- **Package management:** pkgsrc or Homebrew (TBD based on feasibility)
- **Host toolchain:** Xcode clang + open-source build tools

## Architecture

```
USER SPACE:  launchd → sshd/getty → bash → BSD tools
             libSystem (libdispatch, libc, libinfo, libnotify, ...)
             pkgsrc / Homebrew for additional packages
KERNEL:      XNU (Mach + BSD + IOKit), open-source kexts
BOOT:        OpenCore / Chameleon / GRUB → boot.efi → XNU
HARDWARE:    x86_64 PC or VM (QEMU primary dev target)
```

## Repository Structure

```
panthera/
├── docs/                    # Architecture docs, research, phase plans
├── kernel/
│   ├── xnu/                 # XNU source (xnu-10002.41.9) + patches
│   ├── config/              # Kernel configurations (PANTHERA, GENERIC)
│   └── patches/             # Patchset against upstream XNU (quilt/git format)
├── kexts/                   # Required kernel extensions
│   ├── IOPCIFamily/
│   ├── IOStorageFamily/
│   ├── IONetworkingFamily/
│   ├── IOAHCIFamily/        # SATA/AHCI
│   └── ...
├── boot/
│   ├── efi/                 # EFI bootloader (boot.efi or open replacement)
│   └── opencore/            # OpenCore config for Darwin boot on PC hardware
├── userland/
│   ├── launchd/             # launchd (open-source)
│   ├── libsystem/           # libSystem and sub-libraries
│   ├── shell/               # bash + coreutils
│   ├── network/             # mDNSResponder, configd, network setup
│   └── packages/            # pkgsrc bootstrap or Homebrew integration
├── build/
│   ├── xnu.mk               # Kernel build
│   ├── kexts.mk              # Kext build
│   ├── world.mk              # Full userland build
│   ├── rootfs.mk             # Root filesystem image assembly
│   ├── iso.mk                # Bootable ISO generation
│   └── toolchain/            # Build tool setup and stubs
├── rootfs/
│   ├── etc/                  # System configuration files
│   ├── launchd.plist         # launchd job definitions
│   └── rc.d/                 # Boot scripts
├── images/                   # Built output (ISO, VMDK, raw disk images)
└── tests/                    # Boot tests, integration tests, CI
```

## Prior Art — PureDarwin

This project picks up where PureDarwin left off. Key history:

- **PureDarwin Nano** (2008–2012): Minimal bootable Darwin, 9MB ISO, bash + a few utils. Proved the concept on x86_64.
- **PureDarwin Xmas** (2008): More complete build with X11 window manager. Never reached stable usability.
- **PureDarwin project** (2012–2020s): Sporadic updates, build scripts for newer XNU drops, never reached a polished distributable state.
- **Key challenges they hit:** Apple's open-source drops have undocumented interdependencies, missing headers, and assume the rest of macOS exists. Each new XNU release breaks the build in new ways.

Our advantage: we can leverage modern tooling, LLM-assisted dependency resolution, and the accumulated knowledge from Asahi Linux, the Hackintosh community, and PureDarwin's own archives.

## Development Phases

### Phase 1 — Compile XNU (x86_64)
- Fetch xnu-10002.41.9 source + all Apple open-source dependencies
- Resolve header dependencies (Availability.h, AvailabilityMacros, SDK headers)
- Patch and compile XNU for x86_64 RELEASE configuration
- Produce a loadable Mach-O kernel binary
- **Deliverable:** `mach_kernel` that links clean

### Phase 2 — Boot to Panic
- Set up OpenCore or Chameleon bootloader for QEMU x86_64
- Create minimal boot environment (boot.efi or equivalent)
- Boot XNU in QEMU — expect kernel panic due to missing root fs / kexts
- Confirm serial/VGA console output works
- **Deliverable:** XNU boots, prints panic, proves the boot chain works

### Phase 3 — Root Filesystem + Single-User Shell
- Build essential kexts: IOPCIFamily, IOStorageFamily, IOAHCIFamily, IONetworkingFamily
- Build launchd (open-source) and libSystem
- Assemble minimal root filesystem (/, /bin, /sbin, /usr, /etc, /var, /dev)
- Boot to single-user mode with bash
- **Deliverable:** Interactive shell prompt over serial/VGA in QEMU

### Phase 4 — Networking + Multi-User
- IONetworkingFamily + virtio-net or e1000 driver for QEMU
- configd, mDNSResponder, basic network stack
- getty/login flow, multi-user boot
- sshd (OpenSSH) — remote login works
- **Deliverable:** SSH into a running Darwin VM

### Phase 5 — Package Management + Usability
- Bootstrap pkgsrc (NetBSD's portable package system — proven on Darwin)
- Or: port Homebrew's core (Ruby + curl + git)
- Build essential packages: git, curl, wget, python3, vim, tmux
- System identity: kern.ostype=Darwin, kern.osrelease, sw_vers output
- **Deliverable:** Usable headless Darwin server with package installation

### Phase 6 — Distribution
- Bootable ISO image generation (install media)
- VMDK/qcow2 images for VMware/QEMU
- Documentation: installation guide, package list, known issues
- **Deliverable:** Downloadable images, reproducible build

## Key Apple Open-Source Dependencies

XNU does not build in isolation. These projects must be fetched and built (or have headers extracted):

| Component | Apple Project | Purpose |
|-----------|--------------|---------|
| XNU | xnu-10002.41.9 | Kernel |
| Libsystem | Libsystem-1345 | Umbrella userland library |
| libdispatch | libdispatch-1462 | GCD / kernel work queues |
| launchd | launchd-2038 | Init system (PID 1) |
| AvailabilityVersions | AvailabilityVersions-132 | Availability macros |
| IOKitUser | IOKitUser-1929 | IOKit userland interface |
| dtrace | dtrace-388 | DTrace support (can stub) |
| corecrypto | corecrypto-1200 | Kernel crypto |
| libplatform | libplatform-306 | Platform abstractions |
| libpthread | libpthread-518 | POSIX threads |

Version numbers are approximate — use whatever matches xnu-10002 era. Apple's open-source site: https://opensource.apple.com/releases/

## Build Notes

- XNU requires `ctfconvert`/`ctfmerge` from dtrace — stub with `true` if DTrace is disabled
- Many Apple headers reference macOS SDK paths — must redirect or provide shims
- `AvailabilityMacros.h` and `Availability.h` are perennial pain points — generate or extract from SDK
- For x86_64: target triple is `x86_64-apple-darwin23.0` (darwin23 = Sonoma)
- If building on ARM64 macOS host: `clang -target x86_64-apple-darwin23.0`
- QEMU target: `-M q35 -cpu Haswell` (good baseline x86_64 with features XNU expects)
- All development and testing happens in QEMU x86_64 — keep iteration loop fast

## License Rules

- **XNU:** APSL 2.0 — free to modify and redistribute with attribution
- **Apple open-source projects:** Mix of APSL 2.0 and BSD — check each project
- **IOKit kexts:** APSL 2.0 (Apple's open-source kexts)
- **OpenCore:** BSD 3-clause
- **pkgsrc:** BSD — ideal for Darwin due to NetBSD heritage
- **Never include:** proprietary Apple frameworks, closed-source kexts, or macOS binaries

## Development Workflow

1. Fetch/update Apple open-source dependencies (scripted)
2. Apply patchset to XNU and dependencies
3. Build kernel: `make -f build/xnu.mk`
4. Build kexts: `make -f build/kexts.mk`
5. Build userland: `make -f build/world.mk`
6. Assemble root filesystem: `make -f build/rootfs.mk`
7. Generate bootable image: `make -f build/iso.mk`
8. Test in QEMU: `qemu-system-x86_64 -M q35 -cpu Haswell -m 4G -cdrom images/panthera.iso`

## QEMU Testing

Primary development VM configuration:
```
qemu-system-x86_64 \
  -M q35 -cpu Haswell -smp 4 -m 4G \
  -drive file=images/panthera.qcow2,format=qcow2 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device e1000,netdev=net0 \
  -serial mon:stdio \
  -nographic
```

## References

- PureDarwin wiki: https://github.com/PureDarwin/PureDarwin/wiki
- Apple open-source: https://opensource.apple.com/
- XNU build guides: https://kernelshaman.blogspot.com/
- OpenCore documentation: https://dortania.github.io/OpenCore-Install-Guide/
