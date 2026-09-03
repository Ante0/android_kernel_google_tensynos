// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_msg_helper.c Helper to send message/request to CPM.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include "thermal_msg_helper.h"
#include "thermal_msg_mock.h"

/*
 * __msg_gen_send_req_with_status_check - add status check to thermal_msg_send_req
 *
 * @message: composed message sent to cpm
 *
 * Return: 0 on success
 *	Other the error return code sent by cpm_thermal_cpm_send_mbox_request()
 *	Any error code from the status field of the message
 */
int __msg_gen_send_req_with_status_check(union thermal_cpm_message *message)
{
	int ret = 0, status = 0;

	if (!message)
		return -EINVAL;

	ret = thermal_msg_send_req(message, &status);

	if (ret)
		return ret;

	return status;
}

int msg_ntc_channel_generic_req(int ch_id, enum thermal_cpm_mbox_req_type type)
{
	union thermal_cpm_message therm_msg;

	if (ch_id < 0)
		return -EINVAL;

	therm_msg.ntc_req.type = type;
	therm_msg.ntc_req.chid = ch_id;

	return __msg_gen_send_req_with_status_check(&therm_msg);
}

int msg_ntc_channel_generic_req_with_val(int ch_id, enum thermal_cpm_mbox_req_type type,
						int val1, int val2)
{
	union thermal_cpm_message therm_msg;

	if (ch_id < 0)
		return -EINVAL;

	therm_msg.ntc_req.type = type;
	therm_msg.ntc_req.chid = ch_id;
	therm_msg.ntc_req.req_rsvd0 = val1;
	therm_msg.ntc_req.req_rsvd1 = val2;

	return __msg_gen_send_req_with_status_check(&therm_msg);
}

int msg_ntc_channel_generic_read(int ch_id, enum thermal_cpm_mbox_req_type type,
					int *reg_data)
{
	union thermal_cpm_message therm_msg;
	int ret = 0;

	if (ch_id < 0 || !reg_data)
		return -EINVAL;

	therm_msg.ntc_req.type = type;
	therm_msg.ntc_req.chid = ch_id;

	ret = __msg_gen_send_req_with_status_check(&therm_msg);
	if (ret)
		return ret;

	*reg_data = therm_msg.resp.temp;

	return 0;
}

/*
 * msg_ntc_channel_enable - Send a message to enable NTC channel.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_enable(void)
{
	return msg_ntc_channel_generic_req(0,
					   THERMAL_SERVICE_COMMAND_NTC_ENABLE);
}

/*
 * msg_ntc_channel_read_avg_temp - Fetch NTC average temperature.
 *
 * @ch_id: The NTC channel ID to get temperature.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_read_avg_temp(int ch_id, int *reg_data)
{
	return msg_ntc_channel_generic_read(ch_id, THERMAL_SERVICE_COMMAND_NTC_READ_AVG,
					    reg_data);
}

/*
 * msg_ntc_channel_read_temp - Fetch NTC temperature.
 *
 * @ch_id: The NTC channel ID to get temperature.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_read_temp(int ch_id, int *reg_data)
{
	return msg_ntc_channel_generic_read(ch_id, THERMAL_SERVICE_COMMAND_NTC_READ,
					    reg_data);
}

/*
 * msg_ntc_channel_read_irq_status - Fetch IRQ status sticky bit.
 *
 * @reg_data: status value array will be populated here.
 * @len: length of the array
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_read_irq_status(int *reg_data, int len)
{
	union thermal_cpm_message therm_msg;
	int ret = 0;

	if (!reg_data || len != THERMAL_NTC_IRQ_STATUS_BUF_LEN)
		return -EINVAL;

	therm_msg.ntc_req.type = THERMAL_SERVICE_COMMAND_NTC_IRQ_STATUS;
	therm_msg.ntc_req.chid = 0;

	ret = __msg_gen_send_req_with_status_check(&therm_msg);
	if (ret) {
		pr_err("thermal cpm message communication failed with ret:%d\n", ret);
		return -EIO;
	}

	reg_data[0] = therm_msg.resp.status;
	reg_data[1] = therm_msg.resp.status1;

	return 0;
}

/*
 * msg_ntc_channel_clear_and_mask_irq - Clear the IRQ status sticky bit
 * and then mask/unmask the IRQ.
 *
 * @ch_id: The NTC channel ID.
 * @enable: Is the request is to enable the IRQ.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_clear_and_mask_irq(int ch_id, bool enable)
{
	return msg_ntc_channel_generic_req_with_val(
			ch_id, THERMAL_SERVICE_COMMAND_NTC_IRQ_CLEAR_AND_MASK,
			enable ? 1 : 0, 0);
}

/*
 * msg_ntc_channel_mask_fault_irq - Enable or disable the Fault IRQ..
 *
 * @ch_id: The NTC channel ID.
 * @enable: Is the request is to enable the IRQ.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_mask_fault_irq(int ch_id, bool enable)
{
	return msg_ntc_channel_generic_req_with_val(
			ch_id, THERMAL_SERVICE_COMMAND_NTC_FAULT_IRQ_CLEAR_AND_MASK,
			enable ? 1 : 0, 0);
}

/*
 * msg_ntc_channel_clear_data_reg - Clears all the NTC conversion data.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_clear_data_reg(void)
{
	return msg_ntc_channel_generic_req(0, THERMAL_SERVICE_COMMAND_NTC_CLEAR_DATA);
}

/*
 * msg_ntc_channel_set_trips - Write the threshold and hysteresis data.
 *
 * @ch_id: The NTC channel ID.
 * @trip_val: trip temperature register value.
 * @trip_hyst: hysteresis temperature register value.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_set_trips(int ch_id, int trip_val, int trip_hyst)
{
	return msg_ntc_channel_generic_req_with_val(
			ch_id, THERMAL_SERVICE_COMMAND_NTC_SET_TRIPS, trip_val, trip_hyst);
}

/*
 * msg_ntc_channel_set_fault_trip - Write the h/w fault threhsold.
 *
 * @ch_id: The NTC channel ID.
 * @trip_val: trip temperature register value.
 *
 * Returns the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_ntc_channel_set_fault_trip(int ch_id, int trip_val)
{
	return msg_ntc_channel_generic_req_with_val(
			ch_id, THERMAL_SERVICE_COMMAND_NTC_SET_FAULT_TRIP, trip_val, 0);
}

/*
 * TMU messages
 */

