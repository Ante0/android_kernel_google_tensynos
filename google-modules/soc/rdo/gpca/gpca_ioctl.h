/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IOCTL header for the GPCA (General Purpose Crypto Accelerator).
 *
 * Copyright (C) 2022 Google LLC.
 */
#ifndef __GPCA_IOCTL_H__
#define __GPCA_IOCTL_H__

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/types.h>

#include "gpca_keys.h"
#include "gpca_key_policy.h"

#define GPCA_MAGIC 'g'
#define GPCA_IOW(_n, _t) _IOW(GPCA_MAGIC, _n, _t)
#define GPCA_IOWR(_n, _t) _IOWR(GPCA_MAGIC, _n, _t)

#define GPCA_KEY_GENERATE_IOCTL GPCA_IOW(0x40, struct gpca_key_generate_ioctl)
#define GPCA_KEY_DERIVE_IOCTL GPCA_IOW(0x41, struct gpca_key_derive_ioctl)
#define GPCA_KEY_WRAP_IOCTL GPCA_IOWR(0x42, struct gpca_key_wrap_ioctl)
#define GPCA_KEY_UNWRAP_IOCTL GPCA_IOW(0x43, struct gpca_key_unwrap_ioctl)
#define GPCA_KEY_SEND_IOCTL GPCA_IOW(0x44, struct gpca_key_send_ioctl)
#define GPCA_KEY_CLEAR_IOCTL GPCA_IOW(0x45, struct gpca_key_clear_ioctl)
#define GPCA_GET_SOFTWARE_SEED_IOCTL \
	GPCA_IOWR(0x46, struct gpca_get_software_seed_ioctl)
#define GPCA_KEY_IMPORT_IOCTL GPCA_IOW(0x47, struct gpca_key_import_ioctl)
#define GPCA_GET_KEY_POLICY_IOCTL \
	GPCA_IOWR(0x48, struct gpca_get_key_policy_ioctl)
#define GPCA_GET_KEY_METADATA_IOCTL \
	GPCA_IOWR(0x49, struct gpca_get_key_metadata_ioctl)
#define GPCA_SET_KEY_METADATA_IOCTL \
	GPCA_IOW(0x4a, struct gpca_set_key_metadata_ioctl)
#define GPCA_GET_PUBLIC_KEY_IOCTL \
	GPCA_IOWR(0x4b, struct gpca_get_public_key_ioctl)
#define GPCA_SET_PUBLIC_KEY_IOCTL \
	GPCA_IOW(0x4c, struct gpca_set_public_key_ioctl)
#define GPCA_SECURE_IMPORT_KEY_IOCTL \
	GPCA_IOW(0x4d, struct gpca_secure_import_key_ioctl)

/**
 * struct gpca_key_generate_ioctl - Key Generation IOCTL.
 * @keyslot: Keyslot where the generated key is to be stored.
 * @kp: Key policy of the key to be generated.
 */
struct gpca_key_generate_ioctl {
	u8 keyslot;
	struct gpca_key_policy kp;
};

/**
 * struct gpca_key_derive_ioctl - Key Derivation IOCTL.
 * @root_keyslot: Source keyslot from which the key is to be derived..
 * @dest_keyslot: Destination keyslot where the derived key is to be stored.
 * @dest_key_policy: Destination key's key policy.
 * @ctx_buf: Key derivation context buffer.
 * @ctx_buf_size_bytes: Context buffer size in bytes.
 */
struct gpca_key_derive_ioctl {
	u8 root_keyslot;
	u8 dest_keyslot;
	struct gpca_key_policy dest_key_policy;
	u8 *ctx_buf;
	u32 ctx_buf_size_bytes;
};

/**
 * struct gpca_key_clear_ioctl - Key Clear IOCTL.
 * @keyslot: Keyslot of the key to be cleared.
 */
struct gpca_key_clear_ioctl {
	u8 keyslot;
};

/**
 * struct gpca_key_wrap_ioctl - Key Wrap IOCTL.
 * @wrapping_keyslot: Wrapping key's slot used to wrap the source key.
 * @src_keyslot: Source keyslot which has the key to be wrapped.
 * @wrapped_key_buf: Wrapped key buffer where the wrapped key is to be stored.
 * @wrapped_key_buf_size: Allocated Wrapped key buffer size in bytes.
 * @wrapped_key_buf_size_out: Actual wrapped key size in bytes.
 */
struct gpca_key_wrap_ioctl {
	u8 wrapping_keyslot;
	u8 src_keyslot;
	u8 *wrapped_key_buf;
	u32 wrapped_key_buf_size;
	u32 wrapped_key_buf_size_out;
};

/**
 * struct gpca_key_unwrap_ioctl - Key Unwrap IOCTL.
 * @wrapping_keyslot: Wrapping key's slot used to unwrap the wrapped buffer.
 * @dest_keyslot: Destination keyslot where the unwrapped key is to be stored.
 * @wrapped_key_buf: Wrapped key buffer.
 * @wrapped_key_buf_size: Wrapped key buffer size in bytes.
 */
struct gpca_key_unwrap_ioctl {
	u8 wrapping_keyslot;
	u8 dest_keyslot;
	u8 *wrapped_key_buf;
	u32 wrapped_key_buf_size;
};

