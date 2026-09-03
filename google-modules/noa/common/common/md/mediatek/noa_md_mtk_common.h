/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for Modem Router Modem Driver
 *
 * Copyright 2023 Google LLC.
 */
#ifndef __NOA_MD_MTK_COMMON_H__
#define __NOA_MD_MTK_COMMON_H__

#include "common/noa_hw_ring.h"
#include "common/core.h"
#ifndef linux
#include "linux_port/types.h"
#endif


struct noa_modem_pkt_info {
	union {
		u32 _status;

		struct {
			u8 in_tcp_slow_start;
			u8 intf_id;
			u8 network_type;
			u8 drb_cnt;
		};
	};

	union {
		u32 _pkt_type;

		struct {
			u32 pkt_type;
		};
	};

	union {
		u32 _src;

		struct {
			u32 src;
		};
	};

	union {
		u32 _ifindex;

		struct {
			u32 ifindex;
		};
	};
}__attribute__((packed, aligned(4)));

struct noa_modem_ext_txd {
	struct noa_modem_pkt_info pkt_info;
	struct noa_tx_ipsec_metadata vpn;
}__attribute__((packed, aligned(4)));

struct noa_modem_tx_desc {
	struct noa_desc basic;
	struct noa_modem_ext_txd ext;
} __attribute__((packed, aligned(4)));
static_assert(NOA_DESC_MODEM_TX_MTK_BYTE == sizeof(struct noa_modem_tx_desc));

struct noa_modem_ext_rxd {
	union {
		u32 _status;

		struct {
			u32 ch_id : 1U;
			u32 pit_checksum : 1U;
			u32 pit_ip : 1U;
			u32 pit_pro : 1U;
		};
	};

	union {
		u32 _queue_info;

		struct {
			u32 queue_id : 1U;
			u32 hd_offset: 1U;
			u32 hash : 2U;
		};
	};
} __attribute__((packed, aligned(4)));

struct noa_modem_rx_desc {
	struct noa_desc basic;
	struct noa_modem_ext_rxd ext;
} __attribute__((packed, aligned(4)));

static_assert(NOA_DESC_MODEM_RX_MTK_BYTE == sizeof(struct noa_modem_rx_desc));

struct noa_modem_vendor_msg_pit {
	u32 dword1;
	u32 dword2;
	u32 dword3;
	u32 dword4;
} __attribute__((packed, aligned(4)));

struct noa_modem_vendor_pd_pit {
	u32 pd_header;
	u32 addr_low;
	u32 addr_high;
	u32 pd_footer;
} __attribute__((packed, aligned(4)));

struct noa_modem_rx_vendor_msg_pd_desc {
	struct noa_desc basic;
	struct noa_modem_vendor_msg_pit msg;
	struct noa_modem_vendor_pd_pit pd;
} __attribute__((packed, aligned(4)));

struct noa_modem_rx_vendor_pd_desc {
	struct noa_desc basic;
	struct noa_modem_vendor_pd_pit pd;
} __attribute__((packed, aligned(4)));

struct noa_modem_rx_refill_desc {
	u16 rx_tkid;
	u32 modem_address_low;
	u32 modem_address_high;
	u64 noa_data_addr;
} __attribute__((packed, aligned(4)));

static_assert(NOA_DESC_MODEM_RX_MTK_PD_BYTE == sizeof(struct noa_modem_rx_vendor_pd_desc));
static_assert(NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE == sizeof(struct noa_modem_rx_vendor_msg_pd_desc));
static_assert(20U == sizeof(struct noa_modem_rx_refill_desc));

#endif /* __NOA_MD_MTK_COMMON_H__ */
