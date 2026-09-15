# Panthera — libpanthera_extra Consolidation & Patching Infrastructure

Read `CLAUDE.md`, `FOUNDATION_TASK.md`, and `OS_BUILD_ROADMAP.md` for project context.

## Background

Panthera is a bootable Darwin OS built from Apple open-source. The foundation is working: real Apple launchd runs as PID 1, zsh runs with external commands, 31 coreutils are staged. The entire library stack (dyld, libSystem.B, libsystem_c, libsystem_kernel, libsystem_malloc, libsystem_pthread, libdispatch, libxpc, libsystem_info, libpanthera_launchd, libpanthera_extra) is built and boots in QEMU.

**libpanthera_extra.dylib** is the catch-all library for symbols that the real Apple dylibs don't provide — stubs, shims, bridge functions, and thin wrappers. It was built incrementally over many sessions. The result is **17 .o files with 373 duplicate symbol definitions across them**. The relink script (`relink_libpanthera_extra.sh`) cannot rebuild the dylib because modern `ld` rejects duplicates. The current working dylib in git (commit `791ad04`) was built ad-hoc and is not reproducible.

This is a load-bearing problem. Every future agent that needs to add a symbol to libpanthera_extra will hit the same wall. The OS roadmap (networking, SSH, package management) requires adding symbols regularly. The foundation must be rebuildable.

## Critical Rules

1. **NEVER modify libsystem_c.dylib or all_libc.a** — the build script for these is broken and cannot reproduce them. They are frozen artifacts.
2. **NEVER modify libSystem.B.dylib by hand** — use `relink_libSystem.sh` which is reproducible.
3. **Backup the root image before staging any dylib changes**: `cp images/qemu/panthera-root.img images/qemu/panthera-root.img.pre-consolidation`
4. **Run `userland/libsystem/verify_exports.sh` after any library change.**
5. **If anything breaks, restore**: `git checkout 791ad04 -- userland/libsystem/build/sysroot/`
6. **Do not remove symbols that the current working dylib exports** unless you can prove they're provided by another dylib in the re-export chain. Removing a needed symbol causes a boot-time crash.

## Current State

### The sysroot dylibs (all in `userland/libsystem/build/sysroot/usr/lib/system/`):

| Dylib | Size | Status |
|-------|------|--------|
| libsystem_kernel.dylib | 206K | Frozen, working |
| libsystem_platform.dylib | 19K | Frozen, working |
| libsystem_malloc.dylib | 246K | Frozen, working |
| libsystem_c.dylib | 494K | Frozen, NEVER TOUCH |
| libsystem_info.dylib | 138K | Frozen, working |
| libsystem_pthread.dylib | 85K | Frozen, working |
| libdispatch.dylib | 761K | Frozen, working |
| libxpc.dylib | 10K | Frozen, working |
| libpanthera_launchd.dylib | 14K | Frozen, working |
| **libpanthera_extra.dylib** | **72K** | **THIS IS WHAT YOU'RE FIXING** |
| libpanthera_patch.dylib | 13K | New, has 5 symbols (getopt, login_tty, unsetenv, uuid_generate, uuid_is_null) |
| libsystem_malloc_simple.dylib | 13K | Frozen, working |

libSystem.B.dylib (in `usr/lib/`) re-exports all of the above via `relink_libSystem.sh`. It currently re-exports 10 dylibs. **libpanthera_patch.dylib is listed in relink_libSystem.sh but libSystem.B.dylib on disk has NOT been relinked yet** — it only re-exports libpanthera_launchd and libpanthera_extra from the panthera dylibs.

### The 17 source files for libpanthera_extra:

