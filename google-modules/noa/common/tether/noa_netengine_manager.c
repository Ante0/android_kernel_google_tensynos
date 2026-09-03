// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA NET engine
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kimi Luinata <kimiluinata@google.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/device/driver.h>
#include <net/addrconf.h>
#include <net/ipv6.h>

#include "noa_tethering_manager.h"
#include "net/nep_table_manager.h"

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include <soc/google/google_dpa.h>
#include <soc/google/google_dpa_ctrl.h>
#include <soc/google/google_dpa_ring_service_proxy.h>
#include <net/nep_cmd_rpc_service/noa_nep_cmd_rpc.h>
#include <net/nep_cmd_rpc_service/nep_event_rpc_client.h>
#include <net/public/net/ra_proxy.h>
#include <net/public/net/nep_map_table.h>
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */

/*
** Function Prototypes
*/
static int __init noa_netengine_driver_init(void);
static void __exit noa_netengine_driver_exit(void);

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)

struct noa_netengine_manager {
	struct device *dev;
	struct google_dpa *dpa;
	struct dpa_client *dpa_client;
};

int32_t nep_callback_handler(uint32_t id, const void *msg, size_t msg_len);

static int netengine_manager_send_ra_to_kernel(int ifindex, uint8_t *packet_data, int packet_len);
static void nep_rpc_init(void);
static void tethering_da_to_va(struct shared_address_info shared_info);

struct noa_netengine_manager *g_netengine_mgmt;

/*
** Callback handler for NEP RPC service
*/
int32_t nep_callback_handler(uint32_t id, const void *msg, size_t msg_len) {
	int ret = 0;
	pr_info("%s: receive callback from NEP id: %d.\n", __func__, id);

	switch(id) {
		case CMD_CALLBACK_EXTEND_TIMEOUT: {
			const uint64_t *base_time = (const uint64_t *) msg;
			entry_update_timeout(*base_time);
			break;
		}
		case CMD_CALLBACK_SEND_RA_TO_KERNEL: {
			rpc_send_ra_t *rpc_send_ra = (rpc_send_ra_t *) msg;
			pr_info("%s: send RA packet to kernel, ifindex = %d, length = %u\n",
				__func__, rpc_send_ra->ifindex, rpc_send_ra->packet_len);
			netengine_manager_send_ra_to_kernel(rpc_send_ra->ifindex,
							    rpc_send_ra->packet_data,
							    rpc_send_ra->packet_len);
			break;
		}
		default: {
			pr_info("%s: unsupported NEP callback Id: %d", __func__, id);
			ret = -1;
			break;
		}
	}

	return ret;
}

static void nep_rpc_init(void) {
	// Initialize NEP RPC client
	noa_nep_cmd_rpc_init();

	nep_event_rpc_client_register_event_callback(&nep_callback_handler);
	nep_event_rpc_client_open();
}

static int netengine_manager_send_ra_to_kernel(int ifindex, uint8_t *packet_data, int packet_len)
{
	struct sk_buff *skb;
	struct net_device *dev;
	struct inet6_dev *idev;
	int old_ra_honor_pio_life;
	int old_accept_ra_defrtr;
	int new_ra_honor_pio_life = 1;
	int new_accept_ra_defrtr = 1;

	// Get the netdevice
	dev = dev_get_by_index(&init_net, ifindex);
	if (!dev) {
		return -1;
	}

	// There are two issues after the kernel receives an RA with zero lifetime:
	// 1. The IPv6 address's preferred lifetime will count down from two hours instead of being
	//    immediately set to 0.
	// 2. The IpClient will receive a "default gateway removed" event before the "IPv6 addresses
	//    removed" event. Currently, it turns off accept_ra_defrtr on the interface and is
	//    unable to recover from receiving upcoming normal RAs.
	// Here, we'll temporarily change the configuration of ra_honor_pio_life and
	// accept_ra_defrtr to address these issues. We will then revert to the original settings
	// immediately after sending the RA to the kernel. This is a workaround solution before the
	// fix aosp/2674896 is accepted.
	idev = __in6_dev_get(dev);
	if (idev) {
		old_ra_honor_pio_life = idev->cnf.ra_honor_pio_life;
		if (old_ra_honor_pio_life != new_ra_honor_pio_life) {
			idev->cnf.ra_honor_pio_life = new_ra_honor_pio_life;
		}
		old_accept_ra_defrtr = idev->cnf.accept_ra_defrtr;
		if (old_accept_ra_defrtr != new_accept_ra_defrtr) {
			idev->cnf.accept_ra_defrtr = new_accept_ra_defrtr;
		}
	}

	skb = alloc_skb(packet_len, GFP_KERNEL);
	if (!skb) {
		dev_put(dev);
		return -1;
	}

	// Copy the packet data into the sk_buff
	skb_put_data(skb, packet_data, packet_len);

	// Set up sk_buff metadata
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->pkt_type = PACKET_HOST;
	skb->ip_summed = CHECKSUM_NONE;

	// Push the skb to the network stack. Use netif_receive_skb() instead
	// of netif_rx() to ensure the skb is processed synchronously.
	local_bh_disable();
	netif_receive_skb(skb);
	local_bh_enable();

	// Restore the original configuration of ra_honor_pio_life and
	// accept_ra_defrtr after sending RA to kernel.
	if (idev) {
		if (old_ra_honor_pio_life != new_ra_honor_pio_life) {
			idev->cnf.ra_honor_pio_life = old_ra_honor_pio_life;
		}
		if (old_accept_ra_defrtr != new_accept_ra_defrtr) {
			idev->cnf.accept_ra_defrtr = old_accept_ra_defrtr;
		}
	}

	dev_put(dev);
	return 0;
}

