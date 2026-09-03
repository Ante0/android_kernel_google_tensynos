// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Linux kernel crypto Hash implementation.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include "gpca_hash.h"

#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha2.h>
#include <linux/dma-mapping.h>
#include <linux/minmax.h>

#include "gpca_crypto.h"
#include "gpca_internal.h"
#include "gpca_key_policy.h"

#define GOOGLE_GPCA_SHA_PRIORITY 300

struct gpca_ahash_alg {
	struct gpca_dev *gpca_dev;
	struct ahash_alg ahash_alg;
};

struct gpca_ahash_reqctx {
	struct gpca_dev *gpca_dev;
	struct gpca_crypto_op_handle *sha_op_handle;
	u8 residual_buf[SHA512_BLOCK_SIZE] __aligned(sizeof(u32));
	u32 residual_buf_len;
	dma_addr_t residual_buf_dma_addr;
	struct scatterlist sg_ffwd_lists[2];
	u32 block_size;
	struct scatterlist *req_sg_list;
	struct scatterlist *dma_sg_list;
};

struct gpca_ahash_update_cb_ctx {
	struct gpca_dev *gpca_dev;
	struct ahash_request *req;
	u32 msg_buf_len;
	u8 *digest_buf;
	u32 digest_buf_len;
	dma_addr_t digest_dma_addr;
	int prev_op_ret;
	u32 total_msg_len;
	bool final;
};

struct gpca_hmac_ctx {
	struct gpca_dev *gpca_dev;
	struct gpca_key_handle *key_handle;
	struct crypto_ahash *fallback;
	bool imported_key;
	bool needs_fallback;
};

struct gpca_ahash_alg gpca_ahash_algs[] = {
	{
	.ahash_alg = {
		.halg.base.cra_name = "sha224",
		.halg.base.cra_driver_name = "google-gpca-sha224",
		.halg.base.cra_blocksize = SHA224_BLOCK_SIZE,

		.halg.digestsize = SHA224_DIGEST_SIZE,
	},
	},
	{
	.ahash_alg = {
		.halg.base.cra_name = "sha256",
		.halg.base.cra_driver_name = "google-gpca-sha256",
		.halg.base.cra_blocksize = SHA256_BLOCK_SIZE,

		.halg.digestsize = SHA256_DIGEST_SIZE,
	},
	},
	{
	.ahash_alg = {
		.halg.base.cra_name = "sha384",
		.halg.base.cra_driver_name = "google-gpca-sha384",
		.halg.base.cra_blocksize = SHA384_BLOCK_SIZE,

		.halg.digestsize = SHA384_DIGEST_SIZE,
	},
	},
	{
	.ahash_alg = {
		.halg.base.cra_name = "sha512",
		.halg.base.cra_driver_name = "google-gpca-sha512",
		.halg.base.cra_blocksize = SHA512_BLOCK_SIZE,

		.halg.digestsize = SHA512_DIGEST_SIZE,
	}
	},
};

struct gpca_ahash_alg gpca_hmac_algs[] = {
	{
	.ahash_alg = {
		.halg.base.cra_name = "hmac(sha224)",
		.halg.base.cra_driver_name = "google-gpca-hmac-sha224",
		.halg.base.cra_blocksize = SHA224_BLOCK_SIZE,

		.halg.digestsize = SHA224_DIGEST_SIZE,
	},
	},
	{
	.ahash_alg = {
		.halg.base.cra_name = "hmac(sha256)",
		.halg.base.cra_driver_name = "google-gpca-hmac-sha256",
		.halg.base.cra_blocksize = SHA256_BLOCK_SIZE,

		.halg.digestsize = SHA256_DIGEST_SIZE,
	},
	},
	{
	.ahash_alg = {
		.halg.base.cra_name = "hmac(sha384)",
		.halg.base.cra_driver_name = "google-gpca-hmac-sha384",
		.halg.base.cra_blocksize = SHA384_BLOCK_SIZE,

		.halg.digestsize = SHA384_DIGEST_SIZE,
	},
	},
	{
	.ahash_alg = {
		.halg.base.cra_name = "hmac(sha512)",
		.halg.base.cra_driver_name = "google-gpca-hmac-sha512",
		.halg.base.cra_blocksize = SHA512_BLOCK_SIZE,

		.halg.digestsize = SHA512_DIGEST_SIZE,
	}
	},
};

