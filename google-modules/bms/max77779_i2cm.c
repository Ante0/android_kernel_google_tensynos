// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Google LLC
 */

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <linux/time.h>

#include "max77779_pmic.h"
#include "max77779_i2cm.h"

#define MAX77779_I2CM_POLLING_RETRY_DELAY_US 100
#define MAX77779_I2CM_I2C_WRITE_BUFFER_SIZE (32 - 1)//10
static const unsigned int max77779_i2cm_8bit_times_us[] = {85, 26, 23, 13};

#define I2C_ACK_BIT 1
#define I2C_RW_BIT 1
#define I2C_START_BIT 1
#define I2C_STOP_BIT 1
#define I2C_DEV_ADDR_BITS 7

static inline unsigned int max77779_i2cm_get_delay(unsigned int tx_bytes, unsigned int rx_bytes,
		unsigned int speed)
{
	unsigned int bits;

	bits =  I2C_START_BIT +
		I2C_DEV_ADDR_BITS +
		I2C_RW_BIT +
		I2C_ACK_BIT +
		(tx_bytes * (BITS_PER_BYTE + I2C_ACK_BIT)) +
		I2C_STOP_BIT;
	if (rx_bytes)
		bits += I2C_START_BIT +
			I2C_DEV_ADDR_BITS +
			I2C_RW_BIT +
			I2C_ACK_BIT +
			(rx_bytes * (BITS_PER_BYTE + I2C_ACK_BIT)) +
			I2C_STOP_BIT;

	return (bits * max77779_i2cm_8bit_times_us[speed] + 7) >> 3;
}

static int max77779_i2cm_poll_done(struct max77779_i2cm_info *info,
		unsigned int tx_bytes, unsigned int rx_bytes,
		unsigned int *status)
{
	const unsigned int delay_us = max77779_i2cm_get_delay(tx_bytes, rx_bytes, info->speed);
	const unsigned int timeout_us = (info->completion_timeout_ms * 1000LL);
	unsigned int intr_val;
	int ret;

	udelay(delay_us);
	ret = regmap_read_poll_timeout(info->regmap,  MAX77779_I2CM_INTERRUPT,
				       intr_val, (DONEI_GET(intr_val)),
				       MAX77779_I2CM_POLLING_RETRY_DELAY_US, timeout_us);
	if (ret) {
		dev_err(info->dev, "Failed to read Interrupt (%d).\n", ret);
		return ret;
	}

	if (unlikely(ERRI_GET(intr_val)))
		ret = regmap_read(info->regmap, MAX77779_I2CM_STATUS, status);

	return ret;
}

static int max77779_i2cm_done(struct max77779_i2cm_info *info,
		unsigned int tx_bytes, unsigned int rx_bytes,
		unsigned int *status)
{
	unsigned int timeout = msecs_to_jiffies(info->completion_timeout_ms);

	if (info->irq) {
		if (!wait_for_completion_timeout(&info->xfer_done, timeout)) {
			dev_err(info->dev, "Xfer timed out.\n");
			return -ETIMEDOUT;
		}
		return regmap_read(info->regmap, MAX77779_I2CM_STATUS, status);
	}

	return max77779_i2cm_poll_done(info, tx_bytes, rx_bytes, status);
}

static irqreturn_t max777x9_i2cm_irq(int irq, void *ptr)
{
	struct max77779_i2cm_info *info = ptr;
	unsigned int val;
	int err;

	err = regmap_read(info->regmap, MAX77779_I2CM_INTERRUPT, &val);
	if (err) {
		dev_err(info->dev, "Failed to read Interrupt (%d).\n",
				err);
		return IRQ_NONE;
	}
	if (DONEI_GET(val))
		complete(&info->xfer_done);

	/* clear interrupt */
	regmap_write(info->regmap, MAX77779_I2CM_INTERRUPT, val);
	return IRQ_HANDLED;
}

static inline void set_regval(struct max77779_i2cm_info *info,
		unsigned int reg, unsigned int val)
{
	u8 val8 = (u8)val;

	if (reg > I2CM_MAX_REGISTER) {
		dev_err(info->dev, "reg too large %#04x\n", reg);
		return;
	}
	info->reg_vals[reg] = val8;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
static void max77779_i2cm_reset(struct max77779_i2cm_info *info)
{
        /* disable/reenable ip */
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
			   I2CEN_SET(0) | CLOCK_SPEED_SET(info->speed));
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
			   I2CEN_SET(1) | CLOCK_SPEED_SET(info->speed));
}
#endif

