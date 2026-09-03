// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_stats_helper_tests.c Test suite to test all the thermal stats helper functions.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/thermal.h>
#include <linux/units.h>

#include "thermal_stats_mock.h"

struct stats_test_data {
	struct platform_device *fake_pdev;
	int mock_read_dt_prop_ret;
	int mock_msg_ret;
	int mock_sm_ret;
	int mock_register_tr_ret;
	int mock_unregister_tr_ret;
	int mock_register_tr_cb_ret;
	char mock_group_name[THERMAL_NAME_LENGTH];
	struct thermal_sm_stats_data mock_sm_data;
	const char *mock_tz_name;
};

static struct platform_device *fake_pdev;
static struct thermal_stats_driver_data mock_drv_data;

int mock_read_dt_property_string(const struct device_node *np, const char *propname,
			      const char **out_string)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	*out_string = stats_data->mock_group_name;
	return stats_data->mock_read_dt_prop_ret;
}

int mock_initialize_thermal_sm_section(struct device *dev, enum thermal_sm_section section)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_sm_ret;
}

tr_handle mock_register_tr_stats(const char *name, char *group_name)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_register_tr_ret;
}

int mock_register_tr_stats_callbacks(tr_handle instance,
				     struct temp_residency_stats_callbacks *ops)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_register_tr_cb_ret;
}

int mock_get_tr_stats(u8 tz_id)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_msg_ret;
}

int mock_set_tr_thresholds(u8 tz_id)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_msg_ret;
}

int mock_get_tr_thresholds(u8 tz_id)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_msg_ret;
}

int mock_reset_tr_stats(u8 tz_id)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_msg_ret;
}

int mock_get_thermal_sm_stats_metrics(struct thermal_sm_stats_metrics *data)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	memcpy(data, &stats_data->mock_sm_data.metrics, sizeof(*data));
	return stats_data->mock_sm_ret;
}

int mock_set_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	memcpy(&stats_data->mock_sm_data.thresholds, data, sizeof(*data));
	return stats_data->mock_sm_ret;
}

int mock_get_thermal_sm_stats_thresholds(struct thermal_sm_stats_thresholds *data)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	memcpy(data, &stats_data->mock_sm_data.thresholds, sizeof(*data));
	return stats_data->mock_sm_ret;
}

const char *mock_cpm_mbox_get_tz_name(enum hw_thermal_zone_id hw_tzid)
{
	struct kunit *test = kunit_get_current_test();
	struct stats_test_data *stats_data = test->priv;

	return stats_data->mock_tz_name;
}

static void thermal_stats_parse_dt_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;

	// success
	KUNIT_EXPECT_EQ(test, thermal_stats_parse_dt(stats_data->fake_pdev, &mock_drv_data), 0);

	// invalid argument
	KUNIT_EXPECT_EQ(test, thermal_stats_parse_dt(NULL, &mock_drv_data), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_stats_parse_dt(stats_data->fake_pdev, NULL), -EINVAL);
	// read dt failure
	stats_data->mock_read_dt_prop_ret = -ENODATA;
	KUNIT_EXPECT_EQ(test, thermal_stats_parse_dt(stats_data->fake_pdev, &mock_drv_data),
			-ENODATA);
}

static void thermal_stats_register_thermal_metrics_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;
	enum hw_thermal_zone_id mock_tz_id = HW_THERMAL_ZONE_0;

	// success
	KUNIT_EXPECT_EQ(test,
			thermal_stats_register_thermal_metrics(stats_data->fake_pdev,
								&mock_drv_data, mock_tz_id),
			0);

	// invalid argument
	KUNIT_EXPECT_EQ(test,
			thermal_stats_register_thermal_metrics(NULL, &mock_drv_data, mock_tz_id),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_stats_register_thermal_metrics(stats_data->fake_pdev,
								NULL, mock_tz_id),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_stats_register_thermal_metrics(stats_data->fake_pdev,
							       &mock_drv_data, HW_THERMAL_ZONE_MAX),
			-EINVAL);

	// register tr error
	stats_data->mock_register_tr_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_register_thermal_metrics(stats_data->fake_pdev,
								&mock_drv_data, mock_tz_id),
			-EINVAL);
	stats_data->mock_register_tr_ret = 0;

	// register tr cb error
	stats_data->mock_register_tr_cb_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_register_thermal_metrics(stats_data->fake_pdev,
								&mock_drv_data, mock_tz_id),
			-EINVAL);
	stats_data->mock_register_tr_cb_ret = 0;
}

