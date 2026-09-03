// SPDX-License-Identifier: GPL-2.0-only
/*
 * FIFO driver for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#include "gpca_cmd.h"

#include <linux/bitfield.h>
#include <linux/dev_printk.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/regmap.h>

#include "gpca_cmd_defines_internal.h"
#include "gpca_internal.h"
#include "gpca_key_policy.h"
#include "gpca_registers.h"

/* Register Poll timeout */
#define POLL_SLEEP_TIME_IN_US (1)
#define POLL_TIMEOUT_TIME_IN_US (5000000)

/* GPCA command timeout = 10s */
#define GPCA_CMD_TIMEOUT_MS (10000)

static int gpca_reg_poll_until(struct gpca_dev *gpca_dev, u32 offset, u32 mask,
			       u32 expected_val)
{
	u32 val = 0;

	return regmap_read_poll_timeout(gpca_dev->gpca_regmap, offset, val,
					((val & mask) == expected_val),
					POLL_SLEEP_TIME_IN_US,
					POLL_TIMEOUT_TIME_IN_US);
}

static bool validate_keyslot(u8 keyslot)
{
	return (keyslot >= GPCA_MIN_KEYSLOT) && (keyslot <= GPCA_MAX_KEYSLOT);
}

static bool validate_opslot(u8 opslot)
{
	return (opslot >= DOMAIN_MIN_OPSLOT) && (opslot <= DOMAIN_MAX_OPSLOT);
}

static int gpca_read_rsp_fifo(struct gpca_dev *gpca_dev, u32 fifo_offset,
			      u32 *fifo_word)
{
	int ret = 0;

	if (!fifo_word)
		return -EINVAL;

	/* Wait for Response FIFO not empty.*/
	while (gpca_reg_poll_until(gpca_dev, fifo_offset + REG_RSP_FIFO_STATUS,
				   RSP_FIFO_EMPTY, 0))
		dev_warn(gpca_dev->dev, "FIFO is empty!\n");

	/* Read Data from FIFO */
	ret = regmap_read(gpca_dev->gpca_regmap,
			  fifo_offset + REG_RSP_FIFO_DATA, fifo_word);
	dev_dbg(gpca_dev->dev,
		"Reading from FIFO offset = 0x%x val = 0x%x ret = %d",
		fifo_offset + REG_RSP_FIFO_DATA, *fifo_word, ret);

	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"Register read at offset 0x%08x failed with error %d\n",
			fifo_offset + REG_RSP_FIFO_DATA, ret);
		return ret;
	}

	return ret;
}

static void flush_rsp_fifo(struct gpca_dev *gpca_dev, u32 fifo_offset,
			   u32 rsp_size)
{
	int ret = 0;
	u32 fifo_status = 0;
	u32 fifo_data = 0;
	int i = 0;

	for (i = 0; i < rsp_size; ++i) {
		ret = gpca_read_rsp_fifo(gpca_dev, fifo_offset, &fifo_data);
		if (ret)
			return;
	}

	ret = regmap_read(gpca_dev->gpca_regmap,
			  fifo_offset + REG_RSP_FIFO_STATUS, &fifo_status);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"Register read at offset 0x%08x failed with error %d\n",
			fifo_offset + REG_RSP_FIFO_STATUS, ret);
		return;
	}

	if (!(fifo_status & RSP_FIFO_EMPTY)) {
		dev_err(gpca_dev->dev,
			"FIFO has more words than expected (%d).\n", rsp_size);

		/* Flush till FIFO is empty. This is an error case for GPCA. */
		while (!(fifo_status & RSP_FIFO_EMPTY)) {
			/* Read data from response FIFO */
			ret = gpca_read_rsp_fifo(gpca_dev, fifo_offset,
						 &fifo_data);
			if (ret)
				return;
			/* Read response FIFO status */
			ret = regmap_read(gpca_dev->gpca_regmap,
					  fifo_offset + REG_RSP_FIFO_STATUS,
					  &fifo_status);
			if (ret != 0) {
				dev_err(gpca_dev->dev,
					"Register read at offset 0x%08x failed with error %d\n",
					fifo_offset + REG_RSP_FIFO_STATUS, ret);
				return;
			}
		}
	}
}

/**
 * gpca_cmd_send() - Send GPCA command and receive response.
 *
 * @req_buf: GPCA command request buffer pointer.
 * @req_buf_size_words: GPCA command request buffer size in words.
 * @rsp_buf: GPCA command response buffer pointer where the response is copied.
 * @rsp_buf_size_words: GPCA command response buffer size in words.
 * @rsp_buf_size_words_out: Actual response size in words.
 *
 * Return : 0 on success, negative on error
 */
static int gpca_cmd_send(struct gpca_dev *gpca_dev, const u32 *req_buf,
			 u32 req_buf_size_words, u32 *rsp_buf,
			 u32 rsp_buf_size_words, u32 *rsp_buf_size_words_out)
{
	int ret = 0;
	u32 cmd_id = 0;
	u32 req_size_words = 0;
	enum gpca_cmd_error_code cmd_rsp_error = GPCA_CMD_NO_ERROR;
	u32 cmd_rsp_status = 0;
	enum gpca_cmd_fifo_type fifo_type;
	DECLARE_COMPLETION_ONSTACK(cpl);

	if (!req_buf || req_buf_size_words == 0 || !rsp_buf ||
	    rsp_buf_size_words == 0 || !rsp_buf_size_words_out ||
	    req_buf_size_words > GPCA_FIFO_MAX_WORDS ||
	    rsp_buf_size_words > GPCA_FIFO_MAX_WORDS)
		return -EINVAL;

	cmd_id = FIELD_GET(CMD_ID_FIELD, req_buf[0]);

	switch (cmd_id) {
	case GPCA_CMD_SET_CRYPTO_PARAMS:
	case GPCA_CMD_START_CRYPTO_OP_PTRS:
	case GPCA_CMD_START_CRYPTO_OP_DATA:
	case GPCA_CMD_GET_SET_CONTEXT:
	case GPCA_CMD_CLEAR_OPSLOT:
		/* Use CRYPTO FIFO for the Crypto Operations. */
		fifo_type = GPCA_FIFO_TYPE_CRYPTO;
		req_size_words = FIELD_GET(CRYPTO_REQ_SIZE_FIELD, req_buf[0]);
		break;
	case GPCA_CMD_KEY_GENERATION:
	case GPCA_CMD_KEY_DERIVATION:
	case GPCA_CMD_KEY_IMPORT:
	case GPCA_CMD_SECURE_KEY_IMPORT:
	case GPCA_CMD_KEY_WRAP:
	case GPCA_CMD_KEY_UNWRAP:
	case GPCA_CMD_SET_PUBLIC_KEY:
	case GPCA_CMD_GET_PUBLIC_KEY:
	case GPCA_CMD_SET_KEY_METADATA:
	case GPCA_CMD_CLEAR_KEY_SLOT:
	case GPCA_CMD_SEND_KEY:
	case GPCA_CMD_GET_SOFTWARE_SEED:
	case GPCA_CMD_GET_KEY_POLICY:
	case GPCA_CMD_GET_KEY_METADATA:
		/* Use KEY FIFO for the Key Operations. */
		fifo_type = GPCA_FIFO_TYPE_KEY;
		req_size_words = FIELD_GET(KEY_REQ_SIZE_FIELD, req_buf[0]);
		break;
	default:
		return -EINVAL;
	}

	// Ensure Request Header size words is correct
	if (req_buf_size_words != (req_size_words + 1))
		return -EINVAL;

	down(&gpca_dev->gpca_busy);

	gpca_dev->cur_gpca_cmd_ctx.req_buf = req_buf;
	gpca_dev->cur_gpca_cmd_ctx.req_buf_size_words = req_buf_size_words;
	gpca_dev->cur_gpca_cmd_ctx.req_buf_offset = 0;
	gpca_dev->cur_gpca_cmd_ctx.rsp_buf = rsp_buf;
	gpca_dev->cur_gpca_cmd_ctx.rsp_buf_size_words = rsp_buf_size_words;
	gpca_dev->cur_gpca_cmd_ctx.rsp_buf_offset = 0;
	gpca_dev->cur_gpca_cmd_ctx.state = GPCA_CMD_PUT_REQ;
	gpca_dev->cur_gpca_cmd_ctx.cmd_type = fifo_type;
	gpca_dev->cur_gpca_cmd_ctx.cpl = &cpl;

	if (fifo_type == GPCA_FIFO_TYPE_KEY)
		regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_KM_IMR_OFFSET, IRQ_REQ_FIFO_RDY);
	else if (fifo_type == GPCA_FIFO_TYPE_CRYPTO)
		regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_CM_IMR_OFFSET, IRQ_REQ_FIFO_RDY);

	if (wait_for_completion_timeout(&cpl, msecs_to_jiffies(GPCA_CMD_TIMEOUT_MS)) == 0)
		ret = -ETIMEDOUT;
	else
		ret = gpca_dev->cur_gpca_cmd_ctx.ret;
	*rsp_buf_size_words_out = gpca_dev->cur_gpca_cmd_ctx.rsp_buf_size_out;

	if (ret == 0) {
		/* Parse response status and error */
		cmd_rsp_status = FIELD_GET(RSP_STATUS_FIELD, rsp_buf[0]);
		cmd_rsp_error =
			((u64)FIELD_GET(RSP_ERR_FIELD0, rsp_buf[0]) |
			 (u64)FIELD_GET(RSP_ERR_FIELD1, rsp_buf[1]) << 8);

		if (cmd_rsp_status != RSP_STATUS_OK ||
		    cmd_rsp_error != GPCA_CMD_NO_ERROR) {
			dev_info(
				gpca_dev->dev,
				"Command %d failed with status = 0x%x and error = 0x%lx",
				cmd_id, cmd_rsp_status, cmd_rsp_error);
			ret = -EINVAL;
		}
	}

	up(&gpca_dev->gpca_busy);
	return ret;
}

