//
//  Completer.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on October 18th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//
//

#ifndef GenericUSBXHCI_Completer_h
#define GenericUSBXHCI_Completer_h

#include <IOKit/usb/USB.h>

class GenericUSBXHCI;

class Completer
{
private:
	struct CompleterItem
	{
		IOUSBCompletion completion;
		IOReturn status;
		uint32_t actualByteCount;
		CompleterItem* next;
	};

	GenericUSBXHCI* owner;
	CompleterItem* activeHead;
	CompleterItem* activeTail;
	CompleterItem* freeHead;
	CompleterItem* freeTail;
	uint32_t freeCount;

	void InternalFlush(void);

public:
	__attribute__((always_inline))
	void setOwner(GenericUSBXHCI* owner) { this->owner = owner; }
	bool AddItem(IOUSBCompletion const*, IOReturn, uint32_t, bool);
	__attribute__((always_inline))
	void Flush(void) { if (activeHead) InternalFlush(); }
	void Finalize(void);
};

#endif
