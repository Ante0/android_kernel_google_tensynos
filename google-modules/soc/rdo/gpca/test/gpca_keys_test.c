// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPCA Key management unit test.
 *
 * Copyright (C) 2022-2023 Google LLC.
 */

#include <kunit/test.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>

#include "gpca_crypto.h"
#include "gpca_internal.h"
#include "gpca_key_policy.h"
#include "gpca_keys.h"
#include "gpca_ski_test_vectors.h"

struct gpca_dev *gpca_ctx;

static void gpca_keys_storage_encryption(struct kunit *test)
{
	u8 sw_ctx[] = { 0x12, 0x34, 0x00, 0x00, 0x56, 0x78, 0x00, 0x00 };
	u32 sw_ctx_size = 8;
	u8 key_blob[96] = { 0 };
	u32 key_blob_size = 0;
	u8 ise_keyslot = 3;
	struct gpca_key_policy class_key_policy = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_AES256_CMAC,
		GPCA_SUPPORTED_PURPOSE_DERIVE,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_ENABLE,
		GPCA_WAHK_ENABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_policy wrapping_key_policy = {
		GPCA_KEY_CLASS_EPHEMERAL,
		GPCA_ALGO_AES256_KWP,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_policy fbe_key_policy = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_AES256_XTS,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};

	struct gpca_key_handle *class_key, *wrapping_key, *fbe_key;

	/* Set Class key policy & generate class key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &class_key,
					  &class_key_policy));

	/* Set Wrapping key policy & generate wrapping key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &wrapping_key,
					  &wrapping_key_policy));

	/* Wrap class key with wrapping key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_wrap(gpca_ctx, wrapping_key, class_key,
				      key_blob, ARRAY_SIZE(key_blob),
				      &key_blob_size));

	/* Clear class key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &class_key));

	/* Unwrap wrapped class key blob to get back the class key in key table */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_unwrap(gpca_ctx, wrapping_key, &class_key,
					key_blob, key_blob_size));

	/* Derive FBE key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_derive(gpca_ctx, class_key, &fbe_key,
					&fbe_key_policy, sw_ctx, sw_ctx_size));

	/* Send FBE key to UFS ISE key slot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_send(gpca_ctx, fbe_key, ISE_KEY_TABLE,
				      ise_keyslot));

	/* Clear class key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &class_key));

	/* Clear wrapping key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &wrapping_key));

	/* Clear FBE key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &fbe_key));
}

static struct gpca_key_policy valid_key_policies[] = {
	/* Hardware AES encryption key */
	{ GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Ephemeral HMAC key */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_HMAC_SHA2_384,
	  GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
	  GPCA_KMK_DISABLE, GPCA_WAEK_ENABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_DISABLE,
	  GPCA_EVICT_AP_S_DISABLE, GPCA_EVICT_AP_NS_DISABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Portable TDES key*/
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_TDES_CBC,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Key Wrapping Key */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES256_KWP,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_ENABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Key Derivation Key */
	{ GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_AES256_CMAC,
	  GPCA_SUPPORTED_PURPOSE_DERIVE, GPCA_KMK_ENABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Portable CMAC sign/verify key */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_CMAC,
	  GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Bug(b/242022464): RSA not supported in GEM5 currently
	 * // RSA key
	 * { GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_RSA_2048,
	 * GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	 * GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	 * GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	 * GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	 * GPCA_AUTH_DOMAIN_ANDROID_VM },
	 */
	/* ECC Key exchange key */
	{ GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_ECC_P384,
	  GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE, GPCA_KMK_ENABLE,
	  GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE,
	  GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* ECC Sign Key */
	{ GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_ECC_P256,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* ECC P521 Sign Key */
	{ GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_ECC_P521,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
};

static struct gpca_key_policy invalid_key_policies[] = {
	/* WAHK = 1 for ephemeral key */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_ENABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* WAPK = 1 for ephemeral key */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_ENABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* WAPK = 1 for Hardware key */
	{ GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_ENABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* AES key with purpose = Sign */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT |
		  GPCA_SUPPORTED_PURPOSE_SIGN,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* AES CMAC with purpose = Encrypt */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES256_CMAC,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* HMAC with purpose=key exchange */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_HMAC_SHA2_384,
	  GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE, GPCA_KMK_DISABLE,
	  GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE,
	  GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* AES256_KWP with purpose=derive */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES256_KWP,
	  GPCA_SUPPORTED_PURPOSE_DERIVE, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Invalid key class */
	{ 3, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
#if 0 /*GEM5 panics for invalid crypto param */
	/* Invalid Crypto param */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_SHA2_512 + 1,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT |
		  GPCA_SUPPORTED_PURPOSE_SIGN,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
#endif
	/* Invalid supported purpose */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES128_CBC, BIT(6),
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Invalid owner */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES128_CTR,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_GROOT, GPCA_AUTH_DOMAIN_GROOT },
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES128_CTR,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_TZ,
	  GPCA_AUTH_DOMAIN_TZ },
};

static void gpca_keys_generate(struct kunit *test)
{
	u32 i = 0;
	struct gpca_key_handle *key_handle;

	/* Valid key policies */
	for (i = 0; i < ARRAY_SIZE(valid_key_policies); i++) {
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_generate(gpca_ctx, &key_handle,
						      &valid_key_policies[i]),
				    "Failed for iteration %d", i);
		KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	}

	/* Error case: Invalid key policies */
	for (i = 0; i < ARRAY_SIZE(invalid_key_policies); i++) {
		KUNIT_EXPECT_NE_MSG(test, 0,
				    gpca_key_generate(gpca_ctx, &key_handle,
						      &invalid_key_policies[i]),
				    "Failed for iteration %d", i);
	}
}

static void gpca_keys_derive(struct kunit *test)
{
	u8 sw_ctx[] = { 0x12, 0x34, 0x00, 0x00, 0x56, 0x78, 0x00, 0x00 };
	u8 long_sw_ctx[64] = { 0 };
	u32 i = 0;

	struct gpca_key_policy key_derivation_key = {
		GPCA_KEY_CLASS_EPHEMERAL,
		GPCA_ALGO_AES256_CMAC,
		GPCA_SUPPORTED_PURPOSE_DERIVE,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_policy aes_key = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_AES256_GCM,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *src_key_handle, *dst_key_handle;

	/* Ephemeral AES256_CMAC derivation key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &src_key_handle,
					  &key_derivation_key));
	for (i = 0; i < ARRAY_SIZE(valid_key_policies); i++) {
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_key_derive(gpca_ctx, src_key_handle,
						    &dst_key_handle,
						    &valid_key_policies[i],
						    sw_ctx, ARRAY_SIZE(sw_ctx)),
				    "Failed for iteration %d", i);
		KUNIT_EXPECT_EQ(test, 0,
				gpca_key_clear(gpca_ctx, &dst_key_handle));
	}

	/* Error case : Invalid Context */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &valid_key_policies[0],
					NULL, ARRAY_SIZE(sw_ctx)));
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &valid_key_policies[0],
					sw_ctx, 0));
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &valid_key_policies[0],
					long_sw_ctx, ARRAY_SIZE(long_sw_ctx)));

	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &src_key_handle));

	/* Derive Hardware & Portable key from Hardware Derivation key */
	key_derivation_key.key_class = GPCA_KEY_CLASS_HARDWARE;
	key_derivation_key.algo = GPCA_ALGO_AES256_CMAC;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &src_key_handle,
					  &key_derivation_key));

	aes_key.key_class = GPCA_KEY_CLASS_HARDWARE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &aes_key, sw_ctx,
					ARRAY_SIZE(sw_ctx)));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &dst_key_handle));

	aes_key.key_class = GPCA_KEY_CLASS_PORTABLE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &aes_key, sw_ctx,
					ARRAY_SIZE(sw_ctx)));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &dst_key_handle));

	/* Error case : Derive ephemeral key from Hardware derivation key */
	aes_key.key_class = GPCA_KEY_CLASS_EPHEMERAL;
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &aes_key, sw_ctx,
					ARRAY_SIZE(sw_ctx)));

	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &src_key_handle));

	/* Derive Portable key from Portable key */
	key_derivation_key.key_class = GPCA_KEY_CLASS_PORTABLE;
	key_derivation_key.algo = GPCA_ALGO_AES256_CMAC;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &src_key_handle,
					  &key_derivation_key));

	aes_key.key_class = GPCA_KEY_CLASS_PORTABLE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &aes_key, sw_ctx,
					ARRAY_SIZE(sw_ctx)));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &dst_key_handle));

	/* Error case : Derive hardware key from Portable derivation key */
	aes_key.key_class = GPCA_KEY_CLASS_HARDWARE;
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &aes_key, sw_ctx,
					ARRAY_SIZE(sw_ctx)));

	/* Error case : Derive ephemeral key from Portable derivation key */
	aes_key.key_class = GPCA_KEY_CLASS_EPHEMERAL;
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &aes_key, sw_ctx,
					ARRAY_SIZE(sw_ctx)));

	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &src_key_handle));

	/* Error case : Deriving from a generic key (no derive operation) */
	aes_key.key_class = GPCA_KEY_CLASS_HARDWARE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &src_key_handle, &aes_key));

	aes_key.key_class = GPCA_KEY_CLASS_PORTABLE;
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_derive(gpca_ctx, src_key_handle,
					&dst_key_handle, &aes_key, sw_ctx,
					ARRAY_SIZE(sw_ctx)));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &src_key_handle));
}

