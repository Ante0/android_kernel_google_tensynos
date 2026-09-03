/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GPCA crypto operations driver.
 *
 * Copyright (C) 2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_CRYPTO_H
#define _GOOGLE_GPCA_CRYPTO_H

#include "gpca_keys.h"

struct gpca_crypto_op_handle;

/**
 * gpca_crypto_cb - Callback function pointer.
 *
 * @ret: Return value of operation for which callback was registered.
 * @cb_ctx: Additional context to be passed with callback.
 */
typedef void (*gpca_crypto_cb)(int ret, void *cb_ctx);

/**
 * gpca_crypto_init()
 * Initialize GPCA crypto data structures.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_init(struct gpca_dev *gpca_dev);

/**
 * gpca_crypto_deinit()
 * Uninitialize GPCA crypto data structures.
 *
 * @gpca_dev: Pointer to GPCA device struct
 *
 * Return : 0 on success, negative in case of error.
 */
void gpca_crypto_deinit(struct gpca_dev *gpca_dev);

/**
 * gpca_crypto_op_handle_alloc()
 * Allocate crypto operation handle.
 *
 * Return : allocated handle on success, NULL in case of error.
 */
struct gpca_crypto_op_handle *gpca_crypto_op_handle_alloc(void);

/**
 * gpca_crypto_op_handle_free()
 * Free crypto operation handle.
 * It must be called only after gpca_crypto_clear_op.
 *
 * @crypto_op_handle: Pointer to crypto operation handle to be freed.
 */
void gpca_crypto_op_handle_free(struct gpca_crypto_op_handle *crypto_op_handle);

