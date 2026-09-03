// SPDX-License-Identifier: GPL-2.0
/*
 * powercap_helper_tests.c Test suite to test all the powercap helper functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <kunit/static_stub.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/powercap.h>
#include <linux/string.h>
#include <linux/version.h>

#include "google_powercap_cpu.h"
#include "google_powercap_devfreq.h"
#include "google_powercap_helper.h"
#include "google_powercap_stats.h"
#include "google_odpm.h"
#include "google_thermal_odpm_helper.h"
#include "powercap_voltage_match_algo.h"
#include "powercap_weights_algo.h"

#define GPC_TEST_OPP_CT		3
#define GPC_FREQ_INIT		300000
#define GPC_FREQ_INCREMENT	500000
#define GPC_POWER_INIT		500000
#define GPC_POWER_INCREMENT	1000000
#define GPC_VOLT_INIT		100
#define GPC_VOLT_INCREMENT	200
#define GPC_TREE_NODE_CT	4
#define GPC_MOCK_POWER_UW	1000005
#define GPC_TEST_CPU		0
#define GPC_TEST_CPU_MAX	4
#define GPC_ALGO_CHILD_CT	2

#define ODPM_TEST_RAIL_NAME "test_rail"
#define ODPM_TEST_RAIL_NAME_1 "test_rail_1"
#define ODPM_TEST_RAIL_NAME_2 "test_rail_2"
#define ODPM_TEST_POLLING_INTERVAL_100MS 100
#define ODPM_TEST_POLLING_INTERVAL_200MS 200

struct powercap_test_data {
	struct gpowercap gpc;

	/* ODPM test data */
	struct notifier_block test_nb;
	char reg_name[ODPM_REGULATOR_NAME_LEN];
	int b_notifier_reg_ret;
	int b_notifier_unreg_ret;
	bool sched_dw_called;
	unsigned long sched_dw_delay;
	int kzalloc_fail_on_count;
	int kzalloc_call_count;
	int kfree_call_count;
	int notifier_call_count_1;
	int notifier_call_count_2;
	u64 last_power_uw_1;
	u64 last_power_uw_2;
	ktime_t mock_ktime;
	bool mod_dw_called;
	bool cancel_dw_sync_called;
	struct odpm_rail_energy rail_energy[ODPM_CHANNEL_NUM];
	bool devfreq_cancel_dw_sync_called;
	bool odpm_unregister_called;
	bool odpm_register_called;
	bool gpc_device_remove_file_called;
	bool report_power_called;
	u64 reported_power_uw;
	bool stats_update_called[GPC_STAT_MAX];
	u64 stats_update_value[GPC_STAT_MAX];
	int stats_update_count[GPC_STAT_MAX];
};

struct powercap_algo_test_data {
	struct gpowercap parent_gpc;
	struct gpowercap children_gpc[GPC_ALGO_CHILD_CT];
	u64 children_power_limit[GPC_ALGO_CHILD_CT];
	u64 children_get_power_val[GPC_ALGO_CHILD_CT];
	int children_get_power_ret[GPC_ALGO_CHILD_CT];
};

/* Weights Algo Tests */
struct powercap_weights_algo_test_data {
	struct powercap_algo_test_data common;
	struct device_node dn;
	const char *child_names[GPC_ALGO_CHILD_CT];
	const char *child_weights_str[GPC_ALGO_CHILD_CT];
	u32 child_weights[GPC_ALGO_CHILD_CT];
	int of_count_strings_ret;
	int of_read_string_ret;
	int kstrtou32_ret;
	const char *kstrtou32_fail_on_str;
};

static struct powercap_weights_algo_test_data *weights_algo_test_data;

struct powercap_volt_algo_platform_data {
	const void *freq_table;
	unsigned int num_opps;
	unsigned int num_children;
};

static struct powercap_algo_test_data *gpc_algo_test_data;

static struct cdev_opp_table *opp_table;
static struct powercap_test_data *test_data;
static u64 power_limit_uw;
static int update_power_ret, reg_ct_ret, reg_zone_ret;
static int cdev_devfreq_init_ret;
static int qos_add_ret,
	   gpc_register_ret,
	   cpu_policy_ret,
	   cpu_get_opp_ct_ret,
	   cpu_get_opp_ret,
	   match_node_ret;
static unsigned int gpc_cpufreq;
static bool gpc_release_called, pc_unreg_called, remove_qos_called;
static struct gpowercap *last_registered_gpc;
static bool gpc_register_called;
static struct powercap_control_type *pct_test;
static struct powercap_zone pc_zone_test;
static struct gpowercap *virt_node, *leaf_node;
static struct device_node mock_np;
static int of_find_node_ret;
static bool of_node_put_called;
static struct cpufreq_policy *test_policy;

static unsigned int gpc_volt_algo_children_freq[][GPC_ALGO_CHILD_CT] = {
	{ 300000, 300000 },
	{ 800000, 800000 },
	{ 1300000, 1300000 },
};

static const struct powercap_volt_algo_platform_data gpc_volt_algo_test_pdata = {
	.freq_table = gpc_volt_algo_children_freq,
	.num_opps = ARRAY_SIZE(gpc_volt_algo_children_freq),
	.num_children = GPC_ALGO_CHILD_CT,
};

static const struct of_device_id powercap_volt_algo_test_match_data[] = {
	{
		.compatible = "google,lga-rango",
		.data = &gpc_volt_algo_test_pdata,
	},
	{}
};
static struct platform_device *fake_pdev;
static struct device_node gpc_root_dn, gpc_child1_dn, gpc_child2_dn, gpc_dev_dn,
	gpc_volt_algo_dn;
static u32 gpc_cdev_id;

static s32 qos_req_val, gpc_device_create_file_ret;
static u64 gpc_test_set_power_uw(struct gpowercap *gpc, u64 power_uw)
{
	int i;

	for (i = 0; i < GPC_ALGO_CHILD_CT; i++) {
		if (gpc == &gpc_algo_test_data->children_gpc[i]) {
			gpc_algo_test_data->children_power_limit[i] = power_uw;
			return power_uw;
		}
	}

	power_limit_uw = power_uw;

	return power_limit_uw;
}

static int dummy_rebalance(struct gpowercap *gpc)
{
	struct gpowercap *child;

	list_for_each_entry(child, &gpc->children, siblings) {
		if (child->ops && child->ops->set_power_uw)
			child->ops->set_power_uw(child, child->power_limit);
	}
	return 0;
}

static struct gpowercap_ops dummy_parent_ops = {
	.rebalance = dummy_rebalance,
};

static u64 gpc_test_get_power_uw(struct gpowercap *gpc)
{
	int i;

	for (i = 0; i < GPC_ALGO_CHILD_CT; i++) {
		if (gpc == &gpc_algo_test_data->children_gpc[i]) {
			if (gpc_algo_test_data->children_get_power_ret[i])
				gpc->num_opps = 0;
			else
				gpc->num_opps = GPC_TEST_OPP_CT;
			return gpc_algo_test_data->children_get_power_val[i];
		}
	}

	return GPC_MOCK_POWER_UW;
}


static int gpc_test_update_power_uw(struct gpowercap *gpc)
{
	if (gpc == &gpc_algo_test_data->children_gpc[0] ||
	    gpc == &gpc_algo_test_data->children_gpc[1])
		return 0;

	gpc->power_min = opp_table[0].power;
	gpc->power_max = opp_table[GPC_TEST_OPP_CT - 1].power;
	gpc->num_opps = GPC_TEST_OPP_CT;
	gpc->opp_table = opp_table;

	return update_power_ret;
}

static void gpc_test_release(struct gpowercap *gpc)
{
	if (gpc == &gpc_algo_test_data->children_gpc[0] ||
	    gpc == &gpc_algo_test_data->children_gpc[1])
		return;

	gpowercap_unregister(gpc);
	gpc_release_called = true;
}

static struct gpowercap_ops test_ops = {
	.set_power_uw = gpc_test_set_power_uw,
	.get_power_uw = gpc_test_get_power_uw,
	.update_power_uw = gpc_test_update_power_uw,
	.release = gpc_test_release,
};

static const struct of_device_id *mock_match_of_node(const struct of_device_id *matches,
					 const struct device_node *node)
{
	return (match_node_ret) ? NULL : powercap_volt_algo_test_match_data;
}

static struct device_node *mock_of_find_node_by_path(const char *path)
{
	return of_find_node_ret ? NULL : &mock_np;
}

static void mock_of_node_put(struct device_node *np)
{
	of_node_put_called = true;
}

static struct gpowercap *gpc_alloc_and_create_node(const char *name, struct gpowercap *parent,
						   struct gpowercap_ops *ops)
{
	struct gpowercap *gpowercap;
	struct kunit *test = kunit_get_current_test();
	int ret = 0;

	gpowercap = kunit_kzalloc(test, sizeof(*gpowercap), GFP_KERNEL);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpowercap);
	gpowercap_init(gpowercap, ops);
	ret = __gpowercap_register(name, gpowercap, parent);
	if (ret) {
		pr_err("powercap node:%s reg failed. err:%d\n", name, ret);
		return ERR_PTR(ret);
	}

	return gpowercap;
}

static struct powercap_control_type *mock_powercap_register_control_type(
		struct powercap_control_type *control_type, const char *name,
		const struct powercap_control_type_ops *ops)
{
	return (reg_ct_ret) ? ERR_PTR(reg_ct_ret) : pct_test;

}

static struct powercap_zone *mock_powercap_register_zone(
			struct powercap_zone *power_zone,
			struct powercap_control_type *control_type,
			const char *name,
			struct powercap_zone *parent,
			const struct powercap_zone_ops *ops,
			int nr_constraints,
			const struct powercap_zone_constraint_ops *const_ops)
{
	return (reg_zone_ret) ? ERR_PTR(reg_zone_ret) : &pc_zone_test;
}

static int mock_powercap_unregister_control_type(struct powercap_control_type *control_type)
{
	pc_unreg_called = true;
	return 0;
}

static struct cpufreq_policy *mock_cpufreq_cpu_get(unsigned int cpu)
{
	return cpu_policy_ret ? NULL : test_policy;
}

static int mock_gpowercap_register(const char *name, struct gpowercap *gpowercap,
				   struct gpowercap *parent)
{
	gpc_register_called = true;
	last_registered_gpc = gpowercap;
	return gpc_register_ret;
}

static int mock_google_pm_qos_add_cpufreq_request(struct cpufreq_policy *policy,
						  struct freq_qos_request *req,
						  enum freq_qos_req_type type,
						  s32 value)
{
	return qos_add_ret;
}

static int mock_google_pm_qos_remove_cpufreq_request(struct cpufreq_policy *policy,
						     struct freq_qos_request *req)
{
	remove_qos_called = true;
	return 0;
}

static unsigned int mock_cpufreq_quick_get(unsigned int cpu)
{
	return gpc_cpufreq;
}

static int mock_cdev_cpufreq_get_opp_count(unsigned int cpu)
{
	return cpu_get_opp_ct_ret;
}

static int mock_cdev_cpufreq_update_opp_table(unsigned int cpu,
					      enum hw_dev_type cdev_id,
					      struct cdev_opp_table *cdev_table,
					      unsigned int num_opp)
{
	int i = 0;

	if (cpu_get_opp_ret)
		return cpu_get_opp_ret;

	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		cdev_table[i].power = opp_table[i].power;
		cdev_table[i].freq = opp_table[i].freq;
		cdev_table[i].voltage = opp_table[i].voltage;
	}
	return 0;
}

static int mock_powercap_unregister_zone(struct powercap_control_type *control_type,
					 struct powercap_zone *power_zone)
{
	return 0;
}

static int mock_device_create_file(struct device *device, const struct device_attribute *entry)
{
	return gpc_device_create_file_ret;
}

static void mock_device_remove_file(struct device *device, const struct device_attribute *entry)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->gpc_device_remove_file_called = true;
}

static int mock_freq_qos_update_request(struct freq_qos_request *req, s32 new_value)
{
	qos_req_val = new_value;
	return 0;
}

static int mock_cdev_dev_pm_qos_update_request(struct dev_pm_qos_request *req, s32 new_value)
{
	return 0;
}

static int mock_cdev_pm_qos_add_devfreq_request(struct devfreq *devfreq,
						struct dev_pm_qos_request *req,
						enum dev_pm_qos_req_type type, s32 value)
{
	return qos_add_ret;
}

static void mock_cpufreq_cpu_put(struct cpufreq_policy *policy)
{
}

static void mock_warn_on_once(void)
{
}

static int mock_cdev_devfreq_init(struct cdev_devfreq_data *cdev,
				  struct device_node *np,
				  enum hw_dev_type cdev_id,
				  cdev_cb success_cb, cdev_cb release_cb)
{
	return cdev_devfreq_init_ret;
}

static void mock_gpc_cdev_pm_qos_update_request(struct cdev_devfreq_data *cdev, unsigned long freq)
{
	qos_req_val = freq;
}

static int odpm_register_ret, odpm_unregister_ret;

static int mock_odpm_register_client(const char *rail_name,
				     struct notifier_block *nb,
				     unsigned int polling_interval_ms)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->odpm_register_called = true;
	return odpm_register_ret;
}

static int mock_odpm_unregister_client(const char *rail_name,
					  struct notifier_block *nb,
					  unsigned int polling_interval_ms)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->odpm_unregister_called = true;
	return odpm_unregister_ret;
}

static bool mock_gpc_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->cancel_dw_sync_called = true;
	return true;
}

static bool mock_gpc_devfreq_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->devfreq_cancel_dw_sync_called = true;
	return true;
}

static bool mock_gpowercap_report_power_uw(struct gpowercap *gpowercap, u64 power_uw)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->report_power_called = true;
	data->reported_power_uw = power_uw;
	return true;
}

static int mock_of_property_read_u32(const struct device_node *np, const char *propname,
				     u32 *out_value)
{
	if (np == &gpc_child1_dn) {
		if (!strcmp(propname, "google,node-type")) {
			*out_value = GPOWERCAP_NODE_VIRTUAL;
			return 0;
		}
	} else if (np == &gpc_child2_dn) {
		if (!strcmp(propname, "google,node-type")) {
			*out_value = GPOWERCAP_NODE_CPU;
			return 0;
		}
		if (!strcmp(propname, "google,cdev-id")) {
			*out_value = gpc_cdev_id;
			return 0;
		}
	}

	return -EINVAL;
}

static struct device_node *mock_of_parse_phandle(const struct device_node *np,
						 const char *phandle_name, int index)
{
	if (np == &gpc_child2_dn) {
		if (!strcmp(phandle_name, "google,device-phandle"))
			return &gpc_dev_dn;
	}

	return NULL;
}

static void mock_gpc_stats_update(struct gpowercap *gpc, enum gpc_stat_type type, u64 value)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	if (type < GPC_STAT_MAX) {
		data->stats_update_called[type] = true;
		data->stats_update_value[type] = value;
		data->stats_update_count[type]++;
	}
}

static int mock_gpc_weights_of_property_count_strings(struct device_node *np, const char *propname)
{
	return weights_algo_test_data->of_count_strings_ret;
}

static int mock_gpc_weights_of_property_read_string_index(struct device_node *np,
							  const char *propname, int index,
							  const char **out_string)
{
	if (weights_algo_test_data->of_read_string_ret)
		return weights_algo_test_data->of_read_string_ret;

	if (index % 2 == 0)
		*out_string = weights_algo_test_data->child_names[index / 2];
	else
		*out_string = weights_algo_test_data->child_weights_str[index / 2];

	return 0;
}

static int mock_gpc_weights_kstrtou32(const char *s, unsigned int base, u32 *res)
{
	int i;

	if (weights_algo_test_data->kstrtou32_fail_on_str &&
	    !strcmp(s, weights_algo_test_data->kstrtou32_fail_on_str))
		return -EINVAL;

	if (weights_algo_test_data->kstrtou32_ret)
		return weights_algo_test_data->kstrtou32_ret;

	for (i = 0; i < GPC_ALGO_CHILD_CT; i++) {
		if (weights_algo_test_data->child_weights_str[i] &&
		    !strcmp(s, weights_algo_test_data->child_weights_str[i])) {
			*res = weights_algo_test_data->child_weights[i];
			return 0;
		}
	}

	return kstrtou32(s, base, res);
}

