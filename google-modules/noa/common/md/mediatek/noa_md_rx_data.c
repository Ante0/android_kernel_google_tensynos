// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA MD Driver
 *
 * Copyright 2024 Google LLC.
 */

#include "noa_md.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_dma_mapper.h"
#include "noa_md_dpa.h"
#include "noa_md_dpmaif.h"
#include "noa_md_rx_data.h"
#include "noa_md_wrapper_dpmaif.h"
#include "ncp/md/mediatek/ncp_md.h"
#include "ncp/md/mediatek/ncp_md_rx_data.h"
#include "t900/noa_md_mtk_priv_dpmaif.h"
#include "common/ring_id.h"
#include "common/modem_ring_id.h"

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "noa_md_apc2ncp_ring_fullsoc.h"
#elif IS_ENABLED(CONFIG_NOA_SIM_SUPPORT)
#include "noa_md_apc2ncp_ring.h"
#endif

const char *NoaApcSwRxName[kNoaModemRingRxDataEnd] = {
	// 4 NOA Modem SW input Rings
	"apc_modem_sw_default",
	"apc_modem_sw_rx_0",
	"apc_modem_sw_rx_1",
	"apc_modem_sw_rx_2",
};

const static struct noa_ring_ops noa_md_rx_sw_ring_ops = {
	.read_payload = noa_generic_read_raw_pointer,
};

/* Trigger irq_rx_done */
void noa_md_rx_isr_work(struct work_struct *work)
{
	struct noa_md_dpmaif_ops* noa_dpmaif_ops = noa_md_wpr_dpmaif_get_dpmaif_ops();
	struct noa_ring_wrapper *ring;

	if (!md_dev.rx.rx_buffer_desc) {
		NOA_MD_RX_ERROR("rx_buffer_desc is NULL");
		return;
	}

	// multiple noa rx rings
	for (int q_id = 0; q_id < kNoaModemRingRxDataEnd; q_id++) {
		ring = &md_dev.rx.rx_buffer_desc[q_id].ring;
		if (!noa_ring_is_empty(ring) && q_id > 0) {
			// Send irq_rx_done with q_id
			NOA_MD_RX_DATA("q_id:%d", q_id);
			noa_dpmaif_ops->irq_rx_done(md_dev.noa_dcb, q_id - kNoaModemRingRxq0);
		} else {
			NOA_MD_RX_DATA("noa_ring_is_empty:%d, q_id: %d",
				noa_ring_is_empty(ring), q_id);
		}
	}
}

irqreturn_t noa_md_rx_isr(int id, void *data)
{
	struct noa_md_dev *t_md_dev = (struct noa_md_dev*)data;
	NOA_MD_RX_DATA("enter");

	// clear ints first, then schedule a button-helf task
	writel(0, (void *) t_md_dev->rx.noa_hw_rx_ints_addr);

	// queue work which is processed by noa_md_rx_isr_work
	queue_work(t_md_dev->rx.isr_wq, &t_md_dev->rx.isr_work);
	return IRQ_HANDLED;
}

static void noa_md_rx_nep2apc_isr(int id, void *data)
{
	struct noa_md_dev *t_md_dev = (struct noa_md_dev*)data;
	NOA_MD_RX_DATA("enter, id=[%d]", id);

	// queue work which is processed by noa_md_rx_isr_work
	queue_work(t_md_dev->rx.isr_wq, &t_md_dev->rx.isr_work);
}

void noa_md_rx_unmap_modem_dpa_bat(int bat_id, int ring_type,
	unsigned short mapped_rx_tkid)
{
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_bat_ring *noa_bat_ring;

	union dpmaif_bat_record *cur_bat_record;
	struct skb_mapped_t *skb_info;
	struct page_mapped_t *page_info;

	NOA_MD_RX_DATA("bat_id=[%d], ring_type=[%u], mapped_rx_tkid=[%u]",
		bat_id, ring_type, mapped_rx_tkid);

	noa_dcb = md_dev.noa_dcb;
	CHECK_PTR_OR_RETURN(noa_dcb);

	if (ring_type == DPMAIF_BAT) {
		noa_bat_ring = &noa_dcb->bat_infos[bat_id].normal_bat_ring;
	} else {
		noa_bat_ring = &noa_dcb->bat_infos[bat_id].frag_bat_ring;
	}
	CHECK_PTR_OR_RETURN(noa_bat_ring);

	CHECK_PTR_OR_RETURN(noa_bat_ring->sw_record_base);
	cur_bat_record = noa_bat_ring->sw_record_base + mapped_rx_tkid;
	CHECK_PTR_OR_RETURN(cur_bat_record);

	if (noa_bat_ring->type == NORMAL_BAT) {
		skb_info = &cur_bat_record->normal;
		CHECK_PTR_OR_RETURN(skb_info);
		CHECK_PTR_OR_RETURN(skb_info->skb);
		CHECK_PTR_OR_RETURN(skb_info->skb->data);
		noa_md_dma_unmap_by_addr(&md_dev.rx.mapper, (void *)skb_info->skb->data);
	} else {
		page_info = &cur_bat_record->frag;
		CHECK_PTR_OR_RETURN(page_info);
		CHECK_PTR_OR_RETURN(page_info->page);
		noa_md_dma_unmap_by_addr(&md_dev.rx.mapper, (void *)page_info->page);
	}
}

// Refer to mtk_dpmaif_alloc_skb
static int noa_md_rx_alloc_rx_skb(
	struct mtk_dpmaif_ctlb *dcb,
	struct dpmaif_bat_ring *bat_ring,
	unsigned short bat_idx)
{
	union dpmaif_bat_record *cur_bat_record;
	struct skb_mapped_t *skb_info;
	struct dpmaif_bat *cur_bat;
	struct noa_md_rx_tkid_info *tkid_info;
	unsigned short rx_tkid;
	unsigned short mapped_rx_tkid;
	unsigned short next_rx_tkid_free_fore;
	int ret;
	struct noa_md_fw *md_fw = md_dev.md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);
	struct noa_md_fw_rx *rx = md_fw->rx;
	CHECK_PTR_OR_RETURN_ERR(rx, -EINVAL);
	struct noa_bat_info *fw_bat_info = &rx->bat_infos[bat_ring->id];
	CHECK_PTR_OR_RETURN_ERR(fw_bat_info, -EINVAL);
	struct noa_bat_ring *fw_bat_ring = &fw_bat_info->normal_bat_ring;
	CHECK_PTR_OR_RETURN_ERR(fw_bat_ring, -EINVAL);

	if (bat_ring->type == NORMAL_BAT)
		tkid_info = &md_dev.rx.normal_tkid_infos[bat_ring->id];
	else if (bat_ring->type == FRAG_BAT)
		tkid_info = &md_dev.rx.frag_tkid_infos[bat_ring->id];
	else {
		NOA_MD_RX_ERROR("wrong bat_ring->type=%d", bat_ring->type);
		return -EINVAL;
	}

	// Allocate rx tkid free pool
	rx_tkid = tkid_info->free_pool[tkid_info->rx_tkid_free_fore];
	next_rx_tkid_free_fore =
		mtk_dpmaif_ring_buf_get_next_idx(bat_ring->bat_cnt, tkid_info->rx_tkid_free_fore);
	if (next_rx_tkid_free_fore == tkid_info->rx_tkid_free_rear) {
		NOA_MD_RX_ERROR("[%s][%d]RxTkid is out of use at Q(%d), free fore(%d), rear(%d)!",
			__func__, __LINE__,
			bat_ring->id,
			tkid_info->rx_tkid_free_fore,
			tkid_info->rx_tkid_free_rear);
		return -ENOMEM;
	}

	mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(bat_ring->id, DPMAIF_BAT, rx_tkid);
	cur_bat_record = bat_ring->sw_record_base + mapped_rx_tkid;
	skb_info = &cur_bat_record->normal;

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	/*if (skb_info->skb){
		NOA_MD_RX_ERROR("[%s][%d] TODO: recycle rx tkid!",
			__func__, __LINE__);
		return 0;
	}*/

	skb_info->skb = __dev_alloc_skb(bat_ring->buf_size, GFP_ATOMIC);
	if (unlikely(!skb_info->skb)) {
		NOA_MD_RX_ERROR("bat_idx=[%u] failed to allocate skb ", bat_idx);
		return -ENOMEM;
	}

	skb_info->data_len = bat_ring->buf_size;
	skb_info->data_dma_addr = dma_map_single(DCB_TO_DEV(dcb), skb_info->skb->data,
						 skb_info->data_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(DCB_TO_DEV(dcb), skb_info->data_dma_addr)) {
		NOA_MD_RX_ERROR("Failed to map dma");
		dev_kfree_skb_any(skb_info->skb);
		skb_info->skb = NULL;
		return -ENOMEM;
	}

	// Remap data buffer for dpa view
	ret = noa_md_dma_mapper_remap_sg(
		&md_dev.rx.mapper,
		DCB_TO_DEV(dcb),  /* Source device is the DPMAIF device */
		md_dev.dpa_res->dpa_dev,  /* Target device is the DPA */
		(void *)skb_info->skb->data,
		skb_info->data_dma_addr,
		skb_info->data_len,
		&fw_bat_ring->noa_data_addr_apc[mapped_rx_tkid].noa_va,
		GFP_ATOMIC);
	if (ret) {
		NOA_MD_RX_ERROR("Fail to remap for modem normal bat[%d] buffer[%d]",
			bat_ring->id, mapped_rx_tkid);
		dma_unmap_single(DCB_TO_DEV(dcb), skb_info->data_dma_addr, skb_info->data_len,
			DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb_info->skb);
		skb_info->skb = NULL;
		return -ENOMEM;
	}

	// After the above process succeeds, update the free pool index and rx_tkid mapping table
	tkid_info->rx_tkid_free_fore = next_rx_tkid_free_fore;
	tkid_info->rx_tkid[bat_idx] = rx_tkid;
	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(skb_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(skb_info->data_dma_addr));

	/* noa rx path does not need to check bat_ring->mask_tbl.
	* Because the APC bat_idx is not related to rx_tkid mapping,
	* we need to set the bit back of the bat_idx to avoid
	* clearing the bit during the previous reload_rx_buff.
	* This could cause data transfer to stall because the buffer cannot be replenished.
	*/
	set_bit(bat_idx, bat_ring->mask_tbl);

	NOA_MD_RX_DATA("bat_idx=[%u], rx_tkid=[%u] "
		       "skb=[0x%pK] skb(%pK,%pK,%u,%u), "
		       "data_dma_addr=[0x%llx]",
		       bat_idx, rx_tkid, skb_info->skb,
		       skb_info->skb->head, skb_info->skb->data,
		       skb_info->skb->tail, skb_info->skb->end,
		       skb_info->data_dma_addr);

	return 0;
}

