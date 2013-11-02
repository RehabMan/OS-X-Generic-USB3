//
//  Transfers.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on February 10th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "Isoch.h"
#include "XHCITypes.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Transfers
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::CreateTransfer(IOUSBCommand* command, uint32_t streamId)
{
	uint8_t slot = GetSlotID(command->GetAddress());
	if (!slot)
		return kIOUSBEndpointNotFound;
#if 0
	/*
	 * Note: Added Mavericks
	 */
	if (!IsStillConnectedAndEnabled(slot))
		return kIOReturnNoDevice;
#endif
	uint8_t endpoint = TranslateEndpoint(command->GetEndpoint(), command->GetDirection());
	if (endpoint < 2U || endpoint >= kUSBMaxPipes)
		return kIOReturnBadArgument;
	if (streamId == 1U && !IsStreamsEndpoint(slot, endpoint))
		streamId = 0U;
	ringStruct* pRing = GetRing(slot, endpoint, streamId);
	if (pRing->isInactive())
		return kIOReturnBadArgument;
	if (GetNeedsReset(slot))
		return AddDummyCommand(pRing, command);
	if (pRing->deleteInProgress)
		return kIOReturnNoDevice;
	if ((pRing->epType | CTRL_EP) == ISOC_IN_EP)
		return kIOUSBEndpointNotFound;
	XHCIAsyncEndpoint* pAsyncEP = pRing->asyncEndpoint;
	if (!pAsyncEP)
		return kIOUSBEndpointNotFound;
	if (pAsyncEP->aborting)
		return kIOReturnNotPermitted;
	switch (XHCI_EPCTX_0_EPSTATE_GET(GetSlotContext(slot, endpoint)->_e.dwEpCtx0)) {
		case EP_STATE_HALTED:
		case EP_STATE_ERROR:
			return kIOUSBPipeStalled;
	}
	IOReturn rc = pAsyncEP->CreateTDs(command, static_cast<uint16_t>(streamId), 0U, 0xFFU, 0);
	pAsyncEP->ScheduleTDs();
	return rc;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::ReturnAllTransfersAndReinitRing(int32_t slot, int32_t endpoint, uint32_t streamId)
{
	ringStruct* pRing = GetRing(slot, endpoint, streamId);
	if (pRing->isInactive())
		return kIOReturnBadArgument;
	if (!pRing->ptr)
		return kIOReturnBadArgument;
	if ((pRing->epType | CTRL_EP) == ISOC_IN_EP) {
		GenericUSBXHCIIsochEP* pIsochEp = pRing->isochEndpoint;
		if (pIsochEp) {
			for (int32_t count = 0; count < 120 && pIsochEp->tdsScheduled; ++count)
				IOSleep(1U);
			if (pIsochEp->tdsScheduled)
				pIsochEp->tdsScheduled = false;
			AbortIsochEP(pIsochEp);
		}
	}
	if (pRing->dequeueIndex != pRing->enqueueIndex &&
		pRing->asyncEndpoint)
		pRing->asyncEndpoint->Abort();
	pRing->dequeueIndex = pRing->enqueueIndex;
	return ReinitTransferRing(slot, endpoint, streamId);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::ReinitTransferRing(int32_t slot, int32_t endpoint, uint32_t streamId)
{
	int32_t retFromCMD;
	ringStruct* pRing = GetRing(slot, endpoint, streamId);
	if (!pRing)
		return kIOReturnBadArgument;
	if (pRing->isInactive() ||
		!pRing->ptr ||
		pRing->numTRBs < 2U)
		return kIOReturnNoMemory;
	if (pRing->dequeueIndex == pRing->enqueueIndex) {
		bzero(pRing->ptr, static_cast<size_t>(pRing->numTRBs - 1U) * sizeof *pRing->ptr);
		pRing->ptr[pRing->numTRBs - 1U].d = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK) | XHCI_TRB_3_TC_BIT;
		pRing->cycleState = 1U;
		pRing->enqueueIndex = 0U;
		pRing->dequeueIndex = 0U;
		pRing->lastSeenDequeueIndex = 0U;
		pRing->lastSeenFrame = 0U;
		pRing->nextIsocFrame = 0ULL;
	}
#if 0
	/*
	 * Added Mavericks
	 */
	if (!_controllerAvailable) {
		pRing->needsSetTRDQPtr = true;
		return kIOReturnSuccess;
	}
#endif
	retFromCMD = SetTRDQPtr(slot, endpoint, streamId, pRing->dequeueIndex);
	/*
	 * Note: Ignore a context state error because CreateEndpoint doesn't
	 *   care, and UIMAbortStream should succeed even if called e.g. on
	 *   a disabled endpoint.
	 */
	if (retFromCMD == -1000 - XHCI_TRB_ERROR_CONTEXT_STATE)
		return kIOReturnSuccess;
	if (pRing->needsDoorbell) {
		if ((pRing->epType | CTRL_EP) != ISOC_IN_EP &&
			pRing->asyncEndpoint)
			pRing->asyncEndpoint->ScheduleTDs();
		if (IsStreamsEndpoint(slot, endpoint))
			RestartStreams(slot, endpoint, 0U);
		else
			StartEndpoint(slot, endpoint, 0U);
		pRing->needsDoorbell = false;
	}
	return TranslateCommandCompletion(retFromCMD);
}

__attribute__((visibility("hidden")))
int32_t CLASS::SetTRDQPtr(int32_t slot, int32_t endpoint, uint32_t streamId, int32_t index)
{
	TRBStruct localTrb = { 0 };
	int32_t retFromCMD;
	ringStruct* pRing = GetRing(slot, endpoint, streamId);
	if (!pRing)
		return -1256;
	ContextStruct* pContext = GetSlotContext(slot, endpoint);
	switch (XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0)) {
		case EP_STATE_STOPPED:
		case EP_STATE_ERROR:
			break;
		case EP_STATE_RUNNING:
			if (IsStreamsEndpoint(slot, endpoint) &&
				pRing->dequeueIndex == pRing->enqueueIndex)
				break;
		default:
			return -1000 - XHCI_TRB_ERROR_CONTEXT_STATE;
	}
	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	localTrb.d |= XHCI_TRB_3_EP_SET(endpoint);
	SetTRBAddr64(&localTrb, pRing->physAddr + index * sizeof(TRBStruct));
	/*
	 * Note:
	 *   If emptying ring, use enqueue cycle-state, otherwise
	 *   assume index TRB has not been evaluated yet and trust
	 *   its cycle-state.
	 */
	if (index == pRing->enqueueIndex) {
		if (pRing->cycleState)
			localTrb.a |= XHCI_TRB_3_CYCLE_BIT;
	} else {
		if (pRing->ptr[index].d & XHCI_TRB_3_CYCLE_BIT)
			localTrb.a |= XHCI_TRB_3_CYCLE_BIT;
	}
	if (streamId) {
		localTrb.a |= 2U;  // Note: SCT = 1U - Primary Transfer Ring
		localTrb.c |= XHCI_TRB_2_STREAM_SET(streamId);
	}
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_SET_TR_DEQUEUE, 0);
	if (retFromCMD != -1 && retFromCMD > -1000) {
		pRing->dequeueIndex = static_cast<uint16_t>(index);
		return retFromCMD;
	}
