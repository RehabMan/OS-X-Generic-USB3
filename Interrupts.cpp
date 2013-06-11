//
//  Interrupts.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 5th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "Isoch.h"
#include "XHCITypes.h"
#include <IOKit/IOFilterInterruptEventSource.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

extern "C" uint64_t ml_cpu_int_event_time(void);	// Note: in com.apple.kpi.unsupported

#pragma mark -
#pragma mark Interrupts
#pragma mark -

#pragma mark -
#pragma mark Handlers
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::InterruptHandler(OSObject* owner, IOInterruptEventSource*, int)
{

	/*
	 * Threaded Handler Context
	 */
	CLASS* controller = static_cast<CLASS*>(owner);
#if 0
	static bool emitted;
#endif
	if (!controller ||
		controller->isInactive() ||
		controller->m_invalid_regspace ||
		!controller->_controllerAvailable)
		return;
#if 0
	if (!emitted)
		emitted = true;
#endif
	controller->PollInterrupts(0);
}

__attribute__((visibility("hidden")))
bool CLASS::PrimaryInterruptFilter(OSObject* owner, IOFilterInterruptEventSource* source)
{
	/*
	 * Interrupt Context
	 */
	CLASS* controller = static_cast<CLASS*>(owner);
	if (!controller)
		return false;
	static_cast<void>(__sync_fetch_and_add(&controller->_interruptCounters[1], 1));
	if (controller->isInactive()) {
		static_cast<void>(__sync_fetch_and_add(&controller->_interruptCounters[2], 1));
		return false;
	}
	if (!source || !controller->_controllerAvailable) {
		/*
		 * Note: When sleeping and using pin interrupt, may get one that
		 *   belongs to another device.
		 */
		static_cast<void>(__sync_fetch_and_add(&controller->_interruptCounters[3], 1));
		return false;
	}
	if (controller->m_invalid_regspace) {
		static_cast<void>(__sync_fetch_and_add(&controller->_interruptCounters[3], 1));
		source->disable();	// Note: For MSI this is a no-op
		return false;
	}
	// Process this interrupt
	//
#if 0
	controller->_filterInterruptActive = true;
#endif
	controller->FilterInterrupt(source);
#if 0
	controller->_filterInterruptActive = false;
#endif
	return false;
}

__attribute__((visibility("hidden")))
void CLASS::FilterInterrupt(IOFilterInterruptEventSource* source)
{
	/*
	 * Interrupt Context
	 */
	bool invokeContinuation = false;
	int32_t interrupter = source->getIntIndex() - _baseInterruptIndex;
#if 0
	if (!interrupter) {
		uint32_t sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
		if (m_invalid_regspace) {
			source->disable();	// Note: For MSI this is a no-op
			return;
		}
#if 0
		Write32Reg(&_pXHCIOperationalRegisters->USBSts, XHCI_STS_EINT);	// clear XHCI_STS_EINT
#endif
		if (sts & XHCI_STS_HSE)
			invokeContinuation = true;
	}
#endif
	if (preFilterEventRing(source, interrupter)) {
		static_cast<void>(__sync_fetch_and_add(&_interruptCounters[0], 1));
		while (FilterEventRing(interrupter, &invokeContinuation));
		postFilterEventRing(interrupter);
	}
	if (m_invalid_regspace) {
		source->disable();	// Note: For MSI this is a no-op
		return;
	}
	if (invokeContinuation)
		source->signalInterrupt();	// invokes InterruptHandler on WorkLoop
}

__attribute__((noinline, visibility("hidden")))
bool CLASS::preFilterEventRing(IOFilterInterruptEventSource* source, int32_t interrupter)
{
	uint32_t iman;

	/*
	 * Interrupt Context
	 */
	if (!source->getAutoDisable())
		return true;
	iman = Read32Reg(&_pXHCIRuntimeRegisters->irs[interrupter].iman);
	if (m_invalid_regspace)
		return false;
	if (iman & XHCI_IMAN_INTR_PEND) {
		Write32Reg(&_pXHCIRuntimeRegisters->irs[interrupter].iman, iman);	// clear XHCI_IMAN_INTR_PEND
		return true;
	}
	return false;
}

