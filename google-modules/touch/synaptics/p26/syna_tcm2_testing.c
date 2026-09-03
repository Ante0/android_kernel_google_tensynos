// SPDX-License-Identifier: GPL-2.0
/*
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
 * @file syna_tcm2_testing.c
 *
 * This file implements the sample code to perform the production testing.
 */
#include "syna_tcm2_testing.h"
#include "syna_tcm2_testing_limits.h"
#include "synaptics_touchcom_core_dev.h"
#include "synaptics_touchcom_func_base.h"


typedef enum{
	RAW_GAP_TEST = 1,
	SENSOR_SPEED_TEST
}gaptesttype_t;

/**
 * @brief  Print the frame data in log.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] data: frame data
 *    [ in] rows: the number of rows
 *    [ in] cols: the number of columns
 *
 * @return
 *    void
 */
static void syna_testing_print_list(struct syna_tcm *tcm, unsigned char *data, int rows, int cols,
				    bool is_int16_data)
{
	int i;
	int count = 0;
	void *data_ptr = NULL;
	char print_buf[512];
	int value;

	if (data == NULL) {
		LOGE("data is null.");
		return;
	}

	data_ptr = (void *)&data[0];

	for (i = 0; i < cols; i++) {
		value = is_int16_data ? (int)*((unsigned short *)data_ptr + i) :
					*((int *)data_ptr + i);
		count += scnprintf(print_buf + count, PAGE_SIZE - count, "%d ", value);
	}
	LOGI("%s\n", &print_buf[0]);

	count = 0;
	for (i = 0; i < rows; i++) {
		value = is_int16_data ? (int)*((unsigned short *)data_ptr + cols + i) :
					*((int *)data_ptr + cols + i);
		count += scnprintf(print_buf + count, PAGE_SIZE - count, "%d ", value);
	}
	LOGI("%s\n", &print_buf[0]);
}
/**
 * @brief  Print the frame data in log.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] data: frame data
 *    [ in] rows: the number of rows
 *    [ in] cols: the number of columns
 *
 * @return
 *    void
 */
static void syna_testing_print_frame(struct syna_tcm *tcm, unsigned char *data, int rows, int cols)
{
	int i, j;
	int count = 0;
	short *data_ptr = NULL;
	char print_buf[256];

	if (data == NULL) {
		LOGE("data is null.");
		return;
	}

	data_ptr = (short *)&data[0];
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			count += scnprintf(print_buf + count, PAGE_SIZE - count, "%d ",
					data_ptr[i * tcm->tcm_dev->cols + j]);
		}
		LOGI("%s\n", &print_buf[0]);
		count = 0;
	}
}

/*
 * syna_print_serial_number()
 *
 * Print the serial number result.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] sn_data: serial number data
 */
static void syna_print_serial_number(struct syna_tcm *tcm, struct tcm_serial_number_info *sn_data)
{
	if (sn_data == NULL) {
		LOGE("sn_data is null.");
		return;
	}

	LOGI("wafer lot: %*ph\n", (int) sizeof(sn_data->wafer_lot), sn_data->wafer_lot);
	LOGI("x coordinate: %*ph\n", (int) sizeof(sn_data->x_coordinate), sn_data->x_coordinate);
	LOGI("y coordinate: %*ph\n", (int) sizeof(sn_data->y_coordinate), sn_data->y_coordinate);
	LOGI("{date[4] + wafer id[2]&no[2]} or {wafer id[4] + date[4]}: %*ph\n",
		(int) sizeof(sn_data->date_wafer_code), sn_data->date_wafer_code);
}