static void gpc_test_init_data(struct powercap_test_data *data)
{
	cdev_devfreq_init_ret = 0;
	cpu_get_opp_ct_ret = GPC_TEST_OPP_CT;
	cpu_get_opp_ret = 0;
	gpc_cpufreq = 0;
	gpc_register_ret = 0;
	cpu_policy_ret = 0;
	gpc_register_called = false;
	remove_qos_called = false;
	qos_add_ret = 0;
	power_limit_uw = 0;
	reg_zone_ret = 0;
	update_power_ret = 0;
	reg_ct_ret = 0;
	gpc_release_called = false;
	__gpowercap_destroy_hierarchy();
	pc_unreg_called = false;
	gpc_device_create_file_ret = 0;
	gpc_cdev_id = HW_CDEV_BIG;
	data->odpm_unregister_called = false;
	data->odpm_register_called = false;
	odpm_unregister_ret = 0;
	data->devfreq_cancel_dw_sync_called = false;
	odpm_register_ret = 0;
	data->gpc_device_remove_file_called = false;
	data->report_power_called = false;
	data->reported_power_uw = 0;
	memset(data->stats_update_called, 0, sizeof(data->stats_update_called));
	memset(data->stats_update_value, 0, sizeof(data->stats_update_value));
	memset(data->stats_update_count, 0, sizeof(data->stats_update_count));
}

static void powercap_init_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	data->gpc.ops = NULL;
	// NULL pointer as input. Nothing to check. but make sure no abnormal behavior.
	gpowercap_init(NULL, NULL);
	gpowercap_init(NULL, &test_ops);

	gpowercap_init(&data->gpc, NULL);
	KUNIT_EXPECT_NULL(test, data->gpc.ops);

	gpowercap_init(&data->gpc, &test_ops);
	KUNIT_EXPECT_PTR_EQ(test, data->gpc.ops, &test_ops);
	KUNIT_EXPECT_PTR_EQ(test, data->gpc.children.next, &data->gpc.children);
	KUNIT_EXPECT_PTR_EQ(test, data->gpc.children.prev, &data->gpc.children);
	KUNIT_EXPECT_PTR_EQ(test, data->gpc.siblings.next, &data->gpc.siblings);
	KUNIT_EXPECT_PTR_EQ(test, data->gpc.siblings.prev, &data->gpc.siblings);
	KUNIT_EXPECT_NOT_NULL(test, data->gpc.bypass_work.work.func);
}

static void powercap_destroy_hierarchy_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	/* Setup a hierarchy */
	gpc_test_init_data(data);
	fake_pdev->dev.of_node = &gpc_root_dn;
	KUNIT_EXPECT_EQ(test, gpowercap_dt_probe(fake_pdev), 0);
	KUNIT_EXPECT_TRUE(test, gpc_register_called);

	/* Destroy it */
	__gpowercap_destroy_hierarchy();

	/* Verify it was destroyed */
	KUNIT_EXPECT_TRUE(test, pc_unreg_called);

	/* Test destroying again (should be a no-op) */
	pc_unreg_called = false;
	__gpowercap_destroy_hierarchy();
	KUNIT_EXPECT_FALSE(test, pc_unreg_called);
}

static void powercap_gpc_register_and_ops_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	char *buf;
	u64 power_limit;
	struct gpowercap *my_root = NULL;

	gpc_test_init_data(data);
	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	gpowercap_init(&data->gpc, &test_ops);
	// No control type.
	KUNIT_EXPECT_EQ(test, __gpowercap_register("test", &data->gpc, NULL), -EAGAIN);

	// Initialize control type.
	__gpc_init_pct_test();

	/* Manually create hierarchy for testing ops */
	my_root = gpc_alloc_and_create_node("root", NULL, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(my_root));
	virt_node = gpc_alloc_and_create_node("virt", my_root, NULL);
	KUNIT_ASSERT_FALSE(test, IS_ERR(virt_node));
	leaf_node = gpc_alloc_and_create_node("leaf", virt_node, &test_ops);
	KUNIT_ASSERT_FALSE(test, IS_ERR(leaf_node));

	// Test sub/add power.
	__gpowercap_sub_power(leaf_node);
	KUNIT_EXPECT_EQ(test, virt_node->power_min, 0);
	KUNIT_EXPECT_EQ(test, virt_node->power_max, 0);
	__gpowercap_add_power(leaf_node);
	KUNIT_EXPECT_EQ(test, virt_node->power_min, 0);
	KUNIT_EXPECT_EQ(test, virt_node->power_max, opp_table[GPC_TEST_OPP_CT - 1].power);

	// Root already exists.
	KUNIT_EXPECT_EQ(test, __gpowercap_register("test", &data->gpc, NULL), -EBUSY);

	// NULL gpc node.
	KUNIT_EXPECT_EQ(test, __gpowercap_register("test", NULL, virt_node), -EINVAL);

	//Invalid ops
	gpowercap_init(&data->gpc, &test_ops);
	test_ops.release = NULL;
	KUNIT_EXPECT_EQ(test, __gpowercap_register("test", &data->gpc, virt_node), -EINVAL);
	test_ops.release = gpc_test_release;

	reg_zone_ret = -ENODEV;
	gpowercap_init(&data->gpc, &test_ops);
	KUNIT_EXPECT_EQ(test, __gpowercap_register("test", &data->gpc, virt_node), reg_zone_ret);
	reg_zone_ret = 0;

	// Ops test.
	KUNIT_EXPECT_EQ(test, __power_levels_uw_show(leaf_node, buf), 24);
	//kunit_info(test, "%s\n", buf);
	leaf_node->num_opps = 0;
	KUNIT_EXPECT_EQ(test, __power_levels_uw_show(leaf_node, buf), -EAGAIN);
	leaf_node->num_opps = GPC_TEST_OPP_CT;
	// virt node should return -EAGAIN.
	// We will not be registering this sysfs for virt node.
	// But worth checking the case.
	KUNIT_EXPECT_EQ(test, __power_levels_uw_show(virt_node, buf), -EAGAIN);
	kunit_kfree(test, buf);

	// set power limit with a very high value.
	KUNIT_EXPECT_EQ(test, __set_power_limit_uw(leaf_node, INT_MAX), 0);
	KUNIT_EXPECT_EQ(test, power_limit_uw, opp_table[GPC_TEST_OPP_CT - 1].power);
	KUNIT_EXPECT_EQ(test, leaf_node->power_limit, opp_table[GPC_TEST_OPP_CT - 1].power);

	// Test power_limit bypass flag skips the power limit CB.
	// temporarily votes for max power_limit.
	// And then votes back the changed power_limit after the timer expires.
	KUNIT_EXPECT_EQ(test, __set_power_limit_uw(leaf_node,
						   opp_table[1].power), 0);
	KUNIT_EXPECT_EQ(test, __power_limit_bypass(leaf_node, 200), 200);
	KUNIT_EXPECT_EQ(test, power_limit_uw, opp_table[GPC_TEST_OPP_CT - 1].power);
	// change the power limit and check if the power_limit CB is not called.
	KUNIT_EXPECT_EQ(test, __set_power_limit_uw(leaf_node,
						   opp_table[0].power), 0);
	KUNIT_EXPECT_EQ(test, power_limit_uw, opp_table[GPC_TEST_OPP_CT - 1].power);
	// Wait for the bypass time to expire.
	flush_delayed_work(&leaf_node->bypass_work);
	KUNIT_EXPECT_FALSE(test, test_bit(GPOWERCAP_POWER_LIMIT_BYPASS_FLAG,
					  &leaf_node->flags));
	KUNIT_EXPECT_EQ(test, power_limit_uw, opp_table[0].power);

	//get power.
	KUNIT_EXPECT_EQ(test, __get_power_uw(leaf_node, &power_limit), 0);
	KUNIT_EXPECT_EQ(test, power_limit, GPC_MOCK_POWER_UW);
	power_limit = 0;
	KUNIT_EXPECT_EQ(test, __get_power_uw(virt_node, &power_limit), 0);
	KUNIT_EXPECT_EQ(test, power_limit, GPC_MOCK_POWER_UW);

	//Release zone test.
	KUNIT_EXPECT_EQ(test, __gpowercap_release_zone(&virt_node->zone), -EBUSY);
	KUNIT_EXPECT_EQ(test, __gpowercap_release_zone(&leaf_node->zone), 0);
	// Make sure the top nodes power value are updated.
	KUNIT_EXPECT_EQ(test, virt_node->power_min, 0);
	KUNIT_EXPECT_EQ(test, virt_node->power_max, 0);
	KUNIT_EXPECT_TRUE(test, gpc_release_called);
	__gpowercap_destroy_hierarchy();
}

static void powercap_power_limit_bypass(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	gpowercap_init(&data->gpc, &test_ops);

	// Out of range time limit.
	KUNIT_EXPECT_EQ(test, __power_limit_bypass(&data->gpc, -1), 0);
	flush_delayed_work(&data->gpc.bypass_work);
	KUNIT_EXPECT_FALSE(test, test_bit(GPOWERCAP_POWER_LIMIT_BYPASS_FLAG,
					  &data->gpc.flags));
	data->gpc.flags = 0;
	KUNIT_EXPECT_EQ(test,
			__power_limit_bypass(&data->gpc,
					     GPOWERCAP_POWER_LIMIT_BYPASS_TIME_MSEC_MAX + 2),
			GPOWERCAP_POWER_LIMIT_BYPASS_TIME_MSEC_MAX);
	KUNIT_EXPECT_TRUE(test, test_bit(GPOWERCAP_POWER_LIMIT_BYPASS_FLAG, &data->gpc.flags));
	data->gpc.flags = 0;
}

static struct gpowercap_devfreq *__create_and_init_devfreq(struct kunit *test, bool init_odpm,
							   struct gpowercap *parent)
{
	struct gpowercap_devfreq *gpc_devfreq;
	int i = 0;

	// Inactive qos request
	gpc_devfreq = kzalloc(sizeof(*gpc_devfreq), GFP_KERNEL);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpc_devfreq);
	if (parent) {
		gpowercap_init(&gpc_devfreq->gpowercap, &test_ops);
		gpc_devfreq->gpowercap.parent = parent;
		list_add_tail(&gpc_devfreq->gpowercap.siblings, &parent->children);
	}

	gpc_devfreq->cdev.devfreq = kunit_kzalloc(test, sizeof(*gpc_devfreq->cdev.devfreq),
						  GFP_KERNEL);
	gpc_devfreq->cdev.num_opps = GPC_TEST_OPP_CT;
	gpc_devfreq->cdev.opp_table = kcalloc(GPC_TEST_OPP_CT,
					      sizeof(*gpc_devfreq->cdev.opp_table),
					      GFP_KERNEL);
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		gpc_devfreq->cdev.opp_table[i].power = opp_table[i].power;
		gpc_devfreq->cdev.opp_table[i].freq = opp_table[i].freq;
	}
	if (init_odpm) {
		gpc_devfreq->odpm_rail_name = kstrdup(ODPM_TEST_RAIL_NAME, GFP_KERNEL);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_devfreq->odpm_rail_name);
		gpc_devfreq->gpowercap.time_window_us = ODPM_TEST_POLLING_INTERVAL_100MS * 1000;
	}
	dev_pm_qos_add_request(gpc_devfreq->cdev.devfreq->dev.parent,
				&gpc_devfreq->cdev.qos_req,
				DEV_PM_QOS_MAX_FREQUENCY,
				PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);

	return gpc_devfreq;
}

static void powercap_devfreq_update_power_test(struct kunit *test)
{
	struct gpowercap_devfreq *gpc_devfreq;
	struct gpowercap *gpc;
	int i = 0;
	struct devfreq *devfreq;

	// success
	gpc_devfreq = __create_and_init_devfreq(test, true, NULL);
	gpc = &gpc_devfreq->gpowercap;
	KUNIT_EXPECT_EQ(test, gpc_devfreq_update_pd_power_uw(gpc), 0);
	KUNIT_EXPECT_EQ(test, gpc->num_opps, GPC_TEST_OPP_CT);
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		KUNIT_EXPECT_EQ(test, gpc->opp_table[i].power,
				opp_table[i].power);
		KUNIT_EXPECT_EQ(test, gpc->opp_table[i].freq,
				opp_table[i].freq);
	}
	KUNIT_EXPECT_EQ(test, gpc->power_limit, opp_table[GPC_TEST_OPP_CT - 1].power);
	KUNIT_EXPECT_EQ(test, gpc->power_max, opp_table[GPC_TEST_OPP_CT - 1].power);
	KUNIT_EXPECT_EQ(test, gpc->power_min, opp_table[0].power);

	// No devfreq
	devfreq = gpc_devfreq->cdev.devfreq;
	gpc_devfreq->cdev.devfreq = NULL;
	KUNIT_EXPECT_EQ(test, gpc_devfreq_update_pd_power_uw(gpc), -ENODEV);

	gpc_devfreq->cdev.devfreq = devfreq;
	gpc_devfreq_pd_release(&gpc_devfreq->gpowercap);
}

static void powercap_devfreq_get_power_test(struct kunit *test)
{
	struct gpowercap_devfreq *gpc_devfreq;
	struct gpowercap *gpc;
	int i = 0;
	struct devfreq *devfreq;

	// success
	gpc_devfreq = __create_and_init_devfreq(test, true, NULL);
	gpc = &gpc_devfreq->gpowercap;
	KUNIT_EXPECT_EQ(test, gpc_devfreq_update_pd_power_uw(gpc), 0);
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		gpc_devfreq->cdev.devfreq->last_status.current_frequency =
				opp_table[i].freq * HZ_PER_KHZ;
		KUNIT_EXPECT_EQ(test, gpc_devfreq_get_pd_power_uw(gpc),
				opp_table[i].power);
	}

	// When devfreq is not available.
	devfreq = gpc_devfreq->cdev.devfreq;
	gpc_devfreq->cdev.devfreq = NULL;
	KUNIT_EXPECT_EQ(test, gpc_devfreq_get_pd_power_uw(gpc), 0);

	gpc_devfreq->cdev.devfreq = devfreq;

	// ODPM data.
	gpc_devfreq->last_power_uw = opp_table[0].power;
	KUNIT_EXPECT_EQ(test, gpc_devfreq_get_pd_power_uw(gpc), opp_table[0].power);

	gpc_devfreq_pd_release(&gpc_devfreq->gpowercap);
}

