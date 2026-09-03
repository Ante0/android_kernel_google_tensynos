/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * NOA Modem Data Path Controller
 *
 * This module orchestrates the dynamic data path switching process. It acts
 * as a state machine, taking high-level commands from the DPA driver and
 * coordinating with registered clients (like TX and RX) to ensure a seamless
 * transition without data loss.
 */

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include "common/md/mediatek/noa_md_shmem_layout.h"
#include "noa_md.h"
#include "noa_md_apc2ncp_ring.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_dpa.h"
#include "noa_md_dpmaif.h"
#include "noa_md_pcie.h"
#include "noa_md_shmem_sync.h"
#include "noa_md_trace.h"
#include "noa_md_wrapper.h"
#include "noa_md_wrapper_dpmaif.h"

/* DPA related header */
#include "soc/google/google_dpa_ctrl.h"  /* For enum dpa_action, dpa_data_path */

#define DPATH_CTRL_CLIENT_NAME "noa_md_dpath_ctrl"
/**
 * Timeout for waiting on client modules. Design doc P95 target is <200ms.
 * Using a larger value for initial functional stability.
 */
#define DPATH_CTRL_TIMEOUT_MS 5000

/* Forward declarations */
static void noa_md_dpath_ctrl_client_event_work_func(struct work_struct *work);
static void noa_md_dpath_ctrl_rollback(struct noa_md_dpath_ctrl *ctrl);

/**
 * noa_md_dpath_ctrl_pcie_notify() - Notifies the PCIe driver and handles failure.
 * @ctrl:  Pointer to the data path controller context, used for rollback.
 * @path:  The final path (DIRECT or OFFLOAD) for the switch.
 * @state: The current phase of the switch process.
 *
 * This helper function calls the underlying PCIe driver to notify it of a
 * state change. On failure, it automatically triggers a full rollback of
 * the entire switch process.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static inline int noa_md_dpath_ctrl_pcie_notify(
	struct noa_md_dpath_ctrl *ctrl,
	enum dpa_data_path path,
	enum dpath_switch_state state)
{
	int ret = noa_md_pcie_notify_switch(path, state);
	if (ret) {
		NOA_MD_ERROR("noa_md_pcie_notify_switch failed, ret=%d", ret);
		if (state != NOA_MD_DPATH_STATE_ROLLING_BACK) {
			noa_md_dpath_ctrl_rollback(ctrl);
		}
	}
	return ret;
}

/* REMOVED: This entire function is now replaced by noa_md_shmem_sync_send_switch_cmd */
/*
static int noa_md_dpath_ctrl_switch_send_cmd_and_wait(...)
{
    ...
}
*/

/**
 * noa_md_dpath_ctrl_switch_send_cmd_and_wait - Send a switch command and wait
 * for a reply.
 * @p_md_dev:  Pointer to the main NOA modem device struct.
 * @cmd:         The state command (from enum noa_md_switch_command) to send.
 * @target_path: The target path (from enum dpa_data_path) to send.
 * @ap_state:    Optional. Pointer to the AP state to send to the NCP. Can be
 * NULL.
 * @ncp_state:   Optional. Pointer to a struct to store the NCP's reply. Can be
 * NULL.
 *
 * This function sends a data path switch command to the NCP using shared
 * memory.
 * It uses a doorbell to notify the NCP and then waits for the NCP to signal
 * completion via another doorbell interrupt.
 * The whole process is protected by a lock to ensure only one command is
 * active at a time.
 *
 * Context: Process context. This function can sleep.
 * Return:
 * * %0: On success.
 * * %-ETIMEDOUT: If the NCP does not respond in time.
 * * %-EBUSY: If another switch command is already in progress.
 * * %-EIO: If the NCP reports a failure.
 */
static int noa_md_dpath_ctrl_switch_send_cmd_and_wait(
	struct noa_md_dev *p_md_dev,
	enum noa_md_switch_command cmd,
	enum dpa_data_path target_path,
	const struct dpath_ap_state_payload *ap_state,
	struct dpath_ncp_state_payload *ncp_state)
{
	struct noa_md_dpath_ctrl *ctrl = p_md_dev->dpath_ctrl;
	struct noa_md_shmem_handle *shared_mem = &p_md_dev->shmem_handle;
	struct noa_md_shmem_sync_handle *shared_mem_sync = p_md_dev->shmem_sync;
	struct noa_md_switch_payload *payload;
	long timeout = msecs_to_jiffies(ctrl->timeout_ms);

	/* Get the switch_payload pointer from the shared memory base */
	payload = &((struct noa_md_shmem_layout *)shared_mem->va_base)->switch_payload;

	/* 1. Acquire Lock */
	if (mutex_lock_interruptible(&ctrl->payload_lock)) {
		NOA_MD_ERROR("Failed to acquire lock");
		return -EBUSY;
	}

	reinit_completion(&shared_mem_sync->switch_ch.response_done);

	/* 2. Write Command, target_path and Payload */
	payload->command = (u32)cmd;

	switch (target_path) {
	case NOA_DATA_PATH_DIRECT:
	case NOA_DATA_PATH_OFFLOAD:
		payload->target_path = (u32)target_path;
		break;
	default:
		NOA_MD_ERROR("Invalid target_path: %d", target_path);
		mutex_unlock(&ctrl->payload_lock);
		return -EINVAL;
	}

	if (ap_state) {
		payload->ap_state = *ap_state;
	} else {
		memset(&payload->ap_state, 0, sizeof(payload->ap_state));
	}
	payload->status = SWITCH_STATUS_PENDING;