static void max77779_i2cm_manual_start(struct max77779_i2cm_info *info)
{
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
		     MAX77779_I2CM_CONTROL_OEN_MASK |
		     I2CEN_SET(1) |
		     MAX77779_I2CM_CONTROL_SCLO_MASK |
		     MAX77779_I2CM_CONTROL_SDAO_MASK);
	udelay(5);
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
		    MAX77779_I2CM_CONTROL_OEN_MASK |
		    I2CEN_SET(1) |
		    MAX77779_I2CM_CONTROL_SCLO_MASK);
	udelay(5);
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
		    MAX77779_I2CM_CONTROL_OEN_MASK |
		    I2CEN_SET(1));
}

static void max77779_i2cm_manual_stop(struct max77779_i2cm_info *info)
{
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
		     MAX77779_I2CM_CONTROL_OEN_MASK |
		     I2CEN_SET(1) |
		     MAX77779_I2CM_CONTROL_SCLO_MASK);
	udelay(5);
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
		    MAX77779_I2CM_CONTROL_OEN_MASK |
		    I2CEN_SET(1) |
		    MAX77779_I2CM_CONTROL_SCLO_MASK |
		    MAX77779_I2CM_CONTROL_SDAO_MASK);
}

static void max77779_i2cm_clock_out(struct max77779_i2cm_info *info)
{
	const unsigned int val_scl_low = MAX77779_I2CM_CONTROL_OEN_MASK | I2CEN_SET(1);
	const unsigned int val_scl_high = MAX77779_I2CM_CONTROL_OEN_MASK |
			MAX77779_I2CM_CONTROL_SCLO_MASK | I2CEN_SET(1);
	int clk_cnt;

	for (clk_cnt = 0; clk_cnt < 9; clk_cnt++) {
		regmap_write(info->regmap, MAX77779_I2CM_CONTROL, val_scl_low);
		udelay(5);
		regmap_write(info->regmap, MAX77779_I2CM_CONTROL, val_scl_high);
		udelay(5);
	}
}

static inline void max77779_i2cm_dump_i2c_register(struct max77779_i2cm_info *info)
{
	uint32_t timeout, control, sladdr, tx_cnt, rx_cnt, i2cm_cmd;

	regmap_read(info->regmap, MAX77779_I2CM_TIMEOUT, &timeout);
	regmap_read(info->regmap, MAX77779_I2CM_CONTROL, &control);
	regmap_read(info->regmap, MAX77779_I2CM_SLADD, &sladdr);
	regmap_read(info->regmap, MAX77779_I2CM_TXDATA_CNT, &tx_cnt);
	regmap_read(info->regmap, MAX77779_I2CM_RXDATA_CNT, &rx_cnt);
	regmap_read(info->regmap, MAX77779_I2CM_CMD, &i2cm_cmd);

	dev_err(info->dev, "Register dump\n"
		"SDAO    0x%02x   SDA      0x%02x   SCLO    0x%02x   SCL      0x%02x\n"
		"OEN     0x%02x   SPEED    0x%02x   I2CEN   0x%02x   TIMEOUT  0x%02x\n"
		"SLADDR  0x%02x   READ     0x%02x   WRITE   0x%02x   TXCNT    0x%02x\n"
		"RXCNT   0x%02x\n"
		, _max77779_i2cm_control_sdao_get(control)
		, _max77779_i2cm_control_sda_get(control)
		, _max77779_i2cm_control_sclo_get(control)
		, _max77779_i2cm_control_scl_get(control)
		, _max77779_i2cm_control_oen_get(control)
		, _max77779_i2cm_control_clock_speed_get(control)
		, _max77779_i2cm_control_i2cen_get(control)
		, timeout
		, _max77779_i2cm_sladd_slave_id_get(sladdr)
		, _max77779_i2cm_cmd_i2cmread_get(i2cm_cmd)
		, _max77779_i2cm_cmd_i2cmwrite_get(i2cm_cmd)
		, _max77779_i2cm_txdata_cnt_txcnt_get(tx_cnt)
		, _max77779_i2cm_rxdata_cnt_rxcnt_get(rx_cnt)
	);
}

