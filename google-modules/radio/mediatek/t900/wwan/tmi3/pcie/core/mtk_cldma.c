// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include "mtk_bm.h"
#include "mtk_cldma.h"
#include "mtk_cldma_drv.h"
#include "mtk_cldma_hw_interface.h"
#include "mtk_debug.h"
#include "mtk_debugfs.h"
#include "mtk_dev.h"
#include "mtk_except.h"
#include "mtk_fsm.h"
#include "mtk_pcie_memlog.h"
#include "mtk_pcie_trace.h"
#include "mtk_pm.h"
#include "mtk_statistics.h"
#ifdef CONFIG_UT_PCIE_CLDMA
#include "ut_cldma.h"
#endif
#define TAG "CLDMA"
#define cldma_drv_ops_null	NULL
#define CLDMA_STATS_PERIOD_S	(2)
#define DMA_POOL_NAME_LEN	(64)
#define WAIT_HWO_ROUND		(100)
#define WAIT_HWO_TIME		(20)
#define CLDMA_WS_NAME_LEN	(25)
#define CLDMA_RETRY_DELAY_MS	(100)
/* VALID_RX_INTR_GAP_NS : If the interval between two interrupts exceeds 5ms, the average */
/* interrupt interval is recalculated. With seven interrupts every 5ms, there will only  */
/* be 1500 interrupts per second */
#define VALID_RX_INTR_GAP_NS	(5000000)
/* TIME_GAP_THRESHOLD_NS : Limit the number of interrupts to a maximum if 2000 per second */
#define TIME_GAP_THRESHOLD_NS	(500000)
/* WAIT_RX_INTR_TIME_US : Waiting time for an interrupt is approximately half of */
/* the maximum allowed interval between interrupts */
#define WAIT_RX_INTR_TIME_US	(300)
#define NUM_FOR_CAL_CNT		(3)
#define RX_FREQ_STAT_CNT	BIT(NUM_FOR_CAL_CNT)
#define AVG_TIME_GAP(avg, gap)	(((RX_FREQ_STAT_CNT - 1) * (avg) + (gap)) >> NUM_FOR_CAL_CNT)
#define NO_BUDGET		(0)
#define MAX_POLLING_ROUND	(200)
#define PRE_CHECK_INTERVAL_US	(200)
#define PRE_CHECK_MAX_CNT	(10)

static unsigned int mtk_ctrl_keep_wake_time_ms = 500;
enum tx_done_exit_flag {
	TX_DONE_EXIT_INIT,
	TX_DONE_EXIT_NO_VMA,
	TX_DONE_EXIT_HW_OWN,
	TX_DONE_EXIT_FLAG_MAX
};

#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
#include "mtk_google.h"
#include "pcie/link-exception.h"
#endif

#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
#include "pcie/md2ap-wakemon.h"
#endif

#ifdef CONFIG_MTK_MEMLOG_EVENT_SUPPORT

struct event_stats_ctrl_tx {
	struct memlog_event_msg event_msg;
	int cldma_id;
	struct cldma_traffic_tx ctrl_tx;
	struct cldma_traffic_irq ctrl_irq;
} __packed;

#define MTK_DBG_STATS_CTRL_TX(mdev, data) \
do { \
	struct event_stats_ctrl_tx *event_stats_tx; \
	struct cldma_drv_info *__drv_info = data; \
	struct mtk_md_dev *__mdev = mdev; \
	event_stats_tx = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_STATS, \
						sizeof(struct event_stats_ctrl_tx)); \
	if (!event_stats_tx) \
		break; \
	mtk_memlog_event_msg_init(&event_stats_tx->event_msg, STATS_CTRL_TX); \
	event_stats_tx->cldma_id = __drv_info->hw_id; \
	memcpy(&event_stats_tx->ctrl_tx, &__drv_info->stats.cldma_tx, \
	       sizeof(struct cldma_traffic_tx)); \
	memcpy(&event_stats_tx->ctrl_irq, &__drv_info->stats.cldma_irq, \
	       sizeof(struct cldma_traffic_irq)); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_STATS); \
} while (0)

#define MTK_DBG_STATS_CTRL_TX_WITH_BUF(data, __buf, __size) \
do { \
	struct event_stats_ctrl_tx *event_stats_tx = (struct event_stats_ctrl_tx *)__buf; \
	struct cldma_drv_info *__drv_info = data; \
	mtk_memlog_add_info(&event_stats_tx->event_msg); \
	mtk_memlog_event_msg_init(&event_stats_tx->event_msg, STATS_CTRL_TX); \
	event_stats_tx->cldma_id = __drv_info->hw_id; \
	memcpy(&event_stats_tx->ctrl_tx, &__drv_info->stats.cldma_tx, \
	       sizeof(struct cldma_traffic_tx)); \
	memcpy(&event_stats_tx->ctrl_irq, &__drv_info->stats.cldma_irq, \
	       sizeof(struct cldma_traffic_irq)); \
	__size = sizeof(struct event_stats_ctrl_tx); \
} while (0)

struct event_stats_ctrl_rx {
	struct memlog_event_msg event_msg;
	int cldma_id;
	struct cldma_traffic_rx ctrl_rx;
	struct cldma_traffic_irq ctrl_irq;
} __packed;

#define MTK_DBG_STATS_CTRL_RX(mdev, data) \
do { \
	struct event_stats_ctrl_rx *event_stats_rx; \
	struct cldma_drv_info *__drv_info = data; \
	struct mtk_md_dev *__mdev = mdev; \
	event_stats_rx = mtk_memlog_req_address(__mdev, MTK_MEMLOG_RG_STATS, \
						sizeof(struct event_stats_ctrl_rx)); \
	if (!event_stats_rx) \
		break; \
	mtk_memlog_event_msg_init(&event_stats_rx->event_msg, STATS_CTRL_RX); \
	event_stats_rx->cldma_id = __drv_info->hw_id; \
	memcpy(&event_stats_rx->ctrl_rx, &__drv_info->stats.cldma_rx, \
	       sizeof(struct cldma_traffic_rx)); \
	memcpy(&event_stats_rx->ctrl_irq, &__drv_info->stats.cldma_irq, \
	       sizeof(struct cldma_traffic_irq)); \
	mtk_memlog_req_done(__mdev, MTK_MEMLOG_RG_STATS); \
} while (0)

#define MTK_DBG_STATS_CTRL_RX_WITH_BUF(data, __buf, __size) \
do { \
	struct event_stats_ctrl_rx *event_stats_rx = (struct event_stats_ctrl_rx *)__buf; \
	struct cldma_drv_info *__drv_info = data; \
	mtk_memlog_add_info(&event_stats_rx->event_msg); \
	mtk_memlog_event_msg_init(&event_stats_rx->event_msg, STATS_CTRL_RX); \
	event_stats_rx->cldma_id = __drv_info->hw_id; \
	memcpy(&event_stats_rx->ctrl_rx, &__drv_info->stats.cldma_rx, \
	       sizeof(struct cldma_traffic_rx)); \
	memcpy(&event_stats_rx->ctrl_irq, &__drv_info->stats.cldma_irq, \
	       sizeof(struct cldma_traffic_irq)); \
	__size = sizeof(struct event_stats_ctrl_rx); \
} while (0)

#else
#define MTK_DBG_STATS_CTRL_TX(mdev, data) \
do { \
	struct cldma_drv_info *__drv_info = data; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 __i; \
	for (__i = 0; __i < HW_QUEUE_NUM; __i++) { \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"CLDMA%d txq%u: tx_sw_pkt=%llu, tx_hw_pkt=%llu, tx_done_last_time=%llu, " \
			"tx_done_last_cnt=%u, txq_done=%llu\n", \
			__drv_info->hw_id, __i, \
			__drv_info->stats.cldma_tx.tx_sw_pkt[__i], \
			__drv_info->stats.cldma_tx.tx_hw_pkt[__i], \
			__drv_info->stats.cldma_tx.tx_done_last_time[__i], \
			__drv_info->stats.cldma_tx.tx_done_last_cnt[__i], \
			__drv_info->stats.cldma_tx.txq_done[__i]); \
	} \
	MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
		"CLDMA%d irq: irq_total_cnt=%llu, irq_last_time=%llu\n", \
		__drv_info->hw_id, \
		__drv_info->stats.cldma_irq.irq_total_cnt, \
		__drv_info->stats.cldma_irq.irq_last_time); \
} while (0)

#define MTK_DBG_STATS_CTRL_TX_WITH_BUF(data, buf, size) \
do { \
	struct cldma_drv_info *__drv_info = data; \
	u8 __i; \
	char *__buf = buf; \
	ssize_t __size = 0; \
	for (__i = 0; __i < HW_QUEUE_NUM; __i++) { \
		__size += sprintf(__buf + __size, \
				  "CLDMA%d txq%u: tx_sw_pkt=%llu, tx_hw_pkt=%llu, " \
				  "tx_done_last_time=%llu, tx_done_last_cnt=%u, " \
				  "txq_done=%llu\n", \
				  __drv_info->hw_id, __i, \
				  __drv_info->stats.cldma_tx.tx_sw_pkt[__i], \
				  __drv_info->stats.cldma_tx.tx_hw_pkt[__i], \
				  __drv_info->stats.cldma_tx.tx_done_last_time[__i], \
				  __drv_info->stats.cldma_tx.tx_done_last_cnt[__i], \
				  __drv_info->stats.cldma_tx.txq_done[__i]); \
	} \
	__size += sprintf(__buf + __size, \
			  "CLDMA%d irq: irq_total_cnt=%llu, irq_last_time=%llu\n", \
			  __drv_info->hw_id, \
			  __drv_info->stats.cldma_irq.irq_total_cnt, \
			  __drv_info->stats.cldma_irq.irq_last_time); \
	size = __size; \
} while (0)

#define MTK_DBG_STATS_CTRL_RX(mdev, data) \
do { \
	struct cldma_drv_info *__drv_info = data; \
	struct mtk_md_dev *__mdev = mdev; \
	u8 __i; \
	for (__i = 0; __i < HW_QUEUE_NUM; __i++) { \
		MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
			"CLDMA%d rxq%u: rx_pkt=%llu, rx_done_last_time=%llu, " \
			"rx_done_last_cnt=%u, rxq_done=%llu\n", \
			__drv_info->hw_id, __i, \
			__drv_info->stats.cldma_rx.rx_pkt[__i], \
			__drv_info->stats.cldma_rx.rx_done_last_time[__i], \
			__drv_info->stats.cldma_rx.rx_done_last_cnt[__i], \
			__drv_info->stats.cldma_rx.rxq_done[__i]); \
	} \
	MTK_DBG(__mdev, MTK_DBG_STATS, MTK_MEMLOG_RG_STATS, \
		"CLDMA%d irq: irq_total_cnt=%llu, irq_last_time=%llu\n", \
		__drv_info->hw_id, \
		__drv_info->stats.cldma_irq.irq_total_cnt, \
		__drv_info->stats.cldma_irq.irq_last_time); \
} while (0)

#define MTK_DBG_STATS_CTRL_RX_WITH_BUF(data, buf, size) \
do { \
	struct cldma_drv_info *__drv_info = data; \
	u8 __i; \
	char *__buf = buf; \
	ssize_t __size = 0; \
	for (__i = 0; __i < HW_QUEUE_NUM; __i++) { \
		__size += sprintf(__buf + __size, \
				  "CLDMA%d rxq%u: rx_pkt=%llu, rx_done_last_time=%llu, " \
				  "rx_done_last_cnt=%u, rxq_done=%llu\n", \
				  __drv_info->hw_id, __i, \
				  __drv_info->stats.cldma_rx.rx_pkt[__i], \
				  __drv_info->stats.cldma_rx.rx_done_last_time[__i], \
				  __drv_info->stats.cldma_rx.rx_done_last_cnt[__i], \
				  __drv_info->stats.cldma_rx.rxq_done[__i]); \
	} \
	__size += sprintf(__buf + __size, \
			  "CLDMA%d irq: irq_total_cnt=%llu, irq_last_time=%llu\n", \
			  __drv_info->hw_id, \
			  __drv_info->stats.cldma_irq.irq_total_cnt, \
			  __drv_info->stats.cldma_irq.irq_last_time); \
	size = __size; \
} while (0)

#endif

static void cldma_bd_dump(struct cldma_drv_info *drv_info, int nr_bds,
			  struct bd_dsc *bd_dsc_pool)
{
	struct bd_dsc *bd_dsc;
	int *val;
	int i;

	MTK_DBG(drv_info->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP, "BD:\n");
	for (i = 0; i < nr_bds; i++) {
		bd_dsc = bd_dsc_pool + i;
		val = (int *)bd_dsc->bd;
		MTK_DBG(drv_info->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
			"%d: %08x %08x %08x %08x %08x %08x\n", i,
			 *val, *(val + 1), *(val + 2), *(val + 3),
			 *(val + 4), *(val + 5));
	}
}

static void cldma_gpd_dump(struct cldma_drv_info *drv_info, u32 qno)
{
	struct txq *txq = drv_info->txq[qno];
	struct rxq *rxq = drv_info->rxq[qno];
	struct tx_req *txreq;
	struct rx_req *rxreq;
	int *val;
	int i;

	if (!txq || !rxq)
		return;

	/* TX GPD */
	MTK_DBG(drv_info->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		"CLDMA%d TXQ%d tx_budget:%d GPD:\n",
		drv_info->hw_id, qno, atomic_read(&txq->req_budget));
	for (i = 0; i < txq->nr_gpds; i++) {
		txreq = txq->req_pool + i;
		val = (int *)txreq->gpd;
		MTK_DBG(drv_info->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
			"%d: %08x %08x %08x %08x %08x %08x\n", i,
			 *val, *(val + 1), *(val + 2), *(val + 3), *(val + 4), *(val + 5));
		if (txq->nr_bds)
			cldma_bd_dump(drv_info, txq->nr_bds, txreq->bd_dsc_pool);
	}

	/* RX GPD */
	MTK_DBG(drv_info->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
		"CLDMA%d RXQ%d GPD:\n", drv_info->hw_id, qno);
	for (i = 0; i < rxq->nr_gpds; i++) {
		rxreq = rxq->req_pool + i;
		val = (int *)rxreq->gpd;
		MTK_DBG(drv_info->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
			"%d: %08x %08x %08x %08x %08x %08x\n", i,
			 *val, *(val + 1), *(val + 2), *(val + 3), *(val + 4), *(val + 5));
		if (rxq->nr_bds)
			cldma_bd_dump(drv_info, rxq->nr_bds, rxreq->bd_dsc_pool);
	}
}

