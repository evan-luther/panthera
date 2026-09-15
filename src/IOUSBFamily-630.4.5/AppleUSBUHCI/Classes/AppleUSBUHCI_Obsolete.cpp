/*
 * Copyright © 2004-2007, 2012 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
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


#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/usb/IOUSBLog.h>

#include "AppleUSBUHCI.h"


// method in 1.8 and 1.8.1
IOReturn
AppleUSBUHCI::UIMCreateInterruptTransfer(
                                         short				functionNumber,
                                         short				endpointNumber,
                                         IOUSBCompletion                completion,
                                         IOMemoryDescriptor *		CBP,
                                         bool				bufferRounding,
                                         UInt32				bufferSize,
                                         short				direction)
{
#pragma unused (functionNumber, endpointNumber, completion, CBP, bufferRounding, bufferSize, direction)
    // deprecated
	USBLog(1, "AppleUSBUHCI::UIMCreateInterruptTransfer - deprecated");
    return kIOReturnBadArgument;
}

// method in 1.8 and 1.8.1
IOReturn
AppleUSBUHCI::UIMCreateBulkTransfer(
                                    short				functionNumber,
                                    short				endpointNumber,
                                    IOUSBCompletion			completion,
                                    IOMemoryDescriptor *		CBP,
                                    bool				bufferRounding,
                                    UInt32				bufferSize,
                                    short				direction)
{
#pragma unused (functionNumber, endpointNumber, completion, CBP, bufferRounding, bufferSize, direction)
    // deprecated
	USBLog(1, "AppleUSBUHCI::UIMCreateBulkTransfer - deprecated");
    return kIOReturnBadArgument;
}

IOReturn
AppleUSBUHCI::UIMCreateControlTransfer(
                                       short				functionNumber,
                                       short				endpointNumber,
                                       IOUSBCommand*			command,
                                       void *				CBP,
                                       bool				bufferRounding,
                                       UInt32				bufferSize,
                                       short				direction)
{
	IOReturn					err = kIOReturnSuccess;
	IOBufferMemoryDescriptor *	requestMem = NULL;
	IOMemoryDescriptor *			stageMem = NULL;
	IODMACommand *				dmaCommand = NULL;
	bool						isSetupStage = (direction == kUSBNone);
	bool						isDescriptorStage = (!isSetupStage && (command->GetSelector() == DEVICE_REQUEST_DESC));

	if (CBP && bufferSize)
	{
		if (isDescriptorStage)
		{
			stageMem = (IOMemoryDescriptor *)CBP;
		}
		else
		{
			requestMem = IOBufferMemoryDescriptor::withOptions(kIOMemoryUnshared | kIODirectionOutIn, bufferSize);
			if (!requestMem)
			{
				USBLog(1, "AppleUSBUHCI::UIMCreateControlTransfer - no setup/data stage buffer");
				return kIOReturnNoMemory;
			}
			if (direction != kUSBIn)
			{
				bcopy(CBP, requestMem->getBytesNoCopy(), bufferSize);
			}
			stageMem = requestMem;
			if (isSetupStage)
			{
				command->SetRequestMemoryDescriptor(requestMem);
			}
			else
			{
				command->SetBufferMemoryDescriptor(requestMem);
			}
		}

		dmaCommand = command->GetDMACommand();
		if (!dmaCommand)
		{
			if (isSetupStage)
				command->SetRequestMemoryDescriptor(NULL);
			else
				command->SetBufferMemoryDescriptor(NULL);
			if (requestMem)
				requestMem->release();
			return kIOReturnNoResources;
		}
		if (dmaCommand->getMemoryDescriptor())
		{
			dmaCommand->clearMemoryDescriptor();
		}
		err = dmaCommand->setMemoryDescriptor(stageMem);
		if (err != kIOReturnSuccess)
		{
			if (isSetupStage)
				command->SetRequestMemoryDescriptor(NULL);
			else
				command->SetBufferMemoryDescriptor(NULL);
			if (requestMem)
				requestMem->release();
			return err;
		}
	}

	err = UIMCreateControlTransfer(functionNumber,
	                              endpointNumber,
	                              command,
	                              stageMem,
	                              bufferRounding,
	                              bufferSize,
	                              direction);

	if ((err != kIOReturnSuccess) && stageMem)
	{
		if (dmaCommand && dmaCommand->getMemoryDescriptor())
		{
			dmaCommand->clearMemoryDescriptor();
		}
		if (isSetupStage)
		{
			command->SetRequestMemoryDescriptor(NULL);
		}
		else
		{
			command->SetBufferMemoryDescriptor(NULL);
		}
		if (requestMem)
		{
			requestMem->complete();
			requestMem->release();
		}
	}

	return err;
}

// method in 1.8 and 1.8.1
IOReturn
AppleUSBUHCI::UIMCreateControlTransfer(
                                       short				functionNumber,
                                       short				endpointNumber,
                                       IOUSBCompletion			completion,
                                       IOMemoryDescriptor *		CBP,
                                       bool				bufferRounding,
                                       UInt32				bufferSize,
                                       short				direction)
{
#pragma unused (functionNumber, endpointNumber, completion, CBP, bufferRounding, bufferSize, direction)
    // deprecated
	USBLog(1, "AppleUSBUHCI::UIMCreateControlTransfer - deprecated");
    return kIOReturnBadArgument;
}

// method in 1.8 and 1.8.1
IOReturn
AppleUSBUHCI::UIMCreateControlTransfer(
                                       short				functionNumber,
                                       short				endpointNumber,
                                       IOUSBCompletion			completion,
                                       void *				CBP,
                                       bool				bufferRounding,
                                       UInt32				bufferSize,
                                       short				direction)
{
#pragma unused (functionNumber, endpointNumber, completion, CBP, bufferRounding, bufferSize, direction)
    // deprecated
    return kIOReturnIPCError;
}