/* requires info->io_lock to be held */
static void max77779_i2cm_recover(struct max77779_i2cm_info *info)
{
	int val, sda_val, scl_val;
	unsigned long timeout;
	int retries;

	max77779_i2cm_dump_i2c_register(info);

	if (regmap_read(info->regmap, MAX77779_I2CM_CONTROL, &val))
		goto out;

	scl_val = _max77779_i2cm_control_scl_get(val);
	sda_val = _max77779_i2cm_control_sda_get(val);
	if (sda_val == 1)
		goto out;

	/* wait 500 ms to allow scl line to go high */
	if (scl_val == 0) {
		timeout = jiffies + msecs_to_jiffies(500);
		while (time_before(jiffies, timeout)) {
			if (regmap_read(info->regmap, MAX77779_I2CM_CONTROL, &val))
				goto out;
			scl_val = _max77779_i2cm_control_scl_get(val);
			usleep_range(10000, 20000);
		}
		if (timeout)
			dev_err(info->dev, "SCL line is still LOW!!!\n");
	}

	sda_val = _max77779_i2cm_control_sda_get(val);
	if (sda_val == 0) {
		/* 2 start commands, 9 clock cycles followed by a stop command (10 times) */
		for (retries = 0; retries < 10; retries++) {
			/* this start doesn't work if the sda line is stuck low */
			max77779_i2cm_manual_start(info);
			udelay(5);
			max77779_i2cm_manual_start(info);
			udelay(5);
			max77779_i2cm_clock_out(info);
			max77779_i2cm_manual_stop(info);
			udelay(5);

			regmap_read(info->regmap, MAX77779_I2CM_CONTROL, &val);
			sda_val = _max77779_i2cm_control_sda_get(val);
			if (sda_val == 1) {
				dev_err(info->dev, "SDA line is recovered.\n");
				break;
			}
		}
		if (retries == 10)
			dev_err(info->dev, "SDA line is not recovered!!!\n");
	}

out:
	/* return to I2CM FSM */
	regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
		     I2CEN_SET(1) | CLOCK_SPEED_SET(info->speed));
}

static int max77779_i2cm_xfer(struct i2c_adapter *adap,
		struct i2c_msg *msgs, int num_msgs)
{
	struct max77779_i2cm_info *info =
			container_of(adap, struct max77779_i2cm_info, adap);
	struct regmap *regmap = info->regmap;
	unsigned int txdata_cnt = 0;
	unsigned int tx_data_buffer = MAX77779_I2CM_TX_BUFFER_0;
	unsigned int rxdata_cnt = 0;
	unsigned int rx_data_buffer = MAX77779_I2CM_RX_BUFFER_0;
	unsigned int cmd = 0;
	int i, j;
	int err = 0;
	unsigned int status; /* result of status register */
	uint8_t status_err;

	mutex_lock(&info->io_lock);

	regmap_write(info->regmap, MAX77779_I2CM_CONTROL, I2CEN_SET(1));

	set_regval(info, MAX77779_I2CM_INTERRUPT, DONEI_SET(1) | ERRI_SET(1));
	set_regval(info, MAX77779_I2CM_INTMASK, ERRIM_SET(0) | DONEIM_SET(0));
	set_regval(info, MAX77779_I2CM_TIMEOUT, info->timeout);
	set_regval(info, MAX77779_I2CM_CONTROL,
			I2CEN_SET(1) | CLOCK_SPEED_SET(info->speed));
	set_regval(info, MAX77779_I2CM_SLADD, SID_SET(msgs[0].addr));

	/* parse message into regval buffer */
	for (i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];

