/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _DAL_VNDR_HAL_API_H_
#define _DAL_VNDR_HAL_API_H_

#include <linux/types.h>
#include <linux/io.h>
#include "dal_vndr_hal_internal.h"
#include "dal_vndr_hal_defines_be.h"

#define DAL_VNDR_HAL_WBM2SW_RELEASE_SRC_GET(wbm_desc) (((*(((uint32_t *)wbm_desc) + \
		(WBM_RELEASE_RING_TX_RELEASE_SOURCE_MODULE_OFFSET >> 2))) & \
		 WBM_RELEASE_RING_TX_RELEASE_SOURCE_MODULE_MASK) >> \
		 WBM_RELEASE_RING_TX_RELEASE_SOURCE_MODULE_LSB)

/* sw_cookie is 20-bit field, MSB is bit 19 */
#define DAL_VNDR_HAL_TX_DESC_COOKIE_MSB_BIT	\
	(TCL_DATA_CMD_BUF_ADDR_INFO_SW_BUFFER_COOKIE_MSB - \
		TCL_DATA_CMD_BUF_ADDR_INFO_SW_BUFFER_COOKIE_LSB)
#define DAL_VNDR_HAL_TX_DESC_COOKIE_MSB_MASK \
	(1 << DAL_VNDR_HAL_TX_DESC_COOKIE_MSB_BIT)

/* Macro to set MSB bit of sw_cookie for buffer tracking */
#define DAL_VNDR_HAL_TX_DESC_COOKIE_SET_MSB(cookie) \
	((cookie) | DAL_VNDR_HAL_TX_DESC_COOKIE_MSB_MASK)

/* Macro to check if MSB bit is set in sw_cookie */
#define DAL_VNDR_HAL_TX_COMP_COOKIE_MSB_IS_SET(cookie) \
	((cookie) & DAL_VNDR_HAL_TX_DESC_COOKIE_MSB_MASK)

/**
 * dal_vndr_hal_tx_comp_get_desc_id_generic() - Get descriptor ID from Tx
 * completion descriptor
 * @hal_desc: Tx completion descriptor pointer
 *
 * This function extracts the descriptor ID (sw_buffer_cookie) from the
 * hardware completion descriptor.
 *
 * Return: Descriptor ID
 */
static inline uint32_t dal_vndr_hal_tx_comp_get_desc_id_generic(void *hal_desc)
{
	uint32_t comp_desc =
			*(uint32_t *)(((uint8_t *)hal_desc) +
				BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_OFFSET);

	/* Cookie is placed on 2nd word */
	return (comp_desc & BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_MASK) >>
			BUFFER_ADDR_INFO_SW_BUFFER_COOKIE_LSB;
}

/**
 * dal_vndr_hal_tx_comp_get_buffer_source_generic() - Get buffer release source
 * from Tx completion descriptor
 * @hal_desc: Tx completion descriptor pointer
 *
 * This function extracts the buffer release source module from the
 * hardware completion descriptor.
 *
 * Return: Buffer release source module
 */
static inline uint32_t dal_vndr_hal_tx_comp_get_buffer_source_generic(
	void *hal_desc)
{
	return DAL_VNDR_HAL_WBM2SW_RELEASE_SRC_GET(hal_desc);
}

/**
 * dal_vndr_hal_tx_comp_get_tx_status_generic() - Get Tx transmission status
 * @hal_desc: Tx completion descriptor pointer
 *
 * This function extracts the transmit status value from the Tx completion
 * descriptor (TQM release reason).
 *
 * Return: Transmit status value
 */
static inline uint8_t dal_vndr_hal_tx_comp_get_tx_status_generic(void *hal_desc)
{
	uint32_t comp_desc =
		*(uint32_t *)(((uint8_t *)hal_desc) +
			WBM_RELEASE_RING_TX_TQM_RELEASE_REASON_OFFSET);

	return (comp_desc & WBM_RELEASE_RING_TX_TQM_RELEASE_REASON_MASK) >>
			WBM_RELEASE_RING_TX_TQM_RELEASE_REASON_LSB;
}

/*
 * BAR + 4K is always accessible, any access outside this
 * space requires force wake procedure.
 * OFFSET = 4K - 32 bytes = 0xFE0
 */
#define MAPPED_REF_OFF 0xFE0

