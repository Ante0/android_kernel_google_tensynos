// SPDX-License-Identifier: GPL-2.0-only
/*
 * sy70206 regulator driver
 *
 * Copyright 2025 Google LLC.
 *
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>

#define DEVICE_NAME "sy70206"

#define SY70206_MODE_AUTO 0
#define SY70206_MODE_FPWM 0xC

#define SY70206_REG_START 0x01
#define SY70206_REG_END 0x12
#define SY70206_REG_CONF 0x07
#define SY70206_REG_CTRL 0x10
#define SY70206_REG_VOUT1 0x11
#define SY70206_REG_VOUT2 0x12
#define SY70206_REG_INT1 0x0A
#define SY70206_REG_INT2 0x0B
#define SY70206_REG_DEVID 0x0C

#define SY70206_ENABLE_MASK BIT(7)
#define SY70206_MODE_MASK GENMASK(3, 2)
#define SY70206_VSEL_MASK 0xFF

#define SY70206_VOUT_MINUV 2500000
#define SY70206_VOUT_MAXUV 5500000
#define SY70206_VOUT_STPUV 12500
#define SY70206_N_VOUTS                                                        \
	((SY70206_VOUT_MAXUV - SY70206_VOUT_MINUV) / SY70206_VOUT_STPUV + 1)

#define SY70206_I2CRDY_TIMEUS 2500
#define SY70206_INT1_TS_BIT BIT(2)
/* Settings value to disable auto headroom */
#define SY70206_CONF_DISABLE_AUTO_HR 0x09

/*
 *  REGULATOR_MODE_INVALID (0x0): Don't override regulator mode
 *  REGULATOR_MODE_FAST (0x1): Force enable FPWM mode
 *  REGULATOR_MODE_NORMAL (0x2): Force enable PFM mode
 */
static int force_mode = REGULATOR_MODE_INVALID;
module_param(force_mode, int, 0644);
MODULE_PARM_DESC(force_mode,
		"Force the regulator to operate in the specific mode, overriding the set mode");

struct sy70206_priv {
	struct device *dev;
	struct regulator_desc desc;
	struct gpio_desc *cs_gpio;
	struct regmap *regmap;
	int devid;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	uint8_t read_data;
#endif
};

static int sy70206_get_interrupt_status(struct sy70206_priv *priv,
	unsigned int *int1_val, unsigned int *int2_val)
{
	int ret;

	ret = regmap_read(priv->regmap, SY70206_REG_INT1, int1_val);
	if (ret)
		return ret;

	ret = regmap_read(priv->regmap, SY70206_REG_INT2, int2_val);
	if (ret)
		return ret;

	return ret;
}

static int sy70206_check_status(struct regulator_dev *rdev)
{
	struct sy70206_priv *priv = rdev_get_drvdata(rdev);
	unsigned int int1_val;
	unsigned int int2_val;
	int ret;

	ret = sy70206_get_interrupt_status(priv, &int1_val, &int2_val);
	if (ret) {
		dev_err(priv->dev, "check interrupt status failed! (%d)\n", ret);
		return ret;
	}

	if (int1_val) {
		dev_warn(priv->dev, "INT1 status toggled: %#x\n", int1_val);
		if (int1_val & SY70206_INT1_TS_BIT)
			dev_err(priv->dev, "Thermal shutdown occurred, regulator might not function!\n");
	}

	if (int2_val)
		dev_warn(priv->dev, "INT2 status toggled: %#x\n", int2_val);

	return ret;
}

static int sy70206_regulator_enable(struct regulator_dev *rdev)
{
	sy70206_check_status(rdev);
	return regulator_enable_regmap(rdev);
}

static int sy70206_regulator_disable(struct regulator_dev *rdev)
{
	sy70206_check_status(rdev);
	return regulator_disable_regmap(rdev);
}

static int sy70206_regulator_is_enabled(struct regulator_dev *rdev)
{
	sy70206_check_status(rdev);
	return regulator_is_enabled_regmap(rdev);
}

static unsigned int sy70206_of_map_mode(unsigned int mode)
{
	switch (mode) {
	case REGULATOR_MODE_FAST:
		return REGULATOR_MODE_FAST;
	case REGULATOR_MODE_NORMAL:
		return REGULATOR_MODE_NORMAL;
	}

	return REGULATOR_MODE_INVALID;
}