/**
 * gpca_cmd_get_op_response_payload_ptr() - Get the operation's response buffer
 * pointer and size from GPCA response buffer.
 *
 * @fifo_type: GPCA FIFO type.
 * @gpca_rsp_buf: GPCA command response buffer pointer.
 * @gpca_rsp_buf_size_words: GPCA command response buffer size in words.
 * @op_rsp_buf: Pointer to Operation's response buffer pointer.
 * @op_rsp_buf_size_words: Operation's response buffer size in words.
 *
 * Return : Response size from the response header.
 */
static int gpca_cmd_get_op_response_payload_ptr(
	enum gpca_cmd_fifo_type fifo_type, u32 *gpca_rsp_buf,
	u32 gpca_rsp_buf_size_words, u32 **op_rsp_buf,
	u32 *op_rsp_buf_size_words)
{
	u32 gpca_rsp_size_from_hdr = 0;

	if (!gpca_rsp_buf || gpca_rsp_buf_size_words < RSP_NUM_DW ||
	    !op_rsp_buf || !op_rsp_buf_size_words)
		return -EINVAL;

	if (fifo_type == GPCA_FIFO_TYPE_CRYPTO)
		gpca_rsp_size_from_hdr =
			FIELD_GET(CRYPTO_RSP_SIZE_FIELD, gpca_rsp_buf[0]);
	else
		gpca_rsp_size_from_hdr =
			FIELD_GET(KEY_RSP_SIZE_FIELD, gpca_rsp_buf[0]);

	if (gpca_rsp_size_from_hdr <=
	    (RSP_NUM_DW - 1 /*Response header word*/)) {
		*op_rsp_buf = NULL;
		*op_rsp_buf_size_words = 0;
	} else {
		*op_rsp_buf = gpca_rsp_buf + RSP_NUM_DW;
		*op_rsp_buf_size_words =
			gpca_rsp_size_from_hdr -
			(RSP_NUM_DW - 1 /*Response header word*/);
	}

	return 0;
}

/**
 * Key Generation Request:
 * +-------------------------------------------------------+
 * |                  KEY_POLICY[63:32]                    |
 * +-------------------------------------------------------+
 * |                  KEY_POLICY[31:0]                     |
 * +---------------+--------------+-------------+----------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +---------------+--------------+-------------+----------+
 *
 * Key Generation Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_generate(struct gpca_dev *gpca_dev, u8 key_slot,
			  u64 key_policy)
{
	int ret = 0;
	u32 input[KEY_GENERATION_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_keyslot(key_slot))
		return -EINVAL;

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_KEY_GENERATION) |
		FIELD_PREP(KEY_REQ_SIZE_FIELD, KEY_GENERATION_REQ_NUM_DW - 1) |
		FIELD_PREP(KEY_GENERATION_REQ_KEYSLOT_ID_FIELD, key_slot);

	/* Copy key policy to request buffer */
	memcpy(input + KEY_GENERATION_REQ_KEY_POLICY_WORD_OFFSET, &key_policy,
	       sizeof(u64));

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA Key generate command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Key Derivation Request:
 * +---------------------------------------------------------------------------+
 * |                  SW_CONTEXT[255:0]                                        |
 * +---------------------------------------------------------------------------+
 * |                  KEY_POLICY[63:32]                                        |
 * +---------------------------------------------------------------------------+
 * |                  KEY_POLICY[31:0]                                         |
 * +--------------------------------------------------------------+------------+
 * | Reserved(31:3)                                               |CtxSize(2:0)|
 * +-----+---------------------+------------------+-------------+--------------+
 * |R(31)|DerivedKeySlot(30:23)|RootKeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)    |
 * +-----+---------------------+------------------+-------------+--------------+
 *
 * Key Derivation Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_derive(struct gpca_dev *gpca_dev, u8 root_key_slot,
			u8 dest_key_slot, u64 dest_key_policy,
			const u8 *ctx_buf, u32 ctx_buf_size_bytes)
{
	int ret = 0;
	u32 input[KEY_DERIVATION_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 ctx_buf_size_words = BYTES_TO_WORDS(ctx_buf_size_bytes);

	if (!gpca_dev || !validate_keyslot(root_key_slot) ||
	    !validate_keyslot(dest_key_slot) || !ctx_buf ||
	    ctx_buf_size_bytes == 0 ||
	    ctx_buf_size_words > KEY_DERIVATION_MAX_CONTEXT_SIZE_NUM_DW)
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_KEY_DERIVATION) |
		   FIELD_PREP(KEY_REQ_SIZE_FIELD, KEY_DERIVATION_REQ_NUM_DW - 1) |
		   FIELD_PREP(KEY_DERIVATION_REQ_ROOT_KEYSLOT_ID_FIELD,
			      root_key_slot) |
		   FIELD_PREP(KEY_DERIVATION_REQ_DERIVED_KEYSLOT_ID_FIELD,
			      dest_key_slot);
	input[1] = FIELD_PREP(KEY_DERIVATION_REQ_CONTEXT_SIZE_FIELD,
			      ctx_buf_size_words - 1);
	memcpy(input + KEY_DERIVATION_REQ_KEY_POLICY_WORD_OFFSET,
	       &dest_key_policy, sizeof(u64));
	memcpy(input + KEY_DERIVATION_REQ_SW_CONTEXT_WORD_OFFSET, ctx_buf,
	       ctx_buf_size_bytes);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA Derive key command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Key Wrapping Request:
 * +-----+--------------+----------------------+-------------+----------+
 * |R(31)|KeySlot(30:23)|WrappingKeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +-----+--------------+----------------------+-------------+----------+
 *
 * Key Wrapping Response:
 * +-------------------------------------------------------------------+
 * |               Wrapped_Key_Blob[Key_Blob_size:0]                   |
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_wrap(struct gpca_dev *gpca_dev, u8 wrapping_key_slot,
		      u8 src_key_slot, u8 *wrapped_key_buf,
		      u32 wrapped_key_buf_size, u32 *wrapped_key_buf_size_out)
{
	int ret = 0;
	u32 input[KEY_WRAP_REQ_NUM_DW] = { 0 };
	u32 output[KEY_WRAP_RSP_NUM_DW] = { 0 };
	u32 *rsp_wrapped_key_buf = NULL;
	u32 rsp_wrapped_key_buf_size_words = 0;
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_keyslot(wrapping_key_slot) ||
	    !validate_keyslot(src_key_slot) || !wrapped_key_buf ||
	    wrapped_key_buf_size == 0 || !wrapped_key_buf_size_out)
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_KEY_WRAP) |
		   FIELD_PREP(KEY_REQ_SIZE_FIELD, 0) |
		   FIELD_PREP(KEY_WRAP_REQ_WRAPPING_KEYSLOT_ID_FIELD,
			      wrapping_key_slot) |
		   FIELD_PREP(KEY_WRAP_REQ_KEYSLOT_ID_FIELD, src_key_slot);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA Wrap key command send error = %d", ret);
		return ret;
	}

	ret = gpca_cmd_get_op_response_payload_ptr(
		GPCA_FIFO_TYPE_KEY, output, output_size_words,
		&rsp_wrapped_key_buf, &rsp_wrapped_key_buf_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA wrap key get response payload pointer error = 0x%x",
			ret);
		return ret;
	}

	if (!rsp_wrapped_key_buf ||
	    wrapped_key_buf_size <
		    WORDS_TO_BYTES(rsp_wrapped_key_buf_size_words))
		return -EINVAL;

	memcpy(wrapped_key_buf, rsp_wrapped_key_buf,
	       WORDS_TO_BYTES(rsp_wrapped_key_buf_size_words));
	*wrapped_key_buf_size_out =
		WORDS_TO_BYTES(rsp_wrapped_key_buf_size_words);

	return ret;
}