/**
 * @brief  Parse the test limit from the device tree.
 *
 * @param
 *    [ in] np: device node
 *    [ in] name: property name
 *    [out] array: test limit array
 *    [ in] size: size of the array
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_parse_test_limit_16(struct syna_tcm *tcm, const char *name, u16 *array, int size)
{
	int length;
	struct device_node *np = tcm->pdev->dev.parent->of_node;

	length = of_property_count_u16_elems(np, name);
	if (length < 0) {
		LOGE("Fail to get %s elements count, ret = %d\n", name, length);
		return length;
	}

	if (length != size) {
		LOGE("invalid array length %d\n", length);
		return -EINVAL;
	}

	if (of_property_read_u16_array(np, name, array, length) < 0) {
		LOGE("Error reading array %s\n", name);
		return -EINVAL;
	}

	LOGI("Parsed %s from device tree", name);

	return 0;
}
/**
 * @brief  Parse the test limit from the device tree.
 *
 * @param
 *    [ in] np: device node
 *    [ in] name: property name
 *    [out] array: test limit array
 *    [ in] size: size of the array
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_parse_test_limit_32(struct syna_tcm *tcm, const char *name, u32 *array, int size)
{
	int length;
	struct device_node *np = tcm->pdev->dev.parent->of_node;

	length = of_property_count_u32_elems(np, name);
	if (length < 0) {
		LOGE("Fail to get %s elements count, ret = %d\n", name, length);
		return -EINVAL;
	}

	if (length != size) {
		LOGE("invalid array length %d\n", length);
		return -EINVAL;
	}

	if (of_property_read_u32_array(np, name, array, length)) {
		LOGE("Error reading array %s\n", name);
		return -EINVAL;
	}

	LOGI("Parsed %s from device tree", name);

	return 0;
}

/**
 * @brief  Sample code to compare the test result with limits by byte vector.
 *
 * @param
 *    [ in] data: target test data
 *    [ in] data_size: size of test data
 *    [ in] limit: test limit value to be compared with
 *    [ in] limit_size: size of test limit
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static bool syna_testing_compare_byte_vector(unsigned char *data,
	unsigned int data_size, const unsigned char *limit,
	unsigned int limit_size)
{
	bool result = false;
	unsigned char tmp;
	unsigned char p, l;
	int i, j;

	if (!data || (data_size == 0)) {
		LOGE("Invalid test data\n");
		return false;
	}
	if (!limit || (limit_size == 0)) {
		LOGE("Invalid limits\n");
		return false;
	}

	if (limit_size < data_size) {
		LOGE("Limit size mismatched, data size: %d, limits: %d\n",
			data_size, limit_size);
		return false;
	}

	result = true;
	for (i = 0; i < data_size; i++) {
		tmp = data[i];

		for (j = 0; j < 8; j++) {
			p = GET_BIT(tmp, j);
			l = GET_BIT(limit[i], j);
			if (p != l) {
				LOGE("Fail on TRX-%03d (data:%X, limit:%X)\n",
					(i*8 + j), p, l);
				result = false;
			}
		}
	}

	return result;
}

/**
 * @brief  Sample code to compare the test result with limits being
 *         formatted as a frame.
 *
 * @param
 *    [ in] data: target test data
 *    [ in] data_size: size of test data
 *    [ in] rows: the number of rows
 *    [ in] cols: the number of column
 *    [ in] limits_hi: upper-bound test limit
 *    [ in] limits_lo: lower-bound test limit
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static bool syna_testing_compare_frame(unsigned char *data,
	unsigned int data_size, int rows, int cols,
	const short *limits_hi, const short *limits_lo)
{
	bool result = false;
	short *data_ptr = NULL;
	short limit;
	int i, j;

	if (!data || (data_size == 0)) {
		LOGE("Invalid test data\n");
		return false;
	}

	if (data_size < (2 * rows * cols)) {
		LOGE("Size mismatched, data:%d (expected:%d)\n",
			data_size, (2 * rows * cols));
		result = false;
		return false;
	}

	if (rows > LIMIT_BOUNDARY) {
		LOGE("Rows mismatched, rows:%d (expected:%d)\n",
			rows, LIMIT_BOUNDARY);
		result = false;
		return false;
	}

	if (cols > LIMIT_BOUNDARY) {
		LOGE("Columns mismatched, cols: %d (expected:%d)\n",
			cols, LIMIT_BOUNDARY);
		result = false;
		return false;
	}

	result = true;

	if (!limits_hi)
		goto end_of_upper_bound_limit;

	data_ptr = (short *)&data[0];
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			limit = limits_hi[i * cols + j];
			if (*data_ptr > limit) {
				LOGE("Fail on (%2d,%2d)=%5d, limits_hi:%4d\n",
					i, j, *data_ptr, limit);
				result = false;
			}
			data_ptr++;
		}
	}

end_of_upper_bound_limit:

	if (!limits_lo)
		goto end_of_lower_bound_limit;

	data_ptr = (short *)&data[0];
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			limit = limits_lo[i * cols + j];
			if (*data_ptr < limit) {
				LOGE("Fail on (%2d,%2d)=%5d, limits_lo:%4d\n",
					i, j, *data_ptr, limit);
				result = false;
			}
			data_ptr++;
		}
	}

end_of_lower_bound_limit:
	return result;
}
/**
 * @brief  Sample code to compare the test result with limits being
 *         formatted as a list.
 *
 * @param
 *    [ in] data: target test data
 *    [ in] data_size: size of test data
 *    [ in] rows: the number of rows
 *    [ in] cols: the number of column
 *    [ in] limits_hi: upper-bound test limit
 *    [ in] limits_lo: lower-bound test limit
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static bool syna_testing_compare_list(unsigned char *data, unsigned int data_size, int rows,
				      int cols, const int *limits_hi, const int *limits_lo,
				      bool is_int16_data)
{
	bool result = false;
	const void *data_ptr = NULL;
	int limit;
	int i;
	int total_elements = rows + cols;
	int element_size = is_int16_data ? sizeof(unsigned short) : sizeof(int);
	int value;

	if (!data || (data_size == 0)) {
		LOGE("Invalid test data\n");
		return false;
	}

	if (data_size % total_elements != 0) {
		LOGE("Size mismatched, data:%d (expected:%d * N)\n", data_size, total_elements);
		result = false;
		return false;
	}

	if (rows > LIMIT_BOUNDARY) {
		LOGE("Rows mismatched, rows:%d (expected:%d)\n",
			rows, LIMIT_BOUNDARY);
		result = false;
		return false;
	}

	if (cols > LIMIT_BOUNDARY) {
		LOGE("Columns mismatched, cols: %d (expected:%d)\n",
			cols, LIMIT_BOUNDARY);
		result = false;
		return false;
	}

	result = true;

	if (!limits_hi)
		goto end_of_upper_bound_limit;

	data_ptr = (void *)&data[0];
	for (i = 0; i < cols; i++) {
		value = is_int16_data ? (int)(*((unsigned short *)data_ptr)) : *((int *)data_ptr);
		limit = limits_hi[i];
		if (value > limit) {
			LOGE("Fail on cols-%2d=%5d, limits_hi:%4d\n", i, value, limit);
			result = false;
		}
		data_ptr = (void *)((char *)data_ptr + element_size);
	}
	for (i = 0; i < rows; i++) {
		value = is_int16_data ? (int)(*((unsigned short *)data_ptr)) : *((int *)data_ptr);
		limit = limits_hi[cols + i];
		if (value > limit) {
			LOGE("Fail on row-%2d=%5d, limits_hi:%4d\n", i, value, limit);
			result = false;
		}
		data_ptr = (void *)((char *)data_ptr + element_size);
	}

end_of_upper_bound_limit:

	if (!limits_lo)
		goto end_of_lower_bound_limit;

	data_ptr = (int *)&data[0];
	for (i = 0; i < cols; i++) {
		value = is_int16_data ? (int)(*((unsigned short *)data_ptr)) : *((int *)data_ptr);
		limit = limits_lo[i];
		if (value < limit) {
			LOGE("Fail on cols-%2d=%5d, limits_lo:%4d\n", i, value, limit);
			result = false;
		}
		data_ptr = (void *)((char *)data_ptr + element_size);
	}
	for (i = 0; i < rows; i++) {
		value = is_int16_data ? (int)(*((unsigned short *)data_ptr)) : *((int *)data_ptr);
		limit = limits_lo[cols + i];
		if (value < limit) {
			LOGE("Fail on row-%2d=%5d, limits_lo:%4d\n", i, value, limit);
			result = false;
		}
		data_ptr = (void *)((char *)data_ptr + element_size);
	}

end_of_lower_bound_limit:
	return result;
}

/**
 * @brief  Sample code to calculate the GAP frame for the test
 *
 * @param
 *    [ in] in: input frame
 *    [out] out: output frame
 *    [ in] rows: number of rows
 *    [ in] cols: number of cols
 *    [ in] testtype: type of the test to be performed ( 1- full raw cap, 2 - sensor speed test )
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_calculate_gap_frame(short *in, short *out,
		int rows, int cols, gaptesttype_t testtype)
{
	int i, j, idx;
	int val_1, val_2, val_3;
	int x_gap, y_gap;

	if (!in || !out) {
		LOGE("Invalid frame to calculate\n");
		return -1;
	}

	if (rows * cols <= 0) {
		LOGE("Invalid parameter of rows and cols\n");
		return -1;
	}

	idx = 0;
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			val_1 = in[i * cols + j];

			if(j == cols -1)
				val_2 = val_1;
			else
				val_2 = in[i * cols + (j+1)];

			if(i == rows -1)
				val_3 = val_1;
			else
				val_3 = in[(i+1) * cols + j];

			if(testtype == RAW_GAP_TEST){
				if((val_1 == 0) && (val_2 == 0))
					x_gap = 0;
				else
					x_gap = (val_1 > val_2)? (100 - ((val_2*100 + val_1/2)/val_1)) : (100 - ((val_1*100 + val_2/2)/val_2));

				if((val_1 == 0) && (val_3 == 0))
					y_gap = 0;
				else
					y_gap = (val_1 > val_3)? (100 - ((val_3*100 + val_1/2)/val_1)) : (100 - ((val_1*100 + val_3/2)/val_3));

				out[idx++] = (x_gap > y_gap)? x_gap : y_gap;
			}
			else if(testtype == SENSOR_SPEED_TEST){
				x_gap = abs(val_1 - val_2);
				y_gap = abs(val_1 - val_3);

				out[idx++] = (x_gap > y_gap)? x_gap: y_gap;
			}
		}
	}
	return 0;
}

/**
 * @brief  Sample code to calculate the GAP frame for the test
 *
 * @param
 *    [ in] in: input frame
 *    [out] out: output frame
 *    [ in] rows: number of rows
 *    [ in] cols: number of cols
 *    [ in] direction_x: appoint the gap direction x. Follow the panel maker
 *                       that the x direction is the longer side.
 *    [ in] abs_only: indicate to return the abs value
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_calculate_gap_frame_b(short *in, short *out,
		int rows, int cols, bool direction_x, bool abs_only)
{
	int i, j, idx;
	short val_1, val_2;

	if (!in || !out) {
		LOGE("Invalid frame to calculate\n");
		return -1;
	}

	if (rows * cols <= 0) {
		LOGE("Invalid parameter of rows and cols\n");
		return -1;
	}

	idx = 0;
	if (direction_x) {
		for (i = 0; i < rows; i++) {
			for (j = 1; j < cols; j++) {
				val_1 = in[i * cols + j];
				val_2 = in[i * cols + (j-1)];
				if (abs_only) {
					out[idx++] = (short)abs(val_1 - val_2);
				} else {
					out[idx++] = (val_2 == 0) ? 0 :
						((abs(val_1 - val_2)) * 100 / val_2);
				}
			}
		}
	} else {
		for (i = 1; i < rows; i++) {
			for (j = 0; j < cols; j++) {
				val_1 = in[i * cols + j];
				val_2 = in[(i-1) * cols + j];
				if (abs_only) {
					out[idx++] = (short)abs(val_1 - val_2);
				} else {
					out[idx++] = (val_2 == 0) ? 0 :
						((abs(val_1 - val_2)) * 100 / val_2);
				}
			}
		}
	}


	return 0;
}

/**
 * @brief  Sample code to check the device ID.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_device_id(struct syna_tcm *tcm)
{
	int retval;
	bool result;
	struct tcm_identification_info info;
	char *strptr = NULL;

	LOGI("Start testing\n");

	retval = syna_tcm_identify(tcm->tcm_dev, &info, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to get identification\n");
		result = false;
		goto exit;
	}

	strptr = strnstr(info.part_number,
					device_id_limit,
					strlen(info.part_number));
	if (strptr != NULL)
		result = true;
	else {
		LOGE("Device ID mismatched, FW: %s (limit: %s)\n",
			info.part_number, device_id_limit);
		result = false;
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Sample code to check the config ID.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_config_id(struct syna_tcm *tcm)
{
	int retval;
	bool result;
	struct tcm_application_info info;
	int idx;

	LOGI("Start testing\n");

	retval = syna_tcm_get_app_info(tcm->tcm_dev, &info, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to get app info\n");
		result = false;
		goto exit;
	}

	result = true;
	for (idx = 0; idx < sizeof(config_id_limit); idx++) {
		if (config_id_limit[idx] != info.customer_config_id[idx]) {
			LOGE("Fail on byte.%d (data: %02X, limit: %02X)\n",
				idx, info.customer_config_id[idx],
				config_id_limit[idx]);
			result = false;
		}
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Example to trigger the ID comparison testing.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_check_id_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		retval = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	count = 0;

	retval = syna_testing_device_id(tcm);

	retval = scnprintf(buf, PAGE_SIZE - count,
			"Device ID check: %s\n",
			(retval < 0) ? "fail" : "pass");

	buf += retval;
	count += retval;

	retval = syna_testing_config_id(tcm);

	retval = scnprintf(buf, PAGE_SIZE - count,
			"Config ID check: %s\n",
			(retval < 0) ? "fail" : "pass");

	buf += retval;
	count += retval;

	retval = count;
exit:
	return retval;
}

static struct kobj_attribute kobj_attr_check_id =
	__ATTR(check_id, 0444, syna_testing_check_id_show, NULL);

/**
 * @brief  Sample code to perform PT01 (PID01) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_pt01(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID01_TRX_TRX_SHORTS,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID01_TRX_TRX_SHORTS);
		result = false;
		goto exit;
	}

	result = syna_testing_compare_byte_vector(test_data->buf,
			test_data->data_length,
			pt01_limits,
			ARRAY_SIZE(pt01_limits));

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT01 (PID01) testing.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt01_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, i;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt01(tcm, &test_data);

	count += scnprintf(buf, PAGE_SIZE,
			"TEST PT$01: %s\n", (retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	for (i = 0; i < test_data.data_length; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
				test_data.buf[i]);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt01 =
	__ATTR(pt01, 0444, syna_testing_pt01_show, NULL);

/**
 * @brief  Sample code to perform PT5B (PID91) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_pt5b(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;
	static unsigned char TEST_PID91_TRX_TRX_SHORTS = 0x5B;
	unsigned char channel;
	unsigned char p, l;
	int i, j;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID91_TRX_TRX_SHORTS,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID91_TRX_TRX_SHORTS);
		result = false;
		goto exit;
	}

	if (ARRAY_SIZE(pt5b_limits) < test_data->data_length) {
		LOGE("Limit size mismatched, data size: %d, limits: %lu\n",
			test_data->data_length, ARRAY_SIZE(pt5b_limits));
		return false;
	}

	result = true;
	for (i = 0; i < test_data->data_length; i++) {
		for (j = 0; j < 8; j++) {
			p = GET_BIT(test_data->buf[i], j);
			l = GET_BIT(pt5b_limits[i], j);
			if (p != l) {
				channel = i * 8 + j;
				if (channel < tcm->tcm_dev->cols) {
					LOGE("Fail on R-%03d (data:%X, limit:%X)\n", channel, p, l);
				} else if (channel < tcm->tcm_dev->rows) {
					LOGE("Fail on T-%03d (data:%X, limit:%X)\n", channel, p, l);
				} else {
					LOGE("Fail on G-%03d (data:%X, limit:%X)\n", channel, p, l);
				}
				result = false;
			}
		}
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT5B (PID5B) testing.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt5b_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, i;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt5b(tcm, &test_data);

	count += scnprintf(buf, PAGE_SIZE,
			"TEST PT$5B: %s\n", (retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	for (i = 0; i < test_data.data_length; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
				test_data.buf[i]);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt5b =
	__ATTR(pt5b, 0444, syna_testing_pt5b_show, NULL);

/**
 * @brief  Sample code to perform PT05 (PID05) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_pt05(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID05_FULL_RAW_CAP,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID05_FULL_RAW_CAP);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt05_high_limit_name,
			(u16*) pt05_hi_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);
	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt05_low_limit_name,
			(u16*) pt05_lo_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);

	result = syna_testing_compare_frame(test_data->buf,
			test_data->data_length,
			tcm->tcm_dev->rows,
			tcm->tcm_dev->cols,
			(const short *)&pt05_hi_limits[0],
			(const short *)&pt05_lo_limits[0]);

	if (test_data->buf != NULL)
		syna_testing_print_frame(tcm, test_data->buf, tcm->tcm_dev->rows, tcm->tcm_dev->cols);

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Sample code to perform PT05 (PID05) GAP testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] test_data: buffer stored the testing data
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt05_gap(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;
	int rows = tcm->tcm_dev->rows;
	int cols = tcm->tcm_dev->cols;
	short *data_ptr = NULL;
	short *frame = NULL;
	short *gap_frame_x = NULL;
	short *gap_frame_y = NULL;
	int i, j, idx;

	LOGI("Start testing\n");

	if (test_data->buf == NULL) {
		LOGE("Test data is NULL\n");
		result = false;
		goto exit;
	}

	frame = syna_pal_mem_alloc(rows * cols, sizeof(short));
	if (!frame) {
		LOGE("Fail to allocate image for gap test\n");
		result = false;
		frame = NULL;
		goto exit;
	}
	gap_frame_x = syna_pal_mem_alloc(rows * cols, sizeof(short));
	if (!gap_frame_x) {
		LOGE("Fail to allocate gap image x for gap test\n");
		result = false;
		gap_frame_x = NULL;
		goto exit;
	}
	gap_frame_y = syna_pal_mem_alloc(rows * cols, sizeof(short));
	if (!gap_frame_y) {
		LOGE("Fail to allocate gap image y for gap test\n");
		result = false;
		gap_frame_y = NULL;
		goto exit;
	}

	data_ptr = (short *)&test_data->buf[0];
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			frame[i * cols + j] = *data_ptr;
			data_ptr++;
		}
	}

	if (tcm->hw_if->test_algo == 1) {
		LOGI("Gap test algo b");
		retval = syna_testing_calculate_gap_frame_b(frame,
				gap_frame_x, rows, cols, true, false);
		if (retval < 0) {
			LOGE("Fail to get the gap frame x\n");
			result = false;
			goto exit;
		}

		retval = syna_testing_calculate_gap_frame_b(frame,
				gap_frame_y, rows, cols, false, false);
		if (retval < 0) {
			LOGE("Fail to get the gap frame y\n");
			result = false;
			goto exit;
		}

		syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt05_gap_x_limit_name,
				(u16*) pt05_gap_x_limits, rows * (cols - 1));
		syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt05_gap_y_limit_name,
				(u16*) pt05_gap_y_limits, (rows - 1) * cols);

		/* compare to the limits */
		result = true;
		for (i = 0; i < rows; i++) {
			for (j = 0; j < cols - 1; j++) {
				idx = i * (cols - 1) + j;
				if (gap_frame_x[idx] > pt05_gap_x_limits[idx]) {
					LOGE("Fail on gapX (%2d,%2d)=%5d, max:%4d\n",
						j, i, gap_frame_x[idx], pt05_gap_x_limits[idx]);
					result = false;
				}
			}
		}

		for (i = 0; i < rows - 1; i++) {
			for (j = 0; j < cols; j++) {
				idx = i * cols + j;
				if (gap_frame_y[idx] > pt05_gap_y_limits[idx]) {
					LOGE("Fail on gapY (%2d,%2d)=%5d, max:%4d\n",
						j, i, gap_frame_y[idx], pt05_gap_y_limits[idx]);
					result = false;
				}

			}
		}
	} else {
		LOGI("Gap test algo a");
		retval = syna_testing_calculate_gap_frame(frame,
				gap_frame_x, rows, cols, RAW_GAP_TEST);
		if (retval < 0) {
			LOGE("Fail to get the gap frame\n");
			result = false;
			goto exit;
		}

		syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt05_gap_x_limit_name,
				(u16*) pt05_gap_x_limits, rows * cols);

		/* compare to the limits */
		result = true;
		for (i = 0; i < rows; i++) {
			for (j = 0; j < cols; j++) {
				idx = i * cols + j;
				if (gap_frame_x[idx] > pt05_gap_x_limits[idx]) {
					LOGE("Fail on gap frame(%2d,%2d)=%5d, max:%4d\n",
						i, j, gap_frame_x[idx], pt05_gap_x_limits[idx]);
					result = false;
				}
			}
		}
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	if (gap_frame_x)
		syna_pal_mem_free(gap_frame_x);
	if (gap_frame_y)
		syna_pal_mem_free(gap_frame_y);
	if (frame)
		syna_pal_mem_free(frame);

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT05 (PID05) testing.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt05_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, retval_gap, i, j;
	short *data_ptr = NULL;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt05(tcm, &test_data);

	retval_gap = syna_testing_pt05_gap(tcm, &test_data);

	count += scnprintf(buf, PAGE_SIZE,
			"TEST PT$05: %s\n",
			(retval < 0) || (retval_gap < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
			tcm->tcm_dev->cols, tcm->tcm_dev->rows);

	data_ptr = (short *)&(test_data.buf[0]);
	for (i = 0; i < tcm->tcm_dev->rows; i++) {
		for (j = 0; j < tcm->tcm_dev->cols; j++) {
			count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
					data_ptr[i * tcm->tcm_dev->cols + j]);
		}
		count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	}

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt05 =
	__ATTR(pt05, 0444, syna_testing_pt05_show, NULL);


