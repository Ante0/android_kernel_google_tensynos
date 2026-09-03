/* drivers/input/touchscreen/sec_ts.c
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * http://www.samsungsemi.com/
 *
 * Core file for Samsung TSC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "sec_ts.h"

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
#include <goog_touch_interface.h>
#endif
#include <goog_touch_trace.h>

#ifndef CONFIG_SEC_SYSFS
/* Declare extern sec_class */
struct class *sec_class;
#endif

static void sec_ts_reset_handler_work(struct work_struct *work);
#ifdef USE_POWER_RESET_WORK
static void sec_ts_reset_work(struct work_struct *work);
#endif
static void sec_ts_fw_update_work(struct work_struct *work);

#ifdef USE_OPEN_CLOSE
static int sec_ts_input_open(struct input_dev *dev);
static void sec_ts_input_close(struct input_dev *dev);
#endif

int sec_ts_read_information(struct sec_ts_data *ts);

#if defined(I3C_INTERFACE)
static u8 sec_ts_i3c_mif_get_parity(u8 n)
{
    u8 parity = 0;
    while (n > 0) {
        parity = ~parity;
        n = n & (n - 1);
	}
    return parity;
}

static u8 sec_ts_i3c_mif_gen_header(bool MODE_SEL, bool FIFO_MODE, bool BYTE_SEL, bool RW_SEL, u8 BASE_ADDR_SEL)
{
    u8 header = 0x00;
    if (MODE_SEL == true) 	header |= 0x80;
    if (FIFO_MODE == true) 	header |= 0x40;
    if (BYTE_SEL == true) 	header |= 0x20;
    if (RW_SEL == true)		header |= 0x10;
    header += BASE_ADDR_SEL<<1;
    if (sec_ts_i3c_mif_get_parity(header))  header |= 0x01;
    return header;
}

static int sec_ts_i3c_mif_sdr_read_segment(struct i3c_device *i3cdev, u8 *rd_data, int rd_size, bool mif_fifo_mode, bool mif_byte_sel, u8 mif_base_addr_sel, u16 mif_offset)
{
	int ret = 0;
	struct i3c_priv_xfer xfers[2];
	u8 buf[3];

	buf[0] = sec_ts_i3c_mif_gen_header(true, mif_fifo_mode, mif_byte_sel, true, mif_base_addr_sel);
	buf[1] = (mif_offset >> 8) & 0xFF;
	buf[2] = mif_offset & 0xFF;

	xfers[0].rnw = I3C_PRIV_XFER_WRITE;
	xfers[0].len = SEC_TS_I3C_HEADER_SIZE;
	xfers[0].data.out = buf;

	xfers[1].rnw = I3C_PRIV_XFER_READ;
	xfers[1].len = rd_size;
	xfers[1].data.in = rd_data;

	ret = i3c_device_do_priv_xfers(i3cdev, xfers, ARRAY_SIZE(xfers));

	return ret;
}

static int sec_ts_i3c_mif_sdr_write_segment(struct i3c_device *i3cdev, u8 *wt_data, int wt_size, bool mif_fifo_mode, bool mif_byte_sel, u8 mif_base_addr_sel, u16 mif_offset)
{
	int ret = 0;
	struct i3c_priv_xfer xfer;

	wt_data[0] = sec_ts_i3c_mif_gen_header(true, mif_fifo_mode, mif_byte_sel, false, mif_base_addr_sel);
	wt_data[1] = (mif_offset >> 8) & 0xFF;
	wt_data[2] = mif_offset & 0xFF;

	xfer.rnw = I3C_PRIV_XFER_WRITE;
	xfer.len = wt_size;
	xfer.data.out = wt_data;

	ret = i3c_device_do_priv_xfers(i3cdev, &xfer, 1);

	return ret;
}
#endif

#if !defined(I3C_INTERFACE) && !defined(I2C_INTERFACE)
int sec_ts_spi_checksum_enable(struct sec_ts_data *ts, u8 enb)
{
	struct sec_ts_plat_data *pdata = ts->plat_data;
	int ret = 0;

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SPI_CHECKSUM_ENABLE, &enb, 1);
	if (ret < 0) {
		LOGE("Failed to %s checksum", enb ? "enable" : "disable");
		return ret;
	}

	pdata->spi_checksum_enable = enb;

	return ret;
}

static u8 sec_ts_get_checksum8(u8 *buf, u32 byte_size)
{
    u32 temp = 0;
    register u8 *pBase = (u8 *)buf;

    while (0 < byte_size) {
        temp += (u32)((*pBase) & 0xFF);
        pBase++;
        byte_size--;
    }

    return temp & 0xFF;
}

static int sec_ts_spi_udelay(u8 reg, u32 data_len)
{
	switch (reg) {
	case SEC_TS_CMD_SW_RESET:
		return 0;
	case SEC_TS_CMD_ENTER_BOOT_MODE:
		return 0;
	case SEC_TS_CMD_READ_ALL_EVENT:
		return 300;
	case SEC_TS_CMD_RAWDATA_TYPE:
		return 0;
	case SEC_TS_CMD_GET_REPORT:
		return 300;
	case SEC_TS_CMD_GET_HEATMAP:
		return 300;
	case SEC_TS_CMD_P2P_TEST:
		return 0;
	case SEC_TS_CMD_SELF_TEST:
		return 1000;
	case SEC_TS_CMD_READ_RTDP_DATA:
		return 500;
	case SEC_TS_CMD_MEMORY_SET_MEM_ADDR:
		return 0;
	case SEC_TS_CMD_MEMORY_SET_DATA_NUM:
		return 0;
	case SEC_TS_CMD_FLASH_SET_MEM_ADDR:
		return 0;
	case SEC_TS_CMD_FLASH_SET_DATA_NUM:
		return 0;
	default:
		return 40;
	}
}

static int sec_ts_spi_post_udelay(u8 reg)
{
	switch (reg) {
	case SEC_TS_CMD_SW_RESET:
		return 20 * 100;
	case SEC_TS_CMD_ENTER_BOOT_MODE:
		return 250;
	case SEC_TS_CMD_SYSTEM_MODE:
		return 35;
	case SEC_TS_CMD_POWER_MODE:
		return 35;
	case SEC_TS_CMD_TOUCH_FUNCTION:
		return 35;
	case SEC_TS_CMD_READ_ALL_EVENT:
		return 0;
	case SEC_TS_CMD_CLEAR_EVENT_STACK:
		return 35;
	case SEC_TS_CMD_RAWDATA_TYPE:
		return 35;
	case SEC_TS_CMD_RAWDATA_SIZE:
		return 35;
	case SEC_TS_CMD_MEMORY_SET_MEM_ADDR:
		return 30;
	case SEC_TS_CMD_MEMORY_SET_DATA_NUM:
		return 30;
	case SEC_TS_CMD_MEMORY_READ_WRITE_DATA:
		return 35;
	case SEC_TS_CMD_FLASH_SET_MEM_ADDR:
		return 30;
	case SEC_TS_CMD_FLASH_SET_DATA_NUM:
		return 30;
	case SEC_TS_CMD_FLASH_READ_WRITE_DATA:
		return 35;
	case SEC_TS_CMD_SELF_TEST:
		return 500;
	default:
		return 20;
	}
}
#endif

static int sec_ts_write(struct sec_ts_data *ts, u8 reg, u8 *data, int len)
{
	u8 *buf;
	int ret = 0;
	unsigned char retry;
#if defined(I3C_INTERFACE)
	int rd_size;
	int wt_size;
	unsigned int i;
#elif defined(I2C_INTERFACE)
	struct i2c_msg msg;
#else
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };
	unsigned int spi_len = 0;
#endif

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER_STATUS: OFF\n");
		return -EIO;
	}

#if defined(I3C_INTERFACE)
	if (SEC_TS_I3C_HEADER_SIZE + 1 + len > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len is larger than buffer size\n");
		return -EINVAL;
	}
#elif defined(I2C_INTERFACE)
	if (len + 1 > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len is larger than buffer size\n");
		return -EINVAL;
	}
#else
	/* add 3 zero stuffing tx bytes at last */
	if (SEC_TS_SPI_HEADER_SIZE + len + 3 > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len is larger than buffer size\n");
		return -EINVAL;
	}
#endif

	mutex_lock(&ts->io_mutex);
	buf = ts->io_write_buf;

#if defined(I3C_INTERFACE)
	/* i3c */
	for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
		// check previous command status
		for (i = 0; i < SEC_TS_BUF_CHECK_RETRY_CNT; i++) {
			ret = sec_ts_i3c_mif_sdr_read_segment(ts->client, ts->io_read_buf, 4, false, false, SEC_TS_I3C_MIF_RX_IDX, 0);

			if (ret != 0) {
				ret = -EIO;

				LOGE("write - check empty retry %d\n", retry + 1);
				ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}

				break;
			}

			rd_size = (ts->io_read_buf[3] << 24) | (ts->io_read_buf[2] << 16) | (ts->io_read_buf[1] << 8) | (ts->io_read_buf[0] << 0);
			if (rd_size == 0)
				break;

			/* 10us delay */
			usleep_range(10, 10 + 1);
		}

		if (ret != 0 || rd_size != 0 || i == SEC_TS_BUF_CHECK_RETRY_CNT) {
			LOGE("write - check empty retry %d\n", retry + 1);
			continue;
		}

		// write data
		buf[SEC_TS_I3C_HEADER_SIZE] = reg & 0xFF;
		memcpy(buf + SEC_TS_I3C_HEADER_SIZE + 1, data, len);
		wt_size = SEC_TS_I3C_HEADER_SIZE + 1 + len;

#ifdef SEC_TS_DEBUG_IO
		LOGI("i3c write");
		sec_ts_print_data(ts, wt_size, buf);
#endif

		ret = sec_ts_i3c_mif_sdr_write_segment(ts->client, buf, wt_size, false, false, SEC_TS_I3C_MIF_RX_BUF, 0);

		if (ret != 0) {
			ret = -EIO;

			LOGE("write - write data retry %d\n", retry + 1);
			ts->comm_err_count++;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			continue;
		}

		// write data size
		buf[SEC_TS_I3C_HEADER_SIZE + 0] = ((len + 1) >> 0) & 0xFF;
		buf[SEC_TS_I3C_HEADER_SIZE + 1] = ((len + 1) >> 8) & 0xFF;
		wt_size = SEC_TS_I3C_HEADER_SIZE + 2;

		ret = sec_ts_i3c_mif_sdr_write_segment(ts->client, buf, wt_size, false, false, SEC_TS_I3C_MIF_RX_IDX, 0);

		if (ret != 0) {
			ret = -EIO;

			LOGE("write - write data size retry %d\n", retry + 1);
			ts->comm_err_count++;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			continue;
		} else {
			break;
		}
	}

#else

#if defined(I2C_INTERFACE)
	buf[0] = reg;
	memcpy(buf + 1, data, len);

	msg.addr = ts->client->addr;
	msg.flags = 0;
	msg.len = len + 1;
	msg.buf = buf;
#else
	buf[0] = reg;
	buf[1] = (len >> 8) & 0xFF; // write size
	buf[2] = (len) & 0xFF;
	buf[3] = 0x00; // read size 0
	buf[4] = 0x00;
	buf[5] = 0x00; // padding
	buf[6] = 0x00;
	buf[7] = 0x00;
	spi_len = SEC_TS_SPI_HEADER_SIZE;

	if(data)
		memcpy(buf + spi_len, data, len);
	spi_len += len;

	if(ts->plat_data->spi_checksum_enable) {
		u8 checksum = sec_ts_get_checksum8(buf, spi_len);
		buf[spi_len] = checksum;
		spi_len++;
	}
	/* add 3 zero stuffing tx bytes at last */
	memset(buf + spi_len, 0x00, 3);
	/* spi transfer size should be multiple of 4 */
	spi_len = HOST_DMA_ALIGN(spi_len);

	spi_message_init(&msg);
	transfer[0].len = spi_len;
	transfer[0].tx_buf = buf;
	transfer[0].rx_buf = NULL;
	spi_message_add_tail(&transfer[0], &msg);

#ifdef SEC_TS_DEBUG_IO
	LOGI("spi write");
	sec_ts_print_data(ts, spi_len, buf);
#endif

#endif

	for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
#if defined(I2C_INTERFACE)
		if ((ret = i2c_transfer(ts->client->adapter, &msg, 1)) == 1)
			break;
#else
		if ((ret = gti_spi_sync(ts->client, &msg)) == 0)
			break;
#endif

		if (ts->power_status == SEC_TS_STATE_POWER_OFF)
			LOGE("POWER_STATUS: OFF, retry: %d\n",retry);

		usleep_range(1 * 1000, 1 * 1000);

		if (retry > 1) {
			LOGE("retry %d\n", retry + 1);
			ts->comm_err_count++;
		}
	}
#endif

	if (retry == SEC_TS_IO_RETRY_CNT) {
		LOGE("write over retry limit\n");
		ret = -EIO;
#ifdef USE_POR_AFTER_I2C_RETRY
		if (ts->probe_done && !ts->reset_is_on_going)
			schedule_delayed_work(&ts->reset_work,
				msecs_to_jiffies(TOUCH_RESET_DWORK_TIME));
#endif
	}

#if defined(I2C_INTERFACE)
	if (ret == 1)
		ret = 0;
#endif
#if defined(I3C_INTERFACE)
err:
#endif
	mutex_unlock(&ts->io_mutex);
	return ret;
}

static int sec_ts_read(struct sec_ts_data *ts, u8 reg, u8 *data, int len)
{
	u8 *buf;
	int ret = 0;
	unsigned char retry = 0;
#if defined(I3C_INTERFACE)
	u16 mif_offset;
	int wt_size;
	int rd_size;
	unsigned int buf_chk_retry;
	int copy_size = 0, copy_cur = 0;
#elif defined(I2C_INTERFACE)
	struct i2c_msg msg[2];
#else
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };
	u32 spi_delay_us = 0;
	unsigned int spi_write_len = 0, spi_read_len = 0;
	int copy_size = 0, copy_cur = 0;
#endif
	int remain = len;

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER_STATUS: OFF\n");
		return -EIO;
	}

#if defined(I3C_INTERFACE)
	if (SEC_TS_I3C_HEADER_SIZE + 1 > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len is larger than buffer size\n");
		return -EINVAL;
	}
#elif defined(I2C_INTERFACE)
	/* i2c */
#else
	/* add 3 zero stuffing tx bytes at last */
	if (SEC_TS_SPI_HEADER_SIZE > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len is larger than buffer size\n");
		return -EINVAL;
	}
#endif

	if (len > IO_PREALLOC_READ_BUF_SZ) {
		LOGE("len %d over pre-allocated size %d\n", len, IO_PREALLOC_READ_BUF_SZ);
		return -ENOSPC;
	}

	mutex_lock(&ts->io_mutex);
	buf = ts->io_write_buf;

