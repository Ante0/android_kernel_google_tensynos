/* SPDX-License-Identifier: GPL */
/*
 * Google Touch Interface for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#ifndef _GOOG_TOUCH_INTERFACE_
#define _GOOG_TOUCH_INTERFACE_

#include <drm/drm_panel.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <linux/device.h>
#include <linux/kfifo.h>
#include <linux/pm_qos.h>

#include "gti_debug.h"
#include "gti_log.h"
#include "gti_pm.h"
#include "gti_status_event_dev.h"
#include "heatmap.h"
#include "touch_offload.h"
#include "uapi/input/touch_offload.h"

#define GTI_NAME "goog_touch_interface"
#define MAX_SLOTS 10

/* Resample latency */
#define RESAMPLE_LATENCY_DEFAULT (5 * NSEC_PER_MSEC)

/* Buffer size for GTI driver */
#define GTI_BUFFER_SIZE 4096

/*-----------------------------------------------------------------------------
 * enums.
 */

#ifndef __CODESONAR__
#define ENUM_U32 : u32
#define ENUM_U8 : u8
#else
#define ENUM_U32
#define ENUM_U8
#endif

enum gti_cmd_type ENUM_U32 {
	/* GTI_CMD operations. */
	GTI_CMD_OPS_START = 0x100,
	GTI_CMD_CALIBRATE,
	GTI_CMD_PING,
	GTI_CMD_RESET,
	GTI_CMD_SELFTEST,

	/* GTI_CMD_GET operations. */
	GTI_CMD_GET_OPS_START = 0x200,
	GTI_CMD_GET_CONTEXT_DRIVER,
	GTI_CMD_GET_CONTEXT_STYLUS,
	GTI_CMD_GET_COORD_FILTER_ENABLED,
	GTI_CMD_GET_FW_VERSION,
	GTI_CMD_GET_GRIP_MODE,
	GTI_CMD_GET_INT2_MODE,
	GTI_CMD_GET_INT2_STATUS,
	GTI_CMD_GET_IRQ_MODE,
	GTI_CMD_GET_PALM_MODE,
	GTI_CMD_GET_PANEL_ID,
	GTI_CMD_GET_REPORT_RATE,
	GTI_CMD_GET_SCAN_MODE,
	GTI_CMD_GET_SCREEN_PROTECTOR_MODE,
	GTI_CMD_GET_SENSING_MODE,
	GTI_CMD_GET_SENSOR_DATA,
	GTI_CMD_GET_SENSOR_DATA_MANUAL,
	GTI_CMD_GET_VENDOR_REGISTER,
	GTI_CMD_GET_TOUCH_VSYNC_HSYNC_FREQ,
	GTI_CMD_GET_WATER_MODE,

	/* GTI_CMD_NOTIFY operations. */
	GTI_CMD_NOTIFY_OPS_START = 0x300,
	GTI_CMD_NOTIFY_DISPLAY_STATE,
	GTI_CMD_NOTIFY_DISPLAY_VREFRESH,

	/* GTI_CMD_SET operations. */
	GTI_CMD_SET_OPS_START = 0x400,
	GTI_CMD_SET_CONTINUOUS_REPORT,
	GTI_CMD_SET_COORD_FILTER_ENABLED,
	GTI_CMD_SET_GESTURE_CONFIG,
	GTI_CMD_SET_GRIP_MODE,
	GTI_CMD_SET_HEATMAP_ENABLED,
	GTI_CMD_SET_INT2_MODE,
	GTI_CMD_SET_IRQ_MODE,
	GTI_CMD_SET_PALM_MODE,
	GTI_CMD_SET_PANEL_SPEED_MODE,
	GTI_CMD_SET_REPORT_RATE,
	GTI_CMD_SET_SCAN_MODE,
	GTI_CMD_SET_SCREEN_PROTECTOR_MODE,
	GTI_CMD_SET_SENSING_MODE,
	GTI_CMD_SET_WATER_MODE,
};

enum gti_calibrate_result ENUM_U32 {
	GTI_CALIBRATE_RESULT_DONE = 0,
	GTI_CALIBRATE_RESULT_SHELL_CMDS_REDIRECT,
	GTI_CALIBRATE_RESULT_FAIL,
	GTI_CALIBRATE_RESULT_NA = 0xFFFFFFFF,
};

enum gti_continuous_report_setting ENUM_U32 {
	GTI_CONTINUOUS_REPORT_DISABLE = 0,
	GTI_CONTINUOUS_REPORT_ENABLE,
	GTI_CONTINUOUS_REPORT_DRIVER_DEFAULT,
};

enum gti_coord_filter_setting ENUM_U32 {
	GTI_COORD_FILTER_DISABLE = 0,
	GTI_COORD_FILTER_ENABLE,
};

enum gti_display_state_setting ENUM_U32 {
	GTI_DISPLAY_STATE_OFF = 0,
	GTI_DISPLAY_STATE_ON,
};

enum gti_grip_setting ENUM_U32 {
	GTI_GRIP_DISABLE = 0,
	GTI_GRIP_ENABLE,
};

enum gti_heatmap_setting ENUM_U32 {
	GTI_HEATMAP_DISABLE = 0,
	GTI_HEATMAP_ENABLE,
};

enum gti_int2_mode ENUM_U32 {
	GTI_INT2_MODE_KEEP_LOW = 0,
	GTI_INT2_MODE_KEEP_HIGH,
	GTI_INT2_MODE_AUTO,
	GTI_INT2_MODE_NA = 0xFFFFFFFFu,
};

enum gti_int2_status ENUM_U32 {
	GTI_INT2_STATUS_LOW = 0,
	GTI_INT2_STATUS_HIGH,
};

enum gti_irq_mode ENUM_U32 {
	GTI_IRQ_MODE_DISABLE = 0,
	GTI_IRQ_MODE_ENABLE,
	GTI_IRQ_MODE_NA = 0xFFFFFFFFu,
};

/**
 * Motion filter mode.
 *   GTI_MF_MODE_UNFILTER: enable unfilter by continuous reporting.
 *   GTI_MF_MODE_FILTER: only report touch if coord report changed.
 */
enum gti_mf_mode ENUM_U32 {
	GTI_MF_MODE_UNFILTER = 0,
	GTI_MF_MODE_FILTER = 1,
};

enum gti_palm_setting ENUM_U32 {
	GTI_PALM_DISABLE = 0,
	GTI_PALM_ENABLE,
};

enum gti_panel_speed_mode_setting ENUM_U32 {
	GTI_PANEL_SPEED_MODE_NS = 0,
	GTI_PANEL_SPEED_MODE_HS,
};