__attribute__((noinline, visibility("hidden")))
void CLASS::postFilterEventRing(int32_t interrupter)
{
	uint64_t erdp;
	EventRingStruct* ePtr = &_eventRing[interrupter];

	/*
	 * Interrupt Context
	 */
	if (m_invalid_regspace)
		return;
	if (!ePtr->foundSome) {
		/*
		 * Check if EHB is on
		 */
		uint32_t ehb = Read32Reg(reinterpret_cast<uint32_t volatile const*>(&_pXHCIRuntimeRegisters->irs[interrupter].erdp));
		if (m_invalid_regspace ||
			!(ehb & static_cast<uint32_t>(XHCI_ERDP_LO_BUSY)))
			return;
		/*
		 * If EHB is on, fall thru and update ERDP anyway
		 */
	}
	erdp = ePtr->erdp + ePtr->xHCDequeueIndex * sizeof *ePtr->erstPtr;
	erdp |= XHCI_ERDP_LO_BUSY;
	Write64Reg(&_pXHCIRuntimeRegisters->irs[interrupter].erdp, erdp, true);
	ePtr->foundSome = false;
}

__attribute__((noinline, visibility("hidden")))
bool CLASS::FilterEventRing(int32_t interrupter, bool* pInvokeContinuation)
{
	/*
	 * Interrupt Context
	 */
	uint8_t rhPort, cb;
	uint16_t next;
#if 0
	uint32_t mfIndex;
#endif
	TRBStruct localTrb;
	EventRingStruct* ePtr = &_eventRing[interrupter];
	cb = ePtr->cycleState;
	cb ^= *reinterpret_cast<uint8_t volatile*>(&ePtr->erstPtr[ePtr->xHCDequeueIndex].dwEvrsReserved);
	if (cb & XHCI_TRB_3_CYCLE_BIT)
		return false;
	localTrb = *reinterpret_cast<TRBStruct*>(&ePtr->erstPtr[ePtr->xHCDequeueIndex]);
	++ePtr->xHCDequeueIndex;
	if (ePtr->xHCDequeueIndex >= ePtr->numxHCEntries) {
		ePtr->xHCDequeueIndex = 0U;
		ePtr->cycleState ^= 1U;
	}
	ePtr->foundSome = true;
	switch (XHCI_TRB_3_TYPE_GET(localTrb.d)) {
		case XHCI_TRB_EVENT_TRANSFER:
			if (processTransferEvent(&localTrb))
				break;
			if (pInvokeContinuation)	// Note: Invoke PollEventRing2 to handle _isochEPList
				*pInvokeContinuation = true;
			return true;
		case XHCI_TRB_EVENT_MFINDEX_WRAP:
			_millsecondCounter += 2048U;	// 2^14 * 0.125 us = 2048 ms
#if 0
			PrintEventTRB(&localTrb, interrupter, true, 0);
			mfIndex = Read32Reg(&_pXHCIRuntimeRegisters->MFIndex);
			if (m_invalid_regspace)
				return false;
			mfIndex &= XHCI_MFINDEX_MASK;
			_millsecondsTimers[2] = _millsecondCounter + (mfIndex / 8U);
#else
			_millsecondsTimers[2] = _millsecondCounter;
#endif
			_millsecondsTimers[0] = ml_cpu_int_event_time();	// Note: time stored by kernel interrupt handler close to interrupt entry
			break;
		case XHCI_TRB_EVENT_PORT_STS_CHANGE:
			rhPort = static_cast<uint8_t>(localTrb.a >> 24);
			if (rhPort && rhPort <= kMaxPorts)
				RHPortStatusChangeBitmapSet(1U << rhPort);
			if (pInvokeContinuation)	// Note: Invoke PollInterrupts to call EnsureUsability
				*pInvokeContinuation = true;
			return true;
#if 0
		default:
			PrintEventTRB(&localTrb, interrupter, true, 0);
			break;
#endif
	}
	next = ePtr->bounceEnqueueIndex + 1U;
	if (next >= ePtr->numBounceEntries)
		next = 0U;
	if (next == ePtr->bounceDequeueIndex) {
		static_cast<void>(__sync_fetch_and_add(&ePtr->numBounceQueueOverflows, 1));
		return false;
	}
	ePtr->bounceQueuePtr[ePtr->bounceEnqueueIndex] = localTrb;
	ePtr->bounceEnqueueIndex = next;
	if (pInvokeContinuation)
		*pInvokeContinuation = true;
	return true;
}

