//
//  Private.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 3rd, 2013.
//  Copyright (c) 2013-2014 Zenith432. All rights reserved.
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
#define kErrataBrokenStreams 0x800U
#define kErrataAbsoluteEDTLA 0x8U
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
	struct {
		uint64_t addr;	// 0x48 (Added Mavericks)
		uint32_t shortfall;	// 0x50 (Added Mavericks)
		uint32_t count;	// 0x54 (Added Mavericks)
	} softRetries;
	uint8_t epType; // 0x58
	union { // 0x60 - Note: Typed as XHCIIsochEndpoint*, but doubles as XHCIAsyncEndpoint*
		XHCIAsyncEndpoint* asyncEndpoint;
		GenericUSBXHCIIsochEP* isochEndpoint;
	};
	uint8_t slot; // 0x68
	uint8_t endpoint; // 0x69
	bool returnInProgress; // 0x6A
	bool deleteInProgress; // 0x6B
	bool needsDoorbell; // 0x6C
	bool needsSetTRDQPtr;	// 0x6D (Added Mavericks)

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
	bool erdpNeedsUpdate; 	// 0x7
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
	bool oneBitCache;	// Added

	__attribute__((always_inline)) bool isInactive(void) const { return !this->md; }
	__attribute__((always_inline)) bool IsStreamsEndpoint(int32_t endpoint) const { return maxStreamForEndpoint[endpoint] > 1U; }
} __attribute__((aligned(512)));

struct TRBCallbackEntry
{
	TRBCallback func;
	int32_t* param;	// originally int32_t
};

#if 0
struct XHCIRootHubResetParams
{
	IOReturn status;
	uint8_t protocol;
	uint16_t port;
};
#endif

struct PrintSink
{
	void (*printer)(struct PrintSink*, char const*, va_list);
	void print(char const*, ...) __attribute__((format(printf, 2, 3)));
};

#pragma mark -
#pragma mark Options via kernel flags
#pragma mark -

#define GUX_OPTION_NO_SLEEP 1
#define GUX_OPTION_DEFER_INTEL_EHC_PORTS 2
#define GUX_OPTION_NO_MSI 8
#define GUX_OPTION_MAVERICKS 16
#define GUX_OPTION_YOSEMITE 32

#pragma mark -
#pragma mark Diagnostic Counters
#pragma mark -

#define DIAGCTR_SLEEP 0
#define DIAGCTR_RESUME 1
#define DIAGCTR_BNCEOVRFLW 2
#define DIAGCTR_CMDERR 3
#define DIAGCTR_XFERERR 4
#define DIAGCTR_XFERKEEPAWAY 5
#define DIAGCTR_XFERLAYOUT 6
#define DIAGCTR_ORPHANEDTDS 7
#define DIAGCTR_SHORTSUCCESS 8
#define DIAGCTR_BADDOORBELL 9
#define NUM_DIAGCTRS 10

#pragma mark -
#pragma mark Mavericks Quirks
#pragma mark -

#ifdef __LP64__
#define CHECK_FOR_MAVERICKS ((gux_options & GUX_OPTION_MAVERICKS) != 0U)
#else
#define CHECK_FOR_MAVERICKS false
#endif

#pragma mark -
#pragma mark Yosemite Quirks
#pragma mark -

#ifdef __LP64__
#define CHECK_FOR_YOSEMITE ((gux_options & GUX_OPTION_YOSEMITE) != 0U)
#else
#define CHECK_FOR_YOSEMITE false
#endif

#ifdef REHABMAN_UNIVERSAL_BUILD

class IOInterruptEventSource;

// NOTE: This is _V3ExpansionData from 10.9 SDK
// fields at the end which are not accesssed are commented out
// fields in the middle which are not accessed are renamed with x prefix
struct Mavericks_V3ExpansionData {
    uint32_t							_rootHubPollingRate32;
    bool								x_rootHubTransactionWasAborted;
    IOPMDriverAssertionID				x_externalUSBDeviceAssertionID;		// power manager assertion that we have an external USB device
    SInt32								_externalDeviceCount;				// the count of external devices in this controller - changed through the WL gate
    UInt32								x_inCheckPowerModeSleeping;			// The CheckPowerModeGated
    bool								_onThunderbolt;						// T if this controller is on a Thunderbolt bus
    uint32_t							x_thunderboltModelID;				// the model ID of the thunderbolt device in which this controller resides
    uint32_t							x_thunderboltVendorID;				// the vendor ID of the thunderbolt device in which this controller resides
    UInt8								_rootHubNumPortsSS;					// number of SS root hub ports - should be 15 or fewer! (1-based)
    UInt8								_rootHubNumPortsHS;					// number of SS root hub ports - should be 15 or fewer! (1-based)
    UInt8								_rootHubPortsHSStartRange;          // Start port number for the HS Range (1-based)
    UInt8								_rootHubPortsSSStartRange;          // Start port number for the SS Range (1-based)
    IOUSBRootHubInterruptTransaction	x_outstandingSSRHTrans[4];			// Transactions for the Root Hub.  We need 2, one for the current transaction and one for the next.  This is declared as 4 for binary compatibility
    bool								x_wakingFromStandby;					// t when waking from S4 standby - meaning that the hibernation bit was set but we
    bool								x_rootDomainWakingFromHibernation;   // t when pmrootdomain is between powerWill(ON) and powerDid(ON) while waking from hibernation
    UInt32								_rootHubStatusChangedBitmapSS;      // support up to 30 ports for status changes - XHCI has 2 Roothubs
    UInt64								_errata64Bits;						// Bitmap of controller-specific workarounds
    // NOTE: 32 comes from 10.9 header!! (kMaxXHCIPorts)
    UInt8								x_companionPort[32+1];    // Mapping between HS and SS port numbers in an XHCI controllers.  This is 1-based, so index 0 does not correspond to a valid port, and a companionPort of 0 indicates no companion port
    bool								_hasPCIPwrMgmt;                     // Set when PCI power management is avaialable
    bool								x_hibernationWakePart2;              // we are in the middle of the second part of a hibernation wake (OFF(0) to ON(3))
    AbsoluteTime						x_hibernationWakeStartTime;          // the time when we first noticed that we needed to do a hibernation wake
    UInt16								x_powerManagerCSRoffset;				// the offset into Config Space of the Power Manager Control/Status Register
    IOACPIPlatformDevice				*_acpiDevice;						// our device in the ACPI plane
    UInt32                              x_minimumIdlePowerState;				// the lowest power state this controller will go to when everything is idle (initialized to kUSBPowerStateLowPower)
    IOInterruptEventSource				*x_PMEInterruptEventSource;			// interrupt to wake from RTD3
    bool								x_PMEdisabled;						// tracks when we have disabled the PME handler
    bool								x_minimumIdlePowerStateValid;		// T when we have calculated the minimumIdlePowerState
    bool                                _parentDeviceON;                    // T when our parent device is in the ON power state
    //UInt32                              _thunderboltMaxBusStall;            // RMBS advertisement value when on thunderbolt
    
