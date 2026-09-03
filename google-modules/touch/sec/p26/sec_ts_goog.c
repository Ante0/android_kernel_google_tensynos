/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2025 Google LLC
 *    Author: Yen-Chao Chen <davidycchen@google.com>
 */

#include "sec_ts.h"
#include "sec_cmd.h"

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)

#include <goog_touch_interface.h>

irqreturn_t goog_sec_ts_isr(int irq, void *handle)
{
	struct sec_ts_data *ts = (struct sec_ts_data *)handle;

	ts->isr_timestamp = ktime_get();

	return IRQ_WAKE_THREAD;
}

irqreturn_t goog_sec_ts_irq_thread(int irq, void *ptr)
{
	struct sec_ts_data *ts = (struct sec_ts_data *)ptr;

	sec_ts_read_event(ts);

	return IRQ_HANDLED;
}

static int gti_default_handler(void *private_data, enum gti_cmd_type cmd_type,
		struct gti_union_cmd_data *cmd)
{
	struct sec_ts_data *ts = private_data;
	struct sec_cmd *sec_cmd_ptr = NULL;
	int ret = -EOPNOTSUPP;
	char cmd_str[SEC_CMD_STR_LEN];

	cmd_str[0] = '\0';

	if (atomic_cmpxchg(&ts->sec.cmd_is_running, 0, 1)) {
		LOGE("other cmd is running.\n");
		return -EBUSY;
	}

	memset(ts->sec.cmd, 0x00, SEC_CMD_STR_LEN);

	switch (cmd_type) {
	case GTI_CMD_NOTIFY_DISPLAY_STATE:
	case GTI_CMD_NOTIFY_DISPLAY_VREFRESH:
		ret = 0;
		break;
	case GTI_CMD_SET_GRIP_MODE:
		strscpy(cmd_str, "set_grip_detection_enable", sizeof(cmd_str));
		ts->sec.cmd_param[0] = cmd->grip_cmd.setting == GTI_GRIP_ENABLE;
		break;
	case GTI_CMD_SET_PALM_MODE:
		strscpy(cmd_str, "set_palm_detection_enable", sizeof(cmd_str));
		ts->sec.cmd_param[0] = cmd->palm_cmd.setting == GTI_PALM_ENABLE;
		break;
	case GTI_CMD_SET_HEATMAP_ENABLED:
		strscpy(cmd_str, "heatmap_enable", sizeof(cmd_str));
		ts->sec.cmd_param[0] = cmd->heatmap_cmd.setting == GTI_HEATMAP_ENABLE;
		break;
	case GTI_CMD_SET_CONTINUOUS_REPORT:
		strscpy(cmd_str, "set_continuous_report_enable", sizeof(cmd_str));
		ts->sec.cmd_param[0] = cmd->continuous_report_cmd.setting ==
				GTI_CONTINUOUS_REPORT_ENABLE;
		break;
	case GTI_CMD_PING:
		strscpy(cmd_str, "get_chip_id", sizeof(cmd_str));
		if (cmd->ping_cmd.setting != GTI_PING_ENABLE) {
			LOGE("Unknown ping command %d", cmd->scan_cmd.setting);
			goto exit;
		}
		break;
	case GTI_CMD_SET_SCAN_MODE:
		strscpy(cmd_str, "set_touch_mode", sizeof(cmd_str));

		if (cmd->scan_cmd.setting == GTI_SCAN_MODE_AUTO) {
			sec_ts_system_reset(ts, RESET_MODE_SW, false, false);
			goto exit;
		} else if (cmd->scan_cmd.setting == GTI_SCAN_MODE_NORMAL_ACTIVE) {
			ts->sec.cmd_param[0] = SET_TOUCH_MODE_NPA;
		} else if (cmd->scan_cmd.setting == GTI_SCAN_MODE_NORMAL_IDLE) {
			ts->sec.cmd_param[0] = SET_TOUCH_MODE_NPI;
		} else if (cmd->scan_cmd.setting == GTI_SCAN_MODE_LP_ACTIVE) {
			ts->sec.cmd_param[0] = SET_TOUCH_MODE_LPA;
		} else if (cmd->scan_cmd.setting == GTI_SCAN_MODE_LP_IDLE) {
			ts->sec.cmd_param[0] = SET_TOUCH_MODE_LPI;
		} else {
			LOGE("Unknown scan mode %d", cmd->scan_cmd.setting);
			goto exit;
		}
		break;
	case GTI_CMD_SET_SENSING_MODE:
		strscpy(cmd_str, "set_touch_mode", sizeof(cmd_str));

		if (cmd->sensing_cmd.setting == GTI_SENSING_MODE_DISABLE) {
			ts->sec.cmd_param[0] = SET_TOUCH_MODE_SENSE_OFF;
		} else if (cmd->sensing_cmd.setting == GTI_SENSING_MODE_ENABLE) {
			ts->sec.cmd_param[0] = SET_TOUCH_MODE_SENSE_ON;
		} else {
			LOGE("Unknown sensing mode %d", cmd->sensing_cmd.setting);
			goto exit;
		}
		break;
	case GTI_CMD_GET_FW_VERSION:
		strscpy(cmd_str, "get_fw_ver_ic", sizeof(cmd_str));
		break;
	default:
		break;
	}

