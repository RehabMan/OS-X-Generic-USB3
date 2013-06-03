//
//  Isoch.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on March 12th, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Isoch.h"
#include "XHCITypes.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

OSDefineMetaClassAndFinalStructors(GenericUSBXHCIIsochEP, IOUSBControllerIsochEndpoint);
OSDefineMetaClassAndFinalStructors(GenericUSBXHCIIsochTD, IOUSBControllerIsochListElement);

#pragma mark -
#pragma mark Isoch Endpoints
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::CreateIsochEndpoint(int16_t functionAddress, int16_t endpointNumber, uint32_t maxPacketSize,
									uint8_t direction, uint8_t interval, uint32_t maxBurst, uint8_t multiple)
{
	GenericUSBXHCIIsochEP* pIsochEp;
	IOReturn rc;
	uint32_t oneMPS;
	uint8_t slot, endpoint, epType, speed, intervalExponent;

	slot = GetSlotID(functionAddress);
	if (!slot)
		return kIOReturnInternalError;
	endpoint = TranslateEndpoint(endpointNumber, direction);
	if (endpoint < 2U || endpoint >= kUSBMaxPipes)
		return kIOReturnBadArgument;
	epType = (direction == kUSBIn) ? ISOC_IN_EP : ISOC_OUT_EP;
	speed = GetSlCtxSpeed(GetSlotContext(slot));
	/*
	 * Note: The interval is 2^intervalExponent microframes
	 */
	switch (speed) {
		case kUSBDeviceSpeedLow:
			return kIOReturnInternalError;
		case kUSBDeviceSpeedFull:
			intervalExponent = 3U;
			break;
		default:
			if (interval > 16U)
				intervalExponent = 15U;
			else if (!interval)
				intervalExponent = 0U;
			else
				intervalExponent = interval - 1U;
			break;
	}
	oneMPS = maxPacketSize * (maxBurst + 1U) * (multiple + 1U);
	pIsochEp = OSDynamicCast(GenericUSBXHCIIsochEP,
							 FindIsochronousEndpoint(functionAddress, endpointNumber, direction, 0));
	if (pIsochEp) {
		if (!pIsochEp->pRing) {
			IOLog("%s: Found an orphaned IsochEP (ringless) on the IsochEP list\n", __FUNCTION__);
			return kIOReturnInternalError;
		}
		if (oneMPS == pIsochEp->maxPacketSize * pIsochEp->maxBurst * pIsochEp->multiple)
			return kIOReturnSuccess;
		if (XHCI_EPCTX_0_EPSTATE_GET(GetSlotContext(slot, endpoint)->_e.dwEpCtx0) == EP_STATE_RUNNING)
			StopEndpoint(slot, endpoint);
	} else {
		if (_numEndpoints >= _maxNumEndpoints)
			return kIOUSBEndpointCountExceeded;
		pIsochEp = OSDynamicCast(GenericUSBXHCIIsochEP,
								 CreateIsochronousEndpoint(functionAddress, endpointNumber, direction));
		if (!pIsochEp)
			return kIOReturnNoMemory;
		static_cast<void>(__sync_fetch_and_add(&_numEndpoints, 1));
		pIsochEp->speed = speed;
	}
	pIsochEp->maxBurst = static_cast<uint8_t>(maxBurst + 1U);
	pIsochEp->multiple = multiple + 1U;
	pIsochEp->maxPacketSize = maxPacketSize;
	pIsochEp->interval = 1U << intervalExponent;	// in microframes
	pIsochEp->intervalExponent = intervalExponent;
	if (intervalExponent < 3U) {
		pIsochEp->transfersPerTD = 8U >> intervalExponent;
		pIsochEp->frameNumberIncrease = 1U;
	} else {
		pIsochEp->transfersPerTD = 1U;
		pIsochEp->frameNumberIncrease = static_cast<uint16_t>(pIsochEp->interval / 8U);
	}
	pIsochEp->boundOnPagesPerFrame = static_cast<uint16_t>((oneMPS / static_cast<uint32_t>(PAGE_SIZE) + 3U) * pIsochEp->transfersPerTD);
	/*
	 * Notes:
	 *   kIsocRingSizeinMS = 100
	 *   This division by 256 is to translate TRBs -> Pages
	 */
	pIsochEp->numPagesInRingQueue = (static_cast<uint32_t>(pIsochEp->boundOnPagesPerFrame) * 100U + 255U) / 256U;
	pIsochEp->inSlot = 129U;
	rc = CreateEndpoint(slot, endpoint, static_cast<uint16_t>(maxPacketSize),
						intervalExponent, epType, 0U, maxBurst, multiple, pIsochEp);
	if (rc != kIOReturnSuccess && !pIsochEp->pRing) {
		DeleteIsochEP(pIsochEp);
		static_cast<void>(__sync_fetch_and_sub(&_numEndpoints, 1));
	}
	return rc;
}

