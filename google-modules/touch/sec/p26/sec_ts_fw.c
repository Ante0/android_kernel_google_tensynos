/* drivers/input/touchscreen/sec_ts_fw.c
 *
 * Copyright (C) 2015 Samsung Electronics Co., Ltd.
 * http://www.samsungsemi.com/
 *
 * Core file for Samsung TSC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "sec_ts.h"

#define SEC_TS_ENABLE_FW_VERIFY		0
#define SEC_TS_FW_BLK_SIZE		256

enum {
	BUILT_IN = 0,
	UMS, /* deprecated */
	BL, /* deprecated */
	FFU, /* deprecated */
};

typedef struct {
	u32 signature;			/* signature */
	u32 version;			/* version */
	u32 totalsize;			/* total size */
	u32 checksum;			/* checksum */
	u32 img_ver;			/* image file version */
	u32 img_date;			/* image file date */
	u32 img_description;		/* image file description */
	u32 fw_ver;			/* firmware version */
	u32 fw_date;			/* firmware date */
	u32 fw_description;		/* firmware description */
	u32 para_ver;			/* parameter version */
	u32 para_date;			/* parameter date */
	u32 para_description;		/* parameter description */
	u32 num_chunk;			/* number of chunk */
	u32 reserved1;
	u32 reserved2;
} fw_header;

typedef struct {
	u32 signature;
	u32 addr;
	u32 size;
	u32 reserved;
} fw_chunk;

static int sec_ts_enter_boot_mode(struct sec_ts_data *ts)
{
	int ret;
	u8 fw_update_mode_passwd[] = {0x55, 0xAB};
	u8 fw_status;
	u8 id[6];

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_ENTER_BOOT_MODE,
			fw_update_mode_passwd, sizeof(fw_update_mode_passwd));
	sec_ts_delay(20);
	if (ret < 0) {
		LOGE("write fail, enter_boot_mode\n");
		return 0;
	}

	LOGI("write ok, enter_boot_mode - 0x%x 0x%x 0x%x\n",
		SEC_TS_CMD_ENTER_BOOT_MODE, fw_update_mode_passwd[0],
		fw_update_mode_passwd[1]);

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_ID, id, 6);
	if (ret < 0) {
		LOGE("failed to read device ID(%d)\n", ret);
		return 0;
	}
	fw_status = id[0] & 0xF0;
	if (fw_status != SEC_TS_STATUS_BOOT_MODE) {
		LOGE("enter fail! read_boot_status = 0x%x\n", fw_status);
		return 0;
	}

	LOGI("Success! read_boot_status = 0x%x\n", fw_status);

	ts->boot_ver[0] = id[0];
	ts->boot_ver[1] = id[1];
	ts->boot_ver[2] = id[2];
	ts->boot_ver[3] = id[3];
	ts->boot_ver[4] = id[4];
	ts->boot_ver[5] = id[5];

	ts->flash_page_size = SEC_TS_FW_BLK_SIZE;

	LOGI("read_boot_id %02X %02X %02X %02X %02X %02X\n",
		id[0], id[1], id[2], id[3], id[4], id[5]);

	return 1;
}

static int sec_ts_wait_for_reset_done(struct sec_ts_data *ts)
{
	int ret = 0;

	if (atomic_read(&ts->irq_enabled) == 1) {
		if (wait_for_completion_timeout(&ts->boot_completed, msecs_to_jiffies(200)) == 0)
			ret = -ETIME;
	} else {
		ret = sec_ts_wait_for_ready(ts, SEC_TS_ACK_BOOT_COMPLETE,
				SEC_TS_BOOT_COMPLETE_TIME_MS);
	}

	return ret;
}

