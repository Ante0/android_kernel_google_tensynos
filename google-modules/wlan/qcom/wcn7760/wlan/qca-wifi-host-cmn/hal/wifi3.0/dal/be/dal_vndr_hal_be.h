/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _DAL_VNDR_HAL_BE_H_
#define _DAL_VNDR_HAL_BE_H_
#include <linux/types.h>
#include "dal_vndr_hal_internal.h"
#include "dal_vndr_hal_defines_be.h"

/* See  dal_vndr_hal_defines_be.h fo reference
 * DAL_VNDR_HAL_TX_DESC_LEN_DWORDS is NUM_OF_DWORDS_TCL_DATA_CMD
 * DAL_VNDR_HAL_TX_DESC_LEN_BYTES is 4*NUM_OF_DWORDS_TCL_DATA_CMD
 */
#define DAL_VNDR_HAL_TX_DESC_LEN_DWORDS (8)
#define DAL_VNDR_HAL_TX_DESC_LEN_BYTES  (DAL_VNDR_HAL_TX_DESC_LEN_DWORDS * 4)
#define DAL_VNDR_HAL_TX_COMP_DESC_LEN_DWORDS (8)
#define DAL_VNDR_HAL_TX_COMP_DESC_LEN_BYTES \
	(DAL_VNDR_HAL_TX_COMP_DESC_LEN_DWORDS * 4)

/**
 * dal_vndr_hal_default_ops_attach_be() - Add default ops for BE chips
 * @soc: hal_soc handle
 *
 * Return: None
 */
void dal_vndr_hal_default_ops_attach_be(struct dal_vndr_hal_soc *soc);

/**
 * struct dal_vndr_hal_tx_completion_status - HAL Tx completion descriptor
 * contents. For different architecture/target it can vary.
 * The fields of this struct are aligned to WBM2SW TX comp Desc to populate
 * them efficiently. Do not add/removed the fields of the struct.
 * @reserved_va1: reserved for VA
 * @reserved_va2: reserved for VA
 * @release_src: release source = TQM/FW
 * @reserved1: reserved
 * @status: frame acked/failed
 * @reserved2: reserved
 * @ppdu_id: TSF, snapshot of this value when transmission of the
 *           PPDU containing the frame finished.
 * @transmit_cnt: Number of times this frame has been transmitted
 * @reserved3: reserved
 * @ack_frame_rssi: RSSI of the received ACK or BA frame
 * @first_msdu: Indicates this MSDU is the first MSDU in AMSDU
 * @last_msdu: Indicates this MSDU is the last MSDU in AMSDU
 * @msdu_part_of_amsdu : Indicates this MSDU was part of an A-MSDU in MPDU
 * @reserved4: reserved
 * @buffer_timestamp: Frame system entrance timestamp in units of 1024
 *		      microseconds
 * @valid:
 * @bw: Indicates the BW of the upcoming transmission -
 *       <enum 0 transmit_bw_20_MHz>
 *       <enum 1 transmit_bw_40_MHz>
 *       <enum 2 transmit_bw_80_MHz>
 *       <enum 3 transmit_bw_160_MHz>
 *       <enum 4 transmit_bw_320_MHz>
 *       <enum 5 transmit_bw_240_MHz>
 * @pkt_type: Transmit Packet Type
 * @stbc: When set, STBC transmission rate was used
 * @ldpc: When set, use LDPC transmission rates
 * @sgi: <enum 0     0_8_us_sgi > Legacy normal GI
 *       <enum 1     0_4_us_sgi > Legacy short GI
 *       <enum 2     1_6_us_sgi > HE related GI
 *       <enum 3     3_2_us_sgi > HE
 * @mcs: Transmit MCS Rate
 * @ofdma: Set when the transmission was an OFDMA transmission
 * @reserved_bn: reserved
 * @tones_in_ru: The number of tones in the RU used.
 * @reserved5: reserved
 * @tsf: Lower 32 bits of the TSF
 * @peer_id: Peer ID of the flow or MPDU queue
 * @tid: TID of the flow or MPDU queue
 * @reserved6: reserved
 */
