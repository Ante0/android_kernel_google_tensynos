// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Linux kernel crypto Hash implementation.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include "gpca_cipher.h"

#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/des.h>
#include <crypto/gcm.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <linux/dma-mapping.h>
#include <linux/minmax.h>
#include <linux/types.h>

#include "gpca_crypto.h"
#include "gpca_internal.h"
#include "gpca_key_policy.h"

#define GOOGLE_GPCA_AES_PRIORITY 300
#define SCATTERWALK_TO_SG 1
#define SCATTERWALK_FROM_SG 0

struct gpca_aead_alg {
	struct gpca_dev *gpca_dev;
	struct aead_alg aead_alg;
};

struct gpca_aead_ctx {
	struct gpca_dev *gpca_dev;
	struct gpca_key_handle *key_handle;
	enum gpca_algorithm gpca_algo;
};

struct gpca_aead_op_ctx {
	struct gpca_dev *gpca_dev;
	struct aead_request *req;
	struct gpca_crypto_op_handle *op_handle;
	u8 *src_buf;
	u32 src_buf_len;
	dma_addr_t src_dma_addr;
	u8 *dst_buf;
	u32 dst_buf_len;
	dma_addr_t dst_dma_addr;
	int prev_op_ret;
	u32 total_msg_len;
	u32 src_buf_offset;
	u32 dst_buf_offset;
	bool encrypt;
	bool first_op;
};

struct gpca_skcipher_op_ctx {
	struct gpca_dev *gpca_dev;
	struct skcipher_request *req;
	struct gpca_crypto_op_handle *op_handle;
	u8 *src_buf;
	u32 src_buf_len;
	dma_addr_t src_dma_addr;
	u8 *dst_buf;
	u32 dst_buf_len;
	dma_addr_t dst_dma_addr;
	int prev_op_ret;
	u32 total_msg_len;
	u32 src_buf_offset;
	u32 dst_buf_offset;
	bool encrypt;
};

enum gpca_skcipher_algo {
	GPCA_SKCIPHER_AES_ECB,
	GPCA_SKCIPHER_AES_CBC,
	GPCA_SKCIPHER_AES_CTR,
	GPCA_SKCIPHER_TDES_ECB,
	GPCA_SKCIPHER_TDES_CBC,
};

struct gpca_skcipher_alg {
	struct gpca_dev *gpca_dev;
	enum gpca_skcipher_algo algo;
	struct skcipher_alg skcipher_alg;
};

struct gpca_skcipher_ctx {
	struct gpca_dev *gpca_dev;
	struct gpca_key_handle *key_handle;
	enum gpca_skcipher_algo skcipher_algo;
	enum gpca_algorithm gpca_algo;
};

static struct gpca_aead_alg gpca_aead_algs[] = {
	{
	.aead_alg = {
		.base.cra_name = "gcm(aes)",
		.base.cra_driver_name = "google-gpca-aes-gcm",
		.ivsize = GCM_AES_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
	},
	}
};

static struct gpca_skcipher_alg gpca_skcipher_algs[] = {
	{
	.algo = GPCA_SKCIPHER_AES_ECB,
	.skcipher_alg = {
		.base.cra_name = "ecb(aes)",
		.base.cra_driver_name = "google-gpca-aes-ecb",
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.ivsize = 0,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
	}
	},
	{
	.algo = GPCA_SKCIPHER_AES_CBC,
	.skcipher_alg = {
		.base.cra_name = "cbc(aes)",
		.base.cra_driver_name = "google-gpca-aes-cbc",
		.base.cra_blocksize = AES_BLOCK_SIZE,
		.ivsize = AES_BLOCK_SIZE,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
	}
	},
	{
	.algo = GPCA_SKCIPHER_AES_CTR,
	.skcipher_alg = {
		.base.cra_name = "ctr(aes)",
		.base.cra_driver_name = "google-gpca-aes-ctr",
		.base.cra_blocksize = 1,
		.ivsize = AES_BLOCK_SIZE,
		.min_keysize = AES_MIN_KEY_SIZE,
		.max_keysize = AES_MAX_KEY_SIZE,
	}
	},
	{
	.algo = GPCA_SKCIPHER_TDES_ECB,
	.skcipher_alg = {
		.base.cra_name = "ecb(des3_ede)",
		.base.cra_driver_name = "google-gpca-tdes-ecb",
		.base.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.ivsize = 0,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
	}
	},
	{
	.algo = GPCA_SKCIPHER_TDES_CBC,
	.skcipher_alg = {
		.base.cra_name = "cbc(des3_ede)",
		.base.cra_driver_name = "google-gpca-tdes-cbc",
		.base.cra_blocksize = DES3_EDE_BLOCK_SIZE,
		.ivsize = DES3_EDE_BLOCK_SIZE,
		.min_keysize = DES3_EDE_KEY_SIZE,
		.max_keysize = DES3_EDE_KEY_SIZE,
	}
	},
};

static int gpca_aead_queue_request(struct gpca_aead_op_ctx *op_ctx);
static int gpca_skcipher_queue_request(struct gpca_skcipher_op_ctx *op_ctx);
static void gpca_skcipher_init_cb(int ret, void *cb_ctx);

