// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

#define DPTX_NO_DEBUG_REG

#include "dptx.h"
#include "regmaps/ctrl_fields.h"

static const char *dptx_aux_sts_to_str(u32 status)
{
	switch (status) {
	case DPTX_AUX_STS_STATUS_ACK:
		return "AUX_ACK";
		break;
	case DPTX_AUX_STS_STATUS_NACK:
		return "AUX_NACK";
		break;
	case DPTX_AUX_STS_STATUS_DEFER:
		return "AUX_DEFER";
		break;
	case DPTX_AUX_STS_STATUS_I2C_NACK:
		return "AUX_I2C_NACK";
		break;
	case DPTX_AUX_STS_STATUS_I2C_DEFER:
		return "AUX_I2C_DEFER";
		break;
	default:
		break;
	}

	return "AUX_STS_INVALID";
}

static int dptx_handle_aux_reply(struct dptx *dptx, u32 auxcmd, u32 *out_status, u32 *out_br)
{
	u32 auxsts;
	u32 status;
	u32 auxm;
	u32 br;
	int count = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	/*
	 * Wait for max 1000 usec for AUX reply
	 * DP spec says reply should come after 300 usec
	 */
	fsleep(200);

	while (1) {
		if (!dptx_read_regfield(dptx, ctrl_fields->field_aux_reply_received)) {
			dptx_dbg_aux(dptx, "%s: AUX reply received: count=%d\n", __func__, count);
			break;
		}

		if (++count > 80) {
			dptx_warn(dptx, "%s: AUX reply timeout: no reply\n", __func__);
			return -ETIMEDOUT;
		}

		fsleep(10);
	}

	auxsts = dptx_read_reg(dptx, dptx->regs[DPTX], AUX_STATUS);

	status = dptx_read_regfield(dptx, ctrl_fields->field_aux_status) >>
		DPTX_AUX_STS_STATUS_SHIFT;
	*out_status = status;

	auxm = dptx_read_regfield(dptx, ctrl_fields->field_aux_m);

	br = dptx_read_regfield(dptx, ctrl_fields->field_aux_bytes_read);
	*out_br = br;

	if (status == DPTX_AUX_STS_STATUS_ACK && br > 0) {
		dptx_dbg_aux(dptx,
			"%s: cmd=0x%08x sts=0x%08x: %s auxm=%d br=%d\n",
			__func__, auxcmd, auxsts, dptx_aux_sts_to_str(status), auxm, br);
	} else {
		dptx_warn(dptx,
			"%s: cmd=0x%08x sts=0x%08x: %s auxm=%d br=%d "
			"rrcv=%d rerr=%d errc=%d tout=%d disc=%d\n",
			__func__, auxcmd, auxsts, dptx_aux_sts_to_str(status), auxm, br,
			!!dptx_read_regfield(dptx, ctrl_fields->field_aux_reply_received),
			!!dptx_read_regfield(dptx, ctrl_fields->field_aux_reply_err),
			dptx_read_regfield(dptx, ctrl_fields->field_aux_reply_err_code),
			!!dptx_read_regfield(dptx, ctrl_fields->field_aux_timeout),
			!!dptx_read_regfield(dptx, ctrl_fields->field_sink_disconnect_while_active));
	}

	if (!!dptx_read_regfield(dptx, ctrl_fields->field_aux_timeout)) {
		dptx_warn(dptx, "%s: AUX reply timeout: aux_timeout=1\n", __func__);
		return -ETIMEDOUT;
	}

	dptx->aux.data[0] = dptx_read_reg(dptx, dptx->regs[DPTX], AUX_DATA0);
	dptx->aux.data[1] = dptx_read_reg(dptx, dptx->regs[DPTX], AUX_DATA1);
	dptx->aux.data[2] = dptx_read_reg(dptx, dptx->regs[DPTX], AUX_DATA2);
	dptx->aux.data[3] = dptx_read_reg(dptx, dptx->regs[DPTX], AUX_DATA3);
	dptx->aux.sts = auxsts;

	return 0;
}

static void dptx_aux_clear_data(struct dptx *dptx)
{
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA0, 0);
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA1, 0);
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA2, 0);
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA3, 0);
}

static int dptx_aux_read_data(struct dptx *dptx, u8 *bytes, unsigned int len)
{
	unsigned int i;

	u32 *data = dptx->aux.data;

	for (i = 0; i < len; i++)
		bytes[i] = (data[i / 4] >> ((i % 4) * 8)) & 0xff;

	return len;
}

static int dptx_aux_write_data(struct dptx *dptx, u8 const *bytes,
			       unsigned int len)
{
	unsigned int i;
	u32 data[4];

	memset(data, 0, sizeof(u32) * 4);

	for (i = 0; i < len; i++)
		data[i / 4] |= (bytes[i] << ((i % 4) * 8));

	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA0, data[0]);
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA1, data[1]);
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA2, data[2]);
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_DATA3, data[3]);

	return len;
}