#pragma mark -
#pragma mark Pollers
#pragma mark -

__attribute__((visibility("hidden")))
bool CLASS::PollEventRing2(int32_t interrupter)
{
	/*
	 * Threaded Handler Context
	 */
	uint16_t next;
	int32_t value;
	TRBStruct localTrb;
	EventRingStruct* ePtr = &_eventRing[interrupter];
	uint32_t sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (!m_invalid_regspace &&
		(sts & XHCI_STS_HSE) && !_HSEDetected) {
		IOLog("%s: HSE bit set:%x (1)\n", __FUNCTION__, sts);
		_HSEDetected = true;
	}
	value = __sync_lock_test_and_set(&ePtr->numBounceQueueOverflows, 0);
	if (value > 0) {
		_diagCounters[DIAGCTR_BNCEOVRFLW] += value;
		IOLog("%s: Secondary event queue %d overflowed: %d\n", __FUNCTION__,
			  interrupter, value);
	}
#if 0
	value = __sync_lock_test_and_set(&_debugFlag, 0);
	if (value > 0)
		IOLog("%s: DebugFlags: %d\n", __FUNCTION__, value);
	value = __sync_lock_test_and_set(&_errorCounters[2], 0);
	if (value > 0)
		IOLog("%s: Event changed after reading: %d\n", __FUNCTION__, value);
#endif
	value = __sync_lock_test_and_set(&_errorCounters[3], 0);
	if (value > 0)
		IOLog("%s: Isoc problems: %d\n", __FUNCTION__, value);
	for (GenericUSBXHCIIsochEP* iter = static_cast<GenericUSBXHCIIsochEP*>(_isochEPList);
		 iter;
		 iter = static_cast<GenericUSBXHCIIsochEP*>(iter->nextEP))
		if (iter->producerCount != iter->consumerCount)
			RetireIsocTransactions(iter, true);
	if (ePtr->bounceDequeueIndex == ePtr->bounceEnqueueIndex)
		goto done;
	localTrb = ePtr->bounceQueuePtr[ePtr->bounceDequeueIndex];
	ClearTRB(&ePtr->bounceQueuePtr[ePtr->bounceDequeueIndex], false);
	next = ePtr->bounceDequeueIndex + 1U;
	if (next >= ePtr->numBounceEntries)
		next = 0U;
	ePtr->bounceDequeueIndex = next;
	switch (XHCI_TRB_3_TYPE_GET(localTrb.d)) {
		case TRB_RENESAS_CMD_COMP:
			if (!(_errataBits & kErrataRenesas))
				break;
		case XHCI_TRB_EVENT_CMD_COMPLETE:
			if (!DoCMDCompletion(localTrb))
				++_diagCounters[DIAGCTR_CMDERR];
			break;
		case XHCI_TRB_EVENT_TRANSFER:
			/*
			 * Note: processTransferEvent2 returns false
			 *   if the TransferEvent had invalid data
			 *   in it (slot #, endpoint#, TRB pointer.)
			 *   A faulty TransferEvent is ignored, except
			 *   for counting them for diagnostic purposes.
			     Some xHC may return spurious TransferEvents.
			 */
			if (!processTransferEvent2(&localTrb, interrupter))
				++_diagCounters[DIAGCTR_XFERERR];
			break;
#if 0
		case XHCI_TRB_EVENT_PORT_STS_CHANGE:
			PrintEventTRB(&localTrb, interrupter, false, 0);
			EnsureUsability();	 // Note: This is done in PollInterrupts using PCD
			break;
#endif
		case XHCI_TRB_EVENT_MFINDEX_WRAP:
			_millsecondsTimers[1] = _millsecondsTimers[0];
			_millsecondsTimers[3] = _millsecondsTimers[2];
#if 0
			PrintEventTRB(&localTrb, interrupter, false, 0);
#endif
			break;
		case XHCI_TRB_EVENT_DEVICE_NOTIFY:
#if 0
			PrintEventTRB(&localTrb, interrupter, false, 0);
#else
			IOLog("%s: Device Notification, slot %u, err %u, data %#llx, type %u\n", __FUNCTION__,
				  localTrb.d >> 24, localTrb.c >> 24,
				  (static_cast<uint64_t>(localTrb.b) << 24) | (localTrb.a >> 8),
				  (localTrb.a >> 4) & 15U);
#endif
			break;
#if 1
		case XHCI_TRB_EVENT_BW_REQUEST:
			IOLog("%s: Bandwidth Request, slot %u, err %u\n", __FUNCTION__,
				  localTrb.d >> 24, localTrb.c >> 24);
			break;
		case XHCI_TRB_EVENT_HOST_CTRL:
			IOLog("%s: Host Controller, err %u\n", __FUNCTION__, localTrb.c >> 24);
			break;
#endif
	}
	return true;

done:
	sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (!m_invalid_regspace &&
		(sts & XHCI_STS_HSE) && !_HSEDetected) {
		IOLog("%s: HSE bit set:%x (3)\n", __FUNCTION__, sts);
		_HSEDetected = true;
	}
	return false;
}