static int gpca_hash_queue_request(struct gpca_ahash_update_cb_ctx *ahash_ctx);

static int gpca_ahash_cra_init(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct gpca_ahash_reqctx));

	return 0;
}

static void gpca_ahash_init_cb(int ret, void *cb_ctx)
{
	struct ahash_request *req = (struct ahash_request *)cb_ctx;
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);

	if (ret)
		gpca_crypto_op_handle_free(req_ctx->sha_op_handle);

	ahash_request_complete(req, ret);
}

static int gpca_ahash_init_generic(struct ahash_request *req, gpca_crypto_cb cb)
{
	int ret = 0;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);
	enum gpca_algorithm sha_algo = GPCA_ALGO_SHA2_224;

	struct hash_alg_common *hash_alg_common =
		container_of(req->base.tfm->__crt_alg, struct hash_alg_common, base);
	struct ahash_alg *ahash_alg =
		container_of(hash_alg_common, struct ahash_alg, halg);
	struct gpca_ahash_alg *sha_alg =
		container_of(ahash_alg, struct gpca_ahash_alg, ahash_alg);

	dev_dbg(sha_alg->gpca_dev->dev, "init: digest size: %u\n",
		crypto_ahash_digestsize(tfm));

	req_ctx->gpca_dev = sha_alg->gpca_dev;
	req_ctx->sha_op_handle = NULL;
	req_ctx->residual_buf_len = 0;

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA224_DIGEST_SIZE:
		sha_algo = GPCA_ALGO_SHA2_224;
		req_ctx->block_size = SHA224_BLOCK_SIZE;
		break;
	case SHA256_DIGEST_SIZE:
		sha_algo = GPCA_ALGO_SHA2_256;
		req_ctx->block_size = SHA256_BLOCK_SIZE;
		break;
	case SHA384_DIGEST_SIZE:
		sha_algo = GPCA_ALGO_SHA2_384;
		req_ctx->block_size = SHA384_BLOCK_SIZE;
		break;
	case SHA512_DIGEST_SIZE:
		sha_algo = GPCA_ALGO_SHA2_512;
		req_ctx->block_size = SHA512_BLOCK_SIZE;
		break;
	default:
		return -EINVAL;
	}

	req_ctx->sha_op_handle = gpca_crypto_op_handle_alloc();
	if (!req_ctx->sha_op_handle)
		return -ENOMEM;

	ret = gpca_crypto_set_hash_params(sha_alg->gpca_dev, req_ctx->sha_op_handle,
					  sha_algo, cb, req);

	if (ret == 0)
		ret = -EINPROGRESS;
	else
		gpca_crypto_op_handle_free(req_ctx->sha_op_handle);

	return ret;
}

static int gpca_ahash_init(struct ahash_request *req)
{
	return gpca_ahash_init_generic(req, gpca_ahash_init_cb);
}

static void gpca_ahash_mark_request_complete(struct gpca_ahash_update_cb_ctx *ahash_ctx, int ret)
{
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(ahash_ctx->req);
	struct ahash_request *req = ahash_ctx->req;

	dma_unmap_single(req_ctx->gpca_dev->dev, req_ctx->residual_buf_dma_addr,
			 sizeof(req_ctx->residual_buf), DMA_TO_DEVICE);
	if (req_ctx->dma_sg_list)
		dma_unmap_sg(req_ctx->gpca_dev->dev, req_ctx->dma_sg_list,
			     sg_nents(req_ctx->dma_sg_list), DMA_TO_DEVICE);

	kfree(ahash_ctx);
	ahash_request_complete(req, ret);
}

static void gpca_ahash_clear_op_cb(int ret, void *cb_ctx)
{
	struct gpca_ahash_update_cb_ctx *ctx =
		(struct gpca_ahash_update_cb_ctx *)cb_ctx;
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(ctx->req);

	gpca_crypto_op_handle_free(req_ctx->sha_op_handle);
	gpca_ahash_mark_request_complete(ctx, ctx->prev_op_ret == 0 ? ret : ctx->prev_op_ret);
}

