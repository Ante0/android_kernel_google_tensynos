// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024 Google LLC */
#include <linux/io.h>

#include "google_thermal_cooling.h"
#include "thermal_core.h"
#include "thermal/cpm_thermal.h"

#define to_cdev(dev) container_of(dev, struct thermal_cooling_device, device)
#define to_thermal_data(dev) ((struct thermal_data *)to_cdev(dev)->devdata)

static int thermal_tj_set_cur_state(struct thermal_cooling_device *cdev,
				    unsigned long state)
{
	return 0;
}

static int thermal_tj_get_max_state(struct thermal_cooling_device *cdev,
				    unsigned long *state)
{
	*state = 0;
	return 0;
}

static int thermal_tj_get_cur_state(struct thermal_cooling_device *cdev, unsigned long *state)
{
	struct thermal_data *data = cdev->devdata;

	if (!data->curr_state_addr)
		return -ENODATA;

	memcpy_fromio(&data->curr_state, data->curr_state_addr, sizeof(data->curr_state));

	*state = data->curr_state.cdev_state;

	return 0;
}

static const struct thermal_cooling_device_ops thermal_tj_ops = {
	.get_cur_state = thermal_tj_get_cur_state,
	.set_cur_state = thermal_tj_set_cur_state,
	.get_max_state = thermal_tj_get_max_state,
};

struct thermal_cooling_device *tz2poweractor(struct thermal_zone_device *tz)
{
	struct thermal_instance *instance;

	mutex_lock(&tz->lock);
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		if (cdev_is_power_actor(instance->cdev)) {
			mutex_unlock(&tz->lock);
			return instance->cdev;
		}
	}
	mutex_unlock(&tz->lock);
	return NULL;
}

int google_thermal_cooling_init(struct thermal_data *data)
{
	char cdev_name[THERMAL_NAME_LENGTH];

	scnprintf(cdev_name, THERMAL_NAME_LENGTH, "%s-tj", data->tz->type);
	data->tj_cooling = devm_thermal_of_cooling_device_register(data->dev,
								   data->dev->of_node,
								   cdev_name,
								   data,
								   &thermal_tj_ops);
	if (IS_ERR(data->tj_cooling))
		return PTR_ERR(data->tj_cooling);

	return 0;
}
