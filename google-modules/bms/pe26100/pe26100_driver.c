// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for PE26100 MFD charger driver
 */

#if IS_ENABLED(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif /* CONFIG_DEBUG_FS */

#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "google_bms.h"
#include "pe26100_driver.h"
#include "pe26100_usecase.h"
#include <misc/gvotable.h>

struct pe26100_chg_default_reg startup_charger_reg[] = {
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BACK_REG_UNLOCK_1, 0xA5) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_BACK_REG_UNLOCK_2, 0x96) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_1, 0xC0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_2, 0x08) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_3, 0xC0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_4, 0x12) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_5, 0x87) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_MODE, 0x80) },
};

struct pe26100_chg_default_reg reset_precharging_reg[] = {
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_1, 0x0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_2, 0x0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_3, 0x0) },
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_STARTUP_4, 0x32) },
};

const struct pe26100_chg_default_reg reg_pre_prod_fixups[] = {
	{ PE26100_CHG_INIT_DEFAULT_REG(PE26100_CHG_EFF_LOSS, 0xC8) },
};

static void pe26100_chg_mode_lock(struct device *dev)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->mode_lock);
}

static void pe26100_chg_mode_unlock(struct device *dev)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	mutex_unlock(&data->mode_lock);
}

int pe26100_chg_reg_read(struct device *dev, uint8_t reg, uint8_t *val)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);
	int ret, ival;

	ret = regmap_read(data->regmap, reg, &ival);
	if (ret == 0)
		*val = 0xFF & ival;

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_chg_reg_read);

int pe26100_chg_reg_readn(struct device *dev, uint8_t reg, uint8_t *val, int count)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);
	int ret;

	return regmap_bulk_read(data->regmap, reg, val, count);

	return ret;

}
EXPORT_SYMBOL_GPL(pe26100_chg_reg_readn);

int pe26100_chg_reg_write(struct device *dev, uint8_t reg, uint8_t val)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	return regmap_write(data->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(pe26100_chg_reg_write);

int pe26100_chg_reg_writen(struct device *dev, uint8_t reg, uint8_t *val, int count) /* NOTYPO */
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	return regmap_bulk_write(data->regmap, reg, val, count);
}
EXPORT_SYMBOL_GPL(pe26100_chg_reg_writen); /* NOTYPO */

int pe26100_chg_reg_update(struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	return regmap_update_bits(data->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(pe26100_chg_reg_update);

/* -------------------------------------------------------------------------------------------*/

static int pe26100_chg_get_current_mode(struct device *dev)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	return data->cur_mode;
}

/* must have pe26100_chg_mode_lock(dev); */
static void pe26100_chg_set_current_mode(struct device *dev, enum pe26100_chg_mode mode)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	data->cur_mode = mode;
}

int pe26100_chg_set_online(struct device *dev, bool online, enum pe26100_chg_mode mode)
{
	int ret = 0;
	enum pe26100_chg_mode cur_mode;

	pe26100_chg_mode_lock(dev);

	cur_mode = pe26100_chg_get_current_mode(dev);

	if ((cur_mode == mode) || (cur_mode == PE26100_CHG_MODE_INVALID)) {
		pe26100_chg_set_current_mode(dev, online ? mode : PE26100_CHG_MODE_INVALID);
		goto unlock;
	}

	dev_err(dev, "%s: Error: mode is already set cur_mode:%d mode:%d online:%d\n", __func__,
		cur_mode, mode, online);
	ret = -EPERM;
unlock:
	pe26100_chg_mode_unlock(dev);
	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_chg_set_online);

bool pe26100_chg_is_online(struct device *dev, enum pe26100_chg_mode mode)
{
	bool online;

	pe26100_chg_mode_lock(dev);
	online = pe26100_chg_get_current_mode(dev) == mode;
	pe26100_chg_mode_unlock(dev);

	return online;
}
EXPORT_SYMBOL_GPL(pe26100_chg_is_online);

