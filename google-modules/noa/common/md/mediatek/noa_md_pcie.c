/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Google LLC
 */

#include "noa_md_pcie.h"

#include <linux/errno.h>
#include <linux/export.h>

#include "modem_cmd_service.pb.h"
#include "noa_md.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_trace.h"

/* DPA related header */
#include "soc/google/google_dpa_ctrl.h"  /* For enum dpa_data_path */

static struct {
	const struct noa_md_pcie_ops *ops;
} noa_md_pcie;

int noa_md_pcie_set_apc_msi_ctrls_enabled(bool enable)
{
	int return_value = 0;

	if (noa_md_pcie.ops == NULL) {
		NOA_MD_ERROR("No registered noa_md_pcie_ops.");
		return -ENODEV;
	}

	const noa_service_modem_cmd_service_HifConfig *hif_config = noa_md_pcie.ops->get_config();

	for (int i = 0; i < hif_config->dpa_msi_ctrls_count; ++i) {
		int result = noa_md_pcie.ops->enable_msi_ctrl(hif_config->dpa_msi_ctrls[i], enable);
		if (result) {
			if (!return_value) {
				return_value = result;
			}
			NOA_MD_ERROR("Failed to %s ctrl %d, error: %d",
				     (enable ? "enable" : "disable"), hif_config->dpa_msi_ctrls[i],
				     result);
		}
	}
	return return_value;
}

const noa_service_modem_cmd_service_HifConfig *noa_md_pcie_get_hif_config(void)
{
	if (noa_md_pcie.ops == NULL) {
		return NULL;
	}
	return noa_md_pcie.ops->get_config();
}

void noa_md_pcie_set_ops(const struct noa_md_pcie_ops *ops)
{
	if (!ops || !ops->enable_msi_ctrl || !ops->get_config || !ops->set_pci_user) {
		NOA_MD_ERROR("Failed to set PCIe ops: NULL pointer or missing mandatory callbacks.");
		return;
	}
	noa_md_pcie.ops = ops;
}
EXPORT_SYMBOL_GPL(noa_md_pcie_set_ops);

/**
 * noa_md_pcie_notify_switch() - Notify PCIe driver of data path state change.
 * @target_path:   The final target path (DIRECT or OFFLOAD) for the switch.
 * @current_state: The current phase of the switch process.
 *
 * This function is called by the NOA Data Path Controller. It lets the
 * PCIe driver manage its internal state (like clocks or interrupts)
 * based on the combination of the target path and the current switch phase.
 * MSI controls are handled in Phase 2 and 3.
 *
 * Return: 0 on success, or a negative error code if the hardware
 * operation (like toggling interrupts) fails.
 */
int noa_md_pcie_notify_switch(enum dpa_data_path target_path,
	enum dpath_switch_state current_state)
{
	int ret = 0;
	bool enable_interrupts;
	bool action_required = false;

	if (target_path < 0 || target_path >= NOA_DATA_PATH_COUNT) {
		NOA_MD_ERROR("Unknown target path %d", target_path);
		return -EINVAL;
	}

	if (noa_md_pcie.ops == NULL) {
		NOA_MD_ERROR("No registered noa_md_pcie_ops.");
		return -ENODEV;
	}

	switch (current_state) {
	case NOA_MD_DPATH_STATE_DEVICE_PREPARING:  /* Phase 2 */
		/* Disable APC-side MSI interrupts if switching Direct -> Offload */
		if (target_path == NOA_DATA_PATH_OFFLOAD) {
			enable_interrupts = false;
			action_required = true;
		}
		break;

	case NOA_MD_DPATH_STATE_DEVICE_RESUMING:   /* Phase 3 */
		if (target_path == NOA_DATA_PATH_DIRECT) {
			enable_interrupts = true;
			action_required = true;
		}
		break;

	case NOA_MD_DPATH_STATE_ROLLING_BACK:  /* Rollback */
		enable_interrupts = (target_path == NOA_DATA_PATH_DIRECT);
		action_required = true;
		break;

	case NOA_MD_DPATH_STATE_SERVICE_STOPPING:    /* Phase 1 */
	case NOA_MD_DPATH_STATE_SERVICE_RESTARTING:  /* Phase 4 */
	case NOA_MD_DPATH_STATE_IDLE_DIRECT:
	case NOA_MD_DPATH_STATE_IDLE_OFFLOAD:
	case NOA_MD_DPATH_STATE_FAILED:
		break;  /* No change needed, this is a success. */

	default:
		NOA_MD_ERROR("Unknown switch state %d", current_state);
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	if (!action_required) {
		return 0;
	}

	NOA_MD_INFO("Switch Notify: path: %s, state: %s, enable_interrupts: %d",
		noa_md_dpath_ctrl_state_to_str(current_state),
		noa_md_dpath_ctrl_target_path_to_str(target_path),
		enable_interrupts);

	bool enable_pci_user = !enable_interrupts;
	ret = noa_md_pcie.ops->set_pci_user(enable_pci_user);
	if (ret < 0) {
		NOA_MD_ERROR("Failed to set pci_user mode for modem RPM, ret: %d", ret);
		return ret;
	}

	ret = noa_md_pcie_set_apc_msi_ctrls_enabled(enable_interrupts);
	if (ret) {
		NOA_MD_ERROR("Failed to %s MSI ctrls, ret=%d",
			enable_interrupts ? "enable" : "disable", ret);
	}

	return ret;
}
