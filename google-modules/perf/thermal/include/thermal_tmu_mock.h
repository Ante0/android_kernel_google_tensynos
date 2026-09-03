/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_tmu_mock.h Helper to declare and use TMU mock functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_TMU_MOCK_H_
#define _THERMAL_TMU_MOCK_H_

#include "thermal_msg_helper.h"
#include "thermal_tmu_helper.h"

#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
int mock_of_property_read_s32(struct device_node *np, char *name, s32 *val);
bool mock_of_property_read_bool(struct device_node *np, char *name);
int mock_thermal_tmu_parse_tzd_dt(struct google_sensor_data *sensor);
int mock_msg_tmu_set_gov_param(u8 tz_id, u8 type, int val);
int mock_msg_tmu_set_trip_temp(u8 tz_id, u8 *temperature, int num_temperature);
int mock_msg_tmu_set_trip_hyst(u8 tz_id, u8 *hysteresis, int num_hysteresis);
int mock_msg_tmu_set_trip_type(u8 tz_id, u8 *type, int num_type);
int mock_msg_tmu_set_polling_delay_ms(u8 tz_id, u16 delay_ms);
int mock_msg_tmu_set_gov_select(u8 tz_id, u8 gov_select);
int mock_msg_tmu_get_temp(u8 tz_id, u8 *temperature);
int mock_msg_tmu_set_temp_lut(u8 tz_id, u8 temperature, u8 cdev_state, bool append);
int mock_msg_tmu_get_temp_lut(u8 tz_id, u8 index, u8 *temperature, u8 *cdev_state);
int mock_thermal_zone_for_each_trip(struct thermal_zone_device *tzd,
				    int (*cb)(struct thermal_trip *, void *),
				    void *data);
int mock_msg_tmu_get_trip_counter_snapshot(u8 tz_id);
int mock_thermal_sm_initialize_section(struct device *dev, enum thermal_sm_section section);
int mock_thermal_sm_get_tmu_trip_counter(struct thermal_sm_trip_counter_data *data);
#else
static inline int mock_of_property_read_s32(struct device_node *np, char *name, s32 *val)
{
	return -EOPNOTSUPP;
}
static inline bool mock_of_property_read_bool(struct device_node *np, char *name)
{
	return false;
}
static inline int mock_thermal_tmu_parse_tzd_dt(struct google_sensor_data *sensor)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_set_gov_param(u8 tz_id, u8 type, int val)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_set_trip_temp(u8 tz_id, u8 *temperature, int num_temperature)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_set_trip_hyst(u8 tz_id, u8 *hysteresis, int num_hysteresis)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_set_trip_type(u8 tz_id, u8 *type, int num_type)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_set_polling_delay_ms(u8 tz_id, u16 delay_ms)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_set_gov_select(u8 tz_id, u8 gov_select)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_get_temp(u8 tz_id, u8 *temperature)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_set_temp_lut(u8 tz_id, u8 temperature, u8 cdev_state, bool append)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_get_temp_lut(u8 tz_id, u8 index, u8 *temperature, u8 *cdev_state)
{
	return -EOPNOTSUPP;
}
static inline int mock_thermal_zone_for_each_trip(struct thermal_zone_device *tzd,
						  int (*cb)(struct thermal_trip *, void *),
						  void *data)
{
	return -EOPNOTSUPP;
}
static inline int mock_msg_tmu_update_trip_counter_sm(u8 tz_id)
{
	return -EOPNOTSUPP;
}
static inline int mock_thermal_sm_initialize_section(struct device *dev,
						     enum thermal_sm_section section)
{
	return -EOPNOTSUPP;
}
static inline int mock_thermal_sm_get_tmu_trip_counter(struct thermal_sm_trip_counter_data *data)
{
	return -EOPNOTSUPP;
}
#endif // CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST

static inline int read_dt_property_s32(struct device_node *np, char *name, s32 *val)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_of_property_read_s32(np, name, val);
#else
	return of_property_read_s32(np, name, val);
