// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#include "dptx.h"
#include "debugfs/audio_bridge.h"
#include "regmaps/audio_bridge.h"

int dptx_ag_enable_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t ag_enable;
	struct ag_regfields *ag_fields;

	ag_fields = dptx->ag_fields;

	mutex_lock(&dptx->mutex);
	ag_enable = dptx_read_regfield(dptx, ag_fields->field_agen0_en);
	seq_printf(s, "Audio Generator Enable: %u\n", ag_enable);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_ag_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_ag_enable_show, inode->i_private);
}

int dptx_ag_freq_fs_show(struct seq_file *s, void *unused)
{
	struct dptx *dptx = s->private;
	uint8_t freq_fs;
	struct ag_regfields *ag_fields;

	ag_fields = dptx->ag_fields;

	mutex_lock(&dptx->mutex);
	freq_fs = dptx_read_regfield(dptx, ag_fields->field_agen0_freq_fs);
	seq_printf(s, "Audio Generator Sample Frequency: %u\n", freq_fs);
	mutex_unlock(&dptx->mutex);

	return 0;
}

int dptx_ag_freq_fs_open(struct inode *inode, struct file *file)
{
	return single_open(file, dptx_ag_freq_fs_show, inode->i_private);
}
