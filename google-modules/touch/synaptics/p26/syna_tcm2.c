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
 * @file syna_tcm2.c
 *
 * This file implements the Synaptics device driver running under Linux kernel
 * input device subsystem, and also communicate with Synaptics touch controller
 * through TouchComm command-response protocol.
 */

#include "syna_tcm2.h"
#include "linux/delay.h"
#include "syna_tcm2_cdev.h"
#include "syna_tcm2_platform.h"
#include "synaptics_touchcom_core_dev.h"
#include "synaptics_touchcom_func_base.h"
#include "synaptics_touchcom_func_touch.h"
#ifdef STARTUP_REFLASH
#ifdef HAS_TDDI_REFLASH_FEATURE
#include "synaptics_touchcom_func_reflash_tddi.h"
#endif
#ifdef HAS_REFLASH_FEATURE
#include "synaptics_touchcom_func_reflash.h"
#endif
#endif
#ifdef HAS_SYSFS_INTERFACE
#include "syna_tcm2_sysfs.h"
#endif

#include <linux/pinctrl/consumer.h>

static irqreturn_t syna_dev_interrupt_thread(int irq, void *data);
static irqreturn_t syna_dev_isr(int irq, void *handle);
static void syna_dev_release_irq(struct syna_tcm *tcm);
static int syna_dev_restore_feature_setting(struct syna_tcm *tcm, unsigned int delay_ms_resp);
static void syna_dev_show_info(struct syna_tcm *tcm);

#ifdef USE_CUSTOM_TOUCH_REPORT_CONFIG
/** An example of the format of custom touch configuration  */
static unsigned char custom_touch_format[] = {
	/* entity code */                    /* bits */
#ifdef ENABLE_WAKEUP_GESTURE
	TOUCH_REPORT_GESTURE_ID,                8,
#endif
	TOUCH_REPORT_NUM_OF_ACTIVE_OBJECTS,     8,
	TOUCH_REPORT_FOREACH_ACTIVE_OBJECT,
	TOUCH_REPORT_OBJECT_N_INDEX,            8,
	TOUCH_REPORT_OBJECT_N_CLASSIFICATION,   8,
	TOUCH_REPORT_OBJECT_N_X_POSITION,       16,
	TOUCH_REPORT_OBJECT_N_Y_POSITION,       16,
	TOUCH_REPORT_FOREACH_END,
	TOUCH_REPORT_END
};
#endif


#ifdef STARTUP_REFLASH
/** The delayed time to start fw update at startup */
#define STARTUP_REFLASH_DELAY_TIME_MS (200)
#endif

#ifdef RESET_ON_RESUME
/** The delayed time before issuing a reset if reset on resume is required */
#define RESET_ON_RESUME_DELAY_MS (100)
#endif

#define FW_UPDATE_DELAY_MS(erase, write) ((erase << 16) | write)

#if IS_ENABLED(CONFIG_PM) || IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
static const struct dev_pm_ops syna_dev_pm_ops;
#endif



/**
 * @brief  Restore the device and driver information to buffer
 *
 * @param
 *    [ in] tcm: the driver handle
 *    [out] buf:  string buffer for the firmware and the driver information
 *    [ in] buf_size: size of the buf
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
ssize_t syna_get_fw_info(struct syna_tcm *tcm, char *buf, size_t buf_size)
{
	int retval;
	int i;
	unsigned int count;
	struct tcm_dev *tcm_dev;

	tcm_dev = tcm->tcm_dev;

	count = 0;

	retval = scnprintf(buf, buf_size - count,
			"Driver version:     %d.%s\n",
			SYNAPTICS_TCM_DRIVER_VERSION,
			SYNAPTICS_TCM_DRIVER_SUBVER);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = scnprintf(buf, buf_size - count,
			"Core lib version:   %d.%02d\n\n",
			(unsigned char)(SYNA_TCM_CORE_LIB_VERSION >> 8),
			(unsigned char)SYNA_TCM_CORE_LIB_VERSION);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	if (!tcm->is_connected) {
		retval = scnprintf(buf, buf_size - count,
				"Device is NOT connected\n");
		count += retval;
		retval = count;
		goto exit;
	}

	if (tcm->pwr_state == BARE_MODE) {
		retval = count;
		goto exit;
	}

	retval = scnprintf(buf, buf_size - count,
			"TouchComm version:  %d\n", tcm_dev->id_info.version);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	switch (tcm_dev->id_info.mode) {
	case MODE_APPLICATION_FIRMWARE:
		retval = scnprintf(buf, buf_size - count,
				"Firmware mode:      Application Firmware, 0x%02x\n",
				tcm_dev->id_info.mode);
		if (retval < 0)
			goto exit;
		break;
	case MODE_BOOTLOADER:
		retval = scnprintf(buf, buf_size - count,
				"Firmware mode:      Bootloader, 0x%02x\n",
				tcm_dev->id_info.mode);
		if (retval < 0)
			goto exit;
		break;
	case MODE_ROMBOOTLOADER:
		retval = scnprintf(buf, buf_size - count,
				"Firmware mode:      Rom Bootloader, 0x%02x\n",
				tcm_dev->id_info.mode);
		if (retval < 0)
			goto exit;
		break;
	default:
		retval = scnprintf(buf, buf_size - count,
				"Firmware mode:      Mode 0x%02x\n",
				tcm_dev->id_info.mode);
		if (retval < 0)
			goto exit;
		break;
	}
	buf += retval;
	count += retval;

	retval = scnprintf(buf, buf_size - count,
			"Part number:        %*pE",
			(int)sizeof(tcm_dev->id_info.part_number), tcm_dev->id_info.part_number);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = scnprintf(buf, buf_size - count, "\n");
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = scnprintf(buf, buf_size - count,
			"Packrat number:     %d\n\n", tcm_dev->packrat_number);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	if (tcm_dev->id_info.mode != MODE_APPLICATION_FIRMWARE) {
		retval = count;
		goto exit;
	}

	retval = scnprintf(buf, buf_size - count, "Config ID:          ");
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	for (i = 0; i < MAX_SIZE_CONFIG_ID; i++) {
		retval = scnprintf(buf, buf_size - count,
			"0x%2x ", tcm_dev->config_id[i]);
		if (retval < 0)
			goto exit;
		buf += retval;
		count += retval;
	}

	retval = scnprintf(buf, buf_size - count, "\n");
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = scnprintf(buf, buf_size - count,
		"Max X & Y:          %d, %d\n", tcm_dev->max_x, tcm_dev->max_y);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = scnprintf(buf, buf_size - count,
		"Num of objects:     %d\n", tcm_dev->max_objects);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = scnprintf(buf, buf_size - count,
		"Num of cols & rows: %d, %d\n", tcm_dev->cols, tcm_dev->rows);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = snprintf(buf, buf_size - count,
		"Max. Read Size:     %d bytes\n", tcm_dev->max_rd_size);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = snprintf(buf, buf_size - count,
		"Max. Write Size:    %d bytes\n", tcm_dev->max_wr_size);
	if (retval < 0)
		goto exit;

	buf += retval;
	count += retval;

	retval = count;

exit:
	if (retval < 0)
		LOGE("Failed to get firmware info");

	return retval;
}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)

static int gti_default_handler(void *private_data, enum gti_cmd_type cmd_type,
	struct gti_union_cmd_data *cmd)
{
	LOGD("Not supported cmd_type %#x!", cmd_type);

	return -EOPNOTSUPP;
}

static int ping(void *private_data, struct gti_ping_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int retval = 0;

	if (!tcm || !tcm->tcm_dev)
		return -ENODEV;

	retval = syna_tcm_identify(tcm->tcm_dev, &tcm->tcm_dev->id_info, CMD_RESPONSE_IN_POLLING);
	if (retval < 0) {
		LOGE("Fail to get identification\n");
		return retval;
	}

	return 0;
}

static int get_fw_version(void *private_data, struct gti_fw_version_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int cmd_buffer_size = sizeof(cmd->buffer);
	int retval = 0;

	if (!tcm || !tcm->tcm_dev)
		return -ENODEV;

	retval = syna_tcm_identify(tcm->tcm_dev, &tcm->tcm_dev->id_info, CMD_RESPONSE_IN_POLLING);
	if (retval < 0) {
		LOGE("Fail to get identification\n");
		return retval;
	}

	retval = syna_tcm_get_app_info(tcm->tcm_dev, &tcm->tcm_dev->app_info, CMD_RESPONSE_IN_POLLING);
	if (retval < 0) {
		LOGE("Fail to get application info\n");
		return retval;
	}

	syna_get_fw_info(tcm, cmd->buffer, cmd_buffer_size);
	return 0;
}

static int get_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;

	cmd->setting = tcm->hw_if->bdata_attn.irq_enabled ?
			GTI_IRQ_MODE_ENABLE : GTI_IRQ_MODE_DISABLE;

	return 0;
}

static int set_irq_mode(void *private_data, struct gti_irq_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int retval = tcm->hw_if->hw_platform.ops_enable_attn(&tcm->hw_if->hw_platform,
			cmd->setting == GTI_IRQ_MODE_ENABLE);
	if (retval < 0)
		return retval;
	else
		return 0;
}

static int set_reset(void *private_data, struct gti_reset_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;

	if (goog_pm_wake_get_locks(tcm->gti) == 0) {
		LOGI("Connot trigger reset because no wake lock");
		return -EPERM;
	}

	if (cmd->setting == GTI_RESET_MODE_HW || cmd->setting == GTI_RESET_MODE_AUTO) {
		tcm->hw_if->ops_hw_reset(tcm->hw_if);
	} else if (cmd->setting == GTI_RESET_MODE_SW) {
		if (tcm->pwr_state != PWR_ON) {
			LOGI("Connot trigger SW reset because touch is off");
			return -EPERM;
		}
		syna_tcm_reset(tcm->tcm_dev, CMD_RESPONSE_IN_POLLING);
		syna_dev_restore_feature_setting(tcm, CMD_RESPONSE_IN_ATTN);
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

static int syna_calibrate(void *private_data, struct gti_calibrate_cmd *cmd)
{
	(void)private_data;

	/* Return successful calibration since there is nothing to do. */
	cmd->result = GTI_CALIBRATE_RESULT_DONE;
	return 0;
}

static int syna_set_coord_filter_enabled(void *private_data, struct gti_coord_filter_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set coordinate filter because touch is off");
		return -EPERM;
	}

	tcm->coord_filter_enable = cmd->setting == GTI_COORD_FILTER_ENABLE ? 1 : 0;

	if (tcm->hw_if->bdata_attn.irq_enabled) {
		queue_work(tcm->event_wq, &tcm->set_coord_filter_work);
	} else {
		LOGI("%s firmware coordinate filter.\n",
			tcm->coord_filter_enable ? "Enable" : "Disable");
		syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_COORD_FILTER,
			tcm->coord_filter_enable,
			CMD_RESPONSE_IN_POLLING);
	}

	return 0;
}

static int syna_get_coord_filter_enabled(void *private_data, struct gti_coord_filter_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	unsigned short coord_filter_enabled;
	int retval;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get coordinate filter because touch is off");
		return -EPERM;
	}

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_COORD_FILTER,
			&coord_filter_enabled, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to read coordinate filter, retval:%d.", retval);
		return -EIO;
	}

	cmd->setting = coord_filter_enabled ? GTI_COORD_FILTER_ENABLE : GTI_COORD_FILTER_DISABLE;

	return retval;
}

static void syna_set_coord_filter_work(struct work_struct *work)
{
	struct syna_tcm *tcm = container_of(work, struct syna_tcm, set_coord_filter_work);
	int retval = 0;

	retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval) {
		LOGE("Failed to obtain wake lock, ret = %d", retval);
		return;
	}

	LOGI("%s firmware coordinate filter.\n",
		tcm->coord_filter_enable ? "Enable" : "Disable");
	syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_COORD_FILTER,
			tcm->coord_filter_enable,
			CMD_RESPONSE_IN_ATTN);

	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);
}

static int syna_set_palm_mode(void *private_data, struct gti_palm_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set palm mode because touch is off");
		return -EPERM;
	}

	tcm->enable_fw_palm = cmd->setting == GTI_PALM_ENABLE ? 1 : 0;

	if (tcm->hw_if->bdata_attn.irq_enabled) {
		queue_work(tcm->event_wq, &tcm->set_palm_mode_work);
	} else {
		LOGI("%s firmware palm rejection.\n",
			(tcm->enable_fw_palm & 0x01) ? "Enable" : "Disable");
		syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_ENABLE_PALM_REJECTION,
			tcm->enable_fw_palm & 0x01,
			CMD_RESPONSE_IN_POLLING);
	}

	return 0;
}

static int syna_get_palm_mode(void *private_data, struct gti_palm_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	unsigned short palm_mode;
	int retval;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get palm mode because touch is off");
		return -EPERM;
	}

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_ENABLE_PALM_REJECTION,
			&palm_mode, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to read palm mode.");
		return retval;
	}

	cmd->setting = palm_mode ? GTI_PALM_ENABLE : GTI_PALM_DISABLE;

	return retval;
}

static void syna_set_palm_mode_work(struct work_struct *work)
{
	struct syna_tcm *tcm = container_of(work, struct syna_tcm, set_palm_mode_work);
	int retval = 0;

	retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval) {
		LOGE("Failed to obtain wake lock, ret = %d", retval);
		return;
	}

	LOGI("%s firmware palm rejection.\n",
		(tcm->enable_fw_palm & 0x01) ? "Enable" : "Disable");
	syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_ENABLE_PALM_REJECTION,
			tcm->enable_fw_palm & 0x01,
			CMD_RESPONSE_IN_ATTN);

	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);
}

static int syna_set_grip_mode(void *private_data, struct gti_grip_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set grip mode because touch is off");
		return -EPERM;
	}

	tcm->enable_fw_grip = cmd->setting == GTI_GRIP_ENABLE ? 1 : 0;

	if (tcm->hw_if->bdata_attn.irq_enabled) {
		queue_work(tcm->event_wq, &tcm->set_grip_mode_work);
	} else {
		LOGI("%s firmware grip suppression.\n",
			(tcm->enable_fw_grip & 0x01) ? "Enable" : "Disable");
		syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_ENABLE_GRIP_SUPPRESSION,
			tcm->enable_fw_grip & 0x01,
			CMD_RESPONSE_IN_POLLING);
	}

	return 0;
}

static int syna_get_grip_mode(void *private_data, struct gti_grip_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	unsigned short grip_mode;
	int retval;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get grip mode because touch is off");
		return -EPERM;
	}

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_ENABLE_GRIP_SUPPRESSION,
			&grip_mode, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to read grip mode.");
		return retval;
	}

	cmd->setting = grip_mode ? GTI_GRIP_ENABLE : GTI_GRIP_DISABLE;

	return retval;
}

static void syna_set_grip_mode_work(struct work_struct *work)
{
	struct syna_tcm *tcm = container_of(work, struct syna_tcm, set_grip_mode_work);
	int retval = 0;

	retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval) {
		LOGE("Failed to obtain wake lock, ret = %d", retval);
		return;
	}

	LOGI("%s firmware grip suppression.\n",
		(tcm->enable_fw_grip & 0x01) ? "Enable" : "Disable");
	syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_ENABLE_GRIP_SUPPRESSION,
			tcm->enable_fw_grip & 0x01,
			CMD_RESPONSE_IN_ATTN);

	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);
}

static int syna_set_heatmap_enabled(void *private_data, struct gti_heatmap_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int heatmap_report_mode = tcm->hw_if-> metadata_enabled ?
			HEATMAP_MODE_COMBINED_WITH_METADATA : HEATMAP_MODE_COMBINED;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set heatmap mode because touch is off");
		return -EPERM;
	}

	tcm->heatmap_mode = cmd->setting == GTI_HEATMAP_ENABLE ?
			heatmap_report_mode : HEATMAP_MODE_COORD;

	if (tcm->hw_if->bdata_attn.irq_enabled) {
		queue_work(tcm->event_wq, &tcm->set_heatmap_enabled_work);
	} else {
		LOGI("Set heatmap mode %d.\n", tcm->heatmap_mode);
		syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_HEATMAP_MODE,
			tcm->heatmap_mode,
			CMD_RESPONSE_IN_POLLING);
	}

	return 0;
}

static void syna_set_heatmap_enabled_work(struct work_struct *work)
{
	struct syna_tcm *tcm = container_of(work, struct syna_tcm, set_heatmap_enabled_work);
	int retval = 0;

	retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval) {
		LOGE("Failed to obtain wake lock, ret = %d", retval);
		return;
	}

	LOGI("Set heatmap mode %d.\n", tcm->heatmap_mode);

	syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_HEATMAP_MODE,
			tcm->heatmap_mode,
			CMD_RESPONSE_IN_ATTN);

	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);
}

static int syna_set_scan_mode(void *private_data, struct gti_scan_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int retval = 0;
	unsigned short gesture_mode;
	bool doze_enable;

	retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval < 0) {
		LOGE("Failed to obtain wake lock, ret = %d", retval);
		return retval;
	}

	switch (cmd->setting) {
	case GTI_SCAN_MODE_NORMAL_ACTIVE:
		gesture_mode = 0;
		doze_enable = false;
		break;
	case GTI_SCAN_MODE_NORMAL_IDLE:
		gesture_mode = 0;
		doze_enable = true;
		break;
	case GTI_SCAN_MODE_LP_ACTIVE:
		gesture_mode = 1;
		doze_enable = false;
		break;
	case GTI_SCAN_MODE_LP_IDLE:
		gesture_mode = 1;
		doze_enable = true;
		break;
	default:
		LOGE("Invalid scan mode %d.", cmd->setting);
		retval = -EINVAL;
		goto exit;
	}

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_ENABLE_WAKEUP_GESTURE_MODE,
			gesture_mode,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to set wakeup gesture mode via DC command, retval:%d\n", retval);
		retval = -EIO;
		goto exit;
	}

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_DISABLE_DOZE,
			doze_enable ? 0 : 1,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to set DC_DISABLE_DOZE, retval:%d\n", retval);
		retval = -EIO;
		goto exit;
	}

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_FORCE_DOZE_MODE,
			doze_enable ? 1 : 0,
			CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to set DC_FORCE_DOZE_MODE, retval:%d\n", retval);
		retval = -EIO;
	}

