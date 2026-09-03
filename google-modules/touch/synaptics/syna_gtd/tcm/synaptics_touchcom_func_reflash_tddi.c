// SPDX-License-Identifier: GPL-2.0-only
/*
 * Synaptics TouchComm C library
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
 * @file synaptics_touchcom_func_reflash_tddi.c
 *
 * This file implements the fw reflash related functions for TDDI products.
 */

#include "synaptics_touchcom_func_base.h"
#include "synaptics_touchcom_func_reflash_tddi.h"



/*
 * Common parameters regarding the firmware ihex file
 */

#define BINARY_FILE_MAGIC_VALUE 0xaa55

#define ROMBOOT_FLASH_PAGE_SIZE 256

#define MIN_TDDI_BOOT_CONFIG_SIZE 8

#define TDDI_BOOT_CONFIG_SLOTS 16

/*
 * Parameters being used to the location query
 */
enum flash_data {
	FLASH_LCM_DATA = 1,
	FLASH_OEM_DATA,
	FLASH_PPDT_DATA,
	FLASH_FORCE_CALIB_DATA,
	FLASH_OPEN_SHORT_TUNING_DATA,
};

/*
 * Common parameters for JEDEC flash commands
 */

#define JEDEC_STATUS_CHECK_US_MIN 5000
#define JEDEC_STATUS_CHECK_US_MAX 10000


/** Definitions of a JEDEC flash command */
enum flash_command {
	JEDEC_PAGE_PROGRAM = 0x02,
	JEDEC_READ_STATUS = 0x05,
	JEDEC_WRITE_ENABLE = 0x06,
	JEDEC_CHIP_ERASE = 0xc7,
};

/** Definitions of a flash command */
struct external_flash_param {
	union {
		struct {
			unsigned char byte0;
			unsigned char byte1;
			unsigned char byte2;
		};
		struct {
			unsigned char spi_param;
			unsigned char clk_div;
			unsigned char mode;
		};
	};
	unsigned char read_size[2];
	unsigned char command;
};


/** Main structure for reflash */
struct tcm_tddi_reflash_data_blob {
	bool has_s_chip;
	bool has_other_td_chips;
	/* binary data to write */
	const unsigned char *bin_data;
	unsigned int bin_data_size;
	/* parsed data based on given image file */
	struct ihex_info ihex_info;
	struct image_info image_info;
	/* standard information for flash access */
	unsigned int page_size;
	unsigned int write_block_size;
	unsigned int max_write_payload_size;
	/* temporary buffer during the reflash */
	struct tcm_buffer out;
};


/**
 * @brief   Compare the ID information between device and the image file,
 *          and then determine the area to be updated.
 *
 *          Function will invoke the callback, cb_custom_id_comparison, if the custom
 *          method is registered.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [ in] reflash_data:  data blob for tddi reflash
 *
 * @return
 *    one of the following enumerated values being used to indicate the target to update
 *    in case of success
 *
 *       - 0: UPDATE_TDDI_NONE             no needs to update
 *       - 1: UPDATE_FIRMWARE_AND_CONFIG   update the firmware code area and the
 *                                         associated firmware config area
 *       - 2: UPDATE_CONFIG                update the firmware config area only
 *
 *    otherwise, a negative value.
 */
static int syna_tcm_compare_tddi_image_id_info(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data)
{
	enum update_tddi_area result;
	unsigned int idx;
	unsigned int image_fw_id;
	unsigned int device_fw_id;
	unsigned char *image_config_id;
	unsigned char *device_config_id;
	struct app_config_header *header;
	const unsigned char *app_config_data;
	struct area_block *app_config;

	result = UPDATE_TDDI_NONE;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return UPDATE_TDDI_NONE;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash_data\n");
		return UPDATE_TDDI_NONE;
	}

	app_config = &reflash_data->image_info.data[AREA_APP_CONFIG];

	if (app_config->size < sizeof(struct app_config_header)) {
		LOGE("Invalid application config in image file\n");
		return UPDATE_TDDI_NONE;
	}

	app_config_data = app_config->data;
	header = (struct app_config_header *)app_config_data;

	image_fw_id = syna_pal_le4_to_uint(header->build_id);
	device_fw_id = tcm_dev->packrat_number;

	LOGN("Device firmware ID: %d, image build id: %d\n",
		device_fw_id, image_fw_id);

	image_config_id = header->customer_config_id;
	device_config_id = tcm_dev->app_info.customer_config_id;

	if (tcm_dev->cb_custom_id_comparison) {
		result = tcm_dev->cb_custom_id_comparison(image_fw_id, device_fw_id,
			image_config_id, device_config_id, MAX_SIZE_CONFIG_ID);
		goto exit;
	} else {
		if (image_fw_id != device_fw_id) {
			LOGN("Image build ID and device fw ID mismatched\n");
			result = UPDATE_TDDI_FIRMWARE_AND_CONFIG;
			goto exit;
		}

		for (idx = 0; idx < MAX_SIZE_CONFIG_ID; idx++) {
			if (image_config_id[idx] != device_config_id[idx]) {
				LOGN("Different Config ID\n");
				result = UPDATE_TDDI_CONFIG;
				goto exit;
			}
		}
		result = UPDATE_TDDI_NONE;
	}

exit:
	switch (result) {
	case UPDATE_TDDI_FIRMWARE_AND_CONFIG:
		LOGN("Update firmware and config\n");
		break;
	case UPDATE_TDDI_CONFIG:
		LOGN("Update config only\n");
		break;
	case UPDATE_TDDI_NONE:
	default:
		LOGN("No need to do reflash\n");
		break;
	}

	return (int)result;
}
/**
 * @brief   Query the supported features.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [out] reflash_data:  data blob for tddi reflash
 *    [ in] resp_reading:  method to read in the response
 *                         a positive value presents the ms time delay for polling;
 *                         or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    none
 */
static void syna_tcm_tddi_get_features(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int resp_reading)
{
	int retval;
	struct tcm_features_info features;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash_data\n");
		return;
	}

	if (!IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGE("Invalid tcm device mode\n");
		return;
	}

	retval = syna_tcm_get_features(tcm_dev, &features, resp_reading);
	if (retval < 0)
		return;

	reflash_data->has_s_chip = ((features.byte[2] & 0x02) != 0x00) ? true : false;
	reflash_data->has_other_td_chips = ((features.byte[2] & 0x18) != 0x00) ? true : false;

	LOGD("feature:0x%02X. has s-tddi:%s, other-tddi:%s\n", features.byte[2],
		(reflash_data->has_s_chip) ? "yes" : "no",
		(reflash_data->has_other_td_chips) ? "yes" : "no");

}

/**
 * @brief   Wrap up a TouchComm message to communicate with TDDI ROM-Boot.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [out] out:            data sent out, if any
 *    [out] out_size:       size of data sent out
 *    [ in] in:             buffer to store the data read in
 *    [ in] in_size:        size of data read in
 *    [ in] resp_reading:   method to read in the response
 *                           a positive value presents the ms time delay for polling;
 *                           or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_send_command(struct tcm_dev *tcm_dev,
	unsigned char *out, unsigned int out_size, unsigned char *in,
	unsigned int in_size, unsigned int resp_reading)
{
	int retval;
	unsigned char resp_code;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (out_size < sizeof(struct external_flash_param)) {
		LOGE("Invalid size of out data, %d, min. size:%d\n",
			out_size, (int)sizeof(struct external_flash_param));
		return -ERR_INVAL;
	}

	if (resp_reading == CMD_RESPONSE_IN_ATTN) {
		if (!tcm_dev->hw->support_attn) {
			resp_reading = tcm_dev->msg_data.command_polling_time;
			LOGN("No support of IRQ control, use polling mode instead, interval:%d\n",
				resp_reading);
		}
	}

	retval = tcm_dev->write_message(tcm_dev,
			CMD_SPI_MASTER_WRITE_THEN_READ_EXTENDED,
			out,
			out_size,
			&resp_code,
			resp_reading);
	if (retval < 0) {
		LOGE("Fail to send romboot flash command 0x%02x\n",
			CMD_SPI_MASTER_WRITE_THEN_READ_EXTENDED);
		goto exit;
	}

	LOGD("resp_code: 0x%x, resp length: %d\n",
		resp_code, tcm_dev->resp_buf.data_length);

	if ((in == NULL) || (in_size < tcm_dev->resp_buf.data_length))
		goto exit;

	/* copy resp data to caller */
	retval = syna_pal_mem_cpy((unsigned char *)in,
			in_size,
			tcm_dev->resp_buf.buf,
			tcm_dev->resp_buf.buf_size,
			tcm_dev->resp_buf.data_length);
	if (retval < 0) {
		LOGE("Fail to copy resp data to caller\n");
		goto exit;
	}