#if 0
	PrintContext(GetSlotContext(slot));
	PrintContext(pContext);
#endif
	return retFromCMD;
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
	if (QuiesceEndpoint(slot, endpoint) == EP_STATE_DISABLED)
		return;
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
void CLASS::ClearStopTDs(int32_t slot, int32_t endpoint)
{
	SlotStruct* pSlot = SlotPtr(slot);
	ringStruct* pRing = pSlot->ringArrayForEndpoint[endpoint];
	if (!pRing)
		return;
	if (pSlot->IsStreamsEndpoint(endpoint)) {
		uint16_t lastStream = pSlot->lastStreamForEndpoint[endpoint];
		for (uint16_t streamId = 1U; streamId <= lastStream; ++streamId)	// Note: originally <
			ClearTRB(&pRing[streamId].stopTrb, false);
	} else
		ClearTRB(&pRing->stopTrb, false);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AddDummyCommand(ringStruct* pRing, IOUSBCommand* command)
{
	IOReturn rc;

	if ((pRing->epType | CTRL_EP) == ISOC_IN_EP)
		return kIOUSBEndpointNotFound;
	XHCIAsyncEndpoint* pAsyncEp = pRing->asyncEndpoint;
	if (!pAsyncEp)
		return kIOUSBEndpointNotFound;
	if (pAsyncEp->aborting)
		return kIOReturnNotPermitted;
	rc = pAsyncEp->CreateTDs(command, 0U, XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NOOP) | XHCI_TRB_3_IOC_BIT, 0U, 0);
	pAsyncEp->ScheduleTDs();
	return rc;
}

