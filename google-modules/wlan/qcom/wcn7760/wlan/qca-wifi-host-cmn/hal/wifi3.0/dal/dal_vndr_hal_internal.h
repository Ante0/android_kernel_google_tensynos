/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _DAL_VNDR_HAL_INTERNAL_H_
#define _DAL_VNDR_HAL_INTERNAL_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#define dal_vndr_hal_srng_lock_t spinlock_t
#define DAL_VNDR_SRNG_LOCK_INIT(_lock) spin_lock_init(_lock)
#define DAL_VNDR_SRNG_LOCK(_lock) spin_lock_bh(_lock)
#define DAL_VNDR_SRNG_TRY_LOCK(_lock) spin_trylock_bh(_lock)
#define DAL_VNDR_SRNG_UNLOCK(_lock) spin_unlock_bh(_lock)
#define DAL_VNDR_SRNG_LOCK_DESTROY(_lock) /* nothing to do */

/**
 * struct dal_vndr_hal_buf_info - RX MSDU Descriptor
 * @paddr: physical address
 * @sw_cookie: cookie used by SW to identify the buffer
 * @rbm: rbm id
 *
 */
struct dal_vndr_hal_buf_info {
	uint64_t paddr;
	uint32_t sw_cookie;
	uint8_t rbm;
};

/**
 * enum dal_vndr_hal_rx_msdu_desc_flags - Enum for flags in MSDU_DESC_INFO
 *
 * @DAL_VNDR_HAL_MSDU_F_FIRST_MSDU_IN_MPDU: First MSDU in MPDU
 * @DAL_VNDR_HAL_MSDU_F_LAST_MSDU_IN_MPDU: Last MSDU in MPDU
 * @DAL_VNDR_HAL_MSDU_F_MSDU_CONTINUATION: MSDU continuation
 * @DAL_VNDR_HAL_MSDU_F_SA_IS_VALID: Found match for SA in AST
 * @DAL_VNDR_HAL_MSDU_F_SA_IDX_TIMEOUT: AST search for SA timed out
 * @DAL_VNDR_HAL_MSDU_F_DA_IS_VALID: Found match for DA in AST
 * @DAL_VNDR_HAL_MSDU_F_DA_IS_MCBC: DA is MC/BC address
 * @DAL_VNDR_HAL_MSDU_F_DA_IDX_TIMEOUT: AST search for DA timed out
 * @DAL_VNDR_HAL_MSDU_F_INTRA_BSS: This is an intrabss packet
 */
enum dal_vndr_hal_rx_msdu_desc_flags {
	DAL_VNDR_HAL_MSDU_F_FIRST_MSDU_IN_MPDU = (0x1 << 0),
	DAL_VNDR_HAL_MSDU_F_LAST_MSDU_IN_MPDU = (0x1 << 1),
	DAL_VNDR_HAL_MSDU_F_MSDU_CONTINUATION = (0x1 << 2),
	DAL_VNDR_HAL_MSDU_F_SA_IS_VALID = (0x1 << 23),
	DAL_VNDR_HAL_MSDU_F_SA_IDX_TIMEOUT = (0x1 << 24),
	DAL_VNDR_HAL_MSDU_F_DA_IS_VALID = (0x1 << 25),
	DAL_VNDR_HAL_MSDU_F_DA_IS_MCBC = (0x1 << 26),
	DAL_VNDR_HAL_MSDU_F_DA_IDX_TIMEOUT = (0x1 << 27),
	DAL_VNDR_HAL_MSDU_F_INTRA_BSS = (0x1 << 28),
};

/**
 * struct dal_vndr_hal_rx_msdu_desc_info - RX MSDU Descriptor
 * @msdu_flags: [0] first_msdu_in_mpdu
 *              [1] last_msdu_in_mpdu
 *              [2] msdu_continuation - MSDU spread across buffers
 *             [23] sa_is_valid - SA match in peer table
 *             [24] sa_idx_timeout - Timeout while searching for SA match
 *             [25] da_is_valid - Used to identtify intra-bss forwarding
 *             [26] da_is_MCBC
 *             [27] da_idx_timeout - Timeout while searching for DA match
 * @msdu_len: length of the MSDU (14 bits of length)
 *
 */
struct dal_vndr_hal_rx_msdu_desc_info {
	uint32_t msdu_flags;
	uint16_t msdu_len;
};