		if (msg->flags & I2C_M_RD) {
			rxdata_cnt += msg->len;
			if (rxdata_cnt  > MAX77779_I2CM_MAX_READ) {
				dev_err(info->dev, "read too large %d > %d\n",
						rxdata_cnt,
						MAX77779_I2CM_MAX_READ);
				return -EINVAL;
			}

			cmd |= I2CMREAD_SET(1);
		} else {
			txdata_cnt += msg->len;
			if (txdata_cnt  > MAX77779_I2CM_MAX_WRITE) {
				dev_err(info->dev, "write too large %d > %d\n",
						txdata_cnt,
						MAX77779_I2CM_MAX_WRITE);
				return -EINVAL;
			}
			cmd |= I2CMWRITE_SET(1);
			for (j = 0; j < msg->len; j++) {
				u8 buf = msg->buf[j];

				set_regval(info, tx_data_buffer, buf);
				tx_data_buffer++;
			}
		}
	}

	set_regval(info, MAX77779_I2CM_TXDATA_CNT, txdata_cnt);

	err = regmap_raw_write(regmap, MAX77779_I2CM_INTERRUPT,
			&info->reg_vals[MAX77779_I2CM_INTERRUPT],
			tx_data_buffer - MAX77779_I2CM_INTERRUPT);
	if (err) {
		dev_err(info->dev, "regmap_raw_write returned %d\n", err);
		goto xfer_done;
	}

	set_regval(info, MAX77779_I2CM_RXDATA_CNT,
			rxdata_cnt > 0 ? rxdata_cnt - 1 : 0);
	set_regval(info, MAX77779_I2CM_CMD, cmd);

	err = regmap_raw_write(regmap, MAX77779_I2CM_RXDATA_CNT,
			&info->reg_vals[MAX77779_I2CM_RXDATA_CNT],
			2);
	if (err) {
		dev_err(info->dev, "regmap_raw_write returned %d\n", err);
		goto xfer_done;
	}

	err = max77779_i2cm_done(info, txdata_cnt, rxdata_cnt, &status);
	if (err)
		goto xfer_done;
	status_err = ERROR_GET(status);                 /* bit */
	if (I2CM_ERR_TIMEOUT(status_err))               /*  1  */
		err = -ETIMEDOUT;
	else if (I2CM_ERR_ADDRESS_NACK(status_err))     /*  2  */
		err = -ENXIO;
	else if (I2CM_ERR_DATA_NACK(status_err))        /*  3  */
		err = -ENXIO;
	else if (I2CM_ERR_RX_FIFO_NA(status_err))       /*  4  */
		err = -ENOBUFS;
	else if (I2CM_ERR_START_OUT_SEQ(status_err))    /*  5  */
		err = -EBADMSG;
	else if (I2CM_ERR_STOP_OUT_SEQ(status_err))     /*  6  */
		err = -EBADMSG;
	else if (I2CM_ERR_ARBITRATION_LOSS(status_err)) /*  0  */
		err = -EAGAIN;

	if (err) {
		dev_err(info->dev, "addr=0x%02x: I2CM status Error (%#04x).\n",
				(msgs && num_msgs > 0 ? msgs[0].addr : 0xff), status_err);
		goto xfer_done;
	}

	if (!rxdata_cnt) /* nothing to read we are done. */
		goto xfer_done;

	err = regmap_raw_read(regmap, MAX77779_I2CM_RX_BUFFER_0,
			&info->reg_vals[MAX77779_I2CM_RX_BUFFER_0], rxdata_cnt);
	if (err) {
		dev_err(info->dev, "Error reading = %d\n", err);
		goto xfer_done;
	}

	rx_data_buffer = MAX77779_I2CM_RX_BUFFER_0;
	for (i = 0; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];

		if (msg->flags & I2C_M_RD) {
			for (j = 0; j < msg->len; j++) {
				msg->buf[j] = info->reg_vals[rx_data_buffer];
				rx_data_buffer++;
			}
		}
	}

xfer_done:
	if (info->irq) {
		int write_err;

		set_regval(info, MAX77779_I2CM_INTERRUPT, DONEI_SET(1) | ERRI_SET(1));
		set_regval(info, MAX77779_I2CM_INTMASK, ERRIM_SET(1) | DONEIM_SET(1));

		write_err = regmap_raw_write(regmap, MAX77779_I2CM_INTERRUPT,
					     &info->reg_vals[MAX77779_I2CM_INTERRUPT], 2);
		if (write_err)
			dev_err(info->dev, "Error clearing intrs and masks = %d\n", write_err);

	}

	if (err == -ETIMEDOUT)
		max77779_i2cm_recover(info);

	if (err)
		dev_err(info->dev, "addr=0x%02x: Xfer Error (%d)\n",
				(msgs && num_msgs > 0 ? msgs[0].addr : 0xff), err);

	regmap_write(info->regmap, MAX77779_I2CM_CONTROL, I2CEN_SET(0));

	mutex_unlock(&info->io_lock);

	return err ? err : num_msgs;
}

