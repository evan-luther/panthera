# Panthera — Package Porting Guide

Read `OS_BUILD_ROADMAP.md` for foundation rules before doing anything.

This guide is the mandatory process for cross-compiling and installing any new package on Panthera. **Follow every step in order.** Skipping the pre-flight audit is what turns a 30-minute port into a 6-hour debugging session.

## The Golden Rule

**Never boot-test a binary you haven't audited.** Runtime crashes from missing or stubbed symbols are the #1 time sink on this project. Find them at the desk, not in QEMU.

---

## Phase 1: Pre-Flight Symbol Audit

Before writing a single line of build script, check whether the package's symbols are satisfiable.

### Step 1: Cross-compile the package

Use the standard Panthera cross-compilation pattern:

```bash
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
SYSROOT="/Users/admin/panthera/userland/libsystem/build/sysroot"
TARGET="x86_64-apple-darwin23.0"

# For configure-based projects:
CC="$CC" \
CFLAGS="-target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2" \
CPPFLAGS="-target $TARGET -isysroot $SDKROOT" \
LDFLAGS="-target $TARGET -isysroot $SDKROOT -lSystem" \
./configure --host=$TARGET --prefix=/usr [other flags]

make -j$(sysctl -n hw.ncpu)
```

**Critical LDFLAGS rules:**
- **NEVER use `-Wl,-flat_namespace`** — causes shared cache binding failures at runtime
- **NEVER use `-Wl,-undefined,dynamic_lookup`** — same problem
- Always link against `-lSystem` (Panthera's libSystem.B.dylib umbrella)
- If the package needs ncurses: add `-L$SYSROOT/usr/lib -lncurses.5.4`
- If the package needs OpenSSL: add `-L/Users/admin/panthera/userland/openssl/lib -lssl -lcrypto`
- If the package needs zlib: add `-L/Users/admin/panthera/userland/zlib/stage/usr/lib -lz`

### Step 2: Extract undefined symbols

```bash
nm -mu path/to/binary | grep '(undefined)' | awk '{print $NF}' | sort -u > /tmp/pkg_undef.txt
```

For dylibs the package produces:
```bash
nm -mu path/to/lib.dylib | grep '(undefined)' | awk '{print $NF}' | sort -u >> /tmp/pkg_undef.txt
sort -u -o /tmp/pkg_undef.txt /tmp/pkg_undef.txt
```

### Step 3: Check every symbol against the sysroot

```bash
nm -gU userland/libsystem/build/sysroot/usr/lib/system/*.dylib \
       userland/libsystem/build/sysroot/usr/lib/*.dylib 2>/dev/null \
    | awk '{print $NF}' | sort -u > /tmp/sysroot_exports.txt

comm -23 /tmp/pkg_undef.txt /tmp/sysroot_exports.txt > /tmp/missing.txt
cat /tmp/missing.txt
```

If `/tmp/missing.txt` is empty, proceed to Phase 2. If not, fix every missing symbol BEFORE building the root image.

### Step 4: Verify namespace

Every binary and dylib must use **two-level namespace** (symbols bound to specific libraries):

```bash
nm -m path/to/binary | grep '_malloc$'
# GOOD: (undefined) external _malloc (from libSystem)
# BAD:  (undefined) external _malloc              ← flat namespace, will break
```

If you see flat namespace, find the `-flat_namespace` or `-undefined dynamic_lookup` flag in your build and remove it.

### Step 5: Check for dangerous stubs

Some symbols ARE exported but are **stubs that return wrong values**. Check the following known-dangerous patterns:

```bash
# Check if the package uses these — if so, verify they work:
grep -c 'getaddrinfo\|getnameinfo' /tmp/pkg_undef.txt    # DNS — standalone impl in libpanthera_extra
grep -c 'pthread_' /tmp/pkg_undef.txt                      # Threading — limited (frozen dylib)
grep -c 'dlopen\|dlsym\|dlclose' /tmp/pkg_undef.txt       # Dynamic loading — stubbed
grep -c 'iconv_open\|iconv_close' /tmp/pkg_undef.txt      # iconv — real impl available
grep -c 'shm_open\|sem_open\|mq_open' /tmp/pkg_undef.txt  # POSIX IPC — likely stubbed
grep -c 'setlocale\|locale' /tmp/pkg_undef.txt             # Locale — minimal
```

If the package uses `pthread_create` heavily, it may not work — Panthera's pthread is a frozen binary with limited functionality. If it uses `dlopen`, it will get NULL (dynamic loading is stubbed). Plan accordingly: disable optional features that need these.

---

## Phase 2: Fix Missing Symbols

For each symbol in `/tmp/missing.txt`:

### Decision tree

1. **Is it a $UNIX2003 or $DARWIN_EXTSN variant?** (e.g., `_open$UNIX2003`)
   ```bash
   bash userland/libsystem/build/panthera_patch.sh unix2003 _open
   ```

2. **Is it a simple function with an obvious implementation?** (e.g., `_inet_ntop`, `_if_nametoindex`)
   - Write a real implementation in a .c file
   - Add via: `bash userland/libsystem/build/panthera_patch.sh impl path/to/impl.c`

3. **Is it a function that can safely return 0/NULL/error?** (e.g., `_posix_spawnattr_setflags`)
   ```bash
   bash userland/libsystem/build/panthera_patch.sh stub _posix_spawnattr_setflags
   ```
   **WARNING:** Only stub functions where returning 0 is semantically correct (success or no-op). Never stub functions where 0 means "false" when it should mean "true", or where NULL causes a dereference. When in doubt, implement it properly.

4. **Are there 3+ missing symbols from the same subsystem?** (e.g., multiple `pthread_*` or `locale_*`)
   - Check if the subsystem has Apple source in `src/`
   - Build from source rather than stubbing individually

### After adding symbols

**Every time, without exception:**

```bash
bash userland/libsystem/build/relink_libpanthera_extra.sh
bash userland/libsystem/build/relink_libSystem.sh
bash tools/build_shared_cache.sh
bash userland/libsystem/verify_exports.sh
```

If `verify_exports.sh` fails, fix the issue before proceeding.

### Re-run the symbol audit

After adding symbols, repeat Phase 1 Steps 2-3 to confirm zero missing symbols.

---

## Phase 3: Stage and Build Root Image

### Create a build script

Every package must have a build script at `userland/<package>/build_<package>.sh`. Follow the pattern in existing scripts (`userland/curl/build_curl.sh`, `userland/nano/build_nano.sh`).

The script must:
- Be idempotent (skip rebuild if output exists, unless `PANTHERA_FORCE_REBUILD=1`)
- Use the standard cross-compilation flags (no flat_namespace!)
- Place output binaries in `userland/<package>/bin/`
- Place output libraries in `userland/<package>/lib/`
- Run symbol verification on all outputs

### Update root image staging

Edit `rootfs/create_hfs_root_image.sh` to:
- Copy binaries to the correct guest path (`/usr/bin/`, `/usr/sbin/`, `/usr/lib/`)
- Copy any required config files
- Fix install names on dylibs if needed: `install_name_tool -id /usr/lib/libfoo.dylib path/to/libfoo.dylib`
- Create any required directories

### If the package is a daemon

Create a launchd plist in `rootfs/System/Library/LaunchDaemons/com.panthera.<name>.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.panthera.<name></string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/sbin/<name></string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/var/log/<name>.log</string>
    <key>StandardErrorPath</key>
    <string>/var/log/<name>.log</string>
</dict>
</plist>
```

---

## Phase 4: Boot Test

```bash
# Always backup first
cp images/qemu/panthera-root.img images/qemu/panthera-root.img.pre-<package>

# Build the image
bash rootfs/create_hfs_root_image.sh --force

# Boot
PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot --ssh-port 0 \
    --root-disk images/qemu/panthera-root.img
```

Read the boot output as text. Check for:
- `dyld: unresolved bind` warnings — means a symbol is missing from the shared cache
- `dyld: Symbol not found` — hard failure, binary won't load
- Crash/segfault — likely a stub returning a wrong value
- Kernel panic — likely an uninitialized kernel subsystem (see Appendix B)

---

## Phase 5: Verification Checklist

Before declaring a package done, verify ALL of the following:

- [ ] Binary loads without any `dyld:` warnings
- [ ] Binary runs and produces expected output for basic operations
- [ ] No kernel panics triggered by the package
- [ ] Build script is idempotent and committed
- [ ] Root image staging is in `create_hfs_root_image.sh`
- [ ] `verify_exports.sh` passes
- [ ] No `-flat_namespace` or `-undefined dynamic_lookup` on any output binary/dylib
- [ ] All missing symbols were resolved with real implementations or correct error returns (not blind stubs)

---

## Appendix A: Known Stub Limitations

These areas of the sysroot have limited or stubbed implementations. If your package depends heavily on any of these, it may require significant work:

| Area | Status | Impact |
|------|--------|--------|
| `pthread_create` / threading | Frozen dylib, basic support | Packages needing threads may hang or crash |
| `dlopen` / `dlsym` / `dlclose` | Stubbed (returns NULL/error) | No dynamic plugin loading |
| `locale_t` / `newlocale` | Minimal | Locale-dependent formatting may be wrong |
| `shm_open` / POSIX shared memory | Likely stubbed | IPC via shared memory won't work |
| `sem_open` / POSIX semaphores | Likely stubbed | Named semaphores won't work |
| `fork` + `exec` | Working | But `posix_spawn` attributes are partially stubbed |
| `getaddrinfo` | Standalone UDP DNS resolver | Works for A records; no AAAA, no SRV, no mDNS |
| `openssl` | 3.5.5, built from source | Needs CA certs at `/etc/ssl/cert.pem` |
| `curses` / `ncurses` | Built from source (5.4 ABI) | Working, terminfo at `/usr/share/terminfo/` |
| `iconv` | Built from source | Working |
| `zlib` | Built from source | Working |

## Appendix B: Known Kernel Landmines

XNU has macOS subsystems compiled in that Panthera doesn't initialize. If your package triggers one, you'll get a kernel panic. Known ones:

| Subsystem | Trigger | Fix Applied? |
|-----------|---------|-------------|
| Content Filter (cfil) | Any TCP `connect()` | **YES** — early return in `cfil_sock_attach()` |
| NECP | Network policy evaluation | Partially guarded by cfil fix |
| Sandbox | `sandbox_check()` calls | Not triggered (no sandbox profiles loaded) |
| MACF | Mandatory access control hooks | Not triggered (no policies loaded) |

If you hit a new kernel panic, capture the full backtrace. The crash address can be mapped to a function via:
```bash
atos -o build/obj/RELEASE_X86_64/kernel.unstripped -l 0xffffff8000200000 <crash_address>
```
(The load address 0xffffff8000200000 is the kernel's __TEXT vmaddr.)

## Appendix C: Quick Reference — Common Configure Flags

Disable features that need unsupported infrastructure:

```bash
# Threading
--disable-threads --without-threads

# Dynamic loading
--disable-shared --without-dlopen

# Locale
--disable-nls --without-gettext

# Networking (if not needed)
--disable-ipv6

# Documentation (saves build time)
--disable-docs --without-docs

# Platform-specific
--disable-sandbox --disable-seccomp
--without-systemd --without-pam
```

## Appendix D: Example Pre-Flight Audit Script

Save as `tools/audit_package.sh`:

```bash
#!/bin/bash
# Usage: bash tools/audit_package.sh path/to/binary [path/to/lib.dylib ...]
set -euo pipefail

SYSROOT="$(cd "$(dirname "$0")/../userland/libsystem/build/sysroot" && pwd)"

# Collect all undefined symbols from all inputs
undef=$(mktemp)
exports=$(mktemp)
trap 'rm -f "$undef" "$exports"' EXIT

for bin in "$@"; do
    nm -mu "$bin" 2>/dev/null | grep '(undefined)' | awk '{print $NF}'
done | sort -u > "$undef"

# Collect all sysroot exports
nm -gU "$SYSROOT"/usr/lib/system/*.dylib "$SYSROOT"/usr/lib/*.dylib 2>/dev/null \
    | awk '{print $NF}' | sort -u > "$exports"

missing=$(comm -23 "$undef" "$exports")

# Check namespace
flat=""
for bin in "$@"; do
    if nm -m "$bin" 2>/dev/null | grep -q '(undefined) external .* $' 2>/dev/null; then
        # Check for symbols without "(from ...)" qualifier
        bad=$(nm -m "$bin" 2>/dev/null | grep '(undefined) external' | grep -v '(from ' | head -5)
        if [ -n "$bad" ]; then
            flat="$flat\n$bin:\n$bad"
        fi
    fi
done

echo "=== Symbol Audit ==="
echo "Inputs: $*"
echo ""

if [ -z "$missing" ]; then
    echo "PASS: All symbols resolve in sysroot"
else
    echo "FAIL: Missing symbols:"
    echo "$missing"
    echo ""
    echo "Fix each with:"
    echo "  bash userland/libsystem/build/panthera_patch.sh stub <symbol>"
    echo "  bash userland/libsystem/build/panthera_patch.sh impl <file.c>"
    echo "Then: relink_libpanthera_extra.sh → relink_libSystem.sh → build_shared_cache.sh → verify_exports.sh"
fi

echo ""
if [ -n "$flat" ]; then
    echo "WARNING: Flat namespace detected (will break at runtime):"
    echo -e "$flat"
    echo ""
    echo "Remove -Wl,-flat_namespace from your build LDFLAGS"
else
    echo "PASS: All binaries use two-level namespace"
fi
```