/**
 * @brief  Sample code to perform PT0A (PID10) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_pt0a(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID10_DELTA_NOISE,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID10_DELTA_NOISE);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt0a_high_limit_name,
			(u16*) pt0a_hi_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);
	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt0a_low_limit_name,
			(u16*) pt0a_lo_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);

	result = syna_testing_compare_frame(test_data->buf,
			test_data->data_length,
			tcm->tcm_dev->rows,
			tcm->tcm_dev->cols,
			(const short *)&pt0a_hi_limits[0],
			(const short *)&pt0a_lo_limits[0]);

	if (test_data->buf != NULL) {
		syna_testing_print_frame(tcm, test_data->buf, tcm->tcm_dev->rows,
				tcm->tcm_dev->cols);
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT0A (PID10) testing.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt0a_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, i, j;
	short *data_ptr = NULL;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt0a(tcm, &test_data);

	count += scnprintf(buf, PAGE_SIZE,
			"TEST PT$0A: %s\n", (retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
			tcm->tcm_dev->cols, tcm->tcm_dev->rows);

	data_ptr = (short *)&(test_data.buf[0]);
	for (i = 0; i < tcm->tcm_dev->rows; i++) {
		for (j = 0; j < tcm->tcm_dev->cols; j++) {
			count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
					data_ptr[i * tcm->tcm_dev->cols + j]);
		}
		count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	}

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt0a =
	__ATTR(pt0a, 0444, syna_testing_pt0a_show, NULL);

/**
 * @brief  Sample code to perform PT10 (PID16) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_testing_pt10(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID16_SENSOR_SPEED,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID16_SENSOR_SPEED);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt10_high_limit_name,
			(u16*) pt10_hi_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);
	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt10_low_limit_name,
			(u16*) pt10_lo_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);

	result = syna_testing_compare_frame(test_data->buf,
			test_data->data_length,
			tcm->tcm_dev->rows,
			tcm->tcm_dev->cols,
			(const short *)&pt10_hi_limits[0],
			(const short *)&pt10_lo_limits[0]);

	if (test_data->buf != NULL) {
		syna_testing_print_frame(tcm, test_data->buf, tcm->tcm_dev->rows,
				tcm->tcm_dev->cols);
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Sample code to perform PT10 (PID16) GAP testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] test_data: buffer stored the testing data
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt10_gap(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;
	int rows = tcm->tcm_dev->rows;
	int cols = tcm->tcm_dev->cols;
	short *data_ptr = NULL;
	short *frame = NULL;
	short *gap_frame_x = NULL;
	short *gap_frame_y = NULL;
	int i, j, idx;

	LOGI("%s: Start testing\n", __func__);

	if (test_data->buf == NULL) {
		LOGE("Test data is NULL\n");
		result = false;
		goto exit;
	}

	frame = syna_pal_mem_alloc(rows * cols, sizeof(short));
	if (!frame) {
		LOGE("Fail to allocate image for gap test\n");
		result = false;
		frame = NULL;
		goto exit;
	}
	gap_frame_x = syna_pal_mem_alloc(rows * cols, sizeof(short));
	if (!gap_frame_x) {
		LOGE("Fail to allocate gap image x for gap test\n");
		result = false;
		gap_frame_x = NULL;
		goto exit;
	}
	gap_frame_y = syna_pal_mem_alloc(rows * cols, sizeof(short));
	if (!gap_frame_y) {
		LOGE("Fail to allocate gap image y for gap test\n");
		result = false;
		gap_frame_y = NULL;
		goto exit;
	}

	data_ptr = (short *)&test_data->buf[0];
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			frame[i * cols + j] = *data_ptr;
			data_ptr++;
		}
	}

	if (tcm->hw_if->test_algo == 1) {
		LOGI("Gap test algo b");
		retval = syna_testing_calculate_gap_frame_b(frame,
				gap_frame_x, rows, cols, true, true);
		if (retval < 0) {
			LOGE("Fail to get the gap frame x\n");
			result = false;
			goto exit;
		}

		retval = syna_testing_calculate_gap_frame_b(frame,
				gap_frame_y, rows, cols, false, true);
		if (retval < 0) {
			LOGE("Fail to get the gap frame y\n");
			result = false;
			goto exit;
		}

		syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt10_gap_x_limit_name,
				(u16*) pt10_gap_x_limits, rows * (cols - 1));
		syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt10_gap_y_limit_name,
				(u16*) pt10_gap_y_limits, (rows - 1) * cols);

		/* compare to the limits */
		result = true;
		for (i = 0; i < rows; i++) {
			for (j = 0; j < cols - 1; j++) {
				idx = i * (cols - 1) + j;
				if (gap_frame_x[idx] > pt10_gap_x_limits[idx]) {
					LOGE("Fail on gapX (%2d,%2d)=%5d, max:%4d\n",
						j, i, gap_frame_x[idx], pt10_gap_x_limits[idx]);
					result = false;
				}
			}
		}

		for (i = 0; i < rows - 1; i++) {
			for (j = 0; j < cols; j++) {
				idx = i * cols + j;
				if (gap_frame_y[idx] > pt10_gap_y_limits[idx]) {
					LOGE("Fail on gapY (%2d,%2d)=%5d, max:%4d\n",
						j, i, gap_frame_y[idx], pt10_gap_y_limits[idx]);
					result = false;
				}
			}
		}
	} else {
		LOGI("Gap test algo a");
		retval = syna_testing_calculate_gap_frame(frame,
			 gap_frame_x, rows, cols, SENSOR_SPEED_TEST);
		if (retval < 0) {
			LOGE("Fail to get the gap frame\n");
			result = false;
			goto exit;
		}

		syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt10_gap_x_limit_name,
				(u16*) pt10_gap_x_limits, rows * cols);

		/* compare to the limits */
		result = true;
		for (i = 0; i < rows; i++) {
			for (j = 0; j < cols; j++) {
				idx = i * cols + j;
				if (gap_frame_x[idx] > pt10_gap_x_limits[idx]) {
					LOGE("Fail on gap frame(%2d,%2d)=%5d, max:%4d\n",
						i, j, gap_frame_x[idx], pt10_gap_x_limits[idx]);
					result = false;
				}
			}
		}
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	if (gap_frame_x)
		syna_pal_mem_free(gap_frame_x);
	if (gap_frame_y)
		syna_pal_mem_free(gap_frame_y);
	if (frame)
		syna_pal_mem_free(frame);

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT10 (PID16) testing.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt10_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, retval_gap, i, j;
	short *data_ptr = NULL;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt10(tcm, &test_data);

	retval_gap = syna_testing_pt10_gap(tcm, &test_data);

	count += scnprintf(buf, PAGE_SIZE,
			"TEST PT$10: %s\n",
			(retval < 0) || (retval_gap < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
			tcm->tcm_dev->cols, tcm->tcm_dev->rows);

	data_ptr = (short *)&(test_data.buf[0]);
	for (i = 0; i < tcm->tcm_dev->rows; i++) {
		for (j = 0; j < tcm->tcm_dev->cols; j++) {
			count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
					data_ptr[i * tcm->tcm_dev->cols + j]);
		}
		count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	}

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt10 =
	__ATTR(pt10, 0444, syna_testing_pt10_show, NULL);

