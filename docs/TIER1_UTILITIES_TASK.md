# Tier 1 — Essential Utilities for Panthera

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules. Read `docs/PACKAGE_PORTING_GUIDE.md` for the mandatory symbol audit process.

Panthera is a bootable Darwin OS with 69+ coreutils but is missing several critical utilities for daily use. This task cross-compiles and stages them from Apple's open-source repos.

## Source Locations

All sources are at https://github.com/apple-oss-distributions. Download each into `src/`:

```bash
cd src/
# Only download what's not already there
for repo in awk bash less vim adv_cmds system_cmds basic_cmds misc_cmds bzip2 screen lsof top; do
    if [ ! -d "$repo" ] && [ ! -d "${repo}-"* ]; then
        gh release download --repo "apple-oss-distributions/${repo}" --pattern '*.tar.gz' --dir . 2>/dev/null || \
        git clone --depth 1 "https://github.com/apple-oss-distributions/${repo}.git" 2>/dev/null
    fi
done
```

Note: Some repos may not have releases. Use `git clone` as fallback. Check what version is available — use the latest tag.

## Cross-Compilation Pattern

Every utility follows the same pattern:

```bash
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
CFLAGS="-target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2"
LDFLAGS="-target $TARGET -isysroot $SDKROOT -lSystem"
```

**CRITICAL RULES:**
- **NEVER use `-Wl,-flat_namespace`**
- **NEVER use `-Wl,-undefined,dynamic_lookup`**
- Always run `bash tools/audit_package.sh <binary>` before staging
- If the audit shows missing symbols, fix via `panthera_patch.sh` before proceeding

## Utilities to Build

### 1. awk (CRITICAL)

**Source:** `apple-oss-distributions/awk` (One True Awk)
**Install to:** `/usr/bin/awk`
**Dependencies:** libSystem only
**Notes:** Single-file build. Apple's awk is Brian Kernighan's one-true-awk. Very simple compile.

```bash
$CC $CFLAGS -o awk awkgram.tab.c b.c lex.c lib.c main.c parse.c proctab.c run.c tran.c $LDFLAGS
```

If Apple's awk source has yacc-generated files, they should be pre-generated. If not, run yacc on the host first.

### 2. less (CRITICAL)

**Source:** `apple-oss-distributions/less`
**Install to:** `/usr/bin/less`, plus symlink `/usr/bin/more -> less`
**Dependencies:** libSystem, libncurses (have it)
**Notes:** Standard configure/make. Add `-L$SYSROOT/usr/lib -lncurses.5.4` to LDFLAGS.

### 3. bash

**Source:** `apple-oss-distributions/bash`
**Install to:** `/bin/bash`, add to `/etc/shells`
**Dependencies:** libSystem, libncurses (for readline)
**Notes:** Configure with `--without-bash-malloc --disable-nls`. Many scripts have `#!/bin/bash` shebangs.

### 4. vim

**Source:** `apple-oss-distributions/vim` or `apple-oss-distributions/vi`
**Install to:** `/usr/bin/vim`, symlink `/usr/bin/vi -> vim`
**Dependencies:** libSystem, libncurses
**Notes:** Configure with `--with-features=normal --disable-gui --without-x --disable-nls`. If Apple's vim is too complex, use `vi` (nvi) which is simpler.

### 5. system_cmds (CRITICAL)

**Source:** `apple-oss-distributions/system_cmds`
**Install to:** Various (`/bin/`, `/usr/bin/`, `/sbin/`, `/usr/sbin/`)
**Dependencies:** libSystem, possibly IOKit headers for some tools

This repo contains many essential utilities. Build what compiles cleanly:

| Command | Path | Priority |
|---------|------|----------|
| ps | /bin/ps | HIGH — process listing |
| sysctl | /usr/sbin/sysctl | HIGH — kernel parameters |
| mount / umount | /sbin/mount, /sbin/umount | HIGH — filesystem management |
| reboot / halt / shutdown | /sbin/reboot, /sbin/halt | HIGH — clean shutdown |
| dmesg | /sbin/dmesg | HIGH — kernel messages |
| w | /usr/bin/w | MEDIUM — who's logged in |
| passwd | /usr/bin/passwd | MEDIUM — change password |
| su | /usr/bin/su | MEDIUM — switch user |
| login | /usr/bin/login | Already have custom login |
| chpass | /usr/bin/chpass | LOW |
| nologin | /sbin/nologin | LOW |

**Not all will compile.** Some need IOKit, DirectoryService, or other missing frameworks. Build what you can, skip what fails. Don't spend more than 15 minutes on any single tool that has complex dependencies.

### 6. bzip2

