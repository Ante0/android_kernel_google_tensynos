// SPDX-License-Identifier: GPL-2.0-only
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "apt_internal.h"

static int google_apt_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_apt *apt;
	int i;
	char *irqname;
	int err;

	apt = devm_kzalloc(dev, sizeof(*apt), GFP_KERNEL);
	if (!apt)
		return -ENOMEM;
	platform_set_drvdata(pdev, apt);

	apt->dev = dev;
	apt->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(apt->base))
		return PTR_ERR(apt->base);

	if (!dev->of_node)
		return -ENODEV;

	err = of_property_read_u32(dev->of_node, "prescaler", &apt->prescaler);
	if (err < 0) {
		dev_err(dev, "prescaler read error: %d", err);
		return err;
	}

	for (i = 0; i < GOOGLE_APT_NUM_GT_COMP; ++i) {
		irqname = kasprintf(GFP_KERNEL, "gt_comp%d", i);
		if (!irqname)
			return -ENOMEM;
		apt->gt_comp[i].irq = platform_get_irq_byname(pdev, irqname);
		kfree(irqname);

		if (apt->gt_comp[i].irq < 0) {
			dev_err(dev, "failed to get irq for gt_comp %d: %d", i,
				apt->gt_comp[i].irq);
			return apt->gt_comp[i].irq;
		}
	}
	for (i = 0; i < GOOGLE_APT_NUM_LT; ++i) {
		irqname = kasprintf(GFP_KERNEL, "lt%d", i);
		if (!irqname)
			return -ENOMEM;
		apt->lt[i].irq = platform_get_irq_byname(pdev, irqname);
		kfree(irqname);

		if (apt->lt[i].irq < 0) {
			dev_err(dev, "failed to get irq for lt %d: %d", i,
				apt->lt[i].irq);
			return apt->lt[i].irq;
		}
	}
	return google_apt_init(apt);
}

static void google_apt_platform_remove(struct platform_device *pdev)
{
	struct google_apt *apt = platform_get_drvdata(pdev);

	google_apt_exit(apt);
}

static const struct of_device_id google_apt_of_match_table[] = {
	{ .compatible = "google,apt" },
	{},
};
MODULE_DEVICE_TABLE(of, google_apt_of_match_table);

static struct platform_driver google_apt_driver = {
	.probe = google_apt_platform_probe,
	.remove = google_apt_platform_remove,
	.driver = {
		.name = "google-apt",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_apt_of_match_table),
	},
};

static int google_apt_platform_init(void)
{
	int err;

	err = google_apt_debugfs_init();
	if (err)
		pr_warn("Failed to initialize debugfs for google_apt");

	return platform_driver_register(&google_apt_driver);
}
module_init(google_apt_platform_init);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google APT platform driver");
MODULE_LICENSE("GPL");
