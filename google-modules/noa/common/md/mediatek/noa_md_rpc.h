/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * RPC interface definitions for the NOA Mediatek Modem Driver, including
 * data path switching commands.
 */
#ifndef __NOA_MD_RPC_H__
#define __NOA_MD_RPC_H__

#include <linux/device.h>  /* For struct device */
#include <linux/types.h>  /* For u32 */

/* NOA modem related header */
#include "noa_md.h"
#include "noa_md_data_path_ctrl.h"  /* For dpath_*_state_payload */

/* DPA related header */
#include "soc/google/google_dpa_ctrl.h"  /* For enum dpa_data_path */

/**
 * enum ncp_modem_rpc_cmd - All RPC commands sent from AP to NCP modem logic.
 *
 * This enum provides a conceptual list of all supported RPC commands.
 */
enum ncp_modem_rpc_cmd {
	/* Initialization Commands */
	NCP_RPC_CMD_FW_INIT,

	/* Data Path Switching Commands */
	NCP_RPC_CMD_NOTIFY_PREPARE_SWITCH,
	NCP_RPC_CMD_EXCHANGE_STATE,
	NCP_RPC_CMD_COMMIT_SWITCH,
};

/**
 * noa_md_rpc_fw_init() - Sends firmware initialization info to NCP via RPC.
 * @dev: Pointer to the device structure.
 * @fw_info: Pointer to the firmware initialization data.
 *
 * This is a wrapper for the dpa_rpc_modem_cmd_service_fw_init RPC call.
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_rpc_fw_init(struct device *dev,
	const struct noa_md_fw_init *fw_info);

/**
 * noa_md_rpc_notify_prepare_switch() - Tell NCP to prepare for a switch.
 * @target_path: The data path we are switching to.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_rpc_notify_prepare_switch(enum dpa_data_path target_path);

/**
 * noa_md_rpc_exchange_state() - Exchange final ring states with NCP.
 * @ap_state:  Pointer to AP's final ring state.
 * @ncp_state: Pointer to a struct to store NCP's final ring state.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_rpc_exchange_state(const struct dpath_ap_state_payload *ap_state,
	struct dpath_ncp_state_payload *ncp_state);

/**
 * noa_md_rpc_commit_switch() - Tell NCP to commit to the new data path.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_rpc_commit_switch(void);

#endif /* __NOA_MD_RPC_H__ */