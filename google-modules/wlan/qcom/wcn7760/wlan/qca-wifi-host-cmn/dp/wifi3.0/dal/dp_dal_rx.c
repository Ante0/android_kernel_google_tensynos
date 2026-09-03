/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "dp_dal_rx.h"
#include "dp_rx.h"
#include "dp_dal.h"
#include "wlan_cfg.h"
#include "hif.h"
#include "dp_rx_buffer_pool.h"
#include "qdf_platform.h"

extern struct platform_bus_ops *global_plat_ops;

#define BUFFER_ADDR_INFO_SIZE 8

/**
 * dp_dal_rx_replenish_alloc_vendor_cb - vendor callback to alloc rx buffers
 * @priv: pointer to dp dal context
 * @count: number of buffers to allocate
 *
 * Return: 0 on success
 */
int dp_dal_rx_replenish_alloc_vendor_cb(void *priv, uint16_t count)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct rx_desc_pool *rx_desc_pool;
	struct dp_pdev *pdev;
	struct dp_rx_nbuf_frag_info nf_info;
	union dp_rx_desc_list_elem_t *desc_list;
	union dp_rx_desc_list_elem_t *tail;
	union dp_rx_desc_list_elem_t *next;
	struct dp_rx_desc *rx_desc;
	union dp_rx_desc_list_elem_t *next_desc;
	void *buffer_addr_info;
	void *orig_buffer_addr_info;
	uint16_t num_buffer_info_per_page;
	uint16_t nr_desc;
	uint8_t total_pages;
	uint8_t page_idx;
	int mac_id = 0;
	int total_nbuf_count = 0;
	int nbuf_count;
	int i;
	int ret;
	bool dp_buf_page_frag_alloc_enable;

	if (!dal_ctx) {
		dp_err("dal_ctx is NULL");
		return -EINVAL;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("soc is NULL in dal_ctx");
		return -EINVAL;
	}

	pdev = soc->pdev_list[0];
	rx_desc_pool = &soc->rx_desc_buf[mac_id];
	num_buffer_info_per_page = DP_BLOCKMEM_SIZE / BUFFER_ADDR_INFO_SIZE;

	dp_buf_page_frag_alloc_enable =
		wlan_cfg_is_dp_buf_page_frag_alloc_enable(soc->wlan_cfg_ctx);

	nr_desc = dp_rx_get_free_desc_list(soc, mac_id, rx_desc_pool,
					   count, &desc_list, &tail);
	if (!nr_desc) {
		dp_err("dp_dal: no free rx_descs in freelist");
		DP_STATS_INC(pdev, err.desc_alloc_fail, count);
		return -ENOMEM;
	}

	total_pages = nr_desc / num_buffer_info_per_page;
	if (nr_desc % num_buffer_info_per_page)
		total_pages++;

	orig_buffer_addr_info = qdf_mem_malloc(DP_BLOCKMEM_SIZE);
	if (!orig_buffer_addr_info) {
		dp_err("dp_dal:failed to allocate buffer_addr_info array");
		dp_rx_add_desc_list_to_free_list(soc, &desc_list, &tail,
						 mac_id, rx_desc_pool);
		return -ENOMEM;
	}

	for (page_idx = 0; page_idx < total_pages; page_idx++) {
		buffer_addr_info = orig_buffer_addr_info;
		union dp_rx_desc_list_elem_t *desc_list_page_start = desc_list;
		void *buf_addr_info_start = buffer_addr_info;

		qdf_mem_zero(buffer_addr_info, DP_BLOCKMEM_SIZE);
		nbuf_count = 0;

		for (i = 0; i < num_buffer_info_per_page; i++) {
			if (total_nbuf_count >= nr_desc)
				break;

			qdf_mem_zero(&nf_info,
				     sizeof(struct dp_rx_nbuf_frag_info));

			if (qdf_unlikely(rx_desc_pool->rx_mon_dest_frag_enable))
				ret = dp_pdev_frag_alloc_and_map(soc,
								 &nf_info, pdev,
								 rx_desc_pool);
			else
				ret = dp_pdev_nbuf_alloc_and_map_replenish(soc, mac_id,
									   count, &nf_info,
									   pdev, rx_desc_pool);

			if (qdf_unlikely(QDF_IS_STATUS_ERROR(ret))) {
				if (qdf_unlikely(ret  == QDF_STATUS_E_FAULT))
					continue;
				break;
			}

			nbuf_count++;
			next = (desc_list)->next;

			if (qdf_unlikely(rx_desc_pool->rx_mon_dest_frag_enable))
				dp_rx_desc_frag_prep(&desc_list->rx_desc,
						     &nf_info);
			else
				dp_rx_desc_prep(&desc_list->rx_desc,
						&nf_info);

			desc_list->rx_desc.in_use = 1;
			desc_list->rx_desc.in_err_state = 0;
			dp_rx_desc_update_dbg_info(&desc_list->rx_desc,
						   __func__,
						   RX_DESC_REPLENISHED);
			dp_verbose_debug("rx_buf=%pK, paddr=0x%llx, cookie=%d",
					 nf_info.virt_addr.nbuf,
					 (unsigned long long)(nf_info.paddr),
					 desc_list->rx_desc.cookie);

			if (qdf_likely(!rx_desc_pool->rx_mon_dest_frag_enable))
				qdf_assert_always(nf_info.paddr ==
						  QDF_NBUF_CB_PADDR(desc_list->rx_desc.nbuf));

			hal_rxdma_buff_addr_info_set(soc->hal_soc,
						     buffer_addr_info,
						     nf_info.paddr,
						     desc_list->rx_desc.cookie,
						     rx_desc_pool->owner);

			total_nbuf_count++;
			buffer_addr_info += BUFFER_ADDR_INFO_SIZE;
			desc_list = next;
		}

		if (nbuf_count) {
			int synced_cnt;

			if (!global_plat_ops || !global_plat_ops->rxbm_sync) {
				dp_err("no rxbm_sync plat op registered");
				ret = -EINVAL;
			} else {
				ret = global_plat_ops->rxbm_sync(dal_ctx,
								 nbuf_count,
								 &buf_addr_info_start);
			}

			if (ret == nbuf_count)
				continue;

			if (ret < 0) {
				dp_err("DAL rxbm_sync failed with error: %d",
				       ret);
				synced_cnt = 0;
			} else if (qdf_unlikely(ret > nbuf_count)) {
				dp_err("rxbm_sync ret invalid count: %d > %d",
				       ret, nbuf_count);
				break;
			} else {
				dp_info("partial sync: synced %d of %d buffers",
					ret, nbuf_count);
				synced_cnt = ret;
			}

			/*
			 * Free the remaining unsynced buffers for partial or
			 * complete failures.
			 */
			union dp_rx_desc_list_elem_t *curr_desc =
							desc_list_page_start;
			int rem_count = nbuf_count - synced_cnt;
			int j;

			for (j = 0; j < synced_cnt && curr_desc; j++)
				curr_desc = curr_desc->next;

			if (!curr_desc && rem_count > 0) {
				dp_err("Desc list exhausted, rem buffers %d",
				       rem_count);
				total_nbuf_count -= rem_count;
				break;
			}

			for (j = 0; j < rem_count && curr_desc; j++) {
				rx_desc = &curr_desc->rx_desc;
				next_desc = curr_desc->next;

				if (rx_desc->nbuf) {
					dp_rx_nbuf_unmap_pool(soc, rx_desc_pool,
							      rx_desc->nbuf);
					rx_desc->unmapped = 1;
					dp_rx_buffer_pool_nbuf_free(soc,
								    rx_desc->nbuf,
								    mac_id);
					rx_desc->nbuf = NULL;
				}

				rx_desc->in_use = 0;
				rx_desc->in_err_state = 0;

				curr_desc->next = NULL;
				dp_rx_add_desc_list_to_free_list(soc, &curr_desc,
								 &curr_desc, mac_id,
								 rx_desc_pool);
				curr_desc = next_desc;
			}

			total_nbuf_count -= rem_count;
			break;
		}
	}

	qdf_mem_free(orig_buffer_addr_info);

	if (desc_list)
		dp_rx_add_desc_list_to_free_list(soc, &desc_list, &tail,
						 mac_id, rx_desc_pool);

	return 0;
}