/**
 * dal_vndr_hal_write32_mb() - write a value to a register
 * @hal_soc: hal soc handle
 * @offset: offset from bar
 * @value: value to write
 */
static inline void dal_vndr_hal_write32_mb(
		struct dal_vndr_hal_soc *hal_soc, uint32_t offset,
		uint32_t value)
{
	/* Region < BAR + 4K can be directly accessed */
	if (offset < MAPPED_REF_OFF)
		iowrite32(value, hal_soc->dev_base_addr + offset);
}

/**
 * dal_vndr_hal_write_address_32_mb() - write a value to a register
 * @hal_soc: hal soc handle
 * @addr: I/O memory address to write
 * @value: value to write
 */
static inline void dal_vndr_hal_write_address_32_mb(
			struct dal_vndr_hal_soc *hal_soc,
			void *addr, uint32_t value)
{
	uint32_t offset;

	offset = addr - hal_soc->dev_base_addr;
	dal_vndr_hal_write32_mb(hal_soc, offset, value);
}

/**
 * dal_vndr_hal_srng_write_address_32_mb() - write to srng register
 * @hal_soc: hal soc handle
 * @srng: hal srng
 * @addr: I/O memory address to write
 * @value: value to write
 */
static inline void dal_vndr_hal_srng_write_address_32_mb(
					struct dal_vndr_hal_soc *hal_soc,
					struct dal_vndr_hal_srng *srng,
					void *addr,
					uint32_t value)
{
	dal_vndr_hal_write_address_32_mb(hal_soc, addr, value);
}

/**
 * dal_vndr_hal_srng_access_start() - Start ring access
 *
 * @hal_soc_hdl: Opaque HAL SOC handle
 * @hal_ring_hdl: Ring pointer (Source or Destination ring)
 *
 * This API doesn't implement any byte-order conversion on reading hp/tp.
 * So, Use API only for those srngs for which the target writes hp/tp values to
 * the DDR in the Host order.
 *
 * Return: 0 on success; error on failire
 */
static inline int dal_vndr_hal_srng_access_start(
			void *hal_soc_hdl,
			void *hal_ring_hdl)
{
	struct dal_vndr_hal_srng *srng =
		(struct dal_vndr_hal_srng *)hal_ring_hdl;

	if (srng->ring_dir == DAL_VNDR_HAL_SRNG_SRC_RING)
		srng->u.src_ring.cached_tp =
			*(volatile uint32_t *)(srng->u.src_ring.tp_addr);
	else
		srng->u.dst_ring.cached_hp =
			*(volatile uint32_t *)(srng->u.dst_ring.hp_addr);

	return 0;
}

static inline void
dal_vndr_hal_srng_sync_hp(void *hal_soc_hdl, void *hal_ring_hdl)
{
	struct dal_vndr_hal_srng *srng =
		(struct dal_vndr_hal_srng *)hal_ring_hdl;

	srng->u.src_ring.hp =
		*(volatile uint32_t *)(srng->u.src_ring.hp_addr);
}

/**
 * dal_vndr_hal_srng_dst_get_next() - Get next entry from a destination ring
 * @hal_soc: Opaque HAL SOC handle
 * @hal_ring_hdl: Destination ring pointer
 *
 * Return: Opaque pointer for next ring entry; NULL on failure
 */
static inline void *dal_vndr_hal_srng_dst_get_next(
		void *hal_soc,
		void *hal_ring_hdl)
{
	struct dal_vndr_hal_srng *srng =
			(struct dal_vndr_hal_srng *)hal_ring_hdl;
	uint32_t *desc;

	if (srng->u.dst_ring.tp == srng->u.dst_ring.cached_hp)
		return NULL;

	desc = &srng->ring_base_addr[srng->u.dst_ring.tp];

	srng->u.dst_ring.tp = (srng->u.dst_ring.tp + srng->entry_size);
	if (srng->u.dst_ring.tp == srng->ring_size)
		srng->u.dst_ring.tp = 0;

	return (void *)desc;
}

/**
 * dal_vndr_hal_srng_dst_num_valid() - Returns number of valid entries
 * (to be processed by SW) in destination ring
 * @hal_soc: Opaque HAL SOC handle
 * @hal_ring_hdl: Destination ring pointer
 * @sync_hw_ptr: Sync cached head pointer with HW
 *
 * Return: number of valid entries
 */
