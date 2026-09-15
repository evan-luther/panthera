# Tier 2 — Package Foundation for Panthera

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules. Read `docs/PACKAGE_PORTING_GUIDE.md` for the mandatory symbol audit process.

Tier 1 is complete (awk, less, bash, vim, bzip2, system_cmds — 22 binaries). Tier 2 builds the libraries and tools needed for package installation and software compilation.

## Source Repos

All from https://github.com/apple-oss-distributions. Download into `src/`:

```bash
cd src/
for repo_ver in \
    "libarchive:libarchive-160.60.3" \
    "libxml2:libxml2-39.10" \
    "pcre:pcre-21" \
    "expat:expat-45" \
    "libffi:libffi-40" \
    "libedit:libedit-65" \
    "patch_cmds:patch_cmds-72" \
    "sudo:sudo-114.60.3"; do
    repo="${repo_ver%%:*}"
    tag="${repo_ver##*:}"
    if [ ! -d "$tag" ]; then
        git clone --depth 1 --branch "$tag" \
            "https://github.com/apple-oss-distributions/${repo}.git" "$tag" 2>/dev/null
    fi
done
```

Note: Apple's repos often have the source inside a subdirectory (e.g., `libarchive-160.60.3/libarchive/`). Check the directory layout after cloning.

## Build Order

Build libraries first (other packages may depend on them), then tools:

1. **expat** (no deps)
2. **libxml2** (depends on zlib — have it)
3. **pcre** (no deps)
4. **libffi** (no deps)
5. **libedit** (depends on ncurses — have it)
6. **libarchive** (depends on zlib, bzip2, libxml2, expat — all available)
7. **patch_cmds** (no deps)
8. **sudo** (depends on libc — may need PAM stubs)

## Cross-Compilation Pattern

```bash
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
TARGET="x86_64-apple-darwin23.0"
CFLAGS="-target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2"
CPPFLAGS="-target $TARGET -isysroot $SDKROOT"
LDFLAGS="-target $TARGET -isysroot $SDKROOT -lSystem"
```

**NEVER** use `-Wl,-flat_namespace` or `-Wl,-undefined,dynamic_lookup`.

## Package Details

### 1. expat (XML SAX parser)

**Source:** `src/expat-45/`
**Output:** `libexpat.1.dylib` → `/usr/lib/`, headers → sysroot
**Configure:** `./configure --host=$TARGET --prefix=/usr --disable-shared` (or shared)
**Notes:** Very small, pure C. Many packages check for expat in configure.

### 2. libxml2 (XML DOM parser)

**Source:** `src/libxml2-39.10/`
**Output:** `libxml2.2.dylib` → `/usr/lib/`, `xmllint` → `/usr/bin/`
**Configure:**
```bash
./configure --host=$TARGET --prefix=/usr \
    --with-zlib="$SYSROOT/usr" \
    --without-python --without-lzma --without-iconv \
    --disable-static
```
**Notes:** Apple's version. Core dependency for CF (later) and many packages. Build the library AND `xmllint` (useful diagnostic tool). Set install name: `install_name_tool -id /usr/lib/libxml2.2.dylib`.

### 3. pcre (Perl-Compatible Regular Expressions)

**Source:** `src/pcre-21/`
**Output:** `libpcre.1.dylib` → `/usr/lib/`
**Configure:**
```bash
./configure --host=$TARGET --prefix=/usr \
    --enable-utf8 --enable-unicode-properties \
    --disable-cpp --disable-static
```
**Notes:** Many tools use pcre for regex (grep -P, etc.). Apple ships pcre, not pcre2.

### 4. libffi (Foreign Function Interface)

**Source:** `src/libffi-40/`
**Output:** `libffi.dylib` → `/usr/lib/`
**Notes:** Needed by Python, Ruby, and other language runtimes. Has x86_64 assembly — the Apple version should have the right assembly for our target. May need a hand-written Makefile if no configure script.

### 5. libedit (Readline alternative)

**Source:** `src/libedit-65/`
**Output:** `libedit.3.dylib` → `/usr/lib/`
**Configure:**
```bash
./configure --host=$TARGET --prefix=/usr \
    CFLAGS="$CFLAGS -I$SYSROOT/usr/include" \
    LDFLAGS="$LDFLAGS -L$SYSROOT/usr/lib -lncurses.5.4"
```
**Notes:** Apple's readline replacement (BSD licensed). Used by many interactive tools. Depends on ncurses (have it). After building, stage headers to sysroot so future packages can find it.

### 6. libarchive (CRITICAL — bsdtar)

**Source:** `src/libarchive-160.60.3/`
**Output:** `libarchive.13.dylib` → `/usr/lib/`, `bsdtar` → `/usr/bin/tar`, `bsdcpio` → `/usr/bin/cpio`
**Configure:**
```bash
./configure --host=$TARGET --prefix=/usr \
    --with-zlib --with-bz2lib \
    --with-expat --without-xml2 \
    --without-lzma --without-lz4 --without-zstd \
    --without-openssl --disable-acls --disable-xattr \
    --disable-static
```
**Notes:** This is the most important package in Tier 2. `bsdtar` is needed to extract ANY downloaded archive. Without it, no package installation is possible. Apple's version produces `bsdtar` which is the standard `tar` on macOS/FreeBSD.

