// SPDX-License-Identifier: GPL-2.0
/*
 * thermal_cpm_mbox_tests.c Test suite to test all thermal cpm mbox functions.
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */

#include <linux/err.h>
#include <linux/thermal.h>
#include <kunit/test.h>
#include <kunit/test-bug.h>
#include <dt-bindings/soc/google/google-thermal-def.h>

#include "thermal_cpm_mbox_mock.h"
#include "thermal_cpm_mbox_helper.h"

static union thermal_cpm_message mock_req;
static union thermal_cpm_message mock_resp;
static int mock_parse_soc_data_ret;
static int mock_mbox_init_ret;
static int mock_parse_dt_ret;
static int mock_remote_ch;
static int mock_send_ret;
static u32 mock_rx_data[3];

int mock_cpm_send_request(struct cpm_iface_req *cpm_req)
{
	memcpy(cpm_req->req_msg->payload, mock_req.data, sizeof(mock_req));
	memcpy(cpm_req->resp_msg->payload, mock_resp.data, sizeof(mock_req));
	return mock_send_ret;
}

int mock_cpm_send_message(struct cpm_iface_req *cpm_req)
{
	memcpy(cpm_req->req_msg->payload, mock_req.data, sizeof(mock_req));
	return mock_send_ret;
}

int mock_thermal_cpm_mbox_parse_device_tree(struct thermal_cpm_mbox_driver_data *drv_data)
{
	drv_data->remote_ch = mock_remote_ch;
	return mock_parse_dt_ret;
}

int mock_thermal_cpm_mbox_init(void)
{
	return mock_mbox_init_ret;
}

int mock_thermal_cpm_mbox_parse_soc_data(void)
{
	return mock_parse_soc_data_ret;
}

static void thermal_cpm_mbox_probe_helper_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;

	// Parse soc data failed
	mock_parse_soc_data_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			thermal_cpm_mbox_probe_helper(mock_drv_data), -EINVAL);
	mock_parse_soc_data_ret = -ENODEV;
	KUNIT_EXPECT_EQ(test,
			thermal_cpm_mbox_probe_helper(mock_drv_data), -ENODEV);

	// Mbox init failed
	mock_parse_soc_data_ret = 0;
	mock_mbox_init_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			thermal_cpm_mbox_probe_helper(mock_drv_data), -EINVAL);
	mock_mbox_init_ret = -EPROBE_DEFER;
	KUNIT_EXPECT_EQ(test,
			thermal_cpm_mbox_probe_helper(mock_drv_data), -EPROBE_DEFER);

	// // Parse device tree failed
	mock_mbox_init_ret = 0;
	mock_parse_dt_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test,
			thermal_cpm_mbox_probe_helper(mock_drv_data), -EINVAL);

	// Check remote ch is correct
	mock_parse_dt_ret = 0;
	mock_remote_ch = 10;
	KUNIT_EXPECT_EQ(test,
			thermal_cpm_mbox_probe_helper(mock_drv_data), 0);
	KUNIT_EXPECT_EQ(test, mock_drv_data->remote_ch, mock_remote_ch);
}

static bool verify_mbox_resp(union thermal_cpm_message message)
{
	return message.data[0] == mock_resp.data[0] &&
		message.data[1] == mock_resp.data[1] &&
		message.data[2] == mock_resp.data[2];
}

static bool verify_cpm_rx_payload(struct cpm_iface_payload cpm_msg)
{
	return cpm_msg.payload[0] == mock_rx_data[0] &&
		cpm_msg.payload[1] == mock_rx_data[1] &&
		cpm_msg.payload[2] == mock_rx_data[2];
}

static void thermal_cpm_mbox_send_request_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;
	int status;

	// drv_data is NULL
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_req(NULL, &mock_req, &status), -ENODEV);
	// request message is NULL
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_req(mock_drv_data, NULL, &status), -EINVAL);
	// status is NULL
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_req(mock_drv_data, &mock_req, NULL), -EINVAL);

	// success case
	mock_send_ret = 0;
	mock_resp.data[0] = 0;
	mock_resp.data[1] = 1;
	mock_resp.data[2] = 2;
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_req(mock_drv_data, &mock_req, &status),
			mock_send_ret);
	KUNIT_EXPECT_TRUE(test, verify_mbox_resp(mock_req));

	// verify status in success case
	mock_send_ret = 0;
	mock_resp.resp.stat = 20;
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_req(mock_drv_data, &mock_req, &status),
			mock_send_ret);
	KUNIT_EXPECT_TRUE(test, mock_req.resp.stat == mock_resp.resp.stat);

	// Send request failed
	mock_send_ret = -ETIME;
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_req(mock_drv_data, &mock_req, &status),
			mock_send_ret);
	mock_send_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_req(mock_drv_data, &mock_req, &status),
			mock_send_ret);
}

