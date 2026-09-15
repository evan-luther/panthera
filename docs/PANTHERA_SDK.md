# Panthera SDK

Status: host SDK plus first verified in-guest C compiler/linker path.

## Scope

The Panthera SDK is a generated build SDK assembled from Panthera's current
sysroot. It supports host-side cross builds that target Panthera's Darwin
userland and now pairs with an in-guest `/usr/bin/cc` toolchain for basic C
compile/link/run workflows.

Generated SDK:

- `userland/panthera_sdk/Panthera.sdk`
- `userland/panthera_sdk/Panthera.sdk.manifest.tsv`

Authored tools:

- `userland/panthera_sdk/build_panthera_sdk.sh`
- `userland/panthera_sdk/bin/panthera-cc`
- `userland/ld64/build_ld64.sh`
- `userland/ld64/bin/ld`
- `userland/clang/build_clang.sh`
- `userland/clang/bin/clang-17`
- `userland/clang/bin/cc`
- `userland/clang/lib/libLTO.dylib`
- `userland/libunwind/build_libunwind.sh`
- `tools/smoke_panthera_sdk.sh`
- `tools/panthera_sdk_hello.c`
- `tools/panthera_sdk_header_probe.c`
- `tools/audit_panthera_sdk.py`

## Build

```sh
userland/panthera_sdk/build_panthera_sdk.sh
```

The SDK generator copies Panthera headers and dylibs from
`userland/libsystem/build/sysroot`, creates framework compatibility links for
CoreFoundation, IOKit, and SystemConfiguration, writes SDK metadata, and records
a SHA-256 file manifest.

The generated SDK also includes Darwin/Xcode-style shape that mirrors the
useful parts of the ravynOS SDK found at
`/Users/admin/Downloads/ravynos-darwin/Developer/ravynOS.sdk`:

- `SDKSettings.json`
- `SDKSettings.plist`
- `SDKInfo.plist`
- `Entitlements.plist`
- `_PROVENANCE`
- `System.framework`
- `Kernel.framework`
- `usr/local/{bin,include,lib,libexec}`

Supplemental headers are copied only from Panthera source inputs already in the
workspace. Headers for unowned runtime areas are intentionally not imported just
to match another SDK's surface.

## Compile

```sh
userland/panthera_sdk/bin/panthera-cc tools/panthera_sdk_hello.c -o /tmp/panthera_sdk_hello
```

`panthera-cc` wraps the host Clang with Panthera defaults:

- target: `x86_64-apple-darwin23.0`
- deployment target: `14.0`
- sysroot: `userland/panthera_sdk/Panthera.sdk`
- framework search path: Panthera SDK frameworks
- library search paths: Panthera SDK `/usr/lib` and `/usr/lib/system`

## Verification

```sh
PANTHERA_SDK_SMOKE_TAG=panthera-sdk-hello-write-20260518 \
  bash tools/smoke_panthera_sdk.sh
```

The smoke builds the SDK, compiles `tools/panthera_sdk_hello.c`, stages the
result into the root image through the normal manifest/rootfs path under
`PANTHERA_STAGE_SDK_SMOKE=1`, boots QEMU, and runs the binary over OpenSSH.
It also compiles `tools/panthera_sdk_header_probe.c` with `-fsyntax-only` so
core Darwin headers are checked before the guest run.

Current passing evidence:

- `artifacts/boot/panthera-sdk-rayvn-shaped-20260518.log`
- `artifacts/boot/panthera-sdk-rayvn-shaped-20260518.ssh.log`
- `artifacts/boot/panthera-sdk-rayvn-shaped-20260518.sftp.log`
- `artifacts/sdk/panthera-sdk-rayvn-shaped-20260518-hello.file.txt`
- `artifacts/sdk/panthera-sdk-rayvn-shaped-20260518-hello.otool.txt`

## Audit

```sh
tools/audit_panthera_sdk.py \
  --reference /Users/admin/Downloads/ravynos-darwin/Developer/ravynOS.sdk
```

