# CoreFoundation Integration — Upgrade Panthera Infrastructure

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules.

CoreFoundation is now live on Panthera — 1,562 exports, 19 dylibs in the shared cache, full dependency chain (libc++abi → libc++ → ICU → objc4 → CF). The CF verification test passes: CFString, CFDictionary, CFArray, CFPropertyList XML serialization, CFRunLoop, CFLocale all work.

This task puts CF into the critical path by upgrading Panthera's infrastructure to use it.

## Three Tasks (In Priority Order)

### Task 1: Replace launchd's Plist Parser with CFPropertyList

**Goal:** Replace the hand-written ~309 line XML parser in `launchd_all_stubs.c` with CF's plist API.

**Current state:** `panthera_parse_plist_file()` (line 986) and the `panthera_xml_*` helper functions manually parse XML plists character by character. It only handles a subset of keys and only XML format. It's fragile and can't read binary plists.

**Replacement:** Use CFPropertyList to read any plist format in ~30 lines:

```c
#include <CoreFoundation/CoreFoundation.h>

static bool
panthera_parse_plist_file_cf(const char *path, struct panthera_job *job)
{
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
        (const UInt8 *)path, strlen(path), false);
    if (!url) return false;

    CFReadStreamRef stream = CFReadStreamCreateWithFile(NULL, url);
    CFRelease(url);
    if (!stream) return false;

    if (!CFReadStreamOpen(stream)) {
        CFRelease(stream);
        return false;
    }

    CFPropertyListRef plist = CFPropertyListCreateWithStream(NULL,
        stream, 0, kCFPropertyListImmutable, NULL, NULL);
    CFReadStreamClose(stream);
    CFRelease(stream);

    if (!plist || CFGetTypeID(plist) != CFDictionaryGetTypeID()) {
        if (plist) CFRelease(plist);
        return false;
    }

    CFDictionaryRef dict = (CFDictionaryRef)plist;

    /* Extract Label */
    CFStringRef label = CFDictionaryGetValue(dict, CFSTR("Label"));
    if (label && CFGetTypeID(label) == CFStringGetTypeID()) {
        CFStringGetCString(label, job->label, sizeof(job->label),
            kCFStringEncodingUTF8);
        job->in_use = true;
    }

    /* Extract Program */
    CFStringRef program = CFDictionaryGetValue(dict, CFSTR("Program"));
    if (program && CFGetTypeID(program) == CFStringGetTypeID()) {
        CFStringGetCString(program, job->program, sizeof(job->program),
            kCFStringEncodingUTF8);
    }

    /* Extract ProgramArguments */
    CFArrayRef args = CFDictionaryGetValue(dict, CFSTR("ProgramArguments"));
    if (args && CFGetTypeID(args) == CFArrayGetTypeID()) {
        CFIndex count = CFArrayGetCount(args);
        if (count > PANTHERA_MAX_ARGS) count = PANTHERA_MAX_ARGS;
        for (CFIndex i = 0; i < count; i++) {
            CFStringRef arg = CFArrayGetValueAtIndex(args, i);
            if (arg && CFGetTypeID(arg) == CFStringGetTypeID()) {
                CFStringGetCString(arg, job->argv_storage[i],
                    sizeof(job->argv_storage[0]), kCFStringEncodingUTF8);
                job->argv[i] = job->argv_storage[i];
                job->argc = i + 1;
            }
        }
        job->argv[job->argc] = NULL;
    }

    /* Extract booleans */
    CFBooleanRef runAtLoad = CFDictionaryGetValue(dict, CFSTR("RunAtLoad"));
    if (runAtLoad) job->run_at_load = CFBooleanGetValue(runAtLoad);

    CFBooleanRef keepAlive = CFDictionaryGetValue(dict, CFSTR("KeepAlive"));
    if (keepAlive) job->keep_alive = CFBooleanGetValue(keepAlive);

    /* Extract StandardOutPath / StandardErrorPath */
    CFStringRef outPath = CFDictionaryGetValue(dict, CFSTR("StandardOutPath"));
    if (outPath && CFGetTypeID(outPath) == CFStringGetTypeID()) {
        CFStringGetCString(outPath, job->stdout_path,
            sizeof(job->stdout_path), kCFStringEncodingUTF8);
    }

    CFStringRef errPath = CFDictionaryGetValue(dict, CFSTR("StandardErrorPath"));
    if (errPath && CFGetTypeID(errPath) == CFStringGetTypeID()) {
        CFStringGetCString(errPath, job->stderr_path,
            sizeof(job->stderr_path), kCFStringEncodingUTF8);
    }

    /* Extract MachServices */
    CFDictionaryRef machServices = CFDictionaryGetValue(dict, CFSTR("MachServices"));
    if (machServices && CFGetTypeID(machServices) == CFDictionaryGetTypeID()) {
        CFIndex svcCount = CFDictionaryGetCount(machServices);
        if (svcCount > PANTHERA_MAX_JOB_SERVICES)
            svcCount = PANTHERA_MAX_JOB_SERVICES;
        const void **keys = alloca(svcCount * sizeof(void*));
        CFDictionaryGetKeysAndValues(machServices, (const void **)keys, NULL);
        for (CFIndex i = 0; i < svcCount; i++) {
            CFStringRef svcName = (CFStringRef)keys[i];
            if (CFGetTypeID(svcName) == CFStringGetTypeID()) {
                CFStringGetCString(svcName,
                    job->mach_services[job->mach_service_count],
                    sizeof(job->mach_services[0]), kCFStringEncodingUTF8);
                job->mach_service_count++;
            }
        }
    }

    CFRelease(plist);
    return job->in_use;
}
```

