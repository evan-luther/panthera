#include <IOKit/pci/IOPCIPrivate.h>
#include "AppleVTD.h"

#if ACPI_SUPPORT

IOPCIMessagedInterruptController * gIOPCIMessagedInterruptController = NULL;

IOReturn
IOPCISetMSIInterrupt(uint32_t vector, uint32_t count, uint32_t *msiData)
{
	(void)vector;
	(void)count;
	(void)msiData;
	return kIOReturnUnsupported;
}

void
AppleVTD::install(IOWorkLoop *wl, uint32_t flags,
    IOService *provider, const OSData *data,
    IOPCIMessagedInterruptController *messagedInterruptController)
{
	(void)wl;
	(void)flags;
	(void)provider;
	(void)data;
	gIOPCIMessagedInterruptController = messagedInterruptController;
}

void
AppleVTD::installInterrupts(void)
{
}

void
AppleVTD::adjustDevice(IOService *device)
{
	(void)device;
}

void
AppleVTD::removeDevice(IOService *device)
{
	(void)device;
}

void
AppleVTD::relocateDevice(IOService *device, bool paused)
{
	(void)device;
	(void)paused;
}

void
AppleVTD::addMemoryRange(IOPhysicalAddress start, IOPhysicalLength length)
{
	(void)start;
	(void)length;
}

#endif
