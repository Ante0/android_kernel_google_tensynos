// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Linux kernel crypto asymmetric algorithms implementation.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include "gpca_asymm.h"

#include <crypto/akcipher.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/ecc.h>
#include <crypto/scatterwalk.h>
#include <crypto/sha2.h>
#include <linux/asn1_encoder.h>
#include <linux/asn1_decoder.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/minmax.h>

#include "gpca_crypto.h"
#include "gpca_ecdsa_sig.asn1.h"
#include "gpca_internal.h"
#include "gpca_key_policy.h"
#include "gpca_keys.h"

#define GOOGLE_GPCA_ASYMM_PRIORITY 300
#define ECC_UNCOMPRESSED_PUBLIC_KEY 0x04
#define ECC_CURVE_MAX_BYTES ((521 + 7) / 8)
/**
 * ASN1 encoding adds Tag and length for
 * r & s encoded as big endian integers
 * and signature encoded as sequence of r & s.
 * This encoding requires 6 bytes for encoding the
 * r, s and sequence.
 */
#define ASN1_ENCODING_BYTES 6
/**
 * r & s are encoded as integers which may need to
 * be prepended with a 0 byte if the most significant
 * bit is 1 to avoid being interpreted as negative number.
 * r & s are positive numbers.
 */
#define ASN1_MAX_PADDING_BYTES 2

#define SCATTERWALK_TO_SG 1
#define SCATTERWALK_FROM_SG 0

struct gpca_akcipher_alg {
	struct gpca_dev *gpca_dev;
	struct akcipher_alg akcipher_alg;
};

struct gpca_ecc_ctx {
	struct gpca_dev *gpca_dev;
	enum gpca_algorithm gpca_algo;
	struct gpca_key_handle *priv_key_handle;
	struct gpca_key_handle *pub_key_handle;
};

struct gpca_ecc_signature_ctx {
	u32 keylen;
	u8 r[ECC_CURVE_MAX_BYTES];
	u8 s[ECC_CURVE_MAX_BYTES];
};

struct gpca_ecc_sign_op_ctx {
	struct gpca_dev *gpca_dev;
	struct akcipher_request *req;
	struct gpca_crypto_op_handle *op_handle;
	u8 *src_buf;
	u32 src_buf_len;
	dma_addr_t src_dma_addr;
	u8 *dst_buf;
	u32 dst_buf_len;
	dma_addr_t dst_dma_addr;
	int prev_op_ret;
};

struct gpca_ecc_verify_op_ctx {
	struct gpca_dev *gpca_dev;
	struct akcipher_request *req;
	struct gpca_crypto_op_handle *op_handle;
	u8 *src_buf;
	u32 src_buf_len;
	u32 msg_digest_len;
	dma_addr_t src_dma_addr;
	int prev_op_ret;
};

static unsigned char *
asn1_encode_integer_byte_array(unsigned char *encoded_data_start,
			       const unsigned char *encoded_data_end,
			       const unsigned char *byte_array, u32 len)
{
	unsigned char *d = NULL;
	unsigned char *padded_byte_array = NULL;

	if (byte_array[0] & 0x80) {
		padded_byte_array = kcalloc(len + 1, sizeof(unsigned char), GFP_KERNEL);
		if (!padded_byte_array)
			return ERR_PTR(-ENOMEM);
		memcpy(&padded_byte_array[1], byte_array, len);
		len += 1;
		byte_array = padded_byte_array;
	}

	d = asn1_encode_octet_string(encoded_data_start, encoded_data_end,
				     byte_array, len);
	if (!IS_ERR(d))
		encoded_data_start[0] = _tag(UNIV, PRIM, INT);

	kfree(padded_byte_array);
	return d;
}

static int gpca_ecc_init_tfm(struct crypto_akcipher *tfm,
			     enum gpca_algorithm algo)
{
	struct gpca_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct akcipher_alg *akcipher_alg =
		container_of(tfm->base.__crt_alg, struct akcipher_alg, base);
	struct gpca_akcipher_alg *gpca_alg = container_of(akcipher_alg,
							  struct gpca_akcipher_alg,
							  akcipher_alg);

