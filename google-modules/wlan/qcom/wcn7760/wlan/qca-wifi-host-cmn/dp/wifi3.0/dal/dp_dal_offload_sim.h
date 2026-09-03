/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef DP_DAL_OFFLOAD_SIM_H
#define DP_DAL_OFFLOAD_SIM_H

#include "dp_dal_sim.h"
#include "dal_vndr_hal_api.h"
#ifdef FEATURE_DP_DAL_SIM

/* PCIE Doorbell registers for FW communication */
/* Message register - write message value here before triggering interrupt */
#define PCIE_PCIE_LOCAL_REG_APPS_TO_Q6 0x3224
#define PCIE_DOORBELL_MSG_ADDR PCIE_PCIE_LOCAL_REG_APPS_TO_Q6

/* Interrupt register - write 1 to trigger interrupt after writing message */
#define PCIE_PCIE_LOCAL_REG_WCSS_IE_IRQ 0x3228
#define PCIE_DOORBELL_IRQ_ADDR PCIE_PCIE_LOCAL_REG_WCSS_IE_IRQ
#define PCIE_DOORBELL_IRQ_TRIGGER 0x1

/**
 * enum offload_sim_ring_type - Ring type for interrupt handling
 * @OFFLOAD_SIM_RING_TYPE_RX: RX ring (REO destination)
 * @OFFLOAD_SIM_RING_TYPE_TX_CPL: TX completion ring (WBM2SW)
 * @OFFLOAD_SIM_RING_TYPE_TX: Tx data ring (TCL DATA)
 * @OFFLOAD_SIM_RING_TYPE_RX_REFILL: Rx refill ring
 */
enum offload_sim_ring_type {
	OFFLOAD_SIM_RING_TYPE_RX = 0,
	OFFLOAD_SIM_RING_TYPE_TX_CPL = 1,
	OFFLOAD_SIM_RING_TYPE_TX = 2,
	OFFLOAD_SIM_RING_TYPE_RX_REFILL = 3,
};

/**
 * struct offload_sim_irq_ctx - IRQ context for interrupt handler
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @ring_id: Ring ID (0 or 1)
 * @ring_type: Ring type (RX or TX completion)
 * @irq_configured: Flag indicating if IRQ has been successfully configured
 */
struct offload_sim_irq_ctx {
	struct dp_dal_sim_ctx *dal_sim_ctx;
	uint8_t ring_id;
	enum offload_sim_ring_type ring_type;
	bool irq_configured;
};

/**
 * struct dp_dal_offload_sim_ctx - Offload simulation context
 * @hal_soc: Vendor HAL SoC structure
 * @rx_ring_hal_srng: Array of vendor HAL SRNG structures for RX rings
 * @tx_cmpl_ring_hal_srng: Array of vendor HAL SRNG structures for
 * TX completion rings
 * @tx_ring_hal_srng: Array of vendor HAL SRNG structures for TX rings
 * @rx_refill_ring_hal_srng: Vendor HAL SRNG structure for RX refill ring
 * @rx_irq_ctx: IRQ context for RX rings
 * @tx_cpl_irq_ctx: IRQ context for TX completion rings
 * @suspend_msg_irq_num: IRQ number for suspend message
 * @offload_sim_ctx_initialized: Flag indicating if context is initialized
 * @dev_base_addr: device base address
 *
 * This structure maintains the offload simulation context with vendor HAL
 * ring structures that are populated from dal_sim_srng structures.
 */
struct dp_dal_offload_sim_ctx {
	struct dal_vndr_hal_soc hal_soc;
	struct dal_vndr_hal_srng rx_ring_hal_srng[DAL_RX_RINGS_MAX];
	struct dal_vndr_hal_srng tx_cmpl_ring_hal_srng[DAL_TX_RINGS_MAX];
	struct dal_vndr_hal_srng tx_ring_hal_srng[DAL_TX_RINGS_MAX];
	struct dal_vndr_hal_srng rx_refill_ring_hal_srng;
	struct offload_sim_irq_ctx rx_irq_ctx[DAL_RX_RINGS_MAX];
	struct offload_sim_irq_ctx tx_cpl_irq_ctx[DAL_TX_RINGS_MAX];
	int suspend_msg_irq_num;
	bool offload_sim_ctx_initialized;
	void *dev_base_addr;
};

/**
 * dp_dal_offload_sim_init() - Initialize offload simulation context
 * @dal_sim_ctx: Pointer to DAL simulation context
 *
 * This function allocates and initializes the offload simulation context.
 * It assigns values from dal_sim_srng structures (present in dal_sim_ctx)
 * to dal_vndr_hal_srng structures in the offload_sim_ctx.
 *
 * Return: 0 on success, error code on failure
 */
