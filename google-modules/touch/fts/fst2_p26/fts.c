/*
  * fts.c
  *
  * FTS Capacitive touch screen controller (FingerTipS)
  *
  * Copyright (C) 2016, STMicroelectronics Limited.
  * Authors: AMG(Analog Mems Group)
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version 2 as
  * published by the Free Software Foundation.
  *
  * THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
  * OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
  * PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
  * AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
  * INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM
  * THE
  * CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
  * INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
  */


/*!
  * \file fts.c
  * \brief It is the main file which contains all the most important functions
  * generally used by a device driver the driver
  */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/spi/spi.h>
#include <linux/version.h>

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
#include <drm/drm_panel.h>
#endif

#include "fts.h"
#include "fts_lib/fts_flash.h"
#include "fts_lib/fts_io.h"
#include "fts_lib/fts_test.h"
#include "fts_lib/fts_error.h"

extern struct sys_info system_info;
static int system_reseted_up;
static int system_reseted_down;
#ifdef CONFIG_PM
static const struct dev_pm_ops fts_pm_ops;
#endif

char fts_ts_phys[64];
extern struct test_to_do tests;
extern struct sys_info system_info;
extern int fifo_evt_size;

#define event_id(_e)		(EVT_ID_##_e >> 4)
#define handler_name(_h)	fts_##_h##_event_handler
#define install_handler(_i, _evt, _hnd) \
		(_i->event_dispatch_table[event_id(_evt)] = handler_name(_hnd))


#ifdef KERNEL_ABOVE_2_6_38
#define TYPE_B_PROTOCOL
#endif

/* Refer to 2.1.4 Status Event Summary */
static char *event_type_str[EVT_TYPE_STATUS_MAX_NUM] = {
	[EVT_TYPE_STATUS_ECHO] = "Echo",
	[EVT_TYPE_STATUS_GPIO_CHAR_DET] = "GPIO Charger Detect",
	[EVT_TYPE_STATUS_FRAME_DROP] = "Frame Drop",
	[EVT_TYPE_STATUS_FORCE_CAL] = "Force Cal",
	[EVT_TYPE_STATUS_WATER] = "Water Mode",
	[EVT_TYPE_STATUS_PRE_WATER] = "Pre-Water Mode",
	[EVT_TYPE_STATUS_NOISE] = "Noise Status",
	[EVT_TYPE_STATUS_PALM_TOUCH] = "Palm Status",
	[EVT_TYPE_STATUS_GRIP_TOUCH] = "Grip Status",
	[EVT_TYPE_STATUS_GOLDEN_RAW_ERR] = "Golden Raw Data Abnormal",
	[EVT_TYPE_STATUS_INV_GESTURE] = "Invalid Gesture",
	[EVT_TYPE_STATUS_HIGH_SENS] = "High Sensitivity Mode",
	[EVT_TYPE_STATUS_VSYNC] = "Vsync",
};

static void fts_pinctrl_setup(struct fts_ts_info *info, bool active);
static int goog_invert_array(int16_t *buf, int len);


/**
  * Set the value of system_reseted_up flag
  * @param val value to write in the flag
  */
static void set_system_reseted_up(int val)
{
	system_reseted_up = val;
}

/**
  * Set the value of system_reseted_down flag
  * @param val value to write in the flag
  */
static void set_system_reseted_down(int val)
{
	system_reseted_down = val;
}

/* Set the interrupt state
 * @param enable Indicates whether interrupts should enabled.
 * @return OK if success
 */
int fts_set_interrupt(struct fts_ts_info *info, bool enable)
{
	if (info->client == NULL) {
		dev_err(info->dev, "Error: Cannot get client irq.\n");
		return ERROR_OP_NOT_ALLOW;
	}

	if (enable == info->irq_enabled) {
		dev_dbg(info->dev, "Interrupt is already set (enable = %d).\n", enable);
		return OK;
	}

	if (enable && !info->resume_bit) {
		dev_err(info->dev, "Error: Interrupt can't enable in suspend mode.\n");
		return ERROR_OP_NOT_ALLOW;
	}

	mutex_lock(&info->fts_int_mutex);

	info->irq_enabled = enable;
	if (enable) {
		enable_irq(info->client->irq);
		dev_dbg(info->dev, "Interrupt enabled.\n");
	} else {
		disable_irq_nosync(info->client->irq);
		dev_dbg(info->dev, "Interrupt disabled.\n");
	}

	mutex_unlock(&info->fts_int_mutex);
	return OK;
}

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
/**
  * Release all the touches in the linux input subsystem
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  */
static void release_all_touches(struct fts_ts_info *info)
{
	unsigned int type = MT_TOOL_FINGER;
	int i;

	mutex_lock(&info->input_report_mutex);

	for (i = 0; i < TOUCH_ID_MAX + PEN_ID_MAX ; i++) {
		type = i < TOUCH_ID_MAX ? MT_TOOL_FINGER : MT_TOOL_PEN;
		input_mt_slot(info->input_dev, i);
		input_report_abs(info->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(info->input_dev, type, 0);
		input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, -1);
	}
	input_report_key(info->input_dev, BTN_TOUCH, 0);
	input_sync(info->input_dev);

	mutex_unlock(&info->input_report_mutex);

	info->touch_id = 0;
}
#endif

/**
  * The function handle the switching of the mode in the IC enabling/disabling
  * the sensing and the features set from the host
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param force if 1, the enabling/disabling command will be send even
  * if the feature was already enabled/disabled otherwise it will judge if
  * the feature changed status or the IC had a system reset
  * @return OK if success or an error code which specify the type of error
  *encountered
  */
int fts_mode_handler(struct fts_ts_info *info, int force)
{
	int res = OK;
	u8 data = 0;

	/* disable irq wake because resuming from gesture mode */
	if ((info->mode == SCAN_MODE_LOW_POWER) && (info->resume_bit == 1))
		disable_irq_wake(info->client->irq);

	info->mode = SCAN_MODE_HIBERNATE;
	pr_info("%s: Mode Handler starting...\n", __func__);
	switch (info->resume_bit) {
	case 0:	/* screen down */
		pr_info("%s: Screen OFF...\n", __func__);
		if (info->hdm_frame_enabled) {
			info->hdm_frame_mode = false;
			fts_set_fw_settings(FW_SETTINGS_HDM_FRAME_MODE, info->hdm_frame_mode);
		}
		/* do sense off in order to avoid the flooding of the fifo with
		 * touch events if someone is touching the panel during suspend
		 */
		data = SCAN_MODE_HIBERNATE;
		res = fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		if (res == OK)
			info->mode = SCAN_MODE_HIBERNATE;
		set_system_reseted_down(0);
		break;

	case 1:	/* screen up */
		pr_info("%s: Screen ON...\n", __func__);
		if (info->hdm_frame_enabled) {
			info->hdm_frame_mode = true;
			fts_set_fw_settings(FW_SETTINGS_HDM_FRAME_MODE, info->hdm_frame_mode);
		}
		data = SCAN_MODE_ACTIVE;
		res = fts_write_fw_reg(SCAN_MODE_ADDR, &data, 1);
		if (res == OK)
			info->mode = SCAN_MODE_ACTIVE;
		set_system_reseted_up(0);
		break;

	default:
		pr_err("%s: invalid resume_bit value = %d! ERROR %08X\n",
			 __func__, info->resume_bit, ERROR_OP_NOT_ALLOW);
		res = ERROR_OP_NOT_ALLOW;
	}
	/*TODO : For all the gesture related modes */

	pr_info("%s: Mode Handler finished! res = %08X mode = %08X\n",
		 __func__, res, info->mode);
	return res;
}

/**
  * The function dispatch events to the proper event handler according the event
  * ID.
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param data pointer of a byte array which contain the bytes to write.
  * @param data_size size of data in bytes
  * @param event_count total number of events
  * @return OK if success or an error code which specify the type of error
  *encountered
  */
int fts_dispatch_event(struct fts_ts_info *info, u8 *data, int data_size, int event_count)
{
	u8 *event = data;
	u8 event_id;
	unsigned char *evt_data;
	bool has_pointer_event = false;
	int count;
	int latest_frame_index = -1;

	if (data_size != (event_count * fifo_evt_size)) {
		pr_info("%s: event data size %d is not matched %d. ERROR %08X\n",
				__func__, data_size, event_count * fifo_evt_size, ERROR_OP_NOT_ALLOW);
		return ERROR_OP_NOT_ALLOW;
	}

	for (count = 0; count < event_count; count++) {
		evt_data = &event[count * fifo_evt_size];

		if (!info->checksum_force_disabled && info->checksum_enabled) {
			if (fts_validate_checksum(evt_data, fifo_evt_size) < 0)
				continue;
		}

		if (evt_data[0] == EVT_ID_NOEVENT)
			break;

		switch (evt_data[0]) {
		case EVT_ID_TOUCH_HEADER:
			latest_frame_index = count;
			has_pointer_event = true;
			break;
		default:
			break;
		}
	}

	if (has_pointer_event) {
		/* Report header event first */
		evt_data = &event[latest_frame_index * fifo_evt_size];
		event_id = evt_data[0] >> 4;
		info->event_dispatch_table[event_id](info, evt_data);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		goog_input_lock(info->gti);
		goog_input_set_timestamp(info->gti, info->input_dev, info->timestamp);
#else
		mutex_lock(&info->input_report_mutex);
		input_set_timestamp(info->input_dev, info->timestamp);
#endif
	}

	for (count = 0; count < event_count; count++) {
		evt_data = &event[count * fifo_evt_size];

		if (!info->checksum_force_disabled && info->checksum_enabled) {
			if (fts_validate_checksum(evt_data, fifo_evt_size) < 0)
				continue;
		}

		if (evt_data[0] == EVT_ID_NOEVENT)
			break;

		switch (evt_data[0]) {
		case EVT_ID_TOUCH_HEADER:
			continue;
		case EVT_ID_ENTER_POINT:
		case EVT_ID_MOTION_POINT:
			/* Drop old frames which have no matching heatmap */
			if (count < latest_frame_index)
				continue;
			break;
		case EVT_ID_LEAVE_POINT:
			/* Report up event to prevent ghost touch  */
			break;
		default:
			break;
		}
		event_id = evt_data[0] >> 4;
		/* Ensure event ID is within bounds */
		if (event_id < NUM_EVT_ID)
			info->event_dispatch_table[event_id](info, evt_data);
	}

	if (has_pointer_event) {
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		if (info->touch_id == 0)
			goog_input_report_key(info->gti, info->input_dev, BTN_TOUCH, 0);

		goog_input_sync(info->gti, info->input_dev);
		goog_input_unlock(info->gti);
#else
		if (info->touch_id == 0)
			input_report_key(info->input_dev, BTN_TOUCH, 0);

		input_sync(info->input_dev);
		mutex_unlock(&info->input_report_mutex);
#endif
	}

	return OK;
}

/**
  * Bottom Half Interrupt Handler function
  * This handler is called each time there is at least one new event in the FIFO
  * and the interrupt pin of the IC goes low. It will read all the events from
  * the FIFO and dispatch them to the proper event handler according the event
  * ID
  */
static irqreturn_t fts_event_handler(int irq, void *handle)
{
	struct fts_ts_info *info = (struct fts_ts_info *)handle;
	struct fts_hw_platform_data *bdata = info->board;
	int error = 0;
	u8 *data = NULL;
	int data_size = 0;
	bool free_data = false;
	int event_count = 0;
	struct frame_data* frame_data = &info->frame_data;
	int tx_count;
	int rx_count;
	int16_t *self_data;

	if (!info->irq_enabled)
		return IRQ_HANDLED;

	if (gpio_get_value(bdata->irq_gpio)) {
		pr_warn("%s: INT pin is high, skip IRQ", __func__);
		return IRQ_HANDLED;
	}

#ifdef DEBUG
	if (info->delay_msec_before_read_hdm_frame > 0) {
		if (info->delay_msec_before_read_hdm_frame > 1000) {
			msleep(info->delay_msec_before_read_hdm_frame);
			info->delay_msec_before_read_hdm_frame = 0;
		} else {
			mdelay(info->delay_msec_before_read_hdm_frame);
		}
	}
#endif

	if (info->hdm_frame_mode) {
		error = fts_read_hdm_frame_data();
		if (error != OK)
			goto exit;

		memcpy(info->vendor_register_data, &frame_data->header->gui_debug_info,
				sizeof(info->vendor_register_data));
		event_count = frame_data->header->event_count;
		if (event_count == 0)
			goto exit;
		data = frame_data->events;
		data_size = event_count * fifo_evt_size;

		self_data = (int16_t *)frame_data->self_data;
		tx_count = frame_data->header->tx_count;
		rx_count = frame_data->header->rx_count;
		if (info->board->sensor_inverted_x) {
			goog_invert_array(self_data, tx_count);
		}
		if (info->board->sensor_inverted_y) {
			goog_invert_array(self_data + tx_count, rx_count);
		}
	} else {
		error = fts_read_all_fw_fifo(&data, &data_size, &event_count);
		if (error != OK || event_count == 0)
			goto exit;
		free_data = true;
	}

	fts_dispatch_event(info, data, data_size, event_count);

exit:
	if (event_count == 0)
		pr_warn("%s: Error: unexpected INTB low with no events..\n", __func__);

	if (free_data && data != NULL) kfree(data);
	input_sync(info->input_dev);
	return IRQ_HANDLED;
}

/**
  * Top half Interrupt handler function
  * Respond to the interrupt and schedule the bottom half interrupt handler
  * in its work queue
  * @see fts_interrupt_handler()
  */
static irqreturn_t fts_interrupt_handler(int irq, void *handle)
{
	struct fts_ts_info *info = handle;
	info->timestamp = ktime_get();
	return IRQ_WAKE_THREAD;
}

/**
  * Event Handler for no events (EVT_ID_NOEVENT)
  */
static void fts_nop_event_handler(struct fts_ts_info *info,
					unsigned char *event)
{
	pr_info("%s: Doing nothing for event = %02X %02X %02X %02X %02X %02X %02X %02X\n",
		 __func__, event[0], event[1], event[2], event[3],
		 event[4], event[5], event[6], event[7]);
}

/**
  * Event handler for enter and motion events (EVT_ID_ENTER_POINT,
  * EVT_ID_MOTION_POINT )
  * report to the linux input system touches with their coordinated and
  * additional informations
  */
static void fts_enter_pointer_event_handler(struct fts_ts_info *info, unsigned
					    char *event)
{
	struct fts_hw_platform_data *bdata = info->board;
	unsigned char touch_id;
	unsigned int touch_condition = 1, tool = MT_TOOL_FINGER;
	int x, y, z, distance, major, minor;
	u8 touch_type;
	s8 orientation;

	if (!info->resume_bit)
		goto no_report;

	touch_type = event[1] & 0x0F;
	touch_id = (event[1] & 0xF0) >> 4;
	if (fifo_evt_size == FIFO_16_BYTES_EVENT_SIZE) {
		x = ((int)(event[3] << 8)) | (event[2]);
		y = ((int)(event[5] << 8)) | (event[4]);
		z = ((int)(event[7] << 8)) | (event[6]);
		minor = (int)(event[8]);
		major = (int)(event[9]);
		orientation = event[10];
	} else {
		x = (((int)event[3] & 0x0F) << 8) | (event[2]);
		y = ((int)event[4] << 4) | ((event[3] & 0xF0) >> 4);
		z = (int)(event[5]);
		minor = (int)(event[6]);
		major = (int)(event[7]);
		orientation = 0;
	}

	distance = 0;	/* if the tool is touching the display the distance
		* should be 0 */

	if (x == system_info.u16_scr_x_res)
		x--;

	if (y == system_info.u16_scr_y_res)
		y--;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_mt_slot(info->gti, info->input_dev, touch_id);
#else
	input_mt_slot(info->input_dev, touch_id);
#endif
	switch (touch_type) {
	/* TODO: customer can implement a different strategy for each kind of
	 * touch */
	case TOUCH_TYPE_FINGER:
	case TOUCH_TYPE_GLOVE:
	case TOUCH_TYPE_LARGE:
		pr_debug("%s: touch type = %d!\n", __func__, touch_type);
		tool = MT_TOOL_FINGER;
		__set_bit(touch_id, &info->touch_id);
		break;

	case TOUCH_TYPE_FINGER_HOVER:
		pr_debug("%s: touch type = %d!\n", __func__, touch_type);
		tool = MT_TOOL_FINGER;
		z = 0;	/* no pressure */
		__set_bit(touch_id, &info->touch_id);
		distance = DISTANCE_MAX;	/* check with fw report the
						 * hovering distance */
		break;

	default:
		pr_err("%s: Invalid touch type = %d! No Report...\n",
			  __func__, touch_type);
		goto no_report;
	}

	touch_condition = (info->touch_id > 0) ? 1 : 0;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_report_key(info->gti, info->input_dev, BTN_TOUCH, touch_condition);
	goog_input_mt_report_slot_state(info->gti, info->input_dev, tool, 1);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_POSITION_X, x);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_POSITION_Y, y);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_TOUCH_MAJOR,
		major * bdata->mm2px);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_TOUCH_MINOR,
		minor * bdata->mm2px);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_PRESSURE, z);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_DISTANCE, distance);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_ORIENTATION, orientation * 4096 / 90);
	goog_input_sync(info->gti, info->input_dev);
#else
	input_report_key(info->input_dev, BTN_TOUCH, touch_condition);
	input_mt_report_slot_state(info->input_dev, tool, 1);
	input_report_abs(info->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(info->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(info->input_dev, ABS_MT_TOUCH_MAJOR,
		major * bdata->mm2px);
	input_report_abs(info->input_dev, ABS_MT_TOUCH_MINOR,
		minor * bdata->mm2px);
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, z);
	input_report_abs(info->input_dev, ABS_MT_DISTANCE, distance);
	input_report_abs(info->input_dev, ABS_MT_ORIENTATION, orientation);
	input_sync(info->input_dev);
#endif
	pr_debug("%s:  Event 0x%02x - ID[%d], (x, y) = (%4d, %4d) Type = %d\n",
		__func__, *event, touch_id, x, y, touch_type);

no_report:
	return;
}

/**
  * Event handler for leave event (EVT_ID_LEAVE_POINT)
  * Report to the linux input system that one touch left the display
  */
static void fts_leave_pointer_event_handler(struct fts_ts_info *info, unsigned
					    char *event)
{
	unsigned char touch_id;
	unsigned int touch_condition = 1, tool = MT_TOOL_FINGER;
	u8 touch_type;

	touch_type = event[1] & 0x0F;
	touch_id = (event[1] & 0xF0) >> 4;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_mt_slot(info->gti, info->input_dev, touch_id);
#else
	input_mt_slot(info->input_dev, touch_id);
#endif
	switch (touch_type) {
	case TOUCH_TYPE_FINGER:
	case TOUCH_TYPE_GLOVE:
	case TOUCH_TYPE_LARGE:
	case TOUCH_TYPE_FINGER_HOVER:
		pr_debug("%s: touch type = %d!\n", __func__, touch_type);
		tool = MT_TOOL_FINGER;
		__clear_bit(touch_id, &info->touch_id);
		break;
	default:
		pr_err("%s: Invalid touch type = %d! No Report...\n",
			 __func__, touch_type);
		return;
	}

	touch_condition = (info->touch_id > 0) ? 1 : 0;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_PRESSURE, 0);
	goog_input_mt_report_slot_state(info->gti, info->input_dev, tool, 0);
	goog_input_report_abs(info->gti, info->input_dev, ABS_MT_TRACKING_ID, -1);
	goog_input_report_key(info->gti, info->input_dev, BTN_TOUCH, touch_condition);
	goog_input_sync(info->gti, info->input_dev);
#else
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, 0);
	input_mt_report_slot_state(info->input_dev, tool, 0);
	input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, -1);
	input_report_key(info->input_dev, BTN_TOUCH, touch_condition);
	input_sync(info->input_dev);
#endif
}

static void fts_touch_header_event_handler(struct fts_ts_info *info, unsigned
					    char *event)
{
	struct touch_header_event *header_event = (struct touch_header_event *)event;

	/**
	 * This is the first frame after touch is active. The delta time is unknown.
	 * Assume it is at least 100ms.
	 */
	if (header_event->delta_timestamp == 0)
		info->timestamp_sensing += 100000000;
	else
		info->timestamp_sensing += (header_event->timestamp - info->raw_timestamp_sensing) * 10000;
	info->raw_timestamp_sensing = header_event->timestamp;

	goog_input_set_sensing_timestamp(info->gti, info->input_dev, info->timestamp_sensing);

#ifdef DEBUG
	if (info->debug_timestamp)
		pr_info("%s: timestamp: %d delta: %d\n", __func__, header_event->timestamp,
			header_event->delta_timestamp);
#endif
}

/**
  * Perform a system reset of the IC.
  * If the reset pin is associated to a gpio, the function execute an hw reset
  * (toggling of reset pin) otherwise send an hw command to the IC
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param poll_event varaiable to enable polling for controller ready event
  * @return OK if success or an error code which specify the type of error
  */
int fts_system_reset(struct fts_ts_info *info, int poll_event)
{
	int res = 0;
	u8 data = SYSTEM_RESET_VAL;
#ifdef SPRUCE
	int add = 0x001C;
	uint8_t int_data = 0x01;
#endif

	if (info->board->reset_gpio == GPIO_NOT_DEFINED) {
		res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE, SYS_RST_ADDR,
			&data, 1);
		if (res < OK) {
			pr_err("%s: ERROR %08X\n", __func__, res);
			return res;
		}
	} else {
		gpio_set_value_cansleep(info->board->reset_gpio, 0);
		msleep(20);
		gpio_set_value_cansleep(info->board->reset_gpio, 1);
		res = OK;
	}

	if (info->hdm_frame_enabled) {
		info->hdm_frame_mode = false;
		info->frame_data.last_frame_id = 0xFF;
	}

	if (poll_event) {
		res = fts_poll_controller_ready_event();
		if (res < OK)
			pr_err("%s: ERROR %08X\n", __func__, res);
	} else
		msleep(100);

#ifdef SPRUCE
#ifdef FTS_GPIO6_UNUSED
	res = fts_write_read_u8ux(FTS_CMD_HW_REG_R, HW_ADDR_SIZE,
				  FLASH_CTRL_ADDR, &data, 1, DUMMY_BYTE);
	if (res < OK) {
		pr_err("%s: ERROR %08X\n", __func__, res);
		return res;
	}
	data |= 0x80;
	res = fts_write_u8ux(FTS_CMD_HW_REG_W, HW_ADDR_SIZE,
			     FLASH_CTRL_ADDR, &data, 1);
	if (res < OK) {
		pr_err("%s: ERROR %08X\n", __func__, res);
		return res;
	}
#endif

	res = fts_write_fw_reg(add, &int_data, 1);
	if (res < OK)
		pr_err("%s: ERROR %08X\n", __func__, res);
#endif

	return res;
}

#define fts_motion_pointer_event_handler fts_enter_pointer_event_handler
/*!< remap the motion event handler to the same function which handle the enter
 * event */

/**
  * Event handler for error events (EVT_ID_ERROR)
  * Handle unexpected error events implementing recovery strategy and
  * restoring the sensing status that the IC had before the error occured
  */
