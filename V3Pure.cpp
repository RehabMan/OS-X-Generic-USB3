//
//  V3Pure.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on December 26th, 2012.
//  Copyright (c) 2012-2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include <IOKit/usb/IOUSBRootHubDevice.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark IOUSBControllerV3 Pure
#pragma mark -

IOReturn CLASS::ResetControllerState(void)
{
	if (m_invalid_regspace)
		return kIOReturnNotResponding;
	IOReturn rc = StopUSBBus();
	EnableInterruptsFromController(false);
	IOSleep(1U);	// drain primary interrupts
	if (_expansionData &&
		_expansionData->_controllerCanSleep &&
		_device) {
		/*
		 * On the ASM1042, shutting down with PME enabled
		 *   causes spontaneous reboot, so disable it.
		 */
		_device->enablePCIPowerManagement(kPCIPMCSPowerStateD0);
	}
	if (rc == kIOReturnSuccess)
		_uimInitialized = false;
	return rc;
}

IOReturn CLASS::RestartControllerFromReset(void)
{
	if (_uimInitialized)
		return kIOReturnSuccess;
	TakeOwnershipFromBios();
	EnableXHCIPorts();
	DisableComplianceMode();
	IOReturn rc = ResetController();
	if (rc != kIOReturnSuccess)
		return rc;
	RHPortStatusChangeBitmapInit();
	bzero(&_rhPortEmulateCSC[0], sizeof _rhPortEmulateCSC);
	rc = InitializePorts();
	if (rc != kIOReturnSuccess)
		return rc;
	_isSleeping = false;
	for (uint8_t slot = 1U; slot <= _numSlots; ++slot)
		NukeSlot(slot);
	bzero(&_addressMapper, sizeof _addressMapper);
	bzero(&_rhPortBeingResumed[0], sizeof _rhPortBeingResumed);
	bzero(&_rhPortBeingReset[0], sizeof _rhPortBeingReset);
	uint32_t config = Read32Reg(&_pXHCIOperationalRegisters->Config);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->Config, (config & ~XHCI_CONFIG_SLOTS_MASK) | _numSlots);
	Write32Reg(&_pXHCIOperationalRegisters->DNCtrl, UINT16_MAX);
	Write64Reg(&_pXHCIOperationalRegisters->DCBAap, _dcbaa.physAddr, false);
	InitCMDRing();
	for (int32_t interrupter = 0; interrupter < kMaxActiveInterrupters; ++interrupter)
		InitEventRing(interrupter, true);
	if (_scratchpadBuffers.max)
		SetDCBAAAddr64(_dcbaa.ptr, _scratchpadBuffers.physAddr);
	bzero(const_cast<int32_t*>(&_errorCounters[0]), sizeof _errorCounters);
	_inputContext.refCount = 0;
	_numEndpoints = 0U;
	_deviceZero.isBeingAddressed = false;
	_inTestMode = false;
#if 0
	_filterInterruptActive = false;
#endif
	_millsecondCounter = 0ULL;
	bzero(&_interruptCounters[0], sizeof _interruptCounters);
	_uimInitialized = true;
	return kIOReturnSuccess;
}

IOReturn CLASS::SaveControllerStateForSleep(void)
{
	IOReturn rc = StopUSBBus();
	if (rc != kIOReturnSuccess)
		return rc;
	_sleepOpSave.USBCmd = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
	_sleepOpSave.DNCtrl = Read32Reg(&_pXHCIOperationalRegisters->DNCtrl);
	_sleepOpSave.DCBAap = Read64Reg(&_pXHCIOperationalRegisters->DCBAap);
	_sleepOpSave.Config = Read32Reg(&_pXHCIOperationalRegisters->Config);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	for (int32_t interrupter = 0; interrupter < kMaxActiveInterrupters; ++interrupter)
		SaveAnInterrupter(interrupter);
	uint32_t cmd = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->USBCmd, cmd | XHCI_CMD_CSS);
	rc = WaitForUSBSts(XHCI_STS_SSS, 0U);
	if (rc != kIOReturnSuccess)
		return rc;
	uint32_t sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	_isSleeping = true;
	if (sts & XHCI_STS_SRE) {
		++_diagCounters[DIAGCTR_SLEEP];
		Write32Reg(&_pXHCIOperationalRegisters->USBSts, XHCI_STS_SRE);
		/*
		 * Disable PME, on ASM1042 it causes system to wake
		 *   up instantly.
		 */
		if (_device)
			_device->enablePCIPowerManagement(kPCIPMCSPowerStateD0);
		IOLog("%s: xHC Save Error\n", __FUNCTION__);
		return kIOReturnInternalError;
	}
	return kIOReturnSuccess;
}

