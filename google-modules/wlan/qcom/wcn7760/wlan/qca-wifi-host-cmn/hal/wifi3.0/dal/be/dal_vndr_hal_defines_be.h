/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _DAL_VNDR_HAL_DEFINES_BE_H_
#define _DAL_VNDR_HAL_DEFINES_BE_H_
#include <linux/types.h>
#include "dal_vndr_hal_internal.h"

#define NUM_OF_DWORDS_BUFFER_ADDR_INFO 2

struct dal_buffer_addr_info {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
	 uint32_t buffer_addr_31_0                   : 32;
	 uint32_t buffer_addr_39_32                  :  8,
			  return_buffer_manager              :  4,
			  sw_buffer_cookie                   : 20;
#else
	 uint32_t buffer_addr_31_0                   : 32;
	 uint32_t sw_buffer_cookie                   : 20,
			  return_buffer_manager              :  4,
			  buffer_addr_39_32                  :  8;
#endif
};

#define BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET             0x00000000
#define BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_LSB                0
#define BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_MSB                31
#define BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_MASK               0xffffffff

#define BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET            0x00000004
#define BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_LSB               0
#define BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_MSB               7
#define BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_MASK              0x000000ff

#define BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_OFFSET        0x00000004
#define BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_LSB           8
#define BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_MSB           11
#define BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_MASK          0x00000f00

#define BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET             0x00000004
#define BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_LSB                12
#define BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_MSB                31
#define BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_MASK               0xfffff000
/*
 * macro to set the LSW of the nbuf data physical address
 * to the rxdma ring entry
 */
#define DAL_VNDR_HAL_RXDMA_PADDR_LO_SET(buff_addr_info, paddr_lo) \
	((*(((unsigned int *) buff_addr_info) + \
	(DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET >> 2))) = \
	(paddr_lo << DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_LSB) & \
	DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_MASK)

/*
 * macro to set the LSB of MSW of the nbuf data physical address
 * to the rxdma ring entry
 */
#define DAL_VNDR_HAL_RXDMA_PADDR_HI_SET(buff_addr_info, paddr_hi) \
	((*(((unsigned int *) buff_addr_info) + \
	(DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET >> 2))) = \
	(paddr_hi << DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_LSB) & \
	DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_MASK)
/*
 * macro to set the cookie into the rxdma ring entry
 */
#define DAL_VNDR_HAL_RXDMA_COOKIE_SET(buff_addr_info, cookie) \
	((*(((unsigned int *)buff_addr_info) + \
	(BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET >> 2))) &= \
	~BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_MASK); \
	((*(((unsigned int *)buff_addr_info) + \
	(BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET >> 2))) |= \
	(cookie << BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_LSB) & \
	BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_MASK)
/*
 * macro to set the manager into the rxdma ring entry
 */
#define DAL_VNDR_HAL_RXDMA_MANAGER_SET(buff_addr_info, manager) \
	((*(((unsigned int *)buff_addr_info) + \
	(BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_OFFSET >> 2))) &= \
	~BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_MASK); \
	((*(((unsigned int *)buff_addr_info) + \
	(BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_OFFSET >> 2))) |= \
	(manager << BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_LSB) & \
	BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_MASK)

#define NUM_OF_DWORDS_TX_RATE_STATS_INFO 2

struct dal_tx_rate_stats_info {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
	uint32_t tx_rate_stats_info_valid                                :  1,
			 transmit_bw                                             :  3,
			 transmit_pkt_type                                       :  4,
			 transmit_stbc                                           :  1,
			 transmit_ldpc                                           :  1,
			 transmit_sgi                                            :  2,
			 transmit_mcs                                            :  4,
			 ofdma_transmission                                      :  1,
			 tones_in_ru                                             : 12,
			 transmit_nss                                            :  3;
	uint32_t ppdu_transmission_tsf                                   : 32;
#else
	 uint32_t transmit_nss                                            :  3,
			  tones_in_ru                                             : 12,
			  ofdma_transmission                                      :  1,
			  transmit_mcs                                            :  4,
			  transmit_sgi                                            :  2,
			  transmit_ldpc                                           :  1,
			  transmit_stbc                                           :  1,
			  transmit_pkt_type                                       :  4,
			  transmit_bw                                             :  3,
			  tx_rate_stats_info_valid                                :  1;
	 uint32_t ppdu_transmission_tsf                                   : 32;
#endif
};

#define TX_RATE_STATS_INFO_TX_RATE_STATS_INFO_VALID_OFFSET                          0x00000000
#define TX_RATE_STATS_INFO_TX_RATE_STATS_INFO_VALID_LSB                             0
#define TX_RATE_STATS_INFO_TX_RATE_STATS_INFO_VALID_MSB                             0
#define TX_RATE_STATS_INFO_TX_RATE_STATS_INFO_VALID_MASK                            0x00000001

#define TX_RATE_STATS_INFO_TRANSMIT_BW_OFFSET                                       0x00000000
#define TX_RATE_STATS_INFO_TRANSMIT_BW_LSB                                          1
#define TX_RATE_STATS_INFO_TRANSMIT_BW_MSB                                          3
#define TX_RATE_STATS_INFO_TRANSMIT_BW_MASK                                         0x0000000e

#define TX_RATE_STATS_INFO_TRANSMIT_PKT_TYPE_OFFSET                                 0x00000000
#define TX_RATE_STATS_INFO_TRANSMIT_PKT_TYPE_LSB                                    4
#define TX_RATE_STATS_INFO_TRANSMIT_PKT_TYPE_MSB                                    7
#define TX_RATE_STATS_INFO_TRANSMIT_PKT_TYPE_MASK                                   0x000000f0

#define TX_RATE_STATS_INFO_TRANSMIT_STBC_OFFSET                                     0x00000000
#define TX_RATE_STATS_INFO_TRANSMIT_STBC_LSB                                        8
#define TX_RATE_STATS_INFO_TRANSMIT_STBC_MSB                                        8
#define TX_RATE_STATS_INFO_TRANSMIT_STBC_MASK                                       0x00000100

#define TX_RATE_STATS_INFO_TRANSMIT_LDPC_OFFSET                                     0x00000000
#define TX_RATE_STATS_INFO_TRANSMIT_LDPC_LSB                                        9
#define TX_RATE_STATS_INFO_TRANSMIT_LDPC_MSB                                        9
#define TX_RATE_STATS_INFO_TRANSMIT_LDPC_MASK                                       0x00000200

#define TX_RATE_STATS_INFO_TRANSMIT_SGI_OFFSET                                      0x00000000
#define TX_RATE_STATS_INFO_TRANSMIT_SGI_LSB                                         10
#define TX_RATE_STATS_INFO_TRANSMIT_SGI_MSB                                         11
#define TX_RATE_STATS_INFO_TRANSMIT_SGI_MASK                                        0x00000c00

#define TX_RATE_STATS_INFO_TRANSMIT_MCS_OFFSET                                      0x00000000
#define TX_RATE_STATS_INFO_TRANSMIT_MCS_LSB                                         12
#define TX_RATE_STATS_INFO_TRANSMIT_MCS_MSB                                         15
#define TX_RATE_STATS_INFO_TRANSMIT_MCS_MASK                                        0x0000f000

#define TX_RATE_STATS_INFO_OFDMA_TRANSMISSION_OFFSET                                0x00000000
#define TX_RATE_STATS_INFO_OFDMA_TRANSMISSION_LSB                                   16
#define TX_RATE_STATS_INFO_OFDMA_TRANSMISSION_MSB                                   16
#define TX_RATE_STATS_INFO_OFDMA_TRANSMISSION_MASK                                  0x00010000

#define TX_RATE_STATS_INFO_TONES_IN_RU_OFFSET                                       0x00000000
#define TX_RATE_STATS_INFO_TONES_IN_RU_LSB                                          17
#define TX_RATE_STATS_INFO_TONES_IN_RU_MSB                                          28
#define TX_RATE_STATS_INFO_TONES_IN_RU_MASK                                         0x1ffe0000

#define TX_RATE_STATS_INFO_TRANSMIT_NSS_OFFSET                                      0x00000000
#define TX_RATE_STATS_INFO_TRANSMIT_NSS_LSB                                         29
#define TX_RATE_STATS_INFO_TRANSMIT_NSS_MSB                                         31
#define TX_RATE_STATS_INFO_TRANSMIT_NSS_MASK                                        0xe0000000

#define TX_RATE_STATS_INFO_PPDU_TRANSMISSION_TSF_OFFSET                             0x00000004
#define TX_RATE_STATS_INFO_PPDU_TRANSMISSION_TSF_LSB                                0
#define TX_RATE_STATS_INFO_PPDU_TRANSMISSION_TSF_MSB                                31
#define TX_RATE_STATS_INFO_PPDU_TRANSMISSION_TSF_MASK                               0xffffffff

#define NUM_OF_DWORDS_TCL_DATA_CMD 8

struct dal_tcl_data_cmd {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
	 struct   dal_buffer_addr_info                       buf_addr_info;
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
#else
	 struct   dal_buffer_addr_info                       buf_addr_info;
	 uint32_t buffer_timestamp_valid                                  :  1,
			  buffer_timestamp                                        : 19,
			  header_length_read_sel                                  :  1,
			  tx_notify_frame                                         :  3,
			  bank_id                                                 :  6,
			  buf_or_ext_desc_type                                    :  1,
			  tcl_cmd_type                                            :  1;
	 uint32_t tcl_cmd_number                                          : 16,
			  reserved_3a                                             : 16;
	 uint32_t packet_offset                                           :  9,
			  reserved_4a                                             :  1,
			  to_fw                                                   :  1,
			  tcp_over_ipv6_checksum_en                               :  1,
			  tcp_over_ipv4_checksum_en                               :  1,
			  udp_over_ipv6_checksum_en                               :  1,
			  udp_over_ipv4_checksum_en                               :  1,
			  ipv4_checksum_en                                        :  1,
			  data_length                                             : 16;
	 uint32_t vdev_id                                                 :  8,
			  reserved_5a                                             : 11,
			  msdu_color                                              :  2,
			  pmac_id                                                 :  2,
			  flow_override                                           :  1,
			  hlos_tid                                                :  4,
			  who_classify_info_sel                                   :  2,
			  flow_override_enable                                    :  1,
			  hlos_tid_overwrite                                      :  1;
	 uint32_t reserved_6a                                             :  7,
			  index_lookup_override                                   :  1,
			  cache_set_num                                           :  4,
			  search_index                                            : 20;
	 uint32_t looping_count                                           :  4,
			  ring_id                                                 :  8,
			  reserved_7a                                             : 20;
#endif
};

#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET       0x00000000
#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_31_0_LSB          0
#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_31_0_MSB          31
#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_31_0_MASK         0xffffffff

#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET      0x00000004
#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_39_32_LSB         0
#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_39_32_MSB         7
#define TCL_DATA_CMD_BUF_ADDR_INFO_BUFFER_ADDR_39_32_MASK        0x000000ff

#define TCL_DATA_CMD_BUF_ADDR_INFO_RETURN_BUFFER_MANAGER_OFFSET  0x00000004
#define TCL_DATA_CMD_BUF_ADDR_INFO_RETURN_BUFFER_MANAGER_LSB     8
#define TCL_DATA_CMD_BUF_ADDR_INFO_RETURN_BUFFER_MANAGER_MSB     11
#define TCL_DATA_CMD_BUF_ADDR_INFO_RETURN_BUFFER_MANAGER_MASK    0x00000f00

#define TCL_DATA_CMD_BUF_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET       0x00000004
#define TCL_DATA_CMD_BUF_ADDR_INFO_SW_BUFFER_COOKIE_LSB          12
#define TCL_DATA_CMD_BUF_ADDR_INFO_SW_BUFFER_COOKIE_MSB          31
#define TCL_DATA_CMD_BUF_ADDR_INFO_SW_BUFFER_COOKIE_MASK         0xfffff000

#define TCL_DATA_CMD_TCL_CMD_TYPE_OFFSET                         0x00000008
#define TCL_DATA_CMD_TCL_CMD_TYPE_LSB                            0
#define TCL_DATA_CMD_TCL_CMD_TYPE_MSB                            0
#define TCL_DATA_CMD_TCL_CMD_TYPE_MASK                           0x00000001

#define TCL_DATA_CMD_BUF_OR_EXT_DESC_TYPE_OFFSET                 0x00000008
#define TCL_DATA_CMD_BUF_OR_EXT_DESC_TYPE_LSB                    1
#define TCL_DATA_CMD_BUF_OR_EXT_DESC_TYPE_MSB                    1
#define TCL_DATA_CMD_BUF_OR_EXT_DESC_TYPE_MASK                   0x00000002

#define TCL_DATA_CMD_BANK_ID_OFFSET                              0x00000008
#define TCL_DATA_CMD_BANK_ID_LSB                                 2
#define TCL_DATA_CMD_BANK_ID_MSB                                 7
#define TCL_DATA_CMD_BANK_ID_MASK                                0x000000fc

#define TCL_DATA_CMD_TX_NOTIFY_FRAME_OFFSET                      0x00000008
#define TCL_DATA_CMD_TX_NOTIFY_FRAME_LSB                         8
#define TCL_DATA_CMD_TX_NOTIFY_FRAME_MSB                         10
#define TCL_DATA_CMD_TX_NOTIFY_FRAME_MASK                        0x00000700

#define TCL_DATA_CMD_HEADER_LENGTH_READ_SEL_OFFSET               0x00000008
#define TCL_DATA_CMD_HEADER_LENGTH_READ_SEL_LSB                  11
#define TCL_DATA_CMD_HEADER_LENGTH_READ_SEL_MSB                  11
#define TCL_DATA_CMD_HEADER_LENGTH_READ_SEL_MASK                 0x00000800

#define TCL_DATA_CMD_BUFFER_TIMESTAMP_OFFSET                     0x00000008
#define TCL_DATA_CMD_BUFFER_TIMESTAMP_LSB                        12
#define TCL_DATA_CMD_BUFFER_TIMESTAMP_MSB                        30
#define TCL_DATA_CMD_BUFFER_TIMESTAMP_MASK                       0x7ffff000

#define TCL_DATA_CMD_BUFFER_TIMESTAMP_VALID_OFFSET               0x00000008
#define TCL_DATA_CMD_BUFFER_TIMESTAMP_VALID_LSB                  31
#define TCL_DATA_CMD_BUFFER_TIMESTAMP_VALID_MSB                  31
#define TCL_DATA_CMD_BUFFER_TIMESTAMP_VALID_MASK                 0x80000000

#define TCL_DATA_CMD_RESERVED_3A_OFFSET                          0x0000000c
#define TCL_DATA_CMD_RESERVED_3A_LSB                             0
#define TCL_DATA_CMD_RESERVED_3A_MSB                             15
#define TCL_DATA_CMD_RESERVED_3A_MASK                            0x0000ffff

#define TCL_DATA_CMD_TCL_CMD_NUMBER_OFFSET                       0x0000000c
#define TCL_DATA_CMD_TCL_CMD_NUMBER_LSB                          16
#define TCL_DATA_CMD_TCL_CMD_NUMBER_MSB                          31
#define TCL_DATA_CMD_TCL_CMD_NUMBER_MASK                         0xffff0000

#define TCL_DATA_CMD_DATA_LENGTH_OFFSET                          0x00000010
#define TCL_DATA_CMD_DATA_LENGTH_LSB                             0
#define TCL_DATA_CMD_DATA_LENGTH_MSB                             15
#define TCL_DATA_CMD_DATA_LENGTH_MASK                            0x0000ffff

#define TCL_DATA_CMD_IPV4_CHECKSUM_EN_OFFSET                     0x00000010
#define TCL_DATA_CMD_IPV4_CHECKSUM_EN_LSB                        16
#define TCL_DATA_CMD_IPV4_CHECKSUM_EN_MSB                        16
#define TCL_DATA_CMD_IPV4_CHECKSUM_EN_MASK                       0x00010000

#define TCL_DATA_CMD_UDP_OVER_IPV4_CHECKSUM_EN_OFFSET            0x00000010
#define TCL_DATA_CMD_UDP_OVER_IPV4_CHECKSUM_EN_LSB               17
#define TCL_DATA_CMD_UDP_OVER_IPV4_CHECKSUM_EN_MSB               17
#define TCL_DATA_CMD_UDP_OVER_IPV4_CHECKSUM_EN_MASK              0x00020000

#define TCL_DATA_CMD_UDP_OVER_IPV6_CHECKSUM_EN_OFFSET            0x00000010
#define TCL_DATA_CMD_UDP_OVER_IPV6_CHECKSUM_EN_LSB               18
#define TCL_DATA_CMD_UDP_OVER_IPV6_CHECKSUM_EN_MSB               18
#define TCL_DATA_CMD_UDP_OVER_IPV6_CHECKSUM_EN_MASK              0x00040000

#define TCL_DATA_CMD_TCP_OVER_IPV4_CHECKSUM_EN_OFFSET            0x00000010
#define TCL_DATA_CMD_TCP_OVER_IPV4_CHECKSUM_EN_LSB               19
#define TCL_DATA_CMD_TCP_OVER_IPV4_CHECKSUM_EN_MSB               19
#define TCL_DATA_CMD_TCP_OVER_IPV4_CHECKSUM_EN_MASK              0x00080000

#define TCL_DATA_CMD_TCP_OVER_IPV6_CHECKSUM_EN_OFFSET            0x00000010
#define TCL_DATA_CMD_TCP_OVER_IPV6_CHECKSUM_EN_LSB               20
#define TCL_DATA_CMD_TCP_OVER_IPV6_CHECKSUM_EN_MSB               20
#define TCL_DATA_CMD_TCP_OVER_IPV6_CHECKSUM_EN_MASK              0x00100000

#define TCL_DATA_CMD_TO_FW_OFFSET                                0x00000010
#define TCL_DATA_CMD_TO_FW_LSB                                   21
#define TCL_DATA_CMD_TO_FW_MSB                                   21
#define TCL_DATA_CMD_TO_FW_MASK                                  0x00200000

#define TCL_DATA_CMD_RESERVED_4A_OFFSET                          0x00000010
#define TCL_DATA_CMD_RESERVED_4A_LSB                             22
#define TCL_DATA_CMD_RESERVED_4A_MSB                             22
#define TCL_DATA_CMD_RESERVED_4A_MASK                            0x00400000

#define TCL_DATA_CMD_PACKET_OFFSET_OFFSET                        0x00000010
#define TCL_DATA_CMD_PACKET_OFFSET_LSB                           23
#define TCL_DATA_CMD_PACKET_OFFSET_MSB                           31
#define TCL_DATA_CMD_PACKET_OFFSET_MASK                          0xff800000

#define TCL_DATA_CMD_HLOS_TID_OVERWRITE_OFFSET                   0x00000014
#define TCL_DATA_CMD_HLOS_TID_OVERWRITE_LSB                      0
#define TCL_DATA_CMD_HLOS_TID_OVERWRITE_MSB                      0
#define TCL_DATA_CMD_HLOS_TID_OVERWRITE_MASK                     0x00000001

#define TCL_DATA_CMD_FLOW_OVERRIDE_ENABLE_OFFSET                 0x00000014
#define TCL_DATA_CMD_FLOW_OVERRIDE_ENABLE_LSB                    1
#define TCL_DATA_CMD_FLOW_OVERRIDE_ENABLE_MSB                    1
#define TCL_DATA_CMD_FLOW_OVERRIDE_ENABLE_MASK                   0x00000002

#define TCL_DATA_CMD_WHO_CLASSIFY_INFO_SEL_OFFSET                0x00000014
#define TCL_DATA_CMD_WHO_CLASSIFY_INFO_SEL_LSB                   2
#define TCL_DATA_CMD_WHO_CLASSIFY_INFO_SEL_MSB                   3
#define TCL_DATA_CMD_WHO_CLASSIFY_INFO_SEL_MASK                  0x0000000c

#define TCL_DATA_CMD_HLOS_TID_OFFSET                             0x00000014
#define TCL_DATA_CMD_HLOS_TID_LSB                                4
#define TCL_DATA_CMD_HLOS_TID_MSB                                7
#define TCL_DATA_CMD_HLOS_TID_MASK                               0x000000f0

#define TCL_DATA_CMD_FLOW_OVERRIDE_OFFSET                        0x00000014
#define TCL_DATA_CMD_FLOW_OVERRIDE_LSB                           8
#define TCL_DATA_CMD_FLOW_OVERRIDE_MSB                           8
#define TCL_DATA_CMD_FLOW_OVERRIDE_MASK                          0x00000100

#define TCL_DATA_CMD_PMAC_ID_OFFSET                              0x00000014
#define TCL_DATA_CMD_PMAC_ID_LSB                                 9
#define TCL_DATA_CMD_PMAC_ID_MSB                                 10
#define TCL_DATA_CMD_PMAC_ID_MASK                                0x00000600

#define TCL_DATA_CMD_MSDU_COLOR_OFFSET                           0x00000014
#define TCL_DATA_CMD_MSDU_COLOR_LSB                              11
#define TCL_DATA_CMD_MSDU_COLOR_MSB                              12
#define TCL_DATA_CMD_MSDU_COLOR_MASK                             0x00001800