struct dal_vndr_hal_tx_completion_status {
	uint32_t reserved_va1;
	uint32_t reserved_va2;
	uint32_t release_src:3,
		 reserved1:10,
		 status:4,
		 reserved2:15;
	uint32_t ppdu_id:24,
		 transmit_cnt:7,
		 reserved3:1;
	uint32_t ack_frame_rssi:8,
		 first_msdu:1,
		 last_msdu:1,
		 msdu_part_of_amsdu:1,
		 reserved4:2,
		 buffer_timestamp:19;
	uint32_t valid:1,
		 bw:3,
		 pkt_type:4,
		 stbc:1,
		 ldpc:1,
		 sgi:2,
		 mcs:4,
		 ofdma:1,
		 tones_in_ru:12,

		 reserved5:3;
	uint32_t tsf;
	uint32_t peer_id:16,
		 tid:8,
		 reserved6:8;
};

/**
 * dal_vndr_hal_tx_desc_set_buf_addr_be() - Fill Buffer Address information
 * in Tx Desc
 * @desc: Handle to Tx Descriptor
 * @paddr: Physical Address
 * @rbm_id: Return Buffer Manager ID
 * @desc_id: Descriptor ID
 * @type: 0 - Address points to a MSDU buffer
 *        1 - Address points to MSDU extension descriptor
 *
 * Return: None
 */
static inline void
dal_vndr_hal_tx_desc_set_buf_addr_be(void *desc,
				     dma_addr_t paddr, uint8_t rbm_id,
				     uint32_t desc_id, uint8_t type)
{
	/* Set buffer_addr_info.buffer_addr_31_0 */
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD,
			     BUF_ADDR_INFO_BUFFER_ADDR_31_0) =
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD,
				   BUF_ADDR_INFO_BUFFER_ADDR_31_0, paddr);

	/* Set buffer_addr_info.buffer_addr_39_32 */
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD,
			     BUF_ADDR_INFO_BUFFER_ADDR_39_32) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD,
				   BUF_ADDR_INFO_BUFFER_ADDR_39_32,
				   (((uint64_t)paddr) >> 32));

	/* Set buffer_addr_info.return_buffer_manager = rbm id */
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD,
			     BUF_ADDR_INFO_RETURN_BUFFER_MANAGER) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD,
				   BUF_ADDR_INFO_RETURN_BUFFER_MANAGER, rbm_id);

	/* Set buffer_addr_info.sw_buffer_cookie = desc_id */
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD,
			     BUF_ADDR_INFO_SW_BUFFER_COOKIE) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, BUF_ADDR_INFO_SW_BUFFER_COOKIE,
				   desc_id);

	/* Set  Buffer or Ext Descriptor Type */
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD,
			     BUF_OR_EXT_DESC_TYPE) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, BUF_OR_EXT_DESC_TYPE, type);
}

/**
 * dal_vndr_hal_tx_desc_set_lmac_id_be- set lmac id to the descriptor
 * @desc: Cached descriptor that software maintains
 * @lmac_id: lmac id
 */
static inline void
dal_vndr_hal_tx_desc_set_lmac_id_be(void *desc, uint8_t lmac_id)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, PMAC_ID) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, PMAC_ID, lmac_id);
}

/**
 * dal_vndr_hal_tx_desc_set_buf_length_be() - Set Data length in bytes in tx
 * descriptor
 * @desc: Handle to Tx Descriptor
 * @data_length: MSDU length in case of direct descriptor.
 * Length of link extension descriptor in case of Link extension
 * descriptor.Includes the length of Metadata
 * Return: None
 */
static inline void  dal_vndr_hal_tx_desc_set_buf_length_be(void *desc,
							   uint16_t data_length)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, DATA_LENGTH) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, DATA_LENGTH, data_length);
}

