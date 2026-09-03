// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#define pr_fmt(fmt) "MTK_WWAN: " fmt

#include <linux/completion.h>
#include <linux/icmp.h>
#include <linux/if.h>
#include <linux/if_arp.h>
#include <linux/if_link.h>
#include <linux/if_vlan.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/nospec.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/skmsg.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/wwan.h>
#include <net/pkt_sched.h>
#include <net/sch_generic.h>

#include "mtk_data_plane.h"
#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_wwan.h"

#ifdef CONFIG_TX00_UT_WWAN
#include "ut_wwan_case.h"
#endif

#define MTK_NETDEV_MAX		20
#define MTK_DFLT_INTF_ID	0
#define MTK_NETDEV_WDT (HZ)
#define MTK_CMD_WDT (HZ)
#define MTK_MAX_INTF_ID (MTK_NETDEV_MAX - 1)
#define MTK_NAPI_POLL_WEIGHT	128

#define MTK_TSQ_DFLT_SHIFT	7
#define MTK_TSQ_MAX_SHIFT	10

static unsigned int mtk_tsq_shift = MTK_TSQ_DFLT_SHIFT;

#define TAG "WWAN"

static unsigned int napi_budget = MTK_NAPI_POLL_WEIGHT;

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

/* mtk_wwan_recv - Collect data packet.
 * @data_blk: Save netdev information.
 * @skb: Received sk buffer.
 */
int mtk_wwan_recv(struct mtk_data_blk *data_blk, struct sk_buff *skb)
{
	union mtk_data_pkt_info *pkt_info = DATA_SKB_CB(skb);
	struct mtk_wwan_instance *wwan_inst;
	unsigned char q_id;
	u32 id;

	if (unlikely(pkt_info->rx.ch_id > MTK_MAX_INTF_ID)) {
		MTK_WARN(data_blk->mdev, "Invalid interface id=%d\n", pkt_info->rx.ch_id);
		WARN_ON_ONCE(true);
		goto free_skb;
	}

	q_id = pkt_info->rx.q_id;
	id = pkt_info->rx.id;

	rcu_read_lock();
	wwan_inst = rcu_dereference(data_blk->wcb->wwan_inst[pkt_info->rx.ch_id]);
	if (unlikely(!wwan_inst)) {
		MTK_ERR(data_blk->mdev, "Invalid pointer wwan_inst is NULL\n");
		rcu_read_unlock();
		goto free_skb;
	}

	skb->dev = wwan_inst->netdev;

	wwan_inst->stats.rx_packets++;
	wwan_inst->stats.rx_bytes += skb->len;

	trace_mtk_wwan_data_rx(q_id, "4", id);

#if defined(CONFIG_DATA_CPU_LOADING_OPTIMIZE) || defined(CONFIG_DATA_GRO_WITHOUT_NAPI_POLL)
	if (data_blk->trans_info.rxq_attr[q_id] & DATAQ_ATTR_LOW_LATENCY) {
		napi_gro_receive(wwan_inst->wcb->gro_napis[q_id], skb);
	} else {
		local_bh_disable();
		napi_gro_receive(wwan_inst->wcb->gro_napis[q_id], skb);
		local_bh_enable();
	}
#else
	napi_gro_receive(data_blk->wcb->gro_napis[q_id], skb);
#endif

	trace_mtk_wwan_data_rx(q_id, "5", id);

	rcu_read_unlock();
	return 0;

free_skb:
	dev_kfree_skb_any(skb);
	return -EINVAL;
}
EXPORT_SYMBOL(mtk_wwan_recv);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
static u8 mtk_wwan_get_napi_thread_affinity(struct mtk_wwan_ctlb *wcb,
					    unsigned char napi_id)
{
	return wcb->data_blk->trans_info.napi_thrd_aff[napi_id];
}

static void mtk_wwan_set_napi_thread_prio(struct mtk_wwan_ctlb *wcb,
					  unsigned char napi_id)
{
	struct napi_struct *napi = wcb->data_blk->trans_info.napis[napi_id];

	/* Set static priority for napi thread of low latency queue */
	if (wcb->data_blk->trans_info.rxq_attr[napi_id] & DATAQ_ATTR_LOW_LATENCY)
		sched_set_fifo(napi->thread);
}

static void mtk_wwan_set_napi_thread_affinity(struct mtk_wwan_ctlb *wcb,
					      unsigned char napi_id)
{
	struct napi_struct *napi = wcb->data_blk->trans_info.napis[napi_id];
	u32 napi_thread_affinity;
	cpumask_var_t mask;

	if (!wcb->data_blk->trans_info.napi_thrd_aff || !napi->thread) {
		MTK_WARN(wcb->mdev, "napi_thrd_aff = 0x%llx, napi->thread = 0x%llx\n",
			 (u64)wcb->data_blk->trans_info.napi_thrd_aff, (u64)napi->thread);
		return;
	}

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL)) {
		MTK_WARN(wcb->mdev, "Failed to alloc cpumask var\n");
		return;
	}

	napi_thread_affinity = mtk_wwan_get_napi_thread_affinity(wcb, napi_id);
	cpumask_set_cpu(napi_thread_affinity, mask);
	set_cpus_allowed_ptr(napi->thread, mask);

	free_cpumask_var(mask);
}
#endif
#endif
#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
static void mtk_wwan_trans_napi_enable(struct mtk_wwan_ctlb *wcb)
{
	int i;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	int ret;
#endif
#endif

	if (atomic_cmpxchg(&wcb->napi_enabled, 0, 1) == 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
		ret = dev_set_threaded(&wcb->dummy_dev, true);
		MTK_INFO(wcb->mdev, "dev_set_threaded return = %d\n", ret);
#endif
#endif

		for (i = 0; i < wcb->data_blk->trans_info.rxq_cnt; i++) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
			if (!ret) {
				mtk_wwan_set_napi_thread_affinity(wcb, i);
				mtk_wwan_set_napi_thread_prio(wcb, i);
			}
#endif
#endif
			napi_enable(wcb->data_blk->trans_info.napis[i]);
		}
	}
}

