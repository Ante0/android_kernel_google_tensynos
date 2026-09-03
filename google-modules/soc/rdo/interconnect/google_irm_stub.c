// SPDX-License-Identifier: GPL-2.0-only

#include <linux/types.h>
#include <linux/platform_device.h>

#include "google_irm.h"

static int _irm_vote(struct irm_dev *irm_dev, u32 attr,
		     const struct icc_vote *vote)
{
	return 0;
}

static struct irm_dev_ops __irm_dev_ops = {
	.irm_vote = _irm_vote,
};

static int google_irm_stub_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct irm_dev *irm_dev;

	irm_dev = devm_kzalloc(dev, sizeof(*irm_dev), GFP_KERNEL);
	if (!irm_dev)
		return -ENOMEM;
	irm_dev->dev = dev;

	platform_set_drvdata(pdev, irm_dev);

	irm_dev->ops = &__irm_dev_ops;

	return 0;
}

static void google_irm_stub_platform_remove(struct platform_device *pdev)
{
}

static const struct of_device_id google_irm_stub_of_match_table[] = {
	{ .compatible = "google,irm-stub" },
	{}
};
MODULE_DEVICE_TABLE(of, google_irm_stub_of_match_table);

static struct platform_driver google_irm_stub_platform_driver = {
	.probe = google_irm_stub_platform_probe,
	.remove = google_irm_stub_platform_remove,
	.driver = {
		.name = "google-irm-stub",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_irm_stub_of_match_table),
	},
};
module_platform_driver(google_irm_stub_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google IRM stub driver");
MODULE_LICENSE("GPL");