static void gpca_keys_wrap(struct kunit *test)
{
	u8 wrapped_key_buf[96] = { 0 };
	u32 wrapped_key_buf_size;
	u8 big_wrapped_key_buf[160] = { 0 };
	u32 buf_idx = 0;

	struct gpca_key_policy key_wrapping_key = {
		GPCA_KEY_CLASS_EPHEMERAL,
		GPCA_ALGO_AES256_KWP,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM,
	};
	struct gpca_key_policy hmac_key = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_HMAC_SHA2_224,
		GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_ENABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *key_handle, *wrapping_key_handle;

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &wrapping_key_handle,
					  &key_wrapping_key));
	/* Wrap hardware key with ephemeral key */
	hmac_key.key_class = GPCA_KEY_CLASS_HARDWARE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle, &hmac_key));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf),
				      &wrapped_key_buf_size));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));

	/* Wrap portable key with ephemeral key */
	hmac_key.key_class = GPCA_KEY_CLASS_PORTABLE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle, &hmac_key));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf),
				      &wrapped_key_buf_size));

	/* Error case: Invalid wrapping buffer parameter */
	/* NULL buffer */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      NULL, ARRAY_SIZE(wrapped_key_buf),
				      &wrapped_key_buf_size));
	/* Buffer size = 0 */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf, 0,
				      &wrapped_key_buf_size));
	/* Insufficient buffer size */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf) / 2,
				      &wrapped_key_buf_size));
	/* Wrapped buffer size out ptr = NULL */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf), NULL));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));

	/* Error case: Wrapping unwrappable key */
	hmac_key.wrappable_by_ephemeral_key = GPCA_WAEK_DISABLE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle, &hmac_key));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf),
				      &wrapped_key_buf_size));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_clear(gpca_ctx, &wrapping_key_handle));

	/* Wrap portable key with hardware key */
	key_wrapping_key.key_class = GPCA_KEY_CLASS_HARDWARE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &wrapping_key_handle,
					  &key_wrapping_key));
	hmac_key.key_class = GPCA_KEY_CLASS_PORTABLE;
	hmac_key.wrappable_by_hardware_key = GPCA_WAHK_ENABLE;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle, &hmac_key));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf),
				      &wrapped_key_buf_size));

	/* Large wrapped_key_buf to check there is no buffer overflow */
	memset(big_wrapped_key_buf, 0xAB, ARRAY_SIZE(big_wrapped_key_buf));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      big_wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf),
				      &wrapped_key_buf_size));
	for (buf_idx = ARRAY_SIZE(wrapped_key_buf);
	     buf_idx < ARRAY_SIZE(big_wrapped_key_buf); buf_idx++)
		KUNIT_EXPECT_EQ_MSG(
			test, 0xAB, big_wrapped_key_buf[buf_idx],
			"Wrapped key buffer corrupted at index = %d", buf_idx);

	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_clear(gpca_ctx, &wrapping_key_handle));

	/* TODO(b/242022464): Wrap RSA key. Currently RSA not supported in GEM5. */
}

static void gpca_keys_unwrap(struct kunit *test)
{
	u8 wrapped_key_buf[96] = { 0 };
	u32 wrapped_key_buf_size = 0;

	struct gpca_key_policy key_wrapping_key = {
		GPCA_KEY_CLASS_EPHEMERAL,
		GPCA_ALGO_AES256_KWP,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_policy hmac_key = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_HMAC_SHA2_224,
		GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_ENABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *key_handle, *wrapping_key_handle;

	/* Wrapping key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &wrapping_key_handle,
					  &key_wrapping_key));

	/* Key to be wrapped */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle, &hmac_key));

	/* Wrap key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_wrap(gpca_ctx, wrapping_key_handle, key_handle,
				      wrapped_key_buf,
				      ARRAY_SIZE(wrapped_key_buf),
				      &wrapped_key_buf_size));
	/* Clear the key which was wrapped */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));

	/* Error case: Invalid wrapping buffer */
	/* NULL buffer */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_unwrap(gpca_ctx, wrapping_key_handle,
					&key_handle, NULL,
					wrapped_key_buf_size));
	/* Buffer size = 0 */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_unwrap(gpca_ctx, wrapping_key_handle,
					&key_handle, wrapped_key_buf, 0));

	/* Unwrap to put back key into the slot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_unwrap(gpca_ctx, wrapping_key_handle,
					&key_handle, wrapped_key_buf,
					wrapped_key_buf_size));
	/* Clear unwrapped key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	/* Clear wrapping key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_clear(gpca_ctx, &wrapping_key_handle));
}

static void gpca_keys_send(struct kunit *test)
{
	struct gpca_key_policy fbe_key_policy = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_AES256_XTS,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *key_handle;

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle,
					  &fbe_key_policy));

	/* Send key to different destination slots */
	/* SoC GPCA -> ISE */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_send(gpca_ctx, key_handle, ISE_KEY_TABLE, 2));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_send(gpca_ctx, key_handle, ISE_KEY_TABLE, 4));

	/* SoC GPCA -> GSA GPCA */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_send(gpca_ctx, key_handle, GSA_GPCA_KEY_TABLE,
				      2));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_send(gpca_ctx, key_handle, GSA_GPCA_KEY_TABLE,
				      3));

	/* Error case: SoC GPCA -> SoC GPCA */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_send(gpca_ctx, key_handle, SOC_GPCA_KEY_TABLE,
				      2));

	/* Error case: Invalid destination key table or slot */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_send(gpca_ctx, key_handle, ISE_KEY_TABLE,
				      100));
	KUNIT_EXPECT_NE(test, 0, gpca_key_send(gpca_ctx, key_handle, 3, 2));

	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));

	/* Error case: Sending key from empty keyslot */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_send(gpca_ctx, key_handle, ISE_KEY_TABLE, 2));
}

