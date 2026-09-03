// SPDX-License-Identifier: GPL-2.0-only
/*
 * Key Management driver for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#include "gpca_keys.h"

#include <linux/errno.h>
#include <linux/slab.h>

#include "gpca_cmd.h"
#include "gpca_internal.h"
#include "gpca_key_policy.h"
#include "gpca_op_queue.h"
#include "gpca_registers.h"

#define DOMAIN_INVALID_KEYSLOT ((DOMAIN_MAX_KEYSLOT) + 1)
/* Reserve last 2 keyslots for swappable keys and remaining for pinned */
#define DOMAIN_MIN_PINNED_KEYSLOT (DOMAIN_MIN_KEYSLOT)
#define DOMAIN_MAX_PINNED_KEYSLOT (DOMAIN_MAX_KEYSLOT - 2)
#define DOMAIN_MIN_SWAPPABLE_KEYSLOT ((DOMAIN_MAX_KEYSLOT) - 1)
#define DOMAIN_MAX_SWAPPABLE_KEYSLOT (DOMAIN_MAX_KEYSLOT)

struct gpca_cb_ctx {
	int ret;
	struct completion cpl;
};

struct gpca_op_key_gen {
	struct gpca_key_handle *key_handle;
	u64 kp;
	struct gpca_op_context op_ctx;
};

struct gpca_op_key_derive {
	struct gpca_key_handle *root_key_handle;
	struct gpca_key_handle *dest_key_handle;
	u64 dest_kp;
	const u8 *ctx_buf;
	u32 ctx_buf_size;
	struct gpca_op_context op_ctx;
};

struct gpca_op_key_wrap {
	struct gpca_key_handle *wrapping_key_handle;
	struct gpca_key_handle *src_key_handle;
	u8 *wrapped_key_buf;
	u32 wrapped_key_buf_size;
	u32 *wrapped_key_buf_size_out;
	struct gpca_op_context op_ctx;
};

struct gpca_op_key_unwrap {
	struct gpca_key_handle *wrapping_key_handle;
	struct gpca_key_handle *dest_key_handle;
	const u8 *wrapped_key_buf;
	u32 wrapped_key_buf_size;
	struct gpca_op_context op_ctx;
};

struct gpca_op_key_send {
	struct gpca_key_handle *src_key_handle;
	enum gpca_key_table_id dest_key_table;
	u8 dest_keyslot;
	struct gpca_op_context op_ctx;
};

struct gpca_op_key_clear {
	struct gpca_key_handle *key_handle;
	struct gpca_op_context op_ctx;
};

struct gpca_op_get_seed {
	struct gpca_key_handle *key_handle;
	const u8 *label_buf;
	u32 label_buf_size;
	const u8 *ctx_buf;
	u32 ctx_buf_size;
	u8 *seed_buf;
	u32 seed_buf_size;
	u32 *seed_buf_size_out;
	struct gpca_op_context op_ctx;
};

struct gpca_op_key_import {
	struct gpca_key_handle *key_handle;
	u64 kp;
	const u8 *key_buf;
	u32 key_len;
	struct gpca_op_context op_ctx;
};

struct gpca_op_get_kp {
	struct gpca_key_handle *key_handle;
	struct gpca_key_policy *key_policy;
	struct gpca_op_context op_ctx;
};

struct gpca_op_get_metadata {
	struct gpca_key_handle *key_handle;
	struct gpca_key_metadata *key_metadata;
	struct gpca_op_context op_ctx;
};

struct gpca_op_set_metadata {
	struct gpca_key_handle *key_handle;
	const struct gpca_key_metadata *key_metadata;
	struct gpca_op_context op_ctx;
};

struct gpca_op_get_pub_key {
	struct gpca_key_handle *key_handle;
	u8 *pub_key_buf;
	u32 pub_key_buf_size;
	u32 *pub_key_buf_size_out;
	struct gpca_op_context op_ctx;
};

struct gpca_op_set_pub_key {
	struct gpca_key_handle *key_handle;
	const u8 *pub_key_buf;
	u32 pub_key_buf_size;
	struct gpca_op_context op_ctx;
};

struct gpca_op_secure_key_import {
	struct gpca_key_handle *client_key_handle;
	struct gpca_key_handle *server_pub_key_handle;
	struct gpca_key_handle *dest_key_handle;
	bool include_key_policy;
	u64 dest_kp;
	const u8 *ctx_buf;
	u32 ctx_buf_size;
	const u8 *salt_buf;
	u32 salt_buf_size;
	const u8 *wrapped_key_buf;
	u32 wrapped_key_buf_size;
	struct gpca_op_context op_ctx;
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

static int validate_key_send_destination(enum gpca_key_table_id dest_key_table)
{
	if (dest_key_table == SOC_GPCA_KEY_TABLE ||
	    dest_key_table == GSA_GPCA_KEY_TABLE ||
	    dest_key_table == ISE_KEY_TABLE)
		return 0;
	return -EINVAL;
}

static bool is_swappable(const struct gpca_key_policy *gpca_key_policy)
{
	return (gpca_key_policy->wrappable_by_ephemeral_key &&
		(gpca_key_policy->owner == GPCA_OWNER_DOMAIN) &&
		(gpca_key_policy->evict_gsa == GPCA_EVICT_GSA_DISABLE) &&
		(gpca_key_policy->evict_ap_s == GPCA_EVICT_AP_S_DISABLE) &&
		(gpca_key_policy->evict_ap_ns == GPCA_EVICT_AP_NS_DISABLE));
}

static bool is_large_key(const struct gpca_dev *ctx, const struct gpca_key_policy *kp)
{
	if (!ctx->drv_data->hw_bug_hmac_digest_size_keys &&
	    (kp->algo == GPCA_ALGO_HMAC_SHA2_384 ||
	     kp->algo == GPCA_ALGO_HMAC_SHA2_512)) {
		return true;
	}

	return (kp->algo == GPCA_ALGO_RSA_2048 ||
		kp->algo == GPCA_ALGO_RSA_3072 ||
		kp->algo == GPCA_ALGO_RSA_4096);
}

/**
 * alloc_gpca_key_handle() - Allocate GPCA key handle.
 *
 * Return: Allocated GPCA key handle.
 */
static struct gpca_key_handle *alloc_gpca_key_handle(void)
{
	struct gpca_key_handle *handle = NULL;

	handle = kcalloc(1, sizeof(*handle), GFP_KERNEL);
	if (handle)
		handle->keyslot = DOMAIN_INVALID_KEYSLOT;
	return handle;
}

/**
 * free_gpca_key_handle() - Free GPCA key handle.
 *
 * @handle: Key handle to be freed.
 */
static void free_gpca_key_handle(struct gpca_key_handle *handle)
{
	WARN_ON(handle && handle->key_state != UNINITIALIZED);

	kfree(handle);
}

static int swap_out_key(struct gpca_dev *gpca_dev,
			struct gpca_key_slot_node *slot)
{
	int ret = 0;

	if (!slot->key_handle) {
		dev_err(gpca_dev->dev, "Swapping out from empty slot");
		return -EINVAL;
	}

	dev_dbg(gpca_dev->dev, "Swapping out key from slot %d",
		slot->keyslot);
	/**
	 * Swappable key's Public key or wrapped key blob are
	 * already cached while adding the key.
	 * Hence, just clear the key and mark SWAPPED_OUT
	 */
	if (slot->key_handle->is_swappable) {
		/* Clear the key after swapping out */
		ret = gpca_cmd_key_clear(gpca_dev, slot->keyslot);
		if (ret != 0)
			return ret;

		/* Mark the key as SWAPPED_OUT */
		slot->key_handle->key_state = SWAPPED_OUT;
		slot->key_handle = NULL;
	} else {
		/* Key cannot be swapped out */
		return -EINVAL;
	}

	return ret;
}

struct gpca_key_slot_node *gpca_key_reserve_keyslot(struct gpca_dev *gpca_dev,
						    struct gpca_key_handle *handle)
{
	int ret = 0;
	struct gpca_key_slot_node *swap_tail_node = NULL;
	struct gpca_key_slot_node *pinned_tail_node = NULL;

	if (!handle)
		return NULL;

	/* If public key or large key */
	if (handle->is_public_key || handle->is_large_key) {
		/* If large key slot empty assign to it. */
		if (!gpca_dev->large_key.key_handle)
			return &gpca_dev->large_key;

		ret = swap_out_key(gpca_dev, &gpca_dev->large_key);
		if (ret != 0)
			return NULL;
		return &gpca_dev->large_key;
	} else if (!handle->is_swappable) {
		/* If key is unswappable */
		pinned_tail_node = list_last_entry(&gpca_dev->pinned_head,
						   struct gpca_key_slot_node,
						   list);

		/* No pinned slot available for key */
		if (pinned_tail_node->key_handle)
			return NULL;

		/* pinned_tail node is reserved and moved to pinned_head */
		list_move(&pinned_tail_node->list,
			  &gpca_dev->pinned_head);
		return pinned_tail_node;
	} else if (handle->is_swappable) {
		/* If swappable key, reserve swap_tail and move to swap_head */
		swap_tail_node = list_last_entry(
			&gpca_dev->swap_head, struct gpca_key_slot_node, list);
		if (swap_tail_node->key_handle) {
			ret = swap_out_key(gpca_dev, swap_tail_node);
			if (ret != 0)
				return NULL;
		}

		list_move(&swap_tail_node->list, &gpca_dev->swap_head);
		return swap_tail_node;
	}

	return NULL;
}

static void gpca_key_unreserve_keyslot(struct gpca_dev *gpca_dev, struct gpca_key_slot_node *slot)
{
	dev_dbg(gpca_dev->dev, "Unreserving keyslot %d", slot->keyslot);

	/* Case 1: Large keyslot */
	if (slot->keyslot == DOMAIN_LARGE_KEYSLOT)
		return;

	/* For small keys get the slot node */
	if (slot->keyslot >= DOMAIN_MIN_PINNED_KEYSLOT &&
	    slot->keyslot <= DOMAIN_MAX_PINNED_KEYSLOT) {
		/* Case 2: Move node to pinned_tail */
		list_move_tail(&slot->list, &gpca_dev->pinned_head);
	} else {
		/* Case 3: Move node to swap_tail */
		list_move_tail(&slot->list, &gpca_dev->swap_head);
	}
}

int gpca_key_swap_in_key(struct gpca_dev *gpca_dev,
			 struct gpca_key_handle *handle)
{
	int ret = 0;
	struct gpca_key_slot_node *slot = NULL;

	if (!handle || handle->key_state == UNINITIALIZED ||
	    handle->keyslot == DOMAIN_INVALID_KEYSLOT)
		return -EINVAL;

	/* If swapped out, swap into same keyslot using unwrap */
	if (handle->key_state == SWAPPED_OUT) {
		/* Swap in the key */
		if (handle->is_public_key) {
			dev_dbg(gpca_dev->dev,
				"Swapping in key to keyslot %d",
				DOMAIN_LARGE_KEYSLOT);

			/* If public key, set public key */
			ret = gpca_cmd_set_public_key(
				gpca_dev, DOMAIN_LARGE_KEYSLOT,
				handle->wrapped_key_blob,
				handle->wrapped_key_blob_size);
		} else {
			dev_dbg(gpca_dev->dev,
				"Swapping in key to keyslot %d",
				handle->keyslot);

			slot = &((gpca_dev->small_keys)[handle->keyslot -
							DOMAIN_MIN_KEYSLOT]);
			if (slot->key_handle) {
				ret = swap_out_key(gpca_dev, slot);
				if (ret)
					return ret;
			}

			/* Unwrap the key into slot */
			ret = gpca_cmd_key_unwrap(
				gpca_dev, DOMAIN_KWK_KEYSLOT, handle->keyslot,
				handle->wrapped_key_blob,
				handle->wrapped_key_blob_size);
			if (ret)
				return ret;

			slot->key_handle = handle;
			list_move(&slot->list, &gpca_dev->swap_head);
		}
		if (ret == 0)
			handle->key_state = IN_KEY_TABLE;

	} else if (handle->key_state == IN_KEY_TABLE) {
		/**
		 * If the key is in small swappable keyslot,
		 * move slot node to swap_head
		 */
		if (handle->keyslot != DOMAIN_LARGE_KEYSLOT &&
		    handle->is_swappable) {
			slot = &((gpca_dev->small_keys)[handle->keyslot -
							DOMAIN_MIN_KEYSLOT]);
			list_move(&slot->list, &gpca_dev->swap_head);
		}
	} else {
		/* Invalid key state */
		return -EINVAL;
	}
	return 0;
}

void gpca_key_free_keyslot(struct gpca_dev *gpca_dev, u8 keyslot)
{
	struct gpca_key_slot_node *slot = NULL;

	dev_dbg(gpca_dev->dev, "Freeing keyslot %d", keyslot);
	/* Case 1: Large keyslot */
	if (keyslot == DOMAIN_LARGE_KEYSLOT) {
		if (gpca_dev->large_key.key_handle) {
			(gpca_dev->large_key).key_handle->key_state = UNINITIALIZED;
			(gpca_dev->large_key).key_handle->keyslot = DOMAIN_INVALID_KEYSLOT;
			(gpca_dev->large_key).key_handle = NULL;
		} else {
			dev_warn(gpca_dev->dev, "Freeing empty keyslot %d", keyslot);
		}
		return;
	}

	/* For small keys get the slot node */
	slot = &((gpca_dev->small_keys)[keyslot - DOMAIN_MIN_KEYSLOT]);
	if (!slot->key_handle) {
		dev_warn(gpca_dev->dev, "Freeing empty keyslot %d", keyslot);
		return;
	}
	if (!(slot->key_handle->is_swappable)) {
		/* Case 2: Move node to pinned_tail */
		list_move_tail(&slot->list, &gpca_dev->pinned_head);
	} else {
		/* Case 3: Move node to swap_tail */
		list_move_tail(&slot->list, &gpca_dev->swap_head);
	}

	slot->key_handle->key_state = UNINITIALIZED;
	slot->key_handle->keyslot = DOMAIN_INVALID_KEYSLOT;
	slot->key_handle = NULL;
}

static void gpca_key_assign_slot(struct gpca_dev *gpca_dev,
				 struct gpca_key_handle *handle,
				 struct gpca_key_slot_node *slot)
{
	if (!handle || !slot)
		dev_err(gpca_dev->dev, "Invalid key handle or slot");

	slot->key_handle = handle;
	handle->keyslot = slot->keyslot;
	handle->key_state = IN_KEY_TABLE;

	dev_dbg(gpca_dev->dev, "Assigned slot %d for key", handle->keyslot);
}

struct gpca_key_slot_node *gpca_key_mark_key_swappable(struct gpca_dev *gpca_dev,
						       struct gpca_key_handle *handle,
						       struct gpca_key_slot_node *pinned_slot)
{
	int ret = 0;
	struct gpca_key_slot_node *swappable_slot = NULL;

	/* Add to swappable and remove from pinned */
	handle->is_swappable = true;
	handle->wrapping_keyslot = DOMAIN_KWK_KEYSLOT;
	ret = gpca_cmd_key_wrap(gpca_dev, DOMAIN_KWK_KEYSLOT,
				pinned_slot->keyslot,
				handle->wrapped_key_blob,
				sizeof(handle->wrapped_key_blob),
				&handle->wrapped_key_blob_size);
	if (ret)
		return NULL;
	ret = gpca_cmd_key_clear(gpca_dev, pinned_slot->keyslot);
	if (ret)
		return NULL;

	gpca_key_unreserve_keyslot(gpca_dev, pinned_slot);

	swappable_slot = gpca_key_reserve_keyslot(gpca_dev, handle);
	if (!swappable_slot)
		return NULL;

	dev_dbg(gpca_dev->dev, "Moving the key from slot %d to slot %d",
		pinned_slot->keyslot, swappable_slot->keyslot);

