// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Crypto operations driver.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include "gpca_crypto.h"

#include "gpca_asymm.h"
#include <linux/dma-mapping.h>
#include <linux/mutex.h>

#include "gpca_cipher.h"
#include "gpca_cmd.h"
#include "gpca_hash.h"
#include "gpca_internal.h"
#include "gpca_keys_internal.h"
#include "gpca_op_queue.h"

#define DOMAIN_INVALID_OPSLOT (DOMAIN_MAX_OPSLOT + 1)
#define DOMAIN_INVALID_KEYSLOT (DOMAIN_MAX_KEYSLOT + 1)

struct gpca_op_set_crypto_params {
	struct gpca_crypto_op_handle *crypto_op_handle;
	const u8 *iv_buf;
	u32 iv_size;
	enum gpca_algorithm algo;
	enum gpca_supported_purpose purpose;
	struct gpca_op_context op_ctx;
};

struct gpca_op_start_crypto_op {
	struct gpca_crypto_op_handle *crypto_op_handle;
	dma_addr_t data_addr;
	dma_addr_t output_data_addr;
	u32 input_data_size;
	u32 aad_size;
	u32 unencrypted_size;
	u32 signature_size;
	u32 output_data_size;
	bool last;
	struct gpca_op_context op_ctx;
};

struct gpca_op_clear_crypto_op {
	struct gpca_crypto_op_handle *crypto_op_handle;
	struct gpca_op_context op_ctx;
};

static int swap_out_crypto_op(struct gpca_dev *gpca_dev,
			      struct gpca_op_slot_node *op_slot)
{
	int ret = 0;
	dma_addr_t op_ctx_addr = 0;
	int dma_error = 0;

	if (!op_slot->op_handle)
		return -EINVAL;

	op_ctx_addr =
		dma_map_single(gpca_dev->dev, op_slot->op_handle->op_ctx_buf,
			       GPCA_CMD_CRYPTO_OP_CTX_SIZE, DMA_FROM_DEVICE);
	dma_error = dma_mapping_error(gpca_dev->dev, op_ctx_addr);
	if (dma_error) {
		dev_err(gpca_dev->dev, "Couldn't DMA map op_ctx ret = %d\n",
			dma_error);
		return -EFAULT;
	}

	dev_dbg(gpca_dev->dev, "Swapping out crypto operation from slot %d",
		op_slot->opslot);

	/* Get the operation context in handle */
	ret = gpca_cmd_get_op_context(gpca_dev, op_slot->opslot, op_ctx_addr,
				      GPCA_CMD_CRYPTO_OP_CTX_SIZE);
	dma_unmap_single(gpca_dev->dev, op_ctx_addr,
			 GPCA_CMD_CRYPTO_OP_CTX_SIZE, DMA_FROM_DEVICE);
	if (ret != 0)
		return ret;

	ret = gpca_cmd_clear_opslot(gpca_dev, op_slot->opslot);
	if (ret != 0)
		return ret;

	/* Mark the op as swapped out i.e. GPCA_OP_IN_OP_CTX */
	op_slot->op_handle->op_state = GPCA_OP_IN_OP_CTX;
	op_slot->op_handle->opslot = DOMAIN_INVALID_OPSLOT;
	op_slot->op_handle = NULL;
	return ret;
}

static struct gpca_op_slot_node *reserve_opslot(struct gpca_dev *gpca_dev)
{
	int ret = 0;
	struct gpca_op_slot_node *op_slot_tail_node = NULL;

	/* Swap out tail node if needed */
	op_slot_tail_node = list_last_entry(&gpca_dev->gpca_crypto_op_slot_head,
					    struct gpca_op_slot_node,
					    op_slot_list);
	if (op_slot_tail_node->op_handle) {
		ret = swap_out_crypto_op(gpca_dev, op_slot_tail_node);
		if (ret != 0)
			return NULL;
	}

	/* Move the op_slots_tail node to head */
	list_move(&op_slot_tail_node->op_slot_list, &gpca_dev->gpca_crypto_op_slot_head);
	return op_slot_tail_node;
}

