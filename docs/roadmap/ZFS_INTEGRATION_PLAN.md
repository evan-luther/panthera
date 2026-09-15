# Panthera ZFS Filesystem Migration Plan

Status: active architectural plan.

This plan supersedes the older "ZFS as writable data volume" outline. The end
state is Panthera booting with ZFS as the primary root filesystem. A hybrid
HFS/ZFS layout is still useful as a bring-up milestone, but it is not the final
architecture.

Verified baseline before this work:

- Alpha gate `artifacts/release/alpha-release-20260521-weak-coalesce-r16.summary.md`
  passes.
- The original root image was a generated raw HFS+ disk image at
  `images/qemu/panthera-root.img`.
- Root image creation is owned by `rootfs/create_hfs_root_image.sh`.
- Early launchd still contains an HFS-specific `mount("hfs", "/", MNT_UPDATE,
  ...)` remount path in `userland/launchd/real/panthera_boot.c`.
- The standalone `userland/mount_rw/mount_rw.c` helper is also HFS-specific.
- EFI staging currently includes `HFS.kext` and `HFSEncodings.kext`.

## Objective

Move Panthera from an HFS+ root filesystem to a ZFS-backed root system without
masking missing kernel, VFS, block storage, or userland behavior behind
fallbacks.

The final state should have:

- ZFS kernel support loaded during early boot.
- ZFS registered as a Darwin VFS filesystem type.
- `zpool` and `zfs` userland tools built for Panthera and staged into the
  system.
- A reproducible root image builder that can create and validate ZFS-backed
  Panthera images.
- Boot verification proving that `/` is ZFS, not HFS+.
- HFS+ retained only as a compatibility/bootstrapping filesystem, not as the
  default Panthera runtime root.

## Architecture

### Final Target: ZFS Root

The preferred final disk layout is:

```text
GPT disk
  Partition 1: ESP/FAT32
    EFI loader
    XNU kernel
    boot kexts, including SPL and ZFS

  Partition 2: ZFS pool
    tank/ROOT/panthera
    tank/var
    tank/tmp
    tank/Users
    tank/Library
```

The EFI loader does not need to read ZFS if it continues loading the kernel and
boot kexts from the ESP. The kernel and ZFS kext own root mounting after that.

### Bring-Up Milestone: Hybrid HFS/ZFS

Use a temporary hybrid layout only to reduce risk while bringing up the ZFS kext
and tools:

```text
GPT disk
  Partition 1: ESP/FAT32
  Partition 2: HFS+ minimal boot/root scaffold
  Partition 3: ZFS pool
```

This milestone is acceptable only if it proves real ZFS kernel and userland
behavior. It must not become the new release target.

## Non-Negotiables

- Do not implement ZFS through FUSE.
- Do not fake `zpool`, `zfs`, mount, or VFS behavior in userland.
- Do not keep HFS+ as a silent runtime fallback once a ZFS boot path is marked
  passing.
- Do not skip SPL. OpenZFS on Darwin depends on the Solaris Porting Layer.
- Do not remove HFS+ support from Panthera; keep it available for compatibility
  and recovery images.
- Do not overwrite the known-good alpha HFS image while developing this. Build
  ZFS images under separate names until the ZFS gate is green.

## Source Strategy

Primary source candidate:

- OpenZFS on macOS / openzfsonosx fork.

Required first step:

1. Import or vendor a pinned OpenZFS source snapshot under `src/` or
   `vendor/`.
2. Record source URL, tag/commit, license, and local patches in a Panthera
   source ledger.
3. Build SPL and ZFS from source as part of Panthera, not by copying host kexts.

The old Apple ZFS sources can be used as reference material, but the practical
implementation path should use the maintained Darwin/OpenZFS port unless the
source audit proves it incompatible with Panthera's XNU baseline.

## Phase Z0: Evidence and Design Freeze

Goal: establish a clean baseline and avoid destabilizing alpha.

Tasks:

1. Preserve the passing HFS alpha evidence.
2. Add a storage status section to `docs/CURRENT_STATE.md`.
3. Add a ZFS worktree rule: all ZFS images must be created as separate files,
   for example `images/qemu/panthera-zfs-root.img`.
