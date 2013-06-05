//
//  Leaf_Methods.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on December 26th, 2012.
//  Copyright (c) 2012-2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "XHCITypes.h"
#include "Async.h"
#include <IOKit/usb/IOUSBRootHubDevice.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Self
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::SetVendorInfo(void)
{
	OSData *vendProp, *deviceProp, *revisionProp;

	// get this chips vendID, deviceID, revisionID
	vendProp = OSDynamicCast(OSData, _device->getProperty( "vendor-id" ));
	if (vendProp)
		_vendorID = *static_cast<uint16_t const*>(vendProp->getBytesNoCopy());
	deviceProp   = OSDynamicCast(OSData, _device->getProperty( "device-id" ));
	if (deviceProp)
		_deviceID = *static_cast<uint16_t const*>(deviceProp->getBytesNoCopy());
	revisionProp = OSDynamicCast(OSData, _device->getProperty( "revision-id" ));
	if (revisionProp)
		_revisionID = *static_cast<uint16_t const*>(revisionProp->getBytesNoCopy());
}

__attribute__((visibility("hidden")))
IOReturn CLASS::MakeBuffer(uint32_t mem_options,
						   size_t mem_capacity,
						   uint64_t mem_mask,
						   IOBufferMemoryDescriptor **p_desc,
						   void **p_virt_addr,
						   uint64_t* p_phys_addr)
{
	IOReturn rc;
	IOBufferMemoryDescriptor* md;
	void* ptr;
	uint64_t physAddr;
	IOByteCount segLength;

	md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
														  mem_options,
														  mem_capacity,
														  XHCI_HCC_AC64(_HCCLow) ? mem_mask : (mem_mask & UINT32_MAX));
	if (!md) {
		IOLog("%s: Cannot create IOBufferMemoryDescriptor\n", __FUNCTION__);
		return kIOReturnNoMemory;
	}
	rc = md->prepare();
	if (rc != kIOReturnSuccess) {
		IOLog("%s: IOMemoryDescriptor::prepare failed, error == %#x\n", __FUNCTION__, rc);
		md->release();
		return rc;
	}
	ptr = md->getBytesNoCopy();
	if (ptr)
		bzero(ptr, mem_capacity);
	physAddr = md->getPhysicalSegment(0U, &segLength, 0U);
	if (!physAddr) {
		IOLog("%s: IOMemoryDescriptor::getPhysicalSegment returned bad data\n", __FUNCTION__);
		md->complete();
		md->release();
		return kIOReturnInternalError;
	}
	*p_desc = md;
	*p_virt_addr = ptr;
	*p_phys_addr = physAddr;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::MakeBufferUnmapped(uint32_t mem_options,
								   size_t mem_capacity,
								   uint64_t mem_mask,
								   IOBufferMemoryDescriptor **p_desc,
								   uint64_t* p_phys_addr)
{
	IOReturn rc;
	IOBufferMemoryDescriptor* md;
	uint64_t physAddr;
	IOByteCount segLength;

	md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(0,
														  mem_options | kIOMemoryPageable,
														  mem_capacity,
														  XHCI_HCC_AC64(_HCCLow) ? mem_mask : (mem_mask & UINT32_MAX));
	if (!md) {
		IOLog("%s: Cannot create IOBufferMemoryDescriptor\n", __FUNCTION__);
		return kIOReturnNoMemory;
	}
	rc = md->prepare();
	if (rc != kIOReturnSuccess) {
		IOLog("%s: IOMemoryDescriptor::prepare failed, error == %#x\n", __FUNCTION__, rc);
		md->release();
		return rc;
	}
	physAddr = md->getPhysicalSegment(0U, &segLength, 0U);
	if (!physAddr) {
		IOLog("%s: IOMemoryDescriptor::getPhysicalSegment returned bad data\n", __FUNCTION__);
		md->complete();
		md->release();
		return kIOReturnInternalError;
	}
	*p_desc = md;
	*p_phys_addr = physAddr;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AllocRing(ringStruct* pRing, int32_t numPages)
{
	IOReturn rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
							 numPages * PAGE_SIZE,
							 -PAGE_SIZE,
							 &pRing->md,
							 reinterpret_cast<void**>(&pRing->ptr),
							 &pRing->physAddr);
	if (rc != kIOReturnSuccess)
		return kIOReturnNoMemory;
	pRing->numTRBs = static_cast<uint16_t>(numPages * (PAGE_SIZE / sizeof *pRing->ptr));
	pRing->numPages = static_cast<uint16_t>(numPages);
	pRing->cycleState = 1U;
	pRing->enqueueIndex = 0U;
	pRing->dequeueIndex = 0U;
	pRing->lastSeenDequeueIndex = 0U;
	pRing->lastSeenFrame = 0U;
	pRing->nextIsocFrame = 0ULL;
	TRBStruct* t = &pRing->ptr[pRing->numTRBs - 1U];
	SetTRBAddr64(t, pRing->physAddr);
	t->d |= XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK) | XHCI_TRB_3_TC_BIT;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::DecodeExtendedCapability(uint32_t hccParams)
{
	uint32_t ecp = XHCI_HCC_XECP(hccParams);
	if (!ecp)
		return;
	_pXHCIExtendedCapRegisters = reinterpret_cast<XHCIXECPStruct volatile*>(reinterpret_cast<uint32_t volatile*>(_pXHCICapRegisters) + ecp);
	XHCIXECPStruct volatile* iter = _pXHCIExtendedCapRegisters;
	while (true) {
		if (iter->capId == 1U)
			_pUSBLegSup = reinterpret_cast<uint32_t volatile*>(iter);
		else if (iter->capId == 2U)
			DecodeSupportedProtocol(iter);
		if (!(iter->next))
			break;
		iter = reinterpret_cast<XHCIXECPStruct volatile*>(reinterpret_cast<uint32_t volatile*>(iter) + iter->next);
	}
}