exit:
	return retval;
}
/**
 * @brief   Assemble a command packet communicating to the TDDI ROM-Boot targeting on the
 *          TDDI multichip architecture.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] command:        command to send
 *    [out] out:            additional data sent out, if any
 *    [out] out_size:       size of data sent out
 *    [ in] in:             buffer to store the data read in
 *    [ in] in_size:        size of data read in
 *    [ in] resp_reading:   method to read in the response
 *                           a positive value presents the ms time delay for polling;
 *                           or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_multichip_send_command(struct tcm_dev *tcm_dev,
	unsigned char command, unsigned char *out, unsigned int out_size,
	unsigned char *in, unsigned int in_size, unsigned int resp_reading)
{
	int retval;
	unsigned char *payload_buf = NULL;
	unsigned int payload_size;
	struct external_flash_param flash_param;
	unsigned int offset = (int)sizeof(struct external_flash_param);

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set((void *)&flash_param, 0x00, sizeof(flash_param));

	flash_param.read_size[0] = (unsigned char)(in_size & 0xff);
	flash_param.read_size[1] = (unsigned char)(in_size >> 8) & 0xff;

	flash_param.command = command;

	payload_size = offset + out_size;
	if (flash_param.command != 0x00)
		payload_size += 2;

	LOGD("Command: 0x%02x, packet size: %d, out size:%d, in size:%d\n",
		command, payload_size, out_size, in_size);

	payload_buf = syna_pal_mem_alloc(payload_size, sizeof(unsigned char));
	if (!payload_buf) {
		LOGE("Fail to allocate buffer to store flash command\n");
		return -ERR_NOMEM;
	}

	if (flash_param.command != 0x00) {
		payload_buf[offset] = (unsigned char)out_size;
		payload_buf[offset + 1] = (unsigned char)(out_size >> 8);
		if (out_size > 0) {
			retval = syna_pal_mem_cpy(&payload_buf[offset + 2],
					payload_size - offset - 2, out, out_size, out_size);
			if (retval < 0) {
				LOGE("Fail to copy payload to payload_buf\n");
				goto exit;
			}
		}

		LOGD("Packet: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			flash_param.byte0, flash_param.byte1, flash_param.byte2,
			flash_param.read_size[0], flash_param.read_size[1],
			flash_param.command, payload_buf[offset], payload_buf[offset + 1]);
	}

	retval = syna_pal_mem_cpy(payload_buf, payload_size,
			&flash_param, sizeof(flash_param), sizeof(flash_param));
	if (retval < 0) {
		LOGE("Fail to copy flash_param header to payload_buf\n");
		goto exit;
	}

	retval = syna_tcm_tddi_romboot_send_command(tcm_dev, payload_buf,
			payload_size, in, in_size, resp_reading);
	if (retval < 0) {
		LOGE("Fail to write command 0x%x\n", flash_param.command);
		goto exit;
	}

exit:
	syna_pal_mem_free((void *)payload_buf);

	return retval;
}
/**
 * @brief   Get the response packet from the TDDI ROM-Boot targeting on the
 *          TDDI multichip architecture.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] length:         size to read
 *    [out] resp:           buffer to store the resp data
 *    [ in] resp_size:      size of resp data
 *    [ in] delay_ms:       delay time to get the response
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_multichip_get_resp(struct tcm_dev *tcm_dev,
	unsigned int length, unsigned char *resp, unsigned int resp_size,
	unsigned int delay_ms)
{
	int retval;
	unsigned char *tmp_buf = NULL;
	unsigned int xfer_len;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (resp && (resp_size < length)) {
		LOGE("Invalid buffer size, len:%d, size:%d\n",
			length, resp_size);
		return -ERR_INVAL;
	}

	xfer_len = length + 2;

	tmp_buf = syna_pal_mem_alloc(xfer_len, sizeof(unsigned char));
	if (!tmp_buf) {
		LOGE("Fail to allocate tmp_buf\n");
		return -ERR_NOMEM;
	}

	retval = syna_tcm_tddi_romboot_multichip_send_command(tcm_dev, CMD_NONE,
			NULL, 0, tmp_buf, xfer_len, delay_ms);
	if (retval < 0) {
		LOGE("Fail to get resp, size: %d\n", xfer_len);
		goto exit;
	}

	if (resp) {
		retval = syna_pal_mem_cpy(resp, resp_size, &tmp_buf[1],
				xfer_len - 1, length);
		if (retval < 0) {
			LOGE("Fail to copy resp data\n");
			goto exit;
		}
	}

exit:
	syna_pal_mem_free((void *)tmp_buf);

	return retval;
}
/**
 * @brief   Get a status packet from the TDDI ROM-Boot targeting on the
 *          TDDI multichip architecture.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [out] resp_status:    response status returned
 *    [out] resp_length:    response length returned
 *    [ in] delay_ms:       delay time to get the response
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_multichip_get_status(struct tcm_dev *tcm_dev,
	unsigned char *resp_status, unsigned int *resp_length, unsigned int delay_ms)
{
	int retval;
	unsigned char resp[4] = { 0 };
	int timeout = 0;
	int MAX_TIMEOUT = 1000;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	syna_pal_sleep_ms(delay_ms);

	do {
		retval = syna_tcm_tddi_romboot_multichip_send_command(tcm_dev, CMD_NONE,
				NULL, 0, resp, 3, delay_ms);
		if (retval < 0) {
			LOGE("Fail to poll the resp\n");
			goto exit;
		}

		LOGD("status: %02x %02x %02x\n", resp[0], resp[1], resp[2]);

		if (resp[0] == 0xff) {
			syna_pal_sleep_ms(100);
			timeout += 100;
			continue;
		} else if (resp[0] == 0x01) {
			*resp_status = resp[0];
			*resp_length = syna_pal_le2_to_uint(&resp[1]);
			goto exit;
		} else {
			LOGE("Invalid resp, %02x %02x %02x\n",
				resp[0], resp[1], resp[2]);
			retval = -ERR_TCMMSG;
			goto exit;
		}

	} while (timeout < MAX_TIMEOUT);

	if (timeout >= 500) {
		LOGE("Timeout to get the status\n");
		retval = -ERR_TIMEDOUT;
	}
exit:
	return retval;
}
/**
 * @brief   Write the data packet to the TDDI ROM-Boot targeting on the
 *          TDDI multichip architecture.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for tddi reflash
 *    [ in] address:         the address in flash memory to write
 *    [ in] wr_data:         binary data to write
 *    [ in] wr_len:          length of data to write
 *    [ in] wr_delay_ms:     a short delay after the command executed
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_romboot_multichip_write_flash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int address,
	const unsigned char *wr_data, unsigned int wr_len, unsigned int wr_delay_ms)
{
	int retval;
	unsigned int offset;
	unsigned int w_length;
	unsigned int xfer_length;
	unsigned int remaining_length;
	unsigned int flash_address;
	unsigned int block_address;
	unsigned char resp_code = 0;
	unsigned int resp_length = 0;
	unsigned int resp_delay;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	w_length = reflash_data->max_write_payload_size - 2;
	w_length -= (w_length % reflash_data->write_block_size);

	offset = 0;

	remaining_length = wr_len;

	syna_tcm_buf_lock(&reflash_data->out);

	while (remaining_length) {
		if (remaining_length > w_length)
			xfer_length = w_length;
		else
			xfer_length = remaining_length;

		retval = syna_tcm_buf_alloc(&reflash_data->out, xfer_length + 2);
		if (retval < 0) {
			LOGE("Fail to allocate memory for buf.out\n");
			goto exit;
		}

		flash_address = address + offset;
		block_address = flash_address / reflash_data->write_block_size;

		reflash_data->out.buf[0] = (unsigned char)block_address & 0xff;
		reflash_data->out.buf[1] = (unsigned char)(block_address >> 8);

		if (wr_delay_ms == CMD_RESPONSE_IN_ATTN) {
			LOGD("xfer: %d (remaining: %d), delay: ATTN-driven\n",
				xfer_length, remaining_length);
			resp_delay = CMD_RESPONSE_IN_ATTN;
		} else {
			resp_delay = wr_delay_ms;
			LOGD("xfer: %d (remaining: %d), delay: %d ms\n",
				xfer_length, remaining_length, resp_delay);
		}

		retval = syna_pal_mem_cpy(&reflash_data->out.buf[2],
				reflash_data->out.buf_size - 2, &wr_data[offset],
				wr_len - offset, xfer_length);
		if (retval < 0) {
			LOGE("Fail to copy write data ,size: %d\n",
				xfer_length);
			goto exit;
		}

		retval = syna_tcm_tddi_romboot_multichip_send_command(tcm_dev, CMD_WRITE_FLASH,
				reflash_data->out.buf, xfer_length + 2, NULL, 0, resp_delay);
		if (retval < 0) {
			LOGE("Fail to write data to flash addr 0x%x, size %d\n",
				flash_address, xfer_length + 2);
			goto exit;
		}

		retval = syna_tcm_tddi_romboot_multichip_get_status(tcm_dev, &resp_code,
				&resp_length, wr_delay_ms);
		if (retval < 0) {
			LOGE("Fail to get the response of command 0x%x\n",
				CMD_WRITE_FLASH);
			goto exit;
		}

		LOGD("status:%02x, data_length:%d\n", resp_code, resp_length);

		if (resp_code != STATUS_OK) {
			LOGE("Invalid response of command %x\n",
				CMD_WRITE_FLASH);
			retval = -ERR_TCMMSG;
			goto exit;
		}

		retval = syna_tcm_tddi_romboot_multichip_get_resp(tcm_dev, resp_length,
				NULL, 0, wr_delay_ms);
		if (retval < 0) {
			LOGE("Fail to get the boot info packet\n");
			goto exit;
		}

		offset += xfer_length;
		remaining_length -= xfer_length;
	}

	retval = 0;

exit:
	syna_tcm_buf_unlock(&reflash_data->out);

	return retval;
}
/**
 * @brief   Request an erase command to the TDDI ROM-Boot targeting on the
 *          TDDI multichip architecture.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] reflash_data:   data blob for tddi reflash
 *    [ in] address:        the address in flash memory to erase
 *    [ in] size:           size to erase
 *    [ in] erase_delay_ms: a short delay after the command executed
 *                          set a positive value to indicate the delay for flash erase,
 *                          set '0' or 'RESP_IN_ATTN' to select ATTN-driven.
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_romboot_multichip_erase_flash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int address,
	unsigned int size, unsigned int erase_delay_ms)
{
	int retval;
	unsigned int page_start = 0;
	unsigned int page_count = 0;
	unsigned char out_buf[4] = { 0 };
	unsigned char resp_code = 0;
	unsigned int resp_length = 0;
	int size_erase_cmd;
	unsigned int resp_delay;

	page_start = address / reflash_data->page_size;

	page_count = syna_pal_ceil_div(size, reflash_data->page_size);

	if (erase_delay_ms == CMD_RESPONSE_IN_ATTN) {
		resp_delay = CMD_RESPONSE_IN_ATTN;
		LOGD("Page start = %d (0x%04x), Page count = %d (0x%04x), delay: ATTN-driven\n",
			page_start, page_start, page_count, page_count);
	} else {
		resp_delay = erase_delay_ms * page_count;
		LOGD("Page start = %d (0x%04x), Page count = %d (0x%04x), delay: %d ms\n",
			page_start, page_start, page_count, page_count, resp_delay);
	}

	if ((page_start > 0xff) || (page_count > 0xff)) {
		size_erase_cmd = 4;

		out_buf[0] = (unsigned char)(page_start & 0xff);
		out_buf[1] = (unsigned char)((page_start >> 8) & 0xff);
		out_buf[2] = (unsigned char)(page_count & 0xff);
		out_buf[3] = (unsigned char)((page_count >> 8) & 0xff);
	} else {
		size_erase_cmd = 2;

		out_buf[0] = (unsigned char)(page_start & 0xff);
		out_buf[1] = (unsigned char)(page_count & 0xff);
	}

	retval = syna_tcm_tddi_romboot_multichip_send_command(tcm_dev, CMD_ERASE_FLASH,
			out_buf, size_erase_cmd, NULL, 0, resp_delay);
	if (retval < 0) {
		LOGE("Fail to erase data at 0x%x (page:0x%x, count:%d)\n",
			address, page_start, page_count);
		return retval;
	}

	retval = syna_tcm_tddi_romboot_multichip_get_status(tcm_dev, &resp_code,
			&resp_length, resp_delay);
	if (retval < 0) {
		LOGE("Fail to get the response of command 0x%x\n",
			CMD_ERASE_FLASH);
		return retval;
	}

	LOGD("status:%02x, resp length:%d\n", resp_code, resp_length);

	if (resp_code != STATUS_OK) {
		LOGE("Invalid response of command %x\n", CMD_WRITE_FLASH);
		retval = -ERR_TCMMSG;
		return retval;
	}

	return retval;
}
/**
 * @brief   Query the boot information from the TDDI ROM-Boot targeting on the
 *          TDDI multichip architecture.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [out] boot_info:     the requested packet returned
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the ms time delay for polling;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_romboot_multichip_get_boot_info(struct tcm_dev *tcm_dev,
	struct tcm_boot_info *boot_info, unsigned int resp_reading)
{
	int retval = 0;
	unsigned char resp_code;
	unsigned int resp_data_len = 0;
	unsigned int copy_size;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	retval = syna_tcm_tddi_romboot_multichip_send_command(tcm_dev, CMD_GET_BOOT_INFO,
			NULL, 0, NULL, 0, resp_reading);
	if (retval < 0) {
		LOGE("Fail to run command 0x%x\n", CMD_GET_BOOT_INFO);
		goto exit;
	}

	retval = syna_tcm_tddi_romboot_multichip_get_status(tcm_dev, &resp_code,
			&resp_data_len, resp_reading);
	if (retval < 0) {
		LOGE("Fail to get the response of command 0x%x\n", CMD_GET_BOOT_INFO);
		return retval;
	}

	LOGD("status:%02x, resp length:%d\n", resp_code, resp_data_len);

	if (resp_code != STATUS_OK) {
		LOGE("Invalid response of command %x\n", CMD_GET_BOOT_INFO);
		retval = -ERR_TCMMSG;
		return retval;
	}

	if (boot_info == NULL)
		goto exit;

	copy_size = min(sizeof(struct tcm_boot_info), resp_data_len);

	retval = syna_tcm_tddi_romboot_multichip_get_resp(tcm_dev, copy_size,
			(unsigned char *)boot_info, sizeof(struct tcm_boot_info), resp_reading);
	if (retval < 0) {
		LOGE("Fail to get the boot info packet\n");
		return retval;
	}

exit:
	return retval;
}
/**
 * @brief   Confirm that the TDDI ROM-Boot firmware is activate.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [out] reflash_data:  data blob for tddi reflash
 *    [ in] is_multichip:  flag to indicate a multichip DUT
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the ms time delay for polling;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay: method to switch the fw mode
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_preparations(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, bool is_multichip,
	unsigned int resp_reading, unsigned int fw_switch_delay)
{
	int retval;
	unsigned int temp;
	struct tcm_identification_info id_info;
	struct tcm_boot_info *boot_info;
	unsigned int wr_chunk;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	LOGI("Set up preparations, multi-chip: %s\n", (is_multichip) ? "yes" : "no");

	retval = syna_tcm_identify(tcm_dev, &id_info, resp_reading);
	if (retval < 0) {
		LOGE("Fail to do identification\n");
		return retval;
	}

	if (IS_APP_FW_MODE(id_info.mode)) {
		syna_tcm_tddi_get_features(tcm_dev, reflash_data, resp_reading);
		LOGI("%s\n", id_info.part_number);
	}

	/* switch to bootloader mode */
	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		LOGI("Prepare to enter bootloader mode\n");
		if (is_multichip) {
			retval = syna_tcm_switch_fw_mode(tcm_dev,
					MODE_TDDI_BOOTLOADER, fw_switch_delay);
			if (retval < 0) {
				LOGE("Fail to enter tddi bootloader mode\n");
				return retval;
			}
		} else {
			retval = syna_tcm_switch_fw_mode(tcm_dev,
					MODE_MULTICHIP_TDDI_BOOTLOADER, fw_switch_delay);
			if (retval < 0) {
				LOGE("Fail to enter tddi bootloader mode\n");
				return retval;
			}
		}
	}
	/* switch to rom boot mode */
	if (!IS_ROM_BOOTLOADER_MODE(tcm_dev->dev_mode)) {
		LOGI("Prepare to enter rom boot mode\n");
		retval = syna_tcm_switch_fw_mode(tcm_dev, MODE_ROMBOOTLOADER,
				fw_switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter rom boot mode\n");
			return retval;
		}
	}

	if (!IS_ROM_BOOTLOADER_MODE(tcm_dev->dev_mode)) {
		LOGE("Device not in romboot mode\n");
		return -ERR_INVAL;
	}

	if (!is_multichip)
		return 0;

	boot_info = &tcm_dev->boot_info;

	retval = syna_tcm_romboot_multichip_get_boot_info(tcm_dev,
			boot_info, resp_reading);
	if (retval < 0) {
		LOGE("Fail to get boot info\n");
		return retval;
	}

	wr_chunk = tcm_dev->max_wr_size;

	temp = boot_info->write_block_size_words;
	reflash_data->write_block_size = temp * 2;

	LOGI("Write block size: %d (words size: %d)\n",
		reflash_data->write_block_size, temp);

	temp = syna_pal_le2_to_uint(boot_info->erase_page_size_words);
	reflash_data->page_size = temp * 2;

	LOGI("Erase page size: %d (words size: %d)\n",
		reflash_data->page_size, temp);

	temp = syna_pal_le2_to_uint(boot_info->max_write_payload_size);
	reflash_data->max_write_payload_size = temp;

	LOGI("Max write flash data size: %d\n",
		reflash_data->max_write_payload_size);

	if (reflash_data->write_block_size > (wr_chunk - 9)) {
		LOGE("Write block size, %d, greater than chunk space, %d\n",
			reflash_data->write_block_size, (wr_chunk - 9));
		return -ERR_INVAL;
	}

	if (reflash_data->write_block_size == 0) {
		LOGE("Invalid write block size %d\n", reflash_data->write_block_size);
		return -ERR_INVAL;
	}

	if (reflash_data->page_size == 0) {
		LOGE("Invalid erase page size %d\n", reflash_data->page_size);
		return -ERR_INVAL;
	}

	return 0;
}
/**
 * @brief    Assemble a jedec flash commend communicating to the TDDI ROM-Boot.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] flash_command:  flash command to send
 *    [out] out:            additional data sent out, if any
 *    [out] out_size:       size of data sent out
 *    [ in] in:             buffer to store the data read in
 *    [ in] in_size:        size of data read in
 *    [ in] delay_ms:       delay time to get the response
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_jedec_send_command(struct tcm_dev *tcm_dev,
	unsigned char flash_command, unsigned char *out, unsigned int out_size,
	unsigned char *in, unsigned int in_size, unsigned int delay_ms)
{
	int retval;
	unsigned char *payload_buf = NULL;
	unsigned int payload_size;
	struct external_flash_param flash_param;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set((void *)&flash_param, 0x00, sizeof(flash_param));

	flash_param.spi_param = 1;
	flash_param.clk_div = 0x19;

	flash_param.read_size[0] = (unsigned char)(in_size & 0xff);
	flash_param.read_size[1] = (unsigned char)(in_size >> 8) & 0xff;

	flash_param.command = flash_command;

	payload_size = sizeof(struct external_flash_param) + out_size;

	LOGD("Flash command: 0x%02x, total size: %d, wr: %d, rd: %d\n",
		flash_command, payload_size, out_size, in_size);
	LOGD("Packet: %02x %02x %02x %02x %02x %02x\n",
		flash_param.byte0, flash_param.byte1, flash_param.byte2,
		flash_param.read_size[0], flash_param.read_size[1],
		flash_param.command);

	payload_buf = syna_pal_mem_alloc(payload_size, sizeof(unsigned char));
	if (!payload_buf) {
		LOGE("Fail to allocate buffer to store flash command\n");
		return -ERR_NOMEM;
	}

	retval = syna_pal_mem_cpy(payload_buf, payload_size,
			&flash_param, sizeof(flash_param), sizeof(flash_param));
	if (retval < 0) {
		LOGE("Fail to copy flash_param header to payload_buf\n");
		goto exit;
	}

	if (out && (out_size > 0)) {
		retval = syna_pal_mem_cpy(payload_buf + sizeof(flash_param),
				payload_size - sizeof(flash_param), out, out_size, out_size);
		if (retval < 0) {
			LOGE("Fail to copy data to payload_buf\n");
			goto exit;
		}
	}

	retval = syna_tcm_tddi_romboot_send_command(tcm_dev,
			payload_buf, payload_size, in, in_size, delay_ms);
	if (retval < 0) {
		LOGE("Fail to write flash command 0x%x\n", flash_param.command);
		goto exit;
	}

exit:
	syna_pal_mem_free((void *)payload_buf);

	return retval;
}
/**
 * @brief    Poll the flash status through the jedec flash commends.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] delay_ms:       delay time to get the response
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_jedec_get_status(struct tcm_dev *tcm_dev,
	unsigned int delay_ms)
{
	int retval;
	int idx;
	unsigned char status;
	int STATUS_CHECK_RETRY = 50;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	for (idx = 0; idx < STATUS_CHECK_RETRY; idx++) {
		retval = syna_tcm_tddi_romboot_jedec_send_command(tcm_dev,
				JEDEC_READ_STATUS, NULL, 0, &status, sizeof(status),
				delay_ms);
		if (retval < 0) {
			LOGE("Failed to write JEDEC_READ_STATUS\n");
			return retval;
		}

		syna_pal_sleep_us(JEDEC_STATUS_CHECK_US_MIN,
			JEDEC_STATUS_CHECK_US_MAX);
		/* once completed, status = 0 */
		if (!status)
			break;
	}

	if (status)
		retval = -ERR_TCMMSG;
	else
		retval = status;

	return retval;
}