enum gti_ping_mode ENUM_U32 {
	GTI_PING_NOP = 0,
	GTI_PING_ENABLE,
	GTI_PING_NA = 0xFFFFFFFFu,
};

enum gti_proc_type ENUM_U32 {
	GTI_PROC_DUMP,
	GTI_PROC_MS_BASE,
	GTI_PROC_MS_DIFF,
	GTI_PROC_MS_RAW,
	GTI_PROC_SS_BASE,
	GTI_PROC_SS_DIFF,
	GTI_PROC_SS_RAW,
	GTI_PROC_NUM,
};

enum gti_reset_mode ENUM_U32 {
	GTI_RESET_MODE_NOP = 0,
	GTI_RESET_MODE_SW = (1 << 0),
	GTI_RESET_MODE_HW = (1 << 1),
	GTI_RESET_MODE_AUTO = GTI_RESET_MODE_HW | GTI_RESET_MODE_SW,
	GTI_RESET_MODE_NA = 0xFFFFFFFFu,
};

enum gti_scan_mode ENUM_U32 {
	GTI_SCAN_MODE_AUTO = 0,
	GTI_SCAN_MODE_NORMAL_ACTIVE,
	GTI_SCAN_MODE_NORMAL_IDLE,
	GTI_SCAN_MODE_LP_ACTIVE,
	GTI_SCAN_MODE_LP_IDLE,
	GTI_SCAN_MODE_NA = 0xFFFFFFFFu,
};

enum gti_screen_protector_mode ENUM_U32 {
	GTI_SCREEN_PROTECTOR_MODE_DISABLE = 0,
	GTI_SCREEN_PROTECTOR_MODE_ENABLE,
	GTI_SCREEN_PROTECTOR_MODE_NA = 0xFFFFFFFFu,
};

enum gti_selftest_result ENUM_U32 {
	GTI_SELFTEST_RESULT_DONE = 0,
	GTI_SELFTEST_RESULT_PASS = GTI_SELFTEST_RESULT_DONE,
	GTI_SELFTEST_RESULT_SHELL_CMDS_REDIRECT,
	GTI_SELFTEST_RESULT_FAIL = 0x80000000,
	GTI_SELFTEST_RESULT_NA = 0xFFFFFFFFu,
};

enum gti_sensing_mode ENUM_U32 {
	GTI_SENSING_MODE_DISABLE = 0,
	GTI_SENSING_MODE_ENABLE,
	GTI_SENSING_MODE_NA = 0xFFFFFFFFu,
};

/* Touch read method for automatically reading data from interrupt */
#define TOUCH_SENSOR_DATA_READ_METHOD_INT 0x10000
/* Touch read method for manually reading data from command */
#define TOUCH_SENSOR_DATA_READ_METHOD_COMMAND 0x20000

enum gti_sensor_data_type ENUM_U32 {
	GTI_SENSOR_DATA_TYPE_COORD = TOUCH_DATA_TYPE_COORD,
	GTI_SENSOR_DATA_TYPE_MS = TOUCH_SENSOR_DATA_READ_METHOD_INT |
			TOUCH_SCAN_TYPE_MUTUAL | TOUCH_DATA_TYPE_STRENGTH,
	GTI_SENSOR_DATA_TYPE_MS_DIFF = TOUCH_SENSOR_DATA_READ_METHOD_COMMAND |
			TOUCH_SCAN_TYPE_MUTUAL | TOUCH_DATA_TYPE_STRENGTH,
	GTI_SENSOR_DATA_TYPE_MS_RAW = TOUCH_SENSOR_DATA_READ_METHOD_COMMAND |
			TOUCH_SCAN_TYPE_MUTUAL | TOUCH_DATA_TYPE_RAW,
	GTI_SENSOR_DATA_TYPE_MS_BASELINE = TOUCH_SENSOR_DATA_READ_METHOD_COMMAND |
			TOUCH_SCAN_TYPE_MUTUAL | TOUCH_DATA_TYPE_BASELINE,
	GTI_SENSOR_DATA_TYPE_SS = TOUCH_SENSOR_DATA_READ_METHOD_INT |
			TOUCH_SCAN_TYPE_SELF | TOUCH_DATA_TYPE_STRENGTH,
	GTI_SENSOR_DATA_TYPE_SS_DIFF = TOUCH_SENSOR_DATA_READ_METHOD_COMMAND |
			TOUCH_SCAN_TYPE_SELF | TOUCH_DATA_TYPE_STRENGTH,
	GTI_SENSOR_DATA_TYPE_SS_RAW = TOUCH_SENSOR_DATA_READ_METHOD_COMMAND |
			TOUCH_SCAN_TYPE_SELF | TOUCH_DATA_TYPE_RAW,
	GTI_SENSOR_DATA_TYPE_SS_BASELINE = TOUCH_SENSOR_DATA_READ_METHOD_COMMAND |
			TOUCH_SCAN_TYPE_SELF | TOUCH_DATA_TYPE_BASELINE,
};

// clang-format off
enum gti_fw_status ENUM_U32 {
	GTI_FW_STATUS_RESET = 0,
	GTI_FW_STATUS_PALM_ENTER,
	GTI_FW_STATUS_PALM_EXIT,
	GTI_FW_STATUS_GRIP_ENTER,
	GTI_FW_STATUS_GRIP_EXIT,
	GTI_FW_STATUS_WATER_ENTER,
	GTI_FW_STATUS_WATER_EXIT,
	GTI_FW_STATUS_NOISE_MODE,
	GTI_FW_STATUS_GESTURE_EVENT,
	GTI_FW_STATUS_INVALID_GESTURE_EVENT,
};
// clang-format on

enum gti_gesture_params ENUM_U8 {
	GTI_STTW_MIN_X = 0,
	GTI_STTW_MAX_X,
	GTI_STTW_MIN_Y,
	GTI_STTW_MAX_Y,
	GTI_STTW_MIN_FRAME,
	GTI_STTW_MAX_FRAME,
	GTI_STTW_JITTER,
	GTI_STTW_MAX_TOUCH_SIZE,