static void powercap_devfreq_set_power_test(struct kunit *test)
{
	struct gpowercap_devfreq *gpc_devfreq;
	struct gpowercap *gpc;
	int i = 0;
	struct devfreq *devfreq;

	// success
	gpc_devfreq = __create_and_init_devfreq(test, false, NULL);
	gpc = &gpc_devfreq->gpowercap;
	KUNIT_EXPECT_EQ(test, gpc_devfreq_update_pd_power_uw(gpc), 0);
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		KUNIT_EXPECT_EQ(test, gpc_devfreq_set_pd_power_limit(gpc, opp_table[i].power),
				opp_table[i].power);
		KUNIT_EXPECT_EQ(test, qos_req_val, opp_table[i].freq);

		KUNIT_EXPECT_EQ(test, gpc_devfreq_set_pd_power_limit(gpc, opp_table[i].power + 1),
				opp_table[i].power + 1);
		KUNIT_EXPECT_EQ(test, qos_req_val, opp_table[i].freq);
	}

	/* Scenario: Over consumption (util=120) */
	gpc_devfreq->util_percentage = 120;
	test_data->stats_update_called[GPC_STAT_POWER_LIMIT] = false;
	test_data->stats_update_called[GPC_STAT_QOS] = false;
	KUNIT_EXPECT_EQ(test,
			gpc_devfreq_set_pd_power_limit(gpc, opp_table[1].power * 2 / 3),
			opp_table[1].power * 2 / 3);
	/*
	 * Limit = 1000k.
	 * i=0 (500k): est = 600k <= 1000k. OK.
	 * i=1 (1500k): est = 1800k > 1000k. Fail.
	 * Target = 0.
	 */
	KUNIT_EXPECT_EQ(test, qos_req_val, opp_table[0].freq);
	KUNIT_EXPECT_TRUE(test, test_data->stats_update_called[GPC_STAT_POWER_LIMIT]);
	KUNIT_EXPECT_EQ(test, test_data->stats_update_value[GPC_STAT_POWER_LIMIT],
			opp_table[1].power * 2 / 3);
	KUNIT_EXPECT_TRUE(test, test_data->stats_update_called[GPC_STAT_QOS]);
	KUNIT_EXPECT_EQ(test, test_data->stats_update_value[GPC_STAT_QOS],
			opp_table[0].freq);

	/* Scenario: Under consumption (util=80) */
	gpc_devfreq->util_percentage = 80;
	KUNIT_EXPECT_EQ(test,
			gpc_devfreq_set_pd_power_limit(gpc, opp_table[2].power * 80 / 100),
			opp_table[2].power * 80 / 100);
	/*
	 * Limit = 2000k.
	 * i=2 (2500k): est = 2000k <= 2000k. OK.
	 * Target = 2.
	 */
	KUNIT_EXPECT_EQ(test, qos_req_val, opp_table[2].freq);
	gpc_devfreq->util_percentage = 100;

	// When devfreq is not available.
	devfreq = gpc_devfreq->cdev.devfreq;
	gpc_devfreq->cdev.devfreq = NULL;
	KUNIT_EXPECT_EQ(test, gpc_devfreq_set_pd_power_limit(gpc, 0), 0);
	gpc_devfreq->cdev.devfreq = devfreq;
	gpc_devfreq_pd_release(&gpc_devfreq->gpowercap);
}

static void powercap_devfreq_release_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap_devfreq *gpc_devfreq;

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);
	kunit_activate_static_stub(test, gpc_google_thermal_odpm_unregister_client,
				   mock_odpm_unregister_client);

	/* No ODPM rail name */
	gpc_test_init_data(data);
	cdev_devfreq_init_ret = 0;
	gpc_devfreq = __create_and_init_devfreq(test, false, NULL);
	gpc_devfreq_pd_release(&gpc_devfreq->gpowercap);
	KUNIT_EXPECT_FALSE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_FALSE(test, data->devfreq_cancel_dw_sync_called);

	/* With ODPM rail name */
	gpc_test_init_data(data);
	cdev_devfreq_init_ret = 0;
	gpc_devfreq = __create_and_init_devfreq(test, true, NULL);
	gpc_devfreq_pd_release(&gpc_devfreq->gpowercap);
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_TRUE(test, data->devfreq_cancel_dw_sync_called);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);
	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_unregister_client);
}

static void powercap_devfreq_set_time_window_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap_devfreq *gpc_devfreq;

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);
	kunit_activate_static_stub(test, gpc_google_thermal_odpm_unregister_client,
				   mock_odpm_unregister_client);
	gpc_test_init_data(data);
	cdev_devfreq_init_ret = 0;
	gpc_devfreq = __create_and_init_devfreq(test, true, NULL);

	/* Scenario 1: no odpm rail name */
	gpc_test_init_data(data);
	kfree(gpc_devfreq->odpm_rail_name);
	gpc_devfreq->odpm_rail_name = NULL;
	KUNIT_EXPECT_EQ(test,
		__gpc_devfreq_set_time_window_us(&gpc_devfreq->gpowercap,
			ODPM_TEST_POLLING_INTERVAL_200MS * 1000), -EOPNOTSUPP);
	gpc_devfreq->odpm_rail_name = kstrdup(ODPM_TEST_RAIL_NAME, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_devfreq->odpm_rail_name);

	/* Scenario 2: same time window */
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test,
		__gpc_devfreq_set_time_window_us(&gpc_devfreq->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_100MS * 1000), 0);
	KUNIT_EXPECT_FALSE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_FALSE(test, data->odpm_register_called);

	/* Scenario 3: change time window (unregister fails) */
	gpc_test_init_data(data);
	odpm_unregister_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
		__gpc_devfreq_set_time_window_us(&gpc_devfreq->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_200MS * 1000), -EINVAL);
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_FALSE(test, data->odpm_register_called);
	odpm_unregister_ret = 0;

	/* Scenario 4: change time window (register fails) */
	gpc_test_init_data(data);
	odpm_register_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
		__gpc_devfreq_set_time_window_us(&gpc_devfreq->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_200MS * 1000), -EINVAL);
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_TRUE(test, data->odpm_register_called);
	KUNIT_EXPECT_EQ(test, gpc_devfreq->gpowercap.time_window_us, 0);
	/* reset time window */
	gpc_devfreq->gpowercap.time_window_us = ODPM_TEST_POLLING_INTERVAL_100MS * 1000;
	odpm_register_ret = 0;

	/* Scenario 5: success */
	gpc_test_init_data(data);
	gpc_devfreq->last_power_uw = 100000;
	KUNIT_EXPECT_EQ(test,
		__gpc_devfreq_set_time_window_us(&gpc_devfreq->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_200MS * 1000), 0);
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_TRUE(test, data->odpm_register_called);
	KUNIT_EXPECT_EQ(test,
			gpc_devfreq->gpowercap.time_window_us,
			ODPM_TEST_POLLING_INTERVAL_200MS * 1000);
	KUNIT_EXPECT_EQ(test, gpc_devfreq->last_power_uw, 0);

	gpc_devfreq_pd_release(&gpc_devfreq->gpowercap);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);
	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_unregister_client);
}

static void powercap_devfreq_odpm_work_func_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap_devfreq *gpc_devfreq;
	struct gpowercap *dummy_parent;
	u32 util_percentage[4] = {0, 100, 80, 120};
	int i;

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);
	kunit_activate_static_stub(test, gpc_google_thermal_odpm_unregister_client,
				   mock_odpm_unregister_client);
	kunit_activate_static_stub(test, gpowercap_report_power_uw,
				   mock_gpowercap_report_power_uw);

	gpc_test_init_data(data);
	dummy_parent = kunit_kzalloc(test, sizeof(*dummy_parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dummy_parent);
	gpowercap_init(dummy_parent, &dummy_parent_ops);

	gpc_devfreq = __create_and_init_devfreq(test, true, dummy_parent);
	gpc_devfreq_update_pd_power_uw(&gpc_devfreq->gpowercap);

	for (i = 0; i < ARRAY_SIZE(util_percentage); i++) {
		u32 util = (util_percentage[i] ? util_percentage[i] :
				GPOWERCAP_DEVFREQ_UTIL_PERCENTAGE_INITIAL);

		gpc_devfreq->target_opp_idx = gpc_devfreq->cdev.num_opps - 1;
		gpc_devfreq->last_power_uw =
			(gpc_devfreq->cdev.opp_table[gpc_devfreq->target_opp_idx].power *
				util_percentage[i] / 100);
		test_data->stats_update_called[GPC_STAT_UTIL] = false;
		__gpc_devfreq_odpm_work_func(&gpc_devfreq->odpm_work.work);
		KUNIT_EXPECT_EQ_MSG(test, gpc_devfreq->util_percentage, util,
				"Util:[%d]", util_percentage[i]);
		KUNIT_EXPECT_TRUE_MSG(test, data->report_power_called,
				"Util:[%d]", util_percentage[i]);
		KUNIT_EXPECT_EQ_MSG(test, data->reported_power_uw, gpc_devfreq->last_power_uw,
				"Util:[%d]", util_percentage[i]);
		KUNIT_EXPECT_TRUE_MSG(test, test_data->stats_update_called[GPC_STAT_UTIL],
				"Util:[%d] Stats Update", util_percentage[i]);
		KUNIT_EXPECT_EQ_MSG(test, test_data->stats_update_value[GPC_STAT_UTIL], util,
				"Util:[%d] Stats Value", util_percentage[i]);
		data->report_power_called = false;
	}

	gpc_devfreq_pd_release(&gpc_devfreq->gpowercap);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);
	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_unregister_client);
	kunit_deactivate_static_stub(test, gpowercap_report_power_uw);
}


static void powercap_cpu_setup_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	int i;

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);

	// No CPUfreq policy.
	cpu_policy_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0),
			-ENODEV);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));

	// gpowercap_register error.
	gpc_test_init_data(data);
	gpc_register_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0),
			gpc_register_ret);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));

	// get opp ct error.
	gpc_test_init_data(data);
	cpu_get_opp_ct_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0),
			cpu_get_opp_ct_ret);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));

	// opp count is zero.
	gpc_test_init_data(data);
	cpu_get_opp_ct_ret = 0;
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0),
			-ENODEV);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));

	// get OPP table error.
	gpc_test_init_data(data);
	cpu_get_opp_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0),
			cpu_get_opp_ret);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));

	// Qos req error.
	gpc_test_init_data(data);
	qos_add_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0),
			qos_add_ret);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));

	// Success case.
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0), 0);
	KUNIT_EXPECT_TRUE(test, gpc_register_called);
	KUNIT_EXPECT_TRUE(test, list_is_singular(&gpowercap_cpu_list));

	// Registering for sibling CPUs.
	KUNIT_EXPECT_TRUE(test, list_is_singular(&gpowercap_cpu_list));
	for (i = 1; i < GPC_TEST_CPU_MAX; i++) {
		gpc_test_init_data(data);
		KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(i, NULL, HW_CDEV_LIT, NULL, 0), 0);
		KUNIT_EXPECT_FALSE(test, gpc_register_called);
		KUNIT_EXPECT_TRUE(test, list_is_singular(&gpowercap_cpu_list));
	}
	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);

}

static void powercap_cpu_release_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap_cpu *pos, *n;

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_unregister_client,
				   mock_odpm_unregister_client);
	kunit_activate_static_stub(test, gpc_cancel_delayed_work_sync,
				   mock_gpc_cancel_delayed_work_sync);


	// Inactive qos req.
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0), 0);
	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node) {
		__gpc_cpu_pd_release(&pos->gpowercap);
	}
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));
	KUNIT_EXPECT_FALSE(test, remove_qos_called);

	// Active QOS req.
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0), 0);

	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node) {
		pos->qos_req.qos = kunit_kzalloc(test, sizeof(*pos->qos_req.qos),
						 GFP_KERNEL);
		__gpc_cpu_pd_release(&pos->gpowercap);
	}
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));
	KUNIT_EXPECT_TRUE(test, remove_qos_called);

	// Active QOS req & no policy.
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0), 0);
	cpu_policy_ret = -EINVAL;
	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node) {
		pos->qos_req.qos = kunit_kzalloc(test, sizeof(*pos->qos_req.qos),
						 GFP_KERNEL);
		__gpc_cpu_pd_release(&pos->gpowercap);
	}
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));
	KUNIT_EXPECT_FALSE(test, remove_qos_called);

	// With ODPM rail name
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test,
			__gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, "test_rail", 100),
			0);
	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node) {
		__gpc_cpu_pd_release(&pos->gpowercap);
	}
	KUNIT_EXPECT_TRUE(test, list_empty(&gpowercap_cpu_list));
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_TRUE(test, data->cancel_dw_sync_called);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_unregister_client);
	kunit_deactivate_static_stub(test, gpc_cancel_delayed_work_sync);
}

static void powercap_cpu_set_time_window_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap_cpu *gpc_cpu;
	struct gpowercap_cpu *pos, *n;
	const char *rail_name = "test_rail";

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);
	kunit_activate_static_stub(test, gpc_google_thermal_odpm_unregister_client,
				   mock_odpm_unregister_client);

	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test,
			__gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, rail_name,
						ODPM_TEST_POLLING_INTERVAL_100MS),
			0);
	gpc_cpu = list_first_entry(&gpowercap_cpu_list, struct gpowercap_cpu, node);

	/* Scenario 1: no ODPM rail name */
	gpc_cpu->odpm_rail_name = NULL;
	KUNIT_EXPECT_EQ(test,
		__gpc_cpu_set_time_window_us(&gpc_cpu->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_200MS * 1000),
		-EOPNOTSUPP);
	gpc_cpu->odpm_rail_name = rail_name;

	/* Scenario 2: same time window */
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test,
		__gpc_cpu_set_time_window_us(&gpc_cpu->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_100MS * 1000),
		0);
	KUNIT_EXPECT_FALSE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_FALSE(test, data->odpm_register_called);

	/* Scenario 3: change time window (unregister fails) */
	gpc_test_init_data(data);
	odpm_unregister_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
		__gpc_cpu_set_time_window_us(&gpc_cpu->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_200MS * 1000),
		-EINVAL);
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_FALSE(test, data->odpm_register_called);
	odpm_unregister_ret = 0;

	/* Scenario 4: change time window (register fails) */
	gpc_test_init_data(data);
	odpm_register_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
		__gpc_cpu_set_time_window_us(&gpc_cpu->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_200MS * 1000),
		-EINVAL);
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_TRUE(test, data->odpm_register_called);
	KUNIT_EXPECT_EQ(test, gpc_cpu->gpowercap.time_window_us, 0);
	odpm_register_ret = 0;
	gpc_cpu->gpowercap.time_window_us = ODPM_TEST_POLLING_INTERVAL_100MS * 1000;

	/* Scenario 5: success */
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test,
		__gpc_cpu_set_time_window_us(&gpc_cpu->gpowercap,
						ODPM_TEST_POLLING_INTERVAL_200MS * 1000),
		0);
	KUNIT_EXPECT_TRUE(test, data->odpm_unregister_called);
	KUNIT_EXPECT_TRUE(test, data->odpm_register_called);
	KUNIT_EXPECT_EQ(test, gpc_cpu->gpowercap.time_window_us,
			ODPM_TEST_POLLING_INTERVAL_200MS * 1000);

	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node)
		__gpc_cpu_pd_release(&pos->gpowercap);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);
	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_unregister_client);
}

static void powercap_cpu_update_power_test(struct kunit *test)
{
	int i = 0;
	struct gpowercap_cpu *pos;

	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0), 0);
	list_for_each_entry(pos, &gpowercap_cpu_list, node) {
		struct gpowercap *gpc = &pos->gpowercap;

		KUNIT_EXPECT_EQ(test, gpc->num_opps, 0);
		/* Check if we always update the util percentage to 100 */
		pos->util_percentage = 50;
		__gpc_cpu_update_cluster_power_uw(gpc);
		KUNIT_EXPECT_EQ(test, gpc->num_opps, GPC_TEST_OPP_CT);

		for (i = 0; i < GPC_TEST_OPP_CT; i++) {
			KUNIT_EXPECT_EQ(test, gpc->opp_table[i].power,
					opp_table[i].power);
			KUNIT_EXPECT_EQ(test, gpc->opp_table[i].freq,
					opp_table[i].freq);
		}
		KUNIT_EXPECT_EQ(test, gpc->power_limit,
				opp_table[GPC_TEST_OPP_CT - 1].power);
		KUNIT_EXPECT_EQ(test, gpc->power_max,
				opp_table[GPC_TEST_OPP_CT - 1].power);
		KUNIT_EXPECT_EQ(test, gpc->power_min, opp_table[0].power);
		KUNIT_EXPECT_EQ(test, pos->util_percentage, 100);
	}
}

