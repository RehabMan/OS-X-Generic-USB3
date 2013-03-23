/*
 *  GenericUSBXHCI.cpp
 *  GenericUSBXHCI
 *
 *  Created by Zenith432 on December 5th 2012.
 *  Copyright 2012-2013 Zenith432. All rights reserved.
 *
 */

#include "GenericUSBXHCI.h"
#include "GenericUSBXHCIUserClient.h"
#include <IOKit/IOTimerEventSource.h>
#include <libkern/version.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3
OSDefineMetaClassAndFinalStructors(GenericUSBXHCI, IOUSBControllerV3);

#ifndef kIOUSBMessageMuxFromEHCIToXHCI
#define	kIOUSBMessageMuxFromEHCIToXHCI				iokit_usb_msg(0xe1)		// 0xe00040e1  Message from the EHCI HC for ports mux transition from EHCI to XHCI
#define	kIOUSBMessageMuxFromXHCIToEHCI				iokit_usb_msg(0xe2)		// 0xe00040e2  Message from the EHCI HC for ports mux transition from XHCI to EHCI
#endif

static __used char const copyright[] = "Copyright 2012-2013 Zenith432";

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1075
#error Target OS version must be 10.7.5 or above
#endif

#pragma mark -
#pragma mark IOService
#pragma mark -

bool CLASS::willTerminate(IOService* provider, IOOptionBits options)
{
	if (_expansionData && _watchdogTimerActive && _watchdogUSBTimer)
		_watchdogUSBTimer->setTimeoutUS(1U);
	return super::willTerminate(provider, options);
}

bool CLASS::terminate(IOOptionBits options)
{
	m_invalid_regspace = true;
	return super::terminate(options);
}

IOReturn CLASS::message(UInt32 type, IOService* provider, void* argument)
{
	IOReturn rc;
	uint8_t flag;

	rc = super::message(type, provider, argument);
	switch (type) {
		case kIOUSBMessageHubPortDeviceDisconnected:
			if (m_invalid_regspace ||
				(gUSBStackDebugFlags & kUSBDisableMuxedPortsMask) ||
				!_v2ExpansionData ||
				isInactive() ||
				!(_errataBits & kErrataIntelPCIRoutingExtension))
				return rc;
			HCSelectWithMethod(static_cast<char const*>(argument));
			return rc;
		case kIOUSBMessageMuxFromEHCIToXHCI:
			flag = 1U;
			break;
		case kIOUSBMessageMuxFromXHCIToEHCI:
			flag = 0U;
			break;
		default:
			return kIOReturnUnsupported;
	}
	if (m_invalid_regspace ||
		(gUSBStackDebugFlags & kUSBDisableMuxedPortsMask) ||
		!_v2ExpansionData ||
		isInactive() ||
		!(_errataBits & kErrataIntelPCIRoutingExtension))
		return rc;
	HCSelect(static_cast<uint8_t>(reinterpret_cast<size_t>(argument)), flag);
	return rc;
}

IOReturn CLASS::newUserClient(task_t owningTask, void* securityID, UInt32 type, IOUserClient ** handler)
{
	IOUserClient *client;

	if (type != kGUXUCType)
		return kIOReturnUnsupported;
	if (!handler)
		return kIOReturnBadArgument;

	client = OSTypeAlloc(GenericUSBXHCIUserClient);
	if (!client)
		return kIOReturnNoMemory;
	if (!client->initWithTask(owningTask, securityID, type)) {
		client->release();
		return kIOReturnInternalError;
	}
	if (!client->attach(this)) {
		client->release();
		return kIOReturnInternalError;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnInternalError;
	}
	*handler = client;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark IORegistryEntry
#pragma mark -

bool CLASS::init(OSDictionary* dictionary)
{
	/*
	 * (64-bit/32-bit)
	 * sizeof(USBXHCI)           = 0x23B80/0x1A640
	 * sizeof(IOUSBControllerV3) = 680/492
	 * sizeof(IOUSBControllerV2) = 440/360
	 * sizeof(IOUSBController)   = 176/100
	 * sizeof(IOUSBBus)          = 136/80
	 *
	 * Inheritance
	 *   XHCIIsochEndpoint: public IOUSBControllerIsochEndpoint
	 *   XHCIIsochTransferDescriptor: public IOUSBControllerIsochListElement
	 *   XHCIAsyncEndpoint: public OSObject
	 *   XHCIAsyncTransferDescriptor: public OSObject
	 *   RootHubPortTable: public OSObject
	 *   TTBandwidthTable: public OSObject
	 */
	if (!super::init(dictionary)) {
		IOLog("%s: super::init failed\n", __FUNCTION__);
		return false;
	}
	_controllerSpeed = kUSBDeviceSpeedSuper;
	_isochScheduleLock = IOSimpleLockAlloc();
	if (!_isochScheduleLock) {
		IOLog("%s: Unable to allocate SimpleLock\n", __FUNCTION__);
		return false;
	}
	_uimInitialized = false;
	_myBusState = kUSBBusStateReset;
	return true;
}

void CLASS::free(void)
{
	/*
	 * Note: _wdhLock is not freed in original code
	 */
	if (_isochScheduleLock) {
		IOSimpleLockFree(_isochScheduleLock);
		_isochScheduleLock = 0;
	}
	super::free();
}

#pragma mark -
#pragma mark Module Init
#pragma mark -

extern "C"
{

__attribute__((visibility("hidden")))
int gux_log_level = 1;

__attribute__((visibility("hidden")))
int gux_options = 0;

__attribute__((visibility("hidden")))
kern_return_t Startup(kmod_info_t* ki, void * d)
{
	uint32_t v;

    if (GetKernelVersion() < MakeKernelVersion(11,4,2)) {
		IOLog("Darwin 11.4.2 (OS X 10.7.5) or later required for GenericUSBXHCI\n");
		return KERN_FAILURE;
    }
	if (PE_parse_boot_argn("-gux_nosleep", &v, sizeof v))
		gux_options |= GUX_OPTION_NO_SLEEP;
	if (PE_parse_boot_argn("-gux_defer_usb2", &v, sizeof v))
		gux_options |= GUX_OPTION_DEFER_INTEL_EHC_PORTS;
	if (PE_parse_boot_argn("-gux_no_idle", &v, sizeof v))
		gux_options |= GUX_OPTION_NO_INTEL_IDLE;
	if (PE_parse_boot_argn("-gux_nomsi", &v, sizeof v))
		gux_options |= GUX_OPTION_NO_MSI;
	if (PE_parse_boot_argn("gux_log", &v, sizeof v))
		gux_log_level = static_cast<int>(v);
	return KERN_SUCCESS;
}

__attribute__((visibility("hidden")))
void ListOptions(PrintSink* pSink)
{
	/*
	 * Keep this in sync with Startup
	 */
	pSink->print("Kernel Flags\n");
	pSink->print("  -gux_nosleep: Disable XHCI suspend/resume method, and use reset-on-resume (forces USB Bus re-enumeration)\n");
	pSink->print("  -gux_nomsi: Disable MSI and use pin interrupt (if available)\n");
	pSink->print("  -gux_defer_usb2: For Intel Series 7/C210 only - Switch USB 2.0 protocol ports from xHC to EHC\n");
	pSink->print("  -gux_no_idle: For Intel Series 7/C210 only - Disable Doze mode\n");
	pSink->print("  gux_log=n: Set logging level to n.  Available levels 1 - normal, 2 - higher\n");
}

}
