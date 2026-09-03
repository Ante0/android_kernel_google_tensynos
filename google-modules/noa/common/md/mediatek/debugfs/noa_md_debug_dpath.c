// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD Data Path Switching Debugfs Implementation
 *
 * Copyright 2025 Google LLC.
 */
#include <linux/debugfs.h>
#include <linux/kstrtox.h>
#include <linux/uaccess.h>
#include <linux/string.h>

#include "noa_md.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_trace.h"
#include "noa_md_debug_dpath.h"

static ssize_t noa_md_debug_dpath_switch_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	enum dpa_data_path target_path;
	char buf[16];

	if (copy_from_user(buf, user_buf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	buf[min_t(size_t, sizeof(buf) - 1, count)] = '\0';
	strim(buf);

	if (sysfs_streq(buf, "direct")) {
		target_path = NOA_DATA_PATH_DIRECT;
	} else if (sysfs_streq(buf, "offload")) {
		target_path = NOA_DATA_PATH_OFFLOAD;
	} else {
		NOA_MD_ERROR("Invalid path. Use 'direct' or 'offload'.");
		return -EINVAL;
	}

	/**
	 * TODO: b/434646935 - Implement and call
	 * noa_md_dpath_ctrl_trigger_switch(target_path);
	 */
	NOA_MD_INFO("Debugfs: Triggering switch to %s(%d)", buf, target_path);
	return count;
}

static const struct file_operations dpath_switch_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = noa_md_debug_dpath_switch_write,
	.llseek = noop_llseek,
};

static ssize_t noa_md_debug_dpath_force_failure_write(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct noa_md_dev *p_md_dev = file_inode(file)->i_private;
	struct noa_md_dpath_ctrl *ctrl = p_md_dev ? p_md_dev->dpath_ctrl : NULL;
	enum noa_dpath_failure_injection fail_type = NOA_DPATH_FAIL_NONE;
	char buf[16];

	CHECK_PTR_OR_RETURN_ERR(ctrl, -EIO);

	if (copy_from_user(buf, user_buf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	buf[min_t(size_t, sizeof(buf) - 1, count)] = '\0';
	strim(buf);

	if (sysfs_streq(buf, "none")) {
		fail_type = NOA_DPATH_FAIL_NONE;
	} else if (sysfs_streq(buf, "TX")) {
		fail_type = NOA_DPATH_FAIL_CLIENT_TX;
	} else if (sysfs_streq(buf, "RX")) {
		fail_type = NOA_DPATH_FAIL_CLIENT_RX;
	} else if (sysfs_streq(buf, "DOORBELL")) {
		fail_type = NOA_DPATH_FAIL_DOORBELL;
	} else {
		NOA_MD_ERROR(
			"Invalid failure type. Use 'none', 'TX', 'RX', or 'DOORBELL'.");
		return -EINVAL;
	}

	mutex_lock(&ctrl->lock);
	ctrl->failure_injection = fail_type;
	mutex_unlock(&ctrl->lock);

	NOA_MD_INFO("Next switch will inject failure: %s", buf);
	return count;
}

static const struct file_operations force_failure_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = noa_md_debug_dpath_force_failure_write,
	.llseek = noop_llseek,
};

static ssize_t noa_md_debug_dpath_client_timeout_show(struct file *file,
	char __user *user_buf, size_t count, loff_t *ppos)
{
	struct noa_md_dpath_ctrl *ctrl = file_inode(file)->i_private;
	char buf[32];
	size_t len;

	CHECK_PTR_OR_RETURN_ERR(ctrl, -EIO);

	mutex_lock(&ctrl->lock);
	len = scnprintf(buf, sizeof(buf), "%u\n", ctrl->timeout_ms);
	mutex_unlock(&ctrl->lock);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t noa_md_debug_dpath_client_timeout_store(struct file *file,
	const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct noa_md_dpath_ctrl *ctrl = file_inode(file)->i_private;
	u32 timeout;
	int ret;

	CHECK_PTR_OR_RETURN_ERR(ctrl, -EIO);

	ret = kstrtouint_from_user(user_buf, count, 0, &timeout);
	if (ret)
		return ret;

	mutex_lock(&ctrl->lock);
	ctrl->timeout_ms = timeout;
	mutex_unlock(&ctrl->lock);

	NOA_MD_INFO("Client timeout set to %u ms", timeout);
	return count;
}

static const struct file_operations client_timeout_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = noa_md_debug_dpath_client_timeout_show,
	.write = noa_md_debug_dpath_client_timeout_store,
	.llseek = default_llseek,
};

int noa_md_debug_dpath_init(struct dentry *noa_root,
	struct noa_md_dev *p_md_dev)
{
	/* Local variable, no longer file-static global. */
	struct dentry *dpath_dir;

	dpath_dir = debugfs_create_dir("dpath", noa_root);

	CHECK_PTR_OR_RETURN_ERR(dpath_dir, -ENOMEM);

	if (!debugfs_create_file("dpath_switch", 0220, dpath_dir,
				 p_md_dev, &dpath_switch_fops))
		goto err;

	if (!debugfs_create_file("force_failure", 0220, dpath_dir,
				 p_md_dev, &force_failure_fops))
		goto err;

	if (!debugfs_create_file("client_timeout_ms", 0644, dpath_dir,
				 p_md_dev->dpath_ctrl, &client_timeout_fops))
		goto err;

	return 0;

err:
	/*
	 * Cleanup is robust. If any file creation fails, we recursively
	 * remove the directory we just created and everything inside it.
	 */
	debugfs_remove_recursive(dpath_dir);
	return -ENOMEM;
}

void noa_md_debug_dpath_exit(void)
{
	/*
	 * No action needed here. The parent debugfs module (noa_md_debug.c)
	 * calls debugfs_remove_recursive() on the root directory ("noa_md"),
	 * which cleans up the "dpath" subdirectory and all files we created.
	 */
}
