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
UInt32 gUSBStackDebugFlags;	// Note: defined in IOUSBFamily/Classes/IOUSBController.cpp

#pragma mark -
#pragma mark Vendors
#pragma mark -

#define kVendorASMedia 0x1B21U
#define kVendorEtron 0x1B6FU
#define kVendorFrescoLogic 0x1B73U
#define kVendorIntel 0x8086U
#define kVendorRenesas 0x1033U
#define kVendorVIATechnologies 0x1106U
#define kVendorVMware 0x15ADU

#pragma mark -
#pragma mark Errata
#pragma mark -

#define kErrataDisableMSI 2U
#define kErrataIntelPantherPoint 4U
#define kErrataIntelLynxPoint 1U
#define kErrataEnableAutoCompliance 0x10U
#define kErrataIntelPortMuxing 0x20U
#define kErrataParkRing 0x100U
#define kErrataFL1100LowRev 0x200U
#define kErrataVMwarePortSwap 0x400U
#define kErrataSWAssistedIdle (1U << 25)

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

typedef void (*TRBCallback)(GenericUSBXHCI*, TRBStruct*, int32_t*);
typedef uint64_t (*PGetErrata64Bits)(void*, uint16_t, uint16_t, uint16_t);

struct ringStruct
{
	/*
	 * Total Size 88
	 */
	IOBufferMemoryDescriptor* md;	// 0x0
	TRBStruct* ptr;	// 0x8
	uint64_t physAddr;	// 0x10
	uint16_t numTRBs; // 0x18
	uint16_t numPages; // 0x1A
	uint8_t cycleState; // 0x1C - originally uint32_t
	uint16_t volatile enqueueIndex; // 0x20
	uint16_t volatile dequeueIndex; // 0x22
	uint16_t lastSeenDequeueIndex; // 0x24
	uint32_t lastSeenFrame; // 0x28
	TRBStruct stopTrb; // 0x2C
	uint64_t nextIsocFrame; // 0x40
	uint8_t epType; // 0x48
	union { // 0x50 - Note: Typed as XHCIIsochEndpoint*, but doubles as XHCIAsyncEndpoint*
		XHCIAsyncEndpoint* asyncEndpoint;
		GenericUSBXHCIIsochEP* isochEndpoint;
	};
	uint8_t slot; // 0x58
	uint8_t endpoint; // 0x59
	bool returnInProgress; // 0x5A
	bool deleteInProgress; // 0x5B
	bool schedulingPending; // 0x5C

	__attribute__((always_inline)) bool isInactive(void) const { return !this || !this->md; }
} __attribute__((aligned(128)));

struct EventRingStruct
{
	/*
	 * Total Size 56
	 */
	uint16_t xHCDequeueIndex; // 0x0
	uint16_t volatile bounceDequeueIndex;// 0x2
	uint16_t volatile bounceEnqueueIndex;// 0x4
	uint8_t cycleState; // 0x6
	bool foundSome; 	// 0x7
	uint16_t numxHCEntries;	// 0x8
	uint16_t numBounceEntries;	// 0xA
	int32_t volatile numBounceQueueOverflows;	// 0xC
	EventRingSegmentTable* erstPtr; // 0x10 - Note: doubles as TRBs
	TRBStruct* bounceQueuePtr; // 0x18
	uint64_t erdp;		// 0x20
	uint64_t erstba;	// 0x28
	IOBufferMemoryDescriptor* md;	// 0x30
} __attribute__((aligned(64)));

struct SlotStruct
{
	/*
	 * Total Size 416
	 */
	IOBufferMemoryDescriptor* md;	// 0
	ContextStruct* ctx; // Originally 8, 16
	uint64_t physAddr; // 24
	uint16_t maxStreamForEndpoint[kUSBMaxPipes];	// 32 - originally uint32_t[]
	uint16_t lastStreamForEndpoint[kUSBMaxPipes]; // 160 - originally uint32_t[]
	ringStruct* ringArrayForEndpoint[kUSBMaxPipes]; // 288
	bool deviceNeedsReset;	// 544
	bool oneBitCache;

	__attribute__((always_inline)) bool isInactive(void) const { return !this->md; }
	__attribute__((always_inline)) bool IsStreamsEndpoint(int32_t endpoint) const { return maxStreamForEndpoint[endpoint] > 1U; }
} __attribute__((aligned(512)));

struct TRBCallbackEntry
{
	TRBCallback func;
	int32_t* param;	// originally int32_t
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
#define GUX_OPTION_MAVERICKS 16

#define DIAGCTR_SLEEP 0
#define DIAGCTR_RESUME 1
#define DIAGCTR_BNCEOVRFLW 2
#define DIAGCTR_CMDERR 3
#define DIAGCTR_XFERERR 4
#define DIAGCTR_XFERKEEPAWAY 5
#define DIAGCTR_XFERLAYOUT 6
#define DIAGCTR_ORPHANEDTDS 7
#define DIAGCTR_SHORTSUCCESS 8
#define NUM_DIAGCTRS 9

/*
 * Mavericks Offsets
 */
#define V1_locationID 0xE0
#define V1_ignoreDisconnectBitmap 0xE4
#define V3_rootHubStatusChangedBitmapSS 0xCC
#define V3_errata64Bits 0xD0
#define V3_companionPort 0xD8
#define V3_hasPCIPwrMgmt 0xF9
#define V3_acpiDevice 0x110
#define V3_minimumIdlePowerStateValid 0x129
#define V3_GetErrata64Bits 418

#ifdef __LP64__
#define CHECK_FOR_MAVERICKS ((gux_options & GUX_OPTION_MAVERICKS) != 0U)
#else
#define CHECK_FOR_MAVERICKS false
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern int gux_log_level, gux_options;

#if 0
struct XHCIRootHubResetParams
{
	IOReturn status;
	uint8_t protocol;
	uint16_t port;
};
#endif

#ifdef __cplusplus
}
#endif

#endif
