// SPDX-License-Identifier: GPL-2.0
/*
 * Touch Bus Negotiator for Google Pixel devices.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/net.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include "touch_bus_negotiator.h"
#include <uapi/linux/sched/types.h>
#include "aoc_tbn_service_dev.h"

#define TBN_MODULE_NAME "touch_bus_negotiator"
#define TBN_AOC_CHANNEL_THREAD_NAME "tbn_aoc_channel"
#define TBN_DATA_BUFFER_MAX 64

#undef pr_fmt
#define pr_fmt(fmt) "gti: tbn: " fmt
#undef dev_fmt
#define dev_fmt(fmt) "gti: " fmt

static void handle_tbn_event_response(struct tbn_context *tbn,
	struct TbnEventResponse *response);
static RAW_NOTIFIER_HEAD(tbn_event_notifier);
static void tbn_notify_invalid_gesture(void *event);
static void tbn_notify_invalid_gesture_counts(void *event);
static void tbn_notify_device_switch_failure(void *event);
static int tbn_handshaking(struct tbn_context *tbn, enum TbnOperation operation);
static void tbn_remove(struct platform_device *pdev);

static struct tbn_context *tbn_context;

static irqreturn_t tbn_aoc2ap_irq_thread(int irq, void *ptr)
{
	struct tbn_context *tbn = ptr;

	pr_info("%s: bus_released:%d bus_requested:%d.\n", __func__,
		completion_done(&tbn->bus_released), completion_done(&tbn->bus_requested));

	if (completion_done(&tbn->bus_released) && completion_done(&tbn->bus_requested))
		return IRQ_HANDLED;

	/*
	 * For bus release, there two possibilities:
	 * 1. aoc2ap gpio value already changed to AOC
	 * 2. tbn_release_bus() with TBN_RELEASE_BUS_TIMEOUT_MS timeout
	 *    for complete_all(&tbn->bus_released);
	 */
	while (!completion_done(&tbn->bus_released)) {
		if (gpiod_get_raw_value(tbn->aoc2ap_gpio) == TBN_BUS_OWNER_AOC)
			complete_all(&tbn->bus_released);
		else
			usleep_range(10000, 10000);	/* wait 10 ms for gpio stablized */
	}

	/*
	 * For bus request, there two possibilities:
	 * 1. aoc2ap gpio value already changed to AP
	 * 2. tbn_request_bus() with TBN_REQUEST_BUS_TIMEOUT_MS timeout
	 *    for complete_all(&tbn->bus_requested);
	 */
	while (!completion_done(&tbn->bus_requested)) {
		if (gpiod_get_raw_value(tbn->aoc2ap_gpio) == TBN_BUS_OWNER_AP)
			complete_all(&tbn->bus_requested);
		else
			usleep_range(10000, 10000);	/* wait 10 ms for gpio stablized */
	}

	return IRQ_HANDLED;
}

static int aoc_channel_kthread(void *data)
{
	struct tbn_context *tbn = data;
	struct TbnEventHeader header;
	ssize_t len;
	bool service_ready = false;

	while (!kthread_should_stop()) {
		if (service_ready != aoc_tbn_service_ready()) {
			service_ready = !service_ready;
			pr_info("%s: AOC TBN service is %s.\n",
				__func__, service_ready ? "ready" : "not ready");
		}

		if (!service_ready) {
			msleep_interruptible(1000);
			continue;
		}

		len = aoc_tbn_service_read(&header, sizeof(header));
		if (len < 0) {
			pr_err("%s: failed to read message, err: %ld\n",
				__func__, len);
			msleep_interruptible(1000);
			continue;
		}

		if (kthread_should_stop()) {
			break;
		}

		switch (header.operation) {
		case TBN_OPERATION_AOC_RESET:
			if (tbn->event_wq)
				queue_work(tbn->event_wq, &tbn->aoc_reset_work);
			break;

		case TBN_OPERATION_AOC_SEND_GESTURE_EVENT: {
			struct TbnLptwEvent *lptw = NULL;
			struct TbnGestureEvent gesture;

			if (len == sizeof(struct TbnLptwEvent)) {
				lptw = (struct TbnLptwEvent *)&header;
				gesture.type = kLongPress;
				gesture.x = lptw->x;
				gesture.y = lptw->y;
				gesture.major = lptw->major;
				gesture.minor = lptw->minor;
				gesture.angle = lptw->angle;
				gesture.lptw_finger_count = 0;
			} else if (len == sizeof(struct TbnGestureEvent)) {
				memcpy(&gesture, &header, len);
			} else {
				pr_err("%s: Abnormal gesture data length: %ld\n", __func__, len);
				break;
			}

			pr_info("%s: gesture event, type=%u x=%u y=%u major=%u minor=%u angle=%d time=%llu\n",
				__func__,
				gesture.type, gesture.x, gesture.y, gesture.major, gesture.minor,
				gesture.angle, gesture.isr_time);
			if (gesture.type == kLongPress && tbn->lptw_event_cb)
				tbn->lptw_event_cb(&gesture, tbn->lptw_event_cbdata);
			break;
		}

		case TBN_OPERATION_INVALID_GESTURE_EVENT: {
			struct TbnInvalidGestureEvent *event;

			if (len != sizeof(*event)) {
				pr_err("%s: Abnormal invalid gesture data length: %ld\n", __func__,
				       len);
				continue;
			}
			event = (struct TbnInvalidGestureEvent *)&header;
			tbn_notify_invalid_gesture((void *) event->data);
			break;
		}

		case TBN_OPERATION_INVALID_GESTURE_COUNTS: {
			struct TbnInvalidGestureCountEvent *event;

			if (len != sizeof(*event)) {
				pr_err("%s: Abnormal invalid gesture count data length: %ld\n",
					__func__, len);
				continue;
			}
			event = (struct TbnInvalidGestureCountEvent *)&header;
			tbn_notify_invalid_gesture_counts((void *) event->data);
			break;
		}

		case TBN_OPERATION_DEVICE_SWITCH_FAILURE: {
			struct TbnDeviceSwitchFailureEvent *event;

			if (len != sizeof(*event)) {
				pr_err("%s: Abnormal device switch failure length: %ld\n",
					__func__, len);
				continue;
			}
			event = (struct TbnDeviceSwitchFailureEvent *)&header;
			tbn_notify_device_switch_failure((void *) event);
			break;
		}

		default: {
			struct TbnEventResponse *resp;

			if (len != sizeof(*resp)) {
				pr_err("%s: Abnormal resp data length: %ld\n", __func__, len);
				continue;
			}
			resp = (struct TbnEventResponse *)&header;
			handle_tbn_event_response(tbn, resp);
			break;
		}
		}
	}

	return 0;
}