4. Add an explicit release gate expectation: until ZFS is green, the HFS alpha
   gate remains the known-good baseline.
5. Inventory HFS assumptions:
   - `rootfs/create_hfs_root_image.sh`
   - `userland/launchd/real/panthera_boot.c`
   - `userland/mount_rw/mount_rw.c`
   - EFI kext staging
   - QEMU root disk scripts
   - rootfs ownership fixups that patch HFS catalog records directly

Exit criteria:

- Current HFS alpha remains reproducible.
- ZFS work begins on separate image/tool paths.
- HFS-specific assumptions are listed with owners.

## Phase Z1: SPL Kext

Goal: build and boot-load `spl.kext` on Panthera.

Status 2026-05-21: complete for the opt-in boot path.

Evidence:

- `artifacts/zfs/spl-build-20260521-r15.log`
- `artifacts/boot/zfs-load-r6-20260521.log`
- `artifacts/boot/zfs-load-r6-20260521.summary.md`

Notes:

- SPL is built from pinned OpenZFS source as `net.lundman.spl`.
- SPL starts during the opt-in ZFS boot and reaches `SPL: loading` without the
  earlier sysctl lockdown panic.
- SPL now owns SPL runtime state and exposes retain/release semantics for ZFS
  instead of relying on ZFS to re-enter `spl_start()`.

Tasks:

1. Vendor the OpenZFS Darwin source snapshot.
2. Maintain the SPL build glue in `kexts/zfs/build_spl.sh`, following
   Panthera's existing kext build conventions.
3. Build the SPL kext against Panthera's XNU and IOKit headers.
4. Audit every missing KPI symbol. Fix by implementing the missing Darwin
   behavior or exporting the real kernel symbol when appropriate.
5. Stage `spl.kext` into the EFI boot kext set.
6. Boot with SPL only.

Exit criteria:

- SPL loads during boot.
- No panic.
- No broad stub/fallback KPI behavior is introduced.
- Boot log proves SPL init and normal alpha boot still reaches login.

## Phase Z2: ZFS Kext

Goal: build and load `zfs.kext`, with ZFS registered in XNU VFS.

Status 2026-05-21: complete for the opt-in boot path.

Evidence:

- `artifacts/zfs/zfs-build-20260521-r27.log`
- `artifacts/boot/zfs-load-r6-20260521.log`
- `artifacts/boot/zfs-load-r6-20260521.summary.md`
- `artifacts/boot/zfs-zsysctl-20260521-r1.log`
- `artifacts/boot/zfs-zsysctl-20260521-r1.summary.md`
- `artifacts/boot/zfs-zsysctl-20260521-r1.ssh.log`
- `artifacts/boot/zfs-zsysctl-20260521-r1.sftp.log`

The r6 boot proves `org.openzfsonosx.zfs` loads after `net.lundman.spl`,
registers its IOService classes, reaches `ZFS: Loaded module v2.3.1-panthera`,
and still reaches the Panthera login prompt. `/` is still HFS in this proof.
The r1 `zsysctl` smoke proves the live guest can query the loaded ZFS kext
through the native sysctl path: `zfs.kext_version: 2.3.1-panthera`.

Tasks:

1. Create `kexts/zfs/` build glue.
2. Build the ZFS kext against the same pinned source snapshot and SPL headers.
3. Stage `zfs.kext` after `spl.kext`.
4. Verify dependency ordering and bundle identifiers.
5. Boot and confirm:
   - SPL loads.
   - ZFS loads.
   - ZFS registers its VFS ops.
   - no panic under idle boot.

Exit criteria:

- Boot log proves ZFS kext initialization.
- A small in-kernel probe or userland query can confirm the `zfs` filesystem
  type is registered.

## Phase Z3: ZFS Userland

Goal: build native Panthera `zpool` and `zfs` tools.

Status 2026-05-21: complete for the opt-in boot path.

Evidence:

- `userland/zfs/build_zfs_userland.sh` builds OpenZFS macOS `zsysctl`, `zpool`,
  `zfs`, and the required OpenZFS support archives.