static void powercap_cpu_get_power_test(struct kunit *test)
{
	int i = 0;
	struct gpowercap_cpu *pos;

	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0), 0);
	list_for_each_entry(pos, &gpowercap_cpu_list, node) {
		struct gpowercap *gpc = &pos->gpowercap;

		/* Check if we return last_power_uw if it is non-zero. */
		pos->last_power_uw = GPC_MOCK_POWER_UW;
		KUNIT_EXPECT_EQ(test, __gpc_cpu_get_cluster_power_uw(gpc), GPC_MOCK_POWER_UW);
		pos->last_power_uw = 0;

		for (i = 0; i < GPC_TEST_OPP_CT; i++) {
			gpc_cpufreq = opp_table[i].freq;
			KUNIT_EXPECT_EQ(test, __gpc_cpu_get_cluster_power_uw(gpc),
					opp_table[i].power);
		}
	}
	__gpc_cpu_pd_release(&list_first_entry(&gpowercap_cpu_list,
					struct gpowercap_cpu, node)->gpowercap);
}

static void powercap_cpu_set_power_test(struct kunit *test)
{
	int i = 0;
	struct gpowercap_cpu *pos, *n;

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);
	kunit_activate_static_stub(test, gpc_google_thermal_odpm_unregister_client,
				   mock_odpm_unregister_client);

	KUNIT_EXPECT_EQ(test, __gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, NULL, 0), 0);
	list_for_each_entry(pos, &gpowercap_cpu_list, node) {
		struct gpowercap *gpc = &pos->gpowercap;

		__gpc_cpu_update_cluster_power_uw(gpc);

		/* Legacy NO ODPM case */
		pos->util_percentage = 100;
		for (i = 0; i < GPC_TEST_OPP_CT; i++) {
			KUNIT_EXPECT_EQ(test,
					__gpc_cpu_set_cluster_power_limit(gpc,
									  opp_table[i].power),
					opp_table[i].power);

			KUNIT_EXPECT_EQ(test,
					__gpc_cpu_set_cluster_power_limit(gpc,
									  opp_table[i].power + 1),
					opp_table[i].power + 1);
			KUNIT_EXPECT_EQ(test, qos_req_val, opp_table[i].freq);
		}

		/* Scenario: Over consumption (util=120) */
		pos->util_percentage = 120;
		test_data->stats_update_called[GPC_STAT_POWER_LIMIT] = false;
		test_data->stats_update_called[GPC_STAT_QOS] = false;
		KUNIT_EXPECT_EQ(test,
				__gpc_cpu_set_cluster_power_limit(gpc, opp_table[1].power * 2 / 3),
				opp_table[1].power * 2 / 3);
		KUNIT_EXPECT_EQ(test, qos_req_val, opp_table[0].freq);
		KUNIT_EXPECT_TRUE(test, test_data->stats_update_called[GPC_STAT_POWER_LIMIT]);
		KUNIT_EXPECT_EQ(test, test_data->stats_update_value[GPC_STAT_POWER_LIMIT],
				opp_table[1].power * 2 / 3);
		KUNIT_EXPECT_TRUE(test, test_data->stats_update_called[GPC_STAT_QOS]);
		KUNIT_EXPECT_EQ(test, test_data->stats_update_value[GPC_STAT_QOS],
				opp_table[0].freq);

		/* Scenario: Under consumption (util=80) */
		pos->util_percentage = 80;
		KUNIT_EXPECT_EQ(test,
			__gpc_cpu_set_cluster_power_limit(gpc, opp_table[2].power * 80 / 100),
			opp_table[2].power * 80 / 100);
		KUNIT_EXPECT_EQ(test, qos_req_val, opp_table[2].freq);
		pos->util_percentage = 100;
	}
	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node)
		__gpc_cpu_pd_release(&pos->gpowercap);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);
	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_unregister_client);
}

static void powercap_cpu_odpm_work_func_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap_cpu *gpc_cpu;
	struct gpowercap_cpu *pos, *n;
	struct gpowercap *dummy_parent;
	u32 util_percentage[4] = {0, 100, 80, 120};

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);
	kunit_activate_static_stub(test, gpc_google_thermal_odpm_unregister_client,
				   mock_odpm_unregister_client);
	kunit_activate_static_stub(test, gpowercap_report_power_uw,
				   mock_gpowercap_report_power_uw);


	gpc_test_init_data(data);
	dummy_parent = kunit_kzalloc(test, sizeof(*dummy_parent), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dummy_parent);
	gpowercap_init(dummy_parent, &dummy_parent_ops);

	/* Setup with ODPM */
	KUNIT_EXPECT_EQ(test,
		__gpc_cpu_setup(GPC_TEST_CPU, NULL, HW_CDEV_LIT, "test_rail",
				ODPM_TEST_POLLING_INTERVAL_200MS),
		0);
	gpc_cpu = list_first_entry(&gpowercap_cpu_list, struct gpowercap_cpu, node);
	gpc_cpu->gpowercap.parent = dummy_parent;
	list_add_tail(&gpc_cpu->gpowercap.siblings, &dummy_parent->children);

	__gpc_cpu_update_cluster_power_uw(&gpc_cpu->gpowercap);

	for (int i = 0; i < ARRAY_SIZE(util_percentage); i++) {
		u32 util = (util_percentage[i] ? util_percentage[i] :
				GPOWERCAP_CPU_UTIL_PERCENTAGE_INITIAL);
		gpc_cpu->target_opp_idx = gpc_cpu->num_opps - 1;
		gpc_cpu->last_power_uw = (gpc_cpu->opp_table[gpc_cpu->target_opp_idx].power *
								util_percentage[i] / 100);
		test_data->stats_update_called[GPC_STAT_UTIL] = false;
		__gpc_cpu_odpm_work_func(&gpc_cpu->odpm_work.work);
		KUNIT_EXPECT_EQ_MSG(test, gpc_cpu->util_percentage, util,
						"Util:[%d]", util_percentage[i]);
		KUNIT_EXPECT_TRUE_MSG(test, data->report_power_called,
			"Util:[%d]", util_percentage[i]);
		KUNIT_EXPECT_EQ_MSG(test, data->reported_power_uw, gpc_cpu->last_power_uw,
			"Util:[%d]", util_percentage[i]);
		KUNIT_EXPECT_TRUE_MSG(test, test_data->stats_update_called[GPC_STAT_UTIL],
				"Util:[%d] Stats Update", util_percentage[i]);
		KUNIT_EXPECT_EQ_MSG(test, test_data->stats_update_value[GPC_STAT_UTIL], util,
				"Util:[%d] Stats Value", util_percentage[i]);
		data->report_power_called = false;
	}

	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node)
		__gpc_cpu_pd_release(&pos->gpowercap);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);
	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_unregister_client);
	kunit_deactivate_static_stub(test, gpowercap_report_power_uw);
}

static void powercap_devfreq_setup_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	kunit_activate_static_stub(test, gpc_google_thermal_odpm_register_client,
				   mock_odpm_register_client);

	/* Invalid cdev ID */
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test, __gpc_devfreq_setup(NULL, &gpc_dev_dn, HW_CDEV_MAX, NULL, 0),
			-EINVAL);

	/* cdev_devfreq_init fails */
	gpc_test_init_data(data);
	cdev_devfreq_init_ret = -EPROBE_DEFER;
	KUNIT_EXPECT_EQ(test, __gpc_devfreq_setup(NULL, &gpc_dev_dn, HW_CDEV_GPU, NULL, 0),
			cdev_devfreq_init_ret);

	/* gpowercap_register fails */
	gpc_test_init_data(data);
	gpc_register_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __gpc_devfreq_setup(NULL, &gpc_dev_dn, HW_CDEV_GPU, NULL, 0),
			gpc_register_ret);

	/* With ODPM rail name, register fails */
	gpc_test_init_data(data);
	odpm_register_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			__gpc_devfreq_setup(NULL, &gpc_dev_dn, HW_CDEV_GPU, ODPM_TEST_RAIL_NAME,
						100),
			-EINVAL);
	KUNIT_EXPECT_TRUE(test, data->odpm_register_called);

	/* Success case */
	gpc_test_init_data(data);
	KUNIT_EXPECT_EQ(test,
			__gpc_devfreq_setup(NULL, &gpc_dev_dn, HW_CDEV_GPU, ODPM_TEST_RAIL_NAME,
						100),
			0);
	KUNIT_EXPECT_TRUE(test, data->odpm_register_called);

	kunit_deactivate_static_stub(test, gpc_google_thermal_odpm_register_client);
}

static void powercap_dt_probe_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	/* Success */
	gpc_test_init_data(data);
	fake_pdev->dev.of_node = &gpc_root_dn;
	KUNIT_EXPECT_EQ(test, gpowercap_dt_probe(fake_pdev), 0);
	KUNIT_EXPECT_TRUE(test, gpc_register_called);

	/* control type registration fails. */
	gpc_test_init_data(data);
	fake_pdev->dev.of_node = &gpc_root_dn;
	reg_ct_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, gpowercap_dt_probe(fake_pdev), reg_ct_ret);

	/* gpowercap register fails */
	gpc_test_init_data(data);
	fake_pdev->dev.of_node = &gpc_root_dn;
	gpc_register_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, gpowercap_dt_probe(fake_pdev), gpc_register_ret);

	/* No of node */
	gpc_test_init_data(data);
	fake_pdev->dev.of_node = NULL;
	KUNIT_EXPECT_EQ(test, gpowercap_dt_probe(fake_pdev), -ENODEV);
	fake_pdev->dev.of_node = &gpc_root_dn;

	/* Invalid cdev ID */
	gpc_test_init_data(data);
	fake_pdev->dev.of_node = &gpc_root_dn;
	gpc_cdev_id = HW_CDEV_MAX;
	KUNIT_EXPECT_EQ(test, gpowercap_dt_probe(fake_pdev), -EINVAL);
}

static void powercap_dt_remove_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	/* Success */
	gpc_test_init_data(data);
	gpowercap_dt_probe(fake_pdev);
	gpowercap_dt_remove(fake_pdev);
	KUNIT_EXPECT_TRUE(test, pc_unreg_called);

	/* Remove without probe */
	gpc_test_init_data(data);
	gpowercap_dt_remove(fake_pdev);
	KUNIT_EXPECT_FALSE(test, pc_unreg_called);
}

static const struct odpm_rail_energy *mock_godpm_get_rail_energy(void)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	return data->rail_energy;
}

static void *mock_godpm_kzalloc(size_t size, gfp_t flags)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->kzalloc_call_count++;
	if (data->kzalloc_fail_on_count > 0 &&
	    data->kzalloc_call_count == data->kzalloc_fail_on_count)
		return NULL;

	return kunit_kzalloc(test, size, flags);
}

static void mock_godpm_kfree(const void *p)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->kfree_call_count++;
}

static int mock_godpm_blocking_notifier_chain_register(struct blocking_notifier_head *nh,
						  struct notifier_block *nb)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	return data->b_notifier_reg_ret;
}

static int mock_godpm_blocking_notifier_chain_unregister(struct blocking_notifier_head *nh,
						    struct notifier_block *nb)
{
	return 0;
}

static int mock_godpm_blocking_notifier_call_chain(struct blocking_notifier_head *nh,
						      unsigned long val, void *v)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;
	struct odpm_regulator_group *reg_group;
	struct odpm_client_sub_group *sub_group;
	u64 power_uw = (uintptr_t)v;

	list_for_each_entry(reg_group, &odpm_regulator_groups, node) {
		list_for_each_entry(sub_group, &reg_group->sub_groups, node) {
			if (&sub_group->notifier_head == nh) {
				if (!strcmp(reg_group->regulator_name, ODPM_TEST_RAIL_NAME_1)) {
					data->notifier_call_count_1++;
					data->last_power_uw_1 = power_uw;
				} else if (!strcmp(reg_group->regulator_name,
							ODPM_TEST_RAIL_NAME_2)) {
					data->notifier_call_count_2++;
					data->last_power_uw_2 = power_uw;
				}
			}
		}
	}

	return 0;
}

static bool mock_godpm_schedule_delayed_work(struct delayed_work *dwork,
					unsigned long delay)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->sched_dw_called = true;
	data->sched_dw_delay = delay;
	return true;
}

static bool mock_godpm_mod_delayed_work(struct workqueue_struct *wq,
				   struct delayed_work *dwork,
				   unsigned long delay)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->mod_dw_called = true;
	return true;
}

static bool mock_godpm_cancel_delayed_work_sync(struct delayed_work *dwork)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	data->cancel_dw_sync_called = true;
	return true;
}

static ktime_t mock_godpm_ktime_get(void)
{
	struct kunit *test = kunit_get_current_test();
	struct powercap_test_data *data = test->priv;

	return data->mock_ktime;
}

static void odpm_test_init_data(struct powercap_test_data *data)
{
	data->b_notifier_reg_ret = 0;
	data->b_notifier_unreg_ret = 0;
	data->sched_dw_called = false;
	data->mod_dw_called = false;
	data->cancel_dw_sync_called = false;
	data->sched_dw_delay = 0;
	data->kzalloc_fail_on_count = 0;
	data->kzalloc_call_count = 0;
	data->kfree_call_count = 0;
	data->notifier_call_count_1 = 0;
	data->notifier_call_count_2 = 0;
	data->last_power_uw_1 = 0;
	data->last_power_uw_2 = 0;
	data->mock_ktime = 0;
	INIT_LIST_HEAD(&odpm_regulator_groups);
	min_polling_interval_ms = UINT_MAX;
}

static void odpm_register_first_client_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct odpm_regulator_group *reg_group;

	/* Test successful registration of first client */
	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);

	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_TRUE(test, data->sched_dw_called);
	KUNIT_EXPECT_EQ(test, min_polling_interval_ms, ODPM_TEST_POLLING_INTERVAL_200MS);

	reg_group = list_first_entry(&odpm_regulator_groups, struct odpm_regulator_group, node);
	KUNIT_EXPECT_STREQ(test, reg_group->regulator_name, ODPM_TEST_RAIL_NAME);
}

static void odpm_unregister_client_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	/* Register a client first */
	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);

	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&odpm_regulator_groups));

	/* Test successful unregistration */
	data->sched_dw_called = false;
	data->kfree_call_count = 0;
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_unregister_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_EQ(test, data->kfree_call_count, 3);
	KUNIT_EXPECT_TRUE(test, data->cancel_dw_sync_called);
	KUNIT_EXPECT_EQ(test, min_polling_interval_ms, UINT_MAX);
	odpm_polling_work(&odpm_global_work.work);
	KUNIT_EXPECT_FALSE(test, data->sched_dw_called);
}

static void odpm_register_multiple_clients_same_interval_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct odpm_regulator_group *reg_group;
	struct odpm_client_sub_group *sub_group;
	struct notifier_block test_nb_2;

	/* Test successful registration of multiple clients for same rail/interval */
	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);

	/* Register first client */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_TRUE(test, data->sched_dw_called);
	KUNIT_EXPECT_EQ(test, min_polling_interval_ms, ODPM_TEST_POLLING_INTERVAL_200MS);

	/* Reset flag and register second client */
	data->sched_dw_called = false;
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &test_nb_2,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	/* Check that no new group was added and work was not rescheduled */
	KUNIT_EXPECT_TRUE(test, list_is_singular(&odpm_regulator_groups));
	KUNIT_EXPECT_FALSE(test, data->sched_dw_called);
	KUNIT_EXPECT_FALSE(test, data->mod_dw_called);

	reg_group = list_first_entry(&odpm_regulator_groups, struct odpm_regulator_group, node);
	KUNIT_EXPECT_TRUE(test, list_is_singular(&reg_group->sub_groups));
	sub_group = list_first_entry(&reg_group->sub_groups, struct odpm_client_sub_group, node);
	KUNIT_EXPECT_EQ(test, list_count_nodes(&sub_group->clients), 2);

	/* Unregister first client, group should remain */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_unregister_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_FALSE(test, data->cancel_dw_sync_called);

	/* Unregister second client, group should be removed */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_unregister_client(data->reg_name, &test_nb_2,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_TRUE(test, data->cancel_dw_sync_called);
}

