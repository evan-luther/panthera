/*
 * Minimal IOHIDEvent compatibility surface for Panthera.
 *
 * The local IOHIDEventData headers are older than the newer IOHIDEvent.cpp
 * source drop, so we provide only the constructors/factory methods currently
 * needed to load IOHIDFamily and reach kernel-side HID matching.
 */

#include <IOKit/IOLib.h>

#include "IOHIDEvent.h"
#include "IOHIDEventData.h"

typedef struct {
    IOHIDEVENT_BASE;
    UInt32 usagePage;
    UInt32 usage;
    UInt8  down;
    UInt8  pressCount;
    UInt8  longPress;
    UInt8  clickSpeed;
} PantheraKeyboardEventData;

typedef struct {
    IOHIDEVENT_BASE;
    IOFixed x;
    IOFixed y;
    IOFixed z;
    UInt32  buttonState;
    UInt32  oldButtonState;
} PantheraPointerEventData;

typedef struct {
    IOHIDEVENT_BASE;
    IOFixed x;
    IOFixed y;
    IOFixed z;
} PantheraScrollEventData;

typedef struct {
    IOHIDEVENT_BASE;
    IOFixed level;
    UInt32  eventType;
    UInt32  usagePage;
    UInt32  usage;
    UInt8   tapCount;
    UInt8   reserved0[3];
} PantheraBiometricEventData;

typedef struct {
    IOHIDEVENT_BASE;
    UInt32 usagePage;
    UInt32 usage;
    UInt32 version;
    UInt32 length;
    UInt8  data[0];
} PantheraVendorDefinedEventData;

static IOByteCount PantheraBaseEventDataSize(IOHIDEventType type)
{
    switch (type) {
        case kIOHIDEventTypeKeyboard:
            return sizeof(PantheraKeyboardEventData);
        case kIOHIDEventTypePointer:
            return sizeof(PantheraPointerEventData);
        case kIOHIDEventTypeScroll:
            return sizeof(PantheraScrollEventData);
        case kIOHIDEventTypeBiometric:
            return sizeof(PantheraBiometricEventData);
        case kIOHIDEventTypeVendorDefined:
            return sizeof(PantheraVendorDefinedEventData);
        default:
            return sizeof(IOHIDEventData);
    }
}

#define PANTHERA_CREATE_EVENT(var, evType, timeStamp, options, additionalCapacity) \
    IOHIDEvent *var = new IOHIDEvent; \
    if (!(var)) { \
        return NULL; \
    } \
    if (!(var)->initWithTypeTimeStamp((evType), (timeStamp), (options), (additionalCapacity))) { \
        (var)->release(); \
        return NULL; \
    }

OSDefineMetaClassAndStructors(IOHIDEvent, OSObject)

AbsoluteTime IOHIDEvent::getTimeStamp()
{
#if KERNEL
    return _options & kIOHIDEventOptionContinuousTime ? continuoustime_to_absolutetime(_timeStamp) : _timeStamp;
#else
    return _timeStamp;
#endif
}

void IOHIDEvent::setTimeStamp(AbsoluteTime timeStamp)
{
    _timeStamp = timeStamp;
    _options &= ~kIOHIDEventOptionContinuousTime;
}

UInt64 IOHIDEvent::getTimeStampOfType(IOHIDEventTimestampType type)
{
    UInt64 time = _timeStamp;

#if KERNEL
    if (type == kIOHIDEventTimestampTypeAbsolute) {
        time = _options & kIOHIDEventOptionContinuousTime ? continuoustime_to_absolutetime(_timeStamp) : _timeStamp;
    } else if (type == kIOHIDEventTimestampTypeContinuous) {
        time = _options & kIOHIDEventOptionContinuousTime ? _timeStamp : absolutetime_to_continuoustime(_timeStamp);
    }
#endif

    return time;
}

