//
//  RootHub.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 21st 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#ifdef DEBOUNCING
#include "XHCITypes.h"
#endif

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Root Hub
#pragma mark -


#pragma mark -
#pragma mark XHCI Feature Methods
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubPowerPort(uint16_t port, bool state)
{
	uint32_t volatile* pPortSC;
	uint32_t portSC;

	if (_rhPortBeingReset[port])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	pPortSC = &_pXHCIOperationalRegisters->prs[port].PortSC;
	if (state)
		portSC |= XHCI_PS_PP;
	else {
		/*
		 * Clear any pending change flags while powering down port.
		 */
		portSC |= XHCI_PS_CHANGEBITS;
		portSC &= ~(XHCI_PS_PP | XHCI_PS_WAKEBITS);
	}
	Write32Reg(pPortSC, portSC);
	XHCIHandshake(pPortSC, XHCI_PS_PP, portSC, 10);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubEnablePort(uint16_t port, bool state)
{
	uint32_t portSC;

	if (state)
		return kIOReturnUnsupported;
	if (_rhPortBeingReset[port])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	/*
	 * Clear any pending change flags while disabling port.
	 */
	portSC |= XHCI_PS_CHANGEBITS;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC | XHCI_PS_PED);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubSetLinkStatePort(uint8_t linkState, uint16_t port)
{
	uint32_t portSC;

	/*
	 * Note: This method only called on SuperSpeed ports
	 *   (see SetRootHubPortFeature)
	 */
	switch (linkState) {
		case kSSHubPortLinkStateU0:
			return XHCIRootHubSuspendPort(kUSBDeviceSpeedSuper, port, false);
		case kSSHubPortLinkStateU3:
			return XHCIRootHubSuspendPort(kUSBDeviceSpeedSuper, port, true);
	}
	if (_rhPortBeingReset[port])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	/*
	 * Clear any stray PLC when changing state
	 */
	Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC |
			   XHCI_PS_LWS | XHCI_PS_PLS_SET(linkState) | XHCI_PS_PLC);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubWarmResetPort(uint16_t port)
{
	uint32_t portSC;

	if (_rhPortBeingReset[port])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC | XHCI_PS_WPR);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubResetPort(uint8_t protocol, uint16_t port)
{
	IOReturn rc;
#ifdef LONG_RESET
	uint32_t recCount;
#endif

	if (_rhPortBeingReset[port])
		return kIOReturnSuccess;
	_rhPortBeingReset[port] = true;
#ifdef LONG_RESET
	for (recCount = 0U; _workLoop->inGate();) {
		++recCount;
		_workLoop->OpenGate();
	}
#endif
	rc = RHResetPort(protocol, port);
#ifdef LONG_RESET
	while (recCount--)
		_workLoop->CloseGate();
#endif
	_rhPortBeingReset[port] = false;
	return rc;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubSuspendPort(uint8_t protocol, uint16_t port, bool state)
{
	uint64_t deadline;
	uint32_t volatile* pPortSC;
	uint32_t portSC, count;

	if (_rhPortBeingResumed[port] && !state)
		return kIOReturnSuccess;
	if (_rhPortBeingReset[port])
		return kIOReturnNotPermitted;
	pPortSC = &_pXHCIOperationalRegisters->prs[port].PortSC;
	if (state) {
		for (count = 0U; count < 3U; ++count) {
			if (count)
				IOSleep(50U);
			portSC = Read32Reg(pPortSC);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			if (XHCI_PS_PLS_GET(portSC) != XDEV_RECOVERY)
				break;
		}
		if (count >= 3U)
			return kIOUSBDevicePortWasNotSuspended;
	}
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	if (state)
		portSC |= XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U3);
	else if (protocol == kUSBDeviceSpeedSuper)
		portSC |= XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U0);
	else if (protocol == kUSBDeviceSpeedHigh)
		portSC |= XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_RESUME);
	/*
	 * Clear any stray PLC when changing state
	 */
	Write32Reg(pPortSC, portSC | XHCI_PS_PLC);
	if (state)
		CheckedSleep(1U);	// Note: xHC may need up to 1 frame to being suspend, AppleUSBHubPort::Suspend waits another 10msec for suspension to complete
	else {
		_rhPortBeingResumed[port] = true;
		if (protocol != kUSBDeviceSpeedSuper &&
			_rhResumePortTimerThread[port]) {
			clock_interval_to_deadline(20U, kMillisecondScale, &deadline);
			thread_call_enter1_delayed(_rhResumePortTimerThread[port],
									   reinterpret_cast<thread_call_param_t>(static_cast<size_t>(port)),
									   deadline);
		}
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubClearPortConnectionChange(uint16_t port)
{
	IOReturn rc = XHCIRootHubClearPortChangeBit(port, XHCI_PS_CSC);
	if (_rhPortEmulateCSC[port])
		_rhPortEmulateCSC[port] = false;
#ifdef DEBOUNCING
	_rhPortDebouncing[port] = false;
	_rhPortDebounceADisconnect[port] = false;
#endif
	return rc;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubClearPortChangeBit(uint16_t port, uint32_t bitMask)
{
	uint32_t portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC | bitMask);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark RH Port Reset
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::RHResetPort(uint8_t protocol, uint16_t port)
{
	uint32_t portSC;
#ifdef LONG_RESET
	uint32_t companionPortSC, companionPLSAfterReset, count;
	uint16_t companionPort;
#endif

	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC | XHCI_PS_PR);
#ifdef LONG_RESET
	if (!(_errataBits & kErrataIntelPortMuxing) ||
		(gUSBStackDebugFlags & kUSBDisableMuxedPortsMask))
		return kIOReturnSuccess;
	for (count = 0U; count < 8U; ++count) {
		if (count)
			IOSleep(32U);
		portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if (portSC & XHCI_PS_PRC)
			break;
	}
	if (XHCI_PS_SPEED_GET(portSC) == XDEV_SS)
		return kIOReturnSuccess;
	IOSleep(500U - count * 32U);
	companionPort = GetCompanionRootPort(protocol, port);
	if (companionPort == UINT16_MAX)
		return kIOReturnSuccess;
	companionPortSC = Read32Reg(&_pXHCIOperationalRegisters->prs[companionPort].PortSC);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	companionPLSAfterReset = XHCI_PS_PLS_GET(companionPortSC);
	if (XHCI_PS_SPEED_GET(portSC) == XDEV_HS &&
		(portSC & XHCI_PS_CCS) &&
		!(companionPortSC & (XHCI_PS_CCS | XHCI_PS_CEC | XHCI_PS_CAS | XHCI_PS_PLC | XHCI_PS_WRC)) &&
		companionPLSAfterReset == XDEV_RXDETECT) {
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_PRC);
		HCSelect(static_cast<uint8_t>(port), 0U);
		for (count = 0U; count < 8U; ++count) {
			if (count)
				IOSleep(32U);
			portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			if (portSC & XHCI_PS_CSC)
				break;
		}
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_CSC);
		return kIOUSBDeviceTransferredToCompanion;
	}
	if (!_inTestMode && companionPLSAfterReset == XDEV_COMPLIANCE) {
		XHCIRootHubWarmResetPort(companionPort);
		for (count = 0U; count < 8U; ++count) {
			if (count)
				IOSleep(32U);
			companionPortSC = Read32Reg(&_pXHCIOperationalRegisters->prs[companionPort].PortSC);
			if (m_invalid_regspace)
				return kIOReturnNoDevice;
			if (companionPortSC & (XHCI_PS_PRC | XHCI_PS_WRC))
				break;
		}
		if (companionPortSC & (XHCI_PS_PRC | XHCI_PS_WRC))
			Write32Reg(&_pXHCIOperationalRegisters->prs[companionPort].PortSC,
					   (companionPortSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_PRC | XHCI_PS_WRC);
	}
#endif
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark RH Port Resume
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::RHResumePortTimer(uint32_t port)
{
	/*
	 * Note: For a USB2 port, this handler should be trigerred with
	 *   a 20ms delay from discovery of XDEV_RESUME.
	 */
	IOCommandGate* commandGate = _commandGate;
	if (!commandGate)
		return;
	/*
	 * Skip this if initiated from RestoreControllerStateFromSleep
	 */
	if (_controllerAvailable)
		EnsureUsability();
	commandGate->runAction(RHResumePortCompletionEntry,
						   reinterpret_cast<void*>(static_cast<size_t>(port)));
}

__attribute__((visibility("hidden")))
IOReturn CLASS::RHResumePortCompletionEntry(OSObject *owner,
											void *arg0,
											void *arg1,
											void *arg2,
											void *arg3)
{
	CLASS* me = OSDynamicCast(CLASS, owner);
	if (!me)
		return kIOReturnInternalError;
	return me->RHResumePortCompletion(static_cast<uint32_t>(reinterpret_cast<size_t>(arg0)));
}

__attribute__((visibility("hidden")))
IOReturn CLASS::RHResumePortCompletion(uint32_t port)
{
	uint32_t portSC;

	if (!_rhPortBeingResumed[port])
		return kIOReturnInternalError;
	if (m_invalid_regspace /* || !_controllerAvailable */) {
		_rhPortBeingResumed[port] = false;
		return kIOReturnNoDevice;
	}
	portSC = GetPortSCForWriting(static_cast<uint16_t>(port));
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, portSC | XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U0) | XHCI_PS_PLC);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::RHCompleteResumeOnAllPorts(void)
{
	uint32_t wait_val;

	if (m_invalid_regspace || !_controllerAvailable) {
		for (uint8_t port = 0U; port < _rootHubNumPorts; ++port)
			if (_rhPortBeingResumed[port])
				_rhPortBeingResumed[port] = false;
		return kIOReturnInternalError;
	}
	wait_val = 0U;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		if (!_rhPortBeingResumed[port])
			continue;
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC,
				   GetPortSCForWriting(port) | XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U0) | XHCI_PS_PLC);
		wait_val = 2U;
	}
	if (wait_val)
		IOSleep(wait_val);
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port)
		if (_rhPortBeingResumed[port])
			_rhPortBeingResumed[port] = false;
	return kIOReturnSuccess;
}