static void reverse_buffer_in_place(u8 *buf, u32 len)
{
	u8 temp = 0;
	u32 start_idx = 0;
	u32 end_idx = len - 1;

	while (start_idx < end_idx) {
		temp = buf[end_idx];
		buf[end_idx--] = buf[start_idx];
		buf[start_idx++] = temp;
	}
}

static int gpca_aead_init(struct crypto_aead *tfm)
{
	struct gpca_aead_ctx *tfm_ctx = crypto_aead_ctx(tfm);
	struct aead_alg *aead_alg =
		container_of(tfm->base.__crt_alg, struct aead_alg, base);
	struct gpca_aead_alg *gpca_aead_alg =
		container_of(aead_alg, struct gpca_aead_alg, aead_alg);

	tfm_ctx->gpca_dev = gpca_aead_alg->gpca_dev;
	tfm_ctx->key_handle = NULL;

	return 0;
}

static void gpca_aead_exit(struct crypto_aead *tfm)
{
	struct gpca_aead_ctx *tfm_ctx = crypto_aead_ctx(tfm);
	int ret = 0;

	if (tfm_ctx->key_handle)
		ret = gpca_key_clear(tfm_ctx->gpca_dev, &tfm_ctx->key_handle);

	if (ret)
		dev_err(tfm_ctx->gpca_dev->dev,
			"Key clear failed with ret = %d", ret);
	tfm_ctx->gpca_dev = NULL;
	tfm_ctx->key_handle = NULL;
}

static int gpca_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	return authsize == AES_BLOCK_SIZE ? 0 : -EINVAL;
}

static int gpca_aead_setkey(struct crypto_aead *tfm, const u8 *key,
			    unsigned int keylen)
{
	int ret = 0;
	struct gpca_aead_ctx *tfm_ctx = crypto_aead_ctx(tfm);
	struct gpca_key_policy aes_gcm_kp = {
		GPCA_KEY_CLASS_PORTABLE,
		GPCA_ALGO_AES128_GCM,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_ENABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_DISABLE,
		GPCA_EVICT_AP_S_DISABLE,
		GPCA_EVICT_AP_NS_DISABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};

	if (tfm_ctx->key_handle) {
		ret = gpca_key_clear(tfm_ctx->gpca_dev, &tfm_ctx->key_handle);
		if (ret)
			return ret;
	}

	switch (keylen) {
	case AES_KEYSIZE_128:
		tfm_ctx->gpca_algo = GPCA_ALGO_AES128_GCM;
		break;
	case AES_KEYSIZE_192:
		tfm_ctx->gpca_algo = GPCA_ALGO_AES192_GCM;
		break;
	case AES_KEYSIZE_256:
		tfm_ctx->gpca_algo = GPCA_ALGO_AES256_GCM;
		break;
	default:
		return -EINVAL;
	}

	aes_gcm_kp.algo = tfm_ctx->gpca_algo;

	ret = gpca_key_import(tfm_ctx->gpca_dev, &tfm_ctx->key_handle,
			      &aes_gcm_kp, key, keylen);
	dev_dbg(tfm_ctx->gpca_dev->dev, "Import key for algo = %d ret = %d",
		tfm_ctx->gpca_algo, ret);

	return ret;
}

static void gpca_aead_cleanup_and_complete_req(struct gpca_aead_op_ctx *op_ctx, int ret)
{
	struct aead_request *req = op_ctx->req;
	int prev_op_ret = op_ctx->prev_op_ret;

	gpca_crypto_op_handle_free(op_ctx->op_handle);
	kfree(op_ctx);
	aead_request_complete(req, prev_op_ret ? prev_op_ret : ret);
}

static void gpca_aead_clear_op_cb(int ret, void *cb_ctx)
{
	struct gpca_aead_op_ctx *op_ctx = (struct gpca_aead_op_ctx *)cb_ctx;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op ret = %d", ret);

	gpca_aead_cleanup_and_complete_req(op_ctx, ret);
}

static void gpca_aead_op_cb(int ret, void *cb_ctx)
{
	struct gpca_aead_op_ctx *op_ctx = (struct gpca_aead_op_ctx *)cb_ctx;
	struct aead_request *req = NULL;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	req = op_ctx->req;
	dev_dbg(op_ctx->gpca_dev->dev,
		"GPCA AEAD operation callback ret = %d", ret);

	if (op_ctx->first_op) {
		op_ctx->total_msg_len -=
			(op_ctx->src_buf_len - req->assoclen);
		op_ctx->first_op = false;
	} else {
		op_ctx->total_msg_len -= op_ctx->src_buf_len;
	}

	if (op_ctx->src_buf_len)
		dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
				  op_ctx->src_buf, op_ctx->src_dma_addr);

	if (ret == 0 && op_ctx->dst_buf_len)
		scatterwalk_map_and_copy(op_ctx->dst_buf, op_ctx->req->dst,
					 op_ctx->dst_buf_offset,
					 op_ctx->dst_buf_len,
					 SCATTERWALK_TO_SG);

	if (op_ctx->dst_buf_len)
		dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->dst_buf_len,
				  op_ctx->dst_buf, op_ctx->dst_dma_addr);
	op_ctx->src_buf_offset += op_ctx->src_buf_len;
	op_ctx->dst_buf_offset += op_ctx->dst_buf_len;

	if (op_ctx->total_msg_len == 0 || ret) {
		op_ctx->prev_op_ret = ret;
		ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
					   gpca_aead_clear_op_cb, op_ctx);
		dev_dbg(op_ctx->gpca_dev->dev, "Clear GPCA crypto op ret = %d", ret);
		if (ret)
			gpca_aead_cleanup_and_complete_req(op_ctx, ret);
	} else {
		gpca_aead_queue_request(op_ctx);
	}
}

