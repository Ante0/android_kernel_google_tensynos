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
 * @file synaptics_touchcom_func_reflash_image.h
 *
 * This file declares the functions and structures being used in the functions
 * related to the firmware update with image file.
 */

#ifndef _SYNAPTICS_TOUCHCOM_PARSE_FW_IMAGE_H_
#define _SYNAPTICS_TOUCHCOM_PARSE_FW_IMAGE_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "synaptics_touchcom_core_dev.h"


/*
 * Common Definitions
 */

#define ID_STRING_SIZE (32)

#define SIZE_WORDS (8)

#define IMAGE_FILE_MAGIC_VALUE (0x4818472b)

#define FLASH_AREA_MAGIC_VALUE (0x7c05e516)

/** The easy-to-use macros  */
#define CRC32(data, length) \
	(syna_pal_crc32(~0, data, length) ^ ~0)

#define VALUE(value) \
	(syna_pal_le2_to_uint(value))

#define AREA_ID_STR(area) \
	(syna_tcm_get_flash_area_string(area))


/** List of a data partitions */
enum flash_area {
	AREA_NONE = 0,
	/* please add the declarations below */

	AREA_APP_CODE,
	AREA_APP_CODE_COPRO,
	AREA_APP_CONFIG,
	AREA_DISP_CONFIG,
	AREA_BOOT_CODE,
	AREA_BOOT_CONFIG,
	AREA_PROD_TEST,
	AREA_F35_APP_CODE,
	AREA_FORCE_TUNING,
	AREA_GAMMA_TUNING,
	AREA_TEMPERATURE_GAMM_TUNING,
	AREA_CUSTOM_LCM,
	AREA_LOOKUP,
	AREA_CUSTOM_OEM,
	AREA_OPEN_SHORT_TUNING,
	AREA_CUSTOM_OTP,
	AREA_PPDT,
	AREA_ROMBOOT_APP_CODE,
	AREA_TOOL_BOOT_CONFIG,

	/* please add the declarations above */
	AREA_MAX,
};

/** Header of the content of app config defined in the image file */
struct app_config_header {
	unsigned short magic_value[4];
	unsigned char checksum[4];
	unsigned char length[2];
	unsigned char build_id[4];
	unsigned char customer_config_id[16];
};
/** The Partition Descriptor of each data area defined in the image file */
struct area_descriptor {
	unsigned char magic_value[4];
	unsigned char id_string[16];
	unsigned char flags[4];
	unsigned char flash_addr_words[4];
	unsigned char length[4];
	unsigned char checksum[4];
};
/** Definitions of a data area defined in the firmware file */
struct area_block {
	bool available;
	const unsigned char *data;
	unsigned int size;
	unsigned int flash_addr;
	unsigned char id;
};
/** Structure of binary data after the file parsing */
struct image_info {
	struct area_block data[AREA_MAX];
};
/** Header of firmware image file */
struct image_header {
	unsigned char magic_value[4];
	unsigned char num_of_areas[4];
};

/*
 * Common Helpers
 */

/**
 * @brief   Return the string ID of target data partition
 *
 * @param
 *    [ in] area: target flash area
 *
 * @return
 *    the string ID
 */
static inline char *syna_tcm_get_flash_area_string(enum flash_area area)
{
	switch (area) {
	case AREA_BOOT_CODE:
		return "BOOT_CODE";
	case AREA_BOOT_CONFIG:
		return "BOOT_CONFIG";
	case AREA_APP_CODE:
		return "APP_CODE";
	case AREA_APP_CODE_COPRO:
		return "APP_CODE_COPRO";
	case AREA_APP_CONFIG:
		return "APP_CONFIG";
	case AREA_PROD_TEST:
		return "APP_PROD_TEST";
	case AREA_DISP_CONFIG:
		return "DISPLAY";
	case AREA_F35_APP_CODE:
		return "F35_APP_CODE";
	case AREA_FORCE_TUNING:
		return "FORCE";
	case AREA_GAMMA_TUNING:
		return "GAMMA";
	case AREA_TEMPERATURE_GAMM_TUNING:
		return "TEMPERATURE_GAMM";
	case AREA_CUSTOM_LCM:
		return "LCM";
	case AREA_LOOKUP:
		return "LOOKUP";
	case AREA_CUSTOM_OEM:
		return "OEM";
	case AREA_OPEN_SHORT_TUNING:
		return "OPEN_SHORT";
	case AREA_CUSTOM_OTP:
		return "OTP";
	case AREA_PPDT:
		return "PPDT";
	case AREA_ROMBOOT_APP_CODE:
		return "ROMBOOT_APP_CODE";
	case AREA_TOOL_BOOT_CONFIG:
		return "TOOL_BOOT_CONFIG";
	default:
		return "";
	}
}
/**
 * @brief   Create the specific structure representing the target data partition
 *
 * @param
 *    [out] image_info: image info used for storing the block data
 *    [ in] area:       target area
 *    [ in] content:    content of data
 *    [ in] flash_addr: offset of block data
 *    [ in] size:       size of block data
 *    [ in] checksum:   checksum of block data
 *
 * @return
 *     0 or positive value in case of success, a negative value otherwise.
 */