exit:
	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);
	return retval;
}

static int syna_get_scan_mode(void *private_data, struct gti_scan_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	unsigned short scan_mode;
	int retval;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get scan mode because touch is off");
		return -EPERM;
	}

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_TOUCH_SCAN_MODE,
			&scan_mode, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to read scan mode, retval:%d", retval);
		return -EIO;
	}

	switch (scan_mode) {
	case SCAN_NORMAL_IDLE:
		cmd->setting = GTI_SCAN_MODE_NORMAL_IDLE;
		break;
	case SCAN_NORMAL_ACTIVE:
		cmd->setting = GTI_SCAN_MODE_NORMAL_ACTIVE;
		break;
	case SCAN_LPWG_IDLE:
		cmd->setting = GTI_SCAN_MODE_LP_IDLE;
		break;
	case SCAN_LPWG_ACTIVE:
		cmd->setting = GTI_SCAN_MODE_LP_ACTIVE;
		break;
	case SCAN_SLEEP:
		LOGI("Touch is in sleep mode.");
		retval = -EINVAL;
		break;
	default:
		LOGE("Invalid scan mode %u", scan_mode);
		retval = -EINVAL;
		break;
	}

	return retval;
}

static int syna_set_sensing_mode(void *private_data, struct gti_sensing_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int retval = 0;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set sensing mode because touch is off");
		return -EPERM;
	}

	if (tcm->hw_if->bdata_attn.irq_enabled) {
		queue_work(tcm->event_wq, &tcm->set_sensing_mode_work);
	} else {
		if (cmd->setting == GTI_SENSING_MODE_DISABLE) {
			retval = syna_tcm_sleep(tcm->tcm_dev, true, CMD_RESPONSE_IN_POLLING);
			if (retval < 0) {
				LOGE("Failed enter deep sleep mode, ret:%d", retval);
				retval = -EIO;
			}
		} else if (cmd->setting == GTI_SENSING_MODE_ENABLE) {
			retval = syna_tcm_sleep(tcm->tcm_dev, false, CMD_RESPONSE_IN_POLLING);
			if (retval < 0) {
				LOGE("Failed exit deep sleep mode, ret:%d", retval);
				retval = -EIO;
			}
		} else {
			LOGE("Invalid sensing mode %d", cmd->setting);
			retval = -EINVAL;
		}
	}

	return retval;
}

static int syna_get_sensing_mode(void *private_data, struct gti_sensing_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int retval = 0;
	unsigned short scan_mode;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get sensing mode because touch is off");
		return -EPERM;
	}

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_TOUCH_SCAN_MODE,
			&scan_mode, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to read sensing mode, retval:%d", retval);
		return -EIO;
	}

	switch (scan_mode) {
	case SCAN_SLEEP:
		cmd->setting = GTI_SENSING_MODE_DISABLE;
		break;
	case SCAN_NORMAL_IDLE:
	case SCAN_NORMAL_ACTIVE:
	case SCAN_LPWG_IDLE:
	case SCAN_LPWG_ACTIVE:
		cmd->setting = GTI_SENSING_MODE_ENABLE;
		break;
	default:
		LOGE("Invalid scan mode %d", scan_mode);
		retval = -EINVAL;
		break;
	}

	return retval;
}

static void syna_set_sensing_mode_work(struct work_struct *work)
{
	struct syna_tcm *tcm = container_of(work, struct syna_tcm, set_sensing_mode_work);
	int retval = 0;

	retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval) {
		LOGE("Failed to obtain wake lock, ret = %d", retval);
		return;
	}

	LOGI("Set sensing mode %d.\n", tcm->gti->cmd.sensing_cmd.setting);

	if (tcm->gti->cmd.sensing_cmd.setting == GTI_SENSING_MODE_DISABLE) {
		retval = syna_tcm_sleep(tcm->tcm_dev, true, CMD_RESPONSE_IN_ATTN);
		if (retval < 0)
			LOGE("Failed enter deep sleep mode, ret:%d", retval);
	} else if (tcm->gti->cmd.sensing_cmd.setting == GTI_SENSING_MODE_ENABLE) {
		retval = syna_tcm_sleep(tcm->tcm_dev, false, CMD_RESPONSE_IN_ATTN);
		if (retval < 0)
			LOGE("Failed exit deep sleep mode, ret:%d", retval);
	} else {
		LOGE("Invalid sensing mode %d", tcm->gti->cmd.sensing_cmd.setting);
	}

	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);
}

static int syna_set_screen_protector_mode(void *private_data,
		struct gti_screen_protector_mode_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set screen protector mode because touch is off");
		return -EPERM;
	}

	tcm->high_sensitivity_mode = cmd->setting == GTI_SCREEN_PROTECTOR_MODE_ENABLE ? 1 : 0;

	if (tcm->hw_if->bdata_attn.irq_enabled) {
		queue_work(tcm->event_wq, &tcm->set_screen_protector_mode_work);
	} else {
		LOGI("%s screen protector mode.\n",
				tcm->high_sensitivity_mode ? "Enable" : "Disable");
		syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_HIGH_SENSITIVITY_MODE,
				tcm->high_sensitivity_mode,
				CMD_RESPONSE_IN_POLLING);
	}

	return 0;
}

static int syna_get_screen_protector_mode(void *private_data,
		struct gti_screen_protector_mode_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	unsigned short screen_protector_mode;
	int retval;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get screen protector mode because touch is off");
		return -EPERM;
	}

	retval = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_HIGH_SENSITIVITY_MODE,
			&screen_protector_mode, CMD_RESPONSE_IN_ATTN);
	if (retval < 0) {
		LOGE("Fail to read screen protector mode.");
		return retval;
	}

	cmd->setting = screen_protector_mode ?
			GTI_SCREEN_PROTECTOR_MODE_ENABLE : GTI_SCREEN_PROTECTOR_MODE_DISABLE;

	return retval;
}

static void syna_set_screen_protector_mode_work(struct work_struct *work)
{
	struct syna_tcm *tcm = container_of(work, struct syna_tcm, set_screen_protector_mode_work);
	int retval = 0;

	retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval) {
		LOGE("Failed to obtain wake lock, ret = %d", retval);
		return;
	}

	LOGI("%s screen protector mode.\n",
		tcm->high_sensitivity_mode ? "Enable" : "Disable");
	syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_HIGH_SENSITIVITY_MODE,
			tcm->high_sensitivity_mode,
			CMD_RESPONSE_IN_ATTN);

	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);
}

static u8 syna_gesture_dc_list[GTI_GESTURE_PARAMS_MAX] = {
	[GTI_STTW_MIN_X] = DC_STTW_MIN_X,
	[GTI_STTW_MAX_X] = DC_STTW_MAX_X,
	[GTI_STTW_MIN_Y] = DC_STTW_MIN_Y,
	[GTI_STTW_MAX_Y] = DC_STTW_MAX_Y,
	[GTI_STTW_MIN_FRAME] = DC_STTW_MIN_FRAME,
	[GTI_STTW_MAX_FRAME] = DC_STTW_MAX_FRAME,
	[GTI_STTW_JITTER] = DC_STTW_JITTER,
	[GTI_STTW_MAX_TOUCH_SIZE] = DC_STTW_MAX_TOUCH_SIZE,
	[GTI_LPTW_MIN_X] = DC_LPTW_MIN_X,
	[GTI_LPTW_MAX_X] = DC_LPTW_MAX_X,
	[GTI_LPTW_MIN_Y] = DC_LPTW_MIN_Y,
	[GTI_LPTW_MAX_Y] = DC_LPTW_MAX_Y,
	[GTI_LPTW_MIN_FRAME] = DC_LPTW_MIN_FRAME,
	[GTI_LPTW_JITTER] = DC_LPTW_JITTER,
	[GTI_LPTW_MAX_TOUCH_SIZE] = DC_LPTW_MAX_TOUCH_SIZE,
	[GTI_LPTW_MARGINAL_MIN_X] = DC_LPTW_MARGINAL_MIN_X,
	[GTI_LPTW_MARGINAL_MAX_X] = DC_LPTW_MARGINAL_MAX_X,
	[GTI_LPTW_MARGINAL_MIN_Y] = DC_LPTW_MARGINAL_MIN_Y,
	[GTI_LPTW_MARGINAL_MAX_Y] = DC_LPTW_MARGINAL_MAX_Y,
	[GTI_LPTW_MONITOR_CH_MIN_TX] = DC_LPTW_MONITOR_CH_MIN_TX,
	[GTI_LPTW_MONITOR_CH_MAX_TX] = DC_LPTW_MONITOR_CH_MAX_TX,
	[GTI_LPTW_MONITOR_CH_MIN_RX] = DC_LPTW_MONITOR_CH_MIN_RX,
	[GTI_LPTW_MONITOR_CH_MAX_RX] = DC_LPTW_MONITOR_CH_MAX_RX,
	[GTI_LPTW_NODE_COUNT_MIN] = DC_LPTW_NODE_COUNT_MIN,
	[GTI_LPTW_MOTION_BOUNDARY] = DC_LPTW_MOTION_BOUNDARY,
};

static int syna_set_gesture_type(struct syna_tcm *tcm, u8 gesture_type)
{
	int retval = 0;
	unsigned short set_gesture_type = 0;
	int heatmap_mode = tcm->hw_if-> metadata_enabled ?
			HEATMAP_MODE_COMBINED_WITH_METADATA : HEATMAP_MODE_COMBINED;


	if (gesture_type == GTI_GESTURE_DISABLE) {
		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_ENABLE_WAKEUP_GESTURE_MODE,
				0,
				CMD_RESPONSE_IN_ATTN);
		if (retval)
			goto exit;

		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_HEATMAP_MODE,
				heatmap_mode,
				CMD_RESPONSE_IN_ATTN);

		/* Disable REPORT_TOUCH for normal mode. */
		if (tcm->gti && goog_check_late_sense_on_enabled(tcm->gti)) {
			retval = syna_tcm_enable_report(tcm->tcm_dev, REPORT_TOUCH,
					false, CMD_RESPONSE_IN_ATTN);
			if (retval < 0)
				LOGE("Fail to disable report %d\n", REPORT_TOUCH);
		}
	} else {
		if (gesture_type == GTI_GESTURE_STTW) {
			set_gesture_type = GESTURE_TYPE_STTW;
		} else if (gesture_type == GTI_GESTURE_LPTW) {
			set_gesture_type = GESTURE_TYPE_LPTW;
		} else if (gesture_type == GTI_GESTURE_STTW_AND_LPTW) {
			set_gesture_type = GESTURE_TYPE_STTW_AND_LPTW;
		} else {
			LOGE("Unsupported gesture type %d", gesture_type);
			retval = -EINVAL;
			goto exit;
		}

		/* Enable REPORT_TOUCH for gesture mode. */
		if (tcm->gti && goog_check_late_sense_on_enabled(tcm->gti)) {
			retval = syna_tcm_enable_report(tcm->tcm_dev, REPORT_TOUCH,
					true, CMD_RESPONSE_IN_ATTN);
			if (retval < 0)
				LOGE("Fail to enable report %d\n", REPORT_TOUCH);
		}

		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_ENABLE_WAKEUP_GESTURE_MODE,
				1,
				CMD_RESPONSE_IN_ATTN);
		if (retval)
			goto exit;

		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_GESTURE_TYPE,
				set_gesture_type,
				CMD_RESPONSE_IN_ATTN);
		if (retval)
			goto exit;

		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
				DC_HEATMAP_MODE,
				HEATMAP_MODE_COORD,
				CMD_RESPONSE_IN_ATTN);

		if (retval)
			goto exit;

		retval = syna_tcm_enable_report(tcm->tcm_dev, REPORT_INVALID_GESTURE_EVENT, true,
						CMD_RESPONSE_IN_ATTN);
	}

exit:
	return retval;
}

static int syna_set_gesture_config(void *private_data, struct gti_gesture_config_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int retval = 0;
	int i = 0;

	LOGI("Set gesture config");

	for (i = 0; i < GTI_GESTURE_PARAMS_MAX; i++) {
		if (cmd->updating_params[i]) {
			if (i == GTI_GESTURE_TYPE) {
				syna_set_gesture_type(tcm, cmd->params[i]);
			} else {
				retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
						syna_gesture_dc_list[i],
						cmd->params[i],
						CMD_RESPONSE_IN_ATTN);
			}

			if (retval)
				goto exit;
		}
	}

exit:
	return retval;
}

static int syna_get_mutual_sensor_data(void *private_data, struct gti_sensor_data_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	unsigned char report_code = 0;
	int ret = 0;
	int i, j;
	unsigned int rows = tcm->tcm_dev->rows;
	unsigned int cols = tcm->tcm_dev->cols;

	if (cmd->type == GTI_SENSOR_DATA_TYPE_MS) {
		cmd->buffer = (u8 *)tcm->mutual_data;
		cmd->size = rows * cols * sizeof(uint16_t);
		return 0;
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_MS_DIFF:
		report_code = REPORT_DELTA;
		cmd->is_unsigned = false;
		break;
	case GTI_SENSOR_DATA_TYPE_MS_RAW:
		report_code = REPORT_RAW;
		cmd->is_unsigned = true;
		break;
	case GTI_SENSOR_DATA_TYPE_MS_BASELINE:
		report_code = REPORT_BASELINE;
		cmd->is_unsigned = true;
		break;
	default:
		LOGE("Unsupported report type %u", cmd->type);
		return -EINVAL;
	}

	reinit_completion(&tcm->raw_data_completion);

	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 1, CMD_RESPONSE_IN_ATTN);

	tcm->raw_data_report_code = report_code;
	syna_tcm_enable_report(tcm->tcm_dev, tcm->raw_data_report_code, true, CMD_RESPONSE_IN_ATTN);

	if (wait_for_completion_timeout(&tcm->raw_data_completion, msecs_to_jiffies(500)) == 0) {
		LOGE("Wait for sensor data %#x timeout.", cmd->type);
		ret = -ETIMEDOUT;
		goto exit;
	}

	syna_pal_mutex_lock(&tcm->raw_data_mutex);
	for (i = 0; i < cols; i++) {
		for (j = 0; j < rows; j++) {
			tcm->mutual_data_manual[i * rows + j] =
					(u16) (tcm->raw_data_buffer[j * cols + i]);
		}
	}
	syna_pal_mutex_unlock(&tcm->raw_data_mutex);

	cmd->buffer = (u8 *)tcm->mutual_data_manual;
	cmd->size = rows * cols * sizeof(u16);
exit:
	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 0, CMD_RESPONSE_IN_ATTN);
	syna_tcm_enable_report(tcm->tcm_dev, tcm->raw_data_report_code, false, CMD_RESPONSE_IN_ATTN);

	return ret;
}

static int syna_get_self_sensor_data(void *private_data, struct gti_sensor_data_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	unsigned char report_code = 0;
	int ret = 0;
	int i;
	unsigned int rows = tcm->tcm_dev->rows;
	unsigned int cols = tcm->tcm_dev->cols;

	if (cmd->type == GTI_SENSOR_DATA_TYPE_SS) {
		cmd->buffer = (u8 *)tcm->self_data;
		cmd->size = (rows + cols) * sizeof(uint16_t);
		return 0;
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_SS_DIFF:
		report_code = REPORT_DELTA;
		cmd->is_unsigned = false;
		break;
	case GTI_SENSOR_DATA_TYPE_SS_RAW:
		report_code = REPORT_RAW;
		cmd->is_unsigned = true;
		break;
	case GTI_SENSOR_DATA_TYPE_SS_BASELINE:
		report_code = REPORT_BASELINE;
		cmd->is_unsigned = true;
		break;
	default:
		LOGE("Unsupported report type %u", cmd->type);
		return -EINVAL;
	}

	reinit_completion(&tcm->raw_data_completion);

	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 1, CMD_RESPONSE_IN_ATTN);

	tcm->raw_data_report_code = report_code;
	syna_tcm_enable_report(tcm->tcm_dev, tcm->raw_data_report_code, true, CMD_RESPONSE_IN_ATTN);

	if (wait_for_completion_timeout(&tcm->raw_data_completion, msecs_to_jiffies(500)) == 0) {
		LOGE("Wait for sensor data %#x timeout.", cmd->type);
		ret = -ETIMEDOUT;
		goto exit;
	}

	syna_pal_mutex_lock(&tcm->raw_data_mutex);
	for (i = 0; i < rows; i++)
		tcm->self_data_manual[i] = (u16) (tcm->raw_data_buffer[rows * cols + cols + i]);

	for (i = 0; i < cols; i++)
		tcm->self_data_manual[rows + i] = (u16) (tcm->raw_data_buffer[rows * cols + i]);
	syna_pal_mutex_unlock(&tcm->raw_data_mutex);

	cmd->buffer = (u8 *)tcm->self_data_manual;
	cmd->size = (rows + cols) * sizeof(u16);
exit:
	syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_DISABLE_DOZE, 0, CMD_RESPONSE_IN_ATTN);
	syna_tcm_enable_report(tcm->tcm_dev, tcm->raw_data_report_code, false, CMD_RESPONSE_IN_ATTN);

	return ret;
}

