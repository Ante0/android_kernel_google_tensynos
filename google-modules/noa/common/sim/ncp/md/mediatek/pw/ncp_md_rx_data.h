/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */

#ifndef __NCP_MD_RX_DATA_H__
#define __NCP_MD_RX_DATA_H__

#ifdef linux
void noa_ncp_md_rx_dpmaif_event_handle(struct noa_md_fw *md_fw, enum dpmaif_drv_intr_type type,
				       unsigned int q_mask);
#else
void noa_ncp_md_rx_dpmaif_event_handle(struct noa_md_fw *md_fw, noa_dpmaif_drv_intr_type type,
				       unsigned int q_mask);
#endif
int noa_ncp_md_rx_init(struct noa_md_fw *md_fw);
void noa_ncp_md_rx_exit(struct noa_md_fw *md_fw);
int noa_ncp_md_rx_add_tkid_to_free_pool(
	struct noa_md_fw *md_fw,
	unsigned short rx_tkid,
	u32 buf_addr_high,
	u32 buf_addr_low,
	u64 noa_data_addr);
#endif /* __NCP_MD_RX_DATA_H__ */