Current passing audit:

- `artifacts/sdk/panthera-sdk-audit-vs-ravynos-20260518.md`

The audit confirms the required Panthera SDK shape, core Darwin header set, and
reference comparison pass. The remaining ravynOS-only top-level headers are
mostly Carbon, copyfile/xattr, `math.h`, and `stdalign.h` surfaces. Those should
land only when Panthera has the corresponding runtime/library ownership or a
clear source owner, not as decorative copied headers.

## In-Guest Toolchain

The SDK now pairs with real Apple/ravynOS-derived compiler components staged
into the root image:

```sh
userland/ld64/build_ld64.sh
userland/clang/build_clang.sh
userland/libunwind/build_libunwind.sh
```

Current toolchain evidence:

- `artifacts/toolchain/panthera-ld64-smoke`
- `artifacts/toolchain/panthera-ld64-smoke.file.txt`
- `artifacts/toolchain/panthera-ld64-smoke.libs.txt`
- `artifacts/toolchain/panthera-ld64-smoke.otool.txt`
- `artifacts/boot/ld64-guest-smoke-libsystem-first-final-20260518.log`
- `artifacts/boot/ld64-guest-smoke-libsystem-first-final-20260518.ssh.log`
- `artifacts/boot/ld64-guest-smoke-libsystem-first-final-20260518.sftp.log`
- `artifacts/boot/clang-compile-only-guest-20260518g.log`
- `artifacts/boot/ld-direct-no-lto-guest-20260518d.log`
- `artifacts/boot/ld-error-handled-guest-20260518a.log`
- `artifacts/boot/cc-compile-link-guest-20260518h.log`
- `artifacts/boot/printf-exit-flush-20260518a.log`
- `artifacts/boot/guest-toolchain-20260518f.log`
- `artifacts/release/pkgsrc-prereqs-20260518-sdk.md`

This closes the pkgsrc `ld` blocker with a real x86_64 Mach-O linker staged as
`/usr/bin/ld` and the pkgsrc `cc` blocker with `/usr/bin/cc` backed by
`/usr/bin/clang`. The compiler resource directory is staged at
`/usr/lib/clang/17`, and LLVM's `libLTO.dylib` is staged at
`/usr/lib/libLTO.dylib` because Clang's Darwin driver passes it to `ld64`.

The next pkgsrc frontier is no longer the compiler/linker prerequisite set.
The 2026-05-18 archive-runtime pass proved guest `bsdtar`, pkgsrc normal-file
extraction, and pkgsrc symlink extraction after fixing libarchive zlib install
names plus Darwin-compatible `futimens`, `utimensat`, `lutimes`, `removefile`,
and `lchmod` surfaces. The full pkgsrc bootstrap smoke currently times out
during full-tree extraction before the bootstrap script begins; see
`artifacts/boot/pkgsrc-bootstrap-20260518g.summary.md`.

The guest `ld64` and `cc` smokes required correcting dyld/libSystem initializer
ordering, adding Darwin TLV bootstrap and dyld image APIs, integrating LLVM
libunwind into libSystem, and staging the standard Darwin
`libSystem.dylib -> libSystem.B.dylib` linker compatibility symlink. The
current guest path compiles C to an object, links a Mach-O executable with
Apple `ld64`, runs the produced binary, and handles normal `ld64` diagnostic
exceptions without aborting.

Use the consolidated guest regression before changing the compiler, linker,
dyld, libc exit/stdio, or rootfs staging path:

```sh
bash tools/smoke_guest_toolchain.sh --rebuild-rootfs
```

That gate proves `/bin/printf` redirection, Clang compile-only,
`/usr/bin/cc` compile/link/run, direct `ld64` link/run, `ar`/`ranlib`, and an
intentional missing-library linker diagnostic over OpenSSH. The 2026-05-18
dyld fix behind this routes LC_MAIN return through libc `exit(3)` so stdio and
atexit cleanup run before process termination.
