/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * GPCA crypto test function declarations.
 *
 * Copyright (C) 2023 Google LLC.
 */

#ifndef _GOOGLE_GPCA_CRYPTO_TEST_H
#define _GOOGLE_GPCA_CRYPTO_TEST_H

#include <kunit/test.h>

void gpca_crypto_aes(struct kunit *test);
void gpca_crypto_tdes(struct kunit *test);
void gpca_crypto_aes_gcm(struct kunit *test);
void gpca_crypto_sha(struct kunit *test);
void gpca_crypto_hmac(struct kunit *test);
void gpca_crypto_ecc(struct kunit *test);
void gpca_crypto_aes_gcm_known_answer(struct kunit *test);
void gpca_crypto_start_crypto_op_data(struct kunit *test);
void gpca_crypto_get_set_context(struct kunit *test);
void gpca_crypto_op_slot_management(struct kunit *test);
void gpca_crypto_async_sha_update(struct kunit *test);
void gpca_crypto_concurrent_sha(struct kunit *test);
void gpca_crypto_lkc_ecc(struct kunit *test);
void gpca_crypto_aes_tdes_streaming(struct kunit *test);
void gpca_crypto_sha_hmac_streaming(struct kunit *test);
void gpca_crypto_aes_gcm_streaming(struct kunit *test);

#endif /* _GOOGLE_GPCA_CRYPTO_TEST_H */