#endif
}

static inline bool read_dt_property_bool(struct device_node *np, char *name)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_of_property_read_bool(np, name);
#else
	return of_property_read_bool(np, name);
#endif
}

static inline int thermal_tmu_parse_tzd_dt(struct google_sensor_data *sensor)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_thermal_tmu_parse_tzd_dt(sensor);
#else
	return __thermal_tmu_parse_tzd_dt(sensor);
#endif
}

static inline int tmu_set_gov_param(u8 tz_id, u8 type, s32 val)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_set_gov_param(tz_id, type, val);
#else
	return msg_tmu_set_gov_param(tz_id, type, val);
#endif
}

static inline int tmu_set_trip_temp(u8 tz_id, u8 *temperature, int num_temperature)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_set_trip_temp(tz_id, temperature, num_temperature);
#else
	return msg_tmu_set_trip_temp(tz_id, temperature, num_temperature);
#endif
}

static inline int tmu_set_trip_hyst(u8 tz_id, u8 *hysteresis, int num_hysteresis)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_set_trip_hyst(tz_id, hysteresis, num_hysteresis);
#else
	return msg_tmu_set_trip_hyst(tz_id, hysteresis, num_hysteresis);
#endif
}

static inline int tmu_set_trip_type(u8 tz_id, u8 *type, int num_type)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_set_trip_type(tz_id, type, num_type);
#else
	return msg_tmu_set_trip_type(tz_id, type, num_type);
#endif
}

static inline int tmu_set_polling_delay_ms(u8 tz_id, u16 delay_ms)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_set_polling_delay_ms(tz_id, delay_ms);
#else
	return msg_tmu_set_polling_delay_ms(tz_id, delay_ms);
#endif
}

static inline int tmu_set_gov_select(u8 tz_id, u8 gov_select)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_set_gov_select(tz_id, gov_select);
#else
	return msg_tmu_set_gov_select(tz_id, gov_select);
#endif
}

static inline int tmu_get_temp(u8 tz_id, u8 *temperature)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_get_temp(tz_id, temperature);
#else
	return msg_tmu_get_temp(tz_id, temperature);
#endif
}

static inline int tmu_set_temp_lut(u8 tz_id, u8 temperature, u8 cdev_state, bool append)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_set_temp_lut(tz_id, temperature, cdev_state, append);
#else
	return msg_tmu_set_temp_lut(tz_id, temperature, cdev_state, append);
#endif
}

static inline int tmu_get_temp_lut(u8 tz_id, u8 index, u8 *temperature, u8 *cdev_state)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_get_temp_lut(tz_id, index, temperature, cdev_state);
#else
	return msg_tmu_get_temp_lut(tz_id, index, temperature, cdev_state);
#endif
}

static inline int for_each_thermal_zone_trip(struct thermal_zone_device *tzd,
					     int (*cb)(struct thermal_trip *, void *),
					     void *data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_thermal_zone_for_each_trip(tzd, cb, data);
#else
	return thermal_zone_for_each_trip(tzd, cb, data);
#endif
}

static inline int tmu_get_trip_counter_snapshot(u8 tz_id)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_msg_tmu_get_trip_counter_snapshot(tz_id);
#else
	return msg_tmu_get_trip_counter_snapshot(tz_id);
#endif
}

static inline int sm_initialize_section(struct device *dev, enum thermal_sm_section section)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_thermal_sm_initialize_section(dev, section);
#else
	return thermal_sm_initialize_section(dev, section);
#endif
}

static inline int sm_get_tmu_trip_counter(struct thermal_sm_trip_counter_data *data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_HELPER_KUNIT_TEST)
	return mock_thermal_sm_get_tmu_trip_counter(data);
#else
	return thermal_sm_get_tmu_trip_counter(data);
#endif
}

#endif //_THERMAL_TMU_MOCK_H_
