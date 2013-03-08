//
//  V1Pure.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on December 26th, 2012.
//  Copyright (c) 2012-2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "XHCITRB.h"
#include "XHCITypes.h"
#include <IOKit/IOFilterInterruptEventSource.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark IOUSBController Pure
#pragma mark -

IOReturn CLASS::UIMInitialize(IOService* provider)
{
	IOReturn rc;

	_device = OSDynamicCast(IOPCIDevice, provider);
	if (!_device) {
		IOLog("%s: provider is not IOPCIDevice\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	_deviceBase = _device->mapDeviceMemoryWithIndex(0U);
	if (!_deviceBase) {
		IOLog("%s: mapDeviceMemoryWithIndex failed\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoResources;
	}
	SetVendorInfo();
	_errataBits = GetErrataBits(_vendorID, _deviceID, _revisionID);	// Note: originally |=
#if 0
	if (!(_errataBits & (kErrataFrescoLogic | kErrataIntelPantherPoint))) {
		OSBoolean* b = OSDynamicCast(OSBoolean, getProperty("AllowAnyXHCI"));
		if (!(gUSBStackDebugFlags & kUSBEnableAllXHCIControllersMask) && (!b || !b->isTrue())) {
			IOLog("%s: Unsupported xHC chipset\n", __FUNCTION__);
			UIMFinalize();
			return kIOReturnUnsupported;
		}
	}
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1072
	if (!_v3ExpansionData->_onThunderbolt)
		_expansionData->_isochMaxBusStall = 25000U;
#endif
	_pXHCICapRegisters = reinterpret_cast<struct XHCICapRegisters volatile*>(_deviceBase->getVirtualAddress());
	// enable the card registers
	_device->configWrite16(kIOPCIConfigCommand, kIOPCICommandMemorySpace);
#if 0
	uint16_t hciv = Read16Reg(&_pXHCICapRegisters->HCIVersion);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (1)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	if (hciv < 0x100U) {
		IOLog("%s: Unsupported XHCI version %#x\n", __FUNCTION__, static_cast<uint32_t>(hciv));
		UIMFinalize();
		return kIOReturnUnsupported;
	}
#endif
	uint32_t hcp1 = Read32Reg(&_pXHCICapRegisters->HCSParams[0]);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (2)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	_maxInterrupters = static_cast<uint16_t>(XHCI_HCS1_IRQ_MAX(hcp1));
	_numSlots = static_cast<uint8_t>(XHCI_HCS1_DEVSLOT_MAX(hcp1));
	_rootHubNumPorts = static_cast<uint8_t>(XHCI_HCS1_N_PORTS(hcp1));
	if (!_rootHubNumPorts || _rootHubNumPorts > kMaxPorts) {
		IOLog("%s: Invalid number of root hub ports == %u\n", __FUNCTION__, _rootHubNumPorts);
		UIMFinalize();
		return kIOReturnDeviceError;
	}
	_filterInterruptSource =IOFilterInterruptEventSource::filterInterruptEventSource(this,
																					 InterruptHandler,
																					 PrimaryInterruptFilter,
																					 _device,
																					 findInterruptIndex(_device,
																										!(_errataBits & kErrataDisableMSI)));
	if (!_filterInterruptSource) {
		IOLog("%s: Unable to create filterInterruptEventSource\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoResources;
	}
	_baseInterruptIndex = _filterInterruptSource->getIntIndex();
	rc = _workLoop->addEventSource(_filterInterruptSource);
	if (rc != kIOReturnSuccess) {
		IOLog("%s: Unable to add filter to workloop, error == %#x\n", __FUNCTION__, rc);
		UIMFinalize();
		return rc;
	}
	uint32_t hcc = Read32Reg(&_pXHCICapRegisters->HCCParams);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (3)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	DecodeExtendedCapability(hcc);
	TakeOwnershipFromBios();
	_maxNumEndpoints = (kUSBMaxPipes - 1) * 256;
	EnableXHCIPorts();
	if (_errataBits & kErrataIntelPantherPoint)
		_maxNumEndpoints = 64;
	uint32_t u = Read8Reg(&_pXHCICapRegisters->CapLength);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (4)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	_pXHCIOperationalRegisters = reinterpret_cast<struct XHCIOpRegisters volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + u);
	u = Read32Reg(&_pXHCICapRegisters->RTSOff);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (5)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	_pXHCIRuntimeRegisters = reinterpret_cast<struct XHCIRuntimeRegisters volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + u);
	u = Read32Reg(&_pXHCICapRegisters->DBOff);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (6)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	_pXHCIDoorbellRegisters = reinterpret_cast<uint32_t volatile*>(reinterpret_cast<uint8_t volatile*>(_pXHCICapRegisters) + u);
	DisableComplianceMode();
	u = XHCI_HCC_PSA_SZ_MAX(hcc);
	if (u)
		_maxPSASize = 1U << (1U + u);
	else
		_maxPSASize = 0U;
	_HCCLow = static_cast<uint8_t>(hcc & 255U);
	_inputContext.refCount = 0U;
	rc = ResetController();
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (7)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	if (rc != kIOReturnSuccess) {
		IOLog("%s: ResetController failed, error == %#x\n", __FUNCTION__, rc);
		UIMFinalize();
		return rc;
	}
	rc = InitializePorts();
	if (rc != kIOReturnSuccess) {
		IOLog("%s: Invalid regspace (8)\n", __FUNCTION__);
		UIMFinalize();
		return rc;
	}
	_hub3Address = kXHCISSRootHubAddress;
	_hub2Address = kXHCIUSB2RootHubAddress;
	u = Read32Reg(&_pXHCIOperationalRegisters->Config);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (9)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	Write32Reg(&_pXHCIOperationalRegisters->Config, (u & ~XHCI_CONFIG_SLOTS_MASK) | _numSlots);
	Write32Reg(&_pXHCIOperationalRegisters->DNCtrl, UINT16_MAX);
	_slotArray = static_cast<SlotStruct*>(IOMalloc(static_cast<size_t>(_numSlots) * sizeof *_slotArray));
	if (!_slotArray) {
		IOLog("%s: Failed to allocate memory for slots\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoMemory;
	}
	bzero(_slotArray, static_cast<size_t>(_numSlots) * sizeof *_slotArray);
	rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
					(1U + static_cast<size_t>(_numSlots)) * sizeof *_dcbaa.ptr,
					-PAGE_SIZE,
					&_dcbaa.md,
					reinterpret_cast<void**>(&_dcbaa.ptr),
					&_dcbaa.physAddr);
	if (rc != kIOReturnSuccess) {
		IOLog("%s: MakeBuffer(1) failed, error == %#x\n", __FUNCTION__, rc);
		UIMFinalize();
		return rc;
	}
	bzero(_dcbaa.ptr, (1U + static_cast<size_t>(_numSlots)) * sizeof *_dcbaa.ptr);
	Write64Reg(&_pXHCIOperationalRegisters->DCBAap, _dcbaa.physAddr, false);
	_commandRing.numTRBs = PAGE_SIZE / sizeof *_commandRing.ptr;
	_commandRing.callbacks = static_cast<TRBCallbackEntry*>(IOMalloc(_commandRing.numTRBs * sizeof *_commandRing.callbacks));
	if (!_commandRing.callbacks) {
		IOLog("%s: IOMalloc failed\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoMemory;
	}
	bzero(_commandRing.callbacks, _commandRing.numTRBs * sizeof *_commandRing.callbacks);
	rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
					_commandRing.numTRBs * sizeof *_commandRing.ptr,
					-PAGE_SIZE,
					&_commandRing.md,
					reinterpret_cast<void**>(&_commandRing.ptr),
					&_commandRing.physAddr);
	if (rc != kIOReturnSuccess) {
		IOLog("%s: MakeBuffer(2) failed, error == %#x\n", __FUNCTION__, rc);
		UIMFinalize();
		return rc;
	}
	InitCMDRing();
	uint32_t hcp2 = Read32Reg(&_pXHCICapRegisters->HCSParams[1]);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace (10)\n", __FUNCTION__);
		UIMFinalize();
		return kIOReturnNoDevice;
	}
	_istKeepAwayFrames = (hcp2 & 8U) ? (hcp2 & 7U) : 1U;	// minimum of 1 frame
	setProperty("ISTKeepAway", _istKeepAwayFrames, 8U);
	_erstMax = 1U << XHCI_HCS2_ERST_MAX(hcp2);
	for (int32_t interrupter = 0; interrupter < kMaxActiveInterrupters; ++interrupter) {
		rc = InitAnEventRing(interrupter);
		if (rc != kIOReturnSuccess) {
			IOLog("%s: InitAnEventRing(%d) failed, error == %#x\n", __FUNCTION__, interrupter, rc);
			UIMFinalize();
			return rc;
		}
	}
	bzero(&_errorCounters[0], sizeof _errorCounters);
	rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
					GetInputContextSize(),
					-PAGE_SIZE,
					&_inputContext.md,
					reinterpret_cast<void**>(&_inputContext.ptr),
					&_inputContext.physAddr);
	if (rc != kIOReturnSuccess) {
		IOLog("%s: MakeBuffer(3) failed, error == %#x\n", __FUNCTION__, rc);
		UIMFinalize();
		return rc;
	}
	_scratchpadBuffers.max = static_cast<uint8_t>(XHCI_HCS2_SPB_MAX(hcp2));
#if 0
	_maxScratchpadBuffers |= ((hcp2 >> 16) & 0x3E0U);	// Note: These are listed as reserved in xHCI 1.0 Spec
#endif
	rc = AllocScratchpadBuffers();
	if (rc != kIOReturnSuccess) {
		UIMFinalize();
		return rc;
	}
	bzero(&_addressMapper, sizeof _addressMapper);
	RHPortStatusChangeBitmapInit();
	_isSleeping = false;
	_deviceZero.isBeingAddressed = false;
#if 0
	_filterInterruptActive = false;
#endif
	_millsecondCounter = 0ULL;
	bzero(&_interruptCounters, sizeof _interruptCounters);
	_HSEDetected = false;
	_unknown2 = 0U;
	_unknown1 = 0U;
	_magic = 0xDEADBEEFU;
	rc = AllocRHThreadCalls();
	if (rc != kIOReturnSuccess) {
		IOLog("%s: AllocRHThreadCalls failed, rc == %#x\n", __FUNCTION__, rc);
		UIMFinalize();
		return rc;
	}
	CheckSleepCapability();
	SetPropsForBookkeeping();
	_uimInitialized = true;
	registerService();
	return kIOReturnSuccess;
}

