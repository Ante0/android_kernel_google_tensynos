// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA MD (MTK) Driver
 *
 * Copyright 2024 Google LLC.
 */

#include "modem_fw_ring.h"
#include "ncp_md.h"
#include "ncp_md_irq.h"
#include "ncp_md_tx_data.h"
#include "ncp_md_rx_data.h"
#include "ncp_modem_data.h"
#include "noa_md_apc2ncp_ring.h"

#ifndef linux
struct noa_md_fw_tx tx_init;
struct noa_md_fw_rx rx_init;
struct modem_fw_ring_ops tx_ring_ops;
struct modem_fw_ring_ops rx_ring_ops;
struct noa_md_fw md_fw_init = {};

// 5 NOA Tx DRB Rings, synced with the drb_cnt in the dpmaif_txq_cfg(txqs) defined
// in mtk_dpmaif_drv_t900.c
#define TX_TKID_QUEUE_DRB0_SIZE 2048
#define TX_TKID_QUEUE_DRB1_SIZE 2048
#define TX_TKID_QUEUE_DRB2_SIZE 128
#define TX_TKID_QUEUE_DRB3_SIZE 1024
#define TX_TKID_QUEUE_DRB4_SIZE 2048
SEC_EXRAM_DATA u16 tx_tkid_queue_drb0[TX_TKID_QUEUE_DRB0_SIZE] = { 0 };
SEC_EXRAM_DATA u16 tx_tkid_queue_drb1[TX_TKID_QUEUE_DRB1_SIZE] = { 0 };
SEC_EXRAM_DATA u16 tx_tkid_queue_drb2[TX_TKID_QUEUE_DRB2_SIZE] = { 0 };
SEC_EXRAM_DATA u16 tx_tkid_queue_drb3[TX_TKID_QUEUE_DRB3_SIZE] = { 0 };
SEC_EXRAM_DATA u16 tx_tkid_queue_drb4[TX_TKID_QUEUE_DRB4_SIZE] = { 0 };
#endif

struct noa_md_fw *g_md_fw;

static unsigned int traffic_stats_shift = 7;
#define NOA_NCP_MD_RING_TPUT_CALC(CNT, MS_SHIFT)	((CNT) >> (MS_SHIFT))

inline u32 noa_ncp_md_pci_read32(struct mtk_md_dev *mdev, u64 addr)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	return ioread32(priv->ext_reg_base + addr);
}

inline void noa_ncp_md_pci_write32(struct mtk_md_dev *mdev, u64 addr, u32 val)
{
	struct mtk_pci_priv *priv = mdev->hw_priv;

	iowrite32(val, priv->ext_reg_base + addr);
}

#ifndef linux
static void noa_ncp_md_init_queues(struct noa_md_fw *md_fw, struct noa_md_protobuf *buf)
{
        uint32_t low_address = 0x0, high_address = 0x0;
	for (uint32_t index = 0; index < kMaxUlQueueSize; ++index) {
                // Update for modem drb rings.
		md_fw->tx->txqs[index].drb_base = buf->txqs[index].drb_base;
		md_fw->tx->txqs[index].drb_cnt = buf->txqs[index].drb_cnt;
		md_fw->tx->txqs[index].db_delay_ms = buf->txqs[index].db_delay_ms;
		md_fw->tx->txqs[index].burst_submit_cnt =
		                  buf->txqs[index].burst_submit_cnt;
		md_fw->tx->txqs[index].drb_wr_idx = 0;
		md_fw->tx->txqs[index].drb_rd_idx = 0;
		md_fw->tx->txqs[index].drb_rel_rd_idx = 0;
		md_fw->tx->txqs[index].id = index;
		low_address = md_fw->tx->txqs[index].drb_base & 0xFFFFFFFF;
		high_address = md_fw->tx->txqs[index].drb_base >> 32;
		NCP_MD_INFO(
                            "txqs[%d]high_addr=[%p], low_addr=[%p], drb_cnt=%d,"
                            "db_delay_ms=%d, burst_submit_cnt=%d",
                            index, high_address, low_address,
                            md_fw->tx->txqs[index].drb_cnt,
                            md_fw->tx->txqs[index].db_delay_ms,
                            md_fw->tx->txqs[index].burst_submit_cnt);
        }
	md_fw->tx->txq_cnt = kMaxUlQueueSize;
}
#endif

static irqreturn_t noa_ncp_md_apc2ncp_isr(int id, void *data)
{
	struct noa_md_fw *md_fw = (struct noa_md_fw *)data;
	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_md_fw_rx *rx = md_fw->rx;

	NCP_MD_INFO("enter, id=[%d]", id);
	if (id >= kNoaModemRingTxDrb0 && id <= kNoaModemRingTxDrb4) {
		tx->isr_drb_index = id;
		tasklet_schedule(&tx->apc2ncp_task);
	} else if (id >= kNoaModemRingRxRefillNormalBat0 && id < kNoaModemApcToNcpRingMax){
		rx->isr_refill_index = id;
		tasklet_schedule(&rx->rx_tkid_free_poll_task);
	} else {
		NCP_MD_ERROR("Invalid id!");
	}
	return IRQ_HANDLED;
}

static int noa_ncp_md_calc_tput(atomic_t *stats, unsigned int time_shift)
{
	int tmp_stats = ATOMIC_READ(stats);
	ATOMIC_SUB(tmp_stats, stats);
	return NOA_NCP_MD_RING_TPUT_CALC(tmp_stats, time_shift);
}

