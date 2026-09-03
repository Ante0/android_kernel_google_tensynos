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
 * @file syna_tcm2_sysfs.c
 *
 * This file implements sysfs attributes in the reference driver.
 */

#include <linux/string.h>

#include "syna_tcm2.h"
#include "synaptics_touchcom_core_dev.h"
#include "synaptics_touchcom_func_base.h"
#ifdef HAS_TESTING_FEATURE
#include "syna_tcm2_testing.h"
#endif
#ifdef HAS_TDDI_REFLASH_FEATURE
#include "synaptics_touchcom_func_reflash_tddi.h"
#endif
#ifdef HAS_REFLASH_FEATURE
#include "synaptics_touchcom_func_reflash.h"
#endif
#include "syna_tcm2_sysfs.h"


#define SYSFS_ROOT_DIR "sysfs"
#define SYSFS_SUB_DIR "utility"


/**
 * @brief  Debugging attribute to set int2.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static ssize_t syna_sysfs_int2_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	u16 config;
	unsigned int input;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	if (!tcm->is_connected) {
		LOGW("Device is NOT connected\n");
		return count;
	}

	/* Set int2 production mode disabled. */
	if (input == 0) {
		config = INT2_PRODUCTION_DISABLE;
		LOGI("Set INT2 production mode disabled");
	} else if (input == 1) {
	/*  Set int2 as high. */
		config = INT2_PRODUCTION_HIGH;
		LOGI("Set INT2 production mode high");
	} else if (input == 3) {
	/*  Set int2 as low. */
		config = INT2_PRODUCTION_LOW;
		LOGI("Set INT2 production mode low");
	} else {
		LOGE("Unknown option.");
		goto exit;
	}

	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_INT2_PRODUCTION_CMD,
		config, CMD_RESPONSE_IN_ATTN);

exit:
	retval = count;
	return retval;
}
/**
 * @brief  Debugging attribute to show the int2 status.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static ssize_t syna_sysfs_int2_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int retval = 0;
	u16 config;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_INT2_PRODUCTION_CMD,
			&config, tcm->tcm_dev->msg_data.command_polling_time);

	if (retval < 0) {
		retval = scnprintf(buf, PAGE_SIZE, "Read failure.\n");
	} else {
		if (config == INT2_PRODUCTION_DISABLE)
			retval = scnprintf(buf, PAGE_SIZE, "Disabled\n");
		else if (config == INT2_PRODUCTION_HIGH)
			retval = scnprintf(buf, PAGE_SIZE, "High\n");
		else if (config == INT2_PRODUCTION_LOW)
			retval = scnprintf(buf, PAGE_SIZE, "Low\n");
		else
			retval = scnprintf(buf, PAGE_SIZE, "Unknown value %u\n", config);
	}

	return retval;
}

static struct kobj_attribute kobj_attr_int2 =
	__ATTR(int2, 0644, syna_sysfs_int2_show, syna_sysfs_int2_store);

/**
 * @brief  Debugging attribute to issue a reset.
 *         Parameters
 *            1: for a sw reset
 *            2: for a hardware reset
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static ssize_t syna_sysfs_reset_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	unsigned int input;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct syna_hw_attn_data *attn;
	unsigned char code;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	attn = &tcm->hw_if->bdata_attn;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	if (!tcm->is_connected) {
		LOGW("Device is NOT connected\n");
		return count;
	}

	if ((tcm->pwr_state == BARE_MODE) || (input == 2)) {
		if (!tcm->hw_if->ops_hw_reset) {
			LOGE("No hardware reset support\n");
			goto exit;
		}
		tcm->hw_if->ops_hw_reset(tcm->hw_if);
		/* manually read in the event after reset if attn is disabled */
		if (!attn->irq_enabled)
			syna_tcm_get_event_data(tcm->tcm_dev, &code, NULL);

	} else if (input == 1) {
		retval = syna_tcm_reset(tcm->tcm_dev,
			tcm->tcm_dev->msg_data.command_polling_time);
		if (retval < 0) {
			LOGE("Fail to do reset\n");
			goto exit;
		}
	} else {
		LOGW("Unknown option %d (1:sw / 2:hw)\n", input);
		retval = -EINVAL;
		goto exit;
	}

	/* check the fw setup in case the settings is changed */
	if (IS_APP_FW_MODE(tcm->tcm_dev->dev_mode)) {
		retval = tcm->dev_set_up_app_fw(tcm);
		if (retval < 0) {
			LOGE("Fail to set up app fw\n");
			goto exit;
		}
	}

	retval = count;

