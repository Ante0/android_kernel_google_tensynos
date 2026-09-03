// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_sm_helper_tests.c Test suite to test all the shared memory helper functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/random.h>
#include <linux/string.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "thermal_sm_mock.h"

#define THERMAL_SM_TJ_PRESSURE_SIZE 13

static int mock_cdev_to_hw_tzid_ret;

struct thermal_sm_test_data {
	struct device *fake_dev;
	int get_addr_ret;
	int init_sm_ret;
	int read_sm_ret;
	u8 section;
	u32 mock_version;
	u32 mock_addr;
	void *remap_addr;
	u32 size;
	struct thermal_sm_tmu_state_data cdev_states[HW_THERMAL_ZONE_MAX];
	struct thermal_sm_stats_data thermal_stats;
	struct thermal_sm_trip_counter_data trip_counters;
	u8 thermal_sm_tj_pressure_data[THERMAL_SM_TJ_PRESSURE_SIZE];
};

int mock_thermal_sm_get_section_addr(u8 section, u32 *version, u32 *addr, u32 *size)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_sm_test_data *sm_data = test->priv;

	sm_data->section = section;
	*version = sm_data->mock_version;
	*addr = sm_data->mock_addr;
	*size = sm_data->size;
	return sm_data->get_addr_ret;
}

void __iomem *mock_devm_ioremap(struct device *dev, u32 addr, u32 size)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_sm_test_data *sm_data = test->priv;

	return sm_data->remap_addr;
}

void mock_memcpy_fromio(void *dest, void __iomem *base, u32 offset, u32 size)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_sm_test_data *sm_data = test->priv;

	memcpy(dest, sm_data->remap_addr + offset, size);
}

void mock_memcpy_toio(void __iomem *base, void *source, u32 offset, u32 size)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_sm_test_data *sm_data = test->priv;

	memcpy(sm_data->remap_addr + offset, source, size);
}

int mock_sm_cdev_to_hw_tzid(enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid)
{
	*hw_tzid = (enum hw_thermal_zone_id)cdev_id;
	return mock_cdev_to_hw_tzid_ret;
}

static void thermal_sm_init_failure_test(struct kunit *test)
{
	struct thermal_sm_test_data *sm_data = test->priv;
	u8 section = THERMAL_SM_CDEV_STATE, mock_tz_id = 1, cdev_state;
	u32 mock_addr;

	sm_data->mock_version = 0;
	sm_data->mock_addr = 0x12345678;
	sm_data->remap_addr = &mock_addr;
	sm_data->size = 1;
	sm_data->get_addr_ret = 0;

	// invalid arg: null dev
	KUNIT_EXPECT_EQ(test, thermal_sm_initialize_section(NULL, section), -EINVAL);
	// invalid arg: out of range section
	KUNIT_EXPECT_EQ(test,
			thermal_sm_initialize_section(sm_data->fake_dev, THERMAL_SM_MAX_SECTION),
			-EINVAL);
	// get sm addr failure
	sm_data->get_addr_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, thermal_sm_initialize_section(sm_data->fake_dev, section), -ENODEV);
	// unsupported version
	sm_data->get_addr_ret = 0;
	sm_data->mock_version = 0xFF;
	KUNIT_EXPECT_EQ(test, thermal_sm_initialize_section(sm_data->fake_dev, section),
			-EOPNOTSUPP);
	// ioremap failure
	sm_data->mock_version = 0;
	sm_data->get_addr_ret = 0;
	sm_data->remap_addr = NULL;
	KUNIT_EXPECT_EQ(test, thermal_sm_initialize_section(sm_data->fake_dev, section), -ENOMEM);

	// get cdev state with init failure
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tmu_cdev_state(mock_tz_id, &cdev_state), -ENODEV);
}