/*
 * Note: PollForCMDCompletions must be called on the
 *   workloop gate, or else it races with PollEventRing2
 *   for the event ring bounce queue as a 2nd consumer.
 */
__attribute__((visibility("hidden")))
void CLASS::PollForCMDCompletions(int32_t interrupter)
{
	EventRingStruct* ePtr = &_eventRing[interrupter];
	TRBStruct localTrb;
	uint16_t index, err;

	index = ePtr->bounceDequeueIndex;
	while (index != ePtr->bounceEnqueueIndex) {
		if (m_invalid_regspace)
			return;
		localTrb = ePtr->bounceQueuePtr[index];
		switch (XHCI_TRB_3_TYPE_GET(localTrb.d)) {
			case XHCI_TRB_EVENT_TRANSFER:
				err = static_cast<uint16_t>(XHCI_TRB_2_ERROR_GET(localTrb.c));
				if (err != XHCI_TRB_ERROR_STOPPED &&
					err != XHCI_TRB_ERROR_LENGTH)
					break;
				ClearTRB(&ePtr->bounceQueuePtr[index], false);
#if 0
				PrintEventTRB(&localTrb, interrupter, false, 0);
#endif
				if (!DoStopCompletion(&localTrb))
					++_diagCounters[DIAGCTR_XFERERR];
				break;
			case TRB_RENESAS_CMD_COMP:
				if (!(_errataBits & kErrataRenesas))
					break;
			case XHCI_TRB_EVENT_CMD_COMPLETE:
				ClearTRB(&ePtr->bounceQueuePtr[index], false);
#if 0
				PrintEventTRB(&localTrb, 0, false, 0);
#endif
				if (!DoCMDCompletion(localTrb))
					++_diagCounters[DIAGCTR_CMDERR];
				break;
		}
		++index;
		if (index >= ePtr->numBounceEntries)
			index = 0U;
	}
}

__attribute__((visibility("hidden")))
bool CLASS::DoStopCompletion(TRBStruct const* pTrb)
{
	uint64_t addr;
	ringStruct* pRing;
	int32_t trbIndexInRingQueue;
	uint8_t slot, endpoint;

	slot = static_cast<uint8_t>(XHCI_TRB_3_SLOT_GET(pTrb->d));
	if (!slot || slot > _numSlots || ConstSlotPtr(slot)->isInactive())
		return false;
	endpoint = static_cast<uint8_t>(XHCI_TRB_3_EP_GET(pTrb->d));
	if (!endpoint)
		return false;
	if (IsStreamsEndpoint(slot, endpoint)) {
		addr = GetTRBAddr64(pTrb);
		if (!addr)
			return false;	// save the trouble
		pRing = FindStream(slot, endpoint, addr, &trbIndexInRingQueue);
	} else
		pRing = GetRing(slot, endpoint, 0U);
	if (pRing)
		pRing->stopTrb = *pTrb;
	return true;
}

