/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NCP WiFi Firmware
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __NCP_WLAN_FW_H__
#define __NCP_WLAN_FW_H__

#include <common/core.h>
#include <common/wlan/noa_wlan.h>
#include <common/wlan/noa_wlan_qca.h>
#include <nep/nep.h>

struct noa_wlan_fw;

#define PCIE_DMA_MASK 36
#define WLAN_FW_INPUT_SIZE 512
#define WLAN_FW_OUTPUT_SIZE 512

enum {
	RX_TYPE_DATA,
	RX_TYPE_TXCPL,
	RX_TYPE_MAX,
};

enum {
	WLAN_FW_FLAG_INIT,
	WLAN_FW_FLAG_START,
	WLAN_FW_FLAG_IRQ,
};
struct noa_bm_map {
	u16 pktid;
	u16 len;
	unsigned long pa;
	unsigned long va;
	unsigned long desc;
	bool valid;
};

struct noa_input_act {
	struct noa_wlan_fw *fw;
	u32 dst;
	u32 head_offset;
	u32 data_len;
	struct noa_bm_map *buf;
};

#define STA_INFO_COMM  28
#define STA_INFO_PRIVATE 8
#define STA_INFO_CMD 4
struct wlan_sta_info {
	u32 oif;
	u16 bss_idx;
	u16 qos_txq_map[PRIORITY_CLASS];
	u8 addr[MAC_ADDR_LEN];
	u8 encrypt_type : 4,
	encap_type : 2,
	lmac_id : 2;
	u8 bmid;
	u8 reserved1[2];
	u32 search_idx : 20,
	search_type : 2,
	dscp_tid_map_id : 6,
	addry_en : 1,
	addrx_en : 1,
	reserved2 : 2;
	u8 enable;
	u8 reserved3[3];
} __attribute__((packed, aligned(4)));
static_assert((STA_INFO_COMM + STA_INFO_PRIVATE + STA_INFO_CMD) == sizeof(struct wlan_sta_info));

struct noa_wlan_info {
	struct noa_hw_ring rx_rings[WLAN_RX_RING_MAX];
	struct noa_hw_ring tx_rings[WLAN_TX_RING_MAX];
	struct noa_hw_ring tx_cpl_rings[WLAN_TXCPL_MAX];
	struct noa_hw_ring rx_post_rings[WLAN_RX_POST_MAX];
	unsigned long reg_addr;
	unsigned long share_addr;
	u32 reg_size;
	u32 share_size;
	u32 tx_pkt_max;
	u32 tx_bm_sz;
	u32 rx_pkt_max;
	u16 tx_ring_max;
	u16 rx_ring_max;
	u16 tx_cpl_ring_max;
	u16 rx_post_ring_max;
	u32 ints_addr;
	u32 intm_addr;
	u32 irqs[MAX_IRQ_NUM];
	u32 rx_buf_sz;
	u32 chip_id;
	u8 irq_nums;
	struct wlan_sta_info sta_info[MAX_STA_SUPPORT];
};
struct noa_wlan_stat {
	unsigned long rx;
	unsigned long rx_err;
	unsigned long tx;
	unsigned long tx_err;
	unsigned long tx_cpl;
	unsigned long tx_cpl_err;
	unsigned long rx_free;
	unsigned long rx_free_err;
	unsigned long feedback;
	unsigned long rxbm_sync;
	unsigned long rxbm_sync_err;
};

struct ncp_wlan_chip_ops {
	int (*tx)(struct noa_wlan_fw *fw, struct noa_desc *desc,
	unsigned long long *flags);
	int (*request_irqs)(struct noa_wlan_fw *fw);
	void (*release_irqs)(struct noa_wlan_fw *fw);
	int (*rx)(struct noa_wlan_fw *fw,
		struct noa_hw_ring *ring, void *wlan_desc, struct noa_input_act *input_act);
	int (*rx_post)(struct noa_wlan_fw *fw, u32 count, struct noa_bm_buf *bufs);
	int (*tx_cpl)(struct noa_wlan_fw *fw, struct noa_hw_ring *ring, void *desc, u32 *pktid);
};

struct nep_ring {
	struct noa_ring_wrapper ring;
	/**
	 * TODO: This lock is only for safely releasing resource,
	 * we can use flag with atomic operation to do the same thing
	 * and remove this lock from it.
	 */
	spinlock_t lock;
	void *desc;
	dma_addr_t desc_dma;
};

struct noa_wlan_fw {
	u32 type;
	struct device dev;
	struct device *client_dev;
	struct noa_wlan_info wlan_info;
	struct tasklet_struct wlan_tx_task;
	struct nep_ring nep_tx_ring;
	struct nep_ring nep_rx_ring;
	unsigned long noa_hw_ints_addr;
	unsigned long noa_hw_doorbell_addr;
	struct noa_bm_map *rxbm;
	// TODO: migrate to ring service buffer pool?
	struct noa_bm_map *txbm;
	struct {
		noa_ring_producer ring;
		char *ring_buf;
	} tx_buffer_pool;
	u8 *tx_pktid_checker;
	u8 *rx_pktid_checker;
	struct noa_wlan_stat stat;
	spinlock_t rx_lock;
	void __iomem *reg_base;
	void __iomem *share_base;
	bool rxdbg;
	bool txdbg;
	bool disable_cp;
	bool rxbm_tx_check;
	u32 flags;
	struct ncp_wlan_chip_ops *chip_ops;
};
#endif /* __NCP_WLAN_FW_H__ */