- `userland/diskarbitration/build_diskarbitration.sh` builds Panthera's native
  `libDiskArbitration.dylib` surface for the DiskArbitration calls used by the
  OpenZFS macOS backend.
- OpenZFS XDR support is built from Apple `Libinfo-583.0.1` RPC/XDR sources.
- `manifests/zfs.system` stages `/usr/sbin/zsysctl`, `/sbin/zpool`, `/sbin/zfs`,
  and the DiskArbitration dylib/framework only when the ZFS manifest is
  selected.
- `artifacts/boot/zfs-zsysctl-20260521-r1.ssh.log` proves
  `/usr/sbin/zsysctl zfs.kext_version` returns `2.3.1-panthera` inside
  Panthera with `spl.kext` and `zfs.kext` loaded.
- `artifacts/boot/zfs-tools-20260521-r1.ssh.log` proves `/sbin/zpool version`,
  `/sbin/zfs version`, and `/usr/sbin/zsysctl zfs.kext_version` all return
  status 0 inside Panthera and report `2.3.1-panthera`.
- `artifacts/boot/zfs-tools-20260521-r1.sftp.log` proves SFTP still works on
  the same opt-in ZFS boot.

Tasks:

1. Build the first native OpenZFS macOS utility and stage it behind the opt-in
   ZFS manifest.
2. Build minimum required libraries:
   - `libzfs`
   - `libzfs_core`
   - `libnvpair`
   - `libuutil`
   - other required OpenZFS support libraries discovered by configure/build
3. Build:
   - `/sbin/zpool`
   - `/sbin/zfs`
4. Link against Panthera's libSystem and staged dependencies.
5. Add rootfs manifest entries and staging rules.
6. Add a userland smoke that can run:
   - `zpool version`
   - `zfs version`
   - basic library load checks

Exit criteria:

- `zpool` and `zfs` run inside Panthera without unresolved dyld binds.
- The tools report useful version/status information.
- No host OpenZFS binaries are copied into the guest.

## Phase Z4: ZFS Data Pool Smoke

Goal: prove real ZFS read/write behavior on a secondary pool while retaining
the HFS alpha root for control.

Status 2026-05-22: complete for the HFS-root plus secondary-ZFS-data-disk
bring-up milestone.

Current status:

- Complete for Z4.
- QEMU and serial smoke tooling support an explicit secondary data disk through
  `--data-disk`.
- Panthera boots the opt-in ZFS path with a secondary data disk attached.
  Disk numbering is not stable across boots, so the host smoke harness passes
  the authoritative same-boot `BSD root:` device from the serial log into the
  guest command. The guest excludes that root and requires exactly one other
  instantiated `/dev/disk*s1` slice before any pool operation.
- `zpool create -f -m /z pantherazboot <data-slice>` now works from guest
  userland.
- Real ZFS mount, file write/read, `/sbin/umount /z`, and `zpool export` now
  work. Latest evidence:
  `artifacts/boot/zfs-boot-create-tpool-fixed-smoke-20260522-r33.log`.
- Plain import discovery was fixed enough to avoid scanning arbitrary non-disk
  `/dev` entries; the persistent Panthera OpenZFS patch filters macOS `/dev`
  candidates to `disk*`/`rdisk*`.
- An OpenZFS `libtpool` queueing race was found and patched persistently:
  `tpool_dispatch()` now queues the job before waking/creating a worker. This
  avoids Panthera workers sleeping on an empty queue and missing the job.
- The remaining import hang was Panthera pthread/psynch, not OpenZFS import
  logic. The old kernel psynch syscall table used ENOTSUP placeholders while
  libpthread's normal condition variables used the psynch path. A focused
  detached-worker probe showed `pthread_cond_wait()` returning `EINVAL`
  without releasing the mutex.
- XNU now links the real Apple psynch implementation from
  `bsd/pthread/pthread_kext_synch.c`; the placeholder psynch/hash stubs were
  removed from `pthread_kext_support.c`, and `struct ksyn_waitq_element` is
  exposed with the Darwin fields needed by the psynch code.
- Focused evidence:
  `artifacts/boot/zfs-psynch-cond-smoke-20260522-r42.log` proves
  `cond_wait_rc=0`, `COND_PASS`, and `ALL_PASS`.
