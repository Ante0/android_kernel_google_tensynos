/* SPDX-License-Identifier: GPL-2.0
 *
 * Synaptics TouchCom touchscreen driver
 *
 * Copyright (C) 2017-2024 Synaptics Incorporated. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * INFORMATION CONTAINED IN THIS DOCUMENT IS PROVIDED "AS-IS," AND SYNAPTICS
 * EXPRESSLY DISCLAIMS ALL EXPRESS AND IMPLIED WARRANTIES, INCLUDING ANY
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE,
 * AND ANY WARRANTIES OF NON-INFRINGEMENT OF ANY INTELLECTUAL PROPERTY RIGHTS.
 * IN NO EVENT SHALL SYNAPTICS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, PUNITIVE, OR CONSEQUENTIAL DAMAGES ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OF THE INFORMATION CONTAINED IN THIS DOCUMENT, HOWEVER CAUSED
 * AND BASED ON ANY THEORY OF LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, AND EVEN IF SYNAPTICS WAS ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE. IF A TRIBUNAL OF COMPETENT JURISDICTION DOES
 * NOT PERMIT THE DISCLAIMER OF DIRECT DAMAGES OR ANY OTHER DAMAGES, SYNAPTICS'
 * TOTAL CUMULATIVE LIABILITY TO ANY PARTY SHALL NOT EXCEED ONE HUNDRED U.S.
 * DOLLARS.
 */

/**
 * @file syna_tcm2.h
 *
 * The header file for the Synaptics TouchComm reference driver.
 */

#ifndef _SYNAPTICS_TCM2_DRIVER_H_
#define _SYNAPTICS_TCM2_DRIVER_H_

#include "syna_tcm2_platform.h"
#include "synaptics_touchcom_core_dev.h"
#include "synaptics_touchcom_func_touch.h"

#define PLATFORM_DRIVER_NAME "synaptics_tcm"

#define TOUCH_INPUT_NAME "synaptics_tcm_touch"
#define TOUCH_INPUT_PHYS_PATH "synaptics_tcm/touch_input"

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
#include <goog_touch_interface.h>
#endif

#define CHAR_DEVICE_NAME "tcm"
#define CHAR_DEVICE_MODE (0x0600)

#define SYNAPTICS_TCM_DRIVER_ID (1 << 0)
#define SYNAPTICS_TCM_DRIVER_VERSION 1
#define SYNAPTICS_TCM_DRIVER_SUBVER "6.4"


/*
 * Modules Configurations
 */

/** TDDI_PRODUCTS
 *         Open to enable the support of TDDI products.
 *         Typically, it's aligned with the deconfig, CONFIG_TOUCHSCREEN_SYNA_TCM2_TDDI
 */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TDDI)
#define TDDI_PRODUCTS
#endif

/** HAS_SYSFS_INTERFACE
 *         Open to enable the sysfs kernel attributes.
 *         Typically, it's aligned with the deconfig, CONFIG_TOUCHSCREEN_SYNA_TCM2_SYSFS
 */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_SYSFS)
#define HAS_SYSFS_INTERFACE
#endif

/** HAS_REFLASH_FEATURE
 *  HAS_TDDI_REFLASH_FEATURE
 *         Open to enable firmware reflash features.
 *         Typically, it's aligned with the deconfig, CONFIG_TOUCHSCREEN_SYNA_TCM2_REFLASH
 */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_REFLASH)
#if defined(TDDI_PRODUCTS)
#define HAS_TDDI_REFLASH_FEATURE
#else
#define HAS_REFLASH_FEATURE
#endif
#endif
/** HAS_TESTING_FEATURE
 *         Open to enable testing features.
 *         Typically, it's aligned with the deconfig, CONFIG_TOUCHSCREEN_SYNA_TCM2_TESTING
 */
#if defined(CONFIG_TOUCHSCREEN_SYNA_TCM2_TESTING)
#define HAS_TESTING_FEATURE
#endif


/*
 * Driver Configurations
 */

/** TYPE_B_PROTOCOL
 *         Open to enable the multi-touch (MT) protocol
 */
#define TYPE_B_PROTOCOL

/** RESET_ON_CONNECT
 *         Open if willing to issue a reset when connecting to the
 *         touch controller. Set "enable" in default.
 */
