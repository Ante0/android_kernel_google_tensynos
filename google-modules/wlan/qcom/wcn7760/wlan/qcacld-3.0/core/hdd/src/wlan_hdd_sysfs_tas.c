/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <wlan_hdd_includes.h>
#include "osif_psoc_sync.h"
#include <wlan_hdd_sysfs.h>
#include <wlan_hdd_sysfs_tas.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#define TAS_ENABLE 1
#define TAS_DISABLE 0

/**
 * __hdd_sysfs_tas_show() - Show TAS configuration (internal)
 * @hdd_ctx: HDD context
 * @attr: sysfs attribute
 * @buf: output buffer
 *
 * Internal function to show current TAS configuration
 *
 * Return: number of bytes written to buffer
 */
static ssize_t __hdd_sysfs_tas_show(struct hdd_context *hdd_ctx,
				    struct kobj_attribute *attr,
				    char *buf)
{
	if (!hdd_ctx) {
		hdd_err_rl("invalid input");
		return -EINVAL;
	}

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 hdd_ctx->tas_enabled ? 1 : 0);
}

/**
 * hdd_sysfs_tas_show() - Show TAS configuration via sysfs
 * @kobj: kernel object
 * @attr: sysfs attribute
 * @buf: output buffer
 *
 * Sysfs show handler for TAS configuration
 *
 * Return: number of bytes written to buffer, or negative errno on failure
 */
static ssize_t hdd_sysfs_tas_show(struct kobject *kobj,
				  struct kobj_attribute *attr,
				  char *buf)
{
	struct osif_psoc_sync *psoc_sync;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	ssize_t errno_size;

	if (!hdd_ctx || !hdd_ctx->wiphy) {
		hdd_err_rl("invalid input");
		return -EINVAL;
	}

	errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy),
					     &psoc_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_tas_show(hdd_ctx, attr, buf);

	if (psoc_sync)
		osif_psoc_sync_op_stop(psoc_sync);

	return errno_size;
}

/**
 * __hdd_sysfs_tas_store() - Store TAS configuration (internal)
 * @hdd_ctx: HDD context
 * @attr: sysfs attribute
 * @buf: input buffer
 * @count: size of input buffer
 *
 * Internal function to store TAS configuration
 *
 * Return: number of bytes consumed from buffer, or negative errno on failure
 */
static ssize_t
__hdd_sysfs_tas_store(struct hdd_context *hdd_ctx,
		      struct kobj_attribute *attr,
		      const char *buf, size_t count)
{
	char buf_local[MAX_SYSFS_USER_COMMAND_SIZE_LENGTH + 1];
	char *sptr, *token;
	uint32_t value;
	int ret;

	if (!hdd_ctx) {
		hdd_err_rl("invalid hdd ctx");
		return -EINVAL;
	}

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
	if (kstrtou32(token, 0, &value))
		return -EINVAL;

	if (value == TAS_DISABLE)
		hdd_ctx->tas_enabled = false;
	else
		hdd_ctx->tas_enabled = true;

	hdd_info("TAS %s via sysfs",
		 hdd_ctx->tas_enabled ? "enabled" : "disabled");
	hdd_ctx->tas_send_to_fw = true;
	/* Check if pdev is already created */
	if (hdd_ctx->pdev) {
		/* Pdev exists, send TAS mode to firmware */
		ret = hdd_send_tas_mode(hdd_ctx);
		if (ret) {
			hdd_err("Failed to send TAS mode to FW, ret: %d", ret);
			/* Set flag to indicate TAS mode needs to be sent
			 * later once pdev is destroyed and created again.
			 */
			return ret;
		}
	} else {
		/* Pdev not created yet, set flag to send TAS mode later */
		hdd_info("Pdev not created, TAS mode will be sent after pdev create");
	}
	return count;
}

/**
 * hdd_sysfs_tas_store() - Store TAS configuration via sysfs
 * @kobj: kernel object
 * @attr: sysfs attribute
 * @buf: input buffer
 * @count: size of input buffer
 *
 * Sysfs store handler for TAS configuration
 *
 * Return: number of bytes consumed from buffer, or negative errno on failure
 */
static ssize_t
hdd_sysfs_tas_store(struct kobject *kobj,
		    struct kobj_attribute *attr,
		    char const *buf, size_t count)
{
	struct osif_psoc_sync *psoc_sync;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	ssize_t errno_size;

	if (!hdd_ctx || !hdd_ctx->wiphy) {
		hdd_err_rl("invalid input");
		return -EINVAL;
	}

	errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy),
					     &psoc_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_tas_store(hdd_ctx, attr, buf, count);
	if (psoc_sync)
		osif_psoc_sync_op_stop(psoc_sync);

	return errno_size;
}

/* TAS sysfs attribute */
static struct kobj_attribute tas_attribute =
	__ATTR(tas_enable, 0664, hdd_sysfs_tas_show,
	       hdd_sysfs_tas_store);

int hdd_sysfs_tas_create(struct kobject *driver_kobject)
{
	int error;

	if (!driver_kobject) {
		hdd_err("could not get driver kobject!");
		return -EINVAL;
	}

	error = sysfs_create_file(driver_kobject,
				  &tas_attribute.attr);
	if (error)
		hdd_err("could not create tas_enable sysfs file");

	return error;
}

void hdd_sysfs_tas_destroy(struct kobject *driver_kobject)
{
	if (!driver_kobject) {
		hdd_err("could not get driver kobject!");
		return;
	}
	sysfs_remove_file(driver_kobject, &tas_attribute.attr);
}
