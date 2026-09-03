/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "dp_dal.h"
#include "dp_dal_rx.h"
#include "dp_dal_tx.h"
#include <qdf_types.h>
#include "qdf_mem.h"
#include "dp_rx.h"
#include "dp_peer.h"
#include "qdf_module.h"
#include "dp_dal_sim.h"
#include "dp_htt.h"
#include "qdf_platform.h"
#ifdef FEATURE_DP_DAL_OE_SUPPORT
/* Include the required dal headers to access DAL
 * platform ops.
 */
#endif /* FEATURE_DP_DAL_OE_SUPPORT */

#if defined(FEATURE_DP_DAL_OE_SUPPORT) || defined(FEATURE_DP_DAL_SIM)
extern struct platform_bus_ops *global_plat_ops;
#else
/* If DAL OE/DAL SIM support is not present, init platform ops
 * with bypass mode ops.
 */
extern struct platform_bus_ops plat_ops_bypass_mode;
struct platform_bus_ops *global_plat_ops = &plat_ops_bypass_mode;
#endif

/* DAL poll timer interval in milliseconds */
#define DAL_POLL_TIMER_INTERVAL_MS 10
#define DAL_POLL_TIMER_MAX_COUNT 10

/* DAL rx replenish retry timer interval in milliseconds */
#define DAL_RX_REPLENISH_RETRY_TIMER_MS 10
#define DAL_RX_REPLENISH_MAX_RETRIES 10
#define DAL_RX_REPLENISH_BACKOFF_MULTIPLIER 2

#define DAL_RX_POLL_BUDGET 255
#define DAL_TX_POLL_BUDGET 255

/**
 * dp_dal_bus_init_bypass_mode() - Skeleton for platform bus init
 * in bypass mode
 *
 * @priv: private data
 *
 * Return: 0 on success
 */
static int dp_dal_bus_init_bypass_mode(void *priv)
{
	return 0;
}

#if defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE)
/**
 * dp_dal_cleanup_suspended_tx_descs() - Cleanup suspended TX descriptors
 * @dal_ctx: DAL context
 *
 * This function cleans up any remaining suspended TX descriptors during
 * DAL deinit when FEATURE_RUNTIME_PM is enabled.
 *
 * Return: None
 */
static void dp_dal_cleanup_suspended_tx_descs(struct dp_dal_ctx *dal_ctx)
{
	struct dp_dal_suspended_tx_desc *suspended_desc;
	qdf_list_node_t *node;

	if (!dal_ctx)
		return;

	/* Clean up any remaining suspended TX descriptors */
	qdf_spin_lock_bh(&dal_ctx->suspended_tx_lock);
	while (!qdf_list_empty(&dal_ctx->suspended_tx_list)) {
		qdf_list_remove_front(&dal_ctx->suspended_tx_list, &node);
		dal_ctx->suspended_tx_count--;

		suspended_desc =
			qdf_container_of(node, struct dp_dal_suspended_tx_desc,
					 node);

		/* Free the TCL descriptor */
		if (suspended_desc->tcl_desc)
			qdf_mem_free(suspended_desc->tcl_desc);

		/* Free the heap-allocated msdu_info copy */
		if (suspended_desc->msdu_info)
			qdf_mem_free(suspended_desc->msdu_info);

		/* Free the suspended descriptor wrapper */
		qdf_mem_free(suspended_desc);
	}
	qdf_spin_unlock_bh(&dal_ctx->suspended_tx_lock);

	/* Destroy the suspended TX list and lock */
	qdf_list_destroy(&dal_ctx->suspended_tx_list);
	qdf_spinlock_destroy(&dal_ctx->suspended_tx_lock);
}

/**
 * dp_dal_init_suspended_tx_descs() - Initialize suspended TX descriptors
 * @dal_ctx: DAL context
 *
 * This function initializes suspended TX descriptor list and lock during
 * DAL init when FEATURE_RUNTIME_PM is enabled.
 *
 * Return: None
 */
static void dp_dal_init_suspended_tx_descs(struct dp_dal_ctx *dal_ctx)
{
	if (!dal_ctx)
		return;

	qdf_spinlock_create(&dal_ctx->suspended_tx_lock);
	qdf_list_create(&dal_ctx->suspended_tx_list, 0);
	dal_ctx->suspended_tx_count = 0;
}
#else
/**
 * dp_dal_cleanup_suspended_tx_descs() - No-op when FEATURE_RUNTIME_PM disabled
 * @dal_ctx: DAL context
 *
 * Return: None
 */
static inline void
dp_dal_cleanup_suspended_tx_descs(struct dp_dal_ctx *dal_ctx)
{
}

/**
 * dp_dal_init_suspended_tx_descs() - No-op when FEATURE_RUNTIME_PM disabled
 * @dal_ctx: DAL context
 *
 * Return: None
 */
static inline void
dp_dal_init_suspended_tx_descs(struct dp_dal_ctx *dal_ctx)
{
}
#endif /* defined(FEATURE_RUNTIME_PM) || defined (DP_POWER_SAVE) */

/**
 * dp_dal_bus_exit_bypass_mode() - Skeleton for platform bus exit in bypass mode
 *
 * @priv: private data
 */
static void dp_dal_bus_exit_bypass_mode(void *priv)
{
}

/**
 * dp_dal_bus_start_bypass_mode() - Skeleton for platform bus start
 * in bypass mode
 *
 * @priv: private data
 *
 * Return: 0 on success
 */
static int dp_dal_bus_start_bypass_mode(void *priv)
{
	return 0;
}

/**
 * dp_dal_bus_stop_bypass_mode() - Skeleton for platform bus stop in bypass mode
 *
 * @priv: private data
 */
static void dp_dal_bus_stop_bypass_mode(void *priv)
{
}

/**
 * dp_dal_request_irq_bypass_mode() - Skeleton for platform bus request irq in
 * bypass mode
 *
 * @priv: private data
 *
 * Return: 0 on success
 */
static int dp_dal_request_irq_bypass_mode(void *priv)
{
	return 0;
}

/**
 * dp_dal_sta_active_bypass_mode() - Skeleton for platform bus sta active
 * in bypass mode
 *
 * @priv: private data
 * @info: sta info
 * @enable: enable
 *
 * Return: 0 on success
 */
static int dp_dal_sta_active_bypass_mode(void *priv,
					 struct sta_info *info, bool enable)
{
	return 0;
}

/**
 * dp_dal_notify_suspend_bypass_mode() - Skeleton for platform bus notify
 * suspend in bypass mode
 *
 * @priv: private data
 * @intf_pause: Interface pause flag
 *
 * Return: 0 on success
 */
static int dp_dal_notify_suspend_bypass_mode(void *priv, bool intf_pause)
{
	return 0;
}

/**
 * dp_dal_notify_resume_bypass_mode() - Skeleton for platform bus notify resume
 * in bypass mode
 *
 * @priv: private data
 *
 * Return: 0 on success
 */
static int dp_dal_notify_resume_bypass_mode(void *priv)
{
	return 0;
}

/**
 * dp_dal_ssr_dump_bypass_mode() - platform bus ssr dump
 *				   in bypass mode.
 *
 * This function is no-op in bypass mode.
 *
 * @segment: segment
 */
static void dp_dal_ssr_dump_bypass_mode(void *segment)
{
}

/**
 * dp_dal_intf_init_bypass_mode() - interface initialization in bypass mode
 * @priv: pointer to dal context
 * @intf_info: interface info
 *
 * Return: 0 on success
 */
static int dp_dal_intf_init_bypass_mode(void *priv, void *intf_info)
{
	return 0;
}

/**
 * dp_dal_intf_deinit_bypass_mode() - interface deinitialization in bypass mode
 * @priv: pointer to dal context
 * @vdev_id: vdev id corresponds to interface
 *
 * Return: 0 on success
 */
static int dp_dal_intf_deinit_bypass_mode(void *priv, uint16_t vdev_id)
{
	return 0;
}

/**
 * dp_dal_pdev_set_default_routing_helper() - Helper to set default routing
 * @soc: pointer to dp_soc structure
 * @peer: pointer to dp_peer structure
 * @arg: pointer to dp_pdev structure
 *
 * Return: None
 */
static void dp_dal_pdev_set_default_routing_helper(struct dp_soc *soc,
						   struct dp_peer *peer,
						   void *arg)
{
	struct dp_pdev *pdev = (struct dp_pdev *)arg;
	struct dp_vdev *vdev = peer->vdev;
	uint32_t reo_dest;
	uint8_t lmac_peer_id_msb = 0;
	bool hash_based;

	if (soc->dp_dal_mode == DAL_DP_OFFLOAD_MODE) {
		hash_based = false;
		reo_dest = (peer->vdev->qdf_opmode == QDF_STA_MODE) ?
			DAL_DP_DEFAULT_REO_STA : DAL_DP_DEFAULT_REO_SAP;
	} else {
		dp_vdev_get_default_reo_hash(vdev, &reo_dest, &hash_based);
	}

	if (soc->cdp_soc.ol_ops->peer_set_default_routing)
		soc->cdp_soc.ol_ops->peer_set_default_routing(soc->ctrl_psoc,
							      pdev->pdev_id,
							      peer->mac_addr.raw,
							      peer->vdev->vdev_id,
							      hash_based,
							      reo_dest,
							      lmac_peer_id_msb);
}

/**
 * dp_dal_bus_exit() - DAL bus exit
 * @soc: pointer to DP SoC
 *
 * Called during driver deinit dp_pdev_deinit(), this function will release all
 * allocated resources in the offload engine and stops the Offload Engine.
 *
 * Return: void
 */
static void dp_dal_bus_exit(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx)
		return;

	if (global_plat_ops && global_plat_ops->exit)
		global_plat_ops->exit(dal_ctx);
}

/**
 * dp_dal_bus_init() - DAL bus initialization function
 * @soc: pointer to DP SoC
 *
 * Called during cdp_soc_attach_target(), this function sync TXBM information
 * to the offload engine.
 *
 * Return: int
 */
