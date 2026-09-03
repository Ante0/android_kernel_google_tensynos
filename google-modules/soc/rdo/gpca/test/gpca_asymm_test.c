// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Crypto asymmetric unit tests.
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <crypto/akcipher.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/types.h>

#include "gpca_crypto_test.h"
#include "gpca_internal.h"
#include "gpca_keys.h"
#include "gpca_sha_test_vectors.h"
#include "gpca_test_common.h"

extern struct gpca_dev *gpca_ctx;

#define SIGN_OP GPCA_SUPPORTED_PURPOSE_SIGN
#define VERIFY_OP GPCA_SUPPORTED_PURPOSE_VERIFY

void gpca_crypto_ecc(struct kunit *test)
{
	struct gpca_key_handle *priv_key = NULL;
	struct gpca_key_handle *pub_key = NULL;
	u32 i = 0, j = 0;
	enum gpca_algorithm algo;
	dma_addr_t input_dma_addr;
	dma_addr_t output_dma_addr;
	u8 *hash_buf = NULL;
	u8 *signature_buf = NULL;
	u32 signature_size = 0;
	u8 *hash_and_signature = NULL;
	struct gpca_key_policy priv_kp = { GPCA_KEY_CLASS_PORTABLE,
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
	/* Public key is padded to nearest 32B multiple. For P521 size is 160B */
	u8 pub_key_buf[160] = { 0 };
	u32 pub_key_size = 0;
	struct gpca_crypto_op_handle *op_handle = NULL;

	op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, op_handle);

	for (i = 0; i < ARRAY_SIZE(sha_tests); i++) {
		switch (sha_tests[i].algo) {
		case GPCA_ALGO_SHA2_224:
			algo = GPCA_ALGO_ECC_P224;
			signature_size = 56;
			break;
		case GPCA_ALGO_SHA2_256:
			algo = GPCA_ALGO_ECC_P256;
			signature_size = 64;
			break;
		case GPCA_ALGO_SHA2_384:
			algo = GPCA_ALGO_ECC_P384;
			signature_size = 96;
			break;
		case GPCA_ALGO_SHA2_512:
			algo = GPCA_ALGO_ECC_P521;
			signature_size = 132;
			break;
		default:
			KUNIT_FAIL(test, "Invalid algorithm type");
		}

		priv_kp.algo = algo;
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_generate(gpca_ctx, &priv_key, &priv_kp),
				    "Key generate failed for iteration = %d", i);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_asymm_algo_params_sync(gpca_ctx,
									   op_handle,
									   priv_key,
									   algo,
									   SIGN_OP),
				    "Set crypto params failed for iteration = %d", i);
		hash_buf = kmalloc(sha_tests[i].hash_size, GFP_KERNEL);
		signature_buf = kmalloc(signature_size, GFP_KERNEL);

		for (j = 0; j < sha_tests[i].hash_size; j++)
			hash_buf[j] = sha_tests[i].hash[j];

		/* DMA map */
		input_dma_addr = dma_map_single(gpca_ctx->dev, hash_buf,
						sha_tests[i].hash_size,
						DMA_TO_DEVICE);
		output_dma_addr = dma_map_single(gpca_ctx->dev, signature_buf,
						 signature_size,
						 DMA_FROM_DEVICE);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_start_sign_op_sync(gpca_ctx, op_handle,
								   input_dma_addr,
								   sha_tests[i].hash_size,
								   output_dma_addr,
								   signature_size, true),
				    "Start crypto op(ptrs) failed for iteration = %d", i);
		/* DMA Unmap */
		dma_unmap_single(gpca_ctx->dev, input_dma_addr,
				 sha_tests[i].hash_size, DMA_TO_DEVICE);
		dma_unmap_single(gpca_ctx->dev, output_dma_addr, signature_size,
				 DMA_FROM_DEVICE);
		/* Clear op slot */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
				    "Clear op slot failed for iteration = %d",
				    i);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_get_public_key(gpca_ctx, priv_key, pub_key_buf,
							    ARRAY_SIZE(pub_key_buf),
							    &pub_key_size),
				    "Get public key failed for iteration = %d", i);
		/*
		 * Public key received is padded to 32B, send public key to set
		 * public key without padding
		 */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_set_public_key(gpca_ctx, &pub_key, pub_key_buf,
							    signature_size),
				    "Set public key failed for iteration = %d", i);

		hash_and_signature = kmalloc(sha_tests[i].hash_size + signature_size, GFP_KERNEL);
		for (j = 0; j < sha_tests[i].hash_size; j++)
			hash_and_signature[j] = sha_tests[i].hash[j];
		for (j = 0; j < signature_size; j++)
			hash_and_signature[sha_tests[i].hash_size + j] =
				signature_buf[j];
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_asymm_algo_params_sync(gpca_ctx,
									   op_handle,
									   pub_key,
									   algo,
									   VERIFY_OP),
				    "Set crypto params failed for iteration = %d", i);

		/* DMA map */
		input_dma_addr = dma_map_single(gpca_ctx->dev, hash_and_signature,
						sha_tests[i].hash_size + signature_size,
						DMA_TO_DEVICE);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_start_verify_op_sync(gpca_ctx, op_handle,
								     input_dma_addr,
								     sha_tests[i].hash_size,
								     signature_size, true),
				    "Start crypto op(ptrs) failed for iteration = %d", i);
		/* DMA Unmap */
		dma_unmap_single(gpca_ctx->dev, input_dma_addr,
				 sha_tests[i].hash_size + signature_size,
				 DMA_TO_DEVICE);

		/* Clear op slot */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_clear_op_sync(gpca_ctx, op_handle),
				    "Clear op slot failed for iteration = %d",
				    i);

		KUNIT_EXPECT_EQ_MSG(test, 0, gpca_key_clear(gpca_ctx, &priv_key),
				    "Clear private key failed for iteration = %d", i);
		KUNIT_EXPECT_EQ_MSG(test, 0, gpca_key_clear(gpca_ctx, &pub_key),
				    "Clear public key failed for iteration = %d", i);

		kfree(hash_and_signature);
		kfree(signature_buf);
		kfree(hash_buf);
	}
	gpca_crypto_op_handle_free(op_handle);
}

