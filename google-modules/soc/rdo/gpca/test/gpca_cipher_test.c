// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Crypto cipher unit tests.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/types.h>

#include "gpca_crypto_test.h"
#include "gpca_internal.h"
#include "gpca_keys.h"
#include "gpca_test_common.h"

extern struct gpca_dev *gpca_ctx;

struct gpca_symm_crypto_config {
	u32 input_data_size;
	u32 unencrypted_size;
	u32 aad_size;
	bool is_passing_test_case;
};

static struct gpca_symm_crypto_config symm_test_configs[] = {
	{
		/* Small buffer */
		.input_data_size = 16,
		.unencrypted_size = 0,
		.aad_size = 0,
		.is_passing_test_case = true,
	},
	{
		/* 100 KB buffer size */
		.input_data_size = 100 * 1024,
		.unencrypted_size = 0,
		.aad_size = 0,
		.is_passing_test_case = true,
	},
	{
		/**
		 * Max kmalloc size  = 4MB
		 * input_data_size = 4MB - tag_size(16B)
		 */
		.input_data_size = (4 * 1024 * 1024) - 16,
		.unencrypted_size = 0,
		.aad_size = 0,
		.is_passing_test_case = true,
	},
	/**
	 * Max input data size test.
	 *
	 * Doesn't work since max kmalloc size is 4MB.
	 * {
	 *      .input_data_size = (8 * 1024 * 1024) - 1,
	 *      .unencrypted_size = 0,
	 *      .aad_size = 0,
	 *      .is_passing_test_case = true,
	 * },
	 */
	{
		/* Zero size data */
		.input_data_size = 0,
		.unencrypted_size = 0,
		.aad_size = 0,
		.is_passing_test_case = true,
	},
	{
		/* Unencrypted data */
		.input_data_size = 0,
		.unencrypted_size = 32,
		.aad_size = 0,
		.is_passing_test_case = true,
	},
	{
		/* Unencrypted max size */
		.input_data_size = 160,
		.unencrypted_size = (64 * 1024) - 1,
		.aad_size = 0,
		.is_passing_test_case = true,
	},
	{
		/* Small AAD */
		.input_data_size = 16,
		.unencrypted_size = 0,
		.aad_size = 16,
		.is_passing_test_case = true,
	},
	{
		/* AAD max size */
		.input_data_size = 1600,
		.unencrypted_size = 100,
		.aad_size = (64 * 1024) - 1,
		.is_passing_test_case = true,
	},
	/**
	 * Too large input data size.
	 *
	 * Doesn't work since max kmalloc size is 4MB.
	 * {
	 *      .input_data_size = (16 * 1024 * 1024) - 1,
	 *      .unencrypted_size = 0,
	 *      .aad_size = 0,
	 *      .is_passing_test_case = false,
	 * },
	 */
	{
		/* Too large unencrypted size */
		.input_data_size = 16,
		.unencrypted_size = (65 * 1024),
		.aad_size = 0,
		.is_passing_test_case = false,
	},
};

#define ENCRYPT_OP GPCA_SUPPORTED_PURPOSE_ENCRYPT
#define DECRYPT_OP GPCA_SUPPORTED_PURPOSE_DECRYPT

void gpca_crypto_aes(struct kunit *test)
{
	u32 i = 0, j = 0;
	u32 cfg = 0;
	struct gpca_key_handle *key_handle;
	enum gpca_algorithm algo[] = {
		GPCA_ALGO_AES128_CBC, GPCA_ALGO_AES128_CTR,
		GPCA_ALGO_AES128_ECB, GPCA_ALGO_AES192_CBC,
		GPCA_ALGO_AES192_CTR, GPCA_ALGO_AES192_ECB,
		GPCA_ALGO_AES256_CBC, GPCA_ALGO_AES256_CTR,
		GPCA_ALGO_AES256_ECB,
	};
	u8 *plaintext = NULL;
	u8 *ciphertext = NULL;
	u8 *decrypted_plaintext = NULL;
	u32 plaintext_size = 0;
	dma_addr_t pt_dma_addr;
	dma_addr_t ct_dma_addr;
	dma_addr_t decrypted_pt_dma_addr;
	u8 iv[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	u32 iv_sz = 0;
	u8 *iv_ptr = NULL;
	struct gpca_crypto_op_handle *op_handle = NULL;
	int ret = 0;

	struct gpca_key_policy kp = { GPCA_KEY_CLASS_HARDWARE,
				      GPCA_ALGO_AES256_ECB,
				      GPCA_SUPPORTED_PURPOSE_ENCRYPT |
					      GPCA_SUPPORTED_PURPOSE_DECRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_ENABLE,
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);

	/* Encrypt, Decrypt match */
	for (i = 0; i < ARRAY_SIZE(algo); i++) {
		/* Generate key */
		kp.algo = algo[i];

		KUNIT_EXPECT_EQ_MSG(test, 0, gpca_key_generate(gpca_ctx, &key_handle, &kp),
				    "Key generate failed for iteration = %d", i);

		for (cfg = 0; cfg < ARRAY_SIZE(symm_test_configs);
		     cfg++) {
			plaintext_size =
				symm_test_configs[cfg].unencrypted_size +
				symm_test_configs[cfg].input_data_size;
			plaintext = kmalloc(plaintext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, plaintext);

			for (j = 0; j < plaintext_size; j++)
				plaintext[j] = (j + 20) % 256;

			ciphertext = kmalloc(plaintext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, ciphertext);

			decrypted_plaintext =
				kmalloc(plaintext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, decrypted_plaintext);

			if (algo[i] == GPCA_ALGO_AES128_ECB ||
			    algo[i] == GPCA_ALGO_AES192_ECB ||
			    algo[i] == GPCA_ALGO_AES256_ECB) {
				iv_ptr = NULL;
				iv_sz = 0;
			} else {
				iv_ptr = iv;
				iv_sz = ARRAY_SIZE(iv);
			}
			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
										  op_handle,
										  key_handle,
										  algo[i],
										  ENCRYPT_OP,
										  iv_ptr, iv_sz),
					    "Set crypto params encrypt failed for iteration = %d",
					    i);

			if (plaintext_size) {
				/* DMA Map */
				pt_dma_addr = dma_map_single(gpca_ctx->dev,
							     plaintext,
							     plaintext_size,
							     DMA_TO_DEVICE);
				ct_dma_addr = dma_map_single(gpca_ctx->dev,
							     ciphertext,
							     plaintext_size,
							     DMA_FROM_DEVICE);
			}
			if (!symm_test_configs[cfg].is_passing_test_case) {
				ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									  op_handle,
									  pt_dma_addr,
									  symm_test_configs[cfg]
										.input_data_size,
									  0,
									  symm_test_configs[cfg]
										.unencrypted_size,
									  ct_dma_addr,
									  plaintext_size,
									  true);
				KUNIT_EXPECT_NE_MSG(test, 0, ret,
						    "Start crypto op(ptrs) encrypt passed for iteration = %d, exptected failure",
						    i);
				/* Clear op slot */
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
						    "Clear op slot failed for iteration = %d",
						    i);
				kfree(plaintext);
				kfree(ciphertext);
				kfree(decrypted_plaintext);
				continue;
			}
			/* Passing case */
			/* Encrypt */
			ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
								  op_handle,
								  pt_dma_addr,
								  symm_test_configs[cfg]
									.input_data_size,
								  0,
								  symm_test_configs[cfg]
									.unencrypted_size,
								  ct_dma_addr,
								  plaintext_size,
								  true);
			KUNIT_EXPECT_EQ_MSG(test, 0, ret,
					    "Start crypto op(ptrs) encrypt failed for iteration = %d",
					    i);

			if (plaintext_size) {
				dma_unmap_single(gpca_ctx->dev,
						 pt_dma_addr,
						 plaintext_size,
						 DMA_TO_DEVICE);
				dma_unmap_single(gpca_ctx->dev,
						 ct_dma_addr,
						 plaintext_size,
						 DMA_FROM_DEVICE);
			}

			/* Check unencrypted data in ciphertext */
			for (j = 0; j < symm_test_configs[cfg].unencrypted_size; j++)
				KUNIT_EXPECT_EQ_MSG(test, plaintext[j],
						    ciphertext[j],
						    "Unencrypted data in ciphertext didn't match at index = %d expected = %d actual = %d",
						    j, plaintext[j], ciphertext[j]);

			/* Clear op slot */
			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
					    "Clear op slot failed for iteration = %d",
					    i);

			if (algo[i] == GPCA_ALGO_AES128_ECB ||
			    algo[i] == GPCA_ALGO_AES192_ECB ||
			    algo[i] == GPCA_ALGO_AES256_ECB) {
				iv_ptr = NULL;
				iv_sz = 0;
			} else {
				iv_ptr = iv;
				iv_sz = ARRAY_SIZE(iv);
			}
			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
										  op_handle,
										  key_handle,
										  algo[i],
										  DECRYPT_OP,
										  iv_ptr, iv_sz),
					    "Set crypto params decrypt failed for iteration = %d",
					    i);
			/* Decrypt */
			if (plaintext_size) {
				ct_dma_addr = dma_map_single(gpca_ctx->dev, ciphertext,
							     plaintext_size, DMA_TO_DEVICE);
				decrypted_pt_dma_addr = dma_map_single(gpca_ctx->dev,
								       decrypted_plaintext,
								       plaintext_size,
								       DMA_FROM_DEVICE);
			}

			ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
								  op_handle,
								  ct_dma_addr,
								  symm_test_configs[cfg]
									.input_data_size,
								  0,
								  symm_test_configs[cfg]
									.unencrypted_size,
								  decrypted_pt_dma_addr,
								  plaintext_size,
								  true);
			KUNIT_EXPECT_EQ_MSG(test, 0, ret,
					    "Start crypto op(ptrs) decrypt failed for iteration = %d",
					    i);

			/* DMA Unmap */
			if (plaintext_size) {
				dma_unmap_single(gpca_ctx->dev,
						 ct_dma_addr,
						 plaintext_size,
						 DMA_TO_DEVICE);
				dma_unmap_single(gpca_ctx->dev,
						 decrypted_pt_dma_addr,
						 plaintext_size,
						 DMA_FROM_DEVICE);
			}

			/* Compare Plaintext and decrypted plaintext */
			for (j = 0; j < plaintext_size; j++)
				KUNIT_EXPECT_EQ_MSG(test, plaintext[j],
						    decrypted_plaintext[j],
						    "Decrypted plain text didn't match at index = %d expected = %d actual = %d",
						    j, plaintext[j],
						    decrypted_plaintext[j]);

			/* Clear op slot */
			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
					    "Clear op slot failed for iteration = %d",
					    i);
			kfree(plaintext);
			kfree(ciphertext);
			kfree(decrypted_plaintext);
		}
		/* Clear key */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_clear(gpca_ctx, &key_handle),
				    "Clear key failed for iteration = %d", i);
	}
	gpca_crypto_op_handle_free(op_handle);
}