static int gpca_aead_queue_request(struct gpca_aead_op_ctx *op_ctx)
{
	int ret = 0;
	struct crypto_aead *tfm = crypto_aead_reqtfm(op_ctx->req);
	u32 auth_tag_size = crypto_aead_authsize(tfm);
	bool last = false;

	/* The first AES GCM message chunk will also contain AAD */
	if (op_ctx->first_op)
		op_ctx->src_buf_len =
			min_t(u32, KMALLOC_MAX_SIZE - op_ctx->req->assoclen,
			      op_ctx->total_msg_len);
	else
		op_ctx->src_buf_len =
			min_t(u32, KMALLOC_MAX_SIZE, op_ctx->total_msg_len);
	last = (op_ctx->src_buf_len == op_ctx->total_msg_len);

	/* Intermediate AES GCM messages should be block size aligned */
	if (!last)
		op_ctx->src_buf_len -= op_ctx->src_buf_len % AES_BLOCK_SIZE;

	op_ctx->dst_buf_len = op_ctx->src_buf_len;
	/**
	 * GPCA AES GCM operations.
	 * Encrypt: Input = AAD || Plaintext         , Output = Ciphertext || Tag
	 * Decrypt: Input = AAD || Ciphertext || Tag , Output = Plaintext
	 */
	if (last)
		op_ctx->dst_buf_len =
			op_ctx->encrypt ? op_ctx->dst_buf_len + auth_tag_size :
					  op_ctx->dst_buf_len - auth_tag_size;

	if (op_ctx->first_op)
		op_ctx->src_buf_len += op_ctx->req->assoclen;

	if (op_ctx->src_buf_len) {
		op_ctx->src_buf = dma_alloc_coherent(op_ctx->gpca_dev->dev,
						     op_ctx->src_buf_len,
						     &op_ctx->src_dma_addr,
						     GFP_KERNEL);
		if (!op_ctx->src_buf) {
			ret = -ENOMEM;
			goto error;
		}
	} else {
		op_ctx->src_buf = NULL;
		op_ctx->src_dma_addr = 0;
	}

	if (op_ctx->dst_buf_len) {
		op_ctx->dst_buf = dma_alloc_coherent(op_ctx->gpca_dev->dev,
						     op_ctx->dst_buf_len,
						     &op_ctx->dst_dma_addr,
						     GFP_KERNEL);
		if (!op_ctx->dst_buf) {
			ret = -ENOMEM;
			goto free_src_buf;
		}

	} else {
		op_ctx->dst_buf = NULL;
		op_ctx->dst_dma_addr = 0;
	}

	scatterwalk_map_and_copy(op_ctx->src_buf, op_ctx->req->src,
				 op_ctx->src_buf_offset, op_ctx->src_buf_len,
				 SCATTERWALK_FROM_SG);

	/* Copy AAD from src to dst */
	if (op_ctx->first_op && op_ctx->req->assoclen) {
		scatterwalk_map_and_copy(op_ctx->src_buf, op_ctx->req->dst,
					 op_ctx->dst_buf_offset,
					 op_ctx->req->assoclen,
					 SCATTERWALK_TO_SG);
		op_ctx->dst_buf_offset += op_ctx->req->assoclen;
	}

	ret = gpca_crypto_start_symmetric_op(op_ctx->gpca_dev,
					     op_ctx->op_handle, op_ctx->src_dma_addr,
					     op_ctx->first_op ?
						op_ctx->src_buf_len - op_ctx->req->assoclen :
						op_ctx->src_buf_len,
					     op_ctx->first_op ? op_ctx->req->assoclen : 0, 0,
					     op_ctx->dst_dma_addr, op_ctx->dst_buf_len, last,
					     gpca_aead_op_cb, op_ctx);
	dev_dbg(op_ctx->gpca_dev->dev,
		"GPCA AEAD crypto op AAD length = %d input data length = %d output data length = %d last = %d ret = %d",
		op_ctx->first_op ? op_ctx->req->assoclen : 0,
		op_ctx->first_op ? op_ctx->src_buf_len - op_ctx->req->assoclen :
				   op_ctx->src_buf_len,
		op_ctx->dst_buf_len, last, ret);

	if (ret == 0)
		return ret;

	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->dst_buf_len,
			  op_ctx->dst_buf, op_ctx->dst_dma_addr);
free_src_buf:
	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
			  op_ctx->src_buf, op_ctx->src_dma_addr);
