/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

int dptx_ag_enable_show(struct seq_file *s, void *unused);
int dptx_ag_enable_open(struct inode *inode, struct file *file);

int dptx_ag_freq_fs_show(struct seq_file *s, void *unused);
int dptx_ag_freq_fs_open(struct inode *inode, struct file *file);

static const struct file_operations dptx_ag_enable_fops = {
	.open	= dptx_ag_enable_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_ag_freq_fs_fops = {
	.open	= dptx_ag_freq_fs_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
