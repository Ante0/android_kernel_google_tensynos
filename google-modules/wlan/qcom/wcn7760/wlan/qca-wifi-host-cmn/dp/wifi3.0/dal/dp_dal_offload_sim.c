/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "dp_dal_offload_sim.h"
#include "dp_dal_sim.h"
#include <qdf_mem.h>
#include "dal_vndr_hal_be.h"
#include "dal_vndr_hal_internal.h"

#ifdef FEATURE_DP_DAL_SIM

/**
 * dp_dal_offload_sim_ring_access_start() - Start ring access with lock
 * @offload_ctx: Pointer to offload simulation context
 * @ring: Pointer to vendor HAL SRNG structure
 *
 * This function takes the ring lock and calls the vendor HAL API to start
 * ring access. It encapsulates the locking and ring access start operations
 * to ensure consistent behavior across all ring access operations.
 */
static inline void
dp_dal_offload_sim_ring_access_start(struct dp_dal_offload_sim_ctx *offload_ctx,
				     struct dal_vndr_hal_srng *ring)
{
	if (!offload_ctx || !ring) {
		dp_err("NULL context in ring_access_start");
		return;
	}

	/* Take the ring lock */
	DAL_VNDR_SRNG_LOCK(&ring->lock);

	/* Begin ring access using vendor HAL API */
	dal_vndr_hal_srng_access_start(&offload_ctx->hal_soc, ring);
}

/**
 * dp_dal_offload_sim_ring_access_end() - End ring access and release lock
 * @offload_ctx: Pointer to offload simulation context
 * @ring: Pointer to vendor HAL SRNG structure
 *
 * This function calls the vendor HAL API to end ring access and releases
 * the ring lock. It encapsulates the ring access end and unlock operations
 * to ensure consistent behavior across all ring access operations.
 */
static inline void
dp_dal_offload_sim_ring_access_end(struct dp_dal_offload_sim_ctx *offload_ctx,
				   struct dal_vndr_hal_srng *ring)
{
	if (!offload_ctx || !ring) {
		dp_err("NULL context in ring_access_end");
		return;
	}

	/* End ring access using vendor HAL API */
	dal_vndr_hal_srng_access_end(&offload_ctx->hal_soc, ring);

	/* Release the ring lock */
	DAL_VNDR_SRNG_UNLOCK(&ring->lock);
}

static irqreturn_t dp_dal_offload_sim_suspend_msg_handler(int irq, void *arg)
{
	struct dp_dal_sim_ctx *sim_ctx = (struct dp_dal_sim_ctx *)arg;
	uint32_t val = 0;

	if (!sim_ctx) {
		dp_err("NULL sim context in suspend msg handler");
		return IRQ_HANDLED;
	}

	if (sim_ctx->suspend_msg_data_vaddr)
		val = *(uint32_t *)sim_ctx->suspend_msg_data_vaddr;

	dp_info("Suspend msg interrupt received, value=0x%x", val);

	/* Signal the event to unblock any waiters */
	qdf_event_set(&sim_ctx->suspend_msg_event);

	return IRQ_HANDLED;
}

static inline void
dp_dal_offload_sim_init_d3_wow(struct dp_dal_offload_sim_ctx *offload_ctx,
			       struct dp_dal_sim_ctx *dal_sim_ctx)
{
	offload_ctx->dev_base_addr = dal_sim_ctx->dev_base_addr;
}

static inline int
dp_dal_offload_sim_request_suspend_msg_irq(struct dp_dal_sim_ctx *dal_sim_ctx,
					struct dp_dal_offload_sim_ctx *offload_ctx)
{
	int irq_num = dal_sim_ctx->suspend_msg_irq_num;
	int ret;

	if (irq_num <= 0) {
		dp_err("Invalid suspend msg IRQ number %d", irq_num);
		return -EINVAL;
	}

	offload_ctx->suspend_msg_irq_num = irq_num;

	ret = pfrm_request_irq(
			dal_sim_ctx->dev,
			irq_num,
			dp_dal_offload_sim_suspend_msg_handler,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			"dal_offload_sim_suspend_msg",
			dal_sim_ctx);
	if (ret) {
		dp_err("Failed irq register suspend msg IRQ, ret=%d", ret);
		return ret;
	}

	dp_info("Registered suspend msg IRQ %d (from sim_ctx)",
		irq_num);

	return 0;
}

static inline void
dp_dal_offload_sim_free_suspend_msg_irq(struct dp_dal_sim_ctx *dal_sim_ctx,
				     struct dp_dal_offload_sim_ctx *offload_ctx)
{
	if (offload_ctx->suspend_msg_irq_num > 0) {
		pfrm_free_irq(dal_sim_ctx->dev,
			      offload_ctx->suspend_msg_irq_num,
			      dal_sim_ctx);
		offload_ctx->suspend_msg_irq_num = 0;
		dp_debug("Freed suspend msg IRQ");
	}
}

#ifdef DAL_OFFLOAD_SIM
/**
 * dp_dal_offload_sim_hal_addrs_params_init() - Form hal_srng address parameters
 * for offload simulation.
 * @offload_sim_ctx: Pointer to offload simulation context
 * @hal_srng: Pointer to destination dal_vndr_hal_srng structure
 * @sim_srng: Pointer to source dal_sim_srng structure
 *
 * hp/tp address are physical address for lmac rings and for umac rings
 * they are offset from BAR. For HP/TP addr, we need to add base address of
 * device to get the correct virtual address. Google offload engine has to
 * use BAR address to form correct physical address. Vendor HAL APIS are
 * agnostic of the address being used in the dal_vndr_hal_srng structure.
 * Care must be taken to assign correct address based on simulation mode or
 * real use case scenario.
 *
 * Return: None
 *
 */
