# Post-XPC Roadmap — Bringing Up Real Darwin Infrastructure

## Context

Read `OS_BUILD_ROADMAP.md` for foundation rules.

The platform layer is complete:
- CoreFoundation (1,562 exports, full ICU/ObjC runtime)
- XPC (real implementation with object model + Mach IPC connections)
- Mach bootstrap server (check-in, look-up, dead-name notifications)
- launchd (kqueue event loop, job lifecycle, CFPropertyList parsing)
- libc++ / libc++abi / ICU / objc4 all built and in shared cache

This roadmap covers the next phase: standing up real Apple daemons in the correct dependency order.

## Priority 1: Fix Current Networking

**Before building new daemons, fix what's broken.** curl DNS resolution fails intermittently. This is likely a netbringup timing issue from recent image rebuilds.

**Debug:**
```bash
# From the Panthera console after boot:
ifconfig en0          # Is the interface configured?
cat /etc/resolv.conf  # Is the DNS server set?
ping 10.0.2.2         # Does ICMP work?
ping 10.0.2.3         # Can we reach the DNS server?
curl http://google.com # Does DNS resolve?
```

If netbringup didn't run, check `/var/log/netbringup.log` and the launchd delayed spawn timer. If it ran but DNS still fails, the standalone getaddrinfo resolver may need debugging.

**Fix this before proceeding. Everything downstream depends on working networking.**

## Priority 2: notifyd

**Source:** `apple-oss-distributions/Libnotify` (tag: Libnotify-344.0.1, already have Libnotify-317)
**Dependencies:** CF (have it), Mach IPC (have it), XPC (have it)
**Service name:** `com.apple.system.notification_center`

notifyd is the Darwin notification daemon. It provides cross-process notifications via `notify_post()` and `notify_register_*()`. Currently, all notify functions in Panthera are no-ops — they silently do nothing.

**Why first:** It's small, exercises XPC for real in production, and has the widest invisible impact. Libraries across the system degrade gracefully without notifications but work at full capability with them. CF itself uses `notify_register_dispatch()` for some configuration change tracking.

**What notifyd does:**
- Listens on a Mach service port (`com.apple.system.notification_center`)
- Maintains a table of notification names → registered clients
- When `notify_post("com.apple.some.event")` is called, notifyd wakes all registered watchers
- Supports multiple delivery mechanisms: Mach ports, file descriptors, signals, dispatch

**Build approach:**
- Download `Libnotify-344.0.1` (or use the existing `Libnotify-317`)
- The daemon source is in the repo alongside the client library
- The client library (`libnotify.c`, `notify_client.c`) may need to be built and staged to replace the current no-op stubs
- Compile the daemon, stage to `/usr/sbin/notifyd`
- Create launchd plist with MachServices for `com.apple.system.notification_center`

**Verification:** `notify_post("com.panthera.test")` from one process wakes `notify_register_check()` in another.

---

## Priority 3: Real syslogd

**Source:** `apple-oss-distributions/syslog` (tag: syslog-406)
**Dependencies:** CF (have it), notifyd (priority 2), Mach IPC
**Service name:** `com.apple.system.logger`

Replace the minimal 202-line Panthera syslogd with Apple's real syslogd. This gives:
- ASL (Apple System Log) structured logging
- Log facility/severity routing
- Log rotation
- `syslog` CLI tool for querying logs
- Integration with `asl_log()` API

**Why after notifyd:** syslogd uses `notify_post()` to announce new log entries, and other daemons use `notify_register_dispatch()` to watch for log changes.

**Build approach:**
- Check the syslog-406 source structure — the daemon and client library are both in there
- The ASL client library (`libasl`) may need to be built alongside
- Stage daemon to `/usr/sbin/syslogd`, client tools to `/usr/bin/syslog`

**Verification:** `syslog -s "hello from Panthera"` → appears in `/var/log/system.log` and queryable via `syslog -k Sender com.apple.syslog`.

---

## Priority 4: configd + SystemConfiguration.framework

**Source:** `src/configd-1296.40.6/` (already downloaded)
**Additional sources needed:**
- `apple-oss-distributions/IOKitUser` (tag: IOKitUser-100222.80.4) — IOKit userland for interface notifications
- `apple-oss-distributions/bootp` (tag: bootp-531.80.4) — DHCP client (IPConfiguration)

**Dependencies:** CF (have it), XPC (have it), notifyd (priority 2), IOKitUser (need to build)

This is the big networking upgrade. configd replaces the static `netbringup` script with real Darwin networking:
- **DHCP** via bootp/IPConfiguration
- **DNS configuration** from DHCP or manual settings
- **Interface lifecycle** — detects NIC up/down, reconfigures automatically
- **SCDynamicStore** — key-value store for dynamic system state (network, DNS, proxies)
- **SCNetworkReachability** — apps can check "is this host reachable?"

**Build order within configd:**

1. **IOKitUser** — build the userland IOKit library from `apple-oss-distributions/IOKitUser`. This provides `IOServiceGetMatchingServices()`, `IOServiceAddNotification()`, etc. Required by configd's KernelEventMonitor and InterfaceNamer plugins.

2. **SystemConfiguration.framework** — 71 source files, 64K lines. Lives inside the configd source tree at `SystemConfiguration.fproj/`. Build as `libSystemConfiguration.dylib`. Stage headers for downstream consumers.

3. **configd daemon** — the daemon itself at `configd.tproj/`. Links against CF, SystemConfiguration, IOKitUser.

4. **configd plugins** — build in order:
   - `KernelEventMonitor` — interface state tracking
   - `InterfaceNamer` — assigns interface names
   - `IPMonitor` — IP configuration, DNS, routing (12 files)
   - `LinkConfiguration` — link-level settings
   - Skip: `QoSMarking`, `SCNetworkReachability` (can add later)

