// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */
#include <linux/debugfs.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/string.h>
#include "radio-utils.h"
#include "dpmaif-google.h"
#include "feature-control.h"

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_AFFINITY)
/* Mirror from dpmaif.c */
#define MTK_DATA_NAPI_NR_MAX 3
#define DPMAIF_BAT_NUM_MAX 2
#define DPMAIF_CPU_LOADING_MODE 2
struct mtk_data_cpu_affinity_cfg_mirror {
	u64 speed;
	u8 napi_thrd_aff[MTK_DATA_NAPI_NR_MAX];
	u8 steer_wq_aff[MTK_DATA_NAPI_NR_MAX];
	u8 reload_thrd_aff[DPMAIF_BAT_NUM_MAX];
	u8 doorbell_thrd_aff;
};

#define DPMAIF_DBG_NAME_MAX 32

static struct mtk_data_cpu_affinity_cfg_mirror affinity_config[DPMAIF_CPU_LOADING_MODE];
static u32 target_cpu_num;
static bool affinity_parsed;
static struct dentry *dpmaif_dbg_root;

static void dpmaif_google_show_affinity_config(int i)
{
	LOG_INFO("Config[%d] speed=%llu\n", i, affinity_config[i].speed);
	LOG_INFO("  napi_aff  = {%u, %u, %u}\n", affinity_config[i].napi_thrd_aff[0],
		 affinity_config[i].napi_thrd_aff[1], affinity_config[i].napi_thrd_aff[2]);
	LOG_INFO("  steer_aff = {%u, %u, %u}\n", affinity_config[i].steer_wq_aff[0],
		 affinity_config[i].steer_wq_aff[1], affinity_config[i].steer_wq_aff[2]);
	LOG_INFO("  reload_aff= {0x%x, 0x%x}, doorbell=0x%x\n",
		 affinity_config[i].reload_thrd_aff[0],
		 affinity_config[i].reload_thrd_aff[1],
		 affinity_config[i].doorbell_thrd_aff);
}

static int dpmaif_google_init_from_dt(void)
{
	struct device_node *root, *data_np, *aff_np, *child;
	int i = 0;
	int ret = 0;

	root = of_find_node_by_path("/");
	if (!root) {
		LOG_ERR("root node not found!\n");
		return -ENODEV;
	}

	data_np = of_get_child_by_name(root, "radio-google-data");
	if (!data_np) {
		ret = -ENODEV;
		LOG_ERR("radio-google-data node not found!\n");
		goto put_root;
	}

	aff_np = of_get_child_by_name(data_np, "dpmaif-affinity");
	if (!aff_np) {
		ret = -ENODEV;
		LOG_ERR("dpmaif-affinity node not found!\n");
		goto put_data;
	}

	ret = of_property_read_u32(aff_np, "google,target-cpu-num", &target_cpu_num);
	if (ret) {
		LOG_ERR("google,target-cpu-num missing in DTS!\n");
		goto put_aff;
	}

	LOG_INFO("Target CPU num: %u\n", target_cpu_num);

	for_each_child_of_node(aff_np, child) {
		if (i >= DPMAIF_CPU_LOADING_MODE) {
			of_node_put(child);
			break;
		}

		ret |= of_property_read_u64(child, "speed", &affinity_config[i].speed);
		ret |= of_property_read_u8_array(child, "napi-aff",
						 affinity_config[i].napi_thrd_aff,
						 MTK_DATA_NAPI_NR_MAX);
		ret |= of_property_read_u8_array(child, "steer-aff",
						 affinity_config[i].steer_wq_aff,
						 MTK_DATA_NAPI_NR_MAX);
		ret |= of_property_read_u8_array(child, "reload-aff",
						 affinity_config[i].reload_thrd_aff,
						 DPMAIF_BAT_NUM_MAX);
		ret |= of_property_read_u8(child, "doorbell-aff",
					   &affinity_config[i].doorbell_thrd_aff);

		if (ret) {
			of_node_put(child);
			break;
		}

		dpmaif_google_show_affinity_config(i);

		i++;
	}

	if (i == DPMAIF_CPU_LOADING_MODE)
		affinity_parsed = true;

put_aff:
	of_node_put(aff_np);
put_data:
	of_node_put(data_np);
put_root:
	of_node_put(root);
	return affinity_parsed ? 0 : -EINVAL;
}

static void dpmaif_google_debugfs_init(void)
{
	struct dentry *parent_dir = get_radio_debugfs_root();
	char dbg_name[DPMAIF_DBG_NAME_MAX];
	unsigned int i, j;

	if (IS_ERR_OR_NULL(parent_dir)) {
		LOG_ERR("Radio debugfs root not available\n");
		return;
	}

	dpmaif_dbg_root = debugfs_create_dir("dpmaif_google_affinity", parent_dir);
	if (IS_ERR_OR_NULL(dpmaif_dbg_root)) {
		LOG_ERR("Failed to create dpmaif_google debugfs dir!\n");
		dpmaif_dbg_root = NULL;
		return;
	}

	for (i = 0; i < DPMAIF_CPU_LOADING_MODE; i++) {
		/* Speed (u64) */
		scnprintf(dbg_name, sizeof(dbg_name), "conf%u_speed", i);
		debugfs_create_u64(dbg_name, 0644, dpmaif_dbg_root, &affinity_config[i].speed);

		/* Doorbell (u8) */
		scnprintf(dbg_name, sizeof(dbg_name), "conf%u_doorbell", i);
		debugfs_create_u8(dbg_name, 0644, dpmaif_dbg_root,
				  &affinity_config[i].doorbell_thrd_aff);

		/* NAPI Array (u8) */
		for (j = 0; j < MTK_DATA_NAPI_NR_MAX; j++) {
			scnprintf(dbg_name, sizeof(dbg_name), "conf%u_napi_%u", i, j);
			debugfs_create_u8(dbg_name, 0644, dpmaif_dbg_root,
					  &affinity_config[i].napi_thrd_aff[j]);
		}

		/* Steer Array (u8) */
		for (j = 0; j < MTK_DATA_NAPI_NR_MAX; j++) {
			scnprintf(dbg_name, sizeof(dbg_name), "conf%u_steer_%u", i, j);
			debugfs_create_u8(dbg_name, 0644, dpmaif_dbg_root,
					  &affinity_config[i].steer_wq_aff[j]);
		}

		/* Reload Array (u8) */
		for (j = 0; j < DPMAIF_BAT_NUM_MAX; j++) {
			scnprintf(dbg_name, sizeof(dbg_name), "conf%u_reload_%u", i, j);
			debugfs_create_u8(dbg_name, 0644, dpmaif_dbg_root,
					  &affinity_config[i].reload_thrd_aff[j]);
		}
	}
	LOG_INFO("Created all debugfs nodes under radio root\n");
}

