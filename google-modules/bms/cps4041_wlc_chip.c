// SPDX-License-Identifier: GPL-2.0
/*
 * CPS4041 Wireless Charging Drive chip specific functions
 *
 * Copyright 2023 Google LLC
 *
 */

#include "google_wlc_chip.h"
#include "google_wlc.h"
#include "cps4041_wlc_chip.h"
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <misc/logbuffer.h>

static const struct regmap_config cps4041_fwupdate_regmap_config = {
	.reg_bits = 32,
	.val_bits = 8,
};

static int cps4041_get_vout_set(struct google_wlc_data *chgr, u32 *mv)
{
	u16 val;
	int ret;

	ret = chgr->chip->reg_read_adc(chgr, CPS4041_VOUT_SET_REG, &val);
	if (ret == 0) {
		if (val > GOOGLE_WLC_READING_MAX_VOLT_MV)
			return -EINVAL;
		*mv = val;
	}
	return ret;
}

static int cps4041_set_vout(struct google_wlc_data *chgr, u32 mv)
{
	dev_dbg(chgr->dev, "vout is set to %u", mv);
	return chgr->chip->reg_write_16(chgr, CPS4041_VOUT_SET_REG, mv);
}

static int cps4041_get_vrect_target(struct google_wlc_data *chgr, u32 *mv)
{
	/* No real vrect target needed */
	return cps4041_get_vout_set(chgr, mv);
}

static int cps4041_get_sys_mode(struct google_wlc_data *chgr, u8 *mode)
{
	u8 val8;
	int ret;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_OP_MODE_REG, &val8);
	if (ret < 0)
		return ret;

	switch (val8) {
	case CPS4041_OP_MODE_AC_MISSING:
		*mode = RX_MODE_AC_MISSING;
		break;
	case CPS4041_OP_MODE_BPP:
		*mode = RX_MODE_WPC_BPP;
		break;
	case CPS4041_OP_MODE_EPP_NEGO:
		*mode = RX_MODE_WPC_EPP_NEGO;
		break;
	case CPS4041_OP_MODE_EPP_PT:
		*mode = RX_MODE_WPC_EPP;
		break;
	case CPS4041_OP_MODE_MPP_NEGO:
		*mode = RX_MODE_WPC_MPP_NEGO;
		break;
	case CPS4041_OP_MODE_MPP_FULL:
		*mode = RX_MODE_WPC_MPP;
		break;
	case CPS4041_OP_MODE_CLOAK_FORCE:
	case CPS4041_OP_MODE_CLOAK:
		*mode = RX_MODE_MPP_CLOAK;
		break;
	case CPS4041_OP_MODE_MPP_RESTRICTED:
		*mode = RX_MODE_WPC_MPP_RESTRICTED;
		break;
	case CPS4041_OP_MODE_PDET:
		*mode = RX_MODE_PDET;
		break;
	case CPS4041_OP_MODE_MPP_CONTINUOUS_POWER_MODE:
		*mode = RX_MODE_WPC_MPP_CPM;
		break;
	case CPS4041_OP_MODE_MPP_NOMINAL_POWER_MODE:
		*mode = RX_MODE_WPC_MPP_NPM;
		break;
	case CPS4041_OP_MODE_MPP_LIGHT_LOAD_MODE:
		*mode = RX_MODE_WPC_MPP_LPM;
		break;
	case CPS4041_OP_MODE_MPP_HIGH_POWER_MODE:
		*mode = RX_MODE_WPC_MPP_HPM;
		break;
	default:
		*mode = RX_MODE_UNKNOWN;
		break;
	}
	return 0;
}

static int cps4041_add_info_string(struct google_wlc_data *chgr, char *buf)
{
	int count = 0;
	int ret;
	u16 val16;
	u8 val8;

	count += scnprintf(buf + count, PAGE_SIZE - count,
				   "chip id (dt) : %04x\n", chgr->chip_id);

	ret = chgr->chip->reg_read_16(chgr, CPS4041_CHIP_ID_REG, &val16);
	if (ret) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "Could not read from device\n");
		return count;
	}
	count += scnprintf(buf + count, PAGE_SIZE - count,
				   "chip id      : %04x\n", val16);

	ret = chgr->chip->reg_read_8(chgr, CPS4041_FW_MAJOR_REG, &val8);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "fw major     : %hhu\n", val8);

	ret = chgr->chip->reg_read_8(chgr, CPS4041_FW_MINOR_REG, &val8);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "fw minor     : %hhu\n", val8);
	if (chgr->fw_data.ver.crc > 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "fw crc       : %04llx\n", chgr->fw_data.ver.crc);

	count += scnprintf(buf + count, PAGE_SIZE - count,
			   "project id (dt): %02x\n", chgr->pdata->project_id);
	ret = chgr->chip->reg_read_8(chgr, CPS4041_PROJECT_ID_REG, &val8);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "project id     : %02x\n", val8);

	ret = cps4041_get_sys_mode(chgr, &val8);
	if (ret == 0 && mode_is_mpp(val8)) {
		u32 device_id, mfg_id, unique_id;
		u16 ptmc;
		u8 product_id;

		ret = chgr->chip->chip_get_ptmc_id(chgr, &ptmc);
		if (ret == 0)
			count += scnprintf(buf + count, PAGE_SIZE - count,
				   "TX PTMC      : %04x\n", ptmc);
		ret = chgr->chip->chip_get_mpp_xid(chgr, &device_id, &mfg_id, &unique_id,
						   &product_id);
		if (ret == 0) {
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"TX XID/dev_id: %06x\n", device_id);
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"TX XID/mfg_id: %06x\n", mfg_id);
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"TX unique id : %08x\n", unique_id);
			count += scnprintf(buf + count, PAGE_SIZE - count,
					"TX product id: %08x\n", product_id);
		}
	}

	return count;
}

static int cps4041_get_interrupts(struct google_wlc_data *chgr, u32 *int_val,
				 struct google_wlc_bits *int_fields)
{
	u8 buf[4];
	int ret;
	u8 val8;
	u16 val16;
	u32 mask_val;

	ret = chgr->chip->reg_read_n(chgr, CPS4041_INT_FLAG_REG, buf, 4);
	if (ret != 0)
		return ret;
	*int_val = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

	/* Handle EDS interrupt */
	if (*int_val & CPS4041_INTERRUPT_EDS_BIT) {
		ret = chgr->chip->reg_read_8(chgr, CPS4041_EDS0_FLAG_REG, &val8);
		if (ret == 0 && val8 == CPS4041_EDS_FLAG_RECEIVE)
			int_fields->sadt_received = 1;
		if (ret == 0 && val8 == CPS4041_EDS_FLAG_SEND_DONE)
			int_fields->sadt_sent = 1;
		if (ret == 0 &&
		    (val8 == CPS4041_EDS_FLAG_SEND_FAIL || val8 == CPS4041_EDS_FLAG_RECEIVE_FAIL))
			int_fields->sadt_error = 1;
	}
	if (*int_val & CPS4041_INTERRUPT_CHIP_OTP_BIT)
		int_fields->over_temperature = 1;
	if ((*int_val & CPS4041_INTERRUPT_MLDO_OCP_BIT) ||
		(*int_val & CPS4041_INTERRUPT_SR_OCP_BIT))
		int_fields->over_current = 1;
	if (*int_val & CPS4041_INTERRUPT_VRECT_OVP_BIT)
		int_fields->over_voltage = 1;
	if (*int_val & CPS4041_INTERRUPT_RX_MODE_SWITCH_BIT)
		int_fields->operation_mode = 1;
	if (*int_val & CPS4041_INTERRUPT_SYS_MODE_CHG_BIT)
		int_fields->sys_mode = 1;
	if (*int_val & CPS4041_INTERRUPT_POWER_ON_BIT)
		int_fields->stat_vrect = 1;
	if (*int_val & CPS4041_INTERRUPT_RX_READY_BIT)
		int_fields->stat_vout = 1;
	if (*int_val & CPS4041_NEGO_DONE_BIT) {
		int_fields->power_adjust = 1;
		if (chgr->wlc_dc_debug_gain_linear) {
			ret = chgr->chip->reg_read_8(chgr, CPS4041_GAIN_LINEAR_STATUS_REG, &val8);
			dev_info(chgr->dev, "Gain linearization status: 0x%x, ret: %d", val8, ret);
		}
	}
	if (*int_val & CPS4041_INTERRUPT_MPP_INCREASE_POWER_BIT)
		int_fields->load_increase_alert = 1;
	if (*int_val & CPS4041_INTERRUPT_MPP_DECREASE_POWER_BIT)
		int_fields->load_decrease_alert = 1;
	if (*int_val & CPS4041_INTERRUPT_RCS_REPORT_BIT)
		int_fields->rcs = 1;
	if (*int_val & (CPS4041_INTERRUPT_FSK_TO_BIT | CPS4041_INTERRUPT_FSK_PKT_BIT)) {
		if (*int_val & CPS4041_INTERRUPT_FSK_TO_BIT)
			int_fields->fsk_timeout = 1;
		else
			int_fields->fsk_received = 1;

		if (chgr->fsk_log == 0) {
			ret = chgr->chip->reg_read_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
			if (ret == 0) {
				mask_val &= ~CPS4041_INTERRUPT_FSK_PKT_BIT;
				chgr->chip->reg_write_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
			}
		}
	}
	if (*int_val & CPS4041_INTERRUPT_FSK_MISSING_BIT)
		int_fields->fsk_missing = 1;
	if (*int_val & CPS4041_INTERRUPT_DPLOSS_CALI_BIT) {
		ret = chgr->chip->reg_read_8(chgr, CPS4041_DPLOSS_CALI_STATUS_REG, &val8);
		if (ret == 0 && val8 == 0) {
			int_fields->dploss_cal_success = 1;
		} else if (ret == 0 && val8 == 1) {
			ret = chgr->chip->reg_read_16(chgr, CPS4041_DPLOSS_CALI_INFO_REG, &val16);
			dev_info(chgr->dev, "CPS4041 Cal error, info val: 0x%x", val16);
			if (chgr->mpp25.last_dploss_event == DPLOSS_CAL_ENTER) {
				const u16 mask = CPS4041_DPLOSS_CALI_START_REJ_RSN_MASK;
				const u16 shift = CPS4041_DPLOSS_CALI_START_REJ_RSN_SHIFT;

				if (((val16 & mask) >> shift) == CAL_RESP_FOD_REFRESH_SEQ)
					int_fields->dploss_cal_retry = 1;
				else
					int_fields->dploss_cal_error = 1;
			} else {
				int_fields->dploss_cal_error = 1;
			}
		} else if (ret == 0 && val8 == 2) {
			int_fields->dploss_param_match = 1;
		} else if (ret == 0 && val8 == 3) {
			ret = chgr->chip->reg_read_16(chgr, CPS4041_DPLOSS_CALI_INFO_REG,
						      &val16);
			dev_info(chgr->dev, "CPS4041 Param error, info val: 0x%x", val16);
			int_fields->dploss_param_error = 1;
		}
	}
	if (*int_val & CPS4041_INTERRUPT_DYNAMIC_MOD_BIT) {
		int_fields->dynamic_mod = 1;
		ret = chgr->chip->reg_read_8(chgr, CPS4041_DC_CURR_MOD_DEPTH, &val8);
		if (ret == 0)
			dev_dbg(chgr->dev, "real modulation depth: %d", val8);
	}
	if (*int_val & CPS4041_INTERRUPT_CLOAK_ERROR_BIT)
		int_fields->cloak_error = 1;