/**
 * gpca_crypto_set_hash_params()
 * Set crypto parameters for SHA algorithms.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle.
 * @hash_algo: GPCA algorithm for the crypto operation.
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_set_hash_params(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *crypto_op_handle,
				enum gpca_algorithm hash_algo, gpca_crypto_cb cb,
				void *cb_ctx);

/**
 * gpca_crypto_set_hmac_params()
 * Set crypto parameters for HMAC algorithms.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle.
 * @key_handle: Key handle for the crypto operation.
 * @hmac_algo: GPCA algorithm for the crypto operation.
 * @purpose: Purpose of crypto operation i.e. Sign or Verify.
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_set_hmac_params(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *crypto_op_handle,
				struct gpca_key_handle *key_handle,
				enum gpca_algorithm hmac_algo,
				enum gpca_supported_purpose purpose,
				gpca_crypto_cb cb, void *cb_ctx);

/**
 * gpca_crypto_set_symm_algo_params()
 * Set crypto parameters for symmetric (AES) algorithms.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle.
 * @key_handle: Key handle for the crypto operation.
 * @algo: GPCA algorithm for the crypto operation.
 * @purpose: Purpose of crypto operation i.e. Encrypt or Decrypt.
 * @iv_buf: Pointer to IV buffer. NULL for ECB mode.
 * @iv_size: Size of IV in bytes.
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_set_symm_algo_params(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *crypto_op_handle,
				     struct gpca_key_handle *key_handle,
				     enum gpca_algorithm algo,
				     enum gpca_supported_purpose purpose,
				     const u8 *iv_buf, u32 iv_size,
				     gpca_crypto_cb cb, void *cb_ctx);

/**
 * gpca_crypto_set_asymm_algo_params()
 * Set crypto parameters for asymmetric (ECC/RSA) algorithms.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle.
 * @key_handle: Key handle for the crypto operation.
 * @algo: GPCA algorithm for the crypto operation.
 * @purpose: Purpose of crypto operation i.e. Sign or Verify oe Encrypt or Decrypt.
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_set_asymm_algo_params(struct gpca_dev *ctx,
				      struct gpca_crypto_op_handle *crypto_op_handle,
				      struct gpca_key_handle *key_handle,
				      enum gpca_algorithm algo,
				      enum gpca_supported_purpose purpose,
				      gpca_crypto_cb cb, void *cb_ctx);

/**
 * gpca_crypto_start_digest_op()
 * Start GPCA SHA/HMAC digest operation.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle for Digest
 * @data_addr: IOMMU VA of Data which contains Message.
 * @input_data_size: Message size in bytes.
 * @digest_addr: IOMMU VA of output buffer to get Message digest.
 * @digest_size: Size of output buffer in bytes.
 * @is_last: Is it the last data chunk for Digest?
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_start_digest_op(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *crypto_op_handle,
				dma_addr_t input_data_addr, u32 input_data_size,
				dma_addr_t digest_addr, u32 digest_size,
				bool is_last, gpca_crypto_cb cb, void *cb_ctx);

/**
 * gpca_crypto_start_symmetric_op()
 * Start GPCA AES encrypt/decrypt operation.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle for Encrypt/Decrypt
 * @data_addr: IOMMU VA of Data which contains (Unencrypted data || AAD || PT or CT || Tag)
 *             Plaintext(PT) is Input in case of encryption.
 *             Ciphertext(CT) is Input in case of decryption.
 *             AAD is only applicable for AES GCM and is optional.
 *             Unencrypted data is optional.
 *             Tag is applicable for AES GCM decrypt and is of 16 bytes.
 * @input_data_size: Input(PT or CT) data size in bytes.
 * @aad_size: AAD size in bytes.
 * @unencrypted_size: Unencrypted data sizze in bytes.
 * @output_data_addr: IOMMU VA of output buffer to get response.
 *                    Response contains (Unencrypted data || CT or PT || Tag)
 *                    Plaintext(PT) is Output in case of decryption.
 *                    Ciphertext(CT) is Output in case of encryption.
 *                    Unencrypted data is optional.
 *                    Tag is applicable for AES GCM encrypt and is of 16 bytes.
 * @output_data_size: Size of output buffer in bytes.
 * @is_last: Is it the last data chunk for Encrypt/Decrypt?
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_start_symmetric_op(struct gpca_dev *ctx,
				   struct gpca_crypto_op_handle *crypto_op_handle,
				   dma_addr_t data_addr, u32 input_data_size,
				   u32 aad_size, u32 unencrypted_size,
				   dma_addr_t output_data_addr,
				   u32 output_data_size, bool is_last,
				   gpca_crypto_cb cb, void *cb_ctx);

/**
 * gpca_crypto_start_sign_op()
 * Start GPCA ECC/RSA sign operation.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle for Sign
 * @data_addr: IOMMU VA of Data which contains Message Digest.
 * @input_data_size: Message digest size in bytes.
 * @signature_addr: IOMMU VA of output buffer to get signature.
 * @signature_size: Size of output buffer in bytes.
 * @is_last: Is it the last data chunk for Sign?
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_start_sign_op(struct gpca_dev *ctx,
			      struct gpca_crypto_op_handle *crypto_op_handle,
			      dma_addr_t data_addr, u32 input_data_size,
			      dma_addr_t signature_addr, u32 signature_size,
			      bool is_last, gpca_crypto_cb cb, void *cb_ctx);

/**
 * gpca_crypto_start_verify_op()
 * Start GPCA HMAC/ECC/RSA verify operation.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle for Verify
 * @data_addr: IOMMU VA of Data.
 *             For ECC/RSA data contains (Message Digest || Signature)
 *             For HMAC data contains (Message || HMAC Digest)
 * @input_data_size: Size of Message Digest (ECC/RSA) or Message (HMAC) in bytes
 * @signature_size: Size of signature in bytes.
 * @is_last: Is it the last data chunk for Verify?
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_start_verify_op(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *crypto_op_handle,
				dma_addr_t data_addr, u32 input_data_size,
				u32 signature_size, bool is_last,
				gpca_crypto_cb cb, void *cb_ctx);

/**
 * gpca_crypto_clear_op()
 * Clear GPCA crypto operation handle.
 *
 * @ctx: Pointer to GPCA device struct
 * @crypto_op_handle: Pointer to crypto operation handle to clear
 * @cb: Callback function pointer.
 * @cb_ctx: Additional context to be passed with callback.
 *
 * Return : 0 on success, negative in case of error.
 */
int gpca_crypto_clear_op(struct gpca_dev *ctx,
			 struct gpca_crypto_op_handle *crypto_op_handle,
			 gpca_crypto_cb cb, void *cb_ctx);

#endif /* _GOOGLE_GPCA_CRYPTO_H */