exit:
	return retval;
}

static struct kobj_attribute kobj_attr_reset =
	__ATTR(reset, 0220, NULL, syna_sysfs_reset_store);

/**
 * @brief  Debugging attribute to disable/enable the irq
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static ssize_t syna_sysfs_irq_en_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	unsigned int input;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_hw_platform *hw;

	p_dev = container_of(kobj->parent->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);
	hw = &tcm->hw_if->hw_platform;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	if (!hw || !hw->ops_enable_attn)
		return 0;

	if (!tcm->is_connected) {
		LOGW("Device is NOT connected\n");
		return count;
	}

	if (tcm->pwr_state == BARE_MODE) {
		LOGN("In bare connection mode, no irq support\n");
		retval = count;
		goto exit;
	}

	/* disable the interrupt line */
	if (input == 0) {
		retval = hw->ops_enable_attn(hw, false);
		if (retval < 0) {
			LOGE("Fail to disable interrupt\n");
			goto exit;
		}
	} else if (input == 1) {
	/* enable the interrupt line */
		retval = hw->ops_enable_attn(hw, true);
		if (retval < 0) {
			LOGE("Fail to enable interrupt\n");
			goto exit;
		}
	} else {
		LOGW("Unknown option %d (0:disable / 1:enable)\n", input);
		retval = -EINVAL;
		goto exit;
	}

	retval = count;

exit:
	return retval;
}

static struct kobj_attribute kobj_attr_irq_en =
	__ATTR(irq_en, 0220, NULL, syna_sysfs_irq_en_store);


/* Definitions of debugging sysfs attributes */
static struct attribute *attrs_debug[] = {
	&kobj_attr_reset.attr,
	&kobj_attr_irq_en.attr,
	NULL,
};

static struct attribute_group attr_debug_group = {
	.attrs = attrs_debug,
};

/**
 * @brief  Attribute to enable/disable the debugging attributes.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static ssize_t syna_sysfs_debug_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int input;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	if ((input == 1) && (!tcm->sysfs_dir_utility)) {
		tcm->sysfs_debug = input;
		tcm->sysfs_dir_utility = kobject_create_and_add(SYSFS_SUB_DIR, tcm->sysfs_dir);
		if (!tcm->sysfs_dir_utility) {
			LOGE("Fail to create sysfs sub directory for debugging\n");
			return -ENOTDIR;
		}

		if (sysfs_create_group(tcm->sysfs_dir_utility, &attr_debug_group) < 0) {
			LOGE("Fail to create sysfs debug group\n");
			kobject_put(tcm->sysfs_dir_utility);
			return -ENOTDIR;
		}
	} else if (input == 0) {
		tcm->sysfs_debug = input;
		if (tcm->sysfs_dir_utility) {
			sysfs_remove_group(tcm->sysfs_dir_utility, &attr_debug_group);
			kobject_put(tcm->sysfs_dir_utility);
		}
	} else {
		LOGW("Unknown option %d (0:disable / 1:enable)\n", input);
		return -EINVAL;
	}

	return count;
}

static struct kobj_attribute kobj_attr_debug =
	__ATTR(debug, 0220, NULL, syna_sysfs_debug_store);

/**
 * @brief  Attribute to show the device and driver information to the console.
 *
 * @param
 *    [ in] kobj:  handle of kernel object
 *    [ in] attr:  handle of kernel attribute
 *    [out] buf:  string buffer shown on console
 *
 * @return
 *    string output in case of success, a negative value otherwise.
 */
