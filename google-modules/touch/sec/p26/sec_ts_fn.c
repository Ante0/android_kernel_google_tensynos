/* drivers/input/touchscreen/sec_ts_fn.c
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 * http://www.samsungsemi.com/
 *
 * Core file for Samsung TSC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "sec_ts.h"
#ifdef USE_SPEC_CHECK
#include "sec_ts_fac_spec.h"
#endif

static void get_device_id(void *device_data);
static void fw_update(void *device_data);
static void get_fw_ver_bin(void *device_data);
static void get_fw_ver_ic(void *device_data);
static void get_config_ver(void *device_data);
static void module_off_master(void *device_data);
static void module_on_master(void *device_data);
static void get_chip_vendor(void *device_data);
static void get_chip_name(void *device_data);
static void get_chip_id(void *device_data);
static void get_wet_mode(void *device_data);
static void get_x_num(void *device_data);
static void get_y_num(void *device_data);
static void get_x_cross_routing(void *device_data);
static void get_y_cross_routing(void *device_data);
static void run_reference_read(void *device_data);
static void get_node(void *device_data);
static void run_rawcap_read(void *device_data);
static void run_rawcap_factory_read(void *device_data);
static void run_rawcap_gap_read(void *device_data);
static void run_rawcap_highfreq_read(void *device_data);
static void run_rawcap_highfreq_gap_read(void *device_data);
static void run_trx_short_test(void *device_data);
static void run_delta_read(void *device_data);
static void run_rawdata_stdev_read(void *device_data);
static void run_rawdata_p2p_read(void *device_data);
static void run_rawdata_read_type(void *device_data);
static void run_rawdata_read_all(void *device_data);
static void run_self_rawcap_gap_read(void *device_data);
static void run_force_calibration(void *device_data);
static void enable_coordinate_report(void *device_data);
static void glove_mode(void *device_data);
static void clear_cover_mode(void *device_data);
static void dead_zone_enable(void *device_data);
static void set_lowpower_mode(void *device_data);
static void set_grip_data(void *device_data);
static void set_log_level(void *device_data);
static void debug(void *device_data);
static void set_touch_mode(void *device_data);
static void not_support_cmd(void *device_data);
static void set_palm_detection_enable(void *device_data);
static void set_grip_detection_enable(void *device_data);
static void set_coord_filter_enable(void *device_data);
static void set_continuous_report_enable(void *device_data);
static void set_wet_mode_enable(void *device_data);
static void set_noise_mode_enable(void *device_data);
static void set_print_format(void *device_data);
static void enter_recovery(void *device_data);
static void ddi_osc_on(void *device_data);
static void set_hopping_freq(void *device_data);
static void spi_checksum_enable(void *device_data);
static void get_sync_freq(void *device_data);
static void heatmap_enable(void *device_data);
static void sttw_gesture_enable(void *device_data);
static void sttw_invalid_gesture_enable(void *device_data);
static void high_sensitivity_mode_enable(void *device_data);
static void set_freq(void *device_data);
static void get_sensing_info(void *device_data);

static struct sec_cmd sec_cmds[] = {
	{SEC_CMD("get_device_id", get_device_id),},
	{SEC_CMD("fw_update", fw_update),},
	{SEC_CMD("get_fw_ver_bin", get_fw_ver_bin),},
	{SEC_CMD("get_fw_ver_ic", get_fw_ver_ic),},
	{SEC_CMD("get_config_ver", get_config_ver),},
	{SEC_CMD("module_off_master", module_off_master),},
	{SEC_CMD("module_on_master", module_on_master),},
	{SEC_CMD("get_chip_vendor", get_chip_vendor),},
	{SEC_CMD("get_chip_name", get_chip_name),},
	{SEC_CMD("get_chip_id", get_chip_id),},
	{SEC_CMD("get_wet_mode", get_wet_mode),},
	{SEC_CMD("get_x_num", get_x_num),},
	{SEC_CMD("get_y_num", get_y_num),},
	{SEC_CMD("get_x_cross_routing", get_x_cross_routing),},
	{SEC_CMD("get_y_cross_routing", get_y_cross_routing),},
	{SEC_CMD("run_reference_read", run_reference_read),},
	{SEC_CMD("get_node", get_node),},
	{SEC_CMD("run_rawcap_read", run_rawcap_read),},
	{SEC_CMD("run_rawcap_factory_read", run_rawcap_factory_read),},
	{SEC_CMD("run_rawcap_gap_read", run_rawcap_gap_read),},
	{SEC_CMD("run_rawcap_highfreq_read", run_rawcap_highfreq_read),},
	{SEC_CMD("run_rawcap_highfreq_gap_read", run_rawcap_highfreq_gap_read),},
	{SEC_CMD("run_trx_short_test", run_trx_short_test),},
	{SEC_CMD("run_delta_read", run_delta_read),},
	{SEC_CMD("run_rawdata_stdev_read", run_rawdata_stdev_read),},
	{SEC_CMD("run_rawdata_p2p_read", run_rawdata_p2p_read),},
	{SEC_CMD("run_rawdata_read_type", run_rawdata_read_type),},
	{SEC_CMD("run_rawdata_read_all", run_rawdata_read_all),},
	{SEC_CMD("run_self_rawcap_gap_read", run_self_rawcap_gap_read),},
	{SEC_CMD("run_force_calibration", run_force_calibration),},
	{SEC_CMD("enable_coordinate_report", enable_coordinate_report),},
	{SEC_CMD("glove_mode", glove_mode),},
	{SEC_CMD("clear_cover_mode", clear_cover_mode),},
	{SEC_CMD("dead_zone_enable", dead_zone_enable),},
	{SEC_CMD("set_lowpower_mode", set_lowpower_mode),},
	{SEC_CMD("set_grip_data", set_grip_data),},
	{SEC_CMD("set_log_level", set_log_level),},
	{SEC_CMD("debug", debug),},
	{SEC_CMD("set_touch_mode", set_touch_mode),},
	{SEC_CMD("set_palm_detection_enable", set_palm_detection_enable),},
	{SEC_CMD("set_grip_detection_enable", set_grip_detection_enable),},
	{SEC_CMD("set_coord_filter_enable", set_coord_filter_enable),},
	{SEC_CMD("set_continuous_report_enable", set_continuous_report_enable),},
	{SEC_CMD("set_wet_mode_enable", set_wet_mode_enable),},
	{SEC_CMD("set_noise_mode_enable", set_noise_mode_enable),},
	{SEC_CMD("set_print_format", set_print_format),},
	{SEC_CMD("enter_recovery", enter_recovery),},
	{SEC_CMD("ddi_osc_on", ddi_osc_on),},
	{SEC_CMD("set_hopping_freq", set_hopping_freq),},
	{SEC_CMD("spi_checksum_enable", spi_checksum_enable),},
	{SEC_CMD("get_sync_freq", get_sync_freq),},
	{SEC_CMD("heatmap_enable", heatmap_enable),},
	{SEC_CMD("sttw_gesture_enable", sttw_gesture_enable),},
	{SEC_CMD("sttw_invalid_gesture_enable", sttw_invalid_gesture_enable),},
	{SEC_CMD("high_sensitivity_mode_enable", high_sensitivity_mode_enable),},
	{SEC_CMD("set_freq", set_freq),},
	{SEC_CMD("get_sensing_info", get_sensing_info),},
	{SEC_CMD("not_support_cmd", not_support_cmd),},
};

void sec_ts_print_data(struct sec_ts_data *ts, u32 size, u8 *data)
{
	int i, j;
	int unit = 16;
	int cnt;

	cnt = (int)(size / unit);
	cnt = (size % unit) ? (cnt + 1): cnt;

	LOGI("data size: %d(0x%04X)\n", size, size);
	for (i = 0; i < cnt; i++) {
		j = i * unit;
		LOGI("0x%04X: %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X %02X%02X%02X%02X\n",
			j, data[j + 0], data[j + 1], data[j + 2], data[j + 3],
			data[j + 4], data[j + 5], data[j + 6], data[j + 7],
			data[j + 8], data[j + 9], data[j + 10], data[j + 11],
			data[j + 12], data[j + 13], data[j + 14], data[j + 15]);
	}

	return;
}

static void get_device_id(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[20] = { 0 };
	int ret;
	u8 deviceID[6];

	sec_cmd_set_default_result(sec);

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_ID, deviceID, 6);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_READ_ID, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%02X %02X %02X %02X %02X %02X",
			deviceID[0], deviceID[1], deviceID[2],
			deviceID[3], deviceID[4], deviceID[5]);

	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	LOGI("%s\n", buff);
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;
}

static void set_palm_detection_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] == 1)
		para = 0x01; // (status event report)
	else if (sec->cmd_param[0] == 2)
		para = 0x11; // (finger event report)
	else if (sec->cmd_param[0] == 0)
		para = 0x0;
	else {
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_PALM_DETECT, &para, 1);
	if (ret < 0) {
		LOGE("write reg %#x para %#x failed, returned %i\n",
			SEC_TS_CMD_SET_PALM_DETECT, para, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_grip_detection_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] == 1)
		para = 0x1D; // (status event report)
	else if (sec->cmd_param[0] == 2)
		para = 0x9D; // (finger event report)
	else if (sec->cmd_param[0] == 0)
		para = 0x0;
	else {
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GRIP_DETECT, &para, 1);
	if (ret < 0) {
		LOGE("write reg %#x para %#x failed, returned %i\n",
			SEC_TS_CMD_SET_GRIP_DETECT, para, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_coord_filter_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] == 1)
		para = 0x5F;
	else if (sec->cmd_param[0] == 0)
		para = 0x0;
	else {
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_COORD_FILTER, &para, 1);
	if (ret < 0) {
		LOGE("write reg %#x para %#x failed, returned %i\n",
			SEC_TS_CMD_SET_COORD_FILTER, para, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_continuous_report_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] == 1)
		para = 0x1;
	else if (sec->cmd_param[0] == 0)
		para = 0x0;
	else {
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_CONT_REPORT, &para, 1);
	if (ret < 0) {
		LOGE("write reg %#x para %#x failed, returned %i\n",
			SEC_TS_CMD_SET_CONT_REPORT, para, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_wet_mode_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 3) {
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	para = sec->cmd_param[0];

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_WET_MODE, &para, 1);
	if (ret < 0) {
		LOGE("write reg %#x para %#x failed, returned %i\n",
			SEC_TS_CMD_SET_WET_MODE, para, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_noise_mode_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] == 0) {
		para = NOISE_MODE_DEFAULT;
	} else if (sec->cmd_param[0] == 1) {
		para = NOISE_MODE_OFF;
	} else if (sec->cmd_param[0] == 2) {
		para = NOISE_MODE_FORCE_ON;
	} else {
		LOGI("param error! param = %d\n",sec->cmd_param[0]);
		goto err_out;
	}

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_NOISE_MODE, &para, 1);
	if (ret < 0) {
		LOGE("write reg %#x para %#x failed, returned %i\n",
			SEC_TS_CMD_SET_NOISE_MODE, para, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static ssize_t scrub_pos_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[256] = { 0 };

#ifdef CONFIG_SAMSUNG_PRODUCT_SHIP
	LOGI("scrub_id: %d\n", ts->scrub_id);
#else
	LOGI("scrub_id: %d, X: %d, Y: %d\n", ts->scrub_id, ts->scrub_x, ts->scrub_y);
#endif
	snprintf(buff, sizeof(buff), "%d %d %d",
		 ts->scrub_id, ts->scrub_x, ts->scrub_y);

	ts->scrub_x = 0;
	ts->scrub_y = 0;

	return snprintf(buf, PAGE_SIZE, "%s", buff);
}

static DEVICE_ATTR_RO(scrub_pos);

static ssize_t ito_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[256] = { 0 };

	LOGI("%02X%02X%02X%02X\n",
		ts->ito_test[0], ts->ito_test[1],
		ts->ito_test[2], ts->ito_test[3]);

	snprintf(buff, sizeof(buff), "%02X%02X%02X%02X",
		ts->ito_test[0], ts->ito_test[1],
		ts->ito_test[2], ts->ito_test[3]);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%s", buff);
}

static ssize_t raw_check_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	int ii, ret = 0;
	char *buffer = NULL;
	char temp[CMD_RESULT_WORD_LEN] = { 0 };


	buffer = vzalloc(ts->rx_count * ts->tx_count * 6);
	if (!buffer)
		return -ENOMEM;

	memset(buffer, 0x00, ts->rx_count * ts->tx_count * 6);

	for (ii = 0; ii < (ts->rx_count * ts->tx_count - 1); ii++) {
		snprintf(temp, CMD_RESULT_WORD_LEN, "%d ", ts->pFrame[ii]);
		strncat(buffer, temp, CMD_RESULT_WORD_LEN);

		memset(temp, 0x00, CMD_RESULT_WORD_LEN);
	}

	snprintf(temp, CMD_RESULT_WORD_LEN, "%d", ts->pFrame[ii]);
	strncat(buffer, temp, CMD_RESULT_WORD_LEN);

	ret = snprintf(buf, ts->rx_count * ts->tx_count * 6, buffer);
	vfree(buffer);

	return ret;
}

static ssize_t multi_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	LOGI("%d\n", ts->multi_count);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", ts->multi_count);
}

static ssize_t multi_count_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	ts->multi_count = 0;

	LOGI("clear\n");

	return count;
}

static ssize_t wet_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	LOGI("%d, %d\n", ts->wet_count, ts->dive_count);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", ts->wet_count);
}

static ssize_t wet_mode_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	ts->wet_count = 0;
	ts->dive_count = 0;

	LOGI("clear\n");

	return count;
}

static ssize_t comm_err_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	LOGI("%d\n", ts->multi_count);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", ts->comm_err_count);
}

static ssize_t comm_err_count_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	ts->comm_err_count = 0;

	LOGI("clear\n");

	return count;
}

static ssize_t module_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[256] = { 0 };

	LOGI("%d\n", ts->multi_count);

	snprintf(buff, sizeof(buff), "SE%02X%02X%02X%02X%02X",
		ts->plat_data->panel_revision,
		ts->plat_data->img_version_of_bin[2],
		ts->plat_data->img_version_of_bin[3],
		ts->pressure_cal_base, ts->pressure_cal_delta);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%s", buff);
}

static ssize_t vendor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	unsigned char buffer[10] = { 0 };

	snprintf(buffer, 5, ts->plat_data->firmware_name + 8);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "LSI_%s", buffer);
}

static ssize_t checksum_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	ts->checksum_result = 0;

	LOGI("clear\n");

	return count;
}

static ssize_t checksum_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	LOGI("%d\n", ts->checksum_result);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%d", ts->checksum_result);
}

static ssize_t holding_time_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	ts->longest_duration = 0;

	LOGI("clear\n");

	return count;
}

static ssize_t holding_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	LOGI("%lld ms\n", ts->longest_duration);

	return snprintf(buf, SEC_CMD_BUF_SIZE, "%lld ms", ts->longest_duration);
}

static ssize_t all_touch_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	LOGI("touch: %d, force: %d, aod: %d, spay: %d\n",
		ts->all_finger_count, ts->all_force_count,
		ts->all_aod_tap_count, ts->all_spay_count);

	return snprintf(buf, SEC_CMD_BUF_SIZE,
			"\"TTCN\":\"%d\",\"TFCN\":\"%d\",\"TACN\":\"%d\",\"TSCN\":\"%d\"",
			ts->all_finger_count, ts->all_force_count,
			ts->all_aod_tap_count, ts->all_spay_count);
}

static ssize_t all_touch_count_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	ts->all_force_count = 0;
	ts->all_aod_tap_count = 0;
	ts->all_spay_count = 0;

	LOGI("clear\n");

	return count;
}

static ssize_t z_value_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	LOGI("max: %d, min: %d, avg: %d\n", ts->max_z_value, ts->min_z_value, ts->sum_z_value);

	if (ts->all_finger_count)
		return snprintf(buf, SEC_CMD_BUF_SIZE,
				"\"TMXZ\":\"%d\",\"TMNZ\":\"%d\",\"TAVZ\":\"%d\"",
				ts->max_z_value, ts->min_z_value,
				ts->sum_z_value / ts->all_finger_count);
	else
		return snprintf(buf, SEC_CMD_BUF_SIZE,
				"\"TMXZ\":\"%d\",\"TMNZ\":\"%d\"",
				ts->max_z_value, ts->min_z_value);

}

static ssize_t z_value_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);

	ts->max_z_value = 0;
	ts->min_z_value = 0xFFFFFFFF;
	ts->sum_z_value = 0;
	ts->all_finger_count = 0;

	LOGI("clear\n");

	return count;
}

static ssize_t fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	int ret, written = 0;
	u8 data[6];

	/* If there is no FW file available,
	 * sec_ts_save_version_of_ic() and sec_ts_save_version_of_bin() will
	 * no be called. Need to get through SEC_TS_CMD_GET_FW_VERSION cmd.
	 */
	if (ts->plat_data->panel_revision == 0 &&
		ts->plat_data->img_version_of_bin[2] == 0 &&
		ts->plat_data->img_version_of_bin[3] == 0) {
		u8 fw_ver[4];

		ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_FW_VERSION, fw_ver, 4);
		if (ret < 0) {
			LOGE("firmware version read error\n");
			goto out;
		}
		written += scnprintf(buf + written, PAGE_SIZE - written,
			"SE-V%02X,FW-V%02X.%02X.%02X.%02X\n",
			ts->plat_data->panel_revision,
			fw_ver[0], fw_ver[1], fw_ver[2], fw_ver[3]);
		written += scnprintf(buf + written, PAGE_SIZE - written,
			"FW file: N/A\n");
	} else {
		written += scnprintf(buf + written, PAGE_SIZE - written,
			"SE-V%02X,FW-V%02X.%02X.%02X.%02X\n",
			ts->plat_data->panel_revision,
			ts->plat_data->img_version_of_ic[0],
			ts->plat_data->img_version_of_ic[1],
			ts->plat_data->img_version_of_ic[2],
			ts->plat_data->img_version_of_ic[3]);
		written += scnprintf(buf + written, PAGE_SIZE - written,
			"FW file: %s\n",
			ts->plat_data->firmware_name);
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_ID, data, 6);
	if (ret < 0) {
		LOGE("failed to read device id(%d)\n", ret);
		goto out;
	}
	written += scnprintf(buf + written, PAGE_SIZE - written,
		"ID: %02X %02X %02X %02X %02X %02X\n",
		data[0], data[1], data[2], data[3], data[4], data[5]);
