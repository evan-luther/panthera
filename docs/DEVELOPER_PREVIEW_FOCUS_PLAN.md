# Panthera Developer Preview Focus Plan

Status: focused post-alpha execution plan.

This plan narrows Panthera's next engineering phase to three tracks:

1. pkgsrc bootstrap and package building
2. Panthera SDK/toolchain maturity
3. Apple dyld replacement

The tracks are not independent. The SDK/toolchain is the contract that pkgsrc
and Apple dyld must build against. pkgsrc is the first broad consumer of that
contract. Apple dyld is a staged replacement for the current custom dyld and
must not displace the passing boot/runtime path until it can satisfy the same
gates.

## Ground Rules

- Keep the current alpha gate green before and after every release-impacting
  change.
- Prefer Apple/Darwin source and semantics over Panthera-specific workarounds.
- Do not add fallback behavior to hide missing runtime surfaces.
- Keep the current custom dyld as the bootable baseline until Apple dyld has a
  verified replacement path.
- Treat pkgsrc failures as system integration evidence: fix libc, dyld,
  filesystem, toolchain, or daemon semantics at the owning layer.
- Keep diagnostics gated and documented.

## Track 1: SDK And Toolchain

Goal: make Panthera a coherent Darwin-like target for host cross builds and
in-guest builds.

Current state:

- `userland/panthera_sdk/Panthera.sdk` exists.
- `userland/panthera_sdk/bin/panthera-cc` exists.
- SDK audit and host/guest SDK smoke have passed previously.
- In-guest `/usr/bin/cc`, Apple `ld64`, `ar`, and `ranlib` smokes have passed.

Immediate work:

1. Re-run the SDK audit and guest toolchain smoke after the final alpha gate.
2. Make the SDK artifact self-describing:
   - version file
   - source manifest
   - sysroot manifest
   - toolchain manifest
   - known unsupported surfaces
3. Split SDK validation into stable checks:
   - SDK shape audit
   - header compile probe
   - host cross compile/link probe
   - in-guest compile/link/run probe
   - linker diagnostic probe
4. Add a pkgsrc-oriented SDK probe that compiles common configure tests:
   - headers: `sys/param.h`, `sys/types.h`, `sys/stat.h`, `sys/time.h`,
     `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `pthread.h`, `dlfcn.h`
   - functions: `fork`, `execve`, `waitpid`, `pipe`, `select`, `pselect`,
     `poll`, `socket`, `getaddrinfo`, `dlopen`, `pthread_create`
5. Only add headers when Panthera has a real owner for the runtime/API surface.

Definition of done:

- `tools/audit_panthera_sdk.py` passes.
- `tools/smoke_panthera_sdk.sh` passes.
- `tools/smoke_guest_toolchain.sh --rebuild-rootfs` passes.
- A pkgsrc configure-probe smoke passes without decorative headers or stub-only
  runtime lies.

## Track 2: pkgsrc

Goal: bootstrap pkgsrc and build the first real packages on Panthera.

Current state:

- `tools/audit_pkgsrc_prereqs.py` previously passed for prerequisite coverage.
- `tools/smoke_pkgsrc_bootstrap.sh` exists.
- The latest documented full bootstrap did not reach the bootstrap script. It
  timed out during full pkgsrc tree extraction.
- The prior extraction warning floods were already resolved:
  - libarchive zlib install names
  - timestamp restore surfaces
  - symlink permission surfaces
  - `/bin/rm` `_removefile` runtime bind

Immediate work:

1. Re-run the narrow extraction probe against the current alpha-ready rootfs.
2. If full-tree extraction still stalls, fix the storage/filesystem owner path
   rather than changing pkgsrc:
   - `DKIOCSYNCHRONIZECACHE`
   - `IOMedia::synchronize()`
   - `IOBlockStorageDriver`
   - concrete ATA cache-flush behavior
   - HFS journal flush behavior
3. Add an extraction timing/throughput artifact:
   - file count
   - directory count
   - elapsed time
   - final tree size
   - last journal/cache-flush markers
4. Once extraction completes, run `pkgsrc/bootstrap/bootstrap` and fix the first
   real configure/build failure at its owning layer.
5. Promote a first package build smoke after bootstrap succeeds.

Definition of done:

- Full pkgsrc tree extraction completes in guest.
- `pkgsrc/bootstrap/bootstrap` completes with Panthera `/usr/bin/cc` and
  `/usr/bin/ld`.
- `/usr/pkg/bin/bmake` from pkgsrc runs.
- One simple package builds and installs.
- A package-built binary runs over OpenSSH.

## Track 3: Apple dyld

Goal: replace the custom Panthera dyld with Apple `dyld-1122.1.2` or a
minimally patched Apple-derived dyld that can boot Panthera and pass the same
runtime gates.

Current state:

- Apple dyld source is present at `src/dyld-1122.1.2`.
- Panthera currently boots through `userland/dyld/panthera_dyld.cpp`.
- The custom dyld has accumulated required Panthera bring-up behavior:
  - shared cache loading
  - chained fixups
  - ordinal/reexport resolution
  - libSystem initialization ordering
  - dyld image API surfaces
  - LC_MAIN return through libc `exit(3)`

Immediate work:

1. Create an Apple dyld build ledger:
   - source files needed for x86_64 dyld
   - required private headers
   - required libSystem/libc/kernel interfaces
   - required entitlements/code-signing assumptions
   - Panthera-specific patches, if any
2. Attempt a host build of Apple dyld as a non-staged artifact first.
3. Compare Apple dyld's expected kernel/libSystem contract with Panthera's
   current contract:
   - `__DATA_CONST` fixups and protections
   - shared cache mapping interfaces
   - dyld process info APIs
   - libSystem initializer sequencing
   - thread-local variables
   - image notification APIs
4. Add an alternate-dyld smoke path that can boot or run selected binaries
   without replacing the default dyld.
5. Only promote Apple dyld to `/usr/lib/dyld` after it passes:
   - simple binary
   - CoreFoundation binary
   - zsh
   - launchd child process
   - OpenSSH
   - SDK/toolchain smoke
   - alpha gate

Definition of done:

- Apple dyld builds reproducibly from `src/dyld-1122.1.2`.
- A non-default Apple dyld smoke runs selected Panthera binaries.
- Apple dyld can run zsh, launchd children, OpenSSH, and toolchain binaries.
- Default `/usr/lib/dyld` can be switched to Apple dyld and the alpha gate still
  passes.

## Execution Order

1. Freeze and document the final alpha gate evidence.
2. Re-run SDK/toolchain audits to establish the developer-preview baseline.
3. Fix pkgsrc full-tree extraction, starting with the storage synchronize path
   if the current timeout reproduces.
4. Run pkgsrc bootstrap and fix real configure/build blockers.
5. In parallel only after the baseline is stable, start Apple dyld build
   ledger and non-staged build work.
6. Add alternate-dyld smoke tests.
7. Promote Apple dyld only after it passes the same release-impacting gates as
   the custom dyld.

## First Concrete Target

The next narrow engineering target is pkgsrc full-tree extraction on the
alpha-ready rootfs. It is the first blocker that exercises the SDK/toolchain
future without destabilizing the dyld baseline.

Run:

```sh
bash tools/smoke_pkgsrc_extract_probe.sh --rebuild-rootfs
```

If it still stalls or times out, inspect and fix the storage synchronize/cache
flush path before retrying pkgsrc bootstrap.
