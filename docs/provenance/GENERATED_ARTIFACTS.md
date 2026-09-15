# Panthera Generated Artifacts Ledger

Status: Phase 8 release-readiness ledger.

Last updated: 2026-05-16.

## Purpose

This document records paths that are generated, staged, or runtime-derived
rather than authoritative source.

Generated outputs are allowed during bring-up, but they must not be confused
with Panthera-authored source or upstream inputs. The long-term goal is that a
clean checkout can rebuild these paths or fetch them as release artifacts.

## Artifact Classes

| Class | Meaning | Source Review Policy |
|---|---|---|
| `build-output` | Compiler, linker, object, or intermediate output | Do not review as source |
| `staging-output` | Files copied into a guest/sysroot/EFI staging tree | Review producer scripts and manifests instead |
| `runtime-output` | Logs, images, QEMU state, boot artifacts | Keep under `artifacts/` or release artifact storage |
| `generated-source` | MIG, configure, or generated C/header files | Track only when regeneration is unavailable or intentionally pinned |
| `transitional-binary` | Built binary currently tracked for bring-up convenience | Document as exception and plan removal or release-artifact migration |

## Current Ledger

| Path | Class | Producer | Current Policy | Review Target | Next Action |
|---|---|---|---|---|---|
| `BUILD/` | `build-output` | `build/build_xnu.sh` and XNU build system | Ignored | Build scripts, config, patches | Keep ignored. |
| `build/obj/`, `build/dst/`, `build/sym/` | `build-output` | toolchain and package scripts | Ignored | Producer scripts | Keep ignored. |
| `build/header_staging/`, `build/toolstubs/` | `staging-output` | `build/build_xnu.sh` | Generated | Header staging logic | Keep generated. |
| `build/distfiles/` | `runtime-output` | fetch-at-build scripts | Cache only | Third-party checksum manifest | Add checksum verification. |
| `boot/efi/loader/out/` | `build-output` | `boot/efi/build_bootx64.sh` | Ignored | Loader source | Keep ignored. |
| `boot/efi/staging/` | `staging-output` | `boot/efi/stage_phase2_efi.sh` | Ignored | Stage script and manifest | Keep ignored; do not review staged EFI tree as source. |
| `kexts/OpenIOKit/build/` | `build-output` | `kexts/OpenIOKit/build_all.sh` | Ignored | Kext build scripts and plists | Keep ignored. |
| `userland/libsystem/build/sysroot/` | `staging-output` | libSystem build/relink scripts | Ignored but critical | Build scripts, shim sources, verification logs | Keep generated; release artifacts may carry built dylibs. |
| `userland/panthera_sdk/Panthera.sdk/` | `staging-output` | `userland/panthera_sdk/build_panthera_sdk.sh` | Ignored | SDK generator, `panthera-cc`, SDK smoke, SDK audit | Generated from Panthera sysroot plus Panthera-owned source headers; do not edit SDK contents by hand. |
| `userland/panthera_sdk/Panthera.sdk.manifest.tsv` | `staging-output` | `userland/panthera_sdk/build_panthera_sdk.sh` | Ignored | SDK generator and smoke logs | Regenerate with the SDK. |
| `images/shared_cache/` | `staging-output` | `tools/build_shared_cache.sh` | Ignored | Shared-cache builder source | Keep generated. |
| `images/qemu/*.img` | `runtime-output` | `rootfs/create_zfs_root_image.sh`, `rootfs/create_hfs_root_image.sh`, QEMU runtime | Ignored | Rootfs assembly scripts | Keep out of source; booting changes mtime/content. |
| `artifacts/` | `runtime-output` | tests, boot runs, audits | Ignored | Summaries referenced by docs | Keep logs here; cite exact paths in docs. |
| `userland/launchd/obj/` | `build-output` and generated MIG/object area | launchd build flow | Ignored | `userland/launchd/build_launchd.sh`, source inputs | Keep ignored. |
| `userland/notifyd/mig_gen/` | `generated-source` | `userland/notifyd/build_notifyd.sh` | Currently untracked generated output | MIG definitions and build script | Keep generated unless a pinned generated source decision is made. |
| `userland/notifyd/obj/` | `build-output` | `userland/notifyd/build_notifyd.sh` | Currently untracked generated output | Build script | Keep generated. |
| `userland/iokituser/bin/`, `userland/iokituser/lib/` | `staging-output` | `userland/iokituser/build_iokituser.sh` | Ignored | Source/build script | Keep generated. |
| `userland/icu/build-host/`, `userland/icu/build-target/` | `build-output` | `userland/icu/build_icu.sh` | Some tracked outputs currently exist | Build script and source | Classify tracked outputs and move to artifact policy. |
| `userland/*/bin/`, `userland/*/lib/` | `transitional-binary` | package build scripts | Mixed: many tracked | Build scripts and source inputs | Decide per component whether to untrack or mark as release artifact. |
| `tools/build_shared_cache*` binaries | `build-output` | C compiler from `tools/build_shared_cache*.c` | Ignored for known binaries | C source | Keep binaries ignored. |
| `tests/test_*` binaries and `*.dSYM/` | `build-output` | test builds | Ignored except `tests/test_*.c` sources | Test source | Keep generated outputs ignored. |
| `.DS_Store` | `runtime-output` | macOS Finder | Ignored | None | Remove if tracked. |

## Transitional Binary Outputs

The repository currently tracks many files under `userland/*/bin` and
`userland/*/lib`. During bring-up this made rootfs assembly simpler, but for
source review these files are generated outputs.

Until Phase 4 splits component builds from rootfs assembly, treat these as
transitional binary artifacts:

- do not review binary diffs as source
- review the build script and source inputs instead
- update `docs/CURRENT_STATE.md` when a tracked binary is part of the verified
  baseline
- prefer moving long-term distributable binaries to release artifacts

## Generated Source Policy

Generated C or header files may be tracked only when:

- the generator is unavailable on normal hosts
- the generated output is intentionally pinned for compatibility
- the generating command and input files are documented

Otherwise, generated source should be regenerated by the build and kept out of
normal source review.

## Required Follow-Up

1. Keep `tools/verify_third_party_checksums.sh` passing for local tarball
   inputs before treating a checkout as release-reviewable.
2. Keep fetch-at-build scripts on `tools/fetch_with_checksum.sh` for every
   tarball listed in `docs/provenance/THIRD_PARTY_CHECKSUMS.txt`.
3. Audit tracked files under `userland/*/bin`, `userland/*/lib`, and ICU build
   directories.
4. Decide which transitional binaries remain temporarily tracked.
5. Add path-local README files for any generated output that must remain
   tracked.
6. Ensure rootfs assembly fails clearly when a required generated artifact is
   missing.
7. Move release images and caches to release artifact handling, not source
   state.
