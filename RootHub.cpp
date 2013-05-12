//
//  RootHub.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 21st 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"

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

	if (_rhPortBeingReset[port - 1U])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	pPortSC = &_pXHCIOperationalRegisters->prs[port - 1U].PortSC;
	if (state)
		portSC |= XHCI_PS_PP | XHCI_PS_WOE | XHCI_PS_WDE | XHCI_PS_WCE;
	else {
		/*
		 * Clear any pending change flags while powering down port.
		 */
		portSC |= XHCI_PS_CHANGEBITS;
		portSC &= ~XHCI_PS_PP;
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
	if (_rhPortBeingReset[port - 1U])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	/*
	 * Clear any pending change flags while disabling port.
	 */
	portSC |= XHCI_PS_CHANGEBITS;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port - 1U].PortSC, portSC | XHCI_PS_PED);
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
	if (_rhPortBeingReset[port - 1U])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	/*
	 * Clear any stray PLC when changing state
	 */
	Write32Reg(&_pXHCIOperationalRegisters->prs[port - 1U].PortSC, portSC |
			   XHCI_PS_LWS | XHCI_PS_PLS_SET(linkState) | XHCI_PS_PLC);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubWarmResetPort(uint16_t port)
{
	uint32_t portSC;

	if (_rhPortBeingReset[port - 1U])
		return kIOReturnNotPermitted;
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port - 1U].PortSC, portSC | XHCI_PS_WPR);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubResetPort(uint8_t protocol, uint16_t port)
{
	IOReturn rc;
#if 0
	uint32_t recCount;
#endif
	uint16_t _port;

	_port = port - 1U;
	if (_rhPortBeingReset[_port])
		return kIOReturnSuccess;
	_rhPortBeingReset[_port] = true;
#if 0
	for (recCount = 0U; _workLoop->inGate();) {
		++recCount;
		_workLoop->OpenGate();
	}
#endif
	rc = RHResetPort(protocol, port);
#if 0
	while (recCount--)
		_workLoop->CloseGate();
#endif
	_rhPortBeingReset[_port] = false;
	return rc;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubSuspendPort(uint8_t protocol, uint16_t port, bool state)
{
	uint64_t deadline;
	uint32_t volatile* pPortSC;
	uint32_t portSC, count;
	uint16_t _port;

	_port = port - 1U;
	if (_rhPortBeingResumed[_port] && !state)
		return kIOReturnSuccess;
	if (_rhPortBeingReset[_port])
		return kIOReturnNotPermitted;
	pPortSC = &_pXHCIOperationalRegisters->prs[_port].PortSC;
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
		_rhPortBeingResumed[_port] = true;
		if (protocol != kUSBDeviceSpeedSuper &&
			_rhResumePortTimerThread[_port]) {
			clock_interval_to_deadline(20U, kMillisecondScale, &deadline);
			thread_call_enter1_delayed(_rhResumePortTimerThread[_port],
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
	if (_rhPortEmulateCSC[port - 1U])
		_rhPortEmulateCSC[port - 1U] = false;
#if 0
	_rhPortDebouncing[port - 1U] = false;
	_rhPortDebounceADisconnect[port - 1U] = false;
#endif
	return rc;
}

__attribute__((visibility("hidden")))
IOReturn CLASS::XHCIRootHubClearPortChangeBit(uint16_t port, uint32_t bitMask)
{
	uint32_t portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->prs[port - 1U].PortSC, portSC | bitMask);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark RH Port Reset
#pragma mark -

__attribute__((visibility("hidden")))
IOReturn CLASS::RHResetPort(uint8_t protocol, uint16_t port)
{
	uint32_t portSC;
	uint16_t _port;

	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	_port = port - 1U;
	Write32Reg(&_pXHCIOperationalRegisters->prs[_port].PortSC, portSC | XHCI_PS_PR);
#if 0
	if (!(_errataBits & kErrataIntelPCIRoutingExtension) ||
		(gUSBStackDebugFlags & kUSBDisableMuxedPortsMask))
		return kIOReturnSuccess;
	uint32_t count;
	for (count = 0U; count < 8U; ++count) {
		if (count)
			IOSleep(32U);
		portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[_port].PortSC);
		if (m_invalid_regspace)
			return kIOReturnNoDevice;
		if (portSC & XHCI_PS_PRC)
			break;
	}
	if (XHCI_PS_SPEED_GET(portSC) == XDEV_SS)
		return kIOReturnSuccess;
	IOSleep(500U - count * 32U);
	/*
	 * TBD: E66F
	 */
	IOLog("%s: Code Incomplete\n", __FUNCTION__);
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
	uint32_t portSC, _port = port - 1U;

	if (!_rhPortBeingResumed[_port])
		return kIOReturnInternalError;
	if (m_invalid_regspace /* || !_controllerAvailable */) {
		_rhPortBeingResumed[_port] = false;
		return kIOReturnNoDevice;
	}
	portSC = GetPortSCForWriting(port);
	if (m_invalid_regspace)
		return kIOReturnNoDevice;
	Write32Reg(&_pXHCIOperationalRegisters->prs[_port].PortSC, portSC | XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U0) | XHCI_PS_PLC);
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
				   GetPortSCForWriting(port + 1U) | XHCI_PS_LWS | XHCI_PS_PLS_SET(XDEV_U0) | XHCI_PS_PLC);
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
								   reinterpret_cast<thread_call_param_t>(static_cast<size_t>(port) + 1U),
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
			RHPortStatusChangeBitmapSet(1U << (1U + port));
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

#if 0
#define CHECK1 (kSSHubPortChangePortLinkStateMask | kHubPortConnection)
#define CHECK2 (kSSHubPortChangeBHResetMask | kHubPortBeingReset | kHubPortConnection)

__attribute__((noinline, visibility("hidden")))
int32_t CLASS::FindSlotFromPort(uint16_t port)
{
	ContextStruct* pContext;

	++port;
	for (uint8_t slot = 1U; slot <= _numSlots; ++slot) {
		if (SlotPtr(slot)->isInactive())
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
void CLASS::HandlePortDebouncing(uint16_t* pStatusFlags, uint16_t* pChangeFlags, uint16_t port, uint16_t linkState, uint8_t protocol)
{
	uint64_t c_stamp;
	int32_t slot;

	/*
	 * Insanity Now! Serenity Later.
	 */
	if (protocol != kUSBDeviceSpeedSuper) {
		if (_rhPortDebouncing[port])
			*pStatusFlags |= kHubPortDebouncing;
		return;
	}
	if ((*pChangeFlags & (0xFF00U | CHECK1)) == CHECK1 &&
		linkState == XDEV_INACTIVE &&
		(slot = FindSlotFromPort(port) >= 0)) {
		uint32_t portSC;
		bool inGate = getWorkLoop()->inGate();
		IOCommandGate* gate = inGate ? GetCommandGate() : 0;
		_rhPortBeingWarmReset[port] = true;
		XHCIRootHubWarmResetPort(1U + port);
		for (int32_t count = 0; count < 8; ++count) {
			if (count) {
				if (inGate)
					SleepWithGateReleased(gate, 32U);
				else
					IOSleep(32U);
			}
			portSC = Read32Reg(&_pXHCIOperationalRegisters->prs[port].PortSC);
			if (m_invalid_regspace) {
				_rhPortBeingWarmReset[port] = false;
#if 0
				return kIOReturnNoDevice;
#else
				return;
#endif
			}
			if (portSC & XHCI_PS_WRC)
				break;
		}
		if (XHCI_PS_PLS_GET(portSC) == XDEV_U0 &&
			(portSC & (XHCI_PS_PED | XHCI_PS_CCS))) {
			XHCIRootHubClearPortConnectionChange(1U + port);
			*pChangeFlags &= ~kHubPortConnection;
		}
		XHCIRootHubClearPortChangeBit(1U + port, XHCI_PS_PRC | XHCI_PS_WRC);
		SetNeedsReset(slot, true);
		_rhPortBeingWarmReset[port] = false;
		absolutetime_to_nanoseconds(mach_absolute_time(), &c_stamp);
		_rhDebounceNanoSeconds[port] = c_stamp - 100 * kMillisecondScale;
		_rhPortDebouncing[port] = true;
		_rhPortDebounceADisconnect[port] = !(*pStatusFlags & kHubPortConnection);
	} else if ((*pChangeFlags & (0xFF00U | CHECK2)) == kHubPortConnection && !(*pStatusFlags & kHubPortBeingReset)) {
		uint64_t stamp = mach_absolute_time();
		if (_rhPortDebouncing[port]) {
			absolutetime_to_nanoseconds(stamp, &c_stamp);
			if (c_stamp - _rhDebounceNanoSeconds[port] < 100 * kMillisecondScale)
				*pChangeFlags &= ~kHubPortConnection;
			else if (_rhPortDebounceADisconnect[port] ==
					 ((*pStatusFlags & kHubPortConnection) != 0U)) {
				XHCIRootHubClearPortConnectionChange(1U + port);
				*pChangeFlags &= ~kHubPortConnection;
			}
		} else {
			absolutetime_to_nanoseconds(stamp, &_rhDebounceNanoSeconds[port]);
			_rhPortDebouncing[port] = true;
			_rhPortDebounceADisconnect[port] = !(*pStatusFlags & kHubPortConnection);
			*pChangeFlags &= ~kHubPortConnection;
		}
	} else {
		_rhPortDebouncing[port] = false;
		_rhPortDebounceADisconnect[port] = false;
	}
	if (_rhPortDebouncing[port] ||
		(*pChangeFlags & (0xFF00U | kHubPortOverCurrent)) ||
		(*pStatusFlags & kHubPortOverCurrent))
		*pStatusFlags |= kHubPortDebouncing;
}
#endif