static void gpca_ahash_update_cb(int ret, void *cb_ctx)
{
	struct gpca_ahash_update_cb_ctx *ahash_ctx =
		(struct gpca_ahash_update_cb_ctx *)cb_ctx;
	struct ahash_request *req = ahash_ctx->req;
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);
	bool final = false;

	ahash_ctx->total_msg_len -= ahash_ctx->msg_buf_len;
	final = ahash_ctx->final && (ahash_ctx->total_msg_len == 0);

	if (final) {
		/* Copy digest to result buffer on success */
		if (!ret)
			memcpy(req->result, ahash_ctx->digest_buf,
			       ahash_ctx->digest_buf_len);

		dma_free_coherent(ahash_ctx->gpca_dev->dev,
				  ahash_ctx->digest_buf_len,
				  ahash_ctx->digest_buf,
				  ahash_ctx->digest_dma_addr);
	}

	if (final || ret) {
		ahash_ctx->prev_op_ret = ret;
		ret = gpca_crypto_clear_op(req_ctx->gpca_dev,
					   req_ctx->sha_op_handle,
					   gpca_ahash_clear_op_cb, ahash_ctx);
		if (ret) {
			gpca_crypto_op_handle_free(req_ctx->sha_op_handle);
			gpca_ahash_mark_request_complete(ahash_ctx, ahash_ctx->prev_op_ret == 0 ?
						       ret :
						       ahash_ctx->prev_op_ret);
		}
	} else if (ahash_ctx->total_msg_len == req_ctx->residual_buf_len) {
		gpca_ahash_mark_request_complete(ahash_ctx, ret);
	} else {
		ret = gpca_hash_queue_request(ahash_ctx);
		if (ret)
			gpca_ahash_mark_request_complete(ahash_ctx, ret);
	}
}

