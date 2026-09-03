// SPDX-License-Identifier: GPL-2.0-only
/*
 * sgm38125 regulator driver
 *
 * Copyright 2026 Google LLC.
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
#define SGM_REG_DVDD_SET 0x01
#define SGM_REG_AVDD1_SET 0x02
#define SGM_REG_AVDD2_SET 0x03
#define SGM_REG_AVDD3_SET 0x04
#define SGM_REG_SEQ_CTRL 0x05
#define SGM_REG_DVDD_OUTPUT 0x06
#define SGM_REG_AVDD1_OUTPUT 0x07
#define SGM_REG_AVDD2_OUTPUT 0x08
#define SGM_REG_AVDD3_OUTPUT 0x09
#define SGM_REG_SYSTEM_SET 0x0A
#define SGM_REG_INT_FLAG 0x0B
#define SGM_REG_OUTPUT_SUSPEND 0x0C
#define SGM_REG_STATUS 0x0D
#define SGM_REG_INT_MASK 0x0E

#define SGM_I2CRDY_TIMEUS 1000

#define SGM38125_FAST_DISCHARGE_MASK 0x30
#define SGM38125_VOUT_N_VOLTAGE 0xFF
#define SGM38125_VOUT_MASK 0xFF

#define DEVICE_NAME "sgm38125"

#if IS_ENABLED(CONFIG_DEBUG_FS)
#define WRITE_READABLE_ADDR 0x03
#define WRITE_READABLE_ADDR_RESET 0x0
#define WRITE_READABLE_MASK 0x07
#define ENABLE_DISABLE_TIMEOUT 15000
#endif

enum {
	DVDD = 0,
	AVDD1,
	AVDD2,
	AVDD3,
	MAX_LDO_CHANNEL,
};

struct sgm38125_priv {
	struct device *dev;
	struct gpio_desc *en_gpio;
	struct regulator *en_reg;
	struct regulator *vio;
	struct regmap *regmap;
	uint8_t chip_rev;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	uint8_t reg_read;
	struct regulator_dev *reg_devs[MAX_LDO_CHANNEL];
#endif
};

const struct linear_range sgm38125_vol_range[] = {
	REGULATOR_LINEAR_RANGE(528000, 0x03, 0xA2, 8000),
	REGULATOR_LINEAR_RANGE(1504000, 0x0F, 0xFF, 8000),
	REGULATOR_LINEAR_RANGE(1504000, 0x0F, 0xFF, 8000),
	REGULATOR_LINEAR_RANGE(1504000, 0x0F, 0xFF, 8000),
};

static const struct regmap_config sgm38125_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SGM_REG_INT_MASK,
};

static const unsigned int sgm38125_reg_controls[] = {
	SGM_REG_DVDD_SET,
	SGM_REG_AVDD1_SET,
	SGM_REG_AVDD2_SET,
	SGM_REG_AVDD3_SET,
};

static ssize_t devid_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct sgm38125_priv *priv = dev_get_drvdata(dev);

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

static const struct regulator_ops sgm38125_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

#define SGM38125_REGULATOR_DESC(_id, _name, _supply)                        \
	[_id] = {                                                           \
		.name = _name,                                              \
		.supply_name = _supply,                                     \
		.id = _id,                                                  \
		.of_match = of_match_ptr(_name),                            \
		.n_voltages = SGM38125_VOUT_N_VOLTAGE,                      \
		.ops = &sgm38125_regulator_ops,                             \
		.regulators_node = of_match_ptr("regulators"),              \
		.linear_ranges = &sgm38125_vol_range[_id],                  \
		.n_linear_ranges = 1,                                       \
		.vsel_mask = SGM38125_VOUT_MASK,                            \
		.vsel_reg = SGM_REG_##_id##_OUTPUT,                         \
		.enable_reg = SGM_REG_##_id##_SET,                          \
		.enable_mask = BIT(3),                                      \
		.type = REGULATOR_VOLTAGE,                                  \
		.owner = THIS_MODULE,                                       \
		.enable_time = 150,                                         \
	}

static const struct regulator_desc sgm38125_regs_desc[MAX_LDO_CHANNEL] = {
	SGM38125_REGULATOR_DESC(DVDD, "dvdd", "in1"),
	SGM38125_REGULATOR_DESC(AVDD1, "avdd1", "in2"),
	SGM38125_REGULATOR_DESC(AVDD2, "avdd2", "in2"),
	SGM38125_REGULATOR_DESC(AVDD3, "avdd3", "in3"),
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int sgm38125_debugfs_reg_read_read(void *data, u64 *val)
{
	struct sgm38125_priv *priv = data;
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

	ret = regmap_read(priv->regmap, priv->reg_read, &read_data);

	if (ret)
		dev_err(priv->dev, "regmap_read failed: %d\n", ret);
	else
		*val = read_data;

	if (!IS_ERR(priv->vio)) {
		ret = regulator_disable(priv->vio);
		if (ret) {
			dev_err(priv->dev, "Failed to disable regulator: %d\n",
				ret);
			return ret;
		}
	}

	return ret;
}

static int sgm38125_debugfs_reg_read_write(void *data, u64 addr)
{
	struct sgm38125_priv *priv = data;

	priv->reg_read = addr;

	return 0;
}

static ssize_t sgm38125_debugfs_reg_write_write(struct file *filp,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct sgm38125_priv *priv = filp->private_data;
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

/* reg_read file is R/W able. */
DEFINE_DEBUGFS_ATTRIBUTE(fops_reg_read, sgm38125_debugfs_reg_read_read,
			 sgm38125_debugfs_reg_read_write, "0x%llx\n");
