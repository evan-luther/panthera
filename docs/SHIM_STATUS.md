# Panthera Shim Status

> Status: historical shim ledger. Current authoritative project status is
> `docs/CURRENT_STATE.md`; verify live shim status with
> `userland/libsystem/verify_exports.sh`.

**Last updated:** April 7, 2026
**Source plan:** `docs/SHIM_ELIMINATION_PLAN.md`
**Historical verification:** `userland/libsystem/verify_exports.sh` passed after the Phase 7 cleanup pass.

## Summary

Panthera still has shim ownership in `libpanthera_extra.dylib` and CoreFoundation support glue. The first six planned shim-elimination steps are complete, and the first Phase 7 cleanup pass has removed the CF support shims that are already owned by active real dylibs:

- Phase 1: export-audit and ownership cleanup across existing real dylibs
- Phase 2: replace Panthera's block-runtime shims with a real Apple-source `libclosure.dylib`
- Phase 3: make Apple-source `libsystem_notify.dylib` the canonical owner of the remaining notify client APIs
- Phase 4: expand `libsystem_c.dylib` so more libc-owned symbols come from the real Apple-source libc instead of Panthera shims
- Phase 5: eliminate `libpanthera_launchd.dylib` by moving its remaining exports into `libsystem_kernel.dylib`, `libsystem_platform.dylib`, `libbsm.0.dylib`, and the `libSystem.B.dylib` umbrella
- Phase 6: move trace and sandbox stub ownership into dedicated `libsystem_trace.dylib` and `libsystem_sandbox.dylib` stub dylibs, re-exported through `libSystem.B.dylib`

Phase 1 scope was limited to:

- `libsystem_c.dylib`
- `libsystem_kernel.dylib`
- `libsystem_pthread.dylib`
- `libsystem_malloc.dylib`
- `libsystem_platform.dylib`

## Phase 1 Status

### Result

- `verify_exports.sh` now reports:
  - no unresolved symbols in the checked binaries
  - no duplicate exports between dylibs
  - all critical symbols present
- `libpanthera_extra.dylib` no longer exports the previous duplicate set against `libsystem_c`, `libsystem_info`, or `libsystem_pthread`.
- The `___platform_*` compatibility trampoline block was removed from the Panthera shim side after confirming the canonical `__platform_*` providers already exist in `libsystem_platform.dylib`.

### Symbols moved into real dylibs

#### `libsystem_c.dylib`

The Phase 1 audit moved these symbols out of shim ownership and into the real libc build:

- `getopt`, `unsetenv`
- `strtok_r`, `memccpy`, `strlcat`, `strnlen`, `index`
- `vfscanf`, `__dtoa`, `__freedtoa`, `__hdtoa`, `__hldtoa`, `__ldtoa`
- `regcomp`, `regexec`, `regfree`
- `uuid_generate`, `uuid_copy`, `uuid_compare`, `uuid_unparse`, `uuid_is_null`
- `tcdrain`, `openpty`, `forkpty`, `login_tty`
- `arc4random`, `arc4random_addrandom`, `arc4random_buf`, `arc4random_stir`, `arc4random_uniform`
- `sigsetmask`
- `getgroups$DARWIN_EXTSN`
- previously duplicated `fmtcheck` and `pclose` ownership was also removed from `libpanthera_extra`

#### `libsystem_pthread.dylib`

The real libpthread now exports unsuffixed forms of Phase 1 APIs that previously only existed as suffixed exports or were shadowed by shim ownership:

- `pthread_mutexattr_destroy`
- `pthread_rwlock_init`, `pthread_rwlock_destroy`
- `pthread_rwlock_rdlock`, `pthread_rwlock_wrlock`, `pthread_rwlock_unlock`
- `pthread_cond_init`, `pthread_cond_wait`, `pthread_cond_timedwait`
- `pthread_join`
- `pthread_sigmask`
- `pthread_main_thread_np` remains owned by real libpthread, not `libpanthera_extra`

#### Other canonical dylibs

