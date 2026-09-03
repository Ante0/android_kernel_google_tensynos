// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA Core Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <nep/nep.h>
#include <nep/nep_apf.h>
#include <nep/nep_tables.h>
#include <nep/offload.h>
#include <nep/util/utilization_monitor.h>
#include <common/core.h>
#include <nep/ring_manager_instance.h>
#include <wlan/noa_wlan_client.h>
#include "noa.h"

#define NIPQUAD_FMT "%u.%u.%u.%u"
#define NIPQUAD(addr) \
	((unsigned char *)&addr)[0], \
	((unsigned char *)&addr)[1], \
	((unsigned char *)&addr)[2], \
	((unsigned char *)&addr)[3]

#define NIP6_FMT "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x"
#define NIP6(addr) \
	ntohs((addr).s6_addr16[0]), \
	ntohs((addr).s6_addr16[1]), \
	ntohs((addr).s6_addr16[2]), \
	ntohs((addr).s6_addr16[3]), \
	ntohs((addr).s6_addr16[4]), \
	ntohs((addr).s6_addr16[5]), \
	ntohs((addr).s6_addr16[6]), \
	ntohs((addr).s6_addr16[7])

struct noa_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct noa_core *, char *);
	ssize_t (*store)(struct noa_core *, const char *, size_t count);
};

static ssize_t noa_dump_dma_ring(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	struct noa_port *port;
	int i;

	cnt += scnprintf(buf + cnt, len - cnt,
		"======== Ring Address ========\n");
	for (i = 0 ; i < NOA_PORT_MAX; i++) {
		port = noa_sim_get_port(i);
		if (!port->priv)
			continue;

		cnt += scnprintf(buf + cnt, len - cnt,
			"======== Port %d: %s ========\n", i, port->name);

		cnt += scnprintf(buf + cnt, len - cnt,
			"irq: %d, doorbell %p (%d), intm %p (%d), ints %p (%d)\n",
			port->irq,
			&port->doorbell,port->doorbell,
			&port->intm, port->intm,
			&port->ints, port->ints);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "======== Ring Values ========\n");
	cnt += NoaRingManagerInstanceDumpAll(buf + cnt, len - cnt);

	return cnt;
}

static ssize_t noa_dump_nep_stat(struct noa_core *core, char *buf)
{
	struct noa_simulator *sim = noa_sim_get();
	int len = PAGE_SIZE;
	int cnt = 0, i;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump NEP statistic ===\n");
	cnt += scnprintf(buf + cnt, len - cnt, "==== Source Port Counter ===\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu\n", noa_port_id_to_name(i),
				 sim->nep_stat.src[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== Destination Port Counter ===\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu\n", noa_port_id_to_name(i),
				 sim->nep_stat.dst[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== DMA Copy Counter ===\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu\n", noa_port_id_to_name(i),
				 sim->nep_stat.cp[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== Forward Counter ===\n");
	for (i = 0; i < FWD_REASON_MAX; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu\n", noa_reason_to_str(i),
				 sim->nep_stat.reason[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== Mode Counter ===\n");
	for (i = 0; i < NOAD_MODE_MAX; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu\n", noa_mode_to_str(i),
				 sim->nep_stat.mode[i]);
	}
	return cnt;
}

static ssize_t noa_dump_session_entry(struct noa_session *session, char *buf, int len)
{
	struct noa_session_common *entry = &session->common;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
		"DW0::ver: %d, sport: %d, dport: %d, rsv1: %d, state: %d, ageout: %d\n",
		entry->ver, entry->sport, entry->dport, entry->rsv1, entry->state, entry->ageout);
	cnt += scnprintf(buf + cnt, len - cnt,
		"DW1::ethtype: %d, src_mac1: %02X:%02X\n",
		entry->eth_type, entry->src_mac1[0], entry->src_mac1[1]);
	cnt += scnprintf(buf + cnt, len - cnt,
		"DW2::src_mac2: %02X:%02X:%02X:%02X\n",
		entry->src_mac2[0], entry->src_mac2[1], entry->src_mac2[2], entry->src_mac2[3]);
	cnt += scnprintf(buf + cnt, len - cnt,
		"DW3::vlan_act: %d, sec_act: %d, rsv2: %d, dst_mac1: %02X:%02X\n",
		entry->vlan_act, entry->sec_act, entry->rsv2, entry->dst_mac1[0],
			entry->dst_mac1[1]);
	cnt += scnprintf(buf + cnt, len - cnt,
		"DW4::dst_mac2: %02X:%02X:%02X:%02X\n",
		entry->dst_mac2[0], entry->dst_mac2[1], entry->dst_mac2[2], entry->dst_mac2[3]);
	cnt += scnprintf(buf + cnt, len - cnt,
		"DW5::vlan_info: %08X\n", entry->vlan_info);
	cnt += scnprintf(buf + cnt, len - cnt, "DW6::sec_info: %08X\n", entry->sec_info);
	cnt += scnprintf(buf + cnt, len - cnt,
		"DW7::src_ifidx: %d, dst_ifidx: %d, flow_id: %d, clat: %d, related_id %d\n",
		entry->src_ifidx, entry->dst_ifidx, entry->flow_id, entry->clat, entry->related_id);
	return cnt;
}

static ssize_t noa_dump_session(struct noa_core *core, char *buf)
{
	struct noa_simulator *sim = noa_sim_get();
	struct noa_session *entries= sim->session_entries;
	int len = PAGE_SIZE;
	int cnt = 0, i;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Session Entries ===\n");
	for (i = 0; i < SESSION_ENTRY_MAX; i++) {
		if (!entries[i].common.state)
			continue;
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i);
		cnt += noa_dump_session_entry(&entries[i], buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_tether4_key_value(Tether4Key *key, Tether4Value *value, char *buf, int len)
{
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
			 "l4Proto: %d, src4 = " NIPQUAD_FMT ", dst4 = " NIPQUAD_FMT ", srcPort: %d, "
			 "dstPort: %d\n",
			 key->l4Proto, NIPQUAD(key->src4), NIPQUAD(key->dst4), key->srcPort, key->dstPort);
	cnt += scnprintf(buf + cnt, len - cnt, "dstMac: %02X:%02X:%02X:%02X:%02X:%02X"
			 " mangle46: " NIP6_FMT ", manglePort: %u, "
			 ", last_used: %llu\n",
			 value->dstMac[0], value->dstMac[1],
			 value->dstMac[2], value->dstMac[3],
			 value->dstMac[4], value->dstMac[5],
			 NIP6(value->mangle46), value->manglePort,
			 value->last_used);
	return cnt;
}

static ssize_t noa_dump_downstream4_table(struct noa_core *core, char *buf)
{
	struct offload_info *ol_info = get_tethering_offload_info();
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	u32 bucket = 0, last = 0;
	Tether4Key key;
	Tether4Value value;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Downstream4 Table ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "RX iif %d -> TX iif %d\n",
					 ol_info->upstreamIif, ol_info->downstreamIif);
	cnt += scnprintf(buf + cnt, len - cnt, "srcMac(gw) dstMac(%02X:%02X:%02X:%02X:%02X:%02X) -> "
					 "srcMac(%02X:%02X:%02X:%02X:%02X:%02X) dstMac(client)\n",
					 ol_info->upstream_if_Mac[0], ol_info->upstream_if_Mac[1],
					 ol_info->upstream_if_Mac[2], ol_info->upstream_if_Mac[3],
					 ol_info->upstream_if_Mac[4], ol_info->upstream_if_Mac[5],
					 ol_info->downstream_if_Mac[0], ol_info->downstream_if_Mac[1],
					 ol_info->downstream_if_Mac[2], ol_info->downstream_if_Mac[3],
					 ol_info->downstream_if_Mac[4], ol_info->downstream_if_Mac[5]);

	while (nep_tables_downstream4_map_dump_next(&bucket, &last, (void *)&key,
						  (void *)&value) == 0) {
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i++);
		cnt += noa_dump_tether4_key_value(&key, &value, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_upstream4_table(struct noa_core *core, char *buf)
{
	struct offload_info *ol_info = get_tethering_offload_info();
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	u32 bucket = 0, last = 0;
	Tether4Key key;
	Tether4Value value;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump upstream4 Table ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "RX iif %d -> TX iif %d\n",
					 ol_info->downstreamIif, ol_info->upstreamIif);
	cnt += scnprintf(buf + cnt, len - cnt, "srcMac(client) dstMac(%02X:%02X:%02X:%02X:%02X:%02X)"
					 " -> srcMac(%02X:%02X:%02X:%02X:%02X:%02X) dstMac(gw)\n",
					 ol_info->downstream_if_Mac[0], ol_info->downstream_if_Mac[1],
					 ol_info->downstream_if_Mac[2], ol_info->downstream_if_Mac[3],
					 ol_info->downstream_if_Mac[4], ol_info->downstream_if_Mac[5],
					 ol_info->upstream_if_Mac[0], ol_info->upstream_if_Mac[1],
					 ol_info->upstream_if_Mac[2], ol_info->upstream_if_Mac[3],
					 ol_info->upstream_if_Mac[4], ol_info->upstream_if_Mac[5]);

	while (nep_tables_upstream4_map_dump_next(&bucket, &last, (void *)&key,
						  (void *)&value) == 0) {
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i++);
		cnt += noa_dump_tether4_key_value(&key, &value, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_downstream6_key_value(TetherDownstream6Key *key, Tether6Value *value,
						     char *buf, int len)
{
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
			 "Key# iif: %d, dstMac: %02X:%02X:%02X:%02X:%02X:%02X, neigh6: "
			 NIP6_FMT "\n",
			 key->iif, key->dstMac[0], key->dstMac[1], key->dstMac[2], key->dstMac[3],
			 key->dstMac[4], key->dstMac[5], NIP6(key->neigh6));
	cnt += scnprintf(buf + cnt, len - cnt, "Value# oif: %d, macHeader: %02X%02X%02X%02X%02X%02X"
			 "%02X%02X%02X%02X%02X%02X%02X%02X, pmtu: %u\n",
			 value->oif, value->macHeader.h_dest[0], value->macHeader.h_dest[1],
			 value->macHeader.h_dest[2], value->macHeader.h_dest[3],
			 value->macHeader.h_dest[4], value->macHeader.h_dest[5],
			 value->macHeader.h_source[0], value->macHeader.h_source[1],
			 value->macHeader.h_source[2], value->macHeader.h_source[3],
			 value->macHeader.h_source[4], value->macHeader.h_source[5],
			 (ntohs(value->macHeader.h_proto) & 0xFF00) >> 8,
			 ntohs(value->macHeader.h_proto) & 0xFF,
			 value->pmtu);
	return cnt;
}

static ssize_t noa_dump_upstream6_key_value(TetherUpstream6Key *key, Tether6Value *value,
						   char *buf, int len)
{
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
			"Key# iif: %d, dstMac: %02X:%02X:%02X:%02X:%02X:%02X\n",
			key->iif, key->dstMac[0], key->dstMac[1], key->dstMac[2], key->dstMac[3],
			key->dstMac[4], key->dstMac[5]);
	cnt += scnprintf(buf + cnt, len - cnt, "Value# oif: %d, macHeader: %02X%02X%02X%02X%02X%02X"
			 "%02X%02X%02X%02X%02X%02X%02X%02X, pmtu: %u\n",
			 value->oif, value->macHeader.h_dest[0], value->macHeader.h_dest[1],
			 value->macHeader.h_dest[2], value->macHeader.h_dest[3],
			 value->macHeader.h_dest[4], value->macHeader.h_dest[5],
			 value->macHeader.h_source[0], value->macHeader.h_source[1],
			 value->macHeader.h_source[2], value->macHeader.h_source[3],
			 value->macHeader.h_source[4], value->macHeader.h_source[5],
			 (ntohs(value->macHeader.h_proto) & 0xFF00) >> 8,
			 ntohs(value->macHeader.h_proto) & 0xFF,
			 value->pmtu);
	return cnt;
}

static ssize_t noa_dump_downstream6_table(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	u32 bucket = 0, last = 0;
	TetherDownstream6Key key;
	Tether6Value value;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Downstream6 Table ====\n");
	while (nep_tables_downstream6_map_dump_next(&bucket, &last, (void *)&key,
						    (void *)&value) == 0) {
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i++);
		cnt += noa_dump_downstream6_key_value(&key, &value, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_upstream6_table(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	u32 bucket = 0, last = 0;
	TetherUpstream6Key key;
	Tether6Value value;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump upstream6 Table ====\n");
	while (nep_tables_upstream6_map_dump_next(&bucket, &last, (void *)&key,
						  (void *)&value) == 0) {
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i++);
		cnt += noa_dump_upstream6_key_value(&key, &value, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_ingress6_key_value(ClatIngress6Key *key, ClatIngress6Value *value,
					   char *buf, int len)
{
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
			 "Key# iif: %d, pfx96: " NIP6_FMT ", local6: " NIP6_FMT "\n",
			 key->iif, NIP6(key->pfx96), NIP6(key->local6));
	cnt += scnprintf(buf + cnt, len - cnt, "Value# local4: " NIPQUAD_FMT "\n",
			 NIPQUAD(value->local4));
	return cnt;
}

static ssize_t noa_dump_ingress6_table(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	u32 bucket = 0, last = 0;
	ClatIngress6Key key;
	ClatIngress6Value value;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump ingress6 Table ====\n");
	while (nep_tables_ingress6_map_dump_next(&bucket, &last, (void *)&key,
						 (void *)&value) == 0) {
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i++);
		cnt += noa_dump_ingress6_key_value(&key, &value, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_egress4_key_value(ClatEgress4Key *key, ClatEgress4Value *value,
					  char *buf, int len)
{
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
			 "Key# iif: %d, local4: " NIPQUAD_FMT "\n",
			 key->iif, NIPQUAD(key->local4));
	cnt += scnprintf(buf + cnt, len - cnt, "Value# local6: " NIP6_FMT ", pfx96: " NIP6_FMT "\n",
			NIP6(value->local6), NIP6(value->pfx96));
	return cnt;
}

static ssize_t noa_dump_egress4_table(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	u32 bucket = 0, last = 0;
	ClatEgress4Key key;
	ClatEgress4Value value;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump egress4 Table ====\n");
	while (nep_tables_egress4_map_dump_next(&bucket, &last, (void *)&key,
						(void *)&value) == 0) {
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i++);
		cnt += noa_dump_egress4_key_value(&key, &value, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_stats_value(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	struct offload_info *ol_info = get_tethering_offload_info();

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Tethering Stats ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "ifindex: %u\n", ol_info->upstreamIif);
	cnt += scnprintf(buf + cnt, len - cnt,
		"rxPackets: %llu, rxBytes: %llu, rxErrors: %llu,"
		" txPackets: %llu, txBytes: %llu, txErrors: %llu\n",
		ol_info->stats.rxPackets, ol_info->stats.rxBytes, ol_info->stats.rxErrors,
		ol_info->stats.txPackets, ol_info->stats.txBytes, ol_info->stats.txErrors);

	return cnt;
}

static ssize_t noa_dump_limit_value(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;
	struct offload_info *ol_info = get_tethering_offload_info();

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Tethering Limit ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "ifindex : %u\n", ol_info->upstreamIif);
	cnt += scnprintf(buf + cnt, len - cnt, "limit : %llu\n", ol_info->limit_bytes);

	return cnt;
}

static ssize_t noa_dump_flowid_key_value(TetherFlowIdKey *key, TetherFlowIdValue *value,
						   char *buf, int len)
{
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "Key# dstMac: %02X:%02X:%02X:%02X:%02X:%02X, "
			 "priority: %u\n",
			 key->dstMac[0], key->dstMac[1], key->dstMac[2], key->dstMac[3],
			 key->dstMac[4], key->dstMac[5], key->priority);
	cnt += scnprintf(buf + cnt, len - cnt, "Value# flow id: %u\n", *value);
	return cnt;
}

static ssize_t noa_dump_flowid_table(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0, i = 0;
	u32 bucket = 0, last = 0;
	TetherFlowIdKey key;
	TetherFlowIdValue value;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump FlowId Table ====\n");
	while (nep_tables_flowid_map_dump_next(&bucket, &last, (void *)&key,
					      (void *)&value) == 0) {
		cnt += scnprintf(buf + cnt, len - cnt, "==== Entry %d ===\n", i++);
		cnt += noa_dump_flowid_key_value(&key, &value, buf + cnt, len - cnt);
	}
	return cnt;
}

static ssize_t noa_dump_netengine_stats(struct noa_core *core, char *buf)
{
	struct offload_info *ol_info = get_tethering_offload_info();
	struct noa_netengine_stat *stat = &ol_info->netengine_stat;
	int len = PAGE_SIZE;
	int cnt = 0, i;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Netengine Statistic ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "==== Upstream Source Counters ====\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
	cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu (v4 = %lu, v6 = %lu)\n",
			 noa_port_id_to_name(i),
			 stat->upstream_src_v4[i] + stat->upstream_src_v6[i],
			 stat->upstream_src_v4[i], stat->upstream_src_v6[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== Upstream Destination Counters ====\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
	cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu (v4 = %lu, v6 = %lu)\n",
			 noa_port_id_to_name(i),
			 stat->upstream_dst_v4[i] + stat->upstream_dst_v6[i],
			 stat->upstream_dst_v4[i], stat->upstream_dst_v6[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== Downstream Source Counters ====\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
	cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu (v4 = %lu, v6 = %lu)\n",
			 noa_port_id_to_name(i),
			 stat->downstream_src_v4[i] + stat->downstream_src_v6[i],
			 stat->downstream_src_v4[i], stat->downstream_src_v6[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== Downstream Destination Counters ====\n");
	for (i = 0; i < NOA_PORT_MAX; i++) {
	cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %lu (v4 = %lu, v6 = %lu)\n",
			 noa_port_id_to_name(i),
			 stat->downstream_dst_v4[i] + stat->downstream_dst_v6[i],
			 stat->downstream_dst_v4[i], stat->downstream_dst_v6[i]);
	}
	cnt += scnprintf(buf + cnt, len - cnt, "==== IPv4/IPv6 Counters ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "IPv4 packets\t: %lu\n",
			 ol_info->netengine_stat.ipv4_packets);
	cnt += scnprintf(buf + cnt, len - cnt, "IPv4 bytes\t: %lu\n",
			 ol_info->netengine_stat.ipv4_bytes);
	cnt += scnprintf(buf + cnt, len - cnt, "IPv6 packets\t: %lu\n",
			 ol_info->netengine_stat.ipv6_packets);
	cnt += scnprintf(buf + cnt, len - cnt, "IPv6 bytes\t: %lu\n",
			 ol_info->netengine_stat.ipv6_bytes);
	return cnt;
}

static ssize_t noa_clear_netengine_utilization_stats(struct noa_core *core, char *buf)
{
	struct noa_simulator *sim = noa_sim_get();
	int len = PAGE_SIZE;
	int cnt = 0;
	noa_sim_utilization_clear(&sim->offload_util);
	noa_sim_utilization_clear(&sim->no_offload_util);
	cnt += scnprintf(buf + cnt, len - cnt, "Clear netengine CPU utilization stats complete.\n");
	return cnt;
}

static ssize_t noa_dump_netengine_utilization_stats(struct noa_core *core, char *buf)
{
	struct noa_simulator *sim = noa_sim_get();
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "==== Dump netengine CPU utilization stats====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "APC busy percentage (no hw offloaded): %d%%\n",
			 noa_sim_utilization_calculate(&sim->no_offload_util));
	cnt += scnprintf(buf + cnt, len - cnt, "APC busy percentage (hw offload): %d%%\n",
			 noa_sim_utilization_calculate(&sim->offload_util));
	return cnt;
}

static ssize_t noa_dump_netengine_demo(struct noa_core *core, char *buf)
{
	struct offload_info *ol_info = get_tethering_offload_info();
	struct noa_netengine_stat *stat = &ol_info->netengine_stat;
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt,
					"==== Dump NOA tethering TX/RX packet counts ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "==== TX packet counts  ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "WiFi to Modem\t: %lu\n",
			 stat->upstream_dst_v4[NOA_PORT_MODEM_FW] +
			 stat->upstream_dst_v6[NOA_PORT_MODEM_FW]);
	cnt += scnprintf(buf + cnt, len - cnt, "WiFi to WiFi\t: %lu\n\n",
			 stat->upstream_dst_v4[NOA_PORT_WLAN_FW] +
			 stat->upstream_dst_v6[NOA_PORT_WLAN_FW]);

	cnt += scnprintf(buf + cnt, len - cnt, "==== RX packet counts  ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "* to WiFi\t: %lu\n",
			 stat->downstream_dst_v4[NOA_PORT_WLAN_FW] +
			 stat->downstream_dst_v6[NOA_PORT_WLAN_FW]);
	return cnt;
}

static ssize_t noa_dump_netengine_errors(struct noa_core *core, char *buf)
{
	struct noa_simulator *sim = noa_sim_get();
	int len = PAGE_SIZE;
	int cnt = 0, i;

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump Netengine Errors ====\n");
	for (i = 0; i < BPF_TETHER_ERR__MAX; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%s\t: %d\n", bpf_tether_errors[i],
				 sim->nep_error_counters[i]);
	}
	return cnt;
}

static ssize_t noa_dump_apf_program(struct noa_core *core, char *buf)
{
	struct apf_info *apf_info = nep_apf_get_info();
	int len = PAGE_SIZE;
	int cnt = 0, i;
	uint64_t boot_time_ms = ktime_to_ms(ktime_get_boottime());

	cnt += scnprintf(buf + cnt, len - cnt, "==== NOA Dump APF Program ====\n");
	cnt += scnprintf(buf + cnt, len - cnt, "program len = %d, program age = %lld (ms)\n",
			 apf_info->apf_program_len, boot_time_ms - apf_info->apf_program_time_ms);
	/* Print the APF program */
	for (i = 0; i < apf_info->apf_program_len; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%02x", apf_info->apf_program[i]);
		if ((i+1) % 40 == 0) {
			cnt += scnprintf(buf + cnt, len - cnt, "\n");
		}
	}
	cnt += scnprintf(buf + cnt, len - cnt, "\n");
	/* Print the last 60 counters (4 bytes each). */
	for (i = 0; i < 240; i++) {
		cnt += scnprintf(buf + cnt, len - cnt, "%02x",
				 apf_info->apf_program[NEP_MAX_APF_PROGRAM_LEN - 1 - i]);
		if ((i+1) % 40 == 0) {
			cnt += scnprintf(buf + cnt, len - cnt, "\n");
		}
	}
	cnt += scnprintf(buf + cnt, len - cnt, "\n");
	return cnt;
}

static ssize_t noa_sim_config_read(struct noa_core *core, char *buf)
{
	struct noa_simulator *sim = noa_sim_get();
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "force_fallback: %d\n", sim->force_fallback);
	cnt += scnprintf(buf + cnt, len - cnt, "dbg\t: %d\n", sim->dbg);
	cnt += scnprintf(buf + cnt, len - cnt, "monitor_packet_type\t: %d (0=NONE, 1=RS)\n", sim->monitor_packet_type);
	return cnt;
}

enum {
	NOA_SIM_CONFIG_DBG,
	NOA_SIM_CONFIG_FORCE_FALLBACK,
	NOA_SIM_CONFIG_MONITOR_PACKET,
	NOA_SIM_CONFIG_MAX
};

static ssize_t noa_sim_config_write(struct noa_core *core, const char *buf, size_t size)
{
	struct noa_simulator *sim = noa_sim_get();
	int command;
	int value;

	sscanf(buf, "%d %d", &command, &value);
	switch(command) {
	case NOA_SIM_CONFIG_DBG:
		sim->dbg = !sim->dbg;
		break;
	case NOA_SIM_CONFIG_FORCE_FALLBACK:
		sim->force_fallback = !sim->force_fallback;
		break;
	case NOA_SIM_CONFIG_MONITOR_PACKET:
		sim->monitor_packet_type = value;
		break;
	default:
		break;
	}
	return size;
}

#if IS_ENABLED(CONFIG_NOA_WLAN_SUPPORT)
static ssize_t noa_wlan_enable_read(struct noa_core *core, char *buf)
{
	int len = PAGE_SIZE;
	int cnt = 0;

	cnt += scnprintf(buf + cnt, len - cnt, "noa_wlan_enable: %d\n", core->noa_wlan_enable);
	return cnt;
}

static ssize_t noa_wlan_enable_write(struct noa_core *core, const char *buf, size_t size)
{
	int value;
	bool noa_en;

	sscanf(buf, "%d", &value);
	noa_en = !!value;
	if (core->noa_wlan_enable != noa_en) {
		/* do dynamic switch */
		noa_wlan_dynamic_switch(noa_en);
		core->noa_wlan_enable = noa_en;
	}
	return size;
}
#endif

static struct noa_kobj_attr attr_dump_dma_ring =
	__ATTR(dump_dma_ring, 0664, noa_dump_dma_ring, NULL);

static struct noa_kobj_attr attr_dump_session =
	__ATTR(dump_session, 0664, noa_dump_session, NULL);

static struct noa_kobj_attr attr_dump_nep_stat =
	__ATTR(dump_nep_stat, 0664, noa_dump_nep_stat, NULL);

static struct noa_kobj_attr attr_dump_downstream4_table =
	__ATTR(dump_downstream4_table, 0664, noa_dump_downstream4_table, NULL);

static struct noa_kobj_attr attr_dump_upstream4_table =
	__ATTR(dump_upstream4_table, 0664, noa_dump_upstream4_table, NULL);

static struct noa_kobj_attr attr_dump_downstream6_table =
	__ATTR(dump_downstream6_table, 0664, noa_dump_downstream6_table, NULL);

static struct noa_kobj_attr attr_dump_upstream6_table =
	__ATTR(dump_upstream6_table, 0664, noa_dump_upstream6_table, NULL);

static struct noa_kobj_attr attr_dump_ingress6_table =
	__ATTR(dump_ingress6_table, 0664, noa_dump_ingress6_table, NULL);

static struct noa_kobj_attr attr_dump_egress4_table =
	__ATTR(dump_egress4_table, 0664, noa_dump_egress4_table, NULL);

	static struct noa_kobj_attr attr_dump_stats_value =
	__ATTR(dump_stats_table, 0664, noa_dump_stats_value, NULL);

static struct noa_kobj_attr attr_dump_limit_value =
	__ATTR(dump_limit_table, 0664, noa_dump_limit_value, NULL);

static struct noa_kobj_attr attr_dump_flowid_table =
	__ATTR(dump_flowid_table, 0664, noa_dump_flowid_table, NULL);

static struct noa_kobj_attr attr_dump_netengine_stats =
	__ATTR(dump_netengine_stats, 0664, noa_dump_netengine_stats, NULL);

static struct noa_kobj_attr attr_clear_netengine_utilization_stats =
	__ATTR(clear_netengine_utilization_stats, 0664, noa_clear_netengine_utilization_stats, NULL);

static struct noa_kobj_attr attr_dump_netengine_utilization_stats =
	__ATTR(dump_netengine_utilization_stats, 0664, noa_dump_netengine_utilization_stats, NULL);

static struct noa_kobj_attr attr_dump_netengine_demo =
	__ATTR(dump_netengine_demo, 0664, noa_dump_netengine_demo, NULL);

static struct noa_kobj_attr attr_dump_netengine_errors =
	__ATTR(dump_netengine_errors, 0664, noa_dump_netengine_errors, NULL);

static struct noa_kobj_attr attr_dump_apf_program =
        __ATTR(dump_apf_program, 0664, noa_dump_apf_program, NULL);

static struct noa_kobj_attr attr_config =
	__ATTR(config, 0664, noa_sim_config_read, noa_sim_config_write);

#if IS_ENABLED(CONFIG_NOA_WLAN_SUPPORT)
static struct noa_kobj_attr attr_wlan_enable =
	__ATTR(noa_wlan_enable, 0664, noa_wlan_enable_read, noa_wlan_enable_write);
#endif

static ssize_t noa_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct noa_core *core = container_of(kobj, struct noa_core, kobj);
	struct noa_kobj_attr *noa_attr = container_of(attr, struct noa_kobj_attr, attr);

	if (noa_attr->show)
		return noa_attr->show(core, buf);
	return -EIO;
}

static ssize_t noa_sysfs_store(struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	struct noa_core *core = container_of(kobj, struct noa_core, kobj);
	struct noa_kobj_attr *noa_attr = container_of(attr, struct noa_kobj_attr, attr);

	if (noa_attr->store)
		return noa_attr->store(core, buf, count);
	return -EIO;
}

static struct attribute *default_file_attrs[] = {
	&attr_dump_dma_ring.attr,
	&attr_dump_session.attr,
	&attr_dump_nep_stat.attr,
	&attr_dump_upstream4_table.attr,
	&attr_dump_downstream4_table.attr,
	&attr_dump_upstream6_table.attr,
	&attr_dump_downstream6_table.attr,
	&attr_dump_ingress6_table.attr,
	&attr_dump_egress4_table.attr,
	&attr_dump_stats_value.attr,
	&attr_dump_limit_value.attr,
	&attr_dump_flowid_table.attr,
	&attr_dump_netengine_stats.attr,
	&attr_clear_netengine_utilization_stats.attr,
	&attr_dump_netengine_utilization_stats.attr,
	&attr_dump_netengine_demo.attr,
	&attr_dump_netengine_errors.attr,
	&attr_dump_apf_program.attr,
	&attr_config.attr,
#if IS_ENABLED(CONFIG_NOA_WLAN_SUPPORT)
	&attr_wlan_enable.attr,
#endif
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static struct sysfs_ops noa_sysfs_ops = {
	.show = noa_sysfs_show,
	.store = noa_sysfs_store,
};

static struct kobj_type noa_ktype = {
	.sysfs_ops = &noa_sysfs_ops,
	.default_groups = default_file_groups,
};

int noa_subsysfs_init(void *priv, const char *name, struct kobj_type *ktype)
{
	struct noa_core_entry *entry = container_of(priv, struct noa_core_entry, priv);
	int ret;

	ret = kobject_init_and_add(&entry->kobj, ktype, &entry->core->kobj, name);
	if (ret)
		kobject_put(&entry->kobj);
	return ret;
}

void noa_subsysfs_exit(void *priv)
{
	struct noa_core_entry *entry = container_of(priv, struct noa_core_entry, priv);
	kobject_del(&entry->kobj);
	kobject_put(&entry->kobj);
}

int noa_sysfs_init(struct noa_core *core)
{
	int ret;

	ret = kobject_init_and_add(&core->kobj, &noa_ktype, NULL, "noa");
	if (ret)
		kobject_put(&core->kobj);
	return ret;
}

void noa_sysfs_exit(struct noa_core *core)
{
	kobject_del(&core->kobj);
	kobject_put(&core->kobj);
}
