// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2024 Google LLC
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

#include <ap-pmic/da9188.h>
#include <mailbox/protocols/mba/cpm/common/pmic/pmic_service.h>
#include <soc/google/goog_cpm_service_ids.h>

/* DA9188, DA9189 Regulator IDs */
enum da9188_regulator_ids {
	/* M Bucks */
	DA9188_REGL_BUCK1M,
	DA9188_REGL_BUCK2M,
	DA9188_REGL_BUCK3M,
	DA9188_REGL_BUCK4M,
	DA9188_REGL_BUCK5M,
	DA9188_REGL_BUCK6M,
	DA9188_REGL_BUCK7M,
	DA9188_REGL_BUCK8M,
	DA9188_REGL_BUCK9M,
	DA9188_REGL_BUCK10M,
	DA9188_REGL_BUCK11M,
	DA9188_REGL_BUCK12M,
	DA9188_REGL_BUCK13M,
	/* M LDOs */
	DA9188_REGL_LDO1M,
	DA9188_REGL_LDO2M,
	DA9188_REGL_LDO3M,
	DA9188_REGL_LDO4M,
	DA9188_REGL_LDO5M,
	DA9188_REGL_LDO6M,
	DA9188_REGL_LDO7M,
	DA9188_REGL_LDO8M,
	DA9188_REGL_LDO9M,
	DA9188_REGL_LDO10M,
	DA9188_REGL_LDO11M,
	DA9188_REGL_LDO12M,
	DA9188_REGL_LDO13M,
	DA9188_REGL_LDO14M,
	DA9188_REGL_LDO15M,
	DA9188_REGL_LDO16M,
	DA9188_REGL_LDO17M,
	DA9188_REGL_LDO18M,
	DA9188_REGL_LDO19M,
	DA9188_REGL_LDO20M,
	DA9188_REGL_LDO21M,
	DA9188_REGL_LDO22M,
	DA9188_REGL_LDO23M,
	DA9188_REGL_LDO24M,
	DA9188_REGL_LDO25M,
	DA9188_REGL_LDO26M,
	DA9188_REGL_LDO27M,
	DA9188_REGL_LDO28M,
	/* S Bucks */
	DA9188_REGL_BUCK1S,
	DA9188_REGL_BUCK2S,
	DA9188_REGL_BUCK3S,
	DA9188_REGL_BUCK4S,
	DA9188_REGL_BUCK5S,
	DA9188_REGL_BUCK6S,
	DA9188_REGL_BUCK7S,
	DA9188_REGL_BUCK8S,
	DA9188_REGL_BUCK9S,
	DA9188_REGL_BUCK10S,
	DA9188_REGL_BUCK11S,
	DA9188_REGL_BUCK12S,
	DA9188_REGL_BUCK13S,
	DA9188_REGL_BUBO,
	/* S LDOs */
	DA9188_REGL_LDO1S,
	DA9188_REGL_LDO2S,
	DA9188_REGL_LDO3S,
	DA9188_REGL_LDO4S,
	DA9188_REGL_LDO5S,
	DA9188_REGL_LDO6S,
	DA9188_REGL_LDO7S,
	DA9188_REGL_LDO8S,
	DA9188_REGL_LDO9S,
	DA9188_REGL_LDO10S,
	DA9188_REGL_LDO11S,
	DA9188_REGL_LDO12S,
	DA9188_REGL_LDO13S,
	DA9188_REGL_LDO14S,
	DA9188_REGL_LDO15S,
	DA9188_REGL_LDO16S,
	DA9188_REGL_LDO17S,
	DA9188_REGL_LDO18S,
	DA9188_REGL_LDO19S,
	DA9188_REGL_LDO20S,
	DA9188_REGL_LDO21S,
	DA9188_REGL_LDO22S,
	DA9188_REGL_LDO23S,
	DA9188_REGL_LDO24S,
	DA9188_REGL_LDO25S,
	DA9188_REGL_LDO26S,
	DA9188_REGL_LDO27S,
	DA9188_REGL_LDO28S,
	DA9188_REGL_MAX,
};

