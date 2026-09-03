// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA MD Driver
 *
 * Copyright 2024 Google LLC.
 */

#include <linux/wwan.h>

#include "noa_md.h"
#include "noa_md_data_path_ctrl.h"
#include "noa_md_tx_data.h"
#include "noa_md_vpn_tx_data.h"
#include "ncp/md/mediatek/ncp_md.h"
#include "t900/noa_md_mtk_priv.h"
#include "common/ring_id.h"
#include "common/modem_ring_id.h"

const static struct noa_ring_ops noa_md_tx_sw_ring_ops = {
	.write_payload = noa_md_tx_input_desc_write,
	.complete_hook = noa_md_tx_trigger_doorbell,
};

ssize_t noa_md_tx_input_desc_write(
		void *output_buf, size_t buf_len,
		const void *input_data, size_t data_len)
{
	if (buf_len < data_len)
		return -ENOMEM;
	memcpy(output_buf, input_data, data_len);

	return data_len;
}

void noa_md_tx_trigger_doorbell(struct noa_ring_wrapper *ring)
{
	NOA_MD_TX_DATA("enter");
	/* Trigger NOA DMA scheduler to handle input ring */
	if (noa_ring_pos_is_moved(ring)) {
		writel(1, (void *)md_dev.tx.noa_hw_tx_doorbell_addr);
		noa_sim_trig_rx();
	}
}

// TODO: Move to noa_md_utility.c?
static inline u32 noa_md_tx_peek_next_idx(u32 cur_idx, u32 max_cnt)
{
	cur_idx++;
	return (cur_idx == max_cnt) ? 0 : cur_idx;
}

int noa_md_tx_update_ring(
	struct sk_buff *skb,
	dma_addr_t skb_dma_addr,
	struct mtk_tx_pkt_info pkt_info,
	struct noa_tx_ipsec_metadata *metadata) {
	struct noa_md_tx_buffer_desc *tx_buffer_desc = md_dev.tx.tx_buffer_desc;
	noa_ring_producer *ring = &tx_buffer_desc->ring;
	unsigned int cur_idx;
	unsigned char vq_id;
	struct noa_modem_tx_desc raw_desc = {0};
	struct noa_modem_tx_desc *p_desc = &raw_desc;
	unsigned int data_len;
	int ret;
	bool in_tcp_slow_start = false;

	vq_id = pkt_info.q_id;
	NOA_MD_TX_DATA("vq_id=[%d]", vq_id);

	data_len = skb_headlen(skb);

	if (pkt_info.in_tcp_slow_start) {
		in_tcp_slow_start = true;
	}
	cur_idx = tx_buffer_desc->virtual_write_idx;
	NOA_MD_TX_DATA("current virtual_write_idx:%u", cur_idx);
	// NOA md data tx step 6: Fill the descriptor and prepare to put it
	// into the ring buffer
	p_desc->ext.pkt_info.pkt_type = vq_id;
	p_desc->ext.pkt_info.intf_id = pkt_info.intf_id;
	p_desc->ext.pkt_info.network_type = pkt_info.network_type;
	p_desc->ext.pkt_info.src = NCP_MD_FR_APC;
	p_desc->ext.pkt_info.in_tcp_slow_start = in_tcp_slow_start;
	p_desc->ext.pkt_info.drb_cnt = pkt_info.cnt;
	p_desc->ext.vpn.xfrm_interface_id = metadata->xfrm_interface_id;
	NOA_MD_TX_DATA(
		"pkt_type=[%u], intf_id=[%u], network_type=[%u],"
		"in_tcp_slow_start=[%u], drb_cnt=[%u], xfrm_interface_id=[%u]",
		p_desc->ext.pkt_info.pkt_type,
		p_desc->ext.pkt_info.intf_id,
		p_desc->ext.pkt_info.network_type,
		p_desc->ext.pkt_info.in_tcp_slow_start,
		p_desc->ext.pkt_info.drb_cnt,
		p_desc->ext.vpn.xfrm_interface_id);
	p_desc->basic.dp_low = cpu_to_le32(lower_32_bits(skb_dma_addr));
	p_desc->basic.dp_high = cpu_to_le32(upper_32_bits(skb_dma_addr));
	// Virtual address for sw driver mode only
	p_desc->basic.dv = (unsigned long)skb->data;
	p_desc->basic.dl = data_len;
	// FIXME: Need to be changed to NOA_PORT_NETENGINE for VPN
	p_desc->basic.src = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
						 kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);
	p_desc->basic.dst = NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
						 kNoaNetworkFlowHostToDevice, kNoaModemRingTxData);
	p_desc->basic.reason = FWD_REASON_FEEDTHROUGH;
	p_desc->basic.ddone = 0;
	p_desc->basic.cp = NOAD_NO_COPY;
	p_desc->basic.mode = NOAD_MODE_DATA;
	p_desc->basic.desc_type = NOA_DESC_MODEM_TX_MTK;
	p_desc->basic.tkid = tx_buffer_desc->virtual_write_idx;

	// NOA md data tx step 7: Write the descriptor into the ring buffer
	NOA_MD_TX_DATA("call noa_ring_begin_processing");
	ret = noa_ring_begin_processing(ring);
	NOA_MD_TX_DATA("noa_ring_begin_processing - ret:%u", ret);
	if (ret < 0) {
		NOA_MD_TX_ERROR("noa_ring_begin_processing error");
		return ret;
	}
	NOA_MD_TX_DATA("call noa_ring_write");
	ret = noa_ring_write(ring, p_desc, sizeof(*p_desc));
	NOA_MD_TX_DATA("noa_ring_write - ret:%u", ret);
	if (ret < 0) {
		NOA_MD_TX_ERROR("Failed to write tx packet to NOA SW input ring");
		// FIXME: Do not return here because EAGAIN is not processed.
		// Need to review the flow again
		// Temporarily commenting out the return statement to
		// prevent premature exit.
		/*
		return ret;
		*/
	}
	NOA_MD_TX_DATA("call noa_ring_complete_processing");
	noa_ring_complete_processing(ring);
	NOA_MD_TX_DATA("noa_ring_complete_processing end");

	tx_buffer_desc->virtual_write_idx =
		noa_md_tx_peek_next_idx(cur_idx, tx_buffer_desc->num_desc);
	NOA_MD_TX_DATA("update virtual_write_idx:%u",
		tx_buffer_desc->virtual_write_idx);

	// NOA md data tx step 8: Increase the count to be submitted
	atomic_add(1, &tx_buffer_desc->to_submit_cnt);

	return ret;
}