IOReturn CLASS::UIMFinalize()
{
	if (_providerACPIDevice) {
		_providerACPIDevice->release();
		_providerACPIDevice = 0;
	}
	if (_slotArray) {
		for (uint8_t slot = 1U; slot <= _numSlots; ++slot)
			NukeSlot(slot);
		IOFree(_slotArray, static_cast<size_t>(_numSlots) * sizeof *_slotArray);
		_slotArray = 0;
	}
	if (_dcbaa.md) {
		_dcbaa.md->complete();
		_dcbaa.md->release();
		_dcbaa.md = 0;
	}
	if (_commandRing.callbacks) {
		IOFree(_commandRing.callbacks, _commandRing.numTRBs * sizeof *_commandRing.callbacks);
		_commandRing.callbacks = 0;
	}
	if (_commandRing.md) {
		_commandRing.md->complete();
		_commandRing.md->release();
		_commandRing.md = 0;
	}
	for (int32_t interrupter = 0; interrupter < kMaxActiveInterrupters; ++interrupter)
		FinalizeAnEventRing(interrupter);
	if (_inputContext.md) {
		_inputContext.md->complete();
		_inputContext.md->release();
		_inputContext.md = 0;
	}
	FinalizeScratchpadBuffers();
	if (_filterInterruptSource && _workLoop) {
		_workLoop->removeEventSource(_filterInterruptSource);
		_filterInterruptSource->release();
		_filterInterruptSource = 0;
	}
	if (_deviceBase) {
		_deviceBase->release();
		_deviceBase = 0;
	}
	FinalizeRHThreadCalls();
	_uimInitialized = false;
	return kIOReturnSuccess;
}

IOReturn CLASS::UIMAbortEndpoint(short functionNumber, short endpointNumber, short direction)
{
	return UIMAbortStream(UINT32_MAX, functionNumber, endpointNumber, direction);
}

IOReturn CLASS::UIMDeleteEndpoint(short functionNumber, short endpointNumber, short direction)
{
	uint8_t slot, endpoint;
	ringStruct* pRing;
	SlotStruct* pSlot;
	TRBStruct localTrb = { 0 };

	slot = GetSlotID(functionNumber);
	if (!slot)
		return functionNumber ? kIOReturnBadArgument : kIOReturnSuccess;
	endpoint = TranslateEndpoint(endpointNumber, direction);
	if (!endpoint || endpoint >= kUSBMaxPipes)
		return kIOReturnBadArgument;
	pRing = GetRing(slot, endpoint, 0U);
	pSlot = SlotPtr(slot);
	if (pRing)
		pRing->deleteInProgress = true;
	if (!pRing->isInactive()) {
		UIMAbortEndpoint(functionNumber, endpointNumber, direction);
		if (_errataBits & kErrataParkRing)
			ParkRing(pRing);
	}
	if (endpoint == 1U) {
		if (pRing) {
			XHCIAsyncEndpoint* pEp = pRing->asyncEndpoint;
			if (pEp) {
				pEp->Abort();
				pEp->release();
				static_cast<void>(__sync_fetch_and_sub(&_numEndpoints, 1));
				pRing->asyncEndpoint = 0;
			}
		}
	} else {
		GetInputContext();
		ContextStruct* pContext = GetInputContextPtr();
		pContext->_ic.dwInCtx0 = XHCI_INCTX_0_DROP_MASK(endpoint);
		pContext->_ic.dwInCtx1 = XHCI_INCTX_1_ADD_MASK(0U);
		pContext = GetInputContextPtr(1);
		*pContext = *GetSlotContext(slot);
		int32_t numCtx = static_cast<int32_t>(XHCI_SCTX_0_CTX_NUM_GET(pContext->_s.dwSctx0));
		if (numCtx == endpoint) {
			for (--numCtx; numCtx > 1 && GetRing(slot, numCtx, 0U)->isInactive(); --numCtx);
			pContext->_s.dwSctx0 &= ~XHCI_SCTX_0_CTX_NUM_SET(0x1FU);
			pContext->_s.dwSctx0 |= XHCI_SCTX_0_CTX_NUM_SET(numCtx);
		}
		pContext->_s.dwSctx0 &= ~(1U << 24);
		SetTRBAddr64(&localTrb, _inputContext.physAddr);
		localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
		WaitForCMD(&localTrb, XHCI_TRB_TYPE_CONFIGURE_EP, 0);
		ReleaseInputContext();
		DeleteStreams(slot, endpoint);
		if (pRing && pRing->isochEndpoint) {
			if ((pRing->epType | CTRL_EP) == ISOC_IN_EP)
				DeleteIsochEP(pRing->isochEndpoint);
			else {
				XHCIAsyncEndpoint* pEp = pRing->asyncEndpoint;
				if (pEp) {
					pEp->Abort();
					pEp->release();
				}
			}
			static_cast<void>(__sync_fetch_and_sub(&_numEndpoints, 1));
			pRing->isochEndpoint = 0;
		}
	}
	if (pRing) {
		DeallocRing(pRing);
		IOFree(pRing, (1U + static_cast<size_t>(pSlot->maxStreamForEndpoint[endpoint])) * sizeof *pRing);
		pSlot->maxStreamForEndpoint[endpoint] = 0U;
		pSlot->lastStreamForEndpoint[endpoint] = 0U;
		pSlot->ringArrayForEndpoint[endpoint] = 0;
	}
	for (endpoint = 1U; endpoint != kUSBMaxPipes; ++endpoint) {
		pRing = pSlot->ringArrayForEndpoint[endpoint];
		if (!pRing->isInactive())
			return kIOReturnSuccess;
	}
	bzero(&localTrb, sizeof localTrb);
	localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
	WaitForCMD(&localTrb, XHCI_TRB_TYPE_DISABLE_SLOT, 0);
	pSlot->ctx = 0;
	SetDCBAAAddr64(&_dcbaa.ptr[slot], 0ULL);
	pSlot->md->complete();
	pSlot->md->release();
	pSlot->md = 0;
	pSlot->physAddr = 0U;
	pSlot->_intelFlag = false;
	_addressMapper.HubAddress[functionNumber] = 0U;
	_addressMapper.PortOnHub[functionNumber] = 0U;
	_addressMapper.Slot[functionNumber] = 0U;
	_addressMapper.Active[functionNumber] = false;
	return kIOReturnSuccess;
}