#define RESET_ON_CONNECT

/** RESET_ON_RESUME
 *         Open if willing to issue a reset to the touch controller
 *         from suspend. Set "disable" in default.
 */
/* #define RESET_ON_RESUME */

/*
 * @brief: GOOG_INT2_FEATURE
 *         Set "disable" in default.
 */
#define GOOG_INT2_FEATURE

/** LOW_POWER_MODE
 *         Open if willing to enter the lower power mode when the system
 *         going to the suspend mode; otherwise, expected that no power
 *         supplied. Set "enable" in default.
 */
#define LOW_POWER_MODE

#if defined(LOW_POWER_MODE)
/** ENABLE_WAKEUP_GESTURE
 *         Open if having wake-up gesture support.
 */
#define ENABLE_WAKEUP_GESTURE
#endif

/** REPORT_SWAP_XY
 *  REPORT_FLIP_X
 *  REPORT_FLIP_Y
 *         Open if willing to modify the touch data before sending to the
 *         input event subsystem. Set "disable" in default.
 */
/* #define REPORT_SWAP_XY */
/* #define REPORT_FLIP_X */
/* #define REPORT_FLIP_Y */

/**  REPORT_TOUCH_WIDTH
 *         Open if willing to add the width data to the input event.
 */
#define REPORT_TOUCH_WIDTH

#if defined(TDDI_PRODUCTS)
/**  REPORT_KNOB
 *         Open if willing to add the knob data to the input event.
 */
/* #define REPORT_KNOB */
#endif

/** USE_CUSTOM_TOUCH_REPORT_CONFIG
 *         Open if willing to set up the format of touch report.
 *         The custom_touch_format[] array in syna_tcm2.c can be used
 *         to describe the customized report format.
 */
/* #define USE_CUSTOM_TOUCH_REPORT_CONFIG */

/** ENABLE_CUSTOM_TOUCH_ENTITY
 *         Open if having the requirements to parse the custom touch code entity.
 */
#define ENABLE_CUSTOM_TOUCH_ENTITY

/** STARTUP_REFLASH
 *         Open if willing to do fw checking and update at startup.
 *         The firmware image will be obtained by request_firmware() API,
 *         so please ensure the image is built-in or included properly.
 */
#if defined(HAS_REFLASH_FEATURE) || defined(HAS_TDDI_REFLASH_FEATURE)
#define STARTUP_REFLASH

#define FW_IMAGE_NAME "synaptics.img"
#endif

/** ENABLE_DISP_NOTIFIER
 *         Open if having display notification event and willing to listen
 *         the event from display driver.
 *
 *         Set "disable" in default due to no generic notifier for DRM
 */
#if defined(CONFIG_FB) || defined(CONFIG_DRM_BRIDGE)
/* #define ENABLE_DISP_NOTIFIER */
#endif
/** USE_FB
 *         Open if having the support of FB (Frame Buffer) and willing to listen
 *         the event from display driver.
 *         This property is available only when CONFIG_FB in used
 */
#if defined(ENABLE_DISP_NOTIFIER) && defined(CONFIG_FB)
#define USE_FB
#endif
/** RESUME_EARLY_UNBLANK
 *         Open if willing to resume in early un-blanking state.
 *         This property is available only when ENABLE_DISP_NOTIFIER
 *         feature is enabled.
 */
#ifdef ENABLE_DISP_NOTIFIER
/* #define RESUME_EARLY_UNBLANK */
#endif
/** USE_DRM_BRIDGE
 *         Open if having the support of DRM bridge and willing to listen
 *         the event from display driver.
 *         This property is available only when CONFIG_DRM_BRIDGE in used
 */
#if defined(ENABLE_DISP_NOTIFIER) && defined(CONFIG_DRM_BRIDGE)
#define USE_DRM_BRIDGE
#endif

/** ENABLE_EXTERNAL_FRAME_PROCESS
 *         Open if willing to pass the data to the userspace application.
 */
#define ENABLE_EXTERNAL_FRAME_PROCESS

/** FORCE_CONNECTION
 *         Force to install the driver even though the occurrence of errors.
 */
/* #define FORCE_CONNECTION */

/** ENABLE_HELPER
 *         Open if willing to do additional handling in the background workqueue.
 */
#define ENABLE_HELPER