struct dal_vndr_hal_hw_txrx_ops {
	/* tx */
	void (*dal_vndr_hal_tx_desc_set_lmac_id)(void *desc, uint8_t lmac_id);
	void (*dal_vndr_hal_tx_desc_set_buf_addr)(void *desc, dma_addr_t paddr,
						  uint8_t pool_id,
						  uint32_t desc_id,
						  uint8_t type);
	void (*dal_vndr_hal_tx_desc_set_buf_length)(void *desc,
						    uint16_t data_length);
	void (*dal_vndr_hal_tx_desc_set_buf_offset)(void *desc, uint8_t offset);
	void (*dal_vndr_hal_tx_desc_set_l3_checksum_en)(void *desc, uint8_t en);
	void (*dal_vndr_hal_tx_desc_set_l4_checksum_en)(void *desc, uint8_t en);
	void (*dal_vndr_hal_tx_desc_set_bank_id)(void *desc, uint8_t bank_id);
	void (*dal_vndr_hal_tx_desc_set_vdev_id)(void *desc, uint8_t vdev_id);
	void (*dal_vndr_hal_tx_desc_set_hlos_tid)(void *desc, uint8_t hlos_tid);
	void (*dal_vndr_hal_tx_desc_set_flow_override_enable)(void *desc,
							      uint8_t val);
	void (*dal_vndr_hal_tx_desc_set_flow_override)(void *desc, uint8_t val);
	void (*dal_vndr_hal_tx_desc_set_who_classify_info_sel)(void *desc,
							       uint8_t val);
	void (*dal_vndr_hal_tx_desc_set_tx_notify_frame)(void *desc,
							 uint8_t val);
	void (*dal_vndr_hal_tx_comp_get_status)(void *desc, void *ts1);
	dma_addr_t (*dal_vndr_hal_tx_comp_get_paddr)(void *hal_desc);
	uint32_t (*dal_vndr_hal_tx_comp_get_desc_id)(void *hal_desc);
	uint32_t (*dal_vndr_hal_tx_comp_get_buffer_source)(void *hal_desc);
	uint8_t (*dal_vndr_hal_tx_comp_get_tx_status)(void *hal_desc);
	/* rx */
	uint8_t (*dal_vndr_hal_rx_error_status_get)(void *reo_desc);
	uint32_t (*dal_vndr_hal_rx_reo_buf_cookie_get)(void *reo_desc);
	uint8_t (*dal_vndr_hal_rx_ret_buf_manager_get)(void *ring_desc);
	void (*dal_vndr_hal_rx_reo_get_details)(
				void *ring_desc,
				struct dal_vndr_hal_buf_info *buf_info);
	uint32_t (*dal_vndr_hal_rx_msdu_start_nss_get)(uint8_t *buff);
	uint32_t (*dal_vndr_hal_rx_tlv_sgi_get)(uint8_t *buf);
	uint32_t (*dal_vndr_hal_rx_tlv_rate_mcs_get)(uint8_t *buf);
	uint32_t (*dal_vndr_hal_rx_tlv_bw_get)(uint8_t *buf);
	uint32_t (*dal_vndr_hal_rx_tlv_get_pkt_type)(uint8_t *buf);
	uint8_t (*dal_vndr_hal_rx_get_l3_pad_bytes)(uint8_t *buf);
	uint32_t (*dal_vndr_hal_rx_tlv_msdu_done_get)(uint8_t *buf);
	dma_addr_t (*dal_vndr_hal_rx_reo_buf_paddr_get)(void *ring_desc);
	void (*dal_vndr_hal_rx_msdu_desc_info_get)(
			void *desc_addr,
			struct dal_vndr_hal_rx_msdu_desc_info *msdu_desc_info);
	void (*dal_vndr_hal_rxdma_buff_addr_info_set)(void *rxdma_entry,
						      dma_addr_t paddr,
						      uint32_t cookie,
						      uint8_t manager);
	void (*dal_vndr_hal_rxbm_sync)(void *ring_desc, void *buff_addr_info);
};

/**
 * enum dal_vndr_hal_srng_dir - Enum for ring direction
 *
 * @DAL_VNDR_HAL_SRNG_SRC_RING: Src ring
 * @DAL_VNDR_HAL_SRNG_DST_RING: destination ring
 */