static int dp_dal_bus_init(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx)
		return -EINVAL;

	if (global_plat_ops && global_plat_ops->init)
		return global_plat_ops->init(dal_ctx);

	return 0;
}

/**
 * dp_dal_bus_stop - Stop DP DAL bus
 * @soc: pointer to dp_soc structure
 *
 * This function stops the DP DAL bus associated with the given SOC.
 */
static void dp_dal_bus_stop(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx)
		return;

	if (global_plat_ops && global_plat_ops->stop)
		global_plat_ops->stop(dal_ctx);
}

/**
 * dp_dal_bus_start() - DAL bus start function
 * @soc: pointer to DP SoC
 *
 * Called during cdp_soc_attach_target(), this function sync ring information
 * to the offload engine.
 *
 * Return: int
 */
static int dp_dal_bus_start(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx)
		return -EINVAL;

	if (global_plat_ops && global_plat_ops->start)
		return global_plat_ops->start(dal_ctx);

	return 0;
}

/**
 * dp_dal_is_d3_wow_supported() - Check if D3 WOW is supported by FW
 * @soc: Pointer to DP soc
 *
 * This function checks if the firmware supports D3 WOW capability by
 * querying the DP SOC features flags set during service ready event
 * processing via cdp_soc_set_param.
 *
 * Return: true if D3 WOW is supported, false otherwise
 */
static bool dp_dal_is_d3_wow_supported(struct dp_soc *soc)
{
	if (!soc) {
		dp_err("NULL soc");
		return false;
	}

	return soc->features.dal_d3_wow_support;
}

static int dp_dal_set_suspend_config(void *priv, uint64_t msi_addr,
				     uint32_t msi_data,
				     void *suspend_msg_data_vaddr,
				     qdf_dma_addr_t suspend_msg_data_paddr)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;

	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return -EINVAL;
	}

	dal_ctx->suspend_msg_msi_addr = msi_addr;
	dal_ctx->suspend_msg_msi_data = msi_data;

	/* Store DMA buffer info passed from dp_dal_sim.c */
	dal_ctx->suspend_msg_data_vaddr = suspend_msg_data_vaddr;
	dal_ctx->suspend_msg_data_paddr = suspend_msg_data_paddr;

	return 0;
}

/**
 * dp_dal_d3_wow_htt_send() - Send DAL mode info to firmware
 * @soc: Pointer to DP soc
 *
 * This function sends the DAL mode and FW write configuration to firmware
 * via HTT message. It first checks if D3 WoW is supported by FW before
 * sending the message. On success, sets the suspend_htt_msg_sent flag.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS dp_dal_d3_wow_htt_send(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;
	struct htt_h2t_msg_dal_suspend_info dal_suspend_info;
	struct dp_pdev *pdev;
	QDF_STATUS status;

	/* Check if D3 WOW is supported by FW */
	if (!dp_dal_is_d3_wow_supported(soc)) {
		dp_info("D3 WOW not supported by FW - skipping HTT message");
		return QDF_STATUS_SUCCESS;
	}

	pdev = dp_get_pdev_from_soc_pdev_id_wifi3(soc, 0);
	if (!pdev)
		return QDF_STATUS_E_INVAL;

	qdf_mem_zero(&dal_suspend_info, sizeof(dal_suspend_info));
	dal_suspend_info.pdev_id = pdev->pdev_id;
	dal_suspend_info.mode = soc->dp_dal_mode;

	if (soc->dp_dal_mode == DAL_DP_OFFLOAD_MODE) {
		/* Fill FW write buffer addresses */
		dal_suspend_info.write_data_addr_lo =
			(uint32_t)(dal_ctx->suspend_msg_data_paddr & 0xFFFFFFFF);
		dal_suspend_info.write_data_addr_hi =
			(uint32_t)((dal_ctx->suspend_msg_data_paddr >> 32) &
				   0xFFFFFFFF);

		/* Fill MSI configuration */
		dal_suspend_info.write_msi_addr_lo =
			(uint32_t)(dal_ctx->suspend_msg_msi_addr & 0xFFFFFFFF);
		dal_suspend_info.write_msi_addr_hi =
			(uint32_t)((dal_ctx->suspend_msg_msi_addr >> 32) &
				   0xFFFFFFFF);
		dal_suspend_info.write_msi_data = dal_ctx->suspend_msg_msi_data;
	}

	status = dp_h2t_dal_mode_info_send(soc, &dal_suspend_info);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("Failed to send DAL mode info to FW, status=%d", status);
		return status;
	}

	/* Set flag to indicate HTT message was sent successfully */
	dal_ctx->suspend_htt_msg_sent = true;

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_d3_wow_htt_send_on_suspend() - Send HTT message during suspend if
 * not sent during init
 * @soc: Pointer to DP soc
 *
 * This function is called during suspend to send the HTT message if it was
 * not sent during init (e.g., due to late WMI capability arrival). This
 * ensures the message is sent before the first suspend.
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS dp_dal_d3_wow_htt_send_on_suspend(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx)
		return QDF_STATUS_E_INVAL;

	/* If HTT message was not sent during init, send it now */
	if (!dal_ctx->suspend_htt_msg_sent) {
		dp_info("HTT message not sent during init, sending on suspend");
		return dp_dal_d3_wow_htt_send(soc);
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_bus_request_irq() - DAL IRQ registration function
 * @soc: pointer to DP SoC
 *
 * Called during cdp_soc_attach_target(), this function sync IRQ info to OE,
 * OE will register Tx & Rx interrupts.
 *
 * Return: int
 */
static int dp_dal_bus_request_irq(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx)
		return -EINVAL;

	if (global_plat_ops && global_plat_ops->request_irq)
		return global_plat_ops->request_irq(dal_ctx);

	return 0;
}

/**
 * dp_dal_pdev_set_default_routing - Iterate over pdev->vdev->peer list
 * and set default routing for each peer.
 * @pdev: pointer to dp_pdev structure
 *
 * This function is called during mode switch from bypass to offload
 * and vice versa.
 *
 * Return: None
 */
static void dp_dal_pdev_set_default_routing(struct dp_pdev *pdev)
{
	struct dp_soc *soc;
	struct dp_vdev *vdev;

	if (!pdev) {
		dp_err("Invalid pdev");
		return;
	}

	soc = pdev->soc;
	if (!soc) {
		dp_err("Invalid soc");
		return;
	}

	qdf_spin_lock_bh(&pdev->vdev_list_lock);
	DP_PDEV_ITERATE_VDEV_LIST(pdev, vdev) {
		if (dp_vdev_get_ref(soc, vdev, DP_MOD_ID_CDP))
			continue;

		if (vdev->qdf_opmode == QDF_STA_MODE ||
		    vdev->qdf_opmode == QDF_SAP_MODE)
			dp_vdev_iterate_peer(vdev,
					     dp_dal_pdev_set_default_routing_helper,
					     pdev, DP_MOD_ID_CDP);

		dp_vdev_unref_delete(soc, vdev, DP_MOD_ID_CDP);
	}
	qdf_spin_unlock_bh(&pdev->vdev_list_lock);
}

static void dp_dal_save_ring_hp_tp(struct dp_dal_ctx *dal_ctx,
				   struct dp_soc *soc)
{
	struct dal_srng *dal_srng;
	struct hal_srng *hal_srng;
	uint8_t ring_num;
	int i;

	/* REO DST ring */
	for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
		dal_srng = &dal_ctx->rx_ring[i];
		ring_num = dal_srng->ring_num;

		if (!dal_srng->initialized ||
		    ring_num >= soc->num_reo_dest_rings)
			continue;

		hal_srng =
		(struct hal_srng *)soc->reo_dest_ring[ring_num].hal_srng;
		if (!hal_srng) {
			dp_err("hal_srng is NULL for REO DST ring:%d",
			       ring_num);
			continue;
		}

		dal_srng->u.dst_ring.tp = hal_srng->u.dst_ring.tp;
		dp_info("updated REO DST ring:%d TP:%u to dal srng",
			ring_num, dal_srng->u.dst_ring.tp);
	}

	/* TX completion ring */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		dal_srng = &dal_ctx->tx_cmpl_ring[i];
		ring_num = dal_srng->ring_num;

		if (!dal_srng->initialized ||
		    ring_num >= soc->num_tx_comp_rings)
			continue;

		hal_srng =
		(struct hal_srng *)soc->tx_comp_ring[ring_num].hal_srng;
		if (!hal_srng) {
			dp_err("hal_srng is NULL for TX compl ring:%d",
			       ring_num);
			continue;
		}

		dal_srng->u.dst_ring.tp = hal_srng->u.dst_ring.tp;
		dp_info("updated TX comp ring:%d TP:%u to dal srng",
			ring_num, dal_srng->u.dst_ring.tp);
	}

	/* TX ring */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		dal_srng = &dal_ctx->tx_ring[i];
		ring_num = dal_srng->ring_num;

		if (!dal_srng->initialized ||
		    ring_num >= soc->num_tcl_data_rings)
			continue;

		hal_srng =
		(struct hal_srng *)soc->tcl_data_ring[ring_num].hal_srng;
		if (!hal_srng) {
			dp_err("hal_srng is NULL for TX ring:%d",
			       ring_num);
			continue;
		}

		dal_srng->u.src_ring.hp = hal_srng->u.src_ring.hp;
		dp_info("updated Tx ring :%d HP:%u to dal srng",
			ring_num, hal_srng->u.src_ring.hp);
	}
}

