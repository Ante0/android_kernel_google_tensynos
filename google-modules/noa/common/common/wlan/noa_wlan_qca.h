/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA WiFi Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NOA_WLAN_QCA_H__
#define __NOA_WLAN_QCA_H__

#define NUM_OF_DWORDS_TCL_DATA_CMD 7
#define NUM_OF_DWORDS_BUFFER_ADDR_INFO 2

struct buffer_addr_info {
	uint32_t buffer_addr_31_0                : 32; //[31:0]
	uint32_t buffer_addr_39_32               :  8, //[7:0]
			return_buffer_manager           :  3, //[10:8]
			sw_buffer_cookie                : 21; //[31:11]
};

struct tcl_data_cmd {
	struct            buffer_addr_info                       buf_addr_info;
	uint32_t buf_or_ext_desc_type            :  1, //[0]
		epd                             :  1, //[1]
		encap_type                      :  2, //[3:2]
		encrypt_type                    :  4, //[7:4]
		src_buffer_swap                 :  1, //[8]
		link_meta_swap                  :  1, //[9]
		tqm_no_drop                     :  1, //[10]
		reserved_2a                     :  1, //[11]
		search_type                     :  2, //[13:12]
		addrx_en                        :  1, //[14]
		addry_en                        :  1, //[15]
		tcl_cmd_number                  : 16; //[31:16]
	uint32_t data_length                     : 16, //[15:0]
		ipv4_checksum_en                :  1, //[16]
		udp_over_ipv4_checksum_en       :  1, //[17]
		udp_over_ipv6_checksum_en       :  1, //[18]
		tcp_over_ipv4_checksum_en       :  1, //[19]
		tcp_over_ipv6_checksum_en       :  1, //[20]
		to_fw                           :  1, //[21]
		reserved_3a                     :  1, //[22]
		packet_offset                   :  9; //[31:23]
	uint32_t buffer_timestamp                : 19, //[18:0]
		buffer_timestamp_valid          :  1, //[19]
		reserved_4a                     :  1, //[20]
		hlos_tid_overwrite              :  1, //[21]
		hlos_tid                        :  4, //[25:22]
		lmac_id                         :  2, //[27:26]
		reserved_4b                     :  4; //[31:28]
	uint32_t dscp_tid_table_num              :  6, //[5:0]
		search_index                    : 20, //[25:6]
		cache_set_num                   :  4, //[29:26]
		mesh_enable                     :  2; //[31:30]
	uint32_t reserved_6a                     : 20, //[19:0]
		ring_id                         :  8, //[27:20]
		looping_count                   :  4; //[31:28]
};

/* RX format */
#define NUM_OF_DWORDS_RX_MPDU_DESC_INFO 2

struct rx_mpdu_desc_info {
	uint32_t msdu_count                      :  8, //[7:0]
			mpdu_sequence_number            : 12, //[19:8]
			fragment_flag                   :  1, //[20]
			mpdu_retry_bit                  :  1, //[21]
			ampdu_flag                      :  1, //[22]
			bar_frame                       :  1, //[23]
			pn_fields_contain_valid_info    :  1, //[24]
			sa_is_valid                     :  1, //[25]
			sa_idx_timeout                  :  1, //[26]
			da_is_valid                     :  1, //[27]
			da_is_mcbc                      :  1, //[28]
			da_idx_timeout                  :  1, //[29]
			raw_mpdu                        :  1, //[30]
			more_fragment_flag              :  1; //[31]
	uint32_t peer_meta_data                  : 32; //[31:0]
};

#define NUM_OF_DWORDS_RX_MSDU_DESC_INFO 2

struct rx_msdu_desc_info {
	uint32_t first_msdu_in_mpdu_flag         :  1, //[0]
		last_msdu_in_mpdu_flag          :  1, //[1]
		msdu_continuation               :  1, //[2]
		msdu_length                     : 14, //[16:3]
		reo_destination_indication      :  5, //[21:17]
		msdu_drop                       :  1, //[22]
		sa_is_valid                     :  1, //[23]
		sa_idx_timeout                  :  1, //[24]
		da_is_valid                     :  1, //[25]
		da_is_mcbc                      :  1, //[26]
		da_idx_timeout                  :  1, //[27]
		reserved_0a                     :  4; //[31:28]
	uint32_t reserved_1a                     : 32; //[31:0]
};

