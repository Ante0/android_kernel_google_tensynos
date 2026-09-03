/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NOA_WLAN_BRCM_H__
#define __NOA_WLAN_BRCM_H__

#define BCMPCIE_PKT_FLAGS_FRAME_802_3 0x01
#define BCMPCIE_PKT_FLAGS_FRAME_802_11 0x02

typedef enum bcmpcie_msgtype {
	MSG_TYPE_GEN_STATUS = 0x1,
	MSG_TYPE_RING_STATUS = 0x2,
	MSG_TYPE_FLOW_RING_CREATE = 0x3,
	MSG_TYPE_FLOW_RING_CREATE_CMPLT = 0x4,
	/* Enum value as copied from BISON 7.15: new generic message */
	MSG_TYPE_RING_CREATE_CMPLT = 0x4,
	MSG_TYPE_FLOW_RING_DELETE = 0x5,
	MSG_TYPE_FLOW_RING_DELETE_CMPLT = 0x6,
	/* Enum value as copied from BISON 7.15: new generic message */
	MSG_TYPE_RING_DELETE_CMPLT = 0x6,
	MSG_TYPE_FLOW_RING_FLUSH = 0x7,
	MSG_TYPE_FLOW_RING_FLUSH_CMPLT = 0x8,
	MSG_TYPE_IOCTLPTR_REQ = 0x9,
	MSG_TYPE_IOCTLPTR_REQ_ACK = 0xA,
	MSG_TYPE_IOCTLRESP_BUF_POST = 0xB,
	MSG_TYPE_IOCTL_CMPLT = 0xC,
	MSG_TYPE_EVENT_BUF_POST = 0xD,
	MSG_TYPE_WL_EVENT = 0xE,
	MSG_TYPE_TX_POST = 0xF,
	MSG_TYPE_TX_STATUS = 0x10,
	MSG_TYPE_RXBUF_POST = 0x11,
	MSG_TYPE_RX_CMPLT = 0x12,
	MSG_TYPE_LPBK_DMAXFER = 0x13,
	MSG_TYPE_LPBK_DMAXFER_CMPLT = 0x14,
	MSG_TYPE_FLOW_RING_RESUME = 0x15,
	MSG_TYPE_FLOW_RING_RESUME_CMPLT = 0x16,
	MSG_TYPE_FLOW_RING_SUSPEND = 0x17,
	MSG_TYPE_FLOW_RING_SUSPEND_CMPLT = 0x18,
	MSG_TYPE_INFO_BUF_POST = 0x19,
	MSG_TYPE_INFO_BUF_CMPLT = 0x1A,
	MSG_TYPE_H2D_RING_CREATE = 0x1B,
	MSG_TYPE_D2H_RING_CREATE = 0x1C,
	MSG_TYPE_H2D_RING_CREATE_CMPLT = 0x1D,
	MSG_TYPE_D2H_RING_CREATE_CMPLT = 0x1E,
	MSG_TYPE_H2D_RING_CONFIG = 0x1F,
	MSG_TYPE_D2H_RING_CONFIG = 0x20,
	MSG_TYPE_H2D_RING_CONFIG_CMPLT = 0x21,
	MSG_TYPE_D2H_RING_CONFIG_CMPLT = 0x22,
	MSG_TYPE_H2D_MAILBOX_DATA = 0x23,
	MSG_TYPE_D2H_MAILBOX_DATA = 0x24,
	MSG_TYPE_TIMESTAMP_BUFPOST = 0x25,
	MSG_TYPE_HOSTTIMSTAMP = 0x26,
	MSG_TYPE_HOSTTIMSTAMP_CMPLT = 0x27,
	MSG_TYPE_FIRMWARE_TIMESTAMP = 0x28,
	MSG_TYPE_SNAPSHOT_UPLOAD = 0x29,
	MSG_TYPE_SNAPSHOT_CMPLT = 0x2A,
	MSG_TYPE_H2D_RING_DELETE = 0x2B,
	MSG_TYPE_D2H_RING_DELETE = 0x2C,
	MSG_TYPE_H2D_RING_DELETE_CMPLT = 0x2D,
	MSG_TYPE_D2H_RING_DELETE_CMPLT = 0x2E,
	MSG_TYPE_TX_POST_AGGR = 0x2F,
	MSG_TYPE_TX_STATUS_AGGR = 0x30,
	MSG_TYPE_RXBUF_POST_AGGR = 0x31,
	MSG_TYPE_RX_CMPLT_AGGR = 0x32,
	MSG_TYPE_MDATA_CPL = 0x33,
	MSG_TYPE_API_MAX_RSVD = 0x3F
} bcmpcie_msg_type_t;