struct lkc_ecc_test_vector_t {
	char *alg_name;
	enum gpca_algorithm gpca_algo;
	const u8 *priv_key;
	u32 priv_key_len;
	const u8 *msg_digest;
	u32 msg_digest_len;
	u32 public_key_size;
};

const u8 ecc_p224_key[] = { 0x56, 0x55, 0x77, 0xa4, 0x94, 0x15, 0xca,
			    0x76, 0x1a, 0x03, 0x22, 0xad, 0x54, 0xe4,
			    0xad, 0x0a, 0xe7, 0x62, 0x51, 0x74, 0xba,
			    0xf3, 0x72, 0xc2, 0x81, 0x6f, 0x53, 0x28 };

const u8 ecc_p256_key[] = { 0x06, 0x12, 0x46, 0x5c, 0x89, 0xa0, 0x23, 0xab,
			    0x17, 0x85, 0x5b, 0x0a, 0x6b, 0xce, 0xbf, 0xd3,
			    0xfe, 0xbb, 0x53, 0xae, 0xf8, 0x41, 0x38, 0x64,
			    0x7b, 0x53, 0x52, 0xe0, 0x2c, 0x10, 0xc3, 0x46 };

const u8 ecc_p384_key[] = { 0x76, 0x6e, 0x61, 0x42, 0x5b, 0x2d, 0xa9, 0xf8,
			    0x46, 0xc0, 0x9f, 0xc3, 0x56, 0x4b, 0x93, 0xa6,
			    0xf8, 0x60, 0x3b, 0x73, 0x92, 0xc7, 0x85, 0x16,
			    0x5b, 0xf2, 0x0d, 0xa9, 0x48, 0xc4, 0x9f, 0xd1,
			    0xfb, 0x1d, 0xee, 0x4e, 0xdd, 0x64, 0x35, 0x6b,
			    0x9f, 0x21, 0xc5, 0x88, 0xb7, 0x5d, 0xfd, 0x81 };

const u8 ecc_p521_key[] = {
	0x01, 0x93, 0x99, 0x82, 0xb5, 0x29, 0x59, 0x6c, 0xe7, 0x7a, 0x94,
	0xbc, 0x6e, 0xfd, 0x03, 0xe9, 0x2c, 0x21, 0xa8, 0x49, 0xeb, 0x4f,
	0x87, 0xb8, 0xf6, 0x19, 0xd5, 0x06, 0xef, 0xc9, 0xbb, 0x22, 0xe7,
	0xc6, 0x16, 0x40, 0xc9, 0x0d, 0x59, 0x8f, 0x79, 0x5b, 0x64, 0x56,
	0x6d, 0xc6, 0xdf, 0x43, 0x99, 0x2a, 0xe3, 0x4a, 0x13, 0x41, 0xd4,
	0x58, 0x57, 0x44, 0x40, 0xa7, 0x37, 0x1f, 0x61, 0x1c, 0x7d, 0xcd
};

struct lkc_ecc_test_vector_t lkc_ecc_tests[] = {
	{
		"ecdsa-nist-p224",
		GPCA_ALGO_ECC_P224,
		ecc_p224_key,
		ARRAY_SIZE(ecc_p224_key),
		sha224_hash,
		ARRAY_SIZE(sha224_hash),
		56,
	},
	{
		"ecdsa-nist-p256",
		GPCA_ALGO_ECC_P256,
		ecc_p256_key,
		ARRAY_SIZE(ecc_p256_key),
		sha256_hash,
		ARRAY_SIZE(sha256_hash),
		64,
	},
	{
		"ecdsa-nist-p384",
		GPCA_ALGO_ECC_P384,
		ecc_p384_key,
		ARRAY_SIZE(ecc_p384_key),
		sha384_hash,
		ARRAY_SIZE(sha384_hash),
		96,
	},
	{
		"ecdsa-nist-p521",
		GPCA_ALGO_ECC_P521,
		ecc_p521_key,
		ARRAY_SIZE(ecc_p521_key),
		sha512_hash,
		ARRAY_SIZE(sha512_hash),
		132,
	},
};

