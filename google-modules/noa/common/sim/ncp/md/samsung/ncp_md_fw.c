// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD FW
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>
 */

#include "ncp_md_fw.h"
#include "common/core.h" // think about real chip case
#include "common/ring.h"
#include "common/ring_id.h"
#include "common/modem_ring_id.h"
#include "nep/nep.h"
#include "nep/ring_manager.h"

/*
 * Since we introduced the buffer pool mechanism in Ring Service on modem FW TX pool,
 * we no longer include buffer addresses in the input ring. Therefore, we need a new
 * way to track buffer recycling and distinguish between buffers that belong to modem
 * FW TX pool and those belong to apc sides. So, we offset the tkid in the TX pool by
 * a specific value (0x8000). Only tkid exceeding this value belong to the modem FW TX
 * pool and require recycling; otherwise, they don't need to be recycled.
 */
#define NOA_MD_FW_TX_POOL_TKID_OFFSET ((u16)(0x8000U))
#define NOA_MD_FW_OUT_FIFO_SIZE 880 // the max size of uplink low prio queue
#define NOA_MD_FW_IN_FIFO_SIZE 13824 // ensure enough resources
static struct mr_lassen_tx_desc noa_md_fw_out_fifo[NOA_MD_FW_OUT_FIFO_SIZE] = { 0 };
static struct mr_lassen_rx_desc noa_md_fw_in_fifo[NOA_MD_FW_IN_FIFO_SIZE] = { 0 };
static u16 INVALID_REF_IDX_VALUE = 0xFFFF;
static u16 ul_ref_idx[NOA_MD_FW_OUT_FIFO_SIZE] = { 0 };

static noa_buffer_pool_desc g_pool_ring_buf[NOA_MD_FW_IN_FIFO_SIZE] = { 0 };

// devices's interrupt service routine
static irqreturn_t md_dev_irq_handler(int irq, void *arg)
{
	struct noa_pktproc_queue_dl *p_dev_q =
		(struct noa_pktproc_queue_dl *)arg;
	tasklet_schedule(&p_dev_q->q_task);
	return IRQ_HANDLED;
}

static void md_fw_tx_buffer_pool_deinit(struct ncp_md_adaptor *p_adaptor)
{
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, false, buffer_pool_ring_id,
				      kNoaRingNepInput);
	noa_ring_deactivate(&p_adaptor->tx_pool_ring);
	noa_ring_info_clean(&p_adaptor->tx_pool_ring);
}

static ssize_t refill_buffer(void *buf, size_t buf_len, const void *data, size_t data_len)
{
	*((noa_buffer_pool_desc *)buf) = *(const noa_buffer_pool_desc *)data;
	return sizeof(noa_buffer_pool_desc);
}

const static struct noa_ring_ops pool_ring_ops = {
	.write_payload = refill_buffer,
};

static void md_fw_tx_buffer_pool_init(struct ncp_md_adaptor *p_adaptor)
{
	const u8 buffer_pool_ring_id = NoaRingPathIdConvert(
		kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool);
	struct mr_pktproc_adaptor_ul *p_mr_ppa_ul = p_adaptor->p_mr_ppa_ul;
	struct mr_pktproc_queue_ul *p_mr_dev_q_ul =
		p_mr_ppa_ul->dev_q[1]; // always use low prio queue;
	int ret;
	int i;
	int pool_size = NOA_MD_FW_OUT_FIFO_SIZE;
	u32 max_packet_size_w_headroom = p_mr_dev_q_ul->max_packet_size;
	struct noa_ring_regs ring_regs = { 0 };
	struct noa_ring_info ring_info = {
		.head = pool_size - 1,
		.tail = 0,
		.base = (char *)&g_pool_ring_buf[0],
		.size = pool_size,
		.item_len = sizeof(noa_buffer_pool_desc),
		.dpa_base = ring_info.base,
	};

	for (i = 0; i < ring_info.head; i++) {
		// NEP use noa_desc.dv for memory copying in driver mode
		// put VA to noa_desc.dv
		// put PA to noa_desc.dp
		noa_buffer_pool_desc *item = (noa_buffer_pool_desc *)noa_ring_buf_pos(
			ring_info.base, i, ring_info.item_len);
		item->tkid = i + NOA_MD_FW_TX_POOL_TKID_OFFSET;
		item->dp_low = p_mr_dev_q_ul->cp_buff_pbase + max_packet_size_w_headroom * i;
		item->dp_high = 0;
		item->dv = (unsigned long)(p_mr_dev_q_ul->q_buff_vbase +
					   max_packet_size_w_headroom * i);
	}

	ret = NoaRingSharedRegsGet(&ring_regs, kNoaNetworkInterfaceModem,
				   kNoaNetworkFlowDeviceToHost, kNoaModemNepBufferPool,
				   kNoaRingNepInput);
	if (ret) {
		pr_err("Failed to get md buffer pool regs, ret %d\n", ret);
		goto out;
	}

	ret = noa_ring_regs_wrapper_init(&p_adaptor->tx_pool_ring, NOA_RING_TYPE_PRODUCER,
					 &pool_ring_ops, &ring_regs, NULL, "md buf", 0);
	if (ret) {
		pr_err("Failed to init md buf ring, ret %d\n", ret);
		goto out;
	}
	noa_ring_info_setup(&p_adaptor->tx_pool_ring, &ring_info);
	noa_ring_activate(&p_adaptor->tx_pool_ring);

	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true, buffer_pool_ring_id,
				      kNoaRingNepInput);
	ret = 0;

out:
	if (ret) {
		md_fw_tx_buffer_pool_deinit(p_adaptor);
	}
}

