/* SPDX-License-Identifier: GPL-2.0 */
/*
 * LN8282 Switched Capacitor Voltage Regulator Driver
 *
 * Copyright 2023 Google LLC
 *
 */

#pragma clang diagnostic ignored "-Wenum-conversion"
#pragma clang diagnostic ignored "-Wswitch"

#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/of_gpio.h>

#define LN8282_DEVICE_ID_REG 		0x00
#define LN8282_INT_DEVICE_0_REG 	0x01
#define LN8282_INT_DEVICE_1_REG 	0x02
#define LN8282_INT_HV_SC_0_REG		0x03
#define LN8282_INT_DEVICE_0_MASK_REG 	0x05
#define LN8282_INT_DEVICE_1_MASK_REG 	0x06
#define LN8282_INT_HV_SC_0_MASK_REG	0x07
#define LN8282_DEVICE_0_STS_REG		0x09
#define LN8282_DEVICE_1_STS_REG 	0x0A

#define LN8282_HV_SC_0_STS_REG		0x0B
#define LN8282_MODE_STS_BYPASS_BIT	BIT(2)
#define LN8282_MODE_STS_SWITCHING_BIT	BIT(3)

#define LN8282_DEVICE_0_CTRL_REG	0x0D
#define LN8282_DEVICE_1_CTRL_REG	0x0E

#define LN8282_HV_SC_CTRL_0_REG		0x0F
#define LN8282_STANDBY_EN_MASK		BIT(2)
#define LN8282_STANDBY_EN_SHIFT		2
#define LN8282_SC_OPERATION_MODE_MASK	GENMASK(1, 0)
#define LN8282_SWITCHING_MODE_VALUE	0x3
#define LN8282_BYPASS_MODE_VALUE	0x1

#define LN8282_HV_SC_CTRL_1_REG		0x10
#define LN8282_VIN_MAX_OV_CFG_MASK	BIT(6)
#define LN8282_VIN_MAX_OV_CFG_SHIFT	6
#define LN8282_VOUT_PRECHARGE_CFG_MASK	BIT(5)
#define LN8282_VOUT_PRECHARGE_CFG_SHIFT	5
#define LN8282_VOUT_MAX_OV_CFG_MASK	BIT(4)
#define LN8282_VOUT_MAX_OV_CFG_SHIFT	4

#define LN8282_SC_DITHER_CTRL_REG	0x12
#define LN8282_GLITCH_CTRL_REG		0x13
#define LN8282_FAULT_CTRL_REG		0x14

#define LN8282_TRACK_CTRL_REG 		0x15
#define LN8282_TRACK_CFG_MASK		BIT(4)
#define LN8282_TRACK_CFG_SHIFT		4

#define LN8282_LION_CTRL_REG		0x20
#define LN8282_UNLOCK_VAL		0x5B

#define LN8282_TEST_MODE_CTRL_REG	0x3B
#define LN8282_SOFTWARE_RESET_VAL	0xC6

#define LN8282_MISC_CFG_CTRL_REG	0x3C
#define LN8282_SOFT_RESET_REQ_VAL	0x01

#define LN8282_STS_C_REG		0x3F
#define LN8282_DISABLE_SC_STS_BIT	BIT(6)

#define LN8282_STS_RAW_REG		0x44
#define LN8282_VBUT_IN_OV_TRACK_RAW_BIT	BIT(7)


#define LN8282_MODE_BYPASS		0
#define LN8282_MODE_SWITCHING		1

struct ln8282_regulator_data {
	struct device			*dev;
	struct regulator_dev		*rdev;
	struct i2c_client 		*client;
	int 				irq_gpio;
	int 				irq_int;
	struct regmap 			*regmap;
};

