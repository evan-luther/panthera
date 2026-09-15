/*
 * panthera_launchd_config.h — Feature flags for Panthera launchd build.
 *
 * This header is force-included (-include) before all Apple source files.
 * It controls which macOS-specific subsystems are compiled.
 */
#ifndef PANTHERA_LAUNCHD_CONFIG_H
#define PANTHERA_LAUNCHD_CONFIG_H

/* Preempt upstream config.h using its include guard */
#ifndef __CONFIG_H__
#define __CONFIG_H__
#endif

/* Panthera identifier — used for conditional compilation */
#define PANTHERA 1

/* Subsystems to strip from core.c / runtime.c */
#define HAVE_SANDBOX 0
#define HAVE_JETSAM 0
#define HAVE_AUDIT 0
#define HAVE_QUARANTINE 0
#define HAVE_RESPONSIBILITY 0
#define HAVE_XPC_DOMAINS 0
#define HAVE_XPC_EVENTS 0
#define HAVE_EMBEDDED 0
#define HAVE_SYSTEMSTATS 0
#define HAVE_LIBAUDITD 0

/* Override config.h's __has_include checks */
#undef HAVE_SANDBOX
#define HAVE_SANDBOX 0
#undef HAVE_QUARANTINE
#define HAVE_QUARANTINE 0
#undef HAVE_RESPONSIBILITY
#define HAVE_RESPONSIBILITY 0
#undef HAVE_SYSTEMSTATS
#define HAVE_SYSTEMSTATS 0

/* Target is never embedded */
#ifndef TARGET_OS_EMBEDDED
#define TARGET_OS_EMBEDDED 0
#endif

/* Suppress __OS_COMPILETIME_ASSERT__ if not available */
#ifndef __OS_COMPILETIME_ASSERT__
#define __OS_COMPILETIME_ASSERT__(e) _Static_assert(e, #e)
#endif

/* Suppress compile-time spawn_private constants */
#ifndef POSIX_SPAWN_IOS_INTERACTIVE
#define POSIX_SPAWN_IOS_INTERACTIVE 0
#endif
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

/* XPC Jetsam band constants */
#ifndef XPC_JETSAM_PRIORITY_RESERVED
#define XPC_JETSAM_PRIORITY_RESERVED 0
#endif
#ifndef XPC_JETSAM_BAND_LAST
#define XPC_JETSAM_BAND_LAST 0
#endif
#ifndef XPC_LPI_VERSION
#define XPC_LPI_VERSION 0
#endif

/* Launchd path constants */
#ifndef LAUNCHD_DB_PREFIX
#define LAUNCHD_DB_PREFIX "/var/db/launchd.db"
#endif
#ifndef LAUNCHD_LOG_PREFIX
#define LAUNCHD_LOG_PREFIX "/var/log"
#endif
#ifndef LAUNCHD_SOCK_PREFIX
#define LAUNCHD_SOCK_PREFIX "/var/run/launchd"
#endif
#ifndef LAUNCHD_TRUSTED_FD_ENV
#define LAUNCHD_TRUSTED_FD_ENV "LAUNCHD_TRUSTED_FD"
#endif

/* gL1CacheEnabled — Apple-private, stub */
#ifndef gL1CacheEnabled
static int _panthera_gL1CacheEnabled __attribute__((unused)) = 0;
#define gL1CacheEnabled _panthera_gL1CacheEnabled
#endif

/* Missing setiopolicy_np */
#ifndef IOPOL_TYPE_DISK
#define IOPOL_TYPE_DISK 1
#endif
#ifndef IOPOL_SCOPE_THREAD
#define IOPOL_SCOPE_THREAD 2
#endif
#ifndef IOPOL_THROTTLE
#define IOPOL_THROTTLE 3
#endif