static
void wipeIsochList(IOUSBControllerListElement* pHead, IOUSBControllerListElement* pTail)
{
	IOUSBControllerListElement* pTd;

	while (pHead) {
		pTd = (pHead != pTail) ? pHead->_logicalNext : 0;
		pHead->release();
		pHead = pTd;
	}
}

__attribute__((visibility("hidden")))
IOReturn CLASS::NukeIsochEP(GenericUSBXHCIIsochEP* pIsochEp)
{
	wipeIsochList(pIsochEp->toDoList, pIsochEp->toDoEnd);
	wipeIsochList(pIsochEp->doneQueue, pIsochEp->doneEnd);
	wipeIsochList(pIsochEp->deferredQueue, pIsochEp->deferredEnd);
	pIsochEp->activeTDs = 0U;
	return DeleteIsochEP(pIsochEp);
}


__attribute__((visibility("hidden")))
IOReturn CLASS::DeleteIsochEP(GenericUSBXHCIIsochEP* pIsochEp)
{
	IOUSBControllerIsochEndpoint *iter, *prev;

	if (pIsochEp->activeTDs) {
		AbortIsochEP(pIsochEp);
		if (pIsochEp->activeTDs)
			IOLog("%s: after abort there are still %u active TDs\n", __FUNCTION__, static_cast<uint32_t>(pIsochEp->activeTDs));
	}
	if (_v2ExpansionData) {
		for (prev = 0, iter = _isochEPList; iter; prev = iter, iter = iter->nextEP)
			if (iter == pIsochEp) {
				if (prev)
					prev->nextEP = iter->nextEP;
				else
					_isochEPList = iter->nextEP;
				break;
			}
	}
	pIsochEp->release();
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AbortIsochEP(GenericUSBXHCIIsochEP* pIsochEp)
{
	uint64_t timeStamp;
	GenericUSBXHCIIsochTD* pIsochTd;
	uint32_t slot, stopSlot, nextSlot;
	IOReturn rc;

	pIsochEp->aborting = true;
	timeStamp = mach_absolute_time();
	rc = RetireIsocTransactions(pIsochEp, false);
	if (rc != kIOReturnSuccess)
		IOLog("%s: RetireIsocTransactions returned %#x\n", __FUNCTION__, rc);
	if (pIsochEp->outSlot < 128U && pIsochEp->inSlot < 128U) {
		slot = pIsochEp->outSlot;
		stopSlot = pIsochEp->inSlot;
		while (slot != stopSlot) {
			nextSlot = (slot + 1U) & 127U;
			pIsochTd = pIsochEp->tdSlots[slot];
			if (!pIsochTd && nextSlot != pIsochEp->inSlot)
				pIsochEp->outSlot = nextSlot;
			if (pIsochTd) {
				bzero(&pIsochTd->eventTrb, sizeof pIsochTd->eventTrb);
				pIsochTd->UpdateFrameList(reinterpret_cast<AbsoluteTime const&>(timeStamp));
				static_cast<void>(__sync_fetch_and_sub(&pIsochEp->scheduledTDs, 1));
				if (pIsochEp->scheduledTDs < 0)
					IOLog("%s: scheduledTDs for endpoint %p is negative! (%d)\n", __FUNCTION__, pIsochEp, pIsochEp->scheduledTDs);
				PutTDonDoneQueue(pIsochEp, pIsochTd, true);
				pIsochEp->tdSlots[slot] = 0;
			}
			slot = nextSlot;
		}
		pIsochEp->outSlot = 129U;
		pIsochEp->inSlot = 129U;
	}
	pIsochTd = static_cast<GenericUSBXHCIIsochTD*>(GetTDfromToDoList(pIsochEp));
	while (pIsochTd) {
		pIsochTd->UpdateFrameList(reinterpret_cast<AbsoluteTime const&>(timeStamp));
		PutTDonDoneQueue(pIsochEp, pIsochTd, true);
		pIsochTd = static_cast<GenericUSBXHCIIsochTD*>(GetTDfromToDoList(pIsochEp));
	}
	if (!pIsochEp->scheduledTDs) {
		pIsochEp->firstAvailableFrame = 0U;
		pIsochEp->inSlot = 129U;
	}
	pIsochEp->accumulatedStatus = kIOReturnAborted;
	ReturnIsochDoneQueue(pIsochEp);
    pIsochEp->accumulatedStatus = kIOReturnSuccess;
	pIsochEp->aborting = false;
    return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::AddIsocFramesToSchedule_stage2(GenericUSBXHCIIsochEP* pIsochEp, uint16_t nextSlot,
										   uint64_t* pCurrFrame, bool* pFirstMicroFrame)
{
	GenericUSBXHCIIsochTD* pIsochTd;
	size_t transferOffset;
	uint32_t mystery, reqCount, frameId, TDPC, TLBPC, TBC, IsochBurstResiduePackets;
	IOReturn rc;
	uint16_t hwFrame;

	pIsochTd = static_cast<GenericUSBXHCIIsochTD*>(GetTDfromToDoList(pIsochEp));
	if (pIsochEp->outSlot > 128U)
		pIsochEp->outSlot = 0U;
	if (pIsochEp->continuousStream)
		hwFrame = static_cast<uint16_t>(GetFrameNumber() + _istKeepAwayFrames + 10U);
	else
		hwFrame = static_cast<uint16_t>(pIsochTd->_frameNumber);
	pIsochEp->tdSlots[pIsochEp->inSlot] = pIsochTd;
	*pCurrFrame += pIsochEp->frameNumberIncrease;
	pIsochEp->inSlot = nextSlot;
	transferOffset = pIsochTd->transferOffset;
	static_cast<void>(__sync_fetch_and_add(&pIsochEp->scheduledTDs, 1));
	pIsochEp->scheduledFrameNumber = pIsochTd->_frameNumber;
	for (uint32_t transfer = 0U; transfer < pIsochTd->_framesInTD; ++transfer) {
		if (pIsochTd->_pFrames) {
			if (pIsochTd->_lowLatency)
				reqCount = reinterpret_cast<IOUSBLowLatencyIsocFrame*>(pIsochTd->_pFrames)[pIsochTd->_frameIndex + transfer].frReqCount;
			else
				reqCount = pIsochTd->_pFrames[pIsochTd->_frameIndex + transfer].frReqCount;
		} else
			reqCount = 0U;
		mystery = XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_ISOCH);
		if (pIsochEp->continuousStream) {
			if (*pFirstMicroFrame && !pIsochEp->tdsScheduled) {
				frameId = XHCI_TRB_3_FRID_SET(static_cast<uint32_t>(hwFrame));
				*pFirstMicroFrame = false;
			} else
				frameId = XHCI_TRB_3_ISO_SIA_BIT;
		} else {
			if (!transfer)
				frameId = XHCI_TRB_3_FRID_SET(static_cast<uint32_t>(hwFrame));
			else
				frameId = XHCI_TRB_3_ISO_SIA_BIT;
		}
		mystery |= frameId;
		TDPC = (reqCount + pIsochEp->maxPacketSize - 1U) / pIsochEp->maxPacketSize;
		if (!TDPC)
			TDPC = 1U;
		TBC = ((TDPC + pIsochEp->maxBurst - 1U) / pIsochEp->maxBurst) - 1U;
		IsochBurstResiduePackets = TDPC % pIsochEp->maxBurst;
		if (!IsochBurstResiduePackets)
			TLBPC = pIsochEp->maxBurst - 1U;
		else
			TLBPC = IsochBurstResiduePackets - 1U;
		mystery |= XHCI_TRB_3_TBC_SET(TBC);
		mystery |= XHCI_TRB_3_TLBPC_SET(TLBPC);
		pIsochTd->statusUpdated[transfer] = false;
		rc = _createTransfer(pIsochTd,
							 true,
							 reqCount,
							 mystery,
							 transferOffset,
							 pIsochTd->interruptThisTD && (transfer == pIsochTd->_framesInTD - 1U),
							 false,
							 &pIsochTd->firstTrbIndex[transfer],
							 &pIsochTd->trbCount[transfer],
							 0);
		if (rc != kIOReturnSuccess)
			++_diagCounters[DIAGCTR_XFERLAYOUT];
		transferOffset += reqCount;
	}
}

__attribute__((visibility("hidden")))
void CLASS::AddIsocFramesToSchedule(GenericUSBXHCIIsochEP* pIsochEp)
{
	uint64_t currFrame, newCurrFrame, timeStamp;
	GenericUSBXHCIIsochTD* pIsochTd;
	IOReturn rc;
	uint16_t nextSlot;
	bool lostRegisterAccess, firstMicroFrame, ringFullAndEmpty;

	if (m_invalid_regspace)
		return;
	if (!pIsochEp->toDoList)
		return;
	if (pIsochEp->aborting) {
		IOLog("%s: pIsochEp(%p) is aborting - not adding\n", __FUNCTION__, pIsochEp);
		return;
	}
	if (pIsochEp->schedulingDelayed)
		return;
	if (pIsochEp->doneQueue && !pIsochEp->doneEnd) {
		IOLog("%s: inconsistent endpoint queue. pIsochEp[%p] doneQueue[%p] doneEnd[%p] doneQueue->_logicalNext[%p] onDoneQueue[%d] deferredTDs[%d]\n",
			  __FUNCTION__,
			  pIsochEp,
			  pIsochEp->doneQueue,
			  pIsochEp->doneEnd,
			  pIsochEp->doneQueue->_logicalNext,
			  static_cast<int32_t>(pIsochEp->onDoneQueue),
			  static_cast<int32_t>(pIsochEp->deferredTDs));
		IOSleep(1U);
	}
	if (!IOSimpleLockTryLock(_isochScheduleLock)) {
		IOLog("%s: could not obtain scheduling lock\n", __FUNCTION__);
		return;
	}
	/*
	 * Note: GetMicroFrameNumber has an IODelay inside.
	 *   Acquiring a spinlock disables preemption, which
	 *   makes IODelay use a spin delay instead of blocking.
	 */
	currFrame = (GetMicroFrameNumber() + 1ULL) >> 3;
	timeStamp = mach_absolute_time();
	lostRegisterAccess = false;
	if (!pIsochEp->continuousStream)
		while (pIsochEp->toDoList->_frameNumber <= currFrame + _istKeepAwayFrames) {
			pIsochTd = static_cast<GenericUSBXHCIIsochTD*>(GetTDfromToDoList(pIsochEp));
			bzero(&pIsochTd->eventTrb, sizeof pIsochTd->eventTrb);
			rc = pIsochTd->UpdateFrameList(reinterpret_cast<AbsoluteTime const&>(timeStamp));
			if (pIsochEp->scheduledTDs > 0)
				PutTDonDeferredQueue(pIsochEp, pIsochTd);
			else
				PutTDonDoneQueue(pIsochEp, pIsochTd, true);
			if (!pIsochEp->toDoList) {
				IOSimpleLockUnlock(_isochScheduleLock);
				if (thread_call_enter1(_returnIsochDoneQueueThread,
									   static_cast<thread_call_param_t>(pIsochEp)))
					IOLog("%s: thread_call_enter1(_returnIsochDoneQueueThread) was NOT scheduled.  That's not good\n", __FUNCTION__);
				return;
			}
			newCurrFrame = GetFrameNumber();
			if (m_invalid_regspace || !newCurrFrame) {
				lostRegisterAccess = true;
				break;
			}
			if (newCurrFrame > currFrame)
				currFrame = newCurrFrame;
		}
	ringFullAndEmpty = false;
	if (lostRegisterAccess || !pIsochEp->toDoList)
		goto complete;
	currFrame = pIsochEp->toDoList->_frameNumber;
	if (pIsochEp->inSlot > 128U)
		pIsochEp->inSlot = 0U;
	nextSlot = pIsochEp->inSlot;
	if (nextSlot == pIsochEp->outSlot)
		goto complete;
	firstMicroFrame = true;
	do {
		if (!pIsochEp->continuousStream &&
			pIsochEp->scheduledFrameNumber &&
			pIsochEp->tdsScheduled &&
			pIsochEp->scheduledFrameNumber + pIsochEp->frameNumberIncrease < currFrame) {
			pIsochEp->schedulingDelayed = true;
			break;
		}
		if (pIsochEp->inSlot == pIsochEp->outSlot) {
			ringFullAndEmpty = true;
			break;
		}
		for (uint32_t frameNumber = 0U; frameNumber < pIsochEp->frameNumberIncrease; ++frameNumber) {
			nextSlot = (pIsochEp->inSlot + 1U) & 127U;
			if (nextSlot == pIsochEp->outSlot)
				break;
		}
		if (nextSlot == pIsochEp->outSlot ||
			pIsochEp->tdSlots[pIsochEp->inSlot])
			break;
		if (pIsochEp->boundOnPagesPerFrame > FreeSlotsOnRing(pIsochEp->pRing)) {
			if (gux_log_level >= 2)
				++_diagCounters[DIAGCTR_XFERKEEPAWAY];
			break;
		}
		AddIsocFramesToSchedule_stage2(pIsochEp, nextSlot, &currFrame, &firstMicroFrame);
	} while (pIsochEp->toDoList);

complete:
	IOSimpleLockUnlock(_isochScheduleLock);
	if (ringFullAndEmpty)
		IOLog("%s: caught up pIsochEp->inSlot (%#x) pIsochEp->outSlot (%#x) - Ring is Full and Empty!\n",
			  __FUNCTION__, pIsochEp->inSlot, pIsochEp->outSlot);
	StartEndpoint(GetSlotID(pIsochEp->functionAddress),
				  TranslateEndpoint(pIsochEp->endpointNumber, pIsochEp->direction),
				  0U);
	if (!pIsochEp->tdsScheduled)
		pIsochEp->tdsScheduled = true;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::RetireIsocTransactions(GenericUSBXHCIIsochEP* pIsochEp, bool reQueueTransactions)
{
	uint64_t curFrame;
	GenericUSBXHCIIsochTD *pDoneTd, *pPrevTd, *pNextTd, *iTd;
	IOInterruptState intState;
	uint32_t cachedProducer, cachedConsumer, mfIndex;

	intState = IOSimpleLockLockDisableInterrupt(pIsochEp->wdhLock);
	pDoneTd = const_cast<GenericUSBXHCIIsochTD*>(pIsochEp->savedDoneQueueHead);
	cachedProducer = pIsochEp->producerCount;
	IOSimpleLockUnlockEnableInterrupt(pIsochEp->wdhLock, intState);
	cachedConsumer = pIsochEp->consumerCount;

	if (pDoneTd && cachedConsumer != cachedProducer) {
		pPrevTd = 0;
		while (true) {
			pDoneTd->_logicalNext = pPrevTd;
			pPrevTd = pDoneTd;
			++cachedConsumer;
			static_cast<void>(__sync_fetch_and_sub(&pIsochEp->onProducerQ, 1));
			++pIsochEp->onReversedList;
			if (cachedProducer == cachedConsumer)
				break;
			pDoneTd = static_cast<GenericUSBXHCIIsochTD*>(pDoneTd->_doneQueueLink);
		}
		pIsochEp->consumerCount = cachedConsumer;
		while (pDoneTd) {
			pNextTd = static_cast<GenericUSBXHCIIsochTD*>(pDoneTd->_logicalNext);
			pDoneTd->_logicalNext = 0;
			--pIsochEp->onReversedList;
			PutTDonDoneQueue(pIsochEp, pDoneTd, true);
			pDoneTd = pNextTd;
		}
	}
	if (reQueueTransactions) {
		iTd = OSDynamicCast(GenericUSBXHCIIsochTD, pIsochEp->doneEnd);
		if (iTd && iTd->_completion.action) {
			curFrame = GetFrameNumber();
			if (!m_invalid_regspace && iTd->_frameNumber == (curFrame + 1U)) {
				mfIndex = Read32Reg(&_pXHCIRuntimeRegisters->MFIndex);
				if (!m_invalid_regspace && (mfIndex & 7U) == 7U)
					IODelay(125U);
			}
		}
		ReturnIsochDoneQueue(pIsochEp);
		AddIsocFramesToSchedule(pIsochEp);
	}
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark GenericUSBXHCIIsochEP
#pragma mark -

bool GenericUSBXHCIIsochEP::init(void)
{
	if (!IOUSBControllerIsochEndpoint::init())
		return false;
	/*
	 * Note: a subclass of IOUSBControllerIsochEndpoint must
	 *   allow for reinitialization w/o leak, because init
	 *   is called from IOUSBControllerV2::CreateIsochronousEndpoint.
	 */
	if (!wdhLock) {
		wdhLock = IOSimpleLockAlloc();
		if (!wdhLock)
			return false;
	}
	inSlot = 129U;
	outSlot = 129U;
	return true;
}

void GenericUSBXHCIIsochEP::free(void)
{
	if (wdhLock) {
		IOSimpleLockFree(wdhLock);
		wdhLock = 0;
	}
	IOUSBControllerIsochEndpoint::free();
}

#pragma mark -
#pragma mark GenericUSBXHCIIsochTD
#pragma mark -

IOReturn GenericUSBXHCIIsochTD::UpdateFrameList(AbsoluteTime timeStamp)
{
	uint64_t addr;
	int64_t diffIndex64;
	ringStruct* pRing;
	IOUSBLowLatencyIsocFrame* pLLFrames;
	int32_t frameForEvent, i, frIdx;
	IOReturn rc;

	/*
	 * Note: called in primary interrupt context
	 */

	addr = GenericUSBXHCI::GetTRBAddr64(&eventTrb);
	pRing = static_cast<GenericUSBXHCIIsochEP*>(_pEndpoint)->pRing;
	pLLFrames = reinterpret_cast<IOUSBLowLatencyIsocFrame*>(_pFrames);
	rc = _pEndpoint->accumulatedStatus;
	if (!addr) {
		for (i = 0; i < _framesInTD; ++i)
			if (!statusUpdated[i]) {
				frIdx = static_cast<int32_t>(_frameIndex) + i;
				if (_lowLatency) {
					pLLFrames[frIdx].frActCount = 0U;
					pLLFrames[frIdx].frStatus = kIOUSBNotSent1Err;
					pLLFrames[frIdx].frTimeStamp = timeStamp;
				} else {
					_pFrames[frIdx].frActCount = 0U;
					_pFrames[frIdx].frStatus = kIOUSBNotSent1Err;
				}
				statusUpdated[i] = true;
			}
		return kIOUSBNotSent1Err;
	}
	diffIndex64 = GenericUSBXHCI::DiffTRBIndex(addr, pRing->physAddr);
	if (diffIndex64 < 0 || diffIndex64 >= pRing->numTRBs - 1U)
		return kIOReturnSuccess;
	frameForEvent = FrameForEventIndex(static_cast<uint32_t>(diffIndex64));
	if (frameForEvent < 0) {
		for (i = 0; i < _framesInTD; ++i)
			if (!statusUpdated[i]) {
				frIdx = static_cast<int32_t>(_frameIndex) + i;
				if (_lowLatency) {
					pLLFrames[frIdx].frActCount = pLLFrames[frIdx].frReqCount;
					pLLFrames[frIdx].frStatus = kIOReturnSuccess;
					pLLFrames[frIdx].frTimeStamp = timeStamp;
				} else {
					_pFrames[frIdx].frActCount = _pFrames[frIdx].frReqCount;
					_pFrames[frIdx].frStatus = kIOReturnSuccess;
				}
				statusUpdated[i] = true;
			}
		return kIOReturnSuccess;
	}
	for (i = 0; i < _framesInTD; ++i) {
		if (statusUpdated[i])
			continue;
		if (i > frameForEvent)
			break;
		frIdx = static_cast<int32_t>(_frameIndex) + i;
		if (i == frameForEvent) {
			uint8_t condCode = static_cast<uint8_t>(XHCI_TRB_2_ERROR_GET(eventTrb.c));
			uint32_t eventLen = XHCI_TRB_2_REM_GET(eventTrb.c);
			IOReturn frStatus = TranslateXHCIStatus(condCode);
			bool edEvent = ((eventTrb.d) & XHCI_TRB_3_ISP_BIT) != 0U;
			if (condCode == XHCI_TRB_ERROR_XACT) {
				GenericUSBXHCIIsochEP* pIsochEp = static_cast<GenericUSBXHCIIsochEP*>(_pEndpoint);
				if (pIsochEp->direction == kUSBIn &&
					pIsochEp->speed == kUSBDeviceSpeedHigh &&
					pIsochEp->multiple > 1U) {
					if (trbCount[i] > 1U && !edEvent)
						break;
					frStatus = kIOReturnUnderrun;
				}
			}
			if (frStatus != kIOReturnSuccess) {
				if (frStatus != kIOReturnUnderrun) {
					_pEndpoint->accumulatedStatus = frStatus;
					eventLen = 0U;
					edEvent = true;
				} else if (_pEndpoint->accumulatedStatus == kIOReturnSuccess)
					_pEndpoint->accumulatedStatus = kIOReturnUnderrun;
				rc = frStatus;
			}
			if (_lowLatency) {
				if (edEvent)
					pLLFrames[frIdx].frActCount = static_cast<uint16_t>(eventLen);
				else
					pLLFrames[frIdx].frActCount = static_cast<uint16_t>(pLLFrames[frIdx].frReqCount - eventLen);
				pLLFrames[frIdx].frStatus = frStatus;
				pLLFrames[frIdx].frTimeStamp = timeStamp;
			} else {
				if (edEvent)
					_pFrames[frIdx].frActCount = static_cast<uint16_t>(eventLen);
				else
					_pFrames[frIdx].frActCount = static_cast<uint16_t>(_pFrames[frIdx].frReqCount - eventLen);
				_pFrames[frIdx].frStatus = frStatus;
			}
			statusUpdated[i] = true;
			break;
		} else {
			if (_lowLatency) {
				pLLFrames[frIdx].frActCount = pLLFrames[frIdx].frReqCount;
				pLLFrames[frIdx].frStatus = kIOReturnSuccess;
				pLLFrames[frIdx].frTimeStamp = timeStamp;
			} else {
				_pFrames[frIdx].frActCount = _pFrames[frIdx].frReqCount;
				_pFrames[frIdx].frStatus = kIOReturnSuccess;
			}
			statusUpdated[i] = true;
		}
	}
	return rc;
}

IOReturn GenericUSBXHCIIsochTD::Deallocate(IOUSBControllerV2*)
{
	release();
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
GenericUSBXHCIIsochTD* GenericUSBXHCIIsochTD::ForEndpoint(GenericUSBXHCIIsochEP* provider)
{
	GenericUSBXHCIIsochTD* obj = OSTypeAlloc(GenericUSBXHCIIsochTD);;
	if (obj) {
		if (obj->init()) {
			obj->_pEndpoint = provider;
			obj->newFrame = false;
		} else {
			obj->release();
			obj = 0;
		}
	}
	return obj;
}

__attribute__((visibility("hidden")))
IOReturn GenericUSBXHCIIsochTD::TranslateXHCIStatus(uint32_t xhci_err)
{
	switch (xhci_err) {
		case XHCI_TRB_ERROR_SUCCESS:
			return kIOReturnSuccess;
		case XHCI_TRB_ERROR_XACT:
			return kIOUSBNotSent1Err;
		case XHCI_TRB_ERROR_STALL:
			return kIOUSBPipeStalled;
		case XHCI_TRB_ERROR_SHORT_PKT:
			return kIOReturnUnderrun;
	}
	return kIOReturnInternalError;
}

__attribute__((visibility("hidden")))
int32_t GenericUSBXHCIIsochTD::FrameForEventIndex(uint32_t trbIndex) const
{
	uint32_t firstTrbIndex;
	uint8_t transfersInTD = _framesInTD;

	for (uint8_t transfer = 0U; transfer < transfersInTD; ++transfer) {
		firstTrbIndex = this->firstTrbIndex[transfer];
		if (trbIndex >= firstTrbIndex && trbIndex < firstTrbIndex + trbCount[transfer])
			return transfer;
	}
	return -1;
}