error:
	op_ctx->prev_op_ret = ret;
	ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
				   gpca_aead_clear_op_cb, op_ctx);
	dev_dbg(op_ctx->gpca_dev->dev, "Clear GPCA crypto op ret = %d", ret);
	if (ret)
		gpca_aead_cleanup_and_complete_req(op_ctx, ret);

	return ret;
}

static void gpca_aead_init_cb(int ret, void *cb_ctx)
{
	struct gpca_aead_op_ctx *op_ctx = (struct gpca_aead_op_ctx *)cb_ctx;
	struct aead_request *req = NULL;
	struct crypto_aead *tfm = NULL;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	req = op_ctx->req;
	tfm = crypto_aead_reqtfm(req);

	dev_dbg(op_ctx->gpca_dev->dev,
		"GPCA AEAD init callback ret = %d", ret);
	if (req->iv && op_ctx->gpca_dev->reversed_iv)
		reverse_buffer_in_place(req->iv, crypto_aead_ivsize(tfm));

	if (ret)
		return gpca_aead_cleanup_and_complete_req(op_ctx, ret);

	gpca_aead_queue_request(op_ctx);
}

static int gpca_aead_op(struct aead_request *req, bool encrypt)
{
	int ret = 0;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct gpca_aead_ctx *tfm_ctx = crypto_aead_ctx(tfm);
	struct gpca_aead_op_ctx *op_ctx = NULL;
	u32 auth_tag_size = crypto_aead_authsize(tfm);

	dev_dbg(tfm_ctx->gpca_dev->dev,
		"AEAD request with encrypt = %d cryptlen = %d assoclen = %d",
		encrypt, req->cryptlen, req->assoclen);

	if (!encrypt && req->cryptlen < auth_tag_size)
		return -EINVAL;

	if (!tfm_ctx->key_handle)
		return -EINVAL;

	op_ctx = kcalloc(1, sizeof(*op_ctx), GFP_KERNEL);
	if (!op_ctx)
		return -ENOMEM;

	op_ctx->gpca_dev = tfm_ctx->gpca_dev;
	op_ctx->req = req;
	op_ctx->prev_op_ret = 0;
	op_ctx->total_msg_len = req->cryptlen;
	op_ctx->src_buf_offset = 0;
	op_ctx->dst_buf_offset = 0;
	op_ctx->encrypt = encrypt;
	op_ctx->first_op = true;

	op_ctx->op_handle = gpca_crypto_op_handle_alloc();
	if (!op_ctx->op_handle) {
		ret = -ENOMEM;
		goto free_op_ctx;
	}

	/**
	 * Due to b/263710877 IV endianness is different from
	 * what is used by callers of Linux Kernel Crypto API.
	 * On chips with this bug, IV needs to be reversed before
	 * sending to GPCA.
	 */
	if (req->iv && tfm_ctx->gpca_dev->reversed_iv)
		reverse_buffer_in_place(req->iv, crypto_aead_ivsize(tfm));

	ret = gpca_crypto_set_symm_algo_params(tfm_ctx->gpca_dev,
					       op_ctx->op_handle,
					       tfm_ctx->key_handle,
					       tfm_ctx->gpca_algo,
					       encrypt ? GPCA_SUPPORTED_PURPOSE_ENCRYPT :
						  GPCA_SUPPORTED_PURPOSE_DECRYPT,
					       req->iv, crypto_aead_ivsize(tfm),
					       gpca_aead_init_cb, op_ctx);
	dev_dbg(tfm_ctx->gpca_dev->dev,
		"Setting the GPCA algorithm parameters ret = %d", ret);

	if (ret == 0)
		return -EINPROGRESS;

	gpca_crypto_op_handle_free(op_ctx->op_handle);
free_op_ctx:
	kfree(op_ctx);
	return ret;
}

static int gpca_aead_encrypt(struct aead_request *req)
{
	return gpca_aead_op(req, true);
}

static int gpca_aead_decrypt(struct aead_request *req)
{
	return gpca_aead_op(req, false);
}

static void gpca_aead_alg_init(struct aead_alg *alg)
{
	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = GOOGLE_GPCA_AES_PRIORITY;
	alg->base.cra_flags = CRYPTO_ALG_ALLOCATES_MEMORY | CRYPTO_ALG_ASYNC;
	alg->base.cra_blocksize = 1;
	alg->base.cra_ctxsize = sizeof(struct gpca_aead_ctx);
	alg->setkey = gpca_aead_setkey;
	alg->setauthsize = gpca_aead_setauthsize;
	alg->encrypt = gpca_aead_encrypt;
	alg->decrypt = gpca_aead_decrypt;
	alg->init = gpca_aead_init;
	alg->exit = gpca_aead_exit;
}

static void gpca_skcipher_cleanup_and_complete_req(struct gpca_skcipher_op_ctx *op_ctx, int ret)
{
	struct skcipher_request *req = op_ctx->req;
	int prev_op_ret = op_ctx->prev_op_ret;

	gpca_crypto_op_handle_free(op_ctx->op_handle);
	kfree(op_ctx);
	skcipher_request_complete(req, prev_op_ret ? prev_op_ret : ret);
}

