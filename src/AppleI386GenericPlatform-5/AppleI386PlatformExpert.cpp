/*
 * Copyright (c) 1998-2003 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * The contents of this file constitute Original Code as defined in and
 * are subject to the Apple Public Source License Version 1.1 (the
 * "License").  You may not use this file except in compliance with the
 * License.  Please obtain a copy of the License at
 * http://www.apple.com/publicsource and read it before using this file.
 * 
 * This Original Code and all software distributed under the License are
 * distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE OR NON-INFRINGEMENT.  Please see the
 * License for the specific language governing rights and limitations
 * under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1998-2003 Apple Computer, Inc.  All rights reserved. 
 *
 * HISTORY
 */
 
#include <IOKit/system.h>
#include <architecture/i386/pio.h>

#include <pexpert/i386/boot.h>
#include <pexpert/pexpert.h>

#include <IOKit/IORegistryEntry.h>
#include <libkern/c++/OSContainers.h>
#include <libkern/c++/OSKext.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSUnserialize.h>

extern "C" {
#include <i386/cpuid.h>
}

#include <IOKit/platform/ApplePlatformExpert.h>
#include "AppleI386PlatformExpert.h"

#include <IOKit/assert.h>

#define PANTHERA_PE_TRACE(...) PANTHERA_TRACE(printf(__VA_ARGS__))

enum {
    kIRQAvailable   = 0,
    kIRQExclusive   = 1,
    kIRQSharable    = 2,
    kSystemIRQCount = 16
};

enum {
    kPantheraInterruptTriggerModeLevel = 0x01,
    kPantheraInterruptPolarityLow      = 0x02,
    kPantheraInterruptIsShareable      = 0x04,
};

static struct {
    UInt16  consumers;
    UInt16  status;
} IRQ[kSystemIRQCount];

static IOLock * ResourceLock;

static const char *
panthera_prop_string(IORegistryEntry * entry, const char * key)
{
    OSString * value = OSDynamicCast(OSString, entry->getProperty(key));
    return value ? value->getCStringNoCopy() : "<null>";
}

static int
panthera_prop_bool(IORegistryEntry * entry, const char * key)
{
    return (entry->getProperty(key) != 0);
}

static void
panthera_log_display_nub(const char * phase, IOService * nub)
{
    const OSMetaClass * meta = nub->getMetaClass();
    const OSMetaClass * superMeta = meta ? meta->getSuperClass() : 0;
    IOService * provider = nub->getProvider();

    printf("PANTHERA:PE %s class=%s super=%s name=%s IOName=%s reg-name=%s boot-display=%d provider=%s provider-class=%s\n",
           phase,
           meta ? meta->getClassName() : "<null>",
           superMeta ? superMeta->getClassName() : "<null>",
           nub->getName() ? nub->getName() : "<null>",
           panthera_prop_string(nub, "IOName"),
           panthera_prop_string(nub, "name"),
           panthera_prop_bool(nub, "AAPL,boot-display"),
           provider && provider->getName() ? provider->getName() : "<null>",
           (provider && provider->getMetaClass()) ? provider->getMetaClass()->getClassName() : "<null>");
}

static IOService *
panthera_find_platform_child(IOService * parent, const char * name)
{
    OSIterator * children = parent->getChildIterator(gIOServicePlane);
    if (!children) {
        return NULL;
    }

    IOService * child;
    while ((child = OSDynamicCast(IOService, children->getNextObject()))) {
        const char * childName = child->getName();
        if (childName && (0 == strcmp(childName, name))) {
            children->release();
            return child;
        }
    }

    children->release();
    return NULL;
}

static void
panthera_ensure_display_nub(AppleI386PlatformExpert * platform)
{
    if (panthera_find_platform_child(platform, "display")) {
        printf("PANTHERA:PE display nub already present under /IOPlatformExpert\n");
        return;
    }

    OSDictionary * dict = OSDictionary::withCapacity(1);
    OSString * name = dict ? OSString::withCString("display") : NULL;
    if (!dict || !name) {
        printf("PANTHERA:PE failed to allocate display nub dictionary\n");
        OSSafeReleaseNULL(name);
        OSSafeReleaseNULL(dict);
        return;
    }

    dict->setObject("IOName", name);
    name->release();

    IOService * nub = platform->createNub(dict);
    dict->release();
    if (!nub) {
        printf("PANTHERA:PE createNub(display) returned null\n");
        return;
    }

    panthera_log_display_nub("ensure-created", nub);
    if (!nub->attach(platform)) {
        printf("PANTHERA:PE attach(display) failed\n");
        nub->release();
        return;
    }

    panthera_log_display_nub("ensure-attached", nub);
    nub->registerService();
    panthera_log_display_nub("ensure-registered", nub);
}

typedef struct {
    UInt8 maxBusNum;
    UInt8 majorVersion;
    UInt8 minorVersion;
    UInt8 BIOSPresent;
    union {
        struct {
            UInt8 configMethod1:1;
            UInt8 configMethod2:1;
            UInt8 specialCycle1:1;
            UInt8 specialCycle2:1;
            UInt8 reserved:4;
        } s;
        UInt8 bits;
    } u_bus;
    UInt8 reserved[3];
} PCI_bus_info_t;

class AppleI386PlatformExpertGlobals
{
public:
    bool isValid;
    AppleI386PlatformExpertGlobals();
    ~AppleI386PlatformExpertGlobals();
};

static AppleI386PlatformExpertGlobals AppleI386PlatformExpertGlobals;

static void
panthera_prime_platform_kext(const char *bundleID)
{
	OSReturn result = OSKext::loadKextWithIdentifier(bundleID, false);
	PANTHERA_PE_TRACE("PANTHERA:PE prime %s result=0x%x\n", bundleID, result);
}

static bool
panthera_usbhid_logs_enabled(void)
{
	static bool initialized = false;
	static bool enabled = false;
	int bootArg = 0;

	if (!initialized) {
		initialized = true;
		enabled = PE_parse_boot_argn("panthera_usbhid", &bootArg, sizeof(bootArg)) && bootArg != 0;
	}

	return enabled;
}

static OSArray *
panthera_make_top_level(void)
{
	static const char * const names[] = {
		"cpu",
		"8259-pic",
		"io-apic",
		"intel-clock",
		"ps2controller",
		"display",
		"bios",
		"pci",
	};
	OSArray *topLevel = OSArray::withCapacity((unsigned int)(sizeof(names) / sizeof(names[0])));
	if (!topLevel) {
		return NULL;
	}

	for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		OSDictionary *dict = OSDictionary::withCapacity(2);
		OSString *name = dict ? OSString::withCString(names[i]) : NULL;
		if (!dict || !name) {
			OSSafeReleaseNULL(name);
			OSSafeReleaseNULL(dict);
			topLevel->release();
			return NULL;
		}
		dict->setObject("IOName", name);
		if (0 == strcmp(names[i], "8259-pic")) {
			dict->setObject("InterruptControllerName", OSString::withCString("8259-pic"));
		}
		if (0 == strcmp(names[i], "io-apic")) {
			dict->setObject("InterruptControllerName", OSString::withCString("8259-pic"));
			dict->setObject("Physical Address", OSNumber::withNumber((uint64_t) 0xfec00000, 64));
			dict->setObject("Destination APIC ID", OSNumber::withNumber((uint64_t) 0, 32));
			dict->setObject("Base Vector Number", OSNumber::withNumber((uint64_t) 0x40, 32));
		}
		topLevel->setObject(dict);
		name->release();
		dict->release();
	}

	return topLevel;
}

AppleI386PlatformExpertGlobals::AppleI386PlatformExpertGlobals()
{
    ResourceLock         = IOLockAlloc();
    bzero(IRQ, sizeof(IRQ));
}

AppleI386PlatformExpertGlobals::~AppleI386PlatformExpertGlobals()
{
    if (ResourceLock) IOLockFree(ResourceLock);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#define super IOPlatformExpert

OSDefineMetaClassAndStructors(AppleI386PlatformExpert, IOPlatformExpert)

IOService * AppleI386PlatformExpert::probe(IOService * 	/* provider */,
                                           SInt32 *		score )
{
    return (this);
}

bool
AppleI386PlatformExpert::init(OSDictionary * propTable)
{
    if (!super::init(propTable))  return false;

    _interruptControllerName = OSSymbol::withString((OSString *) getProperty("InterruptControllerName"));

    return true;
}

bool
AppleI386PlatformExpert::start(IOService * provider)
{
    setBootROMType(kBootROMTypeNewWorld); /* hammer to new world for i386 */

    if (!super::start(provider))
        return false;

    publishResource("SetDeviceInterrupts", this);

    // Install halt/restart handler.
    PE_halt_restart = handlePEHaltRestart;

    registerService();

    return true;
}

bool AppleI386PlatformExpert::configure( IOService * provider )
{
    OSArray *      topLevel;
    OSArray *      fallbackTopLevel = NULL;
    OSDictionary * dict;
    IOService *    nub;

    topLevel = OSDynamicCast( OSArray, getProperty("top-level") );
    if (topLevel) {
        PANTHERA_PE_TRACE("PANTHERA:PE top-level count=%u\n", topLevel->getCount());
    } else {
        PANTHERA_PE_TRACE("PANTHERA:PE top-level missing, synthesizing\n");
        fallbackTopLevel = panthera_make_top_level();
        topLevel = fallbackTopLevel;
    }

    if (topLevel )
    {
        for (unsigned int i = 0; i < topLevel->getCount(); i++)
        {
            dict = OSDynamicCast(OSDictionary, topLevel->getObject(i));
            if (!dict)
                continue;
            nub = createNub( dict );
            if ( 0 == nub )
                continue;
            PANTHERA_PE_TRACE("PANTHERA:PE configure nub=%s\n", nub->getName());
            if (0 == strcmp("display", nub->getName())) {
                panthera_log_display_nub("configure-created", nub);
            }
            nub->attach( this );
            if (0 == strcmp("pci", nub->getName())) {
                panthera_prime_platform_kext("com.apple.driver.AppleI386PCI");
            } else if (0 == strcmp("8259-pic", nub->getName()) ||
                       0 == strcmp("io-apic", nub->getName())) {
                panthera_prime_platform_kext("com.apple.driver.AppleAPIC");
            }
            if (0 == strcmp("display", nub->getName())) {
                panthera_log_display_nub("configure-attached", nub);
            }
            nub->registerService();
            PANTHERA_PE_TRACE("PANTHERA:PE registerService nub=%s\n", nub->getName());
            if (0 == strcmp("display", nub->getName())) {
                panthera_log_display_nub("configure-registered", nub);
            }
            // nub->release();
        }
    }
    panthera_ensure_display_nub(this);
    OSSafeReleaseNULL(fallbackTopLevel);

    return true;
}


IOService * AppleI386PlatformExpert::createNub(OSDictionary * from)
{
    IOService *      nub;
    OSData *		 prop;
    boot_args *      bootArgs;

    nub = super::createNub(from);

    if (nub)
    {
        const char *name = nub->getName();
        
        if (0 == strcmp( "pci", name))
        {
            PCI_bus_info_t info = {};

        	bootArgs = (boot_args *) PE_state.bootArgs;
            info.maxBusNum = (bootArgs->pciConfigSpaceEndBusNumber > 0xffU)
                ? 0xffU
                : (UInt8) bootArgs->pciConfigSpaceEndBusNumber;
            info.majorVersion = 2;
            info.minorVersion = 0;
            info.BIOSPresent = 0;
            info.u_bus.s.configMethod1 = 1;

        	prop = OSData::withBytes(&info, sizeof(info));
        	assert(prop);
        	if (prop) {
                nub->setProperty("pci-bus-info", prop);
                prop->release();
            }
        }
	else if (0 == strcmp("bios", name))
	{
	    setupBIOS(nub);
        }
        else if (0 == strcmp("display", name))
        {
            panthera_log_display_nub("create-before-boot-flag", nub);
            if ((PE_state.video.v_display == GRAPHICS_MODE)
             && (PE_state.video.v_baseAddr != 0)) {
                nub->setProperty("AAPL,boot-display", kOSBooleanTrue);
                printf("PANTHERA:PE display nub boot-display base=%#x width=%u height=%u depth=%u rowBytes=%u\n",
                       (unsigned int) PE_state.video.v_baseAddr,
                       (unsigned int) PE_state.video.v_width,
                       (unsigned int) PE_state.video.v_height,
                       (unsigned int) PE_state.video.v_depth,
                       (unsigned int) PE_state.video.v_rowBytes);
            } else {
                printf("PANTHERA:PE display nub skipped mode=%u base=%#x\n",
                       (unsigned int) PE_state.video.v_display,
                       (unsigned int) PE_state.video.v_baseAddr);
            }
            panthera_log_display_nub("create-after-boot-flag", nub);
        }
        else if (0 == strcmp("8259-pic", name))
        {
            setupPIC(nub);
        }
    }

    return (nub);
}

void
AppleI386PlatformExpert::setupPIC(IOService *nub)
{
    int            i;
    OSArray *      controller;
    OSArray *      specifier;
    OSData *       tmpData;
    long           tmpLong;

    //
    // For the moment.. assume a classic 8259 interrupt controller
    // with 16 interrupts.
    //
    // Later, this will be changed to detect a APIC and/or MP-Table
    // and then will set the nubs appropriately.

    // Create the interrupt specifer array.
    specifier = OSArray::withCapacity(kSystemIRQCount);
    assert(specifier);
    for (i = 0; i < kSystemIRQCount; i++) {
        tmpLong = i;
        tmpData = OSData::withBytes(&tmpLong, sizeof(tmpLong));
        specifier->setObject(tmpData);
        tmpData->release();
    }

    // Create the interrupt controller array.
    controller = OSArray::withCapacity(kSystemIRQCount);
    assert(controller);
    for (i = 0; i < kSystemIRQCount; i++)
        controller->setObject(_interruptControllerName);

    // Put the two arrays into the property table.
    nub->setProperty(gIOInterruptControllersKey, controller);
    nub->setProperty(gIOInterruptSpecifiersKey, specifier);

    // Release the arrays after being added to the property table.
    specifier->release();
    controller->release();
}

void
AppleI386PlatformExpert::setupBIOS(IOService *nub)
{
    (void) nub;
}

bool
AppleI386PlatformExpert::matchNubWithPropertyTable(IOService *    nub,
					                               OSDictionary * propTable )
{
    OSString * nameProp;
    OSString * match;
    const char *nubName = nub->getName();
    const char *ioClass = "<null>";
    const char *providerClass = "<null>";
    const char *matchName = "<null>";
    int bootDisplayExpected = 0;

    if (OSString *value = OSDynamicCast(OSString, propTable->getObject("IOClass")))
        ioClass = value->getCStringNoCopy();
    if (OSString *value = OSDynamicCast(OSString, propTable->getObject(gIOProviderClassKey)))
        providerClass = value->getCStringNoCopy();
    if (OSString *value = OSDynamicCast(OSString, propTable->getObject(gIONameMatchKey)))
        matchName = value->getCStringNoCopy();
    if (OSDictionary *propertyMatch = OSDynamicCast(OSDictionary, propTable->getObject(gIOPropertyMatchKey)))
        bootDisplayExpected = (propertyMatch->getObject("AAPL,boot-display") != 0);

    if ((nubName && 0 == strcmp("display", nubName))
     || (0 == strcmp("IOBootFramebuffer", ioClass))
     || (0 == strcmp("display", matchName))) {
        printf("PANTHERA:PE match display nub-class=%s nub-name=%s IOName=%s reg-name=%s boot-display=%d personality-class=%s provider-class=%s match-name=%s expects-boot-display=%d\n",
               nub->getMetaClass() ? nub->getMetaClass()->getClassName() : "<null>",
               nubName ? nubName : "<null>",
               panthera_prop_string(nub, "IOName"),
               panthera_prop_string(nub, "name"),
               panthera_prop_bool(nub, "AAPL,boot-display"),
               ioClass,
               providerClass,
               matchName,
               bootDisplayExpected);
    }

    if (0 == (nameProp = (OSString *) nub->getProperty(gIONameKey)))
    {
        if ((nubName && 0 == strcmp("display", nubName))
         || (0 == strcmp("IOBootFramebuffer", ioClass))
         || (0 == strcmp("display", matchName))) {
            printf("PANTHERA:PE match display -> missing gIONameKey\n");
        }
        return (false);
    }

    if ( 0 == (match = (OSString *) propTable->getObject(gIONameMatchKey)))
    {
        if ((nubName && 0 == strcmp("display", nubName))
         || (0 == strcmp("IOBootFramebuffer", ioClass))
         || (0 == strcmp("display", matchName))) {
            printf("PANTHERA:PE match display -> missing IONameMatch\n");
        }
        return (false);
    }

    if ((nubName && 0 == strcmp("display", nubName))
     || (0 == strcmp("IOBootFramebuffer", ioClass))
     || (0 == strcmp("display", matchName))) {
        printf("PANTHERA:PE match display compare name=%s vs match=%s result=%d\n",
               nameProp->getCStringNoCopy(),
               match->getCStringNoCopy(),
               match->isEqualTo(nameProp));
    }

    return (match->isEqualTo( nameProp ));
}

bool AppleI386PlatformExpert::getMachineName( char * name, int maxLength )
{
    strncpy( name, "x86", maxLength );

    return (true);
}


bool AppleI386PlatformExpert::getModelName( char * name, int maxLength )
{
    i386_cpu_info_t *cpuid_cpu_info = cpuid_info();

    if (cpuid_cpu_info->cpuid_brand_string[0] != '\0') {
        strncpy(name, cpuid_cpu_info->cpuid_brand_string, maxLength);
    } else {
        strncpy(name, cpuid_cpu_info->cpuid_model_string, maxLength);
    }

    name[maxLength - 1] = '\0';

    return (true);
}


int AppleI386PlatformExpert::handlePEHaltRestart( unsigned int type )
{
    int ret = -1;
	
    switch ( type )
    {
        case kPERestartCPU:
            // ponytail: legacy-PC i8042 reset; add ACPI reset for machines without it.
            // Wait at most 100 ms for the controller input buffer to clear.
            for (unsigned int poll = 0; poll < 1000; ++poll) {
                if ((inb(0x64) & 0x02) == 0) {
                    outb(0x64, 0xfe); // Pulse the controller's CPU reset output.
                    IODelay(50000);
                    break;
                }
                IODelay(100);
            }
            printf("PANTHERA: i8042 restart did not complete\n");
            break;

        case kPEHaltCPU:
        default:
            break;
    }

    return ret;
}


bool AppleI386PlatformExpert::setNubInterruptVectors(
                                                     IOService *  nub,
                                                     const UInt32 vectors[],
                                                     UInt32       vectorCount )
{
    OSArray * controller = 0;
    OSArray * specifier  = 0;
    bool      success = false;
    // PCI INTx is level/low; ISA child nubs use edge/high.
    const UInt32 flags = nub->metaCast("IOPCIDevice")
        ? kPantheraInterruptTriggerModeLevel | kPantheraInterruptPolarityLow
            | kPantheraInterruptIsShareable
        : 0;

    if ( vectorCount == 0 )
    {
        nub->removeProperty( gIOInterruptControllersKey );
        nub->removeProperty( gIOInterruptSpecifiersKey );
        return true;
    }

    // Create the interrupt specifer and controller arrays.

    specifier  = OSArray::withCapacity( vectorCount );
    controller = OSArray::withCapacity( vectorCount );
    if (!specifier || !controller) goto done;

    for ( UInt32 i = 0; i < vectorCount; i++ )
    {
        UInt32 specifierWords[2] = {
            vectors[i],
            flags
        };
        OSData * data = OSData::withBytes(specifierWords, sizeof(specifierWords));
        specifier->setObject( data );
        controller->setObject( _interruptControllerName );
        if (data) data->release();
    }

    nub->setProperty( gIOInterruptControllersKey, controller );
    nub->setProperty( gIOInterruptSpecifiersKey,  specifier  );
    success = true;

done:
        if (specifier)  specifier->release();
    if (controller) controller->release();
    return success;
}

bool AppleI386PlatformExpert::setNubInterruptVector( IOService * nub,
                                                     UInt32      vector )
{
    return setNubInterruptVectors( nub, &vector, 1 );
}


IOReturn AppleI386PlatformExpert::callPlatformFunction(
                                                       const OSSymbol * functionName,
                                                       bool waitForFunction,
                                                       void * param1, void * param2,
                                                       void * param3, void * param4 )
{
    bool ok;

    if ( functionName->isEqualTo( "SetDeviceInterrupts" ) )
    {
        IOService * nub         = (IOService *) param1;
        UInt32 *    vectors     = (UInt32 *)    param2;
        UInt32      vectorCount = (UInt32)      (uintptr_t) param3;
        bool        exclusive   = (bool)        (uintptr_t) param4;

        if (panthera_usbhid_logs_enabled()) {
            printf("PANTHERA:PE SetDeviceInterrupts nub=%s class=%s vectorCount=%u exclusive=%u vector0=%u\n",
                   nub && nub->getName() ? nub->getName() : "<null>",
                   (nub && nub->getMetaClass()) ? nub->getMetaClass()->getClassName() : "<null>",
                   (unsigned int) vectorCount,
                   exclusive ? 1U : 0U,
                   (vectors && vectorCount) ? (unsigned int) vectors[0] : 0U);
        }

        if (vectorCount != 1) return kIOReturnBadArgument;

        ok = reserveSystemInterrupt( nub, vectors[0], exclusive );
        if (ok == false) {
            if (panthera_usbhid_logs_enabled()) {
                printf("PANTHERA:PE SetDeviceInterrupts reserve failed irq=%u\n",
                       (vectors && vectorCount) ? (unsigned int) vectors[0] : 0U);
            }
            return kIOReturnNoResources;
        }

        ok = setNubInterruptVector( nub, vectors[0] );
        if (ok == false)
            releaseSystemInterrupt( nub, vectors[0], exclusive );

        if (panthera_usbhid_logs_enabled()) {
            printf("PANTHERA:PE SetDeviceInterrupts result=0x%x irq=%u controller=%s\n",
                   ok ? kIOReturnSuccess : kIOReturnNoMemory,
                   (vectors && vectorCount) ? (unsigned int) vectors[0] : 0U,
                   _interruptControllerName ? _interruptControllerName->getCStringNoCopy() : "<null>");
        }

        return ( ok ? kIOReturnSuccess : kIOReturnNoMemory );

    }
    else if ( functionName->isEqualTo( "SetBusClockRateMHz" ) )
    {
        UInt32     rateInMHz       = (UInt32)      (uintptr_t) param1;

        gPEClockFrequencyInfo.bus_clock_rate_hz = (rateInMHz * 1000000);

        return kIOReturnSuccess;
    }
    else if ( functionName->isEqualTo( "SetCPUClockRateMHz" ) )
    {
        UInt32     rateInMHz       = (UInt32)      (uintptr_t) param1;

        gPEClockFrequencyInfo.cpu_clock_rate_hz = (rateInMHz * 1000000);

        return kIOReturnSuccess;
    }
    
    return super::callPlatformFunction( functionName, waitForFunction,
                                        param1, param2, param3, param4 );
}



//---------------------------------------------------------------------------

bool AppleI386PlatformExpert::reserveSystemInterrupt( IOService * client,
                                                      UInt32      vectorNumber,
                                                      bool        exclusive )
{
    bool  ok = false;

    if ( vectorNumber >= kSystemIRQCount ) return ok;

    IOLockLock( ResourceLock );

    if ( exclusive )
    {
        if (IRQ[vectorNumber].status == kIRQAvailable)
        {
            IRQ[vectorNumber].status = kIRQExclusive;
            IRQ[vectorNumber].consumers = 1;
            ok = true;
        }
    }
    else
    {
        if (IRQ[vectorNumber].status == kIRQAvailable ||
            IRQ[vectorNumber].status == kIRQSharable)
        {
            IRQ[vectorNumber].status = kIRQSharable;
            IRQ[vectorNumber].consumers++;
            ok = true;
        }
    }

    IOLockUnlock( ResourceLock );

    return ok;
}

void AppleI386PlatformExpert::releaseSystemInterrupt( IOService * client,
                                                      UInt32      vectorNumber,
                                                      bool        exclusive )
{
    if ( vectorNumber >= kSystemIRQCount ) return;

    IOLockLock( ResourceLock );

    if ( exclusive )
    {
        if (IRQ[vectorNumber].status == kIRQExclusive)
        {
            IRQ[vectorNumber].status = kIRQAvailable;
            IRQ[vectorNumber].consumers = 0;
        }
    }
    else
    {
        if (IRQ[vectorNumber].status == kIRQSharable &&
            --IRQ[vectorNumber].consumers == 0)
        {
            IRQ[vectorNumber].status = kIRQAvailable;
        }
    }

    IOLockUnlock( ResourceLock );
}