out:

	return written;
}

static ssize_t status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct sec_cmd_data *sec = dev_get_drvdata(dev);
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	int written = 0;
	unsigned char data[6] = { 0 };
	int ret;

	memset(data, 0x0, 6);
	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_ID, data, 6);
	if (ret < 0) {
		LOGE("failed to read boot status(%d)\n", ret);
		goto out;
	}
	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Boot status: %#x\n", data[0]);

	memset(data, 0x0, 2);
	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, data, 2);
	if (ret < 0) {
		LOGE("Failed to touch status(%d)\n", ret);
		goto out;
	}
	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Touch mode: %#x, status: %#x\n",
			     data[0], data[1]);

	memset(data, 0x0, 2);
	ret = ts->sec_ts_read(ts, SEC_TS_CMD_TOUCH_FUNCTION, data, 2);
	if (ret < 0) {
		LOGE("failed to read touch functions(%d)\n", ret);
		goto out;
	}
	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Functions: %#x, %#x\n", data[0], data[1]);
	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Wet mode: %d\n", ts->wet_mode);
	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Fingers#: %d\n", ts->touch_count);
	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Report rate: %d\n", ts->report_rate);
	written += scnprintf(buf + written, PAGE_SIZE - written,
			     "Vsync: %d\n", ts->vsync);
out:

	return written;
}

static DEVICE_ATTR_RO(ito_check);
static DEVICE_ATTR_RO(raw_check);
static DEVICE_ATTR_RW(multi_count);
static DEVICE_ATTR_RW(wet_mode);
static DEVICE_ATTR_RW(comm_err_count);
static DEVICE_ATTR_RW(checksum);
static DEVICE_ATTR_RW(holding_time);
static DEVICE_ATTR_RW(all_touch_count);
static DEVICE_ATTR_RW(z_value);
static DEVICE_ATTR_RO(module_id);
static DEVICE_ATTR_RO(vendor);
static DEVICE_ATTR_RO(fw_version);
static DEVICE_ATTR_RO(status);


static struct attribute *cmd_attributes[] = {
	&dev_attr_scrub_pos.attr,
	&dev_attr_ito_check.attr,
	&dev_attr_raw_check.attr,
	&dev_attr_multi_count.attr,
	&dev_attr_wet_mode.attr,
	&dev_attr_comm_err_count.attr,
	&dev_attr_checksum.attr,
	&dev_attr_holding_time.attr,
	&dev_attr_all_touch_count.attr,
	&dev_attr_z_value.attr,
	&dev_attr_module_id.attr,
	&dev_attr_vendor.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_status.attr,
	NULL,
};

static struct attribute_group cmd_attr_group = {
	.attrs = cmd_attributes,
};

static int sec_ts_check_index(struct sec_ts_data *ts)
{
	struct sec_cmd_data *sec = &ts->sec;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int node;

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > ts->tx_count
		|| sec->cmd_param[1] < 0 || sec->cmd_param[1] > ts->rx_count) {

		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		LOGI("parameter error: %u, %u\n", sec->cmd_param[0], sec->cmd_param[0]);
		node = -1;
		return node;
	}
	node = sec->cmd_param[1] * ts->tx_count + sec->cmd_param[0];
	LOGI("node = %d\n", node);

	return node;
}
static void fw_update(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[64] = { 0 };
	int retval = 0;

	sec_cmd_set_default_result(sec);
	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("[ERROR] Touch is stopped\n");
		snprintf(buff, sizeof(buff), "%s", "TSP turned off");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;

		return;
	}

	retval = sec_ts_firmware_update_on_hidden_menu(ts, sec->cmd_param[0]);
	if (retval < 0) {
		snprintf(buff, sizeof(buff), "%s", "NA");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		LOGE("failed [%d]\n", retval);
	} else {
		snprintf(buff, sizeof(buff), "%s", "OK");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_OK;
		LOGI("success [%d]\n", retval);
	}

}

int sec_ts_set_power_mode(struct sec_ts_data *ts, u8 mode)
{
	int ret;
	u8 tmode[1] = { mode };

	LOGI("mode %d\n", mode);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_POWER_MODE, tmode, 1);
	if (ret < 0)
		LOGE("write reg %#x failed, return %i\n", SEC_TS_CMD_POWER_MODE, ret);
	sec_ts_delay(20);

	return ret;
}

int sec_ts_fix_tmode(struct sec_ts_data *ts, u8 mode, u8 state)
{
	int ret;
	u8 tBuff[2] = { mode, state };

	LOGI("mode %d state %d\n", mode, state);

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			(u8 *)&(ts->touch_functions), 2);

	if (ret < 0)
		LOGE("Failed to read touch func_mode command\n");

	ts->touch_functions &= ~((u16)SEC_TS_BIT_SETFUNC_STATE_MANAGEMENT_ON) & 0xFFFF;

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			(u8 *)&ts->touch_functions, 2);
	if (ret < 0)
		LOGE("Failed to write touch func_mode command");

	sec_ts_delay(20);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SYSTEM_MODE, tBuff, sizeof(tBuff));
	if (ret < 0)
		LOGE("Failed to write system_mode command\n");

	sec_ts_delay(20);

	return ret;
}

int sec_ts_release_tmode(struct sec_ts_data *ts)
{
	int ret;

	LOGI("\n");

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_TOUCH_FUNCTION,
		(u8 *)&(ts->touch_functions), 2);

	if (ret < 0)
		LOGE("Failed to read touch func_mode command\n");

	ts->touch_functions |= SEC_TS_BIT_SETFUNC_STATE_MANAGEMENT_ON;

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			(u8 *)&ts->touch_functions, 2);
	if (ret < 0) {
		LOGE("Failed to write touch func_mode command");
	}

	sec_ts_delay(20);

	return ret;
}

/* sec_ts_cm_gap_spec_check :
 * apply gap calculation with ts->pFrameMS data
 * gap = abs(N1 - N2) / MAX(N1, N2) * 100 (%)
 */
static int sec_ts_cm_gap_spec_check(struct sec_ts_data *ts,
					short *pFrameMS, short *gap, bool gap_dir)
{
	int i = 0;
	int j = 0;
	int gapx, gapy, pos1, pos2;
	short dpos1, dpos2;
	int specover_count = 0;

	/* Get x-direction cm gap */
	if (!gap_dir) {
		LOGD("gapX TX\n");

		for (i = 0; i < ts->rx_count; i++) {
			for (j = 0; j < ts->tx_count - 1; j++) {
				/* Exclude last line to get gap between two
				 * lines.
				 */
				pos1 = (i * ts->tx_count) + j;
				pos2 = (i * ts->tx_count) + (j + 1);

				dpos1 = pFrameMS[pos1];
				dpos2 = pFrameMS[pos2];

				if (dpos1 > dpos2)
					gapx = 100 - (dpos2 * 100 / dpos1);
				else
					gapx = 100 - (dpos1 * 100 / dpos2);

				gap[pos1] = gapx;
#ifdef USE_SPEC_CHECK
				if (gapx > cm_gap[i][j])
					specover_count++;
#endif
			}
		}
	}

	/* get y-direction cm gap */
	else {
		LOGD("gapY RX\n");

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

				gap[pos1] = gapy;
#ifdef USE_SPEC_CHECK
				if (gapy > cm_gap[i][j])
					specover_count++;
#endif
			}
		}
	}

#ifdef USE_SPEC_CHECK
	LOGI("Gap NG for %d node(s)\n",
			gap_dir == 0 ? "gapX" : "gapY", specover_count);
#else
	LOGI("\n");
#endif

	return specover_count;
}

/* sec_ts_cs_gap_spec_check :
 * apply gap calculation with `pFrame` data.
 * Please notice that `pFrame` will be changed dynamically by request.
 * It could be `ts->pFrameSS` or `ts->pFrameMS` for corresponding purpose.
 */
static int sec_ts_cs_gap_spec_check(struct sec_ts_data *ts,
					short *pFrameSS, short *gap)
{
	int i;
	int specover_count = 0;
	short dTmp;

	for (i = 0; i < ts->rx_count - 1; i++) {
		dTmp = pFrameSS[i] - pFrameSS[i + 1];
		if (dTmp < 0)
			dTmp *= -1;

		gap[i] = dTmp;
#ifdef USE_SPEC_CHECK
		if (dTmp > cs_rx_gap)
			specover_count++;
#endif
	}

	for (i = ts->rx_count; i < (ts->tx_count + ts->rx_count - 1); i++) {
		dTmp = pFrameSS[i] - pFrameSS[i + 1];
		if (dTmp < 0)
			dTmp *= -1;

		gap[i] = dTmp;
#ifdef USE_SPEC_CHECK
		if (dTmp > cs_tx_gap)
			specover_count++;
#endif
	}

#ifdef USE_SPEC_CHECK
	LOGI("Gap NG for %d node(s)\n", specover_count);
#else
	LOGI("\n");
#endif

	return specover_count;
}

static void sec_ts_min_max_spec_check(struct sec_ts_data *ts,
					struct sec_ts_test_mode *mode)
{
	short *pFrameMS = ts->pFrameMS;
	short *pFrameSS = ts->pFrameSS;
	short *min = mode->min;
	short *max = mode->max;
	enum spec_check_type *spec_check = &mode->spec_check;
#ifdef USE_SPEC_CHECK
	u8 type = mode->type;
	int i = 0;
	int j = 0;
#endif

	if ((pFrameMS == NULL && pFrameSS == NULL)
		|| min == NULL
		|| max == NULL
		|| spec_check == NULL)
		return;

	LOGI("\n");

#ifdef USE_SPEC_CHECK
	/* mutual */
	if (*spec_check == SPEC_CHECK && pFrameMS != NULL) {
		int specover_count = 0;
		short dTmp = 0;

		if (type == TYPE_OFFSET_DATA_SDC) {
			unsigned int region = 0;

			for (i = 0; i < REGION_TYPE_COUNT; i++) {
				min[i] = SHRT_MAX;
				max[i] = SHRT_MIN;
			}

			/* get min, max */
			for (i = 0; i < ts->rx_count; i++) {
				for (j = 0; j < ts->tx_count; j++) {
					dTmp = pFrameMS[i * ts->tx_count + j];
					region = cm_region[i][j];

					if (region == REGION_NOTCH)
						continue;

					min[region] = min(min[region], dTmp);
					max[region] = max(max[region], dTmp);

					if (dTmp > cm_max[region])
						specover_count++;
					if (dTmp < cm_min[region])
						specover_count++;
				}
			}
			LOGI("type = %d, specover = %d\n", type, specover_count);

			if (specover_count == 0 &&
			    (max[REGION_NORMAL] - min[REGION_NORMAL] < cm_mm[REGION_NORMAL]) &&
			    (max[REGION_EDGE]   - min[REGION_EDGE]   < cm_mm[REGION_EDGE])   &&
			    (max[REGION_CORNER] - min[REGION_CORNER] < cm_mm[REGION_CORNER]))
				*spec_check = SPEC_PASS;
			else
				*spec_check = SPEC_FAIL;
		} else if (type == TYPE_NOI_P2P_MIN) {
			/* check p2p min */
			for (i = 0; i < ts->rx_count; i++) {
				for (j = 0; j < ts->tx_count; j++) {
					dTmp = pFrameMS[i * ts->tx_count + j];
					if (cm_region[i][j] != REGION_NOTCH &&
						dTmp < noi_min[i][j])
						specover_count++;
				}
			}
			LOGI("type = %d, specover = %d\n", type, specover_count);

			if (specover_count == 0)
				*spec_check = SPEC_PASS;
			else
				*spec_check = SPEC_FAIL;
		} else if (type == TYPE_NOI_P2P_MAX) {
			/* check p2p max */
			for (i = 0; i < ts->rx_count; i++) {
				for (j = 0; j < ts->tx_count; j++) {
					dTmp = pFrameMS[i * ts->tx_count + j];
					if (cm_region[i][j] != REGION_NOTCH &&
						dTmp > noi_max[i][j])
						specover_count++;
				}
			}
			LOGI("type = %d, mutual specover = %d\n", type, specover_count);

			if (specover_count == 0)
				*spec_check = SPEC_PASS;
			else
				*spec_check = SPEC_FAIL;
		}
	}