static ssize_t syna_sysfs_info_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	int retval = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;


	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (!tcm || !tcm->tcm_dev)
		return -ENODEV;

	retval = syna_tcm_identify(tcm->tcm_dev, &tcm->tcm_dev->id_info,
		tcm->tcm_dev->msg_data.command_polling_time);
	if (retval < 0) {
		LOGE("Fail to get identification\n");
		return retval;
	}

	/* collect app info containing most of sensor information */
	retval = syna_tcm_get_app_info(tcm->tcm_dev, &tcm->tcm_dev->app_info,
		tcm->tcm_dev->msg_data.command_polling_time);
	if (retval < 0) {
		LOGE("Fail to get application info\n");
		return retval;
	}

	return syna_get_fw_info(tcm, buf, PAGE_SIZE);
}

static struct kobj_attribute kobj_attr_info =
	__ATTR(info, 0444, syna_sysfs_info_show, NULL);

/**
 * syna_sysfs_scan_mode_store()
 *
 * Attribute to set different scan mode.
 * 0 - Lock Normal Mode Active Mode.
 * 1 - Lock Normal Mode Doze Mode.
 * 2 - Lock Low Power Gesture Mode Active Mode.
 * 3 - Lock Low Power Gesture Mode Doze Mode.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    on success, return count; otherwise, return error code
 */
static ssize_t syna_sysfs_scan_mode_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	unsigned int input;
	unsigned char command = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct syna_hw_interface *hw_if;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);
	hw_if = tcm->hw_if;

	if (kstrtouint(buf, 10, &input))
		return -EINVAL;

	if (!tcm->is_connected) {
		LOGW("Device is NOT connected\n");
		retval = count;
		goto exit;
	}

	if (hw_if->ops_hw_reset) {
		hw_if->ops_hw_reset(hw_if);
	} else {
		retval = syna_tcm_reset(tcm->tcm_dev,
			tcm->tcm_dev->msg_data.command_polling_time);
		if (retval < 0) {
			LOGE("Fail to do reset\n");
			goto exit;
		}
	}

	if (input == 0 || input == 2) {
		command = DC_DISABLE_DOZE;
	} else if (input == 1 || input == 3) {
		command = DC_FORCE_DOZE_MODE;
	} else {
		LOGW("Un-support command %u\n", input);
		goto exit;
	}

	if (input == 2 || input == 3) {
		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_ENABLE_WAKEUP_GESTURE_MODE,
				1,
				tcm->tcm_dev->msg_data.command_polling_time);
		if (retval < 0) {
			LOGE("Fail to enable wakeup gesture via DC command\n");
			goto exit;
		}
	}

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
			command,
			1,
			tcm->tcm_dev->msg_data.command_polling_time);
	if (retval < 0) {
		LOGE("Fail to set DC command %d\n", command);
		goto exit;
	}

	retval = count;

exit:
	return retval;
}

static struct kobj_attribute kobj_attr_scan_mode =
	__ATTR(scan_mode, 0220, NULL, syna_sysfs_scan_mode_store);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
/**
 * syna_sysfs_force_active_store()
 *
 * Attribute to set different scan mode.
 * 0x10 - Set SYNA_BUS_REF_FORCE_ACTIVE bit 0.
 * 0x11 - Set SYNA_BUS_REF_FORCE_ACTIVE bit 1.
 * 0x20 - Set SYNA_BUS_REF_BUGREPORT bit 0.
 * 0x21 - Set SYNA_BUS_REF_BUGREPORT bit 1.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    on success, return count; otherwise, return error code
 */
