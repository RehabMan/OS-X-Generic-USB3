//
//  GenericUSBXHCIUserClient.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on February 3rd 2012.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCIUserClient.h"
#include "GenericUSBXHCI.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

#define CLASS GenericUSBXHCIUserClient
#define super IOUserClient
OSDefineMetaClassAndFinalStructors(GenericUSBXHCIUserClient, IOUserClient);

#pragma mark -
#pragma mark Helpers
#pragma mark -

struct BufferedPrintSink : PrintSink
{
	void* buffer;
	size_t position;
	size_t capacity;
};

static
void BufferedPrintSinkFunction(struct PrintSink* sink, const char* format, va_list args)
{
	int chars;
	BufferedPrintSink* _sink = static_cast<BufferedPrintSink*>(sink);

	if (!_sink || !_sink->buffer || _sink->position >= _sink->capacity)
		return;
	chars = vsnprintf(static_cast<char*>(_sink->buffer) + _sink->position,
					  _sink->capacity - _sink->position,
					  format, args);
	_sink->position += chars;
}

static
IOReturn MakeMemoryAndPrintSink(size_t capacity, IOBufferMemoryDescriptor** pMd, IOMemoryMap** pMap, BufferedPrintSink* pSink)
{
	IOBufferMemoryDescriptor* md;
	IOMemoryMap* map;
	IOReturn rc;

	md = IOBufferMemoryDescriptor::inTaskWithOptions(0,
													 kIOMemoryPageable | kIODirectionIn,
													 capacity);
	if (!md)
		return kIOReturnNoMemory;
	rc = md->prepare();
	if (rc != kIOReturnSuccess) {
		md->release();
		return rc;
	}
	map = md->createMappingInTask(kernel_task, 0U, kIOMapAnywhere);
	if (!map) {
		md->complete();
		md->release();
		return kIOReturnVMError;
	}
	*pMd = md;
	*pMap = map;
	bzero(pSink, sizeof *pSink);
	pSink->printer = &BufferedPrintSinkFunction;
	pSink->buffer = reinterpret_cast<void*>(map->getVirtualAddress());
	pSink->position = 0U;
	pSink->capacity = capacity;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark User Client
#pragma mark -

IOReturn CLASS::clientClose(void)
{
	if (!terminate())
		IOLog("GenericUSBXHCIUserClient::%s: terminate failed.\n", __FUNCTION__);
	return kIOReturnSuccess;
}

static
IOReturn GatedPrintCapRegs(OSObject* owner, void* pSink, void*, void*, void*)
{
	static_cast<GenericUSBXHCI*>(owner)->PrintCapRegs(static_cast<PrintSink*>(pSink));
	return kIOReturnSuccess;
}

static
IOReturn GatedPrintRuntimeRegs(OSObject* owner, void* pSink, void*, void*, void*)
{
	static_cast<GenericUSBXHCI*>(owner)->PrintRuntimeRegs(static_cast<PrintSink*>(pSink));
	return kIOReturnSuccess;
}

static
IOReturn GatedPrintSlots(OSObject* owner, void* pSink, void*, void*, void*)
{
	static_cast<GenericUSBXHCI*>(owner)->PrintSlots(static_cast<PrintSink*>(pSink));
	return kIOReturnSuccess;
}

static
IOReturn GatedPrintEndpoints(OSObject* owner, void* pSink, void* slot, void*, void*)
{
	static_cast<GenericUSBXHCI*>(owner)->PrintEndpoints(static_cast<uint8_t>(reinterpret_cast<size_t>(slot)),
														static_cast<PrintSink*>(pSink));
	return kIOReturnSuccess;
}

static
IOReturn GatedPrintRootHubPortBandwidth(OSObject* owner, void* pSink, void*, void*, void*)
{
	static_cast<GenericUSBXHCI*>(owner)->PrintRootHubPortBandwidth(static_cast<PrintSink*>(pSink));
	return kIOReturnSuccess;
}

IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	IOBufferMemoryDescriptor* md;
	IOMemoryMap* kernelMap;
	GenericUSBXHCI* provider;
	BufferedPrintSink sink;
	IOReturn ret = kIOReturnUnsupported;

	*options = 0U;
	*memory = 0;
	switch (type & 255U) {
		case kGUXCapRegsDump:
			provider = OSDynamicCast(GenericUSBXHCI, getProvider());
			if (!provider)
				break;
			ret = MakeMemoryAndPrintSink(PAGE_SIZE, &md, &kernelMap, &sink);
			if (ret != kIOReturnSuccess)
				break;
			provider->getWorkLoop()->runAction(GatedPrintCapRegs, provider, &sink);
			kernelMap->release();
			md->complete();
			*options = kIOMapReadOnly;
			*memory = md;
			ret = kIOReturnSuccess;
			break;
		case kGUXRunRegsDump:
			provider = OSDynamicCast(GenericUSBXHCI, getProvider());
			if (!provider)
				break;
			ret = MakeMemoryAndPrintSink(PAGE_SIZE, &md, &kernelMap, &sink);
			if (ret != kIOReturnSuccess)
				break;
			provider->getWorkLoop()->runAction(GatedPrintRuntimeRegs, provider, &sink);
			kernelMap->release();
			md->complete();
			*options = kIOMapReadOnly;
			*memory = md;
			ret = kIOReturnSuccess;
			break;
		case kGUXSlotsDump:
			provider = OSDynamicCast(GenericUSBXHCI, getProvider());
			if (!provider)
				break;
			ret = MakeMemoryAndPrintSink(PAGE_SIZE, &md, &kernelMap, &sink);
			if (ret != kIOReturnSuccess)
				break;
			provider->getWorkLoop()->runAction(GatedPrintSlots, provider, &sink);
			kernelMap->release();
			md->complete();
			*options = kIOMapReadOnly;
			*memory = md;
			ret = kIOReturnSuccess;
			break;
		case kGUXEndpointsDump:
			provider = OSDynamicCast(GenericUSBXHCI, getProvider());
			if (!provider)
				break;
			ret = MakeMemoryAndPrintSink(PAGE_SIZE, &md, &kernelMap, &sink);
			if (ret != kIOReturnSuccess)
				break;
			provider->getWorkLoop()->runAction(GatedPrintEndpoints, provider, &sink, reinterpret_cast<void*>((type >> 8U) & 255U));
			kernelMap->release();
			md->complete();
			*options = kIOMapReadOnly;
			*memory = md;
			ret = kIOReturnSuccess;
			break;
		case kGUXBandwidthDump:
			provider = OSDynamicCast(GenericUSBXHCI, getProvider());
			if (!provider)
				break;
			ret = MakeMemoryAndPrintSink(PAGE_SIZE, &md, &kernelMap, &sink);
			if (ret != kIOReturnSuccess)
				break;
			provider->getWorkLoop()->runAction(GatedPrintRootHubPortBandwidth, provider, &sink);
			kernelMap->release();
			md->complete();
			*options = kIOMapReadOnly;
			*memory = md;
			ret = kIOReturnSuccess;
			break;
	}
	return ret;
}
