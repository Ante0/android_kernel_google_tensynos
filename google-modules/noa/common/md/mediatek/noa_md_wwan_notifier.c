/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2025, Google LLC.
 */

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/wwan.h>  /* For wwan_netdev_drvpriv */

#include "common/md/mediatek/noa_md_dpa_doorbell.h"
#include "common/md/mediatek/noa_md_shmem_layout.h"

#include "noa_md.h"
#include "noa_md_trace.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_shmem_sync.h"
#include "noa_md_wwan_notifier.h"
#include "t900/noa_md_mtk_priv.h"

#define NOA_WWAN_IF_PREFIX "wwan"

/**
 * noa_md_wwan_notifier_netdev_event_to_str() - Converts a netdev event code to
 * a human-readable string for logging.
 * @event: The netdevice event code (e.g., NETDEV_UP, NETDEV_REGISTER).
 *
 * Return: A constant string representing the event.
 */
static const char *noa_md_wwan_notifier_netdev_event_to_str(
	unsigned long event)
{
	switch (event) {
	case NETDEV_UP:
		return "NETDEV_UP";
	case NETDEV_DOWN:
		return "NETDEV_DOWN";
	case NETDEV_REBOOT:
		return "NETDEV_REBOOT";
	case NETDEV_CHANGE:
		return "NETDEV_CHANGE";
	case NETDEV_REGISTER:
		return "NETDEV_REGISTER";
	case NETDEV_UNREGISTER:
		return "NETDEV_UNREGISTER";
	case NETDEV_CHANGEMTU:
		return "NETDEV_CHANGEMTU";
	case NETDEV_CHANGEADDR:
		return "NETDEV_CHANGEADDR";
	case NETDEV_PRE_CHANGEADDR:
		return "NETDEV_PRE_CHANGEADDR";
	case NETDEV_GOING_DOWN:
		return "NETDEV_GOING_DOWN";
	case NETDEV_CHANGENAME:
		return "NETDEV_CHANGENAME";
	case NETDEV_FEAT_CHANGE:
		return "NETDEV_FEAT_CHANGE";
	case NETDEV_BONDING_FAILOVER:
		return "NETDEV_BONDING_FAILOVER";
	case NETDEV_PRE_UP:
		return "NETDEV_PRE_UP";
	case NETDEV_PRE_TYPE_CHANGE:
		return "NETDEV_PRE_TYPE_CHANGE";
	case NETDEV_POST_TYPE_CHANGE:
		return "NETDEV_POST_TYPE_CHANGE";
	case NETDEV_POST_INIT:
		return "NETDEV_POST_INIT";
	case NETDEV_PRE_UNINIT:
		return "NETDEV_PRE_UNINIT";
	case NETDEV_RELEASE:
		return "NETDEV_RELEASE";
	case NETDEV_NOTIFY_PEERS:
		return "NETDEV_NOTIFY_PEERS";
	case NETDEV_JOIN:
		return "NETDEV_JOIN";
	case NETDEV_CHANGEUPPER:
		return "NETDEV_CHANGEUPPER";
	case NETDEV_RESEND_IGMP:
		return "NETDEV_RESEND_IGMP";
	case NETDEV_PRECHANGEMTU:
		return "NETDEV_PRECHANGEMTU";
	case NETDEV_CHANGEINFODATA:
		return "NETDEV_CHANGEINFODATA";
	case NETDEV_BONDING_INFO:
		return "NETDEV_BONDING_INFO";
	case NETDEV_PRECHANGEUPPER:
		return "NETDEV_PRECHANGEUPPER";
	case NETDEV_CHANGELOWERSTATE:
		return "NETDEV_CHANGELOWERSTATE";
	case NETDEV_UDP_TUNNEL_PUSH_INFO:
		return "NETDEV_UDP_TUNNEL_PUSH_INFO";
	case NETDEV_UDP_TUNNEL_DROP_INFO:
		return "NETDEV_UDP_TUNNEL_DROP_INFO";
	case NETDEV_CHANGE_TX_QUEUE_LEN:
		return "NETDEV_CHANGE_TX_QUEUE_LEN";
	case NETDEV_CVLAN_FILTER_PUSH_INFO:
		return "NETDEV_CVLAN_FILTER_PUSH_INFO";
	case NETDEV_CVLAN_FILTER_DROP_INFO:
		return "NETDEV_CVLAN_FILTER_DROP_INFO";
	case NETDEV_SVLAN_FILTER_PUSH_INFO:
		return "NETDEV_SVLAN_FILTER_PUSH_INFO";
	case NETDEV_SVLAN_FILTER_DROP_INFO:
		return "NETDEV_SVLAN_FILTER_DROP_INFO";
	case NETDEV_OFFLOAD_XSTATS_ENABLE:
		return "NETDEV_OFFLOAD_XSTATS_ENABLE";
	case NETDEV_OFFLOAD_XSTATS_DISABLE:
		return "NETDEV_OFFLOAD_XSTATS_DISABLE";
	case NETDEV_OFFLOAD_XSTATS_REPORT_USED:
		return "NETDEV_OFFLOAD_XSTATS_REPORT_USED";
	case NETDEV_OFFLOAD_XSTATS_REPORT_DELTA:
		return "NETDEV_OFFLOAD_XSTATS_REPORT_DELTA";
	case NETDEV_XDP_FEAT_CHANGE:
		return "NETDEV_XDP_FEAT_CHANGE";
	default:
		return "UNKNOWN_NETDEV_EVENT";
	}
}