	if (strlen(cmd_str)) {
		list_for_each_entry(sec_cmd_ptr, &ts->sec.cmd_list_head, list) {
			if (!strncmp(cmd_str, sec_cmd_ptr->cmd_name, sizeof(cmd_str))) {
				sec_cmd_ptr->cmd_func(&ts->sec);
				ret = ts->sec.cmd_state == SEC_CMD_STATUS_OK ? 0 : -1;
				break;
			}
		}
	}

	switch (cmd_type) {
	case GTI_CMD_GET_FW_VERSION:
		if (!ret) {
			strscpy(cmd->fw_version_cmd.buffer, ts->sec.cmd_result,
				min(sizeof(cmd->fw_version_cmd.buffer),
				sizeof(ts->sec.cmd_result)));
		}
		break;
	default:
		break;
	}

exit:
	atomic_set(&ts->sec.cmd_is_running, 0);

	return ret;
}

static int gti_set_coord_filter_enabled(void *private_data, struct gti_coord_filter_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 buffer = 0;
	int ret = 0;

	buffer = cmd->setting == GTI_COORD_FILTER_ENABLE ? 0x5F : 0;

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_COORD_FILTER, &buffer, 1);
	if (ret) {
		LOGE("Failed to set coord_filter mode, ret = %d", ret);
		return ret;
	}

	LOGI("coord_filter %s", cmd->setting ? "enabled" : "disabled");

	return 0;
}

static int gti_get_coord_filter_enabled(void *private_data, struct gti_coord_filter_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 buffer = 0;
	int ret = 0;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_COORD_FILTER, &buffer, 1);
	if (ret) {
		LOGE("Failed to get coord_filter mode, ret = %d", ret);
		return ret;
	}

	LOGI("coord_filter: %#x", buffer);

	cmd->setting = buffer ? GTI_COORD_FILTER_ENABLE : GTI_COORD_FILTER_DISABLE;

	return 0;
}

static int gti_set_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	ts->sec_irq_enable(ts, cmd->setting == GTI_IRQ_MODE_ENABLE);
	return 0;
}

static int gti_get_grip_mode(void *private_data, struct gti_grip_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 buffer = 0;
	int ret = 0;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_GRIP_DETECT, &buffer, sizeof(buffer));
	if (ret) {
		LOGE("Failed to read grip mode, ret = %d", ret);
		return ret;
	}

	LOGI("fw_grip: %#x", buffer);

	cmd->setting = buffer ? GTI_GRIP_ENABLE : GTI_GRIP_DISABLE;

	return 0;
}

static int gti_get_palm_mode(void *private_data, struct gti_palm_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 buffer = 0;
	int ret = 0;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_PALM_DETECT, &buffer, sizeof(buffer));
	if (ret) {
		LOGE("Failed to read palm mode, ret = %d", ret);
		return ret;
	}

	LOGI("fw_palm: %#x", buffer);

	cmd->setting = buffer ? GTI_PALM_ENABLE : GTI_PALM_DISABLE;

	return 0;
}

static int gti_get_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;

	if (atomic_read(&ts->irq_enabled) == 1)
		cmd->setting = GTI_IRQ_MODE_ENABLE;
	else
		cmd->setting = GTI_IRQ_MODE_DISABLE;

	return 0;
}

