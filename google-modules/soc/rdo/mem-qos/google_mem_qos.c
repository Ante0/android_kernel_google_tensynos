// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kernel module to host all mem QoS platform device drivers
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/types.h>

#include <mem-qos/google_mem_qos_scenario.h>
#include "google_mem_qos.h"
#include "google_mem_qos_debugfs.h"
#include "google_qos_box.h"

static struct google_mem_qos_desc mem_qos_desc;

static inline bool is_qos_box_dev_in_list(struct qos_box_dev *qos_box_dev)
{
	struct qos_box_dev *qbox_dev;

	list_for_each_entry(qbox_dev, &mem_qos_desc.qos_box_list, node) {
		if (qbox_dev == qos_box_dev)
			return true;
	}

	return false;
}

int google_qos_box_dev_register(struct qos_box_dev *qos_box_dev)
{
	int ret = 0;

	if (!qos_box_dev) {
		ret = -EINVAL;
		goto out;
	}

	/* add qos_box_dev to the list */
	mutex_lock(&mem_qos_desc.mutex);

	if (is_qos_box_dev_in_list(qos_box_dev)) {
		dev_err(qos_box_dev->dev, "%s: already register to mem_qos\n", __func__);
		ret = -EINVAL;
		goto out_unlock;
	}

	list_add_tail(&qos_box_dev->node, &mem_qos_desc.qos_box_list);

out_unlock:
	mutex_unlock(&mem_qos_desc.mutex);

out:
	return ret;
}

int google_qos_box_dev_unregister(struct qos_box_dev *qos_box_dev)
{
	int ret = 0;

	if (!qos_box_dev) {
		ret = -EINVAL;
		goto out;
	}

	/* remove qos_box_dev from the list */
	mutex_lock(&mem_qos_desc.mutex);

	if (!is_qos_box_dev_in_list(qos_box_dev)) {
		dev_err(qos_box_dev->dev, "%s: not register to mem_qos\n", __func__);
		ret = -EINVAL;
		goto out_unlock;
	}

	list_del(&qos_box_dev->node);

out_unlock:
	mutex_unlock(&mem_qos_desc.mutex);

out:
	return ret;
}

static int __update_active_scenario(u32 scenario)
{
	struct qos_box_dev *qos_box_dev;
	int ret = 0;

	list_for_each_entry(qos_box_dev, &mem_qos_desc.qos_box_list, node) {
		ret = qos_box_dev->ops->select_config(qos_box_dev, scenario);
		if (ret) {
			pr_err("qos_box %s: select_config(%u) failed: %d\n",
			       qos_box_dev->name, scenario, ret);
			ret = -EFAULT;
			break;
		}
	}

	return ret;
}

int google_mem_qos_scenario_vote(u32 scenario)
{
	int ret = 0;

	if (scenario >= NUM_MEM_QOS_SCENARIO || scenario == MEM_QOS_SCENARIO_DEFAULT) {
		pr_err("%s(%u): invalid scenario\n", __func__, scenario);
		return -EINVAL;
	}

	mutex_lock(&mem_qos_desc.mutex);

	mem_qos_desc.scen_usage_cnt[scenario]++;

	if (scenario > mem_qos_desc.active_scenario)
		mem_qos_desc.active_scenario = scenario;

	ret = __update_active_scenario(mem_qos_desc.active_scenario);

	mutex_unlock(&mem_qos_desc.mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(google_mem_qos_scenario_vote);

int google_mem_qos_scenario_unvote(u32 scenario)
{
	u32 i;
	int ret = 0;

	if (scenario >= NUM_MEM_QOS_SCENARIO || scenario == MEM_QOS_SCENARIO_DEFAULT) {
		pr_err("%s(%u): invalid scenario\n", __func__, scenario);
		return -EINVAL;
	}

	mutex_lock(&mem_qos_desc.mutex);

	if (mem_qos_desc.scen_usage_cnt[scenario] == 0) {
		pr_err("%s(%u): scenario usage_cnt already 0\n", __func__, scenario);
		ret = -EINVAL;
		goto out_unlock;
	}

	mem_qos_desc.scen_usage_cnt[scenario]--;

	/* Find new active scenario when ref count of current active scenario becomes 0 */
	if (mem_qos_desc.active_scenario == scenario &&
	    mem_qos_desc.scen_usage_cnt[scenario] == 0) {
		for (i = scenario - 1; i >= 0; i--) {
			if (mem_qos_desc.scen_usage_cnt[i] > 0) {
				mem_qos_desc.active_scenario = (u32)i;
				break;
			}
		}
	}

	ret = __update_active_scenario(mem_qos_desc.active_scenario);

out_unlock:
	mutex_unlock(&mem_qos_desc.mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(google_mem_qos_scenario_unvote);

static struct platform_driver * const google_qos_drivers[] = {
	&google_qos_box_platform_driver,
};

static int __init google_mem_qos_init(void)
{
	int ret = 0;

	mutex_init(&mem_qos_desc.mutex);
	INIT_LIST_HEAD(&mem_qos_desc.qos_box_list);

	/* default scenario ref cnt = 1 */
	BUILD_BUG_ON(MEM_QOS_SCENARIO_DEFAULT >= NUM_MEM_QOS_SCENARIO);
	mem_qos_desc.scen_usage_cnt[MEM_QOS_SCENARIO_DEFAULT] = 1;

	mem_qos_desc.active_scenario = MEM_QOS_SCENARIO_DEFAULT;

	ret = google_mem_qos_init_debugfs(&mem_qos_desc);
	if (ret)
		goto out;

	ret = platform_register_drivers(google_qos_drivers, ARRAY_SIZE(google_qos_drivers));

out:
	return ret;
}
module_init(google_mem_qos_init);

static void __exit google_mem_qos_exit(void)
{
	platform_unregister_drivers(google_qos_drivers, ARRAY_SIZE(google_qos_drivers));
	google_mem_qos_remove_debugfs(&mem_qos_desc);
}
module_exit(google_mem_qos_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google Memory QoS driver");
MODULE_LICENSE("GPL");
