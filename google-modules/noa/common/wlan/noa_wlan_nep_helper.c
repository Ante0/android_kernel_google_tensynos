// SPDX-License-Identifier: GPL-2.0-only
/*
 * NEP helper for NOA WiFi Driver
 *
 * Copyright 2025 Google LLC.
 *
 */
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/net_namespace.h>
#include <nep/netengine.h>
#include <nep/netengine_utils.h>
#include <nep/ring_manager.h>

#include "noa_wlan_client.h"
#include "noa_wlan_nep_helper.h"
#include "wlan_trace.h"

#define noa_wlan_nep_ring_pkt_cnt_inc(client, name) (client->nep_ring_stats.name++)
#define noa_wlan_nep_sw_ring_tx_pkt_inc(client, id)                                                \
	noa_wlan_nep_ring_pkt_cnt_inc(client, nep_sw_input[id])
#define noa_wlan_nep_sw_ring_rx_pkt_inc(client) noa_wlan_nep_ring_pkt_cnt_inc(client, nep_sw_output)

static void noa_wlan_fill_session_ipv4(struct noa_wlan_client *client,
				       struct noa_session_ipv4 *ipv4, struct iphdr *ip)
{
	trace_wlan_ipv4(ip);
	ipv4->priority = ip->tos >> 5;
}

static void noa_wlan_fill_session_ipv6(struct noa_wlan_client *client,
				       struct noa_session_ipv6 *ipv6, struct ipv6hdr *ipv6hdr)
{
	trace_wlan_ipv6(ipv6hdr);
	ipv6->priority = ipv6hdr->priority;
}

int noa_wlan_add_session_entry(struct noa_wlan_client *client, u8 *pkt,
			       struct noa_session_info *txinfo)
{
	struct noa_session session;
	struct noa_session_common *entry = &session.common;
	struct ethhdr *eth = (struct ethhdr *)pkt;
	const int llc_header_size = ntohs(eth->h_proto) < 0x0600 ? DOT11_LLC_SNAP_HDR_LEN : 0;
	struct iphdr *iphdr = (struct iphdr *)(pkt + llc_header_size + sizeof(struct ethhdr));
	struct ipv6hdr *ipv6hdr = (struct ipv6hdr *)iphdr;
	u16 ethertype = txinfo->ethertype ? txinfo->ethertype : eth->h_proto;

	/* not support multicast destination MAC */
	if ((eth->h_dest[0] & 0x1) == 0x1)
		return 0;

	/* forwarding support IPv4 and IPv6 packet only */
	if (ethertype != htons(ETH_P_IP) && ethertype != htons(ETH_P_IPV6))
		return 0;

	memset(&session, 0, sizeof(struct noa_session));
	entry->ver = SESSION_VER_P26;
	entry->ageout = 30;
	entry->related_id = SESSION_RELATED_ID_INVALID;
	entry->dport = SESSION_PORT_WIFI_FW;
	entry->sport = SESSION_PORT_WIFI_FW;
	entry->eth_type = ethertype;
	entry->src_ifidx = txinfo->ifidx;
	entry->dst_ifidx = txinfo->ifidx;
	entry->flow_id = txinfo->flowid;
	entry->sec_act = SESSION_SECACT_NOACT;
	entry->vlan_act = SESSION_VLANACT_NOACT;
	entry->clat = 0;
	entry->state = SESSION_STATE_USED;
	entry->ignore_llc = txinfo->ignore_llc;
	memcpy(entry->dst_mac1, &eth->h_dest[0], 2);
	memcpy(entry->dst_mac2, &eth->h_dest[2], 4);
	memcpy(entry->src_mac1, &eth->h_source[0], 2);
	memcpy(entry->src_mac2, &eth->h_source[2], 4);
	/* IPv4/v6 header */
	if (txinfo->ethertype == htons(ETH_P_IP)) {
		noa_wlan_fill_session_ipv4(client, &session.noa_session_ip.ipv4, iphdr);
	} else {
		noa_wlan_fill_session_ipv6(client, &session.noa_session_ip.ipv6, ipv6hdr);
	}
	noa_sim_add_session_entry(&session);
	return 0;
}
EXPORT_SYMBOL_GPL(noa_wlan_add_session_entry);

int noa_wlan_client_ring_write(struct noa_wlan_client *client, noa_ring_producer *ring, void *data)
{
	int ret;

	ret = noa_ring_begin_processing(ring);
	if (ret < 0) {
		return ret;
	} else if (!ret) {
		dev_err(client->dev,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		return -EINVAL;
	}

	ret = noa_ring_write(ring, data, client->nep_tx_desc_sz);
	if (ret < 0 && ret != -EAGAIN)
		dev_err(client->dev, "Failed to write tx packet to NOA wlan fw input ring\n");

	noa_wlan_nep_sw_ring_tx_pkt_inc(client, 0);
	noa_ring_complete_processing(ring);
	return ret;
}
EXPORT_SYMBOL_GPL(noa_wlan_client_ring_write);

int noa_wlan_replenish_ring_write(struct noa_wlan_client *client, noa_ring_producer *ring,
				  struct noa_bm_buf *bm_bufs, u32 bm_bufs_num, bool to_dev)
{
	int ret;
	int i;
	struct buffer_repln_data data;

	ret = noa_ring_begin_processing(ring);
	if (ret < 0) {
		return ret;
	} else if (!ret) {
		dev_err(client->dev,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		return -EINVAL;
	}

	for (i = 0; i < bm_bufs_num; i++) {
		data.buf = &bm_bufs[i];
		data.to_dev = to_dev;
		ret = noa_ring_write(ring, &data, sizeof(data));
		if (ret < 0 && ret != -EAGAIN)
			dev_err(client->dev,
				"Failed to write rx replenishment to NOA wlan fw input ring\n");
	}
	noa_ring_complete_processing(ring);
	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(noa_wlan_replenish_ring_write);

int noa_wlan_feedback_ring_write(struct noa_wlan_client *client, noa_ring_producer *ring,
				 struct noa_desc *d)
{
	int ret;

	ret = noa_ring_begin_processing(ring);
	if (ret < 0) {
		return ret;
	} else if (!ret) {
		dev_err(client->dev,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		return -EINVAL;
	}
	noa_ring_write(ring, d, sizeof(struct noa_desc));
	noa_ring_complete_processing(ring);
	return 0;
}
EXPORT_SYMBOL_GPL(noa_wlan_feedback_ring_write);

bool noa_wlan_client_ring_is_empty(noa_ring_consumer *ring)
{
	return noa_ring_is_empty(ring);
}
EXPORT_SYMBOL_GPL(noa_wlan_client_ring_is_empty);

/**
 * This function will read items from the Ring until the Ring is empty.
 * It will return the number of items it read.
 */
int noa_wlan_client_ring_read_loop(struct noa_wlan_client *client, noa_ring_consumer *ring,
				   int (*read_func)(struct noa_wlan_client *, void **))
{
	int ret;
	int count = 0;

	ret = noa_ring_begin_processing(ring);
	if (ret < 0) {
		return ret;
	} else if (!ret) {
		dev_err(client->dev,
			"Ring %s is already in processing state, something may went wrong, "
			"because it is only allowed one thread to operate this ring.\n",
			ring->name);
		return -EINVAL;
	}
	/* currently we didn't limite the number of batch count */
	while (true) {
		struct noa_desc *desc;
		unsigned long data_addr = 0;
		ret = noa_ring_read(ring, &data_addr, sizeof(data_addr));
		if (!ret) {
			break;
		} else if (ret < 0 || !data_addr) {
			dev_err(client->dev,
				"Failed to read data from wifi sw ring, drop this item, err: %d\n",
				ret);
			count++;
			noa_ring_tail_inc(ring);
			continue;
		}
		desc = (struct noa_desc *)data_addr;
		ret = read_func(client, (void **)&desc);
		if (ret) {
			dev_err(client->dev,
				"Failed to handle read data from wifi sw ring, err: %d\n", ret);
		}
		count++;
		noa_wlan_nep_sw_ring_rx_pkt_inc(client);
	}
	noa_ring_complete_processing(ring);

	return count;
}
EXPORT_SYMBOL_GPL(noa_wlan_client_ring_read_loop);

void noa_wlan_ring_complete_processing(struct noa_ring_wrapper *ring)
{
	return noa_ring_complete_processing(ring);
}
EXPORT_SYMBOL_GPL(noa_wlan_ring_complete_processing);

ssize_t noa_wlan_ring_read(noa_ring_consumer *consumer, void *data, size_t len)
{
	return noa_ring_read(consumer, data, len);
}
EXPORT_SYMBOL_GPL(noa_wlan_ring_read);

int noa_wlan_ring_begin_processing(struct noa_ring_wrapper *ring)
{
	return noa_ring_begin_processing(ring);
}
EXPORT_SYMBOL_GPL(noa_wlan_ring_begin_processing);
