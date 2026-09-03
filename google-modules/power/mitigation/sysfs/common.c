// SPDX-License-Identifier: GPL-2.0 only
/*
 * common.c Google bcl sysfs driver library functions
 *
 * Copyright (c) 2025 Google LLC
 *
 */
#include <sysfs/common.h>
#include <sysfs/qos.h>
#include "core_pmic/core_pmic_defs.h"
#include "ifpmic/ifpmic_defs.h"
#include "ifpmic/max77779/max77779_irq.h"
#include "soc/soc_defs.h"

static ssize_t safe_emit_bcl_capacity(char *buf, struct bcl_zone *zone)
{
	return zone ? sysfs_emit(buf, "%d\n", zone->bcl_stats.capacity) :
		      -ENODEV;
}

static ssize_t safe_emit_bcl_voltage(char *buf, struct bcl_zone *zone)
{
	return zone ? sysfs_emit(buf, "%d\n", zone->bcl_stats.voltage) :
		      -ENODEV;
}

static ssize_t safe_emit_bcl_time(char *buf, struct bcl_zone *zone)
{
	return zone ? sysfs_emit(buf, "%lld\n", zone->bcl_stats._time) :
		      -ENODEV;
}

static ssize_t batoilo_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[BATOILO1];

	return zone ? sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt)) :
		      -ENODEV;
}

DEVICE_ATTR_RO(batoilo_count);

static ssize_t batoilo2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[BATOILO2];

	return zone ? sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt)) :
		      -ENODEV;
}

DEVICE_ATTR_RO(batoilo2_count);

static ssize_t vdroop2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[UVLO2];

	return zone ? sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt)) :
		      -ENODEV;
}

DEVICE_ATTR_RO(vdroop2_count);

static ssize_t vdroop1_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	struct bcl_zone *zone = bcl_dev->zone[UVLO1];

	return zone ? sysfs_emit(buf, "%d\n", atomic_read(&zone->bcl_cnt)) :
		      -ENODEV;
}

DEVICE_ATTR_RO(vdroop1_count);

static ssize_t smpl_warn_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct bcl_device *bcl_dev = platform_get_drvdata(to_platform_device(dev));

	/* CPM count of PRE_UVLO through get sys_evt is not functioning, use kernel
	 * IRQ count for MBU similar to prior product generation.
	 */

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_UVLO]);
}

DEVICE_ATTR_RO(smpl_warn_count);

static ssize_t ocp_cpu1_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_CPU1]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(ocp_cpu1_count);

static ssize_t ocp_cpu2_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_CPU2]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(ocp_cpu2_count);

static ssize_t ocp_tpu_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_TPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(ocp_tpu_count);

static ssize_t ocp_gpu_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[PRE_OCP_GPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(ocp_gpu_count);

static ssize_t soft_ocp_cpu1_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(soft_ocp_cpu1_count);

static ssize_t soft_ocp_cpu2_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(soft_ocp_cpu2_count);

static ssize_t soft_ocp_tpu_count_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(soft_ocp_tpu_count);

static ssize_t soft_ocp_gpu_count_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_SOC_MBU) && IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188))
		return safe_emit_pre_evt_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);

	return safe_emit_bcl_cnt(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(soft_ocp_gpu_count);

static ssize_t batoilo_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[BATOILO1]);
}

DEVICE_ATTR_RO(batoilo_cap);

static ssize_t batoilo2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[BATOILO2]);
}

DEVICE_ATTR_RO(batoilo2_cap);

static ssize_t vdroop2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[UVLO2]);
}

DEVICE_ATTR_RO(vdroop2_cap);

static ssize_t vdroop1_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[UVLO1]);
}

DEVICE_ATTR_RO(vdroop1_cap);

static ssize_t smpl_warn_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_UVLO]);
}

DEVICE_ATTR_RO(smpl_warn_cap);

static ssize_t ocp_cpu1_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(ocp_cpu1_cap);

static ssize_t ocp_cpu2_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(ocp_cpu2_cap);

static ssize_t ocp_tpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(ocp_tpu_cap);

static ssize_t ocp_gpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(ocp_gpu_cap);

static ssize_t soft_ocp_cpu1_cap_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(soft_ocp_cpu1_cap);

static ssize_t soft_ocp_cpu2_cap_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(soft_ocp_cpu2_cap);

static ssize_t soft_ocp_tpu_cap_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(soft_ocp_tpu_cap);

