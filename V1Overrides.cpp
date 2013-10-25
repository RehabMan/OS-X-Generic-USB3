//
//  V1Overrides.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on December 26th, 2012.
//  Copyright (c) 2012-2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "Isoch.h"
#include "XHCITypes.h"
#include <IOKit/IOTimerEventSource.h>
#include <libkern/version.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark IOUSBController Overrides
#pragma mark -

__attribute__((noinline))
UInt32 CLASS::GetErrataBits(UInt16 vendorID, UInt16 deviceID, UInt16 revisionID)
{
	ErrataListEntry const errataList[] = {
		{ kVendorFrescoLogic, 0x1000U, 0U, UINT16_MAX, kErrataDisableMSI },	// Fresco Logic FL1000
		{ kVendorFrescoLogic, 0x1100U, 0U, 15U, kErrataFL1100LowRev | kErrataParkRing },	// Fresco Logic FL1100, rev 0 - 15
		{ kVendorFrescoLogic, 0x1100U, 16U, UINT16_MAX, kErrataParkRing }, // Fresco Logic FL1100, rev 16 and up
		{ kVendorIntel, 0x1E31U, 0U, UINT16_MAX,
			kErrataSWAssistedIdle |
			kErrataParkRing | kErrataIntelPortMuxing |
			kErrataEnableAutoCompliance | kErrataIntelPantherPoint },	// Intel Series 7/C210
		{ kVendorIntel, 0x8C31U, 0U, UINT16_MAX, kErrataEnableAutoCompliance | kErrataParkRing | kErrataIntelLynxPoint },	// Intel Series 8/C220
		{ kVendorIntel, 0x9C31U, 0U, UINT16_MAX, kErrataEnableAutoCompliance | kErrataParkRing | kErrataIntelLynxPoint },	// Intel Lynx Point
		{ kVendorVMware, 0x778U, 0U, UINT16_MAX, kErrataVMwarePortSwap },	// VMware Virtual xHC
		{ kVendorEtron, 0U, 0U, UINT16_MAX, kErrataBrokenStreams },		// All Etron
		{ kVendorASMedia, 0x1042, 0U, UINT16_MAX, kErrataBrokenStreams }	// ASMedia 1042
	};
	ErrataListEntry const* entryPtr;
	uint32_t i, errata = 0U;
	for (i = 0U, entryPtr = &errataList[0]; i < (sizeof(errataList) / sizeof(errataList[0])); ++i, ++entryPtr)
		if (vendorID == entryPtr->vendID &&
			(deviceID == entryPtr->deviceID || !entryPtr->deviceID) &&
			revisionID >= entryPtr->revisionLo &&
			revisionID <= entryPtr->revisionHi)
			errata |= entryPtr->errata;
	if ((gux_options & GUX_OPTION_NO_INTEL_IDLE) || version_major >= 13)
		errata &= ~kErrataSWAssistedIdle;
	if (gux_options & GUX_OPTION_NO_MSI)
		errata |= kErrataDisableMSI;
	/*
	 * Note: This is done in GetErrata64Bits in Mavericks
	 */
	if (CHECK_FOR_MAVERICKS)
		return errata;
	if (getProperty(kIOPCITunnelledKey, gIOServicePlane) == kOSBooleanTrue) {
		_v3ExpansionData->_onThunderbolt = true;
		requireMaxBusStall(25000U);
	}
	return errata;
}

#define FlushAndReturn do { _completer.Flush(); return; } while (false)

void CLASS::UIMCheckForTimeouts(void)
{
	uint32_t frameNumber, sts, mfIndex;
	uint8_t slot;
	bool isAssociatedRHPortEnabled;

	if (!_controllerAvailable || _wakingFromHibernation)
		return;
	frameNumber = GetFrameNumber32();
	sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (m_invalid_regspace) {
		for (slot = 1U; slot <= _numSlots; ++slot)
			CheckSlotForTimeouts(slot, 0U, false);
		if (_expansionData) {
			_watchdogTimerActive = false;
			if (_watchdogUSBTimer)
				_watchdogUSBTimer->cancelTimeout();
		}
		FlushAndReturn;
	}
	if ((sts & XHCI_STS_HSE) && !_HSEDetected) {
		IOLog("%s: HSE bit set:%x (1)\n", __FUNCTION__, sts);
		_HSEDetected = true;
	}
	for (slot = 1U; slot <= _numSlots; ++slot) {
		isAssociatedRHPortEnabled = IsStillConnectedAndEnabled(slot);
		SlotPtr(slot)->oneBitCache = isAssociatedRHPortEnabled;
		if (!isAssociatedRHPortEnabled)
			CheckSlotForTimeouts(slot, frameNumber, false);
	}
	if (_powerStateChangingTo != kUSBPowerStateStable && _powerStateChangingTo < kUSBPowerStateOn && _powerStateChangingTo > kUSBPowerStateRestart)
		FlushAndReturn;
	mfIndex = Read32Reg(&_pXHCIRuntimeRegisters->MFIndex);
	if (m_invalid_regspace)
		FlushAndReturn;
	mfIndex &= XHCI_MFINDEX_MASK;
	if (!mfIndex)
		FlushAndReturn;
	for (slot = 1U; slot <= _numSlots; ++slot)
		if (ConstSlotPtr(slot)->oneBitCache)
			CheckSlotForTimeouts(slot, frameNumber, true);
	FlushAndReturn;
}

