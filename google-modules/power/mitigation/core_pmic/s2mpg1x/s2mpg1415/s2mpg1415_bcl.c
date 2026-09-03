// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 *
 */

#include <linux/mfd/samsung/s2mpg1415.h>
#include <linux/mfd/samsung/s2mpg14-register.h>
#include <linux/mfd/samsung/s2mpg15-register.h>
#include <linux/mfd/samsung/s2mpg1415-register.h>
#include <soc/google/odpm.h>
#include <linux/pinctrl/consumer.h>

#include "core_pmic_defs.h"
#include "s2mpg1415_bcl.h"
#include "s2mpg1x.h"

#define MAIN_METER_PWR_WARN0 S2MPG14_METER_PWR_WARN0
#define SUB_METER_PWR_WARN0 S2MPG15_METER_PWR_WARN0

int meter_write(int pmic, struct bcl_device *bcl_dev, int idx, u8 value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return core_pmic_sub_write_register(bcl_dev->sub_meter_i2c,
						    SUB_METER_PWR_WARN0 + idx,
						    value);
	case CORE_PMIC_MAIN:
		return core_pmic_main_write_register(bcl_dev->main_meter_i2c,
						     MAIN_METER_PWR_WARN0 + idx,
						     value);
	}
	return 0;
}

int meter_read(int pmic, struct bcl_device *bcl_dev, int idx, u8 *value)
{
	switch (pmic) {
	case CORE_PMIC_SUB:
		return core_pmic_sub_read_register(bcl_dev->sub_meter_i2c,
						   SUB_METER_PWR_WARN0 + idx,
						   value);
	case CORE_PMIC_MAIN:
		return core_pmic_main_read_register(bcl_dev->main_meter_i2c,
						    MAIN_METER_PWR_WARN0 + idx,
						    value);
	}
	return 0;
}

int google_bcl_configure_modem(struct bcl_device *bcl_dev)
{
	struct pinctrl *modem_pinctrl;
	struct pinctrl_state *batoilo_pinctrl_state, *rffe_pinctrl_state;
	int ret;

	modem_pinctrl = devm_pinctrl_get(bcl_dev->device);
	if (IS_ERR_OR_NULL(modem_pinctrl)) {
		dev_err(bcl_dev->device, "Cannot find modem_pinctrl!\n");
		return -EINVAL;
	}
	batoilo_pinctrl_state =
		pinctrl_lookup_state(modem_pinctrl, "bcl-batoilo-modem");
	if (IS_ERR_OR_NULL(batoilo_pinctrl_state)) {
		dev_err(bcl_dev->device,
			"batoilo: pinctrl lookup state failed!\n");
		return -EINVAL;
	}
	rffe_pinctrl_state =
		pinctrl_lookup_state(modem_pinctrl, "bcl-rffe-modem");
	if (IS_ERR_OR_NULL(rffe_pinctrl_state)) {
		dev_err(bcl_dev->device,
			"rffe: pinctrl lookup state failed!\n");
		return -EINVAL;
	}
	ret = pinctrl_select_state(modem_pinctrl, batoilo_pinctrl_state);
	if (ret < 0) {
		dev_err(bcl_dev->device,
			"batoilo: pinctrl select state failed!\n");
		return -EINVAL;
	}
	ret = pinctrl_select_state(modem_pinctrl, rffe_pinctrl_state);
	if (ret < 0) {
		dev_err(bcl_dev->device,
			"rffe: pinctrl select state failed!\n");
		return -EINVAL;
	}
	bcl_dev->config_modem = true;
	return 0;
}

u64 settings_to_current(struct bcl_device *bcl_dev, int pmic, int idx,
			u32 setting)
{
	int rail_i;
	enum s2mpg1415_meter_muxsel muxsel;
	struct odpm_info *info;
	u64 raw_unit;
	u32 resolution;

	setting = setting << LPF_CURRENT_SHIFT;

	if (pmic == CORE_PMIC_MAIN)
		info = bcl_dev->main_odpm;
	else
		info = bcl_dev->sub_odpm;

	if (!info)
		return 0;

	rail_i = info->channels[idx].rail_i;
	muxsel = info->chip.rails[rail_i].mux_select;
	if (pmic == CORE_PMIC_MAIN) {
		if (strstr(bcl_dev->main_rail_names[idx], "VSYS") != NULL)
			resolution = (u32)VSHUNT_MULTIPLIER *
				     ((u64)EXTERNAL_RESOLUTION_VSHUNT) /
				     info->chip.rails[rail_i].shunt_uohms;
		else
			resolution =
				s2mpg14_muxsel_to_current_resolution(muxsel);
	} else {
		if (strstr(bcl_dev->sub_rail_names[idx], "VSYS") != NULL)
			resolution = (u32)VSHUNT_MULTIPLIER *
				     ((u64)EXTERNAL_RESOLUTION_VSHUNT) /
				     info->chip.rails[rail_i].shunt_uohms;
		else
			resolution =
				s2mpg15_muxsel_to_current_resolution(muxsel);
	}
	raw_unit = (u64)setting * resolution;
	raw_unit = raw_unit * MILLI_TO_MICRO;
	return (u32)_IQ30_to_int(raw_unit);
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
