// SPDX-License-Identifier: GPL-2.0
/*
 * Google MPP Wireless Charging Drive chip revision framework
 *
 * Copyright 2023 Google LLC
 *
 */

#pragma clang diagnostic ignored "-Wenum-conversion"
#pragma clang diagnostic ignored "-Wswitch"

#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include "google_wlc.h"
#include "google_wlc_chip.h"

static int google_wlc_chip_reg_read_n(struct google_wlc_data *charger, unsigned int reg,
			    void *buf, size_t n)
{
	int ret;

	mutex_lock(&charger->fwupdate_lock);
	if (charger->fwupdate_mode) {
		mutex_unlock(&charger->fwupdate_lock);
		return 0;
	}
	mutex_unlock(&charger->fwupdate_lock);

	mutex_lock(&charger->io_lock);
	ret = regmap_raw_read(charger->chip->regmap, reg, buf, n);
	mutex_unlock(&charger->io_lock);

	if (ret < 0) {
		/*
		 * Treat -ENOTCONN as -ENODEV to suppress the get/set
		 * prop warnings.
		 */
		const int nret = (ret == -ENOTCONN) ? -ENODEV : ret;

		/* Skip printing error if we are not present since error is expected there */
		if (google_wlc_is_present(charger))
			dev_err(&charger->client->dev,
				"i2c read error, reg:%x, ret:%d (%d)\n", reg, ret, nret);
		return nret;
	}

	if (charger->enable_i2c_debug) {
		const int buf_size = I2C_LOG_NUM * 3 + 1;
		int i, len = n;

		if (!charger->i2c_debug_buf)
			charger->i2c_debug_buf = kmalloc(buf_size, GFP_KERNEL);

		for (i = 0; len > 0; i++) {
			const int count = len > I2C_LOG_NUM ? I2C_LOG_NUM : len;
			const int offset = i * I2C_LOG_NUM;

			google_wlc_hex_str((u8 *)buf + offset, count,
				      charger->i2c_debug_buf, buf_size, 0);
			dev_info(charger->dev,
				 "i2c read %d bytes from reg %04x, offset: %04x: %s\n",
				 count, reg, offset, charger->i2c_debug_buf);
			len -= I2C_LOG_NUM;
		}
	}

	return ret;
}

static int google_wlc_chip_reg_read_16(struct google_wlc_data *charger, u16 reg,
			     u16 *val)
{
	u8 buf[2];
	int ret;

	ret = google_wlc_chip_reg_read_n(charger, reg, buf, 2);
	if (ret == 0)
		*val = (buf[1] << 8) | buf[0];
	return ret;
}

static int google_wlc_chip_reg_read_adc(struct google_wlc_data *charger, u16 reg,
					u16 *val)
{
	u8 buf_1[2], buf_2[2];
	int ret;

	ret = google_wlc_chip_reg_read_n(charger, reg, buf_1, 2);
	ret |= google_wlc_chip_reg_read_n(charger, reg, buf_2, 2);

	if (ret < 0)
		return ret;

	if (buf_1[1] == buf_2[1])
		*val = (buf_2[1] << 8) | buf_2[0];
	else
		*val = (buf_1[1] << 8) | buf_1[0];

	return ret;
}

static int google_wlc_chip_reg_read_8(struct google_wlc_data *charger,
			    u16 reg, u8 *val)
{
	return google_wlc_chip_reg_read_n(charger, reg, val, 1);
}

