// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS SLC Device Driver
 *
 * Copyright (c) 2018 Google, LLC
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-slc: " fmt

#include "lwis_device_slc.h"
#include "lwis_ioreg.h"

#include <linux/anon_inodes.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <soc/google/pt.h>

#if IS_ENABLED(CONFIG_OF)
#include "lwis_dt.h"
#endif

#define LWIS_DRIVER_NAME "lwis-slc"

#define SIZE_TO_KB(x) ((x) / 1024)

static const struct file_operations pt_file_ops = {
	.owner = THIS_MODULE,
};

static int lwis_slc_disable(struct lwis_device *lwis_dev);
static int lwis_slc_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);
static int lwis_slc_register_io_locked(struct lwis_device *lwis_dev, struct lwis_io_entry *entry);

static struct lwis_device_subclass_operations slc_vops = {
	.register_io = lwis_slc_register_io,
	.register_io_locked = lwis_slc_register_io_locked,
	.batch_register_io = NULL,
	.register_io_barrier = NULL,
	.device_enable = NULL,
	.device_disable = lwis_slc_disable,
	.device_resume = NULL,
	.device_suspend = NULL,
	.event_enable = NULL,
	.event_flags_updated = NULL,
	.close = NULL,
};

static int lwis_slc_disable(struct lwis_device *lwis_dev)
{
	struct lwis_ioreg_device *ioreg_dev =
		container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	struct lwis_slc_device *slc_dev = container_of(ioreg_dev, struct lwis_slc_device, io_dev);
	int i = 0;

	if (!lwis_dev) {
		pr_err("LWIS device cannot be NULL\n");
		return -ENODEV;
	}

	if (!slc_dev->partition_handle) {
		dev_err(lwis_dev->dev, "Partition handle is NULL\n");
		return -ENODEV;
	}
	for (i = 0; i < slc_dev->num_pt; i++) {
		if (slc_dev->pt[i].partition_id != PT_PTID_INVALID) {
			dev_info(lwis_dev->dev, "Closing partition id %d at device shutdown",
				 slc_dev->pt[i].id);
			pt_client_disable(slc_dev->partition_handle, slc_dev->pt[i].id);
			slc_dev->pt[i].partition_id = PT_PTID_INVALID;
			slc_dev->pt[i].fd = -1;
		}
	}
	return 0;
}

static int lwis_slc_register_io(struct lwis_device *lwis_dev, struct lwis_io_entry *entry)
{
	struct lwis_ioreg_device *ioreg_dev =
		container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	lwis_save_register_io_info(lwis_dev, entry);
	return lwis_ioreg_io_entry_rw(ioreg_dev, entry);
}

static int lwis_slc_register_io_locked(struct lwis_device *lwis_dev, struct lwis_io_entry *entry)
{
	struct lwis_ioreg_device *ioreg_dev =
		container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	lwis_save_register_io_info(lwis_dev, entry);
	return lwis_ioreg_io_entry_rw_locked(ioreg_dev, entry);
}

static void print_logs_for_pt_matching_failure(struct lwis_slc_device *slc_dev,
					       uint64_t requested_size_in_kb)
{
	int i;

	dev_err(slc_dev->io_dev.base_dev.dev,
		"Failed to find valid partition, largest size supported is %zuKB, asking for %lluKB\n",
		slc_dev->pt[slc_dev->num_pt - 1].size_kb, (unsigned long long)requested_size_in_kb);
	for (i = 0; i < slc_dev->num_pt; i++) {
		dev_err(slc_dev->io_dev.base_dev.dev, "Partition[%d]: size %zuKB is %s\n", i,
			slc_dev->pt[i].size_kb,
			(slc_dev->pt[i].partition_id == PT_PTID_INVALID) ? "NOT in use" : "in use");
	}
}

int lwis_slc_buffer_alloc(struct lwis_device *lwis_dev, struct lwis_alloc_buffer_info *alloc_info)
{
	struct lwis_ioreg_device *ioreg_dev =
		container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	struct lwis_slc_device *slc_dev = container_of(ioreg_dev, struct lwis_slc_device, io_dev);
	int i = 0, fd_or_err = -1;
	ptid_t partition_id = PT_PTID_INVALID;

	if (!lwis_dev) {
		pr_err("LWIS device cannot be NULL\n");
		return -ENODEV;
	}

	if (!alloc_info) {
		dev_err(lwis_dev->dev, "Buffer alloc info is NULL\n");
		return -EINVAL;
	}

	if (!slc_dev->partition_handle) {
		dev_err(lwis_dev->dev, "Partition handle is NULL\n");
		return -ENODEV;
	}

	if (slc_dev->num_pt <= 0) {
		dev_err(lwis_dev->dev, "No valid partitions is found in SLC\n");
		return -EINVAL;
	}

	for (i = 0; i < slc_dev->num_pt; i++) {
		if (slc_dev->pt[i].partition_id == PT_PTID_INVALID &&
		    slc_dev->pt[i].size_kb >= SIZE_TO_KB(alloc_info->size)) {
			partition_id =
				pt_client_enable(slc_dev->partition_handle, slc_dev->pt[i].id);
			if (partition_id != PT_PTID_INVALID) {
				fd_or_err = anon_inode_getfd("slc_pt_file", &pt_file_ops,
							     &slc_dev->pt[i], O_CLOEXEC);
				if (fd_or_err < 0) {
					dev_err(lwis_dev->dev,
						"Failed to create a new file instance for the partition\n");
					return fd_or_err;
				}
				slc_dev->pt[i].fd = fd_or_err;
				slc_dev->pt[i].partition_id = partition_id;
				alloc_info->dma_fd = fd_or_err;
				alloc_info->partition_id = slc_dev->pt[i].partition_id;

				if (slc_dev->pt[i].size_kb > SIZE_TO_KB(alloc_info->size)) {
					dev_warn(
						lwis_dev->dev,
						"Size of SLC Partition is more than what was requested\n");
				}
				return 0;
			}

			dev_err(lwis_dev->dev, "Failed to enable partition id %d\n",
				slc_dev->pt[i].id);
			return -EPROTO;
		}
	}
	print_logs_for_pt_matching_failure(slc_dev, SIZE_TO_KB(alloc_info->size));
	return -EINVAL;
}

int lwis_slc_buffer_realloc(struct lwis_device *lwis_dev, struct lwis_alloc_buffer_info *alloc_info)
{
	struct lwis_ioreg_device *ioreg_dev =
		container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	struct lwis_slc_device *slc_dev = container_of(ioreg_dev, struct lwis_slc_device, io_dev);
	int i = 0, fd_or_err = -1;

	int pt_before_realloc = -1;
	int pt_after_realloc = -1;
	ptid_t partition_id = PT_PTID_INVALID;

	if (!lwis_dev) {
		pr_err("LWIS device cannot be NULL\n");
		return -ENODEV;
	}

	if (!alloc_info) {
		dev_err(lwis_dev->dev, "Buffer alloc info is NULL\n");
		return -EINVAL;
	}

	if (!slc_dev->partition_handle) {
		dev_err(lwis_dev->dev, "Partition handle is NULL\n");
		return -ENODEV;
	}

	if (slc_dev->num_pt <= 0) {
		dev_err(lwis_dev->dev, "No valid partitions is found in SLC\n");
		return -EINVAL;
	}

	/*
	 * Search for both the current pt allocated to the partition and the pt to
	 * allocate to the partition after reallocation.
	 */
	for (i = 0; i < slc_dev->num_pt; i++) {
		if (slc_dev->pt[i].partition_id == PT_PTID_INVALID &&
		    slc_dev->pt[i].size_kb >= SIZE_TO_KB(alloc_info->size)) {
			pt_after_realloc = i;
		} else if (slc_dev->pt[i].partition_id == alloc_info->partition_id) {
			pt_before_realloc = i;
		}

		/* Terminate the search if both pt-s have been found */
		if (pt_before_realloc != -1 && pt_after_realloc != -1)
			break;
	}

	if (pt_before_realloc == -1) {
		dev_err(lwis_dev->dev, "Failed to find current partition\n");
		return -EINVAL;
	}

	if (pt_after_realloc == -1) {
		print_logs_for_pt_matching_failure(slc_dev, SIZE_TO_KB(alloc_info->size));
		return -EINVAL;
	}

	/*Use the pt_client_mutate api to update the pt that the partition is assigned to.*/
	partition_id =
		pt_client_mutate(slc_dev->partition_handle, slc_dev->pt[pt_before_realloc].id,
				 slc_dev->pt[pt_after_realloc].id);

	if (partition_id == PT_PTID_INVALID) {
		dev_err(lwis_dev->dev, "Failed to enable partition id %d\n", slc_dev->pt[i].id);
		return -EPROTO;
	}
	if (partition_id != alloc_info->partition_id) {
		dev_err(lwis_dev->dev, "Partition ID did not remain the same after reallocation\n");
		return -EINVAL;
	}

	fd_or_err = anon_inode_getfd("slc_pt_file", &pt_file_ops, &slc_dev->pt[pt_after_realloc],
				     O_CLOEXEC);
	if (fd_or_err < 0) {
		dev_err(lwis_dev->dev, "Failed to create a new file instance for the partition\n");
		return fd_or_err;
	}
	slc_dev->pt[pt_after_realloc].fd = fd_or_err;
	slc_dev->pt[pt_after_realloc].partition_id = partition_id;
	alloc_info->dma_fd = fd_or_err;
	return 0;
}

int lwis_slc_buffer_free(struct lwis_device *lwis_dev, int fd)
{
	struct file *fp;
	struct slc_partition *slc_pt;

	if (!lwis_dev) {
		pr_err("LWIS device cannot be NULL\n");
		return -ENODEV;
	}

	fp = fget(fd);
	if (fp == NULL)
		return -EBADF;

	if (fp->f_op != &pt_file_ops) {
		dev_err(lwis_dev->dev, "SLC file ops is not equal to pt_file_ops\n");
		fput(fp);
		return -EINVAL;
	}

	slc_pt = (struct slc_partition *)fp->private_data;

	if (slc_pt->fd != fd) {
		dev_warn(lwis_dev->dev, "Stale SLC buffer free for fd %d with ptid %d\n", fd,
			 slc_pt->partition_id);
		fput(fp);
		return -EINVAL;
	}

	if (slc_pt->partition_id != PT_PTID_INVALID && slc_pt->partition_handle) {
		pt_client_disable(slc_pt->partition_handle, slc_pt->id);
		slc_pt->partition_id = PT_PTID_INVALID;
		slc_pt->fd = -1;
	}
	fput(fp);

	return 0;
}

static int lwis_slc_device_setup(struct lwis_slc_device *slc_dev)
{
	int ret = 0;
	int i;

#if IS_ENABLED(CONFIG_OF)
	/* Parse device tree for SLC register spaces (through ioreg routine) */
	ret = lwis_ioreg_device_parse_dt(&slc_dev->io_dev);
	if (ret)
		dev_err(slc_dev->io_dev.base_dev.dev, "Failed to parse IoReg device tree\n");

	ret = lwis_slc_device_parse_dt(slc_dev);
	if (ret)
		dev_err(slc_dev->io_dev.base_dev.dev, "Failed to parse SLC device tree\n");

	/* Initialize SLC partitions and get a handle */
	slc_dev->partition_handle =
		pt_client_register(slc_dev->io_dev.base_dev.k_dev->of_node, NULL, NULL);
	if (IS_ERR_OR_NULL(slc_dev->partition_handle)) {
		ret = PTR_ERR(slc_dev->partition_handle);
		dev_err(slc_dev->io_dev.base_dev.dev, "Failed to register PT client (%d)\n", ret);
		slc_dev->partition_handle = NULL;
		if (ret == -ENOENT)  // gslc's probe is not completed yet
			ret = -EPROBE_DEFER;
		return ret;
	}

	for (i = 0; i < slc_dev->num_pt; i++)
		slc_dev->pt[i].partition_handle = slc_dev->partition_handle;
#else
	/* Non-device-tree init: Save for future implementation */
	ret = -ENOSYS;
#endif

	return ret;
}

static int lwis_slc_device_probe(struct platform_device *plat_dev)
{
	int ret = 0;
	struct lwis_slc_device *slc_dev;
	struct device *dev = &plat_dev->dev;

	/* Allocate SLC device specific data construct */
	slc_dev = devm_kzalloc(dev, sizeof(struct lwis_slc_device), GFP_KERNEL);
	if (!slc_dev)
		return -ENOMEM;

	slc_dev->io_dev.base_dev.type = DEVICE_TYPE_SLC;
	slc_dev->io_dev.base_dev.vops = slc_vops;
	slc_dev->io_dev.base_dev.plat_dev = plat_dev;
	slc_dev->io_dev.base_dev.k_dev = &plat_dev->dev;

	/* Call the base device probe function */
	ret = lwis_base_probe(&slc_dev->io_dev.base_dev);
	if (ret) {
		dev_err(dev, "Error in lwis base probe\n");
		return ret;
	}
	platform_set_drvdata(plat_dev, &slc_dev->io_dev.base_dev);

	/* Call SLC device specific setup function */
	ret = lwis_slc_device_setup(slc_dev);
	if (ret) {
		dev_err(dev, "Error in SLC device initialization\n");
		lwis_base_unprobe(&slc_dev->io_dev.base_dev);
		return ret;
	}

	dev_info(dev, "SLC Device Probe: Success\n");

	return 0;
}

static void lwis_slc_device_remove(struct platform_device *plat_dev)
{
	struct lwis_device *lwis_dev = container_of(&plat_dev, struct lwis_device, plat_dev);
	struct lwis_ioreg_device *ioreg_dev =
		container_of(lwis_dev, struct lwis_ioreg_device, base_dev);
	struct lwis_slc_device *slc_dev = container_of(ioreg_dev, struct lwis_slc_device, io_dev);

	if (slc_dev->partition_handle)
		pt_client_unregister(slc_dev->partition_handle);

	return;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id lwis_id_match[] = {
	{ .compatible = LWIS_SLC_DEVICE_COMPAT },
	{},
};
// MODULE_DEVICE_TABLE(of, lwis_id_match);
static struct platform_driver lwis_driver = {
	.probe = lwis_slc_device_probe,
	.remove = lwis_slc_device_remove,
	.driver = {
		.name = LWIS_DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lwis_id_match,
	},
};
#else /* CONFIG_OF not defined */
static struct platform_device_id lwis_driver_id[] = {
	{
		.name = LWIS_DRIVER_NAME,
		.driver_data = 0,
	},
	{},
};
MODULE_DEVICE_TABLE(platform, lwis_driver_id);
static struct platform_driver lwis_driver = { .probe = lwis_slc_device_probe,
					      .remove = lwis_slc_device_remove,
					      .id_table = lwis_driver_id,
					      .driver = {
						      .name = LWIS_DRIVER_NAME,
						      .owner = THIS_MODULE,
					      } };
#endif /* CONFIG_OF */

/*
 *  lwis_slc_device_init: Init function that will be called by the kernel
 *  initialization routines.
 */
int __init lwis_slc_device_init(void)
{
	int ret = 0;

	pr_info("SLC device initialization\n");

	ret = platform_driver_register(&lwis_driver);
	if (ret)
		pr_err("platform_driver_register failed: %d\n", ret);

	return ret;
}

int lwis_slc_device_deinit(void)
{
	platform_driver_unregister(&lwis_driver);
	return 0;
}