static u32 max77779_i2cm_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_SMBUS_BYTE |
	       I2C_FUNC_SMBUS_BYTE_DATA |
	       I2C_FUNC_SMBUS_WORD_DATA |
	       I2C_FUNC_SMBUS_BLOCK_DATA|
	       I2C_FUNC_SMBUS_I2C_BLOCK |
	       I2C_FUNC_I2C;
}

static const struct i2c_algorithm max77779_i2cm_algorithm = {
	.master_xfer		= max77779_i2cm_xfer,
	.functionality		= max77779_i2cm_func,
};

static const struct i2c_adapter_quirks max77779_i2cm_quirks = {
	.flags = I2C_AQ_COMB_WRITE_THEN_READ |
		 I2C_AQ_NO_ZERO_LEN |
		 I2C_AQ_NO_REP_START,
	.max_num_msgs = 2,
	.max_write_len = MAX77779_I2CM_MAX_WRITE,
	.max_read_len = MAX77779_I2CM_MAX_READ,
	.max_comb_1st_msg_len = MAX77779_I2CM_MAX_WRITE,
	.max_comb_2nd_msg_len = MAX77779_I2CM_MAX_READ
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static int debug_max77779_i2cm_recovery_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;

	mutex_lock(&info->io_lock);

	max77779_i2cm_recover(info);
	max77779_i2cm_reset(info);

	mutex_unlock(&info->io_lock);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_recovery_fops, NULL,
			debug_max77779_i2cm_recovery_set, "%llu\n");

static int debug_max77779_i2cm_reg_get(void *data, u64 *val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;

	return regmap_read(info->regmap, info->debug_reg_address, (int*)val);
}

static int debug_max77779_i2cm_reg_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;

	return regmap_write(info->regmap, info->debug_reg_address, val);
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_reg_fops, debug_max77779_i2cm_reg_get,
			debug_max77779_i2cm_reg_set, "0x%llx\n");

static int debug_max77779_i2cm_i2c_reg_get(void *data, u64 *val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;
	struct i2c_msg xfer[2];
	int ret;

	xfer[0].addr = info->debug_i2c_address;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &info->debug_i2c_reg;

	xfer[1].addr = info->debug_i2c_address;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 1;
	xfer[1].buf = (u8*)val;

	ret = i2c_transfer(&info->adap, xfer, 2);

	return ret == 2 ? 0 : ret;
}

static int debug_max77779_i2cm_i2c_reg_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;
	struct i2c_msg xfer;
	u8 buf[2];
	int ret;

	buf[0] = info->debug_i2c_reg;
	buf[1] = (u8)val;

	xfer.addr = info->debug_i2c_address;
	xfer.flags = 0;
	xfer.len = 2;
	xfer.buf = buf;

	ret = i2c_transfer(&info->adap, &xfer, 1);
	return ret == 1 ? 0 : ret;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_i2c_reg_fops, debug_max77779_i2cm_i2c_reg_get,
			debug_max77779_i2cm_i2c_reg_set, "0x%llx\n");

static int debug_max77779_i2cm_manual_control_get(void *data, u64 *val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;
	int reg, ret;

	ret = regmap_read(info->regmap, MAX77779_I2CM_CONTROL, &reg);
	if (ret < 0)
		return ret;

	*val = _max77779_i2cm_control_oen_get(reg);

	return 0;
}

static int debug_max77779_i2cm_manual_control_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;

	return regmap_write(info->regmap, MAX77779_I2CM_CONTROL,
			    I2CEN_SET(1) |
			    _max77779_i2cm_control_sclo_set(0, 1) |
			    _max77779_i2cm_control_oen_set(0, !!val));
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_manual_control_fops,
			debug_max77779_i2cm_manual_control_get,
			debug_max77779_i2cm_manual_control_set, "%llu\n");

static int debug_max77779_i2cm_manual_start_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;

	if (val)
		max77779_i2cm_manual_start(info);
	else
		max77779_i2cm_manual_stop(info);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_manual_start_fops,
			NULL,
			debug_max77779_i2cm_manual_start_set, "%llu\n");

static int debug_max77779_i2cm_manual_clock_get(void *data, u64 *val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;
	int reg, ret;

	ret = regmap_read(info->regmap, MAX77779_I2CM_CONTROL, &reg);
	if (ret < 0)
		return ret;

	*val = _max77779_i2cm_control_scl_get(reg);

	return 0;
}

