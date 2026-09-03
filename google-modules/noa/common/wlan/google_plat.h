/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2025 Google LLC.
 *
 */
#ifndef __GOOGLE_PLAT_H__
#define __GOOGLE_PLAT_H__

#include "google_plat_internal.h"

extern struct platform_bus_ops *plat_ops;
extern struct platform_bus_ops google_bus_ops;
extern void noa_wlan_client_obj_register(void *dpa, void *vendor, void *bus, void *dev,
					 void **cur_ops);
extern void noa_wlan_client_obj_unregister(void);

/**
 * platform_bus_init - This function is used for platform specific bus initial
 *  handle that should be located before platform_bus_start.
 *
 * @pdev: linux bus data struct such as pci_dev, platform_dev, usb_dev, ...etc
 * @priv: vendor specific bus struct that require to use for bus initialized
 * @vendor_ops: vendor specific bus operations for bypass DPA mode.
 * @return: error code
 */
static inline int platform_bus_init(void *pdev, void *priv, struct platform_bus_ops *vendor_ops)
{
	/* Register noa wlan object to noa wlan driver. */
	noa_wlan_client_obj_register((void *)&google_bus_ops, (void *)vendor_ops, priv, pdev,
				     (void **)&plat_ops);

	if (plat_ops && plat_ops->init)
		return plat_ops->init(pdev, priv);
	return 0;
}

/**
 * platform_bus_exit - This function is used for platform specific bus
 * de-init handle that should be located after platform_bus_stop.
 *
 * @priv: vendor specific bus structure
 */
static inline void platform_bus_exit(void *priv)
{
	if (plat_ops && plat_ops->exit)
		plat_ops->exit(priv);

	noa_wlan_client_obj_unregister();
}

/**
 * platform_bus_start - This function is used for platform specific bus starting
 * handle that should be located after platform_bus_init.
 *
 * @priv: vendor specific bus structure
 * @return error code
 */
static inline int platform_bus_start(void *priv)
{
	if (plat_ops && plat_ops->start)
		return plat_ops->start(priv);
	return 0;
}

/**
 * platform_bus_stop - This function is used for platform specific bus stoping
 * handle that should be located before platform_bus_exit.
 *
 * @priv: vendor specific bus structure
 */
static inline void platform_bus_stop(void *priv)
{
	if (plat_ops && plat_ops->stop)
		plat_ops->stop(priv);
}

/**
 * platform_bus_request_irq - This function is used for bus requesting irq,
 * all irq request handle should be included in this function and this
 * function should be located before platform_bus_start.
 *
 * @priv: vendor specific bus structure
 * @return: error code
 */
static inline int platform_bus_request_irq(void *priv)
{
	if (plat_ops && plat_ops->request_irq)
		return plat_ops->request_irq(priv);
	return 0;
}

/**
 * platform_bus_rx - This function is used for vendor specific bus receiving
 * data packets handle
 *
 * @priv: vendor specific bus structure
 * @cnt: number of rx descriptors are consumed
 * @return error code
 */
static inline bool platform_bus_rx(void *priv, u32 *cnt)
{
	if (plat_ops && plat_ops->rx)
		return plat_ops->rx(priv, cnt);
	return false;
}

/**
 * platform_bus_rx_replenish - This function is used for vendor specific rx
 * buffer replenishing mechanism for the rx receiving data.
 *
 * @priv: vendor specific bus structure
 * @cnt: number of rx buffer replenishing to the bus
 * @use_rsv_pktid: use_rsv_pktid
 * @return: error code or number of replenish
 */
static inline int platform_bus_rx_replenish(void *priv, u32 cnt, bool use_rsv_pktid)
{
	if (plat_ops && plat_ops->rx_replenish)
		return plat_ops->rx_replenish(priv, cnt, use_rsv_pktid);
	return -ENODEV;
}

/**
 * platform_bus_tx - This function is used for vendor specific bus
 * transmitting data packets handle.
 *
 * @priv: vendor specific bus structure
 * @pkt: tx packet buffer
 * @ifidx: interface index for tx packet that would like to send out
 * @return error code
 */