	/* 3. AP Flushes Memory */
	wmb();

	/* 4. AP Rings Doorbell */
	noa_md_dpa_notify_ncp(NOA_MD_APC2NCP_SWITCH_CTRL);

	/* 5. AP Waits for NCP to signal completion */
	if (wait_for_completion_timeout(
		&shared_mem_sync->switch_ch.response_done, timeout) == 0) {
		mutex_unlock(&ctrl->payload_lock);  /* Release lock on timeout */
		NOA_MD_ERROR("Wait for completion timeout");
		return -ETIMEDOUT;
	}

	/* 8. AP Checks the Result from NCP */
	if (payload->status != SWITCH_STATUS_SUCCESS) {
		mutex_unlock(&ctrl->payload_lock);
		NOA_MD_ERROR("Status: %d", payload->status);
		return -EIO;
	}

	/* Copy back response data if needed */
	if (ncp_state) {
		*ncp_state = payload->ncp_state;
	}

	/* 9. AP Releases the Lock */
	mutex_unlock(&ctrl->payload_lock);

	return 0;
}

/**
 * noa_md_dpath_ctrl_set_state() - Safely transitions the controller to a new
 * state.
 * @ctrl:	  Pointer to the data path controller context.
 * @new_state: The state to transition to.
 */
static void noa_md_dpath_ctrl_set_state(struct noa_md_dpath_ctrl *ctrl,
	enum dpath_switch_state new_state)
{
	mutex_lock(&ctrl->lock);
	if (ctrl->current_state != new_state) {
		NOA_MD_INFO("State: %s -> %s",
			noa_md_dpath_ctrl_state_to_str(ctrl->current_state),
			noa_md_dpath_ctrl_state_to_str(new_state));
		ctrl->current_state = new_state;
	}
	mutex_unlock(&ctrl->lock);
}

/**
 * noa_md_dpath_check_dpmaif_trans() - Check dpmaif trans status.
 * @p_md_dev:  Pointer to the main NOA modem device struct.
 * @target_path: The final target path (DIRECT or OFFLOAD) for the switch.
 *
 * If target path is OFFLOAD, dpmaif's trans should be enabled already.
 * If the trans was disabled in this scenario, the rollback procedure
 * should be triggered (rollback to DIRECT).
 * If target path is DIRECT, dpmaif's trans should be disabled already.
 * If the trans was enabled in this scenario, it should raise SW reset
 * flow.
 *
 * Return: 0 on success, or a negative error code if checking is failed
 */
static int noa_md_dpath_check_dpmaif_trans(
	struct noa_md_dev *p_md_dev,
	enum dpa_data_path target_path)
{
	bool trans_enabled = p_md_dev->dpmaif_dcb->trans_enabled;

	if (target_path == NOA_DATA_PATH_OFFLOAD && !trans_enabled) {
		NOA_MD_ERROR("DPMAIF Trans should be enabled for OFFLOAD path.");
		return -EBUSY;
	}

	// TODO: b/461933653 - DPMAIF Trans should be disabled for DIRECT path.
	// Both p_md_dev->dpmaif_dcb and p_md_dev->noa_dcb are currently synchronized.
	// Therefore, we temporarily skip the check that trans_enabled is false when
	// switching to direct mode.

	return 0;
}

/**
 * noa_md_dpath_ctrl_set_target_path() - Safely transitions the controller to a new
 * path.
 * @ctrl:	  Pointer to the data path controller context.
 * @new_target_path: The path to transition to.
 */
static void noa_md_dpath_ctrl_set_target_path(struct noa_md_dpath_ctrl *ctrl,
	enum dpa_data_path new_target_path)
{
	mutex_lock(&ctrl->lock);
	if (ctrl->target_path != new_target_path) {
		NOA_MD_INFO("Path: %s -> %s",
			noa_md_dpath_ctrl_target_path_to_str(ctrl->target_path),
			noa_md_dpath_ctrl_target_path_to_str(new_target_path));
		ctrl->target_path = new_target_path;
	}
	mutex_unlock(&ctrl->lock);
}

/**
 * noa_md_dpath_ctrl_wait_for_clients() - Waits for all clients to report
 * completion and verifies their individual status.
 * @ctrl: Pointer to the data path controller context.
 *
 * Return: 0 on success, -ETIMEDOUT on timeout, or -EIO if any client failed.
 */
static int noa_md_dpath_ctrl_wait_for_clients(struct noa_md_dpath_ctrl *ctrl)
{
	struct noa_dpath_client *client;
	int ret = 0;
	unsigned long timeout_jiffies;

	if (atomic_read(&ctrl->pending_clients) == 0)
		return 0;

	timeout_jiffies = msecs_to_jiffies(ctrl->timeout_ms);
	if (wait_for_completion_timeout(&ctrl->all_clients_done,
			timeout_jiffies) == 0) {
		NOA_MD_ERROR("Timeout waiting %d for clients in state: %s.",
			ctrl->timeout_ms,
			noa_md_dpath_ctrl_state_to_str(ctrl->current_state));

		mutex_lock(&ctrl->lock);
		/* Cancel any still-running work on timeout */
		list_for_each_entry (client, &ctrl->client_list, node) {
			cancel_work_sync(&client->work);
			if (atomic_read(&client->status) ==
				NOA_DPATH_CLIENT_STATE_PENDING) {
				NOA_MD_ERROR("Client %d is still pending.", client->type);
			}
		}
		mutex_unlock(&ctrl->lock);
		return -ETIMEDOUT;
	}

	mutex_lock(&ctrl->lock);
	list_for_each_entry (client, &ctrl->client_list, node) {
		if (atomic_read(&client->status) != NOA_DPATH_CLIENT_STATE_SUCCESS) {
			NOA_MD_ERROR("Client %d failed with state %d",
				client->type,
				atomic_read(&client->status));
			ret = -EIO;
		}
	}
	mutex_unlock(&ctrl->lock);

	return ret;
}

