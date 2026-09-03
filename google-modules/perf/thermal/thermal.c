// SPDX-License-Identifier: GPL-2.0
/*
 * thermal.c driver to configure and register NTC thermistors and TMU sensors.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <dt-bindings/soc/google/google-thermal-def.h>

#include "thermal_msg_helper.h"
#include "thermal_zone_helper.h"
#include "thermal_ntc_helper.h"
#include "thermal_tmu_helper.h"

LIST_HEAD(thermal_driver_list);

struct thermal_client_driver_data {
	struct thermal_zone_device_ops ops;
	int max_sensors;
	int (*configure)(struct platform_device *pdev, int sensor_ct);
	int (*fault_trip)(struct thermal_zone_device *tzd, int temp);
	void (*cleanup)(struct platform_device *pdev);
	int (*configure_sensor)(struct google_sensor_data *sens);
	int (*cleanup_sensor)(struct google_sensor_data *sens);
	int (*post_configure)(struct list_head *sensor_list);
	bool ignore_genl;
	bool irq_wakeable;
};

struct thermal_driver_data {
	const struct thermal_client_driver_data *client_data;
	struct list_head thermal_sensor_list;
	struct list_head node;
};

const struct thermal_client_driver_data google_ntc_thermal_ops = {
	.ops =  {
		.get_temp = thermal_ntc_get_temp,
		.set_trips = thermal_ntc_set_trips,
		.set_trip_temp = thermal_ntc_set_trip_temp,
	},
	.max_sensors = NTC_THERMAL_MAX_CHANNEL,
	.configure = thermal_ntc_configure,
	.fault_trip = thermal_ntc_set_fault_trip,
	.cleanup = thermal_ntc_cleanup,
	.configure_sensor = thermal_ntc_configure_sensor,
	.cleanup_sensor = thermal_ntc_cleanup_sensor,
	.post_configure = thermal_ntc_post_configure,
	.irq_wakeable = true,
};

const struct thermal_client_driver_data google_tmu_thermal_ops = {
	.ops =  {
		.get_temp = thermal_tmu_get_temp,
		.set_trip_temp = thermal_tmu_set_trip_temp,
	},
	.max_sensors = TZ_MAX,
	.configure = thermal_tmu_configure,
	.cleanup = thermal_tmu_cleanup,
	.configure_sensor = thermal_tmu_configure_sensor,
	.cleanup_sensor = thermal_tmu_cleanup_sensor,
	.post_configure = thermal_tmu_post_configure,
	.ignore_genl = true,
};

static const struct of_device_id thermal_match_table[] = {
	{
		.compatible = "google,ntc-thermal",
		.data = &google_ntc_thermal_ops,
	},
	{
		.compatible = "google,tmu-thermal",
		.data = &google_tmu_thermal_ops,
	},
	{}
};
MODULE_DEVICE_TABLE(of, thermal_match_table);

static void thermal_exit(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	struct list_head *n, *pos;

	list_for_each_safe(pos, n, &thermal_driver_list) {
		struct thermal_driver_data *drv_data =
				list_entry(pos, struct thermal_driver_data, node);

		if (drv_data->client_data->cleanup_sensor) {
			struct google_sensor_data *sens;

			list_for_each_entry(sens, &drv_data->thermal_sensor_list, node) {
				ret = drv_data->client_data->cleanup_sensor(sens);
				if (ret) {
					dev_warn(sens->dev,
						 "cleanup_sensor error for sensor:%d ret:%d.\n",
						 sens->sensor_id, ret);
				}
			}
		}

		// Error check is best effort. continue on error to do rest of the cleanup.
		ret = thermal_unregister_sensors(dev, &drv_data->thermal_sensor_list);
		if (ret)
			dev_warn(dev, "Thermal zone unregister error:%d. Continuing...\n",
				 ret);

		if (drv_data->client_data->cleanup)
			drv_data->client_data->cleanup(pdev);
		list_del(&drv_data->node);
		devm_kfree(dev, drv_data);
	}
}

static int thermal_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	const struct of_device_id *dev_id = NULL;
	struct thermal_driver_data *drv_data = NULL;
	const struct thermal_client_driver_data *clnt_data;

	dev_id = of_match_node(thermal_match_table, pdev->dev.of_node);
	if (!dev_id) {
		dev_err(dev, "No matching table for the compatible flag:%s\n",
			pdev->dev.of_node->name);
		return -ENODEV;
	}

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (IS_ERR_OR_NULL(drv_data))
		return -ENOMEM;

	clnt_data = drv_data->client_data = dev_id->data;
	if (!clnt_data) {
		dev_err(dev, "driver data not initialized.\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&drv_data->thermal_sensor_list);
	if (clnt_data->configure) {
		ret = clnt_data->configure(pdev, clnt_data->max_sensors);
		if (ret) {
			dev_err(dev, "Hardware probe and configure error:%d.\n", ret);
			return ret;
		}
	}

	ret = thermal_register_sensors(dev, clnt_data->max_sensors, &clnt_data->ops,
				       &drv_data->thermal_sensor_list, clnt_data->ignore_genl,
				       clnt_data->irq_wakeable);
	if (ret)
		goto thermal_probe_error;

	if (clnt_data->fault_trip) {
		ret = thermal_configure_fault_threshold(&drv_data->thermal_sensor_list,
							clnt_data->fault_trip);
		if (ret)
			goto thermal_probe_error;
	}

	if (clnt_data->post_configure) {
		ret = clnt_data->post_configure(&drv_data->thermal_sensor_list);
		if (ret)
			goto thermal_probe_error;
	}

	if (clnt_data->configure_sensor) {
		struct google_sensor_data *sens;

		list_for_each_entry(sens, &drv_data->thermal_sensor_list, node) {
			ret = clnt_data->configure_sensor(sens);
			if (ret) {
				dev_err(sens->dev, "configure_sensor error for sensor:%d ret:%d.\n",
					sens->sensor_id, ret);
				goto thermal_probe_error;
			}
		}
	}

	list_add_tail(&drv_data->node, &thermal_driver_list);

	return ret;

thermal_probe_error:
	thermal_exit(pdev);
	return ret;
}

static struct platform_driver thermal_sensor_driver = {
	.driver = {
		.name = "thermal-sensors",
		.probe_type = PROBE_FORCE_SYNCHRONOUS,
		.owner = THIS_MODULE,
		.of_match_table = thermal_match_table,
	},
	.probe = thermal_probe,
	.remove = thermal_exit,
};
module_platform_driver(thermal_sensor_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
MODULE_AUTHOR("Jikai Ma <jikai@google.com>");
MODULE_DESCRIPTION("Google LLC Thermal Driver framework");
MODULE_ALIAS("platform:thermal-sensors");
