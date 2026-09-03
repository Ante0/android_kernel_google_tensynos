// SPDX-License-Identifier: GPL-2.0
/*
 * Google Touch Interface for Pixel devices.
 *
 * Copyright 2025 Google LLC.
 */
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)

#include "syna_tcm2.h"
#include "synaptics_touchcom_func_base.h"

int syna_set_report_rate(void *private_data, struct gti_report_rate_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	u16 config = 0;

	if (tcm->pwr_state != PWR_ON) {
		LOGW("Cannot set report rate because touch is off");
		return -EPERM;
	}

	switch (cmd->setting) {
	case 120:
		config = REPORT_RATE_120HZ;
		break;
	case 240:
		config = REPORT_RATE_240HZ;
		break;
	default:
		LOGE("Invalid report rate %u", cmd->setting);
		return -EINVAL;
	}
	LOGI("Set report rate %uHz", cmd->setting);

	return syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_REPORT_RATE_SWITCH, config,
					   CMD_RESPONSE_IN_POLLING);
}

int syna_get_report_rate(void *private_data, struct gti_report_rate_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int ret = 0;
	u16 config = 0;

	if (tcm->pwr_state != PWR_ON) {
		LOGW("Cannot get report rate because touch is off");
		return -EPERM;
	}

	ret = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_REPORT_RATE_SWITCH, &config,
					  CMD_RESPONSE_IN_POLLING);
	if (ret < 0) {
		LOGE("Fail to read report rate, ret: %d", ret);
		return ret;
	}

	switch (config) {
	case REPORT_RATE_120HZ:
		cmd->setting = 120;
		break;
	case REPORT_RATE_240HZ:
		cmd->setting = 240;
		break;
	default:
		LOGE("Fail to read report rate, config: %u", config);
		return -EINVAL;
	}
	return ret;
}

int syna_set_int2_mode(void *private_data, struct gti_int2_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	u16 config = 0;
	int ret = 0;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set int2 mode because touch is off");
		return -EPERM;
	}

	switch (cmd->setting) {
	case GTI_INT2_MODE_KEEP_LOW:
		config = INT2_PRODUCTION_LOW;
		LOGI("Set INT2 production mode low");
		break;
	case GTI_INT2_MODE_KEEP_HIGH:
		config = INT2_PRODUCTION_HIGH;
		LOGI("Set INT2 production mode high");
		break;
	case GTI_INT2_MODE_AUTO:
		config = INT2_PRODUCTION_DISABLE;
		LOGI("Set INT2 production mode disabled(auto)");
		break;
	default:
		LOGE("Invalid INT2 type %d", cmd->setting);
		return -EINVAL;
	}

	ret = syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_INT2_PRODUCTION_CMD,
					  config, CMD_RESPONSE_IN_ATTN);

	return ret;
}

int syna_get_int2_mode(void *private_data, struct gti_int2_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int ret = 0;
	u16 config = 0;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get int2 mode because touch is off");
		return -EPERM;
	}

	ret = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_INT2_PRODUCTION_CMD,
					  &config, CMD_RESPONSE_IN_ATTN);
	if (ret < 0) {
		LOGE("Fail to read int2 mode.");
		return ret;
	}

	switch (config) {
	case INT2_PRODUCTION_LOW:
		cmd->setting = GTI_INT2_MODE_KEEP_LOW;
		break;
	case INT2_PRODUCTION_HIGH:
		cmd->setting = GTI_INT2_MODE_KEEP_HIGH;
		break;
	case INT2_PRODUCTION_DISABLE:
		cmd->setting = GTI_INT2_MODE_AUTO;
		break;
	default:
		LOGE("Invalid INT2 type %d", config);
	}

	return ret;
}

int syna_get_int2_status(void *private_data, struct gti_int2_status_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int ret = 0;
	u16 config = 0;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get int2 status because touch is off");
		return -EPERM;
	}

	ret = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_INT2_STATUS, &config,
					  CMD_RESPONSE_IN_ATTN);
	if (ret < 0) {
		LOGE("Fail to read int2 status.");
		return ret;
	}

	cmd->setting = (((config >> 1) & 0x01) == 1) ? GTI_INT2_STATUS_HIGH :
						       GTI_INT2_STATUS_LOW;

	return 0;
}

int syna_get_water_mode(void *private_data, struct gti_water_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	int ret = 0;
	u16 config = 0;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot get water status because touch is off");
		return -EPERM;
	}

	ret = syna_tcm_get_dynamic_config(tcm->tcm_dev, DC_MOISTURE_MODE, &config,
					  CMD_RESPONSE_IN_ATTN);
	if (ret < 0) {
		LOGE("Fail to read water status.");
		return ret;
	}

	cmd->setting = (config == 1) ? GTI_WATER_ENABLE : GTI_WATER_DISABLE;

	return 0;
}

int syna_set_water_mode(void *private_data, struct gti_water_cmd *cmd)
{
	struct syna_tcm *tcm = private_data;
	u16 config = 0;
	int ret = 0;

	if (goog_pm_wake_get_locks(tcm->gti) == 0 || tcm->pwr_state != PWR_ON) {
		LOGI("Connot set water mode because touch is off");
		return -EPERM;
	}

	config = cmd->setting == GTI_WATER_ENABLE ? 1 : 0;

	ret = syna_tcm_set_dynamic_config(tcm->tcm_dev, DC_MOISTURE_MODE, config,
					  CMD_RESPONSE_IN_ATTN);

	return ret;
}

#endif /* end of IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */
