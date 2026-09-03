// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2022 Google LLC
#include <linux/debugfs.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <ap-pmic/da9186.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>

/* 23 buck, 1 buck-boost, 59 ldo regulator */
#define MORRO_NUM_REGULATORS (23 + 1 + 59)
#define MORRO_M_NUM_REGULATORS (41)
#define MORRO_S_NUM_REGULATORS (42)

#define DEBUGFS_DENTRY_NAME "pmic"
#define USER_INPUT_BUF_SIZE (16)

#define MORRO_BUCK_DESC(_name, _id, _min, _step) {     \
	.name = (#_name),                              \
	.of_match = of_match_ptr(#_name),              \
	.regulators_node = of_match_ptr("regulators"), \
	.ops = &morro_buck_reg_ops,                    \
	.type = REGULATOR_VOLTAGE,                     \
	.id = (_id),                                   \
	.owner = THIS_MODULE,                          \
	.n_voltages = 256,                             \
	.min_uV = (_min),                              \
	.uV_step = (_step),                            \
	.of_map_mode = morro_reg_map_buck_mode,        \
}

/*
 * Most Morro LDOs are powered / sourced from the BUCK regulators.
 * The regulator framework supports the chained power source by setting the
 * "supply_name". But Morro chips already support VOUT Tracking (automatic
 * sub-regulation) that can auto adjust the BUCK Vout to fit the LDO's Vout
 * needs. To avoid conflict and interfere with other regulators, the driver
 * here is not setting the supply_name and will let the Morro HW logic handles
 * it.
 */
#define MORRO_LDO_DESC(_name, _id, _supply, _ops, _min, _step) { \
	.name = (#_name),                                \
	.of_match = of_match_ptr(#_name),                \
	.regulators_node = of_match_ptr("regulators"),   \
	.ops = &(_ops),                                  \
	.type = REGULATOR_VOLTAGE,                       \
	.id = (_id),                                     \
	.owner = THIS_MODULE,                            \
	.n_voltages = 256,                               \
	.min_uV = (_min),                                \
	.uV_step = (_step),                              \
}

static_assert(MB_PMIC_MORRO_REG_ID_MAX == MORRO_NUM_REGULATORS);

#ifdef CONFIG_DEBUG_FS
/* This driver provide control to 2 PMICs */
static const char *const pmic_names[] = {
	"da9186-pmic",
	"da9187-pmic",
};

/* Struct for each PMIC */
struct pmic_info {
	const char *name;
	struct device *dev;
	struct pmic_mfd_mbox mbox;
	u32 mb_dest_channel;
	u16 read_addr;
	u8 pmic_id;
};
#endif

/* Struct for entire device, hold struct for PMICs */
struct morro_device {
	struct device *dev;
	struct pmic_mfd_mbox mbox;
	/* DTS configurable parameters. */
	u32 mb_dest_channel;
	/* DebugFS PMIC register R/W */
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_root;
	size_t num_of_pmics;
	struct pmic_info pmic_info[ARRAY_SIZE(pmic_names)];
#endif
};

/*
 * Get / Set helpers for regulator control.
 */
int morro_reg_mb_get_value(struct regulator_dev *rdev, u16 reg_id, u8 cmd,
			   u32 *read_value)
{
	struct morro_device *drvdata = rdev_get_drvdata(rdev);
	struct mailbox_data req_data = { 0 }, resp_data;
	int ret;

	if (unlikely(!drvdata)) {
		dev_err(&rdev->dev, "Failed to get driver data\n");
		return -EINVAL;
	}

	ret = pmic_mfd_mbox_send_req_blocking_read(drvdata->dev,
						   &drvdata->mbox,
						   drvdata->mb_dest_channel,
						   MB_PMIC_TARGET_REGULATOR,
						   cmd, reg_id, req_data,
						   &resp_data);
	if (unlikely(ret))
		return ret;

	*read_value = resp_data.data[0];

	return ret;
}

int morro_reg_mb_set_value(struct regulator_dev *rdev, u16 reg_id, u8 cmd,
			   u32 value)
{
	struct morro_device *drvdata = rdev_get_drvdata(rdev);
	struct mailbox_data req_data = { 0 };

	if (unlikely(!drvdata)) {
		dev_err(&rdev->dev, "Failed to get driver data\n");
		return -EINVAL;
	}

	req_data.data[0] = value;
	return pmic_mfd_mbox_send_req_blocking(drvdata->dev, &drvdata->mbox,
					       drvdata->mb_dest_channel,
					       MB_PMIC_TARGET_REGULATOR, cmd,
					       reg_id, req_data);
}

/*
 * Morro regulator OPs.
 */
static int morro_reg_get_voltage_sel(struct regulator_dev *rdev)
{
	u32 selector;
	int ret = morro_reg_mb_get_value(rdev, rdev->desc->id,
					 MB_REG_CMD_GET_VOLTAGE_SEL, &selector);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s return volt sel: %u\n",
		rdev_get_name(rdev), selector);
	return selector;
}

static int morro_reg_set_voltage_sel(struct regulator_dev *rdev,
				     unsigned int selector)
{
	int ret = morro_reg_mb_set_value(rdev, rdev->desc->id,
					 MB_REG_CMD_SET_VOLTAGE_SEL, selector);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s set volt sel: %u\n",
		rdev_get_name(rdev), selector);
	return 0;
}

