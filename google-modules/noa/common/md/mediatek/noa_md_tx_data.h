/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD TX Driver
 *
 * Copyright 2025 Google LLC.
 */
#ifndef __NOA_MD_TX_DATA_H__
#define __NOA_MD_TX_DATA_H__

#include "noa_md_data_path_ctrl.h"  // For struct noa_dpath_client

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "t900/noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_data_plane.h"  // For mtk_tx_pkt_info
#endif

#define NOA_MD_NUM_TX_QUEUES 5
#define NOA_MD_TXQ_SIZE 1024
#define NOA_MD_TX_RING_SIZE 1024

struct noa_md_tx_work_data {
	struct delayed_work work;
	struct sk_buff *skb;
	struct mtk_tx_pkt_info pkt_info;
};

struct noa_md_tx_done_work_data {
	struct delayed_work work;
	unsigned int count;
	struct sk_buff **skb_tx_queue;
	dma_addr_t *dma_addr_queue;
};

struct noa_md_tx_buffer_desc {
	atomic_t to_submit_cnt;
	u32 virtual_write_idx;
	u32 num_desc;
	void *desc_base;
	dma_addr_t skb_dma_addr;
	struct noa_ring_wrapper ring;
	// struct noa_tx_skb *sw_tx_skb_record; // TODO: Need review to remove
	// spinlock_t lock; // TODO: Need review to remove
};

struct noa_md_tx_queue {
	struct sk_buff *skb_tx_queue[NOA_MD_TX_RING_SIZE];
	dma_addr_t skb_tx_dma_queue[NOA_MD_TX_RING_SIZE];
	u32 skb_tx_queue_head;
	u32 skb_tx_queue_tail;
	spinlock_t txq_lock;
};

/**
 * struct noa_md_tx - Manages the NOA modem transmit (TX) data path.
 * @tx_buffer_desc: Descriptor for the main TX software ring buffer.
 * @tx_queues:      Array of software queues for staging SKBs before DMA.
 * @noa_md_tx_workqueue: Workqueue for handling outgoing data packets.
 * @noa_md_tx_done_workqueue: Workqueue for handling TX completion tasks.
 * @dpath_client:   Client handle for data path switching notifications.
 * @noa_hw_tx_doorbell_addr: Hardware doorbell address for triggering NOA TX.
 * @vpn_queues:     Array of dedicated queues for VPN TX offload.
 * @num_vpn_queues: Number of allocated VPN queues.
 */
struct noa_md_tx {
	struct noa_md_tx_buffer_desc *tx_buffer_desc;
	struct noa_md_tx_queue tx_queues[NOA_MD_NUM_TX_QUEUES];
	struct workqueue_struct *noa_md_tx_workqueue;
	struct workqueue_struct *noa_md_tx_done_workqueue;
	struct noa_dpath_client *dpath_client;
	unsigned long noa_hw_tx_doorbell_addr;
	struct noa_vpn_tx_queue *vpn_queues;
	/* Dedicated DMA mapper for the TX path to avoid lock contention with RX */
	struct noa_md_dma_mapper mapper;
	unsigned int num_vpn_queues;
};

ssize_t noa_md_tx_input_desc_write(
	void *output_buf, size_t buf_len, const void *input_data, size_t data_len);
void noa_md_tx_trigger_doorbell(struct noa_ring_wrapper *ring);
int noa_md_tx_data_done(int count, int vq_id);
int noa_md_tx_wwan_data(void *dcb, struct sk_buff *skb);
void noa_md_tx_inc(struct sk_buff *skb);
void noa_md_tx_drop_inc(struct sk_buff *skb);
int noa_md_tx_update_ring(
	struct sk_buff *skb,
	dma_addr_t skb_dma_addr,
	struct mtk_tx_pkt_info pkt_info,
	struct noa_tx_ipsec_metadata *metadata);

/**
 * noa_md_tx_update_tkid_queues() - Update TKID queues with DMA address.
 * @dma_addr: DMA address for the drb.
 * @txq_id: TX queue ID.
 */
void noa_md_tx_update_tkid_queues(dma_addr_t dma_addr, int txq_id);

/* Setup & Release functions */
int noa_md_tx_ring_setup(struct noa_md_dev *p_md_dev);
void noa_md_tx_ring_release(struct noa_md_dev *p_md_dev);

/**
 * noa_md_tx_setup() - Initializes all software resources for the TX path.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_tx_setup(void);

/**
 * noa_md_tx_release() - Releases all software resources for the TX path.
 *
 * Tears down workqueues, unregisters clients, and frees allocated memory.
 */
void noa_md_tx_release(void);

/* Dump drb write/read indices of modem rings */
void noa_md_tx_dpmaif_dump_drb_info(void);
#endif /* __NOA_MD_TX_DATA_H__ */
