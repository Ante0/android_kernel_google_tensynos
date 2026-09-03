/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 *
 * RPC implementations for NOA Mediatek Modem Driver.
 */

#include <linux/device.h>  // For struct device
#include <linux/types.h>  // For google_dpa_service_modem_cmd

#include "noa_md.h"
#include "noa_md_rpc.h"
#include "noa_md_trace.h"

/* DPA related header */
#include "soc/google/google_dpa_service_modem_cmd.h"  /* For dpa_rpc_modem_cmd_service_fw_init */

int noa_md_rpc_fw_init(struct device *dev,
	const struct noa_md_fw_init *fw_info)
{
	return dpa_rpc_modem_cmd_service_fw_init(dev, fw_info);
}

int noa_md_rpc_notify_prepare_switch(enum dpa_data_path target_path)
{
	NOA_MD_INFO("Sending NOTIFY_PREPARE_SWITCH to NCP, target: %d",
		target_path);
	// TODO: b/434646935 - Implement RPC call to notify NCP for switch preparation.
	return 0;
}

int noa_md_rpc_exchange_state(const struct dpath_ap_state_payload *ap_state,
	struct dpath_ncp_state_payload *ncp_state)
{
	NOA_MD_INFO("Sending EXCHANGE_STATE to NCP");
	// TODO: b/434646935 - Implement RPC call to exchange final ring states with NCP.

	return 0;
}

int noa_md_rpc_commit_switch(void)
{
	NOA_MD_INFO("Sending COMMIT_SWITCH to NCP");
	// TODO: b/434646935 - Implement RPC call to command NCP to commit the switch.
	return 0;
}