**Implementation steps:**

1. Add `#include <CoreFoundation/CoreFoundation.h>` to `launchd_all_stubs.c`
2. Add the new `panthera_parse_plist_file_cf()` function
3. Rename the old `panthera_parse_plist_file()` to `panthera_parse_plist_file_legacy()`
4. Replace `panthera_parse_plist_file()` with a wrapper that tries CF first, falls back to legacy:
```c
static bool
panthera_parse_plist_file(const char *path, struct panthera_job *job)
{
    if (panthera_parse_plist_file_cf(path, job))
        return true;
    /* Fallback to legacy parser if CF isn't initialized */
    return panthera_parse_plist_file_legacy(path, job);
}
```
5. **Important:** launchd must link against libCoreFoundation. Update `userland/launchd/build_launchd.sh` to add `-lCoreFoundation` and `-I` the CF headers path.
6. Rebuild launchd: `bash userland/launchd/build_launchd.sh`
7. Rebuild root image and boot test
8. Verify all existing plists still load (com.panthera.login, com.panthera.netbringup, com.panthera.sshd, etc.)
9. Once verified, the `panthera_xml_*` helper functions and legacy parser can be removed entirely

**What this unlocks:** launchd can now read binary plists, handle any plist key Apple supports, and survive malformed XML that would crash the manual parser. Future plist keys (StartInterval, StartCalendarInterval, WatchPaths, etc.) just need a few lines of CFDictionary extraction.

**Risk:** Low. CF is verified working. The fallback to the legacy parser means if CF has an issue during early boot (before CF is fully initialized), the system still boots.

---

### Task 2: Audit cf_panthera_support.c

**Goal:** Every stub in the 356-line CF support shim must be verified correct now that CF runs for real.

**File:** `userland/corefoundation/cf_panthera_support.c`

**Audit each function:**

