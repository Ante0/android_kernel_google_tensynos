// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/firmware.h>
#include <linux/mfd/syscon.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "pcie-google.h"

/* PCIe top region registers */
#define PCIE_PWR_UP_RESET_REG   0x20

/* C20PCIe4_PHY regions */
#define PCIE_PHY_GEN_CTRL_3	0x18
#define PHY0_SRAM_EXT_LD_DONE	BIT(4)
#define PHY0_SRAM_INIT_DONE	BIT(7)

#define SRAM_INIT_DELAY_US	100
#define SRAM_INIT_TIMEOUT_US	1000

struct google_pcie_phy {
	struct device *dev;
	void __iomem *top_base;
	void __iomem *phy_base;
	void __iomem *phy_sram_base;
	size_t phy_sram_size;
	u32 state;
	struct phy *phy;
	const struct firmware *fw;
};

static int google_pcie_patch_phy_fw(struct google_pcie_phy *gphy)
{
	int i, ret;
	u32 val;
	u32 size;

	ret = readl_poll_timeout(gphy->phy_base + PCIE_PHY_GEN_CTRL_3,
				 val, (val & PHY0_SRAM_INIT_DONE),
				 SRAM_INIT_DELAY_US, SRAM_INIT_TIMEOUT_US);
	if (ret)
		return ret;

	val = readl(gphy->phy_base + PCIE_PHY_GEN_CTRL_3);
	if (val & PHY0_SRAM_EXT_LD_DONE)
		return 0;

	if (gphy->phy_sram_base) {
		const struct fw_patch_entry *phy_fw_patch;
		size_t phy_fw_patch_size;

		phy_fw_patch = (const struct fw_patch_entry *)gphy->fw->data;
		phy_fw_patch_size = gphy->fw->size / sizeof(struct fw_patch_entry);

		size = (phy_fw_patch_size < (gphy->phy_sram_size / 4)) ?
			phy_fw_patch_size : (gphy->phy_sram_size / 4);

		for (i = 0; i < size; i++) {
			u32 val = le16_to_cpu(phy_fw_patch[i].val);
			u32 addr = le32_to_cpu(phy_fw_patch[i].addr);

			if (addr + sizeof(val) > gphy->phy_sram_size) {
				dev_err(gphy->dev, "PHY FW address out of range: %#x/%#zx\n",
					addr, gphy->phy_sram_size);
				return -EFAULT;
			}
			writel_relaxed(val, gphy->phy_sram_base + addr);
		}
	}

	/* Lane 0 CDR CTL3 */
	writel_relaxed(0x080f, gphy->phy_sram_base + 0xc004);
	writel_relaxed(0x0038, gphy->phy_sram_base + 0xc008);
	writel_relaxed(0x02f8, gphy->phy_sram_base + 0xc00c);
	writel_relaxed(0x0004, gphy->phy_sram_base + 0xc010);

	/* Lane 0 CDR CTL4 */
	writel_relaxed(0x080f, gphy->phy_sram_base + 0xc014);
	writel_relaxed(0x0007, gphy->phy_sram_base + 0xc018);
	writel_relaxed(0x02f9, gphy->phy_sram_base + 0xc01c);
	writel_relaxed(0x0004, gphy->phy_sram_base + 0xc020);

	writel_relaxed(0x9016, gphy->phy_sram_base + 0xc024);

	/* Lane 1 CDR CTL3 */
	writel_relaxed(0x080f, gphy->phy_sram_base + 0xc104);
	writel_relaxed(0x0038, gphy->phy_sram_base + 0xc108);
	writel_relaxed(0x02f8, gphy->phy_sram_base + 0xc10c);
	writel_relaxed(0x0004, gphy->phy_sram_base + 0xc110);

	/* Lane 1 CDR CTL4 */
	writel_relaxed(0x080f, gphy->phy_sram_base + 0xc114);
	writel_relaxed(0x0007, gphy->phy_sram_base + 0xc118);
	writel_relaxed(0x02f9, gphy->phy_sram_base + 0xc11c);
	writel_relaxed(0x0004, gphy->phy_sram_base + 0xc120);

	writel_relaxed(0x9016, gphy->phy_sram_base + 0xc124);

	/* BG_OVRD_OUT */
	writel_relaxed(0x080f, gphy->phy_sram_base + 0xc404);
	writel_relaxed(0x000c, gphy->phy_sram_base + 0xc408);
	writel_relaxed(0x00c4, gphy->phy_sram_base + 0xc40c);
	writel_relaxed(0x0003, gphy->phy_sram_base + 0xc410);

	writel_relaxed(0x9016, gphy->phy_sram_base + 0xc414);

	/* Ensure all writes are completed before setting the done bit */
	wmb(); /* Make sure all SRAM writes are visible */

	val = readl(gphy->phy_base + PCIE_PHY_GEN_CTRL_3);
	val |= PHY0_SRAM_EXT_LD_DONE;
	writel(val, gphy->phy_base + PCIE_PHY_GEN_CTRL_3);

	return 0;
}

