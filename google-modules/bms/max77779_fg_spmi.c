/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC
 */

#include <linux/spmi.h>
#include <linux/regmap.h>
#include <linux/regmap-goog-spmi.h>

#include "max77779.h"
#include "max77779_fg.h"

static const struct regmap_config max77779_fg_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = MAX77779_FG_USR,
};

static const struct regmap_config max77779_fg_debug_regmap_cfg = {
	.reg_bits = 16,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_NATIVE,
	.max_register = 0xFF,
};

static struct spmi_device* max77779_create_spmi_device(struct spmi_controller *ctrl, u8 sid)
{
	struct spmi_device* sdev;
	int ret;

	sdev = spmi_device_alloc(ctrl);
	if (!sdev) {
		pr_err("Error allocating spmi device\n");
		return NULL;
	}
	sdev->usid = sid;
	ret = spmi_device_add(sdev);
	if (ret) {
		pr_err("Error adding spmi device %d\n", ret);
		return NULL;
	}
	return sdev;
}

static int max77779_max17x0x_spmi_regmap_init(struct maxfg_regmap *regmap, struct spmi_device *sdev,
				      const struct regmap_config *regmap_config, bool tag)
{
	struct regmap *map;

	map = devm_regmap_init_goog_spmi(sdev, regmap_config);
	if (IS_ERR(map))
		return PTR_ERR(map);

	if (tag) {
		regmap->regtags.max = ARRAY_SIZE(max77779_fg);
		regmap->regtags.map = max77779_fg;
	} else {
		regmap->regtags.max = ARRAY_SIZE(max77779_debug_fg);
		regmap->regtags.map = max77779_debug_fg;
	}

	regmap->regmap = map;
	return 0;
}

/* NOTE: NEED TO COME BEFORE REGISTER ACCESS */
static int max77779_fg_spmi_regmap_init(struct spmi_device* sdev, struct max77779_fg_chip *chip)
{
	int ret;

	ret = max77779_max17x0x_spmi_regmap_init(&chip->regmap, sdev,
						 &max77779_fg_regmap_cfg, true);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to initialize regmap (%d)\n", ret);
		return ret;
	}

	ret = max77779_max17x0x_spmi_regmap_init(&chip->regmap_debug, chip->secondary_spmi,
					    	 &max77779_fg_debug_regmap_cfg, false);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to initialize debug regmap (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int max77779_fg_spmi_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct max77779_fg_chip *chip;
	int ret, irq;

	irq = max77779_irq_of_parse_and_map(dev->of_node, 0);
	if (irq == -EPROBE_DEFER)
		return irq;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	chip->dev->init_name = "spmi-max77779-fg";
	chip->irq = irq;
	spmi_device_set_drvdata(sdev, chip);

	chip->secondary_spmi = max77779_create_spmi_device(sdev->ctrl,
							   MAX77779_FG_NDGB_ADDRESS_SPMI);
	if (IS_ERR(chip->secondary_spmi)) {
		dev_err(dev, "Error setting up ancillary spmi bus(%ld)\n",
			IS_ERR_VALUE(chip->secondary_spmi));
		ret = PTR_ERR(chip->secondary_spmi);
		goto error;
	}
	spmi_device_set_drvdata(chip->secondary_spmi, chip);

	/* chip->secondary_spmi */
	ret = max77779_fg_spmi_regmap_init(sdev, chip);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize regmap(s)\n");
		goto error;
	}

	ret = max77779_fg_init(chip);
	if (!ret)
		return ret;

error:
	if (chip->secondary_spmi)
		spmi_device_put(chip->secondary_spmi);
	return ret;
}

static void max77779_fg_spmi_remove(struct spmi_device *sdev)
{
	struct max77779_fg_chip *chip = spmi_device_get_drvdata(sdev);

	if (chip->secondary_spmi)
		spmi_device_put(chip->secondary_spmi);

	max77779_fg_remove(chip);
}

static const struct of_device_id max77779_fg_of_match[] = {
	{ .compatible = "maxim,max77779fg-spmi"},
	{},
};
MODULE_DEVICE_TABLE(of, max77779_fg_of_match);

#if IS_ENABLED(CONFIG_PM)
static const struct dev_pm_ops max77779_fg_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(max77779_fg_pm_suspend, max77779_fg_pm_resume)
};
#endif

static struct spmi_driver max77779_fg_spmi_driver = {
	.driver = {
		   .name = "max77779-fg",
		   .of_match_table = max77779_fg_of_match,
#if IS_ENABLED(CONFIG_PM)
		   .pm = &max77779_fg_pm_ops,
#endif
		   .probe_type = PROBE_PREFER_ASYNCHRONOUS,
		   },
	.probe = max77779_fg_spmi_probe,
	.remove = max77779_fg_spmi_remove,
};

module_spmi_driver(max77779_fg_spmi_driver);

MODULE_DESCRIPTION("Maxim 77779 Fuel Gauge SPMI Driver");
MODULE_AUTHOR("Daniel Okazaki <dtokazaki@google.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(FG_MAX77779);