#if defined(I3C_INTERFACE)
	for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
		/* check previous command status */
		for (buf_chk_retry = 0; buf_chk_retry < SEC_TS_BUF_CHECK_RETRY_CNT; buf_chk_retry++) {
			ret = sec_ts_i3c_mif_sdr_read_segment(ts->client, ts->io_read_buf, 4,
					false, false, SEC_TS_I3C_MIF_RX_IDX, 0);
			if (ret != 0) {
				ret = -EIO;

				LOGE("check buffer empty error retry %d\n", retry + 1);
				ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}

				break;
			}

			rd_size = (ts->io_read_buf[3] << 24) | (ts->io_read_buf[2] << 16) | (ts->io_read_buf[1] << 8) | (ts->io_read_buf[0] << 0);
			if (rd_size == 0)
				break;

			/* 10us delay */
			usleep_range(10, 10 + 1);
		}

		if (ret != 0 || rd_size != 0 || buf_chk_retry == SEC_TS_BUF_CHECK_RETRY_CNT) {
			LOGE("check buffer empty error retry %d, ret %d, rd_size %d, buf_chk_retry %d\n",
				retry + 1, ret, rd_size, buf_chk_retry);

			if (buf_chk_retry == SEC_TS_BUF_CHECK_RETRY_CNT) {
				LOGE("buf_chk_retry over retry limit, goto skip_read\n");
				goto skip_read;
			}

			continue;
		}

		/* write register address */
		buf[SEC_TS_I3C_HEADER_SIZE] = reg & 0xFF;
		wt_size = SEC_TS_I3C_HEADER_SIZE + 1;

		ret = sec_ts_i3c_mif_sdr_write_segment(ts->client, buf, wt_size, false, false, SEC_TS_I3C_MIF_RX_BUF, 0);
#ifdef SEC_TS_DEBUG_IO
		LOGI("i3c write addr");
		sec_ts_print_data(ts, wt_size, buf);
#endif

		if (ret != 0) {
			ret = -EIO;

			LOGE("read write addr retry %d\n", retry + 1);
			ts->comm_err_count++;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			continue;
		}

		/* write register size (fixed to 1) */
		buf[SEC_TS_I3C_HEADER_SIZE + 0] = 0x01;
		wt_size = SEC_TS_I3C_HEADER_SIZE + 1;

		ret = sec_ts_i3c_mif_sdr_write_segment(ts->client, buf, wt_size, false, false, SEC_TS_I3C_MIF_RX_IDX, 0);
#ifdef SEC_TS_DEBUG_IO
		LOGI("i3c write size");
		sec_ts_print_data(ts, wt_size, buf);
#endif

		if (ret != 0) {
			ret = -EIO;

			LOGE("read - write data size retry %d\n", retry + 1);
			ts->comm_err_count++;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			continue;
		}

		/* 10us delay */
		usleep_range(10, 10 + 1);

		/* read rd_size */
		mif_offset = 0;

		for (buf_chk_retry = 0; buf_chk_retry < SEC_TS_BUF_CHECK_RETRY_CNT; buf_chk_retry++) {
			ret = sec_ts_i3c_mif_sdr_read_segment(ts->client, ts->io_read_buf, 4, false, false, SEC_TS_I3C_MIF_TX_IDX, mif_offset);

			if (ret != 0) {
				ret = -EIO;

				LOGE("read rd_size error retry %d\n", retry + 1);
				ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}

				break;
			}

			rd_size = (ts->io_read_buf[3] << 24) | (ts->io_read_buf[2] << 16) | (ts->io_read_buf[1] << 8) | (ts->io_read_buf[0] << 0);
			if (rd_size > 0)
				break;

			/* 10us delay */
			usleep_range(10, 10 + 1);
		}

		if (ret != 0 || rd_size <= 0 || buf_chk_retry == SEC_TS_BUF_CHECK_RETRY_CNT) {
			LOGE("read rd_size error retry %d, ret %d, rd_size %d, buf_chk_retry %d\n",
				retry + 1, ret, rd_size, buf_chk_retry);

			if (buf_chk_retry == SEC_TS_BUF_CHECK_RETRY_CNT) {
				LOGE("buf_chk_retry over retry limit, goto skip_read\n");
				goto skip_read;
			}

			continue;
		}

		/* read data */
		copy_size = 0;
		remain = rd_size;
		do {
			if (remain > ts->io_burstmax)
				copy_cur = ts->io_burstmax;
			else
				copy_cur = remain;

			mif_offset = copy_size;

			ret = sec_ts_i3c_mif_sdr_read_segment(ts->client, &ts->io_read_buf[copy_size], copy_cur, false, false, SEC_TS_I3C_MIF_TX_BUF, mif_offset);

			copy_size += copy_cur;
			remain -= copy_cur;

#ifdef SEC_TS_DEBUG_IO
			LOGI("i3c read");
			sec_ts_print_data(ts, copy_cur, &ts->io_read_buf[copy_size]);
#endif

			if (ret != 0) {
				ret = -EIO;

				LOGE("retry %d for 0x%02X size(%d)\n", retry + 1, reg, len);
					ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}
				break;
			}
		} while (remain > 0);

		if (ret != 0) { // read fail, retry
			ret = -EIO;
			continue;
		} else {
			break;
		}
	}

	if (ret == 0 && rd_size > 0) {
		memcpy(data, ts->io_read_buf, rd_size);
		ret = rd_size;
	}

	/* 10us delay */
	usleep_range(10, 10 + 1);

#else

#if defined(I2C_INTERFACE)
	buf[0] = reg;

	msg[0].addr = ts->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = buf;

	msg[1].addr = ts->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = ts->io_read_buf;
#else
	buf[0] = reg;
	buf[1] = 0x00; // write size 0
	buf[2] = 0x00;
	buf[3] = (len >> 8) & 0xFF; // read size
	buf[4] = len & 0xFF;
	buf[5] = 0x00; // padding
	buf[6] = 0x00;
	buf[7] = 0x00;
	spi_write_len = SEC_TS_SPI_HEADER_SIZE;

	if(ts->plat_data->spi_checksum_enable) {
		u8 checksum = sec_ts_get_checksum8(buf, spi_write_len);
		buf[spi_write_len] = checksum;
		spi_write_len++;
	}
	/* add 3 zero stuffing tx bytes at last */
	memset(buf + spi_write_len, 0x00, 3);
	/* spi transfer size should be multiple of 4 */
	spi_write_len = HOST_DMA_ALIGN(spi_write_len);

	spi_read_len = len;
	if(ts->plat_data->spi_checksum_enable)
		spi_read_len++;
	spi_read_len = HOST_DMA_ALIGN(spi_read_len);
#endif

	if (len <= ts->io_burstmax) {
#if defined(I2C_INTERFACE)
		for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
			ret = i2c_transfer(ts->client->adapter, msg, 2);
			if (ret == 2)
				break;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
			}

			if (retry > 1) {
				LOGE("retry %d\n", retry + 1);
				ts->comm_err_count++;
			}
		}
		if (ret == 2)
			memcpy(data, ts->io_read_buf, len);
#else
		for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
			spi_message_init(&msg);
			// spi transfer size should be multiple of 4
			transfer[0].len = spi_write_len;
			transfer[0].tx_buf = buf;
			transfer[0].rx_buf = NULL;
			// CS needs to stay low until read seq. is done
			transfer[0].cs_change =	1;
			spi_message_add_tail(&transfer[0], &msg);

			ret = gti_spi_sync(ts->client, &msg);
#ifdef SEC_TS_DEBUG_IO
			LOGI("spi write");
			sec_ts_print_data(ts, spi_write_len, buf);
#endif
			// write fail
			if (ret != 0) {
				ret = -EIO;

				LOGE("spi write retry %d\n", retry + 1);
				ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}

				if (retry == SEC_TS_IO_RETRY_CNT - 1) {
					LOGE("write reg retry over retry limit, skip read\n");
					goto skip_read;
				}

				continue;
			}

			spi_delay_us = sec_ts_spi_udelay(reg, len);
			usleep_range(spi_delay_us, spi_delay_us + 1);

			// read sequence start
			spi_message_init(&msg);
			transfer[0].len = spi_read_len;
			transfer[0].tx_buf = NULL;
			transfer[0].rx_buf = ts->io_read_buf;
			// CS needs to stay low until read seq. is done
			transfer[0].cs_change =	0;
			spi_message_add_tail(&transfer[0], &msg);
			ret = gti_spi_sync(ts->client, &msg);

#ifdef SEC_TS_DEBUG_IO
			LOGI("spi read");
			sec_ts_print_data(ts, spi_read_len, ts->io_read_buf);
#endif
			// read fail
			if (ret != 0) {
				ret = -EIO;

				LOGE("retry %d for 0x%02X size(%d) delay_us(%d)\n",
					retry + 1, reg, len, spi_delay_us);
					ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}
				continue;
			} else
				break;
		}
		if (ret == 0) {
			if (ts->plat_data->spi_checksum_enable) {
				u8 checksum = sec_ts_get_checksum8(ts->io_read_buf, len);
				if (checksum != ts->io_read_buf[len]) {
					LOGE("checksum error : %02X,%02X\n", checksum,
							ts->io_read_buf[len]);
					ret = -EIO;
					goto err;
				}
			}
			memcpy(data, ts->io_read_buf, len);
		}

		spi_delay_us = sec_ts_spi_post_udelay(reg);
		usleep_range(spi_delay_us, spi_delay_us + 1);

#endif
	} else {
		// len > ts->io_burstmax
		/*
		 * read buffer is 256 byte. do not support long buffer over
		 * than 256. So, try to separate reading data about 256 bytes.
		 **/
#if defined(I2C_INTERFACE)
		for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
			ret = i2c_transfer(ts->client->adapter, msg, 1);
			if (ret == 1)
				break;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			if (retry > 1) {
				LOGE("retry %d\n", retry + 1);
				ts->comm_err_count++;
			}
		}

		do {
			if (remain > ts->io_burstmax)
				msg[1].len = ts->io_burstmax;
			else
				msg[1].len = remain;

			remain -= ts->io_burstmax;

			for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
				ret = i2c_transfer(ts->client->adapter,
						   &msg[1], 1);
				if (ret == 1)
					break;
				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}

				if (retry > 1) {
					LOGE("retry %d\n", retry + 1);
					ts->comm_err_count++;
				}
			}

			msg[1].buf += msg[1].len;

		} while (remain > 0);

		if (ret == 1)
			memcpy(data, ts->io_read_buf, len);
#else
		for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
			spi_message_init(&msg);
			// spi transfer size should be multiple of 4
			transfer[0].len = spi_write_len;
			transfer[0].tx_buf = buf;
			transfer[0].rx_buf = NULL;
			// CS needs to stay low until read seq. is done
			transfer[0].cs_change =	1;
			spi_message_add_tail(&transfer[0], &msg);

			ret = gti_spi_sync(ts->client, &msg);

			// write fail
			if (ret != 0) {
				ret = -EIO;

				LOGE("spi write retry %d\n", retry + 1);
				ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);

				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}

				if (retry == SEC_TS_IO_RETRY_CNT - 1) {
					LOGE("write reg retry over retry limit, skip read\n");
					goto skip_read;
				}

				continue;
			}

			spi_delay_us = sec_ts_spi_udelay(reg, len);
			usleep_range(spi_delay_us, spi_delay_us + 1);

			copy_size = 0;
			remain = spi_read_len;
			do {
				if (remain > ts->io_burstmax)
					copy_cur = ts->io_burstmax;
				else
					copy_cur = remain;

				spi_message_init(&msg);

				transfer[0].len = copy_cur;
				transfer[0].tx_buf = NULL;
				transfer[0].rx_buf =
					&ts->io_read_buf[copy_size];
				// CS needs to stay low until read seq. is done
				transfer[0].cs_change =
					(remain > ts->io_burstmax) ? 1 : 0;

				spi_message_add_tail(&transfer[0], &msg);
				ret = gti_spi_sync(ts->client, &msg);

#ifdef SEC_TS_DEBUG_IO
				LOGI("spi read");
				sec_ts_print_data(ts, copy_cur, &ts->io_read_buf[copy_size]);
#endif

				copy_size += copy_cur;
				remain -= copy_cur;

				if (ret != 0) {
					ret = -EIO;

					LOGE("retry %d\n", retry + 1);
					ts->comm_err_count++;

					usleep_range(1 * 1000, 1 * 1000);
					if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
						LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
						goto err;
					}
					break;
				}
			} while (remain > 0);

			if (ret != 0) { // read fail, retry
				ret = -EIO;
				continue;
			} else {
				break;
			}
		}
		if (ret == 0) {
			if (ts->plat_data->spi_checksum_enable) {
				u8 checksum = sec_ts_get_checksum8(ts->io_read_buf, len);
				if (checksum != ts->io_read_buf[len]) {
					LOGE("checksum error : %02X,%02X\n", checksum,
							ts->io_read_buf[len]);
					ret = -EIO;
					goto err;
				}
			}
			memcpy(data, ts->io_read_buf, len);
		}

		spi_delay_us = sec_ts_spi_post_udelay(reg);
		usleep_range(spi_delay_us, spi_delay_us + 1);
#endif
	}
#endif

#if !defined(I2C_INTERFACE)
skip_read:
#endif

	if (retry == SEC_TS_IO_RETRY_CNT
#if defined(I3C_INTERFACE)
		|| buf_chk_retry == SEC_TS_BUF_CHECK_RETRY_CNT
#endif
	) {
		LOGE("read reg(%#x) over retry limit, comm_err_count %d, io_err_count %d\n",
			reg, ts->comm_err_count, ts->io_err_count);
		ret = -EIO;
		ts->io_err_count++;
#ifdef USE_POR_AFTER_I2C_RETRY
		if (ts->probe_done && !ts->reset_is_on_going)
			schedule_delayed_work(&ts->reset_work,
				msecs_to_jiffies(TOUCH_RESET_DWORK_TIME));
#endif

	} else
		ts->io_err_count = 0;

err:
	mutex_unlock(&ts->io_mutex);

	/*
	 * Do hw reset if continuously failed over SEC_TS_IO_RESET_CNT times
	 * except for FW update process.
	 */
	if (ts->io_err_count >= SEC_TS_IO_RESET_CNT) {
		ts->io_err_count = 0;
		sec_ts_system_reset(ts, RESET_MODE_HW, false, true);
	}

	return ret;
}

static int sec_ts_write_burst(struct sec_ts_data *ts, u8 *data, int len)
{
	int ret = 0;
	int retry = 0;
#if defined(I3C_INTERFACE)
	int rd_size;
	int wt_size;
	unsigned int i;
#elif defined(I2C_INTERFACE)
	/* i2c */
#else
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };
	unsigned int spi_len = 0;
#endif

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("POWER_STATUS: OFF\n");
		return -EIO;
	}

#if defined(I3C_INTERFACE)
	if (SEC_TS_I3C_HEADER_SIZE + len > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len is larger than buffer size\n");
		return -EINVAL;
	}
#elif defined(I2C_INTERFACE)
	if (len > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len %d over pre-allocated size %d\n", len, IO_PREALLOC_WRITE_BUF_SZ);
		return -ENOSPC;
	}
#else
	/* add 3 zero stuffing tx bytes at last */
	if (len + 3 > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("len is larger than buffer size\n");
		return -EINVAL;
	}
#endif

	mutex_lock(&ts->io_mutex);

