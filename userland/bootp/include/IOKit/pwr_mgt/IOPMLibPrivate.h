#ifndef PANTHERA_IOPMLIBPRIVATE_H
#define PANTHERA_IOPMLIBPRIVATE_H

#include <IOKit/pwr_mgt/IOPMLib.h>

typedef void *IOPMConnection;
typedef uint32_t IOPMConnectionMessageToken;
typedef uint32_t IOPMSystemPowerStateCapabilities;
typedef void (*IOPMConnectionNotification)(void *param,
					   IOPMConnection connection,
					   IOPMConnectionMessageToken token,
					   IOPMSystemPowerStateCapabilities capabilities);

enum {
	kIOPMEarlyWakeNotification = 0x00000001,
	kIOPMCapabilityNetwork = 0x00000002,
	kIOPMCapabilityCPU = 0x00000004,
};

static inline IOReturn
IOPMConnectionCreate(CFStringRef name,
		     IOPMSystemPowerStateCapabilities interests,
		     IOPMConnection *connection)
{
	(void)name;
	(void)interests;
	if (connection != NULL) {
		*connection = NULL;
	}
	return kIOReturnUnsupported;
}

static inline IOReturn
IOPMConnectionSetNotification(IOPMConnection connection,
			      void *param,
			      IOPMConnectionNotification notification)
{
	(void)connection;
	(void)param;
	(void)notification;
	return kIOReturnUnsupported;
}

static inline IOReturn
IOPMConnectionSetDispatchQueue(IOPMConnection connection,
			       dispatch_queue_t queue)
{
	(void)connection;
	(void)queue;
	return kIOReturnUnsupported;
}

static inline IOReturn
IOPMConnectionAcknowledgeEvent(IOPMConnection connection,
			       IOPMConnectionMessageToken token)
{
	(void)connection;
	(void)token;
	return kIOReturnSuccess;
}

static inline void
IOPMConnectionRelease(IOPMConnection connection)
{
	(void)connection;
}

#endif /* PANTHERA_IOPMLIBPRIVATE_H */
