# Panthera Third-Party Dependency Ledger

Status: Phase 8 release-readiness ledger.

Last updated: 2026-05-16.

## Purpose

This document records non-Apple inputs used by Panthera. The goal is to make
third-party provenance, versioning, licensing, and reproducibility visible.

Apple open-source releases belong in `docs/provenance/APPLE_PATCHES.md` and the
main provenance inventory. This file is for non-Apple projects and mixed-origin
portable packages.

## Policy

Every third-party dependency should eventually have:

- upstream project name
- version or commit
- source URL or origin
- license
- intake model: vendored, fetched during build, or generated from local source
- checksum or commit verification
- build script
- staged output paths
- default-boot requirement status

Fetch-at-build dependencies must be pinned by version and checksum. Git clones
must be pinned by commit, not only by branch or depth.

Local tarball checksums are recorded in:

- `docs/provenance/THIRD_PARTY_CHECKSUMS.txt`

Verify the currently present tarball inputs with:

```bash
tools/verify_third_party_checksums.sh
```

The manifest records required checksums for all tracked archive inputs. The
fetch-at-build scripts (including Bash, bmake, cctools, curl, LLVM libunwind,
Nano, OpenSSH, OpenSSL, OpenZFS, and zlib) invoke `tools/fetch_with_checksum.sh`,
ensuring missing tarballs are downloaded into `build/distfiles/` and verified
against `docs/provenance/THIRD_PARTY_CHECKSUMS.txt` before extraction.

## Current Intake Models

| Model | Meaning | Current Risk |
|---|---|---|
| Vendored or unpacked source under `src/` | Source exists in the tree or local checkout | Version may be clear, but origin/checksum may not be recorded |
| Fetched during build into `build/distfiles` | Build script downloads tarball if missing | Many scripts do not verify checksums yet |
| Sparse git clone during build | Build script pulls from upstream git | Not reproducible unless pinned to a commit |
| Generated or staged outputs under `userland/` | Built binaries/dylibs are present near source | Source review can confuse output with authored code |

## Fetched During Build

| Component | Version | Source | Build Script | Checksum Status | Default Boot? | Notes |
|---|---:|---|---|---|---|---|
| Bash | `5.2.37` | `https://ftp.gnu.org/gnu/bash/bash-5.2.37.tar.gz` | `userland/bash/build_bash.sh` | Enforced through `tools/fetch_with_checksum.sh` | Optional shell/tooling | Current tarball verifies. |
| bmake | `20260508` | `https://ftp.netbsd.org/pub/NetBSD/misc/sjg/bmake-20260508.tar.gz` | `userland/bmake/build_bmake.sh` | Enforced through `tools/fetch_with_checksum.sh` | Package bootstrap tool | Built with upstream `boot-strap`, filemon disabled, and forced x86_64 `MACHINE`/`MACHINE_ARCH` for Panthera guest behavior. |
| cctools | `1030.6.3` (`920a2b4`) | `https://github.com/apple-oss-distributions/cctools/archive/920a2b45080fb9badf31bf675f03b19973f0dd4f.tar.gz` | `userland/cctools/build_cctools.sh` | Enforced through `tools/fetch_with_checksum.sh` | Package bootstrap tools | Stages Apple cctools `ar` and `ranlib`; linker/compiler driver are tracked separately. |
| curl | `8.12.1` | `https://curl.se/download/curl-8.12.1.tar.xz` | `userland/curl/build_curl.sh` | Enforced through `tools/fetch_with_checksum.sh` | Optional networking tool | Depends on OpenSSL and zlib staging. |
| LLVM libunwind | `18.1.8` | `https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/libunwind-18.1.8.src.tar.xz`, `https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/cmake-18.1.8.src.tar.xz`, `https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/runtimes-18.1.8.src.tar.xz` | `userland/libunwind/build_libunwind.sh` | Enforced through `tools/fetch_with_checksum.sh` | Yes | Apache-2.0 WITH LLVM-exception. Fetches and extracts sibling `libunwind`, `cmake`, and `runtimes` trees under `build/llvm-project-18.1.8`. Companion `cmake` and `runtimes` archives supply LLVM CMake modules (e.g. `HandleLLVMOptions.cmake`) required for standalone builds. Optional `PANTHERA_LLVM_ROOT` provides a developer override to a local checkout. |
| Nano | `8.7.1` | `https://www.nano-editor.org/dist/v8/nano-8.7.1.tar.xz` | `userland/nano/build_nano.sh` | Enforced through `tools/fetch_with_checksum.sh` | Optional editor | Staged under `userland/nano/`. |
| OpenSSH portable | `9.7p1` | `https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-9.7p1.tar.gz` | `userland/openssh/build_openssh.sh` | Enforced through `tools/fetch_with_checksum.sh` | Yes | Staged by `manifests/ssh.system`; `artifacts/release/alpha-release-20260518-r2.summary.md` proves host-forwarded SSH, remote exec, SFTP, `pselect()`/`SIGCHLD`, and signal-trampoline behavior. `openssh-9.9p1.tar.gz` is also present and checksummed, but the current build script uses `9.7p1`. |
| OpenSSL | `3.5.5` | `https://github.com/openssl/openssl/releases/download/openssl-3.5.5/openssl-3.5.5.tar.gz` | `userland/openssl/build_openssl.sh` | Enforced through `tools/fetch_with_checksum.sh` | Dependency for curl/OpenSSH | Uses stage output under `userland/openssl/stage/`. |
| OpenZFS on macOS fork | `zfs-macOS-2.3.1p1` (`e227f8a95eb019ac5b7f770902de79de1e761eca`) | `https://github.com/openzfsonosx/openzfs-fork/archive/refs/tags/zfs-macOS-2.3.1p1.tar.gz` | `tools/prepare_openzfs_source.sh`, `kexts/zfs/build_spl.sh` | Enforced through `tools/fetch_with_checksum.sh`; tag object `3a0b79729d93657247c2443744c4b9fb01b384b1` peels to commit `e227f8a95eb019ac5b7f770902de79de1e761eca` | Future storage platform | Initial Panthera intake is SPL-first. `zfs.kext`, userland tools, and root migration remain gated behind successful SPL load evidence. |
| zlib | `1.3.1` | `https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz` | `userland/zlib/build_zlib.sh` | Enforced through `tools/fetch_with_checksum.sh` | Library dependency | Stages to sysroot and `userland/zlib/stage/`. |