static inline void
dp_dal_offload_sim_hal_addrs_params_init(struct dp_dal_offload_sim_ctx *offload_sim_ctx,
					 struct dal_vndr_hal_srng *hal_srng,
					 struct dal_sim_srng *sim_srng)
{
	/* Here in simulation mode ring_base_addr is filled with virtual address
	 * so that vendor hal apis can use directly ring_base_addr to access
	 * descriptors. In real hardware, ring_base_addr will be filled with
	 * physical address.
	 */
	hal_srng->ring_base_addr = sim_srng->ring_base_vaddr;
	hal_srng->ring_base_paddr = sim_srng->ring_base_paddr;

	if (sim_srng->ring_dir == DAL_VNDR_HAL_SRNG_SRC_RING) {
		hal_srng->u.src_ring.hp = sim_srng->u.src_ring.hp;

		if (sim_srng->lmac_ring)
			/* In offload simulation we are filling virtual address
			 * in  hp_addr and tp_addr field. Usage for offload
			 * engine can vary.
			 *
			 * In case of lmac rings, hp_addr is virtual address
			 * in case of offload engine simulation. Real offload
			 * engine can use physical address.
			 */
			hal_srng->u.src_ring.hp_addr =
				(uint32_t *)(sim_srng->u.src_ring.hp_addr);
		else
			hal_srng->u.src_ring.hp_addr =
				sim_srng->u.src_ring.hp_addr +
				offload_sim_ctx->hal_soc.dev_base_addr;

		hal_srng->u.src_ring.tp_addr =
			(uint32_t *)(sim_srng->u.src_ring.tp_addr);
	} else {
		hal_srng->u.dst_ring.tp = sim_srng->u.dst_ring.tp;

		if (sim_srng->lmac_ring)
			hal_srng->u.dst_ring.tp_addr =
				(uint32_t *)(sim_srng->u.dst_ring.tp_addr);
		else
			hal_srng->u.dst_ring.tp_addr =
					sim_srng->u.dst_ring.tp_addr +
					offload_sim_ctx->hal_soc.dev_base_addr;

		hal_srng->u.dst_ring.hp_addr =
				(uint32_t *)(sim_srng->u.dst_ring.hp_addr);
	}
}
#else
static inline void
dp_dal_offload_sim_hal_addrs_params_init(struct dp_dal_offload_sim_ctx *offload_sim_ctx,
					 struct dal_vndr_hal_srng *hal_srng,
					 struct dal_sim_srng *sim_srng)
{
}
#endif

/**
 * dp_dal_offload_sim_overwrite_tx_desc() - Overwrite tx descriptor
 * for offload simulation.
 * @hal_soc_hdl: hal soc handle
 * @txdesc: tx descriptor
 *
 * This function overwrites tx descriptor sent by wlan host to
 * offload sim with those field values whose set apis are provided
 * in dal vendor hal.
 *
 * Return: None
 *
 */
static inline void
dp_dal_offload_sim_overwrite_tx_desc(void *hal_soc_hdl, void *txdesc)
{
	uint8_t lmac_id;
	dma_addr_t paddr;
	uint32_t paddr_lo, paddr_hi;
	uint8_t rbm_id;
	uint32_t desc_id;
	uint8_t buf_type;
	uint16_t data_length;
	uint8_t buf_offset;
	uint8_t l3_checksum_en;
	uint8_t l4_checksum_en;
	uint8_t bank_id;
	uint8_t vdev_id;
	uint8_t hlos_tid;
	uint8_t flow_override_enable;
	uint8_t flow_override;
	uint8_t who_classify_info_sel;
	uint8_t tx_notify_frame;

	if (!hal_soc_hdl || !txdesc) {
		dp_err_rl("Invalid parameters");
		return;
	}

	/* Read all fields from TX descriptor using HAL_TX_DESC_GET APIs */
	lmac_id = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD, PMAC_ID);

	paddr_lo = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
					    BUF_ADDR_INFO_BUFFER_ADDR_31_0);
	paddr_hi = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
					    BUF_ADDR_INFO_BUFFER_ADDR_39_32);
	paddr = (dma_addr_t)paddr_lo | (((dma_addr_t)paddr_hi) << 32);

	rbm_id = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
					  BUF_ADDR_INFO_RETURN_BUFFER_MANAGER);

	desc_id = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
					   BUF_ADDR_INFO_SW_BUFFER_COOKIE);

	buf_type = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
					    BUF_OR_EXT_DESC_TYPE);

	data_length = DAL_VNDR_HAL_TX_DESC_GET(txdesc,
					       TCL_DATA_CMD, DATA_LENGTH);

	buf_offset = DAL_VNDR_HAL_TX_DESC_GET(txdesc,
					      TCL_DATA_CMD, PACKET_OFFSET);

	l3_checksum_en = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
						  IPV4_CHECKSUM_EN);

	l4_checksum_en = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
						  TCP_OVER_IPV4_CHECKSUM_EN) |
			 DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
						  TCP_OVER_IPV6_CHECKSUM_EN) |
			 DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
						  UDP_OVER_IPV4_CHECKSUM_EN) |
			 DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
						  UDP_OVER_IPV6_CHECKSUM_EN);

	bank_id = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD, BANK_ID);

	vdev_id = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD, VDEV_ID);

	hlos_tid = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD, HLOS_TID);

	flow_override_enable = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
							FLOW_OVERRIDE_ENABLE);

	flow_override = DAL_VNDR_HAL_TX_DESC_GET(txdesc,
						 TCL_DATA_CMD, FLOW_OVERRIDE);

	who_classify_info_sel = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
							 WHO_CLASSIFY_INFO_SEL);

	tx_notify_frame = DAL_VNDR_HAL_TX_DESC_GET(txdesc, TCL_DATA_CMD,
						   TX_NOTIFY_FRAME);
	/* Overwrite all fields using DAL VNDR HAL set APIs */
	dal_vndr_hal_tx_desc_set_lmac_id(hal_soc_hdl, txdesc, lmac_id);
	dal_vndr_hal_tx_desc_set_buf_addr(hal_soc_hdl, txdesc, paddr,
					  rbm_id, desc_id, buf_type);
	dal_vndr_hal_tx_desc_set_buf_length(hal_soc_hdl, txdesc, data_length);
	dal_vndr_hal_tx_desc_set_buf_offset(hal_soc_hdl, txdesc, buf_offset);
	dal_vndr_hal_tx_desc_set_l3_checksum_en(hal_soc_hdl, txdesc,
						l3_checksum_en);
	dal_vndr_hal_tx_desc_set_l4_checksum_en(hal_soc_hdl, txdesc,
						l4_checksum_en);
	dal_vndr_hal_tx_desc_set_bank_id(hal_soc_hdl, txdesc, bank_id);
	dal_vndr_hal_tx_desc_set_vdev_id(hal_soc_hdl, txdesc, vdev_id);
	dal_vndr_hal_tx_desc_set_hlos_tid(hal_soc_hdl, txdesc, hlos_tid);
	dal_vndr_hal_tx_desc_set_flow_override_enable(hal_soc_hdl, txdesc,
						      flow_override_enable);
	dal_vndr_hal_tx_desc_set_flow_override(hal_soc_hdl, txdesc,
					       flow_override);
	dal_vndr_hal_tx_desc_set_who_classify_info_sel(hal_soc_hdl, txdesc,
						       who_classify_info_sel);
	dal_vndr_hal_tx_desc_set_tx_notify_frame(hal_soc_hdl, txdesc,
						 tx_notify_frame);
}

