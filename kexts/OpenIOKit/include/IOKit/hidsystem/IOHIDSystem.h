#ifndef PANTHERA_MINIMAL_IOHIDSYSTEM_H
#define PANTHERA_MINIMAL_IOHIDSYSTEM_H

#include <IOKit/IOReturn.h>

#ifndef sub_iokit_hidsystem
#define sub_iokit_hidsystem err_sub(14)
#endif

#define kIOHIDSystem508MouseClickMessage        iokit_family_msg(sub_iokit_hidsystem, 1)
#define kIOHIDSystemDeviceSeizeRequestMessage   iokit_family_msg(sub_iokit_hidsystem, 2)
#define kIOHIDSystem508SpecialKeyDownMessage    iokit_family_msg(sub_iokit_hidsystem, 3)
#define kIOHIDSystemActivityTickle              iokit_family_msg(sub_iokit_hidsystem, 5)
#define kIOHIDSystemUserHidActivity             iokit_family_msg(sub_iokit_hidsystem, 6)

#endif