/*
 * Returns true if it clears PLC
 */
__attribute__((visibility("hidden")))
bool CLASS::RHCheckForPortResume(uint16_t port, uint8_t protocol, uint32_t havePortSC)
{
	uint64_t deadline;
	uint32_t portSC;

	if (havePortSC == UINT32_MAX) {
		portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
		if (m_invalid_regspace)
			return false;
	} else
		portSC = havePortSC;
	if (_rhPortBeingResumed[port]) {
		/*
		 * Check if port has reached U0 state.
		 */
		if (XHCI_PS_PLS_GET(portSC) == XDEV_U0)
			_rhPortBeingResumed[port] = false;
		return false;
	}
	if (XHCI_PS_PLS_GET(portSC) != XDEV_RESUME)
		return false;
	/*
	 * Device-initiated resume
	 */
	_rhPortBeingResumed[port] = true;
	if (protocol == kUSBDeviceSpeedSuper) {
		Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC,
				   (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U0) | XHCI_PS_PLC);
		return true;
	}
	Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_PLC);
	if (_rhResumePortTimerThread[port]) {
		clock_interval_to_deadline(20U, kMillisecondScale, &deadline);
		thread_call_enter1_delayed(_rhResumePortTimerThread[port],
								   reinterpret_cast<thread_call_param_t>(static_cast<size_t>(port)),
								   deadline);
	}
	return true;
}

