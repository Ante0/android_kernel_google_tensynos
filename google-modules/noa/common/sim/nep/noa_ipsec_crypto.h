/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles XFRM packet offload.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */
#ifndef NOA_IPSEC_CRYPTO_H_
#define NOA_IPSEC_CRYPTO_H_

#include <net/xfrm.h>

#if IS_ENABLED(CONFIG_NOA_SIM_VPN_OFFLOAD_SUPPORT)
int encrypt_packet_for_packet_offload(const struct xfrm_state *sa, unsigned char *pkt, int pkt_len,
				      unsigned char *output, int oseq);
int encrypt_packet_for_crypto_offload(const struct xfrm_state *sa, unsigned char *pkt, int pkt_len);
int decrypt_packet(const struct xfrm_state *sa, unsigned char *pkt, int pkt_len,
		   unsigned char *out_pkt);
#else
static inline int encrypt_packet_for_packet_offload(const struct xfrm_state *sa, unsigned char *pkt,
						    int pkt_len, unsigned char *output, int oseq)
{
	return -EOPNOTSUPP;
}
static inline int encrypt_packet_for_crypto_offload(const struct xfrm_state *sa, unsigned char *pkt,
						    int pkt_len)
{
	return -EOPNOTSUPP;
}
static inline int decrypt_packet(const struct xfrm_state *sa, unsigned char *pkt, int pkt_len,
				 unsigned char *out_pkt)
{
	return -EOPNOTSUPP;
}
#endif

#endif  // NOA_IPSEC_CRYPTO_H_
