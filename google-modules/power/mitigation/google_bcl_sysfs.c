// SPDX-License-Identifier: GPL-2.0 only
/*
 * google_bcl_sysfs.c Google bcl sysfs driver
 *
 * Copyright (c) 2024 Google LLC
 *
 */
#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <max77779.h>
#include "bcl.h"
#include "core_pmic/core_pmic_defs.h"
#include "ifpmic/ifpmic_defs.h"
#include "ifpmic/max77759/max77759_irq.h"
#include "ifpmic/max77779/max77779_irq.h"
#include "soc/soc_defs.h"
#include "soc/userspace/userspace_bcl_qos.h"
#include "uapi/brownout_stats.h"
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
#include <mailbox/protocols/mba/cpm/common/bcl/bcl_service.h>
#endif

static const char * const batt_irq_names[] = {
	"uvlo1", "uvlo2", "batoilo", "batoilo2"
};

static const char * const concurrent_pwrwarn_irq_names[] = {
	"none", "mmwave", "rffe"
};


static ssize_t safe_emit_bcl_capacity(char *buf, struct bcl_zone *zone)
{
	if (!zone)
		return sysfs_emit(buf, "0\n");
	else
		return sysfs_emit(buf, "%d\n", zone->bcl_stats.capacity);
}

static ssize_t safe_emit_bcl_voltage(char *buf, struct bcl_zone *zone)
{
	if (!zone)
		return sysfs_emit(buf, "0\n");
	else
		return sysfs_emit(buf, "%d\n", zone->bcl_stats.voltage);
}

static ssize_t safe_emit_bcl_time(char *buf, struct bcl_zone *zone)
{
	if (!zone)
		return sysfs_emit(buf, "0\n");
	else
		return sysfs_emit(buf, "%lld\n", zone->bcl_stats._time);
}

static ssize_t batoilo_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[BATOILO1];

	if (!zone)
		return sysfs_emit(buf, "0\n");
	return sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt));
}

static DEVICE_ATTR_RO(batoilo_count);

static ssize_t batoilo2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[BATOILO2];

	if (!zone)
		return sysfs_emit(buf, "0\n");
	return sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt));
}

static DEVICE_ATTR_RO(batoilo2_count);

static ssize_t vdroop2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[UVLO2];

	if (!zone)
		return sysfs_emit(buf, "0\n");
	return sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt));
}

static DEVICE_ATTR_RO(vdroop2_count);

static ssize_t vdroop1_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[UVLO1];

	if (!zone)
		return sysfs_emit(buf, "0\n");
	return sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt));
}

static DEVICE_ATTR_RO(vdroop1_count);

static ssize_t smpl_warn_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_UVLO]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_UVLO]);
}

static DEVICE_ATTR_RO(smpl_warn_count);

static ssize_t ocp_cpu1_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_CPU1]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(ocp_cpu1_count);

static ssize_t ocp_cpu2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_CPU2]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(ocp_cpu2_count);

static ssize_t ocp_tpu_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_TPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(ocp_tpu_count);

static ssize_t ocp_gpu_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_GPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(ocp_gpu_count);

static ssize_t soft_ocp_cpu1_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_count);

static ssize_t soft_ocp_cpu2_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_count);

static ssize_t soft_ocp_tpu_count_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(soft_ocp_tpu_count);

static ssize_t soft_ocp_gpu_count_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(soft_ocp_gpu_count);

static ssize_t batoilo_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[BATOILO1]);
}

static DEVICE_ATTR_RO(batoilo_cap);

static ssize_t batoilo2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[BATOILO2]);
}

static DEVICE_ATTR_RO(batoilo2_cap);

static ssize_t vdroop2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[UVLO2]);
}

static DEVICE_ATTR_RO(vdroop2_cap);

static ssize_t vdroop1_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[UVLO1]);
}

static DEVICE_ATTR_RO(vdroop1_cap);

static ssize_t smpl_warn_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_UVLO]);
}

static DEVICE_ATTR_RO(smpl_warn_cap);

static ssize_t ocp_cpu1_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(ocp_cpu1_cap);

static ssize_t ocp_cpu2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(ocp_cpu2_cap);

static ssize_t ocp_tpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(ocp_tpu_cap);

static ssize_t ocp_gpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(ocp_gpu_cap);

static ssize_t soft_ocp_cpu1_cap_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_cap);

static ssize_t soft_ocp_cpu2_cap_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_cap);

static ssize_t soft_ocp_tpu_cap_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(soft_ocp_tpu_cap);

static ssize_t soft_ocp_gpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(soft_ocp_gpu_cap);

static ssize_t batoilo_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[BATOILO1]);
}

static DEVICE_ATTR_RO(batoilo_volt);

static ssize_t batoilo2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[BATOILO2]);
}

static DEVICE_ATTR_RO(batoilo2_volt);

static ssize_t vdroop2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[UVLO2]);
}

static DEVICE_ATTR_RO(vdroop2_volt);

static ssize_t vdroop1_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[UVLO1]);
}

static DEVICE_ATTR_RO(vdroop1_volt);

static ssize_t smpl_warn_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_UVLO]);
}

static DEVICE_ATTR_RO(smpl_warn_volt);

static ssize_t ocp_cpu1_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(ocp_cpu1_volt);

static ssize_t ocp_cpu2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(ocp_cpu2_volt);

static ssize_t ocp_tpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(ocp_tpu_volt);

static ssize_t ocp_gpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(ocp_gpu_volt);

static ssize_t soft_ocp_cpu1_volt_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_volt);

static ssize_t soft_ocp_cpu2_volt_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_volt);

static ssize_t soft_ocp_tpu_volt_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(soft_ocp_tpu_volt);

static ssize_t soft_ocp_gpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(soft_ocp_gpu_volt);

static ssize_t batoilo_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[BATOILO1]);
}

static DEVICE_ATTR_RO(batoilo_time);

static ssize_t batoilo2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[BATOILO2]);
}

static DEVICE_ATTR_RO(batoilo2_time);

static ssize_t vdroop2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[UVLO2]);
}

static DEVICE_ATTR_RO(vdroop2_time);

static ssize_t vdroop1_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[UVLO1]);
}

static DEVICE_ATTR_RO(vdroop1_time);

static ssize_t smpl_warn_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_UVLO]);
}

static DEVICE_ATTR_RO(smpl_warn_time);

static ssize_t ocp_cpu1_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(ocp_cpu1_time);

static ssize_t ocp_cpu2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(ocp_cpu2_time);

static ssize_t ocp_tpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(ocp_tpu_time);

static ssize_t ocp_gpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(ocp_gpu_time);

static ssize_t soft_ocp_cpu1_time_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu1_time);

static ssize_t soft_ocp_cpu2_time_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

static DEVICE_ATTR_RO(soft_ocp_cpu2_time);

static ssize_t soft_ocp_tpu_time_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

static DEVICE_ATTR_RO(soft_ocp_tpu_time);

static ssize_t soft_ocp_gpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

static DEVICE_ATTR_RO(soft_ocp_gpu_time);

static ssize_t db_settings_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size, enum MPMM_SOURCE src)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int value;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	if (kstrtouint(buf, 16, &value) < 0)
		return -EINVAL;

	if (src != BIG && src != MID)
		return -EINVAL;

	google_set_db(bcl_dev, value, src);

	return size;
}

static ssize_t db_settings_show(struct device *dev, struct device_attribute *attr,
				char *buf, enum MPMM_SOURCE src)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	if ((!bcl_dev->sysreg_cpucl0) || (src == LITTLE) || (src == MPMMEN))
		return -EIO;

	return sysfs_emit(buf, "%#x\n", google_get_db(bcl_dev, src));
}

static ssize_t mid_db_settings_store(struct device *dev,
				     struct device_attribute *attr, const char *buf, size_t size)
{
	return db_settings_store(dev, attr, buf, size, MID);
}

static ssize_t mid_db_settings_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return db_settings_show(dev, attr, buf, MID);
}

static DEVICE_ATTR_RW(mid_db_settings);

static ssize_t big_db_settings_store(struct device *dev,
				     struct device_attribute *attr, const char *buf, size_t size)
{
	return db_settings_store(dev, attr, buf, size, BIG);
}

static ssize_t big_db_settings_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return db_settings_show(dev, attr, buf, BIG);
}

static DEVICE_ATTR_RW(big_db_settings);

static ssize_t enable_sw_mitigation_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", READ_ONCE(bcl_dev->sw_mitigation_enabled));
}

static ssize_t enable_sw_mitigation_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_sw_mitigation(bcl_dev, buf, size);
}

static DEVICE_ATTR_RW(enable_sw_mitigation);

static ssize_t enable_hw_mitigation_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n",
			  READ_ONCE(bcl_dev->hw_mitigation_enabled));
}

static ssize_t enable_hw_mitigation_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_hw_mitigation(bcl_dev, buf, size);
}

static DEVICE_ATTR_RW(enable_hw_mitigation);

static ssize_t enable_rffe_mitigation_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->rffe_mitigation_enable);
}

static ssize_t enable_rffe_mitigation_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	if (bcl_dev->rffe_mitigation_enable == value)
		return size;

	bcl_dev->rffe_mitigation_enable = value;
	return size;
}

static DEVICE_ATTR_RW(enable_rffe_mitigation);

static ssize_t main_offsrc1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->main_offsrc1);
}

static DEVICE_ATTR_RO(main_offsrc1);

static ssize_t main_offsrc2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->main_offsrc2);
}

static DEVICE_ATTR_RO(main_offsrc2);

static ssize_t sub_offsrc1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->sub_offsrc1);
}

static DEVICE_ATTR_RO(sub_offsrc1);

static ssize_t sub_offsrc2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->sub_offsrc2);
}

static DEVICE_ATTR_RO(sub_offsrc2);

static ssize_t evt_cnt_uvlo1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.uvlo1);
}

static DEVICE_ATTR_RO(evt_cnt_uvlo1);

static ssize_t evt_cnt_uvlo2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.uvlo2);
}

static DEVICE_ATTR_RO(evt_cnt_uvlo2);

static ssize_t evt_cnt_batoilo1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.batoilo1);
}

static DEVICE_ATTR_RO(evt_cnt_batoilo1);

static ssize_t evt_cnt_batoilo2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.batoilo2);
}

static DEVICE_ATTR_RO(evt_cnt_batoilo2);

static ssize_t evt_cnt_latest_uvlo1_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.uvlo1);
}

static DEVICE_ATTR_RO(evt_cnt_latest_uvlo1);

static ssize_t evt_cnt_latest_uvlo2_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.uvlo2);
}

static DEVICE_ATTR_RO(evt_cnt_latest_uvlo2);

static ssize_t evt_cnt_latest_batoilo1_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.batoilo1);
}

static DEVICE_ATTR_RO(evt_cnt_latest_batoilo1);

static ssize_t evt_cnt_latest_batoilo2_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.batoilo2);
}

static DEVICE_ATTR_RO(evt_cnt_latest_batoilo2);

static ssize_t pwronsrc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->pwronsrc);
}

static DEVICE_ATTR_RO(pwronsrc);

static ssize_t last_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (bcl_dev->ifpmic != MAX77779)
		return -ENODEV;

	return sysfs_emit(buf, "%#x\n", bcl_dev->last_current);
}
static DEVICE_ATTR_RO(last_current);

static ssize_t vimon_buff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int idx, ret;
	uint16_t rdback;
	ssize_t count = 0;

	if (bcl_dev->ifpmic != MAX77779)
		return -ENODEV;

	ret = bcl_dev->ifpmic_ops->vimon_read(bcl_dev->ifpmic_irq_dev);
	if (ret < 0)
		return -ENODEV;

	for (idx = 0; idx < ret / VIMON_BYTES_PER_ENTRY; idx++) {
		rdback = bcl_dev->vimon_intf.data[idx];
		count += sysfs_emit_at(buf, count, "%#x\n", rdback);
	}

	return count;
}
static DEVICE_ATTR_RO(vimon_buff);

static ssize_t ready_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", READ_ONCE(bcl_dev->initialized));
}
static DEVICE_ATTR_RO(ready);

static ssize_t ifpmic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%s\n", bcl_dev->ifpmic == MAX77779 ? "max77779" : "max77759");
}
static DEVICE_ATTR_RO(ifpmic);

static ssize_t bcl_version_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", BCL_VERSION);
}
static DEVICE_ATTR_RO(bcl_version);

static struct attribute *instr_attrs[] = {
	&dev_attr_mid_db_settings.attr,
	&dev_attr_big_db_settings.attr,
	&dev_attr_enable_sw_mitigation.attr,
	&dev_attr_enable_hw_mitigation.attr,
	&dev_attr_enable_rffe_mitigation.attr,
	&dev_attr_main_offsrc1.attr,
	&dev_attr_main_offsrc2.attr,
	&dev_attr_sub_offsrc1.attr,
	&dev_attr_sub_offsrc2.attr,
	&dev_attr_evt_cnt_uvlo1.attr,
	&dev_attr_evt_cnt_uvlo2.attr,
	&dev_attr_evt_cnt_batoilo1.attr,
	&dev_attr_evt_cnt_batoilo2.attr,
	&dev_attr_evt_cnt_latest_uvlo1.attr,
	&dev_attr_evt_cnt_latest_uvlo2.attr,
	&dev_attr_evt_cnt_latest_batoilo1.attr,
	&dev_attr_evt_cnt_latest_batoilo2.attr,
	&dev_attr_pwronsrc.attr,
	&dev_attr_last_current.attr,
	&dev_attr_vimon_buff.attr,
	&dev_attr_ready.attr,
	&dev_attr_ifpmic.attr,
	&dev_attr_bcl_version.attr,
	NULL,
};