static ssize_t cldma_dbg_write(void *data, const char *buf, ssize_t cnt)
{
	struct cldma_dev *cd = data;

	mtk_fsm_evt_submit(cd->trans->mdev, FSM_EVT_DUMP, FSM_F_DFLT, NULL, 0, 0);

	return cnt;
}

MTK_DBGFS(cldma_dbg, NULL, cldma_dbg_write);

static inline void cldma_dbgfs_init(struct cldma_dev *cd)
{
#define CLDMA_DBGFS_NAME_LEN	32
	char name[CLDMA_DBGFS_NAME_LEN] = {0};

	snprintf(name, CLDMA_DBGFS_NAME_LEN, "cldma");
	cd->dentry = mtk_dbgfs_create_dir(mtk_get_dev_dentry(cd->trans->mdev), name);
	if (!cd->dentry)
		return;

	mtk_dbgfs_create_file(cd->dentry, &mtk_dbgfs_cldma_dbg, cd);
}

static inline void cldma_dbgfs_exit(struct cldma_dev *cd)
{
	mtk_dbgfs_remove(cd->dentry);
}

static void mtk_cldma_get_drv_info(struct cldma_drv_info *drv_info, u32 hw_ver)
{
	struct cldma_drv_info_desc *p_drv_info;
	u8 i;

	for (i = 0; (p_drv_info = &cldma_drv_info_tbl[i]) && p_drv_info &&
	     p_drv_info->drv_ops && p_drv_info->hw_regs; i++)
		if (p_drv_info->hw_ver == hw_ver) {
			drv_info->drv_ops = p_drv_info->drv_ops;
			drv_info->hw_regs = p_drv_info->hw_regs;
		}
}

static void mtk_cldma_trigger_dev_ee(struct mtk_md_dev *mdev, enum mtk_hif_id hif_id)
{
	mtk_fsm_evt_submit(mdev, FSM_EVT_DUMP, FSM_F_DFLT, NULL, 0, 0);
	switch (hif_id) {
	case CLDMA0:
		mtk_fsm_trigger_mdee(mdev);
		break;
	case CLDMA1:
		mtk_fsm_trigger_mdee(mdev);
		break;
	case CLDMA4:
		mtk_fsm_trigger_mdee(mdev);
		break;
	default:
		break;
	}
}

static bool cldma_hw_is_accessible(struct cldma_dev *cd)
{
	if (unlikely(test_bit(CLDMA_NOT_ACCESSIBLE, &cd->err_event)))
		return false;
	if (likely(mtk_pci_mmio_check(cd->trans->mdev)))
		return true;

	set_bit(CLDMA_NOT_ACCESSIBLE, &cd->err_event);
	MTK_DBG(cd->trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"%ps Detected Link ERROR\n", __builtin_return_address(0));
	mtk_exception_report_evt(cd->trans->mdev, EXCEPTION_LINK_ERR);

	return false;
}

static void cldma_access_error(struct cldma_dev *cd)
{
	if (test_bit(CLDMA_NOT_ACCESSIBLE, &cd->err_event))
		return;

	set_bit(CLDMA_NOT_ACCESSIBLE, &cd->err_event);
	MTK_DBG(cd->trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"%ps Detected Link ERROR\n", __builtin_return_address(0));
	mtk_exception_report_evt(cd->trans->mdev, EXCEPTION_LINK_ERR);
}

static ssize_t mtk_stats_cldma_tx_cb(struct mtk_md_dev *mdev, void *data, char *buf)
{
	ssize_t size = 0;

	if (!buf)
		MTK_DBG_STATS_CTRL_TX(mdev, data);
	else
		MTK_DBG_STATS_CTRL_TX_WITH_BUF(data, buf, size);

	return size;
}

static ssize_t mtk_stats_cldma_rx_cb(struct mtk_md_dev *mdev, void *data, char *buf)
{
	ssize_t size = 0;

	if (!buf)
		MTK_DBG_STATS_CTRL_RX(mdev, data);
	else
		MTK_DBG_STATS_CTRL_RX_WITH_BUF(data, buf, size);

	return size;
}

static void mtk_cldma_rx_calculate_freq(struct rx_freq_stat *freq_stat)
{
	u64 curr_time;
	u64 time_gap;

	curr_time = local_clock();
	time_gap = curr_time - freq_stat->last_isr_time;
	freq_stat->last_isr_time = curr_time;
	if (time_gap > VALID_RX_INTR_GAP_NS) {
		freq_stat->avg_time_gap = 0;
		freq_stat->stat_cnt = 0;
		return;
	}

	if (freq_stat->stat_cnt >= RX_FREQ_STAT_CNT) {
		freq_stat->avg_time_gap = AVG_TIME_GAP(freq_stat->avg_time_gap, time_gap);
	} else {
		freq_stat->avg_time_gap += time_gap;
		freq_stat->stat_cnt++;
		if (freq_stat->stat_cnt == RX_FREQ_STAT_CNT)
			freq_stat->avg_time_gap >>= NUM_FOR_CAL_CNT;
	}
}

static int mtk_cldma_isr(int irq_id, void *param)
{
	struct cldma_drv_info *drv_info = param;
	struct cldma_traffic_irq *stats_irq;
	struct cldma_traffic_tx *stats_tx;
	struct cldma_traffic_rx *stats_rx;
	struct mtk_md_dev *mdev;
	u32 tx_done, rx_done;
	u32 tx_sta, rx_sta;
	struct txq *txq;
	struct rxq *rxq;
	int i;

	mdev = drv_info->mdev;
	stats_tx = &drv_info->stats.cldma_tx;
	stats_rx = &drv_info->stats.cldma_rx;
	stats_irq = &drv_info->stats.cldma_irq;

	stats_irq->irq_total_cnt++;
	stats_irq->irq_last_time = local_clock();

	drv_info->drv_ops->cldma_get_intr_status(drv_info, &tx_sta, &rx_sta);
	tx_done = (tx_sta >> QUEUE_XFER_DONE) & 0xFF;
	rx_done = (rx_sta >> QUEUE_XFER_DONE) & 0xFF;

	if (tx_done) {
		for (i = 0; i < HW_QUEUE_NUM; i++) {
			txq = drv_info->txq[i];
			if (!(tx_done & BIT(i)) || !txq)
				continue;

			stats_tx->tx_done_last_time[txq->txqno] = local_clock();
			stats_tx->txq_done[i]++;
			trace_mtk_ctrl_tx_isr(drv_info->hw_id, i);
			queue_work(drv_info->wq, &txq->tx_done_work);
		}
	}
	if (rx_done) {
		for (i = 0; i < HW_QUEUE_NUM; i++) {
			rxq = drv_info->rxq[i];
			if (!(rx_done & BIT(i)) || !rxq)
				continue;

			stats_rx->rx_done_last_time[rxq->rxqno] = local_clock();
			stats_rx->rxq_done[i]++;
			trace_mtk_ctrl_rx_isr(drv_info->hw_id, i);
			mtk_cldma_rx_calculate_freq(&rxq->freq_stat);
			__pm_stay_awake(rxq->ws);
			rxq->curr_cnt = drv_info->drv_ops->cldma_get_gpd_cnt(drv_info, DIR_RX, i);
			queue_work(drv_info->wq, &rxq->rx_done_work);
		}
	}

	if ((tx_sta >> QUEUE_ERROR) & 0xFF || (rx_sta >> QUEUE_ERROR) & 0xFF)
		mtk_fsm_evt_submit(mdev, FSM_EVT_DUMP, FSM_F_DFLT, NULL, 0, 0);

	mtk_pci_clear_irq(mdev, drv_info->pci_ext_irq_id);
	mtk_pci_unmask_irq(mdev, drv_info->pci_ext_irq_id);

	return IRQ_HANDLED;
}

static const int mtk_cldma_hw_id_tbl[NR_CLDMA] = {
	[CLDMA0] = CLDMA0_HW_ID,
	[CLDMA1] = CLDMA1_HW_ID,
	[CLDMA4] = CLDMA4_HW_ID,
};

static int mtk_cldma_dev_init(struct cldma_dev *cd, int hif_id)
{
	char gpd_pool_name[DMA_POOL_NAME_LEN];
	char bd_pool_name[DMA_POOL_NAME_LEN];
	struct cldma_drv_info *drv_info;
	struct cldma_hw_regs *hw_regs;
	struct mtk_md_dev *mdev;
	unsigned int flag;
	int hw_id;
	int ret;

	if (!cd || hif_id >= NR_CLDMA)
		return -EINVAL;

	if (cd->cldma_drv_info[hif_id])
		return 0;

	hw_id = mtk_cldma_hw_id_tbl[hif_id];
	mdev = cd->trans->mdev;
	drv_info = devm_kzalloc(mdev->dev, sizeof(*drv_info), GFP_KERNEL);
	if (!drv_info)
		return -ENOMEM;

	drv_info->cd = cd;
	drv_info->mdev = mdev;
	drv_info->hif_id = hif_id;
	drv_info->hw_id = hw_id;
	mutex_init(&drv_info->q_exit_mtx);
	mtk_cldma_get_drv_info(drv_info, mdev->hw_ver);

	if (!drv_info->drv_ops || !drv_info->hw_regs) {
		MTK_ERR(mdev, "Failed to find CLDMA Driver for PCI %x\n", mdev->hw_ver);
		goto err_free_drv_info;
	}

	hw_regs = drv_info->hw_regs;
	snprintf(gpd_pool_name, DMA_POOL_NAME_LEN, "cldma%d_gpd_pool_%s",
		 hw_id, mdev->dev_str);
	snprintf(bd_pool_name, DMA_POOL_NAME_LEN, "cldma%d_bd_pool_%s",
		 hw_id, mdev->dev_str);
	drv_info->gpd_dma_pool = dma_pool_create(gpd_pool_name, mdev->dev,
						 sizeof(union gpd), L1_CACHE_BYTES, 0);
	if (!drv_info->gpd_dma_pool) {
		MTK_ERR(mdev, "Failed to alloc gpd dma pool for cldma%d\n", hw_id);
		goto err_free_drv_info;
	}
	drv_info->bd_dma_pool = dma_pool_create(bd_pool_name, mdev->dev,
						sizeof(union bd), L1_CACHE_BYTES, 0);
	if (!drv_info->bd_dma_pool) {
		MTK_ERR(mdev, "Failed to alloc bd dma pool for cldma%d\n", hw_id);
		goto err_destroy_gpd_pool;
	}

	switch (hif_id) {
	case CLDMA0:
		drv_info->pci_ext_irq_id = mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_CLDMA0);
		drv_info->base_addr = hw_regs->cldma0_base_addr;
		break;
	case CLDMA1:
		drv_info->pci_ext_irq_id = mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_CLDMA1);
		drv_info->base_addr = hw_regs->cldma1_base_addr;
		break;
	case CLDMA4:
		drv_info->pci_ext_irq_id = mtk_pci_get_irq_id(mdev, MTK_IRQ_SRC_CLDMA4);
		drv_info->base_addr = hw_regs->cldma4_base_addr;
		break;
	default:
		goto err_destroy_dma_pool;
	}

	flag = WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI;
	drv_info->wq = alloc_workqueue("cldma%d_workq_%s", flag, 0, hw_id, mdev->dev_str);
	if (!drv_info->wq) {
		MTK_ERR(mdev, "Failed to alloc work queue for cldma%d\n", hw_id);
		goto err_destroy_dma_pool;
	}

	ret = mtk_stats_register_cb(mdev,
				    mdev->utility_cfg->stats_cfg->ctrl_type_base +
				    (hif_id * DIR_MAX) + DIR_TX,
				    CLDMA_STATS_PERIOD_S, mtk_stats_cldma_tx_cb,
				    drv_info);
	if (ret) {
		MTK_ERR(mdev, "Failed to register stats for cldma%d tx\n", hw_id);
		goto err_destroy_workqueue;
	}
	ret = mtk_stats_register_cb(mdev,
				    mdev->utility_cfg->stats_cfg->ctrl_type_base +
				    (hif_id * DIR_MAX) + DIR_RX,
				    CLDMA_STATS_PERIOD_S, mtk_stats_cldma_rx_cb,
				    drv_info);
	if (ret) {
		MTK_ERR(mdev, "Failed to register stats for cldma%d rx\n", hw_id);
		goto err_unregister_stats_tx;
	}

	drv_info->drv_ops->cldma_drv_init(drv_info);

	/* mask/clear PCI CLDMA L1 interrupt */
	mtk_pci_mask_irq(mdev, drv_info->pci_ext_irq_id);
	mtk_pci_clear_irq(mdev, drv_info->pci_ext_irq_id);

	/* register CLDMA interrupt handler */
	mtk_pci_register_irq(mdev, drv_info->pci_ext_irq_id, mtk_cldma_isr, drv_info);

	/* unmask PCI CLDMA L1 interrupt */
	mtk_pci_unmask_irq(mdev, drv_info->pci_ext_irq_id);

	cd->cldma_drv_info[hif_id] = drv_info;
	MTK_INFO(mdev, "CLDMA%d init done\n", hw_id);
	return 0;

err_unregister_stats_tx:
	mtk_stats_unregister_cb(mdev,
				mdev->utility_cfg->stats_cfg->ctrl_type_base +
				(hif_id * 2) + DIR_TX);
err_destroy_workqueue:
	destroy_workqueue(drv_info->wq);
err_destroy_dma_pool:
	dma_pool_destroy(drv_info->bd_dma_pool);
err_destroy_gpd_pool:
	dma_pool_destroy(drv_info->gpd_dma_pool);
err_free_drv_info:
	devm_kfree(mdev->dev, drv_info);

	return -EIO;
}

static inline void mtk_cldma_clr_bd_dsc(struct cldma_drv_info *drv_info,
					struct bd_dsc *bd_dsc_pool, int nr_bds)
{
	struct bd_dsc *bd_dsc;
	int i;

	for (i = 0; i < nr_bds; i++) {
		bd_dsc = bd_dsc_pool + i;
		dma_unmap_single(drv_info->mdev->dev, bd_dsc->data_dma_addr,
				 bd_dsc->data_len, DMA_TO_DEVICE);
		bd_dsc->data_dma_addr = 0;
		bd_dsc->data_len = 0;
		if (bd_dsc->bd->tx_bd.bd_flags & CLDMA_BD_FLAG_EOL) {
			bd_dsc->bd->tx_bd.bd_flags &= ~CLDMA_BD_FLAG_EOL;
			break;
		}
	}
}