### LLVM libunwind 18.1.8 Details

- **Version:** `18.1.8`
- **Source URLs:**
  - `https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/libunwind-18.1.8.src.tar.xz`
  - `https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/cmake-18.1.8.src.tar.xz`
  - `https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/runtimes-18.1.8.src.tar.xz`
- **License:** Apache-2.0 WITH LLVM-exception
- **Intake model:** Fetched during build into `build/distfiles/`
- **Checksum enforcement:** Enforced through `tools/fetch_with_checksum.sh` against `docs/provenance/THIRD_PARTY_CHECKSUMS.txt`:
  - `build/distfiles/libunwind-18.1.8.src.tar.xz` (`c31577d16978b0da0e472ef751f74893a5b459a7ea4a383b75f7ab93cf1e6877`)
  - `build/distfiles/cmake-18.1.8.src.tar.xz` (`59badef592dd34893cd319d42b323aaa990b452d05c7180ff20f23ab1b41e837`)
  - `build/distfiles/runtimes-18.1.8.src.tar.xz` (`9997c2e91e5438e2963306ba5019d85b5384b467535632738d8670ced8f07cb3`)
- **Build script:** `userland/libunwind/build_libunwind.sh`
- **Staged outputs:** `userland/libunwind/lib/libunwind.a`, `userland/libunwind/obj/UnwindRegistersSave.o`, `userland/libunwind/obj/UnwindRegistersRestore.o` (integrated into `libSystem.dylib` / `libSystem.B.dylib`)
- **Default boot requirement:** Yes (foundation unwinding and exception handling runtime support in libSystem)
- **Companion archives requirement:** LLVM's standalone `libunwind` CMake configuration depends on shared CMake infrastructure (such as `HandleLLVMOptions.cmake` and runtime build rules) located in the LLVM `cmake` and `runtimes` directories. The build script extracts all three archives into sibling directories (`libunwind`, `cmake`, `runtimes`) under `build/llvm-project-18.1.8` to enable clean standalone configuration without requiring the full LLVM monorepo git repository.
- **Local override:** Setting `PANTHERA_LLVM_ROOT` to a local directory containing a `libunwind` subdirectory overrides automated fetching and builds directly from that local tree.

### Vendored LLVM scalar power implementation