	/* Unwrap the key into slot */
	ret = gpca_cmd_key_unwrap(gpca_dev, DOMAIN_KWK_KEYSLOT,
				  swappable_slot->keyslot,
				  handle->wrapped_key_blob,
				  handle->wrapped_key_blob_size);
	if (ret)
		return NULL;
	return swappable_slot;
}

int gpca_key_init(struct gpca_dev *gpca_dev)
{
	u32 keyslot = 0;
	u32 idx = 0;
	int ret = 0;
	u64 raw_kp = 0;

	const struct gpca_key_policy wrapping_kp = {
		.key_class = GPCA_KEY_CLASS_EPHEMERAL,
		.algo = GPCA_ALGO_AES256_KWP,
		.purposes = GPCA_SUPPORTED_PURPOSE_ENCRYPT |
			    GPCA_SUPPORTED_PURPOSE_DECRYPT,
		.key_management_key = GPCA_KMK_ENABLE,
		.wrappable_by_ephemeral_key = GPCA_WAEK_DISABLE,
		.wrappable_by_hardware_key = GPCA_WAHK_DISABLE,
		.wrappable_by_portable_key = GPCA_WAPK_DISABLE,
		.boot_state_bound = GPCA_BS_DISABLE,
		.evict_gsa = GPCA_EVICT_GSA_DISABLE,
		.evict_ap_s = GPCA_EVICT_AP_S_DISABLE,
		.evict_ap_ns = GPCA_EVICT_AP_NS_DISABLE,
		.owner = GPCA_OWNER_DOMAIN,
		.auth_domains =
			GPCA_AUTH_DOMAIN_TZ | GPCA_AUTH_DOMAIN_ANDROID_VM |
			GPCA_AUTH_DOMAIN_NS_VM1 | GPCA_AUTH_DOMAIN_NS_VM2
	};

	/**
	 * Generate a Key Wrapping Key for the current domain.
	 * This key is used to swap keys in and out of key table.
	 */
	ret = gpca_key_policy_to_raw(&wrapping_kp, &raw_kp);
	if (ret != 0)
		goto exit;

	ret = gpca_cmd_key_generate(gpca_dev, DOMAIN_KWK_KEYSLOT, raw_kp);
	if (ret != 0)
		goto exit;

	INIT_LIST_HEAD(&gpca_dev->pinned_head);
	INIT_LIST_HEAD(&gpca_dev->swap_head);

	for (keyslot = DOMAIN_MIN_PINNED_KEYSLOT; keyslot <= DOMAIN_MAX_PINNED_KEYSLOT;
	     keyslot++) {
		idx = keyslot - DOMAIN_MIN_KEYSLOT;
		gpca_dev->small_keys[idx].keyslot = keyslot;
		gpca_dev->small_keys[idx].key_handle = NULL;
		list_add_tail(&gpca_dev->small_keys[idx].list,
			      &gpca_dev->pinned_head);
	}
	for (keyslot = DOMAIN_MIN_SWAPPABLE_KEYSLOT; keyslot <= DOMAIN_MAX_SWAPPABLE_KEYSLOT;
	     keyslot++) {
		idx = keyslot - DOMAIN_MIN_KEYSLOT;
		gpca_dev->small_keys[idx].keyslot = keyslot;
		gpca_dev->small_keys[idx].key_handle = NULL;
		list_add_tail(&gpca_dev->small_keys[idx].list,
			      &gpca_dev->swap_head);
	}
	gpca_dev->large_key.keyslot = DOMAIN_LARGE_KEYSLOT;
	gpca_dev->large_key.key_handle = NULL;

exit:
	dev_info(gpca_dev->dev, "%s error code = 0x%x", __func__, ret);
	return ret;
}

void gpca_key_deinit(struct gpca_dev *gpca_dev)
{
	int ret = 0;
	u32 keyslot = 0;
	u32 idx = 0;

	ret = gpca_cmd_key_clear(gpca_dev, DOMAIN_KWK_KEYSLOT);
	if (ret != 0)
		dev_info(gpca_dev->dev, "Key clear in %s error code = 0x%x", __func__, ret);

	// Clear keys from key table
	for (keyslot = DOMAIN_MIN_KEYSLOT; keyslot <= DOMAIN_MAX_KEYSLOT; keyslot++) {
		idx = keyslot - DOMAIN_MIN_KEYSLOT;
		if (gpca_dev->small_keys[idx].key_handle) {
			ret = gpca_cmd_key_clear(gpca_dev, keyslot);
			if (ret != 0)
				dev_info(gpca_dev->dev, "Key clear of slot = %d failed", keyslot);
		}
	}

	if (gpca_dev->large_key.key_handle) {
		ret = gpca_cmd_key_clear(gpca_dev, DOMAIN_LARGE_KEYSLOT);
		if (ret != 0)
			dev_info(gpca_dev->dev, "Key clear of slot = %d failed",
				 DOMAIN_LARGE_KEYSLOT);
	}
}

struct gpca_dev *gpca_key_get_device_context(struct platform_device *pdev)
{
	struct gpca_dev *dev_ctx = NULL;

	if (!pdev)
		return NULL;

	dev_ctx = platform_get_drvdata(pdev);
	return dev_ctx;
}
EXPORT_SYMBOL_GPL(gpca_key_get_device_context);

static int gpca_key_generate_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_key_gen *key_gen_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;
	struct gpca_key_slot_node *slot = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_KEY_GEN)
		return -EINVAL;

	key_gen_ctx = container_of(op_ctx, struct gpca_op_key_gen, op_ctx);
	key_handle = key_gen_ctx->key_handle;
	if (key_handle->key_state != UNINITIALIZED)
		return -EINVAL;

	slot = gpca_key_reserve_keyslot(op_ctx->gpca_dev, key_handle);
	if (!slot) {
		ret = -EINVAL;
		goto exit;
	}

	ret = gpca_cmd_key_generate(op_ctx->gpca_dev, slot->keyslot,
				    key_gen_ctx->kp);
	if (ret != 0)
		goto unreserve_slot;

	if (key_handle->is_swappable) {
		ret = gpca_cmd_key_wrap(op_ctx->gpca_dev, DOMAIN_KWK_KEYSLOT,
					slot->keyslot,
					key_handle->wrapped_key_blob,
					sizeof(key_handle->wrapped_key_blob),
					&key_handle->wrapped_key_blob_size);
	}

	if (ret == 0) {
		gpca_key_assign_slot(op_ctx->gpca_dev, key_handle, slot);
		goto exit;
	} else {
		gpca_cmd_key_clear(op_ctx->gpca_dev, slot->keyslot);
	}

unreserve_slot:
	gpca_key_unreserve_keyslot(op_ctx->gpca_dev, slot);
exit:
	kfree(key_gen_ctx);
	return ret;
}

int gpca_key_generate(struct gpca_dev *ctx, struct gpca_key_handle **key_handle,
		      const struct gpca_key_policy *key_policy)
{
	u64 raw_kp = 0;
	int ret = 0;
	bool is_key_swappable = false;
	bool is_key_large_key = false;
	struct gpca_op_key_gen *key_gen_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !key_policy)
		return -EINVAL;

	ret = gpca_key_policy_to_raw(key_policy, &raw_kp);
	if (ret != 0)
		return ret;

	is_key_swappable = is_swappable(key_policy);
	is_key_large_key = is_large_key(ctx, key_policy);
	/* Large key must be swappable */
	if (is_key_large_key && !is_key_swappable)
		return -EINVAL;

	*key_handle = alloc_gpca_key_handle();
	if (*key_handle == NULL)
		return -ENOMEM;

	(*key_handle)->is_public_key = false;
	(*key_handle)->is_swappable = is_key_swappable;
	(*key_handle)->is_large_key = is_key_large_key;
	if ((*key_handle)->is_swappable)
		(*key_handle)->wrapping_keyslot = DOMAIN_KWK_KEYSLOT;

	/* Queue Key generate operation */
	key_gen_ctx = kcalloc(1, sizeof(*key_gen_ctx), GFP_KERNEL);
	if (!key_gen_ctx) {
		ret = -ENOMEM;
		goto free_key_handle;
	}

	key_gen_ctx->op_ctx.gpca_dev = ctx;
	key_gen_ctx->op_ctx.op_type = GPCA_OP_TYPE_KEY_GEN;
	key_gen_ctx->key_handle = *key_handle;
	key_gen_ctx->kp = raw_kp;
	key_gen_ctx->op_ctx.op_handler = gpca_key_generate_async_handler;
	key_gen_ctx->op_ctx.cb = gpca_cb;
	key_gen_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &key_gen_ctx->op_ctx);
	if (ret != 0) {
		kfree(key_gen_ctx);
		goto free_key_handle;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;

	/* Free key handle if key generate fails */
	if (ret != 0)
		goto free_key_handle;

	return ret;

free_key_handle:
	free_gpca_key_handle(*key_handle);
	*key_handle = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_generate);

static int gpca_key_derive_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_key_derive *key_derive_ctx = NULL;
	struct gpca_key_handle *root_key_handle = NULL;
	struct gpca_key_handle *dest_key_handle = NULL;
	struct gpca_key_slot_node *slot = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_KEY_DERIVE)
		return -EINVAL;

	key_derive_ctx =
		container_of(op_ctx, struct gpca_op_key_derive, op_ctx);
	root_key_handle = key_derive_ctx->root_key_handle;
	dest_key_handle = key_derive_ctx->dest_key_handle;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, root_key_handle);
	if (ret != 0)
		goto exit;

	if (dest_key_handle->key_state != UNINITIALIZED)
		return -EINVAL;

	slot = gpca_key_reserve_keyslot(op_ctx->gpca_dev, dest_key_handle);
	if (!slot) {
		ret = -EINVAL;
		goto exit;
	}

	ret = gpca_cmd_key_derive(op_ctx->gpca_dev, root_key_handle->keyslot,
				  slot->keyslot,
				  key_derive_ctx->dest_kp,
				  key_derive_ctx->ctx_buf,
				  key_derive_ctx->ctx_buf_size);
	if (ret != 0)
		goto unreserve_slot;

	if (dest_key_handle->is_swappable) {
		ret = gpca_cmd_key_wrap(
			op_ctx->gpca_dev, DOMAIN_KWK_KEYSLOT,
			slot->keyslot,
			dest_key_handle->wrapped_key_blob,
			sizeof(dest_key_handle->wrapped_key_blob),
			&dest_key_handle->wrapped_key_blob_size);
	}

	if (ret == 0) {
		gpca_key_assign_slot(op_ctx->gpca_dev, dest_key_handle, slot);
		goto exit;
	}  else {
		gpca_cmd_key_clear(op_ctx->gpca_dev, slot->keyslot);
	}

unreserve_slot:
	gpca_key_unreserve_keyslot(op_ctx->gpca_dev, slot);
exit:
	kfree(key_derive_ctx);
	return ret;
}

int gpca_key_derive(struct gpca_dev *ctx,
		    struct gpca_key_handle *root_key_handle,
		    struct gpca_key_handle **dest_key_handle,
		    const struct gpca_key_policy *dest_key_policy,
		    const u8 *ctx_buf, u32 ctx_buf_size_bytes)
{
	int ret = 0;
	u64 raw_dest_kp = 0;
	bool is_dest_swappable = false;
	bool is_dest_large_key = false;
	struct gpca_op_key_derive *key_derive_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !root_key_handle || !dest_key_handle || !dest_key_policy ||
	    !ctx_buf || ctx_buf_size_bytes == 0)
		return -EINVAL;

	ret = gpca_key_policy_to_raw(dest_key_policy, &raw_dest_kp);
	if (ret != 0)
		return ret;

	is_dest_swappable = is_swappable(dest_key_policy);
	is_dest_large_key = is_large_key(ctx, dest_key_policy);
	/* Large key must be swappable */
	if (is_dest_large_key && !is_dest_swappable)
		return -EINVAL;

	*dest_key_handle = alloc_gpca_key_handle();
	if (*dest_key_handle == NULL)
		return -ENOMEM;

	(*dest_key_handle)->is_public_key = false;
	(*dest_key_handle)->is_swappable = is_dest_swappable;
	(*dest_key_handle)->is_large_key = is_dest_large_key;
	if ((*dest_key_handle)->is_swappable)
		(*dest_key_handle)->wrapping_keyslot = DOMAIN_KWK_KEYSLOT;

	/* Queue Key derive operation */
	key_derive_ctx = kcalloc(1, sizeof(*key_derive_ctx), GFP_KERNEL);
	if (!key_derive_ctx) {
		ret = -ENOMEM;
		goto free_key_handle;
	}
	key_derive_ctx->op_ctx.gpca_dev = ctx;
	key_derive_ctx->op_ctx.op_type = GPCA_OP_TYPE_KEY_DERIVE;
	key_derive_ctx->root_key_handle = root_key_handle;
	key_derive_ctx->dest_key_handle = *dest_key_handle;
	key_derive_ctx->dest_kp = raw_dest_kp;
	key_derive_ctx->ctx_buf = ctx_buf;
	key_derive_ctx->ctx_buf_size = ctx_buf_size_bytes;
	key_derive_ctx->op_ctx.op_handler = gpca_key_derive_async_handler;
	key_derive_ctx->op_ctx.cb = gpca_cb;
	key_derive_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &key_derive_ctx->op_ctx);
	if (ret != 0) {
		kfree(key_derive_ctx);
		goto free_key_handle;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;

	/* Free dest_key_handle if key derive fails */
	if (ret != 0)
		goto free_key_handle;

	return ret;

free_key_handle:
	free_gpca_key_handle(*dest_key_handle);
	*dest_key_handle = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_derive);

static int gpca_key_wrap_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_key_wrap *key_wrap_ctx = NULL;
	struct gpca_key_handle *wrapping_key_handle = NULL;
	struct gpca_key_handle *src_key_handle = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_KEY_WRAP)
		return -EINVAL;

	key_wrap_ctx = container_of(op_ctx, struct gpca_op_key_wrap, op_ctx);
	wrapping_key_handle = key_wrap_ctx->wrapping_key_handle;
	src_key_handle = key_wrap_ctx->src_key_handle;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, wrapping_key_handle);
	if (ret != 0)
		goto exit;

	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, src_key_handle);
	if (ret != 0)
		goto exit;

	ret = gpca_cmd_key_wrap(op_ctx->gpca_dev, wrapping_key_handle->keyslot,
				src_key_handle->keyslot,
				key_wrap_ctx->wrapped_key_buf,
				key_wrap_ctx->wrapped_key_buf_size,
				key_wrap_ctx->wrapped_key_buf_size_out);
exit:
	kfree(key_wrap_ctx);
	return ret;
}

int gpca_key_wrap(struct gpca_dev *ctx,
		  struct gpca_key_handle *wrapping_key_handle,
		  struct gpca_key_handle *src_key_handle, u8 *wrapped_key_buf,
		  u32 wrapped_key_buf_size, u32 *wrapped_key_buf_size_out)
{
	int ret = 0;
	struct gpca_op_key_wrap *key_wrap_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !wrapping_key_handle || !src_key_handle ||
	    !wrapped_key_buf || wrapped_key_buf_size == 0 ||
	    !wrapped_key_buf_size_out)
		return -EINVAL;

	/* Queue Key wrap operation */
	key_wrap_ctx = kcalloc(1, sizeof(*key_wrap_ctx), GFP_KERNEL);
	if (!key_wrap_ctx)
		return -ENOMEM;
	key_wrap_ctx->op_ctx.gpca_dev = ctx;
	key_wrap_ctx->op_ctx.op_type = GPCA_OP_TYPE_KEY_WRAP;
	key_wrap_ctx->wrapping_key_handle = wrapping_key_handle;
	key_wrap_ctx->src_key_handle = src_key_handle;
	key_wrap_ctx->wrapped_key_buf = wrapped_key_buf;
	key_wrap_ctx->wrapped_key_buf_size = wrapped_key_buf_size;
	key_wrap_ctx->wrapped_key_buf_size_out = wrapped_key_buf_size_out;
	key_wrap_ctx->op_ctx.op_handler = gpca_key_wrap_async_handler;
	key_wrap_ctx->op_ctx.cb = gpca_cb;
	key_wrap_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &key_wrap_ctx->op_ctx);
	if (ret != 0) {
		kfree(key_wrap_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_wrap);

static int gpca_key_unwrap_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_key_unwrap *key_unwrap_ctx = NULL;
	struct gpca_key_handle *wrapping_key_handle = NULL;
	struct gpca_key_handle *dest_key_handle = NULL;
	struct gpca_key_slot_node *slot = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_KEY_UNWRAP)
		return -EINVAL;

	key_unwrap_ctx =
		container_of(op_ctx, struct gpca_op_key_unwrap, op_ctx);
	wrapping_key_handle = key_unwrap_ctx->wrapping_key_handle;
	dest_key_handle = key_unwrap_ctx->dest_key_handle;

	/* Wrapping key must be static/unswappable i.e. IN_KEY_TABLE at fixed keyslot */
	if (wrapping_key_handle->key_state != IN_KEY_TABLE)
		return -EINVAL;

	slot = gpca_key_reserve_keyslot(op_ctx->gpca_dev, dest_key_handle);
	if (!slot) {
		ret = -EINVAL;
		goto exit;
	}

	ret = gpca_cmd_key_unwrap(op_ctx->gpca_dev,
				  wrapping_key_handle->keyslot,
				  slot->keyslot,
				  key_unwrap_ctx->wrapped_key_buf,
				  key_unwrap_ctx->wrapped_key_buf_size);
	if (ret != 0)
		gpca_key_unreserve_keyslot(op_ctx->gpca_dev, slot);
	else
		gpca_key_assign_slot(op_ctx->gpca_dev, dest_key_handle, slot);

