// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_msg_helper_tests.c Test suite to test all the message helper functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/thermal.h>
#include <kunit/test.h>

#include "thermal_msg_helper.h"
#include "thermal_msg_mock.h"

static int mock_ret_code;
static int mock_status_code;
static int mock_cdev_to_hw_tzid_ret;

static union thermal_cpm_message mock_msg_inp, mock_resp;

int mock_cpm_msg_send(union thermal_cpm_message *therm_msg, int *status)
{
	if (!therm_msg)
		return -EINVAL;

	memcpy(&mock_msg_inp, therm_msg, sizeof(mock_msg_inp));
	memcpy(therm_msg, &mock_resp, sizeof(mock_resp));
	*status = mock_status_code;
	return mock_ret_code;
}

int mock_msg_cdev_to_hw_tzid(enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid)
{
	*hw_tzid = (enum hw_thermal_zone_id)cdev_id;
	return mock_cdev_to_hw_tzid_ret;
}

static bool verify_req_msg(int ch_id, enum thermal_cpm_mbox_req_type msg_type)
{
	return (mock_msg_inp.ntc_req.type == msg_type &&
		mock_msg_inp.ntc_req.chid == ch_id);
}

static bool verify_req_msg_with_val(int ch_id, enum thermal_cpm_mbox_req_type msg_type,
				    int val, int val1)
{
	return (mock_msg_inp.ntc_req.type == msg_type &&
		mock_msg_inp.ntc_req.chid == ch_id &&
		mock_msg_inp.ntc_req.req_rsvd0 == val &&
		mock_msg_inp.ntc_req.req_rsvd1 == val1);
}

static int thermal_msg_helper_test_init(struct kunit *test)
{
	mock_ret_code = 0;
	mock_status_code = 0;

	memset(&mock_resp, 0, sizeof(mock_resp));

	return 0;
}

static void thermal_cpm_send_req_with_status_check(struct kunit *test)
{
	// success
	KUNIT_EXPECT_EQ(test, __msg_gen_send_req_with_status_check(&mock_msg_inp), 0);

	// Invalid argment
	KUNIT_EXPECT_EQ(test, __msg_gen_send_req_with_status_check(NULL), -EINVAL);

	// ret/comm error
	// set both mock codes to verify ret could block status code
	mock_ret_code = -EINVAL;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, __msg_gen_send_req_with_status_check(&mock_msg_inp), -EINVAL);
	// status error
	mock_ret_code = 0;
	KUNIT_EXPECT_EQ(test, __msg_gen_send_req_with_status_check(&mock_msg_inp), -EOPNOTSUPP);
}



static void thermal_cpm_gen_req(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_req(7,
						    THERMAL_SERVICE_COMMAND_NTC_ENABLE),
			mock_ret_code);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(7, THERMAL_SERVICE_COMMAND_NTC_ENABLE));
	/* Invalid arg. */
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_req(-7,
						    THERMAL_SERVICE_COMMAND_NTC_ENABLE),
			-EINVAL);
	/* Test error return value. */
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_req(7,
						    THERMAL_SERVICE_COMMAND_NTC_ENABLE),
			mock_ret_code);
}

static void thermal_cpm_gen_req_with_val(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_req_with_val(7,
						    THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						    1, 2),
			mock_ret_code);
	KUNIT_EXPECT_TRUE(test, verify_req_msg_with_val(7, THERMAL_SERVICE_COMMAND_NTC_ENABLE,
							1, 2));
	/* Invalid arg. */
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_req_with_val(-7,
						    THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						    1, 2),
			-EINVAL);
	/* Test error return value. */
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_req_with_val(7,
						    THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						    1, 2),
			mock_ret_code);
}

