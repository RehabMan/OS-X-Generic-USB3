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

#define kMaxTransfersPerFrame 8
#define kNumTDSlots 128

class GenericUSBXHCIIsochTD;

class GenericUSBXHCIIsochEP : public IOUSBControllerIsochEndpoint
{
	OSDeclareFinalStructors(GenericUSBXHCIIsochEP);

public:
	GenericUSBXHCIIsochTD* tdSlots[kNumTDSlots];	// 0x88
	struct ringStruct* pRing;	// 0x488
	GenericUSBXHCIIsochTD volatile* savedDoneQueueHead;	// 0x490
	uint32_t volatile producerCount;	// 0x498
	uint32_t volatile consumerCount;	// 0x49C
	IOSimpleLock* wdhLock;	// 0x4A0
	uint64_t scheduledFrameNumber;	// 0x4A8
	uint16_t oneMPS;	// 0x4B0 - originally uint8_t
	uint16_t multiple;	// 0x4B2 - originally uint8_t
	uint32_t numPagesInRingQueue;	// 0x4B4
	uint16_t outSlot;	// 0x4B8
	uint16_t boundOnPagesPerFrame;	// 0x4BA
	uint8_t transfersPerTD;	// 0x4BC
	uint16_t frameNumberIncrease;	// 0x4BD - originally uint8_t, but can be up to 4096!
	uint8_t intervalExponent;	// 0x4BE
	uint8_t speed;	// 0x4BF
	bool schedulingDelayed;	// 0x4C0
	bool volatile tdsScheduled;	// 0x4C1
	bool continuousStream;	// 0x4C2

	bool init(void);
	void free(void);
};

class GenericUSBXHCIIsochTD : public IOUSBControllerIsochListElement
{
	OSDeclareFinalStructors(GenericUSBXHCIIsochTD);

public:
	IOUSBIsocCommand* command;	// offset 0x70
	size_t transferOffset;	// offset 0x78
	uint32_t firstTrbIndex[kMaxTransfersPerFrame];	// offset 0x80
	uint32_t trbCount[kMaxTransfersPerFrame];	// offset 0xA0
	bool statusUpdated[kMaxTransfersPerFrame];	// offset 0xC0
	TRBStruct eventTrb;	// offset 0xC8
	bool newFrame;	// offset 0xD8
	bool interruptThisTD;	// offset 0xD9

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
	static IOReturn TranslateXHCIStatus(uint32_t);
	int32_t FrameForEventIndex(uint32_t);
};

#endif
