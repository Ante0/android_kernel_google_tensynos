// SPDX-License-Identifier: GPL-2.0-only
/*
 * sgm38122 regulator driver
 *
 * Copyright 2025 Google LLC.
 *
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define SGM_REG_CHIP_REV 0x00
#define SGM_REG_ENABLE_CTRL 0x01
#define SGM_REG_SEQ_SET1 0x02
#define SGM_REG_SEQ_SET2 0x03
#define SGM_REG_SEQ_CTRL 0x04
#define SGM_REG_DISCHG_RESISTER 0x05
#define SGM_REG_DVDD_OUTPUT 0x06
#define SGM_REG_AVDD1_OUTPUT 0x07
#define SGM_REG_AVDD2_OUTPUT 0x08
#define SGM_REG_AVDD3_OUTPUT 0x09
#define SGM_REG_INT_SET 0x0A
#define SGM_REG_INT_FLAG 0x0B
#define SGM_REG_OUTPUT_SUSPEND 0x0C
#define SGM_REG_STATUS 0x0D
#define SGM_REG_INT_MASK 0x0E

#define SGM_I2CRDY_TIMEUS 1000

#define SGM38122_FAST_DISCHARGE_SETTING 0xFF
#define SGM38122_EN_REF_ENABLE_SETTING 0x80
#define SGM38122_EN_REF_DISABLE_SETTING 0x00
#define SGM38122_EN_REF_MASK BIT(7)
#define SGM38122_VOUT_N_VOLTAGE 0xFF
#define SGM38122_VOUT_MASK 0xFF

#define DEVICE_NAME "sgm38122"

#if IS_ENABLED(CONFIG_DEBUG_FS)
#define WRITE_READABLE_ADDR 0x02
#define WRITE_READABLE_ADDR_RESET 0x0
#define WRITE_READABLE_MASK 0x77
#define ENABLE_DISABLE_TIMEOUT 15000
#endif

enum {
	DVDD = 0,
	AVDD1,
	AVDD2,
	AVDD3,
	MAX_LDO_CHANNEL,
};

struct sgm38122_priv {
	struct device *dev;
	struct gpio_desc *en_gpio;
	struct regulator *en_reg;
	struct regulator *vio;
	struct regmap *regmap;
	uint8_t chip_rev;
	struct mutex lock;
	bool is_enabled[MAX_LDO_CHANNEL];
	bool always_reapply_settings;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	uint8_t read_data;
	struct regulator_dev *reg_devs[MAX_LDO_CHANNEL];
#endif
};

const struct linear_range sgm38122_vol_range[] = {
	REGULATOR_LINEAR_RANGE(528000, 0x03, 0xA2, 8000),
	REGULATOR_LINEAR_RANGE(1504000, 0x0F, 0xFF, 8000),
	REGULATOR_LINEAR_RANGE(1504000, 0x0F, 0xFF, 8000),
	REGULATOR_LINEAR_RANGE(1504000, 0x0F, 0xFF, 8000),
};

static const struct regmap_config sgm38122_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SGM_REG_INT_MASK,
};

static ssize_t devid_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct sgm38122_priv *priv = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%#x\n", priv->chip_rev);
}

static DEVICE_ATTR_RO(devid);

static struct attribute *attrs[] = {
	&dev_attr_devid.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static int sgm38122_reapply_settings_locked(struct sgm38122_priv *priv)
{
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, SGM_REG_DISCHG_RESISTER, &val);
	if (ret)
		return ret;

	if (val != SGM38122_FAST_DISCHARGE_SETTING) {
		dev_warn(priv->dev, "fast discharge not set, chip reset detected.\n");
		if (priv->always_reapply_settings) {
			ret = regmap_write(priv->regmap, SGM_REG_DISCHG_RESISTER,
						 SGM38122_FAST_DISCHARGE_SETTING);
			if (ret) {
				dev_err(priv->dev, "set fast discharge failed.\n");
				return ret;
			}
		} else {
			dev_warn(priv->dev, "skipping discharge setting, output might not work.\n");
		}
	}

	return ret;
}

static int sgm38122_regulator_enable_locked(struct regulator_dev *rdev)
{
	struct sgm38122_priv *priv = rdev_get_drvdata(rdev);
	int ret;

	/*
	 * Detect chip reset by reading settings register.
	 * Log a warning and reapply the settings if necessary.
	 */
	ret = sgm38122_reapply_settings_locked(priv);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, SGM_REG_ENABLE_CTRL,
				 SGM38122_EN_REF_MASK, SGM38122_EN_REF_ENABLE_SETTING);
	if (ret) {
		dev_err(priv->dev, "set EN_REF failed.\n");
		return ret;
	}

	ret = regulator_enable_regmap(rdev);
	priv->is_enabled[rdev_get_id(rdev)] = true;

	return ret;
}

