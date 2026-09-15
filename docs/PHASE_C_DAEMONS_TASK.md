# Phase C — Real Daemons: OpenSSH, syslog, ntp

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules. Read `docs/PACKAGE_PORTING_GUIDE.md` for the mandatory symbol audit process.

Phases A and B are complete (Tier 1 utilities + Tier 2 libraries). Threading now works (pthread_create, mutex, condvar all functional). This phase brings up real Darwin daemons.

## Priority Order

1. **OpenSSH** — CRITICAL. SSH access is the #1 usability requirement.
2. **syslog** — HIGH. Centralized logging for all daemons.
3. **ntp** — MEDIUM. Time synchronization (QEMU RTC works, but ntp is proper).

## Prerequisites (all met)

- OpenSSL 3.5.5: `userland/openssl/lib/libssl.3.dylib` + `libcrypto.3.dylib`
- zlib: `userland/libsystem/build/sysroot/usr/lib/libz.1.dylib`
- libedit: `userland/libedit/lib/libedit.3.dylib`
- libSystem with 453+ exports
- Working TCP/UDP networking, DNS resolution, TLS 1.3
- select() and poll() both functional
- PTY support: /dev/ptmx + 128 pairs
- pthread_create functional (threads execute, mutex/condvar work)
- launchd plist-driven service management with Mach bootstrap

---

## 1. OpenSSH (CRITICAL)

**Source:** https://github.com/apple-oss-distributions/OpenSSH (tag: OpenSSH-354.80.3)

**Download:**
```bash
cd src/
git clone --depth 1 --branch OpenSSH-354.80.3 \
    https://github.com/apple-oss-distributions/OpenSSH.git OpenSSH-354.80.3
```

**Note:** Apple's OpenSSH wraps the standard OpenSSH source with Apple-specific patches. The actual OpenSSH source is likely inside a subdirectory. Check the layout after cloning — it may be `OpenSSH-354.80.3/openssh/` or similar. If Apple's version is too heavily patched, use upstream OpenSSH from https://www.openssh.com/ instead (the portable version builds on any Unix).

**Output binaries:**

| Binary | Install path | Purpose |
|--------|-------------|---------|
| sshd | /usr/sbin/sshd | SSH server daemon |
| ssh | /usr/bin/ssh | SSH client |
| scp | /usr/bin/scp | Secure copy |
| sftp | /usr/bin/sftp | Secure FTP |
| sftp-server | /usr/libexec/sftp-server | SFTP subsystem |
| ssh-keygen | /usr/bin/ssh-keygen | Key generation |
| ssh-agent | /usr/bin/ssh-agent | Key agent |
| ssh-add | /usr/bin/ssh-add | Add keys to agent |

**Configure:**
```bash
CC="$(xcrun -find clang)"
SDKROOT="$(xcrun -sdk macosx --show-sdk-path)"
TARGET="x86_64-apple-darwin23.0"
SYSROOT="${PANTHERA_ROOT}/userland/libsystem/build/sysroot"
OPENSSL_DIR="${PANTHERA_ROOT}/userland/openssl/stage/usr"
ZLIB_DIR="${SYSROOT}/usr"

./configure --host=$TARGET --prefix=/usr \
    --sysconfdir=/etc/ssh \
    --with-ssl-dir="${OPENSSL_DIR}" \
    --with-zlib="${ZLIB_DIR}" \
    --with-privsep-user=nobody \
    --with-privsep-path=/var/empty \
    --without-pam \
    --without-kerberos5 \
    --without-sandbox \
    --without-selinux \
    --without-audit \
    --without-bsm \
    --disable-strip \
    --disable-utmpx \
    --disable-wtmpx \
    --disable-lastlog \
    CC="$CC" \
    CFLAGS="-target $TARGET -mmacosx-version-min=14.0 -isysroot $SDKROOT -O2" \
    CPPFLAGS="-target $TARGET -isysroot $SDKROOT -I${OPENSSL_DIR}/include" \
    LDFLAGS="-target $TARGET -isysroot $SDKROOT -L${OPENSSL_DIR}/lib -L${ZLIB_DIR}/lib -lSystem"
```

**CRITICAL:** No `-Wl,-flat_namespace`. No `-Wl,-undefined,dynamic_lookup`.

If Apple's OpenSSH version has too many Apple-specific dependencies (sandbox, keychain, etc.), use **upstream portable OpenSSH** instead:
```bash
curl -L -o src/openssh-9.9p1.tar.gz https://cdn.openbsd.org/pub/OpenBSD/OpenSSH/portable/openssh-9.9p1.tar.gz
tar -C src/ -xf src/openssh-9.9p1.tar.gz
```
The portable version is designed for non-OpenBSD systems and has its own configure.

**Launchd plist** — create `rootfs/System/Library/LaunchDaemons/com.panthera.sshd.plist`:
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.panthera.sshd</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/sbin/sshd</string>
        <string>-D</string>
        <string>-e</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/var/log/sshd.log</string>
    <key>StandardErrorPath</key>
    <string>/var/log/sshd.log</string>
