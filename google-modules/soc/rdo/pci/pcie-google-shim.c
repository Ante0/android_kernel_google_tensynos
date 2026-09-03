// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 *
 * Temporarily provide backward compatibility for EP drivers.  Long term,
 * these interfaces should be removed and addressed in the EP drivers.
 */

#include <linux/pcie_google_if.h>
#include "pcie-google.h"
#include "pcie-google-shim.h"

void exynos_pcie_set_perst_gpio(int ch_num, bool on)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL_GPL(exynos_pcie_set_perst_gpio);

void exynos_pcie_set_ready_cto_recovery(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL(exynos_pcie_set_ready_cto_recovery);

void exynos_pcie_set_skip_config(int ch_num, bool val)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL_GPL(exynos_pcie_set_skip_config);

int exynos_pcie_rc_set_outbound_atu(int ch_num, u32 target_addr, u32 offset, u32 size)
{
	pr_warn("WARNING: %s() not implemented", __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(exynos_pcie_rc_set_outbound_atu);

void exynos_pcie_rc_print_msi_register(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL_GPL(exynos_pcie_rc_print_msi_register);

void exynos_pcie_rc_register_dump(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL_GPL(exynos_pcie_rc_register_dump);

void exynos_pcie_rc_dump_all_status(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL_GPL(exynos_pcie_rc_dump_all_status);

void exynos_pcie_rc_force_linkdown_work(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL(exynos_pcie_rc_force_linkdown_work);

bool exynos_pcie_rc_get_sudden_linkdown_state(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
	return false;
}
EXPORT_SYMBOL(exynos_pcie_rc_get_sudden_linkdown_state);

void exynos_pcie_rc_set_sudden_linkdown_state(int ch_num, bool recovery)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL(exynos_pcie_rc_set_sudden_linkdown_state);

bool exynos_pcie_rc_get_cpl_timeout_state(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
	return false;
}
EXPORT_SYMBOL(exynos_pcie_rc_get_cpl_timeout_state);

void exynos_pcie_rc_set_cpl_timeout_state(int ch_num, bool recovery)
{
	pr_warn("WARNING: %s() not implemented", __func__);
}
EXPORT_SYMBOL(exynos_pcie_rc_set_cpl_timeout_state);

int exynos_pcie_l1_exit(int ch_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(exynos_pcie_l1_exit);

int exynos_pcie_rc_l1ss_ctrl(int enable, int id, int ch_num)
{
	int ret;
	int aspm_state = 0;
	struct pci_bus *pci_bus;
	struct pci_dev *pci_dev;

	pci_bus = pci_find_bus(ch_num, 1);
	if (!pci_bus) {
		pr_err("Child bus-1 for channel %d not found\n", ch_num);
		return -ENODEV;
	}

	pci_dev = pci_get_slot(pci_bus, PCI_DEVFN(0, 0));
	if (!pci_dev) {
		pr_err("Endpoint device for channel %d not found\n", ch_num);
		return -ENODEV;
	}

	if (enable)
		aspm_state = PCIE_LINK_STATE_L1 | PCIE_LINK_STATE_CLKPM |
			     PCIE_LINK_STATE_L1_1 | PCIE_LINK_STATE_L1_2;

	ret = pci_enable_link_state(pci_dev, aspm_state);
	pci_dev_put(pci_dev);
	return ret;
}
EXPORT_SYMBOL_GPL(exynos_pcie_rc_l1ss_ctrl);

int exynos_pcie_rc_chk_link_status(int ch_num)
{
	return google_pcie_link_status(ch_num);
}
EXPORT_SYMBOL_GPL(exynos_pcie_rc_chk_link_status);

int exynos_pcie_register_event(void *reg)
{
	pr_warn("WARNING: %s() not implemented", __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(exynos_pcie_register_event);

int exynos_pcie_deregister_event(void *reg)
{
	pr_warn("WARNING: %s() not implemented", __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(exynos_pcie_deregister_event);

int register_separated_msi_vector(int ch_num, irq_handler_t handler,
				  void *context, int *irq_num)
{
	pr_warn("WARNING: %s() not implemented", __func__);
	return 0;
}
EXPORT_SYMBOL_GPL(register_separated_msi_vector);
