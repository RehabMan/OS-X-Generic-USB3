//
//  V2Pure.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on December 26th, 2012.
//  Copyright (c) 2012-2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "Async.h"
#include "XHCITRB.h"
#include "XHCITypes.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark IOUSBControllerV2 Pure
#pragma mark -

IOReturn CLASS::UIMCreateControlEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt16 maxPacketSize,
										 UInt8 speed, USBDeviceAddress highSpeedHub, int highSpeedPort)
{
	TRBStruct localTrb = { 0U };
	IOReturn rc;
	ContextStruct* pContext;
	ringStruct* pRing;
	int32_t retFromCMD;
	uint16_t packetSize;
	uint8_t slot;

	if (functionNumber == _hub3Address || functionNumber == _hub2Address)
		return kIOReturnSuccess;
	packetSize = maxPacketSize != 9U ? maxPacketSize : 512U;
	if (!functionNumber) {
		if (_numEndpoints >= _maxNumEndpoints)
			return kIOUSBEndpointCountExceeded;
		retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_ENABLE_SLOT, 0);
		if (retFromCMD == -1 || retFromCMD <= -1000)
			return retFromCMD == (-1000 - XHCI_TRB_ERROR_NO_SLOTS) ? kIOUSBDeviceCountExceeded : kIOReturnInternalError;
		slot = static_cast<uint8_t>(retFromCMD);
#if 0
		/*
		 * Note: Added Mavericks
		 */
		if (_vendorID == kVendorIntel && _IntelSlotWorkaround && slot == _numSlots) {
			_IntelSlotWorkaround = false;
			retFromCMD = CleanupControlEndpoint(slot, true);
			_IntelSWSlot = slot;
			if (retFromCMD == -1 || retFromCMD <= -1000)
				return kIOReturnInternalError;
			ClearTRB(&localTrb, true);
			retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_ENABLE_SLOT, 0);
			if (retFromCMD == -1 || retFromCMD <= -1000)
				return retFromCMD == (-1000 - XHCI_TRB_ERROR_NO_SLOTS) ? kIOUSBDeviceCountExceeded : kIOReturnInternalError;
			slot = static_cast<uint8_t>(retFromCMD);
			if (slot == _numSlots)
				ExecuteGetPortBandwidthWorkaround();
		}
		_IntelSlotWorkaround = false;
#endif
		if (!slot || slot > _numSlots) {
			/*
			 * Sanity check.  Bail out, 'cause UIMDeleteEndpoint
			 *   won't handle invalid slot # well.
			 */
			CleanupControlEndpoint(slot, true);
			IOLog("%s: xHC assigned invalid slot number %u\n", __FUNCTION__, slot);
			return kIOUSBDeviceCountExceeded;
		}
		pRing = CreateRing(slot, 1, 0U);
		if (!pRing || pRing->md) {
			CleanupControlEndpoint(slot, true);
			return kIOReturnInternalError;
		}
		_addressMapper.Slot[0] = slot;
		_addressMapper.Active[0] = true;
		rc = AllocRing(pRing, 1);
		if (rc != kIOReturnSuccess) {
			CleanupControlEndpoint(slot, false);
			return kIOReturnNoMemory;
		}
		rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
						GetDeviceContextSize(),
						-PAGE_SIZE,
						&SlotPtr(slot)->md,
						reinterpret_cast<void**>(&SlotPtr(slot)->ctx),
						&SlotPtr(slot)->physAddr);
		if (rc != kIOReturnSuccess) {
			CleanupControlEndpoint(slot, false);
			return kIOReturnNoMemory;
		}
		if (!pRing->asyncEndpoint) {
			pRing->epType = CTRL_EP;
			pRing->asyncEndpoint = XHCIAsyncEndpoint::withParameters(this, pRing, packetSize, 0U, 0U);
			if (!pRing->asyncEndpoint) {
				CleanupControlEndpoint(slot, false);
				return kIOReturnNoMemory;
			}
			static_cast<void>(__sync_fetch_and_add(&_numEndpoints, 1));
		}
		SetDCBAAAddr64(&_dcbaa.ptr[slot], ConstSlotPtr(slot)->physAddr);
		return AddressDevice(slot,
							 packetSize,
							 true,
							 speed,
							 GetSlotID(highSpeedHub),
							 highSpeedPort);
	}
	if (endpointNumber)
		return kIOReturnInternalError;
	slot = GetSlotID(functionNumber);
	if (!slot)
		return kIOReturnInternalError;
	pContext = GetSlotContext(slot, 1);
	if (!pContext)
		return kIOReturnInternalError;
	if (XHCI_EPCTX_1_MAXP_SIZE_GET(pContext->_e.dwEpCtx1) == packetSize)
		return kIOReturnSuccess;
	GetInputContext();
	pContext = GetInputContextPtr();
	pContext->_ic.dwInCtx1 = XHCI_INCTX_1_ADD_MASK(1U);
	pContext = GetInputContextPtr(2);
	pContext->_e.dwEpCtx1 = XHCI_EPCTX_1_MAXP_SIZE_SET(static_cast<uint32_t>(packetSize));
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	localTrb.d = XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(slot));
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_EVALUATE_CTX, 0);
	ReleaseInputContext();
	if (retFromCMD == -1)
		return kIOReturnInternalError;
	if (retFromCMD > -1000)
		return kIOReturnSuccess;
	if (retFromCMD == -1000 - XHCI_TRB_ERROR_PARAMETER) {
#if 0
		PrintContext(GetInputContextPtr());
		PrintContext(GetInputContextPtr(2));
#endif
	}
	return kIOReturnInternalError;
}

IOReturn CLASS::UIMCreateBulkEndpoint(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction, UInt8 speed,
									  UInt16 maxPacketSize, USBDeviceAddress highSpeedHub, int highSpeedPort)
{
	return CreateBulkEndpoint(functionNumber, endpointNumber, direction, maxPacketSize, 0U, 0U);
}

IOReturn CLASS::UIMCreateInterruptEndpoint(short functionAddress, short endpointNumber,UInt8 direction,
										   short speed, UInt16 maxPacketSize, short pollingRate,
										   USBDeviceAddress highSpeedHub, int highSpeedPort)
{
	uint32_t maxBurst = 0U;

	/*
	 * Preprocessing code added OS 10.8.3
	 */
	if (maxPacketSize > kUSB_EPDesc_MaxMPS) {
		maxBurst = ((maxPacketSize + kUSB_EPDesc_MaxMPS - 1U) / kUSB_EPDesc_MaxMPS);
		maxPacketSize = (maxPacketSize + maxBurst - 1U) / maxBurst;
		--maxBurst;
	}
	return CreateInterruptEndpoint(functionAddress, endpointNumber, direction, speed,
								   maxPacketSize, pollingRate, maxBurst);
}
