/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _THERMAL_CPM_MBOX_H
#define _THERMAL_CPM_MBOX_H

#include <linux/device.h>
#include <linux/thermal.h>
#include <soc/google/goog_mba_cpm_iface.h>

#define CPM_SERVICE_ID_THERMAL 0x6F
#define MBA_CLIENT_TX_TIMEOUT_MSEC 3000

/*
 * CPM mbox status code.
 */
#define CPM_STATUS_NOT_READY (-3)
#define CPM_STATUS_OFFLINE (-16)
#define CPM_STATUS_FAILED_PRECONDITION (-17)
#define CPM_STATUS_BAD_STATE (-31)

/* HW thermal zone id, it must be algin with CPM side */
enum hw_thermal_zone_id {
	HW_THERMAL_ZONE_0 = 0x0,
	HW_THERMAL_ZONE_1,
	HW_THERMAL_ZONE_2,
	HW_THERMAL_ZONE_3,
	HW_THERMAL_ZONE_4,
	HW_THERMAL_ZONE_5,
	HW_THERMAL_ZONE_6,
	HW_THERMAL_ZONE_7,
	HW_THERMAL_ZONE_8,
	HW_THERMAL_ZONE_9,
	HW_THERMAL_ZONE_10,
	HW_THERMAL_ZONE_11,
	HW_THERMAL_ZONE_12,
	HW_THERMAL_ZONE_MAX,
};

/* HW device type: cdev_id and Rx callback id */
enum hw_dev_type {
	HW_CDEV_BIG = 0x0,
	HW_CDEV_BIG_MID,
	HW_CDEV_MID,
	HW_CDEV_LIT,
	HW_CDEV_GPU,
	HW_CDEV_TPU,
	HW_CDEV_AUR,
	HW_CDEV_ISP,
	HW_CDEV_MEM,
	HW_CDEV_AOC,
	HW_CDEV_MIDLL,
	HW_CDEV_CME,
	HW_CDEV_DSU,
	HW_CDEV_ISPFE,
	HW_CDEV_ISPBE,
	HW_CDEV_C3P,
	HW_CDEV_AOSS,
	HW_CDEV_MAX,
	/* Reserve for NTC Driver */
	HW_RX_CB_NTC = 0x50,
	HW_DEV_MAX,
};

/* Mailbox Request Types */
enum thermal_cpm_mbox_req_type {
	THERMAL_SERVICE_COMMAND_INIT = 0x0,
	THERMAL_SERVICE_COMMAND_GET_TEMP,
	THERMAL_SERVICE_COMMAND_EMUL,
	THERMAL_SERVICE_COMMAND_SET_TRIP_TEMP,
	THERMAL_SERVICE_COMMAND_SET_TRIP_HYST,
	THERMAL_SERVICE_COMMAND_SET_TRIP_TYPE,
	THERMAL_SERVICE_COMMAND_SET_INTERRUPT_ENABLE,
	THERMAL_SERVICE_COMMAND_TMU_CONTROL,
	THERMAL_SERVICE_COMMAND_NTC_ENABLE,
	THERMAL_SERVICE_COMMAND_NTC_READ,
	THERMAL_SERVICE_COMMAND_SET_PARAM,
	THERMAL_SERVICE_COMMAND_SET_GOV_SELECT,
	THERMAL_SERVICE_COMMAND_GET_SM,
	THERMAL_SERVICE_COMMAND_SET_POWERTABLE,
	THERMAL_SERVICE_COMMAND_GET_POWERTABLE,
	THERMAL_SERVICE_COMMAND_SET_POLLING_DELAY,
	THERMAL_SERVICE_COMMAND_NTC_READ_AVG,
	THERMAL_SERVICE_COMMAND_NTC_SET_TRIPS,
	THERMAL_SERVICE_COMMAND_NTC_SET_FAULT_TRIP,
	THERMAL_SERVICE_COMMAND_NTC_IRQ_STATUS,
	THERMAL_SERVICE_COMMAND_NTC_IRQ_CLEAR_AND_MASK,
	THERMAL_SERVICE_COMMAND_NTC_FAULT_IRQ_CLEAR_AND_MASK,
	THERMAL_SERVICE_COMMAND_NTC_CLEAR_DATA,
	THERMAL_SERVICE_COMMAND_GET_TR_STATS,
	THERMAL_SERVICE_COMMAND_RESET_TR_STATS,
	THERMAL_SERVICE_COMMAND_GET_TR_THRESHOLDS,
	THERMAL_SERVICE_COMMAND_SET_TR_THRESHOLDS,
	THERMAL_SERVICE_COMMAND_SET_TEMP_LUT,
	THERMAL_SERVICE_COMMAND_GET_TEMP_LUT,
	THERMAL_SERVICE_COMMAND_GET_TRIP_COUNTER_SNAPSHOT,
	THERMAL_SERVICE_COMMANDS_MAX,
};