static inline uint32_t dal_vndr_hal_srng_dst_num_valid(
		void *hal_soc,
		void *hal_ring_hdl,
		int sync_hw_ptr)
{
	struct dal_vndr_hal_srng *srng =
			(struct dal_vndr_hal_srng *)hal_ring_hdl;
	uint32_t hp;
	uint32_t tp = srng->u.dst_ring.tp;

	if (sync_hw_ptr) {
		hp = *(volatile uint32_t *)(srng->u.dst_ring.hp_addr);
		srng->u.dst_ring.cached_hp = hp;
	} else {
		hp = srng->u.dst_ring.cached_hp;
	}

	if (hp >= tp)
		return (hp - tp) / srng->entry_size;

	return (srng->ring_size - tp + hp) / srng->entry_size;
}

/**
 * dal_vndr_hal_srng_src_num_avail() - Returns number of available entries in
 * src ring
 * @hal_soc: Opaque HAL SOC handle
 * @hal_ring_hdl: Source ring pointer
 * @sync_hw_ptr: Sync cached tail pointer with HW
 *
 * Return: number of available entries
 */
static inline uint32_t dal_vndr_hal_srng_src_num_avail(
		void *hal_soc,
		void *hal_ring_hdl, int sync_hw_ptr)
{
	struct dal_vndr_hal_srng *srng =
				(struct dal_vndr_hal_srng *)hal_ring_hdl;
	uint32_t tp;
	uint32_t hp = srng->u.src_ring.hp;

	if (sync_hw_ptr) {
		tp = *(srng->u.src_ring.tp_addr);
		srng->u.src_ring.cached_tp = tp;
	} else {
		tp = srng->u.src_ring.cached_tp;
	}

	if (tp > hp)
		return ((tp - hp) / srng->entry_size) - 1;
	else
		return ((srng->ring_size - hp + tp) / srng->entry_size) - 1;
}

/**
 * dal_vndr_hal_srng_src_get_next() - Get next entry from a source ring and
 * move cached tail pointer
 * @hal_soc: Opaque HAL SOC handle
 * @hal_ring_hdl: Source ring pointer
 *
 * Return: Opaque pointer for next ring entry; NULL on failure
 */
static inline void *dal_vndr_hal_srng_src_get_next(
			void *hal_soc,
			void *hal_ring_hdl)
{
	struct dal_vndr_hal_srng *srng =
			(struct dal_vndr_hal_srng *)hal_ring_hdl;
	uint32_t *desc;
	uint32_t next_hp = (srng->u.src_ring.hp + srng->entry_size) %
				srng->ring_size;

	if (next_hp != srng->u.src_ring.cached_tp) {
		desc = &(srng->ring_base_addr[srng->u.src_ring.hp]);
		srng->u.src_ring.hp = next_hp;
		return (void *)desc;
	}

	return NULL;
}

/**
 * dal_vndr_hal_srng_access_end() - Update cached ring
 *                          head/tail pointers to HW
 * @hal_soc: Opaque HAL SOC handle
 * @hal_ring_hdl: Ring pointer (Source or Destination ring)
 *
 * The target expects cached head/tail pointer to be updated to the
 * shared location in the little-endian order, This API ensures that.
 * This API should be used only if dal_vndr_hal_srng_access_start was used to
 * start ring access
 *
 */
static inline void dal_vndr_hal_srng_access_end(
		void *hal_soc, void *hal_ring_hdl)
{
	struct dal_vndr_hal_srng *srng =
				(struct dal_vndr_hal_srng *)hal_ring_hdl;

	if (srng->lmac_ring) {
		/* For LMAC rings, ring pointer updates are done through FW and
		 * hence written to a shared memory location that is read by FW
		 */
		if (srng->ring_dir == DAL_VNDR_HAL_SRNG_SRC_RING) {
			*srng->u.src_ring.hp_addr =
					cpu_to_le32(srng->u.src_ring.hp);
		} else {
			*srng->u.dst_ring.tp_addr =
					cpu_to_le32(srng->u.dst_ring.tp);
		}
	} else {
		if (srng->ring_dir == DAL_VNDR_HAL_SRNG_SRC_RING)
			dal_vndr_hal_srng_write_address_32_mb(
						hal_soc,
						srng,
						srng->u.src_ring.hp_addr,
						srng->u.src_ring.hp);
		else
			dal_vndr_hal_srng_write_address_32_mb(
						hal_soc,
						srng,
						srng->u.dst_ring.tp_addr,
						srng->u.dst_ring.tp);
	}
}