static void fts_error_event_handler(struct fts_ts_info *info, unsigned
				    char *event)
{
	int error = 0;

	pr_warn("%s: Received event %02X %02X %02X %02X %02X %02X %02X %02X\n",
		 __func__, event[0], event[1], event[2], event[3], event[4],
		 event[5], event[6], event[7]);

	switch (event[1]) {
	case EVT_TYPE_ERROR_HARD_FAULT:
	case EVT_TYPE_ERROR_MEMORY_MANAGE:
	case EVT_TYPE_ERROR_BUS_FAULT:
	case EVT_TYPE_ERROR_USAGE_FAULT:
	case EVT_TYPE_ERROR_WATCHDOG:
	case EVT_TYPE_ERROR_INIT_ERROR:
	case EVT_TYPE_ERROR_TASK_STACK_OVERFLOW:
	case EVT_TYPE_ERROR_MEMORY_OVERFLOW:
	{
		/* before reset clear all slots */
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		info->touch_id = 0;
#else
		release_all_touches(info);
#endif
		error = fts_system_reset(info, 0);
		if (error < OK)
			pr_err("%s: Cannot reset the device ERROR %08X\n",
				__func__, error);
	}
		break;
	case EVT_TYPE_ERROR_FW_CMD_CHECKSUM:
		pr_info("%s: FW cmd checksum failed = "
			"%02X %02X %02X %02X %02X %02X\n",
			__func__, event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;
	case EVT_TYPE_ERROR_FW_CMD_SPI_LEN_EXCEED:
		pr_info("%s: FW cmd spi length exceeded = "
			"%02X %02X %02X %02X %02X %02X\n",
			__func__, event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;
	}
}

void fts_restore_fw_settings(struct fts_ts_info *info)
{
	int error;

	info->water_mode_enabled = true;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (info->gti && !goog_check_late_sense_on_enabled(info->gti)) {
#endif
	error = fts_mode_handler(info, 0);
	if (error < OK)
		pr_err("%s: Cannot restore the device status ERROR %08X\n",
			__func__, error);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	}

	if (info->gti)
		goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_RESET,
			NULL);
#endif
}

/**
  * Event handler for controller ready event (EVT_ID_CONTROLLER_READY)
  * Handle controller events received after unexpected reset of the IC updating
  * the resets flag and restoring the proper sensing status
  */
static void fts_controller_ready_event_handler(struct fts_ts_info *info,
					       unsigned char *event)
{
	switch (event[1]) {
	case 0x00:
		pr_info("%s: Others reset = "
			"%02X %02X %02X %02X %02X %02X %02X %02X\n",
			__func__, event[0], event[1], event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;
	case 0x01:
		pr_info("%s: UVLO reset = "
			"%02X %02X %02X %02X %02X %02X %02X %02X\n",
			__func__, event[0], event[1], event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;
	case 0x02:
		pr_info("%s: Watchdog reset = "
			"%02X %02X %02X %02X %02X %02X %02X %02X\n",
			__func__, event[0], event[1], event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;
	default:
		pr_info("%s: Unknown reset = "
			"%02X %02X %02X %02X %02X %02X %02X %02X\n",
			__func__, event[0], event[1], event[2], event[3], event[4],
			event[5], event[6], event[7]);
		break;
	}
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	info->touch_id = 0;
#else
	release_all_touches(info);
#endif
	set_system_reseted_up(1);
	set_system_reseted_down(1);

	fts_restore_fw_settings(info);
}

#define log_status_event(force, evt_ptr) \
do { \
	u8 type = evt_ptr[1]; \
	if (force) \
		pr_info("%s: %s =" \
			" %02X %02X %02X %02X %02X %02X\n", \
			__func__, event_type_str[type], \
			evt_ptr[2], evt_ptr[3], evt_ptr[4], \
			evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
	else \
		pr_debug("%s: %s =" \
			" %02X %02X %02X %02X %02X %02X\n", \
			__func__, event_type_str[type], \
			evt_ptr[2], evt_ptr[3], evt_ptr[4], \
			evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
} while (0)

#define log_status_event2(force, sub_str, evt_ptr) \
do { \
	u8 type = evt_ptr[1]; \
	if (force) \
		pr_info("%s: %s - %s =" \
		" %02X %02X %02X %02X %02X %02X\n", \
		__func__, event_type_str[type], sub_str, \
		evt_ptr[2], evt_ptr[3], evt_ptr[4], \
		evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
	else \
		pr_debug("%s: %s - %s =" \
		" %02X %02X %02X %02X %02X %02X\n", \
		__func__, event_type_str[type], sub_str, \
		evt_ptr[2], evt_ptr[3], evt_ptr[4], \
		evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
} while (0)

#define log_status_event3(level, sub_str, evt_ptr) \
do { \
	u8 type = evt_ptr[1]; \
	switch (level) { \
		case 0: \
		pr_debug("%s: %s - %s =" \
		" %02X %02X %02X %02X %02X %02X\n", \
		__func__, event_type_str[type], sub_str, \
		evt_ptr[2], evt_ptr[3], evt_ptr[4], \
		evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
		break; \
		case 1: \
		pr_info("%s: %s - %s =" \
		" %02X %02X %02X %02X %02X %02X\n", \
		__func__, event_type_str[type], sub_str, \
		evt_ptr[2], evt_ptr[3], evt_ptr[4], \
		evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
		break; \
		case 2: \
		pr_warn("%s: %s - %s =" \
		" %02X %02X %02X %02X %02X %02X\n", \
		__func__, event_type_str[type], sub_str, \
		evt_ptr[2], evt_ptr[3], evt_ptr[4], \
		evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
		break; \
		default: \
		pr_err("%s: %s - %s =" \
		" %02X %02X %02X %02X %02X %02X\n", \
		__func__, event_type_str[type], sub_str, \
		evt_ptr[2], evt_ptr[3], evt_ptr[4], \
		evt_ptr[5], evt_ptr[6], evt_ptr[7]); \
		break; \
	} \
} while (0)

/**
  * Print Google debug info details.
  */
static int fts_print_goog_debug_info(struct fts_ts_info *info)
{
	struct goog_debug_info *goog_dbg_info = info->frame_data.goog_dbg_info;
	int res = OK;
	char temp[276] = { 0 };

	if (!goog_dbg_info) {
		res = ERROR_ALLOC;
		pr_err("%s: goog_debug_info is NULL. ERROR %08X\n", __func__, res);
		return res;
	}

	pr_info("%s: fpi_attempts_count: %d\n", __func__, goog_dbg_info->fpi_attempts_count);
	pr_info("%s: fpi_attempts_success_count: %d\n", __func__,
		goog_dbg_info->fpi_attempts_success_count);
	pr_info("%s: cfg_of_fpi: %d\n", __func__, goog_dbg_info->cfg_of_fpi);
	pr_info("%s: cfg_of_rom_ms_raw: %d\n", __func__, goog_dbg_info->cfg_of_rom_ms_raw);
	pr_info("%s: last_2nd_fpi_status: %d\n", __func__, goog_dbg_info->last_2nd_fpi_status);
	pr_info("%s: last_fpi_status: %d\n", __func__, goog_dbg_info->last_fpi_status);
	pr_info("%s: fpi_attempts_sign: %d\n", __func__, goog_dbg_info->fpi_attempts_sign);
	pr_info("%s: water_mode: %d\n", __func__, goog_dbg_info->water_mode);
	pr_info("%s: scan_mode: %d\n", __func__, goog_dbg_info->scan_mode);
	pr_info("%s: pre_touch_det: %d\n", __func__, goog_dbg_info->pre_touch_det);
	pr_info("%s: noise_level: %d\n", __func__, goog_dbg_info->noise_level);
	pr_info("%s: water_status: %d\n", __func__, goog_dbg_info->water_status);
	pr_info("%s: island_count: %d\n", __func__, goog_dbg_info->island_count);
	pr_info("%s: touch_count: %d\n", __func__, goog_dbg_info->touch_count);
	pr_info("%s: valid_touch_count: %d\n", __func__, goog_dbg_info->valid_touch_count);
	pr_info("%s: main_freq_band: %d\n", __func__, goog_dbg_info->main_freq_band);
	pr_info("%s: mm_std0: %d\n", __func__, goog_dbg_info->mm_std0);
	pr_info("%s: mm_std1: %d\n", __func__, goog_dbg_info->mm_std1);
	pr_info("%s: mm_std2: %d\n", __func__, goog_dbg_info->mm_std2);
	pr_info("%s: mm_std3: %d\n", __func__, goog_dbg_info->mm_std3);
	pr_info("%s: ml_diff_coeff1: %d\n", __func__, goog_dbg_info->ml_diff_coeff1);
	pr_info("%s: ml_diff_coeff2: %d\n", __func__, goog_dbg_info->ml_diff_coeff2);
	pr_info("%s: ml_str_coeff1: %d\n", __func__, goog_dbg_info->ml_str_coeff1);
	pr_info("%s: ml_str_coeff2: %d\n", __func__, goog_dbg_info->ml_str_coeff2);
	pr_info("%s: ml_status: %d\n", __func__, goog_dbg_info->ml_status);
	pr_info("%s: conti_rpt_en: %d\n", __func__, goog_dbg_info->conti_rpt_en);
	pr_info("%s: palm_rej_en: %d\n", __func__, goog_dbg_info->palm_rej_en);
	pr_info("%s: grip_rej_en: %d\n", __func__, goog_dbg_info->grip_rej_en);
	pr_info("%s: high_sens_mode_en: %d\n", __func__, goog_dbg_info->high_sens_mode_en);
	pr_info("%s: coor_fltr_en: %d\n", __func__, goog_dbg_info->coor_fltr_en);
	pr_info("%s: conti_rpt_state: %d\n", __func__, goog_dbg_info->conti_rpt_state);
	pr_info("%s: palm_rej_state: %d\n", __func__, goog_dbg_info->palm_rej_state);
	pr_info("%s: disp_mode_det: %d\n", __func__, goog_dbg_info->disp_mode_det);
	pr_info("%s: external_mode_det: %d\n", __func__, goog_dbg_info->external_mode_det);
	pr_info("%s: radiation_mode_det: %d\n", __func__, goog_dbg_info->radiation_mode_det);
	pr_info("%s: reserved2: %d\n", __func__, goog_dbg_info->reserved2);
	pr_info("%s: ms_mrn_std_dev0: %d\n", __func__, goog_dbg_info->ms_mrn_std_dev0);
	pr_info("%s: ms_mrn_std_dev1: %d\n", __func__, goog_dbg_info->ms_mrn_std_dev1);
	pr_info("%s: ms_mrn_std_dev2: %d\n", __func__, goog_dbg_info->ms_mrn_std_dev2);
	pr_info("%s: ms_mrn_std_dev3: %d\n", __func__, goog_dbg_info->ms_mrn_std_dev3);
	pr_info("%s: ms_mrn_diff_std_dev0: %d\n", __func__, goog_dbg_info->ms_mrn_diff_std_dev0);
	pr_info("%s: ms_mrn_diff_std_dev1: %d\n", __func__, goog_dbg_info->ms_mrn_diff_std_dev1);
	pr_info("%s: ms_mrn_diff_std_dev2: %d\n", __func__, goog_dbg_info->ms_mrn_diff_std_dev2);
	pr_info("%s: ms_mrn_diff_std_dev3: %d\n", __func__, goog_dbg_info->ms_mrn_diff_std_dev3);
	pr_info("%s: %s\n", __func__, print_hex("reserved3: ", goog_dbg_info->reserved3, 0x40,
		temp, sizeof(temp)));

	return res;
}

/**
  * Event handler for status events (EVT_ID_STATUS_UPDATE)
  * Handle status update events
  */
static void fts_status_event_handler(struct fts_ts_info *info, u8 *event)
{
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct gti_fw_status_data data = {0};
#endif

	switch (event[1]) {
	case EVT_TYPE_STATUS_ECHO:
		log_status_event(1, event);
		if (event[2] == 0x30 && event[3] == 0xF0 && info->goog_debug_info_log) {
			info->goog_debug_info_log = false;
			if (info->hdm_frame_mode)
				fts_print_goog_debug_info(info);
			else
				pr_err("%s: goog_debug_info is available in HDM frame mode only.\n", __func__);
		}
		break;

	case EVT_TYPE_STATUS_GPIO_CHAR_DET:
	case EVT_TYPE_STATUS_GOLDEN_RAW_ERR:
	case EVT_TYPE_STATUS_INV_GESTURE:
		log_status_event(1, event);
		break;
	case EVT_TYPE_STATUS_FRAME_DROP:
		log_status_event(1, event);
		pr_info("%s: strength: %d\n", __func__, (event[2] << 8) + event[3]);
		break;
	case EVT_TYPE_STATUS_FORCE_CAL:
	{
		u8 data = fifo_evt_size == FIFO_16_BYTES_EVENT_SIZE ? event[3] : event[2];
		switch (data) {
		case 0x01:
			log_status_event2(1, "sense on", event);
			break;

		case 0x02:
			log_status_event2(1, "host command", event);
			break;

		case 0x10:
			log_status_event2(1, "frame drop", event);
			break;

		case 0x11:
			log_status_event2(1, "pure raw", event);
			break;

		case 0x20:
			log_status_event2(1, "ss detect negative strength", event);
			break;

		case 0x30:
			log_status_event2(1, "invalid mutual", event);
			break;

		case 0x31:
			log_status_event2(1, "invalid self", event);
			break;

		case 0x32:
			log_status_event2(1, "invalid self islands", event);
			break;

		default:
			log_status_event2(1, "unknown event", event);
			break;
		}
	}
		break;

	case EVT_TYPE_STATUS_WATER:
	{
		u8 data = fifo_evt_size == FIFO_16_BYTES_EVENT_SIZE ? event[3] : event[2];
		if (data == 1) {
			log_status_event2(1, "entry", event);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_ENTER, NULL);
#endif
		} else {
			log_status_event2(1, "exit", event);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_WATER_EXIT, NULL);
#endif
		}
	}
		break;

	case EVT_TYPE_STATUS_PRE_WATER:
	case EVT_TYPE_STATUS_HIGH_SENS:
	{
		u8 data = fifo_evt_size == FIFO_16_BYTES_EVENT_SIZE ? event[3] : event[2];
		if (data == 1)
			log_status_event2(1, "entry", event);
		else
			log_status_event2(1, "exit", event);
	}
		break;

	case EVT_TYPE_STATUS_NOISE:
	{
		static u8 noise_level;
		static u8 scanning_frequency;
		static u16 std_dev_frequency;

		u16 new_std_dev_freq = (event[2] << 8) + event[3];
		u8 new_scanning_freq = event[4];
		u8 new_noise_level = (event[5] & 0xF0) >> 4;

		if (noise_level != new_noise_level ||
			scanning_frequency != new_scanning_freq ||
			std_dev_frequency != new_std_dev_freq) {
			log_status_event2(1, "changed", event);
			pr_info("%s: level:[%02X->%02X],freq:[%02X->%02X],"
				"std_dev:[%04X->%04X]\n", __func__, noise_level,
				new_noise_level, scanning_frequency, new_scanning_freq,
				std_dev_frequency, new_std_dev_freq);
			noise_level = new_noise_level;
			scanning_frequency = new_scanning_freq;
			std_dev_frequency = new_std_dev_freq;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			data.noise_level = noise_level;
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_NOISE_MODE, &data);
#endif
		} else
			log_status_event(0, event);
	}
		break;

	case EVT_TYPE_STATUS_PALM_TOUCH:
	{
		u8 data = fifo_evt_size == FIFO_16_BYTES_EVENT_SIZE ? event[3] : event[2];
		switch (data) {
		case 0x01:
			log_status_event2(0, "entry", event);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_PALM_ENTER,
				NULL);
#endif
			break;

		case 0x02:
			log_status_event2(0, "exit", event);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_PALM_EXIT,
				NULL);
#endif
			break;

		default:
			log_status_event2(1, "unknown event", event);
			break;
		}
	}
		break;

	case EVT_TYPE_STATUS_GRIP_TOUCH:
	{
		u8 grip_touch_status;
		u8 data = fifo_evt_size == FIFO_16_BYTES_EVENT_SIZE ? event[3] : event[2];

		grip_touch_status = (data & 0xF0) >> 4;
		switch (grip_touch_status) {
		case 0x01:
			log_status_event2(0, "entry", event);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_GRIP_ENTER,
				NULL);
#endif
			break;

		case 0x02:
			log_status_event2(0, "exit", event);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_notify_fw_status_changed(info->gti, GTI_FW_STATUS_GRIP_EXIT,
				NULL);
#endif
			break;

		default:
			log_status_event2(1, "unknown event", event);
			break;
		}
	}
		break;

	case EVT_TYPE_STATUS_VSYNC:
		switch (event[3]) {
		case 0x01:
			log_status_event3(1, "normal", event);
			break;

		case 0x02:
			log_status_event3(2, "no input", event);
			break;

		case 0x03:
			log_status_event3(2, "freq > fast thres", event);
			break;

		case 0x04:
			log_status_event3(2, "freq < slow thres", event);
			break;

		case 0x05:
			log_status_event3(2, "processing time > report rate", event);
			break;

		case 0x06:
			log_status_event3(2, "processing time < report rate", event);
			break;

		case 0x10:
			log_status_event3(2, "no need to trim osc", event);
			break;

		case 0x11:
			log_status_event3(2, "increase the freq of osc", event);
			break;

		case 0x12:
			log_status_event3(2, "decrease the freq of osc", event);
			break;

		case 0x13:
			log_status_event3(2, "vsync freq is out of spec", event);
			break;

		case 0x14:
			log_status_event3(2, "osc parameter reach limit", event);
			break;

		case 0x15:
			log_status_event3(2, "out of vsync adjust thres", event);
			break;

		default:
			log_status_event3(2, "unknown event", event);
			break;
		}
		break;

	default:
		pr_err("%s: Unknown status event (%02X) ="
			" %02X %02X %02X %02X %02X %02X\n",
			__func__, event[1], event[2], event[3],
			event[4], event[5], event[6], event[7]);
		break;
	}
}

/**
  * Event handler for enter and motion events (EVT_ID_ENTER_PEN,
  * EVT_ID_MOTION_PEN)
  * report to the linux input system pen touches with their coordinated and
  * additional informations
  */
static void fts_enter_pen_event_handler(struct fts_ts_info *info, unsigned
					    char *event)
{

	unsigned char pen_id;
	unsigned int touch_condition = 1, tool = MT_TOOL_PEN;
	int x, y, pressure, tilt_x, tilt_y;


	if (!info->resume_bit)
		goto no_report;

	if (fifo_evt_size == FIFO_16_BYTES_EVENT_SIZE) {
		pen_id = (event[1] & 0xF0) >> 4;
		x = ((int)(event[3] << 8)) | (event[2]);
		y = ((int)(event[5] << 8)) | (event[4]);
		pressure = (((int)event[7] & 0x0F) << 8) | (event[6]);
		tilt_x = (((int)event[9] & 0x0F) << 8) | (event[8]);
		tilt_y = ((int)event[10] << 4) | ((event[9] & 0xF0) >> 4);
	} else {
		pen_id = (event[0] & 0x0C) >> 2;
		x = (((int)event[2] & 0x0F) << 8) | (event[1]);
		y = ((int)event[3] << 4) | ((event[2] & 0xF0) >> 4);
		tilt_x = (int)(event[4]);
		tilt_y = (int)(event[5]);
		pressure = (((int)event[7] & 0x0F) << 8) | (event[6]);
	}
	pen_id = pen_id + TOUCH_ID_MAX;

	input_mt_slot(info->input_dev, pen_id);
	if (pressure > 0)
		__set_bit(pen_id, &info->touch_id);
	else
		__clear_bit(pen_id, &info->touch_id);

	touch_condition = (info->touch_id > 0) ? 1 : 0;
	input_report_key(info->input_dev, BTN_TOUCH, touch_condition);
	input_mt_report_slot_state(info->input_dev, tool, 1);
	input_report_abs(info->input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(info->input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(info->input_dev, ABS_TILT_X, tilt_x);
	input_report_abs(info->input_dev, ABS_TILT_Y, tilt_y);
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, pressure);
	input_sync(info->input_dev);
	pr_info("%s:  Event 0x%02x - ID[%d], Pos(x, y) = (%4d, %4d) "
		"Tilt(x, y) = (%4d, %4d)\n", __func__, *event, pen_id, x, y,
		tilt_x, tilt_y);

no_report:
	return;
}

#define fts_motion_pen_event_handler fts_enter_pen_event_handler
/*!< remap the pen motion event handler to the same function which handle the
 * enter event */


/**
  * Event handler for leave event (EVT_ID_LEAVE_PEN )
  * Report to the linux input system that pen touch left the display
  */
static void fts_leave_pen_event_handler(struct fts_ts_info *info, unsigned
					    char *event)
{

	unsigned char pen_id;
	unsigned int touch_condition = 1, tool = MT_TOOL_PEN;


	pen_id = (event[0] & 0x0C) >> 2;
	pen_id = pen_id + TOUCH_ID_MAX;


	input_mt_slot(info->input_dev, pen_id);
	__clear_bit(pen_id, &info->touch_id);

	touch_condition = (info->touch_id > 0) ? 1 : 0;
	input_report_abs(info->input_dev, ABS_MT_PRESSURE, 0);
	input_mt_report_slot_state(info->input_dev, tool, 0);
	input_report_abs(info->input_dev, ABS_MT_TRACKING_ID, -1);
	input_report_key(info->input_dev, BTN_TOUCH, touch_condition);
	input_sync(info->input_dev);
}

/**
  * Initialize the dispatch table with the event handlers for any possible event
  * ID
  * Set IRQ pin behavior (level triggered low)
  * Register top half interrupt handler function.
  * @see fts_interrupt_handler()
  */
static int fts_interrupt_install(struct fts_ts_info *info)
{
	int i, error = 0;

	info->event_dispatch_table = kzalloc(sizeof(event_dispatch_handler_t) *
					     NUM_EVT_ID, GFP_KERNEL);
	if (!info->event_dispatch_table) {
		pr_err("%s: OOM allocating event dispatch table\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < NUM_EVT_ID; i++)
		info->event_dispatch_table[i] = fts_nop_event_handler;

	install_handler(info, ENTER_POINT, enter_pointer);
	install_handler(info, LEAVE_POINT, leave_pointer);
	install_handler(info, MOTION_POINT, motion_pointer);
	install_handler(info, TOUCH_HEADER, touch_header);
	install_handler(info, ERROR, error);
	install_handler(info, CONTROLLER_READY, controller_ready);
	install_handler(info, STATUS_UPDATE, status);
	install_handler(info, ENTER_PEN, enter_pen);
	install_handler(info, LEAVE_PEN, leave_pen);
	install_handler(info, MOTION_PEN, motion_pen);

	/* disable interrupts in any case */
	error = fts_set_interrupt(info, false);
	if (error) return error;

	pr_info("%s: Interrupt Mode\n", __func__);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (goog_request_threaded_irq(info->gti, info->client->irq, fts_interrupt_handler,
#else
	if (request_threaded_irq(info->client->irq, fts_interrupt_handler,
#endif
		fts_event_handler, IRQF_ONESHOT | IRQF_TRIGGER_LOW,
		FTS_TS_DRV_NAME, info)) {
		pr_err("%s: Request irq failed\n", __func__);
		kfree(info->event_dispatch_table);
		error = -EBUSY;
	}
	info->irq_enabled = true;
	return error;
}

/**
  *	Clean the dispatch table and the free the IRQ.
  *	This function is called when the driver need to be removed
  */
static void fts_interrupt_uninstall(struct fts_ts_info *info)
{
	fts_set_interrupt(info, false);
	kfree(info->event_dispatch_table);
	free_irq(info->client->irq, info);
}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
static int gti_default_handler(void *private_data, enum gti_cmd_type cmd_type,
	struct gti_union_cmd_data *cmd)
{
	int res = 0;

	switch (cmd_type) {
	case GTI_CMD_NOTIFY_DISPLAY_STATE:
	case GTI_CMD_NOTIFY_DISPLAY_VREFRESH:
	case GTI_CMD_SET_SCREEN_PROTECTOR_MODE:
		res = -EOPNOTSUPP;
		break;

	case GTI_CMD_SET_HEATMAP_ENABLED:
		/* Heatmap is always enabled. */
		res = 0;
		break;

	case GTI_CMD_GET_CONTEXT_DRIVER:
	case GTI_CMD_GET_CONTEXT_STYLUS:
		/* There is no context from this driver. */
		res = 0;
		break;

	default:
		res = -ESRCH;
		break;

	}

	return res;
}

/**
  * Read a MS Frame from frame buffer memory
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param type type of MS frame to read
  * @return zero if success or an error code which specify the type of error
  */
static int goog_get_ms_frame(struct fts_ts_info *info, ms_frame_type_t type)
{
	u16 offset;
	int res = 0;

	if (!info->fw_ms_data) {
		return -ENOMEM;
	}

	switch (type) {
	case MS_RAW:
		offset = system_info.u16_ms_scr_raw_addr;
		break;
	case MS_STRENGTH:
		offset = system_info.u16_ms_scr_strength_addr;
		break;
	case MS_FILTER:
		offset = system_info.u16_ms_scr_filter_addr;
		break;
	case MS_BASELINE:
		offset = system_info.u16_ms_scr_baseline_addr;
		break;
	default:
		pr_err("%s: Invalid MS type %d\n",  __func__, type);
		return -EINVAL;
	}

	pr_debug("%s: type = %d Offset = 0x%04X\n", __func__, type, offset);

	res = get_frame_data(offset, info->mutual_data_size, info->fw_ms_data);
	if (res < OK) {
		pr_err("%s: error while reading sense data ERROR %08X\n",
			__func__, res);
		return -EIO;
	}

	/* if you want to access one node i,j,
	  * compute the offset like: offset = i*columns + j = > frame[i, j] */

	pr_debug("%s: Frame acquired!\n", __func__);
	return res;
	/* return the number of data put inside frame */

}

static int goog_invert_array(int16_t *buf, int len)
{
	int index_left;
	int index_right;
	int16_t sense_value;

	index_left = 0;
	index_right = len - 1;
	while (index_left < index_right) {
		sense_value = buf[index_left];
		buf[index_left] = buf[index_right];
		buf[index_right] = sense_value;
		index_left++;
		index_right--;
	}
	return 0;
}

/**
  * Read a SS Frame from frame buffer
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param type type of SS frame to read
  * @return zero if success or an error code which specify the type of error
  */
static int goog_get_ss_frame(struct fts_ts_info *info, ss_frame_type_t type)
{
	u16 self_force_offset = 0;
	u16 self_sense_offset = 0;
	int res = 0;
	int force_len, sense_len, tmp_force_len, tmp_sense_len;
	int16_t *tx_ptr;
	int16_t *rx_ptr;

	if (!info->self_data) {
		return -ENOMEM;
	}

	tmp_force_len = force_len = system_info.u8_scr_tx_len;
	tmp_sense_len = sense_len = system_info.u8_scr_rx_len;

	pr_info("%s: fts notifier begin!\n", __func__);

	if (force_len == 0x00 || sense_len == 0x00 ||
		force_len == 0xFF || sense_len == 0xFF) {
		pr_err("%s: number of channels not initialized\n", __func__);
		return -EINVAL;
	}

	switch (type) {
	case SS_RAW:
		self_force_offset = system_info.u16_ss_tch_tx_raw_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_raw_addr;
		break;
	case SS_FILTER:
		self_force_offset = system_info.u16_ss_tch_tx_filter_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_filter_addr;
		break;
	case SS_BASELINE:
		self_force_offset = system_info.u16_ss_tch_tx_baseline_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_baseline_addr;
		break;
	case SS_STRENGTH:
		self_force_offset = system_info.u16_ss_tch_tx_strength_addr;
		self_sense_offset = system_info.u16_ss_tch_rx_strength_addr;
		break;
	case SS_DETECT_RAW:
		self_force_offset = system_info.u16_ss_det_tx_raw_addr;
		self_sense_offset = system_info.u16_ss_det_rx_raw_addr;
		tmp_force_len = (self_force_offset == 0) ? 0 : force_len;
		tmp_sense_len = (self_sense_offset == 0) ? 0 : sense_len;
		break;
	case SS_DETECT_STRENGTH:
		self_force_offset = system_info.u16_ss_det_tx_strength_addr;
		self_sense_offset = system_info.u16_ss_det_rx_strength_addr;
		tmp_force_len = (self_force_offset == 0) ? 0 : force_len;
		tmp_sense_len = (self_sense_offset == 0) ? 0 : sense_len;
		break;
	case SS_DETECT_BASELINE:
		self_force_offset = system_info.u16_ss_det_tx_baseline_addr;
		self_sense_offset = system_info.u16_ss_det_rx_baseline_addr;
		tmp_force_len = (self_force_offset == 0) ? 0 : force_len;
		tmp_sense_len = (self_sense_offset == 0) ? 0 : sense_len;
		break;
	case SS_DETECT_FILTER:
		self_force_offset = system_info.u16_ss_det_tx_filter_addr;
		self_sense_offset = system_info.u16_ss_det_rx_filter_addr;
		tmp_force_len = (self_force_offset == 0) ? 0 : force_len;
		tmp_sense_len = (self_sense_offset == 0) ? 0 : sense_len;
		break;
	default:
		pr_err("%s: Invalid SS type = %d\n", __func__, type);
		return -EINVAL;
	}

	pr_debug("%s: type = %d Force_len = %d Sense_len = %d"
		" Offset_force = 0x%04X Offset_sense = 0x%04X\n",
		__func__, type, tmp_force_len, tmp_sense_len,
		self_force_offset, self_sense_offset);


	if (self_force_offset) {
		tx_ptr = info->self_data;
		res = get_frame_data(self_force_offset,
			tmp_force_len * BYTES_PER_NODE, tx_ptr);
		if (res < OK) {
			pr_err("%s: error while reading force data ERROR %08X\n",
				__func__, res);
			return -EIO;
		}
		if (info->board->sensor_inverted_x) {
			goog_invert_array(tx_ptr, tmp_force_len);
		}
	}

	if (self_sense_offset) {
		rx_ptr = &info->self_data[tmp_force_len];
		res = get_frame_data(self_sense_offset,
			tmp_sense_len * BYTES_PER_NODE, rx_ptr);
		if (res < OK) {
			pr_err("%s: error while reading sense data ERROR %08X\n",
				__func__, res);
			return -EIO;
		}
		if (info->board->sensor_inverted_y) {
			goog_invert_array(rx_ptr, tmp_sense_len);
		}
	}

	pr_debug("%s: Frame acquired!\n", __func__);
	return res;
}

static int get_fw_version(void *private_data, struct gti_fw_version_cmd *cmd)
{
	int cmd_buf_size = sizeof(cmd->buffer);
	ssize_t buf_idx = 0;

	pr_info("%s\n", __func__);
	buf_idx += scnprintf(cmd->buffer + buf_idx, cmd_buf_size - buf_idx,
		"\nREG Revision: 0x%04X\n", system_info.u16_reg_ver);
	buf_idx += scnprintf(cmd->buffer + buf_idx, cmd_buf_size - buf_idx,
		"FW Version: 0x%04X\n", system_info.u16_fw_ver);
	buf_idx += scnprintf(cmd->buffer + buf_idx, cmd_buf_size - buf_idx,
		"SVN Revision: 0x%04X\n", system_info.u16_svn_rev);
	buf_idx += scnprintf(cmd->buffer + buf_idx, cmd_buf_size - buf_idx,
		"Config Afe Ver: 0x%04X\n", system_info.u8_cfg_afe_ver);
	return 0;
}

static int get_mutual_sensor_data(void *private_data, struct gti_sensor_data_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int res = 0;
	uint32_t frame_index = 0;
	uint32_t x_val, y_val;
	uint16_t x, y;
	int max_x = system_info.u8_scr_tx_len;
	int max_y = system_info.u8_scr_rx_len;
	int cmd_type = 0;
	int16_t *fw_ms_data;
	int16_t buf[256];
	int buf_index = 0;


	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot get mutual sensor data because touch is off");
		return -EPERM;
	}

	cmd->buffer = NULL;
	cmd->size = 0;

	if (info->hdm_frame_enabled &&
			cmd->type == GTI_SENSOR_DATA_TYPE_MS) {
		fw_ms_data = (int16_t *)info->frame_data.mutual_data;
	} else {
		if (cmd->type == GTI_SENSOR_DATA_TYPE_MS ||
				cmd->type == GTI_SENSOR_DATA_TYPE_MS_DIFF)
			cmd_type = MS_STRENGTH;
		else if (cmd->type == GTI_SENSOR_DATA_TYPE_MS_BASELINE)
			cmd_type = MS_BASELINE;
		else if (cmd->type == GTI_SENSOR_DATA_TYPE_MS_RAW)
			cmd_type = MS_RAW;
		else {
			pr_err("%s: Invalid command type(0x%X).\n", __func__, cmd->type);
			return -EINVAL;
		}

		res = goog_get_ms_frame(info, cmd_type);
		if (res < 0) {
			pr_err("%s: failed with res=0x%08X.\n", __func__, res);
			return res;
		}
		fw_ms_data = info->fw_ms_data;
	}

	for (y = 0; y < max_y; y++) {
		int index = 0;
		int shift = 1;

		/* Rotate frame counter-clockwise and invert
		 * if necessary.
		 */
		if (info->board->sensor_inverted_y)
			y_val = (max_y - 1) - y;
		else
			y_val = y;

		if (info->board->sensor_inverted_x)
			x_val = max_x - 1;
		else
			x_val = 0;

		if (info->board->tx_rx_dir_swap) {
			index = y_val * max_x + x_val;
			shift = info->board->sensor_inverted_x ? -1 : 1;
		} else {
			index = x_val * max_y + y_val;
			shift = info->board->sensor_inverted_x ? -max_y : max_y;
		}


		for (x = 0; x < max_x; x++) {
			buf[buf_index++] = fw_ms_data[index];
			index += shift;

			if (buf_index == ARRAY_SIZE(buf)) {
				memcpy(info->mutual_data + frame_index, buf, sizeof(buf));
				frame_index += ARRAY_SIZE(buf);
				buf_index = 0;
			}
		}
	}

	if (buf_index != 0) {
		memcpy(info->mutual_data + frame_index, buf, buf_index * 2);
	}

	cmd->buffer = (u8 *)info->mutual_data;
	cmd->size = info->mutual_data_size;
	return res;
}

static int get_self_sensor_data(void *private_data, struct gti_sensor_data_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int res = 0;
	int cmd_type = 0;
	uint8_t *self_data;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot get self sensor data because touch is off");
		return -EPERM;
	}

	if (info->hdm_frame_enabled &&
			cmd->type == GTI_SENSOR_DATA_TYPE_SS) {
		self_data = info->frame_data.self_data;
	} else {
		if (cmd->type == GTI_SENSOR_DATA_TYPE_SS ||
				cmd->type == GTI_SENSOR_DATA_TYPE_SS_DIFF)
			cmd_type = SS_STRENGTH;
		else if (cmd->type == GTI_SENSOR_DATA_TYPE_SS_BASELINE)
			cmd_type = SS_BASELINE;
		else if (cmd->type == GTI_SENSOR_DATA_TYPE_SS_RAW)
			cmd_type = SS_RAW;
		else {
			pr_err("%s: Invalid command type(0x%X).\n", __func__, cmd->type);
			return -EINVAL;
		}

		res = goog_get_ss_frame(info, cmd_type);
		if (res < 0) {
			pr_err("%s: failed with res=0x%08X.\n", __func__, res);
			return res;
		}
		self_data = (u8 *)info->self_data;
	}

	cmd->buffer = self_data;
	cmd->size = info->self_data_size;
	return res;
}

static int selftest(void *private_data, struct gti_selftest_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	char *limits_file = info->test_limits_name;
	int ret;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot do self-test because touch is off");
		return -EPERM;
	}

	fts_set_interrupt(info, false);
	ret = fts_production_test_ito(limits_file, &tests);
	fts_set_interrupt(info, true);

	if (ret < OK)
		cmd->result = GTI_SELFTEST_RESULT_FAIL;
	else
		cmd->result = GTI_SELFTEST_RESULT_DONE;

	fts_system_reset(info, 0);

	return ret;
}

static int calibrate(void *private_data, struct gti_calibrate_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	struct force_update_flag force_update;
	bool irq_enabled = info->irq_enabled;
	bool hdm_frame_mode = info->hdm_frame_mode;
	int ret = 0;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot calibrate because touch is off");
		return -EPERM;
	}

	if (irq_enabled)
		fts_set_interrupt(info, false);

	if (hdm_frame_mode) {
		info->hdm_frame_mode = false;
		fts_set_fw_settings(FW_SETTINGS_HDM_FRAME_MODE, false);
	}

	force_update.panel_init = true;
	ret = full_panel_init(info, &force_update);

	if (irq_enabled)
		fts_set_interrupt(info, true);

	if (hdm_frame_mode) {
		info->hdm_frame_mode = true;
		fts_set_fw_settings(FW_SETTINGS_HDM_FRAME_MODE, true);
	}

	if (ret < OK)
		cmd->result = GTI_CALIBRATE_RESULT_FAIL;
	else
		cmd->result = GTI_CALIBRATE_RESULT_DONE;

	fts_system_reset(info, 0);

	return ret;
}

static int get_sensing_mode(void *private_data, struct gti_sensing_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int ret = 0;

	switch (info->mode) {
	case SCAN_MODE_HIBERNATE:
		cmd->setting = GTI_SENSING_MODE_DISABLE;
		break;
	case SCAN_MODE_ACTIVE:
		cmd->setting = GTI_SENSING_MODE_ENABLE;
		break;
	default:
		pr_err("Invalid scan mode %d", info->mode);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int set_sensing_mode(void *private_data, struct gti_sensing_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int ret = 0;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot set sensing mode because touch is off");
		return -EPERM;
	}

	if (cmd->setting == GTI_SENSING_MODE_DISABLE) {
		info->resume_bit = 0;
		fts_mode_handler(info, 0);
	} else if (cmd->setting == GTI_SENSING_MODE_ENABLE) {
		info->resume_bit = 1;
		fts_mode_handler(info, 0);
	} else {
		pr_err("Invalid sensing mode %d", cmd->setting);
		ret = -EINVAL;
	}
	return ret;
}

static int get_panel_id_from_tic(struct fts_ts_info *info)
{
	int chip_id_tic = system_info.u16_chip0_id;
	struct device_node *np = info->client->dev.of_node;
	const __be32 *prop;
	int array_size;
	int map_size;
	int chip_id_dt;
	int panel_id = -1;
	int index = 0;
	int err = 0;

	prop = of_get_property(np, "st,panel_map_from_tic", &array_size);
	if (prop == NULL) {
		pr_err("%s: Fail to find st,panel_map_from_tic", __func__);
		return panel_id;
	}
	map_size = array_size / 2;

	for (index = 0; index < map_size; index++) {
		err = of_property_read_u32_index(np, "st,panel_map_from_tic",
			index * 2, &chip_id_dt);
		if (err != 0) {
			panel_id = err;
			break;
		}

		if (chip_id_dt != chip_id_tic)
			continue;

		err = of_property_read_u32_index(np, "st,panel_map_from_tic",
			index * 2 + 1, &panel_id);
		if (err != 0)
			panel_id = err;
		else
			pr_info("%s: Find panel(id=%d), match chip_id=0x%02x",
				__func__, panel_id, chip_id_tic);
		break;
	}

	if (panel_id < 0)
		pr_err("%s: Fail to find matched panel, chip_id=0x%02x",
			__func__, chip_id_tic);

	return panel_id;
}

static int goog_get_panel_id_from_tic(void *private_data, struct gti_panel_id_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	cmd->setting = get_panel_id_from_tic(info);
	return 0;
}

static int set_continuous_report(
	void *private_data, struct gti_continuous_report_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot set continuous report because touch is off");
		return -EPERM;
	}

	return fts_set_fw_settings(FW_SETTINGS_CONTINUOUS_REPORT,
		cmd->setting == GTI_CONTINUOUS_REPORT_ENABLE);
}

static int set_grip_mode(void *private_data, struct gti_grip_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int err = 0;
	bool enabled = cmd->setting == GTI_GRIP_ENABLE;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot set grip mode because touch is off");
		return -EPERM;
	}

	err = fts_set_fw_settings(FW_SETTINGS_GRIP_MODE, enabled);
	if (err == 0)
		info->grip_enabled = enabled;
	return err;
}

static int get_grip_mode(void *private_data, struct gti_grip_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	cmd->setting = info->grip_enabled ? GTI_GRIP_ENABLE : GTI_GRIP_DISABLE;
	return 0;
}

static int set_palm_mode(void *private_data, struct gti_palm_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int err = 0;
	bool enabled = cmd->setting == GTI_PALM_ENABLE;
	u8 setting = enabled ? FW_PALM_MODE_TYPE1 : FW_PALM_MODE_DISABLE;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot set palm mode because touch is off");
		return -EPERM;
	}

	err = fts_set_fw_settings(FW_SETTINGS_PALM_MODE, setting);
	if (err == 0)
		info->palm_enabled = enabled;
	return err;
}

static int get_palm_mode(void *private_data, struct gti_palm_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	cmd->setting = info->palm_enabled ? GTI_PALM_ENABLE : GTI_PALM_DISABLE;
	return 0;
}

static int set_coord_filter_enabled(void *private_data,
	struct gti_coord_filter_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int err = 0;
	bool enabled = cmd->setting == GTI_COORD_FILTER_ENABLE;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot set coordinate filter because touch is off");
		return -EPERM;
	}

	err = fts_set_fw_settings(FW_SETTINGS_COORDINATE_FILTER, enabled);
	if (err == 0)
		info->coord_filter_enabled = enabled;
	return err;
}

static int get_coord_filter_enabled(void *private_data,
	struct gti_coord_filter_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	cmd->setting = info->coord_filter_enabled ? GTI_COORD_FILTER_ENABLE : GTI_COORD_FILTER_DISABLE;
	return 0;
}

static int get_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	cmd->setting = info->irq_enabled ? GTI_IRQ_MODE_ENABLE : GTI_IRQ_MODE_DISABLE;

	return 0;
}

static int set_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	return fts_set_interrupt(info, cmd->setting == GTI_IRQ_MODE_ENABLE);
}

static int set_reset(void *private_data, struct gti_reset_cmd *cmd)
{
	struct fts_ts_info *info = private_data;

	/* Reset then sense on. */
	if (cmd->setting == GTI_RESET_MODE_HW || cmd->setting == GTI_RESET_MODE_AUTO)
		return fts_system_reset(info, 0);
	else
		return -EOPNOTSUPP;
}

static int ping(void *private_data, struct gti_ping_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	u8 data[3] = { 0 };
	u16 chip_id;
	int ret = 0;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot ping TIC because touch is off");
		return -EPERM;
	}

	ret = fts_write_read_u8ux(FTS_CMD_HW_REG_R, HW_ADDR_SIZE,
		CHIP_ID_ADDRESS, data, 3, DUMMY_BYTE);
	if (ret < OK) {
		pr_err("%s: Bus Connection issue: %08X\n", __func__, ret);
		return ret;
	}

	chip_id = (u16)((data[0] << 8) + data[1]);
	if (chip_id != CHIP_ID) {
		pr_err("%s: Wrong Chip detected.. Expected|Detected: 0x%04X|0x%04X\n",
			__func__, CHIP_ID, chip_id);
		return -EINVAL;
	}

	return 0;
}