static int morro_reg_is_enabled(struct regulator_dev *rdev)
{
	u32 enabled;
	int ret = morro_reg_mb_get_value(rdev, rdev->desc->id,
					 MB_REG_CMD_GET_IS_ENABLED, &enabled);

	if (unlikely(ret))
		return ret;
	return enabled;
}

static int morro_reg_enable(struct regulator_dev *rdev)
{
	int ret = morro_reg_mb_set_value(rdev, rdev->desc->id,
					 MB_REG_CMD_SET_ENABLE_DISABLE, 1);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s Enabled\n", rdev_get_name(rdev));
	return 0;
}

static int morro_reg_disable(struct regulator_dev *rdev)
{
	int ret = morro_reg_mb_set_value(rdev, rdev->desc->id,
					 MB_REG_CMD_SET_ENABLE_DISABLE, 0);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s Disabled\n", rdev_get_name(rdev));
	return 0;
}

static int morro_reg_get_bypass(struct regulator_dev *rdev, bool *enable)
{
	u32 bypass;
	int ret = morro_reg_mb_get_value(rdev, rdev->desc->id,
					 MB_REG_CMD_GET_BYPASS, &bypass);

	if (unlikely(ret))
		return ret;
	*enable = (bool)bypass;
	dev_dbg(&rdev->dev, "Regulator %s Bypass: %u\n", rdev_get_name(rdev),
		bypass);
	return ret;
}

static int morro_reg_set_bypass(struct regulator_dev *rdev, bool enable)
{
	int ret = morro_reg_mb_set_value(rdev, rdev->desc->id,
					 MB_REG_CMD_SET_BYPASS, enable);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s Set bypass: %u\n",
		rdev_get_name(rdev), enable);
	return 0;
}

static unsigned int morro_reg_get_mode(struct regulator_dev *rdev)
{
	u32 raw_mode;
	int ret = morro_reg_mb_get_value(rdev, rdev->desc->id,
					 MB_REG_CMD_GET_MODE, &raw_mode);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s return raw mode: %u\n",
		rdev_get_name(rdev), raw_mode);
	return (raw_mode == 0) ? REGULATOR_MODE_NORMAL : REGULATOR_MODE_STANDBY;
}

static int morro_reg_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	u32 raw_mode;
	int ret;

	if (unlikely(mode != REGULATOR_MODE_NORMAL &&
		     mode != REGULATOR_MODE_STANDBY))
		return -EINVAL;

	raw_mode = (mode == REGULATOR_MODE_NORMAL) ? 0 : 1;

	ret = morro_reg_mb_set_value(rdev, rdev->desc->id, MB_REG_CMD_SET_MODE,
				     raw_mode);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s set mode: %u\n", rdev_get_name(rdev),
		mode);
	return 0;
}

static unsigned int morro_reg_map_buck_mode(unsigned int mode)
{
	if (mode == REGULATOR_MODE_NORMAL || mode == REGULATOR_MODE_STANDBY)
		return mode;
	return REGULATOR_MODE_INVALID;
}

static const struct regulator_ops morro_buck_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = morro_reg_get_voltage_sel,
	.set_voltage_sel = morro_reg_set_voltage_sel,
	.is_enabled = morro_reg_is_enabled,
	.enable = morro_reg_enable,
	.disable = morro_reg_disable,
	.get_mode = morro_reg_get_mode,
	.set_mode = morro_reg_set_mode,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops morro_ldo_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = morro_reg_get_voltage_sel,
	.set_voltage_sel = morro_reg_set_voltage_sel,
	.is_enabled = morro_reg_is_enabled,
	.enable = morro_reg_enable,
	.disable = morro_reg_disable,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops morro_ldo_reg_bypass_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = morro_reg_get_voltage_sel,
	.set_voltage_sel = morro_reg_set_voltage_sel,
	.is_enabled = morro_reg_is_enabled,
	.enable = morro_reg_enable,
	.disable = morro_reg_disable,
	.get_bypass = morro_reg_get_bypass,
	.set_bypass = morro_reg_set_bypass,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

/* For the regulator that's monitor only, didn't support any change. */
static const struct regulator_ops morro_monitor_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = morro_reg_get_voltage_sel,
	.is_enabled = morro_reg_is_enabled,
	.get_mode = morro_reg_get_mode,
};

