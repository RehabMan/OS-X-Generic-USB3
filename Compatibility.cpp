//
//  Compatibility.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on September 10th 2014.
//  Copyright (c) 2014 Zenith432. All rights reserved.
//
//

#include "GenericUSBXHCI.h"

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Build-time Versioning Test
#pragma mark -

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000
	#if __MAC_OS_X_VERSION_MIN_REQUIRED < 101000
		#error OS 10.10 SDK may only be used to target OS version 10.10.
	#endif
#elif __MAC_OS_X_VERSION_MIN_REQUIRED < 1075
	#error Target OS version must be 10.7.5 or above.
#endif

#if 0

#pragma mark -
#pragma mark Introduced OS 10.10
#pragma mark -

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 101000 && __MAC_OS_X_VERSION_MIN_REQUIRED < 101000
__attribute__((visibility("hidden")))
bool CLASS::init(IORegistryEntry* from, IORegistryPlane const* inPlane)
{
	return super::init(from, inPlane);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::GetRootHubPowerExitLatencies(IOUSBHubExitLatencies** latencies)
{
	return super::GetRootHubPowerExitLatencies(latencies);
}

__attribute__((visibility("hidden")))
IOReturn CLASS::CheckPowerModeBeforeGatedCall(char* fromStr, OSObject* token, UInt32 options)
{
	return super::CheckPowerModeBeforeGatedCall(fromStr, token, options);
}
#endif

#pragma mark -
#pragma mark Introduced OS 10.9
#pragma mark -

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1090 && __MAC_OS_X_VERSION_MIN_REQUIRED < 1090
__attribute__((visibility("hidden")))
IOReturn CLASS::updateReport(IOReportChannelList* channels, IOReportUpdateAction action, void* result, void* destination)
{
	return super::updateReport(channels, action, result, destination);
}

__attribute__((visibility("hidden")))
bool CLASS::DoNotPowerOffPortsOnStop(void)
{
	return super::DoNotPowerOffPortsOnStop();
}

__attribute__((visibility("hidden")))
IOReturn CLASS::configureReport(IOReportChannelList* channels, IOReportConfigureAction action, void* result, void* destination)
{
	return super::configureReport(channels, action, result, destination);
}
#endif

#pragma mark -
#pragma mark Introduced OS 10.8.5
#pragma mark -

#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1080 && __MAC_OS_X_VERSION_MIN_REQUIRED < 1085
__attribute__((visibility("hidden")))
UInt32 CLASS::GetMinimumIdlePowerState(void)
{
	return super::GetMinimumIdlePowerState();
}

__attribute__((visibility("hidden")))
IOReturn CLASS::UpdateDeviceAddress(USBDeviceAddress oldDeviceAddress, USBDeviceAddress newDeviceAddress, UInt8 speed, USBDeviceAddress hubAddress, int port)
{
	return super::UpdateDeviceAddress(oldDeviceAddress, newDeviceAddress, speed, hubAddress, port);
}

__attribute__((visibility("hidden")))
UInt64 CLASS::GetErrata64Bits(UInt16 vendorID, UInt16 deviceID, UInt16 revisionID)
{
	return super::GetErrata64Bits(vendorID, deviceID, revisionID);
}
#endif

#endif //0