static ssize_t soft_ocp_gpu_cap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_capacity(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(soft_ocp_gpu_cap);

static ssize_t batoilo_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[BATOILO1]);
}

DEVICE_ATTR_RO(batoilo_volt);

static ssize_t batoilo2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[BATOILO2]);
}

DEVICE_ATTR_RO(batoilo2_volt);

static ssize_t vdroop2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[UVLO2]);
}

DEVICE_ATTR_RO(vdroop2_volt);

static ssize_t vdroop1_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[UVLO1]);
}

DEVICE_ATTR_RO(vdroop1_volt);

static ssize_t smpl_warn_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_UVLO]);
}

DEVICE_ATTR_RO(smpl_warn_volt);

static ssize_t ocp_cpu1_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(ocp_cpu1_volt);

static ssize_t ocp_cpu2_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(ocp_cpu2_volt);

static ssize_t ocp_tpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(ocp_tpu_volt);

static ssize_t ocp_gpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(ocp_gpu_volt);

static ssize_t soft_ocp_cpu1_volt_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(soft_ocp_cpu1_volt);

static ssize_t soft_ocp_cpu2_volt_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(soft_ocp_cpu2_volt);

static ssize_t soft_ocp_tpu_volt_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(soft_ocp_tpu_volt);

static ssize_t soft_ocp_gpu_volt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_voltage(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(soft_ocp_gpu_volt);

static ssize_t batoilo_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[BATOILO1]);
}

DEVICE_ATTR_RO(batoilo_time);

static ssize_t batoilo2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[BATOILO2]);
}

DEVICE_ATTR_RO(batoilo2_time);

static ssize_t vdroop2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[UVLO2]);
}

DEVICE_ATTR_RO(vdroop2_time);

static ssize_t vdroop1_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[UVLO1]);
}

DEVICE_ATTR_RO(vdroop1_time);

static ssize_t smpl_warn_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_UVLO]);
}

DEVICE_ATTR_RO(smpl_warn_time);

static ssize_t ocp_cpu1_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(ocp_cpu1_time);

static ssize_t ocp_cpu2_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(ocp_cpu2_time);

static ssize_t ocp_tpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(ocp_tpu_time);

static ssize_t ocp_gpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(ocp_gpu_time);

static ssize_t soft_ocp_cpu1_time_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU1]);
}

DEVICE_ATTR_RO(soft_ocp_cpu1_time);

static ssize_t soft_ocp_cpu2_time_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_CPU2]);
}

DEVICE_ATTR_RO(soft_ocp_cpu2_time);

static ssize_t soft_ocp_tpu_time_show(struct device *dev, struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_TPU]);
}

DEVICE_ATTR_RO(soft_ocp_tpu_time);

static ssize_t soft_ocp_gpu_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return safe_emit_bcl_time(buf, bcl_dev->zone[SOFT_PRE_OCP_GPU]);
}

DEVICE_ATTR_RO(soft_ocp_gpu_time);

static ssize_t db_settings_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t size, enum MPMM_SOURCE src)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int value;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

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

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

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

DEVICE_ATTR_RW(mid_db_settings);

static ssize_t big_db_settings_store(struct device *dev,
				     struct device_attribute *attr, const char *buf, size_t size)
{
	return db_settings_store(dev, attr, buf, size, BIG);
}

static ssize_t big_db_settings_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return db_settings_show(dev, attr, buf, BIG);
}

DEVICE_ATTR_RW(big_db_settings);

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

DEVICE_ATTR_RW(enable_sw_mitigation);

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

DEVICE_ATTR_RW(enable_hw_mitigation);

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

	bcl_dev->rffe_mitigation_enable = value;
	return size;
}

DEVICE_ATTR_RW(enable_rffe_mitigation);

static ssize_t main_offsrc1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->main_offsrc1);
}

DEVICE_ATTR_RO(main_offsrc1);

static ssize_t main_offsrc2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->main_offsrc2);
}

DEVICE_ATTR_RO(main_offsrc2);

static ssize_t sub_offsrc1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->sub_offsrc1);
}

DEVICE_ATTR_RO(sub_offsrc1);

static ssize_t sub_offsrc2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->sub_offsrc2);
}

DEVICE_ATTR_RO(sub_offsrc2);

static ssize_t evt_cnt_uvlo1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.uvlo1);
}

DEVICE_ATTR_RO(evt_cnt_uvlo1);

