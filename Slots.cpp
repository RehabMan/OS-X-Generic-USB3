//
//  Slots.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on October 10th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "XHCITypes.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Control Endpoints/Slots
#pragma mark -

__attribute__((visibility("hidden")))
uint8_t CLASS::GetSlotID(int32_t address)
{
	if (address == _hub3Address ||
		address == _hub2Address ||
		static_cast<uint32_t>(address) >= kUSBMaxDevices ||
		!_addressMapper.Active[address])
		return 0U;
	return _addressMapper.Slot[address];
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AddressDevice(uint32_t deviceSlot, uint16_t maxPacketSize, bool wantSAR, uint8_t speed, int32_t highSpeedHubSlot, int32_t highSpeedPort)
{
	ringStruct* pRing;
	ContextStruct *pContext, *pHubContext;
	uint32_t routeString;
	int32_t retFromCMD;
	uint16_t currentPortOnHub, currentHubAddress, addr;
	TRBStruct localTrb = { 0 };

	pRing = GetRing(deviceSlot, 1, 0U);
	if (pRing->isInactive())
		return kIOReturnInternalError;
	currentPortOnHub = _deviceZero.PortOnHub;
	currentHubAddress = _deviceZero.HubAddress;
	routeString = 0U;
	for (int32_t depth = 0; depth < 5; ++depth) {
		if (currentHubAddress == _hub3Address || currentHubAddress == _hub2Address)
			break;
		if (currentPortOnHub > kMaxExternalHubPorts)
			currentPortOnHub = kMaxExternalHubPorts;
		routeString = (routeString << 4) + currentPortOnHub;
		addr = currentHubAddress;
		currentHubAddress = _addressMapper.HubAddress[addr];
		currentPortOnHub = _addressMapper.PortOnHub[addr];
	}
	if (currentHubAddress != _hub3Address && currentHubAddress != _hub2Address) {
		IOLog("%s: Root hub port not found in topology: hub: %u, rootHubSS: %u rootHubHS: %u\n", __FUNCTION__,
			  currentHubAddress, _hub3Address, _hub2Address);
		return kIOReturnInternalError;
	}
	if (!currentPortOnHub || currentPortOnHub > _rootHubNumPorts) {
		IOLog("%s: Root hub port number invalid: %u\n", __FUNCTION__, currentPortOnHub);
		return kIOReturnInternalError;
	}
	GetInputContext();
	pContext = GetInputContextPtr();
	pContext->_ic.dwInCtx1 = XHCI_INCTX_1_ADD_MASK(1U) | XHCI_INCTX_1_ADD_MASK(0U);
	pContext = GetInputContextPtr(1);
	pContext->_s.dwSctx1 |= XHCI_SCTX_1_RH_PORT_SET(static_cast<uint32_t>(currentPortOnHub));
	pContext->_s.dwSctx0 = XHCI_SCTX_0_CTX_NUM_SET(1U);
#if 0
	uint32_t portSpeed = Read32Reg(&_pXHCIOperationalRegisters->prs[currentPortOnHub - 1U].PortSC);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	portSpeed = XHCI_PS_SPEED_GET(portSpeed);
	if (portSpeed >= XDEV_SS) {
		SetSlCtxSpeed(pContext, portSpeed);
		maxPacketSize = 512U;
		goto skip_low_full;
	}
#else
	if ((currentHubAddress == _hub3Address) &&
		(speed < kUSBDeviceSpeedSuper || maxPacketSize != 512U))
		IOLog("%s: Inconsistent device speed %u (maxPacketSize %u) for topology rooted in SuperSpeed hub\n",
			  __FUNCTION__, speed, maxPacketSize);
#endif
	switch (speed) {
		case kUSBDeviceSpeedLow:
			SetSlCtxSpeed(pContext, XDEV_LS);
			break;
		case kUSBDeviceSpeedFull:
		default:
			SetSlCtxSpeed(pContext, XDEV_FS);
			break;
		case kUSBDeviceSpeedHigh:
			SetSlCtxSpeed(pContext, XDEV_HS);
			goto skip_low_full;
		case kUSBDeviceSpeedSuper:
			SetSlCtxSpeed(pContext, XDEV_SS);
			goto skip_low_full;
	}
	/*
	 * Note: Only for Low or Full Speed devices
	 */
	if (highSpeedHubSlot) {
		pContext->_s.dwSctx2 |= XHCI_SCTX_2_TT_PORT_NUM_SET(highSpeedPort);
		pContext->_s.dwSctx2 |= XHCI_SCTX_2_TT_HUB_SID_SET(highSpeedHubSlot);
		pHubContext = GetSlotContext(highSpeedHubSlot);
		if (pHubContext && XHCI_SCTX_0_MTT_GET(pHubContext->_s.dwSctx0))
			pContext->_s.dwSctx0 |= XHCI_SCTX_0_MTT_SET(1U);
		else
			pContext->_s.dwSctx0 &= ~XHCI_SCTX_0_MTT_SET(1U);
	}
skip_low_full:
#if kMaxActiveInterrupters > 1
	pContext->_s.dwSctx2 |= XHCI_SCTX_2_IRQ_TARGET_SET(1U);
#endif
	pContext->_s.dwSctx0 |= XHCI_SCTX_0_ROUTE_SET(routeString);
	pContext = GetInputContextPtr(2);
	pContext->_e.dwEpCtx1 |= XHCI_EPCTX_1_EPTYPE_SET(CTRL_EP);
	pContext->_e.dwEpCtx1 |= XHCI_EPCTX_1_MAXP_SIZE_SET(static_cast<uint32_t>(maxPacketSize));
	pContext->_e.qwEpCtx2 |= (pRing->physAddr + pRing->dequeueIndex * sizeof *pRing->ptr) & XHCI_EPCTX_2_TR_DQ_PTR_MASK;
	if (pRing->cycleState)
		pContext->_e.qwEpCtx2 |= XHCI_EPCTX_2_DCS_SET(1U);
	else
		pContext->_e.qwEpCtx2 &= ~static_cast<uint64_t>(XHCI_EPCTX_2_DCS_SET(1U));
	pContext->_e.dwEpCtx1 |= XHCI_EPCTX_1_CERR_SET(3U);
	pContext->_e.dwEpCtx4 |= XHCI_EPCTX_4_AVG_TRB_LEN_SET(8U);
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	localTrb.d |= XHCI_TRB_3_SLOT_SET(deviceSlot);
	if (!wantSAR)
		localTrb.d |= XHCI_TRB_3_BSR_BIT;
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_ADDRESS_DEVICE, 0);
	ReleaseInputContext();
	if (retFromCMD == -1)
		return kIOReturnInternalError;
	else if (retFromCMD > -1000)
		return kIOReturnSuccess;
	else if (retFromCMD == -1000 - XHCI_TRB_ERROR_PARAMETER) {
#if 0
		PrintContext(GetInputContextPtr());
		PrintContext(GetInputContextPtr(1));
		PrintContext(GetInputContextPtr(2));
#endif
	}
	return TranslateXHCIStatus(-1000 - retFromCMD, false, kUSBDeviceSpeedSuper, false);
}

