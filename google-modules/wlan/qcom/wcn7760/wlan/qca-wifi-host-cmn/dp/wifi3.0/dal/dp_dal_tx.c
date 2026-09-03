/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "dp_dal_tx.h"
#include "hal_tx.h"
#include "qdf_mem.h"
#include "qdf_platform.h"

extern struct platform_bus_ops *global_plat_ops;

#define DP_DAL_SPECIAL_FRAME_MASK (FRAME_MASK_IPV4_ARP | \
				   FRAME_MASK_IPV4_DHCP | \
				   FRAME_MASK_IPV4_EAPOL | \
				   FRAME_MASK_IPV6_DHCP)

#if defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE)
/**
 * dp_dal_tx_queue_suspended_desc() - Queue TX descriptor during suspend
 * @dal_ctx: DAL context
 * @ring_id: Ring ID
 * @vdev_id: VDEV ID
 * @tcl_desc: TCL descriptor pointer (ownership transferred)
 * @tx_metadata: TX metadata (contains stack pointers, create heap copies)
 *
 * Queue TX descriptors when system is in suspend state for processing
 * during resume. Creates heap-allocated copies of stack-allocated structures
 * to preserve TX metadata across suspend/resume cycles.
 *
 * Return: 0 : Success, -ve : Failure
 */
static int
dp_dal_tx_queue_suspended_desc(struct dp_dal_ctx *dal_ctx,
			       uint8_t ring_id, uint32_t vdev_id,
			       void *tcl_desc,
			       struct dp_dal_tx_metadata *tx_metadata)
{
	struct dp_dal_suspended_tx_desc *suspended_desc;
	struct dp_tx_msdu_info_s *msdu_info_copy;

	if (qdf_unlikely(!dal_ctx || !tcl_desc || !tx_metadata ||
			 !tx_metadata->tx_desc || !tx_metadata->msdu_info)) {
		dp_tx_err_rl("Invalid parameters for suspended TX desc");
		return -EINVAL;
	}

	suspended_desc = qdf_mem_malloc(sizeof(*suspended_desc));
	if (!suspended_desc) {
		dp_tx_err_rl("Failed to allocate suspended TX descriptor");
		return -ENOMEM;
	}

	msdu_info_copy = qdf_mem_malloc(sizeof(*msdu_info_copy));
	if (!msdu_info_copy) {
		dp_tx_err_rl("Failed to allocate msdu_info copy");
		qdf_mem_free(suspended_desc);
		return -ENOMEM;
	}

	qdf_mem_copy(msdu_info_copy, tx_metadata->msdu_info,
		     sizeof(*msdu_info_copy));

	suspended_desc->ring_id = ring_id;
	suspended_desc->vdev_id = vdev_id;
	suspended_desc->tcl_desc = tcl_desc;
	suspended_desc->tx_desc = tx_metadata->tx_desc;
	suspended_desc->msdu_info = msdu_info_copy;

	qdf_spin_lock_bh(&dal_ctx->suspended_tx_lock);
	qdf_list_insert_back(&dal_ctx->suspended_tx_list,
			     &suspended_desc->node);
	dal_ctx->suspended_tx_count++;
	qdf_spin_unlock_bh(&dal_ctx->suspended_tx_lock);

	return 0;
}

/**
 * dp_dal_tx_flush_suspended_descs() - Flush suspended TX descriptors
 * @dal_ctx: DAL context
 *
 * Process all suspended TX descriptors during resume by calling platform TX
 * for each queued descriptor and updating statistics appropriately.
 *
 * Return: Number of descriptors processed
 */