static int gpca_hash_queue_request(struct gpca_ahash_update_cb_ctx *ahash_ctx)
{
	struct ahash_request *req = ahash_ctx->req;
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);
	int ret = 0;
	bool final = false;
	dma_addr_t src_dma_addr = 0;
	u32 src_len = 0;

	src_len = min_t(u32,
			req_ctx->req_sg_list ?
				sg_dma_len(req_ctx->req_sg_list) :
				0,
			ahash_ctx->total_msg_len - req_ctx->residual_buf_len);
	/* Copy from source scatterlist to residual_buf as long as smaller than block size */
	while (src_len && src_len + req_ctx->residual_buf_len < req_ctx->block_size) {
		scatterwalk_map_and_copy(req_ctx->residual_buf + req_ctx->residual_buf_len,
					 req_ctx->req_sg_list, 0,
					 src_len,
					 0);
		req_ctx->residual_buf_len += src_len;
		/* Move to next scatterlist entry */
		req_ctx->req_sg_list = sg_next(req_ctx->req_sg_list);
		src_len = min_t(u32,
				req_ctx->req_sg_list ?
					sg_dma_len(req_ctx->req_sg_list) :
					0,
				ahash_ctx->total_msg_len -
					req_ctx->residual_buf_len);
	}

	/* If residual_buf is not empty, populate buffer and send to GPCA */
	if (req_ctx->residual_buf_len) {
		src_dma_addr = req_ctx->residual_buf_dma_addr;
		if (req_ctx->req_sg_list) {
			src_len = min_t(u32, sg_dma_len(req_ctx->req_sg_list),
					ahash_ctx->total_msg_len -
						req_ctx->residual_buf_len);
			src_len = min_t(u32, src_len,
					req_ctx->block_size -
						req_ctx->residual_buf_len);
			scatterwalk_map_and_copy(req_ctx->residual_buf +
							 req_ctx->residual_buf_len,
						 req_ctx->req_sg_list, 0,
						 src_len, 0);

			dma_unmap_sg(req_ctx->gpca_dev->dev,
				     req_ctx->dma_sg_list,
				     sg_nents(req_ctx->dma_sg_list),
				     DMA_TO_DEVICE);
			req_ctx->dma_sg_list = NULL;
			req_ctx->req_sg_list = scatterwalk_ffwd(req_ctx->sg_ffwd_lists,
								req_ctx->req_sg_list,
								req_ctx->block_size -
								    req_ctx->residual_buf_len);
			req_ctx->residual_buf_len += src_len;
			if (req_ctx->req_sg_list) {
				dma_map_sg(req_ctx->gpca_dev->dev,
					   req_ctx->req_sg_list,
					   sg_nents(req_ctx->req_sg_list),
					   DMA_TO_DEVICE);
				req_ctx->dma_sg_list = req_ctx->req_sg_list;
			}
		}
		ahash_ctx->msg_buf_len = req_ctx->residual_buf_len;
		dma_sync_single_for_device(req_ctx->gpca_dev->dev,
					   req_ctx->residual_buf_dma_addr,
					   sizeof(req_ctx->residual_buf),
					   DMA_TO_DEVICE);
	} else if (req_ctx->req_sg_list) {
		/* Start GPCA operation from scatterlist entry */
		src_dma_addr = sg_dma_address(req_ctx->req_sg_list);
		ahash_ctx->msg_buf_len = src_len;

		if (!ahash_ctx->final || ahash_ctx->msg_buf_len != ahash_ctx->total_msg_len) {
			ahash_ctx->msg_buf_len =
				(ahash_ctx->msg_buf_len / req_ctx->block_size) *
				req_ctx->block_size;
			scatterwalk_map_and_copy(req_ctx->residual_buf + req_ctx->residual_buf_len,
						 req_ctx->req_sg_list, ahash_ctx->msg_buf_len,
						 src_len - ahash_ctx->msg_buf_len,
						 0);
			req_ctx->residual_buf_len += src_len - ahash_ctx->msg_buf_len;
		}
		req_ctx->req_sg_list = sg_next(req_ctx->req_sg_list);
	}

	final = ahash_ctx->final && (ahash_ctx->msg_buf_len == ahash_ctx->total_msg_len);
	if (final || (ahash_ctx->msg_buf_len &&
		      ahash_ctx->msg_buf_len % req_ctx->block_size == 0)) {
		if (src_dma_addr == req_ctx->residual_buf_dma_addr)
			req_ctx->residual_buf_len = 0;

		if (final) {
			ahash_ctx->digest_buf_len =
				crypto_ahash_digestsize(crypto_ahash_reqtfm(req));
			ahash_ctx->digest_buf = dma_alloc_coherent(ahash_ctx->gpca_dev->dev,
								   ahash_ctx->digest_buf_len,
								   &ahash_ctx->digest_dma_addr,
								   GFP_KERNEL);
			if (!ahash_ctx->digest_buf) {
				ret = -ENOMEM;
				goto error;
			}

			ret = gpca_crypto_start_digest_op(ahash_ctx->gpca_dev,
							  req_ctx->sha_op_handle,
							  src_dma_addr,
							  ahash_ctx->msg_buf_len,
							  ahash_ctx->digest_dma_addr,
							  ahash_ctx->digest_buf_len,
							  true, gpca_ahash_update_cb, ahash_ctx);
		} else {
			ret = gpca_crypto_start_digest_op(ahash_ctx->gpca_dev,
							  req_ctx->sha_op_handle,
							  src_dma_addr,
							  ahash_ctx->msg_buf_len, 0, 0,
							  false, gpca_ahash_update_cb, ahash_ctx);
		}

		if (ret == 0)
			return ret;

		if (final) {
			dma_free_coherent(ahash_ctx->gpca_dev->dev, ahash_ctx->digest_buf_len,
					  ahash_ctx->digest_buf, ahash_ctx->digest_dma_addr);
		}

	} else {
		ahash_ctx->msg_buf_len = 0;
		gpca_ahash_update_cb(0, ahash_ctx);
		return 0;
	}

error:
	ahash_ctx->prev_op_ret = ret;
	ret = gpca_crypto_clear_op(req_ctx->gpca_dev, req_ctx->sha_op_handle,
				   gpca_ahash_clear_op_cb, ahash_ctx);
	if (ret) {
		gpca_crypto_op_handle_free(req_ctx->sha_op_handle);
		kfree(ahash_ctx);
	}
	return ret;
}

