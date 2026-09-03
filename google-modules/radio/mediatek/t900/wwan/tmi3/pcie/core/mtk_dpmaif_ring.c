// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#define pr_fmt(fmt) "DATA_RING:" fmt

#include <asm/delay.h>
#include <linux/bitfield.h>
#include <linux/printk.h>
#include "mtk_dpmaif_drv.h"
#include "mtk_dpmaif_ring.h"

#define TAG "DATA_RING"

#define PIT_PD_DATA_LEN		GENMASK(31, 16) /* Indicates the data length of current packet. */
#define PIT_PD_BUF_ID		GENMASK(15, 3) /* The low order of buffer index */
#define PIT_PD_BUF_TYPE		BIT(2) /* 0b: normal BAT entry; 1b: fragment BAT entry */
#define PIT_PD_CONT		BIT(1) /* 0b: last entry; 1b: more entry */
#define PIT_PD_PKT_TYPE		BIT(0) /* 0b: normal PIT entry; 1b: message PIT entry */

#define PIT_PD_DLQ_DONE		GENMASK(31, 30)
#define PIT_PD_ULQ_DONE		GENMASK(29, 24)
/* The header length of transport layer and internet layer. */
#define PIT_PD_HD_OFFSET	GENMASK(23, 19)
#define PIT_PD_BI_F		GENMASK(18, 17)
#define PIT_PD_IG		BIT(16)
#define PIT_PD_RSV		GENMASK(15, 11)
#define PIT_PD_H_BID		GENMASK(10, 8) /* The high order of buffer index */
#define PIT_PD_SEQ		GENMASK(7, 0) /* PIT sequence */

#define PIT_MSG_DP		BIT(31) /* Indicates software to drop this packet if set. */
#define PIT_MSG_DW1_RSV1	GENMASK(30, 27)
#define PIT_MSG_NET_TYPE	GENMASK(26, 24)
#define PIT_MSG_CHNL_ID		GENMASK(23, 16) /* channel index */
#define PIT_MSG_DW1_RSV2	GENMASK(15, 12)
#define PIT_MSG_HPC_IDX		GENMASK(11, 8)
#define PIT_MSG_SRC_QID		GENMASK(7, 5)
#define PIT_MSG_ERR		BIT(4)
#define PIT_MSG_CHECKSUM	GENMASK(3, 2)
#define PIT_MSG_CONT		BIT(1) /* 0b: last entry; 1b: more entry */
#define PIT_MSG_PKT_TYPE	BIT(0) /* 0b: normal PIT entry; 1b: message PIT entry */

#define PIT_MSG_HP_IDX		GENMASK(31, 27)
#define PIT_MSG_CMD		GENMASK(26, 24)
#define PIT_MSG_DW2_RSV		GENMASK(23, 21)
#define PIT_MSG_FLOW		GENMASK(20, 16)
#define PIT_MSG_COUNT_L		GENMASK(15, 0)

#define PIT_MSG_HASH		GENMASK(31, 24) /* Hash value calculated by Hardware using packet */
#define PIT_MSG_DW3_RSV1	GENMASK(23, 18)
#define PIT_MSG_PRO		GENMASK(17, 16)
#define PIT_MSG_VBID		GENMASK(15, 3)
#define PIT_MSG_DW3_RSV2	GENMASK(2, 0)

#define PIT_MSG_DLQ_DONE	GENMASK(31, 30)
#define PIT_MSG_ULQ_DONE	GENMASK(29, 24)
#define PIT_MSG_IP		BIT(23)
#define PIT_MSG_DW4_RSV1	BIT(22)
#define PIT_MSG_MR		GENMASK(21, 20)
#define PIT_MSG_DW4_RSV2	GENMASK(19, 17)
#define PIT_MSG_IG		BIT(16)
#define PIT_MSG_DW4_RSV3	GENMASK(15, 11)
#define PIT_MSG_H_BID		GENMASK(10, 8)
/* An incremental number for each PIT, updated for each PIT entries.
 * It is reset to 0 when its value reaches the maximum value.
 */
#define PIT_MSG_PIT_SEQ		GENMASK(7, 0)

#define DPMAIF_POLL_STEP 20
#define DPMAIF_POLL_PIT_CNT_MAX 100

#define DRB_MSG_PKT_LEN		GENMASK(31, 16) /* The length of a whole packet. */
#define DRB_MSG_DW1_RSV		GENMASK(15, 3)
#define DRB_MSG_CONT		BIT(2) /* 0b: last entry; 1b: more entry */
#define DRB_MSG_DTYP		GENMASK(1, 0) /* 00b: normal DRB entry; 01b: message DRB entry */

#define DRB_MSG_DW2_RSV1	GENMASK(31, 30)
#define DRB_MSG_L4_CHK		BIT(29) /* 0b: disable layer4 checksum offload; 1b: enable */
#define DRB_MSG_IP_CHK		BIT(28) /* 0b: disable IP checksum, 1b: enable IP checksum */
#define DRB_MSG_DW2_RSV2	BIT(27)
#define DRB_MSG_NET_TYPE	GENMASK(26, 24)
#define DRB_MSG_CHNL_ID		GENMASK(23, 16) /* channel index */
#define DRB_MSG_COUNT_L		GENMASK(15, 0)

