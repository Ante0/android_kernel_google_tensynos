// SPDX-License-Identifier: GPL
/*
 * Google Touch Interface for Pixel.
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/input/mt.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/proc_fs.h>
#include <linux/pm_runtime.h>
#include <linux/seq_file.h>
#include <trace/hooks/systrace.h>

#if IS_ENABLED(CONFIG_AOC_DRIVER)
#include "aoc.h"
#endif
#include "goog_interface_manager.h"
#include "goog_touch_interface.h"
#include "gti_internal.h"
#include "gti_sim.h"
#include "gti_status_event_dev.h"
#include "touch_bus_negotiator.h"

#undef pr_fmt
#define pr_fmt(fmt) "gti: " fmt
/*-----------------------------------------------------------------------------
 * GTI/common: forward declarations, structures and functions.
 */
static void goog_input_flush_offload_fingers(struct goog_touch_interface *gti);
static void goog_notify_lptw_triggered(struct TbnGestureEvent *lptw, void *data);
static int goog_notify_lptw_left(struct goog_touch_interface *gti);
static void goog_track_lptw_slot(struct goog_touch_interface *gti, u16 x, u16 y, int slot_bit);
static void gti_init_input(struct goog_touch_interface *gti, struct device_node *dn);
static void gti_input_set_timestamp(struct goog_touch_interface *gti, ktime_t timestamp);
static void gti_update_fw_settings(struct goog_touch_interface *gti, bool force_update);
static RAW_NOTIFIER_HEAD(lptw_notifier);
static int gti_default_handler_nop(void *private_data, u32 cmd_type, struct gti_union_cmd_data *cmd)
{
	return -ESRCH;
}

/* Declare all default nop optional functions in struct gti_optional_configuration. */
GTI_OPT_FUNC_WITH_TYPE(GTI_DECLARE_NOP_FUNC);

/*-----------------------------------------------------------------------------
 * GTI: functions.
 */
static int gti_set_display_state(struct notifier_block *nb, unsigned long display_event_id,
				 void *_display_core)
{
	int ret = 0;
	struct goog_display_core *display_core = _display_core;
	struct goog_interface *interface = display_core->interface;
	struct goog_touch_interface *gti;
	enum gti_display_state_setting gti_display_state;

	if (!display_core || !interface)
		goto notify_display_state_done;

	gti = interface->context;

	switch (display_event_id) {
	case DISPLAY_EVENT_ID_SCREEN_OFF:
		gti_display_state = GTI_DISPLAY_STATE_OFF;
		break;
	case DISPLAY_EVENT_ID_SCREEN_ON:
		gti_display_state = GTI_DISPLAY_STATE_ON;
		break;
	default:
		GOOG_LOGW(gti, "unexcepted event_id %lu\n", display_event_id);
		goto notify_display_state_done;
	}

	if (!gti || gti->display_state == gti_display_state)
		goto notify_display_state_done;

	if (gti->pm.enabled == false) {
		GOOG_INFO(gti, "%s before PM is ready",
			  (gti_display_state == GTI_DISPLAY_STATE_OFF) ? "screen-off" :
									 "screen-on");
		goto notify_display_state_done;
	}

	switch (gti_display_state) {
	case GTI_DISPLAY_STATE_OFF:
		GOOG_INFO(gti, "screen-off.\n");
		ret = goog_pm_wake_unlock_nosync(gti, GTI_PM_WAKELOCK_TYPE_SCREEN_ON);
		if (ret < 0)
			GOOG_INFO(gti, "Error while obtaining screen-off wakelock: %d!\n", ret);

		break;
	case GTI_DISPLAY_STATE_ON:
		GOOG_INFO(gti, "screen-on.\n");
		ret = goog_pm_wake_lock_nosync(gti, GTI_PM_WAKELOCK_TYPE_SCREEN_ON, false);
		if (ret < 0)
			GOOG_INFO(gti, "Error while obtaining screen-on wakelock: %d!\n", ret);

		break;
	default:
		GOOG_ERR(gti, "Unexpected value(%u) of display state parameter.\n",
			 gti_display_state);
		goto notify_display_state_done;
	}

	gti->context_changed.screen_state = 1;
	gti->display_state = gti_display_state;
	gti->cmd.display_state_cmd.setting = gti_display_state;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_NOTIFY_DISPLAY_STATE);
	if (ret && ret != -EOPNOTSUPP)
		GOOG_WARN(gti, "Unexpected vendor_cmd return(%d)!\n", ret);

notify_display_state_done:
	return NOTIFY_DONE;
}

bool goog_check_spi_dma_enabled(struct spi_device *spi_dev)
{
	bool ret = false;

	if (spi_dev && spi_dev->controller) {
		struct device_node *dn = spi_dev->controller->dev.of_node;

		ret = of_property_present(dn, "dmas");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(goog_check_spi_dma_enabled);

bool goog_check_late_sense_on_enabled(struct goog_touch_interface *gti)
{
	return gti != NULL ? gti->late_sense_on_enabled : false;
}
EXPORT_SYMBOL_GPL(goog_check_late_sense_on_enabled);

static int goog_get_panel_id_from_tic(struct goog_touch_interface *gti)
{
	int ret;

	gti->cmd.panel_id_cmd.setting = -1;
	ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_PANEL_ID);
	if (ret != 0) {
		GOOG_ERR(gti, "Fail to get panel id from tic!\n");
		return ret;
	}
	return gti->cmd.panel_id_cmd.setting;
}

int goog_get_panel_id(struct device_node *node)
{
	int id = -ENODEV;
	int err;
	int index;
	struct of_phandle_args panelmap;
	struct drm_panel *panel = NULL;

	if (!of_property_read_bool(node, "goog,panel_map")) {
		pr_warn("%s: panel_map doesn't exist!\n", __func__);
		return -EOPNOTSUPP;
	}

	for (index = 0;; index++) {
		err = of_parse_phandle_with_fixed_args(
			node, "goog,panel_map", 1, index, &panelmap);
		if (err) {
			pr_warn("%s: Fail to find panel for index: %d!\n", __func__, index);
			break;
		}

		panel = of_drm_find_panel(panelmap.np);
		of_node_put(panelmap.np);
		if (IS_ERR_OR_NULL(panel))
			continue;

		id = panelmap.args[0];
		break;
	}

	return id;
}
EXPORT_SYMBOL_GPL(goog_get_panel_id);

int goog_get_firmware_name(struct device_node *node, int id, char *name, size_t size)
{
	int err;
	const char *fw_name;

	err = of_property_read_string_index(node, "goog,firmware_names", id, &fw_name);
	if (err == 0) {
		strscpy(name, fw_name, size);
		pr_info("%s: found firmware name: %s\n", __func__, name);
	} else {
		pr_warn("%s: Fail to find firmware name!\n", __func__);
	}
	return err;
}
EXPORT_SYMBOL_GPL(goog_get_firmware_name);

int goog_get_config_name(struct device_node *node, int id, char *name, size_t size)
{
	int err;
	const char *config_name;

	err = of_property_read_string_index(node, "goog,config_names", id, &config_name);
	if (err == 0) {
		strncpy(name, config_name, size);
		pr_info("%s: found config name: %s\n", __func__, name);
	} else {
		pr_warn("%s: Fail to find config name!\n", __func__);
	}
	return err;
}
EXPORT_SYMBOL_GPL(goog_get_config_name);

int goog_get_test_limits_name(struct device_node *node, int id, char *name, size_t size)
{
	int err;
	const char *limits_name;

	err = of_property_read_string_index(node, "goog,test_limits_names", id, &limits_name);
	if (err == 0) {
		strncpy(name, limits_name, size);
		pr_info("%s: found test limits name: %s\n", __func__, name);
	} else {
		pr_warn("%s: Fail to find test limits name!\n", __func__);
	}
	return err;
}
EXPORT_SYMBOL_GPL(goog_get_test_limits_name);

int goog_process_vendor_cmd(struct goog_touch_interface *gti, enum gti_cmd_type cmd_type)
{
	void *private_data = gti->vendor_private_data;
	int ret = -ESRCH;

	/* Use optional vendor operation if available. */
	switch (cmd_type) {
	case GTI_CMD_CALIBRATE:
		ret = gti->options.calibrate(private_data, &gti->cmd.calibrate_cmd);
		break;
	case GTI_CMD_PING:
		ret = gti->options.ping(private_data, &gti->cmd.ping_cmd);
		break;
	case GTI_CMD_RESET:
		ret = gti->options.reset(private_data, &gti->cmd.reset_cmd);
		break;
	case GTI_CMD_SELFTEST:
		ret = gti->options.selftest(private_data, &gti->cmd.selftest_cmd);
		break;
	case GTI_CMD_GET_CONTEXT_DRIVER:
		ret = gti->options.get_context_driver(private_data, &gti->cmd.context_driver_cmd);
		break;
	case GTI_CMD_GET_CONTEXT_STYLUS:
		ret = gti->options.get_context_stylus(private_data, &gti->cmd.context_stylus_cmd);
		break;
	case GTI_CMD_GET_COORD_FILTER_ENABLED:
		ret = gti->options.get_coord_filter_enabled(private_data,
				&gti->cmd.coord_filter_cmd);
		break;
	case GTI_CMD_GET_FW_VERSION:
		ret = gti->options.get_fw_version(private_data, &gti->cmd.fw_version_cmd);
		break;
	case GTI_CMD_GET_GRIP_MODE:
		ret = gti->options.get_grip_mode(private_data, &gti->cmd.grip_cmd);
		break;
	case GTI_CMD_GET_INT2_MODE:
		ret = gti->options.get_int2_mode(private_data, &gti->cmd.int2_cmd);
		break;
	case GTI_CMD_GET_INT2_STATUS:
		ret = gti->options.get_int2_status(private_data, &gti->cmd.int2_status_cmd);
		break;
	case GTI_CMD_GET_IRQ_MODE:
		ret = gti->options.get_irq_mode(private_data, &gti->cmd.irq_cmd);
		break;
	case GTI_CMD_GET_PALM_MODE:
		ret = gti->options.get_palm_mode(private_data, &gti->cmd.palm_cmd);
		break;
	case GTI_CMD_GET_PANEL_ID:
		ret = gti->options.get_panel_id(
			private_data, &gti->cmd.panel_id_cmd);
		break;
	case GTI_CMD_GET_REPORT_RATE:
		ret = gti->options.get_report_rate(private_data, &gti->cmd.report_rate_cmd);
		break;
	case GTI_CMD_GET_SCAN_MODE:
		ret = gti->options.get_scan_mode(private_data, &gti->cmd.scan_cmd);
		break;
	case GTI_CMD_GET_SCREEN_PROTECTOR_MODE:
		ret = gti->options.get_screen_protector_mode(private_data,
				&gti->cmd.screen_protector_mode_cmd);
		break;
	case GTI_CMD_GET_SENSING_MODE:
		ret = gti->options.get_sensing_mode(private_data, &gti->cmd.sensing_cmd);
		break;
	case GTI_CMD_GET_SENSOR_DATA:
		if (gti->cmd.sensor_data_cmd.type & TOUCH_SCAN_TYPE_MUTUAL) {
			ret = gti->options.get_mutual_sensor_data(
				private_data, &gti->cmd.sensor_data_cmd);
		} else if (gti->cmd.sensor_data_cmd.type & TOUCH_SCAN_TYPE_SELF) {
			ret = gti->options.get_self_sensor_data(
				private_data, &gti->cmd.sensor_data_cmd);
		}
		break;
	case GTI_CMD_GET_SENSOR_DATA_MANUAL:
		if (gti->cmd.manual_sensor_data_cmd.type & TOUCH_SCAN_TYPE_MUTUAL) {
			ret = gti->options.get_mutual_sensor_data(
				private_data, &gti->cmd.manual_sensor_data_cmd);
		} else if (gti->cmd.manual_sensor_data_cmd.type & TOUCH_SCAN_TYPE_SELF) {
			ret = gti->options.get_self_sensor_data(
				private_data, &gti->cmd.manual_sensor_data_cmd);
		}
		break;
	case GTI_CMD_GET_VENDOR_REGISTER:
		ret = gti->options.get_vendor_register(private_data,
				&gti->cmd.vendor_register_cmd);
		break;
	case GTI_CMD_GET_TOUCH_VSYNC_HSYNC_FREQ:
		ret = gti->options.get_vsync_hsync_frequency(private_data,
							     &gti->cmd.vsync_hsync_frequency_cmd);
		break;
	case GTI_CMD_GET_WATER_MODE:
		ret = gti->options.get_water_mode(private_data, &gti->cmd.water_cmd);
		break;
	case GTI_CMD_NOTIFY_DISPLAY_STATE:
		ret = gti->options.notify_display_state(private_data,
				&gti->cmd.display_state_cmd);
		break;
	case GTI_CMD_NOTIFY_DISPLAY_VREFRESH:
		ret = gti->options.notify_display_vrefresh(private_data,
				&gti->cmd.display_vrefresh_cmd);
		break;
	case GTI_CMD_SET_CONTINUOUS_REPORT:
		ret = gti->options.set_continuous_report(private_data,
				&gti->cmd.continuous_report_cmd);
		break;
	case GTI_CMD_SET_COORD_FILTER_ENABLED:
		ret = gti->options.set_coord_filter_enabled(private_data,
				&gti->cmd.coord_filter_cmd);
		break;
	case GTI_CMD_SET_GESTURE_CONFIG:
		ret = gti->options.set_gesture_config(private_data, &gti->cmd.gesture_config_cmd);
		break;
	case GTI_CMD_SET_GRIP_MODE:
		GOOG_INFO(gti, "Set firmware grip %s",
				gti->cmd.grip_cmd.setting == GTI_GRIP_ENABLE ?
				"enabled" : "disabled");
		ret = gti->options.set_grip_mode(private_data, &gti->cmd.grip_cmd);
		break;
	case GTI_CMD_SET_HEATMAP_ENABLED:
		ret = gti->options.set_heatmap_enabled(private_data, &gti->cmd.heatmap_cmd);
		break;
	case GTI_CMD_SET_INT2_MODE:
		ret = gti->options.set_int2_mode(private_data, &gti->cmd.int2_cmd);
		break;
	case GTI_CMD_SET_IRQ_MODE:
		ret = gti->options.set_irq_mode(private_data, &gti->cmd.irq_cmd);
		break;
	case GTI_CMD_SET_PALM_MODE:
		GOOG_INFO(gti, "Set firmware palm %s",
				gti->cmd.palm_cmd.setting == GTI_PALM_ENABLE ?
				"enabled" : "disabled");
		ret = gti->options.set_palm_mode(private_data, &gti->cmd.palm_cmd);
		break;
	case GTI_CMD_SET_PANEL_SPEED_MODE:
		GOOG_INFO(gti, "Set panel speed mode: %s",
				gti->cmd.panel_speed_mode_cmd.setting == GTI_PANEL_SPEED_MODE_NS ?
				"NS" : "HS");
		ret = gti->options.set_panel_speed_mode(private_data, &gti->cmd.panel_speed_mode_cmd);
		break;
	case GTI_CMD_SET_REPORT_RATE:
		GOOG_INFO(gti, "Set touch report rate as %d Hz", gti->cmd.report_rate_cmd.setting);
		ret = gti->options.set_report_rate(private_data, &gti->cmd.report_rate_cmd);
		if (ret == 0) {
			if (gti->timestamp_correction_enabled) {
				gti->report_rate = gti->cmd.report_rate_cmd.setting;
				gti->frame_time = ktime_set(0, NSEC_PER_SEC / gti->report_rate);
			}
		}
		break;
	case GTI_CMD_SET_SCAN_MODE:
		ret = gti->options.set_scan_mode(private_data, &gti->cmd.scan_cmd);
		break;
	case GTI_CMD_SET_SCREEN_PROTECTOR_MODE:
		GOOG_INFO(gti, "Set screen protector mode %s",
				gti->cmd.screen_protector_mode_cmd.setting ==
				GTI_SCREEN_PROTECTOR_MODE_ENABLE
				? "enabled" : "disabled");
		ret = gti->options.set_screen_protector_mode(private_data,
				&gti->cmd.screen_protector_mode_cmd);
		break;
	case GTI_CMD_SET_SENSING_MODE:
		ret = gti->options.set_sensing_mode(private_data, &gti->cmd.sensing_cmd);
		break;
	case GTI_CMD_SET_WATER_MODE:
		ret = gti->options.set_water_mode(private_data, &gti->cmd.water_cmd);
		break;
	default:
		break;
	}

	/* Back to vendor default handler if no optional operation available. */
	if (ret == -ESRCH && gti->vendor_default_handler)
		ret = gti->vendor_default_handler(private_data, cmd_type, &gti->cmd);

	/* Take unsupported cmd_type as debug logs for compatibility check. */
	if (ret == -EOPNOTSUPP) {
		GOOG_DBG(gti, "unsupported request cmd_type %#x!\n", cmd_type);
		ret = 0;
	} else if (ret == -ESRCH) {
		GOOG_DBG(gti, "No handler for cmd_type %#x!\n", cmd_type);
		ret = 0;
	}

	return ret;
}

static bool goog_v4l2_read_frame_cb(struct v4l2_heatmap *v4l2)
{
	struct goog_touch_interface *gti = container_of(v4l2, struct goog_touch_interface, v4l2);
	bool ret = false;
	u32 v4l2_size = gti->v4l2.width * gti->v4l2.height * 2;

	if (gti->heatmap_buf && v4l2_size == gti->heatmap_buf_size) {
		memcpy(v4l2->frame, gti->heatmap_buf, v4l2_size);
		ret = true;
	} else {
		GOOG_LOGE(gti, "wrong pointer(%p) or size (W: %lu, H: %lu) vs %u\n",
		gti->heatmap_buf, gti->v4l2.width, gti->v4l2.height, gti->heatmap_buf_size);
	}

	return ret;
}

static void goog_v4l2_read(struct goog_touch_interface *gti, ktime_t timestamp, u64 frame_index)
{
	if (gti->v4l2_enabled) {
		gti->v4l2.frame_index = frame_index;
		heatmap_read(&gti->v4l2, ktime_to_ns(timestamp));
	}
}

static int goog_get_driver_status(struct goog_touch_interface *gti,
				  struct gti_context_driver_cmd *driver_cmd)
{
	gti->context_changed.offload_timestamp = 1;

	driver_cmd->context_changed.value = gti->context_changed.value;
	driver_cmd->screen_state = gti->display_state;
	driver_cmd->noise_state = gti->fw_status.noise_level;
	driver_cmd->water_mode = gti->fw_status.water_mode;
	driver_cmd->charger_state = gti->charger_state;
	driver_cmd->offload_timestamp = ktime_get();

	/* vendor driver overwrite the context */
	return goog_process_vendor_cmd(gti, GTI_CMD_GET_CONTEXT_DRIVER);
}

static int goog_offload_populate_coordinate_channel(struct goog_touch_interface *gti,
						    struct touch_offload_frame *frame, int channel)
{
	int i;
	struct TouchOffloadDataCoord *dc;

	if (channel < 0 || channel >= MAX_CHANNELS) {
		GOOG_LOGE(gti, "Invalid channel: %d\n", channel);
		return -EINVAL;
	}

	dc = (struct TouchOffloadDataCoord *)frame->channel_data[channel];
	memset(dc, 0, frame->channel_data_size[channel]);
	dc->header.channel_type = TOUCH_DATA_TYPE_COORD;
	dc->header.channel_size = TOUCH_OFFLOAD_FRAME_SIZE_COORD;

	for (i = 0; i < MAX_SLOTS; i++) {
		dc->coords[i].x = gti->offload.coords[i].x;
		dc->coords[i].y = gti->offload.coords[i].y;
		dc->coords[i].major = gti->offload.coords[i].major;
		dc->coords[i].minor = gti->offload.coords[i].minor;
		dc->coords[i].pressure = gti->offload.coords[i].pressure;
		dc->coords[i].rotation = gti->offload.coords[i].rotation;
		dc->coords[i].status = gti->offload.coords[i].status;
	}

	return 0;
}

static int goog_offload_populate_mutual_channel(struct goog_touch_interface *gti,
						struct touch_offload_frame *frame, int channel,
						u8 *buffer, u32 size)
{
	struct TouchOffloadData2d *mutual;

	if (channel < 0 || channel >= MAX_CHANNELS) {
		GOOG_LOGE(gti, "Invalid channel: %d\n", channel);
		return -EINVAL;
	}

	mutual = (struct TouchOffloadData2d *)frame->channel_data[channel];
	mutual->heatmap_width = gti->offload.caps.heatmap_width;
	mutual->heatmap_height = gti->offload.caps.heatmap_height;
	mutual->header.channel_type = frame->channel_type[channel];
	mutual->header.channel_size =
		TOUCH_OFFLOAD_FRAME_SIZE_2D(mutual->heatmap_height, mutual->heatmap_width);
	if (IS_ERR_OR_NULL(buffer) ||
		size != TOUCH_OFFLOAD_DATA_SIZE_2D(mutual->heatmap_height, mutual->heatmap_width)) {
		GOOG_LOGW(gti, "invalid buffer %p or size %u!\n", buffer, size);
		return -EINVAL;
	}
	memcpy(mutual->data_flex, buffer, size);

	return 0;
}

static int goog_offload_populate_self_channel(struct goog_touch_interface *gti,
					      struct touch_offload_frame *frame, int channel,
					      u8 *buffer, u32 size)
{
	struct TouchOffloadData1d *self;

	if (channel < 0 || channel >= MAX_CHANNELS) {
		GOOG_LOGE(gti, "Invalid channel: %d\n", channel);
		return -EINVAL;
	}

	self = (struct TouchOffloadData1d *)frame->channel_data[channel];
	self->heatmap_width = gti->offload.caps.heatmap_width;
	self->heatmap_height = gti->offload.caps.heatmap_height;
	self->header.channel_type = frame->channel_type[channel];
	self->header.channel_size =
		TOUCH_OFFLOAD_FRAME_SIZE_1D(self->heatmap_height, self->heatmap_width);
	if (IS_ERR_OR_NULL(buffer) ||
		size != TOUCH_OFFLOAD_DATA_SIZE_1D(self->heatmap_height, self->heatmap_width)) {
		GOOG_LOGW(gti, "invalid buffer %p or size %u!\n", buffer, size);
		return -EINVAL;
	}
	memcpy(self->data_flex, buffer, size);

	return 0;
}

static void goog_offload_populate_driver_status_channel(
		struct goog_touch_interface *gti,
		struct touch_offload_frame *frame, int channel,
		struct gti_context_driver_cmd *driver_cmd)
{
	struct TouchOffloadDriverStatus *ds =
		(struct TouchOffloadDriverStatus *)frame->channel_data[channel];

	memset(ds, 0, frame->channel_data_size[channel]);
	ds->header.channel_type = (u32)CONTEXT_CHANNEL_TYPE_DRIVER_STATUS;
	ds->header.channel_size = sizeof(struct TouchOffloadDriverStatus);

	ds->contents.screen_state = driver_cmd->context_changed.screen_state;
	ds->screen_state = driver_cmd->screen_state;

	ds->contents.display_refresh_rate = driver_cmd->context_changed.display_refresh_rate;
	ds->display_refresh_rate = driver_cmd->display_refresh_rate;

	ds->contents.touch_report_rate = driver_cmd->context_changed.touch_report_rate;
	ds->touch_report_rate = driver_cmd->touch_report_rate;

	ds->contents.noise_state = driver_cmd->context_changed.noise_state;
	ds->noise_state = driver_cmd->noise_state;

	ds->contents.water_mode = driver_cmd->context_changed.water_mode;
	ds->water_mode = driver_cmd->water_mode;

	ds->contents.charger_state = driver_cmd->context_changed.charger_state;
	ds->charger_state = driver_cmd->charger_state;

	ds->contents.offload_timestamp = driver_cmd->context_changed.offload_timestamp;
	ds->offload_timestamp = driver_cmd->offload_timestamp;
}

static void goog_offload_populate_stylus_status_channel(
		struct goog_touch_interface *gti,
		struct touch_offload_frame *frame, int channel,
		struct gti_context_stylus_cmd *stylus_cmd)
{
	struct TouchOffloadStylusStatus *ss =
		(struct TouchOffloadStylusStatus *)frame->channel_data[channel];

	memset(ss, 0, frame->channel_data_size[channel]);
	ss->header.channel_type = (u32)CONTEXT_CHANNEL_TYPE_STYLUS_STATUS;
	ss->header.channel_size = sizeof(struct TouchOffloadStylusStatus);

	ss->contents.coords = stylus_cmd->contents.coords;
	ss->coords[0] = stylus_cmd->pen_offload_coord;

	ss->contents.coords_timestamp = stylus_cmd->contents.coords_timestamp;
	ss->coords_timestamp = stylus_cmd->pen_offload_coord_timestamp;

	ss->contents.pen_paired = stylus_cmd->contents.pen_paired;
	ss->pen_paired = stylus_cmd->pen_paired;

	ss->contents.pen_active = stylus_cmd->contents.pen_active;
	ss->pen_active = stylus_cmd->pen_active;
}

static void goog_offload_populate_vendor_register_channel(struct goog_touch_interface *gti,
		struct touch_offload_frame *frame, int channel, u8 *buffer, u32 size)
{
	struct TouchOffloadVendorRegister *vr =
		(struct TouchOffloadVendorRegister *)frame->channel_data[channel];

	memset(vr, 0, frame->channel_data_size[channel]);
	vr->valid_size = size;
	vr->header.channel_type = frame->channel_type[channel];
	vr->header.channel_size = TOUCH_OFFLOAD_FRAME_SIZE_VENDOR_REGISTER;

	memcpy(vr->data, buffer, min(size, sizeof(vr->data)));
}

static int goog_get_sensor_data(struct goog_touch_interface *gti,
		struct gti_sensor_data_cmd *cmd, bool reset_data)
{
	int ret = 0;
	int err = 0;
	u16 tx = gti->offload.caps.heatmap_width;
	u16 rx = gti->offload.caps.heatmap_height;

	if (reset_data) {
		if (cmd->type == GTI_SENSOR_DATA_TYPE_MS) {
			cmd->size = TOUCH_OFFLOAD_DATA_SIZE_2D(rx, tx);
		} else if (cmd->type == GTI_SENSOR_DATA_TYPE_SS) {
			cmd->size = TOUCH_OFFLOAD_DATA_SIZE_1D(rx, tx);
		} else {
			ret = -EINVAL;
			goto exit;
		}

		memset(gti->heatmap_buf, 0, cmd->size);
		cmd->buffer = gti->heatmap_buf;
		goto exit;
	}

	err = goog_pm_wake_lock(gti, GTI_PM_WAKELOCK_TYPE_SENSOR_DATA, true);
	if (err < 0) {
		GOOG_WARN(gti, "Fail to lock GTI_PM_WAKELOCK_TYPE_SENSOR_DATA: %d!\n", err);
		ret = err;
		goto exit;
	}

	err = goog_process_vendor_cmd(gti, GTI_CMD_GET_SENSOR_DATA);
	if (err < 0) {
		GOOG_WARN(gti, "Fail to get sensor data: %d!\n", err);
		ret = err;
	}

	err = goog_pm_wake_unlock(gti, GTI_PM_WAKELOCK_TYPE_SENSOR_DATA);
	if (err < 0)
		GOOG_WARN(gti, "Fail to unlock GTI_PM_WAKELOCK_TYPE_SENSOR_DATA: %d!\n", err);

exit:
	return ret;
}

static void goog_offload_populate_frame(struct goog_touch_interface *gti,
					struct touch_offload_frame *frame, bool reset_data)
{
	char trace_tag[128];
	u32 channel_type;
	int i;
	int ret;
	struct gti_sensor_data_cmd *cmd = &gti->cmd.sensor_data_cmd;

	scnprintf(trace_tag, sizeof(trace_tag), "%s: IDX=%llu IN_TS=%lld.\n",
		__func__, gti->frame_index, gti->input_timestamp);
	ATRACE_BEGIN(trace_tag);

	frame->header.index = gti->frame_index;
	frame->header.timestamp = gti->input_timestamp;

	/*
	 * TODO(b/201610482):
	 * Porting for other channels, like driver status, stylus status
	 * and others.
	 */

	/* Populate all channels */
	for (i = 0; i < frame->num_channels; i++) {
		channel_type = frame->channel_type[i];
		GOOG_DBG(gti, "#%d: get data(type %#x) from vendor driver", i, channel_type);
		ret = 0;
		cmd->buffer = NULL;
		cmd->size = 0;
		if (channel_type == CONTEXT_CHANNEL_TYPE_DRIVER_STATUS) {
			ATRACE_BEGIN("populate driver context");
			ret = goog_get_driver_status(gti, &gti->cmd.context_driver_cmd);
			if (ret == 0)
				goog_offload_populate_driver_status_channel(
						gti, frame, i,
						&gti->cmd.context_driver_cmd);
			ATRACE_END();
		} else if (channel_type == CONTEXT_CHANNEL_TYPE_STYLUS_STATUS) {
			ATRACE_BEGIN("populate stylus context");
			ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_CONTEXT_STYLUS);
			if (ret == 0)
				goog_offload_populate_stylus_status_channel(
						gti, frame, i,
						&gti->cmd.context_stylus_cmd);
			ATRACE_END();
		} else if (channel_type == CONTEXT_CHANNEL_TYPE_VENDOR_REGISTER) {
			ATRACE_BEGIN("populate vendor register data");
			ret = goog_process_vendor_cmd(gti, GTI_CMD_GET_VENDOR_REGISTER);
			if (ret == 0)
				goog_offload_populate_vendor_register_channel(gti, frame, i,
					gti->cmd.vendor_register_cmd.data,
					gti->cmd.vendor_register_cmd.size);
			ATRACE_END();
		} else if (channel_type == TOUCH_DATA_TYPE_COORD) {
			ATRACE_BEGIN("populate coord");
			ret = goog_offload_populate_coordinate_channel(gti, frame, i);
			ATRACE_END();
		} else if (channel_type & TOUCH_SCAN_TYPE_MUTUAL) {
			ATRACE_BEGIN("populate mutual data");
			cmd->type = GTI_SENSOR_DATA_TYPE_MS;
			ret = goog_get_sensor_data(gti, cmd, reset_data);
			if (ret) {
				GOOG_LOGW(gti, "Fail to get data(type %#x, ret %d)!\n",
					cmd->type, ret);
				cmd->buffer = NULL;
			}
			ret = goog_offload_populate_mutual_channel(gti, frame, i,
				cmd->buffer, cmd->size);
			/* Backup strength data for v4l2. */
			if (ret == 0 && (channel_type & TOUCH_DATA_TYPE_STRENGTH))
				memcpy(gti->heatmap_buf, cmd->buffer, cmd->size);
			ATRACE_END();
		} else if (channel_type & TOUCH_SCAN_TYPE_SELF) {
			ATRACE_BEGIN("populate self data");
			cmd->type = GTI_SENSOR_DATA_TYPE_SS;
			ret = goog_get_sensor_data(gti, cmd, reset_data);
			if (ret) {
				GOOG_LOGW(gti, "Fail to get data(type %#x, ret %d)!\n",
					cmd->type, ret);
				cmd->buffer = NULL;
			}
			ret = goog_offload_populate_self_channel(gti, frame, i,
				cmd->buffer, cmd->size);
			ATRACE_END();
		} else {
			GOOG_ERR(gti, "unrecognized channel_type %#x.\n", channel_type);
		}

		/*
		 * TODO:
		 * Handle the error return for respective channel type and recycle
		 * the offload frame gracefully.
		 */
		if (ret) {
			GOOG_DBG(gti, "skip to populate data(type %#x, ret %d)!\n",
				channel_type, ret);
		}
	}

	ATRACE_END();
}

static void gti_update_fw_settings(struct goog_touch_interface *gti, bool force_update)
{
	int error;
	int ret = 0;
	u32 original_setting = 0;

	error = goog_pm_wake_lock_nosync(gti, GTI_PM_WAKELOCK_TYPE_FW_SETTINGS, true);
	if (error < 0) {
		GOOG_DBG(gti, "Error while obtaining FW_SETTINGS wakelock: %d!\n", error);
		return;
	}

	/*
	 * FW grip control
	 */
	if (!gti->ignore_grip_update) {
		original_setting = gti->cmd.grip_cmd.setting;
		if (gti->offload.offload_running && gti->offload.config.filter_grip)
			gti->cmd.grip_cmd.setting = GTI_GRIP_DISABLE;
		else
			gti->cmd.grip_cmd.setting = gti->default_grip_enabled;

		if (force_update || (original_setting != gti->cmd.grip_cmd.setting)) {
			ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_GRIP_MODE);
			if (ret)
				GOOG_LOGE(gti, "unexpected return(%d)!", ret);
		}
	}

	/*
	 * FW palm control
	 */
	if (!gti->ignore_palm_update) {
		original_setting = gti->cmd.palm_cmd.setting;
		if (gti->offload.offload_running && gti->offload.config.filter_palm)
			gti->cmd.palm_cmd.setting = GTI_PALM_DISABLE;
		else
			gti->cmd.palm_cmd.setting = gti->default_palm_enabled;
		if (force_update || (original_setting != gti->cmd.palm_cmd.setting)) {
			ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_PALM_MODE);
			if (ret)
				GOOG_LOGE(gti, "unexpected return(%d)!", ret);
		}
	}

	/*
	 * FW coord filter control
	 */
	original_setting = gti->cmd.coord_filter_cmd.setting;
	if (gti->offload.offload_running && gti->offload.config.coord_filter)
		gti->cmd.coord_filter_cmd.setting = GTI_COORD_FILTER_DISABLE;
	else
		gti->cmd.coord_filter_cmd.setting = gti->default_coord_filter_enabled;
	if (force_update || (original_setting != gti->cmd.coord_filter_cmd.setting)) {
		ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_COORD_FILTER_ENABLED);
		if (ret)
			GOOG_LOGE(gti, "unexpected return(%d)!", ret);
	}

	/*
	 * FW screen protector mode control
	 */
	original_setting = gti->cmd.screen_protector_mode_cmd.setting;
	gti->cmd.screen_protector_mode_cmd.setting = gti->screen_protector_mode_setting;
	if (force_update || (original_setting != gti->cmd.screen_protector_mode_cmd.setting)) {
		ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_SCREEN_PROTECTOR_MODE);
		if (ret != 0) {
			GOOG_ERR(gti, "Fail to %s screen protector mode!\n",
				 gti->screen_protector_mode_setting ==
						 GTI_SCREEN_PROTECTOR_MODE_ENABLE ?
					 "enable" :
					 "disable");
		}
	}

	/*
	 * FW heatmap control
	 */
	original_setting = gti->cmd.heatmap_cmd.setting;
	gti->cmd.heatmap_cmd.setting = GTI_HEATMAP_ENABLE;
	if (force_update || (original_setting != gti->cmd.heatmap_cmd.setting)) {
		ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_HEATMAP_ENABLED);
		if (ret != 0)
			GOOG_ERR(gti, "Fail to set heatmap enabled!\n");
	}

	/*
	 * FW sesning mode control
	 */
	if (gti->late_sense_on_enabled) {
		gti->cmd.sensing_cmd.setting = GTI_SENSING_MODE_ENABLE;
		ret = goog_process_vendor_cmd(gti, GTI_CMD_SET_SENSING_MODE);
		if (ret != 0)
			GOOG_ERR(gti, "Fail to enable sensing!\n");
	}

	error = goog_pm_wake_unlock_nosync(gti, GTI_PM_WAKELOCK_TYPE_FW_SETTINGS);
	if (error < 0)
		GOOG_DBG(gti, "Error while releasing FW_SETTINGS wakelock: %d!\n", error);
}

void gti_offload_set_running(struct goog_touch_interface *gti, bool running)
{
	if (gti->offload.offload_running != running) {
		GOOG_INFO(gti, "Set offload_running=%d irq_index=%llu input_index=%llu IDX=%llu\n",
			  running, gti->irq_index, gti->input_index, gti->frame_index);

		gti->offload.offload_running = running;

		gti_update_fw_settings(gti, false);
		gti_debug_offload_toggle_push(gti);
	}
}

static void goog_report_lptw_cancel(struct goog_touch_interface *gti,
		unsigned long slot_bit_cancel)
{
	int i = 0;
	int coord_x = (gti->lptw_track_min_x + gti->lptw_track_max_x) / 2;
	int coord_y = (gti->lptw_track_min_y + gti->lptw_track_max_y) / 2;

	if (!gti || !gti->vendor_input_dev)
		return;

	/* Early return if notification succeed, otherwise report it by input. */
	if (goog_notify_lptw_left(gti) == NOTIFY_OK)
		return;

	/* Skip reporting input cancel if the finger stays over 500ms. */
	if (ktime_after(ktime_get(), ktime_add_ms(gti->lptw_cancel_time, 500)))
		return;

	GOOG_INFO(gti, "Report LPTW cancel coord, slot: %#lx.", slot_bit_cancel);

	goog_input_lock(gti);
	gti_input_set_timestamp(gti, ktime_get());
	for (i = 0; i < MAX_SLOTS; i++) {
		if (!test_bit(i, &slot_bit_cancel))
			continue;
		/* Finger down. */
		input_mt_slot(gti->vendor_input_dev, i);
		input_report_key(gti->vendor_input_dev, BTN_TOUCH, 1);
		input_mt_report_slot_state(gti->vendor_input_dev, MT_TOOL_FINGER, 1);
		input_report_abs(gti->vendor_input_dev, ABS_MT_POSITION_X, coord_x);
		input_report_abs(gti->vendor_input_dev, ABS_MT_POSITION_Y, coord_y);
		input_report_abs(gti->vendor_input_dev, ABS_MT_TOUCH_MAJOR, 200);
		input_report_abs(gti->vendor_input_dev, ABS_MT_TOUCH_MINOR, 200);
		input_report_abs(gti->vendor_input_dev, ABS_MT_PRESSURE, 1);
		input_report_abs(gti->vendor_input_dev, ABS_MT_ORIENTATION, 0);
		input_sync(gti->vendor_input_dev);

		/* Report MT_TOOL_PALM for canceling the touch event. */
		input_mt_slot(gti->vendor_input_dev, i);
		input_report_key(gti->vendor_input_dev, BTN_TOUCH, 1);
		input_mt_report_slot_state(gti->vendor_input_dev, MT_TOOL_PALM, 1);
		input_sync(gti->vendor_input_dev);

		/* Release touches. */
		input_mt_slot(gti->vendor_input_dev, i);
		input_report_abs(gti->vendor_input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(gti->vendor_input_dev, MT_TOOL_FINGER, 0);
		input_report_abs(gti->vendor_input_dev, ABS_MT_TRACKING_ID, -1);
		input_report_key(gti->vendor_input_dev, BTN_TOUCH, 0);
		input_sync(gti->vendor_input_dev);
	}

	goog_input_unlock(gti);
}

static void goog_lptw_cancel_delayed_work(struct work_struct *work)
{
	struct goog_touch_interface *gti;
	struct delayed_work *delayed_work;
	delayed_work = container_of(work, struct delayed_work, work);
	gti = container_of(delayed_work, struct goog_touch_interface, lptw_cancel_delayed_work);

	gti->lptw_track_finger = false;
	goog_report_lptw_cancel(gti, 1);
}

static void goog_save_tracking_slot(struct goog_touch_interface *gti, u16 x, u16 y, int slot_bit)
{
	if ((x > gti->lptw_track_min_x) && (x < gti->lptw_track_max_x) &&
		(y > gti->lptw_track_min_y) && (y < gti->lptw_track_max_y)) {
		if (gti->slot_bit_lptw_track != 0) {
			GOOG_WARN(gti, "More than one finger in the tracking area, new slot:%#x",
					slot_bit);
			return;
		}
		set_bit(slot_bit, &gti->slot_bit_lptw_track);
		GOOG_INFO(gti, "LPTW track slot bit %#lx", gti->slot_bit_lptw_track);
	}
}

static void goog_offload_input_report(void *handle, struct TouchOffloadIocReport *report)
{
	struct goog_touch_interface *gti = (struct goog_touch_interface *)handle;
	bool touch_down = 0;
	unsigned int tool_type = MT_TOOL_FINGER;
	int i;
	int dx, dy;
	unsigned long slot_bit_active = 0;
	unsigned long slot_bit_cancel = 0;
	char trace_tag[128];
	char slot_trace_tag[128];
	ktime_t ktime = ktime_get();

	if (!gti || !gti->vendor_input_dev)
		return;

	scnprintf(trace_tag, sizeof(trace_tag),
		"%s: IDX=%llu IN_TS=%lld TS=%lld DELTA=%lld ns.\n",
		__func__, report->index,
		ktime_to_ns(report->timestamp), ktime_to_ns(ktime),
		ktime_to_ns(ktime_sub(ktime, report->timestamp)));
	ATRACE_BEGIN(trace_tag);

	if (gti->lptw_suppress_coords_enabled && gti->lptw_track_finger)
		cancel_delayed_work_sync(&gti->lptw_cancel_delayed_work);

	goog_input_lock(gti);

	if (ktime_before(report->timestamp, gti->input_dev_mono_ktime)) {
		GOOG_WARN(gti, "Drop obsolete input(IDX=%llu IN_TS=%lld TS=%lld DELTA=%lld ns)!\n",
			report->index,
			ktime_to_ns(report->timestamp),
			ktime_to_ns(gti->input_dev_mono_ktime),
			ktime_to_ns(ktime_sub(gti->input_dev_mono_ktime, report->timestamp)));
		goog_input_unlock(gti);
		ATRACE_END();
		return;
	}

	gti_input_set_timestamp(gti, report->timestamp);
	for (i = 0; i < MAX_SLOTS; i++) {
		if (report->coords[i].status != COORD_STATUS_INACTIVE) {
			switch (report->coords[i].status) {
			case COORD_STATUS_EDGE:
			case COORD_STATUS_PALM:
			case COORD_STATUS_CANCEL:
				tool_type = MT_TOOL_PALM;
				break;
			case COORD_STATUS_FINGER:
			case COORD_STATUS_PEN:
			default:
				tool_type = MT_TOOL_FINGER;
				break;
			}
			set_bit(i, &slot_bit_active);

			if (gti->lptw_suppress_coords_enabled) {
				if (gti->lptw_track_finger) {
					goog_save_tracking_slot(gti, report->coords[i].x,
							report->coords[i].y, i);
				}

				if (test_bit(i, &gti->slot_bit_lptw_track)) {
					goog_track_lptw_slot(gti, report->coords[i].x,
							report->coords[i].y, i);
					GOOG_DBG(gti, "Skip reporting lptw tracking slot %d", i);
					continue;
				}
			}

			if (gti->vendor_input_dev->mt &&
			    input_mt_is_active(&gti->vendor_input_dev->mt->slots[i])) {
				dx = report->coords[i].x -
				     input_mt_get_value(&gti->vendor_input_dev->mt->slots[i],
							ABS_MT_POSITION_X);
				dy = report->coords[i].y -
				     input_mt_get_value(&gti->vendor_input_dev->mt->slots[i],
							ABS_MT_POSITION_Y);
				scnprintf(slot_trace_tag, sizeof(slot_trace_tag),
					  "Slot[%d] dx:%d dy:%d", i, dx, dy);
				ATRACE_BEGIN(slot_trace_tag);
				ATRACE_END();
			}

			input_mt_slot(gti->vendor_input_dev, i);
			touch_down = 1;
			input_report_key(gti->vendor_input_dev, BTN_TOUCH, touch_down);
			input_mt_report_slot_state(gti->vendor_input_dev, tool_type, 1);
			input_report_abs(gti->vendor_input_dev, ABS_MT_POSITION_X,
				report->coords[i].x);
			input_report_abs(gti->vendor_input_dev, ABS_MT_POSITION_Y,
				report->coords[i].y);
			input_report_abs(gti->vendor_input_dev, ABS_MT_TOUCH_MAJOR,
				report->coords[i].major);
			input_report_abs(gti->vendor_input_dev, ABS_MT_TOUCH_MINOR,
				report->coords[i].minor);
			input_report_abs(gti->vendor_input_dev, ABS_MT_PRESSURE,
				max_t(int, 1, report->coords[i].pressure));
			if (report->coords[i].pressure == 0)
				GOOG_WARN(gti, "Unexpected ZERO pressure reporting(slot#%d)!", i);
			if (gti->offload.caps.rotation_reporting)
				input_report_abs(gti->vendor_input_dev, ABS_MT_ORIENTATION,
					report->coords[i].rotation);
		} else {
			clear_bit(i, &slot_bit_active);
			if (gti->lptw_suppress_coords_enabled &&
					test_and_clear_bit(i, &gti->slot_bit_lptw_track)) {
				set_bit(i, &slot_bit_cancel);
				if (gti->slot_bit_lptw_track == 0)
					GOOG_INFO(gti, "All lptw tracking slots released");
				continue;
			}

			input_mt_slot(gti->vendor_input_dev, i);
			input_report_abs(gti->vendor_input_dev, ABS_MT_PRESSURE, 0);
			/*
			 * Force to cancel the active figner(s) by MT_TOOL_PALM during screen-off.
			 */
			if (gti->display_state == GTI_DISPLAY_STATE_OFF &&
				gti->vendor_input_dev->mt &&
				input_mt_is_active(&gti->vendor_input_dev->mt->slots[i])) {
				input_mt_report_slot_state(gti->vendor_input_dev, MT_TOOL_PALM, 1);
				input_sync(gti->vendor_input_dev);
			}
			input_mt_report_slot_state(gti->vendor_input_dev, MT_TOOL_FINGER, 0);
		}
	}
	input_report_key(gti->vendor_input_dev, BTN_TOUCH, touch_down);
	input_sync(gti->vendor_input_dev);
	goog_input_unlock(gti);

	if (touch_down)
		goog_v4l2_read(gti, report->timestamp, report->index);

	if (gti->lptw_suppress_coords_enabled) {
		if (slot_bit_cancel || (gti->lptw_track_finger && gti->slot_bit_lptw_track == 0))
			goog_report_lptw_cancel(gti, slot_bit_cancel);
		gti->lptw_track_finger = false;
	}
	ATRACE_END();
}

static int gti_update_charger_state(struct goog_touch_interface *gti,
	struct power_supply *psy)
{
	union power_supply_propval present_val = { 0 };
	int ret = 0;

	if (gti == NULL || psy == NULL)
		return -ENODEV;

	ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT,
		&present_val);
	if (ret < 0) {
		GOOG_WARN(gti,
			"Error while getting power supply property: %d!\n", ret);
	} else if ((u8)present_val.intval != gti->charger_state) {
		/* Note: the expected values for present_val.intval are
		 * 0 and 1. Cast to unsigned byte to ensure the
		 * comparison is handled in the same variable data type.
		 */
		GOOG_INFO(gti, "Charger_state changed from %d to %d\n",
			gti->charger_state, present_val.intval);
		gti->context_changed.charger_state = 1;
		gti->charger_state = (u8)present_val.intval;
	}
	return ret;
}

static int gti_charger_state_change(struct notifier_block *nb, unsigned long action, void *data)
{
	struct goog_touch_interface *gti =
		(struct goog_touch_interface *)container_of(nb,
			struct goog_touch_interface, charger_notifier);
	struct power_supply *psy = (struct power_supply *)data;

	/* Attempt actual status parsing */
	if (psy && psy->desc && action == PSY_EVENT_PROP_CHANGED &&
			!strcmp(psy->desc->name, gti->usb_psy_name)) {
		gti_update_charger_state(gti, psy);
	}
	return NOTIFY_DONE;
}

static int gti_offload_probe(struct goog_touch_interface *gti, struct device_node *dn)
{
	int ret = 0;
	int err = 0;
	u16 values[2];
	const char *offload_dev_name = NULL;
	struct of_phandle_args;
	static u8 *offload_ids_array;
	int offload_ids_size;
	int id_size;
	const char *usb_psy_name = NULL;
	bool is_default_offload_id = false;

	if (!gti || !gti->dev || !gti->vendor_dev || !gti->vendor_input_dev || !dn) {
		GOOG_LOGE(gti, "invalid interface context!");
		return -EINVAL;
	}

	/*
	 * TODO(b/201610482): rename DEVICE_NAME in touch_offload.h for more specific.
	 */
	if (!of_property_read_string(dn, "goog,offload-device-name", &offload_dev_name)) {
		scnprintf(gti->offload.device_name, sizeof(gti->offload.device_name),
			"%s_%s", DEVICE_NAME, offload_dev_name);
	}

	gti->panel_map_from_tic = of_property_read_bool(dn, "goog,panel-map-from-tic");
	if (gti->panel_map_from_tic) {
		if (gti->vendor_private_data == NULL)
			GOOG_LOGE(gti, "Invalid panel_map_from_tic ops before gti_connect()!\n");
		else
			gti->panel_id = goog_get_panel_id_from_tic(gti);
	} else {
		gti->panel_id = goog_get_panel_id(dn);
	}

	offload_ids_size = of_property_count_u8_elems(dn, "goog,touch_offload_ids");
	if (offload_ids_size > 0 && gti->panel_id >= 0) {
		id_size = sizeof(gti->offload_id);

		if (!offload_ids_array)
			offload_ids_array = devm_kzalloc(gti->dev, offload_ids_size, GFP_KERNEL);
		if (offload_ids_array == NULL) {
			GOOG_WARN(gti, "Fail to alloc offload_ids_array");
			err = -ENOMEM;
		} else {
			err = of_property_read_u8_array(dn, "goog,touch_offload_ids",
							offload_ids_array, offload_ids_size);
			if (err == 0) {
				if (id_size * (gti->panel_id + 1) <= offload_ids_size) {
					memcpy(&gti->offload_id, offload_ids_array + id_size * gti->panel_id, id_size);
				} else {
					GOOG_WARN(gti, "Panel id is invalid, id: %d, ids size: %d",
							gti->panel_id, offload_ids_size);
					err = -EINVAL;
				}
			} else {
				GOOG_WARN(gti, "Fail to read touch_offload_ids");
			}
		}
	} else {
		err = of_property_read_u8_array(dn, "goog,touch_offload_id", gti->offload_id_byte,
						4);
	}

	if (err < 0) {
		GOOG_INFO(gti, "set default offload id: GOOG!\n");
		gti->offload_id_byte[0] = 'G';
		gti->offload_id_byte[1] = 'O';
		gti->offload_id_byte[2] = 'O';
		gti->offload_id_byte[3] = 'G';
		is_default_offload_id = true;
	}

	gti->offload.caps.touch_offload_major_version = TOUCH_OFFLOAD_INTERFACE_MAJOR_VERSION;
	gti->offload.caps.touch_offload_minor_version = TOUCH_OFFLOAD_INTERFACE_MINOR_VERSION;
	gti->offload.caps.device_id = gti->offload_id;
	gti->offload.caps.touch_width =
		input_abs_get_max(gti->vendor_input_dev, ABS_MT_POSITION_X) + 1;
	gti->offload.caps.touch_height =
		input_abs_get_max(gti->vendor_input_dev, ABS_MT_POSITION_Y) + 1;

	if (of_property_read_u16_array(dn, "goog,channel-num", values, 2) == 0) {
		gti->offload.caps.heatmap_width = values[0];
		gti->offload.caps.heatmap_height = values[1];
	} else {
		GOOG_ERR(gti, "Please set \"goog,channel-num\" in dts!");
		/*
		 * TODO(b/356993163): Refine the GTI capability.
		 */
		gti->offload.caps.tx_size = 50;
		gti->offload.caps.rx_size = 50;
	}

	/*
	 * TODO(b/201610482): Set more offload caps from parameters or from dtsi?
	 */
	gti->offload.caps.heatmap_size = HEATMAP_SIZE_FULL;
	gti->offload.caps.bus_type = BUS_TYPE_SPI;
	if (of_property_read_u32(dn, "spi-max-frequency", &gti->offload.caps.bus_speed_hz))
		gti->offload.caps.bus_speed_hz = 0;

	if (is_default_offload_id || of_property_read_u16(dn, "goog,offload-caps-data-types",
							  &gti->offload.caps.touch_data_types)) {
		gti->offload.caps.touch_data_types =
			TOUCH_DATA_TYPE_COORD | TOUCH_DATA_TYPE_STRENGTH |
			TOUCH_DATA_TYPE_RAW | TOUCH_DATA_TYPE_BASELINE |
			TOUCH_DATA_TYPE_FILTERED;
	}
	if (is_default_offload_id || of_property_read_u16(dn, "goog,offload-caps-scan-types",
							  &gti->offload.caps.touch_scan_types)) {
		gti->offload.caps.touch_scan_types =
			TOUCH_SCAN_TYPE_MUTUAL | TOUCH_SCAN_TYPE_SELF;
	}
	if (is_default_offload_id ||
	    of_property_read_u16(dn, "goog,offload-caps-context-channel-types",
				 &gti->offload.caps.context_channel_types)) {
		gti->offload.caps.context_channel_types =
			CONTEXT_CHANNEL_TYPE_DRIVER_STATUS;
	}
	GOOG_INFO(gti, "offload.caps: data_types %#x, scan_types %#x, context_channel_types %#x.\n",
		gti->offload.caps.touch_data_types,
		gti->offload.caps.touch_scan_types,
		gti->offload.caps.context_channel_types);

	gti->offload.caps.continuous_reporting = true;
	gti->offload.caps.noise_reporting = false;
	gti->offload.caps.cancel_reporting =
		!of_property_read_bool(dn, "goog,offload-caps-cancel-reporting-disabled");
	gti->offload.caps.size_reporting = true;
	gti->offload.caps.filter_grip = true;
	gti->offload.caps.filter_palm = true;
	gti->offload.caps.coord_filter =
		of_property_read_bool(dn, "goog,offload-caps-coord-filter");
	gti->offload.caps.num_sensitivity_settings = 1;
	gti->offload.caps.rotation_reporting =
		!of_property_read_bool(dn, "goog,offload-caps-rotation-reporting-disabled");

	gti->offload.hcallback = (void *)gti;
	gti->offload.report_cb = goog_offload_input_report;
	ret = touch_offload_init(&gti->offload);
	if (ret) {
		GOOG_ERR(gti, "offload init failed, ret %d!\n", ret);
		goto err_offload_probe;
	}

	gti->offload_enabled = of_property_read_bool(dn, "goog,offload-enabled");
	GOOG_INFO(gti, "offload.caps: Coord W/H: %d * %d (Heatmap W/H: %d * %d).\n",
		gti->offload.caps.touch_width, gti->offload.caps.touch_height,
		gti->offload.caps.heatmap_width, gti->offload.caps.heatmap_height);

	GOOG_INFO(gti, "offload ID: 0x%02x %02x %02x %02x, offload_enabled=%d.\n",
		gti->offload_id_byte[0], gti->offload_id_byte[1], gti->offload_id_byte[2],
		gti->offload_id_byte[3], gti->offload_enabled);

	GOOG_INFO(gti, "offload.caps: version: %u.%u\n",
		gti->offload.caps.touch_offload_major_version,
		gti->offload.caps.touch_offload_minor_version);

	gti->default_grip_enabled = of_property_read_bool(dn, "goog,default-grip-disabled") ?
					    GTI_GRIP_DISABLE :
					    GTI_GRIP_ENABLE;
	gti->default_palm_enabled = of_property_read_bool(dn, "goog,default-palm-disabled") ?
					    GTI_PALM_DISABLE :
					    GTI_PALM_ENABLE;
	gti->default_coord_filter_enabled =
		of_property_read_bool(dn, "goog,default-coord-filter-disabled") ?
			GTI_COORD_FILTER_DISABLE :
			GTI_COORD_FILTER_ENABLE;

	gti->heatmap_buf_size =
		gti->offload.caps.heatmap_width * gti->offload.caps.heatmap_height * sizeof(u16);
	gti->heatmap_buf = devm_kzalloc(gti->dev, gti->heatmap_buf_size, GFP_KERNEL);
	if (!gti->heatmap_buf) {
		GOOG_ERR(gti, "heamap alloc failed!\n");
		ret = -ENOMEM;
		goto err_offload_probe;
	}

	/*
	 * Heatmap_probe must be called before irq routine is registered,
	 * because heatmap_read is called from the irq context.
	 * If the ISR runs before heatmap_probe is finished, it will invoke
	 * heatmap_read and cause NPE, since read_frame would not yet be set.
	 */
	gti->v4l2.parent_dev = gti->vendor_dev;
	gti->v4l2.input_dev = gti->vendor_input_dev;
	gti->v4l2.read_frame = goog_v4l2_read_frame_cb;
	gti->v4l2.width = gti->offload.caps.heatmap_width;
	gti->v4l2.height = gti->offload.caps.heatmap_height;
	gti->v4l2.frame_index_enabled = true;
	gti->v4l2.timeperframe.numerator = 1;
	gti->v4l2.timeperframe.denominator = 120;

	ret = heatmap_probe(&gti->v4l2);
	if (ret) {
		GOOG_ERR(gti, "v4l2 init failed, ret %d!\n", ret);
		goto err_offload_probe;
	}
	gti->v4l2_enabled = of_property_read_bool(dn, "goog,v4l2-enabled");
	GOOG_INFO(gti, "v4l2 W/H=(%lu, %lu), v4l2_enabled=%d.\n",
		gti->v4l2.width, gti->v4l2.height, gti->v4l2_enabled);

	if (!of_property_read_string(dn, "goog,usb-psy-name", &usb_psy_name))
		strscpy(gti->usb_psy_name, usb_psy_name, sizeof(gti->usb_psy_name));
	else
		strscpy(gti->usb_psy_name, "usb", sizeof(gti->usb_psy_name));

	/* Register for charger plugging status */
	gti->charger_notifier.notifier_call = gti_charger_state_change;
	ret = power_supply_reg_notifier(&gti->charger_notifier);
	if (ret) {
		GOOG_ERR(gti, "Fail to register power_supply_reg_notifier!\n");
		goto err_offload_probe;
	}

	gti_update_charger_state(gti,
		power_supply_get_by_name(gti->usb_psy_name));

err_offload_probe:
	return ret;
}

static void gti_offload_remove(struct goog_touch_interface *gti)
{
	if (!gti || !gti->dev)
		return;

	gti->offload_enabled = false;
	gti->v4l2_enabled = false;
	power_supply_unreg_notifier(&gti->charger_notifier);
	touch_offload_cleanup(&gti->offload);
	heatmap_remove(&gti->v4l2);
	devm_kfree(gti->dev, gti->heatmap_buf);
}

static void goog_input_coordinate_report(struct goog_touch_interface *gti,
					 struct TouchOffloadCoord *coords)
{
	int i;
	int touch_down = 0;

	if (!gti || !gti->vendor_input_dev)
		return;

	for (i = 0; i < MAX_SLOTS; i++) {
		input_mt_slot(gti->vendor_input_dev, i);
		if (coords[i].status != COORD_STATUS_INACTIVE) {
			touch_down |= 1;
			input_report_key(gti->vendor_input_dev, BTN_TOUCH, touch_down);
			input_mt_report_slot_state(gti->vendor_input_dev, MT_TOOL_FINGER, true);
			input_report_abs(gti->vendor_input_dev,
				ABS_MT_POSITION_X, coords[i].x);
			input_report_abs(gti->vendor_input_dev,
				ABS_MT_POSITION_Y, coords[i].y);
			input_report_abs(gti->vendor_input_dev,
				ABS_MT_TOUCH_MAJOR, coords[i].major);
			input_report_abs(gti->vendor_input_dev,
				ABS_MT_TOUCH_MINOR, coords[i].minor);
			input_report_abs(gti->vendor_input_dev,
				ABS_MT_PRESSURE, max_t(int, 1, coords[i].pressure));
			if (gti->offload.caps.rotation_reporting)
				input_report_abs(gti->vendor_input_dev, ABS_MT_ORIENTATION,
					coords[i].rotation);
		} else {
			input_report_abs(gti->vendor_input_dev, ABS_MT_PRESSURE, 0);
			input_mt_report_slot_state(gti->vendor_input_dev, MT_TOOL_FINGER, false);
		}
	}
	input_report_key(gti->vendor_input_dev, BTN_TOUCH, touch_down);
	input_sync(gti->vendor_input_dev);
}

static void goog_input_flush_offload_fingers(struct goog_touch_interface *gti)
{
	ktime_t timestamp;

	goog_input_lock(gti);
	if (gti->input_timestamp_changed) {
		timestamp = gti->input_timestamp;
	} else {
		GOOG_WARN(gti, "No timestamp set by vendor driver before input report!");
		timestamp = ktime_get();
	}
	gti_input_set_timestamp(gti, timestamp);
	goog_input_coordinate_report(gti, gti->offload.coords);

	goog_input_unlock(gti);
}

int goog_input_process(struct goog_touch_interface *gti, bool reset_data)
{
	int ret = 0;
	struct touch_offload_frame **frame = &gti->offload_frame;
	bool input_flush;

	/*
	 * Only do the input process if active slot(s) update
	 * or slot(s) state change or resetting frame data.
	 */
	if (!(gti->slot_bit_active & gti->slot_bit_in_use) &&
		!gti->slot_bit_changed && !reset_data)
		return -EPERM;

	mutex_lock(&gti->input_process_lock);
	/*
	 * Increase the frame index after the offload running successfully once.
	 * This is to ignore the dummy offload frame before offload driver
	 * configuration complete.
	 */
	if (likely(gti->frame_index != 0) || (gti->offload.offload_running == true))
		gti->frame_index++;

	/*
	 * Increase the input index when any slot bit changed which
	 * means the finger is down or up.
	 */
	if (gti->slot_bit_changed)
		gti->input_index++;

	/*
	 * Flush offload coords back to legacy input reporting for the
	 * following cases:
	 * 1. offload_enabled is disabled.
	 * 2. Fail to reserve frame.
	 * 3. Fail to queue frame.
	 * Otherwise, goog_offload_input_report() will report coords later.
	 */
	input_flush = true;
	if (gti->offload_enabled) {
		ret = touch_offload_reserve_frame(&gti->offload, frame);
		if (ret != 0 || frame == NULL) {
			if (gti->offload.offload_running && gti->debug_warning_limit) {
				gti->debug_warning_limit--;
				GOOG_WARN(gti, "offload: No buffers available, ret=%d IDX=%llu!\n",
					ret, gti->frame_index);
			}
			gti_offload_set_running(gti, false);
			ret = -EBUSY;
		} else {
			if (!gti->offload.offload_running)
				gti->debug_warning_limit = TOUCH_OFFLOAD_BUFFER_NUM;
			gti_offload_set_running(gti, true);
			goog_offload_populate_frame(gti, *frame, reset_data);
			ret = touch_offload_queue_frame(&gti->offload, *frame);
			if (ret) {
				GOOG_WARN(gti, "Fail to queue frame, ret=%d IDX=%llu!\n",
					ret, gti->frame_index);
			} else {
				gti->offload_frame = NULL;
				input_flush = false;
			}
		}
	}
	if (input_flush)
		goog_input_flush_offload_fingers(gti);

	/*
	 * If offload is NOT running, read heatmap directly by callback.
	 * Otherwise, heatmap will be handled for both offload and v4l2
	 * during goog_offload_populate_frame().
	 */
	if (!gti->offload.offload_running && gti->v4l2_enabled) {
		int ret;
		struct gti_sensor_data_cmd *cmd = &gti->cmd.sensor_data_cmd;

		cmd->buffer = NULL;
		cmd->size = 0;
		cmd->type = GTI_SENSOR_DATA_TYPE_MS;
		ret = goog_get_sensor_data(gti, cmd, reset_data);
		if (ret == 0 && cmd->buffer && cmd->size)
			memcpy(gti->heatmap_buf, cmd->buffer, cmd->size);
		goog_v4l2_read(gti, gti->input_timestamp, gti->frame_index);
	}

	gti_debug_input_update(gti);
	gti->input_timestamp_changed = false;
	gti->slot_bit_in_use = 0;

	mutex_unlock(&gti->input_process_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(goog_input_process);

void goog_input_lock(struct goog_touch_interface *gti)
{
	if (!gti)
		return;
	mutex_lock(&gti->input_lock);
}
EXPORT_SYMBOL_GPL(goog_input_lock);

void goog_input_unlock(struct goog_touch_interface *gti)
{
	if (!gti)
		return;
	mutex_unlock(&gti->input_lock);
}
EXPORT_SYMBOL_GPL(goog_input_unlock);

static int pid_controller_init(struct pid_controller *pid, s64 kp, s64 ki, s64 kd, s64 div)
{
	pid->k1 = kp + ki + kd;
	pid->k2 = -1 * kp - 2 * kd;
	pid->k3 = kd;
	pid->div = div;
	return 0;
}

static s64 pid_controller_process(struct pid_controller *pid, s64 e)
{
	pid->e2 = pid->e1;
	pid->e1 = pid->e0;
	pid->e0 = e;
	return (pid->k1 * pid->e0 + pid->k2 * pid->e1 + pid->k3 * pid->e2) / pid->div;
}

void goog_input_set_timestamp(
		struct goog_touch_interface *gti,
		struct input_dev *dev, ktime_t timestamp)
{
	s64 dt = 0;
	s64 dt_sensing = 0;
	s64 e = 0;
	s64 u = 0;
	ktime_t timestamp_corrected;
	s64 max_dt = 100 * NSEC_PER_MSEC;
	s64 u_limit = 100 * NSEC_PER_USEC;

	if (!gti) {
		input_set_timestamp(dev, timestamp);
		return;
	}

	if (gti->timestamp_correction_enabled) {
		ATRACE_BEGIN("do timestamp correction");
		dt = ktime_to_ns(ktime_sub(timestamp, gti->input_timestamp));
		dt_sensing = gti->sensing_timestamp - gti->last_sensing_timestamp;
		/*
		 * 1. If this is first finger down, skip correction.
		 * 2. If delta time is bigger than 100ms, skip correction. The related time
		 *    doesn't matter in this case.
		 * 3. If sensing time is not ready, skip correction.
		 */
		if (gti->sensing_timestamp_changed && gti->slot_bit_active &&
				dt > -max_dt && dt < max_dt &&
				dt_sensing > -max_dt && dt_sensing < max_dt) {
			timestamp_corrected = ktime_add_ns(gti->input_timestamp, dt_sensing);

			/*
			 * Use closed-loop control and pid controller to track host timestamp.
			 * Because host clock and TIC clock are not synced. There will be a drift
			 * over time.
			 *
			 * input: timestamp
			 * output: timestamp_corrected
			 * closed-loop error: e = timestamp - timestamp_corrected
			 * control signal: u = pid(e)
			 */
			e = ktime_to_ns(ktime_sub(timestamp, timestamp_corrected));
			u = pid_controller_process(&gti->pid, e);

			/*
			 * Limit the maximum and minimum u to reduce the jitter of report rate.
			 */
			if (u > u_limit)
				u = u_limit;
			else if (u < -u_limit)
				u = -u_limit;

			timestamp_corrected = ktime_add_ns(timestamp_corrected, u);

			dt = ktime_to_ns(ktime_sub(timestamp, timestamp_corrected));
			if (dt > 2 * ktime_to_ns(gti->frame_time)) {
				timestamp_corrected = ktime_sub_ns(timestamp,
						2 * ktime_to_ns(gti->frame_time));
			} else if (dt < -2 * NSEC_PER_MSEC) {
				timestamp_corrected = ktime_add_ns(timestamp, 2 * NSEC_PER_MSEC);
			}

			timestamp = timestamp_corrected;
		}
		ATRACE_END();
	}

	if (ktime_to_ns(ktime_sub(timestamp, gti->input_timestamp)) <= 0) {
		GOOG_WARN(
			gti,
			"Input timestamp jumped backward! cur_input: %lld (prev: %lld), cur_sensing: %llu (prev: %llu)",
			(long long)ktime_to_ns(timestamp),
			(long long)ktime_to_ns(gti->input_timestamp), gti->sensing_timestamp,
			gti->last_sensing_timestamp);
		gti->input_timestamp = ktime_add_ns(gti->input_timestamp, NSEC_PER_USEC);
	} else {
		gti->input_timestamp = timestamp;
	}

	gti->input_timestamp_changed = true;
	gti->sensing_timestamp_changed = false;
}
EXPORT_SYMBOL_GPL(goog_input_set_timestamp);

void goog_input_set_sensing_timestamp(
		struct goog_touch_interface *gti,
		struct input_dev *dev, u64 timestamp)
{
	if (gti == NULL)
		return;

	if (timestamp < gti->sensing_timestamp) {
		GOOG_ERR(gti,
			"Invalid timestamp. The timestamps must be monotonic, prev: %llu new: %llu\n",
			gti->sensing_timestamp, timestamp);
		return;
	}

	gti->last_sensing_timestamp = gti->sensing_timestamp;
	gti->sensing_timestamp = timestamp;
	gti->sensing_timestamp_changed = true;
}
EXPORT_SYMBOL_GPL(goog_input_set_sensing_timestamp);

void goog_input_mt_slot(
		struct goog_touch_interface *gti,
		struct input_dev *dev, int slot)
{
	if (!gti) {
		input_mt_slot(dev, slot);
		return;
	}

	if (slot < 0 || slot >= MAX_SLOTS) {
		GOOG_ERR(gti, "Invalid slot: %d\n", slot);
		return;
	}

	gti->slot = slot;
	/*
	 * Make sure the input timestamp should be set before updating 1st mt_slot.
	 * This is for input report switch between offload and legacy.
	 */
	if (!gti->slot_bit_in_use && !gti->input_timestamp_changed)
		GOOG_ERR(gti, "please exec goog_input_set_timestamp before %s!\n", __func__);
	set_bit(slot, &gti->slot_bit_in_use);
}
EXPORT_SYMBOL_GPL(goog_input_mt_slot);

void goog_input_mt_report_slot_state(
		struct goog_touch_interface *gti,
		struct input_dev *dev, unsigned int tool_type, bool active)
{
	if (!gti) {
		input_mt_report_slot_state(dev, tool_type, active);
		return;
	}

	switch (tool_type) {
	case MT_TOOL_FINGER:
		if (active) {
			gti->offload.coords[gti->slot].status = COORD_STATUS_FINGER;
			if (!test_and_set_bit(gti->slot,
					&gti->slot_bit_active)) {
				set_bit(gti->slot, &gti->slot_bit_changed);
			}
		} else {
			gti->offload.coords[gti->slot].status = COORD_STATUS_INACTIVE;
			if (test_and_clear_bit(gti->slot,
					&gti->slot_bit_active)) {
				set_bit(gti->slot, &gti->slot_bit_changed);
			}
		}
		break;

	default:
		GOOG_WARN(gti, "unexcepted input tool_type(%#x) active(%d)!\n",
			tool_type, active);
		break;
	}
}
EXPORT_SYMBOL_GPL(goog_input_mt_report_slot_state);

void goog_input_report_abs(
		struct goog_touch_interface *gti,
		struct input_dev *dev, unsigned int code, int value)
{
	if (!gti) {
		input_report_abs(dev, code, value);
		return;
	}

	switch (code) {
	case ABS_MT_POSITION_X:
		gti->offload.coords[gti->slot].x = value;
		if ((value > gti->abs_x_max) || (value < gti->abs_x_min)) {
			GOOG_WARN(gti, "Unexpected x-coord (slot#%d range#(%d, %d)), x: %d!",
					gti->slot, gti->abs_x_min, gti->abs_x_max, value);
		}
		break;
	case ABS_MT_POSITION_Y:
		gti->offload.coords[gti->slot].y = value;
		if ((value > gti->abs_y_max) || (value < gti->abs_y_min)) {
			GOOG_WARN(gti, "Unexpected y-coord (slot#%d range#(%d, %d)), y: %d!",
					gti->slot, gti->abs_y_min, gti->abs_y_max, value);
		}
		break;
	case ABS_MT_TOUCH_MAJOR:
		gti->offload.coords[gti->slot].major =
                    value * gti->resolution_scale_factor;
		break;
	case ABS_MT_TOUCH_MINOR:
		gti->offload.coords[gti->slot].minor =
                    value * gti->resolution_scale_factor;
		break;
	case ABS_MT_PRESSURE:
		gti->offload.coords[gti->slot].pressure = value;
		break;
	case ABS_MT_ORIENTATION:
		gti->offload.coords[gti->slot].rotation = value;
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(goog_input_report_abs);

void goog_input_report_key(
		struct goog_touch_interface *gti,
		struct input_dev *dev, unsigned int code, int value)
{
	if (!gti) {
		input_report_key(dev, code, value);
		return;
	}
}
EXPORT_SYMBOL_GPL(goog_input_report_key);

void goog_input_sync(struct goog_touch_interface *gti, struct input_dev *dev)
{
	if (!gti) {
		input_sync(dev);
		return;
	}
}
EXPORT_SYMBOL_GPL(goog_input_sync);

static void goog_input_release_all_fingers(struct goog_touch_interface *gti)
{
	int i;

	if (!gti || !gti->vendor_input_dev)
		return;

	goog_input_lock(gti);

	goog_input_set_timestamp(gti, gti->vendor_input_dev, ktime_get());
	for (i = 0; i < MAX_SLOTS; i++) {
		goog_input_mt_slot(gti, gti->vendor_input_dev, i);
		goog_input_mt_report_slot_state(
			gti, gti->vendor_input_dev, MT_TOOL_FINGER, false);
	}
	goog_input_report_key(gti, gti->vendor_input_dev, BTN_TOUCH, 0);
	goog_input_sync(gti, gti->vendor_input_dev);

	goog_input_unlock(gti);

	goog_input_process(gti, true);
}

void goog_input_unregister_device(struct device *vendor_dev, struct input_dev *vendor_input_dev)
{
	struct goog_touch_interface *gti = gim_vendor_get_interface_context(vendor_dev);

	if (vendor_dev)
		GOOG_LOGI(gti, "%s", gim_of_node_full_name(vendor_dev->of_node));
	if (gti && gti->dev && gti->vendor_input_dev == vendor_input_dev) {
		sysfs_remove_link(&gti->dev->kobj, "vendor_input");
		goog_input_lock(gti);
		gti->vendor_input_dev = NULL;
		goog_input_unlock(gti);
		gti_offload_remove(gti);
	}

	input_unregister_device(vendor_input_dev);
}
EXPORT_SYMBOL_GPL(goog_input_unregister_device);

int goog_input_register_device(struct device *vendor_dev, struct input_dev *vendor_input_dev)
{
	int ret;
	struct goog_touch_interface *gti = gim_vendor_get_interface_context(vendor_dev);

	if (vendor_dev)
		GOOG_LOGI(gti, "%s", gim_of_node_full_name(vendor_dev->of_node));
	if (!gti) {
		GOOG_LOGE(gti, "invalid interface context\n");
	} else {
		if (gti->vendor_dev == NULL)
			gti->vendor_dev = vendor_dev;
		if (gti->vendor_dev != vendor_dev)
			GOOG_LOGE(gti, "mismatched vendor_dev!");
		if (gti->vendor_input_dev == NULL) {
			goog_input_lock(gti);
			gti->vendor_input_dev = vendor_input_dev;
			goog_input_unlock(gti);
			gti_offload_probe(gti, vendor_dev->of_node);
			/*
			 * goog_init_input() needs the offload.cap initialization
			 * by goog_offload_probe().
			 */
			gti_init_input(gti, vendor_dev->of_node);
		} else {
			GOOG_LOGE(gti, "already registered for interface!\n");
		}
	}

	ret = input_register_device(vendor_input_dev);
	if (ret == 0)
		gti_sysfs_create_vendor_input_link(gti);

	return ret;
}
EXPORT_SYMBOL_GPL(goog_input_register_device);

static int goog_tbn_event_handler(struct notifier_block *nb, unsigned long action, void *data)
{
	struct goog_touch_interface *gti = container_of(nb, struct goog_touch_interface,
							tbn_event_notifier);
	u32 dev_id;

	if (gti == NULL)
		return NOTIFY_DONE;

	dev_id = gim_vendor_get_interface_dev_id(gti->vendor_dev);

	if (action == TBN_OPERATION_INVALID_GESTURE_EVENT) {
		struct invalid_gesture_event *gesture = (struct invalid_gesture_event *)data;
		struct gti_status_invalid_gesture_event status_event;

		if (gesture == NULL || dev_id != gesture->device_id)
			return NOTIFY_DONE;

		GOOG_DBG(gti, "dev_id: %d, Received Invalid Gesture: type=%d\n",
			dev_id, gesture->type);

		memset(&status_event, 0, sizeof(status_event));

		status_event.gesture_type = (__u8)gesture->type;
		status_event.timestamp = gesture->timestamp;
		status_event.x_down = gesture->x_down;
		status_event.y_down = gesture->y_down;
		status_event.x_valid = gesture->x_valid;
		status_event.y_valid  = gesture->y_valid;
		status_event.x_final = gesture->x_final;
		status_event.y_final = gesture->y_final;
		status_event.distance = gesture->distance;
		status_event.node_count = gesture->node_count;
		status_event.finger_count = gesture->finger_count;
		status_event.frame_count = gesture->frame_count;

		gti_status_send_invalid_gesture_event(&gti->status_event_dev, &status_event);
	} else if (action == TBN_OPERATION_INVALID_GESTURE_COUNTS) {
		struct invalid_gesture_count *count = (struct invalid_gesture_count *)data;

		if (count == NULL || dev_id != count->device_id)
			return NOTIFY_DONE;

		GOOG_DBG(gti, "dev_id: %d, Received Invalid Gesture Count\n", dev_id);
		// TODO Handle the statistics.
	} else if (action == TBN_OPERATION_DEVICE_SWITCH_FAILURE) {
		struct TbnDeviceSwitchFailureEvent *fail =
			(struct TbnDeviceSwitchFailureEvent *)data;

		if (fail == NULL || dev_id != fail->device_id)
			return NOTIFY_DONE;

		if (fail->enable_device_failure)
			GOOG_ERR(gti, "TBN reported enable device failure!\n");

		if (fail->disable_device_failure)
			GOOG_ERR(gti, "TBN reported disable device failure!\n");
	} else if (action == TBN_OPERATION_AOC_RESET) {
		gti_pm_state_update(&gti->pm);
	}

	return NOTIFY_OK;
}

static void goog_register_tbn(struct goog_touch_interface *gti, struct device_node *dn)
{
	if (!gti || !dn)
		return;

	gti->tbn_protection_enabled = of_property_read_bool(dn, "goog,tbn-protection-enabled");
	gti->tbn_enabled = of_property_read_bool(dn, "goog,tbn-enabled");
	if (gti->tbn_enabled) {
		if (register_tbn(&gti->tbn_register_mask)) {
			GOOG_ERR(gti, "Fail to register tbn context!\n");
			gti->tbn_enabled = false;
		} else {
			GOOG_INFO(gti, "tbn_register_mask = %#x.\n", gti->tbn_register_mask);
			register_tbn_lptw_callback(goog_notify_lptw_triggered, gti);

			gti->tbn_event_notifier.notifier_call = goog_tbn_event_handler;
			tbn_event_notifier_register(&gti->tbn_event_notifier, true);
		}
	}
}

static void gti_init_input(struct goog_touch_interface *gti, struct device_node *dn)
{
	int i;
	u16 display_resolution[2];
	u32 dev_id;

	if (!gti || !gti->vendor_input_dev || !dn) {
		GOOG_LOGE(gti, "invalid interface context!");
		return;
	}

	dev_id = gim_vendor_get_interface_dev_id(gti->vendor_dev);

	if (dev_id < 0) {
		GOOG_LOGE(gti, "failed to get dev_id!\n");
		return;
	}

	INIT_KFIFO(gti->debug_fifo_healthcheck);
	INIT_KFIFO(gti->debug_fifo_input);
	INIT_KFIFO(gti->debug_fifo_offload_toggle);
	for (i = 0 ; i < MAX_SLOTS ; i++)
		gti->debug_input[i].slot = i;
	gti->debug_warning_limit = TOUCH_OFFLOAD_BUFFER_NUM;
	gti->resample_latency = ns_to_ktime(RESAMPLE_LATENCY_DEFAULT);
	gti->timestamp_correction_enabled =
		of_property_read_bool(dn, "goog,timestamp-correction-enabled");
	if (gti->timestamp_correction_enabled) {
		pid_controller_init(&gti->pid, 1, 20, 1, 200);

		if (of_property_read_u32(dn, "goog,default-report-rate", &gti->default_report_rate))
			gti->default_report_rate = 240;
	}

	gti->abs_x_max = input_abs_get_max(gti->vendor_input_dev, ABS_MT_POSITION_X);
	gti->abs_x_min = input_abs_get_min(gti->vendor_input_dev, ABS_MT_POSITION_X);
	gti->abs_y_max = input_abs_get_max(gti->vendor_input_dev, ABS_MT_POSITION_Y);
	gti->abs_y_min = input_abs_get_min(gti->vendor_input_dev, ABS_MT_POSITION_Y);

	/*
	 * Initialize the ABS_MT_ORIENTATION to support orientation reporting.
	 * Initialize the ABS_MT_TOUCH_MAJOR and ABS_MT_TOUCH_MINOR depending on
	 * the larger values of ABS_MT_POSITION_X and ABS_MT_POSITION_Y to support
	 * shape algo reporting.
	 */
	if (gti->offload.caps.rotation_reporting) {
		int abs_x_max = gti->abs_x_max;
		int abs_x_min = gti->abs_x_min;
		int abs_x_res = input_abs_get_res(gti->vendor_input_dev, ABS_MT_POSITION_X);
		int abs_y_max = gti->abs_y_max;
		int abs_y_min = gti->abs_y_min;
		int abs_y_res = input_abs_get_res(gti->vendor_input_dev, ABS_MT_POSITION_Y);
		int abs_major_max = abs_x_max;
		int abs_major_min = abs_x_min;
		int abs_major_res = abs_x_res;
		int abs_minor_max = abs_y_max;
		int abs_minor_min = abs_y_min;
		int abs_minor_res = abs_y_res;

		if (abs_x_max < abs_y_max) {
			swap(abs_major_max, abs_minor_max);
			swap(abs_major_min, abs_minor_min);
			swap(abs_major_res, abs_minor_res);
		}
		input_set_abs_params(gti->vendor_input_dev, ABS_MT_ORIENTATION, -4096, 4096, 0, 0);
		input_set_abs_params(gti->vendor_input_dev, ABS_MT_TOUCH_MAJOR, abs_major_min,
				     abs_major_max, 0, 0);
		input_set_abs_params(gti->vendor_input_dev, ABS_MT_TOUCH_MINOR, abs_minor_min,
				     abs_minor_max, 0, 0);
		input_abs_set_res(gti->vendor_input_dev, ABS_MT_TOUCH_MAJOR, abs_major_res);
		input_abs_set_res(gti->vendor_input_dev, ABS_MT_TOUCH_MINOR, abs_minor_res);
	}

	/*
	 * Initialize the ABS_MT_TOOL_TYPE to support touch cancel.
	 */
	input_set_abs_params(gti->vendor_input_dev, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER, MT_TOOL_PALM,
			     0, 0);

	/*
	 * Initialize the resolution_scale_factor and coords for LPTW
	 */
	if (of_property_read_u16_array(dn, "goog,display-resolution", display_resolution, 2) == 0) {
		/* Scale the tracking area to touch resolution. */
		int touch_max_x = input_abs_get_max(gti->vendor_input_dev, ABS_MT_POSITION_X) + 1;
		int display_max_x = display_resolution[0];

		if (touch_max_x != 0 && display_max_x != 0)
			gti->resolution_scale_factor = touch_max_x / display_max_x;
	}
	tbn_update_super_resolution_scale(gti->resolution_scale_factor, dev_id);
	GOOG_LOGI(gti, "resolution_scale_factor %d", gti->resolution_scale_factor);

	gti->lptw_suppress_coords_enabled =
		of_property_read_bool(dn, "goog,lptw-suppress-coords-enabled");
	if (gti->lptw_suppress_coords_enabled) {
		u32 coords[4];

		if (of_property_read_u32_array(dn, "goog,lptw-tracking-area", coords, 4)) {
			GOOG_LOGE(gti, "goog,lptw-tracking-area not found\n");
			coords[0] = 200;
			coords[1] = 200;
			coords[2] = 200;
			coords[3] = 200;
		}
		gti->lptw_track_min_x = coords[0] * gti->resolution_scale_factor;
		gti->lptw_track_max_x = coords[1] * gti->resolution_scale_factor;
		gti->lptw_track_min_y = coords[2] * gti->resolution_scale_factor;
		gti->lptw_track_max_y = coords[3] * gti->resolution_scale_factor;
		GOOG_LOGI(gti, "goog,lptw-tracking-area %d, %d, %d, %d\n", gti->lptw_track_min_x,
			  gti->lptw_track_max_x, gti->lptw_track_min_y, gti->lptw_track_max_y);
	}
}

static void gti_init_vendor_options(struct goog_touch_interface *gti,
				    struct gti_optional_configuration *options,
				    struct device_node *dn)
{
	if (!gti || !dn) {
		GOOG_LOGE(gti, "invalid interface context!");
		return;
	}

	/* Conditional set vendor optional configuration with the default NOP handler. */
	GTI_OPT_FUNC_WITH_TYPE(GTI_COND_SET_WITH_DEFAULT);

	/* Specific handle for post_irq_thread_fn. */
	gti->options.post_irq_thread_fn = (options && options->post_irq_thread_fn) ?
		options->post_irq_thread_fn : NULL;

	/* Initialize the vendor common features. */
	gti->ignore_force_active = of_property_read_bool(dn, "goog,ignore-force-active");
	gti->manual_heatmap_from_irq = of_property_read_bool(dn, "goog,manual-heatmap-from-irq");
	gti->late_sense_on_enabled = of_property_read_bool(dn, "goog,late-sense-on-enabled");
	gti->reset_after_selftest = of_property_read_bool(dn, "goog,reset-after-selftest");

	gti->panel_map_from_tic = of_property_read_bool(dn, "goog,panel-map-from-tic");
	if (gti->panel_map_from_tic)
		gti->panel_id = goog_get_panel_id_from_tic(gti);
	else
		gti->panel_id = goog_get_panel_id(dn);

	if (gti->panel_id >= 0) {
		goog_get_firmware_name(dn, gti->panel_id, gti->fw_name, sizeof(gti->fw_name));
		goog_get_config_name(dn, gti->panel_id, gti->config_name, sizeof(gti->config_name));
		goog_get_test_limits_name(dn, gti->panel_id, gti->test_limits_name,
					  sizeof(gti->test_limits_name));
	}
}

void goog_reset_fw_status(struct goog_touch_interface *gti)
{
	if (gti->fw_status.water_mode != 0) {
		GOOG_INFO(gti, "Exit water mode\n");
		gti->fw_status.water_mode = 0;
		gti->context_changed.water_mode = 1;
	}

	if (gti->fw_status.noise_level != 0) {
		GOOG_INFO(gti, "Exit noise mode\n");
		gti->fw_status.noise_level = 0;
		gti->context_changed.noise_state = 1;
	}
}

void goog_notify_fw_status_changed(struct goog_touch_interface *gti,
		enum gti_fw_status status, struct gti_fw_status_data* data)
{
	const char *gesture_type = "N/A";

	if (!gti)
		return;

	switch (status) {
	case GTI_FW_STATUS_RESET:
		GOOG_INFO(gti, "Firmware has been reset or needs to restore settings\n");
		goog_reset_fw_status(gti);
		goog_input_release_all_fingers(gti);
		gti_update_fw_settings(gti, true);

		if (gti->timestamp_correction_enabled) {
			gti->report_rate = gti->default_report_rate;
			gti->frame_time = ktime_set(0, NSEC_PER_SEC / gti->report_rate);
		}

		break;
	case GTI_FW_STATUS_PALM_ENTER:
		GOOG_INFO(gti, "Enter palm mode\n");
		break;
	case GTI_FW_STATUS_PALM_EXIT:
		GOOG_INFO(gti, "Exit palm mode\n");
		break;
	case GTI_FW_STATUS_GRIP_ENTER:
		GOOG_INFO(gti, "Enter grip mode\n");
		break;
	case GTI_FW_STATUS_GRIP_EXIT:
		GOOG_INFO(gti, "Exit grip mode\n");
		break;
	case GTI_FW_STATUS_WATER_ENTER:
		GOOG_INFO(gti, "Enter water mode\n");
		gti->fw_status.water_mode = 1;
		gti->context_changed.water_mode = 1;
		break;
	case GTI_FW_STATUS_WATER_EXIT:
		GOOG_INFO(gti, "Exit water mode\n");
		gti->fw_status.water_mode = 0;
		gti->context_changed.water_mode = 1;
		break;
	case GTI_FW_STATUS_NOISE_MODE:
		if (data == NULL) {
			GOOG_INFO(gti, "Noise level is changed, level: unknown\n");
		} else {
			if (data->noise_level == GTI_NOISE_MODE_EXIT) {
				GOOG_INFO(gti, "Exit noise mode\n");
				gti->fw_status.noise_level= 0;
			} else {
				GOOG_INFO(gti, "Enter noise mode, level: %d\n", data->noise_level);
				gti->fw_status.noise_level = data->noise_level;
			}
			gti->context_changed.noise_state = 1;
		}
		break;
	case GTI_FW_STATUS_GESTURE_EVENT:
		if (data->gesture_event.type == GTI_GESTURE_STTW)
			gesture_type = "STTW";
		else if (data->gesture_event.type == GTI_GESTURE_LPTW)
			gesture_type = "LPTW";
		GOOG_INFO(gti, "Gesture %s detected, x:%u y:%u major:%u minor:%u angle:%d.\n",
				gesture_type,
				data->gesture_event.x,
				data->gesture_event.y,
				data->gesture_event.major,
				data->gesture_event.minor,
				data->gesture_event.angle);
		break;
	case GTI_FW_STATUS_INVALID_GESTURE_EVENT:
		switch (data->invalid_gesture_event.type) {
		case GTI_STTW_INVALID_GESTURE_OUT_OF_RANGE: {
			const struct sttw_out_of_range_payload *payload =
				&data->invalid_gesture_event.payload.sttw_out_of_range;
			GOOG_INFO(gti, "Invalid STTW detected, reason: out of range, (%u, %u)\n",
				  payload->x_down, payload->y_down);
			break;
		}
		case GTI_STTW_INVALID_GESTURE_SHIFT: {
			const struct sttw_shift_payload *payload =
				&data->invalid_gesture_event.payload.sttw_shift;
			GOOG_INFO(
				gti,
				"Invalid STTW detected, reason: shift, (%u, %u) -> (%u, %u), distance: %u\n",
				payload->x_down, payload->y_down, payload->x_final,
				payload->y_final, payload->distance);
			break;
		}
		case GTI_STTW_INVALID_GESTURE_PALM: {
			const struct sttw_palm_payload *payload =
				&data->invalid_gesture_event.payload.sttw_palm;
			GOOG_INFO(gti,
				  "Invalid STTW detected, reason: palm, (%u, %u), node_count: %u\n",
				  payload->x_down, payload->y_down, payload->node_count);
			break;
		}
		case GTI_STTW_INVALID_GESTURE_MULTI_TOUCH: {
			const struct sttw_multi_touch_payload *payload =
				&data->invalid_gesture_event.payload.sttw_multi_touch;
			GOOG_INFO(
				gti,
				"Invalid STTW detected, reason: multi touch, (%u, %u), finger_count: %u\n",
				payload->x_down, payload->y_down, payload->finger_count);
			break;
		}
		case GTI_STTW_INVALID_GESTURE_LONG_PRESS: {
			const struct sttw_long_press_payload *payload =
				&data->invalid_gesture_event.payload.sttw_long_press;
			GOOG_INFO(
				gti,
				"Invalid STTW detected, reason: long press, (%u, %u), frame_count: %u\n",
				payload->x_down, payload->y_down, payload->frame_count);
			break;
		}
		case GTI_STTW_INVALID_GESTURE_RAPID_TOUCH: {
			const struct sttw_rapid_touch_payload *payload =
				&data->invalid_gesture_event.payload.sttw_rapid_touch;
			GOOG_INFO(
				gti,
				"Invalid STTW detected, reason: rapid touch, (%u, %u), frame_count: %u\n",
				payload->x_down, payload->y_down, payload->frame_count);
			break;
		}
		case GTI_LPTW_INVALID_GESTURE_OUT_OF_RANGE: {
			const struct lptw_out_of_range_payload *payload =
				&data->invalid_gesture_event.payload.lptw_out_of_range;
			GOOG_INFO(
				gti,
				"Invalid LPTW detected, reason: out of range, (%u, %u) -> (%u, %u)\n",
				payload->x_down, payload->y_down, payload->x_final,
				payload->y_final);
			break;
		}
		case GTI_LPTW_INVALID_GESTURE_SHIFT: {
			const struct lptw_shift_payload *payload =
				&data->invalid_gesture_event.payload.lptw_shift;
			GOOG_INFO(
				gti,
				"Invalid LPTW detected, reason: shift, (%u, %u)/(%u, %u) -> (%u, %u), distance: %u\n",
				payload->x_down, payload->y_down, payload->x_valid,
				payload->y_valid, payload->x_final, payload->y_final,
				payload->distance);
			break;
		}
		case GTI_LPTW_INVALID_GESTURE_TOO_SMALL: {
			const struct lptw_too_small_payload *payload =
				&data->invalid_gesture_event.payload.lptw_too_small;
			GOOG_INFO(
				gti,
				"Invalid LPTW detected, reason: too small, (%u, %u), node_count: %u\n",
				payload->x_down, payload->y_down, payload->node_count);
			break;
		}
		case GTI_LPTW_INVALID_GESTURE_PALM: {
			const struct lptw_palm_payload *payload =
				&data->invalid_gesture_event.payload.lptw_palm;
			GOOG_INFO(gti,
				  "Invalid LPTW detected, reason: palm, (%u, %u), node_count: %u\n",
				  payload->x_down, payload->y_down, payload->node_count);
			break;
		}
		case GTI_LPTW_INVALID_GESTURE_RAPID_TOUCH: {
			const struct lptw_rapid_touch_payload *payload =
				&data->invalid_gesture_event.payload.lptw_rapid_touch;
			GOOG_INFO(
				gti,
				"Invalid LPTW detected, reason: rapid touch, (%u, %u), frame_count: %u\n",
				payload->x_down, payload->y_down, payload->frame_count);
			break;
		}
		default:
			GOOG_ERR(gti, "Unknown invalid gesture type %u",
				 data->invalid_gesture_event.type);
			break;
		}
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(goog_notify_fw_status_changed);

int goog_get_lptw_triggered(struct goog_touch_interface *gti)
{
	if (gti == NULL)
		return -ENODEV;

	return gti->lptw_triggered;
}
EXPORT_SYMBOL_GPL(goog_get_lptw_triggered);

static void goog_notify_lptw_triggered(struct TbnGestureEvent *lptw, void *data)
{
	struct goog_touch_interface *gti = (struct goog_touch_interface *)data;

	GOOG_INFO(gti, "Notify lptw event down");

	gti->lptw_x = lptw->x;
	gti->lptw_y = lptw->y;
	gti->lptw_major = lptw->major;
	gti->lptw_minor = lptw->minor;
	gti->lptw_angle = lptw->angle;
	gti->lptw_finger_count = lptw->lptw_finger_count;
	raw_notifier_call_chain(&lptw_notifier, 1, (void *) &gti->lptw_data[0]);
	gti->lptw_down = true;
}

void goog_lptw_notifier_register(struct notifier_block *nb, bool reg)
{
	if (reg)
		raw_notifier_chain_register(&lptw_notifier, nb);
	else
		raw_notifier_chain_unregister(&lptw_notifier, nb);
}
EXPORT_SYMBOL_GPL(goog_lptw_notifier_register);

static int goog_notify_lptw_left(struct goog_touch_interface *gti)
{
	int ret = 0;

	if (gti->lptw_down) {
		GOOG_INFO(gti, "Notify lptw event up");
		gti->lptw_down = false;
		ret = raw_notifier_call_chain(&lptw_notifier, 0, (void *) &gti->lptw_data[0]);
		if (ret != NOTIFY_OK)
			GOOG_INFO(gti, "Notify lptw event up failed, ret=%d", ret);
	} else {
		GOOG_INFO(gti, "Lptw event already up");
		ret = NOTIFY_OK;
	}

	return ret;
}

static void goog_track_lptw_slot(struct goog_touch_interface *gti, u16 x, u16 y, int slot_bit)
{
	gti->lptw_x = x;
	gti->lptw_y = y;

	if (!gti->lptw_down)
		return;

	if ((x < gti->lptw_track_min_x) || (x > gti->lptw_track_max_x) ||
		(y < gti->lptw_track_min_y) || (y > gti->lptw_track_max_y)) {
		GOOG_INFO(gti, "The tracking slot %#x moves out from the tracking area",
				slot_bit);
		goog_notify_lptw_left(gti);
	}
}

static void gti_input_set_timestamp(struct goog_touch_interface *gti, ktime_t timestamp)
{
	if (gti) {
		/*
		 * In android framework, the default value of resample latency is 5 milliseconds.
		 * For this solution, we need to add the compensation of resample latency to event
		 * time. So the result is equal to adjusting the resample latency to the new value.
		 */
		ktime_t latency_comp = ktime_sub(gti->resample_latency, RESAMPLE_LATENCY_DEFAULT);
		ATRACE_INT("Resample latency offset", latency_comp);

		input_set_timestamp(gti->vendor_input_dev, ktime_add(timestamp, latency_comp));
		gti->input_dev_mono_ktime = timestamp;
	}
}

static irqreturn_t gti_irq_handler(int irq, void *data)
{
	irqreturn_t ret;
	struct goog_touch_interface *gti = (struct goog_touch_interface *)data;

	gti->irq_index++;
	__ATRACE_INT_PID(0, "gti_th_irq_index", gti->irq_index);

	if (gti->vendor_irq_handler != NULL && gti->vendor_irq_cookie != NULL)
		ret = gti->vendor_irq_handler(irq, gti->vendor_irq_cookie);
	else
		ret = IRQ_WAKE_THREAD;
	gti_debug_healthcheck_update(gti, true);
	return ret;
}

static irqreturn_t gti_irq_thread_fn(int irq, void *data)
{
	char trace_tag[64];
	int pm_ret;
	irqreturn_t ret = IRQ_NONE;
	struct goog_touch_interface *gti = (struct goog_touch_interface *)data;

	scnprintf(trace_tag, sizeof(trace_tag), "%s: IRQ_IDX=%lld.", __func__, gti->irq_index);
	ATRACE_BEGIN(trace_tag);
	/*
	 * Allow vendor driver to handle wake-up gesture events by irq_thread_fn()
	 * after pm_suspend() complete without requiring a prior request for an IRQ
	 * wakelock. This is only for the tbn_enabled disabled case.
	 */
	pm_ret = goog_pm_wake_lock(gti, GTI_PM_WAKELOCK_TYPE_IRQ, true);
	if (pm_ret < 0 && gti->tbn_enabled) {
		GOOG_WARN(gti, "Skipping stray interrupt, pm state: (%d, %d)\n",
				gti->pm.state, gti->pm.new_state);
		/* sleep 10ms to let suspend process disable IRQ. */
		usleep_range(10 * USEC_PER_MSEC, 10 * USEC_PER_MSEC);
		ATRACE_END();
		return IRQ_HANDLED;
	}

	cpu_latency_qos_update_request(&gti->pm_qos_req, 100 /* usec */);

	/*
	 * Some vendor drivers read sensor data inside vendor_irq_thread_fn and
	 * some inside goog_input_process. Use input_heatmap_lock to avoid race that
	 * heatmap reading between sysfs/procfs and drivers concurrently.
	 */
	mutex_lock(&gti->input_heatmap_lock);

	if (gti->vendor_irq_thread_fn != NULL && gti->vendor_irq_cookie != NULL)
		ret = gti->vendor_irq_thread_fn(irq, gti->vendor_irq_cookie);
	else
		ret = IRQ_HANDLED;

	goog_input_process(gti, false);

	mutex_unlock(&gti->input_heatmap_lock);

	if (ret == IRQ_HANDLED && gti->vendor_irq_thread_fn != NULL &&
			gti->options.post_irq_thread_fn != NULL &&
			gti->vendor_irq_cookie != NULL) {
		ret = gti->options.post_irq_thread_fn(irq, gti->vendor_irq_cookie);
	}

	gti_debug_healthcheck_update(gti, false);
	cpu_latency_qos_update_request(&gti->pm_qos_req, PM_QOS_DEFAULT_VALUE);
	if (pm_ret == 0)
		goog_pm_wake_unlock_nosync(gti, GTI_PM_WAKELOCK_TYPE_IRQ);
	ATRACE_END();

	return ret;
}

int goog_devm_request_threaded_irq(struct goog_touch_interface *gti, struct device *dev,
				   unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
				   unsigned long irqflags, const char *devname, void *cookie)
{
	int ret;

	if (gti) {
		gti->vendor_irq_cookie = cookie;
		gti->vendor_irq_handler = handler;
		gti->vendor_irq_thread_fn = thread_fn;
		ret = devm_request_threaded_irq(dev, irq, gti_irq_handler, gti_irq_thread_fn,
				irqflags, devname, gti);
	} else {
		ret = devm_request_threaded_irq(dev, irq, handler, thread_fn, irqflags, devname,
						cookie);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(goog_devm_request_threaded_irq);

void goog_devm_free_irq(struct goog_touch_interface *gti,
		struct device *dev, unsigned int irq)
{
	devm_free_irq(dev, irq, gti);
}
EXPORT_SYMBOL(goog_devm_free_irq);

int goog_request_threaded_irq(struct goog_touch_interface *gti, unsigned int irq,
			      irq_handler_t handler, irq_handler_t thread_fn,
			      unsigned long irqflags, const char *devname, void *cookie)
{
	int ret;

	if (gti) {
		gti->vendor_irq_cookie = cookie;
		gti->vendor_irq_handler = handler;
		gti->vendor_irq_thread_fn = thread_fn;
		ret = request_threaded_irq(irq, gti_irq_handler, gti_irq_thread_fn,
				irqflags, devname, gti);
	} else {
		ret = request_threaded_irq(irq, handler, thread_fn, irqflags, devname, cookie);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(goog_request_threaded_irq);

void goog_touch_interface_device_destroy(struct goog_touch_interface *gti)
{
	struct class *gti_class = gim_get_interface_class(GOOG_INTERFACE_TYPE_TOUCH);

	if (gti && gti->dev) {
		if (gti->vendor_dev)
			sysfs_remove_link(&gti->dev->kobj, "vendor");
		if (gti->vendor_input_dev)
			sysfs_remove_link(&gti->dev->kobj, "vendor_input");
		if (gti_class)
			device_destroy(gti_class, gti->dev_t);
		unregister_chrdev_region(gti->dev_t, 1);
		gti->dev = NULL;
	}
}

struct device *goog_touch_interface_device_create(char *name, struct goog_touch_interface *gti)
{
	int ret = 0;
	struct gti_optional_configuration *options = NULL;
	struct class *gti_class = gim_get_interface_class(GOOG_INTERFACE_TYPE_TOUCH);

	if (!name || !gti || !gti_class) {
		GOOG_LOGE(gti, "invalid interface context!");
		return ERR_PTR(-EINVAL);
	}

	/* Set vendor optional with the default NOP handler. */
	GTI_OPT_FUNC_WITH_TYPE(GTI_COND_SET_WITH_DEFAULT);

	/*
	 * Initialize the variables with default values.
	 */
	mutex_init(&gti->input_lock);
	mutex_init(&gti->input_process_lock);
	mutex_init(&gti->input_heatmap_lock);
	INIT_DELAYED_WORK(&gti->lptw_cancel_delayed_work, goog_lptw_cancel_delayed_work);
	gti->screen_protector_mode_setting = GTI_SCREEN_PROTECTOR_MODE_DISABLE;
	gti->display_state = GTI_DISPLAY_STATE_ON;
	gti->panel_id = -1;
	gti->resolution_scale_factor = 1;

	ret = alloc_chrdev_region(&gti->dev_t, 0, 1, name);
	if (ret) {
		GOOG_ERR(gti, "alloc_chrdev_region failed!\n");
		goto error_gti_dev_create;
	}

	gti->dev = device_create(gti_class, NULL, gti->dev_t, gti, name);
	if (gti->dev == NULL) {
		GOOG_ERR(gti, "device_create %s failed\n", name);
		ret = -ENODEV;
		goto error_gti_dev_create;
	}

	if (gti_sysfs_init(gti))
		GOOG_ERR(gti, "sysfs_init failed!\n");

	gti_procfs_init(gti);

	return gti->dev;

error_gti_dev_create:
	return ERR_PTR(ret);
}

int goog_pm_wake_lock_nosync(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type,
			     bool skip_pm_resume)
{
	if (gti == NULL)
		return -ENODEV;

	return gti_pm_wake_lock_nosync_internal(&gti->pm, type, skip_pm_resume);
}
EXPORT_SYMBOL_GPL(goog_pm_wake_lock_nosync);

int goog_pm_wake_lock(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type,
		      bool skip_pm_resume)
{
	if (gti == NULL)
		return -ENODEV;

	return gti_pm_wake_lock_internal(&gti->pm, type, skip_pm_resume);
}
EXPORT_SYMBOL_GPL(goog_pm_wake_lock);

int goog_pm_wake_unlock_nosync(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type)
{
	if (gti == NULL)
		return -ENODEV;

	return gti_pm_wake_unlock_nosync_internal(&gti->pm, type);
}
EXPORT_SYMBOL_GPL(goog_pm_wake_unlock_nosync);

int goog_pm_wake_unlock(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type)
{
	if (gti == NULL)
		return -ENODEV;

	return gti_pm_wake_unlock_internal(&gti->pm, type);
}
EXPORT_SYMBOL_GPL(goog_pm_wake_unlock);

bool goog_pm_wake_check_locked(struct goog_touch_interface *gti, enum gti_pm_wakelock_type type)
{
	if (gti == NULL)
		return false;

	return gti_pm_wake_check_locked_internal(&gti->pm, type);
}
EXPORT_SYMBOL_GPL(goog_pm_wake_check_locked);

u32 goog_pm_wake_get_locks(struct goog_touch_interface *gti)
{
	if (gti == NULL)
		return 0;

	return gti_pm_wake_get_locks_internal(&gti->pm);
}
EXPORT_SYMBOL_GPL(goog_pm_wake_get_locks);

int goog_pm_register_notification(struct goog_touch_interface *gti, const struct dev_pm_ops *ops)
{
	if ((gti == NULL) || !gti->pm.enabled)
		return -ENODEV;

	gti->vendor_resume = ops->resume;
	gti->vendor_suspend = ops->suspend;
	return 0;
}
EXPORT_SYMBOL_GPL(goog_pm_register_notification);

int goog_pm_unregister_notification(struct goog_touch_interface *gti)
{
	if ((gti == NULL) || !gti->pm.enabled)
		return -ENODEV;

	gti->vendor_resume = NULL;
	gti->vendor_suspend = NULL;
	return 0;
}
EXPORT_SYMBOL_GPL(goog_pm_unregister_notification);

static int gti_resume(void *private_data)
{
	struct goog_touch_interface *gti = (struct goog_touch_interface *)private_data;
	int ret = 0;
	int err = 0;

	GOOG_LOGI(gti, "offload.running: %d\n", gti->offload.offload_running);
	if (gti->tbn_register_mask) {
		gti->lptw_triggered = false;
		ret = tbn_request_bus_with_result(gti->tbn_register_mask, &gti->lptw_triggered);
		if (ret) {
			GOOG_ERR(gti, "tbn_request_bus failed, ret %d!\n", ret);
			if (gti->tbn_protection_enabled) {
				if (ret == -ETIMEDOUT) {
					GOOG_ERR(gti, "TBN timed out, reset AoSS to recover\n");
#if IS_ENABLED(CONFIG_AOC_DRIVER)
					aoc_trigger_watchdog("touch: TBN timed out");
#endif
				} else if (ret == -ENODEV) {
					GOOG_ERR(gti, "TBN device not found, undergoing SSR...\n");
				} else {
					ret = tbn_release_bus(gti->tbn_register_mask);
					if (ret)
						GOOG_ERR(gti, "tbn_release_bus failed, ret %d!\n", ret);
				}
				ret = -EIO;
				goto end;
			}
		}
	}

	if (gti->lptw_suppress_coords_enabled && gti->lptw_triggered)
		gti->lptw_cancel_time = ktime_get();

	if (gti->vendor_resume) {
		ret = gti->vendor_resume(gti->vendor_dev);
		if (ret != 0) {
			GOOG_ERR(gti, "Failed to resume FW, err %d!\n", ret);

			gti->cmd.reset_cmd.setting = GTI_RESET_MODE_AUTO;
			err = goog_process_vendor_cmd(gti, GTI_CMD_RESET);
			if (err) {
				GOOG_LOGE(gti, "Failed to reset FW, err: %d!\n", err);
				gti->cmd.reset_cmd.setting = GTI_RESET_MODE_NA;
			}

			err = tbn_release_bus(gti->tbn_register_mask);
			if (err)
				GOOG_ERR(gti, "tbn_release_bus failed, err: %d!\n", err);

			goto end;
		}
	}

	if (gti->lptw_suppress_coords_enabled && gti->lptw_triggered) {
		gti->lptw_track_finger = true;
		gti->slot_bit_lptw_track = 0;
		queue_delayed_work(gti->pm.event_wq, &gti->lptw_cancel_delayed_work,
				   msecs_to_jiffies(40));
	}

end:
	return ret;
}

static int gti_suspend(void *private_data)
{
	struct goog_touch_interface *gti = (struct goog_touch_interface *)private_data;
	int ret = 0;

	GOOG_LOGI(gti, "irq_index: %llu, input_index: %llu.\n", gti->irq_index, gti->input_index);

	if (gti->vendor_suspend)
		gti->vendor_suspend(gti->vendor_dev);

#if IS_ENABLED(CONFIG_SPI_DW_GOOGLE_QUIRKS)
	if (gti->vendor_spi_dev) {
		ret = pm_runtime_suspend(gti->vendor_spi_dev->controller->dev.parent);
		if (ret != 0) {
			GOOG_ERR(gti, "PM runtime suspend failed, ret %d!\n", ret);
		}
	}
#endif

	gti_debug_healthcheck_dump(gti);
	gti_debug_input_dump(gti);
	gti_debug_offload_toggle_dump(gti);

	goog_reset_fw_status(gti);
	goog_input_release_all_fingers(gti);

	if (gti->tbn_register_mask) {
		ret = tbn_release_bus(gti->tbn_register_mask);
		if (ret)
			GOOG_ERR(gti, "tbn_release_bus failed, ret %d!\n", ret);
	}

	return 0;
}

static struct gti_pm_ops gti_dev_pm_ops = {
	.resume = gti_resume,
	.suspend = gti_suspend,
};

// Reference: goog_offload_populate_frame
static void touch_sim_populate_frame(struct goog_touch_interface *gti,
				     struct touch_offload_frame *offload_frame, char *buf,
				     size_t count)
{
	size_t handle_count = 0;
	int i = 0;
	struct TouchOffloadChannelHeader *channel_header;

	offload_frame->header.index = gti->frame_index;
	offload_frame->header.timestamp = gti->input_timestamp;

	buf += sizeof(struct TouchOffloadFrameHeader);
	handle_count += sizeof(struct TouchOffloadFrameHeader);

	while (handle_count < count) {
		channel_header = (struct TouchOffloadChannelHeader *)buf;
		u32 channel_size = channel_header->channel_size;
		u32 channel_type = channel_header->channel_type;

		for (i = 0; i < offload_frame->num_channels; i++) {
			if (channel_type != (u32)offload_frame->channel_type[i])
				continue;

			if (channel_size != offload_frame->channel_data_size[i]) {
				GOOG_ERR(gti, "Channel size not match !! %d %d with type %d",
					 channel_size, offload_frame->channel_data_size[i],
					 channel_type);
				break;
			}

			memcpy(offload_frame->channel_data[i], buf, channel_size);

			if (channel_type == CONTEXT_CHANNEL_TYPE_DRIVER_STATUS) {
				struct TouchOffloadDriverStatus *ds =
					(struct TouchOffloadDriverStatus *)
						offload_frame->channel_data[i];
				ds->contents.offload_timestamp = ktime_get();
				ds->offload_timestamp = ktime_get();
			}
			break;
		}

		buf += channel_size;
		handle_count += channel_size;
	}
}

static void touch_sim_input_flush_offload_fingers(struct goog_touch_interface *gti, char *buf,
						  size_t count)
{
	size_t handle_count = 0;

	goog_input_lock(gti);

	struct TouchOffloadChannelHeader *channel_header;
	struct TouchOffloadDataCoord *dc;

	buf += sizeof(struct TouchOffloadFrameHeader);
	handle_count += sizeof(struct TouchOffloadFrameHeader);

	while (handle_count < count) {
		channel_header = (struct TouchOffloadChannelHeader *)buf;
		u32 channel_size = channel_header->channel_size;
		u32 channel_type = channel_header->channel_type;

		if (channel_type == TOUCH_DATA_TYPE_COORD) {
			dc = (struct TouchOffloadDataCoord *)buf;
			goog_input_coordinate_report(gti, dc->coords);
			break;
		}

		buf += channel_size;
		handle_count += channel_size;
	}
	goog_input_unlock(gti);
}

static int touch_sim_input_process(void *gti_self, char *buf, size_t count, ktime_t timestamp)
{
	if (gti_self == NULL || buf == NULL)
		return -EINVAL;

	struct goog_touch_interface *gti = gti_self;
	struct touch_offload_frame **frame = &gti->offload_frame;
	int ret = 0;

	goog_input_set_timestamp(gti, gti->vendor_input_dev, timestamp);
	mutex_lock(&gti->input_process_lock);
	gti->frame_index++;

	if (gti->offload_enabled) {
		ret = touch_offload_reserve_frame(&gti->offload, frame);
		if (ret != 0 || frame == NULL) {
			GOOG_WARN(gti,
				  "offload: No buffers available in touch_sim, ret=%d IDX=%llu!\n",
				  ret, gti->frame_index);
			ret = -EBUSY;
			goto exit;
		}

		touch_sim_populate_frame(gti, *frame, buf, count);
		ret = touch_offload_queue_frame(&gti->offload, *frame);
		if (ret)
			GOOG_WARN(gti, "Fail to queue frame, ret=%d IDX=%llu!\n", ret,
				  gti->frame_index);
		else
			gti->offload_frame = NULL;

	} else {
		touch_sim_input_flush_offload_fingers(gti, buf, count);
	}

exit:
	mutex_unlock(&gti->input_process_lock);
	return ret;
}

static struct touch_sim *touch_sim_probe(struct goog_touch_interface *gti)
{
	int ret = 0;
	struct touch_sim *sim;
	char *name;
	u32 dev_id;
	struct class *gti_class = gim_get_interface_class(GOOG_INTERFACE_TYPE_TOUCH);

	if (gti == NULL || gti_class == NULL)
		return NULL;

	if (gti->sim)
		return gti->sim;

	gti->touch_sim_enabled =
		of_property_read_bool(gti->vendor_dev->of_node, "goog,touch-sim-enabled");
	if (!gti->touch_sim_enabled)
		return NULL;

	sim = devm_kzalloc(gti->dev, sizeof(struct touch_sim), GFP_KERNEL);
	if (!sim) {
		GOOG_ERR(gti, "Failed to allocate memory for touch_sim\n");
		return NULL;
	}

	atomic_set(&sim->device_is_locked, 0);
	sim->pop_data_cb = touch_sim_input_process;
	sim->private_data = gti;
	init_waitqueue_head(&sim->event_wait_queue);

	dev_id = gim_vendor_get_interface_dev_id(gti->vendor_dev);
	name = kasprintf(GFP_KERNEL, "touch_sim.%d", dev_id);
	if (!name) {
		GOOG_ERR(gti, "Failed to kasprintf() for touch_sim!\n");
		goto err_touch_sim_probe;
	}

	ret = alloc_chrdev_region(&sim->devt, 0, 1, name);
	if (ret) {
		GOOG_ERR(gti, "Failed to alloc_chrdev_region() for %s!\n", name);
		goto err_touch_sim_probe;
	}

	sim->dev = device_create(gti_class, gti->dev, sim->devt, sim, name);
	if (IS_ERR_OR_NULL(sim->dev)) {
		GOOG_ERR(gti, "Failed to create %s device\n", name);
		goto err_touch_sim_probe;
	}

	cdev_init(&sim->cdev, &touch_sim_fops);
	sim->cdev.owner = THIS_MODULE;
	if (cdev_add(&sim->cdev, sim->dev->devt, 1)) {
		GOOG_ERR(gti, "Failed to add touch_sim cdev\n");
		device_destroy(gti_class, sim->dev->devt);
		goto err_touch_sim_probe;
	}

	gti->sim = sim;
	GOOG_LOGI(gti, "device create \"%s\".\n", name);
	kfree(name);
	return sim;

err_touch_sim_probe:
	devm_kfree(gti->dev, sim);
	kfree(name);
	return NULL;
}

static void touch_sim_remove(struct goog_touch_interface *gti)
{
	struct class *gti_class = gim_get_interface_class(GOOG_INTERFACE_TYPE_TOUCH);

	if (gti->sim) {
		touch_sim_stop(gti->sim);
		device_destroy(gti_class, gti->sim->dev->devt);
		cdev_del(&gti->sim->cdev);
		devm_kfree(gti->dev, gti->sim);
		gti->sim = NULL;
	}
}

struct goog_touch_interface *
goog_touch_interface_connect(struct device *vendor_dev,
			     int (*vendor_default_handler)(void *vendor_private_data, u32 cmd_type,
							   struct gti_union_cmd_data *cmd),
			     struct gti_optional_configuration *vendor_options)
{
	struct goog_touch_interface *gti = gim_vendor_get_interface_context(vendor_dev);

	if (!vendor_dev || !gti) {
		GOOG_LOGE(gti, "invalid interface context for %s!\n",
			  gim_of_node_full_name(vendor_dev->of_node));
		return NULL;
	}

	if (gti->vendor_dev == NULL)
		gti->vendor_dev = vendor_dev;
	if (gti->vendor_dev != vendor_dev)
		GOOG_LOGE(gti, "mismatched vendor_dev!");
	gti->vendor_private_data = dev_get_drvdata(vendor_dev);
	if (vendor_dev->bus != NULL && strncmp(vendor_dev->bus->name, "spi", 3) == 0) {
		gti->vendor_spi_dev = to_spi_device(vendor_dev);
	} else if ((vendor_dev->parent != NULL) && (vendor_dev->parent->bus != NULL) &&
		   (strncmp(vendor_dev->parent->bus->name, "spi", 3) == 0)) {
		gti->vendor_spi_dev = to_spi_device(vendor_dev->parent);
	} else {
		GOOG_WARN(gti, "Cannot find SPI device.\n");
	}

	gti_sysfs_create_vendor_link(gti);
	if (vendor_default_handler)
		gti->vendor_default_handler = vendor_default_handler;
	else
		gti->vendor_default_handler = gti_default_handler_nop;

	gti_init_vendor_options(gti, vendor_options, vendor_dev->of_node);
	goog_register_tbn(gti, vendor_dev->of_node);

	/* init pm_qos. */
	cpu_latency_qos_add_request(&gti->pm_qos_req, PM_QOS_DEFAULT_VALUE);

	gti_pm_probe(&gti->pm, gti->dev, &gti_dev_pm_ops, gti);

	touch_sim_probe(gti);

	gti_status_event_probe(gti);

	gti->display_state_notifier.notifier_call = gti_set_display_state;
	gim_register_display_state_notifier(gti->vendor_dev, &gti->display_state_notifier);

	return gti;
}
EXPORT_SYMBOL_GPL(goog_touch_interface_connect);

/*
 * TODO(b/430410955):
 *   This API will be obsolete soon, please use goog_touch_interface_connect() with
 *   goog_input_register_device() instead.
 */
struct goog_touch_interface *
goog_touch_interface_probe(void *vendor_private_data, struct device *vendor_dev,
			   struct input_dev *vendor_input_dev,
			   int (*vendor_default_handler)(void *vendor_private_data, u32 cmd_type,
							 struct gti_union_cmd_data *cmd),
			   struct gti_optional_configuration *vendor_options)
{
	struct goog_touch_interface *gti = NULL;

	if (vendor_private_data != dev_get_drvdata(vendor_dev)) {
		GOOG_LOGE(gti, "private_data can't match\n");
		return NULL;
	}

	gti = goog_touch_interface_connect(vendor_dev, vendor_default_handler, vendor_options);
	if (!gti)
		return NULL;

	if (vendor_input_dev) {
		gti->vendor_input_dev = vendor_input_dev;
		gti_sysfs_create_vendor_input_link(gti);
		gti_offload_probe(gti, vendor_dev->of_node);
		/*
		 * goog_init_input() needs the offload.cap initialization by goog_offload_probe().
		 */
		gti_init_input(gti, vendor_dev->of_node);
	}

	return gti;
}
EXPORT_SYMBOL_GPL(goog_touch_interface_probe);

int goog_touch_interface_disconnect(struct device *vendor_dev)
{
	struct goog_touch_interface *gti = gim_vendor_get_interface_context(vendor_dev);

	if (!gti)
		return -ENODEV;

	gti_status_event_remove(gti);

	touch_sim_remove(gti);

	gim_unregister_display_state_notifier(gti->vendor_dev, &gti->display_state_notifier);

	gti_pm_remove(&gti->pm);

	cpu_latency_qos_remove_request(&gti->pm_qos_req);

	if (gti->tbn_event_notifier.notifier_call)
		tbn_event_notifier_register(&gti->tbn_event_notifier, false);

	if (gti->tbn_enabled && gti->tbn_register_mask)
		unregister_tbn(&gti->tbn_register_mask);

	return 0;
}
EXPORT_SYMBOL_GPL(goog_touch_interface_disconnect);

/*
 * TODO(b/430410955):
 *   This API will be obsolete soon, please use goog_touch_interface_disconnect() instead.
 */
int goog_touch_interface_remove(struct goog_touch_interface *gti)
{
	int ret;

	if (!gti || !gti->dev)
		return -ENODEV;

	ret = goog_touch_interface_disconnect(gti->vendor_dev);
	if (gti->vendor_input_dev)
		sysfs_remove_link(&gti->dev->kobj, "vendor_input");
	gti_offload_remove(gti);

	return ret;
}
EXPORT_SYMBOL_GPL(goog_touch_interface_remove);

MODULE_DESCRIPTION("Google Touch Interface");
MODULE_AUTHOR("Super Liu<supercjliu@google.com>");
MODULE_LICENSE("GPL");
