// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_zone_helper.c Helper to register thermal zone.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <kunit/static_stub.h>
#include <kunit/visibility.h>

#include "thermal_zone_helper.h"
#include "thermal_zone_mock.h"
#include <soc/google/thermal.h>

VISIBLE_IF_KUNIT void tzh_register_tz_id_irq_wakeable(int tz_id)
{
	KUNIT_STATIC_STUB_REDIRECT(tzh_register_tz_id_irq_wakeable, tz_id);
	register_tz_id_irq_wakeable(tz_id);
}

/*
 * thermal_register_sensors - Register the thermal sensors with thermal zone.
 *
 * This helper will be used by the sensor drivers to register sensor_ct number of
 * sensors, starting with sensor ID 0. The sensor driver should provide the ops
 * that it supports for this helper to use and the list head for the helper to add.
 * This helper will continue to register if a thermal zone is not defined in the
 * devicetree.
 *
 * @dev: Device pointer for the sensor device.
 * @sensor_ct: Maximum number of sensors this device supports.
 * @ops: A set of thermal sensor ops.
 * @head: A list head to be populated with the sensors.
 * @ignore_genl: If true, ignore generic netlink for the thermal zone.
 * @irq_wakeable: If true, register the thermal zone as IRQ wakeable.
 *
 * Return: 0 on success.
 *      - EINVAL if the input argument is not expected.
 *      - ENOMEM if memory allocation error for the structure.
 *      - Other error returned by the function devm_thermal_of_zone_register.
 */
int thermal_register_sensors(struct device *dev, int sensor_ct,
			    const struct thermal_zone_device_ops *ops,
			    struct list_head *head, bool ignore_genl,
			    bool irq_wakeable)
{
	int ret = 0, i = 0;
	struct google_sensor_data *sens = NULL;

	if (!dev || !ops || !head || sensor_ct <= 0)
		return -EINVAL;

	if (!ops->get_temp) {
		dev_err(dev, "Invalid callback combination.\n");
		return -EINVAL;
	}

	for (i = 0; i < sensor_ct; i++) {
		sens = devm_kzalloc(dev, sizeof(*sens), GFP_KERNEL);
		if (!sens) {
			return -ENOMEM;
			goto register_error;
		}

		sens->dev = dev;
		sens->sensor_id = i;
		sens->sens_data.ntc_data.low_trip = -INT_MAX;
		sens->sens_data.ntc_data.high_trip = INT_MAX;
		sens->sens_data.ntc_data.fault_trip = INT_MAX;
		sens->sens_data.ntc_data.tr_stats_handle = -1;
		sens->tzd = register_thermal_zone(dev, i, sens, ops);
		if (!IS_ERR(sens->tzd) && ignore_genl)
			register_tz_id_ignore_genl(sens->tzd->id);
		if (!IS_ERR(sens->tzd) && irq_wakeable)
			tzh_register_tz_id_irq_wakeable(sens->tzd->id);

		if (IS_ERR(sens->tzd)) {
			ret = PTR_ERR(sens->tzd);
			if (ret == -ENODEV) {
				dev_warn(dev,
					  "Thermal zone not defined for ID:%d. Continuing.\n",
					  i);
				devm_kfree(dev, sens);
				ret = 0;
				continue;
			}
			dev_err(dev,
				"Failed to register sensor with ID:%d. err:%d\n",
				i, ret);
			goto register_error;
		}
		list_add_tail(&sens->node, head);
	}

	return ret;

register_error:
	thermal_unregister_sensors(dev, head);

	return ret;

}

/*
 * thermal_unregister_sensors - Unregister the thermal zones.
 *
 * unregisters all the sensors and its associated thermal zone. Traverse the list
 * head to find the list of sensors that needs to be unregistered.
 *
 * @dev: Device pointer for the sensor device.
 * @head: A list head that will be traversed to find the sensors.
 *
 * Return: 0 on success.
 *      - EINVAL if the input argument is not expected.
 */
int thermal_unregister_sensors(struct device *dev, struct list_head *head)
{
	struct list_head *n, *pos;

	if (!dev || !head)
		return -EINVAL;

	list_for_each_safe(pos, n, head) {
		struct google_sensor_data *sens =
				list_entry(pos, struct google_sensor_data,
					   node);
		unregister_thermal_zone(dev, sens);
		list_del(&sens->node);
		devm_kfree(dev, sens);
	}

	return 0;
}

