# Phase D — Full CoreFoundation Build Chain

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules. Read `docs/PACKAGE_PORTING_GUIDE.md` for symbol audits.

This task builds a complete, real CoreFoundation from Apple open-source — no stubs, no shortcuts. Four dependencies must be built in order before CF itself.

## Build Order (strict dependency chain)

```
1. libc++abi  → C++ ABI runtime (exception handling, RTTI)
2. libc++     → C++ standard library (std::map, std::string, containers)
3. ICU        → International Components for Unicode
4. objc4      → Objective-C runtime
5. CF         → CoreFoundation
```

Each step depends on the previous. Build in order.

## What Already Exists in the Sysroot

- libdispatch (303 exports, frozen binary)
- libpthread (204 exports, frozen binary, pthread_create works)
- libxml2 (built from source, in sysroot)
- libffi (built from source, in sysroot)
- libedit (built from source, in sysroot)
- Mach IPC (working — bootstrap server, port allocation, mach_msg)
- All standard POSIX (libc with 1085 exports, libsystem_kernel with 865 exports)

## Sources

| Component | Apple repo | Tag | Already downloaded? |
|-----------|-----------|-----|-------------------|
| libc++abi | apple-oss-distributions/libcppabi | libcppabi-26 | No — download |
| libc++ | apple-oss-distributions/libcpp | libcpp-31 | No — download |
| ICU | apple-oss-distributions/ICU | ICU-76142.3.1.1 | No — download |
| objc4 | apple-oss-distributions/objc4 | (use src/objc4-906) | Yes |
| CF | apple-oss-distributions/CF | (use src/CF-1153.18) | Yes |

Download missing sources:
```bash
cd src/
git clone --depth 1 --branch libcppabi-26 https://github.com/apple-oss-distributions/libcppabi.git libcppabi-26
git clone --depth 1 --branch libcpp-31 https://github.com/apple-oss-distributions/libcpp.git libcpp-31
git clone --depth 1 --branch ICU-76142.3.1.1 https://github.com/apple-oss-distributions/ICU.git ICU-76142.3.1.1
```

## Cross-Compilation Baseline

```bash
CC="$(xcrun -find clang)"
CXX="$(xcrun -find clang++)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
COMMON_CFLAGS="-target ${TARGET} -mmacosx-version-min=14.0 -isysroot ${SDKROOT} -O2"
COMMON_LDFLAGS="-target ${TARGET} -isysroot ${SDKROOT} -lSystem"
```

**NEVER** use `-Wl,-flat_namespace` or `-Wl,-undefined,dynamic_lookup`.

---

## Step 1: libc++abi

**Source:** `src/libcppabi-26/`
**Output:** `libc++abi.dylib` → sysroot + root image at `/usr/lib/`
**Dependencies:** libSystem only

libc++abi provides the C++ ABI: `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch`, `std::type_info`, `__dynamic_cast`, guard variables for static initialization, etc.

**Build approach:** Apple's source wraps the LLVM libc++abi. Check the directory structure — there should be a `src/` subdirectory with the actual source. It may have a CMakeLists.txt or need a hand-written compile.

Key files to compile: `cxa_exception.cpp`, `cxa_exception_storage.cpp`, `cxa_demangle.cpp`, `cxa_personality.cpp`, `cxa_guard.cpp`, `cxa_handlers.cpp`, `cxa_default_handlers.cpp`, `cxa_aux_runtime.cpp`, `cxa_vector.cpp`, `cxa_virtual.cpp`, `fallback_malloc.cpp`, `abort_message.cpp`, `stdlib_*.cpp`, `private_typeinfo.cpp`.

```bash
$CXX $COMMON_CFLAGS -std=c++20 -nostdinc++ \
    -I${SDKROOT}/usr/include/c++/v1 \
    -D_LIBCPP_BUILDING_LIBRARY \
    -c src_file.cpp -o obj_file.o

# Link as dylib
$CXX $COMMON_LDFLAGS -dynamiclib \
    -install_name /usr/lib/libc++abi.dylib \
    *.o -o libc++abi.dylib
```

