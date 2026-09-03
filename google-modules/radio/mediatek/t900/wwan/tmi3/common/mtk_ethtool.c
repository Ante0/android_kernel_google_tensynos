// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include "mtk_wwan.h"

#define MTK_MAX_COALESCE_TIME	3
#define MTK_MAX_COALESCE_FRAMES	1000

static int mtk_ethtool_cmd_execute(struct net_device *dev, enum mtk_data_cmd_type cmd, void *data)
{
	return mtk_wwan_cmd_execute(dev, cmd, data);
}

static void mtk_ethtool_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	if (sset != ETH_SS_STATS)
		return;

	mtk_ethtool_cmd_execute(dev, DATA_CMD_STRING_GET, data);
}

static int mtk_ethtool_get_sset_count(struct net_device *dev, int sset)
{
	int s_count = 0;
	int ret;

	if (sset != ETH_SS_STATS)
		return -EOPNOTSUPP;

	ret = mtk_ethtool_cmd_execute(dev, DATA_CMD_STRING_CNT_GET, &s_count);
	if (ret)
		return ret;

	return s_count;
}

static void mtk_ethtool_get_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	mtk_ethtool_cmd_execute(dev, DATA_CMD_TRANS_DUMP, data);
}

#if (KERNEL_VERSION(5, 15, 0) <= LINUX_VERSION_CODE)
static int mtk_ethtool_get_coalesce(struct net_device *dev,
				    struct ethtool_coalesce *ec,
				    struct kernel_ethtool_coalesce *kec,
				    struct netlink_ext_ack *ack)
{
	struct mtk_data_intr_coalesce intr_get;
	int ret;

	ret = mtk_ethtool_cmd_execute(dev, DATA_CMD_INTR_COALESCE_GET, &intr_get);
	if (ret)
		return ret;

	ec->rx_coalesce_usecs = intr_get.rx_coalesce_usecs;
	ec->tx_coalesce_usecs = intr_get.tx_coalesce_usecs;
	ec->rx_max_coalesced_frames = intr_get.rx_coalesced_frames;
	ec->tx_max_coalesced_frames = intr_get.tx_coalesced_frames;

	return 0;
}

static int mtk_ethtool_set_coalesce(struct net_device *dev,
				    struct ethtool_coalesce *ec,
				    struct kernel_ethtool_coalesce *kec,
				    struct netlink_ext_ack *ack)
{
	struct mtk_data_intr_coalesce intr_set;

	if (ec->rx_coalesce_usecs > MTK_MAX_COALESCE_TIME ||
	    ec->tx_coalesce_usecs > MTK_MAX_COALESCE_TIME ||
	    ec->rx_max_coalesced_frames > MTK_MAX_COALESCE_FRAMES ||
	    ec->tx_max_coalesced_frames > MTK_MAX_COALESCE_FRAMES)
		return -EINVAL;

	intr_set.rx_coalesce_usecs = ec->rx_coalesce_usecs;
	intr_set.tx_coalesce_usecs = ec->tx_coalesce_usecs;
	intr_set.rx_coalesced_frames = ec->rx_max_coalesced_frames;
	intr_set.tx_coalesced_frames = ec->tx_max_coalesced_frames;

	return mtk_ethtool_cmd_execute(dev, DATA_CMD_INTR_COALESCE_SET, &intr_set);
}
#else
static int mtk_ethtool_get_coalesce(struct net_device *dev,
				    struct ethtool_coalesce *ec)
{
	struct mtk_data_intr_coalesce intr_get;
	int ret;

	ret = mtk_ethtool_cmd_execute(dev, DATA_CMD_INTR_COALESCE_GET,
				      &intr_get);

	if (ret)
		return ret;

	ec->rx_coalesce_usecs = intr_get.rx_coalesce_usecs;
	ec->tx_coalesce_usecs = intr_get.tx_coalesce_usecs;
	ec->rx_max_coalesced_frames = intr_get.rx_coalesced_frames;
	ec->tx_max_coalesced_frames = intr_get.tx_coalesced_frames;

	return 0;
}

static int mtk_ethtool_set_coalesce(struct net_device *dev,
				    struct ethtool_coalesce *ec)
{
	struct mtk_data_intr_coalesce intr_set;

	if (ec->rx_coalesce_usecs > MTK_MAX_COALESCE_TIME ||
	    ec->tx_coalesce_usecs > MTK_MAX_COALESCE_TIME ||
	    ec->rx_max_coalesced_frames > MTK_MAX_COALESCE_FRAMES ||
	    ec->tx_max_coalesced_frames > MTK_MAX_COALESCE_FRAMES)
		return -EINVAL;

	intr_set.rx_coalesce_usecs = ec->rx_coalesce_usecs;
	intr_set.tx_coalesce_usecs = ec->tx_coalesce_usecs;
	intr_set.rx_coalesced_frames = ec->rx_max_coalesced_frames;
	intr_set.tx_coalesced_frames = ec->tx_max_coalesced_frames;