- Latest import/mount/export evidence:
  `artifacts/boot/zfs-import-psynch-smoke-20260522-r43.log` proves
  `import_rc=0`, `mount_rc=0`, readback of `hello-panthera-zfs`,
  `unmount_rc=0`, `export_rc=0`, and `PANTHERA_ZFS_BOOT_SMOKE_OK`.
- Host-side persistence gate evidence:
  `artifacts/boot/zfs-data-pool-20260522-r34-import-scan-dev.driver.log`
  proves the controlled two-boot smoke now passes without the temporary
  launchd job. The create boot proves root `disk1s1`, target
  `/dev/disk0s1`, `PANTHERA_ZFS_DATA_CREATE_RC:0`, payload write/read,
  `PANTHERA_ZFS_DATA_UNMOUNT_RC:0`,
  `PANTHERA_ZFS_DATA_EXPORT_RC:0`, and
  `PANTHERA_ZFS_DATA_CREATE_OK:PASS`. The import boot proves
  root `disk0s1`, normal `zpool import -N -d /dev pantherazboot` pool
  discovery, `PANTHERA_ZFS_DATA_IMPORT_RC:0`, ONLINE `zpool status`,
  `PANTHERA_ZFS_DATA_MOUNT_RC:0`, persistent payload readback,
  `PANTHERA_ZFS_DATA_UNMOUNT_RC:0`,
  `PANTHERA_ZFS_DATA_EXPORT_RC:0`, and
  `PANTHERA_ZFS_DATA_IMPORT_OK:PASS`.
- The normal Z4 gate does not enable heavy OpenZFS raw tracing. Low-level raw
  trace writes are opt-in through `PANTHERA_ZFS_RAW_TRACE=1` so diagnostic
  output does not perturb SSH-backed smokes.
- The old launchd ZFS mount-smoke job is gated behind
  `PANTHERA_STAGE_ZFS_MOUNT_SMOKE=1`, and the `libpanthera_extra`
  `PANTHERA:dlopen` trace is gated behind `PANTHERA_DLOPEN_TRACE=1`.

Tasks:

1. Keep the existing HFS root image as the control root for this phase.
2. Attach a separate QEMU data disk through `--data-disk`.
3. Convert the temporary launchd smoke into `tools/smoke_zfs_data_pool.sh`.
4. In the guest:
   - create or import a pool
   - create a dataset
   - mount it
   - write files
   - sync
   - reboot
   - re-import and verify persistence
5. Add `tools/smoke_zfs_data_pool.sh`.

Exit criteria:

- Panthera can create/import a ZFS pool in QEMU.
- Files written to ZFS persist across reboot.
- The smoke verifies `/` is still HFS for this phase so results are not
  confused with the final root migration.

Result:

- Z4 exit criteria are met by
  `artifacts/boot/zfs-data-pool-20260522-r34-import-scan-dev.driver.log`.
  The old launchd ZFS mount-smoke job is now gated behind
  `PANTHERA_STAGE_ZFS_MOUNT_SMOKE=1`; the next work is Z5 after cleaning noisy
  diagnostics from release-style ZFS boots.

## Phase Z5: Hybrid System/Data Layout

Goal: run normal Panthera state directories from ZFS.

Status 2026-05-22: complete for the controlled HFS-root plus ZFS writable-state
milestone. Full ZFS root remains Z6.

Evidence:

- `tools/smoke_zfs_hybrid_layout.sh` creates a fresh HFS control root image and
  a separate ZFS data disk, creates `pantherazhybrid/{var,tmp,Users,Library}`
  with final mountpoints, exports the pool, then reboots to verify early
  launchd import/mount.
- `userland/launchd/real/panthera_boot.c` invokes the opt-in
  `/sbin/panthera_zfs_hybrid_mount` helper before launch daemon import when
  `/etc/panthera/zfs-hybrid.conf` is staged.
- The helper imports with `zpool import -N -d /dev`, mounts configured
  datasets, verifies each mountpoint is ZFS via `statfs`, creates required
  runtime directories under ZFS `/var`, and seeds SystemConfiguration
  preferences into ZFS `/Library` when needed.