enum dal_vndr_hal_srng_dir {
	DAL_VNDR_HAL_SRNG_SRC_RING,
	DAL_VNDR_HAL_SRNG_DST_RING
};

/**
 * struct dal_vndr_hal_soc - HAL context to be used to access SRNG APIs
 * @dev_base_addr: Device base address
 * @ops: TXRX operations
 */
struct dal_vndr_hal_soc {
	void *dev_base_addr;
	struct dal_vndr_hal_hw_txrx_ops *ops;
};

struct dal_vndr_hal_srng {
	/* Unique SRNG ring ID */
	uint8_t ring_id;
	/* Physical base address of the ring */
	dma_addr_t ring_base_paddr;
	/* base address of the ring used for fetching descriptors */
	uint32_t *ring_base_addr;
	/* Number of entries in ring */
	uint32_t num_entries;
	/* Ring size */
	uint32_t ring_size;
	/* Ring size mask */
	uint32_t ring_size_mask;
	/* Whether LMAC ring */
	bool lmac_ring;
	/* Size of ring entry */
	uint32_t entry_size;
	/* MSI Address */
	dma_addr_t msi_addr;
	/* MSI data */
	uint32_t msi_data;
	/* Misc flags */
	uint32_t flags;
	/* Ring type/name */
	int ring_type;
	/* irq number */
	int irq_num;
	/* Source or Destination ring */
	enum dal_vndr_hal_srng_dir ring_dir;

	union {
		struct {
			/* SW tail pointer */
			uint32_t tp;

			/* Shadow head pointer location to be updated by HW */
			uint32_t *hp_addr;

			/* Cached head pointer */
			uint32_t cached_hp;

			/* Tail pointer location to be updated by SW – This
			 * will be a register address and need not be
			 * accessed through SW structure
			 */
			uint32_t *tp_addr;
		} dst_ring;

		struct {
			/* SW head pointer */
			uint32_t hp;

			/* Shadow tail pointer location to be updated by HW */
			uint32_t *tp_addr;

			/* Cached tail pointer */
			uint32_t cached_tp;

			/* Head pointer location to be updated by SW – This
			 * will be a register address and need not be accessed
			 * through SW structure
			 */
			uint32_t *hp_addr;
		} src_ring;
	} u;

	struct dal_vndr_hal_soc *hal_soc;
	/* Lock for serializing ring index updates */
	dal_vndr_hal_srng_lock_t lock;
};

/*
 * Given the offset of a field in bytes, returns uint8_t *
 */
#define DAL_VNDR_OFFSET_TO_BYTE_PTR(_ptr, _off_in_bytes)	\
	(((uint8_t *)(_ptr)) + (_off_in_bytes))

/*
 * Given the offset of a field in bytes, returns uint32_t *
 */
#define DAL_VNDR_OFFSET_TO_WORD_PTR(_ptr, _off_in_bytes)	\
	(((uint32_t *)(_ptr)) + ((_off_in_bytes) >> 2))

/*
 * Given the offset of a field in bytes, returns uint64_t *
 */
#define DAL_VNDR_OFFSET_TO_QWORD_PTR(_ptr, _off_in_bytes)	\
	(((uint64_t *)(_ptr)) + ((_off_in_bytes) >> 3))

#define DAL_VNDR_HAL_MS(_word, _mask, _shift)		\
	(((_word) & (_mask)) >> (_shift))

#define DAL_VNDR_HAL_OFFSET(block, field) block ## _ ## field ## _OFFSET

#define DAL_VNDR_HAL_TX_LSB(block, field) block ## _ ## field ## _LSB

#define DAL_VNDR_HAL_TX_MASK(block, field) block ## _ ## field ## _MASK

#define DAL_VNDR_HAL_TX_DESC_OFFSET(desc, block, field) \
	(((uint8_t *)desc) + DAL_VNDR_HAL_OFFSET(block, field))

#define DAL_VNDR_HAL_SET_FLD(desc, block, field) \
	(*(uint32_t *) ((uint8_t *) desc + DAL_VNDR_HAL_OFFSET(block, field)))

#define DAL_VNDR_HAL_SET_FLD_OFFSET(desc, block, field, offset) \
	(*(uint32_t *) ((uint8_t *) desc + DAL_VNDR_HAL_OFFSET(block, field) + (offset)))

