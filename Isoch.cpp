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
	 * The interval is 2^intervalExponent microframes
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
	 * kIsocRingSizeinMS = 100
	 * This division by 256 is to translate TRBs -> Pages
	 */
	pIsochEp->numPagesInRingQueue = (static_cast<uint32_t>(pIsochEp->boundOnPagesPerFrame) * 100U + 255U) / 256U;
	pIsochEp->inSlot = 129U;
	rc = CreateEndpoint(slot, endpoint, static_cast<uint16_t>(maxPacketSize),
						intervalExponent, epType, 0U, maxBurst, multiple, pIsochEp);
	if (rc != kIOReturnSuccess && !pIsochEp->pRing) {
		static_cast<void>(__sync_fetch_and_sub(&_numEndpoints, 1));
		DeleteIsochEP(pIsochEp);
	}
	return rc;
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
IOReturn CLASS::AbortIsochEP(class GenericUSBXHCIIsochEP* pIsochEp)
{
	/*
	 * TBD
	 */
	return kIOReturnUnsupported;
}

__attribute__((visibility("hidden")))
void CLASS::AddIsocFramesToSchedule(GenericUSBXHCIIsochEP*)
{
	/*
	 * TBD
	 */
}

__attribute__((visibility("hidden")))
IOReturn CLASS::RetireIsocTransactions(GenericUSBXHCIIsochEP*, bool)
{
	/*
	 * TBD
	 */
	return kIOReturnUnsupported;
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
	/*
	 * TBD
	 */
	return 0U;
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
int32_t GenericUSBXHCIIsochTD::FrameForEventIndex(uint32_t trbIndex)
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