void gpca_crypto_tdes(struct kunit *test)
{
	u32 i = 0, j = 0;
	u32 cfg = 0;
	struct gpca_key_handle *key_handle;
	struct gpca_crypto_op_handle *op_handle = NULL;
	enum gpca_algorithm algo[] = { GPCA_ALGO_TDES_ECB, GPCA_ALGO_TDES_CBC };
	u8 *plaintext = NULL;
	u8 *ciphertext = NULL;
	u8 *decrypted_plaintext = NULL;
	u32 plaintext_size = 0;
	dma_addr_t pt_dma_addr;
	dma_addr_t ct_dma_addr;
	dma_addr_t decrypted_pt_dma_addr;
	u8 iv[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
	u8 *iv_ptr = NULL;
	u32 iv_sz = 0;
	int ret = 0;
	struct gpca_key_policy kp = { GPCA_KEY_CLASS_HARDWARE,
				      GPCA_ALGO_TDES_ECB,
				      GPCA_SUPPORTED_PURPOSE_ENCRYPT |
					      GPCA_SUPPORTED_PURPOSE_DECRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_ENABLE,
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);

	/* Encrypt, Decrypt match */
	for (i = 0; i < ARRAY_SIZE(algo); i++) {
		/* Generate key */
		kp.algo = algo[i];

		KUNIT_EXPECT_EQ_MSG(test, 0, gpca_key_generate(gpca_ctx, &key_handle, &kp),
				    "Key generate failed for iteration = %d", i);

		for (cfg = 0; cfg < ARRAY_SIZE(symm_test_configs);
		     cfg++) {
			plaintext_size =
				symm_test_configs[cfg].unencrypted_size +
				symm_test_configs[cfg].input_data_size;
			plaintext = kmalloc(plaintext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, plaintext);

			for (j = 0; j < plaintext_size; j++)
				plaintext[j] = (j + 10) % 256;

			ciphertext = kmalloc(plaintext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, ciphertext);

			decrypted_plaintext =
				kmalloc(plaintext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, decrypted_plaintext);

			if (algo[i] == GPCA_ALGO_TDES_ECB) {
				iv_ptr = NULL;
				iv_sz = 0;
			} else {
				iv_ptr = iv;
				iv_sz = ARRAY_SIZE(iv);
			}
			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
										  op_handle,
										  key_handle,
										  algo[i],
										  ENCRYPT_OP,
										  iv_ptr,
										  iv_sz),
					    "Set crypto params encrypt failed for iteration = %d",
					    i);

			/* DMA Map */
			if (plaintext_size) {
				pt_dma_addr = dma_map_single(gpca_ctx->dev,
							     plaintext,
							     plaintext_size,
							     DMA_TO_DEVICE);
				ct_dma_addr = dma_map_single(gpca_ctx->dev,
							     ciphertext,
							     plaintext_size,
							     DMA_FROM_DEVICE);
			}

			if (symm_test_configs[cfg].is_passing_test_case) {
				/* Encrypt */
				ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									  op_handle,
									  pt_dma_addr,
									  symm_test_configs[cfg]
										.input_data_size,
									  0,
									  symm_test_configs[cfg]
										.unencrypted_size,
									  ct_dma_addr,
									  plaintext_size,
									  true);
				KUNIT_EXPECT_EQ_MSG(test, 0, ret,
						    "Start crypto op(ptrs) encrypt failed for iteration = %d",
						    i);

				if (plaintext_size) {
					dma_unmap_single(gpca_ctx->dev,
							 pt_dma_addr,
							 plaintext_size,
							 DMA_TO_DEVICE);
					dma_unmap_single(gpca_ctx->dev,
							 ct_dma_addr,
							 plaintext_size,
							 DMA_FROM_DEVICE);
				}

				/* Check unencrypted data in ciphertext */
				for (j = 0; j < symm_test_configs[cfg].unencrypted_size; j++)
					KUNIT_EXPECT_EQ_MSG(test, plaintext[j],
							    ciphertext[j],
							    "Unencrypted data in ciphertext didn't match at index = %d expected = %d actual = %d",
							    j, plaintext[j], ciphertext[j]);

				/* Clear op slot */
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
						    "Clear op slot failed for iteration = %d",
						    i);

				if (algo[i] == GPCA_ALGO_TDES_ECB) {
					iv_ptr = NULL;
					iv_sz = 0;
				} else {
					iv_ptr = iv;
					iv_sz = ARRAY_SIZE(iv);
				}
				ret = gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
									    op_handle,
									    key_handle,
									    algo[i],
									    DECRYPT_OP,
									    iv_ptr,
									    iv_sz);
				KUNIT_EXPECT_EQ_MSG(test, 0, ret,
						    "Set crypto params decrypt failed for iteration = %d",
						    i);

				/* Decrypt */
				if (plaintext_size) {
					ct_dma_addr = dma_map_single(gpca_ctx->dev, ciphertext,
								     plaintext_size, DMA_TO_DEVICE);
					decrypted_pt_dma_addr = dma_map_single(gpca_ctx->dev,
									       decrypted_plaintext,
									       plaintext_size,
									       DMA_FROM_DEVICE);
				}

				ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									  op_handle,
									  ct_dma_addr,
									  symm_test_configs[cfg]
										.input_data_size,
									  0,
									  symm_test_configs[cfg]
										.unencrypted_size,
									  decrypted_pt_dma_addr,
									  plaintext_size,
									  true);
				KUNIT_EXPECT_EQ_MSG(test, 0, ret,
						    "Start crypto op(ptrs) decrypt failed for iteration = %d",
						    i);
				/* DMA Unmap */
				if (plaintext_size) {
					dma_unmap_single(gpca_ctx->dev,
							 ct_dma_addr,
							 plaintext_size,
							 DMA_TO_DEVICE);
					dma_unmap_single(gpca_ctx->dev,
							 decrypted_pt_dma_addr,
							 plaintext_size,
							 DMA_FROM_DEVICE);
				}

				/* Compare Plaintext and decrypted plaintext */
				for (j = 0; j < plaintext_size; j++)
					KUNIT_EXPECT_EQ_MSG(test, plaintext[j],
							    decrypted_plaintext[j],
							    "Decrypted plain text didn't match at index = %d expected = %d actual = %d",
							    j, plaintext[j],
							    decrypted_plaintext[j]);

				/* Clear op slot */
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
						    "Clear op slot failed for iteration = %d",
						    i);
			} else {
				ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									  op_handle,
									  pt_dma_addr,
									  symm_test_configs[cfg]
										.input_data_size,
									  0,
									  symm_test_configs[cfg]
										.unencrypted_size,
									  ct_dma_addr,
									  plaintext_size,
									  true);
				KUNIT_EXPECT_NE_MSG(test, 0, ret,
						    "Start crypto op(ptrs) encrypt passed for iteration = %d, expected failure",
						    i);
				/* Clear op slot */
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
						    "Clear op slot failed for iteration = %d",
						    i);
			}
			kfree(plaintext);
			kfree(ciphertext);
			kfree(decrypted_plaintext);
		}
		/* Clear key */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_clear(gpca_ctx, &key_handle),
				    "Clear key failed for iteration = %d", i);
	}
	gpca_crypto_op_handle_free(op_handle);
}