static int gpca_skcipher_init(struct crypto_skcipher *tfm)
{
	struct gpca_skcipher_ctx *tfm_ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *skcipher_alg =
		container_of(tfm->base.__crt_alg, struct skcipher_alg, base);
	struct gpca_skcipher_alg *gpca_skcipher_alg =
		container_of(skcipher_alg, struct gpca_skcipher_alg, skcipher_alg);

	tfm_ctx->gpca_dev = gpca_skcipher_alg->gpca_dev;
	tfm_ctx->key_handle = NULL;
	tfm_ctx->skcipher_algo = gpca_skcipher_alg->algo;

	return 0;
}

static void gpca_skcipher_exit(struct crypto_skcipher *tfm)
{
	struct gpca_skcipher_ctx *tfm_ctx = crypto_skcipher_ctx(tfm);
	int ret = 0;

	if (tfm_ctx->key_handle)
		ret = gpca_key_clear(tfm_ctx->gpca_dev, &tfm_ctx->key_handle);

	if (ret)
		dev_err(tfm_ctx->gpca_dev->dev,
			"Key clear failed with ret = %d", ret);
	tfm_ctx->gpca_dev = NULL;
	tfm_ctx->key_handle = NULL;
}

static int gpca_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key, unsigned int keylen)
{
	int ret = 0;
	struct gpca_skcipher_ctx *tfm_ctx = crypto_skcipher_ctx(tfm);
	struct gpca_key_policy kp = {
		GPCA_KEY_CLASS_PORTABLE,
		GPCA_ALGO_AES128_GCM,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_ENABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_DISABLE,
		GPCA_EVICT_AP_S_DISABLE,
		GPCA_EVICT_AP_NS_DISABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};

	if (tfm_ctx->key_handle) {
		ret = gpca_key_clear(tfm_ctx->gpca_dev, &tfm_ctx->key_handle);
		if (ret)
			return ret;
	}

	switch (tfm_ctx->skcipher_algo) {
	case GPCA_SKCIPHER_AES_ECB:
		switch (keylen) {
		case AES_KEYSIZE_128:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES128_ECB;
			break;
		case AES_KEYSIZE_192:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES192_ECB;
			break;
		case AES_KEYSIZE_256:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES256_ECB;
			break;
		default:
			return -EINVAL;
		}
		break;
	case GPCA_SKCIPHER_AES_CBC:
		switch (keylen) {
		case AES_KEYSIZE_128:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES128_CBC;
			break;
		case AES_KEYSIZE_192:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES192_CBC;
			break;
		case AES_KEYSIZE_256:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES256_CBC;
			break;
		default:
			return -EINVAL;
		}
		break;
	case GPCA_SKCIPHER_AES_CTR:
		switch (keylen) {
		case AES_KEYSIZE_128:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES128_CTR;
			break;
		case AES_KEYSIZE_192:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES192_CTR;
			break;
		case AES_KEYSIZE_256:
			tfm_ctx->gpca_algo = GPCA_ALGO_AES256_CTR;
			break;
		default:
			return -EINVAL;
		}
		break;
	case GPCA_SKCIPHER_TDES_ECB:
		tfm_ctx->gpca_algo = GPCA_ALGO_TDES_ECB;
		break;
	case GPCA_SKCIPHER_TDES_CBC:
		tfm_ctx->gpca_algo = GPCA_ALGO_TDES_CBC;
		break;
	default:
		return -EINVAL;
	}

	kp.algo = tfm_ctx->gpca_algo;

	ret = gpca_key_import(tfm_ctx->gpca_dev, &tfm_ctx->key_handle,
			      &kp, key, keylen);
	dev_dbg(tfm_ctx->gpca_dev->dev, "Import key for algo = %d ret = %d",
		tfm_ctx->gpca_algo, ret);

	return ret;
}

static void gpca_skcipher_clear_op_cb(int ret, void *cb_ctx)
{
	struct gpca_skcipher_op_ctx *op_ctx = (struct gpca_skcipher_op_ctx *)cb_ctx;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op ret = %d", ret);
	gpca_skcipher_cleanup_and_complete_req(op_ctx, ret);
}

static void gpca_skcipher_clear_and_queue_next_op_cb(int ret, void *cb_ctx)
{
	struct gpca_skcipher_op_ctx *op_ctx = (struct gpca_skcipher_op_ctx *)cb_ctx;
	struct skcipher_request *req = NULL;
	struct crypto_skcipher *tfm = NULL;
	struct gpca_skcipher_ctx *tfm_ctx = NULL;
	u32 ivsize = 0;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	req = op_ctx->req;
	tfm = crypto_skcipher_reqtfm(req);
	tfm_ctx = crypto_skcipher_ctx(tfm);
	ivsize = crypto_skcipher_ivsize(tfm);

	dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op ret = %d", ret);
	if (ret || op_ctx->prev_op_ret) {
		gpca_skcipher_cleanup_and_complete_req(op_ctx, ret);
		return;
	}

	if (ivsize && tfm_ctx->gpca_dev->reversed_iv)
		reverse_buffer_in_place(op_ctx->req->iv, ivsize);

	ret = gpca_crypto_set_symm_algo_params(tfm_ctx->gpca_dev,
					       op_ctx->op_handle,
					       tfm_ctx->key_handle,
					       tfm_ctx->gpca_algo,
					       op_ctx->encrypt ? GPCA_SUPPORTED_PURPOSE_ENCRYPT :
							 GPCA_SUPPORTED_PURPOSE_DECRYPT,
					       op_ctx->req->iv, ivsize,
					       gpca_skcipher_init_cb, op_ctx);
	dev_dbg(tfm_ctx->gpca_dev->dev,
		"Setting the GPCA algorithm parameters ivsize = %d ret = %d", ivsize, ret);

	if (ret)
		gpca_skcipher_cleanup_and_complete_req(op_ctx, ret);
}

