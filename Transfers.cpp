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
	uint8_t endpoint = TranslateEndpoint(command->GetEndpoint(), command->GetDirection());
	if (endpoint < 2U || endpoint >= kUSBMaxPipes)
		return kIOReturnBadArgument;
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
		return kIOReturnNoMemory;
	if (IsIsocEP(slot, endpoint)) {
		GenericUSBXHCIIsochEP* pIsochEp = pRing->isochEndpoint;
		if (pIsochEp) {
			for (int32_t count = 0; count < 120 && pIsochEp->tdsScheduled; ++count)
				IOSleep(1U);
			if (pIsochEp->tdsScheduled)
				pIsochEp->tdsScheduled = false;
			AbortIsochEP(pIsochEp);
		}
		return kIOReturnSuccess;
	}
	if (pRing->dequeueIndex != pRing->enqueueIndex) {
		XHCIAsyncEndpoint* pAsyncEp = pRing->asyncEndpoint;
		if (pAsyncEp)
			pAsyncEp->Abort();
	}
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
	retFromCMD = SetTRDQPtr(slot, endpoint, streamId, pRing->dequeueIndex);
	if (pRing->schedulingPending) {
		if (!IsIsocEP(slot, endpoint)) {
			XHCIAsyncEndpoint* pAsyncEp = pRing->asyncEndpoint;
			if (pAsyncEp)
				pAsyncEp->ScheduleTDs();
		}
		if (IsStreamsEndpoint(slot, endpoint))
			RestartStreams(slot, endpoint, 0U);
		else
			StartEndpoint(slot, endpoint, 0U);
		pRing->schedulingPending = false;
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
	uint32_t epState = XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0);
	if (epState != EP_STATE_STOPPED && epState != EP_STATE_ERROR) {
		if (!IsStreamsEndpoint(slot, endpoint) ||
			pRing->dequeueIndex != pRing->enqueueIndex)
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

#pragma mark -
#pragma mark XHCIAsyncEndpoint
#pragma mark -

__attribute__((visibility("hidden")))
XHCIAsyncEndpoint* XHCIAsyncEndpoint::withParameters(CLASS* provider, ringStruct* pRing, uint32_t maxPacketSize, uint32_t maxBurst, uint32_t multiple)
{
	XHCIAsyncEndpoint* obj;
	obj = static_cast<XHCIAsyncEndpoint*>(IOMalloc(sizeof *obj));
	if (!obj)
		return 0;
	bzero(obj, sizeof *obj);
	obj->provider = provider;
	obj->pRing = pRing;
	obj->setParameters(maxPacketSize, maxBurst, multiple);
	return obj;
}

#define kAsyncMaxFragmentSize (1U << 17)

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::setParameters(uint32_t maxPacketSize, uint32_t maxBurst, uint32_t multiple)
{
	uint32_t MBPMultiple;

	this->maxPacketSize = maxPacketSize;
	this->maxBurst = maxBurst;
	this->multiple = multiple;
	/*
	 * Note: MBP = maxPacketSize * (1U + maxBurst)
	 *  (Max Burst Payload)
	 */
	MBPMultiple = maxPacketSize * (1U + maxBurst) * (1U + multiple);
	maxTDBytes = kAsyncMaxFragmentSize;
	if (MBPMultiple && MBPMultiple < kAsyncMaxFragmentSize)
		maxTDBytes -= (kAsyncMaxFragmentSize % MBPMultiple);
}

__attribute__((visibility("hidden")))
bool XHCIAsyncEndpoint::checkOwnership(CLASS* provider, ringStruct* pRing)
{
	return this->provider == provider && this->pRing == pRing;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::release(void)
{
	aborting = true;
	pRing->returnInProgress = true;
	MoveAllTDsFromReadyQToDoneQ();
	if (numTDsDone)
		Complete(kIOReturnAborted);
	wipeAsyncList(freeHead, freeTail);
	freeHead = 0;
	freeTail = 0;
	numTDsFree = 0U;
	aborting = false;
	pRing->returnInProgress = false;
	IOFree(this, sizeof *this);
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::wipeAsyncList(XHCIAsyncTD* pHead, XHCIAsyncTD* pTail)
{
	XHCIAsyncTD* pNextTd;

	while (pHead) {
		pNextTd = (pHead != pTail) ? pHead->next : 0;
		pHead->release();
		pHead = pNextTd;
	}
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::nuke(void)
{
	wipeAsyncList(queuedHead, queuedTail);
	wipeAsyncList(scheduledHead, scheduledTail);
	wipeAsyncList(doneHead, doneTail);
	wipeAsyncList(freeHead, freeTail);
	IOFree(this, sizeof *this);
}

__attribute__((visibility("hidden")))
IOReturn XHCIAsyncEndpoint::CreateTDs(IOUSBCommand* command, uint16_t streamId, uint32_t mystery, uint8_t immediateDataSize, uint8_t const* pImmediateData)
{
	XHCIAsyncTD* pTd;
	size_t transferRequestBytes, numBytesLeft, bytesDone;
	uint32_t maxBytesPerTD, currentTDBytes;
	uint16_t tdIndex;
	bool haveImmediateData;
	bool usingMultipleTDs;

	if (aborting)
		return kIOReturnNotPermitted;
	transferRequestBytes = command->GetReqCount();
	if (transferRequestBytes) {
		IODMACommand* dmac = command->GetDMACommand();
		if (!dmac || !dmac->getMemoryDescriptor()) {
			IOLog("%s: no DMA Command or missing memory descriptor\n", __FUNCTION__);
			return kIOReturnBadArgument;
		}
	}
	command->SetUIMScratch(9U, 0U);
	if (immediateDataSize <= sizeof pTd->immediateData) {
		transferRequestBytes = immediateDataSize;
		haveImmediateData = true;
		usingMultipleTDs = false;
		maxBytesPerTD = immediateDataSize;
	} else if (transferRequestBytes <= maxTDBytes) {
		haveImmediateData = false;
		usingMultipleTDs = false;
		maxBytesPerTD = static_cast<uint32_t>(transferRequestBytes);
	} else {
		haveImmediateData = false;
		usingMultipleTDs = true;
		maxBytesPerTD = maxTDBytes;
	}

	numBytesLeft = transferRequestBytes;
	bytesDone = 0U;
	tdIndex = 1U;
	currentTDBytes = maxBytesPerTD;
	do {
		pTd = GetTDFromFreeQueue(true);
		if (!pTd)
			return kIOReturnNoMemory;
		pTd->command = command;
		pTd->interruptThisTD = !(bytesDone % maxTDBytes);
		pTd->multiTDTransaction = usingMultipleTDs;
		pTd->bytesPreceedingThisTD = bytesDone;
		pTd->bytesThisTD = currentTDBytes;
		pTd->mystery = mystery;
		pTd->maxNumPagesInTD = static_cast<uint16_t>((currentTDBytes / PAGE_SIZE) + 2U);
		pTd->streamId = streamId;
		pTd->shortfall = currentTDBytes;
		if (haveImmediateData) {
			pTd->haveImmediateData = haveImmediateData;
			if (immediateDataSize)
				bcopy(pImmediateData, &pTd->immediateData[0], immediateDataSize);
		}
		PutTD(&queuedHead, &queuedTail, pTd, &numTDsQueued);
		bytesDone += currentTDBytes;
		if (bytesDone >= transferRequestBytes)
			break;
		++tdIndex;
		numBytesLeft -= currentTDBytes;
		pTd->bytesFollowingThisTD = numBytesLeft;
		currentTDBytes = (numBytesLeft < maxBytesPerTD) ? static_cast<uint32_t>(numBytesLeft) : maxBytesPerTD;
	} while (true);
	pTd->numTDsThisTransaction = tdIndex;
	pTd->interruptThisTD = true;
	pTd->finalTDInTransaction = true;
	pTd->bytesFollowingThisTD = 0U;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::ScheduleTDs(void)
{
	XHCIAsyncTD* pTd;
	IOReturn rc;

	if (!queuedHead)
		return;
	if (aborting) {
		if (pRing->returnInProgress)
			pRing->schedulingPending = true;
		return;
	}
	do {
		if (!(provider->CanTDFragmentFit(pRing, maxTDBytes))) {
			if (gux_log_level >= 2 && provider)
				++provider->_diagCounters[DIAGCTR_XFERKEEPAWAY];
			break;
		}
		pTd = GetTD(&queuedHead, &queuedTail, &numTDsQueued);
		if (!pTd)
			continue;
		rc = provider->_createTransfer(pTd,
									   false,
									   pTd->bytesThisTD,
									   pTd->mystery,
									   pTd->bytesPreceedingThisTD,
									   pTd->interruptThisTD,
									   pTd->multiTDTransaction,
									   &pTd->firstTrbIndex,
									   &pTd->TrbCount,
									   &pTd->lastTrbIndex);
		if (rc != kIOReturnSuccess) {
			if (provider)
				++provider->_diagCounters[DIAGCTR_XFERLAYOUT];
			PutTDAtHead(&queuedHead, &queuedTail, pTd, &numTDsQueued);
			break;
		}
		PutTD(&scheduledHead, &scheduledTail, pTd, &numTDsScheduled);
		if (pRing->returnInProgress)
			pRing->schedulingPending = true;
		else
			provider->StartEndpoint(pRing->slot, pRing->endpoint, pTd->streamId);
	} while(queuedHead);
}

__attribute__((visibility("hidden")))
IOReturn XHCIAsyncEndpoint::Abort(void)
{
	aborting = true;
	pRing->returnInProgress = true;
	while (scheduledHead) {
		IOUSBCommand* command = scheduledHead->command;
		FlushTDs(command, 1);
		MoveTDsFromReadyQToDoneQ(command);
	}
	pRing->returnInProgress = false;
	aborting = false;
#if 0
	if (numTDsDone)
		Complete(provider->GetNeedsReset(pRing->slot) ? kIOReturnNotResponding : kIOReturnAborted);
#else
	if (numTDsDone)
		Complete(kIOReturnAborted);
#endif
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
struct XHCIAsyncTD* XHCIAsyncEndpoint::GetTDFromActiveQueueWithIndex(uint16_t indexInQueue)
{
	XHCIAsyncTD *pPrevTd, *pTd;
	uint32_t orphanCount = 0U;
	pTd = pPrevTd = scheduledHead;
	while (pTd) {
		if (pTd->lastTrbIndex == indexInQueue)
			break;
		if (pTd == scheduledTail)
			return 0;
		++orphanCount;
		pPrevTd = pTd;
		pTd = pTd->next;
	}
	if (!pTd)
		return 0;
#if 0
	if (pTd == scheduledTail) {
		if (pTd == scheduledHead) {
			scheduledTail = 0;
			scheduledHead = 0;
		} else
			scheduledTail = pPrevTd;
	} else if (pTd == scheduledHead)
		scheduledHead = pTd->next;
	else
		pPrevTd->next = pTd->next;
	--numTDsScheduled;
#else
	if (orphanCount) {
		/*
		 * Flush all scheduled TDs prior to pTd
		 */
		if (doneHead)
			doneTail->next = scheduledHead;
		else
			doneHead = scheduledHead;
		doneTail = pPrevTd;
		numTDsDone += orphanCount;
		scheduledHead = pTd;
		numTDsScheduled -= orphanCount;
		Complete(kIOReturnSuccess);
		if (provider)
			provider->_diagCounters[DIAGCTR_ORPHANEDTDS] += orphanCount;
	}
	GetTD(&scheduledHead, &scheduledTail, &numTDsScheduled);
#endif
	return pTd;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::RetireTDs(XHCIAsyncTD* pTd, IOReturn passthruReturnCode, bool callCompletion, bool flush)
{
	uint8_t slot, endpoint;

	PutTDonDoneQueue(pTd);
	if (flush) {
		slot = pRing->slot;
		endpoint = pRing->endpoint;
		/*
		 * Notes:
		 * If the endpoint is still running
		 *   - if more TDs are queued for transaction-to-flush, must stop the EP, and set TRDQPtr
		 *     in order to break the chain.
		 *   - otherwise, if other transactions are scheduled beyond flushed one, it's not safe
		 *     to set TRDQPtr since the xHC may have advanced past the flushed transaction.  Let the
		 *     EP continue to run.  Any events for flushed TDs are safely discarded in processTransferEvent2.
		 *   - otherwise, EP will safely idle after finishing flushed transaction, so can let
		 *     it run.
		 */
		if ((!queuedHead || queuedHead->command != pTd->command) &&
			XHCI_EPCTX_0_EPSTATE_GET(provider->GetSlotContext(slot, endpoint)->_e.dwEpCtx0) == EP_STATE_RUNNING) {
			FlushTDs(pTd->command, 0);
		} else {
			provider->QuiesceEndpoint(slot, endpoint);
			FlushTDs(pTd->command, 2);
			MoveTDsFromReadyQToDoneQ(pTd->command);
			if (scheduledHead) {
				/*
				 * Note - original is inverted...
				 */
				if (provider->IsStreamsEndpoint(slot, endpoint))
					provider->RestartStreams(slot, endpoint, 0U);
				else
					provider->StartEndpoint(slot, endpoint, 0U);
			}
		}
	}
	if (callCompletion)
		Complete(passthruReturnCode);
	ScheduleTDs();
}

__attribute__((visibility("hidden")))
XHCIAsyncTD* XHCIAsyncEndpoint::GetTDFromFreeQueue(bool newOneOk)
{
	XHCIAsyncTD* pTd;

	if (!freeHead)
		return newOneOk ? XHCIAsyncTD::ForEndpoint(this) : 0;
	pTd = GetTD(&freeHead, &freeTail, &numTDsFree);
	if (pTd)
		pTd->reinit();
	return pTd;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::PutTDonDoneQueue(XHCIAsyncTD* pTd)
{
	if (doneHead && !doneTail) {
		XHCIAsyncTD* pLastTd = doneHead;
		uint32_t count = 0U;
		while (pLastTd->next) {
			if (count++ > numTDsDone) {
				pLastTd = 0;
				break;
			}
			pLastTd = pLastTd->next;
		}
		doneTail = pLastTd;
	}
	PutTD(&doneHead, &doneTail, pTd, &numTDsDone);
}

/*
 * updateDequeueOption
 *   0 - don't update TRDQPtr
 *   1 - update TRDQPtr if ring empty after flush
 *   2 - always update TRDQPtr
 */
__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::FlushTDs(IOUSBCommand* command, int updateDequeueOption)
{
	XHCIAsyncTD* pTd;
	uint32_t streamId;
	int32_t indexInQueue;
	bool updateDequeueIndex;

	pTd = scheduledHead;
	if (!command || !pTd)
		return;
	updateDequeueIndex = false;
	indexInQueue = 0;
	streamId = 0U;
	while (pTd && pTd->command == command) {
		GetTD(&scheduledHead, &scheduledTail, &numTDsScheduled);
		streamId = pTd->streamId;
		indexInQueue = pTd->lastTrbIndex + 1;
		if (indexInQueue >= static_cast<int32_t>(pRing->numTRBs) - 1)
			indexInQueue = 0;
		updateDequeueIndex = true;
		PutTDonDoneQueue(pTd);
		pTd = scheduledHead;
	}
	switch (updateDequeueOption) {
		case 0:
			return;
		case 1:
			if (scheduledHead)
				return;
			break;
	}
	if (updateDequeueIndex)
		provider->SetTRDQPtr(pRing->slot, pRing->endpoint, streamId, indexInQueue);
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::MoveTDsFromReadyQToDoneQ(IOUSBCommand* command)
{
	XHCIAsyncTD* pTd;
	while (queuedHead) {
		if (command && command != queuedHead->command)
			break;
		pTd = GetTD(&queuedHead, &queuedTail, &numTDsQueued);
		if (pTd)
			PutTDonDoneQueue(pTd);
	}
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::MoveAllTDsFromReadyQToDoneQ(void)
{
	XHCIAsyncTD* pTd;
	while (queuedHead) {
		pTd = GetTD(&queuedHead, &queuedTail, &numTDsQueued);
		if (pTd)
			PutTDonDoneQueue(pTd);
	}
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::Complete(IOReturn passthruReturnCode)
{
	XHCIAsyncTD* pTd;
	IOUSBCommand* command;
	IOUSBCompletion comp;

	while (doneHead) {
		pTd = GetTD(&doneHead, &doneTail, &numTDsDone);
		if (!pTd)
			continue;
		command = pTd->command;
		command->SetUIMScratch(9U, command->GetUIMScratch(9U) + pTd->shortfall);
		if (pTd->interruptThisTD &&
			pTd->finalTDInTransaction) {
			comp = command->GetUSLCompletion();
			if (comp.action) {
				provider->Complete(comp,
								   passthruReturnCode,
								   command->GetUIMScratch(9U));
				pTd->shortfall = 0U;
			}
			pTd->interruptThisTD = false;
		}
		PutTD(&freeHead, &freeTail, pTd, &numTDsFree);
	}
}

__attribute__((visibility("hidden")))
bool XHCIAsyncEndpoint::NeedTimeouts(void)
{
	XHCIAsyncTD* pTd;
	IOUSBCommand* command;
	uint32_t ndto, cto;

	pTd = scheduledHead;
	if (!pTd)
		return true;
	command = pTd->command;
	if (!command)
		return true;
	ndto = command->GetNoDataTimeout();
	cto = command->GetCompletionTimeout();
	if (pTd->streamId || (ndto >= cto && cto)) {
		ndto = 0U;
		command->SetNoDataTimeout(0U);
	}
	return ndto || cto;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::UpdateTimeouts(bool abortAll, uint32_t frameNumber, bool stopped)
{
	uint64_t addr;
	int64_t trbIndex64;
	XHCIAsyncTD* pTd;
	IOUSBCommand* command;
	IOReturn passthruReturnCode;
	uint32_t shortfall, ndto, cto, bytesTransferred, firstSeen, TRTime;
	int32_t next;
	bool returnATransfer, ED;

	addr = GenericUSBXHCI::GetTRBAddr64(&pRing->stopTrb);
	ED = (pRing->stopTrb.d & XHCI_TRB_3_ED_BIT) != 0U;
	if (!ED && XHCI_TRB_2_ERROR_GET(pRing->stopTrb.c) == XHCI_TRB_ERROR_LENGTH)
		shortfall = 0U;
	else
		shortfall = XHCI_TRB_2_REM_GET(pRing->stopTrb.c);
	trbIndex64 = GenericUSBXHCI::DiffTRBIndex(addr, pRing->physAddr);
	if (trbIndex64 < 0 || trbIndex64 >= pRing->numTRBs - 1U)
		trbIndex64 = pRing->dequeueIndex;
	pTd = scheduledHead;
	if (!pTd)
		return;
	if (ED)
		shortfall = pTd->bytesThisTD - shortfall;
	if (!abortAll) {
		command = pTd->command;
		if (!command)
			return;
		ndto = command->GetNoDataTimeout();
		cto = command->GetCompletionTimeout();
		firstSeen = command->GetUIMScratch(5U);
		if (!firstSeen) {
			command->SetUIMScratch(5U, frameNumber);
			firstSeen = frameNumber;
		}
		returnATransfer = false;
		if (cto && frameNumber - firstSeen > cto) {
			pTd->shortfall = pTd->bytesThisTD - shortfall;
			returnATransfer = true;
		}
		if (ndto) {
			bytesTransferred = command->GetUIMScratch(4U);
			TRTime = command->GetUIMScratch(6U);
			if (!TRTime ||
				command->GetUIMScratch(3U) != static_cast<uint32_t>(trbIndex64) ||
				bytesTransferred != shortfall) {
				command->SetUIMScratch(3U, static_cast<uint32_t>(trbIndex64));
				command->SetUIMScratch(4U, shortfall);
				command->SetUIMScratch(6U, frameNumber);
			} else if (frameNumber - TRTime > ndto) {
				pTd->shortfall = pTd->bytesThisTD - bytesTransferred;
				returnATransfer = true;
			}
		}
		if (!returnATransfer)
			return;
		passthruReturnCode = kIOUSBTransactionTimeout;
	} else {
		pTd->shortfall = pTd->bytesThisTD - shortfall;
		passthruReturnCode = kIOReturnNotResponding;
	}
	GetTD(&scheduledHead, &scheduledTail, &numTDsScheduled);
	next = pTd->lastTrbIndex + 1;
	if (next >= static_cast<int32_t>(pRing->numTRBs) - 1)
		next = 0;
	if (provider->GetNeedsReset(pRing->slot))
		passthruReturnCode = kIOReturnNotResponding;
	if (!stopped)
		provider->QuiesceEndpoint(pRing->slot, pRing->endpoint);
	provider->SetTRDQPtr(pRing->slot, pRing->endpoint, pTd->streamId, next);
	RetireTDs(pTd, passthruReturnCode, true, true);
}

__attribute__((visibility("hidden")))
XHCIAsyncTD* XHCIAsyncEndpoint::GetTD(XHCIAsyncTD** pHead, XHCIAsyncTD** pTail, uint32_t* pNumTDs)
{
	XHCIAsyncTD* res = *pHead;
	if (!res)
		return 0;
	if (res == *pTail) {
		*pTail = 0;
		*pHead = 0;
	} else
		*pHead = res->next;
	--*pNumTDs;
	return res;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::PutTD(XHCIAsyncTD** pHead, XHCIAsyncTD** pTail, XHCIAsyncTD* pTd, uint32_t* pNumTDs)
{
	if (*pHead)
		(*pTail)->next = pTd;
	else
		*pHead = pTd;
	*pTail = pTd;
	++*pNumTDs;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::PutTDAtHead(XHCIAsyncTD** pHead, XHCIAsyncTD** pTail, XHCIAsyncTD* pTd, uint32_t* pNumTDs)
{
	if (*pHead)
		pTd->next = *pHead;
	else
		*pTail = pTd;
	*pHead = pTd;
	++*pNumTDs;
}

#pragma mark -
#pragma mark XHCIAsyncTD
#pragma mark -

__attribute__((visibility("hidden")))
XHCIAsyncTD* XHCIAsyncTD::ForEndpoint(XHCIAsyncEndpoint* provider)
{
	XHCIAsyncTD* pTd;

	pTd = static_cast<XHCIAsyncTD*>(IOMalloc(sizeof *pTd));
	if (!pTd)
		return 0;
	bzero(pTd, sizeof *pTd);
	pTd->provider = provider;
	return pTd;
}

__attribute__((visibility("hidden")))
void XHCIAsyncTD::reinit(void)
{
	XHCIAsyncEndpoint* p = provider;
	bzero(this, sizeof *this);
	provider = p;
}

__attribute__((visibility("hidden")))
void XHCIAsyncTD::release(void)
{
	IOFree(this, sizeof *this);
}