int sec_ts_hw_reset(struct sec_ts_data *ts, bool wait_for_done)
{
	int ret = 0;
	int reset_gpio = ts->plat_data->reset_gpio;
#if defined(I3C_INTERFACE)
	struct i3c_master_controller *i3cmaster;
#endif

	LOGI("wait_for_done %d.\n", wait_for_done);
	if (wait_for_done)
		reinit_completion(&ts->boot_completed);

	if (!gpio_is_valid(reset_gpio)) {
		LOGE("invalid gpio %d.\n", reset_gpio);
		return -EINVAL;
	}

	gpio_set_value(reset_gpio, 0);
	sec_ts_delay(10);
	gpio_set_value(reset_gpio, 1);

	/* Wait 70 ms at least from bootloader to applicateion mode. */
	sec_ts_delay(70);

#if defined(I3C_INTERFACE)
	LOGE("hw_reset and do i3c daa\n");
	i3cmaster = i3c_dev_get_master(ts->client->desc);
	i3c_master_do_daa(i3cmaster);
#endif

	if (wait_for_done) {
		ret = sec_ts_wait_for_reset_done(ts);
		if (!ret)
			LOGI("done.\n");
		else
			LOGE("hw_reset time out!\n");
		complete_all(&ts->boot_completed);
	}

	return ret;
}

int sec_ts_sw_reset(struct sec_ts_data *ts, bool wait_for_done)
{
	int ret = 0;
#if defined(I3C_INTERFACE)
	struct i3c_master_controller *i3cmaster;
#endif

	LOGI("wait_for_done %d.\n", wait_for_done);
	if (wait_for_done)
		reinit_completion(&ts->boot_completed);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_SW_RESET, NULL, 0);
	if (ret < 0) {
		LOGE("failed to write sw_reset.\n");
		return -EIO;
	}

	/* Wait 70 ms at least from bootloader to applicateion mode. */
	sec_ts_delay(70);

#if defined(I3C_INTERFACE)
	LOGE("sw_reset and do i3c daa\n");
	i3cmaster = i3c_dev_get_master(ts->client->desc);
	i3c_master_do_daa(i3cmaster);
#endif

	if (wait_for_done) {
		ret = sec_ts_wait_for_reset_done(ts);
		if (!ret)
			LOGI("done.\n");
		else
			LOGE("sw_reset time out!\n");

		complete_all(&ts->boot_completed);
	}

	return ret;
}

int sec_ts_system_reset(struct sec_ts_data *ts,
			enum RESET_MODE mode,
			bool wait_for_done,
			bool sense_on)
{
	int ret = 0;

	LOGI("mode %d, wait_for_done %d, sense_on %d.\n", mode, wait_for_done, sense_on);

	if (mode & RESET_MODE_SW) {
		ret = sec_ts_sw_reset(ts, wait_for_done);
		if (ret)
			LOGE("sw reset failed.");
		else
			goto sw_reset_done;
	}

	if (mode & RESET_MODE_HW) {
		if (ret)
			LOGE("sw_reset failed or time out, try hw_reset to recover!\n");
		ret = sec_ts_hw_reset(ts, wait_for_done);
		if (ret)
			LOGE("hw reset failed.");
	}

sw_reset_done:
	/* Sense on. */
	if (sense_on) {
		/* SENSE_ON */
		ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
		if (ret < 0) {
			LOGE("failed to write sense_on.\n");
		}
	}

	/* Initialize wet status. */
	ts->wet_mode = 0;
	ts->wet_count = 0;

	return ret;
}