IOReturn CLASS::RestoreControllerStateFromSleep(void)
{
	uint32_t sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (sts & XHCI_STS_PCD) {
		for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
			uint32_t portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			if (portSC & XHCI_PS_CSC) {
				if (portSC & XHCI_PS_PED) {
					IOLog("%s: Port %u on bus %#x - connect status changed but still enabled. clearing enable bit: portSC(%#x)\n",
						  __FUNCTION__, port + 1U, static_cast<uint32_t>(_busNumber), portSC);
					Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_PEC);
				} else {
					char const* nstr = "xHC";
					char const* dstr = (portSC & XHCI_PS_CCS) ? "connected" : "disconnected";
					uint8_t protocol = kUSBDeviceSpeedLow;
					uint16_t portNum = PortNumberCanonicalToProtocol(port, &protocol);
					if (protocol == kUSBDeviceSpeedHigh && _rootHubDevice)
						nstr = _rootHubDevice->getName();
					else if (protocol == kUSBDeviceSpeedSuper && _expansionData && _rootHubDeviceSS)
						nstr = _rootHubDeviceSS->getName();
					IOLog("%s(%s): Port %u on bus %#x %s: portSC(%#x)\n",
						  __FUNCTION__, nstr, portNum, static_cast<uint32_t>(_busNumber), dstr, portSC);
					EnsureUsability();
				}
			} else if (XHCI_PS_PLS_GET(portSC) == XDEV_RESUME) {
				uint8_t protocol = kUSBDeviceSpeedHigh;
				IOUSBHubPolicyMaker* pm = 0;
				uint16_t portNum = PortNumberCanonicalToProtocol(port, &protocol);
				if (portNum)
					pm = GetHubForProtocol(protocol);
				/*
				 * Note: This message causes HID device that generated PME to
				 *   do a full system wakeup.  Without it, the display remains down.
				 */
				if (pm)
					pm->message(kIOUSBMessageRootHubWakeEvent, this, reinterpret_cast<void*>(portNum - 1U));
				else
					IOLog("%s: Port %u on bus %#x has remote wakeup from some device\n",
						  __FUNCTION__, port + 1U, static_cast<uint32_t>(_busNumber));
				RHCheckForPortResume(port, protocol, portSC);
			}
		}
	}
	if (_isSleeping) {
		Write32Reg(&_pXHCIOperationalRegisters->USBCmd, _sleepOpSave.USBCmd);
		Write32Reg(&_pXHCIOperationalRegisters->DNCtrl, _sleepOpSave.DNCtrl);
		Write64Reg(&_pXHCIOperationalRegisters->DCBAap, _sleepOpSave.DCBAap, false);
		Write32Reg(&_pXHCIOperationalRegisters->Config, _sleepOpSave.Config);
		for (int32_t interrupter = 0; interrupter < kMaxActiveInterrupters; ++interrupter)
			RestoreAnInterrupter(interrupter);
		uint32_t cmd = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		Write32Reg(&_pXHCIOperationalRegisters->USBCmd, cmd | XHCI_CMD_CRS);
		IOReturn rc = WaitForUSBSts(XHCI_STS_RSS, 0U);
		if (rc == kIOReturnNoDevice)
			return rc;
		sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if (sts & XHCI_STS_SRE) {
			++_diagCounters[DIAGCTR_RESUME];
			Write32Reg(&_pXHCIOperationalRegisters->USBSts, XHCI_STS_SRE);
			IOLog("%s: xHC Restore Error\n", __FUNCTION__);
			_uimInitialized = false;
			rc = RestartControllerFromReset();
			if (rc != kIOReturnSuccess)
				IOLog("%s: RestartControllerFromReset failed with %#x\n", __FUNCTION__, rc);
			SantizePortsAfterPowerLoss();
#if 0
			NotifyRootHubsOfPowerLoss();
#endif
			return kIOReturnSuccess;
		}
		InitCMDRing();
		_isSleeping = false;
	}
	DisableComplianceMode();
	uint32_t wait = 0U;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		uint32_t portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if (portSC & XHCI_PS_CAS) {
			IOLog("%s: Port %u on bus %#x cold attach detected\n",
				  __FUNCTION__, port + 1U, static_cast<uint32_t>(_busNumber));
			Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_WPR);
			wait = 50U;
		}
	}
	if (wait)
		CheckedSleep(wait);
	return kIOReturnSuccess;
}