/**
 * dp_dal_rx_isr_vendor_cb - rx ISR vendor callback
 * @ring_num: rx ring number
 * @priv: pointer to dp dal context
 *
 * Return: 0 on success
 */
int dp_dal_rx_isr_vendor_cb(int ring_num, void *priv)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct qdf_op_sync *op_sync;
	int grp_id;
	int ret = 0;

	if (!dal_ctx) {
		dp_err("dal_ctx is NULL");
		return -EINVAL;
	}

	if (qdf_op_protect(&op_sync)) {
		dp_err_rl("Driver in transitional state, reject RX ISR ring:%d",
			  ring_num);
		return -EINVAL;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("soc is NULL");
		ret = -EINVAL;
		goto out;
	}

	grp_id = dp_dal_get_ext_grp_id(dal_ctx, ring_num, REO_DST);
	if (grp_id >= HIF_MAX_GROUP) {
		dp_err("invalid group id:%d ring_num:%d ring_type:%s",
		       grp_id, ring_num, " REO_DEST");
		QDF_BUG(0);
		ret = -EINVAL;
		goto out;
	}

	hif_ext_grp_napi_schedule(soc->hif_handle, grp_id);

out:
	qdf_op_unprotect(op_sync);
	return ret;
}

/**
 * dp_dal_rx_bypass_mode() - Skeleton for platform bus rx in bypass mode
 *
 * @priv: private data
 * @cnt: count
 * @ring_id: RX ring id
 *
 * Return: false
 */
