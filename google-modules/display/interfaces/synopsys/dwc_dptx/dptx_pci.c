// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include "dw-devices.h"

#define DPTX_DRV_NAME	"dwc_dptx"
#define ESM_DRV_NAME	"dwc_hdcp22"

#define DPTX_CTRL_MAIN_IRQ			0

#define PROTO_DPTX_BASE_ADDRESS		0x00000000
// SETUP ID
#define PROTO_SETUP_ID_START		0x00000000
#define PROTO_SETUP_ID_END		0x000003FF
// CLOCK MANAGER
#define PROTO_DPTX_CLKMGR_START		0x00001000
#define PROTO_DPTX_CLKMGR_END		0x000013FF
// RESET MANAGER
#define PROTO_DPTX_RSTMGR_DP_START	0x00002000
#define PROTO_DPTX_RSTMGR_DP_END	0x00002007
#define PROTO_DPTX_RSTMGR_HDCP_START	0x00002008
#define PROTO_DPTX_RSTMGR_HDCP_END	0x0000200B
// VIDEOBRIDGE
#define PROTO_DPTX_VG_START		0x00003000
#define PROTO_DPTX_VG_END		0x000033FF
// AUDIO BRIDGE
#define PROTO_DPTX_AG_START		0x00004000
#define PROTO_DPTX_AG_END		0x000043FF
// DPTX CONTROLLER
#define PROTO_DPTX_CTRL_START		0x00010000
#define PROTO_DPTX_CTRL_END		0x00047FFF
// HDCP BLOCK
#define PROTO_DPTX_HDCP_START		0x00050000
#define PROTO_DPTX_HDCP_END		0x000503FF
// PHY INTERFACE
#define PROTO_DPTX_PHYIF_START		0x00057000
#define PROTO_DPTX_PHYIF_END		0x000573FF


static struct dw_driver_conf dw_driver_conf[] = {

	{
		.module_name = DPTX_DRV_NAME,
		.desc = "DPTX - DisplayPort Tx Controller",
		.pdata = NULL,
		.pdata_size = 0,
		.res_count = 8,
		.res = (struct resource[]) {
			{
				.start = PROTO_SETUP_ID_START,
				.end = PROTO_SETUP_ID_END,
				.name = "SETUP_ID",
				.flags = IORESOURCE_MEM,
			}, {
				.start = PROTO_DPTX_CLKMGR_START,
				.end = PROTO_DPTX_CLKMGR_END,
				.name = "CLOCK_MANAGER",
				.flags = IORESOURCE_MEM,
			}, {
				.start = PROTO_DPTX_RSTMGR_DP_START,
				.end = PROTO_DPTX_RSTMGR_DP_END,
				.name = "RESET_MANAGER",
				.flags = IORESOURCE_MEM,
			}, {
				.start = PROTO_DPTX_CTRL_START,
				.end = PROTO_DPTX_CTRL_END,
				.name = "DPTX_CTRL",
				.flags = IORESOURCE_MEM,
			}, {
				.start = PROTO_DPTX_VG_START,
				.end = PROTO_DPTX_VG_END,
				.name = "VIDEO_GENERATOR",
				.flags = IORESOURCE_MEM,
			}, {
				.start = PROTO_DPTX_AG_START,
				.end = PROTO_DPTX_AG_END,
				.name = "AUDIO_GENERATOR",
				.flags = IORESOURCE_MEM,
			}, {
				.start = PROTO_DPTX_PHYIF_START,
				.end = PROTO_DPTX_PHYIF_END,
				.name = "PHY_INTERFACE",
				.flags = IORESOURCE_MEM,
			}, {
				.start = DPTX_CTRL_MAIN_IRQ,
				.end = DPTX_CTRL_MAIN_IRQ,
				.name = "DPTX_MAIN_IRQ",
				.flags = IORESOURCE_IRQ,
			}
		}
	}, {
		.module_name = ESM_DRV_NAME,
		.desc = "ESM - HDCP 2.2 controller",
		.pdata = NULL,
		.pdata_size = 0,
		.res_count = 2,
		.res = (struct resource[]) {
			{
				.start = PROTO_DPTX_HDCP_START,
				.end = PROTO_DPTX_HDCP_END,
				.flags = IORESOURCE_MEM,
			}, {
				.start = PROTO_DPTX_RSTMGR_HDCP_START,
				.end = PROTO_DPTX_RSTMGR_HDCP_END,
				.flags = IORESOURCE_MEM,
			}
		}
	}
};


