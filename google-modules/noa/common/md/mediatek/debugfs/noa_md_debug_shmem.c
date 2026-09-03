// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD Shared Memory Layout Debugfs Implementation
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/debugfs.h>
#include <linux/seq_file.h>  /* For seq_file, single_open */
#include <linux/uaccess.h>  /* For copy_from_user */

#include "common/md/mediatek/noa_md_shmem_layout.h"  /* For struct noa_md_shmem_layout */
#include "noa_md.h"
#include "noa_md_debug_shmem.h"
#include "noa_md_shmem_sync.h"
#include "noa_md_trace.h"  /* For NOA_MD_ERROR */

/**
 * noa_md_debug_shmem_layout_show() - Shows the content of the shared memory.
 * @m: Seq_file pointer. The private member m->private holds our device struct.
 * @v: Unused.
 *
 * This function is called when a user reads the 'layout' debugfs file.
 * It reads from the shared memory region and formats the content for display.
 *
 * Return: Always returns 0.
 */
static int noa_md_debug_shmem_layout_show(struct seq_file *m, void *v)
{
	struct noa_md_dev *dev = m->private;
	struct noa_md_shmem_layout *shmem;

	if (!dev) {
		seq_puts(m, "Error: NOA device not available\n");
		return 0;
	}

	if (!dev->shmem_handle.va_base) {
		seq_puts(m, "Error: Shared memory not initialized or mapped\n");
		return 0;
	}

	shmem = (struct noa_md_shmem_layout *)dev->shmem_handle.va_base;

	seq_puts(m, "NOA MD Shared Memory Layout:\n");
	seq_puts(m, "----------------------------\n");
	// Sample output
	seq_printf(m, "txq[0] pkt cnt: %d\n", shmem->txqs[0].pkt_cnt);
	seq_puts(m, "----------------------------\n");

	return 0;
}

/**
 * noa_md_debug_shmem_layout_open() - Handles opening the 'layout' debugfs file.
 * @inode: Inode structure, contains the pointer to our device struct.
 * @file:  File structure, to which we attach our seq_file operations.
 *
 * This function sets up the seq_file framework for reading the debugfs file.
 *
 * Return: %0 on success, or a negative error code on failure.
 */
static int noa_md_debug_shmem_layout_open(struct inode *inode, struct file *file)
{
	return single_open(file, noa_md_debug_shmem_layout_show, inode->i_private);
}