/**
 * dal_vndr_hal_rxdma_buff_addr_info_set() - set the buffer_addr_info of the
 *				    rxdma ring entry.
 * @hal_soc_hdl: hal soc handle
 * @rxdma_entry: descriptor entry
 * @paddr: physical address of nbuf data pointer.
 * @cookie: SW cookie used as a index to SW rx desc.
 * @manager: who owns the nbuf (host, NSS, etc...).
 *
 */
static inline void dal_vndr_hal_rxdma_buff_addr_info_set(
					void *hal_soc_hdl,
					void *rxdma_entry,
					dma_addr_t paddr,
					uint32_t cookie,
					uint8_t manager)
{
	struct dal_vndr_hal_soc *hal_soc =
		(struct dal_vndr_hal_soc *)hal_soc_hdl;
	hal_soc->ops->dal_vndr_hal_rxdma_buff_addr_info_set(rxdma_entry,
							    paddr,
							    cookie, manager);
}
/**
 * dal_vndr_hal_tx_desc_sync() - Commit the descriptor to Hardware
 * @hal_tx_desc_cached: Cached descriptor that software maintains
 * @hw_desc: Hardware descriptor to be updated
 * @num_bytes: descriptor size
 */
static inline void dal_vndr_hal_tx_desc_sync(
			void *hal_tx_desc_cached,
			void *hw_desc, uint8_t num_bytes)
{
	if (!num_bytes)
		return;

	if (!hal_tx_desc_cached || !hw_desc)
		return;

	memcpy(hw_desc, hal_tx_desc_cached, num_bytes);
}

/**
 * dal_vndr_hal_srng_dst_dec_tp() - decrement the TP of the Dst ring by one
 * @hal_soc: Opaque HAL SOC handle
 * @hal_ring_hdl: Destination ring pointer
 *
 * reset the tail pointer in the destination ring by one entry
 *
 */
static inline void dal_vndr_hal_srng_dst_dec_tp(
			void *hal_soc, void *hal_ring_hdl)
{
	struct dal_vndr_hal_srng *srng =
				(struct dal_vndr_hal_srng *)hal_ring_hdl;

	if (!srng->u.dst_ring.tp)
		srng->u.dst_ring.tp = (srng->ring_size - srng->entry_size);
	else
		srng->u.dst_ring.tp -= srng->entry_size;
}

