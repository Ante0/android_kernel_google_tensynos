/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_tmu_helper.h Helper for TMU related functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_TMU_HELPER_H_
#define _THERMAL_TMU_HELPER_H_

#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/units.h>

#include "thermal_core.h"
#include "thermal_msg_helper.h"
#include "thermal_sm_helper.h"
#include "thermal_zone_helper.h"

enum tmu_gov_select_bit_offset {
	TMU_GOV_GRADUAL_SELECT = 0,
	TMU_GOV_PI_SELECT,
	TMU_GOV_TEMP_LUT_SELECT,
	TMU_GOV_HARDLIMIT_VIA_PID_SELECT,
	TMU_GOV_EARLY_THROTTLE_SELECT,
};

/* tmu gov param message types */
enum tmu_gov_param_type {
	TMU_GOV_PARAM_K_PO = 0,
	TMU_GOV_PARAM_K_PU,
	TMU_GOV_PARAM_K_I,
	TMU_GOV_PARAM_I_MAX,
	TMU_GOV_PARAM_EARLY_THROTTLE_K_P,
	TMU_GOV_PARAM_IRQ_GAIN,
	TMU_GOV_PARAM_TIMER_GAIN,
	TMU_GOV_PARAM_END,
};

struct tmu_gradual_param {
	s32 irq_gain;
	s32 timer_gain;
};

struct tmu_pi_param {
	s32 k_po;
	s32 k_pu;
	s32 k_i;
};

struct tmu_early_throttle_param {
	s32 k_p;
	s32 offset;
};

enum tmu_trip_type {
	TMU_TRIP_TYPE_PASSIVE = 0,
	TMU_TRIP_TYPE_ACTIVE,
	TMU_TRIP_TYPE_HOT,
	TMU_TRIP_TYPE_CRITICAL,
	TMU_TRIP_TYPE_MAX,
};

struct tmu_temp_state_st {
	u32 temp;
	u32 state;
};

struct tmu_sensor_data {
	s32				polling_delay_ms;
	struct tmu_gradual_param	gradual_param;
	struct tmu_pi_param		pi_param;
	struct tmu_early_throttle_param	early_throttle_param;
	s32				junction_offset[THERMAL_TMU_NR_TRIPS];
	unsigned long			gov_select;
	struct device_attribute		dev_attr_trip_counter;
	bool				skip_junction_offset;
	enum hw_thermal_zone_id		hw_tzid;
	u32				temp_state_table_len;
	struct tmu_temp_state_st	*temp_state_table;
};

/* private functions */
int __thermal_tmu_read_dt_property_with_default(struct google_sensor_data *sensor,
						struct device_node *np, char *name,
						int *val, int default_value);
int __thermal_tmu_parse_gradual_param_dt(struct google_sensor_data *sensor,
					 struct device_node *tmu_np);
int __thermal_tmu_parse_pi_param_dt(struct google_sensor_data *sensor, struct device_node *tmu_np);
int __thermal_tmu_parse_early_throttle_param_dt(struct google_sensor_data *sensor,
						struct device_node *tmu_np);
int __thermal_tmu_parse_tzd_dt(struct google_sensor_data *sensor);
int __thermal_tmu_gov_gradual_init(struct device *dev, u8 tz_id, struct tmu_sensor_data *data);
int __thermal_tmu_gov_pi_init(struct device *dev, u8 tz_id, struct tmu_sensor_data *data);
int __thermal_tmu_gov_early_throttle_init(struct device *dev, u8 tz_id,
					  struct tmu_sensor_data *data);
int __thermal_tmu_gov_temp_lut_init(struct device *dev, u8 tz_id,
				    struct tmu_sensor_data *data);
int __thermal_tmu_trip_points_init(struct google_sensor_data *sensor);
int __thermal_tmu_send_init_msg(struct google_sensor_data *sensor);
int __thermal_tmu_set_trip_temp(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
				int temp);
int __thermal_tmu_offset_enable(bool enable);
int __thermal_tmu_trip_counter_init(struct device *dev, struct google_sensor_data *sensor);
int __thermal_tmu_get_trip_counter(struct device_attribute *attr,
				   struct thermal_sm_trip_counter_data *data);

/* global tmu configure and cleanup*/
int thermal_tmu_configure(struct platform_device *pdev, int sensor_ct);
int thermal_tmu_post_configure(struct list_head *sensor_list);
void thermal_tmu_cleanup(struct platform_device *pdev);

/* initialize a tmu after register thermal zone */
int thermal_tmu_configure_sensor(struct google_sensor_data *sensor);
int thermal_tmu_cleanup_sensor(struct google_sensor_data *sensor);

/* thermal zone ops */
int thermal_tmu_get_temp(struct thermal_zone_device *tzd, int *temp);
int thermal_tmu_set_trip_temp(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
			      int temp);
int thermal_tmu_set_polling_delay_ms(struct google_sensor_data *sensor, u32 val);
int thermal_tmu_get_polling_delay_ms(struct google_sensor_data *sensor, u32 *val);
int thermal_tmu_set_junction_offset(struct google_sensor_data *sensor, int offsets[],
				    int num_offset);
int thermal_tmu_get_junction_offset(struct google_sensor_data *sensor, int offsets[]);
int thermal_tmu_set_temp_state_table(struct google_sensor_data *sensor,
				const struct tmu_temp_state_st *table, int len);
int thermal_tmu_get_temp_state_table(struct google_sensor_data *sensor,
				struct tmu_temp_state_st *table, int *len);

/* TODO: config APIs for MPMM throttling */
#endif // _THERMAL_TMU_HELPER_H_