static void thermal_cpm_gen_read(struct kunit *test)
{
	int reg_data;

	/* Test mbox error return value. */
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_read(7,
						     THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						     &reg_data),
			mock_ret_code);
	/* Test invalid arg. */
	mock_ret_code = 0;
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_read(7,
						     THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						     NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_read(-7,
						     THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						     NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_read(-7, THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						     &reg_data),
			-EINVAL);

	mock_ret_code = 0;
	mock_resp.resp.temp = 0x10;
	KUNIT_EXPECT_EQ(test,
			msg_ntc_channel_generic_read(7,
						     THERMAL_SERVICE_COMMAND_NTC_ENABLE,
						     &reg_data),
			0);
	KUNIT_EXPECT_EQ(test, reg_data, 0x10);
}

static void thermal_cpm_ch_enable(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_enable(), mock_ret_code);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(0, THERMAL_SERVICE_COMMAND_NTC_ENABLE));
}

static void thermal_cpm_clear_data(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_clear_data_reg(), mock_ret_code);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(0, THERMAL_SERVICE_COMMAND_NTC_CLEAR_DATA));
}

static void thermal_cpm_clear_and_mask_irq(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_clear_and_mask_irq(1, true), mock_ret_code);
	KUNIT_EXPECT_TRUE(test,
			  verify_req_msg_with_val(1,
						  THERMAL_SERVICE_COMMAND_NTC_IRQ_CLEAR_AND_MASK,
						  1, 0));
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_clear_and_mask_irq(1, false), mock_ret_code);
	KUNIT_EXPECT_TRUE(test,
			  verify_req_msg_with_val(1,
						  THERMAL_SERVICE_COMMAND_NTC_IRQ_CLEAR_AND_MASK,
						  0, 0));
}

static void thermal_cpm_mask_fault_irq(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_mask_fault_irq(1, true), mock_ret_code);
	KUNIT_EXPECT_TRUE(test,
			  verify_req_msg_with_val(
					  1,
					  THERMAL_SERVICE_COMMAND_NTC_FAULT_IRQ_CLEAR_AND_MASK,
					  1, 0));
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_mask_fault_irq(1, false), mock_ret_code);
	KUNIT_EXPECT_TRUE(test,
			  verify_req_msg_with_val(
					  1,
					  THERMAL_SERVICE_COMMAND_NTC_FAULT_IRQ_CLEAR_AND_MASK,
					  0, 0));
}

static void thermal_cpm_set_trips(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_set_trips(1, 45000, 40000), mock_ret_code);
	KUNIT_EXPECT_TRUE(test,
			  verify_req_msg_with_val(1,
						  THERMAL_SERVICE_COMMAND_NTC_SET_TRIPS,
						  45000, 40000));
}

static void thermal_cpm_set_critical_trips(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_set_fault_trip(1, 45000), mock_ret_code);
	KUNIT_EXPECT_TRUE(test,
			  verify_req_msg_with_val(1,
						  THERMAL_SERVICE_COMMAND_NTC_SET_FAULT_TRIP,
						  45000, 0));
}

static void thermal_cpm_read_temp(struct kunit *test)
{
	int reg_data;

	mock_resp.resp.temp = 0x10;
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_read_temp(7, &reg_data), mock_ret_code);
	KUNIT_EXPECT_EQ(test, reg_data, 0x10);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(7, THERMAL_SERVICE_COMMAND_NTC_READ));
}

static void thermal_cpm_read_avg_temp(struct kunit *test)
{
	int reg_data;

	mock_resp.resp.temp = 0x10;
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_read_avg_temp(7, &reg_data), mock_ret_code);
	KUNIT_EXPECT_EQ(test, reg_data, 0x10);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(7, THERMAL_SERVICE_COMMAND_NTC_READ_AVG));
}

static void thermal_cpm_read_irq_status(struct kunit *test)
{
	int reg_data[2];

	mock_resp.resp.status = 0xF0;
	mock_resp.resp.status1 = 0xFA;
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_read_irq_status(reg_data, 2), mock_ret_code);
	KUNIT_EXPECT_EQ(test, reg_data[0], 0xF0);
	KUNIT_EXPECT_EQ(test, reg_data[1], 0xFA);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(0, THERMAL_SERVICE_COMMAND_NTC_IRQ_STATUS));

	// Invalid Args
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_read_irq_status(NULL, 2), -EINVAL);
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_read_irq_status(reg_data, -1), -EINVAL);

	// CPM comm failed
	mock_ret_code = -ENODEV;
	KUNIT_EXPECT_EQ(test, msg_ntc_channel_read_irq_status(reg_data, 2), -EIO);
}