**Source:** `apple-oss-distributions/bzip2`
**Install to:** `/usr/bin/bzip2`, `/usr/bin/bunzip2`, `/usr/bin/bzcat`
**Dependencies:** libSystem only
**Notes:** Trivial build — no configure, just make with CC/CFLAGS overrides.

### 7. screen

**Source:** `apple-oss-distributions/screen`
**Install to:** `/usr/bin/screen`
**Dependencies:** libSystem, libncurses
**Notes:** Terminal multiplexer. Configure with `--disable-pam --disable-socket-dir`. Incredibly useful for headless server work — persistent sessions survive SSH disconnects.

### 8. lsof

**Source:** `apple-oss-distributions/lsof`
**Install to:** `/usr/sbin/lsof`
**Dependencies:** libSystem
**Notes:** May need header stubs for some kernel data structures. Build what compiles.

### 9. top

**Source:** `apple-oss-distributions/top`
**Install to:** `/usr/bin/top`
**Dependencies:** libSystem, libncurses, possibly IOKit/libproc
**Notes:** May be complex due to Mach task inspection APIs. Try it — if it needs too many stubs, defer.

## Build Script Structure

Create one build script per package at `userland/<package>/build_<package>.sh`. Follow the pattern in existing scripts (`userland/curl/build_curl.sh`).

Each script must:
1. Be idempotent (skip if output exists unless PANTHERA_FORCE_REBUILD=1)
2. Use the standard cross-compilation flags
3. Run `tools/audit_package.sh` on all outputs
4. Place binaries in `userland/<package>/bin/`

## Staging

Update `rootfs/create_hfs_root_image.sh` to copy each binary to the correct guest path. Add all new binaries in a dedicated section:

```bash
# === Tier 1 utilities ===
for tool_entry in \
    "userland/awk/bin/awk:/usr/bin/awk" \
    "userland/less/bin/less:/usr/bin/less" \
    "userland/bash/bin/bash:/bin/bash" \
    "userland/vim/bin/vim:/usr/bin/vim" \
    "userland/bzip2/bin/bzip2:/usr/bin/bzip2" \
    "userland/screen/bin/screen:/usr/bin/screen" \
    "userland/lsof/bin/lsof:/usr/sbin/lsof"; do
    src="${PANTHERA_ROOT}/$(echo "$tool_entry" | cut -d: -f1)"
    dst="${mounted_volume}$(echo "$tool_entry" | cut -d: -f2)"
    if [[ -f "$src" ]]; then
        cp "$src" "$dst"
    fi
done

# Symlinks
ln -sf less "${mounted_volume}/usr/bin/more"
ln -sf vim "${mounted_volume}/usr/bin/vi"
ln -sf bzip2 "${mounted_volume}/usr/bin/bunzip2"
ln -sf bzip2 "${mounted_volume}/usr/bin/bzcat"
```

Add `/bin/bash` to `rootfs/etc/shells`.

## Boot Test

After staging all binaries:

```bash
bash rootfs/create_hfs_root_image.sh --force
PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot --ssh-port 0 \
    --root-disk images/qemu/panthera-root.img
```

Verify each utility:
```bash
awk 'BEGIN{print "awk works"}'
echo "hello world" | less    # should page
bash -c 'echo "bash works"'
vim --version | head -1
sysctl kern.ostype
ps aux
bzip2 --help
```

## Handling Missing Symbols

When `audit_package.sh` reports missing symbols:

1. **Check if it's a $UNIX2003 variant:** `bash panthera_patch.sh unix2003 _function`
2. **Check if it's a simple function:** `bash panthera_patch.sh impl path/to/impl.c`
3. **Check if returning 0 is safe:** `bash panthera_patch.sh stub _function`
4. **After adding:** relink_libpanthera_extra → relink_libSystem → build_shared_cache → verify_exports

Do NOT spend hours debugging a single utility. If something needs IOKit, CoreFoundation, or extensive stubs, skip it and move on. Document what was skipped and why.

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- Run `tools/audit_package.sh` on EVERY binary before staging
- No `-flat_namespace` in any LDFLAGS
- After sysroot changes: relink chain → shared cache → verify exports
- Boot test and read output — don't ask the user
- No deferred work — every staged binary must work
- If a utility can't be built cleanly in 15 minutes, skip it and document why

## Success Criteria

- `awk`, `less`, `bash` compile and run in the guest
- At least `ps`, `sysctl`, `reboot` from system_cmds work
- `bzip2` compresses and decompresses
- All binaries pass `audit_package.sh`
- Zero dyld warnings from any new binary
- `/etc/shells` updated with `/bin/bash`

## Deliverables

Report:
1. Which utilities were built successfully (with binary sizes)
2. Which were skipped and why (specific missing dependency)
3. How many symbols were added to the sysroot (if any)
4. Boot test results
