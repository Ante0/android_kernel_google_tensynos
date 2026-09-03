/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/regmap-goog-spmi.h>

#include "max77779.h"
#include "max77779_i2cm.h"

static const struct regmap_config max77779_i2cm_regmap_cfg = {
	.name = "max77779_i2cm_regmap_cfg",
	.reg_bits = 8,
	.val_bits = 8,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = I2CM_MAX_REGISTER,
};

static int max77779_i2cm_spmi_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct max77779_i2cm_info *info;
	struct regmap *regmap;
	int irq;

	irq = max77779_irq_of_parse_and_map(dev->of_node, 0);
	if (irq == -EPROBE_DEFER)
		return irq;

	regmap = devm_regmap_init_goog_spmi(sdev, &max77779_i2cm_regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap.\n");
		return PTR_ERR(regmap);
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->irq = irq;
	info->dev = dev;
	info->regmap = regmap;
	spmi_device_set_drvdata(sdev, info);

	return max77779_i2cm_init(info);
}

static void max77779_i2cm_spmi_remove(struct spmi_device *sdev)
{
	struct max77779_i2cm_info *chip = spmi_device_get_drvdata(sdev);

	max77779_i2cm_remove(chip);
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id max77779_i2cm_match_table[] = {
	{ .compatible = "maxim,max77779i2cm-spmi",},
	{ },
};
#endif

static struct spmi_driver max77779_i2cm_spmi_driver = {
	.probe		= max77779_i2cm_spmi_probe,
	.remove		= max77779_i2cm_spmi_remove,
	.driver = {
		.name   = "max77779_i2cm",
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = max77779_i2cm_match_table,
#endif
	},
};

module_spmi_driver(max77779_i2cm_spmi_driver);
MODULE_DESCRIPTION("Maxim 77779 I2CM SPMI Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(I2CM_MAX77779);
