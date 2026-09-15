#ifndef PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDEVENTSERVICE_H
#define PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDEVENTSERVICE_H

#include <HIDDriverKit/IOHIDDeviceTypes.h>
#include <DriverKit/OSAction.h>
#include <DriverKit/IOService.h>

class IOService;
class IOBufferMemoryDescriptor;
class OSAction;
class OSDictionary;
class IOHIDEvent;
typedef struct IOHIDDigitizerStylusData IOHIDDigitizerStylusData;
typedef struct IOHIDDigitizerTouchData IOHIDDigitizerTouchData;

#define IOHIDEventService_Start_Args \
    IOService * provider

#define IOHIDEventService_Stop_Args \
    IOService * provider

#define IOHIDEventService_dispatchKeyboardEvent_Args \
    uint64_t timeStamp, \
    uint32_t usagePage, \
    uint32_t usage, \
    uint32_t value, \
    IOOptionBits options, \
    bool repeat

#define IOHIDEventService_dispatchRelativePointerEvent_Args \
    uint64_t timeStamp, \
    IOFixed dx, \
    IOFixed dy, \
    uint32_t buttonState, \
    IOOptionBits options, \
    bool accelerate

#define IOHIDEventService_dispatchAbsolutePointerEvent_Args \
    uint64_t timeStamp, \
    IOFixed x, \
    IOFixed y, \
    uint32_t buttonState, \
    IOOptionBits options, \
    bool accelerate

#define IOHIDEventService_dispatchRelativeScrollWheelEvent_Args \
    uint64_t timeStamp, \
    IOFixed dx, \
    IOFixed dy, \
    IOFixed dz, \
    IOOptionBits options, \
    bool accelerate

#define IOHIDEventService_dispatchDigitizerStylusEvent_Args \
    uint64_t timeStamp, \
    IOHIDDigitizerStylusData * stylusData

#define IOHIDEventService_dispatchDigitizerTouchEvent_Args \
    uint64_t timeStamp, \
    IOHIDDigitizerTouchData * touchData, \
    uint32_t touchDataCount

#define IOHIDEventService_SetLED_Args \
    uint32_t usage, \
    bool on

#define IOHIDEventService_SetLEDState_Args \
    uint32_t usagePage, \
    uint32_t usage, \
    bool on

#define IOHIDEventService_dispatchEvent_Args \
    IOHIDEvent * event

#define IOHIDEventService_handleCopyMatchingEvent_Args \
    OSDictionary * matching, \
    IOHIDEvent ** event

#define IOHIDEventService_SetProperties_Args \
    OSDictionary * properties

#define IOHIDEventService__DispatchKeyboardEvent_Args \
    uint64_t timeStamp, \
    uint32_t usagePage, \
    uint32_t usage, \
    uint32_t value, \
    IOOptionBits options, \
    bool repeat

#define IOHIDEventService__DispatchRelativePointerEvent_Args \
    uint64_t timeStamp, \
    IOFixed dx, \
    IOFixed dy, \
    uint32_t buttonState, \
    IOOptionBits options, \
    bool accelerate

#define IOHIDEventService__DispatchAbsolutePointerEvent_Args \
    uint64_t timeStamp, \
    IOFixed x, \
    IOFixed y, \
    uint32_t buttonState, \
    IOOptionBits options, \
    bool accelerate

#define IOHIDEventService__DispatchRelativeScrollWheelEvent_Args \
    uint64_t timeStamp, \
    IOFixed dx, \
    IOFixed dy, \
    IOFixed dz, \
    IOOptionBits options, \
    bool accelerate

#define IOHIDEventService_SetEventMemory_Args \
    IOBufferMemoryDescriptor * memory

#define IOHIDEventService_EventAvailable_Args \
    uint32_t length

#define IOHIDEventService__Start_Args \
    IOService * provider

#define IOHIDEventService_CopyEvent_Args \
    OSDictionary * matching, \
    uint64_t context, \
    OSAction * action

#define IOHIDEventService__CopyEvent_Args \
    OSDictionary * matching, \
    uint64_t context, \
    OSAction * action

#define IOHIDEventService__CompleteCopyEvent_Args \
    OSAction * action, \
    IOBufferMemoryDescriptor * eventBuffer, \
    uint64_t context

#define IOHIDEventService_SetUserProperties_Args \
    OSDictionary * properties, \
    uint64_t context, \
    OSAction * action

#define IOHIDEventService__SetUserProperties_Args \
    OSDictionary * properties, \
    uint64_t context, \
    OSAction * action

#define IOHIDEventService__CompleteSetProperties_Args \
    OSAction * action, \
    IOReturn result, \
    uint64_t context

#define IOHIDEventService_SetLEDAction_Args \
    uint32_t usagePage, \
    uint32_t usage, \
    bool on, \
    uint64_t context, \
    OSAction * action

#define IOHIDEventService__SetLED_Args \
    uint32_t usagePage, \
    uint32_t usage, \
    bool on, \
    uint64_t context, \
    OSAction * action

#define IOHIDEventService__CompleteSetLED_Args \
    OSAction * action, \
    IOReturn result, \
    uint64_t context

#endif /* PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDEVENTSERVICE_H */