static void thermal_tmu_get_temp(struct kunit *test)
{
	u8 temperature, mock_tzid = 1;

	mock_resp.resp.temp = 50;

	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, &temperature), mock_ret_code);
	KUNIT_EXPECT_EQ(test, temperature, 50);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(mock_tzid, THERMAL_SERVICE_COMMAND_GET_TEMP));

	// Invalid Args
	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, NULL), -EINVAL);

	// CPM comm failed
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, &temperature), -EIO);
	// CPM status error: invalid sswrp temp
	mock_ret_code = 0;
	mock_status_code = CPM_STATUS_NOT_READY;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, &temperature), -ENODATA);
	mock_status_code = CPM_STATUS_FAILED_PRECONDITION;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, &temperature), -ENODATA);
	mock_status_code = CPM_STATUS_BAD_STATE;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, &temperature), -ENODATA);
	mock_status_code = CPM_STATUS_OFFLINE;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, &temperature), -ENODATA);
	// CPM status error: other error
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_temp(mock_tzid, &temperature), -ENODATA);
}

static bool verify_tmu_trip_msg(u8 tz_id, enum thermal_cpm_mbox_req_type msg_type, u8 trip_val[])
{
	return (mock_msg_inp.req.type == msg_type &&
		mock_msg_inp.req.tzid == tz_id &&
		mock_msg_inp.req.req_rsvd0 == trip_val[0] &&
		mock_msg_inp.req.req_rsvd1 == trip_val[1] &&
		mock_msg_inp.req.req_rsvd2 == trip_val[2] &&
		mock_msg_inp.req.req_rsvd3 == trip_val[3] &&
		mock_msg_inp.req.req_rsvd4 == trip_val[4] &&
		mock_msg_inp.req.req_rsvd5 == trip_val[5] &&
		mock_msg_inp.req.req_rsvd6 == trip_val[6] &&
		mock_msg_inp.req.req_rsvd7 == trip_val[7]);
}

static void thermal_tmu_set_trip(struct kunit *test)
{
	u8 mock_tzid = 1, trip_val[8] = {0, 1, 2, 3, 4, 5, 6, 7};

	// trip temp
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_temp(mock_tzid, trip_val, THERMAL_TMU_NR_TRIPS),
						    mock_ret_code);
	KUNIT_EXPECT_TRUE(test, verify_tmu_trip_msg(mock_tzid,
						    THERMAL_SERVICE_COMMAND_SET_TRIP_TEMP,
						    trip_val));

	// trip hyst
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_hyst(mock_tzid, trip_val, THERMAL_TMU_NR_TRIPS),
						    mock_ret_code);
	KUNIT_EXPECT_TRUE(test, verify_tmu_trip_msg(mock_tzid,
						    THERMAL_SERVICE_COMMAND_SET_TRIP_HYST,
						    trip_val));

	// trip type
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_type(mock_tzid, trip_val, 8), mock_ret_code);
	KUNIT_EXPECT_TRUE(test, verify_tmu_trip_msg(mock_tzid,
						    THERMAL_SERVICE_COMMAND_SET_TRIP_TYPE,
						    trip_val));

	// Invalid Args: null pointer
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_temp(mock_tzid, NULL, 8), -EINVAL);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_hyst(mock_tzid, NULL, 8), -EINVAL);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_type(mock_tzid, NULL, 8), -EINVAL);
	// Invalid Args: inadequate number of trip values
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_temp(mock_tzid, trip_val, 6), -EINVAL);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_hyst(mock_tzid, trip_val, 6), -EINVAL);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_type(mock_tzid, trip_val, 6), -EINVAL);

	// CPM comm failed
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_temp(mock_tzid, trip_val, 8), -EINVAL);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_hyst(mock_tzid, trip_val, 8), -EINVAL);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_type(mock_tzid, trip_val, 8), -EINVAL);
	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_temp(mock_tzid, trip_val, 8), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_hyst(mock_tzid, trip_val, 8), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, msg_tmu_set_trip_type(mock_tzid, trip_val, 8), -EOPNOTSUPP);
}