static void handle_tbn_event_response(struct tbn_context *tbn,
	struct TbnEventResponse *response)
{
	mutex_lock(&tbn->event_lock);

	if (response->id != tbn->event.id) {
		pr_err("%s: receive wrong response, id: %d, expected id: %d, "
			"bus_released:%d bus_requested:%d.\n",
			__func__, response->id, tbn->event.id,
			completion_done(&tbn->bus_released),
			completion_done(&tbn->bus_requested));
		goto exit;
	}

	if (response->err != 0) {
		pr_err("%s: send tbn event failed, err %d!\n",
			__func__, response->err);
		tbn->event_resp.err = response->err;
	} else {
		tbn->event_resp.lptw_triggered = response->lptw_triggered;
	}

	if (response->operation == TBN_OPERATION_AP_REQUEST_BUS) {
		complete_all(&tbn->bus_requested);
	} else if (response->operation == TBN_OPERATION_AP_RELEASE_BUS) {
		complete_all(&tbn->bus_released);
	} else {
		pr_err("%s: response unknown operation, op: %d!\n",
			__func__, response->operation);
	}

exit:
	mutex_unlock(&tbn->event_lock);
}

static int send_tbn_event(struct tbn_context *tbn, enum TbnOperation operation, void *payload,
			   size_t payload_len)
{
	ssize_t len;
	size_t total_size;
	int retry = 3;
	int ret = 0;

	if (!aoc_tbn_service_ready()) {
		pr_err("%s: AOC TBN service is not ready.\n", __func__);
		return -ENODEV;
	}

	/* Safety check to prevent buffer overflow */
	if (payload_len > sizeof(tbn->event.data)) {
		pr_err("%s: Payload too large (%zu > %zu)\n", __func__, payload_len,
		       sizeof(tbn->event.data));
		return -EINVAL;
	}

	mutex_lock(&tbn->event_lock);

	tbn->event.id = tbn->event_id;
	tbn->event.operation = operation;

	if (payload && payload_len > 0)
		memcpy(tbn->event.data, payload, payload_len);

	total_size = offsetof(struct TbnEvent, data) + payload_len;

	ret = -ETIMEDOUT;
	while (retry--) {
		len = aoc_tbn_service_write(&tbn->event, total_size);
		if (len == total_size) {
			ret = 0;
			break;
		}

		pr_err("%s: failed to send TBN event, retry: %d.\n", __func__, retry);
	}

	tbn->event_id++;

	mutex_unlock(&tbn->event_lock);

	return ret;
}

static inline void tbn_read_prop_u8(struct device_node *np, const char *prop, u8 *dest, int idx)
{
	u8 temp[2];
	int cnt;

	cnt = of_property_count_u8_elems(np, prop);
	if (cnt > 0 && cnt <= 2 && idx < cnt) {
		if (of_property_read_u8_array(np, prop, temp, cnt) == 0) {
			*dest = temp[idx];
			return;
		}
		pr_warn("Failed to read %s\n", prop);
	} else {
		pr_warn("TBN: %s has abnormal elements: idx:%d cnt:%d\n", prop, idx, cnt);
	}
	*dest = 0;
}

static inline void tbn_read_prop_u16(struct device_node *np, const char *prop, u16 *dest, int idx)
{
	u16 temp[2];
	int cnt;

	cnt = of_property_count_u16_elems(np, prop);
	if (cnt > 0 && cnt <= 2 && idx < cnt) {
		if (of_property_read_u16_array(np, prop, temp, cnt) == 0) {
			*dest = temp[idx];
			return;
		}
		pr_warn("Failed to read %s\n", prop);
	} else {
		pr_warn("TBN: %s has abnormal elements: idx:%d cnt:%d\n", prop, idx, cnt);
	}
	*dest = 0;
}