- `libsystem_info.dylib` already owned `getaddrinfo`; duplicate shim ownership was removed.
- `libsystem_platform.dylib` already owned the canonical `__platform_*` string entry points; the shim-side `___platform_*` aliases were removed.

## Phase 2 Status

### Result

- A real `libclosure.dylib` is now built from Apple sources in `src/libclosure-90/`.
- `libSystem.B.dylib` now re-exports `/usr/lib/system/libclosure.dylib`.
- `verify_exports.sh` remains green after the Phase 2 rebuild/relink sequence.
- The block-runtime ownership for `_Block_copy`, `_Block_release`, `_NSConcreteGlobalBlock`, and `_NSConcreteStackBlock` no longer comes from Panthera shim code.

### Canonical ownership after Phase 2

The new dylib lives at:

- install path: `userland/libsystem/build/sysroot/usr/lib/system/libclosure.dylib`
- install name: `/usr/lib/system/libclosure.dylib`

The Apple-source `libclosure` build now owns:

- `_Block_copy`
- `_Block_release`
- `_Block_object_assign`
- `_Block_object_dispose`
- `_NSConcreteGlobalBlock`
- `_NSConcreteStackBlock`

The Panthera-local definitions removed or suppressed for this phase were:

- `_Block_copy` and `_Block_release` from `userland/libsystem/build/obj/panthera_missing.c`
- `_Block_copy` and `_Block_release` from `userland/libsystem/build/shims/libsystem_c/panthera_libc_bridges.c`
- `_NSConcreteGlobalBlock` and `_NSConcreteStackBlock` from `userland/libsystem/build/obj/panthera_extra_stubs.c`
- `_Block_object_assign` and `_Block_object_dispose` from `userland/libsystem/build/obj/panthera_extra_stubs.c`

## Phase 3 Status

### Result

- A reproducible `libsystem_notify.dylib` build now exists in the Phase 3 notify pipeline.
- `userland/notifyd/build_notifyd.sh` now regenerates the Libnotify MIG sources, builds the client and daemon, and stages the canonical client dylib into `userland/libsystem/build/sysroot/usr/lib/system/libsystem_notify.dylib`.
- `libSystem.B.dylib` re-exports `/usr/lib/system/libsystem_notify.dylib`.
- `verify_exports.sh` remains green after rebuilding notify artifacts and relinking `libSystem.B.dylib`.
- `libpanthera_extra.dylib` does not export the Phase 3 notify client symbols anymore, so shim ownership is no longer duplicated there.

### Canonical ownership after Phase 3

The canonical dylib now lives at:

- install path: `userland/libsystem/build/sysroot/usr/lib/system/libsystem_notify.dylib`
- install name: `/usr/lib/system/libsystem_notify.dylib`

The Apple-source `libsystem_notify` build now owns:

- `notify_register_check`
- `notify_peek`
- `notify_cancel`
- `notify_post`

It also exports the broader notify client surface needed by current callers:

- `notify_register_dispatch`
- `notify_register_signal`
- `notify_register_mach_port`
- `notify_register_file_descriptor`
- `notify_check`
- `notify_get_state`
- `notify_set_state`
- `notify_suspend`
- `notify_resume`
- `notify_is_valid_token`

### Runtime validation status

- `notifyd` links cleanly against the current Panthera sysroot.
- `notifyutil` links against `/usr/lib/system/libsystem_notify.dylib`.
- A small client linked against only `-lSystem` resolves `notify_register_check` through `libSystem.B.dylib`, confirming the re-export path.
- A direct runtime smoke using `userland/notifyd/test_notify.c` still fails with `NOTIFY_STATUS_SERVER_NOT_FOUND`, followed by `NOTIFY_STATUS_INVALID_TOKEN`.

This means Phase 3 symbol ownership and libSystem integration are complete, but end-to-end runtime behavior is still blocked by notify service discovery / bootstrap publication rather than by shim exports.

## Phase 4 Status

### Result

