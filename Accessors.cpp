//
//  Accessors.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 12th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "XHCITypes.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Accessors
#pragma mark -

#pragma mark -
#pragma mark Register Space
#pragma mark -

__attribute__((noinline, visibility("hidden")))
uint8_t CLASS::Read8Reg(uint8_t volatile const* p)
{
	uint8_t v;
	if (m_invalid_regspace)
		return UINT8_MAX;
	v = *p;
	if (v != UINT8_MAX)
		return v;
	if (_v3ExpansionData->_onThunderbolt)
		m_invalid_regspace = true;
	return UINT8_MAX;
}

__attribute__((noinline, visibility("hidden")))
uint16_t CLASS::Read16Reg(uint16_t volatile const* p)
{
	uint16_t v;
	if (m_invalid_regspace)
		return UINT16_MAX;
	v = *p;
	if (v != UINT16_MAX)
		return v;
	if (_v3ExpansionData->_onThunderbolt)
		m_invalid_regspace = true;
	return UINT16_MAX;
}

__attribute__((noinline, visibility("hidden")))
uint32_t CLASS::Read32Reg(uint32_t volatile const* p)
{
	uint32_t v;
	if (m_invalid_regspace)
		return UINT32_MAX;
	v = *p;
	if (v != UINT32_MAX)
		return v;
	if (_v3ExpansionData->_onThunderbolt)
		m_invalid_regspace = true;
	return UINT32_MAX;
}

__attribute__((noinline, visibility("hidden")))
uint64_t CLASS::Read64Reg(uint64_t volatile const* p)
{
	uint64_t v;
	uint32_t lowv, highv;
	if (m_invalid_regspace)
		return UINT64_MAX;
	if (XHCI_HCC_AC64(_HCCLow)) {
#if __LP64__
		if (_vendorID == kVendorFrescoLogic) {
#endif
			lowv = *reinterpret_cast<uint32_t volatile const*>(p);
			if (lowv == UINT32_MAX && _v3ExpansionData->_onThunderbolt)
				goto scrap;
			highv = reinterpret_cast<uint32_t volatile const*>(p)[1];
			if (highv == UINT32_MAX && _v3ExpansionData->_onThunderbolt)
				goto scrap;
			return (static_cast<uint64_t>(highv) << 32) | lowv;
#if __LP64__
		} else {
			v = *p;
			if (v == UINT64_MAX && _v3ExpansionData->_onThunderbolt)
				goto scrap;
			return v;
		}
#endif
	} else {
		lowv = *reinterpret_cast<uint32_t volatile const*>(p);
		if (lowv == UINT32_MAX && _v3ExpansionData->_onThunderbolt)
			goto scrap;
		return lowv;
	}
scrap:
	m_invalid_regspace = true;
	return UINT64_MAX;
}

__attribute__((noinline, visibility("hidden")))
void CLASS::Write32Reg(uint32_t volatile* p, uint32_t v)
{
	if (m_invalid_regspace)
		return;
	*p = v;
}

__attribute__((noinline, visibility("hidden")))
void CLASS::Write64Reg(uint64_t volatile* p, uint64_t v, bool)
{
	if (m_invalid_regspace)
		return;
	if (XHCI_HCC_AC64(_HCCLow)) {
#if __LP64__
		if (_vendorID == kVendorFrescoLogic) {
#endif
			*reinterpret_cast<uint32_t volatile*>(p) = static_cast<uint32_t>(v);
			reinterpret_cast<uint32_t volatile*>(p)[1] = static_cast<uint32_t>(v >> 32);
#if __LP64__
		} else
			*p = v;
#endif
	} else {
		*reinterpret_cast<uint32_t volatile*>(p) = static_cast<uint32_t>(v);
		reinterpret_cast<uint32_t volatile*>(p)[1] = 0U;
	}
}

#pragma mark -
#pragma mark Contexts
#pragma mark -

__attribute__((noinline, visibility("hidden")))
ContextStruct* CLASS::GetSlotContext(int32_t slot, int32_t index)
{
	ContextStruct* pContext = SlotPtr(slot)->ctx;
	if (!pContext || index <= 0)
		return pContext;
	return XHCI_HCC_CSZ(_HCCLow) ? (pContext + 2 * index) : (pContext + index);
}

__attribute__((noinline, visibility("hidden")))
ContextStruct* CLASS::GetInputContextPtr(int32_t index)
{
	return XHCI_HCC_CSZ(_HCCLow) ? (_inputContext.ptr + 2 * index) : (_inputContext.ptr + index);
}

__attribute__((visibility("hidden")))
uint8_t CLASS::GetSlCtxSpeed(ContextStruct const* pContext)
{
	switch (XHCI_SCTX_0_SPEED_GET(pContext->_s.dwSctx0)) {
		case XDEV_FS:
			return kUSBDeviceSpeedFull;
		case XDEV_LS:
			return kUSBDeviceSpeedLow;
		case XDEV_HS:
			return kUSBDeviceSpeedHigh;
		default: // (XDEV_SS)
			return kUSBDeviceSpeedSuper;
	}
}

__attribute__((visibility("hidden")))
void CLASS::SetSlCtxSpeed(ContextStruct* pContext, uint32_t speed)
{
	pContext->_s.dwSctx0 |= XHCI_SCTX_0_SPEED_SET(speed);
}

#pragma mark -
#pragma mark TRB
#pragma mark -

__attribute__((noinline, visibility("hidden")))
void CLASS::SetTRBAddr64(TRBStruct* trb, uint64_t addr)
{
	trb->a = static_cast<uint32_t>(addr);
	trb->b = static_cast<uint32_t>(addr >> 32);
}

__attribute__((visibility("hidden")))
void CLASS::ClearTRB(TRBStruct* trb, bool wipeCycleBit)
{
	trb->a = 0U;
	trb->b = 0U;
	trb->c = 0U;
	if (wipeCycleBit)
		trb->d = 0U;
	else
		trb->d &= XHCI_TRB_3_CYCLE_BIT;
}

#pragma mark -
#pragma mark DCBAA
#pragma mark -

__attribute__((noinline, visibility("hidden")))
void CLASS::SetDCBAAAddr64(uint64_t* p, uint64_t addr)
{
	*reinterpret_cast<uint32_t*>(p) = static_cast<uint32_t>(addr) & ~0x3FU;
	reinterpret_cast<uint32_t*>(p)[1] = static_cast<uint32_t>(addr >> 32);
}