	/* self */
	if (*spec_check == SPEC_CHECK && pFrameSS != NULL) {
		int specover_count = 0;

		if (type == TYPE_OFFSET_DATA_SDC) {
			min[0] = min[1] = SHRT_MAX;
			max[0] = max[1] = SHRT_MIN;

			/* get min, max */
			for (i = 0; i < ts->tx_count; i++) {
				if (pFrameSS[i] > cs_tx_max)
					specover_count++;
				if (pFrameSS[i] < cs_tx_min)
					specover_count++;
				min[0] = min(min[0], pFrameSS[i]);
				max[0] = max(max[0], pFrameSS[i]);
			}
			for (i = ts->tx_count;
			     i < ts->tx_count + ts->rx_count; i++) {
				if (pFrameSS[i] > cs_rx_max)
					specover_count++;
				if (pFrameSS[i] < cs_rx_min)
					specover_count++;
				min[1] = min(min[1], pFrameSS[i]);
				max[1] = max(max[1], pFrameSS[i]);
			}
		}

		LOGI("type : %d, self specover = %d\n", type, specover_count);
		if (specover_count == 0 &&
			(max[0] - min[0]) < cs_tx_mm &&
			(max[1] - min[1]) < cs_rx_mm)
			*spec_check = SPEC_PASS;
		else
			*spec_check = SPEC_FAIL;
	}
#else
	*spec_check = SPEC_PASS;
#endif

	return;
}

static void sec_ts_print_frame(struct sec_ts_data *ts)
{
	int i = 0;
	int j = 0;
	const unsigned int buff_size = 7 * (ts->tx_count + 3);
	unsigned int buff_len = 0;
	unsigned char *pStr = NULL;

	pStr = kzalloc(buff_size, GFP_KERNEL);
	if (pStr == NULL)
		return;

	/* tx channel num print */
	buff_len += scnprintf(pStr + buff_len, buff_size - buff_len,
				"     |  ");
	for (i = 0; i < ts->tx_count; i++)
		buff_len += scnprintf(pStr + buff_len, buff_size - buff_len,
				"  Tx%02d ", i);
	buff_len += scnprintf(pStr + buff_len, buff_size - buff_len,
		"|   SELF");
	LOGI("%s\n", pStr);

	/* dash bar print */
	buff_len = 0;
	memset(pStr, 0x0, buff_size);
	buff_len += scnprintf(pStr + buff_len, buff_size - buff_len, "-----+-");
	for (i = 0; i < (ts->tx_count + 1); i++)
		buff_len += scnprintf(pStr + buff_len, buff_size - buff_len,
				"-------");
	LOGI("%s\n", pStr);

	/* rawdata print */
	for (i = 0; i < ts->rx_count; i++) {
		buff_len = 0;
		memset(pStr, 0x0, buff_size);
		buff_len += scnprintf(pStr + buff_len, buff_size - buff_len,
				"Rx%02d | ", i);

		/* mutual */
		for (j = 0; j < ts->tx_count; j++) {
			buff_len += scnprintf(pStr + buff_len,
				buff_size - buff_len,
				" %6d", ts->pFrame[(i * ts->tx_count) + j]);
		}
		/* self */
		buff_len += scnprintf(pStr + buff_len, buff_size - buff_len,
			" | %6d", ts->pFrameSS[i]);

		LOGI("%s\n", pStr);
	}

	buff_len = 0;
	memset(pStr, 0x0, buff_size);
	buff_len += scnprintf(pStr + buff_len, buff_size - buff_len,
			"SELF | ");
	for (j = 0; j < ts->tx_count; j++) {
		buff_len += scnprintf(pStr + buff_len,
			buff_size - buff_len,
			" %6d", ts->pFrameSS[ts->rx_count + j]);
	}
	LOGI("%s\n", pStr);

	kfree(pStr);
}

int sec_ts_read_frame(struct sec_ts_data *ts, u8 w_type, short *min, short *max)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	const int tx = ts->tx_count;
	const int rx = ts->rx_count;
	const int readbytes = (tx + 2) * rx * 2;
	unsigned char *pRead = NULL;
	u8 mode = TYPE_INVALID_DATA;
	short *pTemp = NULL;

	LOGI("\n");

	pRead = kzalloc(readbytes, GFP_KERNEL);
	if (!pRead)
		return -ENOMEM;

	pTemp = kzalloc(readbytes, GFP_KERNEL);
	if (!pTemp)
		goto ErrorExit;

	/* Set raw type to TYPE_INVALID_DATA */
	// mode = TYPE_INVALID_DATA;
	// ret = ts->sec_ts_write(ts, SEC_TS_CMD_RAWDATA_TYPE, &mode, 1);
	// if (ret < 0)
	// 	LOGE("Failed to recover rawdata type\n");

	/* Set raw type to type */
	mode = w_type;
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_RAWDATA_TYPE, &mode, 1);
	if (ret < 0) {
		LOGE("Failed to set rawdata type\n");
		goto ErrorRelease;
	}
	ts->frame_type = w_type;

	sec_ts_delay(50);

	/* read data */
	ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_REPORT, pRead, readbytes);
	if (ret < 0) {
		LOGE("read rawdata failed!\n");
		goto ErrorRelease;
	}

	for (i = 0; i < readbytes; i += 2) {
		pTemp[i / 2] = pRead[i] + (pRead[i + 1] << 8);
		if (pTemp[i / 2] < *min)
			*min = ts->pFrame[i / 2];
		if (pTemp[i / 2] > *max)
			*max = ts->pFrame[i / 2];
	}

	/* flip mutual data */
	short *pFrame_ptr = ts->pFrame;
	for (j = 0; j < rx; j++) {
		for (i = 0; i < tx; i++) {
			*pFrame_ptr++ = pTemp[i * rx + j];
		}
	}

	/* copy self data */
	short *ss_src = &pTemp[tx * rx];
	memcpy(ts->pFrameSS, ss_src, rx * sizeof(short));
	memcpy(&ts->pFrameSS[rx], &ss_src[rx], tx * sizeof(short));

	sec_ts_print_frame(ts);

ErrorRelease:
	/* release data monitory (unprepare AFE data memory) */
	mode = TYPE_INVALID_DATA;
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_RAWDATA_TYPE, &mode, 1);
	if (ret < 0)
		LOGE("Set rawdata type failed\n");
	else
		ts->frame_type = mode;

	kfree(pTemp);

ErrorExit:
	kfree(pRead);

	return ret;
}

static int sec_ts_save_mutual_raw_to_buffer(struct sec_ts_data *ts,
		struct sec_cmd_data *sec, struct sec_ts_test_mode *mode,
		char *buff, const unsigned int buff_size)
{
	int i, j;
	unsigned int buff_len = 0;

	if (mode->spec_check == SPEC_NO_CHECK) {
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"OK %d %d\n",
						ts->rx_count, ts->tx_count);
	} else if (mode->spec_check == SPEC_PASS) {
		buff_len += scnprintf(buff + buff_len,
				    	buff_size - buff_len,
						"OK %d %d\n",
						ts->rx_count, ts->tx_count);
	} else {
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"NG %d %d\n",
						ts->rx_count, ts->tx_count);
	}
	if (!ts->print_format) {
		for (i = 0; i < (ts->rx_count * ts->tx_count); i++) {
			buff_len += scnprintf(buff + buff_len,
							buff_size - buff_len,
							"%3d,", ts->pFrame[i]);
			if (i % ts->tx_count == (ts->tx_count - 1))
				buff_len += scnprintf(buff + buff_len,
						    	buff_size - buff_len, "\n");
		}
	} else {
		for (i = 0; i < ts->tx_count; i++) {
			for (j = 0; j < ts->rx_count; j++) {
				buff_len += scnprintf(buff + buff_len,
								buff_size - buff_len,
								"%3d,",
								ts->pFrame[(j * ts->tx_count) + i]);
			}
			buff_len += scnprintf(buff + buff_len,
					    	buff_size - buff_len, "\n");
		}
	}

	return buff_len;
}

static int sec_ts_save_self_raw_to_buffer(struct sec_ts_data *ts,
		struct sec_cmd_data *sec, struct sec_ts_test_mode *mode,
		char *buff, const unsigned int buff_size)
{
	int i;
	unsigned int buff_len = 0;

	if (mode->spec_check == SPEC_NO_CHECK)
		buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len, "\n");
	else if (mode->spec_check == SPEC_PASS) {
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"OK %d %d\n",
						ts->rx_count, ts->tx_count);
	} else {
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"NG %d %d\n",
						ts->rx_count, ts->tx_count);
	}
	buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len, "      ");
	if (!ts->print_format) {
		for (i = 0; i < (ts->rx_count + ts->tx_count); i++) {
			buff_len += scnprintf(buff + buff_len,
							buff_size - buff_len,
							"%3d,", ts->pFrameSS[i]);
			if (i >= (ts->rx_count - 1))
				buff_len += scnprintf(buff + buff_len,
								buff_size - buff_len, "\n");
		}
	} else {
		for (i = 0; i < ts->rx_count; i++) {
			buff_len += scnprintf(buff + buff_len,
							buff_size - buff_len,
							"%3d,", ts->pFrameSS[i]);
		}
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len, "\n");
		for (i = ts->rx_count; i < (ts->rx_count + ts->tx_count); i++) {
			buff_len += scnprintf(buff + buff_len,
							buff_size - buff_len,
							"%3d,\n", ts->pFrameSS[i]);
		}
	}

	return buff_len;
}

static int sec_ts_read_rawdata(struct sec_ts_data *ts,
		struct sec_cmd_data *sec, struct sec_ts_test_mode *mode)
{
	int ret = 0;
	const unsigned int buff_size = (ts->tx_count + 2) * ts->rx_count *
					CMD_RESULT_WORD_LEN;
	unsigned int buff_len = 0;
	char *buff;

	buff = kzalloc(buff_size, GFP_KERNEL);
	if (!buff)
		goto error_alloc_mem;

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("[ERROR] Touch is stopped\n");
		goto error_power_state;
	}

	LOGI("%d\n", mode->type);

	ret = sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_TOUCH,
			       TOUCH_MODE_STATE_TOUCH);
	if (ret < 0) {
		LOGE("failed to fix tmode\n");
		goto error_test_fail;
	}

	ret = sec_ts_read_frame(ts, mode->type, mode->min, mode->max);
	if (ret < 0) {
		LOGE("failed to read frame\n");
		goto error_test_fail;
	}

	if (mode->spec_check == SPEC_CHECK) {
		sec_ts_min_max_spec_check(ts, mode);
	}

	buff_len += sec_ts_save_mutual_raw_to_buffer(ts, sec,
		mode, buff + buff_len, buff_size - buff_len);

	if (mode->self_report)
		buff_len += sec_ts_save_self_raw_to_buffer(ts, sec,
			mode, buff + buff_len, buff_size - buff_len);

	ret = sec_ts_release_tmode(ts);
	if (ret < 0) {
		LOGE("failed to release tmode\n");
		goto error_test_fail;
	}

	if (!sec)
		goto out_rawdata;
	sec_cmd_set_cmd_result(sec, buff, buff_len);
	sec->cmd_state = SEC_CMD_STATUS_OK;

out_rawdata:
	kfree(buff);

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif

	return ret;

error_test_fail:
error_power_state:
	kfree(buff);
error_alloc_mem:
	if (!sec)
		return ret;

	sec_cmd_set_cmd_result(sec, "FAIL", 4);
	sec->cmd_state = SEC_CMD_STATUS_FAIL;

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif

	return ret;
}

static void get_fw_ver_bin(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[30] = { 0 };

	sec_cmd_set_default_result(sec);

	snprintf(buff, sizeof(buff), "SE-V%02X,FW-V%02X.%02X.%02X.%02X",
		ts->plat_data->panel_revision,
		ts->plat_data->img_version_of_bin[0],
		ts->plat_data->img_version_of_bin[1],
		ts->plat_data->img_version_of_bin[2],
		ts->plat_data->img_version_of_bin[3]);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void get_fw_ver_ic(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[30] = { 0 };
	int ret;
	u8 fw_ver[4];

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("[ERROR] Touch is stopped\n");
		snprintf(buff, sizeof(buff), "%s", "TSP turned off");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
		return;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_FW_VERSION, fw_ver, 4);
	if (ret < 0) {
		LOGE("firmware version read error\n");
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		return;
	}

	snprintf(buff, sizeof(buff), "SE-V%02X,FW-V%02X.%02X.%02X.%02X",
			ts->plat_data->panel_revision,
			fw_ver[0], fw_ver[1], fw_ver[2], fw_ver[3]);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void get_config_ver(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[22] = { 0 };

	sec_cmd_set_default_result(sec);

	snprintf(buff, sizeof(buff), "%s_SE_%02X%02X",
		ts->plat_data->model_name,
		ts->plat_data->config_version_of_ic[2],
		ts->plat_data->config_version_of_ic[3]);

	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void module_off_master(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[3] = { 0 };
	int ret = 0;

	ret = sec_ts_stop_device(ts);

	if (ret == 0)
		snprintf(buff, sizeof(buff), "%s", "OK");
	else
		snprintf(buff, sizeof(buff), "%s", "NG");

	sec_cmd_set_default_result(sec);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (strncmp(buff, "OK", 2) == 0)
		sec->cmd_state = SEC_CMD_STATUS_OK;
	else
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	LOGI("%s\n", buff);
}

static void module_on_master(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[3] = { 0 };
	int ret = 0;

	ret = sec_ts_start_device(ts);

	/* TODO: check this for SPI case
	*	if (ts->input_dev->disabled) {
	*		sec_ts_set_lowpowermode(ts, TO_LOWPOWER_MODE);
	*		ts->power_status = SEC_TS_STATE_LPM;
	*	}
	**/

	if (ret == 0)
		snprintf(buff, sizeof(buff), "%s", "OK");
	else
		snprintf(buff, sizeof(buff), "%s", "NG");

	sec_cmd_set_default_result(sec);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	if (strncmp(buff, "OK", 2) == 0)
		sec->cmd_state = SEC_CMD_STATUS_OK;
	else
		sec->cmd_state = SEC_CMD_STATUS_FAIL;

	LOGI("%s\n", buff);
}

static void get_chip_vendor(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	char buff[16] = { 0 };

	strncpy(buff, "SEC", sizeof(buff));
	sec_cmd_set_default_result(sec);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void get_chip_name(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[16] = { 0 };

	if (ts->plat_data->img_version_of_ic[0] == 0x02)
		strncpy(buff, "MC44", sizeof(buff));
	else if (ts->plat_data->img_version_of_ic[0] == 0x05)
		strncpy(buff, "A552", sizeof(buff));
	else if (ts->plat_data->img_version_of_ic[0] == 0x09)
		strncpy(buff, "Y661", sizeof(buff));
	else if (ts->plat_data->img_version_of_ic[0] == 0x10)
		strncpy(buff, "Y761", sizeof(buff));
	else if (ts->plat_data->img_version_of_ic[0] == 0x34)
		strncpy(buff, "VR40", sizeof(buff));
	else if (ts->plat_data->img_version_of_ic[0] == 0x38)
		strncpy(buff, "8DT0", sizeof(buff));
	else if (ts->plat_data->img_version_of_ic[0] == 0x42)
		strncpy(buff, "3FT0", sizeof(buff));
	else
		strncpy(buff, "N/A", sizeof(buff));

	sec_cmd_set_default_result(sec);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void get_chip_id(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[20] = { 0 };
	int ret;
	u8 chipID[5];

	sec_cmd_set_default_result(sec);

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_CHIP_ID, chipID, 5);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_READ_ID, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%02X %02X %02X %02X %02X",
		chipID[0], chipID[1], chipID[2],
		chipID[3], chipID[4]);

	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	LOGI("%s\n", buff);
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;
}

static void get_wet_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[16] = { 0 };
	char wet_mode_info = 0;
	int ret;


	sec_cmd_set_default_result(sec);

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_WET_MODE_STATUS, &wet_mode_info, 1);
	if (ret < 0) {
		LOGE("fail!, %d\n", ret);
		goto NG;
	}

	snprintf(buff, sizeof(buff), "%d", wet_mode_info);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
	return;

NG:
	snprintf(buff, sizeof(buff), "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	LOGI("%s\n", buff);
}

static void get_x_num(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);
	snprintf(buff, sizeof(buff), "%d", ts->tx_count);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void get_y_num(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);
	snprintf(buff, sizeof(buff), "%d", ts->rx_count);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void get_x_cross_routing(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);
	snprintf(buff, sizeof(buff), "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void get_y_cross_routing(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[16] = { 0 };
	int ret;

	sec_cmd_set_default_result(sec);

	ret = strncmp(ts->plat_data->model_name, "G935", 4)
			&& strncmp(ts->plat_data->model_name, "N930", 4);
	if (ret == 0)
		snprintf(buff, sizeof(buff), "13,14");
	else
		snprintf(buff, sizeof(buff), "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);
}

static void run_reference_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;

	sec_cmd_set_default_result(sec);

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_OFFSET_DATA_SEC;
	mode.self_report = 1;

	sec_ts_read_rawdata(ts, sec, &mode);
}

static void run_rawcap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] == 1)
		execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE | TEST_SHORT);
	else
		execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE);

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_OFFSET_DATA_SDC;
	mode.self_report = 1;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif

	sec_ts_read_rawdata(ts, sec, &mode);
}

