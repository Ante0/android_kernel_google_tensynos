// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "debugfs/video_bridge.h"
#include "regmaps/video_bridge.h"

int dptx_vg_enable_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_enable;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_enable = dptx_read_regfield(dptx, vg_fields->field_vpg0_en);
	seq_printf(s, "Video Generator Enable: %u\n", vg_enable);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_enable_show, inode->i_private);
}

int dptx_vg_colorimetry_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_colorimetry;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_colorimetry = dptx_read_regfield(dptx, vg_fields->field_vpg0_colorimetry);
	seq_printf(s, "Video Generator Colorimetry: %u\n", vg_colorimetry);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_colorimetry_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_colorimetry_show, inode->i_private);
}

int dptx_vg_colordepth_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_colordepth;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_colordepth = dptx_read_regfield(dptx, vg_fields->field_vpg0_colordepth);
	seq_printf(s, "Video Generator Colordepth: %u\n", vg_colordepth);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_colordepth_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_colordepth_show, inode->i_private);
}

int dptx_vg_patt_mode_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_patt_mode;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_patt_mode = dptx_read_regfield(dptx, vg_fields->field_vpg0_patt_mode);
	seq_printf(s, "Video Generator Pattern Mode: %u\n", vg_patt_mode);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_patt_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_patt_mode_show, inode->i_private);
}

int dptx_vg_hactive_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_hactive;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_hactive = dptx_read_regfield(dptx, vg_fields->field_vpg0_hactive);
	seq_printf(s, "Video Generator HActive: %u\n", vg_hactive);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_hactive_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_hactive_show, inode->i_private);
}

int dptx_vg_hblank_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_hblank;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_hblank = dptx_read_regfield(dptx, vg_fields->field_vpg0_hblank);
	seq_printf(s, "Video Generator HBlank: %u\n", vg_hblank);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_hblank_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_hblank_show, inode->i_private);
}

int dptx_vg_vactive_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_vactive;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_vactive = dptx_read_regfield(dptx, vg_fields->field_vpg0_vactive);
	seq_printf(s, "Video Generator VActive: %u\n", vg_vactive);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_vactive_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_vactive_show, inode->i_private);
}

int dptx_vg_vblank_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t vg_vblank;
	struct vg_regfields *vg_fields;

	vg_fields = dptx->vg_fields;

	mutex_lock(&dptx->mutex);
	vg_vblank = dptx_read_regfield(dptx, vg_fields->field_vpg0_vblank);
	seq_printf(s, "Video Generator VBlank: %u\n", vg_vblank);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_vg_vblank_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_vg_vblank_show, inode->i_private);
}
