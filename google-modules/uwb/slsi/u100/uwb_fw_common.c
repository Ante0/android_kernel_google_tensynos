// SPDX-License-Identifier: GPL-2.0
/**
 *
 * @copyright Copyright (c) 2024 Samsung Electronics Co., Ltd
 *
 */

#include <linux/delay.h>
#include <linux/errno.h>

#include "include/uwb.h"
#include "include/uwb_fw_common.h"
#include "include/uwb_spi.h"

#define V1_SELECT_BL1_RSP_LEN 6
#define FAST_MODE_SELECT_BL1_RSP_LEN 9
#define SPI_SPEED_MHZ 1000000
#define APDU_PKT_KBYTES 1024
#define APDU_RECV_SW1 0x90
#define APDU_RECV_SW2 0x00
#define MAX_RETRIES 250
#define PRINT_HEX_PER_LINE_LEN 8
#define MIN_ISO_BLOCK_LEN 4
#define APDU_LAST_BYTE_DELAY_US 50
#define BL1_VER_PREFIX_LEN 5

static void iso_t1_header_pcb_reset(struct u100_ctx *u100_ctx)
{
	u100_ctx->uwb_fw_ctx.iso_t1_pcb = 0;
}

void iso_t1_header_init(struct iso_t1_header *header, uint16_t len,
		struct u100_ctx *u100_ctx)
{
	header->nad = 0x12;
	header->len.len_16 = len;
	if (u100_ctx->uwb_fw_ctx.iso_t1_pcb)
		header->pcb = 0x40;
	else
		header->pcb = 0x00;
	u100_ctx->uwb_fw_ctx.iso_t1_pcb ^= 1;
}

void apdu_header_init(struct fw_apdu *apdu, uint8_t p1, uint8_t p2, uint16_t lc)
{
	apdu->cla = 0x80;
	apdu->ins = 0xE2;
	apdu->p1 = p1;
	apdu->p2 = p2;
	apdu->lc.len_16 = lc;
}

int init_block(uint8_t *block, struct iso_t1_header iso_header, int iso_size,
		struct fw_apdu apdu_pkt, int apdu_size, uint8_t *payload, int size)
{
	int len = iso_size + apdu_size + size + 1;

	if (len > MAX_SPI_BLOCK_SIZE) {
		UWB_ERR("Block size %d is out of range %d", len, MAX_SPI_BLOCK_SIZE);
		return 0;
	}

	memset(block, 0, len);
	len = 0;
	memcpy(block, &iso_header, iso_size);
	len += iso_size;

	memcpy(block + len, &apdu_pkt, apdu_size);
	len += apdu_size;

	memcpy(block + len, payload, size);
	len += size;

	block[len] = lrc8(block, len);
	len++;

	return len;
}

enum fw_err iso_iblock_send(struct u100_ctx *u100_ctx, uint8_t *data, uint32_t len)
{
	int ret;

	ret = uwb_spi_send(u100_ctx, data, len, NULL);
	if (ret < 0)
		return FW_ERROR_IO;

	return FW_OK;
}

int read_iso_iblock(struct u100_ctx *u100_ctx, uint8_t *buffer, uint32_t buffer_len,
		unsigned long initial_delay, unsigned long common_delay, bool is_bl0)
{
	enum fw_err ret;
	uint8_t c = 0;
	uint8_t *s;
	int retry;
	uint32_t len, i;

	if (buffer_len < MIN_ISO_BLOCK_LEN)
		return -EINVAL;
	s = buffer;
	i = 0;

	if (initial_delay > 0)
		mdelay(initial_delay);

	for (retry = 0; retry < MAX_RETRIES; retry++) {
		ret = uwb_spi_recv(u100_ctx, &c, APDU_NAD_LEN);
		if (ret < 0)
			return ret;
		else if (c == APDU_NAD_RESP)
			break;
		else if (c != NONE_DATA_FF && c != NONE_DATA_00)
			UWB_ERR("Unexpected data read on MISO.Expect [0x21] but actual [%#02x]",
					c);
		mdelay(common_delay);
	}
	if (c != APDU_NAD_RESP) {
		UWB_ERR("APDU reading nad[0x21] timeout. Retry times[%d],recv delay time[%lums]",
				MAX_RETRIES, common_delay);
		return -ETIMEDOUT;
	}
	s[i] = c;
	i += APDU_NAD_LEN;
	len = ISO_T1_HEADER_LEN - APDU_NAD_LEN;
	ret = uwb_spi_recv(u100_ctx, (s + i), len);
	if (ret < 0) {
		UWB_ERR("Spi error when reading pcb, len.");
		return ret;
	}

	i += len;
	len = i + s[LEN] + LRC8_LEN;
	if (len > buffer_len) {
		UWB_ERR("Unexpected buffer length. Expect [%d], but actual [%d].Apdu len[%#02x]",
				buffer_len, len, s[LEN]);
		return -ENOMEM;
	}

	if (is_bl0)
		len = s[LEN];
	else
		len = s[LEN] + LRC8_LEN;
	if (len) {
		ret = uwb_spi_recv(u100_ctx, (s + i), len);
		if (ret < 0) {
			UWB_ERR("Spi error when reading sw1, sw2.");
			return ret;
		}
	}
	i += len;
	if (is_bl0) {
		udelay(APDU_LAST_BYTE_DELAY_US);
		ret = uwb_spi_recv(u100_ctx, (s + i), LRC8_LEN);
		if (ret < 0)
			UWB_WARN("Spi error when reading lrc.");
		i += LRC8_LEN;
	}
	return i;
}

