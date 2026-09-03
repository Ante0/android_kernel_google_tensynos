// SPDX-License-Identifier: GPL-2.0-only
/**
 * GPCA test utility functions.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/completion.h>
#include <linux/printk.h>

#include "gpca_test_common.h"

struct gpca_cb_ctx {
	int ret;
	struct completion cpl;
};

static void gpca_cb(int ret, void *cb_ctx)
{
	struct gpca_cb_ctx *gpca_cb_ctx = (struct gpca_cb_ctx *)cb_ctx;

	if (gpca_cb_ctx) {
		gpca_cb_ctx->ret = ret;
		complete(&gpca_cb_ctx->cpl);
	} else {
		pr_err("GPCA callback context is NULL.");
	}
}

int gpca_crypto_set_symm_algo_params_sync(struct gpca_dev *ctx,
					  struct gpca_crypto_op_handle *op_handle,
					  struct gpca_key_handle *key_handle,
					  enum gpca_algorithm algo,
					  enum gpca_supported_purpose purpose,
					  const u8 *iv_buf, u32 iv_size)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_set_symm_algo_params(ctx, op_handle, key_handle, algo,
					 purpose, iv_buf, iv_size, gpca_cb,
					 &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_start_symmetric_op_sync(struct gpca_dev *ctx,
					struct gpca_crypto_op_handle *op_handle,
					dma_addr_t data_addr,
					u32 input_data_size, u32 aad_size,
					u32 unencrypted_size,
					dma_addr_t output_data_addr,
					u32 output_data_size, bool is_last)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_start_symmetric_op(ctx, op_handle, data_addr,
				       input_data_size, aad_size,
				       unencrypted_size, output_data_addr,
				       output_data_size, is_last, gpca_cb,
				       &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_clear_op_sync(struct gpca_dev *ctx,
			      struct gpca_crypto_op_handle *op_handle)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_clear_op(ctx, op_handle, gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_set_hash_params_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     enum gpca_algorithm hash_algo)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_set_hash_params(ctx, op_handle, hash_algo, gpca_cb,
				    &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_set_asymm_algo_params_sync(struct gpca_dev *ctx,
					   struct gpca_crypto_op_handle *op_handle,
					   struct gpca_key_handle *key_handle,
					   enum gpca_algorithm algo,
					   enum gpca_supported_purpose purpose)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_set_asymm_algo_params(ctx, op_handle, key_handle, algo,
					  purpose, gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_start_digest_op_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     dma_addr_t input_data_addr,
				     u32 input_data_size,
				     dma_addr_t digest_addr, u32 digest_size,
				     bool is_last)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_start_digest_op(ctx, op_handle, input_data_addr,
				    input_data_size, digest_addr, digest_size,
				    is_last, gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_start_sign_op_sync(struct gpca_dev *ctx,
				   struct gpca_crypto_op_handle *op_handle,
				   dma_addr_t data_addr, u32 input_data_size,
				   dma_addr_t signature_addr,
				   u32 signature_size, bool is_last)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_start_sign_op(ctx, op_handle, data_addr, input_data_size,
				  signature_addr, signature_size, is_last,
				  gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_start_verify_op_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     dma_addr_t data_addr, u32 input_data_size,
				     u32 signature_size, bool is_last)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_start_verify_op(ctx, op_handle, data_addr, input_data_size,
				    signature_size, is_last, gpca_cb,
				    &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

int gpca_crypto_set_hmac_params_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     struct gpca_key_handle *key_handle,
				     enum gpca_algorithm hmac_algo,
				     enum gpca_supported_purpose purpose)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_set_hmac_params(ctx, op_handle, key_handle, hmac_algo,
				    purpose, gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

void reverse_buffer_in_place(u8 *buf, u32 len)
{
	u8 temp = 0;
	u32 start_idx = 0;
	u32 end_idx = len - 1;

	while (start_idx < end_idx) {
		temp = buf[end_idx];
		buf[end_idx--] = buf[start_idx];
		buf[start_idx++] = temp;
	}
}