/**
 * dp_dal_offload_sim_hal_ring_init() - Init dal_vndr_hal_srng structure.
 * @offload_sim_ctx: offload sim ctx
 * @hal_srng: Pointer to destination dal_vndr_hal_srng structure
 * @sim_srng: Pointer to source dal_sim_srng structure
 *
 * Copies ring information field by field from dal_sim_srng to dal_vndr_hal_srng
 */
static void
dp_dal_offload_sim_hal_ring_init(struct dp_dal_offload_sim_ctx *offload_sim_ctx,
				 struct dal_vndr_hal_srng *hal_srng,
				 struct dal_sim_srng *sim_srng)
{
	/* Copy basic ring information */
	hal_srng->ring_id = sim_srng->hal_ring_id;
	hal_srng->num_entries = sim_srng->num_entries;
	hal_srng->ring_size = sim_srng->ring_size;
	hal_srng->ring_size_mask = sim_srng->ring_size_mask;
	hal_srng->lmac_ring = sim_srng->lmac_ring;
	hal_srng->entry_size = sim_srng->entry_size;
	hal_srng->msi_addr = sim_srng->msi_addr;
	hal_srng->msi_data = sim_srng->msi_data;
	hal_srng->ring_type = sim_srng->ring_type;
	hal_srng->ring_dir = sim_srng->ring_dir;
	hal_srng->irq_num = sim_srng->irq_num;
	hal_srng->hal_soc = &offload_sim_ctx->hal_soc;

	/* copy ring address information from sim ring to hal ring */
	dp_dal_offload_sim_hal_addrs_params_init(
		offload_sim_ctx, hal_srng, sim_srng);
	DAL_VNDR_SRNG_LOCK_INIT(&hal_srng->lock);
}

int dp_dal_offload_sim_init(struct dp_dal_sim_ctx *dal_sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	int i;
	int status = 0;

	if (!dal_sim_ctx) {
		dp_err("NULL DAL sim context");
		return -EINVAL;
	}

	offload_ctx = qdf_mem_malloc(sizeof(*offload_ctx));
	if (!offload_ctx) {
		dp_err("Failed to allocate offload sim context");
		return -ENOMEM;
	}

	qdf_mem_zero(offload_ctx, sizeof(*offload_ctx));

	offload_ctx->hal_soc.ops = qdf_mem_malloc(
					sizeof(*offload_ctx->hal_soc.ops));
	if (!offload_ctx->hal_soc.ops) {
		status = -ENOMEM;
		goto free_offload_ctx;
	}

	/* Store the dev base addr here from dal_sim_ctx.
	 * Google DAL has to assign BAR address here.
	 */
	offload_ctx->hal_soc.dev_base_addr = dal_sim_ctx->dev_base_addr;
	dp_dal_offload_sim_init_d3_wow(offload_ctx, dal_sim_ctx);

	/* Copy dal_vndr_hal_srng from dal_sim_srng structures */
	/* RX rings */
	for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
		dp_dal_offload_sim_hal_ring_init(
					offload_ctx,
					&offload_ctx->rx_ring_hal_srng[i],
					&dal_sim_ctx->rx_ring[i]);
	}

	/* TX completion rings */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		dp_dal_offload_sim_hal_ring_init(
					offload_ctx,
					&offload_ctx->tx_cmpl_ring_hal_srng[i],
					&dal_sim_ctx->tx_cmpl_ring[i]);
	}

	/* TX rings */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		dp_dal_offload_sim_hal_ring_init(
					offload_ctx,
					&offload_ctx->tx_ring_hal_srng[i],
					&dal_sim_ctx->tx_ring[i]);
	}

	/* RX refill ring */
	dp_dal_offload_sim_hal_ring_init(offload_ctx,
					 &offload_ctx->rx_refill_ring_hal_srng,
					 &dal_sim_ctx->rx_refill_ring);

	/* Vendor HAL ops can be overridden here if needed with target_type*/
	dal_vndr_hal_ops_attach(&offload_ctx->hal_soc);

	offload_ctx->offload_sim_ctx_initialized = true;
	dal_sim_ctx->offload_sim_ctx = offload_ctx;

	return 0;

free_offload_ctx:
	qdf_mem_free(offload_ctx);
	return status;
}

void dp_dal_offload_sim_deinit(struct dp_dal_sim_ctx *dal_sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;

	if (!dal_sim_ctx) {
		dp_warn("NULL DAL sim context in deinit");
		return;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_warn("NULL offload sim context in deinit");
		return;
	}

	dp_info("Deinitializing offload simulation context");

	dp_dal_offload_sim_free_irq(dal_sim_ctx);

	offload_ctx->offload_sim_ctx_initialized = false;
	qdf_mem_free(offload_ctx->hal_soc.ops);
	qdf_mem_free(offload_ctx);

	dal_sim_ctx->offload_sim_ctx = NULL;
	dp_info("Offload simulation context deinitialized successfully");
}

/**
 * dp_dal_offload_sim_interrupt_handler() - Interrupt handler for offload sim
 * @irq: IRQ number
 * @arg: Pointer to offload_sim_irq_ctx structure
 *
 * This function is called when an interrupt is received. It calls
 * dp_dal_sim_schedule_work to queue work for processing the interrupt.
 *
 * Return: IRQ_HANDLED
 */
static irqreturn_t dp_dal_offload_sim_interrupt_handler(int irq, void *arg)
{
	struct offload_sim_irq_ctx *irq_ctx = (struct offload_sim_irq_ctx *)arg;
	struct dp_dal_sim_ctx *sim_ctx;

	if (!irq_ctx) {
		dp_err("NULL IRQ context in interrupt handler");
		return IRQ_HANDLED;
	}

	sim_ctx = irq_ctx->dal_sim_ctx;
	if (!sim_ctx) {
		dp_err("NULL sim context in interrupt handler");
		return IRQ_HANDLED;
	}

	dp_debug("Interrupt %d received for ring_type=%d, ring_id=%d",
		 irq, irq_ctx->ring_type, irq_ctx->ring_id);

	dp_dal_offload_sim_disable_ring_irq(sim_ctx, irq_ctx->ring_type,
					    irq_ctx->ring_id);

	/* Call dal sim api to queue work for processing interrupt */
	dp_dal_sim_schedule_work(arg);

	return IRQ_HANDLED;
}

int dp_dal_offload_sim_request_irq(struct dp_dal_sim_ctx *dal_sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	int i, ret;

	if (!dal_sim_ctx) {
		dp_err("NULL DAL sim context in request_irq");
		return -EINVAL;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload sim context in request_irq");
		return -EINVAL;
	}

	dp_info("Registering IRQs for offload simulation");

	/* Register RX ring IRQs */
	for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
		offload_ctx->rx_irq_ctx[i].dal_sim_ctx = dal_sim_ctx;
		offload_ctx->rx_irq_ctx[i].ring_id = i;
		offload_ctx->rx_irq_ctx[i].ring_type = OFFLOAD_SIM_RING_TYPE_RX;
		offload_ctx->rx_irq_ctx[i].irq_configured = false;

		ret = pfrm_request_irq(
			dal_sim_ctx->dev,
			dal_sim_ctx->rx_ring[i].irq_num,
			dp_dal_offload_sim_interrupt_handler,
			IRQF_SHARED | IRQF_NO_SUSPEND,
			"dal_offload_sim_wlan_rx",
			&offload_ctx->rx_irq_ctx[i]);
		if (ret) {
			dp_err("Failed irq register RX ring %d, ret=%d",
			       i, ret);
			goto free_irqs;
		}

		offload_ctx->rx_irq_ctx[i].irq_configured = true;
		dp_info("Registered RX IRQ %d for ring %d",
			dal_sim_ctx->rx_ring[i].irq_num, i);
	}

	/* Register TX completion ring IRQs */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		offload_ctx->tx_cpl_irq_ctx[i].dal_sim_ctx = dal_sim_ctx;
		offload_ctx->tx_cpl_irq_ctx[i].ring_id = i;
		offload_ctx->tx_cpl_irq_ctx[i].ring_type =
						OFFLOAD_SIM_RING_TYPE_TX_CPL;
		offload_ctx->tx_cpl_irq_ctx[i].irq_configured = false;

		ret = pfrm_request_irq(
				dal_sim_ctx->dev,
				dal_sim_ctx->tx_cmpl_ring[i].irq_num,
				dp_dal_offload_sim_interrupt_handler,
				IRQF_SHARED | IRQF_NO_SUSPEND,
				"dal_offload_sim_wlan_tx",
				&offload_ctx->tx_cpl_irq_ctx[i]);
		if (ret) {
			dp_err("Failed irq register TX compl ring %d, ret=%d",
			       i, ret);
			goto free_irqs;
		}

		offload_ctx->tx_cpl_irq_ctx[i].irq_configured = true;
		dp_info("Registered TX completion IRQ %d for ring %d",
			dal_sim_ctx->tx_cmpl_ring[i].irq_num, i);
	}

	ret = dp_dal_offload_sim_request_suspend_msg_irq(dal_sim_ctx, offload_ctx);
	if (ret)
		goto free_irqs;

	dp_info("IRQ registration complete");
	return 0;

free_irqs:
	dp_dal_offload_sim_free_irq(dal_sim_ctx);
	return ret;
}