/**
 * @brief    Send a jedec flash commend to write data to the flash.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] address:         the address in flash memory to write
 *    [ in] data:            data to write
 *    [ in] data_size:       size of data
 *    [ in] delay_ms:        a short delay time in millisecond to wait for
 *                           the completion of flash access
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_jedec_write_flash(struct tcm_dev *tcm_dev,
	unsigned int address, const unsigned char *data, unsigned int data_size,
	unsigned int delay_ms)
{
	int retval = 0;
	unsigned int offset;
	unsigned char buf[ROMBOOT_FLASH_PAGE_SIZE + 3];
	unsigned int remaining_length;
	unsigned int xfer_length;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!data) || (data_size == 0)) {
		LOGE("Invalid image data, no data available\n");
		return -ERR_INVAL;
	}

	remaining_length = data_size;

	offset = 0;

	while (remaining_length) {
		if (remaining_length > ROMBOOT_FLASH_PAGE_SIZE)
			xfer_length = ROMBOOT_FLASH_PAGE_SIZE;
		else
			xfer_length = remaining_length;

		syna_pal_mem_set(buf, 0x00, sizeof(buf));

		retval = syna_tcm_tddi_romboot_jedec_send_command(tcm_dev,
				JEDEC_WRITE_ENABLE, NULL, 0, NULL, 0, delay_ms);
		if (retval < 0) {
			LOGE("Failed to write JEDEC_WRITE_ENABLE\n");
			goto exit;
		}

		buf[0] = (unsigned char)((address + offset) >> 16);
		buf[1] = (unsigned char)((address + offset) >> 8);
		buf[2] = (unsigned char)(address + offset);

		retval = syna_pal_mem_cpy(&buf[3], sizeof(buf) - 3,
				&data[offset], data_size - offset, xfer_length);
		if (retval < 0) {
			LOGE("Fail to copy data to write, size: %d\n",
				xfer_length);
			goto exit;
		}

		retval = syna_tcm_tddi_romboot_jedec_send_command(tcm_dev,
				JEDEC_PAGE_PROGRAM, buf, sizeof(buf), NULL, 0, delay_ms);
		if (retval < 0) {
			LOGE("Failed to write data to addr 0x%x (offset: %x)\n",
				address + offset, offset);
			LOGE("Remaining data %d\n",
				remaining_length);
			goto exit;
		}

		retval = syna_tcm_tddi_romboot_jedec_get_status(tcm_dev, delay_ms);
		if (retval < 0) {
			LOGE("Fail to get correct status, retval = %d\n",
				retval);
			goto exit;
		}
		offset += xfer_length;
		remaining_length -= xfer_length;
	}

exit:
	return retval;
}
/**
 * @brief    Send a jedec flash commend to erase the flash.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] delay_ms:       delay time to get the response
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_jedec_erase_flash(struct tcm_dev *tcm_dev,
	unsigned int delay_ms)
{
	int retval;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	retval = syna_tcm_tddi_romboot_jedec_send_command(tcm_dev,
			JEDEC_WRITE_ENABLE, NULL, 0, NULL, 0, delay_ms);
	if (retval < 0) {
		LOGE("Failed to write JEDEC_WRITE_ENABLE\n");
		return retval;
	}

	retval = syna_tcm_tddi_romboot_jedec_send_command(tcm_dev,
			JEDEC_CHIP_ERASE, NULL, 0, NULL, 0, delay_ms);
	if (retval < 0) {
		LOGE("Failed to write JEDEC_WRITE_ENABLE\n");
		return retval;
	}

	retval = syna_tcm_tddi_romboot_jedec_get_status(tcm_dev, delay_ms);
	if (retval < 0) {
		LOGE("Fail to get correct status, retval = %d\n", retval);
		return retval;
	}

	return 0;
}
/**
 * @brief    Write data to the flash memory.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for tddi reflash
 *    [ in] address:         the address in flash memory to write
 *    [ in] data:            data to write
 *    [ in] size:            size of data to write
 *    [ in] delay_ms:        a short delay time in millisecond to wait for
 *                           the completion of flash access
 *    [ in] is_multichip:    use multi-chip command packet instead
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_write_flash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int address,
	const unsigned char *data, unsigned int size, unsigned int delay_ms,
	bool is_multichip)
{
	if (!tcm_dev || !reflash_data)
		return -ERR_INVAL;

	if (!data)
		return -ERR_INVAL;

	if (is_multichip)
		return syna_tcm_romboot_multichip_write_flash(tcm_dev, reflash_data,
			address, data, size, delay_ms);
	else
		return syna_tcm_tddi_romboot_jedec_write_flash(tcm_dev, address,
			data, size, delay_ms);
}
/**
 * @brief    Request to erase the flash memory.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for tddi reflash
 *    [ in] address:         the address in flash memory to erase
 *    [ in] size:            size to erase
 *    [ in] delay_ms:        delay time to get the response
 *    [ in] is_multichip:    use multi-chip command packet instead
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_erase_flash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int address,
	unsigned int size, unsigned int delay_ms, bool is_multichip)
{
	if (!tcm_dev || !reflash_data)
		return -ERR_INVAL;

	if (is_multichip)
		return syna_tcm_romboot_multichip_erase_flash(tcm_dev, reflash_data,
				address, size, delay_ms);
	else
		return syna_tcm_tddi_romboot_jedec_erase_flash(tcm_dev, delay_ms);
}
/**
 * @brief    Implement the sequence of erase-and-program upon TDDI ROM-Boot.
 *
 * @param
 *    [ in] tcm_dev:                the TouchComm device handle
 *    [ in] reflash_data:           data blob for tddi reflash
 *    [ in] is_multichip:           flag to indicate a multichip DUT
 *    [ in] flash_delay_settings:   set up the us delay time to wait for the completion of flash access
 *                                    for polling,     set a value formatted with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_do_program(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, bool is_multichip,
	unsigned int flash_delay_settings)
{
	int retval = 0;
	int idx;
	struct ihex_info *ihex_info = NULL;
	struct ihex_area_block *block;
	const unsigned int ROMBOOT_DELAY_TIME = 20;
	unsigned int flash_erase_delay_ms;
	unsigned int flash_write_delay_us;
	unsigned int fw_switch_time;
	unsigned int resp_handling;
	bool done = false;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash_data blob\n");
		return -ERR_INVAL;
	}

	ihex_info = &reflash_data->ihex_info;

	if (flash_delay_settings == CMD_RESPONSE_IN_ATTN) {
		fw_switch_time = flash_delay_settings;
		resp_handling = flash_delay_settings;
	} else {
		fw_switch_time = tcm_dev->fw_mode_switching_time;
		resp_handling = tcm_dev->msg_data.command_polling_time;
	}

	/* for ihex programming, it's better to run in polling.
	 * in addition, 20 ms is the typical period for polling.
	 */
	flash_erase_delay_ms = (flash_delay_settings >> 16) & 0xFFFF;
	flash_write_delay_us = flash_delay_settings & 0xFFFF;
	if ((flash_erase_delay_ms == 0) || (flash_erase_delay_ms < ROMBOOT_DELAY_TIME))
		flash_erase_delay_ms = ROMBOOT_DELAY_TIME;
	if ((flash_write_delay_us == 0) || (flash_write_delay_us < ROMBOOT_DELAY_TIME))
		flash_write_delay_us = ROMBOOT_DELAY_TIME;

	/* enter the tddi bootloader mode */
	retval = syna_tcm_tddi_romboot_preparations(tcm_dev, reflash_data,
			is_multichip, resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up tddi romboot preparations\n");
		goto exit;
	}

	if (tcm_dev->dev_mode != MODE_ROMBOOTLOADER) {
		LOGE("Incorrect rom-bootloader mode, 0x%02x, expected: 0x%02x\n",
			tcm_dev->dev_mode, MODE_ROMBOOTLOADER);
		return -ERR_INVAL;
	}

	if (!is_multichip) {
		/* for single-chip, mass erase will affect the entire flash memory
		 * so the info of flash address and the size to erase are useless
		 */
		retval = syna_tcm_tddi_romboot_erase_flash(tcm_dev, reflash_data,
				0, 0, flash_erase_delay_ms, false);
	} else {
		/* for multi-chip, do erase based on the areas defined in the ihex file */
		for (idx = 0; idx < IHEX_MAX_BLOCKS; idx++) {
			block = &ihex_info->block[idx];
			if (!block->available)
				continue;
			if (block->size == 0)
				continue;

			retval = syna_tcm_tddi_romboot_erase_flash(tcm_dev, reflash_data,
					block->flash_addr, block->size, flash_erase_delay_ms, true);
		}
	}
	if (retval < 0) {
		LOGE("Fail to erase flash\n");
		goto exit;
	}

	LOGN("Flash erased\n");
	LOGN("Start to write all data to flash\n");

	for (idx = 0; idx < IHEX_MAX_BLOCKS; idx++) {

		block = &ihex_info->block[idx];

		if (!block->available)
			continue;

		LOGD("block:%d, addr:0x%x, size:%d\n",
			idx, block->flash_addr, block->size);

		if (block->size == 0)
			continue;

		retval = syna_tcm_tddi_romboot_write_flash(tcm_dev, reflash_data,
				block->flash_addr, block->data, block->size, flash_write_delay_us,
				is_multichip);
		if (retval < 0) {
			LOGE("Fail to write data to addr 0x%x, size:%d\n",
				block->flash_addr, block->size);
			goto exit;
		}

		LOGI("Data written, size:%d\n", block->size);
	}

	retval = 0;
	done = true;

