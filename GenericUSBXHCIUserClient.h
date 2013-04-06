//
//  GenericUSBXHCIUserClient.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on February 3rd 2012.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#ifndef __GenericUSBXHCIUserClient__
#define __GenericUSBXHCIUserClient__

#include <IOKit/IOUserClient.h>

#define EXPORT __attribute__((visibility("default")))

#define kGUXUCType 1U

#define kGUXCapRegsDump 1U
#define kGUXRunRegsDump 2U
#define kGUXSlotsDump 3U
#define kGUXEndpointsDump 4U
#define kGUXBandwidthDump 5U
#define kGUXOptionsDump 6U

class EXPORT GenericUSBXHCIUserClient : public IOUserClient
{
	OSDeclareFinalStructors(GenericUSBXHCIUserClient);

public:
	IOReturn clientClose(void);
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
};

#endif