IOReturn CLASS::UIMClearEndpointStall(short functionNumber, short endpointNumber, short direction)
{
	uint8_t slot, endpoint;
	ringStruct* pRing;
	ContextStruct* pContext;

	slot = GetSlotID(functionNumber);
	if (!slot)
		return kIOReturnInternalError;
	endpoint = TranslateEndpoint(endpointNumber, direction);
	if (!endpoint || endpoint >= kUSBMaxPipes)
		return kIOReturnBadArgument;
	pRing = GetRing(slot, endpoint, 0U);
	if (pRing->isInactive())
		return kIOReturnBadArgument;
	if (pRing->endpointUnusable)
		return kIOUSBClearPipeStallNotRecursive;
	pContext = GetSlotContext(slot, endpoint);
	if (XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0) != EP_STATE_HALTED)
		ClearEndpoint(slot, endpoint);
	return UIMAbortStream(UINT32_MAX, functionNumber, endpointNumber, direction);
}

void CLASS::UIMRootHubStatusChange(void)
{
#if 0
	uint16_t statusChangedBitmap = 0U;
	IOUSBHubStatus hubStatus;
	IOUSBHubPortStatus portStatus;
	uint32_t statusBit = 1U, portToCheck;
	uint32_t sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (m_invalid_regspace)
		return;
	if ((sts & XHCI_STS_HSE) && !_HSEDetected) {
		IOLog("%s: HSE bit set:%#x (1)\n", __FUNCTION__, sts);
		_HSEDetected = true;
	}
	if (!_controllerAvailable || _wakingFromHibernation)
		return;
	RHCheckForPortResumes();
	if (GetRootHubStatus(&hubStatus) != kIOReturnSuccess) {
		_rootHubStatusChangedBitmap = statusChangedBitmap;
		return;
	}
	if (hubStatus.statusFlags & (kHubLocalPowerStatus | kHubOverCurrentIndicator))
		statusChangedBitmap |= statusBit;
	statusBit <<= 1;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port, statusBit <<= 1) {
		if (_rhPortResetPending[port])
			continue;
		portStatus.statusFlags = 0U;
		portStatus.changeFlags = 0U;
		portToCheck = PortNumberCanonicalToProtocol(port, reinterpret_cast<uint8_t*>(&portStatus.statusFlags));
		if (!portToCheck)
			continue;
		if (GetRootHubPortStatus(&portStatus, portToCheck) != kIOReturnSuccess)
			continue;
		if ((portStatus.statusFlags & kHubPortConnection) &&
			!(portStatus.changeFlags & kHubPortConnection) &&
			_rhPortEmulateCSC[port])
			portStatus.changeFlags |= kHubPortConnection;
		if (!(portStatus.changeFlags & ~kSSHubPortChangeBHResetMask))
			continue;
		portStatus.changeFlags |= kHubPortConnection;
		statusChangedBitmap |= statusBit;
	}
#else
	uint16_t statusChangedBitmap = RHPortStatusChangeBitmapGrab();
	if (gux_log_level >= 2 && statusChangedBitmap)
		IOLog("%s: statusChangedBitmap == %#x\n", __FUNCTION__, statusChangedBitmap);
	statusChangedBitmap |= _rhPortStatusChangeBitmapGated;
	_rhPortStatusChangeBitmapGated = statusChangedBitmap;
#endif
	_rootHubStatusChangedBitmap = statusChangedBitmap;
}