enum thermal_mailbox_request_id {
	THERMAL_RESERVED = 0x0,
	THERMAL_REQUEST_THROTTLE,
	THERMAL_NTC_REQUEST,
	THERMAL_STATE_NOTIFICATION,
	NUM_THERMAL_MAILBOX_REQUESTS,
};

enum thermal_cpm_trip_id {
	THERMAL_CPM_TRIP_COLD_COMP,
	THERMAL_CPM_TRIP_SWITCH_ON,
	THERMAL_CPM_TRIP_EARLY_THROTTLE,
	THERMAL_CPM_TRIP_CONTROL,
	THERMAL_CPM_TRIP_ALERT,
	THERMAL_CPM_TRIP_SW_MAX,
	THERMAL_CPM_TRIP_DFS,
	THERMAL_CPM_TRIP_RESERVED,
	THERMAL_CPM_TRIP_MAX
};

/*
 * 12-byte Mailbox message format (REQ, MSG)
 *  (MSB)    3          2          1          0
 * ---------------------------------------------
 * |          | tzid     |          | type     |
 * ---------------------------------------------
 * |          |          |          |          |
 * ---------------------------------------------
 * |          |          |          |          |
 * ---------------------------------------------
 */
struct thermal_cpm_mbox_request {
	u8 type;
	u8 rsvd;
	u8 tzid;
	u8 rsvd2;
	u8 req_rsvd0;
	u8 req_rsvd1;
	u8 req_rsvd2;
	u8 req_rsvd3;
	u8 req_rsvd4;
	u8 req_rsvd5;
	u8 req_rsvd6;
	u8 req_rsvd7;
};

struct thermal_cpm_ntc_request {
	u8 type;
	u8 rsvd;
	u8 chid;
	u8 rsvd2;
	u16 req_rsvd0;
	u16 req_rsvd1;
	u16 req_rsvd2;
	u16 req_rsvd3;
};

/*
 * 12-byte Mailbox message format (RESP)
 *  (MSB)    3          2          1          0
 * ---------------------------------------------
 * | stat     |  tz_id   | ret      | type     |
 * ---------------------------------------------
 * |          |          |        temp         |
 * ---------------------------------------------
 * |          |          |          |          |
 * ---------------------------------------------
 */
struct thermal_cpm_mbox_response {
	u8 type;
	s8 ret;
	u8 tzid;
	s8 stat;
	u16 temp;
	u16 status;
	u16 status1;
	u8 rsvd0;
	u8 rsvd1;
};

union thermal_cpm_message {
	u32 data[GOOG_MBA_PAYLOAD_SIZE];
	struct thermal_cpm_mbox_response resp;
	struct thermal_cpm_mbox_request req;
	struct thermal_cpm_ntc_request ntc_req;
};

struct tz_cdev_map {
	unsigned int sensor_id;
	enum hw_thermal_zone_id hw_tzid;
	enum hw_dev_type cdev_id;
	char tz_name[THERMAL_NAME_LENGTH];
};

struct thermal_cpm_mbox_platform_data {
	int tz_max;
	const struct tz_cdev_map *tz_cdev_table;
};

