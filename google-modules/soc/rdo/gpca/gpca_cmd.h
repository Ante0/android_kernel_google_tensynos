/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Driver for processing GPCA command using GPCA Hardware.
 *
 * Copyright (C) 2022 Google LLC.
 */

#ifndef _GOOGLE_GPCA_CMD_H
#define _GOOGLE_GPCA_CMD_H

#include <linux/types.h>

#include "gpca_registers.h"

struct gpca_dev;

/* Crypto operation context size in bytes */
#define GPCA_CMD_CRYPTO_OP_CTX_SIZE GPCA_OP_CTX_SIZE_BYTES

/**
 * enum  gpca_cmd_crypto_op_type - Crypto operation type.
 *
 * @GPCA_CMD_CRYPTO_OP_ENCRYPT: Encryption.
 * @GPCA_CMD_CRYPTO_OP_DECRYPT: Decryption.
 * @GPCA_CMD_CRYPTO_OP_SIGN: Sign
 * @GPCA_CMD_CRYPTO_OP_VERIFY: Verify
 */
enum gpca_cmd_crypto_op_type {
	GPCA_CMD_CRYPTO_OP_ENCRYPT = 0,
	GPCA_CMD_CRYPTO_OP_DECRYPT,
	GPCA_CMD_CRYPTO_OP_SIGN,
	GPCA_CMD_CRYPTO_OP_VERIFY,
};

/**
 * enum gpca_cmd_crypto_algo - Supported Crypto algorithms.
 *
 * The cryptographic algorithms supported by GPCA.
 * It is a subset of algorithms supported in key policy.
 * The mapping of algorithms is as per GKMI Architecture
 * Specification.
 */
enum gpca_cmd_crypto_algo {
	GPCA_CMD_CRYPTO_ALGO_AES128_ECB = 0,
	GPCA_CMD_CRYPTO_ALGO_AES128_CBC = 1,
	GPCA_CMD_CRYPTO_ALGO_AES128_CTR = 2,
	GPCA_CMD_CRYPTO_ALGO_AES128_GCM = 3,
	GPCA_CMD_CRYPTO_ALGO_AES192_ECB = 5,
	GPCA_CMD_CRYPTO_ALGO_AES192_CBC = 6,
	GPCA_CMD_CRYPTO_ALGO_AES192_CTR = 7,
	GPCA_CMD_CRYPTO_ALGO_AES192_GCM = 8,
	GPCA_CMD_CRYPTO_ALGO_AES256_ECB = 10,
	GPCA_CMD_CRYPTO_ALGO_AES256_CBC = 11,
	GPCA_CMD_CRYPTO_ALGO_AES256_CTR = 12,
	GPCA_CMD_CRYPTO_ALGO_AES256_GCM = 13,
	GPCA_CMD_CRYPTO_ALGO_TDES_ECB = 17,
	GPCA_CMD_CRYPTO_ALGO_TDES_CBC = 18,
	GPCA_CMD_CRYPTO_ALGO_HMAC_SHA2_224 = 19,
	GPCA_CMD_CRYPTO_ALGO_HMAC_SHA2_256 = 20,
	GPCA_CMD_CRYPTO_ALGO_HMAC_SHA2_384 = 21,
	GPCA_CMD_CRYPTO_ALGO_HMAC_SHA2_512 = 22,
	GPCA_CMD_CRYPTO_ALGO_RSA_2048 = 25,
	GPCA_CMD_CRYPTO_ALGO_RSA_3072 = 26,
	GPCA_CMD_CRYPTO_ALGO_RSA_4096 = 27,
	GPCA_CMD_CRYPTO_ALGO_ECC_P224 = 28,
	GPCA_CMD_CRYPTO_ALGO_ECC_P256 = 29,
	GPCA_CMD_CRYPTO_ALGO_ECC_P384 = 30,
	GPCA_CMD_CRYPTO_ALGO_ECC_P521 = 31,
	GPCA_CMD_CRYPTO_ALGO_ECC_ED25519 = 32,
	GPCA_CMD_CRYPTO_ALGO_SHA2_224 = 33,
	GPCA_CMD_CRYPTO_ALGO_SHA2_256 = 34,
	GPCA_CMD_CRYPTO_ALGO_SHA2_384 = 35,
	GPCA_CMD_CRYPTO_ALGO_SHA2_512 = 36,
};