/*
 * msg_tmu_get_temp - Read temperature from a thermal zone
 *
 * @tz_id: thermal zone id
 * @temperature: sensor temperature output
 *
 * Return: 0 on success
 *	-EINVAL: null temperature output pointer
 *	Other the error message sent by cpm_thermal_cpm_send_mbox_request().
 */
int msg_tmu_get_temp(u8 tz_id, u8 *temperature)
{
	union thermal_cpm_message message;
	int ret, status;

	if (!temperature)
		return -EINVAL;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_TEMP;
	message.req.tzid = tz_id;

	ret = thermal_msg_send_req(&message, &status);
	if (ret) {
		pr_err("thermal cpm message communication failed with ret:%d\n", ret);
		return -EIO;
	}
	if (status)
		return -ENODATA;

	*temperature = message.resp.temp;

	return 0;
}

/*
 * msg_tmu_set_trip_generic - generic function of thermal zone trip point setters
 *
 * @tz_id: thermal zone id
 * @val: pointer to set value array
 * @num_val: size of the array above
 * @command: cpm mbox request type
 *
 * Return: 0 on success
 *	-EINVAL: null temperature array pointer or wrong size of array
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
static int msg_tmu_set_trip_generic(u8 tz_id, u8 *val, int num_val,
				    enum thermal_cpm_mbox_req_type command)
{
	union thermal_cpm_message message;

	/* exact 8 trip points are expected to fulfill the message */
	if (!val || num_val != THERMAL_TMU_NR_TRIPS)
		return -EINVAL;

	message.req.type = command;
	message.req.tzid = tz_id;
	message.req.req_rsvd0 = val[0];
	message.req.req_rsvd1 = val[1];
	message.req.req_rsvd2 = val[2];
	message.req.req_rsvd3 = val[3];
	message.req.req_rsvd4 = val[4];
	message.req.req_rsvd5 = val[5];
	message.req.req_rsvd6 = val[6];
	message.req.req_rsvd7 = val[7];

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_tmu_set_trip_temp - Write trip temperatures of a thermal zone
 *
 * @tz_id: thermal zone id
 * @temperature: pointer to trip temperature array
 * @num_temperature: size of the array above
 *
 * Return: 0 on success
 *	-EINVAL: null temperature array pointer
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_trip_temp(u8 tz_id, u8 *temperature, int num_temperature)
{
	return msg_tmu_set_trip_generic(tz_id, temperature, num_temperature,
					THERMAL_SERVICE_COMMAND_SET_TRIP_TEMP);
}

