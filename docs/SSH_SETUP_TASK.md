# SSH Setup — Historical Dropbear Plan

Status: superseded. Panthera's active alpha SSH implementation is portable
OpenSSH staged by `manifests/ssh.system`, verified by
`tools/alpha_release_gate.sh`, and launched for developers with
`tools/run_qemu_ssh.sh`. Keep this document only as historical context for the
earlier Dropbear starter plan.

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules. Read `docs/PACKAGE_PORTING_GUIDE.md` for the package porting process — follow Phase 1 (symbol audit) before boot testing.

Panthera needs SSH so users can connect via `ssh -p 2222 root@localhost` from the host into the QEMU guest. Dropbear was previously built but all artifacts (build script, binaries, plist, shims) were lost. This task rebuilds it from scratch.

## What Already Exists

- **Dropbear source:** `src/dropbear-2024.86/` (complete, with configure)
- **QEMU port forward:** Host port 2222 → guest port 22 (already in `boot/qemu/run_phase2_qemu.sh`)
- **All required sysroot symbols are present:** openpty, forkpty, login_tty, crypt, getaddrinfo, freeaddrinfo, gai_strerror, inet_ntop, if_nametoindex, clock — all verified
- **OpenSSL:** libssl.3.dylib and libcrypto.3.dylib built and staged
- **Root password:** hash `rtpKG9CXsBXAA` in `/etc/master.passwd` (password is likely empty or "panthera")
- **Login shell:** `/bin/zsh`
- **PTY support:** `/dev/ptmx` + 128 PTY pairs in devfs

## What's Missing

- `userland/dropbear/` — entire directory needs recreation (build script, bin/, localoptions.h)
- `rootfs/System/Library/LaunchDaemons/com.panthera.dropbear.plist` — launchd plist
- `rootfs/etc/shells` — required by Dropbear for shell validation
- Staging in `rootfs/create_hfs_root_image.sh`

## Step 1: Create Build Script

Create `userland/dropbear/build_dropbear.sh` following the standard Panthera cross-compilation pattern.

**Configure flags:**
```bash
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"

CC="$CC" \
CFLAGS="-target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2 -w" \
CPPFLAGS="-target $TARGET -isysroot $SDKROOT" \
LDFLAGS="-target $TARGET -isysroot $SDKROOT -lSystem" \
./configure --host=$TARGET \
    --disable-zlib --disable-pam --disable-shadow --disable-syslog \
    --disable-lastlog --disable-utmp --disable-utmpx \
    --disable-wtmp --disable-wtmpx --disable-loginfunc \
    --disable-pututline --disable-pututxline \
    --enable-bundled-libtom --disable-harden

make -j$(sysctl -n hw.ncpu) PROGRAMS="dropbear dropbearkey"
```

**CRITICAL LDFLAGS rules:**
- **NO `-Wl,-flat_namespace`** — causes shared cache binding failures
- **NO `-Wl,-undefined,dynamic_lookup`** — same problem
- Link with `-lSystem` only. Dropbear uses bundled libtomcrypt/libtommath (`--enable-bundled-libtom`) so no external crypto dependency.

**Create `localoptions.h`** in the Dropbear source directory before configure:
```c
#define DROPBEAR_SVR_AGENTFWD 0
#define DROPBEAR_CLI_AGENTFWD 0
#define DROPBEAR_SVR_LOCALTCPFWD 0
#define DROPBEAR_SVR_REMOTETCPFWD 0
#define DROPBEAR_SVR_LOCALSTREAMFWD 0
#define DROPBEAR_CLI_LOCALTCPFWD 0
#define DROPBEAR_CLI_REMOTETCPFWD 0
#define DROPBEAR_SFTPSERVER 0
#define DROPBEAR_SK_KEYS 0
#define DROPBEAR_CLI_PROXYCMD 0
#define DROPBEAR_CLI_NETCAT 0
#define DROPBEAR_USER_ALGO_LIST 0
#define INETD_MODE 0
#define DROPBEAR_REEXEC 0
#define DEBUG_TRACE 0
```

**Output:**
- `userland/dropbear/bin/dropbear`
- `userland/dropbear/bin/dropbearkey`

## Step 2: Symbol Audit

After building, run the symbol audit **before** staging:
```bash
bash tools/audit_package.sh userland/dropbear/bin/dropbear userland/dropbear/bin/dropbearkey
```

Both must show PASS for symbols and namespace. Fix any missing symbols via `panthera_patch.sh` before proceeding.

## Step 3: Create Launchd Plist