Symlinks: `tar -> bsdtar`, `cpio -> bsdcpio`

### 7. patch_cmds (diff + patch)

**Source:** `src/patch_cmds-72/`
**Output:** `patch` → `/usr/bin/patch`, `diff` → `/usr/bin/diff` (if included)
**Notes:** Apple's patch command. Simple C, links against libSystem only. Essential for applying source patches.

Note: `diff` might be in `text_cmds` or `file_cmds` instead of `patch_cmds`. Check the source tree. If patch_cmds only has `patch`, that's fine — `diff` may already be built.

### 8. sudo

**Source:** `src/sudo-114.60.3/`
**Output:** `sudo` → `/usr/bin/sudo`, `sudoers` → `/etc/sudoers`
**Configure:**
```bash
./configure --host=$TARGET --prefix=/usr \
    --without-pam --without-sendmail \
    --without-ldap --without-selinux \
    --disable-nls
```
**Notes:** Privilege escalation. Configure without PAM (Panthera has no PAM stack). May need password checking via crypt() (have it in sysroot). If it requires too many stubs, defer — it's the lowest priority in Tier 2.

## Build Script Structure

Create `userland/<package>/build_<package>.sh` for each package. Each script must:

1. Download source if not in `src/` (use git clone from Apple's repo)
2. Configure with cross-compilation flags
3. Build
4. Copy outputs to `userland/<package>/bin/` and/or `userland/<package>/lib/`
5. Fix install names: `install_name_tool -id /usr/lib/libfoo.dylib`
6. Run `tools/audit_package.sh` on all outputs
7. Be idempotent (skip if outputs exist unless PANTHERA_FORCE_REBUILD=1)

## Staging

Update `rootfs/create_hfs_root_image.sh`:

**Libraries** — copy dylibs to `/usr/lib/`, create version symlinks:
```bash
# Example for libxml2
cp libxml2.2.dylib "${mounted_volume}/usr/lib/libxml2.2.dylib"
ln -sf libxml2.2.dylib "${mounted_volume}/usr/lib/libxml2.dylib"
```

**Tools** — copy binaries to appropriate paths:
```bash
# bsdtar is the big one
cp bsdtar "${mounted_volume}/usr/bin/bsdtar"
ln -sf bsdtar "${mounted_volume}/usr/bin/tar"
ln -sf bsdtar "${mounted_volume}/usr/bin/cpio"
```

**Sysroot** — for libraries that other packages depend on (libxml2, pcre, expat, libffi, libedit), also stage headers and dylibs into the sysroot so future cross-compilation can find them:
```bash
cp libxml2.2.dylib "${SYSROOT}/usr/lib/libxml2.2.dylib"
ln -sf libxml2.2.dylib "${SYSROOT}/usr/lib/libxml2.dylib"
cp -r include/libxml2 "${SYSROOT}/usr/include/"
```

## Handling Missing Symbols

If `audit_package.sh` reports missing symbols:

1. Check if it's already in the sysroot under a different name ($UNIX2003 variant, etc.)
2. Check if the Tier 1 session already added it (36 symbols were added)
3. Add via `panthera_patch.sh` if needed
4. After adding: relink_libpanthera_extra → relink_libSystem → build_shared_cache → verify_exports

## Boot Test

After staging everything:

```bash
bash rootfs/create_hfs_root_image.sh --force
PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot --ssh-port 0 \
    --root-disk images/qemu/panthera-root.img
```

Verify:
```bash
tar --version                           # bsdtar works
echo '<root/>' | xmllint -              # libxml2 works
echo "test" | bzip2 | bunzip2          # compression pipeline
patch --version                         # patch available
sudo -V                                 # sudo available (if built)
```

## Priority

If time is limited, build in this order:

1. **libarchive (bsdtar)** — blocks all package installation
2. **patch_cmds** — needed to apply patches
3. **libxml2** — dependency for many packages and future CF
4. **pcre** — dependency for many packages
5. **expat** — dependency for libarchive and others
6. **libedit** — nice to have, improves interactive tools
7. **libffi** — needed for Python/Ruby later
8. **sudo** — nice to have, can defer

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- Run `tools/audit_package.sh` on EVERY output before staging
- No `-flat_namespace` in any LDFLAGS
- After sysroot changes: relink chain → shared cache → verify exports
- Boot test and read output — don't ask the user
- No deferred work — every staged binary/library must work
- If a package can't be built cleanly in 30 minutes, skip and document why
- Stage library headers to sysroot so future packages can find them

## Success Criteria

- `tar` (bsdtar) extracts .tar.gz and .tar.bz2 archives
- `patch` applies diffs
- libxml2, pcre, expat, libffi, libedit are built and staged (both in root image and sysroot)
- All binaries pass `audit_package.sh`
- Zero dyld warnings
- Future cross-compilations can find the new libraries via `-L$SYSROOT/usr/lib -lxml2`

## Deliverables

Report:
1. Which packages built successfully (binary/library sizes)
2. Which were skipped and why
3. How many sysroot symbols were added
4. Boot test results showing tar/patch/xmllint working