exit:
	retval = syna_tcm_reset(tcm_dev, resp_handling);
	if (retval < 0) {
		LOGE("Fail to do reset\n");
		goto exit;
	} else {
		if (done && (!IS_APP_FW_MODE(tcm_dev->dev_mode)))
			syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE,
				fw_switch_time);
	}

	return retval;
}
/**
 * @brief    Implement the sequence of reflash upon TDDI ROM-Boot.
 *
 * @param
 *    [ in] tcm_dev:              the TouchComm device handle
 *    [ in] reflash_data:         data blob for tddi reflash
 *    [ in] is_multichip:         flag to indicate a multichip DUT
 *    [ in] flash_delay_settings: set up the us delay time to wait for the completion of flash access
 *                                    for polling,     set a value formatted with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_romboot_do_reflash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, bool is_multichip,
	unsigned int flash_delay_settings)
{
	int retval = 0;
	int idx;
	struct image_info *image_info = NULL;
	struct area_block *block;
	bool has_tool_boot_cfg = false;
	unsigned int flash_erase_delay_ms;
	unsigned int flash_write_delay_us;
	unsigned int fw_switch_time;
	unsigned int resp_handling;
	bool done = false;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash_data blob\n");
		return -ERR_INVAL;
	}

	if (!is_multichip) {
		LOGE("Rom-bootloader reflash is supposed to target on tddi multichip only\n");
		return -ERR_INVAL;
	}

	image_info = &reflash_data->image_info;

	has_tool_boot_cfg = image_info->data[AREA_TOOL_BOOT_CONFIG].available;

	if (flash_delay_settings == CMD_RESPONSE_IN_ATTN) {
		flash_erase_delay_ms = flash_delay_settings;
		flash_write_delay_us = flash_delay_settings;
		fw_switch_time = flash_delay_settings;
		resp_handling = flash_delay_settings;
	} else {
		flash_erase_delay_ms = (flash_delay_settings >> 16) & 0xFFFF;
		flash_write_delay_us = flash_delay_settings & 0xFFFF;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
		resp_handling = tcm_dev->msg_data.command_polling_time;
	}

	/* enter the tddi bootloader mode */
	retval = syna_tcm_tddi_romboot_preparations(tcm_dev, reflash_data,
			is_multichip, resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to set up tddi romboot preparations\n");
		goto exit;
	}

	if (tcm_dev->dev_mode != MODE_ROMBOOTLOADER) {
		LOGE("Incorrect rom-bootloader mode, 0x%02x, expected: 0x%02x\n",
			tcm_dev->dev_mode, MODE_ROMBOOTLOADER);
		return -ERR_INVAL;
	}

	/* Traverse through all blocks in the image file,
	 * then erase the corresponding block area
	 */
	for (idx = 0; idx < AREA_MAX; idx++) {
		block = &image_info->data[idx];
		if (!block->available)
			continue;

		if (idx == AREA_ROMBOOT_APP_CODE)
			continue;

		if ((idx == AREA_BOOT_CONFIG) && has_tool_boot_cfg)
			continue;

		LOGD("Erase %s block - address: 0x%x (%d), size: %d\n",
			AREA_ID_STR(block->id), block->flash_addr,
			block->flash_addr, block->size);

		if (block->size == 0)
			continue;

		retval = syna_tcm_tddi_romboot_erase_flash(tcm_dev, reflash_data,
				block->flash_addr, block->size, flash_erase_delay_ms,
				is_multichip);
		if (retval < 0) {
			LOGE("Fail to erase %s area\n", AREA_ID_STR(block->id));
			goto exit;
		}

		LOGN("%s partition erased\n", AREA_ID_STR(block->id));
	}

	for (idx = 0; idx < AREA_MAX; idx++) {
		block = &image_info->data[idx];
		if (!block->available)
			continue;

		if (idx == AREA_ROMBOOT_APP_CODE)
			continue;

		if ((idx == AREA_BOOT_CONFIG) && has_tool_boot_cfg)
			continue;

		LOGD("Prepare to update %s partition\n", AREA_ID_STR(idx));
		LOGD("Write data to %s - address: 0x%x (%d), size: %d\n",
			AREA_ID_STR(block->id), block->flash_addr,
			block->flash_addr, block->size);

		if (block->size == 0)
			continue;

		retval = syna_tcm_tddi_romboot_write_flash(tcm_dev, reflash_data,
				block->flash_addr, block->data, block->size, flash_write_delay_us,
				is_multichip);
		if (retval < 0) {
			LOGE("Fail to update %s partition, size: %d\n",
				AREA_ID_STR(block->id), block->size);
			goto exit;
		}

		LOGN("%s written\n", AREA_ID_STR(block->id));
	}

	retval = 0;
	done = true;

exit:
	retval = syna_tcm_reset(tcm_dev, resp_handling);
	if (retval < 0) {
		LOGE("Fail to do reset\n");
		goto exit;
	} else {
		if (done && (!IS_APP_FW_MODE(tcm_dev->dev_mode)))
			syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE,
				fw_switch_time);
	}

	return retval;
}