static int syna_dev_ptflib_decoder(struct syna_tcm *tcm, const u16 *in_array,
				   const int in_array_size, u16 *out_array,
				   const int out_array_max_size)
{
	const u16 ESCAPE_MASK = 0xF000;
	const u16 ESCAPE_BIT = 0x8000;

	int i;
	int j;
	int out_array_size = 0;
	u16 prev_word = 0;
	u16 repetition = 0;
	u16 *temp_out_array = out_array;

	for (i = 0; i < in_array_size; i++) {
		u16 curr_word = in_array[i];
		if ((curr_word & ESCAPE_MASK) == ESCAPE_BIT) {
			repetition = (curr_word & ~ESCAPE_MASK);
			if (out_array_size + repetition > out_array_max_size)
				break;
			for (j = 0; j < repetition; j++) {
				*temp_out_array++ = prev_word;
				out_array_size++;
			}
		} else {
			if (out_array_size >= out_array_max_size)
				break;
			*temp_out_array++ = curr_word;
			out_array_size++;
			prev_word = curr_word;
		}
	}

	if (i != in_array_size || out_array_size != out_array_max_size) {
		LOGE("%d (in=%d, out=%d, rep=%d, out_max=%d).\n",
			i, in_array_size, out_array_size,
			repetition, out_array_max_size);
		memset(out_array, 0, out_array_max_size * sizeof(u16));
		return -1;
	}

	return out_array_size;
}

static void syna_parse_heatmap(struct syna_tcm *tcm, const unsigned char *heatmap_data,
		unsigned short heatmap_data_size)
{
	int i, j;
	uint16_t *temp_buffer;
	unsigned int rows = tcm->tcm_dev->rows;
	unsigned int cols = tcm->tcm_dev->cols;

	temp_buffer = syna_pal_mem_alloc(cols * rows, sizeof(u_int16_t));
	if (!temp_buffer) {
		LOGE("Failed to allocate temp_buffer");
		return;
	}

	if (!tcm->self_data || !tcm->mutual_data) {
		LOGE("There is no self_data or mutual_data");
		return;
	}

	/* Parse self data. */
	for (i = 0; i < rows; i++)
		tcm->self_data[i] = ((uint16_t *) heatmap_data)[cols + i];

	for (i = 0; i < cols; i++)
		tcm->self_data[rows + i] = ((uint16_t *) heatmap_data)[i];

	/* Parse mutual data. */
	syna_dev_ptflib_decoder(tcm, &((u16 *) heatmap_data)[cols + rows],
			heatmap_data_size / 2 - cols - rows, temp_buffer, cols * rows);

	for (i = 0; i < cols; i++) {
		for (j = 0; j < rows; j++)
			tcm->mutual_data[rows * i + j] = temp_buffer[cols * j + i];
	}

	syna_pal_mem_free(temp_buffer);
}

static int syna_get_vendor_register(void *private_data, struct gti_vendor_register_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int pos = 0;
	int size = sizeof(cmd->data);

	if (pos + sizeof(tcm->metadata) > size)
		return -EINVAL;

	memcpy(cmd->data, tcm->metadata, sizeof(tcm->metadata));
	pos += sizeof(tcm->metadata);

	if (pos + sizeof(tcm->simulation_data) > size)
		return -EINVAL;

	memcpy(cmd->data + pos, tcm->simulation_data, sizeof(tcm->simulation_data));
	pos += sizeof(tcm->simulation_data);
	cmd->size = pos;

	return 0;
}

static void syna_gti_init(struct syna_tcm *tcm)
{
	int retval = 0;
	struct gti_optional_configuration *options;
	struct platform_device *pdev = tcm->pdev;
	struct syna_hw_attn_data *attn = &tcm->hw_if->bdata_attn;
	unsigned int mutual_data_size =
			sizeof(u_int16_t) * (tcm->tcm_dev->rows * tcm->tcm_dev->cols);
	unsigned int self_data_size =
			sizeof(u_int16_t) * (tcm->tcm_dev->rows + tcm->tcm_dev->cols);

	tcm->mutual_data = devm_kzalloc(&pdev->dev, mutual_data_size, GFP_KERNEL);
	if (!tcm->mutual_data) {
		LOGE("Failed to allocate mutual_sensing_data");
		return;
	}

	tcm->self_data = devm_kzalloc(&pdev->dev, self_data_size, GFP_KERNEL);
	if (!tcm->self_data) {
		LOGE("Failed to allocate self_sensing_data");
		return;
	}

	tcm->mutual_data_manual = devm_kzalloc(&pdev->dev, mutual_data_size, GFP_KERNEL);
	if (!tcm->mutual_data) {
		LOGE("Failed to allocate mutual_sensing_data");
		return;
	}

	tcm->self_data_manual = devm_kzalloc(&pdev->dev, self_data_size, GFP_KERNEL);
	if (!tcm->self_data) {
		LOGE("Failed to allocate self_sensing_data");
		return;
	}

	/* release the interrupt and register the gti irq later. */
	syna_dev_release_irq(tcm);

	INIT_WORK(&tcm->set_coord_filter_work, syna_set_coord_filter_work);
	INIT_WORK(&tcm->set_grip_mode_work, syna_set_grip_mode_work);
	INIT_WORK(&tcm->set_palm_mode_work, syna_set_palm_mode_work);
	INIT_WORK(&tcm->set_heatmap_enabled_work, syna_set_heatmap_enabled_work);
	INIT_WORK(&tcm->set_screen_protector_mode_work, syna_set_screen_protector_mode_work);
	INIT_WORK(&tcm->set_sensing_mode_work, syna_set_sensing_mode_work);

	options = devm_kzalloc(&pdev->dev, sizeof(struct gti_optional_configuration), GFP_KERNEL);

	options->ping = ping;
	options->get_fw_version = get_fw_version;
	options->get_irq_mode = get_irq_mode;
	options->set_irq_mode = set_irq_mode;
	options->reset = set_reset;
	options->calibrate = syna_calibrate;
	options->selftest = tcm->selftest;
	options->get_coord_filter_enabled = syna_get_coord_filter_enabled;
	options->set_coord_filter_enabled = syna_set_coord_filter_enabled;
	options->set_grip_mode = syna_set_grip_mode;
	options->get_grip_mode = syna_get_grip_mode;
	options->set_palm_mode = syna_set_palm_mode;
	options->get_palm_mode = syna_get_palm_mode;
	options->set_heatmap_enabled = syna_set_heatmap_enabled;
	options->set_int2_mode = syna_set_int2_mode;
	options->get_int2_mode = syna_get_int2_mode;
	options->get_int2_status = syna_get_int2_status;
	options->set_scan_mode = syna_set_scan_mode;
	options->get_scan_mode = syna_get_scan_mode;
	options->set_sensing_mode = syna_set_sensing_mode;
	options->get_sensing_mode = syna_get_sensing_mode;
	options->set_screen_protector_mode = syna_set_screen_protector_mode;
	options->get_screen_protector_mode = syna_get_screen_protector_mode;
	options->set_gesture_config = syna_set_gesture_config;
	options->get_mutual_sensor_data = syna_get_mutual_sensor_data;
	options->get_self_sensor_data = syna_get_self_sensor_data;
	options->get_vendor_register = syna_get_vendor_register;
	options->get_water_mode = syna_get_water_mode;
	options->set_water_mode = syna_set_water_mode;
	options->get_report_rate = syna_get_report_rate;
	options->set_report_rate = syna_set_report_rate;

	tcm->gti = goog_touch_interface_connect(&tcm->pdev->dev,
						gti_default_handler, options);
	if (!tcm->gti) {
		LOGE("Failed to initialize GTI");
		return;
	}

	retval = goog_pm_register_notification(tcm->gti, &syna_dev_pm_ops);
	if (retval < 0)
		LOGE("Failed to register GTI pm");

	LOGI("Register IRQ by GTI.");
	attn->irq_id = gpio_to_irq(attn->irq_gpio);
	retval = goog_devm_request_threaded_irq(tcm->gti, &tcm->pdev->dev,
			attn->irq_id, syna_dev_isr, syna_dev_interrupt_thread,
			attn->irq_flags, PLATFORM_DRIVER_NAME, tcm);
	if (retval < 0)
		LOGE("Failed to request GTI IRQ");
	else
		attn->irq_enabled = true;

	syna_dev_restore_feature_setting(tcm, CMD_RESPONSE_IN_ATTN);
}

static void syna_notify_fw_status(struct syna_tcm *tcm, struct custom_fw_status *status)
{
	struct gti_fw_status_data gti_status_data = { 0 };

	if (!tcm->gti)
		return;

	if (status->b0_moisture != tcm->fw_status.b0_moisture) {
		goog_notify_fw_status_changed(tcm->gti,
			status->b0_moisture ? GTI_FW_STATUS_WATER_ENTER : GTI_FW_STATUS_WATER_EXIT,
			&gti_status_data);
	}
	if (status->b1_noise_state != tcm->fw_status.b1_noise_state) {
		gti_status_data.noise_level = status->b1_noise_state;
		goog_notify_fw_status_changed(tcm->gti,
			GTI_FW_STATUS_NOISE_MODE, &gti_status_data);
	}
	if (status->b3_grip != tcm->fw_status.b3_grip) {
		goog_notify_fw_status_changed(tcm->gti,
			status->b3_grip ? GTI_FW_STATUS_GRIP_ENTER : GTI_FW_STATUS_GRIP_EXIT,
			&gti_status_data);
	}
	if (status->b4_palm != tcm->fw_status.b4_palm) {
		goog_notify_fw_status_changed(tcm->gti,
			status->b4_palm ? GTI_FW_STATUS_PALM_ENTER : GTI_FW_STATUS_PALM_EXIT,
			&gti_status_data);
	}
	memcpy(&tcm->fw_status, status, sizeof(tcm->fw_status));
}
#endif

/**
 * @brief  Restore the feature settings after the device resume.
 *
 * @param
 *    [ in] tcm: tcm driver handle
 *    [ in] delay_ms_resp: delay time for response reading.
 *                         a positive value presents the time for polling;
 *                         or, set '0' or 'CMD_RESPONSE_IN_ATTN' for ATTN driven
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_restore_feature_setting(struct syna_tcm *tcm, unsigned int delay_ms_resp)
{
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct gti_fw_status_data gti_status_data = { 0 };
	struct custom_fw_status status = { 0 };
	unsigned char heatmap_report_mode = REPORT_TOUCH_AND_HEATMAP;
#endif
	LOGI("Restore touch feature settings.");
	int retval = 0;

	tcm->sync_lost_state = 0;
	tcm->sync_lost_state_last = 0;

	if (tcm->hw_if->compression_threshold != 0) {
		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_COMPRESSION_THRESHOLD,
						     tcm->hw_if->compression_threshold,
						     delay_ms_resp);
		if (retval < 0) {
			LOGE("Fail to set DC_COMPRESSION_THRESHOLD, retval:%d", retval);
			return retval;
		}
	}

	if (tcm->hw_if->grip_delta_threshold != 0) {
		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_GRIP_DELTA_THRESHOLD,
						     tcm->hw_if->grip_delta_threshold,
						     delay_ms_resp);
		if (retval < 0) {
			LOGE("Fail to set DC_GRIP_DELTA_THRESHOLD, retval:%d", retval);
			return retval;
		}
	}

	if (tcm->hw_if->grip_border_threshold != 0) {
		retval = syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_GRIP_BORDER_THRESHOLD,
						     tcm->hw_if->grip_border_threshold,
						     delay_ms_resp);
		if (retval < 0) {
			LOGE("Fail to set DC_GRIP_BORDER_THRESHOLD, retval:%d", retval);
			return retval;
		}
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (tcm->gti && goog_check_late_sense_on_enabled(tcm->gti)) {
		heatmap_report_mode = tcm->hw_if-> metadata_enabled ?
				REPORT_TOUCH_AND_HEATMAP_WITH_METADATA : REPORT_TOUCH_AND_HEATMAP;
		retval = syna_tcm_enable_report(tcm->tcm_dev, heatmap_report_mode, true,
						delay_ms_resp);
		if (retval < 0) {
			LOGE("Enable report %x fail.", heatmap_report_mode);
			return retval;
		}
		retval =
			syna_tcm_enable_report(tcm->tcm_dev, REPORT_FW_STATUS, true, delay_ms_resp);
		if (retval < 0) {
			LOGE("Enable report %x fail.", REPORT_FW_STATUS);
			return retval;
		}
	}

	syna_notify_fw_status(tcm, &status);
	goog_notify_fw_status_changed(tcm->gti, GTI_FW_STATUS_RESET, &gti_status_data);
	tcm->raw_timestamp_sensing = 0;
#endif

	return 0;
}

#if defined(ENABLE_HELPER)
static void syna_dev_get_reset_reason(struct syna_tcm *tcm)
{
	int retval;
	unsigned short reason;
	struct syna_hw_interface *hw_if;
	struct syna_hw_attn_data *attn;
	unsigned int resp_handling;

	if (!tcm)
		return;

	hw_if = tcm->hw_if;
	attn = &hw_if->bdata_attn;
	if (attn && (attn->irq_id) && (attn->irq_enabled))
		resp_handling = CMD_RESPONSE_IN_ATTN;
	else
		resp_handling = tcm->tcm_dev->msg_data.command_polling_time;

	retval = syna_tcm_get_dynamic_config(
			tcm->tcm_dev, DC_FW_RESET_REASON, &reason, resp_handling);
	if (retval < 0)
		LOGE("Fail to read DC_FW_RESET_REASON.");
	else
		LOGI("Reset reason %#x", reason);
}

/**
 * @brief  Ensure the reset is done.
 *         This function will be called after receiving identify event.
 *         Using get scan mode command to ensure the reset is done.
 * @param
 *    [ in] tcm: tcm driver handle
 */
static void syna_dev_ensure_reset_done(struct syna_tcm *tcm)
{
	struct syna_hw_interface *hw_if;
	struct syna_hw_attn_data *attn;
	unsigned int resp_handling;
	unsigned short scan_mode;

	if (!tcm)
		return;

	hw_if = tcm->hw_if;
	attn = &hw_if->bdata_attn;
	if (attn && (attn->irq_id) && (attn->irq_enabled))
		resp_handling = CMD_RESPONSE_IN_ATTN;
	else
		resp_handling = tcm->tcm_dev->msg_data.command_polling_time;

	int retry_count = 3;
	while (retry_count-- > 0) {
		int retval = syna_tcm_get_dynamic_config(tcm->tcm_dev,
							 DC_TOUCH_SCAN_MODE,
							 &scan_mode,
							 resp_handling);
		if (retval < 0) {
			LOGI("Device reset is not done. error: %d", retval);
		} else {
			if (scan_mode == SCAN_NORMAL_ACTIVE || scan_mode == SCAN_NORMAL_IDLE) {
				LOGI("Ensure reset done.");
				return;
			}

			LOGI("Device reset is not done. scan mode %d", scan_mode);
		}
		syna_pal_sleep_ms(30);
	}

	LOGE("Ensure reset failed.");
}

