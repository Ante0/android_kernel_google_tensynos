/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __BCL_SYSFS_COMMON_H
#define __BCL_SYSFS_COMMON_H

#include <linux/kernel.h>
#include "bcl.h"

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

#define DEVICE_PWRWARN_ATTR(_name, _num)             \
	struct device_attribute attr_##_name##_num = \
		__ATTR(_name##_num, 0644, _name##_show, _name##_store)

#define GEN_ODPM_STAT(pmic, ch)                                               \
	static ssize_t odpm_irq_stat_##ch##_##pmic##_bckup_show(              \
		struct device *dev, struct device_attribute *attr, char *buf) \
	{                                                                     \
		return odpm_irq_stat(dev, attr, buf, pmic, ch);               \
	}                                                                     \
	static DEVICE_ATTR_RO(odpm_irq_stat_##ch##_##pmic##_bckup)

#define GEN_ODPM_STAT_EXT(pmic, ch) \
static ssize_t odpm_irq_stat_ext_##ch##_##pmic##_bckup_show(struct device *dev, \
							struct device_attribute *attr, char *buf) \
{ \
	return odpm_irq_stat(dev, attr, buf, pmic, ch + SYS_EVT_ODPM_M_CH); \
} \
static DEVICE_ATTR_RO(odpm_irq_stat_ext_##ch##_##pmic##_bckup)

static const char * const batt_irq_names[] = {
	"uvlo1", "uvlo2", "batoilo", "batoilo2"
};

static const char * const concurrent_pwrwarn_irq_names[] = {
	"none", "mmwave", "rffe"
};

#define DEVICE_ATTR_HEADER(_name) \
	extern struct device_attribute dev_attr_##_name

DEVICE_ATTR_HEADER(mid_db_settings);
DEVICE_ATTR_HEADER(batoilo_count);
DEVICE_ATTR_HEADER(batoilo2_count);
DEVICE_ATTR_HEADER(vdroop2_count);
DEVICE_ATTR_HEADER(vdroop1_count);
DEVICE_ATTR_HEADER(smpl_warn_count);
DEVICE_ATTR_HEADER(ocp_cpu1_count);
DEVICE_ATTR_HEADER(ocp_cpu2_count);
DEVICE_ATTR_HEADER(ocp_tpu_count);
DEVICE_ATTR_HEADER(ocp_gpu_count);
DEVICE_ATTR_HEADER(soft_ocp_cpu1_count);
DEVICE_ATTR_HEADER(soft_ocp_cpu2_count);
DEVICE_ATTR_HEADER(soft_ocp_tpu_count);
DEVICE_ATTR_HEADER(soft_ocp_gpu_count);
DEVICE_ATTR_HEADER(batoilo_cap);
DEVICE_ATTR_HEADER(batoilo2_cap);
DEVICE_ATTR_HEADER(vdroop2_cap);
DEVICE_ATTR_HEADER(vdroop1_cap);
DEVICE_ATTR_HEADER(smpl_warn_cap);
DEVICE_ATTR_HEADER(ocp_cpu1_cap);
DEVICE_ATTR_HEADER(ocp_cpu2_cap);
DEVICE_ATTR_HEADER(ocp_tpu_cap);
DEVICE_ATTR_HEADER(ocp_gpu_cap);
DEVICE_ATTR_HEADER(soft_ocp_cpu1_cap);
DEVICE_ATTR_HEADER(soft_ocp_cpu2_cap);
DEVICE_ATTR_HEADER(soft_ocp_tpu_cap);
DEVICE_ATTR_HEADER(soft_ocp_gpu_cap);
DEVICE_ATTR_HEADER(batoilo_volt);
DEVICE_ATTR_HEADER(batoilo2_volt);
DEVICE_ATTR_HEADER(vdroop2_volt);
DEVICE_ATTR_HEADER(vdroop1_volt);
DEVICE_ATTR_HEADER(smpl_warn_volt);
DEVICE_ATTR_HEADER(ocp_cpu1_volt);
DEVICE_ATTR_HEADER(ocp_cpu2_volt);
DEVICE_ATTR_HEADER(ocp_tpu_volt);
DEVICE_ATTR_HEADER(ocp_gpu_volt);
DEVICE_ATTR_HEADER(soft_ocp_cpu1_volt);
DEVICE_ATTR_HEADER(soft_ocp_cpu2_volt);
DEVICE_ATTR_HEADER(soft_ocp_tpu_volt);
DEVICE_ATTR_HEADER(soft_ocp_gpu_volt);
DEVICE_ATTR_HEADER(batoilo_time);
DEVICE_ATTR_HEADER(batoilo2_time);
DEVICE_ATTR_HEADER(vdroop2_time);
DEVICE_ATTR_HEADER(vdroop1_time);
DEVICE_ATTR_HEADER(smpl_warn_time);
DEVICE_ATTR_HEADER(ocp_cpu1_time);
DEVICE_ATTR_HEADER(ocp_cpu2_time);
DEVICE_ATTR_HEADER(ocp_tpu_time);
DEVICE_ATTR_HEADER(ocp_gpu_time);
DEVICE_ATTR_HEADER(soft_ocp_cpu1_time);
DEVICE_ATTR_HEADER(soft_ocp_cpu2_time);
DEVICE_ATTR_HEADER(soft_ocp_tpu_time);
DEVICE_ATTR_HEADER(soft_ocp_gpu_time);
DEVICE_ATTR_HEADER(big_db_settings);
DEVICE_ATTR_HEADER(enable_sw_mitigation);
DEVICE_ATTR_HEADER(enable_hw_mitigation);
DEVICE_ATTR_HEADER(enable_rffe_mitigation);
DEVICE_ATTR_HEADER(main_offsrc1);
DEVICE_ATTR_HEADER(main_offsrc2);
DEVICE_ATTR_HEADER(sub_offsrc1);
DEVICE_ATTR_HEADER(sub_offsrc2);
DEVICE_ATTR_HEADER(evt_cnt_uvlo1);
DEVICE_ATTR_HEADER(evt_cnt_uvlo2);
DEVICE_ATTR_HEADER(evt_cnt_batoilo1);
DEVICE_ATTR_HEADER(evt_cnt_batoilo2);
DEVICE_ATTR_HEADER(evt_cnt_latest_uvlo1);
DEVICE_ATTR_HEADER(evt_cnt_latest_uvlo2);
DEVICE_ATTR_HEADER(evt_cnt_latest_batoilo1);
DEVICE_ATTR_HEADER(evt_cnt_latest_batoilo2);
DEVICE_ATTR_HEADER(pwronsrc);
DEVICE_ATTR_HEADER(last_current);
DEVICE_ATTR_HEADER(vimon_buff);
DEVICE_ATTR_HEADER(ready);
DEVICE_ATTR_HEADER(ifpmic);
DEVICE_ATTR_HEADER(bcl_version);
DEVICE_ATTR_HEADER(uvlo1_lvl);
DEVICE_ATTR_HEADER(uvlo2_lvl);
DEVICE_ATTR_HEADER(batoilo1_lvl);
DEVICE_ATTR_HEADER(batoilo2_lvl);
DEVICE_ATTR_HEADER(smpl_lvl);
DEVICE_ATTR_HEADER(ocp_cpu1_lvl);
DEVICE_ATTR_HEADER(ocp_cpu2_lvl);
DEVICE_ATTR_HEADER(ocp_tpu_lvl);
DEVICE_ATTR_HEADER(ocp_gpu_lvl);
DEVICE_ATTR_HEADER(soft_ocp_cpu1_lvl);
DEVICE_ATTR_HEADER(soft_ocp_cpu2_lvl);
DEVICE_ATTR_HEADER(soft_ocp_tpu_lvl);
DEVICE_ATTR_HEADER(soft_ocp_gpu_lvl);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_heavy_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_medium_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_light_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_start_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_heavy_time);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_medium_time);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_light_time);
DEVICE_ATTR_HEADER(last_triggered_uvlo1_start_time);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_heavy_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_medium_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_light_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_start_cnt);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_heavy_time);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_medium_time);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_light_time);
DEVICE_ATTR_HEADER(last_triggered_uvlo2_start_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_heavy_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_medium_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_light_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_start_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_heavy_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_medium_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_light_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo2_start_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo_heavy_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo_medium_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo_light_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo_start_cnt);
DEVICE_ATTR_HEADER(last_triggered_batoilo_heavy_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo_medium_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo_light_time);
DEVICE_ATTR_HEADER(last_triggered_batoilo_start_time);
DEVICE_ATTR_HEADER(less_than_5ms_count);
DEVICE_ATTR_HEADER(between_5ms_to_10ms_count);
DEVICE_ATTR_HEADER(greater_than_10ms_count);
DEVICE_ATTR_HEADER(oilo1_triggered);
DEVICE_ATTR_HEADER(oilo2_triggered);
DEVICE_ATTR_HEADER(uvlo1_triggered);
DEVICE_ATTR_HEADER(uvlo2_triggered);
DEVICE_ATTR_HEADER(smpl_triggered);
DEVICE_ATTR_HEADER(triggered_idx);
DEVICE_ATTR_HEADER(enable_br_stats);
DEVICE_ATTR_HEADER(trigger_br_stats);
DEVICE_ATTR_HEADER(meter_channels);
DEVICE_ATTR_HEADER(uvlo_dur);
DEVICE_ATTR_HEADER(pre_uvlo_hit_cnt_m);
DEVICE_ATTR_HEADER(pre_uvlo_hit_cnt_s);
DEVICE_ATTR_HEADER(pre_ocp_cpu1_bckup);
DEVICE_ATTR_HEADER(pre_ocp_cpu2_bckup);
DEVICE_ATTR_HEADER(pre_ocp_tpu_bckup);
DEVICE_ATTR_HEADER(pre_ocp_gpu_bckup);
DEVICE_ATTR_HEADER(sys_evt_pmic);
DEVICE_ATTR_HEADER(sys_evt_addr);
DEVICE_ATTR_HEADER(sys_evt_data);
DEVICE_ATTR_HEADER(odpm_irq_stat_cpu1_bckup);
DEVICE_ATTR_HEADER(odpm_irq_stat_cpu2_bckup);
DEVICE_ATTR_HEADER(odpm_irq_stat_gpu_bckup);
DEVICE_ATTR_HEADER(odpm_irq_stat_tpu_bckup);
DEVICE_ATTR_HEADER(qos_batoilo2);
DEVICE_ATTR_HEADER(qos_batoilo);
DEVICE_ATTR_HEADER(qos_vdroop1);
DEVICE_ATTR_HEADER(qos_vdroop2);
DEVICE_ATTR_HEADER(qos_smpl_warn);
DEVICE_ATTR_HEADER(qos_ocp_cpu2);
DEVICE_ATTR_HEADER(qos_ocp_cpu1);
DEVICE_ATTR_HEADER(qos_ocp_gpu);
DEVICE_ATTR_HEADER(qos_ocp_tpu);
DEVICE_ATTR_HEADER(uvlo1_disabled);
DEVICE_ATTR_HEADER(uvlo2_disabled);
DEVICE_ATTR_HEADER(batoilo_disabled);
DEVICE_ATTR_HEADER(batoilo2_disabled);
DEVICE_ATTR_HEADER(smpl_disabled);
DEVICE_ATTR_HEADER(main_mitigation_threshold);
DEVICE_ATTR_HEADER(sub_mitigation_threshold);
DEVICE_ATTR_HEADER(main_mitigation_module_id);
DEVICE_ATTR_HEADER(sub_mitigation_module_id);