static void thermal_cpm_mbox_send_message_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;

	// drv_data is NULL
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_msg(NULL, mock_req), -ENODEV);

	// success case
	mock_send_ret = 0;
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_msg(mock_drv_data, mock_req), mock_send_ret);

	// Send request failed
	mock_send_ret = -ETIME;
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_msg(mock_drv_data, mock_req), mock_send_ret);
	mock_send_ret = -EINVAL;
	KUNIT_EXPECT_EQ(test, __thermal_cpm_send_mbox_msg(mock_drv_data, mock_req), mock_send_ret);
}

static const struct tz_cdev_map test_tz_cdev_table[] = {
	[0] = {
		.sensor_id = TZ_BIG,
		.hw_tzid = HW_THERMAL_ZONE_0,
		.cdev_id = HW_CDEV_BIG,
		.tz_name = "BIG"
	},
	[1] = {
		.sensor_id = TZ_BIG_MID,
		.hw_tzid = HW_THERMAL_ZONE_1,
		.cdev_id = HW_CDEV_BIG_MID,
		.tz_name = "BIG_MID"
	},
	[2] = {
		.sensor_id = TZ_MID,
		.hw_tzid = HW_THERMAL_ZONE_2,
		.cdev_id = HW_CDEV_MID,
		.tz_name = "MID"
	},
	[3] = {
		.sensor_id = TZ_LIT,
		.hw_tzid = HW_THERMAL_ZONE_3,
		.cdev_id = HW_CDEV_LIT,
		.tz_name = "LITTLE"
	},
	[4] = {
		.sensor_id = TZ_GPU,
		.hw_tzid = HW_THERMAL_ZONE_4,
		.cdev_id = HW_CDEV_GPU,
		.tz_name = "GPU"
	},
	[5] = {
		.sensor_id = TZ_TPU,
		.hw_tzid = HW_THERMAL_ZONE_5,
		.cdev_id = HW_CDEV_TPU,
		.tz_name = "TPU"
	},
};


static const struct thermal_cpm_mbox_platform_data test_soc_data = {
	.tz_max = ARRAY_SIZE(test_tz_cdev_table),
	.tz_cdev_table = test_tz_cdev_table,
};

static void sensor_id_to_hw_tzid_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;
	enum hw_thermal_zone_id hw_tzid;
	int mock_sensor_id;

	// Invalid input
	mock_sensor_id = TZ_BIG;
	KUNIT_EXPECT_EQ(test,
			sensor_id_to_hw_tzid(mock_drv_data, mock_sensor_id, NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, sensor_id_to_hw_tzid(NULL, mock_sensor_id, &hw_tzid), -EINVAL);
	mock_sensor_id = -1;
	KUNIT_EXPECT_EQ(test,
			sensor_id_to_hw_tzid(mock_drv_data, mock_sensor_id, &hw_tzid),
			-EINVAL);
	mock_sensor_id = TZ_MAX;
	KUNIT_EXPECT_EQ(test,
			sensor_id_to_hw_tzid(mock_drv_data, mock_sensor_id, &hw_tzid),
			-EINVAL);

	// Valid
	for (int i = 0; i < test_soc_data.tz_max; ++i) {
		mock_sensor_id = test_soc_data.tz_cdev_table[i].sensor_id;
		KUNIT_EXPECT_EQ(test,
				sensor_id_to_hw_tzid(mock_drv_data, mock_sensor_id, &hw_tzid),
				0);
		KUNIT_EXPECT_EQ(test, hw_tzid, test_soc_data.tz_cdev_table[i].hw_tzid);
	}
}

static void hw_tzid_to_rx_cb_type_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;
	enum hw_thermal_zone_id mock_hw_tzid;
	enum hw_dev_type cdev_id_out;

	// Invalid input
	mock_hw_tzid = HW_THERMAL_ZONE_0;
	KUNIT_EXPECT_EQ(test,
			hw_tzid_to_rx_cb_type(mock_drv_data, mock_hw_tzid, NULL),
			-EINVAL);
	mock_hw_tzid = -1;
	KUNIT_EXPECT_EQ(test,
			hw_tzid_to_rx_cb_type(mock_drv_data, mock_hw_tzid, &cdev_id_out),
			-EINVAL);
	mock_hw_tzid = HW_THERMAL_ZONE_MAX;
	KUNIT_EXPECT_EQ(test,
			hw_tzid_to_rx_cb_type(mock_drv_data, mock_hw_tzid, &cdev_id_out),
			-EINVAL);

	// Valid
	for (int i = 0; i < test_soc_data.tz_max; ++i) {
		mock_hw_tzid = test_soc_data.tz_cdev_table[i].hw_tzid;
		KUNIT_EXPECT_EQ(test,
				hw_tzid_to_rx_cb_type(mock_drv_data, mock_hw_tzid, &cdev_id_out),
				0);
		KUNIT_EXPECT_EQ(test, cdev_id_out, test_soc_data.tz_cdev_table[i].cdev_id);
	}
}