/**
 * noa_md_tx_is_tkid_queues_empty - Check if there is any tethering packet(tkid) pending in the tkid
 * queues from shared memory and waiting for tx done event.
 *
 * Return: True if no tkid is pending in all tkid queues, false when shared memory is NULL or there
 * is any tkid pending in the tkid queues.
 */
static bool noa_md_tx_is_tkid_queues_empty(void)
{
	struct noa_md_shmem_layout *shmem =
		(struct noa_md_shmem_layout *)md_dev.shmem_handle.va_base;
	struct noa_md_tx_tkid_queue_fifo *tx_tkid_queues = NULL;
	bool all_empty = true;
	int i;

	CHECK_PTR_OR_RETURN_ERR(shmem, false);

	tx_tkid_queues = shmem->tx_tkid_queues;

	dma_rmb();
	for (i = 0; i < NOA_MD_MAX_UL_QUEUE_SIZE; i++) {
		u32 head = tx_tkid_queues[i].head;
		u32 tail = tx_tkid_queues[i].tail;

		if (head != tail) {
			NOA_MD_TX_ERROR("txq%d is NOT empty: h=[%u], t=[%u]",
					i, head, tail);
			all_empty = false;
		}
	}

	if (all_empty) {
		NOA_MD_TX_INFO("All TKID queues are empty.");
	}

	return all_empty;
}

static u16 noa_md_tx_get_tkid_from_dma_addr(dma_addr_t dma_base, dma_addr_t dma_addr)
{
	u16 tkid = 0;
	u64 min_dma_addr = dma_base;
	u64 max_dma_addr = dma_base + NOA_MD_MAX_TX_PKT_SIZE * (NOA_MD_FW_FIFO_SIZE - 1);
	if (min_dma_addr <= dma_addr && dma_addr <= max_dma_addr) {
		tkid = (dma_addr - dma_base) / NOA_MD_MAX_TX_PKT_SIZE +
		       NOA_MD_FW_TX_POOL_TKID_OFFSET;
	}
	NOA_MD_TX_DATA("dma_addr=0x%llx, dma_base=0x%llx, tkid=0x%x, min_dma_addr=0x%llx"
		       ", max_dma_addr=0x%llx",
		       dma_addr, dma_base, tkid, min_dma_addr, max_dma_addr);
	return tkid;
}

