// SPDX-License-Identifier: GPL-2.0
/*
 * RA9530 Wireless Charging Drive chip specific functions
 *
 * Copyright 2023 Google LLC
 *
 */

#pragma clang diagnostic ignored "-Wenum-conversion"
#pragma clang diagnostic ignored "-Wswitch"

#include "google_wlc_chip.h"
#include "google_wlc.h"
#include "ra9582_wlc_chip.h"
#include <linux/i2c.h>
#include <linux/delay.h>

static int ra9582_get_vout_set(struct google_wlc_data *chgr, u32 *mv)
{
	u8 val;
	int ret;

	ret = chgr->chip->reg_read_8(chgr, RA9582_VOUT_SET_REG, &val);
	if (ret == 0)
		*mv = val * 100 + 3500;
	return ret;
}

static int ra9582_get_vrect_target(struct google_wlc_data *chgr, u32 *mv)
{
	u16 val;
	int ret;

	ret = chgr->chip->reg_read_16(chgr, RA9582_VRECT_TARGET_REG, &val);
	if (ret == 0)
		*mv = val * 25200 / 4096;
	return ret;
}


static int ra9582_get_sys_mode(struct google_wlc_data *chgr, u8 *mode)
{
	u8 val8;
	int ret = chgr->chip->reg_read_8(chgr, RA9582_SYS_OP_MODE_REG, &val8);

	if (ret < 0) {
		dev_err(chgr->dev, "Failed to read sys mode: %d\n", ret);
		return ret;
	}

	switch (val8) {
	case RA9582_SYS_OP_MODE_AC_MISSING:
		*mode = RX_MODE_AC_MISSING;
		break;
	case RA9582_SYS_OP_MODE_WPC_BPP:
		*mode = RX_MODE_WPC_BPP;
		break;
	case RA9582_SYS_OP_MODE_WPC_EPP:
		*mode = RX_MODE_WPC_EPP;
		break;
	case RA9582_SYS_OP_MODE_WPC_MPP:
		*mode = RX_MODE_WPC_MPP;
		break;
	case RA9582_SYS_OP_MODE_WPC_CLOAK:
		*mode = RX_MODE_MPP_CLOAK;
		break;
	case RA9582_SYS_OP_MODE_WPC_MPP_RESTRICTED:
		*mode = RX_MODE_WPC_MPP_RESTRICTED;
		break;
	default:
		dev_err(chgr->dev, "Unrecognized sys mode: %02x\n", val8);
		*mode = RX_MODE_UNKNOWN;
		break;
	}
	return 0;
}

static int ra9582_add_info_string(struct google_wlc_data *chgr, char *buf)
{
	int count = 0;
	int ret;
	u16 val16;
	u8 val8;
	int i;

	count += scnprintf(buf + count, PAGE_SIZE - count,
				   "chip id (dt) : %04x\n", chgr->chip_id);

	ret = chgr->chip->reg_read_16(chgr, RA9582_CHIP_ID_REG, &val16);
	if (ret) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "Could not read from device\n");
		return count;
	}
	count += scnprintf(buf + count, PAGE_SIZE - count,
				   "chip id      : %04x\n", val16);
	ret = chgr->chip->reg_read_8(chgr, RA9582_CHIP_REVISION_REG, &val8);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "chip rev     : %02x\n", val8);

	ret = chgr->chip->reg_read_8(chgr, RA9582_CUSTOMER_ID_REG, &val8);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "cust id      : %02x\n", val8);

	ret = chgr->chip->reg_read_16(chgr, RA9582_FIRMWARE_MAJOR_REG, &val16);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "fw major     : %04x\n", val16);

	ret = chgr->chip->reg_read_16(chgr, RA9582_FIRMWARE_MINOR_REG, &val16);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "fw minor     : %04x\n", val16);

	count += scnprintf(buf + count, PAGE_SIZE - count, "fw date      : ");
	for (i = 0; i < RA9582_FW_DATE_SIZE; i++) {
		ret = chgr->chip->reg_read_8(chgr,
				       RA9582_FIRMWARE_DATE_REG + i, &val8);
		if (ret == 0)
			count += scnprintf(buf + count, PAGE_SIZE - count,
					   "%c", val8);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

	count += scnprintf(buf + count, PAGE_SIZE - count, "fw time      : ");
	for (i = 0; i < RA9582_FW_TIME_SIZE; i++) {
		ret = chgr->chip->reg_read_8(chgr,
				       RA9582_FIRMWARE_TIME_REG + i, &val8);
		if (ret == 0)
			count += scnprintf(buf + count, PAGE_SIZE - count,
					   "%c", val8);
	}
	count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

	ret = chgr->chip->reg_read_16(chgr, RA9582_WPC_MANUFACTURER_REG, &val16);
	if (ret == 0)
		count += scnprintf(buf + count, PAGE_SIZE - count,
				   "wpc mfg code : %04x\n", val16);

	return count;
}

static int ra9582_get_interrupts(struct google_wlc_data *chgr, u32 *int_val,
				 struct google_wlc_bits *int_fields)
{
	u16 val16;
	int ret;

	ret = chgr->chip->reg_read_16(chgr, RA9582_INTERRUPT_REG, &val16);
	if (ret != 0)
		return ret;
	*int_val = val16;

	if (val16 & RA9582_INTERRUPT_SADT_ERR_BIT)
		int_fields->sadt_error = 1;
	if (val16 & RA9582_INTERRUPT_OVER_TEMP_BIT)
		int_fields->over_temperature = 1;
	if (val16 & RA9582_INTERRUPT_OVER_CURR_BIT)
		int_fields->over_current = 1;
	if (val16 & RA9582_INTERRUPT_OVER_VOLT_BIT)
		int_fields->over_voltage = 1;
	if (val16 & RA9582_INTERRUPT_MODE_BIT)
		int_fields->operation_mode = 1;
	if (val16 & RA9582_INTERRUPT_VRECT_BIT)
		int_fields->stat_vrect = 1;
	if (val16 & RA9582_INTERRUPT_VOUT_BIT)
		int_fields->stat_vout = 1;
	if (val16 & RA9582_INTERRUPT_SADT_SENT_BIT)
		int_fields->sadt_sent = 1;
	if (val16 & RA9582_INTERRUPT_SADT_RCVD_BIT)
		int_fields->sadt_received = 1;
	if (val16 & RA9582_INTERRUPT_PROP_RCVD_BIT)
		int_fields->prop_received = 1;
	if (val16 & RA9582_INTERRUPT_PTC_BIT)
		int_fields->power_adjust = 1;
	if (val16 & RA9582_INTERRUPT_LOAD_DECREASE_BIT)
		int_fields->load_decrease_alert = 1;
	if (val16 & RA9582_INTERRUPT_LOAD_INCREASE_BIT)
		int_fields->load_increase_alert = 1;

	return 0;
}

static int ra9582_enable_interrupts(struct google_wlc_data *chgr)
{
	u16 mask_val;
	int ret;

	mask_val = RA9582_INTERRUPT_SADT_ERR_BIT | RA9582_INTERRUPT_OVER_TEMP_BIT |
		   RA9582_INTERRUPT_OVER_CURR_BIT | RA9582_INTERRUPT_OVER_VOLT_BIT |
		   RA9582_INTERRUPT_MODE_BIT | RA9582_INTERRUPT_VRECT_BIT |
		   RA9582_INTERRUPT_VOUT_BIT | RA9582_INTERRUPT_SADT_SENT_BIT |
		   RA9582_INTERRUPT_SADT_RCVD_BIT | RA9582_INTERRUPT_PROP_RCVD_BIT |
		   RA9582_INTERRUPT_PTC_BIT | RA9582_INTERRUPT_LOAD_DECREASE_BIT;


	ret = chgr->chip->reg_write_16(chgr, RA9582_INTERRUPT_ENABLE_REG, mask_val);
	dev_info(chgr->dev, "Enabled interrupts: %04x, ret: %d\n", mask_val, ret);
	return ret;
}

static int ra9582_chip_set_cmd_reg(struct google_wlc_data *chgr, u16 cmd)
{
	u16 cur_cmd = 0;
	int retry;
	int ret;

	for (retry = 0; retry < RA9582_COMMAND_SEND_RETRIES; retry++) {
		ret = chgr->chip->reg_read_16(chgr, RA9582_SYSTEM_COMMAND_REG, &cur_cmd);
		if (ret == 0 && cur_cmd == 0)
			break;
		/* If we got disconnected, exit early */
		if (ret && chgr->status == GOOGLE_WLC_STATUS_NOT_DETECTED)
			return ret;
		msleep(25);
	}

	if (retry >= RA9582_COMMAND_SEND_RETRIES) {
		dev_err(chgr->dev,
			"Failed to wait for cmd free %02x\n", cur_cmd);
		return -EBUSY;
	}

	ret = chgr->chip->reg_write_16(chgr, RA9582_SYSTEM_COMMAND_REG, cmd);
	if (ret) {
		dev_err(chgr->dev,
			"Failed to set cmd reg %02x: %d\n", cmd, ret);
		return ret;
	}

	for (retry = 0; retry < RA9582_COMMAND_SEND_RETRIES; retry++) {
		ret = chgr->chip->reg_read_16(chgr, RA9582_SYSTEM_COMMAND_REG, &cur_cmd);
		if (ret == 0 && cur_cmd == 0)
			break;
		/* If we got disconnected, exit early */
		if (ret && chgr->status == GOOGLE_WLC_STATUS_NOT_DETECTED)
			return ret;
		msleep(25);
	}

	if (retry >= RA9582_COMMAND_SEND_RETRIES) {
		dev_err(chgr->dev,
			"Cmd reg failed to go back to 0: %02x\n", cur_cmd);
		return -EBUSY;
	}
	return ret;
}

static int ra9582_clear_interrupts(struct google_wlc_data *chgr, u32 int_val)
{
	u16 clear_val = int_val;
	u16 val16;
	int ret;

	ret = chgr->chip->reg_write_16(chgr, RA9582_INTERRUPT_CLEAR_REG, clear_val);
	if (ret)
		return ret;

	ret = ra9582_chip_set_cmd_reg(chgr, RA9582_COMMAND_CLEAR_IRQ);
	if (ret)
		return ret;
	dev_dbg(chgr->dev, "Cleared interrupts: %04x, ret: %d\n", clear_val, ret);

	ret = chgr->chip->reg_read_16(chgr, RA9582_INTERRUPT_REG, &val16);
	if (ret)
		return ret;
	if (val16 != 0)
		dev_warn(chgr->dev, "Interrupts still present after clear: %04x\n", val16);

	return ret;
}

static int ra9582_set_cloak_mode(struct google_wlc_data *chgr, bool enable, u8 reason)
{
	u8 val8;
	int ret;

	ret = chgr->chip->reg_read_8(chgr, RA9582_SYS_OP_MODE_REG, &val8);
	if (ret)
		return ret;
	if (enable && val8 == RA9582_SYS_OP_MODE_WPC_MPP) {
		dev_info(chgr->dev, "Entering Cloak Mode");
		return ra9582_chip_set_cmd_reg(chgr, RA9582_COMMAND_CLOAK_TOGGLE);
	} else if (!enable && val8 == RA9582_SYS_OP_MODE_WPC_CLOAK) {
		dev_info(chgr->dev, "Exiting Cloak Mode");
		return ra9582_chip_set_cmd_reg(chgr, RA9582_COMMAND_CLOAK_TOGGLE);
	}
	dev_err(chgr->dev, "Invalid cloak mode parameters: Mode: %u, enable: %u", val8, enable);
	return -EINVAL;
}

/* not supporting multi stream */
static int ra9582_send_sadt(struct google_wlc_data *chgr, u8 stream)
{
	return ra9582_chip_set_cmd_reg(chgr, RA9582_COMMAND_SEND_ADT);
}

static int ra9582_eds_reset(struct google_wlc_data *chgr)
{
	int ret;

	ret = chgr->chip->reg_write_16(chgr, RA9582_EDS_RESET_REG, 0xFFFF);
	if (ret == 0)
		ret = chgr->chip->chip_send_sadt(chgr, 0);
	return ret;
}

static int ra9582_get_status_fields(struct google_wlc_data *chgr,
				    struct google_wlc_bits *status_fields)
{
	u16 val16;
	int ret;

	ret = chgr->chip->reg_read_16(chgr, RA9582_STATUS_REG, &val16);
	if (ret != 0)
		return ret;

	if (val16 & RA9582_INTERRUPT_SADT_ERR_BIT)
		status_fields->sadt_error = 1;
	if (val16 & RA9582_INTERRUPT_OVER_TEMP_BIT)
		status_fields->over_temperature = 1;
	if (val16 & RA9582_INTERRUPT_OVER_CURR_BIT)
		status_fields->over_current = 1;
	if (val16 & RA9582_INTERRUPT_OVER_VOLT_BIT)
		status_fields->over_voltage = 1;
	if (val16 & RA9582_INTERRUPT_MODE_BIT)
		status_fields->operation_mode = 1;
	if (val16 & RA9582_INTERRUPT_VRECT_BIT)
		status_fields->stat_vrect = 1;
	if (val16 & RA9582_INTERRUPT_VOUT_BIT)
		status_fields->stat_vout = 1;
	if (val16 & RA9582_INTERRUPT_SADT_SENT_BIT)
		status_fields->sadt_sent = 1;
	if (val16 & RA9582_INTERRUPT_SADT_RCVD_BIT)
		status_fields->sadt_received = 1;
	if (val16 & RA9582_INTERRUPT_PROP_RCVD_BIT)
		status_fields->prop_received = 1;

	return 0;
}

static int ra9582_send_csp(struct google_wlc_data *chgr)
{
	int ret;
	u8 to_send = chgr->last_capacity;

	if (chgr->last_capacity <= 0)
		return 0;

	if (chgr->last_capacity > 100)
		to_send = 100;

	ret = chgr->chip->reg_write_8(chgr, RA9582_CHARGE_STATUS_REG, to_send);
	if (ret) {
		dev_err(chgr->dev, "Failed to set csp: %d\n", ret);
		return ret;
	}

	dev_info(chgr->dev, "Sending CSP, soc: %d\n", to_send);

	return ra9582_chip_set_cmd_reg(chgr, RA9582_COMMAND_SEND_CHG_STATUS);
}

static int ra9582_send_ept(struct google_wlc_data *chgr, enum ept_reason reason)
{
	u8 ept_value;
	int ret;

	switch (reason) {
	case EPT_UNKNOWN:
		ept_value = 0;
		break;
	case EPT_END_OF_CHARGE:
		ept_value = 1;
		break;
	case EPT_INTERNAL_FAULT:
		ept_value = 2;
		break;
	case EPT_OVER_TEMP:
		ept_value = 3;
		break;
	case EPT_OVER_VOLTAGE:
		ept_value = 4;
		break;
	case EPT_OVER_CURRENT:
		ept_value = 5;
		break;
	case EPT_BATTERY_FAILURE:
		ept_value = 6;
		break;
	case EPT_NO_RESPONSE:
		ept_value = 8;
		break;
	case EPT_ABORT_NEGOTIATION:
		ept_value = 10;
		break;
	case EPT_RESTART_POWER_TRANSFER:
		ept_value = 11;
		break;
	case EPT_REPING:
		ept_value = 12;
		break;
	case EPT_NFC_RX:
		ept_value = 13;
		break;
	case EPT_NFC_TX:
		ept_value = 14;
		break;
	default:
		return -EINVAL;
	}

	ret = chgr->chip->reg_write_8(chgr, RA9582_END_OF_POWER_TRANSFER_REG, ept_value);
	if (ret) {
		dev_err(chgr->dev, "Failed to set ept: %d\n", ret);
		return ret;
	}

	dev_info(chgr->dev, "Sending EPT, value: %d\n", ept_value);

	return ra9582_chip_set_cmd_reg(chgr, RA9582_COMMAND_SEND_EOP);
}

static int ra9582_get_cloak_reason(struct google_wlc_data *chgr, u8 *reason)
{
	int ret;
	u8 val8;

	ret = chgr->chip->reg_read_8(chgr, RA9582_SYS_OP_MODE_REG, &val8);
	if (val8 != RA9582_SYS_OP_MODE_WPC_CLOAK)
		return -EINVAL;
	ret = chgr->chip->reg_read_8(chgr, RA9582_CLOAK_REASON_REG, &val8);
	if (ret)
		return ret;
	if (val8 == RA9582_CLOAK_REASON_TX_INITATED_VAL)
		*reason = CLOAK_TX_INITIATED;
	else
		*reason = CLOAK_GENERIC;

	return 0;
}