	GTI_LPTW_MIN_X,
	GTI_LPTW_MAX_X,
	GTI_LPTW_MIN_Y,
	GTI_LPTW_MAX_Y,
	GTI_LPTW_MIN_FRAME,
	GTI_LPTW_JITTER,
	GTI_LPTW_MAX_TOUCH_SIZE,
	GTI_LPTW_MARGINAL_MIN_X,
	GTI_LPTW_MARGINAL_MAX_X,
	GTI_LPTW_MARGINAL_MIN_Y,
	GTI_LPTW_MARGINAL_MAX_Y,
	GTI_LPTW_MONITOR_CH_MIN_TX,
	GTI_LPTW_MONITOR_CH_MAX_TX,
	GTI_LPTW_MONITOR_CH_MIN_RX,
	GTI_LPTW_MONITOR_CH_MAX_RX,
	GTI_LPTW_NODE_COUNT_MIN,
	GTI_LPTW_MOTION_BOUNDARY,
	GTI_LPTW_INT2_ASSERT_MIN_X,
	GTI_LPTW_INT2_ASSERT_MAX_X,
	GTI_LPTW_INT2_ASSERT_MIN_Y,
	GTI_LPTW_INT2_ASSERT_MAX_Y,
	GTI_LPTW_INT2_DEASSERT_MIN_X,
	GTI_LPTW_INT2_DEASSERT_MAX_X,
	GTI_LPTW_INT2_DEASSERT_MIN_Y,
	GTI_LPTW_INT2_DEASSERT_MAX_Y,

	GTI_GESTURE_TYPE,

	GTI_GESTURE_PARAMS_MAX,
};

enum gti_gesture_type ENUM_U8 {
	GTI_GESTURE_DISABLE = 0,
	GTI_GESTURE_STTW,
	GTI_GESTURE_LPTW,
	GTI_GESTURE_STTW_AND_LPTW,
	GTI_GESTURE_TYPE_MAX,
};

enum gti_invalid_gesture_type ENUM_U8 {
	GTI_STTW_INVALID_GESTURE_OUT_OF_RANGE = 0x01,
	GTI_STTW_INVALID_GESTURE_SHIFT,
	GTI_STTW_INVALID_GESTURE_PALM,
	GTI_STTW_INVALID_GESTURE_MULTI_TOUCH,
	GTI_STTW_INVALID_GESTURE_LONG_PRESS,
	GTI_STTW_INVALID_GESTURE_RAPID_TOUCH,
	GTI_LPTW_INVALID_GESTURE_OUT_OF_RANGE = 0x11,
	GTI_LPTW_INVALID_GESTURE_SHIFT,
	GTI_LPTW_INVALID_GESTURE_TOO_SMALL,
	GTI_LPTW_INVALID_GESTURE_PALM,
	GTI_LPTW_INVALID_GESTURE_RAPID_TOUCH,
};

enum gti_noise_mode_level ENUM_U8 {
	GTI_NOISE_MODE_EXIT = 0,
	GTI_NOISE_MODE_LEVEL1,
	GTI_NOISE_MODE_LEVEL2,
	GTI_NOISE_MODE_LEVEL3,
};

enum gti_ical_res ENUM_U32 {
	ICAL_RES_SUCCESS = 0,
	ICAL_RES_FAIL = 0x80000000,
	ICAL_RES_FAIL_INVALID_BUS_ACCESS = 0x80000001,
	ICAL_RES_NA = 0xFFFFFFFFu,
};

enum gti_ical_state ENUM_U32 {
	ICAL_STATE_IDLE = 0,
	ICAL_STATE_INIT_CAL = 101,
	ICAL_STATE_RUN_CAL = 102,
	ICAL_STATE_END_CAL = 103,
	ICAL_STATE_INIT_TEST = 201,
	ICAL_STATE_RUN_TEST = 202,
	ICAL_STATE_END_TEST = 203,
	ICAL_STATE_INIT_RESET = 301,
	ICAL_STATE_RUN_RESET = 302,
	ICAL_STATE_END_RESET = 303,
	ICAL_STATE_NA = 0xFFFFFFFFu,
};

enum gti_water_setting ENUM_U32 {
	GTI_WATER_DISABLE = 0,
	GTI_WATER_ENABLE,
};

#undef ENUM_U32
#undef ENUM_U8

/*-----------------------------------------------------------------------------
 * const char.
 */

const static char *gesture_params_list[GTI_GESTURE_PARAMS_MAX] = {
	"sttw_min_x",
	"sttw_max_x",
	"sttw_min_y",
	"sttw_max_y",
	"sttw_min_frame",
	"sttw_max_frame",
	"sttw_jitter",
	"sttw_max_touch_size",
	"lptw_min_x",
	"lptw_max_x",
	"lptw_min_y",
	"lptw_max_y",
	"lptw_min_frame",
	"lptw_jitter",
	"lptw_max_touch_size",
	"lptw_marginal_min_x",
	"lptw_marginal_max_x",
	"lptw_marginal_min_y",
	"lptw_marginal_max_y",
	"lptw_monitor_ch_min_tx",
	"lptw_monitor_ch_max_tx",
	"lptw_monitor_ch_min_rx",
	"lptw_monitor_ch_max_rx",
	"lptw_node_count_min",
	"lptw_motion_boundary",
	"lptw_int2_assert_min_x",
	"lptw_int2_assert_max_x",
	"lptw_int2_assert_min_y",
	"lptw_int2_assert_max_y",
	"lptw_int2_deassert_min_x",
	"lptw_int2_deassert_max_x",
	"lptw_int2_deassert_min_y",
	"lptw_int2_deassert_max_y",
	"gesture_type",
};

/*-----------------------------------------------------------------------------
 * Structures.
 */

struct touch_sim;

struct gti_calibrate_cmd {
	enum gti_calibrate_result result;
	char buffer[GTI_BUFFER_SIZE];
};

struct gti_context_changed {
	union {
		struct {
		u32 screen_state : 1;
		u32 display_refresh_rate : 1;
		u32 touch_report_rate : 1;
		u32 noise_state : 1;
		u32 water_mode : 1;
		u32 charger_state : 1;
		u32 hinge_angle : 1;
		u32 offload_timestamp : 1;
		};
		u32 value;
	};
};

struct gti_context_driver_cmd {
	struct gti_context_changed context_changed;

	u8 screen_state;
	u8 display_refresh_rate;
	u8 touch_report_rate;
	u8 noise_state;
	u8 water_mode;
	u8 charger_state;
	s16 hinge_angle;

	ktime_t offload_timestamp;
};

struct gti_context_stylus_cmd {
	struct {
		u32 coords : 1;
		u32 coords_timestamp : 1;
		u32 pen_paired : 1;
		u32 pen_active : 1;
	} contents;
	struct TouchOffloadCoord pen_offload_coord;
	ktime_t pen_offload_coord_timestamp;
	u8 pen_paired;
	u8 pen_active;
};

struct gti_vendor_register_cmd {
	u8 data[TOUCH_OFFLOAD_VENDOR_REGISTER_DATA_SIZE];
	u32 size;
};