static const struct attribute_group instr_group = {
	.attrs = instr_attrs,
	.name = "instruction",
};

static ssize_t uvlo1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int uvlo1_lvl;
	int ret;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO1])
		return -EIO;
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->get_uvlo1(bcl_dev->ifpmic_irq_dev, &uvlo1_lvl);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%dmV\n", uvlo1_lvl);
}

static ssize_t uvlo1_lvl_store(struct device *dev,
			       struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO1])
		return -EIO;
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->set_uvlo1(bcl_dev->ifpmic_irq_dev, value);
	if (ret)
		return ret;

	return size;

}

static DEVICE_ATTR_RW(uvlo1_lvl);

static ssize_t uvlo2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int uvlo2_lvl;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO2])
		return sysfs_emit(buf, "disabled\n");
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->get_uvlo2(bcl_dev->ifpmic_irq_dev, &uvlo2_lvl);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%dmV\n", uvlo2_lvl);
}

static ssize_t uvlo2_lvl_store(struct device *dev,
			       struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[UVLO2]) {
		dev_err(bcl_dev->device, "UVLO2 is disabled\n");
		return -EIO;
	}
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->set_uvlo2(bcl_dev->ifpmic_irq_dev, value);
	if (ret)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(uvlo2_lvl);

static int check_batoilo_threshold_range(struct bcl_device *bcl_dev, int idx, int value)
{

	int batoilo_lower_limit, batoilo_upper_limit;

	if (idx != BATOILO1 && idx != BATOILO2)
		return -EINVAL;

	if (!bcl_dev->zone[idx])
		return -EIO;

	if (idx == BATOILO1) {
		batoilo_lower_limit = bcl_dev->batt_irq_conf1.batoilo_lower_limit;
		batoilo_upper_limit = bcl_dev->batt_irq_conf1.batoilo_upper_limit;
	} else {
		batoilo_lower_limit = bcl_dev->batt_irq_conf2.batoilo_lower_limit;
		batoilo_upper_limit = bcl_dev->batt_irq_conf2.batoilo_upper_limit;
	}

	if (value < batoilo_lower_limit || value > batoilo_upper_limit) {
		dev_err(bcl_dev->device, "zone %d: %d outside of range %d - %d mA.",
			idx,
			value,
			batoilo_lower_limit, batoilo_upper_limit);
		return -EINVAL;
	}

	return 0;
}

static ssize_t batoilo1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int lvl;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[BATOILO1])
		return -EIO;
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->get_oilo1(bcl_dev->ifpmic_irq_dev, &lvl);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%umA\n", lvl);
}

static ssize_t batoilo1_lvl_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->set_oilo1(bcl_dev->ifpmic_irq_dev, value);
	if (ret)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo1_lvl);

static ssize_t batoilo2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int batoilo2_lvl;

	if (!bcl_dev)
		return -EIO;
	if (!bcl_dev->zone[BATOILO2])
		return -EIO;
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->get_oilo2(bcl_dev->ifpmic_irq_dev, &batoilo2_lvl);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%umA\n", batoilo2_lvl);
}

static ssize_t batoilo2_lvl_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev->irq_pmic_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->set_oilo2(bcl_dev->ifpmic_irq_dev, value);
	if (ret)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo2_lvl);

#define BATOILO_TYPE_FUNC(num, type)                                          \
	static ssize_t batoilo##num##_##type##_lvl_store(                     \
		struct device *dev, struct device_attribute *attr,            \
		const char *buf, size_t size)                                 \
	{                                                                     \
		struct platform_device *pdev =                                \
			container_of(dev, struct platform_device, dev);       \
		struct bcl_device *bcl_dev = platform_get_drvdata(pdev);      \
		unsigned int value;                                           \
		int ret;                                                      \
\
		ret = kstrtou32(buf, 10, &value);                             \
		if (ret)                                                      \
			return ret;                                           \
\
		ret = check_batoilo_threshold_range(bcl_dev, BATOILO##num,    \
						    value);                   \
		if (ret)                                                      \
			return ret;                                           \
\
		bcl_dev->batt_irq_conf##num.batoilo_##type##_trig_lvl =       \
			(value -                                              \
			 bcl_dev->batt_irq_conf##num.batoilo_lower_limit) /   \
			BO_STEP;                                              \
\
		return size;                                                  \
	}                                                                     \
\
	static ssize_t batoilo##num##_##type##_lvl_show(                      \
		struct device *dev, struct device_attribute *attr, char *buf) \
	{                                                                     \
		struct platform_device *pdev =                                \
			container_of(dev, struct platform_device, dev);       \
		struct bcl_device *bcl_dev = platform_get_drvdata(pdev);      \
		unsigned int lvl_mA;                                          \
\
		if (!bcl_dev->zone[BATOILO##num])                             \
			return -EIO;                                          \
		if (!bcl_dev->intf_pmic_dev)                                  \
			return -EBUSY;                                        \
\
		lvl_mA = BO_STEP * bcl_dev->batt_irq_conf##num                \
					   .batoilo_##type##_trig_lvl +       \
			 bcl_dev->batt_irq_conf##num.batoilo_lower_limit;     \
\
		return sysfs_emit(buf, "%umA\n", lvl_mA);                     \
	} \
\
	static DEVICE_ATTR_RW(batoilo##num##_##type##_lvl)

BATOILO_TYPE_FUNC(1, usb);
BATOILO_TYPE_FUNC(1, wlc);
BATOILO_TYPE_FUNC(1, otg);
BATOILO_TYPE_FUNC(2, usb);
BATOILO_TYPE_FUNC(2, wlc);
BATOILO_TYPE_FUNC(2, otg);

static ssize_t smpl_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int smpl_warn_lvl;

	if (!bcl_dev)
		return -EIO;
	if (core_pmic_main_read_uvlo(bcl_dev, &smpl_warn_lvl) < 0)
		return -EBUSY;
	return sysfs_emit(buf, "%umV\n", smpl_warn_lvl);
}

static ssize_t smpl_lvl_store(struct device *dev,
			      struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int val;
	int ret;

	ret = kstrtou32(buf, 10, &val);
	if (ret)
		return ret;

	if (!bcl_dev)
		return -EIO;
	return core_pmic_main_store_uvlo(bcl_dev, val, size);
}

static DEVICE_ATTR_RW(smpl_lvl);

static ssize_t ocp_cpu1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, PRE_OCP_CPU1,
				       CPU1_UPPER_LIMIT, CPU1_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_cpu1_lvl_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (core_pmic_main_set_ocp_lvl(bcl_dev, value, PRE_OCP_CPU1,
				       CPU1_LOWER_LIMIT, CPU1_UPPER_LIMIT,
				       CPU1_STEP) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(ocp_cpu1_lvl);

static ssize_t ocp_cpu2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, PRE_OCP_CPU2,
				       CPU2_UPPER_LIMIT, CPU2_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_cpu2_lvl_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (core_pmic_main_set_ocp_lvl(bcl_dev, value, PRE_OCP_CPU2,
				       CPU2_LOWER_LIMIT, CPU2_UPPER_LIMIT,
				       CPU2_STEP) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(ocp_cpu2_lvl);

static ssize_t ocp_tpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, PRE_OCP_TPU,
				       TPU_UPPER_LIMIT, TPU_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_tpu_lvl_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (core_pmic_main_set_ocp_lvl(bcl_dev, value, PRE_OCP_TPU,
				       TPU_LOWER_LIMIT, TPU_UPPER_LIMIT,
				       TPU_STEP) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(ocp_tpu_lvl);

static ssize_t ocp_gpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)) {
		if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, PRE_OCP_GPU,
					       GPU_UPPER_LIMIT, GPU_STEP) < 0)
			return -EINVAL;
	} else {
		if (core_pmic_sub_get_ocp_lvl(bcl_dev, &val, PRE_OCP_GPU,
					       GPU_UPPER_LIMIT, GPU_STEP) < 0)
			return -EINVAL;
	}

	return sysfs_emit(buf, "%llumA\n", val);

}

static ssize_t ocp_gpu_lvl_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)) {
		if (core_pmic_main_set_ocp_lvl(bcl_dev, value, PRE_OCP_GPU,
					       GPU_LOWER_LIMIT, GPU_UPPER_LIMIT,
					       GPU_STEP) < 0)
			return -EINVAL;
	} else {
		if (core_pmic_sub_set_ocp_lvl(bcl_dev, value, PRE_OCP_GPU,
					      GPU_LOWER_LIMIT, GPU_UPPER_LIMIT,
					      GPU_STEP) < 0)
			return -EINVAL;
	}

	return size;
}

static DEVICE_ATTR_RW(ocp_gpu_lvl);

static ssize_t soft_ocp_cpu1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, SOFT_PRE_OCP_CPU1,
				       CPU1_UPPER_LIMIT, CPU1_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);
}

static ssize_t soft_ocp_cpu1_lvl_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (core_pmic_main_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_CPU1,
				       CPU1_LOWER_LIMIT, CPU1_UPPER_LIMIT,
				       CPU1_STEP) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(soft_ocp_cpu1_lvl);

static ssize_t soft_ocp_cpu2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, SOFT_PRE_OCP_CPU2,
				       CPU2_UPPER_LIMIT, CPU2_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);
}

static ssize_t soft_ocp_cpu2_lvl_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (core_pmic_main_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_CPU2,
				       CPU2_LOWER_LIMIT, CPU2_UPPER_LIMIT,
				       CPU2_STEP) < 0)
		return -EINVAL;
	return size;
}

static DEVICE_ATTR_RW(soft_ocp_cpu2_lvl);

static ssize_t soft_ocp_tpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, SOFT_PRE_OCP_TPU,
				       TPU_UPPER_LIMIT, TPU_STEP) < 0)
		return -EINVAL;
	return sysfs_emit(buf, "%llumA\n", val);
}

static ssize_t soft_ocp_tpu_lvl_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	if (core_pmic_main_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_TPU,
				       TPU_SOFT_LOWER_LIMIT, TPU_SOFT_UPPER_LIMIT,
				       TPU_STEP) < 0)
		return -EINVAL;
#else
	if (core_pmic_main_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_TPU,
				       TPU_LOWER_LIMIT, TPU_UPPER_LIMIT,
				       TPU_STEP) < 0)
		return -EINVAL;
#endif
	return size;
}

static DEVICE_ATTR_RW(soft_ocp_tpu_lvl);

static ssize_t soft_ocp_gpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)) {
		if (core_pmic_main_get_ocp_lvl(bcl_dev, &val, SOFT_PRE_OCP_GPU,
					       GPU_UPPER_LIMIT, GPU_STEP) < 0)
			return -EINVAL;
	} else {
		if (core_pmic_sub_get_ocp_lvl(bcl_dev, &val, SOFT_PRE_OCP_GPU,
					       GPU_UPPER_LIMIT, GPU_STEP) < 0)
			return -EINVAL;
	}

	return sysfs_emit(buf, "%llumA\n", val);
}

static ssize_t soft_ocp_gpu_lvl_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)) {
		if (core_pmic_main_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_GPU,
					       GPU_LOWER_LIMIT, GPU_UPPER_LIMIT,
					       GPU_STEP) < 0)
			return -EINVAL;
	} else {
		if (core_pmic_sub_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_GPU,
					       GPU_LOWER_LIMIT, GPU_UPPER_LIMIT,
					       GPU_STEP) < 0)
			return -EINVAL;
	}

	return size;
}

static DEVICE_ATTR_RW(soft_ocp_gpu_lvl);

static struct attribute *triggered_lvl_attrs[] = {
	&dev_attr_uvlo1_lvl.attr,
	&dev_attr_uvlo2_lvl.attr,
	&dev_attr_batoilo1_lvl.attr,
	&dev_attr_batoilo1_usb_lvl.attr,
	&dev_attr_batoilo1_wlc_lvl.attr,
	&dev_attr_batoilo1_otg_lvl.attr,
	&dev_attr_batoilo2_lvl.attr,
	&dev_attr_batoilo2_usb_lvl.attr,
	&dev_attr_batoilo2_wlc_lvl.attr,
	&dev_attr_batoilo2_otg_lvl.attr,
	&dev_attr_smpl_lvl.attr,
	&dev_attr_ocp_cpu1_lvl.attr,
	&dev_attr_ocp_cpu2_lvl.attr,
	&dev_attr_ocp_tpu_lvl.attr,
	&dev_attr_ocp_gpu_lvl.attr,
	&dev_attr_soft_ocp_cpu1_lvl.attr,
	&dev_attr_soft_ocp_cpu2_lvl.attr,
	&dev_attr_soft_ocp_tpu_lvl.attr,
	&dev_attr_soft_ocp_gpu_lvl.attr,
	NULL,
};

static const struct attribute_group triggered_lvl_group = {
	.attrs = triggered_lvl_attrs,
	.name = "triggered_lvl",
};

#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188) || IS_ENABLED(CONFIG_SOC_ZUMAPRO)
enum DEBOUNCE_TYPE {
	deglitch,
	release
};

