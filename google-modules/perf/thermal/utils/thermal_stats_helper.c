// SPDX-License-Identifier: GPL-2.0-only
/*
 * cpm_thermal_stats_helper.h Helper to register and update thermal stats
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#include <linux/units.h>

#include "thermal_stats_mock.h"

#define NANO_TO_MILLI	(NANO / MILLI)

LIST_HEAD(stats_data_list);
DEFINE_MUTEX(stats_lock);

/*
 * thermal_stats_parse_dt - parse thermal_stats device tree
 *
 * @pdev: platform device pointer
 * @drv_data: thermal_stats driver data
 *
 * Return: 0 on success;
 *	-EINVAL: invalid argument or driver data;
 *	error code returned from of_property_read functions.
 */
int thermal_stats_parse_dt(struct platform_device *pdev, struct thermal_stats_driver_data *drv_data)
{
	int ret = 0;

	if (!pdev || !drv_data)
		return -EINVAL;

	ret = read_dt_property_string(pdev->dev.of_node, "group-name", &drv_data->group_name);

	if (ret)
		dev_err(&pdev->dev, "Failed to read thermal stats group name: ret=%d", ret);

	return ret;
}

/*
 * thermal_stats_init - Initialize CPM thermal stats
 *
 * @pdev: platform device pointer
 * @drv_data: thermal_stats driver data
 *
 * Return: 0 on success;
 *	-EINVAL: invalid argument;
 *	error code from thermal_sm_initialize_section
 *	error code from thermal_stats_register_thermal_metrics
 */
int thermal_stats_init(struct platform_device *pdev, struct thermal_stats_driver_data *drv_data)
{
	u64 time_before, time_after;
	int ret;

	if (!pdev || !drv_data)
		return -EINVAL;

	/* init cpm and kernel time sync */
	/* get a base gtc ticks */
	time_before = (u64)ktime_get_boottime_seconds();
	drv_data->ktime_sync.ticks_base = goog_gtc_get_counter();
	time_after = (u64)ktime_get_boottime_seconds();
	/* calculate a base epoch timestamp in seconds */
	/* base boot time: median time between before and after getting gtc ticks for
	 * least sync error.
	 */
	drv_data->ktime_sync.ktime_real_second_base = (time_before + time_after) >> 1;
	/* base epoch timestamp: base boot time + difference between boot timestamp
	 * and epoch timestamp
	 */
	drv_data->ktime_sync.ktime_real_second_base +=
		ktime_get_real_seconds() - ktime_get_boottime_seconds();

	/* init shared memory */
	ret = initialize_thermal_sm_section(&pdev->dev, THERMAL_SM_STATS);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to initialize shared memory stats section: ret=%d", ret);
		return ret;
	}

	for (enum hw_thermal_zone_id i = 0; i < HW_THERMAL_ZONE_MAX; ++i) {
		ret = thermal_stats_register_thermal_metrics(pdev, drv_data, i);
		if (ret)
			return ret;
	}
	return 0;
}

/*
 * thermal_stats_cleanup - clean up thermal stats
 *
 * @pdev: platform device pointer
 */
void thermal_stats_cleanup(struct platform_device *pdev)
{
	struct device *dev;
	struct thermal_stats_tz_data *pos, *n;
	int ret = 0;

	if (!pdev)
		return;

	dev = &pdev->dev;
	list_for_each_entry_safe(pos, n, &stats_data_list, node) {
		ret = unregister_temp_residency_stats(pos->tr_handle);
		if (ret)
			dev_warn(&pdev->dev,
				"Failed to unregister temperature residency: handle=%d ret=%d",
				 pos->tr_handle, ret);

		list_del(&pos->node);
	}
}

/*
 * thermal_stats_register_thermal_metrics - register a thermal zone to thermal_metrics driver
 *
 * @pdev: platform device pointer
 * @tz_id: thermal zone id to register
 *
 * Return: 0 on success;
 *	-EINVAL: invalid argument and driver data;
 *	-ENOMEM: fail to allocate thermal zone stats data;
 */
int thermal_stats_register_thermal_metrics(struct platform_device *pdev,
					   struct thermal_stats_driver_data *drv_data,
					   enum hw_thermal_zone_id tz_id)
{
	const char *tz_name;
	char group_name[THERMAL_NAME_LENGTH];
	struct thermal_stats_tz_data *tz_stats = NULL;
	tr_handle tr_handle;
	struct temp_residency_stats_callbacks tr_cb_struct = {
		.set_thresholds = thermal_stats_set_tr_thresholds,
		.get_thresholds = thermal_stats_get_tr_thresholds,
		.get_stats = thermal_stats_get_tr_stats,
		.reset_stats = thermal_stats_reset_stats,
	};
	int ret;