enum fw_err parse_iso_iblock(uint8_t *buffer, uint32_t buffer_len)
{

	if (buffer_len < MIN_ISO_BLOCK_LEN) {
		UWB_ERR("APDU response length error. Mini length[%d], but actual length[%d]",
			MIN_ISO_BLOCK_LEN, buffer_len);
		return FW_ERROR_PARAMETER;
	}

	if (buffer[buffer_len - 2] == APDU_RECV_SW2 &&
		buffer[buffer_len - 3] == APDU_RECV_SW1)
		return FW_OK;

	print_hex_dump(KERN_ERR, LOG_TAG "APDU Recv data: ",
		DUMP_PREFIX_NONE, PRINT_HEX_PER_LINE_LEN, 1,
		buffer, buffer_len, false);
	if (buffer[buffer_len - 3] == APDU_SW1_VAL_ERR_KEY) {
		UWB_ERR("U100 BL0 key verification failed");
		return FW_ERROR_KEY_BL0;
	} else if (buffer[buffer_len - 3] == APDU_SW1_FW_HEADER_ERR) {
		UWB_ERR("U100 BL1 key verification failed");
		return FW_ERROR_KEY_BL1;
	} else if (buffer[buffer_len - 2] == APDU_SW2_CMAC_ERR &&
		buffer[buffer_len - 3] == APDU_SW1_CMAC_ERR)
		UWB_ERR("U100 cmac verification failed");
	else if (buffer[buffer_len - 2] == APDU_SW2_CRC_ERR &&
		buffer[buffer_len - 3] == APDU_SW1_CRC_ERR)
		UWB_ERR("U100 APDU pkt CRC verification failed");
	else
		UWB_ERR("APDU response unknown error");

	return FW_ERROR_RSQ;
}

unsigned char lrc8(const uint8_t *s, size_t n)
{
	uint8_t c = 0;

	while (n) {
		c ^= *s++;
		n--;
	}
	return c;
}

static enum fw_err send_mode_command(struct u100_ctx *u100_ctx, const uint8_t *select_cmd,
		int select_cmd_len, bool is_bl0)
{
	enum fw_err ret;
	int rsp_len;
	uint32_t cmd_len = select_cmd_len + ISO_T1_HEADER_LEN;
	struct iso_t1_header header;

	iso_t1_header_init(&header, select_cmd_len, u100_ctx);
	memcpy(u100_ctx->uwb_fw_ctx.cmd, &header, ISO_T1_HEADER_LEN);
	memcpy(u100_ctx->uwb_fw_ctx.cmd + ISO_T1_HEADER_LEN, select_cmd,
			select_cmd_len);
	u100_ctx->uwb_fw_ctx.cmd[cmd_len] = lrc8(u100_ctx->uwb_fw_ctx.cmd, cmd_len);
	cmd_len += 1;

	ret = iso_iblock_send(u100_ctx, u100_ctx->uwb_fw_ctx.cmd, cmd_len);
	if (ret != FW_OK)
		return ret;

	rsp_len = read_iso_iblock(u100_ctx, u100_ctx->uwb_fw_ctx.cmd_rsp, MAX_RSP_LEN,
			SELECT_POLL_INTERVAL_MS, SELECT_POLL_INTERVAL_MS, is_bl0);
	if (rsp_len == -ETIMEDOUT)
		return FW_ERROR_TIME;

	if (rsp_len < FW_OK)
		return FW_ERROR_IO;

	u100_ctx->uwb_fw_ctx.cmd_rsp_len = rsp_len;
	return parse_iso_iblock(u100_ctx->uwb_fw_ctx.cmd_rsp, rsp_len);
}