__attribute__((visibility("hidden")))
bool CLASS::CanTDFragmentFit(ringStruct const* pRing, uint32_t numBytes)
{
	uint16_t numFit, spaceInStart;
	uint32_t maxNumPages = (numBytes / PAGE_SIZE) + 2U;
	if ((numBytes & PAGE_MASK) > 1U)
		++maxNumPages;
	/*
	 * Note: maxNumPages accounts for possible EVENT_DATA
	 */
	if (pRing->enqueueIndex < pRing->dequeueIndex)
		numFit = pRing->dequeueIndex - 1U - pRing->enqueueIndex;
	else {
		numFit = pRing->numTRBs - pRing->enqueueIndex;
		spaceInStart = pRing->dequeueIndex;
		if (spaceInStart) {
			if (numFit)
				--numFit;
			if (numFit < (--spaceInStart))
				numFit = spaceInStart;
		} else
			numFit = (numFit > 2U) ? (numFit - 2U) : 0U;
	}
	return maxNumPages <= numFit;
}

__attribute__((visibility("hidden")))
uint32_t CLASS::FreeSlotsOnRing(ringStruct const* pRing)
{
	if (pRing->enqueueIndex < pRing->dequeueIndex)
		return pRing->dequeueIndex - 1U - pRing->enqueueIndex;
	uint32_t v = pRing->dequeueIndex + pRing->numTRBs - pRing->enqueueIndex;
#if 0
	if (GetSlCtxSpeed(GetSlotContext(pRing->slot)) >= kUSBDeviceSpeedSuper) {
		uint32_t maxPacketSize, maxBurst, mult, align;
		ContextStruct* epContext = GetSlotContext(pRing->slot, pRing->endpoint);
		maxPacketSize = XHCI_EPCTX_1_MAXP_SIZE_GET(epContext->_e.dwEpCtx1);
		maxBurst = XHCI_EPCTX_1_MAXB_GET(epContext->_e.dwEpCtx1) + 1U;
		mult = XHCI_EPCTX_0_MULT_GET(epContext->_e.dwEpCtx0) + 1U;
		align = 1U + (maxPacketSize * maxBurst * mult) / 4096U;
		if (v > align)
			return v - align;
		return 0U;
	}
#endif
	if (v > 3U)
		return v - 3U;
	return 0U;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::_createTransfer(void* pTd, bool isIsochTransfer, uint32_t bytesToTransfer, uint32_t mystery,
								size_t startingOffsetInBuffer, bool interruptNeeded, bool multiTDTransaction,
								uint32_t* pFirstTrbIndex, uint32_t* pTrbCount, int16_t* pLastTrbIndex)
{
	IODMACommand* command;
	ringStruct* pRing;
	ContextStruct* pContext;
	TRBStruct *pFirstTrbInFragment, *pTrb;
	size_t offsetInBuffer, residueEstimate, bytesFollowingThisTD, bytesPreceedingThisTD;
	uint32_t bytesLeftInTD, bytesCurrentTrb, maxPacketSize, maxBurstSize, multiple, MBPMultiple, fourth, finalFourth;
	int32_t lastTrbIndex, TrbCountInTD, TrbCountInFragment;
	IOReturn rc;
	bool isNoopOrStatus, isFirstFragment, haveImmediateData, finalTDInTransaction;
	uint8_t slot, endpoint, copyOfImmediateData[8];

	offsetInBuffer = startingOffsetInBuffer;
	bytesLeftInTD = bytesToTransfer;
	pFirstTrbInFragment = 0;
	if (isIsochTransfer) {
		GenericUSBXHCIIsochTD* pIsochTd = static_cast<GenericUSBXHCIIsochTD*>(pTd);
		pRing = static_cast<GenericUSBXHCIIsochEP*>(pIsochTd->_pEndpoint)->pRing;
		command = pIsochTd->command->GetDMACommand();
		isNoopOrStatus = false;
		bytesFollowingThisTD = 0U;
		bytesPreceedingThisTD = 0U;
		haveImmediateData = false;
		finalTDInTransaction = false;
	} else {
		XHCIAsyncTD* pATd = static_cast<XHCIAsyncTD*>(pTd);
		bytesFollowingThisTD = pATd->bytesFollowingThisTD;
		finalTDInTransaction = pATd->finalTDInTransaction;
		bytesPreceedingThisTD = pATd->bytesPreceedingThisTD;
		command = pATd->command->GetDMACommand();
		pRing = pATd->provider->pRing;
		switch (XHCI_TRB_3_TYPE_GET(mystery)) {
			case XHCI_TRB_TYPE_STATUS_STAGE:
			case XHCI_TRB_TYPE_NOOP:
				isNoopOrStatus = true;
				break;
			default:
				isNoopOrStatus = false;
				break;
		}
		haveImmediateData = pATd->haveImmediateData;
		if (haveImmediateData)
			bcopy(&pATd->immediateData[0], &copyOfImmediateData[0], sizeof copyOfImmediateData);
	}
	slot = pRing->slot;
	endpoint = pRing->endpoint;
	pContext = GetSlotContext(slot, endpoint);
	maxPacketSize = XHCI_EPCTX_1_MAXP_SIZE_GET(pContext->_e.dwEpCtx1);
	if (!maxPacketSize)
		return kIOReturnBadArgument;
	maxBurstSize = XHCI_EPCTX_1_MAXB_GET(pContext->_e.dwEpCtx1);
	multiple = XHCI_EPCTX_0_MULT_GET(pContext->_e.dwEpCtx0);
	MBPMultiple = maxPacketSize * (1U + maxBurstSize) * (1U + multiple);
	residueEstimate = maxPacketSize + bytesFollowingThisTD - 1U;

	isFirstFragment = true;
	TrbCountInTD = 0;
	fourth = 0U;
	TrbCountInFragment = 0;
	/*
	 * TBD:
	 *   If we call PutBackTRB and return an error, pTd is returned
	 *     to queuedHead to be rescheduled later, but we may have
	 *     already trigerred some TD fragments from it.  So pTd
	 *     needs to be updated to reflect fragments completed.
	 */
	do {
		pTrb = GetNextTRB(pRing, 0, &pFirstTrbInFragment, isFirstFragment && !bytesPreceedingThisTD);
		if (!pTrb) {
			PutBackTRB(pRing, pFirstTrbInFragment);
			return kIOReturnInternalError;
		}
		finalFourth = fourth;
		bytesCurrentTrb = bytesLeftInTD;
		++TrbCountInFragment;
		fourth = pTrb->d & XHCI_TRB_3_CYCLE_BIT;
		fourth ^= XHCI_TRB_3_CYCLE_BIT;
		if (haveImmediateData) {
			if (bytesLeftInTD)
				bcopy(&copyOfImmediateData[0], pTrb, bytesLeftInTD);
		} else {
			rc = GenerateNextPhysicalSegment(pTrb,
											 &bytesCurrentTrb,
											 offsetInBuffer,
											 command);
			if (rc != kIOReturnSuccess) {
				/*
				 * TBD: The transaction should be aborted if this
				 *   happens, since it means genIOVMSegments has
				 *   failed and there's a defect in the memory descriptor.
				 *   If the TD is requeued, it causes an infinite loop.
				 */
				PutBackTRB(pRing, pFirstTrbInFragment);
				return rc;
			}
		}
		if (!bytesLeftInTD) {
			pTrb->a = 0U;
			pTrb->b = 0U;
		}
		bytesLeftInTD -= bytesCurrentTrb;
		if (!bytesLeftInTD &&
			!haveImmediateData &&
			finalTDInTransaction &&
			TrbCountInTD > 0)
			fourth |= XHCI_TRB_3_ENT_BIT;
#if kMaxActiveInterrupters > 1
		pTrb->c = XHCI_TRB_2_IRQ_SET(1U);
#else
		pTrb->c = 0U;
#endif
		if (!isNoopOrStatus) {
			uint32_t TDSize = static_cast<uint32_t>((residueEstimate + bytesLeftInTD) / maxPacketSize);
			if (TDSize > 31U)
				TDSize = 31U;
			pTrb->c |= XHCI_TRB_2_TDSZ_SET(TDSize) | XHCI_TRB_2_BYTES_SET(bytesCurrentTrb);
		}
		fourth |= mystery ? : XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NORMAL);
		if (!haveImmediateData)
			fourth |= XHCI_TRB_3_CHAIN_BIT;
		lastTrbIndex = static_cast<int32_t>(pTrb - pRing->ptr);
		if (pFirstTrbInFragment != pTrb) {
			pTrb->d = fourth;
			fourth = finalFourth;
		}
		offsetInBuffer += bytesCurrentTrb;
		if (offsetInBuffer &&
			!isIsochTransfer &&
			bytesLeftInTD &&
			!(offsetInBuffer % MBPMultiple)) {
			CloseFragment(pRing, pFirstTrbInFragment, fourth);
			pFirstTrbInFragment = 0;
			isFirstFragment = false;
			TrbCountInFragment = 0;
		}
		mystery = 0U;
		++TrbCountInTD;
	} while(bytesLeftInTD);

	if (TrbCountInTD == 1) {
		if (GetRing(slot, endpoint, 0U)) {
			if (interruptNeeded) {
				fourth |= XHCI_TRB_3_IOC_BIT;
				switch (XHCI_TRB_3_TYPE_GET(fourth)) {
					case XHCI_TRB_TYPE_NORMAL:
					case XHCI_TRB_TYPE_DATA_STAGE:
					case XHCI_TRB_TYPE_ISOCH:
						fourth |= XHCI_TRB_3_ISP_BIT;
						break;
				}
			} else if (endpoint & 1U)	/* in/ctrl endpoint */
				switch (XHCI_TRB_3_TYPE_GET(fourth)) {
					case XHCI_TRB_TYPE_NORMAL:
					case XHCI_TRB_TYPE_ISOCH:
						fourth |= XHCI_TRB_3_BEI_BIT | XHCI_TRB_3_ISP_BIT;
						break;
					case XHCI_TRB_TYPE_DATA_STAGE:
						fourth |= XHCI_TRB_3_ISP_BIT;
						break;
				}
			fourth &= ~(XHCI_TRB_3_CHAIN_BIT | XHCI_TRB_3_ENT_BIT);
			if (lastTrbIndex == pRing->numTRBs - 2U)
				pRing->ptr[pRing->numTRBs - 1U].d &= ~XHCI_TRB_3_CHAIN_BIT;
		}
	} else {
		finalFourth = fourth;
		pTrb = GetNextTRB(pRing,
						  (finalTDInTransaction || !multiTDTransaction) ? pTd : 0,
						  &pFirstTrbInFragment,
						  isFirstFragment && !bytesPreceedingThisTD);
		if (!pTrb) {
			PutBackTRB(pRing, pFirstTrbInFragment);
			return kIOReturnInternalError;
		}
		lastTrbIndex = static_cast<int32_t>(pTrb - pRing->ptr);
		SetTRBAddr64(pTrb, pRing->physAddr + lastTrbIndex * sizeof *pRing->ptr);
#if kMaxActiveInterrupters > 1
		pTrb->c = XHCI_TRB_2_IRQ_SET(1U);
#else
		pTrb->c = 0U;
#endif
		fourth = pTrb->d & XHCI_TRB_3_CYCLE_BIT;
		fourth ^= (XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_EVENT_DATA) | XHCI_TRB_3_CYCLE_BIT);
		if (multiTDTransaction && !finalTDInTransaction)
			fourth |= XHCI_TRB_3_CHAIN_BIT;
		++TrbCountInFragment;
		++TrbCountInTD;
		if (interruptNeeded)
			fourth |= XHCI_TRB_3_IOC_BIT;
		else if (endpoint & 1U) /* in/ctrl endpoint */
			fourth |= XHCI_TRB_3_BEI_BIT | XHCI_TRB_3_IOC_BIT;
		pTrb->d = fourth;
		fourth = finalFourth;
	}
	if (TrbCountInFragment)
		CloseFragment(pRing, pFirstTrbInFragment, fourth);
	if (pFirstTrbIndex)
		*pFirstTrbIndex = pFirstTrbInFragment ? static_cast<uint32_t>(pFirstTrbInFragment - pRing->ptr) : UINT32_MAX;
	if (pLastTrbIndex)
		*pLastTrbIndex = static_cast<int16_t>(lastTrbIndex);
	if (pTrbCount)
		*pTrbCount = static_cast<uint32_t>(TrbCountInTD);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