static int sec_ts_save_self_gap_raw_to_buffer(struct sec_ts_data *ts,
		struct sec_ts_test_mode *mode, short *gap,
		char *buff, const unsigned int buff_size)
{
	int i;
	unsigned int buff_len = 0;

	if (mode->spec_check == SPEC_NO_CHECK) {
		buff_len = scnprintf(buff + buff_len,
						buff_size - buff_len, "\n");
	} else if (mode->spec_check == SPEC_PASS) {
		buff_len = scnprintf(buff + buff_len,
						buff_size - buff_len,
						"OK %d %d\n",
						ts->rx_count, ts->tx_count);
	} else {
		buff_len = scnprintf(buff + buff_len,
						buff_size - buff_len,
						"NG %d %d\n",
						ts->rx_count, ts->tx_count);
	}

	for (i = 0; i < (ts->rx_count - 1); i++) {
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
				    	"%6d,", gap[i]);
	}

	buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len, "\n");

	for (i = ts->rx_count; i < ts->rx_count + (ts->tx_count - 1); i++) {
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"%6d,\n", gap[i]);
	}
	return buff_len;
}

static int sec_ts_save_mutual_gap_raw_to_buffer(struct sec_ts_data *ts,
		struct sec_ts_test_mode *mode, short *gap_x, short *gap_y,
		char *buff, const unsigned int buff_size)
{
	int i;
	unsigned int buff_len = 0;
	short dTmp;

	if (mode->spec_check == SPEC_NO_CHECK) {
		buff_len = scnprintf(buff + buff_len,
						buff_size - buff_len,
						"OK %d %d\n",
						ts->rx_count, ts->tx_count);
	} else if (mode->spec_check == SPEC_PASS) {
		buff_len = scnprintf(buff + buff_len,
						buff_size - buff_len,
						"OK %d %d\n",
						ts->rx_count, ts->tx_count);
	} else {
		buff_len = scnprintf(buff + buff_len,
						buff_size - buff_len,
						"NG %d %d\n",
						ts->rx_count, ts->tx_count);
	}

	for (i = 0; i < (ts->tx_count * ts->rx_count); i++) {
		dTmp = (gap_x[i] > gap_y[i]) ? gap_x[i] : gap_y[i];
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"%3d,", dTmp);

		if (i % ts->tx_count == (ts->tx_count - 1))
			buff_len += scnprintf(buff + buff_len,
							buff_size - buff_len, "\n");
	}

	return buff_len;
}

/* run_rawcap_factory_read :
 * Combine Cm/Cs offset and gap test to merge
 */
static void run_rawcap_factory_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;

	int ret_cm_gap_x = 0, ret_cm_gap_y = 0;
	int ret_cs_gap = 0;
	int ret;

	unsigned int raw_buff_len = 0;
	unsigned int gap_buff_len = 0;

	short *gap, *gap_x, *gap_y = NULL;
	char *gap_buff = NULL;
	char *raw_buff = NULL;

	const unsigned int raw_buff_size = ts->tx_count * ts->rx_count *
		CMD_RESULT_WORD_LEN + (ts->tx_count + ts->rx_count) *
		CMD_RESULT_WORD_LEN;

	const unsigned int mutual_gap_buff_size =
		ts->tx_count * ts->rx_count * 2;
	const unsigned int mutual_buff_size =
		mutual_gap_buff_size * CMD_RESULT_WORD_LEN
		+ 4 * CMD_RESULT_WORD_LEN;

	const int self_gap_buff_size = (ts->tx_count + ts->rx_count) * 2;
	const int self_buff_size = self_gap_buff_size * CMD_RESULT_WORD_LEN
		+ 4 * CMD_RESULT_WORD_LEN;

	const int gap_buff_size = mutual_buff_size + self_buff_size;

	const unsigned int X_DIR = 0;
	const unsigned int Y_DIR = 1;

	const u8 self_report = sec->cmd_param[1];

	sec_cmd_set_default_result(sec);

	raw_buff = kzalloc(raw_buff_size, GFP_KERNEL);
	gap_x = kzalloc(mutual_gap_buff_size, GFP_KERNEL);
	gap_y = kzalloc(mutual_gap_buff_size, GFP_KERNEL);
	gap = kzalloc(self_gap_buff_size, GFP_KERNEL);
	gap_buff = kzalloc(gap_buff_size, GFP_KERNEL);

	if (!raw_buff || !gap_x || !gap_y || !gap || !gap_buff) {
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		sec_cmd_set_cmd_result(sec, "FAIL", 4);
		goto ErrorAlloc;
	}

	LOGI("memory alloc done! selftest start!\n");

	if (sec->cmd_param[0] == 1)
		execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE | TEST_SHORT);
	else if (sec->cmd_param[0] == 2)
		execute_selftest(ts, TEST_SHORT);
	else
		execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE);

	LOGI("selftest done! read frame start!\n");

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_OFFSET_DATA_SDC;
	mode.self_report = self_report;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif

	ret = sec_ts_read_frame(ts, mode.type, mode.min, mode.max);
	if (ret < 0) {
		sec_cmd_set_cmd_result(sec, "FAIL", 4);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		goto ErrorAlloc;
	}

	LOGI("read frame done! gap check start!\n");

	ret_cm_gap_x = sec_ts_cm_gap_spec_check(ts, ts->pFrameMS, gap_x, X_DIR);
	ret_cm_gap_y = sec_ts_cm_gap_spec_check(ts, ts->pFrameMS, gap_y, Y_DIR);
	if (self_report)
		ret_cs_gap = sec_ts_cs_gap_spec_check(ts, ts->pFrameSS, gap);

#ifdef USE_SPEC_CHECK
	if (mode.spec_check == SPEC_CHECK) {
		if (0 != (ret_cm_gap_x + ret_cm_gap_y + ret_cs_gap))
			mode.spec_check = SPEC_FAIL;
	}
#else
	mode.spec_check = SPEC_PASS;
#endif

	LOGI("gap check done! save result start!\n");

	raw_buff_len = sec_ts_save_mutual_raw_to_buffer(ts, sec, &mode,
			raw_buff + raw_buff_len, raw_buff_size - raw_buff_len);
	if (self_report)
		raw_buff_len += sec_ts_save_self_raw_to_buffer(ts, sec, &mode,
				raw_buff + raw_buff_len, raw_buff_size - raw_buff_len);

#ifdef USE_SPEC_CHECK
	raw_buff_len += scnprintf(raw_buff + raw_buff_len,
				raw_buff_size - raw_buff_len,
				"%3d,%3d", mode.min[0], mode.max[0]);
#else
	/* raw_buff_len += scnprintf(raw_buff + raw_buff_len,
				raw_buff_size - raw_buff_len,
				"OK"); */
#endif

	sec_cmd_set_cmd_result(sec, raw_buff, raw_buff_len);

	gap_buff_len = sec_ts_save_mutual_gap_raw_to_buffer(ts,	&mode,
					gap_x, gap_y,
					gap_buff + gap_buff_len,
					gap_buff_size - gap_buff_len);
	if (self_report)
		gap_buff_len += sec_ts_save_self_gap_raw_to_buffer(ts, &mode,
						gap,
						gap_buff + gap_buff_len,
						gap_buff_size - gap_buff_len);

	sec_cmd_set_cmd_result_2(sec, gap_buff, gap_buff_len);

	LOGI("save result done!\n");

ErrorAlloc:
	LOGI("free memory alloc!\n");

	kfree(raw_buff);
	kfree(gap);
	kfree(gap_x);
	kfree(gap_y);
	kfree(gap_buff);
}

static void get_node(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	short val = 0;
	int node = 0;

	sec_cmd_set_default_result(sec);
	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("[ERROR] Touch is stopped\n");
		snprintf(buff, sizeof(buff), "%s", "TSP turned off");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
		return;
	}

	node = sec_ts_check_index(ts);
	if (node < 0)
		return;

	val = ts->pFrame[node];
	snprintf(buff, sizeof(buff), "%d", val);
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;
	LOGI("%s\n", buff);

}

static void run_rawcap_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;
	int ret_cm_gap_x, ret_cm_gap_y;
	short *gap_x, *gap_y;
	char *buff;
	const unsigned int buff_size = ts->tx_count * ts->rx_count * 2
		* CMD_RESULT_WORD_LEN + 4 * CMD_RESULT_WORD_LEN;
	const unsigned int readbytes = ts->tx_count * ts->rx_count * 2;
	const unsigned int X_DIR = 0;
	const unsigned int Y_DIR = 1;

	sec_cmd_set_default_result(sec);

	gap_x = kzalloc(readbytes, GFP_KERNEL);
	gap_y = kzalloc(readbytes, GFP_KERNEL);
	buff = kzalloc(buff_size, GFP_KERNEL);

	if (!gap_x || !gap_y || !buff) {
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		sec_cmd_set_cmd_result(sec, "FAIL", 4);
		goto ErrorAlloc;
	}

	execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE);

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_OFFSET_DATA_SDC;
	mode.self_report = 1;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif

	sec_ts_read_frame(ts, mode.type, mode.min, mode.max);

	ret_cm_gap_x = sec_ts_cm_gap_spec_check(ts, ts->pFrameMS, gap_x, X_DIR);
	ret_cm_gap_y = sec_ts_cm_gap_spec_check(ts, ts->pFrameMS, gap_y, Y_DIR);

#ifdef USE_SPEC_CHECK
	if (0 == (ret_cm_gap_x + ret_cm_gap_y)) {
		mode.spec_check = SPEC_PASS;
		sec->cmd_state = SEC_CMD_STATUS_OK;
	} else {
		mode.spec_check = SPEC_FAIL;
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
#else
	mode.spec_check = SPEC_PASS;
#endif

	sec_ts_save_mutual_gap_raw_to_buffer(ts, &mode, gap_x, gap_y,
									buff, buff_size);

	sec_cmd_set_cmd_result(sec, buff, buff_size);

ErrorAlloc:
	kfree(buff);
	kfree(gap_y);
	kfree(gap_x);

}

static void run_rawcap_highfreq_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;

	sec_cmd_set_default_result(sec);

	execute_selftest(ts, TEST_OPEN | TEST_NOT_SAVE | TEST_HIGH_FREQ );

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_OFFSET_DATA_SDC;
	mode.self_report = 0;

	sec_ts_read_rawdata(ts, sec, &mode);

}

static void run_rawcap_highfreq_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;
	int ret_cm_gap_x, ret_cm_gap_y;
	unsigned int gap_buff_len = 0;
	short *gap_x, *gap_y;
	char *gap_buff = NULL;
	const unsigned int gap_buff_size = ts->tx_count * ts->rx_count * 2
		* CMD_RESULT_WORD_LEN + 4 * CMD_RESULT_WORD_LEN;
	const unsigned int readbytes = ts->tx_count * ts->rx_count * 2;
	const unsigned int X_DIR = 0;
	const unsigned int Y_DIR = 1;

	sec_cmd_set_default_result(sec);

	gap_x = kzalloc(readbytes, GFP_KERNEL);
	gap_y = kzalloc(readbytes, GFP_KERNEL);
	gap_buff = kzalloc(gap_buff_size, GFP_KERNEL);

	if (!gap_x || !gap_y || !gap_buff) {
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		sec_cmd_set_cmd_result(sec, "FAIL", 4);
		goto ErrorAlloc;
	}

	execute_selftest(ts, TEST_OPEN | TEST_NOT_SAVE | TEST_HIGH_FREQ );

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_OFFSET_DATA_SDC;
	mode.self_report = 0;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif

	sec_ts_read_rawdata(ts, sec, &mode);

	ret_cm_gap_x = sec_ts_cm_gap_spec_check(ts, ts->pFrameMS, gap_x, X_DIR);
	ret_cm_gap_y = sec_ts_cm_gap_spec_check(ts, ts->pFrameMS, gap_y, Y_DIR);

#ifdef USE_SPEC_CHECK
	if (0 == (ret_cm_gap_x + ret_cm_gap_y)) {
		mode.spec_check = SPEC_PASS;
		sec->cmd_state = SEC_CMD_STATUS_OK;
	} else {
		mode.spec_check = SPEC_FAIL;
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
#else
	mode.spec_check = SPEC_PASS;
#endif

	gap_buff_len = sec_ts_save_mutual_gap_raw_to_buffer(ts, &mode,
					gap_x, gap_y,
					gap_buff + gap_buff_len,
					gap_buff_size - gap_buff_len);

	sec_cmd_set_cmd_result_2(sec, gap_buff, gap_buff_len);

ErrorAlloc:
	kfree(gap_buff);
	kfree(gap_y);
	kfree(gap_x);

}

static void run_trx_short_test(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = {0};
	int rc;
	char para = TO_TOUCH_MODE;


	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("Touch is stopped!\n");
		snprintf(buff, sizeof(buff), "%s", "TSP turned off");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
		return;
	}

	rc = execute_selftest(ts, TEST_SHORT | TEST_OPEN | TEST_NODE_VARIANCE);
	if (rc > 0) {
		ts->sec_ts_write(ts, SEC_TS_CMD_POWER_MODE, &para, 1);

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_OK;

		LOGI("%s\n", buff);
		return;
	}

	ts->sec_ts_write(ts, SEC_TS_CMD_POWER_MODE, &para, 1);

	snprintf(buff, sizeof(buff), "%s", "NG");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_FAIL;

	LOGI("%s\n", buff);
}

static void run_delta_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;

	sec_cmd_set_default_result(sec);

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_SIGNAL_DATA;
	mode.self_report = 1;

	sec_ts_read_rawdata(ts, sec, &mode);
}

