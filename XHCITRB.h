//
//  XHCITRB.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 26th, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#ifndef GenericUSBXHCI_XHCITRB_h
#define GenericUSBXHCI_XHCITRB_h

/* Commands */
#define	XHCI_TRB_TYPE_RESERVED		0x00
#define	XHCI_TRB_TYPE_NORMAL		0x01
#define	XHCI_TRB_TYPE_SETUP_STAGE	0x02
#define	XHCI_TRB_TYPE_DATA_STAGE	0x03
#define	XHCI_TRB_TYPE_STATUS_STAGE	0x04
#define	XHCI_TRB_TYPE_ISOCH		0x05
#define	XHCI_TRB_TYPE_LINK		0x06
#define	XHCI_TRB_TYPE_EVENT_DATA	0x07
#define	XHCI_TRB_TYPE_NOOP		0x08
#define	XHCI_TRB_TYPE_ENABLE_SLOT	0x09
#define	XHCI_TRB_TYPE_DISABLE_SLOT	0x0A
#define	XHCI_TRB_TYPE_ADDRESS_DEVICE	0x0B
#define	XHCI_TRB_TYPE_CONFIGURE_EP	0x0C
#define	XHCI_TRB_TYPE_EVALUATE_CTX	0x0D
#define	XHCI_TRB_TYPE_RESET_EP		0x0E
#define	XHCI_TRB_TYPE_STOP_EP		0x0F
#define	XHCI_TRB_TYPE_SET_TR_DEQUEUE	0x10
#define	XHCI_TRB_TYPE_RESET_DEVICE	0x11
#define	XHCI_TRB_TYPE_FORCE_EVENT	0x12
#define	XHCI_TRB_TYPE_NEGOTIATE_BW	0x13
#define	XHCI_TRB_TYPE_SET_LATENCY_TOL  	0x14
#define	XHCI_TRB_TYPE_GET_PORT_BW	0x15
#define	XHCI_TRB_TYPE_FORCE_HEADER	0x16
#define	XHCI_TRB_TYPE_NOOP_CMD		0x17
#define	TRB_RENESAS_GET_FW		49

/* Events */
#define	XHCI_TRB_EVENT_TRANSFER		0x20
#define	XHCI_TRB_EVENT_CMD_COMPLETE	0x21
#define	XHCI_TRB_EVENT_PORT_STS_CHANGE  0x22
#define	XHCI_TRB_EVENT_BW_REQUEST      	0x23
#define	XHCI_TRB_EVENT_DOORBELL		0x24
#define	XHCI_TRB_EVENT_HOST_CTRL	0x25
#define	XHCI_TRB_EVENT_DEVICE_NOTIFY	0x26
#define	XHCI_TRB_EVENT_MFINDEX_WRAP	0x27
#define	TRB_RENESAS_CMD_COMP 48

/* Error codes */
#define	XHCI_TRB_ERROR_INVALID		0x00
#define	XHCI_TRB_ERROR_SUCCESS		0x01
#define	XHCI_TRB_ERROR_DATA_BUF		0x02
#define	XHCI_TRB_ERROR_BABBLE		0x03
#define	XHCI_TRB_ERROR_XACT		0x04
#define	XHCI_TRB_ERROR_TRB		0x05
#define	XHCI_TRB_ERROR_STALL		0x06
#define	XHCI_TRB_ERROR_RESOURCE		0x07
#define	XHCI_TRB_ERROR_BANDWIDTH	0x08
#define	XHCI_TRB_ERROR_NO_SLOTS		0x09
#define	XHCI_TRB_ERROR_STREAM_TYPE	0x0A
#define	XHCI_TRB_ERROR_SLOT_NOT_ON	0x0B
#define	XHCI_TRB_ERROR_ENDP_NOT_ON	0x0C
#define	XHCI_TRB_ERROR_SHORT_PKT	0x0D
#define	XHCI_TRB_ERROR_RING_UNDERRUN	0x0E
#define	XHCI_TRB_ERROR_RING_OVERRUN	0x0F
#define	XHCI_TRB_ERROR_VF_RING_FULL	0x10
#define	XHCI_TRB_ERROR_PARAMETER	0x11
#define	XHCI_TRB_ERROR_BW_OVERRUN	0x12
#define	XHCI_TRB_ERROR_CONTEXT_STATE	0x13
#define	XHCI_TRB_ERROR_NO_PING_RESP	0x14
#define	XHCI_TRB_ERROR_EV_RING_FULL	0x15
#define	XHCI_TRB_ERROR_INCOMPAT_DEV	0x16
#define	XHCI_TRB_ERROR_MISSED_SERVICE	0x17
#define	XHCI_TRB_ERROR_CMD_RING_STOP	0x18
#define	XHCI_TRB_ERROR_CMD_ABORTED	0x19
#define	XHCI_TRB_ERROR_STOPPED		0x1A
#define	XHCI_TRB_ERROR_LENGTH		0x1B
#define	XHCI_TRB_ERROR_BAD_MELAT	0x1D
#define	XHCI_TRB_ERROR_ISOC_OVERRUN	0x1F
#define	XHCI_TRB_ERROR_EVENT_LOST	0x20
#define	XHCI_TRB_ERROR_UNDEFINED	0x21
#define	XHCI_TRB_ERROR_INVALID_SID	0x22
#define	XHCI_TRB_ERROR_SEC_BW		0x23
#define	XHCI_TRB_ERROR_SPLIT_XACT	0x24