/**
 * Initialize GPCA command processing
 *
 * @gpca_dev: Pointer to the GPCA device struct
 *
 * Return: 0 if success, negative number if initialization fails.
 */
int gpca_cmd_init(struct gpca_dev *gpca_dev);

/**
 * Generate a key in the given keyslot.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Key slot of generated key.
 * @key_policy: Raw Key Policy of the key.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_generate(struct gpca_dev *gpca_dev, u8 key_slot,
			  u64 key_policy);

/**
 * Derive a key from the root key.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @root_key_slot: Source Key slot used for derivation.
 * @dest_key_slot: Derived key slot.
 * @dest_key_policy: Raw Key Policy of the derived key.
 * @ctx_buf: Software Context Buffer pointer used for key derivation.
 * @ctx_buf_size_bytes: Software context buffer size in bytes.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_derive(struct gpca_dev *gpca_dev, u8 root_key_slot,
			u8 dest_key_slot, u64 dest_key_policy,
			const u8 *ctx_buf, u32 ctx_buf_size_bytes);

/**
 * Wrap a key using the wrapping key.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @wrapping_key_slot: Wrapping key slot
 * @src_key_slot: Key slot of the key being wrapped.
 * @wrapped_key_buf: Buffer pointer for storing the wrapped key.
 * @wrapped_key_buf_size: Size of wrapped_key_buf in bytes.
 * @wrapped_key_buf_size_out: Actual wrapped key size.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_wrap(struct gpca_dev *gpca_dev, u8 wrapping_key_slot,
		      u8 src_key_slot, u8 *wrapped_key_buf,
		      u32 wrapped_key_buf_size, u32 *wrapped_key_buf_size_out);

/**
 * Unwrap a key into the given keyslot.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @wrapping_key_slot: Wrapping key slot.
 * @dest_key_slot: Key slot of unwrapped key.
 * @wrapped_key_buf: Wrapped key buffer pointer.
 * @wrapped_key_buf_size: Wrapped key buffer size in bytes.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_unwrap(struct gpca_dev *gpca_dev, u8 wrapping_key_slot,
			u8 dest_key_slot, const u8 *wrapped_key_buf,
			u32 wrapped_key_buf_size);

/**
 * Send a key to destination key table's slot.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @src_key_slot: Source key slot.
 * @dest_key_table: Destination key table (i.e GSA, ISE, etc.)
 * @dest_key_slot: Destination key table slot.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_send(struct gpca_dev *gpca_dev, u8 src_key_slot,
		      u32 dest_key_table, u8 dest_key_slot);

/**
 * Clear key from the given keyslot.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Key slot of the key to be removed.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_clear(struct gpca_dev *gpca_dev, u8 key_slot);

/**
 * Get software seed derived from source key.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Source key slot from which the seed is derived.
 * @label_buf: Label buffer pointer. Label is used while deriving seed.
 * @label_buf_size: Label buffer size in bytes.
 * @ctx_buf: Software context buffer pointer. It is used for deriving seed.
 * @ctx_buf_size: Software context buffer size in bytes.
 * @seed_buf: Seed buffer pointer where the derived seed will be stored.
 * @seed_buf_size: Seed buffer size in bytes.
 * @seed_buf_size_out: Actual seed size in bytes.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_get_software_seed(struct gpca_dev *gpca_dev, u8 key_slot,
			       const u8 *label_buf, u32 label_buf_size,
			       const u8 *ctx_buf, u32 ctx_buf_size,
			       u8 *seed_buf, u32 seed_buf_size,
			       u32 *seed_buf_size_out);

/**
 * Import a key into the given keyslot.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Imported key slot.
 * @key_policy: Raw Key Policy of the key.
 * @key_buf: Key buffer containing the key material.
 * @key_len: Length of key in bytes.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_import(struct gpca_dev *gpca_dev, u8 key_slot, u64 key_policy,
			const u8 *key_buf, u32 key_len);

/**
 * Get key policy of the key in given keyslot.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Key slot whose key policy is to be queried.
 * @key_policy: Pointer to get key policy.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_get_key_policy(struct gpca_dev *gpca_dev, u8 key_slot,
			    u64 *key_policy);

/**
 * Get key metadata.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Key slot whose metadata is to be retrieved.
 * @acv: Pointer to get ACV for the keyslot.
 * @input_pdid: Pointer to get Input PDID for keyslot.
 * @output_pdid: Pointer to get Output PDID for keyslot.
 * @validity: Pointer to get validity of keyslot.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_get_key_metadata(struct gpca_dev *gpca_dev, u8 key_slot, bool *acv,
			      u8 *input_pdid, u8 *output_pdid, u64 *validity);

/**
 * Set key metadata.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Key slot with which to associate the metadata.
 * @acv: Pointer to get ACV for the keyslot.
 * @input_pdid: Pointer to get Input PDID for keyslot.
 * @output_pdid: Pointer to get Output PDID for keyslot.
 * @validity: Pointer to get validity of keyslot.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_set_key_metadata(struct gpca_dev *gpca_dev, u8 key_slot, bool acv,
			      u8 input_pdid, u8 output_pdid, u64 validity);

/**
 * Get public key.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Key slot from which public key is being queried.
 * @pub_key_buf: Buffer to get Public Key in.
 * @pub_key_buf_size: Allocated public key buffer size in bytes.
 * @pub_key_buf_size_out: Actual public key size in bytes.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_get_public_key(struct gpca_dev *gpca_dev, u8 key_slot,
			    u8 *pub_key_buf, u32 pub_key_buf_size,
			    u32 *pub_key_buf_size_out);

/**
 * Set public key.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @key_slot: Public key slot.
 * @pub_key_buf: Public key buffer.
 * @pub_key_buf_size: Public key buffer size in bytes.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_set_public_key(struct gpca_dev *gpca_dev, u8 key_slot,
			    const u8 *pub_key_buf, u32 pub_key_buf_size);

/**
 * Secure key import.
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @client_key_slot: Client private key slot.
 * @server_pub_key_slot: Server's public key slot.
 * @dest_key_slot: Secure Imported key slot.
 * @salt_present: HKDF Salt present.
 * @include_key_policy: Wrapped key buffer has key policy
 * @key_policy: Provisioned key policy if include_key_policy = 1.
 * @ctx_buf: Context buffer
 * @ctx_buf_size: Context buffer size in bytes.
 * @salt_buf: Salt buffer
 * @salt_buf_size: Salt buffer size in bytes.
 * @wrapped_key_buf: Wrapped key buffer.
 * @wrapped_key_buf_size: Wrapped key buffer size in bytes.
 *
 * Return: 0 if success, negative number if sending error occurs.
 */
