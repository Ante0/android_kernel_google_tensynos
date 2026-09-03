// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_tmu_helper_tests.c Test suite to test all the TMU helper functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/thermal.h>
#include <linux/units.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "thermal_msg_helper.h"
#include "thermal_tmu_debugfs_helper.h"
#include "thermal_tmu_helper.h"
#include "thermal_tmu_mock.h"
#include "thermal_zone_helper.h"

struct tmu_test_data {
	struct device *fake_dev;
	struct thermal_zone_device *fake_tzd;
	struct tmu_sensor_data *mock_tmu_data, tmu_set_data;
	struct google_sensor_data *mock_sens_data;
	int read_dt_prop_ret;
	int mock_dt_prop;
	int parse_dt_ret;
	int set_gov_param_ret;
	int set_trip_temp_ret;
	int set_trip_hyst_ret;
	int set_trip_type_ret;
	int set_polling_delay_ret;
	int set_gov_select_ret;
	int get_trip_counter_ret;
	int get_tzd_trip_ret;
	int sm_init_ret;
	int sm_read_trip_counter_ret;
	unsigned long mock_gov_select;
	int get_temp_ret;
	int mock_tmu_temp;
	u8 trip_temp_set_values[THERMAL_TMU_NR_TRIPS];
	u8 trip_hyst_set_values[THERMAL_TMU_NR_TRIPS];
	u8 trip_type_set_values[THERMAL_TMU_NR_TRIPS];
	struct thermal_sm_trip_counter_data trip_counter_values;
	int set_temp_state_table_len;
	struct tmu_temp_state_st set_temp_state_table[THERMAL_TEMP_LUT_MAX_ENTRIES];
};

static struct thermal_trip mock_trips[THERMAL_TMU_NR_TRIPS] = {
	{.temperature = 20000, .hysteresis = 5000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
	{.temperature = 60000, .hysteresis = 6000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
	{.temperature = 90000, .hysteresis = 7000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
	{.temperature = 80000, .hysteresis = 8000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
	{.temperature = 98000, .hysteresis = 9000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
	{.temperature = 103000, .hysteresis = 10000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
	{.temperature = 105000, .hysteresis = 11000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
	{.temperature = 115000, .hysteresis = 12000, .type = THERMAL_TRIP_ACTIVE, .priv = NULL},
};
static struct device *fake_dev;
static struct thermal_zone_device *fake_tzd;

int mock_of_property_read_s32(struct device_node *np, char *name, s32 *val)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	*val = tmu_test_data->mock_dt_prop;

	return tmu_test_data->read_dt_prop_ret;
}

bool mock_of_property_read_bool(struct device_node *np, char *name)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	return (tmu_test_data->read_dt_prop_ret > 0);
}

int mock_thermal_tmu_parse_tzd_dt(struct google_sensor_data *sensor)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	return tmu_test_data->parse_dt_ret;
}

int mock_msg_tmu_set_gov_param(u8 tz_id, u8 type, int val)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	switch (type) {
	case TMU_GOV_PARAM_K_PO:
		tmu_test_data->tmu_set_data.pi_param.k_po = val;
		break;
	case TMU_GOV_PARAM_K_PU:
		tmu_test_data->tmu_set_data.pi_param.k_pu = val;
		break;
	case TMU_GOV_PARAM_K_I:
		tmu_test_data->tmu_set_data.pi_param.k_i = val;
		break;
	case TMU_GOV_PARAM_I_MAX:
		tmu_test_data->tmu_set_data.pi_param.k_i = val;
		break;
	case TMU_GOV_PARAM_EARLY_THROTTLE_K_P:
		tmu_test_data->tmu_set_data.early_throttle_param.k_p = val;
		break;
	case TMU_GOV_PARAM_IRQ_GAIN:
		tmu_test_data->tmu_set_data.gradual_param.irq_gain = val;
		break;
	case TMU_GOV_PARAM_TIMER_GAIN:
		tmu_test_data->tmu_set_data.gradual_param.timer_gain = val;
		break;
	default:
		break;
	}

	return tmu_test_data->set_gov_param_ret;
}

int mock_msg_tmu_set_trip_temp(u8 tz_id, u8 *temperature, int num_temperature)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	memcpy(tmu_test_data->trip_temp_set_values, temperature,
	       num_temperature * sizeof(*temperature));

	return tmu_test_data->set_trip_temp_ret;
}
int mock_msg_tmu_set_trip_hyst(u8 tz_id, u8 *hysteresis, int num_hysteresis)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	memcpy(tmu_test_data->trip_hyst_set_values, hysteresis,
	       num_hysteresis * sizeof(*hysteresis));

	return tmu_test_data->set_trip_hyst_ret;
}
int mock_msg_tmu_set_trip_type(u8 tz_id, u8 *type, int num_type)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	memcpy(tmu_test_data->trip_type_set_values, type, num_type * sizeof(*type));

	return tmu_test_data->set_trip_type_ret;
}
int mock_msg_tmu_set_polling_delay_ms(u8 tz_id, u16 delay_ms)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	tmu_test_data->tmu_set_data.polling_delay_ms = delay_ms;

	return tmu_test_data->set_polling_delay_ret;
}

int mock_msg_tmu_set_gov_select(u8 tz_id, u8 gov_select)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	tmu_test_data->mock_gov_select = gov_select;

	return tmu_test_data->set_gov_select_ret;
}
int mock_msg_tmu_get_temp(u8 tz_id, u8 *temperature)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	*temperature = tmu_test_data->mock_tmu_temp;

	return tmu_test_data->get_temp_ret;
}

int mock_msg_tmu_set_temp_lut(u8 tz_id, u8 temperature, u8 cdev_state, bool append)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	if (append) {
		int len = tmu_test_data->set_temp_state_table_len;

		if (len == THERMAL_TEMP_LUT_MAX_ENTRIES)
			return -ENOMEM;

		/* unit test only, it does not maintain mononicity. */
		tmu_test_data->set_temp_state_table[len].temp = temperature;
		tmu_test_data->set_temp_state_table[len].state = cdev_state;
		tmu_test_data->set_temp_state_table_len++;
	} else {
		memset(&tmu_test_data->set_temp_state_table[0], 0,
			sizeof(struct tmu_temp_state_st)*THERMAL_TEMP_LUT_MAX_ENTRIES);
		tmu_test_data->set_temp_state_table[0].temp = temperature;
		tmu_test_data->set_temp_state_table[0].state = cdev_state;
		tmu_test_data->set_temp_state_table_len = 1;
	}

	return 0;
}

