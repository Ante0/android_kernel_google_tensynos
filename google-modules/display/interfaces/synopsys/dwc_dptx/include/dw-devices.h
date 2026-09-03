/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __DW_DEVICES_H__
#define __DW_DEVICES_H__

#define DW_MAX_DEVICES		4

struct dw_dev {
	struct pci_dev		*pdev;
	struct device		*dev;
	unsigned int		devices_cnt;
	struct platform_device	*devices[DW_MAX_DEVICES];
};

struct dw_driver_conf {
	char		*module_name;
	char		*desc;
	void		*pdata;
	size_t		pdata_size;
	int		res_count;
	struct resource	*res;
};

int dw_devices_register(struct dw_dev *dw_dev,
			struct dw_driver_conf *driver_conf, int driver_count,
			resource_size_t mem_base, int irq_base);
void dw_devices_unregister(struct dw_dev *dw_dev);

#endif /* __DW_DEVICES_H__ */

