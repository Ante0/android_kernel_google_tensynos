// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_msg_helper.h Helper to send message/request to CPM.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_MSG_HELPER_H_
#define _THERMAL_MSG_HELPER_H_

#include "thermal_cpm_mbox.h"

#define THERMAL_NTC_IRQ_STATUS_BUF_LEN	2
#define THERMAL_TMU_NR_TRIPS (8)
#define THERMAL_TEMP_LUT_MAX_ENTRIES (24)

int __msg_gen_send_req_with_status_check(union thermal_cpm_message *message);

int msg_ntc_channel_enable(void);
int msg_ntc_channel_read_temp(int ch_id, int *reg_data);
int msg_ntc_channel_read_avg_temp(int ch_id, int *reg_data);
int msg_ntc_channel_set_trips(int ch_id, int trip_val, int trip_hyst);
int msg_ntc_channel_set_fault_trip(int ch_id, int trip_val);
int msg_ntc_channel_read_irq_status(int *reg_data, int len);
int msg_ntc_channel_clear_and_mask_irq(int ch_id, bool enable);
int msg_ntc_channel_mask_fault_irq(int ch_id, bool enable);
int msg_ntc_channel_clear_data_reg(void);

int msg_ntc_channel_generic_req(int ch_id, enum thermal_cpm_mbox_req_type type);
int msg_ntc_channel_generic_req_with_val(int ch_id, enum thermal_cpm_mbox_req_type type,
					 int val1, int val2);
int msg_ntc_channel_generic_read(int ch_id, enum thermal_cpm_mbox_req_type type,
					int *reg_data);

int msg_tmu_get_temp(u8 tz_id, u8 *temperature);
int msg_tmu_set_trip_temp(u8 tz_id, u8 *temperature, int num_temperature);
int msg_tmu_set_trip_hyst(u8 tz_id, u8 *hysteresis, int num_hysteresis);
int msg_tmu_set_trip_type(u8 tz_id, u8 *type, int num_type);
int msg_tmu_get_trip_counter_snapshot(u8 tz_id);
int msg_tmu_set_gov_param(u8 tz_id, u8 type, int val);
int msg_tmu_set_gov_select(u8 tz_id, u8 gov_select);
int msg_tmu_set_power_table(enum hw_dev_type cdev_id, u8 idx, int val);
int msg_tmu_get_power_table(enum hw_dev_type cdev_id, u8 idx, int *val,
			    int *max_state_idx);
int msg_tmu_set_polling_delay_ms(u8 tz_id, u16 delay);
int msg_tmu_set_temp_lut(u8 tz_id, u8 temperature, u8 cdev_state, bool append);
int msg_tmu_get_temp_lut(u8 tz_id, u8 index, u8 *temperature, u8 *cdev_state);

int msg_thermal_sm_get_section_addr(u8 section, u32 *version, u32 *addr, u32 *size);

int msg_stats_get_tr_stats(u8 tz_id);
int msg_stats_reset_tr_stats(u8 tz_id);
int msg_stats_get_tr_thresholds(u8 tz_id);
int msg_stats_set_tr_thresholds(u8 tz_id);
#endif  // _THERMAL_MSG_HELPER_H_