static int sgm38122_regulator_enable(struct regulator_dev *rdev)
{
	struct sgm38122_priv *priv = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&priv->lock);

	ret = sgm38122_regulator_enable_locked(rdev);

	mutex_unlock(&priv->lock);
	return ret;
}

static int sgm38122_regulator_disable_locked(struct regulator_dev *rdev)
{
	struct sgm38122_priv *priv = rdev_get_drvdata(rdev);
	int ret;
	int i;

	/*
	 * Detect chip reset by reading settings register.
	 * Log a warning and reapply the settings if necessary.
	 */
	ret = sgm38122_reapply_settings_locked(priv);
	if (ret)
		return ret;

	ret = regulator_disable_regmap(rdev);
	priv->is_enabled[rdev_get_id(rdev)] = false;

	for (i = 0; i < MAX_LDO_CHANNEL; i++) {
		if (priv->is_enabled[i])
			return ret;
	}

	ret = regmap_update_bits(priv->regmap, SGM_REG_ENABLE_CTRL,
				 SGM38122_EN_REF_MASK, SGM38122_EN_REF_DISABLE_SETTING);

	return ret;
}

static int sgm38122_regulator_disable(struct regulator_dev *rdev)
{
	struct sgm38122_priv *priv = rdev_get_drvdata(rdev);
	int ret;

	mutex_lock(&priv->lock);

	ret = sgm38122_regulator_disable_locked(rdev);

	mutex_unlock(&priv->lock);
	return ret;
}

static const struct regulator_ops sgm38122_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = sgm38122_regulator_enable,
	.disable = sgm38122_regulator_disable,
	.is_enabled = regulator_is_enabled_regmap,
};

#define SGM38122_REGULATOR_DESC(_id, _name, _supply)                        \
	[_id] = {                                                           \
		.name = _name,                                              \
		.supply_name = _supply,                                     \
		.id = _id,                                                  \
		.of_match = of_match_ptr(_name),                            \
		.n_voltages = SGM38122_VOUT_N_VOLTAGE,                      \
		.ops = &sgm38122_regulator_ops,                             \
		.regulators_node = of_match_ptr("regulators"),              \
		.linear_ranges = &sgm38122_vol_range[_id],                  \
		.n_linear_ranges = 1,                                       \
		.vsel_mask = SGM38122_VOUT_MASK,                            \
		.vsel_reg = SGM_REG_##_id##_OUTPUT,                         \
		.enable_reg = SGM_REG_ENABLE_CTRL,                          \
		.enable_mask = BIT(_id),                                    \
		.type = REGULATOR_VOLTAGE,                                  \
		.owner = THIS_MODULE,                                       \
		.enable_time = 150,                                         \
	}