static int gti_get_scan_mode(void *private_data, struct gti_scan_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 buffer[2] = {0};
	int ret = 0;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, buffer, sizeof(buffer));
	if (ret) {
		LOGE("Failed to read scan mode, ret = %d", ret);
		return ret;
	}

	if (buffer[0] == TOUCH_SYSTEM_MODE_TOUCH && buffer[1] == TOUCH_MODE_STATE_TOUCH) {
		cmd->setting = GTI_SCAN_MODE_NORMAL_ACTIVE;
	} else if (buffer[0] == TOUCH_SYSTEM_MODE_TOUCH && buffer[1] == TOUCH_MODE_STATE_IDLE) {
		cmd->setting = GTI_SCAN_MODE_NORMAL_IDLE;
	} else if (buffer[0] == TOUCH_SYSTEM_MODE_LOWPOWER && buffer[1] == TOUCH_MODE_STATE_TOUCH) {
		cmd->setting = GTI_SCAN_MODE_LP_ACTIVE;
	} else if (buffer[0] == TOUCH_SYSTEM_MODE_LOWPOWER && buffer[1] == TOUCH_MODE_STATE_IDLE) {
		cmd->setting = GTI_SCAN_MODE_LP_IDLE;
	} else if (buffer[0] == TOUCH_SYSTEM_MODE_SLEEP) {
		cmd->setting = GTI_SCAN_MODE_AUTO;
		LOGW("Touch is in sleep mode");
	} else {
		cmd->setting = GTI_SCAN_MODE_NA;
		LOGE("Unknown scan mode system mode: %d, touch mode: %d", buffer[0], buffer[1]);
	}

	return 0;

}

static int gti_get_sensing_mode(void *private_data, struct gti_sensing_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 mode;
	int ret = 0;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_POWER_MODE, &mode, 1);
	if (ret) {
		LOGE("Failed to read sensing mode, ret = %d", ret);
		return ret;
	}

	if (mode == TO_TOUCH_MODE || mode == TO_LOWPOWER_MODE)
		cmd->setting = GTI_SENSING_MODE_ENABLE;
	else if (mode == TO_SLEEP_MODE || mode == TO_STOP_MODE)
		cmd->setting = GTI_SENSING_MODE_DISABLE;
	else
		cmd->setting = GTI_SENSING_MODE_NA;

	LOGI("power mode: %d", mode);

	return 0;
}