__attribute__((visibility("hidden")))
void CLASS::NukeSlot(uint8_t slot)
{
	SlotStruct* pSlot = SlotPtr(slot);
	if (pSlot->isInactive())
		return;
	for (int32_t endpoint = 1; endpoint != kUSBMaxPipes; ++endpoint) {
		ringStruct* pRing = pSlot->ringArrayForEndpoint[endpoint];
		if (!pRing) {
			pSlot->maxStreamForEndpoint[endpoint] = 0U;
			pSlot->lastStreamForEndpoint[endpoint] = 0U;
			continue;
		}
		uint16_t lastStream = pSlot->lastStreamForEndpoint[endpoint];
		for (uint16_t streamId = 0U; streamId <= lastStream; ++streamId) {
			if (streamId)
				pRing[streamId].md = 0;
			else
				DeallocRing(&pRing[streamId]);
			if ((pRing[streamId].epType | CTRL_EP) == ISOC_IN_EP) {
				if (pRing[streamId].isochEndpoint)
					NukeIsochEP(pRing[streamId].isochEndpoint);
			} else {
				if (pRing[streamId].asyncEndpoint)
					pRing[streamId].asyncEndpoint->nuke();
			}
		}
		IOFree(pRing, (1U + static_cast<size_t>(pSlot->maxStreamForEndpoint[endpoint])) * sizeof *pRing);
		pSlot->maxStreamForEndpoint[endpoint] = 0U;
		pSlot->lastStreamForEndpoint[endpoint] = 0U;
		pSlot->ringArrayForEndpoint[endpoint] = 0;
	}
	pSlot->ctx = 0;
	pSlot->md->complete();
	pSlot->md->release();
	pSlot->md = 0;
	pSlot->physAddr = 0ULL;
	/*
	 * TBD: pSlot->deviceNeedsReset = false; ???
	 */
}

