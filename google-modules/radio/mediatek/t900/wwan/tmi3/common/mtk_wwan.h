/* SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Copyright (c) 2022, MediaTek Inc.
 */

#ifndef __MTK_WWAN_H__
#define __MTK_WWAN_H__
#include <linux/netdevice.h>
#include "mtk_data_plane.h"

#define WWAN_DSCP_SIZE 64

enum mtk_wwan_sioc {
	MTK_WWAN_SIOC_SET_PKTPRIO = SIOCDEVPRIVATE + 14,
	MTK_WWAN_SIOC_SET_NETTYPE = SIOCDEVPRIVATE + 15
};

int mtk_wwan_init(struct mtk_data_blk *data_blk);
void mtk_wwan_exit(struct mtk_data_blk *data_blk);
int mtk_wwan_recv(struct mtk_data_blk *data_blk, struct sk_buff *skb);
void mtk_wwan_notify(struct mtk_data_blk *data_blk, enum mtk_data_evt evt, u64 data);
void mtk_wwan_ethtool_set_ops(struct net_device *dev);
int mtk_wwan_cmd_execute(struct net_device *dev, enum mtk_data_cmd_type cmd, void *data);

#endif /* __MTK_WWAN_H__ */