static void parse_select_bl1_rsp(uint8_t *data, int len, struct fw_download_param *param)
{
	if (len <= V1_SELECT_BL1_RSP_LEN) {
		param->fast_mode = false;
		param->max_segment_size = MAX_AP_APDU_DATA_LEN;
		return;
	}
	data += ISO_T1_HEADER_LEN;
	param->fast_mode = *data;
	if (len >= FAST_MODE_SELECT_BL1_RSP_LEN) {
		data++;
		param->download_spi_speed = *data * SPI_SPEED_MHZ;
		data++;
		param->max_segment_size = *data ? *data * APDU_PKT_KBYTES : MAX_AP_APDU_DATA_LEN;
	}
}

static enum fw_err select_uwb_bl0_mode(struct u100_ctx *u100_ctx)
{
	static const uint8_t select_cmd[] = {0x00, 0xA4, 0x04, 0x00, 0x08, 0xA0, 0x00,
					0x53, 0x2E, 0x4C, 0x53, 0x49, 0x00, 0x00};

	return send_mode_command(u100_ctx, select_cmd, sizeof(select_cmd), true);
}

enum fw_err enter_uwb_bl0_mode(struct u100_ctx *u100_ctx)
{
	iso_t1_header_pcb_reset(u100_ctx);
	if (u100_ctx->u100_state != U100_BL0_STATE) {
		if (!uwbs_sync_start_download(u100_ctx))
			return FW_ERROR;
	}
	return select_uwb_bl0_mode(u100_ctx);
}

static enum fw_err select_uwb_bl1_mode(struct u100_ctx *u100_ctx)
{
	static const uint8_t select_cmd[] = {0x00, 0x75, 0x02, 0x01, 0x10};
	enum fw_err ret;

	ret = send_mode_command(u100_ctx, select_cmd, sizeof(select_cmd), false);
	if (ret < FW_OK)
		return FW_ERROR_IO;
	u100_ctx->u100_state = U100_BL1_STATE;
	parse_select_bl1_rsp(u100_ctx->uwb_fw_ctx.cmd_rsp, u100_ctx->uwb_fw_ctx.cmd_rsp_len,
			&u100_ctx->uwb_fw_ctx.download_param);
	return ret;

}

enum fw_err enter_uwb_bl1_mode(struct u100_ctx *u100_ctx)
{
	enum fw_err ret;

	iso_t1_header_pcb_reset(u100_ctx);
	if (u100_ctx->u100_state == U100_BL1_STATE)
		return FW_OK;
	if (!uwbs_sync_start_download(u100_ctx))
		return FW_ERROR;
	ret = select_uwb_bl0_mode(u100_ctx);
	if (ret != FW_OK)
		return ret;
	ret = select_uwb_bl1_mode(u100_ctx);
	return ret;
}

enum fw_err enter_uwb_bl0_mode_with_retry(struct u100_ctx *u100_ctx, int max_retry)
{
	int ret = 0;
	int retry_cnt = 0;

	u100_ctx->u100_state = U100_UNKNOWN_STATE;
	while (retry_cnt < max_retry) {
		retry_cnt++;
		ret = enter_uwb_bl0_mode(u100_ctx);
		if (ret == FW_OK)
			break;
	}
	return ret;
}

enum fw_err enter_uwb_bl1_mode_with_retry(struct u100_ctx *u100_ctx, int max_retry)
{
	int ret = 0;
	int retry_cnt = 0;

	while (retry_cnt < max_retry) {
		retry_cnt++;
		ret = enter_uwb_bl1_mode(u100_ctx);
		if (ret == FW_OK)
			break;
	}
	return ret;
}

int get_cfg_by_type(int config, int type)
{
	return (config >> (type & 0xF)) & 1;
}

void set_bit_val(int *config, int type, int val)
{
	if (val)
		*config |= (1 << (type & 0xF));
	else
		*config &= ~(1 << (type & 0xF));
}