void noa_md_tx_update_tkid_queues(dma_addr_t dma_addr, int txq_id)
{
	struct noa_md_shmem_layout *shmem =
		(struct noa_md_shmem_layout *)md_dev.shmem_handle.va_base;
	struct noa_md_tx_tkid_queue_fifo *vq;
	u16 tkid = 0;
	u32 head, tail;

	CHECK_PTR_OR_RETURN(shmem);

	if (unlikely(txq_id < 0 || txq_id >= NOA_MD_MAX_UL_QUEUE_SIZE)) {
		NOA_MD_TX_ERROR("Invalid txq_id: %d", txq_id);
		return;
	}

	vq = &shmem->tx_tkid_queues[txq_id];

	dma_rmb();
	head = vq->head;
	tail = vq->tail;
	if (unlikely(head >= NOA_MD_FW_FIFO_SIZE || tail >= NOA_MD_FW_FIFO_SIZE)) {
		NOA_MD_TX_ERROR("txq%d: Invalid head(%u) or tail(%u)", txq_id, head, tail);
		return;
	}

	if (head == tail) {
		NOA_MD_TX_DATA("txq%d: skip updating, queue is empty (h=%d, t=%d)",
				txq_id, head, tail);
		return;
	}

	tkid = noa_md_tx_get_tkid_from_dma_addr(md_dev.tx_buffer_pool.dma_base, dma_addr);
	if (tkid != 0 && tkid == vq->items[head]) {
		NOA_MD_TX_DATA("txq%d: ulq_done for tkid 0x%x, dma_addr=%pad",
				txq_id, tkid, &dma_addr);
		vq->items[head] = 0;
		vq->head = noa_md_tx_peek_next_idx(head, NOA_MD_FW_FIFO_SIZE);
		dma_wmb();
	}
}

static void noa_md_wwan_data_tx_work_func(struct work_struct *work)
{
	int ret = 0;
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct noa_md_tx_work_data *work_data =
		container_of(delayed_work, struct noa_md_tx_work_data, work);
	struct sk_buff *skb = work_data->skb;
	u32 tail;
	dma_addr_t skb_dma_addr;

	// For process old skb that is not yet free
	struct sk_buff *old_skb = NULL;
	dma_addr_t old_dma_addr = 0;

	// For update TX stats
	struct mtk_wwan_instance *wwan_inst = NULL;
	unsigned long tx_bytes = 0;

	NOA_MD_TX_DATA("enter, skb=0x%p, dcb=0x%p", skb, md_dev.dcb);

	CHECK_PTR_OR_RETURN(md_dev.dcb);
	CHECK_PTR_OR_RETURN(skb);
	CHECK_PTR_OR_RETURN(skb->dev);

	// skb has valid dev
	wwan_inst = wwan_netdev_drvpriv(skb->dev);
	if (!wwan_inst) {
		NOA_MD_TX_ERROR("wwan_inst is null");
	}

	tx_bytes = skb->len;

	// NOA md data tx step 3: Perform DMA mapping on skb->data for
	// hardware access
	skb_dma_addr = dma_map_single(
		DCB_TO_DEV(md_dev.dcb), skb->data, skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(DCB_TO_DEV(md_dev.dcb), skb_dma_addr)) {
		NOA_MD_TX_ERROR("DMA mapping failed");
		if(wwan_inst && tx_bytes) {
			noa_trace_tx_drop_inc(&wwan_inst->stats, tx_bytes);
		}
		dev_kfree_skb(skb);
		goto end;
	}

	// NOA md data tx step 4: Put skb and skb_dma_addr into TX queue
	unsigned char q_id = DATA_SKB_CB(skb)->tx.q_id;
	NOA_MD_TX_DATA("q_id=[%d]", q_id);
	if (q_id >= NOA_MD_NUM_TX_QUEUES) {
		NOA_MD_TX_ERROR("q_id=[%d]", q_id);
		goto end;
	}
	struct noa_md_tx_queue *txq = &md_dev.tx.tx_queues[q_id];
	spin_lock(&txq->txq_lock);
	tail = txq->skb_tx_queue_tail;
	if ((tail + 1) % NOA_MD_TX_RING_SIZE == txq->skb_tx_queue_head) {
		spin_unlock(&txq->txq_lock);
		dma_unmap_single(
			DCB_TO_DEV(md_dev.dcb), skb_dma_addr, skb_headlen(skb), DMA_TO_DEVICE);
		if(wwan_inst && tx_bytes) {
			noa_trace_tx_drop_inc(&wwan_inst->stats, tx_bytes);
		}
		dev_kfree_skb(skb);
		NOA_MD_TX_ERROR(
			"TX queue full, packet dropped (total dropped: %lu)\n",
			md_dev.trace->tx_dropped_count);
		goto end;
	}

	// Check original data
	if (txq->skb_tx_queue[tail] != NULL) {
		old_skb = txq->skb_tx_queue[tail];
		txq->skb_tx_queue[tail] = NULL;
		old_dma_addr = txq->skb_tx_dma_queue[tail];
		txq->skb_tx_dma_queue[tail] = 0;
	}
	txq->skb_tx_queue[tail] = skb;
	txq->skb_tx_dma_queue[tail] = skb_dma_addr;
	txq->skb_tx_queue_tail = (tail + 1) % NOA_MD_TX_RING_SIZE;
	spin_unlock(&txq->txq_lock);
	NOA_MD_TX_DATA("q_id=[%d], head=[%d], tail=[%d]",
		q_id, txq->skb_tx_queue_head, txq->skb_tx_queue_tail);

	if (ret <= 0) {
		NOA_MD_TX_ERROR("noa_md_tx_update_ring failed, ret=[%d]", ret);
		// if noa_md_tx_update_ring fail，cancel DMA mapping
		dma_unmap_single(
			DCB_TO_DEV(md_dev.dcb), skb_dma_addr, skb_headlen(skb), DMA_TO_DEVICE);
		dev_kfree_skb(skb);
		goto end;
	}

	// Update TX static
	if (wwan_inst && tx_bytes) {
		noa_trace_tx_inc(&wwan_inst->stats, tx_bytes);
	}

end:
	// Release old skb and DMA address
	if (old_skb) {
		if (old_dma_addr) {
			dma_unmap_single(
				DCB_TO_DEV(md_dev.dcb), old_dma_addr, skb_headlen(old_skb), DMA_TO_DEVICE);
		}
		dev_kfree_skb(old_skb);
	}
	kfree(work_data);
	return;
}