void gpca_crypto_aes_gcm(struct kunit *test)
{
	u32 i = 0, j = 0;
	u32 cfg = 0;
	struct gpca_key_handle *key_handle;
	struct gpca_crypto_op_handle *op_handle = NULL;
	enum gpca_algorithm algo[] = { GPCA_ALGO_AES128_GCM,
				       GPCA_ALGO_AES192_GCM,
				       GPCA_ALGO_AES256_GCM };
	u8 *plaintext = NULL;
	u8 *ciphertext = NULL;
	u8 *decrypted_plaintext = NULL;
	u32 plaintext_size = 0;
	u32 ciphertext_size = 0;
	u32 decrypted_pt_size = 0;
	u32 tag_size = 16;
	dma_addr_t pt_dma_addr;
	dma_addr_t ct_dma_addr;
	dma_addr_t decrypted_pt_dma_addr;
	u8 iv[] = { 0x92, 0x1d, 0x25, 0x07, 0xfa, 0x80,
		    0x07, 0xb7, 0xbd, 0x06, 0x7d, 0x34 };
	int ret = 0;

	struct gpca_key_policy kp = { GPCA_KEY_CLASS_HARDWARE,
				      GPCA_ALGO_AES128_GCM,
				      GPCA_SUPPORTED_PURPOSE_ENCRYPT |
					      GPCA_SUPPORTED_PURPOSE_DECRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_ENABLE,
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);

	/* Encrypt, Decrypt match */
	for (i = 0; i < ARRAY_SIZE(algo); i++) {
		/* Generate key */
		kp.algo = algo[i];

		KUNIT_EXPECT_EQ_MSG(test, 0, gpca_key_generate(gpca_ctx, &key_handle, &kp),
				    "Key generate failed for iteration = %d", i);

		for (cfg = 0; cfg < ARRAY_SIZE(symm_test_configs);
		     cfg++) {
			plaintext_size =
				symm_test_configs[cfg].unencrypted_size +
				symm_test_configs[cfg].aad_size +
				symm_test_configs[cfg].input_data_size;
			plaintext = kmalloc(plaintext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, plaintext);

			for (j = 0; j < plaintext_size; j++)
				plaintext[j] = (j + 30) % 256;

			/* Allocate extra space for AAD so that we can pass that for decryption */
			ciphertext_size =
				symm_test_configs[cfg].unencrypted_size +
				symm_test_configs[cfg].input_data_size +
				tag_size +
				symm_test_configs[cfg].aad_size;
			ciphertext = kmalloc(ciphertext_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, ciphertext);

			decrypted_pt_size =
				symm_test_configs[cfg].unencrypted_size +
				symm_test_configs[cfg].input_data_size;
			decrypted_plaintext =
				kmalloc(decrypted_pt_size, GFP_KERNEL);
			KUNIT_EXPECT_PTR_NE(test, NULL, decrypted_plaintext);

			KUNIT_EXPECT_EQ_MSG(test, 0,
					    gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
										  op_handle,
										  key_handle,
										  algo[i],
										  ENCRYPT_OP,
										  iv,
										  ARRAY_SIZE(iv)),
				"Set crypto params encrypt failed for iteration = %d",
				i);

			/* DMA Map */
			if (plaintext_size)
				pt_dma_addr = dma_map_single(gpca_ctx->dev,
							     plaintext,
							     plaintext_size,
							     DMA_TO_DEVICE);

			ct_dma_addr = dma_map_single(gpca_ctx->dev, ciphertext,
						     ciphertext_size,
						     DMA_FROM_DEVICE);

			if (symm_test_configs[cfg].is_passing_test_case) {
				/* Encrypt */
				ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									  op_handle,
									  pt_dma_addr,
									  symm_test_configs[cfg]
										.input_data_size,
									  symm_test_configs[cfg]
										.aad_size,
									  symm_test_configs[cfg]
										.unencrypted_size,
									  ct_dma_addr,
									  ciphertext_size -
									  symm_test_configs[cfg]
										.aad_size,
									  true);
				KUNIT_EXPECT_EQ_MSG(test, 0, ret,
						    "Start crypto op(ptrs) encrypt failed for iteration = %d",
						    i);

				if (plaintext_size)
					dma_unmap_single(gpca_ctx->dev,
							 pt_dma_addr,
							 plaintext_size,
							 DMA_TO_DEVICE);
				dma_unmap_single(gpca_ctx->dev, ct_dma_addr,
						 ciphertext_size,
						 DMA_FROM_DEVICE);

				/* Check unencrypted data in ciphertext */
				for (j = 0; j < symm_test_configs[cfg].unencrypted_size; j++)
					KUNIT_EXPECT_EQ_MSG(test, plaintext[j],
							    ciphertext[j],
							    "Unencrypted data in ciphertext didn't match at index = %d expected = %d actual = %d",
							    j, plaintext[j], ciphertext[j]);

				/* Clear op slot */
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
						    "Clear op slot failed for iteration = %d",
						    i);

				ret = gpca_crypto_set_symm_algo_params_sync(gpca_ctx, op_handle,
									    key_handle,
									    algo[i], DECRYPT_OP,
									    iv, ARRAY_SIZE(iv));
				KUNIT_EXPECT_EQ_MSG(test, 0, ret,
						    "Set crypto params decrypt failed for iteration = %d",
						    i);

				/**
				 * Assemble Ciphertext for decryption
				 * Expected format = Unencrypted data || AAD || Ciphertext || tag
				 */
				memmove(&ciphertext[symm_test_configs[cfg]
							    .unencrypted_size +
						    symm_test_configs[cfg]
							    .aad_size],
					&ciphertext[symm_test_configs[cfg]
							    .unencrypted_size],
					symm_test_configs[cfg]
							.input_data_size +
						tag_size);
				/*Copy AAD from plaintext */
				memcpy(&ciphertext[symm_test_configs[cfg]
							   .unencrypted_size],
				       &plaintext[symm_test_configs[cfg]
							  .unencrypted_size],
				       symm_test_configs[cfg].aad_size);

				/* Decrypt */
				ct_dma_addr = dma_map_single(gpca_ctx->dev,
							     ciphertext,
							     ciphertext_size,
							     DMA_TO_DEVICE);
				if (decrypted_pt_size)
					decrypted_pt_dma_addr = dma_map_single(gpca_ctx->dev,
									       decrypted_plaintext,
									       decrypted_pt_size,
									       DMA_FROM_DEVICE);

				ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									  op_handle,
									  ct_dma_addr,
									  symm_test_configs[cfg]
										.input_data_size
										+ tag_size,
									  symm_test_configs[cfg]
										.aad_size,
									  symm_test_configs[cfg]
										.unencrypted_size,
									  decrypted_pt_dma_addr,
									  decrypted_pt_size,
									  true);
				KUNIT_EXPECT_EQ_MSG(test, 0, ret,
						    "Start crypto op(ptrs) decrypt failed for iteration = %d",
						    i);

				/* DMA Unmap */
				dma_unmap_single(gpca_ctx->dev, ct_dma_addr,
						 ciphertext_size,
						 DMA_TO_DEVICE);
				if (decrypted_pt_size)
					dma_unmap_single(gpca_ctx->dev,
							 decrypted_pt_dma_addr,
							 decrypted_pt_size,
							 DMA_FROM_DEVICE);

				/* Clear op slot */
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
						    "Clear op slot failed for iteration = %d",
						    i);

				/* Compare Plaintext and decrypted plaintext */
				for (j = 0; j < symm_test_configs[cfg].unencrypted_size; j++)
					KUNIT_EXPECT_EQ_MSG(test, plaintext[j],
							    decrypted_plaintext[j],
							    "Decrypted unencrypted text didn't match for config = %d at index = %d expected = %d actual = %d",
							    cfg, j, plaintext[j],
							    decrypted_plaintext[j]);

				for (j = symm_test_configs[cfg]
						 .unencrypted_size +
					 symm_test_configs[cfg].aad_size;
				     j < plaintext_size; j++)
					KUNIT_EXPECT_EQ_MSG(test, plaintext[j],
							    decrypted_plaintext
								[j -
								symm_test_configs[cfg]
									.aad_size],
							    "Decrypted plain text didn't match for config = %d at index = %d expected = %d actual = %d",
							    cfg, j, plaintext[j],
							    decrypted_plaintext
								[j -
								symm_test_configs[cfg]
									.aad_size]);
			} else {
				ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									  op_handle,
									  pt_dma_addr,
									  symm_test_configs[cfg]
										.input_data_size,
									  symm_test_configs[cfg]
										.aad_size,
									  symm_test_configs[cfg]
										.unencrypted_size,
									  ct_dma_addr,
									  ciphertext_size,
									  true);
				KUNIT_EXPECT_NE_MSG(test, 0, ret,
						    "Start crypto op(ptrs) encrypt passed for iteration = %d, expected failure",
						    i);
				/* Clear op slot */
				KUNIT_EXPECT_EQ_MSG(test, 0,
						    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
						    "Clear op slot failed for iteration = %d",
						    i);
			}
			kfree(plaintext);
			kfree(ciphertext);
			kfree(decrypted_plaintext);
		}

		/* Clear key */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_clear(gpca_ctx, &key_handle),
				    "Clear key failed for iteration = %d", i);
	}
	gpca_crypto_op_handle_free(op_handle);
}

