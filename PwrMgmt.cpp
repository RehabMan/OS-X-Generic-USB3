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
#if 1
		setProperty("ResetOnResume", false);
#endif
		return;
	}
	IOLog("%s: xHC will be unloaded across sleep\n", getName());
	_expansionData->_controllerCanSleep = false;
#if 0
	setProperty("Card Type", "PCI");
#else
	/*
	 * Note:
	 *   Always set the Card Type to Built-in, in order
	 *   to enable the extra-current mechanism, even
	 *   if xHC does not support save/restore.
	 * See IOUSBRootHubDevice::start
	 */
	setProperty("Card Type", "Built-in");
	setProperty("ResetOnResume", true);
#endif
}

__attribute__((visibility("hidden")))
IOReturn CLASS::IntelSleepMuxBugWorkaround(void)
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
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::QuiesceAllEndpoints(void)
{
	for (uint8_t slot = 1U; slot <= _numSlots; ++slot) {
		if (ConstSlotPtr(slot)->isInactive())
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
		uint32_t portSC = GetPortSCForWriting(port);
		if (m_invalid_regspace)
			return;
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC | XHCI_PS_WAKEBITS);
	}
}

__attribute__((visibility("hidden")))
void CLASS::SetPropsForBookkeeping(void)
{
	/*
	 * Note:
	 *   IOUSBController::CreateRootHubDevice copies these properties
	 *   from _device to the root hub devices, and also to IOResources.
	 *   The copying to IOResources takes place once (globally).
	 *   IOUSBRootHubDevice::RequestExtraWakePower then uses the global
	 *   values on IOResources to do power-accounting for all root hubs
	 *   in the system (!).  So whatever value given here for extra current
	 *   must cover 400mA of extra current for each SS root hub port in
	 *   the system.
	 *   Since there may be more than one xHC, must account for them all.
	 *   Set a high value of 255 to (hopefully) cover everything.
	 *   Additionally, iPhone/iPad ask for 1600mA of extra power on high-speed
	 *   ports, so we allow for that as well.
	 */
#if 0
	uint32_t defaultTotalExtraCurrent = (kUSB3MaxPowerPerPort - kUSB2MaxPowerPerPort) * _rootHubNumPorts;
	uint32_t defaultMaxCurrentPerPort = kUSB3MaxPowerPerPort;
#else
	uint32_t defaultTotalExtraCurrent = (kUSB3MaxPowerPerPort - kUSB2MaxPowerPerPort) * 255U;
	uint32_t defaultMaxCurrentPerPort = kUSB3MaxPowerPerPort + 1600U;
#endif
	/*
	 * Note: Only set defaults if none were injected via DSDT
	 */
	if (!OSDynamicCast(OSNumber, _device->getProperty(kAppleMaxPortCurrent)))
		_device->setProperty(kAppleMaxPortCurrent, defaultMaxCurrentPerPort, 32U);
	if (!OSDynamicCast(OSNumber, _device->getProperty(kAppleCurrentExtra)))
		_device->setProperty(kAppleCurrentExtra, defaultTotalExtraCurrent, 32U);
	if (!OSDynamicCast(OSNumber, _device->getProperty(kAppleMaxPortCurrentInSleep)))
		_device->setProperty(kAppleMaxPortCurrentInSleep, defaultMaxCurrentPerPort, 32U);
	if (!OSDynamicCast(OSNumber, _device->getProperty(kAppleCurrentExtraInSleep)))
		_device->setProperty(kAppleCurrentExtraInSleep, defaultTotalExtraCurrent, 32U);
}