#if defined(I3C_INTERFACE)
	for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
		/* check previous command status */
		for (i = 0; i < SEC_TS_BUF_CHECK_RETRY_CNT; i++) {
			ret = sec_ts_i3c_mif_sdr_read_segment(ts->client, ts->io_read_buf, 4, false, false, SEC_TS_I3C_MIF_RX_IDX, 0);
			if (ret != 0) {
				ret = -EIO;

				LOGE("write burst - check empty retry %d\n", retry + 1);
				ts->comm_err_count++;

				usleep_range(1 * 1000, 1 * 1000);
				if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
					LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
					goto err;
				}

				break;
			}

			rd_size = (ts->io_read_buf[3] << 24) | (ts->io_read_buf[2] << 16) | (ts->io_read_buf[1] << 8) | (ts->io_read_buf[0] << 0);
			if (rd_size == 0)
				break;

			/* 10us delay */
			usleep_range(10, 10 + 1);
		}

		if (ret != 0 || rd_size != 0 || i == SEC_TS_BUF_CHECK_RETRY_CNT) {
			LOGE("write burst - check empty retry %d\n", retry + 1);
			continue;
		}

		// write data
		memcpy(ts->io_write_buf + SEC_TS_I3C_HEADER_SIZE, data, len);
		wt_size = SEC_TS_I3C_HEADER_SIZE + len;

		ret = sec_ts_i3c_mif_sdr_write_segment(ts->client, ts->io_write_buf, wt_size, false, false, SEC_TS_I3C_MIF_RX_BUF, 0);

		if (ret != 0) {
			ret = -EIO;

			LOGE("write burst - write data retry %d\n", retry + 1);
			ts->comm_err_count++;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			continue;
		}

		/* write data size */
		ts->io_write_buf[SEC_TS_I3C_HEADER_SIZE + 0] = (len >> 0) & 0xFF;
		ts->io_write_buf[SEC_TS_I3C_HEADER_SIZE + 1] = (len >> 8) & 0xFF;
		ts->io_write_buf[SEC_TS_I3C_HEADER_SIZE + 2] = (len >> 16) & 0xFF;
		ts->io_write_buf[SEC_TS_I3C_HEADER_SIZE + 3] = (len >> 24) & 0xFF;
		wt_size = SEC_TS_I3C_HEADER_SIZE + 4;

		ret = sec_ts_i3c_mif_sdr_write_segment(ts->client, ts->io_write_buf, wt_size, false, false, SEC_TS_I3C_MIF_RX_IDX, 0);

		if (ret != 0) {
			ret = -EIO;

			LOGE("write burst - write data size retry %d\n", retry + 1);
			ts->comm_err_count++;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			continue;
		} else {
			break;
		}
	}

#else

#if defined(I2C_INTERFACE)
	memcpy(ts->io_write_buf, data, len);
	data = ts->io_write_buf;
#else
	if(data)
		memcpy(ts->io_write_buf, data, len);
	spi_len = len;

	if(ts->plat_data->spi_checksum_enable) {
		u8 checksum = sec_ts_get_checksum8(ts->io_write_buf, spi_len);
		ts->io_write_buf[spi_len] = checksum;
		spi_len++;
	}
	/* add 3 zero stuffing tx bytes at last */
	memset(ts->io_write_buf + spi_len, 0x00, 3);
	/* spi transfer size should be multiple of 4 */
	spi_len = ((spi_len + 3) & (~3));

	spi_message_init(&msg);
	transfer[0].len = spi_len;
	transfer[0].tx_buf = ts->io_write_buf;
	transfer[0].rx_buf = NULL;
	spi_message_add_tail(&transfer[0], &msg);

#ifdef SEC_TS_DEBUG_IO
	LOGI("spi write");
	sec_ts_print_data(ts, spi_len, ts->io_write_buf);
#endif

#endif

	for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
#if defined(I2C_INTERFACE)
		if ((ret = i2c_master_send(ts->client, data, len)) == len)
			break;
#else
		if ((ret = gti_spi_sync(ts->client, &msg)) == 0)
			break;
#endif

		usleep_range(1 * 1000, 1 * 1000);

		if (retry > 1) {
			LOGE("retry %d\n", retry + 1);
			ts->comm_err_count++;
		}
	}

#endif

	if (retry == SEC_TS_IO_RETRY_CNT) {
		LOGE("write over retry limit\n");
		ret = -EIO;
	}

#if defined(I3C_INTERFACE)
err:
#endif
	mutex_unlock(&ts->io_mutex);
	return ret;
}

static int sec_ts_read_bulk(struct sec_ts_data *ts, u8 *data, int len)
{
	int ret = 0;
	unsigned char retry;
	int remain = len;
#if defined(I3C_INTERFACE)
	struct i3c_priv_xfer xfer;
	int copy_size = 0, copy_cur = 0;
	int retry_msg = 0;
#elif defined(I2C_INTERFACE)
	struct i2c_msg msg;
#else
	struct spi_message msg;
	struct spi_transfer transfer[1] = { { 0 } };
	unsigned int spi_len = 0;
	int copy_size = 0, copy_cur = 0;
	int retry_msg = 0;
#endif

	if (len > IO_PREALLOC_READ_BUF_SZ) {
		LOGE("len %d over pre-allocated size %d\n", len, IO_PREALLOC_READ_BUF_SZ);
		return -ENOSPC;
	}

	mutex_lock(&ts->io_mutex);

#if defined(I3C_INTERFACE)
retry_message:
	remain = (len) & ~3;
	do {
		if (remain > ts->io_burstmax)
			copy_cur = ts->io_burstmax;
		else
			copy_cur = remain;

		xfer.rnw = I3C_PRIV_XFER_READ;
		xfer.len = copy_cur;
		xfer.data.in = &ts->io_read_buf[copy_size];

		copy_size += copy_cur;
		remain -= copy_cur;

		for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
			ret = i3c_device_do_priv_xfers(ts->client, &xfer, 1);
			if (ret == 0)
				break;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			if (retry > 1) {
				LOGE("retry %d\n", retry + 1);
				ts->comm_err_count++;
			}
		}
	} while (remain > 0);

	if (ret == 0)
		memcpy(data, ts->io_read_buf, len);
	else {
		LOGI("i3c fail, ret %d\n", ret);
		if (retry_msg++ < SEC_TS_IO_RETRY_CNT)
			goto retry_message;
	}

#elif defined(I2C_INTERFACE)
	msg.addr = ts->client->addr;
	msg.flags = I2C_M_RD;
	msg.len = len;
	msg.buf = ts->io_read_buf;

	do {
		if (remain > ts->io_burstmax)
			msg.len = ts->io_burstmax;
		else
			msg.len = remain;

		remain -= ts->io_burstmax;

		for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
			ret = i2c_transfer(ts->client->adapter, &msg, 1);
			if (ret == 1)
				break;
			usleep_range(1 * 1000, 1 * 1000);

			if (retry > 1) {
				LOGE("retry %d\n", retry + 1);
				ts->comm_err_count++;
			}
		}

		if (retry == SEC_TS_IO_RETRY_CNT) {
			LOGE("read over retry limit\n");
			ret = -EIO;
			break;
		}

		msg.buf += msg.len;

	} while (remain > 0);

	if (ret == 1)
		memcpy(data, ts->io_read_buf, len);

#else
retry_message:
	spi_len = len;
	if(ts->plat_data->spi_checksum_enable)
		spi_len++;

	remain = spi_len = ((spi_len + 3) & (~3));
	do {
		if (remain > ts->io_burstmax)
			copy_cur = ts->io_burstmax;
		else
			copy_cur = remain;

		spi_message_init(&msg);
		transfer[0].len = copy_cur;
		transfer[0].tx_buf = NULL;
		transfer[0].rx_buf = &ts->io_read_buf[copy_size];
		/* CS needs to stay low until read seq. is done
		 */
		transfer[0].cs_change = (remain > ts->io_burstmax) ? 1 : 0;

		spi_message_add_tail(&transfer[0], &msg);

		copy_size += copy_cur;
		remain -= copy_cur;

		for (retry = 0; retry < SEC_TS_IO_RETRY_CNT; retry++) {
			ret = gti_spi_sync(ts->client, &msg);
			if (ret == 0)
				break;

			usleep_range(1 * 1000, 1 * 1000);
			if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
				LOGE("POWER_STATUS: OFF, retry: %d\n", retry);
				goto err;
			}

			if (retry > 1) {
				LOGE("retry %d\n", retry + 1);
				ts->comm_err_count++;
			}
		}
	} while (remain > 0);

	if (ret == 0) {
		if (ts->plat_data->spi_checksum_enable) {
			u8 checksum = sec_ts_get_checksum8(ts->io_read_buf, len);
			if (checksum != ts->io_read_buf[len]) {
				LOGE("checksum error : %02X,%02X\n", checksum,
						ts->io_read_buf[len]);
				ret = -EIO;
				goto err;
			}
		}
		memcpy(data, ts->io_read_buf, len);
	} else {
		LOGI("spi fail, ret %d\n", ret);
		if (retry_msg++ < SEC_TS_IO_RETRY_CNT)
			goto retry_message;
	}
#endif

#if defined(I2C_INTERFACE)
	if (ret == 1)
		ret = 0;
#endif

#if !defined(I2C_INTERFACE)
err:
#endif
	mutex_unlock(&ts->io_mutex);
	return ret;
}

static void sec_irq_enable(struct sec_ts_data *ts, bool enable)
{
	if (enable) {
		if (!atomic_cmpxchg(&ts->irq_enabled, 0, 1)) {
			enable_irq(ts->plat_data->irq);
			LOGI("Irq enabled");
		} else {
			LOGW("Irq already enabled");
		}
	} else {
		if (atomic_cmpxchg(&ts->irq_enabled, 1, 0)) {
			disable_irq(ts->plat_data->irq);
			LOGI("Irq disabled");
		} else {
			LOGW("Irq already disabled");
		}
	}
}

static void sec_disable_irq_nosync(struct sec_ts_data *ts)
{
	if (atomic_cmpxchg(&ts->irq_enabled, 1, 0)) {
		disable_irq_nosync(ts->plat_data->irq);
		LOGI("Irq disabled");
	} else {
		LOGW("Irq already disabled");
	}
}


#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
#include <linux/sec_debug.h>
extern struct tsp_dump_callbacks dump_callbacks;
static struct delayed_work *p_ghost_check;

static void sec_ts_check_rawdata(struct work_struct *work)
{
	struct sec_ts_data *ts = container_of(work, struct sec_ts_data,
					      ghost_check.work);

	if (ts->tsp_dump_lock == 1) {
		LOGE("ignored ## already checking..\n");
		return;
	}
	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("ignored ## IC is power off\n");
		return;
	}

	ts->tsp_dump_lock = 1;
	LOGI("start ##\n");
	sec_ts_run_rawdata_all((void *)ts, false);
	msleep(100);

	LOGI("done ##\n");
	ts->tsp_dump_lock = 0;

}

static void dump_tsp_log(void)
{
	LOGI("start\n");

	if (p_ghost_check == NULL) {
		LOGE("ignored ## tsp probe fail!!\n");
		return;
	}
	schedule_delayed_work(p_ghost_check, msecs_to_jiffies(100));
}
#endif

void sec_ts_delay(unsigned int ms)
{
	if (ms < 20)
		usleep_range(ms * 1000, ms * 1000);
	else
		msleep(ms);
}

int sec_ts_get_checksum_status(struct sec_ts_data *ts)
{
	int ret = 0;
	int retry = 0;
	const u8 model_id[2] = {0x3D, 0x70};
	u8 device_id[6] = {0};

	while (retry < SEC_TS_IO_RETRY_CNT) {
		LOGI("Get checksum status, retry = %d", retry);
		ts->plat_data->spi_checksum_enable = 1;
		sec_ts_delay(20);
		/* Don't check the returned value because it will be -EIO when FW checksum is off. */
		sec_ts_read(ts, SEC_TS_CMD_READ_ID, device_id, 6);
		if (device_id[1] == model_id[0] && device_id[2] == model_id[1]) {
			LOGI("Checksum enabled!");
			break;
		} else {
			ts->plat_data->spi_checksum_enable = 0;
			sec_ts_delay(20);
			ret = sec_ts_read(ts, SEC_TS_CMD_READ_ID, device_id, 6);
			if (ret < 0) {
				LOGE("read reg %#x failed, returned %i\n", SEC_TS_CMD_READ_ID, ret);
				return -EIO;
			}
			if (device_id[1] == model_id[0] && device_id[2] == model_id[1]) {
				LOGI("Checksum disabled!");
				break;
			} else {
				LOGE("[%d] Unknown checksum status! device_id %*ph", retry, 6, device_id);
			}
		}
		retry++;
	}

	/* Set checksum as enabled if the get checksum status failed. */
	if (retry == SEC_TS_IO_RETRY_CNT) {
		LOGW("Cannot get checksum status after retry, set checksum enabled as default.");
		ts->plat_data->spi_checksum_enable = 1;
	}

	return 0;
}

int sec_ts_wait_for_ready(struct sec_ts_data *ts, unsigned int ack, unsigned int time)
{
	int rc = -1;
	int retry = 0;
	int count = max(1, time / SEC_TS_WAIT_FOR_READY_INTERVAL_MS);
	u8 tBuff[SEC_TS_EVENT_BUFF_SIZE] = {0,};

	while (retry < count) {
		LOGI("Polling event %#x", ack);
		if (sec_ts_read(ts, SEC_TS_CMD_READ_ONE_EVENT, tBuff,
			SEC_TS_EVENT_BUFF_SIZE) >= 0) {
			if (((tBuff[0] >> 2) & 0xF) == TYPE_STATUS_EVENT_INFO) {
				if (tBuff[1] == ack) {
					rc = 0;
					break;
				}
			} else if (((tBuff[0] >> 2) & 0xF) == TYPE_STATUS_EVENT_VENDOR_INFO) {
				if (tBuff[1] == ack) {
					rc = 0;
					break;
				}
			}
		}
		sec_ts_delay(SEC_TS_WAIT_FOR_READY_INTERVAL_MS);
		retry++;
	}
	if (retry == count)
		LOGE("Time Over\n");

	LOGI("%02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X [%d]\n",
		tBuff[0], tBuff[1], tBuff[2], tBuff[3],
		tBuff[4], tBuff[5], tBuff[6], tBuff[7], retry);

	return rc;
}

static void sec_ts_reinit(struct sec_ts_data *ts)
{
	u8 w_data[2] = {0x00, 0x00};
	int ret = 0;

	LOGI("Cover=0x%x, Power mode=0x%x\n", ts->touch_functions, ts->lowpower_status);

	/* Cover mode */
	if (ts->touch_functions & SEC_TS_BIT_SETFUNC_COVER) {
		w_data[0] = ts->cover_cmd;
		ret = sec_ts_write(ts, SET_TS_CMD_SET_COVER_TYPE,
				   (u8 *)&w_data[0], 1);
		if (ret < 0)
			LOGE("Failed to send command(0x%x)", SET_TS_CMD_SET_COVER_TYPE);

		ret = sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
				   (u8 *)&(ts->touch_functions), 2);
		if (ret < 0)
			LOGE("Failed to send command(0x%x)", SEC_TS_CMD_TOUCH_FUNCTION);
	}

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* Power mode */
	if (ts->lowpower_status == TO_LOWPOWER_MODE) {
		w_data[0] = TO_LOWPOWER_MODE;
		ret = sec_ts_write(ts, SEC_TS_CMD_POWER_MODE,
				   (u8 *)&w_data[0], 1);
		if (ret < 0)
			LOGE("Failed to send command(0x%x)", SEC_TS_CMD_POWER_MODE);

		sec_ts_delay(50);

	} else {
		sec_ts_set_grip_type(ts, ONLY_EDGE_HANDLER);
	}
#endif
}

static void sec_ts_handle_coord_event(struct sec_ts_data *ts,
				struct sec_ts_event_coordinate *p_event_coord)
{
	u8 t_id;

#ifdef USE_OPEN_CLOSE
	if (ts->input_closed) {
		LOGE("device is closed\n");
		return;
	}
#endif

