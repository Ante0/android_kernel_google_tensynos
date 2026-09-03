// SPDX-License-Identifier: GPL-2.0-only
/*
 * google_bcl_data_logging.c Google bcl Data Logging driver
 *
 * Copyright (c) 2023, Google LLC. All rights reserved.
 *
 */

#include <linux/ktime.h>
#include "bcl.h"
#include "core_pmic/core_pmic_defs.h"
#include "ifpmic/max77759/max77759_irq.h"
#include "ifpmic/max77779/max77779_irq.h"
#include "soc/soc_defs.h"

#include <trace/events/bcl_exynos.h>

static void log_ifpmic_power(struct bcl_device *bcl_dev)
{
	int idx, ret;
	int i = 0;

	if (!IS_ENABLED(CONFIG_REGULATOR_S2MPG14))
		return;
	if (bcl_dev->ifpmic != MAX77779)
		return;
	ret = bcl_dev->ifpmic_ops->vimon_read(bcl_dev->ifpmic_irq_dev);
	if (ret <= 0)
		return;
	for (idx = 0; idx < ret / VIMON_BYTES_PER_ENTRY; idx = idx + 2) {
		bcl_dev->br_stats->vimon_intf.v_data[i] = bcl_dev->vimon_intf.data[idx];
		bcl_dev->br_stats->vimon_intf.i_data[i] = bcl_dev->vimon_intf.data[idx + 1];
		i++;
	}
	bcl_dev->br_stats->vimon_intf.count = i;
}


static bool cool_down_odpm_lpf_task(struct timespec64 ts_prev)
{
	struct timespec64 ts;
	struct timespec64 ts_delta;

	ktime_get_real_ts64(&ts);

	ts_delta = timespec64_sub(ts, ts_prev);

	if (ts_delta.tv_sec == 0 &&
		ts_delta.tv_nsec < DATA_LOGGING_COOL_DOWN_TIME_MS * NSEC_PER_MSEC)
		return true;

	return false;
}

static void data_logging_main_odpm_lpf_task(struct bcl_device *bcl_dev)
{
	if (cool_down_odpm_lpf_task(bcl_dev->br_stats->main_odpm_lpf.time))
		return;

	core_pmic_main_meter_read_lpf_data(bcl_dev, bcl_dev->br_stats);
	compute_odpm_lpf(bcl_dev,
				   bcl_dev->br_stats->main_odpm_lpf.time,
				   bcl_dev->main_mitigation_conf,
				   &bcl_dev->br_stats->main_odpm_lpf,
				   bcl_dev->max_odpm_stats->main_max_odpm_lpf);
}

static void data_logging_sub_odpm_lpf_task(struct bcl_device *bcl_dev)
{
	/* google_odpm integrated main and sub instant data in one reading
	 * on anacapa.
	 */
	if (!IS_ENABLED(CONFIG_REGULATOR_S2MPG14))
		return;

	if (cool_down_odpm_lpf_task(bcl_dev->br_stats->sub_odpm_lpf.time))
		return;

	core_pmic_sub_meter_read_lpf_data(bcl_dev, bcl_dev->br_stats);
	compute_odpm_lpf(bcl_dev,
				   bcl_dev->br_stats->sub_odpm_lpf.time,
				   bcl_dev->sub_mitigation_conf,
				   &bcl_dev->br_stats->sub_odpm_lpf,
				   bcl_dev->max_odpm_stats->sub_max_odpm_lpf);
}

static void google_bcl_write_irq_triggered_event(struct bcl_device *bcl_dev, int idx)
{
	ktime_get_real_ts64((struct timespec64 *)&bcl_dev->br_stats->triggered_time);
	bcl_dev->br_stats->triggered_idx = idx;
}

static void google_bcl_init_brownout_stats(struct bcl_device *bcl_dev)
{
	memset((void *)bcl_dev->br_stats, 0, bcl_dev->br_stats_size);
	bcl_dev->br_stats->triggered_idx = TRIGGERED_SOURCE_MAX;
}

