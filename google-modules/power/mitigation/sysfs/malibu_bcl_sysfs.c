// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "%s:%s " fmt, KBUILD_MODNAME, __func__
#include <linux/platform_device.h>
#include "common.h"
#include "dyn_clk_div.h"
#include "debounce_time.h"
#include "ocp_timer.h"
#include "mitigation_response.h"
#include "soc/soc_defs.h"
#include "bcl.h"
#include "batoilo_policy_lvl.h"

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
	&dev_attr_pwronsrc.attr, &dev_attr_last_current.attr,
	&dev_attr_vimon_buff.attr, &dev_attr_ready.attr,
	&dev_attr_ifpmic.attr, &dev_attr_bcl_version.attr,
	NULL,
};

static const struct attribute_group instr_group = {
	.attrs = instr_attrs,
	.name = "instruction",
};

BATOILO_TYPE_FUNC(1, usb);
BATOILO_TYPE_FUNC(1, wlc);
BATOILO_TYPE_FUNC(2, usb);
BATOILO_TYPE_FUNC(2, wlc);

static struct attribute *triggered_lvl_attrs[] = {
	&dev_attr_uvlo1_lvl.attr,	  &dev_attr_uvlo2_lvl.attr,
	&dev_attr_batoilo1_lvl.attr,	  &dev_attr_batoilo1_usb_lvl.attr,
	&dev_attr_batoilo1_wlc_lvl.attr,  &dev_attr_batoilo2_lvl.attr,
	&dev_attr_batoilo2_usb_lvl.attr,  &dev_attr_batoilo2_wlc_lvl.attr,
	&dev_attr_smpl_lvl.attr,	  &dev_attr_ocp_cpu1_lvl.attr,
	&dev_attr_ocp_cpu2_lvl.attr,	  &dev_attr_ocp_tpu_lvl.attr,
	&dev_attr_ocp_gpu_lvl.attr,	  &dev_attr_soft_ocp_cpu1_lvl.attr,
	&dev_attr_soft_ocp_cpu2_lvl.attr, &dev_attr_soft_ocp_tpu_lvl.attr,
	&dev_attr_soft_ocp_gpu_lvl.attr,  NULL,
};

static const struct attribute_group triggered_lvl_group = {
	.attrs = triggered_lvl_attrs,
	.name = "triggered_lvl",
};

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

GEN_CLK_DIV(GPU);
GEN_CLK_DIV(AUR);

static struct attribute *clock_div_attrs[] = {
	&dev_attr_clk_GPU_div.attr,
	&dev_attr_clk_AUR_div.attr,
	NULL,
};

static const struct attribute_group clock_div_group = {
	.attrs = clock_div_attrs,
	.name = "clock_div",
};

GEN_CLK_RATIO(CPU1A, heavy);
GEN_CLK_RATIO(CPU1A, light);
GEN_CLK_RATIO(CPU1B, heavy);
GEN_CLK_RATIO(CPU1B, light);
GEN_CLK_RATIO(CPU2, light);
GEN_CLK_RATIO(CPU2, heavy);
GEN_CLK_RATIO(AUR, heavy);
GEN_CLK_RATIO(AUR, light);
GEN_CLK_RATIO(TPU, light);
GEN_CLK_RATIO(TPU, heavy);
GEN_CLK_RATIO(GPU, light);
GEN_CLK_RATIO(GPU, heavy);
GEN_CLK_RATIO(CME, heavy);
GEN_CLK_RATIO(CME, light);
GEN_CLK_RATIO(DSU, heavy);
GEN_CLK_RATIO(DSU, light);

static struct attribute *clock_ratio_attrs[] = {
	&dev_attr_clk_CPU1A_heavy_ratio.attr,
	&dev_attr_clk_CPU1A_light_ratio.attr,
	&dev_attr_clk_CPU1B_heavy_ratio.attr,
	&dev_attr_clk_CPU1B_light_ratio.attr,
	&dev_attr_clk_CPU2_heavy_ratio.attr,
	&dev_attr_clk_CPU2_light_ratio.attr,
	&dev_attr_clk_AUR_heavy_ratio.attr,
	&dev_attr_clk_AUR_light_ratio.attr,
	&dev_attr_clk_GPU_heavy_ratio.attr,
	&dev_attr_clk_GPU_light_ratio.attr,
	&dev_attr_clk_TPU_heavy_ratio.attr,
	&dev_attr_clk_TPU_light_ratio.attr,
	&dev_attr_clk_CME_heavy_ratio.attr,
	&dev_attr_clk_CME_light_ratio.attr,
	&dev_attr_clk_DSU_heavy_ratio.attr,
	&dev_attr_clk_DSU_light_ratio.attr,

