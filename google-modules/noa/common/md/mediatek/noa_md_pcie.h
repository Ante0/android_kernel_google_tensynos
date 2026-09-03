/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Google LLC
 */

#ifndef __NOA_MD_PCIE_H__
#define __NOA_MD_PCIE_H__

#include <linux/errno.h>
#include <linux/types.h>

#include "modem_cmd_service.pb.h"

/*
 * Forward declare enums to avoid circular header dependencies,
 * as this file is included by other modules (like T900).
 */
enum dpa_data_path;
enum dpath_switch_state;

struct noa_md_pcie_irq_mapping {
	int irq_id;
	int hwirq;
};

struct noa_md_pcie_bar_region {
	uintptr_t addr;
	size_t size;
};

/**
 * struct noa_md_pcie_ops - PCIe operations for the NOA modem
 * @enable_msi_ctrl: Enable or disable a specific MSI control interrupt line.
 *                   Takes the control line index and a boolean enable flag.
 *                   Returns 0 on success or a negative error code.
 * @get_config: Retrieve the PCIe configuration settings.
 *              Returns a pointer to a static configuration structure.
 * @set_pci_user: Notify the modem driver of the NOA offload mode status.
 *                The boolean parameter indicates if offload is active.
 *                This prevents the driver from disabling the PCIe link
 *                while it is still in use. Returns 0 or negative error.
 *
 * This structure defines the callbacks implemented by the modem driver to
 * abstract the hardware-specific PCIe layer operations.
 */
struct noa_md_pcie_ops {
	int (*enable_msi_ctrl)(int, bool);
	const noa_service_modem_cmd_service_HifConfig *(*get_config)(void);
	int (*set_pci_user)(bool);
};

/**
 * noa_md_pcie_set_ops() - Set PCIe operations for the NOA module.
 * @ops: Pointer to the 'noa_md_pcie_ops' structure.
 *
 * This function registers a set of PCIe operational functions to be
 * used by the NOA module. It's intended to be called by a vendor
 * driver that implements the required operations.
 */
void noa_md_pcie_set_ops(const struct noa_md_pcie_ops *ops);

/**
 * noa_md_pcie_set_apc_msi_ctrls_enabled() - Enable or disable MSI control lines offload to NOA.
 * @enable: %true to enable the control lines for APC; %false to disable.
 *
 * This function manages the ownership of the MSI control lines. When offloading these lines to NOA,
 * we typically call noa_md_pcie_set_apc_msi_ctrls_enabled(false) to disable the control line
 * interrupts from the APC. Conversely, noa_md_pcie_set_apc_msi_ctrls_enabled(true) is called to
 * enable the APC MSI control lines when we stop offloading.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_pcie_set_apc_msi_ctrls_enabled(bool enable);

/**
 * noa_md_pcie_get_hif_config - Get the Modem HIF configuration.
 *
 * This function retrieves a pointer to the statically allocated Modem HIF
 * configuration structure.
 *
 * Return: A pointer to the configuration structure on success, or NULL if
 * no configuration is available.
 */
 const noa_service_modem_cmd_service_HifConfig *noa_md_pcie_get_hif_config(void);

/**
 * noa_md_pcie_notify_switch() - Notify PCIe driver of data path state change.
 * @target_path:   The final target path (DIRECT or OFFLOAD) for the switch.
 * @current_state: The current phase of the switch process.
 *
 * This function is called by the NOA Data Path Controller. It lets the
 * PCIe driver manage its internal state (like clocks or interrupts)
 * based on the combination of the target path and the current switch phase.
 *
 * Return: 0 on success, or a negative error code if the hardware
 * operation (like toggling interrupts) fails.
 */
int noa_md_pcie_notify_switch(enum dpa_data_path target_path,
	enum dpath_switch_state current_state);

#endif