void IOHIDEvent::setTimeStampOfType(UInt64 timeStamp, IOHIDEventTimestampType type)
{
    _timeStamp = timeStamp;
    if (type == kIOHIDEventTimestampTypeAbsolute) {
        _options &= ~kIOHIDEventOptionContinuousTime;
    } else if (type == kIOHIDEventTimestampTypeContinuous) {
        _options |= kIOHIDEventOptionContinuousTime;
    }
}

bool IOHIDEvent::initWithType(IOHIDEventType type, IOByteCount additionalCapacity)
{
    return initWithTypeTimeStamp(type, 0, 0, additionalCapacity);
}

bool IOHIDEvent::initWithTypeTimeStamp(IOHIDEventType type,
                                       UInt64         timeStamp,
                                       IOOptionBits   options,
                                       IOByteCount    additionalCapacity)
{
    IOByteCount capacity = PantheraBaseEventDataSize(type) + additionalCapacity;

    if (!OSObject::init()) {
        return false;
    }

    _data = (IOHIDEventData *)IOMallocZeroData(capacity);
    if (!_data) {
        return false;
    }

    _capacity    = capacity;
    _children    = NULL;
    _parent      = NULL;
    _timeStamp   = timeStamp;
    _senderID    = 0;
    _typeMask    = (type < 64) ? (1ULL << type) : 0;
    _options     = options;
    _eventCount  = 1;

    _data->size    = (UInt32)capacity;
    _data->type    = type;
    _data->options = options;
    _data->depth   = 0;

    return true;
}

IOHIDEvent * IOHIDEvent::withType(IOHIDEventType type, IOOptionBits options)
{
    PANTHERA_CREATE_EVENT(event, type, 0, options, 0);
    return event;
}

IOHIDEvent * IOHIDEvent::withBytes(const void *bytes, IOByteCount size)
{
    const IOHIDEventData *source = (const IOHIDEventData *)bytes;
    IOHIDEvent *event;

    if (!bytes || size < sizeof(IOHIDEventData)) {
        return NULL;
    }

    event = new IOHIDEvent;
    if (!event) {
        return NULL;
    }

    if (!event->initWithTypeTimeStamp(source->type,
                                      0,
                                      source->options,
                                      size - PantheraBaseEventDataSize(source->type))) {
        event->release();
        return NULL;
    }

    if (size > event->_capacity) {
        size = event->_capacity;
    }

    bcopy(bytes, event->_data, size);
    event->_capacity = size;

    return event;
}

void IOHIDEvent::free(void)
{
    if (_children) {
        _children->release();
        _children = NULL;
    }

    if (_data) {
        IOFreeData(_data, _capacity);
        _data = NULL;
    }

    OSObject::free();
}

void IOHIDEvent::appendChild(IOHIDEvent *childEvent)
{
    if (!_children) {
        const OSObject *events[] = { childEvent };

        _children = OSArray::withObjects(events, 1);
        _data->options |= kIOHIDEventOptionIsCollection;
    } else {
        _children->setObject(childEvent);
    }
}

OSArray * IOHIDEvent::getChildren()
{
    return _children;
}

IOHIDEventType IOHIDEvent::getType()
{
    return _data->type;
}

void IOHIDEvent::setType(IOHIDEventType type)
{
    initWithType(type);
}

IOHIDEvent * IOHIDEvent::getEvent(IOHIDEventType type, IOOptionBits options __unused)
{
    return (_data->type == type) ? this : NULL;
}

