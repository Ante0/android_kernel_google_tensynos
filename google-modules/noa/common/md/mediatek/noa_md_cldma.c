/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * NOA Modem CLDMA Path Implementation
 */

#include <linux/slab.h>

#include "noa_md.h"
#include "noa_md_cldma.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_trace.h"
#include "t900/noa_md_mtk_priv_cldma.h"
#include "t900/noa_md_mtk_priv_cldma_drv.h"

/**
 * noa_md_cldma_offload_config_sync() - Sync CLDMA offload configuration to NCP.
 * @p_md_dev: Pointer to the main NOA modem device structure.
 * @hif_id:   The CLDMA controller index.
 * @qno:      The hardware queue number.
 *
 * This function remaps the GPD and BD descriptors of a specific CLDMA RX queue
 * into the DPA address space, and then sends the configuration to the NCP
 * via the shared memory data channel.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_cldma_offload_config_sync(struct noa_md_dev *p_md_dev,
					    int hif_id, int qno)
{
	struct noa_md_shmem_data_payload req = {0};
	struct noa_md_shmem_data_payload resp = {0};
	struct noa_md_shmem_data_cldma_offload_config_req *config = &req.cldma_config_req;
	struct noa_md_shmem_data_cldma_offload_config_resp *config_resp =
		&resp.cldma_config_resp;
	struct cldma_dev *cd = p_md_dev->cldma_dev;
	struct cldma_drv_info *drv_info;
	struct rxq *rxq;
	struct device *mdev_dev;
	struct device *dpa_dev = p_md_dev->dpa_res->dpa_dev;
	struct rx_req *req_pool;
	int ret;

	if (!cd) {
		NOA_MD_CPATH_ERROR("cldma_dev is null");
		return -EINVAL;
	}

	if (hif_id >= NR_CLDMA || qno >= HW_QUE_NUM) {
		NOA_MD_CPATH_ERROR("Invalid hif_id %d or qno %d", hif_id, qno);
		return -EINVAL;
	}

	drv_info = cd->cldma_drv_info[hif_id];
	if (!drv_info) {
		NOA_MD_CPATH_ERROR("drv_info[%d] is null", hif_id);
		return -EINVAL;
	}

	rxq = drv_info->rxq[qno];
	if (!rxq) {
		NOA_MD_CPATH_ERROR("rxq[%d] for CLDMA%d is null", qno, hif_id);
		return -EINVAL;
	}

	mdev_dev = drv_info->mdev->dev;
	req_pool = &rxq->req_pool[0];

	config->hif_id = hif_id;
	config->qno = qno;
	config->nr_gpds = rxq->nr_gpds;
	config->nr_bds = rxq->nr_bds;

	/* Remap GPD ring */
	ret = noa_md_dma_mapper_remap_sg(
		&p_md_dev->cldma.mapper,
		mdev_dev,
		dpa_dev,
		(void *)req_pool->gpd,
		req_pool->gpd_dma_addr,
		sizeof(union gpd) * rxq->nr_gpds,
		&config->gpd_dpa,
		GFP_KERNEL);
	if (ret) {
		NOA_MD_CPATH_ERROR("Fail to remap CLDMA%d RXQ%d GPD, ret:%d", hif_id, qno, ret);
		return ret;
	}

	/* Remap BD ring if present */
	if (rxq->nr_bds > 0 && req_pool->bd_dsc_pool) {
		ret = noa_md_dma_mapper_remap_sg(
			&p_md_dev->cldma.mapper,
			mdev_dev,
			dpa_dev,
			(void *)req_pool->bd_dsc_pool[0].bd,
			req_pool->bd_dsc_pool[0].bd_dma_addr,
			sizeof(union bd) * rxq->nr_bds,
			&config->bd_dpa,
			GFP_KERNEL);
		if (ret) {
			NOA_MD_CPATH_ERROR("Fail to remap CLDMA%d RXQ%d BD, ret:%d", hif_id, qno, ret);
			return ret;
		}
	}

	NOA_MD_CPATH_INFO("Sending CLDMA%d RXQ%d offload config: GPD_DPA:0x%llx, BD_DPA:0x%llx",
			  hif_id, qno, config->gpd_dpa, config->bd_dpa);

	/* Send generic data command to NCP */
	ret = noa_md_shmem_sync_send_data_cmd(p_md_dev->shmem_sync,
					      NOA_MD_SHMEM_DATA_CMD_CLDMA_OFFLOAD_CONFIG,
					      &req, &resp);
	if (ret) {
		NOA_MD_CPATH_ERROR("Failed to send CLDMA offload config cmd: %d", ret);
		return ret;
	}

	pr_info("JASON RESP:%d\n", config_resp->status);

	return 0;
}