static int mtk_cldma_check_tx_hwo(struct txq *txq, struct tx_req *req)
{
	struct cldma_drv_info *drv_info = txq->drv_info;
	struct mtk_md_dev *mdev = drv_info->mdev;
	int cnt, ret = 0;

	for (cnt = 0; cnt < WAIT_HWO_ROUND; cnt++) {
		if (!(req->gpd->tx_gpd.gpd_flags & CLDMA_GPD_FLAG_HWO))
			break;
		udelay(WAIT_HWO_TIME);
	}

	if (cnt == WAIT_HWO_ROUND) {
		MTK_ERR(mdev, "Failed to check cldma%d txq%d gpd%d HWO=0\n",
			drv_info->hw_id, txq->txqno, txq->free_idx);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		radio_google_cldma_error_handler(mdev->google, EXCP_REASON_CLDMA_TX_HWO_ERROR);
#endif
		mtk_cldma_trigger_dev_ee(mdev, drv_info->hif_id);
		ret = -EAGAIN;
	} else if (cnt) {
		drv_info->stats.cldma_tx.hwo_delay_detected_cnt[txq->txqno]++;
	}

	return ret;
}

static int mtk_cldma_check_tx_req(struct cldma_drv_info *drv_info, struct txq *txq)
{
	struct tx_req *req = txq->req_pool + txq->free_idx;
	u64 curr_addr;
	int ret;

	curr_addr = drv_info->drv_ops->cldma_get_curr_addr(drv_info, DIR_TX, txq->txqno);
	if (unlikely(!curr_addr))
		return -ENXIO;

	if (req->gpd_dma_addr == curr_addr)
		return -EAGAIN;

	ret = mtk_cldma_check_tx_hwo(txq, req);

	return ret;
}

void mtk_cldma_tx_done_work(struct work_struct *work)
{
	struct txq *txq = container_of(work, struct txq, tx_done_work);
	struct cldma_traffic_tx *stats_tx;
	struct cldma_drv_info *drv_info;
	struct cldma_drv_ops *drv_ops;
	struct mtk_ctrl_trans *trans;
	struct mtk_md_dev *mdev;
	struct tx_req *req;
	int i, err, hif_id;
	struct trb *trb;
	u8 exit_flag;
	u32 txqno;
	u32 state;

	drv_info = txq->drv_info;
	hif_id = drv_info->hif_id;
	txqno = txq->txqno;
	mdev = drv_info->mdev;
	drv_ops = drv_info->drv_ops;
	stats_tx = &drv_info->stats.cldma_tx;
	stats_tx->tx_done_last_cnt[txqno] = 0;
	trans = drv_info->cd->trans;

	mtk_pm_runtime_get(mdev, MTK_USER_CTRL, false);
	mtk_pm_ds_lock(mdev, MTK_USER_CTRL);
again:
	exit_flag = TX_DONE_EXIT_INIT;
	for (i = 0; i < txq->nr_gpds; i++) {
		req = txq->req_pool + txq->free_idx;

		mutex_lock(&txq->tx_mtx);
		if (!req->data_vm_addr) {
			mutex_unlock(&txq->tx_mtx);
			exit_flag = TX_DONE_EXIT_NO_VMA;
			break;
		} else if (req->gpd->tx_gpd.gpd_flags & CLDMA_GPD_FLAG_HWO) {
			mutex_unlock(&txq->tx_mtx);
			exit_flag = TX_DONE_EXIT_HW_OWN;
			break;
		}
		mutex_unlock(&txq->tx_mtx);

		if (txq->nr_bds)
			mtk_cldma_clr_bd_dsc(drv_info, req->bd_dsc_pool, txq->nr_bds);
		else
			dma_unmap_single(mdev->dev, req->data_dma_addr,
					 req->data_len, DMA_TO_DEVICE);

		trb = (struct trb *)req->skb->cb;
		trb->status = 0;
		trace_mtk_ctrl_tx_done(txq->drv_info->hw_id, txqno,
				       req->skb, req->skb->data, i);
		trb->trb_complete(req->skb);

		req->data_vm_addr = NULL;
		req->data_dma_addr = 0;
		req->data_len = 0;
		req->skb = NULL;

		txq->free_idx = (txq->free_idx + 1) % txq->nr_gpds;
		if (atomic_fetch_inc(&txq->req_budget) == NO_BUDGET)
			wake_up(&trans->trb_srv[trans->srv_cfg[hif_id][txqno]]->trb_waitq);
	}
	stats_tx->tx_hw_pkt[txqno] += i;
	stats_tx->tx_done_last_cnt[txqno] += i;

	MTK_DBG_CTRL_TX_DONE(mdev, drv_info->hw_id, txqno, txq->wr_idx, txq->free_idx,
			     i, atomic_read(&txq->req_budget), exit_flag,
			     txq->que->log_rg_offset);
	mtk_stats_chk_and_proc(mdev, BIT(mdev->utility_cfg->stats_cfg->ctrl_type_base +
					 (hif_id * 2) + DIR_TX));

	err = mtk_pm_ds_wait_complete(mdev, MTK_USER_CTRL);
	if (unlikely(err)) {
		MTK_ERR(mdev, "Failed to lock ds:%d tx_done\n", err);
		goto out;
	}
	err = mtk_cldma_check_tx_req(drv_info, txq);
	if (!err)
		goto again;
	else if (err == -ENXIO)
		goto out;

	state = drv_ops->cldma_check_intr_status(drv_info, DIR_TX, txqno, QUEUE_XFER_DONE);
	if (state) {
		if (unlikely(state == LINK_ERROR_VAL)) {
			cldma_access_error(drv_info->cd);
			goto out;
		}

		drv_ops->cldma_clr_intr_status(drv_info, DIR_TX, txqno, QUEUE_XFER_DONE);

		cond_resched();

		goto again;
	}

out:
	if (likely(cldma_hw_is_accessible(drv_info->cd)))
		drv_ops->cldma_unmask_intr(drv_info, DIR_TX, txqno, QUEUE_XFER_DONE);
	mtk_pm_ds_unlock_instant(mdev, MTK_USER_CTRL);
	mtk_pm_runtime_put(mdev, MTK_USER_CTRL, false);
}

static void mtk_cldma_rx_skb_adjust(struct mtk_md_dev *mdev, struct rxq *rxq,
				    struct rx_req *req)
{
	struct bd_dsc *bd_dsc;
	int i;

	for (i = 0; i < rxq->nr_bds; i++) {
		bd_dsc = req->bd_dsc_pool + i;
		if (bd_dsc->data_dma_addr) {
			dma_unmap_single(mdev->dev, bd_dsc->data_dma_addr,
					 req->frag_size, DMA_FROM_DEVICE);
			bd_dsc->data_dma_addr = 0;
		}
		bd_dsc->skb->len = 0;
		skb_reset_tail_pointer(bd_dsc->skb);
		skb_put(bd_dsc->skb,
			le16_to_cpu(bd_dsc->bd->rx_bd.data_recv_len));
		if (req->skb != bd_dsc->skb) {
			req->skb->len += bd_dsc->skb->len;
			req->skb->data_len += bd_dsc->skb->len;
		}
		bd_dsc->bd->rx_bd.data_recv_len = 0;
		bd_dsc->skb = NULL;
	}
	if (!rxq->nr_bds) {
		if (req->data_dma_addr) {
			dma_unmap_single(mdev->dev, req->data_dma_addr,
					 req->mtu, DMA_FROM_DEVICE);
			req->data_dma_addr = 0;
		}
		req->skb->len = 0;
		skb_reset_tail_pointer(req->skb);
		skb_put(req->skb, le16_to_cpu(req->gpd->rx_gpd.data_recv_len));
	}

	req->gpd->rx_gpd.data_recv_len = 0;
}

static void mtk_cldma_rx_skb_put(struct rxq *rxq, struct rx_req *req)
{
	struct bd_dsc *bd_dsc;
	int i;

	if (!req->gpd->rx_gpd.data_recv_len)
		return;

	for (i = 0; i < rxq->nr_bds; i++) {
		bd_dsc = req->bd_dsc_pool + i;
		bd_dsc->skb->len = 0;
		skb_reset_tail_pointer(bd_dsc->skb);
		skb_put(bd_dsc->skb,
			le16_to_cpu(bd_dsc->bd->rx_bd.data_recv_len));
		if (req->skb != bd_dsc->skb) {
			req->skb->len += bd_dsc->skb->len;
			req->skb->data_len += bd_dsc->skb->len;
		}
		bd_dsc->bd->rx_bd.data_recv_len = 0;
		bd_dsc->skb = NULL;
	}
	if (!rxq->nr_bds) {
		req->skb->len = 0;
		skb_reset_tail_pointer(req->skb);
		skb_put(req->skb, le16_to_cpu(req->gpd->rx_gpd.data_recv_len));
	}

	req->gpd->rx_gpd.data_recv_len = 0;
}

static void mtk_cldma_rx_skb_unmap(struct mtk_md_dev *mdev, struct rxq *rxq,
				   struct rx_req *req)
{
	struct bd_dsc *bd_dsc;
	int i;

	for (i = 0; i < rxq->nr_bds; i++) {
		bd_dsc = req->bd_dsc_pool + i;
		if (bd_dsc->data_dma_addr) {
			dma_unmap_single(mdev->dev, bd_dsc->data_dma_addr,
					 req->frag_size, DMA_FROM_DEVICE);
			bd_dsc->data_dma_addr = 0;
		}
	}
	if (!rxq->nr_bds) {
		if (req->data_dma_addr) {
			dma_unmap_single(mdev->dev, req->data_dma_addr,
					 req->mtu, DMA_FROM_DEVICE);
			req->data_dma_addr = 0;
		}
	}
}

static int mtk_cldma_reload_rx_skb(struct mtk_md_dev *mdev, struct rxq *rxq,
				   struct mtk_bm_pool *bm_pool, struct rx_req *req)
{
	struct sk_buff *tail = NULL;
	struct bd_dsc *bd_dsc;
	int nr_bds;
	int i, err;

	nr_bds = rxq->nr_bds;

	for (i = 0; i < nr_bds; i++) {
		bd_dsc = req->bd_dsc_pool + i;
		bd_dsc->skb = mtk_bm_alloc(bm_pool, CLDMA_RETRY_DELAY_MS);
		if (!bd_dsc->skb) {
			MTK_WARN(mdev, "Failed to alloc SKB\n");
			err = -ENOMEM;
			goto err_free_skb;
		}
		bd_dsc->skb->next = NULL;
		bd_dsc->data_dma_addr = dma_map_single(mdev->dev, bd_dsc->skb->data,
						       req->frag_size, DMA_FROM_DEVICE);
		err = dma_mapping_error(mdev->dev, bd_dsc->data_dma_addr);
		if (unlikely(err)) {
			MTK_WARN(mdev, "Failed to map SKB data\n");
			err = -EFAULT;
			goto err_free_skb;
		}
		bd_dsc->bd->rx_bd.data_buff_ptr_h =
			cpu_to_le32((u64)(bd_dsc->data_dma_addr) >> 32);
		bd_dsc->bd->rx_bd.data_buff_ptr_l =
			cpu_to_le32(bd_dsc->data_dma_addr);
		if (tail) {
			tail->next = bd_dsc->skb;
			tail = bd_dsc->skb;
			continue;
		}
		if (!req->skb) {
			req->skb = bd_dsc->skb;
		} else {
			skb_shinfo(req->skb)->frag_list = bd_dsc->skb;
			tail = bd_dsc->skb;
		}
	}
	if (!nr_bds) {
		req->skb = mtk_bm_alloc(bm_pool, CLDMA_RETRY_DELAY_MS);
		if (!req->skb) {
			MTK_WARN(mdev, "Failed to alloc SKB\n");
			err = -ENOMEM;
			goto err_free_skb;
		}

		req->data_dma_addr = dma_map_single(mdev->dev, req->skb->data,
						    req->mtu, DMA_FROM_DEVICE);
		err = dma_mapping_error(mdev->dev, req->data_dma_addr);
		if (unlikely(err)) {
			MTK_WARN(mdev, "Failed to map SKB data\n");
			err = -EFAULT;
			goto err_free_skb;
		}
		req->gpd->rx_gpd.data_buff_ptr_h = cpu_to_le32((u64)req->data_dma_addr >> 32);
		req->gpd->rx_gpd.data_buff_ptr_l = cpu_to_le32(req->data_dma_addr);
	}
	return 0;

err_free_skb:
	if (nr_bds) {
		if (req->skb)
			skb_shinfo(req->skb)->frag_list = NULL;
		for (i = 0; i < nr_bds; i++) {
			bd_dsc = req->bd_dsc_pool + i;
			if (!bd_dsc->skb)
				break;
			if (!dma_mapping_error(mdev->dev, bd_dsc->data_dma_addr))
				dma_unmap_single(mdev->dev, bd_dsc->data_dma_addr,
						 req->frag_size, DMA_FROM_DEVICE);
			bd_dsc->data_dma_addr = 0;
			bd_dsc->skb->next = NULL;
			mtk_bm_free(bm_pool, bd_dsc->skb);
		}
	} else {
		req->data_dma_addr = 0;
		if (req->skb)
			mtk_bm_free(bm_pool, req->skb);
	}
	req->skb = NULL;

	return err;
}

static int mtk_cldma_check_rx_hwo(struct rxq *rxq, struct rx_req *req)
{
	struct cldma_drv_info *drv_info = rxq->drv_info;
	struct mtk_md_dev *mdev = drv_info->mdev;
	int cnt, ret = 0;

	for (cnt = 0; cnt < WAIT_HWO_ROUND; cnt++) {
		if (!(req->gpd->rx_gpd.gpd_flags & CLDMA_GPD_FLAG_HWO))
			break;
		udelay(WAIT_HWO_TIME);
	}

	if (cnt == WAIT_HWO_ROUND) {
		MTK_ERR(mdev, "Failed to check cldma%d rxq%d gpd%d HWO=0 pre:%u curr:%u\n",
			drv_info->hw_id, rxq->rxqno, rxq->free_idx, rxq->pre_cnt, rxq->curr_cnt);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		radio_google_cldma_error_handler(mdev->google, EXCP_REASON_CLDMA_RX_HWO_ERROR);
#endif
		mtk_cldma_trigger_dev_ee(mdev, drv_info->hif_id);
		ret = -EAGAIN;
	} else if (cnt) {
		drv_info->stats.cldma_rx.hwo_delay_detected_cnt[rxq->rxqno]++;
	}

	return ret;
}