static int read_debounce(struct device *dev, int idx, enum DEBOUNCE_TYPE type,
						 bool is_int)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u8 val, regval, reg_addr;

	if (!bcl_dev->intf_pmic_dev)
		return -EBUSY;

	switch (idx) {
	case UVLO1:
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO1_CNFG_1, &regval) < 0)
			return -EIO;
		if (type == deglitch) {
			val = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_det_get(regval);
			bcl_dev->batt_irq_conf1.uvlo_det = val;
		} else {
			val = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_rel_get(regval);
			bcl_dev->batt_irq_conf1.uvlo_rel = val;
		}
		break;
	case UVLO2:
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO2_CNFG_1, &regval) < 0)
			return -EIO;
		if (type == deglitch) {
			val = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_det_get(regval);
			bcl_dev->batt_irq_conf2.uvlo_det = val;
		} else {
			val = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_rel_get(regval);
			bcl_dev->batt_irq_conf2.uvlo_rel = val;
		}
		break;
	case BATOILO1:
		reg_addr = is_int ? MAX77779_BAT_OILO1_CNFG_2 : MAX77779_BAT_OILO1_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		if (is_int) {
			if (type == deglitch) {
				val = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_det_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_int_det = val;
			} else {
				val = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_rel_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_int_rel = val;
			}
		} else {
			if (type == deglitch) {
				val = _max77779_bat_oilo1_cnfg_1_bat_oilo1_det_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_det = val;
			} else {
				val = _max77779_bat_oilo1_cnfg_1_bat_oilo1_rel_get(regval);
				bcl_dev->batt_irq_conf1.batoilo_rel = val;
			}
		}
		break;
	case BATOILO2:
		reg_addr = is_int ? MAX77779_BAT_OILO2_CNFG_2 : MAX77779_BAT_OILO2_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		if (is_int) {
			if (type == deglitch) {
				val = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_det_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_int_det = val;
			} else {
				val = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_rel_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_int_rel = val;
			}
		} else {
			if (type == deglitch) {
				val = _max77779_bat_oilo2_cnfg_1_bat_oilo2_det_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_det = val;
			} else {
				val = _max77779_bat_oilo2_cnfg_1_bat_oilo2_rel_get(regval);
				bcl_dev->batt_irq_conf2.batoilo_rel = val;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	return val;
}

static int write_debounce(struct device *dev, const char *buf, int idx,
						  enum DEBOUNCE_TYPE type, bool is_int)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u8 val, regval, *irq_conf_val, reg_addr;
	int ret;

	ret = kstrtou8(buf, 16, &val);
	if (ret)
		return ret;
	if (!bcl_dev->intf_pmic_dev)
		return -EBUSY;

	switch (idx) {
	case UVLO1:
		if ((type == deglitch && val > UVLO_DET_MAX) ||
		    (type == release && val > UVLO_REL_MAX))
			return -EINVAL;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO1_CNFG_1, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[UVLO1]->bcl_irq);
		if (type == deglitch) {
			regval = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_det_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf1.uvlo_det;
		} else {
			regval = _max77779_sys_uvlo1_cnfg_1_sys_uvlo1_rel_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf1.uvlo_rel;
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							MAX77779_SYS_UVLO1_CNFG_1, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[UVLO1]->bcl_irq);
		break;
	case UVLO2:
		if ((type == deglitch && val > UVLO_DET_MAX) ||
		    (type == release && val > UVLO_REL_MAX))
			return -EINVAL;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 MAX77779_SYS_UVLO2_CNFG_1, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[UVLO2]->bcl_irq);
		if (type == deglitch) {
			regval = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_det_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf2.uvlo_det;
		} else {
			regval = _max77779_sys_uvlo2_cnfg_1_sys_uvlo2_rel_set(regval, val);
			irq_conf_val = &bcl_dev->batt_irq_conf2.uvlo_rel;
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							MAX77779_SYS_UVLO2_CNFG_1, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[UVLO2]->bcl_irq);
		break;
	case BATOILO1:
		if ((type == deglitch && val > OILO_DET_MAX) ||
		    (type == release && val > OILO_REL_MAX))
			return -EINVAL;
		reg_addr = is_int ? MAX77779_BAT_OILO1_CNFG_2 : MAX77779_BAT_OILO1_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[BATOILO1]->bcl_irq);
		if (is_int) {
			if (type == deglitch) {
				regval = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_det_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_int_det;
			} else {
				regval = _max77779_bat_oilo1_cnfg_2_bat_oilo1_int_rel_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_int_rel;
			}
		} else {
			if (type == deglitch) {
				regval = _max77779_bat_oilo1_cnfg_1_bat_oilo1_det_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_det;
			} else {
				regval = _max77779_bat_oilo1_cnfg_1_bat_oilo1_rel_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf1.batoilo_rel;
			}
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							reg_addr, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[BATOILO1]->bcl_irq);
		break;
	case BATOILO2:
		if ((type == deglitch && val > OILO_DET_MAX) ||
		    (type == release && val > OILO_REL_MAX))
			return -EINVAL;
		reg_addr = is_int ? MAX77779_BAT_OILO2_CNFG_2 : MAX77779_BAT_OILO2_CNFG_1;
		if (max77779_external_chg_reg_read(bcl_dev->intf_pmic_dev,
							 reg_addr, &regval) < 0)
			return -EIO;

		disable_irq(bcl_dev->zone[BATOILO2]->bcl_irq);
		if (is_int) {
			if (type == deglitch) {
				regval = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_det_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_int_det;
			} else {
				regval = _max77779_bat_oilo2_cnfg_2_bat_oilo2_int_rel_set(regval,
											  val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_int_rel;
			}
		} else {
			if (type == deglitch) {
				regval = _max77779_bat_oilo2_cnfg_1_bat_oilo2_det_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_det;
			} else {
				regval = _max77779_bat_oilo2_cnfg_1_bat_oilo2_rel_set(regval, val);
				irq_conf_val = &bcl_dev->batt_irq_conf2.batoilo_rel;
			}
		}
		if (max77779_external_chg_reg_write(bcl_dev->intf_pmic_dev,
							reg_addr, regval) == 0)
			*irq_conf_val = val;
		enable_irq(bcl_dev->zone[BATOILO2]->bcl_irq);
		break;
	default:
		return -EINVAL;
	}

	if (ret < 0)
		return -EIO;

	return 0;
}

static ssize_t uvlo1_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO1, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t uvlo1_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO1, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(uvlo1_det);

static ssize_t uvlo2_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO2, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);

}

static ssize_t uvlo2_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO2, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(uvlo2_det);

static ssize_t batoilo1_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo1_det);

static ssize_t batoilo2_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, deglitch, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, deglitch, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo2_det);

static ssize_t batoilo1_int_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, deglitch, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_int_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, deglitch, true);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo1_int_det);

static ssize_t batoilo2_int_det_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, deglitch, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_int_det_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, deglitch, true);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo2_int_det);

static ssize_t uvlo1_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO1, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t uvlo1_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO1, release, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(uvlo1_rel);

static ssize_t uvlo2_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, UVLO2, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t uvlo2_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, UVLO2, release, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(uvlo2_rel);

static ssize_t batoilo1_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, release, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo1_rel);

static ssize_t batoilo2_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, release, false);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, release, false);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo2_rel);

static ssize_t batoilo1_int_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO1, release, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo1_int_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO1, release, true);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo1_int_rel);

static ssize_t batoilo2_int_rel_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = read_debounce(dev, BATOILO2, release, true);

	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%#x\n", ret);
}

static ssize_t batoilo2_int_rel_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	int ret = write_debounce(dev, buf, BATOILO2, release, true);

	if (ret < 0)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(batoilo2_int_rel);

static struct attribute *debounce_time_attrs[] = {
	&dev_attr_uvlo1_det.attr,
	&dev_attr_uvlo2_det.attr,
	&dev_attr_batoilo1_det.attr,
	&dev_attr_batoilo2_det.attr,
	&dev_attr_batoilo1_int_det.attr,
	&dev_attr_batoilo2_int_det.attr,
	&dev_attr_uvlo1_rel.attr,
	&dev_attr_uvlo2_rel.attr,
	&dev_attr_batoilo1_rel.attr,
	&dev_attr_batoilo2_rel.attr,
	&dev_attr_batoilo1_int_rel.attr,
	&dev_attr_batoilo2_int_rel.attr,
	NULL,
};

static const struct attribute_group debounce_time_group = {
	.attrs = debounce_time_attrs,
	.name = "debounce_time",
};
#endif

static ssize_t clk_div_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	return get_clk_div(bcl_dev, idx, buf);
}

static ssize_t clk_div_store(struct bcl_device *bcl_dev, int idx,
			     const char *buf, size_t size)
{
	return set_clk_div(bcl_dev, idx, buf, size);
}

#define GEN_CLK_DIV(core)\
static ssize_t clk_##core##_div_show(struct device *dev,\
				     struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return clk_div_show(bcl_dev, core, buf);\
} \
\
static ssize_t clk_##core##_div_store(struct device *dev, struct device_attribute *attr,\
				      const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return clk_div_store(bcl_dev, core, buf, size);\
} \
static DEVICE_ATTR_RW(clk_##core##_div)


#define GEN_CLK_STATS(core)\
static ssize_t clk_##core##_stats_show(struct device *dev,\
				       struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_clk_stats(bcl_dev, core, buf);\
} \
\
static DEVICE_ATTR_RO(clk_##core##_stats)

GEN_CLK_DIV(CPU0);

#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)

GEN_CLK_DIV(CPU1A);
GEN_CLK_DIV(CPU1B);

#else

GEN_CLK_DIV(CPU1);

#endif

GEN_CLK_DIV(CPU2);
GEN_CLK_DIV(GPU);
GEN_CLK_DIV(TPU);
GEN_CLK_DIV(AUR);

static struct attribute *clock_div_attrs[] = {
	&dev_attr_clk_CPU0_div.attr,
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
	&dev_attr_clk_CPU1A_div.attr,
	&dev_attr_clk_CPU1B_div.attr,
#else
	&dev_attr_clk_CPU1_div.attr,
#endif
	&dev_attr_clk_CPU2_div.attr,
	&dev_attr_clk_GPU_div.attr,
	&dev_attr_clk_TPU_div.attr,
	&dev_attr_clk_AUR_div.attr,
	NULL,
};

static const struct attribute_group clock_div_group = {
	.attrs = clock_div_attrs,
	.name = "clock_div",
};

static ssize_t clk_ratio_show(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx, char *buf,
			      int sub_idx)
{
	return get_clk_ratio(bcl_dev, idx, buf, sub_idx);
}

static ssize_t clk_ratio_store(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
			       const char *buf, size_t size, int sub_idx)
{
	return set_clk_ratio(bcl_dev, idx, buf, size, sub_idx);
}

#define GEN_CLK_RATIO(core, div)\
static ssize_t clk_##core##_##div##_ratio_show(struct device *dev,\
					       struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return clk_ratio_show(bcl_dev, div, buf, core);\
} \
\
static ssize_t clk_##core##_##div##_ratio_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return clk_ratio_store(bcl_dev, div, buf, size, core);\
} \
static DEVICE_ATTR_RW(clk_##core##_##div##_ratio)

GEN_CLK_RATIO(CPU0, light);

#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)

GEN_CLK_RATIO(CPU0, heavy);
GEN_CLK_RATIO(CPU1A, heavy);
GEN_CLK_RATIO(CPU1A, light);
GEN_CLK_RATIO(CPU1B, heavy);
GEN_CLK_RATIO(CPU1B, light);
GEN_CLK_RATIO(AUR, heavy);
GEN_CLK_RATIO(AUR, light);

#else

GEN_CLK_RATIO(CPU1, light);
GEN_CLK_RATIO(CPU1, heavy);

#endif

GEN_CLK_RATIO(CPU2, light);
GEN_CLK_RATIO(CPU2, heavy);
GEN_CLK_RATIO(TPU, light);
GEN_CLK_RATIO(TPU, heavy);
GEN_CLK_RATIO(GPU, light);
GEN_CLK_RATIO(GPU, heavy);

static struct attribute *clock_ratio_attrs[] = {
	&dev_attr_clk_CPU0_light_ratio.attr,
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
	&dev_attr_clk_CPU0_heavy_ratio.attr,
	&dev_attr_clk_CPU1A_heavy_ratio.attr,
	&dev_attr_clk_CPU1A_light_ratio.attr,
	&dev_attr_clk_CPU1B_heavy_ratio.attr,
	&dev_attr_clk_CPU1B_light_ratio.attr,
	&dev_attr_clk_AUR_heavy_ratio.attr,
	&dev_attr_clk_AUR_light_ratio.attr,
#else
	&dev_attr_clk_CPU1_heavy_ratio.attr,
	&dev_attr_clk_CPU1_light_ratio.attr,
#endif
	&dev_attr_clk_CPU2_heavy_ratio.attr,
	&dev_attr_clk_CPU2_light_ratio.attr,
	&dev_attr_clk_GPU_heavy_ratio.attr,
	&dev_attr_clk_GPU_light_ratio.attr,
	&dev_attr_clk_TPU_heavy_ratio.attr,
	&dev_attr_clk_TPU_light_ratio.attr,
	NULL,
};

static const struct attribute_group clock_ratio_group = {
	.attrs = clock_ratio_attrs,
	.name = "clock_ratio",
};

GEN_CLK_STATS(CPU0);

#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)

GEN_CLK_STATS(CPU1A);
GEN_CLK_STATS(CPU1B);

#else

GEN_CLK_STATS(CPU1);

#endif

GEN_CLK_STATS(CPU2);
GEN_CLK_STATS(TPU);
GEN_CLK_STATS(GPU);
GEN_CLK_STATS(AUR);

static ssize_t last_triggered_cnt(struct bcl_zone *zone, char *buf, int mode)
{
	if (!zone)
		return -ENODEV;
	if (mode >= 0 && mode < MAX_MITIGATION_MODE)
		return sysfs_emit(buf, "%d\n",
				  atomic_read(&zone->last_triggered.triggered_cnt[mode]));
	return sysfs_emit(buf, "0\n");
}

static ssize_t last_triggered_time(struct bcl_zone *zone, char *buf, int mode)
{
	if (!zone)
		return -ENODEV;
	if (mode >= 0 && mode < MAX_MITIGATION_MODE)
		return sysfs_emit(buf, "%lld\n", zone->last_triggered.triggered_time[mode]);
	return sysfs_emit(buf, "0\n");
}

static ssize_t last_triggered_uvlo1_heavy_cnt_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_heavy_cnt);

