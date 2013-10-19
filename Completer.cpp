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
			freeHead = freeHead->next;
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
	if (activeHead)
		activeTail->next = pItem;
	else
		activeHead = pItem;
	activeTail = pItem;
	return true;
}

__attribute__((visibility("hidden")))
void Completer::InternalFlush(void)
{
	CompleterItem* pNext;
	do {
		if (activeHead == activeTail)
			pNext = 0;
		else
			pNext = activeHead->next;
		if (owner)
			owner->Complete(activeHead->completion, activeHead->status, activeHead->actualByteCount);
		if (freeCount < MAX_FREE_RETENTION) {
			if (freeHead)
				freeTail->next = activeHead;
			else
				freeHead = activeHead;
			freeTail = activeHead;
			++freeCount;
		} else
			IOFree(activeHead, sizeof *pNext);
		activeHead = pNext;
	} while (activeHead);
	activeTail = 0;
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
}