static ssize_t evt_cnt_uvlo2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.uvlo2);
}

DEVICE_ATTR_RO(evt_cnt_uvlo2);

static ssize_t evt_cnt_batoilo1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.batoilo1);
}

DEVICE_ATTR_RO(evt_cnt_batoilo1);

static ssize_t evt_cnt_batoilo2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt.batoilo2);
}

DEVICE_ATTR_RO(evt_cnt_batoilo2);

static ssize_t evt_cnt_latest_uvlo1_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.uvlo1);
}

DEVICE_ATTR_RO(evt_cnt_latest_uvlo1);

static ssize_t evt_cnt_latest_uvlo2_show(struct device *dev, struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.uvlo2);
}

DEVICE_ATTR_RO(evt_cnt_latest_uvlo2);

static ssize_t evt_cnt_latest_batoilo1_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.batoilo1);
}

DEVICE_ATTR_RO(evt_cnt_latest_batoilo1);

static ssize_t evt_cnt_latest_batoilo2_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%i\n", bcl_dev->evt_cnt_latest.batoilo2);
}

DEVICE_ATTR_RO(evt_cnt_latest_batoilo2);

static ssize_t pwronsrc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%#x\n", bcl_dev->pwronsrc);
}

DEVICE_ATTR_RO(pwronsrc);

static ssize_t last_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (bcl_dev->ifpmic != MAX77779)
		return -ENODEV;

	return sysfs_emit(buf, "%#x\n", bcl_dev->last_current);
}
DEVICE_ATTR_RO(last_current);

static ssize_t vimon_buff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int idx, ret;
	uint16_t rdback;
	ssize_t count = 0;

	if (bcl_dev->ifpmic != MAX77779)
		return -ENODEV;

	if (!bcl_dev->ifpmic_ops)
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
DEVICE_ATTR_RO(vimon_buff);

static ssize_t ready_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", READ_ONCE(bcl_dev->initialized));
}
DEVICE_ATTR_RO(ready);

static ssize_t ifpmic_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "max777%c9\n", bcl_dev->ifpmic == MAX77779 ? '7' : '5');
}
DEVICE_ATTR_RO(ifpmic);

static ssize_t bcl_version_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s\n", BCL_VERSION);
}
DEVICE_ATTR_RO(bcl_version);

static ssize_t uvlo1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int uvlo1_lvl;

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
	if (!bcl_dev->zone[UVLO1]) {
		dev_err(bcl_dev->device, "UVLO1 is disabled\n");
		return -EIO;
	}
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->set_uvlo1(bcl_dev->ifpmic_irq_dev, value);
	if (ret)
		return ret;

	return size;
}

DEVICE_ATTR_RW(uvlo1_lvl);

static ssize_t uvlo2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int uvlo2_lvl;

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

DEVICE_ATTR_RW(uvlo2_lvl);

static ssize_t batoilo1_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int batoilo1_lvl;

	if (!bcl_dev->zone[BATOILO1])
		return -EIO;
	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->get_oilo1(bcl_dev->ifpmic_irq_dev, &batoilo1_lvl);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%umA\n", batoilo1_lvl);
}

static ssize_t batoilo1_lvl_store(struct device *dev,
				  struct device_attribute *attr, const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int value;
	int ret;

	if (!bcl_dev->zone[BATOILO1])
		return -EIO;

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

DEVICE_ATTR_RW(batoilo1_lvl);

static ssize_t batoilo2_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int batoilo2_lvl;

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

	if (!bcl_dev->zone[BATOILO2])
		return -EIO;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (!bcl_dev->ifpmic_irq_dev)
		return -EBUSY;

	ret = bcl_dev->ifpmic_ops->set_oilo2(bcl_dev->ifpmic_irq_dev, value);
	if (ret)
		return ret;

	return size;
}

DEVICE_ATTR_RW(batoilo2_lvl);

static ssize_t smpl_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	unsigned int smpl_warn_lvl;

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

	return core_pmic_main_store_uvlo(bcl_dev, val, size);
}

DEVICE_ATTR_RW(smpl_lvl);

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

DEVICE_ATTR_RW(ocp_cpu1_lvl);

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

DEVICE_ATTR_RW(ocp_cpu2_lvl);

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

DEVICE_ATTR_RW(ocp_tpu_lvl);