	NULL,
};

static const struct attribute_group clock_ratio_group = {
	.attrs = clock_ratio_attrs,
	.name = "clock_ratio",
};

GEN_MITIGATION_RES_EN(CPU1A);
GEN_MITIGATION_RES_EN(CPU1B);
GEN_MITIGATION_RES_EN(CPU2);
GEN_MITIGATION_RES_EN(GPU);
GEN_MITIGATION_RES_EN(TPU);
GEN_MITIGATION_RES_EN(AUR);
GEN_MITIGATION_RES_EN(CME);
GEN_MITIGATION_RES_EN(DSU);

static struct attribute *mitigation_res_en_attrs[] = {
	&dev_attr_mitigation_CPU1A_res_en.attr,
	&dev_attr_mitigation_CPU1B_res_en.attr,
	&dev_attr_mitigation_CPU2_res_en.attr,
	&dev_attr_mitigation_GPU_res_en.attr,
	&dev_attr_mitigation_TPU_res_en.attr,
	&dev_attr_mitigation_AUR_res_en.attr,
	&dev_attr_mitigation_CME_res_en.attr,
	&dev_attr_mitigation_DSU_res_en.attr,
	NULL,
};

GEN_MITIGATION_RES_TYPE(CPU1A);
GEN_MITIGATION_RES_TYPE(CPU1B);
GEN_MITIGATION_RES_TYPE(CPU2);
GEN_MITIGATION_RES_TYPE(GPU);
GEN_MITIGATION_RES_TYPE(TPU);
GEN_MITIGATION_RES_TYPE(AUR);
GEN_MITIGATION_RES_TYPE(CME);
GEN_MITIGATION_RES_TYPE(DSU);

static struct attribute *mitigation_res_type_attrs[] = {
	&dev_attr_mitigation_CPU1A_res_type.attr,
	&dev_attr_mitigation_CPU1B_res_type.attr,
	&dev_attr_mitigation_CPU2_res_type.attr,
	&dev_attr_mitigation_GPU_res_type.attr,
	&dev_attr_mitigation_TPU_res_type.attr,
	&dev_attr_mitigation_AUR_res_type.attr,
	&dev_attr_mitigation_CME_res_type.attr,
	&dev_attr_mitigation_DSU_res_type.attr,
	NULL,
};

GEN_MITIGATION_RES_HYST(CPU1A);
GEN_MITIGATION_RES_HYST(CPU1B);
GEN_MITIGATION_RES_HYST(CPU2);
GEN_MITIGATION_RES_HYST(GPU);
GEN_MITIGATION_RES_HYST(TPU);
GEN_MITIGATION_RES_HYST(AUR);
GEN_MITIGATION_RES_HYST(CME);
GEN_MITIGATION_RES_HYST(DSU);