#pragma mark -
#pragma mark Event Ring Setup
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::InitAnEventRing(int32_t which)
{
	EventRingStruct* ePtr = &_eventRing[which];

	ePtr->numxHCEntries = 252U;
	IOReturn rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
							 PAGE_SIZE,
							 -PAGE_SIZE,
							 &ePtr->md,
							 reinterpret_cast<void**>(&ePtr->erstPtr),
							 &ePtr->erdp);
	if (rc != kIOReturnSuccess)
		return rc;
	InitEventRing(which, false);
	ePtr->numBounceEntries = 5120U;
	ePtr->bounceQueuePtr = static_cast<TRBStruct*>(IOMalloc(static_cast<size_t>(ePtr->numBounceEntries) * sizeof *ePtr->bounceQueuePtr));
	if (!ePtr->bounceQueuePtr)
		return kIOReturnNoMemory;
	bzero(ePtr->bounceQueuePtr, static_cast<size_t>(ePtr->numBounceEntries) * sizeof *ePtr->bounceQueuePtr);
	ePtr->bounceDequeueIndex = 0U;
	ePtr->bounceEnqueueIndex = 0U;
	ePtr->numBounceQueueOverflows = 0;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::InitEventRing(int32_t which, bool restarting)
{
	EventRingStruct* ePtr = &_eventRing[which];
	XHCIInterruptRegisterSet volatile* irSet = &_pXHCIRuntimeRegisters->irs[which];

	if (restarting) {
		if (ePtr->numxHCEntries)
			bzero(ePtr->erstPtr, ePtr->numxHCEntries * sizeof *ePtr->erstPtr);
	} else {
		bzero(ePtr->erstPtr, (static_cast<size_t>(ePtr->numxHCEntries) + 4U) * sizeof *ePtr->erstPtr);
		ePtr->erstba = ePtr->erdp;
		ePtr->erdp += 4U * sizeof *ePtr->erstPtr;
		ePtr->erstPtr->qwEvrsTablePtr = ePtr->erdp;
		ePtr->erstPtr->dwEvrsTableSize = ePtr->numxHCEntries;
		ePtr->erstPtr += 4;
	}
	Write64Reg(&irSet->erdp, ePtr->erdp, false);
	Write32Reg(&irSet->erstsz, 1U);
	Write64Reg(&irSet->erstba, ePtr->erstba, false);
	Write32Reg(&irSet->imod, 160U);	// IMODI = 40 us
	Write32Reg(&irSet->iman, XHCI_IMAN_INTR_ENA);
	ePtr->xHCDequeueIndex = 0U;
	ePtr->cycleState = 1U;
	ePtr->foundSome = false;
}

__attribute__((visibility("hidden")))
void CLASS::FinalizeAnEventRing(int32_t which)
{
	EventRingStruct* ePtr = &_eventRing[which];
	if (ePtr->md) {
		ePtr->md->complete();
		ePtr->md->release();
		ePtr->md = 0;
	}
	if (ePtr->bounceQueuePtr) {
		IOFree(ePtr->bounceQueuePtr, static_cast<size_t>(ePtr->numBounceEntries) * sizeof *ePtr->bounceQueuePtr);
		ePtr->bounceQueuePtr = 0;
	}
}

#pragma mark -
#pragma mark Sleep/Wake
#pragma mark -

__attribute__((noinline, visibility("hidden")))
void CLASS::SaveAnInterrupter(int32_t i)
{
	XHCIInterruptRegisterSet volatile* irSet = &_pXHCIRuntimeRegisters->irs[i];
	XHCIInterruptRegisterSet* sleep_area = &_interruptBackup[i];

	sleep_area->erstsz = Read32Reg(&irSet->erstsz);
	sleep_area->erstba = Read64Reg(&irSet->erstba);
	sleep_area->erdp = Read64Reg(&irSet->erdp);
	sleep_area->imod = Read32Reg(&irSet->imod);
	sleep_area->iman = Read32Reg(&irSet->iman);
}