#if defined(TDDI_PRODUCTS)
/** IS_TDDI_MULTICHIP
 *         Indicate the TDDI multichip architecture
 */
/* #define IS_TDDI_MULTICHIP */
#endif


/*
 * Definitions of TouchComm device driver
 */


/** Enumeration of the power states */
enum power_state {
	PWR_OFF = 0,
	PWR_ON,
	LOW_PWR,
	BARE_MODE,
};

#if defined(ENABLE_HELPER)
/** Definitions of the background helper thread */
enum helper_task {
	HELP_NONE = 0,
	HELP_RESET_DETECTED,
};

struct syna_tcm_helper {
	syna_pal_atomic_t task;
	struct work_struct work;
};
#endif

/**
 * @brief: Structure for $C2 report
 *
 * Enumerate the power states of device
 */
struct custom_fw_status {
	union {
		struct {
			unsigned char b0_moisture:1;
			unsigned char b1_noise_state:1;
			unsigned char b2_freq_hopping:1;
			unsigned char b3_grip:1;
			unsigned char b4_palm:1;
			unsigned char b5_fast_relaxation:1;
			unsigned char b6__7_reserved:2;
			unsigned char reserved;
		} __packed;
		unsigned char data[2];
	};
};

/**
 * @brief: Custom Commands, Reports, or Events
 */
enum custom_report_types {
	REPORT_FW_STATUS = 0xc2,
	REPORT_HEAT_MAP = 0xc3,
	REPORT_HEAT_MAP_WITH_METADATA = 0xc4,
	REPORT_TOUCH_AND_HEATMAP = 0xc5,
	REPORT_TOUCH_AND_HEATMAP_WITH_METADATA = 0xc6,
};

enum custom_dynamic_config {
	DC_STTW_JITTER = 0xC2,
	DC_STTW_MAX_TOUCH_SIZE = 0xC3,
	DC_STTW_MIN_FRAME = 0xC5,
	DC_STTW_MAX_FRAME = 0xC6,
	DC_STTW_MIN_X = 0xC7,
	DC_STTW_MAX_X = 0xC8,
	DC_STTW_MIN_Y = 0xC9,
	DC_STTW_MAX_Y = 0xCA,
	DC_HIGH_SENSITIVITY_MODE = 0xCB,
	DC_INT2_PRODUCTION_CMD = 0xD2,
	DC_LPTW_MIN_X = 0xD7,
	DC_LPTW_MAX_X = 0xD8,
	DC_LPTW_MIN_Y = 0xD9,
	DC_LPTW_MAX_Y = 0xDA,
	DC_LPTW_MIN_FRAME = 0xDB,
	DC_LPTW_JITTER = 0xDC,
	DC_LPTW_MAX_TOUCH_SIZE = 0xDD,
	DC_LPTW_MARGINAL_MIN_X = 0xDE,
	DC_LPTW_MARGINAL_MAX_X = 0xDF,
	DC_LPTW_MARGINAL_MIN_Y = 0xE0,
	DC_LPTW_MARGINAL_MAX_Y = 0xE1,
	DC_LPTW_MONITOR_CH_MIN_TX = 0xE2,
	DC_LPTW_MONITOR_CH_MAX_TX = 0xE3,
	DC_LPTW_MONITOR_CH_MIN_RX = 0xE4,
	DC_LPTW_MONITOR_CH_MAX_RX = 0xE5,
	/* Set 0 for high report rate(240Hz), 1 for low report rate(120Hz). */
	DC_REPORT_RATE_SWITCH = 0xE6,
	DC_LPTW_NODE_COUNT_MIN = 0xE7,
	DC_LPTW_MOTION_BOUNDARY = 0xE8,
	DC_FORCE_DOZE_MODE = 0xF0,
	DC_COMPRESSION_THRESHOLD = 0xF1,
	DC_TOUCH_SCAN_MODE = 0xF2,
	DC_ENABLE_PALM_REJECTION = 0xF3,
	/*
	 * DC_CONTINUOUSLY_REPORT: Enable/Disable continuously reporting when
	 * it's controlled by the host.
	 */
	DC_CONTINUOUSLY_REPORT = 0xF5,
	/*
	 * DC_HOST_CONTINUOUSLY_REPORT: Select to control continuously
	 * reporting by the host or by the firmware.
	 */
	DC_HOST_CONTINUOUSLY_REPORT = 0xF6,
	DC_GRIP_DELTA_THRESHOLD = 0xF6,
	DC_GRIP_BORDER_THRESHOLD = 0xF7,
	DC_COORD_FILTER = 0xF8,
	DC_HEATMAP_MODE = 0xFC,
	DC_FW_RESET_REASON = 0xFD,
	DC_GESTURE_TYPE = 0xFE,
};