	if (*int_val & CPS4041_INTERRUPT_DEBUG_INFO_BIT) {
		u32 val32 = 0;

		ret = chgr->chip->reg_read_n(chgr, CPS4041_DEBUG_INFO_REG, buf, 4);
		chgr->chip->reg_write_n(chgr, CPS4041_DEBUG_INFO_REG, &val32, 4);
		if (ret != 0)
			return ret;

		val32 = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		dev_info(chgr->dev, "CPS4041 DEBUG_INFO IRQ, val: 0x%08x", val32);

		/* Debug Info Bits */
		if (val32 & CPS4041_INTERRUPT_MLDO_OFF_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ MLDO off bit\n");
		if (val32 & CPS4041_INTERRUPT_MLDO_ON_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ MLDO on bit\n");
		if (val32 & CPS4041_INTERRUPT_VDD_ERR_BIT) {
			int_fields->vdd_err = 1;
			dev_dbg(chgr->dev, "CPS4041 IRQ VDD ERR on bit\n");
		}
		if (val32 & CPS4041_INTERRUPT_HEAVY_LOAD_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ Heavy Load bit\n");
		if (val32 & CPS4041_INTERRUPT_LIGHT_LOAD_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ Light Load bit\n");
		if (val32 & CPS4041_INTERRUPT_VRECT_OVP_TO_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ VRECT OVP TO bit\n");
		if (val32 & CPS4041_INTERRUPT_VRECT_BYP_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ VRECT BYP bit\n");
		if (val32 & CPS4041_INTERRUPT_CHIP_HTP_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ HTP bit\n");
		if (val32 & CPS4041_INTERRUPT_MLDO_HOCP_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ MLDO HOCP bit\n");
		if (val32 & CPS4041_INTERRUPT_MLDO_SCP_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ MLDO SCP bit\n");
		if (val32 & CPS4041_INTERRUPT_MLDO_OPP_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ MLDO OPP bit\n");
		if (val32 & CPS4041_INTERRUPT_MLDO_UVP_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ MLDO UVP bit\n");
		if (val32 & CPS4041_INTERRUPT_MLDO_OVP_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ MLDO OVP bit\n");
		if (val32 & CPS4041_INTERRUPT_AC_LOSS_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ AC LOSS bit\n");
		if (val32 & CPS4041_INTERRUPT_SR_BR_SW_FAIL_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ SR_BR_SW_FAIL bit\n");
		if (val32 & CPS4041_INTERRUPT_SR_BR_SW_SUCC_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ SR_BR_SW_SUCC bit\n");
		if (val32 & CPS4041_INTERRUPT_START_OV_BIT)
			dev_dbg(chgr->dev, "CPS4041 IRQ START OV bit\n");
	}

	return 0;
}

static int cps4041_enable_interrupts(struct google_wlc_data *chgr)
{
	u32 mask_val;
	int ret;

	mask_val = 0xffffffff;

	mask_val &= ~CPS4041_INTERRUPT_MPP_INCREASE_POWER_BIT;
	if (chgr->fsk_log == 0)
		mask_val &= ~CPS4041_INTERRUPT_FSK_PKT_BIT;
	if (chgr->pdata->mod_depth_max == 0)
		mask_val &= ~CPS4041_INTERRUPT_DYNAMIC_MOD_BIT;

	ret = chgr->chip->reg_write_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
	dev_info(chgr->dev, "Enabled interrupts: %08x, ret: %d\n", mask_val, ret);
	return ret;
}

static int cps4041_chip_set_cmd_reg(struct google_wlc_data *chgr, u32 cmd, int timeout_ms)
{
	int ret;
	int retry_ms;
	u32 cmd_val = 0;
	int sleep_ms;

	mutex_lock(&chgr->cmd_lock);
	ret = chgr->chip->reg_read_n(chgr, CPS4041_CMD_REG, &cmd_val, 4);
	if (ret) {
		dev_err(chgr->dev, "Failed to read cmd reg: %d\n", ret);
		goto exit;
	}
	cmd_val = cmd_val | cmd;

	ret = chgr->chip->reg_write_n(chgr, CPS4041_CMD_REG, &cmd_val, 4);
	if (ret) {
		dev_err(chgr->dev, "Failed to set cmd reg %d: %d\n", cmd, ret);
		goto exit;
	}
	/* don't allow to sleep over 1s */
	if (timeout_ms > CPS4041_MAX_CMD_TIMEOUT)
		timeout_ms = CPS4041_MAX_CMD_TIMEOUT;

	if (timeout_ms <= 10)
		sleep_ms = 1;
	else if (timeout_ms <= 100)
		sleep_ms = 10;
	else
		sleep_ms = 100;

	retry_ms = 0;
	while (retry_ms < timeout_ms) {
		ret = chgr->chip->reg_read_n(chgr, CPS4041_CMD_REG, &cmd_val, 4);
		if ((cmd_val & cmd) == 0 && ret == 0)
			break;
		retry_ms += sleep_ms;
		msleep(sleep_ms);
	}

	if (retry_ms >= timeout_ms && (cmd_val & cmd) != 0)
		dev_warn(chgr->dev,
			"Long time to send command 0x%x. cmd reg stuck at 0x%x\n", cmd, cmd_val);
exit:
	mutex_unlock(&chgr->cmd_lock);
	return ret;
}

static int cps4041_clear_interrupts(struct google_wlc_data *chgr, u32 int_val)
{
	int ret;

	ret = chgr->chip->reg_write_n(chgr, CPS4041_INT_CLR_REG, &int_val, 4);
	if (ret)
		return ret;

	ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_INT_CLR_EN, 10);
	if (ret)
		return ret;

	return ret;
}
static int cps4041_chip_set_func_reg(struct google_wlc_data *chgr, u32 func, const bool enable)
{
	int ret;
	u32 reg_val = 0;

	mutex_lock(&chgr->cmd_lock);

	ret = chgr->chip->reg_read_n(chgr, CPS4041_FUNC_EN_REG, &reg_val, 4);
	if (ret != 0)
		goto exit;

	if (enable)
		reg_val = reg_val | func;
	else
		reg_val = reg_val & ~func;

	ret = chgr->chip->reg_write_n(chgr, CPS4041_FUNC_EN_REG, &reg_val, 4);
exit:
	mutex_unlock(&chgr->cmd_lock);
	return ret;
}
static int cps4041_pdet_en(struct google_wlc_data *chgr, const bool enable)
{
	dev_info(chgr->dev, "Setting PDET en=%d", enable);
	return cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_PDET_EN, enable);
}

static int cps4041_set_cloak_mode(struct google_wlc_data *chgr, bool enable, u8 reason)
{
	u8 val8;
	int ret;

	ret = cps4041_get_sys_mode(chgr, &val8);
	if (ret)
		return ret;
	if (enable && mode_is_mpp(val8) && val8 != RX_MODE_WPC_MPP_NEGO) {
		dev_info(chgr->dev, "Entering Cloak Mode, reason=%d", reason);
		ret = chgr->chip->reg_write_8(chgr, CPS4041_CLOAK_REASON_REG, reason);
		return cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_MPP_CLOAK_ENTER, 1000);
	}
	if (!enable && val8 == RX_MODE_MPP_CLOAK) {
		dev_info(chgr->dev, "Exiting Cloak Mode");
		return cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_MPP_CLOAK_EXIT,
						chgr->cloak_ping_delay_ms + 1000);
	}
	if (!enable && val8 == RX_MODE_AC_MISSING) {
		dev_info(chgr->dev, "Turning off VDD");
		return cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_EX_VDD_EN, false);
	}
	if (val8 == RX_MODE_WPC_BPP || val8 == RX_MODE_WPC_EPP || val8 == RX_MODE_PDET) {
		ret = cps4041_pdet_en(chgr, enable);
		if (ret == 0)
			return ret;
	}
	dev_err(chgr->dev, "Invalid cloak mode parameters: Mode: %u, enable: %u", val8, enable);
	return -EINVAL;
}

static int cps4041_send_sadt(struct google_wlc_data *chgr, u8 stream)
{
	int ret;

	ret = chgr->chip->reg_write_8(chgr, CPS4041_EDS0_FLAG_REG, CPS4041_EDS_FLAG_SEND);
	if (ret)
		return ret;
	ret = chgr->chip->reg_write_8(chgr, CPS4041_EDS0_NUMBER_REG, stream);
	if (ret)
		return ret;
	return cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_MPP_EDS_EN, 100);
}

static int cps4041_eds_reset(struct google_wlc_data *chgr)
{
	return 0;
}

static int cps4041_get_status_fields(struct google_wlc_data *chgr,
				    struct google_wlc_bits *status_fields)
{
	u32 temp;

	return cps4041_get_interrupts(chgr, &temp, status_fields);
}

static int cps4041_send_csp(struct google_wlc_data *chgr)
{
	int ret;
	u8 to_send = chgr->last_capacity;

	if (chgr->last_capacity <= 0)
		return 0;

	if (chgr->last_capacity > 100)
		to_send = 100;

	ret = chgr->chip->reg_write_8(chgr, CPS4041_CS_VAL_REG, to_send);
	if (ret) {
		dev_err(chgr->dev, "Failed to set csp: %d\n", ret);
		return ret;
	}

	dev_info(chgr->dev, "Sending CSP, soc: %d\n", to_send);

	return cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_SEND_CSP, 10);
}

static int cps4041_send_ept(struct google_wlc_data *chgr, enum ept_reason reason)
{
	int ret;

	dev_info(chgr->dev, "Sending EPT\n");
	ret = chgr->chip->reg_write_8(chgr, CPS4041_EPT_VAL_REG, reason);
	if (ret)
		dev_err(chgr->dev, "Failed to set ept reason:%d, ret=%d\n", reason, ret);

	ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_SEND_EPT, 10);

	return ret;
}

static int cps4041_get_cloak_reason(struct google_wlc_data *chgr, u8 *reason)
{
	int ret;
	u8 val8;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_OP_MODE_REG, &val8);
	if (val8 != CPS4041_OP_MODE_CLOAK)
		return -EINVAL;
	ret = chgr->chip->reg_read_8(chgr, CPS4041_CLOAK_REASON_REG, &val8);
	if (ret)
		return ret;
	*reason = val8;
	return 0;
}

static int cps4041_get_cloak_exit_reason(struct google_wlc_data *chgr, u8 *reason)
{
	int ret;
	u8 val8;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_EXIT_CLOAK_REASON_REG, &val8);
	if (ret)
		return ret;

	switch (val8) {
	case CPS4041_CLOAK_EXIT_NORMAL:
		*reason = CLOAK_EXIT_NORMAL;
		break;
	case CPS4041_CLOAK_EXIT_FSK_FAIL:
		*reason = CPS4041_CLOAK_EXIT_FSK_FAIL;
		break;
	case CPS4041_CLOAK_DET_PING_TIMEOUT:
		*reason = CLOAK_DET_PING_TIMEOUT;
		break;
	case CPS4041_CLOAK_PING_TIMEOUT:
		*reason = CLOAK_PING_TIMEOUT;
		break;
	case CPS4041_CLOAK_EXIT_PTX_XID_CHECK_FAIL:
		*reason = CLOAK_EXIT_PTX_XID_CHECK_FAIL;
		break;
	case CPS4041_CLOAK_PTX_POWER_DOWN_TIMEOUT:
		*reason = CLOAK_PTX_POWER_DOWN_TIMEOUT;
		break;
	case CPS4041_CLOAK_PTX_POWER_DOWN_FAILURE:
		*reason = CLOAK_PTX_POWER_DOWN_FAILURE;
		break;
	case CPS4041_CLOAK_PTX_FSK_MISSING:
		*reason = CLOAK_PTX_FSK_MISSING;
		break;
	case CPS4041_CLOAK_PING_FREQ_INCORRECT:
		*reason = CLOAK_PING_FREQ_INCORRECT;
		break;
	default:
		*reason = val8;
		break;
	}
	return 0;
}

static int cps4041_get_negotiated_power(struct google_wlc_data *chgr, u32 *mw)
{
	u8 val, val_comp;
	int ret;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_NEGO_DONE_POWER_REG, &val);
	ret |= chgr->chip->reg_read_8(chgr, CPS4041_NEGO_DONE_POWER_COMP_REG, &val_comp);
	if (ret < 0)
		return ret;

	*mw = val * 500 + val_comp * 100;

	return ret;
}

static int cps4041_get_potential_power(struct google_wlc_data *chgr, u32 *mw)
{
	u8 val;
	int ret;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_TX_POTENTIAL_POWER_REG, &val);
	if (ret < 0)
		return ret;

	*mw = val * 100;

	return ret;
}

static int cps4041_enable_load_increase(struct google_wlc_data *chgr, bool enable)
{
	int ret;
	u32 mask_val;

	ret = chgr->chip->reg_read_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
	if (ret != 0)
		return ret;

	if (enable)
		mask_val |= CPS4041_INTERRUPT_MPP_INCREASE_POWER_BIT;
	else
		mask_val &= ~CPS4041_INTERRUPT_MPP_INCREASE_POWER_BIT;

	ret = chgr->chip->reg_write_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);

	return ret;
}

static int cps4041_fw_reg_read_n(struct google_wlc_data *chgr,
				 unsigned int reg, char *buf, size_t n)
{
	int ret, i;
	u32 *data;
	ssize_t len = 0, bytes = n * 4;

	if (IS_ERR(chgr->chip->fw_regmap))
		return -ENODEV;

	data = kmalloc(bytes, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_lock(&chgr->io_lock);
	ret = regmap_raw_read(chgr->chip->fw_regmap, reg, data, bytes);
	mutex_unlock(&chgr->io_lock);
	if (ret < 0) {
		kfree(data);
		return ret;
	}

	for (i = 0; i < n; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len, "%08x: %08x\n",
				 reg + i, data[i]);

	kfree(data);
	return len;
}

static int cps4041_fw_reg_write_n(struct google_wlc_data *chgr, u32 reg, const char *buf, size_t n)
{
	u32 *val;
	char *data, *tmp_buf;
	int i, ret;
	ssize_t bytes = n * 4;

	if (IS_ERR(chgr->chip->fw_regmap))
		return -ENODEV;

	val = kmalloc(bytes, GFP_KERNEL);
	if (!val)
		return -ENOMEM;

	tmp_buf = kstrdup(buf, GFP_KERNEL);
	data = tmp_buf;
	if (!data) {
		kfree(val);
		return -ENOMEM;
	}
	for (i = 0; data && i < n; i++) {
		char *d = strsep(&data, " ");

		if (!*d)
			continue;
		ret = kstrtou32(d, 16, &val[i]);
		if (ret < 0)
			break;
	}

	mutex_lock(&chgr->io_lock);
	ret = regmap_raw_write(chgr->chip->fw_regmap, reg, val, bytes);
	mutex_unlock(&chgr->io_lock);

	kfree(val);
	kfree(tmp_buf);

	if (i != n || ret < 0)
		ret = -EINVAL;

	return ret;
}

static int cps4041_get_mpp_xid(struct google_wlc_data *chgr, u32 *device_id, u32 *mfg_rsvd_id,
			       u32 *unique_id, u8 *product_id)
{
	int ret;
	u8 buf[3];
	u8 mode;

	ret = chgr->chip->chip_get_sys_mode(chgr, &mode);
	if (ret != 0 || !mode_is_mpp(mode))
		return -EINVAL;

	ret = chgr->chip->reg_read_n(chgr, CPS4041_TX_DEVICE_ID_REG, buf, 3);
	if (ret != 0)
		return ret;
	*device_id = ((buf[0] << 16) | (buf[1] << 8) | (buf[2] & 0xF8)) >> 3;
	*mfg_rsvd_id = (buf[2] & 0x7) << 16;

	ret = chgr->chip->reg_read_n(chgr, CPS4041_TX_MFGID_REG, buf, 2);
	if (ret != 0)
		return ret;
	*mfg_rsvd_id |= (buf[1] << 8) | buf[0];

	if (chgr->ptmc == CPS8200_PTMC_ID) {
		*unique_id = *device_id << 12 | *mfg_rsvd_id >> 7;
		*product_id = (*mfg_rsvd_id >> 1) & 0x3F;
		chgr->txsrc = *mfg_rsvd_id & 0x1;
	} else {
		*unique_id = 0;
		*product_id = 0;
		chgr->txsrc = 0;
	}

	return 0;
}

static int cps4041_get_tx_kest(struct google_wlc_data *chgr, u32 *kest)
{
	u16 val;
	int ret;

	ret = chgr->chip->reg_read_16(chgr, CPS4041_TX_KEST_REG, &val);

	if (ret == 0)
		*kest = ((u32)val) * 1000 / 4095;
	return ret;
}

static int cps4041_get_project_id(struct google_wlc_data *chgr, u8 *pid)
{
	int ret;
	u8 val8;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_PROJECT_ID_REG, &val8);

	if (ret == 0)
		*pid = val8;

	return ret;
}

static int cps4041_send_packet(struct google_wlc_data *chgr, struct google_wlc_packet packet)
{
	int ret;
	u8 buf[CPS4041_PPP_SIZE];
	u32 mask_val;

	buf[0] = packet.header;
	buf[1] = packet.cmd;
	memcpy(buf + 2, packet.data, CPS4041_PPP_SIZE - 2);
	ret = chgr->chip->reg_write_n(chgr, CPS4041_PPP_HEADER_REG, buf, CPS4041_PPP_SIZE);
	if (ret != 0)
		return ret;

	ret = chgr->chip->reg_read_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
	if (ret != 0)
		return ret;

	mask_val |= CPS4041_INTERRUPT_FSK_PKT_BIT;
	ret = chgr->chip->reg_write_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
	if (ret != 0)
		return ret;

	ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_SEND_DATA, 1000);

	return ret;
}

static int cps4041_get_packet(struct google_wlc_data *chgr, struct google_wlc_packet *packet,
			      size_t *len)
{
	int ret;
	u8 buf[CPS4041_BC_SIZE];
	*len = 0;
	int vrect, iout, prect;

	ret = chgr->chip->reg_read_n(chgr, CPS4041_BC_HEADER_REG, buf, CPS4041_BC_SIZE);
	if (ret == 0 && packet) {
		packet->header = buf[0];
		packet->cmd = buf[1];
		memcpy(packet->data, buf + 2, CPS4041_BC_SIZE - 2);
		*len = CPS4041_BC_SIZE - 2;
	}
	if (chgr->debug_cal_power && packet->header == CPS4041_CAL_CAPTURE_PACKET_ID) {
		ret |= chgr->chip->chip_get_vrect(chgr, &vrect);
		ret |= chgr->chip->chip_get_iout(chgr, &iout);
		if (ret == 0) {
			iout += CPS4041_CHIP_CURRENT_MA;;
			prect = vrect * iout / 1000;
			dev_info(chgr->dev, "Cal capture debug: vrect: %d, irect: %d, prect: %d",
				vrect, iout, prect);
		}
	}

	return ret;
}

static int cps4041_get_mated_q(struct google_wlc_data *chgr, u8 *res)
{
	int ret;
	u8 val8;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_MATED_Q_RES_REG, &val8);