static void thermal_stats_init_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;

	// success
	KUNIT_EXPECT_EQ(test, thermal_stats_init(stats_data->fake_pdev, &mock_drv_data), 0);

	/* Calculation of gtc ticks and epoch timestamp base calculations is simple and
	 * the risk is low. The real time timestamps also make it hard to validate. Therefore,
	 * it is ignored here. More unit test will be needed if any field bugs are reported.
	 */

	// invalid argument
	KUNIT_EXPECT_EQ(test, thermal_stats_init(NULL, &mock_drv_data), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_stats_init(stats_data->fake_pdev, NULL), -EINVAL);

	// init sm failure
	stats_data->mock_sm_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_stats_init(stats_data->fake_pdev, &mock_drv_data), -EINVAL);

	// invalid tz_id
	stats_data->mock_sm_ret = -EINVAL;
	stats_data->mock_tz_name = NULL;
	KUNIT_EXPECT_EQ(test, thermal_stats_init(stats_data->fake_pdev, &mock_drv_data), -EINVAL);
}

static void thermal_stats_get_data_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;
	tr_handle mock_tr_handle = 0;

	thermal_stats_init(stats_data->fake_pdev, &mock_drv_data);

	// success
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, thermal_stats_get_data_by_tr_handle(mock_tr_handle));

	// handle not found
	mock_tr_handle = HW_THERMAL_ZONE_MAX;
	KUNIT_EXPECT_PTR_EQ(test, thermal_stats_get_data_by_tr_handle(mock_tr_handle),
			    ERR_PTR(-EINVAL));
}

static void thermal_stats_set_tr_thresholds_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;
	int mock_thresholds_count, mock_thresholds[MAX_TR_THRESHOLD_COUNT];
	tr_handle mock_tr_handle = 0;

	mock_thresholds_count = MAX_TR_THRESHOLD_COUNT;
	for (int i = 0; i < MAX_TR_THRESHOLD_COUNT; ++i)
		mock_thresholds[i] = get_random_u32();

	thermal_stats_init(stats_data->fake_pdev, &mock_drv_data);

	// success
	KUNIT_EXPECT_EQ(test,
			thermal_stats_set_tr_thresholds(mock_tr_handle, mock_thresholds,
							mock_thresholds_count),
			0);
	KUNIT_EXPECT_EQ(test, mock_thresholds_count,
			stats_data->mock_sm_data.thresholds.threshold_count);
	for (int i = 0; i < MAX_TR_THRESHOLD_COUNT; ++i)
		KUNIT_EXPECT_EQ(test, (u8)(mock_thresholds[i] / MILLI),
				stats_data->mock_sm_data.thresholds.thresholds[i]);

	// invalid arguments
	KUNIT_EXPECT_EQ(test,
			thermal_stats_set_tr_thresholds(mock_tr_handle + 1, mock_thresholds,
							mock_thresholds_count),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_stats_set_tr_thresholds(mock_tr_handle, NULL,
							mock_thresholds_count),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_stats_set_tr_thresholds(mock_tr_handle, mock_thresholds,
							MAX_TR_THRESHOLD_COUNT + 1),
			-EINVAL);

	// msg error
	stats_data->mock_msg_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_set_tr_thresholds(mock_tr_handle, mock_thresholds,
							mock_thresholds_count),
			-ENODEV);
	stats_data->mock_msg_ret = 0;

	// sm error
	stats_data->mock_sm_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_set_tr_thresholds(mock_tr_handle, mock_thresholds,
							mock_thresholds_count),
			-ENODEV);
	stats_data->mock_sm_ret = 0;
}