static const struct regulator_desc morro_regulators_desc[] = {
	/* Morro-M */
	MORRO_BUCK_DESC(buck_1m, MB_PMIC_MORRO_REG_ID_BUCK_1M, 240000, 5000),
	MORRO_BUCK_DESC(buck_2m, MB_PMIC_MORRO_REG_ID_BUCK_2M, 240000, 5000),
	MORRO_BUCK_DESC(buck_3m, MB_PMIC_MORRO_REG_ID_BUCK_3M, 240000, 5000),
	MORRO_BUCK_DESC(buck_4m, MB_PMIC_MORRO_REG_ID_BUCK_4M, 240000, 5000),
	MORRO_BUCK_DESC(buck_5m, MB_PMIC_MORRO_REG_ID_BUCK_5M, 240000, 5000),
	MORRO_BUCK_DESC(buck_6m, MB_PMIC_MORRO_REG_ID_BUCK_6M, 240000, 5000),
	MORRO_BUCK_DESC(buck_7m, MB_PMIC_MORRO_REG_ID_BUCK_7M, 240000, 5000),
	MORRO_BUCK_DESC(buck_8m, MB_PMIC_MORRO_REG_ID_BUCK_8M, 240000, 5000),
	MORRO_BUCK_DESC(buck_9m, MB_PMIC_MORRO_REG_ID_BUCK_9M, 240000, 5000),
	MORRO_BUCK_DESC(buck_10m, MB_PMIC_MORRO_REG_ID_BUCK_10M, 240000, 5000),
	MORRO_LDO_DESC(ldo_1m, MB_PMIC_MORRO_REG_ID_LDO_1M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_2m, MB_PMIC_MORRO_REG_ID_LDO_2M, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_3m, MB_PMIC_MORRO_REG_ID_LDO_3M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_4m, MB_PMIC_MORRO_REG_ID_LDO_4M, NULL,
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_5m, MB_PMIC_MORRO_REG_ID_LDO_5M, NULL,
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_6m, MB_PMIC_MORRO_REG_ID_LDO_6M, "buck_6m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_7m, MB_PMIC_MORRO_REG_ID_LDO_7M, "ldo_2m",
		       morro_ldo_reg_ops, 960000, 15000),
	MORRO_LDO_DESC(ldo_8m, MB_PMIC_MORRO_REG_ID_LDO_8M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_9m, MB_PMIC_MORRO_REG_ID_LDO_9M, "buck_6m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_10m, MB_PMIC_MORRO_REG_ID_LDO_10M, "buck_6m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_11m, MB_PMIC_MORRO_REG_ID_LDO_11M, "buck_9m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_12m, MB_PMIC_MORRO_REG_ID_LDO_12M, "buck_9m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_13m, MB_PMIC_MORRO_REG_ID_LDO_13M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_14m, MB_PMIC_MORRO_REG_ID_LDO_14M, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_15m, MB_PMIC_MORRO_REG_ID_LDO_15M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_16m, MB_PMIC_MORRO_REG_ID_LDO_16M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_17m, MB_PMIC_MORRO_REG_ID_LDO_17M, "buck_9m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_18m, MB_PMIC_MORRO_REG_ID_LDO_18M, "buck_6m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_19m, MB_PMIC_MORRO_REG_ID_LDO_19M, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_20m, MB_PMIC_MORRO_REG_ID_LDO_20M, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_21m, MB_PMIC_MORRO_REG_ID_LDO_21M, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_22m, MB_PMIC_MORRO_REG_ID_LDO_22M, "buck_6m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_23m, MB_PMIC_MORRO_REG_ID_LDO_23M, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_24m, MB_PMIC_MORRO_REG_ID_LDO_24M, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_25m, MB_PMIC_MORRO_REG_ID_LDO_25M, "buck_6m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_26m, MB_PMIC_MORRO_REG_ID_LDO_26M, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_27m, MB_PMIC_MORRO_REG_ID_LDO_27M, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_28m, MB_PMIC_MORRO_REG_ID_LDO_28M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_29m, MB_PMIC_MORRO_REG_ID_LDO_29M, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_30m, MB_PMIC_MORRO_REG_ID_LDO_30M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_31m, MB_PMIC_MORRO_REG_ID_LDO_31M, "buck_8m",
		       morro_ldo_reg_ops, 400000, 5000),
	/* Morro-S */
	MORRO_BUCK_DESC(buck_1s, MB_PMIC_MORRO_REG_ID_BUCK_1S, 240000, 5000),
	MORRO_BUCK_DESC(buck_2s, MB_PMIC_MORRO_REG_ID_BUCK_2S, 240000, 5000),
	MORRO_BUCK_DESC(buck_3s, MB_PMIC_MORRO_REG_ID_BUCK_3S, 240000, 5000),
	MORRO_BUCK_DESC(buck_4s, MB_PMIC_MORRO_REG_ID_BUCK_4S, 240000, 5000),
	MORRO_BUCK_DESC(buck_5s, MB_PMIC_MORRO_REG_ID_BUCK_5S, 240000, 5000),
	MORRO_BUCK_DESC(buck_6s, MB_PMIC_MORRO_REG_ID_BUCK_6S, 240000, 5000),
	MORRO_BUCK_DESC(buck_7s, MB_PMIC_MORRO_REG_ID_BUCK_7S, 800000, 10000),
	MORRO_BUCK_DESC(buck_8s, MB_PMIC_MORRO_REG_ID_BUCK_8S, 240000, 5000),
	MORRO_BUCK_DESC(buck_9s, MB_PMIC_MORRO_REG_ID_BUCK_9S, 240000, 5000),
	MORRO_BUCK_DESC(buck_10s, MB_PMIC_MORRO_REG_ID_BUCK_10S, 240000, 5000),
	MORRO_BUCK_DESC(buck_11s, MB_PMIC_MORRO_REG_ID_BUCK_11S, 800000, 10000),
	MORRO_BUCK_DESC(buck_12s, MB_PMIC_MORRO_REG_ID_BUCK_12S, 800000, 10000),
	MORRO_BUCK_DESC(buck_13s, MB_PMIC_MORRO_REG_ID_BUCK_13S, 240000, 5000),
	/* 1 buck-boost */
	// TODO(cychu): Register min: 1.299V seems is an error.
	MORRO_BUCK_DESC(buck_boost, MB_PMIC_MORRO_REG_ID_BUCK_BOOST, 1290000,
			10000),
	MORRO_LDO_DESC(ldo_1s, MB_PMIC_MORRO_REG_ID_LDO_1S, "buck_3s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_2s, MB_PMIC_MORRO_REG_ID_LDO_2S, "buck_3s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_3s, MB_PMIC_MORRO_REG_ID_LDO_3S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_4s, MB_PMIC_MORRO_REG_ID_LDO_4S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_5s, MB_PMIC_MORRO_REG_ID_LDO_5S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_6s, MB_PMIC_MORRO_REG_ID_LDO_6S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_7s, MB_PMIC_MORRO_REG_ID_LDO_7S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_8s, MB_PMIC_MORRO_REG_ID_LDO_8S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_9s, MB_PMIC_MORRO_REG_ID_LDO_9S, "buck_6s",
		       morro_ldo_reg_bypass_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_10s, MB_PMIC_MORRO_REG_ID_LDO_10S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_11s, MB_PMIC_MORRO_REG_ID_LDO_11S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_12s, MB_PMIC_MORRO_REG_ID_LDO_12S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_13s, MB_PMIC_MORRO_REG_ID_LDO_13S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_14s, MB_PMIC_MORRO_REG_ID_LDO_14S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_15s, MB_PMIC_MORRO_REG_ID_LDO_15S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_16s, MB_PMIC_MORRO_REG_ID_LDO_16S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_17s, MB_PMIC_MORRO_REG_ID_LDO_17S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_18s, MB_PMIC_MORRO_REG_ID_LDO_18S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_19s, MB_PMIC_MORRO_REG_ID_LDO_19S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_20s, MB_PMIC_MORRO_REG_ID_LDO_20S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_21s, MB_PMIC_MORRO_REG_ID_LDO_21S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_22s, MB_PMIC_MORRO_REG_ID_LDO_22S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_23s, MB_PMIC_MORRO_REG_ID_LDO_23S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_24s, MB_PMIC_MORRO_REG_ID_LDO_24S, "buck_6s",
		       morro_ldo_reg_ops, 400000, 5000),
	MORRO_LDO_DESC(ldo_25s, MB_PMIC_MORRO_REG_ID_LDO_25S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_26s, MB_PMIC_MORRO_REG_ID_LDO_26S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_27s, MB_PMIC_MORRO_REG_ID_LDO_27S, "buck_7s",
		       morro_ldo_reg_ops, 1200000, 10000),
	MORRO_LDO_DESC(ldo_28s, MB_PMIC_MORRO_REG_ID_LDO_28S, "buck_boost",
		       morro_ldo_reg_ops, 1200000, 10000),
};