/* XPC export macros — needed by SDK's servers/bootstrap.h */
#ifndef XPC_EXPORT
#define XPC_EXPORT extern
#endif
#ifndef XPC_WARN_RESULT
#define XPC_WARN_RESULT
#endif
#ifndef XPC_NONNULL3
#define XPC_NONNULL3
#endif
#ifndef XPC_NONNULL5
#define XPC_NONNULL5
#endif
#ifndef XPC_NONNULL4
#define XPC_NONNULL4
#endif
#ifndef XPC_NONNULL1
#define XPC_NONNULL1
#endif
#ifndef XPC_NONNULL2
#define XPC_NONNULL2
#endif

/* Bootstrap property flags */
#ifndef BOOTSTRAP_PROPERTY_XPC_SINGLETON
#define BOOTSTRAP_PROPERTY_XPC_SINGLETON 0x4000
#endif
#ifndef BOOTSTRAP_PROPERTY_XPC_DOMAIN
#define BOOTSTRAP_PROPERTY_XPC_DOMAIN 0x8000
#endif

/* DBG_FUNC_START/END for ktrace */
#ifndef DBG_FUNC_START
#define DBG_FUNC_START 1
#endif
#ifndef DBG_FUNC_END
#define DBG_FUNC_END 2
#endif

/* Private proc_info structures not in public SDK */
#include <stdint.h>
#include <sys/types.h>
#include <stddef.h>
#ifndef PROC_PIDUNIQIDENTIFIERINFO
#define PROC_PIDUNIQIDENTIFIERINFO 17
struct proc_uniqidentifierinfo {
    uint8_t  p_uuid[16];
    uint64_t p_uniqueid;
    uint64_t p_puniqueid;
    int32_t  p_reserve2;
    int32_t  p_reserve3;
    int32_t  p_reserve4;
};
#define PROC_PIDUNIQIDENTIFIERINFO_SIZE (sizeof(struct proc_uniqidentifierinfo))
#endif

/* rusage_info_v1 is in the SDK's sys/resource.h — no stub needed */

/*
 * Sandbox stubs — sandbox_check and friends used throughout core.c
 * without #if HAVE_SANDBOX guards. Define them as no-ops.
 */
#if !HAVE_SANDBOX
#ifndef sandbox_check
static inline int sandbox_check(pid_t pid, const char *op, ...) { (void)pid; (void)op; return 0; }
#endif
#ifndef SANDBOX_FILTER_NONE
#define SANDBOX_FILTER_NONE 0
#endif
#ifndef SANDBOX_FILTER_PATH
#define SANDBOX_FILTER_PATH 1
#endif
#ifndef SANDBOX_FILTER_LOCAL_NAME
#define SANDBOX_FILTER_LOCAL_NAME 2
#endif
#ifndef SANDBOX_FILTER_GLOBAL_NAME
#define SANDBOX_FILTER_GLOBAL_NAME 3
#endif
#ifndef SANDBOX_NAMED
#define SANDBOX_NAMED 0x0001
#endif
#ifndef TASK_SEATBELT_PORT
#define TASK_SEATBELT_PORT 7
#endif
#endif /* !HAVE_SANDBOX */

/*
 * Quarantine stubs — qtn_proc types used in job_start_child
 */
#if !HAVE_QUARANTINE
typedef void *qtn_proc_t;
static inline qtn_proc_t qtn_proc_alloc(void) { return NULL; }
static inline int qtn_proc_init_with_data(qtn_proc_t q, void *d, size_t s) { (void)q; (void)d; (void)s; return 0; }
static inline int qtn_proc_apply_to_self(qtn_proc_t q) { (void)q; return 0; }
static inline void qtn_proc_free(qtn_proc_t q) { (void)q; }
#endif /* !HAVE_QUARANTINE */

/*
 * Jetsam stubs
 */
#if !HAVE_JETSAM
#ifndef DEFAULT_JETSAM_PRIORITY
#define DEFAULT_JETSAM_PRIORITY 0
#endif
#ifndef DEFAULT_JETSAM_DAEMON_HIGHWATERMARK
#define DEFAULT_JETSAM_DAEMON_HIGHWATERMARK 5
#endif
#endif /* !HAVE_JETSAM */