#define DAL_VNDR_HAL_SET_FLD_64(desc, block, field) \
	(*(uint64_t *)((uint8_t *)desc + DAL_VNDR_HAL_OFFSET(block, field)))

#define DAL_VNDR_HAL_SET_FLD_OFFSET_64(desc, block, field, offset) \
	(*(uint64_t *)((uint8_t *)desc + DAL_VNDR_HAL_OFFSET(block, field) + (offset)))

#define DAL_VNDR_HAL_TX_SM(block, field, value) \
	((value << (block ## _ ## field ## _LSB)) & \
	 (block ## _ ## field ## _MASK))

#define DAL_VNDR_HAL_TX_MS(block, field, value) \
	(((value) & (block ## _ ## field ## _MASK)) >> \
	 (block ## _ ## field ## _LSB))

#define DAL_VNDR_HAL_TX_DESC_GET(desc, block, field) \
	DAL_VNDR_HAL_TX_MS(block, field, DAL_VNDR_HAL_SET_FLD(desc, block, field))

#define DAL_VNDR_HAL_TX_DESC_OFFSET_GET(desc, block, field, offset) \
	DAL_VNDR_HAL_TX_MS(block, field, DAL_VNDR_HAL_SET_FLD_OFFSET(desc, block, field, offset))

#define DAL_VNDR_HAL_TX_DESC_SUBBLOCK_GET(desc, block, sub, field) \
	DAL_VNDR_HAL_TX_MS(sub, field, DAL_VNDR_HAL_SET_FLD(desc, block, sub))

#define DAL_VNDR_HAL_TX_DESC_GET_64(desc, block, field) \
	DAL_VNDR_HAL_TX_MS(block, field, DAL_VNDR_HAL_SET_FLD_64(desc, block, field))

#define DAL_VNDR_HAL_TX_DESC_OFFSET_GET_64(desc, block, field, offset) \
	DAL_VNDR_HAL_TX_MS(block, field, DAL_VNDR_HAL_SET_FLD_OFFSET_64(desc, block, field,\
		  offset))

#define DAL_VNDR_HAL_TX_DESC_SUBBLOCK_GET_64(desc, block, sub, field) \
	DAL_VNDR_HAL_TX_MS(sub, field, DAL_VNDR_HAL_SET_FLD_64(desc, block, sub))

/**
 * enum dal_vndr_hal_reo_error_status - Enum which encapsulates
 * "reo_push_reason"
 *
 * @DAL_VNDR_HAL_REO_ERROR_DETECTED: Packets arrived because of an error
 * detected
 * @DAL_VNDR_HAL_REO_ROUTING_INSTRUCTION: Packets arrived because of REO routing
 */
enum dal_vndr_hal_reo_error_status {
	DAL_VNDR_HAL_REO_ERROR_DETECTED = 0,
	DAL_VNDR_HAL_REO_ROUTING_INSTRUCTION = 1,
};

#define DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_OFFSET       0x0
#define DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_LSB          0
#define DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_31_0_MASK         0xffffffff

#define DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_OFFSET	0x4
#define DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_LSB	0
#define DAL_VNDR_HAL_BUFFER_ADDR_INFO_BUFFER_ADDR_39_32_MASK	0x000000ff

#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_OFFSET 0x0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_LSB    0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_MASK   0x00000001

#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_OFFSET 0x0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_LSB    1
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_MASK   0x00000002

#define DAL_VNDR_HAL_RX_FIRST_MSDU_IN_MPDU_FLAG_GET(msdu_info_ptr)	\
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_OFFSET)) & \
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_FIRST_MSDU_IN_MPDU_FLAG_MASK)

#define DAL_VNDR_HAL_RX_LAST_MSDU_IN_MPDU_FLAG_GET(msdu_info_ptr) \
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_OFFSET)) & \
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_LAST_MSDU_IN_MPDU_FLAG_MASK)

#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_MSDU_LENGTH_OFFSET	0x0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_MSDU_LENGTH_LSB		3
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_MSDU_LENGTH_MASK		0x0001fff8

#define DAL_VNDR_HAL_RX_MSDU_PKT_LENGTH_GET(msdu_info_ptr)		\
	(DAL_VNDR_HAL_MS((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,		\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_MSDU_LENGTH_OFFSET)),	\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_MSDU_LENGTH_MASK,		\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_MSDU_LENGTH_LSB))

#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_SA_IS_VALID_OFFSET	0x0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_SA_IS_VALID_LSB		18
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_SA_IS_VALID_MASK		0x00040000

#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_VALID_OFFSET	0x0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_VALID_LSB		19
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_VALID_MASK		0x00080000

#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_MCBC_OFFSET	0x0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_MCBC_LSB		20
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_MCBC_MASK		0x00100000

#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_INTRA_BSS_OFFSET		0x0
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_INTRA_BSS_LSB		26
#define DAL_VNDR_HAL_RX_MSDU_DESC_INFO_INTRA_BSS_MASK		0x04000000

#define DAL_VNDR_HAL_RX_MSDU_CONTINUATION_FLAG_GET(msdu_info_ptr)	\
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		RX_MSDU_DESC_INFO_MSDU_CONTINUATION_OFFSET)) & \
		RX_MSDU_DESC_INFO_MSDU_CONTINUATION_MASK)