int mock_msg_tmu_get_temp_lut(u8 tz_id, u8 index, u8 *temperature, u8 *cdev_state)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	if (!temperature || !cdev_state)
		return -EINVAL;

	if (index >= tmu_test_data->set_temp_state_table_len) {
		*temperature = 0;
		*cdev_state = __UINT8_MAX__;
		return 0;
	}

	*temperature = tmu_test_data->set_temp_state_table[index].temp;
	*cdev_state = tmu_test_data->set_temp_state_table[index].state;

	return 0;
}

int mock_thermal_zone_for_each_trip(struct thermal_zone_device *tzd,
				    int (*cb)(struct thermal_trip *, void *),
				    void *data)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;
	int ret = thermal_zone_for_each_trip(tzd, cb, data);

	if (tmu_test_data->get_tzd_trip_ret)
		return tmu_test_data->get_tzd_trip_ret;

	return ret;
}

int mock_msg_tmu_get_trip_counter_snapshot(u8 tz_id)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	return tmu_test_data->get_trip_counter_ret;
}

int mock_thermal_sm_initialize_section(struct device *dev, enum thermal_sm_section section)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	return tmu_test_data->sm_init_ret;
}

int mock_thermal_sm_get_tmu_trip_counter(struct thermal_sm_trip_counter_data *data)
{
	struct kunit *test = kunit_get_current_test();
	struct tmu_test_data *tmu_test_data = test->priv;

	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		data->counters[i] = tmu_test_data->trip_counter_values.counters[i];

	return tmu_test_data->sm_read_trip_counter_ret;
}

static void thermal_tmu_read_dt_property_with_default_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sens = tmu_test_data->mock_sens_data;
	struct device_node mock_node;
	int val, mock_default = 10;

	// success read from dt
	tmu_test_data->mock_dt_prop = 2;
	KUNIT_EXPECT_EQ(test,
			__thermal_tmu_read_dt_property_with_default(sens, &mock_node,
								    "dt_prop_test", &val,
								    mock_default),
			0);
	KUNIT_EXPECT_EQ(test, val, tmu_test_data->mock_dt_prop);
	// success return default
	tmu_test_data->read_dt_prop_ret = -1;
	KUNIT_EXPECT_EQ(test,
			__thermal_tmu_read_dt_property_with_default(sens, &mock_node,
								    "dt_prop_test", &val,
								    mock_default),
			0);
	KUNIT_EXPECT_EQ(test, val, mock_default);

	// NULL input
	KUNIT_EXPECT_EQ(test,
			__thermal_tmu_read_dt_property_with_default(NULL, &mock_node,
								    "dt_prop_test", &val,
								    mock_default),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			__thermal_tmu_read_dt_property_with_default(sens, NULL,
								    "dt_prop_test", &val,
								    mock_default),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			__thermal_tmu_read_dt_property_with_default(sens, &mock_node, NULL, &val,
								    mock_default),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			__thermal_tmu_read_dt_property_with_default(sens, &mock_node,
								    "dt_prop_test", NULL,
								    mock_default),
			-EINVAL);
}

static void thermal_tmu_gov_dt_parse_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu_data = sensor->sens_priv_data;
	struct device_node mock_np;

	// success
	tmu_test_data->mock_dt_prop = 20;
	// gradual
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_gradual_param_dt(sensor, &mock_np), 0);
	KUNIT_EXPECT_EQ(test, tmu_data->gradual_param.irq_gain, tmu_test_data->mock_dt_prop);
	KUNIT_EXPECT_EQ(test, tmu_data->gradual_param.timer_gain, tmu_test_data->mock_dt_prop);
	// pi
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_pi_param_dt(sensor, &mock_np), 0);
	KUNIT_EXPECT_FALSE(test, test_bit(TMU_GOV_PI_SELECT, &tmu_data->gov_select));
	KUNIT_EXPECT_EQ(test, tmu_data->pi_param.k_po, tmu_test_data->mock_dt_prop);
	KUNIT_EXPECT_EQ(test, tmu_data->pi_param.k_pu, tmu_test_data->mock_dt_prop);
	KUNIT_EXPECT_EQ(test, tmu_data->pi_param.k_i, tmu_test_data->mock_dt_prop);
	// early throttle
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_early_throttle_param_dt(sensor, &mock_np), 0);
	KUNIT_EXPECT_FALSE(test, test_bit(TMU_GOV_EARLY_THROTTLE_SELECT, &tmu_data->gov_select));
	KUNIT_EXPECT_EQ(test, tmu_data->early_throttle_param.k_p, tmu_test_data->mock_dt_prop);
	KUNIT_EXPECT_EQ(test, tmu_data->early_throttle_param.offset, tmu_test_data->mock_dt_prop);

	// NULL input
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_gradual_param_dt(NULL, &mock_np), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_gradual_param_dt(sensor, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_pi_param_dt(NULL, &mock_np), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_pi_param_dt(sensor, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_early_throttle_param_dt(NULL, &mock_np), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_parse_early_throttle_param_dt(sensor, NULL), -EINVAL);

	// TODO: b/411443456 add missing device tree properties in TMU driver
}

static void thermal_tmu_gov_param_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct device *dev = tmu_test_data->fake_dev;
	struct tmu_sensor_data *tmu = tmu_test_data->mock_tmu_data;
	u8 mock_tz_id = 1;

	// success
	tmu->gradual_param.irq_gain = 1;
	tmu->gradual_param.timer_gain = 2;
	tmu->pi_param.k_po = 50;
	tmu->pi_param.k_pu = 60;
	tmu->pi_param.k_i = 80;
	tmu->early_throttle_param.k_p = 40;
	tmu_test_data->set_gov_param_ret = 0;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_gradual_init(dev, mock_tz_id, tmu), 0);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.gradual_param.irq_gain,
			tmu->gradual_param.irq_gain);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.gradual_param.timer_gain,
			tmu->gradual_param.timer_gain);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_pi_init(dev, mock_tz_id, tmu), 0);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.pi_param.k_po, tmu->pi_param.k_po);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.pi_param.k_pu, tmu->pi_param.k_pu);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.pi_param.k_i, tmu->pi_param.k_i);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_early_throttle_init(dev, mock_tz_id, tmu), 0);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.early_throttle_param.k_p,
			tmu->early_throttle_param.k_p);

	// error thermal msg ret
	tmu_test_data->set_gov_param_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_gradual_init(dev, mock_tz_id, tmu), -ENODEV);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_pi_init(dev, mock_tz_id, tmu), -ENODEV);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_early_throttle_init(dev, mock_tz_id, tmu), -ENODEV);
}

