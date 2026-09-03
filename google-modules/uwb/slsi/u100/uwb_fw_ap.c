// SPDX-License-Identifier: GPL-2.0
/**
 *
 * @copyright Copyright (c) 2024 Samsung Electronics Co., Ltd
 *
 */
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>

#include "include/uwb_fw_common.h"
#include "include/uwb_spi.h"

#define HEADER_LEN 20
#define BL_HEADER_LEN 16
#define MAX_RECV_PKT_LEN 8
#define PROCESS_DATA_TIMEOUT_MS 2000

static int fw_download_pkt_per_size = 2048;
module_param(fw_download_pkt_per_size, int, 0664);
MODULE_PARM_DESC(fw_download_pkt_per_size, "Size of each packet for firmware download");

static unsigned long fw_initial_poll_interval_ms = 10;
module_param(fw_initial_poll_interval_ms, ulong, 0664);
MODULE_PARM_DESC(fw_initial_poll_interval_ms, "Initial polling interval when downloading FW");

static unsigned long fw_common_poll_interval_ms = 5;
module_param(fw_common_poll_interval_ms, ulong, 0664);
MODULE_PARM_DESC(fw_common_poll_interval_ms, "Polling interval when downloading FW");

void handle_fw_ap_send(struct u100_ctx *u100_ctx)
{
	int ret = 0;

	if (!wait_for_completion_timeout(&u100_ctx->process_done_cmpl,
			msecs_to_jiffies(PROCESS_DATA_TIMEOUT_MS)))
		UWB_WARN("Firmware package processing timeout.");
	reinit_completion(&u100_ctx->process_done_cmpl);
	switch (u100_ctx->uwb_fw_ctx.action) {
	case ACTION_SEND_GET_BL1_VER_CMD:
		ret = uwb_spi_send(u100_ctx, u100_ctx->uwb_fw_ctx.cmd,
				u100_ctx->uwb_fw_ctx.cmd_len, NULL);
		break;
	case ACTION_RECV_BL1_VER_RESP:
		ret = uwb_spi_send(u100_ctx, u100_ctx->uwb_fw_ctx.send_block,
				u100_ctx->uwb_fw_ctx.cmd_rsp_len, u100_ctx->uwb_fw_ctx.cmd_rsp);
		break;
	case ACTION_SEND_HEADER:
		ret = uwb_spi_send(u100_ctx, u100_ctx->uwb_fw_ctx.send_block,
				u100_ctx->uwb_fw_ctx.spi_len, NULL);
		break;
	case ACTION_SEND_BODY:
		ret = uwb_spi_send(u100_ctx, u100_ctx->uwb_fw_ctx.send_block,
				u100_ctx->uwb_fw_ctx.spi_len, u100_ctx->uwb_fw_ctx.read_block);
		break;
	case ACTION_RECV_RESP:
		ret = uwb_spi_recv(u100_ctx, u100_ctx->uwb_fw_ctx.read_block, MAX_RECV_PKT_LEN);
		break;
	default:
		break;
	}
	if (ret < 0)
		UWB_ERR("Spi error %d when handle action %d", ret, u100_ctx->uwb_fw_ctx.action);
	complete_all(&u100_ctx->tx_done_cmpl);
}

static enum fw_err uwb_fw_flash_header(struct u100_ctx *u100_ctx,
			struct uwb_tlv *tlv, u16 len)
{
	enum fw_err ret;
	uint32_t send_block_len = 0;
	struct iso_t1_header iso_header;
	struct fw_apdu apdu_pkt;
	int read_block_len;

	if (len > MAX_ISO_PKT_LEN - ISO_T1_HEADER_LEN - FW_APDU_T_LEN - LRC8_LEN)
		return FW_ERROR_PARAMETER;

	iso_t1_header_init(&iso_header, FW_APDU_T_LEN + len, u100_ctx);
	apdu_header_init(&apdu_pkt, 0x20, 0x00, len);

	send_block_len = init_block(u100_ctx->uwb_fw_ctx.send_block, iso_header, ISO_T1_HEADER_LEN,
			apdu_pkt, FW_APDU_T_LEN, tlv->value, len);
	if (send_block_len <= 0)
		return FW_ERROR_IO;

	ret = iso_iblock_send(u100_ctx, u100_ctx->uwb_fw_ctx.send_block, send_block_len);
	if (ret != FW_OK)
		return ret;

	read_block_len = read_iso_iblock(u100_ctx, u100_ctx->uwb_fw_ctx.read_block,
			MAX_ISO_PKT_LEN, fw_initial_poll_interval_ms,
			fw_common_poll_interval_ms, false);
	if (read_block_len == -ETIMEDOUT)
		return FW_ERROR_TIME;