static int md_fw_tx_pool_replenish(struct ncp_md_adaptor *p_adaptor, u32 read_idx)
{
	int ret = -1;
	struct mr_pktproc_adaptor_ul *p_mr_ppa_ul = p_adaptor->p_mr_ppa_ul;
	struct mr_pktproc_queue_ul *p_mr_dev_q_ul =
		p_mr_ppa_ul->dev_q[1]; // always use low prio queue;
	struct mr_lassen_tx_desc *p_noa_desc =
		(struct mr_lassen_tx_desc *)(unsigned long)p_adaptor->p_noa2md_ring->base;
	struct noa_desc *p_desc = &(p_noa_desc[read_idx].basic);
	noa_ring_producer *ring = &p_adaptor->tx_pool_ring;
	u16 pktid = p_desc->tkid;
	noa_buffer_pool_desc item;
	u16 idx = pktid - NOA_MD_FW_TX_POOL_TKID_OFFSET;

	if (pktid < NOA_MD_FW_TX_POOL_TKID_OFFSET || idx >= NOA_MD_FW_IN_FIFO_SIZE) {
		return 0;
	}

	ret = noa_ring_begin_processing(ring);
	if (ret <= 0) {
		pr_err("Ring %s is not ready, ret %d\n", ring->name, ret);
		return ret;
	}

	item.tkid = pktid;
	item.dp_low = p_mr_dev_q_ul->cp_buff_pbase + p_mr_dev_q_ul->max_packet_size * idx;
	item.dp_high = 0;
	item.dv =
		(unsigned long)(p_mr_dev_q_ul->q_buff_vbase + p_mr_dev_q_ul->max_packet_size * idx);
	ret = noa_ring_write(ring, &item, sizeof(item));
	if (ret < 0) {
		pr_err("Failed to replenish tkid %u md pool, head %u, tail %u, ret %d\n", pktid,
		       ring->basic.head, noa_ring_tail_read_once(ring), ret);
		goto out;
	}

	ret = 0;
out:
	noa_ring_complete_processing(ring);
	return ret;
}

static void md_fw_noa2md_read_increment(struct ncp_md_adaptor *p_adaptor)
{
	struct noa_ring *p_noa2md_ring = p_adaptor->p_noa2md_ring;
	md_fw_tx_pool_replenish(p_adaptor, p_noa2md_ring->read);
	smp_store_release(&p_noa2md_ring->read,
			  (u32)_noa_dma_get_next_idx(p_noa2md_ring->read, p_noa2md_ring->ctrl));
}

void md_fw_out_fifo_init(struct mr_pktproc_adaptor_ul *p_mr_ppa_ul)
{
	struct noa_ring *ring =
		NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				     kNoaModemRingTxData, kNoaRingNepOutput);
	struct mr_pktproc_queue_ul *p_mr_dev_q_ul = NULL;
	int i;

	ring->base  = (unsigned long)&noa_md_fw_out_fifo[0];
	ring->len = sizeof(struct mr_lassen_tx_desc);
	ring->ctrl  = NOA_MD_FW_OUT_FIFO_SIZE;
	ring->write = 0;
	ring->read  = 0;
	ring->dpa_base = ring->base;

	/* Fill out output ring's desc here */
	p_mr_dev_q_ul =
		p_mr_ppa_ul->dev_q[1]; // always use low prio queue
	if (!p_mr_dev_q_ul) {
		panic("dev q is NULL!");
	}

	if (ring->ctrl != p_mr_dev_q_ul->num_desc) {
		panic("NUM DESC is inconsistent! max cnt(%d), num desc(%d)\n",
			ring->ctrl,
			p_mr_dev_q_ul->num_desc);
	}

	/*
	for (i = 0; i < 10; i++) {
		ncp_md_info("[%d]: dp:0x%llx, dv:0x%llx\n",
			i,
			p_desc[i].dp_low,
			p_desc[i].dv);
	}
	*/

	for (i = 0; i < NOA_MD_FW_OUT_FIFO_SIZE; i++) {
		ul_ref_idx[i] = INVALID_REF_IDX_VALUE;
	}
}

void md_fw_in_fifo_init(void)
{
	struct noa_ring *ring =
		NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				     kNoaModemRingRxData, kNoaRingNepInput);
	ring->base = (unsigned long)&noa_md_fw_in_fifo[0];
	ring->len = sizeof(struct mr_lassen_rx_desc);
	ring->ctrl = NOA_MD_FW_IN_FIFO_SIZE;
	ring->write = 0;
	ring->read = 0;
	ring->dpa_base = ring->base;
}

static void md_fw_rx_pkt_task(unsigned long data);

void mr2md_handling_task(int src, struct ncp_md_adaptor *p_adaptor)
{
	struct noa_ring *p_noa2md_ring
		= p_adaptor->p_noa2md_ring;
	struct mr_lassen_tx_desc *p_noa_desc =
		(struct mr_lassen_tx_desc *)(unsigned long)p_adaptor->p_noa2md_ring->base;
	struct noa_desc *p_desc = NULL;
	struct hrtimer *p_timer = NULL;

	u16 rd      = p_adaptor->noa2md_ring_done_ptr;
	u16 wr = (u16)smp_load_acquire(&p_noa2md_ring->write);
	u16 max_cnt = p_noa2md_ring->ctrl;
	u16 cur_cnt = _noa_dma_get_read_count(rd, wr, max_cnt);
	int i       = 0;
	int ret     = 0;

	p_timer = &p_adaptor->timer_tx_recycle;
	if (cur_cnt == 0) {
		return;
	}
	// this path will exist two types
	// type 1: rx refill buffers
	// (noa_desc.mode = NOAD_MODE_FEEDBACK)
	// type 2: tx data (copied by NEP)
	// (noa_desc.mode = NOAD_MODE_DATA)
	for (i = 0; i < cur_cnt; i++) {
		p_desc = &(p_noa_desc[p_adaptor->noa2md_ring_done_ptr].basic);
		switch (p_desc->mode) {
		case NOAD_MODE_FEEDBACK:
			ret = rx_refill(p_adaptor, p_desc);
			break;
		case NOAD_MODE_DATA:
			// Tx Data Task
			ret = tx_data_handling(p_adaptor, p_desc, p_adaptor->noa2md_ring_done_ptr);
			break;
		default:
			// Error, unknown Mode
			pr_err("Unknown Mode(%d)\n",
			       p_noa_desc[p_adaptor->noa2md_ring_done_ptr].basic.mode);
			break;
		}

		if (ret) {
			// No Memory
			// Current DESC is not handled!
			// Don't advance index
			break;
		}

		p_adaptor->noa2md_ring_done_ptr = _noa_dma_get_next_idx(
				p_adaptor->noa2md_ring_done_ptr,
				max_cnt);
	}

	if (!hrtimer_is_queued(p_timer)) {
		ktime_t ktime = ktime_set(0, p_adaptor->tx_recycle_timeout_ns);
		hrtimer_start(p_timer, ktime, HRTIMER_MODE_REL);
	}

	if (ret) {
		//TODO:
		//reschedule self
		panic("TODO: dest ring is out-of-resource!");
	}
}
EXPORT_SYMBOL_GPL(mr2md_handling_task);