static enum tmu_trip_type to_tmu_trip_type(enum thermal_trip_type thermal_trip_type)
{
	switch (thermal_trip_type) {
	case THERMAL_TRIP_ACTIVE:
		return TMU_TRIP_TYPE_ACTIVE;
	case THERMAL_TRIP_PASSIVE:
		return TMU_TRIP_TYPE_PASSIVE;
	case THERMAL_TRIP_HOT:
		return TMU_TRIP_TYPE_HOT;
	case THERMAL_TRIP_CRITICAL:
		return TMU_TRIP_TYPE_CRITICAL;
	default:
		return TMU_TRIP_TYPE_MAX;
	}
}

static void thermal_tmu_trip_points_init_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;

	// success
	KUNIT_EXPECT_EQ(test, __thermal_tmu_trip_points_init(sensor), 0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i) {
		KUNIT_EXPECT_EQ(test, tmu_test_data->trip_temp_set_values[i] * MILLI,
				mock_trips[i].temperature);
		KUNIT_EXPECT_EQ(test, tmu_test_data->trip_hyst_set_values[i] * MILLI,
				mock_trips[i].hysteresis);
		KUNIT_EXPECT_EQ(test, tmu_test_data->trip_type_set_values[i],
				to_tmu_trip_type(mock_trips[i].type));
	}

	// NULL input
	KUNIT_EXPECT_EQ(test, __thermal_tmu_trip_points_init(NULL), -EINVAL);

	// error thermal msg ret
	tmu_test_data->set_trip_hyst_ret = 0;
	tmu_test_data->set_trip_temp_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_trip_points_init(sensor), -ENODEV);
	tmu_test_data->set_trip_temp_ret = 0;
	tmu_test_data->set_trip_hyst_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_trip_points_init(sensor), -ENODEV);
	tmu_test_data->set_trip_hyst_ret = 0;
	tmu_test_data->set_trip_type_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_trip_points_init(sensor), -ENODEV);
}

static void thermal_tmu_set_polling_delay_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu = sensor->sens_priv_data;

	// success
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_polling_delay_ms(sensor, 25), 0);
	KUNIT_EXPECT_EQ(test, tmu->polling_delay_ms, 25);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.polling_delay_ms, 25);

	// NULL sensor
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_polling_delay_ms(NULL, 25), -EINVAL);
	// Out of range delay_ms
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_polling_delay_ms(sensor, U16_MAX + 10), 0);
	KUNIT_EXPECT_EQ(test, tmu->polling_delay_ms, U16_MAX);
	KUNIT_EXPECT_EQ(test, tmu_test_data->tmu_set_data.polling_delay_ms, U16_MAX);
	// error thermal msg ret
	tmu_test_data->set_polling_delay_ret = -ENODEV;
	tmu->polling_delay_ms = 25;
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_polling_delay_ms(sensor, 50), -ENODEV);
	KUNIT_ASSERT_NE(test, tmu->polling_delay_ms, 50);
}

static void thermal_tmu_send_init_msg_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu = sensor->sens_priv_data;

	// success
	tmu->gradual_param.irq_gain = 1;
	tmu->gradual_param.timer_gain = 2;
	tmu->pi_param.k_po = 50;
	tmu->pi_param.k_pu = 60;
	tmu->pi_param.k_i = 80;
	tmu->early_throttle_param.k_p = 40;
	tmu->polling_delay_ms = 25;
	tmu->gov_select = 0xAA;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_send_init_msg(sensor), 0);
	KUNIT_EXPECT_EQ(test, tmu_test_data->mock_gov_select, 0xAA);

	// NULL sensor pointer
	KUNIT_EXPECT_EQ(test, __thermal_tmu_send_init_msg(NULL), -EINVAL);
	// error thermal msg ret
	tmu_test_data->set_gov_select_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_send_init_msg(sensor), -ENODEV);
}

static void thermal_tmu_configure_sensor_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu;

	// success
	KUNIT_EXPECT_EQ(test, thermal_tmu_configure_sensor(sensor), 0);
	tmu = sensor->sens_priv_data;
	KUNIT_EXPECT_TRUE(test, test_bit(TMU_GOV_GRADUAL_SELECT, &tmu->gov_select));

	// NULL sensor
	KUNIT_EXPECT_EQ(test, thermal_tmu_configure_sensor(NULL), -EINVAL);

	// error ret codes
	tmu_test_data->parse_dt_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_tmu_configure_sensor(sensor), -EINVAL);

	devm_kfree(sensor->dev, sensor->sens_priv_data);
	sensor->sens_priv_data = tmu_test_data->mock_tmu_data;
}

static void thermal_tmu_get_temp_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct thermal_zone_device *tzd = tmu_test_data->fake_tzd;
	int temp;

	// success
	tmu_test_data->mock_tmu_temp = 65;
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_temp(tzd, &temp), 0);
	KUNIT_EXPECT_EQ(test, temp, 65 * MILLI);

	// error thermal msg ret
	tmu_test_data->get_temp_ret = -EIO;
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_temp(tzd, &temp), -EIO);
	// test invalid thermal temperature
	tmu_test_data->get_temp_ret = -ENODATA;
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_temp(tzd, &temp), 0);
	KUNIT_EXPECT_EQ(test, temp, THERMAL_TEMP_INVALID);
}

struct thermal_tmu_set_trip_test_walk_data {
	struct kunit *test;
	int index;
};

static int thermal_tmu_set_trip_test_walk_cb(struct thermal_trip *trip, void *data)
{
	struct thermal_tmu_set_trip_test_walk_data * const twd = data;
	struct kunit *test = twd->test;
	struct tmu_test_data *tmu_test_data = test->priv;
	struct thermal_zone_device *tzd = tmu_test_data->fake_tzd;
	int trip_idx = twd->index;
	int mock_trip_temp = 95000;

	tmu_test_data->set_trip_temp_ret = 0;
	// success
	KUNIT_EXPECT_EQ_MSG(test,
			    __thermal_tmu_set_trip_temp(tzd, trip, mock_trip_temp),
			    0, "set_trip_temp: idx=%d", trip_idx);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i) {
		int valid_temp;

		if (i == trip_idx) {
			valid_temp = mock_trip_temp / MILLI;
		} else {
			valid_temp = mock_trips[i].temperature / MILLI;
		}

		KUNIT_EXPECT_EQ_MSG(test, tmu_test_data->trip_temp_set_values[i],
				    valid_temp, "validate trip temp %d of test idx=%d",
				    i, trip_idx);
	}

	// error thermal msg ret
	tmu_test_data->set_trip_temp_ret = -ENODEV;
	KUNIT_EXPECT_EQ_MSG(test,
			    __thermal_tmu_set_trip_temp(tzd, trip, mock_trip_temp),
			    -ENODEV, "set_trip_temp: idx=%d: thermal msg err", trip_idx);

	twd->index++;
	return 0;
}

static void thermal_tmu_set_trip_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct thermal_zone_device *tzd = tmu_test_data->fake_tzd;
	struct thermal_tmu_set_trip_test_walk_data twd = {
		.test = test,
	};

	thermal_zone_for_each_trip(tzd, thermal_tmu_set_trip_test_walk_cb, &twd);

	// TODO: b/401000969 and b/411443456 unit test coverage check on for_each_trip
}

static void thermal_tmu_get_polling_delay_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu = sensor->sens_priv_data;
	int val;

	// success
	tmu->polling_delay_ms = 60;
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_polling_delay_ms(sensor, &val), 0);
	KUNIT_EXPECT_EQ(test, val, 60);

	// NULL inputs
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_polling_delay_ms(NULL, &val), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_polling_delay_ms(sensor, NULL), -EINVAL);
}

enum debugfs_test_type {
	TMU_TEST_GOV_PARAM,
	TMU_TEST_GOV_SELECT,
	TMU_TEST_POLLING_DELAY,
};

struct debugfs_test_case {
	int (*setter)(void *data, u64 val);
	int (*getter)(void *data, u64 *val);
	enum debugfs_test_type type;
	size_t verify_val_offset;
	int bitmask;
	const char *msg;
};

#define DEBUGFS_ROOT "tmu"
#define PARAM_OFFSETOF(__gov, __param)  \
		(offsetof(struct tmu_sensor_data, __gov##_param) +     \
		offsetof(struct tmu_##__gov##_param, __param))


struct debugfs_test_case debugfs_test_case[] = {
	{
		.setter = thermal_tmu_debugfs_gradual_irq_gain_set,
		.getter = thermal_tmu_debugfs_gradual_irq_gain_get,
		.type = TMU_TEST_GOV_PARAM,
		.verify_val_offset = PARAM_OFFSETOF(gradual, irq_gain),
		.msg = "gradual irq_gain test",
	},
	{
		.setter = thermal_tmu_debugfs_gradual_timer_gain_set,
		.getter = thermal_tmu_debugfs_gradual_timer_gain_get,
		.type = TMU_TEST_GOV_PARAM,
		.verify_val_offset = PARAM_OFFSETOF(gradual, timer_gain),
		.msg = "gradual timer_gain test",
	},
	{
		.setter = thermal_tmu_debugfs_pi_k_po_set,
		.getter = thermal_tmu_debugfs_pi_k_po_get,
		.type = TMU_TEST_GOV_PARAM,
		.verify_val_offset = PARAM_OFFSETOF(pi, k_po),
		.msg = "pi k_po test",
	},
	{
		.setter = thermal_tmu_debugfs_pi_k_pu_set,
		.getter = thermal_tmu_debugfs_pi_k_pu_get,
		.type = TMU_TEST_GOV_PARAM,
		.verify_val_offset = PARAM_OFFSETOF(pi, k_pu),
		.msg = "pi k_pu test",
	},
	{
		.setter = thermal_tmu_debugfs_pi_k_i_set,
		.getter = thermal_tmu_debugfs_pi_k_i_get,
		.type = TMU_TEST_GOV_PARAM,
		.verify_val_offset = PARAM_OFFSETOF(pi, k_i),
		.msg = "pi k_i test",
	},
	{
		.setter = thermal_tmu_debugfs_early_throttle_k_p_set,
		.getter = thermal_tmu_debugfs_early_throttle_k_p_get,
		.type = TMU_TEST_GOV_PARAM,
		.verify_val_offset = PARAM_OFFSETOF(early_throttle, k_p),
		.msg = "early throttle k_p test",
	},
	{
		.setter = thermal_tmu_debugfs_gradual_select_set,
		.getter = thermal_tmu_debugfs_gradual_select_get,
		.type = TMU_TEST_GOV_SELECT,
		.bitmask = TMU_GOV_GRADUAL_SELECT,
		.msg = "gov gradual select test",
	},
	{
		.setter = thermal_tmu_debugfs_pi_select_set,
		.getter = thermal_tmu_debugfs_pi_select_get,
		.type = TMU_TEST_GOV_SELECT,
		.bitmask = TMU_GOV_PI_SELECT,
		.msg = "gov pi select test",
	},
	{
		.setter = thermal_tmu_debugfs_temp_lut_select_set,
		.getter = thermal_tmu_debugfs_temp_lut_select_get,
		.type = TMU_TEST_GOV_SELECT,
		.bitmask = TMU_GOV_TEMP_LUT_SELECT,
		.msg = "gov temp_lut select test",
	},
	{
		.setter = thermal_tmu_debugfs_hardlimit_via_pid_select_set,
		.getter = thermal_tmu_debugfs_hardlimit_via_pid_select_get,
		.type = TMU_TEST_GOV_SELECT,
		.bitmask = TMU_GOV_HARDLIMIT_VIA_PID_SELECT,
		.msg = "gov hardlimit_via_pid select test",
	},
	{
		.setter = thermal_tmu_debugfs_early_throttle_select_set,
		.getter = thermal_tmu_debugfs_early_throttle_select_get,
		.type = TMU_TEST_GOV_SELECT,
		.bitmask = TMU_GOV_EARLY_THROTTLE_SELECT,
		.msg = "gov early_throttle select test",
	},
	{
		.setter = thermal_tmu_debugfs_polling_delay_ms_set,
		.getter = thermal_tmu_debugfs_polling_delay_ms_get,
		.type = TMU_TEST_POLLING_DELAY,
		.verify_val_offset = offsetof(struct tmu_sensor_data, polling_delay_ms),
		.msg = "polling_delay_ms test",
	},
};

static void thermal_tmu_debugfs_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu = sensor->sens_priv_data;

	// Debugfs Init: invalid argument
	KUNIT_EXPECT_EQ(test, thermal_tmu_debugfs_init(NULL), -EINVAL);
	// Debugfs Init: success
	KUNIT_EXPECT_EQ(test, thermal_tmu_debugfs_init(sensor->dev), 0);

	// Debugfs sensor init: invalid argument
	KUNIT_EXPECT_EQ(test, thermal_tmu_debugfs_sensor_init(NULL), -EINVAL);
	// Debugfs sensor init: success
	KUNIT_EXPECT_EQ(test, thermal_tmu_debugfs_sensor_init(sensor), 0);

	// test file nodes
	for (int i = 0; i < ARRAY_SIZE(debugfs_test_case); ++i) {
		u64 mock_success_val_set = 1, mock_failure_val_set = 0, mock_val_get;
		s32 verify_val;
		bool test_gov_select;
		int *comm_ret;

		switch (debugfs_test_case[i].type) {
		case TMU_TEST_GOV_PARAM:
			comm_ret = &tmu_test_data->set_gov_param_ret;
			break;
		case TMU_TEST_GOV_SELECT:
			comm_ret = &tmu_test_data->set_gov_select_ret;
			break;
		case TMU_TEST_POLLING_DELAY:
			comm_ret = &tmu_test_data->set_polling_delay_ret;
		}

		// setter success
		*comm_ret = 0;
		KUNIT_EXPECT_EQ_MSG(test, debugfs_test_case[i].setter(sensor, mock_success_val_set),
				    0, debugfs_test_case[i].msg);
		switch (debugfs_test_case[i].type) {
		case TMU_TEST_GOV_PARAM:
		case TMU_TEST_POLLING_DELAY:
			verify_val = *(s32 *)((char *)tmu + debugfs_test_case[i].verify_val_offset);
			KUNIT_EXPECT_EQ_MSG(test, verify_val, mock_success_val_set,
					    debugfs_test_case[i].msg);
			break;
		case TMU_TEST_GOV_SELECT:
			test_gov_select = test_bit(debugfs_test_case[i].bitmask, &tmu->gov_select);
			KUNIT_EXPECT_TRUE_MSG(test, test_gov_select, debugfs_test_case[i].msg);
			break;
		default:
			break;
		}

		// setter comm failure
		*comm_ret = -EINVAL;
		KUNIT_EXPECT_EQ_MSG(test, debugfs_test_case[i].setter(sensor, mock_failure_val_set),
				    -EINVAL, debugfs_test_case[i].msg);
		switch (debugfs_test_case[i].type) {
		case TMU_TEST_GOV_PARAM:
		case TMU_TEST_POLLING_DELAY:
			verify_val = *(s32 *)((char *)tmu + debugfs_test_case[i].verify_val_offset);
			KUNIT_EXPECT_EQ_MSG(test, verify_val, mock_success_val_set,
					    debugfs_test_case[i].msg);
			break;
		case TMU_TEST_GOV_SELECT:
			test_gov_select = test_bit(debugfs_test_case[i].bitmask, &tmu->gov_select);
			KUNIT_EXPECT_TRUE_MSG(test, test_gov_select, debugfs_test_case[i].msg);
			break;
		default:
			break;
		}

		// getter
		KUNIT_EXPECT_EQ_MSG(test, debugfs_test_case[i].getter(sensor, &mock_val_get), 0,
				    debugfs_test_case[i].msg);
		switch (debugfs_test_case[i].type) {
		case TMU_TEST_GOV_PARAM:
		case TMU_TEST_POLLING_DELAY:
			verify_val = *(s32 *)((char *)tmu + debugfs_test_case[i].verify_val_offset);
			KUNIT_EXPECT_EQ_MSG(test, verify_val, mock_val_get,
					    debugfs_test_case[i].msg);
			break;
		case TMU_TEST_GOV_SELECT:
			test_gov_select = test_bit(debugfs_test_case[i].bitmask, &tmu->gov_select);
			KUNIT_EXPECT_TRUE_MSG(test, test_gov_select, debugfs_test_case[i].msg);
			break;
		default:
			break;
		}

		// NULL tmu failure
		sensor->sens_priv_data = NULL;
		KUNIT_EXPECT_EQ_MSG(test, debugfs_test_case[i].setter(sensor, mock_success_val_set),
				    -EINVAL, debugfs_test_case[i].msg);
		KUNIT_EXPECT_EQ_MSG(test, debugfs_test_case[i].getter(sensor, &mock_val_get),
				    -EINVAL, debugfs_test_case[i].msg);
		sensor->sens_priv_data = (void *)tmu;
	}

	// Debugfs cleanup
	thermal_tmu_debugfs_cleanup();
	struct dentry *valid_debugfs_root = debugfs_lookup(DEBUGFS_ROOT, NULL);

	KUNIT_EXPECT_NULL(test, valid_debugfs_root);
}

static void thermal_tmu_offset_enable_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct thermal_zone_device *tzd = tmu_test_data->fake_tzd;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu = sensor->sens_priv_data;
	struct list_head mock_list_head;
	int mock_offset_base = 2000, mock_offsets[THERMAL_TMU_NR_TRIPS],
	    valid_offsets[THERMAL_TMU_NR_TRIPS] = {0};

	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i) {
		mock_offsets[i] = (i + 1) * mock_offset_base;
		__thermal_tmu_set_trip_temp(tzd, &tzd->trips[i].trip, mock_trips[i].temperature);
	}

	// invalid thermal sensor list
	KUNIT_EXPECT_EQ(test, __thermal_tmu_offset_enable(true), -EINVAL);

	INIT_LIST_HEAD(&mock_list_head);
	list_add_tail(&sensor->node, &mock_list_head);
	thermal_tmu_post_configure(&mock_list_head);

	// set_offset with all zeros: success
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_set_junction_offset(sensor, valid_offsets,
							THERMAL_TMU_NR_TRIPS), 0);
	KUNIT_EXPECT_TRUE(test, tmu->skip_junction_offset);

	// offset_enable with skip_junction_offset as true: success
	KUNIT_EXPECT_EQ(test, __thermal_tmu_offset_enable(true), 0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i) {
		KUNIT_EXPECT_EQ(test, 0, tmu->junction_offset[i]);
		KUNIT_EXPECT_EQ(test, mock_trips[i].temperature,
				tmu_test_data->trip_temp_set_values[i] * MILLI);
	}

	KUNIT_EXPECT_EQ(test, __thermal_tmu_offset_enable(false), 0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		KUNIT_EXPECT_EQ(test, mock_trips[i].temperature,
				tmu_test_data->trip_temp_set_values[i] * MILLI);

	// set_offset: success
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_set_junction_offset(sensor, mock_offsets, THERMAL_TMU_NR_TRIPS),
			0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		KUNIT_EXPECT_EQ(test, mock_offsets[i], tmu->junction_offset[i]);
	KUNIT_EXPECT_FALSE(test, tmu->skip_junction_offset);

	// get_offset: success
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_get_junction_offset(sensor, valid_offsets),
			0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		KUNIT_EXPECT_EQ(test, valid_offsets[i], tmu->junction_offset[i]);

	// set_offset: invalid argument
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_set_junction_offset(NULL, mock_offsets, THERMAL_TMU_NR_TRIPS),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_set_junction_offset(sensor, NULL, THERMAL_TMU_NR_TRIPS),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_set_junction_offset(sensor, mock_offsets,
							THERMAL_TMU_NR_TRIPS + 1),
			-EINVAL);
	// get_offset: invalid argument
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_get_junction_offset(NULL, valid_offsets),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_tmu_get_junction_offset(sensor, NULL),
			-EINVAL);

	// offset_enable: success
	KUNIT_EXPECT_EQ(test, __thermal_tmu_offset_enable(true), 0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i) {
		int valid_temp = (mock_trips[i].temperature + tmu->junction_offset[i]) / MILLI;

		KUNIT_EXPECT_EQ(test, valid_temp, tmu_test_data->trip_temp_set_values[i]);
	}

	KUNIT_EXPECT_EQ(test, __thermal_tmu_offset_enable(false), 0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i) {
		int valid_temp = mock_trips[i].temperature / MILLI;

		KUNIT_EXPECT_EQ(test, valid_temp, tmu_test_data->trip_temp_set_values[i]);
	}

	// offset_enable: set trip temp error
	tmu_test_data->set_trip_temp_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_offset_enable(true), -EINVAL);
}

static void thermal_tmu_get_trip_counter_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct device *dev = tmu_test_data->fake_dev;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu_data = sensor->sens_priv_data;
	struct device_attribute *attr = &tmu_data->dev_attr_trip_counter;
	struct thermal_sm_trip_counter_data trip_counter_read;
	struct list_head mock_list_head;

	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		tmu_test_data->trip_counter_values.counters[i] = get_random_u64();

	// init failure: sm init error
	tmu_test_data->sm_init_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_trip_counter_init(dev, sensor), -ENODEV);

	INIT_LIST_HEAD(&mock_list_head);
	list_add_tail(&sensor->node, &mock_list_head);
	thermal_tmu_post_configure(&mock_list_head);

	// get_trip_counter: success
	KUNIT_EXPECT_EQ(test, __thermal_tmu_get_trip_counter(attr, &trip_counter_read), 0);
	for (int i = 0; i < THERMAL_TMU_NR_TRIPS; ++i)
		KUNIT_EXPECT_EQ(test, trip_counter_read.counters[i],
				tmu_test_data->trip_counter_values.counters[i]);
	// attr not found
	KUNIT_EXPECT_EQ(test, __thermal_tmu_get_trip_counter(NULL, &trip_counter_read), -ENODEV);
	// msg error
	tmu_test_data->get_trip_counter_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_get_trip_counter(attr, &trip_counter_read), -EIO);
	//  error
	tmu_test_data->get_trip_counter_ret = 0;
	tmu_test_data->sm_read_trip_counter_ret = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test,
			__thermal_tmu_get_trip_counter(attr, &trip_counter_read),
			-EOPNOTSUPP);
}

/**
 * thermal_tmu_temp_lut_test
 * Test cases for the following functions:
 *	__thermal_tmu_gov_temp_lut_init
 *	thermal_tmu_get_temp_state_table
 */
static void thermal_tmu_temp_lut_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct device *dev = tmu_test_data->fake_dev;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu = sensor->sens_priv_data;
	struct tmu_temp_state_st mock_temp_state_table[10];
	int mock_table_len;

	/* test for NULL parameters */
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_temp_lut_init(NULL, 0, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_temp_lut_init(NULL, 0, tmu), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_temp_lut_init(dev, 0, NULL), -EINVAL);

	/* test for empty temp state table */
	tmu->temp_state_table_len = 0;
	tmu->temp_state_table = NULL;
	set_bit(TMU_GOV_TEMP_LUT_SELECT, &tmu->gov_select);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_temp_lut_init(dev, 0, tmu), 0);
	KUNIT_EXPECT_TRUE(test, test_bit(TMU_GOV_TEMP_LUT_SELECT, &tmu->gov_select));
	clear_bit(TMU_GOV_TEMP_LUT_SELECT, &tmu->gov_select);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_temp_lut_init(dev, 0, tmu), 0);
	KUNIT_EXPECT_FALSE(test, test_bit(TMU_GOV_TEMP_LUT_SELECT, &tmu->gov_select));

	/* test for a valid temp state table */
	tmu->temp_state_table_len = 2;
	/* tmu->temp_state_table is devm_kfreed after calling
	 * __thermal_tmu_gov_temp_lut_init and thermal_tmu_set_temp_state_table
	 */
	tmu->temp_state_table = devm_kcalloc(dev,
		tmu->temp_state_table_len, sizeof(*tmu->temp_state_table), GFP_KERNEL);
	if (!tmu->temp_state_table) {
		KUNIT_FAIL(test, "Failed to allocate tmu->temp_state_table");
		return;
	}

	tmu->temp_state_table[0].temp = 85;
	tmu->temp_state_table[0].state = 0;
	tmu->temp_state_table[1].temp = 255;
	tmu->temp_state_table[1].state = 1;
	KUNIT_EXPECT_EQ(test, __thermal_tmu_gov_temp_lut_init(dev, 0, tmu), 0);
	KUNIT_EXPECT_EQ(test, tmu->temp_state_table_len, 0);
	KUNIT_EXPECT_NULL(test, tmu->temp_state_table);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table_len, 2);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[0].temp, 85);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[0].state, 0);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[1].temp, 255);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[1].state, 1);

	/* test for NULL parameters */
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_temp_state_table(NULL, NULL, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_tmu_get_temp_state_table(sensor, NULL, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test,
		thermal_tmu_get_temp_state_table(sensor, &mock_temp_state_table[0], NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test,
		thermal_tmu_get_temp_state_table(sensor, NULL, &mock_table_len), -EINVAL);

	/* test for a valid table*/
	memset(mock_temp_state_table, 0, sizeof(mock_temp_state_table));
	mock_table_len = 0;
	tmu_test_data->set_temp_state_table_len = 2;
	tmu_test_data->set_temp_state_table[0].temp = 85;
	tmu_test_data->set_temp_state_table[0].state = 0;
	tmu_test_data->set_temp_state_table[1].temp = 255;
	tmu_test_data->set_temp_state_table[1].state = 1;
	KUNIT_EXPECT_EQ(test,
		thermal_tmu_get_temp_state_table(sensor,
			&mock_temp_state_table[0], &mock_table_len), 0);
	KUNIT_EXPECT_EQ(test, mock_table_len, 2);
	KUNIT_EXPECT_EQ(test, mock_temp_state_table[0].temp, 85);
	KUNIT_EXPECT_EQ(test, mock_temp_state_table[0].state, 0);
	KUNIT_EXPECT_EQ(test, mock_temp_state_table[1].temp, 255);
	KUNIT_EXPECT_EQ(test, mock_temp_state_table[1].state, 1);
	KUNIT_EXPECT_EQ(test, tmu->temp_state_table_len, 0);
	KUNIT_EXPECT_NULL(test, tmu->temp_state_table);
}

/**
 * thermal_tmu_temp_lut_debugfs_test
 * Test cases for the following functions:
 *	thermal_tmu_set_temp_state_table
 */
static void thermal_tmu_temp_lut_debugfs_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	struct tmu_sensor_data *tmu = sensor->sens_priv_data;
	struct tmu_temp_state_st mock_temp_state_table[10];

	mock_temp_state_table[0].temp = 90;
	mock_temp_state_table[0].state = 0;
	mock_temp_state_table[1].temp = 125;
	mock_temp_state_table[1].state = 1;
	/* test for NULL parameters */
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_temp_state_table(NULL, NULL, 0), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_temp_state_table(sensor, NULL, 0), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_temp_state_table(
		NULL, &mock_temp_state_table[0], 0), -EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_temp_state_table(
		sensor, &mock_temp_state_table[0], THERMAL_TEMP_LUT_MAX_ENTRIES+1), -EINVAL);

	/* test for a valid table*/
	KUNIT_EXPECT_EQ(test, thermal_tmu_set_temp_state_table(
		sensor, &mock_temp_state_table[0], 2), 0);
	KUNIT_EXPECT_EQ(test, tmu->temp_state_table_len, 0);
	KUNIT_EXPECT_NULL(test, tmu->temp_state_table);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table_len, 2);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[0].temp, 90);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[0].state, 0);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[1].temp, 125);
	KUNIT_EXPECT_EQ(test, tmu_test_data->set_temp_state_table[1].state, 1);
}

struct debugfs_temp_lut_test_case {
	const char *msg;
	const char *input;
	const char *output;
	int setter_ret;
	uint8_t table_len;
	uint8_t temp_table[THERMAL_TEMP_LUT_MAX_ENTRIES];
	uint8_t state_table[THERMAL_TEMP_LUT_MAX_ENTRIES];
};

static const struct debugfs_temp_lut_test_case debugfs_temp_lut_test_case[] = {
	{
		.msg = "dbgfs lut: incomplete table",
		.input = "70 0 80",
		.setter_ret = -EINVAL,
	},
	{
		.msg = "dbgfs lut: non-decimal",
		.input = "abc def 80 2",
		.setter_ret = -EINVAL,
	},
	{
		.msg = "dbgfs lut: out of uint8_t range",
		.input = "70 0 256 2",
		.setter_ret = -ERANGE,
	},
	{
		.msg = "dbgfs lut: valid table of len 6",
		.input = "70 0 80 2 90 4 100 6 110 8 255 10",
		.output = "70 0\n80 2\n90 4\n100 6\n110 8\n255 10\n",
		.setter_ret = 33,
		.table_len = 6,
		.temp_table = {70, 80, 90, 100, 110, 255},
		.state_table = {0, 2, 4, 6, 8, 10},
	},
	{
		.msg = "dbgfs lut: valid table of len 2",
		.input = "85 0 255 3",
		.output = "85 0\n255 3\n",
		.setter_ret = 10,
		.table_len = 2,
		.temp_table = {85, 255},
		.state_table = {0, 3},
	},
	{
		.msg = "dbgfs lut: valid table of max len 24",
		.input = "10 0 20 1 30 2 40 3 50 4 60 5 70 6 80 7 90 8 "
			"100 9 110 10 120 11 130 12 140 13 150 14 160 15 170 16 "
			"180 17 190 18 200 19 210 20 220 21 230 22 240 23",
		.output = "10 0\n20 1\n30 2\n40 3\n50 4\n60 5\n70 6\n80 7\n90 8\n"
			"100 9\n110 10\n120 11\n130 12\n140 13\n150 14\n160 15\n170 16\n"
			"180 17\n190 18\n200 19\n210 20\n220 21\n230 22\n240 23\n",
		.setter_ret = 148,
		.table_len = 24,
		.temp_table = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130, 140,
			150, 160, 170, 180, 190, 200, 210, 220, 230, 240},
		.state_table = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
			17, 18, 19, 20, 21, 22, 23},
	},
};

/**
 * thermal_tmu_debugfs_temp_state_table_test
 * Test cases for the following functions:
 *	__thermal_tmu_debugfs_temp_state_table_set
 *	__thermal_tmu_debugfs_temp_state_table_get
 */