static int gti_get_mutual_or_self_sensor_data(void *private_data, struct gti_sensor_data_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 data_type;
	int ret, i;
	short *temp_frame;
	short min[REGION_TYPE_COUNT], max[REGION_TYPE_COUNT];

	if (cmd->type == GTI_SENSOR_DATA_TYPE_MS) {
		cmd->buffer = (u8*) ts->pFrameMS_irq;
		cmd->size = ts->tx_count * ts->rx_count * 2;
		return 0;
	} else if (cmd->type == GTI_SENSOR_DATA_TYPE_SS) {
		cmd->buffer = (u8*) ts->pFrameSS_irq;
		cmd->size = (ts->tx_count + ts->rx_count) * 2;
		return 0;
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_MS_DIFF:
	case GTI_SENSOR_DATA_TYPE_SS_DIFF:
		data_type = TYPE_SIGNAL_DATA;
		break;
	case GTI_SENSOR_DATA_TYPE_MS_RAW:
	case GTI_SENSOR_DATA_TYPE_SS_RAW:
		data_type = TYPE_DECODED_DATA;
		break;
	case GTI_SENSOR_DATA_TYPE_MS_BASELINE:
	case GTI_SENSOR_DATA_TYPE_SS_BASELINE:
		data_type = TYPE_AMBIENT_DATA;
		break;
	default:
		LOGE("Unsupported report type %u", cmd->type);
		return -EOPNOTSUPP;
	}

	/* Keep touch in the active mode to make sure touch can respond in time. */
	ret = sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
	if (ret < 0) {
		LOGE("Failed to fix touch mode: %d : state: %d\n",
				TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
		goto exit;
	}

	ret = sec_ts_read_frame(ts, data_type, min, max);
	if (ret < 0) {
		LOGI("Failed to read data_type %d : ## ret: %d\n", data_type, ret);
		goto exit;
	} else {
#ifdef USE_SPEC_CHECK
		LOGI("data_type %d : Max/Min %d,%d ##\n", data_type, max[0], min[0]);
#else
		LOGI("data_type %d ##\n", data_type);
#endif
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_MS_DIFF:
	case GTI_SENSOR_DATA_TYPE_MS_RAW:
	case GTI_SENSOR_DATA_TYPE_MS_BASELINE:
		cmd->size = ts->rx_count * ts->tx_count * 2;
		cmd->buffer = (u8 *) ts->pFrameMS;
		break;
	case GTI_SENSOR_DATA_TYPE_SS_DIFF:
	case GTI_SENSOR_DATA_TYPE_SS_RAW:
	case GTI_SENSOR_DATA_TYPE_SS_BASELINE:
		cmd->size = (ts->rx_count + ts->tx_count) * 2;
		/* Move tx data to the front of rx data. */
		temp_frame = kzalloc(cmd->size, GFP_KERNEL);
		memcpy(temp_frame, ts->pFrameSS, cmd->size);
		memset(ts->pFrameSS, 0x00, cmd->size);
		for (i = 0; i < ts->tx_count; i++)
			ts->pFrameSS[i] = temp_frame[ts->rx_count + i];
		for (i = 0; i < ts->rx_count; i++)
			ts->pFrameSS[ts->tx_count + i] = temp_frame[i];
		cmd->buffer = (u8 *) ts->pFrameSS;
		kfree(temp_frame);
		break;
	default:
		LOGE("Unsupported report type %u", cmd->type);
		return -EOPNOTSUPP;
	}

exit:
	sec_ts_release_tmode(ts);

	return ret;
}

static int gti_get_water_mode(void *private_data, struct gti_water_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 enabled = 0;
	int ret = 0;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_WET_MODE, &enabled, 1);
	if (ret) {
		LOGE("Failed to get fw water mode, ret = %d", ret);
		return ret;
	}

	LOGI("fw_water: %#x", enabled);

	cmd->setting = enabled ? GTI_WATER_ENABLE : GTI_WATER_DISABLE;

	return 0;
}

static int gti_set_water_mode(void *private_data, struct gti_water_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 enabled = 0;
	int ret = 0;

	enabled = (cmd->setting == GTI_WATER_ENABLE) ? 1 : 0;
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_WET_MODE, &enabled, 1);
	if (ret) {
		LOGE("Failed to set fw_water mode, ret = %d", ret);
		return ret;
	}

	LOGI("fw_water: %s", enabled ? "enabled" : "disabled");

	return 0;
}

static int gti_get_screen_protector_mode(void *private_data,
					 struct gti_screen_protector_mode_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 enabled = 0;
	int ret = 0;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_HIGH_SENSITIVITY, &enabled, 1);
	if (ret) {
		LOGE("Failed to get high sensitivity mode, ret = %d", ret);
		return ret;
	}

	LOGI("high sensitivity: %#x", enabled);

	cmd->setting = enabled ?
		       GTI_SCREEN_PROTECTOR_MODE_ENABLE : GTI_SCREEN_PROTECTOR_MODE_DISABLE;

	return 0;
}

static int gti_set_screen_protector_mode(void *private_data,
					 struct gti_screen_protector_mode_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	u8 enabled = 0;
	int ret = 0;

	enabled = (cmd->setting == GTI_SCREEN_PROTECTOR_MODE_ENABLE) ? 1 : 0;
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_HIGH_SENSITIVITY, &enabled, 1);
	if (ret) {
		LOGE("Failed to set high sensitivity mode, ret = %d", ret);
		return ret;
	}

	LOGI("high sensitivity mode: %s", enabled ? "enabled" : "disabled");

	return 0;
}

static int gti_get_vendor_register(void *private_data, struct gti_vendor_register_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;

	memcpy(cmd->data, ts->vendor_register_data, VENDOR_REGISTER_DATA_SIZE);
	cmd->size = VENDOR_REGISTER_DATA_SIZE;
	return 0;
}

static int read_line(char *data, char *line, int size, int *length)
{
	int i = 0;

	if (size < 1)
		return -EINVAL;

	while (data[i] != '\n' && i < size) {
		line[i] = data[i];
		i++;
	}
	*length = i + 1;
	line[i] = '\0';

	return 0;
}