/* File operations structure for the "layout" debugfs node. */
static const struct file_operations noa_md_dbg_shmem_layout_fops = {
	.owner   = THIS_MODULE,
	.open    = noa_md_debug_shmem_layout_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/**
 * noa_md_debug_shmem_cmd_to_str() - Converts a debug command index to a string.
 * @cmd: The debug command index.
 *
 * Return: A string representation of the command.
 */
static const char *noa_md_debug_shmem_cmd_to_str(enum noa_md_shmem_debug_cmd cmd)
{
	switch (cmd) {
	case NOA_MD_SHMEM_DEBUG_CMD_NONE:
		return "NOA_MD_SHMEM_DEBUG_CMD_NONE";
	case NOA_MD_SHMEM_DEBUG_CMD_ENABLE:
		return "NOA_MD_SHMEM_DEBUG_CMD_ENABLE";
	default:
		return "UNKNOWN_DEBUG_CMD";
	}
}

/**
 * noa_md_debug_shmem_debug_notify_show() - Lists available debug sub-commands.
 * @m: Seq_file pointer.
 * @v: Unused.
 *
 * This function is called when a user reads the 'debug_notify' debugfs file.
 * It lists the defined sub-commands for the debug channel and their indices.
 *
 * Return: Always returns 0.
 */
static int noa_md_debug_shmem_debug_notify_show(struct seq_file *m, void *v)
{
	unsigned int i;

	seq_puts(m, "NOA MD Debug Notify Sub-commands:\n");
	seq_puts(m, "---------------------------------\n");

	for (i = 0; i < NOA_MD_SHMEM_DEBUG_CMD_MAX; i++) {
		seq_printf(m, "[%u] %s\n", i,
			   noa_md_debug_shmem_cmd_to_str((enum noa_md_shmem_debug_cmd)i));
	}

	seq_puts(m, "---------------------------------\n");
	seq_puts(m, "Usage: echo <index> > debug_notify\n");

	return 0;
}

/**
 * noa_md_debug_shmem_debug_notify_open() - Handles opening the 'debug_notify' file.
 * @inode: Inode structure.
 * @file:  File structure.
 *
 * Return: %0 on success.
 */
static int noa_md_debug_shmem_debug_notify_open(struct inode *inode, struct file *file)
{
	return single_open(file, noa_md_debug_shmem_debug_notify_show, inode->i_private);
}

/**
 * noa_md_debug_shmem_debug_notify_write() - Triggers the debug notification doorbell.
 * @file:  File structure.
 * @buf:   User buffer.
 * @count: Number of bytes to write.
 * @ppos:  Current file position.
 *
 * This function is called when a user writes to the 'debug_notify' debugfs file.
 * It sends a generic debug command to the NCP, which rings the
 * NOA_MD_APC2NCP_SHMEM_DEBUG_NOTIFY doorbell.
 *
 * Return: @count on success, or a negative error code on failure.
 */
static ssize_t noa_md_debug_shmem_debug_notify_write(struct file *file,
						    const char __user *buf,
						    size_t count, loff_t *ppos)
{
	struct noa_md_dev *dev = file_inode(file)->i_private;
	struct noa_md_shmem_debug_payload req = {0};
	struct noa_md_shmem_debug_payload resp = {0};
	unsigned int val;
	int ret;

	if (!dev || !dev->shmem_sync) {
		NOA_MD_ERROR("NOA device(%p) or shmem_sync(%p) not available",
			dev, dev->shmem_sync);
		return -EINVAL;
	}

	ret = kstrtouint_from_user(buf, count, 0, &val);
	if (ret) {
		NOA_MD_ERROR("Failed to parse sub_cmd from user: %d", ret);
		return ret;
	}

	NOA_MD_INFO("Triggering NOA_MD_APC2NCP_SHMEM_DEBUG_NOTIFY [%u: %s]",
		    val, noa_md_debug_shmem_cmd_to_str((enum noa_md_shmem_debug_cmd)val));

	/* Initialize request payload. */
	req.sub_cmd = (u32)val;

	/* Send generic debug command with user-provided sub_cmd. */
	ret = noa_md_shmem_sync_send_debug_cmd(dev->shmem_sync,
					       (enum noa_md_shmem_debug_cmd)val,
					       &req, &resp);
	if (ret) {
		NOA_MD_ERROR("Failed to send debug cmd [%u: %s]: %d",
			     val, noa_md_debug_shmem_cmd_to_str((enum noa_md_shmem_debug_cmd)val),
			     ret);
		return ret;
	}

	if (val == NOA_MD_SHMEM_DEBUG_CMD_ENABLE) {
		NOA_MD_INFO("Debug enable status received from NCP: %u",
			    resp.enable_resp.status);
	}

	return count;
}

/* File operations structure for the "debug_notify" debugfs node. */
static const struct file_operations noa_md_dbg_shmem_debug_notify_fops = {
	.owner   = THIS_MODULE,
	.open    = noa_md_debug_shmem_debug_notify_open,
	.read    = seq_read,
	.write   = noa_md_debug_shmem_debug_notify_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/**
 * noa_md_debug_shmem_init() - Initialize the shmem debugfs entries.
 * @noa_root: The root dentry for the NOA driver's debugfs files.
 * @p_md_dev: Pointer to the main NOA device structure.
 *
 * Creates a 'shmem' directory and a 'layout' file within it.
 *
 * Return: %0 on success, or a negative error code on failure.
 */
int noa_md_debug_shmem_init(struct dentry *noa_root, struct noa_md_dev *p_md_dev)
{
	struct dentry *shmem_dir;

	CHECK_PTR_OR_RETURN_ERR(noa_root, -EINVAL);
	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	shmem_dir = debugfs_create_dir("shmem", noa_root);
	if (!shmem_dir) {
		NOA_MD_ERROR("Failed to create 'shmem' debugfs directory");
		return -ENOMEM;
	}

	if (!debugfs_create_file("layout", 0444, shmem_dir, p_md_dev,
				 &noa_md_dbg_shmem_layout_fops)) {
		NOA_MD_ERROR("Failed to create 'layout' debugfs file");
		return -ENOMEM;
	}

	if (!debugfs_create_file("debug_notify", 0644, shmem_dir, p_md_dev,
				 &noa_md_dbg_shmem_debug_notify_fops)) {
		NOA_MD_ERROR("Failed to create 'debug_notify' debugfs file");
		return -ENOMEM;
	}

	return 0;
}

/**
 * noa_md_debug_shmem_exit() - Clean up the shmem debugfs entries.
 *
 * Removes the 'shmem' directory and all its contents.
 */
void noa_md_debug_shmem_exit(void)
{
	/*
	 * No action needed here.
	 * The parent debugfs module will call debugfs_remove_recursive() on the
	 * root directory, which cleans up everything we created in our init
	 * function.
	 */
}