ssize_t clk_ratio_show(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
		       char *buf, int sub_idx);
ssize_t clk_ratio_store(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
			const char *buf, size_t size, int sub_idx);
ssize_t clk_div_show(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t clk_div_store(struct bcl_device *bcl_dev, int idx, const char *buf,
		      size_t size);
ssize_t main_pwrwarn_threshold_show(struct device *dev,
				    struct device_attribute *attr, char *buf);
ssize_t main_pwrwarn_threshold_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size);
ssize_t sub_pwrwarn_threshold_show(struct device *dev,
				   struct device_attribute *attr, char *buf);
ssize_t sub_pwrwarn_threshold_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size);
ssize_t odpm_irq_stat(struct device *dev, struct device_attribute *attr,
		      char *buf, int pmic, int channel);
ssize_t sys_evt_main_read(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *attr, char *buf, loff_t off,
			  size_t count);
ssize_t sys_evt_sub_read(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t off,
			 size_t count);
ssize_t cpm_cached_sys_evt_main_read(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *attr, char *buf,
				     loff_t off, size_t count);
ssize_t cpm_cached_sys_evt_sub_read(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *attr, char *buf,
				    loff_t off, size_t count);
ssize_t br_stats_dump_read(struct file *filp, struct kobject *kobj,
			   struct bin_attribute *attr, char *buf, loff_t off,
			   size_t count);
ssize_t max_odpm_stats_dump_read(struct file *filp,
				  struct kobject *kobj, struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count);

#endif /* __BCL_SYSFS_COMMON_H */