int dpmaif_google_fill_affinity(unsigned int index, u32 cpu_count, u64 *speed, u8 *napi,
				size_t napi_len, u8 *steer, size_t steer_len, u8 *reload,
				size_t reload_len, u8 *doorbell)
{
	if (index >= DPMAIF_CPU_LOADING_MODE || cpu_count != target_cpu_num) {
		LOG_ERR("CPU count mismatch (%d vs DTS:%u), ignoring Google Affinity\n", cpu_count,
			target_cpu_num);
		return -EINVAL;
	}

	if (napi_len != MTK_DATA_NAPI_NR_MAX || steer_len != MTK_DATA_NAPI_NR_MAX ||
	    reload_len != DPMAIF_BAT_NUM_MAX) {
		LOG_ERR("Affinity buffer length mismatch! (NAPI:%zu, Steer:%zu, Reload:%zu)\n",
			napi_len, steer_len, reload_len);
		return -E2BIG;
	}

	*speed = affinity_config[index].speed;
	memcpy(napi, affinity_config[index].napi_thrd_aff, MTK_DATA_NAPI_NR_MAX);
	memcpy(steer, affinity_config[index].steer_wq_aff, MTK_DATA_NAPI_NR_MAX);
	memcpy(reload, affinity_config[index].reload_thrd_aff, DPMAIF_BAT_NUM_MAX);
	*doorbell = affinity_config[index].doorbell_thrd_aff;

	dpmaif_google_show_affinity_config(index);

	return 0;
}
EXPORT_SYMBOL_GPL(dpmaif_google_fill_affinity);

int dpmaif_google_affinity_init(void)
{
	int ret;

	ret = dpmaif_google_init_from_dt();
	if (ret) {
		LOG_ERR("Parse DTS failed!, ret=%d!\n", ret);
		return -ENODEV;
	}

	dpmaif_google_debugfs_init();

	return 0;
}

void dpmaif_google_affinity_exit(void)
{
	debugfs_remove_recursive(dpmaif_dbg_root);
	dpmaif_dbg_root = NULL;
}
#endif /* CONFIG_GOOGLE_MODEM_DATA_AFFINITY */

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
#define NCM_IF_PREFIX "ncm"
static atomic_t lro_limit = ATOMIC_INIT(DEFAULT_LRO_SIZE);
static atomic_t ncm_if_count = ATOMIC_INIT(0);

unsigned int dpmaif_google_get_lro_limit(void)
{
	return atomic_read(&lro_limit);
}
EXPORT_SYMBOL_GPL(dpmaif_google_get_lro_limit);

static int dpmaif_google_notifier_netdev_events(struct notifier_block *nb, unsigned long event,
						void *ptr)
{
	struct net_device *net_dev = netdev_notifier_info_to_dev(ptr);

	if (!str_has_prefix(net_dev->name, NCM_IF_PREFIX))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		/* Set the limit when the first NCM interface comes up. */
		if (atomic_inc_return(&ncm_if_count) == 1) {
			atomic_set(&lro_limit, NCM_LRO_SIZE_LIMIT);
			LOG_INFO("Update LRO limit for NCM driver: %u\n", NCM_LRO_SIZE_LIMIT);
		}
		break;
	case NETDEV_DOWN:
		/* Restore the default when the last NCM interface goes down. */
		if (atomic_dec_and_test(&ncm_if_count)) {
			atomic_set(&lro_limit, DEFAULT_LRO_SIZE);
			LOG_INFO("Set LRO limit back to default: %u\n", DEFAULT_LRO_SIZE);
		}
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block dpmaif_google_notifier_netdev_nb = {
	.notifier_call = dpmaif_google_notifier_netdev_events,
};

int dpmaif_google_lro_size_limit_init(void)
{
	int ret;

	if (!get_lro_size_limit_enable_status())
		return 0;

	ret = register_netdevice_notifier(&dpmaif_google_notifier_netdev_nb);

	if (ret)
		LOG_ERR("Failed to register netdevice notifier: %d\n", ret);
	else
		LOG_INFO("netdevice notifier registered successfully\n");

	return ret;
}

void dpmaif_google_lro_size_limit_exit(void)
{
	if (!get_lro_size_limit_enable_status())
		return;

	unregister_netdevice_notifier(&dpmaif_google_notifier_netdev_nb);
	LOG_INFO("netdevice notifier unregistered\n");
}

#endif