/* spawn process type constants */
#ifndef POSIX_SPAWN_PROC_TYPE_APP_TAL
#define POSIX_SPAWN_PROC_TYPE_APP_TAL 0x101
#endif

/*
 * XPC service/event constants used in core.c
 */
#ifndef XPC_SERVICE_ENV_ATTACHED
#define XPC_SERVICE_ENV_ATTACHED "XPC_SERVICE_ENV_ATTACHED"
#endif
#ifndef XPC_SERVICE_RENDEZVOUS_TOKEN
#define XPC_SERVICE_RENDEZVOUS_TOKEN "XPC_SERVICE_RENDEZVOUS_TOKEN"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_STREAM
#define XPC_EVENT_ROUTINE_KEY_STREAM "XPC_EVENT_ROUTINE_KEY_STREAM"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_TOKEN
#define XPC_EVENT_ROUTINE_KEY_TOKEN "XPC_EVENT_ROUTINE_KEY_TOKEN"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_NAME
#define XPC_EVENT_ROUTINE_KEY_NAME "XPC_EVENT_ROUTINE_KEY_NAME"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_ENTITLEMENTS
#define XPC_EVENT_ROUTINE_KEY_ENTITLEMENTS "XPC_EVENT_ROUTINE_KEY_ENTITLEMENTS"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_EVENT
#define XPC_EVENT_ROUTINE_KEY_EVENT "XPC_EVENT_ROUTINE_KEY_EVENT"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_FLAGS
#define XPC_EVENT_ROUTINE_KEY_FLAGS "XPC_EVENT_ROUTINE_KEY_FLAGS"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_STATE
#define XPC_EVENT_ROUTINE_KEY_STATE "XPC_EVENT_ROUTINE_KEY_STATE"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_OP
#define XPC_EVENT_ROUTINE_KEY_OP "XPC_EVENT_ROUTINE_KEY_OP"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_ERROR
#define XPC_EVENT_ROUTINE_KEY_ERROR "XPC_EVENT_ROUTINE_KEY_ERROR"
#endif
#ifndef XPC_EVENT_FLAG_ENTITLEMENTS
#define XPC_EVENT_FLAG_ENTITLEMENTS 0x1
#endif

/* XPC event routine key port/events */
#ifndef XPC_EVENT_ROUTINE_KEY_PORT
#define XPC_EVENT_ROUTINE_KEY_PORT "XPC_EVENT_ROUTINE_KEY_PORT"
#endif
#ifndef XPC_EVENT_ROUTINE_KEY_EVENTS
#define XPC_EVENT_ROUTINE_KEY_EVENTS "XPC_EVENT_ROUTINE_KEY_EVENTS"
#endif
/* XPC event operation codes */
#ifndef XPC_EVENT_GET_NAME
#define XPC_EVENT_GET_NAME 1
#endif
#ifndef XPC_EVENT_SET
#define XPC_EVENT_SET 2
#endif
#ifndef XPC_EVENT_COPY
#define XPC_EVENT_COPY 3
#endif
#ifndef XPC_EVENT_CHECK_IN
#define XPC_EVENT_CHECK_IN 4
#endif
#ifndef XPC_EVENT_LOOK_UP
#define XPC_EVENT_LOOK_UP 5
#endif
#ifndef XPC_EVENT_PROVIDER_CHECK_IN
#define XPC_EVENT_PROVIDER_CHECK_IN 6
#endif
#ifndef XPC_EVENT_PROVIDER_SET_STATE
#define XPC_EVENT_PROVIDER_SET_STATE 7
#endif
#ifndef XPC_EVENT_COPY_ENTITLEMENTS
#define XPC_EVENT_COPY_ENTITLEMENTS 8
#endif