    // Support for XHCI Thunderbolt Docks and Extra current
    //SInt32                              _tbCurrentExtra;
    //SInt32                              _tbCurrentExtraInSleep;
    //SInt32                              _tbMaxCurrentPerPort;
    //SInt32                              _tbMaxCurrentPerPortInSleep;
    //SInt32                              _tbExternalSSPorts;
    //SInt32                              _tbUnconnectedExternalSSPorts;
    //SInt32                              _tbRevocableExtraCurrent;
    //SInt32                              _tbExternalNonSSPortsUsingExtraCurrent;
    //UInt32                              _tbCaptiveBitmap;
    //UInt32                              _tbExternalConnectorBitmap;
    //bool                                _tbBitmapsExist;
};

// NOTE: Taken from 10.10.0 SDK.
// fields at the end which are not accesssed are commented out
// fields in the middle which are not accessed are renamed with x prefix
struct Yosemite_V3ExpansionData {
    uint32_t							_rootHubPollingRate32;
    bool								x_rootHubTransactionWasAborted;
    IOPMDriverAssertionID				x_externalUSBDeviceAssertionID;		// power manager assertion that we have an external USB device
    SInt32								_externalDeviceCount;				// the count of external devices in this controller - changed through the WL gate
    UInt32								x_inCheckPowerModeSleeping;			// The CheckPowerModeGated
    uint32_t							x_thunderboltModelID;				// the model ID of the thunderbolt device in which this controller resides
    uint32_t							x_thunderboltVendorID;				// the vendor ID of the thunderbolt device in which this controller resides
    UInt8								_rootHubNumPortsSS;					// number of SS root hub ports - should be 15 or fewer! (1-based)
    UInt8								_rootHubNumPortsHS;					// number of SS root hub ports - should be 15 or fewer! (1-based)
    UInt8								_rootHubPortsHSStartRange;          // Start port number for the HS Range (1-based)
    UInt8								_rootHubPortsSSStartRange;          // Start port number for the SS Range (1-based)
    IOUSBRootHubInterruptTransaction	x_outstandingSSRHTrans[4];			// Transactions for the Root Hub.  We need 2, one for the current transaction and one for the next.  This is declared as 4 for binary compatibility
    bool								x_wakingFromStandby;					// t when waking from S4 standby - meaning that the hibernation bit was set but we
    bool								x_rootDomainWakingFromHibernation;   // t when pmrootdomain is between powerWill(ON) and powerDid(ON) while waking from hibernation
    UInt32								_rootHubStatusChangedBitmapSS;      // support up to 30 ports for status changes - XHCI has 2 Roothubs
    UInt64								_errata64Bits;						// Bitmap of controller-specific workarounds
    // NOTE: 32 comes from 10.9 header!! (kMaxXHCIPorts)
    UInt8								x_companionPort[32+1];    // Mapping between HS and SS port numbers in an XHCI controllers.  This is 1-based, so index 0 does not correspond to a valid port, and a companionPort of 0 indicates no companion port
    bool								_hasPCIPwrMgmt;                     // Set when PCI power management is avaialable
    bool								x_hibernationWakePart2;              // we are in the middle of the second part of a hibernation wake (OFF(0) to ON(3))
    AbsoluteTime						x_hibernationWakeStartTime;          // the time when we first noticed that we needed to do a hibernation wake
    UInt16								x_powerManagerCSRoffset;				// the offset into Config Space of the Power Manager Control/Status Register
    IOACPIPlatformDevice				*_acpiDevice;						// our device in the ACPI plane
    UInt32                              x_minimumIdlePowerState;				// the lowest power state this controller will go to when everything is idle (initialized to kUSBPowerStateLowPower)
    /*IOInterruptEventSource*/void				*x_PMEInterruptEventSource;			// interrupt to wake from RTD3
    bool								x_PMEdisabled;						// tracks when we have disabled the PME handler
    bool								x_minimumIdlePowerStateValid;		// T when we have calculated the minimumIdlePowerState
    bool                                _parentDeviceON;                    // T when our parent device is in the ON power state
    //UInt32                              _thunderboltMaxBusStall;            // RMBS advertisement value when on thunderbolt
    