static int gpca_ahash_update_or_finup(struct ahash_request *req, bool final)
{
	int ret = 0;
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);
	struct gpca_dev *gpca_dev = req_ctx->gpca_dev;
	struct gpca_ahash_update_cb_ctx *ahash_ctx = NULL;

	if (!req->nbytes && !final)
		return 0;

	if (!req_ctx->sha_op_handle)
		return -EINVAL;

	if (!final &&
	    req->nbytes + req_ctx->residual_buf_len < req_ctx->block_size) {
		/* Copy request bytes to context buffer */
		scatterwalk_map_and_copy(req_ctx->residual_buf +
						 req_ctx->residual_buf_len,
					 req->src, 0, req->nbytes, 0);

		req_ctx->residual_buf_len += req->nbytes;
		return 0;
	}

	req_ctx->residual_buf_dma_addr =
		dma_map_single(gpca_dev->dev, req_ctx->residual_buf,
			       sizeof(req_ctx->residual_buf), DMA_TO_DEVICE);
	req_ctx->req_sg_list = req->src;
	req_ctx->dma_sg_list = NULL;
	/* DMA map scatterlist */
	if (req_ctx->req_sg_list && req->nbytes) {
		dma_map_sg(gpca_dev->dev, req_ctx->req_sg_list,
			   sg_nents(req_ctx->req_sg_list), DMA_TO_DEVICE);
		req_ctx->dma_sg_list = req_ctx->req_sg_list;
	}

	ahash_ctx = kcalloc(1, sizeof(*ahash_ctx), GFP_KERNEL);
	if (!ahash_ctx)
		return -ENOMEM;
	ahash_ctx->gpca_dev = gpca_dev;
	ahash_ctx->req = req;
	ahash_ctx->final = final;
	ahash_ctx->total_msg_len = req_ctx->residual_buf_len + req->nbytes;

	ret = gpca_hash_queue_request(ahash_ctx);
	if (ret == 0)
		ret = -EINPROGRESS;

	return ret;
}

static int gpca_ahash_update(struct ahash_request *req)
{
	return gpca_ahash_update_or_finup(req, false);
}

static int gpca_ahash_finup(struct ahash_request *req)
{
	return gpca_ahash_update_or_finup(req, true);
}

static int gpca_ahash_final(struct ahash_request *req)
{
	req->src = NULL;
	req->nbytes = 0;
	return gpca_ahash_update_or_finup(req, true);
}

void gpca_ahash_digest_cb(int ret, void *cb_ctx)
{
	struct ahash_request *req = (struct ahash_request *)cb_ctx;
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);

	if (ret) {
		gpca_crypto_op_handle_free(req_ctx->sha_op_handle);
		ahash_request_complete(req, ret);
	}

	ret = gpca_ahash_finup(req);
	if (ret && ret != -EINPROGRESS)
		ahash_request_complete(req, ret);
}

static int gpca_ahash_digest(struct ahash_request *req)
{
	int ret = 0;

	ret = gpca_ahash_init_generic(req, gpca_ahash_digest_cb);
	if (ret == 0)
		ret = -EINPROGRESS;
	return ret;
}

static int gpca_ahash_export(struct ahash_request *req, void *out)
{
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);

	memcpy(out, req_ctx, sizeof(*req_ctx));
	return 0;
}

static int gpca_ahash_import(struct ahash_request *req, const void *in)
{
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);

	memcpy(req_ctx, in, sizeof(*req_ctx));
	return 0;
}

static void gpca_ahash_alg_init(struct ahash_alg *alg)
{
	alg->halg.base.cra_module = THIS_MODULE;
	alg->halg.base.cra_priority = GOOGLE_GPCA_SHA_PRIORITY;
	alg->halg.base.cra_alignmask = 0;
	alg->halg.base.cra_flags = CRYPTO_ALG_ALLOCATES_MEMORY |
				   CRYPTO_ALG_ASYNC;
	alg->halg.base.cra_init = gpca_ahash_cra_init;

	alg->halg.statesize = sizeof(struct gpca_ahash_reqctx);

	alg->init = gpca_ahash_init;
	alg->update = gpca_ahash_update;
	alg->final = gpca_ahash_final;
	alg->finup = gpca_ahash_finup;
	alg->digest = gpca_ahash_digest;
	alg->export = gpca_ahash_export;
	alg->import = gpca_ahash_import;
}

static int gpca_hmac_cra_init(struct crypto_tfm *tfm)
{
	struct gpca_hmac_ctx *hmac_ctx = crypto_tfm_ctx(tfm);
	struct hash_alg_common *hash_alg_common = container_of(tfm->__crt_alg,
							       struct hash_alg_common,
							       base);
	struct ahash_alg *ahash_alg =
		container_of(hash_alg_common, struct ahash_alg, halg);
	struct gpca_ahash_alg *hmac_alg =
		container_of(ahash_alg, struct gpca_ahash_alg, ahash_alg);

	hmac_ctx->gpca_dev = hmac_alg->gpca_dev;
	hmac_ctx->key_handle = NULL;
	hmac_ctx->imported_key = false;
	hmac_ctx->fallback = crypto_alloc_ahash(tfm->__crt_alg->cra_name, 0,
						CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(hmac_ctx->fallback)) {
		dev_err(hmac_ctx->gpca_dev->dev, "Could not load fallback HMAC driver.\n");
		return PTR_ERR(hmac_ctx->fallback);
	}
	hmac_ctx->needs_fallback = false;

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct gpca_ahash_reqctx) +
				 crypto_ahash_reqsize(hmac_ctx->fallback));

	return 0;
}

