/*
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef __APPLEHIDUSAGETABLES__
#define __APPLEHIDUSAGETABLES__

#include <IOKit/hid/IOHIDUsageTables.h>

/*
 * The public 2008 OSS drop omits the Apple vendor usage table contents.
 * These values only gate Apple-vendor descriptor handling; generic USB HID
 * keyboards do not depend on them.
 */
enum
{
    kHIDPage_AppleVendor                     = kHIDPage_VendorDefinedStart,
    kHIDPage_AppleVendorTopCase              = kHIDPage_VendorDefinedStart + 1,
    kHIDPage_AppleVendorKeyboard             = kHIDPage_VendorDefinedStart + 2,
    kHIDPage_AppleVendorDisplayCover         = kHIDPage_VendorDefinedStart + 3,
    kHIDPage_AppleVendorSmartCover           = kHIDPage_VendorDefinedStart + 4,
    kHIDPage_AppleVendorMultitouch           = kHIDPage_VendorDefinedStart + 5,
    kHIDPage_AppleVendorMotion               = kHIDPage_VendorDefinedStart + 6,
    kHIDPage_AppleVendorSensor               = kHIDPage_VendorDefinedStart + 7,
    kHIDPage_AppleVendorHIDEvent             = kHIDPage_VendorDefinedStart + 8,
    kHIDPage_AppleVendorBattery              = kHIDPage_VendorDefinedStart + 9,

    kHIDUsage_AppleVendor_Message            = 0x0001,
    kHIDUsage_AppleVendor_Payload            = 0x0002,
    kHIDUsage_AppleVendor_MultipleInterfaces = 0x0003,
    kHIDUsage_AppleVendor_Properties         = 0x0004,
    kHIDUsage_AppleVendor_NXEvent            = 0x0005,
    kHIDUsage_AppleVendor_NXEvent_Translated = 0x0006,
    kHIDUsage_AppleVendor_Perf               = 0x0007,
    kHIDUsage_AppleVendor_DFR                = 0x0008,

    /* AppleVendorTopCase usages */
    kHIDUsage_AV_TopCase_BrightnessUp        = 0x0001,
    kHIDUsage_AV_TopCase_BrightnessDown      = 0x0002,
    kHIDUsage_AV_TopCase_VideoMirror         = 0x0003,
    kHIDUsage_AV_TopCase_IlluminationUp      = 0x0004,
    kHIDUsage_AV_TopCase_IlluminationDown    = 0x0005,
    kHIDUsage_AV_TopCase_IlluminationToggle  = 0x0006,
    kHIDUsage_AV_TopCase_KeyboardFn          = 0x0007,

    /* AppleVendorKeyboard usages */
    kHIDUsage_AppleVendorKeyboard_Function          = 0x0001,
    kHIDUsage_AppleVendorKeyboard_Spotlight         = 0x0002,
    kHIDUsage_AppleVendorKeyboard_Dashboard         = 0x0003,
    kHIDUsage_AppleVendorKeyboard_Launchpad         = 0x0004,
    kHIDUsage_AppleVendorKeyboard_Reserved          = 0x0005,
    kHIDUsage_AppleVendorKeyboard_CapsLockDelayEnable = 0x0006,
    kHIDUsage_AppleVendorKeyboard_PowerState        = 0x0007,
    kHIDUsage_AppleVendorKeyboard_Expose_All        = 0x0008,
    kHIDUsage_AppleVendorKeyboard_Expose_Desktop    = 0x0009,
    kHIDUsage_AppleVendorKeyboard_Brightness_Up     = 0x000a,
    kHIDUsage_AppleVendorKeyboard_Brightness_Down   = 0x000b,
    kHIDUsage_AppleVendorKeyboard_Language          = 0x000c,
    kHIDUsage_AppleVendorKeyboard_LongPress         = 0x000d,
    kHIDUsage_AppleVendorKeyboard_CapsLockState     = 0x000e,

    /* AppleVendorSmartCover usages */
    kHIDUsage_AppleVendorSmartCover_Attach          = 0x0001,

    /* AppleVendorMultitouch usages */
    kHIDUsage_AppleVendorMultitouch_TouchCancel     = 0x0001,

    /* AppleVendorSensor usages */
    kHIDUsage_AppleVendorSensor_BTSniffOff          = 0x0001,

    /* AppleVendorHIDEvent usages */
    kHIDUsage_AppleVendorHIDEvent_PhaseBegan        = 0x0001,
    kHIDUsage_AppleVendorHIDEvent_PhaseEnded        = 0x0002
};

#endif