static int syna_tcm_save_flash_block_data(struct image_info *image_info,
	unsigned int area, const unsigned char *content, unsigned int offset,
	unsigned int size, unsigned int checksum)
{
	if (!image_info) {
		LOGE("Invalid image_info\n");
		return -ERR_INVAL;
	}

	if ((area <= (int)AREA_NONE) || (area >= (int)AREA_MAX)) {
		LOGE("Invalid flash area\n");
		return -ERR_INVAL;
	}

	if (checksum != CRC32((const char *)content, size)) {
		LOGE("%s checksum error, in image: 0x%x (0x%x)\n",
			AREA_ID_STR((enum flash_area)area), checksum,
			CRC32((const char *)content, size));
		return -ERR_INVAL;
	}
	image_info->data[area].size = size;
	image_info->data[area].data = content;
	image_info->data[area].flash_addr = offset;
	image_info->data[area].id = (unsigned char)area;
	image_info->data[area].available = true;

	LOGI("%s area - address:0x%08x (%d), size:%d\n",
		AREA_ID_STR((enum flash_area)area), offset, offset, size);

	return 0;
}
/**
 * @brief   Query the corresponding ID of flash area
 *
 * @param
 *    [ in] str: string to look for
 *
 * @return
 *    the corresponding ID in case of success, AREA_NONE (0) otherwise.
 */
static inline enum flash_area syna_tcm_get_flash_area_id(char *str)
{
	int area;
	char *target;
	unsigned int len;

	for (area = AREA_MAX - 1; area >= 0; area--) {
		target = (char *)AREA_ID_STR((enum flash_area)area);
		len = syna_pal_str_len(target);

		if (syna_pal_str_cmp(str, target, len) == 0)
			return (enum flash_area)area;
	}

	LOGW("Un-defined area string, %s\n", str);
	return AREA_NONE;
}
/**
 * @brief   Parse the given firmware image file and turn into the binary data for partitions.
 *
 * @param
 *    [ in] image:        image file given
 *    [out] image_info:   data blob stored the parsed data from an image file
 *
 *
 * @return
 *    0 or positive value in case of success, a negative value otherwise.
 */
static inline int syna_tcm_parse_fw_image(const unsigned char *image,
	struct image_info *image_info)
{
	int retval = 0;
	unsigned int idx;
	unsigned int addr;
	unsigned int offset;
	unsigned int length;
	unsigned int checksum;
	unsigned int flash_addr;
	unsigned int magic_value;
	unsigned int num_of_areas;
	struct image_header *header;
	struct area_descriptor *descriptor;
	const unsigned char *content;
	enum flash_area target_area;

	if (!image) {
		LOGE("No image data\n");
		return -ERR_INVAL;
	}

	if (!image_info) {
		LOGE("Invalid image_info blob\n");
		return -ERR_INVAL;
	}

	syna_pal_mem_set(image_info, 0x00, sizeof(struct image_info));

	header = (struct image_header *)image;

	magic_value = syna_pal_le4_to_uint(header->magic_value);
	if (magic_value != IMAGE_FILE_MAGIC_VALUE) {
		LOGE("Invalid image file magic value\n");
		return -ERR_INVAL;
	}

	offset = sizeof(struct image_header);
	num_of_areas = syna_pal_le4_to_uint(header->num_of_areas);

	for (idx = 0; idx < num_of_areas; idx++) {
		addr = syna_pal_le4_to_uint(image + offset);
		descriptor = (struct area_descriptor *)(image + addr);
		offset += 4;

		magic_value = syna_pal_le4_to_uint(descriptor->magic_value);
		if (magic_value != FLASH_AREA_MAGIC_VALUE)
			continue;

		length = syna_pal_le4_to_uint(descriptor->length);
		content = (unsigned char *)descriptor + sizeof(*descriptor);
		flash_addr = syna_pal_le4_to_uint(descriptor->flash_addr_words);
		flash_addr = flash_addr * 2;
		checksum = syna_pal_le4_to_uint(descriptor->checksum);

		target_area = syna_tcm_get_flash_area_id((char *)descriptor->id_string);
		retval = syna_tcm_save_flash_block_data(image_info,
			(unsigned int) target_area, content, flash_addr,
			length, checksum);
		if (retval < 0)
			return retval;
	}
	return 0;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* end of _SYNAPTICS_TOUCHCOM_PARSE_FW_IMAGE_H_ */