bool dp_dal_rx_bypass_mode(void *priv, u32 *cnt, u16 ring_id)
{
	return false;
}

/**
 * dp_dal_rx_replenish_bypass_mode() - replenish rx buffers
 * in bypass mode
 *
 * @priv: private data
 * @cnt: count
 * @use_rsv_pktid: use reserved packet id
 *
 * Return: 0 on success
 */
int dp_dal_rx_replenish_bypass_mode(void *priv, u32 cnt, bool use_rsv_pktid)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct rx_desc_pool *rx_desc_pool;
	union dp_rx_desc_list_elem_t *desc_list_head = NULL;
	union dp_rx_desc_list_elem_t *desc_list_tail = NULL;
	int mac_id = 0;

	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return -EINVAL;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC is NULL in DAL context");
		return -EINVAL;
	}

	qdf_spin_lock_bh(&dal_ctx->dal_replenish_lock);
	if (qdf_atomic_read(&dal_ctx->bm_replenish_not_allowed)) {
		qdf_spin_unlock_bh(&dal_ctx->dal_replenish_lock);
		return -EBUSY;
	}

	rx_desc_pool = &soc->rx_desc_buf[mac_id];
	__dp_rx_buffers_replenish(soc, mac_id,
				  &soc->rx_refill_buf_ring[mac_id],
				  rx_desc_pool, cnt,
				  &desc_list_head, &desc_list_tail, true,
				  false, __func__);
	qdf_spin_unlock_bh(&dal_ctx->dal_replenish_lock);

	return 0;
}

/**
 * dp_dal_rx_rxbm_sync_bypass_mode() - Skeleton for platform bus rxbm sync
 * in bypass mode
 *
 * @priv: private data
 * @cnt: count
 * @rxbm: rxbm
 *
 * Return: 0 on success
 */
int dp_dal_rx_rxbm_sync_bypass_mode(void *priv, u32 cnt, void **rxbm)
{
	return 0;
}

/**
 * dp_dal_rx_pkt_reinject_bypass_mode() - Skeleton for platform bus rx packet
 * reinject in bypass mode
 *
 * @pkt: packet data pointer
 * @length: packet length
 *
 * Return: false (packet not consumed, let WLAN driver submit to network stack)
 */