	return mtk_ethtool_cmd_execute(dev, DATA_CMD_INTR_COALESCE_SET,
				       &intr_set);
}
#endif

#if (KERNEL_VERSION(6, 8, 0) > LINUX_VERSION_CODE)
static int mtk_ethtool_get_rxfh(struct net_device *dev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct mtk_data_rxfh rxfh;

	if (!indir && !key)
		return 0;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	rxfh.indir = indir;
	rxfh.key = key;

	return mtk_ethtool_cmd_execute(dev, DATA_CMD_RXFH_GET, &rxfh);
}
#else
static int mtk_ethtool_get_rxfh(struct net_device *dev,
				struct ethtool_rxfh_param *param)
{
	struct mtk_data_rxfh rxfh;

	if (!param->indir && !param->key)
		return 0;

	param->hfunc = ETH_RSS_HASH_TOP;

	rxfh.indir = param->indir;
	rxfh.key = param->key;

	return mtk_ethtool_cmd_execute(dev, DATA_CMD_RXFH_GET, &rxfh);
}
#endif

#if (KERNEL_VERSION(6, 8, 0) > LINUX_VERSION_CODE)
static int mtk_ethtool_set_rxfh(struct net_device *dev, const u32 *indir,
				const u8 *key, const u8 hfunc)
{
	struct mtk_data_rxfh rxfh;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE)
		return -EOPNOTSUPP;

	if (!indir && !key)
		return 0;

	rxfh.indir = (u32 *)indir;
	rxfh.key = (u8 *)key;

	return mtk_ethtool_cmd_execute(dev, DATA_CMD_RXFH_SET, &rxfh);
}
#else
static int mtk_ethtool_set_rxfh(struct net_device *dev,
				struct ethtool_rxfh_param *param,
				struct netlink_ext_ack *extack)
{
	struct mtk_data_rxfh rxfh;

	if (param->hfunc != ETH_RSS_HASH_NO_CHANGE)
		return -EOPNOTSUPP;

	if (!param->indir && !param->key)
		return 0;

	rxfh.indir = (u32 *)param->indir;
	rxfh.key = (u8 *)param->key;

	return mtk_ethtool_cmd_execute(dev, DATA_CMD_RXFH_SET, &rxfh);
}
#endif

static int mtk_ethtool_get_rxfhc(struct net_device *dev,
				 struct ethtool_rxnfc *rxnfc, u32 *rule_locs)
{
	u32 rx_rings;
	int ret;

	/* Only supported ETHTOOL_GRXRINGS */
	if (!rxnfc || rxnfc->cmd != ETHTOOL_GRXRINGS)
		return -EOPNOTSUPP;

	ret = mtk_ethtool_cmd_execute(dev, DATA_CMD_RXQ_NUM_GET, &rx_rings);
	if (!ret)
		rxnfc->data = rx_rings;

	return ret;
}

static void mtk_ethtool_get_channels(struct net_device *dev,
				     struct ethtool_channels *channels)
{
	struct mtk_data_channels ch;
	int ret;

	ret = mtk_ethtool_cmd_execute(dev, DATA_CMD_CHANNELS_GET, &ch);
	if (!ret) {
		channels->max_rx = ch.max_rx;
		channels->rx_count = ch.rx_count;
	}
}

static u32 mtk_ethtool_get_indir_size(struct net_device *dev)
{
	u32 indir_size = 0;

	mtk_ethtool_cmd_execute(dev, DATA_CMD_INDIR_SIZE_GET, &indir_size);

	return indir_size;
}

static u32 mtk_ethtool_get_hkey_size(struct net_device *dev)
{
	u32 hkey_size = 0;

	mtk_ethtool_cmd_execute(dev, DATA_CMD_HKEY_SIZE_GET, &hkey_size);

	return hkey_size;
}

static const struct ethtool_ops mtk_wwan_ethtool_ops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS | ETHTOOL_COALESCE_MAX_FRAMES,
#endif
	.get_ethtool_stats = mtk_ethtool_get_stats,
	.get_sset_count = mtk_ethtool_get_sset_count,
	.get_strings = mtk_ethtool_get_strings,
	.get_coalesce = mtk_ethtool_get_coalesce,
	.set_coalesce = mtk_ethtool_set_coalesce,
	.get_rxfh = mtk_ethtool_get_rxfh,
	.set_rxfh = mtk_ethtool_set_rxfh,
	.get_rxnfc = mtk_ethtool_get_rxfhc,
	.get_channels = mtk_ethtool_get_channels,
	.get_rxfh_indir_size = mtk_ethtool_get_indir_size,
	.get_rxfh_key_size = mtk_ethtool_get_hkey_size,
};

void mtk_wwan_ethtool_set_ops(struct net_device *dev)
{
	dev->ethtool_ops = &mtk_wwan_ethtool_ops;
}