static int sy70206_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct sy70206_priv *priv = rdev_get_drvdata(rdev);
	unsigned int mode_override = REGULATOR_MODE_INVALID;
	unsigned int mode_val;

	mode_override = sy70206_of_map_mode(force_mode);
	if (mode_override != REGULATOR_MODE_INVALID) {
		mode = mode_override;
		dev_info(priv->dev, "force regulator set mode into %d\n", mode_override);
	}

	switch (mode) {
	case REGULATOR_MODE_FAST:
		mode_val = SY70206_MODE_FPWM;
		break;
	case REGULATOR_MODE_NORMAL:
		mode_val = SY70206_MODE_AUTO;
		break;
	default:
		dev_err(&rdev->dev, "mode not supported\n");
		return -EINVAL;
	}

	sy70206_check_status(rdev);
	return regmap_update_bits(priv->regmap, SY70206_REG_CTRL,
				  SY70206_MODE_MASK, mode_val);
}

static unsigned int sy70206_get_mode(struct regulator_dev *rdev)
{
	struct sy70206_priv *priv = rdev_get_drvdata(rdev);
	unsigned int val;
	int ret;

	sy70206_check_status(rdev);
	ret = regmap_read(priv->regmap, SY70206_REG_CTRL, &val);
	if (ret)
		return ret;

	if ((val & SY70206_MODE_MASK) == SY70206_MODE_FPWM)
		return REGULATOR_MODE_FAST;

	return REGULATOR_MODE_NORMAL;
}

static const struct regulator_ops sy70206_regulator_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = sy70206_regulator_enable,
	.disable = sy70206_regulator_disable,
	.is_enabled = sy70206_regulator_is_enabled,
	.set_mode = sy70206_set_mode,
	.get_mode = sy70206_get_mode,
};

static bool sy70206_is_accessible_reg(struct device *dev, unsigned int reg)
{
	return (reg >= SY70206_REG_START && reg <= SY70206_REG_END);
}

static bool sy70206_is_volatile_reg(struct device *dev, unsigned int reg)
{
	return (reg >= SY70206_REG_INT1 && reg <= SY70206_REG_DEVID);
}

static ssize_t devid_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct sy70206_priv *priv = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%#x\n", priv->devid);
}

static DEVICE_ATTR_RO(devid);

static struct attribute *attrs[] = {
	&dev_attr_devid.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static const struct regmap_config sy70206_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SY70206_REG_END,
	.cache_type = REGCACHE_NONE,
	.writeable_reg = sy70206_is_accessible_reg,
	.readable_reg = sy70206_is_accessible_reg,
	.volatile_reg = sy70206_is_volatile_reg,
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int sy70206_debugfs_reg_read_read(void *data, u64 *val)
{
	struct sy70206_priv *priv = data;

	*val = priv->read_data;

	return 0;
}

static int sy70206_debugfs_reg_read_write(void *data, u64 addr)
{
	struct sy70206_priv *priv = data;
	int ret;
	int read_data;

	ret = regmap_read(priv->regmap, addr, &read_data);

	if (ret)
		dev_err(priv->dev, "regmap_read failed: %d\n", ret);
	else
		priv->read_data = read_data;

	return 0;
}

static ssize_t sy70206_debugfs_reg_write_write(struct file *filp,
						const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct sy70206_priv *priv = filp->private_data;
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

	ret = regmap_write(priv->regmap, addr, data);

	if (ret)
		dev_err(priv->dev, "regmap_write failed: %d\n", ret);

	return count;
}

/* reg_read file is R/W able. */
DEFINE_DEBUGFS_ATTRIBUTE(fops_reg_read, sy70206_debugfs_reg_read_read,
			 sy70206_debugfs_reg_read_write, "0x%llx\n");
/* reg_write file is writeable. */
static const struct file_operations fops_reg_write = {
	.open = simple_open,
	.write = sy70206_debugfs_reg_write_write,
};

static int sy70206_debugfs_init(struct i2c_client *i2c)
{
	struct dentry *dentry;
	char dir_name[32];
	struct sy70206_priv *priv = dev_get_drvdata(&i2c->dev);

	scnprintf(dir_name, sizeof(dir_name), "%s-%s", DEVICE_NAME,
		 dev_name(&i2c->adapter->dev));

	if (!debugfs_initialized())
		return -ENODEV;

	dentry = debugfs_create_dir(dir_name, NULL);

	debugfs_create_file("reg_read", 0660, dentry, priv, &fops_reg_read);
	debugfs_create_file("reg_write", 0220, dentry, priv, &fops_reg_write);

	return 0;
}

#endif

static int sy70206_probe(struct i2c_client *i2c)
{
	struct sy70206_priv *priv;
	struct regulator_config regulator_cfg = {};
	struct regulator_dev *rdev;
	unsigned int devid;
	int ret;

	priv = devm_kzalloc(&i2c->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->cs_gpio =
		devm_gpiod_get_optional(&i2c->dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->cs_gpio)) {
		dev_err(&i2c->dev, "Failed to get 'enable' gpio\n");
		return PTR_ERR(priv->cs_gpio);
	}

	usleep_range(SY70206_I2CRDY_TIMEUS, SY70206_I2CRDY_TIMEUS + 100);

	priv->regmap = devm_regmap_init_i2c(i2c, &sy70206_regmap_config);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(&i2c->dev, "Failed to init regmap (%d)\n", ret);
		return ret;
	}

