// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_sm_helper.c Helper for thermal shared memory management
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#include <linux/io.h>

#include "thermal_sm_helper.h"
#include "thermal_sm_mock.h"

static const u8 support_section_ver[THERMAL_SM_MAX_SECTION] = {
	[THERMAL_SM_CDEV_STATE] = 1,
	[THERMAL_SM_STATS] = 1,
	[THERMAL_SM_TRIP_COUNT] = 0,
	[THERMAL_SM_TJ_PRESSURE] = 0,
};

static struct thermal_sm_section_data thermal_sm_sections[THERMAL_SM_MAX_SECTION];

/*
 * thermal_sm_initialize_section - initialize a thermal shared memory section
 *
 * @dev: device pointer to the sensro device
 * @section: section of thermal shared memory
 *
 * return: 0 on success
 * 	-EINVAL with NULL dev or invalid section;
 * 	-EIO: NULL sm addr or 0 size
 * 	-EOPNOTSUPP: sm version not supported
 * 	other error returned when request section address.
 */
int thermal_sm_initialize_section(struct device *dev, enum thermal_sm_section section)
{
	u32 section_ver = 0, addr = 0, size = 0, supported_ver;
	void *remap_addr = NULL;
	int ret;

	if (!dev || section >= THERMAL_SM_MAX_SECTION)
		return -EINVAL;

	/* return if section is already initialized */
	if (thermal_sm_sections[section].addr != NULL)
		return 0;

	ret = thermal_sm_get_section_addr(section, &section_ver, &addr, &size);
	if (ret) {
		dev_warn(dev, "Failed to get thermal_sm addr: section=%d ret=%d", section, ret);
		return ret;
	}

	if (addr == 0 || size == 0) {
		dev_warn(dev, "Invalid thermal_sm: section=%d, addr=%d, size=%d",
			 section, addr, size);
		return -EIO;
	}

	supported_ver = support_section_ver[section];
	if (section_ver > supported_ver) {
		dev_warn(dev,
			 "Unsupported thermal_sm section: section=%d, ver=%d, supported_ver=%d",
			 section, section_ver, supported_ver);
		return -EOPNOTSUPP;
	}

	remap_addr = thermal_sm_devm_ioremap(dev, addr, size);
	if (IS_ERR_OR_NULL(remap_addr)){
		dev_warn(dev, "Failed to remap section addr: section=%d", section);
		return -ENOMEM;
	}
	thermal_sm_sections[section].size = size;
	thermal_sm_sections[section].addr = remap_addr;

	dev_dbg(dev, "thermal_sm section init: section=%d, ver=%d, addr=%d, size=%d",
		section, section_ver, addr, size);

	return 0;
}

/*
 * thermal_sm_get_tmu_cdev_state: read cdev state of a HW thermal zone
 *
 * @cdev_id: HW cdev ID
 * @cdev_state: pointer to the output of thermal zone cdev state
 *
 * Return: 0 on success
 * 	-EINVAL: NULL cdev_state
 * 	-ENODEV: invalid sm addr or sm doesn't exit for cdev_id
 * 	error code return from cdev_to_tzid translation
 */
int thermal_sm_get_tmu_cdev_state(enum hw_dev_type cdev_id, u8 *cdev_state)
{
	struct thermal_sm_tmu_state_data tz_state;
	struct thermal_sm_section_data *sm_state_section =
		&thermal_sm_sections[THERMAL_SM_CDEV_STATE];
	enum hw_thermal_zone_id tz_id;
	u32 offset;
	int ret;

	if (!cdev_state)
		return -EINVAL;

	ret = thermal_sm_cdev_to_hw_tzid(cdev_id, &tz_id);
	if (ret) {
		pr_err("%s: Invalid cdev_id: %d", __func__, cdev_id);
		return ret;
	}

	offset = (u32)tz_id * sizeof(tz_state);

	if (!sm_state_section->addr || tz_id >= sm_state_section->size / sizeof(tz_state))
		return -ENODEV;

	thermal_sm_memcpy_fromio(&tz_state, sm_state_section->addr, offset, sizeof(tz_state));
	*cdev_state = tz_state.cdev;
	return 0;
}

