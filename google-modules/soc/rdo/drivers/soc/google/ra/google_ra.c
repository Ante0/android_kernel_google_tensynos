// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for Realm Allocator (RA).
 * This enables client devices to set up the SID-to-PID mapping in RA,
 * or to turn on/off PID override.
 * The Partition ID (PID) can be used to control cache allocation in the
 * Google System Level Cache (GSLC).
 *
 * Copyright (C) 2023, Google LLC
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <ra/google_ra.h>

#define GOOGLE_RA_AUTOSUSPEND_DELAY_MS 1

/* property register bitfields */
#define GOOGLE_RA_PROPERTY (0x04)
#define GOOGLE_RA_PROPERTY_PID_WIDTH_FIELD GENMASK(15, 8)
#define GOOGLE_RA_PROPERTY_SID_WIDTH_FIELD GENMASK(7, 0)

/* pid tbl register bitfields */
#define GOOGLE_RA_PID_TBL_BASE (0x200)
#define GOOGLE_RA_IGN_PT_AWPID_1_FIELD BIT(31)
#define GOOGLE_RA_AWPID_1_FIELD GENMASK(30, 24)
#define GOOGLE_RA_IGN_PT_ARPID_1_FIELD BIT(23)
#define GOOGLE_RA_ARPID_1_FIELD GENMASK(22, 16)
#define GOOGLE_RA_IGN_PT_AWPID_0_FIELD BIT(15)
#define GOOGLE_RA_AWPID_0_FIELD GENMASK(14, 8)
#define GOOGLE_RA_IGN_PT_ARPID_0_FIELD BIT(7)
#define GOOGLE_RA_ARPID_0_FIELD GENMASK(6, 0)

#define GOOGLE_RA_PID_OVERRIDE_ENABLED (0)
#define GOOGLE_RA_PID_TBL_OFFSET(sid) (4 * ((sid) / 2))

static const struct regmap_config google_ra_pid_tbl_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.reg_base = GOOGLE_RA_PID_TBL_BASE,
	.name = "sid_pid_tbl_regmap",
};

static struct platform_driver google_ra_driver;

static int google_ra_update_pid_tbl(struct google_ra *ra, u32 sid, u32 rpid, u32 rpid_ign, u32 wpid,
				    u32 wpid_ign)
{
	ptrdiff_t offset = GOOGLE_RA_PID_TBL_OFFSET(sid);
	u32 mask = 0;
	u32 pid = 0;
	int err = 0;

	if (sid % 2 == 0) {
		u32p_replace_bits(&pid, rpid, GOOGLE_RA_ARPID_0_FIELD);
		u32p_replace_bits(&pid, rpid_ign, GOOGLE_RA_IGN_PT_ARPID_0_FIELD);
		u32p_replace_bits(&pid, wpid, GOOGLE_RA_AWPID_0_FIELD);
		u32p_replace_bits(&pid, wpid_ign, GOOGLE_RA_IGN_PT_AWPID_0_FIELD);
		mask = GOOGLE_RA_ARPID_0_FIELD | GOOGLE_RA_IGN_PT_ARPID_0_FIELD |
		       GOOGLE_RA_AWPID_0_FIELD | GOOGLE_RA_IGN_PT_AWPID_0_FIELD;
	} else {
		u32p_replace_bits(&pid, rpid, GOOGLE_RA_ARPID_1_FIELD);
		u32p_replace_bits(&pid, rpid_ign, GOOGLE_RA_IGN_PT_ARPID_1_FIELD);
		u32p_replace_bits(&pid, wpid, GOOGLE_RA_AWPID_1_FIELD);
		u32p_replace_bits(&pid, wpid_ign, GOOGLE_RA_IGN_PT_AWPID_1_FIELD);
		mask = GOOGLE_RA_ARPID_1_FIELD | GOOGLE_RA_IGN_PT_ARPID_1_FIELD |
		       GOOGLE_RA_AWPID_1_FIELD | GOOGLE_RA_IGN_PT_AWPID_1_FIELD;
	}

	if (pm_runtime_enabled(ra->dev))
		pm_runtime_get_sync(ra->dev);

	err = regmap_update_bits(ra->sid_pid_tbl, offset, mask, pid);
	if (err)
		goto put_ra_exit;

	ra->pid_cache[sid >> 1].usage = true;
	err = regmap_read(ra->sid_pid_tbl, offset, &ra->pid_cache[sid >> 1].reg);

put_ra_exit:
	if (pm_runtime_enabled(ra->dev)) {
		pm_runtime_mark_last_busy(ra->dev);
		pm_runtime_put_autosuspend(ra->dev);
	}
	return err;
}

