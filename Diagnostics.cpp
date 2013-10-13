//
//  Diagnostics.cpp
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 5th 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#include "GenericUSBXHCI.h"
#include "XHCITypes.h"
#include <IOKit/IOFilterInterruptEventSource.h>
#include <libkern/OSKextLib.h>
#include <libkern/version.h>

#define CLASS GenericUSBXHCI
#define super IOUSBControllerV3

#pragma mark -
#pragma mark Pretty Printers
#pragma mark -

static
char const* stringForPLS(uint32_t linkState)
{
	switch (linkState) {
		case XDEV_U0:
			return "U0";
		case 1U:
			return "U1";
		case 2U:
			return "U2";
		case XDEV_U3:
			return "U3";
		case XDEV_DISABLED:
			return "Disabled";
		case XDEV_RXDETECT:
			return "RxDetect";
		case XDEV_INACTIVE:
			return "Inactive";
		case XDEV_POLLING:
			return "Polling";
		case XDEV_RECOVERY:
			return "Recovery";
		case XDEV_HOTRESET:
			return "HotReset";
		case XDEV_COMPLIANCE:
			return "ComplianceMode";
		case XDEV_TEST:
			return "TestMode";
		case XDEV_RESUME:
			return "Resume";
	}
	return "Unknown";
}

static
char const* stringForSpeed(uint32_t speed)
{
	switch (speed) {
		case XDEV_FS:
			return "Full";
		case XDEV_LS:
			return "Low";
		case XDEV_HS:
			return "High";
		case XDEV_SS:
			return "Super";
	}
	return "Unknown";
}

static
char const* stringForPIC(uint32_t pic)
{
	switch (pic) {
		case 0U:
			return "Off";
		case 1U:
			return "Amber";
		case 2U:
			return "Green";
		case 3U:
			return "Undefined";
	}
	return "Unknown";
}

static
char const* stringForSlotState(uint32_t state)
{
	switch (state) {
		case 0U:
			return "Disabled";
		case 1U:
			return "Default";
		case 2U:
			return "Addressed";
		case 3U:
			return "Configured";
	}
	return "Reserved";
}

static
char const* stringForEPState(uint32_t state)
{
	switch (state) {
		case EP_STATE_DISABLED:
			return "Disable";
		case EP_STATE_RUNNING:
			return "Running";
		case EP_STATE_HALTED:
			return "Halted";
		case EP_STATE_STOPPED:
			return "Stopped";
		case EP_STATE_ERROR:
			return "Error";
	}
	return "Unknown";
}

static
char const* stringForEPType(uint32_t epType)
{
	switch (epType) {
		case ISOC_OUT_EP:
			return "Isoch Out";
		case BULK_OUT_EP:
			return "Bulk Out";
		case INT_OUT_EP:
			return "Interrupt Out";
		case CTRL_EP:
			return "Control";
		case ISOC_IN_EP:
			return "Isoch In";
		case BULK_IN_EP:
			return "Bulk In";
		case INT_IN_EP:
			return "Interrupt In";
	}
	return "Unknown";
}

static
char const* stringForL1S(uint32_t v)
{
	switch (v) {
		case 0U:
			return "Invalid";
		case 1U:
			return "Success";
		case 2U:
			return "Not Yet";
		case 3U:
			return "Not Supported";
		case 4U:
			return "Timeout/Error";
	}
	return "Reserved";
}

static
char const* stringForTestMode(uint32_t v)
{
	switch (v) {
		case 0U:
			return "Disabled";
		case 1U:
			return "J_STATE";
		case 2U:
			return "K_STATE";
		case 3U:
			return "SE0_NAK";
		case 4U:
			return "Packet";
		case 5U:
			return "FORCE_ENABLE";
		case 15U:
			return "Error";
	}
	return "Reserved";
}

static inline
char test_bit(uint32_t v, int b)
{
	return (v & (1U << b)) ? 'Y': 'N';
}

#pragma mark -
#pragma mark More Printers
#pragma mark -

static
void printVersions(PrintSink* pSink)
{
	char const* ver_str;

	if (!pSink)
		return;
	pSink->print("Darwin %d.%d.%d\n", version_major, version_minor, version_revision);
	ver_str = OSKextGetCurrentVersionString();
	if (ver_str)
		pSink->print("Kext Version %s\n", ver_str);
}

