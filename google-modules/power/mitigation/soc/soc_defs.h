/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __SOC_DEFS_H
#define __SOC_DEFS_H

#include "bcl.h"

#if IS_ENABLED(CONFIG_SOC_ZUMA) || IS_ENABLED(CONFIG_SOC_GS201) || IS_ENABLED(CONFIG_SOC_GS101)
int cpu_buff_read(struct bcl_device *bcl_dev, int cluster, unsigned int type, unsigned int *reg);
int cpu_buff_write(struct bcl_device *bcl_dev, int cluster, unsigned int type, unsigned int val);
int cpu_sfr_read(struct bcl_device *bcl_dev, int idx, void __iomem *addr, unsigned int *reg);
int cpu_sfr_write(struct bcl_device *bcl_dev, int idx, void __iomem *addr, unsigned int value);
bool bcl_is_cluster_on(struct bcl_device *bcl_dev, int cluster);
bool bcl_is_subsystem_on(struct bcl_device *bcl_dev, unsigned int addr);
bool bcl_disable_power(struct bcl_device *bcl_dev, int system_src);
bool bcl_enable_power(struct bcl_device *bcl_dev, int system_src);
int google_bcl_init_notifier(struct bcl_device *bcl_dev);
int google_init_ratio(struct bcl_device *data, enum SUBSYSTEM_SOURCE idx);
static inline void google_bcl_setup_irq_mb(struct bcl_device *bcl_dev) {}
static inline int google_bcl_setup_mailbox(struct bcl_device *bcl_dev) { return 0; }
static inline void google_bcl_teardown_mailbox(struct bcl_device *bcl_dev) {}
void google_bcl_clk_div(struct bcl_device *bcl_dev);
int google_bcl_init_instruction(struct bcl_device *bcl_dev);
static inline int google_bcl_setup_qos_device(struct bcl_device *bcl_dev,
					      enum non_cpu_qos_device_idx qidx) { return 0; }
static inline void google_bcl_setup_qos_work(struct work_struct *work) {}
static inline void google_bcl_cancel_batfet_timer(struct bcl_device *bcl_dev) {}
static inline void google_bcl_cancel_bat_throttle_timer(struct bcl_device *bcl_dev) {}
static inline void google_bcl_set_batfet_timer(struct bcl_device *bcl_dev) {}
static inline void google_bcl_set_bat_throttle_timer(struct bcl_device *bcl_dev) {}
#else
void google_bcl_set_bat_throttle_timer(struct bcl_device *bcl_dev);
void google_bcl_set_batfet_timer(struct bcl_device *bcl_dev);
void google_bcl_cancel_batfet_timer(struct bcl_device *bcl_dev);
void google_bcl_cancel_bat_throttle_timer(struct bcl_device *bcl_dev);
void google_bcl_setup_qos_work(struct work_struct *work);
int google_bcl_setup_qos_device(struct bcl_device *bcl_dev, enum non_cpu_qos_device_idx qidx);
static inline int cpu_buff_read(struct bcl_device *bcl_dev, int cluster, unsigned int type,
				unsigned int *reg) { return 0; }
static inline int cpu_buff_write(struct bcl_device *bcl_dev, int cluster, unsigned int type,
				 unsigned int val) { return 0; }
static inline int cpu_sfr_read(struct bcl_device *bcl_dev, int idx, void __iomem *addr,
			       unsigned int *reg) { return 0; }
static inline int cpu_sfr_write(struct bcl_device *bcl_dev, int idx, void __iomem *addr,
				unsigned int value) { return 0; }
static inline bool bcl_is_cluster_on(struct bcl_device *bcl_dev, int cluster) { return true; }
static inline bool bcl_is_subsystem_on(struct bcl_device *bcl_dev,
				       unsigned int addr) { return true; }
static inline bool bcl_disable_power(struct bcl_device *bcl_dev, int system_src) { return true; }
static inline bool bcl_enable_power(struct bcl_device *bcl_dev, int system_src) { return true; }
static inline int google_bcl_init_notifier(struct bcl_device *bcl_dev) { return 0; }
static inline int google_init_ratio(struct bcl_device *data,
				    enum SUBSYSTEM_SOURCE idx) { return 0; }
