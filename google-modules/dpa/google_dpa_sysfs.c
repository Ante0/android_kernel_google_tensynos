// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2024 Google LLC
 */

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

#include "google_dpa_boot.h"
#include "google_dpa_ctrl_sysfs.h"
#include "google_dpa_internal.h"
#include "google_dpa_rpc_sysfs.h"
#include "google_dpa_sysfs.h"

static inline struct google_dpa *get_google_dpa(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	return platform_get_drvdata(pdev);
}

static ssize_t google_dpa_firmware_show(struct google_dpa *dpa, const char *firmware_name,
					char *buf)
{
	int ret;

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret)
		return ret;
	ret = sysfs_emit(buf, "%s", firmware_name);
	mutex_unlock(&dpa->mutex);

	return ret;
}

static ssize_t google_dpa_firmware_store(struct google_dpa *dpa, struct google_dpa_mcu *mcu,
					 const char *buf, size_t count)
{
	struct device *dev = dpa->dev;
	char *p;
	int len, ret;

	len = strcspn(buf, "\n");
	if (!len) {
		dev_err(dpa->dev, "Firmware name cannot be empty");
		return -EINVAL;
	}

	p = kstrndup(buf, len, GFP_KERNEL);
	if (!p) {
		dev_err(dpa->dev, "Failed to allocate memory to store the firmware name");
		return -ENOMEM;
	}

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret) {
		kfree(p);
		return ret;
	}

	if (dpa->fw_state != GOOGLE_DPA_FW_UNLOADED) {
		dev_err(dev, "firmware name cannot be updated when DPA is running.");
		ret = -EINVAL;
		kfree(p);
	} else {
		kfree_const(mcu->firmware_name);
		mcu->firmware_name = p;
		ret = count;
	}
	mutex_unlock(&dpa->mutex);
	return ret;
}

static ssize_t google_dpa_ncp_firmware_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct google_dpa *dpa = get_google_dpa(dev);

	return google_dpa_firmware_show(dpa, dpa->ncp.firmware_name, buf);
}

static ssize_t google_dpa_ncp_firmware_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct google_dpa *dpa = get_google_dpa(dev);

	return google_dpa_firmware_store(dpa, &dpa->ncp, buf, count);
}

/* TODO(b/426465018): Remove this attribute after the image format migration */
static struct device_attribute dev_attr_ncp_firmware =
	__ATTR(firmware, 0644, google_dpa_ncp_firmware_show, google_dpa_ncp_firmware_store);

static ssize_t google_dpa_nep_firmware_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct google_dpa *dpa = get_google_dpa(dev);

	return google_dpa_firmware_show(dpa, dpa->nep.firmware_name, buf);
}

static ssize_t google_dpa_nep_firmware_store(struct device *dev, struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct google_dpa *dpa = get_google_dpa(dev);

	return google_dpa_firmware_store(dpa, &dpa->nep, buf, count);
}

/* TODO(b/426465018): Remove this attribute after the image format migration */
static struct device_attribute dev_attr_nep_firmware =
	__ATTR(firmware, 0644, google_dpa_nep_firmware_show, google_dpa_nep_firmware_store);

static const char *const google_dpa_state_string[] = {
	[GOOGLE_DPA_FW_UNLOADED] = "unloaded",
	[GOOGLE_DPA_FW_AUTHENTICATED] = "authenticated",
	[GOOGLE_DPA_FW_NCP_RELEASED] = "ncp released",
	[GOOGLE_DPA_FW_RUNNING] = "running",
	[GOOGLE_DPA_FW_CRASHED] = "crashed",
	[GOOGLE_DPA_FW_POWER_OFF_FAILED] = "power off failed",
};

static ssize_t firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct google_dpa *dpa = get_google_dpa(dev);
	int ret;

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret)
		return ret;
	ret = sysfs_emit(buf, "%s", dpa->firmware_name);
	mutex_unlock(&dpa->mutex);

	return ret;
}