	ctx->gpca_dev = gpca_alg->gpca_dev;
	ctx->gpca_algo = algo;
	ctx->priv_key_handle = NULL;
	ctx->pub_key_handle = NULL;

	return 0;
}

static int gpca_ecc_p224_init_tfm(struct crypto_akcipher *tfm)
{
	return gpca_ecc_init_tfm(tfm, GPCA_ALGO_ECC_P224);
}

static int gpca_ecc_p256_init_tfm(struct crypto_akcipher *tfm)
{
	return gpca_ecc_init_tfm(tfm, GPCA_ALGO_ECC_P256);
}

static int gpca_ecc_p384_init_tfm(struct crypto_akcipher *tfm)
{
	return gpca_ecc_init_tfm(tfm, GPCA_ALGO_ECC_P384);
}

static int gpca_ecc_p521_init_tfm(struct crypto_akcipher *tfm)
{
	return gpca_ecc_init_tfm(tfm, GPCA_ALGO_ECC_P521);
}

static struct gpca_akcipher_alg gpca_akcipher_algs[] = {
	{
		.akcipher_alg = {
			.init = gpca_ecc_p224_init_tfm,
			.base.cra_name = "ecdsa-nist-p224",
			.base.cra_driver_name = "google-gpca-ecdsa-nist-p224",
		},
	},
	{
		.akcipher_alg = {
			.init = gpca_ecc_p256_init_tfm,
			.base.cra_name = "ecdsa-nist-p256",
			.base.cra_driver_name = "google-gpca-ecdsa-nist-p256",
		},
	},
	{
		.akcipher_alg = {
			.init = gpca_ecc_p384_init_tfm,
			.base.cra_name = "ecdsa-nist-p384",
			.base.cra_driver_name = "google-gpca-ecdsa-nist-384",
		},
	},
	{
		.akcipher_alg = {
			.init = gpca_ecc_p521_init_tfm,
			.base.cra_name = "ecdsa-nist-p521",
			.base.cra_driver_name = "google-gpca-ecdsa-nist-p521",
		},
	}
};

static void gpca_ecc_exit_tfm(struct crypto_akcipher *tfm)
{
	struct gpca_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);

	if (ctx->priv_key_handle)
		gpca_key_clear(ctx->gpca_dev, &ctx->priv_key_handle);

	if (ctx->pub_key_handle)
		gpca_key_clear(ctx->gpca_dev, &ctx->pub_key_handle);
}

static int gpca_ecc_public_key_size(enum gpca_algorithm gpca_algo)
{
	switch (gpca_algo) {
	case GPCA_ALGO_ECC_P224:
		return 56;
	case GPCA_ALGO_ECC_P256:
		return 64;
	case GPCA_ALGO_ECC_P384:
		return 96;
	case GPCA_ALGO_ECC_P521:
		return 132;
	default:
		return 0;
	}
}

static int gpca_ecc_msg_digest_size(enum gpca_algorithm gpca_algo)
{
	switch (gpca_algo) {
	case GPCA_ALGO_ECC_P224:
		return SHA224_DIGEST_SIZE;
	case GPCA_ALGO_ECC_P256:
		return SHA256_DIGEST_SIZE;
	case GPCA_ALGO_ECC_P384:
		return SHA384_DIGEST_SIZE;
	case GPCA_ALGO_ECC_P521:
		return SHA512_DIGEST_SIZE;
	default:
		return 0;
	}
}

static int gpca_ecc_set_pub_key(struct crypto_akcipher *tfm, const void *key,
				unsigned int keylen)
{
	struct gpca_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	const u8 *d = key;
	int ret;

	if (!key || keylen < 1 ||
	    (keylen - 1) < gpca_ecc_public_key_size(ctx->gpca_algo))
		return -EINVAL;

	/* Only uncompressed format of public key is supported */
	if (d[0] != ECC_UNCOMPRESSED_PUBLIC_KEY) {
		dev_err(ctx->gpca_dev->dev,
			"Public key not in uncompressed format");
		return -EINVAL;
	}

	keylen--;

	if (ctx->pub_key_handle) {
		ret = gpca_key_clear(ctx->gpca_dev, &ctx->pub_key_handle);
		if (ret)
			return ret;
	}

	ret = gpca_key_set_public_key(ctx->gpca_dev, &ctx->pub_key_handle,
				      &d[1], keylen);
	dev_dbg(ctx->gpca_dev->dev, "Import public key for algo = %d ret = %d",
		ctx->gpca_algo, ret);

	return ret;
}