/**
 * struct gpca_key_send_ioctl - Key Send IOCTL.
 * @src_keyslot: Source keyslot from which the key is to be sent.
 * @dest_key_table: Destination key table.
 * @dest_keyslot: Destination keyslot where the key is to be sent.
 */
struct gpca_key_send_ioctl {
	u8 src_keyslot;
	enum gpca_key_table_id dest_key_table;
	u8 dest_keyslot;
};

/**
 * struct gpca_get_software_seed_ioctl - Get Software Seed IOCTL.
 * @keyslot: Source keyslot from which seed is to be derived.
 * @label_buf: Label buffer used for seed derivation.
 * @label_buf_size: label buffer size in bytes.
 * @ctx_buf: Context buffer used for seed derivation.
 * @ctx_buf_size: Context buffer size in bytes.
 * @seed_buf: Buffer where the generated seed is to be stored.
 * @seed_buf_size: Allocated Seed buffer size in bytes.
 * @seed_buf_size_out: Actual seed size in bytes.
 */
struct gpca_get_software_seed_ioctl {
	u8 keyslot;
	u8 *label_buf;
	u32 label_buf_size;
	u8 *ctx_buf;
	u32 ctx_buf_size;
	u8 *seed_buf;
	u32 seed_buf_size;
	u32 seed_buf_size_out;
};

/**
 * struct gpca_key_import_ioctl - Import key IOCTL.
 * @keyslot: Keyslot where the key is to be stored.
 * @key_policy: Key Policy of the key to be imported.
 * @key_buf: Key buffer containing the key material.
 * @key_len: Length of key in bytes.
 */
struct gpca_key_import_ioctl {
	u8 keyslot;
	struct gpca_key_policy key_policy;
	u8 *key_buf;
	u32 key_len;
};

/**
 * struct gpca_get_key_policy_ioctl - Get key policy IOCTL.
 * @keyslot: Keyslot whose key policy is being queried.
 * @key_policy: Key Policy of the key.
 */
struct gpca_get_key_policy_ioctl {
	u8 keyslot;
	struct gpca_key_policy key_policy;
};

/**
 * struct gpca_get_key_metadata_ioctl - Get key metadata IOCTL.
 * @keyslot: Keyslot whose key metadata is being queried.
 * @key_metadata: Key Metadata retrieved from GPCA.
 */
struct gpca_get_key_metadata_ioctl {
	u8 keyslot;
	struct key_metadata key_metadata;
};

/**
 * struct gpca_set_key_metadata_ioctl - Set key metadata IOCTL.
 * @keyslot: Keyslot whose key metadata is to be set.
 * @key_metadata: Key Metadata to be set.
 */
struct gpca_set_key_metadata_ioctl {
	u8 keyslot;
	struct key_metadata key_metadata;
};

/**
 * struct gpca_get_public_key_ioctl - Get Public key IOCTL.
 * @keyslot: Keyslot from which to get the public key.
 * @public_key_buf: Pointer to buffer where the retrieved public key
 *                  is to be stored.
 * @public_key_buf_size: Allocated public key buffer size in bytes.
 * @public_key_buf_size_out: Actual public key size in bytes.
 */
struct gpca_get_public_key_ioctl {
	u8 keyslot;
	u8 *public_key_buf;
	u32 public_key_buf_size;
	u32 public_key_buf_size_out;
};

/**
 * struct gpca_set_public_key_ioctl - Set public key IOCTL.
 * @keyslot: Keyslot in which to put the public key.
 * @public_key_buf: Public key buffer pointer.
 * @public_key_buf_size: Public key buffer size in bytes.
 */
struct gpca_set_public_key_ioctl {
	u8 keyslot;
	u8 *public_key_buf;
	u32 public_key_buf_size;
};

/**
 * struct gpca_secure_import_key_ioctl - Secure import key IOCTL.
 * @client_keyslot: Client private key slot
 * @server_pub_keyslot: Server's public key slot.
 * @dest_keyslot: Slot where the imported key is to be stored.
 * @salt_present: Is HKDF Salt present?
 * @include_key_policy: Does Wrapped key buffer have key policy?
 * @key_policy: Provisioned key policy / Wrapping key policy
 *              based on include_key_policy = 0 / 1 respectively.
 * @ctx_buf: Context buffer
 * @ctx_buf_size: Context buffer size in bytes.
 * @salt_buf: Salt buffer
 * @salt_buf_size: Salt buffer size in bytes.
 * @wrapped_key_buf: Wrapped key buffer.
 * @wrapped_key_buf_size: Wrapped key buffer size in bytes.
 *
 */
struct gpca_secure_import_key_ioctl {
	u8 client_keyslot;
	u8 server_pub_keyslot;
	u8 dest_keyslot;
	bool salt_present;
	bool include_key_policy;
	struct gpca_key_policy key_policy;
	u8 *ctx_buf;
	u32 ctx_buf_size;
	u8 *salt_buf;
	u32 salt_buf_size;
	u8 *wrapped_key_buf;
	u32 wrapped_key_buf_size;
};

/**
 * Process GPCA IOCTLs.
 *
 * @file: Device file pointer
 * @cmd: IOCTL command
 * @arg: IOCTL arguments
 *
 * Return: 0 if success, negative number if error occurs.
 */
long gpca_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif /* __GPCA_IOCTL_H__ */