struct thermal_cpm_mbox_rx_work {
	struct blocking_notifier_head notifier;
	struct work_struct work;
	/* spinlock for protecting data when rx callback */
	spinlock_t rx_lock;
	u32 data[GOOG_MBA_PAYLOAD_SIZE];
};

struct thermal_cpm_mbox_driver_data {
	struct device *dev;
	struct cpm_iface_client *client;
	u32 remote_ch;
	const struct thermal_cpm_mbox_platform_data *soc_data;
	struct thermal_cpm_mbox_rx_work rx_work[HW_DEV_MAX];
	/* TODO: move away from static array to dynamic list */
};


static inline bool is_thermal_cpm_throttle_message(void *data)
{
	struct thermal_cpm_mbox_response *resp = (struct thermal_cpm_mbox_response *)data;

	if (!resp)
		return false;

	return resp->type == THERMAL_REQUEST_THROTTLE;
}

/*
 * thermal_cpm_send_mbox_req - send a thermal request to CPM host.
 * @message: Thermal request structure contains 3 bytes data, it will be overright
 *	     after receiving CPM response.
 *
 * @status: Return error from the CPM response.
 *
 * Return: Mailbox error, 0 on success.
 *
 * This function will wait until cpm response (blocking API). The message will be handle
 * by mailbox driver.
 */
int thermal_cpm_send_mbox_req(union thermal_cpm_message *message, int *status);
/*
 * thermal_cpm_send_mbox_msg - send a thermal message to CPM host.
 *
 * @message: Thermal message structure contains 3 byte data.
 *
 * Return: Mailbox error, 0 on success.
 *
 * This function will send a message to cpm, and won't block from cpm response.
 */
int thermal_cpm_send_mbox_msg(union thermal_cpm_message message);
/*
 * thermal_cpm_mbox_register_notification - registering a callback when
 *					    receiving cpm rx callback.
 *
 * @type: Rx callback type.
 * @nb: Notifier block trigger when receiving cpm rx callback.
 *
 * Return: Register error, 0 on success.
 *
 * This function provide interface for register a callback when receiving cpm rx callback.
 */
int thermal_cpm_mbox_register_notification(enum hw_dev_type type,
					   struct notifier_block *nb);
/* thermal_cpm_mbox_unregister_notification - unregister for cpm rx callback.
 *
 * @type: Rx callback type.
 * @nb: Notifier block to be remove.
 *
 * This function provide interface for unregister cpm rx callback.
 */
void thermal_cpm_mbox_unregister_notification(enum hw_dev_type type,
					      struct notifier_block *nb);

/* thermal_cpm_mbox_cdev_to_hw_tzid - find the mapped hw tz_id of a cdev_id
 *
 * @cdev_id: the cdev_id to look up
 * @hw_tzid: output of HW tz_id
 *
 * Return: 0 on success
 * 	-EINVAL: invalid cdev_id or no mapped tz_id found
 */
int thermal_cpm_mbox_cdev_to_hw_tzid(enum hw_dev_type cdev_id, enum hw_thermal_zone_id *hw_tzid);

/*
 * thermal_cpm_mbox_get_tz_name(enum hw_thermal_zone_id hw_tzid)
 *
 * @ hw_tzid: HW TZ id
 *
 * Return: thermal zone name
 */
const char *thermal_cpm_mbox_get_tz_name(enum hw_thermal_zone_id hw_tzid);

/* thermal_cpm_mbox_sensor_id_to_hw_tzid - map thermal sensor id to HW TZ id
 *
 * @ sensor_id: thermal sensor id
 * @ hw_tzid: mapped HW TZ id
 *
 * Return: 0 on success.
 *	-EINVAL: no mapped TZ id found in CPM
 */
int thermal_cpm_mbox_sensor_id_to_hw_tzid(int sensor_id, enum hw_thermal_zone_id *hw_tzid);

#endif /* _THERMAL_CPM_MBOX_H */
