/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */

#ifndef __NCP_MD_TX_DATA_H__
#define __NCP_MD_TX_DATA_H__

#ifdef linux
void noa_ncp_md_tx_doorbell_work(struct work_struct *work);
void noa_ncp_md_tx_done_work(struct work_struct *work);
#else
void noa_ncp_md_tx_doorbell_work(void* data);
void noa_ncp_md_tx_done_work(void* data);
#endif
void noa_ncp_md_tx_irq_tx_done(
        struct noa_md_fw *md_fw, unsigned int q_mask);
int noa_ncp_md_tx_init(struct noa_md_fw *md_fw);
void noa_ncp_md_tx_exit(struct noa_md_fw *md_fw);
int noa_ncp_md_tx_pool_replenish(struct noa_md_fw *md_fw, u16 pktid);
void noa_ncp_md_tx_drb_rel_ctrl(struct noa_tx_queue *txq, unsigned int drb_speed);
#endif /* __NCP_MD_TX_DATA_H__ */