static int mtk_cldma_check_rx_req(struct cldma_drv_info *drv_info, struct rxq *rxq)
{
	struct rx_req *req = rxq->req_pool + rxq->free_idx;
	u64 curr_addr;
	int ret;

	curr_addr = drv_info->drv_ops->cldma_get_curr_addr(drv_info, DIR_RX, rxq->rxqno);
	if (unlikely(!curr_addr))
		return -ENXIO;

	if (req->gpd_dma_addr == curr_addr)
		return -EAGAIN;

	ret = mtk_cldma_check_rx_hwo(rxq, req);

	return ret;
}

static bool mtk_cldma_rx_check_intr_again(struct rxq *rxq)
{
	struct cldma_drv_info *drv_info;
	struct rx_freq_stat *freq_stat;
	struct cldma_drv_ops *drv_ops;
	bool need_check_again = false;
	bool has_waited = false;
	struct mtk_md_dev *mdev;
	u32 rxqno;
	u32 state;

	drv_info = rxq->drv_info;
	drv_ops = drv_info->drv_ops;
	mdev = drv_info->mdev;
	rxqno = rxq->rxqno;
	freq_stat = &rxq->freq_stat;

	do {
		state = drv_ops->cldma_check_intr_status(drv_info, DIR_RX,
							 rxqno, QUEUE_XFER_DONE);
		if (state) {
			if (unlikely(state == LINK_ERROR_VAL))
				break;

			drv_ops->cldma_clr_intr_status(drv_info, DIR_RX,
						       rxqno, QUEUE_XFER_DONE);
			cond_resched();
			return true;
		} else if (freq_stat->stat_cnt >= RX_FREQ_STAT_CNT &&
			   freq_stat->avg_time_gap < TIME_GAP_THRESHOLD_NS) {
			if (!has_waited)
				usleep_range(WAIT_RX_INTR_TIME_US, 2 * WAIT_RX_INTR_TIME_US);
			need_check_again = has_waited ? false : true;
			has_waited = true;
		}
	} while (need_check_again);

	return false;
}

void mtk_cldma_rx_done_work(struct work_struct *work)
{
	struct rxq *rxq = container_of(work, struct rxq, rx_done_work);
	struct rx_req *req = NULL, *pre_req = NULL;
	struct cldma_traffic_rx *stats_rx;
	struct cldma_drv_info *drv_info;
	struct cldma_drv_ops *drv_ops;
	struct mtk_bm_pool *bm_pool;
	struct mtk_md_dev *mdev;
	int i, err, idx;

	drv_info = rxq->drv_info;
	mdev = drv_info->mdev;
	drv_ops = drv_info->drv_ops;
	stats_rx = &drv_info->stats.cldma_rx;
	stats_rx->rx_done_last_cnt[rxq->rxqno] = 0;

	if (rxq->que->rx_frag_size > Q_FRAG_3_5K)
		bm_pool = drv_info->cd->trans->ctrl_blk->bm_pool_63K;
	else
		bm_pool = drv_info->cd->trans->ctrl_blk->bm_pool;

	mtk_pm_runtime_get(mdev, MTK_USER_CTRL, false);
again:
	for (i = 0; i < rxq->nr_gpds; i++) {
		req = rxq->req_pool + rxq->free_idx;
		if (!req->skb) {
			MTK_ERR(mdev, "Failed to get valid req cldma%d rxq%d req%d\n",
				drv_info->hw_id, rxq->rxqno, rxq->free_idx);
			goto err_out;
		}

		if (req->gpd->rx_gpd.gpd_flags & CLDMA_GPD_FLAG_HWO)
			break;

		mtk_cldma_rx_skb_adjust(mdev, rxq, req);
		trace_mtk_ctrl_rx_done(rxq->drv_info->hw_id, rxq->rxqno,
				       req->skb, req->skb->data, i);
		do {
#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
			if (md2ap_wakemon_cldma_rx_check(mdev->google, drv_info->hw_id)) {
				md2ap_wakemon_cldma_rx(mdev->google, rxq->rxqno,
						       mtk_google_resolve_skb_port(req->skb,
										   rxq->arg));
			}
#endif
			err = rxq->rx_done(req->skb, rxq->arg,
					   atomic_read(&rxq->need_exit) ? true : false);
			if (err == -EAGAIN)
				usleep_range(1000, 2000);
			else
				req->skb = NULL;
		} while (err == -EAGAIN);

		err = mtk_cldma_reload_rx_skb(mdev, rxq, bm_pool, req);
		if (err)
			goto err_out;

		wmb(); /* ensure addr set done before HWO setup done  */

		idx = rxq->free_idx == 0 ? rxq->nr_gpds - 1 : rxq->free_idx - 1;
		pre_req = rxq->req_pool + idx;
		pre_req->gpd->rx_gpd.gpd_flags |= CLDMA_GPD_FLAG_HWO;
		rxq->free_idx = (rxq->free_idx + 1) % rxq->nr_gpds;
	}
	stats_rx->rx_pkt[rxq->rxqno] += i;
	stats_rx->rx_done_last_cnt[rxq->rxqno] += i;

	MTK_DBG_CTRL_RX_DONE(mdev, drv_info->hw_id, rxq->rxqno,
			     rxq->free_idx, i, rxq->que->log_rg_offset);
	mtk_stats_chk_and_proc(mdev, BIT(mdev->utility_cfg->stats_cfg->ctrl_type_base +
					 (drv_info->hif_id * 2) + DIR_RX));

	mtk_pm_ds_lock(mdev, MTK_USER_CTRL);
	err = mtk_pm_ds_wait_complete(mdev, MTK_USER_CTRL);
	if (unlikely(err)) {
		MTK_ERR(mdev, "Failed to lock ds:%d rx_done\n", err);
		goto out;
	}

	err = mtk_cldma_check_rx_req(drv_info, rxq);
	if (!err) {
		mtk_pm_ds_unlock_instant(mdev, MTK_USER_CTRL);
		goto again;
	} else if (err == -ENXIO) {
		goto out;
	}

	if (!atomic_read(&rxq->need_exit))
		drv_ops->cldma_resume_queue(drv_info, DIR_RX, rxq->rxqno);

	if (mtk_cldma_rx_check_intr_again(rxq)) {
		mtk_pm_ds_unlock_instant(mdev, MTK_USER_CTRL);
		goto again;
	}

out:
	if (likely(cldma_hw_is_accessible(drv_info->cd))) {
		drv_ops->cldma_unmask_intr(drv_info, DIR_RX, rxq->rxqno, QUEUE_XFER_DONE);
		drv_ops->cldma_clear_ip_busy(drv_info);
	}
	mtk_pm_ds_unlock_instant(mdev, MTK_USER_CTRL);
err_out:
	mtk_pm_runtime_put(mdev, MTK_USER_CTRL, false);
	__pm_wakeup_event(rxq->ws, mtk_ctrl_keep_wake_time_ms);
}

static int mtk_cldma_get_rx_pending_cnt(struct rxq *rxq)
{
	struct cldma_drv_info *drv_info = rxq->drv_info;
	struct mtk_md_dev *mdev = drv_info->mdev;
	int cnt = 0, ret = 0;

	if (unlikely(rxq->curr_cnt == LINK_ERROR_VAL)) {
		cldma_access_error(drv_info->cd);
		return -EIO;
	} else if (unlikely(rxq->curr_cnt == U16_MAX)) {
		ret = -EIO;
	} else {
		cnt = (rxq->curr_cnt + U16_MAX - rxq->pre_cnt) % U16_MAX;
	}

	if (unlikely(cnt > rxq->nr_gpds - 1))
		ret = -EIO;

	if (ret) {
		MTK_ERR(mdev, "Failed to get cldma%d rxq%d pending cnt pre:0x%08x curr:0x%08x\n",
			drv_info->hw_id, rxq->rxqno, rxq->pre_cnt, rxq->curr_cnt);
#if IS_ENABLED(CONFIG_ARCH_GOOGLE)
		radio_google_cldma_error_handler(mdev->google, EXCP_REASON_CLDMA_ERROR);
#endif
		mtk_cldma_trigger_dev_ee(mdev, drv_info->hif_id);
		return ret;
	}

	return cnt;
}

static bool mtk_cldma_rx_check_cnt_again(struct rxq *rxq)
{
	struct cldma_drv_info *drv_info;
	struct rx_freq_stat *freq_stat;
	struct cldma_drv_ops *drv_ops;
	bool need_check_again = false;
	bool has_waited = false;
	u32 rxqno, val;

	drv_info = rxq->drv_info;
	drv_ops = drv_info->drv_ops;
	rxqno = rxq->rxqno;
	freq_stat = &rxq->freq_stat;

	do {
		drv_ops->cldma_clr_intr_status(drv_info, DIR_RX, rxqno, QUEUE_XFER_DONE);
		val = drv_ops->cldma_get_gpd_cnt(drv_info, DIR_RX, rxqno);
		if (val != rxq->pre_cnt) {
			rxq->curr_cnt = val;
			return true;
		}

		if (freq_stat->stat_cnt >= RX_FREQ_STAT_CNT &&
		    freq_stat->avg_time_gap < TIME_GAP_THRESHOLD_NS) {
			if (!has_waited)
				usleep_range(WAIT_RX_INTR_TIME_US, 2 * WAIT_RX_INTR_TIME_US);
			need_check_again = has_waited ? false : true;
			has_waited = true;
		}
	} while (need_check_again);

	return false;
}

void mtk_cldma_rx_done_work_optimize(struct work_struct *work)
{
	struct rxq *rxq = container_of(work, struct rxq, rx_done_work);
	struct rx_req *req = NULL, *pre_req = NULL;
	struct cldma_traffic_rx *stats_rx;
	struct cldma_drv_info *drv_info;
	struct cldma_drv_ops *drv_ops;
	bool need_queue_work = false;
	struct mtk_bm_pool *bm_pool;
	int i, err, idx, ret, cnt;
	struct mtk_md_dev *mdev;
	u32 polling_round = 0;
	int retry_cnt;

	drv_info = rxq->drv_info;
	mdev = drv_info->mdev;
	drv_ops = drv_info->drv_ops;
	stats_rx = &drv_info->stats.cldma_rx;
	stats_rx->rx_done_last_cnt[rxq->rxqno] = 0;

	if (rxq->que->rx_frag_size > Q_FRAG_3_5K)
		bm_pool = drv_info->cd->trans->ctrl_blk->bm_pool_63K;
	else
		bm_pool = drv_info->cd->trans->ctrl_blk->bm_pool;

	mtk_pm_runtime_get(mdev, MTK_USER_CTRL, false);
again:
	cnt = mtk_cldma_get_rx_pending_cnt(rxq);
	if (unlikely(cnt < 0))
		goto err_out;

	for (i = 0; i < cnt; i++) {
		req = rxq->req_pool + rxq->free_idx;
		if (!req->skb) {
			MTK_ERR(mdev, "Failed to get valid req cldma%d rxq%d req%d\n",
				drv_info->hw_id, rxq->rxqno, rxq->free_idx);
			goto err_out;
		}

		ret = mtk_cldma_check_rx_hwo(rxq, req);
		if (ret)
			goto err_out;

		mtk_cldma_rx_skb_put(rxq, req);
		retry_cnt = 0;
		do {
			if (rxq->nr_bds)
				dma_sync_single_for_cpu(mdev->dev,
							req->bd_dsc_pool[0].data_dma_addr,
							req->frag_size, DMA_FROM_DEVICE);
			else
				dma_sync_single_for_cpu(mdev->dev, req->data_dma_addr,
							req->mtu, DMA_FROM_DEVICE);
			err = mtk_port_rx_pre_check(req->skb, rxq->arg);
			if (likely(err != -EAGAIN))
				break;
			udelay(PRE_CHECK_INTERVAL_US);
		} while (retry_cnt++ < PRE_CHECK_MAX_CNT);
		mtk_cldma_rx_skb_unmap(mdev, rxq, req);
		trace_mtk_ctrl_rx_done(rxq->drv_info->hw_id, rxq->rxqno,
				       req->skb, req->skb->data, i);
		do {
#if IS_ENABLED(CONFIG_GOOGLE_MD2AP_WAKEUP_MONITOR)
			if (md2ap_wakemon_cldma_rx_check(mdev->google, drv_info->hw_id)) {
				md2ap_wakemon_cldma_rx(mdev->google, rxq->rxqno,
						       mtk_google_resolve_skb_port(req->skb,
										   rxq->arg));
			}
#endif
			err = rxq->rx_done(req->skb, rxq->arg,
					   atomic_read(&rxq->need_exit) ? true : false);
			if (err == -EAGAIN)
				usleep_range(1000, 2000);
			else
				req->skb = NULL;
		} while (err == -EAGAIN);

		err = mtk_cldma_reload_rx_skb(mdev, rxq, bm_pool, req);
		if (err)
			goto err_out;

		wmb(); /* ensure addr set done before HWO setup done  */

		idx = rxq->free_idx == 0 ? rxq->nr_gpds - 1 : rxq->free_idx - 1;
		pre_req = rxq->req_pool + idx;
		pre_req->gpd->rx_gpd.gpd_flags |= CLDMA_GPD_FLAG_HWO;
		rxq->free_idx = (rxq->free_idx + 1) % rxq->nr_gpds;
	}
	rxq->pre_cnt = (rxq->pre_cnt + i) % U16_MAX;
	stats_rx->rx_pkt[rxq->rxqno] += i;
	stats_rx->rx_done_last_cnt[rxq->rxqno] += i;

	MTK_DBG_CTRL_RX_DONE(mdev, drv_info->hw_id, rxq->rxqno,
			     rxq->free_idx, i, rxq->que->log_rg_offset);
	mtk_stats_chk_and_proc(mdev, BIT(mdev->utility_cfg->stats_cfg->ctrl_type_base +
					 (drv_info->hif_id * 2) + DIR_RX));

	mtk_pm_ds_lock(mdev, MTK_USER_CTRL);
	err = mtk_pm_ds_wait_complete(mdev, MTK_USER_CTRL);
	if (unlikely(err)) {
		MTK_ERR(mdev, "Failed to lock ds:%d rx_done\n", err);
		goto out;
	}

	if (!atomic_read(&rxq->need_exit))
		drv_ops->cldma_resume_queue(drv_info, DIR_RX, rxq->rxqno);

	if (mtk_cldma_rx_check_cnt_again(rxq)) {
		if (!mutex_trylock(&drv_info->q_exit_mtx)) {
			mtk_pm_ds_unlock_instant(mdev, MTK_USER_CTRL);
			goto again;
		}
		if (polling_round++ < MAX_POLLING_ROUND) {
			mutex_unlock(&drv_info->q_exit_mtx);
			mtk_pm_ds_unlock_instant(mdev, MTK_USER_CTRL);
			goto again;
		} else {
			need_queue_work = true;
			queue_work(drv_info->wq, &rxq->rx_done_work);
		}
		mutex_unlock(&drv_info->q_exit_mtx);
	}

out:
	if (likely(cldma_hw_is_accessible(drv_info->cd))) {
		if (!need_queue_work)
			drv_ops->cldma_unmask_intr(drv_info, DIR_RX, rxq->rxqno, QUEUE_XFER_DONE);
		drv_ops->cldma_clear_ip_busy(drv_info);
	}
	mtk_pm_ds_unlock_instant(mdev, MTK_USER_CTRL);
err_out:
	mtk_pm_runtime_put(mdev, MTK_USER_CTRL, false);
	__pm_wakeup_event(rxq->ws, mtk_ctrl_keep_wake_time_ms);
}