void md_refill_from_apc(struct ncp_md_adaptor *p_adaptor)
{
	struct noa_ring *p_noa2md_ring
		= p_adaptor->p_noa2md_ring;
	struct mr_lassen_tx_desc *p_noa_desc =
		(struct mr_lassen_tx_desc *)(unsigned long)p_adaptor->p_noa2md_ring->base;

	struct noa_pktproc_queue_dl *p_dev_q = NULL;
	struct noa_pktproc_adaptor_dl *p_dev_ppa_dl =
		p_adaptor->p_noa_ppa_dl;
	struct noa_pktproc_desc_sktbuf *p_dev_desc = NULL;

	u16 r = p_noa2md_ring->read, w = (u16)smp_load_acquire(&p_noa2md_ring->write),
	    len = p_noa2md_ring->ctrl;
	//u16 sz = p_noa2md_ring->len;
	u16 cnt = _noa_dma_get_read_count(r, w, len);
	int i = 0;

	u32 fore;
	u8 skip_cnt;

	// input has "cnt" resources (free addresses)
	skip_cnt = 0;
	for (i = 0 ; i < cnt; i++) {
		if (skip_cnt == p_dev_ppa_dl->num_queue) {
			// all device queue is full
			break;
		}
		// transfer to device q desc in RR
		p_dev_q =
			p_dev_ppa_dl->dev_q[p_adaptor->dev_q_rr_start_idx];

		p_adaptor->dev_q_rr_start_idx = _noa_dma_get_next_idx(
			p_adaptor->dev_q_rr_start_idx,
			p_dev_ppa_dl->num_queue);

		fore = *p_dev_q->fore_ptr;
		if (_noa_dma_get_next_idx(fore, p_dev_q->num_desc) ==
			p_dev_q->done_ptr) {
			skip_cnt++;
			continue;
		}

		p_dev_desc = p_dev_q->desc_sktbuf;

		/* Write device desc here */
		p_dev_desc[fore].cp_data_paddr = p_noa_desc[p_noa2md_ring->read].basic.dp_low;

		if (0 == fore)
			p_dev_desc[fore].control |= (1 << 7); // HEAD

		if (fore == (p_dev_q->num_desc - 1))
			p_dev_desc[fore].control |= (1 << 3); // RINGED

		*p_dev_q->fore_ptr = _noa_dma_get_next_idx(
			*p_dev_q->fore_ptr, p_dev_q->num_desc);
		md_fw_noa2md_read_increment(p_adaptor);
	}
}

int rx_refill(struct ncp_md_adaptor *p_adaptor, struct noa_desc *p_desc)
{
	struct noa_pktproc_queue_dl *p_dev_q = NULL;
	struct noa_pktproc_adaptor_dl *p_dev_ppa_dl =
		p_adaptor->p_noa_ppa_dl;
	struct noa_pktproc_desc_sktbuf *p_dev_desc = NULL;
	u32 fore;
	int ret = 0;
	unsigned long dst_paddr;

	// Only support ONE queue now
	p_dev_q = p_dev_ppa_dl->dev_q[0];
	fore = *p_dev_q->fore_ptr;

	if (_noa_dma_get_next_idx(fore, p_dev_q->num_desc) ==
		p_dev_q->done_ptr) {
		// device queue is full
		return -ENOMEM;
	}

	p_dev_desc = p_dev_q->desc_sktbuf;

	/* Write device desc here */
	dst_paddr =
		p_dev_ppa_dl->cp_buff_pbase +
		(p_dev_ppa_dl->true_packet_size * p_desc->tkid) +
		p_dev_ppa_dl->skb_padding_size;

	p_dev_desc[fore].cp_data_paddr =
		dst_paddr;

	if (0 == fore)
		p_dev_desc[fore].control |= (1 << 7); // HEAD

	if (fore == (p_dev_q->num_desc - 1))
		p_dev_desc[fore].control |= (1 << 3); // RINGED

	*p_dev_q->fore_ptr = _noa_dma_get_next_idx(
		*p_dev_q->fore_ptr, p_dev_q->num_desc);

	return ret;
}