static void hw_cdev_id_to_hw_tzid_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;
	enum hw_dev_type mock_cdev_id;
	enum hw_thermal_zone_id hw_tzid_out;

	// Invalid input
	mock_cdev_id = HW_CDEV_BIG;
	KUNIT_EXPECT_EQ(test,
			hw_cdev_id_to_hw_tzid(mock_drv_data, mock_cdev_id, NULL),
			-EINVAL);
	mock_cdev_id = -1;
	KUNIT_EXPECT_EQ(test,
			hw_cdev_id_to_hw_tzid(mock_drv_data, mock_cdev_id, &hw_tzid_out),
			-EINVAL);
	mock_cdev_id = HW_DEV_MAX;
	KUNIT_EXPECT_EQ(test,
			hw_cdev_id_to_hw_tzid(mock_drv_data, mock_cdev_id, &hw_tzid_out),
			-EINVAL);

	// Valid
	for (int i = 0; i < test_soc_data.tz_max; ++i) {
		mock_cdev_id = test_soc_data.tz_cdev_table[i].cdev_id;
		KUNIT_EXPECT_EQ(test,
				hw_cdev_id_to_hw_tzid(mock_drv_data, mock_cdev_id, &hw_tzid_out),
			0);
		KUNIT_EXPECT_EQ(test, hw_tzid_out, test_soc_data.tz_cdev_table[i].hw_tzid);
	}
}

static int thermal_cpm_mbox_rx_notifier_test(struct notifier_block *nb, unsigned long event,
					     void *data)
{
	memcpy(mock_rx_data, data, sizeof(mock_rx_data));

	return 0;
}

static struct notifier_block test_rx_notifier = {
	.notifier_call = thermal_cpm_mbox_rx_notifier_test,
};

static void thermal_cpm_mbox_rx_callback_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;
	struct cpm_iface_payload cpm_msg;
	struct thermal_cpm_mbox_request *req =
			(struct thermal_cpm_mbox_request *)cpm_msg.payload;

	// Init mock_drv_data
	KUNIT_EXPECT_EQ(test,
			thermal_cpm_mbox_probe_helper(mock_drv_data), 0);
	mock_drv_data->soc_data = kunit_kzalloc(test, sizeof(*mock_drv_data->soc_data), GFP_KERNEL);
	mock_drv_data->soc_data = &test_soc_data;
	// Register CDEV notifier
	KUNIT_EXPECT_EQ(test, __thermal_cpm_mbox_register_notification(mock_drv_data,
								       HW_CDEV_BIG,
								       &test_rx_notifier),
			0);

	// trigger cpm_mbox_rx_callback to check if notifier been called
	cpm_msg.payload[0] = 0;
	cpm_msg.payload[1] = 1;
	cpm_msg.payload[2] = 2;
	req->type = THERMAL_REQUEST_THROTTLE;
	req->tzid = HW_CDEV_BIG;
	cpm_mbox_rx_callback(0, &cpm_msg, mock_drv_data);

	flush_work(&mock_drv_data->rx_work[HW_CDEV_BIG].work);

	KUNIT_EXPECT_TRUE(test, verify_cpm_rx_payload(cpm_msg));
	__thermal_cpm_mbox_unregister_notification(mock_drv_data, HW_CDEV_BIG, &test_rx_notifier);

	// Check if it is throttle type.
	req->type = THERMAL_REQUEST_THROTTLE;
	req->tzid = HW_CDEV_BIG;
	KUNIT_EXPECT_TRUE(test, is_thermal_cpm_throttle_message(cpm_msg.payload));

	req->type = THERMAL_STATE_NOTIFICATION;
	KUNIT_EXPECT_FALSE(test, is_thermal_cpm_throttle_message(cpm_msg.payload));

	// Register NTC notifier
	KUNIT_EXPECT_EQ(test,
			__thermal_cpm_mbox_register_notification(mock_drv_data, HW_RX_CB_NTC,
								 &test_rx_notifier),
			0);

	// trigger cpm_mbox_rx_callback to check if notifier been called
	cpm_msg.payload[0] = 0;
	cpm_msg.payload[1] = 0;
	cpm_msg.payload[2] = 0;
	req->type = THERMAL_NTC_REQUEST;
	req->tzid = 30;
	req->req_rsvd0 = 0x2;
	cpm_mbox_rx_callback(0, &cpm_msg, mock_drv_data);

	flush_work(&mock_drv_data->rx_work[HW_RX_CB_NTC].work);
	cpm_msg.payload[0] = req->tzid;
	cpm_msg.payload[1] = req->req_rsvd0;
	cpm_msg.payload[2] = 0;

	KUNIT_EXPECT_TRUE(test, verify_cpm_rx_payload(cpm_msg));
	__thermal_cpm_mbox_unregister_notification(mock_drv_data, HW_RX_CB_NTC, &test_rx_notifier);

	// Register trip state notification
	KUNIT_EXPECT_EQ(test,
			__thermal_cpm_mbox_register_notification(mock_drv_data, HW_CDEV_BIG,
								 &test_rx_notifier),
			0);

	// trigger cpm_mbox_rx_callback to check if notifier been called
	cpm_msg.payload[0] = 0;
	cpm_msg.payload[1] = 0;
	cpm_msg.payload[2] = 0;
	req->type = THERMAL_STATE_NOTIFICATION;
	req->tzid = 0;
	req->req_rsvd0 = THERMAL_CPM_TRIP_DFS;
	req->req_rsvd1 = 1;
	req->req_rsvd2 = 100;
	req->req_rsvd3 = 1;
	cpm_mbox_rx_callback(0, &cpm_msg, mock_drv_data);

	flush_work(&mock_drv_data->rx_work[HW_CDEV_BIG].work);

	KUNIT_EXPECT_TRUE(test, verify_cpm_rx_payload(cpm_msg));
	__thermal_cpm_mbox_unregister_notification(mock_drv_data, HW_CDEV_BIG, &test_rx_notifier);
}