/**
 * dal_vndr_hal_tx_desc_set_lmac_id() - Set LMAC ID in Tx descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @lmac_id: LMAC ID value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_lmac_id(
				void *hal_soc_hdl,
				void *desc, uint8_t lmac_id)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_lmac_id(desc, lmac_id);
}

/**
 * dal_vndr_hal_tx_desc_set_buf_addr() - Set buffer address in Tx descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @paddr: Physical buffer address
 * @rbm_id: rbm ID
 * @desc_id: Descriptor ID
 * @type: Buffer type
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_buf_addr(
				void *hal_soc_hdl,
				void *desc, dma_addr_t paddr,
				uint8_t rbm_id, uint32_t desc_id,
				uint8_t type)
{
	struct dal_vndr_hal_soc *hal_soc =
					(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_buf_addr(desc, paddr, rbm_id,
							desc_id, type);
}

/**
 * dal_vndr_hal_tx_desc_set_buf_length() - Set buffer length in Tx descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @data_length: Buffer data length
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_buf_length(
				void *hal_soc_hdl,
				void *desc, uint16_t data_length)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_buf_length(desc, data_length);
}

/**
 * dal_vndr_hal_tx_desc_set_buf_offset() - Set buffer offset in Tx descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @offset: Buffer offset value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_buf_offset(
				void *hal_soc_hdl, void *desc, uint8_t offset)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_buf_offset(desc, offset);
}

/**
 * dal_vndr_hal_tx_desc_set_l3_checksum_en() - Enable L3 checksum offload
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @en: Enable flag (1 = enable, 0 = disable)
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_l3_checksum_en(
			void *hal_soc_hdl, void *desc, uint8_t en)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_l3_checksum_en(desc, en);
}

/**
 * dal_vndr_hal_tx_desc_set_l4_checksum_en() - Enable L4 checksum offload
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @en: Enable flag (1 = enable, 0 = disable)
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_l4_checksum_en(
				void *hal_soc_hdl, void *desc, uint8_t en)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_l4_checksum_en(desc, en);
}

/**
 * dal_vndr_hal_tx_desc_set_bank_id() - Set bank ID in Tx descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @bank_id: Bank ID value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_bank_id(
			void *hal_soc_hdl, void *desc, uint8_t bank_id)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_bank_id(desc, bank_id);
}

/**
 * dal_vndr_hal_tx_desc_set_vdev_id() - Set VDEV ID in Tx descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @vdev_id: VDEV ID value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_vdev_id(
			void *hal_soc_hdl, void *desc, uint8_t vdev_id)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_vdev_id(desc, vdev_id);
}

/**
 * dal_vndr_hal_tx_desc_set_hlos_tid() - Set HLOS TID in Tx descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @hlos_tid: HLOS TID value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_hlos_tid(
			void *hal_soc_hdl, void *desc, uint8_t hlos_tid)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_hlos_tid(desc, hlos_tid);
}

/**
 * dal_vndr_hal_tx_desc_set_flow_override_enable() - Enable flow override
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @val: Enable value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_flow_override_enable(
				void *hal_soc_hdl, void *desc, uint8_t val)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_flow_override_enable(desc, val);
}

/**
 * dal_vndr_hal_tx_desc_set_flow_override() - Set flow override value
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @val: Flow override value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_flow_override(
				void *hal_soc_hdl, void *desc, uint8_t val)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_flow_override(desc, val);
}

/**
 * dal_vndr_hal_tx_desc_set_who_classify_info_sel() - Set WHO classify info
 * selector
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @val: WHO classify info selector value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_who_classify_info_sel(
				void *hal_soc_hdl, void *desc, uint8_t val)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_who_classify_info_sel(desc, val);
}

/**
 * dal_vndr_hal_tx_desc_set_tx_notify_frame() - Set Tx notify frame flag
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx descriptor pointer
 * @val: Notify frame flag value
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_desc_set_tx_notify_frame(
				void *hal_soc_hdl, void *desc, uint8_t val)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_desc_set_tx_notify_frame(desc, val);
}

/**
 * dal_vndr_hal_tx_comp_get_status() - Get Tx completion status
 * @hal_soc_hdl: HAL SoC handle
 * @desc: Tx completion descriptor pointer
 * @ts1: Tx status structure pointer
 *
 * Return: void
 */
static inline void dal_vndr_hal_tx_comp_get_status(
			void *hal_soc_hdl, void *desc, void *ts1)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_tx_comp_get_status(desc, ts1);
}

/**
 * dal_vndr_hal_rx_error_status_get() - Get Rx error status
 * @hal_soc_hdl: HAL SoC handle
 * @reo_desc: REO descriptor pointer
 *
 * Return: Error status value
 */
static inline uint8_t dal_vndr_hal_rx_error_status_get(
			void *hal_soc_hdl, void *reo_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_error_status_get(reo_desc);
}

/**
 * dal_vndr_hal_rx_reo_buf_cookie_get() - Get REO buffer cookie
 * @hal_soc_hdl: HAL SoC handle
 * @reo_desc: REO descriptor pointer
 *
 * Return: Buffer cookie value
 */
static inline uint32_t dal_vndr_hal_rx_reo_buf_cookie_get(
			void *hal_soc_hdl, void *reo_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_reo_buf_cookie_get(reo_desc);
}

/**
 * dal_vndr_hal_rx_ret_buf_manager_get() - Get return buffer manager ID
 * @hal_soc_hdl: HAL SoC handle
 * @ring_desc: Ring descriptor pointer
 *
 * Return: Buffer manager ID
 */
static inline uint8_t dal_vndr_hal_rx_ret_buf_manager_get(
				void *hal_soc_hdl, void *ring_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_ret_buf_manager_get(ring_desc);
}

/**
 * dal_vndr_hal_rx_reo_get_details() - Get REO buffer details
 * @hal_soc_hdl: HAL SoC handle
 * @ring_desc: Ring descriptor pointer
 * @buf_info: Buffer info structure pointer to be filled
 *
 * Return: void
 */
static inline void dal_vndr_hal_rx_reo_get_details(
			void *hal_soc_hdl, void *ring_desc,
			struct dal_vndr_hal_buf_info *buf_info)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_rx_reo_get_details(ring_desc, buf_info);
}

