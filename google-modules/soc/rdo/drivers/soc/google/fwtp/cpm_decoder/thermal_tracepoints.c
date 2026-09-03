// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024 Google LLC */

#include <linux/kernel.h>
#include "cpm_tracepoint_decoder.h"
#include "thermal_cpm_mbox.h"
#include "thermal_tracepoints.h"

union curr_state_u {
	struct {
		uint8_t cdev_state;
		uint8_t tzid;
		uint8_t temp;
		uint8_t ctrl_temp;
	};
	uint32_t word32;
};

#define CURR_STATE_STR_LEN 13
static enum tracepoint_handle curr_state_handler(const char *tp_string,
						 u32 payload, u64 timestamp)
{
	union curr_state_u state;
	char clock_name[CURR_STATE_STR_LEN];
	const char *tz_name;

	state.word32 = payload;

	tz_name = thermal_cpm_mbox_get_tz_name(state.tzid);
	if (!tz_name)
		return CLIENT_TP_HANDLING_ERROR;

	scnprintf(clock_name, sizeof(clock_name), "%s_temp", tz_name);
	add_cpm_param_trace(clock_name, (unsigned int)state.temp, timestamp);

	scnprintf(clock_name, sizeof(clock_name), "%s_cdev", tz_name);
	add_cpm_param_trace(clock_name, (unsigned int)state.cdev_state,
			    timestamp);

	return CLIENT_TP_HANDLING_NOT_COMPLETE;
}

struct client_tracepoint thermal_tj_pid_curr_state = {
	.enabled = true,
	.tp_string = "GOVcurr %d",
	.init = NULL,
	.handler = curr_state_handler,
	.exit = NULL
};

#define SENSOR_STR_LEN 16
static enum tracepoint_handle thermsen_handler(const char *tp_string,
					       u32 payload, u64 timestamp)
{
	char sensor_error_clock_name[SENSOR_STR_LEN];
	char sensor_temp_clock_name[SENSOR_STR_LEN];

	u16 temp = payload & 0xFFFF;
	u8 id = (payload >> 16) & 0xFF;
	u8 error = (payload >> 24) & 0xFF;

	scnprintf(sensor_temp_clock_name, sizeof(sensor_temp_clock_name),
		  "Sensor_%d", id);
	scnprintf(sensor_error_clock_name, sizeof(sensor_error_clock_name),
		  "SenErr_%d", id);

	/* Set sensor error */
	add_cpm_param_trace(sensor_error_clock_name, error != 0, timestamp);
	/* Add sensor temperature */
	add_cpm_param_trace(sensor_temp_clock_name, temp, timestamp);

	return CLIENT_TP_HANDLING_COMPLETE;
}

struct client_tracepoint thermal_tj_sensors = { .enabled = true,
						.tp_string = "thermSen %d",
						.init = NULL,
						.handler = thermsen_handler,
						.exit = NULL };
MODULE_LICENSE("GPL");