static
void printLegacy(PrintSink* pSink, uint32_t volatile const* pUSBLegSup)
{
	uint32_t v1, v2;

	if (!pSink || !pUSBLegSup)
		return;
	v1 = *pUSBLegSup;
	v2 = pUSBLegSup[1];
	pSink->print("Legacy Owner[Bios,OS] %c%c, SMIEn[Event,HSE,OSOwn,PCI,BAR] %c%c%c%c%c, Chg[OWOwn,PCI,BAR] %c%c%c\n",
				 test_bit(v1, 16),
				 test_bit(v1, 24),
				 test_bit(v2, 0),
				 test_bit(v2, 4),
				 test_bit(v2, 13),
				 test_bit(v2, 14),
				 test_bit(v2, 15),
				 test_bit(v2, 29),
				 test_bit(v2, 30),
				 test_bit(v2, 31));
}

static
void printIntelMuxRegs(PrintSink* pSink, IOPCIDevice* pDevice)
{
	uint32_t v1, v2, v3, v4;

	if (!pSink || !pDevice)
		return;
	v1 = pDevice->configRead32(PCI_XHCI_INTEL_XUSB2PR);
	v2 = pDevice->configRead32(PCI_XHCI_INTEL_XUSB2PRM);
	v3 = pDevice->configRead32(PCI_XHCI_INTEL_USB3_PSSEN);
	v4 = pDevice->configRead32(PCI_XHCI_INTEL_USB3PRM);
	pSink->print("Intel USB2.0 Port Routing %#x, Mask %#x\n", v1, v2);
	pSink->print("Intel SuperSpeed Enable %#x, Mask %#x\n", v3, v4);
}

static
void printDiagCounters(PrintSink* pSink, int32_t const* pDiagCounters)
{
	if (pDiagCounters[DIAGCTR_SLEEP])
		pSink->print("# Save Errors %u\n", pDiagCounters[DIAGCTR_SLEEP]);
	if (pDiagCounters[DIAGCTR_RESUME])
		pSink->print("# Restore Errors %u\n", pDiagCounters[DIAGCTR_RESUME]);
	if (pDiagCounters[DIAGCTR_BNCEOVRFLW])
		pSink->print("# Event Queue Overflows %u\n", pDiagCounters[DIAGCTR_BNCEOVRFLW]);
	if (pDiagCounters[DIAGCTR_CMDERR])
		pSink->print("# Spurious Command Completion Events %u\n", pDiagCounters[DIAGCTR_CMDERR]);
	if (pDiagCounters[DIAGCTR_XFERERR])
		pSink->print("# Spurious Transfer Events %u\n", pDiagCounters[DIAGCTR_XFERERR]);
	if (pDiagCounters[DIAGCTR_XFERKEEPAWAY])
		pSink->print("# Transfer Ring Keepaways %u\n", pDiagCounters[DIAGCTR_XFERKEEPAWAY]);
	if (pDiagCounters[DIAGCTR_XFERLAYOUT])
		pSink->print("# Transfer Layout Errors %u\n", pDiagCounters[DIAGCTR_XFERLAYOUT]);
	if (pDiagCounters[DIAGCTR_ORPHANEDTDS])
		pSink->print("# Orphaned Transfer Descriptors %u\n", pDiagCounters[DIAGCTR_ORPHANEDTDS]);
	if (pDiagCounters[DIAGCTR_SHORTSUCCESS])
		pSink->print("# Short Transfers with Success Code %u\n", pDiagCounters[DIAGCTR_SHORTSUCCESS]);
}

#pragma mark -
#pragma mark Prink Sink for IOLog
#pragma mark -

static
void IOLogSinkFunction(struct PrintSink*, char const* format, va_list args)
{
	IOLogv(format, args);
}

static
struct PrintSink const IOLogSink = { &IOLogSinkFunction };


__attribute__((visibility("hidden")))
void PrintSink::print(char const* format, ...)
{
	va_list args;
	va_start(args, format);
	printer(this, format, args);
	va_end(args);
}

