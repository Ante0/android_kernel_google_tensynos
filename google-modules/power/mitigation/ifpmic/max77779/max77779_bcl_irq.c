// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 *
 */

#include <kunit/static_stub.h>
#include <kunit/visibility.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <bcl.h>
#include <max77779.h>
#include <max77779_regs.h>
#include "max77779_bcl_irq.h"

#define OILO_STEP 200
#define OILO_LVL_OFFSET 1

#define OILO1_LOWER_LIMIT 2000
#define OILO1_UPPER_LIMIT 8000

#define OILO2_LOWER_LIMIT 4000
#define OILO2_UPPER_LIMIT 10000

VISIBLE_IF_KUNIT int max77779_external_pmic_reg_read_helper(struct device *dev,
							    uint8_t reg,
							    uint8_t *val)
{
	KUNIT_STATIC_STUB_REDIRECT(max77779_external_pmic_reg_read_helper, dev,
				   reg, val);
	return max77779_external_pmic_reg_read(dev, reg, val);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_external_pmic_reg_read_helper);

VISIBLE_IF_KUNIT int max77779_external_pmic_reg_write_helper(struct device *dev,
							     uint8_t reg,
							     uint8_t val)
{
	KUNIT_STATIC_STUB_REDIRECT(max77779_external_pmic_reg_write_helper, dev,
				   reg, val);
	return max77779_external_pmic_reg_write(dev, reg, val);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_external_pmic_reg_write_helper);

VISIBLE_IF_KUNIT int max77779_external_chg_reg_read_helper(struct device *dev,
							   uint8_t reg,
							   uint8_t *val)
{
	KUNIT_STATIC_STUB_REDIRECT(max77779_external_chg_reg_read_helper, dev,
				   reg, val);
	return max77779_external_chg_reg_read(dev, reg, val);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_external_chg_reg_read_helper);

VISIBLE_IF_KUNIT int max77779_external_chg_reg_write_helper(struct device *dev,
							    uint8_t reg,
							    uint8_t val)
{
	KUNIT_STATIC_STUB_REDIRECT(max77779_external_chg_reg_write_helper, dev,
				   reg, val);
	return max77779_external_chg_reg_write(dev, reg, val);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_external_chg_reg_write_helper);

VISIBLE_IF_KUNIT int max77779_external_vimon_read_buffer_helper(struct device *dev,
								uint16_t *buff,
								size_t *count,
								size_t buff_max)
{
	KUNIT_STATIC_STUB_REDIRECT(max77779_external_vimon_read_buffer_helper, dev,
				   buff, count, buff_max);
	return max77779_external_vimon_read_buffer(dev, buff, count, buff_max);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_external_vimon_read_buffer_helper);

int max77779_get_irq(struct device *ifpmic_irq_dev, int *idx)
{
	u8 vdroop_int;
	u8 ret;
	const u8 clr_bcl_irq_mask =
		(MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK |
		 MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK |
		 MAX77779_PMIC_VDROOP_INT_SYS_UVLO1_INT_MASK |
		 MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK);
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	ret = max77779_external_pmic_reg_read_helper(
		data->pmic_dev, MAX77779_PMIC_VDROOP_INT, &vdroop_int);
	if (ret < 0)
		return IRQ_NONE;
	if (!(vdroop_int & clr_bcl_irq_mask))
		return IRQ_NONE;

	/* UVLO2 has the highest priority and then BATOILO, then UVLO1 */
	if (vdroop_int & MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK)
		*idx = UVLO2;
	else if (vdroop_int & MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK)
		*idx = BATOILO2;
	else if (vdroop_int & MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK)
		*idx = BATOILO1;
	else if (vdroop_int & MAX77779_PMIC_VDROOP_INT_SYS_UVLO1_INT_MASK)
		*idx = UVLO1;

	return ret;
}
EXPORT_SYMBOL_GPL(max77779_get_irq);