static void gpca_hmac_cra_exit(struct crypto_tfm *tfm)
{
	struct gpca_hmac_ctx *hmac_ctx = crypto_tfm_ctx(tfm);
	int ret = 0;

	if (hmac_ctx->key_handle && hmac_ctx->imported_key)
		ret = gpca_key_clear(hmac_ctx->gpca_dev, &hmac_ctx->key_handle);

	if (ret)
		dev_err(hmac_ctx->gpca_dev->dev, "Key clear failed with ret = %d", ret);

	if (hmac_ctx->fallback)
		crypto_free_ahash(hmac_ctx->fallback);
}

static int gpca_hmac_setkey(struct crypto_ahash *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct gpca_hmac_ctx *hmac_ctx = crypto_ahash_ctx(tfm);
	u32 max_keylen = 0;
	enum gpca_algorithm hmac_algo = GPCA_ALGO_HMAC_SHA2_224;
	struct gpca_key_policy hmac_kp = {
		GPCA_KEY_CLASS_PORTABLE,
		GPCA_ALGO_HMAC_SHA2_224,
		GPCA_SUPPORTED_PURPOSE_SIGN,
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
	u8 *padded_key = NULL;
	int ret = 0;
	bool digest_size_hmac_keys = hmac_ctx->gpca_dev->drv_data->hw_bug_hmac_digest_size_keys;

	if (hmac_ctx->key_handle && hmac_ctx->imported_key) {
		ret = gpca_key_clear(hmac_ctx->gpca_dev, &hmac_ctx->key_handle);
		if (ret)
			return ret;
	}

	// If keylen > supported HMAC key size use fallback.
	if (digest_size_hmac_keys) {
		max_keylen = crypto_ahash_digestsize(tfm);
	} else {
		/**
		 * TODO: keylen > block_size key needs to be hashed
		 *       and imported.
		 */
		max_keylen = crypto_ahash_blocksize(tfm);
	}
	if (keylen > max_keylen) {
		hmac_ctx->needs_fallback = true;
		ret = crypto_ahash_setkey(hmac_ctx->fallback, key, keylen);
		return ret;
	}

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA224_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_224;
		break;
	case SHA256_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_256;
		break;
	case SHA384_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_384;
		break;
	case SHA512_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_512;
		break;
	default:
		return -EINVAL;
	}

	hmac_kp.algo = hmac_algo;
	padded_key = kmalloc(max_keylen, GFP_KERNEL);
	if (!padded_key)
		return -ENOMEM;

	memset(padded_key, 0, max_keylen);
	memcpy(padded_key, key, keylen);
	ret = gpca_key_import(hmac_ctx->gpca_dev, &hmac_ctx->key_handle,
			      &hmac_kp, padded_key, max_keylen);
	hmac_ctx->imported_key = true;
	kfree(padded_key);

	return ret;
}

static int gpca_hmac_init_generic(struct ahash_request *req, gpca_crypto_cb cb)
{
	int ret = 0;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct gpca_ahash_reqctx *req_ctx = ahash_request_ctx(req);
	enum gpca_algorithm hmac_algo = GPCA_ALGO_HMAC_SHA2_224;
	struct gpca_hmac_ctx *tfm_ctx = crypto_tfm_ctx(req->base.tfm);

	dev_dbg(tfm_ctx->gpca_dev->dev, "init: digest size: %u\n",
		crypto_ahash_digestsize(tfm));

	if (!tfm_ctx->key_handle)
		return -ENOKEY;

	req_ctx->gpca_dev = tfm_ctx->gpca_dev;
	req_ctx->sha_op_handle = NULL;
	req_ctx->residual_buf_len = 0;
	req_ctx->block_size = crypto_ahash_blocksize(tfm);

	switch (crypto_ahash_digestsize(tfm)) {
	case SHA224_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_224;
		break;
	case SHA256_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_256;
		break;
	case SHA384_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_384;
		break;
	case SHA512_DIGEST_SIZE:
		hmac_algo = GPCA_ALGO_HMAC_SHA2_512;
		break;
	default:
		return -EINVAL;
	}

	req_ctx->sha_op_handle = gpca_crypto_op_handle_alloc();
	if (!req_ctx->sha_op_handle)
		return -ENOMEM;

	ret = gpca_crypto_set_hmac_params(tfm_ctx->gpca_dev, req_ctx->sha_op_handle,
					  tfm_ctx->key_handle, hmac_algo,
					  GPCA_SUPPORTED_PURPOSE_SIGN, cb, req);

	if (ret == 0)
		ret = -EINPROGRESS;
	else
		gpca_crypto_op_handle_free(req_ctx->sha_op_handle);

	return ret;
}