static int mtk_cldma_alloc_tx_bd(struct cldma_drv_info *drv_info, struct txq *txq,
				 struct tx_req *req)
{
	struct bd_dsc *bd_dsc, *last_bd_dsc = NULL;
	int i;

	req->bd_dsc_pool = devm_kcalloc(drv_info->mdev->dev, txq->nr_bds,
					sizeof(*bd_dsc), GFP_KERNEL);
	if (!req->bd_dsc_pool)
		return -ENOMEM;

	for (i = 0; i < txq->nr_bds; i++) {
		bd_dsc = req->bd_dsc_pool + i;
		bd_dsc->bd = dma_pool_zalloc(drv_info->bd_dma_pool, GFP_KERNEL,
					     &bd_dsc->bd_dma_addr);
		if (!bd_dsc->bd)
			return -ENOMEM;
		if (!last_bd_dsc) {
			req->gpd->tx_gpd.data_buff_ptr_h =
				cpu_to_le32((u64)(bd_dsc->bd_dma_addr) >> 32);
			req->gpd->tx_gpd.data_buff_ptr_l =
				cpu_to_le32(bd_dsc->bd_dma_addr);
		} else {
			last_bd_dsc->bd->tx_bd.next_bd_ptr_h =
				cpu_to_le32((u64)(bd_dsc->bd_dma_addr) >> 32);
			last_bd_dsc->bd->tx_bd.next_bd_ptr_l =
				cpu_to_le32(bd_dsc->bd_dma_addr);
		}
		last_bd_dsc = bd_dsc;
	}
	return 0;
}

static struct txq *mtk_cldma_txq_alloc(struct cldma_drv_info *drv_info, struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct cldma_drv_ops *drv_ops;
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct mtk_md_dev *mdev;
	struct bd_dsc *bd_dsc;
	struct tx_req *next;
	struct tx_req *req;
	u16 tx_frag_size;
	struct txq *txq;
	int i, j, err;

	mdev = drv_info->mdev;
	ctrl_blk = mdev->ctrl_blk;
	trans = ctrl_blk->ctrl_hw_priv;
	drv_ops = drv_info->drv_ops;

	txq = devm_kzalloc(mdev->dev, sizeof(*txq), GFP_KERNEL);
	if (!txq) {
		MTK_ERR(drv_info->mdev, "Failed to allocate txq for cldma%d\n", drv_info->hw_id);
		return NULL;
	}

	mutex_init(&txq->tx_mtx);
	txq->que = radix_tree_lookup(&trans->queue_tbl, trb->channel_id & 0xFFFF);
	txq->drv_info = drv_info;
	txq->txqno = txq->que->txqno;
	txq->nr_gpds = txq->que->tx_nr_gpds;
	atomic_set(&txq->req_budget, txq->que->tx_nr_gpds - 1);
	txq->is_stopping = false;
	tx_frag_size = txq->que->tx_frag_size;
	if (txq->que->tx_mtu > tx_frag_size && tx_frag_size)
		txq->nr_bds = (txq->que->tx_mtu + tx_frag_size - 1) / tx_frag_size;

	txq->req_pool = devm_kcalloc(mdev->dev, txq->nr_gpds, sizeof(*req), GFP_KERNEL);
	if (!txq->req_pool) {
		MTK_ERR(drv_info->mdev, "Failed to allocate txq req pool for cldma%d txq%d\n",
			drv_info->hw_id, txq->txqno);
		goto err_free_txq;
	}

	for (i = 0; i < txq->nr_gpds; i++) {
		req = txq->req_pool + i;
		req->mtu = txq->que->tx_mtu;
		req->frag_size = tx_frag_size;
		req->gpd = dma_pool_zalloc(drv_info->gpd_dma_pool, GFP_KERNEL, &req->gpd_dma_addr);
		if (!req->gpd) {
			MTK_ERR(drv_info->mdev, "Failed to allocate gpd for cldma%d txq%d\n",
				drv_info->hw_id, txq->txqno);
			goto err_free_req;
		}
		if (txq->nr_bds) {
			err = mtk_cldma_alloc_tx_bd(drv_info, txq, req);
			if (err) {
				MTK_ERR(drv_info->mdev,
					"Failed to allocate bd for cldma%d txq%d\n",
					drv_info->hw_id, txq->txqno);
				goto err_free_req;
			}
			req->gpd->tx_gpd.gpd_flags |= CLDMA_GPD_FLAG_BDP;
		}
	}

	for (i = 0; i < txq->nr_gpds; i++) {
		req = txq->req_pool + i;
		next = txq->req_pool + ((i + 1) % txq->nr_gpds);
		req->gpd->tx_gpd.gpd_flags |= CLDMA_GPD_FLAG_IOC;
		req->gpd->tx_gpd.next_gpd_ptr_h = cpu_to_le32((u64)(next->gpd_dma_addr) >> 32);
		req->gpd->tx_gpd.next_gpd_ptr_l = cpu_to_le32(next->gpd_dma_addr);
	}

	if (!trans->drv_cfg->tx_done_work) {
		MTK_ERR(mdev, "Failed to find drv_cfg->tx_done_work\n");
		goto err_free_req;
	}
	INIT_WORK(&txq->tx_done_work, trans->drv_cfg->tx_done_work);

	drv_ops->cldma_stop_queue(drv_info, DIR_TX, txq->txqno);
	txq->tx_started = false;
	drv_ops->cldma_setup_start_addr(drv_info, DIR_TX, txq->txqno,
					txq->req_pool[0].gpd_dma_addr);
	drv_ops->cldma_unmask_intr(drv_info, DIR_TX, txq->txqno, QUEUE_ERROR);
	drv_ops->cldma_unmask_intr(drv_info, DIR_TX, txq->txqno, QUEUE_XFER_DONE);

	drv_info->txq[txq->txqno] = txq;
	return txq;

err_free_req:
	for (i = 0; i < txq->nr_gpds; i++) {
		req = txq->req_pool + i;
		if (!req->gpd)
			break;
		if (req->bd_dsc_pool) {
			for (j = 0; j < txq->nr_bds; j++) {
				bd_dsc = req->bd_dsc_pool + j;
				if (!bd_dsc->bd)
					break;
				dma_pool_free(drv_info->bd_dma_pool, bd_dsc->bd,
					      bd_dsc->bd_dma_addr);
			}
			devm_kfree(mdev->dev, req->bd_dsc_pool);
		}
		dma_pool_free(drv_info->gpd_dma_pool, req->gpd, req->gpd_dma_addr);
	}
	devm_kfree(mdev->dev, txq->req_pool);
err_free_txq:
	devm_kfree(mdev->dev, txq);
	return NULL;
}

static int mtk_cldma_txq_free(struct cldma_drv_info *drv_info, u32 txqno)
{
	struct cldma_drv_ops *drv_ops;
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct mtk_md_dev *mdev;
	struct bd_dsc *bd_dsc;
	struct tx_req *req;
	struct txq *txq;
	struct trb *trb;
	int irq_id;
	int i, j;

	mdev = drv_info->mdev;
	ctrl_blk = mdev->ctrl_blk;
	trans = ctrl_blk->ctrl_hw_priv;
	drv_ops = drv_info->drv_ops;

	txq = drv_info->txq[txqno];
	drv_info->txq[txqno] = NULL;
	/* stop HW tx transaction */
	if (likely(cldma_hw_is_accessible(drv_info->cd)))
		drv_ops->cldma_stop_queue(drv_info, DIR_TX, txqno);
	txq->tx_started = false;

	irq_id = mtk_pci_get_virq_id(mdev, drv_info->pci_ext_irq_id);
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"cldma%d txq%d free sync irq\n", drv_info->hw_id, txq->txqno);
	synchronize_irq(irq_id);
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"cldma%d txq%d free flush work\n", drv_info->hw_id, txq->txqno);
	/* flush on-going work */
	flush_work(&txq->tx_done_work);
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"cldma%d txq%d free flush work done\n", drv_info->hw_id, txq->txqno);
	if (likely(cldma_hw_is_accessible(drv_info->cd))) {
		drv_ops->cldma_mask_intr(drv_info, DIR_TX, txqno, QUEUE_XFER_DONE);
		drv_ops->cldma_mask_intr(drv_info, DIR_TX, txqno, QUEUE_ERROR);
	}

	/* free tx req resource */
	for (i = 0; i < txq->nr_gpds; i++) {
		req = txq->req_pool + txq->free_idx;
		if (req->skb && req->data_len) {
			if (!txq->nr_bds)
				dma_unmap_single(mdev->dev, req->data_dma_addr,
						 req->data_len, DMA_TO_DEVICE);
			for (j = 0; j < txq->nr_bds; j++) {
				bd_dsc = req->bd_dsc_pool + j;
				dma_unmap_single(mdev->dev, bd_dsc->data_dma_addr,
						 bd_dsc->data_len, DMA_TO_DEVICE);
			}
			trb = (struct trb *)req->skb->cb;
			trb->status = -EPIPE;
			trb->trb_complete(req->skb);
		}
		for (j = 0; j < txq->nr_bds; j++) {
			bd_dsc = req->bd_dsc_pool + j;
			dma_pool_free(drv_info->bd_dma_pool, bd_dsc->bd,
				      bd_dsc->bd_dma_addr);
		}
		if (req->bd_dsc_pool)
			devm_kfree(mdev->dev, req->bd_dsc_pool);
		dma_pool_free(drv_info->gpd_dma_pool, req->gpd, req->gpd_dma_addr);
		txq->free_idx = (txq->free_idx + 1) % txq->nr_gpds;
	}

	devm_kfree(mdev->dev, txq->req_pool);
	devm_kfree(mdev->dev, txq);

	return 0;
}

static int mtk_cldma_alloc_rx_bd(struct cldma_drv_info *drv_info, struct rx_req *req,
				 struct mtk_bm_pool *bm_pool, int nr_bds)
{
	struct bd_dsc *bd_dsc, *last_bd_dsc = NULL;
	struct sk_buff *tail = NULL;
	struct mtk_md_dev *mdev;
	u32 left_size;
	int err;
	int i;

	mdev = drv_info->mdev;
	left_size = req->mtu;

	req->bd_dsc_pool = devm_kcalloc(mdev->dev, nr_bds,
					sizeof(*bd_dsc), GFP_KERNEL);
	if (!req->bd_dsc_pool)
		return -ENOMEM;
	for (i = 0; i < nr_bds; i++) {
		bd_dsc = req->bd_dsc_pool + i;
		bd_dsc->bd = dma_pool_zalloc(drv_info->bd_dma_pool, GFP_KERNEL,
					     &bd_dsc->bd_dma_addr);
		if (!bd_dsc->bd)
			return -ENOMEM;

		bd_dsc->skb = mtk_bm_alloc(bm_pool, CLDMA_RETRY_DELAY_MS);
		if (!bd_dsc->skb)
			return -ENOMEM;
		bd_dsc->skb->next = NULL;
		bd_dsc->data_dma_addr =
			dma_map_single(mdev->dev, bd_dsc->skb->data,
				       req->frag_size, DMA_FROM_DEVICE);
		err = dma_mapping_error(mdev->dev, bd_dsc->data_dma_addr);
		if (unlikely(err))
			return -ENOMEM;

		bd_dsc->bd->rx_bd.data_buff_ptr_h =
			cpu_to_le32((u64)(bd_dsc->data_dma_addr) >> 32);
		bd_dsc->bd->rx_bd.data_buff_ptr_l =
			cpu_to_le32(bd_dsc->data_dma_addr);
		bd_dsc->bd->rx_bd.data_allow_len =
			cpu_to_le16(min(req->frag_size, left_size));
		left_size -= min(req->frag_size, left_size);
		if (!last_bd_dsc) {
			req->gpd->rx_gpd.data_buff_ptr_h =
				cpu_to_le32((u64)(bd_dsc->bd_dma_addr) >> 32);
			req->gpd->rx_gpd.data_buff_ptr_l =
				cpu_to_le32(bd_dsc->bd_dma_addr);
		} else {
			last_bd_dsc->bd->rx_bd.next_bd_ptr_h =
				cpu_to_le32((u64)(bd_dsc->bd_dma_addr) >> 32);
			last_bd_dsc->bd->rx_bd.next_bd_ptr_l =
				cpu_to_le32(bd_dsc->bd_dma_addr);
		}
		last_bd_dsc = bd_dsc;
		if (tail) {
			tail->next = bd_dsc->skb;
			tail = bd_dsc->skb;
			continue;
		}
		if (!req->skb) {
			req->skb = bd_dsc->skb;
		} else {
			skb_shinfo(req->skb)->frag_list = bd_dsc->skb;
			tail = bd_dsc->skb;
		}
	}
	last_bd_dsc->bd->rx_bd.bd_flags |= CLDMA_BD_FLAG_EOL;
	return 0;
}

