// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel driver for NOA ring manager
 *
 * Copyright 2025 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/device/driver.h>

#include "ring_shared_info.h"
#include "ring_manager.h"

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
static ssize_t ring_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return NoaRingSharedInfoDumpAll(buf, PAGE_SIZE);
}
static DEVICE_ATTR_RO(ring_info);

static struct attribute *noa_ring_manager_attrs[] = {
	&dev_attr_ring_info.attr,
	NULL,
};

static const struct attribute_group noa_ring_manager_attr_group = {
	.attrs = noa_ring_manager_attrs,
};
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)

static int noa_ring_manager_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	ret = sysfs_create_group(&dev->kobj, &noa_ring_manager_attr_group);
	if (ret) {
		dev_err(dev, "Failed to create sysfs group, ret %d\n", ret);
		goto out;
	}

	ret = 0;
out:
	return ret;
}

static void noa_ring_manager_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &noa_ring_manager_attr_group);
}

static const struct of_device_id noa_ring_manager_of_match[] = {
	{
		.compatible = "google,dpa-ring-manager",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, noa_ring_manager_of_match);

static struct platform_driver noa_ring_manager_driver = {
    .probe = noa_ring_manager_probe,
    .remove = noa_ring_manager_remove,
    .driver = {
        .name = "dpa_ring_manager",
	.owner = THIS_MODULE,
        .of_match_table = noa_ring_manager_of_match,
    },
};

#else /* CONFIG_NOA_FULLSOC_SUPPORT */

static struct noa_ring_manager_driver *ring_mgmt;

static struct noa_ring *get_ring_shared_info(const struct NoaRingSharedInfoRoot *root,
					     uint8_t interface, uint8_t flow, uint8_t category,
					     uint8_t direction)
{
	return &root->base->entries[interface]->entries[flow]->entries[category].entries[direction];
}

static int init_ring_manager_in_simulator(void)
{
	ring_mgmt = kzalloc(sizeof(*ring_mgmt), GFP_KERNEL);
	if (!ring_mgmt) {
		return -ENOMEM;
	}
	ring_mgmt->shared_info.base = NoaRingSharedInfoRootBaseInstance();
	ring_mgmt->shared_info.get_ring = get_ring_shared_info;
	NoaRingSharedInfoRootRegister(&ring_mgmt->shared_info);
	return 0;
}

static void deinit_ring_manager_in_simulator(void)
{
	if (!ring_mgmt) {
		return;
	}
	kfree(ring_mgmt);
	ring_mgmt = NULL;
}
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */

static int __init noa_ring_manager_driver_init(void)
{
	int ret;

#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
	ret = platform_driver_register(&noa_ring_manager_driver);
#else /* CONFIG_NOA_FULLSOC_SUPPORT */
	ret = init_ring_manager_in_simulator();
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
	if (ret) {
		pr_err("Failed to init noa ring manager driver\n");
		goto out;
	}

	ret = 0;
out:
	return ret;
}
module_init(noa_ring_manager_driver_init);

static void __exit noa_ring_manager_driver_exit(void)
{
#if IS_ENABLED(CONFIG_NOA_FULLSOC_SUPPORT)
	platform_driver_unregister(&noa_ring_manager_driver);
#else /* CONFIG_NOA_FULLSOC_SUPPORT */
	deinit_ring_manager_in_simulator();
#endif /* CONFIG_NOA_FULLSOC_SUPPORT */
}
module_exit(noa_ring_manager_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Hu <kaiwenhu@google.com>");
MODULE_DESCRIPTION("NOA ring manager kernel module");
