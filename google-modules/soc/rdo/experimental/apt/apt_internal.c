// SPDX-License-Identifier: GPL-2.0-only
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/tick.h>
#include <linux/types.h>

#include "apt_clocksource.h"
#include "apt_gt_comp.h"
#include "apt_internal.h"
#include "apt_lt.h"

static struct dentry *google_apt_debugfs_root;

static int debugfs_tick_broadcast_write(void *data, u64 val)
{
	if (val) {
		tick_broadcast_enable();
		tick_broadcast_enter();
	} else {
		tick_broadcast_exit();
		tick_broadcast_disable();
	}
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(debugfs_tick_broadcast_fops, NULL,
			 debugfs_tick_broadcast_write, "%lld\n");

int google_apt_debugfs_init(void)
{
	if (!debugfs_initialized())
		return -ENODEV;

	google_apt_debugfs_root = debugfs_create_dir("google-apt", NULL);
	if (IS_ERR(google_apt_debugfs_root))
		return PTR_ERR(google_apt_debugfs_root);

	debugfs_create_file_unsafe("tick_broadcast", 0200,
				   google_apt_debugfs_root, NULL,
				   &debugfs_tick_broadcast_fops);
	return 0;
}

static int apt_debugfs_stat(struct seq_file *file, void *data)
{
	struct device *dev = file->private;
	struct google_apt *apt = dev_get_drvdata(dev);
	int i;

	seq_puts(file, "=== APT stat ===");
	seq_printf(file, "device name: %s\n", dev_name(dev));
	seq_printf(file, "tclk frequency: %lu\n", clk_get_rate(apt->tclk));
	seq_printf(file, "prescaler: %d\n", apt->prescaler);

	for (i = 0; i < GOOGLE_APT_NUM_GT_COMP; i++)
		google_apt_gt_comp_stat(&apt->gt_comp[i], file);

	for (i = 0; i < GOOGLE_APT_NUM_LT; i++)
		google_apt_lt_stat(&apt->lt[i], file);

	return 0;
}

static void apt_init_debugfs(struct google_apt *apt)
{
	struct device *dev = apt->dev;

	apt->debugfs =
		debugfs_create_dir(dev_name(dev), google_apt_debugfs_root);
	if (IS_ERR(apt->debugfs)) {
		dev_warn(dev, "failed to create debugfs");
		return;
	}

	debugfs_create_devm_seqfile(dev, "stat", apt->debugfs,
				    apt_debugfs_stat);
}

int google_apt_init(struct google_apt *apt)
{
	struct device *dev = apt->dev;
	u32 clk_config = 0;
	int err;
	int i;

	apt_init_debugfs(apt);

	apt->tclk = devm_clk_get(dev, NULL);
	if (IS_ERR(apt->tclk)) {
		dev_err(dev, "failed to get tclk.\n");
		return PTR_ERR(apt->tclk);
	}

	// Disable global timer before initialization.
	google_apt_writel(apt, GOOGLE_APT_GT_TIM_CONFIG_VALUE_DISABLE,
			  GOOGLE_APT_GT_TIM_CONFIG);

	// Reset 64-bit global timer counter to 0, so that the
	// GT_TIM_LOWER and  GT_TIM_UPPER is loaded when global timer is
	// enabled from disable state.
	google_apt_writel(apt, 0x0, GOOGLE_APT_GT_TIM_LOAD_LOWER);
	google_apt_writel(apt, 0x0, GOOGLE_APT_GT_TIM_LOAD_UPPER);

	// 1 is also valid for apt->prescaler, but we don't have to enable the
	// prescaler.
	if (apt->prescaler > 1 && apt->prescaler <= 65536) {
		clk_config =
			FIELD_PREP(GOOGLE_APT_CONFIG_CLK_FIELD_PRESCALER,
				   apt->prescaler - 1) |
			FIELD_PREP(GOOGLE_APT_CONFIG_CLK_FIELD_PRESCALER_EN,
				   GOOGLE_APT_CONFIG_CLK_PRESCALER_EN_TRUE);
	} else if (apt->prescaler != 1) {
		dev_warn(dev, "ignore invalid prescaler value %u.\n",
			 apt->prescaler);
	}
	google_apt_writel(apt, clk_config, GOOGLE_APT_CONFIG_CLK);

	// TODO request non gt_comp or lt IRQ, this should be added in later
	// version of APT.

	// Enabling global timer is required before global timer comparator
	// initialize.
	google_apt_writel(apt, GOOGLE_APT_GT_TIM_CONFIG_VALUE_ENABLE,
			  GOOGLE_APT_GT_TIM_CONFIG);

	err = google_apt_clocksource_init(apt);
	if (err) {
		dev_err(dev, "failed to initialize clocksource: %d.\n", err);
		return err;
	}

	for (i = 0; i < GOOGLE_APT_NUM_GT_COMP; i++) {
		apt->gt_comp[i].idx = i;
		err = google_apt_gt_comp_init(&apt->gt_comp[i]);
		if (err) {
			dev_err(dev,
				"failed to initialize global timer comparator %d: %d.\n",
				i, err);
			return err;
		}
	}

	for (i = 0; i < GOOGLE_APT_NUM_LT; i++) {
		apt->lt[i].idx = i;
		err = google_apt_lt_init(&apt->lt[i]);
		if (err) {
			dev_err(dev,
				"failed to initialize local timer %d: %d.\n", i,
				err);
			return err;
		}
	}

	return 0;
}

int google_apt_exit(struct google_apt *apt)
{
	struct device *dev = apt->dev;

	// Cannot unregister clock event device. Do all of the other cleanups.
	dev_warn(dev, "APT should not be removed.\n");

	google_apt_writel(apt, GOOGLE_APT_GT_TIM_CONFIG_VALUE_DISABLE,
			  GOOGLE_APT_GT_TIM_CONFIG);

	google_apt_clocksource_exit(apt);
	return -EBUSY;
}

unsigned long google_apt_get_prescaled_tclk_rate(const struct google_apt *apt)
{
	return clk_get_rate(apt->tclk) / apt->prescaler;
}
