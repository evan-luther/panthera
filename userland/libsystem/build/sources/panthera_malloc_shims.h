/*
 * Panthera libsystem_malloc build shims
 *
 * Force-included before all compilation units. Provides defines and stubs
 * for private Apple interfaces that libmalloc needs but that don't exist
 * as files in the sysroot (or need extra configuration).
 */

#ifndef _PANTHERA_MALLOC_SHIMS_H_
#define _PANTHERA_MALLOC_SHIMS_H_

#include <stdint.h>

/* ---- DARWINTEST mode: gets us dtrace stub macros from dtrace.h ---- */
#define DARWINTEST 1

/* ---- Platform identification ---- */
#ifndef TARGET_OS_DRIVERKIT
#define TARGET_OS_DRIVERKIT 0
#endif
#ifndef MALLOC_TARGET_DK_OSX
#define MALLOC_TARGET_DK_OSX 0
#endif
#ifndef TARGET_OS_EXCLAVECORE
#define TARGET_OS_EXCLAVECORE 0
#endif
#ifndef TARGET_OS_EXCLAVEKIT
#define TARGET_OS_EXCLAVEKIT 0
#endif

/* ---- Prevent SDK os/availability.h from overriding our sysroot stubs ---- */
#ifndef __OS_AVAILABILITY__
#define __OS_AVAILABILITY__
#endif

/* ---- Prevent SDK AvailabilityInternal.h ---- */
#ifndef __AVAILABILITY_INTERNAL__
#define __AVAILABILITY_INTERNAL__
#endif

/* ---- Swift interop macros (used by SDK mach headers) ---- */
#ifndef __swift_nonisolated_unsafe
#define __swift_nonisolated_unsafe
#endif

/* ---- _os_set_crash_log_cause_and_message ---- */
/* crashlog_private.h in sysroot may declare this but as extern */
#ifndef _os_set_crash_log_cause_and_message
#define _os_set_crash_log_cause_and_message(cause, msg) ((void)0)
#endif

/* ---- CrashReporterClient glue for os/crashlog_private.h ---- */
struct crashreporter_annotations_t {
	uint64_t version;
	uint64_t message;
	uint64_t signature_string;
	uint64_t backtrace;
	uint64_t message2;
	uint64_t thread;
	uint64_t dialog_mode;
	uint64_t abort_cause;
};

extern struct crashreporter_annotations_t gCRAnnotations;

#ifndef CRSetCrashLogMessage
#define CRSetCrashLogMessage(msg) ((void)(msg))
#endif

/* ---- ASL level definitions ---- */
#ifndef ASL_LEVEL_ERR
#define ASL_LEVEL_EMERG   0
#define ASL_LEVEL_ALERT   1
#define ASL_LEVEL_CRIT    2
#define ASL_LEVEL_ERR     3
#define ASL_LEVEL_WARNING 4
#define ASL_LEVEL_NOTICE  5
#define ASL_LEVEL_INFO    6
#define ASL_LEVEL_DEBUG   7
#endif

/* ---- Reporting flags used by malloc_printf.c ---- */
#ifndef MALLOC_REPORT_NOPREFIX
#define MALLOC_REPORT_NOPREFIX    0x100
#define MALLOC_REPORT_BACKTRACE   0x200
#define MALLOC_REPORT_NOWRITE     0x400
#define MALLOC_REPORT_NOLOG       0x800
#define MALLOC_REPORT_CRASH       0x1000
#endif

/* os_unfair_lock options are defined in os/lock_private.h as an enum */

/* ---- NOTE_MEMORYSTATUS defines (for memory pressure) ---- */
#ifndef NOTE_MEMORYSTATUS_PRESSURE_WARN
#define NOTE_MEMORYSTATUS_PRESSURE_WARN    0x00000001
#define NOTE_MEMORYSTATUS_PRESSURE_NORMAL  0x00000002
#define NOTE_MEMORYSTATUS_PRESSURE_CRITICAL 0x00000004
#define NOTE_MEMORYSTATUS_PROC_LIMIT_WARN  0x00000010
#define NOTE_MEMORYSTATUS_PROC_LIMIT_CRITICAL 0x00000020
#define NOTE_MEMORYSTATUS_MSL_STATUS       0x00000100
#endif

/* ---- __options_decl support ---- */
#ifndef __options_decl
#define __options_decl(name, type, ...) typedef type name; enum __VA_ARGS__
#endif

/*
 * libmalloc-474 still uses the historical one-argument mach_task_is_self()
 * form in a couple of places. Force-include mach/task.h first, then provide
 * a compatibility wrapper that queries the newer two-argument MIG routine.
 */
#include <mach/task.h>
static inline boolean_t
panthera_mach_task_is_self(task_name_t task)
{
	boolean_t is_self = FALSE;
	return mach_task_is_self(task, &is_self) == KERN_SUCCESS && is_self;
}
#undef mach_task_is_self
#define mach_task_is_self(task) panthera_mach_task_is_self(task)

/* stack_logging_mode_type is defined in stack_logging.h */

/* os_atomic types are handled by os/atomic.h which includes <stdatomic.h> */

/* ---- OSQueueHead (used by nano_zone.h) ---- */
/* libkern/OSAtomic.h in sysroot doesn't include OSAtomicQueue.h */
#ifndef _OSATOMICQUEUE_H_
#include <libkern/OSAtomicQueue.h>
#endif

/* ---- Ensure xzone empty files don't cause issues ---- */
#ifndef _XZONE_MALLOC_H_
#define _XZONE_MALLOC_H_
#endif
#ifndef _XZONE_INLINE_INTERNAL_H_
#define _XZONE_INLINE_INTERNAL_H_
#endif

#endif /* _PANTHERA_MALLOC_SHIMS_H_ */