struct google_ra *get_google_ra_by_index(struct device *consumer, int ra_index)
{
	struct device_link *link;
	struct device_node *ra_np;
	struct device *dev;
	struct google_ra *ra;

	ra_np = of_parse_phandle(consumer->of_node, "ras", ra_index);
	if (!ra_np) {
		dev_err(consumer, "%pOF is missing ras[%d] property to link to realm allocator\n",
			consumer->of_node, ra_index);
		return ERR_PTR(-EINVAL);
	}

	dev = driver_find_device_by_of_node(&google_ra_driver.driver, ra_np);
	of_node_put(ra_np);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	link = device_link_add(consumer, dev, DL_FLAG_AUTOREMOVE_CONSUMER | DL_FLAG_PM_RUNTIME);
	if (!link) {
		dev_err(dev, "unable to add RA device link\n");
		return ERR_PTR(-EINVAL);
	}

	ra = dev_get_drvdata(dev);
	return ra;
}
EXPORT_SYMBOL_GPL(get_google_ra_by_index);

struct google_ra *get_google_ra_by_name(struct device *consumer, const char *ra_name)
{
	int ra_index;

	ra_index = of_property_match_string(consumer->of_node, "ra-names", ra_name);
	if (ra_index < 0) {
		pr_err("%pOF fails to match string %s in ra-names property\n", consumer->of_node,
		       ra_name);
		return ERR_PTR(ra_index);
	}
	return get_google_ra_by_index(consumer, ra_index);
}
EXPORT_SYMBOL_GPL(get_google_ra_by_name);

int google_ra_sid_set_pid(struct google_ra *ra, u32 sid, u32 rpid, u32 wpid)
{
	int err = 0;

	if (sid >> ra->sid_width) {
		dev_err(ra->dev, "invalid sid value %x (width: %u)\n", sid, ra->sid_width);
		return -EINVAL;
	}
	if (rpid >> ra->pid_width || wpid >> ra->pid_width) {
		dev_err(ra->dev, "invalid pid value (rpid %x OR wpid %x) width %u\n", rpid, wpid,
			ra->pid_width);
		return -EINVAL;
	}

	err = google_ra_update_pid_tbl(ra, sid, rpid, GOOGLE_RA_PID_OVERRIDE_ENABLED, wpid,
				       GOOGLE_RA_PID_OVERRIDE_ENABLED);
	return err;
}
EXPORT_SYMBOL_GPL(google_ra_sid_set_pid);