static int noa_md_rx_alloc_rx_page(
	struct mtk_dpmaif_ctlb *dcb,
	struct dpmaif_bat_ring *bat_ring,
	unsigned short bat_idx)
{
	union dpmaif_bat_record *cur_bat_record;
	struct page_mapped_t *page_info;
	struct dpmaif_bat *cur_bat;
	struct noa_md_rx_tkid_info *tkid_info;
	void *data;
	unsigned short rx_tkid;
	unsigned short mapped_rx_tkid;
	unsigned short next_rx_tkid_free_fore;
	int ret;
	struct noa_md_fw *md_fw = md_dev.md_fw;
	struct noa_md_fw_rx *rx = md_fw->rx;
	struct noa_bat_info *fw_bat_info = &rx->bat_infos[bat_ring->id];
	struct noa_bat_ring *fw_bat_ring = &fw_bat_info->frag_bat_ring;

	if (bat_ring->type == NORMAL_BAT)
		tkid_info = &md_dev.rx.normal_tkid_infos[bat_ring->id];
	else if (bat_ring->type == FRAG_BAT)
		tkid_info = &md_dev.rx.frag_tkid_infos[bat_ring->id];
	else {
		NOA_MD_RX_ERROR("wrong bat_ring->type=%d", bat_ring->type);
		return -EINVAL;
	}

	// Allocate rx tkid free pool
	rx_tkid = tkid_info->free_pool[tkid_info->rx_tkid_free_fore];
	next_rx_tkid_free_fore =
		mtk_dpmaif_ring_buf_get_next_idx(bat_ring->bat_cnt, tkid_info->rx_tkid_free_fore);
	if (next_rx_tkid_free_fore == tkid_info->rx_tkid_free_rear) {
		NOA_MD_RX_ERROR("[%s][%d]RxTkid is out of use at Q(%d), free fore(%d), rear(%d)!",
			__func__, __LINE__,
			bat_ring->id,
			tkid_info->rx_tkid_free_fore,
			tkid_info->rx_tkid_free_rear);
		return -ENOMEM;
	}

	mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(bat_ring->id, DPMAIF_BAT, rx_tkid);
	cur_bat_record = bat_ring->sw_record_base + mapped_rx_tkid;
	page_info = &cur_bat_record->frag;

	/* For re-init flow, we don't release
	 * the rx buffer on FSM_STATE_OFF state.
	 * because we will pin rx buffers to BAT entries
	 * again on FSM_STATE_BOOTUP state,
	 * and also in bat preload case.
	 */
	/*if (page_info->page) {
		NOA_MD_RX_ERROR("[%s][%d] TODO: recycle rx tkid!",
			__func__, __LINE__);
		return 0;
	}*/

	data = netdev_alloc_frag(bat_ring->buf_size);
	if (unlikely(!data))
		return -ENOMEM;

	page_info->page = virt_to_head_page(data);
	page_info->offset = data - page_address(page_info->page);
	page_info->data_len = bat_ring->buf_size;
	page_info->data_dma_addr = dma_map_page(DCB_TO_DEV(dcb), page_info->page,
						page_info->offset, page_info->data_len,
						DMA_FROM_DEVICE);

	if (dma_mapping_error(DCB_TO_DEV(dcb), page_info->data_dma_addr)) {
		NOA_MD_RX_ERROR("Failed to map dma!");
		put_page(page_info->page);
		page_info->page = NULL;
		return -ENOMEM;
	}

	// Remap data buffer for dpa view
	ret = noa_md_dma_mapper_remap_sg(
		&md_dev.rx.mapper,
		DCB_TO_DEV(dcb),  /* Source device is the DPMAIF device */
		md_dev.dpa_res->dpa_dev,  /* Target device is the DPA */
		(void *)page_info->page,
		page_info->data_dma_addr,
		page_info->data_len ,
		&fw_bat_ring->noa_data_addr_apc[mapped_rx_tkid].noa_va,
		GFP_ATOMIC);
	if (ret) {
		NOA_MD_RX_ERROR("Fail to remap for modem frag bat[%d] buffer[%d]",
			bat_ring->id, mapped_rx_tkid);
		dma_unmap_page(DCB_TO_DEV(dcb), page_info->data_dma_addr, page_info->data_len,
			DMA_FROM_DEVICE);
		put_page(page_info->page);
		page_info->page = NULL;
		return -ENOMEM;
	}

	// After the above process succeeds, update the free pool index and rx_tkid mapping table
	tkid_info->rx_tkid_free_fore = next_rx_tkid_free_fore;
	tkid_info->rx_tkid[bat_idx] = rx_tkid;
	cur_bat = bat_ring->bat_base + bat_idx;
	cur_bat->buf_addr_high = cpu_to_le32(upper_32_bits(page_info->data_dma_addr));
	cur_bat->buf_addr_low = cpu_to_le32(lower_32_bits(page_info->data_dma_addr));

	/* noa rx path does not need to check bat_ring->mask_tbl.
	* Because the APC bat_idx is not related to rx_tkid mapping,
	* we need to set the bit back of the bat_idx to avoid
	* clearing the bit during the previous reload_rx_buff.
	* This could cause data transfer to stall because the buffer cannot be replenished.
	*/
	set_bit(bat_idx, bat_ring->mask_tbl);

	return 0;
}

int noa_md_rx_tkid_info_setup(void)
{
	struct noa_md_rx *rx;
	struct noa_md_rx_tkid_info *normal_tkid_infos;
	struct noa_md_rx_tkid_info *frag_tkid_infos;
	struct dpmaif_bat_info *bat_infos;
	struct mtk_dpmaif_ctlb *noa_dcb;
	int bat_ring_num;
	int ret = 0;
	int i, j;

	NOA_MD_RX_INFO("enter");
	// Prepare
	rx = &md_dev.rx;
	CHECK_PTR_OR_RETURN_ERR(rx, -EINVAL);

	noa_dcb = md_dev.noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);
	bat_ring_num = noa_dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	bat_infos = noa_dcb->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(bat_infos, -EINVAL);

	normal_tkid_infos = kzalloc(sizeof(*normal_tkid_infos) * bat_ring_num, GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(normal_tkid_infos, error);
	frag_tkid_infos = kzalloc(sizeof(*frag_tkid_infos) * bat_ring_num, GFP_KERNEL);
	CHECK_PTR_OR_GOTO_ERR(frag_tkid_infos, error);

	for (i = 0; i < bat_ring_num; i++) {
		struct dpmaif_bat_info *bat_info;
		struct dpmaif_bat_ring *normal_bat_ring;
		struct dpmaif_bat_ring *frag_bat_ring;
		struct noa_md_rx_tkid_info *normal_tkid_info;
		struct noa_md_rx_tkid_info *frag_tkid_info;

		// Update ring allocation function
		bat_info = &bat_infos[i];
		normal_bat_ring = &bat_info->normal_bat_ring;
		frag_bat_ring = &bat_info->frag_bat_ring;
		normal_bat_ring->alloc = noa_md_rx_alloc_rx_skb;
		frag_bat_ring->alloc = noa_md_rx_alloc_rx_page;

		// TKID init
		// Normal BAT
		normal_tkid_info = &normal_tkid_infos[i];
		normal_tkid_info->rx_tkid = kzalloc(
			sizeof(*normal_tkid_info->rx_tkid) * normal_bat_ring->bat_cnt, GFP_KERNEL);
		CHECK_PTR_OR_GOTO_ERR(normal_tkid_info->rx_tkid, error);

		normal_tkid_info->free_pool = kzalloc(
			sizeof(*normal_tkid_info->free_pool) * normal_bat_ring->bat_cnt, GFP_KERNEL);
		CHECK_PTR_OR_GOTO_ERR(normal_tkid_info->free_pool, error);

		NOA_MD_RX_INFO("normal_bat_ring[%d]->bat_cnt=[%d]", i, normal_bat_ring->bat_cnt);
		for (j = 0; j < normal_bat_ring->bat_cnt; j++) {
			normal_tkid_info->rx_tkid[j] = ~(0x0);
			if (i == 0) {
				normal_tkid_info->free_pool[j] = j + NOA_MD_RX_NOA_NORMAL_BAT0_BASE;
			} else {
				normal_tkid_info->free_pool[j]= j + NOA_MD_RX_NOA_NORMAL_BAT1_BASE;
			}
		}
		normal_tkid_info->rx_tkid_free_fore = 0;
		normal_tkid_info->rx_tkid_free_rear = 0;

		// Frag BAT
		frag_tkid_info = &frag_tkid_infos[i];
		frag_tkid_info->rx_tkid = kzalloc(
			sizeof(*frag_tkid_info->rx_tkid) * frag_bat_ring->bat_cnt, GFP_KERNEL);
		CHECK_PTR_OR_GOTO_ERR(frag_tkid_info->rx_tkid, error);

		frag_tkid_info->free_pool = kzalloc(
			sizeof(*frag_tkid_info->free_pool) * frag_bat_ring->bat_cnt, GFP_KERNEL);
		CHECK_PTR_OR_GOTO_ERR(frag_tkid_info->free_pool, error);

		NOA_MD_RX_INFO("frag_bat_ring[%d]->bat_cnt=[%d]", i, frag_bat_ring->bat_cnt);
		for(j = 0; j < frag_bat_ring->bat_cnt; j++) {
			frag_tkid_info->rx_tkid[j] = ~(0x0);
			if (i == 0) {
				frag_tkid_info->free_pool[j] = j + NOA_MD_RX_NOA_FRAG_BAT0_BASE;
			} else {
				frag_tkid_info->free_pool[j] = j + NOA_MD_RX_NOA_FRAG_BAT1_BASE;
			}
		}
		frag_tkid_info->rx_tkid_free_fore = 0;
		frag_tkid_info->rx_tkid_free_rear = 0;
	}

	rx->normal_tkid_infos = normal_tkid_infos;
	rx->frag_tkid_infos = frag_tkid_infos;

	return ret;

error:
	noa_md_rx_tkid_info_release();
	return -ENOMEM;
}

void noa_md_rx_tkid_info_release(void)
{
	struct noa_md_rx *rx;
	struct mtk_dpmaif_ctlb *noa_dcb;
	int bat_ring_num;

	rx = &md_dev.rx;

	if (!rx->normal_tkid_infos || !rx->frag_tkid_infos)
		return;

	noa_dcb = md_dev.noa_dcb;
	if (!noa_dcb)
		return;

	bat_ring_num = noa_dcb->drv_info->cfg->rx_cfg.bat_ring_num;

	for (int i = 0; i < bat_ring_num; i++) {
		struct noa_md_rx_tkid_info *normal_tkid_info;
		struct noa_md_rx_tkid_info *frag_tkid_info;

		normal_tkid_info = &rx->normal_tkid_infos[i];
		if (normal_tkid_info->rx_tkid)
			kfree(normal_tkid_info->rx_tkid);
		if (normal_tkid_info->free_pool)
			kfree(normal_tkid_info->free_pool);

		frag_tkid_info = &rx->frag_tkid_infos[i];
		if (frag_tkid_info->rx_tkid)
			kfree(frag_tkid_info->rx_tkid);
		if (frag_tkid_info->free_pool)
			kfree(frag_tkid_info->free_pool);
	}

	if (rx->normal_tkid_infos) {
		kfree(rx->normal_tkid_infos);
		rx->normal_tkid_infos = NULL;
	}
	if (rx->frag_tkid_infos) {
		kfree(rx->frag_tkid_infos);
		rx->frag_tkid_infos = NULL;
	}
}

int noa_md_rx_ring_setup(struct noa_md_dev *p_md_dev)
{
	int ret;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct noa_md_rx_buffer_desc *desc;
	struct noa_ring_regs regs = { 0 };
	struct noa_ring_info info = {
		.head = 0,
		.tail = 0,
		.base = NULL,
		.size = NOA_MD_TX_RING_SIZE,
		.item_len = sizeof(struct dpmaif_pd_pit),
		.dpa_base = NULL,
	};

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	for (int ring_type = 0; ring_type < kNoaModemRingRxDataEnd; ring_type++) {
		desc = &p_md_dev->rx.rx_buffer_desc[ring_type];
		NOA_MD_RX_INFO("ring_type=[%d]", ring_type);
		ret = NoaDpaRingSharedRegsGet(p_md_dev->dpa_res->dpa, &regs,
					      kNoaNetworkInterfaceModem,
					      kNoaNetworkFlowDeviceToHost, ring_type,
					      kNoaRingNepOutput);
		if (ret) {
			NOA_MD_RX_ERROR("NoaDpaRingSharedRegsGet Error: %d", ret);
			return ret;
		}

		noa_ring_regs_wrapper_init(
			&desc->ring,
			NOA_RING_TYPE_CONSUMER,
			&noa_md_rx_sw_ring_ops,
			&regs,
			p_md_dev->dev,
			NoaApcSwRxName[ring_type],
			0);

		if (ring_type == kNoaModemRingRxData) {
			desc->desc_base = dma_alloc_coherent(
				p_md_dev->dev,
				info.size * info.item_len,
				&desc->dma_addr,
				GFP_KERNEL);
			if (!desc->desc_base) {
				NOA_MD_RX_ERROR("dma alloc fail: %d", desc->desc_base);
				return -ENOMEM;
			}
			// Remap address for dpa
			ret = noa_md_dma_mapper_remap_sg(
				&p_md_dev->rx.mapper,
				p_md_dev->dev,  /* Source device is the DPMAIF device */
				p_md_dev->dpa_res->dpa_dev,  /* Target device is the DPA */
				(void *)desc->desc_base,
				desc->dma_addr,
				info.size * info.item_len,
				&desc->desc_dpa_base,
				GFP_KERNEL);
			if (ret) {
				NOA_MD_ERROR("Fail to remap kNoaModemRingRxData, ret:%d", ret);
				return -ENOMEM;
			}
		} else {  // support multiple noa rx rings
			int q_id = ring_type - kNoaModemRingRxq0;
			desc->desc_base = noa_dcb->rxqs[q_id].pit_base;
			desc->dma_addr = noa_dcb->rxqs[q_id].pit_dma_addr;
			info.size = noa_dcb->rxqs[q_id].pit_cnt;
			desc->desc_dpa_base = p_md_dev->md_fw->rx->dpmaif_rxqs[q_id].noa_pit_dpa_base;
		}
		info.base = (char *)desc->desc_base;
		info.dpa_base = (char *)desc->desc_dpa_base;
		NOA_MD_RX_INFO("info.size=[%d], info.item_len=[%d]", info.size, info.item_len);
		NOA_MD_RX_INFO("ring_type[%d] desc_dpa_base: 0x%llx", ring_type, desc->desc_dpa_base);
		NOA_MD_RX_INFO("ring_type[%d] desc_base: 0x%llx", ring_type, (u64)desc->desc_base);
		NOA_MD_RX_INFO("ring_type[%d] dma_addr: 0x%llx", ring_type, (u64)desc->dma_addr);

		noa_ring_info_setup(&desc->ring, &info);

		ret = noa_md_dpa_register_isr(NOA_MD_DPA_NEP_DOORBELL, ring_type,
					      noa_md_rx_nep2apc_isr, p_md_dev);
		if (ret) {
			NOA_MD_ERROR("noa_md_dpa_register_isr[%d]=[%d]", ring_type, ret);
			return ret;
		}
	}

	return 0;
}

