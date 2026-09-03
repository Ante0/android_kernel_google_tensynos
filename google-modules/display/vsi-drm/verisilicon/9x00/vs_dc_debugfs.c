// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#include "vs_dc_debugfs.h"

#include <drm/drm_print.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>

#include "vs_dc.h"
#include "vs_dc_pre.h"
#include "vs_dc_post.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int vs_dc_reg_dump(struct seq_file *s, struct dc_hw *hw, enum dc_hw_reg_bank_type reg_type)
{
	int ret, ret_dump;
	struct drm_printer p = drm_seq_file_printer(s);

	if (!s || !hw)
		return -EINVAL;

	if (hw->reg_dump_offset > hw->reg_size) {
		seq_printf(s, "Offset %u exceeds register range %u\n", hw->reg_dump_offset,
			   hw->reg_size);
		return -EINVAL;
	}

	if (hw->reg_dump_offset + hw->reg_dump_size > hw->reg_size) {
		seq_printf(s, "Exceeding register range: offset %u, dump size %u\n",
			   hw->reg_dump_offset, hw->reg_dump_size);
		hw->reg_dump_size = hw->reg_size - hw->reg_dump_offset;
	}

	ret = dc_be_power_get_if_active(hw->dev);
	if (ret < 0) {
		dev_err(hw->dev, "Failed to power BE ON, ret %d\n", ret);
		return ret;
	}
	ret = dc_fe_power_get(hw->dev, true);
	if (ret < 0)
		dev_err(hw->dev, "Failed to power FE ON, ret %d\n", ret);

	if (!ret) {
		dev_dbg(hw->dev, "Device is powered OFF\n");
		return ret;
	}

	ret_dump = dc_hw_reg_dump(hw, &p, reg_type);

	ret = dc_fe_power_put(hw->dev, false);
	if (ret < 0)
		dev_err(hw->dev, "Failed to power FE OFF, ret %d\n", ret);
	ret = dc_be_power_put(hw->dev, true);
	if (ret < 0) {
		dev_err(hw->dev, "Failed to power BE OFF, ret %d\n", ret);
		return ret;
	}

	return ret_dump;
}

struct reg_dump_data {
	struct dc_hw *hw;
	enum dc_hw_reg_bank_type reg_type;
	size_t dump_size;
};

static struct reg_dump_data reg_dump_data_normal = {
	.reg_type = DC_HW_REG_BANK_ACTIVE,
};
static struct reg_dump_data reg_dump_data_shadow = {
	.reg_type = DC_HW_REG_BANK_SHADOW,
};

static int reg_dump_show(struct seq_file *s, void *data)
{
	struct reg_dump_data *reg_dump_data = s->private;

	return vs_dc_reg_dump(s, reg_dump_data->hw, reg_dump_data->reg_type);
}

static int hw_dump_show(struct seq_file *s, void *data)
{
	const struct dc_hw *hw = s->private;
	u8 i;

	for (i = 0; i < DC_DISPLAY_NUM; i++) {
		const struct dc_hw_display *display = &hw->display[i];

		seq_printf(s, "display-%d\n", i);
		display->func.print_state(s, display, 1);
	}
	for (i = 0; i < DC_PLANE_NUM; i++) {
		const struct dc_hw_plane *plane = &hw->plane[i];

		seq_printf(s, "plane-%d\n", i);
		plane->func.print_state(s, plane, 1);
	}
	for (i = 0; i < DC_WB_NUM; i++) {
		const struct dc_hw_wb *wb = &hw->wb[i];

		seq_printf(s, "wb-%d\n", i);
		wb->func.print_state(s, wb, 1);
	}
	return 0;
}

static int reg_dump_open(struct inode *inode, struct file *file)
{
	struct reg_dump_data *data = inode->i_private;

	/*
	 * each line (containing up to 16 bytes) will print up to 46 chars,
	 * plus an additional line for the header
	 */
	data->dump_size = ((data->hw->reg_dump_size - 1) / 16 + 2) * 46;

	return single_open_size(file, reg_dump_show, data, data->dump_size);
}

static int hw_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, hw_dump_show, inode->i_private);
}