static_assert(ARRAY_SIZE(morro_regulators_desc) == MORRO_NUM_REGULATORS);

static struct of_regulator_match morro_m_regulators_matches[] = {
	{
		.name = "buck_1m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_1M],
	},
	{
		.name = "buck_2m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_2M],
	},
	{
		.name = "buck_3m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_3M],
	},
	{
		.name = "buck_4m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_4M],
	},
	{
		.name = "buck_5m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_5M],
	},
	{
		.name = "buck_6m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_6M],
	},
	{
		.name = "buck_7m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_7M],
	},
	{
		.name = "buck_8m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_8M],
	},
	{
		.name = "buck_9m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_9M],
	},
	{
		.name = "buck_10m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_10M],
	},
	{
		.name = "ldo_1m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_1M],
	},
	{
		.name = "ldo_2m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_2M],
	},
	{
		.name = "ldo_3m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_3M],
	},
	{
		.name = "ldo_4m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_4M],
	},
	{
		.name = "ldo_5m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_5M],
	},
	{
		.name = "ldo_6m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_6M],
	},
	{
		.name = "ldo_7m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_7M],
	},
	{
		.name = "ldo_8m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_8M],
	},
	{
		.name = "ldo_9m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_9M],
	},
	{
		.name = "ldo_10m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_10M],
	},
	{
		.name = "ldo_11m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_11M],
	},
	{
		.name = "ldo_12m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_12M],
	},
	{
		.name = "ldo_13m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_13M],
	},
	{
		.name = "ldo_14m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_14M],
	},
	{
		.name = "ldo_15m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_15M],
	},
	{
		.name = "ldo_16m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_16M],
	},
	{
		.name = "ldo_17m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_17M],
	},
	{
		.name = "ldo_18m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_18M],
	},
	{
		.name = "ldo_19m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_19M],
	},
	{
		.name = "ldo_20m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_20M],
	},
	{
		.name = "ldo_21m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_21M],
	},
	{
		.name = "ldo_22m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_22M],
	},
	{
		.name = "ldo_23m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_23M],
	},
	{
		.name = "ldo_24m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_24M],
	},
	{
		.name = "ldo_25m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_25M],
	},
	{
		.name = "ldo_26m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_26M],
	},
	{
		.name = "ldo_27m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_27M],
	},
	{
		.name = "ldo_28m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_28M],
	},
	{
		.name = "ldo_29m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_29M],
	},
	{
		.name = "ldo_30m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_30M],
	},
	{
		.name = "ldo_31m",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_31M],
	},
};