void gpca_crypto_aes_gcm_known_answer(struct kunit *test)
{
	const u8 key[] = { 0x28, 0xff, 0x3d, 0xef, 0x08, 0x17, 0x93, 0x11,
			   0xe2, 0x73, 0x4c, 0x6d, 0x1c, 0x4e, 0x28, 0x71 };
	u8 iv[] = { 0x32, 0xbc, 0xb9, 0xb5, 0x69, 0xe3,
		    0xb8, 0x52, 0xd3, 0x7c, 0x76, 0x6a };
	const u8 aad[] = { 0xc3 };
	const u8 ct[] = { 0xf5, 0x8d, 0x45, 0x32, 0x12, 0xc2, 0xc8,
			  0xa4, 0x36, 0xe9, 0x28, 0x36, 0x72, 0xf5,
			  0x79, 0xf1, 0x19, 0x12, 0x29, 0x78 };
	u8 tag[] = { 0x59, 0x01, 0x13, 0x1d, 0x07, 0x60, 0xc8, 0x71,
		     0x59, 0x01, 0xd8, 0x81, 0xfd, 0xfd, 0x3b, 0xc0 };
	const u8 expected_msg[] = { 0xdf, 0xc6, 0x1a, 0x20, 0xdf, 0x85, 0x05,
				    0xb5, 0x3e, 0x3c, 0xd5, 0x9f, 0x25, 0x77,
				    0x0d, 0x50, 0x18, 0xad, 0xd3, 0xd6 };

	struct gpca_key_policy kp = { GPCA_KEY_CLASS_PORTABLE,
				      GPCA_ALGO_AES128_GCM,
				      GPCA_SUPPORTED_PURPOSE_DECRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_ENABLE,
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };
	struct gpca_key_handle *key_handle = NULL;
	struct gpca_crypto_op_handle *op_handle = NULL;
	dma_addr_t ct_dma_addr, decrypted_pt_dma_addr;
	u8 *ct_buf;
	u8 *decrypted_pt_buf;
	u32 j = 0;

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_import(gpca_ctx, &key_handle, &kp, key,
					ARRAY_SIZE(key)));

	if (gpca_ctx->reversed_iv)
		reverse_buffer_in_place(iv, ARRAY_SIZE(iv));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
							      op_handle,
							      key_handle,
							      GPCA_ALGO_AES128_GCM,
							      DECRYPT_OP, iv,
							      ARRAY_SIZE(iv)));

	ct_buf = kmalloc(ARRAY_SIZE(aad) + ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
			 GFP_KERNEL);
	decrypted_pt_buf = kmalloc(ARRAY_SIZE(ct), GFP_KERNEL);

	for (j = 0; j < ARRAY_SIZE(aad); j++)
		ct_buf[j] = aad[j];
	for (j = 0; j < ARRAY_SIZE(ct); j++)
		ct_buf[j + ARRAY_SIZE(aad)] = ct[j];
	for (j = 0; j < ARRAY_SIZE(tag); j++)
		ct_buf[j + ARRAY_SIZE(aad) + ARRAY_SIZE(ct)] = tag[j];

	/* Decrypt */
	ct_dma_addr = dma_map_single(gpca_ctx->dev, ct_buf,
				     ARRAY_SIZE(aad) + ARRAY_SIZE(ct) +
					     ARRAY_SIZE(tag),
				     DMA_TO_DEVICE);
	decrypted_pt_dma_addr = dma_map_single(gpca_ctx->dev, decrypted_pt_buf,
					       ARRAY_SIZE(ct), DMA_FROM_DEVICE);
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_start_symmetric_op_sync(gpca_ctx,
							    op_handle,
							    ct_dma_addr,
							    0,
							    ARRAY_SIZE(aad), 0,
							    0,
							    0, false));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_start_symmetric_op_sync(gpca_ctx,
							    op_handle,
							    ct_dma_addr + ARRAY_SIZE(aad),
							    ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
							    0, 0,
							    decrypted_pt_dma_addr,
							    ARRAY_SIZE(ct), true));

	/* DMA Unmap */
	dma_unmap_single(gpca_ctx->dev, ct_dma_addr,
			 ARRAY_SIZE(aad) + ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
			 DMA_TO_DEVICE);
	dma_unmap_single(gpca_ctx->dev, decrypted_pt_dma_addr, ARRAY_SIZE(ct),
			 DMA_FROM_DEVICE);

	/* Clear op slot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_clear_op_sync(gpca_ctx, op_handle));

	/* Compare Plaintext and decrypted plaintext */
	for (j = 0; j < ARRAY_SIZE(ct); j++)
		KUNIT_EXPECT_EQ_MSG(test, expected_msg[j], decrypted_pt_buf[j],
				    "Decrypted unencrypted text didn't match for at index = %d expected = %d actual = %d",
				    j, expected_msg[j], decrypted_pt_buf[j]);

	kfree(ct_buf);
	kfree(decrypted_pt_buf);

	/* Skip AAD */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
							      op_handle,
							      key_handle,
							      GPCA_ALGO_AES128_GCM,
							      DECRYPT_OP, iv,
							      ARRAY_SIZE(iv)));

	ct_buf = kmalloc(ARRAY_SIZE(ct) + ARRAY_SIZE(tag), GFP_KERNEL);
	decrypted_pt_buf = kmalloc(ARRAY_SIZE(ct), GFP_KERNEL);

	for (j = 0; j < ARRAY_SIZE(ct); j++)
		ct_buf[j] = ct[j];
	for (j = 0; j < ARRAY_SIZE(tag); j++)
		ct_buf[j + ARRAY_SIZE(ct)] = tag[j];
	/* Decrypt */
	ct_dma_addr = dma_map_single(gpca_ctx->dev, ct_buf,
				     ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
				     DMA_TO_DEVICE);
	decrypted_pt_dma_addr = dma_map_single(gpca_ctx->dev, decrypted_pt_buf,
					       ARRAY_SIZE(ct), DMA_FROM_DEVICE);

	KUNIT_EXPECT_NE(test, 0,
			gpca_crypto_start_symmetric_op_sync(gpca_ctx,
							    op_handle,
							    ct_dma_addr,
							    ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
							    0, 0, decrypted_pt_dma_addr,
							    ARRAY_SIZE(ct), true));

	/* DMA Unmap */
	dma_unmap_single(gpca_ctx->dev, ct_dma_addr,
			 ARRAY_SIZE(ct) + ARRAY_SIZE(tag), DMA_TO_DEVICE);
	dma_unmap_single(gpca_ctx->dev, decrypted_pt_dma_addr, ARRAY_SIZE(ct),
			 DMA_FROM_DEVICE);

	/* Clear op slot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_clear_op_sync(gpca_ctx, op_handle));
	kfree(ct_buf);
	kfree(decrypted_pt_buf);

	/* Wrong tag */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
							      op_handle,
							      key_handle,
							      GPCA_ALGO_AES128_GCM,
							      DECRYPT_OP, iv,
							      ARRAY_SIZE(iv)));

	ct_buf = kmalloc(ARRAY_SIZE(aad) + ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
			 GFP_KERNEL);
	decrypted_pt_buf = kmalloc(ARRAY_SIZE(ct), GFP_KERNEL);

	/* Tamper tag */
	tag[0] = ~tag[0];
	for (j = 0; j < ARRAY_SIZE(aad); j++)
		ct_buf[j] = aad[j];
	for (j = 0; j < ARRAY_SIZE(ct); j++)
		ct_buf[j + ARRAY_SIZE(aad)] = ct[j];
	for (j = 0; j < ARRAY_SIZE(tag); j++)
		ct_buf[j + ARRAY_SIZE(aad) + ARRAY_SIZE(ct)] = tag[j];

	/* Decrypt */
	ct_dma_addr = dma_map_single(gpca_ctx->dev, ct_buf,
				     ARRAY_SIZE(aad) + ARRAY_SIZE(ct) +
					     ARRAY_SIZE(tag),
				     DMA_TO_DEVICE);
	decrypted_pt_dma_addr = dma_map_single(gpca_ctx->dev, decrypted_pt_buf,
					       ARRAY_SIZE(ct), DMA_FROM_DEVICE);

	KUNIT_EXPECT_NE(test, 0,
			gpca_crypto_start_symmetric_op_sync(gpca_ctx,
							    op_handle,
							    ct_dma_addr,
							    ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
							    ARRAY_SIZE(aad), 0,
							    decrypted_pt_dma_addr,
							    ARRAY_SIZE(ct), true));

	/* DMA Unmap */
	dma_unmap_single(gpca_ctx->dev, ct_dma_addr,
			 ARRAY_SIZE(aad) + ARRAY_SIZE(ct) + ARRAY_SIZE(tag),
			 DMA_TO_DEVICE);
	dma_unmap_single(gpca_ctx->dev, decrypted_pt_dma_addr, ARRAY_SIZE(ct),
			 DMA_FROM_DEVICE);

	/* Clear op slot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_crypto_clear_op_sync(gpca_ctx, op_handle));
	kfree(ct_buf);
	kfree(decrypted_pt_buf);

	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	gpca_crypto_op_handle_free(op_handle);
}

struct gpca_streaming_test {
	const u8 *key;
	u32 key_size;
	enum gpca_algorithm algo;
	u32 block_size;
	const u8 *pt;
	u32 pt_size;
	const u8 *ct;
	u32 ct_size;
	const u8 *iv;
	u32 iv_size;
};

const u8 aes128_key[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
			  0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };

const u8 aes192_key[] = { 0x8e, 0x73, 0xb0, 0xf7, 0xda, 0x0e, 0x64, 0x52,
			  0xc8, 0x10, 0xf3, 0x2b, 0x80, 0x90, 0x79, 0xe5,
			  0x62, 0xf8, 0xea, 0xd2, 0x52, 0x2c, 0x6b, 0x7b };

const u8 aes256_key[] = { 0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
			  0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
			  0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
			  0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4 };

const u8 aes_pt[] = { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
		      0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
		      0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
		      0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
		      0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
		      0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
		      0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
		      0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10 };

const u8 aes_cbc_iv[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };

const u8 aes_ctr_iv[] = { 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
			  0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff };

/* From https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_CBC.pdf */
const u8 aes128_cbc_ct[] = { 0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
			     0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
			     0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
			     0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
			     0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b,
			     0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
			     0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09,
			     0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7 };

