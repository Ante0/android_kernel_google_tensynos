// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "google_boot_diary.h"

static int google_boot_diary_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct boot_context *boot_ctx;
	u32 memory_address;
	void __iomem *boot_diary_address;
	void __iomem *boot_context_address;
	struct debugfs_blob_wrapper *boot_diary_blob;
	struct google_boot_diary *google_boot_diary;

	google_boot_diary = devm_kzalloc(dev, sizeof(*google_boot_diary), GFP_KERNEL);
	if (!google_boot_diary)
		return -ENOMEM;

	boot_ctx = devm_kzalloc(dev, sizeof(*boot_ctx), GFP_KERNEL);
	if (!boot_ctx)
		return -ENOMEM;

	boot_diary_blob = devm_kzalloc(dev, sizeof(*boot_diary_blob), GFP_KERNEL);
	if (!boot_diary_blob)
		return -ENOMEM;

	if (of_property_read_u32(np, "memory-address",
				 &memory_address)) {
		dev_err(dev, "No boot-dairy address\n");
		return -EINVAL;
	}

	boot_context_address = devm_ioremap(dev, memory_address, sizeof(struct boot_context));
	memcpy_fromio(boot_ctx, boot_context_address, sizeof(struct boot_context));

	boot_diary_address = devm_ioremap(dev,
					  (resource_size_t)boot_ctx->boot_diary,
					  sizeof(struct boot_diary_log));

	boot_diary_blob->data = boot_diary_address;
	boot_diary_blob->size = sizeof(struct boot_diary_log);

	google_boot_diary->debugfs_root = debugfs_create_dir(dev_name(dev), NULL);
	debugfs_create_blob("boot_diary_blob", 0440, google_boot_diary->debugfs_root,
			    boot_diary_blob);

	return 0;
}

static void google_boot_diary_platform_remove(struct platform_device *pdev)
{
	struct google_boot_diary *google_boot_diary = (struct google_boot_diary *)
						       platform_get_drvdata(pdev);

	debugfs_remove_recursive(google_boot_diary->debugfs_root);
}

static const struct of_device_id google_boot_diary_of_match_table[] = {
	{ .compatible = "google,boot-diary" },
	{},
};
MODULE_DEVICE_TABLE(of, google_boot_diary_of_match_table);

static struct platform_driver google_boot_diary_platform_driver = {
	.probe = google_boot_diary_platform_probe,
	.remove = google_boot_diary_platform_remove,
	.driver = {
		.name = "google-boot-diary",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_boot_diary_of_match_table),
	},
};
module_platform_driver(google_boot_diary_platform_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google boot-diary driver");
MODULE_LICENSE("GPL");