static void thermal_stats_get_tr_thresholds_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;
	int mock_thresholds_count, mock_thresholds[MAX_TR_THRESHOLD_COUNT];
	tr_handle mock_tr_handle = 0;

	stats_data->mock_sm_data.thresholds.threshold_count = MAX_TR_THRESHOLD_COUNT;
	for (int i = 0; i < MAX_TR_THRESHOLD_COUNT; ++i)
		stats_data->mock_sm_data.thresholds.thresholds[i] = get_random_u8();

	thermal_stats_init(stats_data->fake_pdev, &mock_drv_data);

	// success
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_thresholds(mock_tr_handle, mock_thresholds,
							&mock_thresholds_count),
			0);
	KUNIT_EXPECT_EQ(test, mock_thresholds_count,
			stats_data->mock_sm_data.thresholds.threshold_count);
	for (int i = 0; i < MAX_TR_THRESHOLD_COUNT; ++i)
		KUNIT_EXPECT_EQ(test, (u8)(mock_thresholds[i] / MILLI),
				stats_data->mock_sm_data.thresholds.thresholds[i]);

	// invalid arguments
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_thresholds(mock_tr_handle, NULL,
							&mock_thresholds_count),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_thresholds(mock_tr_handle, mock_thresholds, NULL),
			-EINVAL);

	// msg error
	stats_data->mock_msg_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_thresholds(mock_tr_handle, mock_thresholds,
							&mock_thresholds_count),
			-ENODEV);
	stats_data->mock_msg_ret = 0;

	// sm error
	stats_data->mock_sm_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_thresholds(mock_tr_handle, mock_thresholds,
							&mock_thresholds_count),
			-ENODEV);
	stats_data->mock_sm_ret = 0;
}

static void thermal_stats_ticks_to_time_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;
	struct thermal_stats_time_sync mock_ktime_sync = {
		.ktime_real_second_base = get_random_u32(),
		.ticks_base = get_random_u32(),
	};
	u64 verify_ticks, mock_ticks = get_random_u32();

	thermal_stats_init(stats_data->fake_pdev, &mock_drv_data);
	// success
	verify_ticks = mock_ktime_sync.ktime_real_second_base +
		       goog_gtc_ticks_to_ns(mock_ticks - mock_ktime_sync.ticks_base) / NANO;
	KUNIT_EXPECT_EQ(test,
			__thermal_stats_ticks_to_real_seconds(&mock_ktime_sync, mock_ticks),
			verify_ticks);

	// invalid argument
	KUNIT_EXPECT_EQ(test, __thermal_stats_ticks_to_real_seconds(NULL, mock_ticks), -EINVAL);
}