static int ln8282_soft_reset(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	struct ln8282_regulator_data *rdata = rdev_get_drvdata(rdev);
	int sts_c, sts_raw;
	int ret;

	ret = regmap_read(regmap, LN8282_STS_C_REG, &sts_c);
	ret |= regmap_read(regmap, LN8282_STS_RAW_REG, &sts_raw);
	if (ret)
		return -EINVAL;
	dev_info(rdata->dev, "soft reset check: sts_c=%02x, sts_raw=%02x", sts_c, sts_raw);
	if (sts_c & LN8282_DISABLE_SC_STS_BIT && sts_raw & LN8282_VBUT_IN_OV_TRACK_RAW_BIT) {
		ret = regmap_write(regmap, LN8282_LION_CTRL_REG, LN8282_UNLOCK_VAL);
		if (!ret)
			ret = regmap_write(regmap, LN8282_TEST_MODE_CTRL_REG,
					   LN8282_SOFTWARE_RESET_VAL);

		if (!ret)
			ret = regmap_write(regmap, LN8282_MISC_CFG_CTRL_REG,
						   LN8282_SOFT_RESET_REQ_VAL);
	}
	return ret;
}

static int ln8282_set_switching_mode(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	struct ln8282_regulator_data *rdata = rdev_get_drvdata(rdev);
	int ret, i, val;

	dev_info(rdata->dev, "Setting switching mode\n");
	ret = regmap_update_bits(regmap, LN8282_HV_SC_CTRL_0_REG, LN8282_STANDBY_EN_MASK, 1 << LN8282_STANDBY_EN_SHIFT);
	if (!ret)
		ret = regmap_update_bits(regmap, LN8282_TRACK_CTRL_REG, LN8282_TRACK_CFG_MASK, 0);
	if (!ret)
		ret = regmap_update_bits(regmap, LN8282_HV_SC_CTRL_1_REG, LN8282_VIN_MAX_OV_CFG_MASK, 1 << LN8282_VIN_MAX_OV_CFG_SHIFT);
	if (!ret)
		ret = regmap_update_bits(regmap, LN8282_HV_SC_CTRL_1_REG, LN8282_VOUT_MAX_OV_CFG_MASK, 1 << LN8282_VOUT_MAX_OV_CFG_SHIFT);
	if (!ret)
		ret = regmap_update_bits(regmap, LN8282_HV_SC_CTRL_1_REG, LN8282_VOUT_PRECHARGE_CFG_MASK, 0 << LN8282_VOUT_PRECHARGE_CFG_SHIFT);
	if (!ret)
		ret = regmap_update_bits(regmap, LN8282_HV_SC_CTRL_0_REG, LN8282_SC_OPERATION_MODE_MASK, LN8282_SWITCHING_MODE_VALUE);
	if (!ret)
		ret = regmap_update_bits(regmap, LN8282_HV_SC_CTRL_0_REG, LN8282_STANDBY_EN_MASK, 0);

	if (ret != 0) {
		dev_err(rdata->dev, "Error updating one of the registers\n");
		return ret;
	}

	/* Wait up to 1 second for the mode to actually change */
	for (i = 0; i < 10; i += 1) {
		usleep_range(100 * USEC_PER_MSEC, 120 * USEC_PER_MSEC);
		ret = regmap_read(regmap, LN8282_HV_SC_0_STS_REG, &val);
		if (ret || (val & LN8282_MODE_STS_SWITCHING_BIT))
			return ret;
	}
	dev_err(rdata->dev, "Mode never switched after 1 second\n");
	/* Mode never switched, return error */
	return -EINVAL;
}