static ssize_t syna_sysfs_force_active_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = 0;
	unsigned char input;
	struct device *p_dev;
	struct syna_tcm *tcm;
	bool active;
	u32 ref = 0;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtou8(buf, 16, &input))
		return -EINVAL;

	if (!tcm->is_connected) {
		LOGW("Device is NOT connected\n");
		retval = count;
		goto exit;
	}

	switch (input) {
	case 0x10:
		ref = GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE;
		active = false;
		break;
	case 0x11:
		ref = GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE;
		active = true;
		break;
	case 0x20:
		ref = GTI_PM_WAKELOCK_TYPE_BUGREPORT;
		active = false;
		break;
	case 0x21:
		ref = GTI_PM_WAKELOCK_TYPE_BUGREPORT;
		active = true;
		break;
	default:
		LOGE("Invalid input %#x.\n", input);
		retval = -EINVAL;
		goto exit;
	}

	LOGI("Set pm wake bit %#x %s.", ref,
	     active ? "enable" : "disable");

	if (active)
		retval = goog_pm_wake_lock(tcm->gti, ref, false);
	else
		retval = goog_pm_wake_unlock_nosync(tcm->gti, ref);

	if (retval < 0) {
		LOGE("Set pm wake bit %#x %s failed.", ref,
				active ? "enable" : "disable");
		goto exit;
	}

	retval = count;

exit:
	return retval;
}

static struct kobj_attribute kobj_attr_force_active =
	__ATTR(force_active, 0220, NULL, syna_sysfs_force_active_store);
#endif

/**
 * syna_sysfs_get_raw_data_read()
 *
 * Attribute to show the raw data.
 *
 * @param
 *    [ in] fp:     file pointer
 *    [ in] kobj:   an instance of kobj
 *    [ in] battr:  an instance of bin attribute structure
 *    [ in] buf:    string buffer input
 *    [ in] offset: starting offset
 *    [ in] count:  size of buffer input
 *
 * @return
 *    on success, number of characters being output;
 *    otherwise, negative value on error.
 */
#define STR_BUF_LENGTH (PAGE_SIZE * 2)
static char syna_str_buf[STR_BUF_LENGTH];
static loff_t syna_output_str_length;
static ssize_t syna_sysfs_get_raw_data_read(struct file *fp, struct kobject *kobj,
		struct bin_attribute *battr, char *buf,
		loff_t offset, size_t count)
{
	u64 index = 0;
	struct device *p_dev;
	struct syna_tcm *tcm;
	struct tcm_dev *tcm_dev;
	int i, j, mutual_length;
	bool is_signed;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);
	tcm_dev = tcm->tcm_dev;
	mutual_length = tcm_dev->cols * tcm_dev->rows;
	is_signed = (tcm->raw_data_report_code == REPORT_DELTA);

	if (offset > 0)
		goto output_string;

	syna_output_str_length = 0;

	if (wait_for_completion_timeout(&tcm->raw_data_completion,
					msecs_to_jiffies(500)) == 0) {
		complete_all(&tcm->raw_data_completion);
		index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index, "Timeout\n");
		goto skip_data;
	}

	if (!tcm->raw_data_buffer) {
		index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index,
				   "Raw data buffer is NULL.\n");
		goto skip_data;
	}

	syna_pal_mutex_lock(&tcm->raw_data_mutex);
	/* Mutual raw. */
	index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index, "Mutual\n");
	for (i = 0; i < tcm_dev->rows; i++) {
		for (j = 0; j < tcm_dev->cols; j++) {
			index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index,
				(is_signed) ? "%d " : "%u ",
				(is_signed) ? tcm->raw_data_buffer[i * tcm_dev->cols + j] :
					      (u16) (tcm->raw_data_buffer[i * tcm_dev->cols + j]));
		}
		index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index, "\n");
	}

	/* Self raw. */
	index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index, "Self\n");
	for (i = 0; i < tcm_dev->cols; i++) {
		index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index,
			(is_signed) ? "%d " : "%u ",
			(is_signed) ? tcm->raw_data_buffer[mutual_length + i] :
				      (u16) (tcm->raw_data_buffer[mutual_length + i]));
	}
	index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index, "\n");

	for (j = 0; j < tcm_dev->rows; j++) {
		index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index,
			(is_signed) ? "%d " : "%u ",
			(is_signed) ? tcm->raw_data_buffer[mutual_length + i + j] :
				      (u16) (tcm->raw_data_buffer[mutual_length + i + j]));
	}
	index += scnprintf(syna_str_buf + index, STR_BUF_LENGTH - index, "\n");

	syna_pal_mutex_unlock(&tcm->raw_data_mutex);

	LOGI("Got raw data, report code %#x\n", tcm->raw_data_report_code);