__attribute__((visibility("hidden")))
void CLASS::DecodeSupportedProtocol(XHCIXECPStruct volatile* pCap)
{
	XHCIXECPStruct_SP volatile* pSPCap = reinterpret_cast<XHCIXECPStruct_SP volatile*>(pCap);
	switch (pSPCap->revisionMajor) {
		case 3U:
			if (pSPCap->nameString != ' BSU')
				break;
			_v3ExpansionData->_rootHubNumPortsSS = pSPCap->compatiblePortCount;
			_v3ExpansionData->_rootHubPortsSSStartRange = pSPCap->compatiblePortOffset;
			break;
		case 2U:
			if (pSPCap->nameString != ' BSU')
				break;
			_v3ExpansionData->_rootHubNumPortsHS = pSPCap->compatiblePortCount;
			_v3ExpansionData->_rootHubPortsHSStartRange = pSPCap->compatiblePortOffset;
			break;
	}
}

__attribute__((visibility("hidden")))
void CLASS::TakeOwnershipFromBios(void)
{
	uint32_t v;
	IOReturn rc;

	if (!_pUSBLegSup)
		return;
	v = Read32Reg(_pUSBLegSup);
	if (m_invalid_regspace)
		return;
	if (v & XHCI_HC_BIOS_OWNED) {
		Write32Reg(_pUSBLegSup, v | XHCI_HC_OS_OWNED);
		rc = XHCIHandshake(_pUSBLegSup, XHCI_HC_BIOS_OWNED, 0U, 100);
		if (rc == kIOReturnNoDevice)
			return;
		if (rc == kIOReturnTimeout) {
			IOLog("%s: Unable to take ownership of xHC from BIOS within 100 ms\n", __FUNCTION__);
			/*
			 * Fall through to break bios hold by disabling SMI enables
			 */
		}
	}
	v = Read32Reg(_pUSBLegSup + 1);
	if (m_invalid_regspace)
		return;
	/*
	 * Clear all SMI enables
	 */
	v &= XHCI_LEGACY_DISABLE_SMI;
	/*
	 * Clear RW1C bits
	 */
	v |= XHCI_LEGACY_SMI_EVENTS;
	Write32Reg(_pUSBLegSup + 1, v);
}

__attribute__((noinline, visibility("hidden")))
IOReturn CLASS::WaitForUSBSts(uint32_t test_mask, uint32_t test_target)
{
	return XHCIHandshake(&_pXHCIOperationalRegisters->USBSts, test_mask, test_target, 100);
}

__attribute__((noinline, visibility("hidden")))
IOReturn CLASS::XHCIHandshake(uint32_t volatile const* pReg, uint32_t test_mask, uint32_t test_target, int32_t msec)
{
	for (int32_t count = 0; count < msec; ++count) {
		if (count)
			IOSleep(1U);
		uint32_t reg = Read32Reg(pReg);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if ((reg & test_mask) == (test_target & test_mask))
			return kIOReturnSuccess;
	}
	return kIOReturnTimeout;	// Note: originally kIOReturnInternalError
}

