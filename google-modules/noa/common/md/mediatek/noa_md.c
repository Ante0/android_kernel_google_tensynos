// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA MD Driver
 *
 * Copyright 2024 Google LLC.
 */

#include <linux/device.h>
#include <linux/fs.h>
#include <linux/if_link.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/* NOA related header */
#include "common/modem_ring_id.h"
#include "common/ring.h"
#include "common/ring_id.h"
#include "ncp/md/mediatek/ncp_md.h"
#include "ncp/md/mediatek/ncp_md_rx_data.h"
#include "nep/nep.h"
#include "nep/ring_manager.h"

/* NOA modem related header */
#include "noa_md.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_dma_mapper.h"
#include "noa_md_dpa.h"
#include "noa_md_dpmaif.h"
#include "noa_md_pcie.h"
#include "noa_md_rpc.h"
#include "noa_md_rx_data.h"
#include "noa_md_shmem_sync.h"
#include "noa_md_trace.h"
#include "noa_md_tx_data.h"
#include "noa_md_utility.h"
#include "noa_md_wrapper.h"
#include "noa_md_wrapper_dpmaif.h"
#include "noa_md_wwan_notifier.h"

/* DPA related header */
#include "soc/google/google_dpa.h"
#include "soc/google/google_dpa_doorbell.h"
#include "soc/google/google_dpa_ctrl.h"

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "noa_md_apc2ncp_ring_fullsoc.h"
#elif IS_ENABLED(CONFIG_NOA_SIM_SUPPORT)
#include "noa_md_apc2ncp_ring.h"
#endif

#include "debugfs/noa_md_debug.h"

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
#include "soc/google/google_dpa_ring_service_proxy.h"
#endif

struct noa_md_dev md_dev;
EXPORT_SYMBOL(md_dev);

/* Forward declarations */
// TODO: b/434646935 - Reorder the static functions in this file in a follow-up
// change and remove the need for these forward declarations.

/* High-Level Resource Management */
static int noa_md_res_release(struct noa_md_dev *p_md_dev);
static int noa_md_fw_release(struct noa_md_dev *p_md_dev);

/* High-Level Remap & Ring Management (Symmetric Pairs) */
static int noa_md_ring_alloc_remap_setup(struct noa_md_dev *p_md_dev);
static void noa_md_ring_alloc_remap_release(struct noa_md_dev *p_md_dev);

/* Generic Ring Management (Symmetric Pairs) */
static int noa_md_ring_release(struct noa_md_dev *p_md_dev);

/* Low-Level Memory Mapping Helpers (Symmetric Pairs) */
static int noa_md_apc2ncp_ring_memory_map_setup_tx(
	struct mtk_dpmaif_ctlb *dcb, u64 *dpa_base);
static void noa_md_apc2ncp_ring_memory_map_release_tx(void);
static int noa_md_apc2ncp_ring_memory_map_setup_rx(struct noa_md_dev *p_md_dev);
static void noa_md_apc2ncp_ring_memory_map_release_rx(struct noa_md_dev *p_md_dev);