int tx_data_handling(
	struct ncp_md_adaptor *p_adaptor,
	struct noa_desc *p_desc,
	const int desc_rd_idx)
{
	struct mr_pktproc_adaptor_ul *p_mr_ppa_ul =
		p_adaptor->p_mr_ppa_ul;
	struct mr_pktproc_queue_ul *p_mr_dev_q_ul = NULL;
	struct mr_pktproc_desc_ul *p_dev_desc = NULL;
	struct mr_lassen_ext_txd *p_txd_ext = NULL;
	u32 done;
	//u64 data_ptr;
	int ret = 0;
	// for Debug information
	/*
	struct noa_ring *p_noa2md_ring
		= p_adaptor->p_noa2md_ring;
	*/

	p_mr_dev_q_ul =
		p_mr_ppa_ul->dev_q[1]; // always use low prio queue
	done = p_mr_dev_q_ul->done_ptr;

	if (_noa_dma_get_next_idx(done, p_mr_dev_q_ul->num_desc) ==
		*p_mr_dev_q_ul->rear_ptr) {
		return -ENOMEM;
	}

	/*
	data_ptr = p_desc->dp_high;
	data_ptr <<= 32;
	data_ptr |= p_desc->dp_low;
	*/
	// simulation uses VA
	//data_ptr = p_desc->dv;
	p_txd_ext = (struct mr_lassen_ext_txd *)p_desc->ext_data;

	// fill out desc context
	p_dev_desc = &p_mr_dev_q_ul->desc_ul[done];
	p_dev_desc->sktbuf_point = p_desc->dp_low;
	p_dev_desc->data_size = p_desc->dl;
	p_dev_desc->total_pkt_size = p_dev_desc->data_size;
	p_dev_desc->last_desc = 1;
	p_dev_desc->seg_on = 0;
	p_dev_desc->hw_set = 0;
	p_dev_desc->lcid = p_txd_ext->channel_id;

	if (MR_TO_MD_FR_WIFI == p_txd_ext->src) {
		p_dev_desc->data_size += p_mr_ppa_ul->cp_padding;
		p_dev_desc->total_pkt_size += p_mr_ppa_ul->cp_padding;
	}

	// Debug information
	/*
	ncp_md_info("DESC[%d]:sktbuf_point(0x%lx), data_size(%d), lcid(%d),"
				"headroom_sz(%d), fore(%d), rear(%d), read(%d)\n",
		desc_rd_idx,
		p_dev_desc->sktbuf_point,
		p_dev_desc->data_size,
		p_dev_desc->lcid,
		p_mr_dev_q_ul->headroom_sz,
		*p_mr_dev_q_ul->fore_ptr,
		*p_mr_dev_q_ul->rear_ptr,
		p_noa2md_ring->read
		);
	*/

	barrier();

	if (unlikely(INVALID_REF_IDX_VALUE != ul_ref_idx[desc_rd_idx])) {
		panic("ul_ref_idx[%d](%d) is not in init stage!\n",
			desc_rd_idx,
			ul_ref_idx[desc_rd_idx]);
	}
	ul_ref_idx[desc_rd_idx] = done;

	p_mr_dev_q_ul->done_ptr = _noa_dma_get_next_idx(
		p_mr_dev_q_ul->done_ptr, p_mr_dev_q_ul->num_desc);

	*p_mr_dev_q_ul->fore_ptr = p_mr_dev_q_ul->done_ptr;

	// doorbell to modem device
	if (p_adaptor->mr2cp_irq)
		p_adaptor->mr2cp_irq(p_adaptor->p_mld);

	return ret;
}

/* Doorbell ISR from noa_simulator NOA_PORT_MODEM_FW output ring */
static irqreturn_t noa_md_fw_isr_db(int id, void *data)
{
	struct ncp_md_adaptor *p_md_adaptor =
		(struct ncp_md_adaptor *)data;
	struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_FW);
	// md fw device driver
	// input: noa_simulator NOA_PORT_MODEM_FW output ring
	// output: device queue (need init data to set device interface)
	// ncp_md_adaptor members:
	// 1. ring pointer
	// 2. init data
	// 3. isr_db function pointer
	port->ints = 0;
	if (p_md_adaptor->mr2md_handling_task) {
		// src 0: from APC->NOA->NOA MD FW
		p_md_adaptor->mr2md_handling_task(0, p_md_adaptor);
	}

	return IRQ_HANDLED;
}

static enum hrtimer_restart timer_tx_recycle_func(struct hrtimer *timer)
{
	struct ncp_md_adaptor *p_adaptor =
		container_of(timer, struct ncp_md_adaptor, timer_tx_recycle);
	struct noa_ring *p_noa2md_ring
		= p_adaptor->p_noa2md_ring;
	struct mr_lassen_tx_desc *p_noa_desc =
		(struct mr_lassen_tx_desc *)(unsigned long)p_adaptor->p_noa2md_ring->base;
	struct noa_desc *p_desc = NULL;
	struct mr_pktproc_queue_ul *p_mr_dev_q_ul = NULL;
	struct mr_pktproc_adaptor_ul *p_mr_ppa_ul =
		p_adaptor->p_mr_ppa_ul;
	bool need_resch = false;
	/*
	struct mr_pktproc_desc_ul *p_dev_desc = NULL;
	u32 count;
	u32 last_ptr;
	u32 fore_ptr;
	u32 prev_ptr;
	*/

	p_mr_dev_q_ul =
		p_mr_ppa_ul->dev_q[1]; // always use low prio queue

