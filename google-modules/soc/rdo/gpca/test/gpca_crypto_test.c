// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Crypto unit tests.
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/types.h>

#include "gpca_cmd.h"
#include "gpca_crypto_test.h"
#include "gpca_internal.h"
#include "gpca_keys.h"
#include "gpca_sha_test_vectors.h"
#include "gpca_test_common.h"

struct gpca_dev *gpca_ctx;

void gpca_crypto_start_crypto_op_data(struct kunit *test)
{
	u32 j = 0;
	u8 opslot = 3;
	const u8 key[] = { 0x95, 0xbc, 0xde, 0x70, 0xc0, 0x94, 0xf0, 0x4e,
			   0x3d, 0xd8, 0x25, 0x9c, 0xaf, 0xd8, 0x8c, 0xe8 };
	const u8 iv[] = { 0x12, 0xcf, 0x09, 0x7a, 0xd2, 0x23,
			  0x80, 0x43, 0x2f, 0xf4, 0x0a, 0x5c };
	const u8 aad[] = { 0xc7, 0x83, 0xa0, 0xcc, 0xa1, 0x0a, 0x8d, 0x9f,
			   0xb8, 0xd2, 0x7d, 0x69, 0x65, 0x94, 0x63, 0xf2 };
	const u8 ct[] = { 0x8a, 0x02, 0x3b, 0xa4, 0x77, 0xf5, 0xb8, 0x09,
			  0xbd, 0xdc, 0xda, 0x8f, 0x55, 0xe0, 0x90, 0x64,
			  0xd6, 0xd8, 0x8a, 0xae, 0xc9, 0x9c, 0x1e, 0x14,
			  0x12, 0x12, 0xea, 0x5b, 0x08, 0x50, 0x36, 0x60 };
	u8 tag[] = { 0x56, 0x2f, 0x50, 0x0d, 0xae, 0x63, 0x5d, 0x60,
		     0xa7, 0x69, 0xb4, 0x66, 0xe1, 0x5a, 0xcd, 0x1e };
	const u8 expected_msg[] = { 0x32, 0xf5, 0x1e, 0x83, 0x7a, 0x97, 0x48,
				    0x83, 0x89, 0x25, 0x06, 0x6d, 0x69, 0xe8,
				    0x71, 0x80, 0xf3, 0x4a, 0x64, 0x37, 0xe6,
				    0xb3, 0x96, 0xe5, 0x64, 0x3b, 0x34, 0xcb,
				    0x2e, 0xe4, 0xf7, 0xb1 };
	struct gpca_key_policy kp = { GPCA_KEY_CLASS_PORTABLE,
				      GPCA_ALGO_AES128_GCM,
				      GPCA_SUPPORTED_PURPOSE_DECRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_DISABLE, /* non-swappable */
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };
	struct gpca_key_handle *key_handle = NULL;
	u32 output_size = 0;
	u8 msg[32] = { 0 };
	u8 too_big_aad[64] = { 0 };
	u8 too_big_input_data[32] = { 0 };

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_import(gpca_ctx, &key_handle, &kp, key,
					ARRAY_SIZE(key)));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_set_crypto_symm_algo_params(gpca_ctx, opslot, key_handle->keyslot,
							     GPCA_CMD_CRYPTO_ALGO_AES128_GCM,
							     GPCA_CMD_CRYPTO_OP_DECRYPT, iv,
							     ARRAY_SIZE(iv)));
	/* Error case: Too big AAD */
	KUNIT_EXPECT_NE(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, NULL, 0, too_big_aad,
						      ARRAY_SIZE(too_big_aad), NULL, 0,
						      &output_size,
						      false));
	/* Error case: Too big Input data */
	KUNIT_EXPECT_NE(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, too_big_input_data,
						      ARRAY_SIZE(too_big_input_data), NULL, 0, NULL,
						      0, &output_size, false));
	/* Error case: invalid buffer sizes with NULL buffers */
	KUNIT_EXPECT_NE(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, NULL, 0,
						      NULL, 1, NULL, 0,
						      &output_size, false));
	KUNIT_EXPECT_NE(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, NULL, ARRAY_SIZE(ct), NULL,
						      0, NULL, 0, &output_size, false));
	KUNIT_EXPECT_NE(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, NULL, 0, NULL, 0, NULL,
						      ARRAY_SIZE(msg), &output_size, false));

	/* Non-zero AAD and zero input data */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, NULL, 0, aad,
						      ARRAY_SIZE(aad), NULL, 0, &output_size,
						      false));
	KUNIT_EXPECT_EQ(test, 0, output_size);

	/* Zero AAD and non-zero input data first 16 bytes */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, ct, 16, NULL, 0, msg,
						      ARRAY_SIZE(msg), &output_size, false));
	KUNIT_EXPECT_EQ(test, 16, output_size);

	/* Zero AAD and zero input data */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, NULL, 0, NULL, 0, msg + 16,
						      ARRAY_SIZE(msg) - 16, &output_size, false));
	KUNIT_EXPECT_EQ(test, 0, output_size);

	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, ct + 16,
						      ARRAY_SIZE(ct) - 16, NULL, 0,
						      msg + 16, ARRAY_SIZE(msg) - 16,
						      &output_size, false));
	KUNIT_EXPECT_EQ(test, ARRAY_SIZE(ct) - 16, output_size);

	/* LAST = 1 */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, opslot, tag, ARRAY_SIZE(tag),
						      NULL, 0, NULL, 0, &output_size, true));
	KUNIT_EXPECT_EQ(test, 0, output_size);

	/* Compare msg with expected_msg*/
	for (j = 0; j < ARRAY_SIZE(expected_msg); j++)
		KUNIT_EXPECT_EQ_MSG(test, expected_msg[j], msg[j],
				    "Decrypted message didn't match at index = %d expected = %d actual = %d",
				    j, expected_msg[j], msg[j]);

	KUNIT_EXPECT_EQ(test, 0, gpca_cmd_clear_opslot(gpca_ctx, opslot));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
}

