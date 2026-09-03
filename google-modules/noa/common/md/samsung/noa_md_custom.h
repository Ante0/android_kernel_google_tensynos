/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (CPIF) Driver
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Jason Lin <wwlin@google.com>
 */
#ifndef __NOA_MD_CUSTOM_H__
#define __NOA_MD_CUSTOM_H__

#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/* Macros */
#define nif_err_limited(fmt, ...) \
	printk_ratelimited(KERN_ERR "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)

#define nif_info(fmt, ...) \
	pr_info("%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)

/*
 * This data structure is let APC pass md device's init parameters
 */
struct noa_md_dev_init_info {
	u64 cp_base;            /* CP base address for pktproc */
	u32 info_rgn_offset;	/* Offset of info region */
	u32 info_rgn_size;	/* Size of info region */
	u32 desc_rgn_offset;	/* Offset of descriptor region */
	u32 desc_rgn_size;	/* Size of descriptor region */
	u32 buff_rgn_offset;	/* Offset of data buffer region */
	u32 buff_rgn_size;	/* Size of data buffer region */
	u32 num_queue;		/* Number of queue */
	u32 max_packet_size;	/* Max packet size CP sees */
	u32 true_packet_size;	/* True packet size AP allocated */
	struct device *dev;  // simulation ONLY
	void __iomem *info_vbase;
	void __iomem *desc_vbase;
	void __iomem *buff_vbase;
	unsigned long buff_pbase;
	u64 cp_buff_pbase;
	u32 skb_padding_size;
	u32 buff_size_by_q;

	void __iomem *apc2noa_info_vbase;	/* I/O region for information */
	void __iomem *apc2noa_desc_vbase;	/* I/O region for descriptor */
	void __iomem *noa2apc_info_vbase;	/* I/O region for information */
	void __iomem *noa2apc_desc_vbase;	/* I/O region for descriptor */

	// Fake interrupt to APC
	// Simulator ONLY
	struct napi_struct *p_napi_noa2apc;

	// uplink path
	u64 ul_cp_base;
	void __iomem *ul_info_vbase;	/* I/O region for descriptor */
	void __iomem *ul_desc_vbase;	/* I/O region for descriptor */
	void __iomem *ul_buff_vbase;    // Simulation Only
	u32 ul_default_max_packet_size;
	u32 ul_num_queue;
	u32 ul_desc_rgn_size;
	u32 ul_buff_rgn_offset;	/* Offset of data buffer region */
	u32 ul_buff_size_by_q;
	u32 ul_q_max_packet_size[2];
	u32 ul_default_headroom_sz;
	u32 ul_cp_padding;
	// end of uplink path

	// for doorbell to Modem device
	void *p_mld;
	void (*mr2cp_irq)(void *);
};

enum DOORBELL_TYPE {
	BELL_NOA2APC_Q0 = 0,
	BELL_NOA2APC_Q1,
	BELL_NOA2APC_Q2,
	BELL_NOA2APC_Q3,
	BELL_SUM
};

/*
 * return the pointer of noa_pktproc_adaptor_dl
 * for the second initial stage (by cpif cmd)
 * assign VA mapping to NOA through this path
 */
extern void *noa_md_custom_init(struct noa_md_dev_init_info *init_info);
extern u32 *noa_md_get_apc2noa_write_ref(void);
extern u32 *noa_md_get_apc2noa_read_ref(void);
extern void noa_md_set_apc2noa_max_num(u32 max_num);
extern void noa_md_set_apc2noa_desc_size(u32 desc_size);
extern void noa_md_apc2noa_doorbell(void);
extern u32 *noa_md_get_noa2apc_write_ref(void);
extern u32 *noa_md_get_noa2apc_read_ref(void);
extern void noa_md_set_noa2apc_max_num(u32 max_num);
extern void noa_md_set_noa2apc_desc_size(u32 desc_size);
extern void noa_md_set_va_mapping(void *p_adaptor, void **p_map);

/* Debug Functions */
#define DBG_BUF_MAX_SIZE 4096
static u8 dbg_buf[DBG_BUF_MAX_SIZE] = {0};
static u8 fake_eth_src[6] = {0x02, 0x07, 0x00, 0x00, 0x00, 0x00};
static u8 fake_eth_dst[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
extern void dbg_pr_buf(u8 *p_buf, u32 len, u8 *prefix);
// output the format of wireshark raw
extern void dbg_pr_buf_w_fake_eth(u8 *p_buf, u32 len);
/* End of Debug Functions */

struct noa_md_client {
	struct ncp_md_adaptor *p_md_adaptor;
};

#endif /* __NOA_MD_CUSTOM_H__ */