#define DAL_VNDR_HAL_RX_MSDU_SA_IS_VALID_FLAG_GET(msdu_info_ptr)	\
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_SA_IS_VALID_OFFSET)) &	\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_SA_IS_VALID_MASK)

#define DAL_VNDR_HAL_RX_MSDU_DA_IS_VALID_FLAG_GET(msdu_info_ptr)	\
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_VALID_OFFSET)) &	\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_VALID_MASK)

#define DAL_VNDR_HAL_RX_MSDU_DA_IS_MCBC_FLAG_GET(msdu_info_ptr)	\
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_MCBC_OFFSET)) &	\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_DA_IS_MCBC_MASK)

#define DAL_VNDR_HAL_RX_MSDU_INTRA_BSS_FLAG_GET(msdu_info_ptr)		\
	((*DAL_VNDR_OFFSET_TO_WORD_PTR(msdu_info_ptr,			\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_INTRA_BSS_OFFSET)) &	\
		DAL_VNDR_HAL_RX_MSDU_DESC_INFO_INTRA_BSS_MASK)

/**
 * get_hweight8() - count num of 1's in 8-bit bitmap
 * @w: input bitmap
 *
 * Count num of 1's set in the 8-bit bitmap
 *
 * Return: num of 1's
 */
static inline
unsigned int get_hweight8(unsigned int w)
{
	unsigned int res = w - ((w >> 1) & 0x55);

	res = (res & 0x33) + ((res >> 2) & 0x33);

	return (res + (res >> 4)) & 0x0F;
}

/**
 * enum dal_vndr_hal_pkt_type - Type of packet type reported by HW
 * @DAL_VNDR_HAL_DOT11A: 802.11a PPDU type
 * @DAL_VNDR_HAL_DOT11B: 802.11b PPDU type
 * @DAL_VNDR_HAL_DOT11N_MM: 802.11n Mixed Mode PPDU type
 * @DAL_VNDR_HAL_DOT11AC: 802.11ac PPDU type
 * @DAL_VNDR_HAL_DOT11AX: 802.11ax PPDU type
 * @DAL_VNDR_HAL_DOT11BA: 802.11ba (WUR) PPDU type
 * @DAL_VNDR_HAL_DOT11BE: 802.11be PPDU type
 * @DAL_VNDR_HAL_DOT11AZ: 802.11az (ranging) PPDU type
 * @DAL_VNDR_HAL_DOT11N_GF: 802.11n Green Field PPDU type
 * @DAL_VNDR_HAL_DOT11_MAX: Maximum enumeration
 *
 * Enum indicating the packet type reported by HW in rx_pkt_tlvs (RX data)
 * or WBM2SW ring entry's descriptor (TX data completion)
 */
enum dal_vndr_hal_pkt_type {
	DAL_VNDR_HAL_DOT11A = 0,
	DAL_VNDR_HAL_DOT11B = 1,
	DAL_VNDR_HAL_DOT11N_MM = 2,
	DAL_VNDR_HAL_DOT11AC = 3,
	DAL_VNDR_HAL_DOT11AX = 4,
	DAL_VNDR_HAL_DOT11BA = 5,
	DAL_VNDR_HAL_DOT11BE = 6,
	DAL_VNDR_HAL_DOT11AZ = 7,
	DAL_VNDR_HAL_DOT11N_GF = 8,
	DAL_VNDR_HAL_DOT11_MAX,
};