/**
 * dp_dal_mode_switch_bypass_to_offload - Handles mode switch indication from
 * bypass to offload
 * @dal_ctx: DAL context
 *
 * This function performs the following actions:
 * 1. Sets a flag indicating that a mode switch is in progress, which prevents
 *	suspension during this transition
 * 2. Updates the DAL mode to ensure that any new peer connection will be set
 *	up with hash-based routing
 * 3. Update the latest TP for DAL owned rings in dal srng.
 * 4. Completes the init sequence with DAL
 * 5. Iterates over the vdev list and send interface information of STA and SAP
 * 6. Iterates over connected peers in STA/SAP modes to enable peer-based
 *	routing for them
 * 7. Resets the mode switch in progress flag to false once the operation
 *	is finished
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
dp_dal_mode_switch_bypass_to_offload(struct dp_dal_ctx *dal_ctx)
{
	struct dp_soc *soc;
	struct dp_pdev *pdev;
	struct dp_vdev *vdev;
	QDF_STATUS status;

	if (!dal_ctx) {
		dp_err("DAL context is NULL, reject mode switch");
		return QDF_STATUS_E_INVAL;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC context is NULL, reject mode switch");
		return QDF_STATUS_E_INVAL;
	}

	pdev = soc->pdev_list[0];
	if (!pdev) {
		dp_err("PDEV is NULL, reject mode switch");
		return QDF_STATUS_E_INVAL;
	}

	qdf_timer_sync_cancel(&dal_ctx->dal_poll_timer);

	soc->dp_dal_mode = DAL_DP_OFFLOAD_MODE;

	/*
	 * This is necessary when a previous mode switch occurred from offload
	 * to bypass, and the host driver may have polled the rings managed by
	 * OE. Therefore, the latest TP value must be provided to DAL during
	 * the switch back from bypass to offload mode.
	 */
	dp_dal_save_ring_hp_tp(dal_ctx, soc);

	status = dp_dal_bus_init(soc);
	if (status != QDF_STATUS_SUCCESS) {
		dp_err("DAL platform bus init failed during mode switch %d",
		       status);
		goto abort_mode_switch;
	}

	status = dp_dal_bus_request_irq(soc);
	if (status != QDF_STATUS_SUCCESS) {
		dp_err("DAL ptfm bus request IRQ failed during mode switch %d",
		       status);
		goto bus_exit;
	}

	status = dp_dal_d3_wow_htt_send(soc);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("Failed to send DAL mode info to FW");
		goto bus_exit;
	}

	status = dp_dal_bus_start(soc);
	if (status != QDF_STATUS_SUCCESS) {
		dp_err("DAL platform bus start failed during mode switch %d",
		       status);
		goto revert_fw_mode;
	}

	qdf_spin_lock_bh(&pdev->vdev_list_lock);
	DP_PDEV_ITERATE_VDEV_LIST(pdev, vdev) {
		if (dp_vdev_get_ref(soc, vdev, DP_MOD_ID_CDP))
			continue;

		if (vdev->qdf_opmode == QDF_STA_MODE ||
		    vdev->qdf_opmode == QDF_SAP_MODE) {
			status = dp_dal_interface_add(soc, vdev);
			if (status) {
				dp_err("Failed to add interface for vdev_id:%d",
				       vdev->vdev_id);
				dp_vdev_unref_delete(soc, vdev, DP_MOD_ID_CDP);
				qdf_spin_unlock_bh(&pdev->vdev_list_lock);
				goto revert_fw_mode;
			}
		}

		dp_vdev_unref_delete(soc, vdev, DP_MOD_ID_CDP);
	}
	qdf_spin_unlock_bh(&pdev->vdev_list_lock);

	dp_dal_pdev_set_default_routing(pdev);

	soc->dal_mode_switch_in_progress = false;
	dp_info("Mode switch from bypass to offload completed successfully");

	/*
	 * Ensure that any ongoing replenish operations are completed before
	 * returning from mode switch indication to DAL. Also make sure no
	 * further replenish from bypass path after this.
	 */
	qdf_spin_lock_bh(&dal_ctx->dal_replenish_lock);
	qdf_atomic_set(&dal_ctx->bm_replenish_not_allowed, 1);
	qdf_spin_unlock_bh(&dal_ctx->dal_replenish_lock);

	if (qdf_atomic_read(&dal_ctx->rx_replenish_failures))
		qdf_timer_mod(&dal_ctx->rx_replenish_retry_timer,
			      dal_ctx->rx_replenish_retry_interval_ms);

	return QDF_STATUS_SUCCESS;

revert_fw_mode:
	soc->dp_dal_mode = DAL_DP_BYPASS_MODE;
	status = dp_dal_d3_wow_htt_send(soc);
	if (QDF_IS_STATUS_ERROR(status))
		dp_err("Failed to send DAL mode info to FW");

bus_exit:
	dp_dal_bus_exit(soc);
abort_mode_switch:
	soc->dp_dal_mode = DAL_DP_BYPASS_MODE;
	soc->dal_mode_switch_in_progress = false;

	qdf_spin_lock_bh(&dal_ctx->dal_replenish_lock);
	qdf_atomic_set(&dal_ctx->bm_replenish_not_allowed, 0);
	qdf_spin_unlock_bh(&dal_ctx->dal_replenish_lock);

	/* set hash-based routing since the offload mode switch failed */
	dp_dal_pdev_set_default_routing(pdev);
	qdf_timer_mod(&dal_ctx->dal_poll_timer, DAL_POLL_TIMER_INTERVAL_MS);
	dp_err("DAL mode switch from bypass to offload aborted due to failure");
	return QDF_STATUS_E_FAILURE;
}

/**
 * dp_dal_mode_switch_offload_to_bypass - Handle mode switch from offload mode
 * to bypass mode
 * @dal_ctx: DAL context
 *
 * This function performs the following actions:
 * 1. Sets a flag indicating that a mode switch is in progress, which prevents
 *	suspension during this transition
 * 2. Updates the DAL mode to ensure that any new peer connection will be set
 *	up with peer-based routing
 * 3. Iterates over connected peers in STA/SAP modes to enable hash-based
 *	routing for them
 * 4. Retrieves the current HP/TP snapshot from DAL, which will be used to poll
 *	DAL rings after the mode switch completes
 * 5. Resets the mode switch in progress flag to false once the operation is
 *	finished
 *
 * Return: QDF_STATUS
 */
static QDF_STATUS
dp_dal_mode_switch_offload_to_bypass(struct dp_dal_ctx *dal_ctx)
{
	struct dp_soc *soc;
	struct dp_pdev *pdev;
	QDF_STATUS status;

	if (!dal_ctx) {
		dp_err("DAL context is NULL, reject mode switch");
		return QDF_STATUS_E_INVAL;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC is NULL, reject mode switch");
		return QDF_STATUS_E_INVAL;
	}

	pdev = soc->pdev_list[0];
	if (!pdev) {
		dp_err("PDEV is NULL reject mode switch");
		return QDF_STATUS_E_INVAL;
	}

	qdf_spin_lock_bh(&dal_ctx->dal_replenish_lock);
	qdf_atomic_set(&dal_ctx->bm_replenish_not_allowed, 0);
	qdf_spin_unlock_bh(&dal_ctx->dal_replenish_lock);

	soc->dp_dal_mode = DAL_DP_BYPASS_MODE;
	status = dp_dal_d3_wow_htt_send(soc);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("Failed to send DAL mode info to FW");
		soc->dp_dal_mode = DAL_DP_OFFLOAD_MODE;
		soc->dal_mode_switch_in_progress = false;
		return QDF_STATUS_E_FAILURE;
	}

	/* Start polling timer */
	dal_ctx->poll_count = 0;
	qdf_timer_mod(&dal_ctx->dal_poll_timer, DAL_POLL_TIMER_INTERVAL_MS);

	soc->dal_mode_switch_in_progress = false;
	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_mode_switch_ind_handler - handler for mode switch indication
 * @priv: pointer to dal context
 * @cur_mode: current operating mode
 * @new_mode: new operating mode
 *
 * Return: 0 on success, non-zero on failure.
 */
static int dp_dal_mode_switch_ind_handler(void *priv, u8 cur_mode, u8 new_mode)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct qdf_op_sync *op_sync;
	QDF_STATUS pm_status;
	int ret = 0;

	if (qdf_op_protect(&op_sync)) {
		dp_err_rl("Driver in transitional state, reject mode switch cur:%d new:%d",
			  cur_mode, new_mode);
		return -EINVAL;
	}

	if (!dal_ctx) {
		dp_err("DAL context is NULL, reject mode switch");
		ret = -EINVAL;
		goto out;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC is NULL, reject mode switch");
		ret = -EINVAL;
		goto out;
	}

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, reject mode switch");
		ret = -EINVAL;
		goto out;
	}

	/* Set mode switch in progress flag before preventing runtime suspend
	 * to ensure to call local ops in dp dal resume sequence during mode switch
	 */
	soc->dal_mode_switch_in_progress = true;

	/* Prevent runtime suspend during mode switch to avoid register access
	 * during suspend state which can cause system crash
	 */
	pm_status = qdf_runtime_pm_prevent_suspend_sync(
				&dal_ctx->mode_switch_runtime_lock);
	if (QDF_IS_STATUS_ERROR(pm_status)) {
		dp_err("Failed to prevent runtime suspend, reject mode switch cur:%d new:%d",
		       cur_mode, new_mode);
		ret = -EBUSY;
		soc->dal_mode_switch_in_progress = false;
		goto out;
	}

	if (cur_mode == DAL_DP_BYPASS_MODE &&
	    new_mode == DAL_DP_OFFLOAD_MODE) {
		if (dp_dal_mode_switch_bypass_to_offload(dal_ctx) !=
		    QDF_STATUS_SUCCESS)
			ret = -EINVAL;
	} else if (cur_mode == DAL_DP_OFFLOAD_MODE &&
		   new_mode == DAL_DP_BYPASS_MODE) {
		if (dp_dal_mode_switch_offload_to_bypass(dal_ctx) !=
		    QDF_STATUS_SUCCESS)
			ret = -EINVAL;
	} else {
		dp_err("invalid mode switch ind rcvd cur_mode:%d new_mode:%d",
		       cur_mode, new_mode);
		ret = -EINVAL;
	}

	/* Reset mode switch in progress flag*/
	soc->dal_mode_switch_in_progress = false;

	/* Allow runtime suspend after mode switch operations complete */
	qdf_runtime_pm_allow_suspend(&dal_ctx->mode_switch_runtime_lock);

