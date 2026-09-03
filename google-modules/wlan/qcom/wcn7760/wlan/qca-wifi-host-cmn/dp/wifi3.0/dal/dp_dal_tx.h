/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef DP_DAL_TX_H
#define DP_DAL_TX_H

#include "dp_types.h"
#include "cdp_txrx_cmn_struct.h"
#include "dp_dal.h"
#include "qdf_status.h"
#include "dp_tx.h"

/**
 * enum dp_dal_tx_status - DAL TX return status codes
 * @DP_DAL_TX_SUCCESS: TX completed successfully
 * @DP_DAL_TX_QUEUED: TX queued for later processing during suspend
 * @DP_DAL_TX_FAILURE: TX failed
 */
enum dp_dal_tx_status {
	DP_DAL_TX_SUCCESS = 0,
	DP_DAL_TX_QUEUED = 1,
	DP_DAL_TX_FAILURE = -1,
};

/**
 * struct dp_dal_tx_metadata - DAL TX metadata structure
 * @msdu_info: Pointer to MSDU information structure containing packet
 *             details for transmission
 * @vdev: Pointer to DP VDEV structure
 * @tx_desc: Pointer to TX descriptor structure
 *
 * This structure encapsulates metadata required for DAL
 * TX operations, providing necessary packet information for
 * transmission handling.
 */
struct dp_dal_tx_metadata {
	struct dp_tx_msdu_info_s *msdu_info;
	struct dp_vdev *vdev;
	struct dp_tx_desc_s *tx_desc;
};

#if defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE)
/**
 * struct dp_dal_suspended_tx_desc - Suspended TX descriptor for runtime PM
 * @node: List node for linking suspended descriptors
 * @ring_id: Ring ID for the suspended TX
 * @vdev_id: VDEV ID for the suspended TX
 * @tcl_desc: Pointer to TCL descriptor (ownership transferred)
 * @tx_desc: Pointer to TX descriptor (global memory, remains valid)
 * @msdu_info: Heap-allocated copy of msdu_info (ownership transferred)
 *
 * This structure stores data needed for resume processing. The tx_desc
 * pointer remains valid since it points to global memory. A heap-allocated
 * copy of msdu_info is created to preserve the TX metadata across
 * suspend/resume cycles.
 */
struct dp_dal_suspended_tx_desc {
	qdf_list_node_t node;
	uint8_t ring_id;
	uint32_t vdev_id;
	void *tcl_desc;
	struct dp_tx_desc_s *tx_desc;
	struct dp_tx_msdu_info_s *msdu_info;
};
#endif /* defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE) */

/**
 * dp_dal_tx_cmp_isr_vendor_cb - tx cmlp ISR vendor callback
 * @ring_num: tx completion ring number
 * @priv: pointer to dp dal context
 *
 * Return: 0 on success
 */
int dp_dal_tx_cmp_isr_vendor_cb(int ring_num, void *priv);

/**
 * dp_dal_tx_bypass_mode() - Platform bus tx in bypass mode
 *
 * @priv: private data
 * @ring_id: ring ID for TCL descriptor enqueue
 * @ifidx: interface index
 * @desc: TX descriptor
 * @tx_metadata: pointer to dp_dal_tx_metadata structure containing MSDU info
 *
 * Return: 0 on success
 */
int dp_dal_tx_bypass_mode(void *priv, u8 ring_id, u32 ifidx, void *desc,
			  void *tx_metadata);

/**
 * dp_dal_tx_cpl_bypass_mode() - Skeleton for platform bus tx completion
 * handler in bypass mode
 *
 * @priv: private data
 * @cnt: packet count
 * @ring_id: Ring Id of the completion ring
 *
 * Return: true on success
 */
bool dp_dal_tx_cpl_bypass_mode(void *priv, u32 *cnt, u16 ring_id);

/**
 * dp_dal_tx_queue_active_bypass_mode() - Skeleton for platform tx queue active
 * in bypass mode
 *
 * @priv: private data
 * @flowid: flow id
 * @enable: enable or not
 *
 * Return: 0 on success
 */
int dp_dal_tx_queue_active_bypass_mode(void *priv, u16 flowid, bool enable);

/**
 * dp_dal_tx_hw_enqueue - Enqueue a BE TX packet (DAL stub).
 * @soc: DP SOC context.
 * @vdev: DP VDEV context.
 * @tx_desc: TX descriptor for the packet.
 * @fw_metadata: Firmware metadata associated with the packet.
 * @metadata: Exception metadata for TX path.
 * @msdu_info: MSDU information for the packet.
 *
 * This is a placeholder implementation that currently returns
 * %QDF_STATUS_SUCCESS. It should be replaced with the actual
 * hardware enqueue logic.
 *
 * Return: %QDF_STATUS_SUCCESS on success.
 */
QDF_STATUS dp_dal_tx_hw_enqueue(struct dp_soc *soc,
				struct dp_vdev *vdev,
				struct dp_tx_desc_s *tx_desc,
				uint16_t fw_metadata,
				struct cdp_tx_exception_metadata *metadata,
				struct dp_tx_msdu_info_s *msdu_info);

/**
 * dp_dal_tx_cpl_cb() - DAL TX completion callback handler
 * @priv: DAL context (dal_ctx)
 * @desc: TX completion descriptor
 * @ring_id: Ring ID
 *
 * This callback handler processes TX completion descriptors received from
 * the DAL module. It does descriptor validation checks from and adds
 * descriptors to a dedicated list within dal_ctx for later processing.
 *
 * Return: 0 for successful processing
 */
int dp_dal_tx_cpl_cb(void *priv, void *desc, u16 ring_id);

/**
 * dp_dal_tx_comp_handler() - DAL TX completion handler
 * @soc: DP SOC context
 * @ring_id: Ring ID
 * @dp_budget: NAPI budget
 *
 * This is the primary API for processing TX completions in the DAL module.
 * It invokes platform_bus_tx_cpl() to get completions from the DAL module,
 * then processes the descriptor list accumulated via tx_cpl_cb() by
 * calling dp_tx_comp_process_desc_list().
 *
 * Return: Number of completions processed
 */
uint32_t dp_dal_tx_comp_handler(struct dp_soc *soc, u16 ring_id,
				uint32_t dp_budget);

#if defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE)
/**
 * dp_dal_tx_flush_suspended_descs() - Flush suspended TX descriptors
 * @dal_ctx: DAL context
 *
 * Process all suspended TX descriptors during resume.
 *
 * Return: Number of descriptors processed
 */
uint32_t dp_dal_tx_flush_suspended_descs(struct dp_dal_ctx *dal_ctx);
#else
static inline uint32_t
dp_dal_tx_flush_suspended_descs(struct dp_dal_ctx *dal_ctx)
{
	return 0;
}
#endif /* defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE) */

static inline bool
dp_dal_tx_is_special_frame(qdf_nbuf_t nbuf, uint32_t frame_mask)
{
	if (((frame_mask & FRAME_MASK_IPV4_ARP) &&
	     qdf_nbuf_is_ipv4_arp_pkt(nbuf)) ||
	    ((frame_mask & FRAME_MASK_IPV4_DHCP) &&
	     qdf_nbuf_is_ipv4_dhcp_pkt(nbuf)) ||
	    ((frame_mask & FRAME_MASK_IPV4_EAPOL) &&
	     qdf_nbuf_is_ipv4_eapol_pkt(nbuf)) ||
	    ((frame_mask & FRAME_MASK_IPV6_DHCP) &&
	     qdf_nbuf_is_ipv6_dhcp_pkt(nbuf)))
		return true;

	return false;
}
#endif /* DP_DAL_TX_H */