/**
 * Key Unwrapping Request:
 * +--------------------------------------------------------------------+
 * |                Wrapped_Key_Blob[Key_blob_size:0]                   |
 * +-----+--------------+----------------------+-------------+----------+
 * |R(31)|KeySlot(30:23)|WrappingKeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +-----+--------------+----------------------+-------------+----------+
 *
 * Key Unwrapping Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_unwrap(struct gpca_dev *gpca_dev, u8 wrapping_key_slot,
			u8 dest_key_slot, const u8 *wrapped_key_buf,
			u32 wrapped_key_buf_size)
{
	int ret = 0;
	u32 input[KEY_UNWRAP_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	u32 input_size = KEY_UNWRAP_REQ_MIN_NUM_DW +
			 BYTES_TO_WORDS(wrapped_key_buf_size);
	if (!gpca_dev || !validate_keyslot(wrapping_key_slot) ||
	    !validate_keyslot(dest_key_slot) || !wrapped_key_buf ||
	    wrapped_key_buf_size == 0 ||
	    wrapped_key_buf_size > MAX_WRAPPED_KEY_SIZE_BYTES)
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_KEY_UNWRAP) |
		   FIELD_PREP(KEY_REQ_SIZE_FIELD, input_size - 1) |
		   FIELD_PREP(KEY_UNWRAP_REQ_WRAPPING_KEYSLOT_ID_FIELD,
			      wrapping_key_slot) |
		   FIELD_PREP(KEY_UNWRAP_REQ_KEYSLOT_ID_FIELD, dest_key_slot);
	memcpy(input + KEY_UNWRAP_REQ_WRAPPED_KEY_BLOB_WORD_OFFSET,
	       wrapped_key_buf, wrapped_key_buf_size);

	ret = gpca_cmd_send(gpca_dev, input, input_size, output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA unwrap key command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Key Send Request:
 * +-------------------------------------------------+-----------------+
 * | Reserved(31:7)                                  |DestKeyTable(6:0)|
 * +-----+------------------+-----------------+-------------+----------+
 * |R(31)|DestKeySlot(30:23)|SrcKeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +-----+------------------+-----------------+-------------+----------+
 *
 * Key Send Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_send(struct gpca_dev *gpca_dev, u8 src_key_slot,
		      u32 dest_key_table, u8 dest_key_slot)
{
	int ret = 0;
	u32 input[SEND_KEY_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_keyslot(src_key_slot))
		return -EINVAL;

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SEND_KEY) |
		FIELD_PREP(KEY_REQ_SIZE_FIELD, SEND_KEY_REQ_NUM_DW - 1) |
		FIELD_PREP(SEND_KEY_REQ_SRC_KEYSLOT_ID_FIELD, src_key_slot) |
		FIELD_PREP(SEND_KEY_REQ_DEST_KEYSLOT_ID_FIELD, dest_key_slot);
	input[1] =
		FIELD_PREP(SEND_KEY_REQ_DEST_KEYTABLE_ID_FIELD, dest_key_table);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA send key command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Key Clear Request:
 * +---------------+--------------+-------------+----------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +---------------+--------------+-------------+----------+
 *
 * Key Clear Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_clear(struct gpca_dev *gpca_dev, u8 key_slot)
{
	int ret = 0;
	u32 input[CLEAR_KEYSLOT_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_keyslot(key_slot))
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_CLEAR_KEY_SLOT) |
		   FIELD_PREP(CLEAR_KEYSLOT_REQ_KEYSLOT_ID_FIELD, key_slot);
	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA clear key command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Get Software Seed Request:
 * +---------------------------------------------------------------------------------------+
 * |                           Context[255:0]                                              |
 * +---------------------------------------------------------------------------------------+
 * |                           Label[255:0]                                                |
 * +--------------+----------------+---------------+--------------+-------------+----------+
 * |CtxSize(31:29)|LabelSize(28:26)|SeedSize(25:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +--------------+----------------+---------------+--------------+-------------+----------+
 *
 * Get Software Seed Response:
 * +-------------------------------------------------------------------+
 * |                     Software Seed[512:0]                          |
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_get_software_seed(struct gpca_dev *gpca_dev, u8 src_key_slot,
			       const u8 *label_buf, u32 label_buf_size,
			       const u8 *ctx_buf, u32 ctx_buf_size,
			       u8 *seed_buf, u32 seed_buf_size,
			       u32 *seed_buf_size_out)
{
	int ret = 0;
	u32 input[GET_SOFTWARE_SEED_REQ_NUM_DW] = { 0 };
	u32 output[GET_SOFTWARE_SEED_RSP_MIN_NUM_DW +
		   GET_SOFTWARE_SEED_MAX_SEED_SIZE_NUM_DW] = { 0 };
	u32 req_seed_size = 0;
	u32 *rsp_seed_buf = NULL;
	u32 rsp_seed_buf_size_words = 0;
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_keyslot(src_key_slot) || !seed_buf ||
	    seed_buf_size == 0 || !seed_buf_size_out || !label_buf ||
	    label_buf_size == 0 ||
	    label_buf_size >
		    WORDS_TO_BYTES(GET_SOFTWARE_SEED_MAX_LABEL_SIZE_NUM_DW) ||
	    !ctx_buf || ctx_buf_size == 0 ||
	    ctx_buf_size >
		    WORDS_TO_BYTES(GET_SOFTWARE_SEED_MAX_CONTEXT_SIZE_NUM_DW))
		return -EINVAL;

	switch (seed_buf_size) {
	case 16:
		req_seed_size = 0;
		break;
	case 32:
		req_seed_size = 1;
		break;
	case 64:
		req_seed_size = 2;
		break;
	default:
		return -EINVAL;
	}

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_GET_SOFTWARE_SEED) |
		FIELD_PREP(KEY_REQ_SIZE_FIELD, GET_SOFTWARE_SEED_REQ_NUM_DW - 1) |
		FIELD_PREP(GET_SOFTWARE_SEED_REQ_KEYSLOT_FIELD, src_key_slot) |
		FIELD_PREP(GET_SOFTWARE_SEED_REQ_SEED_SIZE_FIELD,
			   req_seed_size) |
		FIELD_PREP(GET_SOFTWARE_SEED_REQ_LABEL_SIZE_FIELD,
			   BYTES_TO_WORDS(label_buf_size) - 1) |
		FIELD_PREP(GET_SOFTWARE_SEED_REQ_CONTEXT_SIZE_FIELD,
			   BYTES_TO_WORDS(ctx_buf_size) - 1);
	/* Copy Label to request buffer */
	memcpy(input + GET_SOFTWARE_SEED_REQ_LABEL_WORD_OFFSET, label_buf,
	       label_buf_size);
	/* Copy Context buffer to request buffer */
	memcpy(input + GET_SOFTWARE_SEED_REQ_CONTEXT_WORD_OFFSET, ctx_buf, ctx_buf_size);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get software seed command send error = %d",
			ret);
		return ret;
	}

	ret = gpca_cmd_get_op_response_payload_ptr(GPCA_FIFO_TYPE_KEY, output,
						   output_size_words,
						   &rsp_seed_buf,
						   &rsp_seed_buf_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get software seed, get response payload pointer error = 0x%x",
			ret);
		return ret;
	}

	if (!rsp_seed_buf ||
	    seed_buf_size < WORDS_TO_BYTES(rsp_seed_buf_size_words))
		return -EINVAL;

	memcpy(seed_buf, rsp_seed_buf, WORDS_TO_BYTES(rsp_seed_buf_size_words));
	*seed_buf_size_out = WORDS_TO_BYTES(rsp_seed_buf_size_words);

	return ret;
}

/**
 * Key Import Request:
 *
 * +-------------------------------------------------------+
 * |                 KEY_TO_IMPORT[KeyLen:0]               |
 * +-------------------------------------------------------+
 * |                  KEY_POLICY[63:32]                    |
 * +-------------------------------------------------------+
 * |                  KEY_POLICY[31:0]                     |
 * +---------------+--------------+-------------+----------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +---------------+--------------+-------------+----------+
 *
 * Key Import Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_import(struct gpca_dev *gpca_dev, u8 key_slot, u64 key_policy,
			const u8 *key_buf, u32 key_len)
{
	int ret = 0;
	u32 input[KEY_IMPORT_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 req_size = 0;

	if (!gpca_dev || !validate_keyslot(key_slot) || !key_buf ||
	    key_len == 0 || key_len > WORDS_TO_BYTES(MAX_KEY_SIZE_WORDS))
		return -EINVAL;

	req_size = KEY_IMPORT_REQ_MIN_NUM_DW + BYTES_TO_WORDS(key_len);
	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_KEY_IMPORT) |
		   FIELD_PREP(KEY_REQ_SIZE_FIELD, req_size - 1) |
		   FIELD_PREP(KEY_IMPORT_REQ_KEYSLOT_ID_FIELD, key_slot);

	/* Copy key policy to request buffer */
	memcpy(input + KEY_IMPORT_REQ_KEY_POLICY_WORD_OFFSET, &key_policy,
	       sizeof(u64));
	/* Scramble input buffer before copying key */
	get_random_bytes(input + KEY_IMPORT_REQ_KEY_WORD_OFFSET, key_len);
	/* Copy key material to request buffer */
	memcpy(input + KEY_IMPORT_REQ_KEY_WORD_OFFSET, key_buf, key_len);

	ret = gpca_cmd_send(gpca_dev, input, req_size, output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA import key command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Get Key policy Request:
 *
 * +---------------+--------------+-------------+----------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +---------------+--------------+-------------+----------+
 *
 * Get key policy Response:
 * +-------------------------------------------------------------------+
 * |                      Key Policy[63:0]                             |
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_get_key_policy(struct gpca_dev *gpca_dev, u8 key_slot,
			    u64 *key_policy)
{
	int ret = 0;
	u32 input[GET_KEY_POLICY_REQ_NUM_DW] = { 0 };
	u32 output[GET_KEY_POLICY_RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 *rsp_gpca_key_policy_buf = NULL;
	u32 rsp_gpca_key_policy_buf_size_words = 0;

	if (!gpca_dev || !validate_keyslot(key_slot) || !key_policy)
		return -EINVAL;

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_GET_KEY_POLICY) |
		FIELD_PREP(KEY_REQ_SIZE_FIELD, GET_KEY_POLICY_REQ_NUM_DW - 1) |
		FIELD_PREP(GET_KEY_POLICY_REQ_KEYSLOT_ID_FIELD, key_slot);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get key policy command send error = %d", ret);
		return ret;
	}

	ret = gpca_cmd_get_op_response_payload_ptr(
		GPCA_FIFO_TYPE_KEY, output, output_size_words,
		&rsp_gpca_key_policy_buf, &rsp_gpca_key_policy_buf_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get key policy get response payload pointer error = 0x%x",
			ret);
		return ret;
	}

	if (!rsp_gpca_key_policy_buf ||
	    WORDS_TO_BYTES(rsp_gpca_key_policy_buf_size_words) != sizeof(u64))
		return -EINVAL;

	memcpy(key_policy, rsp_gpca_key_policy_buf,
	       WORDS_TO_BYTES(rsp_gpca_key_policy_buf_size_words));

	return ret;
}

