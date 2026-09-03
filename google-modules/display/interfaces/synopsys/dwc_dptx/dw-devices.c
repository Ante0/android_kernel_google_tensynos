// SPDX-License-Identifier: GPL-2.0
/*
 * Synopsys Designware generic devices handler for PCI
 *
 * Copyright (C) 2016 Synopsys, Inc.
 * Jose Abreu <joabreu@synopsys.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

#include "dw-devices.h"

static void dw_devices_set_res_offset(struct dw_driver_conf *driver_conf,
				      resource_size_t mem_base, int irq_base)
{
	int i;

	for (i = 0; i < driver_conf->res_count; i++) {
		struct resource *res = &driver_conf->res[i];

		if (res->flags == IORESOURCE_MEM) {
			res->start += mem_base;
			res->end += mem_base;
		} else if (res->flags == IORESOURCE_IRQ) {
			res->start += irq_base;
			res->end += irq_base;
		}
	}
}

int dw_devices_register(struct dw_dev *dw_dev,
			struct dw_driver_conf *driver_conf, int driver_count,
			resource_size_t mem_base, int irq_base)
{
	struct platform_device *pdev;
	int i, ret;

	for (i = 0; i < driver_count; i++) {
		struct dw_driver_conf *conf = &driver_conf[i];
		struct platform_device_info pdev_info = {
			.parent = dw_dev->dev,
			.name = conf->module_name,
			.id = PLATFORM_DEVID_NONE,
			.res = conf->res,
			.num_res = conf->res_count,
			.data = conf->pdata,
			.size_data = conf->pdata_size,
			.dma_mask = DMA_BIT_MASK(32),
		};

		dev_dbg(dw_dev->dev, "adding device '%s' (%s)\n",
			conf->module_name, conf->desc);

		/* Set memory/irq offset */
		dw_devices_set_res_offset(conf, mem_base, irq_base);

		/* Add device */
		pdev = platform_device_register_full(&pdev_info);
		if (IS_ERR(pdev)) {
			dev_err(dw_dev->dev,
				"failed to register device '%s' (%s)\n",
				conf->module_name, conf->desc);
			ret = PTR_ERR(pdev);
			goto fail_devices;
		}

		dw_dev->devices[dw_dev->devices_cnt++] = pdev;
		dev_info(dw_dev->dev, "added device '%s' (%s)\n",
			 conf->module_name, conf->desc);
		if (dw_dev->devices_cnt >= DW_MAX_DEVICES)
			break;
	}

	return 0;

fail_devices:
	while (--i >= 0)
		platform_device_unregister(dw_dev->devices[i]);
	return ret;
}

void dw_devices_unregister(struct dw_dev *dw_dev)
{
	int i;

	for (i = dw_dev->devices_cnt - 1; i >= 0; i--) {
		if (dw_dev->devices[i]) {
			dev_info(dw_dev->dev, "removing device '%s'\n",
				 dw_dev->devices[i]->name);
			platform_device_unregister(dw_dev->devices[i]);
		}
	}
}
