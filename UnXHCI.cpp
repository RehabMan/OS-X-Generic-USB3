//
//  UnXHCI.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 26th, 2013.
//  Copyright (c) 2013-2014 Zenith432. All rights reserved.
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

	if (_vendorID != kVendorIntel)
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

	if (!_rootHubDeviceSS)
		goto done;
	if (_muxedPortsExist)
		goto done;
	if (!(_errataBits & kErrataIntelPortMuxing))
		goto done;
	o = _rootHubDeviceSS->getProperty(kUSBDevicePropertyLocationID);
	if (!o)
		goto done;
	n = static_cast<OSNumber*>(o)->unsigned32BitValue();
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1090
	if (CHECK_FOR_MAVERICKS)
		_providerACPIDevice = _v3ExpansionData ? _v3ExpansionData->_acpiDevice : 0;
	else
#endif
		_providerACPIDevice = CopyACPIDevice(_device);
	if (!_providerACPIDevice)
		goto done;
	string_buf = &_muxName[0];
	t = 0U;
	for (uint32_t port = 1U; port != kMaxExternalHubPorts; ++port) {
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
 * TBD: Does this really work?
 *   It's not documented anywhere
 */
__attribute__((noinline, visibility("hidden")))
void CLASS::DisableComplianceMode(void)
{
	if ((_vendorID == kVendorFrescoLogic || _vendorID == kVendorIntel) &&
		!(_errataBits & kErrataEnableAutoCompliance)) {
		_pXHCIPPTChickenBits = reinterpret_cast<uint32_t volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + 0x80EC);
		*_pXHCIPPTChickenBits |= 1U;
	}
}

__attribute__((noinline, visibility("hidden")))
void CLASS::EnableComplianceMode(void)
{
	if ((_vendorID == kVendorFrescoLogic || _vendorID == kVendorIntel) &&
		!(_errataBits & kErrataEnableAutoCompliance)) {
		_pXHCIPPTChickenBits = reinterpret_cast<uint32_t volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + 0x80EC);
		*_pXHCIPPTChickenBits &= ~1U;
	}
}
#endif

__attribute__((visibility("hidden")))
IOReturn CLASS::FL1100Tricks(int choice)
{
	uint32_t volatile* pReg;
	uint32_t v;

	switch (choice) {
		case 1:
			if (FL1100Tricks(4) != kIOReturnSuccess)
				return kIOReturnNoDevice;
			return FL1100Tricks(3);
		case 2:
			pReg = reinterpret_cast<uint32_t volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + 0x80C0);
			v = Read32Reg(pReg);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			Write32Reg(pReg, v & ~0x4000U);
			break;
		case 3:
			pReg = reinterpret_cast<uint32_t volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + 0x80EC);
			v = Read32Reg(pReg);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			v &= ~0x7000U;
			v |= 0x5000U;
			Write32Reg(pReg, v & 0xEFFFFFFFU);
			break;
		case 4:
			pReg = reinterpret_cast<uint32_t volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + 0x8094);
			v = Read32Reg(pReg);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			Write32Reg(pReg, v | 0x800000U);
			break;
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
uint32_t CLASS::VMwarePortStatusShuffle(uint32_t statusChangedBitmap, uint8_t numPortsEach)
{
	uint8_t i;
	uint32_t mask, outss, ouths, inmap;
	outss = 0U;
	ouths = 0U;
	mask = 2U;
	inmap = (statusChangedBitmap >> 1);
	for (i = 0U; inmap && i < numPortsEach; ++i, inmap >>= 2, mask <<= 1) {
		if (inmap & 1U)
			outss |= mask;
		if (inmap & 2U)
			ouths |= mask;
	}
	return (statusChangedBitmap & 1U) | outss | (ouths << numPortsEach);
}

#if 0
__attribute__((visibility("hidden")))
uint32_t CLASS::CheckACPITablesForCaptiveRootHubPorts(uint8_t numPorts)
{
	IOReturn rc;
	uint32_t v;
	uint8_t connectorType;

	if (!numPorts)
		return 0U;
	v = 0U;
	for (uint8_t port = 1U; port <= numPorts; ++port) {
		connectorType = 254U;
		/*
		 * IOReturn IOUSBControllerV3::GetConnectorType(IORegistryEntry* provider, UInt32 portNumber, UInt32 locationID, UInt8* connectorType);
		 */
		rc = GetConnectorType(_device, port, _expansionData->_locationID, &connectorType);
		if (rc == kIOReturnSuccess && connectorType == kUSBProprietaryConnector)
			v |= 1U << port;
	}
	return v;
}
#endif

__attribute__((visibility("hidden")))
IOReturn CLASS::HCSelect(uint8_t port, uint8_t controllerType)
{
	static char const xHCMuxedPorts[80] = "XHCA\0XHCB\0XHCC\0XHCD";
	char const* method;

	if (!_muxedPortsSearched)
		DiscoverMuxedPorts();
	if (!_providerACPIDevice ||
		!_muxedPortsExist ||
		port >= kMaxExternalHubPorts)
		return kIOReturnNoMemory;
	if (controllerType == 1U)
		method = &xHCMuxedPorts[port * 5U];
	else if (!controllerType)
		method = &_muxName[port * 5U];
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
