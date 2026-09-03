// SPDX-License-Identifier: MIT

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>


#define TPS628600_REG_VOUT_2 0x02
#define TPS628600_REG_CONTROL 0x03
#define TPS628600_MIN_UV 400000
#define TPS628600_MAX_UV 1987500
#define TPS628600_STEP_UV 12500
#define TPS628600_N_VOUTS ((TPS628600_MAX_UV - TPS628600_MIN_UV) / TPS628600_STEP_UV + 1)
#define TPS628600_VOUT_2_DEFAULT 0x38
#define TPS628600_CONTROL_SETTING 0x63

struct tps628600_reg {
	struct device *dev;
	struct i2c_client *i2c;
	struct gpio_desc *enable_gpio;
	const struct regulator_desc *desc;
	bool enabled;
	u8 vout;
};

static int tps628600_is_enabled(struct regulator_dev *rdev)
{
	struct tps628600_reg *reg = rdev_get_drvdata(rdev);

	return reg->enabled;
}

static int tps628600_enable(struct regulator_dev *rdev)
{
	struct tps628600_reg *reg = rdev_get_drvdata(rdev);
	int ret;

	if (WARN(reg->enabled, "vout already enabled\n"))
		return 0;

	dev_dbg(reg->dev, "enabling\n");
	gpiod_set_value_cansleep(reg->enable_gpio, 1);
	reg->enabled = true;

	usleep_range(1000, 1010);

	ret = i2c_smbus_write_byte_data(reg->i2c, TPS628600_REG_VOUT_2, reg->vout);
	if (ret < 0) {
		dev_warn(reg->dev, "failed to set enable register (ret=%d)\n", ret);
		if (ret == -ETIMEDOUT)
			return 0;
	}

	/* Disable output discharge after each toggle */
	ret = i2c_smbus_write_byte_data(reg->i2c, TPS628600_REG_CONTROL, TPS628600_CONTROL_SETTING);
	if (ret)
		dev_err(reg->dev, "Failed to disable output discharge\n");

	return ret;
}

static int tps628600_disable(struct regulator_dev *rdev)
{
	struct tps628600_reg *reg = rdev_get_drvdata(rdev);

	if (WARN(!reg->enabled, "vout already disabled\n"))
		return 0;

	dev_dbg(reg->dev, "disabling\n");
	gpiod_set_value_cansleep(reg->enable_gpio, 0);
	reg->enabled = false;

	return 0;
}

static int tps628600_get_voltage_sel(struct regulator_dev *rdev)
{
	struct tps628600_reg *reg = rdev_get_drvdata(rdev);
	const struct regulator_ops *ops = reg->desc->ops;
	int vout = i2c_smbus_read_byte_data(reg->i2c, TPS628600_REG_VOUT_2);
	int voltage;

	if (vout < 0) {
		dev_warn(reg->dev, "failed to read vout (ret=%d)\n", vout);
		if (vout != -ETIMEDOUT)
			return vout;
		vout = reg->vout;
	}

	voltage = (vout * TPS628600_STEP_UV) + TPS628600_MIN_UV;

	return ops->map_voltage(rdev, voltage, voltage);
}

static int tps628600_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	struct tps628600_reg *reg = rdev_get_drvdata(rdev);
	const struct regulator_ops *ops = reg->desc->ops;
	int ret;
	int voltage;
	u8 vout;

	voltage = ops->list_voltage(rdev, selector);
	vout = (voltage - TPS628600_MIN_UV) / TPS628600_STEP_UV;

	dev_dbg(reg->dev, "setting %d uv(%#02X)\n", voltage, vout);
	reg->vout = vout;
	ret = i2c_smbus_write_byte_data(reg->i2c, TPS628600_REG_VOUT_2, vout);
	if (ret < 0) {
		dev_warn(reg->dev, "failed to set vout (ret=%d)\n", vout);
		if (ret == -ETIMEDOUT)
			return 0;
	}

	return ret;
}

static const struct regulator_ops tps628600_buck_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.is_enabled = tps628600_is_enabled,
	.enable = tps628600_enable,
	.disable = tps628600_disable,
	.get_voltage_sel = tps628600_get_voltage_sel,
	.set_voltage_sel = tps628600_set_voltage_sel,
};

static const struct regulator_desc tps628600_desc = {
	.name = "tps628600-reg",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.min_uV = TPS628600_MIN_UV,
	.uV_step = TPS628600_STEP_UV,
	.n_voltages = TPS628600_N_VOUTS,
	.ops = &tps628600_buck_reg_ops,
};

static struct regulator_init_data tps628600_data = {
	.constraints = {
		.min_uV = TPS628600_MIN_UV,
		.max_uV = TPS628600_MAX_UV,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
};

static int tps628600_probe(struct i2c_client *i2c)
{
	struct tps628600_reg *reg;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int data;

	reg = devm_kzalloc(&i2c->dev, sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	i2c_set_clientdata(i2c, reg);
	reg->dev = &i2c->dev;
	reg->i2c = i2c;
	reg->desc = &tps628600_desc;

	reg->i2c->adapter->timeout = 50;
	reg->i2c->adapter->retries = 2;

	reg->enable_gpio = devm_gpiod_get(reg->dev, "enable", GPIOD_ASIS);
	if (IS_ERR(reg->enable_gpio)) {
		if (PTR_ERR(reg->enable_gpio) == -EBUSY)
			dev_err(&i2c->dev, "Could not acquire GPIO, used by another device.\n");
		return PTR_ERR(reg->enable_gpio);
	}

	config.dev = &i2c->dev;
	config.of_node = i2c->dev.of_node;
	config.driver_data = reg;
	config.init_data = &tps628600_data;

	rdev = devm_regulator_register(&i2c->dev, reg->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "regulator registration failed\n");
		return PTR_ERR(rdev);
	}

	data = gpiod_get_value_cansleep(reg->enable_gpio);
	if (data < 0)
		return data;
	reg->enabled = data ? true : false;

	if (reg->enabled) {
		data = i2c_smbus_read_byte_data(reg->i2c, TPS628600_REG_VOUT_2);
		if (data < 0)
			return data;
		reg->vout = data;
	} else {
		reg->vout = TPS628600_VOUT_2_DEFAULT;
	}

	return 0;
}

static const struct of_device_id __maybe_unused tps628600_of_match_table[] = {
	{ .compatible = "ti,tps628600", },
	{}
};
MODULE_DEVICE_TABLE(of, tps628600_of_match_table);

static struct i2c_driver tps628600_driver = {
	.driver = {
		.name = "tps628600",
		.of_match_table = tps628600_of_match_table,
	},
	.probe = tps628600_probe,
};
module_i2c_driver(tps628600_driver);

MODULE_AUTHOR("Jeremy DeHaan <jdehaan@google.com>");
MODULE_DESCRIPTION("I2C based regulator Driver");
MODULE_LICENSE("Dual MIT/GPL");