/**
 * noa_md_wwan_notifier_ifindex_table_update() - Updates a single entry in the shared
 * ifindex table and notifies the NCP.
 *
 * @p_md_dev:    Pointer to the main NOA modem device struct.
 * @intf_id:     The interface ID, used as the index in the table.
 * @ifindex:     The new interface index value to set.
 * @is_register: True if the device is being registered, false if unregistered.
 *
 * This function updates a specific entry in the shared memory table, ensures
 * memory coherency for the DMA device, and then rings a doorbell to notify the
 * NCP of the update.
 */
static void noa_md_wwan_notifier_ifindex_table_update(
	struct noa_md_dev *p_md_dev,
	unsigned int intf_id,
	int ifindex,
	bool is_register)
{
	struct noa_md_shmem_layout *shmem;
	struct noa_md_shmem_sync_handle *shmem_sync;
	u32 *wwan_ifindex_table = NULL;
	int ret;

	CHECK_PTR_OR_RETURN(p_md_dev);
	CHECK_PTR_OR_RETURN(p_md_dev->shmem_sync);
	CHECK_PTR_OR_RETURN(p_md_dev->shmem_handle.va_base);

	shmem_sync = p_md_dev->shmem_sync;
	shmem = (struct noa_md_shmem_layout *)p_md_dev->shmem_handle.va_base;
	wwan_ifindex_table = shmem->wwan_ifindex_table;

	/* Boundary check for the interface ID */
	if (intf_id >= MAX_WWAN_IFINDEX_TABLE_SIZE) {
		NOA_MD_ERROR(
			"intf_id %u is out of bounds for wwan_ifindex_table", intf_id);
		return;
	}

	spin_lock_bh(&p_md_dev->netdev_update_lock);

	if (!p_md_dev->wwan_notifier_ready) {
		NOA_MD_INFO("WWAN notifier not ready, skipping update for intf_id %u ifindex %u",
			    intf_id, ifindex);
		spin_unlock_bh(&p_md_dev->netdev_update_lock);
		return;
	}

	if (is_register) {
		wwan_ifindex_table[intf_id] = (ifindex <= 0) ? 0 : ifindex;
		NOA_MD_INFO("Registering ifindex %d at table index %u",
			    wwan_ifindex_table[intf_id], intf_id);
	} else {
		NOA_MD_INFO("Unregistering ifindex at table index %u", intf_id);
		wwan_ifindex_table[intf_id] = 0;
	}

	spin_unlock_bh(&p_md_dev->netdev_update_lock);

	/* Write Memory Barrier */
	dma_wmb();

	/* Notify NCP that the shared memory has been updated. */
	ret = noa_md_shmem_sync_send_data_cmd(
		shmem_sync,
		NOA_MD_SHMEM_DATA_CMD_IFINDEX_TABLE_UPDATE,
		NULL, NULL  /* This command does not require payload */
	);

	if (ret) {
		NOA_MD_ERROR("Failed to send IFINDEX_TABLE_UPDATE cmd, ret=%d. ", ret);
	}
}