uint32_t dp_dal_tx_flush_suspended_descs(struct dp_dal_ctx *dal_ctx)
{
	struct dp_dal_suspended_tx_desc *suspended_desc, *next_desc;
	struct dp_dal_tx_metadata tx_metadata = {};
	uint32_t processed_count = 0;
	uint8_t ring_id;
	int ret;

	if (qdf_unlikely(!dal_ctx))
		return 0;

	qdf_spin_lock_bh(&dal_ctx->suspended_tx_lock);

	if (qdf_atomic_read(&dal_ctx->deinit_in_progress)) {
		qdf_spin_unlock_bh(&dal_ctx->suspended_tx_lock);
		return 0;
	}

	qdf_list_for_each_del(&dal_ctx->suspended_tx_list, suspended_desc,
			      next_desc, node) {
		qdf_list_remove_node(&dal_ctx->suspended_tx_list,
				     &suspended_desc->node);
		dal_ctx->suspended_tx_count--;

		tx_metadata.tx_desc = suspended_desc->tx_desc;
		tx_metadata.msdu_info = suspended_desc->msdu_info;
		tx_metadata.vdev =
			dp_vdev_get_ref_by_id(dal_ctx->soc,
					      suspended_desc->vdev_id,
					      DP_MOD_ID_TX);

		if (qdf_likely(tx_metadata.vdev && global_plat_ops &&
			       global_plat_ops->tx)) {
			suspended_desc->tx_desc->flags |=
					DP_TX_DESC_FLAG_QUEUED_TX;

			ret = global_plat_ops->tx(dal_ctx,
						  suspended_desc->ring_id,
						  suspended_desc->vdev_id,
						  suspended_desc->tcl_desc,
						  &tx_metadata);

			/* DAL will return -EBUSY if mode switch is in progress,
			 * try to send via bypass mode tx path if it a special
			 * frame.
			 */
			if (ret == -EBUSY &&
			    dp_dal_tx_is_special_frame(suspended_desc->tx_desc->nbuf,
						       DP_DAL_SPECIAL_FRAME_MASK))
				ret = dp_dal_tx_bypass_mode(dal_ctx,
							    suspended_desc->ring_id,
							    suspended_desc->vdev_id,
							    suspended_desc->tcl_desc,
							    &tx_metadata);

			if (ret == 0) {
				ring_id = tx_metadata.msdu_info->tx_queue.ring_id;

				dp_tx_update_proto_stats(
						tx_metadata.vdev,
						suspended_desc->tx_desc->nbuf,
						ring_id, TX_ENQUEUE_HW);

				DP_STATS_INC_PKT(tx_metadata.vdev,
						 tx_i[tx_metadata.msdu_info->xmit_type].processed,
						 1, dp_tx_get_pkt_len(suspended_desc->tx_desc));
				DP_STATS_INC(dal_ctx->soc,
					     tx.tcl_enq[ring_id], 1);
				dp_pkt_add_timestamp(
						tx_metadata.vdev,
						QDF_PKT_TX_DRIVER_EXIT,
						qdf_get_log_timestamp(),
						suspended_desc->tx_desc->nbuf);
			} else {
				dp_tx_err_rl("Failed to flush suspended TX desc, ret: %d",
					     ret);
				DP_STATS_INC(tx_metadata.vdev,
					     tx_i[tx_metadata.msdu_info->xmit_type].dropped.enqueue_fail,
					     1);
				suspended_desc->tx_desc->flags &=
						~DP_TX_DESC_FLAG_QUEUED_TX;
			}

			dp_tx_hw_enqueue_post_process(dal_ctx->soc,
						      tx_metadata.vdev,
						      suspended_desc->tx_desc,
						      tx_metadata.msdu_info,
						      ret);
			dp_vdev_unref_delete(dal_ctx->soc, tx_metadata.vdev,
					     DP_MOD_ID_TX);
		} else {
			dp_tx_err_rl("VDEV/Platform ops is NULL during flush");
			dp_tx_comp_free_buf(dal_ctx->soc,
					    suspended_desc->tx_desc, false);
			dp_tx_desc_release(
				dal_ctx->soc, suspended_desc->tx_desc,
				tx_metadata.msdu_info->tx_queue.desc_pool_id);
		}

		qdf_mem_free(suspended_desc->msdu_info);
		qdf_mem_free(suspended_desc->tcl_desc);
		qdf_mem_free(suspended_desc);
		processed_count++;
	}

	qdf_spin_unlock_bh(&dal_ctx->suspended_tx_lock);

	if (processed_count > 0) {
		dp_debug("Flushed %u suspended TX descriptors during resume",
			 processed_count);
	}

	return processed_count;
}
#endif /* defined(FEATURE_RUNTIME_PM) || defined(DP_POWER_SAVE) */

#ifdef FEATURE_RUNTIME_PM
/**
 * dp_dal_tx_rtpm_wrapper() - Runtime PM aware platform TX wrapper
 * @dal_ctx: DAL context
 * @ring_id: Ring ID
 * @vdev_id: VDEV ID
 * @tcl_desc: TCL descriptor (ownership may be transferred)
 * @tx_metadata: TX metadata
 *
 * This function wraps the platform TX call with runtime PM handling
 * similar to dp_tx_ring_access_end_wrapper for bypass mode.
 *
 * Return: enum dp_dal_tx_status values
 */
static int dp_dal_tx_rtpm_wrapper(struct dp_dal_ctx *dal_ctx, uint8_t ring_id,
				  uint32_t vdev_id, void *tcl_desc,
				  struct dp_dal_tx_metadata *tx_metadata)
{
	struct dp_soc *soc = dal_ctx->soc;
	int ret;
	int platform_ret = 0;

	if (!global_plat_ops || !global_plat_ops->tx) {
		DP_STATS_INC_PKT(dal_ctx,
				 tx.offload[ring_id][DAL_TX_NOSUPPORT_DRP], 1,
				 dp_tx_get_pkt_len(tx_metadata->tx_desc));
		dp_tx_err_rl("Platform TX operation not available");
		return DP_DAL_TX_FAILURE;
	}

	if (dp_get_rtpm_tput_policy_requirement(soc)) {
		platform_ret = global_plat_ops->tx(dal_ctx, ring_id, vdev_id,
						   tcl_desc, tx_metadata);

		/* DAL will return -EBUSY if mode switch is in progress, try to
		 * send via bypass mode tx path if it a special frame.
		 */
		if (platform_ret == -EBUSY &&
		    dp_dal_tx_is_special_frame(tx_metadata->tx_desc->nbuf,
					       DP_DAL_SPECIAL_FRAME_MASK))
			platform_ret = dp_dal_tx_bypass_mode(dal_ctx, ring_id,
							     vdev_id, tcl_desc,
							     tx_metadata);

		return (platform_ret == 0) ? DP_DAL_TX_SUCCESS :
					      DP_DAL_TX_FAILURE;
	}