	if (ret != 0)
		return ret;
	if (val8 == 0)
		*res = MATED_Q_INVALID;
	else if (val8 == 1)
		*res = MATED_Q_NO_FOD;
	else if (val8 == 2)
		*res = MATED_Q_FOD;
	else if (val8 == 3)
		*res = MATED_Q_INCONCLUSIVE;
	else
		return -EINVAL;

	return ret;
}

static int cps4041_select_ask_mode(struct google_wlc_data *chgr, int mode_number)
{
	int ret = 0;

	switch (mode_number) {
	case CPS4041_ASK_MOD_DC_LOAD:
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMA12_EN, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMB12_EN, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CM_POLARITY_EN, 0);
		ret |= chgr->chip->reg_write_8(chgr, CPS4041_ASK_MOD_MLDO_DELTA_REG, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL1, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL2, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_TYPE, 1);
		break;
	case CPS4041_ASK_MOD_DC_LOAD_ADAPTIVE:
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMA12_EN, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMB12_EN, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CM_POLARITY_EN, 0);
		ret |= chgr->chip->reg_write_8(chgr, CPS4041_ASK_MOD_MLDO_DELTA_REG, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL1, 1);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL2, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_TYPE, 1);
		break;
	case CPS4041_ASK_MOD_CAP_CMB_POSITIVE:
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMA12_EN, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMB12_EN, 1);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CM_POLARITY_EN, 0);
		ret |= chgr->chip->reg_write_8(chgr, CPS4041_ASK_MOD_MLDO_DELTA_REG, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL1, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL2, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_TYPE, 0);
		break;
	case CPS4041_ASK_MOD_CAP_CMB_NEGATIVE:
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMA12_EN, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMB12_EN, 1);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CM_POLARITY_EN, 1);
		ret |= chgr->chip->reg_write_8(chgr, CPS4041_ASK_MOD_MLDO_DELTA_REG, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL1, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL2, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_TYPE, 0);
		break;
	case CPS4041_ASK_MOD_CAP_CMB_POSITIVE_300MV:
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMA12_EN, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CMB12_EN, 1);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_CM_POLARITY_EN, 0);
		ret |= chgr->chip->reg_write_8(chgr, CPS4041_ASK_MOD_MLDO_DELTA_REG,
					       CPS4041_ASK_DELTA_NEG_300MV_VAL);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL1, 0);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_MLDO_CTRL_SEL2, 1);
		ret |= cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_ASK_MOD_TYPE, 0);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