static int goog_parse_limit(char *file, size_t file_size,
		char *label, int *data, int row, int column)
{
	bool label_found = false;
	char *token = NULL;
	int i = 0;
	int j = 0;
	int k = 0;
	int length = 0;
	int pointer = 0;
	char line[256];
	char *line2 = NULL;

	while (pointer < file_size) {
		if (read_line(&file[pointer], line, file_size - pointer, &length) < 0) {
			LOGE("Read error while searching for label %s", label);
			return -EIO;
		}
		pointer += length;
		if (line[0] != '*')
			continue;

		line2 = &line[1];
		token = strsep(&line2, ",");

		if (token && (strcmp(token, label) == 0)) {
			label_found = true;
			break;
		}
	}

	if (!label_found) {
		LOGE("Failed to find label %s", label);
		return -ENOENT;
	}

	for (i = 0; i < row; i++) {
		if (read_line(&file[pointer], line, file_size - pointer, &length) < 0) {
			LOGE("read_line failed for label %s", label);
			return -EIO;
		}
		pointer += length;
		line2 = line;
		token = strsep(&line2, ",");
		for (j = 0; (j < column) && (token != NULL); j++) {
			if (sscanf(token, "%d", (data + k)) == 1) {
				k++;
				token = strsep(&line2, ",");
			}
		}
	}

	if (k == row * column) {
		LOGI("Parsed %s array size %d", label, k);
		return 0;
	}

	LOGE("Failed to parse %s", label);
	return -ENOENT;
}

static int goog_parse_limit_file(struct sec_ts_data *ts)
{
	const struct firmware *limit_file = NULL;
	char* limit;
	int size = 0;
	int ret = 0;

	LOGI("Load selftest limit from %s", ts->plat_data->selftest_limit_name);
	ret = request_firmware(&limit_file, ts->plat_data->selftest_limit_name, &ts->client->dev);
	if (ret) {
		LOGE("Failed to load selftest limit, ret: %d", ret);
		return ret;
	}

	size = limit_file->size;
	limit = (char *)kmalloc(size * sizeof(char), GFP_KERNEL);
	if (!limit) {
		LOGE("Failed to allocate memory for limit");
		goto exit;
	}
	memcpy(limit, (char *)limit_file->data, size);

	ret = goog_parse_limit(limit, size, "CMR_P2P_H", &ts->cmr_p2p_h, 1, 1);
	if (ret < 0)
		goto exit;
	ret = goog_parse_limit(limit, size, "CMR_P2P_L", &ts->cmr_p2p_l, 1, 1);
	if (ret < 0)
		goto exit;
	ret = goog_parse_limit(limit, size, "CMR_P2P_H-L_MAX", &ts->cmr_p2p_h_l_max, 1, 1);
	if (ret < 0)
		goto exit;
	ret = goog_parse_limit(limit, size, "CMR_P2P_H-L_MIN", &ts->cmr_p2p_h_l_min, 1, 1);
	if (ret < 0)
		goto exit;

	LOGI("%d %d %d %d", ts->cmr_p2p_h, ts->cmr_p2p_l, ts->cmr_p2p_h_l_max, ts->cmr_p2p_h_l_min);

	if (!ts->cm1_max)
		ts->cm1_max = devm_kzalloc(&ts->client->dev, ts->tx_count * ts->rx_count * sizeof(int), GFP_KERNEL);
	if (!ts->cm1_min)
		ts->cm1_min = devm_kzalloc(&ts->client->dev, ts->tx_count * ts->rx_count * sizeof(int), GFP_KERNEL);
	if (!ts->cm1_gap)
		ts->cm1_gap = devm_kzalloc(&ts->client->dev, ts->tx_count * ts->rx_count * sizeof(int), GFP_KERNEL);

	ret = goog_parse_limit(limit, size, "CM1_MAX", ts->cm1_max, ts->rx_count, ts->tx_count);
	if (ret < 0)
		goto exit;
	ret = goog_parse_limit(limit, size, "CM1_MIN", ts->cm1_min, ts->rx_count, ts->tx_count);
	if (ret < 0)
		goto exit;
	ret = goog_parse_limit(limit, size, "CM1_GAP", ts->cm1_gap, ts->rx_count, ts->tx_count);
	if (ret < 0)
		goto exit;

exit:
	kfree(limit);
	release_firmware(limit_file);
	return ret;
}