int dp_dal_offload_sim_init(struct dp_dal_sim_ctx *dal_sim_ctx);

/**
 * dp_dal_offload_sim_deinit() - Deinitialize offload simulation context
 * @dal_sim_ctx: Pointer to DAL simulation context
 *
 * This function deinitializes and frees the offload simulation context.
 */
void dp_dal_offload_sim_deinit(struct dp_dal_sim_ctx *dal_sim_ctx);

/**
 * dp_dal_offload_sim_request_irq() - Register IRQs for offload simulation
 * @dal_sim_ctx: Pointer to DAL simulation context
 *
 * This function registers interrupt handlers for RX and TX completion rings
 * using platform-specific IRQ registration (pfrm_request_irq).
 *
 * Return: 0 on success, error code on failure
 */
int dp_dal_offload_sim_request_irq(struct dp_dal_sim_ctx *dal_sim_ctx);
/**
 * dp_dal_offload_sim_free_irq() - Free IRQs for offload simulation
 * @dal_sim_ctx: Pointer to DAL simulation context
 *
 * This function frees interrupt handlers for RX and TX completion rings.
 */
void dp_dal_offload_sim_free_irq(struct dp_dal_sim_ctx *dal_sim_ctx);

/**
 * dp_dal_offload_sim_disable_irq() - Disable IRQs for offload simulation
 * @dal_sim_ctx: Pointer to DAL simulation context
 *
 * This function disables interrupt handlers for RX and TX completion
 */
void dp_dal_offload_sim_disable_irq(struct dp_dal_sim_ctx *dal_sim_ctx);

/**
 * dp_dal_offload_sim_enable_ring_irq() - Enable irq for particular ring type
 *					  and ring_num.
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @ring_type: ring type (rx or tx_completion)
 * @ring_id: ring id
 * This function enables irq for a partticular ring type and ring num.
 *
 * Return: None
 */
static inline void
dp_dal_offload_sim_enable_ring_irq(struct dp_dal_sim_ctx *dal_sim_ctx,
				   int ring_type, int ring_id)
{
	int irq_num;

	if (!dal_sim_ctx) {
		dp_err("NULL sim context");
		return;
	}

	irq_num = ring_type == OFFLOAD_SIM_RING_TYPE_RX ?
				dal_sim_ctx->rx_ring[ring_id].irq_num :
				dal_sim_ctx->tx_cmpl_ring[ring_id].irq_num;
	pfrm_enable_irq(dal_sim_ctx->dev, irq_num);
}

/**
 * dp_dal_offload_sim_disable_ring_irq() - Disable irq for particular ring type
 *					   and ring_num.
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @ring_type: ring type (rx or tx_completion)
 * @ring_id: ring id
 * This function disables irq for a partticular ring type and ring num.
 *
 * Return: None
 */
static inline void
dp_dal_offload_sim_disable_ring_irq(struct dp_dal_sim_ctx *dal_sim_ctx,
				    int ring_type, int ring_id)
{
	int irq_num;

	if (!dal_sim_ctx) {
		dp_err("NULL sim context");
		return;
	}

	irq_num = ring_type == OFFLOAD_SIM_RING_TYPE_RX ?
			       dal_sim_ctx->rx_ring[ring_id].irq_num :
			       dal_sim_ctx->tx_cmpl_ring[ring_id].irq_num;
	pfrm_disable_irq_nosync(dal_sim_ctx->dev, irq_num);
}

/**
 * dp_dal_offload_sim_tx_hw_enqueue() - Enqueue TX descriptor to hardware ring
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @ring_id: Ring ID for TCL descriptor enqueue
 * @desc: Pointer to the cached TCL descriptor
 * @tx_metadata: Pointer to TX metadata
 *
 * This wrapper function handles ring access start/end and syncs the TX
 * descriptor content to hardware. It encapsulates the ring access logic
 * for TX descriptor enqueue operations.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_dal_offload_sim_tx_hw_enqueue(
			struct dp_dal_sim_ctx *dal_sim_ctx,
			u8 ring_id,
			void *desc,
			void *tx_metadata);

/**
 * dp_dal_offload_sim_get_reo_desc() - Get REO descriptors from RX ring
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @ring_id: Ring ID for REO descriptor retrieval
 * @budget: Maximum number of descriptors to retrieve
 *
 * This wrapper function handles ring locking, access start/end, and
 * retrieves REO descriptors from the specified RX ring up to the budget limit.
 * It encapsulates all HAL ring operations for RX descriptor retrieval.
 * The descriptors are enqueued to the DAL sim descriptor list for the ring.
 *
 * Return: Number of descriptors retrieved and enqueued, negative error code
 * on failure
 */
