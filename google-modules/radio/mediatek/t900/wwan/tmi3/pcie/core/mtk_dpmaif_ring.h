/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_DPMAIF_RING_H__
#define __MTK_DPMAIF_RING_H__

#include <linux/bitops.h>
#include <linux/types.h>

/* drb c_bit */
#define DPMAIF_DRB_LASTONE	0x00
#define DPMAIF_DRB_MORE		0x01

/* pit c_bit */
#define DPMAIF_PIT_LASTONE	0x00
#define DPMAIF_PIT_MORE		0x01

/* pit Pro_bit */
#define DPMAIF_PIT_TCP 0x01
#define DPMAIF_PIT_UDP 0x02

/* pit IP_bit */
#define DPMAIF_PIT_IPV4 0x00
#define DPMAIF_PIT_IPV6 0x01

enum dpmaif_rcsum_state {
	CS_RESULT_INVALID = -1,
	CS_RESULT_PASS = 0,
	CS_RESULT_FAIL = 1,
	CS_RESULT_NOTSUPP = 2,
	CS_RESULT_RSV = 3
};

/* pit type */
enum dpmaif_pit_type {
	PD_PIT = 0,
	MSG_PIT,
};

/* buffer type */
enum dpmaif_bat_type {
	NORMAL_BAT = 0,
	FRAG_BAT = 1,
};

/* RX: buffer address table */
struct dpmaif_bat {
	__le32 buf_addr_low;
	__le32 buf_addr_high;
};

struct dpmaif_rx_info {
	u32 pit_pd_seq;
	u32 msg_pit;
	u32 pit_msg_chnl_id;
	u32 pit_msg_checksum;
	u32 pit_msg_err;
	u32 pit_msg_dp;
	u32 pit_msg_hash;
	u32 pit_msg_pro;
	u32 pit_msg_ip;
	u32 normal_bat;
	u32 pit_pd_cur_bid;
	u32 pit_pd_data_len;
	u32 pit_pd_hd_offset;
	u32 pit_continue;
	u64 pit_pd_dma_addr;
};

struct dpmaif_pd_pit {
	__le32 pd_header;
	__le32 addr_low;
	__le32 addr_high;
	__le32 pd_footer;
};

struct dpmaif_msg_pit {
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
};

struct dpmaif_tx_info {
	u32 msg_pkt_len;
	u16 msg_count_l;
	u16 msg_network_type;
	u8 msg_channel_id;
	u8 msg_txcsum;
	dma_addr_t pd_data_dma_addr;
	u32 pd_data_len;
	u8 pd_is_last;
};

struct dpmaif_msg_drb {
	__le32 msg_header1;
	__le32 msg_header2;
	__le32 msg_rsv1;
	__le32 msg_rsv2;
};

struct dpmaif_pd_drb {
	__le32 pd_header;
	__le32 addr_low;
	__le32 addr_high;
	__le32 pd_rsv;
};

/* drb->type */
enum dpmaif_drb_type {
	PD_DRB,
	MSG_DRB,
};

static inline unsigned int mtk_dpmaif_ring_buf_get_next_idx(unsigned int buf_len,
							    unsigned int buf_idx)
{
	buf_idx++;

	return buf_idx < buf_len ? buf_idx : 0;
}

static inline unsigned int mtk_dpmaif_ring_buf_readable(unsigned int total_cnt, unsigned int rd_idx,
							unsigned int  wr_idx)
{
	unsigned int pkt_cnt;

	if (wr_idx >= rd_idx)
		pkt_cnt = wr_idx - rd_idx;
	else
		pkt_cnt = total_cnt + wr_idx - rd_idx;

	return pkt_cnt;
}

static inline unsigned int mtk_dpmaif_ring_buf_writable(unsigned int total_cnt,
							unsigned int rel_idx, unsigned int wr_idx)
{
	unsigned int pkt_cnt;

	if (wr_idx < rel_idx)
		pkt_cnt = rel_idx - wr_idx - 1;
	else
		pkt_cnt = total_cnt + rel_idx - wr_idx - 1;

	return pkt_cnt;
}

static inline unsigned int mtk_dpmaif_ring_buf_releasable(unsigned int total_cnt,
							  unsigned int rel_idx, unsigned int rd_idx)
{
	unsigned int pkt_cnt;

	if (rel_idx <= rd_idx)
		pkt_cnt = rd_idx - rel_idx;
	else
		pkt_cnt = total_cnt + rd_idx - rel_idx;

	return pkt_cnt;
}

int mtk_dpmaif_get_rx_info(void *pit, struct dpmaif_rx_info *rx_info, u32 pit_seq_expect, u8 q_id);
void mtk_dpmaif_fill_tx_info(void *drb, struct dpmaif_tx_info *tx_info, int type);

#endif