SInt32 IOHIDEvent::getIntegerValue(IOHIDEventField key, IOOptionBits options)
{
    IOHIDEvent *event = getEvent(IOHIDEventFieldEventType(key), options);

    if (!event) {
        return 0;
    }

    switch (key) {
        case kIOHIDEventFieldKeyboardUsagePage:
            return ((PantheraKeyboardEventData *)event->_data)->usagePage;
        case kIOHIDEventFieldKeyboardUsage:
            return ((PantheraKeyboardEventData *)event->_data)->usage;
        case kIOHIDEventFieldKeyboardDown:
            return ((PantheraKeyboardEventData *)event->_data)->down;
        case kIOHIDEventFieldKeyboardRepeat:
            return (((PantheraKeyboardEventData *)event->_data)->pressCount > 1);
        case kIOHIDEventFieldPointerButtonMask:
            return ((PantheraPointerEventData *)event->_data)->buttonState;
        case kIOHIDEventFieldVendorDefinedUsagePage:
            return ((PantheraVendorDefinedEventData *)event->_data)->usagePage;
        case kIOHIDEventFieldVendorDefinedUsage:
            return ((PantheraVendorDefinedEventData *)event->_data)->usage;
        case kIOHIDEventFieldVendorDefinedVersion:
            return ((PantheraVendorDefinedEventData *)event->_data)->version;
        case kIOHIDEventFieldVendorDefinedDataLength:
            return ((PantheraVendorDefinedEventData *)event->_data)->length;
        case kIOHIDEventFieldBiometricEventType:
            return ((PantheraBiometricEventData *)event->_data)->eventType;
        default:
            return 0;
    }
}

IOHIDDouble IOHIDEvent::getDoubleValue(IOHIDEventField key, IOOptionBits options)
{
    return (IOHIDDouble)getFixedValue(key, options) / 65536.0;
}

IOFixed IOHIDEvent::getFixedValue(IOHIDEventField key, IOOptionBits options)
{
    IOHIDEvent *event = getEvent(IOHIDEventFieldEventType(key), options);

    if (!event) {
        return 0;
    }

    switch (key) {
        case kIOHIDEventFieldPointerX:
            return ((PantheraPointerEventData *)event->_data)->x;
        case kIOHIDEventFieldPointerY:
            return ((PantheraPointerEventData *)event->_data)->y;
        case kIOHIDEventFieldPointerZ:
            return ((PantheraPointerEventData *)event->_data)->z;
        case kIOHIDEventFieldScrollX:
            return ((PantheraScrollEventData *)event->_data)->x;
        case kIOHIDEventFieldScrollY:
            return ((PantheraScrollEventData *)event->_data)->y;
        case kIOHIDEventFieldScrollZ:
            return ((PantheraScrollEventData *)event->_data)->z;
        case kIOHIDEventFieldBiometricLevel:
            return ((PantheraBiometricEventData *)event->_data)->level;
        default:
            return 0;
    }
}

UInt8 * IOHIDEvent::getDataValue(IOHIDEventField key, IOOptionBits options)
{
    IOHIDEvent *event = getEvent(IOHIDEventFieldEventType(key), options);

    if (!event) {
        return NULL;
    }

    switch (key) {
        case kIOHIDEventFieldVendorDefinedData:
            return ((PantheraVendorDefinedEventData *)event->_data)->data;
        default:
            return NULL;
    }
}

void IOHIDEvent::setIntegerValue(IOHIDEventField key, SInt32 value, IOOptionBits options)
{
    IOHIDEvent *event = getEvent(IOHIDEventFieldEventType(key), options);

    if (!event) {
        return;
    }

    switch (key) {
        case kIOHIDEventFieldKeyboardUsagePage:
            ((PantheraKeyboardEventData *)event->_data)->usagePage = (UInt32)value;
            break;
        case kIOHIDEventFieldKeyboardUsage:
            ((PantheraKeyboardEventData *)event->_data)->usage = (UInt32)value;
            break;
        case kIOHIDEventFieldKeyboardDown:
            ((PantheraKeyboardEventData *)event->_data)->down = value ? 1 : 0;
            break;
        case kIOHIDEventFieldPointerButtonMask:
            ((PantheraPointerEventData *)event->_data)->buttonState = (UInt32)value;
            break;
        case kIOHIDEventFieldVendorDefinedUsagePage:
            ((PantheraVendorDefinedEventData *)event->_data)->usagePage = (UInt32)value;
            break;
        case kIOHIDEventFieldVendorDefinedUsage:
            ((PantheraVendorDefinedEventData *)event->_data)->usage = (UInt32)value;
            break;
        case kIOHIDEventFieldVendorDefinedVersion:
            ((PantheraVendorDefinedEventData *)event->_data)->version = (UInt32)value;
            break;
        case kIOHIDEventFieldVendorDefinedDataLength:
            ((PantheraVendorDefinedEventData *)event->_data)->length = (UInt32)value;
            break;
        case kIOHIDEventFieldBiometricEventType:
            ((PantheraBiometricEventData *)event->_data)->eventType = (UInt32)value;
            break;
        default:
            break;
    }
}