int pe26100_chg_read_vbatt(struct device *dev, int *vbatt)
{
	uint16_t reg;
	int ret;

	ret = pe26100_chg_reg_readn(dev, PE26100_CHG_VBATT_ADC_LB, (uint8_t *)&reg, 2);
	if (ret < 0)
		return ret;

	*vbatt = (reg >> PE26100_CHG_VBATT_ADC_LB_SHIFT) * 5000; /* 5mV step size */

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_chg_read_vbatt);

int pe26100_chg_current_now(struct device *dev, int *iic, enum pe26100_chg_mode mode)
{
	uint16_t reg;
	int ret;

	if (!pe26100_chg_is_online(dev, mode)) {
		*iic = 0;
		return 0;
	}

	ret = pe26100_chg_reg_readn(dev, PE26100_CHG_IIN_ADC_LB, (uint8_t *)&reg, 2);
	if (ret < 0)
		return ret;

	*iic = (reg >> PE26100_CHG_IIN_ADC_LB_SHIFT) * 6000; /* 6ma step size */

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_chg_current_now);

int pe26100_chg_read_vout(struct device *dev, int *vout)
{
	uint16_t reg;
	int ret;

	ret = pe26100_chg_reg_readn(dev, PE26100_CHG_VOUT_ADC_LB, (uint8_t *)&reg, 2);
	if (ret < 0)
		return ret;

	*vout = (reg >> 6) * 5000; /* 5mV step size */

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_chg_read_vout);

int pe26100_chg_read_vin(struct device *dev, int *vin)
{
	uint16_t reg;
	int ret;

	ret = pe26100_chg_reg_readn(dev, PE26100_CHG_VIN_ADC_LB, (uint8_t *)&reg, 2);
	if (ret < 0)
		return ret;

	*vin = (reg >> 6) * 20000; /* 20mV step size */

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_chg_read_vin);

int pe26100_chg_read_temp(struct device *dev, int *temp)
{
	uint16_t reg;
	int ret;

	ret = pe26100_chg_reg_read(dev, PE26100_CHG_TEMP_ADC, (uint8_t *)&reg);
	if (ret < 0)
		return ret;

	*temp = reg;

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_chg_read_temp);


int pe26100_chg_set_charge_enabled(struct device *dev, bool enabled, enum pe26100_chg_mode mode)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	if (!pe26100_chg_is_online(dev, mode))
		return -EPERM;

	data->chg_enabled = enabled;

	return 0;
}
EXPORT_SYMBOL_GPL(pe26100_chg_set_charge_enabled);

int pe26100_chg_get_charge_enabled(struct device *dev, int *enabled, enum pe26100_chg_mode mode)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	if (!pe26100_chg_is_online(dev, mode)) {
		*enabled = 0;
		return 0;
	}

	*enabled = !data->chg_hard_disabled && data->chg_enabled;
	return 0;
}
EXPORT_SYMBOL_GPL(pe26100_chg_get_charge_enabled);

int pe26100_chg_set_charge_disabled(struct device *dev, bool enabled)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	data->chg_hard_disabled = enabled;

	return 0;
}
EXPORT_SYMBOL_GPL(pe26100_chg_set_charge_disabled);

uint8_t pe26100_get_chip_rev(struct device *dev)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	return data->chip_rev;
}
EXPORT_SYMBOL_GPL(pe26100_get_chip_rev);

/* -------------------------------------------------------------------------------------------*/