static void thermal_sm_get_tmu_cdev_state_test(struct kunit *test)
{
	struct thermal_sm_test_data *sm_data = test->priv;
	u8 cdev_state;
	enum hw_dev_type mock_cdev_id;
	u8 mock_cdev_state = get_random_u8();

	sm_data->mock_version = 1;
	sm_data->mock_addr = 0x12345678;
	sm_data->remap_addr = sm_data->cdev_states;
	sm_data->size = sizeof(sm_data->cdev_states);
	sm_data->get_addr_ret = 0;
	mock_cdev_id = HW_CDEV_BIG;
	sm_data->cdev_states[mock_cdev_id].cdev = mock_cdev_state;

	// init cdev state section test
	KUNIT_EXPECT_EQ(test,
			thermal_sm_initialize_section(sm_data->fake_dev, THERMAL_SM_CDEV_STATE),
			0);

	KUNIT_EXPECT_EQ(test, thermal_sm_get_tmu_cdev_state(mock_cdev_id, &cdev_state), 0);
	KUNIT_EXPECT_EQ(test, cdev_state,
			sm_data->cdev_states[HW_THERMAL_ZONE_0].cdev);

	// invalid arg: null pointer
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tmu_cdev_state(mock_cdev_id, NULL), -EINVAL);

	// invalid arg: out of range cdev
	mock_cdev_to_hw_tzid_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tmu_cdev_state(mock_cdev_id, &cdev_state), -EINVAL);
}

static void thermal_sm_thermal_stats_test(struct kunit *test)
{
	struct thermal_sm_test_data *sm_data = test->priv;
	struct thermal_sm_stats_metrics mock_metrics, *sm_metrics;
	struct thermal_sm_stats_thresholds mock_thresholds, *sm_thresholds;

	sm_data->mock_version = 1;
	sm_data->mock_addr = get_random_u32();
	sm_data->remap_addr = &sm_data->thermal_stats;
	sm_data->size = sizeof(sm_data->thermal_stats);
	sm_data->get_addr_ret = 0;

	sm_metrics = &sm_data->thermal_stats.metrics;
	sm_metrics->max_sample_timestamp = get_random_u64();
	sm_metrics->min_sample_timestamp = get_random_u64();
	sm_metrics->bucket_count = get_random_u8();
	sm_metrics->max_sample_temps = get_random_u8();
	sm_metrics->min_sample_temps = get_random_u8();
	for (int i = 0; i < ARRAY_SIZE(sm_metrics->time_in_state); ++i) {
		sm_metrics->time_in_state[i] = get_random_u64();
	}

	sm_thresholds = &sm_data->thermal_stats.thresholds;
	sm_thresholds->threshold_count = get_random_u8();
	for (int i = 0; i < ARRAY_SIZE(sm_thresholds->thresholds); ++i) {
		sm_thresholds->thresholds[i] = get_random_u8();
	}

	// uninitialized sm section
	KUNIT_EXPECT_EQ(test, thermal_sm_get_thermal_stats_metrics(&mock_metrics), -ENODEV);
	KUNIT_EXPECT_EQ(test, thermal_sm_get_thermal_stats_thresholds(&mock_thresholds), -ENODEV);
	KUNIT_EXPECT_EQ(test, thermal_sm_set_thermal_stats_thresholds(&mock_thresholds), -ENODEV);


	// init cdev state section test
	KUNIT_EXPECT_EQ(test,
			thermal_sm_initialize_section(sm_data->fake_dev, THERMAL_SM_STATS),
			0);

	KUNIT_EXPECT_EQ(test, thermal_sm_get_thermal_stats_metrics(&mock_metrics), 0);
	KUNIT_EXPECT_EQ(test, memcmp(&mock_metrics, sm_metrics, sizeof(mock_metrics)), 0);

	KUNIT_EXPECT_EQ(test, thermal_sm_get_thermal_stats_thresholds(&mock_thresholds), 0);
	KUNIT_EXPECT_EQ(test, memcmp(&mock_thresholds, sm_thresholds, sizeof(mock_thresholds)), 0);

	mock_thresholds.threshold_count++;
	for (int i = 0; i < ARRAY_SIZE(mock_thresholds.thresholds); ++i) {
		sm_thresholds->thresholds[i]++;
	}
	KUNIT_EXPECT_EQ(test, thermal_sm_set_thermal_stats_thresholds(&mock_thresholds), 0);
	KUNIT_EXPECT_EQ(test, memcmp(&mock_thresholds, sm_thresholds, sizeof(mock_thresholds)), 0);

	// invalid arg: null pointer
	KUNIT_EXPECT_EQ(test, thermal_sm_get_thermal_stats_metrics(NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_sm_get_thermal_stats_thresholds(NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_sm_set_thermal_stats_thresholds(NULL), -EINVAL);
}

static void thermal_sm_get_trip_counter_test(struct kunit *test)
{
	struct thermal_sm_test_data *sm_data = test->priv;
	struct thermal_sm_trip_counter_data trip_counter_out;

	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		sm_data->trip_counters.counters[i] = get_random_u64();

	sm_data->mock_addr = get_random_u32();
	sm_data->remap_addr = &sm_data->trip_counters;
	sm_data->size = sizeof(sm_data->trip_counters);

	// uninitialized sm section
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tmu_trip_counter(&trip_counter_out), -ENODEV);

	// init cdev state section test
	KUNIT_EXPECT_EQ(test,
			thermal_sm_initialize_section(sm_data->fake_dev, THERMAL_SM_TRIP_COUNT),
			0);

	// success
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tmu_trip_counter(&trip_counter_out), 0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		KUNIT_EXPECT_EQ(test, trip_counter_out.counters[i],
				sm_data->trip_counters.counters[i]);

	// Invalid argument
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tmu_trip_counter(NULL), -EINVAL);
}

static void thermal_sm_tj_pressure_test(struct kunit *test)
{
	struct thermal_sm_test_data *sm_data = test->priv;
	u8 data[THERMAL_SM_TJ_PRESSURE_SIZE];

	for (int i = 0; i < sizeof(sm_data->thermal_sm_tj_pressure_data); ++i)
		sm_data->thermal_sm_tj_pressure_data[i] = get_random_u8();

	sm_data->mock_addr = get_random_u32();
	sm_data->remap_addr = &sm_data->thermal_sm_tj_pressure_data;
	sm_data->size = sizeof(sm_data->thermal_sm_tj_pressure_data);

	// uninitialized sm section
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tj_pressure_data(data, sizeof(data)), -ENODEV);

	// init tj pressure section
	KUNIT_EXPECT_EQ(
		test, thermal_sm_initialize_section(sm_data->fake_dev, THERMAL_SM_TJ_PRESSURE), 0);

	// uninitialized input arguments
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tj_pressure_data(NULL, sizeof(data)), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tj_pressure_data(data, sizeof(data) + 1), -EINVAL);

	// success for complete buffer read
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tj_pressure_data(data, sizeof(data)), 0);
	KUNIT_EXPECT_MEMEQ(test, data, sm_data->thermal_sm_tj_pressure_data, sizeof(data));

	// success for partial read
	KUNIT_EXPECT_EQ(test, thermal_sm_get_tj_pressure_data(data, sizeof(data) - 1), 0);
	KUNIT_EXPECT_MEMEQ(test, data, sm_data->thermal_sm_tj_pressure_data, sizeof(data) - 1);
}

static struct kunit_case thermal_sm_helper_test[] = {
	KUNIT_CASE(thermal_sm_init_failure_test),  KUNIT_CASE(thermal_sm_get_tmu_cdev_state_test),
	KUNIT_CASE(thermal_sm_thermal_stats_test), KUNIT_CASE(thermal_sm_get_trip_counter_test),
	KUNIT_CASE(thermal_sm_tj_pressure_test),   {},
};

static int thermal_sm_test_init(struct kunit *test)
{
	struct thermal_sm_test_data *sm_data;

	sm_data = kunit_kzalloc(test, sizeof(*sm_data), GFP_KERNEL);
	sm_data->fake_dev = root_device_register("mock-thermal-sm-device");

	test->priv = sm_data;

	return 0;
}

static void thermal_sm_test_exit(struct kunit *test)
{
	struct thermal_sm_test_data *sm_data = test->priv;

	root_device_unregister(sm_data->fake_dev);
}

static struct kunit_suite thermal_sm_helper_test_suite = {
	.name = "thermal_sm_helper_tests",
	.test_cases = thermal_sm_helper_test,
	.init = thermal_sm_test_init,
	.exit = thermal_sm_test_exit,
};

kunit_test_suite(thermal_sm_helper_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jikai Ma <jikai@google.com>");
