// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 *
 */

#include <linux/mfd/samsung/s2mpg12-register.h>
#include <linux/mfd/samsung/s2mpg13-register.h>

#include "core_pmic_defs.h"
#include "s2mpg1213_bcl.h"

int get_throttle_lvl_addr(int id)
{
	if (id == OCP_WARN_CPUCL1)
		return CPU1_OCP_WARN;
	else if (id == OCP_WARN_CPUCL2)
		return CPU2_OCP_WARN;
	else if (id == SOFT_OCP_WARN_CPUCL1)
		return SOFT_CPU1_OCP_WARN;
	else if (id == SOFT_OCP_WARN_CPUCL2)
		return SOFT_CPU2_OCP_WARN;
	else if (id == OCP_WARN_TPU)
		return TPU_OCP_WARN;
	else if (id == SOFT_OCP_WARN_TPU)
		return SOFT_TPU_OCP_WARN;
	else if (id == OCP_WARN_GPU)
		return GPU_OCP_WARN;
	else if (id == SOFT_OCP_WARN_GPU)
		return SOFT_GPU_OCP_WARN;
	else
		return -EINVAL;
}

int register_zone(struct bcl_device *bcl_dev, int idx, const char *devname,
		  struct gpio_desc *pin, int irq, int type, int irq_config,
		  int polarity, u32 flag)
{
	int ret;

	ret = google_bcl_register_zone(bcl_dev, idx, devname, pin, irq, type,
				       irq_config, polarity, flag);
	if (ret < 0)
		return ret;

	bcl_dev->zone[idx]->throttle_lvl_addr = get_throttle_lvl_addr(idx);

	return 0;
}

int google_bcl_configure_modem(struct bcl_device *bcl_dev)
{
	return 0;
}

void compute_odpm_lpf(struct bcl_device *bcl_dev,
				struct timespec64 triggered_time,
				struct bcl_mitigation_conf *mitigation_conf,
				struct odpm_lpf *odpm_lpf,
				struct max_odpm_lpf *max_odpm_lpf)
{
	int i;
	u32 odpm_lpf_value, odpm_lpf_thres;

	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		odpm_lpf_value = odpm_lpf->value[i];
		odpm_lpf_thres = mitigation_conf[i].threshold;
		if (odpm_lpf_value >= odpm_lpf_thres) {
			/* Compute mitigation modules */
			atomic_or(BIT(mitigation_conf[i].module_id),
					  &bcl_dev->mitigation_module_ids);

			if (odpm_lpf_value >= odpm_lpf_thres * 3)
				max_odpm_lpf[i].count_lvl_2++;
			else if (odpm_lpf_value >= odpm_lpf_thres * 2)
				max_odpm_lpf[i].count_lvl_1++;
			else
				max_odpm_lpf[i].count_lvl_0++;
		}
		if (odpm_lpf_value >= max_odpm_lpf[i].value) {
			max_odpm_lpf[i].time = triggered_time;
			max_odpm_lpf[i].value = odpm_lpf_value;
			max_odpm_lpf[i].triggered_idx = bcl_dev->br_stats->triggered_idx;
		}

	}
}

int meter_write(int pmic, struct bcl_device *bcl_dev, int idx, u8 value)
{
	return 0;
}
int meter_read(int pmic, struct bcl_device *bcl_dev, int idx, u8 *value)
{
	return 0;
}
