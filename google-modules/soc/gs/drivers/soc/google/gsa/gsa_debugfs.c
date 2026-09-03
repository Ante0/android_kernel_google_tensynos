// SPDX-License-Identifier: GPL-2.0-only
/*
 * DebugFS interface for the Google GSA core kernel driver.
 *
 * Copyright (C) 2024 Google LLC
 */
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/minmax.h>

#include "gsa_mbox.h"
#include "gsa_priv.h"

#define GSA_WAKELOCK_ACQUIRE  1
#define GSA_WAKELOCK_RELEASE  0

static int gsa_debugfs_awake_set(void *device, u64 val)
{
	int ret;
	u32 cmd;
	struct device *gsa = device;

	if (val == GSA_WAKELOCK_ACQUIRE) {
		cmd = GSA_MB_CMD_WAKELOCK_ACQUIRE;
	} else if (val == GSA_WAKELOCK_RELEASE) {
		cmd = GSA_MB_CMD_WAKELOCK_RELEASE;
	} else {
		dev_err(gsa, "Invalid GSA command\n");
		return -EINVAL;
	}

	ret = gsa_send_cmd(gsa, cmd, NULL, 0, NULL, 0);
	if (ret < 0)
		dev_err(gsa, "GSA wakelock command fail\n");
	return ret;
}

DEFINE_DEBUGFS_ATTRIBUTE(gsa_debugfs_awake_fops, NULL, gsa_debugfs_awake_set, "%llu\n");

static ssize_t gsa_debugfs_version_read(struct file *filp, char __user *buf, size_t count,
					loff_t *ppos)
{
	ssize_t len;
	struct device *gsa = filp->private_data;
	char *local_buf = kzalloc(PAGE_SIZE, GFP_KERNEL);

	if (!local_buf)
		return -ENOMEM;

	len = gsa_get_gsa_version(gsa, local_buf);
	if (len < 0) {
		kfree(local_buf);
		return -EINVAL;
	}

	len = simple_read_from_buffer(buf, count, ppos, local_buf, len);
	kfree(local_buf);

	return len;
}

static const struct file_operations gsa_debugfs_version_fops = {
	.open = simple_open,
	.read = gsa_debugfs_version_read,
};

static int gsa_trace_dump(struct gsa_mbox *mb, dma_addr_t buf, size_t buf_size,
			  u32 *recved_data, u32 *total_data)
{
	u32 req[GSA_TRACE_DUMP_REQ_ARGC] = { 0 };
	u32 rsp[GSA_TRACE_DUMP_RSP_ARGC] = { 0 };
	int ret;

	req[GSA_TRACE_DUMP_BUF_ADDR_LO_IDX] = (u32)buf;
	req[GSA_TRACE_DUMP_BUF_ADDR_HI_IDX] = (u32)(buf >> 32);
	req[GSA_TRACE_DUMP_BUF_SIZE_IDX] = buf_size;
	ret = gsa_send_mbox_cmd(mb, GSA_MB_CMD_RUN_TRACE_DUMP, req, GSA_TRACE_DUMP_REQ_ARGC, rsp,
				GSA_TRACE_DUMP_RSP_ARGC);
	if (ret < 0)
		return ret;
	*recved_data = rsp[GSA_TRACE_DUMP_RSP_SENT_DATA_IDX];
	*total_data = rsp[GSA_TRACE_DUMP_RSP_TOTAL_DATA_IDX];
	return 0;
}

static ssize_t gsa_debugfs_trace_read(struct file *filp, char __user *buf,
				      size_t count, loff_t *ppos)
{
	struct device *dev = filp->private_data;
	struct platform_device *pdev = to_platform_device(dev);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	u32 recved_data = 0;
	u32 total_data = 0;
	ssize_t ret = 0;
	dma_addr_t dma_hdl = 0;
	size_t alloc_pages = max_t(size_t, (*ppos + count + PAGE_SIZE - 1) / PAGE_SIZE, 1);
	size_t alloc_size = alloc_pages * PAGE_SIZE;
	void *dma_addr = dma_alloc_coherent(dev, alloc_size, &dma_hdl,
					    GFP_KERNEL);

	dev_dbg(dev, "Trace output position: %lld.\n", *ppos);
	dev_dbg(dev, "Trace output buffer size: %zu.\n", count);
	dev_dbg(dev, "Trace read size: %zu.\n", alloc_size);
	if (dma_addr == NULL) {
		dev_err(dev, "DMA alloc failed.\n");
		ret = -ENOMEM;
		goto exit;
	}

	ret = gsa_trace_dump(s->mb, dma_hdl, alloc_size, &recved_data,
			     &total_data);
	if (ret < 0) {
		dev_err(dev, "Send command failed: %ld.\n", ret);
		ret = -EIO;
		goto release_memory;
	}
	if (recved_data < total_data)
		dev_info(dev, "Unfetched dump data (%u/%u).\n", recved_data,
			 total_data);
	ret = simple_read_from_buffer(buf, count, ppos, dma_addr, recved_data);
release_memory:
	if (dma_addr != NULL)
		dma_free_coherent(dev, alloc_size, dma_addr, dma_hdl);
exit:
	return ret;
}

static const struct file_operations gsa_debugfs_trace_fops = {
	.open = simple_open,
	.read = gsa_debugfs_trace_read,
};

static int gsa_debugfs_pm_stat_open(struct inode *inode, struct file *filp)
{
	struct device *dev = inode->i_private;
	struct platform_device *pdev = to_platform_device(dev);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	return gsa_pm_stat_open(dev, s, filp);
}

const struct file_operations gsa_debugfs_pm_stats_fops = {
	.open = gsa_debugfs_pm_stat_open,
	.read = gsa_pm_stat_read,
	.release = gsa_pm_state_release,
};

void gsa_debugfs_init(struct device *gsa)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);
	struct dentry *gsa_df = debugfs_create_dir("gsa", 0);

	debugfs_create_file("version", 0600, gsa_df, gsa, &gsa_debugfs_version_fops);
	debugfs_create_file("stay_awake", 0600, gsa_df, gsa, &gsa_debugfs_awake_fops);
	debugfs_create_file("trace", 0600, gsa_df, gsa, &gsa_debugfs_trace_fops);
	debugfs_create_file("pm_stat", 0400, gsa_df, gsa, &gsa_debugfs_pm_stats_fops);

	s->debugfs_dir = gsa_df;
}

void gsa_debugfs_exit(struct device *gsa)
{
	struct platform_device *pdev = to_platform_device(gsa);
	struct gsa_dev_state *s = platform_get_drvdata(pdev);

	debugfs_remove_recursive(s->debugfs_dir);
}