/**
 * @brief  Sample code to perform PT11 (PID17) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] test_data: buffer stored the testing data
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt11(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID17_ADC_RANGE,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID17_ADC_RANGE);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt11_high_limit_name,
			(u16*) pt11_hi_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);
	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt11_low_limit_name,
			(u16*) pt11_lo_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);

	result = syna_testing_compare_frame(test_data->buf,
			test_data->data_length,
			tcm->tcm_dev->rows,
			tcm->tcm_dev->cols,
			(const short *)&pt11_hi_limits[0],
			(const short *)&pt11_lo_limits[0]);

	if (test_data->buf != NULL) {
		syna_testing_print_frame(tcm, test_data->buf, tcm->tcm_dev->rows,
				tcm->tcm_dev->cols);
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT11 (PID17) testing.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt11_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, i, j;
	short *data_ptr = NULL;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt11(tcm, &test_data);

	count += scnprintf(buf, PAGE_SIZE,
			"TEST PT$11: %s\n", (retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
			tcm->tcm_dev->cols, tcm->tcm_dev->rows);

	data_ptr = (short *)&(test_data.buf[0]);
	for (i = 0; i < tcm->tcm_dev->rows; i++) {
		for (j = 0; j < tcm->tcm_dev->cols; j++) {
			count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
					data_ptr[i * tcm->tcm_dev->cols + j]);
		}
		count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	}

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt11 =
	__ATTR(pt11, 0444, syna_testing_pt11_show, NULL);

/**
 * @brief  Sample code to perform PT12 (PID18) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] test_data: buffer stored the testing data
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt12(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID18_HYBRID_ABS_RAW,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID18_HYBRID_ABS_RAW);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_32(tcm, tcm->hw_if->pt12_high_limit_name,
			(u32*) pt12_hi_limits, tcm->tcm_dev->rows + tcm->tcm_dev->cols);
	syna_testing_parse_test_limit_32(tcm, tcm->hw_if->pt12_low_limit_name,
			(u32*) pt12_lo_limits, tcm->tcm_dev->rows + tcm->tcm_dev->cols);

	result = syna_testing_compare_list(test_data->buf, test_data->data_length,
					   tcm->tcm_dev->rows, tcm->tcm_dev->cols,
					   (const int *)&pt12_hi_limits[0],
					   (const int *)&pt12_lo_limits[0], false);

	if (test_data->buf != NULL) {
		syna_testing_print_list(tcm, test_data->buf, tcm->tcm_dev->rows, tcm->tcm_dev->cols,
					false);
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT12 (PID18) testing.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt12_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, i;
	unsigned int count = 0;
	int *data_ptr = NULL;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt12(tcm, &test_data);

	count = scnprintf(buf, PAGE_SIZE,
			"TEST PT$12: %s\n", (retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
			tcm->tcm_dev->cols, tcm->tcm_dev->rows);

	data_ptr = (int *)&(test_data.buf[0]);
	for (i = 0; i < tcm->tcm_dev->cols; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
				data_ptr[i]);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	for (i = 0; i < tcm->tcm_dev->rows; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
				data_ptr[tcm->tcm_dev->cols + i]);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt12 =
	__ATTR(pt12, 0444, syna_testing_pt12_show, NULL);

/**
 * @brief  Sample code to perform PT16 (PID22) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] test_data: buffer stored the testing data
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt16(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID22_TRANS_CAP_RAW,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID22_TRANS_CAP_RAW);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt16_high_limit_name,
			(u16*) pt16_hi_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);
	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt16_low_limit_name,
			(u16*) pt16_lo_limits, tcm->tcm_dev->rows * tcm->tcm_dev->cols);

	result = syna_testing_compare_frame(test_data->buf,
			test_data->data_length,
			tcm->tcm_dev->rows,
			tcm->tcm_dev->cols,
			(const short *)&pt16_hi_limits[0],
			(const short *)&pt16_lo_limits[0]);

	if (test_data->buf != NULL) {
		syna_testing_print_frame(tcm, test_data->buf, tcm->tcm_dev->rows,
				tcm->tcm_dev->cols);
	}

exit:
	LOGI("Result = %s\n", (result)?"pass":"fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT16 (PID22) testing.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt16_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, i, j;
	short *data_ptr = NULL;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt16(tcm, &test_data);

	count = scnprintf(buf, PAGE_SIZE,
			"TEST PT$16: %s\n", (retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
			tcm->tcm_dev->cols, tcm->tcm_dev->rows);

	data_ptr = (short *)&(test_data.buf[0]);
	for (i = 0; i < tcm->tcm_dev->rows; i++) {
		for (j = 0; j < tcm->tcm_dev->cols; j++) {
			count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
					data_ptr[i * tcm->tcm_dev->cols + j]);
		}
		count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	}

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt16 =
	__ATTR(pt16, 0444, syna_testing_pt16_show, NULL);

/**
 * @brief  Sample code to perform PT16 (PID22) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] test_data: buffer stored the testing data
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt1a(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("Start testing\n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev, TEST_PID26_HYBRID_ABS_W_CBC, test_data,
					      CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID26_HYBRID_ABS_W_CBC);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_32(tcm, tcm->hw_if->pt1a_low_limit_name, (u32 *)pt1a_limits,
					 tcm->tcm_dev->rows + tcm->tcm_dev->cols);

	result = syna_testing_compare_list(test_data->buf, test_data->data_length,
					   tcm->tcm_dev->rows, tcm->tcm_dev->cols, NULL,
					   (const int *)&pt1a_limits[0], true);

	if (test_data->buf != NULL) {
		syna_testing_print_list(tcm, test_data->buf, tcm->tcm_dev->rows, tcm->tcm_dev->cols,
					true);
	}

exit:
	LOGI("Result = %s\n", (result) ? "pass" : "fail");

	return ((result) ? 0 : -1);
}

/**
 * @brief  Attribute to trigger the PT1A (PID26) testing.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt1a_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int retval, i;
	unsigned short *data_ptr = NULL;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = snprintf(buf, PAGE_SIZE, "Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt1a(tcm, &test_data);

	count = snprintf(buf, PAGE_SIZE, "TEST PT$1A: %s\n", (retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n", tcm->tcm_dev->cols,
			   tcm->tcm_dev->rows);

	data_ptr = (unsigned short *)&(test_data.buf[0]);
	for (i = 0; i < tcm->tcm_dev->cols; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%d ", data_ptr[i]);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
	for (i = 0; i < tcm->tcm_dev->rows; i++) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
				   data_ptr[tcm->tcm_dev->cols + i]);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt1a = __ATTR(pt1a, 0444, syna_testing_pt1a_show, NULL);

/**
 * @brief  Sample code to perform tags Moisture (RID30) testing.
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [ in] test_data: buffer stored the testing data
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt_tag_moisture(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;
	short *data_ptr = NULL;
	short limit;
	int i, j, rows, cols;
	const unsigned int REPORT_TAG_MOISTURE = 30;

	rows = tcm->tcm_dev->rows;
	cols = tcm->tcm_dev->cols;

	LOGI("Start testing\n");

	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 1, CMD_RESPONSE_IN_ATTN);

	/* disable irq to avoid the testing data being grabbed by the interrupt context */
	syna_tcm_enable_irq(tcm->tcm_dev, false);

	retval = syna_tcm_enable_report(tcm->tcm_dev, REPORT_TAG_MOISTURE, true,
			tcm->tcm_dev->msg_data.command_polling_time);
	if (retval < 0) {
		LOGE("Fail to enable RID%d\n", REPORT_TAG_MOISTURE);
		result = false;
		goto exit;
	}

	retval = syna_tcm_wait_for_report(tcm->tcm_dev, REPORT_TAG_MOISTURE, test_data, 5, 500);
	if ((retval < 0) || (test_data->data_length == 0)) {
		LOGE("Fail to get a frame of RID%d to test\n", REPORT_TAG_MOISTURE);
		result = false;
		goto exit;
	}

	if (test_data->data_length % (rows * cols) != 0) {
		LOGE("Invalid frame size of RID%d, %d\n", REPORT_TAG_MOISTURE, test_data->data_length);
		result = false;
		goto exit;
	}

	retval = syna_tcm_enable_report(tcm->tcm_dev, REPORT_TAG_MOISTURE, false,
			tcm->tcm_dev->msg_data.command_polling_time);
	if (retval < 0) {
		LOGE("Fail to disable RID%d\n", REPORT_TAG_MOISTURE);
		result = false;
		goto exit;
	}

	syna_testing_parse_test_limit_16(tcm, tcm->hw_if->pt_tag_moisture_limit_name,
			(u16*) pt_moisture_limits, rows * cols);

	/* compare to the limits */
	result = true;
	data_ptr = (short *)&test_data->buf[0];
	for (i = 0; i < rows; i++) {
		for (j = 0; j < cols; j++) {
			if (*data_ptr < 0)
				continue;

			limit = pt_moisture_limits[i * cols + j];
			if (*data_ptr > limit) {
				LOGE("Fail on (%2d,%2d)=%5d, limits_hi:%4d\n",
					i, j, *data_ptr, limit);
				result = false;
			}
			data_ptr++;
		}
	}

	if (test_data->buf != NULL) {
		syna_testing_print_frame(tcm, test_data->buf, tcm->tcm_dev->rows,
				tcm->tcm_dev->cols);
	}

