// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 * This file implements the Modem APC2NCP Ring interfaces for the NOA Mediatek Modem Driver.
 */

#include "noa_md_apc2ncp_ring.h"

#include "ncp_md_irq.h"

#ifndef linux
//#include "hwio/hwio.h"

#define NOA_NCP_MEMORY_REMAP_OFFSET 0x20000000 // same with kApToModemMemoryReMapOffset
#define NOA_UL_DRB_RING_DESC_BASE 0x06AE0000 // same with kNoaUlDrbRingRegionOffset
#define NOA_DL_REFILL_RING_DESC_BASE 0x069B0000 // same with kNoaDlRefillDescRingRegionOffset
#endif

struct noa_md_apc2ncp_tx_buffer_desc apc2ncp_tx_buffer_desc[kNoaModemApcToNcpRingMax];
#ifdef linux
EXPORT_SYMBOL_GPL(apc2ncp_tx_buffer_desc);
#endif

const static struct noa_ring_ops noa_md_apc2ncp_ring_apc_ops = {
	.write_payload = noa_md_apc2ncp_desc_write,
	.complete_hook = noa_md_apc2ncp_trigger_doorbell,
};

const static struct noa_ring_ops noa_md_apc2ncp_ring_ncp_ops = {
	.read_payload = noa_generic_read_raw_pointer,
};

#define NOA_MODEM_TX_DRB_DESC_BYTE 16U // (sizeof(struct noa_modem_pd_drb)
#define NOA_MODEM_RX_REFILL_DESC_BYTE 20U // sizeof(struct noa_modem_rx_refill_desc)
#define NOA_MODEM_BUFF_SIZE 2048

const char *NoaModemApcToNcpRingName[kNoaModemApcToNcpRingMax] = {
	// 5 NOA Tx DRB Rings
	"apc2ncp_tx_drb0",
	"apc2ncp_tx_drb1",
	"apc2ncp_tx_drb2",
	"apc2ncp_tx_drb3",
	"apc2ncp_tx_drb4",
	// 4 NOA Rx Refill Rings
	"apc2ncp_rx_refill_normal_bat0",
	"apc2ncp_rx_refill_frag_bat0",
	"apc2ncp_rx_refill_normal_bat1",
	"apc2ncp_rx_refill_frag_bat1",
};
#ifdef linux
EXPORT_SYMBOL_GPL(NoaModemApcToNcpRingName);
#endif

u32 NoaModemApcToNcpRingSize[kNoaModemApcToNcpRingMax] = {
	// 5 NOA Tx DRB Rings, synced with the drb_cnt in the dpmaif_txq_cfg(txqs) defined in mtk_dpmaif_drv_t900.c
	2048, 2048, 128, 1024, 2048,
	// 4 NOA Rx Refill Rings, synced with the bat_cnt in the dpmaif_rxq_cfg(bats/frags) defined in mtk_dpmaif_drv_t900.c
	32768, // for normal_bat0
	1024, // for frag_bat0
	8192, // for normal_bat1
	1024, // for frag_bat1
};
#ifdef linux
EXPORT_SYMBOL_GPL(NoaModemApcToNcpRingSize);
#endif

struct noa_md_apc2ncp_tx_buffer_desc *noa_md_apc2ncp_get_tx_buffer_desc(void) {
	return apc2ncp_tx_buffer_desc;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_get_tx_buffer_desc);
#endif

ssize_t noa_md_apc2ncp_desc_write(void *output_buf, size_t buf_len, const void *input_data,
				  size_t data_len)
{
	if (buf_len < data_len)
		return -ENOMEM;
	memcpy(output_buf, input_data, data_len);

	return data_len;
}

void noa_md_apc2ncp_trigger_doorbell(struct noa_ring_wrapper *ring)
{
	u32 ring_type = ((struct noa_md_apc2ncp_ring_owner *)(ring->owner))->ring_type;

	APC2NCP_DATA("trigger doorbell for apc2ncp_ring[%d]", ring_type);
	/* Trigger NOA DMA scheduler to handle APC2NCP ring */
	if (noa_ring_pos_is_moved(ring)) {
		ncp_md_irq_set_ncp_intr_mask(ring_type);
		ncp_md_irq_notify_ncp();
	}
}

