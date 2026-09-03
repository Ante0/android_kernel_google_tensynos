// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mfd/samsung/rtc-s2mpg12.h>
#include <linux/mfd/samsung/s2mpg12.h>
#include <linux/mfd/samsung/s2mpg12-meter.h>
#include <linux/mfd/samsung/s2mpg12-register.h>
#include <linux/mfd/samsung/s2mpg13.h>
#include <linux/mfd/samsung/s2mpg13-meter.h>
#include <linux/mfd/samsung/s2mpg13-register.h>

#include "core_pmic_defs.h"
#include "s2mpg1213_bcl.h"
#include "s2mpg1x.h"

#define MAIN_OFFSRC1 S2MPG12_PM_OFFSRC1
#define MAIN_OFFSRC2 S2MPG12_PM_OFFSRC2
#define MAIN_PWRONSRC S2MPG12_PM_PWRONSRC
#define RTC_SCRATCH1 S2MPG12_RTC_SCRATCH1

int core_pmic_main_write_register(struct i2c_client *i2c, u8 reg, u8 value)
{
	return s2mpg12_write_reg(i2c, reg, value);
}

int core_pmic_main_read_register(struct i2c_client *i2c, u8 reg, u8 *value)
{
	return s2mpg12_read_reg(i2c, reg, value);
}

void core_pmic_main_meter_read_lpf_data(struct bcl_device *bcl_dev, struct brownout_stats *br_stats)
{
}

int core_pmic_main_get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 id, u16 limit, u16 step)
{
	u8 value = 0;
	unsigned int ocp_warn_lvl;
	u8 addr;

	if (!bcl_dev->zone[id])
		return 0;
	if (bcl_dev->zone[id]->throttle_lvl_addr < 0)
		return -EINVAL;
	addr = bcl_dev->zone[id]->throttle_lvl_addr;

	if (pmic_read(CORE_PMIC_MAIN, bcl_dev, addr, &value)) {
		dev_err(bcl_dev->device, "S2MPG1213 read %#x failed.", addr);
		return -EBUSY;
	}
	value &= OCP_WARN_MASK;
	ocp_warn_lvl = limit - value * step;
	*val = ocp_warn_lvl;
	return 0;
}

int core_pmic_main_set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 id,
			       u16 llimit, u16 ulimit, u16 step)
{
	u8 value, addr;
	int ret;

	if (!bcl_dev->zone[id])
		return 0;
	if (bcl_dev->zone[id]->throttle_lvl_addr < 0)
		return -EINVAL;
	addr = bcl_dev->zone[id]->throttle_lvl_addr;

	if (val < llimit || val > ulimit) {
		dev_err(bcl_dev->device, "OCP_WARN LEVEL %llu outside of range %d - %d mA.", val,
		       llimit, ulimit);
		return -EBUSY;
	}
	if (pmic_read(CORE_PMIC_MAIN, bcl_dev, addr, &value)) {
		dev_err(bcl_dev->device, "S2MPG1213 read %#x failed.", addr);
		return -EBUSY;
	}
	disable_irq(bcl_dev->zone[id]->bcl_irq);
	value &= ~(OCP_WARN_MASK) << OCP_WARN_LVL_SHIFT;
	value |= ((ulimit - val) / step) << OCP_WARN_LVL_SHIFT;
	ret = pmic_write(CORE_PMIC_MAIN, bcl_dev, addr, value);
	enable_irq(bcl_dev->zone[id]->bcl_irq);

	return ret;
}

int core_pmic_main_read_uvlo(struct bcl_device *bcl_dev, unsigned int *smpl_warn_lvl)
{
	u8 value = 0;

	if (!bcl_dev->main_pmic_i2c)
		return -EBUSY;
	pmic_read(CORE_PMIC_MAIN, bcl_dev, SMPL_WARN_CTRL, &value);
	value >>= SMPL_WARN_SHIFT;

	*smpl_warn_lvl = value * 100 + SMPL_LOWER_LIMIT;
	return 0;
}