#undef FlushAndReturn

IOReturn CLASS::UIMCreateControlTransfer(short functionNumber, short endpointNumber, IOUSBCommand* command,
										 IOMemoryDescriptor* CBP, bool /* bufferRounding */, UInt32 bufferSize,
										 short direction)
{
	IOReturn rc;
	uint32_t mystery;	// Note: structure as fourth uint32_t of Transfer TRB
	uint8_t slot, immediateDataSize;
	ringStruct* pRing;
	SetupStageHeader smallbuf1;
	SetupStageHeader smallbuf2;

	slot = GetSlotID(functionNumber);
	if (!slot)
		return kIOUSBEndpointNotFound;
	if (endpointNumber)
		return kIOReturnBadArgument;
#if 0
	/*
	 * Note: Added Mavericks
	 */
	if (!IsStillConnectedAndEnabled(slot))
		return kIOReturnNoDevice;
#endif
	pRing = GetRing(slot, 1, 0U);
	if (pRing->isInactive())
		return kIOReturnBadArgument;
	if (GetNeedsReset(slot))
		return AddDummyCommand(pRing, command);
	if (pRing->deleteInProgress)
		return kIOReturnNoDevice;
	if (pRing->epType != CTRL_EP)
		return kIOUSBEndpointNotFound;
	XHCIAsyncEndpoint* pAsyncEp = pRing->asyncEndpoint;
	if (!pAsyncEp)
		return kIOUSBEndpointNotFound;
	if (pAsyncEp->aborting)
		return kIOReturnNotPermitted;
	if (CBP && bufferSize) {
		IODMACommand* dmac = command->GetDMACommand();
		if (!dmac) {
			IOLog("%s: no dmaCommand\n", __FUNCTION__);
			return kIOReturnNoMemory;
		}
		IOMemoryDescriptor const* dmac_md = dmac->getMemoryDescriptor();
		if (dmac_md != CBP) {
			IOLog("%s: mismatched CBP (%p) and dmaCommand memory descriptor (%p)\n", __FUNCTION__,
				  CBP, dmac_md);
			return kIOReturnInternalError;
		}
	}
	bzero(&smallbuf1, sizeof smallbuf1);
	if (direction == kUSBNone) {
		if (bufferSize != sizeof smallbuf2)
			return kIOReturnBadArgument;
		if (CBP->readBytes(0U, &smallbuf2, sizeof smallbuf2) != sizeof smallbuf2)
			return kIOReturnInternalError;
		if (smallbuf2.bmRequestType == 0U && smallbuf2.bRequest == 5U) { /* kSetAddress */
			uint16_t deviceAddress = smallbuf2.wValue;
			_deviceZero.isBeingAddressed = true;
			_addressMapper.HubAddress[deviceAddress] = static_cast<uint8_t>(_deviceZero.HubAddress);
			_addressMapper.PortOnHub[deviceAddress] = static_cast<uint8_t>(_deviceZero.PortOnHub);
			_addressMapper.Slot[deviceAddress] = static_cast<uint8_t>(slot);
			_addressMapper.Active[deviceAddress] = true;
			_deviceZero.HubAddress = 0U;
			_deviceZero.PortOnHub = 0U;
			mystery = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NOOP) | XHCI_TRB_3_IOC_BIT;
			immediateDataSize = 0U;
		} else {
			mystery = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_SETUP_STAGE) | XHCI_TRB_3_IDT_BIT | XHCI_TRB_3_IOC_BIT;
			/*
			 * Set TRT field
			 */
			if (smallbuf2.wLength >= 1U)
				/*
				 * Note: Upper bit of bmRequestType is transfer direction (1 - in, 0 - out)
				 */
				mystery |= (smallbuf2.bmRequestType & 0x80U) ? XHCI_TRB_3_TRT_IN : XHCI_TRB_3_TRT_OUT;
			// else XHCI_TRB_3_TRT_NONE
			bcopy(&smallbuf2, &smallbuf1, sizeof smallbuf1);
			immediateDataSize = sizeof smallbuf1;
		}
	} else if (bufferSize) {
		mystery = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_DATA_STAGE);
		if (direction == kUSBIn)
			mystery |= XHCI_TRB_3_DIR_IN;
		immediateDataSize = 0xFFU;	// Note: means data is not immediate
	} else if (_deviceZero.isBeingAddressed) {
		_addressMapper.Slot[0] = 0U;
		_addressMapper.Active[0] = false;
		_deviceZero.isBeingAddressed = false;
		mystery = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NOOP) | XHCI_TRB_3_IOC_BIT;
		immediateDataSize = 0U;
	} else {
		mystery = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_STATUS_STAGE) | XHCI_TRB_3_IOC_BIT;
		if (direction == kUSBIn)
			mystery |= XHCI_TRB_3_DIR_IN;
		immediateDataSize = 0U;
	}
	rc = pAsyncEp->CreateTDs(command, 0U, mystery, immediateDataSize, reinterpret_cast<uint8_t const*>(&smallbuf1));
	pAsyncEp->ScheduleTDs();
	return rc;
}