int dp_dal_offload_sim_get_reo_desc(
				struct dp_dal_sim_ctx *dal_sim_ctx,
				u16 ring_id,
				u32 budget);
/**
 * dp_dal_offload_sim_get_tx_compl_desc() - Get TX completion descriptors from
 * Tx compl ring
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @ring_id: Ring ID for TX completion descriptor retrieval
 * @budget: Maximum number of descriptors to retrieve
 *
 * This wrapper function handles ring locking, access start/end, and
 * retrieves TX completion descriptors from the specified WBM ring up to
 * the budget limit. It encapsulates all HAL ring operations for TX
 * completion descriptor retrieval. The descriptors are enqueued to the
 * DAL sim descriptor list for the ring.
 *
 * Return: Number of descriptors retrieved and enqueued,
 * negative error code on failure
 */
int dp_dal_offload_sim_get_tx_compl_desc(
				struct dp_dal_sim_ctx *dal_sim_ctx,
				u16 ring_id,
				u32 budget);

/**
 * dp_dal_offload_sim_get_rx_refill_avail_entries() - Get available entries in
 * refill ring
 * @dal_sim_ctx: Pointer to DAL simulation context
 *
 * This wrapper function calculates and returns the number of available entries
 * in the RX refill ring. It uses the vendor HAL API to get
 * the available entries.
 *
 * Return: Number of available entries in the RX refill ring,
 * negative error code on failure
 */
uint32_t dp_dal_offload_sim_get_rx_refill_avail_entries(
			struct dp_dal_sim_ctx *dal_sim_ctx);

/**
 * dp_dal_offload_sim_rxbm_sync() - Sync RX buffer manager descriptors to
 * refill ring
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @cnt: Number of descriptors to sync
 * @rx_buff: Array of RX buffer pointers
 *
 * This wrapper function handles ring locking, access start/end, and copies
 * RX buffer manager descriptors one by one to the RX refill ring entries.
 * It encapsulates all HAL ring operations for RX buffer replenishment.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_dal_offload_sim_rxbm_sync(
				struct dp_dal_sim_ctx *dal_sim_ctx,
				u32 cnt,
				void **rx_buff);

/**
 * dp_dal_offload_sim_fetch_current_hp_tp() - Fetch current HP and TP values
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @hp: Pointer to store head pointer value
 * @tp: Pointer to store tail pointer value
 * @ring_type: Offload simulation ring type
 * @ring_id: Ring ID
 *
 * This function fetches the current head pointer (HP) and tail pointer (TP)
 * values for a specified ring in the offload simulation context.It calls
 * dal vendor hal api to get hp tp of the ring.
 *
 * Return: 0 on success, negative error code on failure
 */
int dp_dal_offload_sim_fetch_current_hp_tp(
				struct dp_dal_sim_ctx *dal_sim_ctx,
				uint32_t *hp,
				uint32_t *tp,
				int ring_type,
				int ring_id);

/**
 * dp_dal_offload_sim_sync_refill_ring_hp() - sync refill ring hp from DDR
 * @dal_sim_ctx: DAL SIM context
 *
 * This function sync the HP to refill ring from DDR
 *
 * Return: none
 */
void dp_dal_offload_sim_sync_refill_ring_hp(struct dp_dal_sim_ctx *dal_sim_ctx);

/**
 * dp_dal_offload_sim_sync_refill_ring_hp_to_ddr() - sync refill ring local hp
 * to DDR
 * @sim_ctx: DAL SIM context
 *
 * This function sync the refill ring local HP to DDR
 *
 * Return: none
 */
void
dp_dal_offload_sim_sync_refill_ring_hp_to_ddr(struct dp_dal_sim_ctx *sim_ctx);

/**
 * dp_dal_offload_sim_handle_msg() - Handle message from DAL simulator
 * @dal_sim_ctx: Pointer to DAL simulation context
 * @msg_type: Type of message to handle (INTF_PAUSE, INTF_RESUME)
 *
 * This function processes messages sent from the DAL simulator (Host) to
 * the Offload Engine (FW/OLE). It simulates the FW behavior by checking
 * conditions and returning an ACK or NACK status.
 *
 * Return: Message response (ACK/NACK with type) or negative error code
 */
int dp_dal_offload_sim_handle_msg(struct dp_dal_sim_ctx *dal_sim_ctx,
				  uint32_t msg_type);

#endif /* FEATURE_DAL_DP_SUPPORT */
#endif /* DP_DAL_OFFLOAD_SIM_H */
