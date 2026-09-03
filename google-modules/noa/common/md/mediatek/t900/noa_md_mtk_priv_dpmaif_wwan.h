/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA MD MTK T900 Private header file for DPMAIF
 *
 * This file synchronizes with MTK T900 mtk_dpmaif_wwan.c for
 * the definition and structures.
 *
 * Copyright (c) 2025 Google Inc.
 *
 */
#ifndef __NOA_WWAN_MTK_PRIV_DPMAIF_WWAN_H__
#define __NOA_WWAN_MTK_PRIV_DPMAIF_WWAN_H__

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_data_plane.h"
#endif

#define MTK_NETDEV_MAX		20
#define MTK_NAPI_POLL_WEIGHT    128

/* struct mtk_wwan_instance - This is netdevice's private data,
 * contains information about netdevice.
 * @wcb: Contains all information about WWAN port layer.
 * @stats: Statistics of netdevice's tx/rx packets.
 * @tx_busy: Statistics of netdevice's busy counts.
 * @tx_timeout: Statistics of netdevice's tx_timeout packets.
 * @netdev: Pointer to netdevice structure.
 * @intf_id: The netdevice's interface id
 * @network_type: Indicate the type of packets transmitted by the interface.
 * @pkt_prio: Indicate the packets priority transmitted by the interface.
 * @hif_ops: Contains trans layer ops: send, select_txq, napi_poll, ....
 */
struct mtk_wwan_instance {
	struct mtk_wwan_ctlb *wcb;
	struct rtnl_link_stats64 stats;
	unsigned long long tx_busy;
	unsigned long long tx_timeout;
	struct net_device *netdev;
	unsigned int intf_id;
	unsigned short network_type;
	unsigned char pkt_prio;
	struct mtk_data_hif_ops *hif_ops;
};

/* struct mtk_wwan_ctlb - Contains WWAN port layer information and save trans information needed.
 * @data_blk: Contains data port, trans layer, md_dev structure.
 * @mdev: Pointer of mtk_md_dev.
 * @wwan_inst: wwan instance, max is 20.
 * @dummy_dev: Used for multiple network devices share one napi.
 * @gro_napis: Structure for gro napi.
 * @napi_enable: Mark for napi state.
 * @active_cnt: The counter of network devices that are UP.
 * @reg_done: Mark for ntwork devices register state.
 */
struct mtk_wwan_ctlb {
	struct mtk_data_blk *data_blk;
	struct mtk_md_dev *mdev;
	struct mtk_wwan_instance __rcu *wwan_inst[MTK_NETDEV_MAX];
	struct net_device dummy_dev;
	struct napi_struct *gro_napis[MTK_DATA_NAPI_NR_MAX];
	atomic_t napi_enabled;
	unsigned int active_cnt;
	bool reg_done;
};
#endif // __NOA_WWAN_MTK_PRIV_DPMAIF_WWAN_H__