IOReturn CLASS::UIMCreateBulkTransfer(IOUSBCommand* command)
{
	if (!command)
		return kIOReturnBadArgument;
	return CreateTransfer(command, command->GetStreamID());
}

IOReturn CLASS::UIMCreateInterruptTransfer(IOUSBCommand* command)
{
	IOUSBCompletion comp;
	IOMemoryDescriptor* md;
	USBDeviceAddress addr;
	IODMACommand* dmac;
	uint32_t speed, tag;

	if (!command)
		return kIOReturnBadArgument;
	addr = command->GetAddress();
	if (addr == _hub3Address || addr == _hub2Address) {
		comp = command->GetUSLCompletion();
		dmac = command->GetDMACommand();
		if (dmac && dmac->getMemoryDescriptor())
				dmac->clearMemoryDescriptor();
		if (command->GetEndpoint() == 1U) {
			md = command->GetBuffer();
			if (!md)
				return kIOReturnInternalError;
			speed = (addr == _hub3Address ? kUSBDeviceSpeedSuper : kUSBDeviceSpeedHigh);
			tag = ((static_cast<uint32_t>(addr) << kUSBAddress_Shift) & kUSBAddress_Mask);
			tag |= ((speed << kUSBSpeed_Shift) & kUSBSpeed_Mask);
			md->setTag(tag);
			return RootHubQueueInterruptRead(md, static_cast<uint32_t>(command->GetReqCount()), comp);
		} else {
			Complete(comp, kIOUSBEndpointNotFound, static_cast<uint32_t>(command->GetReqCount()));
			return kIOUSBEndpointNotFound;
		}
	}
	return CreateTransfer(command, 0U);
}

