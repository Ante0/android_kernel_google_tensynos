// SPDX-License-Identifier: GPL-2.0

#include <linux/sysfs.h>

#include "customer/volcanic/custom_command.h"

#include "scheduling.h"
#include "sysconfig.h"

static ssize_t scheduling_max_deferral_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pixel_dev->scheduling_max_deferral);
}

static ssize_t scheduling_max_deferral_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	IMG_INT32 rc;
	struct pixel_gpu_device *pixel_dev = device_to_pixel(dev);
	PPVRSRV_DEVICE_NODE dev_node = pixel_dev->dev_config->psDevNode;
	PVRSRV_RGXDEV_INFO *info = dev_node->pvDevice;

	if (kstrtouint(buf, 0, &pixel_dev->scheduling_max_deferral))
		return -EINVAL;

	PVRSRVPowerLockWrite(dev_node);

	rc = PVRSRVSetDevicePowerStateKM(dev_node, PVRSRV_DEV_POWER_STATE_ON,
					 PVRSRV_POWER_FLAGS_NONE);
	if (rc != PVRSRV_OK) {
		dev_err(pixel_dev->dev, "GPU poweron failed (rc %i): skip setting scheduling_max_deferral to %i",
				rc, pixel_dev->scheduling_max_deferral);
		count = -EIO;
		goto exit;

	}

	RGXFWIF_KCCB_CMD cmd = {
		.eCmdType = RGXFWIF_KCCB_CMD_PLATFORM_CMD,
		.uCmdData.sPlatformData = {
			.ui32PlatformCmd = PIXEL_RGXFWIF_PLATFORM_CMD_SET_MAX_DEFERRAL_LIMIT,
			.cmd_data.max_deferral.max_deferral = pixel_dev->scheduling_max_deferral,
		},
	};

	rc = pixel_send_custom_command(info, &cmd);
	if (rc != PVRSRV_OK) {
		dev_err(pixel_dev->dev, "pixel_send_custom_command failed %i", rc);
		count = -EIO;
	} else {
		dev_info(pixel_dev->dev, "scheduling_max_deferral set to %i",
				pixel_dev->scheduling_max_deferral);
	}

exit:
	PVRSRVPowerUnlockWrite(dev_node);

	if (rc != PVRSRV_OK)
		return rc;
	return count;
}

static DEVICE_ATTR_RW(scheduling_max_deferral);

int init_scheduling(struct pixel_gpu_device *pixel_dev)
{
	pixel_dev->scheduling_max_deferral = 0;
	int rc = sysfs_create_file(&pixel_dev->dev->kobj, &dev_attr_scheduling_max_deferral.attr);
	if (rc < 0)
		dev_warn(pixel_dev->dev, "Unable to create scheduling_max_deferral file");
	return rc;
}

void deinit_scheduling(struct pixel_gpu_device *pixel_dev)
{
	sysfs_remove_file(&pixel_dev->dev->kobj, &dev_attr_scheduling_max_deferral.attr);
}