/* 13 bucks, 28 ldos */
const int da9188_num_regulators = DA9188_REGL_LDO28M - DA9188_REGL_BUCK1M + 1;
/* 13 bucks, 1 buck-boost, 28 ldos */
const int da9189_num_regulators = DA9188_REGL_LDO28S - DA9188_REGL_BUCK1S + 1;

#define REGULATOR_BUCK_DESC(_name, _id, _min, _step)           \
	{                                                      \
		.name = (#_name),                              \
		.of_match = of_match_ptr(#_name),              \
		.regulators_node = of_match_ptr("regulators"), \
		.ops = &pmic_buck_reg_ops,                     \
		.type = REGULATOR_VOLTAGE,                     \
		.id = (_id),                                   \
		.owner = THIS_MODULE,                          \
		.n_voltages = 256,                             \
		.min_uV = (_min),                              \
		.uV_step = (_step),                            \
		.of_map_mode = pmic_reg_map_buck_mode,         \
	}

#define REGULATOR_LDO_DESC(_name, _id, _ops, _min, _step)      \
	{                                                      \
		.name = (#_name),                              \
		.of_match = of_match_ptr(#_name),              \
		.regulators_node = of_match_ptr("regulators"), \
		.ops = &(_ops),                                \
		.type = REGULATOR_VOLTAGE,                     \
		.id = (_id),                                   \
		.owner = THIS_MODULE,                          \
		.n_voltages = 256,                             \
		.min_uV = (_min),                              \
		.uV_step = (_step),                            \
	}

#if defined(CONFIG_DEBUG_FS) && \
	IS_ENABLED(CONFIG_GOOGLE_DA9188_REGULATOR_REGISTER_ACCESS)
#define DEBUGFS_DENTRY_NAME "pmic"
#define USER_INPUT_BUF_SIZE (16)

/* This driver provide control to 2 PMICs */
enum pmic_idx {
	PMIC_IDX_DA9188 = 0,
	PMIC_IDX_DA9189,
	PMIC_IDX_MAX,
};

static const char *const pmic_names[PMIC_IDX_MAX] = {
	"da9188-pmic",
	"da9189-pmic",
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
struct pmic_device {
	struct device *dev;
	struct pmic_mfd_mbox mbox;
	/* DTS configurable parameters. */
	u32 mb_dest_channel;
	/* DebugFS PMIC register R/W */
#if defined(CONFIG_DEBUG_FS) && \
	IS_ENABLED(CONFIG_GOOGLE_DA9188_REGULATOR_REGISTER_ACCESS)
	struct dentry *debugfs_root;
	size_t num_of_pmics;
	struct pmic_info pmic_info[PMIC_IDX_MAX];
#endif
};

/*
 * Get / Set helpers for regulator control.
 */
static int pmic_reg_mb_get_value(struct regulator_dev *rdev, u16 reg_id, u8 cmd,
				 u32 *read_value)
{
	struct pmic_device *drvdata = rdev_get_drvdata(rdev);
	struct mailbox_data req_data = { 0 }, resp_data;
	int ret;

	if (unlikely(!drvdata)) {
		dev_err(&rdev->dev, "Failed to get driver data\n");
		return -EINVAL;
	}

	ret = da9188_mfd_mbox_send_req_blocking_read(drvdata->dev,
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

static int pmic_reg_mb_set_value(struct regulator_dev *rdev, u16 reg_id, u8 cmd,
				 u32 value)
{
	struct pmic_device *drvdata = rdev_get_drvdata(rdev);
	struct mailbox_data req_data = { 0 };

	if (unlikely(!drvdata)) {
		dev_err(&rdev->dev, "Failed to get driver data\n");
		return -EINVAL;
	}

	req_data.data[0] = value;

	return da9188_mfd_mbox_send_req_blocking(drvdata->dev, &drvdata->mbox,
						 drvdata->mb_dest_channel,
						 MB_PMIC_TARGET_REGULATOR, cmd,
						 reg_id, req_data);
}

/*
 * DA9188-9189 regulator OPs.
 */
static int pmic_reg_get_voltage_sel(struct regulator_dev *rdev)
{
	u32 selector;
	int ret = pmic_reg_mb_get_value(rdev, rdev->desc->id,
					MB_REG_CMD_GET_VOLTAGE_SEL, &selector);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s return volt sel: %u\n",
		rdev_get_name(rdev), selector);

	return selector;
}

static int pmic_reg_set_voltage_sel(struct regulator_dev *rdev,
				    unsigned int selector)
{
	int ret = pmic_reg_mb_set_value(rdev, rdev->desc->id,
					MB_REG_CMD_SET_VOLTAGE_SEL, selector);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s set volt sel: %u\n",
		rdev_get_name(rdev), selector);

	return 0;
}

static int pmic_reg_is_enabled(struct regulator_dev *rdev)
{
	u32 enabled;
	int ret = pmic_reg_mb_get_value(rdev, rdev->desc->id,
					MB_REG_CMD_GET_IS_ENABLED, &enabled);

	if (unlikely(ret))
		return ret;

	return enabled;
}

static int pmic_reg_enable(struct regulator_dev *rdev)
{
	int ret = pmic_reg_mb_set_value(rdev, rdev->desc->id,
					MB_REG_CMD_SET_ENABLE_DISABLE, 1);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s Enabled\n", rdev_get_name(rdev));

	return 0;
}

static int pmic_reg_disable(struct regulator_dev *rdev)
{
	int ret = pmic_reg_mb_set_value(rdev, rdev->desc->id,
					MB_REG_CMD_SET_ENABLE_DISABLE, 0);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s Disabled\n", rdev_get_name(rdev));

	return 0;
}

static int pmic_reg_get_bypass(struct regulator_dev *rdev, bool *enable)
{
	u32 bypass;
	int ret = pmic_reg_mb_get_value(rdev, rdev->desc->id,
					MB_REG_CMD_GET_BYPASS, &bypass);

	if (unlikely(ret))
		return ret;
	*enable = (bool)bypass;
	dev_dbg(&rdev->dev, "Regulator %s Bypass: %u\n", rdev_get_name(rdev),
		bypass);

	return ret;
}

static int pmic_reg_set_bypass(struct regulator_dev *rdev, bool enable)
{
	int ret = pmic_reg_mb_set_value(rdev, rdev->desc->id,
					MB_REG_CMD_SET_BYPASS, enable);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s Set bypass: %u\n",
		rdev_get_name(rdev), enable);

	return 0;
}

static unsigned int pmic_reg_get_mode(struct regulator_dev *rdev)
{
	u32 raw_mode;
	int ret = pmic_reg_mb_get_value(rdev, rdev->desc->id,
					MB_REG_CMD_GET_MODE, &raw_mode);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s return raw mode: %u\n",
		rdev_get_name(rdev), raw_mode);

	return (raw_mode == 0) ? REGULATOR_MODE_NORMAL : REGULATOR_MODE_STANDBY;
}

static int pmic_reg_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	u32 raw_mode = (mode == REGULATOR_MODE_NORMAL) ? 0 : 1;
	int ret;

	if (unlikely(mode != REGULATOR_MODE_NORMAL &&
		     mode != REGULATOR_MODE_STANDBY))
		return -EINVAL;

	ret = pmic_reg_mb_set_value(rdev, rdev->desc->id, MB_REG_CMD_SET_MODE,
				    raw_mode);

	if (unlikely(ret))
		return ret;
	dev_dbg(&rdev->dev, "Regulator %s set mode: %u\n", rdev_get_name(rdev),
		mode);

	return 0;
}

static unsigned int pmic_reg_map_buck_mode(unsigned int mode)
{
	return (mode == REGULATOR_MODE_NORMAL ||
		mode == REGULATOR_MODE_STANDBY) ?
		       mode :
		       REGULATOR_MODE_INVALID;
}

static const struct regulator_ops pmic_buck_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = pmic_reg_get_voltage_sel,
	.set_voltage_sel = pmic_reg_set_voltage_sel,
	.is_enabled = pmic_reg_is_enabled,
	.enable = pmic_reg_enable,
	.disable = pmic_reg_disable,
	.get_mode = pmic_reg_get_mode,
	.set_mode = pmic_reg_set_mode,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops pmic_ldo_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = pmic_reg_get_voltage_sel,
	.set_voltage_sel = pmic_reg_set_voltage_sel,
	.is_enabled = pmic_reg_is_enabled,
	.enable = pmic_reg_enable,
	.disable = pmic_reg_disable,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

static const struct regulator_ops pmic_ldo_reg_bypass_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = pmic_reg_get_voltage_sel,
	.set_voltage_sel = pmic_reg_set_voltage_sel,
	.is_enabled = pmic_reg_is_enabled,
	.enable = pmic_reg_enable,
	.disable = pmic_reg_disable,
	.get_bypass = pmic_reg_get_bypass,
	.set_bypass = pmic_reg_set_bypass,
	.set_voltage_time_sel = regulator_set_voltage_time_sel,
};

/* For the regulator that's monitor only, didn't support any change. */
static const struct regulator_ops pmic_monitor_reg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.get_voltage_sel = pmic_reg_get_voltage_sel,
	.is_enabled = pmic_reg_is_enabled,
	.get_mode = pmic_reg_get_mode,
};

