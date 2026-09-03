// SPDX-License-Identifier: GPL
/*
 * Google Interface Manager for Pixel Input.
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/device/class.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>

#include "goog_interface_manager.h"
#include "gti_internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "gti: gim: " fmt
#undef dev_fmt
#define dev_fmt(fmt) "gti: " fmt

static struct goog_interface_manager *g_gim;

struct goog_interface *gim_vendor_get_interface(struct device *vendor_dev)
{
	struct goog_interface *interface = NULL;
	u8 i;

	if (!g_gim || !vendor_dev)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(g_gim->interfaces); i++) {
		if (g_gim->interfaces[i].vendor_dev_node &&
		    device_match_of_node(vendor_dev, g_gim->interfaces[i].vendor_dev_node)) {
			interface = &g_gim->interfaces[i];
			break;
		}
	}

	return interface;
}

struct proc_dir_entry *gim_get_interface_proc_root(enum goog_interface_type type)
{
	if (!g_gim)
		return NULL;

	return g_gim->interface_proc_root[type];
}

struct class *gim_get_interface_class(enum goog_interface_type type)
{
	if (!g_gim)
		return NULL;

	return g_gim->interface_class[type];
}

static void goog_interface_device_destroy(struct goog_interface_manager *gim)
{
	u8 idx;

	/*
	 * TODO: support optional GOOG_INTERFACE_TYPE_SB init.
	 */
	for (idx = 0; idx < ARRAY_SIZE(gim->interfaces); idx++) {
		if (!gim->interfaces[idx].context)
			continue;
		if (gim->interfaces[idx].type == GOOG_INTERFACE_TYPE_TOUCH) {
			goog_touch_interface_device_destroy(gim->interfaces[idx].context);
			gim->interfaces[idx].context = NULL;
			gim->interfaces[idx].dev = NULL;
		}
	}
}

static void goog_interface_device_create(struct goog_interface_manager *gim)
{
	u8 idx;
	struct device *dev;
	struct device_node *dn;
	const char *dn_name;
	char *name;

	/*
	 * TODO: support optional GOOG_INTERFACE_TYPE_SB init.
	 */
	for (idx = 0; idx < ARRAY_SIZE(gim->interfaces); idx++) {
		dev = NULL;
		dn = gim->interfaces[idx].vendor_dev_node;
		dn_name = gim_of_node_full_name(dn);
		name = gim->interfaces[idx].name;

		if (!dn)
			continue;

		if (gim->interfaces[idx].type == GOOG_INTERFACE_TYPE_TOUCH) {
			gim->interfaces[idx].context = devm_kzalloc(
				gim->dev, sizeof(struct goog_touch_interface), GFP_KERNEL);
			if (!gim->interfaces[idx].context)
				continue;
			dev = goog_touch_interface_device_create(name,
								 gim->interfaces[idx].context);
			if (IS_ERR_OR_NULL(dev)) {
				pr_warn("device create %s failed for %s\n", name, dn_name);
				devm_kfree(gim->dev, gim->interfaces[idx].context);
				gim->interfaces[idx].context = NULL;
			} else {
				pr_info("device create %s successfully for %s\n", name, dn_name);
				gim->interfaces[idx].dev = dev;
			}
		}
	}
}

static void goog_interface_destroy(struct goog_interface_manager *gim)
{
	u8 idx;

	gim_unregister_panel_bridge(gim);
	goog_interface_device_destroy(gim);

	/*
	 * TODO: support optional GOOG_INTERFACE_TYPE_SB init.
	 */
	for (idx = 0; idx < GOOG_INTERFACE_TYPE_MAX; idx++) {
		if (gim->interface_class[idx]) {
			class_destroy(gim->interface_class[idx]);
			gim->interface_class[idx] = NULL;
		}
		if (gim->interface_proc_root[idx]) {
			proc_remove(gim->interface_proc_root[idx]);
			gim->interface_proc_root[idx] = NULL;
		}
	}
}

static void goog_interface_create(struct goog_interface_manager *gim)
{
	/*
	 * TODO: support optional GOOG_INTERFACE_TYPE_SB init.
	 */
	gim->interface_class[GOOG_INTERFACE_TYPE_TOUCH] = class_create(GTI_NAME);
	gim->interface_proc_root[GOOG_INTERFACE_TYPE_TOUCH] = proc_mkdir(GTI_NAME, NULL);

	goog_interface_device_create(gim);
	gim_register_panel_bridge(gim);
}

static void goog_of_find_input_devices(struct goog_interface_manager *gim)
{
	u32 dev_id;
	struct device_node *dn;
	const char *dn_name;

	if (!gim)
		return;

	/*
	 * TODO: support optional GOOG_INTERFACE_TYPE_SB init from DTS config.
	 */
	for_each_node_by_name(dn, "touchscreen") {
		dn_name = gim_of_node_full_name(dn);
		pr_info("input device %s from %s\n",
			of_device_is_available(dn) ? "enabled" : "disabled", dn_name);
		if (!of_device_is_available(dn))
			continue;

		if (of_property_read_u32(dn, "goog,dev-id", &dev_id))
			dev_id = 0;

		if (dev_id >= MAX_GTI_DEVICES) {
			pr_err("invalid dev_id %u from %s\n", dev_id, dn_name);
			continue;
		}

		if (gim->interfaces[dev_id].vendor_dev_node) {
			struct device_node *dn_conflict = gim->interfaces[dev_id].vendor_dev_node;
			const char *dn_conflict_name = gim_of_node_full_name(dn_conflict);

			pr_err("duplicate dev_id %u between %s and %s\n", dev_id, dn_name,
			       dn_conflict_name);
			continue;
		}

		scnprintf(gim->interfaces[dev_id].name, sizeof(gim->interfaces[dev_id].name),
			  "gti.%d", dev_id);
		gim->interfaces[dev_id].type = GOOG_INTERFACE_TYPE_TOUCH;
		gim->interfaces[dev_id].vendor_dev_node = dn;
		gim->interfaces[dev_id].dev_id = dev_id;
	}
}

static void gim_remove(struct platform_device *pdev)
{
	struct goog_interface_manager *gim = platform_get_drvdata(pdev);

	if (!gim)
		return;

	goog_interface_destroy(gim);
	g_gim = NULL;
}

static int gim_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct goog_interface_manager *gim;

	if (g_gim) {
		ret = -EEXIST;
		goto err_gim_probe;
	}

	gim = devm_kzalloc(dev, sizeof(struct goog_interface_manager), GFP_KERNEL);
	if (!gim) {
		ret = -ENOMEM;
		goto err_gim_probe;
	}

	gim->plat_dev = pdev;
	gim->dev = dev;
	g_gim = gim;
	platform_set_drvdata(pdev, gim);
	goog_of_find_input_devices(gim);
	goog_interface_create(gim);

	return 0;

err_gim_probe:
	pr_err("probe failed, ret %d!\n", ret);
	return ret;
}

static const struct of_device_id gim_of_match[] = {
	{ .compatible = GIM_NAME },
	{},
};
MODULE_DEVICE_TABLE(of, gim_of_match);

static struct platform_driver gim_driver = {
	.driver = {
		.name = GIM_NAME,
		.of_match_table = gim_of_match,
	},
	.probe = gim_probe,
	.remove = gim_remove,
};

static int __init gim_init(void)
{
	return platform_driver_register(&gim_driver);
}
subsys_initcall(gim_init);

static void __exit gim_exit(void)
{
	platform_driver_unregister(&gim_driver);
}
module_exit(gim_exit);

MODULE_DESCRIPTION("Google Interface Manager for Pixel Input");
MODULE_AUTHOR("Super Liu <supercjliu@google.com>");
MODULE_LICENSE("GPL");