static int gpca_ecc_set_priv_key(struct crypto_akcipher *tfm, const void *key,
				 unsigned int keylen)
{
	struct gpca_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct gpca_key_policy ecc_priv_kp = { GPCA_KEY_CLASS_PORTABLE,
					       GPCA_ALGO_ECC_P224,
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
					       GPCA_AUTH_DOMAIN_ANDROID_VM };
	int ret = 0;

	ecc_priv_kp.algo = ctx->gpca_algo;

	if (ctx->priv_key_handle) {
		ret = gpca_key_clear(ctx->gpca_dev, &ctx->priv_key_handle);
		if (ret)
			return ret;
	}

	ret = gpca_key_import(ctx->gpca_dev, &ctx->priv_key_handle,
			      &ecc_priv_kp, key, keylen);
	dev_dbg(ctx->gpca_dev->dev, "Import private key for algo = %d ret = %d",
		ctx->gpca_algo, ret);

	return ret;
}

/*
 * Get the r and s components of a signature from the ASN1 encoded signature.
 */
static int gpca_ecc_get_signature_rs(u8 *dest, size_t hdrlen, unsigned char tag,
				     const void *value, size_t vlen,
				     unsigned int keylen)
{
	ssize_t diff = vlen - keylen;
	const char *d = value;

	if (!value || !vlen)
		return -EINVAL;

	/* diff = 0: 'value' has exactly the right size
	 * diff > 0: 'value' has too many bytes; one leading zero is allowed that
	 *           makes the value a positive integer; error on more
	 * diff < 0: 'value' is missing leading zeros, which we add
	 */
	if (diff > 0) {
		/* skip over leading zeros that make 'value' a positive int */
		if (*d == 0) {
			vlen -= 1;
			diff--;
			d++;
		}
		if (diff)
			return -EINVAL;
	}
	if (-diff >= keylen)
		return -EINVAL;

	if (diff) {
		/* leading zeros not given in 'value' */
		memset(dest, 0, -diff);
	}

	memcpy(&dest[-diff], d, vlen);

	return 0;
}

int gpca_ecc_get_signature_r(void *context, size_t hdrlen, unsigned char tag,
			     const void *value, size_t vlen)
{
	struct gpca_ecc_signature_ctx *sig = context;

	return gpca_ecc_get_signature_rs(sig->r, hdrlen, tag, value, vlen,
					 sig->keylen);
}

int gpca_ecc_get_signature_s(void *context, size_t hdrlen, unsigned char tag,
			     const void *value, size_t vlen)
{
	struct gpca_ecc_signature_ctx *sig = context;

	return gpca_ecc_get_signature_rs(sig->s, hdrlen, tag, value, vlen,
					 sig->keylen);
}

static void gpca_ecc_verify_cleanup_and_complete(struct gpca_ecc_verify_op_ctx *op_ctx, int ret)
{
	struct akcipher_request *req = op_ctx->req;
	int prev_op_ret = op_ctx->prev_op_ret;

	gpca_crypto_op_handle_free(op_ctx->op_handle);
	kfree(op_ctx);
	akcipher_request_complete(req, prev_op_ret ? prev_op_ret : ret);
}

static void gpca_ecc_clear_verify_op_cb(int ret, void *cb_ctx)
{
	struct gpca_ecc_verify_op_ctx *op_ctx =
		(struct gpca_ecc_verify_op_ctx *)cb_ctx;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op callback ret = %d", ret);
	gpca_ecc_verify_cleanup_and_complete(op_ctx, ret);
}

static void gpca_ecc_verify_op_cb(int ret, void *cb_ctx)
{
	struct gpca_ecc_verify_op_ctx *op_ctx =
		(struct gpca_ecc_verify_op_ctx *)cb_ctx;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(op_ctx->gpca_dev->dev, "Start verify op callback ret = %d", ret);
	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
			  op_ctx->src_buf, op_ctx->src_dma_addr);

	op_ctx->prev_op_ret = ret;
	ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
				   gpca_ecc_clear_verify_op_cb, op_ctx);
	dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op ret = %d", ret);
	if (ret)
		gpca_ecc_verify_cleanup_and_complete(op_ctx, ret);
}