__attribute__((visibility("hidden")))
void CLASS::RHCheckForPortResumes(void)
{
	uint8_t protocol;

	if (m_invalid_regspace)
		return;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		protocol = kUSBDeviceSpeedHigh;
		PortNumberCanonicalToProtocol(port, &protocol);
		RHCheckForPortResume(port, protocol, UINT32_MAX);
		if (m_invalid_regspace)
			return;
	}
}

#pragma mark -
#pragma mark RH Port Misc
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::RHClearUnserviceablePorts(void)
{
	uint16_t mask = static_cast<uint16_t>(_rhPortStatusChangeBitmapGated >> 16);
	uint8_t port = 15U;
	for (; mask; mask >>= 1, ++port)
		if (mask & 1U) {
			uint32_t portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
			if (m_invalid_regspace)
				break;
			if (portSC & XHCI_PS_CHANGEBITS)
				Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_CHANGEBITS);
		}
	_rhPortStatusChangeBitmapGated &= UINT16_MAX;
}

#pragma mark -
#pragma mark RH Port Thread Calls
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::RHResumePortTimerEntry(OSObject* owner, void* param)
{
	CLASS* me = OSDynamicCast(CLASS, owner);
	if (!me)
		return;
	me->RHResumePortTimer(static_cast<uint32_t>(reinterpret_cast<size_t>(param)));
}

#pragma mark -
#pragma mark Port Accessors/Numbering
#pragma mark -

__attribute__((noinline, visibility("hidden")))
uint32_t CLASS::GetPortSCForWriting(uint16_t port)
{
	uint32_t portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
	if (m_invalid_regspace)
		return portSC;
	/*
	 * Note: all bits that aren't RW-1-commands
	 */
	return portSC & (XHCI_PS_DR | XHCI_PS_WAKEBITS |
					 XHCI_PS_CAS | XHCI_PS_PIC_SET(3U) | (15U << 10) /* Speed */ | XHCI_PS_PP |
					 XHCI_PS_OCA | XHCI_PS_CCS);
}