static int noa_md_dcb_validate(const struct mtk_dpmaif_ctlb *dcb)
{
	unsigned char txq_cnt, tx_srvs_cnt, rxq_cnt;

	NOA_MD_INFO("enter, dcb=0x%p, size=%lu", dcb, sizeof(*dcb));

	CHECK_PTR_OR_RETURN_ERR(dcb, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(dcb->drv_info, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(dcb->drv_info->cfg, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(dcb->txqs, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(dcb->rxqs, -EINVAL);

	txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
	tx_srvs_cnt = dcb->drv_info->cfg->tx_srvs_cfg.tx_srv_cnt;
	rxq_cnt = dcb->drv_info->cfg->rx_cfg.rxq_cnt;

	NOA_MD_INFO("txq_cnt=[%u], rxq_cnt=[%u]", txq_cnt, rxq_cnt);

	if (txq_cnt != DPMAIF_TXQ_CNT_MAX || rxq_cnt != DPMAIF_RXQ_CNT_MAX) {
		NOA_MD_ERROR("queue size wrong");
		return -EINVAL;
	}

	for (int i = 0; i < txq_cnt; i++) {
		struct dpmaif_txq *txq = &dcb->txqs[i];
		CHECK_PTR_OR_RETURN_ERR(txq, -EINVAL);
		CHECK_PTR_OR_RETURN_ERR(txq->drb_base, -EINVAL);
		NOA_MD_INFO("txqs%d=0x%p, drb_base=0x%p, drb_cnt=%d, "
					"burst_submit_cnt=%d, doorbell_delay=%d",
					i, txq, txq->drb_base, txq->drb_cnt,
					txq->burst_submit_cnt,
					dcb->drv_info->cfg->tx_cfg.txqs[i].doorbell_delay);
	}

	for (int i = 0; i < tx_srvs_cnt; i++) {
		struct dpmaif_tx_srv *tx_srv = &dcb->tx_srvs[i];
		struct task_struct *srv;
		CHECK_PTR_OR_RETURN_ERR(tx_srv, -EINVAL);
		srv = tx_srv->srv;
		if (srv) {
			long state;
			rcu_read_lock();
			state = srv->__state;
			rcu_read_unlock();

			NOA_MD_INFO("srv=0x%p, state(raw)=%lx", srv, state);
		}
	}

	for (int i = 0; i < rxq_cnt; i++) {
		struct dpmaif_rxq *rxq = &dcb->rxqs[i];
		CHECK_PTR_OR_RETURN_ERR(rxq, -EINVAL);
		CHECK_PTR_OR_RETURN_ERR(rxq->pit_base, -EINVAL);
		NOA_MD_INFO("rxq%d=0x%p, pit_base=0x%p, pit_cnt=%d, "
					"pit_burst_rel_cnt=%d",
					i, rxq, rxq->pit_base, rxq->pit_cnt,
					rxq->pit_burst_rel_cnt);
	}
	return 0;
}

static int noa_md_fw_setup_pci(struct noa_md_dev *p_md_dev)
{
	p_md_dev->fw_info.hif_config = noa_md_pcie_get_hif_config();
	return p_md_dev->fw_info.hif_config ? 0 : -ENODEV;
}

static int noa_md_setup_fw_resources(struct noa_md_dev *p_md_dev)
{
	int ret;

	ret = noa_md_ring_alloc_remap_setup(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_ring_alloc_remap_setup: %d", ret);
		return ret;
	}

	ret = noa_md_rpc_fw_init(p_md_dev->dev, &p_md_dev->fw_info);
	if (ret) {
		NOA_MD_ERROR("Fail to send modem rpc call for fw init: %d", ret);
		// TODO: b/426399485 - Design a recovery mechanism
		// for the RPC timeout case(e.g. Trigger dpa crash).
		// noa_md_ring_alloc_remap_release(p_md_dev);
		return ret;
	}

	ret = noa_md_tx_ring_setup_activate(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("Fail to setup ring: %d", ret);
		/*
		 * Only needs to call noa_md_ring_alloc_remap_release because there
		 * is no release function for noa_md_rpc_fw_init
		 */
		noa_md_ring_alloc_remap_release(p_md_dev);
		return ret;
	}

	return 0;
}

static void noa_md_check_and_trigger_fw_init(struct noa_md_dev *p_md_dev)
{
	bool should_trigger = false;

	mutex_lock(&p_md_dev->fw_init_state_lock);

	if (p_md_dev->dpmaif_ring_state == NOA_DPMAIF_RING_STATE_READY &&
		p_md_dev->dpa_state == NOA_STATE_READY) {
		p_md_dev->dpmaif_ring_state = NOA_DPMAIF_RING_STATE_DONE;
		should_trigger = true;
	}

	mutex_unlock(&p_md_dev->fw_init_state_lock);

	if (should_trigger) {
		int ret;
		NOA_MD_INFO("Both states are READY. Triggering FW Init");
		ret = noa_md_setup_fw_resources(p_md_dev);
		if (ret) {
			NOA_MD_ERROR("Fail to setup FW resource: %d", ret);
			// TODO: b/433460810 - Consider setting a failure state here if
			// needed.
			return;
		}
	} else {
		NOA_MD_INFO("dpmaif_ring_state: %d, dpa_state: %d",
			p_md_dev->dpmaif_ring_state, p_md_dev->dpa_state
		);
	}
}

static void noa_md_dpa_state_change_cb(enum dpa_state state, void *context)
{
	NOA_MD_INFO("DPA state changed to: %d", state);
	struct noa_md_dev* p_md_dev = (struct noa_md_dev*)context;

	mutex_lock(&p_md_dev->fw_init_state_lock);
	p_md_dev->dpa_state = state;
	mutex_unlock(&p_md_dev->fw_init_state_lock);

	if (state == NOA_STATE_CRASH) {
		noa_md_tx_dpmaif_dump_drb_info();
		return;
	}

	noa_md_check_and_trigger_fw_init(p_md_dev);
}

static void noa_md_set_dpmaif_ring_state(enum noa_dpmaif_ring_state new_state,
	struct noa_md_dev *p_md_dev)
{
	NOA_MD_INFO("DPMAIF Ring state changed to: %d", new_state);

	mutex_lock(&p_md_dev->fw_init_state_lock);
	p_md_dev->dpmaif_ring_state = new_state;
	mutex_unlock(&p_md_dev->fw_init_state_lock);

	noa_md_check_and_trigger_fw_init(p_md_dev);
}

static int noa_md_res_setup(struct noa_md_dev *p_md_dev)
{
	struct noa_md_shmem_handle *shmem_handle = NULL;
	unsigned int shmem_size = sizeof(struct noa_md_shmem_layout);
	int ret = 0;

	NOA_MD_INFO("enter");

	shmem_handle = &p_md_dev->shmem_handle;
	shmem_handle->va_base = dma_alloc_coherent(
		p_md_dev->dev /* noa_md_dev */,
		shmem_size,
		&shmem_handle->dma_base, GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(shmem_handle->va_base, error);

	p_md_dev->dpa_state_client = noa_md_dpa_register_state_client(
		noa_md_dpa_state_change_cb, p_md_dev);
	if (!p_md_dev->dpa_state_client) {
		NOA_MD_ERROR("Failed to register DPA state client");
		goto error;
	}

	ret = noa_md_tx_setup();
	if(ret) {
		NOA_MD_ERROR("noa_md_tx_setup:%d", ret);
		goto error;
	}

	ret = noa_md_rx_setup();
	if(ret) {
		NOA_MD_ERROR("noa_md_rx_setup:%d", ret);
		goto error;
	}

	ret = noa_md_cldma_setup();
	if (ret) {
		NOA_MD_ERROR("noa_md_cldma_setup:%d", ret);
		goto error;
	}

	NOA_MD_INFO("exit");
	return 0;
error:
	noa_md_res_release(p_md_dev);
	NOA_MD_INFO("exit, ret:%d", ret);
	return ret;
}

static int noa_md_res_release(struct noa_md_dev *p_md_dev)
{
	NOA_MD_INFO("enter");
	noa_md_ring_release(p_md_dev);
	noa_md_tx_release();
	noa_md_rx_release();
	noa_md_cldma_release();
	noa_md_dpa_release(p_md_dev);

	// Release shared memory
	struct noa_md_shmem_handle *shmem_handle = &p_md_dev->shmem_handle;
	if (shmem_handle->va_base && shmem_handle->dma_base) {
		dma_free_coherent(
			p_md_dev->dev,
			sizeof(struct noa_md_shmem_layout),
			(void *)shmem_handle->va_base,
			shmem_handle->dma_base);
		shmem_handle->va_base = NULL;
		shmem_handle->pa_base = 0;
	}


	NOA_MD_INFO("exit");
	return 0;
}

static void noa_md_ncp2apc_isr(int id, void *resource)
{
	struct noa_md_dev* p_md_dev = (struct noa_md_dev*)resource;
	struct noa_md_dpmaif_ops* noa_dpmaif_ops =
		noa_md_wpr_dpmaif_get_dpmaif_ops();

	NOA_MD_DATA("enter, id:%d", id);
	noa_dpmaif_ops->irq_tx_done(p_md_dev->noa_dcb, 1 << id);
}

static int noa_md_dpmaif_stats_sync_impl(struct noa_md_dev *p_md_dev,
	enum noa_md_wpr_sync_duration duration)
{
	struct mtk_dpmaif_ctlb *dcb_from;
	struct mtk_dpmaif_ctlb *dcb_to;
	switch (duration)
	{
	case NOA_MD_WPR_SYNC_DPMAIF_TO_NOA:
		dcb_from = p_md_dev->dpmaif_dcb;
		dcb_to = p_md_dev->noa_dcb;
		break;
	case NOA_MD_WPR_SYNC_NOA_TO_DPMAIF:
		dcb_from = p_md_dev->noa_dcb;
		dcb_to = p_md_dev->dpmaif_dcb;
		break;
	default:
		NOA_MD_ERROR("duration=%d", duration);
		return -EINVAL;
	}

	CHECK_PTR_OR_RETURN_ERR(dcb_from, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(dcb_to, -EINVAL);

	NOA_MD_INFO(
		"duration: %d, dpmaif_dcb: 0x%pK, noa_dcb: 0x%pK, "
		"dcb_from: 0x%pK (dpmaif_state: %d, dpmaif_sw_reset: %d, trans_enabled: %d, "
		"irq_enabled: %d), "
		"dcb_to: 0x%pK (dpmaif_state: %d, dpmaif_sw_reset: %d, trans_enabled: %d, "
		"irq_enabled: %d)",
		duration, p_md_dev->dpmaif_dcb, p_md_dev->noa_dcb,
		dcb_from, dcb_from->dpmaif_state, dcb_from->dpmaif_sw_reset,
		dcb_from->trans_enabled, dcb_from->irq_enabled,
		dcb_to, dcb_to->dpmaif_state, dcb_to->dpmaif_sw_reset, dcb_to->trans_enabled,
		dcb_to->irq_enabled
	);

	dcb_to->dpmaif_state = dcb_from->dpmaif_state;
	dcb_to->dpmaif_sw_reset = dcb_from->dpmaif_sw_reset;
	dcb_to->trans_enabled = dcb_from->trans_enabled;
	dcb_to->irq_enabled = dcb_from->irq_enabled;
	dcb_to->dpmaif_rx_legacy = dcb_from->dpmaif_rx_legacy;

	return 0;
}

int noa_md_dpmaif_stats_sync(void *data)
{
	enum noa_md_wpr_sync_duration *duration = (enum noa_md_wpr_sync_duration *)data;

	CHECK_PTR_OR_RETURN_ERR(duration, -EINVAL);

	return noa_md_dpmaif_stats_sync_impl(&md_dev, *duration);
}
EXPORT_SYMBOL_GPL(noa_md_dpmaif_stats_sync);

void noa_md_dpmaif_start(void *data)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb = (struct mtk_dpmaif_ctlb *)data;
	NOA_MD_INFO("enter");
	if (noa_md_dcb_validate(dpmaif_dcb)) {
		NOA_MD_ERROR("dcb check failed");
		return;
	}

	md_dev.dpmaif_dcb = dpmaif_dcb;
	md_dev.dcb = dpmaif_dcb;

	noa_md_dpmaif_stats_sync_impl(&md_dev, NOA_MD_WPR_SYNC_DPMAIF_TO_NOA);

	return;
}
EXPORT_SYMBOL_GPL(noa_md_dpmaif_start);

/**
 * noa_md_cldma_init() - Listener for mtk_cldma_init event.
 * @data: Pointer to the mtk_ctrl_trans structure.
 */
void noa_md_cldma_init(void *data)
{
	struct mtk_ctrl_trans *trans = (struct mtk_ctrl_trans *)data;

	NOA_MD_INFO("enter");
	if (!trans) {
		NOA_MD_ERROR("trans is null");
		return;
	}

	/* Store trans or perform NOA-side init if needed */
}
EXPORT_SYMBOL_GPL(noa_md_cldma_init);

/**
 * noa_md_cldma_exit() - Listener for mtk_cldma_exit event.
 * @data: Pointer to the mtk_ctrl_trans structure.
 */
void noa_md_cldma_exit(void *data)
{
	struct mtk_ctrl_trans *trans = (struct mtk_ctrl_trans *)data;

	NOA_MD_INFO("enter");
	if (!trans) {
		NOA_MD_ERROR("trans is null");
		return;
	}

	/* Perform NOA-side cleanup */
}
EXPORT_SYMBOL_GPL(noa_md_cldma_exit);

/**
 * noa_md_cldma_dev_init() - Listener for mtk_cldma_dev_init event.
 * @data: Pointer to the cldma_dev structure.
 */
void noa_md_cldma_dev_init(void *data)
{
	struct cldma_dev *cd = (struct cldma_dev *)data;

	NOA_MD_INFO("enter");
	if (!cd) {
		NOA_MD_ERROR("cldma_dev is null");
		return;
	}

	/* Save cldma_dev context */
	md_dev.cldma_dev = cd;
}
EXPORT_SYMBOL_GPL(noa_md_cldma_dev_init);

/**
 * noa_md_cldma_dev_exit() - Listener for mtk_cldma_dev_exit event.
 * @data: Pointer to the cldma_dev structure.
 */
void noa_md_cldma_dev_exit(void *data)
{
	struct cldma_dev *cd = (struct cldma_dev *)data;

	NOA_MD_INFO("enter");
	if (!cd) {
		NOA_MD_ERROR("cldma_dev is null");
		return;
	}

	if (md_dev.cldma_dev == cd)
		md_dev.cldma_dev = NULL;
}
EXPORT_SYMBOL_GPL(noa_md_cldma_dev_exit);

/**
 * noa_md_cldma_open() - Listener for mtk_cldma_open event.
 * @data: Pointer to the cldma_dev structure.
 */
void noa_md_cldma_open(void *data)
{
	struct cldma_dev *cd = (struct cldma_dev *)data;

	NOA_MD_INFO("enter");
	if (!cd) {
		NOA_MD_ERROR("cldma_dev is null");
		return;
	}

	/* Sync CLDMA state to NOA */
}
EXPORT_SYMBOL_GPL(noa_md_cldma_open);

/**
 * noa_md_cldma_close() - Listener for mtk_cldma_close event.
 * @data: Pointer to the cldma_dev structure.
 */
void noa_md_cldma_close(void *data)
{
	struct cldma_dev *cd = (struct cldma_dev *)data;

	NOA_MD_INFO("enter");
	if (!cd) {
		NOA_MD_ERROR("cldma_dev is null");
		return;
	}

	/* Handle CLDMA stop on NOA side */
}
EXPORT_SYMBOL_GPL(noa_md_cldma_close);

void noa_md_dpmaif_stop(void *data)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb = (struct mtk_dpmaif_ctlb *)data;
	struct mtk_dpmaif_ctlb *noa_dcb = md_dev.noa_dcb;
	NOA_MD_INFO("enter");
	if (dpmaif_dcb && noa_dcb) {
		noa_dcb->dpmaif_state = dpmaif_dcb->dpmaif_state;
		noa_dcb->dpmaif_sw_reset = dpmaif_dcb->dpmaif_sw_reset;
	}
	return;
}
EXPORT_SYMBOL_GPL(noa_md_dpmaif_stop);

static int noa_md_tx_queues_remap_setup(struct noa_md_dev *p_md_dev,
	u64 *apc2ncp_dpa_base)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb = p_md_dev->dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb = p_md_dev->noa_dcb;
	struct device *dpmaif_dev = DCB_TO_DEV(dpmaif_dcb);
	struct device *noa_dev = DCB_TO_DEV(noa_dcb);
	struct device *dpa_dev = p_md_dev->dpa_res->dpa_dev;
	int txq_cnt = dpmaif_dcb->drv_info->cfg->tx_cfg.txq_cnt;
	int ret;

	NOA_MD_INFO("dpmaif_dev:0x%p, noa_dev:0x%p", dpmaif_dev, noa_dev);

	for (int i = 0; i < txq_cnt; i++) {
		struct dpmaif_txq *txq_source = &dpmaif_dcb->txqs[i];
		struct noa_txbm *txq_dest = &p_md_dev->fw_info.txqs[i];
		ret = noa_md_dma_mapper_remap_sg(
			&p_md_dev->tx.mapper,
			dpmaif_dev,  /* Source device is the DPMAIF device */
			dpa_dev,  /* Target device is the DPA */
			(void *)txq_source->drb_base,
			txq_source->drb_dma_addr,
			txq_source->drb_cnt * sizeof(*txq_source->drb_base),
			&txq_dest->drb_dpa_base,
			GFP_KERNEL);
		NOA_MD_INFO("[%d]drb_base=0x%lx, drb_dpa_base=0x%lx", i,
					(unsigned long)txq_source->drb_base, txq_dest->drb_dpa_base);
		if (ret) {
			NOA_MD_ERROR("Fail to remap modem drb queue%d, ret:%d", i, ret);
			goto error;
		}

		struct dpmaif_txq *txq_dpmaif_source = &noa_dcb->txqs[i];
		ret = noa_md_dma_mapper_remap_sg(
			&p_md_dev->tx.mapper,
			noa_dev,  /* Source device is the DPMAIF device */
			dpa_dev,  /* Target device is the DPA */
			(void *)txq_dpmaif_source->drb_base,
			txq_dpmaif_source->drb_dma_addr,
			txq_dpmaif_source->drb_cnt * sizeof(*txq_dpmaif_source->drb_base),
			&apc2ncp_dpa_base[i],
			GFP_KERNEL);
		NOA_MD_INFO("[%d][apc2ncp]drb_base=0x%lx, drb_dpa_base=0x%lx", i,
					(unsigned long)txq_dpmaif_source->drb_base, apc2ncp_dpa_base[i]);
		if (ret) {
			NOA_MD_ERROR("Fail to remap apc2ncp ring%d, ret:%d", i, ret);
			goto error;
		}
	}

	return 0;

error:
	noa_md_dma_unmap_all(&p_md_dev->tx.mapper);
	return ret;
}

/**
 * noa_md_tx_queues_remap_release() - Release DMA mappings for all TX queues.
 * @p_md_dev: Pointer to the main noa_md_dev driver structure.
 */
static void noa_md_tx_queues_remap_release(struct noa_md_dev *p_md_dev)
{
	noa_md_dma_unmap_all(&p_md_dev->tx.mapper);
}

/**
 * noa_md_tx_buffer_pool_setup() - Allocate and remap the TX buffer pool for NCP.
 * @p_md_dev: Pointer to the main noa_md_dev driver structure.
 *
 * Allocates a DMA-coherent buffer for the TX data path FIFO and remaps its
 * address into the DPA's address space for direct hardware access by the NCP.
 * The physical address is stored in the firmware info payload.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_tx_buffer_pool_setup(struct noa_md_dev *p_md_dev)
{
	struct noa_tx_buffer_pool *pool = &p_md_dev->tx_buffer_pool;
	struct mtk_dpmaif_ctlb *noa_dcb = p_md_dev->noa_dcb;
	struct device *noa_dev = DCB_TO_DEV(noa_dcb);
	struct device *dpa_dev = p_md_dev->dpa_res->dpa_dev;
	size_t pool_size = NOA_MD_MAX_TX_PKT_SIZE * NOA_MD_FW_FIFO_SIZE;
	int ret;

	pool->va_base = dma_alloc_coherent(
		noa_dev, pool_size, &pool->dma_base, GFP_KERNEL);
	CHECK_PTR_OR_RETURN_ERR(pool->va_base, -ENOMEM);

	ret = noa_md_dma_mapper_remap_sg(
		&p_md_dev->tx.mapper,
		noa_dev,  /* Source device is the DPMAIF device */
		dpa_dev,  /* Target device is the DPA */
		(void *)pool->va_base, pool->dma_base,
		pool_size,
		(u64 *)&pool->dpa_base,
		GFP_KERNEL);
	if (ret) {
		NOA_MD_ERROR("Failed to remap tx_buffer_pool: %d", ret);
		dma_free_coherent(noa_dev,
			pool_size, (void *)pool->va_base, pool->dma_base);
		pool->va_base = NULL;
		return ret;
	}

	NOA_MD_INFO("va_base=0x%lx, dma_base=0x%lx, dpa_base=0x%lx",
		(unsigned long)pool->va_base, (unsigned long)pool->dma_base,
		(unsigned long)pool->dpa_base);

	// TODO: b/435273452 - correct the name of va_base/pa_base for tx buffer pool to avoid
	// confusion, va_base is for nep ring service which use dpa view address(dpa_base) and
	// pa_base is for modem drb which use modem view address(dma_base)
	p_md_dev->fw_info.pool.va_base = (u64)pool->dpa_base;
	p_md_dev->fw_info.pool.pa_base = pool->dma_base;

	return 0;
}

/**
 * noa_md_tx_buffer_pool_release() - Unmap and free the TX buffer pool.
 * @p_md_dev: Pointer to the main noa_md_dev driver structure.
 */
static void noa_md_tx_buffer_pool_release(struct noa_md_dev *p_md_dev)
{
	struct noa_tx_buffer_pool *pool = &p_md_dev->tx_buffer_pool;
	struct mtk_dpmaif_ctlb *noa_dcb = p_md_dev->noa_dcb;
	struct device *noa_dev = DCB_TO_DEV(noa_dcb);
	size_t pool_size = NOA_MD_MAX_TX_PKT_SIZE * NOA_MD_FW_FIFO_SIZE;

	if (pool->va_base) {
		noa_md_dma_unmap_by_addr(&p_md_dev->tx.mapper, (void *)pool->va_base);
		dma_free_coherent(noa_dev, pool_size, (void *)pool->va_base, pool->dma_base);
		pool->va_base = NULL;
		pool->dpa_base = NULL;
	}
}

/**
 * noa_md_shmem_remap_setup() - Remap the shared memory handle for NCP access.
 * @p_md_dev: Pointer to the main noa_md_dev driver structure.
 *
 * Remaps the previously allocated shared memory region into the DPA's address
 * space, allowing the NCP to access it. The resulting physical address is
 * stored in the firmware info payload.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_shmem_remap_setup(struct noa_md_dev *p_md_dev)
{
	struct noa_md_shmem_handle *shmem_handle = &p_md_dev->shmem_handle;
	u32 shmem_size = sizeof(struct noa_md_shmem_layout);
	int ret;

	/* The shared memory is allocated in noa_md_res_setup() */
	CHECK_PTR_OR_RETURN_ERR(shmem_handle->va_base, -ENXIO);

	ret = noa_md_dma_mapper_remap_sg(
		&p_md_dev->mapper,
		p_md_dev->dev,  /* Source device is the NOA_MD device */
		p_md_dev->dpa_res->dpa_dev,  /* Target device is the DPA */
		(void *)shmem_handle->va_base,
		shmem_handle->dma_base,
		shmem_size,
		&shmem_handle->pa_base,
		GFP_KERNEL);
	if (ret) {
		NOA_MD_ERROR("Failed to remap shmem_handle: %d", ret);
		return ret;
	}

	NOA_MD_INFO("[Shared_memory] size=%u, va_base=0x%p, pa_base=0x%llx",
		shmem_size, shmem_handle->va_base, shmem_handle->pa_base);

	p_md_dev->fw_info.shared_mem_info.addr = shmem_handle->pa_base;
	p_md_dev->fw_info.shared_mem_info.size = shmem_size;

	return 0;
}

/**
 * noa_md_shmem_remap_release() - Unmap the shared memory handle.
 * @p_md_dev: Pointer to the main noa_md_dev driver structure.
 */
static void noa_md_shmem_remap_release(struct noa_md_dev *p_md_dev)
{
	struct noa_md_shmem_handle *shmem_handle = &p_md_dev->shmem_handle;

	if (shmem_handle->va_base) {
		noa_md_dma_unmap_by_addr(&p_md_dev->mapper,
			(void *)shmem_handle->va_base);
	}
}

static int noa_md_ring_alloc_remap_setup(struct noa_md_dev *p_md_dev)
{
	struct mtk_dpmaif_ctlb *noa_dcb = p_md_dev->noa_dcb;
	u64 apc2ncp_drb_dpa_base[NOA_MD_MAX_UL_QUEUE_SIZE];
	int ret;

	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	struct device *noa_dev = DCB_TO_DEV(noa_dcb);

	CHECK_PTR_OR_RETURN_ERR(noa_dev, -EINVAL);

	ret = noa_md_tx_queues_remap_setup(p_md_dev, &apc2ncp_drb_dpa_base[0]);
	if (ret) {
		NOA_MD_ERROR("noa_md_tx_queues_remap_setup, ret=%d", ret);
		return ret;
	}
	ret = noa_md_rx_queues_remap_setup(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_rx_queues_remap_setup, ret=%d", ret);
		goto err_release_tx_queue;
	}

	NOA_MD_INFO("noa_md_apc2ncp_ring_memory_map_setup_tx");
	ret = noa_md_apc2ncp_ring_memory_map_setup_tx(noa_dcb, apc2ncp_drb_dpa_base);
	if (ret) {
		NOA_MD_ERROR("noa_md_apc2ncp_ring_memory_map_setup_tx, ret=%d", ret);
		goto err_release_rx_queue;
	}

	NOA_MD_INFO("noa_md_apc2ncp_ring_memory_map_setup_rx");
	ret = noa_md_apc2ncp_ring_memory_map_setup_rx(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_apc2ncp_ring_memory_map_setup_rx, ret=%d", ret);
		goto err_release_tx_map;
	}

	ret = noa_md_tx_buffer_pool_setup(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("Fail to remap tx_buffer_pool=%d", ret);
		goto err_release_rx_map;
	}

	ret = noa_md_shmem_remap_setup(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("Fail to remap shmem_handle=%d", ret);
		goto err_release_tx_pool;
	}

	// noa_md_rx_ring setup and activation
	ret = noa_md_rx_ring_setup_activate(p_md_dev);
	if (ret < 0) {
		NOA_MD_ERROR("Fail to init setup noa_md_rx_ring, ret=[%d]", ret);
		goto err_release_shmem;
	}

#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
	/* TODO: Handle self-contained cleanup if errors. */
	NOA_MD_INFO("noa_md_apc2ncp_ring_apc_setup");
	ret = noa_md_apc2ncp_ring_apc_setup(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_apc2ncp_ring_apc_setup=%d", ret);
		goto err_release_apc_rings;
	}
#endif

	return 0;

#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
err_release_apc_rings:
	noa_md_apc2ncp_ring_apc_release();
#endif
	noa_md_rx_ring_release(p_md_dev);
err_release_shmem:
	noa_md_shmem_remap_release(p_md_dev);
err_release_tx_pool:
	noa_md_tx_buffer_pool_release(p_md_dev);
err_release_rx_map:
	noa_md_apc2ncp_ring_memory_map_release_rx(p_md_dev);
err_release_tx_map:
	noa_md_apc2ncp_ring_memory_map_release_tx();
err_release_rx_queue:
	noa_md_rx_queues_remap_release(p_md_dev);
err_release_tx_queue:
	noa_md_tx_queues_remap_release(p_md_dev);
	return ret;
}


/**
 * noa_md_ring_alloc_remap_release() - Symmetrically releases resources from setup.
 * @p_md_dev: Pointer to the main noa_md_dev driver structure.
 *
 * This function is the counterpart to noa_md_ring_alloc_remap_setup(). It
 * tears down all remapped and allocated ring resources in the reverse
 * order of their creation to ensure a clean shutdown and prevent resource leaks.
 */
static void noa_md_ring_alloc_remap_release(struct noa_md_dev *p_md_dev)
{
	CHECK_PTR_OR_RETURN(p_md_dev);
	NOA_MD_INFO("enter");

	/*
	 * Release resources in the reverse order of allocation in
	 * noa_md_ring_alloc_remap_setup() [LIFO].
	 */

	/* 8. Corresponds to noa_md_rx_ring_setup_activate() */
	noa_md_rx_ring_release(p_md_dev);

	/* 7. Corresponds to noa_md_shmem_remap_setup() */
	noa_md_shmem_remap_release(p_md_dev);

	/* 6. Corresponds to noa_md_tx_buffer_pool_setup() */
	noa_md_tx_buffer_pool_release(p_md_dev);

#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
	/* 5. Corresponds to noa_md_apc2ncp_ring_apc_setup() */
	noa_md_apc2ncp_ring_apc_release();
#endif

	/* 4. Corresponds to noa_md_apc2ncp_ring_memory_map_setup_rx() */
	noa_md_apc2ncp_ring_memory_map_release_rx(p_md_dev);

	/* 3. Corresponds to noa_md_apc2ncp_ring_memory_map_setup_tx() */
	noa_md_apc2ncp_ring_memory_map_release_tx();

	/* 2. Corresponds to noa_md_rx_queues_remap_setup() */
	noa_md_rx_queues_remap_release(p_md_dev);

	/* 1. Corresponds to noa_md_tx_queues_remap_setup() */
	noa_md_tx_queues_remap_release(p_md_dev);

	NOA_MD_INFO("exit");
}

/**
 * noa_md_apc2ncp_ring_memory_map_setup_tx() - Maps existing TX DRB rings for NCP.
 *
 * This function does NOT allocate new memory. It points to the TX DRB rings
 * that are already allocated and managed by the DPMAIF driver.
 *
 * @dcb: The NOA software DCB containing TX queue information.
 * @dpa_base: Array of DPA base addresses for the TX rings.
 * @return 0 on success.
 */
static int noa_md_apc2ncp_ring_memory_map_setup_tx(
	struct mtk_dpmaif_ctlb *dcb, u64 *dpa_base)
{
	u64 *desc_mmap = NoaModemApcToNcpRingDescMemoryMap;
	u64 *desc_dpa_mmap = NoaModemApcToNcpRingDescDpaMemoryMap;
	dma_addr_t *dma_addr_mmap = NoaModemApcToNcpRingDmaAddrMemoryMap;
	const u32 start_ring = kNoaModemRingTxDrb0;
	const u32 end_ring = kNoaModemRingTxDrb4;
	u32 ring_type;

	/* Map 5 NOA Tx DRB Rings */
	for (ring_type = start_ring; ring_type <= end_ring; ring_type++) {
		int txq_id = ring_type - start_ring;

		desc_mmap[ring_type] = (uintptr_t)dcb->txqs[txq_id].drb_base;
		desc_dpa_mmap[ring_type] = (uintptr_t)dpa_base[ring_type];
		dma_addr_mmap[ring_type] = dcb->txqs[txq_id].drb_dma_addr;
	}
	return 0;
}

/**
 * noa_md_apc2ncp_ring_memory_map_setup_rx() - Allocates and maps RX Refill rings for NCP.
 *
 * This function allocates coherent DMA memory for the 4 RX Refill rings,
 * which are owned and managed by the NOA driver. It also remaps them for DPA access.
 *
 * @p_md_dev: Pointer to the main NOA modem device struct.
 * @return 0 on success, negative error code on failure.
 */
static int noa_md_apc2ncp_ring_memory_map_setup_rx(struct noa_md_dev *p_md_dev)
{
	u64 *desc_mmap = NoaModemApcToNcpRingDescMemoryMap;
	u64 *desc_dpa_mmap = NoaModemApcToNcpRingDescDpaMemoryMap;
	dma_addr_t *dma_addr_mmap = NoaModemApcToNcpRingDmaAddrMemoryMap;
	const u32 *ring_size = NoaModemApcToNcpRingSize;
	const u32 start_ring = kNoaModemRingRxRefillNormalBat0;
	const u32 end_ring = kNoaModemRingRxRefillFragBat1;
	struct device *noa_dev = DCB_TO_DEV(p_md_dev->noa_dcb);
	u32 ring_type;
	size_t alloc_size;
	int ret;

	for (ring_type = start_ring; ring_type <= end_ring; ring_type++) {
		alloc_size = noa_md_apc2ncp_get_ring_item_len(ring_type, true) *
			ring_size[ring_type];

		desc_mmap[ring_type] = (uintptr_t)dma_alloc_coherent(
			noa_dev,
			alloc_size,
			(dma_addr_t *)&dma_addr_mmap[ring_type],
			GFP_KERNEL);
		if (!desc_mmap[ring_type]) {
			NOA_MD_ERROR("Failed to dma_alloc_coherent for ring %u", ring_type);
			ret = -ENOMEM;
			goto err_free_coherent;
		}
	}

	for (ring_type = start_ring; ring_type <= end_ring; ring_type++) {
		alloc_size = noa_md_apc2ncp_get_ring_item_len(ring_type, true) *
			ring_size[ring_type];

		ret = noa_md_dma_mapper_remap_sg(
			&p_md_dev->rx.mapper,
			noa_dev,  /* Source device is the DPMAIF device */
			p_md_dev->dpa_res->dpa_dev,  /* Target device is the DPA */
			(void *)desc_mmap[ring_type],
			dma_addr_mmap[ring_type],
			alloc_size,
			&desc_dpa_mmap[ring_type],
			GFP_KERNEL);
		if (ret) {
			NOA_MD_ERROR("Fail to remap refill ring %u, ret:%d", ring_type, ret);
			goto err_unmap_and_free;
		}
	}

	return 0;

err_unmap_and_free:
	noa_md_apc2ncp_ring_memory_map_release_rx(p_md_dev);
	return ret;

err_free_coherent:
	while (ring_type-- > kNoaModemRingRxRefillNormalBat0) {
		alloc_size = noa_md_apc2ncp_get_ring_item_len(ring_type, true) *
			ring_size[ring_type];
		dma_free_coherent(
			noa_dev,
			alloc_size,
			(void *)desc_mmap[ring_type],
			(dma_addr_t)dma_addr_mmap[ring_type]);
		desc_mmap[ring_type] = 0;
		dma_addr_mmap[ring_type] = 0;
	}
	return ret;
}

/**
 * noa_md_apc2ncp_ring_memory_map_release_tx() - Clears pointers to TX DRB rings.
 *
 * This function does NOT free any memory, as the TX rings are owned by the
 * DPMAIF driver. It only nullifies the pointers in the global map arrays.
 */
static void noa_md_apc2ncp_ring_memory_map_release_tx(void)
{
	u64 *desc_mmap = NoaModemApcToNcpRingDescMemoryMap;
	u64 *desc_dpa_mmap = NoaModemApcToNcpRingDescDpaMemoryMap;
	dma_addr_t *dma_addr_mmap = NoaModemApcToNcpRingDmaAddrMemoryMap;
	const u32 start_ring = kNoaModemRingTxDrb0;
	const u32 end_ring = kNoaModemRingTxDrb4;
	u32 ring_type;

	for (ring_type = start_ring; ring_type <= end_ring; ring_type++) {
		desc_mmap[ring_type] = 0;
		desc_dpa_mmap[ring_type] = 0;
		dma_addr_mmap[ring_type] = 0;
	}
}

/**
 * noa_md_apc2ncp_ring_memory_map_release_rx() - Frees allocated RX Refill rings.
 *
 * This function frees the coherent DMA memory that was allocated for the
 * RX Refill rings during the setup phase.
 *
 * @p_md_dev: Pointer to the main NOA modem device struct.
 */
static void noa_md_apc2ncp_ring_memory_map_release_rx(struct noa_md_dev *p_md_dev)
{
	struct device *noa_dev = DCB_TO_DEV(p_md_dev->noa_dcb);
	u64 *desc_mmap = NoaModemApcToNcpRingDescMemoryMap;
	u64 *desc_dpa_mmap = NoaModemApcToNcpRingDescDpaMemoryMap;
	dma_addr_t *dma_addr_mmap = NoaModemApcToNcpRingDmaAddrMemoryMap;
	const u32 *ring_size = NoaModemApcToNcpRingSize;
	const u32 start_ring = kNoaModemRingRxRefillNormalBat0;
	const u32 end_ring = kNoaModemRingRxRefillFragBat1;
	u32 ring_type;

	/* Only release RX Refill Rings, which this module owns. */
	for (ring_type = start_ring; ring_type <= end_ring; ring_type++) {
		void *cpu_addr = (void *)desc_mmap[ring_type];
		if (cpu_addr) {
			noa_md_dma_unmap_by_addr(&p_md_dev->mapper, cpu_addr);
		}

		if (desc_mmap[ring_type] &&
			dma_addr_mmap[ring_type]) {
			size_t alloc_size =
				noa_md_apc2ncp_get_ring_item_len(ring_type, true) *
				ring_size[ring_type];
			dma_free_coherent(
				noa_dev, alloc_size,
				(void *)desc_mmap[ring_type],
				(dma_addr_t)dma_addr_mmap[ring_type]);
		}
		desc_mmap[ring_type] = 0;
		desc_dpa_mmap[ring_type] = 0;
		dma_addr_mmap[ring_type] = 0;
	}
}

static int noa_md_fw_alloc_and_init(struct noa_md_dev *p_md_dev)
{
	struct mtk_md_dev *mdev = NULL;
	struct mtk_dpmaif_ctlb *dpmaif_dcb = NULL;
	struct mtk_dpmaif_ctlb *noa_dcb = NULL;
	struct noa_md_fw *md_fw = NULL;

	NOA_MD_INFO("enter, p_md_dev=0x%p");
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	mdev = DCB_TO_MDEV(dpmaif_dcb);
	CHECK_PTR_OR_RETURN_ERR(mdev, -EINVAL);

	p_md_dev->mdev = mdev;
	p_md_dev->dpmaif_dcb = dpmaif_dcb;

	noa_dcb = noa_md_noa_dcb_init(p_md_dev);
	if (noa_md_dcb_validate(noa_dcb)) {
		NOA_MD_ERROR("dcb check failed");
		return -EINVAL;
	}

	/* Allocate memory for noa_md_fw structure */
	md_fw = kzalloc(sizeof(struct noa_md_fw), GFP_KERNEL);
	if (!md_fw) {
		NOA_MD_ERROR("md_fw alloc fail!");
		noa_md_dpmaif_dcb_deinit(noa_dcb);
		return -ENOMEM;
	}

	/* Initialize noa_md_fw structure */
	md_fw->dev = p_md_dev->dev;
	md_fw->drv_info = dpmaif_dcb->drv_info;
	md_fw->noa_drv_info = noa_dcb->drv_info;

	NOA_MD_INFO("unified_desc_enabled=%d", p_md_dev->feature_ctrl.unified_desc_enabled);
	if (p_md_dev->feature_ctrl.unified_desc_enabled) {
		p_md_dev->dcb = noa_dcb;
	} else {
		p_md_dev->dcb = dpmaif_dcb;
	}

	p_md_dev->md_fw = md_fw;

	NOA_MD_INFO("exit");
	return 0;
}

static int noa_md_fw_setup_tx(struct noa_md_dev* p_md_dev)
{
	struct noa_md_fw *md_fw = NULL;
	struct mtk_dpmaif_ctlb *dpmaif_dcb = NULL;  // Data Control block for DPMAIF(Modem)
	struct mtk_dpmaif_ctlb *noa_dcb = NULL;  // Data Control block for NOA driver
	struct dpmaif_drv_info *drv_info = NULL;
	unsigned char txq_cnt = 0;

	// txqs_for_modem:
	//   Points to the DPMAIF TX queues used by the modem.
	//   It points to tx queues of dpmaif_dcb.
	// txqs_for_noa:
	//   Points to the DPMAIF TX queues used by NOA.
	//   When unified_desc_enabled is true,
	//   txqs_for_noa points to tx queues of noa_dcb,
	//   otherwise it points to tx queues of dpmaif_dcb.
	struct dpmaif_txq *txqs_for_modem = NULL;
	struct dpmaif_txq *txqs_for_noa = NULL;

	NOA_MD_INFO("enter");

	// Check for NULL pointer before proceeding
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	md_fw = p_md_dev->md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(md_fw->dev, -EINVAL);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	drv_info = md_fw->drv_info;
	CHECK_PTR_OR_RETURN_ERR(drv_info, -EINVAL);
	txq_cnt = drv_info->cfg->tx_cfg.txq_cnt;

	txqs_for_modem = dpmaif_dcb->txqs;
	txqs_for_noa = noa_dcb->txqs;

	// Update TX queue information for each queue
	for (int i = 0; i < txq_cnt; i++) {
		struct dpmaif_txq *txq_source = &txqs_for_modem[i];
		struct noa_txbm *txq_dest = &p_md_dev->fw_info.txqs[i];

		txq_dest->drb_base = (uint64_t)txq_source->drb_base;
		txq_dest->drb_cnt = txq_source->drb_cnt;
		txq_dest->drb_wr_idx = txq_source->drb_wr_idx;
		txq_dest->drb_rd_idx = txq_source->drb_rd_idx;
		txq_dest->drb_rel_rd_idx = txq_source->drb_rel_rd_idx;
		txq_dest->burst_submit_cnt = txq_source->burst_submit_cnt;
		txq_dest->db_delay_ms = drv_info->cfg->tx_cfg.txqs[i].doorbell_delay;

		txqs_for_noa[i].db_delay_ns = 0; // Disable aggregation in dpmaif
	}

	NOA_MD_INFO("exit");
	return 0;
}

unsigned short noa_md_rx_tkid_to_bat_index(int bat_id, int bat_type,
	unsigned short rx_tkid)
{
	unsigned short base;

	switch (bat_type) {
	case DPMAIF_BAT:
		if (unlikely(bat_id == 0)) {
			/**
			 * For BAT 0, the base is 0, so the tkid is the index.
			 * No subtraction needed.
			 */
			return rx_tkid;
		} else if (likely(bat_id == 1)) {
			base = NOA_MD_RX_NOA_NORMAL_BAT1_BASE;
		} else {
			NOA_MD_ERROR("Invalid bat_id %d for DPMAIF_BAT", bat_id);
			return NOA_MD_RX_INVALID_BAT_INDEX;
		}
		break;
	case DPMAIF_FRAG:
		if (likely(bat_id == 0)) {
			base = NOA_MD_RX_NOA_FRAG_BAT0_BASE;
		} else if (likely(bat_id == 1)) {
			base = NOA_MD_RX_NOA_FRAG_BAT1_BASE;
		} else {
			NOA_MD_ERROR("Invalid bat_id %d for DPMAIF_FRAG", bat_id);
			return NOA_MD_RX_INVALID_BAT_INDEX;
		}
		break;
	default:
		NOA_MD_ERROR("Invalid bat_type %d", bat_type);
		return NOA_MD_RX_INVALID_BAT_INDEX;
	}

	/* Boundary check to prevent integer underflow and out-of-bounds access. */
	if (unlikely(rx_tkid < base)) {
		NOA_MD_ERROR("rx_tkid %u is out of range for base %u", rx_tkid, base);
		return NOA_MD_RX_INVALID_BAT_INDEX;
	}

	return rx_tkid - base;
}
EXPORT_SYMBOL_GPL(noa_md_rx_tkid_to_bat_index);

/**
 * noa_md_rx_bat_ring_init_from_dpmaif() - Init a single NCP BAT ring from
 * DPMAIF state.
 * @ncp_bat_ring:      Destination NCP BAT ring to populate.
 * @noa_bat_ring:      Intermediate NOA software view of the BAT ring.
 * @dpmaif_bat_ring:   Source DPMAIF hardware-view of the BAT ring.
 * @tkid_pool:     Token ID pool for this ring.
 * @bat_id:        The numerical ID of this BAT ring.
 * @bat_type:      The type of this BAT ring (NORMAL or FRAG).
 *
 * This helper performs the core logic of syncing a single ring. It allocates
 * software token IDs (tkid) for all hardware-owned buffers and synchronizes
 * metadata like buffer counts and pointers.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_rx_bat_ring_init_from_dpmaif(
	struct noa_bat_ring *ncp_bat_ring, struct dpmaif_bat_ring *noa_bat_ring,
	const struct dpmaif_bat_ring *dpmaif_bat_ring,
	struct noa_md_rx_tkid_info *tkid_pool, int bat_id, int bat_type)
{
	unsigned short idx;

	if (unlikely(noa_bat_ring->bat_cnt < dpmaif_bat_ring->bat_cnt ||
			ncp_bat_ring->bat_cnt < dpmaif_bat_ring->bat_cnt)) {
		NOA_MD_ERROR("BAT ring size mismatch for bat_id %d", bat_id);
		return -EINVAL;
	}

	/* Initialize software-view bitmaps */
	bitmap_fill(noa_bat_ring->mask_tbl, dpmaif_bat_ring->bat_cnt);
	bitmap_copy(ncp_bat_ring->mask_tbl, dpmaif_bat_ring->mask_tbl,
		dpmaif_bat_ring->bat_cnt);

	memset(noa_bat_ring->sw_record_base, 0,
	       noa_bat_ring->bat_cnt * sizeof(*noa_bat_ring->sw_record_base));

	/* Iterate through existing hardware buffers and assign software token IDs */
	for (idx = 0; idx < dpmaif_bat_ring->bat_cnt; idx++) {
		unsigned short rx_tkid;
		unsigned short mapped_rx_tkid;

		/**
		 * TODO: b/434646935 - test_bit check should be removed and placed in a
		 * more appropriate function. The reason is:
		 *
		 * 1. This is an initialization (init) function. Its main purpose is
		 * to create a static mapping for ALL hardware buffers to their
		 * software token IDs (rx_tkid).
		 *
		 * 2. The test_bit() check is for runtime logic to dynamically see if
		 * a buffer needs filling. It should not be in a one-time init process.
		 *
		 * 3. In the initial state, this check causes the loop to be skipped entirely,
		 * which means the token ID mapping fails.
		 */
		if (test_bit(idx, dpmaif_bat_ring->mask_tbl))
			continue;

		rx_tkid = tkid_pool->free_pool[tkid_pool->rx_tkid_free_fore];
		tkid_pool->rx_tkid_free_fore =
			mtk_dpmaif_ring_buf_get_next_idx(dpmaif_bat_ring->bat_cnt,
				tkid_pool->rx_tkid_free_fore);

		ncp_bat_ring->rx_tkid_info.rx_tkid[idx] = rx_tkid;
		mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(bat_id, bat_type, rx_tkid);
		noa_bat_ring->sw_record_base[mapped_rx_tkid] =
			dpmaif_bat_ring->sw_record_base[idx];
	}

	/* Copy ring metadata */
	ncp_bat_ring->bat_base       = dpmaif_bat_ring->bat_base;
	ncp_bat_ring->bat_dma_addr   = dpmaif_bat_ring->bat_dma_addr;
	ncp_bat_ring->bat_cnt        = dpmaif_bat_ring->bat_cnt;
	ncp_bat_ring->bat_wr_idx     = dpmaif_bat_ring->bat_wr_idx;
	ncp_bat_ring->bat_rd_idx     = dpmaif_bat_ring->bat_rd_idx;
	ncp_bat_ring->max_reload_cnt = dpmaif_bat_ring->max_reload_cnt;
	ncp_bat_ring->to_reload_cnt  = dpmaif_bat_ring->to_reload_cnt;
	ncp_bat_ring->bat_stats      = dpmaif_bat_ring->bat_stats;
	ncp_bat_ring->doorbell_th    = dpmaif_bat_ring->doorbell_th;
	ncp_bat_ring->buf_size       = dpmaif_bat_ring->buf_size;

	noa_bat_ring->max_reload_cnt = dpmaif_bat_ring->max_reload_cnt;
	noa_bat_ring->to_reload_cnt  = dpmaif_bat_ring->to_reload_cnt;

	return 0;
}

int noa_md_rx_bat_rings_init_from_dpmaif(struct noa_md_dev* p_md_dev)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct noa_md_fw *md_fw;
	struct noa_md_fw_rx *rx;
	struct dpmaif_drv_info *drv_info;
	struct dpmaif_bat_info *dpmaif_bat_infos;
	struct dpmaif_bat_info *noa_bat_infos;
	struct noa_bat_info *ncp_bat_infos;
	unsigned char bat_ring_num;
	int i, ret;

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	md_fw = p_md_dev->md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);

	rx = md_fw->rx;
	CHECK_PTR_OR_RETURN_ERR(rx, -EINVAL);

	drv_info = md_fw->drv_info;
	CHECK_PTR_OR_RETURN_ERR(drv_info, -EINVAL);

	bat_ring_num = drv_info->cfg->rx_cfg.bat_ring_num;
	NOA_MD_INFO("Initializing %d BAT rings from DPMAIF state.", bat_ring_num);

	dpmaif_bat_infos = dpmaif_dcb->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_bat_infos, -EINVAL);

	noa_bat_infos = noa_dcb->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(noa_bat_infos, -EINVAL);

	ncp_bat_infos = rx->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(ncp_bat_infos, -EINVAL);

	for (i = 0; i < bat_ring_num; i++) {
		NOA_MD_INFO("Initializing BAT ring id: %d", i);

		/* Process Normal BAT Ring */
		ret = noa_md_rx_bat_ring_init_from_dpmaif(
			&ncp_bat_infos[i].normal_bat_ring,
			&noa_bat_infos[i].normal_bat_ring,
			&dpmaif_bat_infos[i].normal_bat_ring,
			&p_md_dev->rx.normal_tkid_infos[i],
			i, DPMAIF_BAT);
		if (unlikely(ret)) {
			NOA_MD_ERROR("Failed to init normal BAT ring %d: %d", i, ret);
			return ret;
		}

		if (!noa_bat_infos[i].frag_bat_enabled) {
			continue;
		}

		/* Process Fragment BAT Ring */
		ret = noa_md_rx_bat_ring_init_from_dpmaif(
			&ncp_bat_infos[i].frag_bat_ring,
			&noa_bat_infos[i].frag_bat_ring,
			&dpmaif_bat_infos[i].frag_bat_ring,
			&p_md_dev->rx.frag_tkid_infos[i],
			i, DPMAIF_FRAG);
		if (unlikely(ret)) {
			NOA_MD_ERROR("Failed to init frag BAT ring %d: %d", i, ret);
			return ret;
		}
	}

	return 0;
}

/**
 * noa_md_rx_bat_ring_init_ncp - Initialize SW context for a NCP RX BAT ring
 * @bat_ring_source: (Read-only) Pointer to the source DPMAIF BAT ring config.
 * @bat_ring_dest:   Pointer to the destination NOA BAT ring to be initialized.
 *
 * This function copies configuration from a source structure, allocates
 * necessary software buffers (mask table, data address array, TKID info),
 * initializes the TKID free pool, and sets the initial TKID values.
 *
 * Return: 0 on success, -ENOMEM on memory allocation failure.
 */
static int noa_md_rx_bat_ring_init_ncp(
	const struct dpmaif_bat_ring *bat_ring_source,
	struct noa_bat_ring *bat_ring_dest)
{
	unsigned short *rx_tkid;
	unsigned int dest_bat_cnt;

	NOA_MD_INFO("enter");

	bat_ring_dest->bat_base = bat_ring_source->bat_base;
	bat_ring_dest->bat_cnt = bat_ring_source->bat_cnt;
	bat_ring_dest->max_reload_cnt = bat_ring_source->max_reload_cnt;
	bat_ring_dest->dynamic_reload = bat_ring_source->dynamic_reload;

	bat_ring_dest->id = bat_ring_source->id;
	bat_ring_dest->type = bat_ring_source->type;
	bat_ring_dest->bat_cnt_err_intr_set = bat_ring_source->bat_cnt_err_intr_set;
	bat_ring_dest->doorbell_th = bat_ring_source->doorbell_th;
	atomic_set(
		&bat_ring_dest->to_reload_cnt,
		atomic_read(&bat_ring_source->to_reload_cnt));

	spin_lock_init(&bat_ring_dest->rx_tkid_info.rx_tkid_lock);

