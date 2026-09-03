// SPDX-License-Identifier: GPL-2.0-only
/*
 * GSLC debugfs support
 *
 * Copyright (C) 2021 Google LLC.
 */
#include "gslc_debugfs.h"

#include <linux/debugfs.h>

#include "gslc_cpm_mba.h"
#include "gslc_partition.h"
#include "gslc_regmap.h"

/**
 * gslc_reg_set() - Sets the value of a GSLC CSR
 *
 * @data - Debug data containing the CSR offset
 * @val  - Value to be written at the offset
 */
static int gslc_reg_set(void *data, u64 val)
{
	struct gslc_dbg *dbg = (struct gslc_dbg *)data;

	gslc_csr_write(dbg->reg_offset, (u32)(val));

	return 0;
}

/**
 * gslc_reg_get() - Gets the value of a GSLC CSR
 *
 * @data - Debug data containing the CSR offset
 * @val  - Value that is read from the CSR offset
 */
static int gslc_reg_get(void *data, u64 *val)
{
	struct gslc_dbg *dbg = (struct gslc_dbg *)data;

	*val = gslc_csr_read(dbg->reg_offset);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_val, gslc_reg_get, gslc_reg_set, "0x%08llx\n");

/**
 * gslc_pid_cmd_process() - Used to enable/disable/mutate the PID
 *
 * @data - Debug data containing the PID cmd value
 * @val  - PID command
 *
 * Return - 0 on success, negative on error
 */
static int gslc_pid_cmd_process(void *data, u64 val)
{
	struct gslc_dev *gslc_dev = (struct gslc_dev *)data;
	struct gslc_dbg *dbg = gslc_dev->dbg;
	struct gslc_mba_raw_msg req = { 0 };

	switch (val) {
	case CMD_ID_PARTITION_ENABLE: {
		struct gslc_partition_en_req *en_req =
			(struct gslc_partition_en_req *)(&req);
		en_req->cmd = CMD_ID_PARTITION_ENABLE;
		en_req->cfg = dbg->cfg;
		en_req->priority = dbg->priority;
		en_req->ovr_valid = dbg->ovr_valid;
		en_req->size = dbg->size;
		en_req->overrides = dbg->overrides;
		break;
	}
	case CMD_ID_PARTITION_DISABLE: {
		struct gslc_partition_dis_req *dis_req =
			(struct gslc_partition_dis_req *)(&req);
		dis_req->cmd = CMD_ID_PARTITION_DISABLE;
		dis_req->pid = dbg->pid;
		break;
	}
	case CMD_ID_PARTITION_MUTATE: {
		struct gslc_partition_mutate_req *mut_req =
			(struct gslc_partition_mutate_req *)(&req);
		mut_req->cmd = CMD_ID_PARTITION_MUTATE;
		mut_req->cfg = dbg->cfg;
		mut_req->priority = dbg->priority;
		mut_req->pid = dbg->pid;
		mut_req->size = dbg->size;
		break;
	}
	default:
		dbg->pid = 0;
		return -EINVAL;
	}
	dbg->pid = gslc_client_partition_req(gslc_dev, &req);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_pid_cmd, NULL, gslc_pid_cmd_process,
			 "0x%08llx\n");

/**
 * gslc_create_debugfs() - Create the debugfs dir and files
 *
 * All command and command parameters are write-only and status registers are
 * read-only.
 *
 * @gslc_dbg - Pointer to the GSLC debug struct
 */
void gslc_create_debugfs(struct gslc_dev *gslc_dev)
{
	struct gslc_dbg *dbg = gslc_dev->dbg;
	struct dentry *sfr_dentry = NULL;
	struct dentry *partition_dentry = NULL;

	dbg->base_dir = debugfs_create_dir("gslc", NULL);

	/* SFR directory - used to read/write to any SLC SFR*/
	sfr_dentry = debugfs_create_dir("sfr", dbg->base_dir);
	debugfs_create_x32("offset", 0200, sfr_dentry, &dbg->reg_offset);
	debugfs_create_file("val", 0600, sfr_dentry, dbg, &fops_val);

	/* Partition directory - Used to enable/disable partitions*/
	partition_dentry = debugfs_create_dir("partition", dbg->base_dir);
	debugfs_create_u8("pid", 0600, partition_dentry, &dbg->pid);
	debugfs_create_u8("cfg", 0200, partition_dentry, &dbg->cfg);
	debugfs_create_u8("priority", 0200, partition_dentry, &dbg->priority);
	debugfs_create_u8("ovr_valid", 0200, partition_dentry, &dbg->ovr_valid);
	debugfs_create_u32("ovr", 0200, partition_dentry,
			   &dbg->overrides);
	debugfs_create_u32("size", 0200, partition_dentry, &dbg->size);
	debugfs_create_file("cmd", 0200, partition_dentry, gslc_dev,
			    &fops_pid_cmd);
}

/**
 * gslc_remove_debugfs() - Cleanup the debug directories
 *
 * @gslc_dbg - Pointer to the GSLC debug struct
 */
void gslc_remove_debugfs(struct gslc_dev *gslc_dev)
{
	debugfs_remove_recursive(gslc_dev->dbg->base_dir);
}
