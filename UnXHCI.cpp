//
//  UnXHCI.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 26th, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include <IOKit/usb/IOUSBRootHubDevice.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Stuff Extraneous to the xHC Specification
#pragma mark (Mostly Intel-Specific)
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::EnableXHCIPorts(void)
{
	uint32_t v1, v2, v3, v4;

	if (!(_errataBits & kErrataIntelPCIRoutingExtension))
		return;
	v1 = _device->configRead32(PCI_XHCI_INTEL_XUSB2PR);
	if (v1 == UINT32_MAX) {
		m_invalid_regspace = true;
		return;
	}
	v2 = _device->configRead32(PCI_XHCI_INTEL_XUSB2PRM);
	if (v2 == UINT32_MAX) {
		m_invalid_regspace = true;
		return;
	}
	v3 = _device->configRead32(PCI_XHCI_INTEL_USB3_PSSEN);
	if (v3 == UINT32_MAX) {
		m_invalid_regspace = true;
		return;
	}
	v4 = _device->configRead32(PCI_XHCI_INTEL_USB3PRM);
	if (v4 == UINT32_MAX) {
		m_invalid_regspace = true;
		return;
	}
	v1 &= ~v2;
	if (!(gux_options & GUX_OPTION_DEFER_INTEL_EHC_PORTS))
		v1 |= v2;
	_device->configWrite32(PCI_XHCI_INTEL_XUSB2PR, v1);
	_device->configWrite32(PCI_XHCI_INTEL_USB3_PSSEN, (v3 & ~v4) | v4);
}

__attribute__((visibility("hidden")))
bool CLASS::DiscoverMuxedPorts(void)
{
	OSObject* o;
	uint32_t n;
	uint8_t t;
	char* string_buf;

	if (_rootHubDeviceSS == 0)
		goto done;
	if (_muxedPortsExist)
		goto done;
	if (!(_errataBits & kErrataIntelPCIRoutingExtension))
		goto done;
	o = _rootHubDeviceSS->getProperty(kUSBDevicePropertyLocationID);
	if (!o)
		goto done;
	n = static_cast<OSNumber*>(o)->unsigned32BitValue();
	_providerACPIDevice = CopyACPIDevice(_device);
	if (!_providerACPIDevice)
		goto done;
	string_buf = &_muxName[0];
	t = 0U;
	for (uint32_t port = 1U; port != kMaxPorts; ++port) {
		if (IsPortMuxed(_device, port, n, string_buf))
			++t;
		string_buf += 5;
	}
	if (t)
		_muxedPortsExist = true;
done:
	_muxedPortsSearched = true;
	return _muxedPortsExist;
}

#if 0
/*
 * Does this really work?
 * It's not documented anywhere
 */
__attribute__((noinline, visibility("hidden")))
void CLASS::DisableComplianceMode(void)
{
	if ((_errataBits & (kErrataFrescoLogic | kErrataIntelPantherPoint)) &&
		!(_errataBits & kErrataDisableComplianceExtension)) {
		_unknown3 = reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + 0x80EC;
		*_unknown3 |= 1U;
	}
}

__attribute__((noinline, visibility("hidden")))
void CLASS::EnableComplianceMode(void)
{
	if ((_errataBits & (kErrataFrescoLogic | kErrataIntelPantherPoint)) &&
		!(_errataBits & kErrataDisableComplianceExtension)) {
		_unknown3 = reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + 0x80EC;
		*_unknown3 &= ~1U;
	}
}
#endif

__attribute__((visibility("hidden")))
IOReturn CLASS::HCSelect(uint8_t arg, uint8_t flag)
{
	static char const xHCMuxedPorts[80] = "XHCA\0XHCB\0XHCC\0XHCD";
	char const* method;

	if (!_muxedPortsSearched)
		DiscoverMuxedPorts();
	if (!_providerACPIDevice ||
		!_muxedPortsExist ||
		arg > 14U)
		return kIOReturnNoMemory;
	if (flag == 1U)
		method = &xHCMuxedPorts[arg * 5U];
	else if (!flag)
		method = &_muxName[arg * 5U];
	else
		return kIOReturnNoMemory;
	return HCSelectWithMethod(method);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::HCSelectWithMethod(char const* method)
{
	UInt32 result = 0U;

	if (!_muxedPortsSearched)
		DiscoverMuxedPorts();
	if (!_providerACPIDevice)
		return kIOReturnUnsupported;
	return _providerACPIDevice->evaluateInteger(method, &result);
}
