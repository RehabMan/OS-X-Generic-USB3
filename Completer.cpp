//
//  Completer.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on October 18th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//
//

#include "GenericUSBXHCI.h"
#include "Completer.h"

#define MAX_FREE_RETENTION 16

__attribute__((visibility("hidden")))
bool Completer::AddItem(IOUSBCompletion const* pCompletion, IOReturn status, uint32_t actualByteCount, bool allowImmediate)
{
	CompleterItem* pItem;

	if (!pCompletion)
		return true;
	if (freeHead) {
		pItem = freeHead;
		if (freeHead == freeTail) {
			freeHead = 0;
			freeTail = 0;
		} else
			freeHead = pItem->next;
		--freeCount;
	} else {
		pItem = static_cast<CompleterItem*>(IOMalloc(sizeof *pItem));
		if (!pItem) {
			if (allowImmediate) {
				if (owner)
					owner->Complete(*pCompletion, status, actualByteCount);
				return true;
			}
			return false;
		}
	}
	pItem->completion = *pCompletion;
	pItem->status = status;
	pItem->actualByteCount = actualByteCount;
	if (activeTail)
		activeTail->next = pItem;
	else
		activeHead = pItem;
	activeTail = pItem;
	/*
	 * Note: If flushing, we're already executing inside
	 *   InternalFlush, so no need to reschedule.
	 */
	if (!flushing && owner)
		owner->ScheduleEventSource();
	return true;
}

__attribute__((visibility("hidden")))
void Completer::InternalFlush(void)
{
	CompleterItem* pItem;
	flushing = true;
	pItem = activeHead;
	do {
		if (pItem == activeTail) {
			activeHead = 0;
			activeTail = 0;
		} else
			activeHead = pItem->next;
		if (freeCount < MAX_FREE_RETENTION) {
			if (freeTail)
				freeTail->next = pItem;
			else
				freeHead = pItem;
			freeTail = pItem;
			++freeCount;
			/*
			 * Note: pItem is on free list and may be reused inside Complete,
			 *   but its content is loaded on stack before the call.
			 */
			if (owner)
				owner->Complete(pItem->completion, pItem->status, pItem->actualByteCount);
		} else {
			if (owner)
				owner->Complete(pItem->completion, pItem->status, pItem->actualByteCount);
			IOFree(pItem, sizeof *pItem);
		}
	} while ((pItem = activeHead));
	flushing = false;
}

__attribute__((visibility("hidden")))
void Completer::Finalize(void)
{
	CompleterItem* pNext;
	while (activeHead) {
		if (activeHead == activeTail)
			pNext = 0;
		else
			pNext = activeHead->next;
		IOFree(activeHead, sizeof *pNext);
		activeHead = pNext;
	}
	activeTail = 0;
	while (freeHead) {
		if (freeHead == freeTail)
			pNext = 0;
		else
			pNext = freeHead->next;
		IOFree(freeHead, sizeof *pNext);
		freeHead = pNext;
	}
	freeTail = 0;
	freeCount = 0;
	flushing = false;
}