static const struct regulator_desc pmic_regulators_desc[] = {
	/* DA9188 (M) */
	REGULATOR_BUCK_DESC(buck_1m, DA9188_REGL_BUCK1M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_2m, DA9188_REGL_BUCK2M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_3m, DA9188_REGL_BUCK3M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_4m, DA9188_REGL_BUCK4M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_5m, DA9188_REGL_BUCK5M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_6m, DA9188_REGL_BUCK6M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_7m, DA9188_REGL_BUCK7M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_8m, DA9188_REGL_BUCK8M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_9m, DA9188_REGL_BUCK9M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_10m, DA9188_REGL_BUCK10M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_11m, DA9188_REGL_BUCK11M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_12m, DA9188_REGL_BUCK12M, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_13m, DA9188_REGL_BUCK13M, 240000, 5000),
	REGULATOR_LDO_DESC(ldo_1m, DA9188_REGL_LDO1M, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_2m, DA9188_REGL_LDO2M, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_3m, DA9188_REGL_LDO3M, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_4m, DA9188_REGL_LDO4M, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_5m, DA9188_REGL_LDO5M, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_6m, DA9188_REGL_LDO6M, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_7m, DA9188_REGL_LDO7M, pmic_ldo_reg_ops, 960000,
			   15000),
	REGULATOR_LDO_DESC(ldo_8m, DA9188_REGL_LDO8M, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_9m, DA9188_REGL_LDO9M, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_10m, DA9188_REGL_LDO10M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_11m, DA9188_REGL_LDO11M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_12m, DA9188_REGL_LDO12M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_13m, DA9188_REGL_LDO13M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_14m, DA9188_REGL_LDO14M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_15m, DA9188_REGL_LDO15M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_16m, DA9188_REGL_LDO16M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_17m, DA9188_REGL_LDO17M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_18m, DA9188_REGL_LDO18M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_19m, DA9188_REGL_LDO19M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_20m, DA9188_REGL_LDO20M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_21m, DA9188_REGL_LDO21M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_22m, DA9188_REGL_LDO22M, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_23m, DA9188_REGL_LDO23M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_24m, DA9188_REGL_LDO24M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_25m, DA9188_REGL_LDO25M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_26m, DA9188_REGL_LDO26M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_27m, DA9188_REGL_LDO27M, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_28m, DA9188_REGL_LDO28M, pmic_ldo_reg_ops,
			   400000, 5000),
	/* DA9189 (S) */
	REGULATOR_BUCK_DESC(buck_1s, DA9188_REGL_BUCK1S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_2s, DA9188_REGL_BUCK2S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_3s, DA9188_REGL_BUCK3S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_4s, DA9188_REGL_BUCK4S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_5s, DA9188_REGL_BUCK5S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_6s, DA9188_REGL_BUCK6S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_7s, DA9188_REGL_BUCK7S, 800000, 10000),
	REGULATOR_BUCK_DESC(buck_8s, DA9188_REGL_BUCK8S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_9s, DA9188_REGL_BUCK9S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_10s, DA9188_REGL_BUCK10S, 240000, 5000),
	REGULATOR_BUCK_DESC(buck_11s, DA9188_REGL_BUCK11S, 800000, 10000),
	REGULATOR_BUCK_DESC(buck_12s, DA9188_REGL_BUCK12S, 800000, 10000),
	REGULATOR_BUCK_DESC(buck_13s, DA9188_REGL_BUCK13S, 240000, 5000),
	/* 1 buck-boost */
	REGULATOR_BUCK_DESC(buck_boost, DA9188_REGL_BUBO, 1300000, 10000),
	REGULATOR_LDO_DESC(ldo_1s, DA9188_REGL_LDO1S, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_2s, DA9188_REGL_LDO2S, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_3s, DA9188_REGL_LDO3S, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_4s, DA9188_REGL_LDO4S, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_5s, DA9188_REGL_LDO5S, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_6s, DA9188_REGL_LDO6S, pmic_ldo_reg_ops, 400000,
			   5000),
	REGULATOR_LDO_DESC(ldo_7s, DA9188_REGL_LDO7S, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_8s, DA9188_REGL_LDO8S, pmic_ldo_reg_ops, 1200000,
			   10000),
	REGULATOR_LDO_DESC(ldo_9s, DA9188_REGL_LDO9S, pmic_ldo_reg_bypass_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_10s, DA9188_REGL_LDO10S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_11s, DA9188_REGL_LDO11S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_12s, DA9188_REGL_LDO12S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_13s, DA9188_REGL_LDO13S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_14s, DA9188_REGL_LDO14S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_15s, DA9188_REGL_LDO15S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_16s, DA9188_REGL_LDO16S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_17s, DA9188_REGL_LDO17S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_18s, DA9188_REGL_LDO18S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_19s, DA9188_REGL_LDO19S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_20s, DA9188_REGL_LDO20S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_21s, DA9188_REGL_LDO21S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_22s, DA9188_REGL_LDO22S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_23s, DA9188_REGL_LDO23S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_24s, DA9188_REGL_LDO24S, pmic_ldo_reg_ops,
			   400000, 5000),
	REGULATOR_LDO_DESC(ldo_25s, DA9188_REGL_LDO25S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_26s, DA9188_REGL_LDO26S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_27s, DA9188_REGL_LDO27S, pmic_ldo_reg_ops,
			   1200000, 10000),
	REGULATOR_LDO_DESC(ldo_28s, DA9188_REGL_LDO28S, pmic_ldo_reg_ops,
			   1200000, 10000),
};