static void thermal_tmu_get_trip_counter_snapshot(struct kunit *test)
{
	enum hw_thermal_zone_id mock_tzid = HW_THERMAL_ZONE_0;

	// success
	KUNIT_EXPECT_EQ(test, msg_tmu_get_trip_counter_snapshot((u8)mock_tzid), 0);

	// invalid tz_id
	KUNIT_EXPECT_EQ(test, msg_tmu_get_trip_counter_snapshot(HW_THERMAL_ZONE_MAX), -EINVAL);

	// CPM comm failed
	mock_ret_code = -ENODEV;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_trip_counter_snapshot((u8)mock_tzid), -ENODEV);

	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_trip_counter_snapshot((u8)mock_tzid), -EOPNOTSUPP);
}

static void thermal_tmu_set_gov_param(struct kunit *test)
{
	u8 mock_tzid = 1, mock_param_type = 2;
	int mock_param_value = 10;

	KUNIT_EXPECT_EQ(test, msg_tmu_set_gov_param(mock_tzid, mock_param_type, mock_param_value),
			0);
	KUNIT_EXPECT_EQ(test, mock_msg_inp.data[1], mock_param_value);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(mock_tzid, THERMAL_SERVICE_COMMAND_SET_PARAM));

	// CPM comm failed
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_gov_param(mock_tzid, mock_param_type, mock_param_value),
			-EINVAL);
	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_gov_param(mock_tzid, mock_param_type, mock_param_value),
			-EOPNOTSUPP);
}

static void thermal_tmu_set_gov_select(struct kunit *test)
{
	u8 mock_tzid = 1, mock_gov_select = 0x55;

	KUNIT_EXPECT_EQ(test, msg_tmu_set_gov_select(mock_tzid, mock_gov_select), mock_ret_code);
	KUNIT_EXPECT_EQ(test, mock_msg_inp.req.rsvd, mock_gov_select);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(mock_tzid, THERMAL_SERVICE_COMMAND_SET_GOV_SELECT));

	// CPM comm failed
	// ret error code indicates CPM comm errors and should block status code
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_gov_select(mock_tzid, mock_gov_select), -EINVAL);
	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_gov_select(mock_tzid, mock_gov_select), -EOPNOTSUPP);
}

static void thermal_tmu_set_power_table(struct kunit *test)
{
	u8 mock_cdevid = 1, mock_idx = 2;
	int mock_val = 2000;

	KUNIT_EXPECT_EQ(test, msg_tmu_set_power_table(mock_cdevid, mock_idx, mock_val),
			mock_ret_code);
	KUNIT_EXPECT_EQ(test, mock_msg_inp.req.rsvd, mock_idx);
	KUNIT_EXPECT_EQ(test, mock_msg_inp.data[1], mock_val);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(mock_cdevid,
					       THERMAL_SERVICE_COMMAND_SET_POWERTABLE));

	// Invalid cdev.
	mock_cdev_to_hw_tzid_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_power_table(mock_cdevid, mock_idx, mock_val),
			-EINVAL);
	mock_cdev_to_hw_tzid_ret = 0;

	// CPM comm failed
	// ret error code indicates CPM comm errors and should block status code
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_power_table(mock_cdevid, mock_idx, mock_val), -EINVAL);
	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_power_table(mock_cdevid, mock_idx, mock_val),
			-EOPNOTSUPP);
}