TRBStruct* CLASS::GetNextTRB(ringStruct* pRing, void* isLastTrbInTransaction, TRBStruct** ppFirstTrbInFragment, bool isFirstFragmentInTransaction)
{
	TRBStruct *pTrb1, *pTrb2;
	int32_t indexOfLinkTrb, indexOfFirstTrbInFragment;
	uint32_t fourth;
	uint16_t newEnqueueIndex, reposition;

	/*
	 * Probably the most clever code in the driver...
	 */
	if (!ppFirstTrbInFragment)
		return 0;
	indexOfLinkTrb = static_cast<int32_t>(pRing->numTRBs) - 1;
	if (pRing->enqueueIndex >= indexOfLinkTrb)	// sanity check
		return 0;
	if (pRing->enqueueIndex == indexOfLinkTrb - 1) {
		if (!(pRing->dequeueIndex))	// queue full?
			return 0;
		pTrb1 = &pRing->ptr[indexOfLinkTrb];	// * to link TRB
		fourth = pTrb1->d;
		if (isLastTrbInTransaction)
			fourth &= ~XHCI_TRB_3_CHAIN_BIT;
		else
			fourth |= XHCI_TRB_3_CHAIN_BIT;
		if (pRing->cycleState)
			fourth |= XHCI_TRB_3_CYCLE_BIT;
		else
			fourth &= ~XHCI_TRB_3_CYCLE_BIT;
		pTrb1->d = fourth;
		pRing->cycleState ^= 1U;
		newEnqueueIndex = 0U;
	} else {
		if (!pRing->enqueueIndex && (pTrb1 = *ppFirstTrbInFragment) != 0) {
			/*
			 * TD fragment has been split across link TRB,
			 *   so it must be relocated to beginning of ring
			 *   and relinked (ring shortened for single cycle).
			 */
			indexOfFirstTrbInFragment = static_cast<int32_t>(pTrb1 - pRing->ptr);
			/*
			 * (indexOfLinkTrb - indexOfFirstTrbInFragment) is the number of TRBs
			 * already queued.  Check if there's space to copy TRBs already
			 *  queued + 1 more (+ 1 extra so ring doesn't fill).
			 */
			if (pRing->dequeueIndex < indexOfLinkTrb - indexOfFirstTrbInFragment + 2)
				return 0;
			for (reposition = 0U, pTrb2 = pRing->ptr;
				 indexOfFirstTrbInFragment < indexOfLinkTrb;
				 ++indexOfFirstTrbInFragment, ++reposition, ++pTrb1, ++pTrb2) {
				*pTrb2 = *pTrb1;	// copy from indexOfFirstTrbInFragment to reposition
				pTrb2->d ^= XHCI_TRB_3_CYCLE_BIT;
			}
			/*
			 * indexOfFirstTrbInFragment, pTrb1 point to link TRB
			 * reposition is location for new TRB to queue
			 */
			pTrb2 = *ppFirstTrbInFragment;	// reload location of original first TRB and link from there
			pTrb2->a = pTrb1->a;
			pTrb2->b = pTrb1->b;
			pTrb2->c = pTrb1->c;
			fourth = pTrb1->d;
			if (isFirstFragmentInTransaction)
				fourth &= ~XHCI_TRB_3_CHAIN_BIT;
			else
				fourth |= XHCI_TRB_3_CHAIN_BIT;
			/*
			 * Note: This could lead xHC to immediately parse link TRB
			 */
			IOSync();
			pTrb2->d = fourth;
			IOSync();
			newEnqueueIndex = reposition + 1U;
			*ppFirstTrbInFragment = pRing->ptr;
			pRing->enqueueIndex = reposition;
		} else {
			newEnqueueIndex = pRing->enqueueIndex + 1U;
			if (newEnqueueIndex == pRing->dequeueIndex) // queue full?
				return 0;
		}
	}
	pTrb1 = &pRing->ptr[pRing->enqueueIndex];
	pRing->enqueueIndex = newEnqueueIndex;
	ClearTRB(pTrb1, false);
	if (!(*ppFirstTrbInFragment))
		*ppFirstTrbInFragment = pTrb1;
	return pTrb1;
}