skip_data:
	syna_output_str_length = index;

output_string:
	if (syna_output_str_length > PAGE_SIZE && count >= PAGE_SIZE)
		index = PAGE_SIZE;
	else
		index = min((u64) count, (u64) syna_output_str_length);

	LOGI("remaining length: %lld, offset: %lld.\n", syna_output_str_length, offset);

	memcpy(buf, syna_str_buf + offset, index);

	syna_output_str_length -= index;

	if (offset == 0) {
		syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 0,
				CMD_RESPONSE_IN_ATTN);
		syna_tcm_enable_report(tcm_dev, tcm->raw_data_report_code, false,
				CMD_RESPONSE_IN_ATTN);
	}
	return index;
}

/**
 * syna_sysfs_get_raw_data_write()
 *
 * Attribute to enable the raw data report type.
 *
 * @param
 *    [ in] fp:     file pointer
 *    [ in] kobj:   an instance of kobj
 *    [ in] battr:  an instance of bin attribute structure
 *    [ in] buf:    string buffer input
 *    [ in] offset: starting offset
 *    [ in] count:  size of buffer input
 *
 * @return
 *    on success, return count; otherwise, return error code
 */
static ssize_t syna_sysfs_get_raw_data_write(struct file *fp, struct kobject *kobj,
		struct bin_attribute *battr, char *buf,
		loff_t offset, size_t count)
{
	int retval = count;
	unsigned char input;
	unsigned char report_code;
	struct device *p_dev;
	struct syna_tcm *tcm;
	const unsigned char REPORT_BASELINE = 0x14;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (offset != 0)
		return count;

	if (kstrtou8(buf, 16, &input))
		return -EINVAL;

	switch (input) {
	case REPORT_DELTA:
		report_code = REPORT_DELTA;
		break;
	case REPORT_RAW:
		report_code = REPORT_RAW;
		break;
	case REPORT_BASELINE:
		report_code = REPORT_BASELINE;
		break;
	default:
		LOGE("Invalid input %#x.\n", input);
		retval = -EINVAL;
		goto exit;
	}

	LOGI("Enable raw data, report code %#x\n", report_code);

	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 1, CMD_RESPONSE_IN_ATTN);

	tcm->raw_data_report_code = report_code;
	syna_tcm_enable_report(tcm->tcm_dev, report_code, true, CMD_RESPONSE_IN_ATTN);
	reinit_completion(&tcm->raw_data_completion);

exit:
	return retval;
}

static struct bin_attribute bin_attr_get_raw_data =
	__BIN_ATTR(get_raw_data, 0644, syna_sysfs_get_raw_data_read,
	syna_sysfs_get_raw_data_write, 0);

/**
 * syna_sysfs_high_sensitivity_show()
 *
 * Attribute to show current sensitivity mode.
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
static ssize_t syna_sysfs_high_sensitivity_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", tcm->high_sensitivity_mode);
}

/**
 * syna_sysfs_high_sensitivity_store()
 *
 * Attribute to set high sensitivity mode.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    on success, return count; otherwise, return error code
 */
static ssize_t syna_sysfs_high_sensitivity_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = count;
	bool input;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtobool(buf, &input)) {
		LOGE("Invalid input %s", buf);
		return -EINVAL;
	}

	tcm->high_sensitivity_mode = input;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	tcm->gti->cmd.screen_protector_mode_cmd.setting =
		input ? GTI_SCREEN_PROTECTOR_MODE_ENABLE : GTI_SCREEN_PROTECTOR_MODE_DISABLE;
#endif

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_HIGH_SENSITIVITY_MODE,
				input,
				CMD_RESPONSE_IN_ATTN);

	LOGI("%s high sensitivity mode.\n",
	     tcm->high_sensitivity_mode ? "Enable" : "Disable");

	retval = count;

	return retval;
}

static struct kobj_attribute kobj_attr_high_sensitivity =
	__ATTR(high_sensitivity, 0644, syna_sysfs_high_sensitivity_show,
	       syna_sysfs_high_sensitivity_store);

