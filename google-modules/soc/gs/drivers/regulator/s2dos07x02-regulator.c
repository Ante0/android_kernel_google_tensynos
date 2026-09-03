// SPDX-License-Identifier: MIT

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>


#define S2DOS07X02_REG_BUCK_VOUT 0x03
#define S2DOS07X02_REG_BUCK_EN 0x04
#define S2DOS07X02_MIN_UV 850000
#define S2DOS07X02_MAX_UV 2100000
#define S2DOS07X02_STEP_UV 12500
#define S2DOS07X02_N_VOUTS ((S2DOS07X02_MAX_UV - S2DOS07X02_MIN_UV) / S2DOS07X02_STEP_UV + 1)
#define S2DOS07X02_BUCK_VOUT_DEFAULT 0x9B

struct s2dos07x02_pmic {
	struct device *dev;
	struct dentry *dentry;
	struct i2c_client *i2c;
	const struct regulator_desc *desc;
	bool enabled;
	u8 vout;
};

static int s2dos07x02_is_enabled(struct regulator_dev *rdev)
{
	struct s2dos07x02_pmic *pmic = rdev_get_drvdata(rdev);

	return pmic->enabled;
}

static int s2dos07x02_enable(struct regulator_dev *rdev)
{
	struct s2dos07x02_pmic *pmic = rdev_get_drvdata(rdev);
	int ret;

	if (WARN(pmic->enabled, "vout already enabled\n"))
		return 0;

	dev_dbg(pmic->dev, "enabling\n");
	pmic->enabled = true;
	ret = i2c_smbus_write_byte_data(pmic->i2c, S2DOS07X02_REG_BUCK_EN, 0x01);
	if (ret < 0) {
		dev_warn(pmic->dev, "failed to set enable register (ret=%d)\n", ret);
		if (ret == -ETIMEDOUT)
			return 0;
		else
			return ret;
	}

	usleep_range(200, 210);
	return i2c_smbus_write_byte_data(pmic->i2c, S2DOS07X02_REG_BUCK_VOUT, pmic->vout);
}

static int s2dos07x02_disable(struct regulator_dev *rdev)
{
	struct s2dos07x02_pmic *pmic = rdev_get_drvdata(rdev);
	int ret;

	if (WARN(!pmic->enabled, "vout already disabled\n"))
		return 0;

	dev_dbg(pmic->dev, "disabling\n");
	pmic->enabled = false;
	ret = i2c_smbus_write_byte_data(pmic->i2c, S2DOS07X02_REG_BUCK_EN, 0x00);
	if (ret < 0) {
		dev_warn(pmic->dev, "failed to set disable register (ret=%d)\n", ret);
		if (ret == -ETIMEDOUT)
			return 0;
	}

	return ret;
}

static int s2dos07x02_get_voltage_sel(struct regulator_dev *rdev)
{
	struct s2dos07x02_pmic *pmic = rdev_get_drvdata(rdev);
	const struct regulator_ops *ops = pmic->desc->ops;
	int vout = i2c_smbus_read_byte_data(pmic->i2c, S2DOS07X02_REG_BUCK_VOUT);
	int voltage;

	if (vout < 0) {
		dev_warn(pmic->dev, "failed to read vout (ret=%d)\n", vout);
		if (vout != -ETIMEDOUT)
			return vout;
		vout = pmic->vout;
	}

	voltage = ((vout - S2DOS07X02_BUCK_VOUT_DEFAULT) * S2DOS07X02_STEP_UV) + S2DOS07X02_MIN_UV;

	return ops->map_voltage(rdev, voltage, voltage);
}

static int s2dos07x02_set_voltage_sel(struct regulator_dev *rdev, unsigned int selector)
{
	struct s2dos07x02_pmic *pmic = rdev_get_drvdata(rdev);
	const struct regulator_ops *ops = pmic->desc->ops;
	int ret;
	int voltage;
	u8 vout;

	voltage = ops->list_voltage(rdev, selector);
	vout = S2DOS07X02_BUCK_VOUT_DEFAULT + ((voltage - S2DOS07X02_MIN_UV) / S2DOS07X02_STEP_UV);

	dev_dbg(pmic->dev, "setting %d uv(%#02X)\n", voltage, vout);
	pmic->vout = vout;
	ret = i2c_smbus_write_byte_data(pmic->i2c, S2DOS07X02_REG_BUCK_VOUT, vout);
	if (ret < 0) {
		dev_warn(pmic->dev, "failed to set vout (ret=%d)\n", ret);
		if (ret == -ETIMEDOUT)
			return 0;
	}

	return ret;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
#define S2DOS07X02_AP_REGS_NUM 9
#define S2DOS07X02_DDI_REGS_NUM 8

static int s2dos07x02_reg_dump_show(struct seq_file *m, void *data)
{
	struct regulator_dev *rdev = m->private;
	struct s2dos07x02_pmic *pmic = rdev_get_drvdata(rdev);
	int i;

	if (!pmic->enabled) {
		dev_info(pmic->dev, "regulator is not enabled\n");
		return -EPERM;
	}

	/* Interface with AP */
	for (i = 0; i < S2DOS07X02_AP_REGS_NUM; i++) {
		static const u8 ap_regs[S2DOS07X02_AP_REGS_NUM] = { 0x02, 0x03, 0x04, 0x06, 0x07,
								    0x08, 0x09, 0x0A, 0x0B };
		const u8 read_back = i2c_smbus_read_byte_data(pmic->i2c, ap_regs[i]);

		seq_printf(m, "%#02x: %#02x\n", ap_regs[i], read_back);
	}

	/* Interface with DDI */
	i2c_smbus_write_byte_data(pmic->i2c, 0x0C, 0x01);
	i2c_smbus_write_byte_data(pmic->i2c, 0x0D, 0x01);
	for (i = 0; i < S2DOS07X02_DDI_REGS_NUM; i++) {
		static const u8 ddi_regs[S2DOS07X02_DDI_REGS_NUM] = { 0x20, 0x21, 0x22, 0x23,
								      0x24, 0x25, 0x26, 0x28 };
		const u8 read_back = i2c_smbus_read_byte_data(pmic->i2c, ddi_regs[i]);

		seq_printf(m, "%#02x: %#02x\n", ddi_regs[i], read_back);
	}
	i2c_smbus_write_byte_data(pmic->i2c, 0x0C, 0x00);
	i2c_smbus_write_byte_data(pmic->i2c, 0x0D, 0x00);

	return 0;
}

static int s2dos07x02_reg_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, s2dos07x02_reg_dump_show, inode->i_private);
}

