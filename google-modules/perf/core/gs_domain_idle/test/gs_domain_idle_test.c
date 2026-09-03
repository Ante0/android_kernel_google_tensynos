// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 * KUnit tests for Google domain idle governor.
 */

#define KUNIT_TEST

#include <kunit/test.h>

#undef EXPORT_SYMBOL_GPL
#define EXPORT_SYMBOL_GPL(...)

#include "../gs_domain_idle.c"

static struct generic_pm_domain mock_genpd_0 = { .name = "pd0" };
static struct generic_pm_domain mock_genpd_1 = { .name = "pd1" };

static int mock_lock_count;
static int mock_unlock_count;
static void mock_genpd_lock(struct generic_pm_domain *genpd)
{
	mock_lock_count++;
}

static void mock_genpd_unlock(struct generic_pm_domain *genpd)
{
	mock_unlock_count++;
}

static struct genpd_lock_ops mock_lock_ops = {
	.lock = mock_genpd_lock,
	.unlock = mock_genpd_unlock,
};

static struct gs_domain_data mock_gs_domain_data[3];
static int mock_attached_ids[] = { 5, 7 };

static int cluster_enabled_cb_cluster_id;
static int cluster_enabled_cb_enabled;
static int cluster_enabled_cb_call_count;

static void mock_set_cluster_enabled_cb(int cluster_id, int enabled)
{
	cluster_enabled_cb_cluster_id = cluster_id;
	cluster_enabled_cb_enabled = enabled;
	cluster_enabled_cb_call_count++;
}

/* Backup original static variables */
static struct gs_domain_data *orig_gs_domain_idle_data_arr;
static void (*orig_set_cluster_enabled_cb)(int, int);

static int gs_domain_idle_test_init(struct kunit *test)
{
	/* Backup globals */
	orig_gs_domain_idle_data_arr = gs_domain_idle_data_arr;
	orig_set_cluster_enabled_cb = set_cluster_enabled_cb;

	/* Setup mock data */
	mock_gs_domain_data[0] = (struct gs_domain_data){
		.genpd = &mock_genpd_0,
		.num_attached_policies_ids = 2,
		.attached_policies_ids = mock_attached_ids,
		.is_initialized = true,
		.power_off_disabled = false,
	};
	mock_gs_domain_data[1] =
		(struct gs_domain_data){ .genpd = &mock_genpd_1, .is_initialized = true };
	mock_gs_domain_data[2] = (struct gs_domain_data){ .is_initialized = false };

	gs_domain_idle_data_arr = mock_gs_domain_data;
	set_cluster_enabled_cb = NULL;
	mock_genpd_0.lock_ops = &mock_lock_ops;

	return 0;
}

static void gs_domain_idle_test_exit(struct kunit *test)
{
	/* Restore globals */
	gs_domain_idle_data_arr = orig_gs_domain_idle_data_arr;
	set_cluster_enabled_cb = orig_set_cluster_enabled_cb;
}

static void register_set_cluster_enabled_cb_test(struct kunit *test)
{
	void (*expected_cb)(int, int) = mock_set_cluster_enabled_cb;

	KUNIT_EXPECT_NULL(test, set_cluster_enabled_cb);

	register_set_cluster_enabled_cb(mock_set_cluster_enabled_cb);

	KUNIT_EXPECT_NOT_NULL(test, set_cluster_enabled_cb);
	KUNIT_EXPECT_PTR_EQ(test, set_cluster_enabled_cb, expected_cb);

	set_cluster_enabled_cb = NULL;
}