#define DRB_PD_DATA_LEN		GENMASK(31, 16) /* the length of a payload. */
#define DRB_PD_RSV		GENMASK(15, 3)
#define DRB_PD_CONT		BIT(2)/* 0b: last entry; 1b: more entry */
#define DRB_PD_DTYP		GENMASK(1, 0) /* 00b: normal DRB entry; 01b: message DRB entry. */

int mtk_dpmaif_get_rx_info(void *pit, struct dpmaif_rx_info *rx_info, u32 pit_seq_expect,
			   u8 q_id)
{
	struct dpmaif_pd_pit *pd_pit = (struct dpmaif_pd_pit *)pit;
	int ret = -DATA_PIT_SEQ_CHK_FAIL;
	struct dpmaif_msg_pit *msg_pit;
	u64 dma_addr;
	u32 cnt = 0;

	/* The longest check time is 2ms, step is 20us */
	do {
		rx_info->pit_pd_seq = FIELD_GET(PIT_PD_SEQ, le32_to_cpu(pd_pit->pd_footer));
		if (rx_info->pit_pd_seq == pit_seq_expect) {
			ret = 0;
			break;
		}

		udelay(DPMAIF_POLL_STEP);
	} while (++cnt < DPMAIF_POLL_PIT_CNT_MAX);

	if (unlikely(ret < 0))
		goto out;

	rx_info->msg_pit = FIELD_GET(PIT_PD_PKT_TYPE, le32_to_cpu(pd_pit->pd_header));
	if (rx_info->msg_pit) {
		trace_mtk_tput_data_rx(q_id, "m");
		msg_pit = (struct dpmaif_msg_pit *)pit;
		rx_info->pit_msg_chnl_id = FIELD_GET(PIT_MSG_CHNL_ID, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_checksum = FIELD_GET(PIT_MSG_CHECKSUM,
						      le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_err = FIELD_GET(PIT_MSG_ERR, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_dp = FIELD_GET(PIT_MSG_DP, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_hash = FIELD_GET(PIT_MSG_HASH, le32_to_cpu(msg_pit->dword3));
		rx_info->pit_msg_pro = FIELD_GET(PIT_MSG_PRO, le32_to_cpu(msg_pit->dword3));
		rx_info->pit_msg_ip = FIELD_GET(PIT_MSG_IP, le32_to_cpu(msg_pit->dword4));
	} else {
		trace_mtk_tput_data_rx(q_id, "n");
		rx_info->normal_bat = FIELD_GET(PIT_PD_BUF_TYPE, le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_cur_bid = (FIELD_GET(PIT_PD_H_BID,
				le32_to_cpu(pd_pit->pd_footer)) << 13) +
				FIELD_GET(PIT_PD_BUF_ID, le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_data_len = FIELD_GET(PIT_PD_DATA_LEN,
						     le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_hd_offset = FIELD_GET(PIT_PD_HD_OFFSET,
						      le32_to_cpu(pd_pit->pd_footer)) << 2;
		rx_info->pit_continue = FIELD_GET(PIT_PD_CONT,
						  le32_to_cpu(pd_pit->pd_header));
		dma_addr = le32_to_cpu(pd_pit->addr_high);
		rx_info->pit_pd_dma_addr = (dma_addr << 32) + le32_to_cpu(pd_pit->addr_low);
	}

out:
	return ret;
}

void mtk_dpmaif_fill_tx_info(void *drb, struct dpmaif_tx_info *tx_info, int type)
{
	struct dpmaif_msg_drb *msg_drb;
	struct dpmaif_pd_drb *pd_drb;

	if (type == MSG_DRB) {
		msg_drb = (struct dpmaif_msg_drb *)drb;
		msg_drb->msg_header1 = cpu_to_le32(FIELD_PREP(DRB_MSG_DTYP, MSG_DRB) |
			       FIELD_PREP(DRB_MSG_CONT, DPMAIF_DRB_MORE) |
			       FIELD_PREP(DRB_MSG_PKT_LEN, tx_info->msg_pkt_len));
		msg_drb->msg_header2 = cpu_to_le32(FIELD_PREP(DRB_MSG_COUNT_L,
							      tx_info->msg_count_l) |
			FIELD_PREP(DRB_MSG_CHNL_ID, tx_info->msg_channel_id) |
			FIELD_PREP(DRB_MSG_L4_CHK, tx_info->msg_txcsum) |
			FIELD_PREP(DRB_MSG_NET_TYPE, tx_info->msg_network_type));
	} else {
		pd_drb = (struct dpmaif_pd_drb *)drb;
		pd_drb->pd_header = cpu_to_le32(FIELD_PREP(DRB_PD_DTYP, PD_DRB));
		if (tx_info->pd_is_last)
			pd_drb->pd_header |= cpu_to_le32(FIELD_PREP(DRB_PD_CONT,
					DPMAIF_DRB_LASTONE));
		else
			pd_drb->pd_header |= cpu_to_le32(FIELD_PREP(DRB_PD_CONT, DPMAIF_DRB_MORE));

		pd_drb->pd_header |= cpu_to_le32(FIELD_PREP(DRB_PD_DATA_LEN, tx_info->pd_data_len));
		pd_drb->addr_low = cpu_to_le32(lower_32_bits(tx_info->pd_data_dma_addr));
		pd_drb->addr_high = cpu_to_le32(upper_32_bits(tx_info->pd_data_dma_addr));
	}
}

