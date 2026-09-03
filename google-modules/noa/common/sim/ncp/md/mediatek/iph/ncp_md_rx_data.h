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
void noa_ncp_md_rx_activate(struct noa_md_fw *md_fw);
void noa_ncp_md_rx_deactivate(struct noa_md_fw *md_fw);
void noa_ncp_md_rx_reload_bat_all(struct noa_md_fw *md_fw);
int noa_ncp_md_rx_add_tkid_to_free_pool(struct noa_md_fw *md_fw, unsigned short rx_tkid,
					u32 buf_addr_high, u32 buf_addr_low, u64 noa_data_addr);
void noa_ncp_md_rx_update_ring_info_for_offload_path(struct dpath_ap_state_payload *ap_state,
						     struct noa_md_fw_rx *rx);
void noa_ncp_md_rx_update_ring_info_for_direct_path(struct noa_md_fw_rx *rx,
						    struct dpath_ncp_state_payload *ncp_state);
void noa_ncp_md_rx_stop_rxq(struct noa_md_fw_rx *rx);
void noa_ncp_md_rx_flush_rx_table(struct noa_md_fw_rx *rx);
void noa_ncp_md_rx_done_task(unsigned long data);
void noa_ncp_md_rx_tkid_free_poll_task(unsigned long data);
void noa_ncp_md_rx_set_switch_command(struct noa_md_fw_rx *rx, uint32_t switch_cmd);
#endif /* __NCP_MD_RX_DATA_H__ */
