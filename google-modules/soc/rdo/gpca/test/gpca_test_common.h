/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * GPCA test utility functions.
 *
 * Copyright (C) 2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_TEST_COMMON_H
#define _GOOGLE_GPCA_TEST_COMMON_H

#include <linux/types.h>

#include "gpca_crypto.h"

int gpca_crypto_set_symm_algo_params_sync(struct gpca_dev *ctx,
					  struct gpca_crypto_op_handle *op_handle,
					  struct gpca_key_handle *key_handle,
					  enum gpca_algorithm algo,
					  enum gpca_supported_purpose purpose,
					  const u8 *iv_buf, u32 iv_size);

int gpca_crypto_start_symmetric_op_sync(struct gpca_dev *ctx,
					struct gpca_crypto_op_handle *op_handle,
					dma_addr_t data_addr,
					u32 input_data_size, u32 aad_size,
					u32 unencrypted_size,
					dma_addr_t output_data_addr,
					u32 output_data_size, bool is_last);

int gpca_crypto_clear_op_sync(struct gpca_dev *ctx,
			      struct gpca_crypto_op_handle *op_handle);

int gpca_crypto_set_hash_params_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     enum gpca_algorithm hash_algo);

int gpca_crypto_set_asymm_algo_params_sync(struct gpca_dev *ctx,
					   struct gpca_crypto_op_handle *op_handle,
					   struct gpca_key_handle *key_handle,
					   enum gpca_algorithm algo,
					   enum gpca_supported_purpose purpose);

int gpca_crypto_start_digest_op_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     dma_addr_t input_data_addr,
				     u32 input_data_size,
				     dma_addr_t digest_addr, u32 digest_size,
				     bool is_last);

int gpca_crypto_start_sign_op_sync(struct gpca_dev *ctx,
				   struct gpca_crypto_op_handle *op_handle,
				   dma_addr_t data_addr, u32 input_data_size,
				   dma_addr_t signature_addr,
				   u32 signature_size, bool is_last);

int gpca_crypto_start_verify_op_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     dma_addr_t data_addr, u32 input_data_size,
				     u32 signature_size, bool is_last);

int gpca_crypto_set_hmac_params_sync(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     struct gpca_key_handle *key_handle,
				     enum gpca_algorithm hmac_algo,
				     enum gpca_supported_purpose purpose);

void reverse_buffer_in_place(u8 *buf, u32 len);

#endif /* _GOOGLE_GPCA_TEST_COMMON_H */
