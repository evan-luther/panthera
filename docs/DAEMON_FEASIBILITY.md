# Panthera — Darwin Daemon Feasibility Assessment

## syslogd — RECOMMENDED, BUILD FROM SCRATCH

**Apple source:** syslog-385 (APSL, not yet downloaded)
**Bootstrap service:** `com.apple.system.logger`
**Feasibility:** High — can be wire-compatible without CoreFoundation

A minimal Panthera syslogd (~300-500 lines C) that:
- Registers `com.apple.system.logger` via bootstrap_check_in
- Receives ASL messages on its Mach port
- Writes to `/var/log/system.log`
- Accepts legacy BSD syslog via `/var/run/syslog` Unix socket

Any program calling `syslog()` or `asl_log()` works unchanged — libc routes to the Mach port. The ASL wire format is documented in Apple's open-source asl.h headers.

**Why it matters:** First real bootstrap client. Centralized logging for all daemons. Useful for debugging everything else.

**Dependencies:** Mach IPC (have it), bootstrap check-in (have it), file I/O. No CoreFoundation.

---

## configd — NOT FEASIBLE WITHOUT COREFOUNDATION

**Apple source:** `src/configd-1296.40.6/` (168K lines, 7.9MB)
**Bootstrap service:** `com.apple.SystemConfiguration.configd`
**Feasibility:** Low — requires CoreFoundation + Objective-C runtime + SystemConfiguration framework

configd is not just "configure the network." It's a dynamic key-value store (SCDynamicStore) where daemons publish and subscribe to system state. Programs call:
```c
SCDynamicStoreCopyValue(store, CFSTR("State:/Network/Interface/en0/IPv4"))
```
to read network config and get notified on changes. Replicating this API requires the full SystemConfiguration framework (140+ source files) which depends on CoreFoundation.

**A from-scratch replacement that just configures interfaces is possible but would NOT be API-compatible.** Nothing calling SCDynamicStore would function. It would just be a better netbringup.

### What Panthera actually needs for networking

The current problem: `netbringup` is a one-shot script that configures en0 statically. It works but:
- No DHCP
- No dynamic reconfiguration
- Hardcoded to QEMU's 10.0.2.x subnet
- No DNS configuration beyond static /etc/resolv.conf

**Practical solutions (no CoreFoundation needed):**
1. A Panthera network daemon that handles interface bringup, static config from a plist, and basic DHCP client
2. Port a standalone DHCP client (dhclient from ISC, or udhcpc from BusyBox)
3. Keep static config for QEMU, add DHCP for real hardware later

---

## opendirectoryd — CLOSED SOURCE, NOT NEEDED

**Apple source:** Not available (closed source)
**Feasibility:** None

Panthera already has working user/group resolution via:
- `/etc/passwd` and `/etc/group` (flat files)
- Libinfo (libsystem_info.dylib) providing `getpwnam()`, `getpwuid()`, `getgrnam()`

opendirectoryd is the backing daemon for LDAP, Active Directory, and network account lookups. Not needed for a standalone Darwin server.

---

## Priority Order

1. **syslogd** — build from scratch, exercises bootstrap, gives real logging
2. **Network daemon** — replace netbringup with a proper plist-driven daemon (static config first, DHCP later)
3. **Plist key cleanup** — replace hardcoded label checks with SessionCreate/StartDelay plist keys
4. **SSH verification** — Dropbear is built and staged but not boot-tested end-to-end

## CoreFoundation: The Gating Dependency

configd, notifyd, and most Apple system daemons require CoreFoundation. Apple publishes CF as open source (swift-corelibs-foundation), but porting it to Panthera's sysroot is a project-sized effort:
- ~200K lines of C/Objective-C
- Needs Objective-C runtime (libobjc)
- Needs libdispatch (have it) and libxml2
- Needs ICU for Unicode

If CoreFoundation is ever ported, configd and notifyd become feasible. Until then, Panthera should build lightweight from-scratch replacements for essential services.
