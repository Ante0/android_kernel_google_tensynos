/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Key management driver for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022 Google LLC.
 */

#ifndef _GOOGLE_GPCA_KEYS_H
#define _GOOGLE_GPCA_KEYS_H

#include <linux/platform_device.h>
#include <linux/types.h>

#include "gpca_key_policy.h"

/* Destinations for Key send operation */
enum gpca_key_table_id {
	SOC_GPCA_KEY_TABLE = 0,
	GSA_GPCA_KEY_TABLE,
	ISE_KEY_TABLE,
};

/**
 * struct gpca_key_metadata - Key Metadata.
 * @acv: Access control identifiers validity.
 * @input_pdid: PDID for read transactions.
 * @output_pdid: PDID for write transactions.
 * @validity: Vailidity timestamp.
 */
struct gpca_key_metadata {
	bool acv;
	u8 input_pdid;
	u8 output_pdid;
	u64 validity;
};

struct gpca_key_handle;
struct gpca_dev;

/**
 * Get GPCA Device context handle.
 *
 * @pdev : Platform device handle.
 *
 * Return: GPCA device context or NULL if error.
 */
struct gpca_dev *gpca_key_get_device_context(struct platform_device *pdev);

/**
 * Generate a key in the given keyslot.
 *
 * @ctx: GPCA device context.
 * @key_handle: Key handle of generated key.
 * @key_policy: Pointer to Key Policy of the key.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_generate(struct gpca_dev *ctx, struct gpca_key_handle **key_handle,
		      const struct gpca_key_policy *key_policy);

/**
 * Derive a key from the root key.
 *
 * @ctx: GPCA device context.
 * @root_key_handle: Source Key handle used for derivation.
 * @dest_key_handle: Derived key handle.
 * @dest_key_policy: Pointer to Key Policy of the derived key.
 * @ctx_buf: Software Context Buffer pointer used for key derivation.
 * @ctx_buf_size_bytes: Software context buffer size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_derive(struct gpca_dev *ctx,
		    struct gpca_key_handle *root_key_handle,
		    struct gpca_key_handle **dest_key_handle,
		    const struct gpca_key_policy *dest_key_policy,
		    const u8 *ctx_buf, u32 ctx_buf_size_bytes);

/**
 * Wrap a key using the wrapping key.
 *
 * @ctx: GPCA device context.
 * @wrapping_key_handle: Wrapping key handle.
 * @src_key_handle: Key handle of the key being wrapped.
 * @wrapped_key_buf: Buffer pointer for storing the wrapped key.
 * @wrapped_key_buf_size: Size of wrapped_key_buf in bytes.
 * @wrapped_key_buf_size_out: Actual wrapped key size.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_wrap(struct gpca_dev *ctx,
		  struct gpca_key_handle *wrapping_key_handle,
		  struct gpca_key_handle *src_key_handle, u8 *wrapped_key_buf,
		  u32 wrapped_key_buf_size, u32 *wrapped_key_buf_size_out);

/**
 * Unwrap a key into the given keyslot.
 *
 * @ctx: GPCA device context.
 * @wrapping_key_handle: Wrapping key handle.
 * @dest_key_handle: Key handle of unwrapped key.
 * @wrapped_key_buf: Wrapped key buffer pointer.
 * @wrapped_key_buf_size: Wrapped key buffer size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_unwrap(struct gpca_dev *ctx,
		    struct gpca_key_handle *wrapping_key_handle,
		    struct gpca_key_handle **dest_key_handle,
		    const u8 *wrapped_key_buf, u32 wrapped_key_buf_size);

/**
 * Send a key to destination key table's slot.
 *
 * @ctx: GPCA device context.
 * @src_key_handle: Source key handle.
 * @dest_key_table: Destination key table (i.e GSA, ISE, etc.)
 * @dest_keyslot: Destination key table slot.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_send(struct gpca_dev *ctx, struct gpca_key_handle *src_key_handle,
		  enum gpca_key_table_id dest_key_table, u8 dest_keyslot);

/**
 * Clear key from the given keyslot.
 *
 * @ctx: GPCA device context.
 * @key_handle: Key handle of the key to be removed.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_clear(struct gpca_dev *ctx, struct gpca_key_handle **key_handle);

/**
 * Get software seed derived from source key.
 *
 * @ctx: GPCA device context.
 * @key_handle: Source key handle from which the seed is derived.
 * @label_buf: Label buffer pointer. Label is used while deriving seed.
 * @label_buf_size: Label buffer size in bytes.
 * @ctx_buf: Software context buffer pointer. It is used for deriving seed.
 * @ctx_buf_size: Software context buffer size in bytes.
 * @seed_buf: Seed buffer pointer where the derived seed will be stored.
 * @seed_buf_size: Seed buffer size in bytes.
 * @seed_buf_size_out: Actual seed size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_get_software_seed(struct gpca_dev *ctx,
			       struct gpca_key_handle *key_handle,
			       const u8 *label_buf, u32 label_buf_size,
			       const u8 *ctx_buf, u32 ctx_buf_size,
			       u8 *seed_buf, u32 seed_buf_size,
			       u32 *seed_buf_size_out);

/**
 * Import a key into the given keyslot.
 *
 * @ctx: GPCA device context.
 * @key_handle: Imported key handle.
 * @key_policy: Pointer to Key Policy of the key.
 * @key_buf: Key buffer containing the key material.
 * @key_len: Length of key in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_import(struct gpca_dev *ctx, struct gpca_key_handle **key_handle,
		    const struct gpca_key_policy *key_policy, const u8 *key_buf,
		    u32 key_len);

/**
 * Get key policy of the key in given keyslot.
 *
 * @ctx: GPCA device context.
 * @key_handle: Key handle whose key policy is to be queried.
 * @key_policy: Pointer to get key policy.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_get_policy(struct gpca_dev *ctx,
			struct gpca_key_handle *key_handle,
			struct gpca_key_policy *key_policy);

/**
 * Get key metadata.
 *
 * @ctx: GPCA device context.
 * @key_handle: Key handle whose metadata is to be retrieved.
 * @key_metadata: Pointer to struct gpca_key_metadata where the retrieved
 *                metadata is to be stored.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_get_metadata(struct gpca_dev *ctx,
			  struct gpca_key_handle *key_handle,
			  struct gpca_key_metadata *key_metadata);

/**
 * Set key metadata.
 *
 * @ctx: GPCA device context.
 * @key_handle: Key handle with which to associate the metadata.
 * @key_metadata: Pointer to key metadata which is to be set for
 *                the key in keyslot.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_set_metadata(struct gpca_dev *ctx,
			  struct gpca_key_handle *key_handle,
			  const struct gpca_key_metadata *key_metadata);

/**
 * Get public key.
 *
 * @ctx: GPCA device context.
 * @key_handle: Key handle from which public key is being queried.
 * @pub_key_buf: Buffer to get Public Key in.
 * @pub_key_buf_size: Allocated public key buffer size in bytes.
 * @pub_key_buf_size_out: Actual public key size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_get_public_key(struct gpca_dev *ctx,
			    struct gpca_key_handle *key_handle, u8 *pub_key_buf,
			    u32 pub_key_buf_size, u32 *pub_key_buf_size_out);

/**
 * Set public key.
 *
 * @ctx: GPCA device context.
 * @key_handle: Public key handle.
 * @pub_key_buf: Public key buffer.
 * @pub_key_buf_size: Public key buffer size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_set_public_key(struct gpca_dev *ctx,
			    struct gpca_key_handle **key_handle,
			    const u8 *pub_key_buf, u32 pub_key_buf_size);

/**
 * Secure key import.
 *
 * @ctx: GPCA device context.
 * @client_key_handle: Client private key handle
 * @server_pub_key_handle: Server's public key handle.
 * @dest_key_handle: Secure Import key handle.
 * @include_key_policy: Wrapped key buffer has key policy
 * @key_policy: Pointer to Provisioned key policy if include_key_policy = 1.
 * @ctx_buf: Context buffer
 * @ctx_buf_size: Context buffer size in bytes.
 * @salt_buf: Optional HKDF salt buffer
 * @salt_buf_size: Salt buffer size in bytes.
 * @wrapped_key_buf: Wrapped key buffer.
 * @wrapped_key_buf_size: Wrapped key buffer size in bytes.
 *
 * Return: 0 if success, negative number if error occurs.
 */
int gpca_key_secure_import(struct gpca_dev *ctx,
			   struct gpca_key_handle *client_key_handle,
			   struct gpca_key_handle *server_pub_key_handle,
			   struct gpca_key_handle **dest_key_handle,
			   bool include_key_policy,
			   const struct gpca_key_policy *key_policy,
			   const u8 *ctx_buf, u32 ctx_buf_size,
			   const u8 *salt_buf, u32 salt_buf_size,
			   const u8 *wrapped_key_buf, u32 wrapped_key_buf_size);

#endif /* _GOOGLE_GPCA_KEYS_H */