static int goog_gap_spec_compare(struct sec_ts_data *ts, short *pFrameMS, int *gap_limit)
{
	int i = 0;
	int j = 0;
	int gapx, gapy, pos1, pos2;
	short dpos1, dpos2;
	int specover_count = 0;

	/* Get x-direction cm gap */
	for (i = 0; i < ts->rx_count; i++) {
		for (j = 0; j < ts->tx_count - 1; j++) {
			/* Exclude last line to get gap between two */
			pos1 = (i * ts->tx_count) + j;
			pos2 = (i * ts->tx_count) + (j + 1);

			dpos1 = pFrameMS[pos1];
			dpos2 = pFrameMS[pos2];

			if (dpos1 > dpos2)
				gapx = 100 - (dpos2 * 100 / dpos1);
			else
				gapx = 100 - (dpos1 * 100 / dpos2);

			if (gapx > gap_limit[pos1])
				specover_count++;
		}
	}

	/* get y-direction cm gap */
	for (i = 0; i < ts->rx_count - 1; i++) {
		for (j = 0; j < ts->tx_count; j++) {
			pos1 = (i * ts->tx_count) + j;
			pos2 = ((i + 1) * ts->tx_count) + j;

			dpos1 = pFrameMS[pos1];
			dpos2 = pFrameMS[pos2];

			if (dpos1 > dpos2)
				gapy = 100 - (dpos2 * 100 / dpos1);
			else
				gapy = 100 - (dpos1 * 100 / dpos2);

			if (gapy > gap_limit[pos1])
				specover_count++;
		}
	}

	return specover_count;
}