/**
 * @brief   Confirm that the TDDI bootloader firmware is activate.
 *
 * @param
 *    [ in] tcm_dev:          the TouchComm device handle
 *    [out] reflash_data:     data blob for tddi reflash
 *    [ in] resp_reading:     method to read in the response
 *                             a positive value presents the ms time delay for polling;
 *                             or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay:  method to switch the fw mode
 *                             a positive value presents the ms time delay for polling;
 *                             or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_preparations(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int resp_reading,
	unsigned int fw_switch_delay)
{
	int retval;
	unsigned int temp;
	struct tcm_identification_info id_info;
	struct tcm_boot_info *boot_info;
	unsigned int wr_chunk;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	LOGI("Set up preparations\n");

	retval = syna_tcm_identify(tcm_dev, &id_info, resp_reading);
	if (retval < 0) {
		LOGE("Fail to do identification\n");
		return retval;
	}

	/* switch to bootloader mode */
	if (IS_APP_FW_MODE(id_info.mode)) {
		LOGI("Prepare to enter bootloader mode\n");
		retval = syna_tcm_switch_fw_mode(tcm_dev,
				MODE_BOOTLOADER,
				fw_switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter tddi bootloader mode\n");
			return retval;
		}
	}

	if (tcm_dev->dev_mode != MODE_TDDI_BOOTLOADER) {
		LOGE("Fail to enter tddi bootloader mode (current: 0x%x)\n",
			tcm_dev->dev_mode);
		return retval;
	}

	boot_info = &tcm_dev->boot_info;

	/* get boot info to set up the flash access */
	retval = syna_tcm_get_boot_info(tcm_dev, boot_info, resp_reading);
	if (retval < 0) {
		LOGE("Fail to get boot info at mode 0x%x\n",
			id_info.mode);
		return retval;
	}

	wr_chunk = tcm_dev->max_wr_size;

	temp = boot_info->write_block_size_words;
	reflash_data->write_block_size = temp * 2;

	LOGI("Write block size: %d (words size: %d)\n",
		reflash_data->write_block_size, temp);

	temp = syna_pal_le2_to_uint(boot_info->erase_page_size_words);
	reflash_data->page_size = temp * 2;

	LOGI("Erase page size: %d (words size: %d)\n",
		reflash_data->page_size, temp);

	temp = syna_pal_le2_to_uint(boot_info->max_write_payload_size);
	reflash_data->max_write_payload_size = temp;

	LOGI("Max write flash data size: %d\n",
		reflash_data->max_write_payload_size);

	if (reflash_data->write_block_size > (wr_chunk - 9)) {
		LOGE("Write block size, %d, greater than chunk space, %d\n",
			reflash_data->write_block_size, (wr_chunk - 9));
		return -ERR_INVAL;
	}

	if (reflash_data->write_block_size == 0) {
		LOGE("Invalid write block size %d\n",
			reflash_data->write_block_size);
		return -ERR_INVAL;
	}

	if (reflash_data->page_size == 0) {
		LOGE("Invalid erase page size %d\n",
			reflash_data->page_size);
		return -ERR_INVAL;
	}

	return 0;
}
/**
 * @brief   Wrap up a TouchComm message to communicate with TDDI bootloader.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] command:        given command code
 *    [ in] payload:        payload data, if any
 *    [ in] payload_len:    length of payload data
 *    [ in] resp_reading:   method to read in the response
 *                           a positive value presents the ms time delay for polling;
 *                           or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_send_command(struct tcm_dev *tcm_dev,
		unsigned char command, unsigned char *payload,
		unsigned int payload_len, unsigned int resp_reading)
{
	int retval = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!IS_TDDI_BOOTLOADER_MODE(tcm_dev->dev_mode)) {
		LOGE("Device is not in BL mode, 0x%x\n", tcm_dev->dev_mode);
		retval = -ERR_INVAL;
	}

	LOGD("Command 0x%x, length:%d\n", command, payload_len);

	retval = tcm_dev->write_message(tcm_dev,
			command,
			payload,
			payload_len,
			NULL,
			resp_reading);
	if (retval < 0) {
		LOGE("Fail to send command 0x%02x\n", command);
		goto exit;
	}

exit:
	return retval;
}

/**
 * @brief   Check whether the BOOT CONFIG area is valid.
 *
 * @param
 *    [ in] boot_config:     block data of boot_config from image file
 *    [ in] boot_info:       data of boot info
 *    [ in] block_size:      max size of write block
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_boot_config(struct area_block *boot_config,
		struct tcm_boot_info *boot_info, unsigned int block_size)
{
	unsigned int start_block;
	unsigned int image_addr;
	unsigned int device_addr;

	if (!boot_config) {
		LOGE("Invalid boot_config block data\n");
		return -ERR_INVAL;
	}

	if (!boot_info) {
		LOGE("Invalid boot_info\n");
		return -ERR_INVAL;
	}

	if (boot_config->size < MIN_TDDI_BOOT_CONFIG_SIZE) {
		LOGE("No valid BOOT_CONFIG size, %d, in image file\n",
			boot_config->size);
		return -ERR_INVAL;
	}

	image_addr = boot_config->flash_addr;

	LOGD("Boot Config address in image file: 0x%x\n", image_addr);

	start_block = VALUE(boot_info->boot_config_start_block);
	device_addr = start_block * block_size;

	LOGD("Boot Config address in device: 0x%x\n", device_addr);

	/* unless you know all details; otherwise, it's better not to update the boot config */
	return 0;
}
/**
 * @brief   Check whether the DISP CONFIG area is valid.
 *
 * @param
 *    [ in] disp_config:     block data of disp_config from image file
 *    [ in] boot_info:       data of boot info
 *    [ in] block_size:      max size of write block
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_disp_config(struct area_block *disp_config,
		struct tcm_boot_info *boot_info, unsigned int block_size)
{
	unsigned int temp;
	unsigned int image_addr;
	unsigned int image_size;
	unsigned int device_addr;
	unsigned int device_size;

	if (!disp_config) {
		LOGE("Invalid disp_config block data\n");
		return -ERR_INVAL;
	}

	if (!boot_info) {
		LOGE("Invalid boot_info\n");
		return -ERR_INVAL;
	}

	/* disp_config area may not be included in all product */
	if (disp_config->size == 0) {
		LOGD("No DISP_CONFIG in image file\n");
		return 0;
	}

	image_addr = disp_config->flash_addr;
	image_size = disp_config->size;

	LOGD("Disp Config address in image file: 0x%x, size: %d\n",
		image_addr, image_size);

	temp = VALUE(boot_info->display_config_start_block);
	device_addr = temp * block_size;

	temp = VALUE(boot_info->display_config_length_blocks);
	device_size = temp * block_size;

	LOGD("Disp Config address in device: 0x%x, size: %d\n",
		device_addr, device_size);

	if (image_addr != device_addr)
		LOGW("Disp Config address mismatch, image:0x%x, dev:0x%x\n",
			image_addr, device_addr);

	if (image_size != device_size)
		LOGW("Disp Config address size mismatch, image:%d, dev:%d\n",
			image_size, device_size);

	return image_size;
}
/**
 * @brief   Check whether the APP CONFIG area is valid.
 *
 * @param
 *    [ in] app_config:      block data of app_config from image file
 *    [ in] app_info:        data of application info
 *    [ in] block_size:      max size of write block
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_app_config(struct area_block *app_config,
		struct tcm_application_info *app_info, unsigned int block_size)
{
	unsigned int temp;
	unsigned int image_addr;
	unsigned int image_size;
	unsigned int device_addr;
	unsigned int device_size;

	if (!app_config) {
		LOGE("Invalid app_config block data\n");
		return -ERR_INVAL;
	}

	if (!app_info) {
		LOGE("Invalid app_info\n");
		return -ERR_INVAL;
	}

	if (app_config->size == 0) {
		LOGD("No APP_CONFIG in image file\n");
		return 0;
	}

	image_addr = app_config->flash_addr;
	image_size = app_config->size;

	LOGD("App Config address in image file: 0x%x, size: %d\n",
		image_addr, image_size);

	temp = VALUE(app_info->app_config_start_write_block);
	device_addr = temp * block_size;
	device_size = VALUE(app_info->app_config_size);

	LOGD("App Config address in device: 0x%x, size: %d\n",
		device_addr, device_size);

	if (device_addr == 0 && device_size == 0)
		return image_size;

	if (image_addr != device_addr)
		LOGW("App Config address mismatch, image:0x%x, dev:0x%x\n",
			image_addr, device_addr);

	if (image_size != device_size)
		LOGW("App Config address size mismatch, image:%d, dev:%d\n",
			image_size, device_size);

	return image_size;
}
/**
 * @brief   Check whether the APP CODE area is valid.
 *
 * @param
 *    [ in] app_code:      block data of app_code from image file
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_app_code(struct area_block *app_code)
{
	if (!app_code) {
		LOGE("Invalid app_code block data\n");
		return -ERR_INVAL;
	}

	if (app_code->size == 0) {
		LOGD("No %s in image file\n", AREA_ID_STR(app_code->id));
		return -ERR_INVAL;
	}

	return app_code->size;
}
/**
 * @brief   Check whether the openshort area is valid.
 *
 * @param
 *    [ in] open_short:      block data of open_short from image file
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_openshort(struct area_block *open_short)
{
	if (!open_short) {
		LOGE("Invalid open_short block data\n");
		return -ERR_INVAL;
	}

	/* open_short area may not be included in all product */
	if (open_short->size == 0) {
		LOGD("No %s in image file\n", AREA_ID_STR(open_short->id));
		return 0;
	}

	return open_short->size;
}
/**
 * @brief   Check whether the APP PROD_TEST area is valid.
 *
 * @param
 *    [ in] prod_test:  block data of app_prod_test from image file
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_app_prod_test(struct area_block *prod_test)
{
	if (!prod_test) {
		LOGE("Invalid app_prod_test block data\n");
		return -ERR_INVAL;
	}

	/* app_prod_test area may not be included in all product */
	if (prod_test->size == 0) {
		LOGD("No %s in image file\n", AREA_ID_STR(prod_test->id));
		return 0;
	}

	return prod_test->size;
}
/**
 * @brief   Check whether the PPDT area is valid.
 *
 * @param
 *    [ in] ppdt:      block data of PPDT from image file
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_ppdt(struct area_block *ppdt)
{
	if (!ppdt) {
		LOGE("Invalid ppdt block data\n");
		return -ERR_INVAL;
	}

	/* open_short area may not be included in all product */
	if (ppdt->size == 0) {
		LOGD("No %s in image file\n", AREA_ID_STR(ppdt->id));
		return 0;
	}

	return ppdt->size;
}
/**
 * @brief   Helper to ensure a valid data partition.
 *
 * @param
 *    [ in] tcm_dev:      the TouchComm device handle
 *    [ in] reflash_data: data blob for tddi reflash
 *    [ in] block:        target block area to check
 *
 * @return
 *    '0' represent no need to do reflash;
 *    the positive value means a valid partition to update;
 *    otherwise, negative value on error.
 */
static int syna_tcm_tddi_bootloader_check_block(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data,
	struct area_block *block)
{
	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!block) {
		LOGE("Invalid block data\n");
		return -ERR_INVAL;
	}

	switch (block->id) {
	case AREA_APP_CODE:
		return syna_tcm_tddi_bootloader_check_app_code(block);
	case AREA_APP_CONFIG:
		return syna_tcm_tddi_bootloader_check_app_config(block,
			&tcm_dev->app_info,
			reflash_data->write_block_size);
	case AREA_BOOT_CONFIG:
		return syna_tcm_tddi_bootloader_check_boot_config(block,
			&tcm_dev->boot_info,
			reflash_data->write_block_size);
	case AREA_DISP_CONFIG:
		return syna_tcm_tddi_bootloader_check_disp_config(block,
			&tcm_dev->boot_info,
			reflash_data->write_block_size);
	case AREA_OPEN_SHORT_TUNING:
		return syna_tcm_tddi_bootloader_check_openshort(block);
	case AREA_PROD_TEST:
		return syna_tcm_tddi_bootloader_check_app_prod_test(block);
	case AREA_PPDT:
		return syna_tcm_tddi_bootloader_check_ppdt(block);
	default:
		return 0;
	}

	return 0;
}