static_assert(ARRAY_SIZE(morro_m_regulators_matches) == MORRO_M_NUM_REGULATORS);

static struct of_regulator_match morro_s_regulators_matches[] = {
	{
		.name = "buck_1s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_1S],
	},
	{
		.name = "buck_2s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_2S],
	},
	{
		.name = "buck_3s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_3S],
	},
	{
		.name = "buck_4s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_4S],
	},
	{
		.name = "buck_5s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_5S],
	},
	{
		.name = "buck_6s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_6S],
	},
	{
		.name = "buck_7s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_7S],
	},
	{
		.name = "buck_8s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_8S],
	},
	{
		.name = "buck_9s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_9S],
	},
	{
		.name = "buck_10s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_10S],
	},
	{
		.name = "buck_11s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_11S],
	},
	{
		.name = "buck_12s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_12S],
	},
	{
		.name = "buck_13s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_13S],
	},
	{
		.name = "buck_boost",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_BUCK_BOOST],
	},
	{
		.name = "ldo_1s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_1S],
	},
	{
		.name = "ldo_2s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_2S],
	},
	{
		.name = "ldo_3s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_3S],
	},
	{
		.name = "ldo_4s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_4S],
	},
	{
		.name = "ldo_5s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_5S],
	},
	{
		.name = "ldo_6s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_6S],
	},
	{
		.name = "ldo_7s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_7S],
	},
	{
		.name = "ldo_8s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_8S],
	},
	{
		.name = "ldo_9s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_9S],
	},
	{
		.name = "ldo_10s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_10S],
	},
	{
		.name = "ldo_11s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_11S],
	},
	{
		.name = "ldo_12s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_12S],
	},
	{
		.name = "ldo_13s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_13S],
	},
	{
		.name = "ldo_14s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_14S],
	},
	{
		.name = "ldo_15s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_15S],
	},
	{
		.name = "ldo_16s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_16S],
	},
	{
		.name = "ldo_17s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_17S],
	},
	{
		.name = "ldo_18s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_18S],
	},
	{
		.name = "ldo_19s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_19S],
	},
	{
		.name = "ldo_20s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_20S],
	},
	{
		.name = "ldo_21s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_21S],
	},
	{
		.name = "ldo_22s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_22S],
	},
	{
		.name = "ldo_23s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_23S],
	},
	{
		.name = "ldo_24s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_24S],
	},
	{
		.name = "ldo_25s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_25S],
	},
	{
		.name = "ldo_26s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_26S],
	},
	{
		.name = "ldo_27s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_27S],
	},
	{
		.name = "ldo_28s",
		.desc = &morro_regulators_desc[MB_PMIC_MORRO_REG_ID_LDO_28S],
	},
};

static_assert(ARRAY_SIZE(morro_s_regulators_matches) == MORRO_S_NUM_REGULATORS);