__attribute__((noinline, visibility("hidden")))
void CLASS::RestoreAnInterrupter(int32_t i)
{
	XHCIInterruptRegisterSet volatile* irSet = &_pXHCIRuntimeRegisters->irs[i];
	XHCIInterruptRegisterSet* sleep_area = &_interruptBackup[i];

	Write32Reg(&irSet->erstsz, sleep_area->erstsz);
	Write64Reg(&irSet->erstba, sleep_area->erstba, false);
	Write64Reg(&irSet->erdp, sleep_area->erdp, false);
	Write32Reg(&irSet->imod, sleep_area->imod);
	Write32Reg(&irSet->iman, sleep_area->iman);
}

#pragma mark -
#pragma mark PCI
#pragma mark -

__attribute__((noinline, visibility("hidden")))
int CLASS::findInterruptIndex(IOService* target, bool allowMSI)
{
	int source, interruptType, pinInterruptToUse, msgInterruptToUse;

	pinInterruptToUse = 0;
	msgInterruptToUse = -1;
	for (interruptType = 0, source = 0;
		 target->getInterruptType(source, &interruptType) == kIOReturnSuccess;
		 ++source)
		if (interruptType & kIOInterruptTypePCIMessaged) {
			if (msgInterruptToUse < 0)
				msgInterruptToUse = source;
		} else
			pinInterruptToUse = source;
	return (msgInterruptToUse >= 0 && allowMSI) ? msgInterruptToUse : pinInterruptToUse;
}

#pragma mark -
#pragma mark Transfer Events
#pragma mark -

/*
 * Returns true iff event TRB should be copied to bounce buffer
 */