/**
 * dal_vndr_hal_tx_desc_set_buf_offset_be() - Sets Packet Offset field in Tx
 * descriptor
 * @desc: Handle to Tx Descriptor
 * @offset: Packet offset from Metadata in case of direct buffer descriptor.
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_buf_offset_be(void *desc,
							  uint8_t offset)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, PACKET_OFFSET) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, PACKET_OFFSET, offset);
}

/**
 * dal_vndr_hal_tx_desc_set_l3_checksum_en_be() -  Set IPv4 checksum enable flag
 * in  Tx Descriptor for MSDU_buffer type
 * @desc: Handle to Tx Descriptor
 * @en: ipv4 checksum enable flags
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_l3_checksum_en_be(void *desc,
							      uint8_t en)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, IPV4_CHECKSUM_EN) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, IPV4_CHECKSUM_EN, en);
}

/**
 * dal_vndr_hal_tx_desc_set_l4_checksum_en_be() - Set TCP/IP checksum enable
 * flags in Tx Descriptor for MSDU_buffer type
 * @desc: Handle to Tx Descriptor
 * @en: UDP/TCP over ipv4/ipv6 checksum enable flags (5 bits)
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_l4_checksum_en_be(void *desc,
							      uint8_t en)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, IPV4_CHECKSUM_EN) |=
		(DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, UDP_OVER_IPV4_CHECKSUM_EN, en) |
		 DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, UDP_OVER_IPV6_CHECKSUM_EN, en) |
		 DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, TCP_OVER_IPV4_CHECKSUM_EN, en) |
		 DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, TCP_OVER_IPV6_CHECKSUM_EN, en));
}

/**
 * dal_vndr_hal_tx_desc_set_bank_id_be() - set bank id to the descriptor
 * @desc: Cached descriptor that software maintains
 * @bank_id: bank id
 */
static inline void dal_vndr_hal_tx_desc_set_bank_id_be(void *desc,
						       uint8_t bank_id)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, BANK_ID) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, BANK_ID, bank_id);
}

/**
 * dal_vndr_hal_tx_desc_set_vdev_id_be() - set vdev id to the tx  descriptor
 * @desc: Cached descriptor that software maintains
 * @vdev_id: vdev id
 */
static inline void dal_vndr_hal_tx_desc_set_vdev_id_be(void *desc,
						       uint8_t vdev_id)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, VDEV_ID) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, VDEV_ID, vdev_id);
}

/**
 * dal_vndr_hal_tx_desc_set_hlos_tid_be() - Set the TID value
 * (override DSCP/PCP fields in frame) to be used for Tx Frame
 * @desc: Handle to Tx Descriptor
 * @hlos_tid: HLOS TID
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_hlos_tid_be(void *desc,
							uint8_t hlos_tid)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, HLOS_TID) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, HLOS_TID, hlos_tid);

	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, HLOS_TID_OVERWRITE) |=
	   DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, HLOS_TID_OVERWRITE, 1);
}

/**
 * dal_vndr_hal_tx_desc_set_flow_override_enable_be() - Set flow_override_enable
 * @desc: Handle to Tx Descriptor
 * @val: Value to be set
 *
 * Return: None
 */
static inline void  dal_vndr_hal_tx_desc_set_flow_override_enable_be(
								void *desc,
								uint8_t val)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, FLOW_OVERRIDE_ENABLE) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, FLOW_OVERRIDE_ENABLE, val);
}

/**
 * dal_vndr_hal_tx_desc_set_flow_override_be() - Set flow_override field in
 * TX desc
 * @desc: Handle to Tx Descriptor
 * @val: Value to be set
 *
 * Return: None
 */
static inline void  dal_vndr_hal_tx_desc_set_flow_override_be(void *desc,
							      uint8_t val)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, FLOW_OVERRIDE) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, FLOW_OVERRIDE, val);
}

/**
 * dal_vndr_hal_tx_desc_set_who_classify_info_sel_be() - Set
 * who_classify_info_sel field
 * @desc: Handle to Tx Descriptor
 * @val: Value to be set
 *
 * Return: None
 */