static void gpca_keys_clear(struct kunit *test)
{
	struct gpca_key_policy tdes_key = {
		GPCA_KEY_CLASS_PORTABLE,
		GPCA_ALGO_TDES_ECB,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *key_handle;

	/* Generate key & clear keyslot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle, &tdes_key));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));

	/* Error case: Clear empty keyslot */
	KUNIT_EXPECT_NE(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
}

static void gpca_keys_get_software_seed(struct kunit *test)
{
	u8 sw_ctx[] = { 0x12, 0x34, 0x00, 0x00, 0x56, 0x78, 0x00, 0x00 };
	u8 label[] = { 0xFF, 0xEE, 0xDD, 0xCC };
	u8 long_sw_ctx[64] = { 0 };

	u8 seed_buf[64] = { 0 };
	u32 seed_buf_size = 0;
	u8 big_seed_buf[128] = { 0xCD };
	u32 buf_idx = 0;

	struct gpca_key_policy key_derivation_key = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_AES256_CMAC,
		GPCA_SUPPORTED_PURPOSE_DERIVE,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *key_handle;

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle,
					  &key_derivation_key));
	/**
	 * Get software seed of different sizes 16B, 32B & 64B
	 * from a key derivation key
	 */
	/* 16B seed */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   16, &seed_buf_size));
	KUNIT_EXPECT_EQ(test, 16, seed_buf_size);

	/* 32B seed */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   32, &seed_buf_size));
	KUNIT_EXPECT_EQ(test, 32, seed_buf_size);

	/* 64B seed */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   64, &seed_buf_size));
	KUNIT_EXPECT_EQ(test, 64, seed_buf_size);

	/* Get seed in a large buffer to ensure no buffer overflow */
	memset(big_seed_buf, 0xCD, ARRAY_SIZE(big_seed_buf));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_software_seed(
				gpca_ctx, key_handle, label, ARRAY_SIZE(label),
				sw_ctx, ARRAY_SIZE(sw_ctx), big_seed_buf, 64,
				&seed_buf_size));
	KUNIT_EXPECT_EQ(test, 64, seed_buf_size);
	for (buf_idx = 64; buf_idx < ARRAY_SIZE(big_seed_buf); buf_idx++)
		KUNIT_EXPECT_EQ_MSG(test, 0xCD, big_seed_buf[buf_idx],
				    "Seed Buffer corrupted at index = %d",
				    buf_idx);

	/* Invalid Software seed size : 20B */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   20, &seed_buf_size));

	/* Invalid seed buffer */
	/* NULL buffer */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), NULL, 64,
						   &seed_buf_size));

	/* Buffer size = 0 */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   0, &seed_buf_size));
	/* Buffer size out ptr = NULL */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   64, NULL));

	/* Invalid Software context & label */
	/* NULL software context */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   ARRAY_SIZE(label), NULL,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   64, &seed_buf_size));

	/* Software context size = 0 */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(
				gpca_ctx, key_handle, label, ARRAY_SIZE(label),
				sw_ctx, 0, seed_buf, 64, &seed_buf_size));
	/* Software context too big ( > 32 bytes) */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(
				gpca_ctx, key_handle, label, ARRAY_SIZE(label),
				long_sw_ctx, ARRAY_SIZE(long_sw_ctx), seed_buf,
				64, &seed_buf_size));
	/* NULL Label */
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(gpca_ctx, key_handle, NULL,
						   ARRAY_SIZE(label), sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   64, &seed_buf_size));
	/* Label size = 0*/
	KUNIT_EXPECT_EQ(test, -EINVAL,
			gpca_key_get_software_seed(gpca_ctx, key_handle, label,
						   0, sw_ctx,
						   ARRAY_SIZE(sw_ctx), seed_buf,
						   64, &seed_buf_size));
	/* Label too big ( > 32 bytes)*/
	KUNIT_EXPECT_EQ(
		test, -EINVAL,
		gpca_key_get_software_seed(gpca_ctx, key_handle, long_sw_ctx,
					   ARRAY_SIZE(long_sw_ctx), sw_ctx,
					   ARRAY_SIZE(sw_ctx), seed_buf, 64,
					   &seed_buf_size));

	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
}

static u32 get_key_size_bytes(enum gpca_algorithm algo)
{
	if (gpca_ctx->drv_data->hw_bug_hmac_digest_size_keys) {
		switch (algo) {
		case GPCA_ALGO_HMAC_SHA2_224:
			return 28;
		case GPCA_ALGO_HMAC_SHA2_256:
			return 32;
		case GPCA_ALGO_HMAC_SHA2_384:
			return 48;
		case GPCA_ALGO_HMAC_SHA2_512:
			return 64;
		default:
			break;
		}
	}

	switch (algo) {
	case GPCA_ALGO_AES128_ECB:
	case GPCA_ALGO_AES128_CBC:
	case GPCA_ALGO_AES128_CTR:
	case GPCA_ALGO_AES128_GCM:
		return 16;
	case GPCA_ALGO_AES192_ECB:
	case GPCA_ALGO_AES192_CBC:
	case GPCA_ALGO_AES192_CTR:
	case GPCA_ALGO_AES192_GCM:
	case GPCA_ALGO_TDES_ECB:
	case GPCA_ALGO_TDES_CBC:
		return 24;
	case GPCA_ALGO_ECC_P224:
		return 28;
	case GPCA_ALGO_AES128_XTS:
	case GPCA_ALGO_AES256_ECB:
	case GPCA_ALGO_AES256_CBC:
	case GPCA_ALGO_AES256_CTR:
	case GPCA_ALGO_AES256_GCM:
	case GPCA_ALGO_AES256_CMAC:
	case GPCA_ALGO_AES256_KWP:
	case GPCA_ALGO_HKDF_SHA2_256:
	case GPCA_ALGO_ECC_P256:
	case GPCA_ALGO_ECC_ED25519:
		return 32;
	case GPCA_ALGO_AES192_XTS:
	case GPCA_ALGO_ECC_P384:
		return 48;
	case GPCA_ALGO_AES256_XTS:
	case GPCA_ALGO_HKDF_SHA2_512:
	case GPCA_ALGO_HMAC_SHA2_224:
	case GPCA_ALGO_HMAC_SHA2_256:
		return 64;
	case GPCA_ALGO_ECC_P521:
		return 66;
	case GPCA_ALGO_HMAC_SHA2_384:
	case GPCA_ALGO_HMAC_SHA2_512:
		return 128;
	case GPCA_ALGO_RSA_2048:
		return 256;
	case GPCA_ALGO_RSA_3072:
		return 384;
	case GPCA_ALGO_RSA_4096:
		return 512;
	case GPCA_ALGO_SHA2_224:
	case GPCA_ALGO_SHA2_256:
	case GPCA_ALGO_SHA2_384:
	case GPCA_ALGO_SHA2_512:
		return 0;
	default:
		break;
	}
	return 0;
}