exit:
	syna_tcm_enable_irq(tcm->tcm_dev, true);

	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 0, CMD_RESPONSE_IN_ATTN);

	LOGI("Result = %s\n", (result)?"pass":"fail");


	return ((result) ? 0 : -1);
}
/**
 * @brief  Attribute to trigger the Tags Moisture (RID30) testing.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_testing_pt_moisture_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval, i, j;
	short *data_ptr = NULL;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt_tag_moisture(tcm, &test_data);

	count += scnprintf(buf, PAGE_SIZE,
			"TEST Tags Moisture: %s\n",
			(retval < 0) ? "fail" : "pass");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "%d %d\n",
			tcm->tcm_dev->cols, tcm->tcm_dev->rows);

	if (test_data.buf) {
		data_ptr = (short *)&(test_data.buf[0]);
		for (i = 0; i < tcm->tcm_dev->rows; i++) {
			for (j = 0; j < tcm->tcm_dev->cols; j++) {
				count += scnprintf(buf + count, PAGE_SIZE - count, "%d ",
						data_ptr[i * tcm->tcm_dev->cols + j]);
			}
			count += scnprintf(buf + count, PAGE_SIZE - count, "\n");
		}
	}

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt_tag_moisture =
	__ATTR(pt_moisture, 0444, syna_testing_pt_moisture_show, NULL);

/*
 * syna_testing_pt_serial_number()
 *
 * Sample code to perform PT Serial Number testing
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    on success, 0; otherwise, negative value on error.
 */
