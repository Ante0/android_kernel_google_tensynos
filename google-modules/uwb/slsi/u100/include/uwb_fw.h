/* SPDX-License-Identifier: GPL-2.0 */

/**
 * @copyright Copyright (c) 2024 Samsung Electronics Co., Ltd
 *
 */

#ifndef __UWB_FW_H__
#define __UWB_FW_H__

#include <linux/firmware.h>
#include <linux/types.h>

#define MAX_APDU_DATA_LEN     249
#define MAX_AP_APDU_DATA_LEN  240
#define MAX_ISO_PKT_LEN       258
#define MAX_FLASH_DATA_CNT    16
#define MAX_FW_NAME_LEN       127
#define MAX_BL1_VERSION_LEN   32

#define MAX_BIN_CNT    2
#define DEFAULT_BIN_IDX 0
#define DEFAULT_BIN_TEST_IDX 1

#define MAX_FW_INFO_SIZE 257
#define TLV_TYPE_FW_VERSION 0x00
#define TLV_TYPE_NV_HASH 0x01
#define TLV_TYPE_KEY_TYPE 0x0B
#define TLV_TYPE_DEV_ID 0xA1
#define TLV_TYPE_HW_STATUS 0xA2
#define TLV_TYPE_HOST_TYPE 0xA3
#define HOST_TYPE_LEN 1
#define FW_TYPE_LEN 1
#define FW_VERSION_LEN 12
#define NV_HASH_LEN 32
#define KEY_TYPE_LEN 1
#define DEV_ID_LEN 16
#define HW_STATUS_LEN 1

#define HOST_TYPE_BIT_OFFSET 4
#define MAX_RSP_LEN 260
#define MAX_CMD (255 + 3 + 2)
#define MAX_AP_APDU_DATA_FM_LEN 8192
#define SEND_PKT_HEADER_LEN 12
#define MAX_SPI_BLOCK_SIZE (MAX_AP_APDU_DATA_FM_LEN + SEND_PKT_HEADER_LEN)
#define FW_INFO_RESERVED_LEN (MAX_FW_INFO_SIZE - FW_TYPE_LEN - FW_VERSION_LEN - NV_HASH_LEN \
				- KEY_TYPE_LEN - DEV_ID_LEN - HW_STATUS_LEN - HOST_TYPE_LEN)
#define DEFAULT_FAST_MODE_DISABLE 0

struct u100_ctx;

enum {
	KEY_TYPE_UNKNOWN = 0x00,
	KEY_TYPE_TEST = 0x01,
	KEY_TYPE_LIVE = 0x02,
};

enum {
	TYPE_FW = 0x22,
	TYPE_FW_V = 0x23,
	TYPE_BL1 = 0x33,
	TYPE_BL1_V = 0x34,
	TYPE_OPTION = 0x55,
	TYPE_INJECT = 0x66,
	TYPE_BIN_C = 0xFF,
};

enum {
	FW_TYPE_EDS = 0x00,
	FW_TYPE_FACTORY = 0x01,
	FW_TYPE_USER = 0x02,
	FW_TYPE_DEV = 0x03,
	FW_TYPE_UNKNOWN = 0xFF,
};

enum flash_action {
	ACTION_IDLE = 0x00,
	ACTION_SEND_GET_BL1_VER_CMD = 0x01,
	ACTION_SEND_HEADER = 0x02,
	ACTION_SEND_BODY = 0x03,
	ACTION_RECV_RESP = 0x04,
	ACTION_RECV_BL1_VER_RESP = 0x05,
};

struct firmware_info {
	uint8_t fw_type;
	char version[FW_VERSION_LEN];
	char nv_hash[NV_HASH_LEN];
	uint8_t key_type;
	char devid[DEV_ID_LEN];
	uint8_t hw_status;
	uint8_t host_type;
	char reserved[FW_INFO_RESERVED_LEN];
};

struct flash_param {
	char fw_name[MAX_FW_NAME_LEN + 1];
	unsigned char disable_crc_check;
	char reserved[16];
};

struct uwb_tlv {
	unsigned char type;
	unsigned int len;
	unsigned char *value;
	const char *label;
	unsigned int max_retry_cnt;

	int (*prepare)(struct u100_ctx *u100_ctx, struct uwb_tlv *tlv);
	int (*flash)(struct u100_ctx *u100_ctx, struct uwb_tlv *tlv);
};

struct firmware_data {
	size_t size;
	uint8_t data[];
};

struct uwb_fw_progress_data {
	int total_size;
	int already_flash_size;
	int threshold_size;
	int step_flash_size;
	int actual_flash_cnt;
};

struct fw_download_param {
	bool fast_mode;
	unsigned int max_segment_size; /* Unit:Byte */
	unsigned int download_spi_speed; /* Unit:Hz */
};

struct uwb_firmware {
	char fw_name[MAX_FW_NAME_LEN + 1];
	struct firmware_data *fw;
	struct uwb_tlv flash_data_arr[MAX_FLASH_DATA_CNT];
	struct uwb_tlv *checksum;
	struct uwb_tlv *bl1_ver;
	struct uwb_tlv *fw_tlv;
	int flash_data_cnt;
	struct uwb_fw_progress_data progress_data;
};

struct uwb_firmware_ctx {
	int config;
	uint8_t iso_t1_pcb;
	bool is_auto;
	bool disable_crc_check;
	int current_bin_idx;
	bool test_fw_loaded;
	int spi_len;
	enum flash_action action;
	int cmd_rsp_len;
	int cmd_len;
	uint8_t cmd_rsp[MAX_RSP_LEN];
	uint8_t cmd[MAX_CMD];
	uint8_t send_block[MAX_SPI_BLOCK_SIZE];
	uint8_t read_block[MAX_SPI_BLOCK_SIZE];
	struct uwb_firmware uwb_fw[MAX_BIN_CNT];
	struct fw_download_param download_param;
	uint8_t bl1_version[MAX_BL1_VERSION_LEN];
	int8_t bl1_retries;
	void (*notify_progress)(struct uwb_fw_progress_data *progress_data,
		int flashed_len, bool force, uint32_t per_pkt_size);
};

int uwb_flash_ap(struct u100_ctx *u100_ctx, struct uwb_tlv *tlv);
int uwb_flash_bl1(struct u100_ctx *u100_ctx, struct uwb_tlv *tlv);
int uwb_flash_bl1_prepare(struct u100_ctx *u100_ctx, struct uwb_tlv *tlv);

#endif  // __UWB_FW_H__