static ssize_t ocp_gpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;
	int ret;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)) {
		ret = core_pmic_main_get_ocp_lvl(bcl_dev, &val, PRE_OCP_GPU, GPU_UPPER_LIMIT,
						 GPU_STEP);
	} else {
		ret = core_pmic_sub_get_ocp_lvl(bcl_dev, &val, PRE_OCP_GPU, GPU_UPPER_LIMIT,
						GPU_STEP);
	}

	if (ret < 0)
		return -EINVAL;

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
		ret = core_pmic_main_set_ocp_lvl(bcl_dev, value, PRE_OCP_GPU, GPU_LOWER_LIMIT,
						 GPU_UPPER_LIMIT, GPU_STEP);
	} else {
		ret = core_pmic_sub_set_ocp_lvl(bcl_dev, value, PRE_OCP_GPU, GPU_LOWER_LIMIT,
						GPU_UPPER_LIMIT, GPU_STEP);
	}

	if (ret < 0)
		return -EINVAL;

	return size;
}

DEVICE_ATTR_RW(ocp_gpu_lvl);

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

DEVICE_ATTR_RW(soft_ocp_cpu1_lvl);

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

DEVICE_ATTR_RW(soft_ocp_cpu2_lvl);

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
	unsigned int lower_limit, upper_limit;
	int ret;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)) {
		lower_limit = TPU_SOFT_LOWER_LIMIT;
		upper_limit = TPU_SOFT_UPPER_LIMIT;
	} else {
		lower_limit = TPU_LOWER_LIMIT;
		upper_limit = TPU_UPPER_LIMIT;
	}
	ret = core_pmic_main_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_TPU,
					 lower_limit, upper_limit, TPU_STEP);
	if (ret < 0)
		return -EINVAL;

	return size;
}

DEVICE_ATTR_RW(soft_ocp_tpu_lvl);

static ssize_t soft_ocp_gpu_lvl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	u64 val;
	int ret;

	if (IS_ENABLED(CONFIG_GOOGLE_MFD_DA9188)) {
		ret = core_pmic_main_get_ocp_lvl(bcl_dev, &val, SOFT_PRE_OCP_GPU, GPU_UPPER_LIMIT,
						 GPU_STEP);
	} else {
		ret = core_pmic_sub_get_ocp_lvl(bcl_dev, &val, SOFT_PRE_OCP_GPU, GPU_UPPER_LIMIT,
						GPU_STEP);
	}

	if (ret < 0)
		return -EINVAL;

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
		ret = core_pmic_main_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_GPU, GPU_LOWER_LIMIT,
						 GPU_UPPER_LIMIT, GPU_STEP);
	} else {
		ret = core_pmic_sub_set_ocp_lvl(bcl_dev, value, SOFT_PRE_OCP_GPU, GPU_LOWER_LIMIT,
						GPU_UPPER_LIMIT, GPU_STEP);
	}
	if (ret < 0)
		return -EINVAL;

	return size;
}

DEVICE_ATTR_RW(soft_ocp_gpu_lvl);

ssize_t clk_div_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	return get_clk_div(bcl_dev, idx, buf);
}

ssize_t clk_div_store(struct bcl_device *bcl_dev, int idx,
			     const char *buf, size_t size)
{
	return set_clk_div(bcl_dev, idx, buf, size);
}

ssize_t clk_ratio_show(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx, char *buf,
			      int sub_idx)
{
	return get_clk_ratio(bcl_dev, idx, buf, sub_idx);
}

ssize_t clk_ratio_store(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
			       const char *buf, size_t size, int sub_idx)
{
	return set_clk_ratio(bcl_dev, idx, buf, size, sub_idx);
}

static ssize_t last_triggered_cnt(struct bcl_zone *zone, char *buf, int mode)
{
	if (mode < 0 || mode >= MAX_MITIGATION_MODE)
		return -EINVAL;

	return sysfs_emit(
		buf, "%d\n",
		atomic_read(&zone->last_triggered.triggered_cnt[mode]));
}

static ssize_t last_triggered_time(struct bcl_zone *zone, char *buf, int mode)
{
	if (mode < 0 || mode >= MAX_MITIGATION_MODE)
		return -EINVAL;

	return sysfs_emit(buf, "%lld\n",
			  zone->last_triggered.triggered_time[mode]);
}

static ssize_t last_triggered_uvlo1_heavy_cnt_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_heavy_cnt);

static ssize_t last_triggered_uvlo1_medium_cnt_show(struct device *dev,
							     struct device_attribute *attr,
							     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_medium_cnt);

