#include <IOKit/usb/IOUSBLog.h>

#include "AppleUSBUHCI.h"

void
AppleUSBUHCI::PollInterrupts(IOUSBCompletionAction safeAction)
{
#pragma unused(safeAction)
    if (!_controllerAvailable || isInactive()) {
        return;
    }

    ProcessCompletedTransactions();
}

void
AppleUSBUHCI::InterruptHandler(OSObject *owner, IOInterruptEventSource *, int)
{
    AppleUSBUHCI *controller = OSDynamicCast(AppleUSBUHCI, owner);
    if (!controller || controller->isInactive() || !controller->_controllerAvailable) {
        return;
    }

    controller->PollInterrupts();
}

bool
AppleUSBUHCI::PrimaryInterruptFilter(OSObject *owner, IOFilterInterruptEventSource *)
{
    AppleUSBUHCI *controller = OSDynamicCast(AppleUSBUHCI, owner);
    if (!controller || controller->isInactive() || !controller->_controllerAvailable) {
        return false;
    }

    controller->_filterInterruptActive = true;
    bool result = controller->FilterInterrupt();
    controller->_filterInterruptActive = false;
    return result;
}

bool
AppleUSBUHCI::FilterInterrupt(void)
{
    UInt16 enabledInterrupts = ioRead16(kUHCI_INTR);
    UInt16 activeInterrupts = ioRead16(kUHCI_STS);

    if ((enabledInterrupts == 0xffff) ||
        (activeInterrupts == 0xffff)) {
        _controllerAvailable = false;
        return false;
    }

    activeInterrupts &= kUHCI_STS_MASK;
    if (!activeInterrupts) {
        return false;
    }

    IOLog("PANTHERA:UHCI filter sts=0x%x intr=0x%x\n",
          (unsigned)activeInterrupts,
          (unsigned)enabledInterrupts);

    // Ack the interrupt bits in primary context so the action handler can
    // safely drain completed transactions on the workloop.
    ioWrite16(kUHCI_STS, activeInterrupts);
    IOSync();

    return true;
}