static int syna_testing_pt_serial_number(struct syna_tcm *tcm, struct tcm_buffer *test_data)
{
	int retval;
	bool result = false;

	LOGI("%s: Start testing\n", __func__);

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID92_SERIAL_NUMBER,
			test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to run test %d\n", TEST_PID92_SERIAL_NUMBER);
		result = false;
	}

	return ((result) ? 0 : -1);
}

/*
 * syna_testing_pt_serial_number_show()
 *
 * Attribute to show the result of serial number test to the console.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    on success, number of characters being output;
 *    otherwise, negative value on error.
 */
static ssize_t syna_testing_pt_serial_number_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;
	struct tcm_serial_number_info sn_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	retval = syna_testing_pt_serial_number(tcm, &test_data);

	count = scnprintf(buf, PAGE_SIZE, "TEST PT Serial Number: \n");

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	memcpy(&sn_data, test_data.buf, sizeof(sn_data));
	syna_print_serial_number(tcm, &sn_data);

	count += scnprintf(buf + count, PAGE_SIZE - count, "wafer lot: ");
	count += scnprintf(buf + count, PAGE_SIZE - count, "%*ph\n",
			(int) sizeof(sn_data.wafer_lot), sn_data.wafer_lot);
	count += scnprintf(buf + count, PAGE_SIZE - count, "x coordinate: ");
	count += scnprintf(buf + count, PAGE_SIZE - count, "%*ph\n",
			(int) sizeof(sn_data.x_coordinate), sn_data.x_coordinate);
	count += scnprintf(buf + count, PAGE_SIZE - count, "y coordinate: ");
	count += scnprintf(buf + count, PAGE_SIZE - count, "%*ph\n",
			(int) sizeof(sn_data.y_coordinate), sn_data.y_coordinate);
	count += scnprintf(buf + count, PAGE_SIZE - count,
			"{date[4] + wafer id[2]&no[2]} or {wafer id[4] + date[4]}: ");
	count += scnprintf(buf + count, PAGE_SIZE - count, "%*ph\n",
			(int) sizeof(sn_data.date_wafer_code), sn_data.date_wafer_code);

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}