struct cmn_msg_hdr {
	u8 msg_type;
	u8 if_id;
	u8 flags;
	u8 epoch;
	u32 request_id;
};

struct compl_msg_hdr {
	union {
		signed short status;
#ifdef linux
		struct pktts_compl_hdr {
#else
		struct {
#endif
			u16 d_t4;
		} tx_pktts;
	};
	union {
		u16 ring_id;
		u16 flow_ring_id;
	};
};

struct ts_timestamp_srcid {
	union {
		u32 ts_low;
		u32 rate_spec;
	};
	union {
		u32 ts_high;
		union {
			u32 ts_high_ext : 28;
			u32 clk_id_ext : 3;
			u32 phase : 1;
			u32 marker_ext;
		};
		u32 tx_pkt_band_retry_info;
	};
};

struct pktts {
	u32 tref;
	u16 d_t2;
	u16 d_t3;
};

struct wlan_rxbuf_cmpl {
	struct cmn_msg_hdr cmn_hdr;
	struct compl_msg_hdr compl_hdr;
	u16 metadata_len;
	u16 data_len;
	u16 data_offset;
	u16 flags;
	u32 rx_status_0;
	u32 rx_status_1;

	union {
		struct {
			u32 marker;
			struct ts_timestamp_srcid ts;
		};
		struct {
			struct pktts rx_pktts;
			u32 marker_ext;
		};
	};
};

enum {
	PKT_CSUM_TYPE_IPV4_SHIFT = 0, /* pkt has IPv4 hdr */
	PKT_CSUM_TYPE_IPV6_SHIFT = 1, /* pkt has IPv6 hdr */
	PKT_CSUM_TYPE_TCP_SHIFT = 2, /* pkt has TCP hdr */
	PKT_CSUM_TYPE_UDP_SHIFT = 3, /* pkt has UDP hdr */
	PKT_CSUM_TYPE_NWK_CSUM_SHIFT = 4, /* pkt requires IP csum offload */
	PKT_CSUM_TYPE_TRANS_CSUM_SHIFT = 5, /* pkt requires TCP/UDP csum offload */
	PKT_CSUM_TYPE_PSEUDOHDR_CSUM_SHIFT = 6, /* pkt requires pseudo header csum offload */
};

struct pkt_info_cso {
	/* packet csum type = ipv4/v6|udp|tcp|nwk_csum|trans_csum|ph_csum */
	u8 ver;
	u8 pkt_csum_type;
	u8 nwk_hdr_len; /* IP header length */
	u8 trans_hdr_len; /* TCP header length */
};

struct host_txbuf_post {
	struct cmn_msg_hdr cmn_hdr;
	u8 txhdr[14];
	u8 flags;
	u8 seg_cnt;
	unsigned long long metadata_buf_addr;
	unsigned long long data_buf_addr;
	u16 metadata_buf_len;
	u16 data_len;
	union {
		struct {
			u8 ext_flags;
			u8 scale_factor;
			u8 rate;
			u8 exp_time;
		};
		u32 marker;
	};
	struct pkt_info_cso pktinfo;
	u32 pad;
};

struct host_txbuf_cmpl {
	struct cmn_msg_hdr cmn_hdr;
	struct compl_msg_hdr compl_hdr;
	union {
		struct {
			struct {
				union {
					u16 metadata_len;
					u16 tx_status_ext;
				};
				u16 tx_status;
			};
			struct ts_timestamp_srcid ts;
		};
		struct {
			struct pktts tx_pktts;
			u32 marker_ext;
		};
	};
};

struct host_rxbuf_post {
	/** common message header */
	struct cmn_msg_hdr cmn_hdr;
	/** provided meta data buffer len */
	u16 metadata_buf_len;
	/** provided data buffer len to receive data */
	u16 data_buf_len;
	/** alignment to make the host buffers start on 8 byte boundary */
	u16 rsvd;
	/** provided meta data buffer */
	u64 metadata_buf_addr;
	/** provided data buffer to receive data */
	union {
		struct {
			u32 low_addr;
			u32 high_addr;
		};
		u64 addr64;
	};
};

#endif /*__NOA_WLAN_BRCM_H__*/