static int debug_max77779_i2cm_manual_clock_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;

	return regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
				  MAX77779_I2CM_CONTROL_SCLO_MASK,
				  _max77779_i2cm_control_sclo_set(0, !!val));
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_manual_clock_fops,
			debug_max77779_i2cm_manual_clock_get,
			debug_max77779_i2cm_manual_clock_set, "%llu\n");

static int debug_max77779_i2cm_manual_data_get(void *data, u64 *val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;
	int reg, ret;

	ret = regmap_read(info->regmap, MAX77779_I2CM_CONTROL, &reg);
	if (ret < 0)
		return ret;

	*val = _max77779_i2cm_control_sda_get(reg);

	return 0;
}

static int debug_max77779_i2cm_manual_data_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;

	return regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
				  MAX77779_I2CM_CONTROL_SDAO_MASK,
				  _max77779_i2cm_control_sdao_set(0, !!val));
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_manual_data_fops,
			debug_max77779_i2cm_manual_data_get,
			debug_max77779_i2cm_manual_data_set, "%llu\n");

static int debug_max77779_i2cm_manual_ack_set(void *data, u64 val)
{
	struct max77779_i2cm_info *info = (struct max77779_i2cm_info *)data;
	int i, ret;

	/* Write 7-bit address */
	for (i = 6; i >= 0; i--) {
		ret = regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
					 MAX77779_I2CM_CONTROL_SCLO_MASK,
					 _max77779_i2cm_control_sclo_set(0, 0));
		if (ret < 0)
			return ret;
		udelay(5);
		ret = regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
					 MAX77779_I2CM_CONTROL_SCLO_MASK |
					 MAX77779_I2CM_CONTROL_SDAO_MASK,
					 _max77779_i2cm_control_sclo_set(0, 1) |
					 _max77779_i2cm_control_sdao_set(0, (val >> i) & 1));
		if (ret < 0)
			return ret;
		udelay(5);
	}

	/* set write bit */
	ret = regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
					MAX77779_I2CM_CONTROL_SCLO_MASK,
					_max77779_i2cm_control_sclo_set(0, 0));
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
					MAX77779_I2CM_CONTROL_SCLO_MASK |
					MAX77779_I2CM_CONTROL_SDAO_MASK,
					_max77779_i2cm_control_sclo_set(0, 1) |
					_max77779_i2cm_control_sdao_set(0, 0));
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
					MAX77779_I2CM_CONTROL_SCLO_MASK,
					_max77779_i2cm_control_sclo_set(0, 0));
	if (ret < 0)
		return ret;

	/* read ack */
	return regmap_update_bits(info->regmap, MAX77779_I2CM_CONTROL,
				 MAX77779_I2CM_CONTROL_SCLO_MASK,
				 _max77779_i2cm_control_sclo_set(0, 1));
}

DEFINE_SIMPLE_ATTRIBUTE(debug_max77779_i2cm_manual_ack_fops,
			NULL,
			debug_max77779_i2cm_manual_ack_set, "%llu\n");

static int debugfs_init(struct max77779_i2cm_info *info)
{
	const char *dir_name = info->adap.name; /* TODO: add device id */
	const char *manual_controls_dir_name = "manual_control";
	struct dentry *manual_controls_dir;

	info->debugfs_root = debugfs_create_dir(dir_name, NULL);
	if (IS_ERR(info->debugfs_root)) {
		dev_err(info->dev, "Failed to create debugfs directory %s\n", dir_name);
		return -ENOMEM;
	}
	debugfs_create_file("i2cm_recovery", 0200, info->debugfs_root, info,
			    &debug_max77779_i2cm_recovery_fops);
	debugfs_create_file("i2c_data", 0600, info->debugfs_root, info,
			    &debug_max77779_i2cm_i2c_reg_fops);
	debugfs_create_u32("i2c_addr", 0600, info->debugfs_root,
			   &info->debug_i2c_address);
	debugfs_create_u8("i2c_reg", 0600, info->debugfs_root,
			  &info->debug_i2c_reg);
	debugfs_create_u32("reg", 0600, info->debugfs_root,
			   &info->debug_reg_address);
	debugfs_create_file("data", 0600, info->debugfs_root, info,
			    &debug_max77779_i2cm_reg_fops);

	manual_controls_dir = debugfs_create_dir(manual_controls_dir_name, info->debugfs_root);
	if (IS_ERR(manual_controls_dir)) {
		dev_err(info->dev, "Failed to create debugfs directory %s\n",
			manual_controls_dir_name);
		return -ENOMEM;
	}
	debugfs_create_file("control", 0600, manual_controls_dir, info,
			    &debug_max77779_i2cm_manual_control_fops);
	debugfs_create_file("start", 0600, manual_controls_dir, info,
			    &debug_max77779_i2cm_manual_start_fops);
	debugfs_create_file("clock", 0600, manual_controls_dir, info,
			    &debug_max77779_i2cm_manual_clock_fops);
	debugfs_create_file("data", 0600, manual_controls_dir, info,
			    &debug_max77779_i2cm_manual_data_fops);
	debugfs_create_file("ack", 0200, manual_controls_dir, info,
			    &debug_max77779_i2cm_manual_ack_fops);
	return 0;
}

