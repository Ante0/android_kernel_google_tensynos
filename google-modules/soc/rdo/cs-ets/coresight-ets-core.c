// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * Description: CoreSight Trace Memory Controller driver
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/coresight.h>
#include <linux/amba/bus.h>
#include <linux/version.h>

#include "coresight-priv.h"
#include "coresight-tmc.h"
#include "coresight-tmc-ets.h"

DEFINE_CORESIGHT_DEVLIST(ets_devs, "tmc_ets");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
int tmc_wait_for_tmcready(struct tmc_drvdata *drvdata)
#else
void tmc_wait_for_tmcready(struct tmc_drvdata *drvdata)
#endif
{
	struct coresight_device *csdev = drvdata->csdev;
	struct csdev_access *csa = &csdev->access;

	/* Ensure formatter, unformatter and hardware fifo are empty */
	if (coresight_timeout(csa, TMC_STS, TMC_STS_TMCREADY_BIT, 1)) {
		dev_err(&csdev->dev,
			"timeout while waiting for TMC to be Ready\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		return -EBUSY;
#endif
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	return 0;
#endif
}

void tmc_flush_and_stop(struct tmc_drvdata *drvdata)
{
	struct coresight_device *csdev = drvdata->csdev;
	struct csdev_access *csa = &csdev->access;
	u32 ffcr;

	ffcr = readl_relaxed(drvdata->base + TMC_FFCR);
	ffcr |= TMC_FFCR_STOP_ON_FLUSH;
	writel_relaxed(ffcr, drvdata->base + TMC_FFCR);
	ffcr |= BIT(TMC_FFCR_FLUSHMAN_BIT);
	writel_relaxed(ffcr, drvdata->base + TMC_FFCR);
	/* Ensure flush completes */
	if (coresight_timeout(csa, TMC_FFCR, TMC_FFCR_FLUSHMAN_BIT, 0)) {
		dev_err(&csdev->dev,
			"timeout while waiting for completion of Manual Flush\n");
	}

	tmc_wait_for_tmcready(drvdata);
}

void tmc_enable_hw(struct tmc_drvdata *drvdata)
{
	writel_relaxed(TMC_CTL_CAPT_EN, drvdata->base + TMC_CTL);
}

void tmc_disable_hw(struct tmc_drvdata *drvdata)
{
	writel_relaxed(0x0, drvdata->base + TMC_CTL);
}

static enum tmc_mem_intf_width tmc_get_memwidth(u32 devid)
{
	enum tmc_mem_intf_width memwidth;

	/*
	 * Excerpt from the TRM:
	 *
	 * DEVID::MEMWIDTH[10:8]
	 * 0x2 Memory interface databus is 32 bits wide.
	 * 0x3 Memory interface databus is 64 bits wide.
	 * 0x4 Memory interface databus is 128 bits wide.
	 * 0x5 Memory interface databus is 256 bits wide.
	 */
	switch (BMVAL(devid, 8, 10)) {
	case 0x2:
		memwidth = TMC_MEM_INTF_WIDTH_32BITS;
		break;
	case 0x3:
		memwidth = TMC_MEM_INTF_WIDTH_64BITS;
		break;
	case 0x4:
		memwidth = TMC_MEM_INTF_WIDTH_128BITS;
		break;
	case 0x5:
		memwidth = TMC_MEM_INTF_WIDTH_256BITS;
		break;
	default:
		memwidth = 0;
	}

	return memwidth;
}

static struct attribute *coresight_tmc_mgmt_attrs[] = {
	coresight_simple_reg32(rsz, TMC_RSZ),
	coresight_simple_reg32(sts, TMC_STS),
	coresight_simple_reg32(trg, TMC_TRG),
	coresight_simple_reg32(ctl, TMC_CTL),
	coresight_simple_reg32(ffsr, TMC_FFSR),
	coresight_simple_reg32(ffcr, TMC_FFCR),
	coresight_simple_reg32(mode, TMC_MODE),
	coresight_simple_reg32(pscr, TMC_PSCR),
	coresight_simple_reg32(devid, CORESIGHT_DEVID),
	coresight_simple_reg32(authstatus, TMC_AUTHSTATUS),
	NULL,
};

static ssize_t trigger_cntr_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val = drvdata->trigger_cntr;

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t trigger_cntr_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->trigger_cntr = val;
	return size;
}
static DEVICE_ATTR_RW(trigger_cntr);

static ssize_t buffer_size_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return sprintf(buf, "%#x\n", drvdata->size);
}

static ssize_t buffer_size_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct tmc_drvdata *drvdata = dev_get_drvdata(dev->parent);

	/* Only permitted for TMC-ETRs */
	if (drvdata->config_type != TMC_CONFIG_TYPE_ETR)
		return -EPERM;

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;
	/* The buffer size should be page aligned */
	if (val & (PAGE_SIZE - 1))
		return -EINVAL;
	drvdata->size = val;
	return size;
}

static DEVICE_ATTR_RW(buffer_size);

static struct attribute *coresight_tmc_attrs[] = {
	&dev_attr_trigger_cntr.attr,
	&dev_attr_buffer_size.attr,
	NULL,
};