void dp_dal_offload_sim_free_irq(struct dp_dal_sim_ctx *dal_sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	int i;

	if (!dal_sim_ctx) {
		dp_warn("NULL DAL sim context in free_irq");
		return;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_warn("NULL offload sim context in free_irq");
		return;
	}

	/* Free RX ring IRQs */
	for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
		if (offload_ctx->rx_irq_ctx[i].irq_configured) {
			pfrm_free_irq(dal_sim_ctx->dev,
				      dal_sim_ctx->rx_ring[i].irq_num,
				      &offload_ctx->rx_irq_ctx[i]);
			offload_ctx->rx_irq_ctx[i].irq_configured = false;
			dp_debug("Freed RX IRQ %d for ring %d",
				 dal_sim_ctx->rx_ring[i].irq_num, i);
		}
	}

	/* Free TX completion ring IRQs */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		if (offload_ctx->tx_cpl_irq_ctx[i].irq_configured) {
			pfrm_free_irq(dal_sim_ctx->dev,
				      dal_sim_ctx->tx_cmpl_ring[i].irq_num,
				       &offload_ctx->tx_cpl_irq_ctx[i]);
			offload_ctx->tx_cpl_irq_ctx[i].irq_configured = false;
			dp_debug("Freed TX completion IRQ %d for ring %d",
				 dal_sim_ctx->tx_cmpl_ring[i].irq_num, i);
		}
	}

	dp_dal_offload_sim_free_suspend_msg_irq(dal_sim_ctx, offload_ctx);
}

void dp_dal_offload_sim_disable_irq(struct dp_dal_sim_ctx *dal_sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	int i;

	if (!dal_sim_ctx) {
		dp_warn("NULL DAL sim context in disable_irq");
		return;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_warn("NULL offload sim context in disable_irq");
		return;
	}

	/* Disable RX ring IRQs */
	for (i = 0; i < DAL_RX_RINGS_MAX; i++) {
		if (offload_ctx->rx_irq_ctx[i].irq_configured) {
			pfrm_disable_irq(dal_sim_ctx->dev,
					 dal_sim_ctx->rx_ring[i].irq_num);
			dp_debug("Disabled RX IRQ %d for ring %d",
				 dal_sim_ctx->rx_ring[i].irq_num, i);
		}
	}

	/* Disable TX completion ring IRQs */
	for (i = 0; i < DAL_TX_RINGS_MAX; i++) {
		if (offload_ctx->tx_cpl_irq_ctx[i].irq_configured) {
			pfrm_disable_irq(dal_sim_ctx->dev,
					 dal_sim_ctx->tx_cmpl_ring[i].irq_num);
			dp_debug("Disabled TX completion IRQ %d for ring %d",
				 dal_sim_ctx->tx_cmpl_ring[i].irq_num, i);
		}
	}

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
int dp_dal_offload_sim_tx_hw_enqueue(struct dp_dal_sim_ctx *dal_sim_ctx,
				     u8 ring_id, void *desc, void *tx_metadata)
{
	struct dp_dal_ctx *dal_ctx;
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *tcl_ring;
	void *hal_tx_desc;
	int ret = 0;

	if (!dal_sim_ctx) {
		dp_err("NULL simulator context in tx_hw_enqueue");
		return -EINVAL;
	}

	dal_ctx = (struct dp_dal_ctx *)dal_sim_ctx->dp_dal_ctx;
	if (!dal_ctx) {
		dp_err("NULL DAL context in tx_hw_enqueue");
		return -EINVAL;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context in tx_hw_enqueue");
		return -EINVAL;
	}

	/* Get TCL ring for the specified ring_id from offload context */
	tcl_ring = &offload_ctx->tx_ring_hal_srng[ring_id];

	dp_debug("Enqueuing TX descriptor for ring_id %u", ring_id);

	/* Begin ring access with lock */
	dp_dal_offload_sim_ring_access_start(offload_ctx, tcl_ring);

	/* Get next available descriptor slot in TCL ring */
	hal_tx_desc = dal_vndr_hal_srng_src_get_next(&offload_ctx->hal_soc,
						     tcl_ring);
	if (!hal_tx_desc) {
		dp_verbose_debug("TCL ring full for ring_id %u", ring_id);
		ret = -ENOSPC;
		goto exit;
	}

	/* Overwrite descriptor if vendor HAL is configured */
	if (dp_dal_sim_cfg_use_vndr_hal(dal_sim_ctx))
		dp_dal_offload_sim_overwrite_tx_desc(&offload_ctx->hal_soc,
						     desc);

	/* Sync cached descriptor content to HW descriptor */
	dal_vndr_hal_tx_desc_sync(desc, hal_tx_desc,
				  DAL_VNDR_HAL_TX_DESC_LEN_BYTES);

	dp_debug("TX descriptor enqueued successfully for ring_id %u", ring_id);

exit:
	/* End ring access and release lock */
	dp_dal_offload_sim_ring_access_end(offload_ctx, tcl_ring);

	return ret;
}

int dp_dal_offload_sim_get_reo_desc(struct dp_dal_sim_ctx *dal_sim_ctx,
				    u16 ring_id, u32 budget)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *reo_ring;
	void *reo_desc;
	u32 retrieved = 0;
	int ret;

	if (!dal_sim_ctx) {
		dp_err("NULL simulator context in get_reo_desc");
		return -EINVAL;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context in get_reo_desc");
		return -EINVAL;
	}

	/* Get REO ring for the specified ring_id */
	reo_ring = &offload_ctx->rx_ring_hal_srng[ring_id];

	dp_debug("Getting REO descriptors for ring_id %u with budget %u",
		 ring_id, budget);

	/* Begin ring access with lock */
	dp_dal_offload_sim_ring_access_start(offload_ctx, reo_ring);

	/* Start accessing SW2SW ring with lock */
	dp_dal_sim_sw2sw_ring_access_start(
				&dal_sim_ctx->rx_sw2sw_ring[ring_id]);

	/* Reap REO descriptors until budget is reached or no more descriptor */
	while (retrieved < budget) {
		/* Get next REO descriptor from the ring */
		reo_desc = dal_vndr_hal_srng_dst_get_next(&offload_ctx->hal_soc,
							  reo_ring);
		if (!reo_desc) {
			/* No more descriptors in this ring */
			break;
		}

		/* Enqueue descriptor to DAL sim SW2SW ring */
		ret = dp_dal_sim_sw2sw_ring_enqueue(
					dal_sim_ctx,
					&dal_sim_ctx->rx_sw2sw_ring[ring_id],
					reo_desc);
		if (ret) {
			dp_err_rl("Failed to enqueue RX desc, ret=%d", ret);
			/* Decrementing TP in HAL ring since reaping stopped */
			dal_vndr_hal_srng_dst_dec_tp(&offload_ctx->hal_soc,
						     reo_ring);
			break;
		}
		retrieved++;
	}

	/* End accessing SW2SW ring and release lock */
	dp_dal_sim_sw2sw_ring_access_end(&dal_sim_ctx->rx_sw2sw_ring[ring_id]);

	/* End ring access and release lock */
	dp_dal_offload_sim_ring_access_end(offload_ctx, reo_ring);

	dp_debug("Retrieved %u REO descriptors for ring_id %u",
		 retrieved, ring_id);

	return retrieved;
}