	if (read_block_len < FW_OK)
		return FW_ERROR_IO;

	return parse_iso_iblock(u100_ctx->uwb_fw_ctx.read_block, read_block_len);
}

static enum fw_err uwb_fw_flash_header_fast_mode(struct u100_ctx *u100_ctx,
			struct uwb_tlv *tlv, int len)
{
	enum fw_err ret;
	uint32_t send_block_len = 0;
	struct iso_t1_header iso_header;
	struct fw_apdu apdu_pkt;

	if (len > fw_download_pkt_per_size)
		return FW_ERROR_PARAMETER;

	iso_t1_header_init(&iso_header, FW_APDU_T_FM_LEN + len, u100_ctx);
	apdu_header_init(&apdu_pkt, 0x20, 0x00, len);

	send_block_len = init_block(u100_ctx->uwb_fw_ctx.send_block, iso_header,
			ISO_T1_HEADER_FM_LEN, apdu_pkt, FW_APDU_T_FM_LEN, tlv->value, len);
	if (send_block_len == 0)
		return FW_ERROR_IO;

	u100_ctx->uwb_fw_ctx.spi_len = ALIGN(send_block_len, 4);
	u100_ctx->uwb_fw_ctx.action = ACTION_SEND_HEADER;

	reinit_completion(&u100_ctx->tx_done_cmpl);
	complete_all(&u100_ctx->process_done_cmpl);

	ret = wait_for_completion_timeout(&u100_ctx->tx_done_cmpl,
			msecs_to_jiffies(WAIT_INTERRUPT_TIMEOUT_MS));
	if (!ret) {
		UWB_INFO("Irq timeout and retry send");
		handle_fw_ap_send(u100_ctx);
	}

	return FW_OK;
}

static enum fw_err uwb_flash_fw_img_fast_mode(struct u100_ctx *u100_ctx,
			uint8_t *data, uint32_t file_size, char p1)
{
	enum fw_err ret = FW_OK;
	uint32_t send_block_len;
	struct fw_apdu apdu_pkt;
	struct iso_t1_header iso_pkt;
	uint32_t index;
	bool is_last_packet;
	uint32_t actual_len = 0;
	uint32_t total_package = (file_size + fw_download_pkt_per_size - 1)
							/ fw_download_pkt_per_size;
	struct uwb_firmware_ctx *uwb_fw_ctx = &u100_ctx->uwb_fw_ctx;

	uwb_fw_ctx->action = ACTION_SEND_BODY;
	for (index = 0; index < total_package; index++) {
		is_last_packet = (index + 1) * fw_download_pkt_per_size >= file_size;
		actual_len = is_last_packet ? (file_size - index * fw_download_pkt_per_size) :
				fw_download_pkt_per_size;

		send_block_len = 0;
		memset(&apdu_pkt, 0, FW_APDU_T_FM_LEN);
		memset(&iso_pkt, 0, ISO_T1_HEADER_FM_LEN);
		memset(uwb_fw_ctx->read_block, 0, MAX_RECV_PKT_LEN);

		if (is_last_packet)
			apdu_header_init(&apdu_pkt, p1, 0x01, actual_len);
		else
			apdu_header_init(&apdu_pkt, 0x20, 0x01, actual_len);
		iso_t1_header_init(&iso_pkt, actual_len + FW_APDU_T_FM_LEN, u100_ctx);

		send_block_len = init_block(uwb_fw_ctx->send_block, iso_pkt,
						ISO_T1_HEADER_FM_LEN, apdu_pkt, FW_APDU_T_FM_LEN,
						data, actual_len);
		if (send_block_len == 0)
			return FW_ERROR_IO;

		data += actual_len;
		uwb_fw_ctx->spi_len = ALIGN(send_block_len, 4);

		reinit_completion(&u100_ctx->tx_done_cmpl);
		complete_all(&u100_ctx->process_done_cmpl);

		ret = wait_for_completion_timeout(&u100_ctx->tx_done_cmpl,
				msecs_to_jiffies(WAIT_INTERRUPT_TIMEOUT_MS));
		if (ret == 0) {
			ret = FW_ERROR_IO;
			UWB_ERR("Send packet timeout for %dms in fw downloading process",
					WAIT_INTERRUPT_TIMEOUT_MS);
			return ret;
		}

		ret = check_iso_iblock(uwb_fw_ctx->read_block, MAX_SPI_BLOCK_SIZE);
		if (ret != FW_OK) {
			UWB_ERR("FW fast download error when %d/%d packet", index, total_package);
			print_hex_dump(KERN_ERR, LOG_TAG "APDU Recv data: ",
					DUMP_PREFIX_NONE, MAX_RECV_PKT_LEN, 1,
					uwb_fw_ctx->read_block, MAX_RECV_PKT_LEN, false);
			return ret;
		}
		if (!is_last_packet)
			uwb_fw_ctx->notify_progress(
				&uwb_fw_ctx->uwb_fw[uwb_fw_ctx->current_bin_idx].progress_data,
				actual_len, is_last_packet, fw_download_pkt_per_size);
	}

	memset(uwb_fw_ctx->read_block, 0, MAX_RECV_PKT_LEN);
	uwb_fw_ctx->action = ACTION_RECV_RESP;
	reinit_completion(&u100_ctx->tx_done_cmpl);
	complete_all(&u100_ctx->process_done_cmpl);

	ret = wait_for_completion_timeout(&u100_ctx->tx_done_cmpl,
				msecs_to_jiffies(WAIT_INTERRUPT_TIMEOUT_MS));
	uwb_fw_ctx->action = ACTION_IDLE;
	if (ret == 0) {
		UWB_ERR("Read last rsp timeout for %dms in fw downloading process",
				WAIT_INTERRUPT_TIMEOUT_MS);
		ret = FW_ERROR_IO;
		return ret;
	}

	ret = check_iso_iblock(uwb_fw_ctx->read_block, MAX_RECV_PKT_LEN);
	complete_all(&u100_ctx->process_done_cmpl);
	if (ret != FW_OK) {
		print_hex_dump(KERN_ERR, LOG_TAG "Last APDU Recv data: ",
				DUMP_PREFIX_NONE, MAX_RECV_PKT_LEN, 1,
				uwb_fw_ctx->read_block, MAX_RECV_PKT_LEN, false);
		return ret;
	}

	return FW_OK;
}