void gpca_crypto_get_set_context(struct kunit *test)
{
	u8 first_opslot = 3;
	u8 second_opslot = 6;
	dma_addr_t ctx_dma_addr;
	u8 *ctx_buf = NULL;
	u32 ctx_buf_size = GPCA_CMD_CRYPTO_OP_CTX_SIZE;
	u8 msg_buf[32] = { 0 };
	u8 ct_buf[32] = { 0 };
	u32 output_size = 0;
	struct gpca_key_handle *key_handle = NULL;
	struct gpca_key_policy kp = { GPCA_KEY_CLASS_PORTABLE,
				      GPCA_ALGO_AES128_ECB,
				      GPCA_SUPPORTED_PURPOSE_ENCRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_DISABLE, /* non-swappable */
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };

	ctx_buf = kmalloc(ctx_buf_size, GFP_KERNEL);
	ctx_dma_addr = dma_map_single(gpca_ctx->dev, ctx_buf, ctx_buf_size,
				      DMA_FROM_DEVICE);
	/* Error case: Get context on empty slot */
	KUNIT_EXPECT_NE(test, 0,
			gpca_cmd_get_op_context(gpca_ctx, first_opslot,
						ctx_dma_addr, ctx_buf_size));
	/* Generate key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_generate(gpca_ctx, &key_handle, &kp));
	/* Set crypto params */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_set_crypto_symm_algo_params(gpca_ctx, first_opslot,
							     key_handle->keyslot,
							     GPCA_CMD_CRYPTO_ALGO_AES128_ECB,
							     GPCA_CMD_CRYPTO_OP_ENCRYPT, NULL, 0));
	/* Get context */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_get_op_context(gpca_ctx, first_opslot,
						ctx_dma_addr, ctx_buf_size));
	dma_unmap_single(gpca_ctx->dev, ctx_dma_addr, ctx_buf_size,
			 DMA_FROM_DEVICE);
	/* Clear opslot */
	KUNIT_EXPECT_EQ(test, 0, gpca_cmd_clear_opslot(gpca_ctx, first_opslot));
	/* Set in same opslot */
	ctx_dma_addr = dma_map_single(gpca_ctx->dev, ctx_buf, ctx_buf_size,
				      DMA_TO_DEVICE);
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_set_op_context(gpca_ctx, first_opslot,
						ctx_dma_addr, ctx_buf_size));
	dma_unmap_single(gpca_ctx->dev, ctx_dma_addr, ctx_buf_size,
			 DMA_TO_DEVICE);
	/* Start crypto op last = 0 */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, first_opslot, msg_buf, 16, NULL, 0,
						      ct_buf, ARRAY_SIZE(ct_buf), &output_size,
						      false));
	KUNIT_EXPECT_EQ(test, 16, output_size);
	/* Get context */
	ctx_dma_addr = dma_map_single(gpca_ctx->dev, ctx_buf, ctx_buf_size,
				      DMA_FROM_DEVICE);
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_get_op_context(gpca_ctx, first_opslot,
						ctx_dma_addr, ctx_buf_size));
	dma_unmap_single(gpca_ctx->dev, ctx_dma_addr, ctx_buf_size,
			 DMA_FROM_DEVICE);
	/* clear opslot */
	KUNIT_EXPECT_EQ(test, 0, gpca_cmd_clear_opslot(gpca_ctx, first_opslot));
	/* set context in second opslot */
	ctx_dma_addr = dma_map_single(gpca_ctx->dev, ctx_buf, ctx_buf_size,
				      DMA_TO_DEVICE);
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_set_op_context(gpca_ctx, second_opslot,
						ctx_dma_addr, ctx_buf_size));
	dma_unmap_single(gpca_ctx->dev, ctx_dma_addr, ctx_buf_size,
			 DMA_TO_DEVICE);
	/* Start crypto op last = 1 */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_start_crypto_op_data(gpca_ctx, second_opslot, msg_buf + 16, 16,
						      NULL, 0, ct_buf + 16, ARRAY_SIZE(ct_buf) - 16,
						      &output_size, true));
	KUNIT_EXPECT_EQ(test, 16, output_size);
	/* Get context */
	ctx_dma_addr = dma_map_single(gpca_ctx->dev, ctx_buf, ctx_buf_size,
				      DMA_FROM_DEVICE);
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_get_op_context(gpca_ctx, second_opslot,
						ctx_dma_addr, ctx_buf_size));
	dma_unmap_single(gpca_ctx->dev, ctx_dma_addr, ctx_buf_size,
			 DMA_FROM_DEVICE);
	/* Error case: Set context on occupied opslot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_set_crypto_hash_params(gpca_ctx, first_opslot,
							GPCA_CMD_CRYPTO_ALGO_SHA2_512));
	ctx_dma_addr = dma_map_single(gpca_ctx->dev, ctx_buf, ctx_buf_size,
				      DMA_TO_DEVICE);
	KUNIT_EXPECT_NE(test, 0,
			gpca_cmd_set_op_context(gpca_ctx, first_opslot,
						ctx_dma_addr, ctx_buf_size));
	dma_unmap_single(gpca_ctx->dev, ctx_dma_addr, ctx_buf_size,
			 DMA_TO_DEVICE);
	/* Clear opslot */
	KUNIT_EXPECT_EQ(test, 0, gpca_cmd_clear_opslot(gpca_ctx, first_opslot));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_cmd_clear_opslot(gpca_ctx, second_opslot));
	/* Clear key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	kfree(ctx_buf);
}

void gpca_crypto_op_slot_management(struct kunit *test)
{
	/**
	 * 8 operation slots are supported in GPCA.
	 * Set params for 5 SHA256 operations
	 * Set params for 5 SHA512 operations
	 */

	u32 i = 0, j = 0;
	struct gpca_crypto_op_handle *op_handles[10] = { 0 };
	dma_addr_t msg_dma_addr;
	dma_addr_t hash_dma_addr;
	u8 *msg_buf = NULL;
	u8 *hash_bufs[10] = { 0 };
	u32 offsets[10] = { 0 };
	u32 sha256_block_size = 64;
	u32 sha512_block_size = 128;
	u32 sha256_digest_size = 32;
	u32 sha512_digest_size = 64;
	u32 msg_size = 0;
	u32 completed_ops = 0;
	bool last_msg_chunk = false;
	u32 hash_size = 0;

	for (i = 0; i < 5; i++) {
		op_handles[i] = gpca_crypto_op_handle_alloc();
		KUNIT_EXPECT_PTR_NE(test, NULL, op_handles[i]);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_hash_params_sync(gpca_ctx,
								     op_handles[i],
								     GPCA_ALGO_SHA2_256),
			"Set crypto params failed for iteration = %d", i);
		hash_bufs[i] = kmalloc(sha256_digest_size, GFP_KERNEL);
	}
	for (i = 5; i < 10; i++) {
		op_handles[i] = gpca_crypto_op_handle_alloc();
		KUNIT_EXPECT_PTR_NE(test, NULL, op_handles[i]);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_hash_params_sync(gpca_ctx,
								     op_handles[i],
								     GPCA_ALGO_SHA2_512),
			"Set crypto params failed for iteration = %d", i);
		hash_bufs[i] = kmalloc(sha512_digest_size, GFP_KERNEL);
	}

	while (completed_ops < 10) {
		for (i = 0; i < 10; i++) {
			if (i < 5) {
				msg_size = ARRAY_SIZE(sha256_msg) - offsets[i] >
							   sha256_block_size ?
						   sha256_block_size :
						   ARRAY_SIZE(sha256_msg) -
							   offsets[i];
				if (msg_size == 0)
					continue;

				msg_buf = kmalloc(msg_size, GFP_KERNEL);
				for (j = 0; j < msg_size; j++)
					msg_buf[j] = sha256_msg[offsets[i] + j];
			} else {
				msg_size = ARRAY_SIZE(sha512_msg) - offsets[i] >
							   sha512_block_size ?
						   sha512_block_size :
						   ARRAY_SIZE(sha512_msg) -
							   offsets[i];
				if (msg_size == 0)
					continue;
				msg_buf = kmalloc(msg_size, GFP_KERNEL);
				for (j = 0; j < msg_size; j++)
					msg_buf[j] = sha512_msg[offsets[i] + j];
			}

			offsets[i] += msg_size;

			if ((i < 5 && offsets[i] == ARRAY_SIZE(sha256_msg)) ||
			    (i >= 5 && offsets[i] == ARRAY_SIZE(sha512_msg)))
				last_msg_chunk = true;
			else
				last_msg_chunk = false;

			if (last_msg_chunk) {
				if (i < 5)
					hash_size = sha256_digest_size;
				else
					hash_size = sha512_digest_size;
			} else {
				hash_size = 0;
			}

			/* DMA map */
			msg_dma_addr = dma_map_single(gpca_ctx->dev, msg_buf,
						      msg_size, DMA_TO_DEVICE);
			if (last_msg_chunk)
				hash_dma_addr = dma_map_single(gpca_ctx->dev,
							       hash_bufs[i],
							       hash_size,
							       DMA_FROM_DEVICE);

			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_start_digest_op_sync(gpca_ctx,
									     op_handles[i],
									     msg_dma_addr,
									     msg_size,
									     hash_dma_addr,
									     hash_size,
									     last_msg_chunk),
				"Start crypto op(ptrs) failed for iteration = %d",
				i);

			/* DMA Unmap */
			dma_unmap_single(gpca_ctx->dev, msg_dma_addr, msg_size,
					 DMA_TO_DEVICE);
			if (last_msg_chunk)
				dma_unmap_single(gpca_ctx->dev, hash_dma_addr,
						 hash_size, DMA_FROM_DEVICE);

			if (last_msg_chunk)
				completed_ops++;

			kfree(msg_buf);
		}
	}

	for (i = 0; i < 10; i++) {
		/* Clear op slot */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_clear_op_sync(gpca_ctx, op_handles[i]),
				    "Clear op slot failed for iteration = %d",
				    i);
		gpca_crypto_op_handle_free(op_handles[i]);
	}

	for (i = 0; i < 5; i++) {
		/* Compare hash */
		for (j = 0; j < sha256_digest_size; j++)
			KUNIT_EXPECT_EQ_MSG(test, hash_bufs[i][j], sha256_hash[j],
					    "Hash didn't match for iteration %d at index %d, expected = 0x%x actual = 0x%x",
					    i, j, hash_bufs[i][j], sha256_hash[j]);
		kfree(hash_bufs[i]);
	}
	for (i = 5; i < 10; i++) {
		/* Compare hash */
		for (j = 0; j < sha512_digest_size; j++)
			KUNIT_EXPECT_EQ_MSG(test, hash_bufs[i][j], sha512_hash[j],
					    "Hash didn't match for iteration %d at index %d, expected = 0x%x actual = 0x%x",
					    i, j, hash_bufs[i][j], sha512_hash[j]);
		kfree(hash_bufs[i]);
	}
}