static int cps4041_set_mod_mode(struct google_wlc_data *chgr, enum ask_mod_mode mode)
{
	int ret;
	int to_select;

	if (mode == ASK_MOD_MODE_NEG_CAP_MOD)
		to_select = CPS4041_ASK_MOD_CAP_CMB_NEGATIVE;
	else if (mode == ASK_MOD_MODE_ADAPTIVE_LOAD_MOD)
		to_select = CPS4041_ASK_MOD_DC_LOAD_ADAPTIVE;
	else if (mode == ASK_MOD_MODE_POS_CAP_MOD)
		to_select = CPS4041_ASK_MOD_CAP_CMB_POSITIVE;
	else if (mode == ASK_MOD_MODE_LOAD_MOD)
		to_select = CPS4041_ASK_MOD_DC_LOAD;
	else
		return -EINVAL;

	ret = cps4041_select_ask_mode(chgr, to_select);

	dev_info(chgr->dev, "Modulation set: %s, num=%d, ret=%d",
		 ask_mod_str[mode],  to_select, ret);
	return ret;

}

static int cps4041_enable_auto_vout(struct google_wlc_data *chgr, bool enable)
{
	int ret;

	if (enable)
		chgr->chip->reg_write_16(chgr, CPS4041_VOUT_SET_REG, chgr->chip->val_default_vout);
	ret = cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_MLDO_AUTO_CTRL_EN, enable);
	dev_info(chgr->dev, "auto vout enable: %d, ret: %d", enable, ret);

	return ret;
}

static int cps4041_get_mode_capabilities(struct google_wlc_data *chgr,
						 struct mode_cap_data *cap)
{
	int ret;
	u8 val8;
	u8 min_volt;
	u8 max_volt;
	u8 pot_pwr;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_TX_POWER_MODE_CAP_INFO_REG, &val8);
	if (ret < 0)
		return ret;

	if (val8 & CPS4041_TX_POWER_MODE_CAP_CPM_BIT) {
		cap->cpm.supported = true;
		ret = chgr->chip->reg_read_8(chgr, CPS4041_CPM_VOL_REF0_REG, &min_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_CPM_VOL_REF1_REG, &max_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_CPM_MAX_POTENTIAL_POWER_REG, &pot_pwr);
		if (ret < 0)
			return -EINVAL;
		cap->cpm.min_volt = min_volt * 100;
		cap->cpm.max_volt = max_volt * 100;
		cap->cpm.pot_pwr = pot_pwr * 100;
	}
	if (val8 & CPS4041_TX_POWER_MODE_CAP_LPM_BIT) {
		cap->lpm.supported = true;
		ret = chgr->chip->reg_read_8(chgr, CPS4041_LPM_VOL_REF0_REG, &min_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_LPM_VOL_REF1_REG, &max_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_LPM_MAX_POTENTIAL_POWER_REG, &pot_pwr);
		if (ret < 0)
			return -EINVAL;
		cap->lpm.min_volt = min_volt * 100;
		cap->lpm.max_volt = max_volt * 100;
		cap->lpm.pot_pwr = pot_pwr * 100;
	}
	if (val8 & CPS4041_TX_POWER_MODE_CAP_NPM_BIT) {
		cap->npm.supported = true;
		ret = chgr->chip->reg_read_8(chgr, CPS4041_NPM_VOL_REF0_REG, &min_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_NPM_VOL_REF1_REG, &max_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_NPM_MAX_POTENTIAL_POWER_REG, &pot_pwr);
		if (ret < 0)
			return -EINVAL;
		cap->npm.min_volt = min_volt * 100;
		cap->npm.max_volt = max_volt * 100;
		cap->npm.pot_pwr = pot_pwr * 100;
	}
	if (val8 & CPS4041_TX_POWER_MODE_CAP_HPM_BIT) {
		cap->hpm.supported = true;
		ret = chgr->chip->reg_read_8(chgr, CPS4041_HPM_VOL_REF0_REG, &min_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_HPM_VOL_REF1_REG, &max_volt);
		ret |= chgr->chip->reg_read_8(chgr, CPS4041_HPM_MAX_POTENTIAL_POWER_REG, &pot_pwr);
		if (ret < 0)
			return -EINVAL;
		cap->hpm.min_volt = min_volt * 100;
		cap->hpm.max_volt = max_volt * 100;
		cap->hpm.pot_pwr = pot_pwr * 100;
	}
	return 0;
}