static void unreserve_opslot(struct gpca_dev *gpca_dev, struct gpca_op_slot_node *op_slot)
{
	list_move_tail(&op_slot->op_slot_list, &gpca_dev->gpca_crypto_op_slot_head);
}

static int swap_in_crypto_op(struct gpca_dev *gpca_dev,
			     struct gpca_crypto_op_handle *op_handle)
{
	int ret = 0;
	struct gpca_op_slot_node *op_slot = NULL;
	dma_addr_t op_ctx_addr = 0;
	int dma_error = 0;

	if (op_handle->op_state == GPCA_OP_IN_OP_CTX) {
		op_slot = reserve_opslot(gpca_dev);
		if (ret != 0)
			return ret;

		op_ctx_addr = dma_map_single(gpca_dev->dev,
					     op_handle->op_ctx_buf,
					     GPCA_CMD_CRYPTO_OP_CTX_SIZE,
					     DMA_TO_DEVICE);
		dma_error = dma_mapping_error(gpca_dev->dev, op_ctx_addr);
		if (dma_error) {
			dev_err(gpca_dev->dev, "Couldn't DMA map op_ctx ret = %d\n",
				dma_error);
			return -EFAULT;
		}

		dev_dbg(gpca_dev->dev,
			"Swapping in crypto operation to opslot %d",
			op_slot->opslot);
		/* Set the operation context into reserved opslot */
		ret = gpca_cmd_set_op_context(gpca_dev, op_slot->opslot,
					      op_ctx_addr,
					      GPCA_CMD_CRYPTO_OP_CTX_SIZE);
		dma_unmap_single(gpca_dev->dev, op_ctx_addr,
				 GPCA_CMD_CRYPTO_OP_CTX_SIZE, DMA_TO_DEVICE);
		if (ret == 0) {
			/* Assign the operation to reserved slot node */
			op_slot->op_handle = op_handle;
			op_handle->opslot = op_slot->opslot;
			op_handle->op_state = GPCA_OP_IN_OP_TABLE;
		}

	} else if (op_handle->op_state == GPCA_OP_IN_OP_TABLE) {
		op_slot = &((gpca_dev->op_slots)[op_handle->opslot -
						 DOMAIN_MIN_OPSLOT]);
		list_move(&op_slot->op_slot_list, &gpca_dev->gpca_crypto_op_slot_head);

	} else {
		/* Invalid crypto operation state */
		dev_err(gpca_dev->dev, "Invalid operation state %d for swap in",
			op_handle->op_state);
		return -EINVAL;
	}
	return ret;
}

static void free_opslot(struct gpca_dev *gpca_dev, u8 opslot)
{
	struct gpca_op_slot_node *slot = NULL;

	dev_dbg(gpca_dev->dev, "Freeing opslot %d", opslot);
	/* Move opslot to tail and mark free */
	slot = &((gpca_dev->op_slots)[opslot - DOMAIN_MIN_OPSLOT]);
	if (slot->op_handle) {
		slot->op_handle->opslot = DOMAIN_INVALID_OPSLOT;
		slot->op_handle->op_state = GPCA_OP_UNINITIALIZED;
	} else {
		dev_warn(gpca_dev->dev, "Freeing empty opslot %d", opslot);
	}
	slot->op_handle = NULL;
	list_move_tail(&slot->op_slot_list, &gpca_dev->gpca_crypto_op_slot_head);
}