const char *noa_md_apc2ncp_get_ring_name(u32 ring_type)
{
	if (ring_type < 0 || ring_type >= kNoaModemApcToNcpRingMax ||
		NoaModemApcToNcpRingName[ring_type] == NULL) {
		return "unknown";
	}
	return NoaModemApcToNcpRingName[ring_type];
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_get_ring_name);
#endif

u32 noa_md_apc2ncp_get_ring_item_len(u32 ring_type, bool is_desc)
{
	// 5 NOA Tx DRB Rings
	if (kNoaModemRingTxDrb0 <= ring_type && ring_type <= kNoaModemRingTxDrb4) {
		return (is_desc) ? NOA_MODEM_TX_DRB_DESC_BYTE : NOA_MODEM_BUFF_SIZE;
	}

	// 4 NOA Rx Refill Rings
	if (kNoaModemRingRxRefillNormalBat0 <= ring_type &&
		ring_type <= kNoaModemRingRxRefillFragBat1) {
		return (is_desc) ? NOA_MODEM_RX_REFILL_DESC_BYTE : NOA_MODEM_RX_REFILL_DESC_BYTE;
	}

	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_get_ring_item_len);
#endif

u32 noa_md_apc2ncp_get_ring_size(u32 ring_type)
{
	if (ring_type < 0 || ring_type >= kNoaModemApcToNcpRingMax) {
		return 0;
	}
	return NoaModemApcToNcpRingSize[ring_type];
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_get_ring_size);
#endif

u64 NoaModemApcToNcpRingDescMemoryMap[kNoaModemApcToNcpRingMax] = {};
#ifdef linux
EXPORT_SYMBOL_GPL(NoaModemApcToNcpRingDescMemoryMap);
#endif
u64 NoaModemApcToNcpRingDescDpaMemoryMap[kNoaModemApcToNcpRingMax] = {};
#ifdef linux
EXPORT_SYMBOL_GPL(NoaModemApcToNcpRingDescDpaMemoryMap);
#endif
u64 NoaModemApcToNcpRingDmaAddrMemoryMap[kNoaModemApcToNcpRingMax] = {};
#ifdef linux
EXPORT_SYMBOL_GPL(NoaModemApcToNcpRingDmaAddrMemoryMap);
#endif

#ifndef linux
void noa_md_apc2ncp_ring_memory_map_setup_internal(u64 *mmap, u32 offset, u32 ring_type_start,
						   u32 ring_type_end, bool is_desc)
{
	u32 ring_type = 0;
	u32 cur_base = DRAM_FAKE_MODEM_MEMORY_BASE + offset;
	u32 *ring_size = NoaModemApcToNcpRingSize;
	for (ring_type = ring_type_start; ring_type <= ring_type_end; ring_type++) {
		mmap[ring_type] = cur_base;
		cur_base += noa_md_apc2ncp_get_ring_item_len(ring_type, is_desc) *
			    ring_size[ring_type];
	}
}
void noa_md_apc2ncp_ring_memory_map_setup(void)
{
	// for 5 NOA Tx DRB Rings
	noa_md_apc2ncp_ring_memory_map_setup_internal(NoaModemApcToNcpRingDescMemoryMap,
						      NOA_UL_DRB_RING_DESC_BASE,
						      kNoaModemRingTxDrb0, kNoaModemRingTxDrb4,
						      true);
	noa_md_apc2ncp_ring_memory_map_setup_internal(NoaModemApcToNcpRingDescDpaMemoryMap,
						      NOA_UL_DRB_RING_DESC_BASE,
						      kNoaModemRingTxDrb0, kNoaModemRingTxDrb4,
						      false);
	noa_md_apc2ncp_ring_memory_map_setup_internal(NoaModemApcToNcpRingDmaAddrMemoryMap,
						      NOA_UL_DRB_RING_DESC_BASE,
						      kNoaModemRingTxDrb0, kNoaModemRingTxDrb4,
						      false);

	// for 4 NOA Rx Refill Rings
	noa_md_apc2ncp_ring_memory_map_setup_internal(NoaModemApcToNcpRingDescMemoryMap,
						      NOA_DL_REFILL_RING_DESC_BASE,
						      kNoaModemRingRxRefillNormalBat0,
						      kNoaModemRingRxRefillFragBat1, true);
	noa_md_apc2ncp_ring_memory_map_setup_internal(NoaModemApcToNcpRingDescDpaMemoryMap,
						      NOA_DL_REFILL_RING_DESC_BASE,
						      kNoaModemRingRxRefillNormalBat0,
						      kNoaModemRingRxRefillFragBat1, false);
	noa_md_apc2ncp_ring_memory_map_setup_internal(NoaModemApcToNcpRingDmaAddrMemoryMap,
						      NOA_DL_REFILL_RING_DESC_BASE,
						      kNoaModemRingRxRefillNormalBat0,
						      kNoaModemRingRxRefillFragBat1, false);
}
#endif