static int cps4041_set_mpp_powermode(struct google_wlc_data *chgr, enum mpp_powermode powermode,
				     bool preserve_session)
{
	int ret;
	u8 cap_bit;
	u8 sel_val;
	u8 preserve_val;
	u8 val8;

	switch (powermode) {
	case MPP_POWERMODE_CONTINUOUS:
		cap_bit = CPS4041_TX_POWER_MODE_CAP_CPM_BIT;
		sel_val = 0;
		break;
	case MPP_POWERMODE_LIGHT:
		cap_bit = CPS4041_TX_POWER_MODE_CAP_LPM_BIT;
		sel_val = 2;
		break;
	case MPP_POWERMODE_NOMINAL:
		cap_bit = CPS4041_TX_POWER_MODE_CAP_NPM_BIT;
		sel_val = 1;
		break;
	case MPP_POWERMODE_HIGH:
		cap_bit = CPS4041_TX_POWER_MODE_CAP_HPM_BIT;
		sel_val = 3;
		break;
	default:
		dev_err(chgr->dev, "Invalid powermode requested: %s", mpp_powermode_str[powermode]);
		return -EINVAL;
	}

	ret = chgr->chip->reg_read_8(chgr, CPS4041_TX_POWER_MODE_CAP_INFO_REG, &val8);
	if (ret != 0)
		return ret;
	if ((val8 & cap_bit) == 0) {
		dev_err(chgr->dev, "Requested powermode %s not supported, cap_info: 0x%x",
			mpp_powermode_str[powermode], val8);
		return -EINVAL;
	}

	if (preserve_session)
		preserve_val = 1;
	else
		preserve_val = 2;

	val8 = 0;
	val8 |= sel_val << CPS4041_POWER_MODE_SEL_MAIN_MODE_SHIFT;
	val8 |= preserve_val << CPS4041_POWER_MODE_SEL_PREFERENCE_SHIFT;

	ret = chgr->chip->reg_write_8(chgr, CPS4041_POWER_MODE_SEL_REG, val8);
	if (ret != 0)
		return ret;

	ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_POWER_MODE_CHG, 10);
	if (ret != 0)
		return ret;

	return ret;
}

static int cps4041_chip_set_dynamic_mod(struct google_wlc_data *chgr)
{
	int ret = 0;
	u8 level = chgr->pdata->mod_depth_max;
	bool enable = level > 0;

	if (!chgr->mod_enable)
		return 0;

	if (enable) {
		ret = chgr->chip->reg_write_8(chgr, CPS4041_DYNAMIC_MOD_MAX_DEPTH_REG, level);
		if (ret == 0)
			ret = cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_DYNAMIC_MOD_EN,
							true);
	}
	if (ret || !enable)
		cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_DYNAMIC_MOD_EN, false);

	if (ret) {
		dev_err(chgr->dev, "fail to set dynamic modulation");
		return ret;
	}
	dev_info(chgr->dev, "dynamic modulation %s, max depth: %d",
		 enable ? "enabled" : "disabled", level);

	return 0;
}

static int cps4041_get_xce(struct google_wlc_data *chgr, s8 *xce)
{
	int ret;
	u8 val8;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_CE_VAL_REG, &val8);

	if (ret == 0)
		*xce = (s8)val8;

	return ret;
}

static int cps4041_write_poweron_params(struct google_wlc_data *chgr)
{

	if (chgr->wlc_dc_debug_powermode)
		chgr->chip->reg_write_8(chgr, CPS4041_POWER_MODE_SEL_REG,
					chgr->wlc_dc_debug_powermode);
	if (chgr->wlc_dc_debug_gain_linear) {
		cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_GAIN_LINEAR_EN, 1);
		chgr->chip->reg_write_8(chgr, CSP4041_GAIN_LINEAR_COEFF_REG,
					chgr->wlc_dc_debug_gain_linear);
	}
	if (chgr->cloak_ping_delay_ms > 0)
		chgr->chip->reg_write_8(chgr, CPS4041_CLOAK_PING_DELAY_REG,
					chgr->cloak_ping_delay_ms / 500);
	chgr->chip->chip_set_dynamic_mod(chgr);

	return 0;
}

static int cps4041_do_dploss_event(struct google_wlc_data *chgr,
					   enum dploss_cal_event event)
{
	int ret = 0;

	switch (event) {
	case DPLOSS_CAL_ENTER:
		ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_DPLOSS_CALI_ENTER, 1000);
		break;
	case DPLOSS_CAL_EXTEND:
		ret = cps4041_chip_set_func_reg(chgr,
						CPS4041_FUNCTION_DPLOSS_CALI_EXTEND_EN, true);
		if (ret == 0)
			ret = cps4041_chip_set_cmd_reg(chgr,
						       CPS4041_COMMAND_DPLOSS_CALI_ENTER, 1000);
		break;
	case DPLOSS_CAL_CAPTURE:
		ret = chgr->chip->reg_write_8(chgr, CPS4041_DPLOSS_CALI_CAP_NUM_REG,
			chgr->pdata->dploss_points_num);
		if (ret == 0)
			ret = cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_DPLOSS_CALI_CAP_EN,
							true);
		break;
	case DPLOSS_CAL_COMMIT:
		ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_DPLOSS_CALI_CMT, 1000);
		break;
	case DPLOSS_CAL_EXIT:
		ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_DPLOSS_CALI_EXIT, 1000);
		break;
	case DPLOSS_CAL_ABORT:
		cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_DPLOSS_CALI_CAP_EN, false);
		break;
	default:
		dev_err(chgr->dev, "Unable to handle dploss event: %d", event);
		break;
	}

	return ret;
}

/* FW update */

static int cps4041_chip_fw_reg_read_n(struct google_wlc_data *chgr,
				      unsigned int reg,
				      void *data, size_t n)
{
	int ret;

	mutex_lock(&chgr->io_lock);
	ret = regmap_raw_read(chgr->chip->fw_regmap, reg, data, n * 4);
	if (ret < 0)
		ret = (ret == -ENOTCONN) ? -ENODEV : ret;
	mutex_unlock(&chgr->io_lock);
	return ret;

}

static int cps4041_chip_fw_reg_read_32(struct google_wlc_data *chgr,
				       unsigned int reg,
				       unsigned int *val)
{
	return cps4041_chip_fw_reg_read_n(chgr, reg, val, 1);
}

static int cps4041_chip_fw_reg_write_n(struct google_wlc_data *chgr,
				       unsigned int reg,
				       const void *data, size_t n)
{
	int ret;

	mutex_lock(&chgr->io_lock);
	ret = regmap_raw_write(chgr->chip->fw_regmap, reg, data, n * 4);
	if (ret < 0)
		ret = (ret == -ENOTCONN) ? -ENODEV : ret;
	mutex_unlock(&chgr->io_lock);
	return ret;
}

static int cps4041_chip_fw_reg_write_32(struct google_wlc_data *chgr,
					unsigned int reg,
					const unsigned int val)
{
	return cps4041_chip_fw_reg_write_n(chgr, reg, &val, 1);
}

static int cps4041_validation(struct google_wlc_data *chgr)
{
	int ret, i;
	u32 status_code;

	for (i = 0; i < 30; i++) {
		if (i != 0)
			msleep(100);
		DEBUG_I2C_LOG(chgr, "attempt %d read 0x%08x", i, CPS4041_FW_VALIDATION_REG);
		ret = cps4041_chip_fw_reg_read_32(chgr, CPS4041_FW_VALIDATION_REG, &status_code);
		if (ret < 0) {
			logbuffer_devlog(chgr->fw_log, chgr->dev,
					 "read 0x%08x failed", CPS4041_FW_VALIDATION_REG);
			return ret;
		}
		if (status_code == CPS_VALIDATION_SUCCESSFUL) {
			ret = 0;
			break;
		}
		DEBUG_I2C_LOG(chgr, "verification status=0x%08x", status_code);
		ret = -EIO;
		if (status_code == CPS_VALIDATION_PERFORMED)
			continue;
	}

	return ret;
}

static int cps4041_bootloader_download(struct google_wlc_data *chgr,
				       const struct firmware *bl_data)
{
	int ret;

	logbuffer_devlog(chgr->fw_log, chgr->dev, "bootloader download start");

	DEBUG_I2C_LOG(chgr, "Enable 32-bit wide address access right");
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_32BIT_EN_REG, CPS4041_32BIT_EN);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev,
				 "enable 32-bit wide address access failed");
		return ret;
	}

	DEBUG_I2C_LOG(chgr, "Configure password");
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_PASSWORD_REG, CPS4041_BL_PWD);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "configure password failed");
		return ret;
	}

	DEBUG_I2C_LOG(chgr, "Halt the MCU");
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_CHIP_CONTROL_REG, CPS4041_MCU_HALT);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "halt the MCU failed");
		return ret;
	}
	DEBUG_I2C_LOG(chgr, "Load the bootloader code to the SRAM");
	ret = cps4041_chip_fw_reg_write_n(chgr, CPS4041_BOOTLOADER_START_REG,
					  bl_data->data, bl_data->size / 4);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "write bootloader failed");
		return ret;
	}

	DEBUG_I2C_LOG(chgr, "Enable regmap function");
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_REGMAP_FUNC_REG, CPS4041_REGMAP_EN);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "enable regmap function failed");
		return ret;
	}

	DEBUG_I2C_LOG(chgr,  "Restart the MCU");
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_CHIP_CONTROL_REG, CPS4041_MCU_RESTART);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "restart the MCU failed");
		return ret;
	}

	return ret;
}

static int cps4041_bootloader_verification(struct google_wlc_data *chgr)
{
	int ret;

	logbuffer_devlog(chgr->fw_log, chgr->dev, "bootloader verification start");
	msleep(20);

	DEBUG_I2C_LOG(chgr, "enable 32-bit wide register address access");
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_32BIT_EN_REG, CPS4041_32BIT_EN);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "enable 32-bit wide addr failed");
		return ret;
	}

	DEBUG_I2C_LOG(chgr, "enable bootloader verification");
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_FW_DOWNLOAD_REG, CPS4041_BL_VERIFY);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "enable bootloader verification failed");
		return ret;
	}

	ret = cps4041_validation(chgr);
	logbuffer_devlog(chgr->fw_log, chgr->dev,
			 "Check bootloader verify result:%s", ret == 0 ? "verify" : "failed");

	return ret;
}

