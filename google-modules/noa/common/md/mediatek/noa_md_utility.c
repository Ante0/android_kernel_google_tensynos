// Copyright 2024 Google LLC.


#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "noa_md_utility.h"

void noa_md_dump_ndev_info(struct net_device *netdev)
{
	if (!netdev) {
		NOA_MD_ERROR_LIMIT("netdev is null");
	}

	NOA_MD_INFO_LIMIT("netdev info:");
	NOA_MD_INFO_LIMIT("  netdev: 0x%lx", netdev);
	NOA_MD_INFO_LIMIT("  ifindex: %d", netdev->ifindex);
	NOA_MD_INFO_LIMIT("  name: %s", netdev->name);
	NOA_MD_INFO_LIMIT("  type: %d", netdev->type);
	NOA_MD_INFO_LIMIT("  mtu: %d", netdev->mtu);
	NOA_MD_INFO_LIMIT("  hard_header_len: %d", netdev->hard_header_len);
	NOA_MD_INFO_LIMIT("  addr_len: %d", netdev->addr_len);
	NOA_MD_INFO_LIMIT("  address: %pM", netdev->dev_addr);
	NOA_MD_INFO_LIMIT("  broadcast: %pM", netdev->broadcast);
	NOA_MD_INFO_LIMIT("  flags: 0x%lx", netdev->flags);
	NOA_MD_INFO_LIMIT("  gflags: 0x%lx", netdev->gflags);
	NOA_MD_INFO_LIMIT("  priv_flags: 0x%lx", netdev->priv_flags);
	NOA_MD_INFO_LIMIT("  features: 0x%llx", netdev->features);
	NOA_MD_INFO_LIMIT("  hw_features: 0x%llx", netdev->hw_features);
	NOA_MD_INFO_LIMIT("  wanted_features: 0x%llx", netdev->wanted_features);
	NOA_MD_INFO_LIMIT("  vlan_features: 0x%llx", netdev->vlan_features);
	NOA_MD_INFO_LIMIT("  group: %d", netdev->group);
	NOA_MD_INFO_LIMIT("  promiscuity: %d", netdev->promiscuity);
	NOA_MD_INFO_LIMIT("  allmulti: %d", netdev->allmulti);
	NOA_MD_INFO_LIMIT("  tx_queue_len: %u", netdev->tx_queue_len);
	NOA_MD_INFO_LIMIT("  num_tx_queues: %d", netdev->num_tx_queues);
	NOA_MD_INFO_LIMIT("  real_num_tx_queues: %d", netdev->real_num_tx_queues);
	NOA_MD_INFO_LIMIT("  num_rx_queues: %d", netdev->num_rx_queues);
	NOA_MD_INFO_LIMIT("  real_num_rx_queues: %d", netdev->real_num_rx_queues);
	NOA_MD_INFO_LIMIT("  qdisc: %p", netdev->qdisc);
	NOA_MD_INFO_LIMIT("  reg_state: %d", netdev->reg_state);

	NOA_MD_INFO_LIMIT("  is_up: %s", (netdev->flags & IFF_UP) ? "yes" : "no");
	NOA_MD_INFO_LIMIT(
		"  is_running: %s", (netdev->flags & IFF_RUNNING) ? "yes" : "no");

	NOA_MD_INFO_LIMIT("  tx_packets: %lu", netdev->stats.tx_packets);
	NOA_MD_INFO_LIMIT("  tx_bytes: %lu", netdev->stats.tx_bytes);
	NOA_MD_INFO_LIMIT("  rx_packets: %lu", netdev->stats.rx_packets);
	NOA_MD_INFO_LIMIT("  rx_bytes: %lu", netdev->stats.rx_bytes);

	NOA_MD_INFO_LIMIT("  tx_errors: %lu", netdev->stats.tx_errors);
	NOA_MD_INFO_LIMIT("  rx_errors: %lu", netdev->stats.rx_errors);
	NOA_MD_INFO_LIMIT("  tx_dropped: %lu", netdev->stats.tx_dropped);
	NOA_MD_INFO_LIMIT("  rx_dropped: %lu", netdev->stats.rx_dropped);

	NOA_MD_INFO_LIMIT("  base_addr: 0x%lx", netdev->base_addr);
	NOA_MD_INFO_LIMIT("  irq: %d", netdev->irq);
	NOA_MD_INFO_LIMIT("  watchdog_timeo: %d", netdev->watchdog_timeo);
	NOA_MD_INFO_LIMIT(
		"  needs_free_netdev: %s", netdev->needs_free_netdev ? "yes" : "no");
	NOA_MD_INFO_LIMIT("  perm_addr: %pM", netdev->perm_addr);
	return;
}

int noa_md_get_ndev_info(
		struct net_device *netdev, char* strbuff, ssize_t buf_size)
{
	int count = 0;

	if (!netdev || !strbuff || !buf_size) {
		NOA_MD_ERROR("netdev or strbuff is null");
		return count;
	}