/*
 * msg_tmu_set_trip_hyst - Write hysteresises of a thermal zone trip point
 *
 * @tz_id: thermal zone id
 * @hysteresis: pointer to trip hysteresis array
 * @num_hysteresis: size of the array above
 *
 * Return: 0 on success
 *	-EINVAL: null temperature array pointer
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_trip_hyst(u8 tz_id, u8 *hysteresis, int num_hysteresis)
{
	return msg_tmu_set_trip_generic(tz_id, hysteresis, num_hysteresis,
					THERMAL_SERVICE_COMMAND_SET_TRIP_HYST);
}

/*
 * msg_tmu_set_trip_type - Write type of a thermal zone trip point
 *
 * @tz_id: thermal zone id
 * @type: pointer to trip type array
 * @num_type: size of the array above
 *
 * Return: 0 on success
 *	-EINVAL: null temperature array pointer
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_trip_type(u8 tz_id, u8 *type, int num_type)
{
	return msg_tmu_set_trip_generic(tz_id, type, num_type,
					THERMAL_SERVICE_COMMAND_SET_TRIP_TYPE);
}

/*
 * msg_tmu_get_trip_counter_snapshot
 *
 * This function Requests CPM to take a snapshot of selected thermal zone and store it
 * in the trip counter section of thermal shared memory
 *
 * @tz_id: thermal zone id
 *
 * Return: 0 on success
 *	-EINVAL: invalid tz_id
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_get_trip_counter_snapshot(u8 tz_id)
{
	union thermal_cpm_message message;

	if (tz_id >= HW_THERMAL_ZONE_MAX)
		return -EINVAL;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_TRIP_COUNTER_SNAPSHOT;
	message.req.tzid = tz_id;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_tmu_set_gov_param - Set a tmu governor parameter
 *
 * @tz_id: thermal zone id
 * @type: governor parameter type
 * @val: set value of the governor type
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_gov_param(u8 tz_id, u8 type, int val)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_PARAM;
	message.req.tzid = tz_id;
	message.req.rsvd = type;
	// TODO: b/382740252 to have 32 bit data entry managed
	message.data[1] = val;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_tmu_set_gov_select - Set governor bit select of a thermal zone
 *
 * @tz_id: thermal zone id
 * @gov_select: set value of the governor bit select
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_gov_select(u8 tz_id, u8 gov_select)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_GOV_SELECT;
	message.req.tzid = tz_id;
	message.req.rsvd = gov_select;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_tmu_set_polling_delay_ms - Set polling delay of a thermal zone
 *
 * @tz_id: thermal zone id
 * @delay: set value of the polling delay in meliseconds
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_polling_delay_ms(u8 tz_id, u16 delay)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_POLLING_DELAY;
	message.req.tzid = tz_id;
	// TODO: b/382740252 to have 32 bit data entry managed
	message.data[1] = delay;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_tmu_set_power_table - Set state2power table by index
 *
 * @cdev_id: cooling device id
 * @idx: state2power index to set
 * @val: power to set
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_power_table(enum hw_dev_type cdev_id, u8 idx, int val)
{
	union thermal_cpm_message message;
	enum hw_thermal_zone_id tz_id;
	int ret;

	ret = thermal_msg_cdev_to_hw_tzid(cdev_id, &tz_id);
	if (ret) {
		pr_err("Invalid cdev ID:%d.\n", cdev_id);
		return ret;
	}
	message.req.type = THERMAL_SERVICE_COMMAND_SET_POWERTABLE;
	message.req.tzid = tz_id;
	message.req.rsvd = idx;
	// TODO: b/382740252 to have 32 bit data entry managed
	message.data[1] = val;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_tmu_get_power_table - Get state2power by index
 *
 * @cdev_id: cooling device id
 * @idx: index of state2power table to get
 * @val: pointer to the output
 * @max_state_idx: output of max state index, which can be used to determine table size
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_get_power_table(enum hw_dev_type cdev_id, u8 idx, int *val,
			    int *max_state_idx)
{
	union thermal_cpm_message message;
	int ret;
	enum hw_thermal_zone_id tz_id;

	if (!val)
		return -EINVAL;

	ret = thermal_msg_cdev_to_hw_tzid(cdev_id, &tz_id);
	if (ret) {
		pr_err("Invalid cdev ID:%d.\n", cdev_id);
		return ret;
	}

	message.req.type = THERMAL_SERVICE_COMMAND_GET_POWERTABLE;
	message.req.tzid = tz_id;
	message.req.rsvd = idx;

	ret = __msg_gen_send_req_with_status_check(&message);
	if (ret) {
		pr_err("thermal cpm message communication failed with ret:%d\n", ret);
		return -EIO;
	}

	// TODO: b/382740252 to have 32 bit data entry managed
	*val = message.data[1];
	if (max_state_idx != NULL)
		*max_state_idx = message.data[2];

	return 0;
}

/*
 * msg_tmu_set_temp_lut - Set temp lut entry of a thermal zone
 *
 * @tz_id: thermal zone id
 * @temperature: the temperature threshold below which cdev_state is allowed to use
 * @cdev_state: The lowest cdev_state allowed to use
 * @append: 0 to restart setting a new temp lut; 1 to append an entry to current table
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_set_temp_lut(u8 tz_id, u8 temperature, u8 cdev_state, bool append)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_TEMP_LUT;
	message.req.tzid = tz_id;
	message.req.req_rsvd0 = temperature;
	message.req.req_rsvd1 = cdev_state;
	message.req.req_rsvd2 = append;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_tmu_get_temp_lut - Get temp lut entry of a thermal zone
 *
 * @tz_id: thermal zone id
 * @index: The index of the temp lut to get
 * @temperature: pointer to the temperature value
 * @cdev_state: pointer to the cdev_state
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_tmu_get_temp_lut(u8 tz_id, u8 index, u8 *temperature, u8 *cdev_state)
{
	union thermal_cpm_message message;
	int ret;

	if (!temperature || !cdev_state || index >= THERMAL_TEMP_LUT_MAX_ENTRIES)
		return -EINVAL;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_TEMP_LUT;
	message.req.tzid = tz_id;
	message.req.req_rsvd0 = index;

	ret = __msg_gen_send_req_with_status_check(&message);
	if (ret)
		return ret;

	*temperature = message.resp.temp;
	*cdev_state = message.resp.status;

	return 0;
}

/* Thermal shared memory section support*/