static void gpca_ecc_verify_init_cb(int ret, void *cb_ctx)
{
	struct gpca_ecc_verify_op_ctx *op_ctx =
		(struct gpca_ecc_verify_op_ctx *)cb_ctx;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(op_ctx->gpca_dev->dev,
		"Set asymmetric algorithm parameters callback ret = %d", ret);

	if (ret) {
		dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
				  op_ctx->src_buf, op_ctx->src_dma_addr);
		gpca_ecc_verify_cleanup_and_complete(op_ctx, ret);
	}

	ret = gpca_crypto_start_verify_op(op_ctx->gpca_dev,
					  op_ctx->op_handle,
					  op_ctx->src_dma_addr,
					  op_ctx->msg_digest_len,
					  op_ctx->src_buf_len - op_ctx->msg_digest_len,
					  true,
					  gpca_ecc_verify_op_cb, op_ctx);
	dev_dbg(op_ctx->gpca_dev->dev,
		"Start verify op input_data_len = %d signature_len = %d ret = %d",
		op_ctx->msg_digest_len,
		op_ctx->src_buf_len - op_ctx->msg_digest_len, ret);
	if (ret) {
		op_ctx->prev_op_ret = ret;
		dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
				  op_ctx->src_buf, op_ctx->src_dma_addr);
		ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
					   gpca_ecc_clear_verify_op_cb, op_ctx);
		dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op ret = %d", ret);
		if (ret)
			gpca_ecc_verify_cleanup_and_complete(op_ctx, ret);
	}
}

static int gpca_ecc_verify(struct akcipher_request *req)
{
	int ret = 0;
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct gpca_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct gpca_ecc_signature_ctx sig_ctx;
	u8 *digest_and_signature = NULL;
	u32 offset = 0;
	struct gpca_ecc_verify_op_ctx *op_ctx = NULL;

	dev_dbg(ctx->gpca_dev->dev,
		"ECC verify algo = %d src_len = %d dst_len = %d",
		ctx->gpca_algo, req->src_len, req->dst_len);

	if (!ctx->pub_key_handle)
		return -EINVAL;

	sig_ctx.keylen = gpca_ecc_public_key_size(ctx->gpca_algo) >> 1;

	digest_and_signature = kmalloc(req->src_len + req->dst_len, GFP_KERNEL);
	if (!digest_and_signature)
		return -ENOMEM;

	scatterwalk_map_and_copy(digest_and_signature, req->src, 0,
				 req->src_len + req->dst_len,
				 SCATTERWALK_FROM_SG);

	ret = asn1_ber_decoder(&gpca_ecdsa_sig_decoder, &sig_ctx,
			       digest_and_signature, req->src_len);
	dev_dbg(ctx->gpca_dev->dev, "ECC ASN1 signature decode ret = %d", ret);
	if (ret)
		goto free_digest_and_signature;

	op_ctx = kcalloc(1, sizeof(*op_ctx), GFP_KERNEL);
	if (!op_ctx) {
		ret = -ENOMEM;
		goto free_digest_and_signature;
	}

	op_ctx->gpca_dev = ctx->gpca_dev;
	op_ctx->req = req;
	op_ctx->prev_op_ret = 0;

	op_ctx->msg_digest_len = gpca_ecc_msg_digest_size(ctx->gpca_algo);
	op_ctx->src_buf_len = gpca_ecc_public_key_size(ctx->gpca_algo) +
			      gpca_ecc_msg_digest_size(ctx->gpca_algo);
	op_ctx->src_buf = dma_alloc_coherent(op_ctx->gpca_dev->dev,
					     op_ctx->src_buf_len,
					     &op_ctx->src_dma_addr, GFP_KERNEL);
	if (!op_ctx->src_buf) {
		ret = -ENOMEM;
		goto free_op_ctx;
	}
	if (gpca_ecc_msg_digest_size(ctx->gpca_algo) > req->dst_len) {
		memset(op_ctx->src_buf, 0,
		       gpca_ecc_msg_digest_size(ctx->gpca_algo) - req->dst_len);
		offset += (gpca_ecc_msg_digest_size(ctx->gpca_algo) -
			   req->dst_len);
		memcpy(op_ctx->src_buf + offset,
		       &digest_and_signature[req->src_len], req->dst_len);
		offset += req->dst_len;
	} else {
		memcpy(op_ctx->src_buf, &digest_and_signature[req->src_len],
		       gpca_ecc_msg_digest_size(ctx->gpca_algo));
		offset += gpca_ecc_msg_digest_size(ctx->gpca_algo);
	}

	memcpy(op_ctx->src_buf + offset, sig_ctx.r, sig_ctx.keylen);
	offset += sig_ctx.keylen;
	memcpy(op_ctx->src_buf + offset, sig_ctx.s, sig_ctx.keylen);

	op_ctx->op_handle = gpca_crypto_op_handle_alloc();
	if (!op_ctx->op_handle) {
		ret = -ENOMEM;
		goto free_src_buf;
	}
	ret = gpca_crypto_set_asymm_algo_params(ctx->gpca_dev,
						op_ctx->op_handle,
						ctx->pub_key_handle,
						ctx->gpca_algo,
						GPCA_SUPPORTED_PURPOSE_VERIFY,
						gpca_ecc_verify_init_cb, op_ctx);
	dev_dbg(ctx->gpca_dev->dev,
		"Set asymmetric algorithm parameters algo = %d ret = %d",
		ctx->gpca_algo, ret);

	if (ret == 0) {
		kfree(digest_and_signature);
		return -EINPROGRESS;
	}

	gpca_crypto_op_handle_free(op_ctx->op_handle);
free_src_buf:
	dma_free_coherent(ctx->gpca_dev->dev, op_ctx->src_buf_len,
			  op_ctx->src_buf, op_ctx->src_dma_addr);
free_op_ctx:
	kfree(op_ctx);
free_digest_and_signature:
	kfree(digest_and_signature);
	return ret;
}