static void sec_ts_save_version_of_bin(struct sec_ts_data *ts,
				       const fw_header *fw_hd)
{
	ts->plat_data->img_version_of_bin[3] =
			((fw_hd->img_ver >> 24) & 0xff);
	ts->plat_data->img_version_of_bin[2] =
			((fw_hd->img_ver >> 16) & 0xff);
	ts->plat_data->img_version_of_bin[1] =
			((fw_hd->img_ver >> 8) & 0xff);
	ts->plat_data->img_version_of_bin[0] =
			((fw_hd->img_ver >> 0) & 0xff);

	ts->plat_data->core_version_of_bin[3] =
			((fw_hd->fw_ver >> 24) & 0xff);
	ts->plat_data->core_version_of_bin[2] =
			((fw_hd->fw_ver >> 16) & 0xff);
	ts->plat_data->core_version_of_bin[1] =
			((fw_hd->fw_ver >> 8) & 0xff);
	ts->plat_data->core_version_of_bin[0] =
			((fw_hd->fw_ver >> 0) & 0xff);

	ts->plat_data->config_version_of_bin[3] =
			((fw_hd->para_ver >> 24) & 0xff);
	ts->plat_data->config_version_of_bin[2] =
			((fw_hd->para_ver >> 16) & 0xff);
	ts->plat_data->config_version_of_bin[1] =
			((fw_hd->para_ver >> 8) & 0xff);
	ts->plat_data->config_version_of_bin[0] =
			((fw_hd->para_ver >> 0) & 0xff);

	LOGI("img_ver of bin: %x.%x.%x.%x\n",
			ts->plat_data->img_version_of_bin[0],
			ts->plat_data->img_version_of_bin[1],
			ts->plat_data->img_version_of_bin[2],
			ts->plat_data->img_version_of_bin[3]);

	LOGI("core_ver of bin: %x.%x.%x.%x\n",
			ts->plat_data->core_version_of_bin[0],
			ts->plat_data->core_version_of_bin[1],
			ts->plat_data->core_version_of_bin[2],
			ts->plat_data->core_version_of_bin[3]);

	LOGI("config_ver of bin: %x.%x.%x.%x\n",
			ts->plat_data->config_version_of_bin[0],
			ts->plat_data->config_version_of_bin[1],
			ts->plat_data->config_version_of_bin[2],
			ts->plat_data->config_version_of_bin[3]);
}

static int sec_ts_save_version_of_ic(struct sec_ts_data *ts)
{
	u8 img_ver[4] = {0,};
	int ret;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_GET_FW_VERSION, img_ver, 4);
	if (ret < 0) {
		LOGE("Image version read error\n");
		return -EIO;
	}
	LOGI("IC Image version info: %x.%x.%x.%x\n",
		img_ver[0], img_ver[1], img_ver[2], img_ver[3]);

	ts->plat_data->img_version_of_ic[0] = img_ver[0];
	ts->plat_data->img_version_of_ic[1] = img_ver[1];
	ts->plat_data->img_version_of_ic[2] = img_ver[2];
	ts->plat_data->img_version_of_ic[3] = img_ver[3];

	return 1;
}

static int sec_ts_check_firmware_version(struct sec_ts_data *ts,
					 const u8 *fw_info)
{
	fw_header *fw_hd;
	u8 id[6];
	u8 boot_mode;
	int i;
	int ret;
	/*
	 * sec_ts_check_firmware_version
	 * return value = 1 : firmware download needed,
	 * return value = 0 : skip firmware download
	 */

	fw_hd = (fw_header *)fw_info;

	sec_ts_save_version_of_bin(ts, fw_hd);

	/* firmware download if BOOT_STATUS */
	ret = ts->sec_ts_read(ts, SEC_TS_CMD_READ_ID, id, 6);
	if (ret < 0) {
		LOGE("fail to read Device ID\n");
		return -EIO;
	}

	boot_mode = id[0] & 0xF0;
	if (boot_mode == SEC_TS_STATUS_BOOT_MODE) {
		LOGE("ReadBootStatus = 0x%x, Firmware download Start!\n", id[0]);
		return 1;
	}

	ret = sec_ts_save_version_of_ic(ts);
	if (ret < 0) {
		LOGE("fail to read ic version\n");
		return -EIO;
	}

	/* check f/w version
	 * ver[0] : IC version
	 * ver[1] : Project version
	 */
	for (i = 0; i < 2; i++) {
		if (ts->plat_data->img_version_of_ic[i] !=
			ts->plat_data->img_version_of_bin[i]) {
			LOGE("do not matched ic/project version info\n");
			return 0;
		}
	}

	/* check f/w version
	 * ver[2] : f/w major version
	 * ver[3] : f/w minor version
	 */
	for (i = 2; i < 4; i++) {
		if (ts->plat_data->img_version_of_ic[i] !=
		    ts->plat_data->img_version_of_bin[i])
			return 1;
	}

	return 0;
}