static void get_tz_name_test(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;

	// success test
	for (enum hw_thermal_zone_id i = 0; i < HW_THERMAL_ZONE_MAX; ++i) {
		if (!get_tz_name(mock_drv_data, i))
			continue;

		KUNIT_EXPECT_STREQ_MSG(test,
				       get_tz_name(mock_drv_data, i),
				       test_soc_data.tz_cdev_table[i].tz_name,
				       "tz name test: %d\n", i);
	}

	// invalid argument
	KUNIT_EXPECT_NULL(test, get_tz_name(mock_drv_data, HW_THERMAL_ZONE_MAX));
}

static int thermal_cpm_mbox_test_init(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data;

	mock_drv_data = kunit_kzalloc(test, sizeof(*mock_drv_data), GFP_KERNEL);
	mock_drv_data->dev = root_device_register("mock_thermal-cpm-mbox-device");
	mock_drv_data->soc_data = &test_soc_data;

	test->priv = mock_drv_data;

	return 0;
}

static void thermal_cpm_mbox_test_exit(struct kunit *test)
{
	struct thermal_cpm_mbox_driver_data *mock_drv_data = test->priv;

	root_device_unregister(mock_drv_data->dev);
}

static struct kunit_case thermal_cpm_mbox_test[] = {
	KUNIT_CASE(thermal_cpm_mbox_probe_helper_test),
	KUNIT_CASE(thermal_cpm_mbox_send_request_test),
	KUNIT_CASE(thermal_cpm_mbox_send_message_test),
	KUNIT_CASE(thermal_cpm_mbox_rx_callback_test),
	KUNIT_CASE(sensor_id_to_hw_tzid_test),
	KUNIT_CASE(hw_tzid_to_rx_cb_type_test),
	KUNIT_CASE(hw_cdev_id_to_hw_tzid_test),
	KUNIT_CASE(get_tz_name_test),
	{},
};

static struct kunit_suite thermal_cpm_mbox_test_suite = {
	.name = "thermal_cpm_mbox_tests",
	.test_cases = thermal_cpm_mbox_test,
	.init = thermal_cpm_mbox_test_init,
	.exit = thermal_cpm_mbox_test_exit,
};

kunit_test_suite(thermal_cpm_mbox_test_suite);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Henry Hsiao <pinhsinh@google.com>");
MODULE_AUTHOR("Jikai Ma <jikai@google.com>");
MODULE_AUTHOR("Ram Chandrasekar <rchandrasekar@google.com>");
MODULE_DESCRIPTION("Google LLC Thermal CPM Mbox Interface");