static inline void  dal_vndr_hal_tx_desc_set_who_classify_info_sel_be(
							void *desc,
							uint8_t val)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, WHO_CLASSIFY_INFO_SEL) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, WHO_CLASSIFY_INFO_SEL, val);
}

/**
 * dal_vndr_hal_tx_desc_set_tx_notify_frame_be() - Set TX notify_frame
 * field in Tx desc
 * @desc: Handle to Tx Descriptor
 * @val: Value to be set
 *
 * Return: None
 */
static inline void dal_vndr_hal_tx_desc_set_tx_notify_frame_be(void *desc,
							       uint8_t val)
{
	DAL_VNDR_HAL_SET_FLD(desc, TCL_DATA_CMD, TX_NOTIFY_FRAME) |=
		DAL_VNDR_HAL_TX_SM(TCL_DATA_CMD, TX_NOTIFY_FRAME, val);
}

/**
 * dal_vndr_hal_tx_comp_get_status_generic_be - Get generic tx completion status
 * @desc: WBM descriptor
 * @ts1: completion ring Tx status
 *
 * This function will parse the WBM completion descriptor and populate in
 * HAL structure
 *
 * Return: none
 */
static inline void
dal_vndr_hal_tx_comp_get_status_generic_be(void *desc, void *ts1)
{
	struct dal_vndr_hal_tx_completion_status *ts =
		(struct dal_vndr_hal_tx_completion_status *)ts1;

	*ts = *((struct dal_vndr_hal_tx_completion_status *)desc);

	ts->msdu_part_of_amsdu = (ts->first_msdu && ts->last_msdu) ?
				  false : true;
	ts->tid = ts->tid & 0xF;
}

/**
 * dal_vndr_hal_tx_comp_get_paddr_be() - Get paddr within comp descriptor
 * @hal_desc: completion ring descriptor pointer
 *
 * This function will get buffer physical address within hardware completion
 * descriptor
 *
 * Return: Buffer physical address
 */
static inline dma_addr_t dal_vndr_hal_tx_comp_get_paddr_be(void *hal_desc)
{
	uint32_t paddr_lo;
	uint32_t paddr_hi;

	paddr_lo = *(uint32_t *)(((uint8_t *)hal_desc) +
			BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET);

	paddr_hi = *(uint32_t *)(((uint8_t *)hal_desc) +
			BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET);

	paddr_hi = (paddr_hi & BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_MASK) >>
		BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_LSB;

	return (dma_addr_t)(paddr_lo | (((uint64_t)paddr_hi) << 32));
}

/**
 * dal_vndr_hal_rx_error_status_get_be() - Get rx error status from ring desc
 * @ring_desc: reo ring descriptor
 *
 * Return: rx error status
 */
static inline uint8_t dal_vndr_hal_rx_error_status_get_be(void *ring_desc)
{
	return DAL_VNDR_HAL_RX_ERROR_STATUS_GET(ring_desc);
}

/**
 * dal_vndr_hal_rx_reo_buf_cookie_get_be() - Get sw cookie from ring desc
 * @reo_desc: ring descriptor
 *
 * Return: sw cookie
 */
static inline uint32_t dal_vndr_hal_rx_reo_buf_cookie_get_be(void *reo_desc)
{
	return DAL_VNDR_HAL_RX_REO_BUF_COOKIE_GET(reo_desc);
}

/**
 * dal_vndr_hal_rx_ret_buf_manager_get_be() - Get return buffer manager from
 * ring desc
 * @ring_desc: ring descriptor
 *
 * Return: rbm
 */
static inline uint8_t dal_vndr_hal_rx_ret_buf_manager_get_be(void *ring_desc)
{
	return DAL_VNDR_HAL_RX_BUF_RBM_GET(ring_desc);
}

/**
 * dal_vndr_hal_rx_reo_buf_paddr_get_be() - Get reo buff paddr from ring desc
 * @ring_desc: ring descriptor
 *
 * Return: reo buff paddr
 */