struct gti_continuous_report_cmd {
	enum gti_continuous_report_setting setting;
};

struct gti_coord_filter_cmd {
	enum gti_coord_filter_setting setting;
};

struct gti_display_state_cmd {
	enum gti_display_state_setting setting;
};

struct gti_display_vrefresh_cmd {
	u32 setting;
};

struct gti_fw_version_cmd {
	char buffer[0x200];
};

struct gti_gesture_config_cmd {
	u8 updating_params[GTI_GESTURE_PARAMS_MAX];
	u16 params[GTI_GESTURE_PARAMS_MAX];
};

struct gti_grip_cmd {
	enum gti_grip_setting setting;
};

struct gti_heatmap_cmd {
	enum gti_heatmap_setting setting;
};

struct gti_int2_cmd {
	enum gti_int2_mode setting;
};

struct gti_int2_status_cmd {
	enum gti_int2_status setting;
};

struct gti_irq_cmd {
	enum gti_irq_mode setting;
};

struct gti_palm_cmd {
	enum gti_palm_setting setting;
};

struct gti_panel_id_cmd {
	int setting;
};

struct gti_panel_speed_mode_cmd {
	enum gti_panel_speed_mode_setting setting;
};

struct gti_ping_cmd {
	enum gti_ping_mode setting;
};

struct gti_report_rate_cmd {
	u32 setting;
};

struct gti_reset_cmd {
	enum gti_reset_mode setting;
};

struct gti_scan_cmd {
	enum gti_scan_mode setting;
};

struct gti_screen_protector_mode_cmd {
	enum gti_screen_protector_mode setting;
};

struct gti_selftest_cmd {
	enum gti_selftest_result result;
	char buffer[GTI_BUFFER_SIZE];
	bool is_ical;
};

struct gti_sensing_cmd {
	enum gti_sensing_mode setting;
};

struct gti_sensor_data_cmd {
	enum gti_sensor_data_type type;
	u8 *buffer;
	u32 size;
	/* Set by vendor driver and the default value is false */
	bool is_unsigned;
};

struct gti_vsync_hsync_frequency_cmd {
	u16 vsync_value;
	u16 hsync_value;
};

struct gti_water_cmd {
	enum gti_water_setting setting;
};

/**
 * struct gti_union_cmd_data - GTI commands to vendor driver.
 * @calibrate_cmd: command to calibrate the touchscreen
 * @context_driver_cmd: command to update touch offload driver context.
 * @context_stylus_cmd: command to update touch offload stylus context.
 * @continuous_report_cmd: command to set continuous reporting.
 * @coord_filter_cmd: command to set/get coordinate filter enabled.
 * @display_state_cmd: command to notify display state.
 * @display_vrefresh_cmd: command to notify display vertical refresh rate.
 * @fw_version_cmd: command to get fw version.
 * @gesture_config_cmd: command to set gesture parameters and gesture types.
 * @grip_cmd: command to set/get grip mode.
 * @heatmap_cmd: command to set heatmap enabled.
 * @int2_cmd: command to set/get int2 mode.
 * @int2_status_cmd: command to get int2 status.
 * @irq_cmd: command to set/get irq mode.
 * @palm_cmd: command to set/get palm mode.
 * @panel_id_cmd: command to get panel id.
 * @panel_speed_mode_cmd: command to set panel speed mode.
 * @ping_cmd: command to ping T-IC.
 * @report_rate_cmd: command to change touch report rate.
 * @reset_cmd: command to reset T-IC.
 * @scan_cmd: command to set/get scan mode.
 * @screen_protector_mode_cmd: command to set/get screen protector mode.
 * @selftest_cmd: command to do self-test.
 * @sensing_cmd: command to set/set sensing mode.
 * @sensor_data_cmd: command to get sensor data.
 * @manual_sensor_data_cmd: command to get sensor data manually.
 * @vendor_register_cmd: command to get touch vendor register.
 * @vsync_hsync_frequency_cmd: command to get vsync and hsync frequency rate.
 * @water_cmd: command to set/get water mode.
 */
struct gti_union_cmd_data {
	struct gti_calibrate_cmd calibrate_cmd;
	struct gti_context_driver_cmd context_driver_cmd;
	struct gti_context_stylus_cmd context_stylus_cmd;
	struct gti_continuous_report_cmd continuous_report_cmd;
	struct gti_coord_filter_cmd coord_filter_cmd;
	struct gti_display_state_cmd display_state_cmd;
	struct gti_display_vrefresh_cmd display_vrefresh_cmd;
	struct gti_fw_version_cmd fw_version_cmd;
	struct gti_gesture_config_cmd gesture_config_cmd;
	struct gti_grip_cmd grip_cmd;
	struct gti_heatmap_cmd heatmap_cmd;
	struct gti_int2_cmd int2_cmd;
	struct gti_int2_status_cmd int2_status_cmd;
	struct gti_irq_cmd irq_cmd;
	struct gti_palm_cmd palm_cmd;
	struct gti_panel_id_cmd panel_id_cmd;
	struct gti_panel_speed_mode_cmd panel_speed_mode_cmd;
	struct gti_ping_cmd ping_cmd;
	struct gti_report_rate_cmd report_rate_cmd;
	struct gti_reset_cmd reset_cmd;
	struct gti_scan_cmd scan_cmd;
	struct gti_screen_protector_mode_cmd screen_protector_mode_cmd;
	struct gti_selftest_cmd selftest_cmd;
	struct gti_sensing_cmd sensing_cmd;
	struct gti_sensor_data_cmd sensor_data_cmd;
	struct gti_sensor_data_cmd manual_sensor_data_cmd;
	struct gti_vendor_register_cmd vendor_register_cmd;
	struct gti_vsync_hsync_frequency_cmd vsync_hsync_frequency_cmd;
	struct gti_water_cmd water_cmd;
};

/**
 * struct gti_gesture_event_data - GTI gesture event data for notifying changed.
 * @gesture_type: triggered gesture type.
 * @x: x coordinate for the finger.
 * @y: x coordinate for the finger.
 * @major: major of the finger in pixels.
 * @minor: minor of the finger in pixels.
 * @angle: finger angle from -4095 ~ 4096.
 */
struct gti_gesture_event_data {
	enum gti_gesture_type type;
	u16 x;
	u16 y;
	u16 major;
	u16 minor;
	s16 angle;
};

struct sttw_out_of_range_payload {
	u16 x_down;
	u16 y_down;
} __packed;

struct sttw_shift_payload {
	u16 x_down;
	u16 y_down;
	u16 x_final;
	u16 y_final;
	u16 distance;
} __packed;