#else
static inline int debugfs_init(struct max77779_i2cm_info *info)
{
	return 0;
}
#endif

int max77779_i2cm_init(struct max77779_i2cm_info *info)
{
	struct device *dev = info->dev;
	int err = 0;
	int i2c_id = -1;

	if (!IS_ENABLED(CONFIG_OF))
		return -EINVAL;

	err = of_property_read_u32(dev->of_node, "max77779,timeout",
			&info->timeout);
	if (err || (info->timeout > MAX77779_MAX_TIMEOUT)) {
		dev_warn(dev, "Invalid max77779,timeout set to max.\n");
		info->timeout = MAX77779_TIMEOUT_DEFAULT;
	}

	err = of_property_read_u32(dev->of_node, "max77779,speed",
			&info->speed);
	if (err || (info->speed > MAX77779_MAX_SPEED)) {
		dev_warn(dev, "Invalid max77779,speed - set to min.\n");
		info->speed = MAX77779_SPEED_DEFAULT;
	}

	err = of_property_read_u32(dev->of_node,
			"max77779,completion_timeout_ms",
			&info->completion_timeout_ms);
	if (err)
		info->completion_timeout_ms =
			MAX77779_COMPLETION_TIMEOUT_MS_DEFAULT;

	of_property_read_u32(dev->of_node, "i2c-id", &i2c_id);

	mutex_init(&info->io_lock);
	init_completion(&info->xfer_done);

	/*
	 * Write I2CM_MASK to disable interrupts. They will be enabled during xfer if
	 * using interrupts.
	 */
	err = regmap_write(info->regmap, MAX77779_I2CM_INTMASK, ERRIM_SET(1) | DONEIM_SET(1));
	if (err) {
		dev_err(dev, "Failed to setup interrupts.\n");
		return -EIO;
	}

	if (info->irq) {
		err = devm_request_threaded_irq(info->dev, info->irq, NULL,
				max777x9_i2cm_irq,
				IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_ONESHOT,
				"max777x9_i2cm", info);
		if (err < 0) {
			dev_err(dev, "Failed to get irq thread.\n");
			return err;
		}
	}

	/* setup the adapter */
	strscpy(info->adap.name, "max77779-i2cm", sizeof(info->adap.name));
	info->adap.owner   = THIS_MODULE;
	info->adap.algo    = &max77779_i2cm_algorithm;
	info->adap.retries = 2;
	info->adap.class   = I2C_CLASS_HWMON;
	info->adap.dev.of_node = dev->of_node;
	info->adap.algo_data = info;
	info->adap.dev.parent = info->dev;
	info->adap.nr = i2c_id;
	info->adap.quirks = &max77779_i2cm_quirks;

	err = i2c_add_numbered_adapter(&info->adap);
	if (err < 0)
		dev_err(dev, "failed to add bus to i2c core\n");

	if (debugfs_init(info))
		dev_err(dev, "failed to add debugfs\n");

	return err;
}
EXPORT_SYMBOL_GPL(max77779_i2cm_init);

void max77779_i2cm_remove(struct max77779_i2cm_info *info)
{
	mutex_destroy(&info->io_lock);
}
EXPORT_SYMBOL_GPL(max77779_i2cm_remove);

MODULE_DESCRIPTION("Maxim 77779 I2C Bridge Driver");
MODULE_AUTHOR("Jim Wylder <jwylder@google.com>");
MODULE_LICENSE("GPL");
