// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/delay.h>
#include <linux/device/bus.h>
#include <linux/dma-map-ops.h>
#include <linux/iommu.h>
#include <linux/module.h>
#include <linux/pci-pwrctrl.h>
#include <linux/pci.h>
#include <linux/pcie_google_if.h>
#include <linux/platform_device.h>

#include "mtk-pcie.h"
#include "radio-utils.h"
#include "ramdump-stat.h"

static struct pci_pwrctrl g_pwrctrl;

/*
 * pci-pwrctrl enumerates PCI devices asynchronously. Poll for a few seconds.
 *
 * @match_node: Device node to match up with a PCI device.
 */
static int mtk_wait_for_pcidev(struct device_node *match_node)
{
	const int poll_ms = 5 * 1000;
	const int interval_ms = 100;
	struct device *dev;

	for (int i = 0; i < poll_ms; i += interval_ms) {
		dev = bus_find_device_by_of_node(&pci_bus_type, match_node);
		if (dev) {
			put_device(dev);
			return 0;
		}

		msleep(interval_ms);
	}

	/*
	 * Check one last time, so the last sleep in the above loop isn't
	 * wasted.
	 */
	dev = bus_find_device_by_of_node(&pci_bus_type, match_node);
	if (dev) {
		put_device(dev);
		return 0;
	}

	LOG_ERR("Failed to enumerate PCI device for node: %pOF\n", match_node);
	return -ETIMEDOUT;
}

static int setup_aoc_smmu(struct mtk_google_pcie *mtk_google_pcie)
{
	struct device *dev = mtk_google_pcie->goog->mdev->dev;
	u32 size;
	int prot, rc;
	unsigned long long phys_addr;

	phys_addr = mtk_google_pcie->aoc_sram_addr;
	size = mtk_google_pcie->aoc_sram_size;
	if (!phys_addr || !size) {
		LOG_ERR("Invalid AoC SRAM address (%#llx) or size (%#x)\n", phys_addr, size);
		return -EINVAL;
	}

	if (iommu_iova_to_phys(iommu_get_domain_for_dev(dev), phys_addr)) {
		LOG_ERR("AoC SMMU already mapped!\n");
		return 0;
	}

	if (dev_is_dma_coherent(dev))
		prot = IOMMU_READ | IOMMU_WRITE | IOMMU_CACHE;
	else
		prot = IOMMU_READ | IOMMU_WRITE;

	rc = iommu_map(iommu_get_domain_for_dev(dev), phys_addr, phys_addr, size, prot, GFP_KERNEL);
	if (rc) {
		LOG_ERR("iommu_map failed for %#llx (size: %#x, rc: %d)\n", phys_addr, size, rc);
	}

	return rc;
}

static int reset_aoc_smmu(struct mtk_google_pcie *mtk_google_pcie)
{
	struct device *dev = mtk_google_pcie->goog->mdev->dev;
	u32 size;
	size_t rc;
	unsigned long long phys_addr;

	phys_addr = mtk_google_pcie->aoc_sram_addr;
	size = mtk_google_pcie->aoc_sram_size;
	if (!phys_addr || !size) {
		LOG_ERR("Invalid AoC SRAM address (%#llx) or size (%#x)\n", phys_addr, size);
		return -EINVAL;
	}

	if (!iommu_iova_to_phys(iommu_get_domain_for_dev(dev), phys_addr)) {
		LOG_ERR("AoC SMMU already unmapped!\n");
		return 0;
	}

	rc = iommu_unmap(iommu_get_domain_for_dev(dev), phys_addr, size);
	if (rc != size) {
		LOG_ERR("iommu_unmap failed: %#llx, size: %#x, rc: %#zx\n", phys_addr, size, rc);
	}

	return 0;
}