- **Upstream:** [`llvmorg-17.0.6/libc/AOR_v20.02/math`](https://github.com/llvm/llvm-project/tree/llvmorg-17.0.6/libc/AOR_v20.02/math).
- **Local files:** `userland/libsystem/build/shims/libsystem_c/math/`
  contains unchanged `pow.c`, `pow_log_data.c`, `exp_data.c`, `math_err.c`,
  and `math_config.h`, plus upstream `LICENSE.TXT`.
- **License:** Apache-2.0 WITH LLVM-exception. File hashes are recorded in
  `docs/provenance/THIRD_PARTY_CHECKSUMS.txt`; intake matched pinned upstream bytes.
- **Ownership:** native `libsystem_c`; both libc build entrypoints compile
  the scalar non-FMA path, with errno/rounding support and FP contraction off.
  This replaces the self-recursive `__builtin_pow` bridge, not the other
  math functions. No host `libsystem_m` or build-time download is needed.
- **Behavior check:** `tools/libsystem_primitives_probe.c` calls the exported
  function through a volatile pointer and covers signed/domain/special values,
  overflow, subnormal underflow, and a non-integral exponent.

## Git Fetches During Build

| Component | Source | Build Script | Pin Status | Risk | Required Action |
|---|---|---|---|---|---|
| Vim `xdiff/` supplement | `https://github.com/vim/vim.git` sparse checkout of `src/xdiff` | `userland/vim/build_vim.sh` | Unpinned depth-1 clone | Not reproducible | Pin to a commit or vendor the required `xdiff/` subtree with license note. |

## Source Trees Or Local Inputs Under `src/`

These are non-Apple or mixed-origin inputs present under `src/` and used by
Panthera build scripts.

| Component | Version / Path | Build Script | Intake Status | Default Boot? | Required Action |
|---|---|---|---|---|---|
| dash | `src/dash-0.5.12` | Not currently central | Unpacked source | No | Record origin URL/checksum or remove from default path. |
| Dropbear | `src/dropbear-2024.86` | historical starter plan only | Unpacked source | Superseded | OpenSSH portable is the active alpha SSH implementation; keep Dropbear only as historical/provenance material unless it is deliberately revived. |
| GNU awk or awk source | `src/awk` | `userland/awk/build_awk.sh` | Local source tree | Optional utility | Identify upstream and version. |
| bzip2 | `src/bzip2` | `userland/bzip2/build_bzip2.sh` | Local source tree | Optional utility | Identify upstream and version. |
| FreeBSD libc | `src/freebsd-libc` | libSystem support | Local source tree | Foundation support | Document origin and license files. |
| less | `src/less` | `userland/less/build_less.sh` | Local source tree | Optional utility | Identify upstream and version. |
| libiconv | `src/libiconv-1.17` | libSystem/userland support | Versioned local source | Likely support library | Record origin and checksum. |
| ncurses | `src/ncurses-6.5` | ncurses/userland tools | Versioned local source | Shell/terminfo support | Record origin and checksum. |
| PCRE | `src/pcre-21/pcre-8.44.tar.bz2` plus Apple patches | `userland/pcre/build_pcre.sh` | Tarball inside Apple-style source tree; checksum recorded | Optional library | Patch provenance still needs source-level review. |
| screen | `src/screen` | Not currently central | Local source tree | No | Identify upstream/version or exclude from default build. |
| Vim | `src/vim/src` plus fetched `xdiff/` | `userland/vim/build_vim.sh` | Local source plus unpinned supplement | Optional editor | Pin supplement and record source version. |

## Vendored Non-Apple Data

| Component | Path | Role | Required Action |
|---|---|---|---|
| user template | `vendor/usertemplate-109` | Guest user template payload | Document origin, license, and whether it is Apple OSS or vendored data. |

## Required Follow-Up

1. Keep `tools/verify_third_party_checksums.sh` passing after source intake
   changes.
2. Pin Vim `xdiff/` to a commit or vendor it explicitly.
3. Decide whether `userland/*/bin` and `userland/*/lib` outputs remain tracked
   transitional artifacts or move fully to generated artifact policy.
4. Identify origins for local source trees without versioned directory names.

## Intake Template

Use this template when adding a new third-party input:

```md
### Component Name

- Version:
- Source URL:
- License:
- Intake model:
- Checksum or commit:
- Build script:
- Staged outputs:
- Default boot requirement:
- Notes:
```
