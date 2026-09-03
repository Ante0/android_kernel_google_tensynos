/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/regmap-goog-spmi.h>

#include "max77779.h"
#include "max77779_vimon.h"

static const struct regmap_config max77779_vimon_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_VIMON_SIZE,
	.readable_reg = max77779_vimon_is_reg,
	.volatile_reg = max77779_vimon_is_reg,
};

static int max77779_vimon_direct_spmi_read(struct max77779_vimon_data *data, u8 reg,
					  unsigned int *val)
{
	struct spmi_device *sdev;

	dev_dbg(data->dev, "vimon_direct_spmi_read(%02x)\n", reg);

	sdev = to_spmi_device(data->dev);
	if (!sdev) {
		dev_err(data->dev, "failed to get spmi_driver in direct read\n");
		return -EIO;
	}

	return spmi_ext_register_read(sdev, reg, (u8 *)val, 2);
}

static int max77779_vimon_direct_spmi_write(struct max77779_vimon_data *data, u8 reg,
					   unsigned int val)
{
	struct spmi_device *sdev;

	dev_dbg(data->dev, "vimon_direct_spmi_write(%02x): %04x\n", reg, (u16)val);

	sdev = to_spmi_device(data->dev);
	if (!sdev) {
		dev_err(data->dev, "failed to get spmi_driver in direct read\n");
		return -EIO;
	}

	return spmi_ext_register_write(sdev, reg, (u8 *)&val, 2);
}

static int max77779_vimon_spmi_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct max77779_vimon_data *data;
	struct regmap *regmap;
	int irq;

	irq = max77779_irq_of_parse_and_map(dev->of_node, 0);
	if (irq == -EPROBE_DEFER)
		return irq;

	regmap = devm_regmap_init_goog_spmi(sdev, &max77779_vimon_regmap_cfg);
	if (IS_ERR(regmap)) {
		dev_err(dev, "Failed to initialize regmap\n");
		return PTR_ERR(regmap);
	};

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->irq = irq;
	data->dev = dev;
	data->dev->init_name = "spmi-max77779-vimon";
	data->regmap = regmap;
	spmi_device_set_drvdata(sdev, data);

	data->direct_reg_read = &max77779_vimon_direct_spmi_read;
	data->direct_reg_write = &max77779_vimon_direct_spmi_write;

	return max77779_vimon_init(data);
}

static void max77779_vimon_spmi_remove(struct spmi_device *sdev)
{
	struct max77779_vimon_data *data = spmi_device_get_drvdata(sdev);

	max77779_vimon_remove(data);
}

static const struct of_device_id max77779_vimon_of_match_table[] = {
	{ .compatible = "maxim,max77779vimon-spmi"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_vimon_of_match_table);

static struct spmi_driver max77779_vimon_spmi_driver = {
	.driver = {
		.name = "max77779-vimon",
		.owner = THIS_MODULE,
		.of_match_table = max77779_vimon_of_match_table,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe	= max77779_vimon_spmi_probe,
	.remove   = max77779_vimon_spmi_remove,
};

module_spmi_driver(max77779_vimon_spmi_driver);
MODULE_DESCRIPTION("Maxim 77779 Vimon SPMI Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(VIMON_MAX77779);