bool dp_dal_rx_pkt_reinject_bypass_mode(void *pkt, uint32_t length)
{
	/* In bypass mode, DAL does not consume packets for reinjection */
	return false;
}

/**
 * dp_dal_rx_add_desc_to_tail() - Add descriptor to tail of global list
 * @dal_ctx: DAL context
 * @ring_id: Ring ID
 * @rx_desc: Descriptor to add to tail
 *
 * Inline function to add descriptor to tail of global list.
 * This is used by the rx vendor cb to enqueue new descriptors.
 */
static inline int
dp_dal_rx_add_desc_to_tail(struct dp_dal_ctx *dal_ctx, uint8_t ring_id,
			   struct dp_rx_desc *rx_desc)
{
	struct dp_dal_rx_desc_node *node;

	node = qdf_mem_malloc(sizeof(*node));
	if (qdf_unlikely(!node)) {
		dp_err_rl("Failed to allocate DAL RX node for ring %u",
			  ring_id);
		return -ENOMEM;
	}

	node->rx_desc = rx_desc;
	node->next = NULL;

	if (!dal_ctx->rx_desc_head[ring_id]) {
		dal_ctx->rx_desc_head[ring_id] = node;
		dal_ctx->rx_desc_tail[ring_id] = node;
	} else {
		dal_ctx->rx_desc_tail[ring_id]->next = node;
		dal_ctx->rx_desc_tail[ring_id] = node;
	}

	dal_ctx->rx_desc_count[ring_id]++;

	return 0;
}

/**
 * dp_dal_rx_desc_cb() - Vendor callback for RX descriptor processing
 * @priv: DAL context (dal_ctx)
 * @desc: RX descriptor
 * @ring_id: Ring ID for proper parameter passing
 *
 * This callback processes RX descriptors and enqueues valid rx_desc
 * into the global rx_desc list in dal_ctx after all validations.
 *
 * Return: 0 for successful processing
 */
int dp_dal_rx_desc_cb(void *priv, void *desc, u16 ring_id)
{
	struct dp_dal_ctx *dal_ctx = (struct dp_dal_ctx *)priv;
	struct dp_soc *soc;
	struct qdf_op_sync *op_sync;
	hal_ring_desc_t ring_desc = (hal_ring_desc_t)desc;
	struct dp_rx_desc *rx_desc = NULL;
	int ret = 0;

	if (qdf_unlikely(!dal_ctx)) {
		dp_err("DAL context is NULL");
		return -EINVAL;
	}

	if (qdf_op_protect(&op_sync)) {
		dp_err_rl("Driver in transitional state, reject RX desc ring:%u",
			  ring_id);
		return -EINVAL;
	}

	soc = dal_ctx->soc;
	if (qdf_unlikely(!soc)) {
		dp_err("SOC is NULL");
		ret = -EINVAL;
		goto out;
	}

	if (qdf_unlikely(!ring_desc)) {
		dp_err_rl("RX descriptor is NULL");
		ret = -EINVAL;
		goto out;
	}

	if (qdf_unlikely(ring_id >= MAX_REO_DEST_RINGS)) {
		dp_err_rl("Invalid ring_id %u, max allowed %u", ring_id,
			  MAX_REO_DEST_RINGS);
		ret = -EINVAL;
		goto out;
	}

	rx_desc = soc->arch_ops.dp_rx_validate_and_fetch_rx_desc(soc, ring_desc,
								 ring_id);
	if (!rx_desc) {
		ret = -EINVAL;
		goto out;
	}

	qdf_spin_lock_bh(&dal_ctx->dal_rx_desc_lock);
	if (dp_dal_rx_add_desc_to_tail(dal_ctx, ring_id, rx_desc)) {
		qdf_spin_unlock_bh(&dal_ctx->dal_rx_desc_lock);
		ret = -ENOMEM;
		goto out;
	}
	qdf_spin_unlock_bh(&dal_ctx->dal_rx_desc_lock);

out:
	qdf_op_unprotect(op_sync);
	return ret;
}