IOReturn CLASS::GetRootHubDeviceDescriptor(IOUSBDeviceDescriptor* desc)
{
    static
	IOUSBDeviceDescriptor const newDesc2 =
	{
		sizeof(IOUSBDeviceDescriptor),			// UInt8 length;
		kUSBDeviceDesc,							// UInt8 descType;
		HostToUSBWord(kUSBRel20),				// UInt16 usbRel Supports USB 2.0;
		kUSBHubClass,							// UInt8 class;
		kUSBHubSubClass,						// UInt8 subClass;
		1,										// UInt8 protocol;
		9,										// UInt8 maxPacketSize;
		HostToUSBWord(kAppleVendorID),			// UInt16 vendor:  Use the Apple Vendor ID from USB-IF
		HostToUSBWord(kPrdRootHubAppleSS),		// UInt16 product:  All our root hubs are the same
		HostToUSBWord(0x0300),					// UInt16 devRel:
		2,										// UInt8 manuIdx;
		1,										// UInt8 prodIdx;
		0,										// UInt8 serialIdx;
		1										// UInt8 numConf;
	};
	static
    IOUSBDeviceDescriptor const newDesc3 =
	{
		sizeof(IOUSBDeviceDescriptor),			// UInt8 length;
		kUSBDeviceDesc,							// UInt8 descType;
		HostToUSBWord(kUSBRel30),				// UInt16 usbRel Supports USB 3.0;
		kUSBHubClass,							// UInt8 class;
		kUSBHubSubClass,						// UInt8 subClass;
		3,										// UInt8 protocol;
		9,										// UInt8 maxPacketSize;
		HostToUSBWord(kAppleVendorID),			// UInt16 vendor:  Use the Apple Vendor ID from USB-IF
		HostToUSBWord(kPrdRootHubAppleSS),		// UInt16 product:  All our root hubs are the same
		HostToUSBWord(0x0300),					// UInt16 devRel:
		2,										// UInt8 manuIdx;
		1,										// UInt8 prodIdx;
		0,										// UInt8 serialIdx;
		1										// UInt8 numConf;
	};

	if (!desc)
		return kIOReturnNoMemory;

	if ((desc->bLength & 3U) != 2U)
		bcopy(&newDesc3, desc, newDesc3.bLength);
	else
		bcopy(&newDesc2, desc, newDesc2.bLength);

	return kIOReturnSuccess;
}

IOReturn CLASS::GetRootHubDescriptor(IOUSBHubDescriptor* desc)
{
	IOUSBHubDescriptor hubDesc;
	uint32_t appleCaptive, i, numBytes;
	uint8_t* dstPtr;
	OSNumber* appleCaptiveProperty;

	hubDesc.length = sizeof(hubDesc);
	hubDesc.hubType = kUSBHubDescriptorType;
	hubDesc.numPorts = _v3ExpansionData->_rootHubNumPortsHS;
	hubDesc.characteristics = XHCI_HCC_PPC(_HCCLow) ? kPerPortSwitchingBit : 0U;
	hubDesc.powerOnToGood = 50U;
	hubDesc.hubCurrent = 0U;
	numBytes = ((hubDesc.numPorts + 1U) / 8U) + 1U;
	appleCaptiveProperty = OSDynamicCast(OSNumber, _device->getProperty(kAppleInternalUSBDevice));
	appleCaptive = appleCaptiveProperty ? appleCaptiveProperty->unsigned32BitValue() : 0U;
	dstPtr = &hubDesc.removablePortFlags[0];
	for (i = 0U; i < numBytes; i++) {
		*dstPtr++ = (uint8_t) (appleCaptive & 0xFFU);
        appleCaptive >>= 8;
	}
    for (i = 0U; i < numBytes; i++) {
        *dstPtr++ = 0xFFU;
    }
    hubDesc.length -= ((sizeof(hubDesc.removablePortFlags) - numBytes) +
                       (sizeof(hubDesc.pwrCtlPortFlags) - numBytes));
	if (!desc)
		return kIOReturnNoMemory;
	bcopy(&hubDesc, desc, hubDesc.length);
	return kIOReturnSuccess;
}

IOReturn CLASS::GetRootHubConfDescriptor(OSData* desc)
{
	static
	struct {
		IOUSBConfigurationDescriptor confDesc;
		IOUSBInterfaceDescriptor intfDesc;
		IOUSBEndpointDescriptor endptDesc;
		IOUSBSuperSpeedEndpointCompanionDescriptor compantionDesc;
	} __attribute__((packed)) const allDesc = {
		{
			sizeof(IOUSBConfigurationDescriptor),//UInt8 length;
			kUSBConfDesc,               //UInt8 descriptorType;
			HostToUSBWord(sizeof(IOUSBConfigurationDescriptor) +
						  sizeof(IOUSBInterfaceDescriptor) +
						  sizeof(IOUSBEndpointDescriptor) +
						  sizeof(IOUSBSuperSpeedEndpointCompanionDescriptor)),   //UInt16 totalLength;
			1,                          //UInt8 numInterfaces;
			1,                          //UInt8 configValue;
			0,                          //UInt8 configStrIndex;
			0x60,                       //UInt8 attributes; self powered, supports remote wkup
			0,                          //UInt8 maxPower;
		},
		{
			sizeof(IOUSBInterfaceDescriptor),//UInt8 length;
			kUSBInterfaceDesc,      //UInt8 descriptorType;
			0,                      //UInt8 interfaceNumber;
			0,                      //UInt8 alternateSetting;
			1,                      //UInt8 numEndpoints;
			kUSBHubClass,           //UInt8 interfaceClass;
			kUSBHubSubClass,        //UInt8 interfaceSubClass;
			0,                      //UInt8 interfaceProtocol;
			0                       //UInt8 interfaceStrIndex;
		},
		{
			sizeof(IOUSBEndpointDescriptor),	// UInt8 length;
			kUSBEndpointDesc,					// UInt8 descriptorType;
			0x81,								// UInt8  endpointAddress; In, 1
			0x10 | kUSBInterrupt,				// UInt8 attributes;
			HostToUSBWord(2),					// UInt16 maxPacketSize;
			9,									// UInt8 interval (256 microframes or 32 ms)
		},
		{
			sizeof(IOUSBSuperSpeedEndpointCompanionDescriptor),
			kUSBSuperSpeedEndpointCompanion,
			0,
			0,
			HostToUSBWord(2)
		}
	};
	if (!desc || !desc->appendBytes(&allDesc, sizeof allDesc))
		return(kIOReturnNoMemory);
	return kIOReturnSuccess;
}