/*
** Address translation from device address (da) to virtual address (va).
*/
static void tethering_da_to_va(struct shared_address_info shared_info) {
	void __iomem *vaddr = NULL;
	bool is_iomem;

	vaddr = google_dpa_da_to_va(g_netengine_mgmt->dpa, GOOGLE_DPA_MCU_NEP,
		shared_info.base_addr, shared_info.memory_size, &is_iomem);

	if (IS_ERR_OR_NULL(vaddr)) {
		panic("%s: Failed to get dpa tethering shared info, ret %ld\n",
			__func__, PTR_ERR(vaddr));
		return;
	}

	uint64_t diff = (uintptr_t) vaddr - shared_info.base_addr;
	register_tethering_shared_info((uintptr_t) vaddr, diff, shared_info.upstream4_buckets_addr_offset, shared_info.downstream4_buckets_addr_offset);
}

static void netengine_manager_on_data_path_change(enum dpa_data_path desired_data_path,
						  enum dpa_action action, void *context)
{
	if (action != NOA_ACTION_SERVICE_PRE_SWITCH) {
		return;
	}

	if (desired_data_path == NOA_DATA_PATH_DIRECT) {
		google_dpa_ring_service_rpc_event_netengine_deactivate();
	} else if (desired_data_path == NOA_DATA_PATH_OFFLOAD) {
		google_dpa_ring_service_rpc_event_netengine_activate();
	}
}

static void netengine_manager_state_callback(enum dpa_state state, void *context)
{
	if (state == NOA_STATE_READY) {
		if (!g_netengine_mgmt) {
			g_netengine_mgmt = (struct noa_netengine_manager *)context;
			nep_rpc_init();

			// Request nep table base address
			struct shared_address_info shared_info;
			noa_nep_cmd_request_send(CMD_REQUEST_BASE_ADDR, NULL, 0, &shared_info.base_addr, NULL);
			tethering_da_to_va(shared_info);
		}
	}

	return;
}

static struct dpa_callbacks netengine_callbacks = {
	.on_state_changed = netengine_manager_state_callback,
	.on_data_path_changed = netengine_manager_on_data_path_change,
};

static void release_netengine_manager(struct noa_netengine_manager *netengine_mgmt)
{
	if (!netengine_mgmt) {
		return;
	}
	if (netengine_mgmt->dpa_client) {
		google_dpa_ctrl_unregister(netengine_mgmt->dpa_client);
	}
	if (netengine_mgmt->dpa) {
		google_dpa_put(netengine_mgmt->dpa);
	}
}

static int noa_netengine_manager_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct noa_netengine_manager *netengine_mgmt;

	// allocate memory
	netengine_mgmt = devm_kzalloc(dev, sizeof(*netengine_mgmt), GFP_KERNEL);
	if (!netengine_mgmt) {
		ret = -ENOMEM;
		goto out;
	}

	platform_set_drvdata(pdev, netengine_mgmt);
	netengine_mgmt->dev = dev;

	// Get dpa handle
	netengine_mgmt->dpa = google_dpa_get(dev);
	if (IS_ERR(netengine_mgmt->dpa)) {
		ret = PTR_ERR(netengine_mgmt->dpa);
		netengine_mgmt->dpa = NULL;
		dev_err_probe(dev, ret, "Failed to get dpa structure.");
		goto out;
	}

	// Register to the dpa memory
	netengine_mgmt->dpa_client =
		google_dpa_ctrl_register("netengine_manager", &netengine_callbacks, netengine_mgmt);
	if (!netengine_mgmt->dpa_client) {
		ret = -ENOMEM;
		dev_err_probe(dev, ret, "Failed to register netengine manager client on dpa");
		goto out;
	}

	ret = 0;
out:
	if (ret) {
		release_netengine_manager(netengine_mgmt);
	}
	return ret;
}

static void noa_netengine_manager_remove(struct platform_device *pdev)
{
	struct noa_netengine_manager *netengine_mgmt = platform_get_drvdata(pdev);
	release_netengine_manager(netengine_mgmt);
}

static const struct of_device_id noa_netengine_manager_of_match[] = {
	{
		.compatible = "google,dpa-netengine-manager",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, noa_netengine_manager_of_match);

static struct platform_driver noa_netengine_manager_driver = {
	.probe = noa_netengine_manager_probe,
	.remove = noa_netengine_manager_remove,
	.driver = {
		.name = "dpa_netengine_manager",
		.owner = THIS_MODULE,
		.of_match_table = noa_netengine_manager_of_match,
    },
};

#endif /* CONFIG_NOA_FULLSOC_SUPPORT */

/*
** Module Init function
*/
static int __init noa_netengine_driver_init(void)
{
	int ret;
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
	ret = platform_driver_register(&noa_netengine_manager_driver);
	if (ret) {
		pr_err("Failed to init noa netengine driver\n");
		goto out;
	}
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
	ret = init_noa_tethering_manager();
	if (ret) {
		pr_err("Failed to init noa tethering manager\n");
		goto out;
	}

	pr_info("noa_netengine_driver_init Done\n");
	ret = 0;
out:
	return ret;
}

/*
** Module exit function
*/
static void __exit noa_netengine_driver_exit(void)
{
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
	platform_driver_unregister(&noa_netengine_manager_driver);
#else /* CONFIG_NOA_FULLSOC_SUPPORT */
	deinit_noa_tethering_manager();
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
}

module_init(noa_netengine_driver_init);
module_exit(noa_netengine_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Chien <markchien@google.com>");
MODULE_DESCRIPTION("NOA NET Engine Driver");