static void mtk_cldma_rxq_alloc_cancel(struct cldma_drv_info *drv_info, struct rx_req *req,
				       struct mtk_bm_pool *bm_pool, int nr_bds)
{
	struct mtk_md_dev *mdev;
	struct bd_dsc *bd_dsc;
	int i;

	mdev = drv_info->mdev;

	if (nr_bds) {
		if (req->skb)
			skb_shinfo(req->skb)->frag_list = NULL;
		if (req->bd_dsc_pool) {
			for (i = 0; i < nr_bds; i++) {
				bd_dsc = req->bd_dsc_pool + i;
				if (!bd_dsc->bd)
					break;
				if (bd_dsc->skb) {
					if (!dma_mapping_error(mdev->dev, bd_dsc->data_dma_addr))
						dma_unmap_single(mdev->dev, bd_dsc->data_dma_addr,
								 req->frag_size, DMA_FROM_DEVICE);
					bd_dsc->data_dma_addr = 0;
					bd_dsc->skb->next = NULL;
					mtk_bm_free(bm_pool, bd_dsc->skb);
				}
				dma_pool_free(drv_info->bd_dma_pool, bd_dsc->bd,
					      bd_dsc->bd_dma_addr);
			}
			devm_kfree(mdev->dev, req->bd_dsc_pool);
		}
	} else {
		if (req->skb) {
			if (!dma_mapping_error(mdev->dev, req->data_dma_addr))
				dma_unmap_single(mdev->dev, req->data_dma_addr,
						 req->mtu, DMA_FROM_DEVICE);
			req->data_dma_addr = 0;
			mtk_bm_free(bm_pool, req->skb);
		}
	}
	dma_pool_free(drv_info->gpd_dma_pool, req->gpd, req->gpd_dma_addr);
}

static struct rxq *mtk_cldma_rxq_alloc(struct cldma_drv_info *drv_info, struct sk_buff *skb)
{
	struct trb_open_priv *trb_open_priv = (struct trb_open_priv *)skb->data;
	struct trb *trb = (struct trb *)skb->cb;
	char ws_name[CLDMA_WS_NAME_LEN];
	struct cldma_drv_ops *drv_ops;
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct mtk_bm_pool *bm_pool;
	struct mtk_md_dev *mdev;
	struct rx_req *next;
	struct rx_req *req;
	u16 rx_frag_size;
	struct rxq *rxq;
	int err;
	int i;

	mdev = drv_info->mdev;
	ctrl_blk = mdev->ctrl_blk;
	trans = ctrl_blk->ctrl_hw_priv;
	drv_ops = drv_info->drv_ops;

	rxq = devm_kzalloc(mdev->dev, sizeof(*rxq), GFP_KERNEL);
	if (!rxq) {
		MTK_ERR(drv_info->mdev, "Failed to allocate rxq for cldma%d\n", drv_info->hw_id);
		return NULL;
	}

	rxq->que = radix_tree_lookup(&trans->queue_tbl, trb->channel_id & 0xFFFF);
	if (rxq->que->rx_nr_gpds < MIN_GPD_NUM) {
		MTK_ERR(mdev, "Failed to alloc cldma%d rxq%d due to gpd number < 2\n",
			drv_info->hw_id, rxq->rxqno);
		goto err_free_rxq;
	}
	rxq->drv_info = drv_info;
	rxq->rxqno = rxq->que->rxqno;
	rxq->nr_gpds = rxq->que->rx_nr_gpds;
	rxq->arg = trb->priv;
	rxq->rx_done = trb_open_priv->rx_done;
	atomic_set(&rxq->need_exit, 0);
	rx_frag_size = rxq->que->rx_frag_size;
	if (rxq->que->rx_mtu > rx_frag_size && rx_frag_size)
		rxq->nr_bds = (rxq->que->rx_mtu + rx_frag_size - 1) / rx_frag_size;

	snprintf(ws_name, CLDMA_WS_NAME_LEN, "cldma%dq%d_wakeup_source",
		 drv_info->hw_id, rxq->rxqno);
	rxq->ws = wakeup_source_register(NULL, ws_name);
	if (!rxq->ws) {
		MTK_ERR(mdev, "Failed to register cldma%d rxq%d wakeup source\n",
			drv_info->hw_id, rxq->rxqno);
		goto err_free_rxq;
	}

	rxq->req_pool = devm_kcalloc(mdev->dev, rxq->nr_gpds, sizeof(*req), GFP_KERNEL);
	if (!rxq->req_pool) {
		MTK_ERR(mdev, "Failed to alloc req_pool for cldma%d rxq%d wsu\n",
			drv_info->hw_id, rxq->rxqno);
		goto err_unregister_ws;
	}

	if (rxq->que->rx_frag_size > Q_FRAG_3_5K)
		bm_pool = ctrl_blk->bm_pool_63K;
	else
		bm_pool = ctrl_blk->bm_pool;

	/* setup rx request */
	for (i = 0; i < rxq->nr_gpds; i++) {
		req = rxq->req_pool + i;
		req->mtu = rxq->que->rx_mtu;
		req->frag_size = rx_frag_size;
		req->gpd = dma_pool_zalloc(drv_info->gpd_dma_pool, GFP_KERNEL, &req->gpd_dma_addr);
		if (!req->gpd) {
			MTK_ERR(mdev, "Failed to allocate gpd for cldma%d rxq%d\n",
				drv_info->hw_id, rxq->rxqno);
			goto err_free_req;
		}
		if (rxq->nr_bds) {
			err = mtk_cldma_alloc_rx_bd(drv_info, req, bm_pool, rxq->nr_bds);
			if (err) {
				MTK_ERR(mdev, "Failed to allocate bd for cldma%d rxq%d\n",
					drv_info->hw_id, rxq->rxqno);
				goto err_free_req;
			}
			req->gpd->rx_gpd.gpd_flags |= CLDMA_GPD_FLAG_BDP;
		} else {
			req->skb = mtk_bm_alloc(bm_pool, CLDMA_RETRY_DELAY_MS);
			if (!req->skb) {
				MTK_ERR(mdev, "Failed to allocate skb for cldma%d rxq%d\n",
					drv_info->hw_id, rxq->rxqno);
				goto err_free_req;
			}
			req->data_dma_addr = dma_map_single(mdev->dev, req->skb->data,
							    req->mtu, DMA_FROM_DEVICE);
			err = dma_mapping_error(mdev->dev, req->data_dma_addr);
			if (unlikely(err)) {
				MTK_ERR(mdev, "Failed to allocate map skb for cldma%d rxq%d\n",
					drv_info->hw_id, rxq->rxqno);
				goto err_free_req;
			}
		}
	}

	for (i = 0; i < rxq->nr_gpds; i++) {
		req = rxq->req_pool + i;
		next = rxq->req_pool + ((i + 1) % rxq->nr_gpds);
		req->gpd->rx_gpd.gpd_flags |= CLDMA_GPD_FLAG_IOC;
		req->gpd->rx_gpd.data_allow_len = cpu_to_le16(req->mtu);
		req->gpd->rx_gpd.next_gpd_ptr_h = cpu_to_le32((u64)(next->gpd_dma_addr) >> 32);
		req->gpd->rx_gpd.next_gpd_ptr_l = cpu_to_le32(next->gpd_dma_addr);
		if (!rxq->nr_bds) {
			req->gpd->rx_gpd.data_buff_ptr_h =
				cpu_to_le32((u64)(req->data_dma_addr) >> 32);
			req->gpd->rx_gpd.data_buff_ptr_l = cpu_to_le32(req->data_dma_addr);
		}
		if (i != rxq->nr_gpds - 1)
			req->gpd->rx_gpd.gpd_flags |= CLDMA_GPD_FLAG_HWO;
	}

	if (!trans->drv_cfg->rx_done_work) {
		MTK_ERR(mdev, "Failed to find drv_cfg->rx_done_work\n");
		goto err_free_req;
	}
	INIT_WORK(&rxq->rx_done_work, trans->drv_cfg->rx_done_work);

	drv_info->rxq[rxq->rxqno] = rxq;
	drv_ops->cldma_stop_queue(drv_info, DIR_RX, rxq->rxqno);
	rxq->pre_cnt = drv_ops->cldma_get_gpd_cnt(drv_info, DIR_RX, rxq->rxqno);
	if (unlikely(rxq->pre_cnt == LINK_ERROR_VAL)) {
		cldma_access_error(drv_info->cd);
		MTK_ERR(mdev, "Failed to get pre cnt for cldma%d rxq%d\n",
			drv_info->hw_id, rxq->rxqno);
		goto err_clean_drv_rxq;
	}
	rxq->curr_cnt = rxq->pre_cnt;
	drv_ops->cldma_setup_start_addr(drv_info, DIR_RX,
					rxq->rxqno, rxq->req_pool[0].gpd_dma_addr);
	drv_ops->cldma_start_queue(drv_info, DIR_RX, rxq->rxqno);
	drv_ops->cldma_unmask_intr(drv_info, DIR_RX, rxq->rxqno, QUEUE_ERROR);
	drv_ops->cldma_unmask_intr(drv_info, DIR_RX, rxq->rxqno, QUEUE_XFER_DONE);

	return rxq;

err_clean_drv_rxq:
	drv_info->rxq[rxq->rxqno] = NULL;
err_free_req:
	for (i = 0; i < rxq->nr_gpds; i++) {
		req = rxq->req_pool + i;
		if (!req->gpd)
			break;
		mtk_cldma_rxq_alloc_cancel(drv_info, req, bm_pool, rxq->nr_bds);
	}

	devm_kfree(mdev->dev, rxq->req_pool);
err_unregister_ws:
	wakeup_source_unregister(rxq->ws);
err_free_rxq:
	devm_kfree(mdev->dev, rxq);
	return NULL;
}

static int mtk_cldma_rxq_free(struct cldma_drv_info *drv_info, u32 rxqno)
{
	struct cldma_drv_ops *drv_ops;
	struct mtk_ctrl_blk *ctrl_blk;
	struct mtk_ctrl_trans *trans;
	struct mtk_bm_pool *bm_pool;
	struct mtk_md_dev *mdev;
	struct bd_dsc *bd_dsc;
	struct rx_req *req;
	struct rxq *rxq;
	int irq_id;
	int i, j;

	mdev = drv_info->mdev;
	ctrl_blk = mdev->ctrl_blk;
	trans = ctrl_blk->ctrl_hw_priv;
	drv_ops = drv_info->drv_ops;

	rxq = drv_info->rxq[rxqno];
	drv_info->rxq[rxqno] = NULL;
	if (rxq->que->rx_frag_size > Q_FRAG_3_5K)
		bm_pool = drv_info->cd->trans->ctrl_blk->bm_pool_63K;
	else
		bm_pool = drv_info->cd->trans->ctrl_blk->bm_pool;

	mutex_lock(&drv_info->q_exit_mtx);
	/* stop HW rx transaction */
	atomic_set(&rxq->need_exit, 1);
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"cldma%d rxq%d free stop hwq\n", drv_info->hw_id, rxq->rxqno);
	if (likely(cldma_hw_is_accessible(drv_info->cd)))
		drv_ops->cldma_stop_queue(drv_info, DIR_RX, rxqno);

	irq_id = mtk_pci_get_virq_id(mdev, drv_info->pci_ext_irq_id);
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"cldma%d rxq%d free sync irq\n", drv_info->hw_id, rxq->rxqno);
	synchronize_irq(irq_id);
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"cldma%d rxq%d free flush work\n", drv_info->hw_id, rxq->rxqno);
	/* flush on-going work */
	flush_work(&rxq->rx_done_work);
	mutex_unlock(&drv_info->q_exit_mtx);
	/* mask L2 RX interrupt again to avoid race condition causing use-after-free issue */
	if (likely(cldma_hw_is_accessible(drv_info->cd))) {
		drv_ops->cldma_mask_intr(drv_info, DIR_RX, rxqno, QUEUE_XFER_DONE);
		drv_ops->cldma_mask_intr(drv_info, DIR_RX, rxqno, QUEUE_ERROR);
	}

	/* free rx req resource */
	for (i = 0; i < rxq->nr_gpds; i++) {
		req = rxq->req_pool + rxq->free_idx;
		if (!(req->gpd->rx_gpd.gpd_flags & CLDMA_GPD_FLAG_HWO) &&
		    le16_to_cpu(req->gpd->rx_gpd.data_recv_len)) {
			MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
				"cldma%d rxq%d dispatch req%d in rxq_free\n",
				drv_info->hw_id, rxq->rxqno, rxq->free_idx);
			mtk_cldma_rx_skb_adjust(mdev, rxq, req);
			rxq->rx_done(req->skb, rxq->arg, true);
			req->skb = NULL;
		}
		if (req->skb) {
			if (rxq->nr_bds) {
				skb_shinfo(req->skb)->frag_list = NULL;
			} else {
				if (req->data_dma_addr)
					dma_unmap_single(mdev->dev, req->data_dma_addr,
							 req->mtu, DMA_FROM_DEVICE);
				mtk_bm_free(bm_pool, req->skb);
			}
		}
		for (j = 0; j < rxq->nr_bds; j++) {
			bd_dsc = req->bd_dsc_pool + j;
			if (bd_dsc->skb) {
				if (bd_dsc->data_dma_addr)
					dma_unmap_single(mdev->dev, bd_dsc->data_dma_addr,
							 req->frag_size, DMA_FROM_DEVICE);
				bd_dsc->skb->next = NULL;
				mtk_bm_free(bm_pool, bd_dsc->skb);
			}
			dma_pool_free(drv_info->bd_dma_pool,
				      bd_dsc->bd, bd_dsc->bd_dma_addr);
		}
		if (req->bd_dsc_pool)
			devm_kfree(mdev->dev, req->bd_dsc_pool);
		dma_pool_free(drv_info->gpd_dma_pool, req->gpd, req->gpd_dma_addr);
		rxq->free_idx = (rxq->free_idx + 1) % rxq->nr_gpds;
	}

	devm_kfree(mdev->dev, rxq->req_pool);
	MTK_DBG(mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"cldma%d rxq%d free unregister ws\n", drv_info->hw_id, rxq->rxqno);
	wakeup_source_unregister(rxq->ws);
	devm_kfree(mdev->dev, rxq);

	return 0;
}

