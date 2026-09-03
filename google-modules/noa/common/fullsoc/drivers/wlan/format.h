/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * LVM WLAN Ring Descriptor Formats
 *
 * Copyright (c) 2024 Google LLC.
 * Author: Henry Yen <henryyen@google.com>
 */

#ifndef __LVM_DRIVER_WLAN_FORMAT_H__
#define __LVM_DRIVER_WLAN_FORMAT_H__

struct cmn_msg_hdr {
	u8	msg_type;
	u8	if_id;
	u8	flags;
	u8	epoch;
	u32	request_id;
};

struct compl_msg_hdr {
	union {
		signed short	status;

		struct pktts_compl_hdr {
			u16	d_t4;
		} tx_pktts;
	};

	union {
		u16		ring_id;
		u16		flow_ring_id;
	};
};

struct ts_timestamp_srcid {
	union {
		u32		ts_low;
		u32		rate_spec;
	};

	union {
		u32		ts_high;

		union {
			u32	ts_high_ext : 28;
			u32	clk_id_ext : 3;
			u32	phase : 1;
			u32	marker_ext;
		};

		u32		tx_pkt_band_retry_info;
	};
};

struct pktts {
	u32	tref;
	u16	d_t2;
	u16	d_t3;
};

struct wlan_rxbuf_cmpl {
	struct cmn_msg_hdr	cmn_hdr;
	struct compl_msg_hdr	compl_hdr;
	u16			metadata_len;
	u16			data_len;
	u16			data_offset;
	u16			flags;
	u32			rx_status_0;
	u32			rx_status_1;

	union {
		struct {
			u32	marker;
			struct ts_timestamp_srcid ts;
		};

		struct {
			struct pktts	rx_pktts;
			u32		marker_ext;
		};
	};
};

struct pkt_info_cso {
	u8	ver;
	u8	pkt_csum_type;
	u8	nwk_hdr_len;
	u8	trans_hdr_len;
};

struct host_txbuf_post {
	struct cmn_msg_hdr	cmn_hdr;
	u8			txhdr[14];
	u8			flags;
	u8			seg_cnt;
	unsigned long long	metadata_buf_addr;
	unsigned long long	data_buf_addr;
	u16			metadata_buf_len;
	u16			data_len;

	union {
		struct {
			u8	ext_flags;
			u8	scale_factor;
			u8	rate;
			u8	exp_time;
		};

		u32		marker;
	};

	struct pkt_info_cso	pktinfo;
	u32			pad;
};

struct host_txbuf_cmpl {
	struct cmn_msg_hdr	cmn_hdr;
	struct compl_msg_hdr	compl_hdr;

	union {
		struct {
			struct {
				union {
					u16	metadata_len;
					u16	tx_status_ext;
				};

				u16		tx_status;
			};

			struct ts_timestamp_srcid ts;
		};

		struct {
			struct pktts		tx_pktts;
			u32			marker_ext;
		};
	};
};

struct host_rxbuf_post {
	struct cmn_msg_hdr	cmn_hdr;
	u16			metadata_buf_len;
	u16			data_buf_len;
	u16			rsvd;
	u64			metadata_buf_addr;

	union {
		struct {
			u32	low_addr;
			u32	high_addr;
		};

		u64		addr64;
		u64		data_buf_addr;
	};
};

#endif  /* __LVM_DRIVER_WLAN_FORMAT_H__ */
