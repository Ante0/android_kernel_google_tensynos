// SPDX-License-Identifier: GPL-2.0-only
/*
 * GSLC debugfs support
 *
 * Copyright (C) 2023 Google LLC.
 */
#include <linux/debugfs.h>

#include <ra/google_ra.h>

static int google_ra_read_sid(void *data, u64 *val)
{
	struct google_ra *ra_dev = (struct google_ra *)data;
	struct google_ra_dbg *dbg = ra_dev->dbg;

	*val = dbg->sid;
	return 0;
}

static int google_ra_write_sid(void *data, u64 sid)
{
	struct google_ra *ra_dev = (struct google_ra *)data;
	struct google_ra_dbg *dbg = ra_dev->dbg;
	int err;

	err = google_ra_sid_get_pid(ra_dev, (u32)sid, &dbg->rpid, &dbg->wpid);
	if (err)
		return err;

	dbg->sid = (u32)sid;
	dev_info(ra_dev->dev, "current SID->PID config: sid 0x%02x: rpid 0x%02x, wpid 0x%02x\n",
		 dbg->sid, dbg->rpid, dbg->wpid);

	return err;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ra_read_write_sid, google_ra_read_sid, google_ra_write_sid,
			 "0x%02llx\n");

static int google_ra_read_rpid(void *data, u64 *val)
{
	struct google_ra *ra_dev = (struct google_ra *)data;
	struct google_ra_dbg *dbg = ra_dev->dbg;

	*val = dbg->rpid;
	return 0;
}

static int google_ra_write_rpid(void *data, u64 rpid)
{
	struct google_ra *ra_dev = (struct google_ra *)data;
	struct google_ra_dbg *dbg = ra_dev->dbg;
	int err;

	err = google_ra_sid_set_pid(ra_dev, dbg->sid, (u32)rpid, dbg->wpid);
	if (err)
		return err;

	dbg->rpid = (u32)rpid;
	dev_info(ra_dev->dev, "PID set: sid 0x%02x: rpid 0x%02x, wpid 0x%02x\n", dbg->sid,
		 dbg->rpid, dbg->wpid);

	return err;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ra_read_write_rpid, google_ra_read_rpid, google_ra_write_rpid,
			 "0x%02llx\n");

static int google_ra_read_wpid(void *data, u64 *val)
{
	struct google_ra *ra_dev = (struct google_ra *)data;
	struct google_ra_dbg *dbg = ra_dev->dbg;

	*val = dbg->wpid;
	return 0;
}

static int google_ra_write_wpid(void *data, u64 wpid)
{
	struct google_ra *ra_dev = (struct google_ra *)data;
	struct google_ra_dbg *dbg = ra_dev->dbg;
	int err;

	err = google_ra_sid_set_pid(ra_dev, dbg->sid, dbg->rpid, (u32)wpid);
	if (err)
		return err;

	dbg->wpid = (u32)wpid;
	dev_info(ra_dev->dev, "PID set: sid 0x%02x: rpid 0x%02x, wpid 0x%02x\n", dbg->sid,
		 dbg->rpid, dbg->wpid);

	return err;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ra_read_write_wpid, google_ra_read_wpid, google_ra_write_wpid,
			 "0x%02llx\n");

int google_ra_create_debugfs(struct google_ra *ra_dev)
{
	struct google_ra_dbg *dbg = devm_kzalloc(ra_dev->dev, sizeof(*ra_dev->dbg), GFP_KERNEL);
	struct dentry *ra_dentry = debugfs_create_dir(ra_dev->full_name, NULL);

	if (!dbg)
		return -ENOMEM;

	ra_dev->dbg = dbg;
	dbg->base_dir = ra_dentry;

	debugfs_create_file("sid", 0600, ra_dentry, ra_dev, &fops_ra_read_write_sid);
	debugfs_create_file("rpid", 0600, ra_dentry, ra_dev, &fops_ra_read_write_rpid);
	debugfs_create_file("wpid", 0600, ra_dentry, ra_dev, &fops_ra_read_write_wpid);

	return 0;
}

void google_ra_remove_debugfs(struct google_ra *ra_dev)
{
	debugfs_remove_recursive(ra_dev->dbg->base_dir);
}
