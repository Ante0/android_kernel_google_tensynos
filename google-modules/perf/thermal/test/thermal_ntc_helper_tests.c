// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_ntc_helper_tests.c Test suite to test all the NTC helper functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <linux/thermal.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "thermal_ntc_mock.h"

#define THERMAL_NTC_TEST_REG_VAL 0x4A38
#define THERMAL_NTC_TEST_TEMPERATURE 62500
#define THERMAL_NTC_THRESHOLD_TEST_TEMP 70000
#define THERMAL_NTC_THRESHOLD_REG_VAL 0x3C
#define THERMAL_NTC_THRESHOLD_TEST_TEMP_2 50000
#define THERMAL_NTC_THRESHOLD_REG_VAL_2 0x65
#define THERMAL_NTC_THRESHOLD_DATA_REG_VAL_70C 0x3B60
#define THERMAL_NTC_THRESHOLD_DATA_REG_VAL_69_5C 0x3C00
#define THERMAL_NTC_THRESHOLD_DATA_REG_VAL_69C 0x3E80
#define THERMAL_NTC_THRESHOLD_DATA_REG_VAL_50C 0x6590
#define THERMAL_NTC_THRESHOLD_DATA_REG_VAL_50_5C 0x64FA
#define THERMAL_NTC_THRESHOLD_DATA_REG_VAL_51C 0x639C

struct ntc_test_data {
	struct thermal_zone_device *tzd;
	struct platform_device *fake_pdev;
	struct device *fake_dev;
	int get_temp_ret;
	int get_temp_reg_data;
	bool get_temp_avg_called;
	bool trip_disable_called;
	bool trip_enable_called;
	int trip_val;
	int hyst_val;
	int set_trip_ret;
	int ch_enable_ret;
	int clr_data_ret;
	int warn_irq_ret;
	int warn_irq_ct;
	int fault_irq_ret;
	int fault_irq_ct;
	int read_irq_ret;
	int reg_data[2];
	struct list_head mock_list;
	tr_handle stats_register_ret;
	bool stats_unregister_called;
	int stats_set_thresholds_ret;
	int stats_update_ret;
	int cpm_cb_ret;
	bool bool_dt_ret;
};

bool mock_ntc_of_property_read_bool(const struct device_node *np, const char *propname)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	return ntc_data->bool_dt_ret;
}

int mock_cpm_mbox_register_notification(enum hw_dev_type type, struct notifier_block *nb)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	return ntc_data->cpm_cb_ret;
}
static int mock_thermal_read_temp(int *reg_data)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	*reg_data = ntc_data->get_temp_reg_data;
	return ntc_data->get_temp_ret;
}

int mock_thermal_get_avg_temp(int *reg_data)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	ntc_data->get_temp_avg_called = true;
	return mock_thermal_read_temp(reg_data);
}

int mock_thermal_get_temp(int *reg_data)
{
	return mock_thermal_read_temp(reg_data);
}

int mock_register_temp_residency_stats(const char *name, char *group_name)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	return ntc_data->stats_register_ret;
}

int mock_unregister_temp_residency_stats(tr_handle instance)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	ntc_data->stats_unregister_called = true;
	return 0;
}

int mock_temp_residency_stats_set_thresholds(tr_handle instance, const int *thresholds,
					     int num_thresholds)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	return ntc_data->stats_set_thresholds_ret;
}

int mock_temp_residency_stats_update(tr_handle instance, int temp)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	return ntc_data->stats_update_ret;
}

int mock_thermal_ch_enable(void)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	return ntc_data->ch_enable_ret;
}

int mock_thermal_clr_data(void)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	return ntc_data->clr_data_ret;
}

int mock_ntc_clear_and_mask_irq(int ch_id, bool enable, bool fault_trip)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;
	int ret = 0;

	if (enable)
		ntc_data->trip_enable_called = true;
	else
		ntc_data->trip_disable_called = true;

	if (fault_trip) {
		ntc_data->fault_irq_ct++;
		ret = ntc_data->fault_irq_ret;
	} else {
		ntc_data->warn_irq_ct++;
		ret = ntc_data->warn_irq_ret;
	}

	return ret;
}