static ssize_t last_triggered_uvlo1_light_cnt_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, light);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_light_cnt);

static ssize_t last_triggered_uvlo1_start_cnt_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO1], buf, START);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_start_cnt);

static ssize_t last_triggered_uvlo1_heavy_time_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_heavy_time);

static ssize_t last_triggered_uvlo1_medium_time_show(struct device *dev,
							      struct device_attribute *attr,
							      char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_medium_time);

static ssize_t last_triggered_uvlo1_light_time_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, light);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_light_time);

static ssize_t last_triggered_uvlo1_start_time_show(struct device *dev,
						    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO1], buf, START);
}

DEVICE_ATTR_RO(last_triggered_uvlo1_start_time);

static ssize_t last_triggered_uvlo2_heavy_cnt_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_heavy_cnt);

static ssize_t last_triggered_uvlo2_medium_cnt_show(struct device *dev,
							     struct device_attribute *attr,
							     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_medium_cnt);

static ssize_t last_triggered_uvlo2_light_cnt_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, light);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_light_cnt);

static ssize_t last_triggered_uvlo2_start_cnt_show(struct device *dev,
						   struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[UVLO2], buf, START);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_start_cnt);

static ssize_t last_triggered_uvlo2_heavy_time_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_heavy_time);

static ssize_t last_triggered_uvlo2_medium_time_show(struct device *dev,
							      struct device_attribute *attr,
							      char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_medium_time);

static ssize_t last_triggered_uvlo2_light_time_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, light);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_light_time);

static ssize_t last_triggered_uvlo2_start_time_show(struct device *dev,
						    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[UVLO2], buf, START);
}

DEVICE_ATTR_RO(last_triggered_uvlo2_start_time);

static ssize_t last_triggered_batoilo2_heavy_cnt_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_heavy_cnt);

static ssize_t last_triggered_batoilo2_medium_cnt_show(struct device *dev,
								struct device_attribute *attr,
								char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_medium_cnt);

static ssize_t last_triggered_batoilo2_light_cnt_show(struct device *dev,
							  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, light);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_light_cnt);

static ssize_t last_triggered_batoilo2_start_cnt_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO2], buf, START);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_start_cnt);

static ssize_t last_triggered_batoilo2_heavy_time_show(struct device *dev,
							  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_heavy_time);

static ssize_t last_triggered_batoilo2_medium_time_show(struct device *dev,
								 struct device_attribute *attr,
								 char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_medium_time);

static ssize_t last_triggered_batoilo2_light_time_show(struct device *dev,
							   struct device_attribute *attr,
							   char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, light);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_light_time);

static ssize_t last_triggered_batoilo2_start_time_show(struct device *dev,
						       struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO2], buf, START);
}

DEVICE_ATTR_RO(last_triggered_batoilo2_start_time);

static ssize_t last_triggered_batoilo_heavy_cnt_show(struct device *dev,
							struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_batoilo_heavy_cnt);

static ssize_t last_triggered_batoilo_medium_cnt_show(struct device *dev,
							       struct device_attribute *attr,
							       char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_batoilo_medium_cnt);

static ssize_t last_triggered_batoilo_light_cnt_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, light);
}

DEVICE_ATTR_RO(last_triggered_batoilo_light_cnt);

static ssize_t last_triggered_batoilo_start_cnt_show(struct device *dev,
						     struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_cnt(bcl_dev->zone[BATOILO], buf, START);
}

DEVICE_ATTR_RO(last_triggered_batoilo_start_cnt);

static ssize_t last_triggered_batoilo_heavy_time_show(struct device *dev,
							 struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, heavy);
}

DEVICE_ATTR_RO(last_triggered_batoilo_heavy_time);

static ssize_t last_triggered_batoilo_medium_time_show(struct device *dev,
								struct device_attribute *attr,
								char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, MEDIUM);
}

DEVICE_ATTR_RO(last_triggered_batoilo_medium_time);

static ssize_t last_triggered_batoilo_light_time_show(struct device *dev,
							  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, light);
}

DEVICE_ATTR_RO(last_triggered_batoilo_light_time);

static ssize_t last_triggered_batoilo_start_time_show(struct device *dev,
						      struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return last_triggered_time(bcl_dev->zone[BATOILO], buf, START);
}

DEVICE_ATTR_RO(last_triggered_batoilo_start_time);