void noa_md_rx_ring_release(struct noa_md_dev *p_md_dev)
{
	struct noa_md_rx_buffer_desc *desc;
	struct noa_ring_wrapper *ring;

	for (int ring_type = 0; ring_type < kNoaModemRingRxDataEnd; ring_type++) {
		desc = &p_md_dev->rx.rx_buffer_desc[ring_type];
		ring = &desc->ring;
		NOA_MD_RX_INFO("noa_ring_deactivate");
		spin_lock(&desc->lock);
		noa_ring_deactivate(ring);
		if (desc->desc_base && ring_type == kNoaModemRingRxData) {
			noa_md_dma_unmap_by_addr(&p_md_dev->rx.mapper, desc->desc_base);

			dma_free_coherent(
				p_md_dev->dev,
				desc->ring.basic.size * desc->ring.basic.item_len,
				desc->desc_base,
				desc->dma_addr);
		}
		desc->desc_base = NULL;
		desc->dma_addr = 0;
		noa_ring_info_clean(ring);
		spin_unlock(&desc->lock);
	}
	noa_interrupt_unregister(noa_port_irq_get(NOA_PORT_MODEM_SW), p_md_dev);
}

int noa_md_rx_get_frg_read_idx(u32 q_num)
{
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_bat_ring *noa_bat_ring = NULL;

	noa_dcb = md_dev.noa_dcb;
	switch (q_num) {
	case DPMAIF_BAT0:
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT0].frag_bat_ring;
		break;
	case DPMAIF_BAT1:
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT1].frag_bat_ring;
		break;
	default:
		break;
	}
	return noa_bat_ring->bat_rd_idx;
}
EXPORT_SYMBOL_GPL(noa_md_rx_get_frg_read_idx);

int noa_md_rx_get_bat_read_idx(u32 q_num)
{
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_bat_ring *noa_bat_ring = NULL;

	noa_dcb = md_dev.noa_dcb;
	switch (q_num) {
	case DPMAIF_BAT0:
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT0].normal_bat_ring;
		break;
	case DPMAIF_BAT1:
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT1].normal_bat_ring;
		break;
	default:
		break;
	}
	return noa_bat_ring->bat_rd_idx;
}
EXPORT_SYMBOL_GPL(noa_md_rx_get_bat_read_idx);

int noa_md_rx_get_bat_write_idx(u32 q_num)
{
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_bat_ring *noa_bat_ring = NULL;

	noa_dcb = md_dev.noa_dcb;
	switch (q_num) {
	case DPMAIF_BAT0:
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT0].normal_bat_ring;
		break;
	case DPMAIF_BAT1:
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT1].normal_bat_ring;
		break;
	default:
		break;
	}
	return noa_bat_ring->bat_wr_idx;
}
EXPORT_SYMBOL_GPL(noa_md_rx_get_bat_write_idx);

int noa_md_rx_get_ring_write_idx(u32 q_num)
{
	struct noa_md_rx_buffer_desc *desc = md_dev.rx.rx_buffer_desc;
	// multiple noa rx rings
	noa_ring_consumer* ring = &desc[q_num + kNoaModemRingRxq0].ring;

	return noa_ring_head_read_once(ring);
}
EXPORT_SYMBOL_GPL(noa_md_rx_get_ring_write_idx);

int noa_md_rx_set_ring_read_idx(u32 q_num, u32 count)
{
	struct noa_md_rx_buffer_desc *desc = md_dev.rx.rx_buffer_desc;
	// multiple noa rx rings
	noa_ring_consumer* ring = &desc[q_num + kNoaModemRingRxq0].ring;
	u32 tail;

	if (!is_noa_ring_activate(ring)) {
		NOA_MD_RX_ERROR("noa ring not activate");
		return -EINVAL;
	}

	tail = noa_ring_tail_read_once(ring);
	tail = noa_ring_move_pos(tail, count, ring->basic.size);
	noa_ring_tail_write_once(ring, tail);

	return 0;
}
EXPORT_SYMBOL_GPL(noa_md_rx_set_ring_read_idx);

int noa_md_rx_add_tkid_to_free_pool(u32 q_num, u32 tkid)
{
	int ret = 0;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_bat_ring *noa_bat_ring;
	struct noa_md_rx_tkid_info *tkid_infos = NULL;

	/* Add rx_tkid infos into NOA rx_tkid free pool */
	noa_dcb = md_dev.noa_dcb;
	switch (q_num) {
	case DPMAIF_BAT0:
		if (tkid >= NOA_MD_RX_NOA_NORMAL_BAT0_BASE &&
		    tkid < NOA_MD_RX_NOA_FRAG_BAT0_BASE) { /* NOA_NORMAL_BAT0 */
			tkid_infos = &md_dev.rx.normal_tkid_infos[DPMAIF_BAT0];
			noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT0].normal_bat_ring;
		} else if (tkid >= NOA_MD_RX_NOA_FRAG_BAT0_BASE &&
			   tkid < NOA_MD_RX_NOA_NORMAL_BAT1_BASE) { /* NOA_FRAG_BAT0 */
			tkid_infos = &md_dev.rx.frag_tkid_infos[DPMAIF_BAT0];
			noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT0].frag_bat_ring;
		} else {
			NOA_MD_ERROR_LIMIT("Invalid rx_tkid=[%d] value for BAT Ring ID=[%d]", tkid,
					   q_num);
		}
		if (tkid_infos != NULL) {
			NOA_MD_RX_DATA("tkid=[%d] at tkid_infos->rx_tkid_free_rear]=[%u]", tkid,
				       tkid_infos->rx_tkid_free_rear);
			tkid_infos->free_pool[tkid_infos->rx_tkid_free_rear] = tkid;
			tkid_infos->rx_tkid_free_rear = mtk_dpmaif_ring_buf_get_next_idx(
				noa_bat_ring->bat_cnt, tkid_infos->rx_tkid_free_rear);
		} else {
			NOA_MD_RX_ERROR("tkid_infos is NULL");
			ret = -EINVAL;
		}
		break;
	case DPMAIF_BAT1:
		if (tkid >= NOA_MD_RX_NOA_NORMAL_BAT1_BASE &&
		    tkid < NOA_MD_RX_NOA_FRAG_BAT1_BASE) { /* NOA_NORMAL_BAT1 */
			tkid_infos = &md_dev.rx.normal_tkid_infos[DPMAIF_BAT1];
			noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT1].normal_bat_ring;
		} else if (tkid >= NOA_MD_RX_NOA_FRAG_BAT1_BASE &&
			   tkid < NOA_MD_RX_NOA_MAX_BAT_BASE) { /* NOA_FRAG_BAT1 */
			tkid_infos = &md_dev.rx.frag_tkid_infos[DPMAIF_BAT1];
			noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT1].frag_bat_ring;
		} else {
			NOA_MD_ERROR_LIMIT("Invalid rx_tkid=[%d] value for BAT Ring ID=[%d]", tkid,
					   q_num);
		}
		if (tkid_infos != NULL) {
			NOA_MD_RX_DATA("tkid=[%d] at tkid_infos->rx_tkid_free_rear]=[%u]", tkid,
				       tkid_infos->rx_tkid_free_rear);
			tkid_infos->free_pool[tkid_infos->rx_tkid_free_rear] = tkid;
			tkid_infos->rx_tkid_free_rear = mtk_dpmaif_ring_buf_get_next_idx(
				noa_bat_ring->bat_cnt, tkid_infos->rx_tkid_free_rear);
		} else {
			NOA_MD_RX_ERROR("tkid_infos is NULL");
			ret = -EINVAL;
		}
		break;
	default:
		break;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(noa_md_rx_add_tkid_to_free_pool);

int noa_md_rx_write_bat_refill_ring(u32 bat_id, u32 count)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	struct noa_md_apc2ncp_tx_buffer_desc *cur_desc;
	noa_ring_producer *ring;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_bat_ring *noa_bat_ring;
	struct noa_md_rx_tkid_info *tkid_infos;
	int bat_rd_idx;
	unsigned short rx_tkid;
	unsigned short mapped_rx_tkid;
	int q_num;
	struct noa_modem_rx_refill_desc *p_desc = NULL;
	struct dpmaif_bat *cur_bat;
	int ret = 0;
	u32 ring_wr_idx;
	struct noa_md_fw *md_fw = md_dev.md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);
	struct noa_md_fw_rx *rx = md_fw->rx;
	CHECK_PTR_OR_RETURN_ERR(rx, -EINVAL);
	struct noa_bat_info *fw_bat_info = &rx->bat_infos[bat_id];
	CHECK_PTR_OR_RETURN_ERR(fw_bat_info, -EINVAL);
	struct noa_bat_ring *fw_bat_ring = &fw_bat_info->normal_bat_ring;
	CHECK_PTR_OR_RETURN_ERR(fw_bat_ring, -EINVAL);

	/* Write rx_rkid, bat address, and noa_data_address to NCP through RxRefill Rings */
	noa_dcb = md_dev.noa_dcb;

	if (bat_id == DPMAIF_BAT0) {
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT0].normal_bat_ring;
		tkid_infos = &md_dev.rx.normal_tkid_infos[DPMAIF_BAT0];
		cur_desc = &desc[kNoaModemRingRxRefillNormalBat0];
		q_num = kNoaModemRingRxRefillNormalBat0;
		ring = &cur_desc->ring;
	} else {
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT1].normal_bat_ring;
		tkid_infos = &md_dev.rx.normal_tkid_infos[DPMAIF_BAT1];
		cur_desc = &desc[kNoaModemRingRxRefillNormalBat1];
		q_num = kNoaModemRingRxRefillNormalBat1;
		ring = &desc[kNoaModemRingRxRefillNormalBat1].ring;
	}

	if (!is_noa_ring_activate(ring)) {
		return -EINVAL;
	}

	while (noa_bat_ring->bat_rd_idx != noa_bat_ring->bat_wr_idx) {
		bat_rd_idx = noa_bat_ring->bat_rd_idx;
		cur_bat = noa_bat_ring->bat_base + bat_rd_idx;
		rx_tkid = tkid_infos->rx_tkid[bat_rd_idx];
		mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(bat_id, DPMAIF_BAT, rx_tkid);

		ring_wr_idx = noa_ring_head_read_once(ring);
		if (ring_wr_idx >= ring->basic.size) {
			NOA_MD_RX_ERROR("ring_wr_idx[%u] is out of range, ring size is [%u]",
				ring_wr_idx, ring->basic.size);
			return -EINVAL;
		}

		p_desc =
			cur_desc->desc_base + ring_wr_idx * sizeof(struct noa_modem_rx_refill_desc);
		if (!p_desc) {
			NOA_MD_RX_ERROR("p_desc error");
			return -ENOMEM;
		}
		p_desc->rx_tkid = (u16)rx_tkid;
		p_desc->modem_address_low = (u32)cur_bat->buf_addr_low;
		p_desc->modem_address_high = (u32)cur_bat->buf_addr_high;
		p_desc->noa_data_addr =
			(u64)fw_bat_ring->noa_data_addr_apc[mapped_rx_tkid].noa_va;

		ring_wr_idx = noa_ring_move_pos(ring_wr_idx, 1, ring->basic.size);

		noa_ring_head_write_once(ring, ring_wr_idx);

		noa_bat_ring->bat_rd_idx = mtk_dpmaif_ring_buf_get_next_idx(
			noa_bat_ring->bat_cnt, noa_bat_ring->bat_rd_idx);

		NOA_MD_RX_DATA(
			"Refill ring: rd=%u->%u, tkid=%u (mapped:%u), "
			"bat_addr=[h:0x%x, l:0x%x], noa_addr=0x%llx",
			bat_rd_idx, noa_bat_ring->bat_rd_idx, rx_tkid, mapped_rx_tkid,
			cur_bat->buf_addr_high, cur_bat->buf_addr_low,
			p_desc->noa_data_addr);

	}
	noa_md_dpa_notify_ncp(q_num);

	return ret;
}
EXPORT_SYMBOL_GPL(noa_md_rx_write_bat_refill_ring);

