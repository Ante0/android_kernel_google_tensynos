// SPDX-License-Identifier: GPL-2.0
/*
 * The mock client driver for the RA driver.
 * This driver provides the debugfs interface to trigger the RA driver
 * APIs. It would act as a client driver of RA driver to test the PID
 * configuration flow.
 *
 * The module generates the debugfs directory and files when it is loaded:
 *  - ra_index(read/write): select the RA driver with the given index.
 *  - sid(read/write), rpid(read/write), wpid(read/write): to configure
 *    the SID -> PID mapping, write the values in these files in order of
 *    sid -> r/wpid
 *
 * To control the device power status with the sysfs interface:
 *  - rpm resume: echo on > /sys/devices/platform/${RA_TEST_DEVICE}/power/control
 *  - rpm suspend: echo auto > /sys/devices/platform/${RA_TEST_DEVICE}/power/control
 *
 * Copyright (C) 2023, Google LLC
 */
#include <linux/debugfs.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>

#include <ra/google_ra.h>

struct google_ra_test_dbg {
	struct dentry *base_dir;
	u32 ra_index;
	u32 sid;
	u32 rpid;
	u32 wpid;
};

struct google_ra_test {
	struct device *dev;
	struct google_ra **ras;
	u32 num_ra;
	struct google_ra_test_dbg dbg;
};

static int google_ra_test_read_ra_index(void *data, u64 *index)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;

	*index = ra_test->dbg.ra_index;
	return 0;
}

static int google_ra_test_select_ra_by_index(void *data, u64 index)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;

	if (index >= ra_test->num_ra) {
		dev_err(ra_test->dev, "invalid ra index %llu\n", index);
		return -EINVAL;
	}
	ra_test->dbg.ra_index = index;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ra_test_read_write_index, google_ra_test_read_ra_index,
			 google_ra_test_select_ra_by_index, "0x%02llx\n");

static int google_ra_test_read_sid(void *data, u64 *val)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;

	*val = ra_test->dbg.sid;
	return 0;
}

static int google_ra_test_write_sid(void *data, u64 sid)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;
	struct google_ra *ra = ra_test->ras[ra_test->dbg.ra_index];
	int err;

	err = google_ra_sid_get_pid(ra, (u32)sid, &ra_test->dbg.rpid, &ra_test->dbg.wpid);
	if (err)
		return err;

	ra_test->dbg.sid = sid;

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ra_test_rw_sid, google_ra_test_read_sid, google_ra_test_write_sid,
			 "0x%02llx\n");

static int google_ra_test_write_rpid(void *data, u64 rpid)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;
	struct google_ra *ra = ra_test->ras[ra_test->dbg.ra_index];

	ra_test->dbg.rpid = rpid;

	return google_ra_sid_set_pid(ra, ra_test->dbg.sid, ra_test->dbg.rpid, ra_test->dbg.wpid);
}

static int google_ra_test_read_rpid(void *data, u64 *val)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;

	*val = ra_test->dbg.rpid;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ra_test_rw_rpid, google_ra_test_read_rpid, google_ra_test_write_rpid,
			 "0x%02llx\n");

static int google_ra_test_write_wpid(void *data, u64 wpid)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;
	struct google_ra *ra = ra_test->ras[ra_test->dbg.ra_index];

	ra_test->dbg.wpid = wpid;
	return google_ra_sid_set_pid(ra, ra_test->dbg.sid, ra_test->dbg.rpid, ra_test->dbg.wpid);
}

static int google_ra_test_read_wpid(void *data, u64 *val)
{
	struct google_ra_test *ra_test = (struct google_ra_test *)data;

	*val = ra_test->dbg.wpid;
	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_ra_test_rw_wpid, google_ra_test_read_wpid, google_ra_test_write_wpid,
			 "0x%02llx\n");