static struct gpca_key_policy valid_import_key_policies[] = {
	/* AES 128 ECB encryption key */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_ECB,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 128 CBC encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_CBC,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 128 CTR encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_CTR,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 128 GCM encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 128 XTS encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_XTS,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 192 ECB encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES192_ECB,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 192 CBC encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES192_CBC,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 192 CTR encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES192_CTR,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 192 GCM encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES192_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 192 XTS encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES192_XTS,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 256 ECB encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_ECB,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 256 CBC encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_CBC,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 256 CTR encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_CTR,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 256 GCM encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 256 XTS encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_XTS,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 256 CMAC Key derivation key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_CMAC,
	  GPCA_SUPPORTED_PURPOSE_DERIVE, GPCA_KMK_ENABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* CMAC sign/verify key */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_CMAC,
	  GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* AES 256 KWP Key wrapping key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_KWP,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_ENABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* TDES ECB encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_TDES_ECB,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* TDES CBC encryption key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_TDES_CBC,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* HMAC SHA224 key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_HMAC_SHA2_224,
	  GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
	  GPCA_KMK_DISABLE, GPCA_WAEK_ENABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_DISABLE,
	  GPCA_EVICT_AP_S_DISABLE, GPCA_EVICT_AP_NS_DISABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* HMAC SHA256 key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_HMAC_SHA2_256,
	  GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
	  GPCA_KMK_DISABLE, GPCA_WAEK_ENABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_DISABLE,
	  GPCA_EVICT_AP_S_DISABLE, GPCA_EVICT_AP_NS_DISABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* HMAC SHA384 key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_HMAC_SHA2_384,
	  GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
	  GPCA_KMK_DISABLE, GPCA_WAEK_ENABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_DISABLE,
	  GPCA_EVICT_AP_S_DISABLE, GPCA_EVICT_AP_NS_DISABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* HMAC SHA512 key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_HMAC_SHA2_512,
	  GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
	  GPCA_KMK_DISABLE, GPCA_WAEK_ENABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_DISABLE,
	  GPCA_EVICT_AP_S_DISABLE, GPCA_EVICT_AP_NS_DISABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /**
	   * Bug(b/242022464): RSA not supported in GEM5 currently
	   * // RSA 2048 key
	   * { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_RSA_2048,
	   * GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	   * GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	   * GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	   * GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	   * GPCA_AUTH_DOMAIN_ANDROID_VM },
	   * // RSA 3072 key
	   * { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_RSA_3072,
	   * GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	   * GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	   * GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	   * GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	   * GPCA_AUTH_DOMAIN_ANDROID_VM },
	   * // RSA 4096 key
	   * { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_RSA_4096,
	   * GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	   * GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	   * GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	   * GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	   * GPCA_AUTH_DOMAIN_ANDROID_VM },
	   */
	  /* ECC P224 Sign key */
	  { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P224,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* ECC P256 sign key */
	   { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P256,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* ECC P384 sign key */
	   { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P384,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* ECC P521 sign key */
	   { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P521,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	  /* ECC ED25519 sign key */
	   { GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_ED25519,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* ECC P256 Key exchange key */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P256,
	  GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE, GPCA_KMK_ENABLE,
	  GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE,
	  GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* ECC P384 Key exchange key */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P384,
	  GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE, GPCA_KMK_ENABLE,
	  GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE,
	  GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* ECC P521 Key exchange key */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P521,
	  GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE, GPCA_KMK_ENABLE,
	  GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE,
	  GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Key with WAPK = 1 */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_ECC_P521,
	  GPCA_SUPPORTED_PURPOSE_SIGN, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_ENABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
};

static struct gpca_key_policy invalid_import_key_policies[] = {
	/* Ephemeral key */
	{ GPCA_KEY_CLASS_EPHEMERAL, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Hardware key */
	{ GPCA_KEY_CLASS_HARDWARE, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* AES key with purpose = Sign */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT |
		  GPCA_SUPPORTED_PURPOSE_SIGN,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* AES CMAC with purpose = Encrypt */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_CMAC,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* HMAC with purpose=key exchange */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_HMAC_SHA2_384,
	  GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE, GPCA_KMK_DISABLE,
	  GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE,
	  GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* AES256_KWP with purpose=derive */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES256_KWP,
	  GPCA_SUPPORTED_PURPOSE_DERIVE, GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE,
	  GPCA_WAHK_DISABLE, GPCA_WAPK_DISABLE, GPCA_BS_DISABLE,
	  GPCA_EVICT_GSA_ENABLE, GPCA_EVICT_AP_S_ENABLE,
	  GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_ANDROID_VM,
	  GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Invalid key class */
	{ 3, GPCA_ALGO_AES256_GCM,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
#if 0 /* GEM5 panics for invalid crypto param */
	/* Invalid Crypto param */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_SHA2_512 + 1,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT |
		  GPCA_SUPPORTED_PURPOSE_SIGN,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
#endif
	/* Invalid supported purpose */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_CBC, BIT(6),
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_ANDROID_VM, GPCA_AUTH_DOMAIN_ANDROID_VM },
	/* Invalid owner - currently we allow only NS VM1 as owner */
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_CTR,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE,
	  GPCA_OWNER_DOMAIN_GROOT, GPCA_AUTH_DOMAIN_GROOT },
	{ GPCA_KEY_CLASS_PORTABLE, GPCA_ALGO_AES128_CTR,
	  GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
	  GPCA_KMK_DISABLE, GPCA_WAEK_DISABLE, GPCA_WAHK_DISABLE,
	  GPCA_WAPK_DISABLE, GPCA_BS_DISABLE, GPCA_EVICT_GSA_ENABLE,
	  GPCA_EVICT_AP_S_ENABLE, GPCA_EVICT_AP_NS_ENABLE, GPCA_OWNER_DOMAIN_TZ,
	  GPCA_AUTH_DOMAIN_TZ },
};

static void gpca_keys_import(struct kunit *test)
{
	u32 i = 0;
	u8 key_buf[68] = { 0 };
	u32 key_size = 0;
	u8 huge_key_buf[1600] = { 0 };

	struct gpca_key_handle *key_handle;

	for (i = 0; i < ARRAY_SIZE(key_buf); i++)
		key_buf[i] = i;

	/* Valid key policies */
	for (i = 0; i < ARRAY_SIZE(valid_import_key_policies); i++) {
		key_size =
			get_key_size_bytes(valid_import_key_policies[i].algo);
		KUNIT_EXPECT_EQ_MSG(
			test, 0,
			gpca_key_import(gpca_ctx, &key_handle,
					&valid_import_key_policies[i], key_buf,
					key_size),
			"Failed for iteration %d", i);
		KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	}

	/* Error case: Invalid key policies */
	for (i = 0; i < ARRAY_SIZE(invalid_import_key_policies); i++) {
		key_size =
			get_key_size_bytes(invalid_import_key_policies[i].algo);
		KUNIT_EXPECT_NE_MSG(
			test, 0,
			gpca_key_import(gpca_ctx, &key_handle,
					&invalid_import_key_policies[i],
					key_buf, key_size),
			"Failed for iteration %d", i);
	}

	key_size = get_key_size_bytes(valid_import_key_policies[0].algo);
	/* Error case: Wrong key size */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_import(gpca_ctx, &key_handle,
					&valid_import_key_policies[0], key_buf,
					key_size - 4));

	/* Error case: Key buffer NULL or key size = 0 */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_import(gpca_ctx, &key_handle,
					&valid_import_key_policies[0], NULL,
					key_size));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_import(gpca_ctx, &key_handle,
					&valid_import_key_policies[0], key_buf,
					0));
	/* Error case: Key too big */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_import(gpca_ctx, &key_handle,
					&valid_import_key_policies[0],
					huge_key_buf,
					ARRAY_SIZE(huge_key_buf)));
}

static void gpca_keys_get_key_policy(struct kunit *test)
{
	struct gpca_key_policy get_key_policy = { 0 };
	struct gpca_key_policy overflow_kp[2] = { 0 };

	struct gpca_key_policy key_policy = { GPCA_KEY_CLASS_HARDWARE,
					      GPCA_ALGO_AES256_CMAC,
					      GPCA_SUPPORTED_PURPOSE_DERIVE,
					      GPCA_KMK_ENABLE,
					      GPCA_WAEK_ENABLE,
					      GPCA_WAHK_ENABLE,
					      GPCA_WAPK_DISABLE,
					      GPCA_BS_DISABLE,
					      GPCA_EVICT_GSA_ENABLE,
					      GPCA_EVICT_AP_S_ENABLE,
					      GPCA_EVICT_AP_NS_ENABLE,
					      GPCA_OWNER_DOMAIN_ANDROID_VM,
					      GPCA_AUTH_DOMAIN_ANDROID_VM };
	struct gpca_key_policy second_key_policy = {
		GPCA_KEY_CLASS_PORTABLE,
		GPCA_ALGO_ECC_ED25519,
		GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
		GPCA_KMK_DISABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_ENABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_DISABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_DISABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *key_handle;

	/* Generate key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle, &key_policy));
	/* Get Key Policy */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_policy(gpca_ctx, key_handle,
					    &get_key_policy));
	/* Compare key policy against what was set */
	KUNIT_EXPECT_EQ(test, 0,
			memcmp(&key_policy, &get_key_policy,
			       sizeof(struct gpca_key_policy)));
	/* Error case: NULL buffer to get key policy */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_policy(gpca_ctx, key_handle, NULL));
	/* Check that there is no overflow while getting key policy */
	overflow_kp[1] = second_key_policy;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_policy(gpca_ctx, key_handle,
					    &overflow_kp[0]));
	KUNIT_EXPECT_EQ(test, 0,
			memcmp(&key_policy, &overflow_kp[0],
			       sizeof(struct gpca_key_policy)));
	KUNIT_EXPECT_EQ(test, 0,
			memcmp(&second_key_policy, &overflow_kp[1],
			       sizeof(struct gpca_key_policy)));