static void thermal_tmu_debugfs_temp_state_table_test(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data = test->priv;
	struct google_sensor_data *sensor = tmu_test_data->mock_sens_data;
	char *write_buf, *read_buf;
	ssize_t write_len = 0, read_len;

	write_buf = kunit_kzalloc(test,
			TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, write_buf);
	read_buf = kunit_kzalloc(test,
			TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, read_buf);

	/* test for NULL parameters */
	KUNIT_EXPECT_EQ(test, __thermal_tmu_debugfs_temp_state_table_set(
				NULL, write_buf, write_len), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_debugfs_temp_state_table_set(
				sensor, NULL, write_len), -EINVAL);

	for (int i = 0; i < ARRAY_SIZE(debugfs_temp_lut_test_case); ++i) {
		write_len = scnprintf(write_buf, TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE,
				debugfs_temp_lut_test_case[i].input);
		/* test for setting the table */
		read_len = __thermal_tmu_debugfs_temp_state_table_set(
				sensor, write_buf, write_len);
		KUNIT_EXPECT_EQ_MSG(test, read_len, debugfs_temp_lut_test_case[i].setter_ret,
				debugfs_temp_lut_test_case[i].msg);
		if (debugfs_temp_lut_test_case[i].setter_ret <= 0)
			continue;

		KUNIT_EXPECT_EQ_MSG(test, read_len, write_len,
				debugfs_temp_lut_test_case[i].msg);
		KUNIT_EXPECT_EQ_MSG(test, tmu_test_data->set_temp_state_table_len,
				debugfs_temp_lut_test_case[i].table_len,
				debugfs_temp_lut_test_case[i].msg);
		for (int j = 0; j < debugfs_temp_lut_test_case[i].table_len; ++j) {
			KUNIT_EXPECT_EQ_MSG(test,
					tmu_test_data->set_temp_state_table[j].temp,
					debugfs_temp_lut_test_case[i].temp_table[j],
					debugfs_temp_lut_test_case[i].msg);
			KUNIT_EXPECT_EQ_MSG(test,
					tmu_test_data->set_temp_state_table[j].state,
					debugfs_temp_lut_test_case[i].state_table[j],
					debugfs_temp_lut_test_case[i].msg);
		}

		/* test for getting the table back */
		memset(read_buf, 0, TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE);
		read_len = __thermal_tmu_debugfs_temp_state_table_get(
				sensor, read_buf, TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE);
		KUNIT_EXPECT_EQ_MSG(test, read_len,
				debugfs_temp_lut_test_case[i].setter_ret + 1,
				debugfs_temp_lut_test_case[i].msg);
		KUNIT_EXPECT_STREQ_MSG(test, read_buf,
				debugfs_temp_lut_test_case[i].output,
				debugfs_temp_lut_test_case[i].msg);
	}

	/* test for getting an empty table */
	tmu_test_data->set_temp_state_table_len = 0;
	memset(read_buf, 0, TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE);
	read_len = __thermal_tmu_debugfs_temp_state_table_get(
			sensor, read_buf, TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE);
	KUNIT_ASSERT_EQ(test, read_len, 4);
	KUNIT_EXPECT_STREQ(test, read_buf, "n/a\n");

	/* test for invalid param */
	KUNIT_EXPECT_EQ(test, __thermal_tmu_debugfs_temp_state_table_get(NULL, read_buf,
				TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_debugfs_temp_state_table_get(sensor, NULL,
				TMU_DEBUGFS_TEMP_STATE_TABLE_BUF_SIZE), -EINVAL);
	KUNIT_EXPECT_EQ(test, __thermal_tmu_debugfs_temp_state_table_get(sensor,
				read_buf, 0), -EINVAL);
}

static struct kunit_case thermal_tmu_helper_test[] = {
	KUNIT_CASE(thermal_tmu_read_dt_property_with_default_test),
	KUNIT_CASE(thermal_tmu_gov_dt_parse_test),
	KUNIT_CASE(thermal_tmu_gov_param_test),
	KUNIT_CASE(thermal_tmu_trip_points_init_test),
	KUNIT_CASE(thermal_tmu_send_init_msg_test),
	KUNIT_CASE(thermal_tmu_configure_sensor_test),
	KUNIT_CASE(thermal_tmu_get_temp_test),
	KUNIT_CASE(thermal_tmu_set_trip_test),
	KUNIT_CASE(thermal_tmu_set_polling_delay_test),
	KUNIT_CASE(thermal_tmu_get_polling_delay_test),
#if IS_ENABLED(CONFIG_DEBUG_FS)
	KUNIT_CASE(thermal_tmu_debugfs_test),
	KUNIT_CASE(thermal_tmu_temp_lut_debugfs_test),
	KUNIT_CASE(thermal_tmu_debugfs_temp_state_table_test),
#endif /* CONFIG_DEBUG_FS */
	KUNIT_CASE(thermal_tmu_offset_enable_test),
	KUNIT_CASE(thermal_tmu_get_trip_counter_test),
	KUNIT_CASE(thermal_tmu_temp_lut_test),
	{},
};

static int mock_tzd_get_temp(struct thermal_zone_device *tzd, int *temp)
{
	return 0;
}

static int mock_tzd_set_trip_val(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
				 int val)
{
	return 0;
}

static int thermal_tmu_test_init(struct kunit *test)
{
	struct tmu_test_data *tmu_test_data;
	struct google_sensor_data *sens;
	struct tmu_sensor_data *tmu;

	tmu_test_data = kunit_kzalloc(test, sizeof(*tmu_test_data), GFP_KERNEL);
	sens = kunit_kzalloc(test, sizeof(*sens), GFP_KERNEL);
	tmu = kunit_kzalloc(test, sizeof(*tmu), GFP_KERNEL);
	sens->sens_priv_data = tmu;
	tmu_test_data->mock_sens_data = sens;
	tmu_test_data->mock_tmu_data = tmu;
	tmu_test_data->fake_dev = sens->dev = fake_dev;
	tmu_test_data->fake_tzd = sens->tzd = fake_tzd;
	fake_tzd->devdata = sens;

	test->priv = tmu_test_data;

	return 0;
}

static int thermal_tmu_test_suite_init(struct kunit_suite *suite)
{
	struct thermal_zone_device_ops ops = {
		.get_temp = mock_tzd_get_temp,
		.set_trip_temp = mock_tzd_set_trip_val,
	};

	fake_tzd = thermal_zone_device_register_with_trips("mock_tmu-sensor", mock_trips,
							   THERMAL_TMU_NR_TRIPS, NULL,
							   &ops, NULL, 200, 200);
	fake_dev = root_device_register("mock_tmu-device");

	return 0;
}

static void thermal_tmu_test_suite_exit(struct kunit_suite *suite)
{
	thermal_zone_device_unregister(fake_tzd);
	root_device_unregister(fake_dev);
}

static struct kunit_suite thermal_tmu_helper_test_suite = {
	.name = "thermal_tmu_helper_tests",
	.test_cases = thermal_tmu_helper_test,
	.init = thermal_tmu_test_init,
	.suite_init = thermal_tmu_test_suite_init,
	.suite_exit = thermal_tmu_test_suite_exit,
};

kunit_test_suite(thermal_tmu_helper_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jikai Ma <jikai@google.com>");
