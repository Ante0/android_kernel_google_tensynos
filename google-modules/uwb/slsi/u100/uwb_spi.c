// SPDX-License-Identifier: GPL-2.0
/**
 * @copyright Copyright (c) 2023 Samsung Electronics Co., Ltd
 *
 */

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/errno.h>

#include "include/uwb_spi.h"

#define SPI_SPEED_FLASH_HZ 5000000
#define SPI_SPEED_UCI_HZ 5000000
#define SPI_SLOW_TX_MS 1
#define LINE_BUFF_SIZE 140

static int dump_spi_payload;
module_param(dump_spi_payload, int, 0664);
MODULE_PARM_DESC(dump_spi_payload, "Logs the U100 spi payload in the kernel messages");

static int spi_speed_flash = SPI_SPEED_FLASH_HZ;
module_param(spi_speed_flash, int, 0664);
MODULE_PARM_DESC(spi_speed_flash, "The spi frequency when downloading firmware");

static int spi_speed_uci = SPI_SPEED_UCI_HZ;
module_param(spi_speed_uci, int, 0664);
MODULE_PARM_DESC(spi_speed_uci, "The spi frequency when reading/sending UCI data");

static uint spi_slow_tx_ms = SPI_SLOW_TX_MS;
module_param(spi_slow_tx_ms, uint, 0664);
MODULE_PARM_DESC(spi_slow_tx_ms, "The leeway for the slow SPI transaction threshold (in ms)");

static int spi_speed = SPI_SPEED_UCI_HZ;

static void uwb_spi_data(char *buf, unsigned int len)
{
	const u8 *ptr = buf;
	int i, linelen, remaining = len;
	unsigned char linebuf[LINE_BUFF_SIZE];
	int rowsize = 16;
	int groupsize = 1;

	for (i = 0; i < len; i += rowsize) {
		linelen = min(remaining, rowsize);
		remaining -= rowsize;

		hex_dump_to_buffer(ptr + i, linelen, rowsize, groupsize,
				linebuf, sizeof(linebuf), 0);
		if (dump_spi_payload)
			UWB_INFO("%s\n", linebuf);
	}
}

void uwb_set_spi_type(enum uwb_spi_type spi_type)
{
	if (spi_type == SPI_TYPE_FLASH)
		uwb_set_spi_speed(spi_speed_flash);
	else
		uwb_set_spi_speed(spi_speed_uci);
}

void uwb_set_spi_speed(int speed)
{
	UWB_DEBUG("SPI speed changes from %d to %d", spi_speed, speed);
	spi_speed = speed;
}

int uwb_get_spi_speed(void)
{
	return spi_speed;
}

int uwb_spi_send(struct u100_ctx *u100_ctx, char *buff, unsigned int size, char *recv_buff)
{
	int ret;
	struct spi_message msg;
	struct spi_transfer tx;
	u64 start, end, elapsed_time;
	uint spi_slow_tx_limit = 0;

	if (dump_spi_payload) {
		UWB_INFO("Send len: %d, data:\n", size);
		uwb_spi_data(buff, size);
	}
	mutex_lock(&u100_ctx->lock);

	memset(&tx, 0, sizeof(struct spi_transfer));
	spi_message_init(&msg);
	tx.len = size;
	tx.tx_buf = buff;
	if (recv_buff)
		tx.rx_buf = recv_buff;
	tx.speed_hz = spi_speed;

	spi_message_add_tail(&tx, &msg);

	/*
	 * 8 bits/byte * 1000 ms/s * (1/spi_speed_uci) s/bit = ms/byte
	 * size bytes * (8000 / spi_speed_uci) ms/byte = total ms required
	 */
	if (spi_speed_uci)
		spi_slow_tx_limit = (8000 * size) / spi_speed_uci;

	start = ktime_get_coarse_ns();
	ret = spi_sync(u100_ctx->spi, &msg);
	end = ktime_get_coarse_ns();

	if (ret < 0)
		UWB_ERR("Failed to send by spi_sync(), error:%d\n", ret);

	elapsed_time = div_u64(end - start, 1000000);

	if (elapsed_time > (spi_slow_tx_limit + spi_slow_tx_ms))
		atomic_inc(&u100_ctx->num_spi_slow_txs);
	mutex_unlock(&u100_ctx->lock);

	if (ret < 0)
		usleep_range(5000, 10000);

	return ret;
}

int uwb_spi_recv(struct u100_ctx *u100_ctx, char *buff, unsigned int size)
{
	int ret = 0;
	struct spi_message msg;
	struct spi_transfer rx = {0};
	u64 start, end, elapsed_time;
	uint spi_slow_tx_limit = 0;

	mutex_lock(&u100_ctx->lock);

	rx.len = size;
	rx.rx_buf = buff;
	rx.bits_per_word = 8;
	rx.speed_hz = spi_speed;

	rx.tx_buf = kmalloc(size, GFP_KERNEL);
	if (!rx.tx_buf) {
		mutex_unlock(&u100_ctx->lock);
		return -ENOMEM;
	}
	memset((void *)rx.tx_buf, 0xff, size);

	spi_message_init(&msg);
	spi_message_add_tail(&rx, &msg);

	/*
	 * 8 bits/byte * 1000 ms/s * (1/spi_speed_uci) s/bit = ms/byte
	 * size bytes * (8000 / spi_speed_uci) ms/byte = total ms required
	 */
	if (spi_speed_uci)
		spi_slow_tx_limit = (8000 * size) / spi_speed_uci;

	start = ktime_get_coarse_ns();
	ret = spi_sync(u100_ctx->spi, &msg);
	end = ktime_get_coarse_ns();

	if (ret < 0)
		UWB_ERR("Failed to read by spi_sync(), error:%d\n", ret);

	elapsed_time = div_u64(end - start, 1000000);

	if (elapsed_time > (spi_slow_tx_limit + spi_slow_tx_ms))
		atomic_inc(&u100_ctx->num_spi_slow_txs);
	mutex_unlock(&u100_ctx->lock);
	if (ret == 0 && dump_spi_payload) {
		UWB_INFO("Receive len: %d, data:\n", size);
		uwb_spi_data(buff, size);
	}

	kfree(rx.tx_buf);

	if (ret < 0)
		usleep_range(5000, 10000);

	return ret;
}