static int cps4041_program_firmware(struct google_wlc_data *chgr,
				    const struct firmware *fw_data)
{
	int ret, offset, bytes_remaining;
	u32 start_addr, mtp_val, code_length;
	u16 data_buffer;
	size_t fw_size;

	DEBUG_I2C_LOG(chgr, "write block size: %d bytes(%d DWORD)",
		      CPS4041_FWDATA_BUF_BYTES, CPS4041_FWDATA_BUF_BYTES / 4);
	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_DATA_BUF_SIZE_REG,
					   CPS4041_FWDATA_BUF_BYTES / 4);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev,
				 "write fw block size fail");
		return ret;
	}

	if (chgr->fw_data.erase_fw)
		fw_size = fw_data->size / 2;
	else
		fw_size = fw_data->size;

	for (offset = 0; offset < fw_size; offset += CPS4041_FWDATA_BUF_BYTES) {
		bytes_remaining = fw_data->size - offset;

		logbuffer_devlog(chgr->fw_log, chgr->dev,
				 "offset=%d, bytes_remaining=%d", offset, bytes_remaining);

		data_buffer = (offset / CPS4041_FWDATA_BUF_BYTES) % 2;
		if (data_buffer == 0) {
			start_addr = CPS4041_DATA_BUF0_REG;
			mtp_val = CPS4041_DATA0_TO_MTP;
		} else {
			start_addr = CPS4041_DATA_BUF1_REG;
			mtp_val = CPS4041_DATA1_TO_MTP;
		}

		code_length = min(CPS4041_FWDATA_BUF_BYTES, bytes_remaining);
		DEBUG_I2C_LOG(chgr, "offset=%d len=%d data_buffer=%d",
			      offset, code_length, data_buffer);

		ret = cps4041_chip_fw_reg_write_n(chgr, start_addr, &fw_data->data[offset],
						  code_length / 4);
		if (ret < 0) {
			logbuffer_devlog(chgr->fw_log, chgr->dev,
					 "write fw at %d failed", offset);
			return ret;
		}

		if (offset != 0)
			ret = cps4041_validation(chgr);
		if (ret < 0) {
			logbuffer_devlog(chgr->fw_log, chgr->dev,
					 "validate fw at %d failed", offset);
			return ret;
		}

		ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_FW_DOWNLOAD_REG, mtp_val);
		if (ret < 0) {
			logbuffer_devlog(chgr->fw_log, chgr->dev,
					 "write data to mtp at %d failed", offset);
			return ret;
		}

	}
	ret = cps4041_validation(chgr);
	if (ret < 0)
		DEBUG_I2C_LOG(chgr, "validate fw at %d failed", offset);

	return ret;
}

static int cps4041_firmware_verify(struct google_wlc_data *chgr)
{
	int ret;

	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_FW_DOWNLOAD_REG,
					   CPS4041_CRC_VERIFICATION);
	if (ret < 0)
		logbuffer_devlog(chgr->fw_log, chgr->dev, "enable verification failed");

	ret = cps4041_validation(chgr);
	if (ret < 0)
		logbuffer_devlog(chgr->fw_log, chgr->dev, "firmware verification failed");
	return ret;
}

static int cps4041_chip_reset(struct google_wlc_data *chgr)
{
	int ret;
	u32 status_code;

	ret = cps4041_chip_fw_reg_read_32(chgr, CPS4041_FW_UPDATE_STATUS_REG, &status_code);
	if (ret == 0)
		logbuffer_devlog(chgr->fw_log, chgr->dev, "status_code=0x%08x", status_code);

	ret = cps4041_chip_fw_reg_write_32(chgr, CPS4041_CHIP_CONTROL_REG, CPS4041_CHIP_RESET);
	if (ret < 0)
		logbuffer_devlog(chgr->fw_log, chgr->dev, "reset CPS4041 failed");

	msleep(100);

	return ret;
}
static int cps4041_chip_fwupdate_init(struct google_wlc_data *chgr)
{
	int ret;
	const char *bl_name = NULL, *fw_name = NULL;

	ret = of_property_read_string(chgr->dev->of_node, "google,wlc-bootloader", &bl_name);
	if (ret != 0)
		return ret;
	chgr->pdata->bl_name = devm_kstrdup(chgr->dev, bl_name, GFP_KERNEL);

	ret = of_property_read_string(chgr->dev->of_node, "google,wlc-firmware", &fw_name);
	if (ret != 0)
		return ret;
	chgr->pdata->fw_name = devm_kstrdup(chgr->dev, fw_name, GFP_KERNEL);

	logbuffer_devlog(chgr->fw_log, chgr->dev, "bl=%s,fw=%s",
			 chgr->pdata->bl_name, chgr->pdata->fw_name);

	return ret;
}

static int cps4041_firmware_request(struct google_wlc_data *chgr,
				    const struct firmware **bl_data,
				    const struct firmware **fw_data)
{
	const char *bl_name = chgr->pdata->bl_name,
		   *fw_name = chgr->pdata->fw_name,
		   *blank_name = "blank_fw.bin";
	struct wlc_fw_ver ver = { 0 };
	int ret;

	/* upload stats for errors on loading firmware */
	chgr->fw_data.ver.major = -1;
	chgr->fw_data.ver.minor = -1;

	if (!chgr->fw_data.update_support)
		return -EINVAL;

	DEBUG_I2C_LOG(chgr, "init bootloader data");
	ret = request_firmware(bl_data, bl_name, chgr->dev);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "init bootloader data failed");
		chgr->fw_data.update_support = false;
		return -EINVAL;
	}
	DEBUG_I2C_LOG(chgr, "init firmware data");
	if (chgr->fw_data.erase_fw)
		fw_name = blank_name;
	ret = request_firmware(fw_data, fw_name, chgr->dev);
	if (ret < 0) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "init firmware data failed");
		chgr->fw_data.update_support = false;
		return -EINVAL;
	}

	ver.project_id = (*fw_data)->data[CPS4041_FW_PROJECTID_OFFSET];
	ver.major = (*fw_data)->data[CPS4041_FW_MAJOR_OFFSET];
	ver.minor = (*fw_data)->data[CPS4041_FW_MINOR_OFFSET];

	chgr->fw_data.ver.project_id = ver.project_id;
	if (ver.project_id != chgr->fw_data.cur_ver.project_id && chgr->fw_data.erase_fw == false) {
		logbuffer_devlog(chgr->fw_log, chgr->dev, "fw file with wrong project id=%02x",
				 ver.project_id);
		chgr->fw_data.update_support = false;
		return -EINVAL;
	}

	switch (chgr->fw_data.update_option) {
	case FWUPDATE_DISABLE:
		chgr->fw_data.needs_update = false;
		break;
	case FWUPDATE_FORCE:
	case FWUPDATE_FORCE_MANUAL:
		chgr->fwupdate_state = FWU_STATE_IN_PROGRESS;
		chgr->fw_data.needs_update = true;
		break;
	case FWUPDATE_DIFF:
		if (ver.major != chgr->fw_data.cur_ver.major ||
		    ver.minor != chgr->fw_data.cur_ver.minor) {
			chgr->fw_data.needs_update = true;
		} else {
			chgr->fw_data.needs_update = false;
			chgr->fw_data.update_done = true;
		}
		break;
	case FWUPDATE_NEW:
		if ((ver.major >= chgr->fw_data.cur_ver.major &&
		    ver.minor > chgr->fw_data.cur_ver.minor) ||
		    (ver.major > chgr->fw_data.cur_ver.major))  {
			chgr->fw_data.needs_update = true;
		} else {
			chgr->fw_data.needs_update = false;
			chgr->fw_data.update_done = true;
		}
		break;
	default:
		chgr->fw_data.needs_update = false;
		chgr->fw_data.update_support = false;
		break;
	}

	logbuffer_devlog(chgr->fw_log, chgr->dev,
			 "fw=0x%02x 0x%02x, request=0x%02x 0x%02x, option=%d, needs_update=%d",
			 chgr->fw_data.cur_ver.major, chgr->fw_data.cur_ver.minor, ver.major,
			 ver.minor, chgr->fw_data.update_option, chgr->fw_data.needs_update);
	return 0;
}

static int cps4041_read_request_firmware(struct google_wlc_data *chgr)
{
	const struct firmware *fw_data = NULL;
	int ret = 0;

	if (chgr->pdata->fw_name == NULL)
		return -EINVAL;

	ret = request_firmware(&fw_data, chgr->pdata->fw_name, chgr->dev);
	if (ret < 0)
		return ret;

	chgr->fw_data.req_ver.major = fw_data->data[CPS4041_FW_MAJOR_OFFSET];
	chgr->fw_data.req_ver.minor = fw_data->data[CPS4041_FW_MINOR_OFFSET];

	release_firmware(fw_data);

	return ret;
}

static int cps4041_read_firmware_version(struct google_wlc_data *chgr, struct wlc_fw_ver *ver)
{
	int ret;
	u8 val8;

	ret = chgr->chip->reg_read_8(chgr, CPS4041_FW_MAJOR_REG, &val8);
	if (ret == 0)
		ver->major = val8;
	ret = chgr->chip->reg_read_8(chgr, CPS4041_FW_MINOR_REG, &val8);
	if (ret == 0)
		ver->minor = val8;
	ret = chgr->chip->reg_read_8(chgr, CPS4041_PROJECT_ID_REG, &val8);
	if (ret == 0)
		ver->project_id = val8;
	/* Update FW according to dtsi settings */
	if (ver->project_id != chgr->pdata->project_id && chgr->pdata->project_id > 0)
		ver->project_id = chgr->pdata->project_id;

	return ret;
}

