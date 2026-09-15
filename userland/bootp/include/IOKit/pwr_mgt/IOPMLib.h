#ifndef PANTHERA_IOPMLIB_H
#define PANTHERA_IOPMLIB_H

#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOTypes.h>

typedef struct __IONotificationPort *IONotificationPortRef;
typedef void (*IOServiceInterestCallback)(void *refcon,
					  io_service_t service,
					  natural_t messageType,
					  void *messageArgument);

static inline io_connect_t
IORegisterForSystemPower(void *refcon,
			 IONotificationPortRef *thePortRef,
			 IOServiceInterestCallback callback,
			 io_object_t *notifier)
{
	(void)refcon;
	(void)callback;
	if (thePortRef != NULL) {
		*thePortRef = NULL;
	}
	if (notifier != NULL) {
		*notifier = 0;
	}
	return 0;
}

static inline void
IONotificationPortSetDispatchQueue(IONotificationPortRef notify,
				   dispatch_queue_t queue)
{
	(void)notify;
	(void)queue;
}

static inline IOReturn
IOAllowPowerChange(io_connect_t kernelPort, long notificationID)
{
	(void)kernelPort;
	(void)notificationID;
	return kIOReturnSuccess;
}

static inline CFArrayRef
IOPMCopyScheduledPowerEvents(void)
{
	return NULL;
}

static inline IOReturn
IOPMCancelScheduledPowerEvent(CFDateRef time_to_wake,
			      CFStringRef my_id,
			      CFStringRef type)
{
	(void)time_to_wake;
	(void)my_id;
	(void)type;
	return kIOReturnUnsupported;
}

#ifndef kIOPMAutoWake
#define kIOPMAutoWake "wake"
#endif

#ifndef kIOPMPowerEventAppNameKey
#define kIOPMPowerEventAppNameKey "appName"
#endif

#ifndef kIOPMPowerEventTimeKey
#define kIOPMPowerEventTimeKey "time"
#endif
#ifndef kIOPMPowerEventLeewayKey
#define kIOPMPowerEventLeewayKey "leeway"
#endif

#endif /* PANTHERA_IOPMLIB_H */
