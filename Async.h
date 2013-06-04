//
//  Async.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on February 10th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#ifndef GenericUSBXHCI_Async_h
#define GenericUSBXHCI_Async_h

struct XHCIAsyncTD;

struct XHCIAsyncEndpoint
{
	ringStruct* pRing;	// 0x10 (start)
	XHCIAsyncTD* queuedHead;	// 0x18
	XHCIAsyncTD* queuedTail;	// 0x20
	XHCIAsyncTD* scheduledHead;	// 0x28
	XHCIAsyncTD* scheduledTail;	// 0x30
	XHCIAsyncTD* doneHead;	// 0x38
	XHCIAsyncTD* doneTail;	// 0x40
	XHCIAsyncTD* freeHead;	// 0x48
	XHCIAsyncTD* freeTail;	// 0x50
	uint32_t numTDsQueued;	// 0x58
	uint32_t numTDsScheduled;	// 0x5C
	uint32_t numTDsDone;	// 0x60
	uint32_t numTDsFree;	// 0x64
	bool aborting;	// 0x68
	uint32_t maxPacketSize;	// 0x6C
	uint32_t maxBurst;	// 0x70
	uint32_t multiple;	// 0x74
	uint32_t maxTDBytes;	// 0x78
	GenericUSBXHCI* provider;	// 0x80
								// sizeof 0x88

	IOReturn CreateTDs(IOUSBCommand*, uint16_t, uint32_t, uint8_t, uint8_t*);
	void ScheduleTDs(void);
	IOReturn Abort(void);
	XHCIAsyncTD* GetTDFromActiveQueueWithIndex(uint16_t);
	void RetireTDs(XHCIAsyncTD*, IOReturn, bool, bool);
	XHCIAsyncTD* GetTDFromFreeQueue(bool);
	void PutTDonDoneQueue(XHCIAsyncTD*);
	void FlushTDsWithStatus(IOUSBCommand*);
	void MoveTDsFromReadyQToDoneQ(IOUSBCommand*);
	void MoveAllTDsFromReadyQToDoneQ(void);
	void Complete(IOReturn);
	bool NeedTimeouts(void);
	void UpdateTimeouts(bool, uint32_t, bool);
	static XHCIAsyncTD* GetTD(XHCIAsyncTD**, XHCIAsyncTD**, uint32_t*);
	static XHCIAsyncEndpoint* withParameters(GenericUSBXHCI*, ringStruct*, uint32_t, uint32_t, uint32_t);
	void setParameters(uint32_t, uint32_t, uint32_t);
	bool checkOwnership(GenericUSBXHCI*, ringStruct*);
	void release(void);
	void nuke(void);
};

struct XHCIAsyncTD
{
	IOUSBCommand* command;	// 0x10 (start)
	uint32_t bytesThisTD;	// 0x18
	size_t bytesFollowingThisTD;	// 0x1C - original uint32_t
	size_t bytesPreceedingThisTD;	// 0x20
	uint32_t firstTrbIndex;	// 0x28
	uint32_t TrbCount;	// 0x2C
	bool interruptThisTD;	// 0x30
	bool multiTDTransaction;	// 0x31
	uint16_t numTDsThisTransaction;	// 0x32
	uint32_t mystery;	// 0x34
	uint32_t shortfall;	// 0x38
	uint16_t maxNumPagesInTD;	// 0x3C
	bool haveImmediateData;	// 0x3E
	bool finalTDInTransaction;	// 0x3F
	uint8_t immediateData[8];	// 0x40
	uint16_t streamId;	// 0x48
	int16_t lastTrbIndex;	// 0x4A
	bool flushed;	// 0x4C
	bool lastFlushedTD;	// 0x4D
	bool lastInRing;	// 0x4E
	XHCIAsyncEndpoint* provider;	// 0x50
	XHCIAsyncTD* next;	// 0x58
						// sizeof 0x60

	static XHCIAsyncTD* ForEndpoint(XHCIAsyncEndpoint*);
	void reinit(void);
	void release(void);
};

#endif