- The active `libsystem_c.dylib` build already contained the relevant upstream Libc source files for most of the planned Phase 4 surface.
- The remaining Phase 4 gap was export visibility, not missing source inclusion: several upstream libc entry points were still compiled as `__private_extern__`, so Panthera shim code remained the only public owner.
- `build_libsystem_c.sh` now forces the Phase 4 upstream owners in `locale/FreeBSD/collate.c`, `locale/FreeBSD/setlocale.c`, and `stdio/FreeBSD/vfscanf.c` to export their compatibility entry points from `libsystem_c.dylib`.
- `libpanthera_extra.dylib` no longer exports the overlapping locale/collation helpers or `__svfscanf_l`.
- `verify_exports.sh` remains green after rebuilding `libsystem_c.dylib`, relinking `libpanthera_extra.dylib`, and relinking `libSystem.B.dylib`.

### Canonical ownership after Phase 4

`libsystem_c.dylib` is now the canonical owner for these additional libc APIs that were previously still shim-owned:

- `__collate_load_tables`
- `__collate_lookup_which`
- `__collate_xfrm`
- `__collate_collating_symbol`
- `__collate_equiv_class`
- `__collate_mbstowcs`
- `__collate_substitute`
- `__collate_wcsdup`
- `__detect_path_locale`
- `__get_locale_env`
- `__open_path_locale`
- `__svfscanf_l`

This Phase 4 pass also confirms that the rest of the expected libc-owned groups are already being exported by the real `libsystem_c.dylib` in the current build:

- `getopt` plus `optarg`, `optind`, `opterr`, `optopt`
- `unsetenv`
- `strtoimax`, `strtoumax`, `strtol`, `strtoul`, `strtoll`, `strtoull` and locale variants
- `regcomp`, `regexec`, `regfree`
- `uuid_generate`, `uuid_copy`, `uuid_compare`, `uuid_unparse`, `uuid_is_null`
- `tcgetattr`, `tcsetattr`, `tcflow`, `tcflush`, `tcdrain`, `tcsendbreak`, `cfmakeraw`, `cfgetispeed`, `cfsetospeed`
- `openpty`, `forkpty`, `login_tty`
- `opendir`, `closedir`, `readdir`, `readdir_r`, `fdopendir`, `__opendir2`
- `sprintf`, `snprintf`, `vsprintf`, `vsnprintf`, `vasprintf`, `vfscanf`, `vsscanf`
- `__dtoa`, `__freedtoa`, `__hdtoa`, `__hldtoa`, `__ldtoa`
- `sigsetmask`

### Symbols removed from active shim ownership in Phase 4

The Phase 4 relink removed these from the exported surface of `libpanthera_extra.dylib`:

- `__collate_load_tables`
- `__collate_lookup_which`
- `__collate_xfrm`
- `__collate_collating_symbol`
- `__collate_equiv_class`
- `__collate_mbstowcs`
- `__collate_substitute`
- `__collate_wcsdup`
- `__detect_path_locale`
- `__get_locale_env`
- `__open_path_locale`
- `__svfscanf_l`

### `libpanthera_patch` status after Phase 4

The old patch dylib still exists on disk, but its libc-owned exports are no longer needed in practice:

- `libSystem.B.dylib` does not re-export `libpanthera_patch.dylib`
- `libsystem_c.dylib` now exports `getopt`, `unsetenv`, `login_tty`, `uuid_generate`, `uuid_is_null`, and the getopt globals directly

So the libc-symbol purpose of `libpanthera_patch` is functionally eliminated, even though the orphan dylib and shared-cache manifest references have not yet been removed.

## Phase 5 Status

### Result

- `libpanthera_launchd.dylib` is no longer built by the active layer-5/6/7 flow.
- `libSystem.B.dylib` no longer re-exports `/usr/lib/system/libpanthera_launchd.dylib`.
- The old built output `userland/libsystem/build/sysroot/usr/lib/system/libpanthera_launchd.dylib` is removed.
- The former layer-7 symbols now come from canonical owner libraries in the current Panthera sysroot.
- A Phase 5 completion pass replaced the temporary base-dylib augment pattern for `libsystem_kernel.dylib` and `libsystem_platform.dylib` with direct relinks, added real libc `posix_spawnp` ownership to `libsystem_c.dylib`, and removed the last overlapping `libpanthera_extra` spawn/proc/network compatibility exports.
- `verify_exports.sh` now passes cleanly after the Phase 5 rebuild/relink sequence.