static void tbn_parse_panel_settings_dt(struct tbn_context *tbn)
{
	struct device_node *np = tbn->dev->of_node;
	int i;

	for (i = 0; i < tbn->max_devices; i++) {
		struct PanelSettings *ps = &tbn->panel_settings[i].settings;

		ps->device_id = i;
		tbn_read_prop_u8(np, "tbn,panel_setting_version", &ps->version, i);
		tbn_read_prop_u8(np, "tbn,gesture_type", &ps->type, i);
		tbn_read_prop_u16(np, "tbn,panel_height_pixel", &ps->panel_height_pixel, i);
		tbn_read_prop_u16(np, "tbn,panel_height_mm", &ps->panel_height_mm, i);
		tbn_read_prop_u8(np, "tbn,lptw_support_int2", &ps->lptw_support_int2, i);

		/* Set the default as 10. Can be update by tbn_update_super_resolution_scale */
		ps->super_resolution_scale = 10;
	}
}

static void tbn_parse_sttw_configs_dt(struct tbn_context *tbn)
{
	struct device_node *np = tbn->dev->of_node;
	int i;

	for (i = 0; i < tbn->max_devices; i++) {
		if (!(tbn->panel_settings[i].settings.type & kSingleTap))
			continue;

		struct STTWParams *params = &tbn->sttw_configs[i].params;

		params->device_id = i;
		tbn_read_prop_u8(np, "tbn,sttw_version", &params->version, i);
		tbn_read_prop_u16(np, "tbn,sttw_min_x", &params->min_x, i);
		tbn_read_prop_u16(np, "tbn,sttw_max_x", &params->max_x, i);
		tbn_read_prop_u16(np, "tbn,sttw_min_y", &params->min_y, i);
		tbn_read_prop_u16(np, "tbn,sttw_max_y", &params->max_y, i);
		tbn_read_prop_u8(np, "tbn,sttw_min_frame_count", &params->min_frame_count, i);
		tbn_read_prop_u8(np, "tbn,sttw_max_frame_count", &params->max_frame_count, i);
		tbn_read_prop_u16(np, "tbn,sttw_motion_tolerance", &params->motion_tolerance, i);
		tbn_read_prop_u8(np, "tbn,sttw_max_touch_size", &params->max_touch_size, i);
	}
}

static void tbn_parse_lptw_configs_dt(struct tbn_context *tbn)
{
	struct device_node *np = tbn->dev->of_node;
	int i;

	for (i = 0; i < tbn->max_devices; i++) {
		if (!(tbn->panel_settings[i].settings.type & kLongPress))
			continue;

		/* Access the inner 'params' struct */
		struct LPTWParams *params = &tbn->lptw_configs[i].params;

		params->device_id = i;
		tbn_read_prop_u8(np, "tbn,lptw_version", &params->version, i);
		tbn_read_prop_u16(np, "tbn,lptw_min_x", &params->min_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_max_x", &params->max_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_min_y", &params->min_y, i);
		tbn_read_prop_u16(np, "tbn,lptw_max_y", &params->max_y, i);
		tbn_read_prop_u8(np, "tbn,lptw_min_frame_count", &params->min_frame_count, i);
		tbn_read_prop_u8(np, "tbn,lptw_max_touch_size", &params->max_touch_size, i);
		tbn_read_prop_u16(np, "tbn,lptw_marginal_min_x", &params->marginal_min_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_marginal_max_x", &params->marginal_max_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_marginal_min_y", &params->marginal_min_y, i);
		tbn_read_prop_u16(np, "tbn,lptw_marginal_max_y", &params->marginal_max_y, i);
		tbn_read_prop_u8(np, "tbn,lptw_monitor_channel_min_tx",
				 &params->monitor_channel_min_tx, i);
		tbn_read_prop_u8(np, "tbn,lptw_monitor_channel_max_tx",
				 &params->monitor_channel_max_tx, i);
		tbn_read_prop_u8(np, "tbn,lptw_monitor_channel_min_rx",
				 &params->monitor_channel_min_rx, i);
		tbn_read_prop_u8(np, "tbn,lptw_monitor_channel_max_rx",
				 &params->monitor_channel_max_rx, i);
		tbn_read_prop_u8(np, "tbn,lptw_min_node_count", &params->min_node_count, i);
		tbn_read_prop_u16(np, "tbn,lptw_motion_tolerance_inner",
				  &params->motion_tolerance_inner, i);
		tbn_read_prop_u16(np, "tbn,lptw_motion_tolerance_outer",
				  &params->motion_tolerance_outer, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_assert_min_x",
				  &params->int2_assert_min_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_assert_max_x",
				  &params->int2_assert_max_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_assert_min_y",
				  &params->int2_assert_min_y, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_assert_max_y",
				  &params->int2_assert_max_y, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_deassert_min_x",
				  &params->int2_deassert_min_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_deassert_max_x",
				  &params->int2_deassert_max_x, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_deassert_min_y",
				  &params->int2_deassert_min_y, i);
		tbn_read_prop_u16(np, "tbn,lptw_int2_deassert_max_y",
				  &params->int2_deassert_max_y, i);
	}
}

static void tbn_parse_gesture_settings(struct tbn_context *tbn)
{
	tbn_parse_panel_settings_dt(tbn);
	tbn_parse_sttw_configs_dt(tbn);
	tbn_parse_lptw_configs_dt(tbn);
}