/**
 * Get Key metadata Request:
 *
 * +---------------+--------------+-------------+----------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +---------------+--------------+-------------+----------+
 *
 * Get key metadata Response:
 * +-------------------------------------------------------------------+
 * |                      Key Metadata[95:0]                           |
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_get_key_metadata(struct gpca_dev *gpca_dev, u8 key_slot, bool *acv,
			      u8 *input_pdid, u8 *output_pdid, u64 *validity)
{
	int ret = 0;
	u32 input[GET_KEY_METADATA_REQ_NUM_DW] = { 0 };
	u32 output[GET_KEY_METADATA_RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 *rsp_gpca_key_metadata_buf = NULL;
	u32 rsp_gpca_key_metadata_buf_size_words = 0;

	if (!gpca_dev || !validate_keyslot(key_slot) || !acv || !input_pdid ||
	    !output_pdid || !validity)
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_GET_KEY_METADATA) |
		   FIELD_PREP(KEY_REQ_SIZE_FIELD,
			      GET_KEY_METADATA_REQ_NUM_DW - 1) |
		   FIELD_PREP(GET_KEY_METADATA_REQ_KEYSLOT_ID_FIELD, key_slot);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get key metadata command send error = %d", ret);
		return ret;
	}

	ret = gpca_cmd_get_op_response_payload_ptr(
		GPCA_FIFO_TYPE_KEY, output, output_size_words,
		&rsp_gpca_key_metadata_buf,
		&rsp_gpca_key_metadata_buf_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get key metadata get response payload pointer error = 0x%x",
			ret);
		return ret;
	}

	if (!rsp_gpca_key_metadata_buf ||
	    WORDS_TO_BYTES(rsp_gpca_key_metadata_buf_size_words) <
		    KEY_METADATA_SIZE_BYTES)
		return -EINVAL;

	*acv = FIELD_GET(GET_KEY_METADATA_RSP_ACV_FIELD,
			 rsp_gpca_key_metadata_buf[GET_KEY_METADATA_RSP_ACV_PDID_WORD]);
	*input_pdid = FIELD_GET(GET_KEY_METADATA_RSP_INPUT_PDID_FIELD,
				rsp_gpca_key_metadata_buf[GET_KEY_METADATA_RSP_ACV_PDID_WORD]);
	*output_pdid = FIELD_GET(GET_KEY_METADATA_RSP_OUTPUT_PDID_FIELD,
				 rsp_gpca_key_metadata_buf[GET_KEY_METADATA_RSP_ACV_PDID_WORD]);
	memcpy(validity,
	       &rsp_gpca_key_metadata_buf[GET_KEY_METADATA_RSP_VALIDITY_WORD_OFFSET],
	       sizeof(u64));

	return ret;
}

/**
 * Set Key metadata Request:
 * +-----------------------------------------------------------------+
 * |                    Validity[63:0]                               |
 * +--------------------------------+------------+------------+------+
 * | Reserved(31:9)                 |OP_PDID(8:5)|IP_PDID(4:1)|ACV(0)|
 * +---------------+--------------+--------------+-------------------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5) |   CmdID(4:0)      |
 * +---------------+--------------+--------------+-------------------+
 *
 * Set key metadata Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_set_key_metadata(struct gpca_dev *gpca_dev, u8 key_slot, bool acv,
			      u8 input_pdid, u8 output_pdid, u64 validity)
{
	int ret = 0;
	u32 input[SET_KEY_METADATA_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_keyslot(key_slot))
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SET_KEY_METADATA) |
		   FIELD_PREP(KEY_REQ_SIZE_FIELD,
			      SET_KEY_METADATA_REQ_NUM_DW - 1) |
		   FIELD_PREP(SET_KEY_METADATA_REQ_KEYSLOT_ID_FIELD, key_slot);
	input[1] =
		FIELD_PREP(SET_KEY_METADATA_REQ_ACV_FIELD, acv) |
		FIELD_PREP(SET_KEY_METADATA_REQ_INPUT_PDID_FIELD, input_pdid) |
		FIELD_PREP(SET_KEY_METADATA_REQ_OUTPUT_PDID_FIELD, output_pdid);

	/* Copy key policy to request buffer */
	memcpy(input + SET_KEY_METADATA_REQ_VALIDITY_WORD_OFFSET, &validity,
	       sizeof(u64));

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA set key metadata command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Get Public key Request:
 *
 * +---------------+--------------+-------------+----------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +---------------+--------------+-------------+----------+
 *
 * Get public key Response:
 * +-------------------------------------------------------------------+
 * |                      Public Key[4127:0]                           |
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_get_public_key(struct gpca_dev *gpca_dev, u8 key_slot,
			    u8 *pub_key_buf, u32 pub_key_buf_size,
			    u32 *pub_key_buf_size_out)
{
	int ret = 0;
	u32 input[GET_PUBLIC_KEY_REQ_NUM_DW] = { 0 };
	u32 output[GET_PUBLIC_KEY_RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 *rsp_public_key_buf = NULL;
	u32 rsp_public_key_buf_size_words = 0;

	if (!gpca_dev || !validate_keyslot(key_slot) || !pub_key_buf ||
	    pub_key_buf_size == 0 || !pub_key_buf_size_out)
		return -EINVAL;

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_GET_PUBLIC_KEY) |
		FIELD_PREP(KEY_REQ_SIZE_FIELD, GET_PUBLIC_KEY_REQ_NUM_DW - 1) |
		FIELD_PREP(GET_PUBLIC_KEY_REQ_KEYSLOT_ID_FIELD, key_slot);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get public key command send error = %d", ret);
		return ret;
	}

	ret = gpca_cmd_get_op_response_payload_ptr(
		GPCA_FIFO_TYPE_KEY, output, output_size_words,
		&rsp_public_key_buf, &rsp_public_key_buf_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get public key get response payload pointer error = 0x%x",
			ret);
		return ret;
	}

	if (!rsp_public_key_buf ||
	    pub_key_buf_size < WORDS_TO_BYTES(rsp_public_key_buf_size_words))
		return -EINVAL;

	memcpy(pub_key_buf, rsp_public_key_buf,
	       WORDS_TO_BYTES(rsp_public_key_buf_size_words));
	*pub_key_buf_size_out = WORDS_TO_BYTES(rsp_public_key_buf_size_words);

	return ret;
}

/**
 * Set Public key Request:
 * +-------------------------------------------------------+
 * |                 Public Key[4127:0]                    |
 * +---------------+--------------+-------------+----------+
 * |Reserved(31:23)|KeySlot(22:15)|ReqSize(14:5)|CmdID(4:0)|
 * +---------------+--------------+-------------+----------+
 *
 * Set public key Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_set_public_key(struct gpca_dev *gpca_dev, u8 key_slot,
			    const u8 *pub_key_buf, u32 pub_key_buf_size)
{
	int ret = 0;
	u32 input[SET_PUBLIC_KEY_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 req_size = 0;

	if (!gpca_dev || !validate_keyslot(key_slot) || !pub_key_buf ||
	    pub_key_buf_size == 0 ||
	    pub_key_buf_size > MAX_PUBLIC_KEY_SIZE_BYTES)
		return -EINVAL;

	req_size = SET_PUBLIC_KEY_REQ_MIN_NUM_DW +
		   BYTES_TO_WORDS(pub_key_buf_size);
	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SET_PUBLIC_KEY) |
		   FIELD_PREP(KEY_REQ_SIZE_FIELD, req_size - 1) |
		   FIELD_PREP(SET_PUBLIC_KEY_REQ_KEYSLOT_ID_FIELD, key_slot);

	/* Copy public key to request buffer */
	memcpy(input + SET_PUBLIC_KEY_REQ_PUBLIC_KEY_WORD_OFFSET, pub_key_buf,
	       pub_key_buf_size);

	ret = gpca_cmd_send(gpca_dev, input, req_size, output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA set public key command send error = %d", ret);
		return ret;
	}

	return ret;
}

/**
 * Secure key import Request:
 * +-----------------------------------------------------------------------------+
 * |                 Wrapped Key Blob[Size:0]                                    |
 * +-----------------------------------------------------------------------------+
 * |                 Salt[127:0]                                                 |
 * +-----------------------------------------------------------------------------+
 * |                 Context[255:0]                                              |
 * +-----------------------------------------------------------------------------+
 * |                 Key Policy[63:0]                                            |
 * +------------------------------------------------+------+-----+---------------+
 * | Reserved(31:10)                                |IKP(9)|SP(8)|OutKeySlot(7:0)|
 * +-+-----------------------+--------------------+------------------+-----------+
 * |R|ServerPubKeySlot(30:23)|ClientKeySlot(22:15)|ReqSize(14:5)     |CmdID(4:0) |
 * +-+-----------------------+--------------------+------------------+-----------+
 *
 * Secure key import Response:
 * +-------------------------------------------------------------------+
 * |                       Error Code[39:8]                            |
 * +----------------------+-------------+-----+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|R(15)|RspSize(14:5)|CmdID(4:0)|
 * +----------------------+-------------+-----+-------------+----------+
 */