IOReturn CLASS::UIMCreateIsochTransfer(IOUSBIsocCommand* command)
{
	uint64_t curFrameNumber, frameNumberStart;
	IODMACommand* dmac;
	GenericUSBXHCIIsochEP* pIsochEp;
	GenericUSBXHCIIsochTD* pIsochTd;
	IOUSBIsocFrame* pFrames;
	IOUSBLowLatencyIsocFrame* pLLFrames;
	size_t transferOffset;
	uint32_t transferCount, updateFrequency, epInterval, transfersPerTD, frameNumberIncrease, frameCount, framesBeforeInterrupt, transfer;
	bool lowLatency, newFrame;

	/*
	 * Note: See UIMCreateIsochTransfer in AppleUSBXHCIUIM.cpp for reference
	 */
	if (!command)
		return kIOReturnBadArgument;
	curFrameNumber = GetFrameNumber();
#if 0
	/*
	 * Note: Added Mavericks
	 */
	if (!IsStillConnectedAndEnabled(GetSlotID(command->GetAddress())))
		return kIOReturnNoDevice;
#endif
	transferCount = command->GetNumFrames();
	if (!transferCount || transferCount > 1000U)
		return kIOReturnBadArgument;
	lowLatency = command->GetLowLatency();
	updateFrequency = command->GetUpdateFrequency();
	pFrames = command->GetFrameList();
	pLLFrames = reinterpret_cast<IOUSBLowLatencyIsocFrame*>(pFrames);
	frameNumberStart = command->GetStartFrame();
	pIsochEp = OSDynamicCast(GenericUSBXHCIIsochEP,
							 FindIsochronousEndpoint(command->GetAddress(),
													 command->GetEndpoint(),
													 command->GetDirection(),
													 0));
	if (!pIsochEp)
		return kIOUSBEndpointNotFound;
	if (pIsochEp->aborting)
		return kIOReturnNotPermitted;
	if (pIsochEp->pRing->isInactive())
		return kIOReturnBadArgument;
	if (pIsochEp->pRing->deleteInProgress)
		return kIOReturnNoDevice;
	if (frameNumberStart == kAppleUSBSSIsocContinuousFrame)
		pIsochEp->continuousStream = true;
	else {
		if (frameNumberStart < pIsochEp->firstAvailableFrame)
			return kIOReturnIsoTooOld;
		if (pIsochEp->continuousStream)
			return kIOReturnBadArgument;
	}
	newFrame = false;
	if (!pIsochEp->continuousStream) {
		if (frameNumberStart != pIsochEp->firstAvailableFrame)
			newFrame = true;
		pIsochEp->firstAvailableFrame = frameNumberStart;
		if (static_cast<int64_t>(frameNumberStart - curFrameNumber) < -1024)
			return kIOReturnIsoTooOld;
		else if (static_cast<int64_t>(frameNumberStart - curFrameNumber) > 1024)
			return kIOReturnIsoTooNew;
	}
	dmac = command->GetDMACommand();
	if (!dmac || !dmac->getMemoryDescriptor()) {
		IOLog("%s: no DMA Command or missing memory descriptor\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	epInterval = pIsochEp->interval;
	if (epInterval >= 8U) {
		transfersPerTD = 1U;
		frameNumberIncrease = epInterval / 8U;
	} else {
		transfersPerTD = 8U / epInterval;
		frameNumberIncrease = 1U;
	}
	if (!pIsochEp->continuousStream) {
		if (newFrame &&
			frameNumberIncrease > 1U &&
			(frameNumberStart % frameNumberIncrease))
			return kIOReturnBadArgument;
	}
	if (transferCount % transfersPerTD)
		return kIOReturnBadArgument;
	if (!updateFrequency)
		updateFrequency = 8U;
	if (lowLatency && updateFrequency < 8U)
		framesBeforeInterrupt = updateFrequency;
	else
		framesBeforeInterrupt = 8U;
	frameCount = 0U;
	transferOffset = 0U;
	pIsochTd = 0;
	for (uint32_t baseTransferIndex = 0U; baseTransferIndex < transferCount; baseTransferIndex += transfersPerTD) {
		pIsochTd = GenericUSBXHCIIsochTD::ForEndpoint(pIsochEp);
		if (!pIsochTd)
			return kIOReturnNoMemory;
		pIsochTd->_lowLatency = lowLatency;
		pIsochTd->_framesInTD = 0U;
		pIsochTd->newFrame = newFrame;
		pIsochTd->interruptThisTD = false;
		if (frameCount > framesBeforeInterrupt) {
			pIsochTd->interruptThisTD = true;
			frameCount -= framesBeforeInterrupt;
		}
		pIsochEp->firstAvailableFrame += frameNumberIncrease;
		if (frameNumberIncrease == 1U)
			newFrame = false;
		pIsochTd->transferOffset = transferOffset;
		for (transfer = 0U;
			 transfer < transfersPerTD && (baseTransferIndex + transfer) < transferCount;
			 ++transfer) {
			if (lowLatency) {
				pLLFrames[baseTransferIndex + transfer].frStatus = kUSBLowLatencyIsochTransferKey;
				transferOffset += pLLFrames[baseTransferIndex + transfer].frReqCount;
			} else
				transferOffset += pFrames[baseTransferIndex + transfer].frReqCount;
		}
		pIsochTd->_framesInTD = static_cast<uint8_t>(transfer);
		pIsochTd->_pFrames = pFrames;
		pIsochTd->_frameNumber = frameNumberStart;
		pIsochTd->_frameIndex = baseTransferIndex;
		pIsochTd->_completion.action = 0;
		pIsochTd->_pEndpoint = pIsochEp;
		pIsochTd->command = command;
		PutTDonToDoList(pIsochEp, pIsochTd);
		frameNumberStart += frameNumberIncrease;
		frameCount += frameNumberIncrease;
	}
	if (!pIsochTd)
		return kIOReturnInternalError;
	pIsochTd->_completion = command->GetUSLCompletion();
	pIsochTd->interruptThisTD = true;
	AddIsocFramesToSchedule(pIsochEp);
	return kIOReturnSuccess;
}
