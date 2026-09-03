// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 *
 */

#include "core_pmic_defs.h"
#include "s2mpg1x.h"

#define USB_ENABLE_MASK BIT(0)

int pmic_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value)
{
	switch (pmic) {
	case CORE_PMIC_SUB_I2C:
		return core_pmic_sub_write_register(bcl_dev->sub_i2c, reg, value);
	case CORE_PMIC_MAIN_I2C:
		return core_pmic_main_write_register(bcl_dev->main_i2c, reg, value);
	case CORE_PMIC_SUB:
		return core_pmic_sub_write_register(bcl_dev->sub_pmic_i2c, reg, value);
	case CORE_PMIC_MAIN:
		return core_pmic_main_write_register(bcl_dev->main_pmic_i2c, reg, value);
	case CORE_PMIC_MAIN_RTC:
		return core_pmic_main_write_register(bcl_dev->main_rtc_i2c, reg,
						     value);
	}
	return 0;
}

int pmic_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value)
{
	switch (pmic) {
	case CORE_PMIC_SUB_I2C:
		return core_pmic_sub_read_register(bcl_dev->sub_i2c, reg, value);
	case CORE_PMIC_MAIN_I2C:
		return core_pmic_main_read_register(bcl_dev->main_i2c, reg, value);
	case CORE_PMIC_SUB:
		return core_pmic_sub_read_register(bcl_dev->sub_pmic_i2c, reg, value);
	case CORE_PMIC_MAIN:
		return core_pmic_main_read_register(bcl_dev->main_pmic_i2c, reg, value);
	case CORE_PMIC_MAIN_RTC:
		return core_pmic_main_read_register(bcl_dev->main_rtc_i2c, reg,
						    value);
	}
	return 0;
}

int read_uvlo_dur(struct bcl_device *bcl_dev, uint64_t *data) { return 0; }
int read_pre_uvlo_hit_cnt(struct bcl_device *bcl_dev, uint16_t *data, int pmic) { return 0; }
int read_pre_ocp_bckup(struct bcl_device *bcl_dev, int *pre_ocp_bckup, int rail) { return 0; }
int read_odpm_int_bckup(struct bcl_device *bcl_dev, int *odpm_int_bckup, u16 *type, int pmic,
			int channel)
{
	return 0;
}

int core_pmic_get_scratch_pad(struct bcl_device *bcl_dev, u8 *value)
{
	u8 rtc_scratch_reg, ret_value;
	int ret;

	ret = get_rtc_scratch1_register(&rtc_scratch_reg);
	if (ret)
		return ret;

	ret = pmic_read(CORE_PMIC_MAIN_RTC, bcl_dev, rtc_scratch_reg,
			&ret_value);
	if (ret)
		return ret;

	*value = ret_value;
	return 0;
}

void core_pmic_set_scratch_pad(struct bcl_device *bcl_dev, u8 value)
{
	u8 rtc_scratch_reg;
	int ret;

	ret = get_rtc_scratch1_register(&rtc_scratch_reg);
	if (ret)
		return;

	pmic_write(CORE_PMIC_MAIN_RTC, bcl_dev, rtc_scratch_reg, value);
}

int get_scratch_value(struct bcl_device *bcl_dev, bool is_usb_plugged, u8 *value)
{
	u8 cur_value;
	int ret;

	ret = core_pmic_get_scratch_pad(bcl_dev, &cur_value);
	if (ret)
		return ret;

	*value = is_usb_plugged ? cur_value | USB_ENABLE_MASK :
				  cur_value & ~USB_ENABLE_MASK;
	return 0;
}

void core_pmic_bcl_init_bbat(struct bcl_device *bcl_dev) {}
void core_pmic_teardown(struct bcl_device *bcl_dev) {}
int core_pmic_mbox_request(struct bcl_device *bcl_dev) { return 0; }
uint32_t core_pmic_get_cpm_cached_sys_evt(struct bcl_device *bcl_dev) { return 0; }
