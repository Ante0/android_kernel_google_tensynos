// SPDX-License-Identifier: GPL-2.0-only
/*
 * Key Policy driver for the GPCA(General Purpose Crypto Accelerator)
 *
 * Copyright (C) 2022 Google LLC.
 */

#include "gpca_key_policy.h"

#include <linux/bitfield.h>

#define KEY_POLICY_KEY_CLASS_FIELD GENMASK_ULL(2, 0)
#define KEY_POLICY_CRYPTO_PARAMS_FIELD GENMASK_ULL(10, 3)
#define KEY_POLICY_SUPPORTED_PURPOSE_FIELD GENMASK_ULL(18, 11)
#define KEY_POLICY_KMK_FIELD GENMASK_ULL(19, 19)
#define KEY_POLICY_WAEK_FIELD GENMASK_ULL(20, 20)
#define KEY_POLICY_WAHK_FIELD GENMASK_ULL(21, 21)
#define KEY_POLICY_WAPK_FIELD GENMASK_ULL(22, 22)
#define KEY_POLICY_BS_FIELD GENMASK_ULL(23, 23)
#define KEY_POLICY_EVICT_GSA_FIELD GENMASK_ULL(24, 24)
#define KEY_POLICY_EVICT_AP_S_FIELD GENMASK_ULL(25, 25)
#define KEY_POLICY_EVICT_AP_NS_FIELD GENMASK_ULL(26, 26)
#define KEY_POLICY_OWNER_FIELD GENMASK_ULL(31, 28)
#define KEY_POLICY_AUTH_DOMAINS_FIELD GENMASK_ULL(37, 32)

/**
 * +--------------------------------------------------------------------+-----------------+
 * | RESERVED (6:31)                                                    |AUTH_DOMAINS(0:5)|
 * +-----------+-+-------+------+-----+--+----+----+----+---+---------+--------+----------+
 * |OWNR(28:31)|R|E_AP_NS|E_AP_S|E_GSA|BS|WAPK|WAHK|WAEK|KMK|SP(11:18)|CP(3:10)|CLASS(0:2)|
 * +-----------+-+-------+------+-----+--+----+----+----+---+---------+--------+----------+
 * SP : Supported Purposes
 * CP : Crypto Param
 * E_* : Evict bits
 */
int gpca_key_policy_to_raw(const struct gpca_key_policy *kp, u64 *raw_kp)
{
	if (!kp || !raw_kp)
		return -EINVAL;

	*raw_kp =
		(FIELD_PREP(KEY_POLICY_KEY_CLASS_FIELD, kp->key_class) |
		 FIELD_PREP(KEY_POLICY_CRYPTO_PARAMS_FIELD, kp->algo) |
		 FIELD_PREP(KEY_POLICY_SUPPORTED_PURPOSE_FIELD, kp->purposes) |
		 FIELD_PREP(KEY_POLICY_KMK_FIELD, kp->key_management_key) |
		 FIELD_PREP(KEY_POLICY_WAEK_FIELD,
			    kp->wrappable_by_ephemeral_key) |
		 FIELD_PREP(KEY_POLICY_WAHK_FIELD,
			    kp->wrappable_by_hardware_key) |
		 FIELD_PREP(KEY_POLICY_WAPK_FIELD,
			    kp->wrappable_by_portable_key) |
		 FIELD_PREP(KEY_POLICY_BS_FIELD, kp->boot_state_bound) |
		 FIELD_PREP(KEY_POLICY_EVICT_GSA_FIELD, kp->evict_gsa) |
		 FIELD_PREP(KEY_POLICY_EVICT_AP_S_FIELD, kp->evict_ap_s) |
		 FIELD_PREP(KEY_POLICY_EVICT_AP_NS_FIELD, kp->evict_ap_ns) |
		 FIELD_PREP(KEY_POLICY_OWNER_FIELD, kp->owner) |
		 FIELD_PREP(KEY_POLICY_AUTH_DOMAINS_FIELD, kp->auth_domains));

	return 0;
}

int gpca_raw_to_key_policy(u64 raw_kp, struct gpca_key_policy *kp)
{
	if (!kp)
		return -EINVAL;
	kp->key_class = FIELD_GET(KEY_POLICY_KEY_CLASS_FIELD, raw_kp);
	kp->algo = FIELD_GET(KEY_POLICY_CRYPTO_PARAMS_FIELD, raw_kp);
	kp->purposes = FIELD_GET(KEY_POLICY_SUPPORTED_PURPOSE_FIELD, raw_kp);
	kp->key_management_key = FIELD_GET(KEY_POLICY_KMK_FIELD, raw_kp);
	kp->wrappable_by_ephemeral_key =
		FIELD_GET(KEY_POLICY_WAEK_FIELD, raw_kp);
	kp->wrappable_by_hardware_key =
		FIELD_GET(KEY_POLICY_WAHK_FIELD, raw_kp);
	kp->wrappable_by_portable_key =
		FIELD_GET(KEY_POLICY_WAPK_FIELD, raw_kp);
	kp->boot_state_bound = FIELD_GET(KEY_POLICY_BS_FIELD, raw_kp);
	kp->evict_gsa = FIELD_GET(KEY_POLICY_EVICT_GSA_FIELD, raw_kp);
	kp->evict_ap_s = FIELD_GET(KEY_POLICY_EVICT_AP_S_FIELD, raw_kp);
	kp->evict_ap_ns = FIELD_GET(KEY_POLICY_EVICT_AP_NS_FIELD, raw_kp);
	kp->owner = FIELD_GET(KEY_POLICY_OWNER_FIELD, raw_kp);
	kp->auth_domains = FIELD_GET(KEY_POLICY_AUTH_DOMAINS_FIELD, raw_kp);

	return 0;
}