int gpca_cmd_key_secure_import(struct gpca_dev *gpca_dev, u8 client_key_slot,
			       u8 server_pub_key_slot, u8 dest_key_slot,
			       bool salt_present, bool include_key_policy,
			       u64 key_policy, const u8 *ctx_buf,
			       u32 ctx_buf_size, const u8 *salt_buf,
			       u32 salt_buf_size, const u8 *wrapped_key_buf,
			       u32 wrapped_key_buf_size)
{
	int ret = 0;
	u32 input[SECURE_KEY_IMPORT_REQ_NUM_DW] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 req_size = 0;

	if (!gpca_dev || !validate_keyslot(client_key_slot) ||
	    !validate_keyslot(server_pub_key_slot) ||
	    !validate_keyslot(dest_key_slot) || !ctx_buf || ctx_buf_size == 0 ||
	    ctx_buf_size > MAX_CONTEXT_SIZE_BYTES ||
	    salt_buf_size > MAX_SALT_SIZE_BYTES || !wrapped_key_buf ||
	    wrapped_key_buf_size == 0 ||
	    wrapped_key_buf_size > MAX_WRAPPED_KEY_SIZE_BYTES)
		return -EINVAL;
	/* Salt is optional. */
	if (!salt_buf && salt_buf_size != 0)
		return -EINVAL;

	req_size = SECURE_KEY_IMPORT_REQ_MIN_NUM_DW +
		   BYTES_TO_WORDS(wrapped_key_buf_size);
	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SECURE_KEY_IMPORT) |
		FIELD_PREP(KEY_REQ_SIZE_FIELD, req_size - 1) |
		FIELD_PREP(SECURE_KEY_IMPORT_REQ_CLIENT_KEYSLOT_ID_FIELD,
			   client_key_slot) |
		FIELD_PREP(SECURE_KEY_IMPORT_REQ_SERVER_PUBLIC_KEYSLOT_ID_FIELD,
			   server_pub_key_slot);
	input[1] =
		FIELD_PREP(SECURE_KEY_IMPORT_REQ_OUTPUT_KEYSLOT_ID_FIELD,
			   dest_key_slot) |
		FIELD_PREP(SECURE_KEY_IMPORT_REQ_SP_FIELD, salt_present) |
		FIELD_PREP(SECURE_KEY_IMPORT_REQ_IKP_FIELD, include_key_policy);

	/* Copy key policy to request buffer */
	memcpy(input + SECURE_KEY_IMPORT_REQ_KEY_POLICY_WORD_OFFSET,
	       &key_policy, sizeof(u64));
	/* Copy context to request buffer */
	memcpy(input + SECURE_KEY_IMPORT_REQ_CONTEXT_WORD_OFFSET, ctx_buf,
	       ctx_buf_size);
	/* Copy salt to request buffer */
	if (salt_buf)
		memcpy(input + SECURE_KEY_IMPORT_REQ_SALT_WORD_OFFSET, salt_buf,
		       salt_buf_size);
	/* Copy wrapped key blob to request buffer */
	memcpy(input + SECURE_KEY_IMPORT_REQ_WRAPPED_KEY_WORD_OFFSET,
	       wrapped_key_buf, wrapped_key_buf_size);

	ret = gpca_cmd_send(gpca_dev, input, req_size, output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA secure key import command send error = %d",
			ret);
		return ret;
	}

	return ret;
}

/**
 * Set Crypto Params Request:
 * Hash Operation
 * +-------------+-----------------------+---------------+-------------+----------+
 * |R(31:24)     | Crypto Params (23:16) |OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +-------------+-----------------------+---------------+-------------+----------+
 * HMAC/ECC/RSA
 * +------------------------------------------------------------------+------------+
 * | R(31:4)                                                          |Purpose(3:0)|
 * +--------------+-----------------------+---------------+-------------+----------+
 * |KeySlot(31:24)| Crypto Params (23:16) |OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +--------------+-----------------------+---------------+-------------+----------+
 * AES/TDES
 * +-------------------------------------------------------------------------------+
 * |                              IV[127:0]                                        |
 * +------------------------------------------------------+-----------+------------+
 * | R(31:4)                                              |IvSize(5:4)|Purpose(3:0)|
 * +--------------+-----------------------+---------------+-------------+----------+
 * |KeySlot(31:24)| Crypto Params (23:16) |OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +--------------+-----------------------+---------------+-------------+----------+
 *
 * Set Crypto Params Response:
 * +-----------------------------------------------------------------------------+
 * |                       Error Code[39:8]                                      |
 * +----------------------+-------------+---------------+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|OpSlotID(15:11)|RspSize(10:5)|CmdID(4:0)|
 * +----------------------+-------------+---------------+-------------+----------+
 */
int gpca_cmd_set_crypto_hash_params(struct gpca_dev *gpca_dev, u8 opslot,
				    enum gpca_cmd_crypto_algo gpca_algo)
{
	int ret = 0;
	u32 input[SET_CRYPTO_PARAMS_HASH_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot))
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SET_CRYPTO_PARAMS) |
		   FIELD_PREP(CRYPTO_REQ_SIZE_FIELD,
			      SET_CRYPTO_PARAMS_HASH_SIZE_WORDS - 1) |
		   FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		   FIELD_PREP(SET_CRYPTO_PARAMS_CRYPTO_PARAMS_FIELD, gpca_algo);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA set crypto hash params command send error = %d",
			ret);
		return ret;
	}

	return ret;
}
#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
EXPORT_SYMBOL_GPL(gpca_cmd_set_crypto_hash_params);
#endif

int gpca_cmd_set_crypto_hmac_params(struct gpca_dev *gpca_dev, u8 opslot,
				    u8 keyslot,
				    enum gpca_cmd_crypto_algo gpca_algo,
				    enum gpca_cmd_crypto_op_type op_type)
{
	int ret = 0;
	u32 input[SET_CRYPTO_PARAMS_REQ_MIN_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot) || !validate_keyslot(keyslot))
		return -EINVAL;

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SET_CRYPTO_PARAMS) |
		FIELD_PREP(CRYPTO_REQ_SIZE_FIELD,
			   SET_CRYPTO_PARAMS_REQ_MIN_SIZE_WORDS - 1) |
		FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		FIELD_PREP(SET_CRYPTO_PARAMS_CRYPTO_PARAMS_FIELD, gpca_algo) |
		FIELD_PREP(SET_CRYPTO_PARAMS_KEYSLOT_ID_FIELD, keyslot);

	input[1] = FIELD_PREP(SET_CRYPTO_PARAMS_PURPOSE_FIELD, op_type);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA set crypto HMAC params command send error = %d",
			ret);
		return ret;
	}

	return ret;
}

int gpca_cmd_set_crypto_symm_algo_params(struct gpca_dev *gpca_dev, u8 opslot,
					 u8 keyslot,
					 enum gpca_cmd_crypto_algo gpca_algo,
					 enum gpca_cmd_crypto_op_type op_type,
					 const u8 *iv_buf, u32 iv_size)
{
	int ret = 0;
	u32 input[SET_CRYPTO_PARAMS_REQ_MAX_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;
	u32 req_size = 0;
	enum gpca_cmd_iv_type iv_size_field = 0;

	if (!gpca_dev || !validate_opslot(opslot) ||
	    !validate_keyslot(keyslot) || (!iv_buf && iv_size != 0) ||
	    iv_size > WORDS_TO_BYTES(SET_CRYPTO_PARAMS_MAX_IV_SIZE_WORDS))
		return -EINVAL;

	switch (iv_size) {
	case 0:
		iv_size_field = GPCA_CMD_NO_IV;
		break;
	case 8:
		iv_size_field = GPCA_CMD_8B_IV;
		break;
	case 12:
		iv_size_field = GPCA_CMD_12B_IV;
		break;
	case 16:
		iv_size_field = GPCA_CMD_16B_IV;
		break;
	default:
		return -EINVAL;
	}

	req_size =
		SET_CRYPTO_PARAMS_REQ_MIN_SIZE_WORDS + BYTES_TO_WORDS(iv_size);

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SET_CRYPTO_PARAMS) |
		FIELD_PREP(CRYPTO_REQ_SIZE_FIELD, req_size - 1) |
		FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		FIELD_PREP(SET_CRYPTO_PARAMS_CRYPTO_PARAMS_FIELD, gpca_algo) |
		FIELD_PREP(SET_CRYPTO_PARAMS_KEYSLOT_ID_FIELD, keyslot);
	input[1] = FIELD_PREP(SET_CRYPTO_PARAMS_PURPOSE_FIELD, op_type) |
		   FIELD_PREP(SET_CRYPTO_PARAMS_IV_SIZE_FIELD, iv_size_field);
	if (iv_buf)
		memcpy(input + SET_CRYPTO_PARAMS_IV_WORD_OFFSET, iv_buf,
		       iv_size);

	ret = gpca_cmd_send(gpca_dev, input, req_size, output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA set crypto Symmetric algorithm params command send error = %d",
			ret);
		return ret;
	}

	return ret;
}
#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
EXPORT_SYMBOL_GPL(gpca_cmd_set_crypto_symm_algo_params);
#endif

