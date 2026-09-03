/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _CPM_THERMAL_H
#define _CPM_THERMAL_H

#include <linux/mailbox_client.h>
#include <linux/thermal.h>
#include <linux/spinlock.h>

#include <soc/google/goog_mba_cpm_iface.h>
#define CPM_SERVICE_ID_THERMAL	0x6F

#define MBA_CLIENT_TX_TIMEOUT 3000

#define CPM_THERMAL_PRESS_POLLING_DELAY_ON 100
#define CPM_THERMAL_PRESS_POLLING_DELAY_OFF 0

/* Mailbox Request Types */
enum thermal_mailbox_type {
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
	NUM_THERMAL_SERVICE_COMMANDS,
};

enum thermal_param_type {
	THERMAL_PARAM_K_PO,
	THERMAL_PARAM_K_PU,
	THERMAL_PARAM_K_I,
	THERMAL_PARAM_I_MAX,
	THERMAL_PARAM_EARLY_THROTTLE_K_P,
	THERMAL_PARAM_IRQ_GAIN,
	THERMAL_PARAM_TIMER_GAIN,
	NUM_THERMAL_PARAM_TYPE,
};

enum thermal_mailbox_rx_type {
	/* Thermal ping callback */
	THERMAL_RX_PING = 0x0,
	/* Thermal throttle callback */
	THERMAL_RX_THROTTLE,
	NUM_THERMAL_RX,
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
struct cpm_thermal_request {
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

struct cpm_ntc_thermal_request {
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
 * |        status       |        temp         |
 * ---------------------------------------------
 * |          |          |        status1      |
 * ---------------------------------------------
 */
struct cpm_thermal_response {
	u8 type;
	s8 ret;
	u8 tzid;
	u8 stat;
	u16 temp;
	u16 status;
	u16 status1;
	u8 rsvd0;
	u8 rsvd1;
};

union thermal_message {
	u32 data[3];
	struct cpm_thermal_response resp;
	struct cpm_thermal_request req;
	struct cpm_ntc_thermal_request ntc_req;
};

struct thermal_zone_node {
	struct list_head list;
	struct thermal_zone_device *tz;
};

struct thermal_throttle_work {
	union thermal_message thermal_msg;
	struct work_struct work;
};

enum thermal_section {
	THERMAL_CURR_STATE = 0,
	THERMAL_SECTION_NUM,
};

struct google_cpm_thermal {
	struct device *dev;
	struct cpm_iface_client *client;

	u32 remote_ch;

	struct cpm_thermal_ops *ops;

	/* lock for protect thermal_zone_list */
	struct mutex thermal_zone_list_lock;
	struct list_head thermal_zone_list;

	/* spinlock for protecting thermal_message when doing throttling */
	spinlock_t throttle_lock;
	struct thermal_throttle_work throttle_work;
};

struct cpm_thermal_ops {
	int (*register_thermal_zone)(struct google_cpm_thermal *cpm_thermal,
				     struct thermal_zone_device *tz);
	int (*unregister_thermal_zone)(struct google_cpm_thermal *cpm_thermal,
				       struct thermal_zone_device *tz);
	int (*init)(struct google_cpm_thermal *cpm_thermal);
	int (*get_temp)(struct google_cpm_thermal *cpm_thermal, u8 tz);
	int (*set_trip_temp)(struct google_cpm_thermal *cpm_thermal, u8 tz, s8 *temperature);
	int (*set_trip_hyst)(struct google_cpm_thermal *cpm_thermal, u8 tz, u8 *hysteresis);
	int (*set_trip_type)(struct google_cpm_thermal *cpm_thermal, u8 tz,
			     enum thermal_trip_type *type);
	int (*set_interrupt_enable)(struct google_cpm_thermal *cpm_thermal, u8 tz, u16 inten);
	int (*tmu_control)(struct google_cpm_thermal *cpm_thermal, u8 tz, bool control);
	int (*set_param)(struct google_cpm_thermal *cpm_thermal, u8 tz, u8 type, int val);
	int (*set_gov_select)(struct google_cpm_thermal *cpm_thermal, u8 tz, u32 gov_select);
	int (*get_sm_addr)(struct google_cpm_thermal *cpm_thermal, u8 tz,
			   enum thermal_section section, u8 *version, int *addr,
			   u32 *size);
	int (*set_powertable)(struct google_cpm_thermal *cpm_thermal, u8 tz, u8 idx, int val);
	int (*get_powertable)(struct google_cpm_thermal *cpm_thermal, u8 tz, u8 idx, int *val);
	int (*set_polling_delay)(struct google_cpm_thermal *cpm_thermal, u8 tz, u16 delay);
};

int cpm_thermal_send_mbox_request(union thermal_message *message);

#endif /* _CPM_THERMAL_H */