struct sttw_palm_payload {
	u16 x_down;
	u16 y_down;
	u8 node_count;
} __packed;

struct sttw_multi_touch_payload {
	u16 x_down;
	u16 y_down;
	u8 finger_count;
} __packed;

struct sttw_long_press_payload {
	u16 x_down;
	u16 y_down;
	u8 frame_count;
} __packed;

struct sttw_rapid_touch_payload {
	u16 x_down;
	u16 y_down;
	u8 frame_count;
} __packed;

struct lptw_out_of_range_payload {
	u16 x_down;
	u16 y_down;
	u16 x_final;
	u16 y_final;
} __packed;

struct lptw_shift_payload {
	u16 x_down;
	u16 y_down;
	u16 x_valid;
	u16 y_valid;
	u16 x_final;
	u16 y_final;
	u16 distance;
} __packed;

struct lptw_too_small_payload {
	u16 x_down;
	u16 y_down;
	u8 node_count;
} __packed;

struct lptw_palm_payload {
	u16 x_down;
	u16 y_down;
	u8 node_count;
} __packed;

struct lptw_rapid_touch_payload {
	u16 x_down;
	u16 y_down;
	u8 frame_count;
} __packed;

struct invalid_gesture_event {
	uint8_t device_id;
	enum gti_invalid_gesture_type type;
	uint16_t x_down;
	uint16_t y_down;
	uint16_t x_valid;
	uint16_t y_valid;
	uint16_t x_final;
	uint16_t y_final;
	uint16_t distance;
	uint8_t node_count;
	uint8_t finger_count;
	uint8_t frame_count;
	uint64_t timestamp;
	uint8_t reserved[16];
} __packed;

struct invalid_gesture_count {
	uint8_t device_id;
	uint16_t sttw_out_of_range_count;
	uint16_t sttw_shift_count;
	uint16_t sttw_palm_count;
	uint16_t sttw_multi_touch_count;
	uint16_t sttw_long_press_count;
	uint16_t sttw_rapid_touch_count;
	uint16_t lptw_out_of_range_count;
	uint16_t lptw_shift_count;
	uint16_t lptw_palm_count;
	uint16_t lptw_too_small_count;
	uint16_t lptw_rapid_touch_count;
	uint8_t reserved[16];
} __packed;

union invalid_gesture_payload {
	struct sttw_out_of_range_payload sttw_out_of_range;
	struct sttw_shift_payload sttw_shift;
	struct sttw_palm_payload sttw_palm;
	struct sttw_multi_touch_payload sttw_multi_touch;
	struct sttw_long_press_payload sttw_long_press;
	struct sttw_rapid_touch_payload sttw_rapid_touch;
	struct lptw_out_of_range_payload lptw_out_of_range;
	struct lptw_shift_payload lptw_shift;
	struct lptw_too_small_payload lptw_too_small;
	struct lptw_palm_payload lptw_palm;
	struct lptw_rapid_touch_payload lptw_rapid_touch;
	u8 data[];
};

struct gti_invalid_gesture_event_data {
	enum gti_invalid_gesture_type type;
	union invalid_gesture_payload payload;
};

/**
 * struct gti_fw_status_data - GTI fw status data for notifying changed.
 * @noise_level: the noise level for noise mode.
 */
struct gti_fw_status_data {
	enum gti_noise_mode_level noise_level;
	u8 water_mode;
	struct gti_gesture_event_data gesture_event;
	struct gti_invalid_gesture_event_data invalid_gesture_event;
};

/**
 * Declare the function pointer for struct gti_optional_configuration.
 * Please keep this in front of struct gti_optional_configuration definition.
 */
#define GTI_DECLARE_OPT_FUNC(name, type) int (*name)(void *private_data, type cmd);

/**
 * Generate the series of function declarations or assignments for
 * struct gti_optional_configuration. And, combine with "_ops" for corresponding cases.
 * - GTI_OPT_FUNC_WITH_TYPE(GTI_COND_SET_WITH_DEFAULT)
 * - GTI_OPT_FUNC_WITH_TYPE(GTI_DECLARE_NOP_FUNC)
 * - GTI_OPT_FUNC_WITH_TYPE(GTI_DECLARE_OPT_FUNC)
 * Please keep this in front of struct gti_optional_configuration definition.
 */
// clang-format off
 #define GTI_OPT_FUNC_WITH_TYPE(_ops) \
	_ops(calibrate, struct gti_calibrate_cmd *) \
	_ops(get_context_driver, struct gti_context_driver_cmd *) \
	_ops(get_context_stylus, struct gti_context_stylus_cmd *) \
	_ops(get_coord_filter_enabled, struct gti_coord_filter_cmd *) \
	_ops(get_fw_version, struct gti_fw_version_cmd *) \
	_ops(get_grip_mode, struct gti_grip_cmd *) \
	_ops(get_int2_mode, struct gti_int2_cmd *) \
	_ops(get_int2_status, struct gti_int2_status_cmd *) \
	_ops(get_irq_mode, struct gti_irq_cmd *) \
	_ops(get_mutual_sensor_data, struct gti_sensor_data_cmd *) \
	_ops(get_palm_mode, struct gti_palm_cmd *) \
	_ops(get_panel_id, struct gti_panel_id_cmd *) \
	_ops(get_report_rate, struct gti_report_rate_cmd *) \
	_ops(get_scan_mode, struct gti_scan_cmd *) \
	_ops(get_screen_protector_mode, struct gti_screen_protector_mode_cmd *) \
	_ops(get_self_sensor_data, struct gti_sensor_data_cmd *) \
	_ops(get_sensing_mode, struct gti_sensing_cmd *) \
	_ops(get_vendor_register, struct gti_vendor_register_cmd *) \
	_ops(get_vsync_hsync_frequency, struct gti_vsync_hsync_frequency_cmd *) \
	_ops(get_water_mode, struct gti_water_cmd *) \
	_ops(notify_display_state, struct gti_display_state_cmd *) \
	_ops(notify_display_vrefresh, struct gti_display_vrefresh_cmd *) \
	_ops(ping, struct gti_ping_cmd *) \
	_ops(reset, struct gti_reset_cmd *) \
	_ops(selftest, struct gti_selftest_cmd *) \
	_ops(set_continuous_report, struct gti_continuous_report_cmd *) \
	_ops(set_coord_filter_enabled, struct gti_coord_filter_cmd *) \
	_ops(set_gesture_config, struct gti_gesture_config_cmd *) \
	_ops(set_grip_mode, struct gti_grip_cmd *) \
	_ops(set_heatmap_enabled, struct gti_heatmap_cmd *) \
	_ops(set_int2_mode, struct gti_int2_cmd *) \
	_ops(set_irq_mode, struct gti_irq_cmd *) \
	_ops(set_palm_mode, struct gti_palm_cmd *) \
	_ops(set_panel_speed_mode, struct gti_panel_speed_mode_cmd *) \
	_ops(set_report_rate, struct gti_report_rate_cmd *) \
	_ops(set_scan_mode, struct gti_scan_cmd *) \
	_ops(set_screen_protector_mode, struct gti_screen_protector_mode_cmd *) \
	_ops(set_sensing_mode, struct gti_sensing_cmd *) \
	_ops(set_water_mode, struct gti_water_cmd *)