exit:
	kfree(key_unwrap_ctx);
	return ret;
}

int gpca_key_unwrap(struct gpca_dev *ctx,
		    struct gpca_key_handle *wrapping_key_handle,
		    struct gpca_key_handle **dest_key_handle,
		    const u8 *wrapped_key_buf, u32 wrapped_key_buf_size)
{
	int ret = 0;
	struct gpca_op_key_unwrap *key_unwrap_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !wrapping_key_handle || !dest_key_handle ||
	    !wrapped_key_buf || wrapped_key_buf_size == 0)
		return -EINVAL;

	*dest_key_handle = alloc_gpca_key_handle();
	if (*dest_key_handle == NULL)
		return -ENOMEM;

	(*dest_key_handle)->is_public_key = false;
	(*dest_key_handle)->is_swappable = true;
	(*dest_key_handle)->is_large_key =
		(wrapped_key_buf_size == MAX_WRAPPED_KEY_SIZE_BYTES);
	(*dest_key_handle)->wrapping_keyslot = wrapping_key_handle->keyslot;

	/* Queue Key unwrap operation */
	key_unwrap_ctx = kcalloc(1, sizeof(*key_unwrap_ctx), GFP_KERNEL);
	if (!key_unwrap_ctx) {
		ret = -ENOMEM;
		goto free_key_handle;
	}
	key_unwrap_ctx->op_ctx.gpca_dev = ctx;
	key_unwrap_ctx->op_ctx.op_type = GPCA_OP_TYPE_KEY_UNWRAP;
	key_unwrap_ctx->wrapping_key_handle = wrapping_key_handle;
	key_unwrap_ctx->dest_key_handle = *dest_key_handle;
	key_unwrap_ctx->wrapped_key_buf = wrapped_key_buf;
	key_unwrap_ctx->wrapped_key_buf_size = wrapped_key_buf_size;
	key_unwrap_ctx->op_ctx.op_handler = gpca_key_unwrap_async_handler;
	key_unwrap_ctx->op_ctx.cb = gpca_cb;
	key_unwrap_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &key_unwrap_ctx->op_ctx);
	if (ret != 0) {
		kfree(key_unwrap_ctx);
		goto free_key_handle;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;

	/* Free dest_key_handle if key unwrap fails */
	if (ret != 0)
		goto free_key_handle;

	return ret;

free_key_handle:
	free_gpca_key_handle(*dest_key_handle);
	*dest_key_handle = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_unwrap);

static int gpca_key_send_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_key_send *key_send_ctx = NULL;
	struct gpca_key_handle *src_key_handle = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_KEY_SEND)
		return -EINVAL;

	key_send_ctx = container_of(op_ctx, struct gpca_op_key_send, op_ctx);
	src_key_handle = key_send_ctx->src_key_handle;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, src_key_handle);
	if (ret != 0)
		goto exit;
	ret = gpca_cmd_key_send(op_ctx->gpca_dev, src_key_handle->keyslot,
				key_send_ctx->dest_key_table,
				key_send_ctx->dest_keyslot);
exit:
	kfree(key_send_ctx);
	return ret;
}

int gpca_key_send(struct gpca_dev *ctx, struct gpca_key_handle *src_key_handle,
		  enum gpca_key_table_id dest_key_table, u8 dest_keyslot)
{
	int ret = 0;
	struct gpca_op_key_send *key_send_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !src_key_handle ||
	    validate_key_send_destination(dest_key_table) != 0)
		return -EINVAL;

	/* Queue Key send operation */
	key_send_ctx = kcalloc(1, sizeof(*key_send_ctx), GFP_KERNEL);
	if (!key_send_ctx)
		return -ENOMEM;
	key_send_ctx->op_ctx.gpca_dev = ctx;
	key_send_ctx->op_ctx.op_type = GPCA_OP_TYPE_KEY_SEND;
	key_send_ctx->src_key_handle = src_key_handle;
	key_send_ctx->dest_key_table = dest_key_table;
	key_send_ctx->dest_keyslot = dest_keyslot;
	key_send_ctx->op_ctx.op_handler = gpca_key_send_async_handler;
	key_send_ctx->op_ctx.cb = gpca_cb;
	key_send_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &key_send_ctx->op_ctx);
	if (ret != 0) {
		kfree(key_send_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_send);

static int gpca_key_clear_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_key_clear *key_clear_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_KEY_CLEAR)
		return -EINVAL;

	key_clear_ctx = container_of(op_ctx, struct gpca_op_key_clear, op_ctx);
	key_handle = key_clear_ctx->key_handle;
	if (key_handle->key_state == SWAPPED_OUT) {
		key_handle->key_state = UNINITIALIZED;
		key_handle->keyslot = DOMAIN_INVALID_KEYSLOT;
		ret = 0;
		goto exit;
	}

	ret = gpca_cmd_key_clear(op_ctx->gpca_dev, key_handle->keyslot);
	if (ret == 0)
		gpca_key_free_keyslot(op_ctx->gpca_dev, key_handle->keyslot);

exit:
	kfree(key_clear_ctx);
	return ret;
}

int gpca_key_clear(struct gpca_dev *ctx, struct gpca_key_handle **key_handle)
{
	int ret = 0;
	struct gpca_op_key_clear *key_clear_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || *key_handle == NULL)
		return -EINVAL;

	/* Queue Key clear operation */
	key_clear_ctx = kcalloc(1, sizeof(*key_clear_ctx), GFP_KERNEL);
	if (!key_clear_ctx)
		return -ENOMEM;
	key_clear_ctx->op_ctx.gpca_dev = ctx;
	key_clear_ctx->op_ctx.op_type = GPCA_OP_TYPE_KEY_CLEAR;
	key_clear_ctx->key_handle = *key_handle;
	key_clear_ctx->op_ctx.op_handler = gpca_key_clear_async_handler;
	key_clear_ctx->op_ctx.cb = gpca_cb;
	key_clear_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &key_clear_ctx->op_ctx);
	if (ret != 0) {
		kfree(key_clear_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;

	if (ret == 0) {
		free_gpca_key_handle(*key_handle);
		*key_handle = NULL;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_clear);

static int
gpca_key_get_software_seed_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_get_seed *get_seed_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_GET_SEED)
		return -EINVAL;

	get_seed_ctx = container_of(op_ctx, struct gpca_op_get_seed, op_ctx);
	key_handle = get_seed_ctx->key_handle;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, key_handle);
	if (ret != 0)
		goto exit;

	ret = gpca_cmd_get_software_seed(
		op_ctx->gpca_dev, key_handle->keyslot, get_seed_ctx->label_buf,
		get_seed_ctx->label_buf_size, get_seed_ctx->ctx_buf,
		get_seed_ctx->ctx_buf_size, get_seed_ctx->seed_buf,
		get_seed_ctx->seed_buf_size, get_seed_ctx->seed_buf_size_out);

exit:
	kfree(get_seed_ctx);
	return ret;
}