const u8 aes192_cbc_ct[] = { 0x4f, 0x02, 0x1d, 0xb2, 0x43, 0xbc, 0x63, 0x3d,
			     0x71, 0x78, 0x18, 0x3a, 0x9f, 0xa0, 0x71, 0xe8,
			     0xb4, 0xd9, 0xad, 0xa9, 0xad, 0x7d, 0xed, 0xf4,
			     0xe5, 0xe7, 0x38, 0x76, 0x3f, 0x69, 0x14, 0x5a,
			     0x57, 0x1b, 0x24, 0x20, 0x12, 0xfb, 0x7a, 0xe0,
			     0x7f, 0xa9, 0xba, 0xac, 0x3d, 0xf1, 0x02, 0xe0,
			     0x08, 0xb0, 0xe2, 0x79, 0x88, 0x59, 0x88, 0x81,
			     0xd9, 0x20, 0xa9, 0xe6, 0x4f, 0x56, 0x15, 0xcd };

const u8 aes256_cbc_ct[] = { 0xf5, 0x8c, 0x4c, 0x04, 0xd6, 0xe5, 0xf1, 0xba,
			     0x77, 0x9e, 0xab, 0xfb, 0x5f, 0x7b, 0xfb, 0xd6,
			     0x9c, 0xfc, 0x4e, 0x96, 0x7e, 0xdb, 0x80, 0x8d,
			     0x67, 0x9f, 0x77, 0x7b, 0xc6, 0x70, 0x2c, 0x7d,
			     0x39, 0xf2, 0x33, 0x69, 0xa9, 0xd9, 0xba, 0xcf,
			     0xa5, 0x30, 0xe2, 0x63, 0x04, 0x23, 0x14, 0x61,
			     0xb2, 0xeb, 0x05, 0xe2, 0xc3, 0x9b, 0xe9, 0xfc,
			     0xda, 0x6c, 0x19, 0x07, 0x8c, 0x6a, 0x9d, 0x1b };

/* From https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_ECB.pdf */
const u8 aes128_ecb_ct[] = { 0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
			     0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97,
			     0xf5, 0xd3, 0xd5, 0x85, 0x03, 0xb9, 0x69, 0x9d,
			     0xe7, 0x85, 0x89, 0x5a, 0x96, 0xfd, 0xba, 0xaf,
			     0x43, 0xb1, 0xcd, 0x7f, 0x59, 0x8e, 0xce, 0x23,
			     0x88, 0x1b, 0x00, 0xe3, 0xed, 0x03, 0x06, 0x88,
			     0x7b, 0x0c, 0x78, 0x5e, 0x27, 0xe8, 0xad, 0x3f,
			     0x82, 0x23, 0x20, 0x71, 0x04, 0x72, 0x5d, 0xd4 };

const u8 aes192_ecb_ct[] = { 0xbd, 0x33, 0x4f, 0x1d, 0x6e, 0x45, 0xf2, 0x5f,
			     0xf7, 0x12, 0xa2, 0x14, 0x57, 0x1f, 0xa5, 0xcc,
			     0x97, 0x41, 0x04, 0x84, 0x6d, 0x0a, 0xd3, 0xad,
			     0x77, 0x34, 0xec, 0xb3, 0xec, 0xee, 0x4e, 0xef,
			     0xef, 0x7a, 0xfd, 0x22, 0x70, 0xe2, 0xe6, 0x0a,
			     0xdc, 0xe0, 0xba, 0x2f, 0xac, 0xe6, 0x44, 0x4e,
			     0x9a, 0x4b, 0x41, 0xba, 0x73, 0x8d, 0x6c, 0x72,
			     0xfb, 0x16, 0x69, 0x16, 0x03, 0xc1, 0x8e, 0x0e };

const u8 aes256_ecb_ct[] = { 0xf3, 0xee, 0xd1, 0xbd, 0xb5, 0xd2, 0xa0, 0x3c,
			     0x06, 0x4b, 0x5a, 0x7e, 0x3d, 0xb1, 0x81, 0xf8,
			     0x59, 0x1c, 0xcb, 0x10, 0xd4, 0x10, 0xed, 0x26,
			     0xdc, 0x5b, 0xa7, 0x4a, 0x31, 0x36, 0x28, 0x70,
			     0xb6, 0xed, 0x21, 0xb9, 0x9c, 0xa6, 0xf4, 0xf9,
			     0xf1, 0x53, 0xe7, 0xb1, 0xbe, 0xaf, 0xed, 0x1d,
			     0x23, 0x30, 0x4b, 0x7a, 0x39, 0xf9, 0xf3, 0xff,
			     0x06, 0x7d, 0x8d, 0x8f, 0x9e, 0x24, 0xec, 0xc7 };

/* From https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_CTR.pdf */
const u8 aes128_ctr_ct[] = { 0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
			     0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
			     0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
			     0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
			     0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
			     0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
			     0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
			     0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee };

const u8 aes192_ctr_ct[] = { 0x1a, 0xbc, 0x93, 0x24, 0x17, 0x52, 0x1c, 0xa2,
			     0x4f, 0x2b, 0x04, 0x59, 0xfe, 0x7e, 0x6e, 0x0b,
			     0x09, 0x03, 0x39, 0xec, 0x0a, 0xa6, 0xfa, 0xef,
			     0xd5, 0xcc, 0xc2, 0xc6, 0xf4, 0xce, 0x8e, 0x94,
			     0x1e, 0x36, 0xb2, 0x6b, 0xd1, 0xeb, 0xc6, 0x70,
			     0xd1, 0xbd, 0x1d, 0x66, 0x56, 0x20, 0xab, 0xf7,
			     0x4f, 0x78, 0xa7, 0xf6, 0xd2, 0x98, 0x09, 0x58,
			     0x5a, 0x97, 0xda, 0xec, 0x58, 0xc6, 0xb0, 0x50 };

const u8 aes256_ctr_ct[] = { 0x60, 0x1e, 0xc3, 0x13, 0x77, 0x57, 0x89, 0xa5,
			     0xb7, 0xa7, 0xf5, 0x04, 0xbb, 0xf3, 0xd2, 0x28,
			     0xf4, 0x43, 0xe3, 0xca, 0x4d, 0x62, 0xb5, 0x9a,
			     0xca, 0x84, 0xe9, 0x90, 0xca, 0xca, 0xf5, 0xc5,
			     0x2b, 0x09, 0x30, 0xda, 0xa2, 0x3d, 0xe9, 0x4c,
			     0xe8, 0x70, 0x17, 0xba, 0x2d, 0x84, 0x98, 0x8d,
			     0xdf, 0xc9, 0xc5, 0x8d, 0xb6, 0x7a, 0xad, 0xa6,
			     0x13, 0xc2, 0xdd, 0x08, 0x45, 0x79, 0x41, 0xa6 };

const u8 tdes_key[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
			0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
			0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23 };

const u8 tdes_key2[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
			 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01,
			 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef };

const u8 tdes_pt[] = { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
		       0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
		       0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
		       0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51 };

/* From https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/TDES_CBC.pdf */
const u8 tdes_cbc_iv[] = { 0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17 };

const u8 tdes_cbc_ct[] = { 0x20, 0x79, 0xc3, 0xd5, 0x3a, 0xa7, 0x63, 0xe1,
			   0x93, 0xb7, 0x9e, 0x25, 0x69, 0xab, 0x52, 0x62,
			   0x51, 0x65, 0x70, 0x48, 0x1f, 0x25, 0xb5, 0x0f,
			   0x73, 0xc0, 0xbd, 0xa8, 0x5c, 0x8e, 0x0d, 0xa7 };