	/* Clear key*/
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));

	/* Error case: Get Key Policy for empty keyslot */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_policy(gpca_ctx, key_handle,
					    &get_key_policy));
}

static void gpca_keys_set_public_key(struct kunit *test)
{
	u8 pub_key[] = {
		/* ECC 384 x-coord */
		0xc8, 0x3d, 0x30, 0xde, 0x9c, 0x4e, 0x18, 0x16, 0x7c, 0xb4,
		0x1c, 0x99, 0x7, 0x81, 0xb3, 0x4b, 0x9f, 0xce, 0xb5, 0x27, 0x93,
		0xb4, 0x62, 0x7e, 0x69, 0x67, 0x96, 0xc5, 0x80, 0x35, 0x15,
		0xdb, 0xc4, 0xd1, 0x42, 0x97, 0x7d, 0x91, 0x4b, 0xc0, 0x4c,
		0x15, 0x32, 0x61, 0xcc, 0x5b, 0x53, 0x7f,
		/* ECC 384 y-coord */
		0x42, 0x31, 0x8e, 0x5c, 0x15, 0xd6, 0x5c, 0x3f, 0x54, 0x51,
		0x89, 0x78, 0x16, 0x19, 0x26, 0x7d, 0x89, 0x92, 0x50, 0xd8, 0xa,
		0xcc, 0x61, 0x1f, 0xe7, 0xed, 0x9, 0x43, 0xa0, 0xf5, 0xbf, 0xc9,
		0xd4, 0x32, 0x8f, 0xf7, 0xcc, 0xf6, 0x75, 0xae, 0xa, 0xac, 0x6,
		0x9c, 0xcb, 0x4b, 0x4d, 0x6e
	};
	u8 big_pub_key[1500] = { 0 };
	struct gpca_key_handle *key_handle;

	/* Set public key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_set_public_key(gpca_ctx, &key_handle, pub_key,
						ARRAY_SIZE(pub_key)));
	/* TODO(b/242022464): Set RSA public key */
	/* Clear key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	/* Error case: NULL public key */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_set_public_key(gpca_ctx, &key_handle, NULL,
						ARRAY_SIZE(pub_key)));
	/* Error case: Public key size = 0 */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_set_public_key(gpca_ctx, &key_handle, pub_key,
						0));
	/* Error case: Too big public key */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_set_public_key(gpca_ctx, &key_handle,
						big_pub_key,
						ARRAY_SIZE(big_pub_key)));
}

static void gpca_keys_get_public_key(struct kunit *test)
{
	u8 pub_key[] = {
		/* ECC 384 x-coord */
		0xc8, 0x3d, 0x30, 0xde, 0x9c, 0x4e, 0x18, 0x16, 0x7c, 0xb4,
		0x1c, 0x99, 0x7, 0x81, 0xb3, 0x4b, 0x9f, 0xce, 0xb5, 0x27, 0x93,
		0xb4, 0x62, 0x7e, 0x69, 0x67, 0x96, 0xc5, 0x80, 0x35, 0x15,
		0xdb, 0xc4, 0xd1, 0x42, 0x97, 0x7d, 0x91, 0x4b, 0xc0, 0x4c,
		0x15, 0x32, 0x61, 0xcc, 0x5b, 0x53, 0x7f,
		/* ECC 384 y-coord */
		0x42, 0x31, 0x8e, 0x5c, 0x15, 0xd6, 0x5c, 0x3f, 0x54, 0x51,
		0x89, 0x78, 0x16, 0x19, 0x26, 0x7d, 0x89, 0x92, 0x50, 0xd8, 0xa,
		0xcc, 0x61, 0x1f, 0xe7, 0xed, 0x9, 0x43, 0xa0, 0xf5, 0xbf, 0xc9,
		0xd4, 0x32, 0x8f, 0xf7, 0xcc, 0xf6, 0x75, 0xae, 0xa, 0xac, 0x6,
		0x9c, 0xcb, 0x4b, 0x4d, 0x6e
	};
	u8 out_pub_key[1280] = { 0 };
	u32 pub_key_size = 0;
	u8 huge_pub_key[2000] = { 0 };
	u32 i = 0;
	struct gpca_key_policy key_derivation_key = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_AES256_CMAC,
		GPCA_SUPPORTED_PURPOSE_DERIVE,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_ENABLE,
		GPCA_EVICT_AP_S_ENABLE,
		GPCA_EVICT_AP_NS_ENABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};
	struct gpca_key_handle *key_handle;

	/* Set Public Key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_set_public_key(gpca_ctx, &key_handle, pub_key,
						ARRAY_SIZE(pub_key)));
	/* Get Public key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_public_key(
				gpca_ctx, key_handle, out_pub_key,
				ARRAY_SIZE(out_pub_key), &pub_key_size));
	/* Since the key is stored in large key slot expected size = 10240b = 1280B */
	KUNIT_EXPECT_EQ(test, ARRAY_SIZE(out_pub_key), pub_key_size);
	for (i = 0; i < ARRAY_SIZE(pub_key); i++)
		KUNIT_EXPECT_EQ(test, pub_key[i], out_pub_key[i]);

	/* Check that there is no overflow */
	memset(huge_pub_key, 0xFE, ARRAY_SIZE(huge_pub_key));
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_public_key(
				gpca_ctx, key_handle, huge_pub_key,
				ARRAY_SIZE(huge_pub_key), &pub_key_size));
	KUNIT_EXPECT_EQ(test, ARRAY_SIZE(out_pub_key), pub_key_size);

	for (i = 0; i < ARRAY_SIZE(pub_key); i++)
		KUNIT_EXPECT_EQ(test, pub_key[i], huge_pub_key[i]);

	for (i = ARRAY_SIZE(out_pub_key); i < ARRAY_SIZE(huge_pub_key); i++)
		KUNIT_EXPECT_EQ_MSG(test, 0xFE, huge_pub_key[i],
				    "Public key buffer corrupted at index = %d",
				    i);

	/* Error case: NULL public key buffer */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_public_key(gpca_ctx, key_handle, NULL,
						ARRAY_SIZE(out_pub_key),
						&pub_key_size));
	/* Error case: Public key buffer size = 0 */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_public_key(gpca_ctx, key_handle,
						out_pub_key, 0, &pub_key_size));
	/* Error case: Public key buffer size insufficient */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_public_key(gpca_ctx, key_handle,
						out_pub_key, 16,
						&pub_key_size));
	/* Error case: Public key buf size out NULL */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_public_key(gpca_ctx, key_handle,
						out_pub_key,
						ARRAY_SIZE(out_pub_key), NULL));
	/* Clear public key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
	/* Error case: Empty keyslot */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_public_key(
				gpca_ctx, key_handle, out_pub_key,
				ARRAY_SIZE(out_pub_key), &pub_key_size));

	/* Error case: Non ECC/RSA key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(gpca_ctx, &key_handle,
					  &key_derivation_key));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_public_key(
				gpca_ctx, key_handle, out_pub_key,
				ARRAY_SIZE(out_pub_key), &pub_key_size));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &key_handle));
}

/* Bug(b/245510075): Not implemented on GEM5 */
#if 0
static void gpca_keys_set_key_metadata(struct kunit *test)
{
	u8 keyslot = 1;
	u8 invalid_keyslot = 39;
	struct gpca_key_policy key_derivation_key = { GPCA_KEY_CLASS_EPHEMERAL,
						 GPCA_ALGO_AES256_CMAC,
						 GPCA_SUPPORTED_PURPOSE_DERIVE,
						 GPCA_KMK_ENABLE,
						 GPCA_WAEK_DISABLE,
						 GPCA_WAHK_DISABLE,
						 GPCA_WAPK_DISABLE,
						 GPCA_BS_DISABLE,
						 GPCA_EVICT_GSA_ENABLE,
						 GPCA_EVICT_AP_S_ENABLE,
						 GPCA_EVICT_AP_NS_ENABLE,
						 GPCA_OWNER_DOMAIN_ANDROID_VM,
						 GPCA_AUTH_DOMAIN_ANDROID_VM };
	/**
	 * What are the Valid values for metadata?
	 * Also set key metadata is not currently supported on GEM5
	 */
	struct key_metadata metadata = { true, 0, 0, 0x1234 };

	/* Generate key in keyslot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(keyslot, &key_derivation_key));

	/* Set Key metadata */

	KUNIT_EXPECT_EQ(test, 0, gpca_key_set_metadata(keyslot, &metadata));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(keyslot));
	/* Set key metadata for invalid or empty keyslot */
	KUNIT_EXPECT_NE(test, 0, gpca_key_set_metadata(keyslot, &metadata));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_set_metadata(invalid_keyslot, &metadata));
}