int dp_dal_offload_sim_get_tx_compl_desc(struct dp_dal_sim_ctx *dal_sim_ctx,
					 u16 ring_id, u32 budget)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *tx_compl_ring;
	void *tx_compl_desc;
	u32 retrieved = 0;
	int ret;

	if (!dal_sim_ctx) {
		dp_err("NULL simulator context in get_tx_compl_desc");
		return -EINVAL;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context in get_tx_compl_desc");
		return -EINVAL;
	}

	/* Get TX completion ring for the specified ring_id */
	tx_compl_ring = &offload_ctx->tx_cmpl_ring_hal_srng[ring_id];

	dp_debug("Getting TX compl descriptors for ring_id %u with budget %u",
		 ring_id, budget);

	/* Begin ring access with lock */
	dp_dal_offload_sim_ring_access_start(offload_ctx, tx_compl_ring);

	/* Start accessing SW2SW ring with lock */
	dp_dal_sim_sw2sw_ring_access_start(
				&dal_sim_ctx->tx_cpl_sw2sw_ring[ring_id]);

	/* Reap TX completion descriptors until budget is reached or
	 * no more descriptors.
	 */
	while (retrieved < budget) {
		/* Get next TX completion descriptor from the ring */
		tx_compl_desc = dal_vndr_hal_srng_dst_get_next(
						&offload_ctx->hal_soc,
						tx_compl_ring);
		if (!tx_compl_desc) {
			/* No more descriptors in this ring */
			break;
		}

		/* Enqueue descriptor to DAL sim SW2SW ring */
		ret = dp_dal_sim_sw2sw_ring_enqueue(
				dal_sim_ctx,
				&dal_sim_ctx->tx_cpl_sw2sw_ring[ring_id],
				tx_compl_desc);
		if (ret) {
			dp_err_rl("Failed to enqueue TX compl desc, ret=%d",
				  ret);
			/* Decrementing TP in HAL ring since reaping stopped */
			dal_vndr_hal_srng_dst_dec_tp(&offload_ctx->hal_soc,
						     tx_compl_ring);
			break;
		}
		retrieved++;
	}

	/* End accessing SW2SW ring and release lock */
	dp_dal_sim_sw2sw_ring_access_end(
				&dal_sim_ctx->tx_cpl_sw2sw_ring[ring_id]);

	/* End ring access and release lock */
	dp_dal_offload_sim_ring_access_end(offload_ctx, tx_compl_ring);

	dp_debug("Retrieved %u TX completion descriptors for ring_id %u",
		 retrieved, ring_id);

	return retrieved;
}

uint32_t
dp_dal_offload_sim_get_rx_refill_avail_entries(struct dp_dal_sim_ctx *dal_sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *rx_refill_ring;
	uint32_t num_entries_avail = 0;

	if (!dal_sim_ctx) {
		dp_err("NULL simulator context in get_rx_refill_avail_entries");
		return 0;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context in get_rx_refill_avail_entries");
		return 0;
	}

	/* Get RX refill ring */
	rx_refill_ring = &offload_ctx->rx_refill_ring_hal_srng;

	/* Take the ring lock */
	DAL_VNDR_SRNG_LOCK(&rx_refill_ring->lock);

	/* Get number of available entries using vendor HAL API */
	num_entries_avail = dal_vndr_hal_srng_src_num_avail(
							&offload_ctx->hal_soc,
							rx_refill_ring,
							1);
	/* Release the ring lock */
	DAL_VNDR_SRNG_UNLOCK(&rx_refill_ring->lock);

	dp_debug("RX refill ring available entries: %u", num_entries_avail);

	return num_entries_avail;
}

void
dp_dal_offload_sim_sync_refill_ring_hp_to_ddr(struct dp_dal_sim_ctx *sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *rx_refill_ring;

	if (!sim_ctx) {
		dp_err("NULL simulator context, can't sync refill ring");
		return;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context, can't sync refill ring hp");
		return;
	}

	rx_refill_ring = &offload_ctx->rx_refill_ring_hal_srng;
	DAL_VNDR_SRNG_LOCK(&rx_refill_ring->lock);
	dal_vndr_hal_srng_access_end(&offload_ctx->hal_soc, rx_refill_ring);
	DAL_VNDR_SRNG_UNLOCK(&rx_refill_ring->lock);
}