static void get_modem_data_from_dts(struct mtk_google_pcie *mtk_google_pcie)
{
	int rc;
	struct device_node *radio_data_np;

	radio_data_np = of_find_node_by_path("/");
	if (!radio_data_np) {
		LOG_ERR("root dts node not found!\n");
		return;
	}

	radio_data_np = of_get_child_by_name(radio_data_np, "radio-google-data");
	if (!radio_data_np) {
		LOG_ERR("radio-google-data not found in dts!\n");
		return;
	}

	rc = of_property_read_u64(radio_data_np, "aoc_sram_addr", &mtk_google_pcie->aoc_sram_addr);
	if (rc)
		LOG_ERR("aoc_sram_addr not found in dts!\n");

	rc = of_property_read_u32(radio_data_np, "aoc_sram_size", &mtk_google_pcie->aoc_sram_size);
	if (rc)
		LOG_ERR("aoc_sram_size not found in dts!\n");

	mtk_google_pcie->aoc_sram_size = ALIGN(mtk_google_pcie->aoc_sram_size, PAGE_SIZE);
	LOG_INFO("AoC SRAM data: addr=%#llx, size=%#x\n", mtk_google_pcie->aoc_sram_addr,
		 mtk_google_pcie->aoc_sram_size);
	of_node_put(radio_data_np);
}

/* PMIC power is restored. */
static int mtk_pcie_pwrctrl_on(int num)
{
	int ret;

	ret = pci_pwrctrl_device_set_ready(&g_pwrctrl);
	if (ret) {
		LOG_ERR("failed to set pwrctrl readiness: %d\n", ret);
		return ret;
	}

	ret = mtk_wait_for_pcidev(g_pwrctrl.dev->of_node);
	if (ret) {
		LOG_ERR("mtk_wait_for_pcidev failed: %d\n", ret);
		pci_pwrctrl_device_unset_ready(&g_pwrctrl);
		return ret;
	}

	update_google_cdd_modem_stat(CDD_EVENT_PCIE_LINK, true);

	return 0;
}

/* PMIC power is being removed. */
static int mtk_pcie_pwrctrl_off(int num)
{
	pci_pwrctrl_device_unset_ready(&g_pwrctrl);
	update_google_cdd_modem_stat(CDD_EVENT_PCIE_LINK, false);

	return 0;
}

static void mtk_pci_pwrctrl_init(struct platform_device *pdev)
{
	struct pci_pwrctrl *pwrctrl = &g_pwrctrl;

	pci_pwrctrl_init(pwrctrl, &pdev->dev);
}

int mtk_pcie_probe_port(struct platform_device *pdev, int port)
{
	mtk_pci_pwrctrl_init(pdev);

	return mtk_pcie_pwrctrl_on(port);
}
EXPORT_SYMBOL_GPL(mtk_pcie_probe_port);

int mtk_pcie_remove_port(int port)
{
	return mtk_pcie_pwrctrl_off(port);
}
EXPORT_SYMBOL_GPL(mtk_pcie_remove_port);

int mtk_pcie_soft_off(struct pci_bus *bus)
{
	return mtk_pcie_pwrctrl_off(pci_domain_nr(bus));
}
EXPORT_SYMBOL_GPL(mtk_pcie_soft_off);

int mtk_pcie_soft_on(struct pci_bus *bus)
{
	return mtk_pcie_pwrctrl_on(pci_domain_nr(bus));
}
EXPORT_SYMBOL_GPL(mtk_pcie_soft_on);

int mtk_google_pcie_init(struct radio_google *goog)
{
	struct mtk_google_pcie *mtk_google_pcie;
	int ret;

	mtk_google_pcie = devm_kzalloc(goog->mdev->dev, sizeof(*mtk_google_pcie), GFP_KERNEL);
	if (!mtk_google_pcie)
		return -ENOMEM;

	mtk_google_pcie->goog = goog;
	goog->mtk_google_pcie = mtk_google_pcie;

	get_modem_data_from_dts(mtk_google_pcie);

	ret = setup_aoc_smmu(mtk_google_pcie);
	if (ret)
		LOG_ERR("setup_aoc_smmu failed!\n");

	return ret;
}
EXPORT_SYMBOL_GPL(mtk_google_pcie_init);

void mtk_google_pcie_exit(struct radio_google *goog)
{
	struct mtk_google_pcie *mtk_google_pcie = goog->mtk_google_pcie;

	if (reset_aoc_smmu(mtk_google_pcie))
		LOG_ERR("reset_aoc_smmu failed!\n");

	devm_kfree(goog->mdev->dev, mtk_google_pcie);
}
EXPORT_SYMBOL_GPL(mtk_google_pcie_exit);