ssize_t main_pwrwarn_threshold_show(struct device *dev, struct device_attribute *attr,
					   char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	ret = sscanf(attr->attr.name, "main_pwrwarn_threshold%d", &idx);
	if (ret != 1 || idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d=%lld\n", bcl_dev->main_setting[idx], bcl_dev->main_limit[idx]);
}

ssize_t main_pwrwarn_threshold_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx, value;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;
	ret = sscanf(attr->attr.name, "main_pwrwarn_threshold%d", &idx);
	if (ret != 1 || idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	bcl_dev->main_setting[idx] = value;
	bcl_dev->main_limit[idx] = settings_to_current(bcl_dev, CORE_PMIC_MAIN, idx, value);
	meter_write(CORE_PMIC_MAIN, bcl_dev, idx, value);

	return size;
}

ssize_t sub_pwrwarn_threshold_show(struct device *dev, struct device_attribute *attr,
					  char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	ret = sscanf(attr->attr.name, "sub_pwrwarn_threshold%d", &idx);
	if (ret != 1 || idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	return sysfs_emit(buf, "%d=%lld\n", bcl_dev->sub_setting[idx], bcl_dev->sub_limit[idx]);
}

ssize_t sub_pwrwarn_threshold_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);
	int ret, idx, value;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	ret = kstrtou32(buf, 10, &value);
	if (ret)
		return ret;
	ret = sscanf(attr->attr.name, "sub_pwrwarn_threshold%d", &idx);
	if (ret != 1 || idx >= METER_CHANNEL_MAX || idx < 0)
		return -EINVAL;

	bcl_dev->sub_setting[idx] = value;
	bcl_dev->sub_limit[idx] = settings_to_current(bcl_dev, CORE_PMIC_SUB, idx, value);
	meter_write(CORE_PMIC_SUB, bcl_dev, idx, value);

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

DEVICE_ATTR_RW(qos_batoilo2);
DEVICE_ATTR_RW(qos_batoilo);
DEVICE_ATTR_RW(qos_vdroop1);
DEVICE_ATTR_RW(qos_vdroop2);
DEVICE_ATTR_RW(qos_smpl_warn);
DEVICE_ATTR_RW(qos_ocp_cpu2);
DEVICE_ATTR_RW(qos_ocp_cpu1);
DEVICE_ATTR_RW(qos_ocp_gpu);
DEVICE_ATTR_RW(qos_ocp_tpu);

/* TODO: b/440450311  Relocate power warning counter*/
static ssize_t less_than_5ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.lt_5ms_count);
			count += sysfs_emit_at(buf, count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx].lt_5ms_count);
		count += sysfs_emit_at(buf, count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx].lt_5ms_count);
		count += sysfs_emit_at(buf, count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

DEVICE_ATTR_RO(less_than_5ms_count);

/* TODO: b/440450311  Relocate power warning counter*/
static ssize_t between_5ms_to_10ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.bt_5ms_10ms_count);
			count += sysfs_emit_at(buf, count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx]
					.bt_5ms_10ms_count);
		count += sysfs_emit_at(buf, count,
					"main CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->main_rail_names[pwrwarn_idx],
					irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		irq_count = atomic_read(&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx]
					.bt_5ms_10ms_count);
		count += sysfs_emit_at(buf, count,
					"sub CH%d[%s]: %i\n",
					pwrwarn_idx,
					bcl_dev->sub_rail_names[pwrwarn_idx],
					irq_count);
	}
	return count;
}

DEVICE_ATTR_RO(between_5ms_to_10ms_count);