void IOHIDEvent::setFixedValue(IOHIDEventField key, IOFixed value, IOOptionBits options)
{
    IOHIDEvent *event = getEvent(IOHIDEventFieldEventType(key), options);

    if (!event) {
        return;
    }

    switch (key) {
        case kIOHIDEventFieldPointerX:
            ((PantheraPointerEventData *)event->_data)->x = value;
            break;
        case kIOHIDEventFieldPointerY:
            ((PantheraPointerEventData *)event->_data)->y = value;
            break;
        case kIOHIDEventFieldPointerZ:
            ((PantheraPointerEventData *)event->_data)->z = value;
            break;
        case kIOHIDEventFieldScrollX:
            ((PantheraScrollEventData *)event->_data)->x = value;
            break;
        case kIOHIDEventFieldScrollY:
            ((PantheraScrollEventData *)event->_data)->y = value;
            break;
        case kIOHIDEventFieldScrollZ:
            ((PantheraScrollEventData *)event->_data)->z = value;
            break;
        case kIOHIDEventFieldBiometricLevel:
            ((PantheraBiometricEventData *)event->_data)->level = value;
            break;
        default:
            break;
    }
}

void IOHIDEvent::setDoubleValue(IOHIDEventField key, IOHIDDouble value, IOOptionBits options)
{
    setFixedValue(key, (IOFixed)(value * 65536.0), options);
}

size_t IOHIDEvent::getLength()
{
    _eventCount = 0;
    return getLength(&_eventCount);
}

IOByteCount IOHIDEvent::getLength(UInt32 *count)
{
    IOByteCount length = _data->size + sizeof(IOHIDSystemQueueElement);

    if (_children) {
        UInt32 childCount = _children->getCount();

        for (UInt32 i = 0; i < childCount; i++) {
            IOHIDEvent *child = (IOHIDEvent *)_children->getObject(i);
            if (child) {
                length += child->getLength(count) - sizeof(IOHIDSystemQueueElement);
            }
        }
    }

    if (count) {
        *count = *count + 1;
    }

    return length;
}

IOByteCount IOHIDEvent::appendBytes(UInt8 *bytes, IOByteCount withLength)
{
    IOByteCount size = _data->size;

    if (size > withLength) {
        return 0;
    }

    bcopy(_data, bytes, size);

    if (_children) {
        UInt32 childCount = _children->getCount();

        for (UInt32 i = 0; i < childCount; i++) {
            IOHIDEvent *child = (IOHIDEvent *)_children->getObject(i);
            if (child) {
                size += child->appendBytes(bytes + size, withLength - size);
            }
        }
    }

    return size;
}

IOByteCount IOHIDEvent::readBytes(void *bytes, IOByteCount withLength)
{
    IOHIDSystemQueueElement *queueElement = NULL;

    if (withLength < sizeof(IOHIDSystemQueueElement)) {
        return 0;
    }

    queueElement = (IOHIDSystemQueueElement *)bytes;
    queueElement->timeStamp = _timeStamp;
    queueElement->options = _options;
    queueElement->eventCount = _eventCount;
    queueElement->senderID = _senderID;
    queueElement->attributeLength = 0;

    withLength -= sizeof(IOHIDSystemQueueElement);

    return appendBytes((UInt8 *)queueElement->payload, withLength);
}

