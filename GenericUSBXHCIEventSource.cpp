//
//  GenericUSBXHCIEventSource.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on October 27th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//
//

#include "GenericUSBXHCI.h"

class GenericUSBXHCIEventSource : public IOEventSource
{
	OSDeclareFinalStructors(GenericUSBXHCIEventSource);

protected:
	bool checkForWork();

public:
	__attribute__((always_inline)) bool init(OSObject* owner) { return IOEventSource::init(owner, 0); }
};

OSDefineMetaClassAndFinalStructors(GenericUSBXHCIEventSource, IOEventSource);

bool GenericUSBXHCIEventSource::checkForWork()
{
	GenericUSBXHCI* _owner = OSDynamicCast(GenericUSBXHCI, owner);
	if (_owner)
		_owner->_completer.Flush();
	return false;
}

__attribute__((visibility("hidden")))
IOReturn GenericUSBXHCI::InitializeEventSource(void)
{
	GenericUSBXHCIEventSource* es = OSTypeAlloc(GenericUSBXHCIEventSource);
	if (!es)
		return kIOReturnNoMemory;
	if (!es->GenericUSBXHCIEventSource::init(this)) {
		es->release();
		return kIOReturnNoMemory;
	}
	if (_workLoop) {
		IOReturn rc = _workLoop->addEventSource(es);
		if (rc != kIOReturnSuccess) {
			es->release();
			return rc;
		}
	}
	_eventSource = es;
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden")))
void GenericUSBXHCI::FinalizeEventSource(void)
{
	if (!_eventSource)
		return;
	if (_workLoop)
		_workLoop->removeEventSource(_eventSource);
	_eventSource->release();
	_eventSource = 0;
}
