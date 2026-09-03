/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_wondertap.h
 *
 * WLAN Host Device Driver file for wondertap functionality.
 *
 */
#if !defined(WLAN_HDD_WONDERTAP_H)
#define WLAN_HDD_WONDERTAP_H

#include <qdf_event.h>
#include "wlan_mlme_public_struct.h"

#ifdef DRIVER_PASSTHRU_MODE
#include <qdf_wondertap.h>

#define WLAN_WONDERTAP_VDEV_OP_TIMEOUT_MS 10000

struct hdd_wondertap_tx_rate_cfg {
	enum phy_ch_width ch_width;
	uint32_t dot11_mode;
	uint8_t gi_val;
	uint8_t nss;
	uint8_t mcs;
};

#define WLAN_PASSTHRU_MAX_PEER 7
#define WLAN_PASSTHRU_MAX_NSS  2

/**
 * enum passthru_peer_status - passthru peer status
 * @PASSTHRU_PEER_SETUP_NOT_DONE: peer setup not done
 * @PASSTHRU_PEER_SETUP_IN_PROGRESS: peer setup in progress
 * @PASSTHRU_PEER_SETUP_SUCCESSFUL: peer setup is successful
 * @PASSTHRU_PEER_SETUP_FAILED: peer setup failed
 * @PASSTHRU_PEER_SETUP_MAX: peer setup status max macro
 */
enum passthru_peer_status {
	PASSTHRU_PEER_SETUP_NOT_DONE,
	PASSTHRU_PEER_SETUP_IN_PROGRESS,
	PASSTHRU_PEER_SETUP_SUCCESSFUL,
	PASSTHRU_PEER_SETUP_FAILED,
	PASSTHRU_PEER_SETUP_MAX,
};

/**
 * struct passthru_peer_tbl_entry - passthru peer table entry
 * @mac_addr: peer MAC address
 * @peer_status: peer setup status
 * @sta_info: station info stored on UPDATE, returned verbatim on QUERY
 * @caps_applied_in_new: caps were already sent to FW as part of NEW; every
 *  UPDATE for this peer is skipped while this is set, since FW does not
 *  support a second peer_assoc for the same peer. Cleared only on DEL.
 *  WONDER is expected to send caps in either NEW or UPDATE, never both.
 */
struct passthru_peer_tbl_entry {
	struct qdf_mac_addr          mac_addr;
	enum passthru_peer_status    peer_status;
	qdf_wondertap_station_info_t sta_info;
	bool                          caps_applied_in_new;
};

/**
 * struct hdd_wondertap_context - hdd wondertap context
 * @hdd_ctx: global hdd context
 * @wt_adapter: pointer to wondertap adapter
 * @wondertap_vdev_event: wondertap vdev event
 * @wondertap_wakelock: wondertap wakelock
 * @wondertap_rtpm_lock: wondertap rtpm lock
 * @is_frame_filter_set: is frame filter configured
 * @frame_filter: frame filter value
 * @magic: handle for external entity
 * @tx_rate_cfg: transmit rate configuration
 * @peer_tbl: passthru peer table
 * @num_peers: number of peers in the table
 * @is_peer_create_enabled: is peer created enabled
 */
struct hdd_wondertap_context {
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	qdf_event_t wondertap_vdev_event;
	qdf_wake_lock_t wondertap_wakelock;
	qdf_runtime_lock_t wondertap_rtpm_lock;
	bool is_frame_filter_set;
	uint8_t frame_filter;
	uint64_t magic;
	struct hdd_wondertap_tx_rate_cfg tx_rate_cfg;
	struct passthru_peer_tbl_entry peer_tbl[WLAN_PASSTHRU_MAX_PEER];
	uint8_t num_peers;
	bool is_peer_create_enabled;
};

struct hdd_wondertap_peer_setup {
	uint8_t  vdev_id;
	uint8_t  peer_addr[QDF_MAC_ADDR_SIZE];
	uint16_t peer_aid;
};

/**
 * wlan_hdd_wondertap_peer_del() - Delete a wondertap peer
 * @hdd_ctx: HDD context
 * @vdev_id: vdev id
 * @peer_addr: peer MAC address
 *
 * Sends a passthru peer delete message to PE via SME.
 *
 * Return: None
 */
void wlan_hdd_wondertap_peer_del(struct hdd_context *hdd_ctx,
				 uint8_t vdev_id,
				 const uint8_t *peer_addr);

/**
 * wlan_hdd_wondertap_register_ops() - Register wondertap operations
 * @dev: device handle
 *
 * This function registers the WLAN driver's wondertap operations with the
 * wondertap framework. It should be called during driver initialization
 * to enable wondertap functionality.
 *
 * Return: 0 on success, negative error code on failure
 */
int wlan_hdd_wondertap_register_ops(struct device *dev);

/**
 * wlan_hdd_wondertap_unregister_ops() - Unregister wondertap operations
 * @dev: device handle
 * @force_cleanup: force cleanup wondertap resources
 *
 * This function unregisters the WLAN driver's wondertap operations from the
 * wondertap framework. It should be called during driver cleanup to
 * properly release wondertap resources.
 *
 * Return: void
 */
void wlan_hdd_wondertap_unregister_ops(struct device *dev, bool force_cleanup);

/**
 * hdd_sme_passthrough_mode_callback() - Callback triggered by SME layer on
 *  successful channel change operation.
 * @vdev_id: vdev id
 * @is_up: is vdev up
 *
 * Return: None
 */
void hdd_sme_passthrough_mode_callback(uint8_t vdev_id, bool is_up);
#else
static inline int wlan_hdd_wondertap_register_ops(struct device *dev)
{
	return 0;
}

static inline
void wlan_hdd_wondertap_unregister_ops(struct device *dev, bool force_cleanup)
{
}

static inline
void hdd_sme_passthrough_mode_callback(uint8_t vdev_id, bool is_up)
{
}
#endif /*DRIVER_PASSTHRU_MODE */
#endif /* WLAN_HDD_WONDERTAP_H */