int noa_md_tx_wwan_data(void *data_blk, struct sk_buff *skb)
{
	int ret = 0;
	struct noa_md_tx_work_data *work_data;
	union mtk_data_pkt_info *pkt_info = DATA_SKB_CB(skb);

	if (!md_dev.tx.tx_buffer_desc) {
		NOA_MD_TX_ERROR("tx_buffer_desc is null");
		return -ENODEV;
	}

	// Check whether skb is valid before processing
	if (pkt_info->tx.cnt != 2) {
		// TODO: Need support?
		NOA_MD_TX_ERROR("Not support scatter and gather");
		return -EPROTONOSUPPORT;
	}

	NOA_MD_TX_DATA("data_blk=[0x%p]", data_blk);

	// NOA md data tx step 1: Create a work item and store skb and related
	// information in it
	work_data = kmalloc(sizeof(struct noa_md_tx_work_data), GFP_ATOMIC);
	if(!work_data) {
		NOA_MD_TX_ERROR("Alloc noa_md_tx_work_data failed");
		return -ENOMEM;
	}

	work_data->pkt_info.intf_id = pkt_info->tx.intf_id;
	work_data->pkt_info.network_type = pkt_info->tx.network_type;
	work_data->pkt_info.in_tcp_slow_start = pkt_info->tx.in_tcp_slow_start;
	work_data->pkt_info.cnt = pkt_info->tx.cnt;
	work_data->pkt_info.q_id = pkt_info->tx.q_id;
	work_data->skb = skb;

	// NOA md data tx step 2: Queue the work item for processing by
	// noa_md_wwan_data_tx_work_func
	INIT_DELAYED_WORK(&work_data->work, noa_md_wwan_data_tx_work_func);
	queue_delayed_work(md_dev.tx.noa_md_tx_workqueue, &work_data->work, 0);

	return ret;
}
EXPORT_SYMBOL_GPL(noa_md_tx_wwan_data);

void noa_md_tx_inc(struct sk_buff *skb)
{
	struct mtk_wwan_instance *wwan_inst = NULL;
	if (!skb->dev) {
		return;
	}
	wwan_inst = wwan_netdev_drvpriv(skb->dev);
	CHECK_PTR_OR_RETURN(wwan_inst);
	noa_trace_tx_inc(&wwan_inst->stats, skb->len);
}

void noa_md_tx_drop_inc(struct sk_buff *skb)
{
	struct mtk_wwan_instance *wwan_inst = NULL;
	if (!skb->dev) {
		return;
	}
	wwan_inst = wwan_netdev_drvpriv(skb->dev);
	CHECK_PTR_OR_RETURN(wwan_inst);
	noa_trace_tx_drop_inc(&wwan_inst->stats, skb->len);
}

