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
#pragma mark Helper
#pragma mark -

template<typename T>
static
IOReturn WaitForChangeEvent(T volatile const* pEvent, T start_value)
{
	uint64_t deadline;

	if (!(*pEvent == start_value))
		return kIOReturnSuccess;
	clock_interval_to_deadline(100, kMillisecondScale, &deadline);
	do {
		if (assert_wait_deadline(const_cast<T*>(pEvent), THREAD_ABORTSAFE, deadline) != THREAD_WAITING)
			return kIOReturnNoResources;
		if (!(*pEvent == start_value)) {
			thread_wakeup_prim(const_cast<T*>(pEvent), FALSE, THREAD_AWAKENED);
			break;
		}
		switch (thread_block(THREAD_CONTINUE_NULL)) {
			case THREAD_AWAKENED:
			case THREAD_NOT_WAITING:
				break;
			case THREAD_TIMED_OUT:
				return kIOReturnTimeout;
			default:
				return kIOReturnAborted;
		}
	} while (*pEvent == start_value);
	return kIOReturnSuccess;
}

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
IOReturn CLASS::CommandStop(void)
{
	uint32_t lowCRCr = Read32Reg(reinterpret_cast<uint32_t volatile const*>(&_pXHCIOperationalRegisters->CRCr));
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (!(lowCRCr & static_cast<uint32_t>(XHCI_CRCR_LO_CRR)))
		return kIOReturnSuccess;
	_commandRing.stopPending = true;
	Write32Reg(reinterpret_cast<uint32_t volatile*>(&_pXHCIOperationalRegisters->CRCr), static_cast<uint32_t>(XHCI_CRCR_LO_CS));
	WaitForChangeEvent<bool>(&_commandRing.stopPending, true);
	if (_commandRing.stopPending) {
		_commandRing.stopPending = false;
		IOLog("%s: Timeout waiting for command ring to stop, 100ms\n", __FUNCTION__);
		return kIOReturnTimeout;
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::CommandAbort(void)
{
	uint32_t lowCRCr = Read32Reg(reinterpret_cast<uint32_t volatile const*>(&_pXHCIOperationalRegisters->CRCr));
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (!(lowCRCr & static_cast<uint32_t>(XHCI_CRCR_LO_CRR)))
		return kIOReturnSuccess;
	_commandRing.stopPending = true;
	Write32Reg(reinterpret_cast<uint32_t volatile*>(&_pXHCIOperationalRegisters->CRCr), static_cast<uint32_t>(XHCI_CRCR_LO_CA));
	WaitForChangeEvent<bool>(&_commandRing.stopPending, true);
	if (_commandRing.stopPending) {
		IOLog("%s: Timeout waiting for command to abort, 100ms\n", __FUNCTION__);
		_commandRing.stopPending = false;
		return kIOReturnTimeout;
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
int32_t CLASS::WaitForCMD(TRBStruct* trb, int32_t trbType, TRBCallback callback)
{
	int32_t prevIndex;
	int32_t volatile ret = -1;
	uint32_t sts = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	if (m_invalid_regspace)
		return ret;
	if ((sts & XHCI_STS_HSE) && !_HSEDetected) {
		IOLog("%s: HSE bit set:%#x (1)\n", __FUNCTION__, sts);
		_HSEDetected = true;
	}
	if (isInactive() || !_controllerAvailable)
		return ret;
	prevIndex = _commandRing.enqueueIndex;
	if (EnqueCMD(trb, trbType, callback ? : CompleteSlotCommand, const_cast<int32_t*>(&ret)) != kIOReturnSuccess)
		return ret;
	WaitForChangeEvent<int32_t>(&ret, -1);
	/*
	 * Note: Scoop up stop TRBs
	 */
	if (trbType == XHCI_TRB_TYPE_STOP_EP)
		PollForCMDCompletions(0);
	if (ret != -1)
		return ret;
	IOLog("%s: Timeout waiting for command completion (opcode %#x), 100ms\n", __FUNCTION__, static_cast<uint32_t>(trbType));
	CommandAbort();
	if (ret == -1) {
		/*
		 * If command was still not completed, wipe its callback, so &ret
		 *   does not get overrun.
		 */
		_commandRing.callbacks[prevIndex].func = 0;
		_commandRing.callbacks[prevIndex].param = 0;
	}
	return ret;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::EnqueCMD(TRBStruct* trb, int32_t trbType, TRBCallback callback, int32_t* param)
{
	TRBStruct* target;
	uint32_t fourth;
	int32_t next = _commandRing.enqueueIndex;
	if (next < static_cast<int32_t>(_commandRing.numTRBs) - 2)
		++next;
	else
		next = 0;
	if (next == _commandRing.dequeueIndex) {
		IOLog("%s: Ring full, enq:%u, deq:%u\n", __FUNCTION__, _commandRing.enqueueIndex, _commandRing.dequeueIndex);
		return kIOReturnNoResources;
	}
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
		if (!ml_at_interrupt_context())
			IOLog("%s: Zero pointer in CCE\n", __FUNCTION__);
		return false;
	}
	idx64 = DiffTRBIndex(addr, _commandRing.physAddr);
	if (idx64 < 0 || idx64 >= _commandRing.numTRBs - 1U) {
		if (!ml_at_interrupt_context())
			IOLog("%s: bad pointer in CCE: %lld\n", __FUNCTION__, idx64);
		return false;
	}
	if (XHCI_TRB_2_ERROR_GET(trb.c) == XHCI_TRB_ERROR_CMD_RING_STOP) {
		_commandRing.dequeueIndex = static_cast<uint16_t>(idx64);
		_commandRing.stopPending = false;
		if (ml_at_interrupt_context())
			thread_wakeup_prim(const_cast<bool*>(&_commandRing.stopPending), FALSE, THREAD_AWAKENED);
		return true;
	}
	newIdx = static_cast<uint16_t>(idx64) + 1U;
	if (newIdx >= _commandRing.numTRBs - 1U)
		newIdx = 0U;
	copy = _commandRing.callbacks[idx64];
	_commandRing.callbacks[idx64].func = 0;
	_commandRing.callbacks[idx64].param = 0;
	_commandRing.dequeueIndex = newIdx;
	if (copy.func)
		copy.func(this, &trb, copy.param);
	return true;
}

#pragma mark -
#pragma mark Completion Routines called from DoCMDCompletion
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::CompleteSlotCommand(CLASS*, TRBStruct* pTrb, int32_t* param)
{
	int32_t ret, err = static_cast<int32_t>(XHCI_TRB_2_ERROR_GET(pTrb->c));
	if (err == XHCI_TRB_ERROR_SUCCESS)
		ret = static_cast<int32_t>(XHCI_TRB_3_SLOT_GET(pTrb->d));
	else
		ret = -1000 - err;
	*param = ret;
	if (ml_at_interrupt_context())
		thread_wakeup_prim(param, FALSE, THREAD_AWAKENED);
}

#if 0
/*
 * Note: Unused
 *   This is used to handle TRB_RENESAS_GET_FW command
 *   Lower 16 bits of pTrb->c are FWVersionMajor:FWVersionMinor
 *   each field 8 bits.
 */
__attribute__((visibility("hidden")))
void CLASS::CompleteRenesasVendorCommand(CLASS*, TRBStruct* pTrb, int32_t* param)
{
	int32_t ret, err = static_cast<int32_t>(XHCI_TRB_2_ERROR_GET(pTrb->c));
	if (err == XHCI_TRB_ERROR_SUCCESS)
		ret = static_cast<int32_t>(pTrb->c & UINT16_MAX);
	else
		ret = -1000 - err;	// Note: originally 1000 + err
	*param = ret;
	if (ml_at_interrupt_context())
		thread_wakeup_prim(param, FALSE, THREAD_AWAKENED);
}
#endif