const u8 tdes_cbc_ct2[] = { 0x74, 0x01, 0xce, 0x1e, 0xab, 0x6d, 0x00, 0x3c,
			    0xaf, 0xf8, 0x4b, 0xf4, 0x7b, 0x36, 0xcc, 0x21,
			    0x54, 0xf0, 0x23, 0x8f, 0x9f, 0xfe, 0xcd, 0x8f,
			    0x6a, 0xcf, 0x11, 0x83, 0x92, 0xb4, 0x55, 0x81 };

/* From https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/TDES_ECB.pdf */
const u8 tdes_ecb_ct[] = { 0x71, 0x47, 0x72, 0xf3, 0x39, 0x84, 0x1d, 0x34,
			   0x26, 0x7f, 0xcc, 0x4b, 0xd2, 0x94, 0x9c, 0xc3,
			   0xee, 0x11, 0xc2, 0x2a, 0x57, 0x6a, 0x30, 0x38,
			   0x76, 0x18, 0x3f, 0x99, 0xc0, 0xb6, 0xde, 0x87 };

const u8 tdes_ecb_ct2[] = { 0x06, 0xed, 0xe3, 0xd8, 0x28, 0x84, 0x09, 0x0a,
			    0xff, 0x32, 0x2c, 0x19, 0xf0, 0x51, 0x84, 0x86,
			    0x73, 0x05, 0x76, 0x97, 0x2a, 0x66, 0x6e, 0x58,
			    0xb6, 0xc8, 0x8c, 0xf1, 0x07, 0x34, 0x0d, 0x3d };

struct gpca_streaming_test gpca_streaming_tests[] = {
	{
		aes128_key,
		ARRAY_SIZE(aes128_key),
		GPCA_ALGO_AES128_ECB,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes128_ecb_ct,
		ARRAY_SIZE(aes128_ecb_ct),
		NULL,
		0,
	},
	{
		aes192_key,
		ARRAY_SIZE(aes192_key),
		GPCA_ALGO_AES192_ECB,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes192_ecb_ct,
		ARRAY_SIZE(aes192_ecb_ct),
		NULL,
		0,
	},
	{
		aes256_key,
		ARRAY_SIZE(aes256_key),
		GPCA_ALGO_AES256_ECB,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes256_ecb_ct,
		ARRAY_SIZE(aes256_ecb_ct),
		NULL,
		0,
	},
	{
		aes128_key,
		ARRAY_SIZE(aes128_key),
		GPCA_ALGO_AES128_CBC,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes128_cbc_ct,
		ARRAY_SIZE(aes128_cbc_ct),
		aes_cbc_iv,
		ARRAY_SIZE(aes_cbc_iv),
	},
	{
		aes192_key,
		ARRAY_SIZE(aes192_key),
		GPCA_ALGO_AES192_CBC,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes192_cbc_ct,
		ARRAY_SIZE(aes192_cbc_ct),
		aes_cbc_iv,
		ARRAY_SIZE(aes_cbc_iv),
	},
	{
		aes256_key,
		ARRAY_SIZE(aes256_key),
		GPCA_ALGO_AES256_CBC,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes256_cbc_ct,
		ARRAY_SIZE(aes256_cbc_ct),
		aes_cbc_iv,
		ARRAY_SIZE(aes_cbc_iv),
	},
	{
		aes128_key,
		ARRAY_SIZE(aes128_key),
		GPCA_ALGO_AES128_CTR,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes128_ctr_ct,
		ARRAY_SIZE(aes128_ctr_ct),
		aes_ctr_iv,
		ARRAY_SIZE(aes_ctr_iv),
	},
	{
		aes192_key,
		ARRAY_SIZE(aes192_key),
		GPCA_ALGO_AES192_CTR,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes192_ctr_ct,
		ARRAY_SIZE(aes192_ctr_ct),
		aes_ctr_iv,
		ARRAY_SIZE(aes_ctr_iv),
	},
	{
		aes256_key,
		ARRAY_SIZE(aes256_key),
		GPCA_ALGO_AES256_CTR,
		16,
		aes_pt,
		ARRAY_SIZE(aes_pt),
		aes256_ctr_ct,
		ARRAY_SIZE(aes256_ctr_ct),
		aes_ctr_iv,
		ARRAY_SIZE(aes_ctr_iv),
	},
	{
		tdes_key,
		ARRAY_SIZE(tdes_key),
		GPCA_ALGO_TDES_ECB,
		8,
		tdes_pt,
		ARRAY_SIZE(tdes_pt),
		tdes_ecb_ct,
		ARRAY_SIZE(tdes_ecb_ct),
		NULL,
		0,
	},
	{
		tdes_key2,
		ARRAY_SIZE(tdes_key2),
		GPCA_ALGO_TDES_ECB,
		8,
		tdes_pt,
		ARRAY_SIZE(tdes_pt),
		tdes_ecb_ct2,
		ARRAY_SIZE(tdes_ecb_ct2),
		NULL,
		0,
	},
	{
		tdes_key,
		ARRAY_SIZE(tdes_key),
		GPCA_ALGO_TDES_CBC,
		8,
		tdes_pt,
		ARRAY_SIZE(tdes_pt),
		tdes_cbc_ct,
		ARRAY_SIZE(tdes_cbc_ct),
		tdes_cbc_iv,
		ARRAY_SIZE(tdes_cbc_iv),
	},
	{
		tdes_key2,
		ARRAY_SIZE(tdes_key2),
		GPCA_ALGO_TDES_CBC,
		8,
		tdes_pt,
		ARRAY_SIZE(tdes_pt),
		tdes_cbc_ct2,
		ARRAY_SIZE(tdes_cbc_ct2),
		tdes_cbc_iv,
		ARRAY_SIZE(tdes_cbc_iv),
	},
};