static void odpm_register_multiple_clients_different_intervals_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct odpm_regulator_group *reg_group;
	struct notifier_block test_nb_2;

	/* Test successful registration of multiple clients for same rail, different intervals */
	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);

	/* Register first client with 200ms */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_TRUE(test, data->sched_dw_called);
	KUNIT_EXPECT_EQ(test, min_polling_interval_ms, ODPM_TEST_POLLING_INTERVAL_200MS);

	/* Register second client with faster 100ms interval */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &test_nb_2,
							ODPM_TEST_POLLING_INTERVAL_100MS), 0);
	KUNIT_EXPECT_TRUE(test, data->mod_dw_called);
	KUNIT_EXPECT_EQ(test, min_polling_interval_ms, ODPM_TEST_POLLING_INTERVAL_100MS);

	/* Check that there is one regulator group with two sub-groups */
	KUNIT_EXPECT_TRUE(test, list_is_singular(&odpm_regulator_groups));
	reg_group = list_first_entry(&odpm_regulator_groups, struct odpm_regulator_group, node);
	KUNIT_EXPECT_EQ(test, list_count_nodes(&reg_group->sub_groups), 2);

	/* Unregister faster client, polling interval should be recalculated */
	data->mod_dw_called = false;
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_unregister_client(data->reg_name, &test_nb_2,
							ODPM_TEST_POLLING_INTERVAL_100MS), 0);
	KUNIT_EXPECT_TRUE(test, data->mod_dw_called);
	KUNIT_EXPECT_EQ(test, min_polling_interval_ms, ODPM_TEST_POLLING_INTERVAL_200MS);

	/* Unregister last client */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_unregister_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_TRUE(test, data->cancel_dw_sync_called);
}

static void odpm_polling_work_notification_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct notifier_block test_nb_2;
	const char *reg_name_1 = ODPM_TEST_RAIL_NAME_1;
	const char *reg_name_2 = ODPM_TEST_RAIL_NAME_2;
	u64 expected_power_uw;

	odpm_test_init_data(data);

	/* Setup rail energy data */
	data->rail_energy[0].schematic_name = reg_name_1;
	data->rail_energy[0].acc_energy = 100000;
	data->rail_energy[0].timestamp_ms = 100;
	data->rail_energy[1].schematic_name = reg_name_2;
	data->rail_energy[1].acc_energy = 200000;
	data->rail_energy[1].timestamp_ms = 100;

	/* Register client 1 for rail 1, 100ms */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(reg_name_1, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_100MS), 0);
	/* Register client 2 for rail 2, 200ms */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(reg_name_2, &test_nb_2,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);

	/* Initial poll at time 0. No notification, just sets baseline. */
	data->mock_ktime = 0;
	odpm_polling_work(&odpm_global_work.work);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_1, 0);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_2, 0);
	KUNIT_EXPECT_TRUE(test, data->sched_dw_called);
	KUNIT_EXPECT_EQ(test, data->sched_dw_delay,
			msecs_to_jiffies(ODPM_TEST_POLLING_INTERVAL_100MS));
	data->sched_dw_called = false;

	/* Poll at 100ms. Client 1 should be notified. */
	data->mock_ktime = ktime_add_ms(0, ODPM_TEST_POLLING_INTERVAL_100MS);
	/* delta_energy = 100000 uWs, delta_time = 100 ms -> 1W */
	data->rail_energy[0].acc_energy += 100000;
	data->rail_energy[0].timestamp_ms += 100;
	expected_power_uw = 1000000;

	odpm_polling_work(&odpm_global_work.work);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_1, 1);
	KUNIT_EXPECT_EQ(test, data->last_power_uw_1, expected_power_uw);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_2, 0);
	KUNIT_EXPECT_TRUE(test, data->sched_dw_called);
	KUNIT_EXPECT_EQ(test, data->sched_dw_delay,
			msecs_to_jiffies(ODPM_TEST_POLLING_INTERVAL_100MS));
	data->sched_dw_called = false;

	/* Poll at 200ms, both should be notified */
	data->mock_ktime = ktime_add_ms(0, ODPM_TEST_POLLING_INTERVAL_200MS);
	/* rail 1: delta_energy = 100000 uWs, delta_time = 100 ms -> 1W */
	data->rail_energy[0].acc_energy += 100000;
	data->rail_energy[0].timestamp_ms += 100;
	/* rail 2: delta_energy = 300000 uWs, delta_time = 300 ms -> 1W */
	data->rail_energy[1].acc_energy += 300000;
	data->rail_energy[1].timestamp_ms += 300;
	expected_power_uw = 1000000;

	odpm_polling_work(&odpm_global_work.work);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_1, 2);
	KUNIT_EXPECT_EQ(test, data->last_power_uw_1, expected_power_uw);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_2, 1);
	KUNIT_EXPECT_EQ(test, data->last_power_uw_2, expected_power_uw);

	/* Poll at 300ms, only client 1 should be notified */
	data->mock_ktime = ktime_add_ms(0, 3 * ODPM_TEST_POLLING_INTERVAL_100MS);
	/* rail 1: delta_energy = 150000 uWs, delta_time = 100 ms -> 1.5W */
	data->rail_energy[0].acc_energy += 150000;
	data->rail_energy[0].timestamp_ms += 100;
	expected_power_uw = 1500000;

	odpm_polling_work(&odpm_global_work.work);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_1, 3);
	KUNIT_EXPECT_EQ(test, data->last_power_uw_1, expected_power_uw);
	KUNIT_EXPECT_EQ(test, data->notifier_call_count_2, 1);

	/* Cleanup */
	google_thermal_odpm_unregister_client(reg_name_1, &data->test_nb,
					      ODPM_TEST_POLLING_INTERVAL_100MS);
	google_thermal_odpm_unregister_client(reg_name_2, &test_nb_2,
						ODPM_TEST_POLLING_INTERVAL_200MS);
}

static void odpm_register_invalid_interval_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct odpm_regulator_group *reg_group;
	struct odpm_client_sub_group *sub_group;

	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);

	/* Register with an interval that is not a multiple of MIN_POLLING_INTERVAL_MS */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_100MS + 50), 0);

	/* Verify that it was rounded down and a group was created with the correct interval */
	KUNIT_EXPECT_FALSE(test, list_empty(&odpm_regulator_groups));
	reg_group = list_first_entry(&odpm_regulator_groups, struct odpm_regulator_group, node);
	sub_group = list_first_entry(&reg_group->sub_groups, struct odpm_client_sub_group, node);
	KUNIT_EXPECT_EQ(test, sub_group->polling_interval_ms, ODPM_TEST_POLLING_INTERVAL_100MS);
	KUNIT_EXPECT_EQ(test, min_polling_interval_ms, ODPM_TEST_POLLING_INTERVAL_100MS);

	/* Unregister with the same invalid interval, it should succeed */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_unregister_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_100MS + 50), 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&odpm_regulator_groups));
}

static void odpm_unregister_invalid_regulator_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	/* First, register a client successfully */
	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS), 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&odpm_regulator_groups));

	/* Now, try to unregister with an invalid regulator name */
	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_unregister_client("invalid_rail", &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS),
							-ENOENT);

	/* The group should still exist, so we need to clean it up */
	google_thermal_odpm_unregister_client(data->reg_name,
						&data->test_nb,
						ODPM_TEST_POLLING_INTERVAL_200MS);
}

static void odpm_register_error_cases_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;

	/* Test invalid regulator name (NULL) */
	odpm_test_init_data(data);

	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(NULL, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS),
			-EINVAL);
	KUNIT_EXPECT_TRUE(test, list_empty(&odpm_regulator_groups));

	/* Test kzalloc failure */
	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);
	data->kzalloc_fail_on_count = 3; /* Fail on client allocation */

	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS),
			-ENOMEM);
	KUNIT_EXPECT_TRUE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_FALSE(test, data->sched_dw_called);

	/* Test notifier registration failure */
	odpm_test_init_data(data);
	strscpy(data->reg_name, ODPM_TEST_RAIL_NAME, ODPM_REGULATOR_NAME_LEN);
	data->b_notifier_reg_ret = -EBUSY;

	KUNIT_EXPECT_EQ(test,
			google_thermal_odpm_register_client(data->reg_name, &data->test_nb,
							ODPM_TEST_POLLING_INTERVAL_200MS),
			-EBUSY);
	KUNIT_EXPECT_TRUE(test, list_empty(&odpm_regulator_groups));
	KUNIT_EXPECT_FALSE(test, data->sched_dw_called);
}

static void powercap_test_suite_exit(struct kunit_suite *suite)
{
	int i;

	if (!IS_ERR_OR_NULL(pct_test))
		powercap_unregister_control_type(pct_test);
	kfree(opp_table);
	kfree(test_data);
	kfree(test_policy);

	if (gpc_algo_test_data) {
		for (i = 0; i < GPC_ALGO_CHILD_CT; i++)
			kfree(gpc_algo_test_data->children_gpc[i].opp_table);
	}
	kfree(weights_algo_test_data);
	platform_device_put(fake_pdev);
}

static void powercap_test_init_cpu(void)
{
	int i = 0;

	test_policy = kzalloc(sizeof(*test_policy), GFP_KERNEL);
	test_policy->cpu = GPC_TEST_CPU;
	for (i = 0; i < GPC_TEST_CPU_MAX; i++)
		cpumask_set_cpu(i, test_policy->related_cpus);
}

static int powercap_test_suite_init(struct kunit_suite *suite)
{
	int i = 0;
	int j;

	weights_algo_test_data = kzalloc(sizeof(*weights_algo_test_data), GFP_KERNEL);
	if (!weights_algo_test_data)
		return -ENOMEM;

	gpc_algo_test_data = &weights_algo_test_data->common;

	gpowercap_init(&gpc_algo_test_data->parent_gpc, NULL);

	for (i = 0; i < GPC_ALGO_CHILD_CT; i++) {
		struct gpowercap *child = &gpc_algo_test_data->children_gpc[i];

		gpowercap_init(child, &test_ops);
		child->num_opps = GPC_TEST_OPP_CT;
		child->opp_table = kcalloc(GPC_TEST_OPP_CT,
						 sizeof(*child->opp_table), GFP_KERNEL);
		if (!child->opp_table)
			return -ENOMEM;
		for (j = 0; j < GPC_TEST_OPP_CT; j++) {
			child->opp_table[j].power = (GPC_POWER_INIT + GPC_POWER_INCREMENT * j);
			child->opp_table[j].freq = (GPC_FREQ_INIT + GPC_FREQ_INCREMENT * j);
		}
		child->power_min = child->opp_table[0].power;
		child->power_max = child->opp_table[GPC_TEST_OPP_CT - 1].power;
		child->power_limit = child->power_max;
		child->userspace_power_limit = child->power_max;
		child->parent_power_limit = child->power_max;
	}

	pct_test = gpc_powercap_register_control_type(NULL, "gpc_test", NULL);

	fake_pdev = platform_device_alloc("mock_gpc-pdevice", -1);
	fake_pdev->dev.of_node = &gpc_root_dn;
	gpc_root_dn.name = "powercap-tree";
	gpc_root_dn.child = &gpc_child1_dn;
	gpc_child1_dn.name = "virtual-child";
	gpc_child1_dn.parent = &gpc_root_dn;
	gpc_child1_dn.sibling = &gpc_child2_dn;
	gpc_child2_dn.name = "cpu-child";
	gpc_child2_dn.parent = &gpc_root_dn;
	gpc_dev_dn.name = "cpu-dev";

	powercap_test_init_cpu();

	opp_table = kcalloc(GPC_TEST_OPP_CT, sizeof(*opp_table), GFP_KERNEL);
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		opp_table[i].power = (GPC_POWER_INIT + GPC_POWER_INCREMENT * i);
		opp_table[i].freq = (GPC_FREQ_INIT + GPC_FREQ_INCREMENT * i);
		opp_table[i].voltage = (GPC_VOLT_INIT + GPC_VOLT_INCREMENT * i);
	}
	test_data = kzalloc(sizeof(*test_data), GFP_KERNEL);
	return 0;
}

static int powercap_test_init(struct kunit *test)
{
	test->priv = test_data;

	gpc_test_init_data(test_data);
	kunit_activate_static_stub(test, gpc_powercap_register_control_type,
				   mock_powercap_register_control_type);
	kunit_activate_static_stub(test, gpc_powercap_unregister_control_type,
				   mock_powercap_unregister_control_type);
	kunit_activate_static_stub(test, gpc_powercap_register_zone, mock_powercap_register_zone);
	kunit_activate_static_stub(test, gpc_powercap_unregister_zone,
					   mock_powercap_unregister_zone);
	kunit_activate_static_stub(test, gpc_cdev_devfreq_init, mock_cdev_devfreq_init);
	kunit_activate_static_stub(test, gpc_cpufreq_cpu_get, mock_cpufreq_cpu_get);
	kunit_activate_static_stub(test, gpowercap_register, mock_gpowercap_register);
	kunit_activate_static_stub(test, gpc_google_pm_qos_add_cpufreq_request,
				   mock_google_pm_qos_add_cpufreq_request);
	kunit_activate_static_stub(test, gpc_weights_of_property_count_strings,
				   mock_gpc_weights_of_property_count_strings);
	kunit_activate_static_stub(test, gpc_weights_of_property_read_string_index,
				   mock_gpc_weights_of_property_read_string_index);
	kunit_activate_static_stub(test, gpc_weights_kstrtou32, mock_gpc_weights_kstrtou32);
	kunit_activate_static_stub(test, gpc_devfreq_cancel_delayed_work_sync,
				   mock_gpc_devfreq_cancel_delayed_work_sync);
	kunit_activate_static_stub(test, gpc_google_pm_qos_remove_cpufreq_request,
				   mock_google_pm_qos_remove_cpufreq_request);
	kunit_activate_static_stub(test, gpc_cpufreq_quick_get, mock_cpufreq_quick_get);
	kunit_activate_static_stub(test, gpc_cdev_cpufreq_get_opp_count,
				   mock_cdev_cpufreq_get_opp_count);
	kunit_activate_static_stub(test, gpc_cdev_cpufreq_update_opp_table,
				   mock_cdev_cpufreq_update_opp_table);
	kunit_activate_static_stub(test, gpc_device_create_file, mock_device_create_file);
	kunit_activate_static_stub(test, gpc_device_remove_file, mock_device_remove_file);
	kunit_activate_static_stub(test, gpc_freq_qos_update_request,
				   mock_freq_qos_update_request);
	kunit_activate_static_stub(test, gpc_cpufreq_cpu_put, mock_cpufreq_cpu_put);
	kunit_activate_static_stub(test, gpc_warn_on_once, mock_warn_on_once);
	kunit_activate_static_stub(test, gpc_cdev_pm_qos_update_request,
				   mock_gpc_cdev_pm_qos_update_request);
	kunit_activate_static_stub(test, gpc_of_property_read_u32, mock_of_property_read_u32);
	kunit_activate_static_stub(test, gpc_of_parse_phandle, mock_of_parse_phandle);
	kunit_activate_static_stub(test, cdev_dev_pm_qos_update_request,
				   mock_cdev_dev_pm_qos_update_request);
	kunit_activate_static_stub(test, cdev_pm_qos_add_devfreq_request,
				   mock_cdev_pm_qos_add_devfreq_request);
	kunit_activate_static_stub(test, gpc_of_node_put, mock_of_node_put);
	kunit_activate_static_stub(test, gpc_of_find_node_by_path, mock_of_find_node_by_path);
	kunit_activate_static_stub(test, gpc_match_of_node, mock_match_of_node);
	kunit_activate_static_stub(test, godpm_get_rail_energy, mock_godpm_get_rail_energy);
	kunit_activate_static_stub(test, godpm_kzalloc, mock_godpm_kzalloc);
	kunit_activate_static_stub(test, godpm_kfree, mock_godpm_kfree);
	kunit_activate_static_stub(test, godpm_blocking_notifier_chain_register,
				   mock_godpm_blocking_notifier_chain_register);
	kunit_activate_static_stub(test, godpm_blocking_notifier_chain_unregister,
				   mock_godpm_blocking_notifier_chain_unregister);
	kunit_activate_static_stub(test, godpm_blocking_notifier_call_chain,
				   mock_godpm_blocking_notifier_call_chain);
	kunit_activate_static_stub(test, godpm_schedule_delayed_work,
				   mock_godpm_schedule_delayed_work);
	kunit_activate_static_stub(test, godpm_mod_delayed_work, mock_godpm_mod_delayed_work);
	kunit_activate_static_stub(test, godpm_cancel_delayed_work_sync,
				   mock_godpm_cancel_delayed_work_sync);
	kunit_activate_static_stub(test, godpm_ktime_get, mock_godpm_ktime_get);
	kunit_activate_static_stub(test, gpc_stats_update, mock_gpc_stats_update);

	return 0;
}