static void mtk_wwan_trans_napi_disable(struct mtk_wwan_ctlb *wcb)
{
	int i;

	if (atomic_cmpxchg(&wcb->napi_enabled, 1, 0) == 1) {
		for (i = 0; i < wcb->data_blk->trans_info.rxq_cnt; i++) {
			napi_synchronize(wcb->data_blk->trans_info.napis[i]);
			napi_disable(wcb->data_blk->trans_info.napis[i]);
		}
	}
}
#endif
static int mtk_wwan_open(struct net_device *dev)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	struct mtk_wwan_ctlb *wcb = wwan_inst->wcb;
	struct mtk_data_trans_ctl trans_ctl;
	int ret;

	MTK_INFO(wcb->mdev, "Opening netdev=%s, active_cnt=%u\n",
		 dev->name, wcb->active_cnt);

	if (wcb->active_cnt == 0) {
		/* If enable trans failed, means this link not ready. */
		trans_ctl.enable = true;
		ret = mtk_wwan_cmd_execute(dev, DATA_CMD_TRANS_CTL, &trans_ctl);
		if (ret < 0) {
			MTK_ERR(wcb->mdev, "Failed to enable trans\n");
			return ret;
		}
	}

	/* ndo_open hold rtnl_lock() */
	wcb->active_cnt++;

	netif_tx_start_all_queues(dev);
	netif_carrier_on(dev);

	return 0;
}

static int mtk_wwan_stop(struct net_device *dev)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	struct mtk_wwan_ctlb *wcb = wwan_inst->wcb;
	struct mtk_data_trans_ctl trans_ctl;
	int ret;

	MTK_INFO(wcb->mdev, "Stopping netdev=%s, active_cnt=%u\n",
		 dev->name, wcb->active_cnt);

	netif_carrier_off(dev);
	netif_tx_disable(dev);

	if (wcb->active_cnt == 1) {
		trans_ctl.enable = false;
		ret = mtk_wwan_cmd_execute(dev, DATA_CMD_TRANS_CTL, &trans_ctl);
		if (ret < 0)
			MTK_ERR(wcb->mdev, "Failed to disable trans\n");
	}

	/* ndo_stop hold rtnl_lock() */
	wcb->active_cnt--;

	return 0;
}

static int mtk_wwan_change_mtu(struct net_device *dev, int mtu)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	struct mtk_wwan_ctlb *wcb = wwan_inst->wcb;
	int ret;

	ret = mtk_wwan_cmd_execute(dev, DATA_CMD_MTU_SET, &mtu);
	if (ret < 0)
		return ret;

	MTK_INFO(wcb->mdev, "%s MTU change from %u to %d\n", dev->name, dev->mtu, mtu);
	WRITE_ONCE(dev->mtu, mtu);

	return 0;
}

static enum mtk_data_pkt_type mtk_wwan_check_skb_type(struct sk_buff *skb, enum mtk_pkt_type type)
{
	enum mtk_data_pkt_type ret = PKT_UNKNOWN;
	struct frag_hdr *fh;
	struct tcphdr *tcph;
	int inner_offset;
	__be16 frag_off;
	u32 total_len;
	u32 pkt_type;
	u8 l2_hlen;
	u8 nexthdr;

	union {
		struct iphdr *v4;
		struct ipv6hdr *v6;
		unsigned char *hdr;
	} ip;

	switch (type) {
	case ETH_802_3:
		l2_hlen = ETH_HLEN;
		break;
	case ETH_802_1Q:
		l2_hlen = VLAN_ETH_HLEN;
		break;
	case PURE_IP:
		l2_hlen = 0;
		break;
	default:
		l2_hlen = 0;
		break;
	}

	if (l2_hlen)
		skb_pull(skb, l2_hlen);

	pkt_type = skb->data[0] & 0xF0;
	if (pkt_type == IPV4_VERSION) {
		if (unlikely(skb->len < sizeof(struct iphdr)))
			goto out;
		ip.v4 = (struct iphdr *)(skb->data);
		if (ip.v4->protocol == IPPROTO_ICMP) {
			ret = PKT_ICMP;
		} else if (ip.v4->protocol == IPPROTO_TCP) {
			if (skb_is_tcp_pure_ack(skb)) {
				ret = PKT_EMPTY_ACK;
			} else {
				inner_offset = ip.v4->ihl << 2;
				if (unlikely(skb->len < (inner_offset + sizeof(struct tcphdr))))
					goto out;
				tcph = (struct tcphdr *)(skb->data + inner_offset);
				if (((ip.v4->ihl << 2) + (tcph->doff << 2))
						== (ntohs(ip.v4->tot_len)) &&
				    !tcph->syn && !tcph->fin && !tcph->rst)
					ret = PKT_EMPTY_ACK;
			}
		}
	} else if (pkt_type == IPV6_VERSION) {
		if (unlikely(skb->len < sizeof(struct ipv6hdr)))
			goto out;
		ip.v6 = (struct ipv6hdr *)skb->data;
		nexthdr = ip.v6->nexthdr;
		/* Now skip over extension headers. */
		inner_offset = ipv6_skip_exthdr(skb, sizeof(struct ipv6hdr),
						&nexthdr, &frag_off);
		if (unlikely(inner_offset < 0))
			goto out;

		if (nexthdr == NEXTHDR_ICMP) {
			ret = PKT_ICMP;
		} else if (nexthdr == NEXTHDR_TCP) {
			if (unlikely(skb->len < (inner_offset + sizeof(struct tcphdr))))
				goto out;
			total_len = sizeof(struct ipv6hdr) + ntohs(ip.v6->payload_len);
			tcph = (struct tcphdr *)(skb->data + inner_offset);
			if (((total_len - inner_offset) == (tcph->doff << 2)) &&
			    !tcph->syn && !tcph->fin && !tcph->rst)
				ret = PKT_EMPTY_ACK;
		} else if (nexthdr == NEXTHDR_FRAGMENT) {
			if (unlikely(skb->len < (inner_offset + sizeof(struct frag_hdr))))
				goto out;
			fh = (struct frag_hdr *)(skb->data + inner_offset);
			if (fh->nexthdr == NEXTHDR_ICMP)
				ret = PKT_ICMP;
		}
	}

out:
	if (l2_hlen)
		skb_push(skb, l2_hlen);
	return ret;
}

static const enum mtk_data_pkt_prio wwan_dscp2prio_tbl[WWAN_DSCP_SIZE] = {
	/* CS1-4, refer to RFC2474 */
	[8] = PKT_PRIO_1, [16] = PKT_PRIO_1, [24] = PKT_PRIO_1, [32] = PKT_PRIO_1,
	/* CS5/6/7 refer to RFC2474, EF refer to RFC3246 */
	[40] = PKT_PRIO_2, [46] = PKT_PRIO_2, [48] = PKT_PRIO_2, [56] = PKT_PRIO_2,
	/* VOICE-ADMIT, refer to RFC5865 */
	[44] = PKT_PRIO_3
};

static enum mtk_data_pkt_prio mtk_wwan_get_prio_by_dscp(struct mtk_wwan_ctlb *wcb,
							struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h;
	unsigned int dscp;
	struct iphdr *iph;

	if (skb->protocol == htons(ETH_P_IP)) {
		if (unlikely(skb->len < sizeof(struct iphdr)))
			return PKT_PRIO_0;
		iph = ip_hdr(skb);
		dscp = (unsigned int)iph->tos >> 2;
	} else if (skb->protocol == htons(ETH_P_IPV6)) {
		if (unlikely(skb->len < sizeof(struct ipv6hdr)))
			return PKT_PRIO_0;
		ipv6h = ipv6_hdr(skb);
		dscp = (ntohl(*(__be32 *)ipv6h) & 0x0fc00000) >> 22;
	} else {
		return PKT_PRIO_0;
	}

	dscp = array_index_nospec(dscp, WWAN_DSCP_SIZE);

	return wwan_dscp2prio_tbl[dscp];
}

static enum mtk_data_pkt_prio mtk_wwan_get_pkt_prio(struct mtk_wwan_instance *wwan_inst,
						    struct sk_buff *skb,
	enum mtk_pkt_type type)
{
	struct mtk_wwan_ctlb *wcb = wwan_inst->wcb;
	enum mtk_data_pkt_prio pkt_prio;
	enum mtk_data_pkt_type pkt_type;
	u32 mark;

	if (wwan_inst->pkt_prio < PKT_PRIO_MAX)
		return wwan_inst->pkt_prio;

	if (skb->mark) {
#if IS_ENABLED(CONFIG_GOOGLE_WWAN_PKT_PRIO)
		/* As per AOSP release-15, BIT 29 and 30 is reserved for vendor */
		mark = FIELD_GET(GENMASK(30, 29), skb->mark);
		switch (mark) {
		case 0x3:
			pkt_prio = PKT_PRIO_3;
			goto out;
		case 0x2:
			pkt_prio = PKT_PRIO_2;
			goto out;
		case 0x1:
			pkt_prio = PKT_PRIO_1;
			goto out;
		default:
			break;
		}
#else
		mark = skb->mark >> 21 & 0x7ff;
		if (mark == 0x600) {
			pkt_prio = PKT_PRIO_3;
			goto out;
		}
		if (mark == 0x400) {
			pkt_prio = PKT_PRIO_2;
			goto out;
		}
		if (mark == 0x200) {
			pkt_prio = PKT_PRIO_1;
			goto out;
		}
#endif  /* CONFIG_GOOGLE_WWAN_PKT_PRIO */
	}

	pkt_type = mtk_wwan_check_skb_type(skb, type);
	if (pkt_type == PKT_EMPTY_ACK)
		pkt_prio = PKT_PRIO_2;
	else if (pkt_type == PKT_ICMP)
		pkt_prio = PKT_PRIO_1;
	else
		pkt_prio = mtk_wwan_get_prio_by_dscp(wcb, skb);

out:
	return pkt_prio;
}

static unsigned char mtk_wwan_select_txq(struct mtk_wwan_instance *wwan_inst,
					 struct sk_buff *skb, enum mtk_pkt_type pkt_type)
{
	struct mtk_data_blk *data_blk = wwan_inst->wcb->data_blk;
	enum mtk_data_pkt_prio pkt_prio;
	unsigned char qid;

	pkt_prio = mtk_wwan_get_pkt_prio(wwan_inst, skb, PURE_IP);
	qid = data_blk->hif_ops->select_txq(data_blk, skb, pkt_prio);
	if (unlikely(qid >= data_blk->trans_info.txq_cnt))
		qid = 0;

	return qid;
}

