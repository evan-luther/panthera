# Panthera 0.1 — Compatibility Manifest

## Platform

- **Kernel:** XNU xnu-10002.41.9 (Darwin 23.1.0)
- **Architecture:** x86_64
- **Target:** QEMU q35 with Haswell CPU (also works on commodity PC hardware)
- **Userland ABI:** Mach-O 64-bit, two-level namespace
- **C Library:** Apple Libc-1583.40.7 (libsystem_c, 1085 exports)
- **Malloc:** Apple libmalloc-474.0.13 (124 exports)
- **Dynamic linker:** dyld-1122.1.2 (custom Panthera build)

## What Works

### Core OS
- Boot to interactive shell via launchd plist-driven service management
- HFS+ root filesystem (read-write)
- devfs with 291 device nodes (null, zero, random, urandom, console, tty, ptmx, 128 PTY pairs)
- RTC / system clock (reads QEMU MC146818)
- kqueue event loop (EVFILT_MACHPORT, EVFILT_PROC, EVFILT_READ/WRITE via poll)
- Mach IPC (ports, messages, rights, bootstrap server)
- Process management (fork, exec, waitpid, signals, pipes)

### Shell & Utilities
- zsh 5.9 with ZLE line editing, tab completion, command history
- 69+ coreutils (ls, cp, mv, grep, sed, find, sort, etc.)
- nano 8.7 text editor
- ncurses 5.4 with terminfo (vt100, xterm, screen, dumb)

### Networking
- TCP, UDP, ICMP
- DNS resolution (standalone getaddrinfo with RFC 1035 UDP queries)
- curl 8.12.1 with HTTP and HTTPS (TLS 1.3 via OpenSSL 3.5.5)
- Network tools: ifconfig, ping, route, netstat
- Static IP configuration via netbringup

### Service Management
- launchd with plist parsing (Label, ProgramArguments, RunAtLoad, KeepAlive, MachServices, StandardOutPath/ErrorPath)
- Mach bootstrap server (bootstrap_check_in, bootstrap_look_up, bootstrap_register)
- kqueue-based event loop with EVFILT_PROC child supervision
- Job lifecycle with service port cleanup on exit

## Known Limitations

### Threading
- `pthread_create()` returns EAGAIN (cannot create threads)
- Mutex lock/unlock works (single-threaded correctness)
- rwlock, condvar: real implementations in libsystem_pthread for $UNIX2003 variants
- **Impact:** Packages requiring threads must use `--disable-threads` or equivalent

### Dynamic Loading
- `dlopen()` returns NULL for all paths (except RTLD_DEFAULT sentinel)
- `dlsym()` can look up symbols from RTLD_DEFAULT
- `dlclose()` is a no-op
- **Impact:** No runtime plugin loading. Use `--without-dlopen` or static linking.

### Locale
- `setlocale()` works, but locale data is minimal
- `gettext` / `libintl` not available
- **Impact:** Use `--disable-nls`. Locale-dependent formatting may be wrong.

### Networking
- No DHCP client (static IP only)
- No IPv6
- `select()` on sockets does not work (fix in progress). Use `poll()` instead.
- `getaddrinfo()` resolves A records only (no AAAA, no SRV, no mDNS)
- `gethostbyname()` provided by libsystem_info (real implementation)
- `getifaddrs()` not implemented

### POSIX IPC
- `shm_open()` / POSIX shared memory: not implemented
- `sem_open()` / named semaphores: not implemented
- `mq_open()` / message queues: not implemented

### Regex
- `regcomp()` / `regexec()` return error (not implemented)
- **Impact:** Programs using POSIX regex will fail. grep uses its own internal engine.

### Missing Subsystems
- No sandbox / seatbelt
- No audit / BSM
- No content filter (disabled — was causing kernel panics)
- No NECP policy evaluation
- No XPC (stub library, all functions return NULL/0)
- No CoreFoundation
- No Security framework / keychain
- No IOKit userland

## Kernel Notes

- EVFILT_TIMER does not fire (launchd works around this)
- Content filter (cfil_sock_attach) guarded with early return
- TCP works for all tested use cases (HTTP, HTTPS, DNS)
- ICMP ping requires setuid on /sbin/ping (launchd does this at boot)

## Build Requirements

- macOS host with Xcode (clang, xcrun, SDK)
- Cross-compilation target: x86_64-apple-darwin23.0
- All packages link against `-lSystem` (Panthera's libSystem.B.dylib umbrella)
- **Never use `-Wl,-flat_namespace`** in build flags

## File Locations

| Path | Contents |
|------|----------|
| `/etc/panthera-release` | OS version identifier |
| `/etc/resolv.conf` | DNS server (10.0.2.3 for QEMU) |
| `/etc/hostname` | System hostname |
| `/etc/ssl/cert.pem` | CA certificate bundle |
| `/System/Library/CoreServices/SystemVersion.plist` | sw_vers data |
| `/System/Library/LaunchDaemons/` | Service plists |
| `/usr/share/terminfo/` | Terminal capability database |
| `/var/log/` | Service log files |
