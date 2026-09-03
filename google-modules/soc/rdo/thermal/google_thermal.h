/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _GOOGLE_THERMAL_H
#define _GOOGLE_THERMAL_H

#include <linux/device.h>
#include <linux/devfreq.h>
#include <linux/thermal.h>

#include "thermal/cpm_thermal.h"

#define MCELSIUS	(1000)
#define NR_PRESSURE_TZ	3
#define NR_TRIPS	8

#define DEFAULT_PID_K_PO		60
#define DEFAULT_PID_K_PU		60
#define DEFAULT_PID_K_I			50
#define DEFAULT_PID_I_MAX		5
#define DEFAULT_MPMM_CLR_THROTTLE_LEVEL	5
#define DEFAULT_MPMM_THROTTLE_LEVEL	5
#define DEFAULT_GRADUAL_IRQ_GAIN	2
#define DEFAULT_GRADUAL_TIMER_GAIN	1
#define DEFAULT_HARDLIMIT_USE_PID	0
#define DEFAULT_EARLY_THROTTLE_K_P	60

struct thermal_pid_param {
	s8 k_po;
	s8 k_pu;
	s8 k_i;
	s8 i_max;
};

struct thermal_mpmm_param {
	u16 clr_throttle_level;
	u16 throttle_level;
};

struct thermal_gradual_param {
	u8 irq_gradual_gain;
	u8 timer_gradual_gain;
};

struct thermal_hardlimit_param {
	bool use_pid;
};

struct thermal_early_throttle_param {
	s16 k_p;
};

struct thermal_pressure {
	u32 time_window;
	struct cpumask mapped_cpus;
	u32 pressure_index;
};

enum thermal_gov_select_bit_offset {
	THERMAL_GOV_SEL_BIT_GRADUAL = 0,
	THERMAL_GOV_SEL_BIT_PI_LOOP = 1,
	THERMAL_GOV_SEL_BIT_TEMP_LUT = 2,
	THERMAL_GOV_SEL_BIT_HARDLIMIT_VIA_PID = 3,
	THERMAL_GOV_SEL_BIT_EARLY_THROTTLE = 4,
	THERMAL_GOV_SEL_BIT_MPMM = 5,
	THERMAL_GOV_SEL_BIT_INVALID_GOV_MOD = 8
};

struct thermal_gov_param {
	struct thermal_pid_param pid_param;
	struct thermal_mpmm_param mpmm_param;
	struct thermal_gradual_param gradual_param;
	struct thermal_hardlimit_param hardlimit_param;
	struct thermal_early_throttle_param early_throttle_param;
	u32 gov_select;
};

struct thermal_curr_state {
	u8 cdev_state;
};

struct thermal_data {
	struct device *dev;
	struct dentry *debugfs_root;
	struct thermal_zone_device *tz;
	u64 temperature;
	u32 id;
	struct thermal_gov_param gov_param;
	u16 cpm_polling_delay_ms;
	struct thermal_pressure *pressure;
	s32 junction_offset[NR_TRIPS];
	struct google_cpm_thermal *cpm_thermal;
	struct thermal_data_ops *ops;
	struct devfreq *devfreq;
	struct dev_pm_qos_request tj_gpu_max_freq;
	struct thermal_cooling_device *tj_cooling;
	/* shared memory address */
	void __iomem *curr_state_addr;
	struct thermal_curr_state curr_state;
};

struct thermal_data_ops {
	void (*example)(struct thermal_data *data, int level);
	void (*google_thermal_throttling)(struct thermal_data *data, u32 freq);
};

#endif /* _GOOGLE_THERMAL_H */