static void tbn_send_gesture_settings(struct tbn_context *tbn)
{
	for (int i = 0; i < tbn->max_devices; i++) {
		if ((tbn->panel_settings[i].settings.type & kSingleTap) &&
		    (tbn->sttw_configs[i].params.version != 0)) {
			send_tbn_event(tbn, TBN_OPERATION_AP_STTW_CONFIGS,
				       &tbn->sttw_configs[i].params,
				       sizeof(tbn->sttw_configs[i].params));
		}

		if ((tbn->panel_settings[i].settings.type & kLongPress) &&
		    (tbn->lptw_configs[i].params.version != 0)) {
			send_tbn_event(tbn, TBN_OPERATION_AP_LPTW_CONFIGS,
				       &tbn->lptw_configs[i].params,
				       sizeof(tbn->lptw_configs[i].params));
		}

		if (tbn->panel_settings[i].settings.version != 0) {
			send_tbn_event(tbn, TBN_OPERATION_AP_PANEL_SETTINGS,
				       &tbn->panel_settings[i].settings,
				       sizeof(tbn->panel_settings[i].settings));
		}
	}
}

void tbn_update_super_resolution_scale(u8 scale, u32 idx)
{
	if (idx >= tbn_context->max_devices) {
		pr_err("%s: Invalid device index: %u\n", __func__, idx);
		return;
	}

	tbn_context->panel_settings[idx].settings.super_resolution_scale = scale;
	send_tbn_event(tbn_context, TBN_OPERATION_AP_PANEL_SETTINGS,
		       &tbn_context->panel_settings[idx].settings,
		       sizeof(tbn_context->panel_settings[idx].settings));
	pr_info("dev id: %u set super_resolution_scale %u", idx, scale);
}
EXPORT_SYMBOL_GPL(tbn_update_super_resolution_scale);

void tbn_update_high_sensitivity_mode(u8 mode, u32 idx)
{
	if (idx >= tbn_context->max_devices) {
		pr_err("%s: Invalid device index: %u\n", __func__, idx);
		return;
	}

	tbn_context->panel_settings[idx].settings.high_sensitivity_mode = mode;
	send_tbn_event(tbn_context, TBN_OPERATION_AP_PANEL_SETTINGS,
		       &tbn_context->panel_settings[idx].settings,
		       sizeof(tbn_context->panel_settings[idx].settings));
	pr_info("dev id: %u set high_sensitivity_mode %u", idx, mode);
}
EXPORT_SYMBOL_GPL(tbn_update_high_sensitivity_mode);

void tbn_update_checksum_enabled(u8 enabled, u32 idx)
{
	if (idx >= tbn_context->max_devices) {
		pr_err("%s: Invalid device index: %u\n", __func__, idx);
		return;
	}

	tbn_context->panel_settings[idx].settings.checksum_enabled = enabled;
	send_tbn_event(tbn_context, TBN_OPERATION_AP_PANEL_SETTINGS,
		       &tbn_context->panel_settings[idx].settings,
		       sizeof(tbn_context->panel_settings[idx].settings));
	pr_info("dev id: %u set checksum_enabled %u", idx, enabled);
}
EXPORT_SYMBOL_GPL(tbn_update_checksum_enabled);

void tbn_update_gesture_config(enum gti_gesture_params param, u16 value, u32 idx)
{
	struct STTWParams *sttw;
	struct LPTWParams *lptw;
	bool update_sttw = false;
	bool update_lptw = false;

	if (!tbn_context || idx >= tbn_context->max_devices)
		return;

	sttw = &tbn_context->sttw_configs[idx].params;
	lptw = &tbn_context->lptw_configs[idx].params;

	switch (param) {
	case GTI_STTW_MIN_X:
		sttw->min_x = value;
		update_sttw = true;
		break;
	case GTI_STTW_MAX_X:
		sttw->max_x = value;
		update_sttw = true;
		break;
	case GTI_STTW_MIN_Y:
		sttw->min_y = value;
		update_sttw = true;
		break;
	case GTI_STTW_MAX_Y:
		sttw->max_y = value;
		update_sttw = true;
		break;
	case GTI_STTW_MIN_FRAME:
		sttw->min_frame_count = value;
		update_sttw = true;
		break;
	case GTI_STTW_MAX_FRAME:
		sttw->max_frame_count = value;
		update_sttw = true;
		break;
	case GTI_STTW_JITTER:
		sttw->motion_tolerance = value;
		update_sttw = true;
		break;
	case GTI_STTW_MAX_TOUCH_SIZE:
		sttw->max_touch_size = value;
		update_sttw = true;
		break;

	case GTI_LPTW_MIN_X:
		lptw->min_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MAX_X:
		lptw->max_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MIN_Y:
		lptw->min_y = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MAX_Y:
		lptw->max_y = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MIN_FRAME:
		lptw->min_frame_count = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MAX_TOUCH_SIZE:
		lptw->max_touch_size = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MARGINAL_MIN_X:
		lptw->marginal_min_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MARGINAL_MAX_X:
		lptw->marginal_max_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MARGINAL_MIN_Y:
		lptw->marginal_min_y = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MARGINAL_MAX_Y:
		lptw->marginal_max_y = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MONITOR_CH_MIN_TX:
		lptw->monitor_channel_min_tx = (u8)value;
		update_lptw = true;
		break;
	case GTI_LPTW_MONITOR_CH_MAX_TX:
		lptw->monitor_channel_max_tx = (u8)value;
		update_lptw = true;
		break;
	case GTI_LPTW_MONITOR_CH_MIN_RX:
		lptw->monitor_channel_min_rx = (u8)value;
		update_lptw = true;
		break;
	case GTI_LPTW_MONITOR_CH_MAX_RX:
		lptw->monitor_channel_max_rx = (u8)value;
		update_lptw = true;
		break;
	case GTI_LPTW_NODE_COUNT_MIN:
		lptw->min_node_count = (u8)value;
		update_lptw = true;
		break;
	case GTI_LPTW_JITTER:
		lptw->motion_tolerance_inner = value;
		update_lptw = true;
		break;
	case GTI_LPTW_MOTION_BOUNDARY:
		lptw->motion_tolerance_outer = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_ASSERT_MIN_X:
		lptw->int2_assert_min_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_ASSERT_MAX_X:
		lptw->int2_assert_max_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_ASSERT_MIN_Y:
		lptw->int2_assert_min_y = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_ASSERT_MAX_Y:
		lptw->int2_assert_max_y = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_DEASSERT_MIN_X:
		lptw->int2_deassert_min_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_DEASSERT_MAX_X:
		lptw->int2_deassert_max_x = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_DEASSERT_MIN_Y:
		lptw->int2_deassert_min_y = value;
		update_lptw = true;
		break;
	case GTI_LPTW_INT2_DEASSERT_MAX_Y:
		lptw->int2_deassert_max_y = value;
		update_lptw = true;
		break;
	default:
		break;
	}

	if (update_sttw) {
		send_tbn_event(tbn_context, TBN_OPERATION_AP_STTW_CONFIGS,
			       sttw, sizeof(*sttw));
	}
	if (update_lptw) {
		send_tbn_event(tbn_context, TBN_OPERATION_AP_LPTW_CONFIGS,
			       lptw, sizeof(*lptw));
	}
}
EXPORT_SYMBOL_GPL(tbn_update_gesture_config);

