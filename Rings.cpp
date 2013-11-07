//
//  Rings.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on October 10th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//
//

#include "GenericUSBXHCI.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Rings
#pragma mark -

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
	InitPreallocedRing(pRing);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::InitPreallocedRing(ringStruct* pRing)
{
	pRing->enqueueIndex = 0U;
	pRing->dequeueIndex = 0U;
	pRing->lastSeenDequeueIndex = 0U;
	pRing->lastSeenFrame = 0U;
	pRing->nextIsocFrame = 0ULL;
	TRBStruct* t = &pRing->ptr[pRing->numTRBs - 1U];
	SetTRBAddr64(t, pRing->physAddr);
	t->d |= XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK) | XHCI_TRB_3_TC_BIT;
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
int32_t CLASS::CountRingToED(ringStruct const* pRing, int32_t trbIndexInRingQueue, uint32_t* pShortFall)
{
	int32_t next;
	uint32_t trbType;
	TRBStruct* pTrb = &pRing->ptr[trbIndexInRingQueue];

	/*
	 * Note: CountRingToED is called with an index taken from
	 *   a Transfer Event TRB.  For a stop TRB the index
	 *   may be pRing->enqueueIndex, so check to make sure.
	 */
	if (trbIndexInRingQueue == pRing->enqueueIndex)
		return trbIndexInRingQueue;
	trbType = XHCI_TRB_3_TYPE_GET(pTrb->d);
	while (trbType != XHCI_TRB_TYPE_EVENT_DATA &&
		   (pTrb->d & XHCI_TRB_3_CHAIN_BIT)) {
		next = trbIndexInRingQueue + 1;
		if (trbType == XHCI_TRB_TYPE_LINK || next >= static_cast<int32_t>(pRing->numTRBs) - 1)
			next = 0;
		if (next == static_cast<int32_t>(pRing->enqueueIndex))
			break;
		trbIndexInRingQueue = next;
		pTrb = &pRing->ptr[trbIndexInRingQueue];
		trbType = XHCI_TRB_3_TYPE_GET(pTrb->d);
		if (trbType == XHCI_TRB_TYPE_NORMAL)
			*pShortFall += XHCI_TRB_2_BYTES_GET(pTrb->c);
	}
	return trbIndexInRingQueue;
}

__attribute__((visibility("hidden")))
uint16_t CLASS::NextTransferDQ(ringStruct const* pRing, int32_t index)
{
	/*
	 * Note: Assumes index != pRing->enqueueIndex
	 */
	++index;
	if ((index >= static_cast<int32_t>(pRing->numTRBs) - 1) ||
		(index != pRing->enqueueIndex &&
		 XHCI_TRB_3_TYPE_GET(pRing->ptr[index].d) == XHCI_TRB_TYPE_LINK))
		index = 0;
	return static_cast<uint16_t>(index);
}
