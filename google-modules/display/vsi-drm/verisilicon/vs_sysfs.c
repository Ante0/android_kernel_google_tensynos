// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Google LLC
 */

#include <drm/drm_crtc.h>
#include <drm/drm_file.h>
#include <drm/vs_drm.h>

#include "vs_crtc.h"
#include "vs_sysfs.h"

#define CRTC_ATOMIC_ERROR_ATTR_RO(_name)                                                          \
	static ssize_t _name##_show(struct device *dev, struct device_attribute *attr, char *buf) \
	{                                                                                         \
		struct vs_crtc *vs_crtc = dev_get_drvdata(dev);                                   \
		return sysfs_emit(buf, "%d\n", atomic_read(&vs_crtc->_name));                     \
	}                                                                                         \
	DEVICE_ATTR_RO(_name)

CRTC_ATOMIC_ERROR_ATTR_RO(frame_start_timeout);
CRTC_ATOMIC_ERROR_ATTR_RO(frame_start_missing);
CRTC_ATOMIC_ERROR_ATTR_RO(frame_done_timeout);
CRTC_ATOMIC_ERROR_ATTR_RO(frame_done_missing);

static ssize_t underrun_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vs_crtc *vs_crtc = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", atomic_read(&vs_crtc->frame_underrun_count));
}
DEVICE_ATTR_RO(underrun_count);

static struct attribute *dpu_error_count_attrs[] = {
	&dev_attr_frame_start_timeout.attr,
	&dev_attr_frame_start_missing.attr,
	&dev_attr_frame_done_timeout.attr,
	&dev_attr_frame_done_missing.attr,
	&dev_attr_underrun_count.attr,
	NULL,
};

static const struct attribute_group dpu_error_count_group = {
	.name = "errors",
	.attrs = dpu_error_count_attrs,
};

#define CRTC_RECOVERY_ATTR_RO(_index, _prefix, _name)                                             \
	static ssize_t _prefix##_##_name##_show(struct device *dev,                               \
						struct device_attribute *attr, char *buf)         \
	{                                                                                         \
		struct vs_crtc *vs_crtc = dev_get_drvdata(dev);                                   \
		return sysfs_emit(buf, "%u\n", vs_crtc->recovery_stats[_index]._name);            \
	}                                                                                         \
	static DEVICE_ATTR_RO(_prefix##_##_name)

/* Manual Source */
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_MANUAL, manual, success);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_MANUAL, manual, failure);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_MANUAL, manual, max_retry);
/* Frame Update Timeout Source */
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_FRAME_UPDATE_TIMEOUT, frame_update_timeout, success);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_FRAME_UPDATE_TIMEOUT, frame_update_timeout, failure);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_FRAME_UPDATE_TIMEOUT, frame_update_timeout, max_retry);
/* DSI Error Source */
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_DSI_ERR, dsi_err, success);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_DSI_ERR, dsi_err, failure);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_DSI_ERR, dsi_err, max_retry);
/* DDIC Error Source */
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_DDIC_ERR, ddic_err, success);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_DDIC_ERR, ddic_err, failure);
CRTC_RECOVERY_ATTR_RO(SSCD_SRC_DDIC_ERR, ddic_err, max_retry);

static struct attribute *recovery_stats_attrs[] = {
	/* Manual */
	&dev_attr_manual_success.attr,
	&dev_attr_manual_failure.attr,
	&dev_attr_manual_max_retry.attr,
	/* Frame Update Timeout */
	&dev_attr_frame_update_timeout_success.attr,
	&dev_attr_frame_update_timeout_failure.attr,
	&dev_attr_frame_update_timeout_max_retry.attr,
	/* DSI Error */
	&dev_attr_dsi_err_success.attr,
	&dev_attr_dsi_err_failure.attr,
	&dev_attr_dsi_err_max_retry.attr,
	/* DDIC Error */
	&dev_attr_ddic_err_success.attr,
	&dev_attr_ddic_err_failure.attr,
	&dev_attr_ddic_err_max_retry.attr,
	NULL,
};

static const struct attribute_group recovery_stats_group = {
	.name = "recovery_stats",
	.attrs = recovery_stats_attrs,
};

static const struct attribute_group *vs_crtc_sysfs_groups[] = {
	&dpu_error_count_group,
	&recovery_stats_group,
	NULL,
};

static void vs_sysfs_dev_release(struct device *dev)
{
	kfree(dev);
}

int vs_sysfs_create_crtc_files(struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);
	int ret;

	vs_crtc->sysfs_dev = kzalloc(sizeof(*vs_crtc->sysfs_dev), GFP_KERNEL);
	if (!vs_crtc->sysfs_dev)
		return -ENOMEM;

	device_initialize(vs_crtc->sysfs_dev);
	dev_set_drvdata(vs_crtc->sysfs_dev, vs_crtc);

	vs_crtc->sysfs_dev->parent = crtc->dev->primary->kdev;
	vs_crtc->sysfs_dev->release = vs_sysfs_dev_release;
	vs_crtc->sysfs_dev->groups = vs_crtc_sysfs_groups;

	ret = dev_set_name(vs_crtc->sysfs_dev, "crtc-%u", vs_crtc->base.index);
	if (ret) {
		put_device(vs_crtc->sysfs_dev);
		vs_crtc->sysfs_dev = NULL;
		return ret;
	}

	ret = device_add(vs_crtc->sysfs_dev);
	if (ret) {
		put_device(vs_crtc->sysfs_dev);
		vs_crtc->sysfs_dev = NULL;
		return ret;
	}

	return 0;
}

void vs_sysfs_remove_crtc_files(struct drm_crtc *crtc)
{
	struct vs_crtc *vs_crtc = to_vs_crtc(crtc);

	if (vs_crtc->sysfs_dev) {
		device_unregister(vs_crtc->sysfs_dev);
		vs_crtc->sysfs_dev = NULL;
	}
}