void google_bcl_upstream_state(struct bcl_zone *zone, enum MITIGATION_MODE state)
{
	struct bcl_device *bcl_dev = zone->parent;
	int idx = zone->idx;
	static const char * const envp[] = {"TRIG=OILO1", NULL};

	if (!bcl_dev->enabled_br_stats)
		return;

	atomic_inc(&zone->last_triggered.triggered_cnt[state]);
	zone->last_triggered.triggered_time[state] = ktime_to_ms(ktime_get());
	zone->current_state = state;
	if (idx == UVLO1)
		sysfs_notify(&bcl_dev->mitigation_dev->kobj, "triggered_state", "uvlo1_triggered");
	else if (idx == UVLO2)
		sysfs_notify(&bcl_dev->mitigation_dev->kobj, "triggered_state", "uvlo2_triggered");
	else if (idx == BATOILO1) {
		sysfs_notify(&bcl_dev->mitigation_dev->kobj, "triggered_state", "oilo1_triggered");
		if (state == LIGHT)
			log_ifpmic_power(bcl_dev);
		else if (state == HEAVY)
			kobject_uevent_env(&bcl_dev->device->kobj, KOBJ_CHANGE, (char **)envp);
	} else if (idx == BATOILO2)
		sysfs_notify(&bcl_dev->mitigation_dev->kobj, "triggered_state", "oilo2_triggered");
	else if (idx == PRE_UVLO)
		sysfs_notify(&bcl_dev->mitigation_dev->kobj, "triggered_state", "smpl_triggered");
}

static void trace_batt(struct bcl_device *bcl_dev)
{
	int ret;
	int capacity, cycle_count, voltage_now, current_now;
	union power_supply_propval val;

	if (!bcl_dev->batt_psy)
		return;
	if (!trace_clock_set_rate_enabled())
		return;

	ret = power_supply_get_property(bcl_dev->batt_psy,
					POWER_SUPPLY_PROP_CAPACITY, &val);
	capacity = ret ? 0 : val.intval;
	ret = power_supply_get_property(bcl_dev->batt_psy,
					POWER_SUPPLY_PROP_CYCLE_COUNT, &val);
	cycle_count = ret ? 0 : val.intval;
	ret = power_supply_get_property(bcl_dev->batt_psy,
					POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	voltage_now = ret ? 0 : val.intval;
	ret = power_supply_get_property(bcl_dev->batt_psy,
					POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	current_now = ret ? 0 : val.intval;

	trace_clock_set_rate("BCL_BATT_CAPACITY", capacity, raw_smp_processor_id());
	trace_clock_set_rate("BCL_BATT_CYCLE_COUNT", cycle_count, raw_smp_processor_id());
	trace_clock_set_rate("BCL_BATT_VOLTAGE_NOW", voltage_now, raw_smp_processor_id());
	trace_clock_set_rate("BCL_BATT_CURRENT_NOW", current_now, raw_smp_processor_id());
}

void google_bcl_start_data_logging(struct bcl_device *bcl_dev, int idx)
{
	if (!bcl_dev->enabled_br_stats)
		return;

	if (!bcl_dev->data_logging_initialized)
		return;

	google_bcl_init_brownout_stats(bcl_dev);

	google_bcl_write_irq_triggered_event(bcl_dev, idx);
	bcl_dev->br_stats->triggered_state =
		bcl_dev->zone[bcl_dev->br_stats->triggered_idx]->current_state;
	data_logging_main_odpm_lpf_task(bcl_dev);
	data_logging_sub_odpm_lpf_task(bcl_dev);

	bcl_dev->triggered_idx = idx;
	sysfs_notify(&bcl_dev->mitigation_dev->kobj, "br_stats", "triggered_idx");

	trace_batt(bcl_dev);
}

void google_bcl_remove_data_logging(struct bcl_device *bcl_dev)
{
	if (bcl_dev->data_logging_initialized) {
		kfree(bcl_dev->br_stats);
		kfree(bcl_dev->max_odpm_stats);
	}
	bcl_dev->data_logging_initialized = false;
}

int google_bcl_init_data_logging(struct bcl_device *bcl_dev)
{
	bcl_dev->triggered_idx = TRIGGERED_SOURCE_MAX;
	bcl_dev->br_stats_size = sizeof(struct brownout_stats);
	bcl_dev->br_stats = kmalloc(bcl_dev->br_stats_size, GFP_KERNEL);
	if (!bcl_dev->br_stats)
		return -ENOMEM;
	bcl_dev->max_odpm_stats = kzalloc(sizeof(struct max_odpm_stats), GFP_KERNEL);
	if (!bcl_dev->max_odpm_stats) {
		kfree(bcl_dev->br_stats);
		return -ENOMEM;
	}

	google_bcl_init_brownout_stats(bcl_dev);
	bcl_dev->data_logging_initialized = true;

	return 0;
}