/**
 * @brief   Query the address and length of the particular data area.
 *
 * @param
 *    [ in] tcm_dev:       the device handle
 *    [ in] area:          specified area in flash memory
 *    [out] addr:          the flash address of the specified area returned
 *    [out] len:           the size of the specified area returned
 *    [ in] resp_reading:  delay time for response reading.
 *                          a positive value presents the time for polling;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_get_location(struct tcm_dev *tcm_dev,
	enum flash_area area, unsigned int *addr, unsigned int *len,
	unsigned int resp_reading)
{
	int retval = 0;
	unsigned char payload;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	switch (area) {
	case AREA_CUSTOM_LCM:
		payload = FLASH_LCM_DATA;
		break;
	case AREA_CUSTOM_OEM:
		payload = FLASH_OEM_DATA;
		break;
	case AREA_PPDT:
		payload = FLASH_PPDT_DATA;
		break;
	case AREA_FORCE_TUNING:
		payload = FLASH_FORCE_CALIB_DATA;
		break;
	case AREA_OPEN_SHORT_TUNING:
		payload = FLASH_OPEN_SHORT_TUNING_DATA;
		break;
	default:
		LOGE("Invalid flash area %d\n", area);
		return -ERR_INVAL;
	}

	retval = syna_tcm_tddi_bootloader_send_command(tcm_dev,
		CMD_GET_DATA_LOCATION,
		&payload,
		sizeof(payload),
		resp_reading);
	if (retval < 0) {
		LOGE("Fail to query the location of %s\n", AREA_ID_STR(area));
		goto exit;
	}

	if (tcm_dev->resp_buf.data_length < 4) {
		LOGE("Invalid length of resp, %d\n", tcm_dev->resp_buf.data_length);
		retval = -ERR_INVAL;
		goto exit;
	}

	*addr = syna_pal_le2_to_uint(&tcm_dev->resp_buf.buf[0]);
	*len = syna_pal_le2_to_uint(&tcm_dev->resp_buf.buf[2]);

	retval = 0;

exit:
	return retval;
}
/**
 * @brief   Assemble a TouchComm bootloader command to read data from the flash.
 *          Reads to the protected bootloader code or application code areas will
 *          read as 0.
 *
 * @param
 *    [ in] tcm_dev:      the TouchComm device handle
 *    [ in] address:      the address in flash memory to read
 *    [out] rd_data:      data retrieved
 *    [ in] rd_len:       length of data to be read
 *    [ in] rd_delay_us:  a short delay after the command executed
 *                        set 'DEFAULT_FLASH_READ_DELAY' to use default,
 *                        which is 10 us;
 *                        set '0' or 'RESP_IN_ATTN' to select ATTN-driven.
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_read_flash(struct tcm_dev *tcm_dev,
	unsigned int address, unsigned char *rd_data, unsigned int rd_len,
	unsigned int rd_delay_us)
{
	int retval = 0;
	unsigned int length_words;
	unsigned int flash_addr_words;
	unsigned char out[6] = { 0 };
	unsigned int resp_delay;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid rd_data buffer\n");
		return -ERR_INVAL;
	}

	if (address == 0 || rd_len == 0) {
		LOGE("Invalid flash address and length\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	length_words = rd_len / 2;
	flash_addr_words = address / 2;

	LOGD("Flash address: 0x%x (words: 0x%x), size: %d (words: %d)\n",
		address, flash_addr_words, rd_len, length_words);

	if (rd_delay_us == CMD_RESPONSE_IN_ATTN) {
		LOGD("xfer: %d, delay: ATTN-driven\n", length_words);
		resp_delay = CMD_RESPONSE_IN_ATTN;
	} else {
		resp_delay = (rd_delay_us * length_words) / 1000;
		LOGD("xfer: %d, delay: %d ms\n", length_words, resp_delay);
	}

	out[0] = (unsigned char)flash_addr_words;
	out[1] = (unsigned char)(flash_addr_words >> 8);
	out[2] = (unsigned char)(flash_addr_words >> 16);
	out[3] = (unsigned char)(flash_addr_words >> 24);
	out[4] = (unsigned char)length_words;
	out[5] = (unsigned char)(length_words >> 8);

	retval = syna_tcm_tddi_bootloader_send_command(tcm_dev,
		CMD_READ_FLASH,
		out,
		sizeof(out),
		resp_delay);
	if (retval < 0) {
		LOGE("Fail to read flash data from addr 0x%x, size %d\n",
			address, rd_len);
		goto exit;
	}

	if (tcm_dev->resp_buf.data_length != rd_len) {
		LOGE("Fail to read requested length %d, rd_len %d\n",
			tcm_dev->resp_buf.data_length, rd_len);
		retval = -ERR_INVAL;
		goto exit;
	}

	retval = syna_pal_mem_cpy(rd_data,
		rd_len,
		tcm_dev->resp_buf.buf,
		tcm_dev->resp_buf.buf_size,
		rd_len);
	if (retval < 0) {
		LOGE("Fail to copy read data, size %d\n", rd_len);
		goto exit;
	}

exit:
	return retval;
}
/**
 * @brief   Read the data of CUSTOM OTP area in the flash memory.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for reflash
 *    [out] rd_data:         buffer used for storing the retrieved data
 *    [ in] resp_reading:    method to read in the response
 *                            a positive value presents the us time delay for the processing
 *                            of each WORDs in the flash to read;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay: method to switch the fw mode
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_read_flash_custom_otp(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int resp_reading, unsigned int fw_switch_delay)
{
	int retval;
	unsigned int temp;
	unsigned int addr = 0;
	unsigned int length = 0;
	struct tcm_boot_info *boot_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	boot_info = &tcm_dev->boot_info;

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		retval = syna_tcm_tddi_bootloader_preparations(tcm_dev,
			reflash_data, resp_reading, fw_switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter tddi bootloader\n");
			return retval;
		}
	}

	temp = VALUE(boot_info->custom_otp_start_block);
	addr = temp * reflash_data->write_block_size;
	temp = VALUE(boot_info->custom_otp_length_blocks);
	length = temp * reflash_data->write_block_size;

	if (addr == 0 || length == 0) {
		LOGE("CUSTOM_OTP data area unavailable\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGD("CUSTOM_OTP address: 0x%x, length: %d\n", addr, length);

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_tddi_bootloader_read_flash(tcm_dev, addr,
		rd_data->buf, length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read CUSTOM_OTP area (addr: 0x%x, length: %d)\n",
			addr, length);
		goto exit;
	}

	rd_data->data_length = length;

exit:
	return retval;
}
/**
 * @brief   Read the data of DISPLAY CONFIG area in the flash memory.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for reflash
 *    [out] rd_data:         buffer used for storing the retrieved data
 *    [ in] resp_reading:    method to read in the response
 *                            a positive value presents the us time delay for the processing
 *                            of each WORDs in the flash to read;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay: method to switch the fw mode
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_read_flash_disp_config(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int resp_reading, unsigned int fw_switch_delay)
{
	int retval;
	unsigned int temp;
	unsigned int addr = 0;
	unsigned int length = 0;
	struct tcm_boot_info *boot_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	boot_info = &tcm_dev->boot_info;

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		retval = syna_tcm_tddi_bootloader_preparations(tcm_dev,
			reflash_data, resp_reading, fw_switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter tddi bootloader\n");
			return retval;
		}
	}

	temp = VALUE(boot_info->display_config_start_block);
	addr = temp * reflash_data->write_block_size;
	temp = VALUE(boot_info->display_config_length_blocks);
	length = temp * reflash_data->write_block_size;

	if (addr == 0 || length == 0) {
		LOGE("DISP_CONFIG data area unavailable\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGD("DISP_CONFIG address: 0x%x, length: %d\n", addr, length);

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_tddi_bootloader_read_flash(tcm_dev, addr,
		rd_data->buf, length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read DISP_CONFIG area (addr: 0x%x, length: %d)\n",
			addr, length);
		goto exit;
	}

	rd_data->data_length = length;

exit:
	return retval;
}
/**
 * @brief   Read the data of BOOT CONFIG area in the flash memory.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for reflash
 *    [out] rd_data:         buffer used for storing the retrieved data
 *    [ in] resp_reading:    method to read in the response
 *                            a positive value presents the us time delay for the processing
 *                            of each WORDs in the flash to read;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay: method to switch the fw mode
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_read_flash_boot_config(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int resp_reading, unsigned int fw_switch_delay)
{
	int retval;
	unsigned int temp;
	unsigned int addr = 0;
	unsigned int length = 0;
	struct tcm_boot_info *boot_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	boot_info = &tcm_dev->boot_info;

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		retval = syna_tcm_tddi_bootloader_preparations(tcm_dev,
			reflash_data, resp_reading, fw_switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter tddi bootloader\n");
			return retval;
		}
	}

	temp = VALUE(boot_info->boot_config_start_block);
	addr = temp * reflash_data->write_block_size;
	length = MIN_TDDI_BOOT_CONFIG_SIZE * TDDI_BOOT_CONFIG_SLOTS;

	if (addr == 0 || length == 0) {
		LOGE("BOOT_CONFIG data area unavailable\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGD("BOOT_CONFIG address: 0x%x, length: %d\n", addr, length);

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_tddi_bootloader_read_flash(tcm_dev, addr,
		rd_data->buf, length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read BOOT_CONFIG area (addr: 0x%x, length: %d)\n",
			addr, length);
		goto exit;
	}

	rd_data->data_length = length;

exit:
	return retval;
}
/**
 * @brief   Read the data of APP CONFIG area in the flash memory.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for reflash
 *    [out] rd_data:         buffer used for storing the retrieved data
 *    [ in] resp_reading:    method to read in the response
 *                            a positive value presents the us time delay for the processing
 *                            of each WORDs in the flash to read;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay: method to switch the fw mode
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_read_flash_app_config(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, struct tcm_buffer *rd_data,
	unsigned int resp_reading, unsigned int fw_switch_delay)
{
	int retval;
	unsigned int temp;
	unsigned int addr = 0;
	unsigned int length = 0;
	struct tcm_application_info *app_info;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	app_info = &tcm_dev->app_info;

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		retval = syna_tcm_tddi_bootloader_preparations(tcm_dev,
			reflash_data, resp_reading, fw_switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter tddi bootloader\n");
			return retval;
		}
	}

	temp = VALUE(app_info->app_config_start_write_block);
	addr = temp * reflash_data->write_block_size;
	length = VALUE(app_info->app_config_size);

	if (addr == 0 || length == 0) {
		LOGE("APP_CONFIG data area unavailable\n");
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGD("APP_CONFIG address: 0x%x, length: %d\n", addr, length);

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_tddi_bootloader_read_flash(tcm_dev, addr,
		rd_data->buf, length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read APP_CONFIG area (addr: 0x%x, length: %d)\n",
			addr, length);
		goto exit;
	}

	rd_data->data_length = length;

exit:
	return retval;
}
/**
 * @brief   Read the data from the requested address in the flash memory.
 *
 * @param
 *    [ in] tcm_dev:         the TouchComm device handle
 *    [ in] reflash_data:    data blob for reflash
 *    [ in] area:            area to read
 *    [out] rd_data:         buffer used for storing the retrieved data
 *    [ in] resp_reading:    method to read in the response
 *                            a positive value presents the us time delay for the processing
 *                            of each WORDs in the flash to read;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *    [ in] fw_switch_delay: method to switch the fw mode
 *                            a positive value presents the ms time delay for polling;
 *                            or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_read_flash_custom_data(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, enum flash_area area,
	struct tcm_buffer *rd_data, unsigned int resp_reading, unsigned int switch_delay)
{
	int retval;
	unsigned int addr = 0;
	unsigned int length = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid read data buffer\n");
		return -ERR_INVAL;
	}

	retval = syna_tcm_tddi_bootloader_get_location(tcm_dev, area,
		&addr, &length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to get data location of 0x%x\n", area);
		return retval;
	}

	LOGD("Custom data area, %s, address: 0x%x, length: %d\n",
		AREA_ID_STR(area), addr, length);

	addr *= reflash_data->write_block_size;
	length *= reflash_data->write_block_size;

	if (addr == 0 || length == 0) {
		LOGE("Custom data area unavailable, address: 0x%x, length: %d\n",
			addr, length);
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGD("Custom data address: 0x%x, length: %d\n", addr, length);

	if (IS_APP_FW_MODE(tcm_dev->dev_mode)) {
		retval = syna_tcm_tddi_bootloader_preparations(tcm_dev,
			reflash_data, resp_reading, switch_delay);
		if (retval < 0) {
			LOGE("Fail to enter tddi bootloader\n");
			return retval;
		}
	}

	retval = syna_tcm_buf_alloc(rd_data, length);
	if (retval < 0) {
		LOGE("Fail to allocate memory for rd_data buffer\n");
		goto exit;
	}

	retval = syna_tcm_tddi_bootloader_read_flash(tcm_dev, addr,
		rd_data->buf, length, resp_reading);
	if (retval < 0) {
		LOGE("Fail to read custom data area (addr: 0x%x, length: %d)\n",
			addr, length);
		goto exit;
	}

	rd_data->data_length = length;

exit:
	return retval;
}
/**
 * @brief   Assemble a TouchComm bootloader command to write data to the flash.
 *          Until this command completes, the device may be unresponsive.
 *
 * @param
 *    [ in] tcm_dev:      the TouchComm device handle
 *    [ in] reflash_data: data blob for tddi reflash
 *    [ in] address:      the address in flash memory to write
 *    [ in] wr_data:      data to write
 *    [ in] wr_len:       length of data to write
 *    [ in] wr_delay_us:  a short delay after the command executed
 *                         set 'DEFAULT_FLASH_WRITE_DELAY' to use default,
 *                         which is 20 us;
 *                         set '0' or 'RESP_IN_ATTN' to select ATTN-driven.
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_write_flash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int address,
	const unsigned char *wr_data, unsigned int wr_len, unsigned int wr_delay_us)
{
	int retval;
	unsigned int offset;
	unsigned int w_length;
	unsigned int xfer_length;
	unsigned int remaining_length;
	unsigned int flash_address;
	unsigned int block_address;
	unsigned int num_blocks;
	unsigned int resp_delay;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	/* ensure that the length to write is the multiple of max_write_payload_size */
	w_length = reflash_data->max_write_payload_size;
	w_length -= (w_length % reflash_data->write_block_size);

	offset = 0;

	remaining_length = wr_len;

	syna_tcm_buf_lock(&reflash_data->out);

	while (remaining_length) {
		if (remaining_length > w_length)
			xfer_length = w_length;
		else
			xfer_length = remaining_length;

		retval = syna_tcm_buf_alloc(&reflash_data->out, xfer_length + 2);
		if (retval < 0) {
			LOGE("Fail to allocate memory for buf.out\n");
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		flash_address = address + offset;
		block_address = flash_address / reflash_data->write_block_size;
		reflash_data->out.buf[0] = (unsigned char)block_address;
		reflash_data->out.buf[1] = (unsigned char)(block_address >> 8);

		num_blocks = syna_pal_ceil_div(xfer_length, reflash_data->write_block_size);

		if (wr_delay_us == CMD_RESPONSE_IN_ATTN) {
			LOGD("xfer: %d (blocks: %d), delay: ATTN-driven\n", xfer_length, num_blocks);
			resp_delay = CMD_RESPONSE_IN_ATTN;
		} else {
			resp_delay = (wr_delay_us * num_blocks) / 1000;
			LOGD("xfer: %d (blocks: %d), delay: %d ms\n", xfer_length, num_blocks, resp_delay);
		}

		retval = syna_pal_mem_cpy(&reflash_data->out.buf[2],
				reflash_data->out.buf_size - 2,
				&wr_data[offset],
				wr_len - offset,
				xfer_length);
		if (retval < 0) {
			LOGE("Fail to copy write data ,size: %d\n",
				xfer_length);
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		retval = syna_tcm_tddi_bootloader_send_command(tcm_dev,
				CMD_WRITE_FLASH,
				reflash_data->out.buf,
				xfer_length + 2,
				resp_delay);
		if (retval < 0) {
			LOGE("Fail to write data to flash addr 0x%x, size %d\n",
				flash_address, xfer_length + 2);
			syna_tcm_buf_unlock(&reflash_data->out);
			return retval;
		}

		offset += xfer_length;
		remaining_length -= xfer_length;
	}

	syna_tcm_buf_unlock(&reflash_data->out);

	return 0;
}
/**
 * @brief   Write data to the target data partition in TDDI bootloader.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [ in] reflash_data:  data blob for tddi reflash
 *    [ in] area:          target block area to write
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each BLOCKs in the flash to write;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_write_flash_block(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, struct area_block *block,
	unsigned int resp_reading)
{
	int retval;
	unsigned int size;
	unsigned int flash_addr;
	const unsigned char *data;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!block) {
		LOGE("Invalid block data\n");
		return -ERR_INVAL;
	}

	data = block->data;
	size = block->size;
	flash_addr = block->flash_addr;

	LOGD("Write data to %s - address: 0x%x, size: %d\n",
		AREA_ID_STR(block->id), flash_addr, size);

	if (size == 0) {
		LOGI("No need to update, size = %d\n", size);
		goto exit;
	}

	if (flash_addr % reflash_data->write_block_size != 0) {
		LOGE("Flash writes (address:0x%x) not starting on block boundary\n",
			flash_addr);
		return -ERR_INVAL;
	}

	retval = syna_tcm_tddi_bootloader_write_flash(tcm_dev, reflash_data,
			flash_addr, data, size, resp_reading);
	if (retval < 0) {
		LOGE("Fail to write %s to flash (addr: 0x%x, size: %d)\n",
			AREA_ID_STR(block->id), flash_addr, size);
		return retval;
	}

exit:
	LOGN("%s area written\n", AREA_ID_STR(block->id));

	return 0;
}
/**
 * @brief   Assemble a TouchComm bootloader command to erase the flash.
 *          Until this command completes, the device may be unresponsive.
 *
 * @param
 *    [ in] tcm_dev:        the TouchComm device handle
 *    [ in] reflash_data:   data blob for tddi reflash
 *    [ in] address:        the address in flash memory to read
 *    [ in] size:           size of data to write
 *    [ in] erase_delay_ms: a short delay after the command executed
 *                          set a positive value or 'DEFAULT_FLASH_ERASE_DELAY' to use default;
 *                          set '0' or 'RESP_IN_ATTN' to select ATTN-driven.
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_erase_flash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, unsigned int address,
	unsigned int size, unsigned int erase_delay_ms)
{
	int retval;
	unsigned int page_start = 0;
	unsigned int page_count = 0;
	unsigned char out_buf[4] = { 0 };
	int size_erase_cmd;
	unsigned int resp_delay;

	page_start = address / reflash_data->page_size;

	page_count = syna_pal_ceil_div(size, reflash_data->page_size);

	if (erase_delay_ms == CMD_RESPONSE_IN_ATTN) {
		resp_delay = CMD_RESPONSE_IN_ATTN;
		LOGD("Page start = %d (0x%04x), Page count = %d (0x%04x), delay: ATTN-driven\n",
			page_start, page_start, page_count, page_count);
	} else {
		resp_delay = erase_delay_ms * page_count;
		LOGD("Page start = %d (0x%04x), Page count = %d (0x%04x), delay: %d ms\n",
			page_start, page_start, page_count, page_count, resp_delay);
	}

	if ((page_start > 0xff) || (page_count > 0xff)) {
		size_erase_cmd = 4;

		out_buf[0] = (unsigned char)(page_start & 0xff);
		out_buf[1] = (unsigned char)((page_start >> 8) & 0xff);
		out_buf[2] = (unsigned char)(page_count & 0xff);
		out_buf[3] = (unsigned char)((page_count >> 8) & 0xff);
	} else {
		size_erase_cmd = 2;

		out_buf[0] = (unsigned char)(page_start & 0xff);
		out_buf[1] = (unsigned char)(page_count & 0xff);
	}

	retval = syna_tcm_tddi_bootloader_send_command(tcm_dev,
			CMD_ERASE_FLASH,
			out_buf,
			size_erase_cmd,
			resp_delay);
	if (retval < 0) {
		LOGE("Fail to erase data at flash page 0x%x, count %d\n",
			page_start, page_count);
		return retval;
	}

	return 0;
}

/**
 * @brief   Erase the target data partition in TDDI bootloader.
 *
 * @param
 *    [ in] tcm_dev:      the TouchComm device handle
 *    [ in] reflash_data: data blob for tddi reflash
 *    [ in] block:        target block area to erase
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each PAGEs in the flash to erase;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_erase_flash_block(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, struct area_block *block,
	unsigned int resp_reading)
{
	int retval;
	unsigned int size;
	unsigned int flash_addr;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash data blob\n");
		return -ERR_INVAL;
	}

	if (!block) {
		LOGE("Invalid block data\n");
		return -ERR_INVAL;
	}

	flash_addr = block->flash_addr;
	size = block->size;

	LOGD("Erase %s block - address: 0x%x, size: %d\n",
		AREA_ID_STR(block->id), flash_addr, size);

	if (size == 0) {
		LOGI("No need to erase, size = %d\n", size);
		goto exit;
	}

	if (flash_addr % reflash_data->page_size != 0) {
		LOGE("Flash erases (address:0x%x) not starting on page boundary\n",
			flash_addr);
		return -ERR_INVAL;
	}

	retval = syna_tcm_tddi_bootloader_erase_flash(tcm_dev, reflash_data,
			flash_addr, size, resp_reading);
	if (retval < 0) {
		LOGE("Fail to erase %s data (addr: 0x%x, size: %d)\n",
			AREA_ID_STR(block->id), flash_addr, size);
		return retval;
	}

exit:
	LOGN("%s area erased\n", AREA_ID_STR(block->id));

	return 0;
}
/**
 * @brief   Implement the sequence of firmware update in TDDI bootloader.
 *
 * @param
 *    [ in] tcm_dev:                the TouchComm device handle
 *    [ in] reflash_data:           data blob for tddi reflash
 *    [ in] type:                   the area to update
 *    [ in] flash_delay_settings:   set up the us delay time to wait for the completion of flash access
 *                                    for polling,     set a value formatted with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_tddi_bootloader_do_reflash(struct tcm_dev *tcm_dev,
	struct tcm_tddi_reflash_data_blob *reflash_data, enum update_tddi_area type,
	unsigned int flash_delay_settings)
{
	int retval = 0;
	int idx;
	unsigned int flash_erase_delay_ms;
	unsigned int flash_write_delay_us;
	struct image_info *image_info;
	struct area_block *block;
	bool done = false;
	unsigned int fw_switch_time;
	unsigned int resp_handling;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!reflash_data) {
		LOGE("Invalid reflash_data blob\n");
		return -ERR_INVAL;
	}

	image_info = &reflash_data->image_info;

	if (flash_delay_settings == CMD_RESPONSE_IN_ATTN) {
		flash_erase_delay_ms = flash_delay_settings;
		flash_write_delay_us = flash_delay_settings;
		fw_switch_time = flash_delay_settings;
		resp_handling = flash_delay_settings;
	} else {
		flash_erase_delay_ms = (flash_delay_settings >> 16) & 0xFFFF;
		flash_write_delay_us = flash_delay_settings & 0xFFFF;
		fw_switch_time = tcm_dev->fw_mode_switching_time;
		resp_handling = tcm_dev->msg_data.command_polling_time;
	}

	if (type == UPDATE_TDDI_NONE)
		goto exit;

	/* enter the tddi bootloader mode */
	retval = syna_tcm_tddi_bootloader_preparations(tcm_dev,
			reflash_data, resp_handling, fw_switch_time);
	if (retval < 0) {
		LOGE("Fail to do tddi bootloader preparations\n");
		goto exit;
	}

	if (tcm_dev->dev_mode != MODE_TDDI_BOOTLOADER) {
		LOGE("Incorrect bootloader mode, 0x%02x, expected: 0x%02x\n",
			tcm_dev->dev_mode, MODE_TDDI_BOOTLOADER);
		return -ERR_INVAL;
	}

	/* Always mass erase all blocks, before writing the data */
	for (idx = 0; idx < AREA_MAX; idx++) {

		if (type == UPDATE_TDDI_CONFIG) {
			if (idx != AREA_APP_CONFIG)
				continue;
		}

		if (idx == AREA_TOOL_BOOT_CONFIG)
			continue;

		block = &image_info->data[idx];
		if (!block->available)
			continue;

		retval = syna_tcm_tddi_bootloader_check_block(tcm_dev, reflash_data, block);
		if (retval <= 0)
			continue;

		LOGN("Prepare to erase %s area\n", AREA_ID_STR(block->id));

		retval = syna_tcm_tddi_bootloader_erase_flash_block(tcm_dev, reflash_data,
				block, flash_erase_delay_ms);
		if (retval < 0) {
			LOGE("Fail to erase %s area\n", AREA_ID_STR(block->id));
			goto exit;
		}
	}

	/* Write all the data to flash */
	for (idx = 0; idx < AREA_MAX; idx++) {

		if (type == UPDATE_TDDI_CONFIG) {
			if (idx != AREA_APP_CONFIG)
				continue;
		}

		block = &image_info->data[idx];
		if (!block->available)
			continue;

		retval = syna_tcm_tddi_bootloader_check_block(tcm_dev, reflash_data, block);
		if (retval <= 0)
			continue;

		LOGN("Prepare to update %s area\n", AREA_ID_STR(block->id));

		retval = syna_tcm_tddi_bootloader_write_flash_block(tcm_dev,
				reflash_data,
				block,
				flash_write_delay_us);
		if (retval < 0) {
			LOGE("Fail to update %s area\n",
				AREA_ID_STR(block->id));
			goto exit;
		}
	}

	done = true;

exit:
	retval = syna_tcm_reset(tcm_dev, resp_handling);
	if (retval < 0) {
		LOGE("Fail to do reset\n");
		goto exit;
	} else {
		if (done && (!IS_APP_FW_MODE(tcm_dev->dev_mode)))
			syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE,
				fw_switch_time);
	}

	return retval;
}

/**
 * @brief   The entry function to perform firmware update by the firmware image file.
 *
 * @param
 *    [ in] tcm_dev:                the TouchComm device handle
 *    [ in] image:                  binary data to write
 *    [ in] image_size:             size of data array
 *    [ in] flash_delay_settings:   set up the us delay time to wait for the completion of flash access
 *                                    for polling,     set a value formatted with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] force_reflash:          '1' to do reflash anyway
 *                                  '0' to compare ID info before doing reflash.
 *    [ in] is_multichip:           flag to indicate a multi-chip product used
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_tddi_do_fw_update(struct tcm_dev *tcm_dev, const unsigned char *image,
	unsigned int image_size, unsigned int flash_delay_settings, bool force_reflash,
	bool is_multichip)
{
	int retval;
	int type = (int) UPDATE_TDDI_NONE;
	struct tcm_tddi_reflash_data_blob *reflash_data = NULL;
	int app_status;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!image) || (image_size == 0)) {
		LOGE("Invalid image data\n");
		return -ERR_INVAL;
	}

	reflash_data = syna_pal_mem_alloc(1, sizeof(struct tcm_tddi_reflash_data_blob));
	if (!reflash_data) {
		LOGE("Fail to allocate data blob for tddi\n");
		return -ERR_NOMEM;
	}

	LOGN("Prepare to do reflash\n");

	syna_tcm_buf_init(&reflash_data->out);

	reflash_data->bin_data = image;
	reflash_data->bin_data_size = image_size;
	syna_pal_mem_set(&reflash_data->image_info, 0x00, sizeof(struct image_info));

	/* parse the image file */
	retval = syna_tcm_parse_fw_image(image, &reflash_data->image_info);
	if (retval < 0) {
		LOGE("Fail to parse firmware image\n");
		goto exit;
	}

	app_status = syna_pal_le2_to_uint(tcm_dev->app_info.status);

	/* to forcedly update the firmware and config
	 *   - flag of 'force_reflash' has been set
	 *   - device stays in bootloader
	 *   - app firmware doesn't run properly
	 */
	if (IS_TDDI_BOOTLOADER_MODE(tcm_dev->dev_mode))
		force_reflash = true;
	if (IS_APP_FW_MODE(tcm_dev->dev_mode) && (app_status != APP_STATUS_OK))
		force_reflash = true;

	if (force_reflash) {
		type = UPDATE_TDDI_FIRMWARE_AND_CONFIG;
		goto reflash;
	}

	/* determine the partitions to update  */
	type = syna_tcm_compare_tddi_image_id_info(tcm_dev, reflash_data);

	if (type == UPDATE_TDDI_NONE)
		goto exit;