/**
 * dp_dal_rx_desc_list_cleanup() - Cleanup DAL RX descriptor lists
 * @dal_ctx: DAL context
 *
 * This function cleans up any remaining RX descriptors in the DAL RX
 * descriptor lists during DAL deinit. It properly frees all allocated
 * dp_dal_rx_desc_node structures and returns RX descriptors to their
 * respective free lists.
 *
 * Return: None
 */
void dp_dal_rx_desc_list_cleanup(struct dp_dal_ctx *dal_ctx)
{
	struct dp_dal_rx_desc_node *node, *next_node;
	struct dp_rx_desc *rx_desc;
	struct dp_soc *soc;
	struct rx_desc_pool *rx_desc_pool;
	union dp_rx_desc_list_elem_t *desc_list_head[MAX_PDEV_CNT] = {NULL};
	union dp_rx_desc_list_elem_t *desc_list_tail[MAX_PDEV_CNT] = {NULL};
	int ring_id, mac_id;

	if (!dal_ctx) {
		dp_err("DAL context is NULL");
		return;
	}

	soc = dal_ctx->soc;
	if (!soc) {
		dp_err("SOC is NULL in DAL context");
		return;
	}

	qdf_spin_lock_bh(&dal_ctx->dal_rx_desc_lock);

	/* Cleanup RX descriptor lists for all rings */
	for (ring_id = 0; ring_id < MAX_REO_DEST_RINGS; ring_id++) {
		node = dal_ctx->rx_desc_head[ring_id];

		while (node) {
			next_node = node->next;
			rx_desc = node->rx_desc;

			if (rx_desc)
				dp_rx_add_to_free_desc_list(
					&desc_list_head[rx_desc->pool_id],
					&desc_list_tail[rx_desc->pool_id],
					rx_desc);

			qdf_mem_free(node);
			node = next_node;
		}

		dal_ctx->rx_desc_head[ring_id] = NULL;
		dal_ctx->rx_desc_tail[ring_id] = NULL;
		dal_ctx->rx_desc_count[ring_id] = 0;
	}

	qdf_spin_unlock_bh(&dal_ctx->dal_rx_desc_lock);

	/* Return descriptors to their respective free lists */
	for (mac_id = 0; mac_id < MAX_PDEV_CNT; mac_id++) {
		if (desc_list_head[mac_id]) {
			rx_desc_pool = &soc->rx_desc_buf[mac_id];
			dp_rx_add_desc_list_to_free_list(
					soc, &desc_list_head[mac_id],
					&desc_list_tail[mac_id],
					mac_id, rx_desc_pool);
		}
	}
}

/**
 * dp_dal_rx_remove_desc_from_head() - Remove rx desc from head of global list
 * @dal_ctx: DAL context
 * @ring_id: Ring ID
 *
 * Function to remove first descriptor from global list.
 *
 * Return: Pointer to removed descriptor, NULL if list is empty
 */
static inline struct dp_rx_desc *
dp_dal_rx_remove_desc_from_head(struct dp_dal_ctx *dal_ctx, uint8_t ring_id)
{
	struct dp_dal_rx_desc_node *node;
	struct dp_rx_desc *rx_desc;

	node = dal_ctx->rx_desc_head[ring_id];
	if (!node)
		return NULL;

	dal_ctx->rx_desc_head[ring_id] = node->next;

	if (!node->next)
		dal_ctx->rx_desc_tail[ring_id] = NULL;

	dal_ctx->rx_desc_count[ring_id]--;
	rx_desc = node->rx_desc;
	qdf_mem_free(node);

	return rx_desc;
}

/**
 * dp_dal_rx_add_desc_to_head() - Add descriptor back to head of global list
 * @dal_ctx: DAL context
 * @ring_id: Ring ID
 * @rx_desc: Descriptor to add back
 *
 * Inline function to add descriptor back to head of global list.
 */
static inline void
dp_dal_rx_add_desc_to_head(struct dp_dal_ctx *dal_ctx, uint8_t ring_id,
			   struct dp_rx_desc *rx_desc)
{
	struct dp_dal_rx_desc_node *node;