	need_resch = false;
	while (
		p_noa2md_ring->read !=
		p_adaptor->noa2md_ring_done_ptr) {
		// recycle tx buffer or not
		p_desc = &(p_noa_desc[p_noa2md_ring->read].basic);
		/* Debug Info
		ncp_md_info("read(%d), mode(%d), rear ptr(%d), fore ptr(%d),"
					"done ptr(%d), ref idx(%d)\n",
			p_noa2md_ring->read,
			p_desc->mode,
			*p_mr_dev_q_ul->rear_ptr,
			*p_mr_dev_q_ul->fore_ptr,
			p_adaptor->noa2md_ring_done_ptr,
			ul_ref_idx[p_noa2md_ring->read]);
		*/
		switch (p_desc->mode) {
		case NOAD_MODE_FEEDBACK:
			// rx
			// error checking
			if (unlikely(INVALID_REF_IDX_VALUE !=
				ul_ref_idx[p_noa2md_ring->read])) {
				panic("ul_ref_idx[%d](%d) is not INVALID_REF_IDX_VALUE!\n",
				p_noa2md_ring->read,
				ul_ref_idx[p_noa2md_ring->read]);
			}

			md_fw_noa2md_read_increment(p_adaptor);
			break;
		case NOAD_MODE_DATA:
			if (*p_mr_dev_q_ul->rear_ptr ==
				ul_ref_idx[p_noa2md_ring->read]) {
				need_resch = true;
			} else {
				ul_ref_idx[p_noa2md_ring->read] = INVALID_REF_IDX_VALUE;
				md_fw_noa2md_read_increment(p_adaptor);
			}
			break;
		default:
			// Error, unknown Mode
			pr_err("Unknown Mode(%d)\n", p_noa_desc[p_noa2md_ring->read].basic.mode);
			break;
		}

		if (need_resch) {
			break;
		}
	}

	if (need_resch) {
		// trigger the next timeout
		struct hrtimer *p_timer = NULL;
		p_timer = &p_adaptor->timer_tx_recycle;
		if (!hrtimer_is_queued(p_timer)) {
			ktime_t ktime = ktime_set(0, p_adaptor->tx_recycle_timeout_ns);
			hrtimer_start(p_timer, ktime, HRTIMER_MODE_REL);
		}
	}

	if ((u16)smp_load_acquire(&p_noa2md_ring->read) !=
		(u16)smp_load_acquire(&p_noa2md_ring->write)) {
		struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_FW);
		port->ints = 1;
		noa_sim_trig_tx();
	}
	// update Tx device fore_ptr
	/*
	count = _noa_dma_get_read_count(
		*p_mr_dev_q_ul->fore_ptr,
		p_mr_dev_q_ul->done_ptr,
		p_mr_dev_q_ul->num_desc
		);

	if (count > 0) {
		last_ptr = *p_mr_dev_q_ul->fore_ptr;
		fore_ptr = last_ptr + count;
		fore_ptr -= ((fore_ptr > p_mr_dev_q_ul->num_desc) ? p_mr_dev_q_ul->num_desc : 0);
		// set last
		prev_ptr = (0 == fore_ptr) ? (p_mr_dev_q_ul->num_desc - 1) : (fore_ptr - 1);
		p_dev_desc = &p_mr_dev_q_ul->desc_ul[prev_ptr];
		p_dev_desc->last_desc = 1;
		*p_mr_dev_q_ul->fore_ptr = fore_ptr;
		smp_mb();

		// doorbell to modem device
		if (p_adaptor->mr2cp_irq)
			p_adaptor->mr2cp_irq(p_adaptor->p_mld);
	}
	*/

	return HRTIMER_NORESTART;
}