5. **bootp/IPConfiguration** — DHCP client. Separate daemon that configd communicates with.

**Expected challenges:**
- configd.m uses Objective-C — need `-x objective-c` compilation
- IPMonitor plugins use `.m` files (Objective-C) 
- IOKitUser needs kernel IOKit headers (available in XNU exported headers)
- `PrivacyAccounting/PrivacyAccounting.h` — Apple private, needs stub
- `BASupport/BASupport.h` — Apple private, needs stub
- `libproc.h` — may need stubs for process info functions

**Launchd plist:**
```xml
<key>Label</key>
<string>com.apple.configd</string>
<key>MachServices</key>
<dict>
    <key>com.apple.SystemConfiguration.configd</key>
    <true/>
</dict>
<key>RunAtLoad</key>
<true/>
<key>KeepAlive</key>
<true/>
```

**Verification:** `scutil --dns` shows DNS configuration. `scutil --nwi` shows network interface state. DHCP lease acquired automatically.

**Once configd works:** Remove `netbringup` script and plist. Remove standalone `getaddrinfo` from libpanthera_extra (real Libinfo resolver works through configd/mDNSResponder).

---

## Priority 5: PAM + getty (parallel track)

**Source:**
- `apple-oss-distributions/OpenPAM` — PAM framework
- `apple-oss-distributions/pam_modules` — PAM authentication modules (pam_unix, etc.)
- `apple-oss-distributions/system_cmds` — getty (already partially built)

**Dependencies:** libSystem only (no CF needed)

This is independent of the networking/daemon track and can be done in parallel.

**What it provides:**
- Real password authentication (not blank passwords via `-B`)
- PAM stack: `pam_unix` for `/etc/master.passwd` password checking
- getty: the real Darwin login path is `launchd → getty → login → shell`
- `sudo` with PAM (sudo was skipped in Tier 2 due to missing PAM)

**Build order:**
1. Build OpenPAM → `libpam.dylib`, stage to sysroot
2. Build pam_modules → `pam_unix.so`, `pam_deny.so`, `pam_permit.so`
3. Build getty from system_cmds (or write a minimal one)
4. Update Dropbear to use PAM instead of `-B` (blank passwords)
5. Create `/etc/pam.d/sshd`, `/etc/pam.d/login`, `/etc/pam.d/sudo`
6. Retry building sudo with `--with-pam`

**Verification:** SSH login prompts for password. `sudo whoami` prompts for password and returns `root`.

---

## Priority 6: mDNSResponder

**Source:** `apple-oss-distributions/mDNSResponder` (latest tag)
**Dependencies:** CF (have it), configd/SystemConfiguration (priority 4)
**Service name:** `com.apple.mDNSResponder`

The real DNS resolver daemon. Replaces the standalone getaddrinfo with:
- Full DNS resolution (A, AAAA, SRV, MX, TXT records)
- mDNS/Bonjour (`.local` hostname resolution)
- DNS-SD (service discovery — `_ssh._tcp`, `_http._tcp`)
- DNS caching
- Multi-nameserver failover
- Search domain support

**After mDNSResponder:** Remove the standalone `getaddrinfo` from libpanthera_extra. The real `getaddrinfo` in libsystem_info.dylib routes through mDNSResponder for DNS queries.

---

## Priority 7: launchctl

**Source:** Inside `apple-oss-distributions/launchd` (launchd-842.92.1)
**Dependencies:** CF (have it), XPC (have it), bootstrap port (have it)

CLI tool for managing launchd:
- `launchctl load /path/to/plist` — load a service
- `launchctl unload /path/to/plist` — unload a service
- `launchctl list` — list running services
- `launchctl start <label>` — start a service
- `launchctl stop <label>` — stop a service

This communicates with launchd via XPC/Mach IPC through the bootstrap port.

---

## Summary: Build Order

```
[Fix networking] — immediate, blocks everything
       ↓
[notifyd] — small, exercises XPC, wide impact
       ↓
[syslogd] — real logging, makes debugging everything else easier
       ↓
[IOKitUser] → [SystemConfiguration.framework] → [configd] → [bootp/DHCP]
       ↓                                                        ↓
[mDNSResponder] — real DNS, replaces getaddrinfo hack    [PAM + getty] (parallel)
       ↓                                                        ↓
[launchctl] — service management CLI                     [sudo with PAM]
```

## Estimated Effort

| Item | Sessions | Complexity |
|------|----------|-----------|
| Fix networking | <1 | LOW — likely timing issue |
| notifyd | 1-2 | MEDIUM |
| syslogd | 1-2 | MEDIUM |
| IOKitUser | 2-3 | HIGH — kernel/userland interface |
| SystemConfiguration | 3-5 | HIGH — 71 files, 64K lines |
| configd | 2-3 | HIGH — daemon + plugins |
| bootp/DHCP | 2-3 | MEDIUM |
| PAM | 1-2 | MEDIUM |
| getty | <1 | LOW |
| mDNSResponder | 2-3 | HIGH |
| launchctl | 1-2 | MEDIUM |

**Total: ~15-25 sessions.** But the payoff is enormous — after this, Panthera has real Darwin networking with DHCP, real DNS with caching, real logging, real authentication, and real service management.

## Rules

- Read `OS_BUILD_ROADMAP.md` foundation rules
- Don't remove working infrastructure until the replacement is verified
- Keep netbringup as fallback until configd is proven
- Keep standalone getaddrinfo until mDNSResponder works
- Build each daemon independently — if one fails to build, skip and move to the next
- Every daemon gets a launchd plist and a build script
- `tools/audit_package.sh` on every binary
- Boot test after each daemon is added
- No deferred work per daemon — each must work when staged