OSData *IOHIDEvent::createBytes()
{
    OSData *result = NULL;
    IOHIDSystemQueueElement queueElement = { 0 };

    result = OSData::withCapacity((unsigned int)getLength());
    if (!result) {
        return NULL;
    }

    queueElement.timeStamp = _timeStamp;
    queueElement.options = _options;
    queueElement.eventCount = _eventCount;
    queueElement.senderID = _senderID;
    queueElement.attributeLength = 0;

    result->appendBytes(&queueElement, sizeof(queueElement));
    result->appendBytes(_data, _data->size);

    if (_children) {
        for (unsigned int i = 0; i < _children->getCount(); i++) {
            IOHIDEvent *child = (IOHIDEvent *)_children->getObject(i);
            if (!child) {
                continue;
            }
            result->appendBytes(child->_data, child->_data->size);
        }
    }

    return result;
}

IOHIDEventPhaseBits IOHIDEvent::getPhase()
{
    return (_data->options >> kIOHIDEventEventOptionPhaseShift) & kIOHIDEventEventPhaseMask;
}

void IOHIDEvent::setPhase(IOHIDEventPhaseBits phase)
{
    _data->options &= ~(kIOHIDEventEventPhaseMask << kIOHIDEventEventOptionPhaseShift);
    _data->options |= ((phase & kIOHIDEventEventPhaseMask) << kIOHIDEventEventOptionPhaseShift);
}

void IOHIDEvent::setSenderID(uint64_t senderID)
{
    _senderID = senderID;
}

uint64_t IOHIDEvent::getLatency(uint32_t scaleFactor)
{
    AbsoluteTime delta = mach_absolute_time();
    AbsoluteTime ts = getTimeStamp();
    uint64_t ns;

    SUB_ABSOLUTETIME(&delta, &ts);
    absolutetime_to_nanoseconds(delta, &ns);

    return ns / scaleFactor;
}

IOHIDEvent * IOHIDEvent::keyboardEvent(UInt64       timeStamp,
                                       UInt32       usagePage,
                                       UInt32       usage,
                                       Boolean      down,
                                       IOOptionBits options)
{
    return keyboardEvent(timeStamp, usagePage, usage, down, 1, false, 0, options);
}

IOHIDEvent * IOHIDEvent::keyboardEvent(UInt64       timeStamp,
                                       UInt32       usagePage,
                                       UInt32       usage,
                                       Boolean      down,
                                       UInt8        pressCount,
                                       Boolean      longPress,
                                       UInt8        clickSpeed,
                                       IOOptionBits options)
{
    PANTHERA_CREATE_EVENT(event, kIOHIDEventTypeKeyboard, timeStamp, options, 0);
    PantheraKeyboardEventData *data;

    data = (PantheraKeyboardEventData *)event->_data;
    data->usagePage  = usagePage;
    data->usage      = usage;
    data->down       = down ? 1 : 0;
    data->pressCount = pressCount;
    data->longPress  = longPress ? 1 : 0;
    data->clickSpeed = clickSpeed;

    return event;
}

IOHIDEvent * IOHIDEvent::relativePointerEventWithFixed(UInt64       timeStamp,
                                                       IOFixed      x,
                                                       IOFixed      y,
                                                       IOFixed      z,
                                                       UInt32       buttonState,
                                                       UInt32       oldButtonState,
                                                       IOOptionBits options)
{
    PANTHERA_CREATE_EVENT(event, kIOHIDEventTypePointer, timeStamp, options, 0);
    PantheraPointerEventData *data;

    data = (PantheraPointerEventData *)event->_data;
    data->x = x;
    data->y = y;
    data->z = z;
    data->buttonState = buttonState;
    data->oldButtonState = oldButtonState;

    return event;
}

IOHIDEvent * IOHIDEvent::absolutePointerEvent(UInt64       timeStamp,
                                              IOFixed      x,
                                              IOFixed      y,
                                              IOFixed      z,
                                              UInt32       buttonState,
                                              UInt32       oldButtonState,
                                              IOOptionBits options)
{
    return relativePointerEventWithFixed(timeStamp, x, y, z, buttonState, oldButtonState, options);
}

IOHIDEvent * IOHIDEvent::scrollEventWithFixed(UInt64       timeStamp,
                                              IOFixed      x,
                                              IOFixed      y,
                                              IOFixed      z,
                                              IOOptionBits options)
{
    PANTHERA_CREATE_EVENT(event, kIOHIDEventTypeScroll, timeStamp, options, 0);
    PantheraScrollEventData *data;

    data = (PantheraScrollEventData *)event->_data;
    data->x = x;
    data->y = y;
    data->z = z;

    return event;
}