/**
 * dal_vndr_hal_rx_msdu_start_nss_get() - Get NSS from MSDU start
 * @hal_soc_hdl: HAL SoC handle
 * @buf: Buffer pointer
 *
 * Return: NSS value
 */
static inline uint32_t dal_vndr_hal_rx_msdu_start_nss_get(void *hal_soc_hdl,
							  uint8_t *buf)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_msdu_start_nss_get(buf);
}

/**
 * dal_vndr_hal_rx_tlv_sgi_get() - Get SGI from Rx TLV
 * @hal_soc_hdl: HAL SoC handle
 * @buf: Buffer pointer
 *
 * Return: SGI value
 */
static inline uint32_t dal_vndr_hal_rx_tlv_sgi_get(
				void *hal_soc_hdl, uint8_t *buf)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_tlv_sgi_get(buf);
}

/**
 * dal_vndr_hal_rx_tlv_rate_mcs_get() - Get rate MCS from Rx TLV
 * @hal_soc_hdl: HAL SoC handle
 * @buf: Buffer pointer
 *
 * Return: Rate MCS value
 */
static inline uint32_t dal_vndr_hal_rx_tlv_rate_mcs_get(
				void *hal_soc_hdl, uint8_t *buf)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_tlv_rate_mcs_get(buf);
}

/**
 * dal_vndr_hal_rx_tlv_bw_get() - Get bandwidth from Rx TLV
 * @hal_soc_hdl: HAL SoC handle
 * @buf: Buffer pointer
 *
 * Return: Bandwidth value
 */
static inline uint32_t dal_vndr_hal_rx_tlv_bw_get(
				void *hal_soc_hdl, uint8_t *buf)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_tlv_bw_get(buf);
}

/**
 * dal_vndr_hal_rx_tlv_get_pkt_type() - Get packet type from Rx TLV
 * @hal_soc_hdl: HAL SoC handle
 * @buf: Buffer pointer
 *
 * Return: Packet type value
 */
static inline uint32_t dal_vndr_hal_rx_tlv_get_pkt_type(
				void *hal_soc_hdl, uint8_t *buf)
{
	struct dal_vndr_hal_soc *hal_soc =
				 (struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_tlv_get_pkt_type(buf);
}

/**
 * dal_vndr_hal_rx_get_l3_pad_bytes() - Get L3 padding bytes
 * @hal_soc_hdl: HAL SoC handle
 * @buf: Buffer pointer
 *
 * Return: L3 padding bytes value
 */
static inline uint8_t dal_vndr_hal_rx_get_l3_pad_bytes(
				void *hal_soc_hdl, uint8_t *buf)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_get_l3_pad_bytes(buf);
}

/**
 * dal_vndr_hal_rx_tlv_msdu_done_get() - Get MSDU done flag from Rx TLV
 * @hal_soc_hdl: HAL SoC handle
 * @buf: Buffer pointer
 *
 * Return: MSDU done flag value
 */
static inline uint32_t dal_vndr_hal_rx_tlv_msdu_done_get(
				void *hal_soc_hdl, uint8_t *buf)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_tlv_msdu_done_get(buf);
}

/**
 * dal_vndr_hal_rx_reo_buf_paddr_get() - Get REO buffer physical address
 * @hal_soc_hdl: HAL SoC handle
 * @ring_desc: Ring descriptor pointer
 *
 * Return: Buffer physical address
 */
static inline dma_addr_t dal_vndr_hal_rx_reo_buf_paddr_get(void *hal_soc_hdl,
							   void *ring_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_rx_reo_buf_paddr_get(ring_desc);
}

/**
 * dal_vndr_hal_tx_comp_get_paddr() - Get physical address from Tx
 * completion descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @hal_desc: Tx completion descriptor pointer
 *
 * This function extracts the buffer physical address from the hardware
 * completion descriptor.
 *
 * Return: Buffer physical address
 */
static inline dma_addr_t dal_vndr_hal_tx_comp_get_paddr(void *hal_soc_hdl,
							void *hal_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_tx_comp_get_paddr(hal_desc);
}

/**
 * dal_vndr_hal_tx_comp_get_desc_id() - Get descriptor ID from Tx
 * completion descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @hal_desc: Tx completion descriptor pointer
 *
 * This function extracts the descriptor ID (sw_buffer_cookie) from the
 * hardware completion descriptor.
 *
 * Return: Descriptor ID
 */
