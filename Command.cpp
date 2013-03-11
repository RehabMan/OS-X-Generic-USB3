//
//  Command.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 27th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Command Ring
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::InitCMDRing(void)
{
	if (_commandRing.numTRBs) {
		bzero(_commandRing.callbacks, _commandRing.numTRBs * sizeof *_commandRing.callbacks);
		bzero(_commandRing.ptr, _commandRing.numTRBs * sizeof *_commandRing.ptr);
		SetTRBAddr64(&_commandRing.ptr[_commandRing.numTRBs - 1U], _commandRing.physAddr);
		_commandRing.ptr[_commandRing.numTRBs - 1U].d |= XHCI_TRB_3_TYPE_SET(XHCI_TRB_TYPE_LINK) | XHCI_TRB_3_TC_BIT;
	}
	_commandRing.cycleState = 1U;
	_commandRing.enqueueIndex = 0U;
	_commandRing.dequeueIndex = 0U;
	_commandRing.stopPending = false;
	Write64Reg(&_pXHCIOperationalRegisters->CRCr,
			   (_commandRing.physAddr & ~XHCI_CRCR_LO_MASK) | XHCI_CRCR_LO_RCS,
			   false);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::RestoreCRCr(void)
{
	uint64_t newCRCr;
	uint8_t newCycleState;

	newCRCr = _commandRing.physAddr + _commandRing.dequeueIndex * sizeof *_commandRing.ptr;
#if 0
	newCRCr &= ~XHCI_CRCR_LO_MASK;	// This is a XHCI design flaw, as dequeueIndex may not be a multiple of 4
#endif
	if (_commandRing.enqueueIndex >= _commandRing.dequeueIndex)
		newCycleState = _commandRing.cycleState;
	else
		newCycleState = _commandRing.cycleState ^ 1U;
	if (newCycleState & 1U)
		newCRCr |= XHCI_CRCR_LO_RCS;

	Write64Reg(&_pXHCIOperationalRegisters->CRCr, newCRCr, false);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::CommandStop(void)
{
	uint32_t lowCRCr = Read32Reg(reinterpret_cast<uint32_t volatile*>(&_pXHCIOperationalRegisters->CRCr));
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (!(lowCRCr & static_cast<uint32_t>(XHCI_CRCR_LO_CRR)))
		return kIOReturnSuccess;
	_commandRing.stopPending = true;
	Write32Reg(reinterpret_cast<uint32_t volatile*>(&_pXHCIOperationalRegisters->CRCr), XHCI_CRCR_LO_CS);
	for (int32_t count = 0; _commandRing.stopPending && count < 100; ++count) {
		if (count)
			IOSleep(1U);
		PollForCMDCompletions(0);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
	}
	if (_commandRing.stopPending) {
		_commandRing.stopPending = false;
		return kIOReturnTimeout;
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::CommandAbort(void)
{
	uint32_t lowCRCr = Read32Reg(reinterpret_cast<uint32_t volatile*>(&_pXHCIOperationalRegisters->CRCr));
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (!(lowCRCr & static_cast<uint32_t>(XHCI_CRCR_LO_CRR)))
		return kIOReturnSuccess;
	_commandRing.stopPending = true;
	Write32Reg(reinterpret_cast<uint32_t volatile*>(&_pXHCIOperationalRegisters->CRCr), XHCI_CRCR_LO_CA);
	for (int32_t count = 0; _commandRing.stopPending && count < 10000; ++count) {
		if (count)
			IODelay(10U);
		PollForCMDCompletions(0);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
	}
	if (_commandRing.stopPending) {
		_commandRing.stopPending = false;
		return kIOReturnTimeout;
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
int32_t CLASS::WaitForCMD(TRBStruct* trb, int32_t trbType, TRBCallback callback)
{
	int32_t count;
	int32_t volatile ret = -1;
	uint32_t sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (m_invalid_regspace)
		return ret;
	if ((sts & XHCI_STS_HSE) && !_HSEDetected) {
		IOLog("%s: HSE bit set:%x (1)\n", __FUNCTION__, sts);
		_HSEDetected = true;
	}
	if (isInactive() || !_controllerAvailable)
		return ret;
	if (EnqueCMD(trb, trbType, callback ? : _CompleteSlotCommand, const_cast<int32_t*>(&ret)) != kIOReturnSuccess)
		return ret;
	for (count = 0; ret == -1 && count < 10000; ++count) {
		if (count)
			IODelay(10U);
		PollForCMDCompletions(0);
		if (m_invalid_regspace)
			return ret;
	}
	if (ret != -1)
		return ret;
	IOLog("%s: Timeout waiting for command completion, 100ms\n", __FUNCTION__);
	CommandAbort();
	return ret;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::EnqueCMD(TRBStruct* trb, int32_t trbType, TRBCallback callback, void* param)
{
	TRBStruct* target;
	uint32_t fourth;
	int32_t next = _commandRing.enqueueIndex;
	if (next < static_cast<int>(_commandRing.numTRBs) - 2)
		++next;
	else
		next = 0;
	if (next == _commandRing.dequeueIndex)
		return kIOReturnNoResources;
	target = &_commandRing.ptr[_commandRing.enqueueIndex];
	target->a = trb->a;
	target->b = trb->b;
	target->c = trb->c;
	_commandRing.callbacks[_commandRing.enqueueIndex].func = callback;
	_commandRing.callbacks[_commandRing.enqueueIndex].param = param;
	fourth = (trb->d) & ~(XHCI_TRB_3_TYPE_SET(63U) | XHCI_TRB_3_CYCLE_BIT);
	fourth |= XHCI_TRB_3_TYPE_SET(trbType);
	if (_commandRing.cycleState)
		fourth |= XHCI_TRB_3_CYCLE_BIT;
	IOSync();
	target->d = fourth;
	IOSync();
	if (!next) {
		if (_commandRing.cycleState)
			_commandRing.ptr[_commandRing.numTRBs - 1U].d |= XHCI_TRB_3_CYCLE_BIT;
		else
			_commandRing.ptr[_commandRing.numTRBs - 1U].d &= ~XHCI_TRB_3_CYCLE_BIT;
		_commandRing.cycleState ^= 1U;
	}
	_commandRing.enqueueIndex = static_cast<uint16_t>(next);
	Write32Reg(&_pXHCIDoorbellRegisters[0], 0U);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
bool CLASS::DoCMDCompletion(TRBStruct trb)
{
	int64_t idx64;
	TRBCallbackEntry copy;
	uint16_t newIdx;
	uint64_t addr = GetTRBAddr64(&trb);
	if (!addr) {
			IOLog("%s: Zero pointer in CCE\n", __FUNCTION__);
		return false;
	}
	idx64 = DiffTRBIndex(addr, _commandRing.physAddr);
	if (idx64 < 0 || idx64 >= _commandRing.numTRBs - 1U) {
		IOLog("%s: bad pointer in CCE: %lld\n", __FUNCTION__, idx64);
		return false;
	}
	if (XHCI_TRB_2_ERROR_GET(trb.c) == XHCI_TRB_ERROR_CMD_RING_STOP) {
		_commandRing.stopPending = false;
		_commandRing.dequeueIndex = static_cast<uint16_t>(idx64);
		return true;
	}
	newIdx = static_cast<uint16_t>(idx64) + 1U;
	if (newIdx >= _commandRing.numTRBs - 1U)
		newIdx = 0U;
	copy = _commandRing.callbacks[idx64];
	bzero(&_commandRing.callbacks[idx64], sizeof *_commandRing.callbacks);
	_commandRing.dequeueIndex = newIdx;
	if (copy.func)
		copy.func(this, &trb, copy.param);
	return true;
}

#pragma mark -
#pragma mark Completion Routines called from DoCMDCompletion
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::_CompleteSlotCommand(GenericUSBXHCI* owner, TRBStruct* pTrb, void* param)
{
	owner->CompleteSlotCommand(pTrb, param);
}

__attribute__((visibility("hidden")))
void CLASS::CompleteSlotCommand(TRBStruct* pTrb, void* param)
{
	int32_t ret, err = static_cast<int32_t>(XHCI_TRB_2_ERROR_GET(pTrb->c));
	if (err == XHCI_TRB_ERROR_SUCCESS)
		ret = static_cast<int32_t>(XHCI_TRB_3_SLOT_GET(pTrb->d));
	else
		ret = -1000 - err;
	*static_cast<int32_t*>(param) = ret;
#if 0
	/*
	 * Called from either of
	 *   PollEventRing2 - on thread, on gate
	 *     Signalling param could be useful, but never
	 *     used, since EnqueCMD is not used standalone
	 *     with an off-gate thread waiting for the result.
	 *   PollForCMDCompletions - off thread, on gate
	 *     Signalling param is useless because this invocation
	 *     is nested within WaitForCMD which tests the value
	 *     of *param once the call returns.
	 */
	if (getWorkLoop()->onThread())
		return;
	GetCommandGate()->commandWakeup(param);
#endif
}

#if 0
/*
 * Unused
 *   This is used to handle TRB_RENESAS_GET_FW command
 *   Lower 16 bits of pTrb->c are FWVersionMajor:FWVersionMinor
 *   each field 8 bits.
 */
__attribute__((visibility("hidden")))
void CLASS::CompleteRenesasVendorCommand(TRBStruct* pTrb, void* param)
{
	int32_t ret, err = static_cast<int32_t>(XHCI_TRB_2_ERROR_GET(pTrb->c));
	if (err == XHCI_TRB_ERROR_SUCCESS)
		ret = static_cast<int32_t>(pTrb->c & UINT16_MAX);
	else
		ret = -1000 - err;
	*static_cast<int32_t*>(param) = err;
	GetCommandGate()->commandWakeup(param);
}
#endif