/**
 * @brief  Example of the helper work.
 *         Helper could run on the other context and send the secondary command to device.
 * @param
 *    [ in] work: data for work used
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static void syna_dev_helper_work(struct work_struct *work)
{
	unsigned char task;
	struct syna_tcm_helper *helper =
			container_of(work, struct syna_tcm_helper, work);
	struct syna_tcm *tcm =
			container_of(helper, struct syna_tcm, helper);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	int retval = goog_pm_wake_lock(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST, true);
	if (retval) {
		LOGI("%s: Failed to obtain wake lock, ret = %d", __func__, retval);
		goto exit;
	}
#endif
	syna_pal_sleep_ms(50);
	syna_dev_ensure_reset_done(tcm);
	syna_dev_get_reset_reason(tcm);

	task = ATOMIC_GET(helper->task);

	switch (task) {
	case HELP_RESET_DETECTED:
		LOGD("Reset caught (device mode:0x%x)\n", tcm->tcm_dev->dev_mode);
		syna_dev_restore_feature_setting(tcm, CMD_RESPONSE_IN_ATTN);
		break;
	default:
		break;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_pm_wake_unlock_nosync(tcm->gti, GTI_PM_WAKELOCK_TYPE_VENDOR_REQUEST);

exit:
#endif
	ATOMIC_SET(helper->task, HELP_NONE);
}
#endif
/**
 * @brief  Example to process the report resulted by the unexpected reset.
 *
 * @param
 *    [ in]    code:          the code of current touch entity
 *    [ in]    report:        touch report given
 *    [ in]    report_size:   size of given touch report
 *    [ in]    callback_data: private data being passed to the callback function
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_process_unexpected_reset(const unsigned char code,
	const unsigned char *report, unsigned int report_size,
	void *callback_data)
{
	struct syna_tcm *tcm = (struct syna_tcm *)callback_data;

	if (!tcm) {
		LOGE("Invalid data to process\n");
		return -EINVAL;
	}

	if (tcm->pwr_state != PWR_ON) {
		LOGI("Touch is already off.");
		return 0;
	}

	LOGN("Device has been reset, may be the spontaneous reset\n");

#if defined(ENABLE_HELPER)
	if (ATOMIC_GET(tcm->helper.task) == HELP_NONE) {
		ATOMIC_SET(tcm->helper.task, HELP_RESET_DETECTED);
		queue_work(tcm->event_wq, &tcm->helper.work);
	}
#endif

	return 0;
}
#ifdef ENABLE_CUSTOM_TOUCH_ENTITY
/**
 * @brief  Callback to parse the custom or non-standard touch entity from the
 *         touch report.
 *
 *         Please be noted that this function will be invoked in ISR so don't
 *         issue another touchcomm command here.
 *         If really needed, please assign a task to helper thread.
 * @param
 *    [ in]    code:          the code of current touch entity
 *    [ in]    config:        the report configuration stored
 *    [in/out] config_offset: offset of current position in report config,
 *                            and then return the updated position.
 *    [ in]    report:        touch report given
 *    [in/out] report_offset: offset of current position in touch report,
 *                            the updated position should be returned.
 *    [ in]    report_size:   size of given touch report
 *    [ in]    callback_data: pointer to caller data passed to callback function
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_parse_custom_touch_data_cb(const unsigned char code,
	const unsigned char *config, unsigned int *config_offset,
	const unsigned char *report, unsigned int *report_offset,
	unsigned int report_size, void *callback_data)
{
	struct syna_tcm *tcm = (struct syna_tcm *)callback_data;
	struct tcm_touch_data_blob *touch_data;
	struct tcm_objects_data_blob *object_data;
	unsigned int data;
	unsigned int bits;
	int i, count;
	char print_buf[128] = {0};

	touch_data = &tcm->tp_data;
	object_data = touch_data->object_data;

	switch (code) {
	case TOUCH_ENTITY_CUSTOM_ANGLE:
		bits = config[(*config_offset)++];
		syna_tcm_get_touch_data(report, report_size,
				*report_offset, bits, &data);

		object_data[touch_data->obji].custom_data[CUSTOM_DATA_ANGLE] = data;

		*report_offset += bits;
		return bits;
	case TOUCH_ENTITY_CUSTOM_MAJOR:
		bits = config[(*config_offset)++];
		syna_tcm_get_touch_data(report, report_size,
				*report_offset, bits, &data);

		object_data[touch_data->obji].custom_data[CUSTOM_DATA_MAJOR] = data;

		*report_offset += bits;
		return bits;
	case TOUCH_ENTITY_CUSTOM_MINOR:
		bits = config[(*config_offset)++];
		syna_tcm_get_touch_data(report, report_size,
				*report_offset, bits, &data);

		object_data[touch_data->obji].custom_data[CUSTOM_DATA_MINOR] = data;
		*report_offset += bits;
		return bits;
	case TOUCH_ENTITY_SYNC_LOST_STATE:
		bits = config[(*config_offset)++];
		syna_tcm_get_touch_data(report, report_size,
				*report_offset, bits, &data);

		tcm->sync_lost_state_last = tcm->sync_lost_state;
		tcm->sync_lost_state = (data & 0x0F);
		*report_offset += bits;
		return bits;
	case TOUCH_ENTITY_METADATA:
		bits = config[(*config_offset)++];
		count = 0;
		for (i = 0; i < (bits / 16); i++) {
			syna_tcm_get_touch_data(report, report_size,
					*report_offset, 16, &data);
			*report_offset += 16;
			tcm->metadata[i * 2] = (u8)(data & 0xFF);
			tcm->metadata[i * 2 + 1] = (u8)((data >> 8) & 0xFF);
			count += scnprintf(&print_buf[count], sizeof(print_buf) - count,
					"%u ", data);

		}
		count += scnprintf(&print_buf[count], sizeof(print_buf) - count, "\n");
		if (tcm->abnormal_gesture_reported)
			LOGI("metadata=%s", &print_buf[0]);
		return bits;
	default:
		LOGW("Unknown touch config code (idx:%d 0x%02x)\n",
			*config_offset, code);
		return (-EINVAL);
	}

	return (-EINVAL);
}
#endif

#if defined(ENABLE_WAKEUP_GESTURE)
/**
 * syna_dev_parse_custom_gesture_cb()
 *
 * Callback to parse the custom or non-standard gesture data from the
 * touch report.
 *
 * Please be noted that this function will be invoked in ISR so don't
 * issue another touchcomm command here.
 * If really needed, please assign a task to helper thread.
 *
 * @param
 *    [ in]    code:          the code of current touch entity
 *    [ in]    config:        the report configuration stored
 *    [in/out] config_offset: offset of current position in report config,
 *                            the updated position should be returned.
 *    [ in]    report:        touch report given
 *    [in/out] report_offset: offset of current position in touch report,
 *                            the updated position should be returned.
 *    [ in]    report_size:   size of given touch report
 *    [ in]    callback_data: pointer to caller data
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_dev_parse_custom_gesture_cb(const unsigned char code,
		const unsigned char *config, unsigned int *config_offset,
		const unsigned char *report, unsigned int *report_offset,
		unsigned int report_size, void *callback_data)
{
	struct syna_tcm *tcm = (struct syna_tcm *)callback_data;
	unsigned int data;
	unsigned int bits;
	unsigned int offset = *report_offset;
	struct custom_gesture_data {
		unsigned short x;
		unsigned short y;
		unsigned char major;
		unsigned char minor;
		unsigned char angle;
	} __packed;
	struct custom_gesture_data g_pos = {0};
	int major = 0;
	int minor = 0;
	int angle = 0;

	bits = config[(*config_offset)++];

	if (code == TOUCH_REPORT_GESTURE_ID) {
		syna_tcm_get_touch_data(report, report_size, offset, bits, &data);

		switch (data) {
		case GESTURE_NONE:
			break;
		case GESTURE_SINGLE_TAP:
			LOGI("Gesture single tap detected\n");
			tcm->abnormal_gesture_reported = true;
			break;
		case GESTURE_LONG_PRESS:
			LOGI("Gesture long press detected\n");
			tcm->abnormal_gesture_reported = true;
			break;
		default:
			LOGW("Unknown gesture id %d\n", data);
			break;
		}
		tcm->tp_data.gesture_id = data;

		*report_offset += bits;

	} else if (code == TOUCH_REPORT_GESTURE_DATA) {
		if (bits != (sizeof(struct custom_gesture_data) * 8)) {
			LOGE("Invalid size of gesture data %d (expected:%d)\n",
				bits, (int)(sizeof(struct custom_gesture_data) * 8));
			return -EINVAL;
		}

		syna_tcm_get_touch_data(report, report_size, offset, 16, &data);
		g_pos.x = (unsigned short)data;
		offset += 16;

		syna_tcm_get_touch_data(report, report_size, offset, 16, &data);
		g_pos.y = (unsigned short)data;
		offset += 16;

		syna_tcm_get_touch_data(report, report_size, offset, 8, &data);
		g_pos.minor = (unsigned char)data;
		minor = g_pos.minor * tcm->hw_if->pixels_per_mm;
		offset += 8;

		syna_tcm_get_touch_data(report, report_size, offset, 8, &data);
		g_pos.major = (unsigned char)data;
		major = g_pos.major * tcm->hw_if->pixels_per_mm;
		offset += 8;

		syna_tcm_get_touch_data(report, report_size, offset, 8, &data);
		g_pos.angle = (unsigned char) data;
		angle = (int) (((s8) g_pos.angle) * 2048 / 45);
		offset += 8;

		*report_offset += bits;

		if (tcm->tp_data.gesture_id != GESTURE_NONE) {
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			struct gti_fw_status_data data;
			if (tcm->tp_data.gesture_id == GESTURE_SINGLE_TAP)
				data.gesture_event.type = GTI_GESTURE_STTW;
			else if (tcm->tp_data.gesture_id == GESTURE_LONG_PRESS)
				data.gesture_event.type = GTI_GESTURE_LPTW;
			else
				return bits;

			data.gesture_event.x = g_pos.x;
			data.gesture_event.y = g_pos.y;
			data.gesture_event.major = major;
			data.gesture_event.minor = minor;
			data.gesture_event.angle = angle;
			goog_notify_fw_status_changed(tcm->gti, GTI_FW_STATUS_GESTURE_EVENT, &data);
#endif
			LOGD("Gesture data x:%u y:%u major:%d minor:%d  angle:%d\n",
				g_pos.x, g_pos.y, major, minor, angle);
		}
	} else {
		return -EINVAL;
	}

	return bits;
}
#endif
/**
 * @brief  Release all relevant touched events.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    void.
 */
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
static void syna_dev_free_input_events(struct syna_tcm *tcm)
{
	struct input_dev *input_dev = tcm->input_dev;
#ifdef TYPE_B_PROTOCOL
	unsigned int idx;
#endif

	if (input_dev == NULL)
		return;

	syna_pal_mutex_lock(&tcm->tp_event_mutex);

#ifdef TYPE_B_PROTOCOL
	for (idx = 0; idx < MAX_NUM_OBJECTS; idx++) {
		input_mt_slot(input_dev, idx);
		input_report_abs(input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
	}
#endif
	input_report_key(input_dev, BTN_TOUCH, 0);
	input_report_key(input_dev, BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
	input_mt_sync(input_dev);
#endif
	input_sync(input_dev);

	syna_pal_mutex_unlock(&tcm->tp_event_mutex);
}
#endif

/**
 * @brief  Report touched events to the input subsystem.
 *
 *         Assuming that touched data including touch_data_blob and
 *         objects_data_blob are ready.
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    void.
 */
static void syna_dev_report_input_events(struct syna_tcm *tcm)
{
	unsigned int idx;
	unsigned int x;
	unsigned int y;
	unsigned int z;
#ifdef ENABLE_CUSTOM_TOUCH_ENTITY
	int major;
	int minor;
	int angle;
#else
	int wx;
	int wy;
#endif
	unsigned int status;
	unsigned int touch_count;
	struct input_dev *input_dev = tcm->input_dev;
	unsigned int max_objects = tcm->tcm_dev->max_objects;
	struct tcm_touch_data_blob __maybe_unused *touch_data;
	struct tcm_objects_data_blob *object_data;

	if (input_dev == NULL)
		return;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_lock(tcm->gti);
#else
	syna_pal_mutex_lock(&tcm->tp_event_mutex);
#endif

	object_data = &tcm->tp_data.object_data[0];

#ifdef ENABLE_WAKEUP_GESTURE
	touch_data = &tcm->tp_data;
	if ((tcm->pwr_state == LOW_PWR) && tcm->irq_wake) {
		if (touch_data->gesture_id) {
			LOGD("Gesture detected, id:%d\n",
				touch_data->gesture_id);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_input_report_key(tcm->gti, input_dev, KEY_WAKEUP, 1);
			goog_input_sync(tcm->gti, input_dev);
			goog_input_report_key(tcm->gti, input_dev, KEY_WAKEUP, 0);
			goog_input_sync(tcm->gti, input_dev);
#else
			input_report_key(input_dev, KEY_WAKEUP, 1);
			input_sync(input_dev);
			input_report_key(input_dev, KEY_WAKEUP, 0);
			input_sync(input_dev);
#endif
		}
	}
#endif

	if (tcm->pwr_state == LOW_PWR)
		goto exit;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	LOGD("timestamp=%u, gap=%u", touch_data->timestamp, (touch_data->timestamp - tcm->raw_timestamp_sensing));
	tcm->timestamp_sensing += (touch_data->timestamp - tcm->raw_timestamp_sensing) * 1000;
	tcm->raw_timestamp_sensing = touch_data->timestamp;
	goog_input_set_sensing_timestamp(tcm->gti, input_dev, tcm->timestamp_sensing);
	goog_input_set_timestamp(tcm->gti, input_dev, tcm->timestamp);
#endif

	touch_count = 0;

	for (idx = 0; idx < max_objects; idx++) {
		if (tcm->prev_obj_status[idx] == LIFT &&
				object_data[idx].status == LIFT)
			status = NOP;
		else
			status = object_data[idx].status;

		switch (status) {
		case LIFT:
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_input_mt_slot(tcm->gti, input_dev, idx);
			goog_input_report_abs(tcm->gti, input_dev, ABS_MT_PRESSURE, 0);
			goog_input_mt_report_slot_state(tcm->gti, input_dev,
					MT_TOOL_FINGER, 0);
			LOGD("Finger %d UP!", idx);
#else
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(input_dev, idx);
			input_report_abs(input_dev, ABS_MT_PRESSURE, 0);
			input_mt_report_slot_state(input_dev,
					MT_TOOL_FINGER, 0);
#endif
#endif
			break;
		case FINGER:
		case GLOVED_OBJECT:
		case PALM:
			x = object_data[idx].x_pos;
			y = object_data[idx].y_pos;
#ifdef ENABLE_CUSTOM_TOUCH_ENTITY
			major = object_data[idx].custom_data[CUSTOM_DATA_MAJOR];
			minor = object_data[idx].custom_data[CUSTOM_DATA_MINOR];
			angle = object_data[idx].custom_data[CUSTOM_DATA_ANGLE];
			/* Report major and minor in display pixels. */
			major = major * tcm->hw_if->pixels_per_mm;
			minor = minor * tcm->hw_if->pixels_per_mm;
#else
			wx = object_data[idx].x_width;
			wy = object_data[idx].y_width;
			/* Report major and minor in display pixels. */
			wx = wx * tcm->hw_if->pixels_per_mm;
			wy = wy * tcm->hw_if->pixels_per_mm;
#endif

			if (object_data[idx].z == 0) {
				z = 1;
				LOGW("Get a touch coordinate with pressure = 0");
			} else {
				z = object_data[idx].z;
			}
#ifdef REPORT_SWAP_XY
			x = x ^ y;
			y = x ^ y;
			x = x ^ y;
#endif
#ifdef REPORT_FLIP_X
			x = tcm->input_dev_params.max_x - x;
#endif
#ifdef REPORT_FLIP_Y
			y = tcm->input_dev_params.max_y - y;
#endif

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			goog_input_mt_slot(tcm->gti, input_dev, idx);
			goog_input_mt_report_slot_state(tcm->gti, input_dev, MT_TOOL_FINGER, 1);
			goog_input_report_key(tcm->gti, input_dev, BTN_TOUCH, 1);
			goog_input_report_key(tcm->gti, input_dev, BTN_TOOL_FINGER, 1);
			goog_input_report_abs(tcm->gti, input_dev, ABS_MT_POSITION_X, x);
			goog_input_report_abs(tcm->gti, input_dev, ABS_MT_POSITION_Y, y);
			goog_input_report_abs(tcm->gti, input_dev, ABS_MT_PRESSURE, z);
#ifdef ENABLE_CUSTOM_TOUCH_ENTITY
			goog_input_report_abs(tcm->gti, input_dev, ABS_MT_TOUCH_MAJOR, major);
			goog_input_report_abs(tcm->gti, input_dev, ABS_MT_TOUCH_MINOR, minor);
			goog_input_report_abs(tcm->gti, input_dev,
					ABS_MT_ORIENTATION, (s16) (((s8) angle) * 2048 / 45));
			LOGD("Finger %d(%d, %d)[ma:%d,mi:%d,p:%d,o:%d]DOWN!\n", idx, x, y, major, minor, z, (s8)angle);
#else
			goog_input_report_abs(tcm->gti, input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			goog_input_report_abs(tcm->gti, input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#else
#ifdef TYPE_B_PROTOCOL
			input_mt_slot(input_dev, idx);
			input_mt_report_slot_state(input_dev,
					MT_TOOL_FINGER, 1);
#endif
			input_report_key(input_dev, BTN_TOUCH, 1);
			input_report_key(input_dev, BTN_TOOL_FINGER, 1);
			input_report_abs(input_dev, ABS_MT_POSITION_X, x);
			input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
			input_report_abs(input_dev, ABS_MT_PRESSURE, z);
#ifdef REPORT_TOUCH_WIDTH
#ifdef ENABLE_CUSTOM_TOUCH_ENTITY
			input_report_abs(input_dev,
					ABS_MT_TOUCH_MAJOR, major);
			input_report_abs(input_dev,
					ABS_MT_TOUCH_MINOR, minor);
			input_report_abs(input_dev,
					ABS_MT_ORIENTATION, (s16) (((s8) angle) * 2048 / 45));
#else
			input_report_abs(input_dev,
					ABS_MT_TOUCH_MAJOR, max(wx, wy));
			input_report_abs(input_dev,
					ABS_MT_TOUCH_MINOR, min(wx, wy));
#endif
#endif
#ifndef TYPE_B_PROTOCOL
			input_mt_sync(input_dev);
#endif
			LOGD("Finger %d: x = %d, y = %d, z = %d\n", idx, x, y, z);
#endif

			touch_count++;
			break;
		default:
			break;
		}

		tcm->prev_obj_status[idx] = object_data[idx].status;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (touch_count == 0) {
		goog_input_report_key(tcm->gti, input_dev, BTN_TOUCH, 0);
		goog_input_report_key(tcm->gti, input_dev, BTN_TOOL_FINGER, 0);
	}

	goog_input_sync(tcm->gti, input_dev);
#else
	if (touch_count == 0) {
		input_report_key(input_dev, BTN_TOUCH, 0);
		input_report_key(input_dev, BTN_TOOL_FINGER, 0);
#ifndef TYPE_B_PROTOCOL
		input_mt_sync(input_dev);
#endif
	}

	input_set_timestamp(input_dev, tcm->timestamp);
	input_sync(input_dev);
#endif

	tcm->touch_count = touch_count;

	/*
	 * Print the warning log if
	 * 1. the last frame is synced but current frame is not, and
	 * 2. finger exists since last frame.
	 */
	if (!tcm->sync_lost_state_last && tcm->sync_lost_state && (tcm->touch_count > 0))
		LOGW("Touch sync lost, finger count = %d, state = %u", touch_count, tcm->sync_lost_state);

exit:
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_unlock(tcm->gti);
#else
	syna_pal_mutex_unlock(&tcm->tp_event_mutex);
#endif
}

/**
 * @brief  Process the specified events for the invalid gesture detected.
 *
 * @param
 *    [ in]    code:          the code of given report type
 *    [ in]    report:        report data given
 *    [ in]    report_size:   size of the given report
 *    [ in]    callback_data: private data being passed to the callback function
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_dev_process_invalid_gesture_events(const unsigned char code,
						   const unsigned char *report,
						   unsigned int report_size, void *callback_data)
{
	struct syna_tcm *tcm = (struct syna_tcm *)callback_data;
	unsigned short type = 0;
	unsigned char *metadata = NULL;
	unsigned int metadata_size = 0;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct gti_fw_status_data data;
#endif // IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)

	if (!tcm) {
		LOGE("Invalid callback data\n");
		return -EINVAL;
	}

	if (code != REPORT_INVALID_GESTURE_EVENT)
		return 0;

	/* restore the type of event */
	if (report_size >= 2)
		type = (unsigned short)syna_pal_le2_to_uint(&report[0]);

	/* restore the metadata */
	metadata = (unsigned char *)&report[2];
	metadata_size = report_size - 2;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	data.invalid_gesture_event.type = type;
	const size_t max_size = sizeof(data.invalid_gesture_event.payload);
	memcpy(data.invalid_gesture_event.payload.data, metadata, min(max_size, metadata_size));

	goog_notify_fw_status_changed(tcm->gti, GTI_FW_STATUS_INVALID_GESTURE_EVENT, &data);
#else
	/* determine the event */
	switch (type) {
	case 0x01: // STTW_INVALID_GESTURE_OUT_OF_RANGE
		LOGI("Receive invalid gesture event, STTW OUT_OF_RANGE (%u, %u)\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1]);
		break;
	case 0x02: // STTW_INVALID_GESTURE_SHIFT
		LOGI("Receive invalid gesture event, STTW SHIFT (%u, %u) -> (%u, %u) distance: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u16 *)metadata)[2],
		     ((u16 *)metadata)[3], ((u16 *)metadata)[4]);
		break;
	case 0x03: // STTW_INVALID_GESTURE_PALM
		LOGI("Receive invalid gesture event, STTW PALM (%u, %u) node_count: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u8 *)metadata)[4]);
		break;
	case 0x04: // STTW_INVALID_GESTURE_MULTI_TOUCH
		LOGI("Receive invalid gesture event, STTW MULTI_TOUCH (%u, %u) finger_count: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u8 *)metadata)[4]);
		break;
	case 0x05: // STTW_INVALID_GESTURE_LONG_PRESS
		LOGI("Receive invalid gesture event, STTW LONG_PRESS (%u, %u) frame_count: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u8 *)metadata)[4]);
		break;
	case 0x06: // STTW_INVALID_GESTURE_RAPID_TOUCH
		LOGI("Receive invalid gesture event, STTW RAPID_TOUCH (%u, %u) frame_count: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u8 *)metadata)[4]);
		break;
	case 0x11: // LPTW_INVALID_GESTURE_OUT_OF_RANGE
		LOGI("Receive invalid gesture event, LPTW OUT_OF_RANGE (%u, %u) -> (%u, %u)\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u16 *)metadata)[2],
		     ((u16 *)metadata)[3]);
		break;
	case 0x12: // LPTW_INVALID_GESTURE_SHIFT
		LOGI("Receive invalid gesture event, LPTW SHIFT (%u, %u)/(%u, %u) -> (%u, %u) distance: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u16 *)metadata)[2],
		     ((u16 *)metadata)[3], ((u16 *)metadata)[4], ((u16 *)metadata)[5],
		     ((u16 *)metadata)[6]);
		break;
	case 0x13: // LPTW_INVALID_GESTURE_TOO_SMALL
		LOGI("Receive invalid gesture event, LPTW TOO_SMALL (%u, %u) node_count: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u8 *)metadata)[4]);
		break;
	case 0x14: // LPTW_INVALID_GESTURE_PALM
		LOGI("Receive invalid gesture event, LPTW PALM (%u, %u) node_count: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u8 *)metadata)[4]);
		break;
	case 0x15: // LPTW_INVALID_GESTURE_RAPID_TOUCH
		LOGI("Receive invalid gesture event, LPTW RAPID_TOUCH (%u, %u) frame_count: %u\n",
		     ((u16 *)metadata)[0], ((u16 *)metadata)[1], ((u8 *)metadata)[4]);
		break;
	default:
		LOGI("Receive unknown invalid gesture event, %02X\n", type);
		return 0;
	}
#endif // IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)

	return 0;
}

/**
 * @brief  Process the received events.
 *
 * @param
 *    [ in]    code:          the code of given report type
 *    [ in]    report:        report data given
 *    [ in]    report_size:   size of the given report
 *    [ in]    callback_data: private data being passed to the callback function
 *
 * @return
 *    on success, 0 or positive value; otherwise, negative value on error.
 */
static int syna_dev_process_events(const unsigned char code,
	const unsigned char *report, unsigned int report_size,
	void *callback_data)
{
	int retval, i;
	struct syna_tcm *tcm = (struct syna_tcm *)callback_data;
	int size_raw_data;
	struct tcm_dev *tcm_dev = tcm->tcm_dev;
	struct custom_fw_status *status;
	unsigned char *touch_data = 0;
	unsigned short touch_data_size = 0;
	unsigned char *heatmap_data_start = 0;
	unsigned char *heatmap_data = 0;
	unsigned short heatmap_data_size = 0;
	unsigned short crc16 = 0, crc_in_package = 0;

	tcm->abnormal_gesture_reported = false;

	if (!tcm) {
		LOGE("Invalid callback data\n");
		return -EINVAL;
	}

	/* If interrupts are disabled, top-half ISR didn't run. Use current time. */
	if (tcm->hw_if && !tcm->hw_if->bdata_attn.irq_enabled) {
		tcm->timestamp = ktime_get();
	}

	/* not report to input device subsystem if the cdev interface is in used */
	if ((tcm->char_dev_ref_count > 0) && !tcm->concurrent_reporting)
		return 0;

	if (code == REPORT_TOUCH) {
		if (tcm->enable_touch_crc) {
			crc_in_package = (unsigned short)syna_pal_le2_to_uint(&report[report_size - 2]);
			crc16 = syna_tcm_crc16((unsigned char *)report, report_size - 2, 0xFFFF);
			if (crc16 != crc_in_package) {
				LOGE("Mismatched CRC, crc16:%04X (%04X)\n", crc16, crc_in_package);
				return -EIO;
			}
		}
		/* parse touch report once received */
		retval = syna_tcm_parse_touch_report(tcm->tcm_dev,
				(unsigned char *)report,
				report_size,
				&tcm->tp_data);
		if (retval < 0) {
			LOGE("Fail to parse touch report\n");
			return retval;
		}

		/* report the touch event to system */
		syna_dev_report_input_events(tcm);
	} else if (code == tcm->raw_data_report_code) {
		size_raw_data = tcm_dev->rows * tcm_dev->cols + tcm_dev->rows + tcm_dev->cols;
		if (!tcm->raw_data_buffer) {
			tcm->raw_data_buffer = syna_pal_mem_alloc(size_raw_data, sizeof(u16));
			if (!tcm->raw_data_buffer) {
				LOGE("Allocate raw_data_buffer failed\n");
				return -ENOMEM;
			}
		}
		if (report_size == sizeof(u16) * size_raw_data) {
			syna_pal_mutex_lock(&tcm->raw_data_mutex);
			memcpy(tcm->raw_data_buffer, report, report_size);
			syna_pal_mutex_unlock(&tcm->raw_data_mutex);
			complete_all(&tcm->raw_data_completion);
		} else {
			LOGE("Raw data length: %d is incorrect.\n", report_size);
		}
	}

	/* handling the particular report data */
	switch (code) {
	case REPORT_HEAT_MAP:
		LOGD("Heat map data received, size:%d\n", report_size);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		syna_parse_heatmap(tcm, report, report_size);
#endif
		break;
	case REPORT_HEAT_MAP_WITH_METADATA:
		LOGD("Heat map data received, size:%d\n", report_size - SIMULATION_DATA_SIZE);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		syna_parse_heatmap(tcm, &report[SIMULATION_DATA_SIZE],
			report_size - SIMULATION_DATA_SIZE);
#endif
		break;
	case REPORT_FW_STATUS:
		if (tcm->enable_touch_crc) {
			crc_in_package = (unsigned short)syna_pal_le2_to_uint(&report[report_size - 2]);
			crc16 = syna_tcm_crc16((unsigned char *)report, report_size - 2, 0xFFFF);
			if (crc16 != crc_in_package) {
				LOGE("Mismatched CRC, crc16:%04X (%04X)\n", crc16, crc_in_package);
				return -EIO;
			}
		}
		/* for 'fw status' ($c2) report,
		 * report size shall be 2-byte only; the
		 */
		status = (struct custom_fw_status *)&report[0];
		LOGI("Status: moisture:%d noise:%d freq-change:%d, grip:%d, palm:%d, fast relax:%d\n",
			status->b0_moisture, status->b1_noise_state,
			status->b2_freq_hopping, status->b3_grip, status->b4_palm, status->b5_fast_relaxation);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		syna_notify_fw_status(tcm, status);
#endif
		break;
	case REPORT_TOUCH_AND_HEATMAP:
		if (tcm->enable_touch_crc) {
			crc_in_package = (unsigned short)syna_pal_le2_to_uint(&report[report_size - 2]);
			crc16 = syna_tcm_crc16((unsigned char *)report, report_size - 2, 0xFFFF);
			if (crc16 != crc_in_package) {
				LOGE("Mismatched CRC, crc16:%04X (%04X)\n", crc16, crc_in_package);
				return -EIO;
			}
		}
		/* touch data */
		touch_data_size = (report[1] << 8) | report[0];
		touch_data = (unsigned char *)&report[2];

		retval = syna_tcm_parse_touch_report(tcm->tcm_dev,
				touch_data,
				touch_data_size,
				&tcm->tp_data);
		if (retval < 0) {
			LOGE("Fail to parse touch report\n");
			return retval;
		}
		/* forward the touch event to system */
		syna_dev_report_input_events(tcm);

		/* heatmap data */
		heatmap_data_start = touch_data + touch_data_size;
		heatmap_data_size = (heatmap_data_start[1] << 8) | heatmap_data_start[0];
		heatmap_data = touch_data + touch_data_size + 2;
		LOGD("$c5 Heat map data received, size:%d\n", heatmap_data_size);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		syna_parse_heatmap(tcm, heatmap_data, heatmap_data_size);
#endif
		break;
	case REPORT_TOUCH_AND_HEATMAP_WITH_METADATA:
		if (tcm->enable_touch_crc) {
			crc_in_package = (unsigned short)syna_pal_le2_to_uint(&report[report_size - 2]);
			crc16 = syna_tcm_crc16((unsigned char *)report, report_size - 2, 0xFFFF);
			if (crc16 != crc_in_package) {
				LOGE("Mismatched CRC, crc16:%04X (%04X)\n", crc16, crc_in_package);
				return -EIO;
			}
		}
		/* touch data */
		touch_data_size = (report[1] << 8) | report[0];
		touch_data = (unsigned char *)&report[2];

		retval = syna_tcm_parse_touch_report(tcm->tcm_dev,
				touch_data,
				touch_data_size,
				&tcm->tp_data);
		if (retval < 0) {
			LOGE("Fail to parse touch report\n");
			return retval;
		}
		/* forward the touch event to system */
		syna_dev_report_input_events(tcm);

		/* heatmap data */
		heatmap_data_start = touch_data + touch_data_size;
		heatmap_data_size = (heatmap_data_start[1] << 8) | heatmap_data_start[0];
		heatmap_data = touch_data + touch_data_size + 2;
		heatmap_data_size -= SIMULATION_DATA_SIZE;
		LOGD("$%2X Heat map data received, size:%d\n",
			REPORT_TOUCH_AND_HEATMAP_WITH_METADATA, heatmap_data_size);

		memcpy(tcm->simulation_data, heatmap_data, SIMULATION_DATA_SIZE);

		if (tcm->abnormal_gesture_reported) {
			for (i = 0; i < SIMULATION_DATA_SIZE; i+=30) {
				LOGI("simdata:%*phN", min(30, SIMULATION_DATA_SIZE - i),
						heatmap_data + i);
			}
		}

		if (tcm->sysfs_debug_simulation_data) {
			LOGI("simdata=");
			for (i = 0; i < report_size; i+=30)
				LOGI("%*phN", min(30, report_size - i), report + i);
		}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		syna_parse_heatmap(tcm, &heatmap_data[SIMULATION_DATA_SIZE], heatmap_data_size);
#endif
		break;
	default:
		break;
	}

	return 0;
}
/**
 * @brief  Allocate an input device and set up relevant parameters to the
 *         input subsystem.
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_create_input_device(struct syna_tcm *tcm)
{
	int retval = 0;
	struct tcm_dev *tcm_dev = tcm->tcm_dev;
	struct input_dev *input_dev = NULL;
#ifdef DEV_MANAGED_API
	struct device *dev = syna_request_managed_device();

	if (!dev) {
		LOGE("Invalid managed device\n");
		return -EINVAL;
	}

	input_dev = devm_input_allocate_device(dev);
#else /* Legacy API */
	input_dev = input_allocate_device();
#endif
	if (input_dev == NULL) {
		LOGE("Fail to allocate input device\n");
		return -ENODEV;
	}

	input_dev->name = TOUCH_INPUT_NAME;
	input_dev->phys = TOUCH_INPUT_PHYS_PATH;
	input_dev->uniq = "google_touchscreen";
	input_dev->id.product = SYNAPTICS_TCM_DRIVER_ID;
	input_dev->id.version = SYNAPTICS_TCM_DRIVER_VERSION;
	input_dev->dev.parent = tcm->pdev->dev.parent;
	input_set_drvdata(input_dev, tcm);

	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_ABS, input_dev->evbit);
	set_bit(BTN_TOUCH, input_dev->keybit);
	set_bit(BTN_TOOL_FINGER, input_dev->keybit);
#ifdef INPUT_PROP_DIRECT
	set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
#endif

#ifdef ENABLE_WAKEUP_GESTURE
	set_bit(KEY_WAKEUP, input_dev->keybit);
	input_set_capability(input_dev, EV_KEY, KEY_WAKEUP);
#endif

	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, tcm_dev->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, tcm_dev->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);

	input_mt_init_slots(input_dev, tcm_dev->max_objects, INPUT_MT_DIRECT);

#ifdef REPORT_TOUCH_WIDTH
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, tcm_dev->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR, 0, tcm_dev->max_y, 0, 0);
#ifdef ENABLE_CUSTOM_TOUCH_ENTITY
	input_set_abs_params(input_dev, ABS_MT_ORIENTATION, -4096, 4096, 0, 0);
#endif
#endif
	input_set_abs_params(input_dev, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER, MT_TOOL_PALM, 0, 0);

	tcm->input_dev_params.max_x = tcm_dev->max_x;
	tcm->input_dev_params.max_y = tcm_dev->max_y;
	tcm->input_dev_params.max_objects = tcm_dev->max_objects;

	retval = goog_input_register_device(&tcm->pdev->dev, input_dev);
	if (retval < 0) {
		LOGE("Fail to register input device\n");
		input_free_device(input_dev);
		input_dev = NULL;
		return retval;
	}

	tcm->input_dev = input_dev;

	return 0;
}
/**
 * @brief  Release the input device allocated previously.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    void.
 */
static void syna_dev_release_input_device(struct syna_tcm *tcm)
{
	if (!tcm->input_dev)
		return;

	goog_input_unregister_device(&tcm->pdev->dev, tcm->input_dev);

	tcm->input_dev = NULL;
}
/**
 * @brief  Check whether it's need to register the new input device
 *         if any of parameters has changed.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    true if parameters are mismatched, false otherwise.
 */
static bool syna_dev_check_input_params(struct syna_tcm *tcm)
{
	struct tcm_dev *tcm_dev = tcm->tcm_dev;

	if (tcm_dev->max_x == 0 && tcm_dev->max_y == 0)
		return false;

	if (tcm->input_dev_params.max_x != tcm_dev->max_x)
		return true;

	if (tcm->input_dev_params.max_y != tcm_dev->max_y)
		return true;

	if (tcm->input_dev_params.max_objects != tcm_dev->max_objects)
		return true;

	if (tcm_dev->max_objects > MAX_NUM_OBJECTS) {
		LOGW("Out of max num objects defined, in app_info: %d\n",
			tcm_dev->max_objects);
		return false;
	}

	LOGN("Input parameters non-changed\n");
	return false;
}

/**
 * @brief  Set up input device to the input subsystem.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 in case of success, a negative value otherwise.
 */
static int syna_dev_set_up_input_device(struct syna_tcm *tcm)
{
	int retval = 0;

	if (IS_NOT_APP_FW_MODE(tcm->tcm_dev->dev_mode)) {
		LOGN("Application firmware not running, current mode: %02x\n",
			tcm->tcm_dev->dev_mode);
		return 0;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (tcm->gti)
		goog_input_lock(tcm->gti);
#else
	syna_dev_free_input_events(tcm);

	syna_pal_mutex_lock(&tcm->tp_event_mutex);
#endif

	if (!syna_dev_check_input_params(tcm))
		goto exit;

	if (tcm->input_dev != NULL)
		syna_dev_release_input_device(tcm);

	retval = syna_dev_create_input_device(tcm);
	if (retval < 0) {
		LOGE("Fail to create input device\n");
		goto exit;
	}

exit:
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (tcm->gti)
		goog_input_unlock(tcm->gti);
#else
	syna_pal_mutex_unlock(&tcm->tp_event_mutex);
#endif

	return retval;
}

/**
 * @brief  Interrupt handling routine (top-half).
 *
 *         Function is called when the interrupt is asserted.
 *         The purposes of this handler is to store the timestamp of assertion.
 * @param
 *    [ in] irq:  interrupt line
 *    [ in] data: private data being passed to the handler function
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static irqreturn_t syna_dev_isr(int irq, void *data)
{
	struct syna_tcm *tcm = data;

	tcm->timestamp = ktime_get();

	return IRQ_WAKE_THREAD;
}

/**
 * @brief  Interrupt handling routine.
 *
 *         Function is called when the interrupt is asserted.
 *         The purposes of this handler is to read out an event generated by device.
 * @param
 *    [ in] irq:  interrupt line
 *    [ in] data: private data being passed to the handler function
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static irqreturn_t syna_dev_interrupt_thread(int irq, void *data)
{
	int retval;
	unsigned char code = 0;
	struct syna_tcm *tcm = (struct syna_tcm *)data;
	struct syna_hw_attn_data *attn = &tcm->hw_if->bdata_attn;

	if (unlikely(gpio_get_value(attn->irq_gpio) != attn->irq_on_state))
		goto exit;

	tcm->isr_pid = current->pid;

	/* retrieve the original report date generated by firmware */
	retval = syna_tcm_get_event_data(tcm->tcm_dev,
			&code,
			NULL);
	if (retval < 0) {
		LOGE("Fail to get event data\n");
		goto exit;
	}

exit:
	return IRQ_HANDLED;
}

/**
 * @brief  Request interrupt line and register the interrupt handling routine.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_request_irq(struct syna_tcm *tcm)
{
	int retval;
	struct syna_hw_attn_data *attn = &tcm->hw_if->bdata_attn;
#ifdef DEV_MANAGED_API
	struct device *dev = syna_request_managed_device();

	if (!dev) {
		LOGE("Invalid managed device\n");
		retval = -EINVAL;
		goto exit;
	}
#endif

	if (attn->irq_gpio < 0) {
		LOGE("Invalid IRQ GPIO\n");
		retval = -EINVAL;
		goto exit;
	}

	attn->irq_id = gpio_to_irq(attn->irq_gpio);

#ifdef DEV_MANAGED_API
	retval = devm_request_threaded_irq(dev,
			attn->irq_id,
			syna_dev_isr,
			syna_dev_interrupt_thread,
			attn->irq_flags,
			PLATFORM_DRIVER_NAME,
			tcm);
#else /* Legacy API */
	retval = request_threaded_irq(attn->irq_id,
			syna_dev_isr,
			syna_dev_interrupt_thread,
			attn->irq_flags,
			PLATFORM_DRIVER_NAME,
			tcm);
#endif
	if (retval < 0) {
		LOGE("Fail to request threaded irq\n");
		goto exit;
	}

	attn->irq_enabled = true;

	LOGI("Interrupt handler registered\n");

exit:
	return retval;
}

/**
 * @brief  Release an interrupt line allocated previously
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    void.
 */
static void syna_dev_release_irq(struct syna_tcm *tcm)
{
	struct tcm_hw_platform *hw = &tcm->hw_if->hw_platform;
	struct syna_hw_attn_data *attn = &tcm->hw_if->bdata_attn;
#ifdef DEV_MANAGED_API
	struct device *dev = syna_request_managed_device();

	if (!dev) {
		LOGE("Invalid managed device\n");
		return;
	}
#endif

	if (attn->irq_id <= 0)
		return;

	if (hw->ops_enable_attn)
		hw->ops_enable_attn(hw, false);

#ifdef DEV_MANAGED_API
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (tcm->gti)
		goog_devm_free_irq(tcm->gti, &tcm->pdev->dev, attn->irq_id);
	else
		devm_free_irq(dev, attn->irq_id, tcm);
#else
	devm_free_irq(dev, attn->irq_id, tcm);
#endif
#else
	free_irq(attn->irq_id, tcm);
#endif

	attn->irq_id = 0;
	attn->irq_enabled = false;

	LOGI("Interrupt handler released\n");
}

static int syna_dev_register_dispatcher(struct syna_tcm *tcm)
{
	int retval = 0;

	if (!tcm)
		return -EINVAL;

	/* register the handling function for touch reports */
	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_TOUCH, syna_dev_process_events, (void *)tcm);
	if (retval < 0)
		LOGE("Fail to register the touch report handling function\n");

	/* register the handling function for custom reports */
	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_DELTA, syna_dev_process_events, (void *)tcm);
	if (retval < 0)
		LOGE("Fail to register the delta report handling function\n");

	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_RAW, syna_dev_process_events, (void *)tcm);
	if (retval < 0)
		LOGE("Fail to register the raw report handling function\n");

	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_BASELINE, syna_dev_process_events, (void *)tcm);
	if (retval < 0)
		LOGE("Fail to register the baseline report handling function\n");

	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_FW_STATUS, syna_dev_process_events, (void *)tcm);
	if (retval < 0)
		LOGE("Fail to register the report 0x%02X handling function\n", REPORT_FW_STATUS);
	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_HEAT_MAP, syna_dev_process_events, (void *)tcm);
	if (retval < 0)
		LOGE("Fail to register the report 0x%02X handling function\n", REPORT_HEAT_MAP);
	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_TOUCH_AND_HEATMAP, syna_dev_process_events, (void *)tcm);
	if (retval < 0) {
		LOGE("Fail to register the report 0x%02X handling function\n",
				REPORT_TOUCH_AND_HEATMAP);
	}
	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_HEAT_MAP_WITH_METADATA, syna_dev_process_events, (void *)tcm);
	if (retval < 0) {
		LOGE("Fail to register the report 0x%02X handling function\n",
				REPORT_HEAT_MAP_WITH_METADATA);
	}
	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev,
			REPORT_TOUCH_AND_HEATMAP_WITH_METADATA,
			syna_dev_process_events, (void *)tcm);
	if (retval < 0) {
		LOGE("Fail to register the report 0x%02X handling function\n",
				REPORT_TOUCH_AND_HEATMAP_WITH_METADATA);
	}
	retval = syna_tcm_set_report_dispatcher(tcm->tcm_dev, REPORT_INVALID_GESTURE_EVENT,
						syna_dev_process_invalid_gesture_events,
						(void *)tcm);
	if (retval < 0) {
		LOGE("Fail to register the report 0x%02X handling function\n",
		     REPORT_INVALID_GESTURE_EVENT);
	}

	return retval;
}