int pe26100_chg_apply_default_reg_config(struct device *dev,
					 enum pe26100_chg_reg_profile_id id)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);
	struct pe26100_chg_default_reg *cnfg = data->registered_configs[id].profile;
	const int len = data->registered_configs[id].len;
	int i, ret;

	for (i = 0; i < len; i++) {
		dev_dbg(dev, "Writing reg 0x%x val:0x%x\n", cnfg[i].reg, cnfg[i].val);
		ret = pe26100_chg_reg_write(dev, cnfg[i].reg, cnfg[i].val);
		if (ret) {
			dev_err(dev, "Error writing reg:0x%x val:0x%x ret:%d\n", cnfg[i].reg,
				cnfg[i].val, ret);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pe26100_chg_apply_default_reg_config);

static struct pe26100_chg_default_reg *pe26100_chg_find_reg_config(struct pe26100_chg_data *data,
		uint8_t reg,
		enum pe26100_chg_reg_profile_id id)
{
	struct pe26100_chg_default_reg *cnfg = data->registered_configs[id].profile;
	const int len = data->registered_configs[id].len;
	int i;

	for (i = 0; i < len; i++) {
		if (reg == cnfg[i].reg)
			return &(cnfg[i]);
	}

	return ERR_PTR(-EINVAL);
}

uint8_t pe26100_chg_find_reg_config_val(struct device *dev,
					enum pe26100_chg_reg_profile_id id,
					uint8_t reg)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);
	struct pe26100_chg_default_reg *config;

	config = pe26100_chg_find_reg_config(data, reg, id);

	if (!IS_ERR(config))
		return config->val;

	return PTR_ERR(config);
}
EXPORT_SYMBOL_GPL(pe26100_chg_find_reg_config_val);

int pe26100_chg_apply_reg_fixups(struct device *dev,
				 enum pe26100_chg_reg_profile_id id,
				 const struct pe26100_chg_default_reg *fixup_regs,
				 size_t fixup_regs_size)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);
	struct pe26100_chg_default_reg *found_cnfg;
	int i;

	for (i = 0; i < fixup_regs_size / sizeof(struct pe26100_chg_default_reg); i++) {
		found_cnfg = pe26100_chg_find_reg_config(data, fixup_regs[i].reg, id);
		if (!IS_ERR(found_cnfg)) {
			dev_info(dev, "Applying reg fix: reg:0x%x val:0x%x->0x%x\n",
				      found_cnfg->reg, found_cnfg->val, fixup_regs[i].val);
			found_cnfg->val = fixup_regs[i].val;
		} else {
			dev_err(dev, "Error could not find fixup reg in config reg:0x%x\n",
				fixup_regs[i].val);
			return -EINVAL;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(pe26100_chg_apply_reg_fixups);

int pe26100_chg_register_reg_profile(struct device *dev,
				     enum pe26100_chg_reg_profile_id id,
				     struct pe26100_chg_default_reg cnfg[],
				     ssize_t len)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);

	data->registered_configs[id].profile = cnfg;
	data->registered_configs[id].len = len;

	/* reg_pre_prod_fixups only needs to be applied if non-prod chip */
	if (!data->is_preprod || id >= PE26100_CHG_REG_PROFILE_SETUP_CHARGER)
		return 0;

	return pe26100_chg_apply_reg_fixups(dev, id, reg_pre_prod_fixups,
					    sizeof(reg_pre_prod_fixups));
}
EXPORT_SYMBOL_GPL(pe26100_chg_register_reg_profile);

/* -------------------------------------------------------------------------------------------*/

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int read_reg(void *d, u64 *val)
{
	struct pe26100_chg_data *data = d;
	int rc;
	uint8_t temp;

	rc = pe26100_chg_reg_read(data->dev, data->debug_address, &temp);
	if (rc) {
		dev_err(data->dev, "Couldn't read reg %x rc = %d\n",
			data->debug_address, rc);
		return -EAGAIN;
	}

	*val = temp;

	return 0;
}