	t_id = (p_event_coord->tid - 1);

	if (t_id >= MAX_SUPPORT_TOUCH_COUNT + MAX_SUPPORT_HOVER_COUNT) {
		LOGE("tid(%d) is out of range\n", t_id);
		return;
	}

	ts->coord[t_id].id = t_id;
	ts->coord[t_id].action = p_event_coord->tchsta;
	ts->coord[t_id].x = (p_event_coord->x_15_12 << 12) |
			(p_event_coord->x_11_4 << 4) |
			(p_event_coord->x_3_0);
	ts->coord[t_id].y = (p_event_coord->y_15_12 << 12) |
			(p_event_coord->y_11_4 << 4) |
			(p_event_coord->y_3_0);
	ts->coord[t_id].z = p_event_coord->z & SEC_TS_PRESSURE_MAX;
	ts->coord[t_id].ttype = p_event_coord->ttype_3_2 << 2 | p_event_coord->ttype_1_0;
	ts->coord[t_id].major = p_event_coord->major * ts->plat_data->mm2px;
	ts->coord[t_id].minor = p_event_coord->minor * ts->plat_data->mm2px;

	ts->coord[t_id].palm = (ts->coord[t_id].ttype == SEC_TS_TOUCHTYPE_PALM);

	ts->coord[t_id].grip = (ts->coord[t_id].ttype == SEC_TS_TOUCHTYPE_GRIP);

	ts->coord[t_id].left_event = p_event_coord->left_event;

	if (ts->coord[t_id].z <= 0)
		ts->coord[t_id].z = 1;

	ts->coord[t_id].orientation = (s16)((p_event_coord->orientation_15_8 << 8) |
			p_event_coord->orientation_7_0);

	if (ts->coord[t_id].action != SEC_TS_COORDINATE_ACTION_RELEASE &&
			ts->coord[t_id].action != SEC_TS_COORDINATE_ACTION_PRESS &&
			ts->coord[t_id].action != SEC_TS_COORDINATE_ACTION_MOVE) {
		LOGW("do not support coordinate action(%d)\n", ts->coord[t_id].action);
		return;
	}

	if ((ts->coord[t_id].ttype != SEC_TS_TOUCHTYPE_NORMAL) &&
		(ts->coord[t_id].ttype != SEC_TS_TOUCHTYPE_PALM) &&
		(ts->coord[t_id].ttype != SEC_TS_TOUCHTYPE_GRIP) &&
		(ts->coord[t_id].ttype != SEC_TS_TOUCHTYPE_WET) &&
		(ts->coord[t_id].ttype != SEC_TS_TOUCHTYPE_GLOVE)) {
			LOGW("do not support coordinate type(%d)\n", ts->coord[t_id].ttype);
			goto unsupport_type;
	}

	if (ts->coord[t_id].action == SEC_TS_COORDINATE_ACTION_RELEASE) {
		s64 ms_delta;

		ts->coord[t_id].ktime_released = ktime_get();
		ms_delta = ktime_ms_delta(ts->coord[t_id].ktime_released,
					ts->coord[t_id].ktime_pressed);
		if (ts->longest_duration < ms_delta)
			ts->longest_duration = ms_delta;

		if (ts->touch_count > 0)
			ts->touch_count--;
		if (ts->touch_count == 0)
			ts->check_multi = 0;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		goog_input_mt_slot(ts->gti, ts->input_dev, t_id);
		if (ts->plat_data->support_mt_pressure){
			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_PRESSURE, 0);
		}
		goog_input_mt_report_slot_state(ts->gti, ts->input_dev, MT_TOOL_FINGER, 0);

		if (ts->touch_count == 0) {
			goog_input_report_key(ts->gti, ts->input_dev, BTN_TOUCH, 0);
			goog_input_report_key(ts->gti, ts->input_dev, BTN_TOOL_FINGER, 0);
		}
#else
		input_mt_slot(ts->input_dev, t_id);
		if (ts->plat_data->support_mt_pressure)
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);

		if (ts->touch_count == 0) {
			input_report_key(ts->input_dev, BTN_TOUCH, 0);
			input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
		}
#endif
	} else {
		if (ts->coord[t_id].action == SEC_TS_COORDINATE_ACTION_PRESS) {
			ts->coord[t_id].ktime_pressed = ktime_get();
			ts->touch_count++;
			if ((ts->touch_count > 4) && (ts->check_multi == 0)) {
				ts->check_multi = 1;
				ts->multi_count++;
			}
			ts->all_finger_count++;

			ts->max_z_value = max_t(unsigned int, ts->coord[t_id].z, ts->max_z_value);
			ts->min_z_value = min_t(unsigned int, ts->coord[t_id].z, ts->min_z_value);
			ts->sum_z_value += (unsigned int)ts->coord[t_id].z;
		} else if (ts->coord[t_id].action == SEC_TS_COORDINATE_ACTION_MOVE) {
			ts->coord[t_id].mcount++;
		}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		goog_input_mt_slot(ts->gti, ts->input_dev, t_id);
		if (ts->coord[t_id].palm)
			goog_input_mt_report_slot_state(ts->gti, ts->input_dev, MT_TOOL_PALM, 1);
		else if (ts->coord[t_id].grip)
			goog_input_mt_report_slot_state(ts->gti, ts->input_dev, MT_TOOL_PALM, 1);
		else
			goog_input_mt_report_slot_state(ts->gti, ts->input_dev, MT_TOOL_FINGER, 1);

		goog_input_report_key(ts->gti, ts->input_dev, BTN_TOUCH, 1);
		goog_input_report_key(ts->gti, ts->input_dev, BTN_TOOL_FINGER, 1);

		goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_POSITION_X,
				ts->coord[t_id].x);
		goog_input_report_abs(ts->gti, ts->input_dev,ABS_MT_POSITION_Y,
				ts->coord[t_id].y);
		goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_TOUCH_MAJOR,
				ts->coord[t_id].major);
		goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_TOUCH_MINOR,
				ts->coord[t_id].minor);
		goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_ORIENTATION,
				ts->coord[t_id].orientation);
		if (ts->plat_data->support_mt_pressure) {
			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_PRESSURE,
					ts->coord[t_id].z);
		}
#else
		input_mt_slot(ts->input_dev, t_id);
		if (ts->coord[t_id].palm)
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_PALM, 1);
		else if (ts->coord[t_id].grip)
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_PALM, 1);
		else
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 1);

		input_report_key(ts->input_dev, BTN_TOUCH, 1);
		input_report_key(ts->input_dev, BTN_TOOL_FINGER, 1);

		input_report_abs(ts->input_dev, ABS_MT_POSITION_X, ts->coord[t_id].x);
		input_report_abs(ts->input_dev,ABS_MT_POSITION_Y, ts->coord[t_id].y);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, ts->coord[t_id].major);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MINOR, ts->coord[t_id].minor);
		input_report_abs(ts->input_dev, ABS_MT_ORIENTATION, ts->coord[t_id].orientation);
		if (ts->plat_data->support_mt_pressure)
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, ts->coord[t_id].z);
#endif
	}

unsupport_type:
	if (ts->coord[t_id].action == SEC_TS_COORDINATE_ACTION_PRESS) {
		LOGD("[P] tID: %d x: %d y: %d z: %d major: %d minor: %d tc: %u type: %X\n",
			t_id, ts->coord[t_id].x,
			ts->coord[t_id].y, ts->coord[t_id].z,
			ts->coord[t_id].major,
			ts->coord[t_id].minor,
			ts->touch_count,
			ts->coord[t_id].ttype);
	} else if (ts->coord[t_id].action == SEC_TS_COORDINATE_ACTION_RELEASE) {
		LOGD("[R] tID: %d mc: %d tc: %u lx: %d ly: %d v: %02X%02X\n",
			t_id, ts->coord[t_id].mcount,
			ts->touch_count,
			ts->coord[t_id].x, ts->coord[t_id].y,
			ts->plat_data->img_version_of_ic[2],
			ts->plat_data->img_version_of_ic[3]);

		ts->coord[t_id].mcount = 0;
	}
}

static const char *mode_to_str(enum TOUCH_SYSTEM_MODE mode)
{
        switch (mode) {
        case TOUCH_SYSTEM_MODE_BOOT:        return "BOOT";
        case TOUCH_SYSTEM_MODE_CALIBRATION: return "CALIBRATION";
        case TOUCH_SYSTEM_MODE_TOUCH:       return "TOUCH";
        case TOUCH_SYSTEM_MODE_SELFTEST:    return "SELFTEST";
        case TOUCH_SYSTEM_MODE_FLASH:       return "FLASH";
        case TOUCH_SYSTEM_MODE_LOWPOWER:    return "LOWPOWER";
        case TOUCH_SYSTEM_MODE_SLEEP:       return "SLEEP";
        default:                            return "UNKNOWN_MODE";
        }
}

static const char *state_to_str(enum TOUCH_MODE_STATE state)
{
        switch (state) {
        case TOUCH_MODE_STATE_IDLE:     return "IDLE";
        case TOUCH_MODE_STATE_WET:      return "WET";
        case TOUCH_MODE_STATE_TOUCH:    return "TOUCH";
        case TOUCH_MODE_STATE_NOISY:    return "NOISY";
        case TOUCH_MODE_STATE_CAL:      return "CAL";
        case TOUCH_MODE_STATE_COVER:    return "COVER";
        case TOUCH_MODE_STATE_HOVER:    return "HOVER";
        case TOUCH_MODE_STATE_WAKEUP:   return "WAKEUP";
        default:                        return "UNKNOWN_STATE";
        }
}

static void sec_ts_read_vendor_event(struct sec_ts_data *ts,
					struct sec_ts_event_status *p_event_status)
{
	struct gti_fw_status_data gti_status_data = { 0 };
	u8 status_id = p_event_status->status_id;
	u8 status_data_1 = p_event_status->status_data_1;
	u8 status_data_2 = p_event_status->status_data_2;
	u8 status_data_3 = p_event_status->status_data_3;
	u8 status_data_4 = p_event_status->status_data_4;

	switch (status_id) {
	case SEC_TS_EVENT_STATUS_ID_OSC_CAL:
		LOGI("STATUS: OSC Cal: %d.\n", status_data_1);
		break;

	case SEC_TS_EVENT_STATUS_ID_HOPPING:
		LOGI("STATUS: Hopping Index: %d. noise level (0): %d, (1): %d, (2): %d\n",
				status_data_1, status_data_2, status_data_3, status_data_4);
		break;

	case SEC_TS_EVENT_STATUS_ID_STATE:
		LOGI("STATUS: mode/state change to %s/%s from %s/%s",
				mode_to_str(status_data_1), state_to_str(status_data_2),
				mode_to_str(status_data_3), state_to_str(status_data_4));
		break;

	case SEC_TS_EVENT_STATUS_ID_NOISE:
		LOGI("STATUS: noise: %#x\n", status_data_1);
		gti_status_data.noise_level = status_data_1;
		goog_notify_fw_status_changed(ts->gti, GTI_FW_STATUS_NOISE_MODE, &gti_status_data);
		break;

	case SEC_TS_EVENT_STATUS_ID_GRIP:
		if (ts->debug_status)
			LOGI("STATUS: grip: %d.\n", status_data_1);
		break;

	case SEC_TS_EVENT_STATUS_ID_PALM:
		LOGI("STATUS: palm: %d.\n", status_data_1);
		break;

	case SEC_TS_EVENT_STATUS_ID_FOD:
		LOGW("STATUS: SEC_TS_EVENT_STATUS_ID_FOD: %d.\n", status_data_1);
		break;


	default:
		break;
	}
}

static int sec_ts_read_heatmap(struct sec_ts_data *ts, u8 *heatmap_event_buff)
{
	int ret = 0;
	int i, j;
	const int tx_count = ts->tx_count;
	const int rx_count = ts->rx_count;
	const int heatmap_size = (8 + 4 + 4 + (10 * SEC_TS_EVENT_BUFF_SIZE)) +
			((tx_count + 2) * rx_count * 2) + VENDOR_REGISTER_DATA_SIZE;
	const int rawdata_size = (tx_count + 2) * rx_count * 2;
	const int event_total_size = 10 * SEC_TS_EVENT_BUFF_SIZE;
	u64 timestamp;
	u8 coord_count;
	int offset = 0;
	u8 *rBuff = ts->io_heatmap_buf;
	short *rTemp = ts->pFrametemp;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_HEATMAP, rBuff, heatmap_size);
	if (ret < 0) {
		LOGE("Failed to read heatmap!\n");
		return ret;
	}

	/* timestamp */
	timestamp = le64_to_cpup((__le64 *)&rBuff[offset]);
	offset += 8;

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	ts->timestamp_sensing += timestamp - ts->raw_timestamp_sensing;
	ts->raw_timestamp_sensing = timestamp;

	goog_input_set_sensing_timestamp(ts->gti, ts->input_dev,
			ts->timestamp_sensing * NSEC_PER_USEC);
#endif

	/* debug info(4) + sync status info(1) + reserved(2) */
	/* frame_time_coord = (rBuff[offset + 1] << 8) | rBuff[offset + 0];
	   frame_time_buffer = (rBuff[offset + 1] << 8) | rBuff[offset + 0];
	   sync_status_info = rBuff[offset]; */
	offset += (2 + 2 + 1 + 2);

	/* coordinate */
	coord_count = rBuff[offset];
	offset += 1;

	memcpy(heatmap_event_buff, &rBuff[offset], event_total_size);
	offset += event_total_size;

	/* rawdata */
	u8 *raw_ptr = &rBuff[offset];
	for (i = 0; i < (rawdata_size / 2); i++) {
		rTemp[i] = (short)(raw_ptr[0] | (raw_ptr[1] << 8));
		raw_ptr += 2;
	}

	offset += rawdata_size;

	/* flip mutual data */
	short *ms_p = ts->pFrameMS_irq;
	for (j = 0; j < rx_count; j++) {
		for (i = 0; i < tx_count; i++) {
			*ms_p++ = rTemp[(i * rx_count) + j];
		}
	}

	/* copy self data */
	memcpy(ts->pFrameSS_irq, &rTemp[tx_count * rx_count], rx_count * sizeof(short));
	memcpy(&ts->pFrameSS_irq[rx_count], &rTemp[tx_count * rx_count + rx_count],
	       tx_count * sizeof(short));

	/* simulation data */
	memcpy(ts->vendor_register_data, &rBuff[offset], VENDOR_REGISTER_DATA_SIZE);

	LOGD("heatmap coord count: %d.\n", coord_count);

	return coord_count;
}