struct ncp_md_adaptor *ncp_md_adaptor_alloc(
	struct noa_pktproc_adaptor_dl *p_noa_ppa_dl,
	struct mr_pktproc_adaptor_ul *p_mr_ppa_ul,
	struct noa_ring *p_noa2md_ring,
	struct noa_ring *p_md2noa_ring
)
{
	struct ncp_md_adaptor *p_adaptor =
		&md_adaptor;
	struct noa_port *port = noa_sim_get_port(NOA_PORT_MODEM_FW);

	p_noa_ppa_dl->p_md_adaptor = p_adaptor;
	p_mr_ppa_ul->p_md_adaptor = p_adaptor;
	p_adaptor->p_noa_ppa_dl = p_noa_ppa_dl;
	p_adaptor->p_mr_ppa_ul = p_mr_ppa_ul;
	p_adaptor->p_noa2md_ring = p_noa2md_ring;
	p_adaptor->p_md2noa_ring = p_md2noa_ring;
	p_adaptor->dev_q_rr_start_idx = 0;

	p_adaptor->noa_port_md_fw_doorbell_addr =
		(unsigned long)&port->doorbell;

	hrtimer_init(&p_adaptor->timer_tx_recycle, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	p_adaptor->timer_tx_recycle.function = timer_tx_recycle_func;
	p_adaptor->tx_recycle_timeout_ns = 1 * NSEC_PER_MSEC; // 1ms

	p_adaptor->noa2md_ring_done_ptr = 0x0;

	// irq number of NOA_PORT_MODEM_FW is 5
	// defined in nep/nep.c
	noa_interrupt_register(5, noa_md_fw_isr_db, p_adaptor);

	pr_info("[noa_modem] %s DONE!\n", __func__);

	return p_adaptor;
}

// initialize device q, NOA MD FW (NCP) to Modem device
int ncp_md_fw_create_dev_q_dl(struct noa_pktproc_adaptor_dl *noa_ppa_dl, u32 buff_size_by_q)
{
	struct noa_pktproc_queue_dl *q = NULL;
	u32 i;
	int ret = 0;

	for (i = 0; i < noa_ppa_dl->num_queue; i++) {
		noa_ppa_dl->dev_q[i] = kzalloc(sizeof(struct noa_pktproc_queue_dl), GFP_ATOMIC);
		if (noa_ppa_dl->dev_q[i] == NULL) {
			//mif_err_limited("kzalloc() error %d\n", i);
			ret = -ENOMEM;
			goto create_error;
		}
		q = noa_ppa_dl->dev_q[i];
		q->noa_ppa_dl = noa_ppa_dl;

		atomic_set(&q->active, 0);

		q->info_v2 = (struct noa_pktproc_info_v2 *)noa_ppa_dl->info_vbase;
		q->info_v2->num_queues = noa_ppa_dl->num_queue;
		//q->info_v2->desc_mode = noa_ppa_dl->desc_mode;
		//q->info_v2->irq_mode = noa_ppa_dl->use_exclusive_irq;
		q->info_v2->desc_mode = 1; // SKTBUF MODE
		q->info_v2->irq_mode = 1;
		q->info_v2->max_packet_size = noa_ppa_dl->max_packet_size;
		q->q_info_ptr = &q->info_v2->q_info[i];

		q->q_buff_pbase = noa_ppa_dl->buff_pbase + (i * buff_size_by_q);
		q->q_buff_vbase = noa_ppa_dl->buff_vbase + (i * buff_size_by_q);
		q->cp_buff_pbase = noa_ppa_dl->cp_base + noa_ppa_dl->buff_rgn_offset +
				(i * buff_size_by_q);
		q->q_buff_size = buff_size_by_q;
		q->num_desc = buff_size_by_q / noa_ppa_dl->true_packet_size;
		//TODO
		//q->alloc_rx_buf = pktproc_fill_data_addr_without_bm;
		//q->clear_data_addr = pktproc_clear_data_addr_without_bm;

		/*
		if (mld->pktproc_use_36bit_addr)
			q->q_info_ptr->cp_buff_pbase = q->cp_buff_pbase >> 4;
		else
		*/
		q->q_info_ptr->cp_buff_pbase = q->cp_buff_pbase;

		q->q_info_ptr->num_desc = q->num_desc;

		q->desc_sktbuf = noa_ppa_dl->desc_vbase +
			(i * sizeof(struct noa_pktproc_desc_sktbuf) *
			q->num_desc);
		q->cp_desc_pbase = noa_ppa_dl->cp_base + noa_ppa_dl->desc_rgn_offset +
			(i * sizeof(struct noa_pktproc_desc_sktbuf) *
			q->num_desc);
		/*
		if (mld->pktproc_use_36bit_addr)
			q->q_info_ptr->cp_desc_pbase = q->cp_desc_pbase >> 4;
		else
		*/
		q->q_info_ptr->cp_desc_pbase = q->cp_desc_pbase;

		q->q_info_ptr->num_desc = q->num_desc;

		q->desc_sktbuf = noa_ppa_dl->desc_vbase +
			(i * sizeof(struct noa_pktproc_desc_sktbuf) *
			q->num_desc);
		q->cp_desc_pbase = noa_ppa_dl->cp_base + noa_ppa_dl->desc_rgn_offset +
			(i * sizeof(struct noa_pktproc_desc_sktbuf) *
			q->num_desc);

		/*
		if (mld->pktproc_use_36bit_addr)
				q->q_info_ptr->cp_desc_pbase = q->cp_desc_pbase >> 4;
		else
		*/
			q->q_info_ptr->cp_desc_pbase = q->cp_desc_pbase;

		//TODO
		// q->get_packet = pktproc_get_pkt_from_sktbuf_mode;
		q->irq_handler = md_dev_irq_handler;
		// q->update_fore_ptr = pktproc_update_fore_ptr;

		ncp_md_info("init q->lock\n");
		spin_lock_init(&q->lock);

		// q->clean_rx_ring = pktproc_clean_rx_ring;

		q->q_idx = i;
		// q->mld = mld;

		init_dummy_netdev(&q->netdev);
		//netif_napi_add(&q->netdev, &q->napi, noa_pktproc_poll, NAPI_POLL_WEIGHT);
		//napi_enable(&q->napi); // kernel panic
		q->napi_ptr = &q->napi;
		ncp_md_info("NAPI DONE\n");

		tasklet_init(&q->q_task, md_fw_rx_pkt_task, (unsigned long)q);

		// q->enable_irq = pktproc_enable_irq;
		// q->disable_irq = pktproc_disable_irq;

		q->q_info_ptr->fore_ptr = 0;
		q->q_info_ptr->rear_ptr = 0;

		q->fore_ptr = &q->q_info_ptr->fore_ptr;
		q->rear_ptr = &q->q_info_ptr->rear_ptr;
		q->done_ptr = *q->rear_ptr;
	} // end for

create_error:

	return ret;
}

int ncp_md_fw_create_dev_q_ul(struct mr_pktproc_adaptor_ul *mr_ppa_ul, u32 buff_size_by_q)
{
	struct mr_pktproc_queue_ul *q = NULL;
	struct mr_pktproc_info_ul *ul_info;
	u32 i;
	int ret = 0;
	u32 last_q_desc_offset;

	ul_info = (struct mr_pktproc_info_ul *)mr_ppa_ul->info_vbase;
	ul_info->num_queues = mr_ppa_ul->num_queue;

	/* Create queue */
	last_q_desc_offset = 0;
	for (i = 0; i < mr_ppa_ul->num_queue; i++) {
		mr_ppa_ul->dev_q[i] = kzalloc(sizeof(struct mr_pktproc_queue_ul),
				GFP_ATOMIC);
		if (mr_ppa_ul->dev_q[i] == NULL) {
			//mif_err_limited("kzalloc() error %d\n", i);
			ret = -ENOMEM;
			goto create_error;
		}
		q = mr_ppa_ul->dev_q[i];

		atomic_set(&q->active, 0);

		/* Info region */
		q->ul_info = ul_info;
		q->q_info = &q->ul_info->q_info[i];

		// Need to use vbase to write to NOA FW output ring
		// Simulation only
		q->q_buff_vbase = mr_ppa_ul->buff_vbase + (i * buff_size_by_q);
		q->cp_buff_pbase = mr_ppa_ul->cp_base +
			mr_ppa_ul->buff_rgn_offset + (i * buff_size_by_q);

		/* init by APC
		if (mld->pktproc_use_36bit_addr)
			q->q_info->cp_buff_pbase = q->cp_buff_pbase >> 4;
		else
			q->q_info->cp_buff_pbase = q->cp_buff_pbase;
		*/

		/*
		if (mr_ppa_ul->num_queue > 1 &&
			i == PKTPROC_UL_HIPRIO && mr_ppa_ul->hiprio_ack_only) {
			struct link_device *ld = &mld->link_dev;

			ld->hiprio_ack_only = true;
			q->max_packet_size = HIPRIO_MAX_PACKET_SIZE;
		} else {
			q->max_packet_size = mr_ppa_ul->default_max_packet_size;
		}
		*/
		// Don't use q_idx = 0
		q->max_packet_size = mr_ppa_ul->q_max_packet_size[i];

		q->q_buff_size = buff_size_by_q;
		q->num_desc = buff_size_by_q / q->max_packet_size;
		q->q_info->num_desc = q->num_desc;

		q->desc_ul = mr_ppa_ul->desc_vbase + last_q_desc_offset;
		q->cp_desc_pbase = mr_ppa_ul->cp_base +
			mr_ppa_ul->desc_rgn_offset + last_q_desc_offset;
		/* init by APC
		if (mld->pktproc_use_36bit_addr)
			q->q_info->cp_desc_pbase = q->cp_desc_pbase >> 4;
		else
			q->q_info->cp_desc_pbase = q->cp_desc_pbase;
		*/
		q->desc_size = sizeof(struct mr_pktproc_desc_ul) * q->num_desc;
		q->buff_addr_cp = mr_ppa_ul->cp_base + mr_ppa_ul->buff_rgn_offset +
			(i * buff_size_by_q);
		/*
		q->send_packet = pktproc_send_pkt_to_cp;
		q->update_fore_ptr = pktproc_ul_update_fore_ptr;
		*/
		ncp_md_info("UL Q ID(%d), num desc(%d), desc size: 0x%08x\n",
			i, q->num_desc, q->desc_size);


		if ((last_q_desc_offset + q->desc_size) > mr_ppa_ul->desc_rgn_size) {
			panic("Descriptor overflow. 0x%08x + 0x%08x > 0x%08lx\n",
				last_q_desc_offset, q->desc_size, mr_ppa_ul->desc_rgn_size);
			goto create_error;
		}

		spin_lock_init(&q->lock);

		q->q_idx = i;
		//q->mld = mld;
		q->ppa_ul = mr_ppa_ul;

		q->q_info->fore_ptr = 0;
		q->q_info->rear_ptr = 0;

		q->fore_ptr = &q->q_info->fore_ptr;
		q->rear_ptr = &q->q_info->rear_ptr;
		q->done_ptr = *q->fore_ptr;

		last_q_desc_offset += q->desc_size;
	}

create_error:

	return ret;
}

struct ncp_md_adaptor *ncp_md_init(
	struct noa_pktproc_adaptor_dl *noa_ppa_dl, u32 buff_size_by_q,
	struct mr_pktproc_adaptor_ul *mr_ppa_ul, u32 ul_buff_size_by_q)
{
	struct ncp_md_adaptor *p_md_adaptor = NULL;
	ncp_md_fw_create_dev_q_dl(noa_ppa_dl, buff_size_by_q);
	ncp_md_fw_create_dev_q_ul(mr_ppa_ul, ul_buff_size_by_q);
	p_md_adaptor = ncp_md_adaptor_alloc(
		noa_ppa_dl, mr_ppa_ul,
		NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowHostToDevice,
				     kNoaModemRingTxData, kNoaRingNepOutput),
		NoaRingSharedInfoGet(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
				     kNoaModemRingRxData, kNoaRingNepInput));
	p_md_adaptor->mr2md_handling_task = mr2md_handling_task;
	md_fw_tx_buffer_pool_init(p_md_adaptor);
	md_fw_out_fifo_init(mr_ppa_ul);
	md_fw_in_fifo_init();
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
							   kNoaNetworkFlowDeviceToHost,
							   kNoaModemRingRxData),
				      kNoaRingNepInput);
	nep_ring_service_request_send(NEP_CMD_RING_ACTIVE, true,
				      NoaRingPathIdConvert(kNoaNetworkInterfaceModem,
							   kNoaNetworkFlowHostToDevice,
							   kNoaModemRingTxData),
				      kNoaRingNepOutput);

	return p_md_adaptor;
}
EXPORT_SYMBOL_GPL(ncp_md_init);