int gpca_crypto_init(struct gpca_dev *gpca_dev)
{
	u8 opslot = 0;
	u32 idx = 0;
	int ret = 0;

	INIT_LIST_HEAD(&gpca_dev->gpca_crypto_op_slot_head);
	for (opslot = DOMAIN_MIN_OPSLOT, idx = 0; opslot <= DOMAIN_MAX_OPSLOT;
	     opslot++, idx++) {
		gpca_dev->op_slots[idx].opslot = opslot;
		gpca_dev->op_slots[idx].op_handle = NULL;
		list_add_tail(&gpca_dev->op_slots[idx].op_slot_list,
			      &gpca_dev->gpca_crypto_op_slot_head);
	}

	ret = gpca_hash_register(gpca_dev);
	if (ret != 0) {
		dev_err(gpca_dev->dev, "GPCA hash registration failed with ret = %d\n",
			ret);
		return ret;
	}

	ret = gpca_cipher_register(gpca_dev);
	if (ret != 0) {
		dev_err(gpca_dev->dev,
			"GPCA cipher registration failed with ret = %d\n", ret);
		return ret;
	}

	ret = gpca_asymm_register(gpca_dev);
	if (ret != 0)
		dev_err(gpca_dev->dev,
			"GPCA asymmetric algorithm registration failed ret = %d\n",
			ret);

	return ret;
}

void gpca_crypto_deinit(struct gpca_dev *gpca_dev)
{
	gpca_hash_unregister(gpca_dev);
	gpca_cipher_unregister(gpca_dev);
	gpca_asymm_unregister(gpca_dev);
}

struct gpca_crypto_op_handle *gpca_crypto_op_handle_alloc(void)
{
	struct gpca_crypto_op_handle *op_handle = NULL;

	op_handle = kcalloc(1, sizeof(*op_handle), GFP_KERNEL);
	if (op_handle)
		op_handle->opslot = DOMAIN_INVALID_OPSLOT;

	return op_handle;
}
EXPORT_SYMBOL_GPL(gpca_crypto_op_handle_alloc);

void gpca_crypto_op_handle_free(struct gpca_crypto_op_handle *crypto_op_handle)
{
	WARN_ON(crypto_op_handle && crypto_op_handle->op_state != GPCA_OP_UNINITIALIZED);

	kfree(crypto_op_handle);
}
EXPORT_SYMBOL_GPL(gpca_crypto_op_handle_free);

/**
 * Only one GPCA operation async handler runs at any given time.
 * Ordered workqueue executes the handler.
 * Hence, no synchronization is needed for operation slot
 * management.
 */
