//
//  Private.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 3rd, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "XHCITRB.h"

#ifndef GenericUSBXHCI_Private_h
#define GenericUSBXHCI_Private_h

#define IOSync __builtin_ia32_mfence

#pragma mark -
#pragma mark From IOUSBFamily
#pragma mark -

/*
 * From IOUSBFamily/Headers/USBTracepoints.h
 */
#define kUSBEnableTracePointsMask (1U << 1)
#define kUSBDisableMuxedPortsMask (1U << 11)
#define kUSBEnableAllXHCIControllersMask (1U << 12)

extern
#ifdef __cplusplus
"C"
#endif
UInt32 gUSBStackDebugFlags;	// defined in IOUSBFamily/Classes/IOUSBController.cpp

#pragma mark -
#pragma mark Errata
#pragma mark -

#define kErrataFrescoLogic 0x80U
#define kErrataRenesas 8U
#define kErrataDisableMSI 2U
#define kErrataIntelPantherPoint 4U
#define kErrataDisableComplianceExtension 0x10U
#define kErrataIntelPCIRoutingExtension 0x20U
#define kErrataParkRing 0x100U
#define kErrataFL1100 0x200U
#define kErrataASMedia 0x400U
#define kErrataAllowControllerDoze (1U << 25)

#pragma mark -
#pragma mark Structures
#pragma mark -

class GenericUSBXHCI;
class GenericUSBXHCIIsochEP;
class IOBufferMemoryDescriptor;
class OSObject;
struct TRBStruct;
struct EventRingSegmentTable;
struct XHCIAsyncEndpoint;
union ContextStruct;

typedef void (*TRBCallback)(GenericUSBXHCI*, TRBStruct*, void*);

struct ringStruct
{
	/*
	 * Total Size 96
	 */
	IOBufferMemoryDescriptor* md;	// 0x0
	TRBStruct* ptr;	// 0x8
	uint64_t physAddr;	// 0x10
	uint16_t numTRBs; // 0x18
	uint16_t numPages; // 0x1A
	uint8_t cycleState; // 0x1C
	uint16_t enqueueIndex; // 0x20
	uint16_t dequeueIndex; // 0x22
	uint16_t timeOutWatermark; // 0x24
	uint32_t u1; // 0x28
	TRBStruct stopTrb; // 0x2C
	uint64_t u2; // 0x40
	uint8_t epType; // 0x48
	union { // 0x50 - Typed as XHCIIsochEndpoint*, but doubles as XHCIAsyncEndpoint*
		XHCIAsyncEndpoint* asyncEndpoint;
		GenericUSBXHCIIsochEP* isochEndpoint;
	};
	uint8_t slot; // 0x58
	uint8_t endpoint; // 0x59
	bool endpointUnusable; // 0x5A
	bool deleteInProgress; // 0x5B
	bool schedulingPending; // 0x5C

	__attribute__((always_inline)) bool isInactive(void) { return !this || !this->md; }
};

struct EventRingStruct
{
	/*
	 * Total Size 64
	 */
	uint16_t xHCDequeueIndex; // 0x0
	uint16_t bounceDequeueIndex;// 0x2
	uint16_t bounceEnqueueIndex;// 0x4
	uint8_t cycleState; // 0x6
	bool foundSome; 	// 0x7
	uint16_t numxHCEntries;	// 0x8
	uint16_t numBounceEntries;	// 0xA
	int32_t numBounceQueueOverflows;	// 0xC
	EventRingSegmentTable* erstPtr; // 0x10 - Note: doubles as TRBs
	TRBStruct* bounceQueuePtr; // 0x18
	uint64_t erdp;		// 0x20
	uint64_t erstba;	// 0x28
	IOBufferMemoryDescriptor* md;	// 0x30
#if 0
	uint64_t Rsvd;		// 0x38
#endif
};

struct SlotStruct
{
	/*
	 * Total Size 552
	 */
	IOBufferMemoryDescriptor* md;	// 0
	ContextStruct* ctx; // Originally 8, 16
	uint64_t physAddr; // 24
	uint16_t maxStreamForEndpoint[kUSBMaxPipes];	// 32
	uint16_t lastStreamForEndpoint[kUSBMaxPipes]; // 160
	ringStruct* ringArrayForEndpoint[kUSBMaxPipes]; // 288
	bool _intelFlag;	// 544

	__attribute__((always_inline)) bool isInactive(void) { return !this->md; }
};

struct TRBCallbackEntry
{
	TRBCallback func;
	void* param;
};

struct PrintSink
{
	void (*printer)(struct PrintSink*, char const*, va_list);
	void print(char const*, ...) __attribute__((format(printf, 2, 3)));
};

#define GUX_OPTION_NO_SLEEP 1
#define GUX_OPTION_DEFER_INTEL_EHC_PORTS 2
#define GUX_OPTION_NO_INTEL_IDLE 4
#define GUX_OPTION_NO_MSI 8

#define DIAGCTR_SLEEP 0
#define DIAGCTR_RESUME 1
#define DIAGCTR_BNCEOVRFLW 2
#define DIAGCTR_CMDERR 3
#define DIAGCTR_XFERERR 4
#define DIAGCTR_XFERKEEPAWAY 5
#define DIAGCTR_XFERLAYOUT 6
#define NUM_DIAGCTRS 7

#ifdef __cplusplus
extern "C" {
#endif

extern int gux_log_level, gux_options;

#if 0
struct RHPortData
{
	uint32_t retCode;
	uint8_t protocol;
	uint16_t port;
};
#endif

#ifdef __cplusplus
}
#endif

#endif