static void powercap_test_exit(struct kunit *test)
{
	struct gpowercap_cpu *pos, *n;

	list_for_each_entry_safe(pos, n, &gpowercap_cpu_list, node) {
		__gpc_cpu_pd_release(&pos->gpowercap);
	}
	kunit_deactivate_static_stub(test, gpc_powercap_register_control_type);
	kunit_deactivate_static_stub(test, gpc_powercap_unregister_control_type);
	kunit_deactivate_static_stub(test, gpc_powercap_register_zone);
	kunit_deactivate_static_stub(test, gpc_powercap_unregister_zone);
	kunit_deactivate_static_stub(test, gpc_cdev_devfreq_init);
	kunit_deactivate_static_stub(test, gpc_cpufreq_cpu_get);
	kunit_deactivate_static_stub(test, gpowercap_register);
	kunit_deactivate_static_stub(test, gpc_google_pm_qos_add_cpufreq_request);
	kunit_deactivate_static_stub(test, gpc_google_pm_qos_remove_cpufreq_request);
	kunit_deactivate_static_stub(test, gpc_cpufreq_quick_get);
	kunit_deactivate_static_stub(test, gpc_cdev_cpufreq_get_opp_count);
	kunit_deactivate_static_stub(test, gpc_cdev_cpufreq_update_opp_table);
	kunit_deactivate_static_stub(test, gpc_device_create_file);
	kunit_deactivate_static_stub(test, gpc_device_remove_file);
	kunit_deactivate_static_stub(test, gpc_freq_qos_update_request);
	kunit_deactivate_static_stub(test, gpc_cpufreq_cpu_put);
	kunit_deactivate_static_stub(test, gpc_warn_on_once);
	kunit_deactivate_static_stub(test, gpc_weights_of_property_count_strings);
	kunit_deactivate_static_stub(test, gpc_weights_of_property_read_string_index);
	kunit_deactivate_static_stub(test, gpc_weights_kstrtou32);
	kunit_deactivate_static_stub(test, gpc_of_property_read_u32);
	kunit_deactivate_static_stub(test, gpc_cdev_pm_qos_update_request);
	kunit_deactivate_static_stub(test, gpc_devfreq_cancel_delayed_work_sync);
	kunit_deactivate_static_stub(test, gpc_of_parse_phandle);
	kunit_deactivate_static_stub(test, cdev_dev_pm_qos_update_request);
	kunit_deactivate_static_stub(test, cdev_pm_qos_add_devfreq_request);
	kunit_deactivate_static_stub(test, gpc_of_node_put);
	kunit_deactivate_static_stub(test, gpc_of_find_node_by_path);
	kunit_deactivate_static_stub(test, gpc_match_of_node);
	kunit_deactivate_static_stub(test, godpm_get_rail_energy);
	kunit_deactivate_static_stub(test, godpm_kzalloc);
	kunit_deactivate_static_stub(test, godpm_kfree);
	kunit_deactivate_static_stub(test, godpm_blocking_notifier_chain_register);
	kunit_deactivate_static_stub(test, godpm_blocking_notifier_chain_unregister);
	kunit_deactivate_static_stub(test, godpm_blocking_notifier_call_chain);
	kunit_deactivate_static_stub(test, godpm_schedule_delayed_work);
	kunit_deactivate_static_stub(test, godpm_mod_delayed_work);
	kunit_deactivate_static_stub(test, godpm_cancel_delayed_work_sync);
	kunit_deactivate_static_stub(test, godpm_ktime_get);
	kunit_deactivate_static_stub(test, gpc_stats_update);
}

static struct gpowercap *volt_algo_test_init(struct kunit *test)
{
	struct powercap_algo_test_data *data = gpc_algo_test_data;
	struct gpowercap *gpc_volt_algo;
	int i;

	gpc_volt_algo_dn.name = "test_volt";
	gpc_volt_algo = __gpc_volt_algo_setup(&gpc_volt_algo_dn, &data->parent_gpc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_volt_algo);

	for (i = 0; i < GPC_ALGO_CHILD_CT; i++)
		list_add_tail(&data->children_gpc[i].siblings, &gpc_volt_algo->children);

	return gpc_volt_algo;
}

static void volt_algo_test_exit(struct gpowercap *gpc_volt_algo)
{
	struct powercap_algo_test_data *data = gpc_algo_test_data;

	list_del(&data->children_gpc[0].siblings);
	list_del(&data->children_gpc[1].siblings);
	__gpc_volt_algo_release(gpc_volt_algo);
}

static void powercap_volt_algo_setup_release_test(struct kunit *test)
{
	struct powercap_algo_test_data *data = gpc_algo_test_data;
	struct gpowercap *gpc_volt_algo;

	/* Success case */
	gpc_volt_algo_dn.name = "test_volt";
	gpc_volt_algo = __gpc_volt_algo_setup(&gpc_volt_algo_dn, &data->parent_gpc);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpc_volt_algo);
	KUNIT_EXPECT_TRUE(test, gpc_register_called);
	__gpc_volt_algo_release(gpc_volt_algo);

	/* Failure cases */
	gpc_register_called = false;
	gpc_volt_algo = __gpc_volt_algo_setup(NULL, &data->parent_gpc);
	KUNIT_EXPECT_TRUE(test, IS_ERR(gpc_volt_algo));
	KUNIT_EXPECT_FALSE(test, gpc_register_called);

	gpc_volt_algo = __gpc_volt_algo_setup(&gpc_volt_algo_dn, NULL);
	KUNIT_EXPECT_TRUE(test, IS_ERR(gpc_volt_algo));
	KUNIT_EXPECT_FALSE(test, gpc_register_called);

	gpc_register_ret = -ENOMEM;
	gpc_volt_algo = __gpc_volt_algo_setup(&gpc_volt_algo_dn, &data->parent_gpc);
	KUNIT_EXPECT_TRUE(test, IS_ERR(gpc_volt_algo));
	KUNIT_EXPECT_TRUE(test, gpc_register_called);
	gpc_register_ret = 0;
}

static void powercap_volt_algo_evaluate_test(struct kunit *test)
{
	struct powercap_algo_test_data *data = gpc_algo_test_data;
	struct gpowercap *gpc_volt_algo;

	gpc_volt_algo = volt_algo_test_init(test);

	/* Success case */
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_evaluate(gpc_volt_algo), 0);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->num_opps, GPC_TEST_OPP_CT);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->userspace_power_limit, gpc_volt_algo->power_max);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->parent_power_limit, gpc_volt_algo->power_max);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->power_limit, gpc_volt_algo->power_max);

	/* No children */
	list_del(&data->children_gpc[0].siblings);
	list_del(&data->children_gpc[1].siblings);
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_evaluate(gpc_volt_algo), 0);
	list_add_tail(&data->children_gpc[0].siblings, &gpc_volt_algo->children);
	list_add_tail(&data->children_gpc[1].siblings, &gpc_volt_algo->children);

	/* load static table fails (child count mismatch) */
	list_del(&data->children_gpc[1].siblings);
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_evaluate(gpc_volt_algo), -EINVAL);
	list_add_tail(&data->children_gpc[1].siblings, &gpc_volt_algo->children);

	/* of_find_node fails */
	of_find_node_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_evaluate(gpc_volt_algo), -ENODEV);
	of_find_node_ret = 0;

	/* of_match_node fails */
	match_node_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_evaluate(gpc_volt_algo), -ENODEV);
	match_node_ret = 0;

	volt_algo_test_exit(gpc_volt_algo);
}

static void powercap_volt_algo_set_get_power_test(struct kunit *test)
{
	struct powercap_algo_test_data *data = gpc_algo_test_data;
	struct gpowercap *gpc_volt_algo;
	u64 power, power_limit_req;

	gpc_volt_algo = volt_algo_test_init(test);
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_evaluate(gpc_volt_algo), 0);

	/* Set power limit */
	power_limit_req = GPC_POWER_INIT * GPC_ALGO_CHILD_CT;
	test_data->stats_update_called[GPC_STAT_POWER_LIMIT] = false;
	power = __gpc_volt_algo_set_power_limit(gpc_volt_algo, power_limit_req);
	KUNIT_EXPECT_EQ(test, power, power_limit_req);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], GPC_POWER_INIT);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], GPC_POWER_INIT);
	KUNIT_EXPECT_TRUE(test, test_data->stats_update_called[GPC_STAT_POWER_LIMIT]);
	KUNIT_EXPECT_EQ(test, test_data->stats_update_value[GPC_STAT_POWER_LIMIT],
			power_limit_req);

	/* Set power limit above max */
	power = __gpc_volt_algo_set_power_limit(gpc_volt_algo, 9999999);
	/* child0(1.3MHz):2.5W + child1(1.3MHz):2.5W */
	KUNIT_EXPECT_EQ(test, power, 5000000);

	/* Set power limit with no opp_table */
	to_gpowercap_volt_algo(gpc_volt_algo)->opp_table = NULL;
	power = __gpc_volt_algo_set_power_limit(gpc_volt_algo, 1000);
	KUNIT_EXPECT_EQ(test, power, 0);
	to_gpowercap_volt_algo(gpc_volt_algo)->opp_table = gpc_volt_algo->opp_table;

	/* Get power */
	gpc_algo_test_data->children_get_power_val[0] = 100;
	gpc_algo_test_data->children_get_power_val[1] = 200;
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_get_power(gpc_volt_algo), 300);

	/* Get power with one child failing */
	gpc_algo_test_data->children_gpc[0].num_opps = 0;
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_get_power(gpc_volt_algo), 200);
	gpc_algo_test_data->children_gpc[0].num_opps = GPC_TEST_OPP_CT;

	/* Get power with no children */
	list_del(&data->children_gpc[0].siblings);
	list_del(&data->children_gpc[1].siblings);
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_get_power(gpc_volt_algo), 0);

	__gpc_volt_algo_release(gpc_volt_algo);
}

static void powercap_volt_algo_update_power_test(struct kunit *test)
{
	struct gpowercap *gpc_volt_algo;
	struct gpowercap_volt_algo *gpc_volt;

	gpc_volt_algo = volt_algo_test_init(test);
	gpc_volt = to_gpowercap_volt_algo(gpc_volt_algo);

	/* No OPPs yet */
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_update_power_uw(gpc_volt_algo), 0);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->num_opps, 0);

	/* With OPPs */
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_evaluate(gpc_volt_algo), 0);
	KUNIT_EXPECT_EQ(test, __gpc_volt_algo_update_power_uw(gpc_volt_algo), 0);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->num_opps, 3);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->power_min, 1000000);
	KUNIT_EXPECT_EQ(test, gpc_volt_algo->power_max, 5000000);

	volt_algo_test_exit(gpc_volt_algo);
}

static void gpc_weights_algo_test_data_init(struct kunit *test)
{
	weights_algo_test_data->child_names[0] = "child0";
	weights_algo_test_data->child_names[1] = "child1";
	weights_algo_test_data->child_weights_str[0] = "2";
	weights_algo_test_data->child_weights_str[1] = "3";
	weights_algo_test_data->child_weights[0] = 2;
	weights_algo_test_data->child_weights[1] = 3;
	weights_algo_test_data->dn.name = "weights_algo_node";
	weights_algo_test_data->of_count_strings_ret = 4;
	weights_algo_test_data->of_read_string_ret = 0;
	weights_algo_test_data->kstrtou32_ret = 0;
	weights_algo_test_data->kstrtou32_fail_on_str = NULL;
	gpc_register_ret = 0;

	gpc_algo_test_data->children_gpc[0].zone.name =
		(char *) weights_algo_test_data->child_names[0];
	gpc_algo_test_data->children_gpc[1].zone.name =
		(char *) weights_algo_test_data->child_names[1];

	gpc_algo_test_data->children_power_limit[0] = 0;
	gpc_algo_test_data->children_power_limit[1] = 0;
	gpc_algo_test_data->children_get_power_val[0] = 800000;
	gpc_algo_test_data->children_get_power_val[1] = 1200000;
	gpc_algo_test_data->children_get_power_ret[0] = 0;
	gpc_algo_test_data->children_get_power_ret[1] = 0;
}

static void powercap_weights_algo_setup_release_test(struct kunit *test)
{
	struct gpowercap *gpc_weights;
	struct gpowercap_weights_algo *gpc_weights_algo;
	struct gpowercap_weight_child *wc;
	int i = 0;

	gpc_weights_algo_test_data_init(test);

	/* Success case */
	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, gpc_weights);
	KUNIT_EXPECT_TRUE(test, gpc_register_called);

	gpc_weights_algo = to_gpowercap_weights_algo(gpc_weights);
	i = 0;
	list_for_each_entry(wc, &gpc_weights_algo->weighted_children, node) {
		KUNIT_EXPECT_STREQ(test, wc->name, weights_algo_test_data->child_names[i]);
		KUNIT_EXPECT_EQ(test, wc->weight, weights_algo_test_data->child_weights[i]);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, GPC_ALGO_CHILD_CT);
	__gpc_weights_algo_release(gpc_weights);
	KUNIT_EXPECT_TRUE(test, test_data->gpc_device_remove_file_called);

	/* Failure cases */
	gpc_register_called = false;
	gpc_weights = __gpc_weights_algo_setup(NULL, &gpc_algo_test_data->parent_gpc);
	KUNIT_EXPECT_TRUE(test, IS_ERR(gpc_weights));
	KUNIT_EXPECT_FALSE(test, gpc_register_called);

	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn, NULL);
	KUNIT_EXPECT_TRUE(test, IS_ERR(gpc_weights));
	KUNIT_EXPECT_FALSE(test, gpc_register_called);

	gpc_register_ret = -ENOMEM;
	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_EXPECT_TRUE(test, IS_ERR(gpc_weights));
	KUNIT_EXPECT_TRUE(test, gpc_register_called);
	gpc_register_ret = 0;

	gpc_device_create_file_ret = -EINVAL;
	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_EXPECT_TRUE(test, IS_ERR(gpc_weights));
	gpc_device_create_file_ret = 0;

	/* DT read failures */
	gpc_weights_algo_test_data_init(test);
	weights_algo_test_data->of_read_string_ret = -EINVAL;
	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	gpc_weights_algo = to_gpowercap_weights_algo(gpc_weights);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpc_weights_algo->weighted_children));
	__gpc_weights_algo_release(gpc_weights);

	gpc_weights_algo_test_data_init(test);
	weights_algo_test_data->of_count_strings_ret = 3; /* Odd number is invalid */
	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	gpc_weights_algo = to_gpowercap_weights_algo(gpc_weights);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpc_weights_algo->weighted_children));
	__gpc_weights_algo_release(gpc_weights);

	gpc_weights_algo_test_data_init(test);
	weights_algo_test_data->kstrtou32_ret = -EINVAL;
	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	gpc_weights_algo = to_gpowercap_weights_algo(gpc_weights);
	KUNIT_EXPECT_TRUE(test, list_empty(&gpc_weights_algo->weighted_children));
	__gpc_weights_algo_release(gpc_weights);

	/* DT partial read failures */
	gpc_weights_algo_test_data_init(test);
	weights_algo_test_data->child_weights_str[1] = "invalid_weight";
	weights_algo_test_data->kstrtou32_fail_on_str = "invalid_weight";
	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	gpc_weights_algo = to_gpowercap_weights_algo(gpc_weights);
	KUNIT_EXPECT_FALSE(test, list_empty(&gpc_weights_algo->weighted_children));
	i = 0;
	list_for_each_entry(wc, &gpc_weights_algo->weighted_children, node) {
		KUNIT_EXPECT_STREQ(test, wc->name, weights_algo_test_data->child_names[0]);
		KUNIT_EXPECT_EQ(test, wc->weight, weights_algo_test_data->child_weights[0]);
		i++;
	}
	KUNIT_EXPECT_EQ(test, i, 1);
	__gpc_weights_algo_release(gpc_weights);
}