static inline int google_bcl_init_instruction(struct bcl_device *bcl_dev) { return 0; }
void google_bcl_setup_irq_mb(struct bcl_device *bcl_dev);
int google_bcl_setup_mailbox(struct bcl_device *bcl_dev);
void google_bcl_teardown_mailbox(struct bcl_device *bcl_dev);
static inline void google_bcl_clk_div(struct bcl_device *bcl_dev) {}
ssize_t set_mitigation_res_en(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx);
ssize_t get_mitigation_res_en(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t set_mitigation_res_type(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx);
ssize_t get_mitigation_res_type(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t set_mitigation_res_hyst(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx);
ssize_t get_mitigation_res_hyst(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t get_ocp_batfet_timeout_enable(struct bcl_device *bcl_dev, char *buf);
ssize_t set_ocp_batfet_timeout_enable(struct bcl_device *bcl_dev, const char *buf, size_t size);
ssize_t get_ocp_batfet_timeout(struct bcl_device *bcl_dev, char *buf);
ssize_t set_ocp_batfet_timeout(struct bcl_device *bcl_dev, const char *buf, size_t size);
ssize_t get_ocp_bat_throttle_timeout_enable(struct bcl_device *bcl_dev, char *buf);
ssize_t set_ocp_bat_throttle_timeout_enable(struct bcl_device *bcl_dev, const char *buf,
					    size_t size);
ssize_t get_ocp_bat_throttle_timeout(struct bcl_device *bcl_dev, char *buf);
ssize_t set_ocp_bat_throttle_timeout(struct bcl_device *bcl_dev, const char *buf, size_t size);
#endif

#if IS_ENABLED(CONFIG_SOC_MBU)
ssize_t set_dyn_clk_div_enable(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx);
ssize_t get_dyn_clk_div_enable(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t set_dyn_clk_div_ratio(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx);
ssize_t get_dyn_clk_div_ratio(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t set_dyn_clk_div_thresh(struct bcl_device *bcl_dev, const char *buf, size_t size, int idx);
ssize_t get_dyn_clk_div_thresh(struct bcl_device *bcl_dev, int idx, char *buf);
#endif

void google_bcl_parse_clk_div_dtree(struct bcl_device *bcl_dev);
int google_bcl_parse_qos(struct bcl_device *bcl_dev);
void google_bcl_qos_update(struct bcl_zone *zone, int throttle_lvl);
int google_bcl_setup_qos(struct bcl_device *bcl_dev);
void google_bcl_remove_qos(struct bcl_device *bcl_dev);
ssize_t set_clk_ratio(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx,
		      const char *buf, size_t size, int sub_idx);
ssize_t get_clk_ratio(struct bcl_device *bcl_dev, enum RATIO_SOURCE idx, char *buf, int sub_idx);
ssize_t get_clk_stats(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t set_clk_div(struct bcl_device *bcl_dev, int idx, const char *buf, size_t size);
ssize_t get_clk_div(struct bcl_device *bcl_dev, int idx, char *buf);
ssize_t set_hw_mitigation(struct bcl_device *bcl_dev, const char *buf, size_t size);
ssize_t set_sw_mitigation(struct bcl_device *bcl_dev, const char *buf, size_t size);

/**
 * google_init_vd_soc
 * @bcl_dev: bcl_device private data
 *
 * This will enable any appropriate GPIO configurations
 * in order for VDROOP to function properly.
 *
 * Context: Can be called from any context.
 *
 * Return: 0 for successes or reasons for failure
 */
int google_init_vd_soc(struct bcl_device *bcl_dev);

#if IS_ENABLED(CONFIG_SOC_ZUMA)
#define GPA_CON GPA9_CON
#elif IS_ENABLED(CONFIG_SOC_GS201)
#define GPA_CON GPA5_CON
#define CLUSTER1_NONCPU_STATES 0
#define CLUSTER2_NONCPU_STATES 0
#else
#define GPA_CON 0
#define CLUSTER1_NONCPU_STATES 0
#define CLUSTER2_NONCPU_STATES 0
#endif

#endif /* __SOC_DEFS_H */
