/*
 * spawn_private.h — Stub for Apple's private posix_spawn extensions.
 */
#ifndef _SPAWN_PRIVATE_H
#define _SPAWN_PRIVATE_H

#include <spawn.h>

/* Process type constants */
#ifndef POSIX_SPAWN_PROC_TYPE_DAEMON_INTERACTIVE
#define POSIX_SPAWN_PROC_TYPE_DAEMON_INTERACTIVE 0x600
#endif
#ifndef POSIX_SPAWN_PROC_TYPE_DAEMON_STANDARD
#define POSIX_SPAWN_PROC_TYPE_DAEMON_STANDARD 0x601
#endif
#ifndef POSIX_SPAWN_PROC_TYPE_DAEMON_ADAPTIVE
#define POSIX_SPAWN_PROC_TYPE_DAEMON_ADAPTIVE 0x640
#endif
#ifndef POSIX_SPAWN_PROC_TYPE_DAEMON_BACKGROUND
#define POSIX_SPAWN_PROC_TYPE_DAEMON_BACKGROUND 0x641
#endif
#ifndef POSIX_SPAWN_PROC_TYPE_APP_DEFAULT
#define POSIX_SPAWN_PROC_TYPE_APP_DEFAULT 0x100
#endif
#ifndef POSIX_SPAWN_IOS_INTERACTIVE
#define POSIX_SPAWN_IOS_INTERACTIVE 0
#endif

/* Private spawn attribute setters — stub as no-ops */
static inline int posix_spawnattr_setprocesstype_np(posix_spawnattr_t *attr, int type) { (void)attr; (void)type; return 0; }
static inline int posix_spawnattr_setcpumonitor(posix_spawnattr_t *attr, uint64_t pct, uint64_t interval) { (void)attr; (void)pct; (void)interval; return 0; }
static inline int posix_spawnattr_setcpumonitor_default(posix_spawnattr_t *attr) { (void)attr; return 0; }
static inline int posix_spawnattr_setjetsam(posix_spawnattr_t *attr, short flags, int priority, int memlimit) { (void)attr; (void)flags; (void)priority; (void)memlimit; return 0; }
static inline int posix_spawnattr_setjetsam_ext(posix_spawnattr_t *attr, short flags, int priority, int memlimit_active, int memlimit_inactive) { (void)attr; (void)flags; (void)priority; (void)memlimit_active; (void)memlimit_inactive; return 0; }
static inline int posix_spawnattr_set_importancewatch_port_np(posix_spawnattr_t *attr, int count, mach_port_t *ports) { (void)attr; (void)count; (void)ports; return 0; }
static inline int posix_spawnattr_set_darwin_role_np(posix_spawnattr_t *attr, uint64_t role) { (void)attr; (void)role; return 0; }

/* POSIX_SPAWN_START_SUSPENDED */
#ifndef POSIX_SPAWN_START_SUSPENDED
#define POSIX_SPAWN_START_SUSPENDED 0x0080
#endif

/* Process type flags */
#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

#endif /* _SPAWN_PRIVATE_H */