static void gpca_keys_get_key_metadata(struct kunit *test)
{
	u8 keyslot = 1;
	u8 invalid_keyslot = 39;
	struct key_metadata metadata = { true, 0, 0, 0x1234 };
	struct key_metadata get_metadata = { 0 };
	struct key_metadata overflow_metadata[2] = { { 0 } };
	struct key_metadata second_metadata = { false, 0, 0, 0x5678 };

	struct gpca_key_policy key_derivation_key = { GPCA_KEY_CLASS_EPHEMERAL,
						 GPCA_ALGO_AES256_CMAC,
						 GPCA_SUPPORTED_PURPOSE_DERIVE,
						 GPCA_KMK_ENABLE,
						 GPCA_WAEK_DISABLE,
						 GPCA_WAHK_DISABLE,
						 GPCA_WAPK_DISABLE,
						 GPCA_BS_DISABLE,
						 GPCA_EVICT_GSA_ENABLE,
						 GPCA_EVICT_AP_S_ENABLE,
						 GPCA_EVICT_AP_NS_ENABLE,
						 GPCA_OWNER_DOMAIN_ANDROID_VM,
						 GPCA_AUTH_DOMAIN_ANDROID_VM };

	/* Generate key in keyslot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_generate(keyslot, &key_derivation_key));

	/* Set key Metadata */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_set_metadata(keyslot, &metadata));
	/* Get key metadata */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_get_metadata(keyslot, &get_metadata));
	/* Compare against the value in set metadata */
	/* Key metadata must be 96b = 12B in size */
	KUNIT_EXPECT_EQ(test, 0,
			memcmp(&metadata, &get_metadata,
			       sizeof(struct key_metadata)));
	/* Check that there is no overflow */
	overflow_metadata[1] = second_metadata;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_get_metadata(keyslot, &overflow_metadata[0]));
	KUNIT_EXPECT_EQ(test, 0,
			memcmp(&metadata, &overflow_metadata[0],
			       sizeof(struct key_metadata)));
	KUNIT_EXPECT_EQ(test, 0,
			memcmp(&second_metadata, &overflow_metadata[1],
			       sizeof(struct key_metadata)));

	/* Error case: NULL key metadata buffer */
	KUNIT_EXPECT_NE(test, 0, gpca_key_get_metadata(keyslot, NULL));
	/* Clear key */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(keyslot));
	/* Error case: Invalid or empty keyslot */
	KUNIT_EXPECT_NE(test, 0, gpca_key_get_metadata(keyslot, &metadata));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_get_metadata(invalid_keyslot, &metadata));
}
#endif