#if SEC_TS_ENABLE_FW_VERIFY
static int sec_ts_memoryblockread(struct sec_ts_data *ts, u32 mem_addr,
				int mem_size, u8 *buf)
{
	int ret;
	u8 wdata[6];
	u8 *data;

	if (mem_size >= 64 * 1024) {
		LOGE("mem size over 64K\n");
		return -EIO;
	}

	wdata[0] = (u8)((mem_addr >> 24) & 0xff);
	wdata[1] = (u8)((mem_addr >> 16) & 0xff);
	wdata[2] = (u8)((mem_addr >> 8) & 0xff);
	wdata[3] = (u8)((mem_addr >> 0) & 0xff);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_MEMORY_SET_MEM_ADDR, wdata, 4);
	if (ret < 0) {
		LOGE("send command fail, %02X\n", SEC_TS_CMD_MEMORY_SET_MEM_ADDR);
		return -EIO;
	}

	udelay(10);

	wdata[0] = (u8)((mem_size >> 8) & 0xff);
	wdata[1] = (u8)((mem_size >> 0) & 0xff);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_MEMORY_SET_DATA_NUM, wdata, 2);
	if (ret < 0) {
		LOGE("send command fail, %02X\n", SEC_TS_CMD_MEMORY_SET_DATA_NUM);
		return -EIO;
	}

	udelay(10);

	data = buf;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_MEMORY_READ_WRITE_DATA, data, mem_size);
	if (ret < 0) {
		LOGE("memory read fail\n");
		return -EIO;
	}
/*
 *	ret = ts->sec_ts_write(ts, cmd[0], NULL, 0);
 *	ret = ts->sec_ts_read_bulk(ts, data, mem_size);
 **/
	return 0;
}

static int sec_ts_memoryread(struct sec_ts_data *ts, u32 mem_addr,
				u8 *mem_data, u32 mem_size)
{
	int ret;
	int retry = 3;
	int read_size = 0;
	int unit_size;
	int max_size = 256;
	int read_left = (int)mem_size;
	u8 *tmp_data;

	tmp_data = kmalloc(max_size, GFP_KERNEL);
	if (!tmp_data) {
		LOGE("failed to kmalloc\n");
		return -ENOMEM;
	}

	while (read_left > 0) {
		unit_size = (read_left > max_size) ? max_size : read_left;
		retry = 3;
		do {
			ret = sec_ts_memoryblockread(ts, mem_addr, unit_size,
							tmp_data);
			if (retry-- == 0) {
				LOGE("memory read fail mem_addr=%08X,unit_size=%d\n",
					mem_addr, unit_size);
				kfree(tmp_data);
				return -1;
			}

			memcpy(mem_data + read_size, tmp_data, unit_size);
		} while (ret < 0);

		mem_addr += unit_size;
		read_size += unit_size;
		read_left -= unit_size;
	}

	kfree(tmp_data);

	return read_size;
}
#endif

static int sec_ts_memoryblockwrite(struct sec_ts_data *ts, u32 mem_addr,
				int mem_size, u8 *mem_data)
{
	int ret;
	u8 wdata[SEC_TS_FW_BLK_SIZE + 1];

	if (mem_size >= 64 * 1024) {
		LOGE("mem size over 64K\n");
		return -EIO;
	}

	wdata[0] = (u8)((mem_addr >> 24) & 0xff);
	wdata[1] = (u8)((mem_addr >> 16) & 0xff);
	wdata[2] = (u8)((mem_addr >> 8) & 0xff);
	wdata[3] = (u8)((mem_addr >> 0) & 0xff);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_MEMORY_SET_MEM_ADDR, wdata, 4);
	if (ret < 0) {
		LOGE("send command fail, %02X\n", SEC_TS_CMD_MEMORY_SET_MEM_ADDR);
		return -EIO;
	}

	udelay(10);
	wdata[0] = (u8)((mem_size >> 8) & 0xff);
	wdata[1] = (u8)((mem_size >> 0) & 0xff);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_MEMORY_SET_DATA_NUM, wdata, 2);
	if (ret < 0) {
		LOGE("send command fail, %02X\n", SEC_TS_CMD_MEMORY_SET_DATA_NUM);
		return -EIO;
	}

	udelay(10);
	memcpy(&wdata[0], mem_data, mem_size);
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_MEMORY_READ_WRITE_DATA,
			wdata, mem_size);
	if (ret < 0) {
		LOGE("memory write fail\n");
		return -EIO;
	}

	return 0;
}