static void gs_genpd_state_param_sysfs_test(struct kunit *test)
{
	char *buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	ssize_t ret;
	struct gs_domain_data *data = &gs_domain_idle_data_arr[0];
	struct genpd_power_state mock_state = {};
	struct gs_genpd_param_file param_file = {
		.state = &mock_state,
		.genpd_data = data,
	};
	struct attribute *attr = &param_file.base_attr;

	/* --- Test gs_genpd_state_param_show --- */

	/* Test GS_DOMAIN_DISABLE */
	param_file.config_type = GS_DOMAIN_DISABLE;
	data->power_off_disabled = true;
	ret = gs_genpd_state_param_show(NULL, attr, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, "1\n", buf);
	KUNIT_EXPECT_EQ(test, 0, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 0, mock_unlock_count);

	data->power_off_disabled = false;
	ret = gs_genpd_state_param_show(NULL, attr, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, "0\n", buf);
	KUNIT_EXPECT_EQ(test, 0, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 0, mock_unlock_count);

	/* Test GS_DOMAIN_RESIDENCY */
	param_file.config_type = GS_DOMAIN_RESIDENCY;
	mock_state.residency_ns = 123 * NS_PER_US;
	ret = gs_genpd_state_param_show(NULL, attr, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, "123\n", buf);
	KUNIT_EXPECT_EQ(test, 1, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 1, mock_unlock_count);
	mock_lock_count = 0;
	mock_unlock_count = 0;

	/* Test GS_DOMAIN_ENTRY_LATENCY */
	param_file.config_type = GS_DOMAIN_ENTRY_LATENCY;
	mock_state.power_on_latency_ns = 456 * NS_PER_US;
	ret = gs_genpd_state_param_show(NULL, attr, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, "456\n", buf);
	KUNIT_EXPECT_EQ(test, 1, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 1, mock_unlock_count);
	mock_lock_count = 0;
	mock_unlock_count = 0;

	/* Test GS_DOMAIN_EXIT_LATENCY */
	param_file.config_type = GS_DOMAIN_EXIT_LATENCY;
	mock_state.power_off_latency_ns = 789 * NS_PER_US;
	ret = gs_genpd_state_param_show(NULL, attr, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, "789\n", buf);
	KUNIT_EXPECT_EQ(test, 1, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 1, mock_unlock_count);
	mock_lock_count = 0;
	mock_unlock_count = 0;

	/* --- Test gs_genpd_state_param_store --- */

	/* Test GS_DOMAIN_DISABLE */
	param_file.config_type = GS_DOMAIN_DISABLE;
	data->power_off_disabled = false;
	ret = gs_genpd_state_param_store(NULL, attr, "1", 1);
	KUNIT_EXPECT_EQ(test, 1, ret);
	KUNIT_EXPECT_TRUE(test, data->power_off_disabled);
	KUNIT_EXPECT_EQ(test, 0, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 0, mock_unlock_count);

	ret = gs_genpd_state_param_store(NULL, attr, "0", 1);
	KUNIT_EXPECT_EQ(test, 1, ret);
	KUNIT_EXPECT_FALSE(test, data->power_off_disabled);
	KUNIT_EXPECT_EQ(test, 0, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 0, mock_unlock_count);

	ret = gs_genpd_state_param_store(NULL, attr, "invalid", 7);
	KUNIT_EXPECT_LT(test, ret, 0); /* Should be error */
	KUNIT_EXPECT_FALSE(test, data->power_off_disabled);
	KUNIT_EXPECT_EQ(test, 0, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 0, mock_unlock_count);

	/* Test GS_DOMAIN_RESIDENCY */
	param_file.config_type = GS_DOMAIN_RESIDENCY;
	mock_state.residency_ns = 0;
	ret = gs_genpd_state_param_store(NULL, attr, "321", 3);
	KUNIT_EXPECT_EQ(test, 3, ret);
	KUNIT_EXPECT_EQ(test, 321 * NS_PER_US, mock_state.residency_ns);
	KUNIT_EXPECT_EQ(test, 1, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 1, mock_unlock_count);
	mock_lock_count = 0;
	mock_unlock_count = 0;

	/* Test GS_DOMAIN_ENTRY_LATENCY */
	param_file.config_type = GS_DOMAIN_ENTRY_LATENCY;
	mock_state.power_on_latency_ns = 0;
	ret = gs_genpd_state_param_store(NULL, attr, "654", 3);
	KUNIT_EXPECT_EQ(test, 3, ret);
	KUNIT_EXPECT_EQ(test, 654 * NS_PER_US, mock_state.power_on_latency_ns);
	KUNIT_EXPECT_EQ(test, 1, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 1, mock_unlock_count);
	mock_lock_count = 0;
	mock_unlock_count = 0;

	/* Test GS_DOMAIN_EXIT_LATENCY */
	param_file.config_type = GS_DOMAIN_EXIT_LATENCY;
	mock_state.power_off_latency_ns = 0;
	ret = gs_genpd_state_param_store(NULL, attr, "987", 3);
	KUNIT_EXPECT_EQ(test, 3, ret);
	KUNIT_EXPECT_EQ(test, 987 * NS_PER_US, mock_state.power_off_latency_ns);
	KUNIT_EXPECT_EQ(test, 1, mock_lock_count);
	KUNIT_EXPECT_EQ(test, 1, mock_unlock_count);
	mock_lock_count = 0;
	mock_unlock_count = 0;
}

static void gs_domain_idle_cluster_notifier_test(struct kunit *test)
{
	struct gs_domain_data *data = &gs_domain_idle_data_arr[0];
	int ret;

	/* Setup */
	set_cluster_enabled_cb = mock_set_cluster_enabled_cb;
	cluster_enabled_cb_call_count = 0;

	/* Test GENPD_NOTIFY_OFF */
	ret = gs_domain_idle_cluster_notifier(&data->nb, GENPD_NOTIFY_OFF, NULL);
	KUNIT_EXPECT_EQ(test, NOTIFY_OK, ret);
	KUNIT_EXPECT_EQ(test, 2, cluster_enabled_cb_call_count);
	KUNIT_EXPECT_EQ(test, 7, cluster_enabled_cb_cluster_id); /* last one called */
	KUNIT_EXPECT_EQ(test, 0, cluster_enabled_cb_enabled);

	/* Test GENPD_NOTIFY_ON */
	cluster_enabled_cb_call_count = 0;
	ret = gs_domain_idle_cluster_notifier(&data->nb, GENPD_NOTIFY_ON, NULL);
	KUNIT_EXPECT_EQ(test, NOTIFY_OK, ret);
	KUNIT_EXPECT_EQ(test, 2, cluster_enabled_cb_call_count);
	KUNIT_EXPECT_EQ(test, 7, cluster_enabled_cb_cluster_id);
	KUNIT_EXPECT_EQ(test, 1, cluster_enabled_cb_enabled);

	/* Test other action */
	cluster_enabled_cb_call_count = 0;
	ret = gs_domain_idle_cluster_notifier(&data->nb, GENPD_NOTIFY_PRE_ON, NULL);
	KUNIT_EXPECT_EQ(test, NOTIFY_OK, ret);
	KUNIT_EXPECT_EQ(test, 0, cluster_enabled_cb_call_count);

	/* Test power_off_disabled case */
	cluster_enabled_cb_call_count = 0;
	data->power_off_disabled = true;
	ret = gs_domain_idle_cluster_notifier(&data->nb, GENPD_NOTIFY_PRE_OFF, NULL);
	KUNIT_EXPECT_EQ(test, NOTIFY_BAD, ret);
	KUNIT_EXPECT_EQ(test, 0, cluster_enabled_cb_call_count);

	/* Test power_off_disabled but different action */
	ret = gs_domain_idle_cluster_notifier(&data->nb, GENPD_NOTIFY_OFF, NULL);
	KUNIT_EXPECT_EQ(test, NOTIFY_OK, ret);
	KUNIT_EXPECT_EQ(test, 2, cluster_enabled_cb_call_count);
	KUNIT_EXPECT_EQ(test, 7, cluster_enabled_cb_cluster_id);
	KUNIT_EXPECT_EQ(test, 0, cluster_enabled_cb_enabled);

	/* Cleanup */
	data->power_off_disabled = false;
	set_cluster_enabled_cb = NULL;
}

static struct kunit_case gs_domain_idle_test_cases[] = {
	KUNIT_CASE(register_set_cluster_enabled_cb_test),
	KUNIT_CASE(gs_genpd_state_param_sysfs_test),
	KUNIT_CASE(gs_domain_idle_cluster_notifier_test),
	{}
};

static struct kunit_suite gs_domain_idle_test_suite = {
	.name = "gs_domain_idle_test",
	.init = gs_domain_idle_test_init,
	.exit = gs_domain_idle_test_exit,
	.test_cases = gs_domain_idle_test_cases,
};

kunit_test_suite(gs_domain_idle_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for Google Pixel Domain Idle Governor");