static int
gpca_crypto_set_crypto_params_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_set_crypto_params *set_crypto_params_ctx = NULL;
	struct gpca_crypto_op_handle *crypto_op_handle = NULL;
	struct gpca_key_handle *key_handle = NULL;
	u8 keyslot = DOMAIN_MAX_KEYSLOT;
	enum gpca_algorithm algo;
	enum gpca_cmd_crypto_op_type op_type;
	struct gpca_op_slot_node *op_slot = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_SET_CRYPTO_PARAMS)
		return -EINVAL;

	set_crypto_params_ctx =
		container_of(op_ctx, struct gpca_op_set_crypto_params, op_ctx);
	crypto_op_handle = set_crypto_params_ctx->crypto_op_handle;
	key_handle = crypto_op_handle->key_handle;
	algo = set_crypto_params_ctx->algo;

	if (crypto_op_handle->op_state != GPCA_OP_UNINITIALIZED)
		return -EINVAL;

	switch (set_crypto_params_ctx->purpose) {
	case GPCA_SUPPORTED_PURPOSE_ENCRYPT:
		op_type = GPCA_CMD_CRYPTO_OP_ENCRYPT;
		break;
	case GPCA_SUPPORTED_PURPOSE_DECRYPT:
		op_type = GPCA_CMD_CRYPTO_OP_DECRYPT;
		break;
	case GPCA_SUPPORTED_PURPOSE_SIGN:
		op_type = GPCA_CMD_CRYPTO_OP_SIGN;
		break;
	case GPCA_SUPPORTED_PURPOSE_VERIFY:
		op_type = GPCA_CMD_CRYPTO_OP_VERIFY;
		break;
	default:
		ret = -EINVAL;
		goto exit;
	}

	if (key_handle) {
		ret = gpca_key_swap_in_key(op_ctx->gpca_dev, key_handle);
		if (ret != 0)
			goto exit;
		keyslot = key_handle->keyslot;
	}

	op_slot = reserve_opslot(op_ctx->gpca_dev);
	if (!op_slot)
		goto exit;

	if (algo == GPCA_ALGO_SHA2_224 || algo == GPCA_ALGO_SHA2_256 ||
	    algo == GPCA_ALGO_SHA2_384 || algo == GPCA_ALGO_SHA2_512)
		ret = gpca_cmd_set_crypto_hash_params(op_ctx->gpca_dev,
						      op_slot->opslot,
						      (enum gpca_cmd_crypto_algo)algo);
	else if (algo == GPCA_ALGO_HMAC_SHA2_224 ||
		 algo == GPCA_ALGO_HMAC_SHA2_256 ||
		 algo == GPCA_ALGO_HMAC_SHA2_384 ||
		 algo == GPCA_ALGO_HMAC_SHA2_512)
		ret = gpca_cmd_set_crypto_hmac_params(op_ctx->gpca_dev,
						      op_slot->opslot,
						      keyslot,
						      (enum gpca_cmd_crypto_algo)algo, op_type);
	else if (algo == GPCA_ALGO_AES128_CBC || algo == GPCA_ALGO_AES128_CTR ||
		 algo == GPCA_ALGO_AES128_ECB || algo == GPCA_ALGO_AES128_GCM ||
		 algo == GPCA_ALGO_AES192_CBC || algo == GPCA_ALGO_AES192_CTR ||
		 algo == GPCA_ALGO_AES192_ECB || algo == GPCA_ALGO_AES192_GCM ||
		 algo == GPCA_ALGO_AES256_CBC || algo == GPCA_ALGO_AES256_CTR ||
		 algo == GPCA_ALGO_AES256_ECB || algo == GPCA_ALGO_AES256_GCM ||
		 algo == GPCA_ALGO_TDES_CBC || algo == GPCA_ALGO_TDES_ECB)
		ret = gpca_cmd_set_crypto_symm_algo_params(op_ctx->gpca_dev,
							   op_slot->opslot,
							   keyslot,
							   (enum gpca_cmd_crypto_algo)algo,
							   op_type,
							   set_crypto_params_ctx->iv_buf,
							   set_crypto_params_ctx->iv_size);
	else if (algo == GPCA_ALGO_ECC_P224 || algo == GPCA_ALGO_ECC_P256 ||
		 algo == GPCA_ALGO_ECC_P384 || algo == GPCA_ALGO_ECC_P521 ||
		 algo == GPCA_ALGO_ECC_ED25519 || algo == GPCA_ALGO_RSA_2048 ||
		 algo == GPCA_ALGO_RSA_3072 || algo == GPCA_ALGO_RSA_4096)
		ret = gpca_cmd_set_crypto_asymm_algo_params(op_ctx->gpca_dev,
							    op_slot->opslot,
							    keyslot,
							    (enum gpca_cmd_crypto_algo)algo,
							    op_type);
	else
		ret = -EINVAL;

	if (ret == 0) {
		/* Assign the operation to reserved slot node */
		op_slot->op_handle = crypto_op_handle;
		crypto_op_handle->opslot = op_slot->opslot;
		crypto_op_handle->op_state = GPCA_OP_IN_OP_TABLE;
		dev_dbg(op_ctx->gpca_dev->dev, "Reserved slot %d for crypto operation",
			crypto_op_handle->opslot);
	} else {
		unreserve_opslot(op_ctx->gpca_dev, op_slot);
	}

exit:
	kfree(set_crypto_params_ctx);
	return ret;
}