/**
 * noa_md_cldma_on_state_change() - Handles state changes from the controller.
 * @client:      Pointer to the dpath client.
 * @state:       The new state to handle.
 * @target_path: The final data path destination.
 * @ncp_state:   NCP state response.
 *
 * This function executes the CLDMA-specific logic for each step of the switch.
 */
static void noa_md_cldma_on_state_change(
	struct noa_dpath_client *client,
	enum dpath_switch_state state,
	enum dpa_data_path target_path,
	const struct dpath_ncp_state_payload *ncp_state)
{
	struct noa_md_dpath_ctrl *ctrl = client->ctrl;
	struct noa_md_dev *p_md_dev = ctrl->dev;
	struct noa_md_cldma *cldma = &p_md_dev->cldma;
	bool success = true;

	NOA_MD_CPATH_INFO("Handling state: %d for CLDMA", state);

	switch (state) {
	case NOA_MD_DPATH_STATE_DEVICE_PREPARING:
		if (target_path == NOA_DATA_PATH_OFFLOAD) {
			NOA_MD_CPATH_INFO("Preparing CLDMA for OFFLOAD path");
			/* Sync CLDMA0 RXQ0 config as a starting point */
			if (noa_md_cldma_offload_config_sync(p_md_dev, 0, 0)) {
				NOA_MD_CPATH_ERROR("Failed to sync CLDMA offload config");
				success = false;
			}
		}
		break;

	case NOA_MD_DPATH_STATE_DEVICE_RESUMING:
		if (target_path == NOA_DATA_PATH_DIRECT) {
			/* TODO: Implement CLDMA direct path restoration */
			NOA_MD_CPATH_INFO("Restoring CLDMA for DIRECT path");
		}
		break;

	case NOA_MD_DPATH_STATE_ROLLING_BACK:
		/* TODO: Implement rollback logic */
		break;

	case NOA_MD_DPATH_STATE_SERVICE_STOPPING:
	case NOA_MD_DPATH_STATE_SERVICE_RESTARTING:
	default:
		break;
	}

	/* Report completion back to the controller. */
	noa_md_dpath_ctrl_report_completion(cldma->dpath_client, success);
}

/* Define the operations struct with our callback function */
static struct noa_dpath_client_ops cldma_dpath_ops = {
	.on_state_change = noa_md_cldma_on_state_change,
};

/**
 * noa_md_cldma_setup() - Initializes all software resources for the CLDMA path.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_cldma_setup(void)
{
	struct noa_md_cldma *cldma = &md_dev.cldma;
	int ret;

	NOA_MD_CPATH_INFO("enter");

	ret = noa_md_dma_mapper_init(&cldma->mapper, "noa_md_cldma_mapper");
	if (ret) {
		NOA_MD_CPATH_ERROR("Failed to initialize CLDMA DMA mapper: %d", ret);
		return ret;
	}

	/* Register as a data path client to receive state change notifications */
	cldma->dpath_client = noa_md_dpath_ctrl_register_client(
		&md_dev, NOA_CPATH_CLDMA, &cldma_dpath_ops);
	if (IS_ERR(cldma->dpath_client)) {
		ret = PTR_ERR(cldma->dpath_client);
		NOA_MD_CPATH_ERROR("Failed to register CLDMA dpath client: %d", ret);
		noa_md_dma_mapper_release(&cldma->mapper);
		return ret;
	}

	NOA_MD_CPATH_INFO("exit");
	return ret;
}

/**
 * noa_md_cldma_release() - Releases all software resources for the CLDMA path.
 */
void noa_md_cldma_release(void)
{
	struct noa_md_cldma *cldma = &md_dev.cldma;

	NOA_MD_CPATH_INFO("enter");

	if (cldma->dpath_client) {
		noa_md_dpath_ctrl_unregister_client(&md_dev, cldma->dpath_client);
		cldma->dpath_client = NULL;
	}

	noa_md_dma_mapper_release(&cldma->mapper);

	NOA_MD_CPATH_INFO("exit");
}