u16 get_ul_ref_idx(u16 rd_idx)
{
	return ul_ref_idx[rd_idx];
}
EXPORT_SYMBOL_GPL(get_ul_ref_idx);

irq_handler_t ncp_md_fw_get_dev_irq_handler(u32 q_idx)
{
	struct ncp_md_adaptor *p_adaptor =
		&md_adaptor;
	struct noa_pktproc_adaptor_dl *p_dev_ppa_dl =
		p_adaptor->p_noa_ppa_dl;
	return p_dev_ppa_dl->dev_q[q_idx]->irq_handler;
}
EXPORT_SYMBOL_GPL(ncp_md_fw_get_dev_irq_handler);

int *ncp_md_fw_get_irq_ref(u32 q_idx)
{
	struct ncp_md_adaptor *p_adaptor =
		&md_adaptor;
	struct noa_pktproc_adaptor_dl *p_dev_ppa_dl =
		p_adaptor->p_noa_ppa_dl;
	return &p_dev_ppa_dl->dev_q[q_idx]->irq;
}
EXPORT_SYMBOL_GPL(ncp_md_fw_get_irq_ref);

void *ncp_md_fw_get_irq_context(u32 q_idx)
{
	struct ncp_md_adaptor *p_adaptor =
		&md_adaptor;
	struct noa_pktproc_adaptor_dl *p_dev_ppa_dl =
		p_adaptor->p_noa_ppa_dl;
	return (void *)p_dev_ppa_dl->dev_q[q_idx];
}
EXPORT_SYMBOL_GPL(ncp_md_fw_get_irq_context);

