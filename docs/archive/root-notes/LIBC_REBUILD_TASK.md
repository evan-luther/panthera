# Panthera — Rebuild libsystem_c.dylib From Source

Read `OS_BUILD_ROADMAP.md` for foundation rules. This task is about making `build_libsystem_c.sh` produce a complete, trace-free libsystem_c.dylib.

## Why This Matters

The current libsystem_c.dylib is a frozen artifact with debug traces baked in (`libc getenv: enter`, `libc signal: before sigaction`, etc.) that print on every libc call. It also has gaps that require ~230 stub/shim symbols in libpanthera_extra.dylib. Rebuilding from source solves both problems:

1. Clean dylib with no debug traces
2. Most of libpanthera_extra becomes unnecessary

## Current State

`build_libsystem_c.sh` already works partially:
- **403 of 440 source files compile successfully**
- **37 files fail** — these contain the most important functions
- The output has **1028 exports** (same count as the frozen dylib, but different symbols)
- Source tree: `src/Libc-1583.40.7/`

## The 37 Failed Files

These are the files that fail to compile. Each needs its error diagnosed and fixed (usually a missing header, missing internal type, or missing shim):

### Priority 1 — stdio (printf/scanf family)
These are the most impactful. The current frozen dylib's printf has debug traces.

| File | Likely issue |
|------|-------------|
| `stdio/FreeBSD/vfprintf.c` | Missing internal headers (local.h, floatio.h, printfcommon.h) or types |
| `stdio/FreeBSD/vfscanf.c` | Same class of issue |
| `stdio/FreeBSD/vfwprintf.c` | Wide-char printf, depends on locale internals |
| `stdio/FreeBSD/vfwscanf.c` | Wide-char scanf |
| `stdio/FreeBSD/snprintf.c` | Thin wrapper over vfprintf — should be easy |
| `stdio/FreeBSD/sprintf.c` | Same |
| `stdio/FreeBSD/vasprintf.c` | Same |
| `stdio/FreeBSD/vprintf.c` | Same |
| `stdio/FreeBSD/vscanf.c` | Same |
| `stdio/FreeBSD/vsnprintf.c` | Same |
| `stdio/FreeBSD/vsprintf.c` | Same |
| `stdio/FreeBSD/vsscanf.c` | Same |
| `stdio/FreeBSD/vswprintf.c` | Same |

### Priority 2 — locale/collation
These are why libpanthera_extra has ~40 locale stubs.

| File | Likely issue |
|------|-------------|
| `locale/FreeBSD/collate.c` | Missing collate.h internals or FreeBSD-specific types |
| `locale/FreeBSD/runetype.c` | Rune type tables |
| `locale/FreeBSD/setlocale.c` | Depends on internal locale structs |
| `locale/FreeBSD/tolower.c` | Locale-aware case conversion |
| `locale/FreeBSD/toupper.c` | Same |

### Priority 3 — stdlib/gen
| File | Likely issue |
|------|-------------|
| `stdlib/FreeBSD/atexit.c` | Missing atexit.h or internal lock types |
| `stdlib/FreeBSD/getenv.c` | This is where the debug traces live in the frozen dylib |
| `gen/FreeBSD/closedir.c` | Missing `__dirent.h` or internal dir structures |
| `gen/FreeBSD/opendir.c` | Same |
| `gen/FreeBSD/readdir.c` | Same |
| `gen/FreeBSD/rewinddir.c` | Same |
| `gen/FreeBSD/seekdir.c` | Same |
| `gen/FreeBSD/telldir.c` | Missing telldir.h |
| `gen/FreeBSD/popen.c` | Missing internal types |
| `gen/FreeBSD/timezone.c` | Missing tzfile.h or time internals |
| `gen/fts.c` | Missing fts internal types |
| `gen/backtrace.c` | Missing execinfo.h internals |
| `gen/clock_gettime.c` | Missing internal time types |
| `gen/thread_stack_pcs.c` | Missing internal thread types |

### Priority 4 — string/secure
| File | Likely issue |
|------|-------------|
| `string/stpcpy.c` | Likely needs platform string function defines |
| `string/stpncpy.c` | Same |
| `string/strcat.c` | Same |
| `secure/chk_fail.c` | Missing CrashReporter internals |
| `stdtime/FreeBSD/localtime.c` | Missing tzfile.h or internal time zone structs |

## How to Fix Each File

For each failed file:

1. **Get the actual error:**
   ```bash
   # The build script suppresses errors (2>/dev/null). Run the compile command manually:
   SRCDIR="/Users/admin/panthera/src/Libc-1583.40.7"
   SHIMDIR="/Users/admin/panthera/userland/libsystem/build/obj/libsystem_c/shims"
   SDK_PATH="$(xcrun -sdk macosx -show-sdk-path)"
   SYSROOT="/Users/admin/panthera/userland/libsystem/build/sysroot"

   clang -target x86_64-apple-darwin23.0 -c -fPIC -O2 \
       -I"$SHIMDIR" \
       -I"$SRCDIR/include" -I"$SRCDIR/gen" -I"$SRCDIR/gen/FreeBSD" \
       -I"$SRCDIR/stdio" -I"$SRCDIR/stdio/FreeBSD" \
       -I"$SRCDIR/stdlib" -I"$SRCDIR/stdlib/FreeBSD" \
       -I"$SRCDIR/locale" -I"$SRCDIR/locale/FreeBSD" \
       -I"$SRCDIR/fbsdcompat" -I"$SRCDIR/nls" -I"$SRCDIR/darwin" \
       -I"$SRCDIR/string" -I"$SRCDIR/string/FreeBSD" \
       -I"$SRCDIR/stdtime" -I"$SRCDIR/stdtime/FreeBSD" \
       -I"$SRCDIR/secure" -I"$SRCDIR/os" -I"$SRCDIR/collections" \
       -I"$SRCDIR/emulated" -I"$SRCDIR/gdtoa" -I"$SRCDIR/gdtoa/FreeBSD" \
       -I"$SRCDIR/sys" \
       -I"$SYSROOT/usr/include" \
       -I"/Users/admin/panthera/src/xnu-10002.41.9/osfmk" \
       -I"/Users/admin/panthera/src/xnu-10002.41.9/EXTERNAL_HEADERS" \
       -I"/Users/admin/panthera/src/xnu-10002.41.9/bsd" \
       -I"/Users/admin/panthera/src/xnu-10002.41.9/libkern" \
       -I"/Users/admin/panthera/src/libplatform-306.0.1/include" \
       -I"/Users/admin/panthera/src/libplatform-306.0.1/private" \
       -I"/Users/admin/panthera/src/libpthread-519/include" \
       -I"/Users/admin/panthera/src/libpthread-519/private" \
       -I"$SDK_PATH/usr/include" \
       -DPRIVATE -D__DARWIN_UNIX03=1 -D__DARWIN_64_BIT_INO_T=1 \
       -DBUILDING_LIBC=1 -DPLATFORM_MacOSX=1 -D_LIBC_NO_FEATURE_VERIFICATION=1 \
       -w \
       "$SRCDIR/<path/to/file.c>" -o /tmp/test.o 2>&1
   ```

2. **Diagnose the error.** It will be one of:
   - **Missing header**: Create a shim in `$SHIMDIR/` or add an `-I` path
   - **Missing type/struct**: Add a typedef to an existing shim header
   - **Missing function declaration**: Add an `extern` declaration or shim
   - **Incompatible Apple internal API**: Write a thin compat wrapper

3. **Fix it.** Add the minimal shim/header/typedef. Do NOT modify Apple source files.

4. **Verify it compiles**, then move to the next file.

## Approach

Work in priority order. Fix all Priority 1 (stdio) files first, then rerun the build script and check the export count. The stdio files are mostly thin wrappers around vfprintf — fix vfprintf.c and the rest will likely follow.

**Batch by error class.** If 5 files fail because of the same missing header, create that one shim and they all pass.

### After all 37 files compile:

1. Run `build_libsystem_c.sh` and confirm 440/440 compile, 0 failures.

2. Compare exports:
   ```bash
   # Get frozen dylib exports
   nm -gU /tmp/frozen_libsystem_c.dylib | awk '{print $NF}' | sort > /tmp/frozen_exports.txt
   # Get new dylib exports
   nm -gU $OUTDIR/libsystem_c.dylib | awk '{print $NF}' | sort > /tmp/new_exports.txt
   # Find symbols in frozen but not new
   comm -23 /tmp/frozen_exports.txt /tmp/new_exports.txt > /tmp/missing_from_new.txt
   # Find symbols in new but not frozen
   comm -13 /tmp/frozen_exports.txt /tmp/new_exports.txt > /tmp/gained_in_new.txt
   ```

3. Any symbols in `missing_from_new.txt` that are imported by launchd, zsh, dyld, or coreutils MUST be added. These may need additional source files (x86_64 assembly, additional .c files not in the current file list, or explicit symbol aliases).

4. Verify the new dylib has NO debug trace strings:
   ```bash
   strings $OUTDIR/libsystem_c.dylib | grep -iE "libc (getenv|signal|setlocale|getpw)"
   # Must be empty
   ```

5. **DO NOT overwrite the frozen dylib yet.** Build to a temporary path first:
   ```bash
   # Modify build script to output to a different path:
   OUTDIR="/Users/admin/panthera/userland/libsystem/build/obj/libsystem_c_new"
   ```

6. Once the new dylib has equal or more exports and no traces, THEN:
   - Back up the frozen dylib
   - Replace it
   - Relink libSystem.B.dylib
   - Run verify_exports.sh
   - Update the strip list (many libpanthera_extra symbols can now be stripped since libc provides them)
   - Boot test

## What This Eliminates From libpanthera_extra

Once libc is rebuilt cleanly, these entire categories of stubs become unnecessary:

- All `$UNIX2003` aliases for libc functions (libc should export them natively)
- All locale/collation stubs (~40 symbols)
- All gdtoa stubs (~20 symbols)
- All printf/stdio internal stubs (~15 symbols)
- All directory function stubs (opendir, readdir, etc.)
- getenv, setenv, unsetenv, atexit
- tolower, toupper, and locale-aware ctype
- The debug traces disappear automatically

Estimated reduction: libpanthera_extra drops from ~280 exports to ~50-80 (the non-libc stuff: mach globals, malloc bridges, pthread shims, libdispatch stubs).

## What NOT to Do

- Do NOT modify Apple source files in `src/Libc-1583.40.7/`. All fixes go in shim headers.
- Do NOT overwrite the frozen libsystem_c.dylib until the new one is fully verified.
- Do NOT touch libpanthera_extra during this task. The strip list update happens AFTER the new libc is proven.
- Do NOT add debug traces to the new build.
- Do NOT change the build script's compilation flags for the 403 files that already compile — only fix the 37 that fail.

## Success Criteria

- [ ] 440/440 source files compile
- [ ] New libsystem_c.dylib has >= 1028 exports
- [ ] No debug trace strings in the binary
- [ ] All symbols imported by launchd, zsh, dyld, and coreutils are present
- [ ] Boot test passes (launchd → zsh prompt → external commands work)
- [ ] verify_exports.sh passes
- [ ] build_libsystem_c.sh is fully reproducible (run it twice, get the same result)