#pragma mark -
#pragma mark Diagnostics
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::PrintCapRegs(PrintSink* pSink)
{
	uint32_t v;

	if (!pSink)
		pSink = const_cast<PrintSink*>(&IOLogSink);
	printVersions(pSink);
	pSink->print("Vendor %#x, Device %#x, Revision %#x\n", _vendorID, _deviceID, _revisionID);
	pSink->print("CapLength  %u\n", Read8Reg(&_pXHCICapRegisters->CapLength));
	pSink->print("HCIVersion %#x\n", Read16Reg(&_pXHCICapRegisters->HCIVersion));
	v = Read32Reg(&_pXHCICapRegisters->HCSParams[0]);
	pSink->print("MaxSlots %u, MaxIntrs %u, Rsvd(1) %#x, MaxPorts %u\n",
				 XHCI_HCS1_DEVSLOT_MAX(v),
				 XHCI_HCS1_IRQ_MAX(v),
				 (v >> 19) & 0x1FU,
				 XHCI_HCS1_N_PORTS(v));
	v = Read32Reg(&_pXHCICapRegisters->HCSParams[1]);
	pSink->print("IST %u %s, ERST Max %u, Rsvd(2) %#x, SPR %c, Max Scratchpad Bufs %u\n",
				 v & 7U,
				 (v & 8U) ? "frames" : "microframes",
				 1U << XHCI_HCS2_ERST_MAX(v),
				 (v >> 8) & 0x3FFFFU,
				 test_bit(v, 26),
				 XHCI_HCS2_SPB_MAX(v));
	v = Read32Reg(&_pXHCICapRegisters->HCSParams[2]);
	pSink->print("U1 Device Exit Latency %u, Rsvd(3) %#x, U2 Device Exit Latency %u\n",
				 v & 0xFFU,
				 (v >> 8) & 0xFFU,
				 (v >> 16) & 0xFFFFU);
	v = Read32Reg(&_pXHCICapRegisters->HCCParams);
	pSink->print("AC64 %c, BNC %c, CSZ %c, PPC %c, PIND %c, LHRC %c,"
				 " LTC %c, NSS %c, Rsvd(4) %#x, MaxPSASize %u\n",
				 test_bit(v, 0),
				 test_bit(v, 1),
				 test_bit(v, 2),
				 test_bit(v, 3),
				 test_bit(v, 4),
				 test_bit(v, 5),
				 test_bit(v, 6),
				 test_bit(v, 7),
				 (v >> 8) & 0xFU,
				 1U << (1 + XHCI_HCC_PSA_SZ_MAX(v)));
	v = XHCI_HCC_XECP(v);
	if (v) {
		uint32_t volatile* q = reinterpret_cast<uint32_t volatile*>(_pXHCICapRegisters) + v;
		uint32_t psic, psi;
		struct XHCIXECPStruct cap;
		do {
			*reinterpret_cast<uint32_t*>(&cap) = Read32Reg(q);
			pSink->print("  xHC Extended Cap ID %u, Specific %#x\n", cap.capId, cap.capSpecific);
			switch (cap.capId) {
				case 1U:
					pSink->print("    Legacy CTLSTS %#x\n", q[1]);
					break;
				case 2U:
					v = q[2];
					pSink->print("    Supported Protocol Name %#x PortOffset %u PortCount %u ProtocolDefined %#x\n",
								 q[1],
								 v & 0xFFU,
								 (v >> 8) & 0xFFU,
								 (v >> 16) & 0xFFFU);
					psic = (v >> 28) & 0xFU;
					for (uint32_t i = 0U; i < psic; ++i) {
						psi = q[4U + i];
						pSink->print("      PSIV %u PSIE %u PLT %u PFD %c Rsvd %#x Mantissa %u\n",
									 psi & 0xFU,
									 (psi >> 4) & 3U,
									 (psi >> 6) & 3U,
									 test_bit(psi, 8),
									 (psi >> 9) & 0x7FU,
									 (psi >> 16) & 0xFFFFU);
					}
					break;
			}
			q += cap.next;
		} while (cap.next);
	}
	pSink->print("DBOff  %#x\n", Read32Reg(&_pXHCICapRegisters->DBOff));
	pSink->print("RTSOff %#x\n", Read32Reg(&_pXHCICapRegisters->RTSOff));
	pSink->print("PageSize %u\n", (Read32Reg(&_pXHCIOperationalRegisters->PageSize) & 0xFFFFU) << 12);
}

__attribute__((visibility("hidden")))
void CLASS::PrintRuntimeRegs(PrintSink* pSink)
{
	XHCIPortRegisterSet volatile* prs;
	XHCIInterruptRegisterSet volatile* irs;
	uint32_t v, v2, v3;
	uint8_t low3, high3;

	if (!pSink)
		pSink = const_cast<PrintSink*>(&IOLogSink);
	printVersions(pSink);
	pSink->print("Vendor %#x, Device %#x, Revision %#x\n", _vendorID, _deviceID, _revisionID);
	v = Read32Reg(&_pXHCIOperationalRegisters->USBCmd);
	pSink->print("USBCmd RS %c HCRST %c INTE %c HSEE %c LHCRST %c CSS %c CRS %c EWE %c EU3S %c\n",
				 test_bit(v, 0),
				 test_bit(v, 1),
				 test_bit(v, 2),
				 test_bit(v, 3),
				 test_bit(v, 7),
				 test_bit(v, 8),
				 test_bit(v, 9),
				 test_bit(v, 10),
				 test_bit(v, 11));
	v = Read32Reg(&_pXHCIOperationalRegisters->USBSts);
	pSink->print("USBSts HCH %c HSE %c EINT %c PCD %c SSS %c RSS %c SRE %c CNR %c HCE %c\n",
				 test_bit(v, 0),
				 test_bit(v, 2),
				 test_bit(v, 3),
				 test_bit(v, 4),
				 test_bit(v, 8),
				 test_bit(v, 9),
				 test_bit(v, 10),
				 test_bit(v, 11),
				 test_bit(v, 12));
	pSink->print("DNCtrl %#x\n", Read32Reg(&_pXHCIOperationalRegisters->DNCtrl) & 0xFFFFU);
	v = Read32Reg(reinterpret_cast<uint32_t volatile*>(&_pXHCIOperationalRegisters->CRCr));
	pSink->print("CRCr CRR %c\n", test_bit(v, 3));
	pSink->print("Config %u\n", Read32Reg(&_pXHCIOperationalRegisters->Config) & XHCI_CONFIG_SLOTS_MASK);
	pSink->print("MFIndex %u\n", Read32Reg(&_pXHCIRuntimeRegisters->MFIndex) & XHCI_MFINDEX_MASK);
	pSink->print("Last Time Sync xHC %llu milliseconds <-> CPU %llu nanoseconds\n", _millsecondsTimers[3], _millsecondsTimers[1]);
	if (!_expansionData->_controllerCanSleep)
		pSink->print("Will Reset on Resume\n");
	if (_filterInterruptSource && !_filterInterruptSource->getAutoDisable())
		pSink->print("Using MSI\n");
	if ((_errataBits & kErrataIntelPantherPoint) && !(_errataBits & kErrataSWAssistedIdle))
		pSink->print("Intel Doze Disabled\n");
	if (_pUSBLegSup)
		printLegacy(pSink, _pUSBLegSup);
	if (_vendorID == kVendorIntel)
		printIntelMuxRegs(pSink, _device);
	pSink->print("# Configured Endpoints %d\n", _numEndpoints);
	pSink->print("# Interrupts: Total %u, Serviced %u, Inactive %u, Offline %u\n",
				 _interruptCounters[1],
				 _interruptCounters[0],
				 _interruptCounters[2],
				 _interruptCounters[3]);
	printDiagCounters(pSink, &_diagCounters[0]);
	if (_inTestMode)
		pSink->print("Test Mode Active\n");
	if (m_invalid_regspace)
		pSink->print("Disabled due to Invalid Register Access\n");
	if (_HSEDetected)
		pSink->print("Host System Error detected\n");
	low3 = _v3ExpansionData->_rootHubPortsSSStartRange - 1U;
	high3 = low3 + _v3ExpansionData->_rootHubNumPortsSS;
	for (uint8_t port = 0U; port < _rootHubNumPorts; ++port) {
		prs = &_pXHCIOperationalRegisters->prs[port];
		v = Read32Reg(&prs->PortSC);
		pSink->print("Port %3u PortSC CCS %c PED %c OCA %c PR %c\n",
					 1U + port,
					 test_bit(v, 0),
					 test_bit(v, 1),
					 test_bit(v, 3),
					 test_bit(v, 4));
		pSink->print("           PLS %s PP %c Speed %s PIC %s LWS %c\n",
					 stringForPLS(XHCI_PS_PLS_GET(v)),
					 test_bit(v, 9),
					 stringForSpeed(XHCI_PS_SPEED_GET(v)),
					 stringForPIC(XHCI_PS_PIC_GET(v)),
					 test_bit(v, 16));
		pSink->print("           CSC %c PEC %c WRC %c OCC %c PRC %c PLC %c CEC %c CAS %c\n",
					 test_bit(v, 17),
					 test_bit(v, 18),
					 test_bit(v, 19),
					 test_bit(v, 20),
					 test_bit(v, 21),
					 test_bit(v, 22),
					 test_bit(v, 23),
					 test_bit(v, 24));
		pSink->print("           WCE %c WDE %c WOE %c DR %c WPR %c\n",
					 test_bit(v, 25),
					 test_bit(v, 26),
					 test_bit(v, 27),
					 test_bit(v, 30),
					 test_bit(v, 31));
		v = Read32Reg(&prs->PortPmsc);
		if (port >=  low3 && port < high3)
			pSink->print("         PortPmsc U1 %u U2 %u FLA %c PortLi LEC %u\n",
						 v & 0xFFU,
						 (v >> 8) & 0xFFU,
						 test_bit(v, 16),
						 Read32Reg(&prs->PortLi) & 0xFFFFU);
		else
			pSink->print("         PortPmsc L1S %s RWE %c HIRD %u us L1Slot %u HLE %c TestMode %s\n",
						 stringForL1S(v & 7U),
						 test_bit(v, 3),
						 50U + 75U * ((v >> 4) & 15U),
						 (v >> 8) & 255U,
						 test_bit(v, 16),
						 stringForTestMode((v >> 28) & 15U));
	}
	for (uint16_t i = 0U; i < _maxInterrupters; ++i) {
		irs = &_pXHCIRuntimeRegisters->irs[i];
		v = Read32Reg(&irs->iman);
		if (!(v & XHCI_IMAN_INTR_ENA))
			continue;
		v2 = Read32Reg(&irs->imod);
		v3 = Read32Reg(reinterpret_cast<uint32_t volatile*>(&irs->erdp));
		pSink->print("Interrupter %u iman IP %c imod I %u ns C %u ns erstsz %u erdp DESI %u EHB %c\n",
					 i,
					 test_bit(v, 0),
					 250U * (v2 & XHCI_IMOD_IVAL_MASK),
					 250U * (XHCI_IMOD_ICNT_GET(v2)),
					 Read32Reg(&irs->erstsz) & XHCI_ERSTS_MASK,
					 v3 & 7U,
					 test_bit(v3, 3));
	}
}