int max77779_clr_irq(struct device *ifpmic_irq_dev, int idx)
{
	u8 chg_int = 0;
	int ret;
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	if (idx == UVLO1)
		chg_int = MAX77779_PMIC_VDROOP_INT_SYS_UVLO1_INT_MASK;
	else if (idx == BATOILO1 || idx == BATOILO2 || idx == UVLO2)
		chg_int = MAX77779_PMIC_VDROOP_INT_SYS_UVLO2_INT_MASK |
			  MAX77779_PMIC_VDROOP_INT_BAT_OILO1_INT_MASK |
			  MAX77779_PMIC_VDROOP_INT_BAT_OILO2_INT_MASK;

	ret = max77779_external_pmic_reg_write_helper(
		data->pmic_dev, MAX77779_PMIC_VDROOP_INT, chg_int);
	if (ret < 0)
		return IRQ_NONE;
	return ret;
}
EXPORT_SYMBOL_GPL(max77779_clr_irq);

/**
 * max77779_vimon_read() - Read VIMON data.
 * @ifpmic_irq_dev: IFPMIC IRQ device.
 *
 * This function reads VIMON data from the vimon device and copies it to the
 * bcl device's vimon interface.
 *
 * Return: The number of bytes read on success, or an error code.
 */
VISIBLE_IF_KUNIT int max77779_vimon_read(struct device *ifpmic_irq_dev)
{
	int ret;
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	ret = max77779_external_vimon_read_buffer_helper(data->vimon_dev,
						  data->vimon_data,
						  &data->vimon_count,
						  VIMON_BUF_SIZE);

	if (ret < 0)
		return ret;

	memcpy(data->bcl_dev->vimon_intf.data, data->vimon_data,
	       data->vimon_count);

	return data->vimon_count;
}
EXPORT_SYMBOL_IF_KUNIT(max77779_vimon_read);

static int max77779_get_raw_sts(struct device *ifpmic_irq_dev, int idx)
{
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (idx) {
	case UVLO1:
		if (!data->vd1_gpio)
			return -ENODEV;
		return gpiod_get_raw_value(data->vd1_gpio);
	case UVLO2:
	case BATOILO2:
		if (!data->vd2_gpio)
			return -ENODEV;
		return gpiod_get_raw_value(data->vd2_gpio);
	default:
		return -EINVAL;
	}
}

static irqreturn_t max77779_bcl_vdroop_handler(int irq, void *ptr)
{
	int ret;
	struct max77779_irq_context *irq_data = ptr;
	struct max77779_bcl_irq_data *data = irq_data->parent;

	ret = google_bcl_mitigation_trigger(irq_data->idx, data->bcl_dev);
	if (ret < 0)
		dev_err(data->dev, "Mitigation driver idx %d err (%d)",
			irq_data->idx, ret);

	/* IRQ clearing handled by bcl_core */

	return IRQ_HANDLED;
}

static irqreturn_t max77779_bcl_irqb_handler(int irq, void *ptr)
{
	int ret;
	struct max77779_irq_context *irq_data = ptr;
	struct max77779_bcl_irq_data *data = irq_data->parent;

	max77779_clr_irq(data->dev, BATOILO);

	ret = google_bcl_mitigation_trigger(irq_data->idx, data->bcl_dev);
	if (ret < 0)
		dev_err(data->dev, "Mitigation driver idx %d err (%d)",
			irq_data->idx, ret);

	return IRQ_HANDLED;
}

enum OILO_TYPE {
	OILO1,
	OILO2,
};

static int set_oilo_helper(struct device *ifpmic_irq_dev, int val,
			   enum OILO_TYPE type)
{
	u8 reg;
	int lower, upper, addr;
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (type) {
	case OILO1:
		lower = OILO1_LOWER_LIMIT;
		upper = OILO1_UPPER_LIMIT;
		addr = MAX77779_BAT_OILO1_CNFG_0;
		break;
	case OILO2:
		lower = OILO2_LOWER_LIMIT;
		upper = OILO2_UPPER_LIMIT;
		addr = MAX77779_BAT_OILO2_CNFG_0;
		break;
	default:
		return -EINVAL;
	}

	if (val < lower || val > upper)
		return -EINVAL;

	reg = OILO_LVL_OFFSET + (val - lower) / OILO_STEP;
	return max77779_external_chg_reg_write_helper(data->chg_dev, addr, reg);
}

