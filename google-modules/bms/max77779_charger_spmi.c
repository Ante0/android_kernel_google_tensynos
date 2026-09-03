/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/regmap-goog-spmi.h>

#include "max77779.h"
#include "max77779_charger.h"

static const struct regmap_config max77779_chg_regmap_cfg = {
	.name = "max77779_charger",
	.reg_bits = 8,
	.val_bits = 8,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_CHG_CUST_TM,
	.readable_reg = max77779_chg_is_reg,
	.volatile_reg = max77779_chg_is_reg,
};

static int max77779_charger_spmi_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct max77779_chgr_data *data;
	struct regmap *regmap;
	int irq;

	irq = max77779_irq_of_parse_and_map(dev->of_node, 0);
	if (irq == -EPROBE_DEFER)
		return irq;

	regmap = devm_regmap_init_goog_spmi(sdev, &max77779_chg_regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return PTR_ERR(regmap);
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->irq_int = irq;
	data->dev = dev;
	data->dev->init_name = "spmi-max77779-chrg";
	data->regmap = regmap;
	spmi_device_set_drvdata(sdev, data);

	return max77779_charger_init(data);
}

static void max77779_charger_spmi_remove(struct spmi_device *sdev)
{
	struct max77779_chgr_data *data = spmi_device_get_drvdata(sdev);

	max77779_charger_remove(data);
}

static const struct of_device_id max77779_charger_of_match_table[] = {
	{ .compatible = "maxim,max77779chrg-spmi"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_charger_of_match_table);

static const struct dev_pm_ops max77779_charger_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(
		max77779_charger_pm_suspend,
		max77779_charger_pm_resume)
};

static struct spmi_driver max77779_charger_spmi_driver = {
	.driver = {
		.name = "max77779-charger",
		.owner = THIS_MODULE,
		.of_match_table = max77779_charger_of_match_table,
#if IS_ENABLED(CONFIG_PM)
		.pm = &max77779_charger_pm_ops,
#endif
	},
	.probe    = max77779_charger_spmi_probe,
	.remove   = max77779_charger_spmi_remove,
};

module_spmi_driver(max77779_charger_spmi_driver);

MODULE_DESCRIPTION("Maxim 77779 Charger SPMI Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(CHARGER_MAX77779);