static int cps4041_crc_check(struct google_wlc_data *chgr, u64 *crc)
{
	int ret = 0;
	u8 val8 = 0;
	u16 val;

	/* it takes up to 100ms for waiting mode */
	for (int i = 0; i < BACKPOWER_CHECK_RETRY_COUNT; i++) {
		msleep(BACKPOWER_CHECK_DELAY_MS);
		ret = chgr->chip->reg_read_8(chgr, CPS4041_SYS_MODE_REG, &val8);
		if (ret != 0) {
			ret = -FWUPDATE_ERROR_I2C;
			continue;
		}
		if (val8 == CPS4041_SYS_MODE_BACKPOWER)
			break;

		if (val8 == CPS4041_SYS_MODE_EMPTY)
			ret = -FWUPDATE_ERROR_CRC;
		else
			ret = -FWUPDATE_ERROR_BACKPOWER;
	}

	if (ret != 0) {
		if (ret == -FWUPDATE_ERROR_CRC)
			dev_err(chgr->dev, "Empty firmware, mode reg: %#02x", val8);
		else if (ret == -FWUPDATE_ERROR_BACKPOWER)
			dev_err(chgr->dev, "Not in backpower mode, mode reg: %#02x", val8);

		return ret;
	}

	/*
	 * it takes up to 1s + 2s for setting crc computation and reading crc
	 * even though cps suggests waiting up to 2s for setting command
	 */
	msleep(BACKPOWER_CHECK_DELAY_MS);
	ret = cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_CRC_CHECK, 1000);
	if (ret != 0)
		return -FWUPDATE_ERROR_I2C;

	for (int i = 0; i < CRC_CHECK_RETRY_COUNT; i++) {
		msleep(CRC_CHECK_DELAY_MS);
		ret = chgr->chip->reg_read_16(chgr, CPS4041_CRC_VAL_REG, &val);
		if (ret != 0)
			return -FWUPDATE_ERROR_I2C;
		if (val != 0)
			break;
	}
	*crc = val;

	return ret;
}

static int cps4041_chip_fwupdate(struct google_wlc_data *chgr, int step)
{
	const struct firmware *bl_data = NULL, *fw_data = NULL;
	int ret = 0;
	uint32_t fw_tag;

	switch (step) {
	case FW_UPDATE_STEP:
		mutex_lock(&chgr->fwupdate_lock);
		if (chgr->fwupdate_mode || chgr->fw_data.update_option == FWUPDATE_DISABLE) {
			ret = 0;
			mutex_unlock(&chgr->fwupdate_lock);
			break;
		}
		ret = cps4041_firmware_request(chgr, &bl_data, &fw_data);
		if (ret < 0 || !chgr->fw_data.needs_update) {
			chgr->fwupdate_state = FWU_STATE_FAIL;
			mutex_unlock(&chgr->fwupdate_lock);
			break;
		}
		logbuffer_devlog(chgr->fw_log, chgr->dev, "fw_size =%zu, bl_size=%zu",
				 fw_data->size, bl_data->size);
		ret = cps4041_bootloader_download(chgr, bl_data);
		ret |= cps4041_bootloader_verification(chgr);
		if (ret < 0) {
			chgr->fwupdate_state = FWU_STATE_FAIL;
			ret = -FWUPDATE_ERROR_BOOTLOADER;
			mutex_unlock(&chgr->fwupdate_lock);
			break;
		}
		DEBUG_I2C_LOG(chgr, "bootloader download complete");

		logbuffer_devlog(chgr->fw_log, chgr->dev, "firmware size =%zu", fw_data->size);
		DEBUG_I2C_LOG(chgr, "program the firmware");
		ret = cps4041_program_firmware(chgr, fw_data);
		if (ret < 0) {
			chgr->fwupdate_state = FWU_STATE_FAIL;
			ret = -FWUPDATE_ERROR_FLASH;
			mutex_unlock(&chgr->fwupdate_lock);
			break;
		}

		if (chgr->fw_data.erase_fw) {
			logbuffer_devlog(chgr->fw_log, chgr->dev, "firmware erase complete");
			break;
		}
		logbuffer_devlog(chgr->fw_log, chgr->dev, "firmware programming successful");

		DEBUG_I2C_LOG(chgr, "firmware verify start");
		ret = cps4041_firmware_verify(chgr);
		if (ret < 0) {
			logbuffer_devlog(chgr->fw_log, chgr->dev, "firmware verify failed");
			ret = -FWUPDATE_ERROR_FLASH;
			mutex_unlock(&chgr->fwupdate_lock);
			break;
		}

		/* update is done once the firmware is verified */
		chgr->fw_data.update_done = true;

		logbuffer_devlog(chgr->fw_log, chgr->dev, "chip reset");
		ret = cps4041_chip_reset(chgr);

		mutex_unlock(&chgr->fwupdate_lock);
		if (ret == 0) {
			ret = cps4041_read_firmware_version(chgr, &chgr->fw_data.ver);
			if (ret < 0)
				logbuffer_devlog(chgr->fw_log, chgr->dev,
						 "fail to read fw version, ret=%d", ret);
			ret = cps4041_crc_check(chgr, &chgr->fw_data.ver.crc);
			if (ret == 0 && chgr->fw_data.ver.crc == 0)
				ret = -FWUPDATE_ERROR_CRC;
			if (ret < 0) {
				chgr->fwupdate_state = FWU_STATE_FAIL;
				logbuffer_devlog(chgr->fw_log, chgr->dev,
						 "fail to read crc, ret=%d", ret);
			} else {
				chgr->fwupdate_state = FWU_STATE_SUCCESS;
			}

			logbuffer_devlog(chgr->fw_log, chgr->dev,
					 "major=%02x,minor=%02x,project_id=%02x,crc=%04llx",
					 chgr->fw_data.ver.major, chgr->fw_data.ver.minor,
					 chgr->fw_data.ver.project_id,
					 chgr->fw_data.ver.crc);
			logbuffer_devlog(chgr->fw_log, chgr->dev,
					 "firmware download is completed successfully\n");
			if (chgr->fw_data.ver_tag <= 0)
				break;
			fw_tag = chgr->fw_data.ver_tag << 16;
			fw_tag |= chgr->fw_data.ver.major << 8 | chgr->fw_data.ver.minor;
			ret = gbms_storage_write(GBMS_TAG_WLFW, &fw_tag, sizeof(fw_tag));
			if (ret < 0)
				logbuffer_devlog(chgr->fw_log, chgr->dev,
						 "fail to store fw_tag, ret=%d", ret);
			else
				logbuffer_devlog(chgr->fw_log, chgr->dev,
						 "store fw_tag=0x%08x\n", fw_tag);
		} else {
			logbuffer_devlog(chgr->fw_log, chgr->dev, "firmware reset failed\n");
		}

		break;
	case CRC_VERIFY_STEP:
		if (chgr->fw_data.cur_ver.crc > 0)
			break;
		logbuffer_devlog(chgr->fw_log, chgr->dev, "CRC check start");
		chgr->fw_data.cur_ver.crc = 0;
		ret = cps4041_crc_check(chgr, &chgr->fw_data.cur_ver.crc);
		if (ret == 0 && chgr->fw_data.cur_ver.crc == 0)
			ret = -FWUPDATE_ERROR_CRC;

		if (ret < 0) {
			chgr->fw_data.update_done = false;
			chgr->fw_data.cur_ver.project_id = chgr->pdata->project_id;
			if (chgr->fw_data.update_option != FWUPDATE_FORCE_MANUAL &&
			    ret == -FWUPDATE_ERROR_CRC)
				chgr->fw_data.update_option = FWUPDATE_FORCE;
			logbuffer_devlog(chgr->fw_log, chgr->dev,
					 "CRC check failed,ret=%d,crc=0x%04llx\n",
					 ret, chgr->fw_data.cur_ver.crc);
			break;
		}
		if (cps4041_read_firmware_version(chgr, &chgr->fw_data.cur_ver))
			logbuffer_devlog(chgr->fw_log, chgr->dev, "Fail to read fw version");
		logbuffer_devlog(chgr->fw_log, chgr->dev, "fw.major=%02x,minor=%02x,crc=%04llx\n",
				 chgr->fw_data.cur_ver.major, chgr->fw_data.cur_ver.minor,
				 chgr->fw_data.cur_ver.crc);
		break;
	case READ_REQ_FWVER:
		ret = cps4041_read_request_firmware(chgr);
		if (ret < 0)
			logbuffer_devlog(chgr->fw_log, chgr->dev, "Fail to get req_fw version");
		break;
	default:
		ret = -EINVAL;
		logbuffer_devlog(chgr->fw_log, chgr->dev, "unknown step\n");
		break;
	}

	if (bl_data)
		release_firmware(bl_data);
	if (fw_data)
		release_firmware(fw_data);

	return ret;
}
/* FW update */

static int cps4041_get_vinv(struct google_wlc_data *chgr, u32 *val)
{
	struct google_wlc_packet packet;
	int ret, timeout;
	size_t len;

	packet.header = 0x28;
	packet.cmd = 0;
	packet.data[0] = 0x02;
	ret = chgr->chip->chip_send_packet(chgr, packet);
	if (ret)
		return ret;

	chgr->pkt_ready = -EBUSY;
	timeout = 100;
	while (chgr->pkt_ready != 1) {
		if (timeout <= 0 || chgr->pkt_ready == -ETIME)
			return -ETIME;
		timeout -= 20;
		msleep(20);
	}

	ret = chgr->chip->chip_get_packet(chgr, &packet, &len);
	if (ret)
		return ret;

	*val = (packet.data[0] << 8 | packet.data[1])*2;

	return ret;
}

