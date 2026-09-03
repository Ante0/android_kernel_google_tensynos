/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD RX Driver
 *
 * Copyright 2024 Google LLC.
 */
#ifndef __NOA_MD_RX_DATA_H__
#define __NOA_MD_RX_DATA_H__

#include <linux/limits.h>  /* For USHRT_MAX */

#include "common/md/mediatek/noa_md_shmem_layout.h"
#include "noa_md_data_path_ctrl.h"  // For noa_dpath_client
#include "ring.h"

#define NOA_MD_RX_INVALID_BAT_INDEX USHRT_MAX
#define NOA_MD_RX_INVALID_TKID ~(0x0)

struct noa_md_rx_tkid_info {
	unsigned short *rx_tkid;
	spinlock_t rx_tkid_lock;
	unsigned short *free_pool;
	unsigned short rx_tkid_free_fore;
	unsigned short rx_tkid_free_rear;
};

struct noa_md_rx_buffer_desc {
	u32 num_desc;
	void *desc_base;
	dma_addr_t dma_addr;
	uint64_t desc_dpa_base;
	struct noa_ring_wrapper ring;
	// struct noa_rx_skb *skb; // TODO: Need review to remove
	spinlock_t lock;
};

struct noa_md_rx {
	struct noa_md_rx_buffer_desc *rx_buffer_desc;
	struct noa_md_rx_tkid_info *normal_tkid_infos;
	struct noa_md_rx_tkid_info *frag_tkid_infos;
	struct workqueue_struct *isr_wq;
	struct work_struct isr_work;
	struct noa_dpath_client *dpath_client;
	/* Dedicated DMA mapper for the RX path to avoid lock contention with TX */
	struct noa_md_dma_mapper mapper;
	unsigned long noa_hw_rx_ints_addr;
	spinlock_t lock;
};

void noa_md_rx_isr_work(struct work_struct *work);
irqreturn_t noa_md_rx_isr(int id, void *data);
int noa_md_rx_tkid_info_setup(void);
void noa_md_rx_tkid_info_release(void);
int noa_md_rx_ring_setup(struct noa_md_dev *p_md_dev);
void noa_md_rx_ring_release(struct noa_md_dev *p_md_dev);
int noa_md_rx_get_bat_write_idx(u32 q_num);
int noa_md_rx_get_bat_read_idx(u32 q_num);
int noa_md_rx_get_frg_read_idx(u32 q_num);
int noa_md_rx_get_ring_write_idx(u32 q_num);
int noa_md_rx_set_ring_read_idx(u32 q_num, u32 count);
int noa_md_rx_add_tkid_to_free_pool(u32 q_num, u32 rx_tkid);
int noa_md_rx_write_bat_refill_ring(u32 q_num, u32 count);
int noa_md_rx_write_frag_refill_ring(u32 q_num, u32 count);
void noa_md_rx_unmap_modem_dpa_bat(int bat_id, int ring_type,
	unsigned short mapped_rx_tkid);
int noa_md_rx_queues_remap_setup(struct noa_md_dev *p_md_dev);
void noa_md_rx_queues_remap_release(struct noa_md_dev *p_md_dev);
bool noa_md_rx_skip_napi_disable_during_dynamic_switch(struct noa_md_dev *p_md_dev);

/* Setup & Release functions */
/**
 * noa_md_rx_setup() - Initializes all software resources for the RX path.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int noa_md_rx_setup(void);

/**
 * noa_md_rx_release() - Releases all software resources for the RX path.
 *
 * Tears down workqueues, unregisters clients, and frees all RX resources.
 */
void noa_md_rx_release(void);
#endif /* __NOA_MD_RX_DATA_H__ */