	dest_bat_cnt = bat_ring_dest->bat_cnt;
	/* Allocate buffer for SW to recycle BAT. */
	bat_ring_dest->mask_tbl = bitmap_alloc(dest_bat_cnt, GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(bat_ring_dest->mask_tbl, err_out);

	NOA_MD_INFO("dest_bat_cnt: %d", dest_bat_cnt);
	bitmap_fill(bat_ring_dest->mask_tbl, dest_bat_cnt);

	bat_ring_dest->noa_data_addr = kcalloc(dest_bat_cnt,
		sizeof(*bat_ring_dest->noa_data_addr), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(bat_ring_dest->noa_data_addr, err_free_mask_tbl);

	bat_ring_dest->noa_data_addr_apc = kcalloc(dest_bat_cnt,
		sizeof(*bat_ring_dest->noa_data_addr_apc), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(bat_ring_dest->noa_data_addr_apc, err_free_data_addr);

	/* rx_tkid initialization */
	rx_tkid = kcalloc(dest_bat_cnt, sizeof(*rx_tkid), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(rx_tkid, err_free_data_addr_apc);

	/* NCP rx tkid free pool */
	bat_ring_dest->rx_tkid_info.free_pool = kcalloc(dest_bat_cnt,
		sizeof(*bat_ring_dest->rx_tkid_info.free_pool), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(
		bat_ring_dest->rx_tkid_info.free_pool, err_free_rx_tkid);

	/* TODO: b/434646935 - Remove if bat_ring_dest is guaranteed to be zeroed by kzalloc. */
	bat_ring_dest->rx_tkid_info.rx_tkid_free_fore = 0;
	bat_ring_dest->rx_tkid_info.rx_tkid_free_rear = 0;

	/* Initial NCP default value of bid and rx_tkid mapping */
	for (int i = 0; i < dest_bat_cnt; i++)
		rx_tkid[i] = NOA_MD_RX_INVALID_TKID;

	bat_ring_dest->rx_tkid_info.rx_tkid = rx_tkid;
	return 0;

err_free_rx_tkid:
	if (rx_tkid) {
		kfree(rx_tkid);
	}
err_free_data_addr_apc:
	if (bat_ring_dest->noa_data_addr_apc) {
		kfree(bat_ring_dest->noa_data_addr_apc);
		bat_ring_dest->noa_data_addr_apc = NULL;
	}
err_free_data_addr:
	if (bat_ring_dest->noa_data_addr) {
		kfree(bat_ring_dest->noa_data_addr);
		bat_ring_dest->noa_data_addr = NULL;
	}
err_free_mask_tbl:
	if (bat_ring_dest->mask_tbl) {
		bitmap_free(bat_ring_dest->mask_tbl);
		bat_ring_dest->mask_tbl = NULL;
	}
err_out:
	NOA_MD_ERROR("Failed to initialize NCP RX BAT ring, id:%u, type:%d",
		bat_ring_dest->id, bat_ring_dest->type);
	return -ENOMEM;
}

/*
 * noa_md_rx_bat_ring_deinit_ncp - De-initialize a NCP RX BAT ring
 * @bat_ring_dest: Pointer to the NOA BAT ring to be de-initialized.
 */
static void noa_md_rx_bat_ring_deinit_ncp(struct noa_bat_ring *bat_ring_dest)
{
	/* The cleanup order is the reverse of allocation. */
	if (bat_ring_dest->rx_tkid_info.free_pool) {
		kfree(bat_ring_dest->rx_tkid_info.free_pool);
		bat_ring_dest->rx_tkid_info.free_pool = NULL;
	}

	if (bat_ring_dest->rx_tkid_info.rx_tkid) {
		kfree(bat_ring_dest->rx_tkid_info.rx_tkid);
		bat_ring_dest->rx_tkid_info.rx_tkid = NULL;
	}

	if (bat_ring_dest->noa_data_addr) {
		kfree(bat_ring_dest->noa_data_addr);
		bat_ring_dest->noa_data_addr = NULL;
	}

	if (bat_ring_dest->mask_tbl) {
		bitmap_free(bat_ring_dest->mask_tbl);
		bat_ring_dest->mask_tbl = NULL;
	}
}

/**
 * noa_md_rx_init_ncp_bat_info - Initialize normal and fragment BAT info
 * @bat_info_source: (Read-only) Pointer to the source DPMAIF BAT info config.
 * @bat_info_dest:   Pointer to the destination NOA BAT info to be initialized.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_rx_init_ncp_bat_info(
	const struct dpmaif_bat_info *bat_info_source,
	struct noa_bat_info *bat_info_dest)
{
	int ret;

	NOA_MD_INFO("enter");

	/* NCP normal bat ring init */
	ret = noa_md_rx_bat_ring_init_ncp(
		&bat_info_source->normal_bat_ring,
		&bat_info_dest->normal_bat_ring);
	if (ret) {
		NOA_MD_ERROR("Failed to init normal bat ring: %d", ret);
		return ret;
	}

	/* NCP fragment bat ring init */
	if (bat_info_source->frag_bat_enabled) {
		NOA_MD_INFO("frag_bat_enabled");
		ret = noa_md_rx_bat_ring_init_ncp(
			&bat_info_source->frag_bat_ring,
			&bat_info_dest->frag_bat_ring);
		if (ret) {
			NOA_MD_ERROR("Failed to init frag bat ring: %d", ret);
			noa_md_rx_bat_ring_deinit_ncp(&bat_info_dest->normal_bat_ring);
			return ret;
		}
	}

	return ret;
}

static int noa_md_rx_queue_sync(
	struct dpmaif_rxq *rxq_source,
	struct noa_rx_queue *rxq_dest)
{
	CHECK_PTR_OR_RETURN_ERR(rxq_source, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(rxq_dest, -EINVAL);
	rxq_dest->dcb = rxq_source->dcb;
	rxq_dest->id = rxq_source->id;
	rxq_dest->started = rxq_source->started;
	rxq_dest->pit_base = rxq_source->pit_base;
	rxq_dest->pit_dma_addr = rxq_source->pit_dma_addr;
	rxq_dest->pit_cnt = rxq_source->pit_cnt;
	rxq_dest->pit_wr_idx = rxq_source->pit_wr_idx;
	rxq_dest->pit_rd_idx = rxq_source->pit_rd_idx;
	rxq_dest->pit_rel_rd_idx = rxq_source->pit_rel_rd_idx;
	rxq_dest->pit_seq_expect = rxq_source->pit_seq_expect;
	// TODO: b/381183653 - Support PIT/BAT release control
	rxq_dest->pit_poll_enable = rxq_source->pit_poll_enable;
	atomic_set(&rxq_dest->pit_rel_cnt, atomic_read(&rxq_source->pit_rel_cnt));
	atomic_set(&rxq_dest->pit_stats, atomic_read(&rxq_source->pit_stats));
	rxq_dest->pit_cnt_err_intr_set = rxq_source->pit_cnt_err_intr_set;
	rxq_dest->pit_burst_rel_cnt = rxq_source->pit_burst_rel_cnt;
	rxq_dest->pit_seq_fail_cnt = rxq_source->pit_seq_fail_cnt;
	rxq_dest->pit_seq_max = rxq_source->pit_seq_max;
	rxq_dest->bat_ring_id = rxq_source->bat_ring_id;
	rxq_dest->attr = rxq_source->attr;
	rxq_dest->intr_coalesce_frame = rxq_source->intr_coalesce_frame;
	rxq_dest->ws = rxq_source->ws;
	return 0;
}

static int noa_md_rx_set_rpc_modem_cmd(
	struct noa_rx_queue *rxq_noa_md,
	struct noa_rxbm *rxq_rpc)
{
	CHECK_PTR_OR_RETURN_ERR(rxq_noa_md, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(rxq_rpc, -EINVAL);
	rxq_rpc->pit_base = (uint64_t)rxq_noa_md->pit_base;
	rxq_rpc->pit_cnt = rxq_noa_md->pit_cnt;
	rxq_rpc->pit_seq_max = rxq_noa_md->pit_seq_expect;
	rxq_rpc->bat_ring_id = rxq_noa_md->bat_ring_id;
	rxq_rpc->pit_wr_idx = rxq_noa_md->pit_wr_idx;
	rxq_rpc->pit_rd_idx = rxq_noa_md->pit_rd_idx;
	rxq_rpc->pit_rel_rd_idx = rxq_noa_md->pit_rel_rd_idx;

	NOA_MD_INFO("pit_base=[0x%lx], pit_cnt=[%d], pit_seq_max=[%d], "
		"bat_ring_id=[%d], pit_wr_idx=[%d], pit_rd_idx=[%d], pit_rel_rd_idx=[%d]",
		rxq_rpc->pit_base, rxq_rpc->pit_cnt, rxq_rpc->pit_seq_max,
		rxq_rpc->bat_ring_id, rxq_rpc->pit_wr_idx,
		rxq_rpc->pit_rd_idx, rxq_rpc->pit_rel_rd_idx);

	return 0;
}

static int noa_md_fw_setup_rx(struct noa_md_dev* p_md_dev)
{
	struct noa_md_fw *md_fw = NULL;
	struct mtk_dpmaif_ctlb *dpmaif_dcb = NULL;
	struct mtk_dpmaif_ctlb *noa_dcb = NULL;
	struct dpmaif_drv_info *drv_info = NULL;
	struct noa_md_fw_rx *rx = NULL;
	struct noa_bat_info *bat_infos = NULL;
	struct noa_rx_queue *dpmaif_rxqs = NULL;
	struct modem_fw_ring_ops *ring_ops = NULL;
	struct noa_md_fw_ring *rx_ring = NULL;
	unsigned char rxq_cnt = 0;
	unsigned char bat_ring_num = 0;
	int ret = 0;

	// rxqs_for_modem:
	//   Points to the DPMAIF RX queues used by the modem.
	//   It points to rx queues of dpmaif_dcb.
	// rxqs_for_noa:
	//   Points to the DPMAIF RX queues used by NOA.
	//   When unified_desc_enabled is true,
	//   rxqs_for_noa points to rx queues of noa_dcb,
	//   otherwise it points to rx queues of dpmaif_dcb.
	struct dpmaif_rxq *rxqs_for_modem = NULL;

	NOA_MD_INFO("enter");
	// Check for NULL pointer before proceeding
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	md_fw = p_md_dev->md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(md_fw->dev, -EINVAL);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	drv_info = md_fw->drv_info;
	CHECK_PTR_OR_RETURN_ERR(drv_info, -EINVAL);
	rxq_cnt = drv_info->cfg->rx_cfg.rxq_cnt;
	bat_ring_num = drv_info->cfg->rx_cfg.bat_ring_num;

	// Allocate memory for RX-related data structures
	rx = kzalloc(sizeof(*rx), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(rx, error);

	bat_infos = kzalloc(sizeof(*bat_infos) * bat_ring_num, GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(bat_infos, error);

	dpmaif_rxqs = kzalloc(sizeof(*dpmaif_rxqs) * rxq_cnt, GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(dpmaif_rxqs, error);

	ring_ops = kzalloc(sizeof(*ring_ops), GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(ring_ops, error);

	rx_ring = kzalloc(sizeof(*rx_ring) * kNoaModemRingRxDataEnd, GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(rx_ring, error);

	rxqs_for_modem = dpmaif_dcb->rxqs;

	// Initial noa_md_rx_tkid_info
	ret = noa_md_rx_tkid_info_setup();
	if (ret < 0) {
		NOA_MD_ERROR("Fail to init noa_md_rx_tkid_info, ret=[%d]", ret);
	}

	// Initial NCP BAT information
	for (int i = 0; i < bat_ring_num; i++) {
		struct dpmaif_bat_info *bat_info_source = &dpmaif_dcb->bat_infos[i];
		struct noa_bat_info *bat_info_dest = &bat_infos[i];

		bat_info_dest->max_mtu = bat_info_source->max_mtu;
		bat_info_dest->frag_bat_enabled = bat_info_source->frag_bat_enabled;
		bat_info_dest->normal_bat_ring.buf_size = bat_info_source->normal_bat_ring.buf_size;
		bat_info_dest->frag_bat_ring.buf_size = bat_info_source->frag_bat_ring.buf_size;

		noa_md_rx_init_ncp_bat_info(bat_info_source, bat_info_dest);
	}

	// Update RX queue information for each queue
	for (int i = 0; i < rxq_cnt; i++) {
		struct dpmaif_rxq *dpmaif_rxq_source = &rxqs_for_modem[i];
		struct noa_rx_queue *dpmaif_rxq_dest = &dpmaif_rxqs[i];
		struct noa_rxbm *dpmaif_rxq_rpc = &p_md_dev->fw_info.rxqs[i];

		noa_md_rx_queue_sync(dpmaif_rxq_source, dpmaif_rxq_dest);
		noa_md_rx_set_rpc_modem_cmd(dpmaif_rxq_dest, dpmaif_rxq_rpc);
	}

	// Update rx to noa_md_fw
	rx->rxq_cnt = rxq_cnt;
	rx->doorbell_reg_addr = 0;
	rx->bat_ring_num = bat_ring_num;
	rx->bat_infos = bat_infos;
	rx->dpmaif_rxqs = dpmaif_rxqs;
	rx->ring_ops = ring_ops;
	rx->rx_ring = rx_ring;

	md_fw->rx = rx;

	NOA_MD_INFO("exit");
	return 0;

error:
	if (rx_ring) {
		kfree(rx_ring);
	}
	if (ring_ops) {
		kfree(ring_ops);
	}
	if (dpmaif_rxqs) {
		kfree(dpmaif_rxqs);
	}
	if (bat_infos) {
		kfree(bat_infos);
	}
	if (rx) {
		kfree(rx);
	}
	return -ENOMEM;
}

static int noa_md_fw_setup(struct noa_md_dev *p_md_dev,
	struct mtk_dpmaif_ctlb *dpmaif_dcb)
{
	int ret = 0;

	NOA_MD_INFO("enter");
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	p_md_dev->dpmaif_dcb = dpmaif_dcb;

	ret = noa_md_fw_alloc_and_init(p_md_dev);
	NOA_MD_INFO("noa_md_fw_alloc_and_init=%d", ret);
	if(ret < 0) {
		return ret;
	}

	ret = noa_md_fw_setup_tx(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_fw_setup_tx failed: %d", ret);
	}

	ret = noa_md_fw_setup_rx(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_fw_setup_rx failed: %d", ret);
	}

	ret = noa_md_dpmaif_rx_napi_init(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_dpmaif_rx_napi_init failed: %d", ret);
	}

	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
		ret = noa_md_dpa_register_isr(
			NOA_MD_DPA_NCP_DOORBELL, ring_type, noa_md_ncp2apc_isr, p_md_dev);
		if (ret) {
			NOA_MD_ERROR("noa_md_dpa_register_isr[%d]=[%d]", ring_type, ret);
			goto error;
		}
	}

	// TODO: b/452216362 - Remove this code when dynamic switch enabled
#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
	// noa_bat_ring/ncp_bat_ring_sync
	noa_md_rx_bat_rings_init_from_dpmaif(p_md_dev);
#endif

	ret = noa_md_fw_setup_pci(p_md_dev);
	if (ret < 0) {
		NOA_MD_ERROR("Lacks PCIe configuration information.");
		goto error;
	}

	noa_md_set_dpmaif_ring_state(NOA_DPMAIF_RING_STATE_READY, p_md_dev);

	NOA_MD_INFO("exit");

	return ret;

error:
	noa_md_fw_release(p_md_dev);
	NOA_MD_INFO("exit, ret=[%d]", ret);
	return ret;
}

static int noa_md_fw_release_tx(struct noa_md_dev* p_md_dev)
{
	NOA_MD_INFO("enter");

	// Check for NULL pointer before proceeding
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	struct noa_tx_buffer_pool *pool = &p_md_dev->tx_buffer_pool;
	if (pool->va_base && pool->dma_base) {
		dma_free_coherent(
			DCB_TO_DEV(p_md_dev->noa_dcb),
			NOA_MD_MAX_TX_PKT_SIZE * NOA_MD_FW_FIFO_SIZE,
			(void *)pool->va_base, pool->dma_base);
		pool->va_base = NULL;
		pool->pa_base = 0;
	}

	noa_md_apc2ncp_ring_apc_release();

	return 0;
}

static int noa_md_fw_release_rx(struct noa_md_dev* p_md_dev)
{
	struct noa_md_fw *md_fw = NULL;
	struct noa_md_fw_rx *rx = NULL;
	NOA_MD_INFO("enter");

	// Check for NULL pointer before proceeding
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	md_fw = p_md_dev->md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);

	rx = md_fw->rx;
	CHECK_PTR_OR_RETURN_ERR(rx, -EINVAL);

	if(rx->ring_ops) {
		kfree(rx->ring_ops);
		rx->ring_ops = NULL;
	}

	if (rx->dpmaif_rxqs) {
		kfree(rx->dpmaif_rxqs);
		rx->dpmaif_rxqs = NULL;
	}

	if (rx->rxqs) {
		kfree(rx->rxqs);
		rx->rxqs = NULL;
	}

	if (rx->bat_infos) {
		kfree(rx->bat_infos);
		rx->bat_infos = NULL;
	}

	kfree(rx);
	md_fw->rx = NULL;

	return 0;
}

static int noa_md_fw_release(struct noa_md_dev *p_md_dev)
{
	struct noa_md_fw *md_fw = NULL;
	int ret = 0;

	NOA_MD_INFO("enter");

	md_fw = p_md_dev->md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);

	// Release TX
	ret = noa_md_fw_release_tx(p_md_dev);
	if (!ret) {
		NOA_MD_ERROR("noa_md_fw_release_tx:%d", ret);
	}

	// Release RX
	ret = noa_md_fw_release_rx(p_md_dev);
	if (!ret) {
		NOA_MD_ERROR("noa_md_fw_release_rx:%d", ret);
	}

	// Release APC2NCP ring
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	struct noa_ring_wrapper *ring = NULL;
	noa_md_apc2ncp_ring_memory_map_release_tx();
	noa_md_apc2ncp_ring_memory_map_release_rx(p_md_dev);
	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
		ring = &desc[ring_type].ring;
		noa_ring_info_clean(ring);
		noa_md_dpa_unregister_isr(NOA_MD_DPA_NCP_DOORBELL, ring_type);
	}

	kfree(md_fw);
	p_md_dev->md_fw = NULL;

	NOA_MD_INFO("exit");

	return 0;
}

void noa_md_dpmaif_sw_init(void *data)
{
	struct mtk_dpmaif_ctlb *dcb = (struct mtk_dpmaif_ctlb *)data;

	NOA_MD_INFO("enter");
	if (noa_md_dcb_validate(dcb)) {
		NOA_MD_ERROR("dcb check failed");
		return;
	}

	NOA_MD_INFO("exit");
	return;
}
EXPORT_SYMBOL_GPL(noa_md_dpmaif_sw_init);

void noa_md_dpmaif_sw_init_script(void *data)
{
	struct mtk_dpmaif_ctlb *dcb = (struct mtk_dpmaif_ctlb *)data;
	int ret = 0;

	NOA_MD_INFO("enter");
	if (noa_md_dcb_validate(dcb)) {
		NOA_MD_ERROR("dcb check failed");
		return;
	}

	ret = noa_md_pcie_set_apc_msi_ctrls_enabled(false);
	NOA_MD_INFO("noa_md_pcie_enable_offload_ctrls=%d", ret);
	if (ret < 0) {
		NOA_MD_ERROR("Failed to mask interrupts");
		ret = noa_md_pcie_set_apc_msi_ctrls_enabled(true);
		NOA_MD_INFO("noa_md_pcie_enable_offload_ctrls=%d", ret);
	}

	ret = noa_md_fw_setup(&md_dev, dcb);

	// TODO: These 3 functions need to be called by proper state
	noa_md_dpmaif_rx_napi_init(&md_dev);
	noa_md_dpmaif_task_resume(&md_dev);
	noa_md_dpmaif_rx_napi_enable(&md_dev);

	// Disable LRO when switch to offload mode
	noa_md_dpmaif_hw_lro_set(&md_dev, false);

	NOA_MD_INFO("exit, ret=[%d]", ret);
	return;
}
EXPORT_SYMBOL_GPL(noa_md_dpmaif_sw_init_script);

void noa_md_dpmaif_sw_reset(void *data)
{
	NOA_MD_INFO("enter");
	// TODO: Check reset process
	return;
}
EXPORT_SYMBOL_GPL(noa_md_dpmaif_sw_reset);

void noa_md_dpmaif_sw_exit(void *data)
{
	struct mtk_dpmaif_ctlb *dcb = (struct mtk_dpmaif_ctlb *)data;
	int ret = 0;
	NOA_MD_INFO("enter");

	if (noa_md_dcb_validate(dcb)) {
		NOA_MD_ERROR("dcb check failed");
		return;
	}

	ret = noa_md_fw_release(&md_dev);

	NOA_MD_INFO("exit, ret=[%d]", ret);
	return;
}
EXPORT_SYMBOL_GPL(noa_md_dpmaif_sw_exit);

void noa_md_pcie_probe_done(void *data)
{
	struct mtk_dpmaif_ctlb *dcb = (struct mtk_dpmaif_ctlb *)data;
	int ret = 0;

	NOA_MD_INFO("enter");
	if (noa_md_dcb_validate(dcb)) {
		NOA_MD_ERROR("dcb check failed");
		return;
	}

	ret = noa_md_fw_setup(&md_dev, dcb);


	NOA_MD_INFO("exit, ret=[%d]", ret);
	return;
}
EXPORT_SYMBOL_GPL(noa_md_pcie_probe_done);

void noa_md_wwan_init(void *data)
{
	// TODO: Check mtk_wwan_init process
	if (noa_md_wpr_is_noa_rx_enable()) {
		noa_md_dpmaif_rx_napi_init(&md_dev);
	}
	return;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_init);

void noa_md_wwan_setup(void *data)
{
	return;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_setup);

void noa_md_wwan_open(void *data)
{
	if (noa_md_wpr_is_noa_rx_enable()) {
		noa_md_dpmaif_task_resume(&md_dev);
	}
	return;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_open);

void noa_md_wwan_stop(void *data)
{
	return;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_stop);

void noa_md_wwan_exit(void *data)
{
	// TODO: Check mtk_wwan_exit process
	if (noa_md_wpr_is_noa_rx_enable()) {
		noa_md_dpmaif_rx_napi_exit(&md_dev);
	}
	return;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_exit);

void noa_md_wwan_data_event(void *data)
{
	struct noa_md_wpr_custom_evt_data *evt_data = data;
	int evt = evt_data->evt;

	switch (evt) {
	case DATA_EVT_RX_START:
		if (noa_md_wpr_is_noa_rx_enable()) {
			noa_md_dpmaif_rx_napi_enable(&md_dev);
		} else {
			NOA_MD_ERROR_LIMIT(
				"Get DATA_EVT_RX_START but RX feature is not enabled");
		}
		break;
	case DATA_EVT_RX_STOP:
		if (noa_md_wpr_is_noa_rx_enable() &&
		    !noa_md_rx_skip_napi_disable_during_dynamic_switch(&md_dev)) {
			noa_md_dpmaif_rx_napi_disable(&md_dev);
		} else {
			NOA_MD_ERROR_LIMIT(
				"Get DATA_EVT_RX_STOP but RX feature is not enabled");
		}
		break;
	default:
		break;
	}
	// NEP/NCP work process
	return;
}
EXPORT_SYMBOL_GPL(noa_md_wwan_data_event);

void noa_md_netdev_update(void *data)
{
	struct mtk_wwan_ctlb *wcb = (struct mtk_wwan_ctlb *)data;
	unsigned long flags;

	md_dev.netdev_cnt = MTK_NETDEV_MAX;

	if (!wcb) {
		NOA_MD_ERROR("exit, wcb is null");
		return;
	}

	spin_lock_irqsave(&md_dev.netdev_update_lock, flags);

	memset(md_dev.netdevs, 0, sizeof(md_dev.netdevs));
	memset(md_dev.wwan_inst, 0, sizeof(md_dev.wwan_inst));

	for (int i = 0; i < MTK_NETDEV_MAX; i++) {
		struct mtk_wwan_instance *wwan_inst = wcb->wwan_inst[i];
		if (!wwan_inst) {
			NOA_MD_DEBUG_LIMIT("wwan_inst[%d] is null", i);
			continue;
		}

		md_dev.netdevs[i] = wwan_inst->netdev;
		if (!md_dev.netdevs[i]) {
			NOA_MD_DEBUG_LIMIT("netdevs[%d] is null", i);
			continue;
		}

		md_dev.wwan_inst[i] = wwan_inst;
		NOA_MD_DATA("idx=[%d], wwan_inst=[0x%p], netdevs=[0x%p]",
			i, md_dev.wwan_inst[i], md_dev.netdevs[i]);
	}

	spin_unlock_irqrestore(&md_dev.netdev_update_lock, flags);

	return;
}
EXPORT_SYMBOL_GPL(noa_md_netdev_update);

int noa_md_data_irq_handle(void *data)
{
#if IS_ENABLED(CONFIG_NOA_SIM_SUPPORT) && \
	!IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
	struct dpmaif_irq_data *irq_data = (struct dpmaif_irq_data *)data;
	// NOA md data rx step 9: According to the interrupt type,
	// call the event processing function corresponding to the NCP layer
	NOA_MD_DATA_LIMIT(
		"enter, intr_type=[%d], q_mask=[%d]",
		(int)irq_data->intr_type, irq_data->q_mask);
	noa_ncp_md_dpmaif_irq_handle(irq_data->intr_type, irq_data->q_mask);
	return 0;
#else
	// In Full SoC mode, the NCP modem should receive IRQs from the fake modem.
	// We don't directly call noa_ncp_md_dpmaif_irq_handle within the NCP
	// modem module to simulate sending IRQs. Therefore, a macro is used to
	// avoid compilation issues.
	return -EINVAL;
#endif
}
EXPORT_SYMBOL_GPL(noa_md_data_irq_handle);


int noa_md_pm_suspend(void *data)
{
	NOA_MD_INFO("enter");
	struct noa_md_wpr_mtk_t900_pm_data *pm_dat =
		(struct noa_md_wpr_mtk_t900_pm_data *)data;
	if (!pm_dat) {
		NOA_MD_INFO("pm_dat is null");
	}
	return 0;
}

int noa_md_pm_suspend_late(void *data)
{
	NOA_MD_INFO("enter");
	struct noa_md_wpr_mtk_t900_pm_data *pm_dat =
		(struct noa_md_wpr_mtk_t900_pm_data *)data;
	if (!pm_dat) {
		NOA_MD_INFO("pm_dat is null");
	}
	return 0;
}

int noa_md_pm_resume_early(void *data)
{
	NOA_MD_INFO("enter");
	struct noa_md_wpr_mtk_t900_pm_data *pm_dat =
		(struct noa_md_wpr_mtk_t900_pm_data *)data;
	if (!pm_dat) {
		NOA_MD_INFO("pm_dat is null");
	}
	return 0;
}

int noa_md_pm_resume(void *data)
{
	NOA_MD_INFO("enter");
	struct noa_md_wpr_mtk_t900_pm_data *pm_dat =
		(struct noa_md_wpr_mtk_t900_pm_data *)data;
	if (!pm_dat) {
		NOA_MD_INFO("pm_dat is null");
	}
	return 0;
}

void noa_md_ring_service_tx_activate(bool activate)
{
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
	if (activate) {
		google_dpa_ring_service_rpc_event_activate(
			NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
					     kNoaModemRingTxData),
			kNoaRingNepInput);
	} else {
		google_dpa_ring_service_rpc_event_deactivate(
			NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
					     kNoaModemRingTxData),
			kNoaRingNepInput);
	}
#else
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, activate,
					  NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
							   kNoaNetworkFlowHostToDevice,
							   kNoaModemRingTxData),
					  kNoaRingNepInput);
#endif
}

void noa_md_ring_service_rx_activate(bool activate)
{
	for (int ring_type = 0; ring_type < kNoaModemRingRxDataEnd; ring_type++) {
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
		if (activate) {
			google_dpa_ring_service_rpc_event_activate(
				NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
						     kNoaNetworkFlowDeviceToHost, ring_type),
				kNoaRingNepOutput);
		} else {
			google_dpa_ring_service_rpc_event_deactivate(
				NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
						     kNoaNetworkFlowDeviceToHost, ring_type),
				kNoaRingNepOutput);
		}
#else
		nep_ring_service_request_send(
			NEP_CMD_RING_ACTIVE,
			activate,
			NoaRingPathIdConvert(
				kNoaNetworkInterfaceModem,
				kNoaNetworkFlowDeviceToHost,
				ring_type),
				kNoaRingNepOutput);
#endif
	}
}

int noa_md_rx_ring_setup_activate(struct noa_md_dev *p_md_dev)
{
	int ring_type;
	int ret = 0;
	NOA_MD_INFO("enter");
	ret = noa_md_rx_ring_setup(p_md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_rx_ring_setup=[%d]", ret);
		goto error;
	}

	for (ring_type = 0; ring_type < kNoaModemRingRxDataEnd; ring_type++) {
		NOA_MD_INFO("noa_ring_activate, ring_type=[%d]", ring_type);
		noa_ring_activate(&p_md_dev->rx.rx_buffer_desc[ring_type].ring);
	}

	return ret;
error:
	noa_md_rx_ring_release(p_md_dev);
	NOA_MD_ERROR("exit, ret=[%d]", ret);
	return ret;
}

int noa_md_tx_ring_setup_activate(struct noa_md_dev *p_md_dev)
{
	struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_SW);
	int ret = 0;