static int gpca_crypto_set_crypto_params(struct gpca_dev *ctx,
					 struct gpca_crypto_op_handle *crypto_op_handle,
					 struct gpca_key_handle *key_handle,
					 enum gpca_algorithm algo,
					 enum gpca_supported_purpose purpose,
					 const u8 *iv_buf, u32 iv_size,
					 gpca_crypto_cb cb, void *cb_ctx)
{
	int ret;
	struct gpca_op_set_crypto_params *set_crypto_params_ctx = NULL;

	if (!ctx || !crypto_op_handle)
		return -EINVAL;

	/* Queue set crypto params operation */
	set_crypto_params_ctx =
		kcalloc(1, sizeof(*set_crypto_params_ctx), GFP_KERNEL);
	if (!set_crypto_params_ctx)
		return -ENOMEM;

	crypto_op_handle->key_handle = key_handle;
	set_crypto_params_ctx->op_ctx.gpca_dev = ctx;
	set_crypto_params_ctx->op_ctx.op_type = GPCA_OP_TYPE_SET_CRYPTO_PARAMS;
	set_crypto_params_ctx->crypto_op_handle = crypto_op_handle;
	set_crypto_params_ctx->algo = algo;
	set_crypto_params_ctx->purpose = purpose;
	set_crypto_params_ctx->iv_buf = iv_buf;
	set_crypto_params_ctx->iv_size = iv_size;
	set_crypto_params_ctx->op_ctx.op_handler =
		gpca_crypto_set_crypto_params_async_handler;
	set_crypto_params_ctx->op_ctx.cb = cb;
	set_crypto_params_ctx->op_ctx.cb_ctx = cb_ctx;

	ret = gpca_op_queue_push_back(ctx, &set_crypto_params_ctx->op_ctx);
	if (ret)
		kfree(set_crypto_params_ctx);

	return ret;
}

