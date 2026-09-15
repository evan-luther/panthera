#ifndef PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDINTERFACE_H
#define PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDINTERFACE_H

#include <HIDDriverKit/IOHIDDeviceTypes.h>
#include <DriverKit/OSAction.h>
#include <DriverKit/IOService.h>
#include <DriverKit/IORPC.h>

class IOService;
class IOMemoryDescriptor;
class IOBufferMemoryDescriptor;
class OSAction;
class OSArray;

#define IOHIDInterface_ReportAvailable_Args \
    uint64_t timestamp, \
    uint32_t reportID, \
    uint32_t reportLength, \
    IOHIDReportType type, \
    IOMemoryDescriptor * report, \
    OSAction * action

#define IOHIDInterface_AddReportToPool_Args \
    IOBufferMemoryDescriptor * report

#define IOHIDInterface_Open_Args \
    IOService * forClient, \
    IOOptionBits options, \
    OSAction * action

#define IOHIDInterface_Close_Args \
    IOService * forClient, \
    IOOptionBits options

#define IOHIDInterface_SetReport_Args \
    IOMemoryDescriptor * report, \
    IOHIDReportType reportType, \
    uint32_t reportID, \
    IOOptionBits options

#define IOHIDInterface_GetReport_Args \
    IOMemoryDescriptor * report, \
    IOHIDReportType reportType, \
    uint32_t reportID, \
    IOOptionBits options

#define IOHIDInterface_processReport_Args \
    uint64_t timestamp, \
    uint8_t * report, \
    uint32_t reportLength, \
    IOHIDReportType type, \
    uint32_t reportID

#define IOHIDInterface_commitElements_Args \
    OSArray * elements, \
    IOHIDElementCommitDirection direction

#define IOHIDInterface_GetSupportedCookies_Args \
    IOBufferMemoryDescriptor ** cookies

#define IOHIDInterface_setElementValues_Args \
    OSArray * elements

#define IOHIDInterface_getElementValues_Args \
    OSArray * elements

#define IOHIDInterface_SetElementValues_Args \
    uint32_t count, \
    IOMemoryDescriptor * elementValues

#define IOHIDInterface_GetElementValues_Args \
    uint32_t count, \
    IOMemoryDescriptor * elementValues

#define IOHIDInterface_SendDebugBuffer_Args \
    IOMemoryDescriptor * debug

#endif /* PANTHERA_FORWARD_HIDDRIVERKIT_IOHIDINTERFACE_H */