static ssize_t last_triggered_uvlo1_medium_cnt_show(struct device *dev,
							     struct device_attribute *attr,
							     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_medium_cnt);

static ssize_t last_triggered_uvlo1_light_cnt_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_light_cnt);

static ssize_t last_triggered_uvlo1_start_cnt_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_start_cnt);

static ssize_t last_triggered_uvlo1_heavy_time_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_heavy_time);

static ssize_t last_triggered_uvlo1_medium_time_show(struct device *dev,
							      struct device_attribute *attr,
							      char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_medium_time);

static ssize_t last_triggered_uvlo1_light_time_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_light_time);

static ssize_t last_triggered_uvlo1_start_time_show(struct device *dev,
						    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_uvlo1_start_time);

static ssize_t last_triggered_uvlo2_heavy_cnt_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_heavy_cnt);

static ssize_t last_triggered_uvlo2_medium_cnt_show(struct device *dev,
							     struct device_attribute *attr,
							     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_medium_cnt);

static ssize_t last_triggered_uvlo2_light_cnt_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_light_cnt);

static ssize_t last_triggered_uvlo2_start_cnt_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_start_cnt);

static ssize_t last_triggered_uvlo2_heavy_time_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_heavy_time);

static ssize_t last_triggered_uvlo2_medium_time_show(struct device *dev,
							      struct device_attribute *attr,
							      char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_medium_time);

static ssize_t last_triggered_uvlo2_light_time_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_light_time);

static ssize_t last_triggered_uvlo2_start_time_show(struct device *dev,
						    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_uvlo2_start_time);

static ssize_t last_triggered_batoilo2_heavy_cnt_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_heavy_cnt);

static ssize_t last_triggered_batoilo2_medium_cnt_show(struct device *dev,
								struct device_attribute *attr,
								char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_medium_cnt);

static ssize_t last_triggered_batoilo2_light_cnt_show(struct device *dev,
							  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_light_cnt);

static ssize_t last_triggered_batoilo2_start_cnt_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_start_cnt);

static ssize_t last_triggered_batoilo2_heavy_time_show(struct device *dev,
							  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_heavy_time);

static ssize_t last_triggered_batoilo2_medium_time_show(struct device *dev,
								 struct device_attribute *attr,
								 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_medium_time);

static ssize_t last_triggered_batoilo2_light_time_show(struct device *dev,
							   struct device_attribute *attr,
							   char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_light_time);

static ssize_t last_triggered_batoilo2_start_time_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_batoilo2_start_time);

static ssize_t last_triggered_batoilo_heavy_cnt_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_heavy_cnt);

static ssize_t last_triggered_batoilo_medium_cnt_show(struct device *dev,
							       struct device_attribute *attr,
							       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_medium_cnt);

static ssize_t last_triggered_batoilo_light_cnt_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_light_cnt);

static ssize_t last_triggered_batoilo_start_cnt_show(struct device *dev,
						     struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_start_cnt);

static ssize_t last_triggered_batoilo_heavy_time_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, heavy);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_heavy_time);

static ssize_t last_triggered_batoilo_medium_time_show(struct device *dev,
								struct device_attribute *attr,
								char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, MEDIUM);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_medium_time);

static ssize_t last_triggered_batoilo_light_time_show(struct device *dev,
							  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, light);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_light_time);

static ssize_t last_triggered_batoilo_start_time_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, START);
}

static DEVICE_ATTR_RO(last_triggered_batoilo_start_time);

static struct attribute *clock_stats_attrs[] = {
	&dev_attr_clk_CPU0_stats.attr,
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
	&dev_attr_clk_CPU1A_stats.attr,
	&dev_attr_clk_CPU1B_stats.attr,
#else
	&dev_attr_clk_CPU1_stats.attr,
#endif
	&dev_attr_clk_CPU2_stats.attr,
	&dev_attr_clk_TPU_stats.attr,
	&dev_attr_clk_GPU_stats.attr,
	&dev_attr_clk_AUR_stats.attr,
	NULL,
};

static const struct attribute_group clock_stats_group = {
	.attrs = clock_stats_attrs,
	.name = "clock_stats",
};


#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)

#define GEN_MITIGATION_RES_EN(core)\
static ssize_t mitigation_##core##_res_en_show(struct device *dev,\
					       struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_mitigation_res_en(bcl_dev, core, buf);\
} \
\
static ssize_t mitigation_##core##_res_en_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_mitigation_res_en(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(mitigation_##core##_res_en)

GEN_MITIGATION_RES_EN(CPU0);
GEN_MITIGATION_RES_EN(CPU1A);
GEN_MITIGATION_RES_EN(CPU1B);
GEN_MITIGATION_RES_EN(CPU2);
GEN_MITIGATION_RES_EN(GPU);
GEN_MITIGATION_RES_EN(TPU);
GEN_MITIGATION_RES_EN(AUR);

static struct attribute *mitigation_res_en_attrs[] = {
	&dev_attr_mitigation_CPU0_res_en.attr,
	&dev_attr_mitigation_CPU1A_res_en.attr,
	&dev_attr_mitigation_CPU1B_res_en.attr,
	&dev_attr_mitigation_CPU2_res_en.attr,
	&dev_attr_mitigation_GPU_res_en.attr,
	&dev_attr_mitigation_TPU_res_en.attr,
	&dev_attr_mitigation_AUR_res_en.attr,
	NULL,
};

#define GEN_MITIGATION_RES_TYPE(core)\
static ssize_t mitigation_##core##_res_type_show(struct device *dev,\
						 struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_mitigation_res_type(bcl_dev, core, buf);\
} \
\
static ssize_t mitigation_##core##_res_type_store(struct device *dev,\
						  struct device_attribute *attr,\
						  const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_mitigation_res_type(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(mitigation_##core##_res_type)

GEN_MITIGATION_RES_TYPE(CPU0);
GEN_MITIGATION_RES_TYPE(CPU1A);
GEN_MITIGATION_RES_TYPE(CPU1B);
GEN_MITIGATION_RES_TYPE(CPU2);
GEN_MITIGATION_RES_TYPE(GPU);
GEN_MITIGATION_RES_TYPE(TPU);
GEN_MITIGATION_RES_TYPE(AUR);

static struct attribute *mitigation_res_type_attrs[] = {
	&dev_attr_mitigation_CPU0_res_type.attr,
	&dev_attr_mitigation_CPU1A_res_type.attr,
	&dev_attr_mitigation_CPU1B_res_type.attr,
	&dev_attr_mitigation_CPU2_res_type.attr,
	&dev_attr_mitigation_GPU_res_type.attr,
	&dev_attr_mitigation_TPU_res_type.attr,
	&dev_attr_mitigation_AUR_res_type.attr,
	NULL,
};

#define GEN_MITIGATION_RES_HYST(core)\
static ssize_t mitigation_##core##_res_hyst_show(struct device *dev,\
						 struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_mitigation_res_hyst(bcl_dev, core, buf);\
} \
\
static ssize_t mitigation_##core##_res_hyst_store(struct device *dev,\
						  struct device_attribute *attr,\
						  const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_mitigation_res_hyst(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(mitigation_##core##_res_hyst)

GEN_MITIGATION_RES_HYST(CPU0);
GEN_MITIGATION_RES_HYST(CPU1A);
GEN_MITIGATION_RES_HYST(CPU1B);
GEN_MITIGATION_RES_HYST(CPU2);
GEN_MITIGATION_RES_HYST(GPU);
GEN_MITIGATION_RES_HYST(TPU);
GEN_MITIGATION_RES_HYST(AUR);

static struct attribute *mitigation_res_hyst_attrs[] = {
	&dev_attr_mitigation_CPU0_res_hyst.attr,
	&dev_attr_mitigation_CPU1A_res_hyst.attr,
	&dev_attr_mitigation_CPU1B_res_hyst.attr,
	&dev_attr_mitigation_CPU2_res_hyst.attr,
	&dev_attr_mitigation_GPU_res_hyst.attr,
	&dev_attr_mitigation_TPU_res_hyst.attr,
	&dev_attr_mitigation_AUR_res_hyst.attr,
	NULL,
};

static const struct attribute_group mitigation_res_en_group = {
	.attrs = mitigation_res_en_attrs,
	.name = "mitigation_res_en",
};

static const struct attribute_group mitigation_res_type_group = {
	.attrs = mitigation_res_type_attrs,
	.name = "mitigation_res_type",
};

static const struct attribute_group mitigation_res_hyst_group = {
	.attrs = mitigation_res_hyst_attrs,
	.name = "mitigation_res_hyst",
};
#endif

#if IS_ENABLED(CONFIG_SOC_MBU)

#define GEN_DYN_CLK_DIV_EN(core)\
static ssize_t core##_dyn_clk_div_en_show(struct device *dev,\
						   struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_dyn_clk_div_enable(bcl_dev, core, buf);\
} \
\
static ssize_t core##_dyn_clk_div_en_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_dyn_clk_div_enable(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(core##_dyn_clk_div_en)

GEN_DYN_CLK_DIV_EN(CPU1A);
GEN_DYN_CLK_DIV_EN(CPU1B);
GEN_DYN_CLK_DIV_EN(CPU2);

static struct attribute *dyn_clk_div_en_attrs[] = {
	&dev_attr_CPU1A_dyn_clk_div_en.attr,
	&dev_attr_CPU1B_dyn_clk_div_en.attr,
	&dev_attr_CPU2_dyn_clk_div_en.attr,
	NULL,
};

static const struct attribute_group dyn_clk_div_en_group = {
	.attrs = dyn_clk_div_en_attrs,
	.name = "dyn_clk_div_en",
};

#define GEN_DYN_CLK_DIV_RATIO(core)\
static ssize_t core##_dyn_clk_div_ratio_show(struct device *dev,\
						   struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_dyn_clk_div_ratio(bcl_dev, core, buf);\
} \
\
static ssize_t core##_dyn_clk_div_ratio_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_dyn_clk_div_ratio(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(core##_dyn_clk_div_ratio)

GEN_DYN_CLK_DIV_RATIO(CPU1A);
GEN_DYN_CLK_DIV_RATIO(CPU1B);
GEN_DYN_CLK_DIV_RATIO(CPU2);

static struct attribute *dyn_clk_div_ratio_attrs[] = {
	&dev_attr_CPU1A_dyn_clk_div_ratio.attr,
	&dev_attr_CPU1B_dyn_clk_div_ratio.attr,
	&dev_attr_CPU2_dyn_clk_div_ratio.attr,
	NULL,
};

static const struct attribute_group dyn_clk_div_ratio_group = {
	.attrs = dyn_clk_div_ratio_attrs,
	.name = "dyn_clk_div_ratio",
};


#define GEN_DYN_CLK_DIV_THRESH(core)\
static ssize_t core##_dyn_clk_div_thresh_show(struct device *dev,\
						   struct device_attribute *attr, char *buf)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return get_dyn_clk_div_thresh(bcl_dev, core, buf);\
} \
\
static ssize_t core##_dyn_clk_div_thresh_store(struct device *dev,\
						struct device_attribute *attr,\
						const char *buf, size_t size)\
{ \
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);\
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);\
\
	return set_dyn_clk_div_thresh(bcl_dev, buf, size, core);\
} \
static DEVICE_ATTR_RW(core##_dyn_clk_div_thresh)

GEN_DYN_CLK_DIV_THRESH(CPU1A);
GEN_DYN_CLK_DIV_THRESH(CPU1B);
GEN_DYN_CLK_DIV_THRESH(CPU2);

static struct attribute *dyn_clk_div_thresh_attrs[] = {
	&dev_attr_CPU1A_dyn_clk_div_thresh.attr,
	&dev_attr_CPU1B_dyn_clk_div_thresh.attr,
	&dev_attr_CPU2_dyn_clk_div_thresh.attr,
	NULL,
};

static const struct attribute_group dyn_clk_div_thresh_group = {
	.attrs = dyn_clk_div_thresh_attrs,
	.name = "dyn_clk_div_thresh",
};
#endif

static struct attribute *triggered_count_attrs[] = {
	&dev_attr_smpl_warn_count.attr,
	&dev_attr_ocp_cpu1_count.attr,
	&dev_attr_ocp_cpu2_count.attr,
	&dev_attr_ocp_tpu_count.attr,
	&dev_attr_ocp_gpu_count.attr,
	&dev_attr_soft_ocp_cpu1_count.attr,
	&dev_attr_soft_ocp_cpu2_count.attr,
	&dev_attr_soft_ocp_tpu_count.attr,
	&dev_attr_soft_ocp_gpu_count.attr,
	&dev_attr_vdroop1_count.attr,
	&dev_attr_vdroop2_count.attr,
	&dev_attr_batoilo_count.attr,
	&dev_attr_batoilo2_count.attr,
	NULL,
};

static const struct attribute_group triggered_count_group = {
	.attrs = triggered_count_attrs,
	.name = "last_triggered_count",
};

static struct attribute *triggered_time_attrs[] = {
	&dev_attr_smpl_warn_time.attr,
	&dev_attr_ocp_cpu1_time.attr,
	&dev_attr_ocp_cpu2_time.attr,
	&dev_attr_ocp_tpu_time.attr,
	&dev_attr_ocp_gpu_time.attr,
	&dev_attr_soft_ocp_cpu1_time.attr,
	&dev_attr_soft_ocp_cpu2_time.attr,
	&dev_attr_soft_ocp_tpu_time.attr,
	&dev_attr_soft_ocp_gpu_time.attr,
	&dev_attr_vdroop1_time.attr,
	&dev_attr_vdroop2_time.attr,
	&dev_attr_batoilo_time.attr,
	&dev_attr_batoilo2_time.attr,
	NULL,
};

static const struct attribute_group triggered_timestamp_group = {
	.attrs = triggered_time_attrs,
	.name = "last_triggered_timestamp",
};

static struct attribute *triggered_cap_attrs[] = {
	&dev_attr_smpl_warn_cap.attr,
	&dev_attr_ocp_cpu1_cap.attr,
	&dev_attr_ocp_cpu2_cap.attr,
	&dev_attr_ocp_tpu_cap.attr,
	&dev_attr_ocp_gpu_cap.attr,
	&dev_attr_soft_ocp_cpu1_cap.attr,
	&dev_attr_soft_ocp_cpu2_cap.attr,
	&dev_attr_soft_ocp_tpu_cap.attr,
	&dev_attr_soft_ocp_gpu_cap.attr,
	&dev_attr_vdroop1_cap.attr,
	&dev_attr_vdroop2_cap.attr,
	&dev_attr_batoilo_cap.attr,
	&dev_attr_batoilo2_cap.attr,
	NULL,
};

static const struct attribute_group triggered_capacity_group = {
	.attrs = triggered_cap_attrs,
	.name = "last_triggered_capacity",
};

#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
static ssize_t ocp_batfet_timeout_enable_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_batfet_timeout_enable(bcl_dev, buf);
}