enum fw_err uwb_flash_img(struct u100_ctx *u100_ctx, uint8_t *data, uint32_t size,
				const uint32_t max_pkg_payload_len, bool fixed_p2,
				unsigned long initial_delay, unsigned long common_delay,
				bool is_bl0)
{
	enum fw_err ret;
	uint32_t send_len;
	int read_len;
	struct fw_apdu apdu_pkt;
	struct iso_t1_header iso_pkt;
	bool is_last;
	uint8_t p1, p2;
	uint32_t actual_len;
	uint32_t total_pkg = (size + max_pkg_payload_len - 1) / max_pkg_payload_len;
	struct uwb_firmware_ctx *uwb_fw_ctx = &u100_ctx->uwb_fw_ctx;

	if (FW_APDU_T_LEN + ISO_T1_HEADER_LEN + max_pkg_payload_len + LRC8_LEN > MAX_ISO_PKT_LEN)
		return FW_ERROR;

	for (uint32_t index = 0; index < total_pkg; index++) {
		is_last = (index + 1) * max_pkg_payload_len >= size;
		actual_len = is_last ? (size - index * max_pkg_payload_len) : max_pkg_payload_len;
		send_len = 0;
		read_len = 0;
		memset(&apdu_pkt, 0, sizeof(struct fw_apdu));
		memset(&iso_pkt, 0, sizeof(struct iso_t1_header));
		memset(uwb_fw_ctx->read_block, 0, MAX_ISO_PKT_LEN);

		if (is_last)
			p1 = APDU_P1_LAST_PKG;
		else
			p1 = APDU_P1_NO_LAST_PKG;

		if (fixed_p2)
			p2 = APDU_P2_FIXED;
		else
			p2 = index;

		iso_t1_header_init(&iso_pkt, actual_len + FW_APDU_T_LEN, u100_ctx);
		apdu_header_init(&apdu_pkt, p1, p2, actual_len);

		send_len = init_block(uwb_fw_ctx->send_block, iso_pkt, ISO_T1_HEADER_LEN,
				apdu_pkt, FW_APDU_T_LEN, data, actual_len);
		if (send_len <= 0)
			return FW_ERROR_IO;

		data += actual_len;
		ret = iso_iblock_send(u100_ctx, uwb_fw_ctx->send_block, send_len);
		if (ret != FW_OK) {
			UWB_ERR("FW normal download send error %d when %d/%d packet", ret,
					index, total_pkg);
			return ret;
		}

		if (is_last)
			mdelay(LAST_PKT_POLL_DELAY_MS);

		read_len = read_iso_iblock(u100_ctx, uwb_fw_ctx->read_block, MAX_ISO_PKT_LEN,
					initial_delay, common_delay, is_bl0);
		if (read_len == -ETIMEDOUT) {
			UWB_ERR("FW normal download timeout when %d/%d packet", index, total_pkg);
			return FW_ERROR_TIME;
		}

		if (read_len < FW_OK) {
			UWB_ERR("FW normal download read error %d when %d/%d packet", read_len,
					index, total_pkg);
			return FW_ERROR_IO;
		}

		ret = parse_iso_iblock(uwb_fw_ctx->read_block, read_len);
		if (ret != FW_OK)
			return ret;
		if (!is_last)
			uwb_fw_ctx->notify_progress(
				&uwb_fw_ctx->uwb_fw[uwb_fw_ctx->current_bin_idx].progress_data,
				actual_len, is_last, max_pkg_payload_len);
	}
	return FW_OK;
}

/**
 * parse_bl1_version_num	-	parse bl1 version string to bytes
 * @data: bl1 version string (e.g., 1.3.3)
 * @data_len: the data length
 * @u100_ctx: u100_ctx structure
 *
 * Parse bl1 version from string to bytes.
 * Major is one byte, middle is one byte and minor is two bytes.
 *
 * e.g., If data is '1.3.3', the parsed bl1_version is '01 03 0003'
 */
static void parse_bl1_version_num(uint8_t *data, int data_len, struct u100_ctx *u100_ctx)
{
	unsigned int major = 0;
	unsigned int mid = 0;
	unsigned int minor = 0;
	struct uwb_firmware_ctx *uwb_fw_ctx = &u100_ctx->uwb_fw_ctx;

	while (data_len && (data[0] < '0' || data[0] > '9')) {
		data_len--;
		data++;
	}
	if (!data_len) {
		UWB_WARN("Invalid bl1 version");
		return;
	}
	if (sscanf(data, "%d.%d.%d", &major, &mid, &minor) != 3) {
		UWB_WARN("Unknown version format, %s", data);
		return;
	}

	uwb_fw_ctx->bl1_version[0] = major;
	uwb_fw_ctx->bl1_version[1] = mid;
	uwb_fw_ctx->bl1_version[2] = minor >> 8;
	uwb_fw_ctx->bl1_version[3] = minor & 0xFF;
}

