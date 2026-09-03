/* common/md/mediatek/debug/noa_md_debug_t900_cmd.c */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 *
 * NOA MD T900 command debugfs implementation.
 */

#include <linux/debugfs.h>
#include <linux/kstrtox.h>
#include <linux/uaccess.h>

#include "noa_md.h"
#include "noa_md_debug_t900_cmd.h"
#include "noa_md_dpmaif.h"  // For MDEV_TO_DCB
#include "noa_md_trace.h"
#include "noa_md_wrapper.h"  // For noa_md_wpr_t900_set_cmd_enabled
#include "noa_md_wrapper_dpmaif.h"
#include "t900/noa_md_mtk_priv.h"

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
#include "noa_md_wrapper_t900.h"
#endif

#define MAX_CMD_LEN 64

/**
 * t900_cmd_enable_show() - Show the status of all command enable status
 * @m: Seq_file pointer.
 *
 * This function prints the enabled status for each T900 feature command.
 * '1' means enabled for normal driver calls, '0' means disabled.
 *
 * Return: Always 0.
 */
static int t900_cmd_enable_show(struct seq_file *m, void *v)
{
	int i;

	seq_puts(m, "T900 Command Driver-Call Status (1 = Enabled):\n");

	for (i = 0; i < NOA_MD_WPR_T900_CMD_MAX; i++) {
		seq_printf(m, "  CMD[%d]: %d\n", i, noa_md_wpr_t900_is_cmd_enabled(i));
	}

	return 0;
}

/**
 * t900_cmd_enable_write() - Set the enabled state for a specific command.
 * @file: File pointer.
 * @user_buf: User buffer with the command id and state.
 * @count: Number of bytes in user_buf.
 * @ppos: Position offset.
 *
 * Parses two integer arguments from userspace: "<cmd_id> <state>"
 * state '1' enables the command for normal driver calls.
 * state '0' disables it, making it accessible only via debugfs.
 *
 * Example:
 * # Disable command 0 for normal driver calls
 * echo "0 0" > /sys/kernel/debug/noa_md/t900_cmd_ctrl
 *
 * # Enable command 0 for normal driver calls
 * echo "0 1" > /sys/kernel/debug/noa_md/t900_cmd_ctrl
 *
 * Return: Number of bytes written on success, or a negative error code.
 */
static ssize_t t900_cmd_enable_write(
	struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[MAX_CMD_LEN];
	char *p;
	char *token;
	int cmd_id, state, ret;

	if (copy_from_user(buf, user_buf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	buf[min_t(size_t, sizeof(buf) - 1, count)] = '\0';
	p = strim(buf);

	token = strsep(&p, " ");
	if (!token) {
		NOA_MD_ERROR("Invalid format. Use: echo \"<cmd_id> <0|1>\"");
		return -EINVAL;
	}

	ret = kstrtoint(token, 10, &cmd_id);
	if (ret < 0)
		return ret;

	if (!p || *p == '\0') {
		NOA_MD_ERROR("Missing second argument for state (0 or 1)");
		return -EINVAL;
	}

	ret = kstrtoint(p, 10, &state);
	if (ret < 0)
		return ret;

	ret = noa_md_wpr_t900_set_cmd_enabled(cmd_id, !!state);
	if (ret < 0) {
		NOA_MD_ERROR("Failed to set enabled state for CMD[%d], ret=%d",
			cmd_id, ret);
		return ret;
	}

	NOA_MD_INFO("Set driver call for CMD[%d] to %s",
		cmd_id, state ? "ENABLED" : "DISABLED");

	return count;
}

/**
 * t900_cmd_enable_open() - Open handler for the 't900_cmd_enable' debugfs file.
 * @inode: Inode structure.
 * @file:  File structure.
 *
 * Connects the 'show' function to the file's read operation using seq_file.
 *
 * Return: 0 on success.
 */
static int t900_cmd_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, t900_cmd_enable_show, inode->i_private);
}