int noa_md_rx_write_frag_refill_ring(u32 q_num, u32 count)
{
	struct noa_md_apc2ncp_tx_buffer_desc *desc = noa_md_apc2ncp_get_tx_buffer_desc();
	noa_ring_consumer *ring;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_bat_ring *noa_bat_ring;
	struct noa_md_rx_tkid_info *tkid_infos;
	int bat_rd_idx;
	unsigned short rx_tkid;
	struct noa_modem_rx_refill_desc raw_desc = {0};
	struct noa_modem_rx_refill_desc *p_desc = &raw_desc;
	struct dpmaif_bat *cur_bat;
	int ret = 0;

	/* Write rx_rkid, bat address, and noa_data_address to NCP through RxRefill Rings */
	noa_dcb = md_dev.noa_dcb;

	if (q_num == DPMAIF_BAT0) {
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT0].frag_bat_ring;
		tkid_infos = &md_dev.rx.frag_tkid_infos[DPMAIF_BAT0];
		ring = &desc[kNoaModemRingRxRefillFragBat0].ring;
	} else {
		noa_bat_ring = &noa_dcb->bat_infos[DPMAIF_BAT1].frag_bat_ring;
		tkid_infos = &md_dev.rx.frag_tkid_infos[DPMAIF_BAT1];
		ring = &desc[kNoaModemRingRxRefillFragBat1].ring;
	}
	ret = noa_ring_begin_processing(ring);
	NOA_MD_RX_DATA("ReRefill[%d]_ring_begin_processing - ret:%u", q_num, ret);
	if (ret < 0) {
		NOA_MD_RX_ERROR("ReRefill[%d]_ring_begin_processing error", q_num);
		return ret;
	}
	while (noa_bat_ring->bat_rd_idx != noa_bat_ring->bat_wr_idx) {
		bat_rd_idx = noa_bat_ring->bat_rd_idx;
		cur_bat = noa_bat_ring->bat_base + bat_rd_idx;
		rx_tkid = tkid_infos->rx_tkid[bat_rd_idx];

		p_desc->rx_tkid = rx_tkid;
		p_desc->modem_address_low = cur_bat->buf_addr_low;
		p_desc->modem_address_high = cur_bat->buf_addr_high;
		p_desc->noa_data_addr =
			(unsigned long)noa_bat_ring->sw_record_base[rx_tkid].frag.page;

		ret = noa_ring_write(ring, p_desc, sizeof(*p_desc));
		NOA_MD_RX_DATA("ReRefill[%d]_ring_write, ret=[%d]", q_num, ret);
		if (ret < 0) {
			NOA_MD_RX_ERROR("ReRefill[%d]_ring_write, failed to write desc", q_num);
			return ret;
		}

		noa_bat_ring->bat_rd_idx = mtk_dpmaif_ring_buf_get_next_idx(
			noa_bat_ring->bat_cnt, noa_bat_ring->bat_rd_idx);
	}
	noa_ring_complete_processing(ring);

	return ret;
}
EXPORT_SYMBOL_GPL(noa_md_rx_write_frag_refill_ring);

bool noa_md_rx_skip_napi_disable_during_dynamic_switch(struct noa_md_dev *p_md_dev)
{
	enum dpath_switch_state current_state;
	enum dpath_switch_state old_state;
	enum dpa_data_path target_path;
	enum dpa_data_path old_target_path;

	current_state = p_md_dev->dpath_ctrl->current_state;
	old_state = p_md_dev->dpath_ctrl->old_state;
	target_path = p_md_dev->dpath_ctrl->target_path;
	old_target_path = p_md_dev->dpath_ctrl->old_target_path;

	NOA_MD_RX_INFO("current_state=[%d], old_state=[%d], target_path=[%d], old_target_path=[%d]",
		       p_md_dev->dpath_ctrl->current_state, p_md_dev->dpath_ctrl->old_state,
		       p_md_dev->dpath_ctrl->target_path, p_md_dev->dpath_ctrl->old_target_path);

	if ((old_state == NOA_MD_DPATH_STATE_IDLE_OFFLOAD) &&
	    (current_state == NOA_MD_DPATH_STATE_SERVICE_STOPPING) &&
	    (old_target_path == NOA_DATA_PATH_OFFLOAD) && (target_path == NOA_DATA_PATH_DIRECT)) {
		NOA_MD_RX_INFO(
			"Skip noa_napi_disable when State: IDLE_OFFLOAD -> SERVICE_STOPPING");
		return true;
	}

	return false;
}

static int noa_md_rx_update_wcb_gro_napi(struct noa_md_dev *p_md_dev,
					 enum dpa_data_path target_path)
{
	struct mtk_data_blk *data_blk;
	struct mtk_dpmaif_ctlb *dcb = NULL;
	struct mtk_wwan_ctlb *wcb;
	int rxq_cnt;
	struct dpmaif_rxq_cfg *rxq_cfg;

	if (target_path == NOA_DATA_PATH_OFFLOAD) {
		dcb = p_md_dev->noa_dcb;
		CHECK_PTR_OR_RETURN_ERR(dcb, -EINVAL);
	} else if (target_path == NOA_DATA_PATH_DIRECT) {
		dcb = p_md_dev->dpmaif_dcb;
		CHECK_PTR_OR_RETURN_ERR(dcb, -EINVAL);
	} else {
		NOA_MD_RX_ERROR("Abnormal target_path=[%d]", target_path);
		return -EINVAL;
	}

	data_blk = dcb->data_blk;
	CHECK_PTR_OR_RETURN_ERR(data_blk, -EINVAL);

	wcb = data_blk->wcb;
	CHECK_PTR_OR_RETURN_ERR(wcb, -EINVAL);

	rxq_cnt = dcb->drv_info->cfg->rx_cfg.rxq_cnt;

	for (int i = 0; i < rxq_cnt; i++) {
		rxq_cfg = &dcb->drv_info->cfg->rx_cfg.rxqs[i];

		if (rxq_cfg->attr & DATAQ_ATTR_LOW_LATENCY) {
			if (target_path == NOA_DATA_PATH_OFFLOAD) {
				struct dpmaif_rxq *rxq = &dcb->rxqs[i];
				NOA_MD_RX_INFO(
					"Replacing WCB GRO NAPI[%d] with NOA NAPI (0x%p)",
					i, &rxq->napi);

				wcb->gro_napis[i] = &rxq->napi;
			} else {
				NOA_MD_RX_INFO(
					"Replacing WCB GRO NAPI[%d] with original NAPI (0x%p)",
					i, wcb->data_blk->trans_info.napis[i]);

				wcb->gro_napis[i] = wcb->data_blk->trans_info.napis[i];
			}
		}
	}

	return 0;
}

static int noa_md_rx_bat_ring_init_to_dpmaif(struct noa_bat_ring *ncp_bat_ring,
					       struct dpmaif_bat_ring *noa_bat_ring,
					       const struct dpmaif_bat_ring *dpmaif_bat_ring,
					       int bat_id, int bat_type)
{
	unsigned short cur_bid;
	struct dpmaif_bat *cur_bat;

	if (unlikely(noa_bat_ring->bat_cnt < dpmaif_bat_ring->bat_cnt ||
		     ncp_bat_ring->bat_cnt < dpmaif_bat_ring->bat_cnt)) {
		NOA_MD_RX_ERROR("BAT ring size mismatch for bat_id %d", bat_id);
		return -EINVAL;
	}

	NOA_MD_RX_INFO("Initializing BAT ring id: %d, dpmaif_bat_ring->bat_cnt=[%d]", bat_id,
		       dpmaif_bat_ring->bat_cnt);
	bitmap_copy(dpmaif_bat_ring->mask_tbl, ncp_bat_ring->mask_tbl, dpmaif_bat_ring->bat_cnt);
	memset(dpmaif_bat_ring->sw_record_base, 0,
	       dpmaif_bat_ring->bat_cnt * sizeof(*dpmaif_bat_ring->sw_record_base));

	for (cur_bid = 0; cur_bid < dpmaif_bat_ring->bat_cnt; cur_bid++) {
		unsigned short rx_tkid;
		unsigned short mapped_rx_tkid;

		rx_tkid = ncp_bat_ring->rx_tkid_info.rx_tkid[cur_bid];
		mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(bat_id, bat_type, rx_tkid);

		if (test_bit(cur_bid, ncp_bat_ring->mask_tbl)) {
			continue;
		}

		dpmaif_bat_ring->sw_record_base[cur_bid] =
			noa_bat_ring->sw_record_base[mapped_rx_tkid];
		cur_bat = dpmaif_bat_ring->bat_base + cur_bid;

		noa_md_rx_unmap_modem_dpa_bat(bat_id, bat_type, mapped_rx_tkid);

		if (cur_bat->buf_addr_high !=
		    cpu_to_le32(upper_32_bits(
			    dpmaif_bat_ring->sw_record_base[cur_bid].normal.data_dma_addr))) {
			NOA_MD_RX_ERROR("dma addr check fail (high)");
		}
		if (cur_bat->buf_addr_low !=
		    cpu_to_le32(lower_32_bits(
			    dpmaif_bat_ring->sw_record_base[cur_bid].normal.data_dma_addr))) {
			NOA_MD_RX_ERROR(
				"dma addr check fail (low), cur_bid=[%d], rx_tkid=[%d], "
				"mapped_rx_tkid=[%d], cur_bat->buf_addr_low=[0x%x], "
				"sw_record_base.data_dma_addr.low=[0x%x]",
				cur_bid, rx_tkid, mapped_rx_tkid, cur_bat->buf_addr_low,
				cpu_to_le32(lower_32_bits(dpmaif_bat_ring->sw_record_base[cur_bid]
								  .normal.data_dma_addr)));
		}
	}

	return 0;
}

static int noa_md_rx_bat_rings_init_to_dpmaif(struct noa_md_dev* p_md_dev)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct noa_md_fw *md_fw;
	struct noa_md_fw_rx *rx;
	struct dpmaif_bat_info *dpmaif_bat_infos;
	struct dpmaif_bat_info *noa_bat_infos;
	struct noa_bat_info *ncp_bat_infos;
	int ret;

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	md_fw = p_md_dev->md_fw;
	CHECK_PTR_OR_RETURN_ERR(md_fw, -EINVAL);

	rx = md_fw->rx;
	CHECK_PTR_OR_RETURN_ERR(rx, -EINVAL);

	dpmaif_bat_infos = dpmaif_dcb->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_bat_infos, -EINVAL);

	noa_bat_infos = noa_dcb->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(noa_bat_infos, -EINVAL);

	ncp_bat_infos = rx->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(ncp_bat_infos, -EINVAL);

	for (int index = 0; index < NOA_MD_MAX_BAT_INFOS_SIZE; index++) {
		NOA_MD_RX_INFO("Initializing BAT ring id: %d", index);
		/* Process Normal BAT Ring */
		ret = noa_md_rx_bat_ring_init_to_dpmaif(
			&ncp_bat_infos[index].normal_bat_ring,
			&noa_bat_infos[index].normal_bat_ring,
			&dpmaif_bat_infos[index].normal_bat_ring,
			index, DPMAIF_BAT);
		if (unlikely(ret)) {
			NOA_MD_RX_ERROR("Failed to init normal BAT ring %d to dpmaif: %d", index,
					ret);
			return ret;
		}

		if (!noa_bat_infos[index].frag_bat_enabled) {
			continue;
		}

		/* Process Fragment BAT Ring */
		ret = noa_md_rx_bat_ring_init_to_dpmaif(
			&ncp_bat_infos[index].frag_bat_ring,
			&noa_bat_infos[index].frag_bat_ring,
			&dpmaif_bat_infos[index].frag_bat_ring,
			index, DPMAIF_FRAG);
		if (unlikely(ret)) {
			NOA_MD_RX_ERROR("Failed to init frag BAT ring %d to dpmaif: %d", index,
					ret);
			return ret;
		}
	}

	return 0;
}

static void noa_md_rx_free_buf_in_apc_bat(struct mtk_dpmaif_ctlb *noa_dcb, int index,
					  struct dpmaif_bat_ring *bat_ring,
					  struct noa_md_rx_tkid_info *tkid_infos,
					  enum dpmaif_bat_type bat_type)
{
	unsigned short rx_tkid;
	unsigned short mapped_rx_tkid;
	union dpmaif_bat_record *cur_bat_record;

	while (bat_ring->bat_wr_idx != bat_ring->bat_rd_idx) {
		NOA_MD_RX_ERROR("bat_ring[%d] bat_wr_idx[%d] != bat_rd_idx[%d]", index,
				bat_ring->bat_wr_idx, bat_ring->bat_rd_idx);
		rx_tkid = tkid_infos->rx_tkid[bat_ring->bat_rd_idx];
		mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(bat_ring->id, DPMAIF_BAT, rx_tkid);
		cur_bat_record = bat_ring->sw_record_base + mapped_rx_tkid;

		if (bat_type == NORMAL_BAT) {
			if (cur_bat_record->normal.skb) {
				dma_unmap_single(DCB_TO_DEV(noa_dcb),
						 cur_bat_record->normal.data_dma_addr,
						 cur_bat_record->normal.data_len, DMA_FROM_DEVICE);
				dev_kfree_skb_any(cur_bat_record->normal.skb);
				cur_bat_record->normal.skb = NULL;
			} else {
				NOA_MD_RX_ERROR(
					"skb is NULL in bat_ring[%d], bat_rd_idx=%d, "
					"rx_tkid=%d, mapped_rx_tkid=%d, cur_bat_record=0x%p",
					index, bat_ring->bat_rd_idx, rx_tkid, mapped_rx_tkid,
					cur_bat_record);
			}
		} else {
			struct page *page = cur_bat_record->frag.page;
			if (page) {
				dma_unmap_single(DCB_TO_DEV(noa_dcb),
						 cur_bat_record->frag.data_dma_addr,
						 cur_bat_record->frag.data_len, DMA_FROM_DEVICE);
				put_page(page);
				cur_bat_record->frag.page = NULL;
			} else {
				NOA_MD_RX_ERROR(
					"skb is NULL in bat_ring[%d], bat_rd_idx=%d, "
					"rx_tkid=%d, mapped_rx_tkid=%d, cur_bat_record=0x%p",
					index, bat_ring->bat_rd_idx, rx_tkid, mapped_rx_tkid,
					cur_bat_record);
			}
		}
		bat_ring->bat_rd_idx =
			mtk_dpmaif_ring_buf_get_next_idx(bat_ring->bat_cnt, bat_ring->bat_rd_idx);
	}
}

