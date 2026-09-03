/* SPDX-License-Identifier: GPL-2.0 */

#ifndef TOUCHSCREEN_BUS_NEGOTIATOR_H
#define TOUCHSCREEN_BUS_NEGOTIATOR_H

#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#include "goog_touch_interface.h"

#define TBN_DEVICE_NAME "tbn"
#define TBN_CLASS_NAME "tbn"

#define TBN_REQUEST_BUS_TIMEOUT_MS 500
#define TBN_RELEASE_BUS_TIMEOUT_MS 500

enum tbn_mode {
	TBN_MODE_DISABLED = 0,
	TBN_MODE_GPIO,
	TBN_MODE_AOC_CHANNEL,
	TBN_MODE_MOCK,
};

enum tbn_bus_owner {
	TBN_BUS_OWNER_AP = 0,
	TBN_BUS_OWNER_AOC = 1,
};

#ifndef __CODESONAR__
#define ENUM_U32 : __u32
#define ENUM_U8 : __u8
#else
#define ENUM_U32
#define ENUM_U8
#endif

enum TbnOperation ENUM_U32 {
	TBN_OPERATION_IDLE = 0,
	TBN_OPERATION_AP_RELEASE_BUS,
	TBN_OPERATION_AP_REQUEST_BUS,
	TBN_OPERATION_AOC_RESET,
	TBN_OPERATION_AOC_SEND_LPTW_EVENT,
	TBN_OPERATION_AOC_SEND_GESTURE_EVENT = TBN_OPERATION_AOC_SEND_LPTW_EVENT,
	TBN_OPERATION_AP_PANEL_SETTINGS,
	TBN_OPERATION_AP_LPTW_CONFIGS,
	TBN_OPERATION_AP_STTW_CONFIGS,
	TBN_OPERATION_INVALID_GESTURE_EVENT,
	TBN_OPERATION_INVALID_GESTURE_COUNTS,
	TBN_OPERATION_DEVICE_SWITCH_FAILURE,
};

enum GestureType ENUM_U8 {
	kDisabled = 0,
	kSingleTap = 1 << 0,
	kLongPress = 1 << 1,
	kSingleTapAndLongPress = kSingleTap | kLongPress,
	kDoubleTap = 1 << 2,
	kDoubleTapAndLongPress = kDoubleTap | kLongPress,
};

#undef ENUM_U32
#undef ENUM_U8

struct PanelSettings {
	uint8_t version;
	uint8_t device_id;
	enum GestureType type;
	uint16_t panel_height_pixel;
	uint16_t panel_height_mm;
	uint8_t super_resolution_scale;
	uint8_t lptw_support_int2;
	uint8_t high_sensitivity_mode;
	uint8_t checksum_enabled;
	/* reserved for the rest size of touch aoc channel */
	uint8_t reserved[37];
} __packed;

struct STTWParams {
	uint8_t version;
	uint8_t device_id;
	uint16_t min_x;
	uint16_t max_x;
	uint16_t min_y;
	uint16_t max_y;
	uint8_t min_frame_count;
	uint8_t max_frame_count;
	uint16_t motion_tolerance;
	uint8_t max_touch_size;
	/* reserved for the rest size of touch aoc channel */
	uint8_t reserved[33];
} __packed;

struct LPTWParams {
	uint8_t version;
	uint8_t device_id;
	uint16_t min_x;
	uint16_t max_x;
	uint16_t min_y;
	uint16_t max_y;
	uint8_t min_frame_count;
	uint8_t max_touch_size;
	uint16_t marginal_min_x;
	uint16_t marginal_max_x;
	uint16_t marginal_min_y;
	uint16_t marginal_max_y;
	uint8_t monitor_channel_min_tx;
	uint8_t monitor_channel_max_tx;
	uint8_t monitor_channel_min_rx;
	uint8_t monitor_channel_max_rx;
	uint8_t min_node_count;
	uint16_t motion_tolerance_inner;
	uint16_t motion_tolerance_outer;
	uint16_t int2_assert_min_x;
	uint16_t int2_assert_max_x;
	uint16_t int2_assert_min_y;
	uint16_t int2_assert_max_y;
	uint16_t int2_deassert_min_x;
	uint16_t int2_deassert_max_x;
	uint16_t int2_deassert_min_y;
	uint16_t int2_deassert_max_y;
	/* reserved for the rest size of touch aoc channel */
	uint8_t reserved[3];
} __packed;