__attribute__((visibility("hidden")))
void CLASS::CloseFragment(ringStruct* pRing, TRBStruct* pFirstTrbInFragment, uint32_t newFourth)
{
	if (!pFirstTrbInFragment)
		return;
	uint32_t fourth = pFirstTrbInFragment->d;
	newFourth &= ~XHCI_TRB_3_CYCLE_BIT;
	fourth &= XHCI_TRB_3_CYCLE_BIT;
	fourth |= newFourth;
	fourth ^= XHCI_TRB_3_CYCLE_BIT;
	IOSync();
	pFirstTrbInFragment->d = fourth;
	IOSync();
}

__attribute__((visibility("hidden")))
IOReturn CLASS::GenerateNextPhysicalSegment(TRBStruct* pTrb, uint32_t* pLength, size_t offset, IODMACommand* command)
{
	IODMACommand::Segment64 segment;
	UInt64 _offset;
	UInt32 numSegments, returnLength;
	IOReturn rc;

	if (!(*pLength))
		return kIOReturnSuccess;
	numSegments = 1U;
	_offset = offset;
	rc = command->genIOVMSegments(&_offset, &segment, &numSegments);
	if (rc != kIOReturnSuccess)
		return rc;
	if (numSegments != 1U)
		return kIOReturnInternalError;
	SetTRBAddr64(pTrb, segment.fIOVMAddr);
	returnLength = (1U << 16) - static_cast<uint16_t>(segment.fIOVMAddr);
	if (segment.fLength < returnLength)
		returnLength = static_cast<uint32_t>(segment.fLength);
	if (*pLength > returnLength)
		*pLength = returnLength;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::PutBackTRB(ringStruct* pRing, TRBStruct* pTrb)
{
	if (!pRing || !pTrb)
		return;
	bool isFirst = true;
	int32_t index = static_cast<int32_t>(pTrb - pRing->ptr);
	if (index < 0 || index >= pRing->numTRBs)
		return;
	while (pRing->enqueueIndex != index) {
		if (isFirst)
			isFirst = false;
		else {
			if (pRing->cycleState)
				pRing->ptr[pRing->enqueueIndex].d &= ~XHCI_TRB_3_CYCLE_BIT;
			else
				pRing->ptr[pRing->enqueueIndex].d |= XHCI_TRB_3_CYCLE_BIT;
		}
		if (pRing->enqueueIndex == pRing->dequeueIndex)
			break;
		if (pRing->enqueueIndex) {
			--pRing->enqueueIndex;
			continue;
		}
		pRing->enqueueIndex = pRing->numTRBs - 1U;
		pRing->cycleState ^= 1U;
	}
}