__attribute__((visibility("hidden")))
int32_t CLASS::CleanupControlEndpoint(uint8_t slot, bool justDisable)
{
	TRBStruct localTrb = { 0U };
	SlotStruct* pSlot;
	ringStruct* pRing;

	if (justDisable)
		goto do_disable;
	pSlot = SlotPtr(slot);
	pRing = pSlot->ringArrayForEndpoint[1];
	if (pRing) {
		DeallocRing(pRing);
		IOFree(pRing, sizeof *pRing);
		pSlot->ringArrayForEndpoint[1] = 0;
	}
	if (pSlot->md) {
		pSlot->md->complete();
		pSlot->md->release();
		pSlot->md = 0;
		pSlot->ctx = 0;
		pSlot->physAddr = 0U;
	}
	_addressMapper.Slot[0] = 0U;
	_addressMapper.Active[0] = false;

do_disable:
	localTrb.d = XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
	return WaitForCMD(&localTrb, XHCI_TRB_TYPE_DISABLE_SLOT, 0);
}

#pragma mark -
#pragma mark Timeouts
#pragma mark -

__attribute__((visibility("hidden")))
bool CLASS::IsStillConnectedAndEnabled(int32_t slot)
{
	ContextStruct* pContext;
	uint32_t portSC;
	uint8_t port;

	pContext = GetSlotContext(slot);
	if (!pContext)
		return false;
	port = static_cast<uint8_t>(XHCI_SCTX_1_RH_PORT_GET(pContext->_s.dwSctx1)) - 1U;
	if (port >= _rootHubNumPorts)
		return false;
	portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
	if (m_invalid_regspace)
		return false;
	return (portSC & (XHCI_PS_PED | XHCI_PS_CCS)) == (XHCI_PS_PED | XHCI_PS_CCS);
}

__attribute__((visibility("hidden")))
void CLASS::CheckSlotForTimeouts(int32_t slot, uint32_t frameNumber, bool isAssociatedRHPortEnabled)
{
	SlotStruct* pSlot = SlotPtr(slot);
	if (pSlot->isInactive())
		return;
	bool abortAll = pSlot->deviceNeedsReset || !isAssociatedRHPortEnabled;
	for (int32_t endpoint = 1; endpoint != kUSBMaxPipes; ++endpoint) {
		ringStruct* pRing = pSlot->ringArrayForEndpoint[endpoint];
		if (pRing->isInactive() ||
			(pRing->epType | CTRL_EP) == ISOC_IN_EP)
			continue;
		if (pSlot->IsStreamsEndpoint(endpoint)) {
			bool stopped = false;
			uint16_t lastStream = pSlot->lastStreamForEndpoint[endpoint];
			for (uint16_t streamId = 1U; streamId <= lastStream; ++streamId)
				if (checkEPForTimeOuts(slot, endpoint, streamId, frameNumber, abortAll))
					stopped = true;
			if (stopped)
				RestartStreams(slot, endpoint, 0U);
		} else if (checkEPForTimeOuts(slot, endpoint, 0U, frameNumber, abortAll))
			StartEndpoint(slot, endpoint, 0U);
	}
}