static void gpca_increment_iv(u8 *iv, u32 increment)
{
	__be64 *high_be = (__be64 *)iv;
	__be64 *low_be = high_be + 1;
	u64 orig_low = __be64_to_cpu(*low_be);
	u64 new_low = orig_low + (u64)increment;

	*low_be = __cpu_to_be64(new_low);
	if (new_low < orig_low)
		/* there was a carry from the low 8 bytes */
		*high_be = __cpu_to_be64(__be64_to_cpu(*high_be) + 1);
}

static void gpca_skcipher_op_cb(int ret, void *cb_ctx)
{
	struct gpca_skcipher_op_ctx *op_ctx = (struct gpca_skcipher_op_ctx *)cb_ctx;
	struct skcipher_request *req = NULL;
	struct crypto_skcipher *tfm = NULL;
	struct gpca_skcipher_ctx *tfm_ctx = NULL;
	u32 ivsize = 0;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	req = op_ctx->req;
	tfm = crypto_skcipher_reqtfm(op_ctx->req);
	tfm_ctx = crypto_skcipher_ctx(tfm);
	ivsize = crypto_skcipher_ivsize(tfm);

	dev_dbg(op_ctx->gpca_dev->dev,
		"GPCA SKCIPHER operation callback ret = %d", ret);

	if (tfm_ctx->skcipher_algo == GPCA_SKCIPHER_AES_CTR)
		gpca_increment_iv(req->iv,
				  (op_ctx->src_buf_len + AES_BLOCK_SIZE - 1) /
					  AES_BLOCK_SIZE);

	op_ctx->total_msg_len -= op_ctx->src_buf_len;

	if (op_ctx->src_buf_len) {
		if (ret == 0 && op_ctx->total_msg_len == 0 &&
		    !op_ctx->encrypt &&
		    (tfm_ctx->skcipher_algo == GPCA_SKCIPHER_AES_CBC ||
		     tfm_ctx->skcipher_algo == GPCA_SKCIPHER_TDES_CBC))
			memcpy(req->iv,
			       (op_ctx->src_buf + op_ctx->src_buf_len -
				ivsize),
			       ivsize);

		dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
				  op_ctx->src_buf, op_ctx->src_dma_addr);
	}

	if (ret == 0 && op_ctx->dst_buf_len) {
		scatterwalk_map_and_copy(op_ctx->dst_buf, op_ctx->req->dst,
					 op_ctx->dst_buf_offset,
					 op_ctx->dst_buf_len,
					 SCATTERWALK_TO_SG);
		if (op_ctx->total_msg_len == 0 && op_ctx->encrypt &&
		    (tfm_ctx->skcipher_algo == GPCA_SKCIPHER_AES_CBC ||
		     tfm_ctx->skcipher_algo == GPCA_SKCIPHER_TDES_CBC))
			memcpy(req->iv,
			       (op_ctx->dst_buf + op_ctx->dst_buf_len -
				ivsize),
			       ivsize);
	}

	if (op_ctx->dst_buf_len)
		dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->dst_buf_len,
				  op_ctx->dst_buf, op_ctx->dst_dma_addr);
	op_ctx->src_buf_offset += op_ctx->src_buf_len;
	op_ctx->dst_buf_offset += op_ctx->dst_buf_len;

	if (op_ctx->total_msg_len == 0 || ret) {
		op_ctx->prev_op_ret = ret;
		ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
					   gpca_skcipher_clear_op_cb, op_ctx);
		dev_dbg(op_ctx->gpca_dev->dev, "Clear GPCA crypto op ret = %d", ret);
		if (ret)
			gpca_skcipher_cleanup_and_complete_req(op_ctx, ret);
	} else {
		/**
		 * Due to b/264014406 intermediate IV endianness in operation slot
		 * is incorrect due to which streaming mode for AES CBC & CTR
		 * is broken. To workaround the issue, we clear the slot and
		 * set crypto params with IV computed in software.
		 */
		if (op_ctx->gpca_dev->reversed_iv &&
		    (tfm_ctx->skcipher_algo == GPCA_SKCIPHER_AES_CBC ||
		     tfm_ctx->skcipher_algo == GPCA_SKCIPHER_AES_CTR)) {
			op_ctx->prev_op_ret = ret;
			ret = gpca_crypto_clear_op(op_ctx->gpca_dev,
						   op_ctx->op_handle,
						   gpca_skcipher_clear_and_queue_next_op_cb,
						   op_ctx);
			dev_dbg(op_ctx->gpca_dev->dev,
				"Clear GPCA crypto op ret = %d", ret);
			if (ret)
				gpca_skcipher_cleanup_and_complete_req(op_ctx,
								       ret);
		} else {
			gpca_skcipher_queue_request(op_ctx);
		}
	}
}

