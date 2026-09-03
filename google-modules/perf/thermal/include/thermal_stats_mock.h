/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_stats_mock.h Mock functions to thermal stats helper.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_STATS_MOCK_H_
#define _THERMAL_STATS_MOCK_H_

#include <linux/device.h>

#include "thermal_msg_helper.h"
#include "thermal_sm_helper.h"
#include "thermal_stats_helper.h"

#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
int mock_read_dt_property_string(const struct device_node *np, const char *propname,
			      const char **out_string);
int mock_initialize_thermal_sm_section(struct device *dev, enum thermal_sm_section section);
tr_handle mock_register_tr_stats(const char *name, char *group_name);
int mock_register_tr_stats_callbacks(tr_handle instance,
				     struct temp_residency_stats_callbacks *ops);
int mock_get_tr_stats(u8 tz_id);
int mock_set_tr_thresholds(u8 tz_id);
int mock_get_tr_thresholds(u8 tz_id);
int mock_reset_tr_stats(u8 tz_id);
int mock_get_thermal_sm_stats_metrics(struct thermal_sm_stats_metrics *data);
int mock_set_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data);
int mock_get_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data);
const char *mock_cpm_mbox_get_tz_name(enum hw_thermal_zone_id hw_tzid);
#else
static inline int mock_read_dt_property_string(const struct device_node *np, const char *propname,
				 const char **out_string)
{
	return -EOPNOTSUPP;
}
static inline int mock_initialize_thermal_sm_section(struct device *dev,
						     enum thermal_sm_section section)
{
	return -EOPNOTSUPP;
}
static inline tr_handle mock_register_tr_stats(const char *name, char *group_name)
{
	return -EOPNOTSUPP;
}
static inline int mock_register_tr_stats_callbacks(tr_handle instance,
				     struct temp_residency_stats_callbacks *ops)
{
	return -EOPNOTSUPP;
}
static inline int mock_get_tr_stats(u8 tz_id)
{
	return -EOPNOTSUPP;
}
static inline int mock_set_tr_thresholds(u8 tz_id)
{
	return -EOPNOTSUPP;
}
static inline int mock_get_tr_thresholds(u8 tz_id)
{
	return -EOPNOTSUPP;
}
static inline int mock_reset_tr_stats(u8 tz_id)
{
	return -EOPNOTSUPP;
}
static inline int mock_get_thermal_sm_stats_metrics(struct thermal_sm_stats_metrics *data)
{
	return -EOPNOTSUPP;
}
static inline int mock_set_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
	return -EOPNOTSUPP;
}
static inline int mock_get_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
	return -EOPNOTSUPP;
}
static inline const char *mock_cpm_mbox_get_tz_name(enum hw_thermal_zone_id hw_tzid)
{
	return NULL;
}
#endif // GOOGLE_THERMAL_STATS_KUNIT_TEST

static inline int read_dt_property_string(const struct device_node *np, const char *propname,
					  const char **out_string)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_read_dt_property_string(np, propname, out_string);
#else
	return of_property_read_string(np, propname, out_string);
#endif
}

static inline int initialize_thermal_sm_section(struct device *dev, enum thermal_sm_section section)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_initialize_thermal_sm_section(dev, section);
#else
	return thermal_sm_initialize_section(dev, section);
#endif
}

static inline tr_handle register_tr_stats(const char *name, char *group_name)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_register_tr_stats(name, group_name);
#else
	return register_temp_residency_stats(name, group_name);
#endif
}

static inline int register_tr_stats_callbacks(tr_handle instance,
					      struct temp_residency_stats_callbacks *ops)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_register_tr_stats_callbacks(instance, ops);
#else
	return register_temp_residency_stats_callbacks(instance, ops);
#endif
}

static inline int get_tr_stats(u8 tz_id)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_get_tr_stats(tz_id);
#else
	return msg_stats_get_tr_stats(tz_id);
#endif
}

static inline int set_tr_thresholds(u8 tz_id)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_set_tr_thresholds(tz_id);
#else
	return msg_stats_set_tr_thresholds(tz_id);
#endif
}

static inline int get_tr_thresholds(u8 tz_id)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_get_tr_thresholds(tz_id);
#else
	return msg_stats_get_tr_thresholds(tz_id);
#endif
}

static inline int reset_tr_stats(u8 tz_id)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_reset_tr_stats(tz_id);
#else
	return msg_stats_reset_tr_stats(tz_id);
#endif
}

static inline int get_thermal_sm_stats_metrics(struct thermal_sm_stats_metrics *data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_get_thermal_sm_stats_metrics(data);
#else
	return thermal_sm_get_thermal_stats_metrics(data);
#endif
}

static inline int set_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_set_thermal_sm_stats_thresholds(data);
#else
	return thermal_sm_set_thermal_stats_thresholds(data);
#endif
}

static inline int get_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_get_thermal_sm_stats_thresholds(data);
#else
	return thermal_sm_get_thermal_stats_thresholds(data);
#endif
}

static inline const char *cpm_mbox_get_tz_name(enum hw_thermal_zone_id hw_tzid)
{
#if IS_ENABLED(CONFIG_GOOGLE_THERMAL_STATS_KUNIT_TEST)
	return mock_cpm_mbox_get_tz_name(hw_tzid);
#else
	return thermal_cpm_mbox_get_tz_name(hw_tzid);
#endif
}

#endif //_THERMAL_STATS_MOCK_H_
