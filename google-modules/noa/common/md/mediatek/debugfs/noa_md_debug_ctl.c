/* common/md/mediatek/debug/noa_md_debug_ctl.c */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD Feature Control Debugfs Implementation
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>   /* For seq_file, single_open */
#include <linux/uaccess.h>    /* For copy_from_user */
#include <linux/kstrtox.h>    /* For kstrtobool */

#include "noa_md.h"
#include "noa_md_data_path_ctrl.h"  /* For noa_md_dpath_ctrl_get_state */
#include "noa_md_debug_ctl.h"
#include "noa_md_pcie.h"  /* For noa_md_pcie_set_apc_msi_ctrls_enabled */
#include "noa_md_trace.h"  /* For NOA_MD_INFO/ERROR macros */

/* data_path_enabled */
/**
 * noa_md_debug_ctl_data_path_enabled_show() - Shows the current state of the
 * NOA feature.
 * @m: Seq_file pointer. The private member m->private holds our device struct.
 * @v: Unused.
 *
 * Called on 'cat' of the "enabled" debugfs node.
 *
 * Return: Always returns 0.
 */
static int noa_md_debug_ctl_data_path_enabled_show(struct seq_file *m, void *v)
{
	struct noa_md_dev *dev = m->private;

	if (!dev) {
		seq_puts(m, "Error: NOA device not available\n");
		return 0;
	}

	seq_printf(m, "%d\n", dev->feature_ctrl.enabled);
	return 0;
}

/**
 * noa_md_debug_ctl_data_path_enabled_store() - Sets the state of the NOA
 * data path from a user command.
 * @file: File pointer, used to access inode's private data.
 * @user_buf: User buffer with the new value (e.g., "1", "0", "true").
 * @count: Number of bytes in @user_buf.
 * @ppos: Position offset.
 *
 * Called on 'echo' to the "data_path_enabled" debugfs node.
 *
 * Example:
 * # echo 1 > /d/noa_md/control/data_path_enabled  (Enable NOA data path)
 * # echo 0 > /d/noa_md/control/data_path_enabled  (Disable NOA data path)
 *
 * Return: Number of bytes written on success, or a negative error code.
 */
static ssize_t noa_md_debug_ctl_data_path_enabled_store(
	struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct noa_md_dev *dev = file->f_inode->i_private;
	bool new_value;
	int ret;

	if (!dev) {
		NOA_MD_ERROR("Cannot set 'data_path_enabled', device not available");
		return -EIO;
	}

	ret = kstrtobool_from_user(user_buf, count, &new_value);
	if (ret < 0) {
		NOA_MD_ERROR("Invalid input. Use '1' or '0'.");
		return ret;
	}

	dev->feature_ctrl.enabled = new_value;
	NOA_MD_INFO(
		"'data_path_enabled' set to: %s", new_value ? "true" : "false");

	return count;
}

/**
 * noa_md_debug_ctl_data_path_enabled_open() - Handles opening the 'enabled'
 * debugfs file.
 * @inode: Inode structure, contains the pointer to our device struct.
 * @file:  File structure.
 *
 * Return: %0 on success.
 */
static int noa_md_debug_ctl_data_path_enabled_open(struct inode *inode,
	struct file *file)
{
	return single_open(file, noa_md_debug_ctl_data_path_enabled_show,
		inode->i_private);
}

/* File operations for the "enabled" debugfs node. */
static const struct file_operations noa_md_dbg_ctl_data_path_enabled_fops = {
	.owner = THIS_MODULE,
	.open = noa_md_debug_ctl_data_path_enabled_open,
	.read = seq_read,
	.write = noa_md_debug_ctl_data_path_enabled_store,
	.llseek = seq_lseek,
	.release = single_release,
};

/* dynamic_switch_disabled */
/**
 * noa_md_debug_ctl_dynamic_switch_disabled_show() - Shows if dynamic path
 * switching is disabled.
 * @m: Seq_file pointer. The private member m->private holds our device struct.
 * @v: Unused.
 *
 * Called on 'cat' of the "dynamic_switch_disabled" debugfs node. It prints
 * '1' if dynamic switching is disabled, '0' otherwise.
 *
 * Return: Always returns 0.
 */
static int noa_md_debug_ctl_dynamic_switch_disabled_show(
	struct seq_file *m, void *v)
{
	struct noa_md_dev *dev = m->private;
	bool is_disabled;

	if (!dev) {
		seq_puts(m, "Error: NOA device not available\n");
		return 0;
	}

	mutex_lock(&dev->debug_ctl.lock);
	is_disabled = dev->debug_ctl.dynamic_switch_disabled;
	mutex_unlock(&dev->debug_ctl.lock);

	seq_printf(m, "%d\n", is_disabled);
	return 0;
}

/**
 * noa_md_debug_ctl_dynamic_switch_disabled_store() - Enables or disables
 * dynamic path switching.
 * @file: File pointer, used to access inode's private data.
 * @user_buf: User buffer with the new value ("1" to disable, "0" to enable).
 * @count: Number of bytes in @user_buf.
 * @ppos: Position offset.
 *
 * Called on 'echo' to the "dynamic_switch_disabled" debugfs node.
 * This function is state-aware and will configure PCIe MSI controls
 * based on the current data path state when dynamic switching is re-enabled.
 *
 * Example:
 * # echo 1 > /d/noa_md/control/dynamic_switch_disabled (Disable)
 * # echo 0 > /d/noa_md/control/dynamic_switch_disabled (Enable)
 *
 * Return: Number of bytes written on success, or a negative error code.
 */