    // Support for XHCI Thunderbolt Docks and Extra current
    //SInt32                              _tbCurrentExtra;
    //SInt32                              _tbCurrentExtraInSleep;
    //SInt32                              _tbMaxCurrentPerPort;
    //SInt32                              _tbMaxCurrentPerPortInSleep;
    //SInt32                              _tbExternalSSPorts;
    //SInt32                              _tbUnconnectedExternalSSPorts;
    //SInt32                              _tbRevocableExtraCurrent;
    //SInt32                              _tbExternalNonSSPortsUsingExtraCurrent;
    //UInt32                              _tbCaptiveBitmap;
    //UInt32                              _tbExternalConnectorBitmap;
    //bool                                _tbBitmapsExist;
    
    //bool                                _parentDeviceGoingToPCIPause;			// T when our parent device is going to sleep because of PCI Pause
    //bool                                _waitingForPCIPauseToFinish;            // T if we have a thread (or more) on commandSleep() waiting for the PCI Pause event to finish
    //SInt32								_pciPauseQueuedTransactionCount;		// Number of transactions/threads that have been commandSleep()'d due to the system processing a PCI Pause event
    //bool								_pciPauseTransactionToken[kMaxTransactionsDuringPCIPause];	// Token use to sleep/wake threads that come in during PCI Pause
    //bool                                _controllerWasResetOnSleep;
    
};

// NOTE: This taken from _ExpansionData (IOUSBController) in 10.10 SDK
// fields which are not used are prefixed with x
struct Yosemite_ExpansionData
{
    IOCommandPool		*xfreeUSBCommandPool;
    IOCommandPool		*xfreeUSBIsocCommandPool;
    IOTimerEventSource	*xwatchdogUSBTimer;
    bool				x_terminating;
    bool				x_watchdogTimerActiveXX;
    bool				x_pcCardEjected;						// Obsolete
    UInt32				x_busNumberXX;
    UInt32				x_currentSizeOfCommandPoolXX;
    UInt32				x_currentSizeOfIsocCommandPoolXX;
    UInt8				x_controllerSpeedXX;					// Controller speed, passed down for splits
    thread_call_t		x_terminatePCCardThread;				// Obsolete
    bool				x_addressPending[kUSBMaxDevices+2];
    SInt32				x_activeIsochTransfersXX;				// isochronous transfers in the queue
    IOService			*x_provider;							// common name for our provider
    bool				x_controllerCanSleep;				// true iff the controller is able to support sleep/wake
    bool				x_needToClose;
    UInt32				x_isochMaxBusStall;					// value (in ns) of the maximum PCI bus stall allowed for Isoch.
    SInt32				x_activeInterruptTransfersXX;			// interrupt transfers in the queue
    IOUSBRootHubDevice	*x_rootHubDeviceSSXX;
    UInt32              x_locationID;
    UInt32              _ignoreDisconnectBitmap;            // Port bitmap (adjusted for HS/SS ranges for XHCI controllers)
    UInt32              x_watchdogTimerTimeout;
    bool                x_usePCIBusNumber;                   // T if we should use the PCI bus number in the calculations for USB Bus number
    UInt32              x_acpiRootHubDepth;                  // depth of the root hub in the ACPI device tree, this value is either hard coded or set to
    bool                _onThunderbolt;						// T if this controller is on a Thunderbolt bus
};

// NOTE: This taken from IOUSBControllerV3 in 10.9 SDK

class Mavericks_IOUSBControllerV3 : public IOUSBControllerV2
{
    OSDeclareAbstractStructors(Mavericks_IOUSBControllerV3)
    friend class IOUSBDevice;
    friend class IOUSBHubDevice;
    friend class IOUSBRootHubDevice;
    
protected:
    // static variable shared by all instances
    static uint32_t *				_gHibernateState;
    
    
    UInt8							_myBusState;					// kUSBBusStateReset, kUSBBusStateSuspended, kUSBBusStateRunning
    bool							_wakingFromHibernation;			// True while the Hibernation Wake thread is active going to the OFF(0) state
    bool							_needToAckPowerDown;			// True while we are changing power state due to shutdown/restart
    bool							_onCardBus;						// OBSOLETE
    bool							_controllerAvailable;			// true if we can talk to the controller
    SInt32							_powerStateChangingTo;			// a power state that we are in the process of changing to, or -1 if we are stable
    bool							_poweringDown;					// true is the controller is powering down because of a systemWillPowerDown message
    bool							_restarting;					// true is the controller is restarting because of a systemWillPowerDown message
    unsigned long					_myPowerState;					// my current state (since getPowerState doesn't always change in time)
    IOUSBControllerV3				*_ehciController;				// set if I am a companion controller
    unsigned long					_numPowerStates;				// how many power states do I support (defaults to kUSBNumDefaultPowerStates)
    IOPMPowerState					*_myPowerStates;				// my power state array (allocated on the fly)
    IOPCIDevice						*_device;						// my PCI device
    