__attribute__((visibility("hidden")))
bool CLASS::processTransferEvent(TRBStruct const* pTrb)
{
	uint64_t timeStamp;
	int64_t diffIndex64;
	ringStruct* pRing;
	GenericUSBXHCIIsochEP* pIsochEp;
	GenericUSBXHCIIsochTD *pIsochTd, *pCachedHead;
	int32_t slot, endpoint, indexIntoTD;
	uint32_t cachedProducer;
	uint16_t stopSlot, testSlot, nextSlot;
	bool eventIsForThisTD, copyEvent;

	/*
	 * Interrupt Context
	 */
	slot = static_cast<int32_t>(XHCI_TRB_3_SLOT_GET(pTrb->d));
	if (slot <= 0 || slot > _numSlots || ConstSlotPtr(slot)->isInactive())
		return true;
	endpoint = static_cast<int32_t>(XHCI_TRB_3_EP_GET(pTrb->d));
	if (endpoint < 2 || !IsIsocEP(slot, endpoint))
		return true;
	pRing = GetRing(slot, endpoint, 0U);
	if (pRing->isInactive()) {
		static_cast<void>(__sync_fetch_and_add(&_errorCounters[3], 1));
		return true;
	}
	pIsochEp = pRing->isochEndpoint;
	if (!pIsochEp || !pIsochEp->tdsScheduled)
		return true;
	copyEvent = true;
	switch (XHCI_TRB_2_ERROR_GET(pTrb->c)) {
		case XHCI_TRB_ERROR_SUCCESS:
		case XHCI_TRB_ERROR_XACT:
		case XHCI_TRB_ERROR_SHORT_PKT:
			if (pIsochEp->outSlot > 127U)
				break;
			stopSlot = pIsochEp->inSlot & 127U;
			pCachedHead = const_cast<GenericUSBXHCIIsochTD*>(pIsochEp->savedDoneQueueHead);
			cachedProducer = pIsochEp->producerCount;
			testSlot = pIsochEp->outSlot;
			timeStamp = mach_absolute_time();
			while (testSlot != stopSlot) {
				nextSlot = (testSlot + 1U) & 127U;
				pIsochTd = pIsochEp->tdSlots[testSlot];
				if (!pIsochTd || !pIsochEp->tdsScheduled) {
					testSlot = nextSlot;
					continue;
				}
				eventIsForThisTD = false;
				indexIntoTD = -1;
				diffIndex64 = DiffTRBIndex(GetTRBAddr64(pTrb), pRing->physAddr);
				if (diffIndex64 >= 0 && diffIndex64 < pRing->numTRBs - 1U) {	// Note: originally <=
					indexIntoTD = pIsochTd->FrameForEventIndex(static_cast<uint32_t>(diffIndex64));
					if (indexIntoTD >= 0)
						eventIsForThisTD = true;
				}
				pIsochTd->eventTrb = *pTrb;
				pIsochTd->UpdateFrameList(reinterpret_cast<AbsoluteTime const&>(timeStamp));
				if (!eventIsForThisTD || indexIntoTD == static_cast<int32_t>(pIsochTd->_framesInTD) - 1) {
					pIsochEp->tdSlots[testSlot] = 0;
					pIsochTd->_doneQueueLink = pCachedHead;
					pCachedHead = pIsochTd;
					++cachedProducer;
					static_cast<void>(__sync_fetch_and_add(&pIsochEp->onProducerQ, 1));
					static_cast<void>(__sync_fetch_and_sub(&pIsochEp->scheduledTDs, 1));
					pIsochEp->outSlot = testSlot;
				} else
					copyEvent = false;
				if (eventIsForThisTD)
					break;
				testSlot = nextSlot;
			}
			IOSimpleLockLock(pIsochEp->wdhLock);
			pIsochEp->savedDoneQueueHead = pCachedHead;
			pIsochEp->producerCount = cachedProducer;
			IOSimpleLockUnlock(pIsochEp->wdhLock);
			break;
		case XHCI_TRB_ERROR_RING_UNDERRUN:
		case XHCI_TRB_ERROR_RING_OVERRUN:
		case XHCI_TRB_ERROR_MISSED_SERVICE:
		case XHCI_TRB_ERROR_STOPPED:
		case XHCI_TRB_ERROR_LENGTH:
			pIsochEp->tdsScheduled = false;
			break;
	}
	return copyEvent;
}

