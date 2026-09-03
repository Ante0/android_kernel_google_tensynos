// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Codec3P video accelerator
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "vpu_debugfs.h"
#include "vpu_priv.h"

static int dmabuf_info_show(struct seq_file *s, void *unused)
{
	struct vpu_debugfs *debugfs = s->private;
	struct vpu_core *core = container_of(debugfs, struct vpu_core, debugfs);
	struct vpu_dmabuf_list *dmabuf_list;
	struct vpu_dmabuf_info *curr;
	int count;

	if (debugfs->idx >= MAX_NUM_INST) {
		seq_printf(s, "invalid idx %d\n", debugfs->idx);
		return 0;
	}

	dmabuf_list = &core->dmabuf_list[debugfs->idx];
	seq_printf(s, "dump dmabuf info for instance %d\n", debugfs->idx);
	mutex_lock(&dmabuf_list->lock);
	count = 0;
	list_for_each_entry(curr, &dmabuf_list->allocs, list) {
		seq_printf(s, "allocation[%d] fd %d iova %pad size %zu\n",
				count, curr->fd, &curr->iova, curr->size);
		count++;
	}

	count = 0;
	list_for_each_entry(curr, &dmabuf_list->mappings, list) {
		seq_printf(s, "mapping[%d] fd %d iova %pad size %zu\n",
				count, curr->fd, &curr->iova, curr->size);
		count++;
	}
	mutex_unlock(&dmabuf_list->lock);

	return 0;
}

static int power_status_show(struct seq_file *s, void *unused)
{
	struct vpu_debugfs *debugfs = s->private;
	struct vpu_core *core = container_of(debugfs, struct vpu_core, debugfs);

	seq_printf(s, "power_status %d\n", (int)core->power_status);
	return 0;
}

static int vpu_dmabuf_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, dmabuf_info_show, inode->i_private);
}

static int vpu_power_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, power_status_show, inode->i_private);
}

static const struct file_operations debug_info_fops = {
	.open = vpu_dmabuf_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations power_status_fops = {
	.open = vpu_power_status_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void vpu_init_debugfs(struct vpu_debugfs *debugfs)
{
	debugfs->fixed_rate = 0;
	debugfs->slc_disable = 0;
	debugfs->slc_option = 0;
	debugfs->root = debugfs_create_dir("vpu", NULL);
	debugfs_create_u32("fixed_rate", 0600, debugfs->root, &debugfs->fixed_rate);
	debugfs_create_u32("inst_idx", 0600, debugfs->root, &debugfs->idx);
	debugfs_create_u32("slc_disable", 0600, debugfs->root, &debugfs->slc_disable);
	debugfs_create_u32("slc_option", 0600, debugfs->root, &debugfs->slc_option);
	debugfs_create_file("dmabuf_info", 0400, debugfs->root, debugfs, &debug_info_fops);
	debugfs_create_file("power_status", 0400, debugfs->root, debugfs, &power_status_fops);
}

void vpu_deinit_debugfs(struct vpu_debugfs *debugfs)
{
	debugfs_remove_recursive(debugfs->root);
}