int gpca_cmd_set_crypto_asymm_algo_params(struct gpca_dev *gpca_dev, u8 opslot,
					  u8 keyslot,
					  enum gpca_cmd_crypto_algo gpca_algo,
					  enum gpca_cmd_crypto_op_type op_type)
{
	int ret = 0;
	u32 input[SET_CRYPTO_PARAMS_REQ_MIN_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot) || !validate_keyslot(keyslot))
		return -EINVAL;

	input[0] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_SET_CRYPTO_PARAMS) |
		FIELD_PREP(CRYPTO_REQ_SIZE_FIELD,
			   SET_CRYPTO_PARAMS_REQ_MIN_SIZE_WORDS - 1) |
		FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		FIELD_PREP(SET_CRYPTO_PARAMS_CRYPTO_PARAMS_FIELD, gpca_algo) |
		FIELD_PREP(SET_CRYPTO_PARAMS_KEYSLOT_ID_FIELD, keyslot);

	input[1] = FIELD_PREP(SET_CRYPTO_PARAMS_PURPOSE_FIELD, op_type);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA set crypto Asymmetric algorithm params command send error = %d",
			ret);
		return ret;
	}

	return ret;
}

/**
 * Start Crypto Op (Pointers) Request:
 * +--------------------------------------------------------------+-----------------------+
 * |                          Output Data size(31:8)              | OutputData Addr[39:32]|
 * +--------------------------------------------------------------------------------------+
 * |                             Output Data Addr[31:0]                                   |
 * +--------------------------------------------------------------+-----------------------+
 * |                           Input Data size(31:8)              | Data Addr[39:32]      |
 * +--------------------------------------------------------------------------------------+
 * |                             Data Addr[31:0]                                          |
 * +-----+-------+--------+----------------------+----------------------------------------+
 * |R(31)|FWD(30)|Last(29)| SignatureSize(28:16) | AAD_Size(15:0)                         |
 * +---------------------------------------------+---------------+-------------+----------+
 * |Unencrypted size(31:16)                      |OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +---------------------------------------------+---------------+-------------+----------+
 *
 * Start Crypto Op (Pointers) Response:
 * +-----------------------------------------------------------------------------+
 * |                       Error Code[39:8]                                      |
 * +----------------------+-------------+---------------+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|OpSlotID(15:11)|RspSize(10:5)|CmdID(4:0)|
 * +----------------------+-------------+---------------+-------------+----------+
 */
int gpca_cmd_start_crypto_op_ptrs(struct gpca_dev *gpca_dev, u8 opslot,
				  dma_addr_t data_addr, u32 input_data_size,
				  u32 aad_size, u32 signature_size,
				  u32 unencrypted_size,
				  dma_addr_t output_data_addr,
				  u32 output_data_size, bool fwd_input,
				  bool is_last)
{
	int ret = 0;
	u32 input[START_CRYPTO_OP_PTRS_REQ_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot) ||
	    input_data_size > START_CRYPTO_OP_PTRS_MAX_INPUT_SIZE_BYTES ||
	    aad_size > START_CRYPTO_OP_PTRS_MAX_AAD_SIZE_BYTES ||
	    signature_size > START_CRYPTO_OP_PTRS_MAX_SIGNATURE_SIZE_BYTES ||
	    unencrypted_size >
		    START_CRYPTO_OP_PTRS_MAX_UNENCRYPTED_SIZE_BYTES ||
	    output_data_size > START_CRYPTO_OP_PTRS_MAX_INPUT_SIZE_BYTES)
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_START_CRYPTO_OP_PTRS) |
		   FIELD_PREP(CRYPTO_REQ_SIZE_FIELD,
			      START_CRYPTO_OP_PTRS_REQ_SIZE_WORDS - 1) |
		   FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		   FIELD_PREP(START_CRYPTO_OP_PTRS_UNENCRYPTED_SIZE_FIELD,
			      unencrypted_size);
	input[1] =
		FIELD_PREP(START_CRYPTO_OP_PTRS_AAD_SIZE_FIELD, aad_size) |
		FIELD_PREP(START_CRYPTO_OP_PTRS_SIGNATURE_SIZE_FIELD,
			   signature_size) |
		FIELD_PREP(START_CRYPTO_OP_PTRS_FWD_FIELD, fwd_input ? 1 : 0) |
		FIELD_PREP(START_CRYPTO_OP_PTRS_LAST_FIELD, is_last ? 1 : 0);
	input[2] = FIELD_PREP(START_CRYPTO_OP_PTRS_DATA_ADDR_LSB_FIELD,
			      (u32)((u64)data_addr & 0xFFFFFFFF));
	input[3] = FIELD_PREP(START_CRYPTO_OP_PTRS_DATA_ADDR_MSB_FIELD,
			      (u32)(((u64)data_addr >> 32) & 0xFF)) |
		   FIELD_PREP(START_CRYPTO_OP_PTRS_INPUT_DATA_SIZE_FIELD,
			      input_data_size);
	input[4] = FIELD_PREP(START_CRYPTO_OP_PTRS_OUTPUT_DATA_ADDR_LSB_FIELD,
			      (u32)((u64)output_data_addr & 0xFFFFFFFF));
	input[5] = FIELD_PREP(START_CRYPTO_OP_PTRS_OUTPUT_DATA_ADDR_MSB_FIELD,
			      (u32)(((u64)output_data_addr >> 32) & 0xFF)) |
		   FIELD_PREP(START_CRYPTO_OP_PTRS_OUTPUT_DATA_SIZE_FIELD,
			      output_data_size);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA start crypto op (pointers) command send error = %d",
			ret);
		return ret;
	}

	return ret;
}

/**
 * Start Crypto Op (Data) Request:
 * +--------------------------------------------------------------------------------------------+
 * |                    Input Data[255:0]                                                       |
 * +--------------------------------------------------------------------------------------------+
 * |                    AAD[255:0]                                                              |
 * +--------+--------+-----------------+---------------+---------------+-------------+----------+
 * |R(31:28)|Last(27)|Input_size(26:22)|Aad_size(21:16)|OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +--------+--------+-----------------+---------------+---------------+-------------+----------+
 *
 * Start Crypto Op (Data) Response:
 * +-----------------------------------------------------------------------------+
 * |                       Output Data[511:0]                                    |
 * +-----------------------------------------------------------------------------+
 * |                       Error Code[39:8]                                      |
 * +----------------------+-------------+---------------+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|OpSlotID(15:11)|RspSize(10:5)|CmdID(4:0)|
 * +----------------------+-------------+---------------+-------------+----------+
 */
int gpca_cmd_start_crypto_op_data(struct gpca_dev *gpca_dev, u8 opslot,
				  const u8 *input_buf, u32 input_size,
				  const u8 *aad_buf, u32 aad_size,
				  u8 *result_buf, u32 result_size,
				  u32 *result_size_out, bool is_last)
{
	int ret = 0;
	u32 input[START_CRYPTO_OP_DATA_REQ_MAX_SIZE_WORDS] = { 0 };
	u32 output[START_CRYPTO_OP_DATA_MAX_RSP_SIZE_WORDS] = { 0 };
	u32 output_size_words = 0;
	u32 input_offset = 0;
	u32 req_size = 0;
	u32 *rsp_output_buf = NULL;
	u32 rsp_output_buf_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot) ||
	    (!input_buf && input_size != 0) ||
	    input_size > START_CRYPTO_OP_DATA_MAX_INPUT_SIZE_BYTES ||
	    (!aad_buf && aad_size != 0) ||
	    aad_size > START_CRYPTO_OP_DATA_MAX_AAD_SIZE_BYTES ||
	    (!result_buf && result_size != 0) || !result_size_out)
		return -EINVAL;

	req_size = START_CRYPTO_OP_DATA_AAD_WORD_OFFSET +
		   BYTES_TO_WORDS(aad_size) + BYTES_TO_WORDS(input_size);

	input_offset = 0;
	input[input_offset] =
		FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_START_CRYPTO_OP_DATA) |
		FIELD_PREP(CRYPTO_REQ_SIZE_FIELD, req_size - 1) |
		FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		FIELD_PREP(START_CRYPTO_OP_DATA_AAD_SIZE_FIELD, aad_size) |
		FIELD_PREP(START_CRYPTO_OP_DATA_INPUT_SIZE_FIELD, input_size) |
		FIELD_PREP(START_CRYPTO_OP_DATA_LAST_FIELD, is_last ? 1 : 0);
	input_offset = START_CRYPTO_OP_DATA_AAD_WORD_OFFSET;
	if (aad_buf) {
		memcpy(input + input_offset, aad_buf, aad_size);
		input_offset += BYTES_TO_WORDS(aad_size);
	}
	if (input_buf)
		memcpy(input + input_offset, input_buf, input_size);

	ret = gpca_cmd_send(gpca_dev, input, req_size, output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA start crypto op (data) send error = %d", ret);
		return ret;
	}

	ret = gpca_cmd_get_op_response_payload_ptr(GPCA_FIFO_TYPE_CRYPTO,
						   output, output_size_words,
						   &rsp_output_buf,
						   &rsp_output_buf_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA start crypto op(data) get response payload pointer error = 0x%x",
			ret);
		return ret;
	}

	if (result_size < WORDS_TO_BYTES(rsp_output_buf_size_words))
		return -EINVAL;

	/* GPCA response can be NULL for Last=0 commands */
	if (rsp_output_buf)
		memcpy(result_buf, rsp_output_buf,
		       WORDS_TO_BYTES(rsp_output_buf_size_words));
	*result_size_out = WORDS_TO_BYTES(rsp_output_buf_size_words);

	return ret;
}
#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
EXPORT_SYMBOL_GPL(gpca_cmd_start_crypto_op_data);
#endif