static int sec_ts_read_frame_stdev(struct sec_ts_data *ts,
	struct sec_cmd_data *sec, u8 type, short *min, short *max,
	enum spec_check_type *spec_check, bool get_average_only)
{
	int ret = -ENOMEM;
	unsigned char *pRead = NULL;
	short *pFrameAll = NULL;
	int *pFrameAvg = NULL;
	u64 *pFrameStd = NULL;
	u8 inval_type = TYPE_INVALID_DATA;
	int i = 0;
	int j = 0;
	int node_cnt = 0;
	int frame_size = 0;
	int frame_cnt = 0;
	int tmp = 0;

	const unsigned int buff_size = ts->tx_count * ts->rx_count *
					CMD_RESULT_WORD_LEN;
	unsigned int buff_len = 0;
	char *pBuff = NULL;

	LOGI("\n");

	node_cnt = ts->rx_count * ts->tx_count;
	frame_size = node_cnt * 2;
	frame_cnt = 100;

	if (ts->tx_count >= MAX_TX_NUM || ts->rx_count >= MAX_RX_NUM) {
		ret = -EINVAL;
		LOGE("invalid Tx# %d or Rx# %d!\n", ts->tx_count, ts->rx_count);
		goto ErrorAlloc;
	}

	pBuff = kzalloc(buff_size, GFP_KERNEL);
	if (!pBuff)
		goto ErrorAlloc;

	pRead = kzalloc(frame_size, GFP_KERNEL);
	if (!pRead)
		goto ErrorAlloc;

	/* memory whole frame data : 1frame bytes * total frame */
	pFrameAll = kzalloc(frame_size * frame_cnt, GFP_KERNEL);
	if (!pFrameAll)
		goto ErrorAlloc;

	/* float type : type size is double */
	pFrameAvg = kzalloc(frame_size * 2, GFP_KERNEL);
	if (!pFrameAvg)
		goto ErrorAlloc;

	/* 64-bit to prevent overflow */
	pFrameStd = kzalloc(frame_size * 4, GFP_KERNEL);
	if (!pFrameStd)
		goto ErrorAlloc;

	/* fix touch mode */
	ret = sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_TOUCH,
			       TOUCH_MODE_STATE_TOUCH);
	if (ret < 0) {
		LOGE("failed to fix tmode\n");
		goto ErrorAlloc;
	}

	/* set OPCODE and data type */
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_RAWDATA_TYPE, &type, 1);
	if (ret < 0) {
		LOGE("Failed to set rawdata type\n");
		goto ErrorDataType;
	}

	sec_ts_delay(50);

	for (i = 0; i < frame_cnt; i++) {
		/* read data */
		ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_REPORT, pRead, frame_size);
		if (ret < 0) {
			LOGE("read rawdata failed!\n");
			goto ErrorRelease;
		}

		memset(ts->pFrame, 0x00, frame_size);

		for (j = 0; j < frame_size; j += 2) {
			ts->pFrame[j / 2] = pRead[j] + (pRead[j + 1] << 8);
			pFrameAvg[j / 2] += ts->pFrame[j / 2];
		}

		memcpy(pFrameAll + (frame_size * i) / sizeof(short),
		       ts->pFrame, frame_size);
	}

	/* get total frame average of each node */
	/* in the case of getting only average, *1000 not needed */
	for (j = 0; j < node_cnt; j++) {
		if (!get_average_only)
			pFrameAvg[j] = pFrameAvg[j] * 1000;
		pFrameAvg[j] = pFrameAvg[j] / frame_cnt;
	}

	LOGI("FrameAvg x 1000\n");

	/* print frame average x 1000 of each node */
	for (i = 0; i < ts->rx_count; i++) {
		buff_len = scnprintf(pBuff, buff_size, "Rx%02d | ", i);

		for (j = 0; j < ts->tx_count; j++) {
			buff_len += scnprintf(pBuff + buff_len,
					buff_size - buff_len,
					" %6d",
					pFrameAvg[(j * ts->rx_count) + i]);
		}
		LOGI("%s\n", pBuff);
	}

	/* when only getting average, put average in
	 * ts->pFrame and goto set cmd_result
	 */
	if (get_average_only) {
		for (i = 0; i < ts->tx_count; i++) {
			for (j = 0; j < ts->rx_count; j++) {
				ts->pFrame[(j * ts->tx_count) + i] =
					(short)(pFrameAvg[(i * ts->rx_count) + j]);
			}
		}
		goto OnlyAverage;
	}

	/* get standard deviation */
	for (i = 0; i < frame_cnt; i++) {
		for (j = 0; j < node_cnt; j++) {
			tmp = pFrameAll[node_cnt * i + j] * 1000;
			pFrameStd[j] = pFrameStd[j] +
			    (tmp - pFrameAvg[j]) * (tmp - pFrameAvg[j]);
		}
	}

	for (j = 0; j < node_cnt; j++)
		pFrameStd[j] = int_sqrt(pFrameStd[j] / frame_cnt);

	/* print standard deviation x 1000 of each node */
	LOGI("FrameStd x 1000\n");

	*min = *max = pFrameStd[0];

	for (i = 0; i < ts->rx_count; i++) {
		buff_len = scnprintf(pBuff, buff_size, "Rx%02d | ", i);

		for (j = 0; j < ts->tx_count; j++) {
			if (i > 0) {
				if (pFrameStd[(j * ts->rx_count) + i] < *min)
					*min = pFrameStd[(j * ts->rx_count) + i];

				if (pFrameStd[(j * ts->rx_count) + i] > *max)
					*max = pFrameStd[(j * ts->rx_count) + i];
			}
			buff_len += scnprintf(pBuff + buff_len,
					buff_size - buff_len,
					" %6d",
					(int)pFrameStd[(j * ts->rx_count) + i]);
		}
		LOGI("%s\n", pBuff);
	}

	/* Rotate 90 degrees for readability */
	for (i = 0; i < ts->tx_count; i++) {
		for (j = 0; j < ts->rx_count; j++) {
			if (pFrameStd[(i * ts->rx_count) + j] > 32767)
				/* Reduce to short data type and allow high
				 * values to saturate.
				 */
				ts->pFrame[(j * ts->tx_count) + i] = 32767;
			else {
				ts->pFrame[(j * ts->tx_count) + i] =
					(short)(pFrameStd[(i * ts->rx_count) + j]);
			}
		}
	}

#ifdef USE_SPEC_CHECK
	if (*spec_check == SPEC_CHECK) {
		int specover_count = 0;

		for (i = 0; i < ts->tx_count; i++) {
			for (j = 0; j < ts->rx_count; j++) {
				if (ts->pFrame[(j * ts->tx_count) + i] >
				    cm_stdev_max)
					specover_count++;
			}
		}

		if (specover_count == 0)
			*spec_check = SPEC_PASS;
		else
			*spec_check = SPEC_FAIL;
	}
#else
	*spec_check = SPEC_PASS;
#endif

	if (*spec_check == SPEC_PASS)
		buff_len = scnprintf(pBuff, buff_size, "OK %d %d\n",
						ts->rx_count, ts->tx_count);
	else if (*spec_check == SPEC_FAIL)
		buff_len = scnprintf(pBuff, buff_size, "NG %d %d\n",
						ts->rx_count, ts->tx_count);
	else
		buff_len = scnprintf(pBuff, buff_size, "\n");

	for (i = 0; i < node_cnt; i++) {
		buff_len += scnprintf(pBuff + buff_len, buff_size - buff_len,
						"%4d,", ts->pFrame[i]);

		if (i % ts->tx_count == ts->tx_count - 1)
			buff_len += scnprintf(pBuff + buff_len,
							buff_size - buff_len, "\n");
	}

	if (!sec)
		goto ErrorRelease;

	sec_cmd_set_cmd_result(sec, pBuff, buff_len);
	sec->cmd_state = SEC_CMD_STATUS_OK;

ErrorRelease:
OnlyAverage:
	/* release data monitory (unprepare AFE data memory) */
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_RAWDATA_TYPE, &inval_type,
				   1);
	if (ret < 0) {
		LOGE("Failed to set rawdata type\n");
		goto ErrorAlloc;
	}

ErrorDataType:
	/* release mode fix */
	ret = sec_ts_release_tmode(ts);
	if (ret < 0)
		LOGE("failed to release tmode\n");

ErrorAlloc:
	kfree(pFrameStd);
	kfree(pFrameAvg);
	kfree(pFrameAll);
	kfree(pRead);
	kfree(pBuff);

	return ret;
}

static void run_rawdata_stdev_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;

	sec_cmd_set_default_result(sec);

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_REMV_AMB_DATA;
	mode.self_report = 0;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif

	sec_ts_read_frame_stdev(ts, sec, mode.type, mode.min, mode.max,
				&mode.spec_check, false);
}

static int sec_ts_read_frame_p2p(struct sec_ts_data *ts,
		struct sec_cmd_data *sec, struct sec_ts_test_mode mode)
{
	int ret = -1;
	const unsigned int frame_size = ts->tx_count * ts->rx_count * 2;
	short *temp = NULL;
	int i;
	char para = TO_TOUCH_MODE;
	const unsigned int buff_size = ts->tx_count * ts->rx_count * 
		CMD_RESULT_WORD_LEN + 4 * CMD_RESULT_WORD_LEN;
	unsigned int buff_len = 0;
	char *buff;
	u8 result = 0x0;

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("[ERROR] Touch is stopped\n");
		goto ErrorPowerState;
	}

	buff = kzalloc(buff_size, GFP_KERNEL);
	if (!buff)
		goto ErrorAllocbuff;

	temp = kzalloc(frame_size, GFP_KERNEL);
	if (!temp)
		goto ErrorAlloctemp;

	ret = execute_p2ptest(ts);
	if (ret < 0) {
		LOGE("P2P test failed\n");
		goto ErrorP2PTest;
	}

	/* get min data */
	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_NOI_P2P_MIN;
	mode.self_report = 0;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif
	sec_ts_read_frame(ts, mode.type, mode.min, mode.max);
	if (mode.spec_check == SPEC_FAIL)
		result |= 0x1;

	memcpy(temp, ts->pFrame, frame_size);
	memset(ts->pFrame, 0x00, frame_size);

	/* get max data */
	mode.type = TYPE_NOI_P2P_MAX;
	mode.self_report = 0;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif
	sec_ts_read_frame(ts, mode.type, mode.min, mode.max);
	if (mode.spec_check == SPEC_FAIL)
		result |= 0x2;

	for (i = 0; i < (ts->rx_count * ts->tx_count); i++) {
		/* get p2p by subtract min from max data */
		ts->pFrame[i] = ts->pFrame[i] - temp[i];
#ifdef USE_SPEC_CHECK
		if (ts->pFrame[i] > noi_mm)
			result |= 0x4;
#endif
	}

	if (result != 0x0) {
		buff_len += scnprintf(buff + buff_len, buff_size - buff_len,
					"NG %d %d\n", ts->rx_count, ts->tx_count);
	} else {
		buff_len += scnprintf(buff + buff_len, buff_size - buff_len,
					"OK %d %d\n", ts->rx_count, ts->tx_count);
	}

	for (i = 0; i < (ts->rx_count * ts->tx_count); i++) {
		buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"%3d,", ts->pFrame[i]);
		if (i % ts->tx_count == (ts->tx_count - 1))
			buff_len += scnprintf(buff + buff_len,
							buff_size - buff_len, "\n");
	}

	if (sec) {
		sec_cmd_set_cmd_result(sec, buff, buff_len);
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

ErrorP2PTest:
	para = TO_TOUCH_MODE;
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_POWER_MODE, &para, 1);
	if (ret < 0)
		LOGE("Set powermode failed\n");
ErrorAlloctemp:
	kfree(temp);
ErrorAllocbuff:
	kfree(buff);
ErrorPowerState:
	if (sec && ret < 0) {
		sec_cmd_set_cmd_result(sec, "NG", 3);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}

	return ret;
}

static void run_rawdata_p2p_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;

	sec_cmd_set_default_result(sec);

	/* mode will be set during p2p read */
	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));

	sec_ts_read_frame_p2p(ts, sec, mode);
}

int sec_ts_run_rawdata_type(struct sec_ts_data *ts,
		struct sec_cmd_data *sec, u8 data_type)
{
	int ret = -1;
	short min[REGION_TYPE_COUNT], max[REGION_TYPE_COUNT];
	int i, j;
	u8 read_type[9] = {TYPE_RAW_DATA, TYPE_AMBIENT_DATA,
		TYPE_DECODED_DATA, TYPE_REMV_AMB_DATA,
		TYPE_SIGNAL_DATA, TYPE_OFFSET_DATA_SEC, TYPE_OFFSET_DATA_SDC,
		TYPE_NOI_P2P_MIN, TYPE_NOI_P2P_MAX};
	const unsigned int buff_size = (ts->tx_count + 2) * ts->rx_count *
		CMD_RESULT_WORD_LEN + 4 * CMD_RESULT_WORD_LEN;
	unsigned int buff_len = 0;
	char *buff;


	for (i = 0; i < 9; i++) {
		if (read_type[i] == data_type)
			break;
	}

	if (i == 9) {
		LOGE("invalid data type\n");
		return ret;
	}

	buff = kzalloc(buff_size, GFP_KERNEL);
	if (!buff)
		goto error_alloc_mem;

	ts->tsp_dump_lock = 1;
	LOGI("start (wet: %d)##\n", ts->wet_mode);

	if (data_type == TYPE_OFFSET_DATA_SDC) {
		ret = execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE | TEST_NOT_SAVE);
		if (ret < 0) {
			LOGE("SelfTest failed\n");
		}

		ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
		if (ret < 0) {
			LOGE("Set powermode failed\n");
			goto out;
		}
	} else if (data_type == TYPE_NOI_P2P_MIN || data_type == TYPE_NOI_P2P_MAX) {
		ret = execute_p2ptest(ts);
		if (ret < 0) {
			LOGE("P2P test failed\n");
		}

		ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
		if (ret < 0) {
			LOGE("Set powermode failed\n");
			goto out;
		}
	} else {
		ret = sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_TOUCH,
				TOUCH_MODE_STATE_TOUCH);
		if (ret < 0) {
			LOGE("Failed to fix tmode\n");
			goto out;
		}
	}

	ret = sec_ts_read_frame(ts, data_type, min, max);
	if (ret < 0)
		LOGI("Failed to read data_type %d : ## ret: %d\n", data_type, ret);
	else
#ifdef USE_SPEC_CHECK
		LOGI("data_type %d : Max/Min %d,%d ##\n", data_type, max[0], min[0]);
#else
		LOGI("data_type %d ##\n", data_type);
#endif

	if (ret >= 0) {
		buff_len += scnprintf(buff + buff_len,
				buff_size - buff_len,
				"OK %d %d\n",
				ts->rx_count, ts->tx_count);
	} else {
		buff_len += scnprintf(buff + buff_len,
				buff_size - buff_len,
				"NG %d %d\n",
				ts->rx_count, ts->tx_count);
	}

	/* mutual */
	if (!ts->print_format) {
		for (i = 0; i < (ts->rx_count * ts->tx_count); i++) {
			buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len,
					"%3d,", ts->pFrameMS[i]);
			if (i % ts->tx_count == (ts->tx_count - 1))
				buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len, "\n");
		}
	} else {
		for (i = 0; i < ts->tx_count; i++) {
			for (j = 0; j < ts->rx_count; j++) {
				buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len,
						"%3d,",
						ts->pFrameMS[(j * ts->tx_count) + i]);
			}
			buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len, "\n");
		}
	}

	buff_len += scnprintf(buff + buff_len,
			buff_size - buff_len, "\n      ");

	/* self */
	if (!ts->print_format) {
		for (i = 0; i < (ts->rx_count + ts->tx_count); i++) {
			buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len,
					"%3d,", ts->pFrameSS[i]);
			if (i >= ts->rx_count - 1)
				buff_len += scnprintf(buff + buff_len,
						buff_size - buff_len, "\n");
		}
	} else {
		for (i = 0; i < ts->rx_count; i++) {
			buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len,
					"%3d,", ts->pFrameSS[i]);
		}
		buff_len += scnprintf(buff + buff_len,
				buff_size - buff_len, "\n");
		for (i = ts->rx_count; i < (ts->rx_count + ts->tx_count); i++) {
			buff_len += scnprintf(buff + buff_len,
					buff_size - buff_len,
					"%3d,\n", ts->pFrameSS[i]);
		}
	}

	sec_ts_release_tmode(ts);