static const struct attribute_group coresight_tmc_group = {
	.attrs = coresight_tmc_attrs,
};

static const struct attribute_group coresight_tmc_mgmt_group = {
	.attrs = coresight_tmc_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group *coresight_tmc_groups[] = {
	&coresight_tmc_group,
	&coresight_tmc_mgmt_group,
	NULL,
};

static u32 tmc_etr_get_default_buffer_size(struct device *dev)
{
	u32 size;

	if (fwnode_property_read_u32(dev->fwnode, "arm,buffer-size", &size))
		size = SZ_1M;
	return size;
}

static u32 tmc_etr_get_max_burst_size(struct device *dev)
{
	u32 burst_size;

	if (fwnode_property_read_u32(dev->fwnode, "arm,max-burst-size",
				     &burst_size))
		return TMC_AXICTL_WR_BURST_16;

	/* Only permissible values are 0 to 15 */
	if (burst_size > 0xF)
		burst_size = TMC_AXICTL_WR_BURST_16;

	return burst_size;
}

static int tmc_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret = 0;
	u32 devid;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct tmc_drvdata *drvdata;
	struct resource *res = &adev->res;
	struct coresight_desc desc = { 0 };
	struct coresight_dev_list *dev_list = NULL;
	u8 config_type;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		ret = -ENOMEM;
		goto out;
	}

	dev_set_drvdata(dev, drvdata);

	/* Validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		ret = PTR_ERR(base);
		goto out;
	}

	drvdata->base = base;
	desc.access = CSDEV_ACCESS_IOMEM(base);

	spin_lock_init(&drvdata->spinlock);

	devid = readl_relaxed(drvdata->base + CORESIGHT_DEVID);
	drvdata->config_type = BMVAL(devid, 6, 7);
	drvdata->memwidth = tmc_get_memwidth(devid);
	/* This device is not associated with a session */
	drvdata->pid = -1;

	if (drvdata->config_type == TMC_CONFIG_TYPE_ETR) {
		drvdata->size = tmc_etr_get_default_buffer_size(dev);
		drvdata->max_burst_size = tmc_etr_get_max_burst_size(dev);
	} else {
		drvdata->size = readl_relaxed(drvdata->base + TMC_RSZ) * 4;
	}

	desc.dev = dev;
	desc.groups = coresight_tmc_groups;

	config_type = drvdata->config_type;
	switch (config_type) {
	case TMC_CONFIG_TYPE_ETS:
		desc.type = CORESIGHT_DEV_TYPE_SINK;
		desc.subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_PORT;
		desc.ops = &tmc_ets_cs_ops;
		dev_list = &ets_devs;
		break;
	default:
		pr_err("%s: Unsupported TMC config\n", desc.name);
		ret = -EINVAL;
		goto out;
	}

	desc.name = coresight_alloc_device_name(dev_list, dev);
	if (!desc.name) {
		ret = -ENOMEM;
		goto out;
	}

	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata)) {
		ret = PTR_ERR(pdata);
		goto out;
	}
	adev->dev.platform_data = pdata;
	desc.pdata = pdata;

	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto out;
	}

	pm_runtime_put(&adev->dev);
out:
	return ret;
}

static void tmc_shutdown(struct amba_device *adev)
{
	unsigned long flags;
	struct tmc_drvdata *drvdata = amba_get_drvdata(adev);

	spin_lock_irqsave(&drvdata->spinlock, flags);

	if (drvdata->mode == CS_MODE_DISABLED)
		goto out;

	if (drvdata->config_type == TMC_CONFIG_TYPE_ETS)
		tmc_ets_disable_hw(drvdata);

	/*
	 * We do not care about coresight unregister here unlike remove
	 * callback which is required for making coresight modular since
	 * the system is going down after this.
	 */
out:
	spin_unlock_irqrestore(&drvdata->spinlock, flags);
}

static void tmc_remove(struct amba_device *adev)
{
	struct tmc_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	coresight_unregister(drvdata->csdev);
}

static const struct amba_id tmc_ids[] = {
	CS_AMBA_ID(0x000bb961),
	/* Coresight SoC 600 TMC-ETR/ETS */
	CS_AMBA_ID_DATA(0x000bb9e8, (unsigned long)CORESIGHT_SOC_600_ETR_CAPS),
	/* Coresight SoC 600 TMC-ETB */
	CS_AMBA_ID(0x000bb9e9),
	/* Coresight SoC 600 TMC-ETF */
	CS_AMBA_ID(0x000bb9ea),
	{ 0, 0},
};

MODULE_DEVICE_TABLE(amba, tmc_ids);

static struct amba_driver tmc_driver = {
	.drv = {
		.name   = "coresight-tmc-ets",
		.owner  = THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.probe		= tmc_probe,
	.shutdown	= tmc_shutdown,
	.remove		= tmc_remove,
	.id_table	= tmc_ids,
};

module_amba_driver(tmc_driver);

MODULE_AUTHOR("Pratik Patel <pratikp@codeaurora.org>");
MODULE_DESCRIPTION("Arm CoreSight Trace Memory Controller driver");
MODULE_LICENSE("GPL v2");