static unsigned int gpca_ecc_max_size(struct crypto_akcipher *tfm)
{
	struct gpca_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);

	return gpca_ecc_public_key_size(ctx->gpca_algo) + ASN1_ENCODING_BYTES +
	       ASN1_MAX_PADDING_BYTES;
}

static void gpca_ecc_sign_cleanup_and_complete(struct gpca_ecc_sign_op_ctx *op_ctx, int ret)
{
	struct akcipher_request *req = op_ctx->req;
	int prev_op_ret = op_ctx->prev_op_ret;

	gpca_crypto_op_handle_free(op_ctx->op_handle);
	kfree(op_ctx);
	akcipher_request_complete(req, prev_op_ret ? prev_op_ret : ret);
}

static void gpca_ecc_clear_sign_op_cb(int ret, void *cb_ctx)
{
	struct gpca_ecc_sign_op_ctx *op_ctx =
		(struct gpca_ecc_sign_op_ctx *)cb_ctx;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op callback ret = %d", ret);
	gpca_ecc_sign_cleanup_and_complete(op_ctx, ret);
}

static void gpca_ecc_sign_op_cb(int ret, void *cb_ctx)
{
	struct gpca_ecc_sign_op_ctx *op_ctx =
		(struct gpca_ecc_sign_op_ctx *)cb_ctx;
	u8 *rs_encoding_start = NULL, *rs_encoding_end = NULL;
	u8 *encoded_sig_start = NULL, *encoded_sig_end = NULL;
	u8 *cur_ptr = NULL;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(op_ctx->gpca_dev->dev,
		"ECC sign GPCA start operation callback ret = %d",
		ret);
	if (ret)
		goto error;

	rs_encoding_start = kmalloc(op_ctx->req->dst_len, GFP_KERNEL);
	if (!rs_encoding_start) {
		ret = -ENOMEM;
		goto error;
	}
	rs_encoding_end = rs_encoding_start + op_ctx->req->dst_len;
	encoded_sig_start = kmalloc(op_ctx->req->dst_len, GFP_KERNEL);
	if (!encoded_sig_start) {
		ret = -ENOMEM;
		goto free_rs_encoding;
	}
	encoded_sig_end = encoded_sig_start + op_ctx->req->dst_len;

	cur_ptr = asn1_encode_integer_byte_array(rs_encoding_start,
						 rs_encoding_end,
						 op_ctx->dst_buf,
						 op_ctx->dst_buf_len / 2);
	if (IS_ERR(cur_ptr)) {
		ret = PTR_ERR(cur_ptr);
		goto free_encoded_sig;
	}

	cur_ptr = asn1_encode_integer_byte_array(cur_ptr,
						 rs_encoding_end,
						 op_ctx->dst_buf + (op_ctx->dst_buf_len / 2),
						 op_ctx->dst_buf_len / 2);
	if (IS_ERR(cur_ptr)) {
		ret = PTR_ERR(cur_ptr);
		goto free_encoded_sig;
	}

	cur_ptr = asn1_encode_sequence(encoded_sig_start, encoded_sig_end,
				       rs_encoding_start,
				       cur_ptr - rs_encoding_start);
	if (IS_ERR(cur_ptr)) {
		ret = PTR_ERR(cur_ptr);
		goto free_encoded_sig;
	}

	op_ctx->req->dst_len = cur_ptr - encoded_sig_start;
	scatterwalk_map_and_copy(encoded_sig_start, op_ctx->req->dst, 0,
				 op_ctx->req->dst_len, SCATTERWALK_TO_SG);

	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->dst_buf_len,
			  op_ctx->dst_buf, op_ctx->dst_dma_addr);
	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
			  op_ctx->src_buf, op_ctx->src_dma_addr);