out:
	qdf_op_unprotect(op_sync);
	return ret;
}

struct platform_bus_ops plat_ops_bypass_mode = {
	.init = dp_dal_bus_init_bypass_mode,
	.exit = dp_dal_bus_exit_bypass_mode,
	.start = dp_dal_bus_start_bypass_mode,
	.stop = dp_dal_bus_stop_bypass_mode,
	.request_irq = dp_dal_request_irq_bypass_mode,
	.rx = dp_dal_rx_bypass_mode,
	.rx_replenish = dp_dal_rx_replenish_bypass_mode,
	.rxbm_sync = dp_dal_rx_rxbm_sync_bypass_mode,
	.tx = dp_dal_tx_bypass_mode,
	.tx_cpl = dp_dal_tx_cpl_bypass_mode,
	.tx_queue_active = dp_dal_tx_queue_active_bypass_mode,
	.sta_active = dp_dal_sta_active_bypass_mode,
	.notify_suspend = dp_dal_notify_suspend_bypass_mode,
	.notify_resume = dp_dal_notify_resume_bypass_mode,
	.ssr_dump = dp_dal_ssr_dump_bypass_mode,
	.intf_init = dp_dal_intf_init_bypass_mode,
	.intf_deinit = dp_dal_intf_deinit_bypass_mode,
	.rx_pkt_reinject = dp_dal_rx_pkt_reinject_bypass_mode,
};

qdf_export_symbol(plat_ops_bypass_mode);

static inline void
dp_dal_fill_srng_params(struct hal_srng *srng,
			struct hal_srng_params *params,
			uint64_t msi_addr, uint32_t msi_data)
{
	params->msi_addr = msi_addr;
	params->msi_data = msi_data;
	params->intr_timer_thres_us = srng->intr_timer_thres_us;
	params->intr_batch_cntr_thres_entries =
					srng->intr_batch_cntr_thres_entries;
}

static int
dp_dal_set_msi_config(void *priv, uint8_t ring_num, uint8_t ring_type,
		      uint64_t msi_addr, uint32_t msi_data)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *dp_soc = dal_ctx->soc;
	struct hal_srng *srng;
	struct hal_srng_params params = {0};

	/*
	 * This API is invoked only when the mode is set to offload.
	 * Store the mode that will be applied when sending the default
	 * routing configuration to the peer.
	 */
	if (dp_soc->dp_dal_mode != DAL_DP_OFFLOAD_MODE)
		dp_soc->dp_dal_mode = DAL_DP_OFFLOAD_MODE;

	/* Save offload platform ops to use during mode switch resume call */
	if (!dal_ctx->offload_plat_ops) {
		dal_ctx->offload_plat_ops = global_plat_ops;
		dp_info("Saved offload platform ops");
	}

	dp_info("DAL: ring_type:%d num:%d msi_addr:%llu msi_data:%u",
		ring_type, ring_num, msi_addr, msi_data);

	if (ring_type == REO_DST) {
		if (ring_num >= dp_soc->num_reo_dest_rings) {
			dp_err("Invalid REO ring_num:%d received", ring_num);
			return QDF_STATUS_E_INVAL;
		}

		srng =
		(struct hal_srng *)dp_soc->reo_dest_ring[ring_num].hal_srng;

		dp_dal_fill_srng_params(srng, &params, msi_addr, msi_data);
		return hal_srng_set_msi_irq_config(dp_soc->hal_soc,
						   (hal_ring_handle_t)srng,
						   &params);
	} else if (ring_type == COMP_RING_TYPE) {
		if (ring_num >= dp_soc->num_tx_comp_rings) {
			dp_err("Invalid TX comp ring_num:%d received",
			       ring_num);
			return QDF_STATUS_E_INVAL;
		}

		srng =
		(struct hal_srng *)dp_soc->tx_comp_ring[ring_num].hal_srng;

		dp_dal_fill_srng_params(srng, &params, msi_addr, msi_data);
		return hal_srng_set_msi_irq_config(dp_soc->hal_soc,
						   (hal_ring_handle_t)srng,
						   &params);
	} else {
		dp_err("Invalid ring_type:%d received", ring_type);
		return QDF_STATUS_E_INVAL;
	}
}

static inline void
dp_dal_store_ring_info(struct dp_soc *soc, uint8_t ring_type,
		       uint8_t ring_num, uint32_t hp, uint32_t tp)
{
	struct hal_srng *hal_srng;

	switch (ring_type) {
	case REO_DST:
		if (ring_num >= soc->num_reo_dest_rings) {
			dp_err("invalid reo ring num %d rcvc", ring_num);
			return;
		}

		hal_srng =
			(struct hal_srng *)soc->reo_dest_ring[ring_num].hal_srng;
		if (hal_srng) {
			hal_srng->u.dst_ring.tp = tp;
			hal_srng->u.dst_ring.cached_hp = hp;
			dp_info("Updated REO DST ring %d: TP=0x%x HP= 0x%x",
				ring_num, tp, hp);
		} else {
			dp_err("SRNG is null for reo dst ring %d", ring_num);
			return;
		}

		break;
	case COMP_RING_TYPE:
		if (ring_num >= soc->num_tx_comp_rings) {
			dp_err("invalid tx cmpl ring num %d rcvc", ring_num);
			return;
		}

		hal_srng =
			(struct hal_srng *)soc->tx_comp_ring[ring_num].hal_srng;
		if (hal_srng) {
			hal_srng->u.dst_ring.tp = tp;
			hal_srng->u.dst_ring.cached_hp = hp;
			dp_info("Updated TX comp ring %d: TP=0x%x HP= 0x%x",
				ring_num, tp, hp);
		} else {
			dp_err("SRNG is null for tx cmpl ring %d", ring_num);
			return;
		}

		break;
	case TCL_DATA:
		if (ring_num >= soc->num_tcl_data_rings) {
			dp_err("invalid tx cmpl ring num %d rcvc", ring_num);
			return;
		}

		hal_srng =
			(struct hal_srng *)soc->tcl_data_ring[ring_num].hal_srng;
		if (hal_srng) {
			hal_srng->u.src_ring.hp = hp;
			hal_srng->u.src_ring.cached_tp = tp;
			dp_info("Updated tx ring %d:  HP=0x%x TP=0x%x",
				ring_num, hp, tp);
		} else {
			dp_err("invalid tx data ring num %d rcvc", ring_num);
			return;
		}
		break;
	case RXDMA_BUF:
		if (ring_num != 0) {
			dp_err("Invalid Rx refill ring num %d rcvd", ring_num);
			return;
		}

		hal_srng =
			(struct hal_srng *)soc->rx_refill_buf_ring[ring_num].hal_srng;
		if (hal_srng) {
			hal_srng->u.src_ring.hp = hp;
			hal_srng->u.src_ring.cached_tp = tp;
			dp_info("Updated rx refill ring  HP=0x%x TP=0x%x",
				hp, tp);
		} else {
			dp_err("SRNG is null for rx refill ring");
			return;
		}

		break;
	default:
		dp_err("invalid ring type %d received", ring_type);
	}
}

static int
dp_dal_early_mode_switch_ind_handler(void *priv, void *ring_info,
				     uint8_t num_info, uint8_t cur_mode,
				     uint8_t new_mode)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct dal_ring_hp_tp_info *info;
	int i;

	if (!dal_ctx) {
		dp_err("DAL ctx is null");
		return -EINVAL;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC ctx is null");
		return -EINVAL;
	}

	if (!ring_info) {
		dp_err("ring info is empty");
		/* TODO: trigger self recovery? */
		QDF_BUG(0);
		goto set_def_routing;
	}

	info = (struct dal_ring_hp_tp_info *)ring_info;

	for (i = 0; i < num_info; i++)
		dp_dal_store_ring_info(soc, info[i].ring_type, info[i].ring_id,
				       info[i].hp, info[i].tp);

set_def_routing:
	if (cur_mode == DAL_DP_OFFLOAD_MODE &&
	    new_mode == DAL_DP_BYPASS_MODE) {
		soc->dp_dal_mode = DAL_DP_BYPASS_MODE;
		dp_dal_pdev_set_default_routing(soc->pdev_list[0]);
	}

	return 0;
}

struct vendor_cb_ops vendor_cb = {
	.rx_isr_cb = dp_dal_rx_isr_vendor_cb,
	.rx_replenish_alloc_cb = dp_dal_rx_replenish_alloc_vendor_cb,
	.rx_cpl_cb = dp_dal_rx_desc_cb,
	.tx_isr_cb = dp_dal_tx_cmp_isr_vendor_cb,
	.tx_cpl_cb = dp_dal_tx_cpl_cb,
	.set_msi_config = dp_dal_set_msi_config,
	.early_mode_switch_ind = dp_dal_early_mode_switch_ind_handler,
	.mode_switch_ind = dp_dal_mode_switch_ind_handler,
	.set_suspend_msi_config = dp_dal_set_suspend_config,
};

qdf_export_symbol(vendor_cb);

#if defined(FEATURE_DAL_DP_SUPPORT) && !defined(FEATURE_DP_DAL_SIM)
static inline void dp_dal_bus_vote_link_up(struct dp_soc *soc)
{
	return hif_vote_link_up(soc->hif_handle);
}

static inline void dp_dal_bus_vote_link_down(struct dp_soc *soc)
{
	return hif_vote_link_down(soc->hif_handle);
}
#else
static inline void dp_dal_bus_vote_link_up(struct dp_soc *soc)
{
}

static inline void dp_dal_bus_vote_link_down(struct dp_soc *soc)
{
}
#endif

/**
 * dp_dal_soc_detach - detach DP DAL to SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: None.
 */
void dp_dal_soc_detach(struct dp_soc *soc)
{
	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping soc detach");
		return;
	}

	dp_dal_sim_detach(soc->dal_ctx);
	qdf_mem_common_free(soc->dal_ctx);
	soc->dal_ctx = NULL;
	dp_info("DAL context destroyed");
}