static int google_wlc_chip_reg_write_n(struct google_wlc_data *charger, u16 reg,
			     const void *buf, size_t n)
{
	int ret;

	mutex_lock(&charger->fwupdate_lock);
	if (charger->fwupdate_mode) {
		mutex_unlock(&charger->fwupdate_lock);
		return 0;
	}
	mutex_unlock(&charger->fwupdate_lock);

	mutex_lock(&charger->io_lock);
	ret = regmap_raw_write(charger->chip->regmap, reg, buf, n);
	mutex_unlock(&charger->io_lock);

	if (ret < 0) {
		/*
		 * Treat -ENOTCONN as -ENODEV to suppress the get/set
		 * prop warnings.
		 */
		const int nret = (ret == -ENOTCONN) ? -ENODEV : -EIO;

		dev_err(charger->dev,
			"%s: i2c write error, reg: 0x%x, n: %zd ret: %d (%d)\n",
			__func__, reg, n, ret, nret);
		return nret;
	}

	if (charger->enable_i2c_debug) {
		const int buf_size = I2C_LOG_NUM * 3 + 1;
		int i, len = n;

		if (!charger->i2c_debug_buf)
			charger->i2c_debug_buf = kmalloc(buf_size, GFP_KERNEL);

		for (i = 0; len > 0; i++) {
			const int count = len > I2C_LOG_NUM ? I2C_LOG_NUM : len;
			const int offset = i * I2C_LOG_NUM;

			google_wlc_hex_str((u8 *)buf + offset, count,
				      charger->i2c_debug_buf, buf_size, 0);
			dev_info(charger->dev,
				 "i2c write %d bytes to reg %04x, offset: %04x: %s\n",
				 count, reg, offset, charger->i2c_debug_buf);
			len -= I2C_LOG_NUM;
		}
	}

	return 0;
}

static int google_wlc_chip_reg_write_16(struct google_wlc_data *charger, u16 reg, u16 val)
{
	return google_wlc_chip_reg_write_n(charger, reg, &val, 2);
}

static int google_wlc_chip_reg_write_8(struct google_wlc_data *charger, u16 reg, u8 val)
{
	return google_wlc_chip_reg_write_n(charger, reg, &val, 1);
}

/* Functions that can be reasonably shared by chips, only changing the register address */

static bool google_wlc_chip_check_id(struct google_wlc_data *chgr)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_chipid < 0)
		return false;
	ret = chgr->chip->reg_read_16(chgr, chgr->chip->reg_chipid, &val);
	if (ret < 0)
		return false;

	if (val != chgr->chip_id) {
		dev_err(chgr->dev, "Chip ID Mismatch. Probe: 0x%04x, Register value: 0x%04x\n",
			chgr->chip_id, val);
		return false;
	}
	return true;
}

static int google_wlc_chip_get_vout(struct google_wlc_data *chgr, u32 *mv)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_vout < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_adc(chgr, chgr->chip->reg_vout, &val);
	if (ret == 0) {
		if (val > GOOGLE_WLC_READING_MAX_VOLT_MV)
			return -EINVAL;
		*mv = val;
	}
	return ret;
}

static int google_wlc_chip_get_vrect(struct google_wlc_data *chgr, u32 *mv)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_vrect < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_adc(chgr, chgr->chip->reg_vrect, &val);
	if (ret == 0) {
		if (val > GOOGLE_WLC_READING_MAX_VOLT_MV)
			return -EINVAL;
		*mv = val;
	}
	return ret;
}

static int google_wlc_chip_get_iout(struct google_wlc_data *chgr, u32 *ma)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_iout < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_adc(chgr, chgr->chip->reg_iout, &val);
	if (ret == 0) {
		if (val > GOOGLE_WLC_READING_MAX_IOUT_MA)
			return -EINVAL;
		*ma = val;
	}
	return ret;
}

static int google_wlc_chip_get_temp(struct google_wlc_data *chgr, u32 *millic)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_die_temp < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_adc(chgr, chgr->chip->reg_die_temp, &val);
	if (ret == 0) {
		if (val > GOOGLE_WLC_READING_MAX_TEMP_C)
			return -EINVAL;
		*millic = C_TO_MILLIC(val);
	}
	return ret;
}

static int google_wlc_chip_get_opfreq(struct google_wlc_data *chgr, u32 *khz)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_op_freq < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_adc(chgr, chgr->chip->reg_op_freq, &val);
	if (ret == 0) {
		if (val > GOOGLE_WLC_READING_MAX_FREQ_KHZ)
			return -EINVAL;
		*khz = val;
	}
	return ret;
}

static int google_wlc_chip_get_status(struct google_wlc_data *chgr, u16 *status)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_status < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_16(chgr, chgr->chip->reg_status, &val);
	if (ret == 0)
		*status = val;
	return ret;
}

static int google_wlc_chip_write_fod(struct google_wlc_data *chgr, int sys_mode)
{
	char buf[GOOGLE_WLC_MAX_FOD_NUM * 3 + 1];
	int fod_num;
	u8 *fod;

	switch (sys_mode) {
	case RX_MODE_WPC_BPP:
		fod_num = chgr->pdata->bpp_fod_num;
		fod = chgr->pdata->bpp_fods;
		break;
	case RX_MODE_WPC_EPP_NEGO:
	case RX_MODE_WPC_EPP:
		if (chgr->chip->reg_fod_qf > 0 && chgr->pdata->fod_qf > 0 &&
		    chgr->chip->reg_write_8(chgr, chgr->chip->reg_fod_qf, chgr->pdata->fod_qf) == 0)
			dev_dbg(chgr->dev, "writing EPP FOD QF=%02x\n", chgr->pdata->fod_qf);
		if (chgr->chip->reg_fod_rf > 0 && chgr->pdata->fod_rf > 0 &&
		    chgr->chip->reg_write_8(chgr, chgr->chip->reg_fod_rf, chgr->pdata->fod_rf) == 0)
			dev_dbg(chgr->dev, "writing EPP FOD RF=%02x\n", chgr->pdata->fod_rf);
		fod_num = chgr->pdata->epp_fod_num;
		fod = chgr->pdata->epp_fods;
		break;
	default:
		return 0;
	}

	if (chgr->de_fod_n > 0) {
		fod_num = chgr->de_fod_n;
		fod = chgr->de_fod;
	}

	if (chgr->chip->num_fods == -1 || chgr->chip->reg_fod_start == -1)
		return -EINVAL;

	if (fod_num == 0)
		return 0;

	if (fod_num != chgr->chip->num_fods) {
		dev_err(chgr->dev, "Invalid number of fods: %d but should be %d\n",
			fod_num, chgr->chip->num_fods);
		return -EINVAL;
	}

	google_wlc_hex_str(fod, fod_num, buf, fod_num * 3 + 1, false);
	dev_dbg(chgr->dev, "writing %s %d fods: %s\n", sys_op_mode_str[sys_mode], fod_num, buf);
	return chgr->chip->reg_write_n(chgr, chgr->chip->reg_fod_start, fod, fod_num);
}

static int google_wlc_chip_write_mpla(struct google_wlc_data *chgr)
{
	char buf[GOOGLE_WLC_MPLA_NUM_MAX * 3 + 1];
	int mpla_num;
	u8 *mpla;
	int ret;

	if (chgr->qi22_write_mpla2) {
		ret = chgr->chip->reg_write_16(chgr, chgr->chip->reg_alpha_fm_itx,
					 chgr->mpla2_alpha_fm_itx);
		ret |= chgr->chip->reg_write_n(chgr, chgr->chip->reg_alpha_fm_vrect,
					 &chgr->mpla2_alpha_fm_vrect, 4);
		ret |= chgr->chip->reg_write_16(chgr, chgr->chip->reg_alpha_fm_irect,
					 chgr->mpla2_alpha_fm_irect);
		if (ret < 0)
			return ret;
	}

	mpla_num = chgr->pdata->mpp_mpla_num;
	mpla = chgr->pdata->mpp_mplas;

	if (chgr->de_mpla_n > 0) {
		mpla_num = chgr->de_mpla_n;
		mpla = chgr->de_mpla;
	}

	if (chgr->chip->num_mplas == -1 || chgr->chip->reg_mpla_start == -1)
		return -EINVAL;

	if (mpla_num == 0)
		return 0;

	if (mpla_num != chgr->chip->num_mplas) {
		dev_err(chgr->dev, "Invalid number of mplas: %d but should be %d\n",
			mpla_num, chgr->chip->num_mplas);
		return -EINVAL;
	}

	google_wlc_hex_str(mpla, mpla_num, buf, mpla_num * 3 + 1, false);
	dev_dbg(chgr->dev, "writing %s MLPA(%d): %s\n", sys_op_mode_str[chgr->mode], mpla_num, buf);
	return chgr->chip->reg_write_n(chgr, chgr->chip->reg_mpla_start, mpla, mpla_num);
}

static int google_wlc_chip_write_rf_curr(struct google_wlc_data *chgr)
{
	char buf[GOOGLE_WLC_RF_CURR_NUM_MAX * 3 + 1];
	int rf_curr_num;
	u8 *rf_curr;

	rf_curr_num = chgr->pdata->rf_curr_num;
	rf_curr = chgr->pdata->rf_currs;

	if (chgr->de_rf_curr_n > 0) {
		rf_curr_num = chgr->de_rf_curr_n;
		rf_curr = chgr->de_rf_curr;
	}

	if (chgr->chip->num_rf_currs == -1 || chgr->chip->reg_rf_curr_start == -1)
		return -EINVAL;

	if (rf_curr_num == 0)
		return 0;

	if (rf_curr_num != chgr->chip->num_rf_currs) {
		dev_err(chgr->dev, "Invalid number of rf_currs: %d but should be %d\n",
			rf_curr_num, chgr->chip->num_rf_currs);
		return -EINVAL;
	}

	google_wlc_hex_str(rf_curr, rf_curr_num, buf, rf_curr_num * 3 + 1, false);
	dev_dbg(chgr->dev, "writing %s RF_CURR(%d): %s\n", sys_op_mode_str[chgr->mode],
		rf_curr_num, buf);
	return chgr->chip->reg_write_n(chgr, chgr->chip->reg_rf_curr_start,
				       rf_curr, rf_curr_num);
}

static int google_wlc_chip_check_eds_status(struct google_wlc_data *chgr)
{
	u8 val = 0;
	int ret;

	if (chgr->chip->reg_eds_status < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_8(chgr, chgr->chip->reg_eds_status, &val);
	if (val & chgr->chip->bit_eds_status_busy ||
	    (chgr->chip->val_eds_status_busy && val == chgr->chip->val_eds_status_busy)) {
		dev_err(chgr->dev, "eds busy, status: %u\n", val);
		return -EBUSY;
	}
	return ret;
}

static int google_wlc_eds_send_size(struct google_wlc_data *chgr, size_t len)
{
	if (!len || len > chgr->chip->tx_buf_size) {
		dev_err(chgr->dev, "invalid len to send eds, len: %lu\n", len);
		return -EINVAL;
	}

	if (chgr->chip->reg_eds_send_size < 0)
		return -EINVAL;

	return chgr->chip->reg_write_16(chgr, chgr->chip->reg_eds_send_size, len);
}

static int google_wlc_set_eds_data(struct google_wlc_data *chgr, u8 data[], size_t len)
{
	if (!data || chgr->chip->reg_eds_send_buf < 0)
		return -EINVAL;

	return chgr->chip->reg_write_n(chgr, chgr->chip->reg_eds_send_buf, data, len);
}

static int google_wlc_send_eds(struct google_wlc_data *chgr, u8 data[], size_t len, u8 type)
{
	int ret;
	u8 stream;

	/*
	 * proprietary used streams are chip-specific
	 * auth stream as default
	 * otherwise stream equals to type for qi-defined ones
	 */
	if (type == EDS_FW_UPDATE)
		stream = chgr->chip->val_eds_stream_fwupdate > 0 ?
			 chgr->chip->val_eds_stream_fwupdate : EDS_AUTH;
	else if (type != EDS_AUTH && type != EDS_THERMAL)
		stream = EDS_AUTH;
	else
		stream = type;

	dev_info(chgr->dev, "sending eds, len = %zu, type = %u, stream = %u\n", len, type, stream);

	ret = google_wlc_eds_send_size(chgr, len);

	if (ret == 0)
		ret = google_wlc_set_eds_data(chgr, data, len);

	if (ret == 0)
		ret = chgr->chip->chip_send_sadt(chgr, stream);

	return ret;
}

static int google_wlc_eds_recv_size(struct google_wlc_data *chgr, size_t *len)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_eds_recv_size < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_16(chgr, chgr->chip->reg_eds_recv_size, &val);
	if (ret == 0)
		*len = val;
	return ret;
}

static int google_wlc_get_eds_data(struct google_wlc_data *chgr, u8 data[], size_t len)
{
	if (!len || len > chgr->chip->rx_buf_size || !data) {
		dev_err(chgr->dev, "failed to find len or data buf, len: %lu\n", len);
		return -EINVAL;
	}

	if (chgr->chip->reg_eds_recv_buf < 0)
		return -EINVAL;

	return chgr->chip->reg_read_n(chgr, chgr->chip->reg_eds_recv_buf, data, len);
}