static int google_pcie_phy_power_on(struct phy *phy)
{
	int ret;
	struct google_pcie_phy *gphy = phy_get_drvdata(phy);

	ret = google_pcie_patch_phy_fw(gphy);
	if (ret)
		return ret;

	return 0;
}

static int google_pcie_phy_power_off(struct phy *phy)
{
	return 0;
}

static const struct phy_ops google_pcie_phy_ops = {
	.power_on = google_pcie_phy_power_on,
	.power_off = google_pcie_phy_power_off,
	.owner = THIS_MODULE
};

static int google_pcie_phy_probe(struct platform_device *pdev)
{
	struct google_pcie_phy *gphy;
	struct resource *phy_sram_res;
	struct device *dev = &pdev->dev;
	int ret;

	gphy = devm_kzalloc(dev, sizeof(*gphy), GFP_KERNEL);
	if (!gphy)
		return -ENOMEM;

	gphy->dev = dev;
	dev_set_drvdata(dev, gphy);

	phy_sram_res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy_sram");
	if (phy_sram_res) {
		gphy->phy_sram_base = devm_ioremap_resource(dev, phy_sram_res);
		if (IS_ERR(gphy->phy_sram_base))
			return PTR_ERR(gphy->phy_sram_base);

		gphy->phy_sram_size = resource_size(phy_sram_res);

		ret = request_firmware(&gphy->fw, "malibu_pcie_phy_fw.bin", gphy->dev);
		if (ret < 0) {
			dev_err(gphy->dev, "Failed to get PHY firmware: %d\n", ret);
			return ret;
		}
	}

	gphy->top_base = devm_platform_ioremap_resource_byname(pdev, "top");
	if (IS_ERR(gphy->top_base))
		return PTR_ERR(gphy->top_base);

	gphy->phy_base = devm_platform_ioremap_resource_byname(pdev, "phy");
	if (IS_ERR(gphy->phy_base))
		return PTR_ERR(gphy->phy_base);

	pm_runtime_enable(gphy->dev);

	gphy->phy = devm_phy_create(dev, NULL, &google_pcie_phy_ops);
	if (IS_ERR(gphy->phy)) {
		dev_err(dev, "Couldn't create PCIe phy err: %ld\n", PTR_ERR(gphy->phy));
		goto pm_disable;
	}
	phy_set_drvdata(gphy->phy, gphy);

	devm_of_phy_provider_register(dev, of_phy_simple_xlate);

	dev_dbg(dev, "PCIe Phy Probed\n");
	return 0;
pm_disable:
	/* Ensure that PCIe Top registers are accessible */
	pm_runtime_get_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);
	return PTR_ERR(gphy->phy);
}

static void google_pcie_phy_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_pcie_phy *gphy = dev_get_drvdata(dev);

	/* Make sure PCIe Top registers are accessible */
	pm_runtime_get_sync(dev);
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_set_suspended(dev);

	if (gphy->fw)
		release_firmware(gphy->fw);
}

static const struct of_device_id google_pcie_phy_match[] = {
	{
		.compatible = "google,pcie-phy",
	},
	{},
};
MODULE_DEVICE_TABLE(of, google_pcie_phy_match);

static struct platform_driver google_pcie_phy_driver = {
	.driver = {
		.name = "google-pcie-phy",
		.of_match_table = google_pcie_phy_match,
	},
	.probe = google_pcie_phy_probe,
	.remove = google_pcie_phy_remove,
};

module_platform_driver(google_pcie_phy_driver);
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google PCIe Phy Driver");
MODULE_LICENSE("GPL");