__attribute__((visibility("hidden")))
ringStruct* CLASS::GetRing(int32_t slot, int32_t endpoint, uint32_t streamId)
{
	if (streamId <= ConstSlotPtr(slot)->lastStreamForEndpoint[endpoint]) {
		ringStruct* pRing = SlotPtr(slot)->ringArrayForEndpoint[endpoint];
		if (pRing)
			return &pRing[streamId];
	}
	return 0;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AllocScratchpadBuffers(void)
{
	IOReturn rc;
	IOBufferMemoryDescriptor* md;
	uint64_t pageMask;
	uint32_t pageSizes;

	_scratchpadBuffers.md = 0;
	if (!_scratchpadBuffers.max)
		return kIOReturnSuccess;
	pageSizes = Read32Reg(&_pXHCIOperationalRegisters->PageSize);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace\n", __FUNCTION__);
		return kIOReturnNoDevice;
	}
	pageSizes <<= 12;
	if (!pageSizes) {
		IOLog("%s: PageSize register invalid value of zero\n", __FUNCTION__);
		return kIOReturnDeviceError;
	}
	pageSizes &= -pageSizes;	// leave just smallest size
	_scratchpadBuffers.mdGC = OSArray::withCapacity(_scratchpadBuffers.max);
	if (!_scratchpadBuffers.mdGC) {
		IOLog("%s: OSArray::withCapacity failed\n", __FUNCTION__);
		return kIOReturnNoMemory;
	}
	rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
					static_cast<size_t>(_scratchpadBuffers.max) * sizeof *_scratchpadBuffers.ptr,
					-PAGE_SIZE,
					&_scratchpadBuffers.md,
					reinterpret_cast<void**>(&_scratchpadBuffers.ptr),
					&_scratchpadBuffers.physAddr);
	if (rc != kIOReturnSuccess) {
		IOLog("%s: MakeBuffer(1) failed, error == %#x\n", __FUNCTION__, rc);
		return rc;
	}
	pageMask = -static_cast<int64_t>(pageSizes);
	for (uint32_t spbufs = 0U; spbufs < _scratchpadBuffers.max; ++spbufs) {
		rc = MakeBufferUnmapped(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
								pageSizes,
								pageMask,
								&md,
								&_scratchpadBuffers.ptr[spbufs]);
		if (rc != kIOReturnSuccess) {
			IOLog("%s: MakeBuffer(2) failed, error == %#x\n", __FUNCTION__, rc);
			return rc;
		}
		_scratchpadBuffers.mdGC->setObject(md);	// Note: ignores error
		md->release();
	}
	SetDCBAAAddr64(_dcbaa.ptr, _scratchpadBuffers.physAddr);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::FinalizeScratchpadBuffers(void)
{
	if (_scratchpadBuffers.mdGC) {
		uint32_t l = _scratchpadBuffers.mdGC->getCount();
		for (uint32_t i = 0U; i < l; ++i)
			static_cast<IOGeneralMemoryDescriptor*>(_scratchpadBuffers.mdGC->getObject(i))->complete();
		_scratchpadBuffers.mdGC->flushCollection();
		_scratchpadBuffers.mdGC->release();
		_scratchpadBuffers.mdGC = 0;
	}
	if (_scratchpadBuffers.md) {
		_scratchpadBuffers.md->complete();
		_scratchpadBuffers.md->release();
		_scratchpadBuffers.md = 0;
	}
}

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
void CLASS::DeallocRing(ringStruct* pRing)
{
	if (!pRing)
		return;
	if (pRing->md) {
		pRing->md->complete();
		pRing->md->release();
		pRing->md = 0;
	}
	pRing->ptr = 0;
	pRing->physAddr = 0ULL;
	pRing->numTRBs = 0U;
}

__attribute__((visibility("hidden")))
void CLASS::ParkRing(ringStruct* pRing)
{
	int32_t retFromCMD;
	uint8_t slot, endpoint;
	TRBStruct localTrb = { 0 };

	slot = pRing->slot;
#if 0
	if (GetSlCtxSpeed(GetSlotContext(slot)) > kUSBDeviceSpeedHigh)
		return;
#endif
	endpoint = pRing->endpoint;
	QuiesceEndpoint(slot, endpoint);
	localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
	localTrb.d |= XHCI_TRB_3_EP_SET(static_cast<uint32_t>(endpoint));
	/*
	 * Use a 16-byte aligned zero-filled spare space in Event Ring 0
	 *   to park the ring. (see InitEventRing)
	 */
	SetTRBAddr64(&localTrb, _eventRing[0].erstba + sizeof localTrb);
	localTrb.a |= XHCI_TRB_3_CYCLE_BIT;	// Note: set DCS to 1 so it doesn't move
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_SET_TR_DEQUEUE, 0);
	if (retFromCMD != -1 && retFromCMD > -1000)
		return;