static void noa_md_rx_free_buf_in_ncp_free_pool(struct mtk_dpmaif_ctlb *noa_dcb, int index,
						struct dpmaif_bat_ring *bat_ring,
						struct noa_rx_tkid_info *rx_tkid_info,
						enum dpmaif_bat_type bat_type)
{
	unsigned short rx_tkid;
	unsigned short mapped_rx_tkid;
	union dpmaif_bat_record *cur_bat_record;

	while (rx_tkid_info->rx_tkid_free_fore != rx_tkid_info->rx_tkid_free_rear) {
		NOA_MD_RX_ERROR("ncp_bat_ring[%d] rx_tkid_free_fore != rx_tkid_free_rear", index);
		rx_tkid = rx_tkid_info->free_pool[rx_tkid_info->rx_tkid_free_fore].rx_tkid;
		mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(bat_ring->id, DPMAIF_BAT, rx_tkid);
		cur_bat_record = bat_ring->sw_record_base + mapped_rx_tkid;

		if (bat_type == NORMAL_BAT) {
			if (cur_bat_record->normal.skb) {
				dma_unmap_single(DCB_TO_DEV(noa_dcb),
						 cur_bat_record->normal.data_dma_addr,
						 cur_bat_record->normal.data_len, DMA_FROM_DEVICE);
				dev_kfree_skb_any(cur_bat_record->normal.skb);
				cur_bat_record->normal.skb = NULL;
			} else {
				NOA_MD_RX_ERROR(
					"skb is NULL in bat_ring[%d], bat_rd_idx=%d, "
					"rx_tkid=%d, mapped_rx_tkid=%d, cur_bat_record=0x%p",
					index, bat_ring->bat_rd_idx, rx_tkid, mapped_rx_tkid,
					cur_bat_record);
			}
		} else {
			struct page *page = cur_bat_record->frag.page;
			if (page) {
				dma_unmap_single(DCB_TO_DEV(noa_dcb),
						 cur_bat_record->frag.data_dma_addr,
						 cur_bat_record->frag.data_len, DMA_FROM_DEVICE);
				put_page(page);
				cur_bat_record->frag.page = NULL;
			} else {
				NOA_MD_RX_ERROR(
					"skb is NULL in bat_ring[%d], bat_rd_idx=%d, "
					"rx_tkid=%d, mapped_rx_tkid=%d, cur_bat_record=0x%p",
					index, bat_ring->bat_rd_idx, rx_tkid, mapped_rx_tkid,
					cur_bat_record);
			}
		}
		rx_tkid_info->rx_tkid_free_fore = mtk_dpmaif_ring_buf_get_next_idx(
			bat_ring->bat_cnt, rx_tkid_info->rx_tkid_free_fore);
	}
}

/**
 * noa_md_rx_check_index - Check if there are any PITs/BATs are
 * still in NOA path while in switch flow.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * This function is called to handle the PITs and BATs are still
 * in NOA path while in switch flow. For PITs, it needs to flush
 * the NOA_PITs by checking the PIT read/write indices. For BATs,
 * it needs to free the BATs which haven't assign the modem by
 * checking the BAT read/write index and the NCP free pool indices.
 */
static void noa_md_rx_check_index(struct noa_md_dev *p_md_dev)
{
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_rxq *rxq;
	struct noa_md_fw *md_fw = p_md_dev->md_fw;
	struct noa_md_fw_rx *md_fw_rx = md_fw->rx;
	struct noa_md_dpmaif_ops *noa_dpmaif_ops = noa_md_wpr_dpmaif_get_dpmaif_ops();
	struct noa_ring_wrapper *ring;

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN(noa_dcb);

	CHECK_PTR_OR_RETURN(p_md_dev->rx.rx_buffer_desc);

	for (int index = 0; index < NOA_MD_MAX_DL_QUEUE_SIZE; index++) {
		rxq = &noa_dcb->rxqs[index];

		/* Flush NOA_PIT rings if there are any in-flight PITs in NOT_PIT rings.
		 * Ensure that the NOT_PIT rings are empty and that
		 * the pit read/write indices are equal.
		 */
		while (rxq->pit_wr_idx != rxq->pit_rd_idx) {
			ring = &p_md_dev->rx.rx_buffer_desc[index + kNoaModemRingRxq0].ring;
			if (!noa_ring_is_empty(ring)) {
				NOA_MD_RX_INFO("rxq->id=[%d] is not empty", index);
				noa_dpmaif_ops->irq_rx_done(noa_dcb, index);
			} else {
				NOA_MD_RX_INFO("rxq->id=[%d] is empty", index);
			}
		}
		NOA_MD_RX_INFO("[offload->direct] noa pit[%d] pit_wr_idx=[%d], "
			       "pit_rd_idx=[%d], pit_rel_rd_idx=[%d]",
			       rxq->id, rxq->pit_wr_idx, rxq->pit_rd_idx, rxq->pit_rel_rd_idx);
	}

	for (int index = 0; index < NOA_MD_MAX_BAT_INFOS_SIZE; index++) {
		struct dpmaif_bat_ring *noa_nor_bat_ring =
			&noa_dcb->bat_infos[index].normal_bat_ring;
		struct noa_md_rx_tkid_info *tkid_infos = &p_md_dev->rx.normal_tkid_infos[index];
		struct noa_bat_ring *ncp_bat_ring = &md_fw_rx->bat_infos[index].normal_bat_ring;
		struct noa_rx_tkid_info *rx_tkid_info = &ncp_bat_ring->rx_tkid_info;

		noa_md_rx_free_buf_in_apc_bat(noa_dcb, index, noa_nor_bat_ring, tkid_infos,
					      NORMAL_BAT);

		NOA_MD_RX_INFO(
			"ncp free pool: bat[%d] rx_tkid_free_fore=[%d], rx_tkid_free_rear=[%d]",
			noa_nor_bat_ring->id, rx_tkid_info->rx_tkid_free_fore,
			rx_tkid_info->rx_tkid_free_rear);

		noa_md_rx_free_buf_in_ncp_free_pool(noa_dcb, index, noa_nor_bat_ring, rx_tkid_info,
						    NORMAL_BAT);

		NOA_MD_RX_INFO("[offload->direct] noa bat[%d] bat_wr_idx=[%d], bat_rd_idx=[%d]",
			       noa_nor_bat_ring->id, noa_nor_bat_ring->bat_wr_idx,
			       noa_nor_bat_ring->bat_rd_idx);

		NOA_MD_RX_INFO(
			"apc free pool: bat[%d] rx_tkid_free_fore=[%d], rx_tkid_free_rear=[%d]",
			noa_nor_bat_ring->id, tkid_infos->rx_tkid_free_fore,
			tkid_infos->rx_tkid_free_rear);

		if (!noa_dcb->bat_infos[index].frag_bat_enabled) {
			continue;
		}

		struct dpmaif_bat_ring *noa_frag_bat_ring =
			&noa_dcb->bat_infos[index].frag_bat_ring;
		struct noa_md_rx_tkid_info *frag_tkid_infos = &p_md_dev->rx.frag_tkid_infos[index];
		struct noa_bat_ring *ncp_frag_bat_ring = &md_fw_rx->bat_infos[index].frag_bat_ring;
		struct noa_rx_tkid_info *frag_rx_tkid_info = &ncp_frag_bat_ring->rx_tkid_info;

		noa_md_rx_free_buf_in_apc_bat(noa_dcb, index, noa_frag_bat_ring, frag_tkid_infos,
					      FRAG_BAT);

		noa_md_rx_free_buf_in_ncp_free_pool(noa_dcb, index, noa_frag_bat_ring,
						    frag_rx_tkid_info, FRAG_BAT);

		NOA_MD_RX_INFO("bat[%d] bat_wr_idx=[%d], bat_rd_idx=[%d]", noa_frag_bat_ring->id,
			       noa_frag_bat_ring->bat_wr_idx, noa_frag_bat_ring->bat_rd_idx);

		NOA_MD_RX_INFO("bat[%d] rx_tkid_free_fore=[%d], rx_tkid_free_rear=[%d]",
			       noa_frag_bat_ring->id, frag_tkid_infos->rx_tkid_free_fore,
			       frag_tkid_infos->rx_tkid_free_rear);
	}
}

/**
 * noa_md_rx_update_ring_info_for_direct_path - Update modem rings' indices.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 * @ncp_state: Pointer to the NCP state receive from the NCP.
 *
 * This function is called to update modem RX rings' indexes from ncp_state
 * which include PIT, BAT and NCP free pool information and index.
 */
static void noa_md_rx_update_ring_info_for_direct_path(struct noa_md_dev *p_md_dev,
					   const struct dpath_ncp_state_payload *ncp_state)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct noa_md_fw *md_fw;
	struct noa_md_fw_rx *md_fw_rx;

	CHECK_PTR_OR_RETURN(p_md_dev);
	CHECK_PTR_OR_RETURN(ncp_state);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN(dpmaif_dcb);

	md_fw = p_md_dev->md_fw;
	CHECK_PTR_OR_RETURN(md_fw);

	md_fw_rx = md_fw->rx;
	CHECK_PTR_OR_RETURN(md_fw_rx);

	for (int index = 0; index < NOA_MD_MAX_DL_QUEUE_SIZE; index++) {
		// Update modem RX rings' indices from shared memory
		const struct rx_ring_idx *rxq_source = &ncp_state->rxqs[index];
		struct dpmaif_rxq *rxq_dest = &dpmaif_dcb->rxqs[index];
		rxq_dest->pit_wr_idx = rxq_source->pit_wr_idx;
		rxq_dest->pit_rd_idx = rxq_source->pit_rd_idx;
		rxq_dest->pit_rel_rd_idx = rxq_source->pit_rel_rd_idx;
		rxq_dest->pit_seq_expect = rxq_source->pit_seq_expect;

		NOA_MD_RX_INFO("rxqs[%d] pit_wr_idx=[%u], pit_rd_idx=[%u], "
			       "pit_rel_rd_idx=[%u], pit_seq_expect=[%u]",
			       index, rxq_dest->pit_wr_idx, rxq_dest->pit_rd_idx,
			       rxq_dest->pit_rel_rd_idx, rxq_dest->pit_seq_expect);
	}

	for (int index = 0; index < NOA_MD_MAX_BAT_INFOS_SIZE; index++) {
		const struct bat_ring *bat_ring_source =
			&ncp_state->bat_infos[index].normal_bat_ring;
		struct dpmaif_bat_ring *bat_ring_dest =
			&dpmaif_dcb->bat_infos[index].normal_bat_ring;
		struct noa_bat_ring *noa_bat_ring = &md_fw_rx->bat_infos[index].normal_bat_ring;
		struct noa_rx_tkid_info *ncp_tkid_info = &noa_bat_ring->rx_tkid_info;
		bat_ring_dest->bat_wr_idx = bat_ring_source->bat_wr_idx;
		bat_ring_dest->bat_rd_idx = bat_ring_source->bat_rd_idx;
		bat_ring_dest->max_reload_cnt = bat_ring_source->max_reload_cnt;
		atomic_set(&bat_ring_dest->to_reload_cnt, bat_ring_source->to_reload_cnt);

		ncp_tkid_info->rx_tkid_free_fore = bat_ring_source->ncp_free_pool_fore;
		ncp_tkid_info->rx_tkid_free_rear = bat_ring_source->ncp_free_pool_rear;

		NOA_MD_RX_INFO(
			"bat[%d] bat_wr_idx=[%u], bat_rd_idx=[%u], max_reload_cnt=[%u], "
			"to_reload_cnt=[%u], rx_tkid_free_fore=[%u], rx_tkid_free_rear=[%u]",
			index, bat_ring_dest->bat_wr_idx, bat_ring_dest->bat_rd_idx,
			bat_ring_dest->max_reload_cnt, bat_ring_dest->to_reload_cnt,
			ncp_tkid_info->rx_tkid_free_fore, ncp_tkid_info->rx_tkid_free_rear);

		if (!dpmaif_dcb->bat_infos[index].frag_bat_enabled) {
			continue;
		}

		const struct bat_ring *frag_bat_ring_source =
			&ncp_state->bat_infos[index].frag_bat_ring;
		struct dpmaif_bat_ring *frag_bat_ring_dest =
			&dpmaif_dcb->bat_infos[index].frag_bat_ring;
		struct noa_bat_ring *frag_noa_bat_ring = &md_fw_rx->bat_infos[index].frag_bat_ring;
		struct noa_rx_tkid_info *frag_ncp_tkid_info = &frag_noa_bat_ring->rx_tkid_info;
		frag_bat_ring_dest->bat_wr_idx = frag_bat_ring_source->bat_wr_idx;
		frag_bat_ring_dest->bat_rd_idx = frag_bat_ring_source->bat_rd_idx;
		frag_bat_ring_dest->max_reload_cnt = frag_bat_ring_source->max_reload_cnt;
		atomic_set(&frag_bat_ring_dest->to_reload_cnt, frag_bat_ring_source->to_reload_cnt);

		frag_ncp_tkid_info->rx_tkid_free_fore = frag_bat_ring_source->ncp_free_pool_fore;
		frag_ncp_tkid_info->rx_tkid_free_rear = frag_bat_ring_source->ncp_free_pool_rear;
	}
}