int gpca_key_get_software_seed(struct gpca_dev *ctx,
			       struct gpca_key_handle *key_handle,
			       const u8 *label_buf, u32 label_buf_size,
			       const u8 *ctx_buf, u32 ctx_buf_size,
			       u8 *seed_buf, u32 seed_buf_size,
			       u32 *seed_buf_size_out)
{
	int ret = 0;
	struct gpca_op_get_seed *get_seed_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !label_buf || label_buf_size == 0 ||
	    !ctx_buf || ctx_buf_size == 0 || !seed_buf || seed_buf_size == 0 ||
	    !seed_buf_size_out)
		return -EINVAL;

	/* Queue Get software seed operation */
	get_seed_ctx = kcalloc(1, sizeof(*get_seed_ctx), GFP_KERNEL);
	if (!get_seed_ctx)
		return -ENOMEM;

	get_seed_ctx->op_ctx.gpca_dev = ctx;
	get_seed_ctx->op_ctx.op_type = GPCA_OP_TYPE_GET_SEED;
	get_seed_ctx->key_handle = key_handle;
	get_seed_ctx->label_buf = label_buf;
	get_seed_ctx->label_buf_size = label_buf_size;
	get_seed_ctx->ctx_buf = ctx_buf;
	get_seed_ctx->ctx_buf_size = ctx_buf_size;
	get_seed_ctx->seed_buf = seed_buf;
	get_seed_ctx->seed_buf_size = seed_buf_size;
	get_seed_ctx->seed_buf_size_out = seed_buf_size_out;
	get_seed_ctx->op_ctx.op_handler =
		gpca_key_get_software_seed_async_handler;
	get_seed_ctx->op_ctx.cb = gpca_cb;
	get_seed_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &get_seed_ctx->op_ctx);
	if (ret != 0) {
		kfree(get_seed_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_get_software_seed);

static int gpca_key_import_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_key_import *key_import_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;
	struct gpca_key_slot_node *slot = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_KEY_IMPORT)
		return -EINVAL;

	key_import_ctx =
		container_of(op_ctx, struct gpca_op_key_import, op_ctx);
	key_handle = key_import_ctx->key_handle;
	slot = gpca_key_reserve_keyslot(op_ctx->gpca_dev, key_handle);
	if (!slot) {
		ret = -EINVAL;
		goto exit;
	}

	ret = gpca_cmd_key_import(op_ctx->gpca_dev, slot->keyslot,
				  key_import_ctx->kp, key_import_ctx->key_buf,
				  key_import_ctx->key_len);
	if (ret != 0)
		goto unreserve_slot;

	if (key_handle->is_swappable) {
		ret = gpca_cmd_key_wrap(op_ctx->gpca_dev, DOMAIN_KWK_KEYSLOT,
					slot->keyslot,
					key_handle->wrapped_key_blob,
					sizeof(key_handle->wrapped_key_blob),
					&key_handle->wrapped_key_blob_size);
	}
	if (ret == 0) {
		gpca_key_assign_slot(op_ctx->gpca_dev, key_handle, slot);
		goto exit;
	}  else {
		gpca_cmd_key_clear(op_ctx->gpca_dev, slot->keyslot);
	}

unreserve_slot:
	gpca_key_unreserve_keyslot(op_ctx->gpca_dev, slot);
exit:
	kfree(key_import_ctx);
	return ret;
}

int gpca_key_import(struct gpca_dev *ctx, struct gpca_key_handle **key_handle,
		    const struct gpca_key_policy *key_policy, const u8 *key_buf,
		    u32 key_len)
{
	int ret = 0;
	u64 raw_kp = 0;
	bool is_key_swappable = false;
	bool is_key_large_key = false;
	struct gpca_op_key_import *key_import_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !key_policy || !key_buf || key_len == 0)
		return -EINVAL;

	ret = gpca_key_policy_to_raw(key_policy, &raw_kp);
	if (ret != 0)
		return ret;
	is_key_swappable = is_swappable(key_policy);
	is_key_large_key = is_large_key(ctx, key_policy);
	/* Large key must be swappable */
	if (is_key_large_key && !is_key_swappable)
		return -EINVAL;

	*key_handle = alloc_gpca_key_handle();
	if (*key_handle == NULL)
		return -ENOMEM;

	(*key_handle)->is_public_key = false;
	(*key_handle)->is_swappable = is_key_swappable;
	(*key_handle)->is_large_key = is_key_large_key;
	if ((*key_handle)->is_swappable)
		(*key_handle)->wrapping_keyslot = DOMAIN_KWK_KEYSLOT;

	/* Queue Key import operation */
	key_import_ctx = kcalloc(1, sizeof(*key_import_ctx), GFP_KERNEL);
	if (!key_import_ctx) {
		ret = -ENOMEM;
		goto free_key_handle;
	}
	key_import_ctx->op_ctx.gpca_dev = ctx;
	key_import_ctx->op_ctx.op_type = GPCA_OP_TYPE_KEY_IMPORT;
	key_import_ctx->key_handle = *key_handle;
	key_import_ctx->kp = raw_kp;
	key_import_ctx->key_buf = key_buf;
	key_import_ctx->key_len = key_len;
	key_import_ctx->op_ctx.op_handler = gpca_key_import_async_handler;
	key_import_ctx->op_ctx.cb = gpca_cb;
	key_import_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &key_import_ctx->op_ctx);
	if (ret != 0) {
		kfree(key_import_ctx);
		goto free_key_handle;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;

	/* Free key_handle if key import fails */
	if (ret != 0)
		goto free_key_handle;

	return ret;

free_key_handle:
	free_gpca_key_handle(*key_handle);
	*key_handle = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_import);

static int gpca_key_get_policy_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_get_kp *get_kp_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;
	u64 raw_kp = 0;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_GET_KP)
		return -EINVAL;

	get_kp_ctx = container_of(op_ctx, struct gpca_op_get_kp, op_ctx);
	key_handle = get_kp_ctx->key_handle;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, key_handle);
	if (ret != 0)
		goto exit;

	ret = gpca_cmd_get_key_policy(op_ctx->gpca_dev, key_handle->keyslot,
				      &raw_kp);
	if (ret != 0)
		goto exit;

	ret = gpca_raw_to_key_policy(raw_kp, get_kp_ctx->key_policy);

exit:
	kfree(get_kp_ctx);
	return ret;
}

int gpca_key_get_policy(struct gpca_dev *ctx,
			struct gpca_key_handle *key_handle,
			struct gpca_key_policy *key_policy)
{
	int ret = 0;
	struct gpca_op_get_kp *get_kp_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !key_policy)
		return -EINVAL;

	/* Queue get key policy operation */
	get_kp_ctx = kcalloc(1, sizeof(*get_kp_ctx), GFP_KERNEL);
	if (!get_kp_ctx)
		return -ENOMEM;
	get_kp_ctx->op_ctx.gpca_dev = ctx;
	get_kp_ctx->op_ctx.op_type = GPCA_OP_TYPE_GET_KP;
	get_kp_ctx->key_handle = key_handle;
	get_kp_ctx->key_policy = key_policy;
	get_kp_ctx->op_ctx.op_handler = gpca_key_get_policy_async_handler;
	get_kp_ctx->op_ctx.cb = gpca_cb;
	get_kp_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &get_kp_ctx->op_ctx);
	if (ret != 0) {
		kfree(get_kp_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_get_policy);

static int gpca_key_get_metadata_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_get_metadata *get_metadata_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;
	struct gpca_key_metadata *key_metadata = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_GET_METADATA)
		return -EINVAL;

	get_metadata_ctx =
		container_of(op_ctx, struct gpca_op_get_metadata, op_ctx);
	key_handle = get_metadata_ctx->key_handle;
	key_metadata = get_metadata_ctx->key_metadata;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, key_handle);
	if (ret != 0)
		goto exit;
	ret = gpca_cmd_get_key_metadata(op_ctx->gpca_dev, key_handle->keyslot,
					&key_metadata->acv,
					&key_metadata->input_pdid,
					&key_metadata->output_pdid,
					&key_metadata->validity);
exit:
	kfree(get_metadata_ctx);
	return ret;
}

