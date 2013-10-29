//
//  Streams.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on October 9th, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "XHCITypes.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3


#pragma mark -
#pragma mark Sorting Functions
#pragma mark -

static
void swap(ringStruct* pRing, int32_t L, int32_t R)
{
	uint64_t swap_key = pRing[L].physAddr;
	IOBufferMemoryDescriptor* swap_value_1 = pRing[L].md;
	TRBStruct* swap_value_2 = pRing[L].ptr;
	pRing[L].physAddr = pRing[R].physAddr;
	pRing[L].md = pRing[R].md;
	pRing[L].ptr = pRing[R].ptr;
	pRing[R].physAddr = swap_key;
	pRing[R].md = swap_value_1;
	pRing[R].ptr = swap_value_2;
}

/*
 * Returns index P such that
 *   for all index > P elements are > pivot_value
 *   for all index <= P, elements are <= pivot_value
 */
static
int32_t partition(ringStruct* pRing, int32_t L, int32_t R, int32_t pivot_index)
{
	uint64_t pivot_value = pRing[pivot_index].physAddr;
	while (L <= R) {
		while (L <= R && CLASS::DiffTRBIndex(pRing[R].physAddr, pivot_value) > 0)
			--R;
		while (L <= R && CLASS::DiffTRBIndex(pRing[L].physAddr, pivot_value) <= 0)
			++L;
		/*
		 * Note: Either L < R, or L == R + 1 here
		 */
		if (L < R)
			swap(pRing, L++, R--);
	}
	return R;
}

static
void qsort(ringStruct* pRing, uint32_t maxStream)
{
	int32_t lefts[9], rights[9], tos, L, R, pivot_index;
	tos = 0;
	lefts[tos] = 1;
	rights[tos] = static_cast<int32_t>(maxStream);
	while (tos >= 0) {
		L = lefts[tos];
		R = rights[tos];
		if (L >= R) {
			--tos;
			continue;
		}
		pivot_index = partition(pRing, L, R, R);
		if (pivot_index == R)
			/*
			 * Note: This happens if all elements are <= pivot, in
			 *   which case we're guaranteed that nothing was
			 *   moved, and the pivot is still at R.
			 */
			--pivot_index;
		if ((R - pivot_index) <= (pivot_index - L + 1)) {
			lefts[tos] = L;
			rights[tos] = pivot_index;
			++tos;
			lefts[tos] = pivot_index + 1;
			rights[tos] = R;
		} else {
			lefts[tos] = pivot_index + 1;
			rights[tos] = R;
			++tos;
			lefts[tos] = L;
			rights[tos] = pivot_index;
		}
	}
}