struct gpca_cb_ctx {
	int ret;
	struct completion cpl;
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

static int gpca_crypto_set_symm_algo_params_blocking(struct gpca_dev *ctx,
						     struct gpca_crypto_op_handle *crypto_op_handle,
						     struct gpca_key_handle *key_handle,
						     enum gpca_algorithm algo,
						     enum gpca_supported_purpose purpose,
						     const u8 *iv_buf, u32 iv_size)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_set_symm_algo_params(ctx, crypto_op_handle, key_handle,
					 algo, purpose, iv_buf, iv_size,
					 gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

static int gpca_crypto_start_symmetric_op_blocking(struct gpca_dev *ctx,
						   struct gpca_crypto_op_handle *crypto_op_handle,
						   dma_addr_t data_addr, u32 input_data_size,
						   u32 aad_size, u32 unencrypted_size,
						   dma_addr_t output_data_addr,
						   u32 output_data_size, bool is_last)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_start_symmetric_op(ctx, crypto_op_handle, data_addr, input_data_size,
				       aad_size, unencrypted_size, output_data_addr,
				       output_data_size, is_last, gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

static int gpca_crypto_clear_op_blocking(struct gpca_dev *ctx,
					 struct gpca_crypto_op_handle *crypto_op_handle)
{
	struct gpca_cb_ctx gpca_cb_ctx = { 0 };

	init_completion(&gpca_cb_ctx.cpl);
	gpca_crypto_clear_op(ctx, crypto_op_handle, gpca_cb, &gpca_cb_ctx);
	wait_for_completion(&gpca_cb_ctx.cpl);

	return gpca_cb_ctx.ret;
}

#define ENCRYPT_OP GPCA_SUPPORTED_PURPOSE_ENCRYPT

static void gpca_keys_secure_key_import(struct kunit *test)
{
	u8 salt_buf[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
	u8 context_buf[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
			     0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
			     0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
			     0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f };
	u8 large_context_buf[64] = { 0 };
	u8 large_salt_buf[32] = { 0 };
	u8 large_wrapped_key_buf[1500] = { 0 };

	u8 expected_key[] = { 0x32, 0xfd, 0xed, 0xa3, 0x9f, 0x98, 0xb4, 0xf4,
			      0x42, 0x6c, 0x2d, 0x2a, 0xc0, 0x0a, 0xb5, 0xdd,
			      0x4b, 0xfa, 0xbb, 0x68, 0xf3, 0x11, 0x44, 0x72,
			      0x56, 0xed, 0x6d, 0x3d, 0x3a, 0x51, 0xb1, 0x54

	};
	u8 plaintext[] = { 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
			   0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };
	u8 expected_ciphertext[16] = { 0 };

	struct gpca_key_handle *pub_key_handle = NULL;
	struct gpca_key_handle *priv_key_handle = NULL;
	struct gpca_key_handle *ski_handle = NULL;

	struct gpca_key_policy priv_kp = { GPCA_KEY_CLASS_PORTABLE,
					   GPCA_ALGO_ECC_P256,
					   GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE,
					   GPCA_KMK_ENABLE,
					   GPCA_WAEK_ENABLE,
					   GPCA_WAHK_DISABLE,
					   GPCA_WAPK_DISABLE,
					   GPCA_BS_DISABLE,
					   GPCA_EVICT_GSA_DISABLE,
					   GPCA_EVICT_AP_S_DISABLE,
					   GPCA_EVICT_AP_NS_DISABLE,
					   GPCA_OWNER_DOMAIN_ANDROID_VM,
					   GPCA_AUTH_DOMAIN_ANDROID_VM };
	struct gpca_key_policy secure_import_kp = {
		GPCA_KEY_CLASS_PORTABLE,
		GPCA_ALGO_AES256_ECB,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT,
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

	struct gpca_key_policy wrapping_key_policy = {
		GPCA_KEY_CLASS_PORTABLE,
		GPCA_ALGO_AES256_KWP,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_DISABLE,
		GPCA_EVICT_AP_S_DISABLE,
		GPCA_EVICT_AP_NS_DISABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};

	struct gpca_key_handle *expected_key_handle = NULL;
	u8 *msg_buf = NULL, *ct_buf = NULL;
	dma_addr_t pt_dma_addr, ct_dma_addr;
	struct gpca_crypto_op_handle *crypto_op_handle = NULL;
	u32 i = 0;
	u32 test_idx = 0;

	crypto_op_handle = gpca_crypto_op_handle_alloc();
	KUNIT_EXPECT_PTR_NE(test, NULL, crypto_op_handle);

	msg_buf = kmalloc(ARRAY_SIZE(plaintext), GFP_KERNEL);
	ct_buf = kmalloc(ARRAY_SIZE(plaintext), GFP_KERNEL);
	/* Do AES ECB encrypt of plaintext with expected key, store expected CT */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_import(gpca_ctx, &expected_key_handle,
					&secure_import_kp, expected_key,
					ARRAY_SIZE(expected_key)));

	for (i = 0; i < ARRAY_SIZE(plaintext); i++)
		msg_buf[i] = plaintext[i];

	KUNIT_EXPECT_EQ_MSG(test, 0,
			    gpca_crypto_set_symm_algo_params_blocking(gpca_ctx,
								      crypto_op_handle,
								      expected_key_handle,
								      GPCA_ALGO_AES256_ECB,
								      ENCRYPT_OP,
								      NULL, 0),
			    "Set crypto params encrypt failed");
	/* DMA Map */
	pt_dma_addr = dma_map_single(gpca_ctx->dev, msg_buf,
				     ARRAY_SIZE(plaintext), DMA_TO_DEVICE);
	ct_dma_addr = dma_map_single(gpca_ctx->dev, ct_buf,
				     ARRAY_SIZE(plaintext), DMA_FROM_DEVICE);
	KUNIT_EXPECT_EQ_MSG(test, 0,
			    gpca_crypto_start_symmetric_op_blocking(gpca_ctx,
								    crypto_op_handle,
								    pt_dma_addr,
								    ARRAY_SIZE(plaintext),
								    0, 0, ct_dma_addr,
								    ARRAY_SIZE(plaintext),
								    true),
			    "Start crypto op(ptrs) encrypt failed");
	dma_unmap_single(gpca_ctx->dev, pt_dma_addr, ARRAY_SIZE(plaintext),
			 DMA_TO_DEVICE);
	dma_unmap_single(gpca_ctx->dev, ct_dma_addr, ARRAY_SIZE(plaintext),
			 DMA_FROM_DEVICE);
	/* Clear op slot */
	KUNIT_EXPECT_EQ_MSG(test, 0, gpca_crypto_clear_op_blocking(gpca_ctx, crypto_op_handle),
			    "Clear op slot failed");
	memcpy(expected_ciphertext, ct_buf, ARRAY_SIZE(plaintext));

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_clear(gpca_ctx, &expected_key_handle));

	for (test_idx = 0; test_idx < ARRAY_SIZE(gpca_ski_tests); test_idx++) {
		KUNIT_EXPECT_EQ(test, 0,
				gpca_key_set_public_key(gpca_ctx, &pub_key_handle,
							gpca_ski_tests[test_idx].server_public_key,
							gpca_ski_tests[test_idx].pub_key_size));
		priv_kp.algo = gpca_ski_tests[test_idx].ecc_curve;
		KUNIT_EXPECT_EQ(test, 0,
				gpca_key_import(gpca_ctx, &priv_key_handle, &priv_kp,
						gpca_ski_tests[test_idx].device_private_key,
						gpca_ski_tests[test_idx].priv_key_size));

		KUNIT_EXPECT_EQ(test, 0,
				gpca_key_secure_import(gpca_ctx,
						       priv_key_handle, pub_key_handle,
						       &ski_handle,
						       gpca_ski_tests[test_idx].include_key_policy,
						       gpca_ski_tests[test_idx].include_key_policy ?
							&wrapping_key_policy :
							&secure_import_kp, context_buf,
						       sizeof(context_buf),
						       gpca_ski_tests[test_idx].salt_present ?
							salt_buf : NULL,
						       gpca_ski_tests[test_idx].salt_present ?
							sizeof(salt_buf) : 0,
						       gpca_ski_tests[test_idx].ski_blob,
						       gpca_ski_tests[test_idx].ski_blob_size));

		/* Do AES ECB encrypt with key and compare with expected CT */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_set_symm_algo_params_blocking(gpca_ctx,
									      crypto_op_handle,
									      ski_handle,
									      GPCA_ALGO_AES256_ECB,
									      ENCRYPT_OP,
									      NULL, 0),
				    "Set crypto params encrypt failed");
		/* DMA Map */
		pt_dma_addr = dma_map_single(gpca_ctx->dev, msg_buf,
					     ARRAY_SIZE(plaintext), DMA_TO_DEVICE);
		ct_dma_addr = dma_map_single(gpca_ctx->dev, ct_buf,
					     ARRAY_SIZE(plaintext), DMA_FROM_DEVICE);
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_start_symmetric_op_blocking(gpca_ctx,
									    crypto_op_handle,
									    pt_dma_addr,
									    ARRAY_SIZE(plaintext),
									    0, 0,  ct_dma_addr,
									    ARRAY_SIZE(plaintext),
									    true),
				    "Start crypto op(ptrs) encrypt failed");
		dma_unmap_single(gpca_ctx->dev, pt_dma_addr, ARRAY_SIZE(plaintext),
				 DMA_TO_DEVICE);
		dma_unmap_single(gpca_ctx->dev, ct_dma_addr, ARRAY_SIZE(plaintext),
				 DMA_FROM_DEVICE);
		/* Clear op slot */
		KUNIT_EXPECT_EQ_MSG(test, 0,
				    gpca_crypto_clear_op_blocking(gpca_ctx, crypto_op_handle),
				    "Clear op slot failed");
		KUNIT_EXPECT_EQ(test, 0,
				memcmp(expected_ciphertext, ct_buf, ARRAY_SIZE(plaintext)));

		KUNIT_EXPECT_EQ(test, 0,
				gpca_key_clear(gpca_ctx, &ski_handle));

		KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &priv_key_handle));
		KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &pub_key_handle));
	}

	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_set_public_key(gpca_ctx, &pub_key_handle,
						gpca_ski_tests[0].server_public_key,
						gpca_ski_tests[0].pub_key_size));
	priv_kp.algo = gpca_ski_tests[0].ecc_curve;
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_import(gpca_ctx, &priv_key_handle, &priv_kp,
					gpca_ski_tests[0].device_private_key,
					gpca_ski_tests[0].priv_key_size));
	/* Error case: Invalid context buffer */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, NULL,
					       sizeof(context_buf),
					       salt_buf, sizeof(salt_buf),
					       gpca_ski_tests[0].ski_blob,
					       gpca_ski_tests[0].ski_blob_size));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, context_buf,
					       0, salt_buf,
					       sizeof(salt_buf),
					       gpca_ski_tests[0].ski_blob,
					       gpca_ski_tests[0].ski_blob_size));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, large_context_buf,
					       sizeof(large_context_buf), salt_buf,
					       sizeof(salt_buf),
					       gpca_ski_tests[0].ski_blob,
					       gpca_ski_tests[0].ski_blob_size));
	/* Error case: Invalid Salt */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, context_buf,
					       sizeof(context_buf), NULL,
					       sizeof(salt_buf),
					       gpca_ski_tests[0].ski_blob,
					       gpca_ski_tests[0].ski_blob_size));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, context_buf,
					       sizeof(context_buf), large_salt_buf,
					       sizeof(large_salt_buf),
					       gpca_ski_tests[0].ski_blob,
					       gpca_ski_tests[0].ski_blob_size));
	/* Error case: Invalid wrapped key blob */
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, context_buf,
					       sizeof(context_buf), salt_buf,
					       sizeof(salt_buf), NULL,
					       gpca_ski_tests[0].ski_blob_size));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, context_buf,
					       sizeof(context_buf), salt_buf,
					       sizeof(salt_buf),
					       gpca_ski_tests[0].ski_blob, 0));
	KUNIT_EXPECT_NE(test, 0,
			gpca_key_secure_import(gpca_ctx,
					       priv_key_handle, pub_key_handle,
					       &ski_handle, false,
					       &secure_import_kp, context_buf,
					       sizeof(context_buf), salt_buf,
					       sizeof(salt_buf),
					       large_wrapped_key_buf,
					       sizeof(large_wrapped_key_buf)));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &priv_key_handle));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &pub_key_handle));

	gpca_crypto_op_handle_free(crypto_op_handle);
}