void sec_ts_read_event(struct sec_ts_data *ts)
{
	int ret;
	u8 event_id;
	u8 left_event_count;
	u8 *event_buff;
	struct sec_ts_gesture_status *p_gesture_status;
	struct sec_ts_event_status *p_event_status;
	int curr_pos;
	int remain_event_count = 0;
	bool processed_pointer_event = false;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	bool set_timestamp = false;
#endif

	ret = event_id = curr_pos = remain_event_count = 0;

#if defined(I3C_INTERFACE)
	ret = sec_ts_read(ts, SEC_TS_CMD_READ_ALL_EVENT,
			  (u8 *)ts->read_event_buff[0], SEC_TS_EVENT_BUFF_SIZE);
	if (ret < 0) {
		LOGE("read events failed\n");
		return;
	}

	left_event_count = (u8)(ret / SEC_TS_EVENT_BUFF_SIZE);
	remain_event_count = left_event_count - 1;

	if (left_event_count > MAX_EVENT_COUNT - 1 ||
		left_event_count == 0xFF ||
		left_event_count == 0) {
		LOGE("event buffer overflow %d\n", left_event_count);

		/* write clear event stack command
		 * when read_event_count > MAX_EVENT_COUNT
		 **/
		ret = sec_ts_write(ts, SEC_TS_CMD_CLEAR_EVENT_STACK, NULL, 0);
		if (ret < 0)
			LOGE("write clear event failed\n");
		return;
	}

#else
	/* repeat READ_ONE_EVENT until buffer is empty(No event) */
	ret = sec_ts_read(ts, SEC_TS_CMD_READ_ONE_EVENT,
			  (u8 *)ts->read_event_buff[0], SEC_TS_EVENT_BUFF_SIZE);
	if (ret < 0) {
		LOGE("read one event failed\n");
		return;
	}

	if (ts->debug_events)
		LOGI("ONE: %02X %02X %02X %02X %02X %02X %02X %02X\n",
			ts->read_event_buff[0][0], ts->read_event_buff[0][1],
			ts->read_event_buff[0][2], ts->read_event_buff[0][3],
			ts->read_event_buff[0][4], ts->read_event_buff[0][5],
			ts->read_event_buff[0][6], ts->read_event_buff[0][7]);

	if (ts->read_event_buff[0][0] == 0) {
		LOGI("event buffer is empty\n");
		return;
	}

	left_event_count = ts->read_event_buff[0][7] & 0x3F;
	remain_event_count = left_event_count;

	if (left_event_count > MAX_EVENT_COUNT - 1 ||
		left_event_count == 0xFF) {
		LOGE("event buffer overflow %d\n", left_event_count);

		/* write clear event stack command
		 * when read_event_count > MAX_EVENT_COUNT
		 **/
		ret = sec_ts_write(ts, SEC_TS_CMD_CLEAR_EVENT_STACK, NULL, 0);
		if (ret < 0)
			LOGE("write clear event failed\n");
		return;
	}

	if (left_event_count > 0) {
		ret = sec_ts_read(ts, SEC_TS_CMD_READ_ALL_EVENT,
			(u8 *)ts->read_event_buff[1],
			sizeof(u8) * (SEC_TS_EVENT_BUFF_SIZE) *
				(left_event_count));
		if (ret < 0) {
			LOGE("read one event failed\n");
			return;
		}
	}
#endif

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	mutex_lock(&ts->eventlock);
#endif
	do {
		event_buff = ts->read_event_buff[curr_pos];
		event_id = event_buff[0] & 0x3;

		if (ts->debug_events && curr_pos > 0)
			LOGI("ALL: %02X %02X %02X %02X %02X %02X %02X %02X\n",
				event_buff[0], event_buff[1], event_buff[2],
				event_buff[3], event_buff[4], event_buff[5],
				event_buff[6], event_buff[7]);

		switch (event_id) {
		case SEC_TS_STATUS_EVENT:
			p_event_status = (struct sec_ts_event_status *)event_buff;

			if (p_event_status->stype == TYPE_STATUS_EVENT_HEATMAP_INFO) {
				int heatmap_coord_count =
					sec_ts_read_heatmap(ts, ts->heatmap_coord_buff);
				if (heatmap_coord_count < 0) {
					LOGE("Failed to read heatmap");
					break;
				} else if (heatmap_coord_count > MAX_SUPPORT_TOUCH_COUNT) {
					LOGE("Heatmap coord count is out of range: %d\n",
					     heatmap_coord_count);
					break;
				}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
				goog_input_set_timestamp(ts->gti, ts->input_dev,
							 ts->isr_timestamp);
#endif

				for (int i = 0; i < min(heatmap_coord_count,
				     MAX_SUPPORT_TOUCH_COUNT); i ++) {
					sec_ts_handle_coord_event(ts,
					    (struct sec_ts_event_coordinate *)
					    &ts->heatmap_coord_buff[i * SEC_TS_EVENT_BUFF_SIZE]);
				}
				break;
			} else if (p_event_status->stype == TYPE_STATUS_EVENT_VENDOR_INFO) {
				sec_ts_read_vendor_event(ts, p_event_status);
			} else if (p_event_status->stype == TYPE_STATUS_EVENT_INFO) {
				if (p_event_status->status_id == SEC_TS_ACK_BOOT_COMPLETE) {
					u8 status_data_1 = p_event_status->status_data_1;

					switch (status_data_1) {
					case 0x2:
						/* watchdog reset !? */
						LOGE("Touch - unexpected reset! Reason : WDT \n");
						break;
					case 0x4:
						LOGI("sw_reset ack.\n");
						complete_all(&ts->boot_completed);
						break;
					case 0x1:
						LOGI("hw_reset ack.\n");
						complete_all(&ts->boot_completed);
						break;
					default:
						LOGE("Unknown reset %#x", status_data_1);
						break;
					}
					queue_work(ts->event_wq, &ts->reset_handler_work);
				} else if (p_event_status->status_id == SEC_TS_ACK_WET_MODE) {
					ts->wet_mode = p_event_status->status_data_1;
					LOGI("STATUS: water wet mode %d\n", ts->wet_mode);
					if (ts->wet_mode) {
						ts->wet_count++;
						goog_notify_fw_status_changed(
							ts->gti, GTI_FW_STATUS_WATER_ENTER, NULL);
					} else {
						goog_notify_fw_status_changed(
							ts->gti, GTI_FW_STATUS_WATER_EXIT, NULL);
					}
				} else {
					LOGE("Unknown status event info.");
				}
			} else if (p_event_status->stype == TYPE_STATUS_EVENT_ERR) {
				switch (p_event_status->status_id) {
				case SEC_TS_ERR_EVNET_CORE_ERR:
					LOGE("Core error\n");
					break;
				case SEC_TS_ERR_EVENT_QUEUE_FULL:
					LOGE("IC Event Queue is full\n");
					break;
				case SEC_TS_ERR_EVENT_ESD:
					LOGE("ESD detected. run reset\n");
#ifdef USE_RESET_DURING_POWER_ON
					schedule_work(&ts->reset_work.work);
#endif
					break;
				case SEC_TS_ERR_EVENT_SYNC:
					LOGE("Sync error\n");
					break;
				default:
					LOGE("Unknown error.\n");
					break;
				}
			} else {
				LOGE("Unknown status event.");
			}

			LOGI("STATUS: %*ph", (int) sizeof(p_event_status->data),
					p_event_status->data);
			break;

		case SEC_TS_COORDINATE_EVENT:
			processed_pointer_event = true;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			if (!set_timestamp) {
				goog_input_set_timestamp(ts->gti, ts->input_dev,
							 ts->isr_timestamp);
				set_timestamp = true;
			}
#endif
			sec_ts_handle_coord_event(ts,
					(struct sec_ts_event_coordinate *)event_buff);
			break;

		case SEC_TS_GESTURE_EVENT:
			p_gesture_status = (struct sec_ts_gesture_status *)event_buff;
			switch (p_gesture_status->gesture_id) {
			case SEC_TS_GESTURE_CODE_STTW:
				LOGI("gesture STTW: x %d, y %d, major %d, minor %d, orientation %d\n",
					(int)((p_gesture_status->gesture_data_1 << 8) |
					      (p_gesture_status->gesture_data_2 << 0)),
					(int)((p_gesture_status->gesture_data_3 << 8) |
					      (p_gesture_status->gesture_data_4 << 0)),
					(int)p_gesture_status->gesture_data_5,
					(int)p_gesture_status->gesture_data_6,
					(int)((p_gesture_status->gesture_data_7 << 8) |
					      (p_gesture_status->gesture_data_8 << 0)));
				break;
			case SEC_TS_GESTURE_CODE_STTW_INVALID_OUT_OF_RANGE:
				LOGI("gesture STTW (invalid out of range): x %d, y %d\n",
					(int)((p_gesture_status->gesture_data_1 << 8) |
					      (p_gesture_status->gesture_data_2 << 0)),
					(int)((p_gesture_status->gesture_data_3 << 8) |
					      (p_gesture_status->gesture_data_4 << 0)));
				break;
			case SEC_TS_GESTURE_CODE_STTW_INVALID_SHIFT:
				LOGI("gesture STTW (invalid shift): init_x %d, init_y %d, final_x %d, final_y %d, distance %d\n",
					(int)((p_gesture_status->gesture_data_1 << 8) |
					      (p_gesture_status->gesture_data_2 << 0)),
					(int)((p_gesture_status->gesture_data_3 << 8) |
					      (p_gesture_status->gesture_data_4 << 0)),
					(int)((p_gesture_status->gesture_data_5 << 8) |
					      (p_gesture_status->gesture_data_6 << 0)),
					(int)((p_gesture_status->gesture_data_7 << 8) |
					      (p_gesture_status->gesture_data_8 << 0)),
					(int)((p_gesture_status->gesture_data_9 << 8) |
					      (p_gesture_status->gesture_data_10 << 0)));
				break;
			case SEC_TS_GESTURE_CODE_STTW_INVALID_PALM:
				LOGI("gesture STTW (invalid palm): x %d, y %d, node size %d\n",
					(int)((p_gesture_status->gesture_data_1 << 8) |
					      (p_gesture_status->gesture_data_2 << 0)),
					(int)((p_gesture_status->gesture_data_3 << 8) |
					      (p_gesture_status->gesture_data_4 << 0)),
					(int)p_gesture_status->gesture_data_5);
				break;
			case SEC_TS_GESTURE_CODE_STTW_INVALID_MULTI_TOUCH:
				LOGI("gesture STTW (invalid multi touch): x %d, y %d, valid finger count %d\n",
					(int)((p_gesture_status->gesture_data_1 << 8) |
					      (p_gesture_status->gesture_data_2 << 0)),
					(int)((p_gesture_status->gesture_data_3 << 8) |
					      (p_gesture_status->gesture_data_4 << 0)),
					(int)p_gesture_status->gesture_data_5);
				break;
			case SEC_TS_GESTURE_CODE_STTW_INVALID_LONG_PRESS:
				LOGI("gesture STTW (invalid long press): x %d, y %d, frame count %d\n",
					(int)((p_gesture_status->gesture_data_1 << 8) |
					      (p_gesture_status->gesture_data_2 << 0)),
					(int)((p_gesture_status->gesture_data_3 << 8) |
					      (p_gesture_status->gesture_data_4 << 0)),
					(int)p_gesture_status->gesture_data_5);
				break;
			case SEC_TS_GESTURE_CODE_STTW_INVALID_SHORT_PRESS:
				LOGI("gesture STTW (invalid short press): x %d, y %d, frame count %d\n",
					(int)((p_gesture_status->gesture_data_1 << 8) |
					      (p_gesture_status->gesture_data_2 << 0)),
					(int)((p_gesture_status->gesture_data_3 << 8) |
					      (p_gesture_status->gesture_data_4 << 0)),
					(int)p_gesture_status->gesture_data_5);
				break;
			default:
				LOGI("unknown gesture %x %x %x %x %x %x\n",
				event_buff[0], event_buff[1], event_buff[2],
				event_buff[3], event_buff[4], event_buff[5]);
				break;
			}
			break;

		default:
			LOGE("unknown event %x %x %x %x %x %x\n",
				event_buff[0], event_buff[1], event_buff[2],
				event_buff[3], event_buff[4], event_buff[5]);
			break;
		}
		curr_pos++;
		remain_event_count--;
	} while (remain_event_count >= 0);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_sync(ts->gti, ts->input_dev);
#else
	input_set_timestamp(ts->input_dev, ts->isr_timestamp);
	input_sync(ts->input_dev);
	mutex_unlock(&ts->eventlock);
#endif
}

static irqreturn_t sec_ts_isr(int irq, void *handle)
{
	struct sec_ts_data *ts = (struct sec_ts_data *)handle;

	ts->isr_timestamp = ktime_get();

	return IRQ_WAKE_THREAD;
}

static irqreturn_t sec_ts_irq_thread(int irq, void *ptr)
{
	struct sec_ts_data *ts = (struct sec_ts_data *)ptr;

	sec_ts_read_event(ts);

	return IRQ_HANDLED;
}

int sec_ts_glove_mode_enables(struct sec_ts_data *ts, int mode)
{
	int ret;

	if (mode)
		ts->touch_functions = (ts->touch_functions |
				       SEC_TS_BIT_SETFUNC_GLOVE |
				       SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC);
	else
		ts->touch_functions = ((ts->touch_functions &
					(~SEC_TS_BIT_SETFUNC_GLOVE)) |
				       SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("pwr off, glove: %d, status: %x\n",
			mode, ts->touch_functions);
		goto glove_enable_err;
	}

	ret = sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			   (u8 *)&ts->touch_functions, 2);
	if (ret < 0) {
		LOGE("Failed to send command");
		goto glove_enable_err;
	}

	LOGI("glove: %d, status: %x\n", mode, ts->touch_functions);

	return 0;

glove_enable_err:
	return -EIO;
}
EXPORT_SYMBOL(sec_ts_glove_mode_enables);

int sec_ts_set_cover_type(struct sec_ts_data *ts, bool enable)
{
	int ret;

	LOGI("%d\n", ts->cover_type);


	switch (ts->cover_type) {
	case SEC_TS_VIEW_WIRELESS:
	case SEC_TS_VIEW_COVER:
	case SEC_TS_VIEW_WALLET:
	case SEC_TS_FLIP_WALLET:
	case SEC_TS_LED_COVER:
	case SEC_TS_MONTBLANC_COVER:
	case SEC_TS_CLEAR_FLIP_COVER:
	case SEC_TS_QWERTY_KEYBOARD_EUR:
	case SEC_TS_QWERTY_KEYBOARD_KOR:
		ts->cover_cmd = (u8)ts->cover_type;
		break;
	case SEC_TS_CHARGER_COVER:
	case SEC_TS_COVER_NOTHING1:
	case SEC_TS_COVER_NOTHING2:
	default:
		ts->cover_cmd = 0;
		LOGE("not chage touch state, %d\n", ts->cover_type);
		break;
	}

	if (enable)
		ts->touch_functions = (ts->touch_functions |
				       SEC_TS_BIT_SETFUNC_COVER |
				       SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC);
	else
		ts->touch_functions = ((ts->touch_functions &
					(~SEC_TS_BIT_SETFUNC_COVER)) |
				       SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("pwr off, close: %d, status: %x\n",
			enable, ts->touch_functions);
		goto cover_enable_err;
	}

	if (enable) {
		ret = sec_ts_write(ts, SET_TS_CMD_SET_COVER_TYPE,
				   &ts->cover_cmd, 1);
		if (ret < 0) {
			LOGE("Failed to send covertype command: %d", ts->cover_cmd);
			goto cover_enable_err;
		}
	}

	ret = sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			   (u8 *)&(ts->touch_functions), 2);
	if (ret < 0) {
		LOGE("Failed to send command");
		goto cover_enable_err;
	}

	LOGI("close: %d, status: %x\n", enable, ts->touch_functions);

	return 0;

cover_enable_err:
	return -EIO;


}
EXPORT_SYMBOL(sec_ts_set_cover_type);