int gpca_key_get_metadata(struct gpca_dev *ctx,
			  struct gpca_key_handle *key_handle,
			  struct gpca_key_metadata *key_metadata)
{
	int ret = 0;
	struct gpca_op_get_metadata *get_metadata_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !key_metadata)
		return -EINVAL;

	/* Queue get key metadata operation */
	get_metadata_ctx = kcalloc(1, sizeof(*get_metadata_ctx), GFP_KERNEL);
	if (!get_metadata_ctx)
		return -ENOMEM;
	get_metadata_ctx->op_ctx.gpca_dev = ctx;
	get_metadata_ctx->op_ctx.op_type = GPCA_OP_TYPE_GET_METADATA;
	get_metadata_ctx->key_handle = key_handle;
	get_metadata_ctx->key_metadata = key_metadata;
	get_metadata_ctx->op_ctx.op_handler =
		gpca_key_get_metadata_async_handler;
	get_metadata_ctx->op_ctx.cb = gpca_cb;
	get_metadata_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &get_metadata_ctx->op_ctx);
	if (ret != 0) {
		kfree(get_metadata_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_get_metadata);

static int gpca_key_set_metadata_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_set_metadata *set_metadata_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;
	const struct gpca_key_metadata *key_metadata = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_SET_METADATA)
		return -EINVAL;

	set_metadata_ctx =
		container_of(op_ctx, struct gpca_op_set_metadata, op_ctx);
	key_handle = set_metadata_ctx->key_handle;
	key_metadata = set_metadata_ctx->key_metadata;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, key_handle);
	if (ret != 0)
		goto exit;
	ret = gpca_cmd_set_key_metadata(op_ctx->gpca_dev, key_handle->keyslot,
					key_metadata->acv,
					key_metadata->input_pdid,
					key_metadata->output_pdid,
					key_metadata->validity);
exit:
	kfree(set_metadata_ctx);
	return ret;
}

int gpca_key_set_metadata(struct gpca_dev *ctx,
			  struct gpca_key_handle *key_handle,
			  const struct gpca_key_metadata *key_metadata)
{
	int ret = 0;
	struct gpca_op_set_metadata *set_metadata_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !key_metadata)
		return -EINVAL;

	/* Queue set key metadata operation */
	set_metadata_ctx = kcalloc(1, sizeof(*set_metadata_ctx), GFP_KERNEL);
	if (!set_metadata_ctx)
		return -ENOMEM;
	set_metadata_ctx->op_ctx.gpca_dev = ctx;
	set_metadata_ctx->op_ctx.op_type = GPCA_OP_TYPE_SET_METADATA;
	set_metadata_ctx->key_handle = key_handle;
	set_metadata_ctx->key_metadata = key_metadata;
	set_metadata_ctx->op_ctx.op_handler =
		gpca_key_set_metadata_async_handler;
	set_metadata_ctx->op_ctx.cb = gpca_cb;
	set_metadata_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &set_metadata_ctx->op_ctx);
	if (ret != 0) {
		kfree(set_metadata_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_set_metadata);

static int gpca_key_get_public_key_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_get_pub_key *get_pub_key_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_GET_PUB_KEY)
		return -EINVAL;

	get_pub_key_ctx =
		container_of(op_ctx, struct gpca_op_get_pub_key, op_ctx);
	key_handle = get_pub_key_ctx->key_handle;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, key_handle);
	if (ret != 0)
		goto exit;
	ret = gpca_cmd_get_public_key(op_ctx->gpca_dev, key_handle->keyslot,
				      get_pub_key_ctx->pub_key_buf,
				      get_pub_key_ctx->pub_key_buf_size,
				      get_pub_key_ctx->pub_key_buf_size_out);

exit:
	kfree(get_pub_key_ctx);
	return ret;
}

int gpca_key_get_public_key(struct gpca_dev *ctx,
			    struct gpca_key_handle *key_handle, u8 *pub_key_buf,
			    u32 pub_key_buf_size, u32 *pub_key_buf_size_out)
{
	int ret = 0;
	struct gpca_op_get_pub_key *get_pub_key_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !pub_key_buf || pub_key_buf_size == 0 ||
	    !pub_key_buf_size_out)
		return -EINVAL;

	/* Queue get public key operation */
	get_pub_key_ctx = kcalloc(1, sizeof(*get_pub_key_ctx), GFP_KERNEL);
	if (!get_pub_key_ctx)
		return -ENOMEM;
	get_pub_key_ctx->op_ctx.gpca_dev = ctx;
	get_pub_key_ctx->op_ctx.op_type = GPCA_OP_TYPE_GET_PUB_KEY;
	get_pub_key_ctx->key_handle = key_handle;
	get_pub_key_ctx->pub_key_buf = pub_key_buf;
	get_pub_key_ctx->pub_key_buf_size = pub_key_buf_size;
	get_pub_key_ctx->pub_key_buf_size_out = pub_key_buf_size_out;
	get_pub_key_ctx->op_ctx.op_handler =
		gpca_key_get_public_key_async_handler;
	get_pub_key_ctx->op_ctx.cb = gpca_cb;
	get_pub_key_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &get_pub_key_ctx->op_ctx);
	if (ret != 0) {
		kfree(get_pub_key_ctx);
		return ret;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_get_public_key);

static int gpca_key_set_public_key_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_set_pub_key *set_pub_key_ctx = NULL;
	struct gpca_key_handle *key_handle = NULL;
	struct gpca_key_slot_node *slot = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_SET_PUB_KEY)
		return -EINVAL;

	set_pub_key_ctx =
		container_of(op_ctx, struct gpca_op_set_pub_key, op_ctx);
	key_handle = set_pub_key_ctx->key_handle;
	slot = gpca_key_reserve_keyslot(op_ctx->gpca_dev, key_handle);
	if (!slot) {
		ret = -EINVAL;
		goto exit;
	}

	ret = gpca_cmd_set_public_key(op_ctx->gpca_dev, slot->keyslot,
				      set_pub_key_ctx->pub_key_buf,
				      set_pub_key_ctx->pub_key_buf_size);
	if (ret != 0)
		gpca_key_unreserve_keyslot(op_ctx->gpca_dev, slot);
	else
		gpca_key_assign_slot(op_ctx->gpca_dev, key_handle, slot);

exit:
	kfree(set_pub_key_ctx);
	return ret;
}

int gpca_key_set_public_key(struct gpca_dev *ctx,
			    struct gpca_key_handle **key_handle,
			    const u8 *pub_key_buf, u32 pub_key_buf_size)
{
	int ret = 0;
	struct gpca_op_set_pub_key *set_pub_key_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !key_handle || !pub_key_buf || pub_key_buf_size == 0)
		return -EINVAL;

	*key_handle = alloc_gpca_key_handle();
	if (*key_handle == NULL)
		return -ENOMEM;

	(*key_handle)->is_public_key = true;
	(*key_handle)->is_swappable = true;
	(*key_handle)->is_large_key = true;

	if (pub_key_buf_size > sizeof((*key_handle)->wrapped_key_blob)) {
		free_gpca_key_handle(*key_handle);
		*key_handle = NULL;
		return -EINVAL;
	}
	/* Keep a copy of public key in the handle */
	memcpy((*key_handle)->wrapped_key_blob, pub_key_buf, pub_key_buf_size);
	(*key_handle)->wrapped_key_blob_size = pub_key_buf_size;

	/* Queue set public key operation */
	set_pub_key_ctx = kcalloc(1, sizeof(*set_pub_key_ctx), GFP_KERNEL);
	if (!set_pub_key_ctx) {
		ret = -ENOMEM;
		goto free_key_handle;
	}
	set_pub_key_ctx->op_ctx.gpca_dev = ctx;
	set_pub_key_ctx->op_ctx.op_type = GPCA_OP_TYPE_SET_PUB_KEY;
	set_pub_key_ctx->key_handle = *key_handle;
	set_pub_key_ctx->pub_key_buf = pub_key_buf;
	set_pub_key_ctx->pub_key_buf_size = pub_key_buf_size;
	set_pub_key_ctx->op_ctx.op_handler =
		gpca_key_set_public_key_async_handler;
	set_pub_key_ctx->op_ctx.cb = gpca_cb;
	set_pub_key_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &set_pub_key_ctx->op_ctx);
	if (ret != 0) {
		kfree(set_pub_key_ctx);
		goto free_key_handle;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;

	/* Free key handle if set public key fails */
	if (ret != 0)
		goto free_key_handle;
	return ret;

free_key_handle:
	free_gpca_key_handle(*key_handle);
	*key_handle = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_set_public_key);

