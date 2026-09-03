// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_zone_helper_tests.c Test suite to test all thermal zone helper functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/thermal.h>
#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "thermal_zone_helper.h"
#include "thermal_zone_mock.h"

#define THERMAL_TEST_SENSOR_CT 2
#define THERMAL_TEST_TRIP_CT (THERMAL_TRIP_CRITICAL + 1)
#define THERMAL_TEST_HYST 30000
#define THERMAL_TEST_TEMP 40000

struct thermal_zone_test_data {
	struct thermal_zone_device *tzd;
	struct device *fake_device;
	struct platform_device *fake_pdev;
	bool therm_reg_called;
	bool therm_unreg_called;
	int plat_irq_ret;
	int req_th_irq_ret;
	int fault_trip_ret;
	int fault_trip_val;
	struct thermal_zone_device *tz_ret;
	struct list_head mock_list;
	bool reg_irq_wakeable_called;
};
static struct thermal_zone_device *fake_tzd;
static struct device *fake_device;
static struct platform_device *fake_pdev;

struct thermal_zone_device *mock_devm_thermal_of_zone_register(
		struct device *dev, int id, void *data,
		const struct thermal_zone_device_ops *ops)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	zone_data->therm_reg_called = true;
	return zone_data->tz_ret;
}

void mock_devm_thermal_of_zone_unregister(
		struct device *dev, struct thermal_zone_device *tz)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	zone_data->therm_unreg_called = true;
}

int mock_platform_get_irq_optional(struct platform_device *pdev, int idx)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	return zone_data->plat_irq_ret;
}

int mock_devm_request_threaded_irq(struct device *dev, unsigned int irq,
				   irq_handler_t handler, irq_handler_t thread_fn,
				   unsigned long irqflags, const char *devname,
				   void *dev_id)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	return zone_data->req_th_irq_ret;
}

static void mock_register_tz_id_irq_wakeable(int tz_id)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	zone_data->reg_irq_wakeable_called = true;
}

static int mock_fault_trip_cb(struct thermal_zone_device *tzd, int temp)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	zone_data->fault_trip_val = temp;
	return zone_data->fault_trip_ret;
}

static void init_test_var(int tzd_ret_err)
{
	struct list_head *pos, *n;
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	zone_data->therm_reg_called = zone_data->therm_unreg_called = false;
	zone_data->reg_irq_wakeable_called = false;
	zone_data->fault_trip_ret = zone_data->fault_trip_val = 0;
	zone_data->tz_ret = tzd_ret_err ? ERR_PTR(tzd_ret_err) : zone_data->tzd;

	list_for_each_safe(pos, n, &zone_data->mock_list) {
		struct google_sensor_data *sens =
				list_entry(pos, struct google_sensor_data,
					   node);
		list_del(&sens->node);
		devm_kfree(zone_data->fake_device, sens);
	}
}

static int mock_get_temp(struct thermal_zone_device *tzd, int *temp)
{
	return 0;
}

static int mock_set_trip_temp(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
			      int temp)
{
	return 0;
}

static int mock_set_trips(struct thermal_zone_device *tzd, int trip, int hyst)
{
	return 0;
}

static void thermal_construct_and_register(int sens_ct,
					   bool use_get_temp,
					   bool use_set_trip,
					   bool use_set_trips,
					   bool irq_wakeable,
					   int ret_code,
					   const char *str)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;
	struct thermal_zone_device_ops ops = {};
	struct device *fake_device = zone_data->fake_device;

	ops.get_temp = use_get_temp ? mock_get_temp : NULL;
	ops.set_trip_temp = use_set_trip ? mock_set_trip_temp : NULL;
	ops.set_trips = use_set_trips ? mock_set_trips : NULL;

	KUNIT_EXPECT_EQ_MSG(test,
			thermal_register_sensors(fake_device, sens_ct, &ops,
						 &zone_data->mock_list, false, irq_wakeable),
			ret_code,
			str);
}