static_assert(ARRAY_SIZE(pmic_regulators_desc) == DA9188_REGL_MAX);

static struct of_regulator_match da9188_regulators_matches[] = {
	{
		.name = "buck_1m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK1M],
	},
	{
		.name = "buck_2m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK2M],
	},
	{
		.name = "buck_3m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK3M],
	},
	{
		.name = "buck_4m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK4M],
	},
	{
		.name = "buck_5m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK5M],
	},
	{
		.name = "buck_6m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK6M],
	},
	{
		.name = "buck_7m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK7M],
	},
	{
		.name = "buck_8m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK8M],
	},
	{
		.name = "buck_9m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK9M],
	},
	{
		.name = "buck_10m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK10M],
	},
	{
		.name = "buck_11m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK11M],
	},
	{
		.name = "buck_12m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK12M],
	},
	{
		.name = "buck_13m",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK13M],
	},
	{
		.name = "ldo_1m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO1M],
	},
	{
		.name = "ldo_2m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO2M],
	},
	{
		.name = "ldo_3m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO3M],
	},
	{
		.name = "ldo_4m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO4M],
	},
	{
		.name = "ldo_5m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO5M],
	},
	{
		.name = "ldo_6m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO6M],
	},
	{
		.name = "ldo_7m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO7M],
	},
	{
		.name = "ldo_8m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO8M],
	},
	{
		.name = "ldo_9m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO9M],
	},
	{
		.name = "ldo_10m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO10M],
	},
	{
		.name = "ldo_11m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO11M],
	},
	{
		.name = "ldo_12m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO12M],
	},
	{
		.name = "ldo_13m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO13M],
	},
	{
		.name = "ldo_14m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO14M],
	},
	{
		.name = "ldo_15m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO15M],
	},
	{
		.name = "ldo_16m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO16M],
	},
	{
		.name = "ldo_17m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO17M],
	},
	{
		.name = "ldo_18m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO18M],
	},
	{
		.name = "ldo_19m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO19M],
	},
	{
		.name = "ldo_20m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO20M],
	},
	{
		.name = "ldo_21m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO21M],
	},
	{
		.name = "ldo_22m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO22M],
	},
	{
		.name = "ldo_23m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO23M],
	},
	{
		.name = "ldo_24m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO24M],
	},
	{
		.name = "ldo_25m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO25M],
	},
	{
		.name = "ldo_26m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO26M],
	},
	{
		.name = "ldo_27m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO27M],
	},
	{
		.name = "ldo_28m",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO28M],
	},
};