/**
 * @brief  Initialization including the preparation of app info and the
 *         configuration of touch report.
 * @param
 *    [ in] tcm: tcm driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_set_up_app_fw(struct syna_tcm *tcm)
{
	int retval = 0;
	struct tcm_dev *tcm_dev;
	struct syna_hw_attn_data *attn;
	unsigned int resp_handling;

	if (!tcm)
		return -EINVAL;

	tcm_dev = tcm->tcm_dev;
	attn = &tcm->hw_if->bdata_attn;
	if (attn && (attn->irq_id) && (attn->irq_enabled))
		resp_handling = CMD_RESPONSE_IN_ATTN;
	else
		resp_handling = tcm_dev->msg_data.command_polling_time;

	if (IS_NOT_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGN("Application firmware not running, current mode: %02x\n",
			tcm_dev->dev_mode);
		return -EINVAL;
	}

	/* collect app info containing most of sensor information */
	retval = syna_tcm_get_app_info(tcm_dev, &tcm_dev->app_info, resp_handling);
	if (retval < 0) {
		LOGE("Fail to get application info\n");
		return retval;
	}

#ifdef USE_CUSTOM_TOUCH_REPORT_CONFIG
	/* set up the format of touch report */
	retval = syna_tcm_set_touch_report_config(tcm_dev,
			custom_touch_format,
			(unsigned int)sizeof(custom_touch_format),
			resp_handling);
	if (retval < 0) {
		LOGE("Fail to setup the custom touch report format\n");
		return retval;
	}
