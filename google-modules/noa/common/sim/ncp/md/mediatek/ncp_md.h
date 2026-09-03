/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */
#ifndef __NCP_MD_H__
#define __NCP_MD_H__

#include "sys_common.h"

#define NOA_FW_RING_SIZE 512
#define NOA_FW_RING_BUDGET 128
#define NOA_REL_BAT_WEIGHT 128

extern struct noa_md_fw *g_md_fw;

enum noa_src {
	NCP_MD_FR_UNDEF = 0,
	NCP_MD_FR_APC,
	NCP_MD_FR_WIFI,
};

inline u32 noa_ncp_md_pci_read32(struct mtk_md_dev *mdev, u64 addr);
inline void noa_ncp_md_pci_write32(
		struct mtk_md_dev *mdev, u64 addr, u32 val);
//just for sw driver mode
void noa_ncp_md_dpmaif_irq_handle(
		enum dpmaif_drv_intr_type type, unsigned int q_mask);

void noa_ncp_md_rx_dpmaif_rx_done(struct work_struct *work);
int noa_ncp_md_init(void *data);
int noa_ncp_md_exit(void);

#endif /* __NCP_MD_H__ */
