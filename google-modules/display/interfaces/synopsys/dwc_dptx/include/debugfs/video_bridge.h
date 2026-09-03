/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

int dptx_vg_enable_show(struct seq_file *s, void *unused);
int dptx_vg_enable_open(struct inode *inode, struct file *file);

int dptx_vg_colorimetry_show(struct seq_file *s, void *unused);
int dptx_vg_colorimetry_open(struct inode *inode, struct file *file);

int dptx_vg_colordepth_show(struct seq_file *s, void *unused);
int dptx_vg_colordepth_open(struct inode *inode, struct file *file);

int dptx_vg_patt_mode_show(struct seq_file *s, void *unused);
int dptx_vg_patt_mode_open(struct inode *inode, struct file *file);

int dptx_vg_hactive_show(struct seq_file *s, void *unused);
int dptx_vg_hactive_open(struct inode *inode, struct file *file);

int dptx_vg_hblank_show(struct seq_file *s, void *unused);
int dptx_vg_hblank_open(struct inode *inode, struct file *file);

int dptx_vg_vactive_show(struct seq_file *s, void *unused);
int dptx_vg_vactive_open(struct inode *inode, struct file *file);

int dptx_vg_vblank_show(struct seq_file *s, void *unused);
int dptx_vg_vblank_open(struct inode *inode, struct file *file);

static const struct file_operations dptx_vg_enable_fops = {
	.open	= dptx_vg_enable_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_vg_colorimetry_fops = {
	.open	= dptx_vg_colorimetry_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_vg_colordepth_fops = {
	.open	= dptx_vg_colordepth_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_vg_patt_mode_fops = {
	.open	= dptx_vg_patt_mode_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_vg_hactive_fops = {
	.open	= dptx_vg_hactive_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_vg_hblank_fops = {
	.open	= dptx_vg_hblank_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_vg_vactive_fops = {
	.open	= dptx_vg_vactive_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_vg_vblank_fops = {
	.open	= dptx_vg_vblank_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