__attribute__((visibility("hidden")))
bool CLASS::processTransferEvent2(TRBStruct const* pTrb, int32_t interrupter)
{
	int64_t diffIndex64;
	ringStruct* pRing;
	XHCIAsyncEndpoint* pAsyncEp;
	GenericUSBXHCIIsochEP* pIsochEp;
	XHCIAsyncTD* pAsyncTd;
	int32_t trbIndexInRingQueue;
	IOReturn rc;
	uint16_t next;
	bool callCompletion, flush;

	/*
	 * Threaded Handler Context
	 */
	uint64_t addr = GetTRBAddr64(pTrb);
	uint32_t shortfall = XHCI_TRB_2_REM_GET(pTrb->c);
	int32_t err = static_cast<int32_t>(XHCI_TRB_2_ERROR_GET(pTrb->c));
	int32_t slot = static_cast<int32_t>(XHCI_TRB_3_SLOT_GET(pTrb->d));
	int32_t endpoint = static_cast<int32_t>(XHCI_TRB_3_EP_GET(pTrb->d));
	bool ED = ((pTrb->d) & XHCI_TRB_3_ED_BIT) != 0U;

	if (err == XHCI_TRB_ERROR_STOPPED || err == XHCI_TRB_ERROR_LENGTH)
		return DoStopCompletion(pTrb);

	if (slot <= 0 || slot > _numSlots || ConstSlotPtr(slot)->isInactive() || !endpoint)
		return false;
	if (IsStreamsEndpoint(slot, endpoint)) {
		if (!addr)
			return false;	// save the trouble
		pRing = FindStream(slot, endpoint, addr, &trbIndexInRingQueue);
		if (!pRing)
			return false;
	} else {
		pRing = GetRing(slot, endpoint, 0U);
		if (!pRing)
			return false;
		switch (err) {
			case XHCI_TRB_ERROR_RING_UNDERRUN:
			case XHCI_TRB_ERROR_RING_OVERRUN:
				if ((pRing->epType | CTRL_EP) != ISOC_IN_EP ||
					!pRing->isochEndpoint)
					return true;
				pIsochEp = pRing->isochEndpoint;
				if (!pIsochEp->activeTDs) {
					pIsochEp->outSlot = 129U;
					pIsochEp->inSlot = 129U;
				}
				if (pIsochEp->schedulingDelayed) {
					pIsochEp->schedulingDelayed = false;
					AddIsocFramesToSchedule(pIsochEp);
				}
				return true;
		}
		if (!pRing->md)
			return false;
		diffIndex64 = DiffTRBIndex(addr, pRing->physAddr);
		if (diffIndex64 < 0 || diffIndex64 >= pRing->numTRBs - 1U) // Note: originally > pRing->numTRBs
			return true;
		trbIndexInRingQueue = static_cast<int32_t>(diffIndex64);
	}
#if 0
	PrintEventTRB(pTrb, interrupter, false, pRing);
#endif
	if (IsIsocEP(slot, endpoint)) {
	update_dq_and_done:
		next = static_cast<uint16_t>(trbIndexInRingQueue + 1);
		if (next >= pRing->numTRBs - 1U)
			next = 0U;
		pRing->dequeueIndex = next;
		return true;
	}
	pAsyncEp = pRing->asyncEndpoint;
	if (!pAsyncEp)
		goto update_dq_and_done;
	pAsyncTd = pAsyncEp->GetTDFromActiveQueueWithIndex(static_cast<uint16_t>(trbIndexInRingQueue));
	if (!pAsyncTd) {
		if (XHCI_EPCTX_0_EPSTATE_GET(GetSlotContext(slot, endpoint)->_e.dwEpCtx0) != EP_STATE_HALTED)
			goto update_dq_and_done;
		int32_t newIndex = CountRingToED(pRing, trbIndexInRingQueue, &shortfall);
		if (newIndex == trbIndexInRingQueue)
			goto update_dq_and_done;
		trbIndexInRingQueue = newIndex;
		pAsyncTd = pAsyncEp->GetTDFromActiveQueueWithIndex(static_cast<uint16_t>(trbIndexInRingQueue));
		if (!pAsyncTd)
			goto update_dq_and_done;
	}
	/*
	 * This handles the case where xHC issues spurious
	 *   success transfer event with duplicate addr
	 *   after issuing a short-packet transfer event.
	 *   (observed on Intel Series 7/C210)
	 */
	if (pRing->enqueueIndex == pRing->dequeueIndex)
		return true;
	next = static_cast<uint16_t>(trbIndexInRingQueue + 1);
	if (next >= pRing->numTRBs - 1U)
		next = 0U;
	pRing->dequeueIndex = next;
	if (ED)
		shortfall = pAsyncTd->bytesThisTD - shortfall;
	if (err != XHCI_TRB_ERROR_SUCCESS) {
		callCompletion = true;
		flush = !pAsyncTd->finalTDInTransaction;
		rc = TranslateXHCIStatus(err, (endpoint & 1) != 0, GetSlCtxSpeed(GetSlotContext(slot)), false);
	} else {
		callCompletion = pAsyncTd->interruptThisTD;
		flush = false;
		rc = kIOReturnSuccess;
		/*
		 * xHC sometimes reports a SUCCESS completion code
		 *   even though shortfall != 0.  Track this for
		 *   possible flushing.  Presently it's ignored,
		 *   which may cause subsequent TDs in same
		 *   transaction to be orphaned.
		 *   (observed on Etron ASRock P67, Fresco Logic)
		 */
		if (shortfall)
			++_diagCounters[DIAGCTR_SHORTSUCCESS];
	}
	pAsyncTd->shortfall = shortfall;
	pAsyncEp->RetireTDs(pAsyncTd,
						GetNeedsReset(slot) ? kIOReturnNotResponding : rc,
						callCompletion,
						flush);
	return true;
}