    // root hub support
    IOTimerEventSource					*_rootHubTimer;				// timer which fires at the rate of the root hub interrupt endpoint
    UInt8								_rootHubPollingRate;		// Obsolete -- we need to have it be a uint32_t
    UInt8								_rootHubNumPorts;			// number of root hub ports - should be 15 or fewer!
    UInt16								_rootHubStatusChangedBitmap;	// support up to 15 ports for status changes
    bool								_rootHubTimerActive;		// UNUSED
    IOUSBRootHubInterruptTransaction	_outstandingRHTrans[4];		// Transactions for the Root Hub.  We need 2, one for the current transaction and one for the next.  This is declared as 4 for binary compatibility

#if 0
    struct V3ExpansionData {
        uint32_t							_rootHubPollingRate32;
        bool								_rootHubTransactionWasAborted;
        IOPMDriverAssertionID				_externalUSBDeviceAssertionID;		// power manager assertion that we have an external USB device
        SInt32								_externalDeviceCount;				// the count of external devices in this controller - changed through the WL gate
        UInt32								_inCheckPowerModeSleeping;			// The CheckPowerModeGated
        bool								_onThunderbolt;						// T if this controller is on a Thunderbolt bus
        uint32_t							_thunderboltModelID;				// the model ID of the thunderbolt device in which this controller resides
        uint32_t							_thunderboltVendorID;				// the vendor ID of the thunderbolt device in which this controller resides
        UInt8								_rootHubNumPortsSS;					// number of SS root hub ports - should be 15 or fewer! (1-based)
        UInt8								_rootHubNumPortsHS;					// number of SS root hub ports - should be 15 or fewer! (1-based)
        UInt8								_rootHubPortsHSStartRange;          // Start port number for the HS Range (1-based)
        UInt8								_rootHubPortsSSStartRange;          // Start port number for the SS Range (1-based)
        IOUSBRootHubInterruptTransaction	_outstandingSSRHTrans[4];			// Transactions for the Root Hub.  We need 2, one for the current transaction and one for the next.  This is declared as 4 for binary compatibility
        bool								_wakingFromStandby;					// t when waking from S4 standby - meaning that the hibernation bit was set but we
        bool								_rootDomainWakingFromHibernation;   // t when pmrootdomain is between powerWill(ON) and powerDid(ON) while waking from hibernation
        UInt32								_rootHubStatusChangedBitmapSS;      // support up to 30 ports for status changes - XHCI has 2 Roothubs
        UInt64								_errata64Bits;						// Bitmap of controller-specific workarounds
        UInt8								_companionPort[kMaxXHCIPorts+1];    // Mapping between HS and SS port numbers in an XHCI controllers.  This is 1-based, so index 0 does not correspond to a valid port, and a companionPort of 0 indicates no companion port
        bool								_hasPCIPwrMgmt;                     // Set when PCI power management is avaialable
        bool								_hibernationWakePart2;              // we are in the middle of the second part of a hibernation wake (OFF(0) to ON(3))
        AbsoluteTime						_hibernationWakeStartTime;          // the time when we first noticed that we needed to do a hibernation wake
        UInt16								_powerManagerCSRoffset;				// the offset into Config Space of the Power Manager Control/Status Register
        IOACPIPlatformDevice				*_acpiDevice;						// our device in the ACPI plane
        UInt32                              _minimumIdlePowerState;				// the lowest power state this controller will go to when everything is idle (initialized to kUSBPowerStateLowPower)
        IOInterruptEventSource				*_PMEInterruptEventSource;			// interrupt to wake from RTD3
        bool								_PMEdisabled;						// tracks when we have disabled the PME handler
        bool								_minimumIdlePowerStateValid;		// T when we have calculated the minimumIdlePowerState
        bool                                _parentDeviceON;                    // T when our parent device is in the ON power state
        UInt32                              _thunderboltMaxBusStall;            // RMBS advertisement value when on thunderbolt
        
        // Support for XHCI Thunderbolt Docks and Extra current
        SInt32                              _tbCurrentExtra;
        SInt32                              _tbCurrentExtraInSleep;
        SInt32                              _tbMaxCurrentPerPort;
        SInt32                              _tbMaxCurrentPerPortInSleep;
        SInt32                              _tbExternalSSPorts;
        SInt32                              _tbUnconnectedExternalSSPorts;
        SInt32                              _tbRevocableExtraCurrent;
        SInt32                              _tbExternalNonSSPortsUsingExtraCurrent;
        UInt32                              _tbCaptiveBitmap;
        UInt32                              _tbExternalConnectorBitmap;
        bool                                _tbBitmapsExist;
    };
#endif
    Mavericks_V3ExpansionData *_v3ExpansionData;

#if 0
    // TB-relates expansion data
#define _TB_CURRENTEXTRA						_v3ExpansionData->_tbCurrentExtra
#define _TB_CURRENTEXTRAINSLEEP                 _v3ExpansionData->_tbCurrentExtraInSleep
#define _TB_MAXCURRENTPERPORT                   _v3ExpansionData->_tbMaxCurrentPerPort
#define _TB_MAXCURRENTPERPORTINSLEEP			_v3ExpansionData->_tbMaxCurrentPerPortInSleep
#define _TB_EXTERNALSSPORTS                     _v3ExpansionData->_tbExternalSSPorts
#define _TB_UNCONNECTEDEXTERNALSSPORTS			_v3ExpansionData->_tbUnconnectedExternalSSPorts
#define _TB_REVOCABLEEXTRACURRENT               _v3ExpansionData->_tbRevocableExtraCurrent
#define _TB_EXTERNALNONSSPORTSUSINGEXTRACURRENT _v3ExpansionData->_tbExternalNonSSPortsUsingExtraCurrent
#define _TB_CAPTIVEBITMAP                       _v3ExpansionData->_tbCaptiveBitmap
#define _TB_EXTERNALCONNECTORBITMAP             _v3ExpansionData->_tbExternalConnectorBitmap
#define _TB_BITMAPS_EXIST                       _v3ExpansionData->_tbBitmapsExist
    
#endif//0
    