**Stage:** Copy to sysroot and root image at `/usr/lib/libc++abi.dylib`.

---

## Step 2: libc++

**Source:** `src/libcpp-31/`
**Output:** `libc++.1.dylib` → sysroot + root image at `/usr/lib/`
**Dependencies:** libc++abi (step 1), libSystem

libc++ is the C++ standard library: `std::string`, `std::vector`, `std::map`, `std::shared_ptr`, `<algorithm>`, `<iostream>`, `<thread>`, etc.

**Build approach:** Apple's source wraps LLVM libc++. Check the structure. Key source files are in `src/` — `string.cpp`, `vector.cpp`, `algorithm.cpp`, `iostream.cpp`, `memory.cpp`, `mutex.cpp`, `thread.cpp`, `locale.cpp`, `regex.cpp`, etc.

```bash
$CXX $COMMON_CFLAGS -std=c++20 -nostdinc++ \
    -I${SDKROOT}/usr/include/c++/v1 \
    -D_LIBCPP_BUILDING_LIBRARY \
    -c src_file.cpp -o obj_file.o

# Link
$CXX $COMMON_LDFLAGS -dynamiclib \
    -install_name /usr/lib/libc++.1.dylib \
    -compatibility_version 1.0 -current_version 1.0 \
    -L${SYSROOT}/usr/lib -lc++abi \
    *.o -o libc++.1.dylib
```

**Stage:** Copy to sysroot and root image. Create symlinks: `libc++.dylib -> libc++.1.dylib`.

---

## Step 3: ICU

**Source:** `src/ICU-76142.3.1.1/`
**Output:** `libicucore.A.dylib` → sysroot + root image at `/usr/lib/`
**Dependencies:** libc++ (step 2), libSystem

Apple ships ICU as a single `libicucore.A.dylib` that combines all ICU components (common, i18n, data). The Apple repo wraps upstream ICU with Apple-specific patches.

**Build approach:** Apple's ICU has a `makefile` at the top level. Inspect it to understand the build. The actual ICU source is in the `icu/` subdirectory. ICU has its own configure/make system (`icu/source/configure`).

For cross-compilation, ICU typically needs a two-step build:
1. Build host ICU tools first (for generating data files)
2. Cross-compile the target library using `--with-cross-build=<host-build-dir>`

```bash
# Host build (native, for icupkg and other tools)
cd icu/source
./configure --prefix=/tmp/icu-host
make -j$(sysctl -n hw.ncpu)

# Target build (cross-compile for Panthera)
mkdir build-target && cd build-target
../configure --host=$TARGET --prefix=/usr \
    --with-cross-build=../build-host \
    CC="$CC" CXX="$CXX" \
    CFLAGS="$COMMON_CFLAGS" CXXFLAGS="$COMMON_CFLAGS -std=c++17" \
    LDFLAGS="$COMMON_LDFLAGS -L${SYSROOT}/usr/lib -lc++ -lc++abi"
make -j$(sysctl -n hw.ncpu)
```

Apple combines all ICU libs into one `libicucore.A.dylib`. Check the Apple makefile for how they do this. It may use `ld -r` to merge common + i18n + data, or link all objects into one dylib.

**Stage:** Copy to sysroot and root image. Create symlinks: `libicucore.dylib -> libicucore.A.dylib`. Copy ICU headers to sysroot at `/usr/include/unicode/`.

---

## Step 4: objc4 (Objective-C Runtime)

**Source:** `src/objc4-906/`
**Output:** `libobjc.A.dylib` → sysroot + root image at `/usr/lib/`
**Dependencies:** libc++ (step 2, for std::map in 2 files), libdispatch (have it), libpthread (have it)