static inline dma_addr_t dal_vndr_hal_rx_reo_buf_paddr_get_be(void *ring_desc)
{
	struct dal_reo_destination_ring *reo_ring =
		 (struct dal_reo_destination_ring *)ring_desc;

	dma_addr_t paddr =
	 (DAL_VNDR_HAL_RX_REO_BUFFER_ADDR_31_0_GET(reo_ring) |
	  ((uint64_t)(DAL_VNDR_HAL_RX_REO_BUFFER_ADDR_39_32_GET(reo_ring)) << 32));
	return paddr;
}

/**
 * dal_vndr_hal_rx_reo_get_details_be() - Get desc info about paddr,sw_cookie
 * and rbm from reo ring desc
 * @rx_desc: ring descriptor
 * @buf_info: buf_info pointer to be filled with details
 *
 * Return: buf_info is modified to return the paddr, cookie and rbm
 */
static inline void dal_vndr_hal_rx_reo_get_details_be(
				void *rx_desc,
				struct dal_vndr_hal_buf_info *buf_info)
{
	struct dal_reo_destination_ring *reo_ring =
		 (struct dal_reo_destination_ring *)rx_desc;

	buf_info->paddr = dal_vndr_hal_rx_reo_buf_paddr_get_be(
							(void *)reo_ring);
	buf_info->sw_cookie = dal_vndr_hal_rx_reo_buf_cookie_get_be(
							(void *)reo_ring);
	/*
	 * buffer addr info is the first member of ring desc, so the below
	 * call is valid.
	 */
	buf_info->rbm = dal_vndr_hal_rx_ret_buf_manager_get_be(
							(void *)reo_ring);
}

/**
 * dal_vndr_hal_rx_tlv_msdu_done_get_be() - API to get the msdu done bit.
 * @buf: pointer to the start of RX PKT TLV header
 *
 * Return: msdu done bit
 */
static inline uint32_t dal_vndr_hal_rx_tlv_msdu_done_get_be(uint8_t *buf)
{
	return DAL_VNDR_HAL_RX_TLV_MSDU_DONE_GET(buf);
}

/**
 * dal_vndr_hal_rx_get_l3_pad_bytes_be() - API to get l3 padding  bytes
 * @rx_tlv_hdr: pointer to the start of RX PKT TLV headers
 *
 * Return: l3 padding bytes
 */
static inline uint8_t dal_vndr_hal_rx_get_l3_pad_bytes_be(uint8_t *rx_tlv_hdr)
{
	return DAL_VNDR_HAL_RX_TLV_L3_HEADER_PADDING_GET(rx_tlv_hdr);
}

/**
 * dal_vndr_hal_rx_tlv_sgi_get_be() - API to get the Short Guard Interval from
 *                           rx_msdu_start TLV
 * @buf: pointer to the start of RX PKT TLV headers
 *
 * Return: uint32_t(sgi)
 */
static inline uint32_t dal_vndr_hal_rx_tlv_sgi_get_be(uint8_t *buf)
{
	struct dal_rx_pkt_tlvs *rx_pkt_tlvs = (struct dal_rx_pkt_tlvs *)buf;

	return DAL_VNDR_HAL_RX_TLV_SGI_GET(rx_pkt_tlvs);
}

/*
 * Care must be taken to get the correct MCS.
 * The MCS index does not start with 0 when NSS>1 in HT mode.
 * MCS params for optional 20/40MHz, NSS=1~3, EQM(NSS>1):
 * ------------------------------------------------------
 *         NSS     |   1   |   2    |    3    |    4
 * ------------------------------------------------------
 * MCS index: HT20 | 0 ~ 7 | 8 ~ 15 | 16 ~ 23 | 24 ~ 31
 * ------------------------------------------------------
 * MCS index: HT40 | 0 ~ 7 | 8 ~ 15 | 16 ~ 23 | 24 ~ 31
 * ------------------------------------------------------
 * Currently, the MAX_NSS=2. If NSS>2, MCS index = 8 * (NSS-1)
 */
/**
 * dal_vndr_hal_rx_tlv_rate_mcs_get_be() - API to get the MCS rate from
 *                                rx_msdu_start TLV
 * @buf: pointer to the start of RX PKT TLV headers
 *
 * Return: uint32_t(rate_mcs)
 */