static int gti_reset(void *private_data, struct gti_reset_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;

	if (cmd->setting == GTI_RESET_MODE_HW) {
		sec_ts_system_reset(ts, RESET_MODE_HW, false, false);
	} else if (cmd->setting == GTI_RESET_MODE_SW || cmd->setting == GTI_RESET_MODE_AUTO) {
		sec_ts_system_reset(ts, RESET_MODE_SW, false, false);
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static int gti_selftest(void *private_data, struct gti_selftest_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	struct sec_ts_test_mode mode;
	int ret = 0;
	int i = 0;
	int j = 0;
	int index = 0;
	int specover_count = 0;
	short *temp_min = NULL;

	temp_min = kzalloc(ts->tx_count * ts->rx_count * 2, GFP_KERNEL);
	if (!temp_min) {
		LOGE("Failed to allocate temp_min");
		return -ENOMEM;
	}

	ret = goog_parse_limit_file(ts);
	if (ret < 0) {
		LOGE("Failed to parse limit file, ret: %d", ret);
		goto exit;
	}

	/* Short test */
	execute_selftest(ts, TEST_SHORT);
	if (ts->ito_test[0] != 0 || ts->ito_test[1] != 0 ||
			ts->ito_test[2] != 0 || ts->ito_test[3] != 0) {
		LOGE("short test failed, %#x, %#x, %#x, %#x", ts->ito_test[0], ts->ito_test[1],
				ts->ito_test[2], ts->ito_test[3]);
		specover_count++;
	}

	sec_ts_delay(20);

	/* CM1 */
	execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE);

	mode.type = TYPE_OFFSET_DATA_SDC;
	ret = sec_ts_read_frame(ts, mode.type, mode.min, mode.max);
	if (ret < 0) {
		LOGE("Failed to read frame TYPE_OFFSET_DATA_SDC, ret: %d", ret);
		goto exit;
	}

	for (i = 0; i < ts->rx_count; i++) {
		for (j = 0; j < ts->tx_count; j++) {
			index = i * ts->tx_count + j;
			if (ts->pFrame[index] > ts->cm1_max[index]) {
				LOGE("CM1 (%d,%d) = %d, max test limit %d", j, i,
						ts->pFrame[index], ts->cm1_max[index]);
				specover_count++;
			} else if (ts->pFrame[index] < ts->cm1_min[index]) {
				LOGE("CM1 (%d,%d) = %d, min test limit %d", j, i,
						ts->pFrame[index], ts->cm1_min[index]);
				specover_count++;
			}
		}
	}

	specover_count += goog_gap_spec_compare(ts, ts->pFrame, ts->cm1_gap);

	/* CMR P2P */
/*
	execute_p2ptest(ts);

	mode.type = TYPE_NOI_P2P_MIN;
	ret = sec_ts_read_frame(ts, mode.type, mode.min, mode.max);
	if (ret < 0) {
		LOGE("Failed to read frame TYPE_NOI_P2P_MIN, ret: %d", ret);
		goto exit;
	}

	memcpy(temp_min, ts->pFrame, ts->tx_count * ts->rx_count * 2);

	mode.type = TYPE_NOI_P2P_MAX;
	ret = sec_ts_read_frame(ts, mode.type, mode.min, mode.max);
	if (ret < 0) {
		LOGE("Failed to read frame TYPE_NOI_P2P_MAX, ret: %d", ret);
		goto exit;
	}

	for (i = 0; i < ts->rx_count; i++) {
		for (j = 0; j < ts->tx_count; j++) {
			index = i * ts->tx_count + j;
			if (temp_min[index] < ts->cmr_p2p_l) {
				LOGE("P2P min (%d,%d) = %d, min test limit %d", j, i,
						temp_min[index], ts->cmr_p2p_l);
				specover_count++;
			} else if (ts->pFrame[index] > ts->cmr_p2p_h) {
				LOGE("P2P max (%d,%d) = %d, max test limit %d", j, i,
						ts->pFrame[index], ts->cmr_p2p_h);
				specover_count++;
			} else if (ts->pFrame[index] - temp_min[index] > ts->cmr_p2p_h_l_max) {
				LOGE("P2P max-min (%d,%d) = %d, max test limit %d", j, i,
						ts->pFrame[index] - temp_min[index],
						ts->cmr_p2p_h_l_max);
				specover_count++;
			} else if (ts->pFrame[index] - temp_min[index] < ts->cmr_p2p_h_l_min) {
				LOGE("P2P max-min (%d,%d) = %d, min test limit %d", j, i,
						ts->pFrame[index] - temp_min[index],
						ts->cmr_p2p_h_l_min);
				specover_count++;
			}
		}
	}
*/
	if (specover_count == 0)
		cmd->result = GTI_SELFTEST_RESULT_PASS;
	else
		cmd->result = GTI_SELFTEST_RESULT_FAIL;

	/* Trigger reset to let touch back into normal mode. */
	sec_ts_system_reset(ts, RESET_MODE_SW, true, true);

exit:
	kfree(temp_min);

	return ret;
}

static int gti_set_gesture_config(void *private_data, struct gti_gesture_config_cmd *cmd)
{
	struct sec_ts_data *ts = private_data;
	int ret = 0;
	int i;
	bool is_sttw_changed = false;
	bool is_lptw_changed = false;

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		return -ENODEV;
	}

	/* check if LPTW params are trying to be updated */
	for (i = GTI_LPTW_MIN_X; i <= GTI_LPTW_INT2_DEASSERT_MAX_Y; i++) {
		if (cmd->updating_params[i]) {
			is_lptw_changed = true;
			break;
		}
	}

	if (cmd->updating_params[GTI_GESTURE_TYPE] &&
	    (cmd->params[GTI_GESTURE_TYPE] == GTI_GESTURE_LPTW ||
	     cmd->params[GTI_GESTURE_TYPE] == GTI_GESTURE_STTW_AND_LPTW)) {
		is_lptw_changed = true;
	}

	if (is_lptw_changed) {
		LOGE("LPTW gesture config is not supported\n");
		return 0;
	}

	for (i = GTI_STTW_MIN_X; i <= GTI_STTW_MAX_TOUCH_SIZE; i++) {
		if (cmd->updating_params[i]) {
			is_sttw_changed = true;
			break;
		}
	}

	if (!is_sttw_changed && !cmd->updating_params[GTI_GESTURE_TYPE])
		return 0;

	if (atomic_cmpxchg(&ts->sec.cmd_is_running, 0, 1)) {
		LOGE("other cmd is running.\n");
		return -EBUSY;
	}

	if (is_sttw_changed) {
		u8 sttw_param[3] = { 0 };
		for (i = GTI_STTW_MIN_X; i <= GTI_STTW_MAX_TOUCH_SIZE; i++) {
			if (!cmd->updating_params[i])
				continue;

			sttw_param[0] = i;
			sttw_param[1] = (cmd->params[i] >> 8) & 0xFF;
			sttw_param[2] = (cmd->params[i] >> 0) & 0xFF;
			ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
			if (ret) {
				LOGE("Failed to write STTW param %d, ret = %d\n", i, ret);
				goto exit;
			}
		}
	}

	if (cmd->updating_params[GTI_GESTURE_TYPE]) {
		u8 enb[1] = { 0 };

		ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_GESTURE_ENABLE, enb, 1);
		if (ret < 0) {
			LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_SET_GESTURE_ENABLE, ret);
			goto exit;
		}

		if (cmd->params[GTI_GESTURE_TYPE] == GTI_GESTURE_DISABLE) {
			enb[0] &= ~BIT(SEC_TS_GESTURE_CODE_STTW);
		} else if (cmd->params[GTI_GESTURE_TYPE] == GTI_GESTURE_STTW ||
			   cmd->params[GTI_GESTURE_TYPE] == GTI_GESTURE_STTW_AND_LPTW) {
			enb[0] |= BIT(SEC_TS_GESTURE_CODE_STTW);
		} else {
			goto exit;
		}

		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_ENABLE, enb, 1);
		if (ret) {
			LOGE("Failed to write SEC_TS_CMD_SET_GESTURE_ENABLE, ret = %d\n", ret);
			goto exit;
		}

		if (cmd->params[GTI_GESTURE_TYPE] != GTI_GESTURE_DISABLE) {
			sec_ts_set_power_mode(ts, TO_LOWPOWER_MODE);
		}
	}