struct TbnEvent {
	__u32 id;
	enum TbnOperation operation;
	__u8 data[48];
} __packed;

struct TbnEventHeader {
	__u32 id;
	__s32 err;
	enum TbnOperation operation;
	__u8 data[52];
} __packed;

struct TbnEventResponse {
	__u32 id;
	__s32 err;
	enum TbnOperation operation;
	bool lptw_triggered;
} __packed;

struct TbnLptwEvent {
	__u32 id;
	__s32 err;
	enum TbnOperation operation;
	u16 x;
	u16 y;
	u16 major;
	u16 minor;
	s16 angle;
} __packed;

struct TbnGestureEvent {
	__u32 id;
	__s32 err;
	enum TbnOperation operation;
	u64 isr_time;
	u8 lptw_finger_count;
	enum GestureType type;
	u16 x;
	u16 y;
	u16 major;
	u16 minor;
	s16 angle;
	/* reserved 16 bytes */
	u8 reserved[16];
} __packed;

struct TbnPanelSettingsEvent {
	uint32_t id;
	enum TbnOperation operation;
	struct PanelSettings settings;
} __packed;

struct TbnSttwConfigsEvent {
	uint32_t id;
	enum TbnOperation operation;
	struct STTWParams params;
} __packed;

struct TbnLptwConfigsEvent {
	uint32_t id;
	enum TbnOperation operation;
	struct LPTWParams params;
} __packed;

struct TbnInvalidGestureEvent {
	__u32 id;
	__s32 err;
	enum TbnOperation operation;
	uint8_t data[43];
} __packed;

struct TbnInvalidGestureCountEvent {
	__u32 id;
	__s32 err;
	enum TbnOperation operation;
	uint8_t data[39];
} __packed;

struct TbnDeviceSwitchFailureEvent {
	__u32 id;
	__s32 err;
	enum TbnOperation operation;
	u8 device_id;
	bool enable_device_failure;
	bool disable_device_failure;
} __packed;

struct tbn_context {
	struct device *dev;
	struct completion bus_requested;
	struct completion bus_released;
	struct mutex dev_mask_mutex;
	u32 mode;
	u32 max_devices;
	u32 registered_mask;
	u32 requested_dev_mask;
	struct gpio_desc *aoc2ap_gpio;
	struct gpio_desc *ap2aoc_gpio;
	int aoc2ap_irq;
	struct task_struct *aoc_channel_task;

	/* event management */
	struct TbnEventResponse event_resp;
	struct TbnEvent event;
	struct mutex event_lock;
	struct work_struct aoc_reset_work;
	struct workqueue_struct *event_wq;

	u32 event_id;
	struct TbnPanelSettingsEvent *panel_settings;
	struct TbnSttwConfigsEvent *sttw_configs;
	struct TbnLptwConfigsEvent *lptw_configs;

	void (*lptw_event_cb)(struct TbnGestureEvent *lptw, void *user_data);
	void *lptw_event_cbdata;
};

void tbn_update_super_resolution_scale(u8 scale, u32 idx);
void tbn_update_high_sensitivity_mode(u8 mode, u32 idx);
void tbn_update_checksum_enabled(u8 enabled, u32 idx);
void tbn_update_gesture_config(enum gti_gesture_params param, u16 value, u32 idx);
void tbn_debug_configs_dump(struct seq_file *m, u32 dev_id);
void tbn_event_notifier_register(struct notifier_block *nb, bool reg);

int register_tbn(u32 *output);
void register_tbn_lptw_callback(void *callback, void *cbdata);
void unregister_tbn(u32 *output);
int tbn_request_bus_with_result(u32 dev_mask, bool *lptw_triggered);
int tbn_request_bus(u32 dev_mask);
int tbn_release_bus(u32 dev_mask);

#endif /* TOUCHSCREEN_BUS_NEGOTIATOR_H */
