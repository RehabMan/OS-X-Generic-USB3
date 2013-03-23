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
	if (GetIntelFlag(slot))
		return AddDummyCommand(pRing, command);
	if (pRing->deleteInProgress)
		return kIOReturnNoDevice;
	XHCIAsyncEndpoint* pEp = pRing->asyncEndpoint;
	if (!pEp)
		return kIOUSBEndpointNotFound;
	if (pEp->unusable)
		return kIOReturnNotPermitted;
	ContextStruct* pContext = GetSlotContext(slot, endpoint);
	switch (XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0)) {
		case EP_STATE_HALTED:
		case EP_STATE_ERROR:
			return kIOUSBPipeStalled;
	}
	IOReturn rc = pEp->CreateTDs(command, static_cast<uint16_t>(streamId), 0U, 0xFFU, 0);
	pEp->ScheduleTDs();
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
		if (pRing->isochEndpoint) {
			for (int32_t count = 0; pRing->isochEndpoint->_tdsScheduled && count < 120; ++count)
				IOSleep(1U);
			pRing->isochEndpoint->_tdsScheduled = false;
			AbortIsochEP(pRing->isochEndpoint);
		}
		return kIOReturnSuccess;
	}
	if (pRing->dequeueIndex != pRing->enqueueIndex) {
		XHCIAsyncEndpoint* pEp = pRing->asyncEndpoint;
		if (pEp)
			pEp->Abort();
	}
	return ReinitTransferRing(slot, endpoint, streamId);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::ReinitTransferRing(int32_t slot, int32_t endpoint, uint32_t streamId)
{
#if 0
	ContextStruct* pContext;
	TRBStruct localTrb = { 0 };
#endif
	int32_t retFromCMD;
	ringStruct* pRing = GetRing(slot, endpoint, streamId);
	if (!pRing)
		return kIOReturnBadArgument;
#if 0
	pContext = GetSlotContext(slot, endpoint);
	PrintContext(pContext);
#endif
	if (pRing->isInactive() ||
		!pRing->ptr ||
		pRing->numTRBs < 2U)
		return kIOReturnNoMemory;
#if 0
	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	localTrb.d |= XHCI_TRB_3_EP_SET(endpoint);
#endif
	if (pRing->dequeueIndex == pRing->enqueueIndex) {
		bzero(pRing->ptr, (pRing->numTRBs - 1U) * sizeof *pRing->ptr);
		pRing->ptr[pRing->numTRBs - 1U].d |= XHCI_TRB_3_TC_BIT;
		pRing->cycleState = 1U;
		pRing->enqueueIndex = 0U;
		pRing->dequeueIndex = 0U;
	}
	retFromCMD = SetTRDQPtr(slot, endpoint, streamId, pRing->dequeueIndex);
	if (pRing->schedulingPending) {
		if (!IsIsocEP(slot, endpoint)) {
			XHCIAsyncEndpoint* pEp = pRing->asyncEndpoint;
			if (pEp)
				pEp->ScheduleTDs();
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
	if (XHCI_EPCTX_0_EPSTATE_GET(pContext->_e.dwEpCtx0) != EP_STATE_STOPPED) {
		if (!IsStreamsEndpoint(slot, endpoint) ||
			pRing->dequeueIndex != pRing->enqueueIndex)
			return -1000 - XHCI_TRB_ERROR_CONTEXT_STATE;
	}
	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	localTrb.d |= XHCI_TRB_3_EP_SET(endpoint);
	SetTRBAddr64(&localTrb, pRing->physAddr + index * sizeof(TRBStruct));
	if (pRing->cycleState)
		localTrb.a |= XHCI_TRB_3_CYCLE_BIT;
	if (streamId) {
		localTrb.a |= 2U;  // SCT = 1U - Primary Transfer Ring
		localTrb.c |= XHCI_TRB_2_STREAM_SET(streamId);
	}
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_SET_TR_DEQUEUE, 0);
	if (retFromCMD != -1 && retFromCMD > -1000) {
		pRing->dequeueIndex = index;
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
	ringStruct* pRing;
	if (IsStreamsEndpoint(slot, endpoint)) {
		uint16_t lastStream = GetLastStreamForEndpoint(slot, endpoint);
		for (uint16_t streamId = 1U; streamId <= lastStream; ++streamId) {	// Note: originally <
			pRing = GetRing(slot, endpoint, streamId);
			if (pRing)
				ClearTRB(&pRing->stopTrb, false);
		}
	} else {
		pRing = GetRing(slot, endpoint, 0U);
		if (pRing)
			ClearTRB(&pRing->stopTrb, false);
	}
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AddDummyCommand(ringStruct* pRing, IOUSBCommand* command)
{
	IOReturn rc;

	XHCIAsyncEndpoint* pEp = pRing->asyncEndpoint;
	if (!pEp)
		return kIOUSBEndpointNotFound;
	if (pEp->unusable)
		return kIOReturnNotPermitted;
	rc = pEp->CreateTDs(command, 0U, XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_NOOP) | XHCI_TRB_3_IOC_BIT, 0U, 0);
	pEp->ScheduleTDs();
	return rc;
}

__attribute__((visibility("hidden")))
bool CLASS::CanTDFragmentFit(ringStruct const* pRing, uint32_t numBytes)
{
	uint16_t numFit;
	uint32_t maxNumPages = (numBytes / PAGE_SIZE) + 2U;
	if (pRing->enqueueIndex < pRing->dequeueIndex)
		numFit = pRing->dequeueIndex - 1U - pRing->enqueueIndex;
	else {
#if 0
		numFit = pRing->numTRBs - 1U - pRing->enqueueIndex + pRing->dequeueIndex;
#else
		uint16_t v;
		numFit = pRing->numTRBs - pRing->enqueueIndex;
		v = pRing->dequeueIndex;
		numFit = (numFit > 3U) ? numFit - 3U : 0U;
		v = (v > 2U) ? v - 2U : 0U;
		if (numFit < v) numFit = v;
#endif
	}
	return maxNumPages <= numFit;
}

__attribute__((visibility("hidden")))
uint32_t CLASS::FreeSlotsOnRing(ringStruct const* pRing)
{
	if (pRing->enqueueIndex < pRing->dequeueIndex)
		return pRing->dequeueIndex - 1U - pRing->enqueueIndex;
	uint32_t v = pRing->dequeueIndex + pRing->numTRBs - pRing->enqueueIndex;
	if (v > 3U)
		return v - 3U;
	return 0U;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::_createTransfer(void* pTd, bool isIsochTransfer, uint32_t bytesToTransfer, uint32_t mystery,
								size_t startingOffsetInBuffer, bool onMaxBoundary, bool multiTDTransaction,
								int32_t* pFirstTrbIndex, uint32_t* pTrbCount, bool, int16_t* pLastTrbIndex)
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
		command = pIsochTd->_command->GetDMACommand();
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
			if (onMaxBoundary) {
				fourth |= XHCI_TRB_3_IOC_BIT;
				switch (XHCI_TRB_3_TYPE_GET(fourth)) {
					case XHCI_TRB_TYPE_NORMAL:
					case XHCI_TRB_TYPE_DATA_STAGE:
					case XHCI_TRB_TYPE_ISOCH:
						fourth |= XHCI_TRB_3_ISP_BIT;
						break;
				}
			} else if (isFirstFragment)
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
		if (onMaxBoundary)
			fourth |= XHCI_TRB_3_IOC_BIT;
		else if (endpoint & 1U) /* in/ctrl endpoint */
			fourth |= XHCI_TRB_3_BEI_BIT | XHCI_TRB_3_IOC_BIT;
		pTrb->d = fourth;
		fourth = finalFourth;
	}
	if (TrbCountInFragment)
		CloseFragment(pRing, pFirstTrbInFragment, fourth);
	if (pFirstTrbIndex)
		*pFirstTrbIndex = pFirstTrbInFragment ? static_cast<int32_t>(pFirstTrbInFragment - pRing->ptr) : -1;
	if (pLastTrbIndex)
		*pLastTrbIndex = static_cast<int16_t>(lastTrbIndex);
	if (pTrbCount)
		*pTrbCount = static_cast<uint32_t>(TrbCountInTD);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
TRBStruct* CLASS::GetNextTRB(ringStruct* pRing, void* isLastTrbInTransaction, TRBStruct** ppFirstTrbInFragment, bool isFirstTrbInTransaction)
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
				bcopy(pTrb1, pTrb2, sizeof *pTrb2); // copy from indexOfFirstTrbInFragment to reposition
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
			if (isFirstTrbInTransaction)
				fourth &= ~XHCI_TRB_3_CHAIN_BIT;
			else
				fourth |= XHCI_TRB_3_CHAIN_BIT;
			pTrb2->d = fourth;
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
	uint32_t MBPMultiple;
	obj = static_cast<XHCIAsyncEndpoint*>(IOMalloc(sizeof *obj));
	if (!obj)
		return 0;
	bzero(obj, sizeof *obj);
	obj->provider = provider;
	obj->pRing = pRing;
	obj->maxPacketSize = maxPacketSize;
	obj->maxBurst = maxBurst;
	obj->multiple = multiple;
	/*
	 * Note: MBP = maxPacketSize * (1U + maxBurst)
	 *  (Max Burst Payload)
	 */
	MBPMultiple = maxPacketSize * (1U + maxBurst) * (1U + multiple);
	obj->maxTDBytes = (1U << 17) - ((1U << 17) % MBPMultiple);
	return obj;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::release(void)
{
	XHCIAsyncTD* pTd;

	unusable = true;
	pRing->endpointUnusable = true;
	MoveAllTDsFromReadyQToDoneQ();
	if (numTDsDone)
		Complete(kIOReturnAborted);
	while (freeHead) {
		pTd = GetTDFromFreeQueue(false);
		if (pTd)
			pTd->release();
	}
	unusable = false;
	pRing->endpointUnusable = false;
	IOFree(this, sizeof *this);
}

static
void wipeList(XHCIAsyncTD* pHead, XHCIAsyncTD* pTail)
{
	XHCIAsyncTD* pTd;

	while (pHead) {
		pTd = (pHead != pTail) ? pHead->next : 0;
		pHead->release();
		pHead = pTd;
	}
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::nuke(void)
{
	wipeList(queuedHead, queuedTail);
	wipeList(scheduledHead, scheduledTail);
	wipeList(doneHead, doneTail);
	wipeList(freeHead, freeTail);
	IOFree(this, sizeof *this);
}

__attribute__((visibility("hidden")))
IOReturn XHCIAsyncEndpoint::CreateTDs(IOUSBCommand* command, uint16_t streamId, uint32_t mystery, uint8_t immediateDataSize, uint8_t* pImmediateData)
{
	XHCIAsyncTD* pTd;
	size_t transferRequestBytes, numBytesLeft, bytesDone;
	uint32_t maxBytesPerTD, currentTDBytes;
	uint16_t tdIndex;
	bool haveImmediateData;
	bool usingMultipleTDs;

	if (unusable)
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
		pTd->onMaxTDBytesBoundary = !(bytesDone % maxTDBytes);
		pTd->multiTDTransaction = usingMultipleTDs;
		pTd->bytesPreceedingThisTD = bytesDone;
		pTd->bytesThisTD = currentTDBytes;
		pTd->mystery = mystery;
		pTd->maxNumPagesInTD = static_cast<uint16_t>((currentTDBytes / PAGE_SIZE) + 2U);
		pTd->streamId = streamId;
		pTd->bytesThisTDCompleted = currentTDBytes;
		if (haveImmediateData) {
			pTd->haveImmediateData = haveImmediateData;
			if (immediateDataSize)
				bcopy(pImmediateData, &pTd->immediateData[0], immediateDataSize);
		}
		if (queuedHead)
			queuedTail->next = pTd;
		else
			queuedHead = pTd;
		queuedTail = pTd;
		++numTDsQueued;
		bytesDone += currentTDBytes;
		if (bytesDone >= transferRequestBytes)
			break;
		++tdIndex;
		numBytesLeft -= currentTDBytes;
		pTd->bytesFollowingThisTD = numBytesLeft;
		currentTDBytes = (numBytesLeft < maxBytesPerTD) ? static_cast<uint32_t>(numBytesLeft) : maxBytesPerTD;
	} while (true);
	pTd->numTDsThisTransaction = tdIndex;
	pTd->onMaxTDBytesBoundary = true;
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
	if (unusable) {
		if (pRing->endpointUnusable)
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
									   pTd->onMaxTDBytesBoundary,
									   pTd->multiTDTransaction,
									   &pTd->firstTrbIndex,
									   &pTd->TrbCount,
									   false,
									   &pTd->lastTrbIndex);
		if (rc != kIOReturnSuccess) {
			if (provider)
				++provider->_diagCounters[DIAGCTR_XFERLAYOUT];
			if (queuedHead) {
				pTd->next = queuedHead;
				queuedHead = pTd;
			} else {
				queuedHead = pTd;
				queuedTail = pTd;
			}
			++numTDsQueued;
			break;
		}
		if (scheduledHead)
			scheduledTail->next = pTd;
		else
			scheduledHead = pTd;
		scheduledTail = pTd;
		++numTDsScheduled;
		if (pRing->endpointUnusable)
			pRing->schedulingPending = true;
		else
			provider->StartEndpoint(pRing->slot, pRing->endpoint, pTd->streamId);
	} while(queuedHead);
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::Abort(void)
{
	unusable = true;
	pRing->endpointUnusable = true;
	while (scheduledHead) {
		IOUSBCommand* command = scheduledHead->command;
		FlushTDsWithStatus(command);
		MoveTDsFromReadyQToDoneQ(command);
	}
	pRing->endpointUnusable = false;
	unusable = false;
#if 0
	if (numTDsDone)
		Complete(provider->GetIntelFlag(pRing->slot) ? kIOReturnNotResponding : kIOReturnAborted);
#else
	if (numTDsDone)
		Complete(kIOReturnAborted);
#endif
}

__attribute__((visibility("hidden")))
struct XHCIAsyncTD* XHCIAsyncEndpoint::GetTDFromActiveQueueWithIndex(uint16_t indexInQueue)
{
	XHCIAsyncTD *prev, *pTd;
	pTd = prev = scheduledHead;
	while (pTd) {
		if (pTd->lastTrbIndex == indexInQueue)
			break;
		if (pTd == scheduledTail)
			return 0;
		prev = pTd;
		pTd = pTd->next;
	}
	if (!pTd)
		return 0;
	if (pTd == scheduledTail) {
		if (pTd == scheduledHead) {
			scheduledTail = 0;
			scheduledHead = 0;
		} else
			scheduledTail = prev;
	} else if (pTd == scheduledHead)
		scheduledHead = pTd->next;
	else
		prev->next = pTd->next;
	--numTDsScheduled;
	return pTd;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::RetireTDs(XHCIAsyncTD* pTd, IOReturn passthruReturnCode, bool callCompletion, bool resetEndpoint)
{
	uint8_t slot, endpoint;

	PutTDonDoneQueue(pTd);
	if (resetEndpoint) {
		slot = pRing->slot;
		endpoint = pRing->endpoint;
		provider->QuiesceEndpoint(slot, endpoint);
		FlushTDsWithStatus(pTd->command);
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
	if (callCompletion)
		Complete(passthruReturnCode);
	ScheduleTDs();
}

__attribute__((visibility("hidden")))
XHCIAsyncTD* XHCIAsyncEndpoint::GetTDFromFreeQueue(bool newOneOk)
{
	XHCIAsyncTD* pTd;

	if (!freeHead && newOneOk) {
		pTd = XHCIAsyncTD::ForEndpoint(this);
		if (pTd) {
			if (freeHead)
				freeTail->next = pTd;
			else
				freeHead = pTd;
			freeTail = pTd;
			++numTDsFree;
		}
	}
	pTd = GetTD(&freeHead, &freeTail, &numTDsFree);
	if (pTd)
		pTd->reinit();
	return pTd;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::PutTDonDoneQueue(XHCIAsyncTD* pTd)
{
	if (doneHead)
		doneTail->next = pTd;
	else
		doneHead = pTd;
	doneTail = pTd;
	++numTDsDone;
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::FlushTDsWithStatus(IOUSBCommand* command)
{
	XHCIAsyncTD* pTd;
	uint32_t streamId;
	int32_t indexInQueue;
	bool foundSome;

	pTd = scheduledHead;
	if (!command || !pTd)
		return;
	foundSome = false;
	indexInQueue = 0;
	streamId = 0U;
	while (pTd && pTd->command == command) {
		if (pTd == scheduledTail) {
			scheduledTail = 0;
			scheduledHead = 0;
		} else if (pTd == scheduledHead)
			scheduledHead = pTd->next;
		--numTDsScheduled;
		streamId = pTd->streamId;
		indexInQueue = pTd->lastTrbIndex;
		PutTDonDoneQueue(pTd);
		++indexInQueue;
		if (indexInQueue >= static_cast<int32_t>(pRing->numTRBs) - 1)
			indexInQueue = 0;
		pTd = scheduledHead;
		foundSome = true;
	}
	if (foundSome)
		provider->SetTRDQPtr(pRing->slot, pRing->endpoint, streamId, indexInQueue);
}

__attribute__((visibility("hidden")))
void XHCIAsyncEndpoint::MoveTDsFromReadyQToDoneQ(IOUSBCommand* command)
{
	XHCIAsyncTD* pTd;
	while (queuedHead) {
		pTd = GetTD(&queuedHead, &queuedTail, &numTDsQueued);
		if (!pTd)
			continue;
		if (command && command != pTd->command) {
			if (queuedHead)
				pTd->next = queuedHead;
			else
				queuedTail = pTd;
			queuedHead = pTd;
			++numTDsQueued;
			break;
		}
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
		command->SetUIMScratch(9U, command->GetUIMScratch(9U) + pTd->bytesThisTDCompleted);
		if (pTd->onMaxTDBytesBoundary &&
			pTd->finalTDInTransaction) {
			comp = command->GetUSLCompletion();
			if (comp.action) {
				provider->Complete(comp,
								   passthruReturnCode,
								   command->GetUIMScratch(9U));
				pTd->bytesThisTDCompleted = 0U;
			}
			pTd->onMaxTDBytesBoundary = false;
		}
		if (freeHead)
			freeTail->next = pTd;
		else
			freeHead = pTd;
		freeTail = pTd;
		++numTDsFree;
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
void XHCIAsyncEndpoint::UpdateTimeouts(bool isRHPortDisconnected, uint32_t frameNumber, bool isEndpointOk)
{
	uint64_t addr;
	int64_t trbIndex64;
	XHCIAsyncTD* pTd;
	IOUSBCommand* command;
	IOReturn passthruReturnCode;
	uint32_t transferLength, ndto, cto, uims4, uims5, uims6;
	int32_t next;
	bool screwed;

	addr = GenericUSBXHCI::GetTRBAddr64(&pRing->stopTrb);
	transferLength = XHCI_TRB_2_REM_GET(pRing->stopTrb.c);
	trbIndex64 = GenericUSBXHCI::DiffTRBIndex(addr, pRing->physAddr);
	if (trbIndex64 < 0 || trbIndex64 >= pRing->numTRBs - 1U)
		trbIndex64 = pRing->dequeueIndex;
	pTd = scheduledHead;
	if (!pTd)
		return;
	if (!isRHPortDisconnected) {
		command = pTd->command;
		if (!command)
			return;
		ndto = command->GetNoDataTimeout();
		cto = command->GetCompletionTimeout();
		uims5 = command->GetUIMScratch(5U);
		if (!uims5) {
			command->SetUIMScratch(5U, frameNumber);
			uims5 = frameNumber;
		}
		screwed = false;
		if (cto && frameNumber - uims5 > cto) {
			pTd->bytesThisTDCompleted = pTd->bytesThisTD - transferLength;
			screwed = true;
		}
		if (ndto) {
			uims4 = command->GetUIMScratch(4U);
			uims6 = command->GetUIMScratch(6U);
			if (!uims6 ||
				command->GetUIMScratch(3U) != static_cast<uint32_t>(trbIndex64) ||
				uims4 != transferLength) {
				command->SetUIMScratch(3U, static_cast<uint32_t>(trbIndex64));
				command->SetUIMScratch(4U, transferLength);
				command->SetUIMScratch(6U, frameNumber);
			} else if (frameNumber - uims6 > ndto) {
				pTd->bytesThisTDCompleted = pTd->bytesThisTD - uims4;
				screwed = true;
			}
		}
		if (!screwed)
			return;
		passthruReturnCode = kIOUSBTransactionTimeout;
	} else {
		pTd->bytesThisTDCompleted = pTd->bytesThisTD - transferLength;
		passthruReturnCode = kIOReturnNotResponding;
	}
	next = pTd->lastTrbIndex + 1;
	if (next >= static_cast<int32_t>(pRing->numTRBs) - 1)
		next = 0;
	if (provider->GetIntelFlag(pRing->slot))
		passthruReturnCode = kIOReturnNotResponding;
	if (!isEndpointOk)
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