static inline uint32_t dal_vndr_hal_rx_tlv_rate_mcs_get_be(uint8_t *buf)
{
	struct dal_rx_pkt_tlvs *rx_pkt_tlvs = (struct dal_rx_pkt_tlvs *)buf;
	uint32_t rate_mcs;

	rate_mcs = DAL_VNDR_HAL_RX_TLV_RATE_MCS_GET(rx_pkt_tlvs);

	return rate_mcs;
}

/**
 * dal_vndr_hal_rx_tlv_bw_get_be() - API to get the Bandwidth Interval from
 * rx_msdu_start
 * @buf: pointer to the start of RX PKT TLV header
 *
 * Return: uint32_t(bw)
 */
static inline uint32_t dal_vndr_hal_rx_tlv_bw_get_be(uint8_t *buf)
{
	struct dal_rx_pkt_tlvs *rx_pkt_tlvs = (struct dal_rx_pkt_tlvs *)buf;

	return DAL_VNDR_HAL_RX_TLV_BW_GET(rx_pkt_tlvs);
}

/**
 * dal_vndr_hal_rx_tlv_get_pkt_type_be() - API to get the pkt type from
 *                                rx_msdu_start
 * @buf: pointer to the start of RX PKT TLV header
 *
 * Return: uint32_t(pkt type)
 */

static inline uint32_t dal_vndr_hal_rx_tlv_get_pkt_type_be(uint8_t *buf)
{
	struct dal_rx_pkt_tlvs *rx_pkt_tlvs = (struct dal_rx_pkt_tlvs *)buf;
	uint32_t pkt_type;

	pkt_type = DAL_VNDR_HAL_RX_TLV_PKT_TYPE_GET(rx_pkt_tlvs);

	return pkt_type;
}

/**
 * dal_vndr_hal_rx_tlv_nss_get_be() - API to get the NSS Interval from
 * rx_msdu_start
 * @buf: pointer to the start of RX PKT TLV header
 *
 * Return: uint32_t(nss)
 */
static inline uint32_t dal_vndr_hal_rx_tlv_nss_get_be(uint8_t *buf)
{
	struct dal_rx_pkt_tlvs *rx_pkt_tlvs = (struct dal_rx_pkt_tlvs *)buf;
	uint8_t mimo_ss_bitmap;

	mimo_ss_bitmap = DAL_VNDR_HAL_RX_TLV_MIMO_SS_BITMAP(rx_pkt_tlvs);

	return get_hweight8(mimo_ss_bitmap);
}

/**
 * dal_vndr_hal_rx_msdu_flags_get_be() - Get msdu flags from ring desc
 * @msdu_desc_info_hdl: msdu desc info handle
 *
 * This returns flags for msdu continuation, first msdu and last msdu
 * in mpdu.
 * Return: msdu flags
 */
static inline
uint32_t dal_vndr_hal_rx_msdu_flags_get_be(void *msdu_desc_info_hdl)
{
	struct dal_rx_msdu_desc_info *msdu_desc_info =
		(struct dal_rx_msdu_desc_info *)msdu_desc_info_hdl;
	uint32_t flags = 0;

	if (DAL_VNDR_HAL_RX_FIRST_MSDU_IN_MPDU_FLAG_GET(msdu_desc_info))
		flags |= DAL_VNDR_HAL_MSDU_F_FIRST_MSDU_IN_MPDU;

	if (DAL_VNDR_HAL_RX_LAST_MSDU_IN_MPDU_FLAG_GET(msdu_desc_info))
		flags |= DAL_VNDR_HAL_MSDU_F_LAST_MSDU_IN_MPDU;

	if (DAL_VNDR_HAL_RX_MSDU_CONTINUATION_FLAG_GET(msdu_desc_info))
		flags |= DAL_VNDR_HAL_MSDU_F_MSDU_CONTINUATION;

	if (DAL_VNDR_HAL_RX_MSDU_SA_IS_VALID_FLAG_GET(msdu_desc_info))
		flags |= DAL_VNDR_HAL_MSDU_F_SA_IS_VALID;

	if (DAL_VNDR_HAL_RX_MSDU_DA_IS_VALID_FLAG_GET(msdu_desc_info))
		flags |= DAL_VNDR_HAL_MSDU_F_DA_IS_VALID;

	if (DAL_VNDR_HAL_RX_MSDU_DA_IS_MCBC_FLAG_GET(msdu_desc_info))
		flags |= DAL_VNDR_HAL_MSDU_F_DA_IS_MCBC;

	if (DAL_VNDR_HAL_RX_MSDU_INTRA_BSS_FLAG_GET(msdu_desc_info))
		flags |= DAL_VNDR_HAL_MSDU_F_INTRA_BSS;

	return flags;
}