/* TODO: b/440450311  Relocate power warning counter*/
static ssize_t greater_than_10ms_count_show(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int irq_count, batt_idx, pwrwarn_idx;
	ssize_t count = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	for (batt_idx = 0; batt_idx < MAX_BCL_BATT_IRQ; batt_idx++) {
		for (pwrwarn_idx = 0; pwrwarn_idx < MAX_CONCURRENT_PWRWARN_IRQ; pwrwarn_idx++) {
			irq_count = atomic_read(&bcl_dev->ifpmic_irq_bins[batt_idx][pwrwarn_idx]
						.gt_10ms_count);
			count += sysfs_emit_at(buf, count,
						"%s + %s: %i\n",
						batt_irq_names[batt_idx],
						concurrent_pwrwarn_irq_names[pwrwarn_idx],
						irq_count);
		}
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		if (IS_ENABLED(CONFIG_REGULATOR_S2MPG14)) {
			irq_count = atomic_read(
				&bcl_dev->pwrwarn_main_irq_bins[pwrwarn_idx].gt_10ms_count);
		}
		if (IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA))
			irq_count = core_pmic_read_main_pwrwarn(bcl_dev, pwrwarn_idx);

		count += sysfs_emit_at(buf, count, "main CH%d[%s]: %i\n",
				   pwrwarn_idx, bcl_dev->main_rail_names[pwrwarn_idx], irq_count);
	}
	for (pwrwarn_idx = 0; pwrwarn_idx < METER_CHANNEL_MAX; pwrwarn_idx++) {
		if (IS_ENABLED(CONFIG_REGULATOR_S2MPG14)) {
			irq_count = atomic_read(
				&bcl_dev->pwrwarn_sub_irq_bins[pwrwarn_idx].gt_10ms_count);
		}
		if (IS_ENABLED(CONFIG_SOC_RDO) || IS_ENABLED(CONFIG_SOC_LGA))
			irq_count = core_pmic_read_sub_pwrwarn(bcl_dev, pwrwarn_idx);

		count += sysfs_emit_at(buf, count, "sub CH%d[%s]: %i\n",
				   pwrwarn_idx, bcl_dev->sub_rail_names[pwrwarn_idx], irq_count);
	}
	return count;
}

DEVICE_ATTR_RO(greater_than_10ms_count);

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

DEVICE_ATTR_RW(uvlo1_disabled);
DEVICE_ATTR_RW(uvlo2_disabled);
DEVICE_ATTR_RW(batoilo_disabled);
DEVICE_ATTR_RW(batoilo2_disabled);
DEVICE_ATTR_RW(smpl_disabled);

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
DEVICE_ATTR_RO(oilo1_triggered);
DEVICE_ATTR_RO(oilo2_triggered);
DEVICE_ATTR_RO(uvlo1_triggered);
DEVICE_ATTR_RO(uvlo2_triggered);
DEVICE_ATTR_RO(smpl_triggered);

static void bunch_mitigation_threshold_addr(struct bcl_mitigation_conf *mitigation_conf,
					    unsigned int *addr[METER_CHANNEL_MAX])
{
	int i;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return;

	for (i = 0; i < METER_CHANNEL_MAX; i++)
		addr[i] = &mitigation_conf[i].threshold;
}

static void bunch_mitigation_module_id_addr(struct bcl_mitigation_conf *mitigation_conf,
					    unsigned int *addr[METER_CHANNEL_MAX])
{
	int i;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return;

	for (i = 0; i < METER_CHANNEL_MAX; i++)
		addr[i] = &mitigation_conf[i].module_id;
}

static ssize_t mitigation_show(unsigned int *addr[METER_CHANNEL_MAX], char *buf)
{
	int i, at = 0;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	for (i = 0; i < METER_CHANNEL_MAX; i++)
		at += sysfs_emit_at(buf, at, "%d,", *addr[i]);

	return at;
}

static ssize_t mitigation_store(unsigned int *addr[METER_CHANNEL_MAX],
				const char *buf, size_t size)
{
	int i;
	unsigned int ch[METER_CHANNEL_MAX] = { 0 };
	char *str;
	char *sep_str;
	char *token = NULL;

	if (IS_ENABLED(CONFIG_REGULATOR_S2MPG12) || IS_ENABLED(CONFIG_REGULATOR_S2MPG10))
		return -ENODEV;

	str = kstrndup(buf, size, GFP_KERNEL);
	if (!str)
		return -ENOMEM;

	sep_str = str;

	for (i = 0; i < METER_CHANNEL_MAX; i++) {
		token = strsep(&sep_str, MITIGATION_INPUT_DELIM);
		if (!token || kstrtoint(token, 10, &ch[i]))
			break;
		*addr[i] = ch[i];
	}

	kfree(str);

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

DEVICE_ATTR_RW(main_mitigation_threshold);
DEVICE_ATTR_RW(sub_mitigation_threshold);
DEVICE_ATTR_RW(main_mitigation_module_id);
DEVICE_ATTR_RW(sub_mitigation_module_id);

static ssize_t triggered_idx_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->triggered_idx);
}

DEVICE_ATTR_RO(triggered_idx);

static ssize_t enable_br_stats_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return sysfs_emit(buf, "%d\n", bcl_dev->enabled_br_stats);
}