static void md_fw_rx_pkt_task(unsigned long data)
{
	struct noa_pktproc_queue_dl *p_dev_q =
		(struct noa_pktproc_queue_dl *)data;
	struct noa_pktproc_desc_sktbuf *p_dev_desc = NULL;
	struct noa_pktproc_adaptor_dl *p_pkt_adaptor =
		p_dev_q->noa_ppa_dl;
	struct ncp_md_adaptor *p_md_adaptor =
		p_pkt_adaptor->p_md_adaptor;
	struct noa_ring *p_md2noa_ring
		= p_md_adaptor->p_md2noa_ring;
	struct mr_lassen_rx_desc *p_noa_desc =
		(struct mr_lassen_rx_desc *)(unsigned long)p_md_adaptor->p_md2noa_ring->base;
	struct mr_lassen_ext_rxd *p_rxd_ext = NULL;
	u32 rear = 0x0;
	u32 dl_token_id = 0x0;
	u8 *p_dst = NULL;

	/*
	ncp_md_info("%s, q_idx(%d), rear(%d), fore(%d), done(%d)\n",
		__func__,
		p_dev_q->q_idx,
		*p_dev_q->rear_ptr,
		*p_dev_q->fore_ptr,
		p_dev_q->done_ptr);
	*/
	// done_ptr -> rear_ptr

	rear = *p_dev_q->rear_ptr;
	p_dev_desc = p_dev_q->desc_sktbuf;

	while (p_dev_q->done_ptr != rear) {
		/*
		ncp_md_info("cp data paddr 0x%08x, control: 0x%08x, length(%d)\n",
			p_dev_desc[p_dev_q->done_ptr].cp_data_paddr,
			p_dev_desc[p_dev_q->done_ptr].control,
			p_dev_desc[p_dev_q->done_ptr].length);
		*/

		if (_noa_dma_get_next_idx(p_md2noa_ring->write, p_md2noa_ring->ctrl) ==
		    (u16)smp_load_acquire(&p_md2noa_ring->read)) {
			panic("TODO\n");
		}

		// copy the whole dev desc to head room
		dl_token_id =
			(p_dev_desc[p_dev_q->done_ptr].cp_data_paddr -
			p_pkt_adaptor->skb_padding_size -
			p_dev_q->cp_buff_pbase) /
			p_pkt_adaptor->true_packet_size;
		p_dst = p_pkt_adaptor->pf_buf[dl_token_id];

		/*
		ncp_md_info("cp_buff_pbase: 0x%08x, skb padding size(%d),"
				"true pkt size(%d)\n",
			p_dev_q->cp_buff_pbase,
			p_pkt_adaptor->skb_padding_size,
			p_pkt_adaptor->true_packet_size);
		ncp_md_info("in FW, noa ppa dl(%p), pf_buf(%p), tkid(%d)"
				", dst(%p)\n",
			p_pkt_adaptor,
			p_pkt_adaptor->pf_buf,
			dl_token_id,
			p_dst);
		*/

		p_rxd_ext =
			(struct mr_lassen_ext_rxd *)p_noa_desc[p_md2noa_ring->write].basic.ext_data;
		p_rxd_ext->channel_id =
			p_dev_desc[p_dev_q->done_ptr].channel_id;
		p_rxd_ext->status =
			p_dev_desc[p_dev_q->done_ptr].status;
		// End of copy the whole dev desc to head room

		p_noa_desc[p_md2noa_ring->write].basic.dp_low =
			p_dev_desc[p_dev_q->done_ptr].cp_data_paddr;
		p_noa_desc[p_md2noa_ring->write].basic.dl =
			p_dev_desc[p_dev_q->done_ptr].length + p_pkt_adaptor->skb_padding_size;

		p_noa_desc[p_md2noa_ring->write].basic.dst = NoaRingPathIdConvert(
			kNoaNetworkInterfaceNetengine, kNoaNetengineTunnel, kNoaNetengineRingData);

		p_noa_desc[p_md2noa_ring->write].basic.ddone = 0;
		p_noa_desc[p_md2noa_ring->write].basic.src =
			NoaRingPathIdConvert(kNoaNetworkInterfaceModem, kNoaNetworkFlowDeviceToHost,
					     kNoaModemRingRxData);

		p_noa_desc[p_md2noa_ring->write].basic.reason = FWD_REASON_NETENGINE;
		// TODO(b/336924909): For now, set cp bit to 0 for NetEngine able to alter the
		// packet. According to b/336924909#comment3, fix it by removing the copy mechanism
		// from the ring service.
		p_noa_desc[p_md2noa_ring->write].basic.cp = NOAD_NO_COPY;
		p_noa_desc[p_md2noa_ring->write].basic.tkid = dl_token_id;
		p_noa_desc[p_md2noa_ring->write].basic.fk = NOAD_FEEDBACK_ENABLE;
		p_noa_desc[p_md2noa_ring->write].basic.desc_type = NOA_DESC_MODEM_LASSEN;

		// Simulation Only
		// Give Net Engine Virtual Address and head offset
		p_noa_desc[p_md2noa_ring->write].basic.dv = (unsigned long)p_dst;
		p_noa_desc[p_md2noa_ring->write].basic.head_offset =
			p_pkt_adaptor->skb_padding_size;
		// end of Simulation Only

		smp_store_release(&p_md2noa_ring->write,
				  (u32)_noa_dma_get_next_idx(p_md2noa_ring->write,
							     p_md2noa_ring->ctrl));

		p_dev_q->done_ptr = _noa_dma_get_next_idx(
			p_dev_q->done_ptr,
			p_dev_q->num_desc);
	}

	barrier();

	// trigger NOA input ring
	writel(1, (void *)p_md_adaptor->noa_port_md_fw_doorbell_addr);
	noa_sim_trig_rx();
}

