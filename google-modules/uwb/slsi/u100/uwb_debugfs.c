// SPDX-License-Identifier: GPL-2.0
/**
 * @copyright Copyright (c) 2025 Samsung Electronics Co., Ltd
 *
 */

#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "include/uwb_debugfs.h"

#define MAX_GPIO_OPT_LEN 126

static int gpio_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t gpio_write(struct file *file, const char __user *user, size_t count, loff_t *ppos)
{
	char buf[MAX_GPIO_OPT_LEN + 1];
	ssize_t ret = count;
	struct u100_ctx *u100_ctx = (struct u100_ctx *)file->private_data;

	mutex_lock(&u100_ctx->ioctl_mutex);
	if (atomic_read(&u100_ctx->flashing)) {
		UWB_ERR("GPIO operation failed for device is flashing FW\n");
		ret = -EBUSY;
		goto end;
	}

	if (count > MAX_GPIO_OPT_LEN) {
		UWB_ERR("Invalid user buffer count %zu\n", count);
		ret = -EINVAL;
		goto end;
	}

	if (strncpy_from_user(buf, user, count) <= 0) {
		ret = -EFAULT;
		goto end;
	}

	strim(buf);
	if (!strcmp(buf, "poweron")) {
		uwbs_power_on(u100_ctx);
	} else if (!strcmp(buf, "poweroff")) {
		uwbs_power_off(u100_ctx);
	} else if (!strcmp(buf, "reset")) {
		uwbs_reset(u100_ctx);
	} else if (!strcmp(buf, "syncreset")) {
		uwbs_sync_reset(u100_ctx);
	} else if (!strcmp(buf, "ldswon")) {
		pin_ldsw_high(u100_ctx);
	} else if (!strcmp(buf, "ldswoff")) {
		pin_ldsw_low(u100_ctx);
	} else if (!strcmp(buf, "ldswreset")) {
		uwbs_ldsw_reset(u100_ctx);
	} else {
		UWB_ERR("Invalid GPIO opt count %zu buffer %s\n", count, buf);
		ret = -EINVAL;
	}

end:
	mutex_unlock(&u100_ctx->ioctl_mutex);
	return ret;
}

static const struct file_operations gpio_fops = {
	.owner = THIS_MODULE,
	.open = gpio_open,
	.write = gpio_write,
};

static int irq_count_show(struct seq_file *s, void *unused)
{
	struct device *dev = s->private;
	struct u100_ctx *u100_ctx = dev_get_drvdata(dev);

	seq_printf(s, "%u\n", get_irq_count(u100_ctx));
	return 0;
}

static void debugfs_create_files(struct u100_ctx *u100_ctx)
{
	/* File permission: S_IWUSR. The returned error should be ignored. */
	debugfs_create_file("gpio", 0200, u100_ctx->debugfs, u100_ctx, &gpio_fops);

	debugfs_create_devm_seqfile(&u100_ctx->spi->dev, "irq_count",
				    u100_ctx->debugfs, irq_count_show);
}

void uwb_debugfs_init(struct u100_ctx *u100_ctx)
{
	/* Most callers should _ignore_ the errors returned by the calling. */
	u100_ctx->debugfs = debugfs_create_dir("u100", NULL);
	debugfs_create_files(u100_ctx);
}

void uwb_debugfs_deinit(struct u100_ctx *u100_ctx)
{
	/* Nothing will be done if parameter is NULL or an error value. */
	debugfs_remove_recursive(u100_ctx->debugfs);
	u100_ctx->debugfs = NULL;
}