	ret = regmap_read(priv->regmap, SY70206_REG_DEVID, &devid);
	if (ret)
		return ret;

	if (of_property_read_bool(i2c->dev.of_node, "google,disable-auto-headroom")) {
		ret = regmap_write(priv->regmap, SY70206_REG_CONF, SY70206_CONF_DISABLE_AUTO_HR);
		if (ret) {
			dev_err(&i2c->dev, "Failed to disable auto headroom (%d)\n", ret);
			return ret;
		}
		dev_info(&i2c->dev, "Buck-boost auto headroom disabled.\n");
	}

	/* Use for get driver data */
	i2c_set_clientdata(i2c, priv);
	dev_info(&i2c->dev, " Device ID = %#x\n", devid);
	priv->devid = devid;
	priv->dev = &i2c->dev;

	priv->desc.name = "sy70206-regulator";
	priv->desc.n_voltages = SY70206_N_VOUTS;
	priv->desc.ops = &sy70206_regulator_ops;
	priv->desc.type = REGULATOR_VOLTAGE;
	priv->desc.owner = THIS_MODULE;

	priv->desc.min_uV = SY70206_VOUT_MINUV;
	priv->desc.uV_step = SY70206_VOUT_STPUV;
	priv->desc.vsel_reg = SY70206_REG_VOUT1;
	priv->desc.vsel_mask = SY70206_VSEL_MASK;
	priv->desc.enable_reg = SY70206_REG_CTRL;
	priv->desc.enable_mask = SY70206_ENABLE_MASK;

	priv->desc.of_map_mode = sy70206_of_map_mode;

	regulator_cfg.dev = &i2c->dev;
	regulator_cfg.of_node = i2c->dev.of_node;
	regulator_cfg.regmap = priv->regmap;
	regulator_cfg.driver_data = priv;
	regulator_cfg.init_data = of_get_regulator_init_data(
		&i2c->dev, i2c->dev.of_node, &priv->desc);

	rdev = devm_regulator_register(&i2c->dev, &priv->desc, &regulator_cfg);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "Failed to register regulator\n");
		return PTR_ERR(rdev);
	}
	sy70206_check_status(rdev);

	ret = sysfs_create_group(&priv->dev->kobj, &attr_group);
	if (ret)
		dev_err(&i2c->dev, "Failed to create attribute group: %d\n",
			ret);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	return sy70206_debugfs_init(i2c);
#endif

	return 0;
}

static const struct of_device_id __maybe_unused sy70206_of_match_table[] = {
	{
	.compatible = "sy,sy70206",
	},
	{}};
MODULE_DEVICE_TABLE(of, sy70206_of_match_table);

static struct i2c_driver sy70206_driver = {
	.driver = {
		.name = "sy70206",
		.of_match_table = sy70206_of_match_table,
	},
	.probe = sy70206_probe,
};
module_i2c_driver(sy70206_driver);

MODULE_DESCRIPTION("Silergy SY70206 regulator driver");
MODULE_AUTHOR("Xu Han <xuhanyz@google.com>");
MODULE_LICENSE("GPL");