__attribute__((visibility("hidden")))
void CLASS::PrintSlots(PrintSink* pSink)
{
	ContextStruct* pContext;
	uint32_t TTslot;

	if (!pSink)
		pSink = const_cast<PrintSink*>(&IOLogSink);
	for (uint8_t slot = 1U; slot <= _numSlots; ++slot) {
		if (ConstSlotPtr(slot)->isInactive())
			continue;
		pContext = GetSlotContext(slot);
		TTslot = XHCI_SCTX_2_TT_HUB_SID_GET(pContext->_s.dwSctx2);
		pSink->print("Slot %u, Device Address %u\n",
					 slot,
					 XHCI_SCTX_3_DEV_ADDR_GET(pContext->_s.dwSctx3));
		pSink->print("  State %s\n",
					 stringForSlotState(XHCI_SCTX_3_SLOT_STATE_GET(pContext->_s.dwSctx3)));
		pSink->print("  Route String %#x\n",
					 XHCI_SCTX_0_ROUTE_GET(pContext->_s.dwSctx0));
		pSink->print("  Speed %s, Last Endpoint %u\n",
					 stringForSpeed(XHCI_SCTX_0_SPEED_GET(pContext->_s.dwSctx0)),
					 XHCI_SCTX_0_CTX_NUM_GET(pContext->_s.dwSctx0));
		if (XHCI_SCTX_0_HUB_GET(pContext->_s.dwSctx0)) {
			pSink->print("  Hub Y, # Ports %u, MTT %c, TTT %u FS bit times\n",
						 XHCI_SCTX_1_NUM_PORTS_GET(pContext->_s.dwSctx1),
						 test_bit(pContext->_s.dwSctx0, 25),
						 8U * (1U + XHCI_SCTX_2_TT_THINK_TIME_GET(pContext->_s.dwSctx2)));
			if (TTslot)
				pSink->print("  TT Slot %u, TT Port %u\n",
							 TTslot,
							 XHCI_SCTX_2_TT_PORT_NUM_GET(pContext->_s.dwSctx2));
		} else if (TTslot)
			pSink->print("  MTT %c, TT Slot %u, TT Port %u\n",
						 test_bit(pContext->_s.dwSctx0, 25),
						 TTslot,
						 XHCI_SCTX_2_TT_PORT_NUM_GET(pContext->_s.dwSctx2));
		pSink->print("  RH Port #%u, Interrupter %u, Max Exit Latency %u us\n",
					 XHCI_SCTX_1_RH_PORT_GET(pContext->_s.dwSctx1),
					 XHCI_SCTX_2_IRQ_TARGET_GET(pContext->_s.dwSctx2),
					 XHCI_SCTX_1_MAX_EL_GET(pContext->_s.dwSctx1));
	}
}