__attribute__((noinline, visibility("hidden")))
uint16_t CLASS::PortNumberCanonicalToProtocol(uint16_t canonical, uint8_t* pProtocol)
{
	if (_errataBits & kErrataVMwarePortSwap)
		canonical = canonical / 2U + ((canonical & 1U) ? _v3ExpansionData->_rootHubNumPortsSS : 0U);
	if (canonical + 1U >= _v3ExpansionData->_rootHubPortsSSStartRange &&
		canonical + 1U < _v3ExpansionData->_rootHubPortsSSStartRange + _v3ExpansionData->_rootHubNumPortsSS) {
		if (pProtocol)
			*pProtocol = kUSBDeviceSpeedSuper;
		return canonical - _v3ExpansionData->_rootHubPortsSSStartRange + 2U;
	}
	if (canonical + 1U >= _v3ExpansionData->_rootHubPortsHSStartRange &&
		canonical + 1U < _v3ExpansionData->_rootHubPortsHSStartRange + _v3ExpansionData->_rootHubNumPortsHS) {
		if (pProtocol)
			*pProtocol = kUSBDeviceSpeedHigh;
		return canonical - _v3ExpansionData->_rootHubPortsHSStartRange + 2U;
	}
	return 0U;
}

__attribute__((noinline, visibility("hidden")))
uint16_t CLASS::PortNumberProtocolToCanonical(uint16_t port, uint8_t protocol)
{
	switch ((protocol & kUSBSpeed_Mask) >> kUSBSpeed_Shift) {
		case kUSBDeviceSpeedSuper:
			if (port && port <= _v3ExpansionData->_rootHubNumPortsSS) {
				if (_errataBits & kErrataVMwarePortSwap)
					return port * 2U - 2U;
				return port + _v3ExpansionData->_rootHubPortsSSStartRange - 2U;
			}
			break;
		case kUSBDeviceSpeedHigh:
			if (port && port <= _v3ExpansionData->_rootHubNumPortsHS) {
				if (_errataBits & kErrataVMwarePortSwap)
					return port * 2U - 1U;
				return port + _v3ExpansionData->_rootHubPortsHSStartRange - 2U;
			}
			break;
	}
	return UINT16_MAX;
}

__attribute__((visibility("hidden")))
uint16_t CLASS::GetCompanionRootPort(uint8_t protocol, uint16_t port)
{
	if (_errataBits & kErrataVMwarePortSwap)
		return port ^ 1U;
	if (protocol == kUSBDeviceSpeedHigh) {
		port -= _v3ExpansionData->_rootHubPortsHSStartRange;
		if (port + 1U >= _v3ExpansionData->_rootHubNumPortsSS)
			return UINT16_MAX;
		return port + _v3ExpansionData->_rootHubPortsSSStartRange;
	}
	if (protocol == kUSBDeviceSpeedSuper) {
		port -= _v3ExpansionData->_rootHubPortsSSStartRange;
		if (port + 1U >= _v3ExpansionData->_rootHubNumPortsHS)
			return UINT16_MAX;
		return port + _v3ExpansionData->_rootHubPortsHSStartRange;
	}
	return UINT16_MAX;
}

#pragma mark -
#pragma mark Initialization/Finalization
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::InitializePorts(void)
{
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		uint32_t portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if (portSC & XHCI_PS_WRC)
			Write32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC, (portSC & XHCI_PS_WRITEBACK_MASK) | XHCI_PS_WRC);
		if ((portSC & (XHCI_PS_CSC | XHCI_PS_CCS)) == XHCI_PS_CCS) {
			_rhPortEmulateCSC[port] = true;
			RHPortStatusChangeBitmapSet(2U << port);
		}
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::AllocRHThreadCalls(void)
{
	for (uint8_t port = 0U ; port < _rootHubNumPorts; ++port) {
		_rhResumePortTimerThread[port] = thread_call_allocate(reinterpret_cast<thread_call_func_t>(RHResumePortTimerEntry), this);
		if (!_rhResumePortTimerThread[port])
			return kIOReturnNoResources;
	}
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void CLASS::FinalizeRHThreadCalls(void)
{
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		if (_rhResumePortTimerThread[port]) {
			thread_call_cancel(_rhResumePortTimerThread[port]);
			thread_call_free(_rhResumePortTimerThread[port]);
			_rhResumePortTimerThread[port] = 0;
		}
	}
}

#pragma mark -
#pragma mark Port Debouncing
#pragma mark -

#ifdef DEBOUNCING
__attribute__((noinline, visibility("hidden")))
int32_t CLASS::FindSlotFromPort(uint16_t port)
{
	ContextStruct* pContext;

	++port;
	for (uint8_t slot = 1U; slot <= _numSlots; ++slot) {
		if (ConstSlotPtr(slot)->isInactive())
			continue;
		pContext = GetSlotContext(slot);
		if (port != XHCI_SCTX_1_RH_PORT_GET(pContext->_s.dwSctx1))
			continue;
		if (GetSlCtxSpeed(pContext) != kUSBDeviceSpeedSuper)
			continue;
		if (XHCI_SCTX_0_ROUTE_GET(pContext->_s.dwSctx0))
			continue;
		return slot;
	}
	return -1;
}

__attribute__((noinline, visibility("hidden")))
IOReturn CLASS::HandlePortDebouncing(uint16_t* pStatusFlags, uint16_t* pChangeFlags, uint16_t port, uint16_t linkState, uint8_t protocol)
{
	uint64_t currentNanoSeconds, now;
	uint32_t portSC;
	int32_t slot;
	bool warmResetIssued;

	/*
	 * Insanity Now! Serenity Later.
	 *   TBD: Mavericks A744 - B2E6
	 */
	if (protocol != kUSBDeviceSpeedSuper) {
		if (_rhPortDebouncing[port])
			*pStatusFlags |= kHubPortDebouncing;
		return kIOReturnSuccess;
	}
	warmResetIssued = false;
	if ((*pChangeFlags & kHubPortConnection) &&
		(*pChangeFlags & kSSHubPortChangePortLinkStateMask) &&
		linkState == XDEV_INACTIVE) {
		slot = FindSlotFromPort(port);
		if (slot >= 0) {
			_rhPortBeingWarmReset[port] = true;
			XHCIRootHubWarmResetPort(port);
			for (int32_t count = 0; count < 8; ++count) {
				if (count)
					CheckedSleep(32U);
				portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
				if (m_invalid_regspace) {
					_rhPortBeingWarmReset[port] = false;
					return kIOReturnNoDevice;
				}
				if (portSC & XHCI_PS_WRC)
					break;
			}
			if (XHCI_PS_PLS_GET(portSC) == XDEV_U0 &&
				(portSC & (XHCI_PS_PED | XHCI_PS_CCS))) {
				*pChangeFlags &= ~kHubPortConnection;
				XHCIRootHubClearPortConnectionChange(port);
			}
			XHCIRootHubClearPortChangeBit(port, XHCI_PS_PRC | XHCI_PS_WRC);
			SetNeedsReset(slot, true);
			_rhPortBeingWarmReset[port] = false;
			absolutetime_to_nanoseconds(mach_absolute_time(), &currentNanoSeconds);
			_rhDebounceNanoSeconds[port] = currentNanoSeconds - 100 * kMillisecondScale;
			_rhPortDebouncing[port] = true;
			warmResetIssued = true;
			_rhPortDebounceADisconnect[port] = !(*pStatusFlags & kHubPortConnection);
		}
	}
	if (!warmResetIssued) {
		if ((*pChangeFlags & kHubPortConnection) &&
			!(*pChangeFlags & kSSHubPortChangeBHResetMask) &&
			!(*pChangeFlags & kHubPortBeingReset) &&
			!(*pStatusFlags & kSSHubPortStatusBeingResetMask)) {
			now = mach_absolute_time();
			if (_rhPortDebouncing[port]) {
				absolutetime_to_nanoseconds(now, &currentNanoSeconds);
				if (static_cast<int64_t>(currentNanoSeconds - _rhDebounceNanoSeconds[port]) < 100 * kMillisecondScale)
					*pChangeFlags &= ~kHubPortConnection;
				else {
					if (_rhPortDebounceADisconnect[port] &&
						(*pStatusFlags & kHubPortConnection)) {
						*pChangeFlags &= ~kHubPortConnection;
						XHCIRootHubClearPortConnectionChange(port);
					} else if (!_rhPortDebounceADisconnect[port] &&
							   !(*pStatusFlags & kHubPortConnection)) {
						*pChangeFlags &= ~kHubPortConnection;
						XHCIRootHubClearPortConnectionChange(port);
					}
				}
			} else {
				_rhPortDebounceADisconnect[port] = !(*pStatusFlags & kHubPortConnection);
				*pChangeFlags &= ~kHubPortConnection;
				absolutetime_to_nanoseconds(now, &_rhDebounceNanoSeconds[port]);
				_rhPortDebouncing[port] = true;
			}
		} else {
			_rhPortDebouncing[port] = false;
			_rhPortDebounceADisconnect[port] = false;
		}
	}
	if (_rhPortDebouncing[port] ||
		(*pChangeFlags & kHubPortOverCurrent) ||
		(*pStatusFlags & kHubPortOverCurrent))
		*pStatusFlags |= kHubPortDebouncing;
	return kIOReturnSuccess;
}
#endif
