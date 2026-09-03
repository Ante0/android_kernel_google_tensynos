/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef __MTK_PCIE_H__
#define __MTK_PCIE_H__

#include <linux/pci.h>

#include "../common/radio-google.h"

struct platform_device;

struct mtk_google_pcie {
	struct radio_google *goog;
	u64 aoc_sram_addr;
	u32 aoc_sram_size;
};

int mtk_google_pcie_init(struct radio_google *goog);

void mtk_google_pcie_exit(struct radio_google *goog);

int mtk_pcie_probe_port(struct platform_device *pdev, int port);

int mtk_pcie_remove_port(int port);

int mtk_pcie_soft_off(struct pci_bus *bus);

int mtk_pcie_soft_on(struct pci_bus *bus);

#endif