/**
 * syna_sysfs_fw_grip_show()
 *
 * Attribute to show current grip suppression mode.
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
static ssize_t syna_sysfs_fw_grip_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);
	return scnprintf(buf, PAGE_SIZE, "%u\n", tcm->enable_fw_grip);
}

/**
 * syna_sysfs_fw_grip_store()
 *
 * Attribute to set grip suppression mode.
 * 0 - Disable fw grip suppression.
 * 1 - Enable fw grip suppression.
 * 2 - Force disable fw grip suppression.
 * 3 - Force enable fw grip suppression.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    on success, return count; otherwise, return error code
 */
static ssize_t syna_sysfs_fw_grip_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = count;
	u8 input;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtou8(buf, 16, &input)) {
		LOGE("Invalid input %s", buf);
		return -EINVAL;
	}

	tcm->enable_fw_grip = input;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	tcm->gti->cmd.grip_cmd.setting = (input & 0x01) ? GTI_GRIP_ENABLE : GTI_GRIP_DISABLE;
	tcm->gti->ignore_grip_update = (input >> 1) & 0x01;
#endif

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_ENABLE_GRIP_SUPPRESSION,
				(input & 0x01),
				CMD_RESPONSE_IN_ATTN);

	LOGI("Set fw grip suppression mode %u.\n", tcm->enable_fw_grip);

	retval = count;

	return retval;
}

static struct kobj_attribute kobj_attr_fw_grip =
	__ATTR(fw_grip, 0644, syna_sysfs_fw_grip_show,
	       syna_sysfs_fw_grip_store);

/**
 * syna_sysfs_fw_palm_show()
 *
 * Attribute to show current palm rejection mode.
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
static ssize_t syna_sysfs_fw_palm_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);
	return scnprintf(buf, PAGE_SIZE, "%u\n", tcm->enable_fw_palm);
}

/**
 * syna_sysfs_fw_palm_store()
 *
 * Attribute to set palm rejection mode.
 * 0 - Disable fw palm rejection.
 * 1 - Enable fw palm rejection.
 * 2 - Force disable fw palm rejection.
 * 3 - Force enable fw palm rejection.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    on success, return count; otherwise, return error code
 */
static ssize_t syna_sysfs_fw_palm_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = count;
	u8 input;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtou8(buf, 16, &input)) {
		LOGE("Invalid input %s", buf);
		return -EINVAL;
	}

	tcm->enable_fw_palm = input;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	tcm->gti->cmd.palm_cmd.setting = (input & 0x01) ? GTI_PALM_ENABLE : GTI_PALM_DISABLE;
	tcm->gti->ignore_palm_update = (input >> 1) & 0x01;
#endif

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_ENABLE_PALM_REJECTION,
				(input & 0x01),
				CMD_RESPONSE_IN_ATTN);

	LOGI("Set fw palm rejection mode %u.\n", tcm->enable_fw_palm);

	retval = count;

	return retval;
}

static struct kobj_attribute kobj_attr_fw_palm =
	__ATTR(fw_palm, 0644, syna_sysfs_fw_palm_show,
	       syna_sysfs_fw_palm_store);

/**
 * syna_sysfs_compression_threshold_show()
 *
 * Attribute get the heatmap compression threshold.
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
static ssize_t syna_sysfs_compression_threshold_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int retval = 0;
	u16 output;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev,
			DC_COMPRESSION_THRESHOLD,
			&output,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Failed to get compression threshold.\n");
		retval = scnprintf(buf, PAGE_SIZE, "-1\n");
	} else {
		retval = scnprintf(buf, PAGE_SIZE, "%u\n", output);
	}

	return retval;
}

/**
 * syna_sysfs_compression_threshold_store()
 *
 * Attribute set the heatmap compression threshold.
 *
 * @param
 *    [ in] kobj:  an instance of kobj
 *    [ in] attr:  an instance of kobj attribute structure
 *    [ in] buf:   string buffer input
 *    [ in] count: size of buffer input
 *
 * @return
 *    on success, return count; otherwise, return error code
 */