static enum fw_err parse_bl1_version(struct u100_ctx *u100_ctx)
{
	uint16_t iso_len;
	uint8_t *data;
	int data_len;

	if (u100_ctx->uwb_fw_ctx.download_param.fast_mode) {
		iso_len = *((uint16_t *)(u100_ctx->uwb_fw_ctx.cmd_rsp + LEN));
		data = u100_ctx->uwb_fw_ctx.cmd_rsp + ISO_T1_HEADER_FM_LEN;
	} else {
		iso_len = u100_ctx->uwb_fw_ctx.cmd_rsp[LEN];
		data = u100_ctx->uwb_fw_ctx.cmd_rsp + ISO_T1_HEADER_LEN;
	}

	if (!iso_len) {
		memcpy(u100_ctx->uwb_fw_ctx.bl1_version, UNKNOWN_STR, sizeof(UNKNOWN_STR));
		UWB_WARN("Get BL1 version failed, fast-mode[%d].",
			u100_ctx->uwb_fw_ctx.download_param.fast_mode);
		return FW_ERROR_IO;
	}

	data_len = min(iso_len - APDU_SW1_SW2_LEN, MAX_BL1_VERSION_LEN);
	if (!data_len) {
		memcpy(u100_ctx->uwb_fw_ctx.bl1_version, UNKNOWN_STR, sizeof(UNKNOWN_STR));
		UWB_WARN("No BL1 version, fast-mode[%d].",
			u100_ctx->uwb_fw_ctx.download_param.fast_mode);
		return FW_ERROR_IO;
	}
	UWB_INFO("BL1 version: %.*s", data_len, data);

	/* data - bl1 version string (e.g., bl1:v1.3.3) */
	if (data_len > BL1_VER_PREFIX_LEN) {
		data_len -= BL1_VER_PREFIX_LEN;
		data += BL1_VER_PREFIX_LEN;
	}
	parse_bl1_version_num(data, data_len, u100_ctx);
	return FW_OK;

}

static enum fw_err send_cmd_to_fast_mode_bl1(struct u100_ctx *u100_ctx, const uint8_t *cmd,
		int cmd_len)
{
	int len = cmd_len + ISO_T1_HEADER_FM_LEN;
	struct iso_t1_header header;
	int ret;

	u100_ctx->uwb_fw_ctx.cmd_len = ALIGN(len + LRC8_LEN, 4);
	if (u100_ctx->uwb_fw_ctx.cmd_len > MAX_CMD)
		return FW_ERROR;
	iso_t1_header_init(&header, cmd_len, u100_ctx);
	memcpy(u100_ctx->uwb_fw_ctx.cmd, &header, ISO_T1_HEADER_FM_LEN);
	memcpy(u100_ctx->uwb_fw_ctx.cmd + ISO_T1_HEADER_FM_LEN, cmd, cmd_len);
	u100_ctx->uwb_fw_ctx.cmd[len] = lrc8(u100_ctx->uwb_fw_ctx.cmd, len);
	len += LRC8_LEN;

	u100_ctx->uwb_fw_ctx.action = ACTION_SEND_GET_BL1_VER_CMD;
	reinit_completion(&u100_ctx->tx_done_cmpl);
	complete_all(&u100_ctx->process_done_cmpl);
	ret = wait_for_completion_timeout(&u100_ctx->tx_done_cmpl,
			msecs_to_jiffies(WAIT_INTERRUPT_TIMEOUT_MS));
	if (!ret) {
		UWB_WARN("Wait for interrupt timeout[%dms] when sending get BL1 version cmd",
				WAIT_INTERRUPT_TIMEOUT_MS);
		return FW_ERROR_IO;
	}

	u100_ctx->uwb_fw_ctx.cmd_rsp_len = MAX_BL1_VERSION_LEN + ISO_T1_HEADER_FM_LEN
						+ APDU_SW1_SW2_LEN + LRC8_LEN;
	u100_ctx->uwb_fw_ctx.cmd_rsp_len = ALIGN(u100_ctx->uwb_fw_ctx.cmd_rsp_len, 4);
	memset(u100_ctx->uwb_fw_ctx.send_block, 0, u100_ctx->uwb_fw_ctx.cmd_rsp_len);
	u100_ctx->uwb_fw_ctx.action = ACTION_RECV_BL1_VER_RESP;
	reinit_completion(&u100_ctx->tx_done_cmpl);
	complete_all(&u100_ctx->process_done_cmpl);
	ret = wait_for_completion_timeout(&u100_ctx->tx_done_cmpl,
			msecs_to_jiffies(WAIT_INTERRUPT_TIMEOUT_MS));
	if (!ret) {
		UWB_WARN("Wait for interrupt timeout[%dms] when receiving BL1 version rsp",
				WAIT_INTERRUPT_TIMEOUT_MS);
		return FW_ERROR_IO;
	}

	ret = check_iso_iblock(u100_ctx->uwb_fw_ctx.cmd_rsp, u100_ctx->uwb_fw_ctx.cmd_rsp_len);
	if (ret != FW_OK || u100_ctx->uwb_fw_ctx.cmd_rsp_len <= MIN_BL1_VER_RSP_LEN) {
		memcpy(u100_ctx->uwb_fw_ctx.bl1_version, UNKNOWN_STR, sizeof(UNKNOWN_STR));
		UWB_WARN("Get fast-mode BL1 version failed");
		return FW_ERROR_IO;
	}

	return parse_bl1_version(u100_ctx);
}