struct gpca_update_cb_ctx {
	struct gpca_dev *gpca_dev;
	u8 *msg_buf;
	u32 msg_buf_len;
	dma_addr_t msg_dma_addr;
	u8 *hash_buf;
	dma_addr_t hash_dma_addr;
};

static void gpca_update_cb(int ret, void *cb_ctx)
{
	struct gpca_update_cb_ctx *ctx = (struct gpca_update_cb_ctx *)cb_ctx;

	if (ctx) {
		dma_unmap_single(ctx->gpca_dev->dev, ctx->msg_dma_addr,
				 ctx->msg_buf_len, DMA_TO_DEVICE);
		kfree(ctx->msg_buf);
		kfree(ctx);
	} else {
		pr_err("GPCA callback context is NULL.");
	}
}

#define NUM_SHA_OPS 1000
void gpca_crypto_async_sha_update(struct kunit *test)
{
	u32 i = 0, j = 0;
	struct gpca_crypto_op_handle *op_handles[NUM_SHA_OPS] = { 0 };
	u32 offset = 0;
	u32 block_size = 128;
	u32 digest_size = 64;
	u32 msg_size = ARRAY_SIZE(sha512_msg);
	bool last_msg_chunk;
	struct gpca_update_cb_ctx *ctx = NULL;
	u32 msg_len_itr = 0;

	for (i = 0; i < NUM_SHA_OPS; i++) {
		op_handles[i] = gpca_crypto_op_handle_alloc();
		KUNIT_EXPECT_PTR_NE(test, NULL, op_handles[i]);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_hash_params_sync(gpca_ctx,
								     op_handles[i],
								     GPCA_ALGO_SHA2_512),
				    "Set crypto params failed for iteration = %d", i);
	}

	while (offset < msg_size) {
		for (i = 0; i < NUM_SHA_OPS; i++) {
			ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
			ctx->gpca_dev = gpca_ctx;
			ctx->msg_buf_len = min(msg_size - offset, block_size);
			ctx->msg_buf = kmalloc(ctx->msg_buf_len, GFP_KERNEL);
			for (j = 0; j < ctx->msg_buf_len; j++)
				ctx->msg_buf[j] = sha512_msg[offset + j];

			msg_len_itr = ctx->msg_buf_len;
			if (offset + ctx->msg_buf_len == msg_size)
				last_msg_chunk = true;
			else
				last_msg_chunk = false;

			/* DMA map */
			ctx->msg_dma_addr = dma_map_single(gpca_ctx->dev, ctx->msg_buf,
							   ctx->msg_buf_len, DMA_TO_DEVICE);

			if (!last_msg_chunk) {
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_start_digest_op(gpca_ctx,
										op_handles[i],
										ctx->msg_dma_addr,
										ctx->msg_buf_len,
										0, 0, false,
										gpca_update_cb,
										ctx),
						    "Start crypto op(ptrs) failed for iteration = %d",
						    i);
				continue;
			}

			/* Last message chunk */
			ctx->hash_buf = kmalloc(digest_size, GFP_KERNEL);
			ctx->hash_dma_addr = dma_map_single(gpca_ctx->dev,
							    ctx->hash_buf,
							    digest_size,
							    DMA_FROM_DEVICE);
			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_start_digest_op_sync(gpca_ctx,
									     op_handles[i],
									     ctx->msg_dma_addr,
									     ctx->msg_buf_len,
									     ctx->hash_dma_addr,
									     digest_size,
									     true),
					    "Start crypto op(ptrs) failed for iteration = %d",
					    i);

			/* DMA Unmap */
			dma_unmap_single(gpca_ctx->dev, ctx->msg_dma_addr, ctx->msg_buf_len,
					 DMA_TO_DEVICE);
			dma_unmap_single(gpca_ctx->dev, ctx->hash_dma_addr,
					 digest_size, DMA_FROM_DEVICE);
			/* Compare hash */
			for (j = 0; j < digest_size; j++) {
				KUNIT_EXPECT_EQ_MSG(test, ctx->hash_buf[j], sha512_hash[j],
						    "Hash didn't match for iteration %d at index %d, expected = 0x%x actual = 0x%x",
						    i, j, ctx->hash_buf[j], sha512_hash[j]);
				break;
			}

			kfree(ctx->hash_buf);
			kfree(ctx->msg_buf);
			kfree(ctx);
		}
		offset += msg_len_itr;
	}

	for (i = 0; i < NUM_SHA_OPS; i++) {
		/* Clear op slot */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_clear_op_sync(gpca_ctx, op_handles[i]),
				    "Clear op slot failed for iteration = %d",
				    i);
		gpca_crypto_op_handle_free(op_handles[i]);
	}
}

