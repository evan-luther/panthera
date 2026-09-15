#ifndef PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDDEVICE_H
#define PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDDEVICE_H

#include <HIDDriverKit/IOHIDDeviceTypes.h>
#include <DriverKit/OSAction.h>
#include <DriverKit/IOService.h>

class IOMemoryDescriptor;
class IOBufferMemoryDescriptor;
class OSAction;
class OSObject;
class IOService;

#define IOHIDDevice_handleReport_Args \
        uint64_t timestamp, \
        IOMemoryDescriptor * report, \
        uint32_t reportLength, \
        IOHIDReportType reportType, \
        IOOptionBits options

#define IOHIDDevice_getReport_Args \
        IOMemoryDescriptor * report, \
        IOHIDReportType reportType, \
        IOOptionBits options, \
        uint32_t completionTimeout, \
        OSAction * action

#define IOHIDDevice_setReport_Args \
        IOMemoryDescriptor * report, \
        IOHIDReportType reportType, \
        IOOptionBits options, \
        uint32_t completionTimeout, \
        OSAction * action

#define IOHIDDevice_CompleteReport_Args \
        OSAction * action, \
        IOReturn status, \
        uint32_t actualByteCount

#define IOHIDDevice_setProperty_Args \
        OSObject * key, \
        OSObject * value

#define IOHIDDevice__SetProperty_Args \
        IOBufferMemoryDescriptor * serialization

#define IOHIDDevice__HandleReport_Args \
        uint64_t timestamp, \
        IOMemoryDescriptor * report, \
        uint32_t reportLength, \
        IOHIDReportType reportType, \
        IOOptionBits options

#define IOHIDDevice__ProcessReport_Args \
        HIDReportCommandType command, \
        IOMemoryDescriptor * report, \
        IOHIDReportType reportType, \
        IOOptionBits options, \
        uint32_t completionTimeout, \
        OSAction * action

#define IOHIDDevice__CompleteReport_Args \
        OSAction * action, \
        IOReturn status, \
        uint32_t actualByteCount

#define IOHIDDevice__Start_Args \
        IOService * provider

#endif /* PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDDEVICE_H */
