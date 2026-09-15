# Fix Threading — Build and Load the pthread Kernel Extension

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules.

`pthread_create()` causes a kernel panic because the pthread kernel extension is not loaded. The boot log shows `pthread: kext not loaded, skipping init`. Without this kext, the kernel's pthread function pointers are NULL, and any thread creation attempt dereferences NULL+offset → page fault → panic.

The pthread kext is 3 C files (~4,000 lines) at `src/libpthread-519/kern/`. It registers thread management callbacks with the kernel via `pthread_kext_register()`. Once loaded, `pthread_create`, `pthread_mutex_*`, `pthread_cond_*`, and `pthread_rwlock_*` all work through the kernel.

## What the pthread Kext Does

`kern_init.c` (65 lines) is the entry point. It calls:
```c
pthread_kext_register((pthread_functions_t)&pthread_internal_functions, &pthread_kern);
```

This passes a table of function pointers to the kernel:
- `_bsdthread_create` — creates kernel threads for userspace pthreads
- `_bsdthread_register` — registers the userspace pthread library
- `_bsdthread_terminate` — cleans up thread resources
- `_psynch_mutexwait/drop` — kernel-level mutex support
- `_psynch_cvbroad/cvsignal/cvwait` — kernel-level condition variables
- `_psynch_rw_rdlock/wrlock/unlock` — kernel-level reader-writer locks
- `workq_*` — workqueue (GCD) thread management

The kernel provides callbacks back to the kext via `pthread_kern` (a `pthread_callbacks_t` struct).

## Source Files

All in `src/libpthread-519/kern/`:

| File | Lines | Purpose |
|------|-------|---------|
| `kern_init.c` | 65 | Kext entry point, registers function table |
| `kern_support.c` | 1,050 | Thread creation, workqueue support |
| `kern_synch.c` | 2,852 | Mutex, condvar, rwlock kernel implementations |
| `kern_internal.h` | — | Internal data structures |
| `synch_internal.h` | — | Sync primitive internals |
| `kern_trace.h` | — | Kdebug trace points |

## Build Approach

The pthread kext uses the same build pattern as other Panthera kexts. Use `kexts/OpenIOKit/build_kext.sh`:

```bash
cd kexts/OpenIOKit
bash build_kext.sh pthread \
    "${PANTHERA_ROOT}/src/libpthread-519" \
    --extra-include "${PANTHERA_ROOT}/src/libpthread-519" \
    --extra-include "${PANTHERA_ROOT}/src/libpthread-519/kern" \
    --extra-include "${PANTHERA_ROOT}/src/libpthread-519/private" \
    kern/kern_init.c \
    kern/kern_support.c \
    kern/kern_synch.c
```

### Expected Challenges

1. **Missing headers:** The pthread kext includes `<pthread/bsdthread_private.h>`, `<pthread/priority_private.h>`, `<pthread/workqueue_syscalls.h>` which are in `src/libpthread-519/private/`. Add as include path.

2. **`<sys/pthread_shims.h>`:** Should be in the XNU exported headers at `BUILD/obj/EXPORT_HDRS/`. If not, it's in `src/xnu-10002.41.9/bsd/sys/pthread_shims.h`.

3. **Kernel API changes:** The libpthread-519 version may not match xnu-10002.41.9 exactly. The `pthread_functions_s` struct in the kext must match what `pthread_kext_register()` expects. Check `src/xnu-10002.41.9/bsd/sys/pthread_shims.h` for the expected struct layout and adjust if fields differ.

4. **`current_uthread()` conflict:** `kern_init.c` defines `current_uthread()` which may conflict with the kernel's own definition. The kext's version calls through `pthread_kern->get_bsdthread_info()`. May need to rename or guard with `#ifndef`.

5. **Workqueue functions:** `workq_create_threadstack`, `workq_setup_thread`, etc. may reference kernel symbols that need to be resolved. If workqueue support is too complex, stub the workq functions initially (return ENOTSUP) — basic `pthread_create` doesn't need workqueues.