static int dptx_aux_rw(struct dptx *dptx, bool rw, bool i2c, bool mot,
		       bool addr_only, u32 addr, u8 *bytes, unsigned int len)
{
	int retval;
	int tries = 0;
	u32 auxcmd;
	u32 type;
	u32 status = 0;
	u32 br = 0;
	struct ctrl_regfields *ctrl_fields;

	ctrl_fields = dptx->ctrl_fields;

	/* Wait up to 3200 usec if previous AUX transaction experienced timeout */
	if (dptx->aux.noreply_timeout != 0) {
		ktime_t now = ktime_get();
		if (ktime_before(now, dptx->aux.noreply_timeout))
			fsleep(ktime_us_delta(dptx->aux.noreply_timeout, now));
		dptx->aux.noreply_timeout = 0;
	}

again:
	tries++;
	if (tries > 20) {
		dptx_err(dptx, "AUX exceeded retries\n");
		return -EINVAL;
	}

	dptx_dbg_aux(dptx, "%s: addr=0x%08x, len=%d, try=%d\n", __func__, addr, len, tries);

	if (WARN(len > 16 || len == 0,
	    "AUX read/write len must be 1-15, len=%d\n", len))
		return -EINVAL;

	type = rw ? DPTX_AUX_CMD_TYPE_READ : DPTX_AUX_CMD_TYPE_WRITE;

	if (!i2c)
		type |= DPTX_AUX_CMD_TYPE_NATIVE;

	if (i2c && mot)
		type |= DPTX_AUX_CMD_TYPE_MOT;

	dptx_aux_clear_data(dptx);

	if (!rw)
		dptx_aux_write_data(dptx, bytes, len);

	auxcmd = ((type << DPTX_AUX_CMD_TYPE_SHIFT) |
		  (addr << DPTX_AUX_CMD_ADDR_SHIFT) |
		  ((len - 1) << DPTX_AUX_CMD_REQ_LEN_SHIFT));

	if (addr_only)
		auxcmd |= DPTX_AUX_CMD_I2C_ADDR_ONLY;

	dptx_dbg_aux(dptx, "%s - AUX_%s_%s [%s %s]: 0x%04X\n", __func__,
		     i2c ? "I":"N", rw ? "R":"W", mot ? "MOT":"", addr_only ? "AONLY":"", auxcmd);
	dptx_write_reg(dptx, dptx->regs[DPTX], AUX_CMD, auxcmd);

	retval = dptx_handle_aux_reply(dptx, auxcmd, &status, &br);

	if (retval == -ETIMEDOUT) {
		dptx_warn(dptx, "AUX transaction timed out\n");
		dptx->aux.noreply_timeout = ktime_add_us(ktime_get(), 3200);
		return retval;
	}

	if (retval == -ESHUTDOWN) {
		dptx_dbg(dptx, "AUX aborted on driver shutdown\n");
		return retval;
	}

	if (atomic_read(&dptx->aux.abort)) {
		dptx_dbg(dptx, "AUX aborted\n");
		return -ETIMEDOUT;
	}

	switch (status) {
	case DPTX_AUX_STS_STATUS_ACK:
		if (br == 0) {
			dptx_dbg_aux(dptx, "BR=0, Retry\n");
			dptx_soft_reset(dptx, DPTX_SRST_CTRL_AUX);
			mdelay(1);
			goto again;
		}
		dptx_dbg_aux(dptx, "AUX Success\n");
		break;
	case DPTX_AUX_STS_STATUS_NACK:
	case DPTX_AUX_STS_STATUS_I2C_NACK:
		dptx_dbg_aux(dptx, "AUX Nack\n");
		return -EINVAL;
	case DPTX_AUX_STS_STATUS_I2C_DEFER:
	case DPTX_AUX_STS_STATUS_DEFER:
		dptx_dbg_aux(dptx, "AUX Defer\n");
		mdelay(1);
		goto again;
	default:
		dptx_err(dptx, "AUX Status Invalid\n");
		dptx_soft_reset(dptx, DPTX_SRST_CTRL_AUX);
		mdelay(1);
		goto again;
	}

	if (rw)
		dptx_aux_read_data(dptx, bytes, len);

	return 0;
}

int dptx_aux_rw_bytes(struct dptx *dptx, bool rw, bool i2c,
		      u32 addr, u8 *bytes, unsigned int len)
{
	int retval;
	unsigned int i;

	for (i = 0; i < len; ) {
		unsigned int curlen;

		curlen = min_t(unsigned int, len - i, 16);

		if (!i2c) {
			retval = dptx_aux_rw(dptx, rw, i2c, true, false,
					     addr + i, &bytes[i], curlen);
		} else {
			retval = dptx_aux_rw(dptx, rw, i2c, true, false,
					     addr, &bytes[i], curlen);
		}
		if (retval)
			return retval;

		i += curlen;
	}

	return 0;
}

int dptx_read_bytes_from_i2c(struct dptx *dptx, u32 device_addr,
			     u8 *bytes, u32 len)
{
	return dptx_aux_rw_bytes(dptx, true, true,
				 device_addr, bytes, len);
}

int dptx_i2c_addr_only_read(struct dptx *dptx, unsigned int device_addr)
{
	u8 bytes[1];

	return dptx_aux_rw(dptx, true, true, false, true,
			     device_addr, &bytes[0], 1);
}

int dptx_write_bytes_to_i2c(struct dptx *dptx,
			    u32 device_addr,
			    u8 *bytes,
			    u32 len)
{
	return dptx_aux_rw_bytes(dptx, false, true,
				 device_addr, bytes, len);
}

int __dptx_read_bytes_from_dpcd(struct dptx *dptx,
				u32 reg_addr,
				u8 *bytes,
				u32 len)
{
	int retval;

	retval = dptx_aux_rw_bytes(dptx, true, false,
				 reg_addr, bytes, len);
	if (retval < 0)
		dptx->stats.dpcd_read_failures++;

	return retval;
}

int __dptx_write_bytes_to_dpcd(struct dptx *dptx,
			       u32 reg_addr,
			       u8 *bytes,
			       u32 len)
{
	return dptx_aux_rw_bytes(dptx, false, false,
				 reg_addr, bytes, len);
}

int __dptx_read_dpcd(struct dptx *dptx, u32 addr, u8 *byte)
{
	return __dptx_read_bytes_from_dpcd(dptx, addr, byte, 1);
}

int __dptx_write_dpcd(struct dptx *dptx, u32 addr, u8 byte)
{
	return __dptx_write_bytes_to_dpcd(dptx, addr, &byte, 1);
}