/**
 * dp_dal_soc_deinit - De-initialize DP DAL for SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: None.
 */
void dp_dal_soc_deinit(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx;

	if (!soc)
		return;

	if (soc->cdp_soc.ol_ops->get_con_mode &&
	    soc->cdp_soc.ol_ops->get_con_mode() == QDF_GLOBAL_FTM_MODE)
		return;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping soc deinit");
		return;
	}

	if (!soc->dal_ctx)
		return;

	dal_ctx = soc->dal_ctx;

	dp_dal_bus_vote_link_down(soc);

	qdf_atomic_set(&soc->dal_ctx->deinit_in_progress, 1);

	dp_dal_cleanup_suspended_tx_descs(dal_ctx);

	qdf_timer_sync_cancel(&soc->dal_ctx->dal_poll_timer);
	qdf_timer_sync_cancel(&soc->dal_ctx->rx_replenish_retry_timer);

	dp_dal_rx_desc_list_cleanup(dal_ctx);

	/* Deinitialize runtime lock */
	qdf_runtime_lock_deinit(&dal_ctx->mode_switch_runtime_lock);

	qdf_spinlock_destroy(&dal_ctx->dal_rx_desc_lock);
	qdf_spinlock_destroy(&dal_ctx->dal_tx_cpl_lock);
	qdf_spinlock_destroy(&dal_ctx->dal_replenish_lock);

	dp_dal_bus_stop(soc);
	dp_dal_bus_exit(soc);
	qdf_timer_free(&dal_ctx->dal_poll_timer);
	qdf_timer_free(&dal_ctx->rx_replenish_retry_timer);
}

/**
 * dp_dal_soc_attach - Attach DP DAL to SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure.
 */
QDF_STATUS dp_dal_soc_attach(struct dp_soc *soc)
{
	struct dp_dal_ctx *ctx;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_info("DAL feature disabled, skipping soc attach");
		return QDF_STATUS_SUCCESS;
	}

	ctx = qdf_mem_common_alloc(sizeof(*ctx));
	if (!ctx) {
		dp_init_err("Failed to allocate memory for DAL context");
		return QDF_STATUS_E_FAILURE;
	}

	dp_info("DAL context allocated");

	ctx->soc = soc;
	soc->dal_ctx = ctx;

	if (dp_dal_sim_attach(ctx)) {
		qdf_mem_common_free(ctx);
		soc->dal_ctx = NULL;
		dp_init_err("DP DAL sim attach failed");
		return QDF_STATUS_E_FAILURE;
	}

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS
dp_dal_update_ring_grp_id(struct dp_soc *soc,
			  struct dal_srng *dal_ring, enum hal_ring_type type)
{
	struct dp_intr *intr_ctx;
	uint8_t grp_id;
	uint8_t dal_tx_mask;
	uint8_t dal_rx_mask;
	uint8_t ring_idx;
	int i;

	for (i = 0; i < WLAN_CFG_INT_NUM_CONTEXTS; i++) {
		dal_tx_mask = soc->intr_ctx[i].dal_tx_ring_mask;
		dal_rx_mask = soc->intr_ctx[i].dal_rx_ring_mask;

		if (type == REO_DST && dal_rx_mask) {
			if (!(dal_rx_mask & (1 << dal_ring->ring_num)))
				continue;

			intr_ctx = &soc->intr_ctx[i];
			goto get_grp_id;
		}

		if (type == COMP_RING_TYPE && dal_tx_mask) {
			ring_idx = dal_ring->ring_num;

			if (!(1 << wlan_cfg_get_wbm_ring_num_for_index(soc->wlan_cfg_ctx,
								       ring_idx) &
			      dal_tx_mask))
				continue;

			intr_ctx = &soc->intr_ctx[i];
			goto get_grp_id;
		}
	}

	dp_err("Failed to get grp id for the DAL ring %d type %d",
	       dal_ring->ring_num, type);

	return QDF_STATUS_E_FAILURE;

get_grp_id:
	grp_id = hif_get_ext_grp_id(soc->hif_handle, intr_ctx);
	if (grp_id >= HIF_MAX_GROUP) {
		dp_err("failed to get grp id for the DAL ring %d type %d",
		       dal_ring->ring_num, type);
		return QDF_STATUS_E_FAILURE;
	}

	dal_ring->grp_id = grp_id;

	return QDF_STATUS_SUCCESS;
}

static QDF_STATUS dp_dal_create_ring_to_grp_mapping(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;
	struct dal_srng *dal_ring;
	int i;

	for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
		dal_ring = &dal_ctx->rx_ring[i];

		if (!dal_ring->initialized)
			continue;

		if (dp_dal_update_ring_grp_id(soc, dal_ring, REO_DST) !=
		    QDF_STATUS_SUCCESS) {
			dp_err("Failed to update grp_id for RX ring %d", i);
			return QDF_STATUS_E_FAILURE;
		}
	}

	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		dal_ring = &dal_ctx->tx_cmpl_ring[i];

		if (!dal_ring->initialized)
			continue;

		if (dp_dal_update_ring_grp_id(soc, dal_ring, COMP_RING_TYPE) !=
		    QDF_STATUS_SUCCESS) {
			dp_err("Failed to update grp_id for Tx cmp ring %d", i);
			return QDF_STATUS_E_FAILURE;
		}
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_attach_rx_buffers - attach rx buffers to RXDMA_BUF ring
 * @soc: pointer to dp_soc structure
 *
 * Return: 0 on success
 */
static int dp_dal_attach_rx_buffers(struct dp_soc *soc)
{
	struct rx_desc_pool *rx_desc_pool;
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return -EINVAL;
	}

	rx_desc_pool = &soc->rx_desc_buf[0];

	if (global_plat_ops && global_plat_ops->rx_replenish) {
		return global_plat_ops->rx_replenish(dal_ctx,
						     rx_desc_pool->pool_size,
						     false);
	} else {
		dp_err("rx_replenish plat op is not registered");
		QDF_BUG(0);
	}

	return -EOPNOTSUPP;
}

/**
 * dp_dal_enable_threaded_napi() - Enable threaded NAPI for DAL rings
 * @dal_ctx: DAL context pointer
 *
 * This function enables threaded NAPI for all DAL RX and TX completion rings.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure
 */
static QDF_STATUS dp_dal_enable_threaded_napi(struct dp_dal_ctx *dal_ctx)
{
	int i;
	QDF_STATUS status;

	if (!dal_ctx || !dal_ctx->soc) {
		dp_err("Invalid DAL context or SoC");
		return QDF_STATUS_E_INVAL;
	}

	/* Enable threaded NAPI for all DAL RX rings */
	for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
		if (!dal_ctx->rx_ring[i].initialized)
			continue;

		status = hif_exec_set_threaded_napi(dal_ctx->soc->hif_handle,
						    dal_ctx->rx_ring[i].grp_id,
						    true);
		if (QDF_IS_STATUS_ERROR(status)) {
			dp_err("Failed to enable threaded NAPI for RX ring %d",
			       i);
			return QDF_STATUS_E_FAILURE;
		}

		dp_info("Enabled threaded NAPI for RX ring %d", i);
	}

	/* Enable threaded NAPI for all DAL TX completion rings */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		if (!dal_ctx->tx_cmpl_ring[i].initialized)
			continue;

		status = hif_exec_set_threaded_napi(dal_ctx->soc->hif_handle,
						    dal_ctx->tx_cmpl_ring[i].grp_id,
						    true);
		if (QDF_IS_STATUS_ERROR(status)) {
			dp_err("Failed to enable threaded NAPI for TX comp ring %d", i);
			return QDF_STATUS_E_FAILURE;
		}

		dp_info("Enabled threaded NAPI for TX comp ring %d", i);
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_get_intr_ctx_from_ring() - get interrupt context from ring
 * @soc: DP soc handle
 * @ring_num: ring number
 * @ring_type: ring type
 *
 * Return: dp_intr context
 */
static struct dp_intr*
dp_dal_get_intr_ctx_from_ring(struct dp_soc *soc,
			      int ring_num,
			      enum hal_ring_type ring_type)
{
	struct dp_intr *intr_ctx;
	int i;

	for (i = 0; i < WLAN_CFG_INT_NUM_CONTEXTS; i++) {
		intr_ctx = &soc->intr_ctx[i];
		if (ring_type == REO_DST &&
		    (intr_ctx->dal_rx_ring_mask & (1 << ring_num)))
			return intr_ctx;
		else if (ring_type == COMP_RING_TYPE &&
			 (intr_ctx->dal_tx_ring_mask & (1 << ring_num)))
			return intr_ctx;
	}

	return NULL;
}

static void dp_dal_rx_replenish_retry_handler(void *arg)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)arg;
	struct dp_soc *soc;
	uint32_t failures;
	int ret;

	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC context is NULL");
		return;
	}

	if (!global_plat_ops || !global_plat_ops->rx_replenish) {
		dp_err("DAL: rx_replenish op not available");
		return;
	}

	if (qdf_atomic_read(&dal_ctx->deinit_in_progress))
		return;

	failures = qdf_atomic_read(&dal_ctx->rx_replenish_failures);
	if (!failures) {
		dal_ctx->rx_replenish_retry_count = 0;
		dal_ctx->rx_replenish_retry_interval_ms =
					DAL_RX_REPLENISH_RETRY_TIMER_MS;
		return;
	}

	if (global_plat_ops->rx_replenish(dal_ctx, failures, false)) {
		/* replenish via bypass path if mode switch in progress */
		if (soc->dal_mode_switch_in_progress) {
			ret = dp_dal_rx_replenish_bypass_mode(
						dal_ctx, failures, false);
			if (!ret) {
				qdf_atomic_sub(failures,
					       &dal_ctx->rx_replenish_failures);
				dal_ctx->rx_replenish_retry_count = 0;
				dal_ctx->rx_replenish_retry_interval_ms =
						DAL_RX_REPLENISH_RETRY_TIMER_MS;
			}

		} else {
			dp_err("DAL: rx_replenish failed in retry, failures:%u",
			       failures);
			dal_ctx->rx_replenish_retry_count++;
			dal_ctx->rx_replenish_retry_interval_ms *=
					DAL_RX_REPLENISH_BACKOFF_MULTIPLIER;
		}
	} else {
		qdf_atomic_sub(failures, &dal_ctx->rx_replenish_failures);
		dal_ctx->rx_replenish_retry_count = 0;
		dal_ctx->rx_replenish_retry_interval_ms =
					DAL_RX_REPLENISH_RETRY_TIMER_MS;
	}

	/* start timer again if replenish_failure count is non-zero */
	if (!qdf_atomic_read(&dal_ctx->deinit_in_progress) &&
	    qdf_atomic_read(&dal_ctx->rx_replenish_failures))
		qdf_timer_mod(&dal_ctx->rx_replenish_retry_timer,
			      dal_ctx->rx_replenish_retry_interval_ms);
}