/**
 * enum dal_vndr_hal_tx_comp_rel_src - Indicates the release source module
 * @DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_TQM : TQM released this buffer
 * @DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_RXDMA : RXDMA released this buffer
 * @DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_REO : REO released this buffer
 * @DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_FW : FW released this buffer
 * @DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_MAX : count of number of enumerator
 */
enum dal_vndr_hal_tx_comp_rel_src {
	DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_TQM,
	DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_RXDMA,
	DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_REO,
	DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_FW,
	DAL_VNDR_HAL_TX_COMP_RELEASE_SOURCE_MAX
};

/**
 * enum dal_vndr_hal_tx_tqm_release_reason - TQM Release reason codes
 *
 * @DAL_VNDR_HAL_TX_TQM_RR_FRAME_ACKED : ACK of BA for it was received
 * @DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_REM : Remove cmd of type “Remove_mpdus”
 * by SW
 * @DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_TX  : Remove command of type
 * Remove_transmitted_mpdus initiated by SW
 * @DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_NOTX : Remove cmd of type
 * Remove_untransmitted_mpdus initiated by SW
 * @DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_AGED : Remove command of type
 * “Remove_aged_mpdus” or “Remove_aged_msdus” initiated by SW
 * @DAL_VNDR_HAL_TX_TQM_RR_FW_REASON1 : Remove command where fw indicated that
 * remove reason is fw_reason1
 * @DAL_VNDR_HAL_TX_TQM_RR_FW_REASON2 : Remove command where fw indicated that
 * remove reason is fw_reason2
 * @DAL_VNDR_HAL_TX_TQM_RR_FW_REASON3 : Remove command where fw indicated that
 * remove reason is fw_reason3
 * @DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_DISABLE_QUEUE : Remove command where fw
 * indicated that remove reason is remove disable queue
 * @DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_TILL_NONMATCHING: Remove command from fw to
 * remove all mpdu until 1st non-match
 * @DAL_VNDR_HAL_TX_TQM_RR_DROP_THRESHOLD: Dropped due to drop threshold
 * criteria
 * @DAL_VNDR_HAL_TX_TQM_RR_LINK_DESC_UNAVAILABLE: Dropped due to link desc not
 * available
 * @DAL_VNDR_HAL_TX_TQM_RR_DROP_OR_INVALID_MSDU: Dropped due drop bit set or
 * null flow
 * @DAL_VNDR_HAL_TX_TQM_RR_MULTICAST_DROP: Dropped due mcast drop set for VDEV
 * @DAL_VNDR_HAL_TX_TQM_RR_VDEV_MISMATCH_DROP: Dropped due to being set with
 * 'TCL_drop_reason'
 * @DAL_VNDR_HAL_TX_TQM_RR_MAX: Max value TQM release reason code
 */
enum dal_vndr_hal_tx_tqm_release_reason {
	DAL_VNDR_HAL_TX_TQM_RR_FRAME_ACKED,
	DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_REM,
	DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_TX,
	DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_NOTX,
	DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_AGED,
	DAL_VNDR_HAL_TX_TQM_RR_FW_REASON1,
	DAL_VNDR_HAL_TX_TQM_RR_FW_REASON2,
	DAL_VNDR_HAL_TX_TQM_RR_FW_REASON3,
	DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_DISABLE_QUEUE,
	DAL_VNDR_HAL_TX_TQM_RR_REM_CMD_TILL_NONMATCHING,
	DAL_VNDR_HAL_TX_TQM_RR_DROP_THRESHOLD,
	DAL_VNDR_HAL_TX_TQM_RR_LINK_DESC_UNAVAILABLE,
	DAL_VNDR_HAL_TX_TQM_RR_DROP_OR_INVALID_MSDU,
	DAL_VNDR_HAL_TX_TQM_RR_MULTICAST_DROP,
	DAL_VNDR_HAL_TX_TQM_RR_VDEV_MISMATCH_DROP,
	DAL_VNDR_HAL_TX_TQM_RR_MAX,
};

#endif /* _DAL_VNDR_HAL_INTERNAL_H_ */
