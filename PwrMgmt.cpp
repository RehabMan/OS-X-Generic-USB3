//
//  PwrMgmt.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 5th 2013.
//  Copyright (c) 2013-2014 Zenith432. All rights reserved.
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
	bool haveSleep = false;

	if (!(gux_options & GUX_OPTION_NO_SLEEP) &&
		_device->hasPCIPowerManagement(kPCIPMCPMESupportFromD3Cold) &&
		kIOReturnSuccess == _device->enablePCIPowerManagement(kPCIPMCSPowerStateD3))
		haveSleep = true;
	else
		IOLog("%s: xHC will be unloaded across sleep\n", getName());
	_expansionData->_controllerCanSleep = haveSleep;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1090 || defined(REHABMAN_UNIVERSAL_BUILD)
	if (CHECK_FOR_MAVERICKS)
    {
        WRITE_V3EXPANSION(_hasPCIPwrMgmt, haveSleep);
    }
#endif
	/*
	 * Note:
	 *   Always set the Card Type to Built-in, in order
	 *   to enable the extra-current mechanism, even
	 *   if xHC does not support save/restore.
	 * See IOUSBRootHubDevice::start
	 */
#if 0
	setProperty("Card Type", haveSleep ? "Built-in" : "PCI");
#else
	setProperty("Card Type", "Built-in");
	setProperty("ResetOnResume", !haveSleep);
#endif
}

__attribute__((visibility("hidden")))
IOReturn CLASS::RHCompleteSuspendOnAllPorts(void)
{
	uint32_t wait, portSC, changePortSC, idbmp, wantedWakeBits;

	wait = 0U;
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1090 || defined(REHABMAN_UNIVERSAL_BUILD)
	if (CHECK_FOR_MAVERICKS)
    {
#if __MAC_OS_X_VERSION_MAX_ALLOWED < 1090
        idbmp = ((Yosemite_ExpansionData*)_expansionData)->_ignoreDisconnectBitmap;
#else
        idbmp = _expansionData->_ignoreDisconnectBitmap;
#endif
    }
	else
#endif
		idbmp = 0U;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		changePortSC = 0U;
		portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if ((_errataBits & kErrataIntelPantherPoint) &&
			!(portSC & XHCI_PS_CSC))
			changePortSC = XHCI_PS_CSC;
		if ((portSC & XHCI_PS_PED) &&
			XHCI_PS_PLS_GET(portSC) < XDEV_U3) {
			changePortSC |= XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U3);
			wait = 15U;
		}
		if (idbmp & (2U << port))
			wantedWakeBits = XHCI_PS_WOE;
		else
			wantedWakeBits = XHCI_PS_WAKEBITS;
		if (wantedWakeBits != (portSC & XHCI_PS_WAKEBITS)) {
			portSC &= ~XHCI_PS_WAKEBITS;
			changePortSC |= wantedWakeBits;
		}
		if (changePortSC)
			Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | changePortSC);
	}
	if (wait)
		IOSleep(wait);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::QuiesceAllEndpoints(void)
{
	for (uint8_t slot = 1U; slot <= _numSlots; ++slot) {
		SlotStruct* pSlot = SlotPtr(slot);
		if (pSlot->isInactive())
			continue;
		for (int32_t endpoint = 1; endpoint != kUSBMaxPipes; ++endpoint) {
			ringStruct* pRing = pSlot->ringArrayForEndpoint[endpoint];
			if (!pRing->isInactive())
				QuiesceEndpoint(slot, endpoint);
		}
	}
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
	if (_errataBits & kErrataFL1100LowRev)
		FL1100Tricks(3);
	Write32Reg(&_pXHCIOperationalRegisters->USBCmd, XHCI_CMD_HCRST);
	rc = XHCIHandshake(&_pXHCIOperationalRegisters->USBCmd, XHCI_CMD_HCRST, 0U, 1000);
	if (rc == kIOReturnTimeout)
		IOLog("%s: could not get chip to come out of reset within 1000 ms\n", __FUNCTION__);
	if (_errataBits & kErrataFL1100LowRev)
		FL1100Tricks(4);
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

#if 0
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
void CLASS::DisableWakeBits(void)
{
	if (!_wakeEnabled || m_invalid_regspace || isInactive())
		return;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		uint32_t portSC = GetPortSCForWriting(port);
		if (m_invalid_regspace)
			return;
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC & ~XHCI_PS_WAKEBITS);
	}
	_wakeEnabled = false;
}

__attribute__((visibility("hidden")))
void CLASS::EnableWakeBits(void)
{
	if (_wakeEnabled || m_invalid_regspace || isInactive())
		return;
	uint32_t idbmp = _expansionData->_ignoreDisconnectBitmap;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		uint32_t portSC = GetPortSCForWriting(port);
		if (m_invalid_regspace)
			return;
		if (idbmp & (2U << port))
			portSC = (portSC & ~XHCI_PS_WAKEBITS) | XHCI_PS_WOE;
		else
			portSC |= XHCI_PS_WAKEBITS;
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC);
	}
	_wakeEnabled = true;
}
#endif

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