static int ra9582_get_negotiated_power(struct google_wlc_data *chgr, u32 *mw)
{
	u8 val;
	int ret;

	ret = chgr->chip->reg_read_8(chgr, RA9582_EXTENDED_GUARANTEED_LOAD_POWER_REG, &val);
	if (ret == 0)
		*mw = val * 100;

	return ret;
}

static int ra9582_enable_load_increase(struct google_wlc_data *chgr, bool enable)
{
	int ret;
	u16 mask_val;

	ret = chgr->chip->reg_read_16(chgr, RA9582_INTERRUPT_ENABLE_REG, &mask_val);

	if (ret != 0)
		return ret;

	if (enable)
		mask_val |= RA9582_INTERRUPT_LOAD_INCREASE_BIT;
	else
		mask_val &= ~RA9582_INTERRUPT_LOAD_INCREASE_BIT;

	ret = chgr->chip->reg_write_16(chgr, RA9582_INTERRUPT_ENABLE_REG, mask_val);

	dev_info(chgr->dev, "Load increase config set: %d, ret: %d\n", enable, ret);

	return ret;
}

int ra9582_chip_init(struct google_wlc_data *chgr)
{
	struct google_wlc_chip *chip = chgr->chip;

	chip->reg_chipid = RA9582_CHIP_ID_REG;
	chip->reg_vout = RA9582_VOUT_VALUE_REG;
	chip->reg_vrect = RA9582_VRECT_VALUE_REG;
	chip->reg_iout = RA9582_IOUT_VALUE_REG;
	chip->reg_status = RA9582_STATUS_REG;
	chip->reg_fw_major = RA9582_FIRMWARE_MAJOR_REG;
	chip->reg_fw_minor = RA9582_FIRMWARE_MINOR_REG;
	chip->reg_die_temp = RA9582_DIE_TEMP_REG;
	chip->reg_op_freq = RA9582_OP_FREQ_REG;
	chip->num_fods = RA9582_NUM_FODS;
	chip->reg_fod_start = RA9582_FOD_COEF_START_REG;
	chip->reg_eds_send_size = RA9582_SEND_SIZE_REG;
	chip->reg_eds_send_buf = RA9582_SEND_BUF_REG;
	chip->reg_eds_recv_size = RA9582_RECV_SIZE_REG;
	chip->reg_eds_recv_buf = RA9582_RECV_BUF_REG;
	chip->reg_adt_err = RA9582_ADT_ERR_REG;
	chip->reg_eds_status = RA9582_EDS_STATUS_REG;
	chip->bit_eds_status_busy = RA9582_EDS_STATUS_BUSY_BITS;
	chip->tx_buf_size = RA9582_DATA_BUF_SIZE;
	chip->rx_buf_size = RA9582_DATA_BUF_SIZE;

	chip->chip_get_sys_mode = ra9582_get_sys_mode;
	chip->chip_get_vout_set = ra9582_get_vout_set;
	chip->chip_get_vrect_target = ra9582_get_vrect_target;
	chip->chip_add_info_string = ra9582_add_info_string;
	chip->chip_get_interrupts = ra9582_get_interrupts;
	chip->chip_clear_interrupts = ra9582_clear_interrupts;
	chip->chip_enable_interrupts = ra9582_enable_interrupts;
	chip->chip_get_status_fields = ra9582_get_status_fields;
	chip->chip_set_cloak_mode = ra9582_set_cloak_mode;
	chip->chip_send_csp = ra9582_send_csp;
	chip->chip_send_ept = ra9582_send_ept;
	chip->chip_get_cloak_reason = ra9582_get_cloak_reason;
	chip->chip_send_sadt = ra9582_send_sadt;
	chip->chip_eds_reset = ra9582_eds_reset;
	chip->chip_get_negotiated_power = ra9582_get_negotiated_power;
	chip->chip_enable_load_increase = ra9582_enable_load_increase;
	return 0;
}
