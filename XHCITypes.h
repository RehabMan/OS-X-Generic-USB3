//
//  XHCITypes.h
//  GenericUSBXHCI
//
//  Created by Zenith432 on January 26th, 2013.
//  Copyright (c) 2013 Zenith432. All rights reserved.
//

#ifndef GenericUSBXHCI_XHCITypes_h
#define GenericUSBXHCI_XHCITypes_h

#define EP_STATE_DISABLED	0U
#define EP_STATE_RUNNING	1U
#define EP_STATE_HALTED		2U
#define EP_STATE_STOPPED	3U
#define EP_STATE_ERROR		4U

#define ISOC_OUT_EP	1U
#define BULK_OUT_EP	2U
#define INT_OUT_EP	3U
#define CTRL_EP		4U
#define ISOC_IN_EP	5U
#define BULK_IN_EP	6U
#define INT_IN_EP	7U

#ifdef __cplusplus
extern "C" {
#endif

struct xhci_slot_ctx {
	uint32_t dwSctx0;
#define	XHCI_SCTX_0_ROUTE_SET(x)		((x) & 0xFFFFF)
#define	XHCI_SCTX_0_ROUTE_GET(x)		((x) & 0xFFFFF)
#define	XHCI_SCTX_0_SPEED_SET(x)		(((x) & 0xF) << 20)
#define	XHCI_SCTX_0_SPEED_GET(x)		(((x) >> 20) & 0xF)
#define	XHCI_SCTX_0_MTT_SET(x)			(((x) & 0x1) << 25)
#define	XHCI_SCTX_0_MTT_GET(x)			(((x) >> 25) & 0x1)
#define	XHCI_SCTX_0_HUB_SET(x)			(((x) & 0x1) << 26)
#define	XHCI_SCTX_0_HUB_GET(x)			(((x) >> 26) & 0x1)
#define	XHCI_SCTX_0_CTX_NUM_SET(x)		(((x) & 0x1F) << 27)
#define	XHCI_SCTX_0_CTX_NUM_GET(x)		(((x) >> 27) & 0x1F)
	uint32_t dwSctx1;
#define	XHCI_SCTX_1_MAX_EL_SET(x)		((x) & 0xFFFF)
#define	XHCI_SCTX_1_MAX_EL_GET(x)		((x) & 0xFFFF)
#define	XHCI_SCTX_1_RH_PORT_SET(x)		(((x) & 0xFF) << 16)
#define	XHCI_SCTX_1_RH_PORT_GET(x)		(((x) >> 16) & 0xFF)
#define	XHCI_SCTX_1_NUM_PORTS_SET(x)		(((x) & 0xFF) << 24)
#define	XHCI_SCTX_1_NUM_PORTS_GET(x)		(((x) >> 24) & 0xFF)
	uint32_t dwSctx2;
#define	XHCI_SCTX_2_TT_HUB_SID_SET(x)		((x) & 0xFF)
#define	XHCI_SCTX_2_TT_HUB_SID_GET(x)		((x) & 0xFF)
#define	XHCI_SCTX_2_TT_PORT_NUM_SET(x)		(((x) & 0xFF) << 8)
#define	XHCI_SCTX_2_TT_PORT_NUM_GET(x)		(((x) >> 8) & 0xFF)
#define	XHCI_SCTX_2_TT_THINK_TIME_SET(x)	(((x) & 0x3) << 16)
#define	XHCI_SCTX_2_TT_THINK_TIME_GET(x)	(((x) >> 16) & 0x3)
#define	XHCI_SCTX_2_IRQ_TARGET_SET(x)		(((x) & 0x3FF) << 22)
#define	XHCI_SCTX_2_IRQ_TARGET_GET(x)		(((x) >> 22) & 0x3FF)
	uint32_t dwSctx3;
#define	XHCI_SCTX_3_DEV_ADDR_SET(x)		((x) & 0xFF)
#define	XHCI_SCTX_3_DEV_ADDR_GET(x)		((x) & 0xFF)
#define	XHCI_SCTX_3_SLOT_STATE_SET(x)		(((x) & 0x1F) << 27)
#define	XHCI_SCTX_3_SLOT_STATE_GET(x)		(((x) >> 27) & 0x1F)
	uint32_t pad[4];
};

struct xhci_endp_ctx {
	uint32_t dwEpCtx0;
#define	XHCI_EPCTX_0_EPSTATE_SET(x)		((x) & 0x7)
#define	XHCI_EPCTX_0_EPSTATE_GET(x)		((x) & 0x7)
#define	XHCI_EPCTX_0_MULT_SET(x)		(((x) & 0x3) << 8)
#define	XHCI_EPCTX_0_MULT_GET(x)		(((x) >> 8) & 0x3)
#define	XHCI_EPCTX_0_MAXP_STREAMS_SET(x)	(((x) & 0x1F) << 10)
#define	XHCI_EPCTX_0_MAXP_STREAMS_GET(x)	(((x) >> 10) & 0x1F)
#define	XHCI_EPCTX_0_LSA_SET(x)			(((x) & 0x1) << 15)
#define	XHCI_EPCTX_0_LSA_GET(x)			(((x) >> 15) & 0x1)
#define	XHCI_EPCTX_0_IVAL_SET(x)		(((x) & 0xFF) << 16)
#define	XHCI_EPCTX_0_IVAL_GET(x)		(((x) >> 16) & 0xFF)
	uint32_t dwEpCtx1;
#define	XHCI_EPCTX_1_CERR_SET(x)		(((x) & 0x3) << 1)
#define	XHCI_EPCTX_1_CERR_GET(x)		(((x) >> 1) & 0x3)
#define	XHCI_EPCTX_1_EPTYPE_SET(x)		(((x) & 0x7) << 3)
#define	XHCI_EPCTX_1_EPTYPE_GET(x)		(((x) >> 3) & 0x7)
#define	XHCI_EPCTX_1_HID_SET(x)			(((x) & 0x1) << 7)
#define	XHCI_EPCTX_1_HID_GET(x)			(((x) >> 7) & 0x1)
#define	XHCI_EPCTX_1_MAXB_SET(x)		(((x) & 0xFF) << 8)
#define	XHCI_EPCTX_1_MAXB_GET(x)		(((x) >> 8) & 0xFF)
#define	XHCI_EPCTX_1_MAXP_SIZE_SET(x)		(((x) & 0xFFFF) << 16)
#define	XHCI_EPCTX_1_MAXP_SIZE_GET(x)		(((x) >> 16) & 0xFFFF)
	uint64_t qwEpCtx2;
#define	XHCI_EPCTX_2_DCS_SET(x)			((x) & 0x1)
#define	XHCI_EPCTX_2_DCS_GET(x)			((x) & 0x1)
#define	XHCI_EPCTX_2_TR_DQ_PTR_MASK		0xFFFFFFFFFFFFFFF0ULL
	uint32_t dwEpCtx4;
#define	XHCI_EPCTX_4_AVG_TRB_LEN_SET(x)		((x) & 0xFFFF)
#define	XHCI_EPCTX_4_AVG_TRB_LEN_GET(x)		((x) & 0xFFFF)
#define	XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_SET(x)	(((x) & 0xFFFF) << 16)
#define	XHCI_EPCTX_4_MAX_ESIT_PAYLOAD_GET(x)	(((x) >> 16) & 0xFFFF)
	uint32_t pad[3];
};

struct xhci_input_control_ctx {
#define	XHCI_INCTX_NON_CTRL_MASK	0xFFFFFFFCU
	uint32_t dwInCtx0;
#define	XHCI_INCTX_0_DROP_MASK(n)	(1U << (n))
	uint32_t dwInCtx1;
#define	XHCI_INCTX_1_ADD_MASK(n)	(1U << (n))
	uint32_t pad[6];
};

union ContextStruct {
	struct xhci_slot_ctx _s;
	struct xhci_endp_ctx _e;
	struct xhci_input_control_ctx _ic;
};

struct xhci_stream_ctx {
	uint64_t qwSctx0;
#define	XHCI_SCTX_0_DCS_GET(x)		((x) & 0x1)
#define	XHCI_SCTX_0_DCS_SET(x)		((x) & 0x1)
#define	XHCI_SCTX_0_SCT_SET(x)		(((x) & 0x7) << 1)
#define	XHCI_SCTX_0_SCT_GET(x)		(((x) >> 1) & 0x7)
#define	XHCI_SCTX_0_SCT_SEC_TR_RING	0x0
#define	XHCI_SCTX_0_SCT_PRIM_TR_RING	0x1
#define	XHCI_SCTX_0_SCT_PRIM_SSA_8	0x2
#define	XHCI_SCTX_0_SCT_PRIM_SSA_16	0x3
#define	XHCI_SCTX_0_SCT_PRIM_SSA_32	0x4
#define	XHCI_SCTX_0_SCT_PRIM_SSA_64	0x5
#define	XHCI_SCTX_0_SCT_PRIM_SSA_128	0x6
#define	XHCI_SCTX_0_SCT_PRIM_SSA_256	0x7
#define	XHCI_SCTX_0_TR_DQ_PTR_MASK	0xFFFFFFFFFFFFFFF0ULL
	uint32_t dwSctx2;
	uint32_t dwSctx3;
};

struct EventRingSegmentTable
{
	uint64_t qwEvrsTablePtr;
	uint32_t dwEvrsTableSize;
	uint32_t dwEvrsReserved;
};

#ifdef __cplusplus
}
#endif

#endif