#define TCL_DATA_CMD_RESERVED_5A_OFFSET                          0x00000014
#define TCL_DATA_CMD_RESERVED_5A_LSB                             13
#define TCL_DATA_CMD_RESERVED_5A_MSB                             23
#define TCL_DATA_CMD_RESERVED_5A_MASK                            0x00ffe000

#define TCL_DATA_CMD_VDEV_ID_OFFSET                              0x00000014
#define TCL_DATA_CMD_VDEV_ID_LSB                                 24
#define TCL_DATA_CMD_VDEV_ID_MSB                                 31
#define TCL_DATA_CMD_VDEV_ID_MASK                                0xff000000

#define TCL_DATA_CMD_SEARCH_INDEX_OFFSET                         0x00000018
#define TCL_DATA_CMD_SEARCH_INDEX_LSB                            0
#define TCL_DATA_CMD_SEARCH_INDEX_MSB                            19
#define TCL_DATA_CMD_SEARCH_INDEX_MASK                           0x000fffff

#define TCL_DATA_CMD_CACHE_SET_NUM_OFFSET                        0x00000018
#define TCL_DATA_CMD_CACHE_SET_NUM_LSB                           20
#define TCL_DATA_CMD_CACHE_SET_NUM_MSB                           23
#define TCL_DATA_CMD_CACHE_SET_NUM_MASK                          0x00f00000

#define TCL_DATA_CMD_INDEX_LOOKUP_OVERRIDE_OFFSET                0x00000018
#define TCL_DATA_CMD_INDEX_LOOKUP_OVERRIDE_LSB                   24
#define TCL_DATA_CMD_INDEX_LOOKUP_OVERRIDE_MSB                   24
#define TCL_DATA_CMD_INDEX_LOOKUP_OVERRIDE_MASK                  0x01000000

#define TCL_DATA_CMD_RESERVED_6A_OFFSET                          0x00000018
#define TCL_DATA_CMD_RESERVED_6A_LSB                             25
#define TCL_DATA_CMD_RESERVED_6A_MSB                             31
#define TCL_DATA_CMD_RESERVED_6A_MASK                            0xfe000000

#define TCL_DATA_CMD_RESERVED_7A_OFFSET                          0x0000001c
#define TCL_DATA_CMD_RESERVED_7A_LSB                             0
#define TCL_DATA_CMD_RESERVED_7A_MSB                             19
#define TCL_DATA_CMD_RESERVED_7A_MASK                            0x000fffff

#define TCL_DATA_CMD_RING_ID_OFFSET                              0x0000001c
#define TCL_DATA_CMD_RING_ID_LSB                                 20
#define TCL_DATA_CMD_RING_ID_MSB                                 27
#define TCL_DATA_CMD_RING_ID_MASK                                0x0ff00000

#define TCL_DATA_CMD_LOOPING_COUNT_OFFSET                        0x0000001c
#define TCL_DATA_CMD_LOOPING_COUNT_LSB                           28
#define TCL_DATA_CMD_LOOPING_COUNT_MSB                           31
#define TCL_DATA_CMD_LOOPING_COUNT_MASK                          0xf0000000

#define NUM_OF_DWORDS_WBM_RELEASE_RING_TX 8

struct wbm_release_ring_tx {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
	 struct   dal_buffer_addr_info          released_buff_or_desc_addr_info;
	 uint32_t release_source_module                                   :  3,
			  bm_action                                               :  3,
			  buffer_or_desc_type                                     :  3,
			  first_msdu_index                                        :  4,
			  tqm_release_reason                                      :  4,
			  rbm_override_valid                                      :  1,
			  rbm_override                                            :  4,
			  reserved_2a                                             :  7,
			  cache_id                                                :  1,
			  cookie_conversion_status                                :  1,
			  wbm_internal_error                                      :  1;
	 uint32_t tqm_status_number                                       : 24,
			  transmit_count                                          :  7,
			  sw_release_details_valid                                :  1;
	 uint32_t ack_frame_rssi                                          :  8,
			  first_msdu                                              :  1,
			  last_msdu                                               :  1,
			  fw_tx_notify_frame                                      :  3,
			  buffer_timestamp                                        : 19;
	 struct   dal_tx_rate_stats_info                          tx_rate_stats;
	 uint32_t sw_peer_id                                              : 16,
			  tid                                                     :  4,
			  tqm_status_number_31_24                                 :  8,
			  looping_count                                           :  4;
#else
	 struct   dal_buffer_addr_info          released_buff_or_desc_addr_info;
	 uint32_t wbm_internal_error                                      :  1,
			  cookie_conversion_status                                :  1,
			  cache_id                                                :  1,
			  reserved_2a                                             :  7,
			  rbm_override                                            :  4,
			  rbm_override_valid                                      :  1,
			  tqm_release_reason                                      :  4,
			  first_msdu_index                                        :  4,
			  buffer_or_desc_type                                     :  3,
			  bm_action                                               :  3,
			  release_source_module                                   :  3;
	 uint32_t sw_release_details_valid                                :  1,
			  transmit_count                                          :  7,
			  tqm_status_number                                       : 24;
	 uint32_t buffer_timestamp                                        : 19,
			  fw_tx_notify_frame                                      :  3,
			  last_msdu                                               :  1,
			  first_msdu                                              :  1,
			  ack_frame_rssi                                          :  8;
	 struct   dal_tx_rate_stats_info                          tx_rate_stats;
	 uint32_t looping_count                                           :  4,
			  tqm_status_number_31_24                                 :  8,
			  tid                                                     :  4,
			  sw_peer_id                                              : 16;
#endif
};

#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET 0x00000000
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_31_0_LSB    0
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_31_0_MSB    31
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_31_0_MASK   0xffffffff

#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET 0x00000004
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_39_32_LSB   0
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_39_32_MSB   7
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_BUFFER_ADDR_39_32_MASK  0x000000ff

#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_OFFSET 0x00000004
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_LSB 8
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_MSB 11
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_MASK 0x00000f00

#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET 0x00000004
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_SW_BUFFER_COOKIE_LSB    12
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_SW_BUFFER_COOKIE_MSB    31
#define WBM_RELEASE_RING_TX_RELEASED_BUFF_OR_DESC_ADDR_INFO_SW_BUFFER_COOKIE_MASK   0xfffff000

#define WBM_RELEASE_RING_TX_RELEASE_SOURCE_MODULE_OFFSET             0x00000008
#define WBM_RELEASE_RING_TX_RELEASE_SOURCE_MODULE_LSB                0
#define WBM_RELEASE_RING_TX_RELEASE_SOURCE_MODULE_MSB                2
#define WBM_RELEASE_RING_TX_RELEASE_SOURCE_MODULE_MASK               0x00000007

#define WBM_RELEASE_RING_TX_BM_ACTION_OFFSET                         0x00000008
#define WBM_RELEASE_RING_TX_BM_ACTION_LSB                            3
#define WBM_RELEASE_RING_TX_BM_ACTION_MSB                            5
#define WBM_RELEASE_RING_TX_BM_ACTION_MASK                           0x00000038

#define WBM_RELEASE_RING_TX_BUFFER_OR_DESC_TYPE_OFFSET               0x00000008
#define WBM_RELEASE_RING_TX_BUFFER_OR_DESC_TYPE_LSB                  6
#define WBM_RELEASE_RING_TX_BUFFER_OR_DESC_TYPE_MSB                  8
#define WBM_RELEASE_RING_TX_BUFFER_OR_DESC_TYPE_MASK                 0x000001c0

#define WBM_RELEASE_RING_TX_FIRST_MSDU_INDEX_OFFSET                  0x00000008
#define WBM_RELEASE_RING_TX_FIRST_MSDU_INDEX_LSB                     9
#define WBM_RELEASE_RING_TX_FIRST_MSDU_INDEX_MSB                     12
#define WBM_RELEASE_RING_TX_FIRST_MSDU_INDEX_MASK                    0x00001e00

#define WBM_RELEASE_RING_TX_TQM_RELEASE_REASON_OFFSET                0x00000008
#define WBM_RELEASE_RING_TX_TQM_RELEASE_REASON_LSB                   13
#define WBM_RELEASE_RING_TX_TQM_RELEASE_REASON_MSB                   16
#define WBM_RELEASE_RING_TX_TQM_RELEASE_REASON_MASK                  0x0001e000

#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_VALID_OFFSET                0x00000008
#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_VALID_LSB                   17
#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_VALID_MSB                   17
#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_VALID_MASK                  0x00020000

#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_OFFSET                      0x00000008
#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_LSB                         18
#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_MSB                         21
#define WBM_RELEASE_RING_TX_RBM_OVERRIDE_MASK                        0x003c0000

#define WBM_RELEASE_RING_TX_RESERVED_2A_OFFSET                       0x00000008
#define WBM_RELEASE_RING_TX_RESERVED_2A_LSB                          22
#define WBM_RELEASE_RING_TX_RESERVED_2A_MSB                          28
#define WBM_RELEASE_RING_TX_RESERVED_2A_MASK                         0x1fc00000

#define WBM_RELEASE_RING_TX_CACHE_ID_OFFSET                          0x00000008
#define WBM_RELEASE_RING_TX_CACHE_ID_LSB                             29
#define WBM_RELEASE_RING_TX_CACHE_ID_MSB                             29
#define WBM_RELEASE_RING_TX_CACHE_ID_MASK                            0x20000000

#define WBM_RELEASE_RING_TX_COOKIE_CONVERSION_STATUS_OFFSET          0x00000008
#define WBM_RELEASE_RING_TX_COOKIE_CONVERSION_STATUS_LSB             30
#define WBM_RELEASE_RING_TX_COOKIE_CONVERSION_STATUS_MSB             30
#define WBM_RELEASE_RING_TX_COOKIE_CONVERSION_STATUS_MASK            0x40000000

#define WBM_RELEASE_RING_TX_WBM_INTERNAL_ERROR_OFFSET                0x00000008
#define WBM_RELEASE_RING_TX_WBM_INTERNAL_ERROR_LSB                   31
#define WBM_RELEASE_RING_TX_WBM_INTERNAL_ERROR_MSB                   31
#define WBM_RELEASE_RING_TX_WBM_INTERNAL_ERROR_MASK                  0x80000000

#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_OFFSET                 0x0000000c
#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_LSB                    0
#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_MSB                    23
#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_MASK                   0x00ffffff

#define WBM_RELEASE_RING_TX_TRANSMIT_COUNT_OFFSET                    0x0000000c
#define WBM_RELEASE_RING_TX_TRANSMIT_COUNT_LSB                       24
#define WBM_RELEASE_RING_TX_TRANSMIT_COUNT_MSB                       30
#define WBM_RELEASE_RING_TX_TRANSMIT_COUNT_MASK                      0x7f000000