static int gpca_key_secure_import_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	u64 raw_dest_kp = 0;
	struct gpca_key_policy dest_kp = { 0 };
	struct gpca_key_handle *client_key_handle = NULL;
	struct gpca_key_handle *server_pub_key_handle = NULL;
	struct gpca_key_handle *dest_key_handle = NULL;
	bool salt_present = true;
	struct gpca_op_secure_key_import *secure_key_import_ctx = NULL;
	struct gpca_key_slot_node *slot = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_SECURE_KEY_IMPORT)
		return -EINVAL;

	secure_key_import_ctx =
		container_of(op_ctx, struct gpca_op_secure_key_import, op_ctx);
	client_key_handle = secure_key_import_ctx->client_key_handle;
	server_pub_key_handle = secure_key_import_ctx->server_pub_key_handle;
	dest_key_handle = secure_key_import_ctx->dest_key_handle;
	salt_present = (secure_key_import_ctx->salt_buf_size != 0);
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, client_key_handle);
	if (ret != 0)
		goto exit;
	ret = gpca_key_swap_in_key(op_ctx->gpca_dev, server_pub_key_handle);
	if (ret != 0)
		goto exit;
	slot = gpca_key_reserve_keyslot(op_ctx->gpca_dev, dest_key_handle);
	if (!slot) {
		ret = -EINVAL;
		goto exit;
	}

	ret = gpca_cmd_key_secure_import(
		op_ctx->gpca_dev, client_key_handle->keyslot,
		server_pub_key_handle->keyslot, slot->keyslot,
		salt_present, secure_key_import_ctx->include_key_policy,
		secure_key_import_ctx->dest_kp, secure_key_import_ctx->ctx_buf,
		secure_key_import_ctx->ctx_buf_size,
		secure_key_import_ctx->salt_buf,
		secure_key_import_ctx->salt_buf_size,
		secure_key_import_ctx->wrapped_key_buf,
		secure_key_import_ctx->wrapped_key_buf_size);
	if (ret != 0)
		goto unreserve_slot;

	if (dest_key_handle->is_swappable) {
		ret = gpca_cmd_key_wrap(
			op_ctx->gpca_dev, DOMAIN_KWK_KEYSLOT,
			slot->keyslot,
			dest_key_handle->wrapped_key_blob,
			sizeof(dest_key_handle->wrapped_key_blob),
			&dest_key_handle->wrapped_key_blob_size);
	}
	if (ret != 0)
		goto clear_key;
	/**
	 * If key policy is included in wrapped key blob,
	 * query the key policy to determine swappability
	 */
	if (secure_key_import_ctx->include_key_policy) {
		ret = gpca_cmd_get_key_policy(op_ctx->gpca_dev,
					      slot->keyslot,
					      &raw_dest_kp);
		if (ret == 0) {
			ret = gpca_raw_to_key_policy(raw_dest_kp, &dest_kp);
			if (ret == 0 && is_swappable(&dest_kp)) {
				/**
				 * If imported key is swappable,
				 * Wrap it with DOMAIN_KWK and move slot node
				 * to swappable slots list
				 */
				slot = gpca_key_mark_key_swappable(op_ctx->gpca_dev,
								   dest_key_handle,
								   slot);
				if (!slot) {
					ret = -EINVAL;
					goto exit;
				}
			}
		}
	}
	if (ret == 0) {
		gpca_key_assign_slot(op_ctx->gpca_dev, dest_key_handle, slot);
		goto exit;
	}
clear_key:
	gpca_cmd_key_clear(op_ctx->gpca_dev, slot->keyslot);
unreserve_slot:
	gpca_key_unreserve_keyslot(op_ctx->gpca_dev, slot);
exit:
	kfree(secure_key_import_ctx);
	return ret;
}

int gpca_key_secure_import(struct gpca_dev *ctx,
			   struct gpca_key_handle *client_key_handle,
			   struct gpca_key_handle *server_pub_key_handle,
			   struct gpca_key_handle **dest_key_handle,
			   bool include_key_policy,
			   const struct gpca_key_policy *key_policy,
			   const u8 *ctx_buf, u32 ctx_buf_size,
			   const u8 *salt_buf, u32 salt_buf_size,
			   const u8 *wrapped_key_buf, u32 wrapped_key_buf_size)
{
	int ret = 0;
	u64 raw_dest_kp = 0;
	struct gpca_op_secure_key_import *secure_key_import_ctx = NULL;
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	if (!ctx || !client_key_handle || !server_pub_key_handle ||
	    !dest_key_handle || !key_policy || !ctx_buf || ctx_buf_size == 0 ||
	    !wrapped_key_buf || wrapped_key_buf_size == 0)
		return -EINVAL;
	/* Salt is optional */
	if (!salt_buf && salt_buf_size != 0)
		return -EINVAL;

	/**
	 * Secure import of large key is not supported
	 * as domain has only one large keyslot
	 * and in case of secure key import that slot
	 * is occupied by server public key
	 */
	if ((!include_key_policy &&
	     is_large_key(ctx, key_policy)) ||
	    (include_key_policy &&
	     wrapped_key_buf_size == MAX_WRAPPED_KEY_SIZE_BYTES))
		return -EINVAL;

	ret = gpca_key_policy_to_raw(key_policy, &raw_dest_kp);
	if (ret != 0)
		return ret;

	*dest_key_handle = alloc_gpca_key_handle();
	if (*dest_key_handle == NULL)
		return -ENOMEM;

	if (!include_key_policy && is_swappable(key_policy)) {
		(*dest_key_handle)->is_swappable = true;
		(*dest_key_handle)->wrapping_keyslot = DOMAIN_KWK_KEYSLOT;
	} else {
		(*dest_key_handle)->is_swappable = false;
	}
	(*dest_key_handle)->is_large_key = false;
	(*dest_key_handle)->is_public_key = false;

	/* Queue secure key import operation */
	secure_key_import_ctx =
		kcalloc(1, sizeof(*secure_key_import_ctx), GFP_KERNEL);
	if (!secure_key_import_ctx) {
		ret = -ENOMEM;
		goto free_key_handle;
	}
	secure_key_import_ctx->op_ctx.gpca_dev = ctx;
	secure_key_import_ctx->op_ctx.op_type = GPCA_OP_TYPE_SECURE_KEY_IMPORT;
	secure_key_import_ctx->client_key_handle = client_key_handle;
	secure_key_import_ctx->server_pub_key_handle = server_pub_key_handle;
	secure_key_import_ctx->dest_key_handle = *dest_key_handle;
	secure_key_import_ctx->include_key_policy = include_key_policy;
	secure_key_import_ctx->dest_kp = raw_dest_kp;
	secure_key_import_ctx->ctx_buf = ctx_buf;
	secure_key_import_ctx->ctx_buf_size = ctx_buf_size;
	secure_key_import_ctx->salt_buf = salt_buf;
	secure_key_import_ctx->salt_buf_size = salt_buf_size;
	secure_key_import_ctx->wrapped_key_buf = wrapped_key_buf;
	secure_key_import_ctx->wrapped_key_buf_size = wrapped_key_buf_size;
	secure_key_import_ctx->op_ctx.op_handler =
		gpca_key_secure_import_async_handler;
	secure_key_import_ctx->op_ctx.cb = gpca_cb;
	secure_key_import_ctx->op_ctx.cb_ctx = &gpca_cb_ctx;

	init_completion(&gpca_cb_ctx.cpl);
	ret = gpca_op_queue_push_back(ctx, &secure_key_import_ctx->op_ctx);
	if (ret != 0) {
		kfree(secure_key_import_ctx);
		goto free_key_handle;
	}

	wait_for_completion(&gpca_cb_ctx.cpl);
	ret = gpca_cb_ctx.ret;

	/* Free key handle if set public key fails */
	if (ret != 0)
		goto free_key_handle;
	return ret;

free_key_handle:
	free_gpca_key_handle(*dest_key_handle);
	*dest_key_handle = NULL;
	return ret;
}
EXPORT_SYMBOL_GPL(gpca_key_secure_import);