// clang-format on

/**
 * struct gti_optional_configuration - optional configuration by vendor driver.
 * @calibrate: vendor driver operation to exec calibration
 * @get_context_driver: vendor driver operation to update touch offload driver context.
 * @get_context_stylus: vendor driver operation to update touch offload stylus context.
 * @get_coord_filter_enabled: vendor driver operation to get the coordinate filter enabled.
 * @get_fw_version: vendor driver operation to get fw version info.
 * @get_grip_mode: vendor driver operation to get the grip mode setting.
 * @get_int2_mode: vendor driver operation to get int2 mode setting.
 * @get_int2_status: vendor driver operation to get int2 status.
 * @get_irq_mode: vendor driver operation to get irq mode setting.
 * @get_mutual_sensor_data: vendor driver operation to get the mutual sensor data.
 * @get_palm_mode: vendor driver operation to get the palm mode setting.
 * @get_panel_id: vendor driver operation to get panel id.
 * @get_report_rate: vendor driver operation to get report rate.
 * @get_scan_mode: vendor driver operation to get scan mode.
 * @get_screen_protector_mode: vendor driver operation to get screen protector mode.
 * @get_self_sensor_data: vendor driver operation to get the self sensor data.
 * @get_sensing_mode: vendor driver operation to get sensing mode.
 * @get_vsync_hsync_frequency: vendor driver operation to get vsync frequency rate.
 * @get_water_mode: vendor driver operation to get the water mode setting.
 * @notify_display_state: vendor driver operation to notify the display state.
 * @notify_display_vrefresh: vendor driver operation to notify the display vertical refresh rate.
 * @ping: vendor driver operation to ping T-IC.
 * @reset: vendor driver operation to exec reset.
 * @selftest: vendor driver operation to exec self-test.
 * @set_continuous_report: vendor driver operation to apply the continuous reporting setting.
 * @set_coord_filter_enabled: vendor driver operation to apply the coordinate filter enabled.
 * @set_gesture_config: vendor driver operation to apply the gesture settings.
 * @set_grip_mode: vendor driver operation to apply the grip setting.
 * @set_heatmap_enabled: vendor driver operation to apply the heatmap setting.
 * @set_int2_mode: vendor driver operation to apply the int2 setting.
 * @set_irq_mode: vendor driver operation to apply the irq setting.
 * @set_palm_mode: vendor driver operation to apply the palm setting.
 * @set_panel_speed_mode: vendor driver operation to apply the panel speed mode setting.
 * @set_report_rate: driver operation to set touch report rate.
 * @set_scan_mode: vendor driver operation to set scan mode.
 * @set_screen_protector_mode: vendor driver operation to set screen protector mode.
 * @set_sensing_mode: vendor driver operation to set sensing mode.
 * @set_water_mode: vendor driver operation to apply the water setting.
 * @post_irq_thread_fn: post irq thread function that register by vendor driver.
 *
 * Please add the NEW function name with NEW struct type into GTI_OPT_FUNC_WITH_TYPE above once
 * the vendor supports new call-back capability. Then, the series of function declarations and
 * initialization with nop default will handle natively.
 */
struct gti_optional_configuration {
	GTI_OPT_FUNC_WITH_TYPE(GTI_DECLARE_OPT_FUNC);
	/*
	 * Vendor command without GTI default NOP handler.
	 * Need to check before use.
	 */
	irq_handler_t post_irq_thread_fn;
};

/**
 * struct pid_controller - A pid controller.
 * u = (k1*e(i) + k2*e(i-1) + k3*e(i-2)) / div
 *
 * @e0: e(i).
 * @e1: e(i-1).
 * @e2: e(i-2).
 * @k1: the coefficient of e(i).
 * @k2: the coefficient of e(i-1).
 * @k3: the coefficient of e(i-2).
 * @div: for simulate a float value by integer.
 */
struct pid_controller {
	s64 e0;
	s64 e1;
	s64 e2;
	s64 k1;
	s64 k2;
	s64 k3;
	s64 div;
};