/**
 * Get Operation Context Request:
 * +-----------+------------------------------+--------------------------+
 * | R(31:24)  |   Context size(23:8)         | Context Addr[39:32]      |
 * +---------------------------------------------------------------------+
 * |                    Context Addr[31:0]                               |
 * +------------------------+---+---------------+-------------+----------+
 * |R(31:17)                | 0 |OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +------------------------+---+---------------+-------------+----------+
 *
 * Get Operation Context Response:
 * +-----------------------------------------------------------------------------+
 * |                       Error Code[39:8]                                      |
 * +----------------------+-------------+---------------+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|OpSlotID(15:11)|RspSize(10:5)|CmdID(4:0)|
 * +----------------------+-------------+---------------+-------------+----------+
 */
int gpca_cmd_get_op_context(struct gpca_dev *gpca_dev, u8 opslot,
			    dma_addr_t op_ctx_addr, u32 op_ctx_size)
{
	int ret = 0;
	u32 input[GET_SET_OP_CTX_REQ_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot) || op_ctx_size == 0 ||
	    op_ctx_size > GET_SET_OP_CTX_MAX_SIZE_BYTES)
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_GET_SET_CONTEXT) |
		   FIELD_PREP(CRYPTO_REQ_SIZE_FIELD,
			      GET_SET_OP_CTX_REQ_SIZE_WORDS - 1) |
		   FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		   FIELD_PREP(GET_SET_OP_CTX_GET_OR_SET_FIELD, 0);
	input[1] = FIELD_PREP(GET_SET_OP_CTX_CONTEXT_ADDR_LSB_FIELD,
			      (u32)((u64)op_ctx_addr & 0xFFFFFFFF));
	input[2] =
		FIELD_PREP(GET_SET_OP_CTX_CONTEXT_ADDR_MSB_FIELD,
			   (u32)(((u64)op_ctx_addr >> 32) & 0xFF)) |
		FIELD_PREP(GET_SET_OP_CTX_CONTEXT_SIZE_FIELD, op_ctx_size);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA get operation context command send error = %d",
			ret);
		return ret;
	}

	return ret;
}
#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
EXPORT_SYMBOL_GPL(gpca_cmd_get_op_context);
#endif

/**
 * Set Operation Context Request:
 * +-----------+------------------------------+--------------------------+
 * | R(31:24)  |   Context size(23:8)         | Context Addr[39:32]      |
 * +---------------------------------------------------------------------+
 * |                    Context Addr[31:0]                               |
 * +------------------------+---+---------------+-------------+----------+
 * |R(31:17)                | 1 |OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +------------------------+---+---------------+-------------+----------+
 *
 * Set Operation Context Response:
 * +-----------------------------------------------------------------------------+
 * |                       Error Code[39:8]                                      |
 * +----------------------+-------------+---------------+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|OpSlotID(15:11)|RspSize(10:5)|CmdID(4:0)|
 * +----------------------+-------------+---------------+-------------+----------+
 */
int gpca_cmd_set_op_context(struct gpca_dev *gpca_dev, u8 opslot,
			    dma_addr_t op_ctx_addr, u32 op_ctx_size)
{
	int ret = 0;
	u32 input[GET_SET_OP_CTX_REQ_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot) || op_ctx_size == 0 ||
	    op_ctx_size > GET_SET_OP_CTX_MAX_SIZE_BYTES)
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_GET_SET_CONTEXT) |
		   FIELD_PREP(CRYPTO_REQ_SIZE_FIELD,
			      GET_SET_OP_CTX_REQ_SIZE_WORDS - 1) |
		   FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot) |
		   FIELD_PREP(GET_SET_OP_CTX_GET_OR_SET_FIELD, 1);
	input[1] = FIELD_PREP(GET_SET_OP_CTX_CONTEXT_ADDR_LSB_FIELD,
			      (u32)((u64)op_ctx_addr & 0xFFFFFFFF));
	input[2] =
		FIELD_PREP(GET_SET_OP_CTX_CONTEXT_ADDR_MSB_FIELD,
			   (u32)(((u64)op_ctx_addr >> 32) & 0xFF)) |
		FIELD_PREP(GET_SET_OP_CTX_CONTEXT_SIZE_FIELD, op_ctx_size);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA set operation context command send error = %d",
			ret);
		return ret;
	}

	return ret;
}
#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
EXPORT_SYMBOL_GPL(gpca_cmd_set_op_context);
#endif

/**
 * Clear Opslot Request:
 * +---------------------------+---------------+-------------+----------+
 * |R(31:16)                   |OpSlotID(15:11)|ReqSize(10:5)|CmdID(4:0)|
 * +---------------------------+---------------+-------------+----------+
 *
 * Clear Opslot Response:
 * +-----------------------------------------------------------------------------+
 * |                       Error Code[39:8]                                      |
 * +----------------------+-------------+---------------+-------------+----------+
 * |Error code[7:0](31:24)|Status(23:16)|OpSlotID(15:11)|RspSize(10:5)|CmdID(4:0)|
 * +----------------------+-------------+---------------+-------------+----------+
 */
int gpca_cmd_clear_opslot(struct gpca_dev *gpca_dev, u8 opslot)
{
	int ret = 0;
	u32 input[CLEAR_OPSLOT_REQ_SIZE_WORDS] = { 0 };
	u32 output[RSP_NUM_DW] = { 0 };
	u32 output_size_words = 0;

	if (!gpca_dev || !validate_opslot(opslot))
		return -EINVAL;

	input[0] = FIELD_PREP(CMD_ID_FIELD, GPCA_CMD_CLEAR_OPSLOT) |
		   FIELD_PREP(CRYPTO_REQ_SIZE_FIELD,
			      CLEAR_OPSLOT_REQ_SIZE_WORDS - 1) |
		   FIELD_PREP(CRYPTO_REQ_OPSLOT_ID_FIELD, opslot);

	ret = gpca_cmd_send(gpca_dev, input, ARRAY_SIZE(input), output,
			    ARRAY_SIZE(output), &output_size_words);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA clear opslot command send error = %d", ret);
		return ret;
	}

	return ret;
}
#if IS_ENABLED(CONFIG_GOOGLE_GPCA_KEY_TEST)
EXPORT_SYMBOL_GPL(gpca_cmd_clear_opslot);
#endif

static void process_gpca_req_rdy(struct work_struct *work)
{
	struct gpca_dev *gpca_dev =
		container_of(work, struct gpca_dev, gpca_cmd_process_req);
	u32 fifo_offset = 0;
	u32 imr_offset = 0;
	u32 isr_offset = 0;
	u32 req_fifo_status = 0;
	u32 req_fifo_empty_slots = 0;

	if (gpca_dev->cur_gpca_cmd_ctx.cmd_type == GPCA_FIFO_TYPE_KEY) {
		fifo_offset = DOMAIN_GPCA_KEY_FIFO_OFFSET;
		imr_offset = DOMAIN_GPCA_KM_IMR_OFFSET;
		isr_offset = DOMAIN_GPCA_KM_ISR_OFFSET;
	} else {
		fifo_offset = DOMAIN_GPCA_CRYPTO_FIFO_OFFSET;
		imr_offset = DOMAIN_GPCA_CM_IMR_OFFSET;
		isr_offset = DOMAIN_GPCA_CM_ISR_OFFSET;
	}

	/* Read number of empty slots */
	regmap_read(gpca_dev->gpca_regmap, fifo_offset + REG_REQ_FIFO_STATUS, &req_fifo_status);
	req_fifo_empty_slots = GPCA_FIFO_SIZE - FIELD_GET(REQ_FIFO_CNTR, req_fifo_status);

	// Write min(pending req words, empty slots) to REQ_FIFO
	while (req_fifo_empty_slots > 0 &&
	       gpca_dev->cur_gpca_cmd_ctx.req_buf_offset <
		   gpca_dev->cur_gpca_cmd_ctx.req_buf_size_words) {
		// If last request word enable upstreaming of RSP_FIFO_RDY interrupt
		if (gpca_dev->cur_gpca_cmd_ctx.req_buf_offset ==
		    (gpca_dev->cur_gpca_cmd_ctx.req_buf_size_words - 1)) {
			regmap_write(gpca_dev->gpca_regmap, imr_offset, IRQ_RSP_FIFO_RDY);
			gpca_dev->cur_gpca_cmd_ctx.state = GPCA_CMD_GET_RSP;
		}

		regmap_write(gpca_dev->gpca_regmap,
			     fifo_offset + REG_REQ_FIFO_DATA,
			     gpca_dev->cur_gpca_cmd_ctx
				.req_buf[gpca_dev->cur_gpca_cmd_ctx.req_buf_offset++]);

		req_fifo_empty_slots--;
	}

	// Clear ISR REQ_FIFO_RDY
	regmap_write(gpca_dev->gpca_regmap, isr_offset, IRQ_REQ_FIFO_RDY);

	// If more words are to be written to REQ_FIFO, unmask REQ_FIFO_RDY
	if (gpca_dev->cur_gpca_cmd_ctx.req_buf_offset <
	    gpca_dev->cur_gpca_cmd_ctx.req_buf_size_words)
		regmap_write(gpca_dev->gpca_regmap, imr_offset,
			     IRQ_REQ_FIFO_RDY);
}

