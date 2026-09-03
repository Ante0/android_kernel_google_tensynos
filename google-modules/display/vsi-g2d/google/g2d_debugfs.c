// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>

#include "g2d_debugfs.h"
#include "g2d_drv.h"
#include "g2d_sc.h"
#include "g2d_recovery.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int reg_dump_show(struct seq_file *s, void *data)
{
	struct g2d_sc *sc = s->private;
	struct sc_hw *hw = &sc->hw;
	int ret;

	ret = pm_runtime_get_if_in_use(hw->dev);
	if (ret <= 0) {
		if (ret < 0)
			dev_warn(hw->dev, "%s: Skipping dump. PM runtime get err: %d", __func__,
				 ret);
		else
			dev_dbg(hw->dev, "%s: G2D off, skipping dump.", __func__);

		return ret;
	}

	ret = sc_hw_reg_dump(s, hw);
	pm_runtime_put_sync(hw->dev);

	return ret;
}

DEFINE_SHOW_ATTRIBUTE(reg_dump);

static int allow_reset_show(struct seq_file *m, void *v)
{
	struct g2d_sc *sc = m->private;

	seq_printf(m, "%u\n", sc->allow_reset);
	return 0;
}

static ssize_t allow_reset_write(struct file *file, const char __user *ubuf, size_t count,
				 loff_t *ppos)
{
	struct g2d_sc *sc = file->f_inode->i_private;
	u8 new_val;
	int ret;

	ret = kstrtou8_from_user(ubuf, count, 0, &new_val);

	if (ret)
		return ret;
	sc->allow_reset = !!(new_val);

	if (sc->allow_reset)
		g2d_reset_trigger(sc);

	return count;
}

static int allow_reset_open(struct inode *inode, struct file *file)
{
	return single_open(file, allow_reset_show, inode->i_private);
}

static const struct file_operations allow_reset_fops = {
	.owner = THIS_MODULE,
	.open = allow_reset_open,
	.read = seq_read,
	.write = allow_reset_write,
	.llseek = seq_lseek,
	.release = single_release,
};

int g2d_debugfs_init(struct device *dev)
{
	struct drm_device *drm;
	struct g2d_device *g2d_device;
	struct g2d_sc *sc;

	drm = dev_get_drvdata(dev);
	g2d_device = to_g2d_device(drm);
	sc = g2d_device->sc;

	g2d_device->debugfs = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR(g2d_device->debugfs)) {
		dev_err(dev, "could not create debugfs root folder\n");
		return PTR_ERR(g2d_device->debugfs);
	}

	debugfs_create_file("reg_dump", 0444, g2d_device->debugfs, sc, &reg_dump_fops);

	if (!IS_ERR_OR_NULL(g2d_device->core_devfreq))
		debugfs_create_u32("min-core-clk", 0644, g2d_device->debugfs,
				   &g2d_device->min_qos_config.core_clk);

	if (!IS_ERR_OR_NULL(g2d_device->icc_path)) {
		debugfs_create_u32("min-rd-avg-bw", 0644, g2d_device->debugfs,
				   &g2d_device->min_qos_config.rd_avg_bw_mbps);
		debugfs_create_u32("min-rd-peak-bw", 0644, g2d_device->debugfs,
				   &g2d_device->min_qos_config.rd_peak_bw_mbps);
		debugfs_create_u32("min-wr-avg-bw", 0644, g2d_device->debugfs,
				   &g2d_device->min_qos_config.wr_avg_bw_mbps);
		debugfs_create_u32("min-wr-peak-bw", 0644, g2d_device->debugfs,
				   &g2d_device->min_qos_config.wr_peak_bw_mbps);
	}

	debugfs_create_file("allow_reset", 0644, g2d_device->debugfs, sc, &allow_reset_fops);
	return 0;
}

void g2d_debugfs_deinit(struct device *dev)
{
	struct drm_device *drm;
	struct g2d_device *g2d_device;

	drm = dev_get_drvdata(dev);
	g2d_device = to_g2d_device(drm);

	debugfs_remove_recursive(g2d_device->debugfs);
}

#endif /* CONFIG_DEBUG_FS */
