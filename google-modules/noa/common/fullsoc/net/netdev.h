/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM Network Device Definitions
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_NET_NETDEV_H__
#define __LVM_NET_NETDEV_H__

#include <linux/if.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <buffer/manager.h>
#include <net/platform.h>

struct lvm_platform_driver;
struct lvm_netdev;


/**
 * struct lvm_netdev_ops - LVM network device operations
 * @ndo_open: The function to open the network interface
 * @ndo_stop: The function to stop the network interface
 * @ndo_start_xmit: The function to start transmitting a packet
 */
struct lvm_netdev_ops {
	int		(*ndo_open)(struct lvm_netdev *dev);
	int		(*ndo_stop)(struct lvm_netdev *dev);
	netdev_tx_t	(*ndo_start_xmit)(struct lvm_buffer *buf,
					  struct lvm_netdev *dev);
};


/**
 * struct lvm_netdev_stats - LVM network device statistics
 * @rx_packets: Number of received packets
 * @tx_packets: Number of transmitted packets
 * @rx_bytes: Number of received bytes
 * @tx_bytes: Number of transmitted bytes
 * @rx_dropped: Number of dropped received packets
 * @tx_dropped: Number of dropped transmitted packets
 */
struct lvm_netdev_stats {
	u64		rx_packets;
	u64		tx_packets;
	u64		rx_bytes;
	u64		tx_bytes;
	u64		rx_dropped;
	u64		tx_dropped;
};


/**
 * struct lvm_netdev - LVM network device structure
 * @name: Name of the network device
 * @state: Network device state machine
 * @ifidx: Network interface index
 * @priv: Private data for the network device
 * @netdev_ops: Network device operations
 * @stats: Network device statistics
 * @stats_lock: Spinlock for protecting the network device statistics
 */
struct lvm_netdev {
	char				name[IFNAMSIZ];
	unsigned long			state;
	int				ifidx;
	void				*priv;
	const struct lvm_netdev_ops	*netdev_ops;
	struct lvm_netdev_stats		stats;
	spinlock_t			stats_lock;
};


int lvm_netdev_start_xmit(struct lvm_platform_driver *driver);
int lvm_netdev_netif_rx(struct lvm_manager *manager, struct lvm_buffer *buf);
int lvm_netdev_open(struct lvm_platform_driver *driver);
int lvm_netdev_stop(struct lvm_platform_driver *driver);
struct lvm_netdev *lvm_netdev_alloc(const char *name);
void lvm_netdev_free(struct lvm_netdev *netdev);

#endif  /* __LVM_NET_NETDEV_H__ */