</dict>
</plist>
```

Flags: `-D` (don't daemonize, run in foreground for launchd), `-e` (log to stderr).

**SSH config** — create `rootfs/etc/ssh/sshd_config`:
```
Port 22
Protocol 2
HostKey /etc/ssh/ssh_host_rsa_key
HostKey /etc/ssh/ssh_host_ed25519_key
PermitRootLogin yes
PasswordAuthentication yes
PermitEmptyPasswords yes
UsePAM no
Subsystem sftp /usr/libexec/sftp-server
```

**Staging in `rootfs/create_hfs_root_image.sh`:**
- Create directories: `/etc/ssh`, `/var/empty` (privsep), `/usr/libexec`
- Copy all binaries to their install paths
- Copy `sshd_config` to `/etc/ssh/sshd_config`
- Generate host keys at image build time:
```bash
ssh-keygen -t rsa -f "${mounted_volume}/etc/ssh/ssh_host_rsa_key" -N "" -q
ssh-keygen -t ed25519 -f "${mounted_volume}/etc/ssh/ssh_host_ed25519_key" -N "" -q
```
(Use the host macOS ssh-keygen — the keys are portable)

**QEMU port forwarding** is already configured: host:2222 → guest:22 (in `boot/qemu/run_phase2_qemu.sh`).

**Test from host:**
```bash
ssh -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@localhost
```

**After connecting, verify:**
```bash
uname -a
hostname
sw_vers
curl http://google.com
```

---

## 2. syslog

**Source:** https://github.com/apple-oss-distributions/syslog (tag: syslog-406)

**Assessment:** Apple's syslogd is complex — it integrates with ASL (Apple System Log), dispatch queues, and has a non-trivial build. Evaluate the source first.

If Apple's syslogd is too complex, write a **minimal Panthera syslogd** (~200 lines C):
- Opens `/var/run/syslog` Unix socket (BSD syslog protocol)
- Reads messages from the socket
- Appends to `/var/log/system.log` with timestamps
- Optionally registers `com.apple.system.logger` via bootstrap_check_in

This gives centralized logging without Apple-specific complexity.

**Launchd plist:**
```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.panthera.syslogd</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/sbin/syslogd</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>MachServices</key>
    <dict>
        <key>com.apple.system.logger</key>
        <true/>
    </dict>
</dict>
</plist>
```

---

## 3. ntp

**Source:** https://github.com/apple-oss-distributions/ntp (tag: ntp-139)

Or use **OpenNTPD** (simpler, from OpenBSD): https://www.openntpd.org/

**Notes:** QEMU's RTC already provides correct time from the host. NTP is nice to have but not critical. If time is limited, skip this.

**If building:**
```bash
./configure --host=$TARGET --prefix=/usr \
    --disable-all-clocks --disable-debugging \
    CC="$CC" CFLAGS="..." LDFLAGS="..."
```

---

## Symbol Audit

Run `bash tools/audit_package.sh` on EVERY binary before staging. Fix any missing symbols via `panthera_patch.sh`.

OpenSSH commonly needs:
- `getaddrinfo` / `freeaddrinfo` — have them
- `openpty` / `forkpty` / `login_tty` — have them
- `crypt` — have it
- `inet_ntop` / `inet_ntoa` — have them
- `setproctitle` — may need a stub (return void, no-op is fine)
- `daemon` — may need implementation (fork, setsid, chdir /, close fds)
- `getpeereid` — may need stub or implementation (getsockopt SO_PEERCRED equivalent)
- `arc4random` / `arc4random_buf` — may need implementation (read from /dev/urandom)
- `explicit_bzero` — may be in libc or need a stub
- `freezero` — may need implementation (bzero + free)

After adding: relink_libpanthera_extra → relink_libSystem → build_shared_cache → verify_exports

## Build Scripts

Create:
- `userland/openssh/build_openssh.sh`
- `userland/syslog/build_syslogd.sh` (if building Apple's) or `userland/syslog/syslogd.c` (if writing minimal)

## Boot Test

```bash
cp images/qemu/panthera-root.img images/qemu/panthera-root.img.pre-phase-c
bash rootfs/create_hfs_root_image.sh --force
PANTHERA_NOGRAPHIC=1 boot/qemu/run_phase2_qemu.sh --no-reboot \
    --root-disk images/qemu/panthera-root.img
```

Check boot log for:
- `launchd: spawn com.panthera.sshd` — sshd started
- No dyld warnings from sshd
- No crashes

Then from a second terminal:
```bash
ssh -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@localhost
```

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- Run `tools/audit_package.sh` on ALL outputs before staging
- No `-flat_namespace`
- After sysroot changes: relink chain → shared cache → verify exports
- If Apple's OpenSSH is too complex, use upstream portable OpenSSH
- If Apple's syslogd is too complex, write a minimal one
- Boot test and read output — don't ask the user
- No deferred work — SSH must work end-to-end
- SSH test: `ssh -p 2222 root@localhost` from the host

## Success Criteria

1. **SSH:** `ssh -p 2222 root@localhost` drops into a working zsh shell
2. **SSH session:** can run commands, edit files with vim, curl works
3. **scp:** `scp -P 2222 localfile root@localhost:/tmp/` transfers a file
4. **sshd:** restarts automatically if killed (KeepAlive)
5. **syslog:** `/var/log/system.log` captures daemon messages
6. **All binaries** pass `audit_package.sh`

## Deliverables

Report:
1. Which SSH implementation was used (Apple or portable OpenSSH)
2. Which syslog implementation was used (Apple or minimal)
3. How many symbols were added to sysroot
4. SSH connection test result (full session transcript)
5. Boot log showing daemon startup