static u32 gpca_get_block_size(enum gpca_skcipher_algo alg)
{
	switch (alg) {
	case GPCA_SKCIPHER_AES_CBC:
	case GPCA_SKCIPHER_AES_CTR:
	case GPCA_SKCIPHER_AES_ECB:
		return AES_BLOCK_SIZE;
	case GPCA_SKCIPHER_TDES_CBC:
	case GPCA_SKCIPHER_TDES_ECB:
		return DES3_EDE_KEY_SIZE;
	default:
		return 0;
	}
}

static int gpca_skcipher_queue_request(struct gpca_skcipher_op_ctx *op_ctx)
{
	int ret = 0;
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(op_ctx->req);
	struct gpca_skcipher_ctx *tfm_ctx = crypto_skcipher_ctx(tfm);
	u32 block_size = gpca_get_block_size(tfm_ctx->skcipher_algo);
	bool last = false;

	op_ctx->src_buf_len = min_t(u32, KMALLOC_MAX_SIZE, op_ctx->total_msg_len);
	last = (op_ctx->src_buf_len == op_ctx->total_msg_len);

	/* Intermediate messages should be block size aligned */
	if (!last)
		op_ctx->src_buf_len -= op_ctx->src_buf_len % block_size;

	op_ctx->dst_buf_len = op_ctx->src_buf_len;

	if (op_ctx->src_buf_len) {
		op_ctx->src_buf = dma_alloc_coherent(op_ctx->gpca_dev->dev,
						     op_ctx->src_buf_len,
						     &op_ctx->src_dma_addr,
						     GFP_KERNEL);
		if (!op_ctx->src_buf) {
			ret = -ENOMEM;
			goto error;
		}
	} else {
		op_ctx->src_buf = NULL;
		op_ctx->src_dma_addr = 0;
	}

	if (op_ctx->dst_buf_len) {
		op_ctx->dst_buf = dma_alloc_coherent(op_ctx->gpca_dev->dev,
						     op_ctx->dst_buf_len,
						     &op_ctx->dst_dma_addr,
						     GFP_KERNEL);
		if (!op_ctx->dst_buf) {
			ret = -ENOMEM;
			goto free_src_buf;
		}

	} else {
		op_ctx->dst_buf = NULL;
		op_ctx->dst_dma_addr = 0;
	}

	scatterwalk_map_and_copy(op_ctx->src_buf, op_ctx->req->src,
				 op_ctx->src_buf_offset, op_ctx->src_buf_len,
				 SCATTERWALK_FROM_SG);

	ret = gpca_crypto_start_symmetric_op(op_ctx->gpca_dev,
					     op_ctx->op_handle, op_ctx->src_dma_addr,
					     op_ctx->src_buf_len,
					     0, 0,
					     op_ctx->dst_dma_addr, op_ctx->dst_buf_len, last,
					     gpca_skcipher_op_cb, op_ctx);
	dev_dbg(op_ctx->gpca_dev->dev,
		"GPCA SKCIPHER crypto op input data length = %d output data length = %d last = %d ret = %d",
		op_ctx->src_buf_len,
		op_ctx->dst_buf_len, last, ret);

	if (ret == 0)
		return ret;

	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->dst_buf_len,
			  op_ctx->dst_buf, op_ctx->dst_dma_addr);
free_src_buf:
	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
			  op_ctx->src_buf, op_ctx->src_dma_addr);
error:
	op_ctx->prev_op_ret = ret;
	ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
				   gpca_skcipher_clear_op_cb, op_ctx);
	dev_dbg(op_ctx->gpca_dev->dev, "Clear GPCA crypto op ret = %d", ret);
	if (ret)
		gpca_skcipher_cleanup_and_complete_req(op_ctx, ret);

	return ret;
}

static void gpca_skcipher_init_cb(int ret, void *cb_ctx)
{
	struct gpca_skcipher_op_ctx *op_ctx = (struct gpca_skcipher_op_ctx *)cb_ctx;
	struct skcipher_request *req = NULL;
	struct crypto_skcipher *tfm = NULL;
	struct gpca_skcipher_ctx *tfm_ctx = NULL;
	u32 ivsize = 0;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	req = op_ctx->req;
	tfm = crypto_skcipher_reqtfm(req);
	ivsize = crypto_skcipher_ivsize(tfm);
	tfm_ctx = crypto_skcipher_ctx(tfm);

	dev_dbg(op_ctx->gpca_dev->dev,
		"GPCA SKCIPHER init callback ret = %d", ret);

	if (ivsize && tfm_ctx->gpca_dev->reversed_iv)
		reverse_buffer_in_place(req->iv, ivsize);

	if (ret)
		return gpca_skcipher_cleanup_and_complete_req(op_ctx, ret);

	gpca_skcipher_queue_request(op_ctx);
}