int gpca_cmd_key_secure_import(struct gpca_dev *gpca_dev, u8 client_key_slot,
			       u8 server_pub_key_slot, u8 dest_key_slot,
			       bool salt_present, bool include_key_policy,
			       u64 key_policy, const u8 *ctx_buf,
			       u32 ctx_buf_size, const u8 *salt_buf,
			       u32 salt_buf_size, const u8 *wrapped_key_buf,
			       u32 wrapped_key_buf_size);

/**
 * Set Crypto Hash algorithm Params
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot for the crypto operation.
 * @gpca_algo: GPCA algorithm for the crypto operation.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_set_crypto_hash_params(struct gpca_dev *gpca_dev, u8 opslot,
				    enum gpca_cmd_crypto_algo gpca_algo);

/**
 * Set Crypto HMAC algorithm Params
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot for the crypto operation.
 * @keyslot: Key slot for the crypto operation.
 * @gpca_algo: GPCA algorithm for the crypto operation.
 * @op_type: HMAC sign or verify.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_set_crypto_hmac_params(struct gpca_dev *gpca_dev, u8 opslot,
				    u8 keyslot,
				    enum gpca_cmd_crypto_algo gpca_algo,
				    enum gpca_cmd_crypto_op_type op_type);

/**
 * Set Crypto Symmetric algorithm (AES/TDES) Params
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot for the crypto operation.
 * @keyslot: Key slot for the crypto operation.
 * @gpca_algo: GPCA algorithm for the crypto operation.
 * @op_type: Encrypt or Decrypt.
 * @iv_buf: IV buffer for AES/TDES.
 * @iv_size: Length of IV buffer in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_set_crypto_symm_algo_params(struct gpca_dev *gpca_dev, u8 opslot,
					 u8 keyslot,
					 enum gpca_cmd_crypto_algo gpca_algo,
					 enum gpca_cmd_crypto_op_type op_type,
					 const u8 *iv_buf, u32 iv_size);

/**
 * Set Crypto Asymmetric algorithm (ECC/RSA) Params
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot for the crypto operation.
 * @keyslot: Key slot for the crypto operation.
 * @gpca_algo: GPCA algorithm for the crypto operation.
 * @op_type: ECC(sign or verify), RSA(encrypt, decrypt, sign, verify)
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_set_crypto_asymm_algo_params(struct gpca_dev *gpca_dev, u8 opslot,
					  u8 keyslot,
					  enum gpca_cmd_crypto_algo gpca_algo,
					  enum gpca_cmd_crypto_op_type op_type);

/**
 * Start Crypto Operation (Pointers)
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot for the crypto operation.
 * @data_addr: IOMMU VA of input buffer for operation. May contain AAD,
 *             Signature or Unecrypted data based on operation.
 *             Data buffer contents vary based on algorithm & operation:
 *             AES GCM : unencrypted || aad || plaintext/ciphertext with tag
 *             Encrypt/decrypt AES, TDES, RSA : unencrypted || plaintext/ciphertext
 *             SHA/HMAC/ECC/RSA Sign : Message(SHA/HMAC) or message digest(ECC/RSA)
 *             SHA/HMAC/ECC/RSA Verify : Message(SHA/HMAC) or message digest(ECC/RSA) || signature
 * @input_data_size: Size of input data in bytes.
 * @aad_size: AAD size in bytes.
 * @signature_size: Signature size in bytes.
 * @unencrypted_size: Unencrypted data size in bytes.
 * @output_data_addr: IOMMU VA of Output buffer for operation.
 *                    Output buffer contents vary based on algorithm & operation:
 *                    AES GCM : unencrypted || aad || ciphertext with tag/plaintext
 *                    Encrypt/decrypt AES, TDES, RSA : unencrypted || ciphertext/plaintext
 *                    SHA/HMAC/ECC/RSA Sign : Signature (FWD=0) or Input || Signature (FWD=1)
 *                    SHA/HMAC/ECC/RSA Verify : Empty (FWD=0) or Input (FWD=1)
 * @output_data_size: Size of output data in bytes.
 * @fwd_input: Forward input or not?
 * @is_last: Is it the last command for crypto operation?
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_start_crypto_op_ptrs(struct gpca_dev *gpca_dev, u8 opslot,
				  dma_addr_t data_addr, u32 input_data_size,
				  u32 aad_size, u32 signature_size,
				  u32 unencrypted_size,
				  dma_addr_t output_data_addr,
				  u32 output_data_size, bool fwd_input,
				  bool is_last);
/**
 * Start Crypto Op (Data)
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot for the crypto operation.
 * @input_buf: Input buffer for the crypto operation.
 * @input_size: Input buffer size in bytes.
 * @aad_buf: AAD buffer for the crypto operation.
 * @aad_size: AAD buffer size in bytes.
 * @output_buf: Output buffer for the crypto operation.
 * @output_size: Allocated output buffer size in bytes.
 * @output_size_out: Actual output buffer size in bytes.
 * @is_last: Is it the last command for crypto operation?
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_start_crypto_op_data(struct gpca_dev *gpca_dev, u8 opslot,
				  const u8 *input_buf, u32 input_size,
				  const u8 *aad_buf, u32 aad_size,
				  u8 *output_buf, u32 output_size,
				  u32 *output_size_out, bool is_last);
/**
 * Get Operation Context
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot whose context is to be retrieved.
 * @op_ctx_addr: IOMMU VA of buffer into which context is to be populated.
 * @op_ctx_size: Operation context size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_get_op_context(struct gpca_dev *gpca_dev, u8 opslot,
			    dma_addr_t op_ctx_addr, u32 op_ctx_size);
/**
 * Set Operation Context
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot into which context is to be set.
 * @op_ctx_addr: IOMMU VA of Operation context buffer.
 * @op_ctx_size: Operation context size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_set_op_context(struct gpca_dev *gpca_dev, u8 opslot,
			    dma_addr_t op_ctx_addr, u32 op_ctx_size);
/**
 * Clear operation slot
 *
 * @gpca_dev: Pointer to the GPCA device struct
 * @opslot: Operation slot to be cleared.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_cmd_clear_opslot(struct gpca_dev *gpca_dev, u8 opslot);

#endif /* _GOOGLE_GPCA_CMD_H */