static ssize_t ocp_batfet_timeout_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_batfet_timeout_enable(bcl_dev, buf, size);
}

static DEVICE_ATTR_RW(ocp_batfet_timeout_enable);

static ssize_t ocp_bat_throttle_timeout_enable_show(struct device *dev,
						    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_bat_throttle_timeout_enable(bcl_dev, buf);
}

static ssize_t ocp_bat_throttle_timeout_enable_store(struct device *dev,
						     struct device_attribute *attr,
						     const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_bat_throttle_timeout_enable(bcl_dev, buf, size);
}

static DEVICE_ATTR_RW(ocp_bat_throttle_timeout_enable);

static ssize_t ocp_batfet_timeout_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_batfet_timeout(bcl_dev, buf);
}

static ssize_t ocp_batfet_timeout_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_batfet_timeout(bcl_dev, buf, size);
}

static DEVICE_ATTR_RW(ocp_batfet_timeout);

static ssize_t ocp_bat_throttle_timeout_show(struct device *dev,
					     struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return get_ocp_bat_throttle_timeout(bcl_dev, buf);
}

static ssize_t ocp_bat_throttle_timeout_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return set_ocp_bat_throttle_timeout(bcl_dev, buf, size);
}

static DEVICE_ATTR_RW(ocp_bat_throttle_timeout);

static struct attribute *trigger_timer_attrs[] = {
	&dev_attr_ocp_bat_throttle_timeout.attr,
	&dev_attr_ocp_bat_throttle_timeout_enable.attr,
	&dev_attr_ocp_batfet_timeout.attr,
	&dev_attr_ocp_batfet_timeout_enable.attr,
	NULL,
};

static const struct attribute_group trigger_timer_group = {
	.attrs = trigger_timer_attrs,
	.name = "trigger_timeout",
};
#endif

static struct attribute *triggered_volt_attrs[] = {
	&dev_attr_smpl_warn_volt.attr,
	&dev_attr_ocp_cpu1_volt.attr,
	&dev_attr_ocp_cpu2_volt.attr,
	&dev_attr_ocp_tpu_volt.attr,
	&dev_attr_ocp_gpu_volt.attr,
	&dev_attr_soft_ocp_cpu1_volt.attr,
	&dev_attr_soft_ocp_cpu2_volt.attr,
	&dev_attr_soft_ocp_tpu_volt.attr,
	&dev_attr_soft_ocp_gpu_volt.attr,
	&dev_attr_vdroop1_volt.attr,
	&dev_attr_vdroop2_volt.attr,
	&dev_attr_batoilo_volt.attr,
	&dev_attr_batoilo2_volt.attr,
	NULL,
};

static const struct attribute_group triggered_voltage_group = {
	.attrs = triggered_volt_attrs,
	.name = "last_triggered_voltage",
};

static struct attribute *last_triggered_mode_attrs[] = {
	&dev_attr_last_triggered_uvlo1_start_cnt.attr,
	&dev_attr_last_triggered_uvlo1_start_time.attr,
	&dev_attr_last_triggered_uvlo1_light_cnt.attr,
	&dev_attr_last_triggered_uvlo1_light_time.attr,
	&dev_attr_last_triggered_uvlo1_medium_cnt.attr,
	&dev_attr_last_triggered_uvlo1_medium_time.attr,
	&dev_attr_last_triggered_uvlo1_heavy_cnt.attr,
	&dev_attr_last_triggered_uvlo1_heavy_time.attr,
	&dev_attr_last_triggered_uvlo2_start_cnt.attr,
	&dev_attr_last_triggered_uvlo2_start_time.attr,
	&dev_attr_last_triggered_uvlo2_light_cnt.attr,
	&dev_attr_last_triggered_uvlo2_light_time.attr,
	&dev_attr_last_triggered_uvlo2_medium_cnt.attr,
	&dev_attr_last_triggered_uvlo2_medium_time.attr,
	&dev_attr_last_triggered_uvlo2_heavy_cnt.attr,
	&dev_attr_last_triggered_uvlo2_heavy_time.attr,
	&dev_attr_last_triggered_batoilo_start_cnt.attr,
	&dev_attr_last_triggered_batoilo_start_time.attr,
	&dev_attr_last_triggered_batoilo_light_cnt.attr,
	&dev_attr_last_triggered_batoilo_light_time.attr,
	&dev_attr_last_triggered_batoilo_medium_cnt.attr,
	&dev_attr_last_triggered_batoilo_medium_time.attr,
	&dev_attr_last_triggered_batoilo_heavy_cnt.attr,
	&dev_attr_last_triggered_batoilo_heavy_time.attr,
	&dev_attr_last_triggered_batoilo2_start_cnt.attr,
	&dev_attr_last_triggered_batoilo2_start_time.attr,
	&dev_attr_last_triggered_batoilo2_light_cnt.attr,
	&dev_attr_last_triggered_batoilo2_light_time.attr,
	&dev_attr_last_triggered_batoilo2_medium_cnt.attr,
	&dev_attr_last_triggered_batoilo2_medium_time.attr,
	&dev_attr_last_triggered_batoilo2_heavy_cnt.attr,
	&dev_attr_last_triggered_batoilo2_heavy_time.attr,
	NULL,
};

static const struct attribute_group last_triggered_mode_group = {
	.attrs = last_triggered_mode_attrs,
	.name = "last_triggered_mode",
};

#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
static ssize_t vdroop_flt_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	unsigned int reg = 0;
	int ret;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case TPU:
	case GPU:
		return sysfs_emit(buf, "0x%x\n", bcl_dev->core_conf[idx].vdroop_flt);
	case CPU1:
	case CPU2:
		ret = cpu_buff_read(bcl_dev, idx, CPU_BUFF_VDROOP_FLT, &reg);
		if (ret < 0)
			return ret;
		break;
	}
	return sysfs_emit(buf, "0x%x\n", reg);
}

static ssize_t vdroop_flt_store(struct bcl_device *bcl_dev, int idx,
				const char *buf, size_t size)
{
	unsigned int value;
	int ret;

	if (sscanf(buf, "0x%x", &value) != 1)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;
	switch (idx) {
	case TPU:
	case GPU:
		bcl_dev->core_conf[idx].vdroop_flt = value;
		return size;
	case CPU1:
	case CPU2:
		ret = cpu_buff_write(bcl_dev, idx, CPU_BUFF_VDROOP_FLT, value);
		if (ret < 0)
			return ret;
	}
	return size;
}

static ssize_t cpu1_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, CPU1, buf);
}

static ssize_t cpu1_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, CPU1, buf, size);
}

static DEVICE_ATTR_RW(cpu1_vdroop_flt);

static ssize_t cpu2_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, CPU2, buf);
}

static ssize_t cpu2_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, CPU2, buf, size);
}

static DEVICE_ATTR_RW(cpu2_vdroop_flt);

static ssize_t tpu_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, TPU, buf);
}

static ssize_t tpu_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, TPU, buf, size);
}

static DEVICE_ATTR_RW(tpu_vdroop_flt);

static ssize_t gpu_vdroop_flt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_show(bcl_dev, GPU, buf);
}

static ssize_t gpu_vdroop_flt_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return vdroop_flt_store(bcl_dev, GPU, buf, size);
}

static DEVICE_ATTR_RW(gpu_vdroop_flt);

static struct attribute *vdroop_flt_attrs[] = {
	&dev_attr_cpu1_vdroop_flt.attr,
	&dev_attr_cpu2_vdroop_flt.attr,
	&dev_attr_tpu_vdroop_flt.attr,
	&dev_attr_gpu_vdroop_flt.attr,
	NULL,
};

static const struct attribute_group vdroop_flt_group = {
	.attrs = vdroop_flt_attrs,
	.name = "vdroop_flt",
};
#endif

static ssize_t main_pwrwarn_threshold_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	ret = sscanf(attr->attr.name, "main_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d=%lld\n", bcl_dev->main_setting[idx], bcl_dev->main_limit[idx]);
}

static ssize_t main_pwrwarn_threshold_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx, value;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;
	ret = sscanf(attr->attr.name, "main_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	bcl_dev->main_setting[idx] = value;
	bcl_dev->main_limit[idx] = settings_to_current(bcl_dev, CORE_PMIC_MAIN, idx, value);
	meter_write(CORE_PMIC_MAIN, bcl_dev, idx, value);

	return size;
}

static ssize_t sub_pwrwarn_threshold_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	ret = sscanf(attr->attr.name, "sub_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d=%lld\n", bcl_dev->sub_setting[idx], bcl_dev->sub_limit[idx]);
}

static ssize_t sub_pwrwarn_threshold_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx, value;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;
	ret = sscanf(attr->attr.name, "sub_pwrwarn_threshold%d", &idx);
	if (ret != 1)
		return -EINVAL;
	if (idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	bcl_dev->sub_setting[idx] = value;
	bcl_dev->sub_limit[idx] = settings_to_current(bcl_dev, CORE_PMIC_SUB, idx, value);
	meter_write(CORE_PMIC_SUB, bcl_dev, idx, value);

	return size;
}

#define DEVICE_PWRWARN_ATTR(_name, _num) \
	struct device_attribute attr_##_name##_num = \
		__ATTR(_name##_num, 0644, _name##_show, _name##_store)

static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 0);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 1);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 2);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 3);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 4);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 5);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 6);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 7);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 8);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 9);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 10);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 11);
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 12);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 13);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 14);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 15);
#endif

static struct attribute *main_pwrwarn_attrs[] = {
	&attr_main_pwrwarn_threshold0.attr,
	&attr_main_pwrwarn_threshold1.attr,
	&attr_main_pwrwarn_threshold2.attr,
	&attr_main_pwrwarn_threshold3.attr,
	&attr_main_pwrwarn_threshold4.attr,
	&attr_main_pwrwarn_threshold5.attr,
	&attr_main_pwrwarn_threshold6.attr,
	&attr_main_pwrwarn_threshold7.attr,
	&attr_main_pwrwarn_threshold8.attr,
	&attr_main_pwrwarn_threshold9.attr,
	&attr_main_pwrwarn_threshold10.attr,
	&attr_main_pwrwarn_threshold11.attr,
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	&attr_main_pwrwarn_threshold12.attr,
	&attr_main_pwrwarn_threshold13.attr,
	&attr_main_pwrwarn_threshold14.attr,
	&attr_main_pwrwarn_threshold15.attr,
#endif
	NULL,
};

static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 0);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 1);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 2);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 3);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 4);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 5);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 6);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 7);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 8);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 9);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 10);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 11);
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 12);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 13);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 14);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 15);
#endif