static const struct file_operations t900_cmd_enable_fops = {
	.owner = THIS_MODULE,
	.open = t900_cmd_enable_open,
	.read = seq_read,
	.write = t900_cmd_enable_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/**
 * @brief Handles write operations to the "t900_cmd" debugfs file.
 *
 * This function parses user input to call noa_md_wpr_t900_feature_cmd.
 * The expected format is: "<cmd_val> [data_val]"
 *
 * Example:
 * # echo 5 0 > /d/noa_md/t900_cmd   (cmd=5 for DPMAIF_STATUS_SYNC,
 * data: SYNC_DPMAIF_TO_NOA(0) or SYNC_NOA_TO_DPMAIF(1))
 * # echo 0 > /d/noa_md/t900_cmd   (cmd=0 for SW_INIT, gets dcb automatically)
 *
 * @param file File pointer.
 * @param user_buf User buffer with the command and optional data.
 * @param count Number of bytes in user_buf.
 * @param ppos Position offset.
 * @return Number of bytes written on success, or a negative error code.
 */
static ssize_t t900_cmd_write(
	struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[MAX_CMD_LEN];
	char *p, *token;
	int temp_cmd_id = 0;
	int arg2_val = 0;
	int ret;
	enum noa_md_wpr_mtk_t900_drv_cmd cmd_val;
	enum noa_md_wpr_sync_duration sync_duration;
	void *data = NULL;

	if (copy_from_user(buf, user_buf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	buf[min_t(size_t, sizeof(buf) - 1, count)] = '\0';

	p = strim(buf);

	// Parse the first argument (cmd_val)
	token = strsep(&p, " ");
	if (!token) {
		NOA_MD_ERROR("Invalid format. Use: echo \"<cmd_id> [arg2]\"");
		return -EINVAL;
	}

	ret = kstrtoint(token, 10, &temp_cmd_id);
	if (ret < 0) {
		NOA_MD_ERROR(
			"Invalid command ID format, use integer value, ret: %d", ret);
		return ret;
	}

	if (temp_cmd_id >= NOA_MD_WPR_T900_CMD_MAX) {
		NOA_MD_ERROR("Invalid command ID: %d", temp_cmd_id);
		return -EINVAL;
	}

	cmd_val = temp_cmd_id;

	// Parse the optional second argument (arg2_val)
	if (p) {
		ret = kstrtoint(p, 10, &arg2_val);
		if (ret < 0) {
			NOA_MD_ERROR(
				"Invalid second argument format. Use integer value, ret: %d",
				ret);
			return ret;
		}
	}

	// Handle data pointer based on command
	switch (cmd_val) {
	// Commands that require the DCB pointer
	case NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT:
	case NOA_MD_WPR_T900_CMD_DPMAIF_SW_EXIT:
	case NOA_MD_WPR_T900_CMD_DPMAIF_START:
	case NOA_MD_WPR_T900_CMD_DPMAIF_STOP:
	case NOA_MD_WPR_T900_CMD_DPMAIF_SW_RESET:
	{
		struct noa_md_dpmaif_ops *dpmaif_ops =
			noa_md_wpr_dpmaif_get_dpmaif_ops();
		struct mtk_md_dev *mdev = NULL;
		struct mtk_dpmaif_ctlb *dcb = NULL;

		if (dpmaif_ops && dpmaif_ops->get_mdev) {
			mdev = dpmaif_ops->get_mdev();
		}

		if (!mdev) {
			NOA_MD_ERROR("Failed to get mdev for debug command");
			return -ENODEV;
		}

		dcb = MDEV_TO_DCB(mdev);
		if (!dcb) {
			NOA_MD_ERROR("mdev->data_blk->dcb is NULL");
			return -ENODEV;
		}
		data = dcb;
		break;
	}

	// CLDMA commands are not supported yet
	case NOA_MD_WPR_T900_CMD_CLDMA_INIT:
	case NOA_MD_WPR_T900_CMD_CLDMA_EXIT:
	case NOA_MD_WPR_T900_CMD_CLDMA_DEV_INIT:
	case NOA_MD_WPR_T900_CMD_CLDMA_DEV_EXIT:
	case NOA_MD_WPR_T900_CMD_CLDMA_OPEN:
	case NOA_MD_WPR_T900_CMD_CLDMA_CLOSE:
		NOA_MD_ERROR("CLDMA command %d is not supported yet", cmd_val);
		return -EOPNOTSUPP;

	// Command that requires the user-provided second argument
	case NOA_MD_WPR_T900_CMD_DPMAIF_STATUS_SYNC:
		if (arg2_val == NOA_MD_WPR_SYNC_DPMAIF_TO_NOA ||
			arg2_val == NOA_MD_WPR_SYNC_NOA_TO_DPMAIF) {
			NOA_MD_ERROR("Set sync duration: %d", arg2_val);
			sync_duration = arg2_val;
			data = &sync_duration;
		} else {
			NOA_MD_ERROR("Invalid sync duration: %d. Use 0 or 1", arg2_val);
			return -EINVAL;
		}
		break;

	// Commands that require the MTK Data Block pointer
	case NOA_MD_WPR_T900_CMD_WWAN_INIT:
	case NOA_MD_WPR_T900_CMD_WWAN_SETUP:
	case NOA_MD_WPR_T900_CMD_WWAN_OPEN:
	case NOA_MD_WPR_T900_CMD_WWAN_STOP:
	case NOA_MD_WPR_T900_CMD_WWAN_EXIT:
	case NOA_MD_WPR_T900_CMD_NETDEV_UPDATE:
		// TODO: Pull the data_blk from mdev
		data = NULL;
		break;

	default:
		NOA_MD_ERROR("Unsupported feature command %d", cmd_val);
		return -EINVAL;
	}

	// Special handling for commands DPMAIF_SW_INIT
	if (cmd_val == NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT) {
		struct mtk_dpmaif_ctlb *dcb = NULL;
		struct dpmaif_txq *txqs = NULL;
		bool txq_has_pending = false;
		int txq_cnt;

		if (!data) {
			NOA_MD_ERROR("mdev->data_blk->dcb is NULL");
			return -ENODEV;
		}

		dcb = (struct mtk_dpmaif_ctlb *)data;
		txq_cnt = dcb->drv_info->cfg->tx_cfg.txq_cnt;
		txqs = dcb->txqs;

		if (!txqs) {
			NOA_MD_ERROR("txqs is NULL");
			return -ENODEV;
		}

		// TODO: This is a temporary check logic. We should explain in more
		// detail why we doing these checks for the sw init command
		if (txqs) {
			for (int i = 0; i < txq_cnt; i++) {
				if (txqs[i].drb_wr_idx != txqs[i].drb_rd_idx) {
					NOA_MD_ERROR(
						"TXQ[%d] has pending packets "
						"(wr:%u, rd:%u, rel:%u)",
						i, txqs[i].drb_wr_idx, txqs[i].drb_rd_idx,
						txqs[i].drb_rel_rd_idx
					);
					txq_has_pending = true;
					break;
				}

				if (txqs[i].drb_rd_idx != txqs[i].drb_rel_rd_idx) {
					NOA_MD_ERROR(
						"TXQ[%d] has unreleased packets "
						"(wr:%u, rd:%u, rel:%u)",
						i, txqs[i].drb_wr_idx, txqs[i].drb_rd_idx,
						txqs[i].drb_rel_rd_idx);
					txq_has_pending = true;
					break;
				}
			}
		}

		if (txq_has_pending) {
			NOA_MD_ERROR(
				"Pending packets detected in TX queues. "
				"Skipping SW_INIT command execution");
			return -EAGAIN;
		}
	}

	NOA_MD_INFO("Calling noa_md_wpr_t900_feature_cmd with cmd=%d, data=0x%p",
		cmd_val, data);

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
	ret = noa_md_wpr_t900_feature_cmd(cmd_val, data);
	if (ret) {
		NOA_MD_ERROR("t900_feature_cmd failed with ret=%d", ret);
		return ret;
	}
#else
	NOA_MD_ERROR("T900 support is not compiled in");
	return -EOPNOTSUPP;
#endif

	return count;
}

static const struct file_operations t900_cmd_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = t900_cmd_write,
	.llseek = noop_llseek,
};

int noa_md_debug_t900_cmd_init(
	struct dentry *noa_root, struct noa_md_dev *p_md_dev)
{
	if (!debugfs_create_file("t900_cmd", 0220, noa_root, p_md_dev,
				 &t900_cmd_fops)) {  // Using 0220 for a write-only node
		NOA_MD_ERROR("Failed to create 't900_cmd' debugfs file");
		return -ENOMEM;
	}

	if (!debugfs_create_file("t900_cmd_enable", 0644, noa_root, p_md_dev,
				 &t900_cmd_enable_fops)) {
		NOA_MD_ERROR("Failed to create 't900_cmd_enable' debugfs file");
		return -ENOMEM;
	}

#if IS_ENABLED(DCONFIG_NOA_MD_DEBUG_DISABLE_DPMAIF_SW_INIT) && \
	IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_SUPPORT)
	noa_md_wpr_t900_set_cmd_enabled(NOA_MD_WPR_T900_CMD_DPMAIF_SW_INIT, false);
#endif

	NOA_MD_INFO("'t900_cmd' debugfs interface created");
	return 0;
}

void noa_md_debug_t900_cmd_exit(void)
{
	/*
	 * No action needed here.
	 * The parent debugfs module will call debugfs_remove_recursive() on the
	 * root directory, which cleans up everything we created in our init
	 * function.
	 */
}