	node = qdf_mem_malloc(sizeof(*node));
	if (qdf_unlikely(!node)) {
		dp_err_rl("Failed to allocate DAL RX node for ring %u",
			  ring_id);
		return;
	}

	node->rx_desc = rx_desc;
	node->next = dal_ctx->rx_desc_head[ring_id];

	dal_ctx->rx_desc_head[ring_id] = node;
	if (!dal_ctx->rx_desc_tail[ring_id])
		dal_ctx->rx_desc_tail[ring_id] = node;

	dal_ctx->rx_desc_count[ring_id]++;
}

/**
 * dp_dal_rx_handler() - Unified DAL RX handler for both BE and BN
 * @soc: DP SOC context
 * @ring_id: Ring ID
 * @dp_budget: NAPI budget
 *
 * This function implements the unified DAL RX handler following the precise
 * steps:
 * 1. Invoke platform_bus_rx(dal_ctx, &cnt) from NAPI context
 * 2. Process accumulated rx_desc_list in dal_ctx and form skb_list like
 *    first loop
 * 3. Execute RX replenishment via platform_bus_rx_replenish(dal_ctx, cnt)
 * Includes dp_rx_war_peek_msdu_done check for both BE and BN (as requested).
 * Uses Beryllium-style vdev flushing for both architectures.
 *
 * Return: Number of packets processed
 */