free_encoded_sig:
	kfree(encoded_sig_start);
free_rs_encoding:
	kfree(rs_encoding_start);
error:
	op_ctx->prev_op_ret = ret;
	ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
				   gpca_ecc_clear_sign_op_cb, op_ctx);
	dev_dbg(op_ctx->gpca_dev->dev, "Clear crypto op ret = %d", ret);
	if (ret)
		gpca_ecc_sign_cleanup_and_complete(op_ctx, ret);
}

static void gpca_ecc_sign_init_cb(int ret, void *cb_ctx)
{
	struct gpca_ecc_sign_op_ctx *op_ctx =
		(struct gpca_ecc_sign_op_ctx *)cb_ctx;
	struct akcipher_request *req = NULL;
	struct crypto_akcipher *tfm = NULL;
	struct gpca_ecc_ctx *ctx = NULL;
	u32 src_len = 0;

	WARN_ON(!op_ctx);
	if (!op_ctx)
		return;

	dev_dbg(ctx->gpca_dev->dev,
		"ECC sign set asymmetric algorithm parameters callback ret = %d",
		ret);
	req = op_ctx->req;
	tfm = crypto_akcipher_reqtfm(req);
	ctx = akcipher_tfm_ctx(tfm);

	if (ret)
		goto error;

	op_ctx->src_buf_len = gpca_ecc_msg_digest_size(ctx->gpca_algo);
	op_ctx->src_buf = dma_alloc_coherent(ctx->gpca_dev->dev,
					     op_ctx->src_buf_len,
					     &op_ctx->src_dma_addr, GFP_KERNEL);
	if (!op_ctx->src_buf) {
		ret = -ENOMEM;
		goto error;
	}

	op_ctx->dst_buf_len = gpca_ecc_public_key_size(ctx->gpca_algo);
	op_ctx->dst_buf = dma_alloc_coherent(ctx->gpca_dev->dev,
					     op_ctx->dst_buf_len,
					     &op_ctx->dst_dma_addr, GFP_KERNEL);
	if (!op_ctx->dst_buf) {
		ret = -ENOMEM;
		goto free_src_buf;
	}

	src_len = min_t(u32, op_ctx->src_buf_len, op_ctx->req->src_len);
	scatterwalk_map_and_copy(op_ctx->src_buf, op_ctx->req->src, 0, src_len,
				 SCATTERWALK_FROM_SG);
	if (src_len < op_ctx->src_buf_len)
		memset(op_ctx->src_buf + src_len, 0,
		       op_ctx->src_buf_len - src_len);

	ret = gpca_crypto_start_sign_op(op_ctx->gpca_dev,
					op_ctx->op_handle,
					op_ctx->src_dma_addr,
					op_ctx->src_buf_len,
					op_ctx->dst_dma_addr,
					op_ctx->dst_buf_len,
					true,
					gpca_ecc_sign_op_cb, op_ctx);
	dev_dbg(ctx->gpca_dev->dev,
		"ECC sign start operation input_data_len = %d output_data_len = %d ret = %d",
		op_ctx->src_buf_len, op_ctx->dst_buf_len, ret);

	if (ret == 0)
		return;

	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->dst_buf_len,
			  op_ctx->dst_buf, op_ctx->dst_dma_addr);