static struct attribute *mitigation_res_hyst_attrs[] = {
	&dev_attr_mitigation_CPU1A_res_hyst.attr,
	&dev_attr_mitigation_CPU1B_res_hyst.attr,
	&dev_attr_mitigation_CPU2_res_hyst.attr,
	&dev_attr_mitigation_GPU_res_hyst.attr,
	&dev_attr_mitigation_TPU_res_hyst.attr,
	&dev_attr_mitigation_AUR_res_hyst.attr,
	&dev_attr_mitigation_CME_res_hyst.attr,
	&dev_attr_mitigation_DSU_res_hyst.attr,
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

static struct attribute *triggered_count_attrs[] = {
	&dev_attr_smpl_warn_count.attr,	    &dev_attr_ocp_cpu1_count.attr,
	&dev_attr_ocp_cpu2_count.attr,	    &dev_attr_ocp_tpu_count.attr,
	&dev_attr_ocp_gpu_count.attr,	    &dev_attr_soft_ocp_cpu1_count.attr,
	&dev_attr_soft_ocp_cpu2_count.attr, &dev_attr_soft_ocp_tpu_count.attr,
	&dev_attr_soft_ocp_gpu_count.attr,  &dev_attr_vdroop1_count.attr,
	&dev_attr_vdroop2_count.attr,	    &dev_attr_batoilo_count.attr,
	&dev_attr_batoilo2_count.attr,	    NULL,
};

static const struct attribute_group triggered_count_group = {
	.attrs = triggered_count_attrs,
	.name = "last_triggered_count",
};

static struct attribute *triggered_time_attrs[] = {
	&dev_attr_smpl_warn_time.attr,	   &dev_attr_ocp_cpu1_time.attr,
	&dev_attr_ocp_cpu2_time.attr,	   &dev_attr_ocp_tpu_time.attr,
	&dev_attr_ocp_gpu_time.attr,	   &dev_attr_soft_ocp_cpu1_time.attr,
	&dev_attr_soft_ocp_cpu2_time.attr, &dev_attr_soft_ocp_tpu_time.attr,
	&dev_attr_soft_ocp_gpu_time.attr,  &dev_attr_vdroop1_time.attr,
	&dev_attr_vdroop2_time.attr,	   &dev_attr_batoilo_time.attr,
	&dev_attr_batoilo2_time.attr,	   NULL,
};

static const struct attribute_group triggered_timestamp_group = {
	.attrs = triggered_time_attrs,
	.name = "last_triggered_timestamp",
};

static struct attribute *triggered_cap_attrs[] = {
	&dev_attr_smpl_warn_cap.attr,	  &dev_attr_ocp_cpu1_cap.attr,
	&dev_attr_ocp_cpu2_cap.attr,	  &dev_attr_ocp_tpu_cap.attr,
	&dev_attr_ocp_gpu_cap.attr,	  &dev_attr_soft_ocp_cpu1_cap.attr,
	&dev_attr_soft_ocp_cpu2_cap.attr, &dev_attr_soft_ocp_tpu_cap.attr,
	&dev_attr_soft_ocp_gpu_cap.attr,  &dev_attr_vdroop1_cap.attr,
	&dev_attr_vdroop2_cap.attr,	  &dev_attr_batoilo_cap.attr,
	&dev_attr_batoilo2_cap.attr,	  NULL,
};

static const struct attribute_group triggered_capacity_group = {
	.attrs = triggered_cap_attrs,
	.name = "last_triggered_capacity",
};

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

static struct attribute *triggered_volt_attrs[] = {
	&dev_attr_smpl_warn_volt.attr,	   &dev_attr_ocp_cpu1_volt.attr,
	&dev_attr_ocp_cpu2_volt.attr,	   &dev_attr_ocp_tpu_volt.attr,
	&dev_attr_ocp_gpu_volt.attr,	   &dev_attr_soft_ocp_cpu1_volt.attr,
	&dev_attr_soft_ocp_cpu2_volt.attr, &dev_attr_soft_ocp_tpu_volt.attr,
	&dev_attr_soft_ocp_gpu_volt.attr,  &dev_attr_vdroop1_volt.attr,
	&dev_attr_vdroop2_volt.attr,	   &dev_attr_batoilo_volt.attr,
	&dev_attr_batoilo2_volt.attr,	   NULL,
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
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 12);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 13);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 14);
static DEVICE_PWRWARN_ATTR(main_pwrwarn_threshold, 15);

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
	&attr_main_pwrwarn_threshold12.attr,
	&attr_main_pwrwarn_threshold13.attr,
	&attr_main_pwrwarn_threshold14.attr,
	&attr_main_pwrwarn_threshold15.attr,
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
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 12);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 13);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 14);
static DEVICE_PWRWARN_ATTR(sub_pwrwarn_threshold, 15);

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
	&attr_sub_pwrwarn_threshold12.attr,
	&attr_sub_pwrwarn_threshold13.attr,
	&attr_sub_pwrwarn_threshold14.attr,
	&attr_sub_pwrwarn_threshold15.attr,
	NULL,
};

