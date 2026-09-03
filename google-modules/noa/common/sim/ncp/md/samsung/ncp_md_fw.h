/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NCP MD FW
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>
 */
#ifndef __NCP_MD_FW_H__
#define __NCP_MD_FW_H__

#include <common/noa_hw_ring.h>
#include <common/md/samsung/mr_md.h>
#include <linux/hrtimer.h>
#include <md/samsung/noa_md_custom.h>
#include <nep/nep.h>

#define ncp_md_info(fmt, ...) \
	pr_info("ncp_md: " "%s " pr_fmt(fmt), __func__, ##__VA_ARGS__)

struct ncp_md_adaptor;

/* Queue info */
struct noa_pktproc_q_info {
	u32 cp_desc_pbase;
	u32 num_desc;
	u32 cp_buff_pbase;
	u32 fore_ptr;
	u32 rear_ptr;
} __packed;

/* Info for V2 */
struct noa_pktproc_info_v2 {
	u32 num_queues:4,
		desc_mode:2,
		irq_mode:2,
		max_packet_size:16,
		reserved:8;
	struct noa_pktproc_q_info q_info[4];
} __packed;

/* SktBuf mode */
struct noa_pktproc_desc_sktbuf {
	u64 cp_data_paddr:36,
		reserved0:4,
		control:8,
		status:8,
		lro:5,
		clat:2,
		reserved1:1;
	u16 length;
	u16 filter_result;
	u16 information;
	u8 channel_id;
	u8 reserved2:4,
		itg:2,
		reserved3:2;
} __packed;

struct noa_pktproc_queue_dl {
	u32 q_idx;
	atomic_t active;
	spinlock_t lock;

	//struct mem_link_device *mld;
	struct noa_pktproc_adaptor_dl *noa_ppa_dl;

	/* Pointer to fore_ptr of q_info. Increased AP when desc_mode is ringbuf mode */
	u32 *fore_ptr;
	/* Pointer to rear_ptr of q_info. Increased CP when desc_mode is sktbuf mode */
	u32 *rear_ptr;
	/* Follow rear_ptr when desc_mode is sktbuf mode */
	u32 done_ptr;

	/* Store */
	u64 cp_desc_pbase;
	u32 num_desc;
	u64 cp_buff_pbase;

	struct noa_pktproc_info_v2 *info_v2;
	struct noa_pktproc_q_info *q_info_ptr;	// Pointer to q_info of info_v
	struct noa_pktproc_desc_sktbuf *desc_sktbuf;	/* SktBuf mode */

	u32 desc_size;

	/* Pointer to data buffer for a queue */
	u8 __iomem *q_buff_vbase;
	unsigned long q_buff_pbase;
	u32 q_buff_size;

	//void **pf_buf; // from IOC
	/* IRQ */
	int irq;
	bool msi_irq_wake;

	/* NAPI */
	struct net_device netdev;
	struct napi_struct napi;
	struct napi_struct *napi_ptr;

	/* TASKLET */
	struct tasklet_struct q_task;

	/* Func */
	irqreturn_t (*irq_handler)(int irq, void *arg);
	void (*enable_irq)(struct noa_pktproc_queue_dl *q);
	void (*disable_irq)(struct noa_pktproc_queue_dl *q);
	int (*get_packet)(struct noa_pktproc_queue_dl *q, struct sk_buff **new_skb);
	int (*clean_rx_ring)(struct noa_pktproc_queue_dl *q, int budget, int *work_done);
	int (*alloc_rx_buf)(struct noa_pktproc_queue_dl *q);
	int (*update_fore_ptr)(struct noa_pktproc_queue_dl *q, u32 count);
	int (*clear_data_addr)(struct noa_pktproc_queue_dl *q);
};

struct noa_pktproc_adaptor_dl {
	u64 cp_base;		/* CP base address for pktproc */
	u32 info_rgn_offset;	/* Offset of info region */
	u32 info_rgn_size;	/* Size of info region */
	u32 desc_rgn_offset;	/* Offset of descriptor region */
	u32 desc_rgn_size;	/* Size of descriptor region */
	u32 buff_rgn_offset;	/* Offset of data buffer region */
	u32 buff_rgn_size;	/* Size of data buffer region */

	u32 num_queue;		/* Number of queue */
	u32 max_packet_size;	/* Max packet size CP sees */
	u32 true_packet_size;	/* True packet size AP allocated */

	struct device *dev;
	u32 skb_padding_size; // must keep some for tethering
	u32 buff_size_by_q;

	void __iomem *info_vbase;	/* I/O region for information */
	void __iomem *desc_vbase;	/* I/O region for descriptor */
	void __iomem *buff_vbase;	/* I/O region for data buffer */
	unsigned long buff_pbase;
	u64 cp_buff_pbase;
	struct noa_pktproc_queue_dl *dev_q[4];	/* Logical queue */
	struct noa_pktproc_queue_dl *apc2noa_q[1];	/* Logical queue */
	struct noa_pktproc_queue_dl *noa2apc_q[4];	/* Logical queue */

	void __iomem *apc2noa_info_vbase;	/* I/O region for information */
	void __iomem *apc2noa_desc_vbase;	/* I/O region for descriptor */
	void __iomem *noa2apc_info_vbase;	/* I/O region for information */
	void __iomem *noa2apc_desc_vbase;	/* I/O region for descriptor */

	// Fake interrupt to APC
	// Simulator ONLY
	struct napi_struct *p_napi_noa2apc;

	void **pf_buf; // from IOC

	struct ncp_md_adaptor *p_md_adaptor;

	void *apc_adaptor;
	// doorbell to APC
	// TODO: define type
	void (*doorbell)(int type, void *arg);
};

static struct noa_pktproc_adaptor_dl noa_pktproc_dl;

/* Q_info */
struct mr_pktproc_q_info_ul {
	u32 cp_desc_pbase;
	u32 num_desc;
	u32 cp_buff_pbase;
	u32 fore_ptr;
	u32 rear_ptr;
} __packed;

struct mr_pktproc_desc_ul {
	u32 data_size:20, reserve1:12;
	u32 total_pkt_size:20, reserve2:12;
	u64 sktbuf_point:36, reserve3:12, ap2cp_pbp_info:16;
	u32 last_desc:1, reserve4:31;
	u32 hw_set:1, seg_on:1, reserve5:2, segment:2, reserve6:2,
	    lcid:8, ap2cp_info_pp:16;
	u32 reserve7;
	u32 reserve8;
} __packed;

// extension fields in head room
/* must equal to cpif/noa_wrapper.h */
// the source of TO_MD path
enum {
	MR_TO_MD_FR_UNDEF = 0, // not use 0 for debugging
	MR_TO_MD_FR_APC,
	MR_TO_MD_FR_WIFI,
	MR_TO_MD_SUM
};

#define PKTPROC_UL_QUEUE_MAX 2

/* info for pktproc UL */
struct mr_pktproc_info_ul {
	u32 num_queues:4, mode:4, max_packet_size:16, end_bit_owner:1, reserve1:7;
	u32 cp_quota:16, reserve2:16;
	struct mr_pktproc_q_info_ul q_info[PKTPROC_UL_QUEUE_MAX];
} __packed;

/* Logical view for each queue */
struct mr_pktproc_queue_ul {
	u32 q_idx;
	atomic_t active; /* activated when pktproc ul init */
	atomic_t busy; /* used for flow control */
	spinlock_t lock;

	//struct mem_link_device *mld;
	struct mr_pktproc_adaptor_ul *ppa_ul;

	u32 *fore_ptr; /* indicates the last-bit raised desc pointer */
	u32 done_ptr; /* indicates the last packet written by AP */
	u32 *rear_ptr; /* indicates the last desc read by CP */

	/* Store */
	u64 cp_desc_pbase;
	u32 num_desc;
	u64 cp_buff_pbase;

	struct mr_pktproc_info_ul *ul_info;
	struct mr_pktproc_q_info_ul *q_info;	/* Pointer to q_info of info_v */
	struct mr_pktproc_desc_ul *desc_ul;

	u32 desc_size;
	u64 buff_addr_cp; /* base data address value for cp */
	u32 max_packet_size;

	/* Pointer to data buffer */
	u8 __iomem *q_buff_vbase;
	u32 q_buff_size;

	/* Statistics */
	// struct pktproc_statistics_ul stat;

	/* Func */
	int (*send_packet)(struct mr_pktproc_queue_ul *q, struct sk_buff *new_skb);
	int (*update_fore_ptr)(struct mr_pktproc_queue_ul *q, u32 count);

	u32 headroom_sz;
};

/*
 * Descriptor structure mode
 * 0: End bit is set by AP
 * 1: End bit is set by CP
 */
enum pktproc_end_bit_owner {
	END_BIT_AP,
	END_BIT_CP
};

/* PktProc adaptor for UL*/
struct mr_pktproc_adaptor_ul {
	bool support;	/* Is support PktProc feature? */

	unsigned long long cp_base;	/* CP base address for pktproc */
	unsigned long info_rgn_offset;	/* Offset of info region */
	unsigned long info_rgn_size;	/* Size of info region */
	unsigned long desc_rgn_offset;	/* Offset of descriptor region */
	unsigned long desc_rgn_size;	/* Size of descriptor region */
	unsigned long buff_rgn_offset;	/* Offset of data buffer region */
	unsigned long buff_rgn_size;	/* Size of data buffer region */

	u32 num_queue;		/* Number of queue */
	u32 default_max_packet_size;	/* packet size pktproc UL can hold */
	u32 hiprio_ack_only;
	enum pktproc_end_bit_owner end_bit_owner;	/* owner to set end bit. AP:0, CP:1 */
	u32 cp_quota;		/* max number of buffers cp allows us to transfer */
	bool use_hw_iocc;	/* H/W IO cache coherency */
	bool info_rgn_cached;
	bool desc_rgn_cached;
	bool buff_rgn_cached;
	bool padding_required;	/* requires extra length. (s5123 EVT1 only) */
#if IS_ENABLED(CONFIG_EXYNOS_CPIF_IOMMU)
	struct cpif_va_mapper *desc_map;
	struct cpif_va_mapper *buff_map;
#endif
	void __iomem *info_vbase;	/* I/O region for information */
	void __iomem *desc_vbase;	/* I/O region for descriptor */
	void __iomem *buff_vbase;	/* I/O region for data buffer */
	struct mr_pktproc_queue_ul *dev_q[PKTPROC_UL_QUEUE_MAX];/* Logical queue */
	u32 q_max_packet_size[PKTPROC_UL_QUEUE_MAX];	/* packet size pktproc UL can hold */
	u32 cp_padding;

	struct ncp_md_adaptor *p_md_adaptor;
};

static struct mr_pktproc_adaptor_ul mr_pktproc_ul;

struct ncp_md_adaptor {
	struct noa_pktproc_adaptor_dl *p_noa_ppa_dl;
	struct mr_pktproc_adaptor_ul *p_mr_ppa_ul;
	struct noa_ring *p_noa2md_ring;
	struct noa_ring *p_md2noa_ring;
	noa_ring_producer tx_pool_ring;
	unsigned long noa_port_md_fw_doorbell_addr;
	// src 0: from APC->NOA
	// src 1: from tethering path
	void (*mr2md_handling_task)(int src, struct ncp_md_adaptor *p_adaptor);
	// the start index of dev queue in Round-Robin mechanism
	int dev_q_rr_start_idx;

	void *p_mld;
	void (*mr2cp_irq)(void *);
	struct hrtimer timer_tx_recycle;
	unsigned long tx_recycle_timeout_ns;
	u32 noa2md_ring_done_ptr;
};

static struct ncp_md_adaptor md_adaptor;

extern struct ncp_md_adaptor *ncp_md_adaptor_alloc(
	struct noa_pktproc_adaptor_dl *p_noa_ppa_dl,
	struct mr_pktproc_adaptor_ul *p_mr_ppa_ul,
	struct noa_ring *p_noa2md_ring,
	struct noa_ring *p_md2noa_ring
);

void md_fw_out_fifo_init(struct mr_pktproc_adaptor_ul *p_mr_ppa_ul);
void md_fw_in_fifo_init(void);
void mr2md_handling_task(int src, struct ncp_md_adaptor *p_adaptor);
void md_refill_from_apc(struct ncp_md_adaptor *p_adaptor);
int rx_refill(struct ncp_md_adaptor *p_adaptor, struct noa_desc *p_noa_desc);
int tx_data_handling(
	struct ncp_md_adaptor *p_adaptor,
	struct noa_desc *p_noa_desc,
	const int desc_rd_idx);

// initialize device q, NOA MD FW (NCP) to Modem device
int ncp_md_fw_create_dev_q_dl(struct noa_pktproc_adaptor_dl *noa_ppa_dl, u32 buff_size_by_q);
int ncp_md_fw_create_dev_q_ul(struct mr_pktproc_adaptor_ul *mr_ppa_ul, u32 buff_size_by_q);

// TODO: replace with cmd/event mechanism
struct ncp_md_adaptor *ncp_md_init(
	struct noa_pktproc_adaptor_dl *noa_ppa_dl, u32 buff_size_by_q_dl,
	struct mr_pktproc_adaptor_ul *mr_ppa_ul, u32 buff_size_by_q_ul);

u16 get_ul_ref_idx(u16 rd_idx);

/* Simulation Only Functions */
// Let APC get IRQ related functions and fields
extern irq_handler_t ncp_md_fw_get_dev_irq_handler(u32 q_idx);
extern int *ncp_md_fw_get_irq_ref(u32 q_idx);
extern void *ncp_md_fw_get_irq_context(u32 q_idx); // return device queue DS
/* End of Simulation Only Functions */

#endif /* __NCP_MD_FW_H__ */