static void gpca_keys_slot_management(struct kunit *test)
{
	int i = 0;
	struct gpca_key_policy swappable_hmac_key = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_HMAC_SHA2_224,
		GPCA_SUPPORTED_PURPOSE_SIGN | GPCA_SUPPORTED_PURPOSE_VERIFY,
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
	struct gpca_key_policy swappable_key_derivation_key = {
		GPCA_KEY_CLASS_HARDWARE,
		GPCA_ALGO_AES256_CMAC,
		GPCA_SUPPORTED_PURPOSE_DERIVE,
		GPCA_KMK_ENABLE,
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
	/* WAEK=0 hence unswappable */
	struct gpca_key_policy unswappable_wrapping_key = {
		GPCA_KEY_CLASS_EPHEMERAL,
		GPCA_ALGO_AES256_KWP,
		GPCA_SUPPORTED_PURPOSE_ENCRYPT | GPCA_SUPPORTED_PURPOSE_DECRYPT,
		GPCA_KMK_ENABLE,
		GPCA_WAEK_DISABLE,
		GPCA_WAHK_DISABLE,
		GPCA_WAPK_DISABLE,
		GPCA_BS_DISABLE,
		GPCA_EVICT_GSA_DISABLE,
		GPCA_EVICT_AP_S_DISABLE,
		GPCA_EVICT_AP_NS_DISABLE,
		GPCA_OWNER_DOMAIN_ANDROID_VM,
		GPCA_AUTH_DOMAIN_ANDROID_VM
	};

	struct gpca_key_handle *handles[10];
	struct gpca_key_handle *hmac_key_handle;
	u8 ctx_buf[4] = { 1, 2, 3, 4 };
	u8 pub_key[] = {
		/* ECC 384 x-coord */
		0xc8, 0x3d, 0x30, 0xde, 0x9c, 0x4e, 0x18, 0x16, 0x7c, 0xb4,
		0x1c, 0x99, 0x7, 0x81, 0xb3, 0x4b, 0x9f, 0xce, 0xb5, 0x27, 0x93,
		0xb4, 0x62, 0x7e, 0x69, 0x67, 0x96, 0xc5, 0x80, 0x35, 0x15,
		0xdb, 0xc4, 0xd1, 0x42, 0x97, 0x7d, 0x91, 0x4b, 0xc0, 0x4c,
		0x15, 0x32, 0x61, 0xcc, 0x5b, 0x53, 0x7f,
		/* ECC 384 y-coord */
		0x42, 0x31, 0x8e, 0x5c, 0x15, 0xd6, 0x5c, 0x3f, 0x54, 0x51,
		0x89, 0x78, 0x16, 0x19, 0x26, 0x7d, 0x89, 0x92, 0x50, 0xd8, 0xa,
		0xcc, 0x61, 0x1f, 0xe7, 0xed, 0x9, 0x43, 0xa0, 0xf5, 0xbf, 0xc9,
		0xd4, 0x32, 0x8f, 0xf7, 0xcc, 0xf6, 0x75, 0xae, 0xa, 0xac, 0x6,
		0x9c, 0xcb, 0x4b, 0x4d, 0x6e
	};

	/**
	 * There are 5 dynamic keyslots available for domain
	 * Two are reserved for wrappable keys at all times.
	 */

	/* Max 5 unswappable keys can be added to key table */
	for (i = 0; i < 3; i++)
		KUNIT_EXPECT_EQ(test, 0,
				gpca_key_generate(gpca_ctx, &handles[i],
						  &unswappable_wrapping_key));

	KUNIT_EXPECT_NE(test, 0,
			gpca_key_generate(gpca_ctx, &handles[5],
					  &unswappable_wrapping_key));

	/* Clear all unswappable keys */
	for (i = 0; i < 3; i++)
		KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &handles[i]));

	/* Add more than 5 swappable keys to table */
	for (i = 0; i < 10; i++)
		KUNIT_EXPECT_EQ(
			test, 0,
			gpca_key_generate(gpca_ctx, &handles[i],
					  &swappable_key_derivation_key));

	/* Use a Least recently used key to test swap_in of key */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_derive(gpca_ctx, handles[0], &hmac_key_handle,
					&swappable_hmac_key, ctx_buf,
					sizeof(ctx_buf)));

	/* Clear all keys */
	for (i = 0; i < 10; i++)
		KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &handles[i]));
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &hmac_key_handle));

	/* Add a public key to large keyslot */
	KUNIT_EXPECT_EQ(test, 0,
			gpca_key_set_public_key(gpca_ctx, &handles[0], pub_key,
						sizeof(pub_key)));
	/**
	 * TODO: b/290896931 - Import swappable & unswappable RSA key into
	 * large key slot
	 */

	/* Clear all public keys */
	KUNIT_EXPECT_EQ(test, 0, gpca_key_clear(gpca_ctx, &handles[0]));
}

static struct kunit_case gpca_keys_test_cases[] = {
	KUNIT_CASE(gpca_keys_storage_encryption),
	KUNIT_CASE(gpca_keys_generate),
	KUNIT_CASE(gpca_keys_derive),
	KUNIT_CASE(gpca_keys_wrap),
	KUNIT_CASE(gpca_keys_unwrap),
	KUNIT_CASE(gpca_keys_clear),
	KUNIT_CASE(gpca_keys_send),
	KUNIT_CASE(gpca_keys_get_software_seed),
	KUNIT_CASE(gpca_keys_import),
	KUNIT_CASE(gpca_keys_get_key_policy),
	KUNIT_CASE(gpca_keys_set_public_key),
	KUNIT_CASE(gpca_keys_get_public_key),
	/**
	 * Bug(b/245510075): Not implemented on GEM5
	 * KUNIT_CASE(gpca_keys_set_key_metadata),
	 * KUNIT_CASE(gpca_keys_get_key_metadata),
	 */
	KUNIT_CASE(gpca_keys_slot_management),
	KUNIT_CASE(gpca_keys_secure_key_import),
	{}
};

int gpca_suite_init(struct kunit_suite *suite)
{
	struct platform_device *gpca_platform_device =
		get_gpca_platform_device();
	gpca_ctx = gpca_key_get_device_context(gpca_platform_device);
	if (!gpca_ctx)
		return -1;
	return 0;
}

static struct kunit_suite gpca_keys_test_suite = {
	.name = "gpca_keys",
	.test_cases = gpca_keys_test_cases,
	.suite_init = gpca_suite_init,
};

kunit_test_suite(gpca_keys_test_suite);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google GPCA Key management KUnit test");
MODULE_LICENSE("GPL");