#define WBM_RELEASE_RING_TX_SW_RELEASE_DETAILS_VALID_OFFSET          0x0000000c
#define WBM_RELEASE_RING_TX_SW_RELEASE_DETAILS_VALID_LSB             31
#define WBM_RELEASE_RING_TX_SW_RELEASE_DETAILS_VALID_MSB             31
#define WBM_RELEASE_RING_TX_SW_RELEASE_DETAILS_VALID_MASK            0x80000000

#define WBM_RELEASE_RING_TX_ACK_FRAME_RSSI_OFFSET                    0x00000010
#define WBM_RELEASE_RING_TX_ACK_FRAME_RSSI_LSB                       0
#define WBM_RELEASE_RING_TX_ACK_FRAME_RSSI_MSB                       7
#define WBM_RELEASE_RING_TX_ACK_FRAME_RSSI_MASK                      0x000000ff

#define WBM_RELEASE_RING_TX_FIRST_MSDU_OFFSET                        0x00000010
#define WBM_RELEASE_RING_TX_FIRST_MSDU_LSB                           8
#define WBM_RELEASE_RING_TX_FIRST_MSDU_MSB                           8
#define WBM_RELEASE_RING_TX_FIRST_MSDU_MASK                          0x00000100

#define WBM_RELEASE_RING_TX_LAST_MSDU_OFFSET                         0x00000010
#define WBM_RELEASE_RING_TX_LAST_MSDU_LSB                            9
#define WBM_RELEASE_RING_TX_LAST_MSDU_MSB                            9
#define WBM_RELEASE_RING_TX_LAST_MSDU_MASK                           0x00000200

#define WBM_RELEASE_RING_TX_FW_TX_NOTIFY_FRAME_OFFSET                0x00000010
#define WBM_RELEASE_RING_TX_FW_TX_NOTIFY_FRAME_LSB                   10
#define WBM_RELEASE_RING_TX_FW_TX_NOTIFY_FRAME_MSB                   12
#define WBM_RELEASE_RING_TX_FW_TX_NOTIFY_FRAME_MASK                  0x00001c00

#define WBM_RELEASE_RING_TX_BUFFER_TIMESTAMP_OFFSET                  0x00000010
#define WBM_RELEASE_RING_TX_BUFFER_TIMESTAMP_LSB                     13
#define WBM_RELEASE_RING_TX_BUFFER_TIMESTAMP_MSB                     31
#define WBM_RELEASE_RING_TX_BUFFER_TIMESTAMP_MASK                    0xffffe000

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TX_RATE_STATS_INFO_VALID_OFFSET           0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TX_RATE_STATS_INFO_VALID_LSB              0
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TX_RATE_STATS_INFO_VALID_MSB              0
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TX_RATE_STATS_INFO_VALID_MASK             0x00000001

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_BW_OFFSET          0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_BW_LSB             1
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_BW_MSB             3
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_BW_MASK            0x0000000e

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_PKT_TYPE_OFFSET    0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_PKT_TYPE_LSB       4
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_PKT_TYPE_MSB       7
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_PKT_TYPE_MASK      0x000000f0

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_STBC_OFFSET        0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_STBC_LSB           8
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_STBC_MSB           8
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_STBC_MASK          0x00000100

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_LDPC_OFFSET        0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_LDPC_LSB           9
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_LDPC_MSB           9
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_LDPC_MASK          0x00000200

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_SGI_OFFSET         0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_SGI_LSB            10
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_SGI_MSB            11
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_SGI_MASK           0x00000c00

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_MCS_OFFSET         0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_MCS_LSB            12
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_MCS_MSB            15
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_MCS_MASK           0x0000f000

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_OFDMA_TRANSMISSION_OFFSET   0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_OFDMA_TRANSMISSION_LSB      16
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_OFDMA_TRANSMISSION_MSB      16
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_OFDMA_TRANSMISSION_MASK     0x00010000

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TONES_IN_RU_OFFSET          0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TONES_IN_RU_LSB             17
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TONES_IN_RU_MSB             28
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TONES_IN_RU_MASK            0x1ffe0000

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_NSS_OFFSET         0x00000014
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_NSS_LSB            29
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_NSS_MSB            31
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_TRANSMIT_NSS_MASK           0xe0000000

#define WBM_RELEASE_RING_TX_TX_RATE_STATS_PPDU_TRANSMISSION_TSF_OFFSET 0x00000018
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_PPDU_TRANSMISSION_TSF_LSB   0
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_PPDU_TRANSMISSION_TSF_MSB   31
#define WBM_RELEASE_RING_TX_TX_RATE_STATS_PPDU_TRANSMISSION_TSF_MASK  0xffffffff

#define WBM_RELEASE_RING_TX_SW_PEER_ID_OFFSET                       0x0000001c
#define WBM_RELEASE_RING_TX_SW_PEER_ID_LSB                          0
#define WBM_RELEASE_RING_TX_SW_PEER_ID_MSB                          15
#define WBM_RELEASE_RING_TX_SW_PEER_ID_MASK                         0x0000ffff

#define WBM_RELEASE_RING_TX_TID_OFFSET                              0x0000001c
#define WBM_RELEASE_RING_TX_TID_LSB                                 16
#define WBM_RELEASE_RING_TX_TID_MSB                                 19
#define WBM_RELEASE_RING_TX_TID_MASK                                0x000f0000

#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_31_24_OFFSET           0x0000001c
#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_31_24_LSB              20
#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_31_24_MSB              27
#define WBM_RELEASE_RING_TX_TQM_STATUS_NUMBER_31_24_MASK             0x0ff00000

#define WBM_RELEASE_RING_TX_LOOPING_COUNT_OFFSET                     0x0000001c
#define WBM_RELEASE_RING_TX_LOOPING_COUNT_LSB                        28
#define WBM_RELEASE_RING_TX_LOOPING_COUNT_MSB                        31
#define WBM_RELEASE_RING_TX_LOOPING_COUNT_MASK                       0xf0000000
#define NUM_OF_DWORDS_RX_MPDU_DESC_INFO 2

struct dal_rx_mpdu_desc_info {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
	 uint32_t msdu_count                                  :  8,
			  fragment_flag                               :  1,
			  mpdu_retry_bit                              :  1,
			  ampdu_flag                                  :  1,
			  bar_frame                                   :  1,
			  pn_fields_contain_valid_info                :  1,
			  raw_mpdu                                    :  1,
			  more_fragment_flag                          :  1,
			  src_info                                    : 12,
			  mpdu_qos_control_valid                      :  1,
			  tid                                         :  4;
	 uint32_t peer_meta_data                              : 32;
#else
	 uint32_t tid                                         :  4,
			  mpdu_qos_control_valid                      :  1,
			  src_info                                    : 12,
			  more_fragment_flag                          :  1,
			  raw_mpdu                                    :  1,
			  pn_fields_contain_valid_info                :  1,
			  bar_frame                                   :  1,
			  ampdu_flag                                  :  1,
			  mpdu_retry_bit                              :  1,
			  fragment_flag                               :  1,
			  msdu_count                                  :  8;
	 uint32_t peer_meta_data                              : 32;
#endif
};

#define RX_MPDU_DESC_INFO_MSDU_COUNT_OFFSET                    0x00000000
#define RX_MPDU_DESC_INFO_MSDU_COUNT_LSB                       0
#define RX_MPDU_DESC_INFO_MSDU_COUNT_MSB                       7
#define RX_MPDU_DESC_INFO_MSDU_COUNT_MASK                      0x000000ff

#define RX_MPDU_DESC_INFO_FRAGMENT_FLAG_OFFSET                 0x00000000
#define RX_MPDU_DESC_INFO_FRAGMENT_FLAG_LSB                    8
#define RX_MPDU_DESC_INFO_FRAGMENT_FLAG_MSB                    8
#define RX_MPDU_DESC_INFO_FRAGMENT_FLAG_MASK                   0x00000100

#define RX_MPDU_DESC_INFO_MPDU_RETRY_BIT_OFFSET                0x00000000
#define RX_MPDU_DESC_INFO_MPDU_RETRY_BIT_LSB                   9
#define RX_MPDU_DESC_INFO_MPDU_RETRY_BIT_MSB                   9
#define RX_MPDU_DESC_INFO_MPDU_RETRY_BIT_MASK                  0x00000200

#define RX_MPDU_DESC_INFO_AMPDU_FLAG_OFFSET                    0x00000000
#define RX_MPDU_DESC_INFO_AMPDU_FLAG_LSB                       10
#define RX_MPDU_DESC_INFO_AMPDU_FLAG_MSB                       10
#define RX_MPDU_DESC_INFO_AMPDU_FLAG_MASK                      0x00000400

#define RX_MPDU_DESC_INFO_BAR_FRAME_OFFSET                     0x00000000
#define RX_MPDU_DESC_INFO_BAR_FRAME_LSB                        11
#define RX_MPDU_DESC_INFO_BAR_FRAME_MSB                        11
#define RX_MPDU_DESC_INFO_BAR_FRAME_MASK                       0x00000800

#define RX_MPDU_DESC_INFO_PN_FIELDS_CONTAIN_VALID_INFO_OFFSET  0x00000000
#define RX_MPDU_DESC_INFO_PN_FIELDS_CONTAIN_VALID_INFO_LSB     12
#define RX_MPDU_DESC_INFO_PN_FIELDS_CONTAIN_VALID_INFO_MSB     12
#define RX_MPDU_DESC_INFO_PN_FIELDS_CONTAIN_VALID_INFO_MASK    0x00001000

#define RX_MPDU_DESC_INFO_RAW_MPDU_OFFSET                      0x00000000
#define RX_MPDU_DESC_INFO_RAW_MPDU_LSB                         13
#define RX_MPDU_DESC_INFO_RAW_MPDU_MSB                         13
#define RX_MPDU_DESC_INFO_RAW_MPDU_MASK                        0x00002000

#define RX_MPDU_DESC_INFO_MORE_FRAGMENT_FLAG_OFFSET            0x00000000
#define RX_MPDU_DESC_INFO_MORE_FRAGMENT_FLAG_LSB               14
#define RX_MPDU_DESC_INFO_MORE_FRAGMENT_FLAG_MSB               14
#define RX_MPDU_DESC_INFO_MORE_FRAGMENT_FLAG_MASK              0x00004000

#define RX_MPDU_DESC_INFO_SRC_INFO_OFFSET                      0x00000000
#define RX_MPDU_DESC_INFO_SRC_INFO_LSB                         15
#define RX_MPDU_DESC_INFO_SRC_INFO_MSB                         26
#define RX_MPDU_DESC_INFO_SRC_INFO_MASK                        0x07ff8000

#define RX_MPDU_DESC_INFO_MPDU_QOS_CONTROL_VALID_OFFSET        0x00000000
#define RX_MPDU_DESC_INFO_MPDU_QOS_CONTROL_VALID_LSB           27
#define RX_MPDU_DESC_INFO_MPDU_QOS_CONTROL_VALID_MSB           27
#define RX_MPDU_DESC_INFO_MPDU_QOS_CONTROL_VALID_MASK          0x08000000