| Function | Current behavior | Risk | Fix needed? |
|----------|-----------------|------|-------------|
| `pthread_main_thread_np()` | Captures via constructor, returns cached pthread_t | **VERIFY** — Does CFRunLoop correctly identify the main thread? | Test with CFRunLoop |
| `pthread_atfork()` | Stores up to 16 handlers | LOW — correct for single-process | Fine |
| `OSAtomicCompareAndSwap64Barrier()` | Uses `__sync_bool_compare_and_swap` | OK — correct intrinsic | Fine |
| `OSSpinLockLock/Unlock()` | Uses `__sync` intrinsics with pause loop | OK — correct for x86_64 | Fine |
| `_NSGetExecutablePath()` | Reads from `_NSGetArgv()[0]` | **CHECK** — does this return the right path? | Test with a known binary |
| `_dyld_image_count()` | Returns 0 | **DANGEROUS** — CF uses this to enumerate loaded images | Implement properly or return 1 for main binary |
| `_dyld_get_image_name()` | Returns NULL | **DANGEROUS** — CF calls this in CFBundle | Return main executable path for index 0 |
| `_dyld_get_image_vmaddr_slide()` | Returns 0 | **CHECK** — CF uses this for ASLR offset | 0 may be correct if no ASLR |
| `getsectbynamefromheader_64()` | Real implementation — walks Mach-O | OK — correct | Fine |
| `getsegbyname()` | Returns NULL | **DANGEROUS** — CF may use this | Implement — read main binary's mach_header |
| `NXGetLocalArchInfo()` | Returns x86_64 info | OK — correct | Fine |
| `NXFindBestFatArch()` | Simple linear search | OK — correct | Fine |
| `NSStartSearchPathEnumeration()` | Returns /Library, /System/Library | OK — reasonable | Fine |
| `_vproc_transaction_*()` | No-op stubs | OK — no vproc on Panthera | Fine |
| `asl_close/asl_free()` | No-op stubs | OK — ASL not implemented | Fine |
| `bootstrap_strerror()` | Returns generic string | LOW — cosmetic | Could be improved |
| `dlopen_preflight()` | Returns false | OK — no dlopen support | Fine |
| `gethostuuid()` | Returns "PANTHERA" in first 8 bytes | **CHECK** — CF uses this for distributed notifications | Should use a proper deterministic UUID |
| `uuid_generate_random()` | Uses `rand()` | **FIX** — use `arc4random()` for crypto-quality UUIDs | Replace rand() with arc4random_buf() |
| `uuid_generate_time()` | Calls uuid_generate_random with version 1 marker | **FIX** — same arc4random issue | Same fix |
| `mach_make_memory_entry_64()` | Returns KERN_FAILURE | LOW — CF can handle failure | Fine |
| `mach_vm_region()` | Returns KERN_FAILURE | **CHECK** — CFAllocator may use this for memory zone inspection | Test CF allocation patterns |
| `scalbn()` | Uses `__builtin_scalbn` | OK — correct | Fine |
| UNIX2003 variants | Forward to base functions | OK — correct | Fine |

**Priority fixes:**
1. `uuid_generate_random` — replace `rand()` with `arc4random_buf()` (security issue)
2. `_dyld_image_count` — return at least 1, provide main binary info for index 0
3. `_dyld_get_image_name` — return main executable path for index 0
4. `getsegbyname` — implement using the main binary's mach_header

**After fixing:** Rebuild CF, shared cache, root image. Boot test. Run the CF verification test again.

---

### Task 3: Real XPC Implementation

**Goal:** Replace the 177-line XPC stub library with a real implementation that can create dictionaries, connections, and send/receive messages over Mach ports.

**Note:** Apple does NOT provide XPC source. It's closed source. But the API is documented and the wire protocol uses Mach messages. We implement from scratch using:
- Mach IPC (have it — ports, messages, rights all working)
- CF types for the internal object model (have it)
- libdispatch for event dispatch (have it)
- launchd bootstrap for service discovery (have it)

**XPC Object Model:**

XPC objects are typed containers. Internally, each is a CF-compatible object:

```c
struct xpc_object_s {
    uint32_t xo_type;       /* XPC_TYPE_* */
    uint32_t xo_refcount;
    union {
        bool bool_val;
        int64_t int64_val;
        uint64_t uint64_val;
        double double_val;
        struct { char *ptr; size_t len; } string_val;
        struct { void *ptr; size_t len; } data_val;
        CFMutableDictionaryRef dict_val;
        CFMutableArrayRef array_val;
        mach_port_t port_val;
        /* ... */
    };
};
```

**Minimal API Surface (implement these first):**

Object creation/access:
- `xpc_dictionary_create()` — allocate dictionary backed by CFMutableDictionary
- `xpc_dictionary_set_string/int64/uint64/value/mach_send/mach_recv()`
- `xpc_dictionary_get_string/int64/uint64/value/audit_token/mach_send()`
- `xpc_array_create()`, `xpc_array_append_value()`
- `xpc_string_create()`, `xpc_int64_create()`, `xpc_uint64_create()`, `xpc_bool_create()`
- `xpc_retain()`, `xpc_release()`, `xpc_get_type()`, `xpc_copy_description()`

