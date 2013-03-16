#include <IOKit/IOKitLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLASSNAME "GenericUSBXHCI"

#define kGUXUCType 1U

#define kGUXCapRegsDump 1U
#define kGUXRunRegsDump 2U
#define kGUXSlotsDump 3U
#define kGUXEndpointsDump 4U
#define kGUXBandwidthDump 5U
#define kGUXOptionsDump 6U

void printMsgBuffer(io_service_t service, unsigned type)
{
	kern_return_t ret;
	io_connect_t connect;
#if __LP64__
	mach_vm_address_t address;
	mach_vm_size_t size;
#else
	vm_address_t address;
	vm_size_t size;
#endif

	ret = IOServiceOpen(service, mach_task_self(), kGUXUCType, &connect);
	if (ret != KERN_SUCCESS) {
		printf("error: IOServiceOpen returned %#x\n", ret);
		return;
	}

	ret = IOConnectMapMemory(connect, type, mach_task_self(), &address, &size, kIOMapAnywhere);
	if (ret != kIOReturnSuccess) {
		printf("error: IOConnectMapMemory returned %#x\n", ret);
		IOServiceClose(connect);
		return;
	}

	printf("%s\n", (char *) address);

	IOServiceClose(connect);
}

void usage(char const* me)
{
	fprintf(stderr, "Usage: %s <caps | running | slots | endpoints <slot#> | bandwidth | options>\n", me);
	fprintf(stderr, "  caps - dumps cap regs\n");
	fprintf(stderr, "  running - dumps running regs\n");
	fprintf(stderr, "  slots - dumps active device slots\n");
	fprintf(stderr, "  endpoints <slot#> - dumps active endpoints on slot\n");
	fprintf(stderr, "  bandwidth - dumps bandwidth for root hub ports\n");
	fprintf(stderr, "  options - dumps kernel flags supported by kext\n");
}

int main(int argc, char const* argv[])
{
	mach_port_t masterPort;
	io_iterator_t iter = 0;
	io_service_t service;
	kern_return_t ret;
	io_string_t path;
	unsigned type = kGUXCapRegsDump;

	if (argc < 2)
		goto do_usage;
	if (!strcmp(argv[1], "caps"))
		type = kGUXCapRegsDump;
	else if (!strcmp(argv[1], "running"))
		type = kGUXRunRegsDump;
	else if (!strcmp(argv[1], "slots"))
		type = kGUXSlotsDump;
	else if (!strcmp(argv[1], "endpoints")) {
		if (argc < 3)
			goto do_usage;
		int slot_num = atoi(argv[2]);
		type = ((unsigned) (slot_num & 255) << 8) | kGUXEndpointsDump;
	} else if (!strcmp(argv[1], "bandwidth"))
		type = kGUXBandwidthDump;
	else if (!strcmp(argv[1], "options"))
		type = kGUXOptionsDump;
	else
		goto do_usage;

	ret = IOMasterPort(MACH_PORT_NULL, &masterPort);
	if (ret != KERN_SUCCESS) {
		printf("error: IOMasterPort returned %#x\n", ret);
		return -1;
	}
	ret = IOServiceGetMatchingServices(masterPort, IOServiceMatching(CLASSNAME), &iter);
	if (ret != KERN_SUCCESS) {
		printf("error: IOServiceGetMatchingServices returned %#x\n", ret);
		return -1;
	}
	while ((service = IOIteratorNext(iter)) != 0) {
		ret = IORegistryEntryGetPath(service, kIOServicePlane, path);
		if (ret != KERN_SUCCESS) {
			printf("error: IORegistryEntryGetPath returned %#x\n", ret);
			IOObjectRelease(service);
			continue;
		}
		printf("Found a device of class "CLASSNAME": %s\n", path);
		printMsgBuffer(service, type);
		IOObjectRelease(service);
	}
	IOObjectRelease(iter);

	return 0;

do_usage:
	usage(argv[0]);
	return 0;
}