static void thermal_tmu_get_power_table(struct kunit *test)
{
	u8 mock_cdevid = 1, mock_idx = 2;
	int mock_val, mock_max_state_idx;

	mock_resp.data[1] = 2000;
	mock_resp.data[2] = 20;

	KUNIT_EXPECT_EQ(test,
			msg_tmu_get_power_table(mock_cdevid, mock_idx, &mock_val,
						&mock_max_state_idx),
			mock_ret_code);
	KUNIT_EXPECT_EQ(test, mock_val, 2000);
	KUNIT_EXPECT_EQ(test, mock_max_state_idx, 20);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(mock_cdevid,
					       THERMAL_SERVICE_COMMAND_GET_POWERTABLE));
	// null max_state_idx should not return error
	KUNIT_EXPECT_EQ(test,
			msg_tmu_get_power_table(mock_cdevid, mock_idx, &mock_val, NULL),
			mock_ret_code);

	// Invalid cdev.
	mock_cdev_to_hw_tzid_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_get_power_table(mock_cdevid, mock_idx, &mock_val,
						      &mock_max_state_idx),
			-EINVAL);
	mock_cdev_to_hw_tzid_ret = 0;

	// CPM comm failed
	// ret error code indicates CPM comm errors and should block status code
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_power_table(mock_cdevid, mock_idx, mock_val), -EINVAL);
	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_power_table(mock_cdevid, mock_idx, mock_val),
			-EOPNOTSUPP);
}

static void thermal_tmu_set_polling_delay_ms(struct kunit *test)
{
	u8 mock_tzid = 1;
	u16 mock_polling_delay = 200;

	KUNIT_EXPECT_EQ(test, msg_tmu_set_polling_delay_ms(mock_tzid, mock_polling_delay),
							mock_ret_code);
	KUNIT_EXPECT_EQ(test, mock_msg_inp.data[1], mock_polling_delay);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(mock_tzid,
					       THERMAL_SERVICE_COMMAND_SET_POLLING_DELAY));

	// CPM comm failed
	// ret error code indicates CPM comm errors and should block status code
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_polling_delay_ms(mock_tzid, mock_polling_delay), -EINVAL);
	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_tmu_set_polling_delay_ms(mock_tzid, mock_polling_delay),
			-EOPNOTSUPP);
}

#define MOCK_TEMP_LUT_TZID		(0)
#define MOCK_TEMP_LUT_TEMP		(90)
#define MOCK_TEMP_LUT_STATE		(1)
#define MOCK_TEMP_LUT_TEMP_INVALID	(0)
#define MOCK_TEMP_LUT_STATE_INVALID	(255)
static void thermal_tmu_set_temp_lut(struct kunit *test)
{
	u8 tzid = MOCK_TEMP_LUT_TZID;
	u8 temp = MOCK_TEMP_LUT_TEMP;
	u8 cdev_state = MOCK_TEMP_LUT_STATE;
	bool append = false;

	mock_ret_code = 0;
	mock_status_code = 0;

	// verify message
	KUNIT_EXPECT_EQ(test,
		msg_tmu_set_temp_lut(tzid, temp, cdev_state, append),
		0);
	KUNIT_EXPECT_TRUE(test,
		verify_req_msg(tzid, THERMAL_SERVICE_COMMAND_SET_TEMP_LUT));
	KUNIT_EXPECT_EQ(test, mock_msg_inp.req.req_rsvd0, temp);
	KUNIT_EXPECT_EQ(test, mock_msg_inp.req.req_rsvd1, cdev_state);
	KUNIT_EXPECT_EQ(test, mock_msg_inp.req.req_rsvd2, append);

	// CPM comm failed
	// ret error code indicates CPM comm errors and should block status code
	mock_ret_code = -EBUSY;
	KUNIT_EXPECT_EQ(test,
		msg_tmu_set_temp_lut(tzid, temp, cdev_state, append),
		-EBUSY);

	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EPERM;
	KUNIT_EXPECT_EQ(test,
		msg_tmu_set_temp_lut(tzid, temp, cdev_state, append),
		-EPERM);
}

