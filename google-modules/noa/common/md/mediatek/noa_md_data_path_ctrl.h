/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * NOA Modem Data Path Controller
 * Manages the dynamic switching between DPMAIF and NOA data paths.
 */

#ifndef __NOA_MD_DATA_PATH_CTRL_H__
#define __NOA_MD_DATA_PATH_CTRL_H__

#include <linux/atomic.h>      /* For atomic_t */
#include <linux/completion.h>  /* For struct completion */
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* NOA modem related header */
#include "common/md/mediatek/noa_md_shmem_layout.h" /* For struct dpath_ncp_state_payload */

/* DPA related header */
#include "soc/google/google_dpa_ctrl.h"  /* For enum dpa_action, dpa_data_path */

/* Forward declaration */
struct noa_dpath_client;
struct noa_md_dev;
struct noa_md_dpath_ctrl;

/**
 * enum dpath_switch_state - States for the data path switch state machine.
 *
 * These states track the progress of the switch, matching the dpa_action
 * steps.
 */
enum dpath_switch_state {
	/* Stable states */
	NOA_MD_DPATH_STATE_IDLE_DIRECT,
	NOA_MD_DPATH_STATE_IDLE_OFFLOAD,

	/* In-progress states */
	NOA_MD_DPATH_STATE_SERVICE_STOPPING,
	NOA_MD_DPATH_STATE_DEVICE_PREPARING,
	NOA_MD_DPATH_STATE_DEVICE_RESUMING,
	NOA_MD_DPATH_STATE_SERVICE_RESTARTING,

	/* Error handling states */
	NOA_MD_DPATH_STATE_FAILED,
	NOA_MD_DPATH_STATE_ROLLING_BACK,
};

/**
 * enum noa_dpath_client_type - Identifies the client module (TX or RX).
 */
enum noa_dpath_client_type {
	NOA_DPATH_CLIENT_TX,
	NOA_DPATH_CLIENT_RX,
	NOA_CPATH_CLDMA,
};

/**
 * enum noa_dpath_client_state - Tracks the completion status of a client.
 */
enum noa_dpath_client_state {
	NOA_DPATH_CLIENT_STATE_PENDING,
	NOA_DPATH_CLIENT_STATE_SUCCESS,
	NOA_DPATH_CLIENT_STATE_FAILED,
};

/**
 * struct noa_dpath_client_ops - Callbacks for client modules.
 * @on_state_change: Called when the controller's state changes.
 * @state:           The new state the client should transition to.
 * @target_path:     The final data path destination (DIRECT or OFFLOAD).
 * @ncp_state:       Pointer to NCP state data, only valid in DEVICE_PREPARING.
 */
struct noa_dpath_client_ops {
	void (*on_state_change)(
		struct noa_dpath_client *client,
		enum dpath_switch_state state,
		enum dpa_data_path target_path,
		const struct dpath_ncp_state_payload *ncp_state);
};

/**
 * struct noa_dpath_client - A handle for a registered client.
 * @node:            List head for linking into the controller's client list.
 * @ctrl:            Pointer back to the owning controller context.
 * @type:            The type of this client.
 * @ops:             The callback operations for this client.
 * @work:            Work struct for concurrent notification dispatch.
 * @status:          The completion status of the client for the current step.
 * @ncp_state_cache: Temporary storage for ncp_state during async dispatch.
 */
struct noa_dpath_client {
	struct list_head node;
	struct noa_md_dpath_ctrl *ctrl;
	enum noa_dpath_client_type type;
	const struct noa_dpath_client_ops *ops;

	/* For concurrent dispatch and status tracking */
	struct work_struct work;
	atomic_t status;
	const struct dpath_ncp_state_payload *ncp_state_cache;
};

/**
 * enum noa_dpath_failure_injection - Types of failures to inject for
 * debugging.
 */
enum noa_dpath_failure_injection {
	NOA_DPATH_FAIL_NONE,
	NOA_DPATH_FAIL_CLIENT_TX,
	NOA_DPATH_FAIL_CLIENT_RX,
	NOA_DPATH_FAIL_DOORBELL,
};

/**
 * struct noa_dpath_stats - Statistics for data path switching operations.
 * @successful_switches: Counter for successful switches.
 * @failed_switches:     Counter for failed switches.
 * @rollbacks:           Counter for rollbacks due to failures.
 * @last_latency_ns:     Latency of the most recent switch in nanoseconds.
 * @total_latency_ns:    Sum of all switch latencies for calculating average.
 * @max_latency_ns:      Maximum latency observed.
 * @min_latency_ns:      Minimum latency observed.
 */
struct noa_dpath_stats {
	unsigned long long successful_switches;
	unsigned long long failed_switches;
	unsigned long long rollbacks;
	ktime_t last_latency_ns;
	ktime_t total_latency_ns;
	ktime_t max_latency_ns;
	ktime_t min_latency_ns;
};