void dp_dal_offload_sim_sync_refill_ring_hp(struct dp_dal_sim_ctx *dal_sim_ctx)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *rx_refill_ring;

	if (!dal_sim_ctx) {
		dp_err("NULL simulator context, can't sync refill ring");
		return;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context");
		return;
	}

	rx_refill_ring = &offload_ctx->rx_refill_ring_hal_srng;

	DAL_VNDR_SRNG_LOCK(&rx_refill_ring->lock);
	dal_vndr_hal_srng_sync_hp(&offload_ctx->hal_soc, rx_refill_ring);
	DAL_VNDR_SRNG_UNLOCK(&rx_refill_ring->lock);
}

int dp_dal_offload_sim_rxbm_sync(struct dp_dal_sim_ctx *dal_sim_ctx,
				 u32 cnt, void **rx_buff)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *rx_refill_ring;
	struct dal_buffer_addr_info *buf_addr_info;
	void *refill_desc;
	u32 i;

	if (!dal_sim_ctx) {
		dp_err("NULL simulator context in rxbm_sync");
		return 0;
	}

	if (!rx_buff) {
		dp_err("NULL rx_buff array in rxbm_sync");
		return 0;
	}

	buf_addr_info = (struct dal_buffer_addr_info *)(*rx_buff);

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context in rxbm_sync");
		return 0;
	}

	/* Get RX refill ring */
	rx_refill_ring = &offload_ctx->rx_refill_ring_hal_srng;

	/* Begin ring access with lock */
	dp_dal_offload_sim_ring_access_start(offload_ctx, rx_refill_ring);

	/* Copy descriptors one by one to the refill ring entries */
	for (i = 0; i < cnt; i++) {
		/* Get next available entry in the refill ring */
		refill_desc = dal_vndr_hal_srng_src_get_next(
							&offload_ctx->hal_soc,
							rx_refill_ring);
		if (!refill_desc) {
			dp_err_rl("refill ring full synced %u/%u descriptors",
				  i, cnt);
			break;
		}

		/* Copy descriptor from rx_buff array to refill ring entry */
		dal_vndr_hal_rxbm_sync(&offload_ctx->hal_soc,
				       refill_desc,
				       &buf_addr_info[i]);
	}

	/* End ring access and release lock */
	dp_dal_offload_sim_ring_access_end(offload_ctx, rx_refill_ring);

	dp_debug("synced %u RX buffer descriptors to refill ring",
		 i);

	return i;
}

int dp_dal_offload_sim_fetch_current_hp_tp(struct dp_dal_sim_ctx *dal_sim_ctx,
					   uint32_t *hp, uint32_t *tp,
					   int ring_type, int ring_id)
{
	struct dp_dal_offload_sim_ctx *offload_ctx;
	struct dal_vndr_hal_srng *hal_srng = NULL;

	if (!dal_sim_ctx) {
		dp_err("NULL sim context");
		return -EINVAL;
	}

	if (!hp || !tp) {
		dp_err("NULL hp/tp");
		return -EINVAL;
	}

	offload_ctx = (struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context");
		return -EINVAL;
	}

	switch (ring_type) {
	case OFFLOAD_SIM_RING_TYPE_RX:
		if (ring_id < 0 || ring_id >= DAL_RX_RINGS_MAX) {
			dp_err("Invalid RX ring_id %d", ring_id);
			return -EINVAL;
		}

		hal_srng = &offload_ctx->rx_ring_hal_srng[ring_id];
		break;

	case OFFLOAD_SIM_RING_TYPE_TX_CPL:
		if (ring_id < 0 || ring_id >= DAL_TX_RINGS_MAX) {
			dp_err("Invalid TX completion ring_id %d", ring_id);
			return -EINVAL;
		}

		hal_srng = &offload_ctx->tx_cmpl_ring_hal_srng[ring_id];
		break;
	case OFFLOAD_SIM_RING_TYPE_TX:
		if (ring_id < 0 || ring_id >= DAL_TX_RINGS_MAX) {
			dp_err("Invalid TX ring_id %d", ring_id);
			return -EINVAL;
		}

		hal_srng = &offload_ctx->tx_ring_hal_srng[ring_id];
		break;
	case OFFLOAD_SIM_RING_TYPE_RX_REFILL:
		hal_srng = &offload_ctx->rx_refill_ring_hal_srng;
		break;
	default:
		dp_err("Invalid ring_type %d", ring_type);
		return -EINVAL;
	}

	if (!hal_srng) {
		dp_err("NULL hal_srng for offload sim ring_type %d, ring_id %d",
		       ring_type, ring_id);
		return -EINVAL;
	}

	dal_vndr_hal_get_sw_hptp(&offload_ctx->hal_soc, hal_srng, tp, hp);

	return 0;
}