int noa_md_tx_ring_setup(struct noa_md_dev *p_md_dev)
{
	int ret;
	struct noa_md_tx_buffer_desc *desc = p_md_dev->tx.tx_buffer_desc;
	struct noa_ring_regs regs = { 0 }; //defined in noa\common\noa_hw_ring.h
	struct noa_ring_info info = { //defined in noa\common\ring.h
		.head = 0,
		.tail = 0,
		.size = NOA_MD_TX_RING_SIZE,
		.item_len = NOA_DESC_MODEM_TX_MTK_BYTE,
	}; //defined in noa\sim\nep\nep.h
	NOA_MD_TX_INFO("NoaDpaRingSharedRegsGet");
	ret = NoaDpaRingSharedRegsGet(p_md_dev->dpa_res->dpa, &regs, kNoaNetworkInterfaceModem,
				      kNoaNetworkFlowHostToDevice, kNoaModemRingTxData,
				      kNoaRingNepInput);
	if (ret) {
		NOA_MD_TX_ERROR("NoaDpaRingSharedRegsGet: %d", ret);
		return ret;
	}
	noa_ring_regs_wrapper_init(
		&desc->ring,
		NOA_RING_TYPE_PRODUCER,
		&noa_md_tx_sw_ring_ops,
		&regs,
		p_md_dev->dev,
		"apc_modem_sw_tx",
		0);
	/*ring sw initial*/
	NOA_MD_TX_INFO("dma_alloc_coherent: info.size=[%d], info.item_len=[%d]",
		info.size, info.item_len);
	desc->desc_base = dma_alloc_coherent(
		p_md_dev->dev,
		info.size * info.item_len,
		&desc->skb_dma_addr,
		GFP_KERNEL);
	if (!desc->desc_base) {
		NOA_MD_TX_ERROR("dma alloc fail: %d", desc->desc_base);
		return -ENOMEM;
	}
	NOA_MD_TX_INFO(
		"desc desc_base: 0x%llx", (u64)desc->desc_base);

	info.base = (char *)desc->desc_base;
	info.dpa_base = info.base;
	noa_ring_info_setup(&desc->ring, &info);
	return 0;
}

void noa_md_tx_ring_release(struct noa_md_dev *p_md_dev)
{
	struct noa_md_tx_buffer_desc *desc = p_md_dev->tx.tx_buffer_desc;
	struct noa_ring_wrapper *ring = &desc->ring;
	NOA_MD_TX_INFO("noa_ring_deactivate");
	noa_ring_deactivate(ring);
	if (desc->desc_base) {
		dma_free_coherent(
			p_md_dev->dev,
			desc->ring.basic.size * desc->ring.basic.item_len,
			desc->desc_base,
			desc->skb_dma_addr);
	}
	desc->desc_base = NULL;
	desc->skb_dma_addr = 0;
	noa_ring_info_clean(ring);
}

void noa_md_tx_dpmaif_dump_drb_info(void)
{
	unsigned int drb_dump_ridx;
	unsigned int drb_dump_widx;
	const struct mtk_dpmaif_ctlb *dpmaif_dcb = md_dev.dpmaif_dcb;
	struct dpmaif_drv_info *drv_info = dpmaif_dcb->drv_info;

	for(int index = 0; index < NOA_MD_NUM_TX_QUEUES; index++) {
		struct dpmaif_txq *txq = &dpmaif_dcb->txqs[index];
		drb_dump_widx = drv_info->drv_ops->get_ring_idx(drv_info, DPMAIF_DRB_WIDX,
			txq->id);
		drb_dump_ridx = drv_info->drv_ops->get_ring_idx(drv_info, DPMAIF_DRB_RIDX,
			txq->id);
		NOA_MD_TX_ERROR(
			"q%d: w=%u,r=%u\n", index, drb_dump_widx, drb_dump_ridx);
	}
}

static unsigned int noa_md_tx_drb_skb_count(
	unsigned int total_count,
	unsigned int from_index,
	unsigned int to_index)
{
	unsigned int drb_skb_cnt;

	if (from_index <= to_index)
		drb_skb_cnt = to_index - from_index;
	else
		drb_skb_cnt = total_count + to_index - from_index;

	return drb_skb_cnt;
}

/**
 * noa_md_tx_update_ring_info_for_offload_path - Update tx rings'
 * indices for offload path.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 * @ap_state: Pointer to the AP state to send to the NCP.
 *
 * This function is called to update modem rings' indices to ap_state
 * and handle incomplete drb skb from dpmaif to noa.
 */
