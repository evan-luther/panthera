/*
 * Standalone Panthera launcher for Apple's KernelEventMonitor plugin.
 *
 * This keeps the Apple plugin source intact while Panthera's configd is still
 * a minimal SCDynamicStore server and cannot load CFBundle plugins itself.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <SystemConfiguration/SCDPlugin.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void load_KernelEventMonitor(CFBundleRef bundle, Boolean bundleVerbose);
extern void prime_KernelEventMonitor(void);
extern mach_port_t bootstrap_port;
typedef char panthera_bootstrap_name_t[128];
extern kern_return_t bootstrap_look_up(mach_port_t bp,
    const panthera_bootstrap_name_t service_name, mach_port_t *sp);

extern void __CFInitialize(void);

#ifndef PANTHERA_CONFIGD_SERVICE
#define PANTHERA_CONFIGD_SERVICE "com.apple.SystemConfiguration.configd"
#endif

static void
write_console(const char *message)
{
	(void)write(STDERR_FILENO, message, strlen(message));
}

static int
trace_enabled(void)
{
	const char *trace = getenv("PANTHERA_CONFIGD_KERNEL_EVENT_MONITOR_TRACE");
	return (trace != NULL && trace[0] != '\0' && strcmp(trace, "0") != 0);
}

static void
trace_console(const char *message)
{
	if (trace_enabled()) {
		write_console(message);
	}
}

static void
wait_for_configd(void)
{
	mach_port_t service = MACH_PORT_NULL;

	for (;;) {
		kern_return_t kr = bootstrap_look_up(bootstrap_port,
		    PANTHERA_CONFIGD_SERVICE, &service);
		if (kr == KERN_SUCCESS) {
			if (service != MACH_PORT_NULL) {
				(void)mach_port_deallocate(mach_task_self(), service);
			}
			trace_console("PANTHERA:KernelEventMonitor configd ready\n");
			return;
		}
		usleep(10 * 1000);
	}
}

static void
preflight_dynamic_store(void)
{
	trace_console("PANTHERA:KernelEventMonitor preflight store enter\n");
	for (int i = 0; i < 100; i++) {
		SCDynamicStoreRef preflight = SCDynamicStoreCreate(NULL,
		    CFSTR("Panthera KernelEventMonitor preflight"),
		    NULL,
		    NULL);
		if (preflight != NULL) {
			CFRelease(preflight);
			trace_console("PANTHERA:KernelEventMonitor preflight store ok\n");
			return;
		}
		usleep(10 * 1000);
	}
	write_console("PANTHERA:KernelEventMonitor preflight store failed\n");
}

int
main(int argc, char * const argv[])
{
	Boolean verbose = (argc > 1) ? TRUE : FALSE;

	__CFInitialize();
	trace_console("PANTHERA:KernelEventMonitor launcher start\n");
	wait_for_configd();
	preflight_dynamic_store();
	load_KernelEventMonitor(NULL, verbose);
	trace_console("PANTHERA:KernelEventMonitor load returned\n");
	prime_KernelEventMonitor();
	trace_console("PANTHERA:KernelEventMonitor prime queued\n");
	dispatch_main();
	return 0;
}