static netdev_tx_t mtk_wwan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	union mtk_data_pkt_info *pkt_info = DATA_SKB_CB(skb);
	unsigned int skb_len = skb->len;
	int ret;

	if (unlikely(skb->len > dev->mtu)) {
		MTK_ERR(wwan_inst->wcb->mdev,
			"Failed to write skb,netdev=%s,len=0x%x,MTU=0x%x\n",
			dev->name, skb->len, dev->mtu);
		goto err_tx;
	}

	pkt_info->tx.id = mtk_data_get_pkt_id(skb);
	trace_mtk_wwan_data_tx(-1, "0", pkt_info->tx.id);
	pkt_info->tx.intf_id = wwan_inst->intf_id;
	pkt_info->tx.network_type = wwan_inst->network_type;
	/* select trans layer virtual queue */
	pkt_info->tx.q_id = mtk_wwan_select_txq(wwan_inst, skb, PURE_IP);
	trace_mtk_wwan_data_tx(pkt_info->tx.q_id, "1", pkt_info->tx.id);

	pkt_info->tx.in_tcp_slow_start = false;
	/* sk_type and sk_protocol are only accessible when sk_fullsock is true */
	if (skb->sk && sk_fullsock(skb->sk)) {
#if (KERNEL_VERSION(5, 16, 0) <= LINUX_VERSION_CODE)
		if (sk_is_tcp(skb->sk)) {
#else
		if (skb->sk->sk_type == SOCK_STREAM && skb->sk->sk_protocol == IPPROTO_TCP) {
#endif
			if (mtk_tsq_shift <= MTK_TSQ_MAX_SHIFT)
				sk_pacing_shift_update(skb->sk, mtk_tsq_shift);
			pkt_info->tx.in_tcp_slow_start = tcp_in_slow_start(tcp_sk(skb->sk));
		}
	}

	/* Forward skb to trans layer(DPMAIF). */
	ret = wwan_inst->hif_ops->send(wwan_inst->wcb->data_blk, DATA_PKT, skb);
	if (ret == -EBUSY) {
		wwan_inst->tx_busy++;
		return NETDEV_TX_BUSY;
	} else if (ret == -EINVAL) {
		goto err_tx;
	}

	wwan_inst->stats.tx_packets++;
	wwan_inst->stats.tx_bytes += skb_len;
	goto out;

err_tx:
	wwan_inst->stats.tx_errors++;
	wwan_inst->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
out:
	return NETDEV_TX_OK;
}

static void mtk_wwan_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);

	memcpy(stats, &wwan_inst->stats, sizeof(*stats));
}

static void mtk_wwan_tx_timeout(struct net_device *dev, unsigned int txq)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);

	wwan_inst->tx_timeout++;

	MTK_WARN(wwan_inst->wcb->mdev,
		 "wwan%d tx_timeout, txq=%u, active_cnt=%u\n",
		wwan_inst->intf_id, txq, wwan_inst->wcb->active_cnt);
}

static netdev_features_t mtk_wwan_fix_feature(struct net_device *dev,
					      netdev_features_t features)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);

	if (!(features & NETIF_F_RXCSUM)) {
		MTK_INFO(wwan_inst->wcb->mdev, "disabling GRO_HW as RXCSUM is off\n");
		features &= ~NETIF_F_GRO_HW;
	}

	return features;
}

static int mtk_wwan_set_feature(struct net_device *dev, netdev_features_t features)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	bool hw_txcsum_enable = !!(dev->features & NETIF_F_HW_CSUM);
	bool hw_rxcsum_enable = !!(dev->features & NETIF_F_RXCSUM);
	bool hw_gro_enable = !!(dev->features & NETIF_F_GRO_HW);
	netdev_features_t diff = dev->features ^ features;
	netdev_features_t rxcsum_flag;
	netdev_features_t txcsum_flag;
	netdev_features_t gro_hw_flag;
	int ret = 0;

	gro_hw_flag = diff & NETIF_F_GRO_HW;
	rxcsum_flag = diff & NETIF_F_RXCSUM;
	txcsum_flag = diff & NETIF_F_HW_CSUM;

	if (!gro_hw_flag && !rxcsum_flag && !txcsum_flag)
		return ret;

	if (gro_hw_flag) {
		hw_gro_enable = (features & NETIF_F_GRO_HW) ? true : false;
		ret = mtk_wwan_cmd_execute(dev, DATA_CMD_GRO_HW_SET, &hw_gro_enable);
	}

	if (rxcsum_flag) {
		hw_rxcsum_enable = (features & NETIF_F_RXCSUM) ? true : false;
		ret = mtk_wwan_cmd_execute(dev, DATA_CMD_RXCSUM_SET, &hw_rxcsum_enable);
	}

	if (txcsum_flag) {
		hw_txcsum_enable = (features & NETIF_F_HW_CSUM) ? true : false;
		ret = mtk_wwan_cmd_execute(dev, DATA_CMD_TXCSUM_SET, &hw_txcsum_enable);
	}

	MTK_DBG(wwan_inst->wcb->mdev, MTK_DBG_WWAN, MTK_MEMLOG_RG_COMMON,
		"dev->features = %pNF,features = %pNF,gro_hw_enable = %d,rxcsum_enable = %d,txcsum_enable = %d,ret = %d\n",
		&dev->features, &features, hw_gro_enable,
		hw_rxcsum_enable, hw_txcsum_enable, ret);

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
static int mtk_wwan_siocdevprivate(struct net_device *dev, struct ifreq *ifr,
				   void __user *data, int cmd)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
#ifndef CONFIG_DATA_TEST_MODE
	unsigned short network_type;
