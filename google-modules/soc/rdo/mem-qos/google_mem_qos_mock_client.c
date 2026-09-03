// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver to provide debugfs knobs to enable calling Mem QoS APIs by writing
 * the debugfs knobs
 *
 * To call QoS scenario API:
 *   # cd /sys/kernel/debug/google_mem_qos_mock_client/scenario_api
 *   # echo [scenario] > vote
 *
 *   The mock client driver calls google_mem_qos_scenario_vote([scenario]), [scenario] is a
 *   scenario index
 *
 *   # cd /sys/kernel/debug/google_mem_qos_mock_client/scenario_api
 *   # echo [scenario] > unvote
 *
 *   The mock client driver calls google_mem_qos_scenario_unvote([scenario]), [scenario] is a
 *   scenario index
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/debugfs.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <mem-qos/google_qos_box_reg_api.h>
#include <mem-qos/google_mem_qos_scenario.h>

struct dentry *base_dir;


static int scenario_vote_api_set(void *data, u64 val)
{
	u32 idx = (u32)val;
	int ret = 0;

	ret = google_mem_qos_scenario_vote(idx);
	if (ret)
		pr_err("google_mem_qos_scenario_vote(%u) error %d\n", idx, ret);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(scenario_vote_api, NULL, scenario_vote_api_set, "%llx\n");

static int scenario_unvote_api_set(void *data, u64 val)
{
	u32 idx = (u32)val;
	int ret = 0;

	ret = google_mem_qos_scenario_unvote(idx);
	if (ret)
		pr_err("google_mem_qos_scenario_unvote(%u) error %d\n", idx, ret);

	return ret;
}
DEFINE_DEBUGFS_ATTRIBUTE(scenario_unvote_api, NULL, scenario_unvote_api_set, "%llx\n");

static int __create_qos_scenario_switch_api_debugfs_path(struct device *dev)
{
	struct dentry *sub_dir;

	sub_dir = debugfs_create_dir("scenario_api", base_dir);

	debugfs_create_file("vote", 0200, sub_dir, NULL, &scenario_vote_api);
	debugfs_create_file("unvote", 0200, sub_dir, NULL, &scenario_unvote_api);

	return 0;
}

static int __create_debugfs(struct device *dev)
{
	int ret = 0;

	base_dir = debugfs_create_dir("google_mem_qos_mock_client", NULL);

	ret = __create_qos_scenario_switch_api_debugfs_path(dev);
	if (ret)
		goto out;

	return 0;

out:
	debugfs_remove(base_dir);
	return ret;
}

static int google_mem_qos_mock_client_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	ret = __create_debugfs(dev);

	return ret;
}

static void
google_mem_qos_mock_client_platform_remove(struct platform_device *pdev)
{
	debugfs_remove(base_dir);
}

static const struct of_device_id google_mem_qos_mock_client_of_match_table[] = {
	{ .compatible = "google,mem-qos-mock-client" },
	{}
};
MODULE_DEVICE_TABLE(of, google_mem_qos_mock_client_of_match_table);

static struct platform_driver google_mem_qos_mock_client_platform_driver = {
	.probe = google_mem_qos_mock_client_platform_probe,
	.remove = google_mem_qos_mock_client_platform_remove,
	.driver = {
		.name = "google-mem-qos-mock-client",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_mem_qos_mock_client_of_match_table),
	},
};

module_platform_driver(google_mem_qos_mock_client_platform_driver);
MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google Mem QoS mock client driver");
MODULE_LICENSE("GPL");