	ret = hif_rtpm_get(HIF_RTPM_GET_ASYNC, HIF_RTPM_ID_DP);
	if (QDF_IS_STATUS_SUCCESS(ret)) {
		if (hif_system_pm_state_check(soc->hif_handle)) {
			/* System is in suspend state. In offload mode,
			 * we need to defer the TX operation until resume.
			 * Queue the descriptor for later processing.
			 * Ownership of tcl_desc is transferred to suspended
			 * list.
			 */
			ret = dp_dal_tx_queue_suspended_desc(dal_ctx, ring_id,
							     vdev_id, tcl_desc,
							     tx_metadata);
			if (ret) {
				hif_rtpm_put(HIF_RTPM_PUT_ASYNC,
					     HIF_RTPM_ID_DP);
				return DP_DAL_TX_FAILURE;
			}

			platform_ret = DP_DAL_TX_QUEUED;
		} else {
			platform_ret = global_plat_ops->tx(dal_ctx, ring_id,
							   vdev_id, tcl_desc,
							   tx_metadata);
			/* DAL will return -EBUSY if mode switch is in progress,
			 * try to send via bypass mode tx path if it a special
			 * frame.
			 */
			if (platform_ret == -EBUSY &&
			    dp_dal_tx_is_special_frame(tx_metadata->tx_desc->nbuf,
						       DP_DAL_SPECIAL_FRAME_MASK))
				platform_ret =
					dp_dal_tx_bypass_mode(dal_ctx, ring_id,
							      vdev_id, tcl_desc,
							      tx_metadata);
			platform_ret = (platform_ret == 0) ?
					DP_DAL_TX_SUCCESS : DP_DAL_TX_FAILURE;
		}

		hif_rtpm_put(HIF_RTPM_PUT_ASYNC, HIF_RTPM_ID_DP);
	} else {
		/*
		 * Runtime PM get failed, system is likely suspending.
		 * Queue the descriptor for processing during resume.
		 * Ownership of tcl_desc is transferred to suspended list.
		 */
		dp_runtime_get(soc);
		ret = dp_dal_tx_queue_suspended_desc(dal_ctx, ring_id,
						     vdev_id, tcl_desc,
						     tx_metadata);
		if (ret) {
			dp_runtime_put(soc);
			return DP_DAL_TX_FAILURE;
		}

		qdf_atomic_inc(&soc->tx_pending_rtpm);
		dp_runtime_put(soc);
		platform_ret = DP_DAL_TX_QUEUED;
	}

	return platform_ret;
}
#elif defined(DP_POWER_SAVE)
/**
 * dp_dal_tx_rtpm_wrapper() - Direct platform TX wrapper (no runtime PM)
 * @dal_ctx: DAL context
 * @ring_id: Ring ID
 * @vdev_id: VDEV ID
 * @tcl_desc: TCL descriptor
 * @tx_metadata: TX metadata
 *
 * This function directly calls platform TX without runtime PM handling
 * when FEATURE_RUNTIME_PM is not enabled.
 *
 * Return: DP_DAL_TX_SUCCESS for success, DP_DAL_TX_FAILURE for failure
 */
static int dp_dal_tx_rtpm_wrapper(struct dp_dal_ctx *dal_ctx, uint8_t ring_id,
				  uint32_t vdev_id, void *tcl_desc,
				  struct dp_dal_tx_metadata *tx_metadata)
{
	struct dp_soc *soc = dal_ctx->soc;
	int ret;
	int platform_ret = 0;

	if (!global_plat_ops || !global_plat_ops->tx) {
		DP_STATS_INC_PKT(dal_ctx,
				 tx.offload[ring_id][DAL_TX_NOSUPPORT_DRP], 1,
				 dp_tx_get_pkt_len(tx_metadata->tx_desc));
		dp_tx_err_rl("Platform TX operation not available");
		return DP_DAL_TX_FAILURE;
	}

	if (hif_system_pm_state_check(soc->hif_handle)) {
		/* System is in suspend state. In offload mode,
		 * we need to defer the TX operation until resume.
		 * Queue the descriptor for later processing.
		 * Ownership of tcl_desc is transferred to suspended
		 * list.
		 */
		ret = dp_dal_tx_queue_suspended_desc(dal_ctx, ring_id,
						     vdev_id, tcl_desc,
						     tx_metadata);
		platform_ret = (ret == 0) ?
				DP_DAL_TX_QUEUED : DP_DAL_TX_FAILURE;
	} else {
		platform_ret = global_plat_ops->tx(dal_ctx, ring_id,
						   vdev_id, tcl_desc,
						   tx_metadata);
		/* DAL will return -EBUSY if mode switch is in progress,
		 * try to send via bypass mode tx path if it a special
		 * frame.
		 */
		if (platform_ret == -EBUSY &&
		    dp_dal_tx_is_special_frame(tx_metadata->tx_desc->nbuf,
					       DP_DAL_SPECIAL_FRAME_MASK))
			platform_ret = dp_dal_tx_bypass_mode(dal_ctx, ring_id,
							     vdev_id, tcl_desc,
							     tx_metadata);
		platform_ret = (platform_ret == 0) ?
				DP_DAL_TX_SUCCESS : DP_DAL_TX_FAILURE;
	}

	return platform_ret;
}
#else
/**
 * dp_dal_tx_rtpm_wrapper() - Direct platform TX wrapper (no runtime PM)
 * @dal_ctx: DAL context
 * @ring_id: Ring ID
 * @vdev_id: VDEV ID
 * @tcl_desc: TCL descriptor
 * @tx_metadata: TX metadata
 *
 * This function directly calls platform TX without runtime PM handling
 * when FEATURE_RUNTIME_PM is not enabled.
 *
 * Return: DP_DAL_TX_SUCCESS for success, DP_DAL_TX_FAILURE for failure
 */
static int dp_dal_tx_rtpm_wrapper(struct dp_dal_ctx *dal_ctx, uint8_t ring_id,
				  uint32_t vdev_id, void *tcl_desc,
				  struct dp_dal_tx_metadata *tx_metadata)
{
	int ret;

	if (!global_plat_ops || !global_plat_ops->tx) {
		DP_STATS_INC_PKT(dal_ctx,
				 tx.offload[ring_id][DAL_TX_NOSUPPORT_DRP], 1,
				 dp_tx_get_pkt_len(tx_metadata->tx_desc));
		dp_tx_err_rl("Platform TX operation not available");
		return DP_DAL_TX_FAILURE;
	}

	ret = global_plat_ops->tx(dal_ctx, ring_id, vdev_id, tcl_desc,
				  tx_metadata);
	/* DAL will return -EBUSY if mode switch is in progress, try to send
	 * via bypass mode tx path if it a special frame.
	 */
	if (ret == -EBUSY &&
	    dp_dal_tx_is_special_frame(tx_metadata->tx_desc->nbuf,
				       DP_DAL_SPECIAL_FRAME_MASK))
		ret = dp_dal_tx_bypass_mode(dal_ctx, ring_id, vdev_id,
					    tcl_desc, tx_metadata);

	return (ret == 0) ? DP_DAL_TX_SUCCESS : DP_DAL_TX_FAILURE;
}
#endif

/**
 * dp_dal_tx_cmp_isr_vendor_cb - tx cmpl ISR vendor callback
 * @ring_num: tx completion ring number
 * @priv: pointer to dp dal context
 *
 * Return: 0 on success
 */
int dp_dal_tx_cmp_isr_vendor_cb(int ring_num, void *priv)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct qdf_op_sync *op_sync;
	int grp_id;
	QDF_STATUS status;
	int ret = 0;

	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return -EINVAL;
	}

	if (qdf_op_protect(&op_sync)) {
		dp_err_rl("Driver in transitional state, reject TX CMP ISR ring:%d",
			  ring_num);
		return -EINVAL;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC is NULL");
		ret = -EINVAL;
		goto out;
	}

	grp_id = dp_dal_get_ext_grp_id(dal_ctx, ring_num, COMP_RING_TYPE);
	if (grp_id >= HIF_MAX_GROUP) {
		dp_err("invalid group id:%d ring_num:%d ring_type:%s",
		       grp_id, ring_num, "COMP_RING_TYPE");
		QDF_BUG(0);
		ret = -EINVAL;
		goto out;
	}

	status = hif_ext_grp_napi_schedule(soc->hif_handle, grp_id);
	if (status != QDF_STATUS_SUCCESS) {
		dp_err("Failed to sched NAPI for grp_id:%d ring:%d status:%d",
		       grp_id, ring_num, status);
		ret = qdf_status_to_os_return(status);
		goto out;
	}

out:
	qdf_op_unprotect(op_sync);
	return ret;
}

/**
 * dp_dal_tx_override_ring_id_bypass_mode() - Override ring ID for bypass mode
 * @soc: DP SOC handle
 * @ring_id: Pointer to ring ID to be updated
 *
 * When DAL is enabled(compile time), SW2TCL0 & SW2TCL1 is always reserved.
 * In Offload mode, these two rings are used for STA and SAP respectively,
 * but in the bypass mode, these rings are reserved/unused.
 *
 * Therefore, If ring_id is less than 2, add +2 to the ring_id.
 *
 * Return: QDF_STATUS_SUCCESS on success, QDF_STATUS_E_INVAL on error
 */
static inline QDF_STATUS
dp_dal_tx_override_ring_id_bypass_mode(struct dp_soc *soc, uint8_t *ring_id)
{
	if (qdf_unlikely(!soc || !ring_id))
		return QDF_STATUS_E_INVAL;

	/* DAL requires minimum 3 TCL rings */
	if (qdf_unlikely(soc->num_tcl_data_rings < 3))
		return QDF_STATUS_E_INVAL;

	if (*ring_id >= 2)
		return QDF_STATUS_SUCCESS;

	/* For bypass mode, if ring_id < 2, add +2 */
	*ring_id += 2;

	/* Limit it to remaining rings (2 to num_tcl_data_rings-1) */
	if (*ring_id >= soc->num_tcl_data_rings)
		*ring_id = (*ring_id % (soc->num_tcl_data_rings - 2)) + 2;

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_dal_tx_bypass_mode() - Platform bus tx in bypass mode
 *
 * @priv: dal private data
 * @ring_id: ring ID for TCL descriptor enqueue
 * @ifidx: interface index (vdev_id)
 * @desc: TCL descriptor
 * @tx_metadata: pointer to dp_dal_tx_metadata structure containing MSDU info
 *
 * This function implements the TX hardware enqueue functionality for
 * bypass mode.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_dal_tx_bypass_mode(void *priv, u8 ring_id, u32 ifidx, void *desc,
			  void *tx_metadata)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_dal_tx_metadata *metadata =
		(struct dp_dal_tx_metadata *)tx_metadata;
	struct dp_soc *soc = dal_ctx->soc;
	struct dp_vdev *vdev = metadata->vdev;
	struct dp_tx_desc_s *tx_desc = metadata->tx_desc;
	struct dp_tx_msdu_info_s *msdu_info = metadata->msdu_info;
	struct dp_tx_queue *txq = &msdu_info->tx_queue;
	uint8_t tid = msdu_info->tid;
	uint32_t *hal_tx_desc_cached = desc;
	hal_ring_handle_t hal_ring_hdl = NULL;
	void *hal_tx_desc;
	int coalesce = 0;
	uint8_t num_desc_bytes = HAL_TX_DESC_LEN_BYTES;
	uint32_t hp;
	uint8_t old_ring_id = ring_id;
	QDF_STATUS status;

	/* Override ring ID for bypass mode */
	status = dp_dal_tx_override_ring_id_bypass_mode(soc, &txq->ring_id);
	if (qdf_unlikely(status != QDF_STATUS_SUCCESS)) {
		dp_tx_err_rl("Failed to override ring_id, status: %d", status);
		return -EINVAL;
	}

	ring_id = txq->ring_id;

	/* Since ring_id is updated, update the BM_ID info in the desc */
	soc->arch_ops.dp_tx_desc_update_buffer_info(soc, desc,
						    tx_desc, ring_id);
	hal_ring_hdl = dp_tx_get_hal_ring_hdl(soc, ring_id);

	if (qdf_unlikely(dp_tx_hal_ring_access_start(soc, hal_ring_hdl))) {
		dp_tx_err_rl("HAL RING Access Failed -- %pK", hal_ring_hdl);
		DP_STATS_INC(soc, tx.tcl_ring_full[ring_id], 1);
		return -ENOSPC;
	}

	hal_tx_desc = hal_srng_src_get_next(soc->hal_soc, hal_ring_hdl);
	if (qdf_unlikely(!hal_tx_desc)) {
		dp_verbose_debug("TCL ring full ring_id:%d", ring_id);
		DP_STATS_INC(soc, tx.tcl_ring_full[ring_id], 1);
		goto ring_access_fail;
	}

	/* Sync cached descriptor with HW */
	soc->arch_ops.dp_tx_hw_desc_sync(hal_tx_desc_cached, hal_tx_desc,
					 num_desc_bytes);

	coalesce = dp_tx_attempt_coalescing_wrapper(soc, vdev, tx_desc, tid,
						    msdu_info, ring_id);

	if (qdf_unlikely(dp_tx_pkt_tracepoints_enabled())) {
		hp = hal_srng_src_get_hp(hal_ring_hdl);
		qdf_trace_dp_tx_enqueue(tx_desc->nbuf, hp, ring_id, coalesce);
	}

	dp_tx_update_stats(soc, tx_desc, ring_id);

	dp_tx_hw_desc_update_evt((uint8_t *)hal_tx_desc_cached,
				 hal_ring_hdl, soc, ring_id);

ring_access_fail:
	dp_tx_ring_access_end(soc, hal_ring_hdl, coalesce);

	if (hal_tx_desc) {
		DP_STATS_INC_PKT(dal_ctx,
				 tx.offload[old_ring_id][DAL_TX_BYPASSED_PKT], 1,
				 dp_tx_get_pkt_len(tx_desc));
	} else {
		DP_STATS_INC_PKT(dal_ctx,
				 tx.offload[old_ring_id][DAL_TX_BYPASSED_DRP], 1,
				 dp_tx_get_pkt_len(tx_desc));
	}

	return (hal_tx_desc) ? 0 : -ENOSPC;
}

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
bool dp_dal_tx_cpl_bypass_mode(void *priv, u32 *cnt, u16 ring_id)
{
	return true;
}

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
int dp_dal_tx_queue_active_bypass_mode(void *priv, u16 flowid, bool enable)
{
	return 0;
}

/**
 * dp_dal_tx_hw_enqueue - DAL layer's HW enqueue function for TX packets
 * @soc: DP SOC context.
 * @vdev: DP VDEV context.
 * @tx_desc: TX descriptor for the packet.
 * @fw_metadata: Firmware metadata associated with the packet.
 * @metadata: Exception metadata for TX path.
 * @msdu_info: MSDU information for the packet.
 *
 * This function implements the DAL layer's hardware enqueue logic.
 *
 * Return: QDF_STATUS_SUCCESS on success, error code on failure.
 */
QDF_STATUS dp_dal_tx_hw_enqueue(struct dp_soc *soc,
				struct dp_vdev *vdev,
				struct dp_tx_desc_s *tx_desc,
				uint16_t fw_metadata,
				struct cdp_tx_exception_metadata *metadata,
				struct dp_tx_msdu_info_s *msdu_info)
{
	struct dp_dal_ctx *dal_ctx;
	struct dp_dal_tx_metadata tx_metadata = {};
	void *tcl_desc = NULL;
	qdf_nbuf_t nbuf;
	uint8_t vdev_id;
	uint8_t ring_id;
	int ret;
	QDF_STATUS status;