#endif
	unsigned char pkt_prio;
	int ret = 0;

	switch (cmd) {
#ifndef CONFIG_DATA_TEST_MODE
	case MTK_WWAN_SIOC_SET_NETTYPE:
		if (copy_from_user(&network_type, data, sizeof(network_type))) {
			ret = -EFAULT;
		} else {
			if (network_type > DATA_NETWORK_TYPE_MAX) {
				MTK_ERR(wwan_inst->wcb->mdev,
					"Invalid network_type, type=%d,intf_id=%u\n",
					network_type, wwan_inst->intf_id);
				ret = -EINVAL;
			} else {
				wwan_inst->network_type = network_type;
				MTK_INFO(wwan_inst->wcb->mdev,
					 "wwan%u,network_type=%d\n",
					wwan_inst->intf_id, wwan_inst->network_type);
			}
		}
		break;
#endif
	case MTK_WWAN_SIOC_SET_PKTPRIO:
		if (copy_from_user(&pkt_prio, data, sizeof(pkt_prio))) {
			ret = -EFAULT;
		} else {
			if (pkt_prio >= PKT_PRIO_MAX) {
				MTK_ERR(wwan_inst->wcb->mdev,
					"Invalid pkt_prio, prio=%hhu,intf_id=%u\n",
					pkt_prio, wwan_inst->intf_id);
				ret = -EINVAL;
			} else {
				wwan_inst->pkt_prio = pkt_prio;
				MTK_INFO(wwan_inst->wcb->mdev,
					 "wwan%u,pkt_prio=%hhu\n",
					wwan_inst->intf_id, wwan_inst->pkt_prio);
			}
		}
		break;
	default:
		MTK_DBG(wwan_inst->wcb->mdev,
			MTK_DBG_WWAN, MTK_MEMLOG_RG_COMMON, "Invalid cmd, cmd=%d\n", cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif

static const struct net_device_ops mtk_netdev_ops = {
	.ndo_open = mtk_wwan_open,
	.ndo_stop = mtk_wwan_stop,
	.ndo_change_mtu = mtk_wwan_change_mtu,
	.ndo_start_xmit = mtk_wwan_start_xmit,
	.ndo_tx_timeout = mtk_wwan_tx_timeout,
	.ndo_get_stats64	= mtk_wwan_get_stats,
	.ndo_fix_features = mtk_wwan_fix_feature,
	.ndo_set_features = mtk_wwan_set_feature,

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0)
	.ndo_siocdevprivate = mtk_wwan_siocdevprivate,
#endif
};

static int mtk_wwan_cmd_check(struct net_device *dev, enum mtk_data_cmd_type cmd)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	int ret = 0;

	switch (cmd) {
	case DATA_CMD_INTR_COALESCE_GET:
		fallthrough;
	case DATA_CMD_INTR_COALESCE_SET:
		if (!(wwan_inst->wcb->data_blk->trans_info.cap & DATA_F_INTR_COALESCE))
			ret = -EOPNOTSUPP;
		break;

	case DATA_CMD_INDIR_SIZE_GET:
		fallthrough;
	case DATA_CMD_HKEY_SIZE_GET:
		fallthrough;
	case DATA_CMD_RXFH_GET:
		fallthrough;
	case DATA_CMD_RXFH_SET:
		if (!(wwan_inst->wcb->data_blk->trans_info.cap & DATA_F_RXFH))
			ret = -EOPNOTSUPP;
		break;

	case DATA_CMD_RXQ_NUM_GET:
		fallthrough;
	case DATA_CMD_CHANNELS_GET:
		fallthrough;
	case DATA_CMD_TRANS_DUMP:
		fallthrough;
	case DATA_CMD_STRING_CNT_GET:
		fallthrough;
	case DATA_CMD_STRING_GET:
		break;
	case DATA_CMD_MTU_SET:
		fallthrough;
	case DATA_CMD_TRANS_CTL:
		break;
	case DATA_CMD_GRO_HW_SET:
		fallthrough;
	case DATA_CMD_RXCSUM_SET:
		fallthrough;
	case DATA_CMD_TXCSUM_SET:
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static struct sk_buff *mtk_wwan_cmd_alloc(enum mtk_data_cmd_type cmd, unsigned int len)

{
	struct mtk_data_cmd *event;
	struct sk_buff *skb;

	skb = dev_alloc_skb(sizeof(*event) + len);
	if (unlikely(!skb))
		return NULL;

	skb_put(skb, len + sizeof(*event));
	event = (struct mtk_data_cmd *)skb->data;
	event->cmd = cmd;
	event->len = len;

	return skb;
}

static int mtk_wwan_cmd_send(struct net_device *dev, struct sk_buff *skb)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);

	return wwan_inst->hif_ops->send(wwan_inst->wcb->data_blk, DATA_CMD, skb);
}

int mtk_wwan_cmd_execute(struct net_device *dev,
			 enum mtk_data_cmd_type cmd, void *data)
{
	struct mtk_wwan_instance *wwan_inst;
	struct sk_buff *skb;
	int ret;

	if (mtk_wwan_cmd_check(dev, cmd))
		return -EOPNOTSUPP;

	skb = mtk_wwan_cmd_alloc(cmd, sizeof(void *));
	if (unlikely(!skb))
		return -ENOMEM;

	SKB_TO_CMD_DATA(skb) = data;

	ret = mtk_wwan_cmd_send(dev, skb);
	if (ret < 0) {
		wwan_inst = wwan_netdev_drvpriv(dev);
		MTK_ERR(wwan_inst->wcb->mdev, "Failed to execute command:ret=%d,cmd=%d\n", ret, cmd);
	}

	dev_consume_skb_any(skb);

	return ret;
}

