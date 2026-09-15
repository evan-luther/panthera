#include <IOKit/IOLib.h>
#include <IOKit/hid/IOHIDInterface.h>
#include <IOKit/hidsystem/IOHIDUsageTables.h>

#include "IOHIDEventService.h"

static bool pantheraUSBHIDLogsEnabledCompat(void)
{
    static bool initialized = false;
    static bool enabled = false;
    if (!initialized) {
        int value = 0;
        enabled = PE_parse_boot_argn("panthera_usbhid", &value, sizeof(value));
        initialized = true;
    }
    return enabled;
}

static bool pantheraReportContainsKey(const UInt8 *reportKeys, UInt8 key)
{
    for (unsigned index = 0; index < 6; ++index) {
        if (reportKeys[index] == key) {
            return true;
        }
    }
    return false;
}

static bool pantheraReportKeyAppearsEarlier(const UInt8 *reportKeys, unsigned index, UInt8 key)
{
    for (unsigned previousIndex = 0; previousIndex < index; ++previousIndex) {
        if (reportKeys[previousIndex] == key) {
            return true;
        }
    }
    return false;
}

class IOHIDEventDriver : public IOHIDEventService
{
    OSDeclareDefaultStructors(IOHIDEventDriver)

private:
    IOHIDInterface *_interface {nullptr};
    OSArray *_elements {nullptr};
    UInt8 _previousModifiers {0};
    UInt8 _previousKeys[6] {};

    static void handleInterruptReportThunk(
        OSObject *target,
        AbsoluteTime timestamp,
        IOMemoryDescriptor *report,
        IOHIDReportType type,
        UInt32 reportID,
        void *refcon);

    void handleInterruptReport(
        AbsoluteTime timestamp,
        IOMemoryDescriptor *report,
        IOHIDReportType type,
        UInt32 reportID,
        void *refcon);

public:
    bool handleStart(IOService *provider) APPLE_KEXT_OVERRIDE;
    void handleStop(IOService *provider) APPLE_KEXT_OVERRIDE;
    OSArray *getReportElements(void) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(IOHIDEventDriver, IOHIDEventService)

bool IOHIDEventDriver::handleStart(IOService *provider)
{
    bool opened = false;
    OSObject *bootProtocol = nullptr;
    UInt32 bootProtocolValue = 0;

    _interface = OSDynamicCast(IOHIDInterface, provider);
    if (!_interface) {
        return false;
    }

    _elements = _interface->createMatchingElements();
    if (!_elements) {
        return false;
    }

    if (!_interface->open(this, 0, &IOHIDEventDriver::handleInterruptReportThunk, nullptr)) {
        OSSafeReleaseNULL(_elements);
        return false;
    }
    opened = true;

    bootProtocol = _interface->copyProperty("BootProtocol");
    if (OSNumber *number = OSDynamicCast(OSNumber, bootProtocol)) {
        bootProtocolValue = number->unsigned32BitValue();
        setProperty("BootProtocol", bootProtocolValue);
    }

    bzero(_previousKeys, sizeof(_previousKeys));
    _previousModifiers = 0;

    if (pantheraUSBHIDLogsEnabledCompat()) {
        IOLog("PANTHERA:IOHIDEventDriverCompat handleStart provider=%s elements=%p count=%u bootProtocol=%u\n",
              provider->getName(),
              _elements,
              _elements ? (unsigned)_elements->getCount() : 0U,
              (unsigned)bootProtocolValue);
    }

    OSSafeReleaseNULL(bootProtocol);

    (void)opened;
    return true;
}

void IOHIDEventDriver::handleStop(IOService *provider)
{
    if (_interface) {
        _interface->close(this);
    }
    OSSafeReleaseNULL(_elements);
    _interface = nullptr;
    IOHIDEventService::handleStop(provider);
}

OSArray *IOHIDEventDriver::getReportElements(void)
{
    return _elements;
}

void IOHIDEventDriver::handleInterruptReportThunk(
    OSObject *target,
    AbsoluteTime timestamp,
    IOMemoryDescriptor *report,
    IOHIDReportType type,
    UInt32 reportID,
    void *refcon)
{
    IOHIDEventDriver *self = OSDynamicCast(IOHIDEventDriver, target);
    if (self) {
        self->handleInterruptReport(timestamp, report, type, reportID, refcon);
    }
}

void IOHIDEventDriver::handleInterruptReport(
    AbsoluteTime timestamp,
    IOMemoryDescriptor *report,
    IOHIDReportType type,
    UInt32 reportID,
    void *refcon __unused)
{
    UInt8 bytes[8] = {};
    const IOOptionBits keyboardOptions = 0;
    const UInt8 modifierUsages[8] = {
        kHIDUsage_KeyboardLeftControl,
        kHIDUsage_KeyboardLeftShift,
        kHIDUsage_KeyboardLeftAlt,
        kHIDUsage_KeyboardLeftGUI,
        kHIDUsage_KeyboardRightControl,
        kHIDUsage_KeyboardRightShift,
        kHIDUsage_KeyboardRightAlt,
        kHIDUsage_KeyboardRightGUI,
    };

    if (type != kIOHIDReportTypeInput || !report || report->getLength() < sizeof(bytes)) {
        return;
    }

    if (report->readBytes(0, bytes, sizeof(bytes)) != sizeof(bytes)) {
        return;
    }

    for (unsigned index = 0; index < 8; ++index) {
        bool wasPressed = (_previousModifiers & (1U << index)) != 0;
        bool isPressed = (bytes[0] & (1U << index)) != 0;
        if (wasPressed != isPressed) {
            dispatchKeyboardEvent(
                timestamp,
                kHIDPage_KeyboardOrKeypad,
                modifierUsages[index],
                isPressed ? 1 : 0,
                keyboardOptions);
        }
    }

    for (unsigned index = 0; index < 6; ++index) {
        UInt8 previousKey = _previousKeys[index];
        if (previousKey != 0 &&
            !pantheraReportKeyAppearsEarlier(_previousKeys, index, previousKey) &&
            !pantheraReportContainsKey(&bytes[2], previousKey)) {
            dispatchKeyboardEvent(
                timestamp,
                kHIDPage_KeyboardOrKeypad,
                previousKey,
                0,
                keyboardOptions);
        }
    }

    for (unsigned index = 0; index < 6; ++index) {
        UInt8 currentKey = bytes[index + 2];
        if (currentKey != 0 &&
            !pantheraReportKeyAppearsEarlier(&bytes[2], index, currentKey) &&
            !pantheraReportContainsKey(_previousKeys, currentKey)) {
            dispatchKeyboardEvent(
                timestamp,
                kHIDPage_KeyboardOrKeypad,
                currentKey,
                1,
                keyboardOptions);
        }
    }

    _previousModifiers = bytes[0];
    bcopy(&bytes[2], _previousKeys, sizeof(_previousKeys));
}
