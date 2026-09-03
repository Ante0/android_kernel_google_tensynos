 /* SPDX-License-Identifier: BSD-3-Clause-Clear
  *
  * Copyright (c) 2022, MediaTek Inc.
  */

#ifndef __MTK_TRANS_CTRL_H__
#define __MTK_TRANS_CTRL_H__

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/types.h>

#include "mtk_dev.h"
#include "mtk_fsm.h"
#include "mtk_pm.h"
#include "mtk_port.h"

#define TRB_SRV_MAX_NUM			(1)
#define HW_QUE_NUM			(8)
#define TX_GPD_NUM			(16)
#define RX_GPD_NUM			(TX_GPD_NUM)
#define MIN_GPD_NUM			(2)
#define SKB_LIST_MAX_LEN		(16)
#define MTU_RSV_ROOM			(0x100)
#define TRB_NUM_PER_ROUND		(TX_GPD_NUM)
#define TX_BURST_MAX_CNT		(TX_GPD_NUM / 4 + 1)

#define HIF_ID(peer_id)			((peer_id) - 1)

enum mtk_hif_id {
	CLDMA0,
	CLDMA1,
	CLDMA4,
	NR_CLDMA
};

struct queue_info {
	u32 tx_chl;
	u32 rx_chl;
	enum mtk_hif_id hif_id;
	u32 txqno;
	u32 rxqno;
	u32 tx_mtu;
	u32 rx_mtu;
	u32 tx_nr_gpds;
	u32 rx_nr_gpds;
	u32 tx_frag_size;
	u32 rx_frag_size;
	u8 log_rg_offset;
};

struct trans_list {
	struct sk_buff_head skb_list[HW_QUE_NUM];
	u8 tx_burst_cnt[HW_QUE_NUM];
};

struct mtk_ctrl_trans {
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_pm_entity pm_entity;
	struct trb_srv *trb_srv[TRB_SRV_MAX_NUM];
	struct trans_list trans_list[NR_CLDMA];
	void *dev;
	struct radix_tree_root queue_tbl;
	struct mtk_md_dev *mdev;
	int usr_cnt[NR_CLDMA][HW_QUE_NUM];
	u32 tx_mtu_cfg[NR_CLDMA][HW_QUE_NUM];
	u32 rx_mtu_cfg[NR_CLDMA][HW_QUE_NUM];
	atomic_t available;
	int queues_cnt;
	int srv_cfg[NR_CLDMA][HW_QUE_NUM];
	struct queue_info *queue_info;
	struct mtk_ctrl_drv_cfg *drv_cfg;
	int queue_info_num;
	int trb_srv_num;
};

struct srv_que {
	u32 hif_id;
	u32 qno;
	struct list_head list;
};

struct trb_srv {
	u32 srv_id;
	struct list_head srv_q_list[NR_CLDMA];
	struct mtk_ctrl_trans *trans;
	wait_queue_head_t trb_waitq;
	struct task_struct *trb_thread;
};

struct mtk_ctrl_info {
	struct mtk_ctrl_cfg *ctrl_cfg;
	int **srv_cfg;
	struct mtk_ctrl_drv_cfg *drv_cfg;
	struct queue_info *queue_info;
	u32 queue_info_num;
	u32 trb_srv_num;
};

struct mtk_ctrl_info_desc {
	u32 hw_ver;
	struct mtk_ctrl_info *ctrl_info;
};

#define ctrl_info_name(NAME)	mtk_ctrl_info_##NAME

int mtk_trans_ctrl_init(struct mtk_md_dev *mdev);
int mtk_trans_ctrl_exit(struct mtk_md_dev *mdev);

#endif
