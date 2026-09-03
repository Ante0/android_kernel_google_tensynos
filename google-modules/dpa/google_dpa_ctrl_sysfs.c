// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 *
 * This sysfs module exposes file nodes that allow users and
 * applications to query the current state of the dpa control.
 */

#include <linux/device.h>
#include <linux/sysfs.h>

#include "google_dpa_ctrl_internal.h"
#include "google_dpa_ctrl_sysfs.h"
#include "google_dpa_internal.h"

static const char *const state_string[] = {
	[NOA_STATE_UNAVAILABLE] = "unavailable",
	[NOA_STATE_READY] = "ready",
	[NOA_STATE_CRASH] = "crash",
};

static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int state;
	const char *ans;

	state = google_dpa_ctrl_get_state();

	if (state >= NOA_STATE_COUNT)
		ans = "invalid";
	else
		ans = state_string[state];

	return sysfs_emit(buf, "%s\n", ans);
}

static struct device_attribute dev_attr_state = __ATTR(state, 0444, state_show, NULL);

static const char *const data_path_string[] = {
	[NOA_DATA_PATH_DIRECT] = "direct",
	[NOA_DATA_PATH_OFFLOAD] = "offload",
};

static ssize_t data_path_store(struct device *dev, struct device_attribute *attr, const char *buf,
			       size_t count)
{
	int ret = -EINVAL;

	for (int i = NOA_DATA_PATH_DIRECT; i < NOA_DATA_PATH_COUNT; i++) {
		if (sysfs_streq(buf, data_path_string[i])) {
			ret = google_dpa_ctrl_set_data_path(i);
			break;
		}
	}
	if (ret)
		dev_err(dev, "Cannot switch to %s (err=%d)", buf, ret);

	return ret ? ret : count;
}

static ssize_t data_path_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int data_path;
	const char *ans;

	data_path = google_dpa_ctrl_get_data_path();

	if (data_path >= NOA_DATA_PATH_COUNT)
		ans = "invalid";
	else
		ans = data_path_string[data_path];

	return sysfs_emit(buf, "%s\n", ans);
}

static struct device_attribute dev_attr_data_path =
	__ATTR(data_path, 0644, data_path_show, data_path_store);

static const char *const pcie_ownership_string[] = {
	[NOA_PCIE_OWNERSHIP_APC] = "apc",
	[NOA_PCIE_OWNERSHIP_DPA] = "dpa",
};

static ssize_t pcie_ownership_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret = -EINVAL;

	for (int i = NOA_PCIE_OWNERSHIP_APC; i < NOA_PCIE_OWNERSHIP_COUNT; i++) {
		if (sysfs_streq(buf, pcie_ownership_string[i])) {
			ret = google_dpa_ctrl_set_pcie_ownership(i);
			break;
		}
	}
	if (ret)
		dev_err(dev, "Cannot switch to %s (err=%d)", buf, ret);

	return ret ? ret : count;
}

static ssize_t pcie_ownership_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned int pcie_ownership;
	const char *ans;

	pcie_ownership = google_dpa_ctrl_get_pcie_ownership();

	if (pcie_ownership >= NOA_PCIE_OWNERSHIP_COUNT)
		ans = "invalid";
	else
		ans = pcie_ownership_string[pcie_ownership];

	return sysfs_emit(buf, "%s\n", ans);
}

static struct device_attribute dev_attr_pcie_ownership =
	__ATTR(pcie_ownership, 0644, pcie_ownership_show, pcie_ownership_store);

static struct attribute *google_dpa_ctrl_root_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_data_path.attr,
	&dev_attr_pcie_ownership.attr,
	NULL,
};

static struct attribute_group google_dpa_ctrl_root_attr_group = {
	.attrs = google_dpa_ctrl_root_attrs,
	.name = "ctrl",
};

int google_dpa_ctrl_sysfs_init(struct google_dpa *dpa)
{
	return devm_device_add_group(dpa->dev, &google_dpa_ctrl_root_attr_group);
}