void sec_ts_set_grip_type(struct sec_ts_data *ts, u8 set_type)
{
	u8 mode = G_NONE;

	LOGI("re-init grip(%d), edh: %d, edg: %d, lan: %d\n",
		set_type, ts->grip_edgehandler_direction, ts->grip_edge_range,
		ts->grip_landscape_mode);

	/* edge handler */
	if (ts->grip_edgehandler_direction != 0)
		mode |= G_SET_EDGE_HANDLER;

	if (set_type == GRIP_ALL_DATA) {
		/* edge */
		if (ts->grip_edge_range != 60)
			mode |= G_SET_EDGE_ZONE;

		/* dead zone */
		if (ts->grip_landscape_mode == 1)	/* default 0 mode, 32 */
			mode |= G_SET_LANDSCAPE_MODE;
		else
			mode |= G_SET_NORMAL_MODE;
	}

	if (mode)
		set_grip_data_to_ic(ts, mode);

}

/* for debugging--------------------------------------------------------------*/

static int sec_ts_pinctrl_configure(struct sec_ts_data *ts, bool enable)
{
	struct pinctrl_state *state;

	LOGI("%s\n", enable ? "ACTIVE" : "SUSPEND");

	if (enable) {
		state = pinctrl_lookup_state(ts->plat_data->pinctrl,
					     "ts_active");
		if (IS_ERR(ts->plat_data->pinctrl))
			LOGE("could not get active pinstate\n");
	} else {
		state = pinctrl_lookup_state(ts->plat_data->pinctrl,
					     "ts_suspend");
		if (IS_ERR(ts->plat_data->pinctrl))
			LOGE("could not get suspend pinstate\n");
	}

	if (!IS_ERR_OR_NULL(state))
		return pinctrl_select_state(ts->plat_data->pinctrl, state);

	return 0;

}

static int sec_ts_parse_dt(struct device *dev)
{
	struct sec_ts_plat_data *pdata = dev->platform_data;
	struct device_node *np = dev->of_node;
	int ret = 0;

	scnprintf(pdata->firmware_name, sizeof(pdata->firmware_name), "%s",
		SEC_TS_DEFAULT_FW_NAME);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	int panel_id;

	panel_id = goog_get_panel_id(np);
	if (panel_id < 0)
		return -EPROBE_DEFER;

	memset(pdata->firmware_name, 0, sizeof(pdata->firmware_name));
	goog_get_firmware_name(np, panel_id, pdata->firmware_name, sizeof(pdata->firmware_name));
	memset(pdata->selftest_limit_name, 0, sizeof(pdata->selftest_limit_name));
	goog_get_test_limits_name(np, panel_id, pdata->selftest_limit_name,
			sizeof(pdata->selftest_limit_name));
#endif

	LOGI("Firmware name: %s", pdata->firmware_name);

	pdata->irq_gpio = of_get_named_gpio(np, "sec,irq_gpio", 0);
	if (gpio_is_valid(pdata->irq_gpio)) {
		ret = gpio_request_one(pdata->irq_gpio, GPIOF_IN,
				       "sec,tsp_int");
		if (ret) {
			LOGE("Unable to request tsp_int [%d]\n", pdata->irq_gpio);
			return -EINVAL;
		}
	} else {
		LOGE("Failed to get irq gpio\n");
		return -EINVAL;
	}

	pdata->irq = gpio_to_irq(pdata->irq_gpio);

	if (of_property_read_u32(np, "sec,irq_type", &pdata->irq_type)) {
		LOGD("no irq_type property, set to default!\n");
		pdata->irq_type = IRQF_TRIGGER_LOW | IRQF_ONESHOT;
	}

	if (of_property_read_u32(np, "sec,io-burstmax", &pdata->io_burstmax)) {
		LOGD("Failed to get io_burstmax property\n");
		pdata->io_burstmax = 1024;
	}

	if (pdata->io_burstmax > IO_PREALLOC_READ_BUF_SZ ||
	    pdata->io_burstmax > IO_PREALLOC_WRITE_BUF_SZ) {
		LOGE("io_burstmax is larger than io_read_buf and/or io_write_buf.\n");
		return -EINVAL;
	}

	pdata->reset_gpio = of_get_named_gpio(np, "sec,reset_gpio", 0);
	if (gpio_is_valid(pdata->reset_gpio)) {
		ret = gpio_request_one(pdata->reset_gpio,
					GPIOF_OUT_INIT_HIGH,
					"sec,touch_reset_gpio");
		if (ret) {
			LOGE("Failed to request gpio %d, ret %d\n",
				  pdata->reset_gpio, ret);
			pdata->reset_gpio = -1;
		}
		ret = gpio_direction_output(pdata->reset_gpio, 0);

	} else
		LOGE("Failed to get reset_gpio\n");

	if (of_property_read_u32(np, "sec,bringup", &pdata->bringup) < 0)
		pdata->bringup = 0;

#ifdef KEY_SIDE_GESTURE
	pdata->support_sidegesture = of_property_read_bool(np,
			"sec,support_sidegesture");
#endif

	pdata->support_mt_pressure = true;

	if (of_property_read_u8(np, "sec,mm2px", &pdata->mm2px) < 0)
		pdata->mm2px = 1;
	LOGI("mm2px %d\n", pdata->mm2px);

	LOGI("io_burstmax: %d, bringup: %d, FW: %s\n", pdata->io_burstmax, pdata->bringup,
		pdata->firmware_name);
	return ret;
}

int sec_ts_ddi_osc_on(struct sec_ts_data *ts)
{
	int ret;
	u32 MDDI_OSC_ENABLE = 0x40010010;
	u8 OSCEnableAndWriteKey[4] = { 0x11, 0x00, 0x5A, 0x5A };

	/* MDDI_OSC_ENABLE */
	ret = sec_ts_memorywrite(ts, MDDI_OSC_ENABLE, OSCEnableAndWriteKey, 4);

	return ret;
}

int sec_ts_enter_recovery(struct sec_ts_data *ts, u8 on, u8 hw_reset, u8 irq_control)
{
	struct sec_ts_plat_data *pdata = ts->plat_data;
	int ret;

	if (on == 1) {
		if (irq_control == 1) {
			ts->sec_irq_enable(ts, false);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			if (ts->gti)
				goog_devm_free_irq(ts->gti, &ts->client->dev, ts->plat_data->irq);
			else
#endif
			free_irq(ts->plat_data->irq, ts);
		}

		gpio_free(pdata->irq_gpio);

		LOGI("gpio free\n");
		if (gpio_is_valid(pdata->irq_gpio)) {
			ret = gpio_request_one(pdata->irq_gpio, GPIOF_OUT_INIT_LOW, "sec,tsp_int");
			LOGI("gpio request one\n");
			if (ret < 0)
				LOGE("Unable to request tsp_int [%d]: %d\n", pdata->irq_gpio, ret);
		} else {
			LOGE("Failed to get irq gpio\n");
			return -EINVAL;
		}

		if (hw_reset == 1) {
			mdelay(50);
			sec_ts_hw_reset(ts, false);
		}
	} else {
		gpio_free(pdata->irq_gpio);

		if (gpio_is_valid(pdata->irq_gpio)) {
			ret = gpio_request_one(pdata->irq_gpio, GPIOF_IN, "sec,tsp_int");
			if (ret) {
				LOGE("Unable to request tsp_int [%d]\n", pdata->irq_gpio);
				return -EINVAL;
			}
		} else {
			LOGE("Failed to get irq gpio\n");
			return -EINVAL;
		}

		if (hw_reset == 1) {
			mdelay(50);
			sec_ts_hw_reset(ts, false);
		}

		if (irq_control == 1) {
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			if (ts->gti)
				ret = goog_devm_request_threaded_irq(ts->gti, &ts->client->dev,
						ts->plat_data->irq, goog_sec_ts_isr,
						goog_sec_ts_irq_thread, ts->plat_data->irq_type,
						SEC_TS_NAME, ts);
			else
#endif
			ret = request_threaded_irq(pdata->irq, sec_ts_isr, sec_ts_irq_thread,
					ts->plat_data->irq_type, SEC_TS_NAME, ts);
			if (ret < 0) {
				LOGE("Unable to request threaded irq\n");
				return -EINVAL;
			} else {
				atomic_set(&ts->irq_enabled, 1);
			}
		}
	}

	return ret;
}

int sec_ts_read_information(struct sec_ts_data *ts)
{
	unsigned char data[13] = { 0 };
	int ret;

	memset(data, 0x0, 6);
	ret = sec_ts_read(ts, SEC_TS_CMD_READ_ID, data, 6);
	if (ret < 0) {
		LOGE("failed to read device id(%d)\n", ret);
		goto out;
	}
	LOGI("%X, %X, %X, %X, %X, %X\n", data[0], data[1], data[2], data[3], data[4], data[5]);

	memset(data, 0x0, 11);
	ret = sec_ts_read(ts,  SEC_TS_CMD_READ_PANEL_INFO, data, 11);
	if (ret < 0) {
		LOGE("failed to read sub id(%d)\n", ret);
		goto out;
	}
	LOGI("nTX: %d, nRX: %d, rY: %d, rX: %d\n", data[8], data[9],
		(data[2] << 8) | data[3], (data[0] << 8) | data[1]);

	/* Set X,Y Resolution from IC information. */
	if ((data[0] != 0xFF && data[1] != 0xFF)
		&& ((data[0] << 8) | data[1]) > 0)
		ts->plat_data->max_x = ((data[0] << 8) | data[1]) - 1;

	if ((data[2] != 0xFF && data[3] != 0xFF)
		&& ((data[2] << 8) | data[3]) > 0)
		ts->plat_data->max_y = ((data[2] << 8) | data[3]) - 1;

	ts->tx_count = data[8];
	ts->rx_count = data[9];

	memset(data, 0x0, 2);
	ret = sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, data, 2);
	if (ret < 0) {
		LOGE("Failed to read sub id(%d)\n", ret);
		goto out;
	}

	LOGI("TS_STATUS: %02X, %02X\n", data[0], data[1]);

	ret = sec_ts_read(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			  (u8 *)&(ts->touch_functions), 2);
	if (ret < 0) {
		LOGE("Failed to read touch functions(%d)\n", ret);
		goto out;
	}

	LOGI("Functions: %04X\n", ts->touch_functions);

out:
	return ret;
}

static void sec_ts_set_input_prop(struct sec_ts_data *ts,
				  struct input_dev *dev, u8 propbit)
{
	static char sec_ts_phys[64] = { 0 };

	snprintf(sec_ts_phys, sizeof(sec_ts_phys), "%s/input1", dev->name);
	dev->phys = sec_ts_phys;
#if defined(I3C_INTERFACE)
	// dev->id.bustype = BUS_I3C; // BUS_I3C not defined
#elif defined(I2C_INTERFACE)
	dev->id.bustype = BUS_I2C;
#else
	dev->id.bustype = BUS_SPI;
#endif
	dev->dev.parent = &ts->client->dev;

	set_bit(EV_SYN, dev->evbit);
	set_bit(EV_KEY, dev->evbit);
	set_bit(EV_ABS, dev->evbit);
	set_bit(EV_SW, dev->evbit);
	set_bit(BTN_TOUCH, dev->keybit);
	set_bit(BTN_TOOL_FINGER, dev->keybit);
#ifdef SEC_TS_SUPPORT_TOUCH_KEY
	if (ts->plat_data->support_mskey) {
		int i;

		for (i = 0 ; i < ts->plat_data->num_touchkey ; i++)
			set_bit(ts->plat_data->touchkey[i].keycode,
				dev->keybit);

		set_bit(EV_LED, dev->evbit);
		set_bit(LED_MISC, dev->ledbit);
	}
#endif
#ifdef KEY_SIDE_GESTURE
	if (ts->plat_data->support_sidegesture) {
		set_bit(KEY_SIDE_GESTURE, dev->keybit);
		set_bit(KEY_SIDE_GESTURE_RIGHT, dev->keybit);
		set_bit(KEY_SIDE_GESTURE_LEFT, dev->keybit);
	}
#endif
	set_bit(propbit, dev->propbit);
	set_bit(KEY_HOMEPAGE, dev->keybit);

	input_set_abs_params(dev, ABS_MT_POSITION_X, 0, ts->plat_data->max_x, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, ts->plat_data->max_y, 0, 0);
	input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR, 0, 255 * ts->plat_data->mm2px, 0, 0);
	input_set_abs_params(dev, ABS_MT_TOUCH_MINOR, 0, 255 * ts->plat_data->mm2px, 0, 0);
	input_set_abs_params(dev, ABS_MT_TOOL_TYPE, MT_TOOL_FINGER, MT_TOOL_FINGER, 0, 0);
	if (ts->plat_data->support_mt_pressure)
		input_set_abs_params(dev, ABS_MT_PRESSURE, 0, SEC_TS_PRESSURE_MAX, 0, 0);

	/* Units are (-4096, 4096), representing the range between rotation
	 * 90 degrees to left and 90 degrees to the right.
	 */
	input_set_abs_params(dev, ABS_MT_ORIENTATION, -4096, 4096, 0, 0);

	if (propbit == INPUT_PROP_POINTER)
		input_mt_init_slots(dev, MAX_SUPPORT_TOUCH_COUNT, INPUT_MT_POINTER);
	else
		input_mt_init_slots(dev, MAX_SUPPORT_TOUCH_COUNT, INPUT_MT_DIRECT);

	input_set_drvdata(dev, ts);
}

static int sec_ts_fw_init(struct sec_ts_data *ts)
{
	int ret = SEC_TS_ERR_NA;
	bool force_update = false;
	unsigned char data[2] = { 0 };
	unsigned char deviceID[6] = { 0 };

	ret = sec_ts_read(ts, SEC_TS_CMD_READ_ID, deviceID, 6);
	if (ret < 0)
		LOGE("Failed to read device ID(%d)\n", ret);
	else
		LOGI("DEVICE ID: %02X, %02X, %02X, %02X, %02X, %02X\n",
			deviceID[0], deviceID[1], deviceID[2],
			deviceID[3], deviceID[4], deviceID[5]);

	ret = sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, data, 2);
	if (ret < 0)
		LOGE("Failed to touch status(%d)\n", ret);
	LOGI("TOUCH STATUS: %02X || %02X, %02X\n", deviceID[0], data[0], data[1]);

	if ((deviceID[0] & 0xF0) == SEC_TS_STATUS_APP_MODE) {
		ts->checksum_result = 0;
		force_update = false;
	} else {
		ts->checksum_result = 1;
		force_update = true;
	}

	ret = sec_ts_read_information(ts);
	if (ret < 0) {
		LOGE("Failed to read information 0x%x\n", ret);
		return SEC_TS_ERR_INIT;
	}

	ts->touch_functions |= SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC;
	ret = sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			       (u8 *)&ts->touch_functions, 2);
	if (ret < 0)
		LOGE("Failed to send touch func_mode command");

	/* SENSE_ON */
	ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
	if (ret < 0) {
		LOGE("Failed to write SENSE_ON 0x%x\n", ret);
		return SEC_TS_ERR_INIT;
	}

	ts->pFrame = devm_kzalloc(&ts->client->dev,
				  (ts->tx_count + 2) * ts->rx_count * 2, GFP_KERNEL);
	if (!ts->pFrame)
		return SEC_TS_ERR_ALLOC_FRAME;


	ts->pFrameSS = devm_kzalloc(&ts->client->dev, (ts->tx_count + ts->rx_count) * 2,
			GFP_KERNEL);
	if (!ts->pFrameSS)
		return SEC_TS_ERR_ALLOC_FRAME_SS;

	ts->pFrameMS_irq = devm_kzalloc(&ts->client->dev, (ts->tx_count + 2) * ts->rx_count * 2,
			GFP_KERNEL);
	if (!ts->pFrameMS_irq)
		return SEC_TS_ERR_ALLOC_FRAME;


	ts->pFrameSS_irq = devm_kzalloc(&ts->client->dev, (ts->tx_count + ts->rx_count) * 2,
			GFP_KERNEL);
	if (!ts->pFrameSS_irq)
		return SEC_TS_ERR_ALLOC_FRAME;

	ts->pFrametemp = devm_kzalloc(&ts->client->dev, (ts->tx_count + 2) * ts->rx_count * 2,
			GFP_KERNEL);
	if (!ts->pFrametemp)
		return SEC_TS_ERR_ALLOC_FRAME;


	ts->input_dev->name = "sec_touchscreen";
	sec_ts_set_input_prop(ts, ts->input_dev, INPUT_PROP_DIRECT);