u16 core_pmic_main_store_uvlo(struct bcl_device *bcl_dev, unsigned int val, size_t size)
{
	u8 value;
	int ret = 0;

	if (val < SMPL_LOWER_LIMIT || val > SMPL_UPPER_LIMIT) {
		dev_err(bcl_dev->device, "SMPL_WARN LEVEL %d outside of range %d - %d mV.", val,
			SMPL_LOWER_LIMIT, SMPL_UPPER_LIMIT);
		return -EINVAL;
	}
	if (!bcl_dev->main_pmic_i2c) {
		dev_err(bcl_dev->device, "MAIN I2C not found\n");
		return -EIO;
	}
	if (pmic_read(CORE_PMIC_MAIN, bcl_dev, SMPL_WARN_CTRL, &value)) {
		dev_err(bcl_dev->device, "S2MPG1213 read %#x failed.", SMPL_WARN_CTRL);
		return -EBUSY;
	}
	disable_irq(bcl_dev->zone[PRE_UVLO]->bcl_irq);
	value &= ~SMPL_WARN_MASK;
	value |= ((val - SMPL_LOWER_LIMIT) / 100) << SMPL_WARN_SHIFT;
	if (pmic_write(CORE_PMIC_MAIN, bcl_dev, SMPL_WARN_CTRL, value)) {
		dev_err(bcl_dev->device, "i2c write error setting smpl_warn\n");
		enable_irq(bcl_dev->zone[PRE_UVLO]->bcl_irq);
		return ret;
	}

	enable_irq(bcl_dev->zone[PRE_UVLO]->bcl_irq);

	return size;
}

void core_pmic_parse_dtree(struct bcl_device *bcl_dev) {}