#define RX_MPDU_DESC_INFO_TID_OFFSET                           0x00000000
#define RX_MPDU_DESC_INFO_TID_LSB                              28
#define RX_MPDU_DESC_INFO_TID_MSB                              31
#define RX_MPDU_DESC_INFO_TID_MASK                             0xf0000000

#define RX_MPDU_DESC_INFO_PEER_META_DATA_OFFSET                0x00000004
#define RX_MPDU_DESC_INFO_PEER_META_DATA_LSB                   0
#define RX_MPDU_DESC_INFO_PEER_META_DATA_MSB                   31
#define RX_MPDU_DESC_INFO_PEER_META_DATA_MASK                  0xffffffff

#define NUM_OF_DWORDS_RX_MSDU_DESC_INFO 1

struct dal_rx_msdu_desc_info {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
			 uint32_t first_msdu_in_mpdu_flag                   :  1,
					  last_msdu_in_mpdu_flag                    :  1,
					  msdu_continuation                         :  1,
					  msdu_length                               : 14,
					  msdu_drop                                 :  1,
					  sa_is_valid                               :  1,
					  da_is_valid                               :  1,
					  da_is_mcbc                                :  1,
					  l3_header_padding_msb                     :  1,
					  tcp_udp_chksum_fail                       :  1,
					  ip_chksum_fail                            :  1,
					  fr_ds                                     :  1,
					  to_ds                                     :  1,
					  intra_bss                                 :  1,
					  dest_chip_id                              :  2,
					  decap_format                              :  2,
					  reserved_0a                               :  1;
#else
			 uint32_t reserved_0a                               :  1,
					  decap_format                              :  2,
					  dest_chip_id                              :  2,
					  intra_bss                                 :  1,
					  to_ds                                     :  1,
					  fr_ds                                     :  1,
					  ip_chksum_fail                            :  1,
					  tcp_udp_chksum_fail                       :  1,
					  l3_header_padding_msb                     :  1,
					  da_is_mcbc                                :  1,
					  da_is_valid                               :  1,
					  sa_is_valid                               :  1,
					  msdu_drop                                 :  1,
					  msdu_length                               : 14,
					  msdu_continuation                         :  1,
					  last_msdu_in_mpdu_flag                    :  1,
					  first_msdu_in_mpdu_flag                   :  1;
#endif
};

#define RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_OFFSET       0x00000000
#define RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_LSB          0
#define RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_MSB          0
#define RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_MASK         0x00000001

#define RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_OFFSET        0x00000000
#define RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_LSB           1
#define RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_MSB           1
#define RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_MASK          0x00000002

#define RX_MSDU_DESC_INFO_MSDU_CONTINUATION_OFFSET             0x00000000
#define RX_MSDU_DESC_INFO_MSDU_CONTINUATION_LSB                2
#define RX_MSDU_DESC_INFO_MSDU_CONTINUATION_MSB                2
#define RX_MSDU_DESC_INFO_MSDU_CONTINUATION_MASK               0x00000004

#define RX_MSDU_DESC_INFO_MSDU_LENGTH_OFFSET                   0x00000000
#define RX_MSDU_DESC_INFO_MSDU_LENGTH_LSB                      3
#define RX_MSDU_DESC_INFO_MSDU_LENGTH_MSB                      16
#define RX_MSDU_DESC_INFO_MSDU_LENGTH_MASK                     0x0001fff8

#define RX_MSDU_DESC_INFO_MSDU_DROP_OFFSET                     0x00000000
#define RX_MSDU_DESC_INFO_MSDU_DROP_LSB                        17
#define RX_MSDU_DESC_INFO_MSDU_DROP_MSB                        17
#define RX_MSDU_DESC_INFO_MSDU_DROP_MASK                       0x00020000

#define RX_MSDU_DESC_INFO_SA_IS_VALID_OFFSET                   0x00000000
#define RX_MSDU_DESC_INFO_SA_IS_VALID_LSB                      18
#define RX_MSDU_DESC_INFO_SA_IS_VALID_MSB                      18
#define RX_MSDU_DESC_INFO_SA_IS_VALID_MASK                     0x00040000

#define RX_MSDU_DESC_INFO_DA_IS_VALID_OFFSET                   0x00000000
#define RX_MSDU_DESC_INFO_DA_IS_VALID_LSB                      19
#define RX_MSDU_DESC_INFO_DA_IS_VALID_MSB                      19
#define RX_MSDU_DESC_INFO_DA_IS_VALID_MASK                     0x00080000

#define RX_MSDU_DESC_INFO_DA_IS_MCBC_OFFSET                    0x00000000
#define RX_MSDU_DESC_INFO_DA_IS_MCBC_LSB                       20
#define RX_MSDU_DESC_INFO_DA_IS_MCBC_MSB                       20
#define RX_MSDU_DESC_INFO_DA_IS_MCBC_MASK                      0x00100000

#define RX_MSDU_DESC_INFO_L3_HEADER_PADDING_MSB_OFFSET         0x00000000
#define RX_MSDU_DESC_INFO_L3_HEADER_PADDING_MSB_LSB            21
#define RX_MSDU_DESC_INFO_L3_HEADER_PADDING_MSB_MSB            21
#define RX_MSDU_DESC_INFO_L3_HEADER_PADDING_MSB_MASK           0x00200000

#define RX_MSDU_DESC_INFO_TCP_UDP_CHKSUM_FAIL_OFFSET           0x00000000
#define RX_MSDU_DESC_INFO_TCP_UDP_CHKSUM_FAIL_LSB              22
#define RX_MSDU_DESC_INFO_TCP_UDP_CHKSUM_FAIL_MSB              22
#define RX_MSDU_DESC_INFO_TCP_UDP_CHKSUM_FAIL_MASK             0x00400000

#define RX_MSDU_DESC_INFO_IP_CHKSUM_FAIL_OFFSET                0x00000000
#define RX_MSDU_DESC_INFO_IP_CHKSUM_FAIL_LSB                   23
#define RX_MSDU_DESC_INFO_IP_CHKSUM_FAIL_MSB                   23
#define RX_MSDU_DESC_INFO_IP_CHKSUM_FAIL_MASK                  0x00800000

#define RX_MSDU_DESC_INFO_FR_DS_OFFSET                         0x00000000
#define RX_MSDU_DESC_INFO_FR_DS_LSB                            24
#define RX_MSDU_DESC_INFO_FR_DS_MSB                            24
#define RX_MSDU_DESC_INFO_FR_DS_MASK                           0x01000000

#define RX_MSDU_DESC_INFO_TO_DS_OFFSET                         0x00000000
#define RX_MSDU_DESC_INFO_TO_DS_LSB                            25
#define RX_MSDU_DESC_INFO_TO_DS_MSB                            25
#define RX_MSDU_DESC_INFO_TO_DS_MASK                           0x02000000

#define RX_MSDU_DESC_INFO_INTRA_BSS_OFFSET                     0x00000000
#define RX_MSDU_DESC_INFO_INTRA_BSS_LSB                        26
#define RX_MSDU_DESC_INFO_INTRA_BSS_MSB                        26
#define RX_MSDU_DESC_INFO_INTRA_BSS_MASK                       0x04000000

#define RX_MSDU_DESC_INFO_DEST_CHIP_ID_OFFSET                  0x00000000
#define RX_MSDU_DESC_INFO_DEST_CHIP_ID_LSB                     27
#define RX_MSDU_DESC_INFO_DEST_CHIP_ID_MSB                     28
#define RX_MSDU_DESC_INFO_DEST_CHIP_ID_MASK                    0x18000000

#define RX_MSDU_DESC_INFO_DECAP_FORMAT_OFFSET                  0x00000000
#define RX_MSDU_DESC_INFO_DECAP_FORMAT_LSB                     29
#define RX_MSDU_DESC_INFO_DECAP_FORMAT_MSB                     30
#define RX_MSDU_DESC_INFO_DECAP_FORMAT_MASK                    0x60000000
/* REO destination ring*/
#define NUM_OF_DWORDS_REO_DESTINATION_RING 8

struct dal_reo_destination_ring {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
	 struct   dal_buffer_addr_info      buf_or_link_desc_addr_info;
	 struct   dal_rx_mpdu_desc_info     rx_mpdu_desc_info_details;
	 struct   dal_rx_msdu_desc_info     rx_msdu_desc_info_details;
	 uint32_t buffer_virt_addr_31_0              : 32;
	 uint32_t buffer_virt_addr_63_32             : 32;
	 uint32_t reo_dest_buffer_type               :  1,
			  reo_push_reason                    :  2,
			  reo_error_code                     :  5,
			  captured_msdu_data_size            :  4,
			  sw_exception                       :  1,
			  src_link_id                        :  3,
			  reo_destination_struct_signature   :  4,
			  ring_id                            :  8,
			  looping_count                      :  4;
#else
	 struct   dal_buffer_addr_info      buf_or_link_desc_addr_info;
	 struct   dal_rx_mpdu_desc_info     rx_mpdu_desc_info_details;
	 struct   dal_rx_msdu_desc_info     rx_msdu_desc_info_details;
	 uint32_t buffer_virt_addr_31_0              : 32;
	 uint32_t buffer_virt_addr_63_32             : 32;
	 uint32_t looping_count                      :  4,
			  ring_id                            :  8,
			  reo_destination_struct_signature   :  4,
			  src_link_id                        :  3,
			  sw_exception                       :  1,
			  captured_msdu_data_size            :  4,
			  reo_error_code                     :  5,
			  reo_push_reason                    :  2,
			  reo_dest_buffer_type               :  1;
#endif
};

#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET     0x00000000
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_31_0_LSB        0
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_31_0_MSB        31
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_31_0_MASK       0xffffffff

#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET    0x00000004
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_39_32_LSB       0
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_39_32_MSB       7
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_BUFFER_ADDR_39_32_MASK      0x000000ff

#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_OFFSET 0x00000004
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_LSB   8
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_MSB   11
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_RETURN_BUFFER_MANAGER_MASK  0x00000f00