static ssize_t syna_sysfs_compression_threshold_store(struct kobject *kobj,
		struct kobj_attribute *attr, const char *buf, size_t count)
{
	int retval = count;
	u8 input;
	struct device *p_dev;
	struct syna_tcm *tcm;

	p_dev = container_of(kobj->parent, struct device, kobj);
	tcm = dev_get_drvdata(p_dev);

	if (kstrtou8(buf, 10, &input)) {
		LOGE("Invalid input %s", buf);
		return -EINVAL;
	}


	tcm->hw_if->compression_threshold = input;

	syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_COMPRESSION_THRESHOLD,
			input,
			CMD_RESPONSE_IN_ATTN);

	LOGI("Set the heatmap compression threshold as %u.\n",
	     tcm->hw_if->compression_threshold);

	return retval;
}

static struct kobj_attribute kobj_attr_compression_threshold =
	__ATTR(compression_threshold, 0644, syna_sysfs_compression_threshold_show,
	       syna_sysfs_compression_threshold_store);


/* Definitions of sysfs attributes */
static struct attribute *attrs[] = {
	&kobj_attr_int2.attr,
	&kobj_attr_info.attr,
	&kobj_attr_scan_mode.attr,
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	&kobj_attr_force_active.attr,
#endif
	&kobj_attr_high_sensitivity.attr,
	&kobj_attr_fw_grip.attr,
	&kobj_attr_fw_palm.attr,
	&kobj_attr_compression_threshold.attr,
	&kobj_attr_debug.attr,
	NULL,
};
static struct bin_attribute *bin_attrs[] = {
	&bin_attr_get_raw_data,
	NULL,
};


static struct attribute_group attr_group = {
	.attrs = attrs,
	.bin_attrs = bin_attrs,
};

/**
 * @brief  Create a directory for the use of sysfs attributes.
 *
 * @param
 *    [ in] tcm:  the driver handle
 *    [ in] pdev: pointer to platform device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_sysfs_create_dir(struct syna_tcm *tcm, struct platform_device *pdev)
{
	int retval = 0;

	tcm->sysfs_dir = kobject_create_and_add(SYSFS_ROOT_DIR, &pdev->dev.kobj);
	if (!tcm->sysfs_dir) {
		LOGE("Fail to create sysfs directory\n");
		return -ENOTDIR;
	}

	retval = sysfs_create_group(tcm->sysfs_dir, &attr_group);
	if (retval < 0) {
		LOGE("Fail to create sysfs group\n");

		kobject_put(tcm->sysfs_dir);
		return retval;
	}

#ifdef HAS_TESTING_FEATURE
	tcm->sysfs_dir_testing = kobject_create_and_add("testing", tcm->sysfs_dir);
	if (!tcm->sysfs_dir_testing) {
		LOGE("Fail to create sysfs sub directory for testing\n");
		sysfs_remove_group(tcm->sysfs_dir, &attr_group);
		kobject_put(tcm->sysfs_dir);
		return -ENOTDIR;
	}

	retval = syna_testing_register_attributes(tcm, tcm->sysfs_dir_testing);
	if (retval < 0) {
		LOGE("Fail to register testing attributes\n");
		sysfs_remove_group(tcm->sysfs_dir, &attr_group);
		kobject_put(tcm->sysfs_dir);
		return retval;
	}
#endif

	return 0;
}
/**
 * @brief  Remove the directory for the use of sysfs attributes.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
void syna_sysfs_remove_dir(struct syna_tcm *tcm)
{
	if (!tcm) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	if (tcm->sysfs_dir) {
#ifdef HAS_TESTING_FEATURE
		if (tcm->sysfs_dir_testing) {
			syna_testing_remove_attributes(tcm->sysfs_dir_testing);
			kobject_put(tcm->sysfs_dir_testing);
		}
#endif
		if (tcm->sysfs_dir_utility) {
			sysfs_remove_group(tcm->sysfs_dir_utility, &attr_debug_group);
			kobject_put(tcm->sysfs_dir_utility);
		}

		sysfs_remove_group(tcm->sysfs_dir, &attr_group);

		kobject_put(tcm->sysfs_dir);
	}

}