exit:
	atomic_set(&ts->sec.cmd_is_running, 0);
	return ret;
}

void goog_gti_probe(struct sec_ts_data *ts)
{
	int retval = 0;
	struct gti_optional_configuration *options = NULL;

	options = devm_kzalloc(&ts->client->dev,
			sizeof(struct gti_optional_configuration), GFP_KERNEL);
	if (!options) {
		LOGE("devm_kzalloc gti_optional_configuration failed.");
		return;
	}

	options->get_coord_filter_enabled = gti_get_coord_filter_enabled;
	options->set_coord_filter_enabled = gti_set_coord_filter_enabled;
	options->get_palm_mode = gti_get_palm_mode;
	options->get_grip_mode = gti_get_grip_mode;
	options->set_irq_mode = gti_set_irq_mode;
	options->get_irq_mode = gti_get_irq_mode;
	options->get_scan_mode = gti_get_scan_mode;
	options->get_sensing_mode = gti_get_sensing_mode;
	options->get_mutual_sensor_data = gti_get_mutual_or_self_sensor_data;
	options->get_self_sensor_data = gti_get_mutual_or_self_sensor_data;
	options->get_screen_protector_mode = gti_get_screen_protector_mode;
	options->set_screen_protector_mode = gti_set_screen_protector_mode;
	options->get_water_mode = gti_get_water_mode;
	options->set_water_mode = gti_set_water_mode;
	options->get_vendor_register = gti_get_vendor_register;
	options->reset = gti_reset;
	options->selftest = gti_selftest;
	options->set_gesture_config = gti_set_gesture_config;

	/* release the interrupt and register the gti irq later. */
	free_irq(ts->plat_data->irq, ts);
	ts->gti = goog_touch_interface_connect(&ts->client->dev, gti_default_handler, options);
	if (!ts->gti) {
		LOGE("gti probe failed.");
		goto exit;
	}

	retval = goog_pm_register_notification(ts->gti, &sec_ts_dev_pm_ops);
	if (retval < 0)
		LOGE("Failed to register GTI pm");

	retval = goog_devm_request_threaded_irq(ts->gti, &ts->client->dev,
			ts->plat_data->irq, goog_sec_ts_isr, goog_sec_ts_irq_thread,
			ts->plat_data->irq_type, SEC_TS_NAME, ts);
	if (retval < 0)
		LOGE("Failed to request GTI IRQ");
	else
		atomic_set(&ts->irq_enabled, 1);

	ts->raw_timestamp_sensing = 0;
	goog_notify_fw_status_changed(ts->gti, GTI_FW_STATUS_RESET, NULL);

	return;
exit:
	if (options)
		devm_kfree(&ts->client->dev, options);
}

#endif /* IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */
