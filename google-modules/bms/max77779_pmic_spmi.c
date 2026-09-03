/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/regmap-goog-spmi.h>

#include "max77779_pmic.h"

static const struct regmap_config max77779_pmic_regmap_cfg = {
	.name = "max77779_pmic",
	.reg_bits = 8,
	.val_bits = 8,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_PMIC_GPIO_VGPI_CNFG,
	.readable_reg = max77779_pmic_is_readable,
	.volatile_reg = max77779_pmic_is_readable,
};

static int max77779_pmic_spmi_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct max77779_pmic_info *info;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	spmi_device_set_drvdata(sdev, info);

	info->regmap = devm_regmap_init_goog_spmi(sdev, &max77779_pmic_regmap_cfg);
	if (IS_ERR(info->regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	return max77779_pmic_init(info);
}

static void max77779_pmic_spmi_remove(struct spmi_device *sdev)
{
	struct max77779_pmic_info *info = spmi_device_get_drvdata(sdev);

	max77779_pmic_remove(info);
}

static const struct of_device_id max77779_pmic_of_match_table[] = {
	{ .compatible = "maxim,max77779pmic-spmi" },
	{},
};
MODULE_DEVICE_TABLE(of, max77779_pmic_of_match_table);

static struct spmi_driver max77779_pmic_spmi_driver = {
	.driver = {
		.name = "max77779-pmic",
		.owner = THIS_MODULE,
		.of_match_table = max77779_pmic_of_match_table,
	},
	.probe = max77779_pmic_spmi_probe,
	.remove = max77779_pmic_spmi_remove,
};

module_spmi_driver(max77779_pmic_spmi_driver);
MODULE_DESCRIPTION("Maxim 77779 PMIC SPMI Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