static_assert(ARRAY_SIZE(da9188_regulators_matches) == da9188_num_regulators);

static struct of_regulator_match da9189_regulators_matches[] = {
	{
		.name = "buck_1s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK1S],
	},
	{
		.name = "buck_2s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK2S],
	},
	{
		.name = "buck_3s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK3S],
	},
	{
		.name = "buck_4s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK4S],
	},
	{
		.name = "buck_5s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK5S],
	},
	{
		.name = "buck_6s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK6S],
	},
	{
		.name = "buck_7s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK7S],
	},
	{
		.name = "buck_8s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK8S],
	},
	{
		.name = "buck_9s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK9S],
	},
	{
		.name = "buck_10s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK10S],
	},
	{
		.name = "buck_11s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK11S],
	},
	{
		.name = "buck_12s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK12S],
	},
	{
		.name = "buck_13s",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUCK13S],
	},
	{
		.name = "buck_boost",
		.desc = &pmic_regulators_desc[DA9188_REGL_BUBO],
	},
	{
		.name = "ldo_1s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO1S],
	},
	{
		.name = "ldo_2s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO2S],
	},
	{
		.name = "ldo_3s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO3S],
	},
	{
		.name = "ldo_4s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO4S],
	},
	{
		.name = "ldo_5s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO5S],
	},
	{
		.name = "ldo_6s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO6S],
	},
	{
		.name = "ldo_7s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO7S],
	},
	{
		.name = "ldo_8s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO8S],
	},
	{
		.name = "ldo_9s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO9S],
	},
	{
		.name = "ldo_10s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO10S],
	},
	{
		.name = "ldo_11s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO11S],
	},
	{
		.name = "ldo_12s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO12S],
	},
	{
		.name = "ldo_13s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO13S],
	},
	{
		.name = "ldo_14s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO14S],
	},
	{
		.name = "ldo_15s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO15S],
	},
	{
		.name = "ldo_16s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO16S],
	},
	{
		.name = "ldo_17s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO17S],
	},
	{
		.name = "ldo_18s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO18S],
	},
	{
		.name = "ldo_19s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO19S],
	},
	{
		.name = "ldo_20s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO20S],
	},
	{
		.name = "ldo_21s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO21S],
	},
	{
		.name = "ldo_22s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO22S],
	},
	{
		.name = "ldo_23s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO23S],
	},
	{
		.name = "ldo_24s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO24S],
	},
	{
		.name = "ldo_25s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO25S],
	},
	{
		.name = "ldo_26s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO26S],
	},
	{
		.name = "ldo_27s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO27S],
	},
	{
		.name = "ldo_28s",
		.desc = &pmic_regulators_desc[DA9188_REGL_LDO28S],
	},
};

