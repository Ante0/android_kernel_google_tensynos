// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_cpm_mbox driver to communicate with CPM.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include "include/thermal_cpm_mbox.h"
#include "include/thermal_cpm_mbox_helper.h"
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "thermal_cpm_mbox_mock.h"

static struct thermal_cpm_mbox_driver_data *thermal_cpm_data;

int thermal_cpm_send_mbox_req(union thermal_cpm_message *message, int *status)
{
	return __thermal_cpm_send_mbox_req(thermal_cpm_data, message, status);
}
EXPORT_SYMBOL_GPL(thermal_cpm_send_mbox_req);

int thermal_cpm_send_mbox_msg(union thermal_cpm_message message)
{
	return __thermal_cpm_send_mbox_msg(thermal_cpm_data, message);
}
EXPORT_SYMBOL_GPL(thermal_cpm_send_mbox_msg);

int thermal_cpm_mbox_register_notification(enum hw_dev_type type,
					   struct notifier_block *nb)
{
	return __thermal_cpm_mbox_register_notification(thermal_cpm_data, type, nb);
}
EXPORT_SYMBOL_GPL(thermal_cpm_mbox_register_notification);

void thermal_cpm_mbox_unregister_notification(enum hw_dev_type type,
					      struct notifier_block *nb)
{
	__thermal_cpm_mbox_unregister_notification(thermal_cpm_data, type, nb);
}
EXPORT_SYMBOL_GPL(thermal_cpm_mbox_unregister_notification);

int thermal_cpm_mbox_cdev_to_hw_tzid(enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid)
{
	return hw_cdev_id_to_hw_tzid(thermal_cpm_data, cdev_id, hw_tzid);
}
EXPORT_SYMBOL_GPL(thermal_cpm_mbox_cdev_to_hw_tzid);

const char *thermal_cpm_mbox_get_tz_name(enum hw_thermal_zone_id hw_tzid)
{
	return get_tz_name(thermal_cpm_data, hw_tzid);
}
EXPORT_SYMBOL_GPL(thermal_cpm_mbox_get_tz_name);

int thermal_cpm_mbox_sensor_id_to_hw_tzid(int sensor_tzid, enum hw_thermal_zone_id *hw_tzid)
{
	return sensor_id_to_hw_tzid(thermal_cpm_data, sensor_tzid, hw_tzid);
}
EXPORT_SYMBOL_GPL(thermal_cpm_mbox_sensor_id_to_hw_tzid);

static int thermal_cpm_mbox_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct thermal_cpm_mbox_driver_data *drv_data;
	int ret;

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->dev = dev;

	ret = thermal_cpm_mbox_probe_helper(drv_data);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, drv_data);

	thermal_cpm_data = drv_data;

	return ret;
}

static void thermal_cpm_mbox_platform_remove(struct platform_device *pdev)
{
	struct thermal_cpm_mbox_driver_data *drv_data = platform_get_drvdata(pdev);

	thermal_cpm_mbox_free(drv_data);
}

static const struct of_device_id thermal_cpm_mbox_of_match_table[] = {
	{ .compatible = "thermal-cpm-mbox",
	},
	{},
};
MODULE_DEVICE_TABLE(of, thermal_cpm_mbox_of_match_table);

static struct platform_driver thermal_cpm_mbox_driver = {
	.probe = thermal_cpm_mbox_platform_probe,
	.remove_new = thermal_cpm_mbox_platform_remove,
	.driver = {
		.name = "thermal-cpm-mbox",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(thermal_cpm_mbox_of_match_table),
	},
};
module_platform_driver(thermal_cpm_mbox_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Hsiao <pinhsinh@google.com>");
MODULE_AUTHOR("Jikai Ma <jikai@google.com>");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
MODULE_DESCRIPTION("Google LLC Thermal CPM Mbox Interface");
MODULE_LICENSE("GPL");