int sec_ts_memorywrite(struct sec_ts_data *ts, u32 mem_addr,
				u8 *mem_data, u32 mem_size)
{
	int ret;
	int retry = 3;
	int write_size = 0;
	int unit_size;
	int max_size = 256;
	int write_left = (int)mem_size;

	while (write_left > 0) {
		unit_size = (write_left > max_size) ? max_size : write_left;
		retry = 3;
		do {
			ret = sec_ts_memoryblockwrite(ts, mem_addr, unit_size,
							&mem_data[write_size]);
			if (retry-- == 0) {
				LOGE("memory write fail mem_addr=%08X,unit_size=%d\n",
					mem_addr, unit_size);
				return -1;
			}
		} while (ret < 0);

		mem_addr += unit_size;
		write_size += unit_size;
		write_left -= unit_size;
	}

	return write_size;
}


static u8 sec_ts_checksum(u8 *data, int offset_start, int offset_end)
{
	int i;
	u8 checksum = 0;

	for (i = offset_start; i <= offset_end; i++)
		checksum += data[i];

	return checksum;
}

static int sec_ts_flashchipidread(struct sec_ts_data *ts, u8 *id)
{
	int ret;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_FLASH_GET_CHIP_ID,	id, 4);

	return ret;
}

static int sec_ts_flashstatusread(struct sec_ts_data *ts, u8 *status)
{
	int ret;

	ret = ts->sec_ts_read(ts, SEC_TS_CMD_FLASH_GET_STATUS, status, 4);

	return ret;
}

static int sec_ts_flashaddrset(struct sec_ts_data *ts, u32 mem_addr)
{
	int ret;
	u8 wdata[5];

	wdata[0] = (u8)((mem_addr >> 24) & 0xFF);
	wdata[1] = (u8)((mem_addr >> 16) & 0xFF);
	wdata[2] = (u8)((mem_addr >> 8) & 0xFF);
	wdata[3] = (u8)((mem_addr >> 0) & 0xFF);
	wdata[4] = sec_ts_checksum(wdata, 0, 3);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_FLASH_SET_MEM_ADDR,
				wdata, 5);

	return ret;
}

static int sec_ts_flashsizeset(struct sec_ts_data *ts, u32 mem_size)
{
	int ret;
	u8 wdata[3];

	wdata[0] = (u8)((mem_size >> 8) & 0xFF);
	wdata[1] = (u8)((mem_size >> 0) & 0xFF);
	wdata[2] = sec_ts_checksum(wdata, 0, 1);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_FLASH_SET_DATA_NUM,
				wdata, 3);

	return ret;
}

static int sec_ts_flashpagewrite(struct sec_ts_data *ts, u32 mem_size,
				 u8 *mem_data)
{
	int ret;
	u8 wdata[SEC_TS_FW_BLK_SIZE + 4];
	int wsize;

	wsize = ((mem_size + 3) & (~3)) + 4;

	memcpy(&wdata[0], mem_data, mem_size);
	wdata[wsize - 1] = sec_ts_checksum(wdata, 0, (mem_size - 1));

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_FLASH_READ_WRITE_DATA,
				wdata, wsize);

	return ret;
}

static int sec_ts_flashpageerase(struct sec_ts_data *ts, u32 mem_addr,
				 u32 mem_size)
{
	int ret;
	u8 wdata[9];

	wdata[0] = (u8)((mem_addr >> 24) & 0xFF);
	wdata[1] = (u8)((mem_addr >> 16) & 0xFF);
	wdata[2] = (u8)((mem_addr >> 8) & 0xFF);
	wdata[3] = (u8)((mem_addr >> 0) & 0xFF);
	wdata[4] = (u8)((mem_size >> 24) & 0xFF);
	wdata[5] = (u8)((mem_size >> 16) & 0xFF);
	wdata[6] = (u8)((mem_size >> 8) & 0xFF);
	wdata[7] = (u8)((mem_size >> 0) & 0xFF);
	wdata[8] = sec_ts_checksum(wdata, 0, 7);

	ret = ts->sec_ts_write(ts, SEC_TS_CMD_FLASH_ERASE,
				wdata, 9);

	return ret;
}