/**
 * noa_md_dpath_ctrl_notify_and_wait_for_clients() - Notifies all registered
 * clients of a state change and waits for them to complete.
 * @ctrl:      Pointer to the data path controller context.
 * @state:     The new state to notify clients about.
 * @ncp_state: Optional NCP state data for DEVICE_PREPARING.
 *
 * Return: 0 on success, -ETIMEDOUT on timeout, or -EIO if any client failed.
 */
static int noa_md_dpath_ctrl_notify_and_wait_for_clients(
	struct noa_md_dpath_ctrl *ctrl, enum dpath_switch_state state,
	const struct dpath_ncp_state_payload *ncp_state)
{
	struct noa_dpath_client *client;
	int count = 0;

	reinit_completion(&ctrl->all_clients_done);

	mutex_lock(&ctrl->lock);
	list_for_each_entry (client, &ctrl->client_list, node)
		count++;
	atomic_set(&ctrl->pending_clients, count);

	if (count == 0) {
		complete(&ctrl->all_clients_done);
		mutex_unlock(&ctrl->lock);
		return 0;
	}

	list_for_each_entry (client, &ctrl->client_list, node) {
		atomic_set(&client->status, NOA_DPATH_CLIENT_STATE_PENDING);
		client->ncp_state_cache = ncp_state; /* Cache for the worker */
		INIT_WORK(&client->work, noa_md_dpath_ctrl_client_event_work_func);
		queue_work(ctrl->event_wq, &client->work);
	}
	mutex_unlock(&ctrl->lock);
	return noa_md_dpath_ctrl_wait_for_clients(ctrl);
}

/**
 * noa_md_dpath_ctrl_rollback() - Reverts a failed switch attempt.
 * @ctrl: Pointer to the data path controller context.
 */
static void noa_md_dpath_ctrl_rollback(struct noa_md_dpath_ctrl *ctrl)
{
	const enum dpath_switch_state current_state =
		NOA_MD_DPATH_STATE_ROLLING_BACK;
	int ret;

	CHECK_PTR_OR_RETURN(ctrl);
	CHECK_PTR_OR_RETURN(ctrl->dev);
	CHECK_PTR_OR_RETURN(ctrl->dev->shmem_sync);

	NOA_MD_ERROR("Switch failed. Rolling back to %s, target path to %s",
		noa_md_dpath_ctrl_state_to_str(ctrl->old_state),
		noa_md_dpath_ctrl_target_path_to_str(ctrl->old_target_path));

	noa_md_dpath_ctrl_set_state(ctrl, current_state);

	/*
	 * Notify clients to perform their rollback procedures.
	 */
	noa_md_dpath_ctrl_notify_and_wait_for_clients(ctrl, current_state, NULL);

	ret = noa_md_shmem_sync_send_switch_cmd(
		ctrl->dev->shmem_sync, NOA_MD_SWITCH_CMD_ROLLBACK_SWITCH,
		ctrl->old_target_path, NULL, NULL);

	if (ret) {
		NOA_MD_ERROR("NCP rollback notification failed! ret=%d", ret);
		/* Continue rollback, AP state must revert regardless. */
	}

	ret = noa_md_dpath_ctrl_pcie_notify(
		ctrl, ctrl->old_target_path, current_state);
	if (ret) {
		/**
		 * Best-effort recovery: Even if notifying hardware/NCP fails,
		 * the AP's internal state machine must be reverted to the old
		 * stable state to prevent subsequent logic errors. The failure
		 * is logged for debugging.
		 */
	}

	noa_md_dpath_ctrl_set_state(ctrl, ctrl->old_state);
	noa_md_dpath_ctrl_set_target_path(ctrl, ctrl->old_target_path);
}

/**
 * noa_md_dpath_ctrl_handle_service_pre_switch() - Logic for the
 * SERVICE_PRE_SWITCH phase (Phase 1).
 * @ctrl: Pointer to the data path controller context.
 */
static void noa_md_dpath_ctrl_handle_service_pre_switch(
	struct noa_md_dpath_ctrl *ctrl)
{
	const enum dpath_switch_state current_state =
		NOA_MD_DPATH_STATE_SERVICE_STOPPING;
	struct noa_md_dev *p_md_dev;
	int ret;

	CHECK_PTR_OR_RETURN(ctrl);

	p_md_dev = ctrl->dev;

	CHECK_PTR_OR_GOTO_ERR(p_md_dev, err_rollback);
	CHECK_PTR_OR_GOTO_ERR(p_md_dev->dpmaif_dcb, err_rollback);

	/* 1. Set internal state */
	noa_md_dpath_ctrl_set_state(ctrl, current_state);

