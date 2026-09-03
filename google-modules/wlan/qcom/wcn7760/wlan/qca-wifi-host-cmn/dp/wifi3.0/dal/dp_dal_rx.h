/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef DP_DAL_RX_H
#define DP_DAL_RX_H

#include "dp_dal.h"
#include "qdf_nbuf.h"

#ifdef FEATURE_DAL_DP_SUPPORT
/**
 *dp_dal_rx_bypass_mode() - Skeleton for platform bus rx in bypass mode
 *
 * @priv: private data
 * @cnt: count
 * @ring_id: RX ring id
 *
 * Return: false
 */
bool dp_dal_rx_bypass_mode(void *priv, u32 *cnt, u16 ring_id);

/**
 * dp_dal_rx_replenish_bypass_mode() - Skeleton for platform bus rx replenish
 * in bypass mode
 *
 * @priv: private data
 * @cnt: count
 * @use_rsv_pktid: use reserved packet id
 *
 * Return: 0 on success
 */
int dp_dal_rx_replenish_bypass_mode(void *priv, u32 cnt, bool use_rsv_pktid);

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
int dp_dal_rx_rxbm_sync_bypass_mode(void *priv, u32 cnt, void **rxbm);

/**
 * dp_dal_rx_process_nbuf_list - Process a list of qdf_nbufs for RX path.
 * @soc: DP SOC context.
 * @nbuf_list: Head of the qdf_nbuf list to be processed.
 *
 * This is a placeholder (skeleton) implementation. The actual RX processing
 * logic should be added by the driver developer.
 *
 * Return: %QDF_STATUS_SUCCESS on success, or an appropriate error code.
 */
QDF_STATUS dp_dal_rx_process_nbuf_list(struct dp_soc *soc,
				       qdf_nbuf_t nbuf_list);

/**
 *dp_dal_rx_isr_vendor_cb - rx ISR vendor callback
 *@ring_num: rx ring number
 *@priv: pointer to dp dal context
 *
 * Return: 0 on success
 */
int dp_dal_rx_isr_vendor_cb(int ring_num, void *priv);

/**
 *dp_dal_rx_replenish_alloc_vendor_cb - vendor callback to alloc rx buffers
 * @priv: pointer to dp dal context
 * @count: number of buffers to allocate
 *
 * Return: 0 on success
 */
int dp_dal_rx_replenish_alloc_vendor_cb(void *priv, uint16_t count);

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
void dp_dal_rx_desc_list_cleanup(struct dp_dal_ctx *dal_ctx);

/**
 * dp_dal_rx_desc_cb() - vendor callback for RX descriptor processing
 * @priv: DAL context (dal_ctx)
 * @desc: RX descriptor
 * @ring_id: Ring ID for proper parameter passing
 *
 * This callback processes RX descriptors with complete validation
 * checks from dp_rx_process_be/bn() first reap loop, and enqueues valid rx_desc
 * into the global rx_desc list in dal_ctx after all validations.
 *
 * Return: 0 for successful processing
 */
int dp_dal_rx_desc_cb(void *priv, void *desc, u16 ring_id);

/**
 * dp_dal_rx_handler() - RX handler for DAL RX rings
 * @soc: DP SOC context
 * @ring_id: Ring ID
 * @dp_budget: NAPI budget
 *
 * This function implements the DAL RX handler following the precise steps:
 * 1. Invoke platform_bus_rx(dal_ctx, &cnt) from NAPI context
 * 2. Process accumulated rx_desc_list in dal_ctx and form nbuf list
 * 3. Execute RX replenishment via platform_bus_rx_replenish(dal_ctx, cnt)
 *
 * Return: Number of packets processed
 */
uint32_t dp_dal_rx_handler(struct dp_soc *soc, u16 ring_id, uint32_t dp_budget);

/**
 * dp_dal_rx_pkt_reinject_bypass_mode() - Skeleton for platform bus rx packet
 * reinject in bypass mode
 *
 * @pkt: packet data pointer
 * @length: packet length
 *
 * Return: false (packet not consumed, let WLAN driver submit to network stack)
 */
bool dp_dal_rx_pkt_reinject_bypass_mode(void *pkt, uint32_t length);

/**
 * dp_dal_rx_pkt_reinject() - DAL API to reinject RX packet
 * @nbuf: Network buffer to reinject
 *
 * This function checks if the NBUF is non-linear and linearizes it if needed,
 * then invokes platform_bus_rx_pkt_reinject with packet data and length.
 *
 * Return: true if DAL module consumed the packet, false if WLAN driver
 *         should submit to network stack
 */
bool dp_dal_rx_pkt_reinject(qdf_nbuf_t nbuf);
#endif /* FEATURE_DAL_DP_SUPPORT */
#endif /* DP_DAL_RX_H */