    // IOKit methods
    virtual bool					init( OSDictionary *  propTable );
    virtual bool					start( IOService *  provider );
    virtual void					stop( IOService * provider );
    virtual unsigned long			maxCapabilityForDomainState ( IOPMPowerFlags domainState );
    virtual unsigned long			initialPowerStateForDomainState ( IOPMPowerFlags domainState );
    virtual IOReturn				powerStateWillChangeTo ( IOPMPowerFlags capabilities, unsigned long stateNumber, IOService* whatDevice);
    virtual IOReturn				setPowerState( unsigned long powerStateOrdinal, IOService* whatDevice );
    virtual IOReturn				powerStateDidChangeTo ( IOPMPowerFlags capabilities, unsigned long stateNumber, IOService* whatDevice);
    virtual void					powerChangeDone ( unsigned long fromState);
    virtual void					systemWillShutdown( IOOptionBits specifier );
    virtual bool					willTerminate(IOService * provider, IOOptionBits options);
    virtual bool					didTerminate( IOService * provider, IOOptionBits options, bool * defer );
    
    virtual void					free(void);
    
    // IOUSBController methods
    // we override these to deal with methods attempting to go through the workloop while we are in sleep
    virtual IOReturn 		AcquireDeviceZero( void );
    virtual void			ReleaseDeviceZero( void );
    virtual IOReturn 		DeviceRequest(IOUSBDevRequest *request,  IOUSBCompletion *completion, USBDeviceAddress address, UInt8 epNum, UInt32 noDataTimeout, UInt32 completionTimeout);
    virtual IOReturn 		DeviceRequest(IOUSBDevRequestDesc *request,  IOUSBCompletion *completion, USBDeviceAddress address, UInt8 epNum, UInt32 noDataTimeout, UInt32 completionTimeout);
    virtual IOReturn 		ClosePipe(USBDeviceAddress address, Endpoint *endpoint);
    virtual IOReturn 		AbortPipe(USBDeviceAddress address, Endpoint *endpoint);
    virtual IOReturn 		ResetPipe(USBDeviceAddress address, Endpoint *endpoint);
    virtual IOReturn 		ClearPipeStall(USBDeviceAddress address, Endpoint *endpoint);
    virtual IOReturn 		Read(IOMemoryDescriptor *buffer, USBDeviceAddress address, Endpoint *endpoint, IOUSBCompletion *completion, UInt32 noDataTimeout, UInt32 completionTimeout, IOByteCount reqCount);
    virtual IOReturn 		Write(IOMemoryDescriptor *buffer, USBDeviceAddress address, Endpoint *endpoint, IOUSBCompletion *completion, UInt32 noDataTimeout, UInt32 completionTimeout, IOByteCount reqCount);
    virtual IOReturn		IsocIO(IOMemoryDescriptor *buffer, UInt64 frameStart, UInt32 numFrames, IOUSBIsocFrame *frameList, USBDeviceAddress address, Endpoint *endpoint, IOUSBIsocCompletion *completion );
    virtual IOReturn		IsocIO(IOMemoryDescriptor *buffer, UInt64 frameStart, UInt32 numFrames, IOUSBLowLatencyIsocFrame *frameList, USBDeviceAddress address, Endpoint *endpoint, IOUSBLowLatencyIsocCompletion *completion, UInt32 updateFrequency );
    
    // IOUSBControllerV2 methods
    // we override these to deal with methods attempting to go through the workloop while we are in sleep
    virtual IOReturn 		OpenPipe(USBDeviceAddress address, UInt8 speed, Endpoint *endpoint);
    virtual IOReturn		AddHSHub(USBDeviceAddress highSpeedHub, UInt32 flags);
    virtual IOReturn		RemoveHSHub(USBDeviceAddress highSpeedHub);
    virtual IOReturn		SetTestMode(UInt32 mode, UInt32 port);
    virtual IOReturn		ReadV2(IOMemoryDescriptor *buffer, USBDeviceAddress	address, Endpoint *endpoint, IOUSBCompletionWithTimeStamp *completion, UInt32 noDataTimeout, UInt32	completionTimeout, IOByteCount reqCount);
    
    // IOUSBControllerV3 methods
    