out:
	LOGI("ito: %02X %02X %02X %02X\n", ts->ito_test[0], ts->ito_test[1]
			, ts->ito_test[2], ts->ito_test[3]);

	LOGI("done (wet: %d)##\n", ts->wet_mode);
	ts->tsp_dump_lock = 0;

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif

	sec_cmd_set_cmd_result(sec, buff, buff_len);
	kfree(buff);

	return ret;

error_alloc_mem:
	sec_cmd_set_cmd_result(sec, "FAIL", 4);

	return ret;
}

static void run_rawdata_read_type(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[16] = { 0 };
	int ret = -1;

	sec_cmd_set_default_result(sec);

	if (ts->tsp_dump_lock == 1) {
		LOGE("already checking now\n");
		scnprintf(buff, sizeof(buff), "NG %d %d", ts->rx_count, ts->tx_count);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		goto out;
	}
	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("IC is power off\n");
		scnprintf(buff, sizeof(buff), "NG %d %d", ts->rx_count, ts->tx_count);
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		goto out;
	}

	ret = sec_ts_run_rawdata_type(ts, sec, (u8)sec->cmd_param[0]);
	if (ret < 0) {
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}
out:
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	LOGI("%s\n", buff);
}

/*
 * sec_ts_run_rawdata_all : read all raw data
 * "mutual/self 3, 5, 29, 1, 19" data will be saved in log
 */
void sec_ts_run_rawdata_all(struct sec_ts_data *ts)
{
	int ret = -1;
	int i = 0;
	short min[REGION_TYPE_COUNT], max[REGION_TYPE_COUNT];
	int test_num;
	u8 data_type[5] = {TYPE_AMBIENT_DATA, TYPE_DECODED_DATA,
		TYPE_SIGNAL_DATA, TYPE_OFFSET_DATA_SEC, TYPE_OFFSET_DATA_SDC};

	ts->tsp_dump_lock = 1;
	LOGI("start (wet: %d)##\n", ts->wet_mode);

	ret = sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_TOUCH,
			       TOUCH_MODE_STATE_TOUCH);
	if (ret < 0) {
		LOGE("Failed to fix tmode\n");
		goto out;
	}

	test_num = 5;

	for (i = 0; i < test_num; i++) {
		if (data_type[i] == TYPE_OFFSET_DATA_SDC)
			execute_selftest(ts, TEST_OPEN | TEST_NODE_VARIANCE | TEST_SELF_NODE);

		ret = sec_ts_read_frame(ts, data_type[i], min, max);

		if (ret < 0)
			LOGI("mutual %d : error ## ret: %d\n", data_type[i], ret);
		else
#ifdef USE_SPEC_CHECK
			LOGI("mutual %d : Max/Min %d,%d ##\n", data_type[i], max[0], min[0]);
#else
			LOGI("mutual %d ##\n", data_type[i]);
#endif
	}

	ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
	if (ret < 0) {
		LOGE("failed to set touch mode.\n");
		goto out;
	}

	sec_ts_release_tmode(ts);

out:
	LOGI("ito: %02X %02X %02X %02X\n", ts->ito_test[0], ts->ito_test[1]
			, ts->ito_test[2], ts->ito_test[3]);

	LOGI("done (wet: %d)##\n", ts->wet_mode);
	ts->tsp_dump_lock = 0;

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif
}

static void run_rawdata_read_all(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[16] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->tsp_dump_lock == 1) {
		LOGE("already checking now\n");
		snprintf(buff, sizeof(buff), "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		goto out;
	}
	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("IC is power off\n");
		snprintf(buff, sizeof(buff), "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		goto out;
	}

	sec_ts_run_rawdata_all(ts);

	snprintf(buff, sizeof(buff), "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
out:
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	LOGI("%s\n", buff);
}

static void run_self_rawcap_gap_read(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	struct sec_ts_test_mode mode;
	int ret = 0;
	char *buff = NULL;
	short *gap = NULL;
	const int gap_buff_size = (ts->tx_count - 1) + (ts->rx_count - 1);
	const int buff_size = gap_buff_size * CMD_RESULT_WORD_LEN + 4;

	sec_cmd_set_default_result(sec);

	gap = kzalloc(gap_buff_size, GFP_KERNEL);
	buff = kzalloc(buff_size, GFP_KERNEL);
	if (!gap || !buff) {
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		sec_cmd_set_cmd_result(sec, "FAIL", 4);
		goto ErrorAlloc;
	}

	execute_selftest(ts, TEST_SELF_NODE);

	memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
	mode.type = TYPE_OFFSET_DATA_SDC;
	mode.self_report = 1;
#ifdef USE_SPEC_CHECK
	mode.spec_check = SPEC_CHECK;
#endif

	sec_ts_read_frame(ts, mode.type, mode.min, mode.max);

	/* ret is number of spec over channel */
	ret = sec_ts_cs_gap_spec_check(ts, ts->pFrameSS, gap);

#ifdef USE_SPEC_CHECK
	if (ret == 0) {
		mode.spec_check = SPEC_PASS;
		sec->cmd_state = SEC_CMD_STATUS_OK;
	} else {
		mode.spec_check = SPEC_FAIL;
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	}
#else
	mode.spec_check = SPEC_PASS;
#endif

	sec_ts_save_self_gap_raw_to_buffer(ts, &mode, gap,
		buff, buff_size);

	sec_cmd_set_cmd_result(sec, buff, buff_size);

ErrorAlloc:
	kfree(buff);
	kfree(gap);
}

#define GLOVE_MODE_EN		(1 << 0)
#define CLEAR_COVER_EN		(1 << 1)
#define FAST_GLOVE_MODE_EN	(1 << 2)

static void glove_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int glove_mode_enables = 0;

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
		snprintf(buff, sizeof(buff), "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		int retval;

		if (sec->cmd_param[0])
			glove_mode_enables |= GLOVE_MODE_EN;
		else
			glove_mode_enables &= ~(GLOVE_MODE_EN);

		retval = sec_ts_glove_mode_enables(ts, glove_mode_enables);

		if (retval < 0) {
			LOGE("failed, retval = %d\n", retval);
			snprintf(buff, sizeof(buff), "NG");
			sec->cmd_state = SEC_CMD_STATUS_FAIL;
		} else {
			snprintf(buff, sizeof(buff), "OK");
			sec->cmd_state = SEC_CMD_STATUS_OK;
		}
	}

	sec_cmd_set_cmd_result(sec, buff, strlen(buff));
	sec->cmd_state = SEC_CMD_STATUS_WAITING;
	sec_cmd_set_cmd_exit(sec);
}

static void clear_cover_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };

	LOGI("\n");

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 3) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		if (sec->cmd_param[0] > 1) {
			ts->flip_enable = true;
			ts->cover_type = sec->cmd_param[1];
			ts->cover_cmd = (u8)ts->cover_type;
		} else {
			ts->flip_enable = false;
		}

		if (!(ts->power_status == SEC_TS_STATE_POWER_OFF) &&
		    ts->reinit_done) {
			if (ts->flip_enable)
				sec_ts_set_cover_type(ts, true);
			else
				sec_ts_set_cover_type(ts, false);
		}

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_WAITING;
	sec_cmd_set_cmd_exit(sec);

	LOGI("%s\n", buff);
};

static void dead_zone_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret;
	char data = 0;

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		data = sec->cmd_param[0];

		ret = ts->sec_ts_write(ts, SEC_TS_CMD_EDGE_DEADZONE, &data, 1);
		if (ret < 0) {
			LOGE("Failed to set deadzone\n");
			snprintf(buff, sizeof(buff), "%s", "NG");
			sec->cmd_state = SEC_CMD_STATUS_FAIL;
			goto err_set_dead_zone;
		}

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

err_set_dead_zone:
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);

	LOGI("%s\n", buff);
};

static void sec_ts_swap(u8 *a, u8 *b)
{
	u8 temp = *a;

	*a = *b;
	*b = temp;
}

static void rearrange_selftest_result(u8 *data, int length)
{
	int i;

	for (i = 0; i < length; i += 4) {
		sec_ts_swap(&data[i], &data[i + 3]);
		sec_ts_swap(&data[i + 1], &data[i + 2]);
	}
}

int execute_p2ptest(struct sec_ts_data *ts)
{
	int rc;
	u8 tpara[5] = {0x07, 0x00, 0x00, 0x64, 0x00};

	LOGI("P2P test start!\n");

	ts->sec_irq_enable(ts, false);
	rc = ts->sec_ts_write(ts, SEC_TS_CMD_P2P_TEST, tpara, 5);
	if (rc < 0) {
		LOGE("Failed to send P2Ptest command!\n");
		goto err_exit;
	}

	rc = sec_ts_wait_for_ready(ts, SEC_TS_VENDOR_ACK_P2P_TEST_DONE,
			SEC_TS_VENDOR_P2P_TEST_TIME_MS);
	if (rc < 0) {
		LOGE("P2Ptest execution time out!\n");
		goto err_exit;
	}

	LOGI("P2P test done!\n");

err_exit:
	ts->sec_irq_enable(ts, true);

	return rc;
}

/* execute_selftest options
 * bit[7] : Do NOT save
 * bit[6] : Load self-test configuration only
 * bit[5] : Get Self capacitance
 * bit[4] : Reserved
 * bit[3] : Reserved
 * bit[2] : Enable/disable the short test
 * bit[1] : Enable/disable the node variance test
 * bit[0] : Enable/disable the open test
 */
int execute_selftest(struct sec_ts_data *ts, u32 option)
{
	int rc;
	/* Selftest setting
	 * Get self capacitance
	 * Enable/disable the short test
	 * Enable/disable the node variance test
	 * Enable/disable the open test
	 */
	u8 tpara[2] = {(u8)(option & 0xff), (u8)((option & 0xff00) >> 8)};
	u8 *rBuff;
	int i;
	int irq_is_enabled = atomic_read(&ts->irq_enabled);
	int result_size = SEC_TS_SELFTEST_REPORT_SIZE +
				2 * ((ts->tx_count * ts->rx_count) + 5 * (ts->tx_count + ts->rx_count) + 3);

	rBuff = kzalloc(result_size, GFP_KERNEL);
	if (!rBuff)
		return -ENOMEM;

	if (irq_is_enabled)
		ts->sec_irq_enable(ts, false);

	LOGI("Self test start! result_size=%d\n", result_size);
	rc = ts->sec_ts_write(ts, SEC_TS_CMD_SELF_TEST, tpara, 2);
	if (rc < 0) {
		LOGE("Failed to send selftest command!\n");
		goto err_exit;
	}

	rc = sec_ts_wait_for_ready(ts, SEC_TS_VENDOR_ACK_SELF_TEST_DONE,
			SEC_TS_VENDOR_SELF_TEST_TIME_MS);
	if (rc < 0) {
		LOGE("Selftest execution time out!\n");
		goto err_exit;
	}

	LOGI("Self test done!\n");

	rc = ts->sec_ts_read(ts, SEC_TS_CMD_SELF_TEST, rBuff, result_size);
	if (rc < 0) {
		LOGE("Failed to read Selftest result!\n");
		goto err_exit;
	}
	rearrange_selftest_result(rBuff, result_size);

	for (i = 0; i < SEC_TS_SELFTEST_REPORT_SIZE; i += 4) {
		if (i % 8 == 0) pr_cont("\n");
		if (i % 4 == 0) pr_cont("[sec_input] sec_ts : ");

		if (i / 4 == 0) pr_cont("SIG");
		else if (i / 4 == 1) pr_cont("VER");
		else if (i / 4 == 2) pr_cont("SIZ");
		else if (i / 4 == 3) pr_cont("CRC");
		else if (i / 4 == 4) pr_cont("RES");
		else if (i / 4 == 5) pr_cont("COU");
		else if (i / 4 == 6) pr_cont("PAS");
		else if (i / 4 == 7) pr_cont("FAI");
		else if (i / 4 == 8) pr_cont("CHA");
		else if (i / 4 == 9) pr_cont("AMB");
		else if (i / 4 == 10) pr_cont("TXS");
		else if (i / 4 == 11) pr_cont("TXS");
		else if (i / 4 == 12) pr_cont("RXS");
		else if (i / 4 == 13) pr_cont("RXS");
		else if (i / 4 == 14) pr_cont("TXO");
		else if (i / 4 == 15) pr_cont("TXO");
		else if (i / 4 == 16) pr_cont("RXO");
		else if (i / 4 == 17) pr_cont("RXO");
		else if (i / 4 == 18) pr_cont("TXG");
		else if (i / 4 == 19) pr_cont("TXG");
		else if (i / 4 == 20) pr_cont("RXG");
		else if (i / 4 == 21) pr_cont("RXG");
		else if (i / 4 == 22) pr_cont("TXT");
		else if (i / 4 == 23) pr_cont("TXT");
		else if (i / 4 == 24) pr_cont("RXR");
		else if (i / 4 == 25) pr_cont("RXR");
		else if (i / 4 == 26) pr_cont("TXR");
		else if (i / 4 == 27) pr_cont("TXR");
		else if (i / 4 == 28) pr_cont("RXT");
		else if (i / 4 == 29) pr_cont("RXT");

		pr_cont(" %2X, %2X, %2X, %2X  ", rBuff[i], rBuff[i + 1], rBuff[i + 2], rBuff[i + 3]);

		if (i / 4 == 4) {
			/* TX, RX open check. */
			if ((rBuff[i + 3] & 0x30) != 0)
				rc = 0;
			/* TX, RX - GND(VDD) short check. */
			else if ((rBuff[i + 3] & 0xC0) != 0)
				rc = 0;
			/* RX-RX, TX-TX short check. */
			else if ((rBuff[i + 2] & 0x03) != 0)
				rc = 0;
			/* TX-RX short check. */
			else if ((rBuff[i + 2] & 0x04) != 0)
				rc = 0;
			else
				rc = 1;

			ts->ito_test[0] = rBuff[i];
			ts->ito_test[1] = rBuff[i + 1];
			ts->ito_test[2] = rBuff[i + 2];
			ts->ito_test[3] = rBuff[i + 3];
		}

	}

err_exit:
	if (irq_is_enabled)
		ts->sec_irq_enable(ts, true);

	kfree(rBuff);

	return rc;
}

int sec_ts_execute_force_calibration(struct sec_ts_data *ts)
{
	int ret = -1;

	LOGI("\n");

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_PANEL_CAL, NULL, 0);
	if (ret < 0) {
		LOGE("Failed to write Cal command!\n");
		return ret;
	}

	ret = sec_ts_wait_for_ready(ts, SEC_TS_VENDOR_ACK_OFFSET_CAL_DONE,
			SEC_TS_VENDOR_ACK_OFFSET_CAL_MS);

	return ret;
}