uint32_t dp_dal_rx_handler(struct dp_soc *soc, u16 ring_id, uint32_t dp_budget)
{
	struct dp_dal_ctx *dal_ctx;
	struct dp_rx_desc *rx_desc;
	qdf_nbuf_t nbuf_head = NULL;
	qdf_nbuf_t nbuf_tail = NULL;
	uint32_t cnt = 0;
	uint32_t processed = 0;
	uint32_t num_pending = 0;
	bool ret;
	qdf_nbuf_t ebuf_head = NULL;
	qdf_nbuf_t ebuf_tail = NULL;
	bool is_prev_msdu_last = true;
	uint16_t buf_size;
	union dp_rx_desc_list_elem_t *head[MAX_PDEV_CNT] = {NULL};
	union dp_rx_desc_list_elem_t *tail[MAX_PDEV_CNT] = {NULL};
	struct rx_desc_pool *rx_desc_pool;
	qdf_nbuf_t curr_nbuf;
	qdf_nbuf_t next_nbuf;
	int mac_id;
	bool force_break = false;
	uint32_t intr_id;
	uint32_t loop_processed;
	int max_reap_limit;
	struct hif_opaque_softc *scn;
	uint32_t rx_desc_cnt = 0;

	if (qdf_unlikely(!soc)) {
		dp_err_rl("SOC is NULL");
		return 0;
	}

	dal_ctx = soc->dal_ctx;
	if (qdf_unlikely(!dal_ctx)) {
		dp_err_rl("DAL context is NULL");
		return 0;
	}

	if (qdf_unlikely(ring_id >= MAX_REO_DEST_RINGS)) {
		dp_err_rl("Invalid ring_id %u", ring_id);
		return 0;
	}

	scn = soc->hif_handle;
	intr_id = dp_dal_get_ext_grp_id(dal_ctx, ring_id, REO_DST);
	buf_size = wlan_cfg_rx_buffer_size(soc->wlan_cfg_ctx);
	max_reap_limit = dp_rx_get_loop_pkt_limit(soc);

	dp_runtime_pm_mark_last_busy(soc);

more_data:
	nbuf_head = NULL;
	nbuf_tail = NULL;
	ebuf_head = NULL;
	ebuf_tail = NULL;
	qdf_mem_zero(head, sizeof(head));
	qdf_mem_zero(tail, sizeof(tail));
	is_prev_msdu_last = true;

	qdf_spin_lock_bh(&dal_ctx->dal_rx_desc_lock);
	rx_desc_cnt = dal_ctx->rx_desc_count[ring_id];
	qdf_spin_unlock_bh(&dal_ctx->dal_rx_desc_lock);

	/* Check if we have pending descriptors in the list before
	 * fetching new ones.
	 */
	if (!rx_desc_cnt) {
		/* No pending descriptors, try to get new ones */
		if (global_plat_ops && global_plat_ops->rx) {
			ret = global_plat_ops->rx(dal_ctx, &cnt, ring_id);
			if (qdf_unlikely(!ret)) {
				dp_debug("No RX packets available for ring %u",
					 ring_id);
				goto done;
			}
		} else {
			DP_STATS_INC(dal_ctx,
				     rx.rx_dropped_nosupport[ring_id], 1);
			dp_err_rl("Platform RX operation not available");
			goto done;
		}

		if (!dal_ctx->rx_desc_head[ring_id]) {
			dp_debug("Platform RX returned zero descriptors for ring %u",
				 ring_id);
			goto done;
		}
	}

	qdf_spin_lock_bh(&dal_ctx->dal_rx_desc_lock);
	num_pending = dal_ctx->rx_desc_count[ring_id];
	loop_processed = 0;

	while (dal_ctx->rx_desc_head[ring_id]) {
		rx_desc = dp_dal_rx_remove_desc_from_head(dal_ctx, ring_id);
		if (!rx_desc)
			break;

		if (qdf_unlikely(qdf_nbuf_is_rx_chfrag_cont(rx_desc->nbuf))) {
			qdf_nbuf_set_rx_chfrag_end(rx_desc->nbuf, 0);

			if (is_prev_msdu_last) {
				if ((QDF_NBUF_CB_RX_PKT_LEN(rx_desc->nbuf) /
				     (buf_size - soc->rx_pkt_tlv_size) + 1) >
				      num_pending) {
					DP_STATS_INC(soc,
						     rx.msdu_scatter_wait_break,
						     1);
					dp_rx_cookie_reset_invalid_bit(NULL);
					/* Break the loop - insufficient descs
					 * for sg, add descriptor back to head.
					 */
					dp_dal_rx_add_desc_to_head(dal_ctx,
								   ring_id,
								   rx_desc);
					break;
				}
				is_prev_msdu_last = false;
			}
		} else if (qdf_unlikely(!dp_rx_war_peek_msdu_done(soc,
								  rx_desc))) {
			struct dp_rx_desc *old_rx_desc =
				dp_rx_war_store_msdu_done_fail_desc(soc,
								    rx_desc,
								    ring_id);
			if (qdf_likely(old_rx_desc)) {
				dp_rx_add_to_free_desc_list(&head[old_rx_desc->pool_id],
							    &tail[old_rx_desc->pool_id],
							    old_rx_desc);
				num_pending -= 1;
				loop_processed++;
				processed++;
			}
			rx_desc->msdu_done_fail = 1;
			DP_STATS_INC(soc, rx.err.msdu_done_fail, 1);
			dp_err_rl("MSDU DONE failure %d",
				  soc->stats.rx.err.msdu_done_fail);
			dp_rx_msdu_done_fail_event_record(soc, rx_desc,
							  rx_desc->nbuf);
			continue;
		}

		if (!is_prev_msdu_last &&
		    !(qdf_nbuf_is_rx_chfrag_cont(rx_desc->nbuf)))
			is_prev_msdu_last = true;

		DP_RX_PROCESS_NBUF(soc, nbuf_head, nbuf_tail, ebuf_head,
				   ebuf_tail, rx_desc);

		dp_rx_nbuf_unmap(soc, rx_desc, ring_id);

		dp_rx_add_to_free_desc_list(&head[rx_desc->pool_id],
					    &tail[rx_desc->pool_id], rx_desc);

		num_pending -= 1;
		loop_processed++;
		processed++;

		/*
		 * Only if complete msdu is received for scatter case,
		 * then allow break.
		 */
		if (is_prev_msdu_last &&
		    dp_rx_reap_loop_pkt_limit_hit(soc, loop_processed,
						  max_reap_limit))
			break;
	}

	qdf_spin_unlock_bh(&dal_ctx->dal_rx_desc_lock);

	dp_rx_per_core_stats_update(soc, ring_id, loop_processed);

	for (mac_id = 0; mac_id < MAX_PDEV_CNT; mac_id++) {
		if (head[mac_id]) {
			rx_desc_pool = &soc->rx_desc_buf[mac_id];
			dp_rx_add_desc_list_to_free_list(soc, &head[mac_id],
							 &tail[mac_id], mac_id,
							 rx_desc_pool);
		}
	}

	if (global_plat_ops && global_plat_ops->rx_replenish &&
	    loop_processed > 0) {
		ret = global_plat_ops->rx_replenish(dal_ctx, loop_processed,
						    false);
		if (qdf_unlikely(ret)) {
			if (soc->dal_mode_switch_in_progress) {
				ret = dp_dal_rx_replenish_bypass_mode(
						dal_ctx, loop_processed, false);
				if (ret)
					qdf_atomic_add(loop_processed,
						       &dal_ctx->rx_replenish_failures);
			} else {
				dp_err_rl("RX replenish failed, ret: %d", ret);
				qdf_atomic_add(loop_processed,
					       &dal_ctx->rx_replenish_failures);
			}
		}
	} else {
		dp_err_rl("RX replenish failed, null plat ops");
	}

	if (nbuf_head) {
		if (soc->arch_ops.dp_dal_rx_process_nbuf_list) {
			soc->arch_ops.dp_dal_rx_process_nbuf_list(soc, nbuf_head, ring_id);
		} else {
			dp_err_rl("Arch nbuf processing op not available");
			/* Free the nbuf list if no arch_ops handler */
			curr_nbuf = nbuf_head;
			while (curr_nbuf) {
				next_nbuf = qdf_nbuf_next(curr_nbuf);
				dp_rx_nbuf_free(curr_nbuf);
				curr_nbuf = next_nbuf;
			}
		}
	}

	if (dp_rx_enable_eol_data_check(soc) && processed > 0) {
		if (processed >= dp_budget)
			force_break = true;

		if (!force_break) {
			DP_STATS_INC(soc, rx.hp_oos2, 1);

			/* If we still have time, continue processing */
			if (intr_id < HIF_MAX_GROUP &&
			    !hif_exec_should_yield(scn, intr_id))
				goto more_data;
		}
	}

done:
	return processed;
}