#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET     0x00000004
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_SW_BUFFER_COOKIE_LSB        12
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_SW_BUFFER_COOKIE_MSB        31
#define REO_DESTINATION_RING_BUF_OR_LINK_DESC_ADDR_INFO_SW_BUFFER_COOKIE_MASK       0xfffff000

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MSDU_COUNT_OFFSET            0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MSDU_COUNT_LSB               0
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MSDU_COUNT_MSB               7
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MSDU_COUNT_MASK              0x000000ff

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_FRAGMENT_FLAG_OFFSET         0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_FRAGMENT_FLAG_LSB            8
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_FRAGMENT_FLAG_MSB            8
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_FRAGMENT_FLAG_MASK           0x00000100

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_RETRY_BIT_OFFSET        0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_RETRY_BIT_LSB           9
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_RETRY_BIT_MSB           9
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_RETRY_BIT_MASK          0x00000200

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_AMPDU_FLAG_OFFSET            0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_AMPDU_FLAG_LSB               10
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_AMPDU_FLAG_MSB               10
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_AMPDU_FLAG_MASK              0x00000400

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_BAR_FRAME_OFFSET             0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_BAR_FRAME_LSB                11
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_BAR_FRAME_MSB                11
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_BAR_FRAME_MASK               0x00000800

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PN_FIELDS_CONTAIN_VALID_INFO_OFFSET 0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PN_FIELDS_CONTAIN_VALID_INFO_LSB 12
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PN_FIELDS_CONTAIN_VALID_INFO_MSB 12
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PN_FIELDS_CONTAIN_VALID_INFO_MASK 0x00001000

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_RAW_MPDU_OFFSET              0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_RAW_MPDU_LSB                 13
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_RAW_MPDU_MSB                 13
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_RAW_MPDU_MASK                0x00002000

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MORE_FRAGMENT_FLAG_OFFSET    0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MORE_FRAGMENT_FLAG_LSB       14
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MORE_FRAGMENT_FLAG_MSB       14
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MORE_FRAGMENT_FLAG_MASK      0x00004000

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_SRC_INFO_OFFSET              0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_SRC_INFO_LSB                 15
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_SRC_INFO_MSB                 26
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_SRC_INFO_MASK                0x07ff8000

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_QOS_CONTROL_VALID_OFFSET 0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_QOS_CONTROL_VALID_LSB   27
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_QOS_CONTROL_VALID_MSB   27
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_MPDU_QOS_CONTROL_VALID_MASK  0x08000000

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_TID_OFFSET                   0x00000008
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_TID_LSB                      28
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_TID_MSB                      31
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_TID_MASK                     0xf0000000

#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PEER_META_DATA_OFFSET        0x0000000c
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PEER_META_DATA_LSB           0
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PEER_META_DATA_MSB           31
#define REO_DESTINATION_RING_RX_MPDU_DESC_INFO_DETAILS_PEER_META_DATA_MASK          0xffffffff

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FIRST_MSDU_IN_MPDU_FLAG_OFFSET 0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FIRST_MSDU_IN_MPDU_FLAG_LSB  0
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FIRST_MSDU_IN_MPDU_FLAG_MSB  0
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FIRST_MSDU_IN_MPDU_FLAG_MASK 0x00000001

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_LAST_MSDU_IN_MPDU_FLAG_OFFSET 0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_LAST_MSDU_IN_MPDU_FLAG_LSB   1
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_LAST_MSDU_IN_MPDU_FLAG_MSB   1
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_LAST_MSDU_IN_MPDU_FLAG_MASK  0x00000002

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_CONTINUATION_OFFSET     0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_CONTINUATION_LSB        2
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_CONTINUATION_MSB        2
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_CONTINUATION_MASK       0x00000004

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_LENGTH_OFFSET           0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_LENGTH_LSB              3
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_LENGTH_MSB              16
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_LENGTH_MASK             0x0001fff8

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_DROP_OFFSET             0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_DROP_LSB                17
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_DROP_MSB                17
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_MSDU_DROP_MASK               0x00020000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_SA_IS_VALID_OFFSET           0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_SA_IS_VALID_LSB              18
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_SA_IS_VALID_MSB              18
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_SA_IS_VALID_MASK             0x00040000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_VALID_OFFSET           0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_VALID_LSB              19
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_VALID_MSB              19
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_VALID_MASK             0x00080000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_MCBC_OFFSET            0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_MCBC_LSB               20
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_MCBC_MSB               20
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DA_IS_MCBC_MASK              0x00100000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_L3_HEADER_PADDING_MSB_OFFSET 0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_L3_HEADER_PADDING_MSB_LSB    21
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_L3_HEADER_PADDING_MSB_MSB    21
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_L3_HEADER_PADDING_MSB_MASK   0x00200000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TCP_UDP_CHKSUM_FAIL_OFFSET   0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TCP_UDP_CHKSUM_FAIL_LSB      22
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TCP_UDP_CHKSUM_FAIL_MSB      22
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TCP_UDP_CHKSUM_FAIL_MASK     0x00400000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_IP_CHKSUM_FAIL_OFFSET        0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_IP_CHKSUM_FAIL_LSB           23
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_IP_CHKSUM_FAIL_MSB           23
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_IP_CHKSUM_FAIL_MASK          0x00800000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FR_DS_OFFSET                 0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FR_DS_LSB                    24
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FR_DS_MSB                    24
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_FR_DS_MASK                   0x01000000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TO_DS_OFFSET                 0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TO_DS_LSB                    25
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TO_DS_MSB                    25
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_TO_DS_MASK                   0x02000000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_INTRA_BSS_OFFSET             0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_INTRA_BSS_LSB                26
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_INTRA_BSS_MSB                26
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_INTRA_BSS_MASK               0x04000000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DEST_CHIP_ID_OFFSET          0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DEST_CHIP_ID_LSB             27
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DEST_CHIP_ID_MSB             28
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DEST_CHIP_ID_MASK            0x18000000

#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DECAP_FORMAT_OFFSET          0x00000010
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DECAP_FORMAT_LSB             29
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DECAP_FORMAT_MSB             30
#define REO_DESTINATION_RING_RX_MSDU_DESC_INFO_DETAILS_DECAP_FORMAT_MASK            0x60000000

#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_31_0_OFFSET                           0x00000014
#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_31_0_LSB                              0
#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_31_0_MSB                              31
#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_31_0_MASK                             0xffffffff

#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_63_32_OFFSET                          0x00000018
#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_63_32_LSB                             0
#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_63_32_MSB                             31
#define REO_DESTINATION_RING_BUFFER_VIRT_ADDR_63_32_MASK                            0xffffffff

#define REO_DESTINATION_RING_REO_DEST_BUFFER_TYPE_OFFSET                            0x0000001c
#define REO_DESTINATION_RING_REO_DEST_BUFFER_TYPE_LSB                               0
#define REO_DESTINATION_RING_REO_DEST_BUFFER_TYPE_MSB                               0
#define REO_DESTINATION_RING_REO_DEST_BUFFER_TYPE_MASK                              0x00000001

#define REO_DESTINATION_RING_REO_PUSH_REASON_OFFSET                                 0x0000001c
#define REO_DESTINATION_RING_REO_PUSH_REASON_LSB                                    1
#define REO_DESTINATION_RING_REO_PUSH_REASON_MSB                                    2
#define REO_DESTINATION_RING_REO_PUSH_REASON_MASK                                   0x00000006

#define REO_DESTINATION_RING_REO_ERROR_CODE_OFFSET                                  0x0000001c
#define REO_DESTINATION_RING_REO_ERROR_CODE_LSB                                     3
#define REO_DESTINATION_RING_REO_ERROR_CODE_MSB                                     7
#define REO_DESTINATION_RING_REO_ERROR_CODE_MASK                                    0x000000f8

#define REO_DESTINATION_RING_CAPTURED_MSDU_DATA_SIZE_OFFSET                         0x0000001c
#define REO_DESTINATION_RING_CAPTURED_MSDU_DATA_SIZE_LSB                            8
#define REO_DESTINATION_RING_CAPTURED_MSDU_DATA_SIZE_MSB                            11
#define REO_DESTINATION_RING_CAPTURED_MSDU_DATA_SIZE_MASK                           0x00000f00

#define REO_DESTINATION_RING_SW_EXCEPTION_OFFSET                                    0x0000001c
#define REO_DESTINATION_RING_SW_EXCEPTION_LSB                                       12
#define REO_DESTINATION_RING_SW_EXCEPTION_MSB                                       12
#define REO_DESTINATION_RING_SW_EXCEPTION_MASK                                      0x00001000

#define REO_DESTINATION_RING_SRC_LINK_ID_OFFSET                                     0x0000001c
#define REO_DESTINATION_RING_SRC_LINK_ID_LSB                                        13
#define REO_DESTINATION_RING_SRC_LINK_ID_MSB                                        15
#define REO_DESTINATION_RING_SRC_LINK_ID_MASK                                       0x0000e000

#define REO_DESTINATION_RING_REO_DESTINATION_STRUCT_SIGNATURE_OFFSET                0x0000001c
#define REO_DESTINATION_RING_REO_DESTINATION_STRUCT_SIGNATURE_LSB                   16
#define REO_DESTINATION_RING_REO_DESTINATION_STRUCT_SIGNATURE_MSB                   19
#define REO_DESTINATION_RING_REO_DESTINATION_STRUCT_SIGNATURE_MASK                  0x000f0000

#define REO_DESTINATION_RING_RING_ID_OFFSET                                         0x0000001c
#define REO_DESTINATION_RING_RING_ID_LSB                                            20
#define REO_DESTINATION_RING_RING_ID_MSB                                            27
#define REO_DESTINATION_RING_RING_ID_MASK                                           0x0ff00000

#define REO_DESTINATION_RING_LOOPING_COUNT_OFFSET                                   0x0000001c
#define REO_DESTINATION_RING_LOOPING_COUNT_LSB                                      28
#define REO_DESTINATION_RING_LOOPING_COUNT_MSB                                      31
#define REO_DESTINATION_RING_LOOPING_COUNT_MASK                                     0xf0000000

/* 7 qwords for rx_mpdu_start without tag */
#define MPDU_START_WMASK 0x07B8

#ifdef DRIVER_PASSTHRU_MODE
/* 9 qwords for rx_msdu_end without tag */
#define MSDU_END_WMASK 0x155CA
#else
/* 8 qwords for rx_msdu_end without tag */
#define MSDU_END_WMASK 0x115CA
#endif