#ifdef __cplusplus
extern "C" {
#endif

struct TRBStruct
{
	uint32_t a;
	uint32_t b;
#define	XHCI_TRB_1_WLENGTH_MASK		(0xFFFFU << 16)
	uint32_t c;
#define	XHCI_TRB_2_ERROR_GET(x)		(((x) >> 24) & 0xFF)
#define	XHCI_TRB_2_ERROR_SET(x)		(((x) & 0xFF) << 24)
#define	XHCI_TRB_2_TDSZ_GET(x)		(((x) >> 17) & 0x1F)
#define	XHCI_TRB_2_TDSZ_SET(x)		(((x) & 0x1F) << 17)
#define	XHCI_TRB_2_REM_GET(x)		((x) & 0xFFFFFF)
#define	XHCI_TRB_2_REM_SET(x)		((x) & 0xFFFFFF)
#define	XHCI_TRB_2_BYTES_GET(x)		((x) & 0x1FFFF)
#define	XHCI_TRB_2_BYTES_SET(x)		((x) & 0x1FFFF)
#define	XHCI_TRB_2_IRQ_GET(x)		(((x) >> 22) & 0x3FF)
#define	XHCI_TRB_2_IRQ_SET(x)		(((x) & 0x3FF) << 22)
#define	XHCI_TRB_2_STREAM_GET(x)	(((x) >> 16) & 0xFFFF)
#define	XHCI_TRB_2_STREAM_SET(x)	(((x) & 0xFFFF) << 16)
	uint32_t d;
#define	XHCI_TRB_3_TYPE_GET(x)		(((x) >> 10) & 0x3F)
#define	XHCI_TRB_3_TYPE_SET(x)		(((x) & 0x3F) << 10)
#define	XHCI_TRB_3_CYCLE_BIT		(1U << 0)
#define	XHCI_TRB_3_TC_BIT		(1U << 1)	/* command ring only */
#define	XHCI_TRB_3_ENT_BIT		(1U << 1)	/* transfer ring only */
#define	XHCI_TRB_3_ISP_BIT		(1U << 2)
#define	XHCI_TRB_3_ED_BIT		(1U << 2)
#define	XHCI_TRB_3_NSNOOP_BIT		(1U << 3)
#define	XHCI_TRB_3_CHAIN_BIT		(1U << 4)
#define	XHCI_TRB_3_IOC_BIT		(1U << 5)
#define	XHCI_TRB_3_IDT_BIT		(1U << 6)
#define	XHCI_TRB_3_TBC_GET(x)		(((x) >> 7) & 3)
#define	XHCI_TRB_3_TBC_SET(x)		(((x) & 3) << 7)
#define	XHCI_TRB_3_BEI_BIT		(1U << 9)
#define	XHCI_TRB_3_DCEP_BIT		(1U << 9)
#define	XHCI_TRB_3_PRSV_BIT		(1U << 9)
#define	XHCI_TRB_3_BSR_BIT		(1U << 9)
#define	XHCI_TRB_3_TRT_MASK		(3U << 16)
#define	XHCI_TRB_3_TRT_NONE		(0U << 16)
#define	XHCI_TRB_3_TRT_OUT		(2U << 16)
#define	XHCI_TRB_3_TRT_IN		(3U << 16)
#define	XHCI_TRB_3_DIR_IN		(1U << 16)
#define	XHCI_TRB_3_TLBPC_GET(x)		(((x) >> 16) & 0xF)
#define	XHCI_TRB_3_TLBPC_SET(x)		(((x) & 0xF) << 16)
#define	XHCI_TRB_3_EP_GET(x)		(((x) >> 16) & 0x1F)
#define	XHCI_TRB_3_EP_SET(x)		(((x) & 0x1F) << 16)
#define	XHCI_TRB_3_FRID_GET(x)		(((x) >> 20) & 0x7FF)
#define	XHCI_TRB_3_FRID_SET(x)		(((x) & 0x7FF) << 20)
#define	XHCI_TRB_3_ISO_SIA_BIT		(1U << 31)
#define	XHCI_TRB_3_SUSP_EP_BIT		(1U << 23)
#define	XHCI_TRB_3_SLOT_GET(x)		(((x) >> 24) & 0xFF)
#define	XHCI_TRB_3_SLOT_SET(x)		(((x) & 0xFF) << 24)
};

struct SetupStageHeader
{
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

#ifdef __cplusplus
}
#endif

#endif