static ssize_t firmware_store(struct device *dev, struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct google_dpa *dpa = get_google_dpa(dev);
	char *p;
	int len, ret;

	len = strcspn(buf, "\n");
	if (!len) {
		dev_err(dpa->dev, "Firmware name cannot be empty");
		return -EINVAL;
	}

	p = kstrndup(buf, len, GFP_KERNEL);
	if (!p) {
		dev_err(dpa->dev, "Failed to allocate memory to store the firmware name");
		return -ENOMEM;
	}

	ret = mutex_lock_interruptible(&dpa->mutex);
	if (ret) {
		kfree(p);
		return ret;
	}

	if (dpa->fw_state != GOOGLE_DPA_FW_UNLOADED) {
		dev_err(dev, "firmware name cannot be updated when DPA is running.");
		ret = -EINVAL;
		kfree(p);
	} else {
		kfree_const(dpa->firmware_name);
		dpa->firmware_name = p;
		ret = count;
	}
	mutex_unlock(&dpa->mutex);
	return ret;
}

static DEVICE_ATTR_RW(firmware);

static ssize_t state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct google_dpa *dpa = get_google_dpa(dev);
	unsigned int state;
	const char *ans;

	state = dpa->fw_state;

	if (state > GOOGLE_DPA_FW_STATE_LAST)
		ans = "invalid";
	else
		ans = google_dpa_state_string[state];

	return sysfs_emit(buf, "%s\n", ans);
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr, const char *buf,
			   size_t count)
{
	struct google_dpa *dpa = get_google_dpa(dev);
	int ret = 0;

	if (sysfs_streq(buf, "start")) {
		ret = google_dpa_boot(dpa);
		if (ret)
			dev_err(dev, "Failed to boot DPA: %d\n", ret);
	} else if (sysfs_streq(buf, "stop")) {
		ret = google_dpa_shutdown(dpa, /*is_graceful=*/true);
		if (ret)
			dev_err(dev, "Failed to  shutdown DPA: %d\n", ret);
	} else if (sysfs_streq(buf, "force stop")) {
		ret = google_dpa_shutdown(dpa, /*is_graceful*/ false);
		if (ret)
			dev_err(dev, "Failed to forcefully shutdown DPA: %d\n", ret);
	} else if (sysfs_streq(buf, "unload")) {
		ret = google_dpa_unload(dpa);
		if (ret)
			dev_err(dev, "Failed to unload DPA firmware: %d\n", ret);
	} else {
		dev_err(dev, "Unrecognised option: %s\n", buf);
		ret = -EINVAL;
	}
	return ret ? ret : count;
}

static DEVICE_ATTR_RW(state);

static struct attribute *google_dpa_ncp_attrs[] = {
	&dev_attr_ncp_firmware.attr,
	NULL,
};

static struct attribute *google_dpa_nep_attrs[] = {
	&dev_attr_nep_firmware.attr,
	NULL,
};

static struct attribute *google_dpa_root_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_firmware.attr,
	NULL,
};

static struct attribute_group google_dpa_ncp_attr_group = {
	.attrs = google_dpa_ncp_attrs,
	.name = "ncp",
};

static struct attribute_group google_dpa_nep_attr_group = {
	.attrs = google_dpa_nep_attrs,
	.name = "nep",
};

static struct attribute_group google_dpa_root_attr_group = {
	.attrs = google_dpa_root_attrs,
};

static const struct attribute_group *google_dpa_device_attr_groups[] = {
	&google_dpa_ncp_attr_group,
	&google_dpa_nep_attr_group,
	&google_dpa_root_attr_group,
	NULL,
};

int google_dpa_init_sysfs(struct google_dpa *dpa)
{
	int ret = 0;

	for (int i = 0; google_dpa_device_attr_groups[i]; ++i) {
		ret = devm_device_add_group(dpa->dev, google_dpa_device_attr_groups[i]);
		if (ret)
			return ret;
	}

	ret = google_dpa_ctrl_sysfs_init(dpa);
	if (ret)
		return ret;

	ret = google_dpa_rpc_sysfs_init(dpa);

	return ret;
}