static struct attribute *qos_attrs[] = {
	&dev_attr_qos_batoilo2.attr,  &dev_attr_qos_batoilo.attr,
	&dev_attr_qos_vdroop1.attr,   &dev_attr_qos_vdroop2.attr,
	&dev_attr_qos_smpl_warn.attr, &dev_attr_qos_ocp_cpu2.attr,
	&dev_attr_qos_ocp_cpu1.attr,  &dev_attr_qos_ocp_gpu.attr,
	&dev_attr_qos_ocp_tpu.attr,   NULL,
};

static const struct attribute_group main_pwrwarn_group = {
	.attrs = main_pwrwarn_attrs,
	.name = "main_pwrwarn",
};

static const struct attribute_group sub_pwrwarn_group = {
	.attrs = sub_pwrwarn_attrs,
	.name = "sub_pwrwarn",
};

static struct attribute *irq_dur_cnt_attrs[] = {
	&dev_attr_less_than_5ms_count.attr,
	&dev_attr_between_5ms_to_10ms_count.attr,
	&dev_attr_greater_than_10ms_count.attr,
	NULL,
};

static struct attribute *irq_config_attrs[] = {
	&dev_attr_uvlo1_disabled.attr,
	&dev_attr_uvlo2_disabled.attr,
	&dev_attr_batoilo_disabled.attr,
	&dev_attr_batoilo2_disabled.attr,
	&dev_attr_smpl_disabled.attr,
	NULL,
};

static struct attribute *mitigation_attrs[] = {
	&dev_attr_main_mitigation_threshold.attr,
	&dev_attr_sub_mitigation_threshold.attr,
	&dev_attr_main_mitigation_module_id.attr,
	&dev_attr_sub_mitigation_module_id.attr,
	NULL,
};

static struct attribute *triggered_state_sq_attrs[] = {
	&dev_attr_oilo1_triggered.attr, &dev_attr_uvlo1_triggered.attr,
	&dev_attr_uvlo2_triggered.attr, &dev_attr_smpl_triggered.attr,
	&dev_attr_oilo2_triggered.attr, NULL,
};

static struct attribute *triggered_state_mw_attrs[] = {
	&dev_attr_oilo1_triggered.attr,
	&dev_attr_uvlo1_triggered.attr,
	&dev_attr_uvlo2_triggered.attr,
	&dev_attr_smpl_triggered.attr,
	NULL,
};

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

GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 0);
GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 1);
GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 2);
GEN_ODPM_STAT_EXT(SYS_EVT_MAIN, 3);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 0);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 1);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 2);
GEN_ODPM_STAT_EXT(SYS_EVT_SUB, 3);

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

static struct bin_attribute br_stats_dump_attr = {
	.attr = { .name = "stats", .mode = 0444 },
	.read = br_stats_dump_read,
	.size = sizeof(struct brownout_stats),
};

static struct bin_attribute max_odpm_stats_dump_attr = {
	.attr = { .name = "max_odpm_stats", .mode = 0444 },
	.read = max_odpm_stats_dump_read,
	.size = sizeof(struct max_odpm_stats),
};

static struct bin_attribute *br_stats_bin_attrs[] = {
	&br_stats_dump_attr,
	&max_odpm_stats_dump_attr,
	NULL,
};

static struct attribute *br_stats_attrs[] = {
	&dev_attr_triggered_idx.attr,
	&dev_attr_enable_br_stats.attr,
	&dev_attr_trigger_br_stats.attr,
	&dev_attr_meter_channels.attr,
	NULL,
};

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
	&main_pwrwarn_group,
	&sub_pwrwarn_group,
	&irq_dur_cnt_group,
	&qos_group,
	NULL,
};

const struct attribute_group *mitigation_sq_groups[] = {
	&debounce_time_group,
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
	&sys_evt_group,
	&trigger_timer_group,
	&mitigation_res_en_group,
	&mitigation_res_type_group,
	&mitigation_res_hyst_group,
	&mitigation_group,
	&dyn_clk_div_en_group,
	&dyn_clk_div_ratio_group,
	&dyn_clk_div_thresh_group,
	&main_pwrwarn_group,
	&sub_pwrwarn_group,
	&irq_dur_cnt_group,
	&qos_group,
	NULL,
};