static int dptx_driver_probe(struct pci_dev *pci,
			     const struct pci_device_id *id)
{
	int retval = 0;
	int max_irqs = 0;
	struct device *dev = &pci->dev;
	int err, i, irq_base = 0;
	struct dw_dev *dw;

	pr_info("DisplayPort TX PCI probe initiated\n");

	/* Data structure allocation */
	dw = devm_kzalloc(dev, sizeof(*dw), GFP_KERNEL);
	if (!dw)
		return -ENOMEM;

	dw->pdev = pci;
	dw->dev = dev;

	/* Enable PCI device */
	err = pcim_enable_device(pci);
	if (err) {
		pci_err(pci, "enabling pci device failed\n");
		return err;
	}

	err = pci_set_dma_mask(pci, DMA_BIT_MASK(32));
	if (err) {
		dev_err(dev, "failed to set DMA mask\n");
		return err;
	}

	err = pci_set_consistent_dma_mask(pci, DMA_BIT_MASK(32));
	if (err) {
		dev_err(dev, "failed to set consistent DMA mask\n");
		return err;
	}

	pci_set_master(pci);

	/* MSI Interrupts */
	max_irqs = pci_msi_vec_count(pci);
	retval = pci_alloc_irq_vectors(pci, 1, max_irqs, PCI_IRQ_ALL_TYPES);
	if (retval < 0) {
		dev_err(dev, "Failed to allocate IRQ vectors\n");
		return retval;
	}

	irq_base = pci_irq_vector(pci, 0);

	/* Sanity check */
	WARN_ON(ARRAY_SIZE(dw_driver_conf) > DW_MAX_DEVICES);

	/* Load modules */
	for (i = 0; i < ARRAY_SIZE(dw_driver_conf); i++) {
		dev_info(dev, "requesting module '%s'\n",
			 dw_driver_conf[i].module_name);
		if (request_module(dw_driver_conf[i].module_name))
			dev_err(dev, "failed to load module '%s'\n",
				dw_driver_conf[i].module_name);
	}

	/* Devices initialization */
	err = dw_devices_register(dw, dw_driver_conf,
				  ARRAY_SIZE(dw_driver_conf),
				  pci_resource_start(pci, 0), irq_base);
	if (err) {
		dev_err(dev, "failed to register devices\n");
		goto unmap;
	}

	pci_set_drvdata(pci, dw);

	pr_info("DisplayPort TX PCI probe finished");

	return 0;

unmap:
	pci_disable_device(pci);
	pci_release_regions(pci);
	return err;
}

static void dptx_driver_remove(struct pci_dev *pci)
{
	struct dptx *dptx;

	dptx = pci_get_drvdata(pci);
	platform_device_unregister(pci_get_drvdata(pci));
	pci_free_irq_vectors(pci);
}

static const struct pci_device_id pci_ids[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS, 0x9001),
	},
	{ },
};

MODULE_DEVICE_TABLE(pci, pci_ids);

#ifdef CONFIG_PM
static int dptx_pci_runtime_suspend(struct device *dev)
{
	return 0;
}

static int dptx_pci_runtime_resume(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP
static int dptx_pci_suspend(struct device *dev)
{
	return 0;
}

static int dptx_pci_resume(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops dptx_pci_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(dptx_pci_suspend, dptx_pci_resume)
	SET_RUNTIME_PM_OPS(dptx_pci_runtime_suspend, dptx_pci_runtime_resume, NULL)
};

static struct pci_driver dptx_driver = {
	.name = "dwc_dptx_pci",
	.id_table = pci_ids,
	.probe = dptx_driver_probe,
	.remove = dptx_driver_remove,
	.driver = {
		.pm = &dptx_pci_dev_pm_ops,
	}
};

static int __init dptx_driver_init(void)
{
	int retval = 0;

	retval = pci_register_driver(&dptx_driver);

	if (retval < 0)
		return retval;

	return retval;
}

module_init(dptx_driver_init);

static void __exit dptx_driver_cleanup(void)
{
	pci_unregister_driver(&dptx_driver);
}

module_exit(dptx_driver_cleanup);

MODULE_AUTHOR("Synopsys Inc.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Synopsys DesignWare DisplayPort TX Driver - PCI Layer");