/**
 * noa_md_rx_update_ring_info_for_offload_path - Update modem RX rings'
 * indexes for offload path.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 * @ap_state: Pointer to the AP state to send to the NCP.
 *
 * This function is called to update modem RX rings' indexes to ap_state
 * which include PIT and BAT information and index.
 */
static void noa_md_rx_update_ring_info_for_offload_path(
			struct noa_md_dev *p_md_dev,
			struct dpath_ap_state_payload *ap_state)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb;

	CHECK_PTR_OR_RETURN(p_md_dev);
	CHECK_PTR_OR_RETURN(ap_state);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN(dpmaif_dcb);

	for (int index = 0; index < NOA_MD_MAX_DL_QUEUE_SIZE; index++) {
		// Update modem RX rings' indexes to shared memory
		struct dpmaif_rxq *rxq_source = &dpmaif_dcb->rxqs[index];
		struct rx_ring_idx *rxq_dest = &ap_state->rxqs[index];
		rxq_dest->pit_wr_idx = rxq_source->pit_wr_idx;
		rxq_dest->pit_rd_idx = rxq_source->pit_rd_idx;
		rxq_dest->pit_rel_rd_idx = rxq_source->pit_rel_rd_idx;
		rxq_dest->pit_seq_expect = rxq_source->pit_seq_expect;

		NOA_MD_RX_INFO("rxqs[%d] pit_wr_idx=[%u], pit_rd_idx=[%u], pit_rel_rd_idx=[%u], "
			       "pit_seq_expect=[%u]",
			       index, rxq_dest->pit_wr_idx, rxq_dest->pit_rd_idx,
			       rxq_dest->pit_rel_rd_idx, rxq_dest->pit_seq_expect);
	}

	for (int index = 0; index < NOA_MD_MAX_BAT_INFOS_SIZE; index++) {
		struct dpmaif_bat_ring *bat_ring_source =
			&dpmaif_dcb->bat_infos[index].normal_bat_ring;
		struct bat_ring *bat_ring_dest = &ap_state->bat_infos[index].normal_bat_ring;
		bat_ring_dest->bat_wr_idx = bat_ring_source->bat_wr_idx;
		bat_ring_dest->bat_rd_idx = bat_ring_source->bat_rd_idx;
		bat_ring_dest->max_reload_cnt = bat_ring_source->max_reload_cnt;
		bat_ring_dest->to_reload_cnt = atomic_read(&bat_ring_source->to_reload_cnt);

		NOA_MD_RX_INFO("bat[%d] bat_wr_idx=[%u], bat_rd_idx=[%u], max_reload_cnt=[%u], "
			       "to_reload_cnt=[%u]",
			       index, bat_ring_dest->bat_wr_idx, bat_ring_dest->bat_rd_idx,
			       bat_ring_dest->max_reload_cnt, bat_ring_dest->to_reload_cnt);

		if (!dpmaif_dcb->bat_infos[index].frag_bat_enabled) {
			continue;
		}

		struct dpmaif_bat_ring *frag_bat_ring_source =
			&dpmaif_dcb->bat_infos[index].frag_bat_ring;
		struct bat_ring *frag_bat_ring_dest = &ap_state->bat_infos[index].frag_bat_ring;
		frag_bat_ring_dest->bat_wr_idx = frag_bat_ring_source->bat_wr_idx;
		frag_bat_ring_dest->bat_rd_idx = frag_bat_ring_source->bat_rd_idx;
		frag_bat_ring_dest->max_reload_cnt = frag_bat_ring_source->max_reload_cnt;
		frag_bat_ring_dest->to_reload_cnt =
			atomic_read(&frag_bat_ring_source->to_reload_cnt);

		NOA_MD_RX_INFO("bat[%d] bat_wr_idx=[%u], bat_rd_idx=[%u], max_reload_cnt=[%u], "
			       "to_reload_cnt=[%u]",
			       index, frag_bat_ring_dest->bat_wr_idx,
			       frag_bat_ring_dest->bat_rd_idx, frag_bat_ring_dest->max_reload_cnt,
			       frag_bat_ring_dest->to_reload_cnt);
	}
}

static int noa_md_rx_remap_noa_buffer_address_from_direct_to_offload(
	struct noa_md_dev *p_md_dev, struct noa_bat_ring *bat_ring_source,
	enum dpmaif_bat_type type, int bat_ring_id)
{
	int ret;
	unsigned short rx_tkid;
	unsigned short mapped_rx_tkid;
	struct mtk_dpmaif_ctlb *dpmaif_dcb = NULL;
	struct dpmaif_bat_info *dpmaif_bat_infos = NULL;
	struct dpmaif_bat_ring *dpmaif_bat_ring;
	struct device *dpa_dev = p_md_dev->dpa_res->dpa_dev;
	void *va;
	dma_addr_t dma_addr;

	NOA_MD_RX_INFO("enter");
	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	dpmaif_bat_infos = dpmaif_dcb->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_bat_infos, -EINVAL);

	for (int bid_idx = 0; bid_idx < bat_ring_source->bat_cnt; bid_idx++) {
		if (test_bit(bid_idx, bat_ring_source->mask_tbl)) {
			continue; // Skip if mask bit is set
		}
		rx_tkid = bat_ring_source->rx_tkid_info.rx_tkid[bid_idx];
		if (type == NORMAL_BAT) {
			dpmaif_bat_ring = &dpmaif_bat_infos[bat_ring_id].normal_bat_ring;
			mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(
				bat_ring_id, DPMAIF_BAT, rx_tkid);
			va = (void *)dpmaif_bat_ring->sw_record_base[bid_idx].normal.skb->data;
			dma_addr = dpmaif_bat_ring->sw_record_base[bid_idx].normal.data_dma_addr;
		} else {
			dpmaif_bat_ring = &dpmaif_bat_infos[bat_ring_id].frag_bat_ring;
			mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(
				bat_ring_id, DPMAIF_FRAG, rx_tkid);
			va = (void *)dpmaif_bat_ring->sw_record_base[bid_idx].frag.page;
			dma_addr = dpmaif_bat_ring->sw_record_base[bid_idx].frag.data_dma_addr;
		}
		ret = noa_md_dma_mapper_remap_sg(
			&p_md_dev->rx.mapper,
			DCB_TO_DEV(p_md_dev->dpmaif_dcb),
			dpa_dev, /* Target device is the DPA */
			va,
			dma_addr,
			dpmaif_bat_ring->buf_size,
			&bat_ring_source->noa_data_addr[mapped_rx_tkid].noa_va, GFP_KERNEL);
		if (ret) {
			NOA_MD_RX_ERROR("Fail to remap for modem bat[%d] buffer[%d]", bat_ring_id,
					mapped_rx_tkid);
			return ret;
		}
	}

	return 0;
}

/**
 * noa_md_rx_remap_noa_buffer_address_setup() - Setup the remap NOA buffer address
 * when dynamic switch from direct mode to offload mode.
 * @p_md_dev:   Pointer to the main noa_md_dev driver structure.
 * @bat_info_source: The driver's software view of the BAT to be prepared.
 * @bat_ring_id:     The numerical ID of the BAT ring being processed.
 *
 * This function is called to setup remap the buffer addresses which use in
 * noa firmware.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_rx_remap_noa_buffer_address_setup(struct noa_md_dev *p_md_dev,
						    struct noa_bat_info *bat_info_source,
						    int bat_ring_id)
{
	int ret;

	NOA_MD_RX_INFO("enter");
	ret = noa_md_rx_remap_noa_buffer_address_from_direct_to_offload(p_md_dev,
		&bat_info_source->normal_bat_ring, NORMAL_BAT, bat_ring_id);

	if (bat_info_source->frag_bat_enabled) {
		ret = noa_md_rx_remap_noa_buffer_address_from_direct_to_offload(p_md_dev,
			&bat_info_source->frag_bat_ring, FRAG_BAT, bat_ring_id);
	}
	return ret;
}

/**
 * noa_md_rx_remap_noa_buffer_address - Remap the buffer addresses which use in
 * noa firmware.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * This function is called to remap the buffer addresses which use in noa firmware.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_rx_remap_noa_buffer_address(struct noa_md_dev *p_md_dev)
{
	int ret = 0;
	struct noa_md_fw *md_fw = p_md_dev->md_fw;
	struct noa_md_fw_rx *md_fw_rx = md_fw->rx;

	NOA_MD_RX_INFO("enter");
	for (int index = 0; index < NOA_MD_MAX_BAT_INFOS_SIZE; index++) {
		struct noa_bat_info *bat_info_source = &md_fw_rx->bat_infos[index];

		ret = noa_md_rx_remap_noa_buffer_address_setup(p_md_dev, bat_info_source, index);
		if (ret < 0) {
			NOA_MD_RX_ERROR("Fail to remap noa buffer address when dynamic switching "
				     "from direct to offload mode");
			return ret;
		}
	}
	return 0;
}

static void noa_md_rx_reset_noa_tables_index(struct noa_md_dev *p_md_dev)
{
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct noa_md_fw *md_fw = p_md_dev->md_fw;
	struct noa_md_fw_rx *md_fw_rx = md_fw->rx;

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN(noa_dcb);

	for (int index = 0; index < NOA_MD_MAX_BAT_INFOS_SIZE; index++) {
		struct dpmaif_bat_ring *noa_nor_bat_ring =
			&noa_dcb->bat_infos[index].normal_bat_ring;
		struct noa_md_rx_tkid_info *normal_tkid_infos =
			&p_md_dev->rx.normal_tkid_infos[index];
		struct noa_bat_ring *ncp_bat_ring = &md_fw_rx->bat_infos[index].normal_bat_ring;
		struct noa_rx_tkid_info *rx_tkid_info = &ncp_bat_ring->rx_tkid_info;

		NOA_MD_RX_INFO("reset apc bat[%d] rx_tkid and free pool fore/rear index", index);
		for (int i = 0; i < noa_nor_bat_ring->bat_cnt; i++) {
			normal_tkid_infos->rx_tkid[i] = NOA_MD_RX_INVALID_TKID;
			if (index == 0) {
				normal_tkid_infos->free_pool[i] =
					i + NOA_MD_RX_NOA_NORMAL_BAT0_BASE;
			} else {
				normal_tkid_infos->free_pool[i] =
					i + NOA_MD_RX_NOA_NORMAL_BAT1_BASE;
			}
		}
		normal_tkid_infos->rx_tkid_free_fore = 0;
		normal_tkid_infos->rx_tkid_free_rear = 0;

		NOA_MD_RX_INFO("reset ncp bat[%d] rx_tkid and free pool fore/rear index", index);
		for (int i = 0; i < ncp_bat_ring->bat_cnt; i++) {
			rx_tkid_info->rx_tkid[i] = NOA_MD_RX_INVALID_TKID;
			rx_tkid_info->free_pool[i].rx_tkid = 0;
		}
		rx_tkid_info->rx_tkid_free_fore = 0;
		rx_tkid_info->rx_tkid_free_rear = 0;

		if (!noa_dcb->bat_infos[index].frag_bat_enabled) {
			continue;
		}

		struct dpmaif_bat_ring *noa_frag_bat_ring =
			&noa_dcb->bat_infos[index].frag_bat_ring;
		struct noa_md_rx_tkid_info *frag_tkid_infos = &p_md_dev->rx.frag_tkid_infos[index];
		struct noa_bat_ring *ncp_frag_bat_ring = &md_fw_rx->bat_infos[index].frag_bat_ring;
		struct noa_rx_tkid_info *frag_rx_tkid_info = &ncp_frag_bat_ring->rx_tkid_info;

		for (int i = 0; i < noa_frag_bat_ring->bat_cnt; i++) {
			frag_tkid_infos->rx_tkid[i] = NOA_MD_RX_INVALID_TKID;
			if (index == 0) {
				frag_tkid_infos->free_pool[i] = i + NOA_MD_RX_NOA_FRAG_BAT0_BASE;
			} else {
				frag_tkid_infos->free_pool[i] = i + NOA_MD_RX_NOA_FRAG_BAT1_BASE;
			}
		}
		frag_tkid_infos->rx_tkid_free_fore = 0;
		frag_tkid_infos->rx_tkid_free_rear = 0;

		for (int i = 0; i < ncp_frag_bat_ring->bat_cnt; i++) {
			frag_rx_tkid_info->rx_tkid[i] = NOA_MD_RX_INVALID_TKID;
			frag_rx_tkid_info->free_pool[i].rx_tkid = 0;
		}
		frag_rx_tkid_info->rx_tkid_free_fore = 0;
		frag_rx_tkid_info->rx_tkid_free_rear = 0;
	}
}

/**
 * noa_md_rx_on_state_change() - Handles state changes from the controller.
 * @state:       The new state to handle.
 * @target_path: The final data path destination.
 * @ncp_state:   NCP state, only valid in DEVICE_PREPARING.
 *
 * This function executes the RX-specific logic for each step of the switch.
 */