static int sec_ts_flashprotection(struct sec_ts_data *ts, bool enb)
{
	int ret;
	u8 wdata[3];

	/* MDDI_OSC_ENABLE */
	sec_ts_ddi_osc_on(ts);

	/* Set Flash Protection */
	if (enb == true) {
		wdata[0] = (u8)(0xFF);
		wdata[1] = (u8)(0x5A);
		wdata[2] = (u8)(0x5A);
	} else {
		wdata[0] = (u8)(0xFF);
		wdata[1] = (u8)(0xA5);
		wdata[2] = (u8)(0xA5);
	}
	ret = ts->sec_ts_write(ts, SEC_TS_CMD_FLASH_SET_PROTECTION,
				wdata, 3);

	return ret;
}

static int sec_ts_flasherasedelay(u32 flash_addr, u32 flash_size)
{
	u32 erase_size = (flash_size + (0xfff)) & (0xfffff000);
	u32 next_addr = flash_addr & 0xFFFFF000;
	int ret = 30;

	if (flash_size == 0 || (flash_addr + flash_size > 0x400000))
		return ret;

	while (erase_size > 0) {
		if ((next_addr % 0x10000 == 0) && (erase_size >= 0x10000)) {
			erase_size -= 0x10000;
			next_addr += 0x10000;
			ret += 280;
		}
		else if ((next_addr % 0x8000 == 0) && (erase_size >= 0x8000)) {
			erase_size -= 0x8000;
			next_addr += 0x8000;
			ret += 200;
		} else {
			if (erase_size < 0x1000)
				erase_size = 0;
			else
				erase_size -= 0x1000;
			next_addr += 0x1000;
			ret += 80;
		}
	}

	return ret;
}

static int sec_ts_flashwrite(struct sec_ts_data *ts, u32 mem_addr,
				u32 mem_size, u8 *mem_data)
{
	int ret;
	u32 flash_page_size;
	u32 write_left;
	u32 write_offset;
	u8 page_buf[SEC_TS_FW_BLK_SIZE];
	u8 flash_status[4];

	if (mem_size == 0)
		return 0;

	ret = sec_ts_flashpageerase(ts, mem_addr, mem_size);
	if (ret < 0) {
		LOGE("flash erase fail, mem_addr= %08X, mem_size = %08X\n", mem_addr, mem_size);
		return -EIO;
	}
	sec_ts_delay(sec_ts_flasherasedelay(mem_addr, mem_size));

	flash_page_size = ts->flash_page_size;
	write_left = mem_size;
	write_offset = 0;

	sec_ts_flashaddrset(ts, mem_addr);
	sec_ts_flashsizeset(ts, flash_page_size);

	while (write_left > flash_page_size) {
		memcpy(page_buf, mem_data + write_offset, flash_page_size);
		ret = sec_ts_flashpagewrite(ts, flash_page_size, page_buf);
		if (ret < 0) {
			LOGE("flash write fail, mem_addr+write_offset = %08X\n",
				(mem_addr+write_offset));
			goto err;
		}
		sec_ts_delay(SEC_TS_FW_WRITE_DELAY);

		ret = sec_ts_flashstatusread(ts, flash_status);
		if (ret < 0) {
			LOGE("flash write fail, mem_addr+write_offset = %08X\n",
				(mem_addr+write_offset));
			goto err;
		}
		LOGI("mem_addr+write_offset = %08X, flash status = %02X %02X %02X %02X\n",
			(mem_addr+write_offset),
			flash_status[0], flash_status[1],
			flash_status[2], flash_status[3]);

		write_left -= flash_page_size;
		write_offset += flash_page_size;
	}

	// left data
	if (write_left > 0) {
		sec_ts_flashsizeset(ts, write_left);

		memcpy(page_buf, mem_data + write_offset, write_left);
		ret = sec_ts_flashpagewrite(ts, write_left, page_buf);
		if (ret < 0) {
			LOGE("flash write fail (left), mem_addr+write_offset = %08X\n",
				(mem_addr+write_offset));
			goto err;
		}

		sec_ts_delay(SEC_TS_FW_WRITE_DELAY);
	}

	return mem_size;
err:
	return -EIO;
}