### Canonical ownership after Phase 5

#### `libsystem_kernel.dylib`

The following symbols moved out of `panthera_layer7.c` and are now exported by the canonical kernel layer:

- `proc_pidinfo`
- `proc_listallpids`
- `proc_listchildpids`
- `proc_listpgrppids`
- `proc_get_dirty`
- `proc_set_dirty`
- `proc_track_dirty`
- `proc_terminate`
- `proc_setpcontrol`
- `posix_spawnattr_setbinpref_np`
- `posix_spawnattr_setprocesstype_np`
- `posix_spawnattr_setcpumonitor_default`
- `posix_spawnattr_set_importancewatch_port_np`
- `setiopolicy_np`
- `chmod`
- `chmod$UNIX2003`
- `reboot`
- `syscall`

Phase 5 also moved `voucher_mach_msg_set` and `voucher_mach_msg_clear` into `libsystem_kernel.dylib` so launchd no longer depends on the old layer-7 shim for voucher MIG helpers.

The Phase 5 completion pass also restored direct canonical ownership for the broader BSD/Mach surface that the verification pipeline expects from the public kernel layer:

- `close`
- `read`
- `write`
- `dup2`
- `socket`
- `bind`
- `listen`
- `accept`
- `connect`
- `mach_msg`
- `mach_port_allocate`
- `mach_port_deallocate`
- `mach_task_self_`
- `task_self_trap`
- `host_self_trap`
- `NDR_record`
- `host_reboot`
- `host_set_UNDServer`
- `host_set_exception_ports`
- `host_set_special_port`
- `host_statistics`
- `mach_absolute_time`
- `mach_error_string`
- `mach_timebase_info`
- `mig_allocate`
- `mig_dealloc_reply_port`
- `mig_deallocate`
- `mig_get_reply_port`
- `mig_put_reply_port`
- `mig_strncpy`
- `mig_strncpy_zerofill`
- `task_get_special_port`
- `task_policy_set`
- `task_set_exception_ports`
- `task_set_special_port`
- `vm_deallocate`

#### `libsystem_platform.dylib`

- `OSAtomicAdd32`
- `setjmp`
- `longjmp`

#### `libbsm.0.dylib`

- `audit_token_to_au32`

#### `libsystem_c.dylib`

- `posix_spawnp`

#### `libSystem.B.dylib` umbrella / compiler-rt path

- `__udivti3`

This is now provided through a small wrapper linked into the umbrella over the compiler-rt `__udivmodti4` object extracted from the local Clang runtime archive, rather than from Panthera-specific shim code.

### Phase 5 verification notes

- `nm -gU` confirms the former `libpanthera_launchd` surface now resolves from `libsystem_kernel.dylib`, `libsystem_platform.dylib`, `libbsm.0.dylib`, and `libSystem.B.dylib`.
- `otool -l userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib` confirms there is no remaining `LC_REEXPORT_DYLIB` entry for `libpanthera_launchd`.
- `verify_exports.sh` passes after rebuilding `libsystem_c.dylib`, `libpanthera_extra.dylib`, `libsystem_kernel.dylib`, `libsystem_platform.dylib`, and relinking `libSystem.B.dylib`.
- `launchd`, `zsh`, and `dyld` all resolve cleanly against the current combined export table.
- The duplicate-export check is now clean; `libpanthera_extra.dylib` no longer exports the overlapping `posix_spawn*`, `proc_kmsgbuf`, `getnameinfo`, or qos-clamp compatibility symbols that blocked the earlier Phase 5 verification pass.

## Phase 6 Status

### Result

- Reproducible stub builds now exist for `libsystem_trace.dylib` and `libsystem_sandbox.dylib`.
- `libSystem.B.dylib` now re-exports `/usr/lib/system/libsystem_trace.dylib` and `/usr/lib/system/libsystem_sandbox.dylib`.
- `libpanthera_extra.dylib` no longer exports the Phase 6 trace symbols, so trace ownership no longer lives in the catch-all Panthera shim.
- `verify_exports.sh` passes after building the new stub dylibs, relinking `libpanthera_extra.dylib`, and relinking `libSystem.B.dylib`.

### Canonical ownership after Phase 6

The new trace dylib now lives at:

- install path: `userland/libsystem/build/sysroot/usr/lib/system/libsystem_trace.dylib`
- install name: `/usr/lib/system/libsystem_trace.dylib`

`libsystem_trace.dylib` now owns:

- `os_log_create`
- `os_log_type_enabled`
- `_os_log_impl`
- `_os_log_debug_impl`
- `_os_log_error_impl`
- `_os_log_fault_impl`
- `_os_log_default`

The new sandbox dylib now lives at:

- install path: `userland/libsystem/build/sysroot/usr/lib/system/libsystem_sandbox.dylib`
- install name: `/usr/lib/system/libsystem_sandbox.dylib`

`libsystem_sandbox.dylib` now owns:

- `sandbox_init`
- `sandbox_check`
- `sandbox_free_error`

### Symbols removed from active shim ownership in Phase 6

The Phase 6 relink removed these from the exported surface of `libpanthera_extra.dylib`:

- `os_log_create`
- `os_log_type_enabled`
- `_os_log_impl`
- `_os_log_debug_impl`
- `_os_log_error_impl`
- `_os_log_fault_impl`
- `_os_log_default`

There were no `sandbox_*` exports to remove from `libpanthera_extra.dylib`; those symbols were not owned there before this phase.

### Phase 6 verification notes

- `nm -gU userland/libsystem/build/sysroot/usr/lib/system/libsystem_trace.dylib` shows the full expected trace stub surface.
- `nm -gU userland/libsystem/build/sysroot/usr/lib/system/libsystem_sandbox.dylib` shows the full expected sandbox stub surface.
- `nm -gU userland/libsystem/build/sysroot/usr/lib/system/libpanthera_extra.dylib` no longer shows any Phase 6 trace or sandbox exports.
- `otool -l userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib` confirms `LC_REEXPORT_DYLIB` entries for both new stub dylibs.

## Files changed for Phase 1

- `userland/libsystem/build/build_libsystem_c.sh`
- `userland/libsystem/build/shims/libsystem_c/panthera_libc_aliases.s`
- `userland/libsystem/build/build_libsystem_pthread.sh`
- `userland/libsystem/build/shims/libsystem_pthread/panthera_pthread_aliases.s`
- `userland/libsystem/build/libpanthera_extra_strip_symbols.txt`
- `userland/libsystem/build/obj/panthera_extra_aliases.s`

## Files changed for Phase 2

- `userland/libsystem/build/build_libclosure.sh`
- `userland/libsystem/build/relink_libSystem.sh`
- `userland/libsystem/build/obj/panthera_missing.c`
- `userland/libsystem/build/obj/panthera_extra_stubs.c`
- `userland/libsystem/build/shims/libsystem_c/panthera_libc_bridges.c`

## Files changed for Phase 3

- `userland/notifyd/build_notifyd.sh`

## Files changed for Phase 4

- `docs/SHIM_STATUS.md`
- `userland/libsystem/build/build_libsystem_c.sh`
- `userland/libsystem/build/libpanthera_extra_strip_symbols.txt`
- `userland/libsystem/build/sysroot/usr/lib/system/libsystem_c.dylib`
- `userland/libsystem/build/sysroot/usr/lib/system/libpanthera_extra.dylib`

## Files changed for Phase 5

- `docs/SHIM_STATUS.md`
- `userland/launchd/build_launchd.sh`
- `userland/libbsm/libbsm_stub.c`
- `userland/libsystem/build/build_layers567.sh`
- `userland/libsystem/build/build_libsystem_c.sh`
- `userland/libsystem/build/relink_libSystem.sh`
- `userland/libsystem/build/obj/panthera_kernel_phase5_syscalls.c`
- `userland/libsystem/build/obj/panthera_kernel_phase5_voucher.c`
- `userland/libsystem/build/obj/panthera_platform_phase5.c`
- `userland/libsystem/build/obj/compiler_rt_udivti3_wrapper.c`
- `userland/libsystem/build/obj/panthera_extra_bridge.c`
- `userland/libsystem/build/obj/panthera_extra_stubs.c`
- `userland/libsystem/build/obj/panthera_resolve_impl.c`
- `userland/libsystem/build/obj/panthera_resolve_wave2.c`
- `userland/libsystem/build/obj/panthera_tier1_syms.c`
- `userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib`
- `userland/libsystem/build/sysroot/usr/lib/libbsm.0.dylib`
- `userland/libsystem/build/sysroot/usr/lib/system/libpanthera_extra.dylib`
- `userland/libsystem/build/sysroot/usr/lib/system/libsystem_c.dylib`
- `userland/libsystem/build/sysroot/usr/lib/system/libsystem_kernel.dylib`
- `userland/libsystem/build/sysroot/usr/lib/system/libsystem_platform.dylib`
- removed: `userland/libsystem/build/sysroot/usr/lib/system/libpanthera_launchd.dylib`

## Files changed for Phase 6

- `docs/SHIM_STATUS.md`
- `userland/libsystem/build/build_libsystem_trace.sh`
- `userland/libsystem/build/build_libsystem_sandbox.sh`
- `userland/libsystem/build/relink_libSystem.sh`
- `userland/libsystem/build/obj/libsystem_trace_stub.c`
- `userland/libsystem/build/obj/libsystem_sandbox_stub.c`
- `userland/libsystem/build/obj/panthera_extra_stubs.c`
- `userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib`
- `userland/libsystem/build/sysroot/usr/lib/system/libpanthera_extra.dylib`

## Remaining shim-owned symbols after Phase 1

These still remain in shims after the export audit and were not eliminated in this phase.

### `libsystem_c`-adjacent residuals still in shims

- `strchrnul`
- `flsl`
- `fma`
- `clock`
- `fegetenv`
- `fesetenv`
- `fegetround`

Block-runtime ownership is no longer part of this residual list; it moved to the real `libclosure.dylib` in Phase 2.

### User/group and libc surface still missing from real `libsystem_c`

These are not currently exported by `libsystem_c`, and are also not cleanly moved into other canonical dylibs in this phase:

- `getpwnam_r`
- `getpwuid_r`
- `getgrgid_r`
- `getgrent`
- `setgrent`
- `endgrent`
- `getgroups`

### `libsystem_kernel`-adjacent residuals still in shims

- `vfork`
- `recv`
- `send`
- `vm_copy`
- `posix_spawn`
- `posix_spawnp`
- `posix_spawn_file_actions_*`
- `posix_spawnattr_*`

### `libsystem_pthread`-adjacent residuals still in shims

- `os_unfair_lock_lock`
- `os_unfair_lock_unlock`
- `os_unfair_lock_trylock`
- `__setjmp`
- `__longjmp`

### `libsystem_platform`-adjacent residuals still in shims

- `OSAtomicTestAndSetBarrier`

## Remaining shim-owned symbols after Phase 6

These remain outside canonical ownership after completing the Phase 6 trace/sandbox ownership split.

### `libpanthera_extra.dylib`

- `strchrnul`
- `flsl`
- `fma`
- `clock`
- `fegetenv`
- `fesetenv`
- `fegetround`
- `getpwnam_r`
- `getpwuid_r`
- `getgrgid_r`
- `getgrent`
- `setgrent`
- `endgrent`
- `getgroups`
- `vfork`
- `recv`
- `send`
- `vm_copy`
- `os_unfair_lock_lock`
- `os_unfair_lock_unlock`
- `os_unfair_lock_trylock`
- `__setjmp`
- `__longjmp`
- `OSAtomicTestAndSetBarrier`