/*
 * thermal_fetch_and_register_irq - Fetch the irq info from devicetree and register.
 *
 * This function looks for the interrupt information from the devicetree and
 * registers for it. It uses the interrupt handler available in the zone helper
 * function.
 *
 * @pdev: The platform device.
 * @handler: Threaded IRQ handler.
 * @data: The data that needs to be passed back to the IRQ handler.
 *
 * Return: 0 on success.
 */
int thermal_fetch_and_register_irq(struct platform_device *pdev,
				   irq_handler_t handler, void *data,
				   unsigned long irqflags)
{
	int ret = 0, irq_num = 0;
	struct device *dev;

	if (!pdev)
		return -EINVAL;

	dev = &pdev->dev;
	irq_num = get_irq_optional(pdev);
	if (irq_num < 0) {
		dev_err(dev, "IRQ fetch error:%d\n", irq_num);
		return ret;
	}

	ret = register_threaded_irq(dev, irq_num, handler, irqflags, data);
	if (ret) {
		dev_err(dev, "IRQ register error.:%d\n", ret);
		return ret;
	}

	return ret;
}


/*
 * thermal_setup_irq_and_sensors - Fetch IRQ information from devicetree and
 * register thermal zones.
 *
 * @pdev: The platform device.
 * @sensor_ct: Maximum number of sensors this device supports.
 * @ops: A set of thermal sensor ops.
 * @head: A list head to be populated with the sensors.
 * @handler: Threaded IRQ handler.
 *
 * Return: 0 on success.
 *      - Other error returned by thermal_register_sensors and
 *      thermal_fetch_and_register_irq.
 */
int thermal_setup_irq_and_sensors(struct platform_device *pdev, int sensor_ct,
				  const struct thermal_zone_device_ops *ops,
				  struct list_head *head, irq_handler_t handler,
				  unsigned long irqflags)
{
	int ret = 0;
	struct device *dev;

	if (!pdev)
		return -EINVAL;

	dev = &pdev->dev;
	ret = thermal_fetch_and_register_irq(pdev, handler, head, irqflags);
	if (ret)
		goto setup_irq_sensor_exit;

	ret = thermal_register_sensors(dev, sensor_ct, ops, head, false, false);
	if (ret)
		goto setup_irq_sensor_exit;

setup_irq_sensor_exit:
	return ret;
}

struct thermal_configure_fault_threshold_trip_walk_data {
	struct google_sensor_data *sens;
	set_fault_trip_handler *fault_trip;
};

static int thermal_configure_fault_threshold_trip_walk_cb(struct thermal_trip *trip,
							  void *data)
{
	struct thermal_configure_fault_threshold_trip_walk_data * const twd = data;
	struct google_sensor_data *sens = twd->sens;
	int ret;

	if (trip->type != THERMAL_TRIP_HOT)
		return 0;

	dev_info(sens->dev, "sensor:%d configure fault threshold:%d\n",
		 sens->sensor_id, trip->temperature);
	ret = twd->fault_trip(sens->tzd, trip->temperature);
	if (ret) {
		dev_err(sens->dev, "sensor:%d fault trip error:%d.\n",
			sens->sensor_id, ret);
		return ret;
	}
	return 0;
}

/*
 * thermal_configure_fault_threshold - Configure hardware fault threshold.
 *
 * For each sensor, find if the thermal zone has a 'hot' trip type configured and
 * call the provided callback to configure the threshold for that sensor.
 *
 * @head: Head pointer to the list of sensors.
 * @fault_trip: h/w driver callback to configure the trip.
 *
 * Return: 0 on success.
 *	-EINVAL for invalid argument.
 *	- Other error returned by thermal_zone_get_trip() and the driver fault_trip
 *	callback.
 */
int thermal_configure_fault_threshold(struct list_head *head,
				      set_fault_trip_handler fault_trip)
{
	int ret = 0;
	struct list_head *pos;
	struct thermal_configure_fault_threshold_trip_walk_data twd = {
		.fault_trip = fault_trip,
	};

	if (!head || !fault_trip)
		return -EINVAL;

	list_for_each(pos, head) {
		twd.sens = list_entry(pos, struct google_sensor_data, node);

		ret = thermal_zone_for_each_trip(twd.sens->tzd,
						 thermal_configure_fault_threshold_trip_walk_cb,
						 &twd);
		if (ret)
			return ret;
	}

	return ret;
}