#endif
	/* preserve the format of touch report */
	retval = syna_tcm_preserve_touch_report_config(tcm_dev, resp_handling);
	if (retval < 0) {
		LOGE("Fail to preserve touch report config\n");
		return retval;
	}

#ifdef ENABLE_CUSTOM_TOUCH_ENTITY
	/* set up custom touch data parsing method */
	retval = syna_tcm_set_custom_touch_entity_callback(tcm_dev,
			syna_dev_parse_custom_touch_data_cb,
			(void *)tcm);
	if (retval < 0) {
		LOGE("Fail to set up custom touch data parsing method\n");
		return retval;
	}
#endif
#ifdef ENABLE_WAKEUP_GESTURE
	/* set up custom gesture parsing method */
	retval = syna_tcm_set_custom_gesture_callback(tcm_dev,
			syna_dev_parse_custom_gesture_cb,
			(void *)tcm);
	if (retval < 0) {
		LOGE("Fail to set up custom gesture parsing method\n");
		return retval;
	}
#endif
	return 0;
}

#ifdef STARTUP_REFLASH
/**
 * @brief  Example to perform firmware update at the system startup.
 *
 * @param
 *    [ in] work: handle of work structure
 *
 * @return
 *    void.
 */
static void syna_dev_reflash_startup_work(struct work_struct *work)
{
	int retval, i;
	int suffix_fw_name_count = 0;
	struct delayed_work *delayed_work;
	struct syna_tcm *tcm;
	struct tcm_dev *tcm_dev;
	struct device_node *np;
	struct property *prop;
	const struct firmware *fw_entry = NULL;
	const unsigned char *fw_image = NULL;
	const char *suffix_fw_name = NULL;
	unsigned int fw_image_size;
	unsigned char orig_mode;
	struct tcm_features_info features;

	delayed_work = container_of(work, struct delayed_work, work);
	tcm = container_of(delayed_work, struct syna_tcm, reflash_work);

	tcm_dev = tcm->tcm_dev;
	np = tcm->pdev->dev.parent->of_node;

	orig_mode = tcm_dev->dev_mode;

	pm_stay_awake(&tcm->pdev->dev);

	/* Use CPU mode for the firmware update because it cannot fit the 4 bytes alignment.*/
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) && IS_ENABLED(CONFIG_SPI_S3C64XX_GS)
	if (goog_check_spi_dma_enabled(tcm->hw_if->pdev) && tcm->hw_if->s3c64xx_sci) {
		tcm->hw_if->hw_platform.ops_disable_attn_sync(&tcm->hw_if->hw_platform);
		tcm->hw_if->dma_mode = 0;
		tcm->hw_if->s3c64xx_sci->dma_mode = CPU_MODE;
		tcm->hw_if->hw_platform.ops_enable_attn(&tcm->hw_if->hw_platform, true);
	}
#endif

	prop = of_find_property(np, "synaptics,suffix-fw-name", NULL);
	if (prop && prop->length) {
		suffix_fw_name_count = of_property_count_strings(np, "synaptics,suffix-fw-name");
		for (i = 0; i < suffix_fw_name_count; i++) {
			of_property_read_string_index(np, "synaptics,suffix-fw-name", i,
					&suffix_fw_name);
			if (strncmp(tcm_dev->id_info.part_number, suffix_fw_name,
					strlen(suffix_fw_name)) == 0) {
				strcat(tcm->hw_if->fw_name, "_");
				strcat(tcm->hw_if->fw_name, suffix_fw_name);
				break;
			}
		}
	}

	if (syna_pal_str_len(tcm->hw_if->fw_name) == 0)
		scnprintf(tcm->hw_if->fw_name, sizeof(tcm->hw_if->fw_name), "%s", FW_IMAGE_NAME);

	LOGI("Firmware name %s for %*pE", tcm->hw_if->fw_name,
			(int)sizeof(tcm_dev->id_info.part_number), tcm_dev->id_info.part_number);

	/* for 3916TG, the erase time may be longer */
	if (strncmp(tcm_dev->id_info.part_number, "S3916TG", 7) == 0) {
		syna_tcm_config_timings(tcm_dev, NULL, 10000, TIMINGS_CMD_TIMEOUT);
	}

	/* get firmware image */
	retval = request_firmware(&fw_entry,
			tcm->hw_if->fw_name,
			tcm->pdev->dev.parent);
	if (retval < 0) {
		LOGE("Fail to request %s\n", tcm->hw_if->fw_name);
		if (tcm->input_dev)
			goto skip_fw_update;
		else
			goto exit;
	}

	fw_image = fw_entry->data;
	fw_image_size = fw_entry->size;

	LOGD("Firmware image size = %d\n", fw_image_size);

	/* perform fw update */
#ifdef TDDI_PRODUCTS
	retval = syna_tcm_tddi_do_fw_update(tcm_dev,
			fw_image,
			fw_image_size,
			CMD_RESPONSE_IN_ATTN,
			tcm->force_reflash,
			tcm->is_tddi_multichip);
#else
	/* do firmware update for the common device */
	retval = syna_tcm_do_fw_update(tcm_dev,
			fw_image,
			fw_image_size,
			CMD_RESPONSE_IN_ATTN,
			tcm->force_reflash);
#endif
	/* Restore DMA mode */
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) && IS_ENABLED(CONFIG_SPI_S3C64XX_GS)
	tcm->hw_if->hw_platform.ops_disable_attn_sync(&tcm->hw_if->hw_platform);
	if (goog_check_spi_dma_enabled(tcm->hw_if->pdev) && tcm->hw_if->s3c64xx_sci) {
		tcm->hw_if->dma_mode = 1;
		tcm->hw_if->s3c64xx_sci->dma_mode = DMA_MODE;
	}
	/* Wait 300ms to make sure the SPI driver suspends so that it will
	 * acquire the DMA channel when it resumes next time because DMA_MODE
	 * was enabled.
	 */
	msleep(300);
	tcm->hw_if->hw_platform.ops_enable_attn(&tcm->hw_if->hw_platform, true);
#endif
	if (retval < 0) {
		LOGE("Fail to do reflash, reflash_count = %d\n", tcm->reflash_count);
		tcm->force_reflash = true;
		if (tcm->reflash_count++ < 3) {
			queue_delayed_work(tcm->reflash_workqueue, &tcm->reflash_work,
				msecs_to_jiffies(STARTUP_REFLASH_DELAY_TIME_MS));
		}
		goto exit;
	}

	syna_dev_ensure_reset_done(tcm);

	/* re-initialize the app fw */
	retval = syna_dev_set_up_app_fw(tcm);
	if (retval < 0) {
		LOGE("Fail to set up app fw after fw update\n");
		goto exit;
	}

	if (IS_APP_FW_MODE(tcm_dev->dev_mode) && (orig_mode != tcm_dev->dev_mode))
		syna_dev_register_dispatcher(tcm);

	/* ensure the settings of input device
	 * if needed, re-create a new input device
	 */
	retval = syna_dev_set_up_input_device(tcm);
	if (retval < 0) {
		LOGE("Fail to register input device\n");
		goto exit;
	}

	/* Force to reload features and show config info after FW upgrade. */
	tcm->enable_touch_crc = false;
	retval = syna_tcm_get_features(tcm_dev, &features, CMD_RESPONSE_IN_ATTN);
	if (retval >= 0)
		tcm->enable_touch_crc = features.byte[8] & 0x01;

	syna_dev_show_info(tcm);

skip_fw_update:
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	syna_gti_init(tcm);
#endif
exit:
	fw_image = NULL;

	if (fw_entry) {
		release_firmware(fw_entry);
		fw_entry = NULL;
	}

	syna_tcm_config_timings(tcm_dev, NULL,
		tcm->hw_if->product.default_cmd_timeout_ms,
		TIMINGS_CMD_TIMEOUT);

	pm_relax(&tcm->pdev->dev);
}
#endif
#if defined(LOW_POWER_MODE)
/**
 * @brief  Enable or disable the low power gesture mode.
 *
 * @param
 *    [ in] tcm:           tcm driver handle
 *    [ in] en:            '1' to enable low power gesture mode; '0' to disable
 *    [ in] resp_handling: set up the handling of response to command
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_enable_lowpwr_gesture(struct syna_tcm *tcm, bool en,
	unsigned int resp_handling)
{
	int retval = 0;
	unsigned short config;
	struct syna_hw_attn_data *attn;

	if (!tcm)
		return -EINVAL;

	attn = &tcm->hw_if->bdata_attn;

	if (!tcm->lpwg_enabled)
		return 0;

	if (en) {
		if (!tcm->irq_wake) {
			enable_irq_wake(attn->irq_id);
			tcm->irq_wake = true;
		}
		config = 1;
	} else {
		if (tcm->irq_wake) {
			disable_irq_wake(attn->irq_id);
			tcm->irq_wake = false;
		}
		config = 0;
	}

	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_ENABLE_WAKEUP_GESTURE_MODE,
			config,
			resp_handling);
	if (retval < 0) {
		LOGE("Fail to %s wakeup gesture via dynamic config command\n",
			(en) ? "enable" : "disable");
		return retval;
	}

	return retval;
}
#endif
#if defined(LOW_POWER_MODE) && !defined(RESET_ON_RESUME)
/**
 * @brief  Enter normal sensing mode
 *
 * @param
 *    [ in] tcm: tcm driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_enter_normal_sensing(struct syna_tcm *tcm)
{
	int retval = 0;
	struct syna_hw_attn_data *attn;
	struct tcm_dev *tcm_dev;
	unsigned int resp_handling;

	if (!tcm)
		return -EINVAL;

	tcm_dev = tcm->tcm_dev;
	attn = &tcm->hw_if->bdata_attn;
	if ((attn->irq_id) && (attn->irq_enabled))
		resp_handling = CMD_RESPONSE_IN_ATTN;
	else
		resp_handling = tcm_dev->msg_data.command_polling_time;


	if (!tcm->gti || !goog_check_late_sense_on_enabled(tcm->gti)) {
		/* bring out of sleep mode. */
		retval = syna_tcm_sleep(tcm->tcm_dev, false, resp_handling);
		if (retval < 0) {
			LOGE("Fail to exit deep sleep\n");
			return retval;
		}
	}

	/* disable low power gesture mode, if needed */
	if (tcm->lpwg_enabled) {
		retval = syna_dev_enable_lowpwr_gesture(tcm, false, resp_handling);
		if (retval < 0) {
			LOGE("Fail to disable low power gesture mode\n");
			return retval;
		}
	}

#ifdef GOOG_INT2_FEATURE
	tcm->tcm_dev->msg_data.enable_response_log = true;
	retval = syna_tcm_set_dynamic_config(tcm->tcm_dev,
			DC_ENABLE_WAKEUP_GESTURE_MODE,
			0,
			resp_handling);
	tcm->tcm_dev->msg_data.enable_response_log = false;
	if (retval < 0) {
		LOGE("Fail to exit low power gesture mode\n");
		return retval;
	}
	LOGI("Exit gesture mode.");
#endif

	return 0;
}
#endif
#ifdef LOW_POWER_MODE
/**
 * @brief  Enter low-power saving mode, either the lower power gesture
 *         mode or deep sleep mode.
 * @param
 *    [ in] tcm: tcm driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_enter_lowpwr_sensing(struct syna_tcm *tcm)
{
	int retval = 0;
	struct syna_hw_attn_data *attn;
	struct tcm_dev *tcm_dev;
	unsigned int resp_handling;

	if (!tcm)
		return -EINVAL;

	tcm_dev = tcm->tcm_dev;
	attn = &tcm->hw_if->bdata_attn;
	/* */
	if ((attn->irq_id) && (attn->irq_enabled))
		resp_handling = CMD_RESPONSE_IN_ATTN;
	else
		resp_handling = tcm_dev->msg_data.command_polling_time;

	/* enable low power gesture mode, if needed */
	if (tcm->lpwg_enabled) {
		retval = syna_dev_enable_lowpwr_gesture(tcm, true, resp_handling);
		if (retval < 0) {
			LOGE("Fail to disable low power gesture mode\n");
			return retval;
		}
	} else {
	/* enter sleep mode for non-LPWG cases */
		if (!tcm->slept_in_early_suspend) {
			retval = syna_tcm_sleep(tcm->tcm_dev, true, resp_handling);
			if (retval < 0) {
				LOGE("Fail to enter deep sleep\n");
				return retval;
			}
		}
	}

	return 0;
}

