/* SPDX-License-Identifier: GPL-2.0 */

/**
 * @copyright Copyright (c) 2024 Samsung Electronics Co., Ltd
 *
 */

#ifndef __UWB_FW_COMMON_H__
#define __UWB_FW_COMMON_H__

#include <linux/errno.h>
#include <linux/types.h>

#include "uwb.h"

#define RETRY_MAX_CNT 2
#define TYPE_MASK 24
#define LRC8_LEN 1
#define ERR_CODE(code, type) (((code) < 0 ? -(code) : (code)) | ((type) << TYPE_MASK))
#define FLASH_MAX_RETRY_COUNT 5
#define APDU_SW1_VAL_ERR_KEY 0x66
#define APDU_SW1_FW_HEADER_ERR 0x73
#define APDU_SW2_FW_HEADER_ERR 0x00
#define APDU_SW1_CMAC_ERR 0x72
#define APDU_SW2_CMAC_ERR 0x01
#define APDU_SW1_CRC_ERR 0x71
#define APDU_SW2_CRC_ERR 0x00

#define SELECT_POLL_INTERVAL_MS 5
#define LAST_PKT_POLL_DELAY_MS 190
#define APDU_P1_LAST_PKG 0xA0
#define APDU_P1_NO_LAST_PKG 0x20
#define APDU_P2_FIXED 0x01
#define APDU_RECV_SW1 0x90
#define APDU_RECV_SW2 0x00

#define ISO_T1_HEADER_LEN 0x03
#define ISO_T1_HEADER_FM_LEN 0x04
#define FW_APDU_T_LEN 0x5
#define FW_APDU_T_FM_LEN 0x06
#define APDU_NAD_LEN 1
#define APDU_NAD_RESP 0x21
#define APDU_SW1_SW2_LEN 2

#define WAIT_INTERRUPT_TIMEOUT_MS 1000
#define MIN_BL1_VER_RSP_LEN 6

/**
 * @brief Error Code definition
 *
 */
enum fw_err {
	FW_OK = 0,
	FW_ERROR = -134,
	FW_ERROR_PARAMETER = -EINVAL,
	FW_ERROR_IO = -EIO,
	FW_ERROR_TIME = -ETIME,
	FW_ERROR_NOMEMORY = -ENOMEM,
	FW_ERROR_ENCRYPT = -135,
	FW_ERROR_CMAC = -136,
	FW_ERROR_CHECK = -137,
	FW_ERROR_RSQ = -138,
	FW_ERROR_LOG = -139,
	FW_ERROR_KEY_BL0 = -140,
	FW_ERROR_DATA = -EBADMSG,
	FW_ERROR_NO_UPDATE = -141,
	FW_ERROR_KEY_BL1 = -142,
	FW_ERROR_RESERVED = 0x7FFFFFFF
};

enum apdu_recv_pkt {
	NAD = 0,
	PCB = 1,
	LEN = 2,
	SW1 = 3,
	SW2 = 4,
	LRC = 5
};

union apdu_len {
	uint8_t len_8;
	uint16_t len_16;
};

struct iso_t1_header {
	uint8_t nad;
	uint8_t pcb;
	union apdu_len len;
};

struct fw_apdu {
	uint8_t cla;
	uint8_t ins;
	uint8_t p1;
	uint8_t p2;
	union apdu_len lc;
};

int init_block(uint8_t *block, struct iso_t1_header iso_header, int iso_size,
		struct fw_apdu apdu_pkt, int apdu_size, uint8_t *payload, int size);

enum fw_err iso_iblock_send(struct u100_ctx *u100_ctx, uint8_t *data, uint32_t len);

int read_iso_iblock(struct u100_ctx *u100_ctx, uint8_t *buffer, uint32_t buffer_len,
		unsigned long initial_delay, unsigned long common_delay, bool is_bl0);

enum fw_err parse_iso_iblock(uint8_t *buffer, uint32_t buffer_len);

unsigned char lrc8(const uint8_t *s, size_t n);

enum fw_err enter_uwb_bl0_mode(struct u100_ctx *u100_ctx);

enum fw_err enter_uwb_bl1_mode(struct u100_ctx *u100_ctx);

enum fw_err enter_uwb_bl0_mode_with_retry(struct u100_ctx *u100_ctx, int max_retry);

enum fw_err enter_uwb_bl1_mode_with_retry(struct u100_ctx *u100_ctx, int max_retry);

void iso_t1_header_init(struct iso_t1_header *header, uint16_t len, struct u100_ctx *u100_ctx);

void apdu_header_init(struct fw_apdu *apdu, uint8_t p1, uint8_t p2, uint16_t lc);

int get_cfg_by_type(int config, int type);

void set_bit_val(int *config, int type, int val);

enum fw_err uwb_flash_img(struct u100_ctx *u100_ctx, uint8_t *data, uint32_t size,
				const uint32_t max_pkg_payload_len, bool fixed_p2,
				unsigned long initial_delay, unsigned long common_delay,
				bool is_bl0);

enum fw_err uwb_get_bl1_version(struct u100_ctx *u100_ctx);

int check_iso_iblock(uint8_t *data, int max_size);
#endif  // __UWB_FW_COMMON_H__