static void run_force_calibration(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = {0};
	int rc;
	struct sec_ts_test_mode mode;

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("Touch is stopped!\n");
		snprintf(buff, sizeof(buff), "%s", "TSP turned off");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;

		return;
	}

	if (ts->touch_count > 0) {
		snprintf(buff, sizeof(buff), "%s", "NG_FINGER_ON");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		goto out_force_cal;
	}

	ts->sec_irq_enable(ts, false);
	rc = sec_ts_execute_force_calibration(ts);
	if (rc < 0) {
		snprintf(buff, sizeof(buff), "%s", "FAIL");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		memset(&mode, 0x00, sizeof(struct sec_ts_test_mode));
		mode.type = TYPE_AMBIENT_DATA;
		mode.self_report = 1;

		sec_ts_read_rawdata(ts, NULL, &mode);

		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

	ts->sec_irq_enable(ts, true);

out_force_cal:
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	LOGI("%s\n", buff);
}

static void enable_coordinate_report(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	int ret = 0;

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("Touch is stopped!\n");
		sec_cmd_set_cmd_result(sec, "TSP turned off", 14);
		sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
		return;
	}

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		sec_cmd_set_cmd_result(sec, "NG", 2);
		return;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			(u8 *)&(ts->touch_functions), 2);
	if (ret < 0) {
		LOGE("Failed to read touch functions(%d)\n", ret);
		return;
	}

	if (sec->cmd_param[0] == 0) {
		ts->touch_functions &= ~SEC_TS_BIT_SETFUNC_TOUCH_ENGINE_ON;
	} else {
		ts->touch_functions |= SEC_TS_BIT_SETFUNC_TOUCH_ENGINE_ON;
	}

	LOGI("coordinate report %s\n", ((sec->cmd_param[0] == 0) ? "disable" : "enable"));

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			(u8 *)&ts->touch_functions, 2);
	if (ret < 0) {
		LOGE("Failed to write cmd\n");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
		sec_cmd_set_cmd_result(sec, "NG", 2);
	} else {
		sec->cmd_state = SEC_CMD_STATUS_OK;
		sec_cmd_set_cmd_result(sec, "OK", 2);
	}
}

static void set_lowpower_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) {
		snprintf(buff, sizeof(buff), "%s", "NG");
		sec->cmd_state = SEC_CMD_STATUS_FAIL;
	} else {
		snprintf(buff, sizeof(buff), "%s", "OK");
		sec->cmd_state = SEC_CMD_STATUS_OK;
	}

/* set lowpower mode by spay, edge_swipe function.
 *	ts->lowpower_mode = sec->cmd_param[0];
 **/
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	sec_cmd_set_cmd_exit(sec);
}

/*
 *	flag     1  :  set edge handler
 *		2  :  set (portrait, normal) edge zone data
 *		4  :  set (portrait, normal) dead zone data
 *		8  :  set landscape mode data
 *		16 :  mode clear
 *	data
 *		0x30, FFF (y start), FFF (y end),  FF(direction)
 *		0x31, FFFF (edge zone)
 *		0x32, FF (up x), FF (down x), FFFF (y)
 *		0x33, FF (mode), FFF (edge), FFF (dead zone)
 *	case
 *		edge handler set :  0x30....
 *		booting time :  0x30...  + 0x31...
 *		normal mode : 0x32...  (+0x31...)
 *		landscape mode : 0x33...
 *		landscape -> normal (if same with old data) : 0x33, 0
 *		landscape -> normal (etc) : 0x32....  + 0x33, 0
 */

void set_grip_data_to_ic(struct sec_ts_data *ts, u8 flag)
{
	u8 data[8] = { 0 };

	LOGI("flag: %02X (clr,lan,nor,edg,han)\n", flag);

	if (flag & G_SET_EDGE_HANDLER) {
		if (ts->grip_edgehandler_direction == 0) {
			data[0] = 0x0;
			data[1] = 0x0;
			data[2] = 0x0;
			data[3] = 0x0;
		} else {
			data[0] = (ts->grip_edgehandler_start_y >> 4) & 0xFF;
			data[1] = (ts->grip_edgehandler_start_y << 4 & 0xF0) |
				((ts->grip_edgehandler_end_y >> 8) & 0xF);
			data[2] = ts->grip_edgehandler_end_y & 0xFF;
			data[3] = ts->grip_edgehandler_direction & 0x3;
		}
		ts->sec_ts_write(ts, SEC_TS_CMD_EDGE_HANDLER, data, 4);
		LOGI("0x%02X %02X,%02X,%02X,%02X\n", SEC_TS_CMD_EDGE_HANDLER,
			data[0], data[1], data[2], data[3]);
	}

	if (flag & G_SET_EDGE_ZONE) {
		data[0] = (ts->grip_edge_range >> 8) & 0xFF;
		data[1] = ts->grip_edge_range  & 0xFF;
		ts->sec_ts_write(ts, SEC_TS_CMD_EDGE_AREA, data, 2);
		LOGI("0x%02X %02X,%02X\n", SEC_TS_CMD_EDGE_AREA, data[0], data[1]);
	}

	if (flag & G_SET_NORMAL_MODE) {
		data[0] = ts->grip_deadzone_up_x & 0xFF;
		data[1] = ts->grip_deadzone_dn_x & 0xFF;
		data[2] = (ts->grip_deadzone_y >> 8) & 0xFF;
		data[3] = ts->grip_deadzone_y & 0xFF;
		ts->sec_ts_write(ts, SEC_TS_CMD_DEAD_ZONE, data, 4);
		LOGI("0x%02X %02X,%02X,%02X,%02X\n", SEC_TS_CMD_DEAD_ZONE,
			data[0], data[1], data[2], data[3]);
	}

	if (flag & G_SET_LANDSCAPE_MODE) {
		data[0] = ts->grip_landscape_mode & 0x1;
		data[1] = (ts->grip_landscape_edge >> 4) & 0xFF;
		data[2] = (ts->grip_landscape_edge << 4 & 0xF0) |
				((ts->grip_landscape_deadzone >> 8) & 0xF);
		data[3] = ts->grip_landscape_deadzone & 0xFF;
		ts->sec_ts_write(ts, SEC_TS_CMD_LANDSCAPE_MODE, data, 4);
		LOGI("0x%02X %02X,%02X,%02X,%02X\n", SEC_TS_CMD_LANDSCAPE_MODE,
			data[0], data[1], data[2], data[3]);
	}

	if (flag & G_CLR_LANDSCAPE_MODE) {
		data[0] = ts->grip_landscape_mode;
		ts->sec_ts_write(ts, SEC_TS_CMD_LANDSCAPE_MODE, data, 1);
		LOGI("0x%02X %02X\n", SEC_TS_CMD_LANDSCAPE_MODE, data[0]);
	}
}

/*
 * index  0 :  set edge handler
 *  1 :  portrait (normal) mode
 *  2 :  landscape mode
 *
 * data
 *  0, X (direction), X (y start), X (y end)
 *  direction : 0 (off), 1 (left), 2 (right)
 *	ex) echo set_grip_data,0,2,600,900 > cmd
 *
 *  1, X (edge zone), X (dead zone up x), X (dead zone down x), X (dead zone y)
 *	ex) echo set_grip_data,1,200,10,50,1500 > cmd
 *
 *  2, 1 (landscape mode), X (edge zone), X (dead zone)
 *	ex) echo set_grip_data,2,1,200,100 > cmd
 *
 *2, 0 (portrait mode)
 *	ex) echo set_grip_data,2,0  > cmd
 */

static void set_grip_data(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	/* u8 mode = G_NONE; */
	u8 tPara[2] = { 0 };
	int ret;

	sec_cmd_set_default_result(sec);

	memset(buff, 0, sizeof(buff));

	mutex_lock(&ts->device_mutex);

	tPara[0] = sec->cmd_param[0] & 0xFF;
	tPara[1] = (sec->cmd_param[0] >> 8) & 0xFF;

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_DEADZONE_RANGE, tPara, 2);
	if (ret < 0)
		goto err_grip_data;
/*
 *	if (sec->cmd_param[0] == 0) {	// edge handler
 *		if (sec->cmd_param[1] == 0) {	// clear
 *			ts->grip_edgehandler_direction = 0;
 *		} else if (sec->cmd_param[1] < 3) {
 *			ts->grip_edgehandler_direction = sec->cmd_param[1];
 *			ts->grip_edgehandler_start_y = sec->cmd_param[2];
 *			ts->grip_edgehandler_end_y = sec->cmd_param[3];
 *		} else {
 *			LOGE("cmd1 is abnormal, %d (%d)\n",
 *				sec->cmd_param[1], __LINE__);
 *			goto err_grip_data;
 *		}
 *
 *		mode = mode | G_SET_EDGE_HANDLER;
 *		set_grip_data_to_ic(ts, mode);
 *
 *	} else if (sec->cmd_param[0] == 1) {	// normal mode
 *		if (ts->grip_edge_range != sec->cmd_param[1])
 *			mode = mode | G_SET_EDGE_ZONE;
 *
 *		ts->grip_edge_range = sec->cmd_param[1];
 *		ts->grip_deadzone_up_x = sec->cmd_param[2];
 *		ts->grip_deadzone_dn_x = sec->cmd_param[3];
 *		ts->grip_deadzone_y = sec->cmd_param[4];
 *		mode = mode | G_SET_NORMAL_MODE;
 *
 *		if (ts->grip_landscape_mode == 1) {
 *			ts->grip_landscape_mode = 0;
 *			mode = mode | G_CLR_LANDSCAPE_MODE;
 *		}
 *		set_grip_data_to_ic(ts, mode);
 *	} else if (sec->cmd_param[0] == 2) {	// landscape mode
 *		if (sec->cmd_param[1] == 0) {	// normal mode
 *			ts->grip_landscape_mode = 0;
 *			mode = mode | G_CLR_LANDSCAPE_MODE;
 *		} else if (sec->cmd_param[1] == 1) {
 *			ts->grip_landscape_mode = 1;
 *			ts->grip_landscape_edge = sec->cmd_param[2];
 *			ts->grip_landscape_deadzone	= sec->cmd_param[3];
 *			mode = mode | G_SET_LANDSCAPE_MODE;
 *		} else {
 *			LOGE("cmd1 is abnormal, %d (%d)\n",
 *				sec->cmd_param[1], __LINE__);
 *			goto err_grip_data;
 *		}
 *		set_grip_data_to_ic(ts, mode);
 *	} else {
 *		LOGE("cmd0 is abnormal, %d",
 *			sec->cmd_param[0]);
 *		goto err_grip_data;
 *	}
 **/

	mutex_unlock(&ts->device_mutex);

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);
	return;

err_grip_data:
	mutex_unlock(&ts->device_mutex);

	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec_cmd_set_cmd_exit(sec);
}

static void set_log_level(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	u8 w_data[2] = { 0, };
	int ret;

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("Touch is stopped!\n");
		snprintf(buff, sizeof(buff), "%s", "TSP turned off");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;

		return;
	}

	if ((sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) ||
		(sec->cmd_param[1] < 0 || sec->cmd_param[1] > 1) ||
		(sec->cmd_param[2] < 0 || sec->cmd_param[2] > 1) ||
		(sec->cmd_param[3] < 0 || sec->cmd_param[3] > 1) ||
		(sec->cmd_param[4] < 0 || sec->cmd_param[4] > 1) ||
		(sec->cmd_param[5] < 0 || sec->cmd_param[5] > 1) ||
		(sec->cmd_param[6] < 0 || sec->cmd_param[6] > 1) ||
		(sec->cmd_param[7] < 0 || sec->cmd_param[7] > 1)) {
		LOGE("para out of range\n");
		snprintf(buff, sizeof(buff), "%s", "Para out of range");
		sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
		sec->cmd_state = SEC_CMD_STATUS_FAIL;

		return;
	}

	if (sec->cmd_param[0] == 1 && sec->cmd_param[1] == 1 &&
		sec->cmd_param[2] == 1 && sec->cmd_param[3] == 1 &&
		sec->cmd_param[4] == 1 && sec->cmd_param[5] == 1 &&
		sec->cmd_param[6] == 1 && sec->cmd_param[7] == 1) {
		w_data[0] = 0xFF;
		w_data[1] = 0xFF;
	} else {
		w_data[0] = 0x00;
		w_data[1] = 0xFF;
	}

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_VENDOR_EVENT_LEVEL,
				w_data, 2);
	if (ret < 0) {
		LOGE("Failed to write vendor event level\n");
		snprintf(buff, sizeof(buff), "%s", "Write Stat Fail");
		goto err;
	}

	LOGI("VENDOR_EVENT_LEVEL : %02X %02X\n", w_data[0], w_data[1]);

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_OK;

	return;
err:
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
}

