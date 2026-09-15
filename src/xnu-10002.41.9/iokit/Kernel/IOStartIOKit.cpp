/*
 * Copyright (c) 1998-2010 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 *
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */

#include <libkern/c++/OSUnserialize.h>
#include <libkern/c++/OSKext.h>
#include <libkern/section_keywords.h>
#include <libkern/version.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOCatalogue.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOKernelReporters.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOKitDebug.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <IOKit/pwr_mgt/IOPMinformeeList.h>
#include <IOKit/IOStatisticsPrivate.h>
#include <IOKit/IOKitKeysPrivate.h>
#include <IOKit/IOInterruptAccountingPrivate.h>
#include <IOKit/assert.h>
#include <sys/conf.h>
extern "C" {
#include <kern/debug.h>
}

#include "IOKitKernelInternal.h"

const OSSymbol * gIOProgressBackbufferKey;
OSSet *          gIORemoveOnReadProperties;

extern "C" {
void InitIOKit(void *dtTop);
void ConfigureIOKit(void);
void StartIOKitMatching(void);
void IORegistrySetOSBuildVersion(char * build_version);
void IORecordProgressBackbuffer(void * buffer, size_t size, uint32_t theme);

extern void OSlibkernInit(void);
extern void panthera_debugcon_write_public(const char *string);

void iokit_post_constructor_init(void);

SECURITY_READ_ONLY_LATE(static IOPlatformExpertDevice*) gRootNub;

#include <kern/clock.h>
#include <sys/time.h>

void
IOKitInitializeTime( void )
{
	mach_timespec_t         t;

	t.tv_sec = 30;
	t.tv_nsec = 0;

// RTC is not present on this target
#ifndef BCM2837
	IOService::waitForService(
		IOService::resourceMatching("IORTC"), &t );
#endif
#if defined(__i386__) || defined(__x86_64__)
	IOService::waitForService(
		IOService::resourceMatching("IONVRAM"), &t );
#endif

	clock_initialize_calendar();
}

void
iokit_post_constructor_init(void)
{
	IORegistryEntry *           root;
	OSObject *                  obj;

	panthera_debugcon_write_public("PANTHERA:IOK post_ctor cpu\n");
	IOCPUInitialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor cpu done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor platform_actions\n");
	IOPlatformActionsInitialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor platform_actions done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor registry\n");
	root = IORegistryEntry::initialize();
	assert( root );
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor registry done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor service\n");
	IOService::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor service done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor catalogue\n");
	IOCatalogue::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor catalogue done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor statistics\n");
	IOStatistics::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor statistics done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor oskext\n");
	OSKext::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor oskext done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor userclient\n");
	IOUserClient::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor userclient done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor memdesc\n");
	IOMemoryDescriptor::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor memdesc done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor rootparent\n");
	IORootParent::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor rootparent done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor reporter\n");
	IOReporter::initialize();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor reporter done\n");

	// Initializes IOPMinformeeList class-wide shared lock
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor pminformee\n");
	IOPMinformeeList::getSharedRecursiveLock();
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor pminformee done\n");

	panthera_debugcon_write_public("PANTHERA:IOK post_ctor buildvers\n");
	obj = OSString::withCString( version );
	assert( obj );
	if (obj) {
		root->setProperty( kIOKitBuildVersionKey, obj );
		obj->release();
	}
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor buildvers done\n");
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor diagnostics\n");
	obj = IOKitDiagnostics::diagnostics();
	if (obj) {
		root->setProperty( kIOKitDiagnosticsKey, obj );
		obj->release();
	}
	panthera_debugcon_write_public("PANTHERA:IOK post_ctor done\n");
}

/*****
 * Pointer into bootstrap KLD segment for functions never used past startup.
 */
void (*record_startup_extensions_function)(void) = NULL;

void
InitIOKit(void *dtTop)
{
	panthera_debugcon_write_public("PANTHERA:IOK enter\n");
	// Compat for boot-args
	gIOKitTrace |= (gIOKitDebug & kIOTraceCompatBootArgs);

	//
	// Have to start IOKit environment before we attempt to start
	// the C++ runtime environment.  At some stage we have to clean up
	// the initialisation path so that OS C++ can initialise independantly
	// of iokit basic service initialisation, or better we have IOLib stuff
	// initialise as basic OS services.
	//
	panthera_debugcon_write_public("PANTHERA:IOK IOLibInit\n");
	IOLibInit();
	panthera_debugcon_write_public("PANTHERA:IOK IOLibInit done\n");
	panthera_debugcon_write_public("PANTHERA:IOK OSlibkernInit\n");
	OSlibkernInit();
	panthera_debugcon_write_public("PANTHERA:IOK OSlibkernInit done\n");
	panthera_debugcon_write_public("PANTHERA:IOK IOMachPortInitialize\n");
	IOMachPortInitialize();
	panthera_debugcon_write_public("PANTHERA:IOK IOMachPortInitialize done\n");

	panthera_debugcon_write_public("PANTHERA:IOK symbols\n");
	gIOProgressBackbufferKey  = OSSymbol::withCStringNoCopy(kIOProgressBackbufferKey);
	gIORemoveOnReadProperties = OSSet::withObjects((const OSObject **) &gIOProgressBackbufferKey, 1);
	panthera_debugcon_write_public("PANTHERA:IOK symbols done\n");

	panthera_debugcon_write_public("PANTHERA:IOK interruptAccountingInit\n");
	interruptAccountingInit();
	panthera_debugcon_write_public("PANTHERA:IOK interruptAccountingInit done\n");

	panthera_debugcon_write_public("PANTHERA:IOK rootnub new\n");
	gRootNub = new IOPlatformExpertDevice;
	if (__improbable(gRootNub == NULL)) {
		panic("Failed to allocate IOKit root nub");
	}
	panthera_debugcon_write_public("PANTHERA:IOK rootnub init\n");
	bool ok = gRootNub->init(dtTop);
	if (__improbable(!ok)) {
		panic("Failed to initialize IOKit root nub");
	}
	panthera_debugcon_write_public("PANTHERA:IOK rootnub attach\n");
	gRootNub->attach(NULL);
	panthera_debugcon_write_public("PANTHERA:IOK rootnub attach done\n");

	/* If the bootstrap segment set up a function to record startup
	 * extensions, call it now.
	 */
	if (record_startup_extensions_function) {
		panthera_debugcon_write_public("PANTHERA:IOK record_startup_extensions\n");
		record_startup_extensions_function();
	}
	panthera_debugcon_write_public("PANTHERA:IOK done\n");
}

void
ConfigureIOKit(void)
{
	assert(gRootNub != NULL);
	gRootNub->configureDefaults();
}

void
StartIOKitMatching(void)
{
	SOCD_TRACE_XNU(START_IOKIT);
	assert(gRootNub != NULL);
	/* Panthera: inject boot kext personalities into the catalogue BEFORE
	 * registerService() fires matching.  Without a prelinked kernel cache,
	 * the catalogue is empty at this point and matching would find nothing. */
	PANTHERA_TRACE(printf("PANTHERA:MATCH injecting boot kext personalities\n"));
	OSKext::sendAllKextPersonalitiesToCatalog(true /* start matching */);
	PANTHERA_TRACE(printf("PANTHERA:MATCH personalities injected, starting matching\n"));
	bool ok = gRootNub->startIOServiceMatching();
	PANTHERA_TRACE(printf("PANTHERA:MATCH startIOServiceMatching returned %d\n", ok));
	if (__improbable(!ok)) {
		panic("Failed to start IOService matching");
	}

#if !NO_KEXTD
	if (OSKext::iokitDaemonAvailable()) {
		/* Add a busy count to keep the registry busy until the IOKit daemon has
		 * completely finished launching. This is decremented when the IOKit daemon
		 * messages the kernel after the in-kernel linker has been
		 * removed and personalities have been sent.
		 */
		IOService::getServiceRoot()->adjustBusy(1);
	}
#endif
}

void
IORegistrySetOSBuildVersion(char * build_version)
{
	IORegistryEntry * root = IORegistryEntry::getRegistryRoot();

	if (root) {
		if (build_version) {
			root->setProperty(kOSBuildVersionKey, build_version);
		} else {
			root->removeProperty(kOSBuildVersionKey);
		}
	}

	return;
}

void
IORecordProgressBackbuffer(void * buffer, size_t size, uint32_t theme)
{
	IORegistryEntry * chosen;

	if (((unsigned int) size) != size) {
		return;
	}
	if ((chosen = IORegistryEntry::fromPath(kIODeviceTreePlane ":/chosen"))) {
		chosen->setProperty(kIOProgressBackbufferKey, buffer, (unsigned int) size);
		chosen->setProperty(kIOProgressColorThemeKey, theme, 32);

		chosen->release();
	}
}
}; /* extern "C" */