struct concurrent_sha_test_ctx {
	struct kunit *test;
	struct workqueue_struct *wq;
	struct work_struct sha_op;
};

static void sha_op_process(struct work_struct *work)
{
	u32 j = 0;
	struct gpca_crypto_op_handle *op_handle = NULL;
	u32 offset = 0;
	u32 block_size = 128;
	u32 digest_size = 64;
	u32 msg_size = ARRAY_SIZE(sha512_msg);
	bool last_msg_chunk;
	struct gpca_update_cb_ctx *ctx = NULL;
	u32 msg_len_itr = 0;
	struct concurrent_sha_test_ctx *test_ctx = NULL;
	struct kunit *test = NULL;

	test_ctx = container_of(work, struct concurrent_sha_test_ctx, sha_op);
	test = test_ctx->test;

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);
	KUNIT_EXPECT_EQ_MSG(test, 0,
			    gpca_crypto_set_hash_params_sync(gpca_ctx,
							     op_handle,
							     GPCA_ALGO_SHA2_512),
			    "Set crypto params failed");

	while (offset < msg_size) {
		ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
		ctx->gpca_dev = gpca_ctx;
		ctx->msg_buf_len = min(msg_size - offset, block_size);
		ctx->msg_buf = kmalloc(ctx->msg_buf_len, GFP_KERNEL);
		for (j = 0; j < ctx->msg_buf_len; j++)
			ctx->msg_buf[j] = sha512_msg[offset + j];

		msg_len_itr = ctx->msg_buf_len;
		if (offset + ctx->msg_buf_len == msg_size)
			last_msg_chunk = true;
		else
			last_msg_chunk = false;

		/* DMA map */
		ctx->msg_dma_addr = dma_map_single(gpca_ctx->dev, ctx->msg_buf,
						   ctx->msg_buf_len, DMA_TO_DEVICE);

		if (!last_msg_chunk) {
			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_start_digest_op(gpca_ctx,
									op_handle,
									ctx->msg_dma_addr,
									ctx->msg_buf_len,
									0, 0, false,
									gpca_update_cb,
									ctx),
					    "Start crypto op(ptrs) failed");
			offset += msg_len_itr;
			continue;
		}

		/* Last message chunk */
		ctx->hash_buf = kmalloc(digest_size, GFP_KERNEL);
		ctx->hash_dma_addr = dma_map_single(gpca_ctx->dev,
						    ctx->hash_buf,
						    digest_size,
						    DMA_FROM_DEVICE);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_start_digest_op_sync(gpca_ctx,
								     op_handle,
								     ctx->msg_dma_addr,
								     ctx->msg_buf_len,
								     ctx->hash_dma_addr,
								     digest_size,
								     true),
				    "Start crypto op(ptrs) failed");

		/* DMA Unmap */
		dma_unmap_single(gpca_ctx->dev, ctx->msg_dma_addr, ctx->msg_buf_len,
				 DMA_TO_DEVICE);
		dma_unmap_single(gpca_ctx->dev, ctx->hash_dma_addr,
				 digest_size, DMA_FROM_DEVICE);
		/* Compare hash */
		for (j = 0; j < digest_size; j++) {
			KUNIT_EXPECT_EQ_MSG(test, ctx->hash_buf[j], sha512_hash[j],
					    "Hash didn't match at index %d, expected = 0x%x actual = 0x%x",
					    j, ctx->hash_buf[j], sha512_hash[j]);
			break;
		}

		kfree(ctx->hash_buf);
		kfree(ctx->msg_buf);
		kfree(ctx);
		offset += msg_len_itr;
	}

	/* Clear op slot */
	KUNIT_EXPECT_EQ_MSG(test, 0,
			    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
			    "Clear op slot failed");
	gpca_crypto_op_handle_free(op_handle);
}