static void thermal_register_post_validate(int sens_ct, bool irq_wakeable, const char *str)
{
	struct kunit *test = kunit_get_current_test();
	struct thermal_zone_test_data *zone_data = test->priv;

	KUNIT_EXPECT_TRUE_MSG(test, zone_data->therm_reg_called,
			      "[%s]: mock_devm_thermal_of_zone_register not called.\n",
			      str);
	KUNIT_EXPECT_FALSE_MSG(test, zone_data->therm_unreg_called,
			       "[%s]: mock_devm_thermal_of_zone_unregister called.\n",
			       str);
	KUNIT_EXPECT_EQ_MSG(test, list_count_nodes(&zone_data->mock_list), sens_ct, str);

	if (irq_wakeable)
		KUNIT_EXPECT_TRUE_MSG(test, zone_data->reg_irq_wakeable_called,
					"[%s]: mock_register_tz_id_irq_wakeable not called.\n",
					str);
	else
		KUNIT_EXPECT_FALSE_MSG(test, zone_data->reg_irq_wakeable_called,
					"[%s]: mock_register_tz_id_irq_wakeable called.\n",
					str);
}

struct thermal_reg_test_case {
	const char *str;
	bool use_get_temp;
	bool use_set_trip;
	bool use_set_trips;
	bool irq_wakeable;
	int ret;
	int sens_ct;
};

const struct thermal_reg_test_case reg_case[] = {
	{
		.str = "Use get temp",
		.use_get_temp = true,
		.use_set_trip = false,
		.use_set_trips = false,
		.irq_wakeable = false,
		.ret = 0,
		.sens_ct = THERMAL_TEST_SENSOR_CT,
	},
	{
		.str = "Use set trip",
		.use_get_temp = true,
		.use_set_trip = true,
		.use_set_trips = false,
		.irq_wakeable = false,
		.ret = 0,
		.sens_ct = THERMAL_TEST_SENSOR_CT,
	},
	{
		.str = "Use set trips",
		.use_get_temp = true,
		.use_set_trip = false,
		.use_set_trips = true,
		.irq_wakeable = false,
		.ret = 0,
		.sens_ct = THERMAL_TEST_SENSOR_CT,
	},
	{
		.str = "Use irq_wakeable",
		.use_get_temp = true,
		.use_set_trip = false,
		.use_set_trips = false,
		.irq_wakeable = true,
		.ret = 0,
		.sens_ct = THERMAL_TEST_SENSOR_CT,
	},
	{
		.str = "No ops",
		.use_get_temp = false,
		.use_set_trip = false,
		.use_set_trips = false,
		.irq_wakeable = false,
		.ret = -EINVAL,
		.sens_ct = THERMAL_TEST_SENSOR_CT,
	},
};

static void thermal_sens_register_test(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;
	int i = 0;

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->tzd);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->fake_device);

	for (; i < ARRAY_SIZE(reg_case); i++) {
		const struct thermal_reg_test_case *test_param = &reg_case[i];

		init_test_var(0);
		thermal_construct_and_register(test_param->sens_ct,
					       test_param->use_get_temp,
					       test_param->use_set_trip,
					       test_param->use_set_trips,
					       test_param->irq_wakeable,
					       test_param->ret,
					       test_param->str);
		if (test_param->ret)
			continue;
		thermal_register_post_validate(test_param->sens_ct, test_param->irq_wakeable,
						test_param->str);
	}
}

static void thermal_sens_register_invalid_arg(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;
	struct thermal_zone_device_ops ops = {};

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->tzd);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->fake_device);

	KUNIT_EXPECT_EQ(test,
			thermal_register_sensors(NULL, THERMAL_TEST_SENSOR_CT,
						 &ops,  &zone_data->mock_list, false, false),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_register_sensors(zone_data->fake_device,
						 THERMAL_TEST_SENSOR_CT, NULL,
						 &zone_data->mock_list, false, false),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_register_sensors(zone_data->fake_device, -2, &ops,
						 &zone_data->mock_list, false, false),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			thermal_register_sensors(zone_data->fake_device,
						 THERMAL_TEST_SENSOR_CT, &ops,
						 NULL, false, false),
			-EINVAL);
}

