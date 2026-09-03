/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * DOC: wlan_hdd_sysfs_dp_dal.c
 *
 * implementation for creating dal sysfs file
 */

#include <wlan_hdd_includes.h>
#include <wlan_hdd_sysfs.h>
#include <osif_psoc_sync.h>
#include <wlan_hdd_sysfs_dp_dal.h>

static ssize_t
__hdd_sysfs_dp_dal_sim_mode_show(
		struct hdd_context *hdd_ctx,
		struct kobj_attribute *attr, char *buf)
{
	ol_txrx_soc_handle soc = cds_get_context(QDF_MODULE_ID_SOC);
	uint8_t mode;

	if (!wlan_hdd_validate_modules_state(hdd_ctx) || !soc)
		return -EINVAL;

	mode = cdp_dal_sim_get_curr_mode(soc);
	return scnprintf(buf, PAGE_SIZE, "%u\n", mode);
}

static ssize_t
hdd_sysfs_dp_dal_sim_mode_show(
			struct kobject *kobj,
			struct kobj_attribute *attr, char *buf)
{
	struct osif_psoc_sync *psoc_sync;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	ssize_t errno_size;
	int ret;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy),
					     &psoc_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_dp_dal_sim_mode_show(hdd_ctx, attr, buf);

	osif_psoc_sync_op_stop(psoc_sync);

	return errno_size;
}

static ssize_t
__hdd_sysfs_dp_dal_sim_mode_store(struct hdd_context *hdd_ctx,
				  struct kobj_attribute *attr,
				  const char *buf, size_t count)
{
	char buf_local[MAX_SYSFS_USER_COMMAND_SIZE_LENGTH + 1];
	char *sptr, *token;
	uint8_t mode;
	int ret;
	ol_txrx_soc_handle soc = cds_get_context(QDF_MODULE_ID_SOC);

	if (!wlan_hdd_validate_modules_state(hdd_ctx) || !soc)
		return -EINVAL;

	ret = hdd_sysfs_validate_and_copy_buf(buf_local, sizeof(buf_local),
					      buf, count);
	if (ret) {
		hdd_err_rl("invalid input");
		return ret;
	}

	sptr = buf_local;
	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou8(token, 0, &mode))
		return -EINVAL;

	hdd_debug("dal_sim_mode: %u", mode);

	cdp_dal_sim_trigger_mode_switch(soc, mode);

	return count;
}

static ssize_t
hdd_sysfs_dp_dal_sim_mode_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				char const *buf, size_t count)
{
	struct osif_psoc_sync *psoc_sync;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	ssize_t errno_size;
	int ret;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy),
					     &psoc_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_dp_dal_sim_mode_store(hdd_ctx, attr,
						       buf, count);

	osif_psoc_sync_op_stop(psoc_sync);

	return errno_size;
}

static struct kobj_attribute dal_sim_mode_attribute =
	__ATTR(dal_sim_mode, 0664, hdd_sysfs_dp_dal_sim_mode_show,
	       hdd_sysfs_dp_dal_sim_mode_store);

int hdd_sysfs_dp_dal_create(struct kobject *driver_kobject)
{
	int error;

	if (!driver_kobject) {
		hdd_err("could not get driver kobject!");
		return -EINVAL;
	}

	error = sysfs_create_file(driver_kobject, &dal_sim_mode_attribute.attr);
	if (error)
		hdd_err("could not create dal_sim_mode sysfs file");

	return error;
}

void hdd_sysfs_dp_dal_destroy(struct kobject *driver_kobject)
{
	if (!driver_kobject) {
		hdd_err("could not get driver kobject!");
		return;
	}

	sysfs_remove_file(driver_kobject, &dal_sim_mode_attribute.attr);
}
