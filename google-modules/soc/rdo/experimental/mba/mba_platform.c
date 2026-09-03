// SPDX-License-Identifier: GPL-2.0-only
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include "mba_internal.h"

static int google_mba_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_mba *mba;
	int i;
	char *irqname;

	mba = devm_kzalloc(dev, sizeof(*mba), GFP_KERNEL);
	if (!mba)
		return -ENOMEM;
	platform_set_drvdata(pdev, mba);

	mba->dev = dev;

	mba->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mba->base))
		return PTR_ERR(mba->base);

	mba->host.irq = platform_get_irq_byname(pdev, "host");
	if (mba->host.irq < 0) {
		dev_err(dev, "failed to get irq for host side\n");
		return mba->host.irq;
	}

	for (i = 0; i < MBA_NUM_CLIENT; i++) {
		irqname = kasprintf(GFP_KERNEL, "client%d", i);
		if (!irqname)
			return -ENOMEM;
		mba->clients[i].irq = platform_get_irq_byname(pdev, irqname);
		kfree(irqname);

		// AP side normally takes cares of only the host side. So we
		// only warn here instead of error-and-exit.
		if (mba->clients[i].irq < 0)
			dev_warn(dev,
				 "failed to get irq for %i-th client side\n",
				 i);
	}

	return google_mba_init(mba);
}

static void google_mba_platform_remove(struct platform_device *pdev)
{
	struct google_mba *mba = platform_get_drvdata(pdev);

	google_mba_exit(mba);
}

static const struct of_device_id google_mba_of_match_table[] = {
	{ .compatible = "google,mba" },
	{},
};
MODULE_DEVICE_TABLE(of, google_mba_of_match_table);

static struct platform_driver google_mba_driver = {
	.probe = google_mba_platform_probe,
	.remove = google_mba_platform_remove,
	.driver = {
		.name = "google-mba",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_mba_of_match_table),
	},
};

static int google_mba_platform_init(void)
{
	int err;

	err = google_mba_debugfs_init();
	if (err)
		pr_warn("Failed to initialize debugfs for google_mba");

	return platform_driver_register(&google_mba_driver);
}
module_init(google_mba_platform_init);

static void google_mba_platform_exit(void)
{
	google_mba_debugfs_exit();
}
module_exit(google_mba_platform_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google mailbox array driver");
MODULE_LICENSE("GPL");