static void mtk_wwan_dump(struct mtk_wwan_ctlb *wcb)
{
	struct mtk_wwan_instance *wwan_inst;
	struct rtnl_link_stats64 *stats;
	int i;

	MTK_DBG(wcb->mdev, MTK_DBG_WWAN,
		MTK_MEMLOG_RG_DATA_DUMP, "============WWAN_DUMP_START============\n");
	/* Dump wwan_ctlb. */
	MTK_DBG(wcb->mdev, MTK_DBG_WWAN,
		MTK_MEMLOG_RG_DATA_DUMP, "cap=0x%x, active_cnt=%u, reg_done=%d\n",
		wcb->data_blk->trans_info.cap, wcb->active_cnt, wcb->reg_done);

	rcu_read_lock();
	/* Dump wwan_inst. */
	for (i = 0; i < MTK_NETDEV_MAX; i++) {
		wwan_inst = rcu_dereference(wcb->wwan_inst[i]);
		if (!wwan_inst) {
			MTK_DBG(wcb->mdev, MTK_DBG_WWAN,
				MTK_MEMLOG_RG_DATA_DUMP, "wwan%d was not registered\n", i);
			continue;
		}
		MTK_DBG(wcb->mdev, MTK_DBG_WWAN, MTK_MEMLOG_RG_DATA_DUMP,
			"============%s============features=%pNF\n",
			      wwan_inst->netdev->name, &wwan_inst->netdev->features);

		/* Dump stats. */
		stats = &wwan_inst->stats;
		MTK_DBG(wcb->mdev, MTK_DBG_WWAN, MTK_MEMLOG_RG_DATA_DUMP,
			"rx: pkts=%u, bytes=%u, dropped=%u, errors=%u\n",
			      stats->rx_packets, stats->rx_bytes,
			      stats->rx_dropped, stats->rx_errors);
		MTK_DBG(wcb->mdev, MTK_DBG_WWAN, MTK_MEMLOG_RG_DATA_DUMP,
			"tx: pkts=%u, bytes=%u, dropped=%u, errors=%u, timeout=%u, busy=%u\n",
			      stats->tx_packets, stats->tx_bytes,
			      stats->tx_dropped, stats->tx_errors,
			      wwan_inst->tx_timeout, wwan_inst->tx_busy);
	}
	rcu_read_unlock();
	MTK_DBG(wcb->mdev, MTK_DBG_WWAN,
		MTK_MEMLOG_RG_DATA_DUMP, "============WWAN_DUMP_END============\n");
}

static int mtk_wwan_start_txq(struct mtk_wwan_ctlb *wcb, u32 qmask)
{
	struct mtk_wwan_instance *wwan_inst;
	struct net_device *dev;
	int i;

	rcu_read_lock();
	/* All wwan network devices share same HIF queue */
	for (i = 0; i < MTK_NETDEV_MAX; i++) {
		wwan_inst = rcu_dereference(wcb->wwan_inst[i]);
		if (!wwan_inst)
			continue;

		dev = wwan_inst->netdev;

		if (!(dev->flags & IFF_UP))
			continue;

		netif_tx_wake_all_queues(dev);
		netif_carrier_on(dev);
	}
	rcu_read_unlock();

	return 0;
}

static int mtk_wwan_stop_txq(struct mtk_wwan_ctlb *wcb, u32 qmask)
{
	struct mtk_wwan_instance *wwan_inst;
	struct net_device *dev;
	int i;

	rcu_read_lock();
	/* All wwan network devices share same HIF queue */
	for (i = 0; i < MTK_NETDEV_MAX; i++) {
		wwan_inst = rcu_dereference(wcb->wwan_inst[i]);
		if (!wwan_inst)
			continue;

		dev = wwan_inst->netdev;

		if (!(dev->flags & IFF_UP))
			continue;

		netif_carrier_off(dev);
		/* the network transmit lock has already been held in the ndo_start_xmit context */
		netif_tx_stop_all_queues(dev);
	}
	rcu_read_unlock();

	return 0;
}

#if defined(CONFIG_DATA_CPU_LOADING_OPTIMIZE) || defined(CONFIG_DATA_GRO_WITHOUT_NAPI_POLL)
static void mtk_wwan_gro_normal_list(struct napi_struct *napi)
{
	local_bh_disable();
	napi_gro_flush(napi, false);
	if (napi->rx_count) {
		netif_receive_skb_list(&napi->rx_list);
		INIT_LIST_HEAD(&napi->rx_list);
		napi->rx_count = 0;
	}
	local_bh_enable();
}

static void mtk_wwan_gro_napi_exit(struct mtk_wwan_ctlb *wcb)
{
	int i;

	for (i = 0; i < wcb->data_blk->trans_info.rxq_cnt; i++) {
		if (!wcb->gro_napis[i]) {
			MTK_ERR(wcb->mdev, "Invalid parameter, gro_napis%d is NULL\n", i);
			continue;
		}

		if (!(wcb->data_blk->trans_info.rxq_attr[i] & DATAQ_ATTR_LOW_LATENCY)) {
			MTK_INFO(wcb->mdev, "gro_napis%d - 0x%llx, napi_id=%d del, rxq_attr=0x%x\n",
				 i, (u64)wcb->gro_napis[i], wcb->gro_napis[i]->napi_id,
				 wcb->data_blk->trans_info.rxq_attr[i]);

			mtk_wwan_gro_normal_list(wcb->gro_napis[i]);
			netif_napi_del(wcb->gro_napis[i]);
			devm_kfree(wcb->mdev->dev, wcb->gro_napis[i]);
		}
	}
}

static int mtk_wwan_gro_napi_dummy_poll(struct napi_struct *napi, int budget)
{
	return 0;
}