	count += snprintf(strbuff + count, buf_size - count, "netdev info:\n");
	count += snprintf(strbuff + count, buf_size - count,
		"  netdev: 0x%lx\n", (unsigned long)netdev);
	count += snprintf(strbuff + count, buf_size - count,
		"  ifindex: %d\n", netdev->ifindex);
	count += snprintf(strbuff + count, buf_size - count,
		"  name: %s\n", netdev->name);
	count += snprintf(strbuff + count, buf_size - count,
		"  type: %d\n", netdev->type);
	count += snprintf(strbuff + count, buf_size - count,
		"  mtu: %d\n", netdev->mtu);
	count += snprintf(strbuff + count, buf_size - count,
		"  hard_header_len: %d\n", netdev->hard_header_len);
	count += snprintf(strbuff + count, buf_size - count,
		"  addr_len: %d\n", netdev->addr_len);
	count += snprintf(strbuff + count, buf_size - count,
		"  address: %pM\n", netdev->dev_addr);
	count += snprintf(strbuff + count, buf_size - count,
		"  broadcast: %pM\n", netdev->broadcast);
	count += snprintf(strbuff + count, buf_size - count,
		"  flags: 0x%x\n", netdev->flags);
	count += snprintf(strbuff + count, buf_size - count,
		"  gflags: 0x%hx\n", netdev->gflags);
	count += snprintf(strbuff + count, buf_size - count,
		"  priv_flags: 0x%llx\n", (u64)netdev->priv_flags);
	count += snprintf(strbuff + count, buf_size - count,
		"  features: 0x%llx\n", netdev->features);
	count += snprintf(strbuff + count, buf_size - count,
		"  hw_features: 0x%llx\n", netdev->hw_features);
	count += snprintf(strbuff + count, buf_size - count,
		"  wanted_features: 0x%llx\n", netdev->wanted_features);
	count += snprintf(strbuff + count, buf_size - count,
		"  vlan_features: 0x%llx\n", netdev->vlan_features);
	count += snprintf(strbuff + count, buf_size - count,
		"  group: %d\n", netdev->group);
	count += snprintf(strbuff + count, buf_size - count,
		"  promiscuity: %d\n", netdev->promiscuity);
	count += snprintf(strbuff + count, buf_size - count,
		"  allmulti: %d\n", netdev->allmulti);
	count += snprintf(strbuff + count, buf_size - count,
		"  tx_queue_len: %u\n", netdev->tx_queue_len);
	count += snprintf(strbuff + count, buf_size - count,
		"  num_tx_queues: %d\n", netdev->num_tx_queues);
	count += snprintf(strbuff + count, buf_size - count,
		"  real_num_tx_queues: %d\n", netdev->real_num_tx_queues);
	count += snprintf(strbuff + count, buf_size - count,
		"  num_rx_queues: %d\n", netdev->num_rx_queues);
	count += snprintf(strbuff + count, buf_size - count,
		"  real_num_rx_queues: %d\n", netdev->real_num_rx_queues);
	count += snprintf(strbuff + count, buf_size - count,
		"  qdisc: %p\n", netdev->qdisc);
	count += snprintf(strbuff + count, buf_size - count,
		"  reg_state: %d\n", netdev->reg_state);
	count += snprintf(strbuff + count, buf_size - count,
		"  is_up: %s\n", (netdev->flags & IFF_UP) ? "yes" : "no");
	count += snprintf(strbuff + count, buf_size - count,
		"  is_running: %s\n", (netdev->flags & IFF_RUNNING) ? "yes" : "no");
	count += snprintf(strbuff + count, buf_size - count,
		"  tx_packets: %lu\n", netdev->stats.tx_packets);
	count += snprintf(strbuff + count, buf_size - count,
		"  tx_bytes: %lu\n", netdev->stats.tx_bytes);
	count += snprintf(strbuff + count, buf_size - count,
		"  rx_packets: %lu\n", netdev->stats.rx_packets);
	count += snprintf(strbuff + count, buf_size - count,
		"  rx_bytes: %lu\n", netdev->stats.rx_bytes);
	count += snprintf(strbuff + count, buf_size - count,
		"  tx_errors: %lu\n", netdev->stats.tx_errors);
	count += snprintf(strbuff + count, buf_size - count,
		"  rx_errors: %lu\n", netdev->stats.rx_errors);
	count += snprintf(strbuff + count, buf_size - count,
		"  tx_dropped: %lu\n", netdev->stats.tx_dropped);
	count += snprintf(strbuff + count, buf_size - count,
		"  rx_dropped: %lu\n", netdev->stats.rx_dropped);
	count += snprintf(strbuff + count, buf_size - count,
		"  base_addr: 0x%lx\n", (unsigned long)netdev->base_addr);
	count += snprintf(strbuff + count, buf_size - count,
		"  irq: %d\n", netdev->irq);
	count += snprintf(strbuff + count, buf_size - count,
		"  watchdog_timeo: %d\n", netdev->watchdog_timeo);
	count += snprintf(strbuff + count, buf_size - count,
		"  needs_free_netdev: %s\n", netdev->needs_free_netdev ? "yes" : "no");
	count += snprintf(strbuff + count, buf_size - count,
		"  perm_addr: %pM\n", netdev->perm_addr);

	return count;
}