/**
 * @brief  Check whether the touch is in normal scan mode. If not, retry to enter
 *         normal scan mode.
 * @param
 *    [ in] tcm: tcm driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_check_normal_scan_mode(struct syna_tcm *tcm, unsigned int resp_handling)
{
	int ret = 0;
	int retry = 0;
	unsigned short scan_mode = SCAN_SLEEP;

	while (retry++ < 3) {
		ret = syna_tcm_get_dynamic_config(tcm->tcm_dev,
				DC_TOUCH_SCAN_MODE,
				&scan_mode,
				resp_handling);
		if(ret < 0) {
			LOGE("Failed to get DC_TOUCH_SCAN_MODE ret: %d", ret);
			msleep(20);
			continue;
		}

		LOGI("DC_TOUCH_SCAN_MODE is %u", scan_mode);

		switch (scan_mode) {
		case SCAN_NORMAL_IDLE:
		case SCAN_NORMAL_ACTIVE:
			return 0;
		case SCAN_SLEEP:
			ret = syna_tcm_sleep(tcm->tcm_dev, false, resp_handling);
			fallthrough;
		case SCAN_LPWG_IDLE:
		case SCAN_LPWG_ACTIVE:
			tcm->tcm_dev->msg_data.enable_response_log = true;
			ret = syna_tcm_set_dynamic_config(tcm->tcm_dev,
					DC_ENABLE_WAKEUP_GESTURE_MODE,
					0,
					resp_handling);
			tcm->tcm_dev->msg_data.enable_response_log = false;
			break;
		default:
			LOGE("Invalid scan mode %d", scan_mode);
			ret = -EINVAL;
			break;
		}
		msleep(20);
	}

	return ret;
}
#endif

static int syna_pinctrl_configure(struct syna_tcm *tcm, bool enable)
{
	struct pinctrl_state *state;

	if (IS_ERR_OR_NULL(tcm->pinctrl)) {
		LOGE("Invalid pinctrl!\n");
		return -EINVAL;
	}

	LOGD("%s\n", enable ? "ACTIVE" : "SUSPEND");

	if (enable) {
		state = pinctrl_lookup_state(tcm->pinctrl, "ts_active");
		if (IS_ERR(state))
			LOGE("Could not get ts_active pinstate!\n");
	} else {
		state = pinctrl_lookup_state(tcm->pinctrl, "ts_suspend");
		if (IS_ERR(state))
			LOGE("Could not get ts_suspend pinstate!\n");
	}

	if (!IS_ERR_OR_NULL(state))
		return pinctrl_select_state(tcm->pinctrl, state);

	return 0;
}

#if defined(ENABLE_DISP_NOTIFIER)
#if defined(USE_FB)
/**
 * @brief  Receive the early suspend event from the display.
 *
 * @param
 *    [ in] dev: pointer to device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_early_suspend(struct device *dev)
{
	int retval;
	struct syna_tcm *tcm = dev_get_drvdata(dev);

	/* exit directly if device is already in suspend state */
	if (tcm->pwr_state != PWR_ON)
		return 0;

	if (!tcm->lpwg_enabled) {
		retval = syna_tcm_sleep(tcm->tcm_dev, true);
		if (retval < 0) {
			LOGE("Fail to enter deep sleep\n");
			return retval;
		}
	}

	tcm->slept_in_early_suspend = true;

	return 0;
}
/**
 * @brief  Catch the screen on/off event from display.
 *
 * @param
 *    [ in] nb:     pointer to notifier_block
 *    [ in] action: fb action
 *    [ in] data:   private data for callback
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_disp_notifier_cb(struct notifier_block *nb,
	unsigned long action, void *data)
{
	int retval;
	int transition;
	struct fb_event *evdata = data;
	struct syna_tcm *tcm = container_of(nb, struct syna_tcm, fb_notifier);
	int time = 0;
	int disp_blank_powerdown;
	int disp_early_event_blank;
	int disp_blank;
	int disp_blank_unblank;

	if (!evdata || !evdata->data || !tcm)
		return 0;

	retval = 0;

	disp_blank_powerdown = FB_BLANK_POWERDOWN;
	disp_early_event_blank = FB_EARLY_EVENT_BLANK;
	disp_blank = FB_EVENT_BLANK;
	disp_blank_unblank = FB_BLANK_UNBLANK;

	transition = *(int *)evdata->data;

	/* confirm the firmware flashing is completed before screen off */
	if (transition == disp_blank_powerdown) {
		while (ATOMIC_GET(tcm->tcm_dev->firmware_flashing)) {
			syna_pal_sleep_ms(500);

			time += 500;
			if (time >= 5000) {
				LOGE("Timed out waiting for re-flashing\n");
				ATOMIC_SET(tcm->tcm_dev->firmware_flashing, 0);
				return -ETIMEDOUT;
			}
		}
	}

	if (action == disp_early_event_blank &&
		transition == disp_blank_powerdown) {
		retval = syna_dev_early_suspend(&tcm->pdev->dev);
	} else if (action == disp_blank) {
		if (transition == disp_blank_powerdown) {
			retval = tcm->dev_suspend(&tcm->pdev->dev);
			tcm->fb_ready = 0;
		} else if (transition == disp_blank_unblank) {
#ifndef RESUME_EARLY_UNBLANK
			retval = tcm->dev_resume(&tcm->pdev->dev);
			tcm->fb_ready++;
#endif
		} else if (action == disp_early_event_blank &&
			transition == disp_blank_unblank) {
#ifdef RESUME_EARLY_UNBLANK
			retval = tcm->dev_resume(&tcm->pdev->dev);
			tcm->fb_ready++;
#endif
		}
	}

	return 0;
}
#endif
#endif

#define CLEAR_EVENTS_MAX_RETRIES 3
#define READ_EVENTS_MAX_ATTEMPTS 10

static int syna_dev_clear_pending_events(struct syna_tcm *tcm)
{
	int i, j;
	int retval = 0;
	unsigned char code = 0;
	struct syna_hw_attn_data *attn = &tcm->hw_if->bdata_attn;

	if (!tcm || !tcm->hw_if)
		return -EINVAL;

	if (gpio_get_value(attn->irq_gpio) != attn->irq_on_state) {
		LOGI("INT line high initially, no pending messages.");
		return 0;
	}

	for (i = 0; i < CLEAR_EVENTS_MAX_RETRIES; i++) {
		LOGI("INT line low (Clear attempt %d), sleeping and then reading events...", i + 1);

		msleep(100);

		for (j = 0; j < READ_EVENTS_MAX_ATTEMPTS; j++) {
			retval = syna_tcm_get_event_data(tcm->tcm_dev, &code, NULL);
			if (retval < 0) {
				LOGE("Failed to get event data (Read event attempt %d), ret %d",
				     j + 1, retval);
				return retval;
			}

			LOGI("Read event attempt %d: got event code 0x%02x", j + 1, code);

			if (code == STATUS_IDLE) {
				break;
			}
		}

		if (j == READ_EVENTS_MAX_ATTEMPTS) {
			LOGE("Failed to get STATUS_IDLE within %d read event attempts",
			     READ_EVENTS_MAX_ATTEMPTS);
			return -ETIMEDOUT;
		}

		// Check INT status after reading events
		if (gpio_get_value(attn->irq_gpio) != attn->irq_on_state) {
			LOGI("INT line high after event reads on clear attempt %d, pending messages cleared.",
			     i + 1);
			return 0;
		} else {
			LOGW("INT line still low at end of clear attempt %d", i + 1);
		}
	}

	LOGE("Failed to clear all pending messages after %d clear attempts, INT line still low.",
	     CLEAR_EVENTS_MAX_RETRIES);
	return -ETIMEDOUT;
}

/**
 * @brief  Resume from the suspend state.
 *
 * @param
 *    [ in] dev: pointer to device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_resume(struct device *dev)
{
	int retval = 0;
	struct syna_tcm *tcm = dev_get_drvdata(dev);
	struct tcm_dev *tcm_dev;
	struct syna_hw_interface *hw_if;
	struct syna_hw_attn_data *attn;
	unsigned int resp_handling;
#ifdef RESET_ON_RESUME
	int retry = 0;
	unsigned char status;
#endif

	if (!tcm)
		return -EINVAL;

	tcm_dev = tcm->tcm_dev;
	hw_if = tcm->hw_if;
	attn = &hw_if->bdata_attn;
	if (attn && (attn->irq_id) && (attn->irq_enabled))
		resp_handling = CMD_RESPONSE_IN_ATTN;
	else
		resp_handling = tcm_dev->msg_data.command_polling_time;

	/* exit directly if device isn't in suspend state */
	if (tcm->pwr_state == PWR_ON)
		return 0;

	LOGI("Prepare to resume device\n");

	syna_pinctrl_configure(tcm, true);

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* clear all input events  */
	syna_dev_free_input_events(tcm);
#endif

#ifdef RESET_ON_RESUME
	LOGI("Do reset on resume\n");
	syna_pal_sleep_ms(RESET_ON_RESUME_DELAY_MS);

	for (retry = 0; retry < 3; retry++) {
		if (hw_if->ops_hw_reset) {
			hw_if->ops_hw_reset(hw_if);

			/* manually read in the event after reset if attn is disabled */
			if (!attn->irq_enabled) {
				retval = syna_tcm_get_event_data(tcm->tcm_dev,
					&status, NULL);
				if ((retval < 0) || (status != REPORT_IDENTIFY)) {
					LOGE("Fail to complete hw reset, ret = %d, status = %d\n",
						retval, status);
					continue;
				}
			}
			break;
		} else {
			retval = syna_tcm_reset(tcm->tcm_dev, resp_handling);
			if (retval < 0) {
				LOGE("Fail to do sw reset, ret = %d\n", retval);
				continue;
			}
			break;
		}
	}
	if (retval < 0 || (hw_if->ops_hw_reset && (status != REPORT_IDENTIFY))) {
		goto exit;
	}
#else
	// Flush pending events from incomplete commands to ensure state synchronization.
	retval = syna_dev_clear_pending_events(tcm);
	if (retval < 0) {
		LOGE("Fail to clear pending event ret = %d", retval);
		goto exit;
	}

#ifdef LOW_POWER_MODE
	/* enter normal power mode */
	retval = syna_dev_enter_normal_sensing(tcm);
	if (retval < 0) {
		LOGE("Fail to enter normal power mode\n");
		goto exit;
	}
#endif
#ifndef GOOG_INT2_FEATURE
	retval = syna_tcm_rezero(tcm->tcm_dev, resp_handling);
	if (retval < 0) {
		LOGE("Fail to rezero\n");
		goto exit;
	}
#endif
#endif
	tcm->pwr_state = PWR_ON;

	LOGI("Prepare to set up application firmware\n");

	/* set up app firmware */
	retval = syna_dev_set_up_app_fw(tcm);
	if (retval < 0) {
		LOGE("Fail to set up app firmware on resume\n");
		goto exit;
	}

	retval = syna_dev_restore_feature_setting(tcm, resp_handling);
	if (retval < 0) {
		LOGE("Fail to restore feature setting on resume\n");
		goto exit;
	}

	retval = syna_check_normal_scan_mode(tcm, CMD_RESPONSE_IN_POLLING);
	if (retval < 0) {
		LOGE("Fail to set normal scan mode\n");
		goto exit;
	}

	retval = 0;

exit:
	if (retval == 0) {
		LOGI("Device resumed (pwr_state:%d)\n", tcm->pwr_state);

		/* enable irq */
		if ((!attn->irq_enabled) && (hw_if->hw_platform.ops_enable_attn))
			hw_if->hw_platform.ops_enable_attn(&hw_if->hw_platform, true);

		tcm->slept_in_early_suspend = false;
	} else {
		tcm->pwr_state = PWR_OFF;
		LOGE("Device resume failed, retval:%d, pwr_state:%d\n", retval, tcm->pwr_state);
	}

	return retval;
}
/**
 * @brief  Put device into suspend state, either the lower power gesture
 *         mode or sleep mode.
 * @param
 *    [ in] dev: pointer to device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_suspend(struct device *dev)
{
#if defined(LOW_POWER_MODE) && defined(GOOG_INT2_FEATURE)
	int retval, retry;
	unsigned char status;
#endif
	struct syna_tcm *tcm = dev_get_drvdata(dev);
	struct syna_hw_interface *hw_if = tcm->hw_if;

	/* exit directly if device is already in suspend state */
	if (tcm->pwr_state != PWR_ON)
		return 0;

	LOGI("Prepare to suspend device\n");

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* clear all input events  */
	syna_dev_free_input_events(tcm);
#endif
	/* once lpwg is enabled, irq should be alive.
	 * otherwise, disable irq in suspend.
	 */
	if ((!tcm->lpwg_enabled) && (hw_if->hw_platform.ops_enable_attn))
		hw_if->hw_platform.ops_enable_attn(&hw_if->hw_platform, false);

#ifdef LOW_POWER_MODE
	tcm->pwr_state = LOW_PWR;
#ifdef GOOG_INT2_FEATURE
	LOGI("Do reset on suspend\n");

	for (retry = 0; retry < 3; retry++) {
		if (hw_if->ops_hw_reset) {
			hw_if->ops_hw_reset(hw_if);
			retval = syna_tcm_get_event_data(tcm->tcm_dev,
				&status, NULL);
			if ((retval < 0) || (status != REPORT_IDENTIFY)) {
				LOGE("Fail to complete hw reset, ret = %d, status = %d\n",
				     retval, status);
				continue;
			}

			syna_dev_ensure_reset_done(tcm);
			break;
		} else {
			retval = syna_tcm_reset(tcm->tcm_dev, CMD_RESPONSE_IN_POLLING);
			if (retval < 0) {
				LOGE("Fail to do sw reset, ret = %d\n", retval);
				continue;
			}
			break;
		}
	}
#endif
	/* enter power saved mode if power is not off */
	if (syna_dev_enter_lowpwr_sensing(tcm) < 0) {
		LOGE("Fail to enter power suspended mode\n");
		return -EIO;
	}
#else
	tcm->pwr_state = PWR_OFF;
#endif

	/* Enable REPORT_TOUCH for gesture mode. */
	if (tcm->gti && goog_check_late_sense_on_enabled(tcm->gti)) {
		retval = syna_tcm_enable_report(tcm->tcm_dev, REPORT_TOUCH, true,
				CMD_RESPONSE_IN_POLLING);
		if (retval < 0) {
			LOGE("Fail to enable report %d\n", REPORT_TOUCH);
		}
	}

	syna_tcm_clear_command_processing(tcm->tcm_dev);

	syna_pinctrl_configure(tcm, false);

	LOGI("Device suspended (pwr_state:%d)\n", tcm->pwr_state);

	return 0;
}
/**
 * @brief  Output the device information.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    void.
 */
static void syna_dev_show_info(struct syna_tcm *tcm)
{
	struct syna_hw_interface *hw_if = tcm->hw_if;
	bool has_custom_tp_config = false;
	bool startup_reflash_enabled = false;
	bool rst_on_resume_enabled = false;
	bool background_helper_enabled  = false;

	if (!tcm->is_connected)
		return;

	LOGI("Config: max. write size(%d), max. read size(%d)\n",
		tcm->tcm_dev->max_wr_size, tcm->tcm_dev->max_rd_size);

#ifdef USE_CUSTOM_TOUCH_REPORT_CONFIG
	has_custom_tp_config = true;
#endif
#ifdef STARTUP_REFLASH
	startup_reflash_enabled = true;
#endif
#ifdef RESET_ON_RESUME
	rst_on_resume_enabled = true;
#endif
#ifdef ENABLE_HELPER
	background_helper_enabled = true;
#endif

#ifdef TDDI_PRODUCTS
	LOGI("Config: touch/display devices, multichip(%s)\n",
		(tcm->is_tddi_multichip) ? "yes" : "no");
#endif
	LOGI("Config: startup reflash(%s), hw reset(%s), rst on resume(%s)\n",
		(startup_reflash_enabled) ? "yes" : "no",
		(hw_if->ops_hw_reset) ? "yes" : "no",
		(rst_on_resume_enabled) ? "yes" : "no");
	LOGI("Config: lpwg mode(%s), custom tp config(%s) helper work(%s) touch report crc(%s)\n",
		(tcm->lpwg_enabled) ? "yes" : "no",
		(has_custom_tp_config) ? "yes" : "no",
		(background_helper_enabled) ? "yes" : "no",
		(tcm->enable_touch_crc) ? "yes" : "no");
}

/**
 * @brief  Disconnect and power off the device.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_disconnect(struct syna_tcm *tcm)
{
	struct syna_hw_interface *hw_if = tcm->hw_if;

	if (tcm->is_connected == false) {
		LOGI("%s already disconnected\n", PLATFORM_DRIVER_NAME);
		return 0;
	}

	if (tcm->pwr_state == BARE_MODE) {
		LOGI("Disconnect from bare mode\n");
		goto exit;
	}

#ifdef STARTUP_REFLASH
	if (tcm->reflash_workqueue) {
		cancel_delayed_work_sync(&tcm->reflash_work);
		flush_workqueue(tcm->reflash_workqueue);
		destroy_workqueue(tcm->reflash_workqueue);
		tcm->reflash_workqueue = NULL;
	}
#endif

	/* free interrupt line */
	if (hw_if->bdata_attn.irq_id)
		syna_dev_release_irq(tcm);

	/* unregister input device */
	syna_dev_release_input_device(tcm);

	tcm->input_dev_params.max_x = 0;
	tcm->input_dev_params.max_y = 0;
	tcm->input_dev_params.max_objects = 0;

exit:
	/* power off */
	if (hw_if->ops_power_on)
		hw_if->ops_power_on(hw_if, false);

	tcm->pwr_state = PWR_OFF;
	tcm->is_connected = false;

	LOGI("Device %s disconnected\n", PLATFORM_DRIVER_NAME);

	return 0;
}

