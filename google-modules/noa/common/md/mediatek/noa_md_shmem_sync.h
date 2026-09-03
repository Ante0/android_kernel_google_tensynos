/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * NOA Modem Shared Memory Synchronization Module
 *
 * This module encapsulates the synchronization logic for AP-NCP communication
 * that uses a doorbell and shared memory protocol.
 */

#ifndef __NOA_MD_SHMEM_SYNC_H__
#define __NOA_MD_SHMEM_SYNC_H__

#include <linux/completion.h>
#include <linux/mutex.h>

/* Forward declarations to avoid circular dependencies */
struct noa_md_dev;
struct dpath_ap_state_payload;
struct dpath_ncp_state_payload;
enum noa_md_switch_command;
enum dpa_data_path;
// Forward declarations for generic command payloads
struct noa_md_shmem_data_payload;
struct noa_md_shmem_debug_payload;
enum noa_md_shmem_data_transfer_cmd;
enum noa_md_shmem_debug_cmd;

/**
 * enum noa_md_shmem_channel_type - Defines the type of generic channel to use.
 *
 * This is used by the refactored generic command API to select the
 * appropriate shared memory payload, lock, and doorbell for a command.
 */
enum noa_md_shmem_channel_type {
	/**
	 * @GENERIC_DATA_CHANNEL: The channel for general-purpose control commands,
	 * such as table updates. Uses the SHMEM_DATA doorbell pair.
	 */
	GENERIC_DATA_CHANNEL,

	/**
	 * @GENERIC_DEBUG_CHANNEL: The channel for debug and diagnostics commands.
	 * Uses the SHMEM_DEBUG doorbell pair.
	 */
	GENERIC_DEBUG_CHANNEL,
};

/**
 * struct noa_md_shmem_sync_obj - Manages a single communication channel.
 *
 * This object holds all necessary synchronization primitives for one
 * command-response channel between the AP and NCP.
 */
struct noa_md_shmem_sync_obj {
	/* Protects the payload for this specific channel. */
	struct mutex payload_lock;

	/* Used to wait for the NCP's response doorbell. */
	struct completion response_done;

	/* The name of the channel, used for logging. */
	const char *channel_name;

	/* The timeout value for this channel in jiffies. */
	long timeout_jiffies;

	/* Back-pointer to parent device for resource access */
	const struct noa_md_dev *parent_dev;
};

/**
 * struct noa_md_shmem_sync_handle - Manages all AP-NCP sync channels.
 *
 * This handle is the main context for the shmem_sync module. It contains
 * a sync object for each communication channel.
 */
struct noa_md_shmem_sync_handle {
	/* The critical path channel for data path switching. */
	struct noa_md_shmem_sync_obj switch_ch;

	/* The Generic and Debug paths channels */
	struct noa_md_shmem_sync_obj data_ch;
	struct noa_md_shmem_sync_obj debug_ch;

	/* A pointer back to the parent noa_md_dev structure. */
	const struct noa_md_dev *parent_dev;
};

/**
 * noa_md_shmem_sync_send_switch_cmd() - Sends a data path switch command.
 *
 * This function encapsulates the full "lock-write-ring-wait-unlock" sequence
 * for the data path switching channel.
 *
 * @handle:      Pointer to the main synchronization handle.
 * @cmd:         The switch command to send.
 * @target_path: The final data path destination.
 * @ap_state:    (Optional) Pointer to the AP state payload.
 * @ncp_state:   (Optional) Pointer to store the NCP's state response.
 *
 * Return:
 * * 0: On success.
 * * -ETIMEDOUT: If the NCP does not respond within the timeout.
 * * -EIO: If the NCP reports a failure.
 * * Other negative error codes on failure.
 */
int noa_md_shmem_sync_send_switch_cmd(
	struct noa_md_shmem_sync_handle *handle,
	enum noa_md_switch_command cmd,
	enum dpa_data_path target_path,
	const struct dpath_ap_state_payload *ap_state,
	struct dpath_ncp_state_payload *ncp_state);

/**
 * noa_md_shmem_sync_send_data_cmd() - Sends a generic data command.
 *
 * Uses the Notify/Ack protocol via SHMEM_DATA doorbells.
 *
 * @handle:           Pointer to the main synchronization handle.
 * @sub_cmd:          The specific data command from enum noa_md_shmem_data_transfer_cmd.
 * @req_payload_in:   (Optional) Pointer to data payload to send. Content depends on sub_cmd.
 * @resp_payload_out: (Optional) Pointer to store the response payload from NCP.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_shmem_sync_send_data_cmd(
	struct noa_md_shmem_sync_handle *handle,
	enum noa_md_shmem_data_transfer_cmd sub_cmd,
	const struct noa_md_shmem_data_payload *req_payload_in,
	struct noa_md_shmem_data_payload *resp_payload_out);

/**
 * noa_md_shmem_sync_send_debug_cmd() - Sends a generic debug command.
 *
 * Uses the Notify/Ack protocol via SHMEM_DEBUG doorbells.
 *
 * @handle:           Pointer to the main synchronization handle.
 * @sub_cmd:          The specific debug command from enum noa_md_shmem_debug_cmd.
 * @req_payload_in:   (Optional) Pointer to debug payload to send.
 * @resp_payload_out: (Optional) Pointer to store the response payload from NCP.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_shmem_sync_send_debug_cmd(
	struct noa_md_shmem_sync_handle *handle,
	enum noa_md_shmem_debug_cmd sub_cmd,
	const struct noa_md_shmem_debug_payload *req_payload_in,
	struct noa_md_shmem_debug_payload *resp_payload_out);

/**
 * noa_md_shmem_sync_init() - Initializes the synchronization handle.
 * @handle:     Pointer to the sync handle to initialize.
 * @parent_dev: Pointer to the parent noa_md_dev structure.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_shmem_sync_init(struct noa_md_shmem_sync_handle *handle,
	const struct noa_md_dev *parent_dev);

/**
 * noa_md_shmem_sync_exit() - Deinitializes the synchronization handle.
 * @handle: Pointer to the sync handle to deinitialize.
 */
void noa_md_shmem_sync_exit(struct noa_md_shmem_sync_handle *handle);

#endif /* __NOA_MD_SHMEM_SYNC_H__ */