void gpca_crypto_concurrent_sha(struct kunit *test)
{
	struct concurrent_sha_test_ctx *test_ctx = NULL;
	struct workqueue_struct *wq = NULL;
	u32 i = 0;

	test_ctx = kmalloc_array(NUM_SHA_OPS, sizeof(*test_ctx), GFP_KERNEL);
	wq = alloc_workqueue("gpca_op_wq", 0, 0);
	KUNIT_EXPECT_PTR_NE(test, NULL, wq);

	for (i = 0; i < NUM_SHA_OPS; i++) {
		test_ctx[i].wq = wq;
		test_ctx[i].test = test;
		INIT_WORK(&test_ctx[i].sha_op, sha_op_process);
		queue_work(wq, &test_ctx[i].sha_op);
	}
	drain_workqueue(wq);

	kfree(test_ctx);
}

static struct kunit_case gpca_crypto_test_cases[] = {
	KUNIT_CASE(gpca_crypto_aes),
	KUNIT_CASE(gpca_crypto_tdes),
	KUNIT_CASE(gpca_crypto_aes_gcm),
	KUNIT_CASE(gpca_crypto_sha),
	KUNIT_CASE(gpca_crypto_hmac),
	KUNIT_CASE(gpca_crypto_ecc),
	KUNIT_CASE(gpca_crypto_aes_gcm_known_answer),
	KUNIT_CASE(gpca_crypto_start_crypto_op_data),
	KUNIT_CASE(gpca_crypto_get_set_context),
	KUNIT_CASE(gpca_crypto_op_slot_management),
	KUNIT_CASE(gpca_crypto_async_sha_update),
	KUNIT_CASE(gpca_crypto_concurrent_sha),
	KUNIT_CASE(gpca_crypto_lkc_ecc),
	KUNIT_CASE(gpca_crypto_aes_tdes_streaming),
	KUNIT_CASE(gpca_crypto_sha_hmac_streaming),
	KUNIT_CASE(gpca_crypto_aes_gcm_streaming),
	{}
};

int gpca_suite_init(struct kunit_suite *suite)
{
	struct platform_device *gpca_platform_device =
		get_gpca_platform_device();
	gpca_ctx = gpca_key_get_device_context(gpca_platform_device);
	if (!gpca_ctx)
		return -1;
	return 0;
}

static struct kunit_suite gpca_crypto_test_suite = {
	.name = "gpca_crypto",
	.test_cases = gpca_crypto_test_cases,
	.suite_init = gpca_suite_init,
};

kunit_test_suite(gpca_crypto_test_suite);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GPCA Crypto KUnit test");
MODULE_LICENSE("GPL");