int google_ra_sid_get_pid(struct google_ra *ra, u32 sid, u32 *rpid, u32 *wpid)
{
	u32 pid;
	int err;

	if (sid >> ra->sid_width) {
		dev_err(ra->dev, "invalid sid value %x (sid width: %u)\n", sid, ra->sid_width);
		return -EINVAL;
	}

	if (pm_runtime_enabled(ra->dev))
		pm_runtime_get_sync(ra->dev);

	err = regmap_read(ra->sid_pid_tbl, GOOGLE_RA_PID_TBL_OFFSET(sid), &pid);
	if (err) {
		dev_err(ra->dev, "failed to read PID_TBL(sid: %x), err: %d\n", sid, err);
		goto put_ra_exit;
	}

	if (sid % 2 == 0) {
		*rpid = FIELD_GET(GOOGLE_RA_ARPID_0_FIELD, pid);
		*wpid = FIELD_GET(GOOGLE_RA_AWPID_0_FIELD, pid);
	} else {
		*rpid = FIELD_GET(GOOGLE_RA_ARPID_1_FIELD, pid);
		*wpid = FIELD_GET(GOOGLE_RA_AWPID_1_FIELD, pid);
	}

put_ra_exit:
	if (pm_runtime_enabled(ra->dev)) {
		pm_runtime_mark_last_busy(ra->dev);
		pm_runtime_put_autosuspend(ra->dev);
	}
	return err;
}
EXPORT_SYMBOL_GPL(google_ra_sid_get_pid);

static int google_ra_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_ra *ra;
	u32 property;
	int err = 0, num_regs;

	ra = devm_kzalloc(dev, sizeof(*ra), GFP_KERNEL);
	if (!ra)
		return -ENOMEM;

	ra->dev = dev;

	platform_set_drvdata(pdev, ra);

	ra->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ra->base)) {
		dev_err(dev, "unable to map I/O memory\n");
		return PTR_ERR(ra->base);
	}

	ra->full_name = dev->of_node->full_name;

	property = readl_relaxed(ra->base + GOOGLE_RA_PROPERTY);
	ra->sid_width = FIELD_GET(GOOGLE_RA_PROPERTY_SID_WIDTH_FIELD, property);
	ra->pid_width = FIELD_GET(GOOGLE_RA_PROPERTY_PID_WIDTH_FIELD, property);

	ra->sid_pid_tbl = devm_regmap_init_mmio(dev, ra->base, &google_ra_pid_tbl_regmap_config);
	if (IS_ERR(ra->sid_pid_tbl)) {
		dev_err(dev, "failed to create sid_pid_tbl regmap\n");
		return PTR_ERR(ra->sid_pid_tbl);
	}

	num_regs = 1 << (ra->sid_width - 1);
	ra->pid_cache = devm_kcalloc(dev, num_regs, sizeof(struct google_ra_pid_cache), GFP_KERNEL);
	if (!ra->pid_cache)
		return -ENOMEM;

	if (dev->pm_domain) {
		pm_runtime_set_autosuspend_delay(dev, GOOGLE_RA_AUTOSUSPEND_DELAY_MS);
		pm_runtime_use_autosuspend(dev);
		devm_pm_runtime_enable(dev);
	}

	err = google_ra_create_debugfs(ra);
	return err;
}

static void google_ra_driver_remove(struct platform_device *pdev)
{
	struct google_ra *ra = platform_get_drvdata(pdev);

	google_ra_remove_debugfs(ra);
}

static const struct of_device_id google_ra_of_match_table[] = {
	{ .compatible = "google,ra" },
	{},
};
MODULE_DEVICE_TABLE(of, google_ra_of_match_table);

static int google_ra_driver_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct google_ra *ra = platform_get_drvdata(pdev);
	u32 i, num_regs;
	int stride = regmap_get_reg_stride(ra->sid_pid_tbl);

	num_regs = 1 << (ra->sid_width - 1);
	for (i = 0; i < num_regs; i++) {
		if (ra->pid_cache[i].usage)
			regmap_write(ra->sid_pid_tbl, stride * i, ra->pid_cache[i].reg);
	}

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(google_ra_pm_ops, NULL, google_ra_driver_resume, NULL);

static struct platform_driver google_ra_driver = {
	.probe = google_ra_driver_probe,
	.remove = google_ra_driver_remove,
	.driver = {
		.name = "google-ra",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_ra_of_match_table),
		.pm = pm_ptr(&google_ra_pm_ops),
	},
};
module_platform_driver(google_ra_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Realm allocator pid mapping driver");
MODULE_LICENSE("GPL");