void gpca_crypto_aes_tdes_streaming(struct kunit *test)
{
	u32 i = 0, j = 0;
	struct gpca_key_handle *key_handle;
	u8 *plaintext = NULL;
	u8 *ciphertext = NULL;
	u8 *decrypted_plaintext = NULL;
	dma_addr_t pt_dma_addr;
	dma_addr_t ct_dma_addr;
	dma_addr_t decrypted_pt_dma_addr;
	struct gpca_crypto_op_handle *op_handle = NULL;
	int ret = 0;
	u8 iv[16];
	u32 iv_size;

	struct gpca_key_policy kp = { GPCA_KEY_CLASS_PORTABLE,
				      GPCA_ALGO_AES256_ECB,
				      GPCA_SUPPORTED_PURPOSE_ENCRYPT |
					      GPCA_SUPPORTED_PURPOSE_DECRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_ENABLE,
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);

	/* Encrypt, Decrypt match */
	for (i = 0; i < ARRAY_SIZE(gpca_streaming_tests); i++) {
		kp.algo = gpca_streaming_tests[i].algo;
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_import(gpca_ctx, &key_handle, &kp,
						    gpca_streaming_tests[i].key,
						    gpca_streaming_tests[i].key_size),
				    "Key import failed for iteration = %d", i);

		plaintext = dma_alloc_coherent(gpca_ctx->dev,
					       gpca_streaming_tests[i].pt_size,
					       &pt_dma_addr, GFP_KERNEL);
		KUNIT_EXPECT_PTR_NE(test, NULL, plaintext);
		for (j = 0; j < gpca_streaming_tests[i].pt_size; j++)
			plaintext[j] = gpca_streaming_tests[i].pt[j];

		ciphertext = dma_alloc_coherent(gpca_ctx->dev,
						gpca_streaming_tests[i].ct_size,
						&ct_dma_addr, GFP_KERNEL);
		KUNIT_EXPECT_PTR_NE(test, NULL, ciphertext);

		decrypted_plaintext = dma_alloc_coherent(gpca_ctx->dev,
							 gpca_streaming_tests[i].pt_size,
							 &decrypted_pt_dma_addr,
							 GFP_KERNEL);
		KUNIT_EXPECT_PTR_NE(test, NULL, decrypted_plaintext);

		iv_size = gpca_streaming_tests[i].iv_size;
		if (gpca_streaming_tests[i].iv) {
			memcpy(iv, gpca_streaming_tests[i].iv, iv_size);
			if (gpca_ctx->reversed_iv)
				reverse_buffer_in_place(iv, iv_size);
		}
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
									  op_handle,
									  key_handle,
									  kp.algo,
									  ENCRYPT_OP,
									  iv,
									  iv_size),
				    "Set crypto params encrypt failed for iteration = %d", i);

		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									op_handle,
									pt_dma_addr,
									gpca_streaming_tests[i]
										.block_size,
									0,
									0,
									ct_dma_addr,
									gpca_streaming_tests[i]
										.block_size,
									false),
				    "Start crypto op(ptrs) encrypt failed for iteration = %d", i);
		ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx, op_handle,
							  pt_dma_addr +
								gpca_streaming_tests[i].block_size,
							  gpca_streaming_tests[i].block_size, 0, 0,
							  ct_dma_addr +
								gpca_streaming_tests[i].block_size,
							  gpca_streaming_tests[i].block_size,
							  false);
		KUNIT_EXPECT_EQ_MSG(test, 0, ret,
				    "Start crypto op(ptrs) encrypt failed for iteration = %d", i);

		ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx, op_handle,
							  pt_dma_addr +
								(2 *
								gpca_streaming_tests[i].block_size),
							  gpca_streaming_tests[i].pt_size -
								(2 *
								gpca_streaming_tests[i].block_size),
							  0, 0,
							  ct_dma_addr +
								(2 *
								gpca_streaming_tests[i].block_size),
							  gpca_streaming_tests[i].ct_size -
								(2 *
								gpca_streaming_tests[i].block_size),
							  true);
		KUNIT_EXPECT_EQ_MSG(test, 0, ret,
				    "Start crypto op(ptrs) encrypt failed for iteration = %d", i);

		/* Check ciphertext */
		for (j = 0; j < gpca_streaming_tests[i].ct_size; j++)
			KUNIT_EXPECT_EQ_MSG(test, gpca_streaming_tests[i].ct[j],
					    ciphertext[j],
					    "Ciphertext didn't match for test = %d at index = %d expected = %d actual = %d",
					    i, j, gpca_streaming_tests[i].ct[j], ciphertext[j]);

		/* Clear op slot */
		KUNIT_EXPECT_EQ_MSG(test, 0, gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
				    "Clear op slot failed for iteration = %d",
				    i);

		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
									  op_handle,
									  key_handle,
									  kp.algo,
									  DECRYPT_OP,
									  iv,
									  iv_size),
				    "Set crypto params decrypt failed for iteration = %d", i);

		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									op_handle,
									ct_dma_addr,
									gpca_streaming_tests[i]
										.block_size,
									0,
									0,
									decrypted_pt_dma_addr,
									gpca_streaming_tests[i]
										.block_size,
									false),
				    "Start crypto op(ptrs) decrypt failed for iteration = %d", i);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_start_symmetric_op_sync(gpca_ctx,
									op_handle,
									ct_dma_addr +
									gpca_streaming_tests[i]
										.block_size,
									gpca_streaming_tests[i]
										.block_size,
									0,
									0,
									decrypted_pt_dma_addr +
									gpca_streaming_tests[i]
										.block_size,
									gpca_streaming_tests[i]
										.block_size,
									false),
			"Start crypto op(ptrs) decrypt failed for iteration = %d",
			i);

		ret = gpca_crypto_start_symmetric_op_sync(gpca_ctx,
							  op_handle,
							  ct_dma_addr +
								(2 * gpca_streaming_tests[i]
									.block_size),
							  gpca_streaming_tests[i].ct_size -
								(2 * gpca_streaming_tests[i]
									.block_size),
							  0,
							  0,
							  decrypted_pt_dma_addr +
								(2 * gpca_streaming_tests[i]
									.block_size),
							  gpca_streaming_tests[i].pt_size -
								(2 * gpca_streaming_tests[i]
									.block_size),
							  true);
		KUNIT_EXPECT_EQ_MSG(test, 0, ret,
				    "Start crypto op(ptrs) decrypt failed for iteration = %d", i);

		/* Compare Plaintext */
		for (j = 0; j < gpca_streaming_tests[i].pt_size; j++)
			KUNIT_EXPECT_EQ_MSG(test, gpca_streaming_tests[i].pt[j],
					    decrypted_plaintext[j],
					    "Decrypted didn't match for test = %d at index = %d expected = %d actual = %d",
					    i, j, gpca_streaming_tests[i].pt[j],
					    decrypted_plaintext[j]);

		/* Clear op slot */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
				    "Clear op slot failed for iteration = %d", i);

		dma_free_coherent(gpca_ctx->dev,
				  gpca_streaming_tests[i].pt_size, plaintext,
				  pt_dma_addr);
		dma_free_coherent(gpca_ctx->dev,
				  gpca_streaming_tests[i].ct_size, ciphertext,
				  ct_dma_addr);
		dma_free_coherent(gpca_ctx->dev,
				  gpca_streaming_tests[i].pt_size,
				  decrypted_plaintext, decrypted_pt_dma_addr);

		/* Clear key */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_clear(gpca_ctx, &key_handle),
				    "Clear key failed for iteration = %d", i);
	}
	gpca_crypto_op_handle_free(op_handle);
}

struct gpca_streaming_gcm_test {
	const u8 *key;
	u32 key_size;
	enum gpca_algorithm algo;
	u32 block_size;
	const u8 *iv;
	u32 iv_size;
	const u8 *aad;
	u32 aad_size;
	const u8 *pt;
	u32 pt_size;
	const u8 *ct;
	u32 ct_size;
};

/* From https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_GCM.pdf */
const u8 aes_gcm_iv[] = { 0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce,
			  0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88 };

const u8 aes_gcm_aad[] = { 0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
			   0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97,
			   0xf5, 0xd3, 0xd5, 0x85, 0x03, 0xb9, 0x69, 0x9d,
			   0xe7, 0x85, 0x89, 0x5a, 0x96, 0xfd, 0xba, 0xaf,
			   0x43, 0xb1, 0xcd, 0x7f, 0x59, 0x8e, 0xce, 0x23,
			   0x88, 0x1b, 0x00, 0xe3, 0xed, 0x03, 0x06, 0x88,
			   0x7b, 0x0c, 0x78, 0x5e, 0x27, 0xe8, 0xad, 0x3f,
			   0x82, 0x23, 0x20, 0x71, 0x04, 0x72, 0x5d, 0xd4 };

const u8 aes_gcm_pt[] = { 0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
			  0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
			  0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
			  0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
			  0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
			  0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
			  0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
			  0xba, 0x63, 0x7b, 0x39, 0x1a, 0xaf, 0xd2, 0x55 };

const u8 aes128_gcm_key[] = { 0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
			      0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08 };

const u8 aes128_gcm_ct[] = {
	0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24, 0x4b, 0x72, 0x21, 0xb7,
	0x84, 0xd0, 0xd4, 0x9c, 0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0,
	0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e, 0x21, 0xd5, 0x14, 0xb2,
	0x54, 0x66, 0x93, 0x1c, 0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
	0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97, 0x3d, 0x58, 0xe0, 0x91,
	0x47, 0x3f, 0x59, 0x85, 0x64, 0xc0, 0x23, 0x29, 0x04, 0xaf, 0x39, 0x8a,
	0x5b, 0x67, 0xc1, 0x0b, 0x53, 0xa5, 0x02, 0x4d
};

const u8 aes192_gcm_key[] = { 0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
			      0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
			      0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c };

const u8 aes192_gcm_ct[] = {
	0x39, 0x80, 0xca, 0x0b, 0x3c, 0x00, 0xe8, 0x41, 0xeb, 0x06, 0xfa, 0xc4,
	0x87, 0x2a, 0x27, 0x57, 0x85, 0x9e, 0x1c, 0xea, 0xa6, 0xef, 0xd9, 0x84,
	0x62, 0x85, 0x93, 0xb4, 0x0c, 0xa1, 0xe1, 0x9c, 0x7d, 0x77, 0x3d, 0x00,
	0xc1, 0x44, 0xc5, 0x25, 0xac, 0x61, 0x9d, 0x18, 0xc8, 0x4a, 0x3f, 0x47,
	0x18, 0xe2, 0x44, 0x8b, 0x2f, 0xe3, 0x24, 0xd9, 0xcc, 0xda, 0x27, 0x10,
	0xac, 0xad, 0xe2, 0x56, 0x3b, 0x91, 0x53, 0xb4, 0xe7, 0x31, 0x8a, 0x5f,
	0x3b, 0xbe, 0xac, 0x10, 0x8f, 0x8a, 0x8e, 0xdb
};

const u8 aes256_gcm_key[] = { 0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
			      0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
			      0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
			      0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08 };

const u8 aes256_gcm_ct[] = {
	0x52, 0x2d, 0xc1, 0xf0, 0x99, 0x56, 0x7d, 0x07, 0xf4, 0x7f, 0x37, 0xa3,
	0x2a, 0x84, 0x42, 0x7d, 0x64, 0x3a, 0x8c, 0xdc, 0xbf, 0xe5, 0xc0, 0xc9,
	0x75, 0x98, 0xa2, 0xbd, 0x25, 0x55, 0xd1, 0xaa, 0x8c, 0xb0, 0x8e, 0x48,
	0x59, 0x0d, 0xbb, 0x3d, 0xa7, 0xb0, 0x8b, 0x10, 0x56, 0x82, 0x88, 0x38,
	0xc5, 0xf6, 0x1e, 0x63, 0x93, 0xba, 0x7a, 0x0a, 0xbc, 0xc9, 0xf6, 0x62,
	0x89, 0x80, 0x15, 0xad, 0xc0, 0x6d, 0x76, 0xf3, 0x19, 0x30, 0xfe, 0xf3,
	0x7a, 0xca, 0xe2, 0x3e, 0xd4, 0x65, 0xae, 0x62
};