| Source file | Lines | Exports | Purpose |
|------------|-------|---------|---------|
| dyld_stub_binder.s | small | 2 | dyld_stub_binder trampoline |
| panthera_mach_globals.c | small | 2 | Mach global state |
| panthera_boot_bridge.c | 359 | 46 | Syscall wrappers, string conversion, tty, math, iconv |
| panthera_malloc_override.c | small | 11 | Malloc zone registration |
| panthera_missing.c | ~300 | 82 | Broad grab-bag of missing functions |
| panthera_printf.c | small | 4 | printf internals |
| panthera_pthread_simple.c | ~200 | 43 | pthread stubs and bridges |
| panthera_remaining.c | 188 | 31 | Syscall wrappers, ctype, passwd stubs |
| panthera_resolve_aliases.s | ~160 | 80 | Assembly symbol aliases |
| panthera_resolve_impl.c | 1221 | 130 | LARGEST — stack protector, ctype, gdtoa, printf, stdio, locale, collation, Mach, TLV |
| panthera_resolve_wave2.c | 463 | 62 | backtrace, thread_stack_pcs, locale, env |
| panthera_resolve_wave2_asm.s | small | 6 | Assembly helpers for wave2 |
| panthera_resolve_wave3.c | 707 | 77 | syslog, user/group, networking, time, signals |
| panthera_runtime_bridge.c | 330 | 50 | compiler-rt, ctype out-of-line, blocks runtime, mach globals |
| panthera_stdio_init.c | small | 4 | stdio initialization |
| symbol_aliases.s | small | 12 | Miscellaneous symbol aliases |
| unix2003_aliases.s | small | 16 | $UNIX2003 variant aliases |

**Total: ~688 exported symbols across inputs, but only 342 unique exports in the final dylib** (due to the strip list removing 185).

**373 of the 688 are duplicates** — the same symbol defined in 2, 3, even 7 different .o files. This is why the linker rejects the build.

### verify_exports.sh current output:

