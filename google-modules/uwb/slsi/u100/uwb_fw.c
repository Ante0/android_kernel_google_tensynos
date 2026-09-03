// SPDX-License-Identifier: GPL-2.0
/**
 * @copyright Copyright (c) 2024 Samsung Electronics Co., Ltd
 *
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "include/uwb.h"
#include "include/uwb_fw.h"
#include "include/uwb_fw_common.h"
#include "include/uwb_spi.h"

#define FREQUENCY_CNT 3
#define TL_SIZE 5
#define FW_REQUEST_RETRIES 100
#define SET_UWB_TLV_VAL(tlv, tlv_label, retry, func_flash, func_prepare)	\
	({	typecheck(struct uwb_tlv *, (tlv));				\
		typecheck_fn(typeof((tlv)->flash), (func_flash));		\
		typecheck_fn(typeof((tlv)->prepare), (func_prepare));		\
		do {								\
			(tlv)->label = (tlv_label);				\
			(tlv)->flash = (func_flash);				\
			(tlv)->prepare = (func_prepare);			\
			(tlv)->max_retry_cnt = (retry);				\
		} while (0);							\
	})									\

static unsigned int flash_retry_cnt;
module_param(flash_retry_cnt, uint, 0664);
MODULE_PARM_DESC(flash_retry_cnt, "The max retry count of flash fw/opt/inject");

static unsigned int flash_bl1_retry_cnt = FLASH_MAX_RETRY_COUNT;
module_param(flash_bl1_retry_cnt, uint, 0664);
MODULE_PARM_DESC(flash_bl1_retry_cnt, "The max retry count of flash BL1");

static int uwb_prepare_firmware(struct u100_ctx *ctx, struct uwb_firmware *uwb_fw);

static void notify_progress(struct uwb_fw_progress_data *progress_data,
		int flashed_len, bool force, uint32_t per_pkt_size)
{
	per_pkt_size = !per_pkt_size ? MAX_APDU_DATA_LEN : per_pkt_size;
	progress_data->already_flash_size += flashed_len;
	progress_data->step_flash_size += flashed_len;
	if (force || progress_data->already_flash_size %
		progress_data->threshold_size <= per_pkt_size)
		UWB_INFO("Flash progress %d%%",
			progress_data->already_flash_size * 100 / progress_data->total_size);
}

static int cal_flash_size(struct u100_ctx *u100_ctx, struct uwb_firmware *uwb_fw)
{
	int cfg = u100_ctx->uwb_fw_ctx.config;
	int flash_cnt = uwb_fw->flash_data_cnt;
	struct uwb_tlv *flash_data = uwb_fw->flash_data_arr;

	for (; flash_cnt > 0; flash_cnt--) {
		if (flash_data[flash_cnt - 1].flash != NULL
			&& !get_cfg_by_type(cfg, flash_data[flash_cnt - 1].type)) {
			uwb_fw->progress_data.total_size += flash_data[flash_cnt - 1].len;
			uwb_fw->progress_data.actual_flash_cnt++;
		}
	}
	if (!uwb_fw->progress_data.total_size)
		return -ENODATA;
	uwb_fw->progress_data.threshold_size = uwb_fw->progress_data.total_size / FREQUENCY_CNT;
	return 0;
}

static void set_flash_config(int *config, int u100_state)
{
	switch (u100_state) {
	case U100_UNKNOWN_STATE:
	case U100_BL0_STATE:
	case U100_BL1_STATE:
		set_bit_val(config, TYPE_BL1, 0);
		set_bit_val(config, TYPE_FW, 0);
		set_bit_val(config, TYPE_OPTION, 0);
		set_bit_val(config, TYPE_INJECT, 0);
		break;
	}
}

static bool is_flash(struct u100_ctx *u100_ctx)
{
	int u100_state = U100_UNKNOWN_STATE;

	mutex_lock(&u100_ctx->atr_lock);
	u100_state = u100_ctx->u100_state;
	mutex_unlock(&u100_ctx->atr_lock);
	if (u100_ctx->uwb_fw_ctx.is_auto) {
		if (u100_state == U100_FW_STATE) /* if ATR == FW && auto, No Flash */
			return false;
		set_flash_config(&u100_ctx->uwb_fw_ctx.config, u100_state);
	}
	return true;
}

static int set_uwb_blv_info(unsigned char **fw_data, int *fw_len, struct uwb_tlv *tlv,
		struct uwb_firmware *uwb_fw)
{
	if (*fw_len < TL_SIZE)
		return -EBADMSG;
	tlv->type = **fw_data;
	(*fw_data)++;
	(*fw_len)--;
	tlv->len = *((unsigned int *)*fw_data);
	(*fw_data) += 4;
	(*fw_len) -= 4;
	if (*fw_len < tlv->len) {
		UWB_ERR("FW_FLASH Mismatched data length of type [%#02x], expect: %d, actual: %d",
			tlv->type, tlv->len, *fw_len);
		return -EBADMSG;
	}
	tlv->value = *fw_data;
	(*fw_data) += tlv->len;
	(*fw_len) -= tlv->len;