/* reg_write file is writeable. */
static const struct file_operations fops_reg_write = {
	.open = simple_open,
	.write = sgm38125_debugfs_reg_write_write,
};

static int sgm38125_debugfs_init(struct i2c_client *i2c)
{
	struct dentry *dentry;
	char dir_name[32];
	struct sgm38125_priv *priv = dev_get_drvdata(&i2c->dev);

	scnprintf(dir_name, sizeof(dir_name), "%s-%s-%#x", DEVICE_NAME,
		 dev_name(&i2c->adapter->dev), i2c->addr);

	if (!debugfs_initialized())
		return -ENODEV;

	dentry = debugfs_create_dir(dir_name, NULL);

	debugfs_create_file("reg_read", 0660, dentry, priv, &fops_reg_read);
	debugfs_create_file("reg_write", 0220, dentry, priv, &fops_reg_write);

	return 0;
}

#endif

static int sgm38125_probe(struct i2c_client *i2c)
{
	struct sgm38125_priv *priv;
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
	priv->regmap = devm_regmap_init_i2c(i2c, &sgm38125_regmap_config);

	ret = regmap_read(priv->regmap, SGM_REG_CHIP_REV, &chip_rev);
	if (ret) {
		dev_err(dev, "read chip_rev ID failed.");
		if (!IS_ERR(priv->vio))
			regulator_disable(priv->vio);
		return ret;
	}
	dev_info(dev, " Device ID = %#x\n", chip_rev);
	priv->chip_rev = chip_rev;

	for (i = 0; i < MAX_LDO_CHANNEL; ++i) {
		ret = regmap_update_bits(priv->regmap, sgm38125_reg_controls[i],
			SGM38125_FAST_DISCHARGE_MASK, SGM38125_FAST_DISCHARGE_MASK);
		if (ret) {
			dev_err(dev, "fast discharge setting(%d) failed.", i);
			if (!IS_ERR(priv->vio))
				regulator_disable(priv->vio);
			return ret;
		}
		config.dev = dev;
		config.regmap = priv->regmap;
		config.driver_data = priv;
		rdev = devm_regulator_register(dev, &sgm38125_regs_desc[i],
						   &config);
		if (IS_ERR(rdev)) {
			ret = PTR_ERR(rdev);
			dev_err(dev, "regulator %s register failed: %d\n",
				sgm38125_regs_desc[i].name, ret);
			if (!IS_ERR(priv->vio))
				regulator_disable(priv->vio);
			return PTR_ERR(rdev);
		}
		#if IS_ENABLED(CONFIG_DEBUG_FS)
			priv->reg_devs[i] = rdev;
		#endif
	}
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
	return sgm38125_debugfs_init(i2c);
#endif

	return 0;
}

static void sgm38125_remove(struct i2c_client *i2c)
{
	struct sgm38125_priv *priv = i2c_get_clientdata(i2c);

	regulator_disable(priv->en_reg);
	if (priv->en_gpio)
		gpiod_direction_output_raw(priv->en_gpio, 0);
}

static const struct i2c_device_id sgm38125_id[] = {{"sgm38125", 0}, {}};

static const struct of_device_id sgm38125_dt_ids[] = {
	{
	.compatible = "sgm,sgm38125",
	},
	{}};
MODULE_DEVICE_TABLE(of, sgm38125_dt_ids);

static struct i2c_driver sgm38125_driver = {
	.probe = sgm38125_probe,
	.remove = sgm38125_remove,
	.id_table = sgm38125_id,
	.driver = {
		.of_match_table = sgm38125_dt_ids,
		.name = "sgm38125",
		.owner = THIS_MODULE,
	},
};

module_i2c_driver(sgm38125_driver);

MODULE_DESCRIPTION("SG Micro SGM38125 regulator driver");
MODULE_AUTHOR("JY Lin <jingyoulin@google.com>");
MODULE_LICENSE("GPL");