IOHIDEvent * IOHIDEvent::biometricEvent(UInt64                  timeStamp,
                                        IOFixed                 level,
                                        IOHIDBiometricEventType eventType,
                                        IOOptionBits            options)
{
    return biometricEvent(timeStamp, level, eventType, 0, 0, 0, options);
}

IOHIDEvent * IOHIDEvent::biometricEvent(UInt64                  timeStamp,
                                        IOFixed                 level,
                                        IOHIDBiometricEventType eventType,
                                        UInt32                  usagePage,
                                        UInt32                  usage,
                                        UInt8                   tapCount,
                                        IOOptionBits            options)
{
    PANTHERA_CREATE_EVENT(event, kIOHIDEventTypeBiometric, timeStamp, options, 0);
    PantheraBiometricEventData *data;

    data = (PantheraBiometricEventData *)event->_data;
    data->level     = level;
    data->eventType = eventType;
    data->usagePage = usagePage;
    data->usage     = usage;
    data->tapCount  = tapCount;

    return event;
}

IOHIDEvent * IOHIDEvent::standardGameControllerEvent(UInt64       timeStamp,
                                                     IOFixed      dpadUp,
                                                     IOFixed      dpadDown,
                                                     IOFixed      dpadLeft,
                                                     IOFixed      dpadRight,
                                                     IOFixed      faceX,
                                                     IOFixed      faceY,
                                                     IOFixed      faceA,
                                                     IOFixed      faceB,
                                                     IOFixed      shoulderL,
                                                     IOFixed      shoulderR,
                                                     IOOptionBits options)
{
#pragma unused(dpadUp, dpadDown, dpadLeft, dpadRight, faceX, faceY, faceA, faceB, shoulderL, shoulderR)
    PANTHERA_CREATE_EVENT(event, kIOHIDEventTypeGameController, timeStamp, options, 0);
    return event;
}

IOHIDEvent * IOHIDEvent::extendedGameControllerEvent(UInt64       timeStamp,
                                                     IOFixed      dpadUp,
                                                     IOFixed      dpadDown,
                                                     IOFixed      dpadLeft,
                                                     IOFixed      dpadRight,
                                                     IOFixed      faceX,
                                                     IOFixed      faceY,
                                                     IOFixed      faceA,
                                                     IOFixed      faceB,
                                                     IOFixed      shoulderL1,
                                                     IOFixed      shoulderR1,
                                                     IOFixed      shoulderL2,
                                                     IOFixed      shoulderR2,
                                                     IOFixed      joystickX,
                                                     IOFixed      joystickY,
                                                     IOFixed      joystickZ,
                                                     IOFixed      joystickRz,
                                                     IOOptionBits options)
{
#pragma unused(dpadUp, dpadDown, dpadLeft, dpadRight, faceX, faceY, faceA, faceB, shoulderL1, shoulderR1, shoulderL2, shoulderR2, joystickX, joystickY, joystickZ, joystickRz)
    PANTHERA_CREATE_EVENT(event, kIOHIDEventTypeGameController, timeStamp, options, 0);
    return event;
}

IOHIDEvent * IOHIDEvent::vendorDefinedEvent(UInt64       timeStamp,
                                            UInt32       usagePage,
                                            UInt32       usage,
                                            UInt32       version,
                                            UInt8 *      data,
                                            UInt32       length,
                                            IOOptionBits options)
{
    PANTHERA_CREATE_EVENT(event, kIOHIDEventTypeVendorDefined, timeStamp, options, length);
    PantheraVendorDefinedEventData *eventData = (PantheraVendorDefinedEventData *)event->_data;

    eventData->usagePage = usagePage;
    eventData->usage = usage;
    eventData->version = version;
    eventData->length = length;

    if (data && length) {
        bcopy(data, eventData->data, length);
    }

    return event;
}