- Latest passing evidence:
  `artifacts/boot/zfs-hybrid-layout-20260522-r6.driver.log`.
- Create evidence:
  `artifacts/boot/zfs-hybrid-layout-20260522-r6-create.ssh.log` proves
  `zfs create -u -o mountpoint=...` for `/var`, `/tmp`, `/Users`, and
  `/Library`, followed by `zpool export` and
  `PANTHERA_ZFS_HYBRID_CREATE_OK:PASS`.
- Verify evidence:
  `artifacts/boot/zfs-hybrid-layout-20260522-r6-verify.ssh.log` proves all
  four datasets are mounted at their final paths with `mounted=yes`, `/var/run`
  and `/tmp` are writable, and `PANTHERA_ZFS_HYBRID_VERIFY_OK:PASS`.
- SSH/SFTP passes on both create and verify boots.

Tasks:

1. Create datasets:
   - `tank/var`
   - `tank/tmp`
   - `tank/Users`
   - `tank/Library`
2. Add a launchd job or early boot service that imports the pool and mounts
   datasets before dependent daemons start.
3. Remove assumptions that `/var`, `/tmp`, `/Users`, or `/Library` must be on
   HFS.
4. Run the alpha gate with the hybrid image.
5. Add checks proving those paths are ZFS-backed.

Exit criteria:

- Controlled Z5 exit criteria are met for SSH/SFTP, configd/IPConfiguration,
  and writable state verification with `/var`, `/tmp`, `/Users`, and
  `/Library` on ZFS.
- The broader release-style follow-up is to run mDNS, SDK, and guest toolchain
  smokes through the new opt-in ZFS storage gate before attempting ZFS root.
- HFS remains the temporary system scaffold for this milestone.

## Phase Z6: ZFS Root Mount

Goal: mount `tank/ROOT/panthera` as `/`.

Status 2026-05-23: complete as a bootstrap/runtime smoke and opt-in full ZFS
storage/root gate; not yet the default storage target. The current root
bootstrap no longer relies on a readonly root-pool import.

Evidence:

- `tools/smoke_zfs_root_bootstrap.sh` creates/populates a native ZFS root
  pool from a temporary HFS+ control boot and then proves a root-only ZFS boot.
- `boot/efi/stage_phase2_efi.sh --zfs-root tank/ROOT/panthera --rootdev disk0s1`
  stages SPL/ZFS and writes the mountroot selectors
  `zfs_boot=tank/ROOT/panthera rootdev=disk0s1`.
- `kexts/zfs/patches/openzfs-spl-panthera.patch` now keeps early root-pool
  import independent of userland `/private/var/run/disk/by-id` links by using
  the IOMedia BSD node directly during mountroot.
- The root-pool import is writable during mountroot. The earlier readonly
  import experiment reached login but left the SPA read-only and triggered
  `zilog_dirty` write assertions once launchd and daemons began normal writes.
- Panthera's OpenZFS IOMedia LDI path now requests shared writer access
  (`kIOStorageAccessReaderWriter | kIOStorageAccessSharedLock`) for writable
  vdev opens, matching Darwin storage clients that multiplex access down the
  provider stack. The historical BSD media-client first-root writer upgrade is
  also shared instead of exclusive, so it no longer blocks ZFS's writable root
  import.
- Latest passing evidence:
  `artifacts/boot/zfs-root-bootstrap-20260522-r6-root-boot.log`.
- Latest payload-root writable-import evidence:
  `artifacts/boot/zfs-root-payload-bsdshared-20260523-r1.driver.log`.
- Rootfs payload evidence:
  `artifacts/boot/rootfs-payload-zfs-20260522-r2.tar.gz` with SHA-256
  `19eeb1282989ec2fc02819bc21565e4124be72374bf6f85cc29d63ffb9cc7294`.
- Latest focused gate evidence:
  `artifacts/release/zfs-release-20260522-root-r2.summary.md`.
- Latest full storage/root gate evidence:
  `artifacts/release/zfs-release-20260522-full-root-followups-r1.summary.md`.
- Latest focused root follow-up evidence:
  `artifacts/release/zfs-release-20260522-root-followups-r1.summary.md`.
- The passing log proves `zfs_boot=tank/ROOT/panthera rootdev=disk0s1`,
  `ZFS: zfs_vfs_mountroot`, `zfs_boot_probe_disk matched pool tank`,
  `spa_tryimport()`/`spa_import()` progress for `tank`, and login from the ZFS
  root disk.
- The 2026-05-23 payload-root log additionally proves the host-built payload
  path can populate the ZFS root without using the HFS control root as the
  source of root contents, and that writable `spa_import()` reaches login
  without the prior `vdev_disk_open` failure or readonly-SPA assertion.
- `artifacts/boot/zfs-release-20260522-full-root-followups-r1-root-statfs.ssh.log` proves
  `statfs("/")` reports `zfs`, mounted on `/`, and that the ZFS root accepts
  writes after launchd's root remount path.
- `launchd` now selects the root remount path from `statfs("/")`: HFS control
  roots still use HFS `MNT_UPDATE`; ZFS roots use a ZFS `MNT_UPDATE` remount
  argument.
- `tools/zfs_release_gate.sh --only-root` runs the common hygiene/build checks,
  the ZFS-root bootstrap smoke, statfs-over-SSH runtime proof, and direct
  ZFS-root mDNS/SDK/guest-toolchain smokes.

Tasks:

1. Determine and implement the Darwin/OpenZFS root selection path for
   Panthera's XNU baseline.
2. Teach the boot image builder to create a root pool and root dataset.
3. Ensure kernel boot args identify the ZFS root dataset.
4. Remove launchd's HFS `mount("hfs", "/", MNT_UPDATE, ...)` root remount from
   the ZFS boot path.
5. Replace HFS-specific root ownership fixups with filesystem-native image
   construction and validation.
6. Boot to login with `/` mounted from ZFS.

Exit criteria:

- Bootstrap/runtime exit criteria are met: the system reaches login with the
  ZFS root dataset selected by mountroot, `statfs("/")` reports `zfs`, root
  accepts writes, and OpenSSH/SFTP work from the ZFS-root boot.
- Remaining default-storage criteria: replace the temporary HFS control boot
  used to create/import the pool with a direct/reproducible ZFS-root image
  builder or installer flow.

## Phase Z7: ZFS Release Gate

Goal: make ZFS root a repeatable release target.

Status 2026-05-23: passing with the ZFS-root lane enabled.
`tools/zfs_release_gate.sh` gates Z4 secondary data-pool persistence, Z5
hybrid writable-state layout, and, when enabled, the Z6 ZFS-root bootstrap plus
statfs-over-SSH runtime proof.

Latest pre-root storage-gate evidence:

- `artifacts/release/zfs-release-20260522-followups-r1.summary.md` passes.
- The gate now includes diagnostics audit, ZFS staging audit, third-party
  checksum verification, mount-tools rebuild, shared-cache rebuild, SDK
  smoke-binary build, Z4 data-pool persistence, Z5 hybrid writable-state
  verification, mDNS over SSH/SFTP on the hybrid layout, Panthera SDK smoke on
  the hybrid layout, and guest toolchain smoke on the hybrid layout.

Latest focused root-gate evidence:

- `artifacts/release/zfs-release-20260522-root-r2.summary.md` passes.
- The focused root gate passed diagnostics audit, ZFS staging audit,
  third-party checksum verification, mount-tools rebuild, shared-cache rebuild,
  the ZFS-root bootstrap smoke, and the statfs-over-SSH runtime proof.
- `artifacts/release/zfs-release-20260522-root-followups-r1.summary.md`
  passes.
- The root follow-up gate additionally passed mDNS, Panthera SDK, and guest
  toolchain smokes directly on the ZFS-root boot.

Latest full storage/root-gate evidence:

- `artifacts/release/zfs-release-20260522-full-root-followups-r1.summary.md`
  passes.
- The full gate passed diagnostics audit, ZFS staging audit, third-party
  checksum verification, mount-tools rebuild, shared-cache rebuild, SDK
  smoke-binary build, Z4 data-pool persistence, Z5 hybrid writable-state
  verification, hybrid mDNS/SDK/guest-toolchain smokes, ZFS-root bootstrap,
  ZFS-root statfs-over-SSH/SFTP, and ZFS-root mDNS/SDK/guest-toolchain smokes.