#define NUM_OF_DWORDS_REO_DESTINATION_RING 16

struct reo_destination_ring {
	struct  buffer_addr_info  buf_or_link_desc_addr_info;
	struct  rx_mpdu_desc_info rx_mpdu_desc_info_details;
	struct  rx_msdu_desc_info rx_msdu_desc_info_details;
	uint32_t rx_reo_queue_desc_addr_31_0     : 32; //[31:0]
	uint32_t rx_reo_queue_desc_addr_39_32    :  8, //[7:0]
			reo_dest_buffer_type            :  1, //[8]
			reo_push_reason                 :  2, //[10:9]
			reo_error_code                  :  5, //[15:11]
			receive_queue_number            : 16; //[31:16]
	uint32_t soft_reorder_info_valid         :  1, //[0]
			reorder_opcode                  :  4, //[4:1]
			reorder_slot_index              :  8, //[12:5]
			mpdu_fragment_number            :  4, //[16:13]
			captured_msdu_data_size         :  4, //[20:17]
			sw_exception                    :  1, //[21]
			reserved_8a                     : 10; //[31:22]
	uint32_t reo_destination_struct_signature: 32; //[31:0]
	uint32_t reserved_10a                    : 32; //[31:0]
	uint32_t reserved_11a                    : 32; //[31:0]
	uint32_t reserved_12a                    : 32; //[31:0]
	uint32_t reserved_13a                    : 32; //[31:0]
	uint32_t reserved_14a                    : 32; //[31:0]
	uint32_t reserved_15                     : 20, //[19:0]
			ring_id                         :  8, //[27:20]
			looping_count                   :  4; //[31:28]
};

#define NUM_OF_DWORDS_TX_RATE_STATS_INFO 2

struct tx_rate_stats_info {
	uint32_t tx_rate_stats_info_valid        :  1, //[0]
		transmit_bw                     :  2, //[2:1]
		transmit_pkt_type               :  4, //[6:3]
		transmit_stbc                   :  1, //[7]
		transmit_ldpc                   :  1, //[8]
		transmit_sgi                    :  2, //[10:9]
		transmit_mcs                    :  4, //[14:11]
		ofdma_transmission              :  1, //[15]
		tones_in_ru                     : 12, //[27:16]
		reserved_0a                     :  4; //[31:28]
	uint32_t ppdu_transmission_tsf           : 32; //[31:0]
};

#define NUM_OF_DWORDS_WBM_RELEASE_RING 8

struct wbm_release_ring {
	struct buffer_addr_info released_buff_or_desc_addr_info;
	uint32_t release_source_module  :  3, //[2:0]
	bm_action                       :  3, //[5:3]
	buffer_or_desc_type             :  3, //[8:6]
	first_msdu_index                :  4, //[12:9]
	tqm_release_reason              :  4, //[16:13]
	rxdma_push_reason               :  2, //[18:17]
	rxdma_error_code                :  5, //[23:19]
	reo_push_reason                 :  2, //[25:24]
	reo_error_code                  :  5, //[30:26]
	wbm_internal_error              :  1; //[31]
	uint32_t tqm_status_number               : 24, //[23:0]
	transmit_count                  :  7, //[30:24]
	msdu_continuation               :  1; //[31]
	uint32_t ack_frame_rssi                  :  8, //[7:0]
	sw_release_details_valid        :  1, //[8]
	first_msdu                      :  1, //[9]
	last_msdu                       :  1, //[10]
	msdu_part_of_amsdu              :  1, //[11]
	fw_tx_notify_frame              :  1, //[12]
	buffer_timestamp                : 19; //[31:13]
	struct tx_rate_stats_info tx_rate_stats;
	uint32_t sw_peer_id             : 16, //[15:0]
	tid                             :  4, //[19:16]
	ring_id                         :  8, //[27:20]
	looping_count                   :  4; //[31:28]
};

#define RX_PKT_TLV_LEN		392

#define RX_MSDU_END_10_L3_HEADER_PADDING_OFFSET     0x00000028
#define RX_MSDU_END_10_L3_HEADER_PADDING_LSB        26
#define RX_MSDU_END_10_L3_HEADER_PADDING_MASK       0x0c000000
#endif /*__NOA_WLAN_QCA_H__*/