/**
 * struct goog_touch_interface - Google touch interface data for Pixel.
 * @vendor_private_data: the private data pointer that used by touch vendor driver.
 * @vendor_dev: pointer to struct device that used by touch vendor driver.
 * @vendor_input_dev: pointer to struct input_dev that used by touch vendor driver.
 * @vendor_spi_dev: pointer to struct spi_device that used by touch vendor driver.
 * @dev: pointer to struct device that used by google touch interface driver.
 * @options: optional configuration that could apply by vendor driver.
 * @input_lock: protect the input report between non-offload and offload.
 * @input_process_lock: mutex for goog_input_process() function.
 * @input_heatmap_lock: mutex for heatmap reading between vendor driver, GTI or sysfs/procfs.
 * @offload: struct that used by touch offload.
 * @offload_frame: reserved frame that used by touch offload.
 * @v4l2: struct that used by v4l2.
 * @cmd: struct that used by vendor default handler.
 * @proc_dir: struct that used for procfs.
 * @proc_heatmap: struct that used for heatmap procfs.
 * @input_dev_mono_ktime: input timestamp used by input dev and input subsystem.
 * @input_timestamp: input timestamp from touch vendor driver.
 * @resample_latency: resample latency in nanoseconds.
 * @abs_x_min: minimum x of input resolution.
 * @abs_x_max: maximum x of input resolution.
 * @abs_y_min: minimum y of input resolution.
 * @abs_y_max: maximum y of input resolution.
 * @resolution_scale_factor: scale factor of input to display resolution.
 * @display_state: current display state.
 * @mf_mode: current motion filter mode.
 * @screen_protector_mode_setting: the setting of screen protector mode.
 * @tbn_register_mask: the tbn_mask that used to request/release touch bus.
 * @pm: struct that used by gti pm.
 * @pm_qos_req: struct that used by pm qos.
 * @fw_status: firmware status such as water_mode, noise_level, etc.
 * @context_changed: flags that indicate driver status changing.
 * @offload_enabled: touch offload is enabled or not.
 * @v4l2_enabled: v4l2 is enabled or not.
 * @tbn_enabled: tbn is enabled or not.
 * @input_timestamp_changed: input timestamp changed from touch vendor driver.
 * @ignore_grip_update: Ignore fw_grip status updates made on offload state change.
 * @default_grip_enabled: the grip default setting.
 * @ignore_palm_update: Ignore fw_palm status updates made on offload state change.
 * @default_palm_enabled: the palm default setting.
 * @default_coord_filter_enabled: the default setting of coordinate filter.
 * @reset_after_selftest: reset FW after running self-test.
 * @lptw_triggered: LPTW is triggered or not.
 * @lptw_suppress_coords_enabled: enable flag for suppressing the coords after lptw.
 * @lptw_track_finger: flag for tracking the suppressed fingers.
 * @late_sense_on_enabled: enable flag for late sense-on.
 * @panel_map_from_tic: enable flag for readiing panel id from tic.
 * @tbn_protection_enabled: enable flag for bus protection.
 * @lptw_track_min_x: minimum x of tracking area.
 * @lptw_track_max_x: maximum x of tracking area.
 * @lptw_track_min_y: minimum y of tracking area.
 * @lptw_track_max_y: maximum y of tracking area.
 * @lptw_cancel_delayed_work: delayed work for canceling finger.
 * @lptw_cancel_time: record the time for lptw cancel timeout.
 * @lptw_down: true if the finger is still on the screen.
 * @lptw_data: x, y, major, minor, angle for the tracking finger.
 * @ignore_force_active: Ignore the force_active sysfs request.
 * @offload_id: id that used by touch offload.
 * @heatmap_buf: heatmap buffer that used by v4l2.
 * @heatmap_buf_size: heatmap buffer size that used by v4l2.
 * @slot: slot id that current used by input report.
 * @slot_bit_in_use: bitmap of slot in use for this input process cycle.
 * @slot_bit_changed: bitmap of slot state changed for this input process cycle.
 * @slot_bit_active: bitmap of active slot during GTI lifecycle.
 * @slot_bit_lptw_track: bitmap of lptw suppressed fingers.
 * @dev_t: dev_t use by alloc_chrdev_region for google interface driver.
 * @panel_id: id of the display panel.
 * @charger_state: indicates a USB charger is connected.
 * @charger_notifier: notifier for power_supply updates.
 * @ical_state: state of interactive calibration finite state machine.
 * @ical_timestamp_ns: time of last interactive calibration state transition.
 * @ical_result: interactive calibration FSM result.
 * @ical_func_result: result returned from the requested interactive function.
 * @frame_index: the count that handle by goog_input_process().
 * @irq_index: irq count that handle by GTI.
 * @input_index: the count of slot bit changed during goog_input_process().
 * @vendor_irq_handler: irq handler that register by vendor driver.
 * @vendor_irq_thread_fn: irq thread function that register by vendor driver.
 * @vendor_irq_cookie: irq cookie that register by vendor driver.
 * @vendor_default_handler: touch vendor driver default operation.
 * @vendor_resume: touch vendor driver resume operation.
 * @vendor_suspend: touch vendor driver suspend operation.
 * @debug_warning_limit: limit number of warning logs.
 * @debug_input: struct that used to debug input.
 * @debug_fifo_input: kfifo struct to track input report.
 * @debug_healthcheck: struct that used for the health check.
 * @debug_fifo_healthcheck: kfifo struct to track touch interrupt information.
 */

struct goog_touch_interface {
	void *vendor_private_data;
	struct device *vendor_dev;
	struct input_dev *vendor_input_dev;
	struct spi_device *vendor_spi_dev;
	struct device *dev;
	struct gti_optional_configuration options;
	struct mutex input_lock;
	struct mutex input_process_lock;
	struct mutex input_heatmap_lock;
	struct touch_offload_context offload;
	struct touch_offload_frame *offload_frame;
	struct v4l2_heatmap v4l2;
	struct gti_union_cmd_data cmd;
	struct proc_dir_entry *proc_dir;
	struct proc_dir_entry *proc_show[GTI_PROC_NUM];
	struct pid_controller pid;
	ktime_t input_dev_mono_ktime;
	ktime_t input_timestamp;
	u64 sensing_timestamp;
	u64 last_sensing_timestamp;
	bool sensing_timestamp_changed;
	u32 default_report_rate;
	u32 report_rate;
	ktime_t frame_time;
	ktime_t resample_latency;

	int abs_x_min;
	int abs_x_max;
	int abs_y_min;
	int abs_y_max;
	int resolution_scale_factor;

	enum gti_display_state_setting display_state;
	enum gti_mf_mode mf_mode;
	enum gti_screen_protector_mode screen_protector_mode_setting;
	u32 tbn_register_mask;
	struct gti_pm pm;
	struct touch_sim *sim;
	struct gti_status_event_dev status_event_dev;

	struct pm_qos_request pm_qos_req;
	struct notifier_block display_state_notifier;
	struct notifier_block tbn_event_notifier;

	struct gti_fw_status_data fw_status;
	struct gti_context_changed context_changed;

	bool offload_enabled;
	bool v4l2_enabled;
	bool tbn_enabled;
	bool input_timestamp_changed;
	bool ignore_grip_update;
	bool default_grip_enabled;
	bool ignore_palm_update;
	bool default_palm_enabled;
	bool default_coord_filter_enabled;
	bool reset_after_selftest;
	bool lptw_triggered;
	bool lptw_suppress_coords_enabled;
	bool timestamp_correction_enabled;
	bool lptw_track_finger;
	bool late_sense_on_enabled;
	bool panel_map_from_tic;
	bool tbn_protection_enabled;
	bool touch_sim_enabled;
	u32 lptw_track_min_x;
	u32 lptw_track_max_x;
	u32 lptw_track_min_y;
	u32 lptw_track_max_y;
	struct delayed_work lptw_cancel_delayed_work;
	ktime_t lptw_cancel_time;

	bool lptw_down;
	union {
		int lptw_data[6];
		struct {
			int lptw_x;
			int lptw_y;
			int lptw_major;
			int lptw_minor;
			int lptw_angle;
			int lptw_finger_count;
		};
	};