static void process_gpca_rsp_rdy(struct work_struct *work)
{
	u32 rsp_fifo_cntr = 0;
	u32 rsp_fifo_status = 0;
	u32 rsp_first_word = 0;
	u32 rsp_size_words = 0;
	u32 fifo_offset = 0;
	u32 imr_offset = 0;
	u32 isr_offset = 0;
	u32 rsp_fifo_num_extra_words = 0;
	struct gpca_dev *gpca_dev =
		container_of(work, struct gpca_dev, gpca_cmd_process_rsp);

	if (gpca_dev->cur_gpca_cmd_ctx.cmd_type == GPCA_FIFO_TYPE_KEY) {
		fifo_offset = DOMAIN_GPCA_KEY_FIFO_OFFSET;
		imr_offset = DOMAIN_GPCA_KM_IMR_OFFSET;
		isr_offset = DOMAIN_GPCA_KM_ISR_OFFSET;
	} else {
		fifo_offset = DOMAIN_GPCA_CRYPTO_FIFO_OFFSET;
		imr_offset = DOMAIN_GPCA_CM_IMR_OFFSET;
		isr_offset = DOMAIN_GPCA_CM_ISR_OFFSET;
	}

	// Read number of words in RSP_FIFO
	regmap_read(gpca_dev->gpca_regmap,
		    fifo_offset + REG_RSP_FIFO_STATUS,
		    &rsp_fifo_status);
	rsp_fifo_cntr = FIELD_GET(RSP_FIFO_CNTR, rsp_fifo_status);

	if (rsp_fifo_cntr == 0) {
		// Enable RSP_FIFO_RDY IRQ upstreaming
		regmap_write(gpca_dev->gpca_regmap, imr_offset, IRQ_RSP_FIFO_RDY);
		goto exit;
	}

	// If first response word, parse and get response size
	if (gpca_dev->cur_gpca_cmd_ctx.rsp_buf_offset == 0) {
		regmap_read(gpca_dev->gpca_regmap,
			    fifo_offset + REG_RSP_FIFO_DATA,
			    &rsp_first_word);
		rsp_fifo_cntr--;

		if (gpca_dev->cur_gpca_cmd_ctx.cmd_type == GPCA_FIFO_TYPE_KEY)
			rsp_size_words = FIELD_GET(KEY_RSP_SIZE_FIELD, rsp_first_word);
		else if (gpca_dev->cur_gpca_cmd_ctx.cmd_type == GPCA_FIFO_TYPE_CRYPTO)
			rsp_size_words = FIELD_GET(CRYPTO_RSP_SIZE_FIELD, rsp_first_word);

		gpca_dev->cur_gpca_cmd_ctx.rsp_buf_size_out = rsp_size_words + 1;

		// If there isn't sufficient place in rsp buffer flush fifo and return error
		if ((rsp_size_words + 1) > gpca_dev->cur_gpca_cmd_ctx.rsp_buf_size_words) {
			flush_rsp_fifo(gpca_dev, fifo_offset, rsp_size_words);
			gpca_dev->cur_gpca_cmd_ctx.ret = -EINVAL;
			gpca_dev->cur_gpca_cmd_ctx.state = GPCA_CMD_INVALID_STATE;
			complete(gpca_dev->cur_gpca_cmd_ctx.cpl);
			regmap_write(gpca_dev->gpca_regmap, imr_offset, 0);
			goto exit;
		} else {
			gpca_dev->cur_gpca_cmd_ctx
			    .rsp_buf[gpca_dev->cur_gpca_cmd_ctx
					 .rsp_buf_offset++] = rsp_first_word;
		}
	}
	// Read response from FIFO
	while (rsp_fifo_cntr > 0) {
		regmap_read(gpca_dev->gpca_regmap, fifo_offset + REG_RSP_FIFO_DATA,
			    &gpca_dev->cur_gpca_cmd_ctx
				.rsp_buf[gpca_dev->cur_gpca_cmd_ctx.rsp_buf_offset++]);
		rsp_fifo_cntr--;
	}

	// If more words pending to be read for command response, enable RSP_FIFO_RDY
	if (gpca_dev->cur_gpca_cmd_ctx.rsp_buf_offset <
	    gpca_dev->cur_gpca_cmd_ctx.rsp_buf_size_out) {
		regmap_write(gpca_dev->gpca_regmap, imr_offset,
			     IRQ_RSP_FIFO_RDY);
	} else {
		regmap_read(gpca_dev->gpca_regmap,
			    fifo_offset + REG_RSP_FIFO_STATUS, &rsp_fifo_status);
		if ((rsp_fifo_status & RSP_FIFO_EMPTY) == 0) {
			/* b/263688029: Flush extra words from FIFO */
			rsp_fifo_num_extra_words =
			    FIELD_GET(RSP_FIFO_CNTR, rsp_fifo_status);
			dev_warn(gpca_dev->dev,
				 "Flushing %d extra words from FIFO",
				 rsp_fifo_num_extra_words);
			flush_rsp_fifo(gpca_dev, fifo_offset,
				       rsp_fifo_num_extra_words);
		}

		gpca_dev->cur_gpca_cmd_ctx.state = GPCA_CMD_INVALID_STATE;
		gpca_dev->cur_gpca_cmd_ctx.ret = 0;
		complete(gpca_dev->cur_gpca_cmd_ctx.cpl);
		regmap_write(gpca_dev->gpca_regmap, imr_offset, 0);
	}
exit:
	// Clear ISR RSP_FIFO_RDY
	regmap_write(gpca_dev->gpca_regmap, isr_offset, IRQ_RSP_FIFO_RDY);
}

static irqreturn_t gpca_irq_handler(int irq, void *data)
{
	struct gpca_dev *gpca_dev = data;
	u32 km_isr = 0;
	u32 cm_isr = 0;

	dev_dbg(gpca_dev->dev, "%s: got irq %d\n", __func__, irq);

	// Read ISRs
	regmap_read(gpca_dev->gpca_regmap, DOMAIN_GPCA_KM_ISR_OFFSET, &km_isr);
	regmap_read(gpca_dev->gpca_regmap, DOMAIN_GPCA_CM_ISR_OFFSET, &cm_isr);
	// Disable interrupts
	regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_KM_IMR_OFFSET, 0);
	regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_CM_IMR_OFFSET, 0);

	if (km_isr & IRQ_REQ_FIFO_OVERFLOW || km_isr & IRQ_RSP_FIFO_UNDERFLOW ||
	    cm_isr & IRQ_REQ_FIFO_OVERFLOW || cm_isr & IRQ_RSP_FIFO_UNDERFLOW) {
		dev_err(gpca_dev->dev, "FIFO overflow or underflow detected.");
		// Complete current GPCA command and return error
		gpca_dev->cur_gpca_cmd_ctx.ret = -EFAULT;
		gpca_dev->cur_gpca_cmd_ctx.state = GPCA_CMD_INVALID_STATE;
		complete(gpca_dev->cur_gpca_cmd_ctx.cpl);
		return IRQ_HANDLED;
	}

	if (gpca_dev->cur_gpca_cmd_ctx.cmd_type == GPCA_FIFO_TYPE_KEY) {
		if (gpca_dev->cur_gpca_cmd_ctx.state == GPCA_CMD_PUT_REQ &&
		    (km_isr & IRQ_REQ_FIFO_RDY))
			schedule_work(&gpca_dev->gpca_cmd_process_req);
		else if (gpca_dev->cur_gpca_cmd_ctx.state == GPCA_CMD_GET_RSP &&
			 (km_isr & IRQ_RSP_FIFO_RDY))
			schedule_work(&gpca_dev->gpca_cmd_process_rsp);
	} else if (gpca_dev->cur_gpca_cmd_ctx.cmd_type ==
		   GPCA_FIFO_TYPE_CRYPTO) {
		if (gpca_dev->cur_gpca_cmd_ctx.state == GPCA_CMD_PUT_REQ &&
		    (cm_isr & IRQ_REQ_FIFO_RDY))
			schedule_work(&gpca_dev->gpca_cmd_process_req);
		else if (gpca_dev->cur_gpca_cmd_ctx.state == GPCA_CMD_GET_RSP &&
			 (cm_isr & IRQ_RSP_FIFO_RDY))
			schedule_work(&gpca_dev->gpca_cmd_process_rsp);
	}
	return IRQ_HANDLED;
}

int gpca_cmd_init(struct gpca_dev *gpca_dev)
{
	int ret = 0;
	int gpca_irq = 0;
	struct platform_device *pdev = NULL;

	pdev = container_of(gpca_dev->dev, struct platform_device, dev);

	/* Get interrupt number from platform data */
	gpca_irq = platform_get_irq(pdev, 0);
	if (gpca_irq < 0)
		return gpca_irq;

	/* Register interrupt handler */
	ret = devm_request_irq(gpca_dev->dev, gpca_irq, gpca_irq_handler, IRQF_SHARED,
			       dev_name(gpca_dev->dev), gpca_dev);
	if (ret != 0) {
		dev_err(gpca_dev->dev, "Failed to request IRQ");
		return ret;
	}

	/* Enable the interrupts. */
	regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_KM_IMR_OFFSET, 0);
	regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_KM_IER_OFFSET, GPCA_ALL_IRQS_MASK);

	regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_CM_IMR_OFFSET, 0);
	regmap_write(gpca_dev->gpca_regmap, DOMAIN_GPCA_CM_IER_OFFSET, GPCA_ALL_IRQS_MASK);

	INIT_WORK(&gpca_dev->gpca_cmd_process_req, process_gpca_req_rdy);
	INIT_WORK(&gpca_dev->gpca_cmd_process_rsp, process_gpca_rsp_rdy);

	gpca_dev->cur_gpca_cmd_ctx.state = GPCA_CMD_INVALID_STATE;

	return ret;
}