- **launchd**: OK (0 unresolved)
- **zsh**: FAIL — 23 unresolved (22 are termcap/iconv, not libpanthera_extra's problem; 1 is _sigsuspend)
- **dyld**: OK (0 unresolved)
- **57 critical symbols**: All present
- **179 duplicate exports** across dylibs (46 between libpanthera_extra and libsystem_c)
- **libpanthera_patch.dylib** exists but is NOT yet re-exported by libSystem.B.dylib on disk

## Your Task

### Part 1: Audit and Consolidate Sources

**Goal:** Reduce the 17 overlapping source files to a clean, minimal set with ZERO duplicate symbols between files.

1. **Map every symbol.** For each of the 17 source files, run `nm -gU <file>.o` and build a complete map of which symbols live where. Identify every duplicate.

2. **Classify each symbol into one of these categories:**
   - **REAL**: Has a meaningful implementation (not just `return 0` or `return NULL`). These matter.
   - **STUB**: Returns 0/NULL/empty with no side effects. These are placeholders.
   - **ALIAS**: Assembly `.globl` + `jmp` or `.set` pointing to another symbol.
   - **DEAD**: Also provided by another dylib in the re-export chain (libsystem_c, libsystem_kernel, etc.) AND listed in the strip list. These should be removed entirely.

3. **Reorganize into 4 files max:**

   - **`panthera_extra_core.c`** — All REAL implementations. Things like stack protector, gdtoa shims, platform string functions, stdio init, malloc zone bridge, mach globals. These are the symbols that would crash if wrong.
   - **`panthera_extra_stubs.c`** — All STUB implementations. Grouped by subsystem (locale, collation, ASL, regex, posix_spawn, etc.) with a one-line comment per group. These are the "return 0 for now" placeholders.
   - **`panthera_extra_aliases.s`** — All assembly aliases ($UNIX2003 variants, DARWIN_EXTSN variants, base-name aliases). Consolidate from symbol_aliases.s, unix2003_aliases.s, panthera_resolve_aliases.s, and any inline aliases.
   - **`panthera_extra_bridge.c`** — All bridge/wrapper functions that call through to real implementations in other dylibs (e.g., wrappers around syscalls, thin shims over libsystem_kernel functions).

4. **Verify: the union of exports from the 4 new files must be a SUPERSET of the current 342 exported symbols.** No symbol that the current dylib exports may be lost. Check with:
   ```bash
   # Get current exports
   nm -gU sysroot/usr/lib/system/libpanthera_extra.dylib | awk '{print $NF}' | sort > /tmp/current_exports.txt
   # Get new exports (after compiling the 4 new files)
   nm -gU new_core.o new_stubs.o new_aliases.o new_bridge.o | awk '{print $NF}' | sort -u > /tmp/new_exports.txt
   # Every current export must appear in new
   comm -23 /tmp/current_exports.txt /tmp/new_exports.txt
   # ^^^ this MUST be empty
   ```

5. **Also include the 5 symbols from libpanthera_patch.dylib** (getopt, login_tty, unsetenv, uuid_generate, uuid_is_null) in the consolidated sources. Once libpanthera_extra can be rebuilt with these included, libpanthera_patch.dylib becomes unnecessary.

### Part 2: Fix the Relink Script

Update `relink_libpanthera_extra.sh` to:

1. Compile the 4 consolidated source files (with the standard cross-compilation flags):
   ```bash
   SYSROOT="userland/libsystem/build/sysroot"
   CC="xcrun -sdk macosx clang"
   CFLAGS="-target x86_64-apple-darwin23.0 -mmacosx-version-min=14.0 -isysroot $SYSROOT -nostdlib -nostdinc -I$SYSROOT/usr/include"
   ```

2. Link them into libpanthera_extra.dylib with:
   - `-unexported_symbols_list` using the strip list
   - `-undefined dynamic_lookup`
   - `-not_for_dyld_shared_cache`

3. Verify the export count matches or exceeds 342.

4. The script must be **fully self-contained** — no dependency on pre-existing .o files. Run the script from a clean checkout and it produces the dylib.

### Part 3: Update the Strip List

The current strip list (`libpanthera_extra_strip_symbols.txt`, 185 entries) is supposed to hide symbols that other dylibs provide. But verify_exports.sh shows **46 symbols still exported by libpanthera_extra that conflict with libsystem_c**.

1. Run: `nm -gU sysroot/usr/lib/system/libsystem_c.dylib | awk '{print $NF}' | sort > /tmp/libc_exports.txt`
2. Find every symbol exported by BOTH libpanthera_extra and libsystem_c.
3. Add all of them to the strip list.
4. Repeat for libsystem_info, libsystem_kernel, libsystem_pthread, libsystem_malloc.
5. Relink and verify the duplicate count drops to near zero.

### Part 4: Create `panthera_patch.sh` — The Future-Proof Patching Tool

Create `userland/libsystem/build/panthera_patch.sh` — a tool that ANY agent can use to safely add symbols to libpanthera_extra without understanding the build internals. Usage:

```bash
# Add a stub (returns 0):
./panthera_patch.sh stub _my_function

# Add a stub with a specific return value:
./panthera_patch.sh stub _my_function "return -1;"

# Add a full implementation from a C file:
./panthera_patch.sh impl /path/to/implementation.c

# Add a symbol alias:
./panthera_patch.sh alias _new_name _existing_name

# Add a $UNIX2003 alias:
./panthera_patch.sh unix2003 _function_name

# Show what the tool would do without doing it:
./panthera_patch.sh --dry-run stub _my_function

# Verify after patching:
./panthera_patch.sh verify
```

The tool must:

1. **Determine where to add the symbol** — stubs go in `panthera_extra_stubs.c`, aliases go in `panthera_extra_aliases.s`, implementations go in `panthera_extra_bridge.c` (or a specified file).

2. **Check for conflicts** — before adding, verify the symbol doesn't already exist in ANY sysroot dylib:
   ```bash
   for dylib in sysroot/usr/lib/system/*.dylib sysroot/usr/lib/*.dylib; do
       nm -gU "$dylib" 2>/dev/null | grep " $SYMBOL$"
   done
   ```
   If found, warn and ask for confirmation. If it's in the strip list, abort (adding a stripped symbol is pointless).

3. **Add the symbol** to the appropriate source file.

4. **Recompile and relink** by calling `relink_libpanthera_extra.sh`.

5. **Run `verify_exports.sh`** and report results.

6. **Print a summary**: "Added _my_function to panthera_extra_stubs.c. libpanthera_extra.dylib now exports N symbols. verify_exports: PASS/FAIL."

The tool should also support batch mode:
```bash
# Add multiple stubs at once:
./panthera_patch.sh batch <<'EOF'
stub _func1
stub _func2 "return -1;"
alias _func3 _func1
unix2003 _func4
EOF
```

### Part 5: Relink libSystem.B.dylib and Verify

After consolidation:

1. If libpanthera_patch.dylib's symbols are now in libpanthera_extra, remove it from `relink_libSystem.sh`'s required list and re-export list. Otherwise keep it.

2. Run `relink_libSystem.sh` to rebuild libSystem.B.dylib.

3. Run `verify_exports.sh`. Expected result:
   - launchd: OK
   - zsh: FAIL with ~22 termcap/iconv symbols (known, not your problem)
   - dyld: OK
   - 57 critical symbols: all present
   - Duplicate exports: **under 20** (down from 179)

### Part 6: Stage and Boot Test

1. Backup: `cp images/qemu/panthera-root.img images/qemu/panthera-root.img.pre-consolidation`

2. Mount and stage:
   ```bash
   hdiutil attach images/qemu/panthera-root.img -mountpoint /Volumes/PantheraRoot
   cp userland/libsystem/build/sysroot/usr/lib/system/libpanthera_extra.dylib /Volumes/PantheraRoot/usr/lib/system/
   cp userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib /Volumes/PantheraRoot/usr/lib/
   # If libpanthera_patch.dylib was removed from the chain, also remove from root image
   # If kept, stage it too
   hdiutil detach /Volumes/PantheraRoot
   ```

3. Boot test in QEMU. Expected: launchd starts, zsh prompt appears, external commands work:
   ```
   /bin/echo CONSOLIDATION_OK
   /bin/ls /bin
   /bin/cat /etc/passwd
   ```

4. If boot fails: restore from backup, diff the new dylib exports against the old, find what's missing.

### Part 7: Commit

Once boot-tested:
```bash
git add userland/libsystem/build/obj/panthera_extra_core.c
git add userland/libsystem/build/obj/panthera_extra_stubs.c
git add userland/libsystem/build/obj/panthera_extra_aliases.s
git add userland/libsystem/build/obj/panthera_extra_bridge.c
git add userland/libsystem/build/relink_libpanthera_extra.sh
git add userland/libsystem/build/libpanthera_extra_strip_symbols.txt
git add userland/libsystem/build/panthera_patch.sh
git add userland/libsystem/build/sysroot/usr/lib/system/libpanthera_extra.dylib
git add userland/libsystem/build/sysroot/usr/lib/libSystem.B.dylib
```

Do NOT delete the old .o files or old .c sources yet — they can be cleaned up in a follow-up.

## Success Criteria

- [ ] `relink_libpanthera_extra.sh` runs clean and produces a working dylib from source
- [ ] Zero duplicate symbols between the consolidated source files
- [ ] All 342+ current exports preserved (plus the 5 from libpanthera_patch)
- [ ] Duplicate exports between dylibs reduced from 179 to under 20
- [ ] `panthera_patch.sh` works for stub, impl, alias, and unix2003 modes
- [ ] `verify_exports.sh` passes (except known zsh termcap/iconv gap)
- [ ] System boots in QEMU with launchd PID 1 and zsh prompt
- [ ] External commands work: echo, ls, cat at minimum
- [ ] All changes committed to git

## What NOT To Do

- Do not modify libsystem_c.dylib, all_libc.a, or any other frozen dylib
- Do not add features beyond what's specified — no debug traces, no extra logging
- Do not attempt to fix the zsh termcap/iconv unresolved symbols (that's a separate task)
- Do not attempt to fix signal delivery or fork() (separate tasks)
- Do not reorganize the directory structure or move files around
- Do not introduce any new dependencies — the cross-compilation must work with just Xcode clang
