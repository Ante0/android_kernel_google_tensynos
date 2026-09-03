/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

int dptx_clkmng_video_state_show(struct seq_file *s, void *unused);
int dptx_clkmng_video_state_open(struct inode *inode, struct file *file);
ssize_t dptx_clkmng_video_state_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos);

int dptx_clkmng_video_enable_show(struct seq_file *s, void *unused);
int dptx_clkmng_video_enable_open(struct inode *inode, struct file *file);
ssize_t dptx_clkmng_video_enable_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos);

int dptx_clkmng_video_locked_show(struct seq_file *s, void *unused);
int dptx_clkmng_video_locked_open(struct inode *inode, struct file *file);

int dptx_clkmng_audio_state_show(struct seq_file *s, void *unused);
int dptx_clkmng_audio_state_open(struct inode *inode, struct file *file);
ssize_t dptx_clkmng_audio_state_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos);

int dptx_clkmng_audio_enable_show(struct seq_file *s, void *unused);
int dptx_clkmng_audio_enable_open(struct inode *inode, struct file *file);
ssize_t dptx_clkmng_audio_enable_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos);

int dptx_clkmng_audio_locked_show(struct seq_file *s, void *unused);
int dptx_clkmng_audio_locked_open(struct inode *inode, struct file *file);


int dptx_rst_clkmng_show(struct seq_file *s, void *unused);
int dptx_rst_clkmng_open(struct inode *inode, struct file *file);
ssize_t dptx_rst_clkmng_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos);

static const struct file_operations dptx_clkmng_video_state_fops = {
	.open	= dptx_clkmng_video_state_open,
	.write	= dptx_clkmng_video_state_write,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_clkmng_video_enable_fops = {
	.open	= dptx_clkmng_video_enable_open,
	.write	= dptx_clkmng_video_enable_write,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_clkmng_video_locked_fops = {
	.open	= dptx_clkmng_video_locked_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_clkmng_audio_state_fops = {
	.open	= dptx_clkmng_audio_state_open,
	.write	= dptx_clkmng_audio_state_write,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_clkmng_audio_enable_fops = {
	.open	= dptx_clkmng_audio_enable_open,
	.write	= dptx_clkmng_audio_enable_write,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_clkmng_audio_locked_fops = {
	.open	= dptx_clkmng_audio_locked_open,
	.write	= NULL,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations dptx_clkmng_rst_fops = {
	.open	= dptx_rst_clkmng_open,
	.write	= dptx_rst_clkmng_write,
	.read	= seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