static int gpca_hmac_init(struct ahash_request *req)
{
	struct gpca_hmac_ctx *tfm_ctx = crypto_tfm_ctx(req->base.tfm);

	if (tfm_ctx->needs_fallback && tfm_ctx->fallback) {
		ahash_request_set_tfm(req, tfm_ctx->fallback);
		return crypto_ahash_init(req);
	}

	return gpca_hmac_init_generic(req, gpca_ahash_init_cb);
}

static int gpca_hmac_digest(struct ahash_request *req)
{
	int ret = 0;
	struct gpca_hmac_ctx *tfm_ctx = crypto_tfm_ctx(req->base.tfm);

	if (tfm_ctx->needs_fallback && tfm_ctx->fallback) {
		ahash_request_set_tfm(req, tfm_ctx->fallback);
		return crypto_ahash_digest(req);
	}

	ret = gpca_hmac_init_generic(req, gpca_ahash_digest_cb);
	if (ret == 0)
		ret = -EINPROGRESS;
	return ret;
}

static void gpca_hmac_alg_init(struct ahash_alg *alg)
{
	alg->halg.base.cra_module = THIS_MODULE;
	alg->halg.base.cra_priority = GOOGLE_GPCA_SHA_PRIORITY;
	alg->halg.base.cra_alignmask = 0;
	alg->halg.base.cra_flags = CRYPTO_ALG_ALLOCATES_MEMORY |
				   CRYPTO_ALG_ASYNC |
				   CRYPTO_ALG_NEED_FALLBACK;
	alg->halg.base.cra_init = gpca_hmac_cra_init;
	alg->halg.base.cra_exit = gpca_hmac_cra_exit;
	alg->halg.base.cra_ctxsize = sizeof(struct gpca_hmac_ctx);

	alg->halg.statesize = sizeof(struct gpca_ahash_reqctx);

	alg->setkey = gpca_hmac_setkey;
	alg->init = gpca_hmac_init;
	alg->update = gpca_ahash_update;
	alg->final = gpca_ahash_final;
	alg->finup = gpca_ahash_finup;
	alg->digest = gpca_hmac_digest;
	alg->export = gpca_ahash_export;
	alg->import = gpca_ahash_import;
}

int gpca_hash_register(struct gpca_dev *gpca_dev)
{
	int ret = 0;
	u32 i = 0;

	for (i = 0; i < ARRAY_SIZE(gpca_ahash_algs); i++) {
		gpca_ahash_algs[i].gpca_dev = gpca_dev;
		gpca_ahash_alg_init(&gpca_ahash_algs[i].ahash_alg);
		ret = crypto_register_ahash(&gpca_ahash_algs[i].ahash_alg);
		if (ret)
			return ret;
		dev_info(gpca_dev->dev, "SHA AHASH register passed for iteration = %d", i);
	}

	for (i = 0; i < ARRAY_SIZE(gpca_hmac_algs); i++) {
		gpca_hmac_algs[i].gpca_dev = gpca_dev;
		gpca_hmac_alg_init(&gpca_hmac_algs[i].ahash_alg);
		ret = crypto_register_ahash(&gpca_hmac_algs[i].ahash_alg);
		if (ret)
			return ret;
		dev_info(gpca_dev->dev, "HMAC AHASH register passed for iteration = %d", i);
	}

	return 0;
}

void gpca_hash_unregister(struct gpca_dev *gpca_dev)
{
	u32 i = 0;

	for (i = 0; i < ARRAY_SIZE(gpca_ahash_algs); i++)
		crypto_unregister_ahash(&gpca_ahash_algs[i].ahash_alg);
	for (i = 0; i < ARRAY_SIZE(gpca_hmac_algs); i++)
		crypto_unregister_ahash(&gpca_hmac_algs[i].ahash_alg);
}