static void thermal_sens_register_error(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->tzd);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->fake_device);

	init_test_var(-EINVAL);
	thermal_construct_and_register(THERMAL_TEST_SENSOR_CT, true, false,
				       false, false, -EINVAL,
				       "Thermal sensor register failure.");
}

static void thermal_sens_register_error_no_dt(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;

	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->tzd);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, zone_data->fake_device);

	init_test_var(-ENODEV);
	thermal_construct_and_register(THERMAL_TEST_SENSOR_CT, true, false,
				       false, false, 0,
				       "Thermal zone definition not available.");
	KUNIT_EXPECT_TRUE_MSG(test, list_empty(&zone_data->mock_list),
			      "Sensors registered with zone definition not available.");
}

static void thermal_sens_unregister_test(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;

	thermal_construct_and_register(THERMAL_TEST_SENSOR_CT, true, false,
				       false, false, 0,
				       "thermal_sens_unregister_test");
	KUNIT_EXPECT_EQ(test, thermal_unregister_sensors(zone_data->fake_device,
							 &zone_data->mock_list), 0);
	KUNIT_EXPECT_TRUE_MSG(test, zone_data->therm_unreg_called,
			       "mock_devm_thermal_of_zone_unregister called.\n");
}

struct thermal_unreg_test_case {
	const char *str;
	bool use_dev;
	bool use_list;
};

const struct thermal_unreg_test_case unreg_case[] = {
	{
		.str = "NULL list pointer",
		.use_dev = true,
		.use_list = false,
	},
	{
		.str = "NULL dev pointer",
		.use_dev = false,
		.use_list = false,
	},
	{
		.str = "NULL dev and list pointer",
		.use_dev = false,
		.use_list = false,
	},
};

static void thermal_sens_unregister_invalid_arg(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;
	int i = 0;

	thermal_construct_and_register(THERMAL_TEST_SENSOR_CT, true, false,
				       false, false, 0,
				       "thermal_sens_unregister_invalid_arg");
	for (; i < ARRAY_SIZE(unreg_case); i++) {
		const struct thermal_unreg_test_case *test_param = &unreg_case[i];
		struct device *dev = test_param->use_dev ? zone_data->fake_device : NULL;
		struct list_head *list = test_param->use_list ? &zone_data->mock_list : NULL;

		KUNIT_EXPECT_EQ_MSG(test,
				    thermal_unregister_sensors(dev, list),
				    -EINVAL, test_param->str);
	}
}

struct thermal_fetch_irq_test_case {
	const char *str;
	int irq_num_ret;
	int th_irq_ret;
	int func_ret;
	bool use_pdev;
};

const struct thermal_fetch_irq_test_case irq_case[] = {
	{
		.str = "Invalid input arg",
		.irq_num_ret = 0,
		.th_irq_ret = 0,
		.func_ret = -EINVAL,
		.use_pdev = false,
	},
	{
		.str = "IRQ fetch error",
		.irq_num_ret = -ENODEV,
		.th_irq_ret = 0,
		.func_ret = 0,
		.use_pdev = true,
	},
	{
		.str = "IRQ thread func register error",
		.irq_num_ret = 2,
		.th_irq_ret = -ENODEV,
		.func_ret = -ENODEV,
		.use_pdev = true,
	},
	{
		.str = "Success case",
		.irq_num_ret = 1,
		.th_irq_ret = 0,
		.func_ret = 0,
		.use_pdev = true,
	},
};

static irqreturn_t thermal_zone_test_irq_handler(int irq, void *data)
{
	return IRQ_HANDLED;
}

static void thermal_sens_fetch_and_reg_irqs(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;
	int i = 0;

	for (; i < ARRAY_SIZE(irq_case); i++) {
		const struct thermal_fetch_irq_test_case *test_param = &irq_case[i];

		zone_data->plat_irq_ret = test_param->irq_num_ret;
		zone_data->req_th_irq_ret = test_param->th_irq_ret;

		KUNIT_EXPECT_EQ_MSG(test,
				    thermal_fetch_and_register_irq(
						    (test_param->use_pdev ?
						     zone_data->fake_pdev : NULL),
						    thermal_zone_test_irq_handler, NULL,
						    IRQF_ONESHOT),
				    test_param->func_ret, test_param->str);
	}
}