IOReturn CLASS::GetRootHubStatus(IOUSBHubStatus* pStatus)
{
	if (pStatus)
		*(uint32_t*) pStatus = 0U;
	return kIOReturnSuccess;
}

IOReturn CLASS::GetRootHubPortStatus(IOUSBHubPortStatus* pStatus, UInt16 port)
{
	uint32_t portSC;
	uint16_t _port, statusFlags, changeFlags, linkState;
	uint8_t protocol, speed;

	if (!pStatus)
		return kIOReturnBadArgument;
	/*
	 * Note: Bus is never set to kUSBBusStateSuspended...
	 */
	if (_myBusState == kUSBBusStateSuspended)
		return kIOReturnNotResponding;
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	/*
	 * Note: "protocol" is actually the speed of the requesting IOUSBRootHubDevice
	 *   which is either kUSBDeviceSpeedHigh (2) or kUSBDeviceSpeedSuper (3).
	 *   See IOUSBRootHubDevice::DeviceRequestWorker
	 */
	protocol = static_cast<uint8_t>(pStatus->statusFlags & kUSBSpeed_Mask);
	_port = PortNumberProtocolToCanonical(port, protocol);
	if (_port >= _rootHubNumPorts)
		return kIOReturnBadArgument;
	portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[_port].PortSC);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (RHCheckForPortResume(_port, protocol, portSC))
		portSC &= ~XHCI_PS_PLC;
	/*
	 * Note overlap with
	 *   kHubPortBeingReset | kHubPortOverCurrent | kHubPortEnabled | kHubPortConnection
	 */
	statusFlags = static_cast<uint16_t>(portSC & (XHCI_PS_PR | XHCI_PS_OCA | XHCI_PS_PED | XHCI_PS_CCS));
	linkState = static_cast<uint16_t>(XHCI_PS_PLS_GET(portSC));
	if (protocol != 3U) {
		if (linkState == XDEV_U3 || linkState == XDEV_RESUME)
			statusFlags |= kHubPortSuspend;
		speed = static_cast<uint8_t>(XHCI_PS_SPEED_GET(portSC));
		/*
		 * Note: Full Speed is default for USB2 port
		 */
		if (speed == XDEV_HS)
			statusFlags |= kHubPortHighSpeed;
		else if (speed == XDEV_LS)
			statusFlags |= kHubPortLowSpeed;
		if (!XHCI_HCC_PPC(_HCCLow) || (portSC & XHCI_PS_PP))
			statusFlags |= kHubPortPower;
		/*
		 * Note: kHubPortTestMode may be set by reading PortPMSC
		 */
		if (XHCI_HCC_PIND(_HCCLow))
			statusFlags |= kHubPortIndicator;
		/*
		 * Extract PRC, OCC, PEC, CSC in correspondence with
		 *         PR,  OCA, PED, CCS above.
		 */
		changeFlags = static_cast<uint16_t>((portSC >> 17) & 0x1BU);
		if (portSC & XHCI_PS_PLC)
			changeFlags |= kHubPortSuspend;
	} else {
		statusFlags |= ((linkState << kSSHubPortStatusLinkStateShift) & kSSHubPortStatusLinkStateMask);
		if (!XHCI_HCC_PPC(_HCCLow) || (portSC & XHCI_PS_PP))
			statusFlags |= kSSHubPortStatusPowerMask;
#if 0
		/*
		 * This is implied, as it is in fact zero
		 */
		statusFlags |= ((kSSHubPortSpeed5Gbps << kSSHubPortStatusSpeedShift) & kSSHubPortStatusSpeedMask);
#endif
		/*
		 * Note: PEC is always 0 for USB3 ports, but we report
		 *   it just in case chip is non-conforming so it gets cleared.
		 */
		changeFlags = static_cast<uint16_t>(((portSC >> 17) & 0x1BU) |	// PRC, OCC, PEC, CSC as above
											((portSC >> 16) & 0xC0U) |	// CEC, PLC as above
											((portSC >> 14) & kSSHubPortChangeBHResetMask));	// WRC
	}
#if 0
	if (_rhPortBeingWarmReset[_port]) {
		pStatus->statusFlags = HostToUSBWord(statusFlags | kHubPortConnection);
		pStatus->changeFlags = HostToUSBWord(0U);
		return kIOReturnSuccess;
	}