static void noa_ncp_md_ring_rel_ctrl_timer_func(struct timer_list *t)
{
#ifdef linux
	struct noa_md_fw *md_fw = from_timer(md_fw, t, ring_rel_ctrl_timer);
#else
	struct noa_md_fw *md_fw = container_of(t, struct noa_md_fw, ring_rel_ctrl_timer);
#endif
	struct noa_md_fw_tx *tx = md_fw->tx;
	struct noa_tx_queue *txq;

	unsigned int tmp_tput;
	int i;
	for (i = 0; i < tx->txq_cnt; i++) {
		txq = &tx->txqs[i];
		tmp_tput = noa_ncp_md_calc_tput(&txq->drb_stats, traffic_stats_shift);
		noa_ncp_md_tx_drb_rel_ctrl(txq, tmp_tput);
	}
	mod_timer(&md_fw->ring_rel_ctrl_timer,
		  jiffies + msecs_to_jiffies(BIT(traffic_stats_shift)));
}

int noa_ncp_md_init(void *data)
{
	int ret = 0;
	struct noa_md_fw *md_fw;
	NCP_MD_INFO("enter");
#ifdef linux
	md_fw = (struct noa_md_fw *)data;
#else
	struct noa_md_protobuf *buf = (struct noa_md_protobuf *)data;
	md_fw = &md_fw_init;
	md_fw->tx = &tx_init;
	md_fw->rx = &rx_init;
	md_fw->tx->ring_ops = &tx_ring_ops;
	md_fw->rx->ring_ops = &rx_ring_ops;
	noa_ncp_md_init_queues(md_fw, buf);

	md_fw->tx->txqs[kNoaModemRingTxDrb0].tkid_queue = tx_tkid_queue_drb0;
	md_fw->tx->txqs[kNoaModemRingTxDrb1].tkid_queue = tx_tkid_queue_drb1;
	md_fw->tx->txqs[kNoaModemRingTxDrb2].tkid_queue = tx_tkid_queue_drb2;
	md_fw->tx->txqs[kNoaModemRingTxDrb3].tkid_queue = tx_tkid_queue_drb3;
	md_fw->tx->txqs[kNoaModemRingTxDrb4].tkid_queue = tx_tkid_queue_drb4;
	md_fw->tx_buffer_pool.va_base = (void *)(NOA_MD_FW_TX_BUF_POOL_BASE);
	md_fw->tx_buffer_pool.pa_base = NOA_MD_FW_TX_BUF_POOL_BASE;

	ret = noa_dpmaif_register_event_handler(noa_dpmaif_event_handler);
	if (ret < 0) {
		NCP_MD_ERROR("Failed to register event handler. ret=[%d]", ret);
		return ret;
	}
#endif
	md_fw->tx->ring_ops = modem_fw_ring_get_tx_ops();
	ret = noa_ncp_md_tx_init(md_fw);
	if (ret) {
		noa_ncp_md_tx_exit(md_fw);
	}

	md_fw->rx->ring_ops = modem_fw_ring_get_rx_ops();

	ret = noa_ncp_md_rx_init(md_fw);
	if (ret) {
		noa_ncp_md_rx_exit(md_fw);
	}

	ret = noa_md_apc2ncp_ring_ncp_setup(md_fw->dev);
	if (ret) {
		noa_md_apc2ncp_ring_ncp_release();
	}

	ret = ncp_md_irq_init("modem_ncp", true);
	if (ret) {
		NCP_MD_ERROR("ncp_md_irq_init=[%d] for NCP", ret);
		ncp_md_irq_exit(true);
		return ret;
	}
	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
		ret = ncp_md_irq_register(ring_type, noa_ncp_md_apc2ncp_isr, md_fw, true);
		if (ret) {
			NCP_MD_ERROR("ncp_md_irq_register[%d]=[%d] for NCP", ring_type, ret);
			return ret;
		}
	}

	timer_setup(&md_fw->ring_rel_ctrl_timer, noa_ncp_md_ring_rel_ctrl_timer_func, 0);
	mod_timer(&md_fw->ring_rel_ctrl_timer,
		  jiffies + msecs_to_jiffies(BIT(traffic_stats_shift)));

	g_md_fw = md_fw;

	NCP_MD_INFO("ret=[%d]", ret);
	return ret;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ncp_md_init);
#endif

int noa_ncp_md_exit(void)
{
	NCP_MD_INFO("enter");
	if (!g_md_fw) {
		NCP_MD_ERROR("g_md_fw is NULL");
	}

	noa_ncp_md_tx_exit(g_md_fw);
	NCP_MD_INFO("noa_ncp_md_tx_exit");

	noa_ncp_md_rx_exit(g_md_fw);
	NCP_MD_INFO("noa_ncp_md_rx_exit");

	noa_md_apc2ncp_ring_ncp_release();
	NCP_MD_INFO("noa_md_apc2ncp_ring_ncp_release");

	for (int ring_type = 0; ring_type < kNoaModemApcToNcpRingMax; ring_type++) {
		ncp_md_irq_unregister(ring_type, g_md_fw, true);
	}
	ncp_md_irq_exit(true);
	NCP_MD_INFO("ncp_md_irq_exit for NCP");

	g_md_fw = NULL;

	NCP_MD_INFO("exit");
	return 0;
}
#ifdef linux
EXPORT_SYMBOL_GPL(noa_ncp_md_exit);
#endif