/**
 * dp_dal_poll_timer_handler() - Timer handler to poll DAL owned rings
 * @arg: pointer to DAL context
 *
 * This timer handler iterates over DAL owned rings and processes pending
 * entries during mode switch from offload to bypass.
 */
static void dp_dal_poll_timer_handler(void *arg)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)arg;
	struct dp_soc *soc = dal_ctx->soc;
	struct dp_intr *intr_ctx;
	uint32_t hp, tp;
	int i;
	bool poll_again = false;

	/* Process DAL owned REO destination rings */
	for (i = 0; i < soc->num_reo_dest_rings; i++) {
		if (dp_srng_check_dal_owned_ring(&soc->reo_dest_ring[i])) {
			intr_ctx = dp_dal_get_intr_ctx_from_ring(soc, i,
								 REO_DST);
			if (intr_ctx) {
				soc->arch_ops.dp_rx_process(
						intr_ctx,
						soc->reo_dest_ring[i].hal_srng,
						i, DAL_RX_POLL_BUDGET);
				hal_get_sw_hptp(soc->hal_soc,
						soc->reo_dest_ring[i].hal_srng,
						&tp, &hp);
				if (tp != hp)
					poll_again = true;
			}
		}
	}

	/* Process DAL owned TX completion rings */
	for (i = 0; i < soc->num_tx_comp_rings; i++) {
		if (dp_srng_check_dal_owned_ring(&soc->tx_comp_ring[i])) {
			intr_ctx =
			dp_dal_get_intr_ctx_from_ring(soc, i, COMP_RING_TYPE);

			if (intr_ctx) {
				dp_tx_comp_handler(
					intr_ctx, soc,
					soc->tx_comp_ring[i].hal_srng,
					i, DAL_TX_POLL_BUDGET);
				hal_get_sw_hptp(soc->hal_soc,
						soc->tx_comp_ring[i].hal_srng,
						&tp, &hp);
				if (tp != hp)
					poll_again = true;
			}
		}
	}

	dal_ctx->poll_count++;

	/*
	 * Reschedule the timer if poll_again is true and the poll count
	 * is less than DAL_POLL_TIMER_MAX_COUNT.
	 */
	if (poll_again &&
	    dal_ctx->poll_count < DAL_POLL_TIMER_MAX_COUNT)
		qdf_timer_mod(&dal_ctx->dal_poll_timer,
			      DAL_POLL_TIMER_INTERVAL_MS);
}

/**
 * dp_dal_soc_init - Initialize DP DAL for SOC
 * @soc: pointer to dp_soc structure
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure.
 */
QDF_STATUS dp_dal_soc_init(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx;
	QDF_STATUS status;

	if (!soc)
		return QDF_STATUS_E_INVAL;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping soc init");
		return QDF_STATUS_SUCCESS;
	}

	if (!soc->dal_ctx)
		return QDF_STATUS_E_INVAL;

	dal_ctx = soc->dal_ctx;

	qdf_spinlock_create(&dal_ctx->dal_tx_cpl_lock);
	qdf_spinlock_create(&dal_ctx->dal_rx_desc_lock);
	qdf_spinlock_create(&dal_ctx->dal_replenish_lock);
	qdf_atomic_init(&dal_ctx->rx_replenish_failures);
	qdf_atomic_init(&dal_ctx->deinit_in_progress);
	qdf_atomic_init(&dal_ctx->bm_replenish_not_allowed);

	/* Initialize runtime lock for mode switch operations */
	qdf_runtime_lock_init(&dal_ctx->mode_switch_runtime_lock);

	dal_ctx->rx_replenish_retry_interval_ms =
					DAL_RX_REPLENISH_RETRY_TIMER_MS;

	dp_dal_init_suspended_tx_descs(dal_ctx);

	qdf_timer_init(soc->osdev, &dal_ctx->dal_poll_timer,
		       dp_dal_poll_timer_handler, dal_ctx,
		       QDF_TIMER_TYPE_WAKE_APPS);
	qdf_timer_init(soc->osdev, &dal_ctx->rx_replenish_retry_timer,
		       dp_dal_rx_replenish_retry_handler, dal_ctx,
		       QDF_TIMER_TYPE_WAKE_APPS);

	status = dp_dal_create_ring_to_grp_mapping(soc);
	if (status != QDF_STATUS_SUCCESS) {
		dp_err("failed to create DAL ring to grp mapping %d", status);
		goto destroy_lock;
	}

	status = dp_dal_enable_threaded_napi(soc->dal_ctx);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("failed to create napi thread for dal %d", status);
		return status;
	}

	status = dp_dal_bus_init(soc);
	if (status) {
		dp_err("DAL platform bus init failed %d", status);
		goto destroy_lock;
	}

	status = dp_dal_bus_request_irq(soc);
	if (status) {
		dp_err("DAL platform bus request IRQ failed %d", status);
		goto bus_deinit;
	}

	status = dp_dal_d3_wow_htt_send(soc);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("Failed to send DAL mode info to FW");
		goto bus_deinit;
	}

	status = dp_dal_attach_rx_buffers(soc);
	if (status) {
		dp_err("DAL rx buffer attach failed %d", status);
		goto bus_deinit;
	}

	status = dp_dal_bus_start(soc);
	if (status) {
		dp_err("DAL platform bus start failed %d", status);
		goto bus_deinit;
	}

	dp_dal_bus_vote_link_up(soc);

	dp_info("DAL SOC init completed successfully");

	return QDF_STATUS_SUCCESS;

bus_deinit:
	dp_info("DAL SOC init failed");
	dp_dal_bus_exit(soc);
destroy_lock:
	qdf_spinlock_destroy(&dal_ctx->dal_replenish_lock);
	qdf_spinlock_destroy(&dal_ctx->dal_rx_desc_lock);
	qdf_spinlock_destroy(&dal_ctx->dal_tx_cpl_lock);
	return status;
}

/**
 * dp_dal_rx_buffers_replenish() - RX buffer enqueue function used from
 * non-DAL path
 * @soc: pointer to DP SoC
 * @mac_id: mac id
 * @dp_rxdma_srng: dp rxdma circular ring
 * @rx_desc_pool: pointer to rx desc pool
 * @num_req_buffers: Number of Rx buffers to replenish
 * @desc_list: HEAD pointer to rx desc list elem list
 * @tail: TAIL pointer to rx desc list elem list
 * @req_only: If true don't replenish more than req buffers
 *
 * Invoked from a non-DAL path, such as the non-DAL REO DEST ring process, the
 * Rx error path replenishes buffers for processed descriptors. Since the OE
 * manages the rx buffer refill ring, all rx buffer replenishments must be
 * performed through the OE.
 *
 * Return: int
 */
int dp_dal_rx_buffers_replenish(struct dp_soc *soc, uint32_t mac_id,
				struct dp_srng *dp_rxdma_srng,
				struct rx_desc_pool *rx_desc_pool,
				uint32_t num_req_buffers,
				union dp_rx_desc_list_elem_t **desc_list,
				union dp_rx_desc_list_elem_t **tail,
				bool req_only)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;
	int ret;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx))
		return __dp_rx_buffers_replenish(soc, mac_id, dp_rxdma_srng,
						 rx_desc_pool, num_req_buffers,
						 desc_list, tail, req_only,
						 false, __func__);
	if (!dal_ctx)
		return -EINVAL;

	if (desc_list && *desc_list)
		dp_rx_add_desc_list_to_free_list(soc, desc_list, tail,
						 mac_id, rx_desc_pool);

	if (!global_plat_ops || !global_plat_ops->rx_replenish) {
		dp_err("DAL: no op registers for rx_replenish req_buf:%u",
		       num_req_buffers);
		return -EINVAL;
	}

	ret = global_plat_ops->rx_replenish(dal_ctx, num_req_buffers, false);
	if (ret) {
		if (soc->dal_mode_switch_in_progress)
			dp_dal_rx_replenish_bypass_mode(dal_ctx,
							num_req_buffers, false);
		else
			qdf_atomic_add(num_req_buffers,
				       &dal_ctx->rx_replenish_failures);
	}

	return ret;
}

static enum dal_intf_type
qdf_opmode_to_dal_intf_type(enum QDF_OPMODE mode)
{
	switch (mode) {
	case QDF_STA_MODE:
		return DAL_INTF_TYPE_STA;
	case QDF_SAP_MODE:
		return DAL_INTF_TYPE_SAP;
	default:
		return DAL_INTF_TYPE_MAX;
	}
}

/**
 * dp_dal_interface_add() - DAL interface add
 * @soc: pointer to DP SoC
 * @vdev: DP vdev structure
 *
 * Called during dp_vdev_attach_wifi3(), this function will add interface
 * details to offload engine.
 *
 * Return: int
 */
