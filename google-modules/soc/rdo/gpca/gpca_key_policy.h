/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Key Policy driver for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022 Google LLC.
 */

#ifndef _GOOGLE_GPCA_KEY_POLICY_H
#define _GOOGLE_GPCA_KEY_POLICY_H

#include <linux/bits.h>
#include <linux/errno.h>
#include <linux/types.h>

#define GPCA_KMK_ENABLE 1
#define GPCA_KMK_DISABLE 0

#define GPCA_WAEK_ENABLE 1
#define GPCA_WAEK_DISABLE 0

#define GPCA_WAHK_ENABLE 1
#define GPCA_WAHK_DISABLE 0

#define GPCA_WAPK_ENABLE 1
#define GPCA_WAPK_DISABLE 0

#define GPCA_BS_ENABLE 1
#define GPCA_BS_DISABLE 0

#define GPCA_EVICT_GSA_ENABLE 1
#define GPCA_EVICT_GSA_DISABLE 0

#define GPCA_EVICT_AP_S_ENABLE 1
#define GPCA_EVICT_AP_S_DISABLE 0

#define GPCA_EVICT_AP_NS_ENABLE 1
#define GPCA_EVICT_AP_NS_DISABLE 0

enum gpca_key_class {
	GPCA_KEY_CLASS_EPHEMERAL = 0,
	GPCA_KEY_CLASS_HARDWARE,
	GPCA_KEY_CLASS_PORTABLE
};

enum gpca_algorithm {
	GPCA_ALGO_AES128_ECB = 0,
	GPCA_ALGO_AES128_CBC,
	GPCA_ALGO_AES128_CTR,
	GPCA_ALGO_AES128_GCM,
	GPCA_ALGO_AES128_XTS,
	GPCA_ALGO_AES192_ECB,
	GPCA_ALGO_AES192_CBC,
	GPCA_ALGO_AES192_CTR,
	GPCA_ALGO_AES192_GCM,
	GPCA_ALGO_AES192_XTS,
	GPCA_ALGO_AES256_ECB,
	GPCA_ALGO_AES256_CBC,
	GPCA_ALGO_AES256_CTR,
	GPCA_ALGO_AES256_GCM,
	GPCA_ALGO_AES256_XTS,
	GPCA_ALGO_AES256_CMAC,
	GPCA_ALGO_AES256_KWP,
	GPCA_ALGO_TDES_ECB,
	GPCA_ALGO_TDES_CBC,
	GPCA_ALGO_HMAC_SHA2_224,
	GPCA_ALGO_HMAC_SHA2_256,
	GPCA_ALGO_HMAC_SHA2_384,
	GPCA_ALGO_HMAC_SHA2_512,
	GPCA_ALGO_HKDF_SHA2_256,
	GPCA_ALGO_HKDF_SHA2_512,
	GPCA_ALGO_RSA_2048,
	GPCA_ALGO_RSA_3072,
	GPCA_ALGO_RSA_4096,
	GPCA_ALGO_ECC_P224,
	GPCA_ALGO_ECC_P256,
	GPCA_ALGO_ECC_P384,
	GPCA_ALGO_ECC_P521,
	GPCA_ALGO_ECC_ED25519,
	GPCA_ALGO_SHA2_224,
	GPCA_ALGO_SHA2_256,
	GPCA_ALGO_SHA2_384,
	GPCA_ALGO_SHA2_512
};

enum gpca_supported_purpose {
	GPCA_SUPPORTED_PURPOSE_ENCRYPT = BIT(0),
	GPCA_SUPPORTED_PURPOSE_DECRYPT = BIT(1),
	GPCA_SUPPORTED_PURPOSE_SIGN = BIT(2),
	GPCA_SUPPORTED_PURPOSE_VERIFY = BIT(3),
	GPCA_SUPPORTED_PURPOSE_KEY_EXCHANGE = BIT(4),
	GPCA_SUPPORTED_PURPOSE_DERIVE = BIT(5)
};

enum gpca_owner_domain {
	GPCA_OWNER_DOMAIN_GROOT = 0,
	GPCA_OWNER_DOMAIN_GSA,
	GPCA_OWNER_DOMAIN_TZ,
	GPCA_OWNER_DOMAIN_ANDROID_VM,
	GPCA_OWNER_DOMAIN_NS_VM1,
	GPCA_OWNER_DOMAIN_NS_VM2,
};

enum gpca_authorized_domain {
	GPCA_AUTH_DOMAIN_GROOT = BIT(0),
	GPCA_AUTH_DOMAIN_GSA = BIT(1),
	GPCA_AUTH_DOMAIN_TZ = BIT(2),
	GPCA_AUTH_DOMAIN_ANDROID_VM = BIT(3),
	GPCA_AUTH_DOMAIN_NS_VM1 = BIT(4),
	GPCA_AUTH_DOMAIN_NS_VM2 = BIT(5),
};

/**
 * struct gpca_key_policy - Key Policy.
 * @key_class: Key class (Ephemeral, Hardware, Portable)
 * @algo: GPCA Algorithm for which the key is used.
 * @purposes: Purposes supported by key where each purpose
 *            is a gpca_supported_purpose.
 * @key_management_key: Key Management Key (Key used for wrapping/derivation)
 * @wrappable_by_ephemeral_key: Wrap Allowed with Ephemeral Key
 * @wrappable_by_hardware_key: Wrap Allowed with Hardware Key
 * @wrappable_by_portable_key: Wrap Allowed with Portable Key
 * @boot_state_bound: Boot State Binding
 * @evict_gsa: Evict key when GSA security state changes
 * @evict_ap_s: Evict key when  AP secure (TZ) security state
 *              changes in the SOC.
 * @evict_ap_ns: Evict key when  AP non-secure security state
 *               changes in the SOC.
 * @owner: Domain which owns the key.
 * @auth_domains: Domains authorized to use the key where each
 *                domain is a gpca_authorized_domain.
 *
 * This struct has the fields of Key Policy.
 */
struct gpca_key_policy {
	enum gpca_key_class key_class;
	enum gpca_algorithm algo;
	u32 purposes;
	bool key_management_key;
	bool wrappable_by_ephemeral_key;
	bool wrappable_by_hardware_key;
	bool wrappable_by_portable_key;
	bool boot_state_bound;
	bool evict_gsa;
	bool evict_ap_s;
	bool evict_ap_ns;
	enum gpca_owner_domain owner;
	u32 auth_domains;
};

/**
 * Get raw GPCA key policy.
 * @kp: Pointer to Key Policy object of type struct gpca_key_policy
 * @raw_kp: Pointer to Key policy in raw 64 bit format as used by GPCA.
 *
 * Return : 0 on success, negative on error.
 */
int gpca_key_policy_to_raw(const struct gpca_key_policy *kp, u64 *raw_kp);

/**
 * Get GPCA key policy from raw 64 bit key policy.
 * @raw_kp: Key policy in raw 64 bit format as used by GPCA.
 * @kp: Pointer to Key Policy object of type struct gpca_key_policy
 *      where the parsed key policy is to be stored.
 *
 * Return : 0 on success, negative on error.
 */
int gpca_raw_to_key_policy(u64 raw_kp, struct gpca_key_policy *kp);

#endif /* _GOOGLE_GPCA_KEY_POLICY_H */