static int google_wlc_recv_eds(struct google_wlc_data *chgr, size_t *len, u8 *stream)
{
	int ret;
	u8 val = 0;

	ret = google_wlc_eds_recv_size(chgr, len);
	if (ret)
		return ret;

	if (chgr->chip->reg_eds_stream >= 0) {
		ret = chgr->chip->reg_read_8(chgr, chgr->chip->reg_eds_stream, &val);
		if (ret == 0) {
			if (val == chgr->chip->val_eds_stream_fwupdate)
				*stream = EDS_FW_UPDATE;
			else
				*stream = val;
		}
	}

	if (*len) {
		if (val == EDS_THERMAL)
			return google_wlc_get_eds_data(chgr, chgr->rx_thermal_buf, *len);

		return google_wlc_get_eds_data(chgr, chgr->rx_buf, *len);
	}

	dev_err(chgr->dev, "cc recv size 0 found\n");
	return -EINVAL;
}

static int google_wlc_get_adt_err(struct google_wlc_data *chgr, u8 *err)
{
	u8 val;
	int ret;

	if (chgr->chip->reg_adt_err < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_8(chgr, chgr->chip->reg_adt_err, &val);
	if (ret == 0)
		*err = val;
	return ret;
}

static int google_wlc_chip_get_load_step(struct google_wlc_data *chgr, s32 *ua)
{
	return -EINVAL;
}

static int google_wlc_chip_get_ptmc_id(struct google_wlc_data *chgr, u16 *id)
{
	u16 val;
	int ret;

	if (chgr->chip->reg_ptmc_id < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_16(chgr, chgr->chip->reg_ptmc_id, &val);
	if (ret == 0)
		*id = val;
	return ret;
}

static const char *google_wlc_chip_get_txid_str(struct google_wlc_data *chgr)
{
	int ret;
	u32 tx_id;

	if (!chgr->pdata->support_txid)
		return NULL;
	if (chgr->chip->reg_txid_buf < 0)
		return NULL;
	ret = chgr->chip->reg_read_n(chgr, chgr->chip->reg_txid_buf, &tx_id, 4);
	if (ret < 0)
		return NULL;

	scnprintf(chgr->tx_id_str, sizeof(chgr->tx_id_str), "%08x", tx_id);

	chgr->tx_id = tx_id;
	return chgr->tx_id_str;
}

static int google_wlc_chip_get_tx_qi_ver(struct google_wlc_data *chgr, u8 *ver)
{
	u8 val;
	int ret;

	if (chgr->chip->reg_qi_version < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_8(chgr, chgr->chip->reg_qi_version, &val);
	if (ret == 0)
		*ver = val;
	return ret;
}

static int google_wlc_chip_get_tx_rcs(struct google_wlc_data *chgr, u8 *rcs)
{
	u8 val;
	int ret;

	if (chgr->chip->reg_tx_rcs < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_8(chgr, chgr->chip->reg_tx_rcs, &val);
	if (ret == 0)
		*rcs = val & 0xf;
	return ret;
}

static int google_wlc_chip_get_limit_rsn(struct google_wlc_data *chgr, u8 *reason)
{
	u8 val;
	int ret;

	if (chgr->chip->reg_limit_rsn < 0)
		return -EINVAL;
	ret = chgr->chip->reg_read_8(chgr, chgr->chip->reg_limit_rsn, &val);
	if (ret == 0)
		*reason = val;
	return ret;
}

static int google_wlc_chip_set_qi_id(struct google_wlc_data *chgr, u16 qi_id)
{
	if (chgr->chip->reg_qi_id < 0)
		return -EINVAL;
	return chgr->chip->reg_write_16(chgr, chgr->chip->reg_qi_id, qi_id);
}

/* Functions that should be implemented individually by chip */

static int google_wlc_chip_get_vout_set(struct google_wlc_data *chgr, u32 *mv)
{
	return -EINVAL;
}

static int google_wlc_chip_get_vrect_target(struct google_wlc_data *chgr, u32 *mv)
{
	return -EINVAL;
}

static int google_wlc_chip_get_sys_mode(struct google_wlc_data *chgr, u8 *mode)
{
	return -EINVAL;
}

static int google_wlc_chip_add_info_string(struct google_wlc_data *chgr, char *buf)
{
	return -EINVAL;
}

static int google_wlc_chip_get_interrupts(struct google_wlc_data *chgr, u32 *int_val,
				   struct google_wlc_bits *int_fields)
{
	return -EINVAL;
}

static int google_wlc_chip_get_status_fields(struct google_wlc_data *chgr,
				      struct google_wlc_bits *status_fields)
{
	return -EINVAL;
}

static int google_wlc_chip_clear_interrupts(struct google_wlc_data *chgr, u32 int_val)
{
	return -EINVAL;
}

static int google_wlc_chip_enable_interrupts(struct google_wlc_data *chgr)
{
	return -EINVAL;
}

static int google_wlc_chip_set_cloak_mode(struct google_wlc_data *chgr, bool enable, u8 reason)
{
	return -EINVAL;
}

static int google_wlc_chip_send_csp(struct google_wlc_data *chgr)
{
	return -EINVAL;
}

static int google_wlc_chip_send_ept(struct google_wlc_data *chgr, enum ept_reason reason)
{
	return -EINVAL;
}

static int google_wlc_chip_get_cloak_reason(struct google_wlc_data *chgr, u8 *reason)
{
	return -EINVAL;
}

static int google_wlc_chip_send_sadt(struct google_wlc_data *chgr, u8 stream)
{
	return -EINVAL;
}

static int google_wlc_chip_eds_reset(struct google_wlc_data *chgr)
{
	return -EINVAL;
}

static int google_wlc_chip_get_negotiated_power(struct google_wlc_data *chgr, u32 *mw)
{
	return -EINVAL;
}

static int google_wlc_chip_get_potential_power(struct google_wlc_data *chgr, u32 *mw)
{
	return -EINVAL;
}

static int google_wlc_chip_enable_load_increase(struct google_wlc_data *chgr, bool enable)
{
	return -EINVAL;
}

static int google_wlc_fw_reg_read_n(struct google_wlc_data *chgr,
				    unsigned int reg, char *buf, size_t n)
{
	return -EINVAL;
}

static int google_wlc_fw_reg_write_n(struct google_wlc_data *chgr, u32 reg,
				     const char *buf, size_t n)
{
	return -EINVAL;
}

static int google_wlc_chip_get_mpp_xid(struct google_wlc_data *chgr, u32 *device_id,
				       u32 *mfg_rsvd_id, u32 *unique_id, u8 *product_id)
{
	return -EINVAL;
}

static int google_wlc_chip_get_tx_kest(struct google_wlc_data *chgr, u32 *kest)
{
	return -EINVAL;
}

static int google_wlc_chip_get_project_id(struct google_wlc_data *chgr, u8 *pid)
{
	return -EINVAL;
}

static int google_wlc_chip_send_packet(struct google_wlc_data *chgr,
				       struct google_wlc_packet packet)
{
	return -EINVAL;
}

static int google_wlc_chip_get_packet(struct google_wlc_data *chgr,
				      struct google_wlc_packet *packet, size_t *len)
{
	return -EINVAL;
}

static int google_wlc_chip_get_mated_q(struct google_wlc_data *chgr, u8 *res)
{
	return -EINVAL;
}

static int google_wlc_chip_set_mod_mode(struct google_wlc_data *chgr, enum ask_mod_mode mode)
{
	return -EINVAL;
}

static int google_wlc_chip_enable_auto_vout(struct google_wlc_data *chgr, bool enable)
{
	return -EINVAL;
}

static int google_wlc_chip_set_mpp_powermode(struct google_wlc_data *chgr,
					     enum mpp_powermode powermode, bool preserve_session)
{
	return -EINVAL;
}

static int google_wlc_chip_write_poweron_params(struct google_wlc_data *chgr)
{
	return -EINVAL;
}

static int google_wlc_chip_do_dploss_event(struct google_wlc_data *chgr,
					   enum dploss_cal_event event)
{
	return -EINVAL;
}

static int google_wlc_chip_fwupdate(struct google_wlc_data *chgr, int step)
{
	return -EINVAL;
}

static int google_wlc_chip_get_vinv(struct google_wlc_data *chgr, u32 *mv)
{
	return -EINVAL;
}

static int google_wlc_chip_get_mode_capabilities(struct google_wlc_data *chgr,
						 struct mode_cap_data *cap)
{
	return -EINVAL;
}

static int google_wlc_chip_set_dynamic_mod(struct google_wlc_data *chgr)
{
	return -EINVAL;
}

static int google_wlc_chip_get_xce(struct google_wlc_data *chgr, s8 *xce)
{
	return -EINVAL;
}
/* Init */

static int google_wlc_chip_init_funcs(struct google_wlc_data *chgr)
{
	struct google_wlc_chip *chip = chgr->chip;
	int ret = 0;

	chip->reg_chipid = -1;
	chip->reg_vout = -1;
	chip->reg_vrect = -1;
	chip->reg_iout = -1;
	chip->reg_die_temp = -1;
	chip->reg_op_freq = -1;
	chip->reg_status = -1;
	chip->reg_fw_major = -1;
	chip->reg_fw_minor = -1;
	chip->num_fods = -1;
	chip->reg_fod_start = -1;
	chip->reg_eds_recv_size = -1;
	chip->reg_eds_recv_buf = -1;
	chip->reg_eds_stream = -1;
	chip->reg_adt_err = -1;
	chip->reg_eds_send_size = -1;
	chip->reg_eds_send_buf = -1;
	chip->reg_eds_status = -1;
	chip->reg_ptmc_id = -1;
	chip->reg_txid_buf = -1;
	chip->reg_qi_version = -1;
	chip->reg_tx_rcs = -1;
	chip->reg_fod_qf = -1;
	chip->reg_fod_rf = -1;
	chip->num_mplas = -1;
	chip->reg_mpla_start = -1;
	chip->num_rf_currs = -1;
	chip->reg_rf_curr_start = -1;
	chip->reg_limit_rsn = -1;
	chip->val_eds_stream_fwupdate = -1;
	chip->val_default_vout = -1;
	chip->reg_qi_id = -1;

	/* Initialize default i2c functions */
	chip->reg_read_n = google_wlc_chip_reg_read_n;
	chip->reg_read_8 = google_wlc_chip_reg_read_8;
	chip->reg_read_16 = google_wlc_chip_reg_read_16;
	chip->reg_read_adc = google_wlc_chip_reg_read_adc;
	chip->reg_write_n = google_wlc_chip_reg_write_n;
	chip->reg_write_8 = google_wlc_chip_reg_write_8;
	chip->reg_write_16 = google_wlc_chip_reg_write_16;

	/* Initialize default shared functions */
	chip->chip_check_id = google_wlc_chip_check_id;
	chip->chip_get_vout = google_wlc_chip_get_vout;
	chip->chip_get_vrect = google_wlc_chip_get_vrect;
	chip->chip_get_iout = google_wlc_chip_get_iout;
	chip->chip_get_sys_mode = google_wlc_chip_get_sys_mode;
	chip->chip_get_status = google_wlc_chip_get_status;
	chip->chip_get_temp = google_wlc_chip_get_temp;
	chip->chip_get_opfreq = google_wlc_chip_get_opfreq;
	chip->chip_write_fod = google_wlc_chip_write_fod;
	chip->chip_get_load_step = google_wlc_chip_get_load_step;
	chip->chip_get_ptmc_id = google_wlc_chip_get_ptmc_id;
	chip->chip_get_txid_str = google_wlc_chip_get_txid_str;
	chip->chip_get_tx_qi_ver = google_wlc_chip_get_tx_qi_ver;
	chip->chip_get_tx_rcs = google_wlc_chip_get_tx_rcs;
	chip->chip_get_limit_rsn = google_wlc_chip_get_limit_rsn;

	/* Initialize default empty functions */
	chip->chip_get_vout_set = google_wlc_chip_get_vout_set;
	chip->chip_get_vrect_target = google_wlc_chip_get_vrect_target;
	chip->chip_add_info_string = google_wlc_chip_add_info_string;
	chip->chip_get_interrupts = google_wlc_chip_get_interrupts;
	chip->chip_clear_interrupts = google_wlc_chip_clear_interrupts;
	chip->chip_enable_interrupts = google_wlc_chip_enable_interrupts;
	chip->chip_get_status_fields = google_wlc_chip_get_status_fields;
	chip->chip_set_cloak_mode = google_wlc_chip_set_cloak_mode;
	chip->chip_send_csp = google_wlc_chip_send_csp;
	chip->chip_send_ept = google_wlc_chip_send_ept;
	chip->chip_get_cloak_reason = google_wlc_chip_get_cloak_reason;
	chip->chip_recv_eds = google_wlc_recv_eds;
	chip->chip_send_eds = google_wlc_send_eds;
	chip->chip_get_adt_err = google_wlc_get_adt_err;
	chip->chip_send_sadt = google_wlc_chip_send_sadt;
	chip->chip_eds_reset = google_wlc_chip_eds_reset;
	chip->chip_check_eds_status = google_wlc_chip_check_eds_status;
	chip->chip_get_negotiated_power = google_wlc_chip_get_negotiated_power;
	chip->chip_get_potential_power = google_wlc_chip_get_potential_power;
	chip->chip_enable_load_increase = google_wlc_chip_enable_load_increase;
	chip->chip_fw_reg_read_n = google_wlc_fw_reg_read_n;
	chip->chip_fw_reg_write_n = google_wlc_fw_reg_write_n;
	chip->chip_get_mpp_xid = google_wlc_chip_get_mpp_xid;
	chip->chip_get_tx_kest = google_wlc_chip_get_tx_kest;
	chip->chip_get_project_id = google_wlc_chip_get_project_id;
	chip->chip_write_mpla = google_wlc_chip_write_mpla;
	chip->chip_write_rf_curr = google_wlc_chip_write_rf_curr;
	chip->chip_send_packet = google_wlc_chip_send_packet;
	chip->chip_get_packet = google_wlc_chip_get_packet;
	chip->chip_get_mated_q = google_wlc_chip_get_mated_q;
	chip->chip_set_mod_mode = google_wlc_chip_set_mod_mode;
	chip->chip_enable_auto_vout = google_wlc_chip_enable_auto_vout;
	chip->chip_set_mpp_powermode = google_wlc_chip_set_mpp_powermode;
	chip->chip_write_poweron_params = google_wlc_chip_write_poweron_params;
	chip->chip_do_dploss_event = google_wlc_chip_do_dploss_event;
	chip->chip_fwupdate = google_wlc_chip_fwupdate;
	chip->chip_get_vinv = google_wlc_chip_get_vinv;
	chip->chip_get_mode_capabilities = google_wlc_chip_get_mode_capabilities;
	chip->chip_set_dynamic_mod = google_wlc_chip_set_dynamic_mod;
	chip->chip_set_qi_id = google_wlc_chip_set_qi_id;
	chip->chip_get_xce = google_wlc_chip_get_xce;

	/* Chip specific functions and registers. These may override the defaults */
	switch (chgr->chip_id) {
	case RA9582_CHIP_ID:
		ret = ra9582_chip_init(chgr);
		break;
	case CPS4041_CHIP_ID:
		ret = cps4041_chip_init(chgr);
		break;
	default:
		dev_err(chgr->dev, "Unrecognized chip ID specified: %u\n",
			chgr->chip_id);
		ret = 0;
		break;
	}

	if (chip->rx_buf_size)
		chgr->rx_buf = devm_kzalloc(chgr->dev, chip->rx_buf_size, GFP_KERNEL);

	if (chip->tx_buf_size)
		chgr->tx_buf = devm_kzalloc(chgr->dev, chip->tx_buf_size, GFP_KERNEL);

	return ret;
}

int google_wlc_chip_init(struct google_wlc_data *chgr)
{
	struct google_wlc_chip *chip;
	int ret = 0;

	chip = devm_kzalloc(chgr->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chgr->chip = chip;
	chip->regmap = devm_regmap_init_i2c(chgr->client, &google_wlc_default_regmap_config);
	dev_info(chgr->dev, "Regmap initialized\n");
	ret = google_wlc_chip_init_funcs(chgr);
	if (ret < 0)
		return ret;
	return 0;
}