int uwb_flash_ap(struct u100_ctx *ctx, struct uwb_tlv *tlv)
{
	int ret;
	int retry_cnt = 0;
	struct uwb_firmware_ctx *uwb_fw_ctx = &ctx->uwb_fw_ctx;

	reinit_completion(&ctx->process_done_cmpl);

	while (1) {
		retry_cnt++;
		ret = enter_uwb_bl1_mode(ctx);
		if (ret == FW_OK)
			break;
		ctx->u100_state = U100_UNKNOWN_STATE;
		if (retry_cnt >= RETRY_MAX_CNT) {
			UWB_ERR("Select BL1 error(%#02x) for %s\n", ret, tlv->label);
			return ERR_CODE(ret, tlv->type);
		}
	}

	UWB_DEBUG("Start send header for %s", tlv->label);
	if (uwb_fw_ctx->download_param.fast_mode) {
		if (fw_download_pkt_per_size > uwb_fw_ctx->download_param.max_segment_size
			|| fw_download_pkt_per_size > MAX_AP_APDU_DATA_FM_LEN) {
			UWB_ERR("Segment size is too large, maximum supported is %d bytes",
					uwb_fw_ctx->download_param.max_segment_size);
			return ERR_CODE(EINVAL, tlv->type);
		}
		atomic_inc_return(&ctx->flashing_fw);
		reinit_completion(&ctx->tx_done_cmpl);
		ret = uwb_fw_flash_header_fast_mode(ctx, tlv, HEADER_LEN);
	} else
		ret = uwb_fw_flash_header(ctx, tlv, HEADER_LEN);
	if (ret != FW_OK) {
		UWB_ERR("Send header error(%#02x) for %s\n", ret, tlv->label);
		if (uwb_fw_ctx->download_param.fast_mode)
			atomic_dec_return(&ctx->flashing_fw);
		return ERR_CODE(EIO, tlv->type);
	}
	UWB_DEBUG("Send header success for %s\n", tlv->label);

	UWB_DEBUG("BL1 cap: fast_mode=%d, max_segment_size=%d, max_spi_speed=%d",
		uwb_fw_ctx->download_param.fast_mode, uwb_fw_ctx->download_param.max_segment_size,
		uwb_fw_ctx->download_param.download_spi_speed);
	if (uwb_fw_ctx->download_param.fast_mode)
		ret = uwb_flash_fw_img_fast_mode(ctx, tlv->value + HEADER_LEN,
			tlv->len - HEADER_LEN, 0xA0);
	else
		ret = uwb_flash_img(ctx, tlv->value + HEADER_LEN,
			tlv->len - HEADER_LEN, MAX_AP_APDU_DATA_LEN, true,
			fw_initial_poll_interval_ms, fw_common_poll_interval_ms, false);

	if (uwb_fw_ctx->download_param.fast_mode)
		atomic_dec_return(&ctx->flashing_fw);
	if (ret != FW_OK) {
		UWB_ERR("Send data error for %s.\n", tlv->label);
		return ERR_CODE(ret, tlv->type);
	}
	UWB_DEBUG("Send data success for %s\n", tlv->label);
	return 0;
}