#if defined(ENABLE_WAKEUP_GESTURE)
/**
 * @brief: Custom gesture type
 */
enum custom_gesture_type {
	GESTURE_NONE = 0,
	GESTURE_SINGLE_TAP = 6,
	GESTURE_LONG_PRESS = 11,
};
#endif

#if defined(ENABLE_CUSTOM_TOUCH_ENTITY)
/**
 * @brief: Custom touch entity code
 */
enum custom_shape_data {
	TOUCH_ENTITY_CUSTOM_ANGLE = 0xD1,
	TOUCH_ENTITY_CUSTOM_MAJOR = 0xD2,
	TOUCH_ENTITY_CUSTOM_MINOR = 0xD3,
	TOUCH_ENTITY_SYNC_LOST_STATE = 0xD4,
	TOUCH_ENTITY_METADATA = 0xD5,
};

enum custom_touch_entity {
	CUSTOM_DATA_ANGLE = 0x0,
	CUSTOM_DATA_MAJOR = 0x1,
	CUSTOM_DATA_MINOR = 0x2,
};
#endif

/*
 * @section: Touch Scan Mode Dynamic Configuration
 *
 * The current touch scan mode.
 */
enum tcm_scan_mode {
	SCAN_NORMAL_IDLE = 0,
	SCAN_NORMAL_ACTIVE,
	SCAN_LPWG_IDLE,
	SCAN_LPWG_ACTIVE,
	SCAN_SLEEP,
};

/*
 * @section: Touch INT2 Production Configuration
 *
 * The current touch INT2.
 */
enum tcm_int2_production {
	INT2_PRODUCTION_DISABLE = 0,
	INT2_PRODUCTION_HIGH = 1,
	INT2_PRODUCTION_LOW = 3,
};

/*
 * @section: Heatmap Mode Configuration
 *
 */
enum tcm_heatmap_mode {
	HEATMAP_MODE_COORD = 1,
	HEATMAP_MODE_COMBINED = 4,
	HEATMAP_MODE_COMBINED_WITH_METADATA = 16,
};

enum tcm_gesture_type {
	GESTURE_TYPE_STTW = 1,
	GESTURE_TYPE_LPTW = 2,
	GESTURE_TYPE_STTW_AND_LPTW = 3,
};


/**
 * Context of Synaptics TouchComm device driver
 *
 * The structure defines the kernel specific data and the essentials
 * for the device driver.
 */
struct syna_tcm {

	/* Context for the use of TouchComm core library */
	struct tcm_dev *tcm_dev;

	/* Pointer to platform device */
	struct platform_device *pdev;

	/* Stuff related to touch data */
	struct tcm_touch_data_blob tp_data;
	unsigned char prev_obj_status[MAX_NUM_OBJECTS];

	/* Abstraction of hardware interface */
	struct syna_hw_interface *hw_if;

	/* Stuff related to irq event */
	struct tcm_buffer event_data;
	pid_t isr_pid;
	bool irq_wake;

	/* Stuff related to cdev interface */
	struct cdev char_dev;
	dev_t char_dev_num;
	int char_dev_ref_count;
	struct class *device_class;
	struct device *device;

	union {
		u32 sysfs_debug;
		struct {
			u32 sysfs_debug_simulation_data:1;
		};
	};

#if defined(HAS_SYSFS_INTERFACE)
	/* Stuff related to sysfs attributes */
	struct kobject *sysfs_dir;
	struct kobject *sysfs_dir_utility;
#if defined(HAS_TESTING_FEATURE)
	struct kobject *sysfs_dir_testing;
#endif
#endif

