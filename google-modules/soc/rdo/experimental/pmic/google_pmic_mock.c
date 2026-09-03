// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/regmap.h>

#define REBOOT_MODE_REG 0x2FF30

struct google_pmic {
	struct device *dev;
	struct regmap *regmap;
	struct notifier_block reboot_nb;
};

static const struct regmap_config google_pmic_mock_regmap_cfg = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32
};

enum {
	REBOOT_MODE_NORMAL = 0,
	REBOOT_MODE_CHARGE,
	REBOOT_MODE_DMVERITY_CORRUPTED,
	REBOOT_MODE_SHUTDOWN_THERMAL,
	REBOOT_MODE_RESCUE,
	REBOOT_MODE_FASTBOOT,
	REBOOT_MODE_BOOTLOADER,
	REBOOT_MODE_FACTORY,
	REBOOT_MODE_RECOVERY,
	REBOOT_MODE_ROM_RECOVERY
};

static int reboot_handler(struct notifier_block *nb, unsigned long mode,
			  void *command)
{
	struct google_pmic *data =
		container_of(nb, struct google_pmic, reboot_nb);
	const char * const cmd = (const char * const)command;

	if (cmd) {
		u32 value = U32_MAX;

		dev_info(data->dev, "Reboot command: '%s'\n", cmd);
		if (!strcmp(cmd, "charge"))
			value = REBOOT_MODE_CHARGE;
		else if (!strcmp(cmd, "bootloader"))
			value = REBOOT_MODE_BOOTLOADER;
		else if (!strcmp(cmd, "fastboot"))
			value = REBOOT_MODE_FASTBOOT;
		else if (!strcmp(cmd, "recovery"))
			value = REBOOT_MODE_RECOVERY;
		else if (!strcmp(cmd, "dm-verity device corrupted"))
			value = REBOOT_MODE_DMVERITY_CORRUPTED;
		else if (!strcmp(cmd, "rescue"))
			value = REBOOT_MODE_RESCUE;
		else if (!strcmp(cmd, "shutdown-thermal"))
			value = REBOOT_MODE_SHUTDOWN_THERMAL;
		else if (!strcmp(cmd, "from_fastboot") ||
			 !strcmp(cmd, "shell") ||
			 !strcmp(cmd, "userrequested") ||
			 !strcmp(cmd, "userrequested,fastboot") ||
			 !strcmp(cmd, "userrequested,recovery") ||
			 !strcmp(cmd, "userrequested,recovery,ui"))
			value = REBOOT_MODE_NORMAL;
		else
			dev_err(data->dev, "Unknown reboot command: '%s'\n",
				cmd);
		if (value != U32_MAX)
			regmap_write(data->regmap, REBOOT_MODE_REG, value);
	}

	return NOTIFY_DONE;
}

static int google_pmic_mock_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_pmic *data =
		devm_kzalloc(dev, sizeof(struct google_pmic), GFP_KERNEL);
	struct notifier_block reboot_nb = {
		.notifier_call = reboot_handler,
		.priority = INT_MAX,
	};
	void __iomem *base;

	if (!data)
		return -ENOMEM;

	data->dev = dev;
	data->reboot_nb = reboot_nb;
	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);
	data->regmap =
		devm_regmap_init_mmio(dev, base, &google_pmic_mock_regmap_cfg);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);
	platform_set_drvdata(pdev, data);

	return register_reboot_notifier(&data->reboot_nb);
}

static const struct of_device_id google_pmic_of_match_table[] = {
	{ .compatible = "google,pmic-mock" },
	{},
};
MODULE_DEVICE_TABLE(of, google_pmic_of_match_table);

static struct platform_driver google_pmic_platform_driver = {
	.probe = google_pmic_mock_probe,
	.remove = NULL,
	.driver = {
		.name = "google-pmic-mock",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_pmic_of_match_table),
	},
};
module_platform_driver(google_pmic_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google pmic driver");
MODULE_LICENSE("GPL");