int noa_md_wwan_notifier_sync_on_ready(void)
{
	struct noa_md_dev *p_md_dev = &md_dev;
	struct noa_md_shmem_layout *shmem;
	u32 *wwan_ifindex_table = NULL;
	int ret;
	bool need_sync = false;

	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(p_md_dev->shmem_sync, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(p_md_dev->shmem_handle.va_base, -EINVAL);

	shmem = (struct noa_md_shmem_layout *)p_md_dev->shmem_handle.va_base;
	wwan_ifindex_table = shmem->wwan_ifindex_table;

	spin_lock_bh(&p_md_dev->netdev_update_lock);

	/* Clear table first to ensure a clean state */
	memset(wwan_ifindex_table, 0, sizeof(shmem->wwan_ifindex_table));

	for (int i = 0; i < MTK_NETDEV_MAX; i++) {
		struct mtk_wwan_instance *inst = p_md_dev->wwan_inst[i];
		struct net_device *netdev = p_md_dev->netdevs[i];

		if (inst && netdev && inst->intf_id < MAX_WWAN_IFINDEX_TABLE_SIZE) {
			int ifindex = netdev->ifindex;
			if (ifindex > 0) {
				wwan_ifindex_table[inst->intf_id] = ifindex;
				need_sync = true;
				NOA_MD_INFO("Syncing intf_id %u ifindex %d",
					    inst->intf_id, ifindex);
			}
		}
	}

	p_md_dev->wwan_notifier_ready = true;

	/* Write Memory Barrier */
	dma_wmb();

	spin_unlock_bh(&p_md_dev->netdev_update_lock);

	if (need_sync) {
		ret = noa_md_shmem_sync_send_data_cmd(
			p_md_dev->shmem_sync,
			NOA_MD_SHMEM_DATA_CMD_IFINDEX_TABLE_UPDATE,
			NULL, NULL
		);
		if (ret) {
			NOA_MD_ERROR("Failed to send sync ifindex table, ret: %d", ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_notifier_sync_on_ready);

void noa_md_wwan_notifier_reset(void)
{
	struct noa_md_dev *p_md_dev = &md_dev;

	spin_lock_bh(&p_md_dev->netdev_update_lock);
	p_md_dev->wwan_notifier_ready = false;
	spin_unlock_bh(&p_md_dev->netdev_update_lock);

	NOA_MD_INFO("WWAN notifier reset");
}
EXPORT_SYMBOL_GPL(noa_md_wwan_notifier_reset);

/**
 * noa_md_wwan_notifier_netdev_event() - Handles netdevice events.
 * @nb: Pointer to the notifier block.
 * @event: The event type (e.g., NETDEV_REGISTER).
 * @ptr: Pointer to the netdevice notifier info.
 *
 * This is a core callback, so its implementation must be efficient and
 * reliable. It processes only the WWAN interface events and performs
 * the necessary actions.
 *
 * Context: Process context. Can sleep.
 * Return: Always returns NOTIFY_DONE to allow other notifiers to process the
 * event.
 */
static int noa_md_wwan_notifier_netdev_event(struct notifier_block *nb,
				    unsigned long event, void *ptr)
{
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);
	const struct mtk_wwan_instance *priv;

	if (strncmp(net_dev->name, NOA_WWAN_IF_PREFIX,
		    strlen(NOA_WWAN_IF_PREFIX)) != 0) {
		return NOTIFY_DONE;
	}

	priv = wwan_netdev_drvpriv(net_dev);
	if (!priv) {
		NOA_MD_INFO("netdevice '%s' has no private data, skipping",
			    net_dev->name);
		return NOTIFY_DONE;
	}

	switch (event) {
	case NETDEV_REGISTER:
		NOA_MD_INFO("NETDEV_REGISTER for %s, intf_id: %d, ifindex: %d",
			net_dev->name, priv->intf_id, net_dev->ifindex);
		noa_md_wwan_notifier_ifindex_table_update(&md_dev,
			priv->intf_id, net_dev->ifindex, true);
		break;

	case NETDEV_UNREGISTER:
		NOA_MD_INFO("NETDEV_UNREGISTER for %s, intf_id: %d, ifindex: %d",
			net_dev->name, priv->intf_id, net_dev->ifindex);
		noa_md_wwan_notifier_ifindex_table_update(&md_dev,
			priv->intf_id, 0, false);
		break;

	default:
		NOA_MD_DATA("%s(%d) for %s, intf_id: %d, ifindex: %d",
			noa_md_wwan_notifier_netdev_event_to_str(event), event,
			net_dev->name, priv->intf_id, net_dev->ifindex);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block noa_md_wwan_netdev_nb = {
	.notifier_call = noa_md_wwan_notifier_netdev_event,
};

int noa_md_wwan_notifier_init(void)
{
	int ret;

	ret = register_netdevice_notifier(&noa_md_wwan_netdev_nb);
	if (ret) {
		NOA_MD_ERROR("Failed to register netdevice notifier: %d", ret);
	} else {
		NOA_MD_INFO("WWAN netdevice notifier registered successfully");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_notifier_init);

void noa_md_wwan_notifier_exit(void)
{
	unregister_netdevice_notifier(&noa_md_wwan_netdev_nb);
	NOA_MD_INFO("WWAN netdevice notifier unregistered");
}
EXPORT_SYMBOL_GPL(noa_md_wwan_notifier_exit);