static int morro_register_regulator(struct morro_device *morro_dev,
				    struct of_regulator_match *match)
{
	struct device *dev = morro_dev->dev;
	struct regulator_config config = {
		.dev = dev,
		.driver_data = morro_dev,
		.of_node = match->of_node,
		.init_data = match->init_data,
	};
	struct regulator_dev *rdev;

	rdev = devm_regulator_register(dev, match->desc, &config);
	if (IS_ERR(rdev)) {
		dev_err_probe(dev, PTR_ERR(rdev),
			      "Failed to register regulator: %s\n",
			      match->name);
		return PTR_ERR(rdev);
	}
	dev_dbg(dev, "Registered Morro regulator: %s\n", match->name);
	return 0;
}

#ifdef CONFIG_DEBUG_FS
/*
 * Issue a mailbox request to CPM, read the PMIC register data from address
 * specified in read_addr, then return the data.
 */
static int pmic_register_debugfs_reg_read_read(void *data, u64 *val)
{
	struct pmic_info *pmic = data;
	struct mailbox_data req_data, resp_data;
	int ret;

	req_data.data[0] = pmic->pmic_id;
	req_data.data[1] = 1; // Only read 1 byte.
	ret = pmic_mfd_mbox_send_req_blocking_read(pmic->dev, &pmic->mbox,
						   pmic->mb_dest_channel,
						   MB_PMIC_TARGET_REGISTER,
						   MB_REG_CMD_GET_PMIC_REG_BURST,
						   pmic->read_addr, req_data,
						   &resp_data);
	if (unlikely(ret))
		return ret;
	*val = resp_data.data[0];
	return 0;
}

/*
 * Set the read_addr, the read address offset for the PMIC register.
 */
static int pmic_register_debugfs_reg_read_write(void *data, u64 addr)
{
	struct pmic_info *pmic = data;

	if (addr >= BIT_ULL(16)) {
		dev_err(pmic->dev, "Addr must not exceed 16-bits: 0x%llx\n",
			addr);
		return -EINVAL;
	}

	pmic->read_addr = addr;
	dev_dbg(pmic->dev, "Set read addr to: 0x%x\n", pmic->read_addr);
	return 0;
}

/*
 * Accepts space-split fields for PMIC register write, parse them and issue a
 * mailbox msg to CPM.
 * Field format must be:
 *	"{address (16b)} {data (8b)}"
 * Example:
 *	"0x3004 0x5a"
 */
static ssize_t pmic_register_debugfs_reg_write_write(struct file *filp,
						     const char __user *buf,
						     size_t count, loff_t *ppos)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct pmic_info *pmic = filp->private_data;
	char user_input[USER_INPUT_BUF_SIZE] = { 0 };
	ssize_t user_input_len;
	char *user_input_tok;
	char *reg_address_str, *reg_data_str;
	u32 reg_address, reg_data;
	struct mailbox_data req_data;
	int ret;

	ret = debugfs_file_get(dentry);
	if (unlikely(ret))
		return ret;

	/* Empty string or just a newline */
	if (count <= 1) {
		dev_err(pmic->dev, "Empty string was passed.\n");
		ret = -EINVAL;
		goto debugfs_out;
	}
	if (count > USER_INPUT_BUF_SIZE) {
		dev_err(pmic->dev, "User's input is too long.\n");
		ret = -EINVAL;
		goto debugfs_out;
	}

	user_input_len = simple_write_to_buffer(user_input, USER_INPUT_BUF_SIZE,
						ppos, buf, count);
	if (user_input_len < 0) {
		ret = user_input_len;
		goto debugfs_out;
	}

	dev_dbg(pmic->dev, "User input: %s\n", user_input);

	/* Parse input string */
	user_input_tok = user_input;
	reg_address_str = strsep(&user_input_tok, " ");
	reg_data_str = strsep(&user_input_tok, " ");

	/*
	 * "user_input_tok" is expected to be NULL here,
	 * since "strsep" updates it to NULL when there is no more delimiter.
	 */
	if (!reg_address_str || !reg_data_str || user_input_tok) {
		dev_err(pmic->dev,
			"Invalid input format.\n"
			"Fields must be formtted as:\n"
			"\t\"{address in hex (16b)} {data in hex (8b)}\"\n");
		ret = -EINVAL;
		goto debugfs_out;
	}

	dev_dbg(pmic->dev, "Sep: reg_address_str: %s, reg_data_str: %s\n",
		reg_address_str, reg_data_str);

	/* base = 0 for auto decide, accept both HEX and DEC.*/
	if (kstrtou32(reg_address_str, 0, &reg_address)) {
		dev_err(pmic->dev, "Failed to parse Reg address: %s\n",
			reg_address_str);
		ret = -EINVAL;
		goto debugfs_out;
	}
	if (kstrtou32(reg_data_str, 0, &reg_data)) {
		dev_err(pmic->dev, "Failed to parse Reg data: %s\n",
			reg_data_str);
		ret = -EINVAL;
		goto debugfs_out;
	}

	dev_dbg(pmic->dev,
		"Parsed fields:\n"
		"\tReg address: 0x%x, Reg data: 0x%x\n",
		reg_address, reg_data);

	/* User input parsed. Do range check. */
	if (reg_address >= BIT_ULL(16)) {
		dev_err(pmic->dev, "Addr must not exceed 16-bits: 0x%x\n",
			reg_address);
		return -EINVAL;
	}
	if (reg_data >= BIT_ULL(8)) {
		dev_err(pmic->dev, "Data must not exceed 8-bits: 0x%x\n",
			reg_data);
		return -EINVAL;
	}

	/* Create mbox request, send to CPM for reg write. */
	req_data.data[0] = pmic->pmic_id;
	req_data.data[1] = reg_data;
	ret = pmic_mfd_mbox_send_req_blocking(pmic->dev, &pmic->mbox,
					      pmic->mb_dest_channel,
					      MB_PMIC_TARGET_REGISTER,
					      MB_REG_CMD_SET_PMIC_REG_SINGLE,
					      reg_address, req_data);
	if (unlikely(ret))
		return ret;

	ret = user_input_len;