static struct attribute *sub_pwrwarn_attrs[] = {
	&attr_sub_pwrwarn_threshold0.attr,
	&attr_sub_pwrwarn_threshold1.attr,
	&attr_sub_pwrwarn_threshold2.attr,
	&attr_sub_pwrwarn_threshold3.attr,
	&attr_sub_pwrwarn_threshold4.attr,
	&attr_sub_pwrwarn_threshold5.attr,
	&attr_sub_pwrwarn_threshold6.attr,
	&attr_sub_pwrwarn_threshold7.attr,
	&attr_sub_pwrwarn_threshold8.attr,
	&attr_sub_pwrwarn_threshold9.attr,
	&attr_sub_pwrwarn_threshold10.attr,
	&attr_sub_pwrwarn_threshold11.attr,
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	&attr_sub_pwrwarn_threshold12.attr,
	&attr_sub_pwrwarn_threshold13.attr,
	&attr_sub_pwrwarn_threshold14.attr,
	&attr_sub_pwrwarn_threshold15.attr,
#endif
	NULL,
};

static ssize_t qos_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	struct bcl_zone *zone;

	if (!bcl_dev)
		return -EIO;
	zone = bcl_dev->zone[idx];
	if ((!zone) || (!zone->bcl_qos))
		return -EIO;

#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
	return sysfs_emit(buf, "CPU0,CPU1A,CPU1B,CPU2,GPU,TPU,GXP\n%d,%d,%d,%d,%d,%d,%d\n",
			  zone->bcl_qos->cpu_limit[QOS_CPU0][QOS_LIGHT_IND],
			  zone->bcl_qos->cpu_limit[QOS_CPU1A][QOS_LIGHT_IND],
			  zone->bcl_qos->cpu_limit[QOS_CPU1B][QOS_LIGHT_IND],
			  zone->bcl_qos->cpu_limit[QOS_CPU2][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_GPU][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_TPU][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_GXP][QOS_LIGHT_IND]);
#else
	return sysfs_emit(buf, "CPU0,CPU1,CPU2,GPU,TPU,GXP\n%d,%d,%d,%d,%d,%d\n",
			  zone->bcl_qos->cpu_limit[QOS_CPU0][QOS_LIGHT_IND],
			  zone->bcl_qos->cpu_limit[QOS_CPU1][QOS_LIGHT_IND],
			  zone->bcl_qos->cpu_limit[QOS_CPU2][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_GPU][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_TPU][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_GXP][QOS_LIGHT_IND]);
#endif
}

static ssize_t qos_store(struct bcl_device *bcl_dev, int idx, const char *buf, size_t size)
{
	unsigned int soc[7];
	int ret, intended_items;
	struct bcl_zone *zone;

	if (IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)) {
		ret = sscanf(buf, "%d,%d,%d,%d,%d,%d,%d",
			 &soc[0], &soc[1], &soc[2], &soc[3], &soc[4], &soc[5], &soc[6]);
		intended_items = 7;
	} else {
		ret = sscanf(buf, "%d,%d,%d,%d,%d,%d",
			 &soc[0], &soc[1], &soc[2], &soc[3], &soc[4], &soc[5]);
		intended_items = 6;
	}

	if (ret != intended_items)
		return -EINVAL;

	if (!bcl_dev)
		return -EIO;
	zone = bcl_dev->zone[idx];
	if ((!zone) || (!zone->bcl_qos))
		return -EIO;
	zone->bcl_qos->cpu_limit[QOS_CPU0][QOS_LIGHT_IND] = soc[0];
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
	zone->bcl_qos->cpu_limit[QOS_CPU1A][QOS_LIGHT_IND] = soc[1];
	zone->bcl_qos->cpu_limit[QOS_CPU1B][QOS_LIGHT_IND] = soc[2];
#else
	zone->bcl_qos->cpu_limit[QOS_CPU1][QOS_LIGHT_IND] = soc[1];
#endif
	zone->bcl_qos->cpu_limit[QOS_CPU2][QOS_LIGHT_IND] = soc[intended_items - 4];
	zone->bcl_qos->df_limit[QOS_GPU][QOS_LIGHT_IND] = soc[intended_items - 3];
	zone->bcl_qos->df_limit[QOS_TPU][QOS_LIGHT_IND] = soc[intended_items - 2];
	zone->bcl_qos->df_limit[QOS_GXP][QOS_LIGHT_IND] = soc[intended_items - 1];

	return size;
}

static ssize_t qos_batoilo2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, BATOILO2, buf);
}

static ssize_t qos_batoilo2_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, BATOILO2, buf, size);
}

static ssize_t qos_batoilo_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, BATOILO, buf);
}

static ssize_t qos_batoilo_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, BATOILO, buf, size);
}

static ssize_t qos_vdroop1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, UVLO1, buf);
}

static ssize_t qos_vdroop1_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, UVLO1, buf, size);
}

static ssize_t qos_vdroop2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, UVLO2, buf);
}

static ssize_t qos_vdroop2_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, UVLO2, buf, size);
}

static ssize_t qos_smpl_warn_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, PRE_UVLO, buf);
}

static ssize_t qos_smpl_warn_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, PRE_UVLO, buf, size);
}

static ssize_t qos_ocp_cpu2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, PRE_OCP_CPU2, buf);
}

static ssize_t qos_ocp_cpu2_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, PRE_OCP_CPU2, buf, size);
}

static ssize_t qos_ocp_cpu1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, PRE_OCP_CPU1, buf);
}

static ssize_t qos_ocp_cpu1_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, PRE_OCP_CPU1, buf, size);
}

static ssize_t qos_ocp_tpu_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, PRE_OCP_TPU, buf);
}

static ssize_t qos_ocp_tpu_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, PRE_OCP_TPU, buf, size);
}

static ssize_t qos_ocp_gpu_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_show(bcl_dev, PRE_OCP_GPU, buf);
}

static ssize_t qos_ocp_gpu_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return qos_store(bcl_dev, PRE_OCP_GPU, buf, size);
}

static DEVICE_ATTR_RW(qos_batoilo2);
static DEVICE_ATTR_RW(qos_batoilo);
static DEVICE_ATTR_RW(qos_vdroop1);
static DEVICE_ATTR_RW(qos_vdroop2);
static DEVICE_ATTR_RW(qos_smpl_warn);
static DEVICE_ATTR_RW(qos_ocp_cpu2);
static DEVICE_ATTR_RW(qos_ocp_cpu1);
static DEVICE_ATTR_RW(qos_ocp_gpu);
static DEVICE_ATTR_RW(qos_ocp_tpu);

static struct attribute *qos_attrs[] = {
	&dev_attr_qos_batoilo2.attr,
	&dev_attr_qos_batoilo.attr,
	&dev_attr_qos_vdroop1.attr,
	&dev_attr_qos_vdroop2.attr,
	&dev_attr_qos_smpl_warn.attr,
	&dev_attr_qos_ocp_cpu2.attr,
	&dev_attr_qos_ocp_cpu1.attr,
	&dev_attr_qos_ocp_gpu.attr,
	&dev_attr_qos_ocp_tpu.attr,
	NULL,
};

static const struct attribute_group main_pwrwarn_group = {
	.attrs = main_pwrwarn_attrs,
	.name = "main_pwrwarn",
};

static const struct attribute_group sub_pwrwarn_group = {
	.attrs = sub_pwrwarn_attrs,
	.name = "sub_pwrwarn",
};

static ssize_t less_than_5ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.lt_5ms_count);
			count += scnprintf(buf + count, PAGE_SIZE - count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx].lt_5ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx].lt_5ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

static DEVICE_ATTR_RO(less_than_5ms_count);

static ssize_t between_5ms_to_10ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.bt_5ms_10ms_count);
			count += scnprintf(buf + count, PAGE_SIZE - count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx]
					.bt_5ms_10ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx]
					.bt_5ms_10ms_count);
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

static DEVICE_ATTR_RO(between_5ms_to_10ms_count);

static ssize_t greater_than_10ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.gt_10ms_count);
			count += scnprintf(buf + count, PAGE_SIZE - count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
#if IS_ENABLED(CONFIG_REGULATOR_S2MPG14)
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx].gt_10ms_count);
#endif
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
		irq_count = core_pmic_read_main_pwrwarn(bcl_dev, pwrwarn_idx);
#endif
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
#if IS_ENABLED(CONFIG_REGULATOR_S2MPG14)
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx].gt_10ms_count);
#endif
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA)
		irq_count = core_pmic_read_sub_pwrwarn(bcl_dev, pwrwarn_idx);
#endif
		count += scnprintf(buf + count, PAGE_SIZE - count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

static DEVICE_ATTR_RO(greater_than_10ms_count);

static struct attribute *irq_dur_cnt_attrs[] = {
	&dev_attr_less_than_5ms_count.attr,
	&dev_attr_between_5ms_to_10ms_count.attr,
	&dev_attr_greater_than_10ms_count.attr,
	NULL,
};

static ssize_t disabled_store(struct bcl_zone *zone, bool disabled, size_t size)
{
	if (disabled && !zone->disabled) {
		zone->disabled = true;
		disable_irq(zone->bcl_irq);
	} else if (!disabled && zone->disabled) {
		zone->disabled = false;
		enable_irq(zone->bcl_irq);
	}
	return size;
}

static ssize_t uvlo1_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[UVLO1])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[UVLO1]->disabled);
}

static ssize_t uvlo1_disabled_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[UVLO1])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[UVLO1], value, size);
}

static ssize_t uvlo2_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[UVLO2])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[UVLO2]->disabled);
}

static ssize_t uvlo2_disabled_store(struct device *dev, struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[UVLO2])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[UVLO2], value, size);
}

static ssize_t batoilo_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[BATOILO])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[BATOILO]->disabled);
}

static ssize_t batoilo_disabled_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[BATOILO])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[BATOILO], value, size);
}

static ssize_t batoilo2_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[BATOILO2])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[BATOILO2]->disabled);
}

static ssize_t batoilo2_disabled_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[BATOILO2])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[BATOILO2], value, size);
}

static ssize_t smpl_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[PRE_UVLO])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[PRE_UVLO]->disabled);
}

static ssize_t smpl_disabled_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[PRE_UVLO])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[PRE_UVLO], value, size);
}

#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
static ssize_t ocp_cpu1_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[PRE_OCP_CPU1])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[PRE_OCP_CPU1]->disabled);
}

static ssize_t ocp_cpu1_disabled_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[PRE_OCP_CPU1])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[PRE_OCP_CPU1], value, size);
}

static ssize_t ocp_cpu2_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[PRE_OCP_CPU2])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[PRE_OCP_CPU2]->disabled);
}

static ssize_t ocp_cpu2_disabled_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[PRE_OCP_CPU2])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[PRE_OCP_CPU2], value, size);
}

static ssize_t ocp_tpu_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[PRE_OCP_TPU])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[PRE_OCP_TPU]->disabled);
}

static ssize_t ocp_tpu_disabled_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[PRE_OCP_TPU])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[PRE_OCP_TPU], value, size);
}

static ssize_t ocp_gpu_disabled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[PRE_OCP_GPU])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[PRE_OCP_GPU]->disabled);
}

static ssize_t ocp_gpu_disabled_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[PRE_OCP_GPU])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[PRE_OCP_GPU], value, size);
}

static ssize_t soft_ocp_cpu1_disabled_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[SOFT_PRE_OCP_CPU1])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_PRE_OCP_CPU1]->disabled);
}

static ssize_t soft_ocp_cpu1_disabled_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[SOFT_PRE_OCP_CPU1])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[SOFT_PRE_OCP_CPU1], value, size);
}

static ssize_t soft_ocp_cpu2_disabled_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[SOFT_PRE_OCP_CPU2])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_PRE_OCP_CPU2]->disabled);
}

static ssize_t soft_ocp_cpu2_disabled_store(struct device *dev, struct device_attribute *attr,
					    const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[SOFT_PRE_OCP_CPU2])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[SOFT_PRE_OCP_CPU2], value, size);
}

static ssize_t soft_ocp_tpu_disabled_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[SOFT_PRE_OCP_TPU])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_PRE_OCP_TPU]->disabled);
}

static ssize_t soft_ocp_tpu_disabled_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[SOFT_PRE_OCP_TPU])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[SOFT_PRE_OCP_TPU], value, size);
}

static ssize_t soft_ocp_gpu_disabled_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[SOFT_PRE_OCP_GPU])
		return -ENODEV;
	return sysfs_emit(buf, "%d\n", bcl_dev->zone[SOFT_PRE_OCP_GPU]->disabled);
}

static ssize_t soft_ocp_gpu_disabled_store(struct device *dev, struct device_attribute *attr,
					   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->zone[SOFT_PRE_OCP_GPU])
		return -ENODEV;
	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;
	return disabled_store(bcl_dev->zone[SOFT_PRE_OCP_GPU], value, size);
}
#endif

static DEVICE_ATTR_RW(uvlo1_disabled);
static DEVICE_ATTR_RW(uvlo2_disabled);
static DEVICE_ATTR_RW(batoilo_disabled);
static DEVICE_ATTR_RW(batoilo2_disabled);
static DEVICE_ATTR_RW(smpl_disabled);
#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
static DEVICE_ATTR_RW(ocp_cpu1_disabled);
static DEVICE_ATTR_RW(ocp_cpu2_disabled);
static DEVICE_ATTR_RW(ocp_tpu_disabled);
static DEVICE_ATTR_RW(ocp_gpu_disabled);
static DEVICE_ATTR_RW(soft_ocp_cpu1_disabled);
static DEVICE_ATTR_RW(soft_ocp_cpu2_disabled);
static DEVICE_ATTR_RW(soft_ocp_tpu_disabled);
static DEVICE_ATTR_RW(soft_ocp_gpu_disabled);
#endif