static void thermal_tmu_get_temp_lut(struct kunit *test)
{
	u8 tzid = MOCK_TEMP_LUT_TZID;
	u8 temp = MOCK_TEMP_LUT_TEMP_INVALID;
	u8 cdev_state = MOCK_TEMP_LUT_STATE_INVALID;

	mock_resp.resp.temp = MOCK_TEMP_LUT_TEMP;
	mock_resp.resp.status = MOCK_TEMP_LUT_STATE;
	mock_ret_code = 0;
	mock_status_code = 0;

	// verify resp
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, 0, &temp, &cdev_state),
		mock_ret_code);
	KUNIT_EXPECT_EQ(test, temp, MOCK_TEMP_LUT_TEMP);
	KUNIT_EXPECT_EQ(test, cdev_state, MOCK_TEMP_LUT_STATE);
	KUNIT_EXPECT_TRUE(test,
		verify_req_msg(tzid, THERMAL_SERVICE_COMMAND_GET_TEMP_LUT));

	// Invalid Args: null pointer
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, 0, NULL, &cdev_state), -EINVAL);
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, 0, &temp, NULL), -EINVAL);
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, 0, NULL, NULL), -EINVAL);
	// Invalid Args: index out of bound
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, THERMAL_TEMP_LUT_MAX_ENTRIES,
			&temp, &cdev_state), -EINVAL);
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, THERMAL_TEMP_LUT_MAX_ENTRIES + 1,
			&temp, &cdev_state), -EINVAL);

	// CPM comm failed
	// ret error code indicates CPM comm errors and should block status code
	mock_ret_code = -EBUSY;
	temp = MOCK_TEMP_LUT_TEMP_INVALID;
	cdev_state = MOCK_TEMP_LUT_STATE_INVALID;
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, 0, &temp, &cdev_state),
		-EBUSY);
	KUNIT_EXPECT_EQ(test, temp, MOCK_TEMP_LUT_TEMP_INVALID);
	KUNIT_EXPECT_EQ(test, cdev_state, MOCK_TEMP_LUT_STATE_INVALID);

	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EPERM;
	KUNIT_EXPECT_EQ(test,
		msg_tmu_get_temp_lut(tzid, 0, &temp, &cdev_state),
		-EPERM);
	KUNIT_EXPECT_EQ(test, temp, MOCK_TEMP_LUT_TEMP_INVALID);
	KUNIT_EXPECT_EQ(test, cdev_state, MOCK_TEMP_LUT_STATE_INVALID);
}

static void thermal_sm_get_section_addr(struct kunit *test)
{
	u8 mock_section = 1;
	u32 version, addr, size;

	mock_ret_code = 0;
	mock_status_code = 0;
	mock_resp.resp.ret = 1;
	mock_resp.data[1] = 0x12345678;
	mock_resp.data[2] = 20;

	KUNIT_EXPECT_EQ(test,
			msg_thermal_sm_get_section_addr(mock_section, &version, &addr, &size),
			mock_ret_code);
	KUNIT_EXPECT_EQ(test, version, 1);
	KUNIT_EXPECT_EQ(test, addr, 0x12345678);
	KUNIT_EXPECT_EQ(test, size, 20);
	KUNIT_EXPECT_TRUE(test, verify_req_msg(0, THERMAL_SERVICE_COMMAND_GET_SM));

	// Invalid args: version
	KUNIT_EXPECT_EQ(test, msg_thermal_sm_get_section_addr(mock_section, NULL, &addr, &size),
			-EINVAL);
	// Invalid args: addr
	KUNIT_EXPECT_EQ(test, msg_thermal_sm_get_section_addr(mock_section, &version, NULL, &size),
			-EINVAL);
	// Invalid args: size
	KUNIT_EXPECT_EQ(test, msg_thermal_sm_get_section_addr(mock_section, &version, &addr, NULL),
			-EINVAL);

	// CPM comm failed
	mock_ret_code = -EINVAL;
	KUNIT_EXPECT_EQ(test, msg_thermal_sm_get_section_addr(mock_section, &version, &addr, &size),
			-EIO);
	// CPM status error
	mock_ret_code = 0;
	mock_status_code = -EOPNOTSUPP;
	KUNIT_EXPECT_EQ(test, msg_thermal_sm_get_section_addr(mock_section, &version, &addr, &size),
			-EIO);
}

struct msg_stats_test_case {
	int (*func)(u8 tz_id);
	enum thermal_cpm_mbox_req_type valid_type;
	const char *msg;
};

