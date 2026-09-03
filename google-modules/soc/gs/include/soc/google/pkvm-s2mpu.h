/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Samsung Electronics Co., Ltd.
 */

#ifndef __PKVM_S2MPU_H
#define __PKVM_S2MPU_H

#include <asm/kvm_host.h>

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/irqreturn.h>

struct s2mpu_data {
	struct device *dev;
	void __iomem *base;
	bool pkvm_registered;
	bool always_on;
	bool has_sysmmu;
	bool has_pd;
	bool pm_ref;
	pkvm_handle_t id;
};

/*
 * Parse the 's2mpus' DT property of 'parent' and create a device link
 * to all referenced S2MPU devices.
 */
int __pkvm_s2mpu_of_link(struct device *parent);
int pkvm_s2mpu_of_link(struct device *parent);
int pkvm_s2mpu_of_link_v9(struct device *parent);

int pkvm_iommu_s2mpu_init(u64 token);

static inline bool pkvm_s2mpu_ready(struct device *dev)
{
	return !!platform_get_drvdata(to_platform_device(dev));
}

irqreturn_t s2mpu_fault_handler(struct s2mpu_data *data, bool print_caches);

#endif	/* __PKVM_S2MPU_H */