static struct attribute *irq_config_attrs[] = {
	&dev_attr_uvlo1_disabled.attr,
	&dev_attr_uvlo2_disabled.attr,
	&dev_attr_batoilo_disabled.attr,
	&dev_attr_batoilo2_disabled.attr,
	&dev_attr_smpl_disabled.attr,
#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	&dev_attr_ocp_cpu1_disabled.attr,
	&dev_attr_ocp_cpu2_disabled.attr,
	&dev_attr_ocp_tpu_disabled.attr,
	&dev_attr_ocp_gpu_disabled.attr,
	&dev_attr_soft_ocp_cpu1_disabled.attr,
	&dev_attr_soft_ocp_cpu2_disabled.attr,
	&dev_attr_soft_ocp_tpu_disabled.attr,
	&dev_attr_soft_ocp_gpu_disabled.attr,
#endif
	NULL,
};

static int get_final_mitigation_module_ids(struct bcl_device *bcl_dev)
{
	int mitigation_module_ids = atomic_read(&bcl_dev->mitigation_module_ids);
	int w = hweight32(mitigation_module_ids);

	if (w >= HEAVY_MITIGATION_MODULES_NUM || w == 0)
		mitigation_module_ids |= bcl_dev->non_monitored_mitigation_module_ids;

	return mitigation_module_ids;
}

static ssize_t uvlo1_triggered_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[UVLO1])
		return -ENODEV;
	return sysfs_emit(buf, "%d_%d\n", bcl_dev->zone[UVLO1]->current_state,
					  get_final_mitigation_module_ids(bcl_dev));
}

static ssize_t uvlo2_triggered_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[UVLO2])
		return -ENODEV;
	return sysfs_emit(buf, "%d_%d\n", bcl_dev->zone[UVLO2]->current_state,
					  get_final_mitigation_module_ids(bcl_dev));
}

static ssize_t oilo1_triggered_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[BATOILO])
		return -ENODEV;
	return sysfs_emit(buf, "%d_%d\n", bcl_dev->zone[BATOILO]->current_state,
					  get_final_mitigation_module_ids(bcl_dev));
}

static ssize_t oilo2_triggered_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[BATOILO2])
		return -ENODEV;
	return sysfs_emit(buf, "%d_%d\n", bcl_dev->zone[BATOILO2]->current_state,
					  get_final_mitigation_module_ids(bcl_dev));
}

static ssize_t smpl_triggered_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (!bcl_dev->zone[PRE_UVLO])
		return -ENODEV;
	return sysfs_emit(buf, "%d_%d\n", bcl_dev->zone[PRE_UVLO]->current_state,
					  get_final_mitigation_module_ids(bcl_dev));
}
static DEVICE_ATTR_RO(oilo1_triggered);
static DEVICE_ATTR_RO(oilo2_triggered);
static DEVICE_ATTR_RO(uvlo1_triggered);
static DEVICE_ATTR_RO(uvlo2_triggered);
static DEVICE_ATTR_RO(smpl_triggered);

static void bunch_mitigation_threshold_addr(struct bcl_mitigation_conf *mitigation_conf,
					    unsigned int *addr[METER_CHANNEL_MAX])
{
	int i;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return;
#endif

	for (i = 0; i < METER_CHANNEL_MAX; i++)
		addr[i] = &mitigation_conf[i].threshold;
}

static void bunch_mitigation_module_id_addr(struct bcl_mitigation_conf *mitigation_conf,
					    unsigned int *addr[METER_CHANNEL_MAX])
{
	int i;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return;
#endif

	for (i = 0; i < METER_CHANNEL_MAX; i++)
		addr[i] = &mitigation_conf[i].module_id;
}

static ssize_t mitigation_show(unsigned int *addr[METER_CHANNEL_MAX], char *buf)
{
	int i, at = 0;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif

	for (i = 0; i < METER_CHANNEL_MAX; i++)
		at += sysfs_emit_at(buf, at, "%d,", *addr[i]);

	return at;
}

static ssize_t mitigation_store(unsigned int *addr[METER_CHANNEL_MAX],
					const char *buf, size_t size) {
	int i;
	unsigned int ch[METER_CHANNEL_MAX] = {0};
	char * const str = kstrndup(buf, size, GFP_KERNEL);
	char *sep_str = str;
	char *token = NULL;

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	goto mitigation_store_exit;
#endif

	if (!sep_str)
		goto mitigation_store_exit;

	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		token = strsep(&sep_str, MITIGATION_INPUT_DELIM);
		if (!token || sscanf(token, "%d", &ch[i]) != 1)
			break;
		else
			*addr[i] = ch[i];
	}

mitigation_store_exit:
	kfree(str);

#if IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10)
	return -ENODEV;
#endif
	return size;
}

static ssize_t main_mitigation_threshold_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_threshold_addr(bcl_dev->main_mitigation_conf, addr);

	return mitigation_show(addr, buf);
}

static ssize_t main_mitigation_threshold_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_threshold_addr(bcl_dev->main_mitigation_conf, addr);

	return mitigation_store(addr, buf, size);
}

static ssize_t sub_mitigation_threshold_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_threshold_addr(bcl_dev->sub_mitigation_conf, addr);

	return mitigation_show(addr, buf);
}

static ssize_t sub_mitigation_threshold_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_threshold_addr(bcl_dev->sub_mitigation_conf, addr);

	return mitigation_store(addr, buf, size);
}

static ssize_t main_mitigation_module_id_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_module_id_addr(bcl_dev->main_mitigation_conf, addr);

	return mitigation_show(addr, buf);
}

static ssize_t main_mitigation_module_id_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_module_id_addr(bcl_dev->main_mitigation_conf, addr);

	return mitigation_store(addr, buf, size);
}

static ssize_t sub_mitigation_module_id_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_module_id_addr(bcl_dev->sub_mitigation_conf, addr);

	return mitigation_show(addr, buf);
}

static ssize_t sub_mitigation_module_id_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int *addr[METER_CHANNEL_MAX];

	bunch_mitigation_module_id_addr(bcl_dev->sub_mitigation_conf, addr);

	return mitigation_store(addr, buf, size);
}

static DEVICE_ATTR_RW(main_mitigation_threshold);
static DEVICE_ATTR_RW(sub_mitigation_threshold);
static DEVICE_ATTR_RW(main_mitigation_module_id);
static DEVICE_ATTR_RW(sub_mitigation_module_id);

static struct attribute *mitigation_attrs[] = {
	&dev_attr_main_mitigation_threshold.attr,
	&dev_attr_sub_mitigation_threshold.attr,
	&dev_attr_main_mitigation_module_id.attr,
	&dev_attr_sub_mitigation_module_id.attr,
	NULL,
};

static struct attribute *triggered_state_sq_attrs[] = {
	&dev_attr_oilo1_triggered.attr,
	&dev_attr_uvlo1_triggered.attr,
	&dev_attr_uvlo2_triggered.attr,
	&dev_attr_smpl_triggered.attr,
	&dev_attr_oilo2_triggered.attr,
	NULL,
};

static struct attribute *triggered_state_mw_attrs[] = {
	&dev_attr_oilo1_triggered.attr,
	&dev_attr_uvlo1_triggered.attr,
	&dev_attr_uvlo2_triggered.attr,
#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	&dev_attr_smpl_triggered.attr,
#endif
	NULL,
};

static ssize_t triggered_idx_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->triggered_idx);
}

static DEVICE_ATTR(triggered_idx, 0444, triggered_idx_show, NULL);

static ssize_t enable_br_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->enabled_br_stats);
}

static ssize_t enable_br_stats_store(struct device *dev, struct device_attribute *attr,
				                          const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	bool value;
	int ret;

	if (!bcl_dev->data_logging_initialized)
		return -EINVAL;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	bcl_dev->enabled_br_stats = value;

	return size;
}

static DEVICE_ATTR_RW(enable_br_stats);

static ssize_t throttle_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->throttle_state);
}

static ssize_t throttle_state_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	ret = userspace_bcl_qos_update(bcl_dev, value);
	if (ret)
		return ret;

	return size;
}

static DEVICE_ATTR_RW(throttle_state);

static struct attribute *userspace_attrs[] = {
	&dev_attr_throttle_state.attr,
	NULL,
};

static ssize_t trigger_br_stats_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;

	if (!bcl_dev->data_logging_initialized)
		return -EINVAL;

	if (kstrtouint(buf, 16, &value) != 0 || value >= TRIGGERED_SOURCE_MAX)
		return -EINVAL;

	dev_dbg(bcl_dev->device, "Triggered: %d\n", value);
	google_bcl_start_data_logging(bcl_dev, value);
	return size;
}

static DEVICE_ATTR_WO(trigger_br_stats);

static ssize_t meter_channels_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", METER_CHANNEL_MAX);
}
static DEVICE_ATTR_RO(meter_channels);

static struct attribute *br_stats_attrs[] = {
	&dev_attr_triggered_idx.attr,
	&dev_attr_enable_br_stats.attr,
	&dev_attr_trigger_br_stats.attr,
	&dev_attr_meter_channels.attr,
	NULL,
};

static ssize_t max_odpm_stats_dump_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	ssize_t size = sizeof(struct max_odpm_stats);

	if (off > size)
		return 0;
	if (count > size - off)
		count = size - off;

	memcpy(buf, (const void *)bcl_dev->max_odpm_stats + off, count);

	return count;
}

static struct bin_attribute max_odpm_stats_dump_attr = {
	.attr = { .name = "max_odpm_stats", .mode = 0444 },
	.read = max_odpm_stats_dump_read,
	.size = sizeof(struct max_odpm_stats),
};

static ssize_t br_stats_dump_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off > bcl_dev->br_stats_size)
		return 0;
	if (off + count > bcl_dev->br_stats_size)
		count = bcl_dev->br_stats_size - off;

	memcpy(buf, (const void *)bcl_dev->br_stats + off, count);

	return count;
}

static struct bin_attribute br_stats_dump_attr = {
	.attr = { .name = "stats", .mode = 0444 },
	.read = br_stats_dump_read,
	.size = sizeof(struct brownout_stats),
};

static struct bin_attribute *br_stats_bin_attrs[] = {
	&br_stats_dump_attr,
	&max_odpm_stats_dump_attr,
	NULL,
};

static ssize_t uvlo_dur_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	uint64_t uvlo_dur_ts = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret;

	ret = read_uvlo_dur(bcl_dev, &uvlo_dur_ts);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%lld\n", uvlo_dur_ts);
}
static DEVICE_ATTR_RO(uvlo_dur);

static ssize_t pre_uvlo_hit_cnt_rd(struct device *dev, struct device_attribute *attr, char *buf,
				   int pmic)
{
	uint16_t pre_uvlo_hit_cnt = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret;

	ret = read_pre_uvlo_hit_cnt(bcl_dev, &pre_uvlo_hit_cnt, pmic);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%hu\n", pre_uvlo_hit_cnt);
}

static ssize_t pre_uvlo_hit_cnt_m_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_uvlo_hit_cnt_rd(dev, attr, buf, SYS_EVT_MAIN);
}
static DEVICE_ATTR_RO(pre_uvlo_hit_cnt_m);

static ssize_t pre_uvlo_hit_cnt_s_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_uvlo_hit_cnt_rd(dev, attr, buf, SYS_EVT_SUB);
}
static DEVICE_ATTR_RO(pre_uvlo_hit_cnt_s);

static ssize_t pre_ocp_bckup(struct device *dev, struct device_attribute *attr, char *buf,
				   int rail)
{
	int pre_ocp_bckup = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret;

	ret = read_pre_ocp_bckup(bcl_dev, &pre_ocp_bckup, rail);
	if (ret < 0)
		return ret;

	return sysfs_emit(buf, "%hu\n", pre_ocp_bckup);
}

static ssize_t pre_ocp_cpu1_bckup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
#if IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA) || IS_ENABLED(CONFIG_SOC_MBU)
	return pre_ocp_bckup(dev, attr, buf, CPU1A);
#else
	return pre_ocp_bckup(dev, attr, buf, CPU1);
#endif
}
static DEVICE_ATTR_RO(pre_ocp_cpu1_bckup);

static ssize_t pre_ocp_cpu2_bckup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_ocp_bckup(dev, attr, buf, CPU2);
}
static DEVICE_ATTR_RO(pre_ocp_cpu2_bckup);

static ssize_t pre_ocp_tpu_bckup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_ocp_bckup(dev, attr, buf, TPU);
}
static DEVICE_ATTR_RO(pre_ocp_tpu_bckup);

static ssize_t pre_ocp_gpu_bckup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_ocp_bckup(dev, attr, buf, GPU);
}
static DEVICE_ATTR_RO(pre_ocp_gpu_bckup);