static void noa_md_tx_update_ring_info_for_offload_path(
			struct noa_md_dev *p_md_dev,
			struct dpath_ap_state_payload *ap_state)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	int drb_skb_cnt = 0;
	int cur_idx = 0;

	CHECK_PTR_OR_RETURN(p_md_dev);
	CHECK_PTR_OR_RETURN(ap_state);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN(dpmaif_dcb);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN(noa_dcb);

	for (int index = 0; index < NOA_MD_NUM_TX_QUEUES; index++) {
		// Update modem rings' indices to shared memory
		const struct dpmaif_txq *txq_source = &dpmaif_dcb->txqs[index];
		struct tx_ring_idx *txq_dest = &ap_state->txqs[index];
		txq_dest->drb_wr_idx = txq_source->drb_wr_idx;
		txq_dest->drb_rd_idx = txq_source->drb_rd_idx;
		txq_dest->drb_rel_rd_idx = txq_source->drb_rel_rd_idx;

		NOA_MD_TX_INFO("txqs[%d] drb_wr_idx=%u, drb_rd_idx=%u, drb_rel_rd_idx=%u",
				index,
				txq_dest->drb_wr_idx,
				txq_dest->drb_rd_idx,
				txq_dest->drb_rel_rd_idx);

		drb_skb_cnt = noa_md_tx_drb_skb_count(txq_source->drb_cnt,
						      txq_source->drb_rd_idx,
						      txq_source->drb_wr_idx);

		// Move incomplete drb skb records from dpmaif to noa
		if (likely(drb_skb_cnt > 0))  {
			cur_idx = txq_source->drb_rd_idx;
			for (int count = 0; count < drb_skb_cnt; count++) {
				struct dpmaif_drb_skb *drb_skb_source =
						dpmaif_dcb->txqs[index].sw_drb_base + cur_idx;
				struct dpmaif_drb_skb *drb_skb_dest =
						noa_dcb->txqs[index].sw_drb_base + cur_idx;
				// Copy drb skb records from dpmaif to noa
				memcpy(drb_skb_dest, drb_skb_source, sizeof(struct dpmaif_drb_skb));
				// Clear drb skb records for dpmaif
				memset(drb_skb_source, 0x0, sizeof(struct dpmaif_drb_skb));
				cur_idx = noa_md_tx_peek_next_idx(cur_idx, txq_source->drb_cnt);
			}
		}

		// Update modem rings' indices to apc2ncp tx rings
		struct dpmaif_txq *apc2ncp_txq = &noa_dcb->txqs[index];
		apc2ncp_txq->drb_wr_idx = txq_source->drb_wr_idx;
		apc2ncp_txq->drb_rd_idx = txq_source->drb_rd_idx;
		apc2ncp_txq->drb_rel_rd_idx = txq_source->drb_rel_rd_idx;
	}
}

/**
 * noa_md_tx_update_ring_info_for_direct_path - Update tx rings' indices.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 * @ncp_state: Pointer to the NCP state receive from the NCP.
 *
 * This function is called to update modem and noa rings' indices from ncp_state
 * and handle incomplete drb skb from noa to dpmaif.
 */
static void noa_md_tx_update_ring_info_for_direct_path(
			struct noa_md_dev *p_md_dev,
			const struct dpath_ncp_state_payload *ncp_state)
{

	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	int drb_skb_cnt = 0, count;
	u32 source_cur_idx, dest_cur_idx, cur_idx;
	u32 drb_rd_idx, drb_temp_rd_idx, drb_wr_idx;

	CHECK_PTR_OR_RETURN(p_md_dev);
	CHECK_PTR_OR_RETURN(ncp_state);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN(dpmaif_dcb);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN(noa_dcb);

	for (int index = 0; index < NOA_MD_NUM_TX_QUEUES; index++) {
		// Update modem rings' indices from shared memory
		const struct tx_ring_idx *txq_source = &ncp_state->txqs[index];
		struct dpmaif_txq *txq_dest = &dpmaif_dcb->txqs[index];
		txq_dest->drb_wr_idx = txq_source->drb_wr_idx;
		txq_dest->drb_rd_idx = txq_source->drb_rd_idx;
		txq_dest->drb_rel_rd_idx = txq_source->drb_rel_rd_idx;

		NOA_MD_TX_INFO("txqs[%d] drb_wr_idx=%u, drb_rd_idx=%u, drb_rel_rd_idx=%u",
				index,
				txq_dest->drb_wr_idx,
				txq_dest->drb_rd_idx,
				txq_dest->drb_rel_rd_idx);

		// case 1: TX packets already sent to the modem DRB rings
		// but tx done not yet reported.
		const struct tx_apc2ncp_ring_idx *apc2ncp_txq = &ncp_state->apc2ncp_txqs[index];
		drb_wr_idx = apc2ncp_txq->drb_wr_idx;
		drb_rd_idx = apc2ncp_txq->drb_rd_idx;
		drb_temp_rd_idx = apc2ncp_txq->drb_temp_rd_idx;

		NOA_MD_TX_INFO("apc2ncp_txqs[%d] drb_wr_idx=%u, drb_rd_idx=%u, drb_temp_rd_idx=%u",
				index, drb_wr_idx, drb_rd_idx, drb_temp_rd_idx);

		drb_skb_cnt = noa_md_tx_drb_skb_count(txq_dest->drb_cnt,
							drb_rd_idx,
							drb_temp_rd_idx);

		// Move incomplete drb skb records from noa to dpmaif
		if (likely(drb_skb_cnt > 0))  {
			source_cur_idx = drb_rd_idx;
			dest_cur_idx = txq_dest->drb_rd_idx;
			for (count = 0; count < drb_skb_cnt; count++) {
				struct dpmaif_drb_skb *drb_skb_source =
					noa_dcb->txqs[index].sw_drb_base + source_cur_idx;
				struct dpmaif_drb_skb *drb_skb_dest =
					dpmaif_dcb->txqs[index].sw_drb_base + dest_cur_idx;
				// Copy drb skb records from noa to dpmaif
				memcpy(drb_skb_dest, drb_skb_source, sizeof(struct dpmaif_drb_skb));
				// Clear drb skb records for noa
				memset(drb_skb_source, 0x0, sizeof(struct dpmaif_drb_skb));
				// Update the next index
				source_cur_idx =
					noa_md_tx_peek_next_idx(source_cur_idx, txq_dest->drb_cnt);
				dest_cur_idx =
					noa_md_tx_peek_next_idx(dest_cur_idx, txq_dest->drb_cnt);
			}
		}

		// Skip case 2 and 3 for LOW_LATENCY by alcedo design
		if (txq_dest->attr & DPMAIFQ_ATTR_LOW_LATENCY) {
			continue;
		}

		// case 2: TX packets are in NOA DRB rings.
		drb_skb_cnt = noa_md_tx_drb_skb_count(txq_dest->drb_cnt,
						      drb_temp_rd_idx,
						      drb_wr_idx);
		struct dpmaif_vq *dpmaif_vq = &dpmaif_dcb->tx_vqs[index];
		if (likely(drb_skb_cnt > 0)) {
			cur_idx = drb_temp_rd_idx;
			for (count = 0; count < drb_skb_cnt; count++) {
				struct dpmaif_drb_skb *drb_skb =
					noa_dcb->txqs[index].sw_drb_base + cur_idx;
				if (drb_skb->is_msg) {
					skb_queue_tail(&dpmaif_vq->list, drb_skb->skb);
				}
				cur_idx = noa_md_tx_peek_next_idx(cur_idx, txq_dest->drb_cnt);
			}
		}

		// case 3: TX packets are still in the vq list.
		struct dpmaif_vq *noa_vq = &noa_dcb->tx_vqs[index];
		while (!skb_queue_empty(&noa_vq->list)) {
			struct sk_buff *cur_skb = skb_dequeue(&noa_vq->list);
			skb_queue_tail(&dpmaif_vq->list, cur_skb);
		}
	}
}