/*
 * msg_thermal_sm_get_section_addr - Request the address of a thermal shared memory section
 *
 * @section: requested shared memory section
 * @version: section data structure version output
 * @addr: requested shared memory address
 * @size: size of requested section
 *
 * Return: 0 on success
 * 	Other error code from com_thermal_send_mbox_request()
 */
int msg_thermal_sm_get_section_addr(u8 section, u32 *version, u32 *addr, u32 *size)
{
	union thermal_cpm_message message;
	int ret = 0;

	if (!version || !addr || !size)
		return  -EINVAL;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_SM;
	message.req.rsvd = section;

	ret = __msg_gen_send_req_with_status_check(&message);
	if (ret) {
		pr_err("thermal cpm message communication failed with ret:%d\n", ret);
		return -EIO;
	}

	*version = (u32)message.resp.ret;
	// TODO: b/382740252 to have 32 bit data entry managed
	*addr = message.data[1];
	*size = message.data[2];

	return 0;
}

/* Thermal stats message support*/

/*
 * msg_stats_get_tr_stats - Request CPM to prepare temperature residency to shared memory
 *
 * @tz_id: thermal zone id
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_stats_get_tr_stats(u8 tz_id)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_TR_STATS;
	message.req.tzid = tz_id;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_stats_reset_tr_stats - reset CPM thermal stats
 *
 * @tz_id: thermal zone id
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_stats_reset_tr_stats(u8 tz_id)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_RESET_TR_STATS;
	message.req.tzid = tz_id;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_stats_get_tr_thresholds
 *
 * This function requests CPM to prepare temperature residency thresholds to shared memory
 *
 * @tz_id: thermal zone id
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_stats_get_tr_thresholds(u8 tz_id)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_GET_TR_THRESHOLDS;
	message.req.tzid = tz_id;

	return __msg_gen_send_req_with_status_check(&message);
}

/*
 * msg_stats_set_tr_thresholds
 *
 * This function requests CPM to update temperature residency thresholds from shared memory
 *
 * @tz_id: thermal zone id
 *
 * Return: 0 on success
 *	Other the error message sent by thermal_cpm_send_mbox_request().
 */
int msg_stats_set_tr_thresholds(u8 tz_id)
{
	union thermal_cpm_message message;

	message.req.type = THERMAL_SERVICE_COMMAND_SET_TR_THRESHOLDS;
	message.req.tzid = tz_id;

	return __msg_gen_send_req_with_status_check(&message);
}