void gpca_crypto_lkc_ecc(struct kunit *test)
{
	struct crypto_akcipher *tfm;
	struct akcipher_request *req;
	struct scatterlist src, dst;
	struct crypto_wait wait;
	struct scatterlist src_tab[2];
	struct gpca_key_handle *key_handle = NULL;
	u8 *sig = NULL;
	u32 i = 0;
	struct gpca_key_policy ecc_priv_kp = { GPCA_KEY_CLASS_PORTABLE,
					       GPCA_ALGO_ECC_P256,
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
	u8 pub_key[161];
	u32 pub_key_size = 0;
	u8 *digest = NULL;

	for (i = 0; i < ARRAY_SIZE(lkc_ecc_tests); i++) {
		tfm = crypto_alloc_akcipher(lkc_ecc_tests[i].alg_name, 0, 0);
		KUNIT_EXPECT_EQ_MSG(test, 0, IS_ERR(tfm),
				    "Failed to load tfm for %s ret = %d",
				    lkc_ecc_tests[i].alg_name, PTR_ERR(tfm));

		req = akcipher_request_alloc(tfm, GFP_KERNEL);
		KUNIT_EXPECT_PTR_NE_MSG(test, NULL, req,
					"Request alloc failed for iteration = %d", i);

		KUNIT_EXPECT_EQ_MSG(test, 0,
				    crypto_akcipher_set_priv_key(tfm,
								 lkc_ecc_tests[i].priv_key,
								 lkc_ecc_tests[i].priv_key_len),
				    "Set private key failed for iteration = %d",
				    i);

		digest = kmalloc(lkc_ecc_tests[i].msg_digest_len, GFP_KERNEL);
		KUNIT_EXPECT_PTR_NE_MSG(test, NULL, digest,
					"Digest buffer allocation failed for iteration = %d",
					i);
		memcpy(digest, lkc_ecc_tests[i].msg_digest, lkc_ecc_tests[i].msg_digest_len);
		sig = kmalloc(crypto_akcipher_maxsize(tfm), GFP_KERNEL);
		KUNIT_EXPECT_PTR_NE_MSG(test, NULL, sig,
					"Signature buffer allocation failed for iteration = %d",
					i);

		sg_init_one(&src, digest, lkc_ecc_tests[i].msg_digest_len);
		sg_init_one(&dst, sig, crypto_akcipher_maxsize(tfm));
		crypto_init_wait(&wait);
		akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					      crypto_req_done, &wait);
		akcipher_request_set_crypt(req, &src, &dst,
					   lkc_ecc_tests[i].msg_digest_len,
					   crypto_akcipher_maxsize(tfm));

		KUNIT_EXPECT_EQ_MSG(test, 0,
				    crypto_wait_req(crypto_akcipher_sign(req),
						    &wait),
				    "ECC sign failed for iteration = %d", i);

		ecc_priv_kp.algo = lkc_ecc_tests[i].gpca_algo;
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_import(gpca_ctx, &key_handle,
						    &ecc_priv_kp,
						    lkc_ecc_tests[i].priv_key,
						    lkc_ecc_tests[i].priv_key_len),
			"Key import failed for iteration = %d", i);
		// get public key
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_get_public_key(gpca_ctx,
							    key_handle,
							    pub_key,
							    ARRAY_SIZE(pub_key),
							    &pub_key_size),
				    "Get public key failed for iteration = %d",
				    i);
		// clear private key
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_clear(gpca_ctx, &key_handle),
				    "Clear key failed for iteration = %d", i);
		/*
		 * Set the public key size.
		 * Get public key output is padded to 32B alignment.
		 */
		pub_key_size = lkc_ecc_tests[i].public_key_size;
		// prepend 0x04 to public key
		memmove(pub_key + 1, pub_key, pub_key_size);
		pub_key[0] = 0x04;
		pub_key_size += 1;

		KUNIT_EXPECT_EQ_MSG(test, 0,
				    crypto_akcipher_set_pub_key(tfm, pub_key, pub_key_size),
				    "Set public key failed for iteration = %d", i);
		sg_init_table(src_tab, 2);
		sg_set_buf(&src_tab[0], sig, req->dst_len);
		sg_set_buf(&src_tab[1], digest, lkc_ecc_tests[i].msg_digest_len);

		akcipher_request_set_crypt(req, src_tab, NULL, req->dst_len,
					   lkc_ecc_tests[i].msg_digest_len);

		KUNIT_EXPECT_EQ_MSG(test, 0,
				    crypto_wait_req(crypto_akcipher_verify(req),
						    &wait),
				    "ECC verify failed for iteration = %d", i);
		crypto_free_akcipher(tfm);
		kfree(sig);
	}
}