static void thermal_sens_setup_irq_and_sensors(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;
	struct thermal_zone_device_ops ops = {};

	// thermal_sens_fetch_and_reg_irqs() errors out.
	zone_data->req_th_irq_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_setup_irq_and_sensors(zone_data->fake_pdev, THERMAL_TEST_SENSOR_CT,
						      &ops, &zone_data->mock_list,
						      thermal_zone_test_irq_handler,
						      IRQF_ONESHOT),
			-ENODEV);

	// thermal_register_sensors() errors out.
	zone_data->req_th_irq_ret = 0;
	KUNIT_EXPECT_EQ(test,
			thermal_setup_irq_and_sensors(zone_data->fake_pdev, THERMAL_TEST_SENSOR_CT,
						      &ops, &zone_data->mock_list,
						      thermal_zone_test_irq_handler,
						      IRQF_ONESHOT),
			-EINVAL);

	// Success
	init_test_var(0);
	ops.get_temp = mock_get_temp;
	KUNIT_EXPECT_EQ(test,
			thermal_setup_irq_and_sensors(zone_data->fake_pdev, THERMAL_TEST_SENSOR_CT,
						      &ops, &zone_data->mock_list,
						      thermal_zone_test_irq_handler,
						      IRQF_ONESHOT),
			0);
}

static void thermal_sens_create_tzd(struct kunit *test, bool include_hot)
{
	struct thermal_zone_test_data *zone_data = test->priv;
	struct thermal_trip *trip;
	int i = 0;

	zone_data->tzd->num_trips = THERMAL_TEST_TRIP_CT;
	memset(zone_data->tzd->trips, 0,
	       struct_size(zone_data->tzd, trips, THERMAL_TEST_TRIP_CT));
	for (i = 0; i < THERMAL_TEST_TRIP_CT; i++) {
		if (!include_hot && i == THERMAL_TRIP_HOT)
			continue;
		trip = &zone_data->tzd->trips[i].trip;
		trip->temperature = THERMAL_TEST_TEMP + i * 1000;
		trip->hysteresis = THERMAL_TEST_HYST;
		trip->type = i;
	}
}

static struct thermal_zone_device *__thermal_sens_create_tripless_tzd(void)
{
	struct thermal_zone_device *tzd;

	/* Allocate enough trips for thermal_sens_create_tzd() */
	tzd = kzalloc(struct_size(tzd, trips, THERMAL_TEST_TRIP_CT), GFP_KERNEL);
	tzd->ops.get_temp = mock_get_temp;
	snprintf(tzd->type, THERMAL_NAME_LENGTH, "mock-thermal-sensor");

	return tzd;
}

static void __thermal_sens_cleanup_tzd(struct thermal_zone_device *tzd)
{
	if (!tzd)
		return;
	kfree(tzd);
}