#pragma mark -
#pragma mark External Hubs
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::configureHub(uint32_t deviceAddress, uint32_t flags)
{
	uint8_t slot;
	uint32_t nop, ttt;
	int32_t retFromCMD;
	ContextStruct* pContext;
	TRBStruct localTrb = { 0 };

	if (deviceAddress == _hub3Address || deviceAddress == _hub2Address)
		return kIOReturnSuccess;
	slot = GetSlotID(static_cast<int32_t>(deviceAddress));
	if (!slot)
		return kIOReturnBadArgument;
	if (flags & kUSBHSHubFlagsMoreInfoMask) {
		ttt = (flags & kUSBHSHubFlagsTTThinkTimeMask) >> kUSBHSHubFlagsTTThinkTimeShift;
		nop = (flags & kUSBHSHubFlagsNumPortsMask) >> kUSBHSHubFlagsNumPortsShift;
	} else {
		ttt = 3U;	// 32 FS bit times
		nop = kMaxExternalHubPorts;
	}
	GetInputContext();
	pContext = GetInputContextPtr();
	pContext->_ic.dwInCtx1 = XHCI_INCTX_1_ADD_MASK(0U);
	pContext = GetInputContextPtr(1);
	*pContext = *GetSlotContext(slot);
	pContext->_s.dwSctx0 |= XHCI_SCTX_0_HUB_SET(1U);
	if (flags & kUSBHSHubFlagsMultiTTMask)
		pContext->_s.dwSctx0 |= XHCI_SCTX_0_MTT_SET(1U);
	else
		pContext->_s.dwSctx0 &= ~XHCI_SCTX_0_MTT_SET(1U);
	pContext->_s.dwSctx1 &= ~XHCI_SCTX_1_NUM_PORTS_SET(255U);
	pContext->_s.dwSctx1 |= XHCI_SCTX_1_NUM_PORTS_SET(nop);
	pContext->_s.dwSctx2 &= ~XHCI_SCTX_2_TT_THINK_TIME_SET(3U);
	pContext->_s.dwSctx2 |= XHCI_SCTX_2_TT_THINK_TIME_SET(ttt);
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_CONFIGURE_EP, 0);
	ReleaseInputContext();
	if (retFromCMD == -1)
		return kIOReturnInternalError;
	if (retFromCMD > -1000)
		return kIOReturnSuccess;
	if (retFromCMD == -1000 - XHCI_TRB_ERROR_PARAMETER) {
#if 0
		PrintContext(GetInputContextPtr());
		PrintContext(pContext);
#endif
	}
	return kIOReturnInternalError;
}

#pragma mark -
#pragma mark Test Mode
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::EnterTestMode(void)
{
	IOReturn rc;
	uint32_t portSC;

	EnableComplianceMode();
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if (portSC & XHCI_PS_PP)
			XHCIRootHubPowerPort(port, false);
	}
	rc = StopUSBBus();
	if (rc != kIOReturnSuccess)
		return rc;
	_myBusState = kUSBBusStateReset;
	_inTestMode = true;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::LeaveTestMode(void)
{
	IOReturn rc;

	DisableComplianceMode();
	rc = StopUSBBus();
	if (rc != kIOReturnSuccess)
		return rc;
	rc = ResetController();
	if (rc != kIOReturnSuccess)
		return rc;
	_inTestMode = false;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::PlacePortInMode(uint32_t port, uint32_t mode)
{
	IOReturn rc;
	uint32_t portPMSC, cmd;
	uint16_t _port;

	if (!_inTestMode)
		return kIOReturnInternalError;
	_port = PortNumberProtocolToCanonical(port, kUSBDeviceSpeedHigh);
	if (_port >= _rootHubNumPorts)
		return kIOReturnInternalError;
	portPMSC = Read32Reg(&_pXHCIOperationalRegisters->prs[_port].PortPmsc);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	portPMSC &= ~(15U << 28);	// drop TestMode for USB2 port
	if (mode != 5U) {
		rc = StopUSBBus();
		if (rc != kIOReturnSuccess)
			return rc;
	}
	portPMSC |= (mode & 15U) << 28;
	Write32Reg(&_pXHCIOperationalRegisters->prs[_port].PortPmsc, portPMSC);
	if (mode != 5U)
		return kIOReturnSuccess;
	cmd = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	cmd |= XHCI_CMD_RS;
	Write32Reg(&_pXHCIOperationalRegisters->USBCmd, cmd);
	WaitForUSBSts(XHCI_STS_HCH, 0U);
	return kIOReturnSuccess;
}