static const struct file_operations s2dos07x02_reg_dump_fops = {
	.open = s2dos07x02_reg_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static void debugfs_add_s2dos07x02_entries(struct regulator_dev *rdev, struct dentry *parent)
{
	struct s2dos07x02_pmic *pmic = rdev_get_drvdata(rdev);

	pmic->dentry =
		debugfs_create_file("reg_dump", 0400, parent, rdev, &s2dos07x02_reg_dump_fops);
}

static void debugfs_remove_s2dos07x02_entries(struct s2dos07x02_pmic *pmic)
{
	debugfs_remove(pmic->dentry);
}
#endif

static const struct regulator_ops s2dos07x02_buck_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.is_enabled = s2dos07x02_is_enabled,
	.enable = s2dos07x02_enable,
	.disable = s2dos07x02_disable,
	.get_voltage_sel = s2dos07x02_get_voltage_sel,
	.set_voltage_sel = s2dos07x02_set_voltage_sel,
};

static const struct regulator_desc s2dos07x02_desc = {
	.name = "s2dos07x02-pmic",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.min_uV = S2DOS07X02_MIN_UV,
	.uV_step = S2DOS07X02_STEP_UV,
	.n_voltages = S2DOS07X02_N_VOUTS,
	.ops = &s2dos07x02_buck_reg_ops,
};

static struct regulator_init_data s2dos07x02_data = {
	.constraints = {
		.min_uV = S2DOS07X02_MIN_UV,
		.max_uV = S2DOS07X02_MAX_UV,
		.valid_ops_mask = REGULATOR_CHANGE_VOLTAGE | REGULATOR_CHANGE_STATUS,
	},
};

static int s2dos07x02_probe(struct i2c_client *i2c)
{
	struct s2dos07x02_pmic *pmic;
	struct regulator_config config = {};
	struct regulator_dev *rdev;
	int data;

	pmic = devm_kzalloc(&i2c->dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	i2c_set_clientdata(i2c, pmic);
	pmic->dev = &i2c->dev;
	pmic->i2c = i2c;
	pmic->desc = &s2dos07x02_desc;

	pmic->i2c->adapter->timeout = 50;
	pmic->i2c->adapter->retries = 2;

	config.dev = &i2c->dev;
	config.of_node = i2c->dev.of_node;
	config.driver_data = pmic;
	config.init_data = &s2dos07x02_data;

	rdev = devm_regulator_register(&i2c->dev, pmic->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err(&i2c->dev, "regulator registration failed\n");
		return PTR_ERR(rdev);
	}

	data = i2c_smbus_read_byte_data(pmic->i2c, S2DOS07X02_REG_BUCK_EN);
	if (data < 0)
		return data;
	pmic->enabled = data ? true : false;

	data = i2c_smbus_read_byte_data(pmic->i2c, S2DOS07X02_REG_BUCK_VOUT);
	if (data < 0)
		return data;
	pmic->vout = data;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	debugfs_add_s2dos07x02_entries(rdev, rdev->debugfs);
#endif
	return 0;
}

static void s2dos07x02_remove(struct i2c_client *i2c)
{
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct s2dos07x02_pmic *pmic = i2c_get_clientdata(i2c);

	debugfs_remove_s2dos07x02_entries(pmic);
#endif
}

static const struct of_device_id __maybe_unused s2dos07x02_of_match_table[] = {
	{ .compatible = "samsung,s2dos07x02", },
	{}
};
MODULE_DEVICE_TABLE(of, s2dos07x02_of_match_table);

static struct i2c_driver s2dos07x02_driver = {
	.driver = {
		.name = "s2dos07x02",
		.of_match_table = s2dos07x02_of_match_table,
	},
	.probe = s2dos07x02_probe,
	.remove = s2dos07x02_remove,
};
module_i2c_driver(s2dos07x02_driver);

MODULE_AUTHOR("Jeremy DeHaan <jdehaan@google.com>");
MODULE_DESCRIPTION("I2C based PMIC Driver");
MODULE_LICENSE("Dual MIT/GPL");