/**
 * dp_dal_rx_pkt_reinject() - DAL API to reinject RX packet
 * @nbuf: Network buffer to reinject
 *
 * This function checks if the NBUF is non-linear and linearizes it if needed,
 * then invokes platform_bus_rx_pkt_reinject with packet data and length.
 *
 * Return true means DAL consumed the packet
 * Return false means DAL did not consume the packet - driver must submit it
 * to network stack
 */
bool dp_dal_rx_pkt_reinject(qdf_nbuf_t nbuf)
{
	void *pkt_data;
	uint32_t pkt_len;
	int ret;
	bool consumed = false;

	if (qdf_unlikely(!nbuf)) {
		dp_err("Invalid nbuf parameter");
		return false;
	}

	/* Check if NBUF is non-linear and linearize if needed */
	if (qdf_nbuf_is_nonlinear(nbuf)) {
		dp_debug("NBUF is non-linear, linearizing");
		ret = qdf_nbuf_linearize(nbuf);
		if (qdf_unlikely(ret)) {
			dp_err("Failed to linearize nbuf, ret: %d", ret);
			return false;
		}
	}

	/* Get packet data and length */
	pkt_data = qdf_nbuf_data(nbuf);
	pkt_len = qdf_nbuf_len(nbuf);

	if (qdf_likely(global_plat_ops &&
		       global_plat_ops->rx_pkt_reinject)) {
		consumed = global_plat_ops->rx_pkt_reinject(pkt_data, pkt_len);
	} else {
		dp_debug("Platform bus rx_pkt_reinject op not available");
		consumed = false;
	}

	return consumed;
}