static void thermal_stats_get_tr_stats_test(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;
	struct tr_sample mock_max, mock_min;
	time64_t verify_max_timestamp, verify_min_timestamp;
	atomic64_t mock_stats[MAX_TR_THRESHOLD_COUNT + 1];
	tr_handle mock_tr_handle = 0;

	stats_data->mock_sm_data.metrics.max_sample_timestamp = get_random_u64();
	stats_data->mock_sm_data.metrics.min_sample_timestamp = get_random_u64();
	stats_data->mock_sm_data.metrics.max_sample_temps = get_random_u8();
	stats_data->mock_sm_data.metrics.min_sample_temps = get_random_u8();
	stats_data->mock_sm_data.metrics.bucket_count = MAX_TR_THRESHOLD_COUNT + 1;
	for (int i = 0; i < MAX_TR_THRESHOLD_COUNT + 1; ++i)
		stats_data->mock_sm_data.metrics.time_in_state[i] = get_random_u32();

	thermal_stats_init(stats_data->fake_pdev, &mock_drv_data);

	// success
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_stats(mock_tr_handle, mock_stats, &mock_max,
			&mock_min),
			0);
	KUNIT_EXPECT_EQ(test, mock_max.temp,
			(int)stats_data->mock_sm_data.metrics.max_sample_temps * MILLI);
	KUNIT_EXPECT_EQ(test, mock_min.temp,
			(int)stats_data->mock_sm_data.metrics.min_sample_temps * MILLI);
	verify_max_timestamp = __thermal_stats_ticks_to_real_seconds(
		&mock_drv_data.ktime_sync,
		stats_data->mock_sm_data.metrics.max_sample_timestamp);
	KUNIT_EXPECT_EQ(test, mock_max.timestamp, verify_max_timestamp);
	verify_min_timestamp = __thermal_stats_ticks_to_real_seconds(
		&mock_drv_data.ktime_sync,
		stats_data->mock_sm_data.metrics.min_sample_timestamp);
	KUNIT_EXPECT_EQ(test, mock_min.timestamp, verify_min_timestamp);
	for (int i = 0; i < MAX_TR_THRESHOLD_COUNT + 1; ++i) {
		u64 mock_stats_read = atomic64_read(&mock_stats[i]),
		    verify_stats = stats_data->mock_sm_data.metrics.time_in_state[i];
		KUNIT_EXPECT_EQ(test, mock_stats_read,
				goog_gtc_ticks_to_ns(verify_stats) / (NANO / MILLI));
	}

	// invalid argument
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_stats(mock_tr_handle, NULL, &mock_max, &mock_min),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_stats(mock_tr_handle, mock_stats, NULL, &mock_min),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_stats(mock_tr_handle, mock_stats, &mock_max, NULL),
			-EINVAL);

	// msg error
	stats_data->mock_msg_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_stats(mock_tr_handle, mock_stats, &mock_max,
			&mock_min),
			-ENODEV);
	stats_data->mock_msg_ret = 0;

	// sm error
	stats_data->mock_sm_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_stats_get_tr_stats(mock_tr_handle, mock_stats, &mock_max,
			&mock_min),
			-ENODEV);
	stats_data->mock_sm_ret = 0;
}

static struct kunit_case thermal_stats_helper_test[] = {
	KUNIT_CASE(thermal_stats_parse_dt_test),
	KUNIT_CASE(thermal_stats_register_thermal_metrics_test),
	KUNIT_CASE(thermal_stats_init_test),
	KUNIT_CASE(thermal_stats_get_data_test),
	KUNIT_CASE(thermal_stats_ticks_to_time_test),
	KUNIT_CASE(thermal_stats_set_tr_thresholds_test),
	KUNIT_CASE(thermal_stats_get_tr_thresholds_test),
	KUNIT_CASE(thermal_stats_get_tr_stats_test),
	{},
};

static int thermal_stats_suite_init(struct kunit_suite *suite)
{
	fake_pdev = platform_device_alloc("mock_thermal-pdevice", -1);
	platform_set_drvdata(fake_pdev, &mock_drv_data);

	return 0;
}

static void thermal_stats_suite_exit(struct kunit_suite *suite)
{
	platform_device_put(fake_pdev);
}

static int thermal_stats_test_init(struct kunit *test)
{
	struct stats_test_data *stats_data;

	stats_data = kunit_kzalloc(test, sizeof(*stats_data), GFP_KERNEL);
	stats_data->fake_pdev = fake_pdev;
	stats_data->mock_tz_name = "mock_tz_name";
	strscpy(stats_data->mock_group_name, "mock_tmu", THERMAL_NAME_LENGTH);
	test->priv = stats_data;
	mock_drv_data.group_name = stats_data->mock_group_name;
	return 0;
}

static void thermal_stats_test_exit(struct kunit *test)
{
	struct stats_test_data *stats_data = test->priv;

	thermal_stats_cleanup(stats_data->fake_pdev);
}

static struct kunit_suite thermal_stats_helper_test_suite = {
	.name = "thermal_stats_helper_tests",
	.test_cases = thermal_stats_helper_test,
	.init = thermal_stats_test_init,
	.exit = thermal_stats_test_exit,
	.suite_init = thermal_stats_suite_init,
	.suite_exit = thermal_stats_suite_exit,
};

kunit_test_suite(thermal_stats_helper_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jikai Ma <jikai@google.com>");
