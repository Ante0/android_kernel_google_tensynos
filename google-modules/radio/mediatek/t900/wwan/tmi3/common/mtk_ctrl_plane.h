/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_CTRL_PLANE_H__
#define __MTK_CTRL_PLANE_H__

#include <linux/kref.h>
#include <linux/skbuff.h>

#include "mtk_dev.h"
#include "mtk_fsm.h"

#define Q_MTU_2K			(0x800)
#define Q_MTU_3_5K			(0xE00)
#define Q_MTU_7K			(0x1C00)
#define Q_MTU_32K			(0x8000)
#define Q_MTU_63K			(0xFC00)
#define Q_FRAG_2K			(0x800)
#define Q_FRAG_3_5K			(0xE00)
#define Q_FRAG_7K			(0x1C00)
#define Q_FRAG_32K			(0x8000)
#define Q_FRAG_63K			(0xFC00)

enum mtk_trb_cmd_type {
	TRB_CMD_MIN,
	TRB_CMD_ENABLE,
	TRB_CMD_TX,
	TRB_CMD_DISABLE,
	TRB_CMD_CHECK_STA,
	TRB_CMD_SET_CH_CFG,
	TRB_CMD_SET_HIF_CFG,
	TRB_CMD_STOP,
	TRB_CMD_RECOVER,
	TRB_CMD_MAX,
};

enum mtk_hif_dev_ctrl_cmd {
	HIF_CTRL_CMD_TRM_NOTIFY,
	HIF_CTRL_CMD_CHECK_TX_FULL,
	HIF_CTRL_CMD_RPM_GET,
	HIF_CTRL_CMD_RPM_PUT,
	HIF_CTRL_CMD_TX_ABORT
};

struct trb_open_priv {
	u8 log_rg_offset;
	u32 tx_mtu;
	u32 rx_mtu;
	u32 tx_frag_size;
	u32 rx_frag_size;
	int (*rx_done)(struct sk_buff *skb, void *priv, bool force_recv);
};

struct trb_close_priv {
	unsigned long trb_start_time;
	unsigned long disable_start_time;
	unsigned long disable_end_time;
	unsigned long txq_free_start_time;
	unsigned long txq_free_end_time;
	unsigned long rxq_free_start_time;
	unsigned long rxq_free_end_time;
};

struct trb {
	u32 channel_id;
	enum mtk_trb_cmd_type cmd;
	int status;
	struct kref kref;
	void *priv;
	int (*trb_complete)(struct sk_buff *skb);
};

union ctrl_hif_cmd_data {
	u32 rx_ch;
};

struct mtk_ctrl_hif_ops {
	int (*init)(struct mtk_md_dev *mdev);
	int (*exit)(struct mtk_md_dev *mdev);
	int (*submit_skb)(struct mtk_md_dev *mdev, struct sk_buff *skb, bool force_send);
	void (*fsm_indication)(struct mtk_md_dev *mdev, struct mtk_fsm_param *param);
	int (*dump)(struct mtk_md_dev *mdev);
	int (*send_cmd)(struct mtk_md_dev *mdev, int cmd, void *data);
};

struct mtk_ctrl_cfg {
	struct mtk_port_layer_cfg *port_layer_cfg;
	struct mtk_fsm_cfg *fsm_cfg;
};

struct mtk_ctrl_blk {
	struct mtk_md_dev *mdev;
	struct mtk_port_mngr *port_mngr;
	struct mtk_ctrl_hif_ops *ops;
	struct mtk_bm_pool *bm_pool;
	struct mtk_bm_pool *bm_pool_63K;
	void *ctrl_hw_priv;
	struct mtk_ctrl_cfg *cfg;
};

int mtk_ctrl_init(struct mtk_md_dev *mdev, struct mtk_ctrl_hif_ops *ops,
		  struct mtk_ctrl_cfg *cfg);
int mtk_ctrl_exit(struct mtk_md_dev *mdev);
int mtk_ctrl_dump(struct mtk_md_dev *mdev);
#endif /* __MTK_CTRL_PLANE_H__ */