static void *noa_md_apc2ncp_dma_alloc_coherent(u32 ring_type, dma_addr_t *skb_dma_addr,
					       void **desc_dpa_base)
{
	if (ring_type < 0 || ring_type >= kNoaModemApcToNcpRingMax) {
		return 0;
	}

	*desc_dpa_base = (void *)(uintptr_t)NoaModemApcToNcpRingDescDpaMemoryMap[ring_type];
	*skb_dma_addr = (uintptr_t)NoaModemApcToNcpRingDmaAddrMemoryMap[ring_type];
#ifdef NOA_DEBUG
	APC2NCP_INFO("apc2ncp_ring[%d]: desc_base=[0x%llx], skb_dma_addr=[0x%llx],"
		     " desc_dpa_base=[0x%llx]",
		     ring_type, NoaModemApcToNcpRingDescMemoryMap[ring_type], (u64)*skb_dma_addr,
		     (u64)*desc_dpa_base);
#endif
	return (void *)(uintptr_t)NoaModemApcToNcpRingDescMemoryMap[ring_type];
}

static int noa_md_apc2ncp_ring_apc_setup_internal(struct noa_md_apc2ncp_tx_buffer_desc *desc,
						  void *ring_owner, u32 ring_type,
						  const char *ring_name, u32 ring_item_len,
						  u32 ring_size)
{
	int ret;
	struct noa_ring_regs regs = {};
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.base = NULL,
		.item_len = ring_item_len,
		.size = ring_size,
		.dpa_base = NULL,
	};

	ret = NoaRingSharedRegsGet(&regs, kNoaNetworkInterfaceModemApcNcp,
				   kNoaNetworkFlowHostToDevice, ring_type, kNoaNepRingAnyDirection);
	if (ret) {
		APC2NCP_ERROR("apc2ncp_ring[%d]: NoaRingSharedRegsGet=[%d]", ring_type, ret);
		return ret;
	}
	noa_ring_regs_wrapper_init(&desc->ring, NOA_RING_TYPE_PRODUCER,
				   &noa_md_apc2ncp_ring_apc_ops, &regs, ring_owner, ring_name, 0);
	/*ring sw initial*/
#ifdef NOA_DEBUG
	APC2NCP_INFO("apc2ncp_ring[%d]: dma_alloc_coherent, info.size=[%d], info.item_len=[%d]",
		     ring_type, info.size, info.item_len);
#endif
	desc->desc_base = noa_md_apc2ncp_dma_alloc_coherent(ring_type, &desc->skb_dma_addr,
							    &desc->desc_dpa_base);
	if (!desc->desc_base || !desc->desc_dpa_base) {
		APC2NCP_ERROR("apc2ncp_ring[%d]: dma alloc fail, desc_base=[%p(0x%lx)],"
			      " desc_dpa_base=[%p(0x%lx)]",
			      ring_type, desc->desc_base, (uintptr_t)desc->desc_base,
			      desc->desc_dpa_base, (uintptr_t)desc->desc_dpa_base);
		return -ENOMEM;
	}
	APC2NCP_INFO("apc2ncp_ring[%d]: desc desc_base=[%p(0x%lx)], desc_dpa_base=[%p(0x%lx)],"
		     " skb_dma_addr=[%p(0x%lx)]",
		     ring_type, desc->desc_base, (uintptr_t)desc->desc_base, desc->desc_dpa_base,
		     (uintptr_t)desc->desc_dpa_base, (void *)desc->skb_dma_addr,
		     (uintptr_t)desc->skb_dma_addr);

	info.base = (char *)desc->desc_base;
	info.dpa_base = (char *)desc->desc_dpa_base;

	noa_ring_info_setup(&desc->ring, &info);
	noa_ring_activate(&desc->ring);
	// TODO: get remapped memory address per region and store it to ring shared info for NCP