int mock_ntc_set_trips(int ch_id, int trip_val, int hyst_val)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	ntc_data->trip_val = trip_val;
	ntc_data->hyst_val = hyst_val;
	return ntc_data->set_trip_ret;
}

int mock_ntc_set_fault_trip(int ch_id, int trip_val)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	ntc_data->trip_val = trip_val;
	return ntc_data->set_trip_ret;
}

int mock_ntc_read_irq_status(int *reg_data)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	reg_data[0] = ntc_data->reg_data[0];
	reg_data[1] = ntc_data->reg_data[1];
	return ntc_data->read_irq_ret;
}

static void thermal_ntc_test_init_var(void)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;

	ntc_data->get_temp_ret = 0;
	ntc_data->get_temp_reg_data = 0;
	ntc_data->get_temp_avg_called = false;
	ntc_data->ch_enable_ret = 0;
	ntc_data->clr_data_ret = 0;
	ntc_data->warn_irq_ret = 0;
	ntc_data->warn_irq_ct = 0;
	ntc_data->fault_irq_ret = 0;
	ntc_data->fault_irq_ct = 0;
	ntc_data->trip_disable_called = false;
	ntc_data->trip_enable_called = false;
	ntc_data->trip_val = 0;
	ntc_data->hyst_val = 0;
	ntc_data->set_trip_ret = 0;
	ntc_data->read_irq_ret = 0;
	ntc_data->reg_data[0] = 0;
	ntc_data->reg_data[1] = 0;
	ntc_data->stats_register_ret = 0;
	ntc_data->stats_unregister_called = false;
	ntc_data->stats_set_thresholds_ret = 0;
	ntc_data->stats_update_ret = 0;
	ntc_data->cpm_cb_ret = 0;
	ntc_data->bool_dt_ret = false;
}

struct ntc_map_test_case {
	const char *str;
	int idx;
	bool test_lower_bound;
	bool test_upper_bound;
};

static struct ntc_map_test_case ntc_map_test_case[] = {
	{
		.str = "First element search",
		.idx = 0,
	},
	{
		.str = "Second element search",
		.idx = 1,
	},
	{
		.str = "Last but one element search",
		.idx = ARRAY_SIZE(adc_temperature_data) - 2,
	},
	{
		.str = "middle element search",
		.idx = (ARRAY_SIZE(adc_temperature_data) / 2),
	},
	{
		.str = "Test upper bound",
		.test_upper_bound = true,
	},
	{
		.str = "Test lower bound",
		.test_lower_bound = true,
	},
};

static void thermal_ntc_map_data_test(struct kunit *test)
{
	int i = 0, max = ARRAY_SIZE(adc_temperature_data) - 1;

	// Invalid argument.
	KUNIT_EXPECT_EQ(test, thermal_ntc_map_data(0, ADC_MAP_INDEX_MAX, adc_temperature_data,
						   ARRAY_SIZE(adc_temperature_data)), -EINVAL);

	// Use input value to test the slope computation value.
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_map_data(THERMAL_NTC_TEST_REG_VAL, ADC_MAP_REGISTER_VALUE,
					     adc_temperature_data,
					     ARRAY_SIZE(adc_temperature_data)),
			THERMAL_NTC_TEST_TEMPERATURE);
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_map_data(THERMAL_NTC_TEST_TEMPERATURE, ADC_MAP_TEMPERATURE,
					     adc_temperature_data,
					     ARRAY_SIZE(adc_temperature_data)),
			THERMAL_NTC_TEST_REG_VAL);

	// Test boundary conditions.
	for (i = 0; i < ARRAY_SIZE(ntc_map_test_case); i++) {
		struct ntc_map_test_case *test_case = &ntc_map_test_case[i];

		if (test_case->test_lower_bound) {
			KUNIT_EXPECT_EQ_MSG(test,
					    thermal_ntc_map_data(
						adc_temperature_data[0][ADC_MAP_REGISTER_VALUE]+3,
						ADC_MAP_REGISTER_VALUE,
						adc_temperature_data,
						ARRAY_SIZE(adc_temperature_data)),
					    adc_temperature_data[0][ADC_MAP_TEMPERATURE],
					    test_case->str);
			KUNIT_EXPECT_EQ_MSG(test,
					    thermal_ntc_map_data(
						adc_temperature_data[0][ADC_MAP_TEMPERATURE]-3,
						ADC_MAP_TEMPERATURE,
						adc_temperature_data,
						ARRAY_SIZE(adc_temperature_data)),
					    adc_temperature_data[0][ADC_MAP_REGISTER_VALUE],
					    test_case->str);
			continue;
		}
		if (test_case->test_upper_bound) {
			KUNIT_EXPECT_EQ_MSG(test,
					    thermal_ntc_map_data(
						adc_temperature_data[max][ADC_MAP_REGISTER_VALUE]-3,
						ADC_MAP_REGISTER_VALUE,
						adc_temperature_data,
						ARRAY_SIZE(adc_temperature_data)),
					    adc_temperature_data[max][ADC_MAP_TEMPERATURE],
					    test_case->str);
			KUNIT_EXPECT_EQ_MSG(test,
					    thermal_ntc_map_data(
						adc_temperature_data[max][ADC_MAP_TEMPERATURE]+3,
						ADC_MAP_TEMPERATURE,
						adc_temperature_data,
						ARRAY_SIZE(adc_temperature_data)),
					    adc_temperature_data[max][ADC_MAP_REGISTER_VALUE],
					    test_case->str);
			continue;
		}
		KUNIT_EXPECT_EQ_MSG(
				test,
				thermal_ntc_map_data(
				adc_temperature_data[test_case->idx][ADC_MAP_REGISTER_VALUE],
				ADC_MAP_REGISTER_VALUE,
				adc_temperature_data,
				ARRAY_SIZE(adc_temperature_data)),
				adc_temperature_data[test_case->idx][ADC_MAP_TEMPERATURE],
				test_case->str);
		KUNIT_EXPECT_EQ_MSG(test,
				    thermal_ntc_map_data(
					adc_temperature_data[test_case->idx][ADC_MAP_TEMPERATURE],
					ADC_MAP_TEMPERATURE,
					adc_temperature_data,
					ARRAY_SIZE(adc_temperature_data)),
				    adc_temperature_data[test_case->idx][ADC_MAP_REGISTER_VALUE],
				    test_case->str);
	}
}

static void thermal_ntc_configure_sensor_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	struct google_sensor_data *sens = thermal_zone_device_priv(ntc_data->tzd);

	// success
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test, thermal_ntc_configure_sensor(sens), 0);

	ntc_data->stats_register_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_ntc_configure_sensor(sens), 0);

	thermal_ntc_test_init_var();
	ntc_data->stats_set_thresholds_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_ntc_configure_sensor(sens), 0);
}

static void thermal_ntc_cleanup_sensor_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	struct google_sensor_data *sens = thermal_zone_device_priv(ntc_data->tzd);

	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test, thermal_ntc_cleanup_sensor(sens), 0);
	KUNIT_EXPECT_EQ(test, ntc_data->stats_unregister_called, true);

	thermal_ntc_test_init_var();
	sens->sens_data.ntc_data.tr_stats_handle = -1;
	KUNIT_EXPECT_EQ(test, thermal_ntc_cleanup_sensor(sens), 0);
	KUNIT_EXPECT_EQ(test, ntc_data->stats_unregister_called, false);
	sens->sens_data.ntc_data.tr_stats_handle = 0;
}

static void thermal_ntc_get_temp_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	int temp = 0, i = 0;
	struct google_sensor_data *gsens = thermal_zone_device_priv(ntc_data->tzd);
	struct ntc_sensor_data *sens = &gsens->sens_data.ntc_data;
	ktime_t time_val;

	// success
	thermal_ntc_test_init_var();
	ntc_data->get_temp_reg_data = THERMAL_NTC_TEST_REG_VAL;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_EQ(test, temp, THERMAL_NTC_TEST_TEMPERATURE);

	/* Fillup the temp log buffer.*/
	sens->log_ct = 0;
	for (i = 0; i < TEMPERATURE_LOG_BUF_LEN; i++) {
		sens->temp_log[i].time = 0;
		sens->temp_log[i].read_ct = 0;
		sens->temp_log[i].reg = 0;
		sens->temp_log[i].temperature = 0;
	}
	ntc_data->bool_dt_ret = true;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			0);

	ntc_data->get_temp_reg_data = THERMAL_NTC_TEST_REG_VAL;
	for (i = 0; i < TEMPERATURE_LOG_BUF_LEN; i++) {
		// Check the values are zero.
		KUNIT_EXPECT_EQ(test, sens->temp_log[i].time, 0);
		KUNIT_EXPECT_EQ(test, sens->temp_log[i].read_ct, 0);
		KUNIT_EXPECT_EQ(test, sens->temp_log[i].temperature, 0);
		KUNIT_EXPECT_EQ(test, sens->temp_log[i].reg, 0);

		ntc_data->get_temp_reg_data += 100;
		KUNIT_EXPECT_EQ(test,
				thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);

		// Check the values are valid.
		KUNIT_EXPECT_NE(test, sens->temp_log[i].time, 0);
		KUNIT_EXPECT_EQ(test, sens->temp_log[i].read_ct, 1);
		KUNIT_EXPECT_LT(test, sens->temp_log[i].temperature,
				THERMAL_NTC_TEST_TEMPERATURE);
		KUNIT_EXPECT_EQ(test, sens->temp_log[i].reg,
				ntc_data->get_temp_reg_data);
		KUNIT_EXPECT_EQ(test, sens->log_ct,
				(i != TEMPERATURE_LOG_BUF_LEN-1) ? (i + 1) : 0);
	}
	// Check if the temp log rotates.
	time_val = sens->temp_log[sens->log_ct].time;
	ntc_data->get_temp_reg_data += 100;
	KUNIT_EXPECT_EQ(test, sens->log_ct, 0);
	KUNIT_EXPECT_EQ(test, thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_EQ(test, sens->log_ct, 1);
	KUNIT_EXPECT_GT(test, sens->temp_log[sens->log_ct].time, time_val);

	// Check if temp updates read_ct in idx = 1.
	ntc_data->get_temp_reg_data = 0;
	for (i = 0; i < TEMPERATURE_LOG_BUF_LEN; i++) {
		KUNIT_EXPECT_EQ(test,
				thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
		KUNIT_EXPECT_EQ(test, sens->temp_log[sens->log_ct - 1].read_ct, i+1);
	}

	// Check the filtered reading returns the last valid reading.
	ntc_data->stats_update_ret = EXTREME_HIGH_TEMP;
	KUNIT_EXPECT_EQ(test, thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_EQ(test, temp, sens->temp_log[0].temperature);
	// Check if the invalid reading persists.
	sens->temp_log[1].time = 0;
	KUNIT_EXPECT_EQ(test, thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_EQ(test, temp, sens->temp_log[1].temperature);
	sens->temp_log[1].time = ktime_get_real();
	// Check if filtering happens only for inst temp.
	ntc_data->bool_dt_ret = true;
	ntc_data->get_temp_avg_called = false;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			0);
	__thermal_ntc_flush_work();
	sens->irq_handle_phase = false;
	sens->irq_temp = THERMAL_TEMP_INVALID;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_TRUE(test, ntc_data->get_temp_avg_called);
	KUNIT_EXPECT_EQ(test, temp, THERMAL_NTC_MAX_TRIP);

	// get_temp reg read error.
	ntc_data->get_temp_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, thermal_ntc_get_temp(ntc_data->tzd, &temp), -ENODEV);

	// stats update error
	thermal_ntc_test_init_var();
	ntc_data->stats_update_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);

	// Check if we switch reading the inst temp during IRQ phase.
	ntc_data->get_temp_reg_data = THERMAL_NTC_TEST_REG_VAL;
	ntc_data->bool_dt_ret = true;
	ntc_data->get_temp_avg_called = false;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			0);
	__thermal_ntc_flush_work();
	sens->irq_handle_phase = true;
	sens->irq_temp = THERMAL_TEMP_INVALID;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->get_temp_avg_called);
	KUNIT_EXPECT_EQ(test, temp, THERMAL_NTC_TEST_TEMPERATURE);

	// Check if we read the irq temp if available.
	ntc_data->get_temp_reg_data = THERMAL_NTC_TEST_REG_VAL;
	ntc_data->get_temp_avg_called = false;
	sens->irq_handle_phase = true;
	sens->irq_temp = THERMAL_NTC_THRESHOLD_TEST_TEMP;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->get_temp_avg_called);
	KUNIT_EXPECT_EQ(test, temp, THERMAL_NTC_THRESHOLD_TEST_TEMP);
}

static void thermal_ntc_configure_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;

	// success
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, NTC_THERMAL_MAX_CHANNEL);
	KUNIT_EXPECT_EQ(test, ntc_data->fault_irq_ct, NTC_THERMAL_MAX_CHANNEL);

	// INvalid arg
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, 0),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, -1),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(NULL, -1),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(NULL, NTC_THERMAL_MAX_CHANNEL),
			-EINVAL);

	//ch_enable error
	thermal_ntc_test_init_var();
	ntc_data->ch_enable_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			-ENODEV);

	//clr_data error
	thermal_ntc_test_init_var();
	ntc_data->clr_data_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			-ENODEV);

	//warn_irq error
	thermal_ntc_test_init_var();
	ntc_data->warn_irq_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			-ENODEV);

	//fault irq error
	thermal_ntc_test_init_var();
	ntc_data->fault_irq_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			-ENODEV);
}

static void thermal_construct_and_register(int sensor_ct)
{
	struct kunit *test = kunit_get_current_test();
	struct ntc_test_data *ntc_data = test->priv;
	struct device *dev = ntc_data->fake_dev;
	int i = 0;
	struct google_sensor_data *sens = NULL;

	for (i = 0; i < sensor_ct; i++) {
		sens = devm_kzalloc(dev, sizeof(*sens), GFP_KERNEL);
		KUNIT_EXPECT_NOT_ERR_OR_NULL(test, sens);

		sens->dev = dev;
		sens->sensor_id = i;
		sens->tzd = ntc_data->tzd;
		sens->sens_data.ntc_data.high_trip = THERMAL_NTC_THRESHOLD_TEST_TEMP;
		sens->sens_data.ntc_data.low_trip = THERMAL_NTC_THRESHOLD_TEST_TEMP_2;
		list_add_tail(&sens->node, &ntc_data->mock_list);
	}
}

static void thermal_ntc_post_configure_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	struct google_sensor_data *sens;

	thermal_construct_and_register(NTC_THERMAL_MAX_CHANNEL);
	// mbox register error
	ntc_data->cpm_cb_ret = -EEXIST;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_post_configure(&ntc_data->mock_list),
			ntc_data->cpm_cb_ret);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_disable_called);

	// Success
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_post_configure(&ntc_data->mock_list), 0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, NTC_THERMAL_MAX_CHANNEL);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);
	list_for_each_entry(sens, &ntc_data->mock_list, node) {
		KUNIT_EXPECT_FALSE(test, sens->sens_data.ntc_data.irq_handle_phase);
		KUNIT_EXPECT_EQ(test, sens->sens_data.ntc_data.irq_temp,
				THERMAL_TEMP_INVALID);
	}

	// IRQ warn error.
	thermal_ntc_test_init_var();
	ntc_data->warn_irq_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_post_configure(&ntc_data->mock_list), 0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, NTC_THERMAL_MAX_CHANNEL);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);
}

static void thermal_ntc_avg_temp_switch_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	int temp;

	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			0);
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_FALSE(test,
			   ntc_data->get_temp_avg_called);
	ntc_data->bool_dt_ret = true;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			0);
	__thermal_ntc_flush_work();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_get_temp(ntc_data->tzd, &temp), 0);
	KUNIT_EXPECT_TRUE(test,
			   ntc_data->get_temp_avg_called);
}

static void thermal_ntc_set_trips_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	int max = max = ARRAY_SIZE(adc_thresh_data) - 1;

	// success
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_trips(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP_2,
					      THERMAL_NTC_THRESHOLD_TEST_TEMP),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL);
	KUNIT_EXPECT_EQ(test, ntc_data->hyst_val, THERMAL_NTC_THRESHOLD_REG_VAL_2);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_disable_called);

	// Setting same trip return early.
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_trips(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP_2,
					      THERMAL_NTC_THRESHOLD_TEST_TEMP),
			0);
	KUNIT_EXPECT_NE(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL);
	KUNIT_EXPECT_NE(test, ntc_data->hyst_val, THERMAL_NTC_THRESHOLD_REG_VAL_2);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_disable_called);

	// Setting INT_MAX and INT_MIN.
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_trips(ntc_data->tzd, INT_MIN, INT_MAX),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->trip_val, 0);
	KUNIT_EXPECT_EQ(test, ntc_data->hyst_val, 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_disable_called);

	// Setting INT_MAX or INT_MIN.
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_trips(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP_2,
					      INT_MAX),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->trip_val,
			adc_thresh_data[max][ADC_MAP_REGISTER_VALUE] >> 4);
	KUNIT_EXPECT_EQ(test, ntc_data->hyst_val, THERMAL_NTC_THRESHOLD_REG_VAL_2);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_disable_called);

	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_trips(ntc_data->tzd, INT_MIN,
					      THERMAL_NTC_THRESHOLD_TEST_TEMP),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL);
	KUNIT_EXPECT_EQ(test, ntc_data->hyst_val,
			adc_thresh_data[0][ADC_MAP_REGISTER_VALUE] >> 4);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_disable_called);

	// NTC message error.
	thermal_ntc_test_init_var();
	ntc_data->warn_irq_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_trips(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP_2,
					      THERMAL_NTC_THRESHOLD_TEST_TEMP),
			-ENODEV);

	thermal_ntc_test_init_var();
	ntc_data->set_trip_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_trips(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP_2,
					      THERMAL_NTC_THRESHOLD_TEST_TEMP),
			-EINVAL);
}

static void thermal_ntc_set_critical_trip_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	// success
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_fault_trip(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);

	// Setting same trip return early.
	thermal_ntc_test_init_var();
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_fault_trip(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP),
			0);
	KUNIT_EXPECT_NE(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);

	// NTC message error.
	thermal_ntc_test_init_var();
	ntc_data->fault_irq_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_fault_trip(ntc_data->tzd,
						   THERMAL_NTC_THRESHOLD_TEST_TEMP_2),
			-ENODEV);

	thermal_ntc_test_init_var();
	ntc_data->set_trip_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_set_fault_trip(ntc_data->tzd, THERMAL_NTC_THRESHOLD_TEST_TEMP),
			-EINVAL);
}

static void thermal_ntc_irq_notify_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	u32 payload[3] = {0};

	// Switch to avg temp reading.
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_configure(ntc_data->fake_pdev, NTC_THERMAL_MAX_CHANNEL),
			0);
	__thermal_ntc_flush_work();

	// success
	thermal_ntc_test_init_var();
	thermal_construct_and_register(NTC_THERMAL_MAX_CHANNEL);
	payload[0] = NTC_THERMAL_MAX_CHANNEL - 1;
	payload[1] = THERMAL_NTC_OT_EVENT;
	ntc_data->get_temp_reg_data = THERMAL_NTC_THRESHOLD_DATA_REG_VAL_70C;
	KUNIT_EXPECT_EQ(test, thermal_ntc_post_configure(&ntc_data->mock_list), 0);
	ntc_data->warn_irq_ct = 0;
	ntc_data->trip_enable_called = false;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);

	// Test with temperature within the guard band
	ntc_data->get_temp_reg_data = THERMAL_NTC_THRESHOLD_DATA_REG_VAL_69_5C;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);

	// Test with temperature outside guard band.
	ntc_data->get_temp_reg_data = THERMAL_NTC_THRESHOLD_DATA_REG_VAL_69C;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 1);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);
	ntc_data->trip_enable_called = false;
	ntc_data->warn_irq_ct = 0;

	//UT Event.
	payload[0] = NTC_THERMAL_MAX_CHANNEL - 1;
	payload[1] = THERMAL_NTC_UT_EVENT;
	ntc_data->get_temp_reg_data = THERMAL_NTC_THRESHOLD_DATA_REG_VAL_50C;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);

	// UT within guard band.
	ntc_data->get_temp_reg_data = THERMAL_NTC_THRESHOLD_DATA_REG_VAL_50_5C;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);

	// OT and UT within guard band.
	payload[1] = THERMAL_NTC_OT_EVENT | THERMAL_NTC_UT_EVENT;
	ntc_data->get_temp_reg_data = THERMAL_NTC_THRESHOLD_DATA_REG_VAL_50_5C;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 0);
	KUNIT_EXPECT_FALSE(test, ntc_data->trip_enable_called);
	payload[1] = THERMAL_NTC_UT_EVENT;

	// UT outside guard band.
	ntc_data->get_temp_reg_data = THERMAL_NTC_THRESHOLD_DATA_REG_VAL_51C;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 1);
	KUNIT_EXPECT_TRUE(test, ntc_data->trip_enable_called);

	// Invalid channel ID.
	thermal_ntc_test_init_var();
	thermal_construct_and_register(NTC_THERMAL_MAX_CHANNEL);
	KUNIT_EXPECT_EQ(test, thermal_ntc_post_configure(&ntc_data->mock_list), 0);
	ntc_data->warn_irq_ct = 0;
	payload[0] = NTC_THERMAL_MAX_CHANNEL + 1;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 0);


	// emul_temp
	thermal_ntc_test_init_var();
	thermal_construct_and_register(NTC_THERMAL_MAX_CHANNEL);
	KUNIT_EXPECT_EQ(test, thermal_ntc_post_configure(&ntc_data->mock_list), 0);
	ntc_data->warn_irq_ct = 0;
	payload[0] = NTC_THERMAL_MAX_CHANNEL - 1;
	ntc_data->tzd->emul_temperature = 20000;
	KUNIT_EXPECT_EQ(test,
			thermal_ntc_process_irq(&thermal_ntc_irq_notifier, 0,
						(void *)payload),
			0);
	KUNIT_EXPECT_EQ(test, ntc_data->warn_irq_ct, 0);
	ntc_data->tzd->emul_temperature = 0;
}

static void thermal_ntc_create_tzd_with_trip(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	int i = 0;

	ntc_data->tzd->num_trips = THERMAL_TRIP_CRITICAL + 1;
	for (i = 0; i <= THERMAL_TRIP_CRITICAL; i++)
		ntc_data->tzd->trips[i].trip.type = i;
}

struct thermal_ntc_set_trip_test_walk_data {
	struct kunit *test;
	int index;
};

static int thermal_ntc_set_trip_test_walk_cb(struct thermal_trip *trip, void *data)
{
	struct thermal_ntc_set_trip_test_walk_data * const twd = data;
	struct kunit *test = twd->test;
	struct ntc_test_data *ntc_data = test->priv;
	int trip_idx = twd->index;

	if (trip_idx != THERMAL_TRIP_HOT) {
		/* Check for trip_type other than HOT. */
		char test_case_str[10] = "";

		sprintf(test_case_str, "trip:%d", trip_idx);
		thermal_ntc_test_init_var();
		KUNIT_EXPECT_EQ_MSG(test,
				    __thermal_ntc_set_trip_temp(ntc_data->tzd, trip,
								THERMAL_NTC_THRESHOLD_TEST_TEMP),
				    0, test_case_str);
		KUNIT_EXPECT_EQ_MSG(test, ntc_data->trip_val, 0, test_case_str);
	} else {
		/* temperature > initial fault trip */
		thermal_ntc_test_init_var();
		KUNIT_EXPECT_EQ(test,
				thermal_ntc_set_fault_trip(ntc_data->tzd,
							   THERMAL_NTC_THRESHOLD_TEST_TEMP),
				0);
		KUNIT_EXPECT_EQ(test,
				__thermal_ntc_set_trip_temp(ntc_data->tzd, trip,
							    THERMAL_NTC_THRESHOLD_TEST_TEMP
							    + 10000),
				0);
		KUNIT_EXPECT_EQ(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL);

		/* temperature == initial fault trip */
		KUNIT_EXPECT_EQ(test,
				__thermal_ntc_set_trip_temp(ntc_data->tzd, trip,
							    THERMAL_NTC_THRESHOLD_TEST_TEMP),
				0);
		KUNIT_EXPECT_EQ(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL);

		/* temperature < initial fault trip */
		KUNIT_EXPECT_EQ(test,
				__thermal_ntc_set_trip_temp(ntc_data->tzd, trip,
							    THERMAL_NTC_THRESHOLD_TEST_TEMP_2),
				0);
		KUNIT_EXPECT_EQ(test, ntc_data->trip_val, THERMAL_NTC_THRESHOLD_REG_VAL_2);
	}

	twd->index++;
	return 0;
}

static void thermal_ntc_set_trip_cb_test(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	struct thermal_ntc_set_trip_test_walk_data twd = {
		.test = test,
	};

	thermal_ntc_create_tzd_with_trip(test);

	for_each_thermal_trip(ntc_data->tzd, thermal_ntc_set_trip_test_walk_cb, &twd);
}

static struct kunit_case thermal_ntc_helper_test[] = {
	KUNIT_CASE(thermal_ntc_map_data_test),
	KUNIT_CASE(thermal_ntc_get_temp_test),
	KUNIT_CASE(thermal_ntc_avg_temp_switch_test),
	KUNIT_CASE(thermal_ntc_configure_test),
	KUNIT_CASE(thermal_ntc_post_configure_test),
	KUNIT_CASE(thermal_ntc_set_trips_test),
	KUNIT_CASE(thermal_ntc_set_critical_trip_test),
	KUNIT_CASE(thermal_ntc_irq_notify_test),
	KUNIT_CASE(thermal_ntc_set_trip_cb_test),
	KUNIT_CASE(thermal_ntc_configure_sensor_test),
	KUNIT_CASE(thermal_ntc_cleanup_sensor_test),
	{},
};

static int mock_get_temp(struct thermal_zone_device *tzd, int *temp)
{
	return 0;
}

static struct thermal_zone_device *__thermal_ntc_create_tripless_tzd(
		struct kunit *test, struct google_sensor_data *sens)
{
	struct thermal_zone_device *tzd;

	/* Allocate enough trips for thermal_ntc_create_tzd_with_trip() */
	tzd = kunit_kzalloc(test, struct_size(tzd, trips, THERMAL_TRIP_CRITICAL + 1), GFP_KERNEL);
	tzd->ops.get_temp = mock_get_temp;
	tzd->devdata = sens;
	snprintf(tzd->type, THERMAL_NAME_LENGTH, "mock-thermal-sensor");

	return tzd;
}

static int thermal_ntc_test_init(struct kunit *test)
{
	struct ntc_test_data *ntc_data;
	struct google_sensor_data *sens;

	sens = kunit_kzalloc(test, sizeof(*sens), GFP_KERNEL);
	ntc_data = kunit_kzalloc(test, sizeof(*ntc_data), GFP_KERNEL);
	ntc_data->tzd = __thermal_ntc_create_tripless_tzd(test, sens);
	ntc_data->fake_pdev = platform_device_alloc("mock_thermal-pdevice", -1);
	ntc_data->fake_dev = root_device_register("mock_thermal-device");
	sens->dev = ntc_data->fake_dev;

	test->priv = ntc_data;
	INIT_LIST_HEAD(&ntc_data->mock_list);

	return 0;
}

static void thermal_ntc_test_exit(struct kunit *test)
{
	struct ntc_test_data *ntc_data = test->priv;
	struct list_head *pos, *n;

	list_for_each_safe(pos, n, &ntc_data->mock_list) {
		struct google_sensor_data *sens =
				list_entry(pos, struct google_sensor_data, node);
		list_del(&sens->node);
		devm_kfree(ntc_data->fake_dev, sens);
	}
	root_device_unregister(ntc_data->fake_dev);
	platform_device_put(ntc_data->fake_pdev);
	kunit_kfree(test, ntc_data);
}

static struct kunit_suite thermal_ntc_helper_test_suite = {
	.name = "thermal_ntc_helper_tests",
	.test_cases = thermal_ntc_helper_test,
	.init = thermal_ntc_test_init,
	.exit = thermal_ntc_test_exit,
};

kunit_test_suite(thermal_ntc_helper_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
