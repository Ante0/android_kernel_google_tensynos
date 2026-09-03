/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/regmap-goog-spmi.h>

#include "max77779_sp.h"

static const struct regmap_config max77779_sp_regmap_cfg = {
	.name = "max77779_scratch",
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_SP_MAX_ADDR,
	.readable_reg = max77779_sp_is_reg,
	.volatile_reg = max77779_sp_is_reg,
};

static int max77779_sp_spmi_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct max77779_sp_data *data;
	struct regmap *regmap;

	regmap = devm_regmap_init_goog_spmi(sdev, &max77779_sp_regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return -EINVAL;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->regmap = regmap;
	spmi_device_set_drvdata(sdev, data);

	return max77779_sp_init(data);
}

static void max77779_sp_spmi_remove(struct spmi_device *client)
{
	struct max77779_sp_data *data = spmi_device_get_drvdata(client);

	max77779_sp_remove(data);
}

static const struct of_device_id max77779_scratch_of_match_table[] = {
	{ .compatible = "maxim,max77779sp-spmi"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_scratch_of_match_table);

static struct spmi_driver max77779_scratch_spmi_driver = {
	.driver = {
		.name = "max77779-sp",
		.owner = THIS_MODULE,
		.of_match_table = max77779_scratch_of_match_table,
	},
	.probe    = max77779_sp_spmi_probe,
	.remove   = max77779_sp_spmi_remove,
};

module_spmi_driver(max77779_scratch_spmi_driver);
MODULE_DESCRIPTION("Maxim 77779 Scratch SPMI Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
