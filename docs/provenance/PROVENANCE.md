# Panthera Provenance

## Purpose

This document records how Panthera is assembled, which parts come from Apple open-source releases, which parts are Panthera-authored, which parts come from third parties, and which paths are generated artifacts rather than source.

The goal is to make the repository reviewable by systems engineers, Apple engineers, OSS maintainers, and contributors who need a precise answer to:

- what is upstream Apple code
- what has been patched locally
- what is original Panthera code
- what is generated during the build
- what still needs cleanup before the repository is production-grade

Panthera is not macOS. It is a Darwin-derived operating system assembled primarily from Apple open-source components, plus Panthera-authored integration code and a small set of third-party dependencies.

## Assembly Model

At a high level, Panthera is assembled in five layers:

1. Upstream Apple open-source drops under `src/`
2. Panthera build glue that stages headers, compiles kernels/kexts/userland, and builds a sysroot
3. Panthera-authored runtime components such as the EFI loader, custom `dyld`, shared-cache builder, local kexts, and root-image assembly
4. Third-party dependencies used for usability or compatibility
5. Generated outputs such as the staged EFI tree, sysroot dylibs, shared cache, and root image

## Provenance Buckets

Every Panthera path should fit one of these buckets:

### 1. Apple OSS Source Drops

Versioned upstream Apple open-source code imported into `src/`.

Examples:

- `src/xnu-10002.41.9`
- `src/launchd-842.92.1`
- `src/dyld-1122.1.2`
- `src/Libc-1583.40.7`
- `src/Libinfo-583.0.1`
- `src/libdispatch-1462.0.4`
- `src/libmalloc-474.0.13`
- `src/IO*`, `src/Apple*`, `src/hfs-650.0.2`
- `src/file_cmds-475`, `src/shell_cmds-326`, `src/text_cmds-197`

### 2. Apple OSS With Panthera Local Patches

Upstream Apple source trees that are modified locally to support bring-up, compatibility, instrumentation, or missing platform/runtime behavior.

Known examples found during audit:

- `src/xnu-10002.41.9`
- `src/launchd-842.92.1`
- `src/zsh-108`
- `src/shell_cmds-326`

These trees require a patch manifest. Local edits should not remain implicit.

### 3. Panthera-Authored Code

Original code written for Panthera's boot path, runtime, integration, and assembly.

Examples:

- `boot/efi/loader/`
- `boot/qemu/`
- `userland/dyld/panthera_dyld.cpp`
- `tools/build_shared_cache*.c`
- `kexts/OpenIOKit/local/PantheraATAStorage/`
- `userland/network_tools/`
- `rootfs/`
- `userland/launchd/panthera_fork.s`
- `userland/shell/mini_sh.c`

### 4. Third-Party Non-Apple Dependencies

Dependencies that do not originate from Apple OSS.

Examples in tree or build flow:

- `src/libiconv-1.17`
- `src/ncurses-6.5`
- `src/dash-0.5.12`
- `src/dropbear-2024.86`
- `vendor/usertemplate-109`
- Nano, fetched by `userland/nano/build_nano.sh`
- OpenSSL, fetched by `userland/openssl/build_openssl.sh`

### 5. Generated Artifacts

Build outputs or staged outputs that are not source.

Examples:

- `boot/efi/staging/`
- `userland/libsystem/build/sysroot/`
- `images/shared_cache/`
- `images/qemu/panthera-root.img`
- build object trees under `BUILD/`, `build/obj/`, `build/dst/`, `build/sym/`
- staged binaries under `userland/*/bin/`

Generated artifacts should be clearly distinguished from authored source and, where practical, excluded from the main review path.

## Component Inventory