static int write_reg(void *d, u64 val)
{
	struct pe26100_chg_data *data = d;
	int rc;
	u8 temp;

	temp = (u8) val;

	rc = pe26100_chg_reg_write(data->dev, data->debug_address, temp);
	if (rc) {
		dev_err(data->dev, "Couldn't write %#02x to %#02x rc = %d\n",
			temp, data->debug_address, rc);
		return -EAGAIN;
	}
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(register_debug_ops_pe26100, read_reg, write_reg, "%#02llx\n");

static int pe26100_init_debugfs(struct pe26100_chg_data *data)
{
	data->de = debugfs_create_dir("pe26100_driver", NULL);
	if (IS_ERR_OR_NULL(data->de)) {
		dev_err(data->dev, "Couldn't create debug dir\n");
		return -ENOENT;
	}

	debugfs_create_file("data", 0644, data->de, data, &register_debug_ops_pe26100);
	debugfs_create_x32("addr", 0644, data->de, &data->debug_address);

	return 0;
}
#endif /* DEBUGFS */

bool pe26100_is_es8_compat(struct device *dev)
{
	const int chip_ver = pe26100_get_chip_rev(dev);

	return (chip_ver != PE26100_CHG_DIE_REV_VAL_ES6) &&
	       (chip_ver != PE26100_CHG_DIE_REV_VAL_ES7);
}
EXPORT_SYMBOL_GPL(pe26100_is_es8_compat);

bool pe26100_is_es10(struct device *dev)
{
	const int chip_ver = pe26100_get_chip_rev(dev);

	return chip_ver == PE26100_CHG_DIE_REV_VAL_ES10;
}
EXPORT_SYMBOL_GPL(pe26100_is_es10);

static bool pe26100_is_reg(struct device *dev, unsigned int reg)
{
	switch(reg) {
	case PE26100_CHG_IC_ENABLE ... PE26100_CHG_IC_STATUS2:
	case PE26100_CHG_DIE_REV:
	case PE26100_CHG_CHIPID:
	case PE26100_CHG_PSEMI:
		return true;
	case PE26100_CHG_BUCK_CTRL:
	case PE26100_CHG_SECRET_1:
	case PE26100_CHG_SECRET_2:
	case PE26100_CHG_SECRET_3:
	case PE26100_CHG_CAP_BALANCE:
	case PE26100_CHG_FEED_FORWARD:
	case PE26100_CHG_BACK_REG_UNLOCK_1:
	case PE26100_CHG_BACK_REG_UNLOCK_2:
	case PE26100_CHG_STARTUP_1:
	case PE26100_CHG_STARTUP_2:
	case PE26100_CHG_STARTUP_3:
	case PE26100_CHG_STARTUP_4:
	case PE26100_CHG_STARTUP_5:
		return pe26100_is_es8_compat(dev);
	default:
		return false;
	}
}

int pe26100_dump_all_regs(struct device *dev)
{
	u8 tmp[0x100];
	int i, ret;
	char buf[256];
	int count = 0, reg_count = 0;

	ret = pe26100_chg_reg_readn(dev, PE26100_CHG_IC_ENABLE, tmp, sizeof(tmp));
	if (ret)
		return ret;

	for (i = 0; i < sizeof(tmp); i++) {
		if (!pe26100_is_reg(dev, i))
			continue;
		count += scnprintf(buf + count, sizeof(buf) - count, "%#02x:%#02x ", i, tmp[i]);
		if (reg_count && (reg_count % 25) == 0) {
			buf[count - 1] = '\0';
			dev_info(dev, "%s\n", buf);
			count = 0;
		}
		reg_count++;
	}
	buf[count - 1] = '\0';
	dev_info(dev, "%s\n", buf);

	return ret;
}
EXPORT_SYMBOL_GPL(pe26100_dump_all_regs);

static struct regmap_config pe26100_chg_regmap = {
	.name		= "pe26100",
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= PE26100_CHG_PSEMI,
	.readable_reg	= pe26100_is_reg,
	.volatile_reg	= pe26100_is_reg,
};

static const struct mfd_cell pe26100_devs[] = {
	{
		.name = "pe26100-buck-charger",
		.of_compatible = "pe26100-bc",
	},
	{
		.name = "pe26100-cp-charger",
		.of_compatible = "pe26100-cp",
	},
	{
		.name = "pe26100-usecase",
		.of_compatible = "pe26100-usecase",
	},
};

static int pe26100_init_chip_rev(struct pe26100_chg_data *data)
{
	return pe26100_chg_reg_read(data->dev, PE26100_CHG_DIE_REV, &data->chip_rev);
}

static int pe26100_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct pe26100_chg_data *data;
	struct regmap *regmap;
	int ret;

	regmap = devm_regmap_init_i2c(client, &pe26100_chg_regmap);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->dev->init_name = "pe26100_driver";
	data->regmap = regmap;
	i2c_set_clientdata(client, data);

	data->is_preprod = of_property_read_bool(dev->of_node, "pe26100,preprod");

	mutex_init(&data->mode_lock);

	ret = pe26100_init_chip_rev(data);
	if (ret) {
		dev_err(dev, "Error reading chip ID ret:%d\n", ret);
		return ret;
	}

	ret = pe26100_chg_register_reg_profile(dev,
					       PE26100_CHG_REG_PROFILE_SETUP_CHARGER,
					       startup_charger_reg,
					       (sizeof(startup_charger_reg) /
						sizeof(struct pe26100_chg_default_reg)));
	if (ret) {
		dev_err(dev, "Failed to register setup charger reg:%d\n", ret);
		return ret;
	}

	ret = pe26100_chg_register_reg_profile(dev,
					       PE26100_CHG_REG_PROFILE_RESET_PRECHARGING,
					       reset_precharging_reg,
					       (sizeof(reset_precharging_reg) /
						sizeof(struct pe26100_chg_default_reg)));
	if (ret) {
		dev_err(dev, "Failed to register reset precharging reg:%d\n", ret);
		return ret;
	}

	mfd_add_devices(data->dev, PLATFORM_DEVID_AUTO, pe26100_devs,
			ARRAY_SIZE(pe26100_devs), NULL, 0, NULL);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	ret = pe26100_init_debugfs(data);
	if (ret < 0)
		dev_warn(dev, "Failed to initialize debug fs\n");
#endif /* CONFIG_DEBUG_FS */

	return 0;
}

