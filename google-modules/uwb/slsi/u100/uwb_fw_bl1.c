// SPDX-License-Identifier: GPL-2.0
/**
 *
 * @copyright Copyright (c) 2024 Samsung Electronics Co., Ltd
 *
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/string.h>

#include "include/uwb_fw.h"
#include "include/uwb_fw_common.h"

static unsigned long bl1_initial_poll_interval_ms = 10;
module_param(bl1_initial_poll_interval_ms, ulong, 0664);
MODULE_PARM_DESC(bl1_initial_poll_interval_ms, "Initial polling interval when downloading BL1");

static unsigned long bl1_common_poll_interval_ms = 5;
module_param(bl1_common_poll_interval_ms, ulong, 0664);
MODULE_PARM_DESC(bl1_common_poll_interval_ms, "Polling interval when downloading BL1");

static enum fw_err uwb_flash_bl1_img(struct u100_ctx *u100_ctx, uint8_t *buff, uint32_t buff_len)
{
	return uwb_flash_img(u100_ctx, buff, buff_len, MAX_APDU_DATA_LEN, false,
				bl1_initial_poll_interval_ms, bl1_common_poll_interval_ms, true);
}

int uwb_flash_bl1_prepare(struct u100_ctx *u100_ctx, struct uwb_tlv *tlv)
{
	struct uwb_firmware_ctx *fw_ctx = &u100_ctx->uwb_fw_ctx;
	int cur_bin_idx = fw_ctx->current_bin_idx;
	struct uwb_tlv *bl1_ver = fw_ctx->uwb_fw[cur_bin_idx].bl1_ver;

	if (bl1_ver && !memcmp(fw_ctx->bl1_version, bl1_ver->value, bl1_ver->len)) {
		fw_ctx->uwb_fw[cur_bin_idx].progress_data.total_size -= tlv->len;
		fw_ctx->uwb_fw[cur_bin_idx].progress_data.actual_flash_cnt--;
		UWB_INFO("Skip %s update.", tlv->label);
		return ERR_CODE(FW_ERROR_NO_UPDATE, tlv->type);
	}
	return 0;
}

int uwb_flash_bl1(struct u100_ctx *ctx, struct uwb_tlv *tlv)
{
	int ret;
	int retry_cnt = 0;

	while (1) {
		retry_cnt++;
		ret = enter_uwb_bl0_mode(ctx);
		if (ret == FW_OK)
			break;
		ctx->u100_state = U100_UNKNOWN_STATE;
		if (retry_cnt >= RETRY_MAX_CNT) {
			UWB_ERR("Select BL0 mode error(%#02x) for %s\n", ret, tlv->label);
			return ERR_CODE(ret, tlv->type);
		}
	}

	ret = uwb_flash_bl1_img(ctx, tlv->value, tlv->len);

	if (ret != FW_OK) {
		UWB_ERR("Send data error for %s.\n", tlv->label);
		return ERR_CODE(ret, tlv->type);
	}
	return 0;
}
