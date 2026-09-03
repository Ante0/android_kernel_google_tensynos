// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022 Google LLC.
 */

#include "gpca_ioctl.h"

#include <linux/err.h>
#include <linux/uaccess.h>

/* GPCA Wrapped key max size : 64b key policy || 10240b key || 96b metadata */
#define MAX_WRAPPED_KEY_SIZE_BYTES 1300
#define MAX_LABEL_SIZE_BYTES 32
#define MAX_CONTEXT_SIZE_BYTES 32
#define MAX_SEED_SIZE_BYTES 64
/* Largest key = RSA key 10240b = 1280B */
#define MAX_KEY_SIZE_BYTES 1280

long gpca_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	union key_cmds {
		struct gpca_key_generate_ioctl key_gen;
		struct gpca_key_derive_ioctl key_derive;
		struct gpca_key_clear_ioctl key_clear;
		struct gpca_key_wrap_ioctl key_wrap;
		struct gpca_key_unwrap_ioctl key_unwrap;
		struct gpca_key_send_ioctl key_send;
		struct gpca_get_software_seed_ioctl get_seed;
		struct gpca_key_import_ioctl key_import;
		struct gpca_get_key_policy_ioctl get_kp;
		struct gpca_get_key_metadata_ioctl get_metadata;
		struct gpca_set_key_metadata_ioctl set_metadata;
		struct gpca_get_public_key_ioctl get_pubkey;
		struct gpca_set_public_key_ioctl set_pubkey;
		struct gpca_secure_import_key_ioctl secure_key_import;
	} key_cmd;
	union {
		u8 wrapped_key_buf[MAX_WRAPPED_KEY_SIZE_BYTES];
		u8 plain_key_buf[MAX_WRAPPED_KEY_SIZE_BYTES];
	} key_buf;
	u32 key_buf_size = 0;

	u8 label_buf[MAX_LABEL_SIZE_BYTES] = { 0 };
	u8 ctx_buf[MAX_CONTEXT_SIZE_BYTES] = { 0 };
	u8 seed_buf[MAX_SEED_SIZE_BYTES] = { 0 };
	u32 seed_buf_size = 0;
	long retcode = 0;

	switch (cmd) {
	case GPCA_KEY_GENERATE_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.key_gen, (int32_t *)arg,
				   sizeof(key_cmd.key_gen)))
			return -EFAULT;

		retcode = gpca_key_generate(key_cmd.key_gen.keyslot,
					    &key_cmd.key_gen.kp);
		break;
	case GPCA_KEY_DERIVE_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.key_derive, (int32_t *)arg,
				   sizeof(key_cmd.key_derive)))
			return -EFAULT;

		if (key_cmd.key_derive.ctx_buf_size_bytes > sizeof(ctx_buf))
			return -EINVAL;
		if (copy_from_user(ctx_buf, key_cmd.key_derive.ctx_buf,
				   key_cmd.key_derive.ctx_buf_size_bytes))
			return -EFAULT;
		retcode = gpca_key_derive(
			key_cmd.key_derive.root_keyslot,
			key_cmd.key_derive.dest_keyslot,
			&key_cmd.key_derive.dest_key_policy, ctx_buf,
			key_cmd.key_derive.ctx_buf_size_bytes);
		break;
	case GPCA_KEY_CLEAR_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.key_clear, (int32_t *)arg,
				   sizeof(key_cmd.key_clear)))
			return -EFAULT;

		retcode = gpca_key_clear(key_cmd.key_clear.keyslot);
		break;
	case GPCA_KEY_WRAP_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.key_wrap, (int32_t *)arg,
				   sizeof(key_cmd.key_wrap)))
			return -EFAULT;

		retcode = gpca_key_wrap(key_cmd.key_wrap.wrapping_keyslot,
					key_cmd.key_wrap.src_keyslot,
					key_buf.wrapped_key_buf,
					sizeof(key_buf.wrapped_key_buf),
					&key_buf_size);
		if (retcode == 0) {
			if (key_cmd.key_wrap.wrapped_key_buf_size <
			    key_buf_size)
				return -EINVAL;
			if (copy_to_user(key_cmd.key_wrap.wrapped_key_buf,
					 key_buf.wrapped_key_buf, key_buf_size))
				return -EFAULT;
			key_cmd.key_wrap.wrapped_key_buf_size_out =
				key_buf_size;
		}

		if (copy_to_user((int32_t *)arg, &key_cmd.key_wrap,
				 sizeof(key_cmd.key_wrap)))
			return -EFAULT;
		break;
	case GPCA_KEY_UNWRAP_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.key_unwrap, (int32_t *)arg,
				   sizeof(key_cmd.key_unwrap)))
			return -EFAULT;

		if (key_cmd.key_unwrap.wrapped_key_buf_size >
		    sizeof(key_buf.wrapped_key_buf))
			return -EINVAL;
		if (copy_from_user(key_buf.wrapped_key_buf,
				   key_cmd.key_unwrap.wrapped_key_buf,
				   key_cmd.key_unwrap.wrapped_key_buf_size))
			return -EFAULT;
		retcode = gpca_key_unwrap(
			key_cmd.key_unwrap.wrapping_keyslot,
			key_cmd.key_unwrap.dest_keyslot,
			key_buf.wrapped_key_buf,
			key_cmd.key_unwrap.wrapped_key_buf_size);
		break;
	case GPCA_KEY_SEND_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.key_send, (int32_t *)arg,
				   sizeof(key_cmd.key_send)))
			return -EFAULT;

		retcode = gpca_key_send(key_cmd.key_clear.keyslot,
					key_cmd.key_send.dest_key_table,
					key_cmd.key_send.dest_keyslot);
		break;
	case GPCA_GET_SOFTWARE_SEED_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.get_seed, (int32_t *)arg,
				   sizeof(key_cmd.get_seed)))
			return -EFAULT;

		if (key_cmd.get_seed.label_buf_size > sizeof(label_buf))
			return -EINVAL;
		if (copy_from_user(label_buf, key_cmd.get_seed.label_buf,
				   key_cmd.get_seed.label_buf_size))
			return -EFAULT;

		if (key_cmd.get_seed.ctx_buf_size > sizeof(ctx_buf))
			return -EINVAL;
		if (copy_from_user(ctx_buf, key_cmd.get_seed.ctx_buf,
				   key_cmd.get_seed.ctx_buf_size))
			return -EFAULT;

		if (key_cmd.get_seed.seed_buf_size > sizeof(seed_buf))
			return -EINVAL;

		retcode = gpca_get_software_seed(
			key_cmd.get_seed.keyslot, label_buf,
			key_cmd.get_seed.label_buf_size, ctx_buf,
			key_cmd.get_seed.ctx_buf_size, seed_buf,
			key_cmd.get_seed.seed_buf_size, &seed_buf_size);

		if (retcode == 0) {
			if (seed_buf_size > key_cmd.get_seed.seed_buf_size)
				return -EINVAL;
			if (copy_to_user(key_cmd.get_seed.seed_buf, seed_buf,
					 seed_buf_size))
				return -EFAULT;
			key_cmd.get_seed.seed_buf_size_out = seed_buf_size;
		}

		if (copy_to_user((int32_t *)arg, &key_cmd.get_seed,
				 sizeof(key_cmd.get_seed)))
			return -EFAULT;
		break;
	case GPCA_KEY_IMPORT_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.key_import, (int32_t *)arg,
				   sizeof(key_cmd.key_import)))
			return -EFAULT;
		if (key_cmd.key_import.key_len > sizeof(key_buf.plain_key_buf))
			return -EINVAL;
		if (copy_from_user(key_buf.plain_key_buf,
				   key_cmd.key_import.key_buf,
				   key_cmd.key_import.key_len))
			return -EFAULT;

		retcode = gpca_key_import(key_cmd.key_import.keyslot,
					  &key_cmd.key_import.key_policy,
					  key_buf.plain_key_buf,
					  key_cmd.key_import.key_len);
		break;
	case GPCA_GET_KEY_POLICY_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.get_kp, (int32_t *)arg,
				   sizeof(key_cmd.get_kp)))
			return -EFAULT;
		retcode = gpca_get_key_policy(key_cmd.get_kp.keyslot,
					      &key_cmd.get_kp.key_policy);
		if (copy_to_user((int32_t *)arg, &key_cmd.get_kp,
				 sizeof(key_cmd.get_kp)))
			return -EFAULT;
		break;
	case GPCA_GET_KEY_METADATA_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.get_metadata, (int32_t *)arg,
				   sizeof(key_cmd.get_metadata)))
			return -EFAULT;
		retcode = gpca_get_key_metadata(
			key_cmd.get_metadata.keyslot,
			&key_cmd.get_metadata.key_metadata);
		if (copy_to_user((int32_t *)arg, &key_cmd.get_metadata,
				 sizeof(key_cmd.get_metadata)))
			return -EFAULT;
		break;
	case GPCA_SET_KEY_METADATA_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.set_metadata, (int32_t *)arg,
				   sizeof(key_cmd.set_metadata)))
			return -EFAULT;
		retcode = gpca_set_key_metadata(
			key_cmd.set_metadata.keyslot,
			&key_cmd.set_metadata.key_metadata);
		break;
	case GPCA_GET_PUBLIC_KEY_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.get_pubkey, (int32_t *)arg,
				   sizeof(key_cmd.get_pubkey)))
			return -EFAULT;
		retcode = gpca_get_public_key(key_cmd.get_pubkey.keyslot,
					      key_buf.plain_key_buf,
					      sizeof(key_buf.plain_key_buf),
					      &key_buf_size);
		if (retcode == 0) {
			if (key_buf_size >
			    key_cmd.get_pubkey.public_key_buf_size)
				return -EINVAL;
			if (copy_to_user(key_cmd.get_pubkey.public_key_buf,
					 key_buf.plain_key_buf, key_buf_size))
				return -EFAULT;
			key_cmd.get_pubkey.public_key_buf_size_out =
				key_buf_size;
		}

		if (copy_to_user((int32_t *)arg, &key_cmd.get_pubkey,
				 sizeof(key_cmd.get_pubkey)))
			return -EFAULT;
		break;
	case GPCA_SET_PUBLIC_KEY_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.set_pubkey, (int32_t *)arg,
				   sizeof(key_cmd.set_pubkey)))
			return -EFAULT;
		if (key_cmd.set_pubkey.public_key_buf_size >
		    sizeof(key_buf.plain_key_buf))
			return -EINVAL;
		if (copy_from_user(key_buf.plain_key_buf,
				   key_cmd.set_pubkey.public_key_buf,
				   key_cmd.set_pubkey.public_key_buf_size))
			return -EFAULT;
		retcode = gpca_set_public_key(
			key_cmd.set_pubkey.keyslot, key_buf.plain_key_buf,
			key_cmd.set_pubkey.public_key_buf_size);
		break;
	case GPCA_SECURE_IMPORT_KEY_IOCTL:
		if (!arg)
			return -EINVAL;
		if (copy_from_user(&key_cmd.secure_key_import, (int32_t *)arg,
				   sizeof(key_cmd.secure_key_import)))
			return -EFAULT;

		if (key_cmd.secure_key_import.salt_buf_size > sizeof(label_buf))
			return -EINVAL;
		if (copy_from_user(label_buf,
				   key_cmd.secure_key_import.salt_buf,
				   key_cmd.secure_key_import.salt_buf_size))
			return -EFAULT;

		if (key_cmd.secure_key_import.ctx_buf_size > sizeof(ctx_buf))
			return -EINVAL;
		if (copy_from_user(ctx_buf, key_cmd.secure_key_import.ctx_buf,
				   key_cmd.secure_key_import.ctx_buf_size))
			return -EFAULT;

		if (key_cmd.secure_key_import.wrapped_key_buf_size >
		    sizeof(key_buf.wrapped_key_buf))
			return -EINVAL;
		if (copy_from_user(
			    key_buf.wrapped_key_buf,
			    key_cmd.secure_key_import.wrapped_key_buf,
			    key_cmd.secure_key_import.wrapped_key_buf_size))
			return -EFAULT;

		retcode = gpca_key_secure_import(
			key_cmd.secure_key_import.client_keyslot,
			key_cmd.secure_key_import.server_pub_keyslot,
			key_cmd.secure_key_import.dest_keyslot,
			key_cmd.secure_key_import.salt_present,
			key_cmd.secure_key_import.include_key_policy,
			&key_cmd.secure_key_import.key_policy, ctx_buf,
			key_cmd.secure_key_import.ctx_buf_size, label_buf,
			key_cmd.secure_key_import.salt_buf_size,
			key_buf.wrapped_key_buf,
			key_cmd.secure_key_import.wrapped_key_buf_size);
		break;

	default:
		pr_err("Invalid GPCA IOCTL command!");
		retcode = -EINVAL;
	}
	return retcode;
}