	/* 1-1. Check dpmaif trans state */
	ret = noa_md_dpath_check_dpmaif_trans(
		p_md_dev, ctrl->target_path);
	if (ret) {
		NOA_MD_ERROR("Phase 1: noa_md_dpath_check_dpmaif_trans failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 2. Notify PCIe Driver */
	ret = noa_md_dpath_ctrl_pcie_notify(
		ctrl, ctrl->target_path, current_state);
	if (ret) {
		NOA_MD_ERROR("Phase 1: noa_md_dpath_ctrl_pcie_notify failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 3. Send command to NCP */
	ret = noa_md_dpath_ctrl_switch_send_cmd_and_wait(
		p_md_dev, NOA_MD_SWITCH_CMD_NOTIFY_PREPARE_SWITCH,
		ctrl->target_path,
		NULL, NULL);
	if (ret) {
		NOA_MD_ERROR(
			"Phase 1: NCP NOTIFY_PREPARE_SWITCH failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 4. Notify Clients (TX/RX) */
	noa_md_dpath_ctrl_notify_and_wait_for_clients(ctrl, ctrl->current_state, NULL);

	return;

err_rollback:
	noa_md_dpath_ctrl_rollback(ctrl);
}

/**
 * noa_md_dpath_ctrl_handle_device_pre_switch() - Logic for the
 * DEVICE_PRE_SWITCH phase (Phase 2).
 * @ctrl: Pointer to the data path controller context.
 */
static void noa_md_dpath_ctrl_handle_device_pre_switch(
	struct noa_md_dpath_ctrl *ctrl)
{
	const enum dpath_switch_state current_state =
		NOA_MD_DPATH_STATE_DEVICE_PREPARING;
	int ret;
	int valid_devs_count = 0;
	struct dpath_ncp_state_payload ncp_state;
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct net_device *local_netdevs[MTK_NETDEV_MAX] = { NULL };
	struct noa_md_dev *p_md_dev;
	struct noa_md_dpmaif_ops* noa_dpmaif_ops;
	unsigned int duration;
	unsigned long flags;

	CHECK_PTR_OR_RETURN(ctrl);

	p_md_dev = ctrl->dev;

	CHECK_PTR_OR_GOTO_ERR(p_md_dev, err_rollback);

	noa_dpmaif_ops = noa_md_wpr_dpmaif_get_dpmaif_ops();
	CHECK_PTR_OR_GOTO_ERR(noa_dpmaif_ops, err_rollback);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_GOTO_ERR(noa_dcb, err_rollback);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_GOTO_ERR(dpmaif_dcb, err_rollback);

	/* 1. Set internal state */
	noa_md_dpath_ctrl_set_state(ctrl, current_state);

	/* 2. Stop Linux network queues */
	spin_lock_irqsave(&p_md_dev->netdev_update_lock, flags);
	for (int i = 0; i < MTK_NETDEV_MAX; i++) {
		struct net_device *netdev = p_md_dev->netdevs[i];
		/**
		 * TODO: b/434646935 - Add more checks like netif_running() and
		 * netif_carrier_ok() before operating on the netdev to ensure it is
		 * in a ready state for state changes.
		 */
		if (netdev && netdev->reg_state == NETREG_REGISTERED) {
			/* Increment netdev refcount to safely operate on it */
			dev_hold(netdev);
			local_netdevs[valid_devs_count] = netdev;
			valid_devs_count++;
		}
	}
	spin_unlock_irqrestore(&p_md_dev->netdev_update_lock, flags);

	for (int i = 0; i < valid_devs_count; i++) {
		struct net_device *netdev = local_netdevs[i];
		if (netdev) {
			netif_carrier_off(netdev);
			netif_tx_stop_all_queues(netdev);
			/* Decrease the netdev refcount */
			dev_put(netdev);
		} else {
			NOA_MD_ERROR("Phase 2: local_netdevs[%d] is null", i);
		}
	}

	/* 3. Stop DPMAIF/NOA queues */
	switch (ctrl->target_path) {
	case NOA_DATA_PATH_DIRECT:
		/* Call trans_disable to stop tx/rx processes */
		NOA_MD_INFO("Phase 2: trans_disable(noa_dcb)");
		ret = noa_dpmaif_ops->trans_disable(noa_dcb);
		if (ret) {
			NOA_MD_ERROR("Phase 2: trans_disable with noa_dcb failed, ret=%d", ret);
			goto err_rollback;
		}
		NOA_MD_INFO("Phase 2: flush_bat_reload(noa_dcb)");
		noa_dpmaif_ops->flush_bat_reload(noa_dcb);

		/* Sync status from noa to dpmaif */
		duration = NOA_MD_WPR_SYNC_NOA_TO_DPMAIF;
		noa_md_dpmaif_stats_sync((void *)&duration);
		break;

	case NOA_DATA_PATH_OFFLOAD:
		/* Disable LRO when switching to offload mode */
		noa_md_dpmaif_hw_lro_set(p_md_dev, false);

		/* Call trans_disable to stop tx/rx processes */
		ret = noa_dpmaif_ops->trans_disable(dpmaif_dcb);
		if (ret) {
			NOA_MD_ERROR("Phase 2: trans_disable with dpmaif_dcb failed, ret=%d", ret);
			goto err_rollback;
		}
		/* Stop rx and flush bat reload */
		noa_dpmaif_ops->sw_stop_rx(dpmaif_dcb);
		noa_dpmaif_ops->flush_bat_reload(dpmaif_dcb);
		/* Sync status from dpmaif to noa */
		duration = NOA_MD_WPR_SYNC_DPMAIF_TO_NOA;
		noa_md_dpmaif_stats_sync((void *)&duration);
		NOA_MD_INFO("Phase 2: Setup apc2ncp rings in APC side");
		ret = noa_md_apc2ncp_ring_apc_setup(p_md_dev);
		if (ret) {
			NOA_MD_ERROR("Phase 2: noa_md_apc2ncp_ring_apc_setup=%d", ret);
			noa_md_apc2ncp_ring_apc_release();
			goto err_rollback;
		}
		break;

	default:
		/* This should not happen */
		NOA_MD_ERROR("Phase 2: Unknown target path: %d", ctrl->target_path);
		goto err_rollback;
	}

	/* 4. Notify PCIe Driver (Handles MSI disabling if Direct->Offload) */
	ret = noa_md_dpath_ctrl_pcie_notify(
		ctrl, ctrl->target_path, current_state);

	if (ret) {
		NOA_MD_ERROR(
			"Phase 2: noa_md_dpath_ctrl_pcie_notify failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 5. Send EXCHANGE_STATE command to NCP */
	ret = noa_md_dpath_ctrl_switch_send_cmd_and_wait(
		p_md_dev, NOA_MD_SWITCH_CMD_EXCHANGE_STATE,
		ctrl->target_path,
		&ctrl->ap_state, &ncp_state);
	if (ret) {
		NOA_MD_ERROR("Phase 2: NCP EXCHANGE_STATE failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 6 & 7. Notify Clients (TX/RX) with NCP state */
	noa_md_dpath_ctrl_notify_and_wait_for_clients(ctrl, ctrl->current_state, &ncp_state);

	return;

err_rollback:
	noa_md_dpath_ctrl_rollback(ctrl);
}

/**
 * noa_md_dpath_ctrl_handle_device_post_switch() - Logic for the
 * DEVICE_POST_SWITCH phase (Phase 3).
 * @ctrl: Pointer to the data path controller context.
 */
static void noa_md_dpath_ctrl_handle_device_post_switch(
	struct noa_md_dpath_ctrl *ctrl)
{
	const enum dpath_switch_state current_state =
		NOA_MD_DPATH_STATE_DEVICE_RESUMING;
	enum noa_md_wpr_sync_duration sync_dir;
	int ret;
	int valid_devs_count = 0;
	struct dpath_ncp_state_payload ncp_state;
	struct noa_md_dev *p_md_dev;
	struct noa_md_dpmaif_ops* noa_dpmaif_ops;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct net_device *local_netdevs[MTK_NETDEV_MAX] = { NULL };
	unsigned long flags;

	CHECK_PTR_OR_RETURN(ctrl);

	p_md_dev = ctrl->dev;
	CHECK_PTR_OR_GOTO_ERR(p_md_dev, err_rollback);

	noa_dpmaif_ops = noa_md_wpr_dpmaif_get_dpmaif_ops();
	CHECK_PTR_OR_GOTO_ERR(noa_dpmaif_ops, err_rollback);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_GOTO_ERR(noa_dcb, err_rollback);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_GOTO_ERR(dpmaif_dcb, err_rollback);

	/* 1. Set internal state */
	noa_md_dpath_ctrl_set_state(ctrl, current_state);

	/* 2. Send COMMIT_SWITCH command to NCP */
	ret = noa_md_dpath_ctrl_switch_send_cmd_and_wait(
		p_md_dev, NOA_MD_SWITCH_CMD_COMMIT_SWITCH,
		ctrl->target_path,
		&ctrl->ap_state, &ncp_state);
	if (ret) {
		NOA_MD_ERROR("Phase 3: NCP COMMIT_SWITCH failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 3 & 4. Notify Clients (TX/RX) with NCP state to enable hardware functions */
	if (ctrl->target_path == NOA_DATA_PATH_DIRECT) {
		struct noa_md_shmem_handle *shared_mem;
		struct noa_md_shmem_layout *shmem_layout;
		struct noa_md_switch_payload *payload;

		shared_mem = &p_md_dev->shmem_handle;
		CHECK_PTR_OR_GOTO_ERR(shared_mem, err_rollback);

		shmem_layout = (struct noa_md_shmem_layout *)shared_mem->va_base;
		CHECK_PTR_OR_GOTO_ERR(shmem_layout, err_rollback);

		payload = &shmem_layout->switch_payload;
		CHECK_PTR_OR_GOTO_ERR(payload, err_rollback);

		dma_rmb();
		noa_md_dpath_ctrl_notify_and_wait_for_clients(ctrl, ctrl->current_state, &payload->ncp_state);
	} else {
		noa_md_dpath_ctrl_notify_and_wait_for_clients(ctrl, ctrl->current_state, NULL);
	}

	/* 5. Final Setup & State Synchronization */
	switch (ctrl->target_path) {
	case NOA_DATA_PATH_OFFLOAD:
		/* 5.a.i. Enable Offload Flag */
		p_md_dev->feature_ctrl.enabled = true;

		/* 5.a.ii. Call trans_enable to start tx/rx processes */
		ret = noa_dpmaif_ops->trans_enable(noa_dcb);
		if (ret) {
			NOA_MD_ERROR("Phase 3: trans_enable(noa_dcb) failed, ret=%d", ret);
			goto err_rollback;
		}

		/* 5.a.iii. Sync status from noa to dpmaif */
		sync_dir = NOA_MD_WPR_SYNC_NOA_TO_DPMAIF;
		noa_md_dpmaif_stats_sync((void *)&sync_dir);
		break;

	case NOA_DATA_PATH_DIRECT:
		/* 5.b.i. Stop NOA RX */
		noa_dpmaif_ops->sw_stop_rx(noa_dcb);

		/* 5.b.ii. Release apc2ncp rings in AP side */
		noa_md_apc2ncp_ring_apc_release();

		/* 5.b.iii. Disable Offload Flag */
		p_md_dev->feature_ctrl.enabled = false;

		/* 5.b.iv. Call trans_enable to start tx/rx processes */
		ret = noa_dpmaif_ops->trans_enable(dpmaif_dcb);
		if (ret) {
			NOA_MD_ERROR("Phase 3: trans_enable(dpmaif_dcb) failed, ret=%d", ret);
			goto err_rollback;
		}

		/* 5.b.v. Sync status from dpmaif to noa */
		sync_dir = NOA_MD_WPR_SYNC_DPMAIF_TO_NOA;
		noa_md_dpmaif_stats_sync((void *)&sync_dir);

		/* Enable LRO when switching to direct mode */
		noa_md_dpmaif_hw_lro_set(p_md_dev, true);
		break;

	default:
		/* This should not happen */
		NOA_MD_ERROR("Phase 3: Unknown target path: %d", ctrl->target_path);
		goto err_rollback;
	}

	/* 6. Notify PCIe Driver (Handles MSI enabling if Offload->Direct) */
	ret = noa_md_dpath_ctrl_pcie_notify(
		ctrl, ctrl->target_path, current_state);

	if (ret) {
		NOA_MD_ERROR(
			"Phase 3: noa_md_dpath_ctrl_pcie_notify failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 7. Resume Linux network queues */
	spin_lock_irqsave(&p_md_dev->netdev_update_lock, flags);
	for (int i = 0; i < MTK_NETDEV_MAX; i++) {
		struct net_device *netdev = p_md_dev->netdevs[i];
		/**
		 * TODO: b/434646935 - Add more checks like netif_running() and
		 * netif_carrier_ok() before operating on the netdev to ensure it is in
		 * a ready state for state changes.
		 */
		if (netdev && netdev->reg_state == NETREG_REGISTERED) {
			/* Increment netdev refcount to safely operate on it */
			dev_hold(netdev);
			local_netdevs[valid_devs_count] = netdev;
			valid_devs_count++;
		}
	}
	spin_unlock_irqrestore(&p_md_dev->netdev_update_lock, flags);

	for (int i = 0; i < valid_devs_count; i++) {
		struct net_device *netdev = local_netdevs[i];
		if (netdev) {
			netif_tx_start_all_queues(netdev);
			netif_carrier_on(netdev);
			/* Decrease the netdev refcount */
			dev_put(netdev);
		} else {
			NOA_MD_ERROR("Phase 3: local_netdevs[%d] is null", i);
		}
	}

	return;

err_rollback:
	noa_md_dpath_ctrl_rollback(ctrl);
}

/**
 * noa_md_dpath_ctrl_handle_service_post_switch() - Logic for the
 * SERVICE_POST_SWITCH phase (Phase 4).
 * @ctrl: Pointer to the data path controller context.
 */
static void noa_md_dpath_ctrl_handle_service_post_switch(
	struct noa_md_dpath_ctrl *ctrl)
{
	const enum dpath_switch_state current_state =
		NOA_MD_DPATH_STATE_SERVICE_RESTARTING;
	struct noa_md_dev *p_md_dev;
	int ret;

	CHECK_PTR_OR_RETURN(ctrl);

	p_md_dev = ctrl->dev;
	CHECK_PTR_OR_GOTO_ERR(p_md_dev, err_rollback);

	/* 1. Set internal state */
	noa_md_dpath_ctrl_set_state(ctrl, current_state);

	/* 2. Notify PCIe Driver */
	ret = noa_md_dpath_ctrl_pcie_notify(
		ctrl, ctrl->target_path, current_state);
	if (ret) {
		NOA_MD_ERROR(
			"Phase 4: noa_md_dpath_ctrl_pcie_notify failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 3. Send NOTIFY_RESTART command to NCP */
	ret = noa_md_dpath_ctrl_switch_send_cmd_and_wait(
		p_md_dev, NOA_MD_SWITCH_CMD_NOTIFY_RESTART,
		ctrl->target_path,
		NULL, NULL);
	if (ret) {
		NOA_MD_ERROR("Phase 4: NCP NOTIFY_RESTART failed, ret: %d", ret);
		goto err_rollback;
	}

	/* 4 & 5. Notify Clients (TX/RX) */
	noa_md_dpath_ctrl_notify_and_wait_for_clients(ctrl, ctrl->current_state, NULL);

	/* 6. Transition to final stable state */
	if (ctrl->target_path == NOA_DATA_PATH_DIRECT)
		noa_md_dpath_ctrl_set_state(ctrl, NOA_MD_DPATH_STATE_IDLE_DIRECT);
	else
		noa_md_dpath_ctrl_set_state(ctrl, NOA_MD_DPATH_STATE_IDLE_OFFLOAD);

	return;

err_rollback:
	noa_md_dpath_ctrl_rollback(ctrl);
}

/**
 * noa_md_dpath_ctrl_client_event_work_func() - Worker function to call client
 * callbacks.
 * @work: The work_struct associated with a specific client.
 */
static void noa_md_dpath_ctrl_client_event_work_func(struct work_struct *work)
{
	struct noa_dpath_client *client = container_of(
		work, struct noa_dpath_client, work);
	struct noa_md_dpath_ctrl *ctrl = client->ctrl;

	CHECK_PTR_OR_RETURN(ctrl);

	if (client->ops && client->ops->on_state_change) {
		client->ops->on_state_change(
			client,
			ctrl->current_state,
			ctrl->target_path,
			client->ncp_state_cache);
	} else {
		/* If client has no handler, report success immediately */
		noa_md_dpath_ctrl_report_completion(client, true);
	}
}

/**
 * noa_md_dpath_ctrl_on_dpa_state_change() - Handles modem lifecycle state
 * changes.
 * @state:   The new DPA state (READY, CRASH, etc.).
 * @context: Private context.
 */
static void noa_md_dpath_ctrl_on_dpa_state_change(enum dpa_state state,
	void *context)
{
	struct noa_md_dpath_ctrl *ctrl = (struct noa_md_dpath_ctrl *)context;

	CHECK_PTR_OR_RETURN(ctrl);

	NOA_MD_INFO("DPA State Changed:%d, current:%d, old:%d ",
		state, ctrl->current_state, ctrl->old_state);

	switch (state) {
	case NOA_STATE_READY:
		// TODO: b/434646935 - Handle the DPA state
		// Discuss the appropriate action for DPA state changes.
		// Directly changing the path here might not be safe.
		// For now, we only update our internal state.
		// noa_md_dpath_ctrl_set_state(ctrl, NOA_MD_DPATH_STATE_IDLE_DIRECT);
		break;
	case NOA_STATE_UNAVAILABLE:
	case NOA_STATE_CRASH:
		// TODO: b/434646935 - Handle the DPA state
		// Discuss the appropriate action for DPA state changes.
		// noa_md_dpath_ctrl_set_state(ctrl, NOA_MD_DPATH_STATE_IDLE_DIRECT);
		break;
	default:
		break;
	}
}

/**
 * noa_md_dpath_ctrl_on_dpa_action() - Callback handler for actions from DPA
 * driver.
 * @path:    The target data path for the switch.
 * @action:  The action requested by the DPA driver.
 * @context: Private context, points to our controller struct.
 */
static void noa_md_dpath_ctrl_on_dpa_action(enum dpa_data_path path,
	enum dpa_action action, void *context)
{
	struct noa_md_dpath_ctrl *ctrl = (struct noa_md_dpath_ctrl *)context;

	CHECK_PTR_OR_RETURN(ctrl);

	if (!noa_md_debug_ctl_is_dynamic_switch_enabled(ctrl->dev)) {
		NOA_MD_INFO("Dynamic data path switch is disabled via debugfs, "
			"ignoring action %d", action);
		return;
	}

	NOA_MD_INFO("DPA Action: %d, Target Path: %d", action, path);

	mutex_lock(&ctrl->lock);
	if (action == NOA_ACTION_SERVICE_PRE_SWITCH) {
		if (ctrl->current_state != NOA_MD_DPATH_STATE_IDLE_DIRECT &&
			ctrl->current_state != NOA_MD_DPATH_STATE_IDLE_OFFLOAD) {
			mutex_unlock(&ctrl->lock);
			NOA_MD_ERROR(
				"Switch requested while another is in progress (state: %s)",
				noa_md_dpath_ctrl_state_to_str(ctrl->current_state));
			/* Optionally, report error back to DPA driver. */
			return;
		}
		/* Record the state *before* the switch begins */
		ctrl->old_state = ctrl->current_state;
		ctrl->target_path = path;
		if (ctrl->old_state == NOA_MD_DPATH_STATE_IDLE_DIRECT) {
			ctrl->old_target_path = NOA_DATA_PATH_DIRECT;
		} else {
			ctrl->old_target_path = NOA_DATA_PATH_OFFLOAD;
		}

	} else {
		/* For subsequent actions, verify they match the in-progress target */
		if (ctrl->target_path != path) {
			 mutex_unlock(&ctrl->lock);
			 NOA_MD_ERROR("DPA action %d target mismatch! expected %d got %d",
				 action, ctrl->target_path, path);
			 /* Don't know what to do, just return. */
			 return;
		}
	}
	mutex_unlock(&ctrl->lock);

	switch (action) {
	case NOA_ACTION_SERVICE_PRE_SWITCH:
		noa_md_dpath_ctrl_handle_service_pre_switch(ctrl);
		break;
	case NOA_ACTION_DEVICE_PRE_SWITCH:
		noa_md_dpath_ctrl_handle_device_pre_switch(ctrl);
		break;
	case NOA_ACTION_DEVICE_POST_SWITCH:
		noa_md_dpath_ctrl_handle_device_post_switch(ctrl);
		break;
	case NOA_ACTION_SERVICE_POST_SWITCH:
		noa_md_dpath_ctrl_handle_service_post_switch(ctrl);
		break;
	default:
		NOA_MD_ERROR("Unknown DPA action: %d", action);
	}
}

/* Callbacks to be registered with the DPA controller */
static struct dpa_callbacks dpa_event_callbacks = {
	.on_state_changed = noa_md_dpath_ctrl_on_dpa_state_change,
	.on_data_path_changed = noa_md_dpath_ctrl_on_dpa_action,
};

const char *noa_md_dpath_ctrl_state_to_str(enum dpath_switch_state state)
{
	switch (state) {
	case NOA_MD_DPATH_STATE_IDLE_DIRECT:
		return "IDLE_DIRECT";
	case NOA_MD_DPATH_STATE_IDLE_OFFLOAD:
		return "IDLE_OFFLOAD";
	case NOA_MD_DPATH_STATE_SERVICE_STOPPING:
		return "SERVICE_STOPPING";
	case NOA_MD_DPATH_STATE_DEVICE_PREPARING:
		return "DEVICE_PREPARING";
	case NOA_MD_DPATH_STATE_DEVICE_RESUMING:
		return "DEVICE_RESUMING";
	case NOA_MD_DPATH_STATE_SERVICE_RESTARTING:
		return "SERVICE_RESTARTING";
	case NOA_MD_DPATH_STATE_FAILED:
		return "FAILED";
	case NOA_MD_DPATH_STATE_ROLLING_BACK:
		return "ROLLING_BACK";
	default:
		return "UNKNOWN";
	}
}

const char *noa_md_dpath_ctrl_target_path_to_str(enum dpa_data_path data_path)
{
	switch (data_path) {
	case NOA_DATA_PATH_DIRECT:
		return "NOA_DATA_PATH_DIRECT";
	case NOA_DATA_PATH_OFFLOAD:
		return "NOA_DATA_PATH_OFFLOAD";
	default:
		return "UNKNOWN";
	}
}

struct noa_dpath_client *noa_md_dpath_ctrl_register_client(
	struct noa_md_dev *p_md_dev,
	enum noa_dpath_client_type type,
	const struct noa_dpath_client_ops *ops)
{
	struct noa_dpath_client *client;
	struct noa_md_dpath_ctrl *ctrl = p_md_dev ? p_md_dev->dpath_ctrl : NULL;

	CHECK_PTR_OR_RETURN_ERR(ctrl, ERR_PTR(-EINVAL));
	CHECK_PTR_OR_RETURN_ERR(ops, ERR_PTR(-EINVAL));
	CHECK_PTR_OR_RETURN_ERR(ops->on_state_change, ERR_PTR(-EINVAL));


	client = kzalloc(sizeof(*client), GFP_KERNEL);

	CHECK_PTR_OR_RETURN_ERR(client, ERR_PTR(-ENOMEM));

	client->type = type;
	client->ops = ops;
	/* Initial state is SUCCESS */
	atomic_set(&client->status, NOA_DPATH_CLIENT_STATE_SUCCESS);
	client->ctrl = ctrl;

	mutex_lock(&ctrl->lock);
	list_add_tail(&client->node, &ctrl->client_list);
	mutex_unlock(&ctrl->lock);

	return client;
}

void noa_md_dpath_ctrl_unregister_client(
	struct noa_md_dev *p_md_dev,
	struct noa_dpath_client *client)
{
	struct noa_md_dpath_ctrl *ctrl = p_md_dev ? p_md_dev->dpath_ctrl : NULL;

	CHECK_PTR_OR_RETURN(client);
	CHECK_PTR_OR_RETURN(ctrl);

	/* Ensure client->ctrl matches the passed ctrl context for safety */
	if (client->ctrl != ctrl) {
		 /* This should never happen if API is used correctly */
		 NOA_MD_ERROR("Unregister client with mismatched controller context.");
	}

	/* Ensure any pending work is cancelled before freeing */
	cancel_work_sync(&client->work);

	mutex_lock(&ctrl->lock);
	list_del(&client->node);
	mutex_unlock(&ctrl->lock);

	kfree(client);
}

void noa_md_dpath_ctrl_report_completion(struct noa_dpath_client *client,
	bool success)
{
	struct noa_md_dpath_ctrl *ctrl = client ? client->ctrl : NULL;

	CHECK_PTR_OR_RETURN(ctrl);

	atomic_set(&client->status, success ?
		NOA_DPATH_CLIENT_STATE_SUCCESS : NOA_DPATH_CLIENT_STATE_FAILED);

	if (atomic_dec_and_test(&ctrl->pending_clients))
		complete(&ctrl->all_clients_done);
}

int noa_md_dpath_ctrl_init(struct noa_md_dev *p_md_dev)
{
	struct noa_md_dpath_ctrl *ctrl;

	NOA_MD_INFO("enter");
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	CHECK_PTR_OR_RETURN_ERR(ctrl, -ENOMEM);

	ctrl->dev = p_md_dev;
	mutex_init(&ctrl->lock);
	INIT_LIST_HEAD(&ctrl->client_list);
	atomic_set(&ctrl->pending_clients, 0);

	ctrl->current_state = NOA_MD_DPATH_STATE_IDLE_DIRECT;
	init_completion(&ctrl->all_clients_done);

	ctrl->event_wq = alloc_workqueue("noa_dpath_ctrl_wq", WQ_UNBOUND, 0);
	if (!ctrl->event_wq) {
		NOA_MD_ERROR("Failed to create workqueue");
		mutex_destroy(&ctrl->lock);
		kfree(ctrl);
		return -ENOMEM;
	}

	ctrl->timeout_ms = DPATH_CTRL_TIMEOUT_MS;

	/* Register our callbacks with the DPA controller */
	ctrl->dpa_client =
		google_dpa_ctrl_register(
			DPATH_CTRL_CLIENT_NAME, &dpa_event_callbacks, ctrl);

	if (!ctrl->dpa_client) {
		NOA_MD_ERROR("Failed to register with DPA ctrl");
		destroy_workqueue(ctrl->event_wq);
		mutex_destroy(&ctrl->lock);
		kfree(ctrl);
		return -ENODEV;
	}

	p_md_dev->dpath_ctrl = ctrl;
	return 0;
}

void noa_md_dpath_ctrl_exit(struct noa_md_dev *p_md_dev)
{
	struct noa_md_dpath_ctrl *ctrl;

	NOA_MD_INFO("enter");

	CHECK_PTR_OR_RETURN(p_md_dev);

	ctrl = p_md_dev->dpath_ctrl;
	CHECK_PTR_OR_RETURN(ctrl);

	if (ctrl->dpa_client) {
		google_dpa_ctrl_unregister(ctrl->dpa_client);
		ctrl->dpa_client = NULL;
	}

	if (ctrl->event_wq) {
		destroy_workqueue(ctrl->event_wq);
		ctrl->event_wq = NULL;
	}

	/* Ensure no clients are left registered at exit */
	WARN_ON(!list_empty(&ctrl->client_list));

	mutex_destroy(&ctrl->lock);

	kfree(ctrl);
	p_md_dev->dpath_ctrl = NULL;
}