/*
 * dal_vndr_hal_rx_msdu_desc_info_get_be: Gets the flags related to
 * MSDU descriptor.
 * @desc_addr: REO ring descriptor addr
 * @msdu_desc_info: Holds MSDU descriptor info from HAL Rx descriptor
 *
 * Specifically flags needed are: first_msdu_in_mpdu,
 * last_msdu_in_mpdu, msdu_continuation.
 *
 * Return: void
 */
static inline void
dal_vndr_hal_rx_msdu_desc_info_get_be(
			void *desc_addr,
			struct dal_vndr_hal_rx_msdu_desc_info *msdu_desc_info)
{
	struct dal_reo_destination_ring *reo_dst_ring;
	uint32_t *msdu_info;

	reo_dst_ring = (struct dal_reo_destination_ring *)desc_addr;

	msdu_info = (uint32_t *)&reo_dst_ring->rx_msdu_desc_info_details;
	msdu_desc_info->msdu_flags =
			dal_vndr_hal_rx_msdu_flags_get_be(
				(struct dal_rx_msdu_desc_info *)msdu_info);
	msdu_desc_info->msdu_len = DAL_VNDR_HAL_RX_MSDU_PKT_LENGTH_GET(msdu_info);
}

/**
 * dal_vndr_hal_rxdma_buff_addr_info_set_be() - set the buffer_addr_info of the
 * rxdma ring entry.
 * @rxdma_entry: descriptor entry
 * @paddr: physical address of nbuf data pointer.
 * @cookie: SW cookie used as a index to SW rx desc.
 * @manager: who owns the nbuf.
 *
 */
static inline void
dal_vndr_hal_rxdma_buff_addr_info_set_be(void *rxdma_entry,
					 dma_addr_t paddr, uint32_t cookie,
					 uint8_t manager)
{
	uint32_t paddr_lo = ((uint64_t)paddr & 0x00000000ffffffff);
	uint32_t paddr_hi = ((uint64_t)paddr & 0xffffffff00000000) >> 32;

	DAL_VNDR_HAL_RXDMA_PADDR_LO_SET(rxdma_entry, paddr_lo);
	DAL_VNDR_HAL_RXDMA_PADDR_HI_SET(rxdma_entry, paddr_hi);
	DAL_VNDR_HAL_RXDMA_COOKIE_SET(rxdma_entry, cookie);
	DAL_VNDR_HAL_RXDMA_MANAGER_SET(rxdma_entry, manager);
}

/**
 * dal_vndr_hal_rxbm_sync_be() - Sync the buff addr info to refill ring
 * @ring_desc: Ring descriptor pointer
 * @buff_addr_info: Buffer address info pointer
 *
 * This function syncs the buffer address info to the refill ring by copying
 * the buffer address info to the ring descriptor.
 *
 * Return: void
 */
static inline void dal_vndr_hal_rxbm_sync_be(void *ring_desc,
					     void *buff_addr_info)
{
	if (!ring_desc || !buff_addr_info)
		return;

	memcpy(ring_desc, buff_addr_info, sizeof(struct dal_buffer_addr_info));
}

#endif /* _DAL_VNDR_HAL_BE_H_ */
