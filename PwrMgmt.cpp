//
//  PwrMgmt.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 5th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Power Management
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::CheckSleepCapability(void)
{
	if (!(gux_options & GUX_OPTION_NO_SLEEP) &&
		_device->hasPCIPowerManagement(kPCIPMCPMESupportFromD3Cold) &&
		kIOReturnSuccess == _device->enablePCIPowerManagement(kPCIPMCSPowerStateD3)) {
		_expansionData->_controllerCanSleep = true;
		setProperty("Card Type", "Built-in");
		return;
	}
	IOLog("%s: xHC will be unloaded across sleep\n", getName());
	_expansionData->_controllerCanSleep = false;
	setProperty("Card Type", "PCI");
}

__attribute__((visibility("hidden")))
IOReturn CLASS::QuiesceAllEndpoints(void)
{
	if (_errataBits & kErrataIntelPantherPoint) {
		for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
			uint32_t portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			if (portSC & XHCI_PS_CSC)
				continue;
			Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_CSC);
		}
	}
	for (uint8_t slot = 1U; slot <= _numSlots; ++slot) {
		if (SlotPtr(slot)->isInactive())
			continue;
		for (int32_t endpoint = 1; endpoint != kUSBMaxPipes; ++endpoint) {
			ringStruct* pRing = GetRing(slot, endpoint, 0U);
			if (!pRing->isInactive())
				QuiesceEndpoint(slot, endpoint);
		}
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::ResetController(void)
{
	IOReturn rc;

	/*
	 * Wait for Controller Ready in case we're starting from hardware reset
	 */
	Read32Reg(&_pXHCIOperationalRegisters->USBSts);		// Note: CNR may be asserted on 1st read
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	rc = XHCIHandshake(&_pXHCIOperationalRegisters->USBSts, XHCI_STS_CNR, 0U, 1000);
	if (rc != kIOReturnSuccess) {
		if (rc == kIOReturnTimeout)
			IOLog("%s: chip not ready within 1000 ms\n", __FUNCTION__);
		return rc;
	}
	/*
	 * Make sure controller is halted in case we're not starting
	 *   from hardware reset
	 */
	Write32Reg(&_pXHCIOperationalRegisters->USBCmd, 0U);
	rc = WaitForUSBSts(XHCI_STS_HCH, UINT32_MAX);
	if (rc != kIOReturnSuccess) {
		if (rc == kIOReturnTimeout)
			IOLog("%s: could not get chip to halt within 100 ms\n", __FUNCTION__);
		return rc;
	}
	Write32Reg(&_pXHCIOperationalRegisters->USBCmd, XHCI_CMD_HCRST);
	rc = XHCIHandshake(&_pXHCIOperationalRegisters->USBCmd, XHCI_CMD_HCRST, 0U, 1000);
	if (rc == kIOReturnTimeout)
		IOLog("%s: could not get chip to come out of reset within 1000 ms\n", __FUNCTION__);
	return rc;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::StopUSBBus(void)
{
	uint32_t cmd = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (!(cmd & XHCI_CMD_RS)) {
		_myBusState = kUSBBusStateReset;
		return kIOReturnSuccess;
	}
	Write32Reg(&_pXHCIOperationalRegisters->USBCmd, cmd & ~XHCI_CMD_RS);
	IOReturn rc = WaitForUSBSts(XHCI_STS_HCH, UINT32_MAX);
	if (rc != kIOReturnTimeout)
		_myBusState = kUSBBusStateReset;
	return rc;
}

__attribute__((visibility("hidden")))
void CLASS::RestartUSBBus(void)
{
	uint32_t cmd = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
	if (m_invalid_regspace)
		return;
	cmd |= XHCI_CMD_EWE | XHCI_CMD_INTE | XHCI_CMD_RS;
	Write32Reg(&_pXHCIOperationalRegisters->USBCmd, cmd);
	IOReturn rc = WaitForUSBSts(XHCI_STS_HCH, 0U);
	if (rc != kIOReturnNoDevice)
		_myBusState = kUSBBusStateRunning;
}

__attribute__((visibility("hidden")))
void CLASS::NotifyRootHubsOfPowerLoss(void)
{
	IOUSBHubPolicyMaker *pm2, *pm3;

	/*
	 * TBD: Couldn't find a decent way to notify up-stack
	 *   drivers of controller power-loss and need
	 *   for re-enumeration.
	 */
	pm2 = GetHubForProtocol(kUSBDeviceSpeedHigh);
	pm3 = GetHubForProtocol(kUSBDeviceSpeedSuper);
	if (pm2)
#if 1
		pm2->changePowerStateTo(kIOUSBHubPowerStateRestart);
#else
		pm2->HubPowerChange(kIOUSBHubPowerStateRestart);
#endif
	if (pm3)
#if 1
		pm3->changePowerStateTo(kIOUSBHubPowerStateRestart);
#else
		pm3->HubPowerChange(kIOUSBHubPowerStateRestart);
#endif
}

__attribute__((visibility("hidden")))
void CLASS::SantizePortsAfterPowerLoss(void)
{
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		uint32_t portSC = GetPortSCForWriting(1U + static_cast<uint16_t>(port));
		if (m_invalid_regspace)
			return;
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC | XHCI_PS_WCE | XHCI_PS_WDE | XHCI_PS_WOE);
	}
}

__attribute__((visibility("hidden")))
void CLASS::SetPropsForBookkeeping(void)
{
	uint32_t extraPower = (kUSB3MaxPowerPerPort - kUSB2MaxPowerPerPort) * _rootHubNumPorts;
	_device->setProperty(kAppleMaxPortCurrent, kUSB3MaxPowerPerPort, 32U);
	_device->setProperty(kAppleCurrentExtra, extraPower, 32U);
	_device->setProperty(kAppleMaxPortCurrentInSleep, kUSB3MaxPowerPerPort, 32U);
	_device->setProperty(kAppleCurrentExtraInSleep, extraPower, 32U);
}