/**
 * noa_md_tx_on_state_change() - Handles state changes from the controller.
 * @state:       The new state to handle.
 * @target_path: The final data path destination.
 * @ncp_state:   NCP state, valid in DEVICE_PREPARING and SERVICE_RESTARTING.
 *
 * This function executes the TX-specific logic for each step of the switch.
 */
static void noa_md_tx_on_state_change(
	struct noa_dpath_client *client,
	enum dpath_switch_state state,
	enum dpa_data_path target_path,
	const struct dpath_ncp_state_payload *ncp_state)
{
	struct noa_md_dpath_ctrl *ctrl = client->ctrl;
	struct noa_md_dev *p_md_dev = ctrl->dev;
	struct noa_md_tx *tx = &p_md_dev->tx;
	bool success = true;

	NOA_MD_TX_INFO("Handling state: %d", state);

	switch (state) {
	case NOA_MD_DPATH_STATE_DEVICE_PREPARING:
		if (target_path == NOA_DATA_PATH_OFFLOAD) {
			noa_md_tx_update_ring_info_for_offload_path(
					p_md_dev, &ctrl->ap_state);

			if (!noa_md_tx_is_tkid_queues_empty()) {
				NOA_MD_TX_ERROR("switch failed due to tkid_queues is not empty");
				success = false;
				break;
			}
			noa_md_ring_service_tx_activate(true);
		}
		break;

	case NOA_MD_DPATH_STATE_DEVICE_RESUMING:
		if (target_path == NOA_DATA_PATH_DIRECT) {
			noa_md_tx_update_ring_info_for_direct_path(p_md_dev, ncp_state);
			noa_md_ring_service_tx_activate(false);
		}
		break;

	case NOA_MD_DPATH_STATE_ROLLING_BACK:
		// TODO: b/434646935 - Implement rollback logic. This should revert any
		// partial configuration changes made during the switch and restore the
		// TX path to its previous stable state.
		break;

	case NOA_MD_DPATH_STATE_SERVICE_STOPPING:
	case NOA_MD_DPATH_STATE_SERVICE_RESTARTING:
	default:
		/* IDLE, FAILED states do not require action from this module. */
		break;
	}

	// Report completion back to the controller.
	noa_md_dpath_ctrl_report_completion(tx->dpath_client, success);
}

/* Define the operations struct with our callback function */
static struct noa_dpath_client_ops tx_dpath_ops = {
	.on_state_change = noa_md_tx_on_state_change,
};