static int gpca_skcipher_op(struct skcipher_request *req, bool encrypt)
{
	int ret = 0;
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct gpca_skcipher_ctx *tfm_ctx = crypto_skcipher_ctx(tfm);
	struct gpca_skcipher_op_ctx *op_ctx = NULL;
	u32 ivsize = crypto_skcipher_ivsize(tfm);
	u32 tfm_block_size = crypto_skcipher_blocksize(tfm);

	dev_dbg(tfm_ctx->gpca_dev->dev,
		"SKCIPHER request with algo = %d encrypt = %d cryptlen = %d",
		tfm_ctx->gpca_algo, encrypt, req->cryptlen);

	if (!tfm_ctx->key_handle)
		return -EINVAL;

	if (req->cryptlen % tfm_block_size)
		return -EINVAL;

	if (req->cryptlen == 0)
		return 0;

	op_ctx = kcalloc(1, sizeof(*op_ctx), GFP_KERNEL);
	if (!op_ctx)
		return -ENOMEM;

	op_ctx->gpca_dev = tfm_ctx->gpca_dev;
	op_ctx->req = req;
	op_ctx->prev_op_ret = 0;
	op_ctx->total_msg_len = req->cryptlen;
	op_ctx->src_buf_offset = 0;
	op_ctx->dst_buf_offset = 0;
	op_ctx->encrypt = encrypt;

	op_ctx->op_handle = gpca_crypto_op_handle_alloc();
	if (!op_ctx->op_handle) {
		ret = -ENOMEM;
		goto free_op_ctx;
	}

	/**
	 * Due to b/263710877 IV endianness is different from
	 * what is used by callers of Linux Kernel Crypto API.
	 * On chips with this bug, IV needs to be reversed before
	 * sending to GPCA.
	 */
	if (ivsize && tfm_ctx->gpca_dev->reversed_iv)
		reverse_buffer_in_place(req->iv, ivsize);

	ret = gpca_crypto_set_symm_algo_params(tfm_ctx->gpca_dev,
					       op_ctx->op_handle,
					       tfm_ctx->key_handle,
					       tfm_ctx->gpca_algo,
					       encrypt ? GPCA_SUPPORTED_PURPOSE_ENCRYPT :
						  GPCA_SUPPORTED_PURPOSE_DECRYPT,
					       req->iv, ivsize,
					       gpca_skcipher_init_cb, op_ctx);
	dev_dbg(tfm_ctx->gpca_dev->dev,
		"Setting the GPCA algorithm parameters ivsize = %d ret = %d", ivsize, ret);

	if (ret == 0)
		return -EINPROGRESS;

	gpca_crypto_op_handle_free(op_ctx->op_handle);
free_op_ctx:
	kfree(op_ctx);
	return ret;
}

static int gpca_skcipher_encrypt(struct skcipher_request *req)
{
	return gpca_skcipher_op(req, true);
}

static int gpca_skcipher_decrypt(struct skcipher_request *req)
{
	return gpca_skcipher_op(req, false);
}

static void gpca_skcipher_alg_init(struct skcipher_alg *alg)
{
	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = GOOGLE_GPCA_AES_PRIORITY;
	alg->base.cra_flags = CRYPTO_ALG_ALLOCATES_MEMORY | CRYPTO_ALG_ASYNC;
	alg->base.cra_ctxsize = sizeof(struct gpca_skcipher_ctx);
	alg->setkey = gpca_skcipher_setkey;
	alg->encrypt = gpca_skcipher_encrypt;
	alg->decrypt = gpca_skcipher_decrypt;
	alg->init = gpca_skcipher_init;
	alg->exit = gpca_skcipher_exit;
}

int gpca_cipher_register(struct gpca_dev *gpca_dev)
{
	int ret = 0;
	u32 i = 0;

	for (i = 0; i < ARRAY_SIZE(gpca_aead_algs); i++) {
		gpca_aead_algs[i].gpca_dev = gpca_dev;
		gpca_aead_alg_init(&gpca_aead_algs[i].aead_alg);
		ret = crypto_register_aead(&gpca_aead_algs[i].aead_alg);
		if (ret)
			return ret;
		dev_info(gpca_dev->dev,
			 "AEAD register passed for iteration = %d", i);
	}

	for (i = 0; i < ARRAY_SIZE(gpca_skcipher_algs); i++) {
		gpca_skcipher_algs[i].gpca_dev = gpca_dev;
		gpca_skcipher_alg_init(&gpca_skcipher_algs[i].skcipher_alg);
		ret = crypto_register_skcipher(&gpca_skcipher_algs[i].skcipher_alg);
		if (ret)
			return ret;
		dev_info(gpca_dev->dev,
			 "SKCIPHER register passed for iteration = %d", i);
	}

	return ret;
}

void gpca_cipher_unregister(struct gpca_dev *gpca_dev)
{
	u32 i = 0;

	for (i = 0; i < ARRAY_SIZE(gpca_aead_algs); i++) {
		gpca_aead_algs[i].gpca_dev = NULL;
		crypto_unregister_aead(&gpca_aead_algs[i].aead_alg);
	}

	for (i = 0; i < ARRAY_SIZE(gpca_skcipher_algs); i++) {
		gpca_skcipher_algs[i].gpca_dev = NULL;
		crypto_unregister_skcipher(&gpca_skcipher_algs[i].skcipher_alg);
	}
}
