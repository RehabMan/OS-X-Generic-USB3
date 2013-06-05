//
//  XHCIRegs.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on December 14th 2012.
//  Copyright (c) 2012-2013 Zenith432. All rights reserved.
//

#ifndef __XHCIREGS_H__
#define __XHCIREGS_H__

#define	PCI_XHCI_INTEL_XHCC 0x40U		/* Intel xHC System Bus Configuration Register */
#define	PCI_XHCI_INTEL_XHCC_SWAXHCI	(1U << 11)
#define	PCI_XHCI_INTEL_XHCC_SWAXHCIP_SET(x) (((x) & 3U) << 12)
#define	PCI_XHCI_INTEL_XUSB2PR	0xD0U	/* Intel USB2 Port Routing */
#define	PCI_XHCI_INTEL_XUSB2PRM	0xD4U	/* Intel USB2 Port Routing Mask */
#define	PCI_XHCI_INTEL_USB3_PSSEN 0xD8U	/* Intel USB3 Port SuperSpeed Enable */
#define	PCI_XHCI_INTEL_USB3PRM	0xDCU	/* Intel USB3 Port Routing Mask */

#ifdef __cplusplus
extern "C" {
#endif
	
struct XHCICapRegisters
{
	uint8_t  CapLength;
	uint8_t  Rsvd;
	uint16_t HCIVersion;
	uint32_t HCSParams[3];
	uint32_t HCCParams;
	uint32_t DBOff;
	uint32_t RTSOff;
};

/* XHCI capability bits */
#define	XHCI_HCS1_DEVSLOT_MAX(x)((x) & 0xFFU)
#define	XHCI_HCS1_IRQ_MAX(x)	(((x) >> 8) & 0x7FFU)
#define	XHCI_HCS1_N_PORTS(x)	(((x) >> 24) & 0xFFU)
#define	XHCI_HCS2_IST(x)	((x) & 0xFU)
#define	XHCI_HCS2_ERST_MAX(x)	(((x) >> 4) & 0xFU)
#define	XHCI_HCS2_SPB_MAX(x)	(((x) >> 27) & 0x1FU)
#define	XHCI_HCC_AC64(x)	((x) & 0x1U)		/* 64-bit capable */
#define	XHCI_HCC_BNC(x)	(((x) >> 1) & 0x1U)	/* BW negotiation */
#define	XHCI_HCC_CSZ(x)	(((x) >> 2) & 0x1U)	/* context size */
#define	XHCI_HCC_PPC(x)	(((x) >> 3) & 0x1U)	/* port power control */
#define	XHCI_HCC_PIND(x)	(((x) >> 4) & 0x1U)	/* port indicators */
#define	XHCI_HCC_LHRC(x)	(((x) >> 5) & 0x1U)	/* light HC reset */
#define	XHCI_HCC_LTC(x)	(((x) >> 6) & 0x1U)	/* latency tolerance msg */
#define	XHCI_HCC_NSS(x)	(((x) >> 7) & 0x1U)	/* no secondary sid */
#define	XHCI_HCC_PSA_SZ_MAX(x)	(((x) >> 12) & 0xFU)	/* max pri. stream array size */
#define	XHCI_HCC_XECP(x)	(((x) >> 16) & 0xFFFFU)	/* extended capabilities pointer */

struct XHCIPortRegisterSet
{
	uint32_t PortSC;
	uint32_t PortPmsc;
	uint32_t PortLi;
	uint32_t RsvdZ;
};

struct XHCIOpRegisters
{
	uint32_t USBCmd;
	uint32_t USBSts;
	uint32_t PageSize;
	uint32_t RsvdZ1[2];
	uint32_t DNCtrl;
	uint64_t CRCr;
	uint32_t RsvdZ2[4];
	uint64_t DCBAap;
	uint32_t Config;
	uint32_t RsvdZ3[241];
	struct XHCIPortRegisterSet prs[0];
};

struct XHCIOpRegistersUnpadded
{
	uint32_t USBCmd;
	uint32_t USBSts;
	uint32_t PageSize;
	uint32_t RsvdZ1[2];
	uint32_t DNCtrl;
	uint64_t CRCr;
	uint32_t RsvdZ2[4];
	uint64_t DCBAap;
	uint32_t Config;
};

/* XHCI operational bits */
#define	XHCI_CMD_RS		0x00000001U	/* RW Run/Stop */
#define	XHCI_CMD_HCRST	0x00000002U	/* RW Host Controller Reset */
#define	XHCI_CMD_INTE	0x00000004U	/* RW Interrupter Enable */
#define	XHCI_CMD_HSEE	0x00000008U	/* RW Host System Error Enable */
#define	XHCI_CMD_LHCRST	0x00000080U	/* RO/RW Light Host Controller Reset */
#define	XHCI_CMD_CSS	0x00000100U	/* RW Controller Save State */
#define	XHCI_CMD_CRS	0x00000200U	/* RW Controller Restore State */
#define	XHCI_CMD_EWE	0x00000400U	/* RW Enable Wrap Event */
#define	XHCI_CMD_EU3S	0x00000800U	/* RW Enable U3 MFINDEX Stop */
#define	XHCI_STS_HCH	0x00000001U	/* RO - Host Controller Halted */
#define	XHCI_STS_HSE	0x00000004U	/* RW1C - Host System Error */
#define	XHCI_STS_EINT	0x00000008U	/* RW1C - Event Interrupt */
#define	XHCI_STS_PCD	0x00000010U	/* RW1C - Port Change Detect */
#define	XHCI_STS_SSS	0x00000100U	/* RO - Save State Status */
#define	XHCI_STS_RSS	0x00000200U	/* RO - Restore State Status */
#define	XHCI_STS_SRE	0x00000400U	/* RW1C - Save/Restore Error */
#define	XHCI_STS_CNR	0x00000800U	/* RO - Controller Not Ready */
#define	XHCI_STS_HCE	0x00001000U	/* RO - Host Controller Error */
#define	XHCI_CRCR_LO_RCS	0x00000001ULL	/* RW - Ring Cycle State */
#define	XHCI_CRCR_LO_CS		0x00000002ULL	/* RW1S - Command Stop */
#define	XHCI_CRCR_LO_CA		0x00000004ULL	/* RW1S - Command Abort */
#define	XHCI_CRCR_LO_CRR	0x00000008ULL	/* RO - Command Ring Running */
#define	XHCI_CRCR_LO_MASK	0x0000003FULL
#define	XHCI_CONFIG_SLOTS_MASK	0x000000FFU	/* RW - number of device slots enabled */

/* XHCI port status bits (sticky) */
#define	XHCI_PS_CCS		0x00000001U	/* RO - current connect status */
#define	XHCI_PS_PED		0x00000002U	/* RW1C - port enabled / disabled */
#define	XHCI_PS_OCA		0x00000008U	/* RO - over current active */
#define	XHCI_PS_PR		0x00000010U	/* RW1S - port reset */
#define	XHCI_PS_PLS_GET(x)	(((x) >> 5) & 0xFU)	/* RW - port link state */
#define	XHCI_PS_PLS_SET(x)	(((x) & 0xFU) << 5)	/* RW - port link state */
#define	XHCI_PS_PP		0x00000200U	/* RW - port power */
#define	XHCI_PS_SPEED_GET(x)	(((x) >> 10) & 0xFU)	/* RO - port speed */
#define	XHCI_PS_PIC_GET(x)	(((x) >> 14) & 0x3U)	/* RW - port indicator */
#define	XHCI_PS_PIC_SET(x)	(((x) & 0x3U) << 14)	/* RW - port indicator */
#define	XHCI_PS_LWS		0x00010000U	/* RW1S - port link state write strobe */
#define	XHCI_PS_CSC		0x00020000U	/* RW1C - connect status change */
#define	XHCI_PS_PEC		0x00040000U	/* RW1C - port enable/disable change */
#define	XHCI_PS_WRC		0x00080000U	/* RW1C - warm port reset change (RsvdZ for USB2 ports) */
#define	XHCI_PS_OCC		0x00100000U	/* RW1C - over-current change */
#define	XHCI_PS_PRC		0x00200000U	/* RW1C - port reset change */
#define	XHCI_PS_PLC		0x00400000U	/* RW1C - port link state change */
#define	XHCI_PS_CEC		0x00800000U	/* RW1C - config error change (RsvdZ for USB2 ports) */
#define	XHCI_PS_CAS		0x01000000U	/* RO - cold attach status */
#define	XHCI_PS_WCE		0x02000000U	/* RW - wake on connect enable */
#define	XHCI_PS_WDE		0x04000000U	/* RW - wake on disconnect enable */
#define	XHCI_PS_WOE		0x08000000U	/* RW - wake on over-current enable */
#define	XHCI_PS_DR		0x40000000U	/* RO - device removable */
#define	XHCI_PS_WPR		0x80000000U	/* RW1S - warm port reset (RsvdZ for USB2 ports) */
#define	XHCI_PS_CLEAR	0x80FF01FFU	/* command bits */
#define	XHCI_PS_WAKEBITS (XHCI_PS_WCE | XHCI_PS_WDE | XHCI_PS_WOE)
#define	XHCI_PS_WRITEBACK_MASK (XHCI_PS_PP | XHCI_PS_PIC_SET(3U) | XHCI_PS_WAKEBITS)
#define	XHCI_PS_CHANGEBITS (XHCI_PS_CSC | XHCI_PS_PEC | XHCI_PS_WRC | XHCI_PS_OCC | XHCI_PS_PRC | XHCI_PS_PLC | XHCI_PS_CEC)
#define	XDEV_U0			0U
#define	XDEV_U3			3U
#define	XDEV_DISABLED	4U
#define	XDEV_RXDETECT	5U
#define	XDEV_INACTIVE	6U
#define	XDEV_POLLING	7U
#define	XDEV_RECOVERY	8U
#define	XDEV_HOTRESET	9U
#define	XDEV_COMPLIANCE	10U
#define	XDEV_TEST		11U
#define	XDEV_RESUME		15U
#define	XDEV_FS			1U
#define	XDEV_LS			2U
#define	XDEV_HS			3U
#define	XDEV_SS			4U

struct XHCIInterruptRegisterSet
{
	uint32_t iman;
	uint32_t imod;
	uint32_t erstsz;
	uint32_t RsvdP;
	uint64_t erstba;
	uint64_t erdp;
};

struct XHCIRuntimeRegisters
{
	uint32_t MFIndex;
	uint32_t RsvdZ[7];
	struct XHCIInterruptRegisterSet irs[0];
};

#define	XHCI_MFINDEX_MASK	0x00003FFFU	/* RO - Microframe Index */
#define	XHCI_IMAN_INTR_PEND	0x00000001U	/* RW1C - interrupt pending */
#define	XHCI_IMAN_INTR_ENA	0x00000002U	/* RW - interrupt enable */
#define	XHCI_IMOD_IVAL_MASK	0xFFFFU	/* RW - 250ns unit */
#define	XHCI_IMOD_ICNT_GET(x)	(((x) >> 16) & 0xFFFFU)	/* RW - 250ns unit */
#define	XHCI_IMOD_ICNT_SET(x)	(((x) & 0xFFFFU) << 16)	/* RW - 250ns unit */
#define	XHCI_ERSTS_MASK		0x0000FFFFU	/* RW - Event Ring Segment Table Size */
#define	XHCI_ERDP_LO_SINDEX(x)	((x) & 0x7ULL)	/* RW - dequeue segment index */
#define	XHCI_ERDP_LO_BUSY	0x00000008ULL	/* RW1C - event handler busy */

struct XHCIXECPStruct
{
	uint8_t capId;
	uint8_t next;
	uint16_t capSpecific;
};

struct XHCIXECPStruct_SP
{
	uint8_t capId;
	uint8_t next;
	uint8_t revisionMinor;
	uint8_t revisionMajor;
	uint32_t nameString;
	uint8_t compatiblePortOffset;
	uint8_t compatiblePortCount;
	uint16_t Rsvd;
};

#define	XHCI_HC_BIOS_OWNED	(1U << 16)
#define	XHCI_HC_OS_OWNED	(1U << 24)
#define	XHCI_LEGACY_DISABLE_SMI	((7U << 1) | (255U << 5) | (7U << 17))
#define	XHCI_LEGACY_SMI_EVENTS	(7U << 29)

#ifdef __cplusplus
}
#endif

#endif