static int sec_ts_chunk_update(struct sec_ts_data *ts, u32 addr,
				u32 size, u8 *data)
{
	u32 fw_size;
	u32 write_size;
	int ret = 0;

	fw_size = size;

	write_size = sec_ts_flashwrite(ts, addr, fw_size, data);
	if (write_size != fw_size) {
		LOGE("fw write failed, write_size %d != fw_size %d\n", write_size, fw_size);
		ret = -1;
		goto err_write_fail;
	}

err_write_fail:
	sec_ts_delay(10);

	return ret;
}

static int sec_ts_firmware_update(struct sec_ts_data *ts, const u8 *data,
			size_t size, int retry)
{
	int i;
	int ret;
	int cmd_retry = 0;
	fw_header *fw_hd;
	fw_chunk *fw_ch;
	u8 fw_status = 0;
	u8 *fd = (u8 *)data;
	u8 tBuff[6];
	u8 flash_chip_id[4];

	if (ts->plat_data->spi_checksum_enable) {
		while (cmd_retry < SEC_TS_IO_RETRY_CNT) {
			LOGI("[%d] Disable checksum for update firmware", cmd_retry);
			ret = sec_ts_spi_checksum_enable(ts, 0);
			if(ret < 0)
				LOGE("Failed to disable checksum.\n");
			else
				break;
			cmd_retry++;
		}
	}

	ret = sec_ts_flashchipidread(ts, flash_chip_id);
	if(ret < 0) {
		LOGE("flash chip id read fail\n");
		return -1;
	}
	LOGI("flash chip id: %02X %02X %02X %02X\n",
			flash_chip_id[0], flash_chip_id[1],
			flash_chip_id[2], flash_chip_id[3]);

	/* Check whether CRC is appended or not.
	 * Enter Firmware Update Mode
	 */
	ret = sec_ts_enter_boot_mode(ts);
	if (ret != 1) {
		LOGE("firmware mode fail\n");
		return -1;
	}

	LOGI("firmware update retry: %d\n", retry);

	fw_hd = (fw_header *)fd;
	fd += sizeof(fw_header);

	if (fw_hd->signature != SEC_TS_FW_HEADER_SIGN) {
		LOGE("firmware header error = %08X\n", fw_hd->signature);
		return -1;
	}

	LOGI("num_chunk: %d\n", fw_hd->num_chunk);

	/* flash unprotection disable */
	sec_ts_flashprotection(ts, false);

	for (i = 0; i < fw_hd->num_chunk; i++) {
		fw_ch = (fw_chunk *)fd;

		LOGI("[%d] 0x%08X, 0x%08X, 0x%08X, 0x%08X\n",
			i, fw_ch->signature, fw_ch->addr,
			fw_ch->size, fw_ch->reserved);

		if (fw_ch->signature != SEC_TS_FW_CHUNK_SIGN) {
			LOGE("firmware chunk error = %08X\n", fw_ch->signature);
			return -1;
		}
		fd += sizeof(fw_chunk);
		ret = sec_ts_chunk_update(ts, fw_ch->addr, fw_ch->size, fd);
		if (ret < 0) {
			LOGE("firmware chunk write failed, addr=%08X, size = %d\n",
				fw_ch->addr, fw_ch->size);
			return -1;
		}
		fd += fw_ch->size;
	}

	/* flash protection enable */
	sec_ts_flashprotection(ts, true);

	/* sw reset */
	sec_ts_system_reset(ts, RESET_MODE_SW, false, false);
	sec_ts_delay(100);

	sec_ts_get_checksum_status(ts);

	/* SENSE_ON */
	ret = sec_ts_set_power_mode(ts, TO_TOUCH_MODE);
	if (ret < 0) {
		LOGE("write fail, Sense_on\n");
		return -EIO;
	}

	if (ts->sec_ts_read(ts, SEC_TS_CMD_READ_ID, tBuff, 6) < 0) {
		LOGE("read device id fail\n");
		return -EIO;
	}

	fw_status = tBuff[0] & 0xF0;
	if (fw_status != SEC_TS_STATUS_APP_MODE) {
		LOGE("fw update sequence done, BUT read_boot_status = 0x%x\n", fw_status);
		return -EIO;
	}

	LOGI("fw update Success! read_boot_status = 0x%x\n", fw_status);

	return 1;
}

