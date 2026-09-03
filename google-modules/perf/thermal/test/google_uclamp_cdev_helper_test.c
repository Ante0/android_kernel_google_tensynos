// SPDX-License-Identifier: GPL-2.0
/*
 * google_uclamp_cdev_helper_test.c unit test for uclamp cdev helper.
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>

#include "cdev_cpufreq_helper.h"
#include "google_uclamp_cdev_helper.h"

struct uclamp_cdev_test_data {
	struct platform_device *pdev;
	struct device_node cpu_node;
	struct thermal_cooling_device cdev;

	int of_prop_count_ret;
	int of_prop_read_ret;
	u32 phandle;
	u32 cdev_id;
	int cpu_id;
	int of_find_node_ret;
	int of_cpu_node_to_id_ret;

	int opp_count_ret;
	int update_opp_ret;

	int cdev_reg_ret;
	int dev_create_file_ret;
	bool unregister_called;
	bool remove_file_called;
	unsigned long sched_freq_cap;
	struct cdev_opp_table *mock_opp_table;
};

static int mock_of_property_count_u32_elems(const struct device_node *np, const char *propname)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	return data->of_prop_count_ret;
}

static int mock_of_property_read_u32_index(const struct device_node *np, const char *propname,
					   u32 index, u32 *out_value)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	if (data->of_prop_read_ret)
		return data->of_prop_read_ret;

	if (index % 2 == 0)
		*out_value = data->phandle;
	else
		*out_value = data->cdev_id;

	return 0;
}

static struct device_node *mock_of_find_node_by_phandle(u32 phandle)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	if (data->of_find_node_ret)
		return NULL;

	return &data->cpu_node;
}

static int mock_of_cpu_node_to_id(struct device_node *node)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	if (data->of_cpu_node_to_id_ret)
		return data->of_cpu_node_to_id_ret;

	return data->cpu_id;
}

static void mock_of_node_put(struct device_node *node)
{
}

static int mock_cdev_cpufreq_get_opp_count(unsigned int cpu)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	return data->opp_count_ret;
}

static int mock_cdev_cpufreq_update_opp_table(unsigned int cpu, enum hw_dev_type cdev_id,
					      struct cdev_opp_table *cdev_table,
					      unsigned int num_opp)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	if (data->update_opp_ret)
		return data->update_opp_ret;

	for (int i = 0; i < num_opp; i++) {
		cdev_table[i].freq = 100000 * (i + 1);
		cdev_table[i].power = 500 * (i + 1);
	}
	data->mock_opp_table = cdev_table;

	return 0;
}

static struct thermal_cooling_device *
mock_thermal_cooling_device_register(const char *type, void *devdata,
				     const struct thermal_cooling_device_ops *ops)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	if (data->cdev_reg_ret)
		return ERR_PTR(data->cdev_reg_ret);

	data->cdev.devdata = devdata;
	data->cdev.ops = ops;
	return &data->cdev;
}

static void mock_thermal_cooling_device_unregister(struct thermal_cooling_device *cdev)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	data->unregister_called = true;
}

static void mock_sched_thermal_freq_cap(unsigned int cpu, unsigned long freq)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	data->sched_freq_cap = freq;
}

static int mock_device_create_file(struct device *device, const struct device_attribute *entry)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	return data->dev_create_file_ret;
}

static void mock_device_remove_file(struct device *dev, const struct device_attribute *attr)
{
	struct kunit *test = kunit_get_current_test();
	struct uclamp_cdev_test_data *data = test->priv;

	data->remove_file_called = true;
}

static void uclamp_cdev_test_init_data(struct uclamp_cdev_test_data *data)
{
	data->of_prop_count_ret = 2;
	data->of_prop_read_ret = 0;
	data->phandle = 1;
	data->cdev_id = HW_CDEV_LIT;
	data->cpu_id = 0;
	data->of_find_node_ret = 0;
	data->of_cpu_node_to_id_ret = 0;
	data->opp_count_ret = 1;
	data->update_opp_ret = 0;
	data->cdev_reg_ret = 0;
	data->dev_create_file_ret = 0;
	data->unregister_called = false;
	data->remove_file_called = false;
	data->sched_freq_cap = 0;
	data->mock_opp_table = NULL;
}

static struct platform_device *fake_pdev;

static void uclamp_cdev_probe_test(struct kunit *test)
{
	struct uclamp_cdev_test_data *data = test->priv;

	/* Success */
	uclamp_cdev_test_init_data(data);
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), 0);
	thermal_uclamp_remove_helper(data->pdev);

	/* DT property count errors */
	uclamp_cdev_test_init_data(data);
	data->of_prop_count_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	uclamp_cdev_test_init_data(data);
	data->of_prop_count_ret = 1; /* Must be even */
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	/* DT property read errors */
	uclamp_cdev_test_init_data(data);
	data->of_prop_read_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	/* Invalid cdev_id */
	uclamp_cdev_test_init_data(data);
	data->cdev_id = HW_CDEV_MAX;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	/* of_find_node_by_phandle error */
	uclamp_cdev_test_init_data(data);
	data->of_find_node_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	/* of_cpu_node_to_id error */
	uclamp_cdev_test_init_data(data);
	data->of_cpu_node_to_id_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	/* OPP table setup errors */
	uclamp_cdev_test_init_data(data);
	data->opp_count_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EPROBE_DEFER);

	uclamp_cdev_test_init_data(data);
	data->update_opp_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	/* Cooling device registration error */
	uclamp_cdev_test_init_data(data);
	data->cdev_reg_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);

	/* Sysfs file creation error */
	uclamp_cdev_test_init_data(data);
	data->dev_create_file_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), -EINVAL);
}