int dp_dal_interface_add(struct dp_soc *soc, struct dp_vdev *vdev)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;
	struct dal_intf_info intf_info = {0};
	enum dal_intf_type type;
	int status;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping interface add");
		return 0;
	}

	if (!dal_ctx) {
		dp_err("DAL context is NULL, cannot add interface");
		return QDF_STATUS_E_FAILURE;
	}

	type = qdf_opmode_to_dal_intf_type(vdev->qdf_opmode);
	if (type >= DAL_INTF_TYPE_MAX)
		return 0;

	intf_info.type = type;
	intf_info.vdev_id = vdev->vdev_id;
	intf_info.tcl_bank_id = vdev->bank_id;

	if (type == DAL_INTF_TYPE_STA)
		intf_info.tx_ring_id = DAL_TX_RING_ID0_STA;
	else if (type == DAL_INTF_TYPE_SAP)
		intf_info.tx_ring_id = DAL_TX_RING_ID0_SAP;
	intf_info.tx_rbm_id =
		wlan_cfg_get_rbm_id_for_index(soc->wlan_cfg_ctx,
					      intf_info.tx_ring_id);

	qdf_mem_copy(&intf_info.mac_address[0],
		     &vdev->mac_addr.raw[0], QDF_MAC_ADDR_SIZE);

	if (global_plat_ops && global_plat_ops->intf_init) {
		status = global_plat_ops->intf_init(dal_ctx,
						    &intf_info);
		if (status) {
			dp_err("dal interface add failed vdev_id:%d status %d",
			       vdev->vdev_id, status);
			return status;
		}
	}

	if (global_plat_ops && global_plat_ops->tx_queue_active) {
		status = global_plat_ops->tx_queue_active(dal_ctx,
							  vdev->vdev_id, true);
		if (status) {
			dp_err("dal tx queue active failed vdev_id:%d status %d",
			       vdev->vdev_id, status);
			/* Cleanup the interface that was just initialized */
			if (global_plat_ops->intf_deinit)
				global_plat_ops->intf_deinit(dal_ctx,
							     vdev->vdev_id);
			return status;
		}
	}

	return 0;
}

/**
 * dp_dal_interface_remove() - DAL interface remove
 * @soc: pointer to DP SoC
 * @vdev: DP vdev structure
 *
 * Called during dp_vdev_detach_wifi3(), this function will remove interface
 * details from the offload engine.
 *
 * Return: None
 */
void dp_dal_interface_remove(struct dp_soc *soc, struct dp_vdev *vdev)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;
	int status;
	uint8_t vdev_id = vdev->vdev_id;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping interface remove");
		return;
	}

	if (!dal_ctx) {
		dp_err("DAL context is NULL, cannot remove interface");
		return;
	}

	if (vdev->qdf_opmode != QDF_STA_MODE &&
	    vdev->qdf_opmode != QDF_SAP_MODE)
		return;

	if (global_plat_ops && global_plat_ops->tx_queue_active) {
		status = global_plat_ops->tx_queue_active(dal_ctx,
							  vdev_id, false);
		if (status)
			dp_err("dal txq deactivate failed vdev_id:%d status %d",
			       vdev_id, status);
		/* Continue to intf_deinit despite error */
	}

	if (global_plat_ops && global_plat_ops->intf_deinit) {
		status = global_plat_ops->intf_deinit(dal_ctx, vdev_id);
		if (status)
			dp_err("dal intf remove failed vdev_id:%d status %d",
			       vdev_id, status);
	}
}

/**
 * dp_dal_sta_active() - DAL API to send STA information
 * @soc: pointer to DP SoC
 * @info: station information
 * @enable: 0: disconnect, 1: connect
 *
 * Called during STA connect/disconnect, this function will share station
 * information to the offload engine.
 *
 * Return: int
 */
static int dp_dal_sta_active(struct dp_soc *soc, struct sta_info *info,
			     bool enable)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;

	if (!dal_ctx || !info)
		return -EINVAL;

	if (global_plat_ops && global_plat_ops->sta_active)
		return global_plat_ops->sta_active(dal_ctx, info, enable);

	return 0;
}

/**
 * dp_dal_notify_sta_active() - Notify DAL about STA/SAP active state
 * @soc: pointer to DP SoC
 * @peer: pointer to DP peer
 * @peer_mac: peer MAC address
 *
 * This function notifies DAL about STA connect/disconnect events for both
 * STA and AP modes. It is called during peer state transitions.
 *
 * Return: None
 */
void dp_dal_notify_sta_active(struct dp_soc *soc,
			      struct dp_peer *peer,
			      uint8_t *peer_mac)
{
	struct sta_info info = {0};
	bool enable;
	enum ol_txrx_peer_state peer_state;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx))
		return;

	/* Call DAL STA active for STA mode during connect/disconnect */
	if (!peer->vdev)
		return;

	if (peer->vdev->opmode != wlan_op_mode_sta &&
	    peer->vdev->opmode != wlan_op_mode_ap)
		return;

	qdf_spin_lock_bh(&peer->peer_info_lock);
	peer_state = peer->state;
	qdf_spin_unlock_bh(&peer->peer_info_lock);

	/* Enable for AUTH state (connect), disable for other states */
	enable = (peer_state == OL_TXRX_PEER_STATE_AUTH) ? true : false;

	info.bss_idx = peer->vdev->vdev_id;
	qdf_mem_copy(info.addr, peer_mac, MAC_ADDR_LEN);

	if (peer_state == OL_TXRX_PEER_STATE_DISC ||
	    peer_state == OL_TXRX_PEER_STATE_AUTH)
		dp_dal_sta_active(soc, &info, enable);
}

/**
 * dp_dal_notify_suspend() - DAL wrapper for platform notify suspend
 * @soc: pointer to DP SoC
 * @intf_pause: Interface pause flag
 *
 * This function calls the global platform ops notify_suspend function.
 * When this returns successfully, it means there are no pending transactions
 * from the DAL and the device can suspend.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_FAILURE on failure
 */