#ifdef USE_OPEN_CLOSE
	ts->input_dev->open = sec_ts_input_open;
	ts->input_dev->close = sec_ts_input_close;
#endif

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	ret = goog_input_register_device(&ts->client->dev, ts->input_dev);
#else
	ret = input_register_device(ts->input_dev);
#endif
	if (ret) {
		LOGE("Unable to register %s input device 0x%x\n", ts->input_dev->name, ret);
		return SEC_TS_ERR_REG_INPUT_DEV;
	}

	return SEC_TS_ERR_NA;
}

static void sec_ts_device_init(struct sec_ts_data *ts)
{
#if (1) //!defined(CONFIG_SAMSUNG_PRODUCT_SHIP)
	sec_ts_raw_device_init(ts);
#endif
	sec_ts_fn_init(ts);
}

#if defined(I3C_INTERFACE)
static const struct i3c_device_id sec_ts_id[] = {
	I3C_DEVICE(/* (0x0216 >> 1) */ (__u16)0x010B, (__u16)0xC840, (void *)5),
	{ },
};
MODULE_DEVICE_TABLE(i3c, sec_ts_id);
#elif defined(I2C_INTERFACE)
static const struct i2c_device_id sec_ts_id[] = {
	{ SEC_TS_NAME, 0 },
	{ },
};
#else
/* spi */
#endif

#if defined(I3C_INTERFACE)
static int sec_ts_probe(struct i3c_device *client)
#elif defined(I2C_INTERFACE)
static int sec_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
#else
static int sec_ts_probe(struct spi_device *client)
#endif
{
	struct sec_ts_data *ts;
	struct sec_ts_plat_data *pdata;
	int ret = 0;
#if defined(I3C_INTERFACE)
	const struct i3c_device_id *id;
#endif
	u8 deviceID[6] = { 0 };

	LOGI("\n");

#if defined(I3C_INTERFACE)
	id = i3c_device_match_id(client, sec_ts_id);
	LOGI("I3C i3c id->manuf_id =0x%x id->part_id =0x%x \n", id->manuf_id, id->part_id);

#elif defined(I2C_INTERFACE)
	LOGI("I2C interface\n");
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		LOGE("EIO err!\n");
		return -EIO;
	}
#else
	if (client->controller->rt == false) {
		client->rt = true;
		ret = spi_setup(client);
		if (ret < 0) {
			LOGE("setup SPI rt failed(%d)\n", ret);
		}
	}
	LOGI("SPI interface(%d Hz)\n", client->max_speed_hz);
#endif
	/* parse dt */
	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(struct sec_ts_plat_data), GFP_KERNEL);

		if (!pdata) {
			LOGE("Failed to allocate platform data\n");
			goto error_allocate_pdata;
		}

		client->dev.platform_data = pdata;

		ret = sec_ts_parse_dt(&client->dev);
		if (ret) {
			LOGE("Failed to parse dt\n");
			goto error_allocate_mem;
		}
	} else {
		pdata = client->dev.platform_data;
		if (!pdata) {
			LOGE("No platform data found\n");
			goto error_allocate_pdata;
		}
	}

	LOGI("SPI DMA %s", goog_check_spi_dma_enabled(client) ? "enable":"disable");

	pdata->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR(pdata->pinctrl))
		LOGE("could not get pinctrl\n");

	ts = devm_kzalloc(&client->dev, sizeof(struct sec_ts_data), GFP_KERNEL);
	if (!ts)
		goto error_allocate_mem;

	ts->client = client;
	ts->plat_data = pdata;
	ts->crc_addr = 0x0001FE00;
	ts->fw_addr = 0x00002000;
	ts->para_addr = 0x18000;
	ts->flash_page_size = SEC_TS_FW_BLK_SIZE_DEFAULT;
	ts->sec_ts_read = sec_ts_read;
	ts->sec_ts_write = sec_ts_write;
	ts->sec_ts_write_burst = sec_ts_write_burst;
	ts->sec_ts_read_bulk = sec_ts_read_bulk;
	ts->sec_irq_enable = sec_irq_enable;
	ts->sec_disable_irq_nosync = sec_disable_irq_nosync;
	ts->io_burstmax = pdata->io_burstmax;

	ts->io_read_buf = devm_kzalloc(&client->dev, IO_PREALLOC_READ_BUF_SZ, GFP_KERNEL);
	if (!ts->io_read_buf)
		goto error_allocate_mem;

	ts->io_write_buf = devm_kzalloc(&client->dev, IO_PREALLOC_WRITE_BUF_SZ, GFP_KERNEL);
	if (!ts->io_write_buf)
		goto error_allocate_mem;

	INIT_WORK(&ts->reset_handler_work, sec_ts_reset_handler_work);

#ifdef USE_POWER_RESET_WORK
	INIT_DELAYED_WORK(&ts->reset_work, sec_ts_reset_work);
#endif

	ts->event_wq = alloc_workqueue("sec_ts-event-queue", WQ_UNBOUND |
					 WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!ts->event_wq) {
		LOGE("Cannot create work thread\n");
		ret = -ENOMEM;
		goto error_allocate_mem;
	}

#ifdef SEC_TS_FW_UPDATE_ON_PROBE
	INIT_WORK(&ts->fw_update_work, sec_ts_fw_update_work);
#else
	LOGI("fw update on probe disabled!\n");
	ts->fw_update_wq = alloc_workqueue("sec_ts-fw-update-queue",
					    WQ_UNBOUND | WQ_HIGHPRI |
					    WQ_CPU_INTENSIVE, 1);
	if (!ts->fw_update_wq) {
		LOGE("Can't alloc fw update work thread\n");
		ret = -ENOMEM;
		goto error_alloc_fw_update_wq;
	}
	INIT_DELAYED_WORK(&ts->fw_update_work, sec_ts_fw_update_work);
#endif
	ts->is_fw_corrupted = false;


#if defined(I3C_INTERFACE)
	dev_set_drvdata(&client->dev, ts);
#elif defined(I2C_INTERFACE)
	i2c_set_clientdata(client, ts);
#else
	spi_set_drvdata(client, ts);
#endif

	ts->input_dev = input_allocate_device();
	if (!ts->input_dev) {
		LOGE("allocate device err!\n");
		ret = -ENOMEM;
		goto err_allocate_input_dev;
	}

	ts->touch_count = 0;
	ts->max_z_value = 0;
	ts->min_z_value = 0xFFFFFFFF;
	ts->sum_z_value = 0;

	mutex_init(&ts->device_mutex);
	mutex_init(&ts->io_mutex);
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	mutex_init(&ts->eventlock);
#endif

	init_completion(&ts->boot_completed);
	complete_all(&ts->boot_completed);

	sec_ts_pinctrl_configure(ts, true);

	/* power enable */
	ts->power_status = SEC_TS_STATE_POWER_ON;
	mdelay(10);
	#if defined(I3C_INTERFACE)
	// sec_ts_sw_reset(ts ,false);
	#else
	ret = gpio_direction_output(pdata->reset_gpio, 1);
	mdelay(10);
	ret = gpio_direction_output(pdata->reset_gpio, 0);
	mdelay(10);
	ret = gpio_direction_output(pdata->reset_gpio, 1);
	mdelay(10);
	#endif

	sec_ts_delay(100);
	sec_ts_get_checksum_status(ts);

	ret = sec_ts_wait_for_ready(ts, SEC_TS_ACK_BOOT_COMPLETE,
			SEC_TS_BOOT_COMPLETE_TIME_MS);
	if (ret < 0) {
		u8 boot_status;
		/* Read the boot status in case device is in bootloader mode */
		ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_ID, deviceID, 6);
		if (ret < 0) {
			LOGE("could not read boot status. Assuming no device connected.\n");
			ret = -EPROBE_DEFER;
			goto err_init;
		}

		boot_status = deviceID[0] & 0xF0;
		switch (boot_status) {
		case SEC_TS_STATUS_BOOT_MODE:
			LOGE("boot timeout(status %#x)! Reflash FW to recover.\n", boot_status);
			ret = sec_ts_firmware_update_on_probe(ts, true);
			if (ret) {
				ts->is_fw_corrupted = true;
				ret = -EPROBE_DEFER;
				goto err_init;
			}
			break;
		case SEC_TS_STATUS_APP_MODE:
		default:
			LOGE("boot timeout(status %#x)! Reset system to recover.\n", boot_status);
			sec_ts_system_reset(ts, RESET_MODE_HW, true, false);
			break;
		}
	}

	LOGI("power enable\n");

	if (ts->is_fw_corrupted == false) {
		switch (sec_ts_fw_init(ts)) {
		case SEC_TS_ERR_INIT:
		case SEC_TS_ERR_ALLOC_FRAME:
		case SEC_TS_ERR_ALLOC_FRAME_SS:
			ret = -EPROBE_DEFER;
			goto err_init;
		case SEC_TS_ERR_REG_INPUT_DEV:
			goto err_input_register_device;
		}
	}

	LOGI("request_irq = %d\n", ts->plat_data->irq);
	ret = request_threaded_irq(ts->plat_data->irq, sec_ts_isr, sec_ts_irq_thread,
			ts->plat_data->irq_type, SEC_TS_NAME, ts);
	if (ret < 0) {
		LOGE("Unable to request threaded irq\n");
		goto err_irq;
	} else {
		atomic_set(&ts->irq_enabled, 1);
	}

#ifndef CONFIG_SEC_SYSFS
	LOGI("create sec_class\n");
	sec_class = class_create("sec");
#endif

	LOGI("device_init_wakeup\n");
	device_init_wakeup(&client->dev, true);

	if (ts->is_fw_corrupted == false) {
		LOGI("sec_ts_device_init\n");
		sec_ts_device_init(ts);
	}
#ifdef SEC_TS_FW_UPDATE_ON_PROBE
	schedule_work(&ts->fw_update_work);

	/* Do not finish probe without checking and flashing the firmware */
	flush_work(&ts->fw_update_work);
#else
	queue_delayed_work(ts->fw_update_wq, &ts->fw_update_work,
		    msecs_to_jiffies(SEC_TS_FW_UPDATE_DELAY_MS_AFTER_PROBE));
#endif

#if defined(CONFIG_TOUCHSCREEN_DUMP_MODE)
	dump_callbacks.inform_dump = dump_tsp_log;
	INIT_DELAYED_WORK(&ts->ghost_check, sec_ts_check_rawdata);
	p_ghost_check = &ts->ghost_check;
#endif

	ts->probe_done = true;

	LOGI("done\n");

	return 0;

	/* need to be enabled when new goto statement is added */
	/*
	*	sec_ts_fn_remove(ts);
	*	free_irq(ts->plat_data->irq, ts);
	**/
err_irq:
err_input_register_device:
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_input_unregister_device(&ts->client->dev, ts->input_dev);
#else
	input_unregister_device(ts->input_dev);
#endif
	ts->input_dev = NULL;

err_init:
	if (ts->input_dev)
		input_free_device(ts->input_dev);

err_allocate_input_dev:
#ifndef SEC_TS_FW_UPDATE_ON_PROBE
	if (ts->fw_update_wq)
		destroy_workqueue(ts->fw_update_wq);

error_alloc_fw_update_wq:
#endif

	if (ts->event_wq)
		destroy_workqueue(ts->event_wq);

error_allocate_mem:
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);

error_allocate_pdata:
	if (ret == -ECONNREFUSED)
		sec_ts_delay(100);
	if (ret != -EPROBE_DEFER)
		ret = -ENODEV;
#ifdef CONFIG_TOUCHSCREEN_DUMP_MODE
	p_ghost_check = NULL;
#endif
	LOGE("failed(%d)\n", ret);
	return ret;
}

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
void sec_ts_unlocked_release_all_finger(struct sec_ts_data *ts)
{
	int i;
	s64 ms_delta;

	for (i = 0; i < MAX_SUPPORT_TOUCH_COUNT; i++) {
		input_mt_slot(ts->input_dev, i);
		if (ts->plat_data->support_mt_pressure)
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER,
					   false);

		if ((ts->coord[i].action == SEC_TS_COORDINATE_ACTION_PRESS) ||
			(ts->coord[i].action == SEC_TS_COORDINATE_ACTION_MOVE)) {
			LOGI("[RA] tID: %d mc: %d tc: %u v: %02X%02X\n",
				i,
				ts->coord[i].mcount, ts->touch_count,
				ts->plat_data->img_version_of_ic[2],
				ts->plat_data->img_version_of_ic[3]);

			ts->coord[i].ktime_released = ktime_get();
			ms_delta = ktime_ms_delta(ts->coord[i].ktime_released,
						ts->coord[i].ktime_pressed);
			if (ts->longest_duration < ms_delta)
				ts->longest_duration = ms_delta;
		}

		ts->coord[i].action = SEC_TS_COORDINATE_ACTION_RELEASE;
		ts->coord[i].mcount = 0;
	}

	input_mt_slot(ts->input_dev, 0);

	input_report_key(ts->input_dev, BTN_TOUCH, false);
	input_report_key(ts->input_dev, BTN_TOOL_FINGER, false);

	ts->touch_count = 0;
	ts->check_multi = 0;

#ifdef KEY_SIDE_GESTURE
	if (ts->plat_data->support_sidegesture) {
		input_report_key(ts->input_dev, KEY_SIDE_GESTURE, 0);
		input_report_key(ts->input_dev, KEY_SIDE_GESTURE_LEFT, 0);
		input_report_key(ts->input_dev, KEY_SIDE_GESTURE_RIGHT, 0);
	}
#endif
	input_report_key(ts->input_dev, KEY_HOMEPAGE, 0);
	input_sync(ts->input_dev);
}

void sec_ts_locked_release_all_finger(struct sec_ts_data *ts)
{
	mutex_lock(&ts->eventlock);
	sec_ts_unlocked_release_all_finger(ts);
	mutex_unlock(&ts->eventlock);
}
#endif

#ifdef USE_POWER_RESET_WORK
static void sec_ts_reset_work(struct work_struct *work)
{
	struct sec_ts_data *ts = container_of(work, struct sec_ts_data,
							reset_work.work);

	ts->reset_is_on_going = true;
	LOGI("\n");

	sec_ts_stop_device(ts);

	sec_ts_delay(30);

	sec_ts_start_device(ts);

	ts->reset_is_on_going = false;
}
#endif