static void pe26100_remove(struct i2c_client *client)
{
	struct pe26100_chg_data *data = i2c_get_clientdata(client);

	debugfs_remove(data->de);
}

static ssize_t registers_dump_show(struct device *dev, struct device_attribute *attr,
				   char *buf)
{
	struct pe26100_chg_data *data = dev_get_drvdata(dev);
	int ret, i;
	int offset = 0;

	if (!data->regmap) {
		dev_err(dev, "Failed to read, no regmap\n");
		return -EIO;
	}

	for (i = 0; i < PE26100_CHG_PSEMI; i++) {
		int tmp;
		u32 reg_address = i + PE26100_CHG_IC_ENABLE;

		if (!pe26100_is_reg(dev, reg_address))
			continue;

		ret = regmap_read(data->regmap, reg_address, &tmp);
		if (ret < 0) {
			dev_err(dev, "[%s]: Failed to dump ret:%d\n", __func__, ret);
			break;
		}

		ret = sysfs_emit_at(buf, offset, "%02x: %02x\n", reg_address, tmp);
		if (!ret) {
			dev_err(dev, "[%s]: Not all registers printed. last:%x\n", __func__,
				reg_address - 1);
			break;
		}
		offset += ret;
	}

	return ret < 0 ? ret : offset;
}
static DEVICE_ATTR_RO(registers_dump);

static struct attribute *pe26100_attrs[] = {
	&dev_attr_registers_dump.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pe26100);

static const struct i2c_device_id pe26100_id[] = {
	{ "PE26100", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pe26100_id);

#if IS_ENABLED(CONFIG_OF)
static struct of_device_id pe26100_dt_ids[] = {
	{ .compatible = "pe26100",},
	{ },
};

MODULE_DEVICE_TABLE(of, pe26100_dt_ids);
#endif /* CONFIG_OF */

static struct i2c_driver pe26100_chg_driver = {
	.driver = {
		.name = "PE26100",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = pe26100_dt_ids,
#endif /* CONFIG_OF */
		.dev_groups = pe26100_groups,

	},
	.probe        = pe26100_probe,
	.remove       = pe26100_remove,
	.id_table     = pe26100_id,
};

module_i2c_driver(pe26100_chg_driver);
MODULE_DESCRIPTION("PE26100 Charger MFD Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