int sec_ts_firmware_update_on_probe(struct sec_ts_data *ts, bool force_update)
{
	const struct firmware *fw_entry;
	char fw_path[SEC_TS_MAX_FW_PATH];
	int result = -1;
	int ii = 0;
	int ret = 0;

	if (ts->plat_data->bringup != 0 && ts->is_fw_corrupted == false) {
		LOGE("bringup. do not update\n");
		return 0;
	}

	snprintf(fw_path, SEC_TS_MAX_FW_PATH, "%s",
			ts->plat_data->firmware_name);

	ts->sec_irq_enable(ts, false);
	/* Loading Firmware */
	if (request_firmware(&fw_entry, fw_path, &ts->client->dev) !=  0) {
		LOGE("firmware is not available\n");
		goto err_request_fw;
	}
	LOGI("request firmware done! size = %d\n", (int)fw_entry->size);

	result = sec_ts_check_firmware_version(ts, fw_entry->data);
	/* result <= 0 : skip firmware download or read error
	 * force_update == false : not force update
	 **/
	if ((result <= 0) && (!force_update)) {
		LOGI("skip fw update\n");
		goto err_request_fw;
	}

	LOGI("IC config %x %x, Bin config %x %x\n",
			ts->plat_data->config_version_of_ic[2],
			ts->plat_data->config_version_of_ic[3],
			ts->plat_data->config_version_of_bin[2],
			ts->plat_data->config_version_of_bin[3]);

	for (ii = 0; ii < 3; ii++) {
		ret = sec_ts_firmware_update(ts, fw_entry->data,
					    fw_entry->size, ii);
		if (ret >= 0)
			break;
	}

	if (ret < 0) {
		result = -1;
	} else {
		result = 0;
	}

	sec_ts_save_version_of_ic(ts);

err_request_fw:
	release_firmware(fw_entry);
	ts->sec_irq_enable(ts, true);

	return result;
}

static int sec_ts_load_fw_from_bin(struct sec_ts_data *ts)
{
	const struct firmware *fw_entry;
	char fw_path[SEC_TS_MAX_FW_PATH];
	int error = 0;

	if (ts->plat_data->irq)
		ts->sec_irq_enable(ts, false);

	snprintf(fw_path, SEC_TS_MAX_FW_PATH, "%s",
			ts->plat_data->firmware_name);

	LOGI("initial firmware update  %s\n", fw_path);

	/* Loading Firmware */
	if (request_firmware(&fw_entry, fw_path, &ts->client->dev) !=  0) {
		LOGE("firmware is not available\n");
		error = -1;
		goto err_request_fw;
	}
	LOGI("request firmware done! size = %d\n", (int)fw_entry->size);

	if (sec_ts_firmware_update(ts, fw_entry->data, fw_entry->size, 0) < 0)
		error = -1;
	else
		error = 0;

	sec_ts_save_version_of_ic(ts);

err_request_fw:
	release_firmware(fw_entry);
	if (ts->plat_data->irq)
		ts->sec_irq_enable(ts, true);

	return error;
}

int sec_ts_firmware_update_on_hidden_menu(struct sec_ts_data *ts,
					    int update_type)
{
	int ret = 0;

	/* Factory cmd for firmware update
	 * argument represent what is source of firmware like below.
	 *
	 * 0 : [BUILT_IN] Getting firmware which is for user.
	 * 1 : (deprecated) [UMS] Getting firmware from sd card.
	 * 2 : none
	 * 3 : (deprecated) [FFU] Getting firmware from air.
	 */

	switch (update_type) {
	case BUILT_IN:
		ret = sec_ts_load_fw_from_bin(ts);
		break;
	default:
		LOGE("Not support update_type[%d]\n", update_type);
		break;
	}

#ifdef SEC_TS_SUPPORT_CUSTOMLIB
	sec_ts_check_custom_library(ts);
#endif

	return ret;
}
EXPORT_SYMBOL(sec_ts_firmware_update_on_hidden_menu);