    // these are called through the workloop
    static IOReturn					DoEnableAddressEndpoints(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn					DoEnableAllEndpoints(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn					GatedPowerChange(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn					ChangeExternalDeviceCount(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn					DoGetActualDeviceAddress(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3);
    static IOReturn					DoCreateStreams(OSObject *owner, void *arg0, void *arg1, void *arg2, void *arg3 );
    
    // also on the workloop
    static void						RootHubTimerFired(OSObject *owner, IOTimerEventSource *sender);
    
    // Copied from IOUSBController DoAbortEP
    static IOReturn 		DoAbortStream(
                                          OSObject *	owner,
                                          void *	arg0,
                                          void *	arg1,
                                          void *	arg2,
                                          void *	arg3 );
    
    // subclassable methods
    virtual IOReturn				CheckForEHCIController(IOService *provider);
    virtual	IOReturn				AllocatePowerStateArray(void);
    virtual	IOReturn				InitForPM(void);
    virtual IOReturn				CheckPowerModeBeforeGatedCall(char *fromStr);
    virtual IOReturn				EnableAllEndpoints(bool enable);
    virtual IOReturn				EnableBusMastering(bool enable);
    virtual IOReturn				EnsureUsability(void);
    virtual IOReturn				CheckForRootHubChanges(void);
    virtual IOReturn				RootHubQueueInterruptRead(IOMemoryDescriptor *buf, UInt32 bufLen, IOUSBCompletion completion);
    virtual IOReturn				RootHubAbortInterruptRead(void);
    virtual IOReturn				RootHubStartTimer(UInt8 pollingRate);			// Obsolete see RootHubStartTimer32
    virtual IOReturn				RootHubStopTimer(void);
    
    // these methods have a default implementation using some of the virtual methods below
    virtual	void					ControllerOff(void);
    virtual	void					ControllerRestart(void);
    virtual	void					ControllerSleep(void);
    virtual	void					ControllerDoze(void);
    virtual	void					ControllerOn(void);
    
    // these methods are pure virtual, and need to be implemented in the subclass driver
    virtual	IOReturn				ResetControllerState(void) = 0;
    virtual IOReturn				RestartControllerFromReset(void) = 0;
    virtual	IOReturn				SaveControllerStateForSleep(void) = 0;
    virtual	IOReturn				RestoreControllerStateFromSleep(void) = 0;
    virtual IOReturn				DozeController(void) = 0;
    virtual IOReturn				WakeControllerFromDoze(void) = 0;
    virtual IOReturn				UIMEnableAddressEndpoints(USBDeviceAddress address, bool enable) = 0;
    virtual IOReturn				UIMEnableAllEndpoints(bool enable) = 0;
    virtual IOReturn				EnableInterruptsFromController(bool enable) = 0;
    
public:
    
    // public methods
    virtual IOReturn				EnableAddressEndpoints(USBDeviceAddress address, bool enable);
    virtual bool					IsControllerAvailable(void);
    virtual IOReturn				HandlePowerChange(unsigned long powerStateOrdinal);
    virtual	UInt32					AllocateExtraRootHubPortPower(UInt32 extraPowerRequested);		// DEPRECATED
    virtual	void					ReturnExtraRootHubPortPower(UInt32 extraPowerReturned);			// DEPRECATED
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  0);
    virtual IOReturn				RootHubStartTimer32(uint32_t pollingRate);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  1);
    virtual IOReturn				CheckPMAssertions(IOUSBDevice *forDevice, bool deviceBeingAdded);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  2);
    
    /*!
     @function GetRootHubBOSDescriptor
     @abstract UIM function, retrun the BOS descriptor of the simulated root hub device
     As GET_DESCRIPTOR control request for configuration descrptor
     @param  desc   Descriptor structure to return data in
     */
    virtual IOReturn 		GetRootHubBOSDescriptor( OSData *desc );
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  3);
    /*!
     @function GetRootHubDescriptor
     @abstract UIM function, return the hub descriptor of the simulated root hub device
     As GET_DESCRIPTOR control request for hub descrptor
     @param  desc   Descriptor structure to return data in
     */
    virtual IOReturn 		GetRootHub3Descriptor( IOUSB3HubDescriptor *desc );
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  4);
    /*!
     @function UIMDeviceToBeReset
     @abstract Tell the UIM that the device will be reset.
     @param functionAddress USB device ID of device
     @result Return kIOReturnSuccess if pipes have been deleted and need to be setup again.
     */
    virtual IOReturn        UIMDeviceToBeReset(short functionAddress);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  5);
    /*!
     @function Read
     Read from a stream on a bulk endpoint
     @param streamID stream ID of the stream to read from
     @param buffer place to put the transferred data
     @param address Address of the device on the USB bus
     @param endpoint description of endpoint
     @param completion describes action to take when buffer has been filled
     @param noDataTimeout number of milliseconds of no data movement before the request is aborted
     @param completionTimeout number of milliseonds after the command is on the bus in which it must complete
     @param reqCount number of bytes requested for the transfer (must not be greater than the length of the buffer)
     */
    virtual IOReturn Read(UInt32							streamID,
                          IOMemoryDescriptor *			buffer,
                          USBDeviceAddress					address,
                          Endpoint *						endpoint,
                          IOUSBCompletion             *	completion,
                          UInt32							noDataTimeout,
                          UInt32							completionTimeout,
                          IOByteCount						reqCount );
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  6);
    /*!
     @function Write
     Write to a stream on a bulk endpoint
     @param streamID stream ID of the stream to write to
     @param buffer place to get the transferred data
     @param address Address of the device on the USB bus
     @param endpoint description of endpoint
     @param completion describes action to take when buffer has been emptied
     @param noDataTimeout number of milliseconds of no data movement before the request is aborted
     @param completionTimeout number of milliseonds after the command is on the bus in which it must complete
     @param reqCount number of bytes requested for the transfer (must not be greater than the length of the buffer)
     */
    virtual IOReturn 		Write(UInt32				streamID,
                                  IOMemoryDescriptor *	buffer,
                                  USBDeviceAddress 	address,
                                  Endpoint *		endpoint,
                                  IOUSBCompletion *	completion,
                                  UInt32			noDataTimeout,
                                  UInt32			completionTimeout,
                                  IOByteCount		reqCount );
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  7);
    virtual IOReturn 		AbortPipe(UInt32 streamID, USBDeviceAddress address, Endpoint *endpoint);
    
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  8);
    /*!
     @function UIMAbortEndpoint
     @abstract UIM function  Abort the specified endpoint or stream, return all transactions queued on it.
     @param streamID		If zero, abort the endpoint, if non zero abort the stream on the endpoint
     @param functionNumber  The USB device ID of the device to Abort
     @param endpointNumber  The endpoint number to Abort
     @param direction       Specifies direction of the endpoint for uniqueness. kUSBIn or KUSBOut.
     */
    virtual IOReturn 		UIMAbortStream(UInt32		streamID,
                                           short		functionNumber,
                                           short		endpointNumber,
                                           short		direction);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  9);
    virtual IOReturn 		OpenPipe(USBDeviceAddress address, UInt8 speed, Endpoint *endpoint, UInt32 maxStreams, UInt32 maxBurstAndMult);
    
    /*!
     @function UIMMaxSupportedStream
     @abstract UIM function  Returns how many streams per endpoint are supported by the USB controller.  This call does not go through the workloop gate.
     @result	The number of streams per endpont that are supported by the USB controller.  A value of 0 indicates that streams are not supported.
     */
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  10);
    virtual UInt32			UIMMaxSupportedStream(void);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  11);
    
    virtual USBDeviceAddress UIMGetActualDeviceAddress(USBDeviceAddress current);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  12);
    /*!
     @function UIMCreateBulkSSEndpoint
     @abstract Create an endpoint in the controller with superspeed streams and/or maxburst capabilities
     if controller does not support superspeed, this method should not be overridden
     @param functionNumber  USB device ID of device
     @param endpointNumber  endpoint address of the endpoint in the device
     @param direction       Direction of data flow. kUSBIn or kUSBOut
     @param speed           speed of the device kUSBDeviceSpeedFull, kUSBDeviceSpeedHigh, kUSBDeviceSuper
     @param maxPacketSize   maximum packet size of this endpoint
     @param maxStreams		Maximum stream number that this endpoint can use
     @param maxBurst		number of extra packets in a burst transfer
     */
    virtual IOReturn 		UIMCreateSSBulkEndpoint(
                                                    UInt8		functionNumber,
                                                    UInt8		endpointNumber,
                                                    UInt8		direction,
                                                    UInt8		speed,
                                                    UInt16		maxPacketSize,
                                                    UInt32      maxStream,
                                                    UInt32      maxBurst);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  13);
    /*!
     @function UIMCreateSSInterruptEndpoint
     @abstract Create an endpoint in the controller with maxburst capabilities
     if controller does not support superspeed, this method should not be overridden
     @param functionNumber  USB device ID of device
     @param endpointNumber  endpoint address of the endpoint in the device
     @param direction       Direction of data flow. kUSBIn or kUSBOut
     @param speed           speed of the device kUSBDeviceSpeedFull, kUSBDeviceSpeedHigh, kUSBDeviceSpeedSuper
     @param maxPacketSize   maximum packet size of this endpoint
     @param pollingRate
     @param maxBurst		number of extra packets in a burst transfer
     */
    virtual IOReturn 		UIMCreateSSInterruptEndpoint(
                                                         short		functionAddress,
                                                         short		endpointNumber,
                                                         UInt8		direction,
                                                         short		speed,
                                                         UInt16		maxPacketSize,
                                                         short		pollingRate,
                                                         UInt32     maxBurst);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  14);
    /*!
     @function UIMCreateSSIsochEndpoint
     @abstract Create an endpoint in the controller with maxburst capabilities
     if controller does not support superspeed, this method should not be overridden
     @param functionNumber		USB device ID of device
     @param endpointNumber		endpoint address of the endpoint in the device
     @param maxPacketSize		maximum packet size of this endpoint
     @param direction			Direction of data flow. kUSBIn or kUSBOut
     @param interval
     @param maxBurstAndMult		number of extra packets in a burst transfer and burst multiplier
     */
    virtual IOReturn 		UIMCreateSSIsochEndpoint(
                                                     short				functionAddress,
                                                     short				endpointNumber,
                                                     UInt32				maxPacketSize,
                                                     UInt8				direction,
                                                     UInt8				interval,
                                                     UInt32             maxBurstAndMult);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  15);
    /*!
     @function UIMCreateStreams
     @abstract Create streams on an endpoint
     if controller does not support superspeed, this method should not be overridden
     @param functionNumber  USB device ID of device
     @param endpointNumber  endpoint address of the endpoint in the device
     @param direction       Direction of data flow. kUSBIn or kUSBOut
     @param maxStreams		Number of streams to create
     */
    virtual IOReturn UIMCreateStreams(UInt8				functionNumber,
                                      UInt8				endpointNumber,
                                      UInt8				direction,
                                      UInt32            maxStream);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  16);
    
    virtual IOReturn		 	GetActualDeviceAddress(USBDeviceAddress currentAddress, USBDeviceAddress *newAddress);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  17);
    virtual	IOReturn			CreateStreams(UInt8 functionNumber, UInt8 endpointNumber, UInt8 direction,  UInt32 maxStream);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  18);
    /*!
     @function GetRootHubPortErrorCount
     @abstract UIM function, returns the port error count of the simulated root hub device (GET_PORT_ERROR_COUNT from the USB 3.0 spec)
     @param  port   Port Number
     @param	 count	Pointer to a UInt16 to store the actual link error count
     */
    virtual IOReturn 		GetRootHubPortErrorCount( UInt16 port, UInt16 * count );
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  19);
    /* !
     @function GetBandwidthAvailableForDevice
     @abstract returns the bandwidth available (in bytes) for a particular root hub port
     @param forDevice The device looking to get bandwidth
     @param pBandwidthAvailable Pointer to the holder for the bandwidth
     */
    virtual IOReturn        GetBandwidthAvailableForDevice(IOUSBDevice *forDevice, UInt32 *pBandwidthAvailable);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  20);
    /* !
     @function GetMinimumIdlePowerState
     @abstract determines the minimum Idle Power State for this controller and sets the iVar _v3ExpansionData->_minimumIdlePowerState.
     If the controller does support a lower idle power state, then it should also install the PME interrupt handler to handle it
     @result returns the calculated minimum idle power state
     */
    virtual UInt32        GetMinimumIdlePowerState(void);
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  21);
    /* !
     @function GetErrata64Bits
     @abstract  Used to get controller specific information for workarounds
     @result returs a bit field with the errata that apply to the given vendor/device/revision of a USB controller
     */
    virtual UInt64			GetErrata64Bits(UInt16 vendorID, UInt16 deviceID, UInt16 revisionID );
    
    
    OSMetaClassDeclareReservedUsed(IOUSBControllerV3,  22);
    /* !
     @function DoNotPowerOffPortsOnStop
     @abstract  Used to indicate if ports should be powered down on shutdown or hibernate.
     @result returs true if the ports should NOT be powered off on shutdown
     */
    virtual bool			DoNotPowerOffPortsOnStop(void);
    
    OSMetaClassDeclareReservedUnused(IOUSBControllerV3,  23);
    OSMetaClassDeclareReservedUnused(IOUSBControllerV3,  24);
    OSMetaClassDeclareReservedUnused(IOUSBControllerV3,  25);
    OSMetaClassDeclareReservedUnused(IOUSBControllerV3,  26);
    OSMetaClassDeclareReservedUnused(IOUSBControllerV3,  27);
    OSMetaClassDeclareReservedUnused(IOUSBControllerV3,  28);
    OSMetaClassDeclareReservedUnused(IOUSBControllerV3,  29);
    