static void noa_md_rx_on_state_change(
	struct noa_dpath_client *client,
	enum dpath_switch_state state,
	enum dpa_data_path target_path,
	const struct dpath_ncp_state_payload *ncp_state)
{
	struct noa_md_dpath_ctrl *ctrl = client->ctrl;
	struct noa_md_dev *p_md_dev = ctrl->dev;
	struct noa_md_rx *rx = &p_md_dev->rx;
	bool success = true;

	NOA_MD_RX_INFO("Handling state: %d", state);

	switch (state) {
	case NOA_MD_DPATH_STATE_SERVICE_STOPPING:
		break;
	case NOA_MD_DPATH_STATE_DEVICE_PREPARING:
		if (target_path == NOA_DATA_PATH_OFFLOAD) {
			noa_md_rx_bat_rings_init_from_dpmaif(p_md_dev);
			noa_md_rx_update_ring_info_for_offload_path(p_md_dev, &ctrl->ap_state);
			if (noa_md_rx_remap_noa_buffer_address(p_md_dev) < 0)
				success = false;
			noa_md_ring_service_rx_activate(true);
		}
		noa_md_rx_update_wcb_gro_napi(p_md_dev, target_path);
		break;

	case NOA_MD_DPATH_STATE_DEVICE_RESUMING:
		if (target_path == NOA_DATA_PATH_DIRECT) {
			noa_md_ring_service_rx_activate(false);
			noa_md_rx_update_ring_info_for_direct_path(p_md_dev, ncp_state);
			noa_md_rx_check_index(p_md_dev);
			noa_md_rx_bat_rings_init_to_dpmaif(p_md_dev);
			noa_md_rx_reset_noa_tables_index(p_md_dev);
		}
		break;

	case NOA_MD_DPATH_STATE_SERVICE_RESTARTING:
		break;

	case NOA_MD_DPATH_STATE_ROLLING_BACK:
		// TODO: b/434646935 - Implement logic to revert any changes and
		// restore the RX path to its previous working state.
		break;

	default:
		/* IDLE, FAILED states do not require action from this module. */
		break;
	}

	// Report completion back to the controller.
	noa_md_dpath_ctrl_report_completion(rx->dpath_client, success);
}

static struct noa_dpath_client_ops rx_dpath_ops = {
	.on_state_change = noa_md_rx_on_state_change,
};

int noa_md_rx_setup(void)
{
	struct noa_md_rx *rx = &md_dev.rx;
	int ret = 0;

	NOA_MD_RX_INFO("enter");

	/* Prevent re-initialization */
	if (unlikely(rx->rx_buffer_desc)) {
		NOA_MD_RX_ERROR("RX resources seem to be already initialized.");
		return -EALREADY;
	}

	/* 1. Allocate main resources */
	rx->rx_buffer_desc =
		kcalloc(kNoaModemRingRxDataEnd, sizeof(*rx->rx_buffer_desc), GFP_KERNEL);
	if (!rx->rx_buffer_desc) {
		NOA_MD_RX_ERROR("Failed to allocate rx_buffer_desc");
		return -ENOMEM;
	}

	rx->isr_wq = alloc_workqueue("noa_md_rx_isr_wq",
		WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!rx->isr_wq) {
		ret = -ENOMEM;
		goto err_free_buffer_desc;
	}

	/* 2. Initialize members and sub-modules */
	spin_lock_init(&rx->lock);
	for (int i = 0; i < kNoaModemRingRxDataEnd; i++) {
		spin_lock_init(&rx->rx_buffer_desc[i].lock);
	}

	INIT_WORK(&rx->isr_work, noa_md_rx_isr_work);

	ret = noa_md_dma_mapper_init(&rx->mapper, "noa_rx_mapper");
	if (ret) {
		NOA_MD_RX_ERROR("Failed to initialize rx dma mapper: %d", ret);
		goto err_destroy_wq;
	}

	/* 3. Register with external modules */
	rx->dpath_client = noa_md_dpath_ctrl_register_client(
		&md_dev, NOA_DPATH_CLIENT_RX, &rx_dpath_ops);
	if (IS_ERR(rx->dpath_client)) {
		ret = PTR_ERR(rx->dpath_client);
		rx->dpath_client = NULL;
		NOA_MD_RX_ERROR("Failed to register with dpath ctrl: %d", ret);
		goto err_release_mapper;
	}

	NOA_MD_RX_INFO("RX setup successful");
	return 0;

err_release_mapper:
	noa_md_dma_unmap_all(&rx->mapper);
	noa_md_dma_mapper_release(&rx->mapper);
err_destroy_wq:
	destroy_workqueue(rx->isr_wq);
	rx->isr_wq = NULL;
err_free_buffer_desc:
	kfree(rx->rx_buffer_desc);
	rx->rx_buffer_desc = NULL;

	NOA_MD_RX_ERROR("RX setup failed, ret=%d", ret);
	return ret;
}

void noa_md_rx_release(void)
{
	struct noa_md_rx *rx = &md_dev.rx;

	NOA_MD_RX_INFO("enter");

	/* 1. Unregister from external systems to stop new events */
	if (rx->dpath_client) {
		noa_md_dpath_ctrl_unregister_client(&md_dev, rx->dpath_client);
		rx->dpath_client = NULL;
	}

	/* 2. Stop all active components (workqueues) */
	if (rx->isr_wq) {
		cancel_work_sync(&rx->isr_work);
		destroy_workqueue(rx->isr_wq);
		rx->isr_wq = NULL;
	}

	/* 3. Free passive resources */
	noa_md_dma_unmap_all(&rx->mapper);
	noa_md_dma_mapper_release(&rx->mapper);

	noa_md_rx_tkid_info_release();

	if (rx->rx_buffer_desc) {
		kfree(rx->rx_buffer_desc);
		rx->rx_buffer_desc = NULL;
	}

	NOA_MD_RX_INFO("exit");
}

static int noa_md_rx_remap_ncp_tkid_free_pool(struct noa_md_dev *p_md_dev,
					       struct noa_bat_ring *bat_ring_source,
					       struct device *dpa_dev)
{
	int ret;
	struct noa_md_shmem_layout *shmem =
		(struct noa_md_shmem_layout *)p_md_dev->shmem_handle.va_base;
	u64 ncp_free_pool_dpa_base;

	CHECK_PTR_OR_RETURN_ERR(shmem, -EINVAL);

	// Unmap the old dpa base address if it exists to prevent memory leak
	if (bat_ring_source->type == NORMAL_BAT) {
		dma_addr_t old_dpa_base =
			shmem->switch_payload.ap_state.bat_infos[bat_ring_source->id]
				.normal_bat_ring.ncp_free_pool_dpa_base;

		if (old_dpa_base)
			dma_unmap_single(dpa_dev, old_dpa_base,
					 bat_ring_source->bat_cnt *
						 sizeof(*bat_ring_source->rx_tkid_info.free_pool),
					 DMA_BIDIRECTIONAL);
	} else {
		dma_addr_t old_dpa_base =
			shmem->switch_payload.ap_state.bat_infos[bat_ring_source->id]
				.frag_bat_ring.ncp_free_pool_dpa_base;

		if (old_dpa_base)
			dma_unmap_single(dpa_dev, old_dpa_base,
					 bat_ring_source->bat_cnt *
						 sizeof(*bat_ring_source->rx_tkid_info.free_pool),
					 DMA_BIDIRECTIONAL);
	}

	// Remap the dpa base address for ncp rx_tkid free pool to use
	ncp_free_pool_dpa_base = dma_map_single(
		dpa_dev, bat_ring_source->rx_tkid_info.free_pool,
		bat_ring_source->bat_cnt * sizeof(*bat_ring_source->rx_tkid_info.free_pool),
		DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(dpa_dev, ncp_free_pool_dpa_base);
	if (unlikely(ret)) {
		NOA_MD_RX_ERROR("Failed to map dpa base of shared ncp rx_tkid free pool!");
		return ret;
	}
	NOA_MD_RX_INFO("bat[%d], bat_type=[%d], ncp free pool=[0x%lx], "
		    "ncp_free_pool_dpa_base=[0x%lx]",
		    bat_ring_source->id, bat_ring_source->type,
		    (unsigned long)bat_ring_source->rx_tkid_info.free_pool, ncp_free_pool_dpa_base);

	if (bat_ring_source->type == NORMAL_BAT)
		shmem->switch_payload.ap_state.bat_infos[bat_ring_source->id]
			.normal_bat_ring.ncp_free_pool_dpa_base = ncp_free_pool_dpa_base;
	else
		shmem->switch_payload.ap_state.bat_infos[bat_ring_source->id]
			.frag_bat_ring.ncp_free_pool_dpa_base = ncp_free_pool_dpa_base;

	/* Write Memory Barrier */
	dma_wmb();

	return 0;
}

static int noa_md_rx_fw_batbm_init(struct noa_md_dev *p_md_dev,
	struct noa_bat_ring *bat_ring_source,
	struct noa_batbm *bat_ring_dest,
	enum dpmaif_bat_type type,
	int bat_ring_id)
{
	int ret;
	struct mtk_dpmaif_ctlb *dpmaif_dcb = NULL;
	struct dpmaif_bat_info *dpmaif_bat_infos = NULL;
	struct device *dpa_dev = p_md_dev->dpa_res->dpa_dev;
	// TODO: b/452216362 - Remove this code when dynamic switch enabled
#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
	unsigned short rx_tkid;
	unsigned short mapped_rx_tkid;
	struct dpmaif_bat_ring *dpmaif_bat_ring;
#endif

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	dpmaif_bat_infos = dpmaif_dcb->bat_infos;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_bat_infos, -EINVAL);

	NOA_MD_RX_INFO("enter");

	bat_ring_dest->bat_base = (uint64_t)bat_ring_source->bat_base;
	bat_ring_dest->bat_cnt = bat_ring_source->bat_cnt;
	bat_ring_dest->reload_cnt = bat_ring_source->max_reload_cnt;
	bat_ring_dest->bat_wr_idx = bat_ring_source->bat_wr_idx;
	bat_ring_dest->bat_rd_idx = bat_ring_source->bat_rd_idx;

	NOA_MD_RX_INFO("bat[%d], bat_type=[%d], bat_base=[0x%lx], bat_cnt=[%d], "
		"reload_cnt=[%d], bat_wr_idx=[%d], bat_rd_idx=[%d]", bat_ring_id, type,
		bat_ring_dest->bat_base,
		bat_ring_dest->bat_cnt,
		bat_ring_dest->reload_cnt,
		bat_ring_dest->bat_wr_idx,
		bat_ring_dest->bat_rd_idx);

	// Remap NOA BAT base
	ret = noa_md_dma_mapper_remap_sg(
		&p_md_dev->rx.mapper,
		DCB_TO_DEV(p_md_dev->noa_dcb),  /* Source device is the DPMAIF device */
		dpa_dev,  /* Target device is the DPA */
		(void *)bat_ring_source->bat_base,
		bat_ring_source->bat_dma_addr,
		bat_ring_source->bat_cnt * sizeof(*bat_ring_source->bat_base),
		&bat_ring_dest->bat_dpa_base,
		GFP_KERNEL);
	NOA_MD_RX_INFO("bat[%d], bat_type=[%d], bat_base=[0x%lx], bat_dpa_base=[0x%lx]",
		bat_ring_id, type, (unsigned long)bat_ring_source->bat_base,
		bat_ring_dest->bat_dpa_base);
	if (ret) {
		NOA_MD_RX_ERROR("Fail to remap for modem bat_ring[%d]", bat_ring_id);
		return -DATA_DMA_MAP_ERR;
	}