- Z4 import now waits for the non-root data slice before calling
  `zpool import`, avoiding a Darwin device-node publication race where the data
  disk can appear before its `diskNs1` partition node.
- Follow-up ZFS-root payload evidence:
  `artifacts/boot/zfs-root-payload-bsdshared-20260523-r1.driver.log` passes
  with host-built rootfs payload population and writable mountroot import.
- First-class ZFS-root builder evidence:
  `artifacts/boot/zfs-root-builder-refactor-20260523-r1.driver.log` passes.
  `rootfs/create_zfs_root_image.sh` now owns the reproducible ZFS-root image
  creation/population flow, while `tools/smoke_zfs_root_bootstrap.sh` verifies
  that image by booting the ZFS root disk alone.
- The builder's temporary HFS installer/control image is now assembled from an
  explicit ZFS installer manifest set instead of inheriting the normal HFS
  image defaults. This narrows the HFS dependency but does not remove it.
- `rootfs/create_zfs_installer_image.sh` is now the dedicated temporary
  installer-image builder. It gives the installer its own default path and
  `PantheraZFSInstaller` volume name, and uses the explicit ZFS installer
  manifest set.
- Host-side installer-builder evidence:
  `artifacts/boot/zfs-installer-builder-20260523-r1.log` passes and creates
  `images/qemu/panthera-zfs-installer-smoke-20260523-r1.img`.
- `rootfs/create_zfs_root_image_host.sh` is now the preferred fully-ZFS image
  creation path. It uses host OpenZFS `zpool`/`zfs` to create and populate the
  root pool directly, without booting an HFS installer image.
- `rootfs/create_zfs_root_image.sh`, `tools/smoke_zfs_root_bootstrap.sh`, and
  `tools/zfs_release_gate.sh` now default to `--builder-mode direct`.
  `--builder-mode installer` is retained only as an explicit compatibility
  mode.
- Earlier host limitation evidence:
  `artifacts/boot/zfs-root-direct-no-host-openzfs-20260523-r2.log` shows direct
  mode correctly refusing to run before host OpenZFS was installed/approved.
- Superseding host OpenZFS evidence:
  `artifacts/boot/host-openzfs-audit-20260523-r1.log` passes after installing
  and approving OpenZFS on macOS. Host tools and kext both report 2.3.1.
- Direct host-created ZFS-root evidence:
  `artifacts/boot/zfs-root-direct-host-20260523-r2.log` passes and creates the
  root image with host OpenZFS, without booting an HFS installer.
- Direct host-created ZFS-root runtime evidence:
  `artifacts/boot/zfs-root-direct-host-20260523-r2-statfs.ssh.log` proves
  `PANTHERA_ZFS_ROOT_FSTYPE:zfs`, `PANTHERA_ZFS_ROOT_MNTON:/`,
  `PANTHERA_ZFS_ROOT_STATFS_OK`, and SSH/SFTP on the direct-created ZFS root.
- Focused direct-root release-gate evidence:
  `artifacts/release/zfs-release-20260523-direct-root-r1.summary.md` passes.
  It proves the default direct builder path, host OpenZFS audit,
  ZFS-root bootstrap, and statfs-over-SSH/SFTP. mDNS/SDK/toolchain follow-ups
  were intentionally skipped in this focused gate.
- Developer/SSH tooling now defaults to the ZFS root image. The promotion smoke
  `artifacts/boot/zfs-default-openssh-promote-r1.log`,
  `artifacts/boot/zfs-default-openssh-promote-r1.ssh.log`, and
  `artifacts/boot/zfs-default-openssh-promote-r1.sftp.log` passes with
  `images/qemu/panthera-zfs-root.img`, `zfs_boot=tank/ROOT/panthera`, SSH exec,
  and SFTP. HFS remains available only by explicit root-kind selection in those
  developer paths.

Tasks:

1. **Complete (2026-09-09):** pkgsrc smokes use SFTP staging on ZFS rather
   than requiring host `hdiutil attach`. The native extraction/cleanup proof
   is `artifacts/boot/r23-pkgsrc-native-20260909-final.ssh.log`.
   This proves staging, not completion of R3.2 package bootstrap.
2. **Complete (2026-09-10):** alpha defaults to ZFS;
   `artifacts/release/r31-primitives-zfs-20260910.summary.md` passes 23 checks.
3. HFS installer/alpha modes remain explicit compatibility/recovery paths.
   Alpha now restages HFS EFI, clearing a preceding ZFS run's boot selection.
   - mDNS-over-SSH
   - SDK smoke
   - guest toolchain smoke
   - ZFS persistence across reboot
4. `docs/CURRENT_STATE.md` records the passing ZFS root evidence.

Exit criteria:

- A single documented command can build and verify a ZFS-root Panthera image.
- The HFS alpha gate remains available as a recovery/control gate.
- ZFS root becomes the default Panthera storage target only after this gate
  passes.

## Phase Z8: HFS Demotion & Default ZFS Root Promotion

Goal: establish ZFS root as the primary default development and release path while retaining HFS+ strictly as a recovery and compatibility baseline.

Status 2026-05-23: complete. The default root-image builder path is now fully ZFS (`images/qemu/panthera-zfs-root.img`) via `rootfs/create_zfs_root_image.sh` (or `rootfs/create_zfs_root_image_host.sh` using host OpenZFS). The full ZFS storage release gate `artifacts/release/zfs-full-post-upload-20260523-r2.summary.md` is PASS (20/20 checks passing), and development tooling defaults to the ZFS root image. HFS+ image creation (`rootfs/create_hfs_root_image.sh` -> `images/qemu/panthera-root.img`) and HFS kexts (`HFS.kext`, `HFSEncodings.kext`) remain preserved for recovery, compatibility, and regression comparisons.

Tasks:

1. Maintain `rootfs/create_zfs_root_image.sh` consuming the shared rootfs payload contract from `rootfs/create_rootfs_payload_tar.sh`.
2. Keep HFS image tooling (`rootfs/create_hfs_root_image.sh`) explicitly identified as the recovery/compatibility baseline.
3. Ensure `tools/build_world.sh` builds both storage paths and can execute `tools/zfs_release_gate.sh` and `tools/alpha_release_gate.sh`.
4. Keep docs and release notes aligned with ZFS as the default development root and HFS as the recovery baseline.

Exit criteria:

- Default Panthera builds create and boot ZFS-root images (`images/qemu/panthera-zfs-root.img`).
- HFS is documented and retained for recovery/compatibility (`images/qemu/panthera-root.img`).

## Tracking Checklist

- [x] Z0: evidence and HFS assumption inventory
- [x] Z1: SPL kext builds and registers from booter data; `spl_start()` is intentionally deferred until `zfs.kext` depends on it
- [x] Z2: ZFS kext builds, loads, registers with VFS, and is queryable from native Panthera userland through `zsysctl`
- [x] Z3: native `zpool`/`zfs` tools build and run in Panthera
- [x] Z4: secondary ZFS pool read/write/persistence smoke passes
- [x] Z5: `/var`, `/tmp`, `/Users`, and `/Library` run from ZFS
- [x] Z6: `/` mounts from ZFS in the bootstrap smoke
- [x] Z7: ZFS root release gate passes (`artifacts/release/zfs-full-post-upload-20260523-r2.summary.md`)
- [x] Z8: ZFS root promoted to default development target; HFS demoted to recovery/compatibility

## Canonical Integration References

- Authoritative Present State: `docs/CURRENT_STATE.md`
- World Build Orchestrator: `tools/build_world.sh`
- Rootfs Payload Contract: `rootfs/create_rootfs_payload_tar.sh`, `rootfs/create_rootfs_tree.sh`
- ZFS Root Image Generator: `rootfs/create_zfs_root_image.sh`
- HFS Recovery Image Generator: `rootfs/create_hfs_root_image.sh`
- ZFS Release Gate: `tools/zfs_release_gate.sh`
- Verified Full Gate Artifact: `artifacts/release/zfs-full-post-upload-20260523-r2.summary.md`