static int ln8282_set_bypass_mode(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	struct ln8282_regulator_data *rdata = rdev_get_drvdata(rdev);
	int ret, i, val;

	dev_info(rdata->dev,"Setting bypass mode\n");

	ret =regmap_update_bits(regmap, LN8282_HV_SC_CTRL_0_REG, LN8282_STANDBY_EN_MASK, 1 << LN8282_STANDBY_EN_SHIFT);
	if (!ret)
		regmap_update_bits(regmap, LN8282_TRACK_CTRL_REG, LN8282_TRACK_CFG_MASK, 1 << LN8282_TRACK_CFG_SHIFT);
	if (!ret)
		regmap_update_bits(regmap, LN8282_HV_SC_CTRL_1_REG, LN8282_VIN_MAX_OV_CFG_MASK, 1 << LN8282_VIN_MAX_OV_CFG_SHIFT);
	if (!ret)
		regmap_update_bits(regmap, LN8282_HV_SC_CTRL_1_REG, LN8282_VOUT_MAX_OV_CFG_MASK, 1 << LN8282_VOUT_MAX_OV_CFG_SHIFT);
	if (!ret)
		regmap_update_bits(regmap, LN8282_HV_SC_CTRL_1_REG, LN8282_VOUT_PRECHARGE_CFG_MASK, 1 << LN8282_VOUT_PRECHARGE_CFG_SHIFT);
	if (!ret)
		regmap_update_bits(regmap, LN8282_HV_SC_CTRL_0_REG, LN8282_SC_OPERATION_MODE_MASK, LN8282_BYPASS_MODE_VALUE);
	if (!ret)
		regmap_update_bits(regmap, LN8282_HV_SC_CTRL_0_REG, LN8282_STANDBY_EN_MASK, 0);

	if (ret != 0) {
		dev_err(rdata->dev, "Error updating one of the registers\n");
		return ret;
	}

	/* Wait up to 1 second for the mode to actually change */
	for (i = 0; i < 10; i += 1) {
		usleep_range(100 * USEC_PER_MSEC, 120 * USEC_PER_MSEC);
		ret = regmap_read(regmap, LN8282_HV_SC_0_STS_REG, &val);
		if (ret || (val & LN8282_MODE_STS_BYPASS_BIT))
			return ret;
	}
	dev_err(rdata->dev, "Mode never switched after 1 second\n");
	/* Mode never switched, return error */
	return -EINVAL;
}

static unsigned int ln8282_get_mode(struct regulator_dev *rdev)
{
	struct regmap *regmap = rdev_get_regmap(rdev);
	int val, ret;

	ret = regmap_read(regmap, LN8282_HV_SC_0_STS_REG, &val);
	if (ret)
		return REGULATOR_MODE_INVALID;

	if (val & LN8282_MODE_STS_SWITCHING_BIT)
		return REGULATOR_MODE_FAST;
	else if (val & LN8282_MODE_STS_BYPASS_BIT)
		return REGULATOR_MODE_NORMAL;
	else
		return REGULATOR_MODE_STANDBY;
}

static int ln8282_set_mode_helper(struct regulator_dev *rdev, unsigned int mode)
{
	int ret = 0;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		ret = ln8282_set_switching_mode(rdev);
		break;
	case REGULATOR_MODE_NORMAL:
		ret = ln8282_set_bypass_mode(rdev);
		break;
	}
	return ret;
}

static int ln8282_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	int ret;
	struct ln8282_regulator_data *rdata = rdev_get_drvdata(rdev);

	ret = ln8282_set_mode_helper(rdev, mode);

	if (ret && ln8282_get_mode(rdev) == REGULATOR_MODE_STANDBY) {
		dev_err(rdata->dev, "Mode not in switching or bypass, check to reset chip\n");
		ret = ln8282_soft_reset(rdev);
	}
	return ret;
}

static unsigned int ln8282_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case LN8282_MODE_SWITCHING:
		return REGULATOR_MODE_FAST;
	case LN8282_MODE_BYPASS:
		return REGULATOR_MODE_NORMAL;
	}
	return REGULATOR_MODE_INVALID;

}

static bool ln8282_is_reg(struct device *dev, unsigned int reg)
{
	/*
	 * Special registers above TRACK_CTRL (non-sequential) are valid
	 * And reserved registers within the nomral register block are invalid
	 */
	switch (reg) {
	case LN8282_LION_CTRL_REG:
	case LN8282_TEST_MODE_CTRL_REG:
	case LN8282_MISC_CFG_CTRL_REG:
	case LN8282_STS_C_REG:
	case LN8282_STS_RAW_REG:
		return true;
	case 0x04:
	case 0x08:
	case 0x0C:
	case 0x11:
		return false;
	}

	if (reg > LN8282_TRACK_CTRL_REG)
		return false;

	return true;
}

static bool ln8282_is_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case LN8282_DEVICE_ID_REG:
	case LN8282_DEVICE_0_STS_REG:
	case LN8282_DEVICE_1_STS_REG:
	case LN8282_HV_SC_0_STS_REG:
		return false;
	}

	return ln8282_is_reg(dev, reg);
}

static const struct regmap_config ln8282_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LN8282_STS_RAW_REG,

	.readable_reg = ln8282_is_reg,
	.volatile_reg = ln8282_is_reg,
	.writeable_reg = ln8282_is_writeable_reg,
};

static const struct regulator_ops ln8282_regulator_ops = {
	.set_mode = ln8282_set_mode,
	.get_mode = ln8282_get_mode,
};

static const struct regulator_desc ln8282_regulator_desc = {
	.name = "ln8282-capdiv",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &ln8282_regulator_ops,
	.of_map_mode = &ln8282_of_map_mode,
};

static int ln8282_probe(struct i2c_client *client)
{
	struct device_node *of_node = client->dev.of_node;
	struct ln8282_regulator_data *rdata;
	struct regulator_config rcfg = {};
	int ret;

	dev_info(&client->dev, "Probe start\n");
	ret = i2c_check_functionality(client->adapter,
				      I2C_FUNC_SMBUS_BYTE_DATA |
					      I2C_FUNC_SMBUS_WORD_DATA |
					      I2C_FUNC_SMBUS_I2C_BLOCK);
	if (ret < 0) {
		ret = i2c_get_functionality(client->adapter);
		dev_err(&client->dev, "I2C adapter not compatible %x\n", ret);
		return -ENOSYS;
	}

	rdata = devm_kzalloc(&client->dev, sizeof(*rdata), GFP_KERNEL);
	if (rdata == NULL) {
		dev_err(&client->dev, "Failed to allocate regulator data\n");
		return -ENOMEM;
	}

	if (of_node) {
		ret = of_get_named_gpio(of_node, "irq-gpio", 0);
		if (ret < 0) {
			dev_err(&client->dev, "unable to read irq_gpio from dt: %d\n",
				ret);
			return ret;
		}
		rdata->irq_gpio = ret;
		rdata->irq_int = gpio_to_irq(rdata->irq_gpio);
		dev_info(&client->dev, "int gpio:%d, gpio_irq:%d\n", rdata->irq_gpio,
			rdata->irq_int);
	}

	rdata->regmap = devm_regmap_init_i2c(client, &ln8282_regmap_config);

	i2c_set_clientdata(client, rdata);
	rdata->dev = &client->dev;
	rdata->client = client;

	rcfg.dev = &client->dev;
	rcfg.of_node = client->dev.of_node;
	rcfg.regmap = rdata->regmap;
	rcfg.driver_data = rdata;
	rcfg.init_data = of_get_regulator_init_data(&client->dev, client->dev.of_node, &ln8282_regulator_desc);

	rdata->rdev = devm_regulator_register(&client->dev, &ln8282_regulator_desc, &rcfg);
	if (IS_ERR(rdata->rdev)) {
		dev_err(&client->dev, "Failed to register regulator");
		return PTR_ERR(rdata->rdev);
	}

	dev_info(&client->dev, "Probe complete\n");
	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static struct of_device_id ln8282_of_match_table[] = {
	{
		.compatible = "cirrus,ln8282",
	},
	{},
};
#else
#define ln8282_of_match_table NULL
#endif

static struct i2c_driver ln8282_driver = {
    .driver =
	{
	    .name = "ln8282",
	    .owner = THIS_MODULE,
	    .of_match_table = ln8282_of_match_table,
	    .pm = NULL,
	    .probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
    .probe = ln8282_probe,
};

module_i2c_driver(ln8282_driver);
MODULE_DESCRIPTION("Cirrus Logic LN8282 Switched Capacitor Voltage Regulator Driver");
MODULE_AUTHOR("Alice Sheng <alicesheng@google.com>");
MODULE_LICENSE("GPL");
