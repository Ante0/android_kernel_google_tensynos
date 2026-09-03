/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NEP simualtor
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NOA_NETENGINE_H__
#define __NOA_NETENGINE_H__

#ifdef linux
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <common/core.h>
#else
#include "linux_port/tasklet.h"
#endif
#include "nep.h"

extern int32_t net_engine_prepare_pkt_and_desc(uint8_t *pkt, struct noa_desc *desc,
					       bool ignore_llc);
extern void update_wifi_tx_l2_header_for_vpn(unsigned char *l2_pkt, int l3_pkt_len);
extern void update_wifi_rx_l2_header_for_vpn(unsigned char *l2_pkt);
extern int process_pkt_for_vpn(unsigned char *pkt, int *pkt_len, int direction, u32 *ipsec_handle,
							   u32 if_id);
extern void net_engine_offload_util_cnt_inc(bool is_to_apc, bool is_downstream);
extern struct noa_session *noa_sim_lookup_session(uint8_t *pkt);
extern int noa_sim_add_session_entry(struct noa_session *entry);
extern int net_engine_init(void *data);
extern void net_engine_exit(void);
#endif /*__NOA_NETENGINE_H__*/