static inline uint32_t dal_vndr_hal_tx_comp_get_desc_id(void *hal_soc_hdl,
							void *hal_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_tx_comp_get_desc_id(hal_desc);
}

/**
 * dal_vndr_hal_tx_comp_get_buffer_source() - Get buffer release source from
 * Tx completion descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @hal_desc: Tx completion descriptor pointer
 *
 * This function extracts the buffer release source module from the
 * hardware completion descriptor.
 *
 * Return: Buffer release source module
 */
static inline uint32_t dal_vndr_hal_tx_comp_get_buffer_source(
						void *hal_soc_hdl,
						void *hal_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_tx_comp_get_buffer_source(hal_desc);
}

/**
 * dal_vndr_hal_tx_comp_get_tx_status() - Get Tx transmission status
 * @hal_soc_hdl: HAL SoC handle
 * @hal_desc: Tx completion descriptor pointer
 *
 * This function extracts the transmit status value from the Tx completion
 * descriptor (TQM release reason).
 *
 * Return: Transmit status value
 */
static inline uint8_t dal_vndr_hal_tx_comp_get_tx_status(void *hal_soc_hdl,
							 void *hal_desc)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	return hal_soc->ops->dal_vndr_hal_tx_comp_get_tx_status(hal_desc);
}

/**
 * dal_vndr_hal_rx_msdu_desc_info_get() - Get MSDU descriptor info from REO
 * ring descriptor
 * @hal_soc_hdl: HAL SoC handle
 * @desc_addr: REO ring descriptor address
 * @msdu_desc_info: Pointer to structure to be filled with MSDU descriptor info
 *
 * This function extracts MSDU descriptor information including flags
 * (first_msdu_in_mpdu, last_msdu_in_mpdu, msdu_continuation) and MSDU length
 * from the REO destination ring descriptor.
 *
 * Return: void
 */
static inline void dal_vndr_hal_rx_msdu_desc_info_get(
			void *hal_soc_hdl, void *desc_addr,
			struct dal_vndr_hal_rx_msdu_desc_info *msdu_desc_info)
{
	struct dal_vndr_hal_soc *hal_soc =
			(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_rx_msdu_desc_info_get(desc_addr,
							 msdu_desc_info);
}

/**
 * dal_vndr_hal_rxbm_sync() - Sync the buff addr info to refill ring
 * @hal_soc_hdl: HAL SoC handle
 * @ring_desc: Ring descriptor pointer
 * @buff_addr_info: Buffer address info pointer
 *
 * This function syncs the buffer address info to the refill ring.
 *
 * Return: void
 */
static inline void dal_vndr_hal_rxbm_sync(void *hal_soc_hdl, void *ring_desc,
					  void *buff_addr_info)
{
	struct dal_vndr_hal_soc *hal_soc =
				(struct dal_vndr_hal_soc *)hal_soc_hdl;

	hal_soc->ops->dal_vndr_hal_rxbm_sync(ring_desc, buff_addr_info);
}

/**
 * dal_vndr_hal_get_sw_hptp() - Get SW head and tail pointer for any ring
 * @hal_soc: Opaque HAL SOC handle
 * @hal_ring_hdl: Source ring pointer
 * @tailp: Tail Pointer
 * @headp: Head Pointer
 *
 * Return: Update tail pointer and head pointer in arguments.
 */
static inline
void dal_vndr_hal_get_sw_hptp(void *hal_soc, void *hal_ring_hdl,
			      uint32_t *tailp, uint32_t *headp)
{
	struct dal_vndr_hal_srng *srng =
			(struct dal_vndr_hal_srng *)hal_ring_hdl;

	if (srng->ring_dir == DAL_VNDR_HAL_SRNG_SRC_RING) {
		*headp = srng->u.src_ring.hp;
		*tailp = *srng->u.src_ring.tp_addr;
	} else {
		*tailp = srng->u.dst_ring.tp;
		*headp = *srng->u.dst_ring.hp_addr;
	}
}

/**
 * dal_vndr_hal_ops_attach() - Attach HAL operations
 * @hal_soc: HAL SoC handle
 *
 * This function attaches the default HAL operations.
 *
 * Return: void
 */
void dal_vndr_hal_ops_attach(void *hal_soc);

#endif /* _DAL_VNDR_HAL_API_H_ */
