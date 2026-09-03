/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * NOA Modem Shared Memory Synchronization Module
 *
 * This module encapsulates the synchronization logic for AP-NCP communication
 * that uses a doorbell and shared memory protocol. It provides two distinct
 * synchronization mechanisms:
 *
 * 1. Critical Path (switch_cmd): A single-payload, lock-wait-unlock sequence
 *    for time-sensitive data path switching commands.
 *
 * 2. Generic Path (data/debug cmd): A dual-payload, notify-wait-ack handshake
 *    for general-purpose control and debug commands.
 */

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "noa_md.h"
#include "noa_md_dpa.h"
#include "noa_md_shmem_sync.h"
#include "noa_md_trace.h"
#include "common/md/mediatek/noa_md_dpa_doorbell.h"
#include "common/md/mediatek/noa_md_shmem_layout.h"


/* Use a 200 ms timeout for the critical switch path. */
#define SWITCH_CHANNEL_TIMEOUT_MS 200

/* Use a 500 ms timeout for generic data/debug commands */
#define DATA_CHANNEL_TIMEOUT_MS 5000
#define DEBUG_CHANNEL_TIMEOUT_MS 5000

/* --- Internal Helper Functions --- */

/**
 * noa_md_shmem_sync_switch_isr() - ISR for the Critical (Switch) Path.
 */
static void noa_md_shmem_sync_switch_isr(int id, void *resource)
{
	struct noa_md_shmem_sync_handle *handle =
		(struct noa_md_shmem_sync_handle *)resource;
	if (handle) {
		complete(&handle->switch_ch.response_done);
	}
}

/**
 * noa_md_shmem_sync_data_isr() - ISR for the Generic (Data) Path.
 */
static void noa_md_shmem_sync_data_isr(int id, void *resource)
{
	struct noa_md_shmem_sync_handle *handle =
		(struct noa_md_shmem_sync_handle *)resource;
	if (handle) {
		complete(&handle->data_ch.response_done);
	}
}

/**
 * noa_md_shmem_sync_debug_isr() - ISR for the Generic (Debug) Path.
 */
static void noa_md_shmem_sync_debug_isr(int id, void *resource)
{
	struct noa_md_shmem_sync_handle *handle =
		(struct noa_md_shmem_sync_handle *)resource;
	if (handle) {
		complete(&handle->debug_ch.response_done);
	}
}

/**
 * __noa_md_shmem_sync_obj_init() - Initializes a single synchronization
 * object.
 * @sync_obj:   The sync object to initialize.
 * @name:       A name for the channel, used for logging.
 * @parent_dev: Pointer to the parent noa_md_dev for resource access.
 * @timeout_ms: The timeout value for the channel
 */
static void __noa_md_shmem_sync_obj_init(
	struct noa_md_shmem_sync_obj *sync_obj,
	const char *name,
	const struct noa_md_dev *parent_dev,
	long timeout_ms
)
{
	sync_obj->channel_name = name;
	sync_obj->parent_dev = parent_dev;
	sync_obj->timeout_jiffies = msecs_to_jiffies(timeout_ms);
	mutex_init(&sync_obj->payload_lock);
	init_completion(&sync_obj->response_done);
}

/**
 * __noa_md_shmem_sync_obj_exit() - Deinitializes a single synchronization
 * object.
 * @sync_obj: The sync object to deinitialize.
 */
static void __noa_md_shmem_sync_obj_exit(
	struct noa_md_shmem_sync_obj *sync_obj)
{
	mutex_destroy(&sync_obj->payload_lock);
}