	bool ignore_force_active;
	bool manual_heatmap_from_irq;
	union {
		u8 offload_id_byte[4];
		u32 offload_id;
	};
	u8 *heatmap_buf;
	u32 heatmap_buf_size;
	int slot;
	unsigned long slot_bit_in_use;
	unsigned long slot_bit_changed;
	unsigned long slot_bit_active;
	unsigned long slot_bit_lptw_track;
	dev_t dev_t;
	int panel_id;
	char fw_name[64];
	char config_name[64];
	char test_limits_name[64];
	char usb_psy_name[64];

	u8 charger_state;
	struct notifier_block charger_notifier;

	u32 ical_state;
	u64 ical_timestamp_ns;
	s32 ical_result;
	s32 ical_func_result;

	u64 frame_index;
	u64 irq_index;
	u64 input_index;
	irq_handler_t vendor_irq_handler;
	irq_handler_t vendor_irq_thread_fn;
	void *vendor_irq_cookie;

	int (*vendor_default_handler)(void *private_data,
		enum gti_cmd_type cmd_type, struct gti_union_cmd_data *cmd);

	int (*vendor_resume)(struct device *dev);
	int (*vendor_suspend)(struct device *dev);

	/*
	 * Debug used.
	 * Please remember to use INIT_KFIFO() to init the kfifo that declare below.
	 */
	int debug_warning_limit;
	struct gti_debug_input debug_input[MAX_SLOTS];
	struct gti_debug_input debug_input_history[GTI_DEBUG_INPUT_KFIFO_LEN];
	DECLARE_KFIFO(debug_fifo_input, struct gti_debug_input, GTI_DEBUG_INPUT_KFIFO_LEN);
	struct gti_debug_healthcheck debug_healthcheck;
	struct gti_debug_healthcheck debug_healthcheck_history[GTI_DEBUG_HEALTHCHECK_KFIFO_LEN];
	DECLARE_KFIFO(debug_fifo_healthcheck, struct gti_debug_healthcheck,
		GTI_DEBUG_HEALTHCHECK_KFIFO_LEN);
	struct gti_debug_offload_toggle
		debug_offload_toggle_history[GTI_DEBUG_OFFLOAD_TOGGLE_KFIFO_LEN];
	DECLARE_KFIFO(debug_fifo_offload_toggle, struct gti_debug_offload_toggle,
		      GTI_DEBUG_OFFLOAD_TOGGLE_KFIFO_LEN);
};

/*-----------------------------------------------------------------------------
 * Forward declarations.
 */
inline bool goog_check_spi_dma_enabled(struct spi_device *spi_dev);
inline bool goog_check_late_sense_on_enabled(struct goog_touch_interface *gti);
inline void goog_input_lock(struct goog_touch_interface *gti);
inline void goog_input_unlock(struct goog_touch_interface *gti);
inline void goog_input_set_timestamp(
		struct goog_touch_interface *gti,
		struct input_dev *dev, ktime_t timestamp);
inline void goog_input_set_sensing_timestamp(
		struct goog_touch_interface *gti,
		struct input_dev *dev, u64 timestamp);
inline void goog_input_mt_slot(
		struct goog_touch_interface *gti,
		struct input_dev *dev, int slot);
inline void goog_input_mt_report_slot_state(
		struct goog_touch_interface *gti,
		struct input_dev *dev, unsigned int tool_type, bool active);
inline void goog_input_report_abs(
		struct goog_touch_interface *gti,
		struct input_dev *dev, unsigned int code, int value);
inline void goog_input_report_key(
		struct goog_touch_interface *gti,
		struct input_dev *dev, unsigned int code, int value);
inline void goog_input_sync(struct goog_touch_interface *gti, struct input_dev *dev);
inline void goog_input_unregister_device(struct device *vendor_dev,
					 struct input_dev *vendor_input_dev);
inline int goog_input_register_device(struct device *vendor_dev,
				      struct input_dev *vendor_input_dev);
inline int goog_devm_request_threaded_irq(struct goog_touch_interface *gti, struct device *dev,
					  unsigned int irq, irq_handler_t handler,
					  irq_handler_t thread_fn, unsigned long irqflags,
					  const char *devname, void *cookie);
inline int gti_sysfs_create_vendor_input_link(struct goog_touch_interface *gti);
void goog_devm_free_irq(struct goog_touch_interface *gti,
		struct device *dev, unsigned int irq);
inline int goog_request_threaded_irq(struct goog_touch_interface *gti, unsigned int irq,
				     irq_handler_t handler, irq_handler_t thread_fn,
				     unsigned long irqflags, const char *devname, void *cookie);

int goog_process_vendor_cmd(struct goog_touch_interface *gti, enum gti_cmd_type cmd_type);
int goog_input_process(struct goog_touch_interface *gti, bool reset_data);
struct goog_touch_interface *
goog_touch_interface_connect(struct device *vendor_dev,
			     int (*vendor_default_handler)(void *vendor_private_data, u32 cmd_type,
							   struct gti_union_cmd_data *cmd),
			     struct gti_optional_configuration *vendor_options);
int goog_touch_interface_disconnect(struct device *vendor_dev);
struct goog_touch_interface *goog_touch_interface_probe(
	void *vendor_private_data, struct device *vendor_dev, struct input_dev *vendor_input_dev,
	int (*vendor_default_handler)(void *vendor_private_data, enum gti_cmd_type cmd_type,
				      struct gti_union_cmd_data *cmd),
	struct gti_optional_configuration *vendor_options);
int goog_touch_interface_remove(struct goog_touch_interface *gti);

void goog_reset_fw_status(struct goog_touch_interface *gti);
void goog_notify_fw_status_changed(struct goog_touch_interface *gti, enum gti_fw_status status,
				   struct gti_fw_status_data *data);

int goog_get_lptw_triggered(struct goog_touch_interface *gti);
void goog_lptw_notifier_register(struct notifier_block *nb, bool reg);

int goog_get_panel_id(struct device_node *node);
int goog_get_firmware_name(struct device_node *node, int id, char *name, size_t size);
int goog_get_config_name(struct device_node *node, int id, char *name, size_t size);
int goog_get_test_limits_name(struct device_node *node, int id, char *name, size_t size);

int goog_pm_register_notification(struct goog_touch_interface *gti, const struct dev_pm_ops *ops);
int goog_pm_unregister_notification(struct goog_touch_interface *gti);
int goog_pm_wake_lock_nosync(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type,
			     bool skip_pm_resume);
int goog_pm_wake_lock(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type,
		      bool skip_pm_resume);
int goog_pm_wake_unlock_nosync(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type);
int goog_pm_wake_unlock(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type);
bool goog_pm_wake_check_locked(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type);
u32 goog_pm_wake_get_locks(struct goog_touch_interface *gti);

#endif // _GOOG_TOUCH_INTERFACE_