reflash:
	LOGN("Start of reflash\n");
	ATOMIC_SET(tcm_dev->firmware_flashing, 1);

	if (is_multichip) {
		retval = syna_tcm_tddi_romboot_do_reflash(tcm_dev,
				reflash_data,
				is_multichip,
				flash_delay_settings);
		if (retval < 0) {
			LOGE("Fail to do firmware update for tddi mc\n");
			goto exit;
		}
	} else {
		retval = syna_tcm_tddi_bootloader_do_reflash(tcm_dev,
				reflash_data,
				type,
				flash_delay_settings);
		if (retval < 0) {
			LOGE("Fail to do firmware update for tddi sc\n");
			goto exit;
		}
	}

	LOGN("End of reflash\n");

	retval = 0;

exit:
	ATOMIC_SET(tcm_dev->firmware_flashing, 0);

	syna_tcm_buf_release(&reflash_data->out);
	syna_pal_mem_free(reflash_data);

	return retval;
}

/**
 * @brief   The entry function to perform firmware update by the firmware ihex file.
 *
 * @param
 *    [ in] tcm_dev:                the TouchComm device handle
 *    [ in] ihex:                   ihex data to write
 *    [ in] ihex_size:              size of ihex data
 *    [ in] flash_delay_settings:   set up the us delay time to wait for the completion of flash access
 *                                    for polling,     set a value formatted with [erase ms | write us];
 *                                    for ATTN-driven, use '0' or 'RESP_IN_ATTN'
 *    [ in] is_multichip:           flag to indicate a multi-chip product used
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_tddi_do_fw_update_ihex(struct tcm_dev *tcm_dev, const unsigned char *ihex,
	unsigned int ihex_size, unsigned int flash_delay_settings, bool is_multichip)
{
	int retval;
	struct tcm_tddi_reflash_data_blob *reflash_data = NULL;
	struct ihex_info *ihex_info = NULL;
	unsigned int flash_size;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if ((!ihex) || (ihex_size == 0)) {
		LOGE("Invalid ihex data\n");
		return -ERR_INVAL;
	}

	reflash_data = syna_pal_mem_alloc(1, sizeof(struct tcm_tddi_reflash_data_blob));
	if (!reflash_data) {
		LOGE("Fail to allocate data blob for tddi\n");
		return -ERR_NOMEM;
	}

	flash_size = ihex_size + 4096;

	syna_tcm_buf_init(&reflash_data->out);

	reflash_data->bin_data = ihex;
	reflash_data->bin_data_size = ihex_size;
	syna_pal_mem_set(&reflash_data->ihex_info, 0x00, sizeof(struct ihex_info));

	ihex_info = &reflash_data->ihex_info;

	ihex_info->bin_data = syna_pal_mem_alloc(flash_size, sizeof(unsigned char));
	if (!ihex_info->bin_data) {
		LOGE("Fail to allocate buffer for ihex data\n");
		retval = -ERR_NOMEM;
		goto exit;
	}

	ihex_info->bin_data_size = flash_size;

	LOGN("Prepare to do ihex update\n");

	/* parse the ihex file */
	retval = syna_tcm_parse_fw_ihex((const char *)ihex, ihex_size, ihex_info);
	if (retval < 0) {
		LOGE("Fail to parse firmware ihex file\n");
		goto exit;
	}

	LOGN("Start of ihex update\n");
	ATOMIC_SET(tcm_dev->firmware_flashing, 1);

	retval = syna_tcm_tddi_romboot_do_program(tcm_dev,
			reflash_data,
			is_multichip,
			flash_delay_settings);
	if (retval < 0) {
		LOGE("Fail to do ihex update for tddi %s\n", (is_multichip) ? "mc" : "sc");
		goto exit;
	}

	LOGN("End of ihex update\n");

	retval = 0;

exit:
	if (ihex_info->bin_data)
		syna_pal_mem_free((void *)ihex_info->bin_data);

	ATOMIC_SET(tcm_dev->firmware_flashing, 0);

	syna_tcm_buf_release(&reflash_data->out);
	syna_pal_mem_free(reflash_data);

	return retval;
}
/**
 * @brief   Query the TDDI ROM-Boot information.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [out] rom_boot_info: the romboot info packet returned
 *    [ in] resp_reading:  delay time for response reading.
 *                          a positive value presents the time for polling;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_tddi_get_romboot_info(struct tcm_dev *tcm_dev,
	struct tcm_romboot_info *rom_boot_info, unsigned int resp_reading)
{
	int retval = 0;
	unsigned int copy_size = 0;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	retval = tcm_dev->write_message(tcm_dev,
			CMD_GET_ROMBOOT_INFO,
			NULL,
			0,
			NULL,
			resp_reading);
	if (retval < 0) {
		LOGE("Fail to send command 0x%02x\n",
			CMD_GET_ROMBOOT_INFO);
		goto exit;
	}

	if (rom_boot_info == NULL)
		goto exit;

	copy_size = min(sizeof(struct tcm_romboot_info),
		tcm_dev->resp_buf.data_length);

	/* copy romboot_info to caller */
	retval = syna_pal_mem_cpy((unsigned char *)rom_boot_info,
			sizeof(struct tcm_romboot_info),
			tcm_dev->resp_buf.buf,
			tcm_dev->resp_buf.buf_size,
			copy_size);
	if (retval < 0) {
		LOGE("Fail to copy romboot info to caller\n");
		goto exit;
	}

exit:
	return retval;
}
/**
 * @brief   Request to read out data from the flash memory.
 *
 * @param
 *    [ in] tcm_dev:       the TouchComm device handle
 *    [ in] area:          flash area to read
 *    [out] rd_data:       buffer storing the retrieved data
 *    [ in] resp_reading:  method to read in the response
 *                          a positive value presents the us time delay for the processing
 *                          of each WORDs in the flash to read;
 *                          or, set '0' or 'RESP_IN_ATTN' for ATTN driven
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
int syna_tcm_tddi_read_flash_area(struct tcm_dev *tcm_dev, enum flash_area area,
	struct tcm_buffer *rd_data, unsigned int resp_reading)
{
	int retval;
	struct tcm_tddi_reflash_data_blob *reflash_data = NULL;
	unsigned char origin;
	unsigned int fw_switch_time;

	if (!tcm_dev) {
		LOGE("Invalid tcm device handle\n");
		return -ERR_INVAL;
	}

	if (!rd_data) {
		LOGE("Invalid data buffer\n");
		return -ERR_INVAL;
	}

	reflash_data = syna_pal_mem_alloc(1, sizeof(struct tcm_tddi_reflash_data_blob));
	if (!reflash_data) {
		LOGE("Fail to allocate data blob for tddi\n");
		return -ERR_NOMEM;
	}

	if (resp_reading == CMD_RESPONSE_IN_ATTN)
		fw_switch_time = resp_reading;
	else
		fw_switch_time = tcm_dev->fw_mode_switching_time;

	origin = tcm_dev->dev_mode;

	syna_tcm_buf_init(&reflash_data->out);

	switch (area) {
	case AREA_BOOT_CONFIG:
		retval = syna_tcm_tddi_bootloader_read_flash_boot_config(tcm_dev,
			reflash_data, rd_data, resp_reading, fw_switch_time);
		if (retval < 0) {
			LOGE("Fail to get boot config data\n");
			goto exit;
		}
		break;
	case AREA_APP_CONFIG:
		retval = syna_tcm_tddi_bootloader_read_flash_app_config(tcm_dev,
			reflash_data, rd_data, resp_reading, fw_switch_time);
		if (retval < 0) {
			LOGE("Fail to get app config data\n");
			goto exit;
		}
		break;
	case AREA_DISP_CONFIG:
		retval = syna_tcm_tddi_bootloader_read_flash_disp_config(tcm_dev,
			reflash_data, rd_data, resp_reading, fw_switch_time);
		if (retval < 0) {
			LOGE("Fail to get disp config data\n");
			goto exit;
		}
		break;
	case AREA_CUSTOM_OTP:
		retval = syna_tcm_tddi_bootloader_read_flash_custom_otp(tcm_dev,
			reflash_data, rd_data, resp_reading, fw_switch_time);
		if (retval < 0) {
			LOGE("Fail to get custom otp data\n");
			goto exit;
		}
		break;
	case AREA_CUSTOM_LCM:
	case AREA_CUSTOM_OEM:
	case AREA_PPDT:
	case AREA_FORCE_TUNING:
	case AREA_OPEN_SHORT_TUNING:
		retval = syna_tcm_tddi_bootloader_read_flash_custom_data(tcm_dev,
			reflash_data, area, rd_data, resp_reading, fw_switch_time);
		if (retval < 0) {
			LOGE("Fail to get custom data of %s\n", AREA_ID_STR(area));
			goto exit;
		}
		break;
	default:
		LOGE("Invalid data area %d\n", area);
		retval = -ERR_INVAL;
		goto exit;
	}

	LOGI("%s read\n", AREA_ID_STR(area));

	retval = 0;

exit:

	if (!IS_APP_FW_MODE(tcm_dev->dev_mode) && IS_APP_FW_MODE(origin))
		syna_tcm_switch_fw_mode(tcm_dev, MODE_APPLICATION_FIRMWARE, resp_reading);

	syna_tcm_buf_release(&reflash_data->out);
	syna_pal_mem_free(reflash_data);

	return retval;
}