static inline int platform_bus_tx(void *priv, void *pkt, u32 ifidx)
{
	if (plat_ops && plat_ops->tx)
		return plat_ops->tx(priv, pkt, ifidx);
	return -ENODEV;
}

/**
 * platform_bus_tx_cpl - This function is used for vendor specific
 * tx packet recycle mechanism.
 *
 * @priv: vendor specific bus structure
 * @cnt: number of rx descriptors are consumed
 * @return error code
 */
static inline bool platform_bus_tx_cpl(void *priv, u32 *cnt)
{
	if (plat_ops && plat_ops->tx_cpl)
		return plat_ops->tx_cpl(priv, cnt);
	return false;
}

/**
 * platform_bus_tx_queue_active - This function is used to
 * active/deactive txqueue
 *
 * @priv: vendor specific bus structure
 * @flowid: number of rx descriptors are consumed
 * @enable: 0 disable, 1 enable txqueue
 * @return error code
 */
static inline int platform_bus_tx_queue_active(void *priv, u16 flowid, bool enable)
{
	if (plat_ops && plat_ops->tx_queue_active)
		return plat_ops->tx_queue_active(priv, flowid, enable);
	return -ENODEV;
}

/**
 * platform_bus_sta_active - This function is used to
 * active/deactive device and provide the station information
 *
 * @priv: vendor specific bus structure
 * @info: station information
 * @enable: 0 disable, 1 enable device
 * @return error code
 */
static inline int platform_bus_sta_active(void *priv, struct sta_info *info, bool enable)
{
	if (plat_ops && plat_ops->sta_active)
		return plat_ops->sta_active(priv, info, enable);
	return -ENODEV;
}

static inline bool platform_bus_check_suspend(void)
{
	if (plat_ops && plat_ops->check_suspend)
		return plat_ops->check_suspend();
	return true;
}

static inline void platform_bus_notify_resume(void)
{
	if (plat_ops && plat_ops->check_suspend)
		plat_ops->notify_resume();
}

/**
 * platform_bus_ssr_dump - This function is used to fill in the sscd_segment
 * element for creating a solen coredump file when WiFi device occurs crash.
 *
 * @seg: segment structure to capture the memory address and value
 */
static inline void platform_bus_ssr_dump(void *seg)
{
	if (plat_ops && plat_ops->ssr_dump)
		plat_ops->ssr_dump(seg);
}

/**
 * platform_bus_update_up2flow - This function is used to update the
 * up2flow table for the offload engine. The table is used to map the UP to
 * flow priority.
 *
 * @type: type of the table to update
 * @tbl: pointer to the table to update
 */
static inline void platform_bus_update_up2flow(const u8 type, const uint8_t *tbl)
{
	if (plat_ops && plat_ops->update_up2flow)
		plat_ops->update_up2flow(type, tbl);
}

/**
 * platform_bus_update_flowid_lkup_entry - This function is used to update
 * the flowid lookup entry in the offload engine. The entry is used to map
 * the flowid to the flow priority and other parameters.
 *
 * @param entry: pointer to the flowid entry to update
 */
static inline void platform_bus_update_flowid_lkup_entry(struct flow_id_entry_update *entry)
{
	if (plat_ops && plat_ops->update_flowid_lkup_entry)
		plat_ops->update_flowid_lkup_entry(entry);
}

/**
 * platform_bus_sync_pci_link_state - This function is used to keep both the offload
 * system's link state cache and the upstream link state cache in sync. The state is
 * the link state that needs to be saved in the shared memory.
 *
 * @param state: value to be saved
 */
static inline void platform_bus_sync_pci_link_state(int32_t state, bool is_to_shm)
{
	if (plat_ops && plat_ops->sync_pci_link_state)
		plat_ops->sync_pci_link_state(state, is_to_shm);
}

static inline void platform_bus_notify_station_state(uint8_t state, void *dhd_pub, int iflist_idx)
{
	if (plat_ops && plat_ops->notify_station_state)
		plat_ops->notify_station_state(state, dhd_pub, iflist_idx);
}
#endif /*__GOOGLE_PLAT_H__*/
