/*
 * Minimal CrashReporterClient shim for Panthera pthread bring-up.
 * This is only to satisfy compile-time references from os/crashlog_private.h.
 */

#ifndef _PANTHERA_CRASH_REPORTER_CLIENT_H
#define _PANTHERA_CRASH_REPORTER_CLIENT_H

#include <stdint.h>

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

#define CRSetCrashLogMessage(msg) ((void)(msg))

#endif /* _PANTHERA_CRASH_REPORTER_CLIENT_H */