The ObjC runtime is 42 source files (~35K lines): 28 .mm, 2 .c, 6 .s. It provides `objc_msgSend`, class loading, method dispatch, categories, associated objects, weak references, autorelease pools.

**Build approach:** objc4 has no portable build system. Write a `build_objc4.sh` that compiles each file.

Key challenges:
- `.mm` files need ObjC compilation: use `$CC -x objective-c++`
- Assembly files (`.s`) in `Messengers.subproj/` contain the fast `objc_msgSend` implementation for x86_64
- Needs kernel headers for some Mach operations
- Uses `__attribute__((constructor))` for `_objc_init`
- Uses LLVM-derived data structures (`llvm-DenseMap.h`) — these are bundled in the runtime directory

```bash
# Compile .mm files
$CXX -x objective-c++ $COMMON_CFLAGS -std=c++17 \
    -I${SDKROOT}/usr/include/c++/v1 \
    -Iruntime -Iruntime/Messengers.subproj \
    -fobjc-arc -fno-objc-exceptions \
    -c runtime/NSObject.mm -o obj/NSObject.o

# Compile .s files (x86_64 message dispatch)
$CC $COMMON_CFLAGS -c runtime/Messengers.subproj/objc-msg-x86_64.s -o obj/objc-msg-x86_64.o

# Link
$CXX $COMMON_LDFLAGS -dynamiclib \
    -install_name /usr/lib/libobjc.A.dylib \
    -L${SYSROOT}/usr/lib -lc++ -lSystem \
    obj/*.o -o libobjc.A.dylib
```

**Stage:** Copy to sysroot and root image. Create symlinks: `libobjc.dylib -> libobjc.A.dylib`. Copy headers to sysroot at `/usr/include/objc/`.

---

## Step 5: CoreFoundation

**Source:** `src/CF-1153.18/`
**Output:** `libCoreFoundation.dylib` → sysroot + root image at `/usr/lib/`
**Dependencies:** objc4 (step 4), ICU (step 3), libdispatch (have it), libxml2 (have it), libpthread (have it)

All 77 .c files + 1 .mm file. Nothing skipped. Full locale, calendar, timezone, string, bundle (with real dlopen once available), plugin, XML, plist, run loop, socket, Mach port support.

```bash
$CC $COMMON_CFLAGS \
    -I${CF_SRC} \
    -I${SYSROOT}/usr/include \
    -I${SYSROOT}/usr/include/unicode \
    -I${SYSROOT}/usr/include/libxml2 \
    -DCF_BUILDING_CF=1 \
    -DDEPLOYMENT_TARGET_MACOSX=1 \
    -c ${CF_SRC}/CFBase.c -o obj/CFBase.o

# The .m file
$CC -x objective-c $COMMON_CFLAGS \
    [same includes/defines] \
    -c ${CF_SRC}/CFBasicHashFindBucket.m -o obj/CFBasicHashFindBucket.o

# Link
$CC $COMMON_LDFLAGS -dynamiclib \
    -install_name /usr/lib/libCoreFoundation.dylib \
    -L${SYSROOT}/usr/lib \
    -lobjc -licucore -lxml2 -ldispatch -lSystem \
    obj/*.o -o libCoreFoundation.dylib
```

**Stage:**
- Library to sysroot and root image at `/usr/lib/libCoreFoundation.dylib`
- Headers to sysroot at `/usr/include/CoreFoundation/`
- Add to shared cache dylib list in `tools/build_shared_cache_real.c`
- Create framework symlink: `/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation -> /usr/lib/libCoreFoundation.dylib`
- Add `__CFInitialize` call to dyld's init sequence in `panthera_dyld.cpp`

**Rebuild:** shared cache, root image, boot test.

---

## dyld Initialization

After CF is built, add its initializer to `panthera_run_libc_init()` in `userland/dyld/panthera_dyld.cpp` (same pattern as `__pthread_init`):

