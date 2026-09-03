/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Declaration of exported DPA Ctrl APIs.
 *
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_CTRL_H
#define _GOOGLE_DPA_CTRL_H

/*
 * Defines the sequence of actions during a dynamic switch.
 * Components selectively react to relevant actions.
 * Example: Network stack handles service pre/post, ex. drain
 * packets in queues, and WLAN handles device pre/post, ex. mask
 * interrupt, use RPC to prepare remote endpoint.
 */
enum dpa_action {
	NOA_ACTION_SERVICE_PRE_SWITCH,
	NOA_ACTION_DEVICE_PRE_SWITCH,
	NOA_ACTION_DEVICE_POST_SWITCH,
	NOA_ACTION_SERVICE_POST_SWITCH,
	NOA_ACTION_PCIE_OWNERSHIP_PRE_SWITCH,
	NOA_ACTION_PCIE_OWNERSHIP_POST_SWITCH,
	NOA_ACTION_UPDATE,
};

/*
 * This describes the DPA controller state.
 * Only when the controller is at READY state, it can accept
 * external request,ex. set_data_path.
 */
enum dpa_state {
	NOA_STATE_UNAVAILABLE,
	NOA_STATE_READY,
	NOA_STATE_CRASH,
	NOA_STATE_COUNT,
};

/*
 * This describes current DPA data path.
 * DIRECT indicates the data won't be handled by DPA, but handled
 * by the application processor directly. OFFLOAD indicates the data
 * are proceeded by DPA first.
 */
enum dpa_data_path {
	NOA_DATA_PATH_DIRECT,
	NOA_DATA_PATH_OFFLOAD,
	NOA_DATA_PATH_COUNT,
};

/*
 * This describes current PCIe ownership.
 */
enum dpa_pcie_ownership {
	NOA_PCIE_OWNERSHIP_APC,
	NOA_PCIE_OWNERSHIP_DPA,
	NOA_PCIE_OWNERSHIP_COUNT,
};

typedef void (*on_state_changed_cb)(enum dpa_state state, void *context);
typedef void (*on_data_path_changed_cb)(enum dpa_data_path dest,
					enum dpa_action action, void *context);
typedef void (*on_pcie_ownership_changed_cb)(enum dpa_pcie_ownership owner,
					enum dpa_action action, void *context);

struct dpa_callbacks {
	on_state_changed_cb on_state_changed;
	on_data_path_changed_cb on_data_path_changed;
	on_pcie_ownership_changed_cb on_pcie_ownership_changed;
};

struct dpa_client;

/**
 * google_dpa_ctrl_register() - Register to the dpa controller.
 * @client_name: The client name.
 * @callbacks: callbacks for dpa events.
 * @context: The context which is passed along with a callback.
 *
 * Return: return the client pointer or NULL for any errors.
 */
struct dpa_client *google_dpa_ctrl_register(const char *client_name,
					    struct dpa_callbacks *callbacks,
					    void *context);

/**
 * google_dpa_ctrl_unregister() - Unregister to the dpa controller.
 * @client: The dpa_client pointer.
 *
 * Return: 0 for success; otherwise, a negative error code.
 */
int google_dpa_ctrl_unregister(struct dpa_client *client);

#endif /* _GOOGLE_DPA_CTRL_H */