Connection management:
- `xpc_connection_create_mach_service(name, queue, flags)` — looks up service via bootstrap_look_up, creates connection with dispatch queue
- `xpc_connection_set_event_handler(conn, handler)` — sets the block called on incoming messages
- `xpc_connection_resume(conn)` — starts the connection (begins receiving)
- `xpc_connection_send_message(conn, msg)` — serializes XPC dict to Mach message, sends
- `xpc_connection_send_message_with_reply(conn, msg, queue, handler)` — send + async reply
- `xpc_connection_send_message_with_reply_sync(conn, msg)` — send + wait for reply

Service hosting:
- `xpc_main(handler)` — registers as a service, runs dispatch main queue, calls handler for each incoming connection

**Wire Format:** XPC messages are Mach messages with a serialized XPC dictionary as the payload. The serialization format is a simple TLV (type-length-value) encoding. Each key-value pair is: `uint32_t type | uint32_t name_len | name_bytes | uint32_t value_len | value_bytes`.

**Implementation file:** Replace `userland/libsystem/build/obj/libxpc_stubs.c` with a real implementation. Relink `libxpc.dylib` via the existing build infrastructure. The library is currently FROZEN — you will need to unfreeze it to rebuild:

1. Check `OS_BUILD_ROADMAP.md` for the frozen dylib rules
2. Ask the user for permission to unfreeze libxpc before modifying it
3. After implementation, refreeze it

**Testing:** Write a test service and client:
```c
// Service (registered via launchd plist with MachServices)
xpc_main(^(xpc_connection_t peer) {
    xpc_connection_set_event_handler(peer, ^(xpc_object_t msg) {
        const char *greeting = xpc_dictionary_get_string(msg, "greeting");
        xpc_object_t reply = xpc_dictionary_create_reply(msg);
        xpc_dictionary_set_string(reply, "response", "hello from XPC service");
        xpc_connection_send_message(peer, reply);
        xpc_release(reply);
    });
    xpc_connection_resume(peer);
});

// Client
xpc_connection_t conn = xpc_connection_create_mach_service("com.panthera.test.xpc",
    dispatch_get_main_queue(), 0);
xpc_connection_resume(conn);
xpc_object_t msg = xpc_dictionary_create(NULL, NULL, 0);
xpc_dictionary_set_string(msg, "greeting", "hello");
xpc_object_t reply = xpc_connection_send_message_with_reply_sync(conn, msg);
printf("XPC reply: %s\n", xpc_dictionary_get_string(reply, "response"));
```

**Note:** XPC uses blocks (`^{}` syntax) heavily. This requires the blocks runtime (`_Block_copy`, `_Block_release`) which is already in the sysroot via libpanthera_extra.

---

## Build & Test

After each task:
```bash
bash userland/launchd/build_launchd.sh    # Task 1
bash tools/build_shared_cache.sh          # If CF or XPC changed
bash rootfs/create_hfs_root_image.sh --force
PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot --ssh-port 0 \
    --root-disk images/qemu/panthera-root.img
```

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- libxpc is FROZEN — get user permission before modifying
- Keep the legacy plist parser as a fallback in Task 1
- After sysroot changes: relink chain → shared cache → verify exports
- Boot test and read output — don't ask the user
- No deferred work — each task must be complete before moving to the next
- Task order is strict: 1 → 2 → 3

## Success Criteria

**Task 1:**
- All existing launchd plists still load and services start
- launchd reads plists via CFPropertyList (verify by checking log output or adding a trace)
- Boot is not slower (CF plist reading should be faster than the manual parser)

**Task 2:**
- `uuid_generate_random()` uses `arc4random_buf()`, not `rand()`
- `_dyld_image_count()` returns ≥1
- `_dyld_get_image_name(0)` returns the main executable path
- CF test program still passes all checks

**Task 3:**
- XPC dictionary creation/get/set works (unit test)
- XPC connection over Mach service works (service + client test)
- `xpc_main()` runs a service that responds to messages
- launchd's bootstrap is used for service discovery
