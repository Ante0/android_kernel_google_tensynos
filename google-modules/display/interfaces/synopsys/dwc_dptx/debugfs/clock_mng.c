// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "rst_mng.h"
#include "debugfs/clock_mng.h"
#include "regmaps/clock_mng.h"

int dptx_clkmng_video_state_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t video_clock_state;
	struct clkmng_regfields *clkmng_fields;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);
	video_clock_state = dptx_read_regfield(dptx, clkmng_fields->field_video_clock_state);
	video_clock_state++;
	seq_printf(s, "Video Clock State: %u\n", video_clock_state);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_clkmng_video_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_clkmng_video_state_show, inode->i_private);
}

ssize_t dptx_clkmng_video_state_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	int clock_state;
	struct clkmng_regfields *clkmng_fields;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);

	retval = kstrtoint_from_user(ubuf, count, 10, &clock_state);
	if (retval < 0)
		goto done;

	if (clock_state > 0)
		clock_state -= 1; //Align value with state -> State 1 == Value 0

	configure_mmcm(dptx, clock_state, VIDEO);
	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

int dptx_clkmng_video_enable_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t video_clock_enable;
	struct clkmng_regfields *clkmng_fields;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);
	video_clock_enable = dptx_read_regfield(dptx, clkmng_fields->field_video_sen);
	seq_printf(s, "Video Clock Enable: %u\n", video_clock_enable);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_clkmng_video_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_clkmng_video_enable_show, inode->i_private);
}

ssize_t dptx_clkmng_video_enable_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	int clock_enable;
	struct clkmng_regfields *clkmng_fields;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);

	retval = kstrtoint_from_user(ubuf, count, 10, &clock_enable);
	if (retval < 0)
		goto done;

	dptx_write_regfield(dptx, clkmng_fields->field_video_sen, clock_enable);

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}


int dptx_clkmng_video_locked_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t video_locked;
	struct clkmng_regfields *clkmng_fields;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);
	video_locked = dptx_read_regfield(dptx, clkmng_fields->field_video_locked);
	seq_printf(s, "Video Locked: %u\n", video_locked);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_clkmng_video_locked_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_clkmng_video_locked_show, inode->i_private);
}

int dptx_clkmng_audio_state_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t audio_clock_state;
	struct clkmng_regfields *clkmng_fields;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);
	audio_clock_state = dptx_read_regfield(dptx, clkmng_fields->field_audio_clock_state);
	audio_clock_state++;
	seq_printf(s, "Audio Clock State: %u\n", audio_clock_state);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_clkmng_audio_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_clkmng_audio_state_show, inode->i_private);
}

ssize_t dptx_clkmng_audio_state_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	int clock_state;
	struct clkmng_regfields *clkmng_fields;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);

	retval = kstrtoint_from_user(ubuf, count, 10, &clock_state);
	if (retval < 0)
		goto done;

	if (clock_state > 0)
		clock_state -= 1; //Align value with state -> State 1 == Value 0

	dptx_write_regfield(dptx, clkmng_fields->field_audio_clock_state, clock_state);

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}

int dptx_clkmng_audio_enable_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t audio_clock_enable;
	struct clkmng_regfields *clkmng_fields;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);
	audio_clock_enable = dptx_read_regfield(dptx, clkmng_fields->field_audio_sen);
	seq_printf(s, "Audio Clock Enable: %u\n", audio_clock_enable);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_clkmng_audio_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_clkmng_audio_enable_show, inode->i_private);
}

ssize_t dptx_clkmng_audio_enable_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	int retval = 0;
	int clock_enable;
	struct clkmng_regfields *clkmng_fields;

	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);

	retval = kstrtoint_from_user(ubuf, count, 10, &clock_enable);
	if (retval < 0)
		goto done;

	dptx_write_regfield(dptx, clkmng_fields->field_audio_sen, clock_enable);

	retval = count;
done:
	mutex_unlock(&dptx->mutex);
	return retval;
}


int dptx_clkmng_audio_locked_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t audio_locked;
	struct clkmng_regfields *clkmng_fields;

	clkmng_fields = dptx->clkmng_fields;

	mutex_lock(&dptx->mutex);
	audio_locked = dptx_read_regfield(dptx, clkmng_fields->field_audio_locked);
	seq_printf(s, "Audio Locked: %u\n", audio_locked);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_clkmng_audio_locked_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_clkmng_audio_locked_show, inode->i_private);
}


ssize_t dptx_rst_clkmng_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct dptx *dptx = s->private;

	mutex_lock(&dptx->mutex);

	rst_clkmng(dptx);

	mutex_unlock(&dptx->mutex);
	return count;
}

int dptx_rst_clkmng_show(struct seq_file *s, void *unused)
{
	return 0;
}

int dptx_rst_clkmng_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_rst_clkmng_show, inode->i_private);
}