static int set_screen_protector_mode(void *private_data,
		struct gti_screen_protector_mode_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int err = 0;
	bool enabled = cmd->setting == GTI_SCREEN_PROTECTOR_MODE_ENABLE;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot set screen protector mode because touch is off");
		return -EPERM;
	}

	err = fts_set_fw_settings(FW_SETTINGS_HIGH_SENSITIVITY_MODE, enabled);
	if (err == 0)
		info->screen_protector_mode_enabled = enabled;
	return err;
}

static int get_screen_protector_mode(void *private_data,
		struct gti_screen_protector_mode_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	cmd->setting = info->screen_protector_mode_enabled ? GTI_SCREEN_PROTECTOR_MODE_ENABLE :
			GTI_SCREEN_PROTECTOR_MODE_DISABLE;
	return 0;
}

static int set_water_mode(void *private_data, struct gti_water_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	int err = 0;
	bool enabled = cmd->setting == GTI_WATER_ENABLE;

	if (goog_pm_wake_get_locks(info->gti) == 0) {
		pr_info("Connot set water mode because touch is off");
		return -EPERM;
	}

	err = fts_set_fw_settings(FW_SETTINGS_WATER_MODE, enabled);
	if (err == 0)
		info->water_mode_enabled = enabled;
	return err;
}

static int get_water_mode(void *private_data, struct gti_water_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	cmd->setting = info->water_mode_enabled ? GTI_WATER_ENABLE : GTI_WATER_DISABLE;
	return 0;
}

static int get_vendor_register(void *private_data, struct gti_vendor_register_cmd *cmd)
{
	struct fts_ts_info *info = private_data;
	size_t size = sizeof(info->vendor_register_data);

	memcpy(cmd->data, info->vendor_register_data, size);
	cmd->size = size;
	return 0;
}

#endif

#ifdef CONFIG_PM
/**
  * Resume function which perform a system reset, clean all the touches
  *from the linux input system and prepare the ground for enabling the sensing
  */
static void fts_resume(struct fts_ts_info *info)
{
	if (!info->sensor_sleep) return;
	pr_info("%s\n", __func__);

	pm_stay_awake(info->dev);
	fts_pinctrl_setup(info, true);
	info->resume_bit = 1;
	fts_system_reset(info, 0);
	fts_set_interrupt(info, true);
	info->sensor_sleep = false;
}

/**
  * Suspend function which clean all the touches from Linux input system
  *and prepare the ground to disabling the sensing or enter in gesture mode
  */
static void fts_suspend(struct fts_ts_info *info)
{
	if (info->sensor_sleep) return;
	pr_info("%s\n", __func__);

	info->sensor_sleep = true;
	fts_set_interrupt(info, false);
	info->resume_bit = 0;
	fts_mode_handler(info, 0);
	fts_pinctrl_setup(info, false);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	info->touch_id = 0;
#else
	release_all_touches(info);
#endif
	pm_relax(info->dev);
}
#endif

/**
  * Complete the boot up process, initializing the sensing of the IC according
  * to the current setting chosen by the host
  * Register the notifier for the suspend/resume actions and the event handler
  * @return OK if success or an error code which specify the type of error
  */
static int fts_init_sensing(struct fts_ts_info *info)
{
	int error = 0;
#ifdef SPRUCE
	int add = 0x001C;
	uint8_t int_data = 0x01;
	int res = 0;
#endif

	pr_info("%s: Sensing on and restore FW settings..\n", __func__);
	fts_restore_fw_settings(info);
	error |= fts_interrupt_install(info);

#ifdef SPRUCE
	res = fts_write_fw_reg(add, &int_data, 1);
	if (res < OK) {
		pr_err("%s: ERROR %08X\n", __func__, res);
	}
#endif

	if (error < OK)
		pr_err("%s: Init error (ERROR = %08X)\n", __func__, error);


	return error;
}

/**
  *	Implement the fw update and initialization flow of the IC that should be
  *executed at every boot up.
  *	The function perform a fw update of the IC in case of crc error or a new
  *fw version and then understand if the IC need to be re-initialized again.
  *	@return  OK if success or an error code which specify the type of error
  *	encountered
  */

static int fts_chip_init(struct fts_ts_info *info)
{
	int res = OK;
	int i = 0;
	struct force_update_flag force_burn;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct gti_optional_configuration *options;
#endif
	struct frame_data *frame_data = &info->frame_data;


	force_burn.code_update = 0;
	force_burn.panel_init = 0;
	for (; i < FLASH_MAX_SECTIONS; i++)
		force_burn.section_update[i] = 0;

	pr_info("%s: [1]: FW UPDATE..\n", __func__);
	res = flash_update(info, &force_burn);
	if (res != OK) {
		pr_err("%s: [1]: FW UPDATE FAILED.. res = %d\n", __func__, res);
		if (!(res & ERROR_FILE_NOT_FOUND))
			return res;
	}

	if (force_burn.panel_init) {
		pr_info("%s: [2]: MP TEST..\n", __func__);
		res = fts_production_test_main(info->test_limits_name,
			0, &tests, 0);
		if (res != OK)
			pr_err("%s: [2]: MP TEST FAILED.. res = %d\n",
				__func__, res);
	}

	input_set_abs_params(info->input_dev, ABS_MT_POSITION_X, X_AXIS_MIN,
			system_info.u16_scr_x_res-1, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_POSITION_Y, Y_AXIS_MIN,
			system_info.u16_scr_y_res-1, 0, 0);

	if (system_info.u8_scr_tx_len > 0 && system_info.u8_scr_rx_len > 0) {
		info->mutual_data_size =
			system_info.u8_scr_tx_len * system_info.u8_scr_rx_len *
			sizeof(int16_t);
		info->self_data_size =
			(system_info.u8_scr_tx_len + system_info.u8_scr_rx_len) *
			sizeof(int16_t);

		info->frame_data_size =
			sizeof(struct frame_data_header) +
			info->mutual_data_size +
			info->self_data_size + 2 +
			32 * fifo_evt_size +
			sizeof(struct goog_debug_info) +
			sizeof(struct frame_data_footer);
		info->frame_data_buff = (u8 *)devm_kzalloc(info->dev,
			spi_len_dma_align(info->frame_data_size), GFP_KERNEL);
		if (!info->frame_data_buff) {
			pr_err("%s: Failed to allocate frame_data_buff.\n", __func__);
				res = -ENOMEM;
			return res;
		}

		frame_data->header = (struct frame_data_header *)info->frame_data_buff;
		frame_data->mutual_data = info->frame_data_buff +
				sizeof(struct frame_data_header);
		frame_data->self_data = frame_data->mutual_data + info->mutual_data_size;
		frame_data->events = frame_data->self_data + info->self_data_size + 2;
		frame_data->goog_dbg_info = (struct goog_debug_info *)(
				frame_data->events + 32 * fifo_evt_size);
		frame_data->footer = (struct frame_data_footer *)(
				(u8 *)(frame_data->goog_dbg_info) + sizeof(struct goog_debug_info));
	} else {
		pr_err("%s: Incorrect system information ForceLen=%d SenseLen=%d.\n",
			__func__, system_info.u8_scr_tx_len, system_info.u8_scr_rx_len);
		res = -EIO;
		return res;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	info->mutual_data = (short *)devm_kzalloc(info->dev,
		info->mutual_data_size, GFP_KERNEL);
	if (!info->mutual_data) {
		pr_err("%s: Failed to allocate mutual_data.\n", __func__);
		res = -ENOMEM;
		return res;
	}

	info->self_data = devm_kzalloc(info->dev, info->self_data_size,
		GFP_KERNEL);
	if (!info->self_data) {
		pr_err("%s: Failed to allocate self data.\n", __func__);
		res = -ENOMEM;
		return res;
	}

	info->fw_ms_data = (short *)devm_kzalloc(info->dev,
		info->mutual_data_size, GFP_KERNEL);
	if (!info->fw_ms_data) {
		pr_err("%s: Failed to allocate fw mutual_data.\n", __func__);
		res = -ENOMEM;
		return res;
	}

	options = devm_kzalloc(info->dev, sizeof(struct gti_optional_configuration), GFP_KERNEL);
	if (!options) {
		pr_err("%s: GTI optional configuration kzalloc failed.\n",
			__func__);
		res = -ENOMEM;
		return res;
	}

	options->get_fw_version = get_fw_version;
	options->get_mutual_sensor_data = get_mutual_sensor_data;
	options->get_self_sensor_data = get_self_sensor_data;
	options->selftest = selftest;
	options->calibrate = calibrate;
	options->get_sensing_mode = get_sensing_mode;
	options->set_sensing_mode = set_sensing_mode;
	options->get_panel_id = goog_get_panel_id_from_tic;
	options->set_continuous_report = set_continuous_report;
	options->set_grip_mode = set_grip_mode;
	options->get_grip_mode = get_grip_mode;
	options->set_palm_mode = set_palm_mode;
	options->get_palm_mode = get_palm_mode;
	options->set_coord_filter_enabled = set_coord_filter_enabled;
	options->get_coord_filter_enabled = get_coord_filter_enabled;
	options->get_irq_mode = get_irq_mode;
	options->set_irq_mode = set_irq_mode;
	options->reset = set_reset;
	options->ping = ping;
	options->set_screen_protector_mode = set_screen_protector_mode;
	options->get_screen_protector_mode = get_screen_protector_mode;
	options->get_water_mode = get_water_mode;
	options->set_water_mode = set_water_mode;
	options->get_vendor_register = get_vendor_register;

	info->gti = goog_touch_interface_connect(info->dev, gti_default_handler, options);
	res = goog_pm_register_notification(info->gti, &fts_pm_ops);
	if (res < 0) {
		pr_err("%s: Failed to register gti pm", __func__);
		return res;
	}

	res = goog_input_register_device(info->dev, info->input_dev);
	if (res) {
		pr_err("%s: [3]: No such input device\n", __func__);
		res = -ENODEV;
		return res;
	}
#else
	res = input_register_device(info->input_dev);
	if (res) {
		pr_err("%s: [3]: No such input device\n", __func__);
		res = -ENODEV;
		return res;
	}
#endif

	pr_info("%s: [3]: TOUCH INIT..\n", __func__);
	res = fts_init_sensing(info);
	if (res != OK) {
		pr_err("%s: [3]: TOUCH INIT FAILED.. res = %d\n", __func__, res);
		return res;
	}

	return res;
}

#ifndef FW_UPDATE_ON_PROBE
/**
  *	Function called by the delayed workthread executed after the probe in
  * order to perform the fw update flow
  *	@see  fts_chip_init()
  */
static void flash_update_auto(struct work_struct *work)
{
	struct delayed_work *fwu_work = container_of(work, struct delayed_work,
						     work);
	struct fts_ts_info *info = container_of(fwu_work, struct fts_ts_info,
						fwu_work);
	fts_chip_init(info);

}
#endif

/**
  * This function try to attempt to communicate with the IC for the first time
  * during the boot up process in order to read the necessary info for the
  * following stages.
  * The function execute a system reset, read fundamental info (system info)
  * @return OK if success or an error code which specify the type of error
  */
static int fts_init(struct fts_ts_info *info)
{
	int res = 0;
	u8 data[3] = { 0 };
	u16 chip_id = 0;
	int retry_cnt = 0;
	u8 chip_rev = 0;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct device_node *np = info->client->dev.of_node;
	int panel_id = 0;
#endif

	open_channel(info->client);
	init_test_to_do();
#ifndef I2C_INTERFACE
#ifdef SPI4_WIRE
	pr_info("%s: Configuring SPI4..\n", __func__);
	res = configure_spi4();
	if (res < OK) {
		pr_err("%s: Error configuring IC in spi4 mode: %08X\n",
			__func__, res);
		return res;
	}
#endif
#endif
	do {
		res = fts_write_read_u8ux(FTS_CMD_HW_REG_R, HW_ADDR_SIZE,
			CHIP_ID_ADDRESS, data, 3, DUMMY_BYTE);
		if (res < OK) {
			pr_err("%s: Bus Connection issue: %08X\n", __func__, res);
			return res;
		}
		chip_id = (u16)((data[0] << 8) + data[1]);
		chip_rev = data[2];
		pr_info("%s: Chip id: 0x%04X rev:0x%02X, retry: %d\n", __func__, chip_id, chip_rev, retry_cnt);
		if (chip_id != CHIP_ID) {
			pr_err("%s: Wrong Chip detected.. Expected|Detected: 0x%04X|0x%04X\n",
				__func__, CHIP_ID, chip_id);
			if (retry_cnt >= MAX_PROBE_RETRY)
				return ERROR_WRONG_CHIP_ID;
		}

#if !defined(I2C_INTERFACE) && defined(ANGSANA)
		if (chip_rev < CHIP_REV_2_0)
			info->fw_data_length_cmd = 1;
#endif

		fifo_evt_size = FIFO_8_BYTES_EVENT_SIZE;

		res = fts_system_reset(info, 0);
		if (res < OK) {
			pr_err("%s: Bus Connection issue\n", __func__);
			return res;
		}
		retry_cnt++;
	} while (chip_id != CHIP_ID);

	res = read_sys_info(info);
	if (res < 0) {
		pr_info("%s: Could not read sys info.. No FW..\n", __func__);
		return OK;
	}

	/* Check firmware API version to be higher than 3.1 for
	 * touch and pen events to work */
	if ((system_info.u8_api_ver_major < 3) || ((system_info.u8_api_ver_major == 3) && (system_info.u8_api_ver_minor < 1))) {
	    pr_info("%s: WARNING!! Running FW is of older version. Update firmware with APIv3.1 or higher for touch and pen events to work.\n", __func__);
	}

	res = fts_poll_controller_ready_event();
	if (res < OK) {
		if (res == ERROR_BUS_W || res == ERROR_ALLOC) {
			pr_info("%s: Bus Connection or memory allocation issue\n", __func__);
			return res;
		}
		/*other errors are because of FW issues,
		so we continue to flash*/
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (of_property_read_bool(np, "goog,panel-map-from-tic")) {
		panel_id = get_panel_id_from_tic(info);
		if (panel_id < 0) {
			if (panel_id == -EOPNOTSUPP)
				panel_id = 0;
			else
				return -EPROBE_DEFER;
		}

		goog_get_firmware_name(np, panel_id, info->fw_name,
			sizeof(info->fw_name));
		goog_get_test_limits_name(np, panel_id, info->test_limits_name,
			sizeof(info->test_limits_name));
	}
#endif

	return OK;
}

/**
  * From the name of the power regulator get/put the actual regulator structs
  * (copying their references into fts_ts_info variable)
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param get if 1, the regulators are get otherwise they are put (released)
  * back to the system
  * @return OK if success or an error code which specify the type of error
  */
static int fts_get_reg(struct fts_ts_info *info, bool get)
{
	int ret_val;

	if (!get) {
		ret_val = 0;
		goto regulator_put;
	}

	if (of_property_read_bool(info->dev->of_node, "vdd-supply")) {
		info->vdd_reg = regulator_get(info->dev, "vdd");
		if (IS_ERR(info->vdd_reg)) {
			pr_err("%s: Failed to get power regulator\n", __func__);
			ret_val = -EPROBE_DEFER;
			goto regulator_put;
		}
	}

	if (of_property_read_bool(info->dev->of_node, "avdd-supply")) {
		info->avdd_reg = regulator_get(info->dev, "avdd");
		if (IS_ERR(info->avdd_reg)) {
			pr_err("%s: Failed to get bus pullup regulator\n",
				__func__);
			ret_val = -EPROBE_DEFER;
			goto regulator_put;
		}
	}

	return OK;

regulator_put:
	if (info->vdd_reg) {
		regulator_put(info->vdd_reg);
		info->vdd_reg = NULL;
	}

	if (info->avdd_reg) {
		regulator_put(info->avdd_reg);
		info->avdd_reg = NULL;
	}

	return ret_val;
}

/**
  * Enable or disable the power regulators
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param enable if 1, the power regulators are turned on otherwise they are
  * turned off
  * @return OK if success or an error code which specify the type of error
  */
static int fts_enable_reg(struct fts_ts_info *info, bool enable)
{
	int ret_val;

	if (!enable) {
		ret_val = 0;
		goto disable_pwr_reg;
	}

	if (info->vdd_reg) {
		ret_val = regulator_enable(info->vdd_reg);
		if (ret_val < 0) {
			pr_err("%s: Failed to enable bus regulator\n", __func__);
			goto exit;
		}
	}

	if (info->avdd_reg) {
		ret_val = regulator_enable(info->avdd_reg);
		if (ret_val < 0) {
			pr_err("%s: Failed to enable power regulator\n",
				__func__);
			goto disable_bus_reg;
		}
	}

	return OK;

disable_pwr_reg:
	if (info->avdd_reg)
		regulator_disable(info->avdd_reg);

disable_bus_reg:
	if (info->vdd_reg)
		regulator_disable(info->vdd_reg);

exit:
	return ret_val;
}

/**
  * Configure a GPIO according to the parameters
  * @param gpio gpio number
  * @param config if true, the gpio is set up otherwise it is free
  * @param dir direction of the gpio, 0 = in, 1 = out
  * @param state initial value (if the direction is in, this parameter is
  * ignored)
  * return error code
  */

static int fts_gpio_setup(int gpio, bool config, int dir, int state)
{
	int ret_val = 0;
	unsigned char buf[16];

	if (config) {
		scnprintf(buf, 16, "fts_gpio_%u\n", gpio);

		ret_val = gpio_request(gpio, buf);
		if (ret_val) {
			pr_err("%s: Failed to get gpio %d (code: %d)",
				__func__, gpio, ret_val);
			return ret_val;
		}

		if (dir == 0)
			ret_val = gpio_direction_input(gpio);
		else
			ret_val = gpio_direction_output(gpio, state);
		if (ret_val) {
			pr_err("%s: Failed to set gpio %d direction",
				__func__, gpio);
			return ret_val;
		}
	} else
		gpio_free(gpio);

	return ret_val;
}

/**
  * Setup the IRQ and RESET (if present) gpios.
  * If the Reset Gpio is present it will perform a cycle HIGH-LOW-HIGH in order
  *to assure that the IC has been reset properly
  */
static int fts_set_gpio(struct fts_ts_info *info)
{
	int ret_val;
	struct fts_hw_platform_data *bdata = info->board;

	ret_val = fts_gpio_setup(bdata->irq_gpio, true, 0, 0);
	if (ret_val < 0) {
		pr_err("%s: Failed to configure irq GPIO\n", __func__);
		goto err_gpio_irq;
	}

	if (bdata->reset_gpio >= 0) {
		ret_val = fts_gpio_setup(bdata->reset_gpio, true, 1, 0);
		if (ret_val < 0) {
			pr_err("%s: Failed to configure reset GPIO\n", __func__);
			goto err_gpio_reset;
		}
	}
	if (bdata->reset_gpio >= 0) {
		gpio_set_value_cansleep(bdata->reset_gpio, 0);
		msleep(20);
		gpio_set_value_cansleep(bdata->reset_gpio, 1);
	}

	return OK;

err_gpio_reset:
	fts_gpio_setup(bdata->irq_gpio, false, 0, 0);
	bdata->reset_gpio = GPIO_NOT_DEFINED;
err_gpio_irq:
	return ret_val;
}

/** Set pin state to active or suspend
  * @param active 1 for active while 0 for suspend
  */
static void fts_pinctrl_setup(struct fts_ts_info *info, bool active)
{
	int retval;

	if (info->ts_pinctrl) {
		/*
		 * Pinctrl setup is optional.
		 * If pinctrl is found, set pins to active/suspend state.
		 * Otherwise, go on without showing error messages.
		 */
		retval = pinctrl_select_state(info->ts_pinctrl, active ?
				info->pinctrl_state_active :
				info->pinctrl_state_suspend);
		if (retval < 0) {
			dev_err(info->dev, "Failed to select %s pinstate %d\n", active ?
				PINCTRL_STATE_ACTIVE : PINCTRL_STATE_SUSPEND,
				retval);
		}
	} else {
		dev_warn(info->dev, "ts_pinctrl is NULL\n");
	}
}

/**
  * Get/put the touch pinctrl from the specific names. If pinctrl is used, the
  * active and suspend pin control names and states are necessary.
  * @param info pointer to fts_ts_info which contains info about the device and
  * its hw setup
  * @param get if 1, the pinctrl is get otherwise it is put (released) back to
  * the system
  * @return OK if success or an error code which specify the type of error
  */
static int fts_pinctrl_get(struct fts_ts_info *info, bool get)
{
	int retval;

	if (!get) {
		retval = 0;
		goto pinctrl_put;
	}

	info->ts_pinctrl = devm_pinctrl_get(info->dev);
	if (IS_ERR_OR_NULL(info->ts_pinctrl)) {
		retval = PTR_ERR(info->ts_pinctrl);
		dev_info(info->dev, "Target does not use pinctrl %d\n", retval);
		goto err_pinctrl_get;
	}

	info->pinctrl_state_active
		= pinctrl_lookup_state(info->ts_pinctrl, PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(info->pinctrl_state_active)) {
		retval = PTR_ERR(info->pinctrl_state_active);
		dev_err(info->dev, "Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_ACTIVE, retval);
		goto err_pinctrl_lookup;
	}

	info->pinctrl_state_suspend
		= pinctrl_lookup_state(info->ts_pinctrl, PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(info->pinctrl_state_suspend)) {
		retval = PTR_ERR(info->pinctrl_state_suspend);
		dev_err(info->dev, "Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_SUSPEND, retval);
		goto err_pinctrl_lookup;
	}

	info->pinctrl_state_release
		= pinctrl_lookup_state(info->ts_pinctrl, PINCTRL_STATE_RELEASE);
	if (IS_ERR_OR_NULL(info->pinctrl_state_release)) {
		retval = PTR_ERR(info->pinctrl_state_release);
		dev_warn(info->dev, "Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_RELEASE, retval);
	}

	return OK;

err_pinctrl_lookup:
	devm_pinctrl_put(info->ts_pinctrl);
err_pinctrl_get:
	info->ts_pinctrl = NULL;
pinctrl_put:
	if (info->ts_pinctrl) {
		if (IS_ERR_OR_NULL(info->pinctrl_state_release)) {
			devm_pinctrl_put(info->ts_pinctrl);
			info->ts_pinctrl = NULL;
		} else {
			if (pinctrl_select_state(
					info->ts_pinctrl,
					info->pinctrl_state_release))
				dev_warn(info->dev, "Failed to select release pinstate\n");
		}
	}
	return retval;
}



/**
  * Retrieve and parse the hw information from the device tree node defined in
  * the system.
  * the most important information to obtain are: IRQ and RESET gpio numbers,
  * power regulator names
  * In the device file node is possible to define additional optional
  *information that can be parsed here.
  */
static int parse_dt(struct device *dev, struct fts_ts_info *info)
{
	struct fts_hw_platform_data *bdata = info->board;
	struct device_node *np = dev->of_node;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	int panel_id = 0;
#else
	int retval;
	int index;
	struct of_phandle_args panelmap;
	struct drm_panel *panel = NULL;
#endif

	strscpy(info->fw_name, PATH_FILE_FW, sizeof(info->fw_name));
	strscpy(info->test_limits_name, LIMITS_FILE, sizeof(info->test_limits_name));

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (of_property_read_bool(np, "goog,panel_map")) {
		panel_id = goog_get_panel_id(np);
		if (panel_id < 0) {
			if (panel_id == -EOPNOTSUPP)
				panel_id = 0;
			else
				return -EPROBE_DEFER;
		}

		goog_get_firmware_name(np, panel_id, info->fw_name,
			sizeof(info->fw_name));
		goog_get_test_limits_name(np, panel_id, info->test_limits_name,
			sizeof(info->test_limits_name));
	}
#else
	if (of_property_read_bool(np, "st,panel_map")) {
		for (index = 0 ;; index++) {
			retval = of_parse_phandle_with_fixed_args(np,
								  "st,panel_map",
								  1,
								  index,
								  &panelmap);
			if (retval)
				return -EPROBE_DEFER;
			panel = of_drm_find_panel(panelmap.np);
			of_node_put(panelmap.np);
			if (!IS_ERR_OR_NULL(panel)) {
				bdata->panel = panel;
				break;
			}
		}
	}
#endif

	bdata->irq_gpio = of_get_named_gpio(np, "st,irq-gpio", 0);

	pr_info("%s: irq_gpio = %d\n", __func__, bdata->irq_gpio);

	if (of_property_read_bool(np, "st,reset-gpio")) {
		bdata->reset_gpio = of_get_named_gpio(np, "st,reset-gpio", 0);
		pr_info("%s: reset_gpio = %d\n", __func__, bdata->reset_gpio);
	} else
		bdata->reset_gpio = GPIO_NOT_DEFINED;

	if (of_property_read_u8(np, "st,mm2px", &bdata->mm2px)) {
		pr_err("%s: Unable to get mm2px, please check dts", __func__);
		bdata->mm2px = 1;
	} else {
		pr_info("%s: mm2px = %d", __func__, bdata->mm2px);
	}

	bdata->sensor_inverted_x = 0;
	if (of_property_read_bool(np, "st,sensor_inverted_x"))
		bdata->sensor_inverted_x = 1;
	dev_info(dev, "Sensor inverted x = %u\n", bdata->sensor_inverted_x);

	bdata->sensor_inverted_y = 0;
	if (of_property_read_bool(np, "st,sensor_inverted_y"))
		bdata->sensor_inverted_y = 1;
	dev_info(dev, "Sensor inverted y = %u\n", bdata->sensor_inverted_y);

	bdata->tx_rx_dir_swap = 0;
	if (of_property_read_bool(np, "st,tx_rx_dir_swap"))
		bdata->tx_rx_dir_swap = 1;
	dev_info(dev, "tx_rx_dir_swap = %u\n",
		bdata->tx_rx_dir_swap);

	info->hdm_frame_enabled = false;
	if (of_property_read_bool(np, "st,hdm_frame_enabled"))
		info->hdm_frame_enabled = true;

	return OK;
}

/**
  * Probe function, called when the driver it is matched with a device with the
  *same name compatible name
  * This function allocate, initialize and define all the most important
  *function and flow that are used by the driver to operate with the IC.
  * It allocates device variables, initialize queues and schedule works,
  *registers the IRQ handler, suspend/resume callbacks, registers the device to
  *the linux input subsystem etc.
  */
#ifdef I2C_INTERFACE
static int fts_probe(struct i2c_client *client, const struct i2c_device_id
						*idp)
{
#else
static int fts_probe(struct spi_device *client)
{
#endif

	struct fts_ts_info *info = NULL;
	struct fts_hw_platform_data *bdata = NULL;
	int error = 0;
	struct device_node *dp = client->dev.of_node;
	int ret_val;
	u16 bus_type;

	pr_info("%s: driver probe begin!\n", __func__);
	pr_info("%s: driver ver. %s\n", __func__, FTS_TS_DRV_VERSION);

	info = kzalloc(sizeof(struct fts_ts_info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev, "Out of memory... Impossible to allocate struct info!\n");
		error = -ENOMEM;
		goto probe_error_exit_0;
	}

	pr_info("%s: SET Bus Functionality :\n", __func__);
#ifdef I2C_INTERFACE
	pr_info("%s: I2C interface...\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_info("%s: Unsupported I2C functionality\n", __func__);
		error = -EIO;
		goto probe_error_exit_1;
	}

	pr_info("%s: I2C address: %x\n", __func__, client->addr);
	bus_type = BUS_I2C;
#else
	pr_info("%s: SPI interface...\n", __func__);
	client->mode = SPI_MODE_0;
#ifndef SPI4_WIRE
	client->mode |= SPI_3WIRE;
#endif
	client->bits_per_word = 8;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
	client->rt = true;
#endif
	if (spi_setup(client) < 0) {
		pr_info("%s: Unsupported SPI functionality\n", __func__);
		error = -EIO;
		goto probe_error_exit_1;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	info->dma_mode = goog_check_spi_dma_enabled(client);
#endif
	pr_info("%s: SPI interface: dma_mode %d.\n", __func__, info->dma_mode);
	bus_type = BUS_SPI;
#endif

	pr_info("%s: SET Device driver INFO:\n", __func__);

	info->client = client;
	info->dev = &info->client->dev;
	dev_set_drvdata(info->dev, info);

	if (dp) {
		info->board = devm_kzalloc(&client->dev,
					   sizeof(struct fts_hw_platform_data),
					   GFP_KERNEL);
		if (!info->board) {
			pr_err("%s: ERROR:info.board kzalloc failed\n",
				 __func__);
			goto probe_error_exit_1;
		}
		parse_dt(&client->dev, info);
		bdata = info->board;
	}

	pr_info("%s: SET Regulators:\n", __func__);
	error = fts_get_reg(info, true);
	if (error < 0) {
		pr_err("%s: ERROR:Failed to get regulators\n",
			 __func__);
		goto probe_error_exit_1;
	}

	ret_val = fts_enable_reg(info, true);
	if (ret_val < 0) {
		pr_err("%s: ERROR Failed to enable regulators\n",
			 __func__);
		goto probe_error_exit_2;
	}

	pr_info("%s: SET GPIOS_Test:\n", __func__);
	ret_val = fts_set_gpio(info);
	if (ret_val < 0) {
		pr_err("%s: ERROR Failed to set up GPIO's\n",
			 __func__);
		goto probe_error_exit_2;
	}
	info->client->irq = gpio_to_irq(info->board->irq_gpio);
	info->dev = &info->client->dev;

	mutex_init(&info->mutex_read_write);
	mutex_init(&info->mutex_read_write_buf);

	pr_info("%s: SET Pinctrl:\n", __func__);
	ret_val = fts_pinctrl_get(info, true);
	if (!ret_val)
		fts_pinctrl_setup(info, true);

	mutex_init(&info->fts_int_mutex);

	pr_info("%s: SET Input Device Property:\n", __func__);
	info->input_dev = input_allocate_device();
	if (!info->input_dev) {
		pr_err("%s: ERROR: No such input device defined!\n", __func__);
		error = -ENODEV;
		goto probe_error_exit_3;
	}
	info->input_dev->dev.parent = &client->dev;
	info->input_dev->name = FTS_TS_DRV_NAME;
	scnprintf(fts_ts_phys, sizeof(fts_ts_phys), "%s/input0",
		 info->input_dev->name);
	info->input_dev->phys = fts_ts_phys;
	info->input_dev->uniq = "google_touchscreen";
	info->input_dev->id.bustype = bus_type;
	info->input_dev->id.vendor = 0x0001;
	info->input_dev->id.product = 0x0002;
	info->input_dev->id.version = 0x0100;

	__set_bit(EV_SYN, info->input_dev->evbit);
	__set_bit(EV_KEY, info->input_dev->evbit);
	__set_bit(EV_ABS, info->input_dev->evbit);
	__set_bit(BTN_TOUCH, info->input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, info->input_dev->propbit);

	input_mt_init_slots(info->input_dev, TOUCH_ID_MAX + PEN_ID_MAX,
		INPUT_MT_DIRECT);
	input_set_abs_params(info->input_dev, ABS_MT_POSITION_X, X_AXIS_MIN,
						X_AXIS_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_POSITION_Y, Y_AXIS_MIN,
						Y_AXIS_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MAJOR,
		ABS_MAJOR_MIN(bdata->mm2px), ABS_MAJOR_MAX(bdata->mm2px), 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_TOUCH_MINOR,
		ABS_MINOR_MIN(bdata->mm2px), ABS_MINOR_MAX(bdata->mm2px), 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_PRESSURE, PRESSURE_MIN,
						PRESSURE_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_DISTANCE, DISTANCE_MIN,
						DISTANCE_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_ORIENTATION, ORIENTATION_MIN,
						ORIENTATION_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_TILT_X, DISTANCE_MIN,
						DISTANCE_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_TILT_Y, DISTANCE_MIN,
						DISTANCE_MAX, 0, 0);
	input_set_abs_params(info->input_dev, ABS_MT_TOOL_TYPE, 0,
						MT_TOOL_MAX, 0, 0);
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	mutex_init(&(info->input_report_mutex));
#endif

	info->resume_bit = 1;
	ret_val = fts_init(info);
	if (ret_val < OK) {
		pr_err("%s: Initialization fails.. exiting..\n", __func__);
		if (ret_val == ERROR_WRONG_CHIP_ID)
			error = -EPROBE_DEFER;
		else
			error = -EIO;
		goto probe_error_exit_4;
	}

	ret_val = fts_proc_init(info);
	if (ret_val < OK)
		pr_err("%s: Cannot create /proc filenode..\n", __func__);

#if defined(FW_UPDATE_ON_PROBE) && defined(FW_H_FILE)
	ret_val = fts_chip_init(info);
	if (ret_val < OK) {
		pr_err("%s: Flashing FW/Production Test/Touch Init Failed..\n",
			__func__);
		goto probe_error_exit_4;
	}
#else
	pr_info("%s: SET Auto Fw Update:\n", __func__);
	info->fwu_workqueue = alloc_workqueue("fts-fwu-queue", WQ_UNBOUND |
					      WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!info->fwu_workqueue) {
		pr_err("%s: ERROR: Cannot create fwu work thread\n", __func__);
		goto probe_error_exit_4;
	}
	INIT_DELAYED_WORK(&info->fwu_work, flash_update_auto);
#endif
#ifndef FW_UPDATE_ON_PROBE
	queue_delayed_work(info->fwu_workqueue, &info->fwu_work,
			   msecs_to_jiffies(1000));
#endif

	pr_info("%s: Probe Finished!\n", __func__);
	return OK;

probe_error_exit_4:
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	mutex_destroy(&info->input_report_mutex);
#endif

probe_error_exit_3:
	mutex_destroy(&info->mutex_read_write);
	mutex_destroy(&info->mutex_read_write_buf);
	mutex_destroy(&info->fts_int_mutex);
	gpio_free(info->board->reset_gpio);
	gpio_free(info->board->irq_gpio);

probe_error_exit_2:
	fts_enable_reg(info, false);
	fts_get_reg(info, false);

probe_error_exit_1:
	kfree(info);

probe_error_exit_0:
	pr_err("%s: Probe Failed!\n", __func__);

	return error;
}

/**
  * Clear and free all the resources associated to the driver.
  * This function is called when the driver need to be removed.
  */
#ifdef I2C_INTERFACE
static void fts_remove(struct i2c_client *client)
{
#else
static void fts_remove(struct spi_device *client)
{
#endif
	struct fts_ts_info *info = dev_get_drvdata(&(client->dev));

	fts_proc_remove();
	fts_interrupt_uninstall(info);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_unregister_device(info->dev, info->input_dev);
#else
	input_unregister_device(info->input_dev);
	mutex_destroy(&info->input_report_mutex);
#endif
	mutex_destroy(&info->mutex_read_write);
	mutex_destroy(&info->mutex_read_write_buf);
	mutex_destroy(&info->fts_int_mutex);
#ifndef FW_UPDATE_ON_PROBE
	destroy_workqueue(info->fwu_workqueue);
#endif
	gpio_free(info->board->reset_gpio);
	gpio_free(info->board->irq_gpio);
	fts_enable_reg(info, false);
	fts_get_reg(info, false);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_pm_unregister_notification(info->gti);
	goog_touch_interface_disconnect(info->dev);
	kfree(info->mutual_data);
	kfree(info->self_data);
	kfree(info->fw_ms_data);
#endif
	kfree(info);
}

#ifdef CONFIG_PM
static int fts_pm_suspend(struct device *dev)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	fts_suspend(info);
	return 0;
}

static int fts_pm_resume(struct device *dev)
{
	struct fts_ts_info *info = dev_get_drvdata(dev);
	fts_resume(info);
	return 0;
}

static SIMPLE_DEV_PM_OPS(fts_pm_ops, fts_pm_suspend, fts_pm_resume);
#endif

static struct of_device_id fts_of_match_table[] = {
	{
		.compatible = "st,fst2",
	},
	{},
};
MODULE_DEVICE_TABLE(of, fts_of_match_table);

#ifdef I2C_INTERFACE
static const struct i2c_device_id fts_device_id[] = {
	{ FTS_TS_DRV_NAME, 0 },
	{}
};

static struct i2c_driver fts_i2c_driver = {
	.driver			= {
		.name		= FTS_TS_DRV_NAME,
		.of_match_table = fts_of_match_table,
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm		= &fts_pm_ops,
#endif
	},
	.probe			= fts_probe,
	.remove			= fts_remove,
	.id_table		= fts_device_id,
};
#else
static struct spi_driver fts_spi_driver = {
	.driver			= {
		.name		= FTS_TS_DRV_NAME,
		.of_match_table = fts_of_match_table,
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm		= &fts_pm_ops,
#endif
		.owner		= THIS_MODULE,
	},
	.probe			= fts_probe,
	.remove			= fts_remove,
};

#endif

static int __init fts_driver_init(void)
{
#ifdef I2C_INTERFACE
	return i2c_add_driver(&fts_i2c_driver);
#else
	return spi_register_driver(&fts_spi_driver);
#endif
}

static void __exit fts_driver_exit(void)
{
#ifdef I2C_INTERFACE
		i2c_del_driver(&fts_i2c_driver);
#else
		spi_unregister_driver(&fts_spi_driver);
#endif
}


MODULE_DESCRIPTION("STMicroelectronics MultiTouch IC Driver");
MODULE_AUTHOR("STMicroelectronics");
MODULE_LICENSE("GPL");

late_initcall(fts_driver_init);
module_exit(fts_driver_exit);