free_src_buf:
	dma_free_coherent(op_ctx->gpca_dev->dev, op_ctx->src_buf_len,
			  op_ctx->src_buf, op_ctx->src_dma_addr);
	op_ctx->prev_op_ret = ret;
	ret = gpca_crypto_clear_op(op_ctx->gpca_dev, op_ctx->op_handle,
				   gpca_ecc_clear_sign_op_cb, op_ctx);
	if (ret == 0)
		return;
error:
	gpca_ecc_sign_cleanup_and_complete(op_ctx, ret);
}

static int gpca_ecc_sign(struct akcipher_request *req)
{
	int ret = 0;
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct gpca_ecc_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct gpca_ecc_sign_op_ctx *op_ctx = NULL;

	dev_dbg(ctx->gpca_dev->dev,
		"ECC sign algo = %d src_len = %d",
		ctx->gpca_algo, req->src_len);

	if (req->dst_len < gpca_ecc_max_size(tfm)) {
		req->dst_len = gpca_ecc_max_size(tfm);
		return -EINVAL;
	}

	req->dst_len = gpca_ecc_max_size(tfm);
	if (!ctx->priv_key_handle)
		return -EINVAL;

	op_ctx = kcalloc(1, sizeof(*op_ctx), GFP_KERNEL);
	if (!op_ctx)
		return -ENOMEM;

	op_ctx->gpca_dev = ctx->gpca_dev;
	op_ctx->req = req;
	op_ctx->op_handle = gpca_crypto_op_handle_alloc();
	if (!op_ctx->op_handle)
		goto free_op_ctx;

	ret = gpca_crypto_set_asymm_algo_params(op_ctx->gpca_dev,
						op_ctx->op_handle, ctx->priv_key_handle,
						ctx->gpca_algo,
						GPCA_SUPPORTED_PURPOSE_SIGN,
						gpca_ecc_sign_init_cb, op_ctx);
	dev_dbg(ctx->gpca_dev->dev,
		"ECC sign set asymmetric algorithm parameters algo = %d ret = %d",
		ctx->gpca_algo, ret);
	if (ret == 0)
		return -EINPROGRESS;

	gpca_crypto_op_handle_free(op_ctx->op_handle);
free_op_ctx:
	kfree(op_ctx);
	return ret;
}

static void gpca_ecc_alg_init(struct akcipher_alg *alg)
{
	alg->base.cra_module = THIS_MODULE;
	alg->base.cra_priority = GOOGLE_GPCA_ASYMM_PRIORITY;
	alg->base.cra_ctxsize = sizeof(struct gpca_ecc_ctx);
	alg->exit = gpca_ecc_exit_tfm;
	alg->set_pub_key = gpca_ecc_set_pub_key;
	alg->set_priv_key = gpca_ecc_set_priv_key;
	alg->verify = gpca_ecc_verify;
	alg->max_size = gpca_ecc_max_size;
	alg->sign = gpca_ecc_sign;
}

int gpca_asymm_register(struct gpca_dev *gpca_dev)
{
	u32 i = 0;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(gpca_akcipher_algs); i++) {
		gpca_akcipher_algs[i].gpca_dev = gpca_dev;
		gpca_ecc_alg_init(&gpca_akcipher_algs[i].akcipher_alg);
		ret = crypto_register_akcipher(&gpca_akcipher_algs[i].akcipher_alg);
		if (ret)
			return ret;
		dev_info(gpca_dev->dev, "Registered ECC algorithm %d", i);
	}

	return ret;
}

void gpca_asymm_unregister(struct gpca_dev *gpca_dev)
{
	u32 i = 0;

	for (i = 0; i < ARRAY_SIZE(gpca_akcipher_algs); i++)
		crypto_unregister_akcipher(&gpca_akcipher_algs[i].akcipher_alg);
}