static_assert(ARRAY_SIZE(da9189_regulators_matches) == da9189_num_regulators);

static int da9188_register_regulator(struct pmic_device *pmic_dev,
				     struct of_regulator_match *match)
{
	struct device *dev = pmic_dev->dev;
	struct regulator_config config = {
		.dev = dev,
		.driver_data = pmic_dev,
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
	dev_dbg(dev, "Registered regulator: %s\n", match->name);

	return 0;
}

#if defined(CONFIG_DEBUG_FS) && \
	IS_ENABLED(CONFIG_GOOGLE_DA9188_REGULATOR_REGISTER_ACCESS)
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
	req_data.data[1] = 1; /* Only read 1 byte. */
	ret = da9188_mfd_mbox_send_req_blocking_read(pmic->dev, &pmic->mbox,
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
	ret = da9188_mfd_mbox_send_req_blocking(pmic->dev, &pmic->mbox,
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

static int da9188_regulator_debugfs_init(struct pmic_device *pmic_dev)
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

static int da9188_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pmic_device *pmic_dev;
	int ret;
	struct device_node *regulators_np, *da9188_np, *da9189_np;
	int da9188_regulator_count, da9189_regulator_count;
	int i;
	struct regulator_desc *temp_desc;
	u8 val8;

	if (!dev->of_node)
		return -ENODEV;

	pmic_dev = devm_kzalloc(dev, sizeof(*pmic_dev), GFP_KERNEL);
	if (!pmic_dev)
		return -ENOMEM;
	platform_set_drvdata(pdev, pmic_dev);

	pmic_dev->dev = dev;
	pmic_dev->mb_dest_channel = CPM_COMMON_PMIC_SERVICE;

	dev_info(dev, "Init mailbox client\n");
	ret = da9188_mfd_mbox_request(dev, &pmic_dev->mbox);
	if (ret < 0)
		return ret;

	regulators_np = of_get_child_by_name(dev->of_node, "regulators");
	if (!regulators_np) {
		dev_err(dev, "Regulators node not found in DT\n");
		ret = -ENODEV;
		goto free_mbox;
	}

	da9188_np = of_find_node_by_name(regulators_np, "da9188");
	if (!da9188_np) {
		dev_err(dev, "da9188 node not found under regulators node\n");
		ret = -ENODEV;
		goto free_mbox;
	}

	da9189_np = of_find_node_by_name(regulators_np, "da9189");
	if (!da9189_np) {
		dev_err(dev, "da9189 node not found under regulators node\n");
		ret = -ENODEV;
		goto free_mbox;
	}

	/*
	 * Parse the regulators specified in the DT, fill the of_node and
	 * init_data from DT to da9188/da9189_regulators_matches.
	 * For the regulators that have "monitor-only" property, they are either
	 * no need for control (just left with PMIC default) or already managed
	 * by CPM or the SoC internal HW. The driver should not perform any
	 * action (even init) on them to avoid interference.
	 */
	da9188_regulator_count =
		of_regulator_match(dev, da9188_np, da9188_regulators_matches,
				   ARRAY_SIZE(da9188_regulators_matches));
	if (da9188_regulator_count <= 0) {
		dev_err(dev, "No regulator found under da9188 node\n");
		of_node_put(da9188_np);
		ret = -ENODEV;
		goto free_mbox;
	}
	dev_info(dev, "Registering %d regulators on da9188\n",
		 da9188_regulator_count);

	/* Register all matched regulators. */
	for (i = 0; i < ARRAY_SIZE(da9188_regulators_matches); ++i) {
		/* Skip this regulator if it does not matched. */
		if (!da9188_regulators_matches[i].init_data)
			continue;
		/* Check whether this regulator instance is monitor-only. */
		if (of_property_read_bool(da9188_regulators_matches[i].of_node,
					  "google,monitor-only")) {
			/* Don't apply constraints when initialising. */
			da9188_regulators_matches[i].init_data->constraints.apply_uV =
				0;
			/*
			 * Save a copy of the current regulator_desc and
			 * override the ops to be the monitor-only ops.
			 */
			temp_desc = devm_kzalloc(dev, sizeof(*temp_desc),
						 GFP_KERNEL);
			if (!temp_desc) {
				ret = -ENOMEM;
				goto free_mbox;
			}

			*temp_desc = *da9188_regulators_matches[i].desc;
			temp_desc->ops = &pmic_monitor_reg_ops;

			da9188_regulators_matches[i].desc = temp_desc;
		}
		ret = da9188_register_regulator(pmic_dev,
						&da9188_regulators_matches[i]);
		if (ret < 0)
			goto free_mbox;
	}
	of_node_put(da9188_np);

	da9189_regulator_count =
		of_regulator_match(dev, da9189_np, da9189_regulators_matches,
				   ARRAY_SIZE(da9189_regulators_matches));
	if (da9189_regulator_count <= 0) {
		dev_err(dev, "No regulator found under da9189 node\n");
		of_node_put(da9189_np);
		ret = -ENODEV;
		goto free_mbox;
	}
	dev_info(dev, "Registering %d regulators on da9189\n",
		 da9189_regulator_count);

	/* Register all matched regulators. */
	for (i = 0; i < ARRAY_SIZE(da9189_regulators_matches); ++i) {
		/* Skip this regulator if it does not matched. */
		if (!da9189_regulators_matches[i].init_data)
			continue;
		/* Check whether this regulator instance is monitor-only. */
		if (of_property_read_bool(da9189_regulators_matches[i].of_node,
					  "google,monitor-only")) {
			/* Don't apply constraints when initialising. */
			da9189_regulators_matches[i].init_data->constraints.apply_uV =
				0;
			/*
			 * Save a copy of the current regulator_desc and
			 * override the ops to be the monitor-only ops.
			 */
			temp_desc = devm_kzalloc(dev, sizeof(*temp_desc),
						 GFP_KERNEL);
			if (!temp_desc) {
				ret = -ENOMEM;
				goto free_mbox;
			}

			*temp_desc = *da9189_regulators_matches[i].desc;
			temp_desc->ops = &pmic_monitor_reg_ops;

			da9189_regulators_matches[i].desc = temp_desc;
		}
		ret = da9188_register_regulator(pmic_dev,
						&da9189_regulators_matches[i]);
		if (ret < 0)
			goto free_mbox;
	}
	of_node_put(da9189_np);

#if defined(CONFIG_DEBUG_FS) && \
	IS_ENABLED(CONFIG_GOOGLE_DA9188_REGULATOR_REGISTER_ACCESS)
	pmic_dev->num_of_pmics = PMIC_IDX_MAX;
	for (i = 0; i < pmic_dev->num_of_pmics; ++i) {
		pmic_dev->pmic_info[i].name = pmic_names[i];
		pmic_dev->pmic_info[i].dev = pmic_dev->dev;
		pmic_dev->pmic_info[i].mbox = pmic_dev->mbox;
		pmic_dev->pmic_info[i].mb_dest_channel =
			pmic_dev->mb_dest_channel;
	}

	// Main PMIC ID, defaulted to 0x0.
	pmic_dev->pmic_info[PMIC_IDX_DA9188].pmic_id = 0x0;
	ret = of_property_read_u8(dev->of_node, "pmic-id-main", &val8);
	if (ret == 0)
		pmic_dev->pmic_info[PMIC_IDX_DA9188].pmic_id = val8;

	// Sub PMIC ID, defaulted to 0x1.
	pmic_dev->pmic_info[PMIC_IDX_DA9189].pmic_id = 0x1;
	ret = of_property_read_u8(dev->of_node, "pmic-id-sub", &val8);
	if (ret == 0)
		pmic_dev->pmic_info[PMIC_IDX_DA9189].pmic_id = val8;

	return da9188_regulator_debugfs_init(pmic_dev);
#else

	return 0;
#endif

free_mbox:
	da9188_mfd_mbox_release(&pmic_dev->mbox);

	return ret;
}

static void da9188_regulator_remove(struct platform_device *pdev)
{
	struct pmic_device *pmic_dev = platform_get_drvdata(pdev);

	if (IS_ERR_OR_NULL(pmic_dev))
		return;

	da9188_mfd_mbox_release(&pmic_dev->mbox);
#if defined(CONFIG_DEBUG_FS) && \
	IS_ENABLED(CONFIG_GOOGLE_DA9188_REGULATOR_REGISTER_ACCESS)
	debugfs_remove_recursive(pmic_dev->debugfs_root);
#endif
}

static const struct platform_device_id da9188_regulator_id[] = {
	{ "da9188-regulator", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, da9188_regulator_id);

static struct platform_driver da9188_regulator_driver = {
	.probe = da9188_regulator_probe,
	.remove = da9188_regulator_remove,
	.driver = {
		.name = "da9188-regulator",
		.owner = THIS_MODULE,
	},
	.id_table = da9188_regulator_id,
};
module_platform_driver(da9188_regulator_driver);

MODULE_AUTHOR("Ryan Chu <cychu@google.com>");
MODULE_DESCRIPTION("DA9188 PMIC Regulator Driver");
MODULE_LICENSE("GPL");