static ssize_t enable_br_stats_store(struct device *dev,
				     struct device_attribute *attr,
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

DEVICE_ATTR_RW(enable_br_stats);

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

DEVICE_ATTR_WO(trigger_br_stats);

static ssize_t meter_channels_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", METER_CHANNEL_MAX);
}
DEVICE_ATTR_RO(meter_channels);

ssize_t br_stats_dump_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off > bcl_dev->br_stats_size)
		return 0;
	if (count > bcl_dev->br_stats_size - off)
		count = bcl_dev->br_stats_size - off;

	memcpy(buf, (const void *)bcl_dev->br_stats + off, count);

	return count;
}

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
DEVICE_ATTR_RO(uvlo_dur);

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
DEVICE_ATTR_RO(pre_uvlo_hit_cnt_m);

static ssize_t pre_uvlo_hit_cnt_s_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_uvlo_hit_cnt_rd(dev, attr, buf, SYS_EVT_SUB);
}
DEVICE_ATTR_RO(pre_uvlo_hit_cnt_s);

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
	return pre_ocp_bckup(dev, attr, buf, 1);
}
DEVICE_ATTR_RO(pre_ocp_cpu1_bckup);

static ssize_t pre_ocp_cpu2_bckup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_ocp_bckup(dev, attr, buf, CPU2);
}
DEVICE_ATTR_RO(pre_ocp_cpu2_bckup);

static ssize_t pre_ocp_tpu_bckup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_ocp_bckup(dev, attr, buf, TPU);
}
DEVICE_ATTR_RO(pre_ocp_tpu_bckup);

static ssize_t pre_ocp_gpu_bckup_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return pre_ocp_bckup(dev, attr, buf, GPU);
}
DEVICE_ATTR_RO(pre_ocp_gpu_bckup);

ssize_t odpm_irq_stat(struct device *dev, struct device_attribute *attr, char *buf, int pmic,
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
DEVICE_ATTR_RW(sys_evt_pmic);

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
DEVICE_ATTR_RW(sys_evt_addr);

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
DEVICE_ATTR_RO(sys_evt_data);

ssize_t sys_evt_main_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off >= SYS_EVT_MAX_MAIN)
		return 0;
	if (count > SYS_EVT_MAX_MAIN - off)
		count = SYS_EVT_MAX_MAIN - off;

	memcpy(buf, (const void *)bcl_dev->sys_evt_main + off, count);

	return count;
}

ssize_t sys_evt_sub_read(struct file *filp,
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

ssize_t cpm_cached_sys_evt_main_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	if (off >= SYS_EVT_MAX_MAIN)
		return 0;
	if (count > SYS_EVT_MAX_MAIN - off)
		count = SYS_EVT_MAX_MAIN - off;

	memcpy(buf, (const void *)bcl_dev->cpm_cached_sys_evt_main + off, count);

	return count;
}

ssize_t cpm_cached_sys_evt_sub_read(struct file *filp,
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

static ssize_t odpm_irq_stat_cpu1_bckup_show(struct device *dev, struct device_attribute *attr,
					     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.cpu1_pmic,
			     bcl_dev->sys_evt_odpm_param.cpu1_ch);
}
DEVICE_ATTR_RO(odpm_irq_stat_cpu1_bckup);

static ssize_t odpm_irq_stat_cpu2_bckup_show(struct device *dev, struct device_attribute *attr,
					     char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.cpu2_pmic,
			     bcl_dev->sys_evt_odpm_param.cpu2_ch);
}
DEVICE_ATTR_RO(odpm_irq_stat_cpu2_bckup);

static ssize_t odpm_irq_stat_gpu_bckup_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.gpu_pmic,
			     bcl_dev->sys_evt_odpm_param.gpu_ch);
}
DEVICE_ATTR_RO(odpm_irq_stat_gpu_bckup);

static ssize_t odpm_irq_stat_tpu_bckup_show(struct device *dev, struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct bcl_device *bcl_dev = platform_get_drvdata(pdev);

	return odpm_irq_stat(dev, attr, buf, bcl_dev->sys_evt_odpm_param.tpu_pmic,
			     bcl_dev->sys_evt_odpm_param.tpu_ch);
}
DEVICE_ATTR_RO(odpm_irq_stat_tpu_bckup);

ssize_t max_odpm_stats_dump_read(struct file *filp,
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