static void uclamp_cdev_remove_test(struct kunit *test)
{
	struct uclamp_cdev_test_data *data = test->priv;

	uclamp_cdev_test_init_data(data);
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), 0);
	thermal_uclamp_remove_helper(data->pdev);
	KUNIT_EXPECT_TRUE(test, data->unregister_called);
	KUNIT_EXPECT_TRUE(test, data->remove_file_called);
}

static void uclamp_cdev_callbacks_test(struct kunit *test)
{
	struct uclamp_cdev_test_data *data = test->priv;
	unsigned long state;
	struct thermal_uclamp_cdev *uclamp_cdev;
	const struct thermal_cooling_device_ops *ops;

	/* Setup */
	uclamp_cdev_test_init_data(data);
	data->opp_count_ret = 3;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), 0);

	ops = data->cdev.ops;
	KUNIT_ASSERT_NOT_NULL(test, ops);
	uclamp_cdev = data->cdev.devdata;
	KUNIT_ASSERT_NOT_NULL(test, uclamp_cdev);

	/* Test get_max_state */
	KUNIT_EXPECT_EQ(test, ops->get_max_state(&data->cdev, &state), 0);
	KUNIT_EXPECT_EQ(test, state, 2); /* 3 OPPs -> max_state = 2 */

	/* Test get_cur_state (initial state is 0) */
	KUNIT_EXPECT_EQ(test, ops->get_cur_state(&data->cdev, &state), 0);
	KUNIT_EXPECT_EQ(test, state, 0);

	/* Test set_cur_state */
	/* state = 1, idx = max_state - state = 2 - 1 = 1. freq = 200000 */
	KUNIT_EXPECT_EQ(test, ops->set_cur_state(&data->cdev, 1), 0);
	KUNIT_EXPECT_EQ(test, uclamp_cdev->cur_state, 1);
	KUNIT_EXPECT_EQ(test, data->sched_freq_cap, 200000);

	/* state = 2, idx = max_state - state = 2 - 2 = 0. freq = 100000 */
	KUNIT_EXPECT_EQ(test, ops->set_cur_state(&data->cdev, 2), 0);
	KUNIT_EXPECT_EQ(test, uclamp_cdev->cur_state, 2);
	KUNIT_EXPECT_EQ(test, data->sched_freq_cap, 100000);

	/* state = 0, idx = max_state - state = 2 - 0 = 2. freq = 300000 */
	KUNIT_EXPECT_EQ(test, ops->set_cur_state(&data->cdev, 0), 0);
	KUNIT_EXPECT_EQ(test, uclamp_cdev->cur_state, 0);
	KUNIT_EXPECT_EQ(test, data->sched_freq_cap, 300000);

	thermal_uclamp_remove_helper(data->pdev);
}