static struct msg_stats_test_case msg_stats_test_case[] = {
	{
		.func = msg_stats_get_tr_stats,
		.valid_type = THERMAL_SERVICE_COMMAND_GET_TR_STATS,
		.msg = "get_tr_stats_test",
	},
	{
		.func = msg_stats_reset_tr_stats,
		.valid_type = THERMAL_SERVICE_COMMAND_RESET_TR_STATS,
		.msg = "reset_tr_stats_test",
	},
	{
		.func = msg_stats_get_tr_thresholds,
		.valid_type = THERMAL_SERVICE_COMMAND_GET_TR_THRESHOLDS,
		.msg = "get_tr_thresholds_test",
	},
	{
		.func = msg_stats_set_tr_thresholds,
		.valid_type = THERMAL_SERVICE_COMMAND_SET_TR_THRESHOLDS,
		.msg = "set_tr_thresholds_test",
	},
};

static void thermal_stats_msg_test(struct kunit *test)
{
	u8 mock_tzid = 1;

	for (int i = 0; i < ARRAY_SIZE(msg_stats_test_case); ++i) {
		struct msg_stats_test_case *test_case = &msg_stats_test_case[i];

		mock_ret_code = 0;
		KUNIT_EXPECT_EQ_MSG(test, test_case->func(mock_tzid), mock_ret_code,
				    test_case->msg);
		KUNIT_EXPECT_TRUE_MSG(test,
				      verify_req_msg(mock_tzid, test_case->valid_type),
				      test_case->msg);

		// CPM comm failed
		// ret error code indicates CPM comm errors and should block status code
		mock_ret_code = -EINVAL;
		KUNIT_EXPECT_EQ_MSG(test, test_case->func(mock_tzid), -EINVAL, test_case->msg);
		// CPM status error
		mock_ret_code = 0;
		mock_status_code = -EOPNOTSUPP;
		KUNIT_EXPECT_EQ_MSG(test, test_case->func(mock_tzid), -EOPNOTSUPP, test_case->msg);
		mock_status_code = 0;
	}
}

static struct kunit_case thermal_msg_helper_test[] = {
	KUNIT_CASE(thermal_cpm_send_req_with_status_check),
	KUNIT_CASE(thermal_cpm_gen_req),
	KUNIT_CASE(thermal_cpm_gen_req_with_val),
	KUNIT_CASE(thermal_cpm_gen_read),
	KUNIT_CASE(thermal_cpm_ch_enable),
	KUNIT_CASE(thermal_cpm_clear_data),
	KUNIT_CASE(thermal_cpm_clear_and_mask_irq),
	KUNIT_CASE(thermal_cpm_mask_fault_irq),
	KUNIT_CASE(thermal_cpm_set_trips),
	KUNIT_CASE(thermal_cpm_set_critical_trips),
	KUNIT_CASE(thermal_cpm_read_temp),
	KUNIT_CASE(thermal_cpm_read_avg_temp),
	KUNIT_CASE(thermal_cpm_read_irq_status),
	KUNIT_CASE(thermal_tmu_get_temp),
	KUNIT_CASE(thermal_tmu_set_trip),
	KUNIT_CASE(thermal_tmu_get_trip_counter_snapshot),
	KUNIT_CASE(thermal_tmu_set_gov_param),
	KUNIT_CASE(thermal_tmu_set_gov_select),
	KUNIT_CASE(thermal_tmu_set_power_table),
	KUNIT_CASE(thermal_tmu_get_power_table),
	KUNIT_CASE(thermal_tmu_set_polling_delay_ms),
	KUNIT_CASE(thermal_tmu_set_temp_lut),
	KUNIT_CASE(thermal_tmu_get_temp_lut),
	KUNIT_CASE(thermal_sm_get_section_addr),
	KUNIT_CASE(thermal_stats_msg_test),
	{},
};

static struct kunit_suite thermal_msg_helper_test_suite = {
	.name = "thermal_msg_helper_tests",
	.test_cases = thermal_msg_helper_test,
	.init = thermal_msg_helper_test_init,
};

kunit_test_suite(thermal_msg_helper_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
