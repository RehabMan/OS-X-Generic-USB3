//
//  Isoch.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on March 12th, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#ifndef GenericUSBXHCI_Isoch_h
#define GenericUSBXHCI_Isoch_h

#include <IOKit/usb/IOUSBControllerListElement.h>

class GenericUSBXHCIIsochEP : public IOUSBControllerIsochEndpoint
{
	OSDeclareFinalStructors(GenericUSBXHCIIsochEP);

public:
	// offset 0x88
	struct ringStruct* pRing;	// 0x488
	IOSimpleLock* _lock;	// 0x4A0
	uint16_t oneMPS;	// 0x4B0
	uint16_t multiple;	// 0x4B2
	uint32_t numPagesInRingQueue;	// 0x4B4
	uint16_t inSlot2;	// 0x4B8
	uint16_t boundOnPagesPerFrame;	// 0x4BA
	uint8_t intervalsPerFrame;	// 0x4BC
	uint16_t framesPerInterval;	// 0x4BD - originally uint8_t, but can be up to 4096!
	uint8_t intervalExponent;	// 0x4BE
	uint8_t speed;	// 0x4BF
	bool schedulingPending;	// 0x4C0

	bool init(void);
	void free(void);
};

class GenericUSBXHCIIsochTD : public IOUSBControllerIsochListElement
{
	OSDeclareFinalStructors(GenericUSBXHCIIsochTD);

public:
	IOUSBIsocCommand* _command;	// offset 0x70
	uint32_t some_r14;	// offset 0x78
	bool _aFlag;	// offset 0xD8
	bool _bFlag;	// offset 0xD9

	/*
	 * Pure Virtual from IOUSBControllerIsochListElement
	 */
	void SetPhysicalLink(IOPhysicalAddress) {}
	IOPhysicalAddress GetPhysicalLink(void) { return 0U; }
	IOPhysicalAddress GetPhysicalAddrWithType(void) { return 0U; }
	IOReturn UpdateFrameList(AbsoluteTime);
	IOReturn Deallocate(IOUSBControllerV2*);

	/*
	 * Self
	 */
	static GenericUSBXHCIIsochTD* ForEndpoint(GenericUSBXHCIIsochEP*);
	static IOReturn TranslateXHCIStatus(uint32_t, uint16_t*, uint32_t, uint8_t);
	uint32_t FrameForEventIndex(uint32_t);
};

#endif