int core_pmic_main_setup(struct bcl_device *bcl_dev, struct platform_device *pdev)
{
	struct s2mpg12_platform_data *pdata;
	struct s2mpg12_dev *main_dev = NULL;
	struct device_node *p_np;
	struct device_node *np = bcl_dev->device->of_node;
	struct i2c_client *i2c;
	int ret;
	u8 val;
	struct gpio_desc *ocp_cpu1_pin;
	struct gpio_desc *ocp_cpu2_pin;
	struct gpio_desc *ocp_tpu_pin;
	struct gpio_desc *soft_ocp_cpu2_pin;
	struct gpio_desc *soft_ocp_cpu1_pin;
	struct gpio_desc *soft_ocp_tpu_pin;

	u32 flag;

	p_np = of_parse_phandle(np, "google,main-power", 0);
	if (p_np) {
		i2c = of_find_i2c_device_by_node(p_np);
		of_node_put(p_np);
		if (!i2c) {
			dev_err(bcl_dev->device, "Cannot find main-power I2C\n");
			return -ENODEV;
		}
		main_dev = i2c_get_clientdata(i2c);
	}
	if (!main_dev) {
		dev_err(bcl_dev->device, "Main PMIC device not found\n");
		return -ENODEV;
	}
	pdata = dev_get_platdata(main_dev->dev);

	bcl_dev->main_i2c = main_dev->i2c;
	bcl_dev->main_irq_base = pdata->irq_base;
	bcl_dev->main_pmic_i2c = main_dev->pmic;
	bcl_dev->main_rtc_i2c = main_dev->rtc;
	bcl_dev->main_meter_i2c = main_dev->meter;
	bcl_dev->main_dev = main_dev->dev;
	/* clear MAIN information every boot */
	/* see b/213371339 */
	pmic_read(CORE_PMIC_MAIN, bcl_dev, MAIN_OFFSRC1, &val);
	dev_info(bcl_dev->device, "MAIN OFFSRC1 : %#x\n", val);
	bcl_dev->main_offsrc1 = val;
	pmic_read(CORE_PMIC_MAIN, bcl_dev, MAIN_OFFSRC2, &val);
	dev_info(bcl_dev->device, "MAIN OFFSRC2 : %#x\n", val);
	bcl_dev->main_offsrc2 = val;
	pmic_read(CORE_PMIC_MAIN, bcl_dev, MAIN_PWRONSRC, &val);
	dev_info(bcl_dev->device, "MAIN PWRONSRC: %#x\n", val);
	bcl_dev->pwronsrc = val;
	pmic_write(CORE_PMIC_MAIN, bcl_dev, MAIN_OFFSRC1, 0);
	pmic_write(CORE_PMIC_MAIN, bcl_dev, MAIN_OFFSRC2, 0);
	pmic_write(CORE_PMIC_MAIN, bcl_dev, S2MPG12_PM_SMPL_WARN_CTRL, bcl_dev->smpl_ctrl);

	ocp_cpu2_pin = pdata->b2_ocp_warn_pin;
	ocp_cpu1_pin = pdata->b3_ocp_warn_pin;
	ocp_tpu_pin = pdata->b10_ocp_warn_pin;

	soft_ocp_cpu2_pin = pdata->b2_soft_ocp_warn_pin;
	soft_ocp_cpu1_pin = pdata->b3_soft_ocp_warn_pin;
	soft_ocp_tpu_pin = pdata->b10_soft_ocp_warn_pin;

	flag = IRQF_ONESHOT | IRQF_NO_AUTOEN | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
	ret = register_zone(bcl_dev, PRE_UVLO, "smpl_warn",
				       pdata->smpl_warn_pin, gpiod_to_irq(pdata->smpl_warn_pin),
				       CORE_MAIN_PMIC, IRQ_EXIST, POLARITY_LOW, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SMPL_WARN\n");
		return -ENODEV;
	}

	ret = register_zone(bcl_dev, PRE_OCP_CPU1, "ocp_cpu1",
				       ocp_cpu1_pin, gpiod_to_irq(ocp_cpu1_pin), CORE_MAIN_PMIC,
				       IRQ_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: CPUCL1\n");
		return -ENODEV;
	}

	ret = register_zone(bcl_dev, PRE_OCP_CPU2, "ocp_cpu2",
				       ocp_cpu2_pin, gpiod_to_irq(ocp_cpu2_pin), CORE_MAIN_PMIC,
				       IRQ_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: CPUCL2\n");
		return -ENODEV;
	}

	ret = register_zone(bcl_dev, PRE_OCP_TPU, "ocp_tpu",
				       ocp_tpu_pin, gpiod_to_irq(ocp_tpu_pin), CORE_MAIN_PMIC,
				       IRQ_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: TPU\n");
		return -ENODEV;
	}

	ret = register_zone(bcl_dev, SOFT_PRE_OCP_CPU1, "soft_ocp_cpu1",
				       soft_ocp_cpu1_pin, gpiod_to_irq(soft_ocp_cpu1_pin),
				       CORE_MAIN_PMIC, IRQ_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_CPUCL1\n");
		return -ENODEV;
	}

	ret = register_zone(bcl_dev, SOFT_PRE_OCP_CPU2, "soft_ocp_cpu2",
				       soft_ocp_cpu2_pin, gpiod_to_irq(soft_ocp_cpu2_pin),
				       CORE_MAIN_PMIC, IRQ_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_CPUCL2\n");
		return -ENODEV;
	}

	ret = register_zone(bcl_dev, SOFT_PRE_OCP_TPU, "soft_ocp_tpu",
				       soft_ocp_tpu_pin, gpiod_to_irq(soft_ocp_tpu_pin),
				       CORE_MAIN_PMIC, IRQ_EXIST, POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_TPU\n");
		return -ENODEV;
	}


	return 0;
}

int get_rtc_scratch1_register(u8 *value)
{
	*value = RTC_SCRATCH1;
	return 0;
}