int gpca_crypto_set_hash_params(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *op_handle,
				enum gpca_algorithm hash_algo,
				gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_set_crypto_params(ctx, op_handle, NULL, hash_algo,
					     GPCA_SUPPORTED_PURPOSE_SIGN, NULL,
					     0, cb, cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_set_hash_params);

int gpca_crypto_set_hmac_params(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *op_handle,
				struct gpca_key_handle *key_handle,
				enum gpca_algorithm hmac_algo,
				enum gpca_supported_purpose purpose,
				gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_set_crypto_params(ctx, op_handle, key_handle,
					     hmac_algo, purpose, NULL, 0,
					     cb, cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_set_hmac_params);

int gpca_crypto_set_symm_algo_params(struct gpca_dev *ctx,
				     struct gpca_crypto_op_handle *op_handle,
				     struct gpca_key_handle *key_handle,
				     enum gpca_algorithm algo,
				     enum gpca_supported_purpose purpose,
				     const u8 *iv_buf, u32 iv_size,
				     gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_set_crypto_params(ctx, op_handle, key_handle, algo,
					     purpose, iv_buf, iv_size, cb,
					     cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_set_symm_algo_params);

int gpca_crypto_set_asymm_algo_params(struct gpca_dev *ctx,
				      struct gpca_crypto_op_handle *op_handle,
				      struct gpca_key_handle *key_handle,
				      enum gpca_algorithm algo,
				      enum gpca_supported_purpose purpose,
				      gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_set_crypto_params(ctx, op_handle, key_handle, algo,
					     purpose, NULL, 0, cb, cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_set_asymm_algo_params);

/**
 * Only one GPCA operation async handler runs at any given time.
 * Ordered workqueue executes the handler.
 * Hence, no synchronization is needed for operation slot
 * management.
 */
static int
gpca_crypto_start_crypto_op_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_start_crypto_op *start_crypto_op_ctx = NULL;
	struct gpca_crypto_op_handle *crypto_op_handle = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_START_CRYPTO_OP)
		return -EINVAL;

	start_crypto_op_ctx =
		container_of(op_ctx, struct gpca_op_start_crypto_op, op_ctx);
	crypto_op_handle = start_crypto_op_ctx->crypto_op_handle;

	if (crypto_op_handle->op_state == GPCA_OP_UNINITIALIZED)
		return -EINVAL;

	if (crypto_op_handle->key_handle) {
		ret = gpca_key_swap_in_key(op_ctx->gpca_dev,
					   crypto_op_handle->key_handle);
		if (ret != 0)
			goto exit;
	}
	ret = swap_in_crypto_op(op_ctx->gpca_dev, crypto_op_handle);
	if (ret != 0)
		goto exit;

	ret = gpca_cmd_start_crypto_op_ptrs(op_ctx->gpca_dev, crypto_op_handle->opslot,
					    start_crypto_op_ctx->data_addr,
					    start_crypto_op_ctx->input_data_size,
					    start_crypto_op_ctx->aad_size,
					    start_crypto_op_ctx->signature_size,
					    start_crypto_op_ctx->unencrypted_size,
					    start_crypto_op_ctx->output_data_addr,
					    start_crypto_op_ctx->output_data_size,
					    false /* FWD is not supported */,
					    start_crypto_op_ctx->last);
exit:
	kfree(start_crypto_op_ctx);
	return ret;
}

static int gpca_crypto_start_crypto_op(struct gpca_dev *ctx,
				       struct gpca_crypto_op_handle *crypto_op_handle,
				       dma_addr_t data_addr, u32 input_data_size,
				       u32 aad_size, u32 unencrypted_size,
				       dma_addr_t output_data_addr, u32 output_data_size,
				       bool last, u32 signature_size,
				       gpca_crypto_cb cb, void *cb_ctx)
{
	int ret = 0;
	struct gpca_op_start_crypto_op *start_crypto_op_ctx = NULL;

	if (!ctx || !crypto_op_handle)
		return -EINVAL;

	/* Queue start crypto op operation */
	start_crypto_op_ctx =
		kcalloc(1, sizeof(*start_crypto_op_ctx), GFP_KERNEL);
	if (!start_crypto_op_ctx)
		return -ENOMEM;

	start_crypto_op_ctx->op_ctx.gpca_dev = ctx;
	start_crypto_op_ctx->op_ctx.op_type = GPCA_OP_TYPE_START_CRYPTO_OP;
	start_crypto_op_ctx->crypto_op_handle = crypto_op_handle;
	start_crypto_op_ctx->data_addr = data_addr;
	start_crypto_op_ctx->input_data_size = input_data_size;
	start_crypto_op_ctx->aad_size = aad_size;
	start_crypto_op_ctx->unencrypted_size = unencrypted_size;
	start_crypto_op_ctx->output_data_addr = output_data_addr;
	start_crypto_op_ctx->output_data_size = output_data_size;
	start_crypto_op_ctx->last = last;
	start_crypto_op_ctx->signature_size = signature_size;
	start_crypto_op_ctx->op_ctx.op_handler =
		gpca_crypto_start_crypto_op_async_handler;
	start_crypto_op_ctx->op_ctx.cb = cb;
	start_crypto_op_ctx->op_ctx.cb_ctx = cb_ctx;

	ret = gpca_op_queue_push_back(ctx, &start_crypto_op_ctx->op_ctx);
	if (ret)
		kfree(start_crypto_op_ctx);

	return ret;
}

int gpca_crypto_start_digest_op(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *op_handle,
				dma_addr_t input_data_addr, u32 input_data_size,
				dma_addr_t output_data_addr,
				u32 output_data_size, bool last,
				gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_start_crypto_op(ctx, op_handle, input_data_addr,
					   input_data_size, 0, 0,
					   output_data_addr, output_data_size,
					   last, 0, cb, cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_start_digest_op);

int gpca_crypto_start_symmetric_op(struct gpca_dev *ctx,
				   struct gpca_crypto_op_handle *op_handle,
				   dma_addr_t data_addr, u32 input_data_size,
				   u32 aad_size, u32 unencrypted_size,
				   dma_addr_t output_data_addr,
				   u32 output_data_size, bool last,
				   gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_start_crypto_op(ctx, op_handle, data_addr,
					   input_data_size, aad_size,
					   unencrypted_size, output_data_addr,
					   output_data_size, last, 0,
					   cb, cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_start_symmetric_op);

int gpca_crypto_start_sign_op(struct gpca_dev *ctx,
			      struct gpca_crypto_op_handle *op_handle,
			      dma_addr_t data_addr, u32 input_data_size,
			      dma_addr_t signature_addr, u32 signature_size,
			      bool last, gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_start_crypto_op(ctx, op_handle, data_addr,
					   input_data_size, 0, 0,
					   signature_addr, signature_size, last,
					   0, cb, cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_start_sign_op);

int gpca_crypto_start_verify_op(struct gpca_dev *ctx,
				struct gpca_crypto_op_handle *op_handle,
				dma_addr_t data_addr, u32 input_data_size,
				u32 signature_size, bool last,
				gpca_crypto_cb cb, void *cb_ctx)
{
	return gpca_crypto_start_crypto_op(ctx, op_handle, data_addr,
					   input_data_size, 0, 0, 0, 0, last,
					   signature_size, cb, cb_ctx);
}
EXPORT_SYMBOL_GPL(gpca_crypto_start_verify_op);

/**
 * Only one GPCA operation async handler runs at any given time.
 * Ordered workqueue executes the handler.
 * Hence, no synchronization is needed for operation slot
 * management.
 */
static int
gpca_crypto_clear_op_async_handler(struct gpca_op_context *op_ctx)
{
	int ret = 0;
	struct gpca_op_clear_crypto_op *clear_crypto_op_ctx = NULL;
	struct gpca_crypto_op_handle *crypto_op_handle = NULL;

	if (!op_ctx || op_ctx->op_type != GPCA_OP_TYPE_CLEAR_CRYPTO_OP)
		return -EINVAL;

	clear_crypto_op_ctx =
		container_of(op_ctx, struct gpca_op_clear_crypto_op, op_ctx);
	crypto_op_handle = clear_crypto_op_ctx->crypto_op_handle;

	if (crypto_op_handle->op_state == GPCA_OP_UNINITIALIZED)
		return -EINVAL;

	if (crypto_op_handle->op_state == GPCA_OP_IN_OP_CTX) {
		crypto_op_handle->opslot = DOMAIN_INVALID_OPSLOT;
		crypto_op_handle->op_state = GPCA_OP_UNINITIALIZED;
		ret = 0;
		goto exit;
	}
	ret = gpca_cmd_clear_opslot(op_ctx->gpca_dev, crypto_op_handle->opslot);
	if (ret == 0)
		free_opslot(op_ctx->gpca_dev, crypto_op_handle->opslot);

exit:
	if (ret == 0)
		crypto_op_handle->key_handle = NULL;

	kfree(clear_crypto_op_ctx);
	return ret;
}

int gpca_crypto_clear_op(struct gpca_dev *ctx,
			 struct gpca_crypto_op_handle *crypto_op_handle,
			 gpca_crypto_cb cb, void *cb_ctx)
{
	int ret = 0;
	struct gpca_op_clear_crypto_op *clear_crypto_op_ctx = NULL;

	if (!ctx || !crypto_op_handle)
		return -EINVAL;

	/* Queue clear crypto op operation */
	clear_crypto_op_ctx =
		kcalloc(1, sizeof(*clear_crypto_op_ctx), GFP_KERNEL);
	if (!clear_crypto_op_ctx)
		return -ENOMEM;

	clear_crypto_op_ctx->op_ctx.gpca_dev = ctx;
	clear_crypto_op_ctx->op_ctx.op_type = GPCA_OP_TYPE_CLEAR_CRYPTO_OP;
	clear_crypto_op_ctx->crypto_op_handle = crypto_op_handle;
	clear_crypto_op_ctx->op_ctx.op_handler =
		gpca_crypto_clear_op_async_handler;
	clear_crypto_op_ctx->op_ctx.cb = cb;
	clear_crypto_op_ctx->op_ctx.cb_ctx = cb_ctx;

	ret = gpca_op_queue_push_back(ctx, &clear_crypto_op_ctx->op_ctx);
	if (ret)
		kfree(clear_crypto_op_ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(gpca_crypto_clear_op);