/*
 * get_data_from_sm_section - Copy data from shared memory section
 *
 * @section: thermal shared memory section
 * @offset : offset from shared memory section start position
 * @size : size of the data to copy
 * @data : destination buffer to copy data
 *
 * Return: 0 on success
 *	-EINVAL: invalid arguments
 *	-ENODEV: shared memory section isn't initialized properly
 */
static int get_data_from_sm_section(enum thermal_sm_section section, u32 offset, u32 size, u8 *data)
{
	struct thermal_sm_section_data *section_data;

	if (!data || section >= THERMAL_SM_MAX_SECTION)
		return -EINVAL;

	if (!size)
		return 0;

	section_data = &thermal_sm_sections[section];
	if (section_data->size == 0 || !section_data->addr) {
		pr_err("thermal shared memory init error: section=%d addr=%lld, size=%d",
		       section, (u64)section_data->addr, section_data->size);
		return -ENODEV;
	}

	if (offset + size > section_data->size) {
		pr_err("Invalid offset or size: offset=%d, size=%d, section_size=%d", offset, size,
		       section_data->size);
		return -EINVAL;
	}

	thermal_sm_memcpy_fromio(data, section_data->addr, offset, size);

	return 0;
}

/*
 * thermal_sm_get_thermal_stats_metrics - copy thermal_stats metrics from thermal stats section
 *
 * @data: pointer to the thermal_stats metrics data structure destination
 *
 * Return: 0 on success
 *	error-code: error returned by get_data_from_sm_section()
 */
int thermal_sm_get_thermal_stats_metrics(struct thermal_sm_stats_metrics *data)
{
	int metrics_offset = offsetof(struct thermal_sm_stats_data, metrics);

	return get_data_from_sm_section(THERMAL_SM_STATS, metrics_offset, sizeof(*data),
					(u8 *)data);
}

/*
 * thermal_sm_get_tj_pressure_data - copy tj thermal pressure data from shared memory
 *
 * @data: pointer to the destination buffer
 * @size: number of bytes to read
 *
 * Return: 0 on success
 *	error-code: error returned by get_data_from_sm_section()
 */
int thermal_sm_get_tj_pressure_data(u8 *data, u32 size)
{
	return get_data_from_sm_section(THERMAL_SM_TJ_PRESSURE, 0, size, data);
}

/*
 * thermal_sm_get_thermal_stats_thresholds: copy thermal_stats thresholds from thermal stats section
 *
 * @data: pointer to the thermal_stats thresholds destination
 *
 * Return: 0 on success
 *	error-code: error returned by get_data_from_sm_section()
 */
int thermal_sm_get_thermal_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
	int thresholds_offset = offsetof(struct thermal_sm_stats_data, thresholds);

	return get_data_from_sm_section(THERMAL_SM_STATS, thresholds_offset, sizeof(*data),
					(u8 *)data);
}

/*
 * thermal_sm_set_thermal_stats_thresholds: copy thermal_stats thresholds to thermal stats section
 *
 * @data: pointer to the thermal_stats thresholds source
 *
 * Return: 0 on success
 *	error-code: error returned by get_data_from_sm_section()
 */
int thermal_sm_set_thermal_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
	int thresholds_offset = offsetof(struct thermal_sm_stats_data, thresholds);

	return get_data_from_sm_section(THERMAL_SM_STATS, thresholds_offset, sizeof(*data),
					(u8 *)data);
}

/*
 * thermal_sm_get_tmu_trip_counter: read trip counters of a thermal zone
 *
 * @data: pointer to shared memory trip counter section data structure
 *
 * Return: 0 on success
 *	error-code: error returned by get_data_from_sm_section()
 */
int thermal_sm_get_tmu_trip_counter(struct thermal_sm_trip_counter_data *data)
{
	return get_data_from_sm_section(THERMAL_SM_TRIP_COUNT, 0, sizeof(*data), (u8 *)data);
}