static ssize_t odpm_irq_stat(struct device *dev, struct device_attribute *attr, char *buf, int pmic,
			     int channel)
{
	int odpm_int_bckup = 0;
	int ret;
	u16 type = TELEM_POWER;
	const char *suffix;

	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	ret = read_odpm_int_bckup(bcl_dev, &odpm_int_bckup, &type, pmic, channel);
	if (ret < 0)
		return ret;

	switch (type) {
	case TELEM_VOLTAGE:
		suffix = "mV";
		break;
	case TELEM_CURRENT:
		suffix = "mA";
		break;
	case TELEM_POWER:
		suffix = "mW";
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%i %s\n", odpm_int_bckup, suffix);
}

static ssize_t sys_evt_pmic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->sys_evt_sysfs_pmic);
}

static ssize_t sys_evt_pmic_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (value != SYS_EVT_MAIN && value != SYS_EVT_SUB)
		return -EINVAL;

	bcl_dev->sys_evt_sysfs_pmic = value;
	return size;
}
static DEVICE_ATTR_RW(sys_evt_pmic);

static ssize_t sys_evt_addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->sys_evt_sysfs_addr);
}

static ssize_t sys_evt_addr_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	switch (bcl_dev->sys_evt_sysfs_pmic) {
	case SYS_EVT_MAIN:
		if (value > SYS_EVT_MAX_MAIN)
			return -EINVAL;
		bcl_dev->sys_evt_sysfs_addr = value;
		break;
	case SYS_EVT_SUB:
		if (value > SYS_EVT_MAX_SUB)
			return -EINVAL;
		bcl_dev->sys_evt_sysfs_addr = value;
		break;
	default:
		return -EINVAL;
	}

	return size;
}
static DEVICE_ATTR_RW(sys_evt_addr);

static ssize_t sys_evt_data_show(struct device *dev, struct device_attribute *attr,
					     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	uint8_t *sys_evt_buf;

	switch (bcl_dev->sys_evt_sysfs_pmic) {
	case SYS_EVT_MAIN:
		sys_evt_buf = bcl_dev->sys_evt_main;
		if (bcl_dev->sys_evt_sysfs_addr > SYS_EVT_MAX_MAIN)
			return -EINVAL;
		break;
	case SYS_EVT_SUB:
		sys_evt_buf = bcl_dev->sys_evt_sub;
		if (bcl_dev->sys_evt_sysfs_addr > SYS_EVT_MAX_SUB)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return sysfs_emit(buf, "%#x\n", sys_evt_buf[bcl_dev->sys_evt_sysfs_addr]);
}
static DEVICE_ATTR_RO(sys_evt_data);

static ssize_t sys_evt_main_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off >= SYS_EVT_MAX_MAIN)
		return 0;
	if (off + count > SYS_EVT_MAX_MAIN)
		count = SYS_EVT_MAX_MAIN - off;

	memcpy(buf, (const void *)bcl_dev->sys_evt_main + off, count);

	return count;
}

static ssize_t sys_evt_sub_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off >= SYS_EVT_MAX_SUB)
		return 0;
	if (off + count > SYS_EVT_MAX_SUB)
		count = SYS_EVT_MAX_SUB - off;

	memcpy(buf, (const void *)bcl_dev->sys_evt_sub + off, count);

	return count;
}

static struct bin_attribute sys_evt_main_attr = {
	.attr = { .name = "sys_evt_main", .mode = 0444 },
	.read = sys_evt_main_read,
	.size = SYS_EVT_MAX_MAIN,
};

static struct bin_attribute sys_evt_sub_attr = {
	.attr = { .name = "sys_evt_sub", .mode = 0444 },
	.read = sys_evt_sub_read,
	.size = SYS_EVT_MAX_SUB,
};

static ssize_t cpm_cached_sys_evt_main_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off >= SYS_EVT_MAX_MAIN)
		return 0;
	if (off + count > SYS_EVT_MAX_MAIN)
		count = SYS_EVT_MAX_MAIN - off;

	memcpy(buf, (const void *)bcl_dev->cpm_cached_sys_evt_main + off, count);

	return count;
}

static ssize_t cpm_cached_sys_evt_sub_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off >= SYS_EVT_MAX_SUB)
		return 0;
	if (off + count > SYS_EVT_MAX_SUB)
		count = SYS_EVT_MAX_SUB - off;

	memcpy(buf, (const void *)bcl_dev->cpm_cached_sys_evt_sub + off, count);

	return count;
}

static struct bin_attribute cpm_cached_sys_evt_main_attr = {
	.attr = { .name = "cpm_cached_sys_evt_main", .mode = 0444 },
	.read = cpm_cached_sys_evt_main_read,
	.size = SYS_EVT_MAX_MAIN,
};

static struct bin_attribute cpm_cached_sys_evt_sub_attr = {
	.attr = { .name = "cpm_cached_sys_evt_sub", .mode = 0444 },
	.read = cpm_cached_sys_evt_sub_read,
	.size = SYS_EVT_MAX_SUB,
};

static struct bin_attribute *sys_evt_bin_attrs[] = {
	&sys_evt_main_attr,
	&sys_evt_sub_attr,
	&cpm_cached_sys_evt_main_attr,
	&cpm_cached_sys_evt_sub_attr,
	NULL,
};

#define GEN_ODPM_STAT(pmic, ch) \
static ssize_t odpm_irq_stat_##ch##_##pmic##_bckup_show(struct device *dev, \
							struct device_attribute *attr, char *buf) \
{ \
	return odpm_irq_stat(dev, attr, buf, pmic, ch); \
} \
static DEVICE_ATTR_RO(odpm_irq_stat_##ch##_##pmic##_bckup)

GEN_ODPM_STAT(SYS_EVT_MAIN, 0);
GEN_ODPM_STAT(SYS_EVT_MAIN, 1);
GEN_ODPM_STAT(SYS_EVT_MAIN, 2);
GEN_ODPM_STAT(SYS_EVT_MAIN, 3);
GEN_ODPM_STAT(SYS_EVT_MAIN, 4);
GEN_ODPM_STAT(SYS_EVT_MAIN, 5);
GEN_ODPM_STAT(SYS_EVT_MAIN, 6);
GEN_ODPM_STAT(SYS_EVT_MAIN, 7);
GEN_ODPM_STAT(SYS_EVT_MAIN, 8);
GEN_ODPM_STAT(SYS_EVT_MAIN, 9);
GEN_ODPM_STAT(SYS_EVT_MAIN, 10);
GEN_ODPM_STAT(SYS_EVT_MAIN, 11);
GEN_ODPM_STAT(SYS_EVT_SUB, 0);
GEN_ODPM_STAT(SYS_EVT_SUB, 1);
GEN_ODPM_STAT(SYS_EVT_SUB, 2);
GEN_ODPM_STAT(SYS_EVT_SUB, 3);
GEN_ODPM_STAT(SYS_EVT_SUB, 4);
GEN_ODPM_STAT(SYS_EVT_SUB, 5);
GEN_ODPM_STAT(SYS_EVT_SUB, 6);
GEN_ODPM_STAT(SYS_EVT_SUB, 7);
GEN_ODPM_STAT(SYS_EVT_SUB, 8);
GEN_ODPM_STAT(SYS_EVT_SUB, 9);
GEN_ODPM_STAT(SYS_EVT_SUB, 10);
GEN_ODPM_STAT(SYS_EVT_SUB, 11);

#define GEN_ODPM_STAT_EXT(pmic, ch) \
static ssize_t odpm_irq_stat_ext_##ch##_##pmic##_bckup_show(struct device *dev, \
							struct device_attribute *attr, char *buf) \
{ \
	return odpm_irq_stat(dev, attr, buf, pmic, ch + SYS_EVT_ODPM_M_CH); \
} \
static DEVICE_ATTR_RO(odpm_irq_stat_ext_##ch##_##pmic##_bckup)

GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 0);
GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 1);
GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 2);
GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 3);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 0);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 1);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 2);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 3);

static ssize_t odpm_irq_stat_cpu1_bckup_show(struct device *dev, struct device_attribute *attr,
					     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.cpu1_pmic,
			     bcl_dev->sys_evt_odpm_param.cpu1_ch);
}
static DEVICE_ATTR_RO(odpm_irq_stat_cpu1_bckup);

static ssize_t odpm_irq_stat_cpu2_bckup_show(struct device *dev, struct device_attribute *attr,
					     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.cpu2_pmic,
			     bcl_dev->sys_evt_odpm_param.cpu2_ch);
}
static DEVICE_ATTR_RO(odpm_irq_stat_cpu2_bckup);

static ssize_t odpm_irq_stat_gpu_bckup_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.gpu_pmic,
			     bcl_dev->sys_evt_odpm_param.gpu_ch);
}
static DEVICE_ATTR_RO(odpm_irq_stat_gpu_bckup);

static ssize_t odpm_irq_stat_tpu_bckup_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.tpu_pmic,
			     bcl_dev->sys_evt_odpm_param.tpu_ch);
}
static DEVICE_ATTR_RO(odpm_irq_stat_tpu_bckup);

static struct attribute *sys_evt_attrs[] = {
	&dev_attr_uvlo_dur.attr,
	&dev_attr_pre_uvlo_hit_cnt_m.attr,
	&dev_attr_pre_uvlo_hit_cnt_s.attr,
	&dev_attr_pre_ocp_cpu1_bckup.attr,
	&dev_attr_pre_ocp_cpu2_bckup.attr,
	&dev_attr_pre_ocp_tpu_bckup.attr,
	&dev_attr_pre_ocp_gpu_bckup.attr,
	&dev_attr_odpm_irq_stat_0_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_1_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_2_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_3_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_4_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_5_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_6_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_7_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_8_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_9_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_10_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_11_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_0_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_1_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_2_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_3_SYS_EVT_MAIN_bckup.attr,
	&dev_attr_odpm_irq_stat_0_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_1_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_2_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_3_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_4_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_5_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_6_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_7_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_8_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_9_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_10_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_11_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_0_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_1_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_2_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_ext_3_SYS_EVT_SUB_bckup.attr,
	&dev_attr_odpm_irq_stat_cpu1_bckup.attr,
	&dev_attr_odpm_irq_stat_cpu2_bckup.attr,
	&dev_attr_odpm_irq_stat_gpu_bckup.attr,
	&dev_attr_odpm_irq_stat_tpu_bckup.attr,
	&dev_attr_sys_evt_pmic.attr,
	&dev_attr_sys_evt_addr.attr,
	&dev_attr_sys_evt_data.attr,
	NULL,
};

static const struct attribute_group irq_dur_cnt_group = {
	.attrs = irq_dur_cnt_attrs,
	.name = "irq_dur_cnt",
};

static const struct attribute_group qos_group = {
	.attrs = qos_attrs,
	.name = "qos",
};

static const struct attribute_group br_stats_group = {
	.attrs = br_stats_attrs,
	.bin_attrs = br_stats_bin_attrs,
	.name = "br_stats",
};

static const struct attribute_group irq_config_group = {
	.attrs = irq_config_attrs,
	.name = "irq_config",
};

const struct attribute_group triggered_state_sq_group = {
	.attrs = triggered_state_sq_attrs,
	.name = "triggered_state",
};

const struct attribute_group triggered_state_mw_group = {
	.attrs = triggered_state_mw_attrs,
	.name = "triggered_state",
};

const struct attribute_group sys_evt_group = {
	.attrs = sys_evt_attrs,
	.bin_attrs = sys_evt_bin_attrs,
	.name = "sys_evt",
};

static const struct attribute_group userspace_group = {
	.attrs = userspace_attrs,
	.name = "userspace",
};

const struct attribute_group mitigation_group = {
	.attrs = mitigation_attrs,
	.name = "mitigation",
};

const struct attribute_group *mitigation_mw_groups[] = {
	&instr_group,
	&triggered_lvl_group,
	&triggered_count_group,
	&triggered_timestamp_group,
	&triggered_capacity_group,
	&triggered_voltage_group,
	&br_stats_group,
	&last_triggered_mode_group,
	&irq_config_group,
	&triggered_state_mw_group,
	&clock_div_group,
	&clock_ratio_group,
	&clock_stats_group,
#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	&vdroop_flt_group,
#endif
	&main_pwrwarn_group,
	&sub_pwrwarn_group,
	&irq_dur_cnt_group,
	&qos_group,
	NULL,
};

const struct attribute_group *mitigation_sq_groups[] = {
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188) || IS_ENABLED(CONFIG_SOC_ZUMAPRO)
	&debounce_time_group,
#endif
	&instr_group,
	&triggered_lvl_group,
	&triggered_count_group,
	&triggered_timestamp_group,
	&triggered_capacity_group,
	&triggered_voltage_group,
	&br_stats_group,
	&last_triggered_mode_group,
	&irq_config_group,
	&triggered_state_sq_group,
	&clock_div_group,
	&clock_ratio_group,
	&clock_stats_group,
	&userspace_group,
#if IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	&sys_evt_group,
	&trigger_timer_group,
	&mitigation_res_en_group,
	&mitigation_res_type_group,
	&mitigation_res_hyst_group,
#endif
	&mitigation_group,
#if !IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)
	&vdroop_flt_group,
#endif
#if IS_ENABLED(CONFIG_SOC_MBU)
	&dyn_clk_div_en_group,
	&dyn_clk_div_ratio_group,
	&dyn_clk_div_thresh_group,
#endif
	&main_pwrwarn_group,
	&sub_pwrwarn_group,
	&irq_dur_cnt_group,
	&qos_group,
	NULL,
};