	switch (tlv->type) {
	case TYPE_FW:
		SET_UWB_TLV_VAL(tlv, "FW", flash_retry_cnt, uwb_flash_ap, NULL);
		uwb_fw->fw_tlv = tlv;
		break;
	case TYPE_INJECT:
		SET_UWB_TLV_VAL(tlv, "INJECT", flash_retry_cnt, uwb_flash_bl1, NULL);
		break;
	case TYPE_OPTION:
		SET_UWB_TLV_VAL(tlv, "OPTION", flash_retry_cnt, uwb_flash_bl1, NULL);
		break;
	case TYPE_BL1:
		SET_UWB_TLV_VAL(tlv, "BL1", flash_bl1_retry_cnt, uwb_flash_bl1,
			uwb_flash_bl1_prepare);
		break;
	case TYPE_BL1_V:
		SET_UWB_TLV_VAL(tlv, "BL1_V", 0, NULL, NULL);
		uwb_fw->bl1_ver = tlv;
		UWB_INFO("%s: %*phN", tlv->label, tlv->len, tlv->value);
		break;
	case TYPE_FW_V:
		SET_UWB_TLV_VAL(tlv, "FW_V", 0, NULL, NULL);
		UWB_INFO("%s: %.*s", tlv->label, tlv->len, tlv->value);
		break;
	case TYPE_BIN_C:
		SET_UWB_TLV_VAL(tlv, "BIN_C", 0, NULL, NULL);
		uwb_fw->checksum = tlv;
		break;
	default:
		UWB_DEBUG("FW_FLASH Unknown type[%#02x]\n", tlv->type);
		return -ENODATA;
	}

	return 0;
}

/* Entry of parse fw binary file */
static int uwb_parse_fw_binary(struct uwb_firmware *uwb_fw)
{
	int fw_len;
	unsigned char *fw_data;
	struct uwb_tlv *tlv;
	int ret = 0;
	int cnt = 0;
	int index;

	fw_len = uwb_fw->fw->size;
	fw_data = uwb_fw->fw->data;

	while (fw_len) {
		tlv = &uwb_fw->flash_data_arr[cnt];
		ret = set_uwb_blv_info(&fw_data, &fw_len, tlv, uwb_fw);
		if (!ret)
			cnt++;
		else if (ret != -ENODATA) {
			UWB_ERR("Failed to parse the firmware[%s]!\n", uwb_fw->fw_name);
			return ret;
		}
		if (cnt >= MAX_FLASH_DATA_CNT && fw_len != 0) {
			UWB_ERR("Failed to parse the firmware[%s] because of too many tlv data.\n",
					uwb_fw->fw_name);
			return -EBADMSG;
		}
	}

	if (!cnt) {
		UWB_ERR("Firmware[%s] is empty.", uwb_fw->fw_name);
		return -ENODATA;
	}
	uwb_fw->flash_data_cnt = cnt;
	for (index = 0; index < cnt; index++)
		UWB_DEBUG("FW binary data %s[%#02x], len %d\n",
				uwb_fw->flash_data_arr[index].label,
				uwb_fw->flash_data_arr[index].type,
				uwb_fw->flash_data_arr[index].len);
	return 0;
}