#ifndef linux
	// for GEM5 NCP memory remapping
	desc->ring.data_ops->store_base(&desc->ring,
					(u64)(info.base - NOA_NCP_MEMORY_REMAP_OFFSET));
	desc->ring.data_ops->store_dpa_base(&desc->ring,
					(u64)(info.dpa_base - NOA_NCP_MEMORY_REMAP_OFFSET));
#endif
	return 0;
}

int noa_md_apc2ncp_ring_apc_setup(void *ring_owner)
{
	int ret = 0;
	struct noa_md_apc2ncp_tx_buffer_desc *desc = apc2ncp_tx_buffer_desc;

	APC2NCP_INFO("enter");

	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
#ifdef NOA_DEBUG
		APC2NCP_INFO("apc2ncp_ring[%d]: name=[%s], item_len=[%d], size=[%d]", ring_type,
			     noa_md_apc2ncp_get_ring_name(ring_type),
			     noa_md_apc2ncp_get_ring_item_len(ring_type, true),
			     noa_md_apc2ncp_get_ring_size(ring_type));
#endif
		desc->ring_owner.owner = ring_owner;
		desc->ring_owner.ring_type = ring_type;
		ret = noa_md_apc2ncp_ring_apc_setup_internal(
			&desc[ring_type], &desc->ring_owner, ring_type,
			noa_md_apc2ncp_get_ring_name(ring_type),
			noa_md_apc2ncp_get_ring_item_len(ring_type, true),
			noa_md_apc2ncp_get_ring_size(ring_type));

		if (ret) {
			APC2NCP_ERROR(
				"apc2ncp_ring[%d]: noa_md_apc2ncp_ring_apc_setup_internal=[%d]",
				ring_type, ret);
		}
	}
	return ret;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_ring_apc_setup);
#endif

void noa_md_apc2ncp_ring_apc_release(void)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = apc2ncp_tx_buffer_desc;

	APC2NCP_INFO("enter");

	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
		APC2NCP_INFO("noa_ring_deactivate for apc2ncp_ring[%d]", ring_type);
		noa_ring_deactivate(&desc[ring_type].ring);
	}
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_ring_apc_release);
#endif

static int noa_md_apc2ncp_ring_ncp_setup_internal(struct noa_md_apc2ncp_tx_buffer_desc *desc,
						  void *ring_owner, u32 ring_type,
						  const char *ring_name)
{
	int ret;
	struct noa_ring_regs regs = {};

	ret = NoaRingSharedRegsGet(&regs, kNoaNetworkInterfaceModemApcNcp,
				   kNoaNetworkFlowHostToDevice, ring_type, kNoaNepRingAnyDirection);
	if (ret) {
		APC2NCP_ERROR("apc2ncp_ring[%d]: NoaRingSharedRegsGet=[%d]", ring_type, ret);
		return ret;
	}
	noa_ring_regs_wrapper_init(&desc->ring, NOA_RING_TYPE_CONSUMER,
				   &noa_md_apc2ncp_ring_ncp_ops, &regs, ring_owner, ring_name, 0);

	noa_ring_activate(&desc->ring);
	desc->desc_base = desc->ring.basic.base;
	desc->skb_dma_addr = (dma_addr_t)desc->ring.basic.dpa_base;
	desc->desc_dpa_base = desc->ring.basic.dpa_base;
	desc->ring.basic.base = desc->ring.basic.dpa_base;
	if (!desc->desc_base || !desc->desc_dpa_base) {
		APC2NCP_ERROR("apc2ncp_ring[%d]: dma alloc fail, desc_base=[%p(0x%lx)],"
			      " desc_dpa_base=[%p(0x%lx)]",
			      ring_type, desc->desc_base, (uintptr_t)desc->desc_base,
			      desc->desc_dpa_base, (uintptr_t)desc->desc_dpa_base);
		return -ENOMEM;
	}
	APC2NCP_INFO("apc2ncp_ring[%d]: desc desc_base=[%p(0x%lx)],"
		     " desc_dpa_base=[%p(0x%lx)]",
		     ring_type, desc->desc_base, (uintptr_t)desc->desc_base, desc->desc_dpa_base,
		     (uintptr_t)desc->desc_dpa_base);
	return 0;
}