/* XPC process routine keys */
#ifndef XPC_PROCESS_ROUTINE_KEY_LABEL
#define XPC_PROCESS_ROUTINE_KEY_LABEL "label"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_PRIORITY_BAND
#define XPC_PROCESS_ROUTINE_KEY_PRIORITY_BAND "priority-band"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_RCDATA
#define XPC_PROCESS_ROUTINE_KEY_RCDATA "rcdata"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_MEMORY_LIMIT
#define XPC_PROCESS_ROUTINE_KEY_MEMORY_LIMIT "memory-limit"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_NAME
#define XPC_PROCESS_ROUTINE_KEY_NAME "name"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_TYPE
#define XPC_PROCESS_ROUTINE_KEY_TYPE "type"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_NEW_INSTANCE_PORT
#define XPC_PROCESS_ROUTINE_KEY_NEW_INSTANCE_PORT "new-instance-port"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_HANDLE
#define XPC_PROCESS_ROUTINE_KEY_HANDLE "handle"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_PID
#define XPC_PROCESS_ROUTINE_KEY_PID "pid"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_ERROR
#define XPC_PROCESS_ROUTINE_KEY_ERROR "error"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_PATH
#define XPC_PROCESS_ROUTINE_KEY_PATH "path"
#endif
#ifndef XPC_PROCESS_ROUTINE_KEY_ARGV
#define XPC_PROCESS_ROUTINE_KEY_ARGV "argv"
#endif

/* XPC service type constants */
#ifndef XPC_SERVICE_TYPE_BUNDLED
#define XPC_SERVICE_TYPE_BUNDLED 1
#endif
#ifndef XPC_SERVICE_TYPE_LAUNCHD
#define XPC_SERVICE_TYPE_LAUNCHD 2
#endif
#ifndef XPC_SERVICE_TYPE_APP
#define XPC_SERVICE_TYPE_APP 3
#endif
#ifndef XPC_SERVICE_ENTITLEMENT_ATTACH
#define XPC_SERVICE_ENTITLEMENT_ATTACH "com.apple.private.xpc.launchd.service-attach"
#endif

/* XPC Jetsam band suspended */
#ifndef XPC_JETSAM_BAND_SUSPENDED
#define XPC_JETSAM_BAND_SUSPENDED 0
#endif

/* xpc_copy_entitlement_for_token stub */
#ifndef xpc_copy_entitlement_for_token
#define xpc_copy_entitlement_for_token(e, t) (NULL)
#endif

/* Error codes used by XPC domain/event functions */
#ifndef EXNOMEM
#define EXNOMEM 12 /* ENOMEM */
#endif
#ifndef EXINVAL
#define EXINVAL 22 /* EINVAL */
#endif
#ifndef EXSRCH
#define EXSRCH 3  /* ESRCH */
#endif

/* MIG event_name_t type */
#ifndef event_name_t
typedef char event_name_t[256];
#endif

/* proc_bsdinfowithuniqid — private proc_info structure.
 * Uses a byte array for pbsd since struct proc_bsdinfo may not be defined yet. */
#ifndef PROC_PIDT_BSDINFOWITHUNIQID
#define PROC_PIDT_BSDINFOWITHUNIQID 32
struct proc_bsdinfowithuniqid {
    char pbsd[1024]; /* opaque proc_bsdinfo */
    struct proc_uniqidentifierinfo p_uniqidentifier;
};
#define PROC_PIDT_BSDINFOWITHUNIQID_SIZE (sizeof(struct proc_bsdinfowithuniqid))
#endif

/* Panthera boot init — called from launchd_main before Apple init */
extern void panthera_boot_init(void);
/* Panthera job loading — scan LaunchDaemons plists after jobmgr_init */
extern void panthera_load_jobs(void);
/* Panthera job dispatch — start imported LaunchDaemons after runtime setup */
extern void panthera_dispatch_loaded_jobs(void);

#endif /* PANTHERA_LAUNCHD_CONFIG_H */