static enum fw_err try_get_bl1_version(struct u100_ctx *u100_ctx)
{
	static const uint8_t get_bl1_ver_cmd[] = {0x81, 0xed, 0x00, 0x0};
	enum fw_err ret;

	memset(u100_ctx->uwb_fw_ctx.bl1_version, 0, MAX_BL1_VERSION_LEN);
	if (u100_ctx->uwb_fw_ctx.download_param.fast_mode)
		return send_cmd_to_fast_mode_bl1(u100_ctx, get_bl1_ver_cmd,
						sizeof(get_bl1_ver_cmd));
	ret = send_mode_command(u100_ctx, get_bl1_ver_cmd, sizeof(get_bl1_ver_cmd), false);
	if (ret != FW_OK) {
		memcpy(u100_ctx->uwb_fw_ctx.bl1_version, UNKNOWN_STR, sizeof(UNKNOWN_STR));
		UWB_WARN("Get non-fast-mode BL1 version failed.");
		return FW_ERROR_IO;
	}
	if (u100_ctx->uwb_fw_ctx.cmd_rsp_len <= MIN_BL1_VER_RSP_LEN) {
		memcpy(u100_ctx->uwb_fw_ctx.bl1_version, UNKNOWN_STR, sizeof(UNKNOWN_STR));
		print_hex_dump(KERN_WARNING, LOG_TAG "Get Non-Fast BL1 Version Resp: ",
			DUMP_PREFIX_NONE, PRINT_HEX_PER_LINE_LEN, 1,
			u100_ctx->uwb_fw_ctx.cmd_rsp,
			u100_ctx->uwb_fw_ctx.cmd_rsp_len, false);
		UWB_WARN("Get non-fast-mode BL1 version failed.");
		return FW_ERROR_IO;
	}
	return parse_bl1_version(u100_ctx);
}

int check_iso_iblock(uint8_t *data, int max_size)
{
	int len;

	if (data[NAD] != APDU_NAD_RESP)
		return FW_ERROR_IO;

	len = *((uint16_t *)&data[LEN]);
	len += ISO_T1_HEADER_FM_LEN + LRC8_LEN;
	if (len > max_size)
		return FW_ERROR_IO;
	return parse_iso_iblock(data, len);
}

static void set_default_download_param(struct u100_ctx *u100_ctx)
{
	u100_ctx->uwb_fw_ctx.download_param.fast_mode = DEFAULT_FAST_MODE_DISABLE;
	u100_ctx->uwb_fw_ctx.download_param.max_segment_size = MAX_AP_APDU_DATA_FM_LEN;
	u100_ctx->uwb_fw_ctx.download_param.download_spi_speed = uwb_get_spi_speed();
}

enum fw_err uwb_get_bl1_version(struct u100_ctx *u100_ctx)
{
	enum fw_err ret;

	set_default_download_param(u100_ctx);
	iso_t1_header_pcb_reset(u100_ctx);
	if (!uwbs_sync_start_download(u100_ctx))
		return FW_ERROR;
	ret = select_uwb_bl0_mode(u100_ctx);
	if (ret != FW_OK)
		return ret;
	ret = select_uwb_bl1_mode(u100_ctx);
	if (ret != FW_OK)
		return ret;
	atomic_set(&u100_ctx->flashing_fw, u100_ctx->uwb_fw_ctx.download_param.fast_mode);
	ret = try_get_bl1_version(u100_ctx);
	atomic_set(&u100_ctx->flashing_fw, 0);

	return ret;
}