static void powercap_weights_algo_evaluate_test(struct kunit *test)
{
	struct gpowercap *gpc_weights;
	struct gpowercap_weights_algo *gpc_weights_algo;
	struct gpowercap *new_child;
	struct gpowercap_weight_child *wc;
	bool new_child_found;

	gpc_weights_algo_test_data_init(test);

	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_weights);
	gpc_weights_algo = to_gpowercap_weights_algo(gpc_weights);

	list_add_tail(&gpc_algo_test_data->children_gpc[0].siblings, &gpc_weights->children);
	list_add_tail(&gpc_algo_test_data->children_gpc[1].siblings, &gpc_weights->children);

	KUNIT_EXPECT_EQ(test, __gpc_weights_algo_evaluate(gpc_weights), 0);
	KUNIT_EXPECT_EQ(test, gpc_weights_algo->total_weight, 5);

	list_for_each_entry(wc, &gpc_weights_algo->weighted_children, node) {
		if (!strcmp(wc->name, "child0"))
			KUNIT_EXPECT_PTR_EQ(test, wc->gpc, &gpc_algo_test_data->children_gpc[0]);
		else if (!strcmp(wc->name, "child1"))
			KUNIT_EXPECT_PTR_EQ(test, wc->gpc, &gpc_algo_test_data->children_gpc[1]);
	}

	new_child = kunit_kzalloc(test, sizeof(*new_child), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, new_child);
	gpowercap_init(new_child, &test_ops);
	new_child->zone.name = "new_child";
	list_add_tail(&new_child->siblings, &gpc_weights->children);

	KUNIT_EXPECT_EQ(test, __gpc_weights_algo_evaluate(gpc_weights), 0);
	KUNIT_EXPECT_EQ(test, gpc_weights_algo->total_weight, 6);

	new_child_found = false;
	list_for_each_entry(wc, &gpc_weights_algo->weighted_children, node) {
		if (!strcmp(wc->name, "new_child")) {
			new_child_found = true;
			KUNIT_EXPECT_PTR_EQ(test, wc->gpc, new_child);
			KUNIT_EXPECT_EQ(test, wc->weight, GPC_WEIGHTS_ALGO_DEFAULT_WEIGHT);
		}
	}
	KUNIT_EXPECT_TRUE(test, new_child_found);

	__gpc_weights_algo_release(gpc_weights);
}

static void powercap_weights_algo_set_get_power_test(struct kunit *test)
{
	struct gpowercap *gpc_weights;
	u64 power_limit_req = 2000000;
	int temp_opp;

	gpc_weights_algo_test_data_init(test);

	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_weights);

	list_add_tail(&gpc_algo_test_data->children_gpc[0].siblings, &gpc_weights->children);
	list_add_tail(&gpc_algo_test_data->children_gpc[1].siblings, &gpc_weights->children);
	__gpc_weights_algo_evaluate(gpc_weights);

	/* Set power limit */
	test_data->stats_update_called[GPC_STAT_POWER_LIMIT] = false;
	__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], 800000);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], 1200000);
	KUNIT_EXPECT_TRUE(test, test_data->stats_update_called[GPC_STAT_POWER_LIMIT]);
	KUNIT_EXPECT_EQ(test, test_data->stats_update_value[GPC_STAT_POWER_LIMIT],
			power_limit_req);

	/*
	 * Case 1: Child 0 yields to Child 1 (Forward distribution).
	 * Child 0 (weight 2) needs 800k, uses < threshold -> Donor.
	 * Child 1 (weight 3) needs 1200k, uses > threshold -> Receiver.
	 */
	{
		u64 base0 = 800000;
		u64 base1 = 1200000;
		u64 thresh0 = div_u64(base0 * GPC_WEIGHTS_ALGO_RECEIVER_THRESHOLD_PERCENT, 100);
		u64 cur0 = thresh0 - 1000;
		u64 cur1 = base1;
		u64 surplus = base0 - cur0;

		gpc_algo_test_data->children_get_power_val[0] = cur0;
		gpc_algo_test_data->children_get_power_val[1] = cur1;
		gpc_algo_test_data->children_gpc[0].current_power_uw = cur0;
		gpc_algo_test_data->children_gpc[1].current_power_uw = cur1;
		/*
		 * child's power_limit is set to power_max upon init.
		 * A lower value is set above.
		 * To test redistribution, reset power_limit to max to trigger the logic.
		 */
		gpc_algo_test_data->children_gpc[0].power_limit =
			gpc_algo_test_data->children_gpc[0].power_max;
		gpc_algo_test_data->children_gpc[1].power_limit =
			gpc_algo_test_data->children_gpc[1].power_max;
		__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], base0);
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], base1 + surplus);
	}

	/*
	 * Case 2: Child 1 yields to Child 0 (Backward distribution).
	 * Child 0 (weight 2) needs 800k, uses > threshold -> Receiver.
	 * Child 1 (weight 3) needs 1200k, uses < threshold -> Donor.
	 */
	{
		u64 base0 = 800000;
		u64 base1 = 1200000;
		u64 thresh1 = div_u64(base1 * GPC_WEIGHTS_ALGO_RECEIVER_THRESHOLD_PERCENT, 100);
		u64 cur0 = base0;
		u64 cur1 = thresh1 - 1000;
		u64 surplus = base1 - cur1;

		gpc_algo_test_data->children_get_power_val[0] = cur0;
		gpc_algo_test_data->children_get_power_val[1] = cur1;
		gpc_algo_test_data->children_gpc[0].current_power_uw = cur0;
		gpc_algo_test_data->children_gpc[1].current_power_uw = cur1;

		gpc_algo_test_data->children_gpc[0].power_limit =
			gpc_algo_test_data->children_gpc[0].power_max;
		gpc_algo_test_data->children_gpc[1].power_limit =
			gpc_algo_test_data->children_gpc[1].power_max;
		__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], base0 + surplus);
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], base1);
	}

	/*
	 * Case 4: Child 0 userspace limit restricts it (Surplus redistribution).
	 * Power limit: 2000000
	 * Base: Child 0: 800000, Child 1: 1200000.
	 * Child 0 userspace limit: 600000.
	 * Child 0 effective limit: 600000.
	 * Surplus: 200000.
	 * Child 1 receives 200000.
	 * Result: Child 0 limit=600000, Child 1 limit=1400000.
	 */
	gpc_algo_test_data->children_gpc[0].power_limit =
		gpc_algo_test_data->children_gpc[0].power_max;
	gpc_algo_test_data->children_gpc[1].power_limit =
		gpc_algo_test_data->children_gpc[1].power_max;
	gpc_algo_test_data->children_gpc[0].current_power_uw = 800000;
	gpc_algo_test_data->children_gpc[1].current_power_uw = 1200000;
	gpc_algo_test_data->children_gpc[0].userspace_power_limit = 600000;

	__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], 600000);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], 1400000);

	gpc_algo_test_data->children_gpc[0].userspace_power_limit =
		gpc_algo_test_data->children_gpc[0].power_max;

	/* Restore values for get_power test */
	gpc_algo_test_data->children_get_power_val[0] = 800000;
	gpc_algo_test_data->children_get_power_val[1] = 1200000;

	/* Get power */
	KUNIT_EXPECT_EQ(test, __gpc_weights_algo_get_power(gpc_weights), 2000000);

	/* Get power with one child failing */
	temp_opp = gpc_algo_test_data->children_gpc[0].num_opps;
	gpc_algo_test_data->children_gpc[0].num_opps = 0;
	KUNIT_EXPECT_EQ(test, __gpc_weights_algo_get_power(gpc_weights), 1200000);
	gpc_algo_test_data->children_gpc[0].num_opps = temp_opp;

	__gpc_weights_algo_release(gpc_weights);
}

static void powercap_weights_algo_set_power_min_max_test(struct kunit *test)
{
	struct gpowercap *gpc_weights;
	u64 power_limit_req;

	gpc_weights_algo_test_data_init(test);

	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_weights);

	list_add_tail(&gpc_algo_test_data->children_gpc[0].siblings, &gpc_weights->children);
	list_add_tail(&gpc_algo_test_data->children_gpc[1].siblings, &gpc_weights->children);
	__gpc_weights_algo_evaluate(gpc_weights);

	/* Set power limit to MAX */
	power_limit_req = gpc_weights->power_max;
	__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
	/*
	 * Children power limit should be their respective max power
	 * child0 max: 1500000, child1 max: 1500000
	 */
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0],
			opp_table[GPC_TEST_OPP_CT - 1].power);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1],
			opp_table[GPC_TEST_OPP_CT - 1].power);

	/* Set power limit to MIN */
	power_limit_req = gpc_weights->power_min;
	__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
	/*
	 * Children power limit should be their respective min power
	 * child0 min: 500000, child1 min: 500000
	 */
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0],
			opp_table[0].power);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1],
			opp_table[0].power);

	__gpc_weights_algo_release(gpc_weights);
}

static void powercap_weights_algo_dist_weights_sysfs_test(struct kunit *test)
{
	struct gpowercap *gpc_weights;
	char *buf;
	ssize_t ret;
	const char *test_str;

	gpc_weights_algo_test_data_init(test);

	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_weights);

	list_add_tail(&gpc_algo_test_data->children_gpc[0].siblings, &gpc_weights->children);
	list_add_tail(&gpc_algo_test_data->children_gpc[1].siblings, &gpc_weights->children);
	__gpc_weights_algo_evaluate(gpc_weights);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	/* Test show functionality */
	ret = power_distribution_weights_show(&gpc_weights->zone.dev, NULL, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, buf, "child0 : 2\nchild1 : 3\n");

	/* Test store functionality - valid case */
	test_str = "child0:4,child1:6";
	ret = power_distribution_weights_store(&gpc_weights->zone.dev, NULL, test_str,
					       strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, strlen(test_str));
	KUNIT_EXPECT_EQ(test, to_gpowercap_weights_algo(gpc_weights)->total_weight, 10);

	memset(buf, 0, PAGE_SIZE);
	ret = power_distribution_weights_show(&gpc_weights->zone.dev, NULL, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, buf, "child0 : 4\nchild1 : 6\n");

	/* Test store functionality - partial update */
	test_str = "child0:5";
	ret = power_distribution_weights_store(&gpc_weights->zone.dev, NULL, test_str,
					       strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, strlen(test_str));
	/* child0 was 4, now 5. child1 was 6. Total = 11 */
	KUNIT_EXPECT_EQ(test, to_gpowercap_weights_algo(gpc_weights)->total_weight, 11);

	memset(buf, 0, PAGE_SIZE);
	ret = power_distribution_weights_show(&gpc_weights->zone.dev, NULL, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, buf, "child0 : 5\nchild1 : 6\n");

	/* Test store functionality - incorrect child count */
	test_str = "child0:1,child1:2,child2:3";
	ret = power_distribution_weights_store(&gpc_weights->zone.dev, NULL, test_str,
					       strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Test store functionality - non-numerical weight */
	test_str = "child0:a,child1:6";
	ret = power_distribution_weights_store(&gpc_weights->zone.dev, NULL, test_str,
					       strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Test store functionality - non-matching child name */
	test_str = "child_bad:5,child1:7";
	ret = power_distribution_weights_store(&gpc_weights->zone.dev, NULL, test_str,
					       strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
	memset(buf, 0, PAGE_SIZE);
	ret = power_distribution_weights_show(&gpc_weights->zone.dev, NULL, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, buf, "child0 : 5\nchild1 : 6\n");

	/* Test store functionality - incorrect string separators */
	test_str = "child0;5,child1;8";
	ret = power_distribution_weights_store(&gpc_weights->zone.dev, NULL, test_str,
					       strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	kunit_kfree(test, buf);
	__gpc_weights_algo_release(gpc_weights);
}

static void powercap_weights_algo_redist_threshold_sysfs_test(struct kunit *test)
{
	struct gpowercap *gpc_weights;
	char *buf;
	ssize_t ret;
	const char *test_str;
	char expected_str[16];

	gpc_weights_algo_test_data_init(test);

	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_weights);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	/* Test show functionality - initial default value */
	ret = power_redistribution_threshold_show(&gpc_weights->zone.dev, NULL, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	snprintf(expected_str, sizeof(expected_str), "%d\n",
		 GPC_WEIGHTS_ALGO_RECEIVER_THRESHOLD_PERCENT);
	KUNIT_EXPECT_STREQ(test, buf, expected_str);

	/* Test store functionality - valid case */
	test_str = "75";
	ret = power_redistribution_threshold_store(&gpc_weights->zone.dev, NULL, test_str,
						   strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, strlen(test_str));
	KUNIT_EXPECT_EQ(test, to_gpowercap_weights_algo(gpc_weights)->redistribution_threshold, 75);

	memset(buf, 0, PAGE_SIZE);
	ret = power_redistribution_threshold_show(&gpc_weights->zone.dev, NULL, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_STREQ(test, buf, "75\n");

	/* Test store functionality - invalid value > 100 */
	test_str = "105";
	ret = power_redistribution_threshold_store(&gpc_weights->zone.dev, NULL, test_str,
						   strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Test store functionality - non-numerical value */
	test_str = "abc";
	ret = power_redistribution_threshold_store(&gpc_weights->zone.dev, NULL, test_str,
						   strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Test store functionality - negative value */
	test_str = "-10";
	ret = power_redistribution_threshold_store(&gpc_weights->zone.dev, NULL, test_str,
						   strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	/* Test store functionality - invalid value 0 */
	test_str = "0";
	ret = power_redistribution_threshold_store(&gpc_weights->zone.dev, NULL, test_str,
						   strlen(test_str));
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);

	kunit_kfree(test, buf);
	__gpc_weights_algo_release(gpc_weights);
}

static void powercap_weights_algo_rebalance_test(struct kunit *test)
{
	struct gpowercap *gpc_weights;
	u64 power_limit_req = 2000000;
	struct gpowercap *child0, *child1;

	gpc_weights_algo_test_data_init(test);

	gpc_weights = __gpc_weights_algo_setup(&weights_algo_test_data->dn,
					       &gpc_algo_test_data->parent_gpc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, gpc_weights);

	child0 = &gpc_algo_test_data->children_gpc[0];
	child1 = &gpc_algo_test_data->children_gpc[1];

	list_add_tail(&child0->siblings, &gpc_weights->children);
	list_add_tail(&child1->siblings, &gpc_weights->children);
	/*
	 * Manually set parent since we are bypassing standard registration
	 * for children in this test setup.
	 */
	child0->parent = gpc_weights;
	child1->parent = gpc_weights;
	child0->power_max = opp_table[GPC_TEST_OPP_CT - 1].power;
	child1->power_max = opp_table[GPC_TEST_OPP_CT - 1].power;

	__gpc_weights_algo_evaluate(gpc_weights);
	KUNIT_EXPECT_EQ(test, gpc_weights->power_max, 5000000);

	/* Set power limit */
	__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
	/*
	 * Since we bypass __set_power_limit_uw, we must manually update
	 * the parent's power_limit to simulate the framework's behavior,
	 * as rebalance depends on it.
	 */
	gpc_weights->power_limit = power_limit_req;

	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], 800000);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], 1200000);

	/*
	 * Case 1: Child 0 yields to Child 1 (Forward distribution).
	 * Child 0 (weight 2) needs 800k, uses < threshold -> Donor.
	 * Child 1 (weight 3) needs 1200k, uses > threshold -> Receiver.
	 */
	{
		u64 base0 = 800000;
		u64 base1 = 1200000;
		u64 cur0 = div_u64(base0 * (GPC_WEIGHTS_ALGO_RECEIVER_THRESHOLD_PERCENT - 20), 100);
		u64 cur1 = base1;
		u64 surplus = base0 - cur0;

		/* Report child 0. No rebalance yet as child 1 is not updated. */
		KUNIT_EXPECT_FALSE(test, gpowercap_report_power_uw(child0, cur0));
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], base0);
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], base1);

		/* Report child 1. Rebalance should happen. */
		KUNIT_EXPECT_TRUE(test, gpowercap_report_power_uw(child1, cur1));
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], base0);
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], base1 + surplus);
	}

	/*
	 * Case 2: Child 1 yields to Child 0 (Backward distribution).
	 * Child 0 (weight 2) needs 800k, uses > threshold -> Receiver.
	 * Child 1 (weight 3) needs 1200k, uses < threshold -> Donor.
	 */
	{
		u64 base0 = 800000;
		u64 base1 = 1200000;
		u64 cur0 = base0;
		u64 cur1 = div_u64(base1 * (GPC_WEIGHTS_ALGO_RECEIVER_THRESHOLD_PERCENT - 20), 100);
		u64 surplus = base1 - cur1;

		KUNIT_EXPECT_FALSE(test, gpowercap_report_power_uw(child0, cur0));
		KUNIT_EXPECT_TRUE(test, gpowercap_report_power_uw(child1, cur1));

		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], base0 + surplus);
		KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], base1);
	}

	/*
	 * Case 3: Power limit is MAX. Rebalance shouldn't change limits from MAX.
	 */
	__gpc_weights_algo_set_power_limit(gpc_weights, gpc_weights->power_max);
	gpc_weights->power_limit = gpc_weights->power_max;

	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0],
			child0->power_max);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1],
			child1->power_max);

	/* Report power, rebalance shouldn't be triggered/effective for distribution */
	KUNIT_EXPECT_FALSE(test, gpowercap_report_power_uw(child0, 500000));
	/* Returns true because rebalance IS called, but limits remain MAX */
	KUNIT_EXPECT_TRUE(test, gpowercap_report_power_uw(child1, 500000));
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0],
			child0->power_max);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1],
			child1->power_max);

	/*
	 * Case 4: Rebalance with userspace limit on receiver.
	 * Parent Limit: 2000000.
	 * Child 0 (weight 2): Base 800k. Usage 500k. Surplus 300k.
	 * Child 1 (weight 3): Base 1200k. Usage 1200k.
	 * Child 1 Userspace Limit: 1300000.
	 * Expected:
	 *   Child 1 takes 100k surplus (capped at 1.3W).
	 *   Child 0 limit remains 800k.
	 *   Child 1 limit becomes 1300k.
	 */
	/* Restore parent limit */
	__gpc_weights_algo_set_power_limit(gpc_weights, power_limit_req);
	gpc_weights->power_limit = power_limit_req;

	/* Set userspace limit for Child 1 */
	child1->userspace_power_limit = 1300000;

	/* Report power to trigger rebalance */
	KUNIT_EXPECT_FALSE(test, gpowercap_report_power_uw(child0, 500000));
	KUNIT_EXPECT_TRUE(test, gpowercap_report_power_uw(child1, 1200000));

	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[0], 800000);
	KUNIT_EXPECT_EQ(test, gpc_algo_test_data->children_power_limit[1], 1300000);

	/* Cleanup */
	child1->userspace_power_limit = child1->power_max;

	__gpc_weights_algo_release(gpc_weights);
}

