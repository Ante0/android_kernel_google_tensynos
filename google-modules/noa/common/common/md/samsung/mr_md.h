/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for Modem Router Modem Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>
 */
#ifndef __MR_MD_H__
#define __MR_MD_H__

#include <common/noa_hw_ring.h>
#include <common/core.h>
#ifdef linux
#else
#include "linux_port/types.h"
#endif

struct mr_lassen_ext_txd {
	u8 channel_id; // TODO: replace with if index
	u8 src;
	u8 padding[2]; // for alignment
	struct noa_tx_ipsec_metadata ipsec_metadata;

	// This field is added to keep both mr_lassen_ext_txd and mr_lassen_ext_rxd having the
	// same size.
	u32 unused;
} __attribute__((packed, aligned(4)));

struct mr_lassen_ext_rxd {
	u8 channel_id; // TODO: replace with if index
	u8 status;
	u8 padding[2]; // for alignment
	struct noa_rx_ipsec_metadata ipsec_metadata;
} __attribute__((packed, aligned(4)));

struct mr_lassen_tx_desc {
	struct noa_desc basic;
	struct mr_lassen_ext_txd ext;
} __attribute__((packed, aligned(4)));
static_assert(NOA_DESC_MODEM_LASSEN_BYTE == sizeof(struct mr_lassen_tx_desc));

struct mr_lassen_rx_desc {
	struct noa_desc basic;
	struct mr_lassen_ext_rxd ext;
} __attribute__((packed, aligned(4)));
static_assert(NOA_DESC_MODEM_LASSEN_BYTE == sizeof(struct mr_lassen_rx_desc));
#endif /* __MR_MD_H__ */