	if (!pdev || !drv_data || !drv_data->group_name || tz_id >= HW_THERMAL_ZONE_MAX)
		return -EINVAL;

	tz_name = cpm_mbox_get_tz_name(tz_id);
	if (!tz_name) {
		dev_dbg(&pdev->dev, "Unsupported tz_id: %d", tz_id);
		return 0;
	}

	/* test if thermal stats is supported by the tz */
	ret = get_tr_thresholds(tz_id);
	if (ret) {
		dev_dbg(&pdev->dev, "Thermal stats is not supported: %s", tz_name);
		return 0;
	}

	tz_stats = devm_kzalloc(&pdev->dev, sizeof(*tz_stats), GFP_KERNEL);
	if (!tz_stats)
		return -ENOMEM;

	strscpy(group_name, drv_data->group_name, THERMAL_NAME_LENGTH);
	tr_handle = register_tr_stats(tz_name, group_name);
	if (tr_handle < 0) {
		dev_err(&pdev->dev, "Failed to register tr stats: %s: ret=%d", tz_name, tr_handle);
		return tr_handle;
	}

	ret = register_tr_stats_callbacks(tr_handle, &tr_cb_struct);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register tr stats callbacks: %s: ret=%d",
			tz_name, ret);
		unregister_temp_residency_stats(tr_handle);
		return ret;
	}

	tz_stats->tr_handle = tr_handle;
	tz_stats->tz_id = tz_id;
	tz_stats->ktime_sync = &drv_data->ktime_sync;
	list_add_tail(&tz_stats->node, &stats_data_list);

	return 0;
}

/*
 * thermal_stats_get_data_by_tr_handle
 *
 * This function retrieves thermal zone stats data pointer by thermal metrics handle
 *
 * @tr_handle: thermal metrics handle
 *
 * Return: pointer to thermal zone stats data
 *	-EINVAL: if not found
 */
struct thermal_stats_tz_data *thermal_stats_get_data_by_tr_handle(tr_handle tr_handle)
{
	struct thermal_stats_tz_data *tz_stats;

	list_for_each_entry(tz_stats, &stats_data_list, node) {
		if (tz_stats->tr_handle == tr_handle)
			return tz_stats;
	}

	return ERR_PTR(-EINVAL);
}

/*
 * __thermal_stats_tick_to_real_second - convert gtc ticks to unix time
 *
 * @ktime_sync: difference between kernel boot time and unix time
 * @global_tick: gtc ticks
 *
 * Return: unix time
 *	-EINVAL: invalid argument
 */
time64_t __thermal_stats_ticks_to_real_seconds(struct thermal_stats_time_sync *ktime_sync,
					       u64 global_ticks)
{
	u64 ticks_since_sync_seconds;

	if (!ktime_sync)
		return -EINVAL;

	ticks_since_sync_seconds =
		goog_gtc_ticks_to_ns(global_ticks - ktime_sync->ticks_base) / NANO;

	return (time64_t)(ktime_sync->ktime_real_second_base + ticks_since_sync_seconds);
}

/*
 * thermal_stats_get_tr_stats - Copy temperature metrics from shared memory
 *
 * @instance: thermal metrics handle
 * @stats: pointer to thermal stats output
 * @max: max temperature with timestamp
 * @min: min temperature with timestamp
 *
 * Return: 0 on success;
 *	-EINVAL: invalid arguments or tr_handle not found;
 *	error code returned from thermal_msg and thermal_sm
 */
int thermal_stats_get_tr_stats(tr_handle instance, atomic64_t *stats, struct tr_sample *max,
			       struct tr_sample *min)
{
	int ret = 0;
	struct thermal_sm_stats_metrics metrics_buffer;
	struct thermal_stats_tz_data *tz_stats = thermal_stats_get_data_by_tr_handle(instance);

	if (!stats || !max || !min || IS_ERR_OR_NULL(tz_stats))
		return -EINVAL;

	mutex_lock(&stats_lock);
	ret = get_tr_stats(tz_stats->tz_id);
	if (ret)
		goto exit;
	ret = get_thermal_sm_stats_metrics(&metrics_buffer);
	if (ret)
		goto exit;

	for (int i = 0; i < metrics_buffer.bucket_count; ++i) {
		u64 bucket_stats_ms =
			goog_gtc_ticks_to_ns(metrics_buffer.time_in_state[i]) / NANO_TO_MILLI;
		atomic64_set(&stats[i], bucket_stats_ms);
	}