#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
struct dal_rx_msdu_end_compact {
	/* qword-0 */
			 uint32_t rxpcu_mpdu_filter_in_category        :  2,
					  sw_frame_group_id                    :  7,
					  reserved_0                           :  7,
					  phy_ppdu_id                          : 16;
			 uint32_t ip_hdr_chksum                        : 16,
					  reported_mpdu_length                 : 14,
					  reserved_1a                          :  2;
	/* qword-2 */
			 uint32_t ipv6_options_crc;
			 uint32_t da_offset                            :  6,
					  sa_offset                            :  6,
					  da_offset_valid                      :  1,
					  sa_offset_valid                      :  1,
					  reserved_5a                          :  2,
					  l3_type                              : 16;
	/* qword-5 */
			 uint32_t sa_sw_peer_id                        : 16,
					  sa_idx_timeout                       :  1,
					  da_idx_timeout                       :  1,
					  to_ds                                :  1,
					  tid                                  :  4,
					  sa_is_valid                          :  1,
					  da_is_valid                          :  1,
					  da_is_mcbc                           :  1,
					  l3_header_padding                    :  2,
					  first_msdu                           :  1,
					  last_msdu                            :  1,
					  fr_ds                                :  1,
					  ip_chksum_fail_copy                  :  1;
			 uint32_t sa_idx                               : 16,
					  da_idx_or_sw_peer_id                 : 16;
	/* qword-6 */
			 uint32_t msdu_drop                            :  1,
					  reo_destination_indication           :  5,
					  flow_idx                             : 20,
					  use_ppe                              :  1,
					  __reserved_g_0003                    :  2,
					  vlan_ctag_stripped                   :  1,
					  vlan_stag_stripped                   :  1,
					  fragment_flag                        :  1;
			 uint32_t fse_metadata                         : 32;
	/* qword-7 */
			 uint32_t cce_metadata                         : 16,
					  tcp_udp_chksum                       : 16;
			 uint32_t aggregation_count                    :  8,
					  flow_aggregation_continuation        :  1,
					  fisa_timeout                         :  1,
					  tcp_udp_chksum_fail_copy             :  1,
					  msdu_limit_error                     :  1,
					  flow_idx_timeout                     :  1,
					  flow_idx_invalid                     :  1,
					  cce_match                            :  1,
					  amsdu_parser_error                   :  1,
					  cumulative_ip_length                 : 16;
	/* qword-9 */
			 uint32_t msdu_length                          : 14,
					  stbc                                 :  1,
					  ipsec_esp                            :  1,
					  l3_offset                            :  7,
					  ipsec_ah                             :  1,
					  l4_offset                            :  8;
			 uint32_t msdu_number                          :  8,
					  decap_format                         :  2,
					  ipv4_proto                           :  1,
					  ipv6_proto                           :  1,
					  tcp_proto                            :  1,
					  udp_proto                            :  1,
					  ip_frag                              :  1,
					  tcp_only_ack                         :  1,
					  da_is_bcast_mcast                    :  1,
					  toeplitz_hash_sel                    :  2,
					  ip_fixed_header_valid                :  1,
					  ip_extn_header_valid                 :  1,
					  tcp_udp_header_valid                 :  1,
					  mesh_control_present                 :  1,
					  ldpc                                 :  1,
					  ip4_protocol_ip6_next_header         :  8;
	/* qword-11 */
			 uint32_t user_rssi                            :  8,
					  pkt_type                             :  4,
					  sgi                                  :  2,
					  rate_mcs                             :  4,
					  receive_bandwidth                    :  3,
					  reception_type                       :  3,
					  mimo_ss_bitmap                       :  7,
					  msdu_done_copy                       :  1;
			 uint32_t flow_id_toeplitz                     : 32;
	/* qword-13 */
             uint32_t ppdu_start_timestamp_31_0                               : 32;
             uint32_t toeplitz_hash_2_or_4                                    : 32;
	/* qword-15 */
			 uint32_t first_mpdu                           :  1,
					  reserved_30a                         :  1,
					  mcast_bcast                          :  1,
					  ast_index_not_found                  :  1,
					  ast_index_timeout                    :  1,
					  power_mgmt                           :  1,
					  non_qos                              :  1,
					  null_data                            :  1,
					  mgmt_type                            :  1,
					  ctrl_type                            :  1,
					  more_data                            :  1,
					  eosp                                 :  1,
					  a_msdu_error                         :  1,
					  reserved_30b                         :  1,
					  order                                :  1,
					  wifi_parser_error                    :  1,
					  overflow_err                         :  1,
					  msdu_length_err                      :  1,
					  tcp_udp_chksum_fail                  :  1,
					  ip_chksum_fail                       :  1,
					  sa_idx_invalid                       :  1,
					  da_idx_invalid                       :  1,
					  amsdu_addr_mismatch                  :  1,
					  rx_in_tx_decrypt_byp                 :  1,
					  encrypt_required                     :  1,
					  directed                             :  1,
					  buffer_fragment                      :  1,
					  mpdu_length_err                      :  1,
					  tkip_mic_err                         :  1,
					  decrypt_err                          :  1,
					  unencrypted_frame_err                :  1,
					  fcs_err                              :  1;
			 uint32_t reserved_31a                         : 10,
					  decrypt_status_code                  :  3,
					  rx_bitmap_not_updated                :  1,
					  reserved_31b                         : 17,
					  msdu_done                            :  1;
};

struct dal_rx_mpdu_start_compact {
	/* qword-2 */
			 uint32_t pn_31_0                              : 32;
			 uint32_t pn_63_32                             : 32;
	/* qword-3 */
			 uint32_t pn_95_64                             : 32;
			 uint32_t pn_127_96                            : 32;
	/* qword-4 */
			 uint32_t mpdu_frame_control_valid             :  1,
					  mpdu_duration_valid                  :  1,
					  mac_addr_ad1_valid                   :  1,
					  mac_addr_ad2_valid                   :  1,
					  mac_addr_ad3_valid                   :  1,
					  mac_addr_ad4_valid                   :  1,
					  mpdu_sequence_control_valid          :  1,
					  mpdu_qos_control_valid               :  1,
					  mpdu_ht_control_valid                :  1,
					  frame_encryption_info_valid          :  1,
					  mpdu_fragment_number                 :  4,
					  more_fragment_flag                   :  1,
					  reserved_11a                         :  1,
					  fr_ds                                :  1,
					  to_ds                                :  1,
					  encrypted                            :  1,
					  mpdu_retry                           :  1,
					  mpdu_sequence_number                 : 12;
			 uint32_t peer_meta_data                       : 32;
	/* qword-6 */
			 uint32_t key_id_octet                         :  8,
					  new_peer_entry                       :  1,
					  decrypt_needed                       :  1,
					  decap_type                           :  2,
					  rx_insert_vlan_c_tag_padding         :  1,
					  rx_insert_vlan_s_tag_padding         :  1,
					  strip_vlan_c_tag_decap               :  1,
					  strip_vlan_s_tag_decap               :  1,
					  pre_delim_count                      : 12,
					  ampdu_flag                           :  1,
					  bar_frame                            :  1,
					  raw_mpdu                             :  1,
					  reserved_12                          :  1;
			 uint32_t mpdu_length                          : 14,
					  first_mpdu                           :  1,
					  mcast_bcast                          :  1,
					  ast_index_not_found                  :  1,
					  ast_index_timeout                    :  1,
					  power_mgmt                           :  1,
					  non_qos                              :  1,
					  null_data                            :  1,
					  mgmt_type                            :  1,
					  ctrl_type                            :  1,
					  more_data                            :  1,
					  eosp                                 :  1,
					  fragment_flag                        :  1,
					  order                                :  1,
					  u_apsd_trigger                       :  1,
					  encrypt_required                     :  1,
					  directed                             :  1,
					  amsdu_present                        :  1,
					  reserved_13                          :  1;
	/* qword-7 */
			 uint32_t mpdu_frame_control_field             : 16,
					  mpdu_duration_field                  : 16;
			 uint32_t mac_addr_ad1_31_0                    : 32;
	/* qword-8 */
			 uint32_t mac_addr_ad1_47_32                   : 16,
					  mac_addr_ad2_15_0                    : 16;
			 uint32_t mac_addr_ad2_47_16                   : 32;
	/* qword-9 */
			 uint32_t mac_addr_ad3_31_0                    : 32;
			 uint32_t mac_addr_ad3_47_32                   : 16,
					  mpdu_sequence_control_field          : 16;
};
#else
struct dal_rx_msdu_end_compact {
	/* qword-0 */
			 uint32_t phy_ppdu_id                          : 16,
					  reserved_0                           :  7,
					  sw_frame_group_id                    :  7,
					  rxpcu_mpdu_filter_in_category        :  2;
			 uint32_t reserved_1a                          :  2,
					  reported_mpdu_length                 : 14,
					  ip_hdr_chksum                        : 16;
	/* qword-2 */
			 uint32_t ipv6_options_crc;
			 uint32_t l3_type                              : 16,
					  reserved_5a                          :  2,
					  sa_offset_valid                      :  1,
					  da_offset_valid                      :  1,
					  sa_offset                            :  6,
					  da_offset                            :  6;
	/* qword-5 */
			 uint32_t ip_chksum_fail_copy                  :  1,
					  fr_ds                                :  1,
					  last_msdu                            :  1,
					  first_msdu                           :  1,
					  l3_header_padding                    :  2,
					  da_is_mcbc                           :  1,
					  da_is_valid                          :  1,
					  sa_is_valid                          :  1,
					  tid                                  :  4,
					  to_ds                                :  1,
					  da_idx_timeout                       :  1,
					  sa_idx_timeout                       :  1,
					  sa_sw_peer_id                        : 16;
			 uint32_t da_idx_or_sw_peer_id                 : 16,
					  sa_idx                               : 16;
	/* qword-6 */
			 uint32_t fragment_flag                        :  1,
					  vlan_stag_stripped                   :  1,
					  vlan_ctag_stripped                   :  1,
					  __reserved_g_0003                    :  2,
					  use_ppe                              :  1,
					  flow_idx                             : 20,
					  reo_destination_indication           :  5,
					  msdu_drop                            :  1;
			 uint32_t fse_metadata                         : 32;
	/* qword-7 */
			 uint32_t tcp_udp_chksum                       : 16,
					  cce_metadata                         : 16;
			 uint32_t cumulative_ip_length                 : 16,
					  amsdu_parser_error                   :  1,
					  cce_match                            :  1,
					  flow_idx_invalid                     :  1,
					  flow_idx_timeout                     :  1,
					  msdu_limit_error                     :  1,
					  tcp_udp_chksum_fail_copy             :  1,
					  fisa_timeout                         :  1,
					  flow_aggregation_continuation        :  1,
					  aggregation_count                    :  8;
	/* qword-9 */
			 uint32_t l4_offset                            :  8,
					  ipsec_ah                             :  1,
					  l3_offset                            :  7,
					  ipsec_esp                            :  1,
					  stbc                                 :  1,
					  msdu_length                          : 14;
			 uint32_t ip4_protocol_ip6_next_header         :  8,
					  ldpc                                 :  1,
					  mesh_control_present                 :  1,
					  tcp_udp_header_valid                 :  1,
					  ip_extn_header_valid                 :  1,
					  ip_fixed_header_valid                :  1,
					  toeplitz_hash_sel                    :  2,
					  da_is_bcast_mcast                    :  1,
					  tcp_only_ack                         :  1,
					  ip_frag                              :  1,
					  udp_proto                            :  1,
					  tcp_proto                            :  1,
					  ipv6_proto                           :  1,
					  ipv4_proto                           :  1,
					  decap_format                         :  2,
					  msdu_number                          :  8;
	/* qword-11 */
			 uint32_t msdu_done_copy                       :  1,
					  mimo_ss_bitmap                       :  7,
					  reception_type                       :  3,
					  receive_bandwidth                    :  3,
					  rate_mcs                             :  4,
					  sgi                                  :  2,
					  pkt_type                             :  4,
					  user_rssi                            :  8;
			 uint32_t flow_id_toeplitz                     : 32;
	/* qword-15 */
			 uint32_t fcs_err                              :  1,
					  unencrypted_frame_err                :  1,
					  decrypt_err                          :  1,
					  tkip_mic_err                         :  1,
					  mpdu_length_err                      :  1,
					  buffer_fragment                      :  1,
					  directed                             :  1,
					  encrypt_required                     :  1,
					  rx_in_tx_decrypt_byp                 :  1,
					  amsdu_addr_mismatch                  :  1,
					  da_idx_invalid                       :  1,
					  sa_idx_invalid                       :  1,
					  ip_chksum_fail                       :  1,
					  tcp_udp_chksum_fail                  :  1,
					  msdu_length_err                      :  1,
					  overflow_err                         :  1,
					  wifi_parser_error                    :  1,
					  order                                :  1,
					  reserved_30b                         :  1,
					  a_msdu_error                         :  1,
					  eosp                                 :  1,
					  more_data                            :  1,
					  ctrl_type                            :  1,
					  mgmt_type                            :  1,
					  null_data                            :  1,
					  non_qos                              :  1,
					  power_mgmt                           :  1,
					  ast_index_timeout                    :  1,
					  ast_index_not_found                  :  1,
					  mcast_bcast                          :  1,
					  reserved_30a                         :  1,
					  first_mpdu                           :  1;
			 uint32_t msdu_done                            :  1,
					  reserved_31b                         : 17,
					  rx_bitmap_not_updated                :  1,
					  decrypt_status_code                  :  3,
					  reserved_31a                         : 10;
};