6. **`os/log.h`:** kern_support.c includes `<os/log.h>` for kernel logging. May need a stub or the kernel's log infrastructure.

## Info.plist

Create `kexts/OpenIOKit/plists/pthread-Info.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleIdentifier</key>
    <string>com.apple.kec.pthread</string>
    <key>CFBundleExecutable</key>
    <string>pthread</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundlePackageType</key>
    <string>KEXT</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>OSBundleRequired</key>
    <string>Root</string>
    <key>OSBundleCompatibleVersion</key>
    <string>1.0</string>
    <key>OSBundleLibraries</key>
    <dict>
        <key>com.apple.kpi.bsd</key>
        <string>12.0</string>
        <key>com.apple.kpi.libkern</key>
        <string>11.2</string>
        <key>com.apple.kpi.mach</key>
        <string>11.2</string>
        <key>com.apple.kpi.private</key>
        <string>11.2</string>
        <key>com.apple.kpi.unsupported</key>
        <string>11.2</string>
    </dict>
</dict>
</plist>
```

## Staging

After building, the kext must be:

1. **Staged to boot extensions:** Copy the `.kext` bundle to `boot/efi/staging/System/Library/Extensions/pthread.kext/`

2. **Added to the bootloader's kext manifest:** Edit `boot/efi/loader/src/bootx64.c` to add the pthread kext to the boot kext list. Find the existing kext entries (AppleAPIC, IOPCIFamily, etc.) and add:
```c
{"pthread", (const CHAR16 *)u"\\System\\Library\\Extensions\\pthread.kext\\Info.plist", NULL},
```

3. **Rebuild the bootloader:** `bash boot/efi/build_bootx64.sh`

4. **Rebuild the root image:** `bash rootfs/create_hfs_root_image.sh --force`

## Verification

### Step 1: Boot and check kext loads

Look for the absence of `pthread: kext not loaded, skipping init` in the boot log. Instead you should see the kext loading message or simply no pthread error.

### Step 2: Run the pthread test

```bash
/usr/bin/test_pthread
```

Expected output:
```
test_pthread: starting
main thread: 0x...
pthread_create OK, thread=0x...
main: waiting for cond_signal...
[thread 1] started, tid=0x...
[thread 1] shared_value=100, signaling
[thread 1] done
main: woke up, shared_value=100
pthread_join OK, retval=42
test_pthread: ALL PASS
```

### Step 3: If it passes, rebuild packages without --disable-threads

The immediate wins:
- OpenSSL: remove `no-threads` → threaded crypto operations
- curl: gets connection pooling and async DNS
- Future packages stop needing `--disable-threads`

## Fallback: If the Kext Won't Build

If the pthread kext has too many version mismatches with xnu-10002.41.9 and can't be compiled cleanly, there's an alternative: **build the pthread support directly into the kernel.**

XNU's `bsd/kern/pthread_shims.c` has the `pthread_kext_register()` function. Instead of loading an external kext, you could compile the pthread kext source directly into the kernel by:
1. Adding the 3 source files to the XNU build
2. Calling `pthread_start()` from `bsd_init()` after the BSD subsystem is initialized

This avoids all kext loading infrastructure. Check how xnu-10002.41.9 handles this — newer XNU versions may have integrated pthread directly.

## Rules

- Follow the existing kext build pattern in `kexts/OpenIOKit/`
- Do not modify the frozen libsystem_pthread.dylib
- If workqueue functions are too complex, stub them (return ENOTSUP) — basic threading doesn't need workqueues
- Kernel source modifications are acceptable for XNU
- Boot test and read output — don't ask the user
- No deferred work — either the kext loads and threading works, or document exactly why it doesn't

## Success Criteria

- Boot log shows no `pthread: kext not loaded` message
- `test_pthread` prints `ALL PASS`
- No kernel panics from thread creation
- `pthread_create`, `pthread_join`, `pthread_mutex_*`, `pthread_cond_*` all functional