	dal_ctx = soc->dal_ctx;
	if (qdf_unlikely(!dal_ctx)) {
		dp_tx_err_rl("DAL context is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	nbuf = tx_desc->nbuf;
	vdev_id = tx_desc->vdev_id;
	ring_id = msdu_info->tx_queue.ring_id;

	if (qdf_unlikely(!nbuf)) {
		dp_tx_err_rl("nbuf is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	tx_metadata.msdu_info = msdu_info;
	tx_metadata.vdev = vdev;
	tx_metadata.tx_desc = tx_desc;

	tcl_desc = qdf_mem_malloc(HAL_TX_DESC_LEN_BYTES);
	if (qdf_unlikely(!tcl_desc)) {
		dp_tx_err_rl("Failed to allocate TCL descriptor memory");
		return QDF_STATUS_E_NOMEM;
	}

	/* Generate TCL command via tx_gen_hw_desc soc_ops */
	status = soc->arch_ops.dp_tx_gen_hw_desc(soc, vdev, tx_desc,
						 fw_metadata, metadata,
						 msdu_info, tcl_desc);
	if (qdf_unlikely(QDF_IS_STATUS_ERROR(status))) {
		dp_tx_err_rl("Failed to generate HW descriptor, status: %d",
			     status);
		goto err_free_desc;
	}

	if (!dp_tx_desc_set_ktimestamp(vdev, tx_desc))
		dp_tx_desc_set_timestamp(tx_desc);

	DP_STATS_INC_PKT(dal_ctx,
			 tx.offload[ring_id][DAL_TX_TOTAL_PKT_RCVD], 1,
			 dp_tx_get_pkt_len(tx_desc));

	tx_desc->flags |= DP_TX_DESC_FLAG_QUEUED_TX;

	/* Use runtime PM wrapper for platform TX call */
	ret = dp_dal_tx_rtpm_wrapper(dal_ctx, ring_id, vdev_id, tcl_desc,
				     &tx_metadata);

	/* Handle TX failure */
	if (qdf_unlikely(ret == DP_DAL_TX_FAILURE)) {
		tx_desc->flags &= ~DP_TX_DESC_FLAG_QUEUED_TX;
		dp_tx_err_rl("platform tx failed, ret: %d", ret);
		DP_STATS_INC(vdev,
			     tx_i[msdu_info->xmit_type].dropped.enqueue_fail,
			     1);
		DP_STATS_INC_PKT(dal_ctx,
				 tx.offload[ring_id][DAL_TX_FAILED_DRP],
				 1, dp_tx_get_pkt_len(tx_desc));
		status = QDF_STATUS_E_FAILURE;
		goto err_free_desc;
	}

	/* Handle TX queued during suspend - return success, stats updated
	 * during resume
	 */
	if (ret == DP_DAL_TX_QUEUED) {
		/* TCL descriptor ownership transferred to suspended list */
		tx_desc->flags &= ~DP_TX_DESC_FLAG_QUEUED_TX;
		return QDF_STATUS_SUCCESS;
	}

	/* Handle immediate TX success (ret == DP_DAL_TX_SUCCESS) */
	/* Re-init ring_id as platform_bus_tx() might override it */
	ring_id = msdu_info->tx_queue.ring_id;

	dp_tx_update_proto_stats(vdev, tx_desc->nbuf, ring_id, TX_ENQUEUE_HW);

	/* Update stats for immediate TX success */
	DP_STATS_INC_PKT(vdev, tx_i[msdu_info->xmit_type].processed, 1,
			 dp_tx_get_pkt_len(tx_desc));
	DP_STATS_INC(soc, tx.tcl_enq[ring_id], 1);

	dp_pkt_add_timestamp(vdev, QDF_PKT_TX_DRIVER_EXIT,
			     qdf_get_log_timestamp(), tx_desc->nbuf);
	status = QDF_STATUS_SUCCESS;

err_free_desc:
	qdf_mem_free(tcl_desc);
	return status;
}

/**
 * dp_dal_tx_cpl_cb() - DAL TX completion callback handler
 * @priv: DAL context (dal_ctx)
 * @desc: TX completion descriptor
 * @ring_id: Ring ID
 *
 * This callback handler processes TX completion descriptors received from
 * the platform layer. It replicates all descriptor validation checks from
 * dp_tx_comp_handler and adds valid descriptors to a dedicated list within
 * dal_ctx for later processing.
 *
 * Return: 0 for successful processing
 */
int dp_dal_tx_cpl_cb(void *priv, void *desc, u16 ring_id)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct qdf_op_sync *op_sync;
	void *tx_comp_hal_desc = desc;
	struct dp_tx_desc_s *tx_desc = NULL;
	uint8_t buffer_src;
	QDF_STATUS status;
	hal_soc_handle_t hal_soc;
	uint8_t tx_status;

	if (qdf_unlikely(!dal_ctx)) {
		dp_err("DAL context is NULL");
		return 0;
	}

	if (qdf_op_protect(&op_sync)) {
		dp_err_rl("Driver in transitional state, reject TX CPL ring:%u",
			  ring_id);
		return 0;
	}

	soc = dal_ctx->soc;
	if (qdf_unlikely(!soc)) {
		dp_err("SOC is NULL");
		goto out;
	}

	if (qdf_unlikely(!tx_comp_hal_desc)) {
		dp_err("TX completion descriptor is NULL");
		goto out;
	}

	if (qdf_unlikely(ring_id >= MAX_TCL_DATA_RINGS)) {
		dp_err("Invalid ring_id %u", ring_id);
		goto out;
	}

	hal_soc = soc->hal_soc;

	/* Check if SW cookie has DAL reserved bit set */
	if (soc->arch_ops.dp_dal_tx_comp_check_reserved_cookie &&
	    soc->arch_ops.dp_dal_tx_comp_check_reserved_cookie(soc,
							       tx_comp_hal_desc)) {
		dp_tx_comp_debug("Ignoring DAL reserved cookie completion");
		goto out;
	}

	buffer_src = hal_tx_comp_get_buffer_source(hal_soc, tx_comp_hal_desc);

	/* If this buffer was not released by TQM or FW, then it is not
	 * Tx completion indication, assert.
	 */
	if (qdf_unlikely(buffer_src != HAL_TX_COMP_RELEASE_SOURCE_TQM) &&
	    (qdf_unlikely(buffer_src != HAL_TX_COMP_RELEASE_SOURCE_FW))) {
		uint8_t wbm_internal_error;

		dp_err_rl("Tx comp release_src != TQM | FW but from %d",
			  buffer_src);
		hal_dump_comp_desc(tx_comp_hal_desc);
		DP_STATS_INC(soc, tx.invalid_release_source, 1);

		/* When WBM sees NULL buffer_addr_info in any of
		 * ingress rings it sends an error indication,
		 * with wbm_internal_error=1, to a specific ring.
		 * The WBM2SW ring used to indicate these errors is
		 * fixed in HW, and that ring is being used as Tx
		 * completion ring. These errors are not related to
		 * Tx completions, and should just be ignored
		 */
		wbm_internal_error =
			hal_get_wbm_internal_error(hal_soc, tx_comp_hal_desc);

		if (wbm_internal_error) {
			dp_err_rl("Tx comp wbm_internal_error!!");
			DP_STATS_INC(soc, tx.wbm_internal_error[WBM_INT_ERROR_ALL], 1);

			if (HAL_TX_COMP_RELEASE_SOURCE_REO == buffer_src)
				dp_handle_wbm_internal_error(soc,
							     tx_comp_hal_desc,
							     hal_tx_comp_get_buffer_type(
							     tx_comp_hal_desc));
		} else {
			dp_err_rl("Tx comp wbm_internal_error false");
			DP_STATS_INC(soc, tx.non_wbm_internal_err, 1);
		}
		goto out;
	}

	status = soc->arch_ops.tx_comp_get_params_from_hal_desc(
				soc, tx_comp_hal_desc, &tx_desc);
	if (qdf_unlikely(!tx_desc)) {
		if (QDF_IS_STATUS_SUCCESS(
			dp_tx_comp_stale_entry_handle(soc, ring_id, status))) {
			goto out;
		}

		dp_err("unable to retrieve tx_desc!");
		hal_dump_comp_desc(tx_comp_hal_desc);
		DP_STATS_INC(soc, tx.invalid_tx_comp_desc, 1);
		QDF_BUG(0);
		goto out;
	}

	dp_tx_comp_reset_stale_entry_detection(soc, ring_id);
	tx_desc->buffer_src = buffer_src;
	tx_status = hal_tx_comp_get_tx_status(tx_comp_hal_desc);

	/*
	 * If the release source is FW, process the HTT status
	 */
	if (qdf_unlikely(buffer_src == HAL_TX_COMP_RELEASE_SOURCE_FW) ||
	    dp_tx_fw_release_reason(tx_status)) {
		uint8_t htt_tx_status[HAL_TX_COMP_HTT_STATUS_LEN];

		hal_tx_comp_get_htt_desc(tx_comp_hal_desc, htt_tx_status);

		if (qdf_unlikely(!tx_desc->pdev))
			dp_tx_dump_tx_desc(tx_desc);

		soc->arch_ops.dp_tx_process_htt_completion(soc, tx_desc,
							   htt_tx_status,
							   ring_id);
	} else {
		tx_desc->tx_status = tx_status;
		tx_desc->buffer_src = buffer_src;

		/*
		 * If the descriptor is already freed in vdev_detach,
		 * continue to next descriptor
		 */
		if (qdf_unlikely(tx_desc->vdev_id == DP_INVALID_VDEV_ID &&
				 !tx_desc->flags)) {
			dp_tx_comp_info_rl("Descriptor freed in vdev_detach %d",
					   tx_desc->id);
			DP_STATS_INC(soc, tx.tx_comp_exception, 1);
			dp_tx_desc_check_corruption(tx_desc);
			goto out;
		}

		if (qdf_unlikely(!tx_desc->pdev)) {
			dp_tx_comp_warn("pdev is NULL in TX desc, ignored.");
			dp_tx_dump_tx_desc(tx_desc);
			DP_STATS_INC(soc, tx.tx_comp_exception, 1);
			goto out;
		}

		if (qdf_unlikely(tx_desc->pdev->is_pdev_down)) {
			dp_tx_comp_info_rl("pdev in down state %d",
					   tx_desc->id);
			tx_desc->flags |= DP_TX_DESC_FLAG_TX_COMP_ERR;
			dp_tx_comp_free_buf(soc, tx_desc, false);
			dp_tx_desc_release(soc, tx_desc, tx_desc->pool_id);
			goto out;
		}

		if (!(tx_desc->flags & DP_TX_DESC_FLAG_ALLOCATED) ||
		    !(tx_desc->flags & DP_TX_DESC_FLAG_QUEUED_TX)) {
			dp_tx_comp_alert("Txdesc invalid, flgs = %x,id = %d",
					 tx_desc->flags, tx_desc->id);
			qdf_assert_always(0);
		}

		if (qdf_unlikely(tx_desc->flags & DP_TX_DESC_FLAG_REAPED)) {
			dp_tx_comp_alert("Txdesc duplicate entry, flags = %x,id = %d",
					 tx_desc->flags, tx_desc->id);
			qdf_assert_always(0);
		}

		tx_desc->flags |= DP_TX_DESC_FLAG_REAPED;

		hal_tx_comp_desc_sync_wrapper(tx_comp_hal_desc, NULL,
					      tx_desc, buffer_src, 0, 1);

		/* Add valid descriptor to the dedicated list for processing */
		qdf_spin_lock_bh(&dal_ctx->dal_tx_cpl_lock);

		if (!dal_ctx->tx_cpl_desc_list[ring_id]) {
			dal_ctx->tx_cpl_desc_list[ring_id] = tx_desc;
			dal_ctx->tx_cpl_desc_tail[ring_id] = tx_desc;
		} else {
			dal_ctx->tx_cpl_desc_tail[ring_id]->next = tx_desc;
			dal_ctx->tx_cpl_desc_tail[ring_id] = tx_desc;
		}
		tx_desc->next = NULL;
		dal_ctx->tx_cpl_desc_count[ring_id]++;

		qdf_spin_unlock_bh(&dal_ctx->dal_tx_cpl_lock);
	}

out:
	qdf_op_unprotect(op_sync);
	return 0;
}

/**
 * dp_dal_tx_comp_handler() - DAL TX completion handler
 * @soc: DP SOC context
 * @ring_id: Ring ID
 * @dp_budget: NAPI budget
 *
 * This is the primary API for processing TX completions in the DAL module.
 * It invokes platform_tx_cpl to get completions from the platform layer,
 * then processes the descriptor list accumulated via dp_dal_tx_cpl_cb by
 * calling dp_tx_comp_process_desc_list().
 *
 * Return: Number of completions processed
 */
uint32_t dp_dal_tx_comp_handler(struct dp_soc *soc, u16 ring_id,
				uint32_t dp_budget)
{
	struct dp_dal_ctx *dal_ctx;
	struct dp_tx_desc_s *head_desc = NULL;
	uint32_t cnt = 0;
	uint32_t total_processed = 0;
	bool ret;
	bool force_break = false;
	uint32_t intr_id;
	struct hif_opaque_softc *scn;

	DP_HIST_INIT();

	if (qdf_unlikely(!soc)) {
		dp_tx_err_rl("SOC is NULL");
		return 0;
	}

	dal_ctx = soc->dal_ctx;
	if (qdf_unlikely(!dal_ctx)) {
		dp_tx_err_rl("DAL context is NULL");
		return 0;
	}

	if (qdf_unlikely(ring_id >= MAX_TCL_DATA_RINGS)) {
		dp_tx_err_rl("Invalid ring_id %u", ring_id);
		return 0;
	}

	scn = soc->hif_handle;
	intr_id = dp_dal_get_ext_grp_id(dal_ctx, ring_id, COMP_RING_TYPE);

more_data:
	cnt = 0;
	head_desc = NULL;

	/* Invoke platform_tx_cpl to get completions from DAL layer */
	if (global_plat_ops && global_plat_ops->tx_cpl) {
		ret = global_plat_ops->tx_cpl(dal_ctx, &cnt, ring_id);
		if (qdf_unlikely(!ret)) {
			dp_debug("No TX completions available for ring %u",
				 ring_id);
			DP_STATS_INC(dal_ctx,
				     tx.tx_comp_failed[ring_id], 1);
			goto done;
		}
	} else {
		dp_tx_err_rl("Platform TX CPL operation not available");
		DP_STATS_INC(dal_ctx,
			     tx.tx_comp_nosupport[ring_id], 1);
		goto done;
	}

	/* Process the descriptor list accumulated via dp_dal_tx_cpl_cb */
	qdf_spin_lock_bh(&dal_ctx->dal_tx_cpl_lock);
	head_desc = dal_ctx->tx_cpl_desc_list[ring_id];
	if (head_desc) {
		dal_ctx->tx_cpl_desc_list[ring_id] = NULL;
		dal_ctx->tx_cpl_desc_tail[ring_id] = NULL;
		cnt = dal_ctx->tx_cpl_desc_count[ring_id];
		dal_ctx->tx_cpl_desc_count[ring_id] = 0;
	}
	qdf_spin_unlock_bh(&dal_ctx->dal_tx_cpl_lock);

	total_processed += cnt;

	/* pdev_id is always zero. Pass zero instead of
	 * iterating the tx_desc list for pdev id.
	 */
	DP_HIST_PACKET_COUNT_ADD(0, cnt);

	/* Process the descriptor list using the standard DP function */
	if (head_desc)
		dp_tx_comp_process_desc_list(soc, head_desc, ring_id);

	if (dp_tx_comp_enable_eol_data_check(soc) && cnt > 0) {
		if (total_processed >= dp_budget)
			force_break = true;

		if (!force_break) {
			DP_STATS_INC(soc, tx.hp_oos2, 1);

			/* If we still have time, continue processing */
			if (intr_id < HIF_MAX_GROUP &&
			    !hif_exec_should_yield(scn, intr_id))
				goto more_data;
		}
	}

done:
	DP_STATS_INC(soc, tx.tx_comp[ring_id], total_processed);

	DP_TX_HIST_STATS_PER_PDEV();
	return total_processed;
}
