// SPDX-License-Identifier: GPL-2.0
/*
 * devfreq_tj_cdev_helper_test.c unit test for .
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/thermal.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "devfreq_tj_cdev_helper.h"
#include "devfreq_tj_cdev_helper_mock.h"

#define DEVFREQ_TEST_OPP_CT	3
#define DEVFREQ_TEST_MIN_FREQ	500000
#define DEVFREQ_TEST_FREQ_INC	1000000
#define DEVFREQ_TEST_MIN_POWER	50000
#define DEVFREQ_TEST_POWER_INC	100000

struct devfreq_tj_data {
	int cdev_init_ret;
	int cdev_rx_reg_ret;
	int cdev_reg_ret;
	int cdev_dt_read_val;
	int cdev_dt_read_ret;
	int cdev_dt_phandle_ret;
	bool cdev_exit_called;
	bool cdev_rx_unreg_called;
	bool cdev_rx_reg_called;
	bool cdev_reg_called;
	unsigned long qos_freq;
	struct thermal_cooling_device test_cdev;
	struct device_node test_device_node;
	struct devfreq_tj_cdev cdev_tj;
};
static struct platform_device *fake_pdev;

int mock_cdev_devfreq_init(struct cdev_devfreq_data *cdev,
		      struct device_node *np,
		      enum hw_dev_type cdev_id,
		      cdev_cb success_cb,
		      cdev_cb release_cb)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	return tj->cdev_init_ret;
}

void mock_cdev_devfreq_exit(struct cdev_devfreq_data *cdev)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	tj->cdev_exit_called = true;
}

int mock_cpm_mbox_register_notification(enum hw_dev_type type, struct notifier_block *nb)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	tj->cdev_rx_reg_called = true;
	return tj->cdev_rx_reg_ret;
}

void mock_cpm_mbox_unregister_notification(enum hw_dev_type type, struct notifier_block *nb)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	tj->cdev_rx_unreg_called = true;
}

void mock_cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev, unsigned long freq)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	tj->qos_freq = freq;
}

struct thermal_cooling_device *
mock_of_cooling_device_register(struct device *dev,
				struct device_node *np,
				char *type, void *devdata,
				const struct thermal_cooling_device_ops *ops)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	tj->cdev_reg_called = true;
	return tj->cdev_reg_ret ? ERR_PTR(tj->cdev_reg_ret) : &tj->test_cdev;
}

int mock_of_property_read_u32(const struct device_node *np, const char *propname, u32 *out_value)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	*out_value = tj->cdev_dt_read_val;
	return tj->cdev_dt_read_ret;
}

struct device_node *mock_of_parse_phandle(const struct device_node *np,
					  const char *phandle_name,
					  int index)
{
	struct kunit *test = kunit_get_current_test();
	struct devfreq_tj_data *tj = test->priv;

	return tj->cdev_dt_phandle_ret ? NULL : (&tj->test_device_node);
}

static void devfreq_tj_test_init(struct devfreq_tj_data *tj)
{
	tj->cdev_init_ret = 0;
	tj->cdev_rx_reg_ret = 0;
	tj->cdev_reg_ret = 0;
	tj->cdev_dt_read_val = HW_CDEV_GPU;
	tj->cdev_dt_read_ret = 0;
	tj->cdev_dt_phandle_ret = 0;
	tj->cdev_exit_called = false;
	tj->cdev_rx_unreg_called = false;
	tj->cdev_rx_reg_called = false;
	tj->cdev_reg_called = false;
	tj->cdev_tj.nb.notifier_call = NULL;
}

static void devfreq_tj_cdev_probe_test(struct kunit *test)
{
	struct devfreq_tj_data *tj = test->priv;

	// Success.
	KUNIT_EXPECT_EQ(test, devfreq_tj_cdev_probe_helper(fake_pdev), 0);

	// Read HW cdev ID error.
	devfreq_tj_test_init(tj);
	tj->cdev_dt_read_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, devfreq_tj_cdev_probe_helper(fake_pdev), tj->cdev_dt_read_ret);

	// Read HW cdev ID error.
	devfreq_tj_test_init(tj);
	tj->cdev_dt_read_val = HW_CDEV_MAX;
	KUNIT_EXPECT_EQ(test, devfreq_tj_cdev_probe_helper(fake_pdev), -EINVAL);
	tj->cdev_dt_read_val = HW_CDEV_BIG;
	KUNIT_EXPECT_EQ(test, devfreq_tj_cdev_probe_helper(fake_pdev), -EINVAL);

	// Read phandle error.
	devfreq_tj_test_init(tj);
	tj->cdev_dt_phandle_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, devfreq_tj_cdev_probe_helper(fake_pdev), -EINVAL);

	// devfreq cdev helper init error.
	devfreq_tj_test_init(tj);
	tj->cdev_init_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, devfreq_tj_cdev_probe_helper(fake_pdev), tj->cdev_init_ret);
}

static void devfreq_tj_cdev_cleanup_test(struct kunit *test)
{
	struct devfreq_tj_data *tj = test->priv;

	// Before successful init.
	devfreq_tj_cdev_cleanup(&tj->cdev_tj);
	KUNIT_EXPECT_TRUE(test, tj->cdev_exit_called);
	KUNIT_EXPECT_FALSE(test, tj->cdev_rx_unreg_called);

	// After successful init.
	devfreq_tj_test_init(tj);
	__devfreq_tj_cdev_success(&tj->cdev_tj.cdev);
	devfreq_tj_cdev_cleanup(&tj->cdev_tj);
	KUNIT_EXPECT_TRUE(test, tj->cdev_exit_called);
	KUNIT_EXPECT_TRUE(test, tj->cdev_rx_unreg_called);
}

static void devfreq_tj_cdev_success_cb_test(struct kunit *test)
{
	struct devfreq_tj_data *tj = test->priv;
	struct cdev_devfreq_data *cdev = &tj->cdev_tj.cdev;

	// Success.
	__devfreq_tj_cdev_success(cdev);
	KUNIT_EXPECT_NOT_NULL(test, tj->cdev_tj.nb.notifier_call);
	KUNIT_EXPECT_TRUE(test, tj->cdev_rx_reg_called);
	KUNIT_EXPECT_TRUE(test, tj->cdev_reg_called);

	// notification reg error.
	devfreq_tj_test_init(tj);
	tj->cdev_rx_reg_ret = -EINVAL;
	__devfreq_tj_cdev_success(cdev);
	KUNIT_EXPECT_NULL(test, tj->cdev_tj.nb.notifier_call);
	KUNIT_EXPECT_TRUE(test, tj->cdev_rx_reg_called);
	KUNIT_EXPECT_FALSE(test, tj->cdev_reg_called);

	// cdev reg failure.
	devfreq_tj_test_init(tj);
	tj->cdev_reg_ret = -EINVAL;
	__devfreq_tj_cdev_success(cdev);
	KUNIT_EXPECT_NULL(test, tj->cdev_tj.nb.notifier_call);
	KUNIT_EXPECT_TRUE(test, tj->cdev_rx_reg_called);
	KUNIT_EXPECT_TRUE(test, tj->cdev_reg_called);
}

static void devfreq_tj_cdev_mitigation_cb_test(struct kunit *test)
{
	struct devfreq_tj_data *tj = test->priv;
	struct cdev_devfreq_data *cdev = &tj->cdev_tj.cdev;
	int i = 0, cur_state = 0;
	union thermal_cpm_message msg;

	// verify if we ignore other notification type other than throttle.
	msg.req.type = THERMAL_STATE_NOTIFICATION;
	__devfreq_tj_cdev_cb(&tj->cdev_tj.nb, 0, msg.data);
	KUNIT_EXPECT_EQ(test, tj->cdev_tj.cur_cdev_state, 0);

	msg.req.type = THERMAL_REQUEST_THROTTLE;
	msg.data[1] = 0;
	__devfreq_tj_cdev_cb(&tj->cdev_tj.nb, 0, msg.data);
	KUNIT_EXPECT_EQ(test, tj->cdev_tj.cur_cdev_state, DEVFREQ_TEST_OPP_CT - 1);
	KUNIT_EXPECT_EQ(test, tj->qos_freq, cdev->opp_table[0].freq);

	for (i = DEVFREQ_TEST_OPP_CT - 1; i >= 0; i--) {
		cur_state = DEVFREQ_TEST_OPP_CT - i - 1;
		devfreq_tj_test_init(tj);
		msg.data[1] = cdev->opp_table[i].freq;
		__devfreq_tj_cdev_cb(&tj->cdev_tj.nb, 0, msg.data);
		KUNIT_EXPECT_EQ(test, tj->cdev_tj.cur_cdev_state, cur_state);
		KUNIT_EXPECT_EQ(test, tj->qos_freq, cdev->opp_table[i].freq);

		msg.data[1] = cdev->opp_table[i].freq + 1;
		__devfreq_tj_cdev_cb(&tj->cdev_tj.nb, 0, msg.data);
		KUNIT_EXPECT_EQ(test, tj->cdev_tj.cur_cdev_state, cur_state);
		KUNIT_EXPECT_EQ(test, tj->qos_freq, cdev->opp_table[i].freq);
	}
}

static struct kunit_case devfreq_tj_cdev_test[] = {
	KUNIT_CASE(devfreq_tj_cdev_probe_test),
	KUNIT_CASE(devfreq_tj_cdev_cleanup_test),
	KUNIT_CASE(devfreq_tj_cdev_success_cb_test),
	KUNIT_CASE(devfreq_tj_cdev_mitigation_cb_test),
	{},
};

static int devfreq_tj_cdev_test_init(struct kunit *test)
{
	struct devfreq_tj_data *tj =  kunit_kzalloc(test, sizeof(*tj), GFP_KERNEL);
	int i = 0;
	struct cdev_devfreq_data *cdev = &tj->cdev_tj.cdev;

	cdev->num_opps = DEVFREQ_TEST_OPP_CT;
	cdev->cdev_id = HW_CDEV_GPU;
	cdev->opp_table = kunit_kcalloc(test, DEVFREQ_TEST_OPP_CT,
					  sizeof(*cdev->opp_table), GFP_KERNEL);
	for (i = 0; i < DEVFREQ_TEST_OPP_CT; i++) {
		cdev->opp_table[i].freq =  (DEVFREQ_TEST_MIN_FREQ +
					    (DEVFREQ_TEST_FREQ_INC * i));
		cdev->opp_table[i].power = (DEVFREQ_TEST_MIN_POWER +
					    (DEVFREQ_TEST_POWER_INC * i));
	}
	test->priv = tj;
	devfreq_tj_test_init(tj);
	return 0;
}

static void devfreq_tj_cdev_test_exit(struct kunit *test)
{
}

static int devfreq_tj_cdev_test_suite_init(struct kunit_suite *suite)
{
	fake_pdev = platform_device_alloc("mock_cdev-pdevice", -1);
	return 0;
}

static void devfreq_tj_cdev_test_suite_exit(struct kunit_suite *suite)
{
	platform_device_put(fake_pdev);
}

static struct kunit_suite devfreq_tj_cdev_test_suite = {
	.name = "devfreq_tj_cdev_tests",
	.test_cases = devfreq_tj_cdev_test,
	.init = devfreq_tj_cdev_test_init,
	.exit = devfreq_tj_cdev_test_exit,
	.suite_init = devfreq_tj_cdev_test_suite_init,
	.suite_exit = devfreq_tj_cdev_test_suite_exit,
};
kunit_test_suite(devfreq_tj_cdev_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