#if 0
	PrintContext(GetSlotContext(slot));
	PrintContext(GetSlotContext(slot, endpoint));
#endif
}

__attribute__((visibility("hidden")))
IOUSBHubPolicyMaker* CLASS::GetHubForProtocol(uint8_t protocol)
{
	if (protocol == kUSBDeviceSpeedHigh && _rootHubDevice)
		return _rootHubDevice->GetPolicyMaker();
	if (protocol == kUSBDeviceSpeedSuper && _expansionData && _rootHubDeviceSS)
		return _rootHubDeviceSS->GetPolicyMaker();
	return 0;
}

__attribute__((visibility("hidden")))
ringStruct* CLASS::CreateRing(int32_t slot, int32_t endpoint, uint32_t maxStream)
{
	SlotStruct* pSlot = SlotPtr(slot);
	if (pSlot->ringArrayForEndpoint[endpoint]) {
		if (maxStream > pSlot->maxStreamForEndpoint[endpoint])
			return 0;
		return pSlot->ringArrayForEndpoint[endpoint];
	}
	ringStruct* pRing = static_cast<ringStruct*>(IOMalloc((1U + maxStream) * sizeof *pRing));
	if (!pRing)
		return pRing;
	bzero(pRing, (1U + maxStream) * sizeof *pRing);
	pSlot->maxStreamForEndpoint[endpoint] = static_cast<uint16_t>(maxStream);
	pSlot->lastStreamForEndpoint[endpoint] = 0U;
	pSlot->ringArrayForEndpoint[endpoint] = pRing;
	for (uint32_t streamId = 0U; streamId <= maxStream; ++streamId) {
		pRing[streamId].slot = static_cast<uint8_t>(slot);
		pRing[streamId].endpoint = static_cast<uint8_t>(endpoint);
	}
	return pRing;
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
		if (currentPortOnHub > kMaxPorts)
			currentPortOnHub = kMaxPorts;
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
void CLASS::GetInputContext(void)
{
	++_inputContext.refCount;
	bzero(GetInputContextPtr(), GetInputContextSize());
}

__attribute__((visibility("hidden")))
void CLASS::ReleaseInputContext(void)
{
	if (_inputContext.refCount)
		--_inputContext.refCount;
}

__attribute__((visibility("hidden")))
uint32_t CLASS::GetInputContextSize(void)
{
	return 33U * (XHCI_HCC_CSZ(_HCCLow) ? 2U : 1U) * sizeof(ContextStruct);
}

__attribute__((visibility("hidden")))
uint32_t CLASS::GetDeviceContextSize(void)
{
	return 32U * (XHCI_HCC_CSZ(_HCCLow) ? 2U : 1U) * sizeof(ContextStruct);
}

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
void CLASS::CheckSlotForTimeouts(int32_t slot, uint32_t frameNumber)
{
	if (ConstSlotPtr(slot)->isInactive())
		return;
	for (int32_t endpoint = 1; endpoint != kUSBMaxPipes; ++endpoint) {
		if (IsIsocEP(slot, endpoint))
			continue;
		ringStruct* pRing = GetRing(slot, endpoint, 0U);
		if (pRing->isInactive())
			continue;
		if (IsStreamsEndpoint(slot, endpoint)) {
			bool stopped = false;
			uint16_t lastStream = GetLastStreamForEndpoint(slot, endpoint);
			for (uint16_t streamId = 1U; streamId <= lastStream; ++streamId)
				if (checkEPForTimeOuts(slot, endpoint, streamId, frameNumber))
					stopped = true;
			if (stopped)
				RestartStreams(slot, endpoint, 0U);
		} else if (checkEPForTimeOuts(slot, endpoint, 0U, frameNumber))
			StartEndpoint(slot, endpoint, 0U);
	}
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
IOReturn CLASS::GatedGetFrameNumberWithTime(OSObject* owner, void* frameNumber, void* theTime, void*, void*)
{
	CLASS* me = static_cast<CLASS*>(owner);
	*static_cast<uint64_t*>(frameNumber) = me->_millsecondsTimers[3];
	*static_cast<uint64_t*>(theTime) = me->_millsecondsTimers[1];
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
int32_t CLASS::CountRingToED(ringStruct* pRing, int32_t trbIndexInRingQueue, uint32_t* pShortFall, bool updateDequeueIndex)
{
	int32_t next;
	uint32_t trbType;
	TRBStruct* pTrb = &pRing->ptr[trbIndexInRingQueue];

	while (pTrb->d & XHCI_TRB_3_CHAIN_BIT) {
		next = trbIndexInRingQueue + 1;
		if (next >= static_cast<int32_t>(pRing->numTRBs) - 1)
			next = 0;
		if (next == static_cast<int32_t>(pRing->enqueueIndex))
			break;
		trbIndexInRingQueue = next;
		pTrb = &pRing->ptr[trbIndexInRingQueue];
		trbType = XHCI_TRB_3_TYPE_GET(pTrb->d);
		if (trbType == XHCI_TRB_TYPE_NORMAL) {
			*pShortFall += XHCI_TRB_2_BYTES_GET(pTrb->c);
			continue;
		}
		if (trbType == XHCI_TRB_TYPE_EVENT_DATA)
			break;
	}
	if (updateDequeueIndex)
		pRing->dequeueIndex = static_cast<uint16_t>(trbIndexInRingQueue);
	return trbIndexInRingQueue;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::TranslateXHCIStatus(int32_t xhci_err, bool direction, uint8_t speed, bool)
{
	switch (xhci_err) {
		case XHCI_TRB_ERROR_SUCCESS:
		case XHCI_TRB_ERROR_SHORT_PKT:
			return kIOReturnSuccess;
		case XHCI_TRB_ERROR_DATA_BUF:
			return kIOUSBBufferOverrunErr | (direction ? 0U : 1U);
		case XHCI_TRB_ERROR_BABBLE:
		case XHCI_TRB_ERROR_RING_OVERRUN:
		case XHCI_TRB_ERROR_VF_RING_FULL:
		case XHCI_TRB_ERROR_EV_RING_FULL:
		case XHCI_TRB_ERROR_MISSED_SERVICE:
		case XHCI_TRB_ERROR_ISOC_OVERRUN:
		case XHCI_TRB_ERROR_EVENT_LOST:
			return kIOReturnOverrun;
		case XHCI_TRB_ERROR_XACT:
			return speed < kUSBDeviceSpeedHigh ? kIOUSBHighSpeedSplitError : kIOReturnNotResponding;
		case XHCI_TRB_ERROR_STALL:
			return kIOUSBPipeStalled;
		case XHCI_TRB_ERROR_RESOURCE:
			return kIOReturnNoResources;
		case XHCI_TRB_ERROR_BANDWIDTH:
		case XHCI_TRB_ERROR_BW_OVERRUN:
		case XHCI_TRB_ERROR_SEC_BW:
			return kIOReturnNoBandwidth;
		case XHCI_TRB_ERROR_NO_SLOTS:
			return kIOUSBDeviceCountExceeded;
		case XHCI_TRB_ERROR_STREAM_TYPE:
			return kIOReturnInvalid;
		case XHCI_TRB_ERROR_SLOT_NOT_ON:
		case XHCI_TRB_ERROR_ENDP_NOT_ON:
		case XHCI_TRB_ERROR_CMD_RING_STOP:
			return kIOReturnOffline;
		case XHCI_TRB_ERROR_RING_UNDERRUN:
			return kIOReturnUnderrun;
		case XHCI_TRB_ERROR_PARAMETER:
		case XHCI_TRB_ERROR_CONTEXT_STATE:
		case XHCI_TRB_ERROR_BAD_MELAT:
		case XHCI_TRB_ERROR_INVALID_SID:
			return kIOReturnBadArgument;
		case XHCI_TRB_ERROR_NO_PING_RESP:
			return kIOReturnNotResponding;
		case XHCI_TRB_ERROR_INCOMPAT_DEV:
			return kIOReturnDeviceError;
		case XHCI_TRB_ERROR_CMD_ABORTED:
		case XHCI_TRB_ERROR_STOPPED:
		case XHCI_TRB_ERROR_LENGTH:
			return kIOReturnAborted;
		case XHCI_TRB_ERROR_SPLIT_XACT:
			return kIOUSBHighSpeedSplitError;
		case 193:	// Intel FORCE_HDR_USB2_NO_SUPPORT
			return (_errataBits & kErrataIntelPantherPoint) ? kIOReturnInternalError : kIOReturnUnsupported;
		case 199:	// Intel CMPL_WITH_EMPTY_CONTEXT
			return (_errataBits & kErrataIntelPantherPoint) ? kIOReturnInternalError : kIOReturnNotOpen;
		case 200:	// Intel VENDOR_CMD_FAILED
			return (_errataBits & kErrataIntelPantherPoint) ? kIOReturnInternalError : kIOReturnNoBandwidth;
		default:
			return kIOReturnInternalError;
	}
}

__attribute__((visibility("hidden")))
IOReturn CLASS::TranslateCommandCompletion(int32_t code)
{
	if (code == -1)
		return kIOReturnTimeout;
	if (code > -1000)
		return kIOReturnSuccess;
	if (code == -1000 - XHCI_TRB_ERROR_CONTEXT_STATE)
		return kIOReturnNotPermitted;
	if (code == -1000 - XHCI_TRB_ERROR_PARAMETER)
		return kIOReturnBadArgument;
	return kIOReturnInternalError;
}

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
		nop = kMaxPorts;
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

__attribute__((visibility("hidden")))
IOReturn CLASS::ResetDevice(int32_t slot)
{
	int32_t retFromCMD;
	TRBStruct localTrb = { 0 };

	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_RESET_DEVICE, 0);
	return TranslateCommandCompletion(retFromCMD);
}

__attribute__((visibility("hidden")))
int32_t CLASS::NegotiateBandwidth(int32_t slot)
{
	TRBStruct localTrb = { 0 };

	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	return WaitForCMD(&localTrb, XHCI_TRB_TYPE_NEGOTIATE_BW, 0);
}

__attribute__((visibility("hidden")))
int32_t CLASS::SetLTV(uint32_t BELT)
{
	TRBStruct localTrb = { 0 };

	localTrb.d |= (BELT & 0xFFFU) << 16;
	return WaitForCMD(&localTrb, XHCI_TRB_TYPE_SET_LATENCY_TOL, 0);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::GetPortBandwidth(uint8_t HubSlot, uint8_t speed, uint8_t* pBuffer, size_t* pCount)
{
	uint8_t xspeed;
	int32_t retFromCMD;
	TRBStruct localTrb = { 0 };

	if (!pBuffer || !pCount)
		return kIOReturnBadArgument;
	if (!HubSlot && *pCount < _rootHubNumPorts) {
		*pCount = _rootHubNumPorts;
		return kIOReturnNoMemory;
	}
	if (HubSlot && *pCount < kMaxPorts) {
		*pCount = kMaxPorts;
		return kIOReturnNoMemory;
	}
	switch (speed) {
		case kUSBDeviceSpeedLow:
			xspeed = XDEV_LS;
			break;
		case kUSBDeviceSpeedFull:
			xspeed = XDEV_FS;
			break;
		case kUSBDeviceSpeedHigh:
			xspeed = XDEV_HS;
			break;
		case kUSBDeviceSpeedSuper:
			xspeed = XDEV_SS;
			break;
		default:
			return kIOReturnBadArgument;
	}
	localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(HubSlot));
	localTrb.d |= XHCI_TRB_3_TLBPC_SET(static_cast<uint32_t>(xspeed));
	GetInputContext();
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_GET_PORT_BW, 0);
	if (retFromCMD == -1 || retFromCMD <= -1000) {
		ReleaseInputContext();
		if (retFromCMD == -1)
			return kIOReturnInternalError;
		return TranslateXHCIStatus(-1000 - retFromCMD, false, speed, false);
	}
	if (!HubSlot) {
		bcopy(reinterpret_cast<uint8_t const*>(GetInputContextPtr()) + 1, pBuffer, _rootHubNumPorts);
		*pCount = _rootHubNumPorts;
	} else {
		bcopy(reinterpret_cast<uint8_t const*>(GetInputContextPtr()) + 1, pBuffer, kMaxPorts);
		*pCount = kMaxPorts;
	}
	ReleaseInputContext();
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::SleepWithGateReleased(IOCommandGate* pGate, uint32_t msec)
{
	AbsoluteTime deadline;
	uint32_t event = 0U;
	clock_interval_to_deadline(msec,
							   kMillisecondScale,
							   reinterpret_cast<uint64_t*>(&deadline));
	pGate->commandSleep(&event, deadline, THREAD_ABORTSAFE);
}

__attribute__((visibility("hidden")))
void CLASS::CheckedSleep(uint32_t msec)
{
	if (_workLoop->inGate())
		SleepWithGateReleased(_commandGate, msec);
	else
		IOSleep(msec);
}
