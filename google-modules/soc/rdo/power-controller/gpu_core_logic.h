/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2023 Google LLC */

#ifndef GPU_CORE_LOGIC_H
#define GPU_CORE_LOGIC_H

#include <linux/device.h>
#include <linux/of.h>

enum gpu_dts_version {
	GPU_DTS_VERSION_1 = 0,
	GPU_DTS_VERSION_2,
	GPU_DTS_VERSION_3,
	GPU_DTS_VERSION_NUM,
};

struct gpu_core_logic_pd {
	void __iomem *gpu_control_reg;
	void __iomem *pvtc_reg;
	void __iomem *pvtc_pwroff_reg;
	void __iomem *dts_reg;
	enum gpu_dts_version gpu_dts_version;
	u32 gpu_control_ack_timeout_us;
	u32 gpu_control_val;
};

int gpu_core_logic_on(struct device *dev, struct gpu_core_logic_pd *pd);
int gpu_core_logic_off(struct device *dev, struct gpu_core_logic_pd *pd);
int gpu_core_logic_init(struct device *dev, struct device_node *np, struct gpu_core_logic_pd *pd);

#endif /* GPU_CORE_LOGIC_H */