void tbn_debug_configs_dump(struct seq_file *m, u32 dev_id)
{
	struct tbn_context *tbn = tbn_context;
	struct PanelSettings *ps;

	if (!tbn)
		return;

	if (dev_id >= tbn->max_devices) {
		seq_printf(m, "Invalid device ID: %u (Max: %u)\n", dev_id, tbn->max_devices);
		return;
	}

	seq_printf(m, "\t### TBN Configurations (Device %u) ###\n", dev_id);

	ps = &tbn->panel_settings[dev_id].settings;

	/* Panel Settings - Skip if version is 0 */
	if (ps->version == 0) {
		seq_printf(m, " Device %u: Panel Settings not initialized (version 0)\n", dev_id);
		return;
	}

	seq_printf(m, "[Device %u Panel Settings]\n", dev_id);
	seq_printf(m, " ID: %u, Ver: %u, DevID: %u, GestureType: %u, Height: %upx (%umm)\n",
		   tbn->panel_settings[dev_id].id,
		   ps->version,
		   ps->device_id,
		   ps->type,
		   ps->panel_height_pixel,
		   ps->panel_height_mm);
	seq_printf(m, " SR Scale: %u, Support Int2: %u, HighSens: %u, Checksum: %u\n",
		   ps->super_resolution_scale,
		   ps->lptw_support_int2,
		   ps->high_sensitivity_mode,
		   ps->checksum_enabled);

	/* STTW Configs */
	if (ps->type & kSingleTap) {
		struct STTWParams *s = &tbn->sttw_configs[dev_id].params;

		seq_puts(m, "  [STTW Configs]\n");
		seq_printf(m, "   Ver: %u, DevID: %u\n", s->version, s->device_id);
		seq_printf(m, "   X: %u-%u, Y: %u-%u, Frame: %u-%u, ", s->min_x, s->max_x, s->min_y,
			   s->max_y, s->min_frame_count, s->max_frame_count);
		seq_printf(m, "Motion tolerance: %u, Max Size: %u\n", s->motion_tolerance,
			   s->max_touch_size);
	}

	/* LPTW Configs */
	if (ps->type & kLongPress) {
		struct LPTWParams *l = &tbn->lptw_configs[dev_id].params;

		seq_puts(m, "  [LPTW Configs]\n");
		seq_printf(m, "   Ver: %u, DevID: %u\n", l->version, l->device_id);
		seq_printf(m, "   Area: X(%u-%u) Y(%u-%u)\n", l->min_x, l->max_x, l->min_y,
			   l->max_y);
		seq_printf(m, "   Marginal: X(%u-%u) Y(%u-%u), ", l->marginal_min_x,
			   l->marginal_max_x, l->marginal_min_y, l->marginal_max_y);
		seq_printf(m, "Monitor Channels: TX(%u-%u) RX(%u-%u)\n", l->monitor_channel_min_tx,
			   l->monitor_channel_max_tx, l->monitor_channel_min_rx,
			   l->monitor_channel_max_rx);
		seq_printf(m, "   Min Frames: %u, Min Nodes: %u, Max Size: %u, ",
			   l->min_frame_count, l->min_node_count, l->max_touch_size);
		seq_printf(m, "Motion tolerance: Inner(%u) Outer(%u)\n", l->motion_tolerance_inner,
			   l->motion_tolerance_outer);
		seq_printf(m, "   Int2 Assert: X(%u-%u) Y(%u-%u), Deassert: X(%u-%u) Y(%u-%u)\n",
			   l->int2_assert_min_x, l->int2_assert_max_x,
			   l->int2_assert_min_y, l->int2_assert_max_y,
			   l->int2_deassert_min_x, l->int2_deassert_max_x,
			   l->int2_deassert_min_y, l->int2_deassert_max_y);
	}
	seq_puts(m, "\n");
}
EXPORT_SYMBOL_GPL(tbn_debug_configs_dump);

static int tbn_handshaking(struct tbn_context *tbn, enum TbnOperation operation)
{
	struct completion *wait_for_completion;
	enum tbn_bus_owner bus_owner;
	unsigned int irq_type;
	unsigned int timeout;
	const char *msg;
	int ret = 0;

	if (!tbn || tbn->registered_mask == 0) {
		pr_err("%s: tbn is not ready to serve.\n", __func__);
		return -EINVAL;
	}

	if (operation == TBN_OPERATION_AP_REQUEST_BUS) {
		wait_for_completion = &tbn->bus_requested;
		bus_owner = TBN_BUS_OWNER_AP;
		irq_type = IRQF_TRIGGER_FALLING;
		timeout = TBN_REQUEST_BUS_TIMEOUT_MS;
		msg = "request";
	} else if (operation == TBN_OPERATION_AP_RELEASE_BUS) {
		wait_for_completion = &tbn->bus_released;
		bus_owner = TBN_BUS_OWNER_AOC;
		irq_type = IRQF_TRIGGER_RISING;
		timeout = TBN_RELEASE_BUS_TIMEOUT_MS;
		msg = "release";
	} else {
		pr_err("%s: request unknown operation, op: %d.\n",
			__func__, operation);
		return -EINVAL;
	}

	if (tbn->mode == TBN_MODE_GPIO) {
		int ap2aoc_val_org = gpiod_get_raw_value(tbn->ap2aoc_gpio);
		int aoc2ap_val_org = gpiod_get_raw_value(tbn->aoc2ap_gpio);

		reinit_completion(wait_for_completion);

		irq_set_irq_type(tbn->aoc2ap_irq, irq_type);
		enable_irq(tbn->aoc2ap_irq);
		gpiod_direction_output_raw(tbn->ap2aoc_gpio, bus_owner);
		if (wait_for_completion_timeout(wait_for_completion,
			msecs_to_jiffies(timeout)) == 0) {
			int ap2aoc_val = gpiod_get_raw_value(tbn->ap2aoc_gpio);
			int aoc2ap_val = gpiod_get_raw_value(tbn->aoc2ap_gpio);

			complete_all(wait_for_completion);
			if (bus_owner == aoc2ap_val)
				ret = 0;
			else
				ret = -ETIMEDOUT;
			pr_err("AP %s bus ... timeout!, ap2aoc_gpio(B:%d,A:%d)"
				" aoc2ap_gpio(B:%d,A:%d), ret=%d\n",
				msg, ap2aoc_val_org, ap2aoc_val, aoc2ap_val_org,
				aoc2ap_val, ret);
		} else
			pr_info("AP %s bus ... SUCCESS!\n", msg);
		disable_irq_nosync(tbn->aoc2ap_irq);
	} else if (tbn->mode == TBN_MODE_AOC_CHANNEL) {
		tbn->event_resp.lptw_triggered = false;
		tbn->event_resp.err = 0;

		reinit_completion(wait_for_completion);

		ret = send_tbn_event(tbn, operation, NULL, 0);
		if (ret < 0) {
			pr_err("Failed to send %s bus!\n", msg);
		} else if (wait_for_completion_timeout(wait_for_completion,
			msecs_to_jiffies(timeout)) == 0) {
			pr_err("AP %s bus ... timeout!\n", msg);
			complete_all(wait_for_completion);
			ret = -ETIMEDOUT;
		} else {
			if (tbn->event_resp.err == 0) {
				pr_info("AP %s bus ... SUCCESS!\n", msg);
			} else {
				pr_info("AP %s bus ... failed!\n", msg);
				ret = -EBUSY;
			}
		}
	} else if (tbn->mode == TBN_MODE_MOCK) {
		pr_info("AP %s bus ... SUCCESS!\n", msg);
	} else {
		ret = -EINVAL;
	}

	return ret;
}