| Component | Source Origin | Build / Assembly Path | Current Status |
|---|---|---|---|
| Canonical build entrypoint | Panthera-authored | `tools/build_world.sh` | Orchestrates full build graph |
| XNU kernel | Apple OSS `xnu-10002.41.9` | `build/build_xnu.sh` | Upstream source plus Panthera local patches |
| EFI boot path | Panthera-authored | `boot/efi/loader/`, `boot/efi/build_bootx64.sh` | Panthera original code |
| Boot-critical kexts | Apple OSS `Apple*`, `IO*`, `hfs-*` | `kexts/OpenIOKit/build_all.sh`, `build_kext.sh` | Mostly upstream source compiled via Panthera glue |
| ATA storage bridge kext | Panthera-authored | `kexts/OpenIOKit/local/PantheraATAStorage/` | Panthera original code |
| OpenZFS & SPL kexts | OpenZFS Darwin upstream | `kexts/zfs/build_spl.sh`, `kexts/zfs/build_zfs.sh` | Upstream Darwin port with Panthera VFS/LDI patches |
| `dyld` runtime | Apple headers/parsers plus Panthera loader/binder | `userland/dyld/` | Substantially Panthera-authored runtime |
| libSystem stack | Apple OSS sources plus Panthera assembly/patch layers | `userland/libsystem/` | Mixed: upstream builds plus Panthera glue and patch dylibs |
| `launchd` | Apple OSS `launchd-842.92.1` | `userland/launchd/build_launchd.sh` | Upstream source with Panthera plist runtime |
| Core utilities | Apple OSS and third-party package sources | `rootfs/scripts/build_components.sh`, subsystem `userland/*/build_*.sh` scripts | Mostly upstream source with Panthera build glue and shims |
| ZFS userland tools | OpenZFS Darwin upstream | `userland/zfs/build_zfs_userland.sh` | Native `zpool`, `zfs`, `zsysctl` tools |
| Network tools | Panthera-authored | `userland/network_tools/` | Panthera original code |
| Rootfs payload staging | Panthera-authored | `rootfs/create_rootfs_tree.sh`, `rootfs/create_rootfs_payload_tar.sh` | Manifest-neutral payload staging contract |
| ZFS root image assembly | Panthera-authored | `rootfs/create_zfs_root_image.sh` | Active default ZFS root image builder |
| HFS root image assembly | Panthera-authored | `rootfs/create_hfs_root_image.sh` | Verified HFS+ recovery image builder |
| Shared cache builder | Panthera-authored | `tools/build_shared_cache*.c`, `tools/build_shared_cache.sh` | Panthera original code |
| Nano | Third-party upstream | `userland/nano/build_nano.sh` | Fetched with checksum verification, staged locally |
| OpenSSL | Third-party upstream | `userland/openssl/build_openssl.sh` | Fetched with checksum verification, staged locally |
## Known Local Patch Areas

This is a high-level ledger from the read-only audit. It is not yet a complete patch manifest.

### XNU

Known Panthera-specific changes include:

- fallback x86 platform nub seeding and CMOS RTC support in `iokit/Kernel/IOPlatformExpert.cpp`
- bring-up and kext loader tracing in `bsd/kern/bsd_init.c`, `libkern/c++/OSKext.cpp`, `libkern/kxld/*`, `iokit/Kernel/IOStartIOKit.cpp`
- compatibility workarounds in files such as `libkern/os/log.c`, `libkern/crypto/corecrypto_rand.c`, `iokit/bsddev/IOKitBSDInit.cpp`

These should be documented file-by-file in a separate patch ledger.

### launchd

Known Panthera-specific changes include:

- minimal PID 1 bring-up path
- audit/syslog/signal/runtime shortcuts for early boot stability
- serial/console tracing

### zsh and shell userland

Known Panthera-specific changes include:

- signal/job-control tracing in `src/zsh-108/zsh/Src/*`
- bring-up adjustments in `src/shell_cmds-326/id/id.c`

## Third-Party Inputs

Panthera currently uses two different third-party intake models:

### Vendored in Source Tree

Examples:

- `src/libiconv-1.17`
- `src/ncurses-6.5`
- `src/dash-0.5.12`
- `src/dropbear-2024.86`

### Fetched During Build

Examples:

- Nano via `userland/nano/build_nano.sh`
- OpenSSL via `userland/openssl/build_openssl.sh`

Long term, each third-party dependency should follow a single, explicit policy:

- vendored source with version pin and license note, or
- fetch-at-build with pinned URL, checksum verification, and a manifest

## Generated Artifact Policy

The repository currently contains generated outputs that blur the line between source and build product.

Examples identified during audit:

- tracked EFI staging content under `boot/efi/staging/`
- tracked sysroot dylibs under `userland/libsystem/build/sysroot/`

For a production-grade repository, source review should not require reading generated binaries. Generated outputs should either:

- be rebuilt from clean checkout
- be moved to release artifacts
- or be kept only when strictly necessary and clearly marked as generated

## Current Provenance Risks

The current provenance story is strong in intent but incomplete in presentation. The main risks are:

1. Patched Apple source trees do not yet have a formal patch ledger.
2. Generated artifacts are mixed into the tracked repository.
3. Third-party dependencies use mixed intake models.
4. Some staged outputs live near source paths, which makes review less clear.
5. The repository does not yet provide a single clean statement of what is upstream, local patch, third-party, or generated.

## What Reviewers Should Be Able To See

For Panthera to be production-grade and straightforward for Apple engineers or OSS maintainers to review, the repository should make the following obvious:

- exact upstream version for each Apple component
- exact local modifications to upstream code
- exact list of Panthera-authored code paths
- exact list of third-party inputs and why they are present
- exact list of generated outputs and whether they belong in git
- exact build path from clean checkout to bootable guest

## Next Documents To Add

This file is now complemented by:

- `docs/provenance/APPLE_PATCHES.md`
- `docs/provenance/THIRD_PARTY.md`
- `docs/provenance/GENERATED_ARTIFACTS.md`
- `docs/provenance/REPRODUCIBILITY.md`

Those documents convert this high-level provenance statement into strict
engineering ledgers. They are starter ledgers as of Phase 2 and should be
updated in the same change as any source intake, patch, generated artifact, or
reproducibility policy change.