/**
 * __noa_md_shmem_sync_send_and_wait() - Core engine for Critical(Switch) Path.
 *
 * This function executes the full atomic "lock-write-ring-wait-unlock"
 * sequence.
 *
 * @sync_obj:    The sync object for the channel.
 * @payload:     Pointer to the switch_payload structure in shared memory.
 * @cmd:         The switch command to send.
 * @target_path: The final data path destination.
 * @ap_state:    (Optional) Pointer to the AP state payload to write.
 * @ncp_state:   (Optional) Pointer to store the NCP's state response.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int __noa_md_shmem_sync_send_and_wait(
	struct noa_md_shmem_sync_obj *sync_obj,
	struct noa_md_switch_payload *payload,
	enum noa_md_switch_command cmd,
	enum dpa_data_path target_path,
	const struct dpath_ap_state_payload *ap_state,
	struct dpath_ncp_state_payload *ncp_state)
{
	/* Step 1: Acquire Lock to protect the shared payload */
	if (mutex_lock_interruptible(&sync_obj->payload_lock)) {
		NOA_MD_ERROR("%s: Failed to acquire lock", sync_obj->channel_name);
		return -EBUSY;
	}

	/* Step 2: Prepare the payload *inside* the lock */
	payload->command = (u32)cmd;
	payload->target_path = (u32)target_path;
	if (ap_state)
		memcpy(&payload->ap_state, ap_state, sizeof(payload->ap_state));
	else
		memset(&payload->ap_state, 0, sizeof(payload->ap_state));

	reinit_completion(&sync_obj->response_done);
	payload->status = SWITCH_STATUS_PENDING;

	/* Step 3: Memory Barrier to ensure payload is visible before doorbell */
	dma_wmb();

	/* Step 4: Ring the doorbell to notify NCP */
	noa_md_dpa_notify_ncp(NOA_MD_APC2NCP_SWITCH_CTRL);

	/* Step 5: Wait for NCP's response */
	if (wait_for_completion_timeout(&sync_obj->response_done,
			sync_obj->timeout_jiffies) == 0) {
		mutex_unlock(&sync_obj->payload_lock); /* Release lock on timeout */
		NOA_MD_ERROR(
			"%s: Wait for NCP response timeout", sync_obj->channel_name);
		return -ETIMEDOUT;
	}

	/* Step 6: Read Memory Barrier before checking status */
	dma_rmb();

	/* Step 7: Check the result from NCP */
	if (payload->status != SWITCH_STATUS_SUCCESS) {
		NOA_MD_ERROR("%s: NCP reported failure, status: %u",
			sync_obj->channel_name, payload->status);
		mutex_unlock(&sync_obj->payload_lock);
		return -EIO;
	}

	/* Step 8: Process successful response (copy out NCP state) */
	if (ncp_state)
		memcpy(ncp_state, &payload->ncp_state, sizeof(*ncp_state));

	/* Step 9: Release Lock on success */
	mutex_unlock(&sync_obj->payload_lock);

	return 0;
}

/**
 * __noa_md_shmem_sync_send_generic_cmd() - Refactored engine for Data/Debug
 * paths.
 *
 * This function consolidates the logic for sending a generic command, reducing
 * code duplication. It selects the correct channel resources and prepares
 * the payload before calling the core handshake engine.
 *
 * @handle:          The main sync handle.
 * @channel_type:    The type of channel (DATA or DEBUG) to use.
 * @sub_cmd:         The specific sub-command to be sent.
 * @req_payload_in:  (Optional) Input data for the request's payload.
 * @resp_payload_out:(Optional) Pointer to store the output from the response
 * payload.
 * @payload_size:    The size of the specific payload struct (data or debug).
 *
 * Return: 0 on success, negative error code on failure.
 */
static int __noa_md_shmem_sync_send_generic_cmd(
	struct noa_md_shmem_sync_handle *handle,
	enum noa_md_shmem_channel_type channel_type,
	u32 sub_cmd,
	const void *req_payload_in,
	void *resp_payload_out,
	size_t payload_size)
{
	struct noa_md_shmem_layout *shmem_layout;
	struct noa_md_shmem_cmd_payload *req_payload, *resp_payload;
	struct noa_md_shmem_sync_obj *sync_obj;
	u32 notify_db_id;
	void *req_data_ptr, *resp_data_ptr;

	CHECK_PTR_OR_RETURN_ERR(handle, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(handle->parent_dev, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(handle->parent_dev->shmem_handle.va_base, -ENXIO);
	CHECK_PTR_OR_RETURN_ERR(handle->parent_dev->dpa_res, -ENODEV);

	if (handle->parent_dev->dpa_res->state != NOA_STATE_READY) {
		NOA_MD_ERROR("DPA not ready (state: %d)",
			handle->parent_dev->dpa_res->state);
		return -EAGAIN;
	}

	shmem_layout =
		(struct noa_md_shmem_layout *)handle->parent_dev->shmem_handle.va_base;

	/* Step 1: Select channel resources based on type */
	switch (channel_type) {
	case GENERIC_DATA_CHANNEL:
		sync_obj = &handle->data_ch;
		req_payload = &shmem_layout->ap2ncp_data_payload;
		resp_payload = &shmem_layout->ncp2ap_data_payload;
		notify_db_id = NOA_MD_APC2NCP_SHMEM_DATA_NOTIFY;
		req_data_ptr = &req_payload->payload.data_payload;
		resp_data_ptr = &resp_payload->payload.data_payload;
		break;
	case GENERIC_DEBUG_CHANNEL:
		sync_obj = &handle->debug_ch;
		req_payload = &shmem_layout->ap2ncp_debug_payload;
		resp_payload = &shmem_layout->ncp2ap_debug_payload;
		notify_db_id = NOA_MD_APC2NCP_SHMEM_DEBUG_NOTIFY;
		req_data_ptr = &req_payload->payload.debug_payload;
		resp_data_ptr = &resp_payload->payload.debug_payload;
		break;
	default:
		NOA_MD_ERROR("Invalid generic channel type: %d", channel_type);
		return -EINVAL;
	}

	/* Step 2: Lock the REQUEST payload to ensure atomicity */
	if (mutex_lock_interruptible(&sync_obj->payload_lock)) {
		NOA_MD_ERROR("%s: Failed to acquire lock", sync_obj->channel_name);
		return -EBUSY;
	}

	/* Step 3: Prepare request payload *inside* the lock */
	if (req_payload_in)
		memcpy(req_data_ptr, req_payload_in, payload_size);
	else
		memset(req_data_ptr, 0, payload_size);

	if (channel_type == GENERIC_DATA_CHANNEL)
		req_payload->payload.data_payload.sub_cmd = sub_cmd;
	else
		req_payload->payload.debug_payload.sub_cmd = sub_cmd;

	reinit_completion(&sync_obj->response_done);
	req_payload->status = SHMEM_CMD_STATUS_PENDING;

	/* Step 4: Memory Barrier */
	dma_wmb();

	/* Step 5: Ring the _NOTIFY doorbell */
	noa_md_dpa_notify_ncp(notify_db_id);

	/* Step 6: Wait for NCP's _ACK response */
	if (wait_for_completion_timeout(&sync_obj->response_done,
			sync_obj->timeout_jiffies) == 0) {
		mutex_unlock(&sync_obj->payload_lock);
		NOA_MD_ERROR("%s: Wait for NCP ACK timeout", sync_obj->channel_name);
		return -ETIMEDOUT;
	}

	/* Step 7: Read Memory Barrier before reading response status */
	dma_rmb();

	/* Step 8: Check the result in the RESPONSE payload */
	if (resp_payload->status != SHMEM_CMD_STATUS_SUCCESS) {
		NOA_MD_ERROR("%s: NCP reported failure in response, status: %u",
			sync_obj->channel_name, resp_payload->status);
		mutex_unlock(&sync_obj->payload_lock);
		return -EIO;
	}

	/* Step 9: Process successful response */
	if (resp_payload_out)
		memcpy(resp_payload_out, resp_data_ptr, payload_size);

	/* Step 10: Release Lock on success */
	mutex_unlock(&sync_obj->payload_lock);

	return 0;
}

/* --- Public API Functions --- */

int noa_md_shmem_sync_send_switch_cmd(
	struct noa_md_shmem_sync_handle *handle,
	enum noa_md_switch_command cmd,
	enum dpa_data_path target_path,
	const struct dpath_ap_state_payload *ap_state,
	struct dpath_ncp_state_payload *ncp_state)
{
	struct noa_md_shmem_layout *shmem_layout;
	int ret;

	CHECK_PTR_OR_RETURN_ERR(handle, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(handle->parent_dev, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(handle->parent_dev->shmem_handle.va_base, -ENXIO);

	shmem_layout =
		(struct noa_md_shmem_layout *)handle->parent_dev->shmem_handle.va_base;

	/* Execute the full atomic sync protocol */
	ret = __noa_md_shmem_sync_send_and_wait(&handle->switch_ch,
		&shmem_layout->switch_payload,
		cmd, target_path,
		ap_state, ncp_state);
	if (ret)
		NOA_MD_ERROR("Failed to send switch cmd, cmd: %u, target: %u, ret: %d",
			cmd, target_path, ret);

	return ret;
}

int noa_md_shmem_sync_send_data_cmd(
	struct noa_md_shmem_sync_handle *handle,
	enum noa_md_shmem_data_transfer_cmd sub_cmd,
	const struct noa_md_shmem_data_payload *req_payload_in,
	struct noa_md_shmem_data_payload *resp_payload_out)
{
	return __noa_md_shmem_sync_send_generic_cmd(handle, GENERIC_DATA_CHANNEL,
		(u32)sub_cmd, req_payload_in,
		resp_payload_out,
		sizeof(struct noa_md_shmem_data_payload));
}

int noa_md_shmem_sync_send_debug_cmd(
	struct noa_md_shmem_sync_handle *handle,
	enum noa_md_shmem_debug_cmd sub_cmd,
	const struct noa_md_shmem_debug_payload *req_payload_in,
	struct noa_md_shmem_debug_payload *resp_payload_out)
{
	return __noa_md_shmem_sync_send_generic_cmd(handle, GENERIC_DEBUG_CHANNEL,
		(u32)sub_cmd, req_payload_in,
		resp_payload_out,
		sizeof(struct noa_md_shmem_debug_payload));
}

int noa_md_shmem_sync_init(struct noa_md_shmem_sync_handle *handle,
	const struct noa_md_dev *parent_dev)
{
	int ret;

	CHECK_PTR_OR_RETURN_ERR(handle, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(parent_dev, -EINVAL);

	handle->parent_dev = parent_dev;
	__noa_md_shmem_sync_obj_init(&handle->switch_ch, "SWITCH_CHANNEL",
		parent_dev, SWITCH_CHANNEL_TIMEOUT_MS);
	__noa_md_shmem_sync_obj_init(&handle->data_ch, "DATA_CHANNEL",
		parent_dev, DATA_CHANNEL_TIMEOUT_MS);
	__noa_md_shmem_sync_obj_init(&handle->debug_ch, "DEBUG_CHANNEL",
		parent_dev, DEBUG_CHANNEL_TIMEOUT_MS);

	/* Register ISRs for all response/ACK doorbells */
	ret = noa_md_dpa_register_isr(
		NOA_MD_DPA_NCP_DOORBELL,
		NOA_MD_NCP2APC_SWITCH_CTRL_EVENT,
		noa_md_shmem_sync_switch_isr,
		handle);
	if (ret) {
		NOA_MD_ERROR("Failed to register SWITCH_CTRL ISR: %d", ret);
		goto err_out;
	}

	ret = noa_md_dpa_register_isr(
		NOA_MD_DPA_NCP_DOORBELL,
		NOA_MD_NCP2APC_SHMEM_DATA_ACK,
		noa_md_shmem_sync_data_isr,
		handle);
	if (ret) {
		NOA_MD_ERROR("Failed to register DATA_ACK ISR: %d", ret);
		goto err_unregister_switch;
	}

	ret = noa_md_dpa_register_isr(
		NOA_MD_DPA_NCP_DOORBELL,
		NOA_MD_NCP2APC_SHMEM_DEBUG_ACK,
		noa_md_shmem_sync_debug_isr,
		handle);
	if (ret) {
		NOA_MD_ERROR("Failed to register DEBUG_ACK ISR: %d", ret);
		goto err_unregister_data;
	}

	return 0;

err_unregister_data:
	noa_md_dpa_unregister_isr(NOA_MD_DPA_NCP_DOORBELL,
				  NOA_MD_NCP2APC_SHMEM_DATA_ACK);
err_unregister_switch:
	noa_md_dpa_unregister_isr(NOA_MD_DPA_NCP_DOORBELL,
				  NOA_MD_NCP2APC_SWITCH_CTRL_EVENT);
err_out:
	__noa_md_shmem_sync_obj_exit(&handle->switch_ch);
	__noa_md_shmem_sync_obj_exit(&handle->data_ch);
	__noa_md_shmem_sync_obj_exit(&handle->debug_ch);
	return ret;
}

void noa_md_shmem_sync_exit(struct noa_md_shmem_sync_handle *handle)
{
	CHECK_PTR_OR_RETURN(handle);

	/* Unregister all doorbells */
	noa_md_dpa_unregister_isr(NOA_MD_DPA_NCP_DOORBELL,
		NOA_MD_NCP2APC_SWITCH_CTRL_EVENT);
	noa_md_dpa_unregister_isr(NOA_MD_DPA_NCP_DOORBELL,
		NOA_MD_NCP2APC_SHMEM_DATA_ACK);
	noa_md_dpa_unregister_isr(NOA_MD_DPA_NCP_DOORBELL,
		NOA_MD_NCP2APC_SHMEM_DEBUG_ACK);

	__noa_md_shmem_sync_obj_exit(&handle->switch_ch);
	__noa_md_shmem_sync_obj_exit(&handle->data_ch);
	__noa_md_shmem_sync_obj_exit(&handle->debug_ch);
}