static int uwb_fw_flash(struct u100_ctx *u100_ctx)
{
	struct uwb_tlv *tlv;
	int index;
	int ret = 0;
	int8_t retry = 0;
	bool is_fw_fallback = false;
	struct uwb_firmware_ctx *uwb_fw_ctx = &u100_ctx->uwb_fw_ctx;
	int current_bin_idx = uwb_fw_ctx->current_bin_idx;
	struct uwb_firmware *uwb_fw = &uwb_fw_ctx->uwb_fw[current_bin_idx];

	uwb_set_spi_type(SPI_TYPE_FLASH);

	if (uwb_get_bl1_version(u100_ctx) != FW_OK)
		u100_ctx->u100_state = U100_UNKNOWN_STATE;

	for (index = 0; index < uwb_fw->flash_data_cnt; index++) {
		tlv = &uwb_fw->flash_data_arr[index];
		if (!tlv->flash || get_cfg_by_type(uwb_fw_ctx->config, tlv->type))
			continue;
		if (tlv->prepare && tlv->prepare(u100_ctx, &uwb_fw->flash_data_arr[index]))
			continue;

		for (retry = 0; retry <= tlv->max_retry_cnt; retry++) {
			UWB_INFO("FW flashing %s[%#02x] length %d",
					tlv->label, tlv->type, tlv->len);
			if (tlv->type == TYPE_BL1)
				u100_ctx->uwb_fw_ctx.bl1_retries++;
			ret = tlv->flash(u100_ctx, &uwb_fw->flash_data_arr[index]);
			if (!ret)
				break;
			u100_ctx->u100_state = U100_UNKNOWN_STATE;
			uwb_fw->progress_data.already_flash_size -=
				uwb_fw->progress_data.step_flash_size;
			uwb_fw->progress_data.step_flash_size = 0;
			if ((retry < tlv->max_retry_cnt
				&& ret == ERR_CODE(FW_ERROR_KEY_BL0, tlv->type))
				|| ret == ERR_CODE(FW_ERROR_KEY_BL1, tlv->type)) {
				if (!uwb_fw_ctx->test_fw_loaded) {
					uwb_prepare_firmware(u100_ctx,
						&uwb_fw_ctx->uwb_fw[DEFAULT_BIN_TEST_IDX]);
					uwb_fw_ctx->test_fw_loaded = true;
				}
				if (uwb_fw_ctx->uwb_fw[DEFAULT_BIN_TEST_IDX].fw != NULL) {
					uwb_fw_ctx->current_bin_idx ^= 1;
					current_bin_idx = uwb_fw_ctx->current_bin_idx;
				}
				uwb_fw = &uwb_fw_ctx->uwb_fw[current_bin_idx];
				if (ret == ERR_CODE(FW_ERROR_KEY_BL1, TYPE_FW) && !is_fw_fallback) {
					is_fw_fallback = true;
					index = -1;
					break;
				}
			}
		}

		uwb_fw->progress_data.step_flash_size = 0;
		if (ret == ERR_CODE(FW_ERROR_KEY_BL1, TYPE_FW))
			continue;
		if (ret)
			break;
		UWB_INFO("FW Flash %s[%#02x] length %d success", tlv->label, tlv->type, tlv->len);
	}

	uwb_set_spi_type(SPI_TYPE_UCI);

	return ret;
}

static int verify_checksum(struct uwb_firmware *uwb_fw)
{
	u32 crc;
	struct uwb_tlv *checksum_tlv = uwb_fw->checksum;
	size_t len;

	if (!checksum_tlv || !checksum_tlv->value || !checksum_tlv->len) {
		UWB_ERR("Can't verify firmware[%s], checksum is empty", uwb_fw->fw_name);
		return -ENOMSG;
	}

	len = uwb_fw->fw->size - checksum_tlv->len - 1 - 4;
	crc = ~crc32(~0, uwb_fw->fw->data, len);
	if (crc != *(u32 *)checksum_tlv->value) {
		UWB_ERR("Can't verify firmware[%s], expected crc32: %08X, actual crc32: %08X",
			uwb_fw->fw_name, *((u32 *)checksum_tlv->value), crc);
		return -EINVAL;
	}
	return 0;
}

/**
 * @brief Release the acquired firmware
 * @param uwb_fw Pointer to the uwb firmware
 */

static void uwb_release_firmware(struct uwb_firmware *uwb_fw)
{
	kfree(uwb_fw->fw);
	uwb_fw->fw = NULL;
	memset(uwb_fw->fw_name, 0, sizeof(uwb_fw->fw_name));
	memset(uwb_fw->flash_data_arr, 0, sizeof(struct uwb_tlv) * MAX_FLASH_DATA_CNT);
}

/**
 * @brief Load firmware data
 * @param ctx Pointer to the u100 context
 * @param uwb_fw Pointer to the uwb firmware
 * @return If the return value is 0, it's successful; otherwise, it fails.
 */

static int uwb_load_firmware(struct u100_ctx *ctx, struct uwb_firmware *uwb_fw)
{
	int ret;
	int retry_count = FW_REQUEST_RETRIES;
	const struct firmware *fw;

	UWB_DEBUG("FW_FLASH Requesting fw[%s] binary\n", uwb_fw->fw_name);
	ret = strnlen(uwb_fw->fw_name, MAX_FW_NAME_LEN);
	if (!ret) {
		UWB_ERR("No FW name");
		return -EINVAL;
	}

	do {
		fw = NULL;
		ret = request_firmware(&fw, uwb_fw->fw_name, &ctx->spi->dev);
		if (ret) {
			UWB_DEBUG("Request fw failed and retry it.");
			mdelay(1000);
		} else
			break;
		retry_count--;
	} while (ret && retry_count);

	if (ret) {
		UWB_ERR("FW_FLASH Request firmware [%s] failed (ret=%d)\n", uwb_fw->fw_name,
				ret);
		return ret;
	}

	uwb_fw->fw = kzalloc(struct_size(uwb_fw->fw, data, fw->size), GFP_KERNEL);
	if (!uwb_fw->fw) {
		release_firmware(fw);
		return -ENOMEM;
	}

	uwb_fw->fw->size = fw->size;

	memcpy(uwb_fw->fw->data, fw->data, uwb_fw->fw->size);
	release_firmware(fw);
	UWB_DEBUG("FW_FLASH FW binary size: %zu\n", uwb_fw->fw->size);
	return 0;
}

