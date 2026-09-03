/* SPDX-License-Identifier: GPL-2.0-only */
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
 * @file synaptics_touchcom_func_reflash_ihex.h
 *
 * This file declares the functions and structures being used in the functions
 * related to the firmware update with ihex file.
 */

#ifndef _SYNAPTICS_TOUCHCOM_PARSE_FW_IHEX_H_
#define _SYNAPTICS_TOUCHCOM_PARSE_FW_IHEX_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "synaptics_touchcom_core_dev.h"


/*
 * Common Definitions
 */

#define IHEX_RECORD_SIZE (14)

#define IHEX_MAX_BLOCKS (64)


/** Definitions of a data area defined in the ihex file */
struct ihex_area_block {
	bool available;
	const unsigned char *data;
	unsigned int size;
	unsigned int flash_addr;
	unsigned char id;
};
/** Structure of binary data after the ihex file parsing */
struct ihex_info {
	unsigned int records;
	unsigned char *bin_data;
	unsigned int bin_data_size;
	struct ihex_area_block block[IHEX_MAX_BLOCKS];
};

/*
 * Common Helpers
 */

/**
 * @brief   Parse a line in the ihex file and convert into the binary data
 *
 * @param
 *    [ in] line:         a line of string stored in the ihex file
 *    [out] count:        size of actual data
 *    [out] addr:         address of data located
 *    [out] type:         the type of data belonging
 *    [out] buf:          a buffer to store the converted data
 *    [int] buf_size:     size of buffer
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static inline int syna_tcm_parse_ihex_line(char *line, unsigned int *count,
	unsigned int *addr, unsigned int *type, unsigned char *buf,
	unsigned int buf_size)
{
	const unsigned int OFFSET_COUNT = 1;
	const unsigned int SIZE_COUNT = 2;
	const unsigned int OFFSET_ADDR = OFFSET_COUNT + SIZE_COUNT;
	const unsigned int SIZE_ADDR = 4;
	const unsigned int OFFSET_TYPE = OFFSET_ADDR + SIZE_ADDR;
	const unsigned int SIZE_TYPE = 2;
	const unsigned int OFFSET_DATA = OFFSET_TYPE + SIZE_TYPE;
	const unsigned int SIZE_DATA = 2;
	unsigned int pos;

	if (!line) {
		LOGE("No string line\n");
		return -ERR_INVAL;
	}

	if ((!buf) || (buf_size == 0)) {
		LOGE("Invalid temporary data buffer\n");
		return -ERR_INVAL;
	}

	*count = syna_pal_hex_to_uint(line + OFFSET_COUNT, 2);
	*addr = syna_pal_hex_to_uint(line + OFFSET_ADDR, SIZE_ADDR);
	*type = syna_pal_hex_to_uint(line + OFFSET_TYPE, SIZE_TYPE);

	if (*count > buf_size) {
		LOGE("Data size mismatched, required:%d, given:%d\n",
			*count, buf_size);
		return -ERR_INVAL;
	}

	for (pos = 0; pos < *count; pos++)
		buf[pos] = (unsigned char)syna_pal_hex_to_uint(
			&line[(int)((pos << 1) + OFFSET_DATA)],
			SIZE_DATA);

	return 0;
}
/**
 * @brief   Parse and convert the given firmware ihex file into a binary data
 *          to update.
 *
 * @param
 *    [ in] ihex:         original ihex file
 *    [ in] ihex_size:    size of given file
 *    [out] ihex_info:    data blob stored the parsed data from an ihex file.
 *                        assume the data buffer inside was allocated
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static inline int syna_tcm_parse_fw_ihex(const char *ihex, int ihex_size,
	struct ihex_info *ihex_info)
{
	int retval;
	unsigned int pos;
	unsigned int record;
	char *tmp = NULL;
	unsigned int count;
	unsigned int type;
	unsigned char data[32] = { 0 };
	unsigned int addr;
	unsigned int offset;
	unsigned int prev_addr;
	unsigned int block_idx = 0;
	int len_per_line = -1;

	if (!ihex) {
		LOGE("No ihex data\n");
		return -ERR_INVAL;
	}

	if (!ihex_info) {
		LOGE("Invalid ihex_info blob\n");
		return -ERR_INVAL;
	}

	if ((!ihex_info->bin_data) || (ihex_info->bin_data_size == 0)) {
		LOGE("Invalid ihex_info->data\n");
		return -ERR_INVAL;
	}

	for (pos = 0; pos < 130; pos++) {
		if ((len_per_line > 0) && ((char)ihex[pos] == ':'))
			break;

		if ((char)ihex[pos] == ':')
			len_per_line = 0;

		if (len_per_line >= 0)
			len_per_line++;
	}

	if (len_per_line <= 0) {
		LOGE("Invalid length per line\n");
		return -ERR_INVAL;
	}

	tmp = (char *)syna_pal_mem_alloc(len_per_line + 1, sizeof(char));
	if (!tmp) {
		LOGE("Fail to allocate temporary buffer\n");
		return -ERR_NOMEM;
	}

	offset = 0;
	addr = 0;
	pos = 0;
	prev_addr = 0;

	ihex_info->records = ihex_size / len_per_line;
	LOGD("records:%d, len_per_line:%d\n", ihex_info->records, len_per_line);

	for (record = 0; record < ihex_info->records; record++) {
		pos = record * len_per_line;
		if ((char)ihex[pos] != ':') {
			LOGE("Invalid string maker at pos %d, marker:%c\n",
				pos, (char)ihex[pos]);
			goto exit;
		}

		retval = syna_pal_mem_cpy(tmp, len_per_line,
				&ihex[pos], ihex_size - pos, len_per_line);
		if (retval < 0) {
			LOGE("Fail to copy a line at pos %d\n", pos);
			goto exit;
		}

		retval = syna_tcm_parse_ihex_line(tmp, &count, &addr, &type,
			data, sizeof(data));
		if (retval < 0) {
			LOGE("Fail to parse line at pos %d\n", pos);
			goto exit;
		}

		if ((((prev_addr + 2) & 0xFFFF) != addr) && (type == 0x00)) {
			block_idx = (record == 0) ? 0 : block_idx + 1;
			if (block_idx >= IHEX_MAX_BLOCKS) {
				LOGE("Invalid block index\n");
				goto exit;
			}

			ihex_info->block[block_idx].flash_addr =
				addr + offset;
			ihex_info->block[block_idx].data =
				&ihex_info->bin_data[addr + offset];
			ihex_info->block[block_idx].available = true;
		}

		if (type == 0x00) {

			prev_addr = addr;
			addr += offset;

			if (addr >= ihex_info->bin_data_size) {
				LOGE("No enough size for data0 addr:0x%x(%d)\n",
					addr, addr);
				goto exit;
			}
			ihex_info->bin_data[addr++] = data[0];

			if (addr >= ihex_info->bin_data_size) {
				LOGE("No enough size for data1 addr:0x%x(%d)\n",
					addr, addr);
				goto exit;
			}
			ihex_info->bin_data[addr++] = data[1];

			ihex_info->block[block_idx].size += 2;

		} else if (type == 0x02) {
			offset = (data[0] << 8) + data[1];
			offset <<= 4;
		}
	}

	ihex_info->bin_data_size = addr; /* the actual size after data reordering */
	LOGN("Size of firmware binary data = %d\n", ihex_info->bin_data_size);

exit:
	syna_pal_mem_free((void *)tmp);

	return 0;
}


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* end of _SYNAPTICS_TOUCHCOM_PARSE_FW_IHEX_H_ */