static void sec_ts_fw_update_work(struct work_struct *work)
{
#ifdef SEC_TS_FW_UPDATE_ON_PROBE
	struct sec_ts_data *ts = container_of(work, struct sec_ts_data,
					      fw_update_work);
#else
	struct delayed_work *fw_update_work = container_of(work,
					struct delayed_work, work);
	struct sec_ts_data *ts = container_of(fw_update_work,
					struct sec_ts_data, fw_update_work);
#endif

	int ret;

	LOGI("start firmware update after probe.\n");

	ret = sec_ts_firmware_update_on_probe(ts, false);
	if (ret < 0)
		LOGI("firmware update was unsuccessful.\n");

	if (ts->is_fw_corrupted == true && ret == 0) {
		ret = sec_ts_fw_init(ts);
		if (ret == SEC_TS_ERR_NA) {
			ts->is_fw_corrupted = false;
			sec_ts_device_init(ts);
		} else {
			LOGI("Failed to sec_ts_fw_init 0x%x\n", ret);
		}
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_gti_probe(ts);
#endif
}

int sec_ts_set_lowpowermode(struct sec_ts_data *ts, u8 mode)
{
	int ret;
	int retrycnt = 0;
	char para = 0;

	LOGE("%s(%X)\n", mode == TO_LOWPOWER_MODE ? "ENTER" : "EXIT", ts->lowpower_mode);

retry_pmode:
	ret = sec_ts_write(ts, SEC_TS_CMD_POWER_MODE, &mode, 1);
	if (ret < 0)
		LOGE("failed\n");
	sec_ts_delay(50);

	/* read data */

	ret = sec_ts_read(ts, SEC_TS_CMD_POWER_MODE, &para, 1);
	if (ret < 0)
		LOGE("read power mode failed!\n");
	else
		LOGI("power mode - write(%d) read(%d)\n", mode, para);

	if (mode != para) {
		retrycnt++;
		if (retrycnt < 5)
			goto retry_pmode;
	}

	ret = sec_ts_write(ts, SEC_TS_CMD_CLEAR_EVENT_STACK, NULL, 0);
	if (ret < 0)
		LOGE("write clear event failed\n");

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif
	if (device_may_wakeup(&ts->client->dev)) {
		if (mode)
			enable_irq_wake(ts->plat_data->irq);
		else
			disable_irq_wake(ts->plat_data->irq);
	}

	ts->lowpower_status = mode;
	LOGI("end\n");

	return ret;
}

#ifdef USE_OPEN_CLOSE
static int sec_ts_input_open(struct input_dev *dev)
{
	struct sec_ts_data *ts = input_get_drvdata(dev);
	int ret;

	ts->input_closed = false;

	LOGI("\n");

	if (ts->lowpower_status) {
#ifdef USE_RESET_EXIT_LPM
		schedule_delayed_work(&ts->reset_work,
				      msecs_to_jiffies(TOUCH_RESET_DWORK_TIME));
#else
		sec_ts_set_lowpowermode(ts, TO_TOUCH_MODE);
#endif
		ts->power_status = SEC_TS_STATE_POWER_ON;
	} else {
		ret = sec_ts_start_device(ts);
		if (ret < 0)
			LOGE("Failed to start device\n");
	}

	/* because edge and dead zone will recover soon */
	sec_ts_set_grip_type(ts, ONLY_EDGE_HANDLER);

	return 0;
}

static void sec_ts_input_close(struct input_dev *dev)
{
	struct sec_ts_data *ts = input_get_drvdata(dev);

	ts->input_closed = true;

	LOGI("\n");

#ifdef USE_POWER_RESET_WORK
	cancel_delayed_work(&ts->reset_work);
#endif

	if (ts->lowpower_mode) {
		sec_ts_set_lowpowermode(ts, TO_LOWPOWER_MODE);
		ts->power_status = SEC_TS_STATE_LPM;
	} else {
		sec_ts_stop_device(ts);
	}
}
#endif

#if defined(I3C_INTERFACE)
static void sec_ts_remove(struct i3c_device *client)
#elif defined(I2C_INTERFACE)
static void sec_ts_remove(struct i2c_client *client)
#else
static void sec_ts_remove(struct spi_device *client)
#endif
{
#if defined(I3C_INTERFACE)
	struct sec_ts_data *ts = dev_get_drvdata(&client->dev); //i3cdev_get_drvdata(client);
#elif defined(I2C_INTERFACE)
	struct sec_ts_data *ts = i2c_get_clientdata(client);
#else
	struct sec_ts_data *ts = spi_get_drvdata(client);
#endif

	LOGI("\n");

	if (ts == NULL || ts->probe_done == false)
		return;

	destroy_workqueue(ts->event_wq);

#ifdef SEC_TS_FW_UPDATE_ON_PROBE
	cancel_work_sync(&ts->fw_update_work);
#else
	cancel_delayed_work_sync(&ts->fw_update_work);
	destroy_workqueue(ts->fw_update_wq);
#endif

	sec_disable_irq_nosync(ts);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (ts->gti)
		goog_devm_free_irq(ts->gti, &ts->client->dev, ts->plat_data->irq);
	else
#endif
	free_irq(ts->plat_data->irq, ts);

	LOGI("irq disabled\n");

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	goog_pm_unregister_notification(ts->gti);
	goog_touch_interface_disconnect(&ts->client->dev);
	ts->gti = NULL;
#endif

#ifdef USE_POWER_RESET_WORK
	cancel_delayed_work_sync(&ts->reset_work);
	flush_delayed_work(&ts->reset_work);

	LOGI("flush queue\n");

#endif

	sec_ts_fn_remove(ts);

#ifdef CONFIG_TOUCHSCREEN_DUMP_MODE
	p_ghost_check = NULL;
#endif
	device_init_wakeup(&client->dev, false);

	ts->lowpower_mode = false;
	ts->probe_done = false;
	input_mt_destroy_slots(ts->input_dev);
	input_unregister_device(ts->input_dev);

	ts->input_dev = NULL;

	/* need to do software reset for next sec_ts_probe() without error */
	ts->sec_ts_write(ts, SEC_TS_CMD_SW_RESET, NULL, 0);

	if (gpio_is_valid(ts->plat_data->irq_gpio))
		gpio_free(ts->plat_data->irq_gpio);
	if (gpio_is_valid(ts->plat_data->reset_gpio))
		gpio_free(ts->plat_data->reset_gpio);

	sec_ts_raw_device_exit(ts);
#ifndef CONFIG_SEC_SYSFS
	class_destroy(sec_class);
#endif
}

int sec_ts_stop_device(struct sec_ts_data *ts)
{
	LOGI("\n");

	mutex_lock(&ts->device_mutex);

	if (ts->power_status == SEC_TS_STATE_POWER_OFF) {
		LOGE("already power off\n");
		goto out;
	}

	ts->power_status = SEC_TS_STATE_POWER_OFF;

	ts->sec_irq_enable(ts, false);
#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif

	sec_ts_pinctrl_configure(ts, false);

out:
	mutex_unlock(&ts->device_mutex);
	return 0;
}

int sec_ts_start_device(struct sec_ts_data *ts)
{
	int ret;
#if defined(I3C_INTERFACE)
	struct i3c_master_controller *i3cmaster;
#endif

	LOGI("\n");

	sec_ts_pinctrl_configure(ts, true);

	mutex_lock(&ts->device_mutex);

	if (ts->power_status == SEC_TS_STATE_POWER_ON) {
		LOGI("already power on\n");
		goto out;
	}

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif

	sec_ts_delay(70);
	ts->power_status = SEC_TS_STATE_POWER_ON;

#if defined(I3C_INTERFACE)
	LOGE("power on and do i3c daa\n");
	i3cmaster = i3c_dev_get_master(ts->client->desc);
	i3c_master_do_daa(i3cmaster);
#endif

	sec_ts_wait_for_ready(ts, SEC_TS_ACK_BOOT_COMPLETE,
			SEC_TS_BOOT_COMPLETE_TIME_MS);

	if (ts->flip_enable) {
		ret = sec_ts_write(ts, SET_TS_CMD_SET_COVER_TYPE,
				   &ts->cover_cmd, 1);

		ts->touch_functions = ts->touch_functions |
				SEC_TS_BIT_SETFUNC_COVER;
		LOGI("cover cmd write type: %d, mode: %x, ret: %d", ts->touch_functions,
			ts->cover_cmd, ret);
	} else {
		ts->touch_functions = (ts->touch_functions &
				       (~SEC_TS_BIT_SETFUNC_COVER));
		LOGI("cover open, not send cmd");
	}

	ts->touch_functions = ts->touch_functions |
				SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC;
	ret = sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION,
			   (u8 *)&ts->touch_functions, 2);
	if (ret < 0)
		LOGE("Failed to send touch function command");

	sec_ts_set_grip_type(ts, ONLY_EDGE_HANDLER);

	/* SENSE_ON */
	ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
	if (ret < 0)
		LOGE("Failed to write SENSE_ON\n");

	ts->sec_irq_enable(ts, true);

out:
	mutex_unlock(&ts->device_mutex);
	return 0;
}

static int sec_ts_suspend(struct device *dev)
{
	struct sec_ts_data *ts = dev_get_drvdata(dev);
	int ret = 0;

	if (ts->power_status == SEC_TS_STATE_SUSPEND) {
		LOGE("already suspended.\n");
		return 0;
	}

	mutex_lock(&ts->device_mutex);

	ts->sec_disable_irq_nosync(ts);

	/*
	 * Do the system reset to initialize the FW to the default state
	 * before handing over to AOC.
	 */
	sec_ts_system_reset(ts, RESET_MODE_AUTO, true, false);

	/* Stop T-IC */
	sec_ts_set_power_mode(ts, TO_SLEEP_MODE);
	ret = sec_ts_write(ts, SEC_TS_CMD_CLEAR_EVENT_STACK, NULL, 0);
	if (ret < 0)
		LOGE("write clear event failed\n");

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif

	ts->power_status = SEC_TS_STATE_SUSPEND;

	sec_ts_pinctrl_configure(ts, false);

	mutex_unlock(&ts->device_mutex);

	return ret;
}

static int sec_ts_resume(struct device *dev)
{
	struct sec_ts_data *ts = dev_get_drvdata(dev);
	u8 touch_mode[2] = {0};
	int ret = 0;

	ts->comm_err_count = 0;
	ts->longest_duration = 0;
	ts->wet_count = 0;

	mutex_lock(&ts->device_mutex);

	sec_ts_pinctrl_configure(ts, true);

	if (ts->power_status == SEC_TS_STATE_POWER_ON) {
		LOGE("already resumed.\n");
		mutex_unlock(&ts->device_mutex);
		return 0;
	}

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	sec_ts_locked_release_all_finger(ts);
#endif

	ts->power_status = SEC_TS_STATE_POWER_ON;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, touch_mode, sizeof(touch_mode));
	if (ret < 0) {
		LOGE("read touch mode failed(%d)\n", ret);
		ret = sec_ts_system_reset(ts, RESET_MODE_HW, false, false);
		if (ret < 0) {
			LOGE("reset failed! ret %d\n", ret);
		}
	} else {
		LOGI("before resume: mode %#x, state %#x.\n", touch_mode[0], touch_mode[1]);
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	ts->raw_timestamp_sensing = 0;
	goog_notify_fw_status_changed(ts->gti, GTI_FW_STATUS_RESET, NULL);
#endif

	ts->touch_functions = ts->touch_functions | SEC_TS_DEFAULT_ENABLE_BIT_SETFUNC;
	ret = sec_ts_write(ts, SEC_TS_CMD_TOUCH_FUNCTION, (u8 *)&ts->touch_functions, 2);
	if (ret < 0)
		LOGE("Failed to send touch function command.");

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (!goog_check_late_sense_on_enabled(ts->gti)) {
#endif
	/* SENSE_ON */
	ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
	if (ret < 0)
		LOGE("failed to write SENSE_ON\n");
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	}
#endif

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_SYSTEM_MODE, touch_mode, sizeof(touch_mode));
	if (ret < 0)
		LOGE("read touch mode failed(%d)\n", ret);
	else
		LOGI("after resume: mode %#x, state %#x.\n", touch_mode[0], touch_mode[1]);

	if (touch_mode[0] != TOUCH_SYSTEM_MODE_TOUCH) {
		LOGE("Force to recover the unexpected system mode %#x\n", touch_mode[0]);
		sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
	}

	ts->sec_irq_enable(ts, true);

	mutex_unlock(&ts->device_mutex);

	return ret;
}

#if IS_ENABLED(CONFIG_PM) || IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
const struct dev_pm_ops sec_ts_dev_pm_ops = {
	.suspend = sec_ts_suspend,
	.resume = sec_ts_resume,
};
#endif

static void sec_ts_reset_handler_work(struct work_struct *work)
{
	struct sec_ts_data *ts = container_of(work, struct sec_ts_data, reset_handler_work);

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	ts->raw_timestamp_sensing = 0;
	goog_notify_fw_status_changed(ts->gti, GTI_FW_STATUS_RESET, NULL);
#else
	sec_ts_locked_release_all_finger(ts);
#endif
	sec_ts_reinit(ts);
}

#ifdef CONFIG_OF
static const struct of_device_id sec_ts_match_table[] = {
	{ .compatible = "sec,sec_ts",},
	{ },
};
#else
#define sec_ts_match_table NULL
#endif

#if defined(I3C_INTERFACE)
static struct i3c_driver sec_ts_driver = {
	.probe		= sec_ts_probe,
	.remove		= sec_ts_remove,
	.id_table	= sec_ts_id,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SEC_TS_NAME,
#ifdef CONFIG_OF
		.of_match_table = sec_ts_match_table,
#endif
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm = &sec_ts_dev_pm_ops,
#endif
	},
};
#elif defined(I2C_INTERFACE)
static struct i2c_driver sec_ts_driver = {
	.probe		= sec_ts_probe,
	.remove		= sec_ts_remove,
	.id_table	= sec_ts_id,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= SEC_TS_NAME,
#ifdef CONFIG_OF
		.of_match_table = sec_ts_match_table,
#endif
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm = &sec_ts_dev_pm_ops,
#endif
	},
};
#else
static struct spi_driver sec_ts_driver = {
	.probe    = sec_ts_probe,
	.remove   = sec_ts_remove,
	.driver   = {
		.owner  = THIS_MODULE,
		.name = SEC_TS_NAME,
#ifdef CONFIG_OF
		.of_match_table = sec_ts_match_table,
#endif
#if IS_ENABLED(CONFIG_PM) && !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		.pm = &sec_ts_dev_pm_ops,
#endif
	},
};
#endif


static int __init sec_ts_init(void)
{
	int ret = 0;

	LOGI("\n");
#if defined(I3C_INTERFACE)
	ret = i3c_driver_register(&sec_ts_driver);
	LOGI("ret %d\n", ret);
#elif defined(I2C_INTERFACE)
	ret = i2c_add_driver(&sec_ts_driver);
#else
	ret = spi_register_driver(&sec_ts_driver);
#endif
	return ret;
}

static void __exit sec_ts_exit(void)
{
	LOGI("\n");
#if defined(I3C_INTERFACE)
	i3c_driver_unregister(&sec_ts_driver);
#elif defined(I2C_INTERFACE)
	i2c_del_driver(&sec_ts_driver);
#else
	spi_unregister_driver(&sec_ts_driver);
#endif
}

MODULE_AUTHOR("Hyobae, Ahn<hyobae.ahn@samsung.com>");
MODULE_DESCRIPTION("Samsung Electronics TouchScreen driver");
MODULE_LICENSE("GPL");

module_init(sec_ts_init);
module_exit(sec_ts_exit);
