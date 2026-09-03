/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */

#ifndef _PCIE_DESIGNWARE_HOST_CUSTOMIZED_H_
#define _PCIE_DESIGNWARE_HOST_CUSTOMIZED_H_

#include "pcie-designware.h"
#include <linux/version.h>

int goog_pcie_msi_host_init(struct dw_pcie_rp *pp);

#define MAX_NR_LANES	16
#define PCI_EQ_RESV	0xff

enum equalization_preset_type {
	EQ_PRESET_TYPE_8GTS,
	EQ_PRESET_TYPE_16GTS,
	EQ_PRESET_TYPE_32GTS,
	EQ_PRESET_TYPE_64GTS,
	EQ_PRESET_TYPE_MAX
};

struct pci_eq_presets {
	u16 eq_presets_8gts[MAX_NR_LANES];
	u8 eq_presets_Ngts[EQ_PRESET_TYPE_MAX - 1][MAX_NR_LANES];
};

int dw_pcie_link_get_max_link_width(struct dw_pcie *pci);

int of_pci_get_equalization_presets(struct device *dev,
				    struct pci_eq_presets *presets,
				    int num_lanes);

void dw_pcie_config_presets(struct dw_pcie_rp *pp);

#endif /* _PCIE_DESIGNWARE_HOST_CUSTOMIZED_H_ */