debugfs_out:
	debugfs_file_put(dentry);
	return ret;
}

/* reg_read file is R/W able. */
DEFINE_DEBUGFS_ATTRIBUTE(fops_reg_read, pmic_register_debugfs_reg_read_read,
			 pmic_register_debugfs_reg_read_write, "0x%llx\n");
/* reg_write file is writeable. */
static const struct file_operations fops_reg_write = {
	.open = simple_open,
	.write = pmic_register_debugfs_reg_write_write,
};

static int da9186_regulator_debugfs_init(struct morro_device *pmic_dev)
{
	struct dentry *dentry;
	int i;

	if (!debugfs_initialized())
		return -ENODEV;

	pmic_dev->debugfs_root = debugfs_create_dir(DEBUGFS_DENTRY_NAME, NULL);

	for (i = 0; i < pmic_dev->num_of_pmics; ++i) {
		dentry = debugfs_create_dir(pmic_dev->pmic_info[i].name,
					    pmic_dev->debugfs_root);

		debugfs_create_file("reg_read", 0660, dentry,
				    &pmic_dev->pmic_info[i], &fops_reg_read);
		debugfs_create_file("reg_write", 0220, dentry,
				    &pmic_dev->pmic_info[i], &fops_reg_write);
	}

	dev_notice(pmic_dev->dev,
		   "%s() complete. Check:\n"
		   "cd /sys/kernel/debug/%s\n",
		   __func__, DEBUGFS_DENTRY_NAME);
	return 0;
}
#endif