struct dal_rx_mpdu_start_compact {
	/* qword-2 */
			 uint32_t pn_31_0                              : 32;
			 uint32_t pn_63_32                             : 32;
	/* qword-3 */
			 uint32_t pn_95_64                             : 32;
			 uint32_t pn_127_96                            : 32;
	/* qword-4 */
			 uint32_t mpdu_sequence_number                 : 12,
					  mpdu_retry                           :  1,
					  encrypted                            :  1,
					  to_ds                                :  1,
					  fr_ds                                :  1,
					  reserved_11a                         :  1,
					  more_fragment_flag                   :  1,
					  mpdu_fragment_number                 :  4,
					  frame_encryption_info_valid          :  1,
					  mpdu_ht_control_valid                :  1,
					  mpdu_qos_control_valid               :  1,
					  mpdu_sequence_control_valid          :  1,
					  mac_addr_ad4_valid                   :  1,
					  mac_addr_ad3_valid                   :  1,
					  mac_addr_ad2_valid                   :  1,
					  mac_addr_ad1_valid                   :  1,
					  mpdu_duration_valid                  :  1,
					  mpdu_frame_control_valid             :  1;
			 uint32_t peer_meta_data                       : 32;
	/* qword-6 */
			 uint32_t reserved_12                          :  1,
					  raw_mpdu                             :  1,
					  bar_frame                            :  1,
					  ampdu_flag                           :  1,
					  pre_delim_count                      : 12,
					  strip_vlan_s_tag_decap               :  1,
					  strip_vlan_c_tag_decap               :  1,
					  rx_insert_vlan_s_tag_padding         :  1,
					  rx_insert_vlan_c_tag_padding         :  1,
					  decap_type                           :  2,
					  decrypt_needed                       :  1,
					  new_peer_entry                       :  1,
					  key_id_octet                         :  8;
			 uint32_t reserved_13                          :  1,
					  amsdu_present                        :  1,
					  directed                             :  1,
					  encrypt_required                     :  1,
					  u_apsd_trigger                       :  1,
					  order                                :  1,
					  fragment_flag                        :  1,
					  eosp                                 :  1,
					  more_data                            :  1,
					  ctrl_type                            :  1,
					  mgmt_type                            :  1,
					  null_data                            :  1,
					  non_qos                              :  1,
					  power_mgmt                           :  1,
					  ast_index_timeout                    :  1,
					  ast_index_not_found                  :  1,
					  mcast_bcast                          :  1,
					  first_mpdu                           :  1,
					  mpdu_length                          : 14;
	/* qword-7 */
			 uint32_t mpdu_duration_field                  : 16,
					  mpdu_frame_control_field             : 16;
			 uint32_t mac_addr_ad1_31_0                    : 32;
	/* qword-8 */
			 uint32_t mac_addr_ad2_15_0                    : 16,
					  mac_addr_ad1_47_32                   : 16;
			 uint32_t mac_addr_ad2_47_16                   : 32;
	/* qword-9 */
			 uint32_t mac_addr_ad3_31_0                    : 32;
			 uint32_t mpdu_sequence_control_field          : 16,
					  mac_addr_ad3_47_32                   : 16;
};
#endif /* WIFI_BIT_ORDER_BIG_ENDIAN */

typedef struct dal_rx_mpdu_start_compact dal_vndr_hal_rx_mpdu_start_t;
typedef struct dal_rx_msdu_end_compact dal_vndr_hal_rx_msdu_end_t;

struct dal_rx_mpdu_start_tlv {
	dal_vndr_hal_rx_mpdu_start_t rx_mpdu_start;
};

struct dal_rx_msdu_end_tlv {
	dal_vndr_hal_rx_msdu_end_t rx_msdu_end;
};

struct dal_rx_pkt_tlvs {
	struct dal_rx_msdu_end_tlv   msdu_end_tlv;		/*  80 bytes */
	struct dal_rx_mpdu_start_tlv mpdu_start_tlv;	/*  48 bytes */
};

#define DAL_VNDR_HAL_RX_BUF_COOKIE_GET(buff_addr_info)			\
	(DAL_VNDR_HAL_MS((*DAL_VNDR_OFFSET_TO_WORD_PTR(buff_addr_info,	\
		BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET)),	\
		BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_MASK,	\
		BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_LSB))

#define DAL_VNDR_HAL_RX_REO_BUF_COOKIE_GET(reo_desc)	\
	(DAL_VNDR_HAL_RX_BUF_COOKIE_GET(&		\
	(((struct dal_reo_destination_ring *)	\
		reo_desc)->buf_or_link_desc_addr_info)))

#define DAL_VNDR_HAL_RX_MSDU_CONTINUATION_FLAG_GET(msdu_info_ptr)	\
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		RX_MSDU_DESC_INFO_MSDU_CONTINUATION_OFFSET)) & \
		RX_MSDU_DESC_INFO_MSDU_CONTINUATION_MASK)

#define DAL_VNDR_HAL_RX_BUF_RBM_GET(buff_addr_info)			\
	(DAL_VNDR_HAL_MS((*DAL_VNDR_OFFSET_TO_WORD_PTR(buff_addr_info,	\
		BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_OFFSET)),\
		BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_MASK,	\
		BUFFER_ADDR_INFO_RETURN_BUFFER_MANAGER_LSB))

#define DAL_VNDR_HAL_RX_ERROR_STATUS_GET(reo_desc)			\
	(DAL_VNDR_HAL_MS((*DAL_VNDR_OFFSET_TO_WORD_PTR(reo_desc,	\
		REO_DESTINATION_RING_REO_PUSH_REASON_OFFSET)),\
		REO_DESTINATION_RING_REO_PUSH_REASON_MASK,	\
		REO_DESTINATION_RING_REO_PUSH_REASON_LSB))

#define DAL_VNDR_HAL_RX_BUFFER_ADDR_31_0_GET(buff_addr_info)	\
	(DAL_VNDR_HAL_MS((*DAL_VNDR_OFFSET_TO_WORD_PTR(buff_addr_info,	\
		DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET)), \
		DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_MASK,	\
		DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_LSB))

#define DAL_VNDR_HAL_RX_BUFFER_ADDR_39_32_GET(buff_addr_info)		\
	(DAL_VNDR_HAL_MS((*DAL_VNDR_OFFSET_TO_WORD_PTR(buff_addr_info,	\
		DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET)), \
		DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_MASK,	\
		DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_LSB))

#define DAL_VNDR_HAL_RX_MPDU_START(_rx_pkt_tlv)	\
	(((struct dal_rx_pkt_tlvs *)_rx_pkt_tlv)->mpdu_start_tlv.	\
			rx_mpdu_start)

#define DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv)		\
	(((struct dal_rx_pkt_tlvs *)_rx_pkt_tlv)->msdu_end_tlv.rx_msdu_end)

#define DAL_VNDR_HAL_RX_MPDU_SEQUENCE_NUMBER_GET(_rx_pkt_tlv)	\
	DAL_VNDR_HAL_RX_MPDU_START(_rx_pkt_tlv).mpdu_sequence_number

#define DAL_VNDR_HAL_RX_TLV_RATE_MCS_GET(_rx_pkt_tlv)	\
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).rate_mcs

#define DAL_VNDR_HAL_RX_TLV_SGI_GET(_rx_pkt_tlv)	\
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).sgi

#define DAL_VNDR_HAL_RX_TLV_PKT_TYPE_GET(_rx_pkt_tlv)	\
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).pkt_type

#define DAL_VNDR_HAL_RX_TLV_BW_GET(_rx_pkt_tlv)     \
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).receive_bandwidth

#define DAL_VNDR_HAL_RX_TLV_DA_IS_MCBC_GET(_rx_pkt_tlv)	\
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).da_is_mcbc

#define DAL_VNDR_HAL_RX_TLV_L3_HEADER_PADDING_GET(_rx_pkt_tlv)	\
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).l3_header_padding

#define DAL_VNDR_HAL_RX_TLV_MIMO_SS_BITMAP(_rx_pkt_tlv)\
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).mimo_ss_bitmap

#define DAL_VNDR_HAL_RX_TLV_MSDU_DONE_GET(_rx_pkt_tlv)	\
	DAL_VNDR_HAL_RX_MSDU_END(_rx_pkt_tlv).msdu_done

#define DAL_VNDR_HAL_RX_REO_BUFFER_ADDR_39_32_GET(reo_desc)	\
	(DAL_VNDR_HAL_RX_BUFFER_ADDR_39_32_GET(&			\
	(((struct dal_reo_destination_ring *)		\
		reo_desc)->buf_or_link_desc_addr_info)))

#define DAL_VNDR_HAL_RX_REO_BUFFER_ADDR_31_0_GET(reo_desc)	\
	(DAL_VNDR_HAL_RX_BUFFER_ADDR_31_0_GET(&			\
	(((struct dal_reo_destination_ring *)		\
		reo_desc)->buf_or_link_desc_addr_info)))
#endif /* _DAL_VNDR_HAL_DEFINES_BE_H_ */