static const struct file_operations reg_dump_fops = {
	.owner = THIS_MODULE,
	.open = reg_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations hw_dump_fops = {
	.owner = THIS_MODULE,
	.open = hw_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t dc_coredump_write(struct file *file, const char __user *user_buf, size_t len,
				 loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct dc_hw *hw = m->private;
	struct vs_dc *dc = to_vs_dc(hw);
	bool coredump;
	int ret;

	if (!dc->disp_sscd)
		return -EPERM;

	ret = kstrtobool_from_user(user_buf, len, &coredump);
	if (ret)
		return ret;

	if (coredump) {
		if (!is_coredump_enabled())
			dev_err(hw->dev,
				"Manual coredump requested, but coredumping not enabled\n");
		else if (!coredump_source_enabled(SSCD_SRC_MANUAL))
			dev_err(hw->dev,
				"Manual coredump requested, but not a permitted coredump source\n");
		else
			vs_dc_coredump(dc, "Manual core dump");
	}

	return len;
}

static int dc_coredump_show(struct seq_file *s, void *data)
{
	return -EPERM;
}

static int dc_coredump_open(struct inode *inode, struct file *file)
{
	return single_open(file, dc_coredump_show, inode->i_private);
}

static const struct file_operations dc_coredump_fops = {
	.owner = THIS_MODULE,
	.open = dc_coredump_open,
	.write = dc_coredump_write,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int dc_init_debugfs(struct vs_dc *dc)
{
	struct device *dev = dc->hw.dev;
	struct dentry *dentry_dpu_dump;

	dc->debugfs = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(dc->debugfs)) {
		dev_err(dev, "could not create debugfs root folder\n");
		return PTR_ERR(dc->debugfs);
	}

	if (dc->path) {
		debugfs_create_u32("min-rd-avg-bw", 0644, dc->debugfs,
				   &dc->min_qos_config.rd_avg_bw_mbps);
		debugfs_create_u32("min-rd-peak-bw", 0644, dc->debugfs,
				   &dc->min_qos_config.rd_peak_bw_mbps);
		debugfs_create_u32("min-rd-rt-bw", 0644, dc->debugfs,
				   &dc->min_qos_config.rd_rt_bw_mbps);
		debugfs_create_u32("min-wr-avg-bw", 0644, dc->debugfs,
				   &dc->min_qos_config.wr_avg_bw_mbps);
		debugfs_create_u32("min-wr-peak-bw", 0644, dc->debugfs,
				   &dc->min_qos_config.wr_peak_bw_mbps);
		debugfs_create_u32("min-wr-rt-bw", 0644, dc->debugfs,
				   &dc->min_qos_config.wr_rt_bw_mbps);
	}

	debugfs_create_u32("min-core-clk", 0644, dc->debugfs, &dc->min_qos_config.core_clk);

	dentry_dpu_dump = debugfs_create_dir("dpu_dump", dc->debugfs);
	if (IS_ERR(dentry_dpu_dump)) {
		dev_warn(dev, "could not create debugfs dpu_dump folder\n");
	} else {
		reg_dump_data_normal.hw = &dc->hw;
		reg_dump_data_shadow.hw = &dc->hw;
		debugfs_create_file("reg_dump", 0444, dentry_dpu_dump, &reg_dump_data_normal,
				    &reg_dump_fops);
		debugfs_create_file("reg_dump_sh", 0444, dentry_dpu_dump, &reg_dump_data_shadow,
				    &reg_dump_fops);
		debugfs_create_u32("reg_dump_offset", 0664, dentry_dpu_dump,
				   &dc->hw.reg_dump_offset);
		debugfs_create_u32("reg_dump_size", 0664, dentry_dpu_dump, &dc->hw.reg_dump_size);
	}

	if (is_coredump_enabled())
		debugfs_create_file("coredump_trigger", 0444, dc->debugfs, &dc->hw,
				    &dc_coredump_fops);

	debugfs_create_u32("hw_reg_dump_options", 0644, dc->debugfs, &dc->hw_reg_dump_options);

	debugfs_create_file("state", 0444, dc->debugfs, &dc->hw, &hw_dump_fops);

	return 0;
}

void dc_deinit_debugfs(struct vs_dc *dc)
{
	debugfs_remove_recursive(dc->debugfs);
}
#else
int dc_init_debugfs(struct vs_dc *dc)
{
	return 0;
}
void dc_deinit_debugfs(struct vs_dc *dc)
{
	return;
}
#endif