static int dp_dal_offload_sim_send_msg_to_fw(struct dp_dal_sim_ctx *dal_sim_ctx,
					      uint32_t msg_type, uint32_t val)
{
	uint32_t msg;
	struct dp_dal_offload_sim_ctx *offload_ctx;
	void *dev_base_addr;
	QDF_STATUS status;

	if (!dal_sim_ctx) {
		dp_err("NULL sim context");
		return -EINVAL;
	}

	offload_ctx = (struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context");
		return -EINVAL;
	}

	dev_base_addr = offload_ctx->dev_base_addr;

	/* Construct message with TAG, TYPE, and VAL
	 * Format: [TAG(4 bits)][TYPE(16 bits)][VAL(12 bits)]
	 */
	msg = DAL_MSG_CONSTRUCT(OLE_WOW_MSG_TAG_OLE_TO_WLAN, msg_type, val);

	/* Step 1: Write message to PCIE doorbell message address
	 * FW reads the message value from PCIE_DOORBELL_MSG_ADDR (0x3224)
	 */
	qdf_iowrite32(dev_base_addr + PCIE_DOORBELL_MSG_ADDR, msg);

	/* Step 2: Trigger interrupt by writing to PCIE doorbell interrupt address
	 * Writing PCIE_DOORBELL_IRQ_TRIGGER to PCIE_DOORBELL_IRQ_ADDR (0x3228)
	 * triggers the interrupt to notify FW that a message is available
	 */
	/* Event is already in reset state from previous completion or init */
	qdf_iowrite32(dev_base_addr + PCIE_DOORBELL_IRQ_ADDR, PCIE_DOORBELL_IRQ_TRIGGER);

	dp_info("Sent msg 0x%x to FW: msg_addr=0x%x, irq_addr=0x%x, irq_trigger=0x%x",
		msg, PCIE_DOORBELL_MSG_ADDR, PCIE_DOORBELL_IRQ_ADDR, PCIE_DOORBELL_IRQ_TRIGGER);

	/* Poll for MSI address (wait for interrupt)
	 * We wait for the ISR to signal the event, which indicates FW has written
	 * to the MSI address.
	 */
	status = qdf_wait_for_event_completion(&dal_sim_ctx->suspend_msg_event, 100);
	if (status != QDF_STATUS_SUCCESS) {
		dp_err("Timeout waiting for FW write MSI interrupt");
		return -ETIMEDOUT;
	}

	dp_info("Received FW write MSI interrupt");
	return 0;
}

int dp_dal_offload_sim_handle_msg(struct dp_dal_sim_ctx *dal_sim_ctx,
				  uint32_t msg_type)
{
	int ret_val = 0;
	uint32_t type = DAL_MSG_GET_TYPE(msg_type);
	uint32_t tag = DAL_MSG_GET_TAG(msg_type);
	uint32_t val = DAL_MSG_GET_VAL(msg_type);
	struct dp_dal_offload_sim_ctx *offload_ctx;
	void *fw_write_vaddr;
	uint32_t resp;

	if (!dal_sim_ctx) {
		dp_err("NULL sim context");
		return -EINVAL;
	}

	offload_ctx =
		(struct dp_dal_offload_sim_ctx *)dal_sim_ctx->offload_sim_ctx;
	if (!offload_ctx) {
		dp_err("NULL offload context");
		return -EINVAL;
	}

	fw_write_vaddr = dal_sim_ctx->suspend_msg_data_vaddr;
	if (!fw_write_vaddr) {
		dp_err("NULL FW write DDR buffer");
		return -EINVAL;
	}

	dp_info("Offload Sim received msg: Tag=%u Type=%u Val=%u", tag, type, val);

	switch (type) {
	case OLE_WOW_MSG_TYPE_INTF_PAUSE:
		dp_info("Handling INTF_PAUSE");

		/* Clear response area in DDR buffer */
		*(uint32_t *)fw_write_vaddr = 0;

		/* Send message to FW and trigger MSI interrupt */
		ret_val = dp_dal_offload_sim_send_msg_to_fw(dal_sim_ctx,
						  OLE_WOW_MSG_TYPE_INTF_PAUSE,
						  val);
		if (ret_val)
			return ret_val;

		/* Read response from DDR buffer */
		resp = *(uint32_t *)fw_write_vaddr;
		if (resp != 0) {
			uint32_t resp_tag = DAL_MSG_GET_TAG(resp);
			uint32_t resp_type = DAL_MSG_GET_TYPE(resp);

			if (resp_tag == OLE_WOW_MSG_TAG_WLAN_TO_OLE) {
				/* Validate response type is ACK or NACK */
				if (resp_type == OLE_WOW_MSG_TYPE_ACK ||
				    resp_type == OLE_WOW_MSG_TYPE_NACK) {
					dp_info("Received response from FW: 0x%x", resp);
					ret_val = resp;
				} else {
					dp_err("Invalid response type from FW: 0x%x", resp);
					ret_val = -EINVAL;
				}
			} else {
				dp_err("Invalid response tag from FW: 0x%x", resp);
				ret_val = -EINVAL;
			}
		} else {
			dp_err("No response from FW in DDR buffer after interrupt");
			ret_val = -EIO;
		}
		break;

	case OLE_WOW_MSG_TYPE_INTF_RESUME:
		dp_info("Handling INTF_RESUME");

		/* Clear response area in DDR buffer */
		*(uint32_t *)fw_write_vaddr = 0;

		/* Send message to FW and trigger MSI interrupt */
		ret_val = dp_dal_offload_sim_send_msg_to_fw(dal_sim_ctx,
						  OLE_WOW_MSG_TYPE_INTF_RESUME,
						  val);
		if (ret_val)
			return ret_val;

		/* Read response from DDR buffer */
		resp = *(uint32_t *)fw_write_vaddr;
		if (resp != 0) {
			uint32_t resp_tag = DAL_MSG_GET_TAG(resp);
			uint32_t resp_type = DAL_MSG_GET_TYPE(resp);

			if (resp_tag == OLE_WOW_MSG_TAG_WLAN_TO_OLE) {
				/* Validate response type is ACK or NACK */
				if (resp_type == OLE_WOW_MSG_TYPE_ACK ||
				    resp_type == OLE_WOW_MSG_TYPE_NACK) {
					dp_info("Received response from FW: 0x%x", resp);
					ret_val = resp;
				} else {
					dp_err("Invalid response type from FW: 0x%x", resp);
					ret_val = -EINVAL;
				}
			} else {
				dp_err("Invalid response tag from FW: 0x%x", resp);
				ret_val = -EINVAL;
			}
		} else {
			dp_err("No response from FW in DDR buffer after interrupt");
			ret_val = -EIO;
		}
		break;

	default:
		dp_err("Unknown message type %u", type);
		ret_val = -EINVAL;
		break;
	}

	return ret_val;
}

#endif /* FEATURE_DP_DAL_SIM */