static ssize_t noa_md_debug_ctl_dynamic_switch_disabled_store(
	struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct noa_md_dev *dev = file->f_inode->i_private;
	bool new_value;
	int ret;

	if (!dev) {
		NOA_MD_ERROR(
			"Cannot set dynamic_switch_disabled, device not available");
		return -EIO;
	}

	mutex_lock(&dev->debug_ctl.lock);
	ret = kstrtobool_from_user(user_buf, count, &new_value);
	if (ret < 0) {
		mutex_unlock(&dev->debug_ctl.lock);
		NOA_MD_ERROR("Invalid input. Use '1' (disable) or '0' (enable).");
		return ret;
	}

	dev->debug_ctl.dynamic_switch_disabled = new_value;
	mutex_unlock(&dev->debug_ctl.lock);

	if (noa_md_debug_ctl_is_dynamic_switch_enabled(dev)) {
		enum dpath_switch_state current_state =
			noa_md_dpath_ctrl_get_state(dev->dpath_ctrl);
		NOA_MD_INFO("Dynamic switch enabled. Current data path state is '%s'.",
			noa_md_dpath_ctrl_state_to_str(current_state));

		switch (current_state) {
		case NOA_MD_DPATH_STATE_IDLE_DIRECT:
			ret = noa_md_pcie_set_apc_msi_ctrls_enabled(true);
			NOA_MD_INFO("enabling APC MSI ctrls (ret=%d)", ret);
			break;
		case NOA_MD_DPATH_STATE_IDLE_OFFLOAD:
			ret = noa_md_pcie_set_apc_msi_ctrls_enabled(false);
			NOA_MD_INFO("disabling APC MSI ctrls (ret=%d)", ret);
			break;
		default:
			NOA_MD_ERROR("Cannot change MSI settings while data path is in a "
				"transient state (%s)",
				noa_md_dpath_ctrl_state_to_str(current_state));
			break;
		}
	} else {
		ret = noa_md_pcie_set_apc_msi_ctrls_enabled(false);
		NOA_MD_INFO(
			"Dynamic switch disabled by default, "
			"data path defaults to the NOA, "
			"disabling APC MSI ctrls (ret=%d)", ret);
		if (ret) {
			NOA_MD_ERROR("Failed to disable APC MSI ctrls during init");
		}
	}

	NOA_MD_INFO("Dynamic switch handling is now %s",
		new_value ? "disable" : "enable");

	return count;
}

static int dynamic_switch_disabled_open(struct inode *inode, struct file *file)
{
	return single_open(
		file, noa_md_debug_ctl_dynamic_switch_disabled_show, inode->i_private);
}

static const struct file_operations
noa_md_debug_ctl_dynamic_switch_disabled_fops = {
	.owner = THIS_MODULE,
	.open = dynamic_switch_disabled_open,
	.read = seq_read,
	.write = noa_md_debug_ctl_dynamic_switch_disabled_store,
	.llseek = seq_lseek,
	.release = single_release,
};

bool noa_md_debug_ctl_is_dynamic_switch_enabled(
	struct noa_md_dev *p_md_dev)
{
	bool is_enabled;

	if (unlikely(!p_md_dev))
		return false;

	mutex_lock(&p_md_dev->debug_ctl.lock);
	is_enabled = !p_md_dev->debug_ctl.dynamic_switch_disabled;
	mutex_unlock(&p_md_dev->debug_ctl.lock);

	return is_enabled;
}

int noa_md_debug_ctl_init(struct dentry *noa_root, struct noa_md_dev *p_md_dev)
{
	struct dentry *ctl_dir;

	CHECK_PTR_OR_RETURN_ERR(noa_root, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	ctl_dir = debugfs_create_dir("control", noa_root);
	if (!ctl_dir) {
		NOA_MD_ERROR("Failed to create 'control' dir");
		return -ENOMEM;
	}

	/* Create "data_path_enabled" and pass `p_md_dev` as its private data. */
	if (!debugfs_create_file(
		"data_path_enabled", 0644, ctl_dir, p_md_dev,
		&noa_md_dbg_ctl_data_path_enabled_fops)) {
		NOA_MD_ERROR("Failed to create 'enabled' file");
		debugfs_remove_recursive(ctl_dir);
		return -ENOMEM;
	}

	if (!debugfs_create_file(
		"dynamic_switch_disabled", 0644, ctl_dir, p_md_dev,
		&noa_md_debug_ctl_dynamic_switch_disabled_fops)) {
		NOA_MD_ERROR("Failed to create 'dynamic_switch_disabled' file");
		debugfs_remove_recursive(ctl_dir);
		return -ENOMEM;
	}

	mutex_init(&p_md_dev->debug_ctl.lock);

#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_ENABLE_NOA_PATH)
	NOA_MD_INFO("Set enabled to true for debugging purposes.");
	p_md_dev->feature_ctrl.enabled = true;
#endif

#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
	NOA_MD_INFO("Disabling dynamic switch by default.");
	p_md_dev->debug_ctl.dynamic_switch_disabled = true;
#else
	p_md_dev->debug_ctl.dynamic_switch_disabled = false;
#endif

	return 0;
}

void noa_md_debug_ctl_exit(void)
{
	/*
	 * No action needed here.
	 * The parent debugfs directory removal in noa_md_debug_exit() will
	 * recursively clean up everything created in our init function.
	 */
}