#endif
	if ((portSC & (XHCI_PS_CSC | XHCI_PS_CCS)) == XHCI_PS_CCS &&
		_rhPortEmulateCSC[_port])
		changeFlags |= kHubPortConnection;
#if 0
	HandlePortDebouncing(&statusFlags, &changeFlags, _port, linkState, protocol);
#endif
	if (!changeFlags)
		_rhPortStatusChangeBitmapGated &= ~(1U << (1U + _port));
	pStatus->statusFlags = HostToUSBWord(statusFlags);
	pStatus->changeFlags = HostToUSBWord(changeFlags);
	return kIOReturnSuccess;
}

IOReturn CLASS::SetRootHubPortFeature(UInt16 wValue, UInt16 port)
{
	uint16_t _port;
	uint8_t protocol;

	protocol = static_cast<uint8_t>(wValue & kUSBSpeed_Mask);
	wValue = (wValue & kUSBAddress_Mask) >> kUSBAddress_Shift;
	_port = PortNumberProtocolToCanonical(port & UINT8_MAX, protocol);
	if (_port >= _rootHubNumPorts)
		return kIOReturnBadArgument;
	++_port;
	if (gux_log_level >= 2)
		IOLog("%s(wValue %u, port %u)\n", __FUNCTION__, wValue, _port);
	switch (wValue) {
		case kUSBHubPortEnableFeature:
			return XHCIRootHubEnablePort(_port, true);
		case kUSBHubPortSuspendFeature:
			return XHCIRootHubSuspendPort(protocol, _port, true);
		case kUSBHubPortResetFeature:
			return XHCIRootHubResetPort(protocol, _port);
		case kUSBHubPortLinkStateFeature:
			if (protocol != kUSBDeviceSpeedSuper)
				return kIOReturnUnsupported;
			return XHCIRootHubSetLinkStatePort(static_cast<uint8_t>(port >> 8), _port);
		case kUSBHubPortPowerFeature:
			XHCIRootHubPowerPort(_port, true);
			break;
		case kUSBHubPortBHPortResetFeature:
			if (protocol != kUSBDeviceSpeedSuper)
				return kIOReturnUnsupported;
			return XHCIRootHubWarmResetPort(_port);
		default:
			return kIOReturnUnsupported;
	}
	return kIOReturnSuccess;
}

IOReturn CLASS::ClearRootHubPortFeature(UInt16 wValue, UInt16 port)
{
	uint16_t _port;
	uint8_t protocol;

	protocol = static_cast<uint8_t>(wValue & kUSBSpeed_Mask);
	wValue = (wValue & kUSBAddress_Mask) >> kUSBAddress_Shift;
	_port = PortNumberProtocolToCanonical(port, protocol);
	if (_port >= _rootHubNumPorts)
		return kIOReturnBadArgument;
	++_port;
	if (gux_log_level >= 2)
		IOLog("%s(wValue %u, port %u)\n", __FUNCTION__, wValue, _port);
	switch (wValue) {
		case kUSBHubPortEnableFeature:
			return XHCIRootHubEnablePort(_port, false);
		case kUSBHubPortSuspendFeature:
			return XHCIRootHubSuspendPort(protocol, _port, false);
		case kUSBHubPortPowerFeature:
			return XHCIRootHubPowerPort(_port, false);
		case kUSBHubPortConnectionChangeFeature:
			return XHCIRootHubClearPortConnectionChange(_port);
		case kUSBHubPortEnableChangeFeature:
			/*
			 * Shouldn't happen if protocol == kUSBDeviceSpeedSuper
			 */
			return XHCIRootHubClearPortChangeBit(_port, XHCI_PS_PEC);
		case kUSBHubPortSuspendChangeFeature:
			/*
			 * Shouldn't happen if protocol == kUSBDeviceSpeedSuper
			 *   For USB2 port, maps to PLC
			 */
			return XHCIRootHubClearPortChangeBit(_port, XHCI_PS_PLC);
		case kUSBHubPortOverCurrentChangeFeature:
			return XHCIRootHubClearPortChangeBit(_port, XHCI_PS_OCC);
		case kUSBHubPortResetChangeFeature:
			return XHCIRootHubClearPortChangeBit(_port, XHCI_PS_PRC);
		case kUSBHubPortLinkStateChangeFeature:
			/*
			 * Shouldn't happen if protocol != kUSBDeviceSpeedSuper
			 */
			return XHCIRootHubClearPortChangeBit(_port, XHCI_PS_PLC);
		case kUSBHubPortConfigErrorChangeFeature:
			/*
			 * Shouldn't happen if protocol != kUSBDeviceSpeedSuper
			 */
			return XHCIRootHubClearPortChangeBit(_port, XHCI_PS_CEC);
		case kUSBHubPortBHResetChangeFeature:
			/*
			 * Shouldn't happen if protocol != kUSBDeviceSpeedSuper
			 */
			return XHCIRootHubClearPortChangeBit(_port, XHCI_PS_WRC);
		default:
			return kIOReturnUnsupported;
	}
	return kIOReturnSuccess;
}