int google_ra_test_create_debugfs(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_ra_test *ra_test = platform_get_drvdata(pdev);
	struct dentry *ra_test_dentry = debugfs_create_dir(dev->of_node->full_name, NULL);
	int err;

	if (IS_ERR(ra_test_dentry)) {
		dev_err(dev, "fails to create a debugfs dir\n");
		return PTR_ERR(ra_test_dentry);
	}
	ra_test->dbg.base_dir = ra_test_dentry;

	ra_test_dentry = debugfs_create_file("ra_index", 0600, ra_test->dbg.base_dir, ra_test,
					     &fops_ra_test_read_write_index);
	if (IS_ERR(ra_test_dentry)) {
		dev_err(dev, "fails to create debugfs file: ra_index\n");
		err = PTR_ERR(ra_test_dentry);
		goto remove_debugfs;
	}

	ra_test_dentry = debugfs_create_file("sid", 0600, ra_test->dbg.base_dir, ra_test,
					     &fops_ra_test_rw_sid);
	if (IS_ERR(ra_test_dentry)) {
		dev_err(dev, "fails to create debugfs file: sid\n");
		err = PTR_ERR(ra_test_dentry);
		goto remove_debugfs;
	}

	ra_test_dentry = debugfs_create_file("rpid", 0600, ra_test->dbg.base_dir, ra_test,
					     &fops_ra_test_rw_rpid);
	if (IS_ERR(ra_test_dentry)) {
		dev_err(dev, "fails to create debugfs file: rpid\n");
		err = PTR_ERR(ra_test_dentry);
		goto remove_debugfs;
	}

	ra_test_dentry = debugfs_create_file("wpid", 0600, ra_test->dbg.base_dir, ra_test,
					     &fops_ra_test_rw_wpid);
	if (IS_ERR(ra_test_dentry)) {
		dev_err(dev, "fails to create debugfs file: wpid\n");
		err = PTR_ERR(ra_test_dentry);
		goto remove_debugfs;
	}

	return 0;

remove_debugfs:
	debugfs_remove_recursive(ra_test->dbg.base_dir);
	return err;
}

void google_ra_test_remove_debugfs(struct platform_device *pdev)
{
	struct google_ra_test *ra_test = platform_get_drvdata(pdev);

	debugfs_remove_recursive(ra_test->dbg.base_dir);
}

static int google_ra_test_init_ra_drivers(struct google_ra_test *ra_test)
{
	struct device_node *np = ra_test->dev->of_node;
	int num_ra;
	u32 i;

	num_ra = of_count_phandle_with_args(np, "ras", NULL);
	if (num_ra < 0) {
		dev_err(ra_test->dev, "unable to find ras property from %pOF\n", np);
		return num_ra;
	} else if (num_ra == 0) {
		dev_err(ra_test->dev, "ras property is an empty list %pOF\n", np);
		return -EINVAL;
	}
	ra_test->num_ra = num_ra;

	ra_test->ras =
		devm_kcalloc(ra_test->dev, ra_test->num_ra, sizeof(*ra_test->ras), GFP_KERNEL);
	if (!ra_test->ras)
		return -ENOMEM;

	for (i = 0; i < ra_test->num_ra; i++) {
		ra_test->ras[i] = get_google_ra_by_index(ra_test->dev, i);
		if (IS_ERR(ra_test->ras[i])) {
			dev_err(ra_test->dev, "unable to init ra (index %u)\n", i);
			return PTR_ERR(ra_test->ras[i]);
		}
	}
	return 0;
}

static int google_ra_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_ra_test *ra_test;
	int err;

	ra_test = devm_kzalloc(dev, sizeof(*ra_test), GFP_KERNEL);
	if (!ra_test)
		return -ENOMEM;

	ra_test->dev = dev;
	platform_set_drvdata(pdev, ra_test);

	err = google_ra_test_init_ra_drivers(ra_test);
	if (err)
		return err;

	err = google_ra_test_create_debugfs(pdev);
	if (err)
		return err;

	if (dev->pm_domain)
		devm_pm_runtime_enable(dev);

	return 0;
}

static void google_ra_test_remove(struct platform_device *pdev)
{
	google_ra_test_remove_debugfs(pdev);
}

static const struct of_device_id google_ra_test_of_match_table[] = {
	{ .compatible = "google,ra_test_client" },
	{},
};
MODULE_DEVICE_TABLE(of, google_ra_test_of_match_table);

static struct platform_driver google_ra_test_driver = {
	.probe = google_ra_test_probe,
	.remove = google_ra_test_remove,
	.driver = {
		.name = "google-ra-test-client",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_ra_test_of_match_table),
	},
};
module_platform_driver(google_ra_test_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google RA test client driver");
MODULE_LICENSE("GPL");