IOReturn CLASS::DozeController(void)
{
	if (!_v3ExpansionData->_externalDeviceCount &&
		(_errataBits & kErrataAllowControllerDoze)) {
		uint16_t xhcc = _device->configRead16(PCI_XHCI_INTEL_XHCC);
		if (xhcc == UINT16_MAX) {
#if 0
			m_invalid_regspace = true;
			return kIOReturnNoDevice;
#else
			return kIOReturnSuccess;
#endif
		}
		xhcc &= ~PCI_XHCI_INTEL_XHCC_SWAXHCIP_SET(3U);
		/*
		 * Set SWAXHCI to clear if 1) software, 2) MMIO, 3) xHC exits idle
		 */
		xhcc |= PCI_XHCI_INTEL_XHCC_SWAXHCIP_SET(0U);
		xhcc |= PCI_XHCI_INTEL_XHCC_SWAXHCI;
		_device->configWrite16(PCI_XHCI_INTEL_XHCC, xhcc);
	}
	return kIOReturnSuccess;
}

IOReturn CLASS::WakeControllerFromDoze(void)
{
	if (!_v3ExpansionData->_externalDeviceCount &&
		(_errataBits & kErrataAllowControllerDoze)) {
		uint16_t xhcc = _device->configRead16(PCI_XHCI_INTEL_XHCC);
		/*
		 * Clear SWAXHCI if it's still on
		 */
		if (xhcc != UINT16_MAX && (xhcc & PCI_XHCI_INTEL_XHCC_SWAXHCI))
			_device->configWrite16(PCI_XHCI_INTEL_XHCC, xhcc & ~PCI_XHCI_INTEL_XHCC_SWAXHCI);
	}
#if 0
	bool found_resuming = false;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		uint32_t portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if (XHCI_PS_PLS_GET(portSC) == XDEV_RESUME) {
			_rhPortBeingResumed[port] = true;
			found_resuming = true;
		}
	}
	if (found_resuming) {
		IOSleep(20U);
		RHCompleteResumeOnAllPorts();
	}
#endif
	return kIOReturnSuccess;
}

IOReturn CLASS::UIMEnableAddressEndpoints(USBDeviceAddress address, bool enable)
{
	if (address == _hub3Address || address == _hub2Address)
		return kIOReturnSuccess;
	if (gux_log_level >= 2)
		IOLog("%s(%u, %c)\n", __FUNCTION__, address, enable ? 'Y' : 'N');
	uint8_t slot = GetSlotID(address);
	if (!slot) {
		if (address >= kUSBMaxDevices || !enable)
			return kIOReturnBadArgument;
		_addressMapper.Active[address] = true;
		slot = GetSlotID(address);
		if (!slot || slot > _numSlots) {
			_addressMapper.Active[address] = false;
			return kIOReturnInternalError;
		}
		for (int32_t endpoint = 1; endpoint != kUSBMaxPipes; ++endpoint) {
			ringStruct* pRing = GetRing(slot, endpoint, 0U);
			if (pRing->isInactive())
				continue;
			if (IsStreamsEndpoint(slot, endpoint))
				RestartStreams(slot, endpoint, 0U);
			else
				StartEndpoint(slot, endpoint, 0U);

		}
		return kIOReturnSuccess;
	}
	if (enable)
		return kIOReturnSuccess;
	for (int32_t endpoint = 1; endpoint != kUSBMaxPipes; ++endpoint) {
		ringStruct* pRing = GetRing(slot, endpoint, 0U);
		if (pRing->isInactive())
			continue;
		QuiesceEndpoint(slot, endpoint);	// Note: originally StopEndpoint(slot, endpoint)
	}
	_addressMapper.Active[address] = false;
	return kIOReturnSuccess;
}

IOReturn CLASS::EnableInterruptsFromController(bool enable)
{
	if (enable)
		RestartUSBBus();
	else {
		uint32_t cmd = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		Write32Reg(&_pXHCIOperationalRegisters->USBCmd, cmd & ~(XHCI_CMD_INTE | XHCI_CMD_EWE));
	}
	return kIOReturnSuccess;
}
