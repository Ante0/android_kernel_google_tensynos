// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include "feature-control.h"
#include "radio-utils.h"

#define RADIO_CLASS_NAME "radio"
#define RADIO_DEBUGFS_NAME "radio"

static struct class *radio_class;
static struct dentry *radio_debugfs_root;

/* All feature enable flags defined here*/
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_CDD)
static bool cdd_enable_status;
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
static bool wakemon_enable_status;
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
static bool soc_qos_enable_status;
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
static bool lro_size_limit_enable_status;
#endif

/* All feature enable status show functions */

/* All feature enable status store functions */

/* All feature enable sysfs attributes */

/* All exported functions used by the rest of the sources */
#if IS_ENABLED(CONFIG_GOOGLE_MODEM_CDD)
bool get_cdd_enable_status(void)
{
	return cdd_enable_status;
}
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
bool get_wakemon_enable_status(void)
{
	return wakemon_enable_status;
}
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
bool get_soc_qos_enable_status(void)
{
	return soc_qos_enable_status;
}
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
bool get_lro_size_limit_enable_status(void)
{
	return lro_size_limit_enable_status;
}
#endif

/* Function definitions for feature control */
static void get_feature_status_from_dts(void)
{
	struct device_node *np, *child_np;

	np = of_find_node_by_path("/");
	if (!np) {
		LOG_ERR("root dts node not found!\n");
		return;
	}

	child_np = of_get_child_by_name(np, "radio-google-data");
	if (!child_np) {
		LOG_ERR("radio-google-data not found in dts!\n");
		of_node_put(np);
		return;
	}

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_CDD)
	cdd_enable_status = of_property_read_bool(child_np, "cdd_enable");
	LOG_INFO("cdd_enable = %d\n", cdd_enable_status);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
	wakemon_enable_status = of_property_read_bool(child_np, "wakemon_enable");
	LOG_INFO("wakemon_enable = %d\n", wakemon_enable_status);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
	soc_qos_enable_status = of_property_read_bool(child_np, "soc_qos_enable");
	LOG_INFO("soc_qos_enable = %d\n", soc_qos_enable_status);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
	lro_size_limit_enable_status = of_property_read_bool(child_np, "lro_size_limit_enable");
	LOG_INFO("lro_size_limit_enable = %d\n", lro_size_limit_enable_status);
#endif

	of_node_put(child_np);
	of_node_put(np);
}

void google_feature_control_init(void)
{
	radio_class = class_create(RADIO_CLASS_NAME);
	if (IS_ERR(radio_class)) {
		LOG_ERR("Failed to create sysfs class (rc: %ld)!\n", PTR_ERR(radio_class));
		return;
	}

	radio_debugfs_root = debugfs_create_dir(RADIO_DEBUGFS_NAME, NULL);
	if (IS_ERR(radio_debugfs_root)) {
		LOG_ERR("Failed to create debugfs (rc: %ld)!\n", PTR_ERR(radio_debugfs_root));
	}

	get_feature_status_from_dts();

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_CDD)
	debugfs_create_bool("cdd_enable", 0644, radio_debugfs_root, &cdd_enable_status);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
	debugfs_create_bool("wakemon_enable", 0644, radio_debugfs_root, &wakemon_enable_status);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_SOC_BW_QOS)
	debugfs_create_bool("soc_qos_enable", 0644, radio_debugfs_root, &soc_qos_enable_status);
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MODEM_DATA_LRO_SIZE_LIMIT)
	debugfs_create_bool("lro_size_limit_enable", 0444, radio_debugfs_root,
			    &lro_size_limit_enable_status);
#endif

	LOG_INFO("success!\n");
}

void google_feature_control_exit(void)
{
	debugfs_remove_recursive(radio_debugfs_root);

	class_destroy(radio_class);
}

struct dentry *get_radio_debugfs_root(void)
{
	return radio_debugfs_root;
}