int noa_md_apc2ncp_ring_ncp_setup(void *ring_owner)
{
	int ret = 0;
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();

	APC2NCP_INFO("enter");

	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
#ifdef NOA_DEBUG
		APC2NCP_INFO("apc2ncp_ring[%d]: name=[%s], item_len=[%d], size=[%d]", ring_type,
			     noa_md_apc2ncp_get_ring_name(ring_type),
			     noa_md_apc2ncp_get_ring_item_len(ring_type, true),
			     noa_md_apc2ncp_get_ring_size(ring_type));
#endif
		desc->ring_owner.owner = ring_owner;
		desc->ring_owner.ring_type = ring_type;
		ret = noa_md_apc2ncp_ring_ncp_setup_internal(
			&desc[ring_type], &desc->ring_owner, ring_type,
			noa_md_apc2ncp_get_ring_name(ring_type));

		if (ret) {
			APC2NCP_ERROR(
				"apc2ncp_ring[%d]: noa_md_apc2ncp_ring_ncp_setup_internal=[%d]",
				ring_type, ret);
		}
	}
	return ret;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_ring_ncp_setup);
#endif

void noa_md_apc2ncp_ring_ncp_release(void)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();

	APC2NCP_INFO("enter");

	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
		APC2NCP_INFO("noa_ring_deactivate for apc2ncp_ring[%d]", ring_type);
		noa_ring_deactivate(&desc[ring_type].ring);
	}
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_ring_ncp_release);
#endif

int noa_md_apc2ncp_set_ring_write_idx(u32 q_num, u32 count)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_producer* ring = &desc[q_num].ring;
	u32 head;

	if (!is_noa_ring_activate(ring)) {
		APC2NCP_ERROR("noa ring not activate");
		return -EINVAL;

	}

	head = noa_ring_head_read_once(ring);
	head = noa_ring_move_pos(head, count, ring->basic.size);
	noa_ring_head_write_once(ring, head);
	APC2NCP_DATA("queue_id=[%d], head=[%d]", q_num, head);
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_set_ring_write_idx);
#endif

int noa_md_apc2ncp_get_ring_read_idx(u32 q_num)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_consumer* ring = &desc[q_num].ring;
	return noa_ring_tail_read_once(ring);
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_md_apc2ncp_get_ring_read_idx);
#endif

int noa_ncp_md_apc2ncp_set_ring_read_idx(u32 q_num, u32 count)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_consumer* ring = &desc[q_num].ring;
	u32 tail;

	if (!is_noa_ring_activate(ring)) {
		APC2NCP_ERROR("noa ring not activate");
		return -EINVAL;

	}

	tail = noa_ring_tail_read_once(ring);
        tail = noa_ring_move_pos(tail, count, ring->basic.size);
	noa_ring_tail_write_once(ring, tail);
	APC2NCP_DEBUG("queue_id=[%d], tail=[%d]", q_num, tail);
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ncp_md_apc2ncp_set_ring_read_idx);
#endif

int noa_ncp_md_apc2ncp_set_ring_index_by_type(u32 q_num,
					      enum noa_md_apc2ncp_ring_index_type type,
					      u32 ring_index)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_consumer* ring = &desc[q_num].ring;

	if (!is_noa_ring_activate(ring)) {
		APC2NCP_ERROR("noa ring not activate");
		return -EINVAL;
	}

	APC2NCP_DEBUG("queue_id=[%d], type=[%d], ring_index=[%d]", q_num, type, ring_index);

	switch (type) {
		case NOA_MD_RING_WRITE_INDEX:
			noa_ring_head_write_once(ring, ring_index);
			break;
		case NOA_MD_RING_READ_INDEX:
			noa_ring_tail_write_once(ring, ring_index);
			break;
		case NOA_MD_RING_TEMP_READ_INDEX:
			ring->basic.tail = ring_index;
			break;
		default:
			APC2NCP_ERROR("Unknown type: %d", type);
	}

	return 0;
}

u32 noa_ncp_md_apc2ncp_get_ring_index_by_type(u32 q_num,
					      enum noa_md_apc2ncp_ring_index_type type)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_consumer* ring = &desc[q_num].ring;

	APC2NCP_DEBUG("queue_id=[%d], type=[%d]", q_num, type);

	switch (type) {
		case NOA_MD_RING_WRITE_INDEX:
			return noa_ring_head_read_once(ring);
		case NOA_MD_RING_READ_INDEX:
			return noa_ring_tail_read_once(ring);
		case NOA_MD_RING_TEMP_READ_INDEX:
			return ring->basic.tail;
		default:
			APC2NCP_ERROR("Unknown type: %d", type);
	}

	return 0;
}