int noa_md_tx_setup(void)
{
	struct noa_md_tx *tx = &md_dev.tx;
	int i, ret = 0;

	NOA_MD_TX_INFO("enter");

	/* Prevent re-initialization */
	if (unlikely(tx->tx_buffer_desc)) {
		NOA_MD_TX_ERROR("TX resources seem to be already initialized.");
		return -EALREADY;
	}

	/* 1. Allocate main resources */
	tx->tx_buffer_desc = kzalloc(sizeof(*tx->tx_buffer_desc), GFP_KERNEL);
	if (!tx->tx_buffer_desc) {
		NOA_MD_TX_ERROR("Failed to allocate tx_buffer_desc");
		return -ENOMEM;
	}

	tx->noa_md_tx_workqueue = alloc_workqueue(
		"noa_md_tx_workqueue", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!tx->noa_md_tx_workqueue) {
		ret = -ENOMEM;
		goto err_free_buffer_desc;
	}

	tx->noa_md_tx_done_workqueue = alloc_workqueue(
		"noa_md_tx_done_workqueue", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!tx->noa_md_tx_done_workqueue) {
		ret = -ENOMEM;
		goto err_destroy_tx_wq;
	}

	/* 2. Initialize software queues and descriptors */
	for (i = 0; i < NOA_MD_NUM_TX_QUEUES; i++) {
		spin_lock_init(&tx->tx_queues[i].txq_lock);
		tx->tx_queues[i].skb_tx_queue_head = 0;
		tx->tx_queues[i].skb_tx_queue_tail = 0;
	}

	tx->tx_buffer_desc->num_desc = NOA_MD_TXQ_SIZE;
	tx->tx_buffer_desc->virtual_write_idx = 0;

	/* 3. Setup sub-modules */
	ret = noa_md_dma_mapper_init(&tx->mapper, "noa_tx_mapper");
	if (ret) {
		NOA_MD_TX_ERROR("Failed to initialize tx dma mapper: %d", ret);
		goto err_destroy_done_wq;
	}

	ret = noa_md_vpn_tx_queues_setup(
		tx, NOA_MD_NUM_VPN_TX_QUEUES, NOA_MD_VPN_TX_QUEUE_SIZE);
	if (ret) {
		NOA_MD_TX_ERROR("VPN TX queues setup failed: %d", ret);
		goto err_release_mapper;
	}

	tx->dpath_client = noa_md_dpath_ctrl_register_client(
		&md_dev, NOA_DPATH_CLIENT_TX, &tx_dpath_ops);
	if (IS_ERR(tx->dpath_client)) {
		ret = PTR_ERR(tx->dpath_client);
		tx->dpath_client = NULL;
		NOA_MD_TX_ERROR("Failed to register with dpath ctrl: %d", ret);
		goto err_release_vpn_queues;
	}

	NOA_MD_TX_INFO("TX setup successful");
	return 0;

err_release_vpn_queues:
	noa_md_vpn_tx_queues_release(tx);
err_release_mapper:
	noa_md_dma_unmap_all(&tx->mapper);
	noa_md_dma_mapper_release(&tx->mapper);
err_destroy_done_wq:
	destroy_workqueue(tx->noa_md_tx_done_workqueue);
	tx->noa_md_tx_done_workqueue = NULL;
err_destroy_tx_wq:
	destroy_workqueue(tx->noa_md_tx_workqueue);
	tx->noa_md_tx_workqueue = NULL;
err_free_buffer_desc:
	kfree(tx->tx_buffer_desc);
	tx->tx_buffer_desc = NULL;

	NOA_MD_TX_ERROR("TX setup failed, ret=%d", ret);
	return ret;
}

/**
 * noa_md_tx_release() - Releases all software resources for the TX path.
 *
 * Tears down workqueues, unregisters clients, and frees allocated memory.
 */
void noa_md_tx_release(void)
{
	struct noa_md_tx *tx = &md_dev.tx;

	NOA_MD_TX_INFO("enter");

	/* 1. Unregister from data path controller */
	if (tx->dpath_client) {
		noa_md_dpath_ctrl_unregister_client(&md_dev, tx->dpath_client);
		tx->dpath_client = NULL;
	}

	/* 2. Release VPN queue resources */
	noa_md_vpn_tx_queues_release(tx);

	/* 3. Destroy the main TX workqueues */
	if (tx->noa_md_tx_done_workqueue) {
		destroy_workqueue(tx->noa_md_tx_done_workqueue);
		tx->noa_md_tx_done_workqueue = NULL;
	}
	if (tx->noa_md_tx_workqueue) {
		destroy_workqueue(tx->noa_md_tx_workqueue);
		tx->noa_md_tx_workqueue = NULL;
	}

	/* 4. Release the dedicated TX DMA mapper resources */
	noa_md_dma_unmap_all(&tx->mapper);
	noa_md_dma_mapper_release(&tx->mapper);

	/* 5. Free the main TX buffer descriptor */
	if (tx->tx_buffer_desc) {
		kfree(tx->tx_buffer_desc);
		tx->tx_buffer_desc = NULL;
	}

	NOA_MD_TX_INFO("exit");
}