static int mtk_cldma_dev_exit(struct cldma_dev *cd, int hif_id)
{
	struct cldma_drv_info *drv_info;
	struct mtk_md_dev *mdev;
	int virq_id;
	int hw_id;
	int i;

	if (!cd || hif_id >= NR_CLDMA)
		return -EINVAL;

	if (!cd->cldma_drv_info[hif_id])
		return 0;

	/* free cldma descriptor */
	drv_info = cd->cldma_drv_info[hif_id];
	mdev = cd->trans->mdev;
	virq_id = mtk_pci_get_virq_id(mdev, drv_info->pci_ext_irq_id);
	mtk_pci_mask_irq(mdev, drv_info->pci_ext_irq_id);
	synchronize_irq(virq_id);
	for (i = 0; i < HW_QUEUE_NUM; i++) {
		if (drv_info->txq[i])
			mtk_cldma_txq_free(drv_info, drv_info->txq[i]->txqno);
		if (drv_info->rxq[i])
			mtk_cldma_rxq_free(drv_info, drv_info->rxq[i]->rxqno);
	}

	mtk_stats_unregister_cb(mdev,
				mdev->utility_cfg->stats_cfg->ctrl_type_base +
				(hif_id * 2) + DIR_RX);
	mtk_stats_unregister_cb(mdev,
				mdev->utility_cfg->stats_cfg->ctrl_type_base +
				(hif_id * 2) + DIR_TX);
	flush_workqueue(drv_info->wq);
	destroy_workqueue(drv_info->wq);
	dma_pool_destroy(drv_info->bd_dma_pool);
	dma_pool_destroy(drv_info->gpd_dma_pool);
	mtk_pci_unregister_irq(mdev, drv_info->pci_ext_irq_id);

	hw_id = drv_info->hw_id;
	devm_kfree(mdev->dev, drv_info);
	cd->cldma_drv_info[hif_id] = NULL;
	MTK_INFO(mdev, "CLDMA%d exit done\n", hw_id);

	return 0;
}

static int mtk_cldma_start_xfer(struct cldma_drv_info *drv_info, u32 qno)
{
	struct cldma_drv_ops *drv_ops;
	struct txq *txq;
	int ret = 0;
	u32 val;

	txq = drv_info->txq[qno];
	drv_ops = drv_info->drv_ops;

	val = drv_ops->cldma_get_tx_start_addr(drv_info, qno);
	if (unlikely(!val)) {
		drv_ops->cldma_drv_init(drv_info);
		txq = drv_info->txq[qno];
		drv_ops->cldma_setup_start_addr(drv_info, DIR_TX, qno,
						txq->req_pool[txq->free_idx].gpd_dma_addr);
		drv_ops->cldma_start_queue(drv_info, DIR_TX, qno);
		txq->tx_started = true;
	} else if (unlikely(val == LINK_ERROR_VAL)) {
		cldma_access_error(drv_info->cd);
		ret = -EIO;
	} else {
		if (unlikely(!txq->tx_started)) {
			drv_ops->cldma_start_queue(drv_info, DIR_TX, qno);
			txq->tx_started = true;
		} else {
			drv_ops->cldma_resume_queue(drv_info, DIR_TX, qno);
		}
	}

	return ret;
}

/**
 * mtk_cldma_init() - Initialize CLDMA
 *
 * @trans: pointer to transaction structure
 *
 * Return:
 * 0 - OK
 * -ENOMEM - out of memory
 */
int mtk_cldma_init(struct mtk_ctrl_trans *trans)
{
	struct cldma_dev *cd;

	cd = devm_kzalloc(trans->mdev->dev, sizeof(*cd), GFP_KERNEL);
	if (!cd)
		return -ENOMEM;

	cd->trans = trans;
	trans->dev = cd;
	cldma_dbgfs_init(cd);

	return 0;
}

/**
 * mtk_cldma_exit() - De-Initialize CLDMA
 *
 * @trans: pointer to transaction structure
 *
 * Return:
 * 0 - OK
 */
int mtk_cldma_exit(struct mtk_ctrl_trans *trans)
{
	if (!trans->dev)
		return 0;

	cldma_dbgfs_exit(trans->dev);
	devm_kfree(trans->mdev->dev, trans->dev);
	trans->dev = NULL;

	return 0;
}

/**
 * mtk_cldma_open() - Initialize CLDMA hardware queue
 *
 * @cd: pointer to CLDMA device
 * @skb: pointer to socket buffer
 *
 * Return:
 * 0 - OK
 * -EBUSY - hardware queue is busy
 * -EIO - failed to initialize hardware queue
 * -ENOMEM - failed to alloc memory
 */
static int mtk_cldma_open(struct cldma_dev *cd, struct sk_buff *skb)
{
	struct trb_open_priv *trb_open_priv = (struct trb_open_priv *)skb->data;
	struct trb *trb = (struct trb *)skb->cb;
	struct cldma_drv_info *drv_info;
	struct queue_info *que;
	struct txq *txq;
	struct rxq *rxq;
	int err = 0;

	que = radix_tree_lookup(&cd->trans->queue_tbl, trb->channel_id & 0xFFFF);
	drv_info = cd->cldma_drv_info[que->hif_id];
	if (!drv_info) {
		err = -EIO;
		goto out;
	}

	if (que->tx_mtu == 0 || que->rx_mtu == 0 ||
	    que->tx_mtu > Q_MTU_63K || que->rx_mtu > Q_MTU_63K) {
		MTK_ERR(cd->trans->mdev,
			"Failed to enable cldma%d txq%d rxq%d due to wrong mtu: %x %x\n",
			drv_info->hw_id, que->txqno, que->rxqno, que->tx_mtu, que->rx_mtu);
		err = -EINVAL;
		goto out;
	}

	trb_open_priv->tx_mtu = que->tx_mtu;
	trb_open_priv->rx_mtu = que->rx_mtu;
	trb_open_priv->tx_frag_size = que->tx_frag_size;
	trb_open_priv->rx_frag_size = que->rx_frag_size;

	if (drv_info->txq[que->txqno] || drv_info->rxq[que->rxqno]) {
		err = -EBUSY;
		goto out;
	}

	txq = mtk_cldma_txq_alloc(drv_info, skb);
	if (!txq) {
		err = -ENOMEM;
		goto out;
	}

	rxq = mtk_cldma_rxq_alloc(drv_info, skb);
	if (!rxq) {
		err = -ENOMEM;
		mtk_cldma_txq_free(drv_info, txq->txqno);
		goto out;
	}

out:
	trb->status = err;
	trb->trb_complete(skb);

	return err;
}

/**
 * mtk_cldma_tx() - start CLDMA TX transaction
 *
 * @cd: pointer to CLDMA device
 * @skb: pointer to socket buffer
 *
 * Return:
 * 0 - OK
 * -EPIPE - hardware queue is broken
 * -EIO - PCI link error
 * -ETIMEOUT - failed to get ds_lock
 */
static int mtk_cldma_tx(struct cldma_dev *cd, struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct cldma_drv_info *drv_info;
	struct mtk_md_dev *mdev;
	struct queue_info *que;
	struct txq *txq;
	int err = 0;

	que = radix_tree_lookup(&cd->trans->queue_tbl, trb->channel_id & 0xFFFF);
	drv_info = cd->cldma_drv_info[que->hif_id];
	if (unlikely(!drv_info))
		return -EPIPE;
	txq = drv_info->txq[que->txqno];
	if (unlikely(!txq) || txq->is_stopping)
		return -EPIPE;

	mdev = drv_info->mdev;
	if (unlikely(!cldma_hw_is_accessible(cd))) {
		MTK_ERR(mdev, "Failed to access cldma hw\n");
		return -EPIPE;
	}

	mtk_pm_ds_lock(mdev, MTK_USER_CTRL);
	err = mtk_pm_ds_wait_complete(mdev, MTK_USER_CTRL);
	if (unlikely(err)) {
		MTK_ERR(mdev, "Failed to lock ds:%d cldma_tx\n", err);
		goto out;
	}

	trace_mtk_cldma_start_xfer(drv_info->hw_id, que->txqno);
	err = mtk_cldma_start_xfer(drv_info, que->txqno);
	if (unlikely(err))
		MTK_ERR(mdev, "Failed to trigger cldma tx\n");

out:
	mtk_pm_ds_unlock(mdev, MTK_USER_CTRL);

	return err;
}

/**
 * mtk_cldma_close() - De-Initialize CLDMA hardware queue
 *
 * @cd: pointer to CLDMA device
 * @skb: pointer to socket buffer
 *
 * Return:
 * 0 - OK
 * -EPIPE - hardware queue is broken
 */
static int mtk_cldma_close(struct cldma_dev *cd, struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct trb_close_priv *trb_close_priv;
	struct cldma_drv_info *drv_info;
	struct queue_info *que;

	que = radix_tree_lookup(&cd->trans->queue_tbl, trb->channel_id & 0xFFFF);
	drv_info = cd->cldma_drv_info[que->hif_id];
	if (unlikely(!drv_info))
		return -EPIPE;

	trb_close_priv = (struct trb_close_priv *)skb->data;
	if (drv_info->txq[que->txqno]) {
		trb_close_priv->txq_free_start_time = jiffies;
		mtk_cldma_txq_free(drv_info, que->txqno);
		trb_close_priv->txq_free_end_time = jiffies;
	}
	if (drv_info->rxq[que->rxqno]) {
		trb_close_priv->rxq_free_start_time = jiffies;
		mtk_cldma_rxq_free(drv_info, que->rxqno);
		trb_close_priv->rxq_free_end_time = jiffies;
	}

	trb->status = 0;
	trb->trb_complete(skb);

	return 0;
}

static int mtk_cldma_check_device_rx(struct cldma_dev *cd, struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct cldma_drv_info *drv_info;
	struct mtk_md_dev *mdev;
	struct queue_info *que;
	u32 ret;

	que = radix_tree_lookup(&cd->trans->queue_tbl, trb->channel_id & 0xFFFF);
	drv_info = cd->cldma_drv_info[que->hif_id];
	if (unlikely(!drv_info)) {
		ret = -EPIPE;
		goto out;
	}
	mdev = drv_info->mdev;

	mtk_pm_ds_lock(mdev, MTK_USER_CTRL);
	ret = mtk_pm_ds_wait_complete(mdev, MTK_USER_CTRL);
	if (unlikely(ret)) {
		MTK_ERR(mdev, "Failed to lock ds:%d\n", ret);
	} else {
		ret = drv_info->drv_ops->cldma_check_device_rx_status(drv_info, que->txqno);
		if (!ret || ret == LINK_ERROR_VAL)
			ret = -EPIPE;
		else
			ret = 0;
	}
	mtk_pm_ds_unlock(mdev, MTK_USER_CTRL);

out:
	trb->status = ret;
	trb->trb_complete(skb);

	return 0;
}

static int mtk_cldma_txbuf_set(struct cldma_drv_info *drv_info, struct sk_buff *skb,
			       struct tx_req *req, int nr_bds)
{
	struct sk_buff *curr_skb, *next_skb;
	struct mtk_md_dev *mdev;
	struct bd_dsc *bd_dsc;
	int err;
	int i;

	mdev = drv_info->mdev;

	if (nr_bds) {
		bd_dsc = req->bd_dsc_pool;
		curr_skb = skb;
		for (i = 0; i < nr_bds && curr_skb; i++) {
			bd_dsc = req->bd_dsc_pool + i;
			if (req->bd_dsc_pool == bd_dsc) {
				bd_dsc->data_len = skb->len - skb->data_len;
				next_skb = skb_shinfo(skb)->frag_list;
			} else {
				bd_dsc->data_len = curr_skb->len;
				next_skb = curr_skb->next;
			}
			bd_dsc->data_dma_addr = dma_map_single(mdev->dev, curr_skb->data,
							       bd_dsc->data_len, DMA_TO_DEVICE);
			err = dma_mapping_error(mdev->dev, bd_dsc->data_dma_addr);
			if (unlikely(err))
				goto err_unmap_buffer;

			bd_dsc->bd->tx_bd.data_buff_ptr_h =
				cpu_to_le32((u64)(bd_dsc->data_dma_addr) >> 32);
			bd_dsc->bd->tx_bd.data_buff_ptr_l = cpu_to_le32(bd_dsc->data_dma_addr);
			bd_dsc->bd->tx_bd.data_buffer_len = cpu_to_le16(bd_dsc->data_len);
			curr_skb = next_skb;
		}
		bd_dsc->bd->tx_bd.bd_flags = CLDMA_BD_FLAG_EOL;
	} else {
		req->data_dma_addr = dma_map_single(mdev->dev, skb->data,
						    skb->len, DMA_TO_DEVICE);
		err = dma_mapping_error(mdev->dev, req->data_dma_addr);
		if (unlikely(err)) {
			req->data_dma_addr = 0;
			goto err_exit;
		}

		req->gpd->tx_gpd.data_buff_ptr_h = cpu_to_le32((u64)(req->data_dma_addr) >> 32);
		req->gpd->tx_gpd.data_buff_ptr_l = cpu_to_le32(req->data_dma_addr);
	}

	return 0;

err_unmap_buffer:
	for (i = 0; i < nr_bds; i++) {
		bd_dsc = req->bd_dsc_pool + i;
		if (dma_mapping_error(mdev->dev, bd_dsc->data_dma_addr)) {
			bd_dsc->data_dma_addr = 0;
			break;
		}
		dma_unmap_single(mdev->dev, bd_dsc->data_dma_addr,
				 bd_dsc->data_len, DMA_TO_DEVICE);
		bd_dsc->data_dma_addr = 0;
	}
err_exit:
	MTK_ERR(mdev, "Failed to map dma! error:%d\n", err);
	return -EAGAIN;
}