/**
 * struct noa_md_dpath_ctrl - State holder for the data path controller.
 * @dev:                 Pointer back to the main modem device structure.
 * @lock:                Protects all state transitions and the client list.
 * @event_wq:            Workqueue for dispatching client notifications concurrently.
 * @payload_lock:        Mutex to protect shared memory payload for Doorbell cmd.
 * @current_state:       The current state of the switching process.
 * @old_state:           The state before the switch began, for rollback.
 * @target_path:         The final target data path for the current switch.
 * @old_target_path:     The source data path before the switch begin, used for rollback.
 * @client_list:         A list of registered clients (e.g., TX, RX).
 * @dpa_client:          Client handle for registration with the DPA controller.
 * @pending_clients:     Atomic counter for clients that have not yet reported.
 * @all_clients_done:    Completion signal for waiting on all clients.
 * @stats:               Statistics for data path switching.
 * @switch_start_time:   Timestamp to measure switch latency.
 * @timeout_ms:          Client completion timeout in milliseconds.
 * @failure_injection:   Type of failure to inject for the next switch.
 */
struct noa_md_dpath_ctrl {
	struct noa_md_dev *dev;
	struct mutex lock;
	struct workqueue_struct *event_wq;

	struct mutex payload_lock;

	enum dpath_switch_state current_state;
	enum dpath_switch_state old_state;
	enum dpa_data_path target_path;
	enum dpa_data_path old_target_path;

	struct list_head client_list;
	struct dpa_client *dpa_client;

	atomic_t pending_clients;
	struct completion all_clients_done;

	struct noa_dpath_stats stats;

	ktime_t switch_start_time;
	unsigned int timeout_ms;

	struct dpath_ap_state_payload ap_state;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	enum noa_dpath_failure_injection failure_injection;
#endif
};

/**
 * noa_md_dpath_ctrl_state_to_str() - Converts a state enum to a string for
 * logging.
 * @state: The state enum to convert.
 *
 * Return: A constant string representing the state.
 */
const char *noa_md_dpath_ctrl_state_to_str(enum dpath_switch_state state);

/**
 * noa_md_dpath_ctrl_target_path_to_str() - Converts a data path enum to a
 * string for logging.
 * @data_path: The data path enum to convert.
 *
 * Return: A constant string representing the data path.
 */
const char *noa_md_dpath_ctrl_target_path_to_str(enum dpa_data_path data_path);

/**
 * noa_md_dpath_ctrl_register_client() - Allows modules to register.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 * @type:     The client type (e.g., TX or RX).
 * @ops:      A struct containing the function pointers for callbacks.
 *
 * Return: A valid client handle on success, or an ERR_PTR on failure.
 */
struct noa_dpath_client *noa_md_dpath_ctrl_register_client(
	struct noa_md_dev *p_md_dev,
	enum noa_dpath_client_type type,
	const struct noa_dpath_client_ops *ops);

/**
 * noa_md_dpath_ctrl_unregister_client() - Unregisters a client module.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 * @client:   The client handle returned by the register function.
 */
void noa_md_dpath_ctrl_unregister_client(
	struct noa_md_dev *p_md_dev,
	struct noa_dpath_client *client);

/**
 * noa_md_dpath_ctrl_report_completion() - Called by clients to report
 * completion.
 * @client:  The handle of the client reporting completion.
 * @success: True if the operation was successful, false otherwise.
 */
void noa_md_dpath_ctrl_report_completion(struct noa_dpath_client *client,
	bool success);

/**
 * noa_md_dpath_ctrl_get_state() - Safely gets the current data path switch
 * state.
 * @ctrl: Pointer to the data path controller context.
 *
 * Return: The current enum dpath_switch_state.
 */
static inline enum dpath_switch_state noa_md_dpath_ctrl_get_state(
	struct noa_md_dpath_ctrl *ctrl)
{
	enum dpath_switch_state current_state;

	if (unlikely(!ctrl))
		return NOA_MD_DPATH_STATE_FAILED;

	mutex_lock(&ctrl->lock);
	current_state = ctrl->current_state;
	mutex_unlock(&ctrl->lock);

	return current_state;
}

/**
 * noa_md_dpath_ctrl_init() - Initializes the data path controller.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * This function also registers necessary callbacks with the DPA controller.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_dpath_ctrl_init(struct noa_md_dev *p_md_dev);

/**
 * noa_md_dpath_ctrl_exit() - Deinitializes the data path controller.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * This function also unregisters callbacks from the DPA controller.
 */
void noa_md_dpath_ctrl_exit(struct noa_md_dev *p_md_dev);

#endif /* __NOA_MD_DATA_PATH_CTRL_H__ */
