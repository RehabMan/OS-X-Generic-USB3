//
//  Async.cpp
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
		 *     it run. (TBD: what if dequeueIndex doesn't get updated when ring empties?)
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
#if 0
	/*
	 * Note: Added Mavericks
	 */
	if (updateDequeueIndex && !provider->_controllerAvailable) {
		(&pRing->schedulingPending)[1] = true;
		return;
	}
#endif
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
				/*
				 * Note: Mavericks updates a couple
				 *   of diagnostic counters here.
				 */
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
	if (provider->GetNeedsReset(pRing->slot))
		passthruReturnCode = kIOReturnNotResponding;
	/*
	 * Note: Mavericks updates a couple
	 *   of diagnostic counters here.
	 */
	next = pTd->lastTrbIndex + 1;
	if (next >= static_cast<int32_t>(pRing->numTRBs) - 1)
		next = 0;
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
