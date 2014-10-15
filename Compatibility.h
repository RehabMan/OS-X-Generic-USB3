//
//  Compatibility.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on September 10th 2014.
//  Copyright (c) 2014 Zenith432. All rights reserved.
//
//

#if 0

#ifndef GenericUSBXHCI_Compatibility_h
#define GenericUSBXHCI_Compatibility_h

	/*
	 * Introduced OS 10.10
	 */
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000 && __MAC_OS_X_VERSION_MIN_REQUIRED < 101000
	bool init(IORegistryEntry* from, IORegistryPlane const* inPlane);
	IOReturn GetRootHubPowerExitLatencies(IOUSBHubExitLatencies** latencies);
	IOReturn CheckPowerModeBeforeGatedCall(char* fromStr, OSObject* token, UInt32 options);
#endif

	/*
	 * Introduced OS 10.9
	 */
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1090 && __MAC_OS_X_VERSION_MIN_REQUIRED < 1090
	IOReturn updateReport(IOReportChannelList* channels, IOReportUpdateAction action, void* result, void* destination);
	bool DoNotPowerOffPortsOnStop(void);
	IOReturn configureReport(IOReportChannelList* channels, IOReportConfigureAction action, void* result, void* destination);
#endif

	/*
	 * Introduced OS 10.8.5
	 */
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1080 && __MAC_OS_X_VERSION_MIN_REQUIRED < 1085
	UInt32 GetMinimumIdlePowerState(void);
	IOReturn UpdateDeviceAddress(USBDeviceAddress oldDeviceAddress, USBDeviceAddress newDeviceAddress, UInt8 speed, USBDeviceAddress hubAddress, int port);
	UInt64 GetErrata64Bits(UInt16 vendorID, UInt16 deviceID, UInt16 revisionID);
#endif

#endif

#endif //0