Create `rootfs/System/Library/LaunchDaemons/com.panthera.dropbear.plist`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Label</key>
	<string>com.panthera.dropbear</string>
	<key>ProgramArguments</key>
	<array>
		<string>/usr/sbin/dropbear</string>
		<string>-F</string>
		<string>-E</string>
		<string>-R</string>
		<string>-B</string>
	</array>
	<key>RunAtLoad</key>
	<true/>
	<key>KeepAlive</key>
	<true/>
	<key>StandardOutPath</key>
	<string>/var/log/dropbear.log</string>
	<key>StandardErrorPath</key>
	<string>/var/log/dropbear.log</string>
</dict>
</plist>
```

Flags: `-F` foreground (launchd manages lifecycle), `-E` log to stderr, `-R` generate host keys on first connection, `-B` allow blank password.

## Step 4: Create /etc/shells

Create `rootfs/etc/shells`:
```
/bin/zsh
/bin/sh
```

Dropbear validates the user's login shell against this file.

## Step 5: Stage to Root Image

Update `rootfs/create_hfs_root_image.sh`:

1. Add build trigger:
```bash
DROPBEAR_BUILD_SCRIPT="${PANTHERA_ROOT}/userland/dropbear/build_dropbear.sh"
if [[ -x "${DROPBEAR_BUILD_SCRIPT}" ]]; then
    bash "${DROPBEAR_BUILD_SCRIPT}"
fi
```

2. Add directory creation (in the mkdir -p block):
```bash
"${mounted_volume}/etc/dropbear" \
"${mounted_volume}/var/root/.ssh" \
"${mounted_volume}/usr/sbin" \
```

3. Add binary staging:
```bash
DROPBEAR_BIN_DIR="${PANTHERA_ROOT}/userland/dropbear/bin"
if [[ -f "${DROPBEAR_BIN_DIR}/dropbear" ]]; then
    cp "${DROPBEAR_BIN_DIR}/dropbear" "${mounted_volume}/usr/sbin/dropbear"
    cp "${DROPBEAR_BIN_DIR}/dropbearkey" "${mounted_volume}/usr/bin/dropbearkey"
fi
```

4. The `/etc/shells` file is already in the etc staging loop (added in a previous session). Verify it's there:
```bash
grep 'shells' rootfs/create_hfs_root_image.sh
```

The plist will be staged automatically by the existing LaunchDaemons plist loop.

## Step 6: Rebuild and Boot Test

```bash
bash rootfs/create_hfs_root_image.sh --force
PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot \
    --root-disk images/qemu/panthera-root.img
```

Check boot log for:
- `launchd: spawn com.panthera.dropbear` — Dropbear started
- `[svr-dropbear] Running in background` or similar — listening on port 22

## Step 7: SSH Connection Test

From a **second terminal** on the host:
```bash
ssh -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@localhost
```

If root has a blank password, this should connect directly. If it prompts for a password, try empty string or "panthera".

Expected result: zsh prompt inside Panthera via SSH.

## Step 8: Verify SSH Session Works

Once connected via SSH, test:
```bash
uname -a          # Should show Darwin panthera 23.1.0 ... x86_64
hostname          # Should show "panthera"
sw_vers           # Should show Panthera 0.1
ls /              # Filesystem access
curl http://google.com  # Networking from SSH session
```

## Troubleshooting

**Dropbear crashes at startup:**
- Check `/var/log/dropbear.log` for error messages
- Run `/usr/sbin/dropbear -F -E` manually from the console to see errors

**SSH connection refused:**
- Verify Dropbear is listening: from the console, check `netstat -an | grep 22`
- Verify QEMU port forward: the `--ssh-port 0` flag disables it. Use the default (2222) or specify explicitly.

**Authentication fails:**
- Verify `/etc/shells` contains `/bin/zsh`
- Verify root's shell in `/etc/master.passwd` is `/bin/zsh`
- Dropbear's `-B` flag allows blank passwords. If that doesn't work, generate a key: `dropbearkey -t rsa -f /etc/dropbear/dropbear_rsa_host_key` and use pubkey auth.

**PTY allocation fails:**
- Verify `/dev/ptmx` exists (devfs creates it at boot)
- Check that openpty/forkpty/login_tty are in the sysroot (verified above)

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- Run `tools/audit_package.sh` before boot testing
- No `-flat_namespace` in LDFLAGS
- After any sysroot change: relink_libpanthera_extra → relink_libSystem → build_shared_cache → verify_exports
- Boot test and read output — don't ask the user
- No deferred work — SSH must work end-to-end

## Success Criteria

- `ssh -p 2222 root@localhost` connects and drops into a zsh shell
- Shell is functional (can run commands, edit files, curl works)
- Dropbear starts automatically via launchd plist
- Dropbear restarts if killed (KeepAlive)
- Zero dyld warnings from dropbear
- `tools/audit_package.sh` passes for both binaries