	max->temp = (int)metrics_buffer.max_sample_temps * MILLI;
	max->timestamp = __thermal_stats_ticks_to_real_seconds(tz_stats->ktime_sync,
							       metrics_buffer.max_sample_timestamp);
	min->temp = (int)metrics_buffer.min_sample_temps * MILLI;
	min->timestamp = __thermal_stats_ticks_to_real_seconds(tz_stats->ktime_sync,
							       metrics_buffer.min_sample_timestamp);
exit:
	mutex_unlock(&stats_lock);
	return ret;
}

/*
 * thermal_stats_set_tr_thresholds - Copy new thermal metrics thresholds to thermal sm
 *
 * @instance: thermal metrics handler
 * @thresholds: pointer to the new thresholds array
 * @num_thresholds: number of thresholds
 *
 * Return: 0 on success
 *	-EVINAL: invalid arguments and tr_handle not found
 *	error code returned from thermal_msg and thermal_sm
 */
int thermal_stats_set_tr_thresholds(tr_handle instance, const int *thresholds, int num_thresholds)
{
	int ret = 0;
	struct thermal_sm_stats_thresholds thresholds_buffer;
	struct thermal_stats_tz_data *tz_stats = thermal_stats_get_data_by_tr_handle(instance);
	u8 tz_id;

	if (!thresholds || IS_ERR_OR_NULL(tz_stats) || num_thresholds > MAX_TR_THRESHOLD_COUNT)
		return -EINVAL;

	tz_id = tz_stats->tz_id;
	thresholds_buffer.threshold_count = num_thresholds;
	for (int i = 0; i < num_thresholds; ++i)
		thresholds_buffer.thresholds[i] = (u8)(thresholds[i] / MILLI);

	mutex_lock(&stats_lock);
	ret = set_thermal_sm_stats_thresholds(&thresholds_buffer);
	if (ret)
		goto exit;
	ret = set_tr_thresholds(tz_id);
	if (ret)
		goto exit;
exit:
	mutex_unlock(&stats_lock);
	return ret;
}

/*
 * thermal_stats_get_tr_thresholds - Copy thermal metrics thresholds from thermal sm
 *
 * @instance: requested thermal metrics handle
 * @thresholds: pointer to thermal metrics thresholds output
 * @num_thresholds: pointer to number of thermal metrics thresholds output
 *
 * Return: 0 on success;
 *	-EINVAL: invalid arguments;
 *	error code returned from thermal_msg and thermal_sm
 */
int thermal_stats_get_tr_thresholds(tr_handle instance, int *thresholds, int *num_thresholds)
{
	int ret = 0;
	struct thermal_sm_stats_thresholds thresholds_buffer;
	struct thermal_stats_tz_data *tz_stats = thermal_stats_get_data_by_tr_handle(instance);

	if (!thresholds || !num_thresholds || IS_ERR_OR_NULL(tz_stats)) {
		pr_err("thermal_stats: failed to get tz_stats for instance:%d, ret=%ld", instance,
			PTR_ERR(tz_stats));
		return -EINVAL;
	}


	mutex_lock(&stats_lock);
	ret = get_tr_thresholds(tz_stats->tz_id);
	if (ret) {
		pr_err("thermal_stats: get_threshold failed: tz=%d, ret=%d", tz_stats->tz_id, ret);
		goto exit;
	}
	ret = get_thermal_sm_stats_thresholds(&thresholds_buffer);
	if (ret) {
		pr_err("thermal_stats: get_shared_mem failed: tz=%d, ret=%d", tz_stats->tz_id, ret);
		goto exit;
	}
	*num_thresholds = thresholds_buffer.threshold_count;
	for (int i = 0; i < thresholds_buffer.threshold_count; ++i)
		thresholds[i] = MILLI * thresholds_buffer.thresholds[i];
exit:
	mutex_unlock(&stats_lock);
	return ret;
}

/*
 * thermal_stats_reset_stats - Reset thermal metrics by tr_handle
 *
 * @instance: thermal metrics handle
 *
 * Return: 0 on success;
 *	-EINVAL: thermal metrics handle not found;
 *	error code returned from thermal_msg
 */
int thermal_stats_reset_stats(tr_handle instance)
{
	struct thermal_stats_tz_data *tz_stats = thermal_stats_get_data_by_tr_handle(instance);

	if (IS_ERR_OR_NULL(tz_stats))
		return -EINVAL;

	return reset_tr_stats(tz_stats->tz_id);
}
