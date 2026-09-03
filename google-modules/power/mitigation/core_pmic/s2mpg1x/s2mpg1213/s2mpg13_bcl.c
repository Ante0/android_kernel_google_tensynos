// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 *
 */

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mfd/samsung/s2mpg12.h>
#include <linux/mfd/samsung/s2mpg12-meter.h>
#include <linux/mfd/samsung/s2mpg12-register.h>
#include <linux/mfd/samsung/s2mpg13.h>
#include <linux/mfd/samsung/s2mpg13-meter.h>
#include <linux/mfd/samsung/s2mpg13-register.h>

#include "core_pmic_defs.h"
#include "s2mpg1213_bcl.h"
#include "s2mpg1x.h"

#define SUB_OFFSRC1 S2MPG13_PM_OFFSRC
#define SUB_OFFSRC2 S2MPG13_PM_OFFSRC

int core_pmic_sub_write_register(struct i2c_client *i2c, u8 reg, u8 value)
{
	return s2mpg13_write_reg(i2c, reg, value);
}

int core_pmic_sub_read_register(struct i2c_client *i2c, u8 reg, u8 *value)
{
	return s2mpg13_read_reg(i2c, reg, value);
}

void core_pmic_sub_meter_read_lpf_data(struct bcl_device *bcl_dev, struct brownout_stats *br_stats)
{
}

int core_pmic_sub_get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 id, u16 limit, u16 step)
{
	u8 value = 0;
	unsigned int ocp_warn_lvl;
	u8 addr;

	if (!bcl_dev->zone[id])
		return 0;
	if (bcl_dev->zone[id]->throttle_lvl_addr < 0)
		return -EINVAL;
	addr = bcl_dev->zone[id]->throttle_lvl_addr;

	if (pmic_read(CORE_PMIC_SUB, bcl_dev, addr, &value)) {
		dev_err(bcl_dev->device, "S2MPG1213 read %#x failed.", addr);
		return -EBUSY;
	}
	value &= OCP_WARN_MASK;
	ocp_warn_lvl = limit - value * step;
	*val = ocp_warn_lvl;
	return 0;
}

int core_pmic_sub_set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 id,
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
	if (pmic_read(CORE_PMIC_SUB, bcl_dev, addr, &value)) {
		dev_err(bcl_dev->device, "S2MPG1213 read %#x failed.", addr);
		return -EBUSY;
	}
	disable_irq(bcl_dev->zone[id]->bcl_irq);
	value &= ~(OCP_WARN_MASK) << OCP_WARN_LVL_SHIFT;
	value |= ((ulimit - val) / step) << OCP_WARN_LVL_SHIFT;
	ret = pmic_write(CORE_PMIC_SUB, bcl_dev, addr, value);
	enable_irq(bcl_dev->zone[id]->bcl_irq);

	return ret;
}

int core_pmic_sub_setup(struct bcl_device *bcl_dev)
{
	struct s2mpg13_platform_data *pdata;
	struct s2mpg13_dev *sub_dev = NULL;
	struct device_node *p_np;
	struct device_node *np = bcl_dev->device->of_node;
	struct i2c_client *i2c;
	int ret;
	struct gpio_desc *ocp_gpu_pin;
	struct gpio_desc *soft_ocp_gpu_pin;
	u8 val;
	u32 flag;

	p_np = of_parse_phandle(np, "google,sub-power", 0);
	if (p_np) {
		i2c = of_find_i2c_device_by_node(p_np);
		of_node_put(p_np);
		if (!i2c) {
			dev_err(bcl_dev->device, "Cannot find sub-power I2C\n");
			return -ENODEV;
		}
		sub_dev = i2c_get_clientdata(i2c);
	}
	if (!sub_dev) {
		dev_err(bcl_dev->device, "Sub PMIC device not found\n");
		return -ENODEV;
	}
	pdata = dev_get_platdata(sub_dev->dev);

	bcl_dev->sub_i2c = sub_dev->i2c;
	bcl_dev->sub_irq_base = pdata->irq_base;
	bcl_dev->sub_pmic_i2c = sub_dev->pmic;
	bcl_dev->sub_meter_i2c = sub_dev->meter;
	bcl_dev->sub_dev = sub_dev->dev;
	if (pmic_read(CORE_PMIC_SUB_I2C, bcl_dev, SUB_CHIPID, &val))
		return -ENODEV;
	else
		dev_info(bcl_dev->device, "PMIC chipid: %#x\n", val);
	pmic_read(CORE_PMIC_SUB, bcl_dev, SUB_OFFSRC1, &val);
	dev_info(bcl_dev->device, "SUB OFFSRC1 : %#x\n", val);
	bcl_dev->sub_offsrc1 = val;
	pmic_write(CORE_PMIC_SUB, bcl_dev, SUB_OFFSRC1, 0);
	pmic_read(CORE_PMIC_SUB, bcl_dev, SUB_OFFSRC2, &val);
	dev_info(bcl_dev->device, "SUB OFFSRC2 : %#x\n", val);
	bcl_dev->sub_offsrc2 = val;
	pmic_write(CORE_PMIC_SUB, bcl_dev, SUB_OFFSRC2, 0);

	ocp_gpu_pin = pdata->b2_ocp_warn_pin;

	soft_ocp_gpu_pin = pdata->b2_soft_ocp_warn_pin;

	flag = IRQF_ONESHOT | IRQF_NO_AUTOEN | IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING;
	ret = register_zone(bcl_dev, PRE_OCP_GPU, "ocp_gpu", ocp_gpu_pin,
				       gpiod_to_irq(ocp_gpu_pin), CORE_SUB_PMIC, IRQ_EXIST,
				       POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: GPU\n");
		return -ENODEV;
	}

	ret = register_zone(bcl_dev, SOFT_PRE_OCP_GPU, "soft_ocp_gpu", soft_ocp_gpu_pin,
				       gpiod_to_irq(soft_ocp_gpu_pin), CORE_SUB_PMIC, IRQ_EXIST,
				       POLARITY_HIGH, flag);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: SOFT_GPU\n");
		return -ENODEV;
	}

	return 0;
}