	NOA_MD_INFO("enter");

	p_md_dev->tx.noa_hw_tx_doorbell_addr = (unsigned long)&port->doorbell;
	p_md_dev->rx.noa_hw_rx_ints_addr = (unsigned long)&port->ints;

	ret = noa_md_tx_ring_setup(p_md_dev);

	if (ret) {
		goto error;
	}

	NOA_MD_INFO("noa_ring_activate");
	noa_ring_activate(&p_md_dev->tx.tx_buffer_desc->ring);  //noa_ring_wrapper

	NOA_MD_INFO("exit");
	return 0;

error:
	// TODO: b/433460810 - Review cleanup logic. This calls tx/rx release
	// functions directly. Check if a single noa_md_ring_release function
	// is needed here for consistency.
	noa_md_tx_ring_release(p_md_dev);
	NOA_MD_ERROR("exit, ret=[%d]", ret);
	return ret;
}

int noa_md_ring_release(struct noa_md_dev *p_md_dev)
{
	NOA_MD_INFO("enter");

	noa_md_ring_service_tx_activate(false);
	noa_md_ring_service_rx_activate(false);
	// Release TX ring resource
	noa_md_tx_ring_release(p_md_dev);

	// Release RX ring resource
	noa_md_rx_ring_release(p_md_dev);

	NOA_MD_INFO("exit");

	return 0;
}