	// Allocate shared mask table for NCP to use
	bat_ring_dest->mask_table_dpa_base = dma_map_single(
		dpa_dev, bat_ring_source->mask_tbl,
		BITS_TO_LONGS(bat_ring_source->bat_cnt) * sizeof(*bat_ring_source->mask_tbl),
		DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(dpa_dev, bat_ring_dest->mask_table_dpa_base);
	if (unlikely(ret)) {
		NOA_MD_RX_ERROR("Failed to map dma of shared mask table!");
		return -DATA_DMA_MAP_ERR;
	}
	NOA_MD_RX_INFO(
		"bat[%d], bat_type=[%d], mask_table=[0x%lx], "
		"mask_table_dpa_base=[0x%lx]",
		bat_ring_id, type, (unsigned long)bat_ring_source->mask_tbl,
		bat_ring_dest->mask_table_dpa_base);

	// Allocate shared rx_tkid table for NCP to use
	bat_ring_dest->tkid_table_dpa_base = dma_map_single(
		dpa_dev, bat_ring_source->rx_tkid_info.rx_tkid,
		bat_ring_source->bat_cnt * sizeof(*bat_ring_source->rx_tkid_info.rx_tkid),
		DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(dpa_dev, bat_ring_dest->tkid_table_dpa_base);
	if (unlikely(ret)) {
		NOA_MD_RX_ERROR("Failed to map dma of shared rx_tkid table!");
		goto error;
	}
	NOA_MD_RX_INFO(
		"bat[%d], bat_type=[%d], tkid_table=[0x%lx], "
		"tkid_table_dpa_base=[0x%lx]",
		bat_ring_id, type, (unsigned long)bat_ring_source->rx_tkid_info.rx_tkid,
		bat_ring_dest->tkid_table_dpa_base);

	// Allocate shared buffer address table
	bat_ring_dest->buffer_table_dpa_base =
		dma_map_single(dpa_dev, bat_ring_source->noa_data_addr,
			bat_ring_source->bat_cnt * sizeof(*bat_ring_source->noa_data_addr),
			DMA_BIDIRECTIONAL);
	ret = dma_mapping_error(dpa_dev, bat_ring_dest->buffer_table_dpa_base);
	if (unlikely(ret)) {
		NOA_MD_RX_ERROR("Failed to map dma of shared buffer table!");
		goto error;
	}
	NOA_MD_RX_INFO(
		"bat[%d], bat_type=[%d], buffer_table=[0x%lx], "
		"buffer_table_dpa_base=[0x%lx]",
		bat_ring_id, type, (unsigned long)bat_ring_source->noa_data_addr,
		bat_ring_dest->buffer_table_dpa_base);

	// TODO: b/452216362 - Remove this code when dynamic switch enabled
	ret = noa_md_rx_remap_ncp_tkid_free_pool(p_md_dev, bat_ring_source, dpa_dev);
	if (unlikely(ret)) {
		NOA_MD_RX_ERROR("Failed to map dpa base of shared ncp rx_tkid free pool!");
		goto error;
	}

#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
	// Init remap the buffer address for NCP which mask bit is not set
	for (int bid_idx = 0; bid_idx < bat_ring_source->bat_cnt; bid_idx++) {
		if (test_bit(bid_idx, bat_ring_source->mask_tbl)) {
			continue; // Skip if mask bit is set
		}
		rx_tkid = bat_ring_source->rx_tkid_info.rx_tkid[bid_idx];
		if (type == NORMAL_BAT) {
			dpmaif_bat_ring = &dpmaif_bat_infos[bat_ring_id].normal_bat_ring;
			mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(
				bat_ring_id, DPMAIF_BAT, rx_tkid);
			ret = noa_md_dma_mapper_remap_sg(
				&p_md_dev->rx.mapper,
				DCB_TO_DEV(p_md_dev->dpmaif_dcb),
				dpa_dev,
				(void *)dpmaif_bat_ring->sw_record_base[bid_idx].normal.skb->data,
				dpmaif_bat_ring->sw_record_base[bid_idx].normal.data_dma_addr,
				dpmaif_bat_ring->buf_size,
				&bat_ring_source->noa_data_addr[mapped_rx_tkid].noa_va,
				GFP_KERNEL);
			if (ret) {
				NOA_MD_RX_ERROR(
					"Fail to remap for modem normal bat[%d] buffer[%d]",
					bat_ring_id, mapped_rx_tkid);
				goto error;
			}
		} else {
			dpmaif_bat_ring = &dpmaif_bat_infos[bat_ring_id].frag_bat_ring;
			mapped_rx_tkid = noa_md_rx_tkid_to_bat_index(
				bat_ring_id, DPMAIF_FRAG, rx_tkid);
			ret = noa_md_dma_mapper_remap_sg(
				&p_md_dev->rx.mapper,
				DCB_TO_DEV(p_md_dev->dpmaif_dcb),
				dpa_dev,
				(void *)dpmaif_bat_ring->sw_record_base[bid_idx].frag.page,
				dpmaif_bat_ring->sw_record_base[bid_idx].frag.data_dma_addr,
				dpmaif_bat_ring->buf_size,
				&bat_ring_source->noa_data_addr[mapped_rx_tkid].noa_va,
				GFP_KERNEL);
			if (ret) {
				NOA_MD_RX_ERROR("Fail to remap for modem frag bat[%d] buffer[%d]",
					bat_ring_id, mapped_rx_tkid);
				goto error;
			}
		}
	}
#endif

	return 0;

error:
	noa_md_dma_unmap_all(&p_md_dev->rx.mapper);
	return -DATA_DMA_MAP_ERR;
}

/**
 * noa_md_rx_setup_fw_bat() - Setup and remap BAT structures for firmware.
 * @p_md_dev:   Pointer to the main noa_md_dev driver structure.
 * @bat_info_source: The driver's software view of the BAT to be prepared.
 * @bat_info_dest:   The firmware-facing BAT structure to be populated.
 * @bat_ring_id:     The numerical ID of the BAT ring being processed.
 *
 * This function populates the firmware's BAT info structure and remaps all
 * associated memory regions (BAT base, tables, and data buffers) into the
 * DPA's address space for hardware access.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int noa_md_rx_setup_fw_bat(struct noa_md_dev *p_md_dev,
	struct noa_bat_info *bat_info_source,
	struct noa_bat *bat_info_dest, int bat_ring_id)
{
	NOA_MD_RX_INFO("enter");

	// NCP normal bat ring init and remap
	noa_md_rx_fw_batbm_init(p_md_dev, &bat_info_source->normal_bat_ring,
				&bat_info_dest->normal_bat, NORMAL_BAT, bat_ring_id);

	// NCP fragment bat ring init and remap
	if (bat_info_source->frag_bat_enabled) {
		NOA_MD_RX_INFO("frag_bat_enabled");
		noa_md_rx_fw_batbm_init(p_md_dev, &bat_info_source->frag_bat_ring,
					&bat_info_dest->frag_bat, FRAG_BAT, bat_ring_id);
	}

	return 0;
}

int noa_md_rx_queues_remap_setup(struct noa_md_dev *p_md_dev)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb = p_md_dev->dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb = p_md_dev->noa_dcb;
	struct device *dpmaif_dev = DCB_TO_DEV(dpmaif_dcb);
	struct device *noa_dev = DCB_TO_DEV(noa_dcb);
	struct device *dpa_dev = p_md_dev->dpa_res->dpa_dev;
	struct noa_md_fw *md_fw = p_md_dev->md_fw;
	struct noa_md_fw_rx *md_fw_rx = md_fw->rx;
	int rxq_cnt = dpmaif_dcb->drv_info->cfg->rx_cfg.rxq_cnt;
	int bat_ring_num = dpmaif_dcb->drv_info->cfg->rx_cfg.bat_ring_num;
	int ret = 0;

	NOA_MD_RX_INFO("dpmaif_dev:0x%p, noa_dev:0x%p", dpmaif_dev, noa_dev);

	/* Loop 1: Processing RX queues from dpmaif_dcb */
	for (int index = 0; index < rxq_cnt; index++) {
		struct dpmaif_rxq *dpmaif_rxq_source = &dpmaif_dcb->rxqs[index];
		struct noa_rxbm *dpmaif_rxq_dest = &p_md_dev->fw_info.rxqs[index];
		bool contiguous = dpmaif_rxq_source->attr & DPMAIFQ_ATTR_PIT_CACHED;

		NOA_MD_RX_INFO("rxq[%d], contiguous_memory=[%d]", index, contiguous);

		if (contiguous) {
			ret = noa_md_dma_mapper_remap_single(&p_md_dev->rx.mapper,
				dpa_dev,
				(void *)dpmaif_rxq_source->pit_base,
				dpmaif_rxq_source->pit_cnt * sizeof(*dpmaif_rxq_source->pit_base),
				&dpmaif_rxq_dest->pit_dpa_base,
				GFP_KERNEL);
		} else {
			ret = noa_md_dma_mapper_remap_sg(
				&p_md_dev->rx.mapper,
				dpmaif_dev, dpa_dev,
				(void *)dpmaif_rxq_source->pit_base,
				dpmaif_rxq_source->pit_dma_addr,
				dpmaif_rxq_source->pit_cnt * sizeof(*dpmaif_rxq_source->pit_base),
				&dpmaif_rxq_dest->pit_dpa_base,
				GFP_KERNEL);
		}

		NOA_MD_RX_INFO("rxq[%d], pit_base=[0x%lx], pit_dpa_base=[0x%lx]", index,
			(unsigned long)dpmaif_rxq_source->pit_base, dpmaif_rxq_dest->pit_dpa_base);
		if (ret) {
			NOA_MD_RX_ERROR("Fail to remap for modem pit queue%d", index);
			goto error;
		}
		md_fw_rx->dpmaif_rxqs[index].pit_dpa_base = dpmaif_rxq_dest->pit_dpa_base;
	}

	/* Loop 2: Processing RX queues from noa_dcb */
	for (int index = 0; index < rxq_cnt; index++) {
		struct dpmaif_rxq *noa_rxq_source = &noa_dcb->rxqs[index];
		bool contiguous = noa_rxq_source->attr & DPMAIFQ_ATTR_PIT_CACHED;

		NOA_MD_RX_INFO("noa_rxq[%d], contiguous_memory=[%d]", index, contiguous);

		if (contiguous) {
			ret = noa_md_dma_mapper_remap_single(&p_md_dev->rx.mapper,
				dpa_dev,
				(void *)noa_rxq_source->pit_base,
				noa_rxq_source->pit_cnt * sizeof(*noa_rxq_source->pit_base),
				&md_fw_rx->dpmaif_rxqs[index].noa_pit_dpa_base,
				GFP_KERNEL);
		} else {
			ret = noa_md_dma_mapper_remap_sg(
				&p_md_dev->rx.mapper,
				noa_dev, dpa_dev,
				(void *)noa_rxq_source->pit_base,
				noa_rxq_source->pit_dma_addr,
				noa_rxq_source->pit_cnt * sizeof(*noa_rxq_source->pit_base),
				&md_fw_rx->dpmaif_rxqs[index].noa_pit_dpa_base,
				GFP_KERNEL);
		}

		NOA_MD_RX_INFO("noa_rxq[%d], noa_pit_base=[0x%lx], noa_pit_dma_addr=[0x%lx], "
			       "noa_pit_dpa_base=[0x%lx]",
			       index, (unsigned long)noa_rxq_source->pit_base,
			       (unsigned long)noa_rxq_source->pit_dma_addr,
			       md_fw_rx->dpmaif_rxqs[index].noa_pit_dpa_base);
		if (ret) {
			NOA_MD_RX_ERROR("Fail to remap for noa pit queue%d", index);
			goto error;
		}
	}

	for (int index = 0; index < bat_ring_num; index++) {
		struct noa_bat_info *bat_info_source = &md_fw_rx->bat_infos[index];
		struct noa_bat *bat_info_dest = &p_md_dev->fw_info.bat_infos[index];

		bat_info_dest->max_mtu = bat_info_source->max_mtu;
		bat_info_dest->frag_bat_enabled = bat_info_source->frag_bat_enabled;
		bat_info_dest->normal_bat.buf_size = bat_info_source->normal_bat_ring.buf_size;
		bat_info_dest->frag_bat.buf_size = bat_info_source->frag_bat_ring.buf_size;

		NOA_MD_RX_INFO("bat[%d], max_mtu=[%d], frag_bat_enabled=[%d], "
			"normal_bat.buf_size=[%d], frag_bat.buf_siz=[%d]",
			index, bat_info_dest->max_mtu, bat_info_dest->frag_bat_enabled,
			bat_info_dest->normal_bat.buf_size, bat_info_dest->frag_bat.buf_size);

		noa_md_rx_setup_fw_bat(p_md_dev, bat_info_source, bat_info_dest, index);
	}
	return 0;

error:
	noa_md_dma_unmap_all(&p_md_dev->rx.mapper);
	return ret;
}

/**
 * noa_md_rx_queues_remap_release() - Release DMA mappings for all RX queues.
 * @p_md_dev: Pointer to the main noa_md_dev driver structure.
 */
void noa_md_rx_queues_remap_release(struct noa_md_dev *p_md_dev)
{
	noa_md_dma_unmap_all(&p_md_dev->rx.mapper);
}
