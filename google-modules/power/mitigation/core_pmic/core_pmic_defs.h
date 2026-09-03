/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __CORE_PMIC_DEFS_H
#define __CORE_PMIC_DEFS_H

#include "bcl.h"

void compute_odpm_lpf(struct bcl_device *bcl_dev,
				struct timespec64 triggered_time,
				struct bcl_mitigation_conf *mitigation_conf,
				struct odpm_lpf *odpm_lpf,
				struct max_odpm_lpf *max_odpm_lpf);
irqreturn_t sub_pwr_warn_irq_handler(int irq, void *data);
irqreturn_t main_pwr_warn_irq_handler(int irq, void *data);
void main_pwrwarn_irq_work(struct work_struct *work);
void sub_pwrwarn_irq_work(struct work_struct *work);
void pwrwarn_update_start_time(struct bcl_device *bcl_dev,
				int id, struct irq_duration_stats *bins,
				bool *pwr_warn_triggered,
				enum CONCURRENT_PWRWARN_IRQ bin_ind);
void pwrwarn_update_end_time(struct bcl_device *bcl_dev, int id,
				struct irq_duration_stats *bins,
				enum CONCURRENT_PWRWARN_IRQ bin_ind);

int core_pmic_get_scratch_pad(struct bcl_device *bcl_dev, u8 *value);
void core_pmic_set_scratch_pad(struct bcl_device *bcl_dev, u8 value);
int get_scratch_value(struct bcl_device *bcl_dev, bool is_usb_plugged, u8 *value);

int core_pmic_main_get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 id, u16 limit, u16 step);
int core_pmic_main_set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 id,
			       u16 llimit, u16 ulimit, u16 step);
int core_pmic_sub_get_ocp_lvl(struct bcl_device *bcl_dev, u64 *val, u8 id, u16 limit, u16 step);
int core_pmic_sub_set_ocp_lvl(struct bcl_device *bcl_dev, u64 val, u8 id,
			       u16 llimit, u16 ulimit, u16 step);
int core_pmic_main_read_uvlo(struct bcl_device *bcl_dev, unsigned int *smpl_warn_lvl);
u16 core_pmic_main_store_uvlo(struct bcl_device *bcl_dev, unsigned int val, size_t size);
void core_pmic_parse_dtree(struct bcl_device *bcl_dev);
int core_pmic_main_setup(struct bcl_device *bcl_dev, struct platform_device *pdev);
int core_pmic_sub_setup(struct bcl_device *bcl_dev);
void core_pmic_main_meter_read_lpf_data(struct bcl_device *bcl_dev,
					struct brownout_stats *br_stats);
void core_pmic_sub_meter_read_lpf_data(struct bcl_device *bcl_dev, struct brownout_stats *br_stats);
void core_pmic_bcl_init_bbat(struct bcl_device *bcl_dev);
uint32_t core_pmic_get_pre_evt_cnt(struct bcl_device *bcl_dev, int zone_idx);
void core_pmic_teardown(struct bcl_device *bcl_dev);
int core_pmic_mbox_request(struct bcl_device *bcl_dev);
int pmic_write(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 value);
int pmic_read(int pmic, struct bcl_device *bcl_dev, u8 reg, u8 *value);
int meter_write(int pmic, struct bcl_device *bcl_dev, int idx, u8 value);
int meter_read(int pmic, struct bcl_device *bcl_dev, int idx, u8 *value);
int read_uvlo_dur(struct bcl_device *bcl_dev, uint64_t *data);
int read_pre_uvlo_hit_cnt(struct bcl_device *bcl_dev, uint16_t *data, int pmic);
int read_pre_ocp_bckup(struct bcl_device *bcl_dev, int *pre_ocp_bckup, int rail);
int read_odpm_int_bckup(struct bcl_device *bcl_dev, int *odpm_int_bckup, u16 *type, int pmic,
			int channel);
uint32_t core_pmic_read_main_pwrwarn(struct bcl_device *bcl_dev, int pwrwarn_idx);
uint32_t core_pmic_read_sub_pwrwarn(struct bcl_device *bcl_dev, int pwrwarn_idx);
uint32_t core_pmic_get_cpm_cached_sys_evt(struct bcl_device *bcl_dev);

#if IS_ENABLED(CONFIG_KUNIT)
u32 convert_pre_uvlo_lvl(int value);
int convert_pre_ocp_lvl(int value, int idx, u32 *output);
#endif

#endif /* __CORE_PMIC_DEFS_H */