int noa_md_init(void)
{
	static u64 dmamask = DMA_BIT_MASK(PCIE_DMA_MASK);
	struct noa_md_feature_ctrl *feature_ctrl;
	int ret = 0;

	NOA_MD_INFO("enter");

	// Feature control
	feature_ctrl = &md_dev.feature_ctrl;
	feature_ctrl->tx_enabled = true;
	feature_ctrl->rx_enabled = true;
#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_UNIFIED_DESC)
	feature_ctrl->unified_desc_enabled = true;
#else
	feature_ctrl->unified_desc_enabled = false;
#endif

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_TCP_SLOW_START)
	feature_ctrl->tx_tcp_slow_start_enabled = true;
	NOA_MD_INFO("TX tcp slow start: Enabled");
#else
	feature_ctrl->tx_tcp_slow_start_enabled = false;
	NOA_MD_INFO("TX tcp slow start: Disabled");
#endif
	ret = noa_md_wpr_init();
	if (ret < 0) {
		NOA_MD_ERROR("noa_md_wpr_init=[%d]", ret);
		return ret;
	}

	ret = alloc_chrdev_region(&md_dev.dev_num, 0, 1, NOA_MD_DEVICE_NAME);
	if (ret < 0) {
		NOA_MD_ERROR("alloc_chrdev_region=[%d]", ret);
		goto err_noa_md_wpr_exit;
	}

	md_dev.cls = class_create(NOA_MD_DEVICE_NAME);
	if (IS_ERR(md_dev.cls)) {
		NOA_MD_ERROR("exit, class_create=[%ld]", PTR_ERR(md_dev.cls));
		ret = PTR_ERR(md_dev.cls);
		goto err_unregister_chrdev_region;
	}

	md_dev.dev = device_create(
		md_dev.cls, NULL, md_dev.dev_num, NULL, NOA_MD_DEVICE_NAME "_dev");
	if (IS_ERR(md_dev.dev)) {
		NOA_MD_ERROR("exit, device_create=[%ld]", PTR_ERR(md_dev.dev));
		ret = PTR_ERR(md_dev.dev);
		goto err_class_destroy;
	}

	// After device_create，put md_dev pointer to md_dev.dev
	dev_set_drvdata(md_dev.dev, &md_dev);

	md_dev.dev->dma_mask = (u64 *)&dmamask;
	md_dev.dev->coherent_dma_mask = DMA_BIT_MASK(PCIE_DMA_MASK);
	ret = dma_set_mask(md_dev.dev, DMA_BIT_MASK(PCIE_DMA_MASK));
	if (ret) {
		NOA_MD_ERROR("dma_set_mask=[%d]", ret);
		goto err_device_destroy;
	}
	ret = dma_set_coherent_mask(md_dev.dev, DMA_BIT_MASK(PCIE_DMA_MASK));
	if (ret) {
		NOA_MD_ERROR("dma_set_coherent_mask=[%d]", ret);
		goto err_device_destroy;
	}

	// After device_create，put md_dev pointer to md_dev.dev
	dev_set_drvdata(md_dev.dev, &md_dev);

	// Set dpmaif_event_irq_handle callback
	noa_md_wpr_set_modem_irq_handler(noa_md_data_irq_handle);

	// Initialization trace
	if (md_dev.trace) {
		kobject_put(&md_dev.trace->trace_kobj);  // Release old trace kobject
	}

	md_dev.trace = noa_md_trace_init((void*)&md_dev);
	if (!md_dev.trace) {
		NOA_MD_ERROR("trace is null");
		ret = -EINVAL;
		goto err_device_destroy;
	}
	md_dev.md_tlog = noa_md_trace_log_init("noa_md_msg", 0);
	md_dev.ncp_tlog = noa_md_trace_log_init("noa_ncp_msg", 0);

	/* Initialize the spinlock used by updating netdev */
	spin_lock_init(&md_dev.netdev_update_lock);

	ret = noa_md_dpa_init(&md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_dpa_init:%d", ret);
		goto err_noa_md_trace_exit;
	}

	md_dev.shmem_sync = kzalloc(sizeof(*md_dev.shmem_sync), GFP_KERNEL);
	if (!md_dev.shmem_sync) {
		NOA_MD_ERROR("Failed to allocate shmem_sync handle");
		ret = -ENOMEM;
		goto err_noa_md_dpa_release;
	}

	ret = noa_md_shmem_sync_init(md_dev.shmem_sync, &md_dev);
	if (ret) {
		NOA_MD_ERROR("Failed to initialize shmem_sync handle: %d", ret);
		goto err_free_shmem_sync;
	}

	ret = noa_md_dpath_ctrl_init(&md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_dpath_ctrl_init:%d", ret);
		goto err_noa_md_shmem_sync_exit;
	}

	mutex_init(&md_dev.fw_init_state_lock);
	md_dev.dpmaif_ring_state = NOA_DPMAIF_RING_STATE_UNAVAILABLE;
	md_dev.dpa_state = NOA_STATE_UNAVAILABLE;

	ret = noa_md_dma_mapper_init(&md_dev.mapper, "noa_md_mapper");
	if (ret) {
		NOA_MD_ERROR("Failed to initialize noa md dma mapper=%d", ret);
		goto err_noa_md_dpath_ctrl_exit;
	}

	ret = noa_md_res_setup(&md_dev);
	if (ret) {
		NOA_MD_ERROR("noa_md_res_setup:%d", ret);
		goto err_noa_md_dma_mapper_release;
	}

	ret = noa_md_wwan_notifier_init();
	if (ret) {
		NOA_MD_ERROR("noa_md_wwan_notifier_init:%d", ret);
		goto err_noa_md_res_release;
	}

	ret = noa_md_debug_init(&md_dev);
	if (ret) {
		NOA_MD_ERROR("Failed to initialize NOA MD debugfs (or it's disabled)");
		/* Continue even if debugfs fails */
	}

	NOA_MD_INFO("exit");
	return 0;

err_noa_md_res_release:
	noa_md_res_release(&md_dev);
err_noa_md_dma_mapper_release:
	noa_md_dma_mapper_release(&md_dev.mapper);
err_noa_md_dpath_ctrl_exit:
	noa_md_dpath_ctrl_exit(&md_dev);
err_noa_md_shmem_sync_exit:
	noa_md_shmem_sync_exit(md_dev.shmem_sync);
err_free_shmem_sync:
	kfree(md_dev.shmem_sync);
	md_dev.shmem_sync = NULL;
err_noa_md_dpa_release:
	noa_md_dpa_release(&md_dev);
err_noa_md_trace_exit:
	noa_md_trace_exit();
err_device_destroy:
	device_destroy(md_dev.cls, md_dev.dev_num);
err_class_destroy:
	class_destroy(md_dev.cls);
err_unregister_chrdev_region:
	unregister_chrdev_region(md_dev.dev_num, 1);
err_noa_md_wpr_exit:
	noa_md_wpr_exit();

	return ret;
}
EXPORT_SYMBOL_GPL(noa_md_init);

void noa_md_exit(void)
{
	NOA_MD_INFO("enter");

	noa_md_debug_exit();
	noa_md_wwan_notifier_exit();
	noa_md_res_release(&md_dev);
	noa_md_trace_exit();
	device_destroy(md_dev.cls, md_dev.dev_num);
	class_destroy(md_dev.cls);
	unregister_chrdev_region(md_dev.dev_num, 1);

	if (md_dev.dpa_state_client) {
		noa_md_dpa_unregister_state_client(md_dev.dpa_state_client);
		md_dev.dpa_state_client = NULL;
	}

	noa_md_wpr_exit();
	noa_md_dpath_ctrl_exit(&md_dev);

	if (md_dev.shmem_sync) {
		noa_md_shmem_sync_exit(md_dev.shmem_sync);
		kfree(md_dev.shmem_sync);
		md_dev.shmem_sync = NULL;
	}

	noa_md_trace_log_destroy(md_dev.ncp_tlog);
	md_dev.ncp_tlog = NULL;
	noa_md_trace_log_destroy(md_dev.md_tlog);
	md_dev.md_tlog = NULL;

	noa_md_dma_unmap_all(&md_dev.mapper);
	noa_md_dma_mapper_release(&md_dev.mapper);

	NOA_MD_INFO("exit");
	return;
}
EXPORT_SYMBOL_GPL(noa_md_exit);