static int get_oilo_helper(struct device *ifpmic_irq_dev, int *val,
			   enum OILO_TYPE type)
{
	u8 reg;
	int ret, lower, upper, addr;
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (type) {
	case OILO1:
		lower = OILO1_LOWER_LIMIT;
		upper = OILO1_UPPER_LIMIT;
		addr = MAX77779_BAT_OILO1_CNFG_0;
		break;
	case OILO2:
		lower = OILO2_LOWER_LIMIT;
		upper = OILO2_UPPER_LIMIT;
		addr = MAX77779_BAT_OILO2_CNFG_0;
		break;
	default:
		return -EINVAL;
	}

	ret = max77779_external_chg_reg_read_helper(data->chg_dev, addr, &reg);

	if (ret < 0)
		return ret;

	if (reg == 0)
		*val = 0;
	else
		*val = (OILO_STEP * (reg - OILO_LVL_OFFSET)) + lower;
	return 0;
}

VISIBLE_IF_KUNIT int max77779_set_oilo1(struct device *ifpmic_irq_dev, int val)
{
	return set_oilo_helper(ifpmic_irq_dev, val, OILO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_oilo1);

VISIBLE_IF_KUNIT int max77779_get_oilo1(struct device *ifpmic_irq_dev, int *val)
{
	return get_oilo_helper(ifpmic_irq_dev, val, OILO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_get_oilo1);

VISIBLE_IF_KUNIT int max77779_set_oilo2(struct device *ifpmic_irq_dev, int val)
{
	return set_oilo_helper(ifpmic_irq_dev, val, OILO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_oilo2);

VISIBLE_IF_KUNIT int max77779_get_oilo2(struct device *ifpmic_irq_dev, int *val)
{
	return get_oilo_helper(ifpmic_irq_dev, val, OILO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_get_oilo2);

static int set_uvlo_helper(struct device *ifpmic_irq_dev, int val, u8 type)
{
	int ret;
	u8 regval;
	int addr;
	uint8_t (*max77779_sys_uvlo_set)(uint8_t regval, uint8_t val);
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	if (val < VD_LOWER_LIMIT || val > VD_UPPER_LIMIT)
		return -EINVAL;

	switch (type) {
	case UVLO1:
		addr = MAX77779_SYS_UVLO1_CNFG_0;
		max77779_sys_uvlo_set =
			_max77779_sys_uvlo1_cnfg_0_sys_uvlo1_set;
		break;
	case UVLO2:
		addr = MAX77779_SYS_UVLO2_CNFG_0;
		max77779_sys_uvlo_set =
			_max77779_sys_uvlo2_cnfg_0_sys_uvlo2_set;
		break;
	default:
		return -EINVAL;
	}

	ret = max77779_external_chg_reg_read_helper(data->chg_dev, addr,
						    &regval);

	if (ret < 0)
		return ret;

	val = (val - VD_LOWER_LIMIT) / VD_STEP;
	regval = max77779_sys_uvlo_set(regval, val);
	return max77779_external_chg_reg_write_helper(data->chg_dev, addr,
						      regval);
}

static int get_uvlo_helper(struct device *ifpmic_irq_dev, int *val, u8 type)
{
	u8 reg;
	int ret, addr;
	uint8_t (*max77779_sys_uvlo_get)(uint8_t regval);
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (type) {
	case UVLO1:
		addr = MAX77779_SYS_UVLO1_CNFG_0;
		max77779_sys_uvlo_get =
			_max77779_sys_uvlo1_cnfg_0_sys_uvlo1_get;
		break;
	case UVLO2:
		addr = MAX77779_SYS_UVLO2_CNFG_0;
		max77779_sys_uvlo_get =
			_max77779_sys_uvlo2_cnfg_0_sys_uvlo2_get;
		break;
	default:
		return -EINVAL;
	}

	ret = max77779_external_chg_reg_read_helper(data->chg_dev, addr, &reg);

	if (ret < 0)
		return ret;

	*val = VD_STEP * max77779_sys_uvlo_get(reg) + VD_LOWER_LIMIT;
	return 0;
}

VISIBLE_IF_KUNIT int max77779_set_uvlo1(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_helper(ifpmic_irq_dev, val, UVLO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_uvlo1);

VISIBLE_IF_KUNIT int max77779_get_uvlo1(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_helper(ifpmic_irq_dev, val, UVLO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_get_uvlo1);

VISIBLE_IF_KUNIT int max77779_set_uvlo2(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_helper(ifpmic_irq_dev, val, UVLO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_uvlo2);

VISIBLE_IF_KUNIT int max77779_get_uvlo2(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_helper(ifpmic_irq_dev, val, UVLO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_get_uvlo2);

/**
 * max77779_set_uvlo_vdroop() - Set UVLO VDROOP enable/disable.
 * @ifpmic_irq_dev: IFPMIC IRQ device.
 * @uvlo_type: UVLO type (UVLO1 or UVLO2).
 * @vdroop_type: VDROOP type (IF_VDROOP1).
 * @enable: True to enable, false to disable.
 *
 * This function enables or disables the VDROOP feature for a given UVLO type.
 *
 * Return: 0 on success, or an error code.
 */
VISIBLE_IF_KUNIT int max77779_set_uvlo_vdroop(struct device *ifpmic_irq_dev, int uvlo_type,
				  int vdroop_type, bool enable)
{
	u8 reg;
	int ret, addr;
	uint8_t (*max77779_sys_uvlo_set)(uint8_t regval, uint8_t val);
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (uvlo_type) {
	case UVLO1:
		addr = MAX77779_SYS_UVLO1_CNFG_1;
		max77779_sys_uvlo_set =
			_max77779_sys_uvlo1_cnfg_1_sys_uvlo1_vdrp1_en_set;
		if (vdroop_type != IF_VDROOP1)
			return -EINVAL;
		break;
	case UVLO2:
		addr = MAX77779_SYS_UVLO2_CNFG_1;
		max77779_sys_uvlo_set =
			_max77779_sys_uvlo2_cnfg_1_sys_uvlo2_vdrp2_en_set;
		if (vdroop_type != IF_VDROOP2)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	ret = max77779_external_chg_reg_read_helper(data->chg_dev, addr, &reg);
	if (ret < 0)
		return ret;

	reg = max77779_sys_uvlo_set(reg, enable);

	return max77779_external_chg_reg_write_helper(data->chg_dev, addr, reg);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_uvlo_vdroop);

/**
 * max77779_set_oilo_vdroop() - Set OILO VDROOP enable/disable.
 * @ifpmic_irq_dev: IFPMIC IRQ device.
 * @oilo_type: OILO type (BATOILO1 or BATOILO2).
 * @vdroop_type: VDROOP type (IF_VDROOP1 or IF_VDROOP2).
 * @enable: True to enable, false to disable.
 *
 * This function enables or disables the VDROOP feature for a given OILO type.
 *
 * Return: 0 on success, or an error code.
 */
VISIBLE_IF_KUNIT int max77779_set_oilo_vdroop(struct device *ifpmic_irq_dev, int oilo_type,
				  int vdroop_type, bool enable)
{
	u8 reg;
	int ret, addr;
	uint8_t (*oilo_vdroop_set)(uint8_t regval, uint8_t val);
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (oilo_type) {
	case BATOILO1:
		addr = MAX77779_BAT_OILO1_CNFG_3;
		switch (vdroop_type) {
		case IF_VDROOP1:
			oilo_vdroop_set =
				_max77779_bat_oilo1_cnfg_3_bat_oilo1_vdrp1_en_set;
			break;
		case IF_VDROOP2:
			oilo_vdroop_set =
				_max77779_bat_oilo1_cnfg_3_bat_oilo1_vdrp2_en_set;
			break;
		default:
			return -EINVAL;
		}
		break;
	case BATOILO2:
		addr = MAX77779_BAT_OILO2_CNFG_3;
		switch (vdroop_type) {
		case IF_VDROOP1:
			oilo_vdroop_set =
				_max77779_bat_oilo2_cnfg_3_bat_oilo2_vdrp1_en_set;
			break;
		case IF_VDROOP2:
			oilo_vdroop_set =
				_max77779_bat_oilo2_cnfg_3_bat_oilo2_vdrp2_en_set;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}
	ret = max77779_external_chg_reg_read_helper(data->chg_dev, addr, &reg);
	if (ret < 0)
		return ret;

	reg = oilo_vdroop_set(reg, enable);
	return max77779_external_chg_reg_write_helper(data->chg_dev, addr, reg);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_oilo_vdroop);

static int set_uvlo_hyst_helper(struct device *ifpmic_irq_dev, int val,
				int uvlo_type)
{
	int ret;
	u8 regval;
	int addr;
	uint8_t (*max77779_sys_uvlo_hyst_set)(uint8_t regval, uint8_t val);
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	if (val < HYST_LOWER_LIMIT || val > HYST_UPPER_LIMIT)
		return -EINVAL;

	switch (uvlo_type) {
	case UVLO1:
		addr = MAX77779_SYS_UVLO1_CNFG_0;
		max77779_sys_uvlo_hyst_set =
			_max77779_sys_uvlo1_cnfg_0_sys_uvlo1_hyst_set;
		break;
	case UVLO2:
		addr = MAX77779_SYS_UVLO2_CNFG_0;
		max77779_sys_uvlo_hyst_set =
			_max77779_sys_uvlo2_cnfg_0_sys_uvlo2_hyst_set;
		break;
	default:
		return -EINVAL;
	}

	ret = max77779_external_chg_reg_read_helper(data->chg_dev, addr,
						    &regval);

	if (ret < 0)
		return ret;

	val = (val - HYST_LOWER_LIMIT) / HYST_STEP;
	regval = max77779_sys_uvlo_hyst_set(regval, val);
	return max77779_external_chg_reg_write_helper(data->chg_dev, addr,
						      regval);
}

static int get_uvlo_hyst_helper(struct device *ifpmic_irq_dev, int *val,
				int uvlo_type)
{
	u8 reg;
	int ret, addr;
	uint8_t (*max77779_sys_uvlo_hyst_get)(uint8_t regval);
	struct max77779_bcl_irq_data *data = dev_get_drvdata(ifpmic_irq_dev);

	switch (uvlo_type) {
	case UVLO1:
		addr = MAX77779_SYS_UVLO1_CNFG_0;
		max77779_sys_uvlo_hyst_get =
			_max77779_sys_uvlo1_cnfg_0_sys_uvlo1_hyst_get;
		break;
	case UVLO2:
		addr = MAX77779_SYS_UVLO2_CNFG_0;
		max77779_sys_uvlo_hyst_get =
			_max77779_sys_uvlo2_cnfg_0_sys_uvlo2_hyst_get;
		break;
	default:
		return -EINVAL;
	}

	ret = max77779_external_chg_reg_read_helper(data->chg_dev, addr, &reg);

	if (ret < 0)
		return ret;

	*val = (HYST_STEP * max77779_sys_uvlo_hyst_get(reg)) + HYST_LOWER_LIMIT;
	return 0;
}

VISIBLE_IF_KUNIT int max77779_set_uvlo1_hyst(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_uvlo1_hyst);

VISIBLE_IF_KUNIT int max77779_get_uvlo1_hyst(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO1);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_get_uvlo1_hyst);

VISIBLE_IF_KUNIT int max77779_set_uvlo2_hyst(struct device *ifpmic_irq_dev, int val)
{
	return set_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_set_uvlo2_hyst);

VISIBLE_IF_KUNIT int max77779_get_uvlo2_hyst(struct device *ifpmic_irq_dev, int *val)
{
	return get_uvlo_hyst_helper(ifpmic_irq_dev, val, UVLO2);
}
EXPORT_SYMBOL_IF_KUNIT(max77779_get_uvlo2_hyst);

static struct bcl_ifpmic_ops max77779_bcl_ops = {
	.get_raw_sts = max77779_get_raw_sts,
	.get_irq = max77779_get_irq,
	.clr_irq = max77779_clr_irq,
	.vimon_read = max77779_vimon_read,
	.get_oilo1 = max77779_get_oilo1,
	.set_oilo1 = max77779_set_oilo1,
	.get_oilo2 = max77779_get_oilo2,
	.set_oilo2 = max77779_set_oilo2,
	.get_uvlo1 = max77779_get_uvlo1,
	.set_uvlo1 = max77779_set_uvlo1,
	.get_uvlo2 = max77779_get_uvlo2,
	.set_uvlo2 = max77779_set_uvlo2,
	.set_uvlo_vdroop = max77779_set_uvlo_vdroop,
	.set_oilo_vdroop = max77779_set_oilo_vdroop,
	.get_uvlo1_hyst = max77779_get_uvlo1_hyst,
	.set_uvlo1_hyst = max77779_set_uvlo1_hyst,
	.get_uvlo2_hyst = max77779_get_uvlo2_hyst,
	.set_uvlo2_hyst = max77779_set_uvlo2_hyst,
};

static void max77779_bcl_irq_remove(struct platform_device *pdev)
{
	struct max77779_bcl_irq_data *data = platform_get_drvdata(pdev);

	max77779_clr_irq(data->dev, UVLO1);
	max77779_clr_irq(data->dev, UVLO2);
	max77779_clr_irq(data->dev, BATOILO1);
	max77779_clr_irq(data->dev, BATOILO2);

	google_bcl_unregister_ifpmic();
}

static int max77779_bcl_irq_probe(struct platform_device *pdev)
{
	struct max77779_bcl_irq_data *data;
	struct device *google_bcl_dev;
	struct device_node *np;
	struct device_node *child;
	struct device_node *google_bcl_np;
	struct device_node *irq_list_node;
	struct platform_device *google_bcl_device;
	const char *label;
	const char *interrupt_name;
	const char *irq_handler_str;
	irq_handler_t handler_to_arm;
	int ret = 0;
	int ind = 0;
	int irq;
	bool use_gpio_irq;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = &pdev->dev;
	platform_set_drvdata(pdev, data);

	google_bcl_np =
		of_find_compatible_node(NULL, NULL, "google,google-bcl");
	if (!google_bcl_np)
		return -ENODEV;

	google_bcl_device = of_find_device_by_node(google_bcl_np);
	if (!google_bcl_device) {
		of_node_put(google_bcl_np);
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't find google-bcl device\n");
	}

	of_node_put(google_bcl_np);

	google_bcl_dev = &google_bcl_device->dev;

	data->bcl_dev = dev_get_drvdata(google_bcl_dev);
	if (!data->bcl_dev)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't get google-bcl drvdata\n");

	ret = google_bcl_register_ifpmic(&max77779_bcl_ops, data->dev);
	if (ret)
		return dev_err_probe(data->dev, ret,
				     "Failed to register ifpmic ops\n");

	data->pmic_dev = max77779_get_dev(data->dev, "ifpmic");
	if (!data->pmic_dev)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't get ifpmic\n");

	data->chg_dev = max77779_get_dev(data->dev, "chg");
	if (!data->chg_dev)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't get chg\n");

	data->vimon_dev = max77779_get_dev(data->dev, "vimon");
	if (!data->vimon_dev)
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER,
				     "Couldn't get vimon\n");

	np = data->dev->of_node;

	irq_list_node = of_get_child_by_name(np, "max77779-bcl-interrupts");
	if (!irq_list_node)
		return -EINVAL;

	data->vd1_gpio = devm_gpiod_get(data->dev, "vd1", GPIOD_ASIS);
	if (IS_ERR(data->vd1_gpio))
		return PTR_ERR(data->vd1_gpio);

	data->vd2_gpio = devm_gpiod_get(data->dev, "vd2", GPIOD_ASIS);
	if (IS_ERR(data->vd2_gpio))
		return PTR_ERR(data->vd2_gpio);

	ret = google_bcl_init_vdroop_gpio(data->bcl_dev);
	if (ret < 0)
		return ret;

	for_each_available_child_of_node(irq_list_node, child) {
		ret = of_property_read_string(child, "label", &label);
		if (ret < 0)
			goto clean_up_node;

		ret = of_property_read_string(child, "irq-handler",
					      &irq_handler_str);
		if (ret < 0)
			goto clean_up_node;

		ret = of_property_read_string(child, "interrupt-name",
					      &interrupt_name);
		if (ret < 0)
			goto clean_up_node;

		use_gpio_irq = of_property_read_bool(child, "use_gpio_irq");

		if (!data->irq_ctx[ind]) {
			data->irq_ctx[ind] = devm_kzalloc(
				data->dev, sizeof(struct max77779_irq_context),
				GFP_KERNEL);
			if (!data->irq_ctx[ind])
				return -ENOMEM;
		}

		if (strcmp(label, "uvlo1") == 0)
			data->irq_ctx[ind]->idx = UVLO1;
		else if (strcmp(label, "uvlo2") == 0)
			data->irq_ctx[ind]->idx = UVLO2;
		else if (strcmp(label, "oilo1") == 0)
			data->irq_ctx[ind]->idx = BATOILO1;
		else if (strcmp(label, "oilo2") == 0)
			data->irq_ctx[ind]->idx = BATOILO2;

		data->irq_ctx[ind]->parent = data;

		if (strcmp(irq_handler_str, "vdroop") == 0)
			handler_to_arm = max77779_bcl_vdroop_handler;
		else if (strcmp(irq_handler_str, "irqb") == 0)
			handler_to_arm = max77779_bcl_irqb_handler;
		else {
			ret = -EINVAL;
			goto clean_up_node;
		}

		if (use_gpio_irq) {
			if (strcmp(interrupt_name, "vd1") == 0) {
				irq = gpiod_to_irq(data->vd1_gpio);
			} else if (strcmp(interrupt_name, "vd2") == 0) {
				irq = gpiod_to_irq(data->vd2_gpio);
			} else {
				ret = -EINVAL;
				goto clean_up_node;
			}
			ret = devm_request_threaded_irq(
				data->dev, irq, NULL, handler_to_arm,
				IRQF_SHARED | IRQF_ONESHOT |
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING,
				interrupt_name, data->irq_ctx[ind]);
		} else {
			irq = of_irq_get(child, 0);
			if (irq < 0) {
				if (irq == -EPROBE_DEFER)
					of_node_put(child);
				ret = irq;
				goto clean_up_node;
			}

			ret = devm_request_threaded_irq(
				data->dev, irq, NULL, handler_to_arm,
				irq_get_trigger_type(irq) | IRQF_NO_THREAD |
					IRQF_ONESHOT | IRQF_SHARED,
				interrupt_name, data->irq_ctx[ind]);
		}
		if (ret < 0)
			goto clean_up_node;
		ind++;
	}

	max77779_clr_irq(data->dev, UVLO1);
	max77779_clr_irq(data->dev, UVLO2);
	max77779_clr_irq(data->dev, BATOILO1);
	max77779_clr_irq(data->dev, BATOILO2);

clean_up_node:
	of_node_put(irq_list_node);

	if (ret < 0)
		dev_err_probe(data->dev, ret, "Configuration invalid\n");

	return ret;
}

static const struct platform_device_id max77779_bcl_irq_id[] = {
	{ "max77779-bcl-irq", 0 },
	{},
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id max77779_bcl_irq_match_table[] = {
	{
		.compatible = "max77779-bcl-irq",
	},
	{},
};
#endif

static struct platform_driver max77779_bcl_irq_driver = {
	.driver = {
		.name = "max77779-bcl-irq",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = max77779_bcl_irq_match_table,
#endif
	},
	.probe = max77779_bcl_irq_probe,
	.remove = max77779_bcl_irq_remove,
	/* .id_table = max77779_bcl_irq_id, */
};

module_platform_driver(max77779_bcl_irq_driver);

MODULE_DESCRIPTION("Maxim 77779 BCL IRQ driver");
MODULE_AUTHOR("Sam Ou <samou@google.com>");
MODULE_AUTHOR("Hiroshi Akiyama <hiroshiakiyama@google.com>");
MODULE_AUTHOR("Maggie Cheng <maggiecheng@google.com>");
MODULE_AUTHOR("Allen Jiang <alljiang@google.com>");
MODULE_AUTHOR("Jasmine Cha <chajasmine@google.com>");
MODULE_LICENSE("GPL");