__attribute__((visibility("hidden")))
void CLASS::PrintEndpoints(uint8_t slot, PrintSink* pSink)
{
	ContextStruct *pContext, *pEpContext;
	uint32_t numEps, endpoint, epState, maxPSA;

	if (!pSink)
		pSink = const_cast<PrintSink*>(&IOLogSink);
	if (!slot ||
		slot > _numSlots ||
		ConstSlotPtr(slot)->isInactive())
		return;
	pContext = GetSlotContext(slot);
	numEps = XHCI_SCTX_0_CTX_NUM_GET(pContext->_s.dwSctx0);
	for (endpoint = 1U; endpoint <= numEps; ++endpoint) {
		pEpContext = GetSlotContext(slot, endpoint);
		epState = XHCI_EPCTX_0_EPSTATE_GET(pEpContext->_e.dwEpCtx0);
		if (epState == EP_STATE_DISABLED)
			continue;
		pSink->print("Endpoint %u, Type %s, State %s\n",
					 endpoint,
					 stringForEPType(XHCI_EPCTX_1_EPTYPE_GET(pEpContext->_e.dwEpCtx1)),
					 stringForEPState(epState));
		pSink->print("  Multiple %u, Interval %u microframes, CErr %u, Max Burst %u, Max Packet Size %u\n",
					 1U + XHCI_EPCTX_0_MULT_GET(pEpContext->_e.dwEpCtx0),
					 (1U << XHCI_EPCTX_0_IVAL_GET(pEpContext->_e.dwEpCtx0)),
					 XHCI_EPCTX_1_CERR_GET(pEpContext->_e.dwEpCtx1),
					 1U + XHCI_EPCTX_1_MAXB_GET(pEpContext->_e.dwEpCtx1),
					 XHCI_EPCTX_1_MAXP_SIZE_GET(pEpContext->_e.dwEpCtx1));
		pSink->print("  Average TRB Length %u, Max ESIT Payload %u\n",
					 XHCI_EPCTX_4_AVG_TRB_LEN_GET(pEpContext->_e.dwEpCtx4),
					 XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_GET(pEpContext->_e.dwEpCtx4));
		maxPSA = XHCI_EPCTX_0_MAXP_STREAMS_GET(pEpContext->_e.dwEpCtx0);
		if (maxPSA)
			pSink->print("  Streams Endpoint, MaxPSA %u, LSA %c, HID %c\n",
						 2U << maxPSA,
						 test_bit(pEpContext->_e.dwEpCtx0, 15),
						 test_bit(pEpContext->_e.dwEpCtx1, 7));
	}
}

__attribute__((visibility("hidden")))
void CLASS::PrintRootHubPortBandwidth(PrintSink* pSink)
{
	uint8_t buffer[256];
	uint8_t speed;
	size_t siz;
	IOReturn rc;

	if (!pSink)
		pSink = const_cast<PrintSink*>(&IOLogSink);
	for (speed = 0U; speed < 4U; ++speed) {
		siz = sizeof buffer;
		rc = GetPortBandwidth(0U, speed, &buffer[0], &siz);
		if (rc != kIOReturnSuccess) {
			pSink->print("GetPortBandwidth for RootHub, speed %u returned %#x\n", speed, rc);
			continue;
		}
		pSink->print("Bandwidth for RootHub, Speed %u\n", speed);
		for (uint8_t i = 0U; i < _rootHubNumPorts; ++i)
			pSink->print("  %u", buffer[i]);
		pSink->print("\n");
	}
}