static int da9186_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct morro_device *morro_dev;
	int ret;
	struct device_node *regulators_np, *morro_m_np, *morro_s_np;
	int regulator_m_count, regulator_s_count;
	int i;
	struct regulator_desc *temp_desc;

	/* Design to obtain constraints from DT, not platform_data pointer */
	if (dev_get_platdata(dev)) {
		dev_err(dev,
			"platform data is not supported, use device tree\n");
		return -ENODEV;
	}
	if (!dev->of_node) {
		dev_err(dev, "Failed to find DT node: %s\n", pdev->name);
		return -ENODEV;
	}

	morro_dev = devm_kzalloc(dev, sizeof(*morro_dev), GFP_KERNEL);
	if (!morro_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, morro_dev);

	morro_dev->dev = dev;

	ret = of_property_read_u32(dev->of_node, "mba-dest-channel",
				   &morro_dev->mb_dest_channel);
	if (ret < 0) {
		dev_err(dev, "Failed to read mba-dest-channel\n");
		return ret;
	}

	dev_info(dev, "Init mailbox client\n");
	ret = pmic_mfd_mbox_request(dev, &morro_dev->mbox);
	if (ret < 0)
		return ret;

	regulators_np = of_get_child_by_name(dev->of_node, "regulators");
	if (!regulators_np) {
		dev_err(dev, "Regulators node not found in DT\n");
		return -ENODEV;
	}

	morro_m_np = of_find_node_by_name(regulators_np, "morro_m");
	if (!morro_m_np) {
		dev_err(dev, "Morro-M node not found in DT\n");
		return -ENODEV;
	}

	morro_s_np = of_find_node_by_name(regulators_np, "morro_s");
	if (!morro_s_np) {
		dev_err(dev, "Morro-S node not found in DT\n");
		return -ENODEV;
	}

	/*
	 * Parse the regulators specified in the DT, fill the of_node and
	 * init_data from DT to morro_regulators_matches.
	 * For the regulators that have "monitor-only" property, they are either
	 * no need for control (just left with PMIC default) or already managed
	 * by CPM or the SoC internal HW. The driver should not perform any
	 * action (even init) on them to avoid interference.
	 */
	regulator_m_count =
		of_regulator_match(dev, morro_m_np, morro_m_regulators_matches,
				   ARRAY_SIZE(morro_m_regulators_matches));
	if (regulator_m_count <= 0) {
		dev_err(dev, "No regulator found under morro-m node\n");
		of_node_put(morro_m_np);
		return -ENODEV;
	}
	dev_info(dev, "Registering %d regulators on Morro-M\n",
		 regulator_m_count);

	/* Register all matched regulators. */
	for (i = 0; i < ARRAY_SIZE(morro_m_regulators_matches); ++i) {
		/* Skip this regulator if it does not matched. */
		if (!morro_m_regulators_matches[i].init_data)
			continue;
		/* Don't apply constraints when initialising. */
		morro_m_regulators_matches[i].init_data->constraints.apply_uV = 0;
		/* Check whether this regulator instance is monitor-only. */
		if (of_property_read_bool(morro_m_regulators_matches[i].of_node,
					  "google,monitor-only")) {
			/*
			 * Save a copy of the current regulator_desc and
			 * override the ops to be the monitor-only ops.
			 */
			temp_desc = devm_kzalloc(dev, sizeof(*temp_desc),
						 GFP_KERNEL);
			if (!temp_desc)
				return -ENOMEM;

			*temp_desc = *morro_m_regulators_matches[i].desc;
			temp_desc->ops = &morro_monitor_reg_ops;

			morro_m_regulators_matches[i].desc = temp_desc;
		}
		ret = morro_register_regulator(morro_dev,
					       &morro_m_regulators_matches[i]);
		if (ret < 0)
			return ret;
	}
	of_node_put(morro_m_np);

	regulator_s_count =
		of_regulator_match(dev, morro_s_np, morro_s_regulators_matches,
				   ARRAY_SIZE(morro_s_regulators_matches));
	if (regulator_s_count <= 0) {
		dev_err(dev, "No regulator found under morro-s node\n");
		of_node_put(morro_s_np);
		return -ENODEV;
	}
	dev_info(dev, "Registering %d regulators on Morro-S\n",
		 regulator_s_count);

	/* Register all matched regulators. */
	for (i = 0; i < ARRAY_SIZE(morro_s_regulators_matches); ++i) {
		/* Skip this regulator if it does not matched. */
		if (!morro_s_regulators_matches[i].init_data)
			continue;
		/* Don't apply constraints when initialising. */
		morro_s_regulators_matches[i].init_data->constraints.apply_uV = 0;
		/* Check whether this regulator instance is monitor-only. */
		if (of_property_read_bool(morro_s_regulators_matches[i].of_node,
					  "google,monitor-only")) {
			/*
			 * Save a copy of the current regulator_desc and
			 * override the ops to be the monitor-only ops.
			 */
			temp_desc = devm_kzalloc(dev, sizeof(*temp_desc),
						 GFP_KERNEL);
			if (!temp_desc)
				return -ENOMEM;

			*temp_desc = *morro_s_regulators_matches[i].desc;
			temp_desc->ops = &morro_monitor_reg_ops;

			morro_s_regulators_matches[i].desc = temp_desc;
		}
		ret = morro_register_regulator(morro_dev,
					       &morro_s_regulators_matches[i]);
		if (ret < 0)
			return ret;
	}
	of_node_put(morro_s_np);

#ifdef CONFIG_DEBUG_FS
	morro_dev->num_of_pmics = ARRAY_SIZE(pmic_names);
	for (i = 0; i < morro_dev->num_of_pmics; ++i) {
		morro_dev->pmic_info[i].name = pmic_names[i];
		morro_dev->pmic_info[i].dev = morro_dev->dev;
		morro_dev->pmic_info[i].mbox = morro_dev->mbox;
		morro_dev->pmic_info[i].mb_dest_channel =
			morro_dev->mb_dest_channel;
		morro_dev->pmic_info[i].pmic_id = i;
	}
	return da9186_regulator_debugfs_init(morro_dev);
#else
	return 0;
#endif
}

static void da9186_regulator_remove(struct platform_device *pdev)
{
	struct morro_device *morro_dev = platform_get_drvdata(pdev);

	if (IS_ERR_OR_NULL(morro_dev))
		return;

	pmic_mfd_mbox_release(&morro_dev->mbox);
#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(morro_dev->debugfs_root);
#endif
}

static const struct platform_device_id da9186_regulator_id[] = {
	{ "da9186-regulator", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, da9186_regulator_id);

static struct platform_driver da9186_regulator_driver = {
	.probe = da9186_regulator_probe,
	.remove = da9186_regulator_remove,
	.driver = {
		.name = "da9186-regulator",
		.owner = THIS_MODULE,
	},
	.id_table = da9186_regulator_id,
};
module_platform_driver(da9186_regulator_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("DA9186 PMIC Regulator Driver");
MODULE_LICENSE("GPL");