int mtk_cldma_submit_tx(void *dev, struct sk_buff *skb)
{
	struct trb *trb = (struct trb *)skb->cb;
	struct cldma_drv_info *drv_info;
	struct cldma_dev *cd = dev;
	struct queue_info *que;
	struct tx_req *req;
	struct txq *txq;
	int ret;

	que = radix_tree_lookup(&cd->trans->queue_tbl, trb->channel_id & 0xFFFF);
	drv_info = cd->cldma_drv_info[que->hif_id];
	if (unlikely(!drv_info)) {
		ret = -EINVAL;
		goto out;
	}

	txq = drv_info->txq[que->txqno];
	if (unlikely(!txq)) {
		ret = -EINVAL;
		goto out;
	}

	if (!atomic_read(&txq->req_budget)) {
		if (!cldma_hw_is_accessible(cd))
			ret = -EFAULT;
		else
			ret = -EAGAIN;
		goto out;
	}

	req = txq->req_pool + txq->wr_idx;
	req->gpd->tx_gpd.debug_id = 0x01;
	ret = mtk_cldma_txbuf_set(drv_info, skb, req, txq->nr_bds);
	if (ret)
		goto out;
	req->gpd->tx_gpd.data_buff_len = cpu_to_le16(skb->len);

	dma_wmb(); /* ensure data msg set done before HWO setup */

	mutex_lock(&txq->tx_mtx);
	req->gpd->tx_gpd.gpd_flags |= CLDMA_GPD_FLAG_HWO;

	drv_info->stats.cldma_tx.tx_sw_pkt[txq->txqno]++;

	req->data_len = skb->len;
	req->skb = skb;
	req->data_vm_addr = skb->data;
	mutex_unlock(&txq->tx_mtx);

	txq->wr_idx = (txq->wr_idx + 1) % txq->nr_gpds;
	atomic_dec(&txq->req_budget);
	trace_mtk_cldma_submit_tx(drv_info->hw_id, txq->txqno,
				  txq->wr_idx, atomic_read(&txq->req_budget));

out:
	return ret;
}

int mtk_cldma_get_tx_budget(void *dev, enum mtk_hif_id hif_id, u32 qno)
{
	struct cldma_drv_info *drv_info;
	struct cldma_dev *cd = dev;
	struct txq *txq;

	if (unlikely(hif_id >= NR_CLDMA || qno >= HW_QUE_NUM || !cd))
		return -EINVAL;

	drv_info = cd->cldma_drv_info[hif_id];
	if (!drv_info)
		return -EINVAL;
	txq = drv_info->txq[qno];
	if (!txq)
		return -EINVAL;
	return atomic_read(&txq->req_budget);
}

int mtk_cldma_suspend(struct mtk_ctrl_trans *trans)
{
	struct cldma_dev *cd = trans->dev;
	struct cldma_drv_info *drv_info;
	struct txq *txq;
	int irq_id;
	int i, j;

	for (i = 0; i < NR_CLDMA; i++) {
		drv_info = cd->cldma_drv_info[i];
		if (!drv_info)
			continue;

		drv_info->drv_ops->cldma_stop_queue(drv_info, DIR_TX, ALLQ);

		irq_id = mtk_pci_get_virq_id(drv_info->mdev, drv_info->pci_ext_irq_id);
		synchronize_irq(irq_id);
		for (j = 0; j < HW_QUEUE_NUM; j++) {
			txq = drv_info->txq[j];
			if (txq)
				flush_work(&txq->tx_done_work);
		}
	}
	MTK_DBG(trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"CLDMA Suspend Done\n");

	return 0;
}

int mtk_cldma_suspend_late(struct mtk_ctrl_trans *trans)
{
	struct cldma_dev *cd = trans->dev;
	struct cldma_drv_info *drv_info;
	struct rxq *rxq;
	int irq_id;
	int i, j;

	for (i = 0; i < NR_CLDMA; i++) {
		drv_info = cd->cldma_drv_info[i];
		if (!drv_info)
			continue;

		for (j = 0; j < HW_QUEUE_NUM; j++) {
			rxq = drv_info->rxq[j];
			if (!rxq)
				continue;

			mutex_lock(&drv_info->q_exit_mtx);
			atomic_set(&rxq->need_exit, 1);
			drv_info->drv_ops->cldma_stop_queue(drv_info, DIR_RX, j);
			irq_id = mtk_pci_get_virq_id(drv_info->mdev, drv_info->pci_ext_irq_id);
			synchronize_irq(irq_id);
			flush_work(&rxq->rx_done_work);
			mutex_unlock(&drv_info->q_exit_mtx);
		}

		mtk_pci_mask_irq(drv_info->mdev, drv_info->pci_ext_irq_id);
	}
	MTK_DBG(trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"CLDMA Suspend_late Done\n");

	return 0;
}

int mtk_cldma_resume_early(struct mtk_ctrl_trans *trans, bool link_ready)
{
	struct cldma_dev *cd = trans->dev;
	struct cldma_drv_info *drv_info;
	struct rxq *rxq;
	int i, j;

	if (!link_ready || mtk_pm_check_dev_reset(trans->mdev)) {
		MTK_DBG(trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
			"CLDMA Failed to Resume_early due to Link ERROR or resume from L3\n");
		set_bit(CLDMA_NOT_ACCESSIBLE, &cd->err_event);
		return -EIO;
	}

	for (i = 0; i < NR_CLDMA; i++) {
		drv_info = cd->cldma_drv_info[i];
		if (!drv_info)
			continue;

		for (j = 0; j < HW_QUEUE_NUM; j++) {
			rxq = drv_info->rxq[j];
			if (rxq) {
				atomic_set(&rxq->need_exit, 0);
				drv_info->drv_ops->cldma_resume_queue(drv_info, DIR_RX, j);
			}
		}

		mtk_pci_unmask_irq(drv_info->mdev, drv_info->pci_ext_irq_id);
	}
	MTK_DBG(trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"CLDMA Resume_early Done\n");

	return 0;
}

int mtk_cldma_resume(struct mtk_ctrl_trans *trans, bool link_ready)
{
	struct cldma_dev *cd = trans->dev;
	struct cldma_drv_info *drv_info;
	struct txq *txq;
	int i, j;

	if (!link_ready || mtk_pm_check_dev_reset(trans->mdev)) {
		MTK_DBG(trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
			"CLDMA Failed to Resume due to Link ERROR or resume from L3\n");
		set_bit(CLDMA_NOT_ACCESSIBLE, &cd->err_event);
		return -EIO;
	}

	for (i = 0; i < NR_CLDMA; i++) {
		drv_info = cd->cldma_drv_info[i];
		if (!drv_info)
			continue;

		for (j = 0; j < HW_QUEUE_NUM; j++) {
			txq = drv_info->txq[j];
			if (txq) {
				if (atomic_read(&txq->req_budget) < txq->nr_gpds - 1)
					mtk_cldma_start_xfer(drv_info, j);
			}
		}
	}
	MTK_DBG(trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_MISC,
		"CLDMA Resume Done\n");

	return 0;
}

static int (*trb_act_tbl[TRB_CMD_MAX])(struct cldma_dev *cd, struct sk_buff *skb) = {
	[TRB_CMD_ENABLE] = mtk_cldma_open,
	[TRB_CMD_TX] = mtk_cldma_tx,
	[TRB_CMD_DISABLE] = mtk_cldma_close,
	[TRB_CMD_CHECK_STA] = mtk_cldma_check_device_rx,
};

/**
 * mtk_cldma_trb_process() - Dispatch trb request to low-level CLDMA routine
 *
 * @dev: pointer to CLDMA device
 * @skb: pointer to socket buffer
 *
 * Return:
 * 0 - successful
 * <0 - failure
 */
int mtk_cldma_trb_process(void *dev, struct sk_buff *skb)
{
	struct cldma_dev *cd;
	struct trb *trb;
	int ret;

	if (!dev || !skb)
		return -EINVAL;

	cd = (struct cldma_dev *)dev;
	trb = (struct trb *)skb->cb;

	if (!(trb->cmd > TRB_CMD_MIN && trb->cmd < TRB_CMD_SET_CH_CFG))
		return -EINVAL;

	ret = trb_act_tbl[trb->cmd](cd, skb);

	return ret;
}

void mtk_cldma_fsm_state_listener(struct mtk_fsm_param *param, struct mtk_ctrl_trans *trans)
{
	struct cldma_dev *cd = trans->dev;
	struct cldma_drv_info *drv_info;
	struct cldma_drv_ops *drv_ops;
	struct txq *txq;
	int i;

	switch (param->evt_id) {
	case FSM_EVT_GNSS_ENABLE:
		mtk_cldma_dev_init(cd, CLDMA4);
		return;
	case FSM_EVT_GNSS_DISABLE:
		mtk_cldma_dev_exit(cd, CLDMA4);
		return;
	default:
		break;
	}

	switch (param->to) {
	case FSM_STATE_POSTDUMP:
		mtk_cldma_dev_init(cd, CLDMA0);
		break;
	case FSM_STATE_DOWNLOAD:
		if ((param->fsm_flag & FSM_F_DL_PORT_CREATE) ||
		    (param->fsm_flag & FSM_F_DL_PL) ||
		    (param->fsm_flag & FSM_F_DL_FB))
			mtk_cldma_dev_init(cd, CLDMA0);
		break;
	case FSM_STATE_BOOTUP:
		if (param->fsm_flag & FSM_F_SAP_HS_START)
			mtk_cldma_dev_init(cd, CLDMA0);
		else if (param->fsm_flag & FSM_F_MD_HS_START)
			mtk_cldma_dev_init(cd, CLDMA1);
		else if (param->fsm_flag & FSM_F_MD_REBOOT)
			mtk_cldma_dev_exit(cd, CLDMA1);

		break;
	case FSM_STATE_OFF:
		for (i = 0; i < NR_CLDMA; i++)
			mtk_cldma_dev_exit(cd, i);
		break;
	case FSM_STATE_EXCEPTION:
		if (param->fsm_flag & FSM_F_SAP_HS_START) {
			mtk_cldma_dev_init(cd, CLDMA0);
			break;
		}

		if (param->fsm_flag & FSM_F_LINK_EXCEPTION) {
			for (i = 0; i < NR_CLDMA; i++)
				mtk_cldma_dev_exit(cd, i);
			set_bit(CLDMA_NOT_ACCESSIBLE, &cd->err_event);
			break;
		}

		if (param->fsm_flag & FSM_F_MDEE_INIT)
			mtk_cldma_dev_init(cd, CLDMA1);
		drv_info = cd->cldma_drv_info[CLDMA1];
		if (!drv_info) {
			MTK_ERR(trans->mdev, "Failed to get cldma1's drv_info in mdee flow\n");
			break;
		}
		drv_ops = drv_info->drv_ops;
		if (param->fsm_flag & FSM_F_MDEE_INIT) {
			mtk_cldma_dump(trans);
			drv_ops->cldma_stop_queue(drv_info, DIR_TX, ALLQ);
			for (i = 0; i < HW_QUEUE_NUM; i++) {
				txq = drv_info->txq[i];
				if (txq)
					txq->is_stopping = true;
			}
		} else if (param->fsm_flag & FSM_F_MDEE_CLEARQ_DONE) {
			drv_ops->cldma_drv_reset(drv_info);
		} else if (param->fsm_flag & FSM_F_MDEE_ALLQ_RESET) {
			drv_ops->cldma_drv_init(drv_info);
			for (i = 0; i < HW_QUEUE_NUM; i++) {
				txq = drv_info->txq[i];
				if (txq)
					txq->is_stopping = false;
			}
			/* After leaving lowpower L2 states, PCIe will reset,
			 * so CLDMA L1 register needs to be set again.
			 */
			mtk_pci_unmask_irq(drv_info->mdev, drv_info->pci_ext_irq_id);
		}
		break;
	default:
		break;
	}
}

int mtk_cldma_dump(struct mtk_ctrl_trans *trans)
{
	struct cldma_dev *cd = trans->dev;
	struct cldma_drv_info *drv_info;
	struct mtk_md_dev *mdev;
	int err = 0, i, j, qlen;

	for (i = 0; i < NR_CLDMA; i++) {
		drv_info = cd->cldma_drv_info[i];
		if (!drv_info)
			continue;

		for (j = 0; j < HW_QUE_NUM; j++) {
			qlen = skb_queue_len(&trans->trans_list[i].skb_list[j]);
			if (!qlen)
				continue;
			MTK_DBG(trans->mdev, MTK_DBG_CLDMA, MTK_MEMLOG_RG_CTRL_DUMP,
				"CLDMA%d tx_list%d skb count:%d\n", drv_info->hw_id,
				j, qlen);
		}
	}

	for (i = 0; i < NR_CLDMA; i++) {
		drv_info = cd->cldma_drv_info[i];
		if (!drv_info)
			continue;

		for (j = 0; j < HW_QUEUE_NUM; j++)
			cldma_gpd_dump(drv_info, j);

		mdev = drv_info->mdev;
		mtk_pm_runtime_get(mdev, MTK_USER_CTRL, true);
		mtk_pm_ds_lock(mdev, MTK_USER_CTRL);
		err = mtk_pm_ds_wait_complete(mdev, MTK_USER_CTRL);
		if (unlikely(err)) {
			MTK_ERR(mdev, "Failed to lock ds:%d\n", err);
		} else {
			if (likely(cldma_hw_is_accessible(cd)))
				drv_info->drv_ops->cldma_drv_dump(drv_info);
		}

		mtk_pm_ds_unlock(mdev, MTK_USER_CTRL);
		mtk_pm_runtime_put(mdev, MTK_USER_CTRL, true);
	}
	return err;
}

int mtk_cldma_check_ch_cfg(void *dev, struct queue_info *que)
{
	struct cldma_drv_info *drv_info;
	struct cldma_dev *cd = dev;
	struct mtk_md_dev *mdev;
	struct txq *txq;
	struct rxq *rxq;

	mdev = cd->trans->mdev;
	drv_info = cd->cldma_drv_info[que->hif_id];

	if (unlikely(!drv_info)) {
		MTK_ERR(mdev, "CLDMA%d has not been initialized\n",
			mtk_cldma_hw_id_tbl[que->hif_id]);
		return -EINVAL;
	}

	txq = drv_info->txq[que->txqno];
	rxq = drv_info->rxq[que->rxqno];
	if (unlikely(!txq || !rxq)) {
		MTK_ERR(mdev, "CLDMA%d txq%d rxq%d has not been enabled\n",
			mtk_cldma_hw_id_tbl[que->hif_id], que->txqno, que->rxqno);
		return -EINVAL;
	}

	if (que->tx_mtu != txq->que->tx_mtu || que->rx_mtu != rxq->que->rx_mtu) {
		MTK_ERR(mdev, "Channel:%08x tx_mtu:%08x rx_mtu:%08x do not match ch cfg\n",
			que->tx_chl, que->tx_mtu, que->rx_mtu);
		return -EINVAL;
	}

	return 0;
}

module_param(mtk_ctrl_keep_wake_time_ms, uint, 0644);
MODULE_PARM_DESC(mtk_ctrl_keep_wake_time_ms, "This is used to config ctrl keep wake time\n");
