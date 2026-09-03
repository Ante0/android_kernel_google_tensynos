#ifndef MODULES_WDEV_IF_HW_ARCH_WCN7760_MSGBUF_DESCRIPTOR_H
#define MODULES_WDEV_IF_HW_ARCH_WCN7760_MSGBUF_DESCRIPTOR_H

// Referred from WCN7760 FW headers

struct buffer_addr_info {
	uint32_t buffer_addr_31_0                                        : 32;
	uint32_t buffer_addr_39_32                                       :  8,
		 return_buffer_manager                                   :  4,
		 sw_buffer_cookie                                        : 20;
};

struct tcl_data_cmd {
	struct   buffer_addr_info                                          buf_addr_info;
	uint32_t tcl_cmd_type                                            :  1,
		 buf_or_ext_desc_type                                    :  1,
		 bank_id                                                 :  6,
		 tx_notify_frame                                         :  3,
		 header_length_read_sel                                  :  1,
		 buffer_timestamp                                        : 19,
		 buffer_timestamp_valid                                  :  1;
	uint32_t reserved_3a                                             : 16,
		 tcl_cmd_number                                          : 16;
	uint32_t data_length                                             : 16,
		 ipv4_checksum_en                                        :  1,
		 udp_over_ipv4_checksum_en                               :  1,
		 udp_over_ipv6_checksum_en                               :  1,
		 tcp_over_ipv4_checksum_en                               :  1,
		 tcp_over_ipv6_checksum_en                               :  1,
		 to_fw                                                   :  1,
		 reserved_4a                                             :  1,
		 packet_offset                                           :  9;
	uint32_t hlos_tid_overwrite                                      :  1,
		 flow_override_enable                                    :  1,
		 who_classify_info_sel                                   :  2,
		 hlos_tid                                                :  4,
		 flow_override                                           :  1,
		 pmac_id                                                 :  2,
		 msdu_color                                              :  2,
		 reserved_5a                                             : 11,
		 vdev_id                                                 :  8;
	uint32_t search_index                                            : 20,
		 cache_set_num                                           :  4,
		 index_lookup_override                                   :  1,
		 reserved_6a                                             :  7;
	uint32_t reserved_7a                                             : 20,
		 ring_id                                                 :  8,
		 looping_count                                           :  4;
};

struct rx_mpdu_desc_info {
	uint32_t msdu_count                                              :  8,
		 fragment_flag                                           :  1,
		 mpdu_retry_bit                                          :  1,
		 ampdu_flag                                              :  1,
		 bar_frame                                               :  1,
		 pn_fields_contain_valid_info                            :  1,
		 raw_mpdu                                                :  1,
		 more_fragment_flag                                      :  1,
		 src_info                                                : 12,
		 mpdu_qos_control_valid                                  :  1,
		 tid                                                     :  4;
	uint32_t peer_meta_data                                          : 32;
};

struct rx_msdu_desc_info {
	uint32_t first_msdu_in_mpdu_flag                                 :  1,
		 last_msdu_in_mpdu_flag                                  :  1,
		 msdu_continuation                                       :  1,
		 msdu_length                                             : 14,
		 msdu_drop                                               :  1,
		 sa_is_valid                                             :  1,
		 da_is_valid                                             :  1,
		 da_is_mcbc                                              :  1,
		 l3_header_padding_msb                                   :  1,
		 tcp_udp_chksum_fail                                     :  1,
		 ip_chksum_fail                                          :  1,
		 fr_ds                                                   :  1,
		 to_ds                                                   :  1,
		 intra_bss                                               :  1,
		 dest_chip_id                                            :  2,
		 decap_format                                            :  2,
		 reserved_0a                                             :  1;
};

struct reo_destination_ring {
	struct   buffer_addr_info                                          buf_or_link_desc_addr_info;
	struct   rx_mpdu_desc_info                                         rx_mpdu_desc_info_details;
	struct   rx_msdu_desc_info                                         rx_msdu_desc_info_details;
	uint32_t buffer_virt_addr_31_0                                   : 32;
	uint32_t buffer_virt_addr_63_32                                  : 32;
	uint32_t reo_dest_buffer_type                                    :  1,
		 reo_push_reason                                         :  2,
		 reo_error_code                                          :  5,
		 captured_msdu_data_size                                 :  4,
		 sw_exception                                            :  1,
		 src_link_id                                             :  3,
		 reo_destination_struct_signature                        :  4,
		 ring_id                                                 :  8,
		 looping_count                                           :  4;
};

struct wbm_release_ring {
	struct   buffer_addr_info                                          released_buff_or_desc_addr_info;
	uint32_t release_source_module                                   :  3,
		 reserved_2a                                             :  3,
		 buffer_or_desc_type                                     :  3,
		 reserved_2b                                             : 22,
		 wbm_internal_error                                      :  1;
	uint32_t reserved_3a                                             : 32;
	uint32_t reserved_4a                                             : 32;
	uint32_t reserved_5a                                             : 32;
	uint32_t reserved_6a                                             : 32;
	uint32_t reserved_7a                                             : 28,
		 looping_count                                           :  4;
};

#define RX_PKT_TLV_LEN		392
#define RX_MSDU_END_L3_HEADER_PADDING_OFFSET     0x00000028
#define RX_MSDU_END_L3_HEADER_PADDING_LSB        26
#define RX_MSDU_END_L3_HEADER_PADDING_MSB        27
#define RX_MSDU_END_L3_HEADER_PADDING_MASK       0x0c000000

#define NUM_OF_DWORDS_RX_MSDU_END 32
#define RX_MSDU_END_TAG_WORD 1
#define RX_BE_PADDING0_BYTES 2 //8 bytes
#define RX_MPDU_START_TAG_WORD 1
#define RX_MPDU_INFO_VDEV_ID_OFFSET              0x0000005c
#define RX_MPDU_INFO_VDEV_ID_LSB                 0
#define RX_MPDU_INFO_VDEV_ID_MSB                 7
#define RX_MPDU_INFO_VDEV_ID_MASK                0x000000ff

#endif /* _MODULES_WDEV_IF_HW_ARCH_WCN7760_MSGBUF_DESCRIPTOR_H__*/