static struct kobj_attribute kobj_attr_pt_serial_number =
	__ATTR(pt_serial_number, 0444, syna_testing_pt_serial_number_show, NULL);

/*
 * syna_testing_pt_sync_frequency_show()
 *
 * Attribute to show the result of (H/V)sync frequency test to the console.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    on success, number of characters being output;
 *    otherwise, negative value on error.
 */
static ssize_t syna_testing_pt_sync_frequency_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval;
	unsigned int count = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_buffer test_data;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm->is_connected) {
		count = scnprintf(buf, PAGE_SIZE,
				"Device is NOT connected\n");
		goto exit;
	}

	syna_tcm_buf_init(&test_data);

	count = scnprintf(buf, PAGE_SIZE, "TEST PT Sync Frequency: \n");

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID74_VSYNC,
			&test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0)
		goto free_tcm_buffer;

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "Vsync: ");
	count += scnprintf(buf + count, PAGE_SIZE - count, "%dHz\n",
			(u16)(test_data.buf[1] << 8 | test_data.buf[0]));

	retval = syna_tcm_run_production_test(tcm->tcm_dev,
			TEST_PID75_HSYNC,
			&test_data,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0)
		goto free_tcm_buffer;

	if (test_data.buf == NULL)
		goto free_tcm_buffer;

	count += scnprintf(buf + count, PAGE_SIZE - count, "Hsync: ");
	count += scnprintf(buf + count, PAGE_SIZE - count, "%dHz\n",
			(u16)(test_data.buf[1] << 8 | test_data.buf[0]));

free_tcm_buffer:
	syna_tcm_buf_release(&test_data);
exit:
	return count;
}
static struct kobj_attribute kobj_attr_pt_sync_frequency =
	__ATTR(pt_sync_frequency, 0444, syna_testing_pt_sync_frequency_show, NULL);

static ssize_t syna_testing_test_setup_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	unsigned char input;
	struct device *p_dev;
	struct syna_tcm *tcm;
	bool test_setup = false;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtou8(buf, 16, &input))
		return -EINVAL;

	if (!tcm->is_connected) {
		LOGW("Device is NOT connected\n");
		retval = count;
		goto exit;
	}

	switch (input) {
	case 0x00:
		test_setup = false;
		break;
	case 0x01:
		test_setup = true;
		break;
	default:
		LOGE("Invalid input %#x.\n", input);
		retval = -EINVAL;
		goto exit;
	}

	LOGI("Test setup %s.", test_setup ? "enable" : "disable");

	if (test_setup) {
		retval = syna_tcm_enable_report(tcm->tcm_dev,
			REPORT_FW_STATUS, false, CMD_RESPONSE_IN_ATTN);
	} else {
		retval = syna_tcm_enable_report(tcm->tcm_dev,
			REPORT_FW_STATUS, true, CMD_RESPONSE_IN_ATTN);
	}

	if (retval < 0) {
		LOGE("Test setup %s failed.",
			test_setup ? "enable" : "disable");
		goto exit;
	}

	retval = count;

exit:
	return retval;
}

static struct kobj_attribute kobj_attr_test_setup =
	__ATTR(test_setup, 0220, NULL, syna_testing_test_setup_store);

 /* Definitions of sysfs attributes for testing */
static struct attribute *attrs[] = {
	&kobj_attr_check_id.attr,
	&kobj_attr_pt01.attr,
	&kobj_attr_pt5b.attr,
	&kobj_attr_pt05.attr,
	&kobj_attr_pt0a.attr,
	&kobj_attr_pt10.attr,
	&kobj_attr_pt11.attr,
	&kobj_attr_pt12.attr,
	&kobj_attr_pt16.attr,
	&kobj_attr_pt1a.attr,
	&kobj_attr_pt_tag_moisture.attr,
	&kobj_attr_pt_serial_number.attr,
	&kobj_attr_pt_sync_frequency.attr,
	&kobj_attr_test_setup.attr,
	NULL,
};

static struct attribute_group attr_testing_group = {
	.attrs = attrs,
};

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
static int syna_selftest(void *private_data, struct gti_selftest_cmd *cmd)
{
	int retval;
	struct tcm_buffer test_data;
	struct syna_tcm *tcm = private_data;
	char temp_limit_name[LIMIT_NAME_LEN];
	char pt05_high_limit_name_restore[LIMIT_NAME_LEN];
	char pt05_low_limit_name_restore[LIMIT_NAME_LEN];
	char pt05_gap_x_limit_name_restore[LIMIT_NAME_LEN];
	char pt05_gap_y_limit_name_restore[LIMIT_NAME_LEN];
	char pt12_high_limit_name_restore[LIMIT_NAME_LEN];
	char pt12_low_limit_name_restore[LIMIT_NAME_LEN];
	char pt1a_low_limit_name_restore[LIMIT_NAME_LEN];
	struct syna_hw_interface *hw_if = tcm->hw_if;
	struct property *prop;
	struct device_node *np = tcm->pdev->dev.parent->of_node;

	syna_tcm_buf_init(&test_data);

	strncpy(pt05_high_limit_name_restore, hw_if->pt05_high_limit_name,
			sizeof(pt05_high_limit_name_restore));
	strncpy(pt05_low_limit_name_restore, hw_if->pt05_low_limit_name,
			sizeof(pt05_low_limit_name_restore));
	strncpy(pt05_gap_x_limit_name_restore, hw_if->pt05_gap_x_limit_name,
			sizeof(pt05_gap_x_limit_name_restore));
	strncpy(pt05_gap_y_limit_name_restore, hw_if->pt05_gap_y_limit_name,
			sizeof(pt05_gap_y_limit_name_restore));
	strncpy(pt12_high_limit_name_restore, hw_if->pt12_high_limit_name,
			sizeof(pt12_high_limit_name_restore));
	strncpy(pt12_low_limit_name_restore, hw_if->pt12_low_limit_name,
			sizeof(pt12_low_limit_name_restore));
	strncpy(pt1a_low_limit_name_restore, hw_if->pt1a_low_limit_name,
		sizeof(pt1a_low_limit_name_restore));

	if (cmd->is_ical) {
		/* Parse ical specific test limit. */
		strncpy(temp_limit_name, hw_if->pt05_high_limit_name, sizeof(temp_limit_name));
		strcat(temp_limit_name, "_ical");
		prop = of_find_property(np, temp_limit_name, NULL);
		if (prop) {
			strncpy(hw_if->pt05_high_limit_name, temp_limit_name,
					sizeof(hw_if->pt05_high_limit_name));
		}

		strncpy(temp_limit_name, hw_if->pt05_low_limit_name, sizeof(temp_limit_name));
		strcat(temp_limit_name, "_ical");
		prop = of_find_property(np, temp_limit_name, NULL);
		if (prop) {
			strncpy(hw_if->pt05_low_limit_name, temp_limit_name,
					sizeof(hw_if->pt05_low_limit_name));
		}

		strncpy(temp_limit_name, hw_if->pt05_gap_x_limit_name, sizeof(temp_limit_name));
		strcat(temp_limit_name, "_ical");
		prop = of_find_property(np, temp_limit_name, NULL);
		if (prop) {
			strncpy(hw_if->pt05_gap_x_limit_name, temp_limit_name,
					sizeof(hw_if->pt05_gap_x_limit_name));
		}

		strncpy(temp_limit_name, hw_if->pt05_gap_y_limit_name, sizeof(temp_limit_name));
		strcat(temp_limit_name, "_ical");
		prop = of_find_property(np, temp_limit_name, NULL);
		if (prop) {
			strncpy(hw_if->pt05_gap_y_limit_name, temp_limit_name,
					sizeof(hw_if->pt05_gap_y_limit_name));
		}

		strncpy(temp_limit_name, hw_if->pt12_high_limit_name, sizeof(temp_limit_name));
		strcat(temp_limit_name, "_ical");
		prop = of_find_property(np, temp_limit_name, NULL);
		if (prop) {
			strncpy(hw_if->pt12_high_limit_name, temp_limit_name,
					sizeof(hw_if->pt12_high_limit_name));
		}

		strncpy(temp_limit_name, hw_if->pt12_low_limit_name, sizeof(temp_limit_name));
		strcat(temp_limit_name, "_ical");
		prop = of_find_property(np, temp_limit_name, NULL);
		if (prop) {
			strncpy(hw_if->pt12_low_limit_name, temp_limit_name,
					sizeof(hw_if->pt12_low_limit_name));
		}

		strncpy(temp_limit_name, hw_if->pt1a_low_limit_name, sizeof(temp_limit_name));
		strcat(temp_limit_name, "_ical");
		prop = of_find_property(np, temp_limit_name, NULL);
		if (prop) {
			strncpy(hw_if->pt1a_low_limit_name, temp_limit_name,
				sizeof(hw_if->pt1a_low_limit_name));
		}
	}

	/* TRX-TRX shorts */
	retval = syna_testing_pt01(tcm, &test_data);
	/* Full raw capacitance & Full raw capacitance gap*/
	retval |= syna_testing_pt05(tcm, &test_data);
	retval |= syna_testing_pt05_gap(tcm, &test_data);
	/* Abs Raw Cap TX/RX */
	retval |= syna_testing_pt12(tcm, &test_data);

	cmd->result = retval ? GTI_SELFTEST_RESULT_FAIL : GTI_SELFTEST_RESULT_PASS;
	retval = 0;

	/* Restore the test limit. */
	strncpy(hw_if->pt05_high_limit_name, pt05_high_limit_name_restore,
			sizeof(hw_if->pt05_high_limit_name));
	strncpy(hw_if->pt05_low_limit_name, pt05_low_limit_name_restore,
			sizeof(hw_if->pt05_low_limit_name));
	strncpy(hw_if->pt05_gap_x_limit_name, pt05_gap_x_limit_name_restore,
			sizeof(hw_if->pt05_gap_x_limit_name));
	strncpy(hw_if->pt05_gap_y_limit_name, pt05_gap_y_limit_name_restore,
			sizeof(hw_if->pt05_gap_y_limit_name));
	strncpy(hw_if->pt12_high_limit_name, pt12_high_limit_name_restore,
			sizeof(hw_if->pt12_high_limit_name));
	strncpy(hw_if->pt12_low_limit_name, pt12_low_limit_name_restore,
			sizeof(hw_if->pt12_low_limit_name));
	strncpy(hw_if->pt1a_low_limit_name, pt1a_low_limit_name_restore,
		sizeof(hw_if->pt1a_low_limit_name));

	syna_tcm_buf_release(&test_data);

	return retval;
}
#endif

/**
 * @brief  Register sysfs attributes for testing.
 *
 * @param
 *    [ in] tcm:  the driver handle
 *    [ in] sysfs_dir: directory of sysfs attributes
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_testing_register_attributes(struct syna_tcm *tcm, struct kobject *sysfs_dir)
{
	int retval;

	retval = sysfs_create_group(sysfs_dir, &attr_testing_group);
	if (retval < 0) {
		LOGE("Fail to create sysfs group for testing\n");
		return retval;
	}


#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	tcm->selftest = syna_selftest;
#endif

	return 0;
}
/**
 * @brief  Remove a directory allocated previously.
 *
 * @param
 *    [ in] sysfs_dir: directory of sysfs attributes
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_testing_remove_attributes(struct kobject *sysfs_dir)
{
	sysfs_remove_group(sysfs_dir, &attr_testing_group);

	return 0;
}