static void thermal_sens_fault_trip(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data = test->priv;

	// Invalid input arg
	init_test_var(0);
	KUNIT_EXPECT_EQ(test, thermal_configure_fault_threshold(NULL, mock_fault_trip_cb),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_configure_fault_threshold(&zone_data->mock_list, NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, thermal_configure_fault_threshold(NULL, NULL), -EINVAL);

	init_test_var(0);
	thermal_construct_and_register(1, true, false,
				       false, false, 0,
				       "thermal_sens_fault_trip_test");

	// Success TZ without trip
	KUNIT_EXPECT_EQ(test, thermal_configure_fault_threshold(
			&zone_data->mock_list, mock_fault_trip_cb), 0);
	KUNIT_EXPECT_EQ(test, zone_data->fault_trip_val, 0);

	// Success TZ with trip
	thermal_sens_create_tzd(test, true);
	init_test_var(0);
	thermal_construct_and_register(1, true, false,
				       false, false, 0,
				       "thermal_sens_fault_trip_test");

	KUNIT_EXPECT_EQ(test, thermal_configure_fault_threshold(
			&zone_data->mock_list, mock_fault_trip_cb), 0);
	KUNIT_EXPECT_EQ(test, zone_data->fault_trip_val,
			THERMAL_TEST_TEMP + THERMAL_TRIP_HOT * 1000);

	// Fault callback returns error.
	zone_data->fault_trip_ret = -EINVAL;
	zone_data->fault_trip_val = -1;
	KUNIT_EXPECT_EQ(test, thermal_configure_fault_threshold(
			&zone_data->mock_list, mock_fault_trip_cb), -EINVAL);

	// Success TZ with no hot trip
	thermal_zone_device_unregister(zone_data->tzd);
	thermal_sens_create_tzd(test, false);
	init_test_var(0);
	thermal_construct_and_register(1, true, false,
				       false, false, 0,
				       "thermal_sens_fault_trip_test");

	KUNIT_EXPECT_EQ(test, thermal_configure_fault_threshold(
			&zone_data->mock_list, mock_fault_trip_cb), 0);
	KUNIT_EXPECT_EQ(test, zone_data->fault_trip_val, 0);
	thermal_zone_device_unregister(zone_data->tzd);
}

static int thermal_zone_helper_test_init(struct kunit *test)
{
	struct thermal_zone_test_data *zone_data;

	zone_data = kunit_kzalloc(test, sizeof(*zone_data), GFP_KERNEL);
	INIT_LIST_HEAD(&zone_data->mock_list);

	zone_data->fake_device = fake_device;
	zone_data->fake_pdev = fake_pdev;
	zone_data->tzd = fake_tzd;
	fake_tzd->devdata = zone_data;
	test->priv = zone_data;

	kunit_activate_static_stub(test, tzh_register_tz_id_irq_wakeable,
				   mock_register_tz_id_irq_wakeable);
	return 0;
}

static void thermal_zone_helper_test_exit(struct kunit *test)
{
	kunit_deactivate_static_stub(test, tzh_register_tz_id_irq_wakeable);
}

static int thermal_zone_helper_test_suite_init(struct kunit_suite *suite)
{
	/*
	 * Newer kernels have an option to register kunit device.
	 * This is a temporary solution till we move to newer kernel.
	 */
	fake_device = root_device_register("mock_thermal-device");
	fake_pdev = platform_device_alloc("mock_thermal-pdevice", -1);
	fake_tzd = __thermal_sens_create_tripless_tzd();

	return 0;
}

static void thermal_zone_helper_test_suite_exit(struct kunit_suite *suite)
{
	__thermal_sens_cleanup_tzd(fake_tzd);
	root_device_unregister(fake_device);
	platform_device_put(fake_pdev);
}

static struct kunit_case thermal_zone_helper_test[] = {
	KUNIT_CASE(thermal_sens_register_test),
	KUNIT_CASE(thermal_sens_register_invalid_arg),
	KUNIT_CASE(thermal_sens_register_error),
	KUNIT_CASE(thermal_sens_register_error_no_dt),
	KUNIT_CASE(thermal_sens_unregister_test),
	KUNIT_CASE(thermal_sens_unregister_invalid_arg),
	KUNIT_CASE(thermal_sens_fetch_and_reg_irqs),
	KUNIT_CASE(thermal_sens_setup_irq_and_sensors),
	KUNIT_CASE(thermal_sens_fault_trip),
	{},
};

static struct kunit_suite thermal_zone_helper_test_suite = {
	.name = "thermal_zone_helper_tests",
	.test_cases = thermal_zone_helper_test,
	.init = thermal_zone_helper_test_init,
	.exit = thermal_zone_helper_test_exit,
	.suite_init = thermal_zone_helper_test_suite_init,
	.suite_exit = thermal_zone_helper_test_suite_exit
};

kunit_test_suite(thermal_zone_helper_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