static int cps4041_pdet_en_store(void *data, u64 val)
{
	struct google_wlc_data *chgr = data;

	return cps4041_pdet_en(chgr, val);
}
DEFINE_SIMPLE_ATTRIBUTE(debug_pdet_fops, NULL, cps4041_pdet_en_store, "%lld\n");

static int cps4041_powermode_show(void *data, u64 *val)
{
	struct google_wlc_data *chgr = data;

	*val = chgr->wlc_dc_debug_powermode;
	return 0;
}

static int cps4041_powermode_store(void *data, u64 val)
{
	struct google_wlc_data *chgr = data;

	if (val > 0xff)
		return -EINVAL;

	chgr->wlc_dc_debug_powermode = val;
	if (val > 0)
		chgr->chip->reg_write_8(chgr, CPS4041_POWER_MODE_SEL_REG, (u8) val);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_powermode_fops, cps4041_powermode_show,
			cps4041_powermode_store, "%lld\n");

static int cps4041_gain_linear_show(void *data, u64 *val)
{
	struct google_wlc_data *chgr = data;

	*val = chgr->wlc_dc_debug_gain_linear;
	return 0;
}

static int cps4041_gain_linear_store(void *data, u64 val)
{
	struct google_wlc_data *chgr = data;
	int ret;

	if (val > 0xff)
		return -EINVAL;

	chgr->wlc_dc_debug_gain_linear = val;
	if (val > 0) {
		cps4041_chip_set_func_reg(chgr, CPS4041_FUNCTION_GAIN_LINEAR_EN, 1);
		ret = chgr->chip->reg_write_8(chgr, CSP4041_GAIN_LINEAR_COEFF_REG, (u8) val);
		if (ret == 0)
			cps4041_chip_set_cmd_reg(chgr, CPS4041_COMMAND_GAIN_LINEAR_COEFF_CHANGE,
						 1000);
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_gain_linear_fops, cps4041_gain_linear_show,
			cps4041_gain_linear_store, "%lld\n");

static int cps4041_fsk_log_en_store(void *data, u64 val)
{
	struct google_wlc_data *chgr = data;
	u32 mask_val;
	int ret;

	chgr->fsk_log = val;

	if (val == 0)
		return 0;

	ret = chgr->chip->reg_read_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
	if (ret == 0) {
		mask_val |= CPS4041_INTERRUPT_FSK_PKT_BIT;
		chgr->chip->reg_write_n(chgr, CPS4041_INT_EN_REG, &mask_val, 4);
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_fsk_log_fops, NULL, cps4041_fsk_log_en_store, "%lld\n");

int cps4041_chip_init(struct google_wlc_data *chgr)
{
	struct google_wlc_chip *chip = chgr->chip;

	chip->reg_chipid = CPS4041_CHIP_ID_REG;
	chip->reg_vout = CPS4041_ADC_VOUT_REG;
	chip->reg_vrect = CPS4041_ADC_VRECT_REG;
	chip->reg_iout = CPS4041_ADC_IRECT_REG;
	chip->reg_status = CPS4041_STATUS_REG;
	chip->reg_fw_major = CPS4041_FW_MAJOR_REG;
	chip->reg_fw_minor = CPS4041_FW_MINOR_REG;
	chip->reg_die_temp = CPS4041_ADC_TMP_DIE_REG;
	chip->reg_op_freq = CPS4041_FOP_VAL_REG;
	chip->reg_fod_qf = CPS4041_FOD_QF_REG;
	chip->reg_fod_rf = CPS4041_FOD_RF_REG;
	chip->num_fods = CPS4041_NUM_FODS;
	chip->reg_fod_start = CPS4041_FOD_COEF_START_REG;
	chip->reg_eds_send_size = CPS4041_EDS0_SIZE_REG;
	chip->reg_eds_send_buf = CPS4041_EDS0_REG;
	chip->reg_eds_recv_size = CPS4041_EDS0_SIZE_REG;
	chip->reg_eds_recv_buf = CPS4041_EDS0_REG;
	chip->reg_eds_stream = CPS4041_EDS0_NUMBER_REG;
	chip->reg_adt_err = -1;
	chip->reg_eds_status = CPS4041_EDS0_FLAG_REG;
	chip->val_eds_status_busy = CPS4041_EDS_FLAG_BUSY;
	chip->tx_buf_size = CPS4041_DATA_BUF_SIZE;
	chip->rx_buf_size = CPS4041_DATA_BUF_SIZE;
	chip->reg_txid_buf = CPS4041_TXID_BUF;
	chip->reg_ptmc_id = CPS4041_PTMC_REG;
	chip->reg_qi_version = CPS4041_TX_QI_VERSION_REG;
	chip->reg_tx_rcs = CPS4041_TX_RCS_REG;
	chip->num_mplas = CPS4041_NUM_MPLA;
	chip->reg_mpla_start = CPS4041_MPLA_REG;
	chip->reg_rf_curr_start = CPS4041_RF_CURRENT_REG;
	chip->num_rf_currs = CPS4041_NUM_RF_CURRENT;
	chip->reg_limit_rsn = CPS4041_POWER_LIMIT_RSN_REG;
	chip->val_eds_stream_fwupdate = CPS4041_EDS_STREAM_FWUPDATE;
	chip->reg_alpha_fm_itx = CPS4041_ALPHA_FM_ITX_REG;
	chip->reg_alpha_fm_vrect = CPS4041_ALPHA_FM_VRECT_REG;
	chip->reg_alpha_fm_irect = CPS4041_ALPHA_FM_IRECT_REG;
	chip->val_default_vout = CPS4041_DEFAULT_VOUT_SET;
	chip->reg_qi_id = CPS4041_QI_ID_REG;

	chip->chip_get_sys_mode = cps4041_get_sys_mode;
	chip->chip_get_vout_set = cps4041_get_vout_set;
	chip->chip_set_vout = cps4041_set_vout;
	chip->chip_get_vrect_target = cps4041_get_vrect_target;
	chip->chip_add_info_string = cps4041_add_info_string;
	chip->chip_get_interrupts = cps4041_get_interrupts;
	chip->chip_clear_interrupts = cps4041_clear_interrupts;
	chip->chip_enable_interrupts = cps4041_enable_interrupts;
	chip->chip_get_status_fields = cps4041_get_status_fields;
	chip->chip_set_cloak_mode = cps4041_set_cloak_mode;
	chip->chip_send_csp = cps4041_send_csp;
	chip->chip_send_ept = cps4041_send_ept;
	chip->chip_get_cloak_reason = cps4041_get_cloak_reason;
	chip->chip_get_cloak_exit_reason = cps4041_get_cloak_exit_reason;
	chip->chip_send_sadt = cps4041_send_sadt;
	chip->chip_eds_reset = cps4041_eds_reset;
	chip->chip_get_negotiated_power = cps4041_get_negotiated_power;
	chip->chip_get_potential_power = cps4041_get_potential_power;
	chip->chip_enable_load_increase = cps4041_enable_load_increase;
	chip->chip_fw_reg_read_n = cps4041_fw_reg_read_n;
	chip->chip_fw_reg_write_n = cps4041_fw_reg_write_n;
	chip->chip_get_mpp_xid = cps4041_get_mpp_xid;
	chip->chip_get_tx_kest = cps4041_get_tx_kest;
	chip->chip_get_project_id = cps4041_get_project_id;
	chip->chip_send_packet = cps4041_send_packet;
	chip->chip_get_packet = cps4041_get_packet;
	chip->chip_get_mated_q = cps4041_get_mated_q;
	chip->chip_set_mod_mode = cps4041_set_mod_mode;
	chip->chip_enable_auto_vout = cps4041_enable_auto_vout;
	chip->chip_set_mpp_powermode = cps4041_set_mpp_powermode;
	chip->chip_write_poweron_params = cps4041_write_poweron_params;
	chip->chip_do_dploss_event = cps4041_do_dploss_event;
	chip->chip_get_vinv = cps4041_get_vinv;
	chip->chip_get_mode_capabilities = cps4041_get_mode_capabilities;
	chip->chip_set_dynamic_mod = cps4041_chip_set_dynamic_mod;
	chip->chip_get_xce = cps4041_get_xce;

	debugfs_create_file("pdet_en", 0200, chgr->debug_entry, chgr, &debug_pdet_fops);
	debugfs_create_file("wlc_dc_powermode", 0644, chgr->debug_entry, chgr,
				&debug_powermode_fops);
	debugfs_create_file("wlc_dc_gain_linear", 0644, chgr->debug_entry, chgr,
				&debug_gain_linear_fops);
	debugfs_create_file("enable_fsk_log", 0200, chgr->debug_entry, chgr, &debug_fsk_log_fops);
	debugfs_create_u8("cal_power_log", 0644, chgr->debug_entry,
			   &chgr->debug_cal_power);
	debugfs_create_u8("dynamic_mod", 0644, chgr->debug_entry, &chgr->pdata->mod_depth_max);

	chip->fw_regmap = devm_regmap_init_i2c(chgr->client, &cps4041_fwupdate_regmap_config);
	if (IS_ERR(chip->fw_regmap))
		dev_err(chgr->dev, "FW Regmap initial failed, %ld\n", PTR_ERR(chip->fw_regmap));
	chgr->fw_data.update_support = cps4041_chip_fwupdate_init(chgr) == 0;
	chip->chip_fwupdate = cps4041_chip_fwupdate;

	return 0;
}