static void powercap_stats_init_exit_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap *gpc = &data->gpc;
	struct cdev_opp_table *ot;
	int i;

	/* Clean start */
	gpc_stats_exit(gpc);

	/* Setup OPP table */
	ot = kcalloc(GPC_TEST_OPP_CT, sizeof(*ot), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ot);
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		ot[i].power = (i + 1) * 1000;
		ot[i].freq = (i + 1) * 100000;
	}
	gpc->opp_table = ot;
	gpc->num_opps = GPC_TEST_OPP_CT;
	gpc->power_limit = 3000;

	/* Init */
	KUNIT_EXPECT_EQ(test, gpc_stats_init(gpc), 0);
	KUNIT_ASSERT_NOT_NULL(test, gpc->stats);

	/* Verify Enabled Stats */
	KUNIT_EXPECT_TRUE(test, gpc->stats->stats[GPC_STAT_POWER_LIMIT].enabled);
	KUNIT_EXPECT_TRUE(test, gpc->stats->stats[GPC_STAT_QOS].enabled);
	KUNIT_EXPECT_TRUE(test, gpc->stats->stats[GPC_STAT_UTIL].enabled);

	/* Verify Buckets */
	KUNIT_EXPECT_EQ(test, gpc->stats->stats[GPC_STAT_POWER_LIMIT].num_buckets,
			GPC_TEST_OPP_CT);
	KUNIT_EXPECT_EQ(test, gpc->stats->stats[GPC_STAT_QOS].num_buckets,
			GPC_TEST_OPP_CT);
	KUNIT_EXPECT_EQ(test, gpc->stats->stats[GPC_STAT_UTIL].num_buckets,
			GPC_STATS_UTIL_BUCKETS);

	/* Verify Bucket Values */
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		KUNIT_EXPECT_EQ(test,
			gpc->stats->stats[GPC_STAT_POWER_LIMIT].buckets[i].value, ot[i].power);
		KUNIT_EXPECT_EQ(test,
			gpc->stats->stats[GPC_STAT_QOS].buckets[i].value, ot[i].freq);
	}

	/* Exit */
	gpc_stats_exit(gpc);
	KUNIT_EXPECT_NULL(test, gpc->stats);

	kfree(ot);
	gpc->opp_table = NULL;
}

static void powercap_stats_virtual_nodes_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap *gpc = &data->gpc;
	struct cdev_opp_table *ot;
	int i;
	u64 min_p, max_p, step;

	/* Clean start */
	gpc_stats_exit(gpc);

	/* Setup Virtual Node OPP table (freq = 0) */
	ot = kcalloc(2, sizeof(*ot), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ot);
	ot[0].power = 1000;
	ot[0].freq = 0;
	ot[1].power = 5000;
	ot[1].freq = 0;

	gpc->opp_table = ot;
	gpc->num_opps = 2;
	gpc->power_limit = 5000;

	/* Init */
	KUNIT_EXPECT_EQ(test, gpc_stats_init(gpc), 0);
	KUNIT_ASSERT_NOT_NULL(test, gpc->stats);

	/* Verify Enabled/Disabled Stats */
	KUNIT_EXPECT_TRUE(test, gpc->stats->stats[GPC_STAT_POWER_LIMIT].enabled);
	/* Util and QoS should be disabled for virtual nodes */
	KUNIT_EXPECT_FALSE(test, gpc->stats->stats[GPC_STAT_QOS].enabled);
	KUNIT_EXPECT_FALSE(test, gpc->stats->stats[GPC_STAT_UTIL].enabled);

	/* Verify Power Limit Buckets Interpolation */
	KUNIT_EXPECT_EQ(test, gpc->stats->stats[GPC_STAT_POWER_LIMIT].num_buckets,
			GPC_STATS_MAX_BUCKETS);
	min_p = ot[0].power;
	max_p = ot[1].power;
	step = div_u64(max_p - min_p, GPC_STATS_MAX_BUCKETS - 1);

	for (i = 0; i < GPC_STATS_MAX_BUCKETS - 1; i++) {
		KUNIT_EXPECT_EQ(test,
			gpc->stats->stats[GPC_STAT_POWER_LIMIT].buckets[i].value,
			min_p + step * i);
	}
	KUNIT_EXPECT_EQ(test,
		gpc->stats->stats[GPC_STAT_POWER_LIMIT].buckets[GPC_STATS_MAX_BUCKETS - 1].value,
		max_p);

	/* Update OPP table (simulate power range change) */
	ot[0].power = 2000;
	ot[1].power = 10000;
	gpc->num_opps = 2; /* num_opps stays same */

	/* Re-init (should update buckets) */
	KUNIT_EXPECT_EQ(test, gpc_stats_init(gpc), 0);

	min_p = ot[0].power;
	max_p = ot[1].power;
	step = div_u64(max_p - min_p, GPC_STATS_MAX_BUCKETS - 1);

	for (i = 0; i < GPC_STATS_MAX_BUCKETS - 1; i++) {
		KUNIT_EXPECT_EQ(test,
			gpc->stats->stats[GPC_STAT_POWER_LIMIT].buckets[i].value,
			min_p + step * i);
	}
	KUNIT_EXPECT_EQ(test,
		gpc->stats->stats[GPC_STAT_POWER_LIMIT].buckets[GPC_STATS_MAX_BUCKETS - 1].value,
		max_p);

	/* Cleanup */
	gpc_stats_exit(gpc);
	kfree(ot);
	gpc->opp_table = NULL;
}

static void powercap_stats_update_show_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap *gpc = &data->gpc;

	/*
	 * We are testing the stats update function itself here, so we need
	 * to use the real implementation, not the mock.
	 */
	kunit_deactivate_static_stub(test, gpc_stats_update);
	struct cdev_opp_table *ot;
	char *buf;
	int ret, i;

	/* Clean start */
	gpc_stats_exit(gpc);

	/* Setup */
	ot = kcalloc(GPC_TEST_OPP_CT, sizeof(*ot), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ot);
	for (i = 0; i < GPC_TEST_OPP_CT; i++) {
		ot[i].power = (i + 1) * 1000;
		ot[i].freq = (i + 1) * 100000;
	}
	gpc->opp_table = ot;
	gpc->num_opps = GPC_TEST_OPP_CT;
	gpc->power_limit = 3000;

	KUNIT_EXPECT_EQ(test, gpc_stats_init(gpc), 0);

	buf = kunit_kzalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	/* Check avail show */
	ret = gpc_stats_avail_show(&gpc->zone.dev, NULL, buf);
	KUNIT_EXPECT_GT(test, ret, 0);
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "power_limit"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "utility"));
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "qos_request"));

	/* Update stats */
	gpc_stats_update(gpc, GPC_STAT_POWER_LIMIT, 1000);
	/* Wait for residency */
	mdelay(20);
	gpc_stats_update(gpc, GPC_STAT_POWER_LIMIT, 2000);
	mdelay(30);

	/* Show Power Limit stats */
	memset(buf, 0, PAGE_SIZE);
	/*
	 * gpc_stats_show cycles through enabled stats.
	 * Need to call it until we get power_limit.
	 * Or since we know the order, we can predict.
	 * But simplest is to check output.
	 */
	for (i = 0; i < GPC_STAT_MAX; i++) {
		ret = gpc_stats_show(&gpc->zone.dev, NULL, buf);
		if (strstr(buf, "power_limit:"))
			break;
		memset(buf, 0, PAGE_SIZE);
	}
	KUNIT_EXPECT_TRUE(test, strstr(buf, "power_limit:"));
	/*
	 * Bucket 0 (1000) should have ~20ms.
	 * Bucket 1 (2000) should have ~30ms (plus delta from show call).
	 * We check if values are present.
	 */
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "1000 "));
	KUNIT_EXPECT_NOT_NULL(test, strstr(buf, "2000 "));

	/* Verify residency resets after read */
	/*
	 * The stats show function resets time_ms to 0.
	 * So if we read again (after cycling), it should be small.
	 */

	gpc_stats_exit(gpc);
	kfree(ot);
	gpc->opp_table = NULL;
	kunit_kfree(test, buf);
	kunit_activate_static_stub(test, gpc_stats_update, mock_gpc_stats_update);
}

static void powercap_stats_no_opp_test(struct kunit *test)
{
	struct powercap_test_data *data = test->priv;
	struct gpowercap *gpc = &data->gpc;

	gpc_stats_exit(gpc);
	gpc->opp_table = NULL;
	gpc->num_opps = 0;

	/* Should succeed but enable no stats */
	KUNIT_EXPECT_EQ(test, gpc_stats_init(gpc), 0);
	KUNIT_ASSERT_NOT_NULL(test, gpc->stats);

	KUNIT_EXPECT_FALSE(test, gpc->stats->stats[GPC_STAT_POWER_LIMIT].enabled);
	KUNIT_EXPECT_FALSE(test, gpc->stats->stats[GPC_STAT_QOS].enabled);
	KUNIT_EXPECT_FALSE(test, gpc->stats->stats[GPC_STAT_UTIL].enabled);

	gpc_stats_exit(gpc);
}

static struct kunit_case powercap_helper_test[] = {
	KUNIT_CASE(powercap_stats_init_exit_test),
	KUNIT_CASE(powercap_stats_virtual_nodes_test),
	KUNIT_CASE(powercap_stats_update_show_test),
	KUNIT_CASE(powercap_stats_no_opp_test),
	KUNIT_CASE(powercap_init_test),
	KUNIT_CASE(powercap_destroy_hierarchy_test),
	KUNIT_CASE(powercap_gpc_register_and_ops_test),
	KUNIT_CASE(powercap_power_limit_bypass),
	KUNIT_CASE(powercap_devfreq_setup_test),
	KUNIT_CASE(powercap_devfreq_update_power_test),
	KUNIT_CASE(powercap_devfreq_get_power_test),
	KUNIT_CASE_SLOW(powercap_devfreq_set_power_test),
	KUNIT_CASE(powercap_cpu_setup_test),
	KUNIT_CASE(powercap_cpu_release_test),
	KUNIT_CASE(powercap_cpu_update_power_test),
	KUNIT_CASE(powercap_cpu_get_power_test),
	KUNIT_CASE(powercap_cpu_set_power_test),
	KUNIT_CASE(powercap_cpu_odpm_work_func_test),
	KUNIT_CASE(powercap_cpu_set_time_window_test),
	KUNIT_CASE(powercap_devfreq_release_test),
	KUNIT_CASE(powercap_devfreq_set_time_window_test),
	KUNIT_CASE(powercap_devfreq_odpm_work_func_test),
	KUNIT_CASE(powercap_dt_probe_test),
	KUNIT_CASE(powercap_dt_remove_test),
	KUNIT_CASE(powercap_volt_algo_setup_release_test),
	KUNIT_CASE(powercap_volt_algo_evaluate_test),
	KUNIT_CASE(powercap_volt_algo_set_get_power_test),
	KUNIT_CASE(powercap_volt_algo_update_power_test),
	KUNIT_CASE_SLOW(powercap_weights_algo_setup_release_test),
	KUNIT_CASE(powercap_weights_algo_evaluate_test),
	KUNIT_CASE(powercap_weights_algo_set_get_power_test),
	KUNIT_CASE(powercap_weights_algo_set_power_min_max_test),
	KUNIT_CASE(powercap_weights_algo_dist_weights_sysfs_test),
	KUNIT_CASE(powercap_weights_algo_redist_threshold_sysfs_test),
	KUNIT_CASE(powercap_weights_algo_rebalance_test),
	KUNIT_CASE(odpm_register_first_client_test),
	KUNIT_CASE(odpm_unregister_client_test),
	KUNIT_CASE(odpm_register_multiple_clients_same_interval_test),
	KUNIT_CASE(odpm_register_multiple_clients_different_intervals_test),
	KUNIT_CASE(odpm_polling_work_notification_test),
	KUNIT_CASE(odpm_register_invalid_interval_test),
	KUNIT_CASE(odpm_unregister_invalid_regulator_test),
	KUNIT_CASE(odpm_register_error_cases_test),
	{},
};

static struct kunit_suite powercap_helper_test_suite = {
	.name = "powercap_helper_tests",
	.test_cases = powercap_helper_test,
	.init = powercap_test_init,
	.exit = powercap_test_exit,
	.suite_init = powercap_test_suite_init,
	.suite_exit = powercap_test_suite_exit
};
kunit_test_suite(powercap_helper_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