```c
{
    uint64_t addr = resolveSymbol("___CFInitialize", -1, true);
    if (addr) {
        typedef void (*Fn)(void);
        ((Fn)addr)();
    }
}
```

---

## Verification

Test program:

```c
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>

int main(void) {
    CFStringRef str = CFSTR("Hello Panthera");
    char buf[64];
    CFStringGetCString(str, buf, sizeof(buf), kCFStringEncodingUTF8);
    printf("CFString: %s\n", buf);

    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(dict, CFSTR("key"), CFSTR("value"));
    printf("CFDictionary count: %ld\n", CFDictionaryGetCount(dict));

    CFMutableArrayRef arr = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(arr, CFSTR("item1"));
    CFArrayAppendValue(arr, CFSTR("item2"));
    printf("CFArray count: %ld\n", CFArrayGetCount(arr));

    // Plist round-trip
    CFDataRef plistData = CFPropertyListCreateData(NULL, dict,
        kCFPropertyListXMLFormat_v1_0, 0, NULL);
    printf("Plist XML size: %ld bytes\n", plistData ? CFDataGetLength(plistData) : -1);

    // Run loop exists
    CFRunLoopRef rl = CFRunLoopGetCurrent();
    printf("CFRunLoop: %p\n", rl);

    // Locale
    CFLocaleRef locale = CFLocaleCopyCurrent();
    CFStringRef locId = CFLocaleGetIdentifier(locale);
    CFStringGetCString(locId, buf, sizeof(buf), kCFStringEncodingUTF8);
    printf("Locale: %s\n", buf);

    if (plistData) CFRelease(plistData);
    CFRelease(arr);
    CFRelease(dict);
    CFRelease(locale);

    printf("CoreFoundation: ALL PASS\n");
    return 0;
}
```

## Build Scripts

Create one per component:
- `userland/libcxxabi/build_libcxxabi.sh`
- `userland/libcxx/build_libcxx.sh`
- `userland/icu/build_icu.sh`
- `userland/objc4/build_objc4.sh`
- `userland/corefoundation/build_cf.sh`

Each must be idempotent, use standard cross-compile flags, and run `audit_package.sh`.

## Expected Difficulty

| Step | Files | Lines | Difficulty | Estimated rounds of fixing |
|------|-------|-------|-----------|---------------------------|
| libc++abi | ~15 | ~8K | MEDIUM — standalone, well-structured | 2-3 |
| libc++ | ~40 | ~30K | MEDIUM — large but well-structured | 3-5 |
| ICU | ~400 | ~300K | HIGH — huge, two-step cross-build | 5-8 |
| objc4 | 42 | ~35K | HIGH — ObjC/C++ mix, assembly, kernel deps | 5-8 |
| CF | 78 | ~105K | MEDIUM — once deps are built, relatively clean | 3-5 |

Total: probably 4-6 agent sessions. The order is strict — each component won't link without its dependencies.

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- Use Apple's source only — no upstream LLVM/ICU substitutions unless Apple's version is unbuildable
- Run `tools/audit_package.sh` on every output
- No `-flat_namespace`
- After sysroot changes: relink chain → shared cache → verify exports
- Do NOT modify Apple source in `src/` unless absolutely necessary for cross-compilation
- Stage headers AND libraries to sysroot so downstream builds find them
- Boot test and read output — don't ask the user
- No deferred work per step — each step must produce a working library before proceeding

## Success Criteria

- `libCoreFoundation.dylib` loads and exports all public CF symbols
- Test program passes all checks (CFString, CFDictionary, CFArray, CFPropertyList, CFRunLoop, CFLocale)
- CF is in the shared cache
- Headers at `/usr/include/CoreFoundation/` allow `#include <CoreFoundation/CoreFoundation.h>`
- libc++, libc++abi, libicucore, libobjc all in sysroot and root image
- No kernel panics, no dyld warnings
