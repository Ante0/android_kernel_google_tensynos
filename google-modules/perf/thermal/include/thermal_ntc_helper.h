/* SPDX-License-Identifier: GPL-2.0 */
/*
 * thermal_ntc_helper.h Helper for NTC related functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#ifndef _THERMAL_NTC_HELPER_H_
#define _THERMAL_NTC_HELPER_H_

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>

#include "thermal_core.h"
#include "thermal_msg_helper.h"
#include "thermal_zone_helper.h"

#define THERMAL_NTC_MAX_TRIP	132000
#define THERMAL_NTC_MIN_TRIP	-23000
#define NTC_THERMAL_MAX_CHANNEL	8
#define NTC_INIT_DELAY_MS	71000
#define THERMAL_NTC_ERR_READING_IGNORE_TIME_MSEC	30000
#define THERMAL_NTC_OT_EVENT 0x1
#define THERMAL_NTC_UT_EVENT 0x2

enum adc_map_index {
	ADC_MAP_REGISTER_VALUE,
	ADC_MAP_TEMPERATURE,
	ADC_MAP_INDEX_MAX,
};
/*
 * The conversion formula uses logarthm function, which is not supported in kernel.
 * But the value of register to temperature reading is almost linear curve. Inorder to
 * simplify the calculation in kernel, we use this curve and its slope. The below
 * values map to the specific points in curve and more points are added, when the
 * slope changes.
 *
 * docs.google.com/spreadsheets/d/1-otiGsDg619KFP5OnQErquyVAySwye8-99avmfYapOY/edit?usp=drive_link
 *
 * The 2D array has the register value in index 0 and temperature in index 1
 */
static int adc_temperature_data[][ADC_MAP_INDEX_MAX] = {
	{63500, -23000}, {61000, -10000}, {58000, -1000}, {57000, 2000}, {52000, 12000},
	{47000, 20000}, {33000, 40000}, {26000, 50000}, {20000, 61000}, {18000, 64000},
	{15000, 71000}, {10000, 86000}, {2900, 132000},
};

static int adc_thresh_data[][ADC_MAP_INDEX_MAX] = {
	{0xF7D, -23000}, {0xD8D, 5000}, {0xCF0, 10000}, {0xC3E, 15000}, {0xB79, 20000},
	{0xAA5, 25000}, {0x9C7, 30000}, {0x8E5, 35000}, {0x805, 40000}, {0x72C, 45000},
	{0x65D, 50000}, {0x59D, 55000}, {0x4EE, 60000}, {0x44F, 65000}, {0x3C2, 70000},
	{0x345, 75000}, {0x2D8, 80000}, {0x279, 85000}, {0x226, 90000}, {0x1DF, 95000},
	{0xB4, 132000},
};

int thermal_ntc_configure(struct platform_device *pdev, int sensor_ct);
int thermal_ntc_map_data(int input, enum adc_map_index inp_type,
			 int data[][ADC_MAP_INDEX_MAX], size_t data_len);
int thermal_ntc_get_temp(struct thermal_zone_device *tzd, int *temp);
int thermal_ntc_set_trips(struct thermal_zone_device *tzd, int low_trip,
			  int high_trip);
int thermal_ntc_process_irq(struct notifier_block *nb, unsigned long val, void *data);
int thermal_ntc_set_fault_trip(struct thermal_zone_device *tzd, int trip);
void thermal_ntc_cleanup(struct platform_device *pdev);
int __thermal_ntc_set_trip_temp(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
				int temp);
int thermal_ntc_set_trip_temp(struct thermal_zone_device *tzd, const struct thermal_trip *trip,
			      int temp);
int thermal_ntc_configure_sensor(struct google_sensor_data *sens);
int thermal_ntc_post_configure(struct list_head *head);
int thermal_ntc_cleanup_sensor(struct google_sensor_data *sens);

static struct notifier_block thermal_ntc_irq_notifier = {
	.notifier_call = thermal_ntc_process_irq,
};
static struct list_head *thermal_ntc_sens_list;
void __thermal_ntc_dump_thermal_history(struct google_sensor_data *gsens);
int __thermal_ntc_get_filtered_temp(struct google_sensor_data *gsens);
void __thermal_ntc_temp_log_update(struct google_sensor_data *gsens, int temp, int reg);
void __thermal_ntc_flush_work(void);
#endif //_THERMAL_NTC_HELPER_H_