#ifndef __OPEN_SOURCE__
public:
    // Non-virtual public methods which are out of the open source
    bool                            GetInternalHubErrataBits(IORegistryEntry* provider, UInt32 portnum, UInt32 locationID, UInt32 *errataBits);
    IOReturn                        GetConnectorType(IORegistryEntry * provider, UInt32 portNumber, UInt32 locationID, UInt8 *connectorType);
    bool                            CanControllerMuxOverToEHCI( IORegistryEntry * provider, UInt32 locationID );
    void                            UpdateThunderboltExtraCurrentiVars();
#endif
    
protected:
    
    void		FixupNECControllerConfigRegisters(void);
    IOReturn	RHQueueTransaction(IOMemoryDescriptor *buf, UInt32 bufLen, IOUSBCompletion completion, IOUSBRootHubInterruptTransactionPtr outstandingRHXaction);
    void		RHCompleteTransaction(IOUSBRootHubInterruptTransactionPtr outstandingRHTransPtr, UInt16	rhStatusChangedBitmap, UInt16 numPorts, bool cancelTimer);
    void		RHAbortTransaction(IOUSBRootHubInterruptTransactionPtr outstandingRHXaction);
    void		PMEHandler(IOInterruptEventSource * source, int count);
};

#define ON_THUNDERBOLT (CHECK_FOR_YOSEMITE ? ((Yosemite_ExpansionData*)_expansionData)->_onThunderbolt : ((Mavericks_V3ExpansionData*)_v3ExpansionData)->_onThunderbolt)
#define WRITE_ON_THUNDERBOLT(value) do { CHECK_FOR_YOSEMITE ? ((Yosemite_ExpansionData*)_expansionData)->_onThunderbolt = value : ((Mavericks_V3ExpansionData*)_v3ExpansionData)->_onThunderbolt = value; } while(0)
#define READ_V3EXPANSION(field) (CHECK_FOR_YOSEMITE ? ((Yosemite_V3ExpansionData*)_v3ExpansionData)->field : ((Mavericks_V3ExpansionData*)_v3ExpansionData)->field)
#define WRITE_V3EXPANSION(field,value) do { CHECK_FOR_YOSEMITE ? ((Yosemite_V3ExpansionData*)_v3ExpansionData)->field = value : ((Mavericks_V3ExpansionData*)_v3ExpansionData)->field = value; } while(0)

#else //!REHABMAN_UNIVERSAL_BUILD

#if __MAC_OS_X_VERSION_MAX_ALLOWED < 101000
#define ON_THUNDERBOLT _v3ExpansionData->_onThunderbolt
#define WRITE_ON_THUNDERBOLT(value) do { _v3ExpansionData->_onThunderbolt = value; } while(0)
#else
#define ON_THUNDERBOLT _expansionData->_onThunderbolt
#define WRITE_ON_THUNDERBOLT(value) do { _expansionData->_onThunderbolt = value; } while(0)
#endif
#define READ_V3EXPANSION(field) _v3ExpansionData->field
#define WRITE_V3EXPANSION(field,value) do { _v3ExpansionData->field = value; } while(0)

#endif //REHABMAN_UNIVERSAL_BUILD

#pragma mark -
#pragma mark Globals
#pragma mark -

#ifdef __cplusplus
extern "C" {
#endif

extern int gux_log_level, gux_options;

#ifdef __cplusplus
}
#endif

#endif