IOReturn CLASS::SetHubAddress(UInt16 wValue)
{
	if ((wValue & kUSBSpeed_Mask) == kUSBDeviceSpeedSuper)
		_hub3Address = (wValue & kUSBAddress_Mask) >> kUSBAddress_Shift;
	else
		_hub2Address = (wValue & kUSBAddress_Mask) >> kUSBAddress_Shift;
	return kIOReturnSuccess;
}

UInt64 CLASS::GetFrameNumber(void)
{
	return GetMicroFrameNumber() >> 3;
}

UInt32 CLASS::GetFrameNumber32(void)
{
	return static_cast<uint32_t>(GetFrameNumber());
}

void CLASS::PollInterrupts(IOUSBCompletionAction safeAction)
{
	uint32_t sts;

	sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (m_invalid_regspace)
		return;
	if (sts & XHCI_STS_PCD) {
		Write32Reg(&_pXHCIOperationalRegisters->USBSts, XHCI_STS_PCD);
		EnsureUsability();
		if (_myPowerState == kUSBPowerStateOn)
			RHCheckForPortResumes();
	}
	for (int32_t interrupter = 0; interrupter < kMaxActiveInterrupters; ++interrupter)
		while (PollEventRing2(interrupter));
}

IOReturn CLASS::GetRootHubStringDescriptor(UInt8 index, OSData* desc)
{
    // The following strings are in Unicode format
    //
    UInt8 productName3[] = {
		0,			// Length Byte
		kUSBStringDesc,	// Descriptor type
		0x58, 0x00, // "X"
		0x48, 0x00, // "H"
		0x43, 0x00, // "C"
		0x49, 0x00, // "I"
		0x20, 0x00,	// " "
		0x52, 0x00,	// "R"
		0x6F, 0x00,	// "o"
		0x6f, 0x00,	// "o"
		0x74, 0x00,	// "t"
		0x20, 0x00,	// " "
		0x48, 0x00,	// "H"
		0x75, 0x00,	// "u"
		0x62, 0x00,	// "b"
		0x20, 0x00,	// " "
		0x53, 0x00, // "S"
		0x53, 0x00, // "S"
		0x20, 0x00,	// " "
		0x53, 0x00, // "S"
		0x69, 0x00,	// "i"
		0x6d, 0x00,	// "m"
		0x75, 0x00,	// "u"
		0x6c, 0x00,	// "l"
		0x61, 0x00,	// "a"
		0x74, 0x00,	// "t"
		0x69, 0x00,	// "i"
		0x6f, 0x00,	// "o"
		0x6e, 0x00,	// "n"
	};
    UInt8 productName2[] = {
		0,			// Length Byte
		kUSBStringDesc,	// Descriptor type
		0x58, 0x00, // "X"
		0x48, 0x00, // "H"
		0x43, 0x00, // "C"
		0x49, 0x00, // "I"
		0x20, 0x00,	// " "
		0x52, 0x00,	// "R"
		0x6F, 0x00,	// "o"
		0x6f, 0x00,	// "o"
		0x74, 0x00,	// "t"
		0x20, 0x00,	// " "
		0x48, 0x00,	// "H"
		0x75, 0x00,	// "u"
		0x62, 0x00,	// "b"
		0x20, 0x00,	// " "
		0x55, 0x00, // "U"
		0x53, 0x00, // "S"
		0x42, 0x00, // "B"
		0x20, 0x00,	// " "
		0x32, 0x00, // "2"
		0x2E, 0x00, // "."
		0x30, 0x00, // "0"
		0x20, 0x00,	// " "
		0x53, 0x00, // "S"
		0x69, 0x00,	// "i"
		0x6d, 0x00,	// "m"
		0x75, 0x00,	// "u"
		0x6c, 0x00,	// "l"
		0x61, 0x00,	// "a"
		0x74, 0x00,	// "t"
		0x69, 0x00,	// "i"
		0x6f, 0x00,	// "o"
		0x6e, 0x00,	// "n"
	};
    UInt8 vendorName[] = {
		0,			// Length Byte
		kUSBStringDesc,	// Descriptor type
		0x41, 0x00,	// "A"
		0x43, 0x00,	// "C"
		0x4D, 0x00,	// "M"
		0x45, 0x00,	// "E"
		0x20, 0x00,	// " "
		0x49, 0x00,	// "I"
		0x6e, 0x00,	// "n"
		0x63, 0x00,	// "c"
		0x2e, 0x00	// "."
	};

    // According to our device descriptor, index 1 is product, index 2 is Manufacturer
    //
    if ((index > 2) || (index == 0))
        return kIOReturnBadArgument;
	if (!desc)
		return kIOReturnNoMemory;

    // Set the length of our strings
    //
    vendorName[0] = sizeof vendorName;
    productName3[0] = sizeof productName3;
    productName2[0] = sizeof productName2;

    if (index == 1) {
		bool b;
		uint8_t const* p = static_cast<uint8_t const*>(desc->getBytesNoCopy());
		if (!p || ((*p) & 3U) != 2U)
			b = desc->appendBytes(&productName3, productName3[0]);
		else
			b = desc->appendBytes(&productName2, productName2[0]);
		if (!b)
            return kIOReturnNoMemory;
    }
    if (index == 2) {
        if (!desc->appendBytes(&vendorName[0], vendorName[0]))
            return kIOReturnNoMemory;
    }
    return kIOReturnSuccess;
}