/**
 * @brief Preparation to flash firmware
 * @param ctx Pointer to the u100 context
 * @param uwb_fw Pointer to the uwb firmware
 * @return If the return value is 0, it's successful; otherwise, it fails.
 */
static int uwb_prepare_firmware(struct u100_ctx *ctx, struct uwb_firmware *uwb_fw)
{
	int ret;

	ret = uwb_load_firmware(ctx, uwb_fw);
	if (ret) {
		UWB_ERR("FW_FLASH Flash FW binary[%s] loading failed!\n", uwb_fw->fw_name);
		goto err;
	}

	ret = uwb_parse_fw_binary(uwb_fw);
	if (ret) {
		UWB_ERR("FW_FLASH Flash FW binary[%s] parsing failed!\n", uwb_fw->fw_name);
		goto err;
	}

	ret = cal_flash_size(ctx, uwb_fw);
	if (ret) {
		UWB_ERR("FW_FLASH Flash FW binary[%s] failed, size is 0!", uwb_fw->fw_name);
		goto err;
	}

	if (!ctx->uwb_fw_ctx.disable_crc_check) {
		ret = verify_checksum(uwb_fw);
		if (ret) {
			UWB_ERR("FW_FLASH Flash FW binary[%s] checksum failed!\n",
				uwb_fw->fw_name);
			goto err;
		}
	}

	return ret;
err:
	uwb_release_firmware(uwb_fw);
	return ret;
}

int fw_download(struct u100_ctx *ctx)
{
	int ret, ret_reset;
	const char *coredump_msg;
	ktime_t start_time;
	ktime_t end_time;

	start_time = ktime_get();
	memset(&ctx->uwb_fw_ctx.uwb_fw[DEFAULT_BIN_IDX].progress_data, 0,
		sizeof(struct uwb_fw_progress_data));
	memset(&ctx->uwb_fw_ctx.uwb_fw[DEFAULT_BIN_TEST_IDX].progress_data, 0,
		sizeof(struct uwb_fw_progress_data));

	if (!is_flash(ctx)) {
		UWB_DEBUG("No need to flash firmware\n");
		return 0;
	}

	try_module_get(THIS_MODULE);
	atomic_inc_return(&ctx->flashing);

	ret = uwb_prepare_firmware(ctx, &ctx->uwb_fw_ctx.uwb_fw[DEFAULT_BIN_IDX]);
	if (ret)
		goto end_fw_download;

	ctx->uwb_fw_ctx.notify_progress = notify_progress;
	ctx->uwb_fw_ctx.current_bin_idx = DEFAULT_BIN_IDX;
	ctx->uwb_fw_ctx.test_fw_loaded = false;

	ret = uwb_fw_flash(ctx);
	if (!ret) {
		UWB_INFO("Flash progress 100%%\n");
		UWB_INFO("FW_FLASH Flash FW binary[%s] success!\n",
			ctx->uwb_fw_ctx.uwb_fw[ctx->uwb_fw_ctx.current_bin_idx].fw_name);
	} else {
		UWB_ERR("FW_FLASH Flash FW binary[%s] failed!\n",
			ctx->uwb_fw_ctx.uwb_fw[ctx->uwb_fw_ctx.current_bin_idx].fw_name);
	}

	ctx->uwb_fw_ctx.notify_progress = NULL;

	/*
	 * Regardless of the fw flash result, the system resets the chip
	 * and reports a failure if it fails to power on.
	 */
	coredump_msg = ret ? "AFTER_FAILED_FLASH" : "AFTER_SUCCESSFUL_FLASH";
	ret_reset = uwbs_sync_reset_and_coredump(ctx, coredump_msg);

	/* Treat the flash as a failure if the chip can't be turned on */
	if (!ret && ret_reset)
		ret = ret_reset;

	uwb_release_firmware(&ctx->uwb_fw_ctx.uwb_fw[DEFAULT_BIN_IDX]);
	uwb_release_firmware(&ctx->uwb_fw_ctx.uwb_fw[DEFAULT_BIN_TEST_IDX]);

end_fw_download:
	atomic_dec_return(&ctx->flashing);
	module_put(THIS_MODULE);

	end_time = ktime_get();
	UWB_DEBUG("Fw flash cost [%lld]ms", ktime_to_ms(ktime_sub(end_time, start_time)));
	return ret;
}
