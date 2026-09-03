/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Handles XFRM packet offload.
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Mike Yu <yumike@google.com>
 */
#ifndef NOA_IPSEC_H_
#define NOA_IPSEC_H_

#include <linux/types.h>
#include <linux/netdevice.h>

#include <common/core.h>

typedef struct {
	struct xfrm_state *state;
	int seq;
} noa_xfrm_state;

#if 0
int do_encryption(struct sk_buff *skb);
bool do_decryption(struct sk_buff *skb);
bool is_encryption_needed(struct sk_buff *skb);
bool is_decryption_needed(struct sk_buff *skb);
#endif

#if IS_ENABLED(CONFIG_NOA_SIM_VPN_OFFLOAD_SUPPORT)
noa_xfrm_state lookup_sa(unsigned char *packet, int direction, int *oseq,
			 u32 if_id);

// In NOA driver mode, the RPC between AP and NOA is not implemented. Add these functions to
// simulate passing SA/SP from AP to NOA.
int noa_vpn_add_state(struct xfrm_state *state, int seq);
void noa_vpn_del_state(struct xfrm_state *state);
void noa_vpn_free_state(struct xfrm_state *state);
int noa_vpn_add_policy(struct xfrm_policy *policy);
void noa_vpn_del_policy(struct xfrm_policy *policy);
void noa_vpn_free_policy(struct xfrm_policy *policy);
#else
static inline noa_xfrm_state lookup_sa(unsigned char *packet, int direction, int *oseq,
			 u32 if_id)
{
	return (noa_xfrm_state){ .state = NULL,
				 .seq = 0 };
}

static inline int noa_vpn_add_state(struct xfrm_state *state, int seq)
{
	return 0;
}
static inline void noa_vpn_del_state(struct xfrm_state *state) {}
static inline void noa_vpn_free_state(struct xfrm_state *state) {}
static inline int noa_vpn_add_policy(struct xfrm_policy *policy)
{
	return 0;
}
static inline void noa_vpn_del_policy(struct xfrm_policy *policy) {}
static inline void noa_vpn_free_policy(struct xfrm_policy *policy) {}
#endif

#endif  // NOA_IPSEC_H_