struct gpca_streaming_gcm_test gpca_streaming_gcm_tests[] = {
	{
		aes128_gcm_key,
		ARRAY_SIZE(aes128_gcm_key),
		GPCA_ALGO_AES128_GCM,
		16,
		aes_gcm_iv,
		ARRAY_SIZE(aes_gcm_iv),
		aes_gcm_aad,
		ARRAY_SIZE(aes_gcm_aad),
		aes_gcm_pt,
		ARRAY_SIZE(aes_gcm_pt),
		aes128_gcm_ct,
		ARRAY_SIZE(aes128_gcm_ct),
	},
	{
		aes192_gcm_key,
		ARRAY_SIZE(aes192_gcm_key),
		GPCA_ALGO_AES192_GCM,
		16,
		aes_gcm_iv,
		ARRAY_SIZE(aes_gcm_iv),
		aes_gcm_aad,
		ARRAY_SIZE(aes_gcm_aad),
		aes_gcm_pt,
		ARRAY_SIZE(aes_gcm_pt),
		aes192_gcm_ct,
		ARRAY_SIZE(aes192_gcm_ct),
	},
	{
		aes256_gcm_key,
		ARRAY_SIZE(aes256_gcm_key),
		GPCA_ALGO_AES256_GCM,
		16,
		aes_gcm_iv,
		ARRAY_SIZE(aes_gcm_iv),
		aes_gcm_aad,
		ARRAY_SIZE(aes_gcm_aad),
		aes_gcm_pt,
		ARRAY_SIZE(aes_gcm_pt),
		aes256_gcm_ct,
		ARRAY_SIZE(aes256_gcm_ct),
	},
};

void gpca_crypto_aes_gcm_streaming(struct kunit *test)
{
	struct gpca_key_policy kp = { GPCA_KEY_CLASS_PORTABLE,
				      GPCA_ALGO_AES128_GCM,
				      GPCA_SUPPORTED_PURPOSE_ENCRYPT
					| GPCA_SUPPORTED_PURPOSE_DECRYPT,
				      GPCA_KMK_DISABLE,
				      GPCA_WAEK_ENABLE,
				      GPCA_WAHK_DISABLE,
				      GPCA_WAPK_DISABLE,
				      GPCA_BS_DISABLE,
				      GPCA_EVICT_GSA_DISABLE,
				      GPCA_EVICT_AP_S_DISABLE,
				      GPCA_EVICT_AP_NS_DISABLE,
				      GPCA_OWNER_DOMAIN_ANDROID_VM,
				      GPCA_AUTH_DOMAIN_ANDROID_VM };
	struct gpca_key_handle *key_handle = NULL;
	struct gpca_crypto_op_handle *op_handle = NULL;
	dma_addr_t pt_dma_addr, ct_dma_addr, decrypted_pt_dma_addr;
	u8 *pt_buf = NULL;
	u8 *ct_buf = NULL;
	u8 *decrypted_pt_buf = NULL;
	u32 i = 0, j = 0;
	u32 aad_size, iv_size, pt_size, ct_size, block_size;
	u8 iv[12];

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);

	for (i = 0; i < ARRAY_SIZE(gpca_streaming_gcm_tests); i++) {
		kp.algo = gpca_streaming_gcm_tests[i].algo;
		aad_size = gpca_streaming_gcm_tests[1].aad_size;
		iv_size = gpca_streaming_gcm_tests[i].iv_size;
		pt_size = gpca_streaming_gcm_tests[i].pt_size;
		ct_size = gpca_streaming_gcm_tests[i].ct_size;
		block_size = gpca_streaming_gcm_tests[i].block_size;

		KUNIT_EXPECT_EQ(test, 0,
				gpca_key_import(gpca_ctx, &key_handle, &kp,
						gpca_streaming_gcm_tests[i].key,
						gpca_streaming_gcm_tests[i].key_size));

		pt_buf = kmalloc(aad_size + pt_size, GFP_KERNEL);
		ct_buf = kmalloc(ct_size, GFP_KERNEL);

		if (gpca_streaming_gcm_tests[i].iv) {
			memcpy(iv, gpca_streaming_gcm_tests[i].iv, iv_size);
			if (gpca_ctx->reversed_iv)
				reverse_buffer_in_place(iv, iv_size);
		}
		KUNIT_EXPECT_EQ(test, 0,
				gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
								      op_handle,
								      key_handle,
								      kp.algo,
								      ENCRYPT_OP,
								      iv,
								      iv_size));

		memcpy(pt_buf, gpca_streaming_gcm_tests[i].aad, aad_size);
		memcpy(pt_buf + aad_size, gpca_streaming_gcm_tests[i].pt, pt_size);

		/* Encrypt */
		pt_dma_addr = dma_map_single(gpca_ctx->dev, pt_buf, aad_size + pt_size,
					     DMA_TO_DEVICE);
		ct_dma_addr = dma_map_single(gpca_ctx->dev, ct_buf, ct_size, DMA_FROM_DEVICE);

		KUNIT_EXPECT_EQ(test, 0,
				gpca_crypto_start_symmetric_op_sync(gpca_ctx,
								    op_handle,
								    pt_dma_addr,
								    block_size,
								    aad_size, 0,
								    ct_dma_addr,
								    block_size, false));
		KUNIT_EXPECT_EQ(test, 0,
				gpca_crypto_start_symmetric_op_sync(gpca_ctx,
								    op_handle,
								    pt_dma_addr + aad_size
									+ block_size,
								    pt_size - block_size,
								    0, 0,
								    ct_dma_addr + block_size,
								    ct_size - block_size, true));

		/* DMA Unmap */
		dma_unmap_single(gpca_ctx->dev, pt_dma_addr, aad_size + pt_size, DMA_TO_DEVICE);
		dma_unmap_single(gpca_ctx->dev, ct_dma_addr, ct_size, DMA_FROM_DEVICE);

		/* Clear op slot */
		KUNIT_EXPECT_EQ(test, 0, gpca_crypto_clear_op_sync(gpca_ctx, op_handle));

		/* Compare ciphertext */
		for (j = 0; j < ct_size; j++)
			KUNIT_EXPECT_EQ_MSG(test, gpca_streaming_gcm_tests[i].ct[j], ct_buf[j],
					    "Encrypted text didn't match for test %d at index = %d expected = %d actual = %d",
					    i, j, gpca_streaming_gcm_tests[i].ct[j], ct_buf[j]);

		kfree(pt_buf);
		kfree(ct_buf);

		ct_buf = kmalloc(aad_size + ct_size, GFP_KERNEL);
		decrypted_pt_buf = kmalloc(pt_size, GFP_KERNEL);

		KUNIT_EXPECT_EQ(test, 0,
				gpca_crypto_set_symm_algo_params_sync(gpca_ctx,
								      op_handle,
								      key_handle,
								      kp.algo,
								      DECRYPT_OP,
								      iv,
								      iv_size));

		memcpy(ct_buf, gpca_streaming_gcm_tests[i].aad, aad_size);
		memcpy(ct_buf + aad_size, gpca_streaming_gcm_tests[i].ct, ct_size);

		/* Encrypt */
		ct_dma_addr = dma_map_single(gpca_ctx->dev, ct_buf, aad_size + ct_size,
					     DMA_TO_DEVICE);
		decrypted_pt_dma_addr = dma_map_single(gpca_ctx->dev, decrypted_pt_buf, pt_size,
						       DMA_FROM_DEVICE);

		KUNIT_EXPECT_EQ(test, 0,
				gpca_crypto_start_symmetric_op_sync(gpca_ctx,
								    op_handle,
								    ct_dma_addr,
								    block_size,
								    aad_size, 0,
								    decrypted_pt_dma_addr,
								    block_size, false));
		KUNIT_EXPECT_EQ(test, 0,
				gpca_crypto_start_symmetric_op_sync(gpca_ctx,
								    op_handle,
								    ct_dma_addr + aad_size
									+ block_size,
								    ct_size - block_size,
								    0, 0,
								    decrypted_pt_dma_addr
									+ block_size,
								    pt_size - block_size, true));

		/* DMA Unmap */
		dma_unmap_single(gpca_ctx->dev, ct_dma_addr, aad_size + ct_size, DMA_TO_DEVICE);
		dma_unmap_single(gpca_ctx->dev, decrypted_pt_dma_addr, pt_size, DMA_FROM_DEVICE);

		/* Clear op slot */
		KUNIT_EXPECT_EQ(test, 0, gpca_crypto_clear_op_sync(gpca_ctx, op_handle));

		/* Compare plaintext */
		for (j = 0; j < pt_size; j++)
			KUNIT_EXPECT_EQ_MSG(test, gpca_streaming_gcm_tests[i].pt[j],
					    decrypted_pt_buf[j],
					    "Decrypted text didn't match for test %d at index = %d expected = %d actual = %d",
					    i, j, gpca_streaming_gcm_tests[i].pt[j],
					    decrypted_pt_buf[j]);

		kfree(ct_buf);
		kfree(decrypted_pt_buf);

		KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	}
}