int tbn_request_bus_with_result(u32 dev_mask, bool *lptw_triggered)
{
	int ret = 0;

	if (!tbn_context)
		return -ENODEV;

	mutex_lock(&tbn_context->dev_mask_mutex);

	if ((dev_mask & tbn_context->registered_mask) == 0) {
		mutex_unlock(&tbn_context->dev_mask_mutex);
		pr_err("%s: dev_mask %#x is invalid.\n",
			__func__, dev_mask);
		return -EINVAL;
	}

	if (tbn_context->requested_dev_mask == 0) {
		ret = tbn_handshaking(tbn_context, TBN_OPERATION_AP_REQUEST_BUS);
		if ((ret == 0) && (lptw_triggered != NULL))
			*lptw_triggered = tbn_context->event_resp.lptw_triggered;
	} else {
		dev_dbg(tbn_context->dev,
			"%s: Bus already requested, requested_dev_mask %#x dev_mask %#x.\n",
			__func__, tbn_context->requested_dev_mask, dev_mask);
	}
	tbn_context->requested_dev_mask |= dev_mask;

	mutex_unlock(&tbn_context->dev_mask_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(tbn_request_bus_with_result);

int tbn_request_bus(u32 dev_mask)
{
	return tbn_request_bus_with_result(dev_mask, NULL);
}
EXPORT_SYMBOL_GPL(tbn_request_bus);

int tbn_release_bus(u32 dev_mask)
{
	int ret = 0;

	if (!tbn_context)
		return -ENODEV;

	mutex_lock(&tbn_context->dev_mask_mutex);

	if ((dev_mask & tbn_context->registered_mask) == 0) {
		mutex_unlock(&tbn_context->dev_mask_mutex);
		pr_err("%s: dev_mask %#x is invalid.\n",
			__func__, dev_mask);
		return -EINVAL;
	}

	if (tbn_context->requested_dev_mask == 0) {
		pr_warn("%s: Bus already released, dev_mask %#x.\n",
			 __func__, dev_mask);
		mutex_unlock(&tbn_context->dev_mask_mutex);
		return 0;
	}

	/* Release the bus when the last requested_dev_mask bit releases. */
	if (tbn_context->requested_dev_mask == dev_mask) {
		ret = tbn_handshaking(tbn_context, TBN_OPERATION_AP_RELEASE_BUS);
	} else {
		dev_dbg(tbn_context->dev,
			 "%s: Bus is still in use, requested_dev_mask %#x dev_mask %#x.\n",
			 __func__, tbn_context->requested_dev_mask, dev_mask);
	}

	tbn_context->requested_dev_mask &= ~dev_mask;

	mutex_unlock(&tbn_context->dev_mask_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(tbn_release_bus);

int register_tbn(u32 *output)
{
	u32 i = 0;

	*output = 0;

	if (!tbn_context) {
		pr_warn("%s: tbn_context doesn't exist.", __func__);
		return 0;
	}

	mutex_lock(&tbn_context->dev_mask_mutex);
	for (i = 0; i < tbn_context->max_devices; i++) {
		if (tbn_context->registered_mask & BIT_MASK(i))
			continue;
		tbn_context->registered_mask |= BIT_MASK(i);
		/* Assume screen is on while registering tbn. */
		tbn_context->requested_dev_mask |= BIT_MASK(i);
		*output = BIT_MASK(i);
		tbn_send_gesture_settings(tbn_context);
		break;
	}

	mutex_unlock(&tbn_context->dev_mask_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(register_tbn);

void register_tbn_lptw_callback(void* callback, void* cbdata)
{
	tbn_context->lptw_event_cb = callback;
	tbn_context->lptw_event_cbdata = cbdata;
}
EXPORT_SYMBOL_GPL(register_tbn_lptw_callback);

void tbn_event_notifier_register(struct notifier_block *nb, bool reg)
{
	if (reg)
		raw_notifier_chain_register(&tbn_event_notifier, nb);
	else
		raw_notifier_chain_unregister(&tbn_event_notifier, nb);
}
EXPORT_SYMBOL_GPL(tbn_event_notifier_register);

static void tbn_notify_invalid_gesture(void *event)
{
	raw_notifier_call_chain(&tbn_event_notifier, TBN_OPERATION_INVALID_GESTURE_EVENT, event);
}

static void tbn_notify_invalid_gesture_counts(void *event)
{
	raw_notifier_call_chain(&tbn_event_notifier, TBN_OPERATION_INVALID_GESTURE_COUNTS, event);
}

static void tbn_notify_device_switch_failure(void *event)
{
	raw_notifier_call_chain(&tbn_event_notifier, TBN_OPERATION_DEVICE_SWITCH_FAILURE, event);
}

static void tbn_notify_aoc_reset(void)
{
	raw_notifier_call_chain(&tbn_event_notifier, TBN_OPERATION_AOC_RESET, NULL);
}

static void tbn_aoc_reset_work(struct work_struct *work)
{
	struct tbn_context *tbn = container_of(work, struct tbn_context, aoc_reset_work);

	pr_warn("%s: AOC has been reset", __func__);
	if (tbn)
		tbn_send_gesture_settings(tbn_context);

	if (tbn->requested_dev_mask == 0)
		tbn_handshaking(tbn, TBN_OPERATION_AP_RELEASE_BUS);
	else
		tbn_handshaking(tbn, TBN_OPERATION_AP_REQUEST_BUS);

	tbn_notify_aoc_reset();
}

void unregister_tbn(u32 *output)
{
	if (!tbn_context)
		return ;

	mutex_lock(&tbn_context->dev_mask_mutex);
	tbn_context->registered_mask &= ~(*output);
	*output = 0;
	mutex_unlock(&tbn_context->dev_mask_mutex);
}
EXPORT_SYMBOL_GPL(unregister_tbn);

static int tbn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tbn_context *tbn = NULL;
	struct device_node *np = dev->of_node;
	int err = 0;
	struct sched_param param = {
		.sched_priority = 10,
	};

	tbn = devm_kzalloc(dev, sizeof(struct tbn_context), GFP_KERNEL);
	if (!tbn) {
		err = -ENOMEM;
		goto failed;
	}

	tbn->dev = dev;
	tbn->event_resp.lptw_triggered = false;
	tbn_context = tbn;
	dev_set_drvdata(tbn->dev, tbn);

	if (of_property_read_u32(np, "tbn,max_devices", &tbn->max_devices))
		tbn->max_devices = 1;

	tbn->panel_settings = devm_kcalloc(dev, tbn->max_devices,
					   sizeof(struct TbnPanelSettingsEvent), GFP_KERNEL);
	tbn->sttw_configs = devm_kcalloc(dev, tbn->max_devices,
					 sizeof(struct TbnSttwConfigsEvent), GFP_KERNEL);
	tbn->lptw_configs = devm_kcalloc(dev, tbn->max_devices,
					 sizeof(struct TbnLptwConfigsEvent), GFP_KERNEL);

	if (!tbn->panel_settings || !tbn->sttw_configs || !tbn->lptw_configs) {
		err = -ENOMEM;
		goto failed;
	}

	tbn_parse_gesture_settings(tbn);

	err = of_property_read_u32(np, "tbn,mode", &tbn->mode);
	if (err)
		tbn->mode = TBN_MODE_GPIO;

	if (tbn->mode == TBN_MODE_GPIO) {
		tbn->ap2aoc_gpio = devm_gpiod_get(tbn->dev, "tbn,ap2aoc", GPIOD_ASIS);
		if (IS_ERR(tbn->ap2aoc_gpio)) {
			err = PTR_ERR(tbn->ap2aoc_gpio);
			pr_err("%s: Unable to request ap2aoc_gpio, err %d!\n",
			       __func__, err);
			goto failed;
		}
		gpiod_direction_output_raw(tbn->ap2aoc_gpio, 0);

		tbn->aoc2ap_gpio = devm_gpiod_get(tbn->dev, "tbn,aoc2ap", GPIOD_IN);
		if (IS_ERR(tbn->aoc2ap_gpio)) {
			err = PTR_ERR(tbn->aoc2ap_gpio);
			pr_err("%s: Unable to request aoc2ap_gpio, err %d!\n",
			       __func__, err);
			goto failed;
		}

		tbn->aoc2ap_irq = gpiod_to_irq(tbn->aoc2ap_gpio);
		err = devm_request_threaded_irq(tbn->dev,
						tbn->aoc2ap_irq, NULL,
						tbn_aoc2ap_irq_thread,
						IRQF_TRIGGER_RISING |
						IRQF_ONESHOT, "tbn", tbn);
		if (err) {
			pr_err("%s: Unable to request_threaded_irq, err %d!\n",
			       __func__, err);
			goto failed;
		}
		disable_irq_nosync(tbn->aoc2ap_irq);

		pr_info("%s: gpios(aoc2ap: %d ap2aoc: %d)\n",
			__func__, desc_to_gpio(tbn->aoc2ap_gpio), desc_to_gpio(tbn->ap2aoc_gpio));
	} else if (tbn->mode == TBN_MODE_AOC_CHANNEL) {
		mutex_init(&tbn->event_lock);

		tbn->aoc_channel_task = kthread_run(&aoc_channel_kthread, tbn,
			TBN_AOC_CHANNEL_THREAD_NAME);
		if (IS_ERR(tbn->aoc_channel_task)) {
			err = PTR_ERR(tbn->aoc_channel_task);
			goto failed;
		}

		err = sched_setscheduler(tbn->aoc_channel_task, SCHED_FIFO, &param);
		if (err != 0) {
			goto failed;
		}

		tbn->event_wq = alloc_workqueue(
			"tbn_wq", WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
		if (!tbn->event_wq) {
			err = -ENOMEM;
			pr_err("Failed to create work thread for tbn!\n");
			goto failed;
		}

		INIT_WORK(&tbn->aoc_reset_work, tbn_aoc_reset_work);
	} else if (tbn->mode == TBN_MODE_MOCK) {
		err = 0;
	} else {
		pr_err("bus negotiator: invalid mode: %d\n", tbn->mode);
		err = -EINVAL;
		goto failed;
	}

	mutex_init(&tbn->dev_mask_mutex);

	init_completion(&tbn->bus_requested);
	init_completion(&tbn->bus_released);
	complete_all(&tbn->bus_requested);
	complete_all(&tbn->bus_released);

	pr_info("bus negotiator initialized: %pK, mode: %d\n", tbn, tbn->mode);

failed:
	if (err)
		tbn_remove(pdev);

	return err;
}

static void tbn_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tbn_context *tbn = dev_get_drvdata(dev);

	if (tbn == NULL)
		return;

	if (tbn->mode == TBN_MODE_AOC_CHANNEL) {
		if (!IS_ERR(tbn->aoc_channel_task))
			kthread_stop(tbn->aoc_channel_task);
		if (tbn->event_wq)
			destroy_workqueue(tbn->event_wq);
	}

	devm_kfree(dev, tbn);
	tbn_context = NULL;
}

static void tbn_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tbn_context *tbn = dev_get_drvdata(dev);

	if (tbn == NULL)
		return;

	if (tbn->mode == TBN_MODE_AOC_CHANNEL) {
		if (!IS_ERR(tbn->aoc_channel_task))
			kthread_stop(tbn->aoc_channel_task);
	}
}

static struct of_device_id tbn_of_match_table[] = {
	{
		.compatible = TBN_MODULE_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, tbn_of_match_table);

static struct platform_driver tbn_driver = {
	.driver = {
		.name = TBN_MODULE_NAME,
		.of_match_table = tbn_of_match_table,
	},
	.probe = tbn_probe,
	.remove = tbn_remove,
	.shutdown = tbn_shutdown,
};

static int __init tbn_init(void)
{
	return platform_driver_register(&tbn_driver);
}

static void __exit tbn_exit(void)
{
	platform_driver_unregister(&tbn_driver);
}
module_init(tbn_init);
module_exit(tbn_exit);

MODULE_SOFTDEP("pre: touch_offload");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Touch Bus Negotiator");
MODULE_AUTHOR("Google, Inc.");
