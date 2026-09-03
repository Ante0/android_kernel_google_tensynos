/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_ntc_mock.h Helper to declare and use NTC mock functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_NTC_MOCK_H_
#define _THERMAL_NTC_MOCK_H_

#include "thermal_cpm_mbox.h"
#include "thermal_ntc_helper.h"

#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
int mock_thermal_get_temp(int *reg_data);
int mock_thermal_get_avg_temp(int *reg_data);
int mock_thermal_ch_enable(void);
int mock_thermal_clr_data(void);
int mock_ntc_clear_and_mask_irq(int ch_id, bool enable, bool fault_trip);
int mock_ntc_set_trips(int ch_id, int trip_val, int hyst_val);
int mock_ntc_set_fault_trip(int ch_id, int trip_val);
int mock_ntc_read_irq_status(int *reg_data);
int mock_register_temp_residency_stats(const char *name, char *group_name);
int mock_unregister_temp_residency_stats(tr_handle tr_stats_handle);
int mock_temp_residency_stats_set_thresholds(tr_handle tr_stats_handle, const int *thresholds,
					     int num_thresholds);
int mock_temp_residency_stats_update(tr_handle tr_stats_handle, int temp);
int mock_cpm_mbox_register_notification(enum hw_dev_type type, struct notifier_block *nb);
bool mock_ntc_of_property_read_bool(const struct device_node *np, const char *propname);
#else
static inline int mock_thermal_get_temp(int *reg_data)
{
	return -EOPNOTSUPP;
}

static inline int mock_thermal_get_avg_temp(int *reg_data)
{
	return -EOPNOTSUPP;
}

static inline int mock_thermal_ch_enable(void)
{
	return -EOPNOTSUPP;
}

static inline int mock_thermal_clr_data(void)
{
	return -EOPNOTSUPP;
}

static inline int mock_ntc_clear_and_mask_irq(int ch_id, bool enable, bool fault_trip)
{
	return -EOPNOTSUPP;
}

static inline int mock_ntc_set_trips(int ch_id, int trip_val, int hyst_val)
{
	return -EOPNOTSUPP;
}

static inline int mock_ntc_set_fault_trip(int ch_id, int trip_val)
{
	return -EOPNOTSUPP;
}

static inline int mock_ntc_read_irq_status(int *reg_data)
{
	return -EOPNOTSUPP;
}

static inline int mock_register_temp_residency_stats(const char *name, char *group_name)
{
	return -EOPNOTSUPP;
}

static inline int mock_unregister_temp_residency_stats(tr_handle instance)
{
	return -EOPNOTSUPP;
}

static inline int mock_temp_residency_stats_set_thresholds(tr_handle instance,
							   const int *thresholds,
							   int num_thresholds)
{
	return -EOPNOTSUPP;
}

static inline int mock_temp_residency_stats_update(tr_handle instance, int temp)
{
	return -EOPNOTSUPP;
}

static inline int mock_cpm_mbox_register_notification(enum hw_dev_type type,
						      struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline bool mock_ntc_of_property_read_bool(const struct device_node *np,
						  const char *propname)
{
	return false;
}
#endif // CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST

static inline int read_avg_ntc_temp(struct google_sensor_data *sens, int *reg_data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
		return mock_thermal_get_avg_temp(reg_data);
#else
		return msg_ntc_channel_read_avg_temp(sens->sensor_id, reg_data);
#endif
}

static inline int read_ntc_temp(struct google_sensor_data *sens, int *reg_data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
		return mock_thermal_get_temp(reg_data);
#else
		return msg_ntc_channel_read_temp(sens->sensor_id, reg_data);
#endif
}

static inline int register_ntc_temp_residency_stats(const char *name, char *group_name)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_register_temp_residency_stats(name, group_name);
#else
	return register_temp_residency_stats(name, group_name);
#endif
}

static inline int unregister_ntc_temp_residency_stats(tr_handle instance)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_unregister_temp_residency_stats(instance);
#else
	return unregister_temp_residency_stats(instance);
#endif
}

static inline int ntc_temp_residency_stats_set_thresholds(tr_handle instance, const int *thresholds,
							  int num_thresholds)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_temp_residency_stats_set_thresholds(instance, thresholds, num_thresholds);
#else
	return temp_residency_stats_set_thresholds(instance, thresholds, num_thresholds);
#endif
}

static inline int ntc_temp_residency_stats_update(tr_handle instance, int temp)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_temp_residency_stats_update(instance, temp);
#else
	return temp_residency_stats_update(instance, temp);
#endif
}

static inline int ntc_clear_and_mask_warn_irq(int sens_id, bool enable)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_ntc_clear_and_mask_irq(sens_id, enable, false);
#else
	return msg_ntc_channel_clear_and_mask_irq(sens_id, enable);
#endif
}

static inline int ntc_clear_and_mask_fault_irq(int sens_id, bool enable)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_ntc_clear_and_mask_irq(sens_id, enable, true);
#else
	return msg_ntc_channel_mask_fault_irq(sens_id, enable);
#endif
}

static inline int ntc_channel_enable(void)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_thermal_ch_enable();
#else
	return msg_ntc_channel_enable();
#endif
}

static inline int ntc_clr_data(void)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_thermal_clr_data();
#else
	return msg_ntc_channel_clear_data_reg();
#endif
}

static inline void ntc_schedule_work(struct delayed_work *ntc_work)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	schedule_delayed_work(ntc_work, 0);
#else
	schedule_delayed_work(ntc_work, msecs_to_jiffies(NTC_INIT_DELAY_MS));
#endif
}

static inline int ntc_set_trips(int sens_id, int trip_reg_val, int trip_hyst_reg_val)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_ntc_set_trips(sens_id, trip_reg_val, trip_hyst_reg_val);
#else
	return msg_ntc_channel_set_trips(sens_id, trip_reg_val, trip_hyst_reg_val);
#endif
}

static inline int ntc_set_fault_trip(int sens_id, int trip_reg_val)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_ntc_set_fault_trip(sens_id, trip_reg_val);
#else
	return msg_ntc_channel_set_fault_trip(sens_id, trip_reg_val);
#endif
}

static inline int ntc_read_irq_status(int *reg_data, int len)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_ntc_read_irq_status(reg_data);
#else
	return msg_ntc_channel_read_irq_status(reg_data, len);
#endif
}

static inline int ntc_cpm_mbox_register_notification(enum hw_dev_type type,
						     struct notifier_block *nb)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_cpm_mbox_register_notification(type, nb);
#else
	return thermal_cpm_mbox_register_notification(type, nb);
#endif
}

static inline int ntc_of_property_read_bool(const struct device_node *np, const char *propname)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_ntc_of_property_read_bool(np, propname);
#else
	return of_property_read_bool(np, propname);
#endif
}
#endif //_THERMAL_NTC_MOCK_H_