static void uclamp_cdev_state2power_show_test(struct kunit *test)
{
	struct uclamp_cdev_test_data *data = test->priv;
	char *buf;
	ssize_t len;

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, buf);
	/* Setup */
	uclamp_cdev_test_init_data(data);
	data->opp_count_ret = 3;
	KUNIT_EXPECT_EQ(test, thermal_uclamp_probe_helper(data->pdev), 0);

	/* Test state2power_table_show */
	len = state2power_table_show(&data->cdev.device, NULL, buf);
	KUNIT_EXPECT_STREQ(test, buf, "2 1 1 \n");
	KUNIT_EXPECT_GT(test, len, 0);

	thermal_uclamp_remove_helper(data->pdev);
}

static struct kunit_case uclamp_cdev_helper_test_cases[] = {
	KUNIT_CASE(uclamp_cdev_probe_test),
	KUNIT_CASE(uclamp_cdev_remove_test),
	KUNIT_CASE(uclamp_cdev_callbacks_test),
	KUNIT_CASE(uclamp_cdev_state2power_show_test),
	{},
};

static int uclamp_cdev_test_init(struct kunit *test)
{
	struct uclamp_cdev_test_data *data;

	data = kunit_kzalloc(test, sizeof(*data), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, data);

	data->pdev = fake_pdev;
	test->priv = data;

	kunit_activate_static_stub(test, guc_of_property_count_u32_elems,
				   mock_of_property_count_u32_elems);
	kunit_activate_static_stub(test, guc_of_property_read_u32_index,
				   mock_of_property_read_u32_index);
	kunit_activate_static_stub(test, guc_of_find_node_by_phandle, mock_of_find_node_by_phandle);
	kunit_activate_static_stub(test, guc_of_cpu_node_to_id, mock_of_cpu_node_to_id);
	kunit_activate_static_stub(test, guc_of_node_put, mock_of_node_put);
	kunit_activate_static_stub(test, guc_cdev_cpufreq_get_opp_count,
				   mock_cdev_cpufreq_get_opp_count);
	kunit_activate_static_stub(test, guc_cdev_cpufreq_update_opp_table,
				   mock_cdev_cpufreq_update_opp_table);
	kunit_activate_static_stub(test, guc_thermal_cooling_device_register,
				   mock_thermal_cooling_device_register);
	kunit_activate_static_stub(test, guc_thermal_cooling_device_unregister,
				   mock_thermal_cooling_device_unregister);
	kunit_activate_static_stub(test, guc_sched_thermal_freq_cap, mock_sched_thermal_freq_cap);
	kunit_activate_static_stub(test, guc_device_create_file, mock_device_create_file);
	kunit_activate_static_stub(test, guc_device_remove_file, mock_device_remove_file);

	return 0;
}

static void uclamp_cdev_test_exit(struct kunit *test)
{
	kunit_deactivate_static_stub(test, guc_of_property_count_u32_elems);
	kunit_deactivate_static_stub(test, guc_of_property_read_u32_index);
	kunit_deactivate_static_stub(test, guc_of_find_node_by_phandle);
	kunit_deactivate_static_stub(test, guc_of_cpu_node_to_id);
	kunit_deactivate_static_stub(test, guc_of_node_put);
	kunit_deactivate_static_stub(test, guc_cdev_cpufreq_get_opp_count);
	kunit_deactivate_static_stub(test, guc_cdev_cpufreq_update_opp_table);
	kunit_deactivate_static_stub(test, guc_thermal_cooling_device_register);
	kunit_deactivate_static_stub(test, guc_thermal_cooling_device_unregister);
	kunit_deactivate_static_stub(test, guc_sched_thermal_freq_cap);
	kunit_deactivate_static_stub(test, guc_device_create_file);
	kunit_deactivate_static_stub(test, guc_device_remove_file);
}

static int uclamp_cdev_test_suite_init(struct kunit_suite *suite)
{
	fake_pdev = platform_device_alloc("google-uclamp-test", -1);
	if (!fake_pdev)
		return -ENOMEM;

	return 0;
}

static void uclamp_cdev_test_suite_exit(struct kunit_suite *suite)
{
	platform_device_put(fake_pdev);
}

static struct kunit_suite uclamp_cdev_helper_test_suite = {
	.name = "uclamp_cdev_helper_test",
	.test_cases = uclamp_cdev_helper_test_cases,
	.init = uclamp_cdev_test_init,
	.exit = uclamp_cdev_test_exit,
	.suite_init = uclamp_cdev_test_suite_init,
	.suite_exit = uclamp_cdev_test_suite_exit,
};

kunit_test_suite(uclamp_cdev_helper_test_suite);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Google LLC");