	/* Stuff related to the registration of input device */
	struct input_dev *input_dev;
	struct input_params {
		unsigned int max_x;
		unsigned int max_y;
		unsigned int max_objects;
	} input_dev_params;

#if defined(STARTUP_REFLASH)
	/* Workqueue used for firmware update */
	struct delayed_work reflash_work;
	struct workqueue_struct *reflash_workqueue;
	u8 reflash_count;
	bool force_reflash;
#endif
	struct workqueue_struct *event_wq;
	struct pinctrl *pinctrl;

	u32 raw_timestamp_sensing;
	u64 timestamp_sensing;
	ktime_t timestamp; /* Time that the event was first received from the
				* touch IC, acquired during hard interrupt, in
				* CLOCK_MONOTONIC */

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* Stored the last status data */
	struct custom_fw_status fw_status;

	unsigned short heatmap_mode;
	bool set_continuously_report;
	uint16_t *mutual_data;
	uint16_t *self_data;
	uint16_t *mutual_data_manual;
	uint16_t *self_data_manual;
	struct goog_touch_interface *gti;
	/* Work for setting coordinate filter. */
	struct work_struct set_coord_filter_work;
	/* Work for setting firmware grip mode. */
	struct work_struct set_grip_mode_work;
	/* Work for setting firmware palm mode. */
	struct work_struct set_palm_mode_work;
	/* Work for setting heatmap mode. */
	struct work_struct set_heatmap_enabled_work;
	/* Work for setting screen protector mode. */
	struct work_struct set_screen_protector_mode_work;
	/* Work for continuous report commands. */
	struct work_struct set_continuous_report_work;
	/* Work for setting sensing mode. */
	struct work_struct set_sensing_mode_work;
#else
	syna_pal_mutex_t tp_event_mutex;
#endif
	syna_pal_mutex_t raw_data_mutex;

	/* IOCTL-related variables */
	pid_t proc_pid;
	struct task_struct *proc_task;

	int touch_count;

	/* frame-buffer callbacks notifier */
#if defined(USE_FB)
	struct notifier_block fb_notifier;
	unsigned char fb_ready;
#endif
	u8 raw_data_report_code;
	s16 *raw_data_buffer;
	struct completion raw_data_completion;
	bool coord_filter_enable;
	bool high_sensitivity_mode;
	u8 enable_fw_grip;
	u8 enable_fw_palm;

#if defined(USE_DRM_BRIDGE)
	struct drm_bridge panel_bridge;
	struct drm_connector *connector;
	bool is_panel_lp_mode;
#endif
	/* fifo to pass the data to userspace */
#if defined(ENABLE_EXTERNAL_FRAME_PROCESS)
	/* Kernel FIFO */
	unsigned int fifo_remaining_frame;
	struct list_head frame_fifo_queue;
	wait_queue_head_t wait_frame;
#endif

#if defined(ENABLE_HELPER)
	/* Background workqueue */
	struct syna_tcm_helper helper;
#endif

	/* Misc. variables */
	int pwr_state;
	bool slept_in_early_suspend;
	bool lpwg_enabled;
	bool is_connected;
#if defined(TDDI_PRODUCTS)
	bool is_tddi_multichip;
#endif
	bool concurrent_reporting;
	bool has_sync_lost;
	bool has_sync_lost_last;
	bool abnormal_gesture_reported;

	/* Pointer of userspace application info data */
	void *userspace_app_info;

	/* Abstractions */
	int (*dev_connect)(struct syna_tcm *tcm);
	int (*dev_disconnect)(struct syna_tcm *tcm);
	int (*dev_set_up_app_fw)(struct syna_tcm *tcm);
	int (*dev_resume)(struct device *dev);
	int (*dev_suspend)(struct device *dev);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* Self test function pointer.
	 *
	 * @param
	 *    [ in] private_data: driver data
	 *    [out] cmd: self test result
	 *
	 * @return
	 *    on success, 0; otherwise, negative value on error.
	 */
	int (*selftest)(void *private_data, struct gti_selftest_cmd *cmd);
#endif
};

/*
 * Helpers for the registration of chardev nodes
 */
int syna_cdev_create(struct syna_tcm *ptcm, struct platform_device *pdev);
void syna_cdev_remove(struct syna_tcm *ptcm);


ssize_t syna_get_fw_info(struct syna_tcm *tcm, char *buf, size_t buf_size);

#endif /* end of _SYNAPTICS_TCM2_DRIVER_H_ */