/**
 * @brief  Connect and power on the device.
 *
 * @param
 *    [ in] tcm: the driver handle
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_connect(struct syna_tcm *tcm)
{
	int retval;
	struct syna_hw_interface *hw_if = tcm->hw_if;
	struct tcm_dev *tcm_dev = tcm->tcm_dev;
	struct tcm_features_info features;

	if (!tcm_dev) {
		LOGE("Invalid tcm_dev\n");
		return -EINVAL;
	}

	if (tcm->is_connected) {
		LOGI("Device %s already connected\n", PLATFORM_DRIVER_NAME);
		return 0;
	}

	/* power on the connected device */
	if (hw_if->ops_power_on) {
		retval = hw_if->ops_power_on(hw_if, true);
		if (retval < 0)
			return -ENODEV;
		if (hw_if->bdata_pwr.power_delay_ms > 0)
			syna_pal_sleep_ms(hw_if->bdata_pwr.power_delay_ms);
	}

#ifdef RESET_ON_CONNECT
	/* perform a hardware reset */
	if (hw_if->ops_hw_reset)
		hw_if->ops_hw_reset(hw_if);
#endif
	/* detect which modes of touch controller is running */
	retval = syna_tcm_detect_device(tcm->tcm_dev, PROTOCOL_DETECT_VERSION_1, false);
	if (retval < 0) {
		LOGE("Fail to detect the device\n");
		retval = -EPROBE_DEFER;
		goto err_detect_dev;
	}
	/* 'Bare' mode is a special software mode to bypass all control from userspace */
	if (tcm->pwr_state == BARE_MODE) {
		LOGI("Device %s config into bare mode\n", PLATFORM_DRIVER_NAME);
		tcm->is_connected = true;
		return 0;
	}

	if (tcm_dev->dev_mode == MODE_APPLICATION_FIRMWARE) {
		retval = syna_dev_set_up_app_fw(tcm);
		if (retval < 0) {
			LOGE("Fail to set up application firmware\n");

			/* switch to bootloader mode when failed */
			LOGI("Switch device to bootloader mode instead\n");
			syna_tcm_switch_fw_mode(tcm_dev, MODE_BOOTLOADER,
					tcm_dev->fw_mode_switching_time);
		} else {
			/* allocate and register to input device subsystem */
			retval = syna_dev_set_up_input_device(tcm);
			if (retval < 0) {
				LOGE("Fail to set up input device\n");
				goto err_setup_input_dev;
			}

			syna_dev_register_dispatcher(tcm);
			tcm->enable_touch_crc = false;
			retval = syna_tcm_get_features(tcm_dev, &features, CMD_RESPONSE_IN_POLLING);
			if (retval >= 0)
				tcm->enable_touch_crc = features.byte[8] & 0x01;
		}
	} else {
		LOGN("Application firmware not running, current mode: %02x\n", tcm_dev->dev_mode);

		if (tcm_dev->dev_mode == MODE_BOOTLOADER) {
			retval = syna_tcm_get_boot_info(tcm_dev, NULL, CMD_RESPONSE_IN_POLLING);
			if (retval)
				LOGI("Bootloader status: 0x%x\n", tcm_dev->boot_info.status);
		}
	}

	/* register the handling of report resulting from the unexpected reset */
	retval = syna_tcm_set_report_dispatcher(tcm_dev,
			REPORT_IDENTIFY, syna_dev_process_unexpected_reset, (void *)tcm);
	if (retval < 0)
		LOGE("Fail to register the handling function of unexpected reset\n");

	/* register the interrupt handler */
	retval = syna_dev_request_irq(tcm);
	if (retval < 0) {
		LOGE("Fail to request the interrupt line\n");
		goto err_request_irq;
	}

	/* for the reference,
	 * create a delayed work to perform fw update during the startup time
	 */
#ifdef STARTUP_REFLASH
	tcm->force_reflash = false;
	tcm->reflash_count = 0;
	tcm->reflash_workqueue = create_singlethread_workqueue("syna_reflash");
	INIT_DELAYED_WORK(&tcm->reflash_work, syna_dev_reflash_startup_work);
	queue_delayed_work(tcm->reflash_workqueue, &tcm->reflash_work,
			msecs_to_jiffies(STARTUP_REFLASH_DELAY_TIME_MS));
#endif

	tcm->pwr_state = PWR_ON;
	tcm->is_connected = true;

	syna_dev_show_info(tcm);

	LOGI("Device %s connected\n", PLATFORM_DRIVER_NAME);

	return 0;

err_request_irq:
	/* unregister input device */
	syna_dev_release_input_device(tcm);
err_setup_input_dev:
err_detect_dev:
	return retval;
}

#ifdef USE_DRM_BRIDGE
/**
 * @brief  To register a panel bridge based on DAM Bridge framework
 */
struct drm_connector *syna_dev_get_connector(struct drm_bridge *bridge)
{
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	drm_connector_list_iter_begin(bridge->dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->encoder == bridge->encoder)
			break;
	}
	drm_connector_list_iter_end(&conn_iter);
	return connector;
}

static void syna_dev_panel_enable(struct drm_bridge *bridge)
{
	struct syna_tcm *tcm =
			container_of(bridge, struct syna_tcm, panel_bridge);

	LOGD("Panel bridge enabled (pwr_state:%d)\n", tcm->pwr_state);
}

static void syna_dev_panel_disable(struct drm_bridge *bridge)
{
	struct syna_tcm *tcm =
			container_of(bridge, struct syna_tcm, panel_bridge);

	LOGD("Panel bridge disabled (pwr_state:%d)\n", tcm->pwr_state);
}

static void syna_dev_panel_mode_set(struct drm_bridge *bridge,
	const struct drm_display_mode *mode,
	const struct drm_display_mode *adjusted_mode)
{
	struct syna_tcm *tcm =
			container_of(bridge, struct syna_tcm, panel_bridge);

	if (!tcm->connector || !tcm->connector->state) {
		LOGI("Get bridge connector.\n");
		tcm->connector = syna_dev_get_connector(bridge);
	}
	LOGD("Panel bridge mode set (pwr_state:%d)\n", tcm->pwr_state);
}

static const struct drm_bridge_funcs panel_bridge_ops = {
	.enable = syna_dev_panel_enable,
	.disable = syna_dev_panel_disable,
	.mode_set = syna_dev_panel_mode_set,
};

static int syna_dev_register_panel(struct syna_tcm *tcm)
{
#ifdef CONFIG_OF
	tcm->panel_bridge.of_node = tcm->pdev->dev.parent->of_node;
#endif
	tcm->panel_bridge.funcs = &panel_bridge_ops;
	drm_bridge_add(&tcm->panel_bridge);

	return 0;
}

static void syna_dev_unregister_panel(struct drm_bridge *bridge)
{
	struct drm_bridge *node;

	drm_bridge_remove(bridge);

	if (!bridge->dev) /* not attached */
		return;

	drm_modeset_lock(&bridge->dev->mode_config.connection_mutex, NULL);
	list_for_each_entry(node, &bridge->encoder->bridge_chain, chain_node)
		if (node == bridge) {
			if (bridge->funcs->detach)
				bridge->funcs->detach(bridge);
			list_del(&bridge->chain_node);
			break;
		}
	drm_modeset_unlock(&bridge->dev->mode_config.connection_mutex);
	bridge->dev = NULL;
}
#endif

/**
 * @brief  Probe of TouchComm device driver.
 *
 * @param
 *    [ in] pdev: pointer to platform device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_dev_probe(struct platform_device *pdev)
{
	int retval;
	struct syna_tcm *tcm = NULL;
	struct tcm_dev *tcm_dev = NULL;
	struct syna_hw_interface *hw_if = NULL;
	struct tcm_timings timings;

	hw_if = pdev->dev.platform_data;
	if (!hw_if) {
		LOGE("Fail to find hardware configuration\n");
		return -EPROBE_DEFER;
	}

	tcm = syna_pal_mem_alloc(1, sizeof(struct syna_tcm));
	if (!tcm) {
		LOGE("Fail to create the handle of syna_tcm\n");
		return -ENOMEM;
	}

	/*
	 * Set up the of_node asap that .probe() from syna_tcm2_platform_i2c.c
	 * or syna_tcm2_platform_spi.c.
	 */
	pdev->dev.of_node = pdev->dev.parent->of_node;

	tcm->pinctrl = devm_pinctrl_get(pdev->dev.parent);
	if (IS_ERR_OR_NULL(tcm->pinctrl)) {
		LOGE("Could not get pinctrl!\n");
	} else {
		syna_pinctrl_configure(tcm, true);
	}

	/* allocate the TouchCom device handle */
	retval = syna_tcm_allocate_device(&tcm_dev,
		&hw_if->hw_platform, (void *)tcm);
	if ((retval < 0) || (!tcm_dev)) {
		LOGE("Fail to allocate TouchCom device handle\n");
		goto err_allocate_tcm;
	}

	tcm->tcm_dev = tcm_dev;
	tcm->pdev = pdev;
	tcm->hw_if = hw_if;

	/* configure the timings for processing */
	timings.cmd_timeout_ms = hw_if->product.default_cmd_timeout_ms;
	timings.cmd_polling_ms = hw_if->product.default_cmd_polling_ms;
	timings.cmd_turnaround_us[0] = hw_if->product.default_cmd_turnaround_us[0];
	timings.cmd_turnaround_us[1] = hw_if->product.default_cmd_turnaround_us[1];
	timings.cmd_retry_us[0] = hw_if->product.default_cmd_retry_us[0];
	timings.cmd_retry_us[1] = hw_if->product.default_cmd_retry_us[1];
	timings.flash_ops_delay_us[0] = hw_if->product.default_flash_delay_us[0];
	timings.flash_ops_delay_us[1] = hw_if->product.default_flash_delay_us[1];
	timings.flash_ops_delay_us[2] = hw_if->product.default_flash_delay_us[2];
	timings.fw_switch_delay_ms = hw_if->product.default_fw_switch_delay_ms;
	timings.reset_delay_ms = hw_if->bdata_rst.reset_delay_ms;

	retval = syna_tcm_config_timings(tcm_dev, &timings, 0, TIMINGS_ALL);
	if (retval < 0) {
		LOGE("Fail to config the timings\n");
		goto err_setup_timings;
	}

	/* basic initialization */
	syna_tcm_buf_init(&tcm->event_data);

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	syna_pal_mutex_alloc(&tcm->tp_event_mutex);
#endif
	syna_pal_mutex_alloc(&tcm->raw_data_mutex);

#ifdef ENABLE_WAKEUP_GESTURE
	tcm->lpwg_enabled = false;
#else
	tcm->lpwg_enabled = false;
#endif
#ifdef TDDI_PRODUCTS
#ifdef IS_TDDI_MULTICHIP
	tcm->is_tddi_multichip = true;
#else
	tcm->is_tddi_multichip = false;
#endif
#endif
	tcm->irq_wake = false;

	tcm->is_connected = false;
	tcm->pwr_state = PWR_OFF;

	tcm->dev_connect = syna_dev_connect;
	tcm->dev_disconnect = syna_dev_disconnect;
	tcm->dev_set_up_app_fw = syna_dev_set_up_app_fw;
	tcm->dev_resume = syna_dev_resume;
	tcm->dev_suspend = syna_dev_suspend;

	tcm->userspace_app_info = NULL;

	platform_set_drvdata(pdev, tcm);

	device_init_wakeup(&pdev->dev, 1);

	tcm->event_wq = alloc_workqueue("syna_wq", WQ_UNBOUND |
					WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!tcm->event_wq) {
		LOGE("Cannot create work thread\n");
		retval = -ENOMEM;
		goto err_alloc_workqueue;
	}

	/* connect to target device */
	retval = syna_dev_connect(tcm);
	if (retval < 0) {
#ifdef FORCE_CONNECTION
		LOGW("Failed on device detection\n");
		LOGN("Install driver anyway due to the force connection\n");
#else
		LOGE("Fail to connect to the device\n");
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		syna_pal_mutex_free(&tcm->tp_event_mutex);
#endif
		goto err_connect;
#endif
	}
	tcm->raw_data_report_code = 0;
	init_completion(&tcm->raw_data_completion);
	complete_all(&tcm->raw_data_completion);

	/* create the device file and register to char device classes */
	retval = syna_cdev_create(tcm, pdev);
	if (retval < 0) {
		LOGE("Fail to create the device sysfs\n");
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		syna_pal_mutex_free(&tcm->tp_event_mutex);
#endif
		goto err_create_cdev;
	}

#ifdef HAS_SYSFS_INTERFACE
	retval = syna_sysfs_create_dir(tcm, pdev);
	if (retval < 0) {
		LOGE("Fail to create sysfs dir\n");
		retval = -ENOTDIR;
		goto err_create_dir;
	}
#endif

	tcm->enable_fw_grip = 0x02;
	tcm->enable_fw_palm = 0x02;
	syna_dev_restore_feature_setting(tcm, CMD_RESPONSE_IN_ATTN);
#if defined(ENABLE_DISP_NOTIFIER)
#if defined(USE_DRM_BRIDGE)
	retval = syna_dev_register_panel(tcm);
	if (retval < 0) {
		LOGE("Fail to register panel bridge\n");
		goto err_disp_notifier;
	}
#endif
#if defined(USE_FB)
	tcm->fb_notifier.notifier_call = syna_dev_disp_notifier_cb;
	retval = fb_register_client(&tcm->fb_notifier);
	if (retval < 0) {
		LOGE("Fail to register FB notifier client\n");
		goto err_disp_notifier;
	}
#endif
#endif

#if defined(ENABLE_HELPER)
	ATOMIC_SET(tcm->helper.task, HELP_NONE);
	INIT_WORK(&tcm->helper.work, syna_dev_helper_work);
#endif

	LOGI("%s: TouchComm driver, %s ver.: %d.%s, installed\n",
		__func__,
		PLATFORM_DRIVER_NAME,
		SYNAPTICS_TCM_DRIVER_VERSION,
		SYNAPTICS_TCM_DRIVER_SUBVER);

	return 0;

#if defined(ENABLE_DISP_NOTIFIER)
#if defined(USE_DRM_BRIDGE) || defined(USE_FB)
err_disp_notifier:
#endif
#endif
#ifdef HAS_SYSFS_INTERFACE
err_create_dir:
	syna_cdev_remove(tcm);
#endif
err_create_cdev:
	syna_dev_disconnect(tcm);
#ifndef FORCE_CONNECTION
err_connect:
#endif
	if (tcm->event_wq)
		destroy_workqueue(tcm->event_wq);
err_alloc_workqueue:
	syna_tcm_buf_release(&tcm->event_data);
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	syna_pal_mutex_free(&tcm->tp_event_mutex);
#endif
err_setup_timings:
	syna_tcm_remove_device(tcm_dev);
err_allocate_tcm:
	syna_pal_mem_free((void *)tcm);

	return retval;
}

/**
 * @brief  Release all resources allocated previously.
 *
 * @param
 *    [ in] pdev: pointer to platform device
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static void syna_dev_remove(struct platform_device *pdev)
{
	struct syna_tcm *tcm = platform_get_drvdata(pdev);

	if (!tcm) {
		LOGW("Invalid handle to remove\n");
		return;
	}

#if defined(ENABLE_HELPER)
	cancel_work_sync(&tcm->helper.work);
#endif

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_pm_unregister_notification(tcm->gti);

	cancel_work_sync(&tcm->set_grip_mode_work);
	cancel_work_sync(&tcm->set_palm_mode_work);
	cancel_work_sync(&tcm->set_heatmap_enabled_work);
	cancel_work_sync(&tcm->set_screen_protector_mode_work);
#endif

#if defined(ENABLE_DISP_NOTIFIER)
#if defined(USE_DRM_BRIDGE)
	syna_dev_unregister_panel(&tcm->panel_bridge);
#else
	fb_unregister_client(&tcm->fb_notifier);
#endif
#endif

#ifdef HAS_SYSFS_INTERFACE
	syna_sysfs_remove_dir(tcm);
#endif
	/* remove the cdev and sysfs nodes */
	syna_cdev_remove(tcm);

	/* check the connection status, and do disconnection */
	if (syna_dev_disconnect(tcm) < 0)
		LOGE("Fail to do device disconnection\n");

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_touch_interface_disconnect(&tcm->pdev->dev);
	tcm->gti = NULL;
#endif

	if (tcm->userspace_app_info != NULL)
		syna_pal_mem_free(tcm->userspace_app_info);

	if (tcm->raw_data_buffer) {
		syna_pal_mem_free(tcm->raw_data_buffer);
		tcm->raw_data_buffer = NULL;
	}

	syna_tcm_buf_release(&tcm->event_data);

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	syna_pal_mutex_free(&tcm->tp_event_mutex);
#endif

	/* remove the allocated tcm device */
	syna_tcm_remove_device(tcm->tcm_dev);

	/* release the device context */
	syna_pal_mem_free((void *)tcm);
}

/** Definitions of TouchComm platform device */
#if IS_ENABLED(CONFIG_PM) || IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
static const struct dev_pm_ops syna_dev_pm_ops = {
#if !defined(ENABLE_DISP_NOTIFIER) || IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	.suspend = syna_dev_suspend,
	.resume = syna_dev_resume,
#endif
};
#endif

static struct platform_driver syna_dev_driver = {
	.driver = {
		.name = PLATFORM_DRIVER_NAME,
		.owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm = &syna_dev_pm_ops,
#endif
	},
	.probe = syna_dev_probe,
	.remove = syna_dev_remove,
};



/*
 * Entry of the TouchComm device driver.
 */

static int __init syna_dev_module_init(void)
{
	int retval;

	retval = syna_hw_interface_init();
	if (retval < 0)
		return retval;

	return platform_driver_register(&syna_dev_driver);
}

static void __exit syna_dev_module_exit(void)
{
	platform_driver_unregister(&syna_dev_driver);

	syna_hw_interface_exit();
}

module_init(syna_dev_module_init);
module_exit(syna_dev_module_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics TouchComm Touch Driver");
MODULE_LICENSE("GPL v2");
