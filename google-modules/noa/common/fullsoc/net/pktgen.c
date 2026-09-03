// SPDX-License-Identifier: GPL-2.0-only
/*
 * LVM Network Packet Generator
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 *
 * This file provides implementations for managing captured packets for
 * LVM packet generator. This includes reading packet data from files
 * and dumping the raw packet content.
 */

#include <linux/uaccess.h>
#include <core/print.h>
#include <net/pktgen.h>

/**
 * lvm_pktgen_file_read - Read captured packet data from a file
 * @pktgen: Pointer to the lvm_pktgen structure
 *
 * This function reads the packet data from the file specified by
 * pktgen->filepath and stores it in the pktgen->raw buffer. It also
 * updates the pktgen->len field with the size of the read data.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_pktgen_file_read(struct lvm_pktgen *pktgen)
{
	struct file *file;
	loff_t pos = 0;
	int ret = 0;

	if (!pktgen || !pktgen->filepath)
		return -EINVAL;

	file = filp_open(pktgen->filepath, O_RDONLY, 0);
	if (IS_ERR(file)) {
		LVM_ERR("LVM pktgen file opening failed: %s\n",
			pktgen->filepath);
		return PTR_ERR(file);
	}

	pktgen->len = i_size_read(file_inode(file));
	if (pktgen->len <= 0) {
		LVM_ERR("LVM pktgen file size is invalid\n");
		filp_close(file, NULL);
		return -EINVAL;
	}

	pktgen->raw = kmalloc(pktgen->len, GFP_KERNEL);
	if (!pktgen->raw) {
		filp_close(file, NULL);
		return -ENOMEM;
	}

	ret = kernel_read(file, pktgen->raw, pktgen->len, &pos);
	if (ret != pktgen->len) {
		LVM_ERR("LVM pktgen file reading failed\n");
		kfree(pktgen->raw);
		pktgen->raw = NULL;
		filp_close(file, NULL);
		return -EIO;
	}

	filp_close(file, NULL);

	return 0;
}

/**
 * lvm_pktgen_raw_dump - Dump the raw packet content to a buffer
 * @pktgen: Pointer to the lvm_pktgen structure
 * @buf: Pointer to the buffer to store the dumped data
 *
 * This function dumps the raw packet content stored in the pktgen->raw
 * to the provided buffer in a hexadecimal format.
 *
 * Return: Number of bytes written to the buffer.
 */
ssize_t lvm_pktgen_raw_dump(struct lvm_pktgen *pktgen, char *buf)
{
	int i, offset = 0;

	if (!pktgen || !pktgen->raw)
		return offset;

	offset += sprintf(buf + offset, "Dump raw contents:\n");
	offset += sprintf(buf + offset, "===============================\n");

	for (i = 0; i < pktgen->len; i++) {
		offset += sprintf(buf + offset, "%02x ", pktgen->raw[i]);

		if ((i + 1) % 16 == 0)
			offset += sprintf(buf + offset, "\n");
	}

	offset += sprintf(buf + offset, "\n===============================\n");

	return offset;
}

/**
 * lvm_pktgen_init - Initialize the LVM packet generator
 * @net: Pointer to the global LVM network structure
 *
 * This function initializes the LVM packet generator and stores the instance
 * in the lvm_net structure.
 *
 * Return: 0 on success, negative error code otherwise.
 */
int lvm_pktgen_init(struct lvm_net *net)
{
	if (!net)
		return -EINVAL;

	net->pktgen = kzalloc(sizeof(struct lvm_pktgen), GFP_KERNEL);
	if (!net->pktgen)
		return -ENOMEM;

	return 0;
}

/**
 * lvm_pktgen_deinit - Deinitialize the LVM packet generator
 * @net: Pointer to the global LVM network structure
 *
 * This function deinitializes the LVM packet generator by freeing the
 * memory allocated for the lvm_pktgen structure.
 */
void lvm_pktgen_deinit(struct lvm_net *net)
{
	if (!net)
		return;

	kfree(net->pktgen->raw);

	kfree(net->pktgen->filepath);

	kfree(net->pktgen);
}

MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
