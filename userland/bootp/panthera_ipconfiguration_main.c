/*
 * Standalone Panthera launcher for Apple's IPConfiguration plugin.
 *
 * Darwin normally loads IPConfiguration inside configd's plugin runtime.  While
 * Panthera's full configd.tproj plugin loader is still being brought up, keep
 * the Apple bootp/IPConfiguration source as the implementation and provide only
 * the minimal process entrypoint needed to exercise load/start/prime.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#define PANTHERA_IPCONFIGURATION_BUNDLE_PATH "/System/Library/SystemConfiguration/IPConfiguration.bundle"

extern void load(CFBundleRef bundle, Boolean bundleVerbose);
extern void start(const char *bundleName, const char *bundleDir);
extern void prime(void);

static bool
panthera_trace_enabled(void)
{
	const char *value = getenv("PANTHERA_IPCONFIGURATION_TRACE");
	return (value != NULL && value[0] != '\0' &&
	    __builtin_strcmp(value, "0") != 0);
}

static void
panthera_write_console(const char *msg)
{
	(void)write(STDERR_FILENO, msg, __builtin_strlen(msg));
}

static void
panthera_trace_console(const char *msg)
{
	if (panthera_trace_enabled()) {
		panthera_write_console(msg);
	}
}

static void
panthera_configd_preflight(void)
{
	for (int i = 0; i < 100; i++) {
		SCDynamicStoreRef store = SCDynamicStoreCreate(NULL, CFSTR("Panthera IPConfiguration preflight"), NULL, NULL);
		if (store != NULL) {
			CFRelease(store);
			panthera_trace_console("PANTHERA:IPConfiguration configd ready\n");
			return;
		}
		usleep(10 * 1000);
	}
	panthera_write_console("PANTHERA:IPConfiguration configd unavailable on ordered start\n");
}

static CFBundleRef
panthera_copy_ipconfiguration_bundle(void)
{
	CFURLRef url;
	CFBundleRef bundle = NULL;
	const UInt8 *path = (const UInt8 *)PANTHERA_IPCONFIGURATION_BUNDLE_PATH;

	panthera_trace_console("PANTHERA:IPConfiguration bundle url create enter\n");
	url = CFURLCreateFromFileSystemRepresentation(NULL, path,
	    (CFIndex)__builtin_strlen((const char *)path), TRUE);
	if (url == NULL) {
		panthera_write_console("PANTHERA:IPConfiguration bundle url create failed\n");
		return NULL;
	}
	panthera_trace_console("PANTHERA:IPConfiguration bundle url create ok\n");
	panthera_trace_console("PANTHERA:IPConfiguration bundle create enter\n");
	bundle = CFBundleCreate(NULL, url);
	CFRelease(url);
	if (bundle == NULL) {
		panthera_write_console("PANTHERA:IPConfiguration bundle create returned null\n");
	}
	return bundle;
}

int
main(int argc, char **argv)
{
	CFBundleRef bundle;
	bool verbose = false;

	for (int i = 1; i < argc; i++) {
		if (__builtin_strcmp(argv[i], "-v") == 0 ||
		    __builtin_strcmp(argv[i], "--verbose") == 0) {
			verbose = true;
		}
	}

	panthera_trace_console("PANTHERA:IPConfiguration launcher start\n");
	panthera_configd_preflight();
	bundle = panthera_copy_ipconfiguration_bundle();
	if (bundle == NULL) {
		panthera_write_console("PANTHERA:IPConfiguration bundle create failed\n");
		return 2;
	}
	panthera_trace_console("PANTHERA:IPConfiguration bundle create ok\n");
	panthera_trace_console("PANTHERA:IPConfiguration load call enter\n");
	load(bundle, verbose);
	CFRelease(bundle);
	panthera_trace_console("PANTHERA:IPConfiguration load returned\n");
	start("IPConfiguration", "/System/Library/SystemConfiguration/IPConfiguration.bundle");
	panthera_trace_console("PANTHERA:IPConfiguration start returned\n");
	prime();
	panthera_trace_console("PANTHERA:IPConfiguration prime queued\n");
	panthera_trace_console("PANTHERA:IPConfiguration runloop enter\n");
	CFRunLoopRun();
	panthera_trace_console("PANTHERA:IPConfiguration runloop returned\n");
	return 1;
}