static int mtk_wwan_gro_napi_init(struct mtk_wwan_ctlb *wcb, struct net_device *dev)
{
	int i, j;

	for (i = 0; i < wcb->data_blk->trans_info.rxq_cnt; i++) {
#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
		if (wcb->data_blk->trans_info.rxq_attr[i] & DATAQ_ATTR_LOW_LATENCY) {
			wcb->gro_napis[i] = wcb->data_blk->trans_info.napis[i];
			continue;
		}
#endif

		wcb->gro_napis[i] = devm_kzalloc(wcb->mdev->dev, sizeof(*wcb->gro_napis[i]),
						 GFP_KERNEL);
		if (!wcb->gro_napis[i]) {
			MTK_ERR(wcb->mdev, "Failed to allocate gro_napis\n");
			goto exit;
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		netif_napi_add_weight(dev, wcb->gro_napis[i], mtk_wwan_gro_napi_dummy_poll,
				      napi_budget);
#else
		netif_napi_add(dev, wcb->gro_napis[i], mtk_wwan_gro_napi_dummy_poll,
			       napi_budget);
#endif

		MTK_INFO(wcb->mdev, "gro_napis%d-0x%llx napi_id=%d add done, rxq_attr=0x%x\n",
			 i, (u64)wcb->gro_napis[i], wcb->gro_napis[i]->napi_id,
			wcb->data_blk->trans_info.rxq_attr[i]);
	}

	return 0;
exit:
	for (j = i - 1; j >= 0; j--) {
		if (!(wcb->data_blk->trans_info.rxq_attr[j] & DATAQ_ATTR_LOW_LATENCY)) {
			netif_napi_del(wcb->gro_napis[j]);
			devm_kfree(wcb->mdev->dev, wcb->gro_napis[j]);
		}
	}

	return -ENOMEM;
}
#else
static void mtk_wwan_gro_napi_exit(struct mtk_wwan_ctlb *wcb)
{
}

static int mtk_wwan_gro_napi_init(struct mtk_wwan_ctlb *wcb, struct net_device *dev)
{
	int i;

	for (i = 0; i < wcb->data_blk->trans_info.rxq_cnt; i++)
		wcb->gro_napis[i] = wcb->data_blk->trans_info.napis[i];

	return 0;
}
#endif /* CONFIG_DATA_CPU_LOADING_OPTIMIZE */
#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
static void mtk_wwan_trans_napi_exit(struct mtk_wwan_ctlb *wcb)
{
	int i;

	for (i = 0; i < wcb->data_blk->trans_info.rxq_cnt; i++) {
		if (!wcb->data_blk->trans_info.napis[i])
			continue;
		MTK_INFO(wcb->mdev, "napi%d - 0x%llx, napi_id=%d del\n", i,
			 (u64)wcb->data_blk->trans_info.napis[i],
			 wcb->data_blk->trans_info.napis[i]->napi_id);
		netif_napi_del(wcb->data_blk->trans_info.napis[i]);
	}
}

static int mtk_wwan_trans_napi_init(struct mtk_wwan_ctlb *wcb, struct net_device *dev)
{
	struct napi_struct *napi;
	int i, j;

	for (i = 0; i < wcb->data_blk->trans_info.rxq_cnt; i++) {
		napi = wcb->data_blk->trans_info.napis[i];
		if (!napi) {
			MTK_ERR(wcb->mdev, "Invalid parameter, napi=%d is NULL\n", i);
			goto napi_del;
		}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
		netif_napi_add_weight(dev, napi, wcb->data_blk->hif_ops->poll, napi_budget);
#else
		netif_napi_add(dev, napi, wcb->data_blk->hif_ops->poll, napi_budget);
#endif

		MTK_INFO(wcb->mdev, "napi%d-0x%llx napi_id=%d add done\n", i,
			 (u64)napi, napi->napi_id);
	}

	return 0;

napi_del:
	for (j = i - 1; j >= 0; j--)
		netif_napi_del(wcb->data_blk->trans_info.napis[j]);

	return -EINVAL;
}
#endif
static void mtk_wwan_setup(struct net_device *dev)
{
	dev->watchdog_timeo = MTK_NETDEV_WDT;
	dev->mtu = ETH_DATA_LEN;
	dev->min_mtu = ETH_MIN_MTU;

	dev->features = NETIF_F_SG;
	dev->hw_features = NETIF_F_SG;

	dev->features |= NETIF_F_GRO;
	dev->hw_features |= NETIF_F_GRO;

	dev->features |= NETIF_F_RXHASH;
	dev->hw_features |= NETIF_F_RXHASH;

	dev->tx_queue_len = DEFAULT_TX_QUEUE_LEN;

	dev->flags = IFF_NOARP;
	dev->type = ARPHRD_NONE;

	dev->needs_free_netdev = true;

	dev->netdev_ops = &mtk_netdev_ops;
	mtk_wwan_ethtool_set_ops(dev);
}

static int mtk_wwan_newlink(void *ctxt, struct net_device *dev, u32 intf_id,
			    struct netlink_ext_ack *extack)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	struct mtk_wwan_ctlb *wcb = ctxt;
	int ret;

	if (intf_id > MTK_MAX_INTF_ID) {
		MTK_WARN(wcb->mdev, "Interface id=%u invalid\n", intf_id);
		return -EINVAL;
	}

	if (rcu_access_pointer(wcb->wwan_inst[intf_id])) {
		MTK_WARN(wcb->mdev, "Failed to access wwan_inst, interface id=%u\n", intf_id);
		return -EBUSY;
	}

	dev->max_mtu = wcb->data_blk->trans_info.max_mtu;

	if (wcb->data_blk->trans_info.cap & DATA_F_GRO_HW) {
		dev->features |= NETIF_F_GRO_HW;
		dev->hw_features |= NETIF_F_GRO_HW;
	}
	if (wcb->data_blk->trans_info.cap & DATA_F_RXCSUM) {
		dev->features |= NETIF_F_RXCSUM;
		dev->hw_features |= NETIF_F_RXCSUM;
	}
	if (wcb->data_blk->trans_info.cap & DATA_F_TXCSUM) {
		dev->features |= NETIF_F_HW_CSUM;
		dev->hw_features |= NETIF_F_HW_CSUM;
	}

	wwan_inst->wcb = wcb;
	wwan_inst->netdev = dev;
	wwan_inst->intf_id = intf_id;
	wwan_inst->pkt_prio = PKT_PRIO_MAX;
	wwan_inst->hif_ops = wcb->data_blk->hif_ops;

	ret = register_netdevice(dev);
	if (ret) {
		MTK_WARN(wcb->mdev, "Failed to register netdevice, interface id=%u, ret = %d\n",
			 intf_id, ret);
		return ret;
	}

	rcu_assign_pointer(wcb->wwan_inst[intf_id], wwan_inst);

	netif_device_attach(dev);

	MTK_INFO(wcb->mdev, "%s registered, interface id=%u\n", dev->name, intf_id);

	return 0;
}

static void mtk_wwan_dellink(void *ctxt, struct net_device *dev,
			     struct list_head *head)
{
	struct mtk_wwan_instance *wwan_inst = wwan_netdev_drvpriv(dev);
	int intf_id = wwan_inst->intf_id;
	struct mtk_wwan_ctlb *wcb = ctxt;

	if (WARN_ON(rcu_access_pointer(wcb->wwan_inst[intf_id]) != wwan_inst))
		return;

	RCU_INIT_POINTER(wcb->wwan_inst[intf_id], NULL);

#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	wcb->data_blk->hif_ops->flush_rxq(wcb->data_blk);
#endif
	unregister_netdevice_queue(dev, head);

	MTK_INFO(wcb->mdev, "Netdevice unregistered, interface id=%u\n", intf_id);
}

static const struct wwan_ops mtk_wwan_ops = {
	.priv_size = sizeof(struct mtk_wwan_instance),
	.setup = mtk_wwan_setup,
	.newlink = mtk_wwan_newlink,
	.dellink = mtk_wwan_dellink,
};

void mtk_wwan_notify(struct mtk_data_blk *data_blk, enum mtk_data_evt evt, u64 data)
{
	struct mtk_wwan_ctlb *wcb;

	if (unlikely(!data_blk || !data_blk->wcb)) {
		pr_err("Invalid parameter, data_blk = 0x%llx\n", (u64)data_blk);
		return;
	}

	wcb = data_blk->wcb;

	switch (evt) {
	case DATA_EVT_TX_START:
		mtk_wwan_start_txq(wcb, data);
		break;
	case DATA_EVT_TX_STOP:
		mtk_wwan_stop_txq(wcb, data);
		break;
#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
	case DATA_EVT_RX_START:
		mtk_wwan_trans_napi_enable(wcb);
		break;
	case DATA_EVT_RX_STOP:
		mtk_wwan_trans_napi_disable(wcb);
		break;
#endif
#if defined(CONFIG_DATA_CPU_LOADING_OPTIMIZE) || defined(CONFIG_DATA_GRO_WITHOUT_NAPI_POLL)
	case DATA_EVT_RX_FLUSH:
		mtk_wwan_gro_normal_list(wcb->gro_napis[data]);
		break;
#endif
	case DATA_EVT_REG_DEV:
		if (!wcb->reg_done) {
			wwan_register_ops(wcb->mdev->dev, &mtk_wwan_ops, wcb, MTK_DFLT_INTF_ID);
			wcb->reg_done = true;
		}
		break;
	case DATA_EVT_UNREG_DEV:
		if (wcb->reg_done) {
			wwan_unregister_ops(wcb->mdev->dev);
			wcb->reg_done = false;
		}
		break;

	case DATA_EVT_DUMP:
		mtk_wwan_dump(wcb);
		break;

	default:
		MTK_INFO(wcb->mdev, "Invalid parameter, DATA_EVENT=%d\n", evt);
		WARN_ON_ONCE(true);
		break;
	}
}
EXPORT_SYMBOL(mtk_wwan_notify);

int mtk_wwan_init(struct mtk_data_blk *data_blk)
{
	struct mtk_wwan_ctlb *wcb;
	int ret;

	wcb = devm_kzalloc(data_blk->mdev->dev, sizeof(*wcb), GFP_KERNEL);
	if (unlikely(!wcb))
		return -ENOMEM;

	wcb->mdev = data_blk->mdev;
	wcb->data_blk = data_blk;

	/* Multiple virtual network devices share one physical device,
	 * so we use dummy device to enable NAPI for multiple virtual network devices.
	 */
	init_dummy_netdev(&wcb->dummy_dev);

	data_blk->wcb = wcb;
	ret = mtk_wwan_gro_napi_init(wcb, &wcb->dummy_dev);
	if (ret < 0)
		goto wcb_free;
#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
	ret = mtk_wwan_trans_napi_init(wcb, &wcb->dummy_dev);
	if (ret < 0)
		goto gro_napi_exit;
#endif
	MTK_INFO(data_blk->mdev, "MTK TSQ Shift=%u, mtk wwan init done\n", mtk_tsq_shift);

	return 0;

#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
gro_napi_exit:
	mtk_wwan_gro_napi_exit(wcb);
#endif

wcb_free:
	data_blk->wcb = NULL;
	devm_kfree(data_blk->mdev->dev, wcb);

	return ret;
}

void mtk_wwan_exit(struct mtk_data_blk *data_blk)
{
	struct mtk_wwan_ctlb *wcb = data_blk->wcb;

	if (unlikely(!wcb)) {
		pr_err("Invalid parameter data_blk = 0x%llx\n", (u64)data_blk);
		return;
	}
#ifndef CONFIG_DATA_GRO_WITHOUT_NAPI_POLL
	mtk_wwan_trans_napi_exit(wcb);
#endif
	mtk_wwan_gro_napi_exit(wcb);
	devm_kfree(data_blk->mdev->dev, wcb);
	data_blk->wcb = NULL;

	MTK_INFO(data_blk->mdev, "mtk wwan exit done\n");
}

module_param(napi_budget, uint, 0444);
MODULE_PARM_DESC(napi_budget, "This value indicates at most packets receive once napi schedule, default 128\n");
module_param(mtk_tsq_shift, uint, 0644);
MODULE_PARM_DESC(mtk_tsq_shift, "Default TCP small queue budget(1 second >> bit shift), default 7, ~8ms.\n");