#pragma mark -
#pragma mark Streams
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::RestartStreams(int32_t slot, int32_t endpoint, uint32_t streamId)
{
	SlotStruct* pSlot = SlotPtr(slot);
	ringStruct* pRing = pSlot->ringArrayForEndpoint[endpoint];
	if (!pRing)
		return;
	uint16_t lastStream = pSlot->lastStreamForEndpoint[endpoint];
	if (lastStream < 2U)
		return;
	/*
	 * Note: It is probably enough to ring the doorbell
	 *   for the first stream found with a non-empty
	 *   ring.
	 */
	for (uint16_t sid = 1U; sid <= lastStream; ++sid)
		if (sid != streamId &&
			pRing[sid].dequeueIndex != pRing[sid].enqueueIndex)
			StartEndpoint(slot, endpoint, sid);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::CreateStream(ringStruct* pRing, uint16_t streamId)
{
	if (!pRing ||
		streamId >= pRing->numTRBs ||
		!pRing->ptr)
		return kIOReturnBadArgument;
	ringStruct* pStreamRing = &pRing[streamId];
	pStreamRing->nextIsocFrame = 0ULL;
	pStreamRing->returnInProgress = false;
	pStreamRing->deleteInProgress = false;
	pStreamRing->schedulingPending = false;
	if (pStreamRing->md)
		return kIOReturnInternalError;
	pStreamRing->md = pRing->md;
	pStreamRing->epType = pRing->epType;
	XHCIAsyncEndpoint* pAsyncEp = pRing->asyncEndpoint;
	pStreamRing->asyncEndpoint = XHCIAsyncEndpoint::withParameters(this, pStreamRing,
																   pAsyncEp->maxPacketSize,
																   pAsyncEp->maxBurst,
																   pAsyncEp->multiple);
	if (!pStreamRing->asyncEndpoint)
		return kIOReturnNoMemory;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::CleanupPartialStreamAllocations(ringStruct* pRing, uint16_t lastStream)
{
	for (uint16_t i = 1U; i <= lastStream; ++i) {
		if (pRing[i].asyncEndpoint) {
			pRing[i].asyncEndpoint->release();
			pRing[i].asyncEndpoint = 0;
		}
		pRing[i].md = 0;
	}
}

__attribute__((visibility("hidden")))
ringStruct* CLASS::FindStream(int32_t slot, int32_t endpoint, uint64_t addr, int32_t* pTrbIndexInRingQueue)
{
	int64_t diffIdx64;
	SlotStruct* pSlot = SlotPtr(slot);
	ringStruct *pStreamRing, *pRing = pSlot->ringArrayForEndpoint[endpoint];
	int32_t M, L = 1, R = pSlot->lastStreamForEndpoint[endpoint];
	while (L <= R) {
		M = (L + R) / 2;
		pStreamRing = &pRing[M];
		diffIdx64 = DiffTRBIndex(addr, pStreamRing->physAddr);
		if (diffIdx64 < 0) {
			R = M - 1;
			continue;
		}
		if (diffIdx64 >= pStreamRing->numTRBs - 1U) {	// Note: originally >
			L = M + 1;
			continue;
		}
		*pTrbIndexInRingQueue = static_cast<int32_t>(diffIdx64);
		return pStreamRing;
	}
	*pTrbIndexInRingQueue = 0;
	return 0;
}

__attribute__((visibility("hidden")))
void CLASS::DeleteStreams(int32_t slot, int32_t endpoint)
{
	SlotStruct* pSlot = SlotPtr(slot);
	ringStruct* pRing = pSlot->ringArrayForEndpoint[endpoint];
	if (!pRing)
		return;
	uint16_t lastStream = pSlot->lastStreamForEndpoint[endpoint];
	for (uint16_t streamId = 1U; streamId <= lastStream; ++streamId) {
		XHCIAsyncEndpoint* pAsyncEp = pRing[streamId].asyncEndpoint;
		if (pAsyncEp) {
			pAsyncEp->Abort();
			pAsyncEp->release();
			pRing[streamId].asyncEndpoint = 0;
		}
		pRing[streamId].md = 0;
	}
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AllocStreamsContextArray(ringStruct* pRing, uint32_t maxStream)
{
	size_t scaSize = ((1U + maxStream) * sizeof(xhci_stream_ctx) + PAGE_SIZE - 1U) & -PAGE_SIZE;
	/*
	 * TBD: this is contingent on kMaxStreamsAllowed <= 256, so
	 *   maxStream <= 255 and therefore scaSize is always PAGE_SIZE.
	 *   If scaSize > PAGE_SIZE, the first scaSize bytes need
	 *   to be kIOMemoryPhysicallyContiguous.
	 */
	if (kIOReturnSuccess != MakeBuffer(kIODirectionInOut,
									   scaSize + maxStream * PAGE_SIZE,
									   -PAGE_SIZE,
									   &pRing->md,
									   reinterpret_cast<void**>(&pRing->ptr),
									   &pRing->physAddr))
		return kIOReturnNoMemory;
	pRing->numTRBs = static_cast<uint16_t>(1U + maxStream);
	pRing->dequeueIndex = 0U;
	for (uint16_t streamId = 1U; streamId <= maxStream; ++streamId, scaSize += PAGE_SIZE) {
		ringStruct* pStreamRing = &pRing[streamId];
		if (pStreamRing->md)
			continue;
		IOByteCount segLength;
		pStreamRing->ptr = reinterpret_cast<TRBStruct*>(reinterpret_cast<uintptr_t>(pRing->ptr) + scaSize);
		pStreamRing->physAddr = pRing->md->getPhysicalSegment(scaSize, &segLength, 0);
		pStreamRing->numTRBs = static_cast<uint16_t>(PAGE_SIZE / sizeof *pStreamRing->ptr);
		pStreamRing->numPages = 1U;
		pStreamRing->cycleState = 1U;
	}
	qsort(pRing, maxStream);
	for (uint16_t streamId = 1U; streamId <= maxStream; ++streamId) {
		ringStruct* pStreamRing = &pRing[streamId];
		if (pStreamRing->md)
			continue;
		InitPreallocedRing(pStreamRing);
		SetTRBAddr64(&pRing->ptr[streamId], (pStreamRing->physAddr & ~15ULL) | 3ULL);
	}
	return kIOReturnSuccess;
}