static void enter_recovery(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para = 0x0;
	u8 para2 = 0x0;
	u8 para3 = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if ((sec->cmd_param[0] < 0 || sec->cmd_param[0] > 1) 
		|| (sec->cmd_param[1] < 0 || sec->cmd_param[1] > 1)
		|| (sec->cmd_param[2] < 0 || sec->cmd_param[2] > 1)) {
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	para = sec->cmd_param[0]; // enter/exit recovery
	para2 = sec->cmd_param[1]; // hw_reset
	para3 = sec->cmd_param[2]; // irq_control

	ret = sec_ts_enter_recovery(ts, para, para2, para3);
	if (ret < 0) {
		LOGE("Failed to enter recovery, returned %i\n", ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void ddi_osc_on(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	ret = sec_ts_ddi_osc_on(ts);
	if (ret < 0) {
		LOGE("Failed to turn ddi osc on, returned %i\n", ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_hopping_freq(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[4] = { 0 };
	u8 para1 = 0x0;
	u8 para2 = 0x0;
	int ret;

	LOGI("%d\n", sec->cmd_param[0]);

	sec_cmd_set_default_result(sec);

	if (sec->cmd_param[0] == 0) {
		para1 = HOPPING_FREQ_FIX_OFF;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_HOPPING_FREQ_FIX, &para1, 1);
		if (ret < 0) {
			LOGE("write reg %#x para %#x failed, returned %i\n",
				SEC_TS_CMD_SET_HOPPING_FREQ_FIX, para1, ret);
			goto err_out;
		}
	} else {
		if (sec->cmd_param[0] == 1) {
			para2 = HOPPING_FREQ_1;
		} else if (sec->cmd_param[0] == 2) {
			para2 = HOPPING_FREQ_2;
		} else if (sec->cmd_param[0] == 3) {
			para2 = HOPPING_FREQ_3;
		} else {
			LOGI("param error! param = %d\n",sec->cmd_param[0]);
			goto err_out;
		}

		para1 = HOPPING_FREQ_FIX_ON;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_HOPPING_FREQ_FIX, &para1, 1);
		if (ret < 0) {
			LOGE("write reg %#x para %#x failed, returned %i\n",
				SEC_TS_CMD_SET_HOPPING_FREQ_FIX, para1, ret);
			goto err_out;
		}

		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_HOPPING_FREQ, &para2, 1);
		if (ret < 0) {
			LOGE("write reg %#x para %#x failed, returned %i\n",
				SEC_TS_CMD_SET_HOPPING_FREQ, para2, ret);
			goto err_out;
		}
	}

	scnprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void debug(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

	ts->debug = sec->cmd_param[0];

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_print_format(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);

	ts->print_format = !!sec->cmd_param[0];

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void not_support_cmd(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	char buff[SEC_CMD_STR_LEN] = { 0 };

	sec_cmd_set_default_result(sec);
	snprintf(buff, sizeof(buff), "%s", "NA");

	sec_cmd_set_cmd_result(sec, buff, strlen(buff));
	sec->cmd_state = SEC_CMD_STATUS_NOT_APPLICABLE;
	sec_cmd_set_cmd_exit(sec);
}

static void set_touch_mode(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret = 0;
	u8 para[4] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		goto err_out;
	}

	switch (sec->cmd_param[0]) {
	case 1:
		LOGI("param = %d, set Normal ACTIVE mode\n", sec->cmd_param[0]);
		sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_TOUCH);
		break;
	case 2:
		LOGI("param = %d, set Normal IDLE mode\n", sec->cmd_param[0]);
		sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_TOUCH, TOUCH_MODE_STATE_IDLE);
		break;
	case 3:
		LOGI("param = %d, set Lowpower ACTIVE mode\n", sec->cmd_param[0]);
		sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_LOWPOWER, TOUCH_MODE_STATE_TOUCH);
		break;
	case 4:
		LOGI("param = %d, set Lowpower IDLE mode\n", sec->cmd_param[0]);
		sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_LOWPOWER, TOUCH_MODE_STATE_IDLE);
		break;
	case 5:
		LOGI("param = %d, SENSE_ON\n", sec->cmd_param[0]);
		/* SENSE_ON */
		ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
		if (ret < 0)
			LOGE("Failed to write SENSE_ON\n");
		sec_ts_delay(50);
		break;
	case 6:
		LOGI("param = %d, Sense Off\n", sec->cmd_param[0]);
		sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_SLEEP, SLEEP_MODE_STATE_SLEEP);
		break;
	case 7:
		LOGI("param = %d %d, do touch system reset\n",
			sec->cmd_param[0], sec->cmd_param[1]);
		switch (sec->cmd_param[1]) {
		case RESET_MODE_SW:
			ret = sec_ts_system_reset(ts, RESET_MODE_SW, true, false);
			break;
		case RESET_MODE_HW:
			ret = sec_ts_system_reset(ts, RESET_MODE_HW, true, false);
			break;
		default:
			ret = sec_ts_system_reset(ts, RESET_MODE_AUTO, true, false);
		}
		break;
	case 8:
		LOGI("param = %d, Toggle Sense On/Off\n", sec->cmd_param[0]);
		ret = ts->sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, para, 2);
		if (ret < 0) {
			LOGE("Failed to read status(%d)\n", ret);
			goto err_out;
		}

		if (para[0] == TOUCH_SYSTEM_MODE_SLEEP) {// have to sense on
			LOGI("param = %d, Sense On\n", sec->cmd_param[0]);
			/* SENSE_ON */
			ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
			if (ret < 0)
				LOGE("Failed to write SENSE_ON\n");

			sec_ts_delay(300);

			LOGD("SENSE_ON\n");
		} else {// have to sense off
			LOGI("param = %d, Sense Off\n", sec->cmd_param[0]);
			sec_ts_fix_tmode(ts, TOUCH_SYSTEM_MODE_SLEEP, SLEEP_MODE_STATE_SLEEP);
		}

		break;
	default:
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}
	if (ret)
		goto err_out;

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	return;

err_out:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void spi_checksum_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret = 0;
	u8 enb[1] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		goto err_out;
	}

	switch (sec->cmd_param[0]) {
	case 0:
		LOGI("param = %d, set SPI Checksum Disabled\n", sec->cmd_param[0]);
		enb[0] = 0;
		ret = sec_ts_spi_checksum_enable(ts, enb[0]);
		if (ret)
			goto err_out;
		break;
	case 1:
		LOGI("param = %d, set SPI Checksum Enabled\n", sec->cmd_param[0]);
		enb[0] = 1;
		ret = sec_ts_spi_checksum_enable(ts, enb[0]);
		if (ret)
			goto err_out;
		break;
	default:
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	return;

err_out:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void get_sync_freq(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[30] = { 0 };
	int ret;
	u8 freq[4];

	sec_cmd_set_default_result(sec);

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_DDI_SYNC_FREQ, freq, 4);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_GET_DDI_SYNC_FREQ, ret);
		goto err_out;
	}

	scnprintf(buff, sizeof(buff), "Vsync: %d, Hsync: %d",
			(int)((freq[0] << 8) | (freq[1] << 0)),
			(int)((freq[2] << 8) | (freq[3] << 0)));

	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	LOGI("%s\n", buff);
	return;

err_out:
	scnprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	return;
}

static void heatmap_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret = 0;
	u8 enb[1] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		goto err_out;
	}

	switch (sec->cmd_param[0]) {
	case 0:
		LOGI("param = %d, heatmap disabled\n", sec->cmd_param[0]);
		enb[0] = 0;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_HEATMAP_ENABLE, enb, 1);
		if (ret)
			goto err_out;
		break;
	case 1:
		LOGI("param = %d, heatmap enabled\n", sec->cmd_param[0]);
		enb[0] = 1;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_HEATMAP_ENABLE, enb, 1);
		if (ret)
			goto err_out;
		break;
	default:
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	return;

err_out:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void sttw_gesture_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret = 0;
	u8 enb[1] = { 0 };
	u8 sttw_param[3] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		goto err_out;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_GESTURE_ENABLE, enb, 1);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_SET_GESTURE_ENABLE, ret);
		goto err_out;
	}

	switch (sec->cmd_param[0]) {
	case 0:
		LOGI("param = %d, STTW gesture disabled\n", sec->cmd_param[0]);
		enb[0] &= ~(1u << SEC_TS_GESTURE_CODE_STTW);
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_ENABLE, enb, 1);
		if (ret)
			goto err_out;
		break;
	case 1:
		// single_tap_min_x: 910
		sttw_param[0] = 0x00; // 1st para
		sttw_param[1] = 0x03; // (910 >> 16) & 0xFF
		sttw_param[2] = 0x81; // (910 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		// single_tap_max_x: 9890
		sttw_param[0] = 0x01; // 2nd para
		sttw_param[1] = 0x26; // (9890 >> 16) & 0xFF
		sttw_param[2] = 0xA6; // (9890 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		// single_tap_min_y: 1790
		sttw_param[0] = 0x02; // 3rd para
		sttw_param[1] = 0x06; // (1790 >> 16) & 0xFF
		sttw_param[2] = 0xFE; // (1790 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		// single_tap_max_y: 21630
		sttw_param[0] = 0x03; // 4th para
		sttw_param[1] = 0x54; // (21630 >> 16) & 0xFF
		sttw_param[2] = 0x7E; // (21630 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		// single_tap_min_frame_count: 3
		sttw_param[0] = 0x04; // 5th para
		sttw_param[1] = 0x00; // (3 >> 16) & 0xFF
		sttw_param[2] = 0x03; // (3 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		// single_tap_max_frame_count: 31
		sttw_param[0] = 0x05; // 6th para
		sttw_param[1] = 0x00; // (31 >> 16) & 0xFF
		sttw_param[2] = 0x1F; // (31 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		// single_tap_motion_tolerance or single_tap_jitter: 2550
		sttw_param[0] = 0x06; // 7th para
		sttw_param[1] = 0x09; // (2550 >> 16) & 0xFF
		sttw_param[2] = 0xF6; // (2550 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		// single_tap_max_touch_size: 50
		sttw_param[0] = 0x07; // 8th para
		sttw_param[1] = 0x00; // (50 >> 16) & 0xFF
		sttw_param[2] = 0x32; // (50 >>  0) & 0xFF
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_STTW_PARAM, sttw_param, 3);
		if (ret)
			goto err_out;
		LOGI("param = %d, STTW gesture enabled\n", sec->cmd_param[0]);
		enb[0] |= (1u << SEC_TS_GESTURE_CODE_STTW);
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_ENABLE, enb, 1);
		if (ret)
			goto err_out;
		break;
	default:
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	return;

err_out:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void sttw_invalid_gesture_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret = 0;
	u8 enb[1] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		goto err_out;
	}

	switch (sec->cmd_param[0]) {
	case 0:
		LOGI("param = %d, Invalid gesture disabled\n", sec->cmd_param[0]);
		enb[0] = 0;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_INVALID_ENABLE, enb, 1);
		if (ret)
			goto err_out;
		break;
	case 1:
		LOGI("param = %d, Invalid gesture enabled\n", sec->cmd_param[0]);
		enb[0] = 1;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_GESTURE_INVALID_ENABLE, enb, 1);
		if (ret)
			goto err_out;
		break;
	default:
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	return;

err_out:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void high_sensitivity_mode_enable(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret = 0;
	u8 enb[1] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		goto err_out;
	}

	switch (sec->cmd_param[0]) {
	case 0:
		LOGI("param = %d, high sensitivity mode disabled\n", sec->cmd_param[0]);
		enb[0] = 0;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_HIGH_SENSITIVITY, enb, 1);
		if (ret)
			goto err_out;
		break;
	case 1:
		LOGI("param = %d, high sensitivity mode enabled\n", sec->cmd_param[0]);
		enb[0] = 1;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_HIGH_SENSITIVITY, enb, 1);
		if (ret)
			goto err_out;
		break;
	default:
		LOGI("param error! param = %d\n", sec->cmd_param[0]);
		goto err_out;
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	return;

err_out:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void set_freq(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret = 0;
	u8 set_freq_param[3] = { 0 };

	sec_cmd_set_default_result(sec);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER off!\n");
		goto err_out;
	}

	switch (sec->cmd_param[0]) {
	case 0:
		// MS freq set
		LOGI("param = %d, %d, MS freq set to %d\n", sec->cmd_param[0], sec->cmd_param[1], sec->cmd_param[1]);
		set_freq_param[0] = 0x06;
		set_freq_param[1] = (sec->cmd_param[1] >> 8) & 0xFF;
		set_freq_param[2] = (sec->cmd_param[1] >> 0) & 0xFF;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_FREQ, set_freq_param, 3);
		if (ret)
			goto err_out;
		break;
	case 1:
		// SS freq set
		LOGI("param = %d, %d, SS freq set to %d\n", sec->cmd_param[0], sec->cmd_param[1], sec->cmd_param[1]);
		set_freq_param[0] = 0x00;
		set_freq_param[1] = (sec->cmd_param[1] >> 8) & 0xFF;
		set_freq_param[2] = (sec->cmd_param[1] >> 0) & 0xFF;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_FREQ, set_freq_param, 3);
		if (ret)
			goto err_out;
		set_freq_param[0] = 0x01;
		set_freq_param[1] = (sec->cmd_param[1] >> 8) & 0xFF;
		set_freq_param[2] = (sec->cmd_param[1] >> 0) & 0xFF;
		ret = ts->sec_ts_write(ts, SEC_TS_CMD_SET_FREQ, set_freq_param, 3);
		if (ret)
			goto err_out;
		break;
	default:
		LOGI("param error! param = %d, %d\n", sec->cmd_param[0], sec->cmd_param[1]);
		goto err_out;
	}

	snprintf(buff, sizeof(buff), "%s", "OK");
	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));

	return;

err_out:
	snprintf(buff, sizeof(buff), "%s", "NG");
	sec->cmd_state = SEC_CMD_STATUS_FAIL;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
}

static void get_sensing_info(void *device_data)
{
	struct sec_cmd_data *sec = (struct sec_cmd_data *)device_data;
	struct sec_ts_data *ts = container_of(sec, struct sec_ts_data, sec);
	const unsigned int buff_size = SEC_CMD_STR_LEN;
	unsigned int buff_len = 0;
	char buff[SEC_CMD_STR_LEN] = { 0 };
	int ret;
	u8 freq_cur_idx;
	u8 disp_noise_level[3];
	u8 ext_noise_level[3];
	u8 noise_mode_status;
	u8 wet_mode_status;
	u8 touch_mode[2];

	sec_cmd_set_default_result(sec);

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_HOPPING_FREQ, &freq_cur_idx, 1);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_SET_HOPPING_FREQ, ret);
		freq_cur_idx = 0xFF;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_DISP_NOISE_LEVEL, disp_noise_level, 3);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_GET_DISP_NOISE_LEVEL, ret);
		disp_noise_level[0] = 0xFF;
		disp_noise_level[1] = 0xFF;
		disp_noise_level[2] = 0xFF;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_EXT_NOISE_LEVEL, ext_noise_level, 3);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_GET_EXT_NOISE_LEVEL, ret);
		ext_noise_level[0] = 0xFF;
		ext_noise_level[1] = 0xFF;
		ext_noise_level[2] = 0xFF;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SET_NOISE_MODE, &noise_mode_status, 1);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_SET_NOISE_MODE, ret);
		noise_mode_status = 0xFF;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_WET_MODE_STATUS, &wet_mode_status, 1);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_READ_WET_MODE_STATUS, ret);
		wet_mode_status = 0xFF;
	}

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, touch_mode, 2);
	if (ret < 0) {
		LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_SYSTEM_MODE, ret);
		touch_mode[0] = 0xFF;
		touch_mode[1] = 0xFF;
	}

	buff_len += scnprintf(buff + buff_len, buff_size - buff_len,
			"mode %#x, state %#x, freq idx(ms): %d, freq idx(ss): %d, ",
			touch_mode[0], touch_mode[1], (freq_cur_idx & 0x0F), ((freq_cur_idx & 0xF0) >> 4));
	buff_len += scnprintf(buff + buff_len, buff_size - buff_len,
			"disp noise: (0) %d (1) %d (2) %d, ext noise: (0) %d (1) %d (2) %d, ",
			disp_noise_level[0], disp_noise_level[1], disp_noise_level[2],
			ext_noise_level[0], ext_noise_level[1], ext_noise_level[2]);
	buff_len += scnprintf(buff + buff_len, buff_size - buff_len,
			"noise mode: %d, water mode: %d\n",
			noise_mode_status, wet_mode_status);

	sec->cmd_state = SEC_CMD_STATUS_OK;
	sec_cmd_set_cmd_result(sec, buff, strnlen(buff, sizeof(buff)));
	LOGI("%s\n", buff);
	return;
}

int sec_ts_fn_init(struct sec_ts_data *ts)
{
	int retval;

	retval = sec_cmd_init(&ts->sec, sec_cmds,
			ARRAY_SIZE(sec_cmds), SEC_CLASS_DEVT_TSP);
	if (retval < 0) {
		LOGE("Failed to sec_cmd_init\n");
		goto exit;
	}

	retval = sysfs_create_group(&ts->sec.fac_dev->kobj,
			&cmd_attr_group);
	if (retval < 0) {
		LOGE("Failed to create sysfs attributes\n");
		goto exit;
	}

	retval = sysfs_create_link(&ts->sec.fac_dev->kobj,
				&ts->input_dev->dev.kobj, "input");
	if (retval < 0) {
		LOGE("Failed to create input symbolic link\n");
		goto exit;
	}

	ts->reinit_done = true;

	return 0;

exit:
	return retval;
}

void sec_ts_fn_remove(struct sec_ts_data *ts)
{
	LOGE("\n");

	sysfs_remove_link(&ts->sec.fac_dev->kobj, "input");

	sysfs_remove_group(&ts->sec.fac_dev->kobj,
			   &cmd_attr_group);

	sec_cmd_exit(&ts->sec, SEC_CLASS_DEVT_TSP);

}

