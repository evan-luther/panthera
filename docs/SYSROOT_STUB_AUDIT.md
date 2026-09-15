# Panthera -- Sysroot Stub Audit

## Status: COMPLETE (2026-04-05)

All dangerous stubs have been fixed. No stub in the sysroot silently corrupts program behavior.

## Scope

20 source files in `userland/libsystem/build/obj/panthera_*.c`. These compile into
`libpanthera_extra.dylib` (346 exports) via three translation units:

| Translation unit | Includes | Role |
|-----------------|----------|------|
| `panthera_extra_core.c` | `resolve_impl.c`, `mach_globals.c`, `malloc_override.c` | Core impls, Mach, malloc |
| `panthera_extra_stubs.c` | `missing.c`, `pthread_simple.c` | Stubs, string wrappers, pthread |
| `panthera_extra_bridge.c` | `resolve_wave2.c`, `resolve_wave3.c` | Bridge, wave2/3, getaddrinfo |

Plus `panthera_extra_aliases.s` and `panthera_fmtcheck_wrapper.c`.

Files NOT compiled into any library (dead code):
`panthera_runtime_bridge.c`, `panthera_boot_bridge.c`, `panthera_layer7.c`,
`panthera_mach_funcs.c`, `panthera_patch.c`, `panthera_printf.c`,
`panthera_remaining.c`, `panthera_stdio_init.c`, `panthera_if_nametoindex.c`,
`panthera_inet_ntop.c` (last two have content merged into `extra_bridge.c`).

## Re-export Order

libSystem.B.dylib re-exports in this order (first wins on duplicate symbols):
1. libsystem_kernel (FROZEN)
2. libsystem_platform (FROZEN)
3. libsystem_malloc
4. **libpanthera_extra** (position 4 -- shadows 5-10)
5. libsystem_c
6. libsystem_info
7. libsystem_pthread (FROZEN)
8. libdispatch (FROZEN)
9. libxpc (FROZEN)
10. libpanthera_launchd (FROZEN)

## Intentional Shadowing

Two symbols in libpanthera_extra intentionally shadow later libraries:

| Symbol | Shadowed lib | Reason |
|--------|-------------|--------|
| `___fmtcheck` | libsystem_c | Both from Apple Libc source. Identical behavior. |
| `_getaddrinfo` | libsystem_info | libpanthera_extra has standalone DNS resolver (UDP RFC 1035). Works without full Libinfo stack. |

## Stripped Symbols (not exported, real impl in later library)

The `libpanthera_extra_strip_symbols.txt` unexports ~230 symbols that have real
implementations in later libraries. Key ones:

| Symbol | Real provider | Notes |
|--------|--------------|-------|
| `_vsscanf` | libsystem_c | Stub returns 0; real libc version used |
| `_setlogmask` | libsystem_c | Stub returns 0xff; real libc version used |
| `_tcflush` | libsystem_c | Stub no-op; real libc version used |
| `_gethostbyname` | libsystem_info | Stub returns localhost; real version used |
| `_gethostbyaddr` | libsystem_info | Stub returns localhost; real version used |
| `_getpwnam_r` | (stripped) | Stub in wave3; real version in libsystem_info |
| `_pthread_cond_signal` | libsystem_pthread | Stub no-op; real version used |
| `_pthread_cond_broadcast` | libsystem_pthread | Stub no-op; real version used |
| `_pthread_cond_wait$UNIX2003` | libsystem_pthread | Stub no-op; real version used |
| `_pthread_rwlock_*$UNIX2003` | libsystem_pthread | Stub no-ops; real versions used |
| `_initgroups` | libsystem_info | Stripped; real version used |

## Fixed Stubs (was DANGEROUS, now corrected)

### mach_approximate_time / mach_continuous_time / mach_continuous_approximate_time

**File:** `panthera_extra_stubs.c:242-244`
**Was:** Returns 0 (time-based operations silently broken)
**Fix:** Forward to `mach_absolute_time()` in libsystem_kernel

```c
extern uint64_t mach_absolute_time(void);
uint64_t mach_approximate_time(void) { return mach_absolute_time(); }
uint64_t mach_continuous_time(void) { return mach_absolute_time(); }
uint64_t mach_continuous_approximate_time(void) { return mach_absolute_time(); }
```

### os_alloc_once (no-underscore C name)

**File:** `panthera_extra_stubs.c:300`
**Was:** `void *os_alloc_once(void) { return NULL; }` -- wrong signature, callers crash
**Fix:** Forward to real `_os_alloc_once()` in `resolve_impl.c:602`

```c
extern void *_os_alloc_once(unsigned int, unsigned long, void (*)(void *));
void *os_alloc_once(unsigned int slot, unsigned long sz, void (*init)(void *)) {
    return _os_alloc_once(slot, sz, init);
}
```

The real `_os_alloc_once` (C name, symbol `__os_alloc_once`) uses a slot table with
bump allocator for early-init safety. The `os_alloc_once` (C name, symbol `_os_alloc_once`)
now correctly forwards to it.

## MISSING Stubs (visible failure, not silent corruption)

These stubs return error codes that callers detect. Programs fail visibly, not silently.

| Function | File | Returns | Impact |
|----------|------|---------|--------|
| `regcomp()` | extra_stubs:263 | 1 (error) | Regex compilation fails; programs report error |
| `regexec()` | extra_stubs:270 | 1 (no match) | Regex matching fails; programs report no matches |
| `vfscanf()` | resolve_impl:340 | 0 (no items) | Direct vfscanf callers get 0 items; scanf/fscanf/sscanf in libsystem_c use internal engine and work correctly |
| `__svfscanf_l()` | resolve_impl:334 | 0 (no items) | Internal scanf engine stub; libsystem_c has its own working version |

**Note:** `sscanf`, `scanf`, `fscanf` from libsystem_c work correctly (built from 440 Apple
source files with internal scanf engine). Only direct `vfscanf` callers are affected.

## SAFE Stubs (returning 0/NULL/error is correct behavior)

### Logging/notification no-ops
| Function | File | Why safe |
|----------|------|----------|
| `os_log_create()` | extra_stubs:36 | Returns 0 = "no log object". Logging is optional. |
| `os_log_type_enabled()` | extra_stubs:37 | Returns 0 = "logging disabled". Correct. |
| `_os_log_*_impl()` | extra_stubs:38-41 | Log emission no-ops. |
| `notify_register_check/peek/cancel/post()` | extra_stubs:44-66 | No notifyd daemon. No-op is correct. |
| `notify_check()` | extra_bridge:580 | Returns "no change". Prevents spurious config reloads. |

### POSIX error returns
| Function | File | Returns | Why safe |
|----------|------|---------|----------|
| `posix_spawn()` | extra_stubs:247 | 63 (ENOSYS) | Callers check return value |
| `setpriority()` | extra_stubs:313 | -1 (EPERM) | Correct error |
| `settimeofday()` | extra_stubs:320 | -1 | No clock set capability |
| `socketpair$UNIX2003()` | extra_stubs:382 | -1 | Not implemented |
| `open_dprotected_np()` | extra_stubs:400 | -1 | Apple-specific, not supported |
| `acl_copy_ext_native()` | extra_stubs:283 | -1 | No ACL support |
| `acl_size()` | extra_stubs:295 | -1 | No ACL support |
| `dbopen()` | extra_stubs:302 | NULL | No db library |
| `ether_aton()` | extra_stubs:326 | NULL | Not implemented |
| `clock()` | extra_stubs:452 | -1 | `(clock_t)-1` = "not available" per POSIX |

### Thread primitives (single-threaded system)
| Function | File | Why safe |
|----------|------|----------|
| `pthread_mutex_*()` | pthread_simple:55-83 | Track locked state; correct for single-threaded |
| `pthread_mutexattr_*()` | pthread_simple:81-83 | No-op attribute setup |
| `pthread_rwlock_init/destroy()` | pthread_simple:143-146 | No-op; only plain variants exported (UNIX2003 in libsystem_pthread) |
| `pthread_rwlock_rdlock/wrlock/unlock()` | pthread_simple:147-149 | No-op; single-threaded. UNIX2003 variants from libsystem_pthread. |
| `pthread_cond_init/destroy()` | pthread_simple:158-161 | No-op; cond_signal/broadcast stripped (real in libsystem_pthread) |
| `pthread_cond_wait()` | pthread_simple:164 | No-op plain variant; UNIX2003 stripped (real in libsystem_pthread) |
| `pthread_create()` | pthread_simple:115 | Returns EAGAIN = "can't create threads". Correct. |
| `pthread_join()` | pthread_simple:120 | Returns ESRCH. Correct. |
| `pthread_attr_destroy()` | pthread_simple:195 | Cleanup no-op |
| `FLOCKFILE/FUNLOCKFILE()` | missing:51-52 | File locking no-ops; single-threaded |

### Mach/VM stubs
| Function | File | Why safe |
|----------|------|----------|
| `vm_region_64()` | extra_stubs:311 | Returns KERN_INVALID_ADDRESS; used by vfprintf debug path |
| `mach_port_mod_refs()` | wave2:102 | No-op; single-task system |
| `mig_dealloc_reply_port()` | wave2:116 | No-op; no Mach IPC reply ports |
| `host_info()` | wave2:121 | Returns zeroed info |
| `thread_switch/swtch_pri()` | wave2:135-143 | Yield no-ops; single CPU |
| `thread_info/thread_policy()` | wave2:146-158 | Returns zeroed info |
| `mach_boottime_usec()` | resolve_impl:509 | Returns 0; boot time not tracked |
| `mach_port_deallocate()` | resolve_impl:534 | No-op; KERN_SUCCESS |
| `semaphore_create()` | resolve_impl:570 | Returns dummy; unused |
| `task_set_special_port()` | resolve_impl:576 | No-op |

### Nano allocator stubs
| Function | File | Why safe |
|----------|------|----------|
| `nanov2_init/configure()` | wave2:191-196 | No-ops; nano zone disabled |
| `nanov2_create_zone()` | wave2:192 | Returns NULL = "don't use nano zone" |
| `nanov2_forked_zone()` | wave2:197 | Returns NULL |

### dyld introspection
| Function | File | Why safe |
|----------|------|----------|
| `_dyld_get_image_header()` | wave2:268 | Returns NULL; no dynamic loading |
| `_dyld_get_image_slide()` | wave2:274 | Returns 0 |
| `_dyld_is_memory_immutable()` | wave2:278 | Returns 0 = "not immutable" (safe default) |
| `dyld_process_is_restricted()` | wave2:283 | Returns 0 = "not restricted" |

### Misc safe stubs
| Function | File | Why safe |
|----------|------|----------|
| `dlclose()` | extra_stubs:223 | No-op return 0; correct |
| `getgroups$DARWIN_EXTSN()` | extra_stubs:392 | Returns 0 supplementary groups |
| `NSVersionOfLinkTimeLibrary()` | missing:65 | Returns -1 = "not found" |
| `__printf_arginfo_pct/render_*()` | missing:119-121 | Internal printf extensions; unused |
| `__printf_comp()` | resolve_impl:244 | Returns NULL = use legacy printf path |
| `posix_spawnattr_*_qos_clamp_np()` | resolve_impl:358-366 | Apple QoS; no-op is fine |
| `os_thread_self_restrict_rwx_is_supported()` | wave2:206 | Returns 0 = "not supported" |
| `mbr_uid_to_uuid/gid_to_uuid/uuid_to_id()` | (stripped) | Returns -1; no opendirectoryd |
| `tcsetpgrp()` | wave3:540 | No-op; no real terminal pgrp |
| `tcsetattr()` | wave3:579 | No-op; no real termios |
| `tcdrain()` | wave3:584 | Returns 0 (success); no real terminal to drain |
| `tcflow()` | wave3:586 | No-op |
| `tcsendbreak()` | wave3:587 | No-op |
| `sethostname()` | wave3:90 | No-op |
| `getservbyname/getservbyport()` | wave3:102-110 | Returns NULL; no service db |
| `openlog/closelog()` | wave3:25-29 | No-ops; syslog writes to stderr |
| `if_indextoname()` | extra_bridge:553 | Returns NULL with ENXIO |
| `pthread_fchdir_np()` | pthread_simple:220 | No-op |
| `_os_semaphore_dispose()` | wave2:229 | No-op |

## Real Implementations (not stubs)

The majority of functions in these files are **working implementations**, not stubs:

- **String functions:** strlen, strcmp, strcpy, strncpy, strlcpy, strlcat, memmove, memcpy, memset, bzero, memcmp, strchr, strstr, strnlen, strncat, strerror, strchrnul, strtok_r (all forward to `__platform_*` or implemented directly)
- **Math/atomic:** OSAtomic* (using `__sync_*` builtins), os_unfair_lock_* (real spinlocks)
- **Mach:** mach_absolute_time (rdtsc), mach_task_self/host_self (real Mach traps), mach_timebase_info, clock_get_time
- **Memory:** malloc/calloc/free/realloc (arena allocator with mmap), os_alloc_once (slot table)
- **Networking:** getaddrinfo (full DNS resolver), inet_ntop/ntop4/ntop6, inet_aton/pton, if_nametoindex (ioctl), bind/listen/accept (syscalls), recv/send (via recvfrom/sendto)
- **PTY/terminal:** openpty, forkpty, login_tty, tcgetattr (returns defaults), cfget/set speed
- **User management:** getlogin, crypt, uname, getpwnam_r, getgrgid_r, getgrent
- **Process:** fork/vfork, execve, pipe, kill, waitpid, setjmp/longjmp (assembly)
- **Printf/stdio:** __dtoa family, __sfp/__sfprelease (FILE pool), __xvprintf, vfprintf_l
- **Locale/collation:** __collate_* (C locale), __maskrune_l, __get_locale_env
- **Misc:** strtol/strtoul/strtoimax/strtoumax family, getopt, unsetenv, uuid_generate/compare/unparse, sigsetmask, fmtcheck (from Apple source)

## Verification

After fixes, verified:
```
bash userland/libsystem/build/relink_libpanthera_extra.sh  # 361 exports
bash userland/libsystem/build/relink_libSystem.sh          # re-export chain intact
bash tools/build_shared_cache.sh                           # 14 dylibs, 3.4 MB
bash userland/libsystem/verify_exports.sh                  # all 58 critical symbols present
bash rootfs/create_hfs_root_image.sh --force               # 108 commands staged
```

Boot test: system boots, launchd starts services, test_bootstrap Mach IPC passes,
network comes up (10.0.2.15), DNS resolves, ping works.
