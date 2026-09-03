// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC.
 *
 */

#include <linux/of.h>
#include <linux/device.h>

#include "google-cdd-local.h"

static const char *const dpm_policy[] = {
	[GO_DEFAULT_ID] = GO_DEFAULT,	  [GO_PANIC_ID] = GO_PANIC,
	[GO_WATCHDOG_ID] = GO_WATCHDOG,	  [GO_S2M_ID] = GO_S2M,
	[GO_CACHEDUMP_ID] = GO_CACHEDUMP, [GO_SCANDUMP_ID] = GO_SCANDUMP,
};

struct google_cdd_dpm cdd_dpm;

static void google_cdd_dpm_scan_dt_feature(struct device *dev,
					   struct device_node *feature_node)
{
	struct device_node *item;
	unsigned int buffer;

	item = of_find_node_by_name(feature_node, DPM_DUMP_MODE);
	if (item == NULL) {
		dev_dbg(dev,
			"dpm: No ramdump node detected, dump-mode disabled\n");
	} else {
		if (of_property_read_u32(item, DPM_ENABLED, &buffer) != 0) {
			dev_info(dev, "dpm: dump-mode disabled\n");
		} else {
			if (buffer == FULL_DUMP || buffer == QUICK_DUMP) {
				dev_info(dev, "dpm: dump-mode is %s Dump\n",
					 buffer == FULL_DUMP ? "Full" :
							       "Quick");
			} else {
				dev_info(dev, "dpm: dump-mode disabled\n");
			}

			cdd_dpm.feature.dump_mode_enabled = buffer;
		}
	}

	buffer = 0;
	of_property_read_u32(item, DPM_FILE_SUPPORT, &buffer);
	cdd_dpm.feature.dump_mode_file_support = buffer;
	dev_info(dev, "dpm: file-support is %s\n",
		 buffer ? "enabled" : "disabled");

	item = of_find_node_by_name(feature_node, DPM_EVENT);
	if (item == NULL)
		dev_warn(dev, "dpm: No method found\n");
}

static void google_cdd_dpm_read_debug_policy(struct device *dev,
					     struct device_node *property,
					     const char *prop_name,
					     unsigned int *out_value)
{
	if (of_property_read_u32(property, prop_name, out_value) != 0) {
		dev_dbg(dev, "dpm: No %s found\n", prop_name);
	} else {
		dev_info(dev, "dpm: run %s at %s\n", dpm_policy[*out_value],
			 prop_name);
	}
}

static void google_cdd_dpm_scan_dt_policy(struct device *dev,
					  struct device_node *policy_node)
{
	struct device_node *item;
	unsigned int buffer = 0;

	item = of_find_node_by_name(policy_node, DPM_EXCEPTION);
	if (item == NULL) {
		dev_dbg(dev, "dpm: No exception found\n");
		return;
	}

	of_property_read_u32(item, DPM_PRE_LOG, &buffer);
	dev_info(dev, "dpm: Pre log is %s\n", buffer ? "enabled" : "disabled");
	cdd_dpm.policy.pre_log = buffer;

	google_cdd_dpm_read_debug_policy(dev, item, DPM_EL1_DA,
					 &cdd_dpm.policy.el1_da);
	google_cdd_dpm_read_debug_policy(dev, item, DPM_EL1_IA,
					 &cdd_dpm.policy.el1_ia);
	google_cdd_dpm_read_debug_policy(dev, item, DPM_EL1_UNDEF,
					 &cdd_dpm.policy.el1_undef);
	google_cdd_dpm_read_debug_policy(dev, item, DPM_EL1_SP_PC,
					 &cdd_dpm.policy.el1_sp_pc);
	google_cdd_dpm_read_debug_policy(dev, item, DPM_EL1_INV,
					 &cdd_dpm.policy.el1_inv);
	google_cdd_dpm_read_debug_policy(dev, item, DPM_EL1_SERROR,
					 &cdd_dpm.policy.el1_serror);
}

int google_cdd_dpm_scand_dt(struct device *dev)
{
	struct device_node *root;
	struct device_node *next;
	unsigned int buffer;

	memset(&cdd_dpm, 0, sizeof(struct google_cdd_dpm));

	root = of_find_node_by_name(NULL, DPM_ROOT);
	if (root == NULL)
		return -ENODEV;

	cdd_dpm.enabled = true;

	/* version */
	if (of_property_read_u32(root, DPM_VERSION, &buffer) == 0) {
		cdd_dpm.version = buffer;
		dev_info(dev, "dpm: version %01d.%02d\n", buffer / 100,
			 buffer % 100);
	} else {
		dev_info(dev, "dpm: no version found\n");
	}

	/* feature setting */
	next = of_find_node_by_name(root, DPM_FEATURE);
	if (next == NULL)
		dev_warn(dev, "dpm: No features found\n");
	else
		google_cdd_dpm_scan_dt_feature(dev, next);

	/* policy setting */
	next = of_find_node_by_name(root, DPM_POLICY);
	if (next == NULL)
		dev_warn(dev, "dpm: No policy found\n");
	else
		google_cdd_dpm_scan_dt_policy(dev, next);

	return 0;
}