### CoreFoundation support glue

- the remaining `cf_panthera_support.c` ownership is unchanged by Phase 5

## Phase 7 Status

### Result

- `cf_panthera_support.c` no longer defines `pthread_main_thread_np` or `pthread_atfork`; canonical ownership is now `libsystem_pthread.dylib`.
- `cf_panthera_support.c` no longer defines `uuid_generate_random` or `uuid_generate_time`; canonical ownership is now `libsystem_c.dylib`.
- `userland/libsystem/verify_exports.sh` still passes after the cleanup pass.
- the CF support object now exports 34 symbols instead of the earlier 38-symbol surface.

### What still remains in CF support

These symbols were intentionally kept because the active canonical dylibs do not yet export the required public names:

- permanent Panthera-specific surface:
  - `_NSGetExecutablePath`
  - `_dyld_image_count`
  - `_dyld_get_image_name`
  - `_dyld_get_image_vmaddr_slide`
  - `getsectbynamefromheader_64`
  - `getsegbyname`
  - `dlopen_preflight`
  - `NSStartSearchPathEnumeration`
  - `NSGetNextSearchPathEnumeration`
  - `_vproc_transaction_begin`
  - `_vproc_transaction_end`
  - `_vproc_transaction_count`
  - `_vproc_transaction_try_exit`
  - `_vproc_transactions_enable`
- still-unblocked leftovers that Phase 7 planned to remove later, but which are not yet owned by the active real dylibs:
  - `OSAtomicCompareAndSwap64Barrier`
  - `OSSpinLockLock`
  - `OSSpinLockUnlock`
  - `NXGetLocalArchInfo`
  - `NXFindBestFatArch`
  - `gethostuuid`
  - `bootstrap_strerror`
  - `mach_make_memory_entry_64`
  - `mach_vm_region`
  - `scalbn`
  - `asl_close`
  - `asl_free`
  - the 8 UNIX2003 socket wrappers

### Current blocker

`userland/corefoundation/build_cf.sh` now compiles the CF sources again after adding minimal XPC attribute compatibility macros for the SDK `vproc.h` path, but the final link still fails on unrelated pre-existing missing symbols:

- `_thread_resume`
- `_thread_suspend`
- `_vm_page_size`

Those symbols currently appear only in `libsystem_kernel.phase5_base.dylib`, not the active `libsystem_kernel.dylib`, so this is an existing libSystem ownership gap rather than a Phase 7 CF shim regression.

## Known blockers

There are no remaining Phase 6 export blockers. Later shim-elimination phases still remain, but the trace/sandbox ownership split and its verification cleanup are complete.

### `libsystem_kernel` rebuild model

This checkout still does not have a standalone checked-in full-source rebuild script for `libsystem_kernel.dylib`. Phase 5 solves the required ownership move by augmenting the existing canonical dylib in place from validated XNU wrapper objects.

### Remaining non-Phase-6 shims

`libpanthera_extra.dylib`, `libpanthera_patch` cleanup, and the CoreFoundation support glue still need their later planned elimination passes.

## Build and verification sequence used in Phase 1

After each meaningful batch of export ownership changes:

1. rebuild the affected real dylib
2. rerun `userland/libsystem/build/relink_libpanthera_extra.sh` when shim exports changed
3. rerun `userland/libsystem/build/relink_libSystem.sh`
4. rerun `userland/libsystem/verify_exports.sh`

The current Phase 1 endpoint is green on step 4.

## Next Phase

The next planned shim-elimination work is the remainder of Phase 7 from `docs/SHIM_ELIMINATION_PLAN.md`: move the still-kept CoreFoundation support leftovers into their canonical real dylibs, then rerun the CF rebuild once `_thread_resume`, `_thread_suspend`, and `_vm_page_size` are available from the active libSystem link path.
