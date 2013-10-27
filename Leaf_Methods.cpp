//
//  Leaf_Methods.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on December 26th, 2012.
//  Copyright (c) 2012-2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "XHCITypes.h"
#include "Async.h"
#include <IOKit/usb/IOUSBRootHubDevice.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Assorted
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::SetVendorInfo(void)
{
	OSData *vendProp, *deviceProp, *revisionProp;

	// get this chips vendID, deviceID, revisionID
	vendProp = OSDynamicCast(OSData, _device->getProperty( "vendor-id" ));
	if (vendProp)
		_vendorID = *static_cast<uint16_t const*>(vendProp->getBytesNoCopy());
	deviceProp   = OSDynamicCast(OSData, _device->getProperty( "device-id" ));
	if (deviceProp)
		_deviceID = *static_cast<uint16_t const*>(deviceProp->getBytesNoCopy());
	revisionProp = OSDynamicCast(OSData, _device->getProperty( "revision-id" ));
	if (revisionProp)
		_revisionID = *static_cast<uint16_t const*>(revisionProp->getBytesNoCopy());
}


__attribute__((visibility("hidden")))
IOUSBHubPolicyMaker* CLASS::GetHubForProtocol(uint8_t protocol)
{
	if (protocol == kUSBDeviceSpeedHigh && _rootHubDevice)
		return _rootHubDevice->GetPolicyMaker();
	if (protocol == kUSBDeviceSpeedSuper && _expansionData && _rootHubDeviceSS)
		return _rootHubDeviceSS->GetPolicyMaker();
	return 0;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::GatedGetFrameNumberWithTime(OSObject* owner, void* frameNumber, void* theTime, void*, void*)
{
	CLASS* me = static_cast<CLASS*>(owner);
	*static_cast<uint64_t*>(frameNumber) = me->_millsecondsTimers[3];
	*static_cast<uint64_t*>(theTime) = me->_millsecondsTimers[1];
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::SleepWithGateReleased(IOCommandGate* pGate, uint32_t msec)
{
	AbsoluteTime deadline;
	uint32_t event = 0U;
	clock_interval_to_deadline(msec,
							   kMillisecondScale,
							   reinterpret_cast<uint64_t*>(&deadline));
	pGate->commandSleep(&event, deadline, THREAD_ABORTSAFE);
}

__attribute__((visibility("hidden")))
void CLASS::CheckedSleep(uint32_t msec)
{
	if (_workLoop->inGate())
		SleepWithGateReleased(_commandGate, msec);
	else
		IOSleep(msec);
}

__attribute__((visibility("hidden")))
void* CLASS::getV1Ptr(intptr_t offset)
{
	if (_expansionData)
		return reinterpret_cast<uint8_t*>(_expansionData) + offset;
	return 0;
}

__attribute__((visibility("hidden")))
void* CLASS::getV3Ptr(intptr_t offset)
{
	if (_v3ExpansionData)
		return reinterpret_cast<uint8_t*>(_v3ExpansionData) + offset;
	return 0;
}

__attribute__((visibility("hidden")))
void CLASS::OverrideErrataFromProps(void)
{
	if (CHECK_FOR_MAVERICKS && (_errataBits & kErrataBrokenStreams) && !getProperty("DisableUAS"))
		setProperty("DisableUAS", kOSBooleanTrue);
	if ((_errataBits & kErrataAbsoluteEDTLA) &&
		OSDynamicCast(OSBoolean, getProperty("ASMediaEDLTAFix")) == kOSBooleanFalse)
		_errataBits &= ~kErrataAbsoluteEDTLA;
	OSBoolean* b = OSDynamicCast(OSBoolean, getProperty("UseLegacyInt"));
	if (b) {
		if (b->isTrue())
			_errataBits |= kErrataDisableMSI;
		else
			_errataBits &= ~kErrataDisableMSI;
	}
	if (_errataBits & kErrataIntelPantherPoint) {
		b = OSDynamicCast(OSBoolean, getProperty("IntelDoze"));
		if (b) {
			if (b->isTrue())
				_errataBits |= kErrataSWAssistedIdle;
			else
				_errataBits &= ~kErrataSWAssistedIdle;
		}
	}
}

#pragma mark -
#pragma mark Buffers
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::MakeBuffer(uint32_t mem_options,
						   size_t mem_capacity,
						   uint64_t mem_mask,
						   IOBufferMemoryDescriptor **p_desc,
						   void **p_virt_addr,
						   uint64_t* p_phys_addr)
{
	IOReturn rc;
	IOBufferMemoryDescriptor* md;
	void* ptr;
	uint64_t physAddr;
	IOByteCount segLength;

	md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
														  mem_options,
														  mem_capacity,
														  XHCI_HCC_AC64(_HCCLow) ? mem_mask : (mem_mask & UINT32_MAX));
	if (!md) {
		IOLog("%s: Cannot create IOBufferMemoryDescriptor\n", __FUNCTION__);
		return kIOReturnNoMemory;
	}
	rc = md->prepare();
	if (rc != kIOReturnSuccess) {
		IOLog("%s: IOMemoryDescriptor::prepare failed, error == %#x\n", __FUNCTION__, rc);
		md->release();
		return rc;
	}
	ptr = md->getBytesNoCopy();
	if (ptr)
		bzero(ptr, mem_capacity);
	physAddr = md->getPhysicalSegment(0U, &segLength, 0U);
	if (!physAddr) {
		IOLog("%s: IOMemoryDescriptor::getPhysicalSegment returned bad data\n", __FUNCTION__);
		md->complete();
		md->release();
		return kIOReturnInternalError;
	}
	*p_desc = md;
	*p_virt_addr = ptr;
	*p_phys_addr = physAddr;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::MakeBufferUnmapped(uint32_t mem_options,
								   size_t mem_capacity,
								   uint64_t mem_mask,
								   IOBufferMemoryDescriptor **p_desc,
								   uint64_t* p_phys_addr)
{
	IOReturn rc;
	IOBufferMemoryDescriptor* md;
	uint64_t physAddr;
	IOByteCount segLength;

	md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(0,
														  mem_options | kIOMemoryPageable,
														  mem_capacity,
														  XHCI_HCC_AC64(_HCCLow) ? mem_mask : (mem_mask & UINT32_MAX));
	if (!md) {
		IOLog("%s: Cannot create IOBufferMemoryDescriptor\n", __FUNCTION__);
		return kIOReturnNoMemory;
	}
	rc = md->prepare();
	if (rc != kIOReturnSuccess) {
		IOLog("%s: IOMemoryDescriptor::prepare failed, error == %#x\n", __FUNCTION__, rc);
		md->release();
		return rc;
	}
	physAddr = md->getPhysicalSegment(0U, &segLength, 0U);
	if (!physAddr) {
		IOLog("%s: IOMemoryDescriptor::getPhysicalSegment returned bad data\n", __FUNCTION__);
		md->complete();
		md->release();
		return kIOReturnInternalError;
	}
	*p_desc = md;
	*p_phys_addr = physAddr;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark xHC Capabilities
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::DecodeExtendedCapability(uint32_t hccParams)
{
	if (_errataBits & kErrataVMwarePortSwap) {
		_v3ExpansionData->_rootHubNumPortsSS = _rootHubNumPorts / 2U;
		_v3ExpansionData->_rootHubPortsSSStartRange = 1U;
		_v3ExpansionData->_rootHubNumPortsHS = _v3ExpansionData->_rootHubNumPortsSS;
		_v3ExpansionData->_rootHubPortsHSStartRange = _v3ExpansionData->_rootHubPortsSSStartRange + _v3ExpansionData->_rootHubNumPortsSS;
		return;
	}
	uint32_t ecp = XHCI_HCC_XECP(hccParams);
	if (!ecp)
		return;
	_pXHCIExtendedCapRegisters = reinterpret_cast<XHCIXECPStruct volatile*>(reinterpret_cast<uint32_t volatile*>(_pXHCICapRegisters) + ecp);
	XHCIXECPStruct volatile* iter = _pXHCIExtendedCapRegisters;
	while (true) {
		if (iter->capId == 1U)
			_pUSBLegSup = reinterpret_cast<uint32_t volatile*>(iter);
		else if (iter->capId == 2U)
			DecodeSupportedProtocol(iter);
		if (!(iter->next))
			break;
		iter = reinterpret_cast<XHCIXECPStruct volatile*>(reinterpret_cast<uint32_t volatile*>(iter) + iter->next);
	}
}

__attribute__((visibility("hidden")))
void CLASS::DecodeSupportedProtocol(XHCIXECPStruct volatile* pCap)
{
	XHCIXECPStruct_SP volatile* pSPCap = reinterpret_cast<XHCIXECPStruct_SP volatile*>(pCap);
	switch (pSPCap->revisionMajor) {
		case 3U:
			if (pSPCap->nameString != ' BSU')
				break;
			_v3ExpansionData->_rootHubNumPortsSS = pSPCap->compatiblePortCount;
			_v3ExpansionData->_rootHubPortsSSStartRange = pSPCap->compatiblePortOffset;
			break;
		case 2U:
			if (pSPCap->nameString != ' BSU')
				break;
			_v3ExpansionData->_rootHubNumPortsHS = pSPCap->compatiblePortCount;
			_v3ExpansionData->_rootHubPortsHSStartRange = pSPCap->compatiblePortOffset;
			break;
	}
}

#pragma mark -
#pragma mark xHC Operations
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::TakeOwnershipFromBios(void)
{
	uint32_t v;
	IOReturn rc;

	if (!_pUSBLegSup)
		return;
	v = Read32Reg(_pUSBLegSup);
	if (m_invalid_regspace)
		return;
	if (v & XHCI_HC_BIOS_OWNED) {
		Write32Reg(_pUSBLegSup, v | XHCI_HC_OS_OWNED);
		rc = XHCIHandshake(_pUSBLegSup, XHCI_HC_BIOS_OWNED, 0U, 100);
		if (rc == kIOReturnNoDevice)
			return;
		if (rc == kIOReturnTimeout) {
			IOLog("%s: Unable to take ownership of xHC from BIOS within 100 ms\n", __FUNCTION__);
			/*
			 * Fall through to break bios hold by disabling SMI enables
			 */
		}
	}
	v = Read32Reg(_pUSBLegSup + 1);
	if (m_invalid_regspace)
		return;
	/*
	 * Clear all SMI enables
	 */
	v &= XHCI_LEGACY_DISABLE_SMI;
	/*
	 * Clear RW1C bits
	 */
	v |= XHCI_LEGACY_SMI_EVENTS;
	Write32Reg(_pUSBLegSup + 1, v);
}

__attribute__((noinline, visibility("hidden")))
IOReturn CLASS::WaitForUSBSts(uint32_t test_mask, uint32_t test_target)
{
	return XHCIHandshake(&_pXHCIOperationalRegisters->USBSts, test_mask, test_target, 100);
}

__attribute__((noinline, visibility("hidden")))
IOReturn CLASS::XHCIHandshake(uint32_t volatile const* pReg, uint32_t test_mask, uint32_t test_target, int32_t msec)
{
	for (int32_t count = 0; count < msec; ++count) {
		if (count)
			IOSleep(1U);
		uint32_t reg = Read32Reg(pReg);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if ((reg & test_mask) == (test_target & test_mask))
			return kIOReturnSuccess;
	}
	return kIOReturnTimeout;	// Note: originally kIOReturnInternalError
}

__attribute__((visibility("hidden")))
IOReturn CLASS::ResetDevice(int32_t slot)
{
	int32_t retFromCMD;
	TRBStruct localTrb = { 0 };

	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_RESET_DEVICE, 0);
	return TranslateCommandCompletion(retFromCMD);
}

__attribute__((visibility("hidden")))
int32_t CLASS::NegotiateBandwidth(int32_t slot)
{
	TRBStruct localTrb = { 0 };

	localTrb.d |= XHCI_TRB_3_SLOT_SET(slot);
	return WaitForCMD(&localTrb, XHCI_TRB_TYPE_NEGOTIATE_BW, 0);
}

__attribute__((visibility("hidden")))
int32_t CLASS::SetLTV(uint32_t BELT)
{
	TRBStruct localTrb = { 0 };

	localTrb.d |= (BELT & 0xFFFU) << 16;
	return WaitForCMD(&localTrb, XHCI_TRB_TYPE_SET_LATENCY_TOL, 0);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::GetPortBandwidth(uint8_t HubSlot, uint8_t speed, uint8_t* pBuffer, size_t* pCount)
{
	uint8_t xspeed;
	int32_t retFromCMD;
	TRBStruct localTrb = { 0 };

	if (!pBuffer || !pCount)
		return kIOReturnBadArgument;
	if (!HubSlot && *pCount < _rootHubNumPorts) {
		*pCount = _rootHubNumPorts;
		return kIOReturnNoMemory;
	}
	if (HubSlot && *pCount < kMaxExternalHubPorts) {
		*pCount = kMaxExternalHubPorts;
		return kIOReturnNoMemory;
	}
	switch (speed) {
		case kUSBDeviceSpeedLow:
			xspeed = XDEV_LS;
			break;
		case kUSBDeviceSpeedFull:
			xspeed = XDEV_FS;
			break;
		case kUSBDeviceSpeedHigh:
			xspeed = XDEV_HS;
			break;
		case kUSBDeviceSpeedSuper:
			xspeed = XDEV_SS;
			break;
		default:
			return kIOReturnBadArgument;
	}
	localTrb.d |= XHCI_TRB_3_SLOT_SET(static_cast<uint32_t>(HubSlot));
	localTrb.d |= XHCI_TRB_3_TLBPC_SET(static_cast<uint32_t>(xspeed));
	GetInputContext();
	SetTRBAddr64(&localTrb, _inputContext.physAddr);
	retFromCMD = WaitForCMD(&localTrb, XHCI_TRB_TYPE_GET_PORT_BW, 0);
	if (retFromCMD == -1 || retFromCMD <= -1000) {
		ReleaseInputContext();
		if (retFromCMD == -1)
			return kIOReturnInternalError;
		return TranslateXHCIStatus(-1000 - retFromCMD, false, speed, false);
	}
	if (!HubSlot) {
		bcopy(reinterpret_cast<uint8_t const*>(GetInputContextPtr()) + 1, pBuffer, _rootHubNumPorts);
		*pCount = _rootHubNumPorts;
	} else {
		bcopy(reinterpret_cast<uint8_t const*>(GetInputContextPtr()) + 1, pBuffer, kMaxExternalHubPorts);
		*pCount = kMaxExternalHubPorts;
	}
	ReleaseInputContext();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Scratchpad Buffers
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::AllocScratchpadBuffers(void)
{
	IOReturn rc;
	IOBufferMemoryDescriptor* md;
	uint64_t pageMask;
	uint32_t pageSizes;

	_scratchpadBuffers.md = 0;
	if (!_scratchpadBuffers.max)
		return kIOReturnSuccess;
	pageSizes = Read32Reg(&_pXHCIOperationalRegisters->PageSize);
	if (m_invalid_regspace) {
		IOLog("%s: Invalid regspace\n", __FUNCTION__);
		return kIOReturnNoDevice;
	}
	pageSizes <<= 12;
	if (!pageSizes) {
		IOLog("%s: PageSize register invalid value of zero\n", __FUNCTION__);
		return kIOReturnDeviceError;
	}
	pageSizes &= -pageSizes;	// leave just smallest size
	_scratchpadBuffers.mdGC = OSArray::withCapacity(_scratchpadBuffers.max);
	if (!_scratchpadBuffers.mdGC) {
		IOLog("%s: OSArray::withCapacity failed\n", __FUNCTION__);
		return kIOReturnNoMemory;
	}
	rc = MakeBuffer(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
					static_cast<size_t>(_scratchpadBuffers.max) * sizeof *_scratchpadBuffers.ptr,
					-PAGE_SIZE,
					&_scratchpadBuffers.md,
					reinterpret_cast<void**>(&_scratchpadBuffers.ptr),
					&_scratchpadBuffers.physAddr);
	if (rc != kIOReturnSuccess) {
		IOLog("%s: MakeBuffer(1) failed, error == %#x\n", __FUNCTION__, rc);
		return rc;
	}
	pageMask = -static_cast<int64_t>(pageSizes);
	for (uint32_t spbufs = 0U; spbufs < _scratchpadBuffers.max; ++spbufs) {
		rc = MakeBufferUnmapped(kIOMemoryPhysicallyContiguous | kIODirectionInOut,
								pageSizes,
								pageMask,
								&md,
								&_scratchpadBuffers.ptr[spbufs]);
		if (rc != kIOReturnSuccess) {
			IOLog("%s: MakeBuffer(2) failed, error == %#x\n", __FUNCTION__, rc);
			return rc;
		}
		_scratchpadBuffers.mdGC->setObject(md);	// Note: ignores error
		md->release();
	}
	SetDCBAAAddr64(_dcbaa.ptr, _scratchpadBuffers.physAddr);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::FinalizeScratchpadBuffers(void)
{
	if (_scratchpadBuffers.mdGC) {
		uint32_t l = _scratchpadBuffers.mdGC->getCount();
		for (uint32_t i = 0U; i < l; ++i)
			static_cast<IOGeneralMemoryDescriptor*>(_scratchpadBuffers.mdGC->getObject(i))->complete();
		_scratchpadBuffers.mdGC->flushCollection();
		_scratchpadBuffers.mdGC->release();
		_scratchpadBuffers.mdGC = 0;
	}
	if (_scratchpadBuffers.md) {
		_scratchpadBuffers.md->complete();
		_scratchpadBuffers.md->release();
		_scratchpadBuffers.md = 0;
	}
}

#pragma mark -
#pragma mark Input Context
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::GetInputContext(void)
{
	++_inputContext.refCount;
	bzero(GetInputContextPtr(), GetInputContextSize());
}

__attribute__((visibility("hidden")))
void CLASS::ReleaseInputContext(void)
{
	if (_inputContext.refCount)
		--_inputContext.refCount;
}

__attribute__((visibility("hidden")))
uint32_t CLASS::GetInputContextSize(void)
{
	return 33U * (XHCI_HCC_CSZ(_HCCLow) ? 2U : 1U) * sizeof(ContextStruct);
}

__attribute__((visibility("hidden")))
uint32_t CLASS::GetDeviceContextSize(void)
{
	return 32U * (XHCI_HCC_CSZ(_HCCLow) ? 2U : 1U) * sizeof(ContextStruct);
}

#pragma mark -
#pragma mark XHCI Status
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::TranslateXHCIStatus(int32_t xhci_err, bool direction, uint8_t speed, bool)
{
	switch (xhci_err) {
		case XHCI_TRB_ERROR_SUCCESS:
		case XHCI_TRB_ERROR_SHORT_PKT:
			return kIOReturnSuccess;
		case XHCI_TRB_ERROR_DATA_BUF:
			return kIOUSBBufferOverrunErr | (direction ? 0U : 1U);
		case XHCI_TRB_ERROR_BABBLE:
		case XHCI_TRB_ERROR_RING_OVERRUN:
		case XHCI_TRB_ERROR_VF_RING_FULL:
		case XHCI_TRB_ERROR_EV_RING_FULL:
		case XHCI_TRB_ERROR_MISSED_SERVICE:
		case XHCI_TRB_ERROR_ISOC_OVERRUN:
		case XHCI_TRB_ERROR_EVENT_LOST:
			return kIOReturnOverrun;
		case XHCI_TRB_ERROR_XACT:
			return speed < kUSBDeviceSpeedHigh ? kIOUSBHighSpeedSplitError : kIOReturnNotResponding;
		case XHCI_TRB_ERROR_STALL:
			return kIOUSBPipeStalled;
		case XHCI_TRB_ERROR_RESOURCE:
			return kIOReturnNoResources;
		case XHCI_TRB_ERROR_BANDWIDTH:
		case XHCI_TRB_ERROR_BW_OVERRUN:
		case XHCI_TRB_ERROR_SEC_BW:
			return kIOReturnNoBandwidth;
		case XHCI_TRB_ERROR_NO_SLOTS:
			return kIOUSBDeviceCountExceeded;
		case XHCI_TRB_ERROR_STREAM_TYPE:
			return kIOReturnInvalid;
		case XHCI_TRB_ERROR_SLOT_NOT_ON:
		case XHCI_TRB_ERROR_ENDP_NOT_ON:
		case XHCI_TRB_ERROR_CMD_RING_STOP:
			return kIOReturnOffline;
		case XHCI_TRB_ERROR_RING_UNDERRUN:
			return kIOReturnUnderrun;
		case XHCI_TRB_ERROR_PARAMETER:
		case XHCI_TRB_ERROR_CONTEXT_STATE:
		case XHCI_TRB_ERROR_BAD_MELAT:
		case XHCI_TRB_ERROR_INVALID_SID:
			return kIOReturnBadArgument;
		case XHCI_TRB_ERROR_NO_PING_RESP:
			return kIOReturnNotResponding;
		case XHCI_TRB_ERROR_INCOMPAT_DEV:
			return kIOReturnDeviceError;
		case XHCI_TRB_ERROR_CMD_ABORTED:
		case XHCI_TRB_ERROR_STOPPED:
		case XHCI_TRB_ERROR_LENGTH:
			return kIOReturnAborted;
		case XHCI_TRB_ERROR_SPLIT_XACT:
			return kIOUSBHighSpeedSplitError;
		case 193:	// Intel FORCE_HDR_USB2_NO_SUPPORT
			return _vendorID == kVendorIntel ? kIOReturnUnsupported : kIOReturnInternalError;
		case 199:	// Intel CMPL_WITH_EMPTY_CONTEXT
			return _vendorID == kVendorIntel ? kIOReturnNotOpen : kIOReturnInternalError;
		case 200:	// Intel VENDOR_CMD_FAILED
			return _vendorID == kVendorIntel ? kIOReturnNoBandwidth : kIOReturnInternalError;
		default:
			return kIOReturnInternalError;
	}
}

__attribute__((visibility("hidden")))
IOReturn CLASS::TranslateCommandCompletion(int32_t code)
{
	if (code == -1)
		return kIOReturnTimeout;
	if (code > -1000)
		return kIOReturnSuccess;
	if (code == -1000 - XHCI_TRB_ERROR_CONTEXT_STATE)
		return kIOReturnNotPermitted;
	if (code == -1000 - XHCI_TRB_ERROR_PARAMETER)
		return kIOReturnBadArgument;
	return kIOReturnInternalError;
}