static const struct regulator_desc sgm38122_regs_desc[MAX_LDO_CHANNEL] = {
	SGM38122_REGULATOR_DESC(DVDD, "dvdd", "in1"),
	SGM38122_REGULATOR_DESC(AVDD1, "avdd1", "in2"),
	SGM38122_REGULATOR_DESC(AVDD2, "avdd2", "in2"),
	SGM38122_REGULATOR_DESC(AVDD3, "avdd3", "in3"),
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int sgm38122_debugfs_reg_read_read(void *data, u64 *val)
{
	struct sgm38122_priv *priv = data;

	*val = priv->read_data;

	return 0;
}

static int sgm38122_debugfs_reg_read_write(void *data, u64 addr)
{
	struct sgm38122_priv *priv = data;
	int ret;
	int read_data;

	if (!IS_ERR(priv->vio)) {
		ret = regulator_enable(priv->vio);
		if (ret) {
			dev_err(priv->dev, "Failed to enable vio regulator: %d\n",
				ret);
			return ret;
		}
	}

	ret = regmap_read(priv->regmap, addr, &read_data);

	if (ret)
		dev_err(priv->dev, "regmap_read failed: %d\n", ret);
	else
		priv->read_data = read_data;

	if (!IS_ERR(priv->vio)) {
		ret = regulator_disable(priv->vio);
		if (ret) {
			dev_err(priv->dev, "Failed to disable regulator: %d\n",
				ret);
			return ret;
		}
	}

	return 0;
}

static ssize_t sgm38122_debugfs_reg_write_write(struct file *filp,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct sgm38122_priv *priv = filp->private_data;
	char kbuf[32];
	unsigned long addr, data;
	int ret;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	kbuf[count] = '\0';

	ret = sscanf(kbuf, "0x%lx 0x%lx", &addr, &data);

	if (ret != 2) {
		dev_err(priv->dev, "debugfs: Expect 2 hex integers\n");
		return -EINVAL;
	}

	if (!IS_ERR(priv->vio)) {
		ret = regulator_enable(priv->vio);
		if (ret) {
			dev_err(priv->dev, "Failed to enable vio regulator: %d\n",
				ret);
			return ret;
		}
	}

	ret = regmap_write(priv->regmap, addr, data);

	if (ret)
		dev_err(priv->dev, "regmap_write failed: %d\n", ret);


	if (!IS_ERR(priv->vio)) {
		ret = regulator_disable(priv->vio);
		if (ret) {
			dev_err(priv->dev, "Failed to disable regulator: %d\n",
				ret);
			return ret;
		}
	}

	return count;
}

static int sgm38122_debugfs_toggle_reg_devs(struct sgm38122_priv *priv)
{
	int ret;
	int i;
	ktime_t start_time, end_time;
	int latency_us;
	int total_latency_us = 0;

	mutex_lock(&priv->lock);
	for (i = 0; i < MAX_LDO_CHANNEL; i++) {
		if (priv->is_enabled[i]) {
			dev_err(priv->dev, "toggle reg test run failed: resource busy\n");
			mutex_unlock(&priv->lock);
			return -EBUSY;
		}
	}

	for (i = 0; i < MAX_LDO_CHANNEL; i++) {
		start_time = ktime_get();

		ret = sgm38122_regulator_enable_locked(priv->reg_devs[i]);
		if (ret)
			goto error_toggle_reg_devs;

		end_time = ktime_get();
		latency_us = ktime_us_delta(end_time, start_time);
		total_latency_us += latency_us;

		if (latency_us > ENABLE_DISABLE_TIMEOUT) {
			dev_err(priv->dev, "Enable timed out! takes %d ms > %d ms\n",
				latency_us / 1000, ENABLE_DISABLE_TIMEOUT / 1000);
			ret = -ETIMEDOUT;
			goto error_toggle_reg_devs;
		}

		dev_info(priv->dev, "Reg %d enabled, takes %d ms\n",
			i, latency_us / 1000);

		usleep_range(200, 500);
	}

	for (i = 0; i < MAX_LDO_CHANNEL; i++) {
		start_time = ktime_get();

		ret = sgm38122_regulator_disable_locked(priv->reg_devs[i]);
		if (ret)
			goto error_toggle_reg_devs;

		end_time = ktime_get();
		latency_us = ktime_us_delta(end_time, start_time);
		total_latency_us += latency_us;

		if (latency_us > ENABLE_DISABLE_TIMEOUT) {
			dev_err(priv->dev, "Disable timed out! takes %d ms > %d ms\n",
				latency_us / 1000, ENABLE_DISABLE_TIMEOUT / 1000);
			ret = -ETIMEDOUT;
			goto error_toggle_reg_devs;
		}

		dev_info(priv->dev, "Reg %d disabled, takes %d ms\n",
			i, latency_us / 1000);

		usleep_range(200, 500);
	}
	mutex_unlock(&priv->lock);

	if (total_latency_us > ENABLE_DISABLE_TIMEOUT * MAX_LDO_CHANNEL) {
		dev_err(priv->dev, "Enable disable stress timed out! takes %d ms > %d ms\n",
			total_latency_us / 1000, ENABLE_DISABLE_TIMEOUT * MAX_LDO_CHANNEL / 1000);
		return -ETIMEDOUT;
	}

	return 0;

error_toggle_reg_devs:
	for (i = 0; i < MAX_LDO_CHANNEL; i++) {
		sgm38122_regulator_disable_locked(priv->reg_devs[i]);
		usleep_range(200, 500);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static int sgm38122_debugfs_read_write_check(struct sgm38122_priv *priv)
{
	int i;
	int ret;
	int reset_ret;
	int write_value;
	int read_value;

	mutex_lock(&priv->lock);

	ret = regmap_read(priv->regmap, WRITE_READABLE_ADDR, &read_value);
	if (ret || read_value != WRITE_READABLE_ADDR_RESET) {
		dev_err(priv->dev, "regmap_write failed precondition ret: %d, read value: %d\n",
				ret, read_value);
		mutex_unlock(&priv->lock);
		return -EINVAL;
	}

	for (i = 0; i < MAX_LDO_CHANNEL; ++i) {
		write_value = get_random_u8() & WRITE_READABLE_MASK;

		ret = regmap_write(priv->regmap, WRITE_READABLE_ADDR, write_value);
		if (ret) {
			dev_err(priv->dev, "regmap_write failed on test run %d: %d\n", i, ret);
			break;
		}

		usleep_range(10, 20);

		ret = regmap_read(priv->regmap, WRITE_READABLE_ADDR, &read_value);
		if (ret) {
			dev_err(priv->dev, "regmap_read failed on test run %d: %d\n", i, ret);
			break;
		}

		if ((read_value & WRITE_READABLE_MASK) != write_value) {
			dev_err(priv->dev, "Read-write mismatch on test run %d: wrote 0x%x, read 0x%x\n",
					i, write_value, read_value);
			ret = -EIO;
			break;
		}

		dev_info(priv->dev, "Read-write check %d passed. Wrote 0x%x, Read 0x%x\n",
					i + 1, write_value, read_value);

		usleep_range(10, 20);
	}

	// Reset the register to a known state after the test
	reset_ret = regmap_write(priv->regmap, WRITE_READABLE_ADDR, WRITE_READABLE_ADDR_RESET);
	mutex_unlock(&priv->lock);

	if (reset_ret) {
		dev_err(priv->dev, "Failed to reset register 0x%x: %d\n",
				WRITE_READABLE_ADDR, reset_ret);
		return reset_ret;
	}

	return ret;
}

static int sgm38122_debugfs_stress_test_read(void *data, u64 *val)
{
	struct sgm38122_priv *priv = data;
	int ret;
	ktime_t start_time, end_time;
	int latency_us;

	if (!IS_ERR(priv->vio)) {
		ret = regulator_enable(priv->vio);
		if (ret) {
			dev_err(priv->dev, "Failed to enable vio regulator: %d\n",
				ret);
			*val = ret;
			goto out;
		}
	}

	start_time = ktime_get();

	ret = sgm38122_debugfs_toggle_reg_devs(priv);
	if (ret) {
		dev_err(priv->dev, "Failed toggle_reg_devs test: %d\n", ret);
		*val = ret;
		goto out;
	}

	ret = sgm38122_debugfs_read_write_check(priv);
	if (ret) {
		dev_err(priv->dev, "Failed read_write_check test: %d\n", ret);
		*val = ret;
		goto out;
	}

	end_time = ktime_get();
	latency_us = ktime_us_delta(end_time, start_time);

	dev_info(priv->dev, "Stress test complete, takes %d ms\n",
		latency_us / 1000);
	*val = 0;

out:
	if (!IS_ERR(priv->vio)) {
		ret = regulator_disable(priv->vio);
		if (ret) {
			dev_err(priv->dev, "Failed to disable regulator: %d\n",
				ret);
			*val = ret;
			return ret;
		}
	}

	return 0;
}

/* reg_read file is R/W able. */
DEFINE_DEBUGFS_ATTRIBUTE(fops_reg_read, sgm38122_debugfs_reg_read_read,
			 sgm38122_debugfs_reg_read_write, "0x%llx\n");
/* reg_write file is writeable. */
static const struct file_operations fops_reg_write = {
	.open = simple_open,
	.write = sgm38122_debugfs_reg_write_write,
};
DEFINE_DEBUGFS_ATTRIBUTE(fops_reg_stress_read, sgm38122_debugfs_stress_test_read,
			 NULL, "0x%llx\n");

static int sgm38122_debugfs_init(struct i2c_client *i2c)
{
	struct dentry *dentry;
	char dir_name[32];
	struct sgm38122_priv *priv = dev_get_drvdata(&i2c->dev);

	scnprintf(dir_name, sizeof(dir_name), "%s-%s", DEVICE_NAME,
		 dev_name(&i2c->adapter->dev));

	if (!debugfs_initialized())
		return -ENODEV;

	dentry = debugfs_create_dir(dir_name, NULL);

	debugfs_create_file("reg_read", 0660, dentry, priv, &fops_reg_read);
	debugfs_create_file("reg_write", 0220, dentry, priv, &fops_reg_write);
	debugfs_create_file("stress_test_read", 0440, dentry, priv, &fops_reg_stress_read);

	return 0;
}

#endif

static int sgm38122_probe(struct i2c_client *i2c)
{
	struct sgm38122_priv *priv;
	struct device *dev = &i2c->dev;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int i, ret, chip_rev;

	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "i2c check functionality failed.");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->en_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->en_gpio)) {
		dev_warn(dev, "Failed to get enable-gpios: %ld\n", PTR_ERR(priv->en_gpio));
		return PTR_ERR(priv->en_gpio);
	}

	priv->en_reg = devm_regulator_get(dev, "enable");
	if (IS_ERR(priv->en_reg)) {
		dev_err(dev, "Failed to get enable-supply: %ld\n", PTR_ERR(priv->en_reg));
		return PTR_ERR(priv->en_reg);
	}
	ret = regulator_enable(priv->en_reg);
	if (ret) {
		dev_err(dev, "Failed to enable en_reg regulator: %d\n", ret);
		return ret;
	}

	priv->vio = devm_regulator_get(dev, "vio");

	if (!IS_ERR(priv->vio)) {
		ret = regulator_enable(priv->vio);
		if (ret) {
			dev_err(dev, "Failed to enable vio regulator: %d\n", ret);
			return ret;
		}
	} else {
		dev_info(dev, "vio-supply not provided.\n");
	}

	usleep_range(SGM_I2CRDY_TIMEUS, SGM_I2CRDY_TIMEUS + 100);

	priv->dev = &i2c->dev;
	priv->regmap = devm_regmap_init_i2c(i2c, &sgm38122_regmap_config);

	ret = regmap_read(priv->regmap, SGM_REG_CHIP_REV, &chip_rev);
	if (ret) {
		dev_err(dev, "read chip_rev ID failed.");
		if (!IS_ERR(priv->vio))
			regulator_disable(priv->vio);
		return ret;
	}
	dev_info(dev, " Device ID = %#x\n", chip_rev);
	priv->chip_rev = chip_rev;
	priv->always_reapply_settings =
			of_property_read_bool(priv->dev->of_node, "google,enable-pin-can-restart");

	ret = regmap_write(priv->regmap, SGM_REG_DISCHG_RESISTER,
			   SGM38122_FAST_DISCHARGE_SETTING);
	if (ret) {
		dev_err(dev, "fast discharge setting failed.");
		if (!IS_ERR(priv->vio))
			regulator_disable(priv->vio);
		return ret;
	}

	for (i = 0; i < MAX_LDO_CHANNEL; i++) {
		config.dev = dev;
		config.regmap = priv->regmap;
		config.driver_data = priv;
		rdev = devm_regulator_register(dev, &sgm38122_regs_desc[i],
						   &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "regulator %s register failed: %d\n",
				sgm38122_regs_desc[i].name, ret);
			if (!IS_ERR(priv->vio))
				regulator_disable(priv->vio);
			return PTR_ERR(rdev);
		}
		#if IS_ENABLED(CONFIG_DEBUG_FS)
			priv->reg_devs[i] = rdev;
		#endif
	}
	mutex_init(&priv->lock);
	i2c_set_clientdata(i2c, priv);

	if (!IS_ERR(priv->vio)) {
		ret = regulator_disable(priv->vio);
		if (ret) {
			dev_err(dev, "Failed to disable regulator\n");
			return ret;
		}
	}

	ret = sysfs_create_group(&priv->dev->kobj, &attr_group);
	if (ret)
		dev_err(dev, "Failed to create attribute group: %d\n", ret);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	return sgm38122_debugfs_init(i2c);
#endif

	return 0;
}

static void sgm38122_remove(struct i2c_client *i2c)
{
	struct sgm38122_priv *priv = i2c_get_clientdata(i2c);

	mutex_destroy(&priv->lock);
	regulator_disable(priv->en_reg);
	if (priv->en_gpio)
		gpiod_direction_output_raw(priv->en_gpio, 0);
}

static const struct i2c_device_id sgm38122_id[] = {{"sgm38122", 0}, {}};

static const struct of_device_id sgm38122_dt_ids[] = {
	{
	.compatible = "sgm,sgm38122",
	},
	{}};
MODULE_DEVICE_TABLE(of, sgm38122_dt_ids);

static struct i2c_driver sgm38122_driver = {
	.probe = sgm38122_probe,
	.remove = sgm38122_remove,
	.id_table = sgm38122_id,
	.driver = {
		.of_match_table = sgm38122_dt_ids,
		.name = "sgm38122",
		.owner = THIS_MODULE,
	},
};

module_i2c_driver(sgm38122_driver);

MODULE_DESCRIPTION("SG Micro SGM38122 regulator driver");
MODULE_AUTHOR("Xu Han <xuhanyz@google.com>");
MODULE_LICENSE("GPL");