QDF_STATUS dp_dal_notify_suspend(struct dp_soc *soc, bool intf_pause)
{
	struct dp_dal_ctx *dal_ctx;
	QDF_STATUS status;
	int ret = -EOPNOTSUPP;

	if (!soc) {
		dp_err("Invalid SoC pointer");
		return QDF_STATUS_E_INVAL;
	}

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping notify suspend");
		return QDF_STATUS_SUCCESS;
	}

	dal_ctx = soc->dal_ctx;
	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	/* Reject suspend when mode switch is in progress */
	if (soc->dal_mode_switch_in_progress) {
		dp_warn("Mode switch in progress, reject suspend");
		return QDF_STATUS_E_FAILURE;
	}

	/* If HTT message was not sent during init (due to late WMI capability
	 * arrival), send it now during first suspend
	 */
	status = dp_dal_d3_wow_htt_send_on_suspend(soc);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("Failed to send DAL mode info to FW during suspend");
		return QDF_STATUS_E_FAILURE;
	}

	if (global_plat_ops && global_plat_ops->notify_suspend)
		ret = global_plat_ops->notify_suspend(dal_ctx, intf_pause);

	if (ret) {
		dp_err_rl("Suspend notify to DAL failed %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	/* Wait for pending tasks to complete */
	status = hif_try_complete_dp_tasks(soc->hif_handle);
	if (QDF_IS_STATUS_ERROR(status)) {
		dp_err("Failed to complete DP tasks");
		return status;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_notify_resume() - DAL wrapper for platform notify resume
 * @soc: pointer to DP SoC
 *
 * This function calls the global platform ops notify_resume function.
 * This is called when the device is resuming from suspend state.
 * It also flushes any suspended TX descriptors.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_FAILURE on failure
 */
QDF_STATUS dp_dal_notify_resume(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx;
	int ret = -EOPNOTSUPP;

	if (!soc) {
		dp_err("Invalid SoC pointer");
		return QDF_STATUS_E_INVAL;
	}

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping notify resume");
		return QDF_STATUS_SUCCESS;
	}

	dal_ctx = soc->dal_ctx;
	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return QDF_STATUS_E_INVAL;
	}

	/* If mode switch is in progress, use ops based on current mode */
	if (soc->dal_mode_switch_in_progress) {
		dp_info("Mode switch in progress, using ops for current mode: %d",
			soc->dp_dal_mode);
		if (soc->dp_dal_mode == DAL_DP_BYPASS_MODE) {
			/* Use bypass ops */
				ret = plat_ops_bypass_mode.notify_resume(dal_ctx);
		} else if (soc->dp_dal_mode == DAL_DP_OFFLOAD_MODE) {
			/* Use saved offload ops */
				ret = dal_ctx->offload_plat_ops->notify_resume(dal_ctx);
		}
	} else {
		if (global_plat_ops && global_plat_ops->notify_resume)
			ret = global_plat_ops->notify_resume(dal_ctx);
	}

	if (ret) {
		dp_err_rl("Resume notify to DAL failed %d", ret);
		return QDF_STATUS_E_FAILURE;
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_ssr_notify() - DAL wrapper for platform SSR notify
 * @soc: pointer to DP SoC
 *
 * This function calls the global platform ops ssr_dump function.
 * This is called to notify the DAL about SSR event.
 *
 * Return: None
 */
void dp_dal_ssr_notify(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx;

	if (!soc) {
		dp_err("Invalid SoC pointer");
		return;
	}

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping ssr notify");
		return;
	}

	dal_ctx = soc->dal_ctx;
	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return;
	}

	if (global_plat_ops && global_plat_ops->ssr_dump) {
		dp_info("Notifying DAL about SSR event");
		/* TODO: Implement proper segment data collection */
		global_plat_ops->ssr_dump(NULL);
	} else {
		dp_debug("ssr_dump() not supported by platform ops");
	}
}

#ifdef FEATURE_DP_DAL_SIM
static void
dp_dal_update_ring_hp_tp_addr(struct dp_soc *soc, struct hal_srng *srng,
			      struct dal_srng *dal_ring)
{
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;

	if (srng->ring_dir == HAL_SRNG_SRC_RING) {
		dal_ring->u.src_ring.hp = srng->u.src_ring.hp;

		if (dal_ring->lmac_ring) {
			dal_ring->u.src_ring.hp_addr =
				(uint64_t)(srng->u.src_ring.hp_addr);
		} else {
			dal_ring->u.src_ring.hp_addr =
				(void *)srng->u.src_ring.hp_addr -
				hal->dev_base_addr;
		}

		dal_ring->u.src_ring.tp_addr =
			(uint64_t)(srng->u.src_ring.tp_addr);
	} else {
		dal_ring->u.dst_ring.tp = srng->u.dst_ring.tp;

		if (dal_ring->lmac_ring) {
			dal_ring->u.dst_ring.tp_addr =
				(uint64_t)(srng->u.src_ring.tp_addr);
		} else {
			dal_ring->u.dst_ring.tp_addr =
				(void *)srng->u.dst_ring.tp_addr -
				hal->dev_base_addr;
		}
		dal_ring->u.dst_ring.hp_addr =
			(uint64_t)(srng->u.dst_ring.hp_addr);
	}
}
#else
static void
dp_dal_update_ring_hp_tp_addr(struct dp_soc *soc, struct hal_srng *srng,
			      struct dal_srng *dal_ring)
{
	struct hal_soc *hal = (struct hal_soc *)soc->hal_soc;

	if (srng->ring_dir == HAL_SRNG_SRC_RING) {
		dal_ring->u.src_ring.hp = srng->u.src_ring.hp;

		if (dal_ring->lmac_ring)
			dal_ring->u.src_ring.hp_addr =
				hal_srng_get_hp_addr(soc->hal_soc,
						     (hal_ring_handle_t)srng);
		else
			dal_ring->u.src_ring.hp_addr =
				srng->u.src_ring.hp_addr -
				(uint32_t *)(hal->dev_base_addr);

		dal_ring->u.src_ring.tp_addr =
				(uint64_t)(hal->shadow_rdptr_mem_paddr +
				((unsigned long)(srng->u.src_ring.tp_addr) -
				 (unsigned long)(hal->shadow_rdptr_mem_vaddr)));

	} else {
		dal_ring->u.dst_ring.tp = srng->u.dst_ring.tp;

		if (dal_ring->lmac_ring)
			dal_ring->u.dst_ring.tp_addr =
				hal_srng_get_tp_addr(soc->hal_soc,
						     (hal_ring_handle_t)srng);
		else
			dal_ring->u.dst_ring.tp_addr =
				srng->u.dst_ring.tp_addr -
				(uint32_t *)(hal->dev_base_addr);

		dal_ring->u.dst_ring.hp_addr =
				(uint64_t)(hal->shadow_rdptr_mem_paddr +
				((unsigned long)(srng->u.dst_ring.hp_addr) -
				 (unsigned long)(hal->shadow_rdptr_mem_vaddr)));
	}
}
#endif

static void
dp_dal_update_ring_params(struct dp_soc *soc, struct hal_srng *srng,
			  struct dal_srng *dal_ring)
{
	dal_ring->hal_ring_id = srng->ring_id;
	dal_ring->ring_base_paddr = srng->ring_base_paddr;
	dal_ring->ring_base_vaddr = srng->ring_base_vaddr;
	dal_ring->num_entries = srng->num_entries;
	dal_ring->ring_size = srng->ring_size;
	dal_ring->ring_size_mask = srng->ring_size_mask;
	dal_ring->entry_size = srng->entry_size;
	dal_ring->ring_type = srng->ring_type;
	dal_ring->ring_dir = srng->ring_dir;
	dal_ring->lmac_ring = srng->flags & HAL_SRNG_LMAC_RING ? true : false;

	dp_dal_update_ring_hp_tp_addr(soc, srng, dal_ring);
}

void dp_dal_save_srng_info(struct dp_soc *soc, struct dp_srng *srng,
			   enum hal_ring_type type, int ring_num)
{
	struct dp_dal_ctx *dal_ctx = soc->dal_ctx;
	struct dal_srng *dal_ring;
	int ring_info_cnt;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, skipping save srng info");
		return;
	}

	if (type != RXDMA_BUF &&
	    !dp_srng_check_dal_owned_ring(srng))
		return;

	switch (type) {
	case REO_DST:
		ring_info_cnt = dal_ctx->num_rx_ring_info;
		if (ring_info_cnt >= DAL_RX_RINGS_MAX) {
			dp_err("Max rx ring info limit:%d reached",
			       ring_info_cnt);
			return;
		}

		dal_ring = &dal_ctx->rx_ring[ring_info_cnt];
		dp_dal_update_ring_params(soc,
					  (struct hal_srng *)srng->hal_srng,
					  dal_ring);
		dal_ring->ring_num = ring_num;
		dal_ring->initialized = true;
		dal_ctx->num_rx_ring_info++;
		break;
	case TCL_DATA:
		ring_info_cnt = dal_ctx->num_tx_ring_info;
		if (ring_info_cnt >= DAL_TX_RINGS_MAX) {
			dp_err("Max tx ring info limit:%d reached",
			       ring_info_cnt);
			return;
		}

		dal_ring = &dal_ctx->tx_ring[ring_info_cnt];
		dp_dal_update_ring_params(soc,
					  (struct hal_srng *)srng->hal_srng,
					  dal_ring);
		dal_ring->ring_num = ring_num;
		dal_ring->initialized = true;
		dal_ctx->num_tx_ring_info++;
		break;
	case COMP_RING_TYPE:
		ring_info_cnt = dal_ctx->num_tx_cmpl_ring_info;
		if (ring_info_cnt >= DAL_TX_RINGS_MAX) {
			dp_err("Max tx cmpl ring info limit:%d reached",
			       ring_info_cnt);
			return;
		}

		dal_ring = &dal_ctx->tx_cmpl_ring[ring_info_cnt];
		dp_dal_update_ring_params(soc,
					  (struct hal_srng *)srng->hal_srng,
					  dal_ring);
		dal_ring->ring_num = ring_num;
		dal_ring->initialized = true;
		dal_ctx->num_tx_cmpl_ring_info++;
		break;
	case RXDMA_BUF:
		dal_ring = &dal_ctx->rx_refill_ring;
		dp_dal_update_ring_params(soc,
					  (struct hal_srng *)srng->hal_srng,
					  dal_ring);
		dal_ring->initialized = true;
		break;
	default:
		dp_err("Invalid ring info rcvd srng %pK type %d ring_num %d",
		       srng, type, ring_num);
	}
}

int dp_dal_get_ext_grp_id(struct dp_dal_ctx *dal_ctx,
			  int ring_num, enum hal_ring_type type)
{
	struct dal_srng *dal_ring;
	int grp_id = 0xFF;
	int i;

	if (type == REO_DST) {
		for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
			dal_ring = &dal_ctx->rx_ring[i];
			if (dal_ring->ring_num == ring_num)
				return dal_ring->grp_id;
		}
	} else if (type == COMP_RING_TYPE) {
		for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
			dal_ring = &dal_ctx->tx_cmpl_ring[i];
			if (dal_ring->ring_num == ring_num)
				return dal_ring->grp_id;
		}
	} else {
		dp_err("invalid ring_type:%d received", type);
	}

	return grp_id;
}

uint32_t dp_service_dal_srngs(void *dp_ctx, uint32_t dp_budget, int cpu)
{
	struct dp_intr *int_ctx = (struct dp_intr *)dp_ctx;
	struct dp_soc *soc = int_ctx->soc;
	int dal_tx_mask = 0;
	int dal_rx_mask = 0;
	uint32_t work_done = 0;
	int budget = dp_budget;
	uint32_t remaining_quota = dp_budget;
	int i;

	dal_tx_mask = int_ctx->dal_tx_ring_mask;
	dal_rx_mask = int_ctx->dal_rx_ring_mask;

	if (dal_rx_mask) {
		for (i = 0; i < soc->num_reo_dest_rings; i++) {
			if (!(dal_rx_mask & (1 << i)))
				continue;

			work_done = dp_dal_rx_handler(soc, i, remaining_quota);
			if (work_done) {
				dp_verbose_debug("dal rx mask 0x%x ring %d, budget %d, work_done %d",
						 dal_rx_mask, i,
						 budget, work_done);
			}
			budget -= work_done;
			if (budget <= 0)
				goto budget_done;

			remaining_quota = budget;
		}
	}

	if (dal_tx_mask) {
		for (i = 0; i < soc->num_tx_comp_rings; i++) {
			if (!(1 << wlan_cfg_get_wbm_ring_num_for_index(soc->wlan_cfg_ctx, i) &
			      dal_tx_mask))
				continue;

			work_done = dp_dal_tx_comp_handler(soc, i,
							   remaining_quota);
			if (work_done) {
				dp_verbose_debug("dal tx mask 0x%x ring %d, budget %d, work_done %d",
						 dal_tx_mask, i,
						 budget, work_done);
			}
			budget -= work_done;
			if (budget <= 0)
				goto budget_done;

			remaining_quota = budget;
		}
	}

budget_done:
	return dp_budget - budget;
}

/**
 * dp_dal_flush_suspended_tx_descs() - Wrapper to flush suspended TX descs
 * @soc: pointer to DP SoC
 *
 * This function provides a wrapper around dp_dal_tx_flush_suspended_descs
 * that can be called from dp_main.c. It takes a dp_soc pointer and internally
 * calls the DAL TX function with the dal_ctx.
 *
 * Return: Number of descriptors flushed
 */
uint32_t dp_dal_flush_suspended_tx_descs(struct dp_soc *soc)
{
	struct dp_dal_ctx *dal_ctx;

	if (!wlan_cfg_is_dal_feature_enabled(soc->wlan_cfg_ctx)) {
		dp_debug("DAL feature disabled, no suspended TX descs to flush");
		return 0;
	}

	dal_ctx = soc->dal_ctx;
	if (!dal_ctx)
		return 0;

	return dp_dal_tx_flush_suspended_descs(dal_ctx);
}
