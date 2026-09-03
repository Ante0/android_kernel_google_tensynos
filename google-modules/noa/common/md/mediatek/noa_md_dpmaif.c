#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "t900/noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_dev.h"
#endif

#include "noa_md.h"
#include "noa_md_dpa.h"
#include "noa_md_dpmaif.h"
#include "noa_md_trace.h"
#include "noa_md_wrapper_dpmaif.h"
#include "t900/noa_md_mtk_priv.h"

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "noa_md_apc2ncp_ring_fullsoc.h"
#elif IS_ENABLED(CONFIG_NOA_SIM_SUPPORT)
#include "noa_md_apc2ncp_ring.h"
#endif

struct dpmaif_drv_ops noa_dpmaif_drv_ops;

static int noa_md_ncp_doorbell_dl_add_pit_cnt(
	struct dpmaif_drv_info *drv_info, u8 q_num, u32 pit_remain_cnt)
{
	int ret = 0;
	ret = noa_md_rx_set_ring_read_idx(q_num, pit_remain_cnt);
	return ret;
}

static int noa_md_ncp_doorbell_dl_add_bat_cnt(
	struct dpmaif_drv_info *drv_info, u8 q_num, u32 bat_entry_cnt)
{
	int ret = 0;
	ret = noa_md_rx_write_bat_refill_ring(q_num, bat_entry_cnt);
	return ret;
}

static int noa_md_ncp_doorbell_dl_add_frg_cnt(
	struct dpmaif_drv_info *drv_info, u8 q_num, u32 frg_entry_cnt)
{
	int ret = 0;
	ret = noa_md_rx_write_frag_refill_ring(q_num, frg_entry_cnt);
	return ret;
}

static int noa_md_ncp_doorbell_ul_add_drb(struct dpmaif_drv_info *drv_info, u8 q_num, u32 drb_cnt)
{
	int ret = 0;
	ret = noa_md_apc2ncp_set_ring_write_idx(q_num, drb_cnt);
	NOA_MD_DATA("q_num:%d, drb_cnt:%d, ret:%d", q_num, drb_cnt, ret);
	noa_md_dpa_notify_ncp(q_num);
	return ret;
}

#if IS_ENABLED(CONFIG_ENABLE_T900_NOA_SUPPORT)
static int noa_md_ncp_doorbell_dl_add_noa_free_pool(
	struct dpmaif_drv_info *drv_info, u8 q_num, u32 tkid)
{
	int ret = 0;
	ret = noa_md_rx_add_tkid_to_free_pool(q_num, tkid);
	return ret;
}
#endif

/*
 * Send a doorbell signal (refer to mtk_dpmaif_drv_send_doorbell_com).
 *
 * This function internally calls the NCP's doorbell to
 * read or write PIT/BAT/DRB.
 *
 * @param drv_info: Pointer to the dpmaif_drv_info structure.
 * @param type: The type of the ring.
 * @param q_id: The queue ID.
 * @param cnt: The count of doorbells to send.
 * @return 0 on success.
 */
int noa_md_dpmaif_send_doorbell(
	struct dpmaif_drv_info *drv_info,
	enum dpmaif_drv_ring_type type,
	u8 id, u32 cnt)
{
	int ret = 0;

	switch (type) {
	case DPMAIF_PIT:
		ret = noa_md_ncp_doorbell_dl_add_pit_cnt(drv_info, id, cnt);
		break;
	case DPMAIF_BAT:
		ret = noa_md_ncp_doorbell_dl_add_bat_cnt(drv_info, id, cnt);
		break;
	case DPMAIF_FRAG:
		ret = noa_md_ncp_doorbell_dl_add_frg_cnt(drv_info, id, cnt);
		break;
	case DPMAIF_DRB:
		ret = noa_md_ncp_doorbell_ul_add_drb(drv_info, id, cnt);
		break;
#if IS_ENABLED(CONFIG_ENABLE_T900_NOA_SUPPORT)
	case NOA_FREE_POOL:
		ret = noa_md_ncp_doorbell_dl_add_noa_free_pool(drv_info, id, cnt);
		break;
#endif
	default:
		break;
	}

	NOA_MD_DATA("type=%d, id=%d, cnt=%d, ret=%d", type, id, cnt, ret);
	return 0;
}

void noa_md_dpmaif_fill_tx_info(void *drb, struct dpmaif_tx_info *tx_info, int type)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb = md_dev.dpmaif_dcb;
	struct dpmaif_drv_ops *dpmaif_drv_ops = dpmaif_dcb->drv_info->drv_ops;

	// Call mtk_dpmaif_fill_tx_info
	dpmaif_drv_ops->fill_tx_info(drb, tx_info, type);
#if IS_ENABLED(CONFIG_ENABLE_T900_NOA_SUPPORT)
	if (type == MSG_DRB) {
		struct noa_md_feature_ctrl *feature_ctrl = &md_dev.feature_ctrl;
		struct dpmaif_msg_drb *msg_drb = (struct dpmaif_msg_drb *)drb;
		if (feature_ctrl->tx_tcp_slow_start_enabled &&
			tx_info->in_tcp_slow_start) {
			msg_drb->msg_header1 |=
				FIELD_PREP(NOA_MSG_IN_TCP_SLOW_START,
				tx_info->in_tcp_slow_start);
		}
	}
#endif
}

static u32 noa_dpmaif_drv_ul_get_drb_ridx(struct dpmaif_drv_info *drv_info, u8 qno)
{
	return noa_md_apc2ncp_get_ring_read_idx(qno);
}

static int noa_dpmaif_drv_dl_get_pit_wridx(struct dpmaif_drv_info *drv_info, u32 qno)
{
	NOA_MD_DATA("enter: qno=%u", qno);
	return noa_md_rx_get_ring_write_idx(qno);
}

static int noa_dpmaif_drv_dl_get_bat_wridx(struct dpmaif_drv_info *drv_info, u32 qno)
{
	NOA_MD_DATA("enter: qno=%u", qno);
	return noa_md_rx_get_bat_write_idx(qno);
}

static int noa_dpmaif_drv_dl_get_bat_ridx(struct dpmaif_drv_info *drv_info, u32 qno)
{
	NOA_MD_DATA("enter: qno=%u", qno);
	return noa_md_rx_get_bat_read_idx(qno);
}

static int noa_dpmaif_drv_dl_get_frg_ridx(struct dpmaif_drv_info *drv_info, u32 qno)
{
	NOA_MD_DATA("enter: qno=%u", qno);
	return noa_md_rx_get_frg_read_idx(qno);
}

int noa_md_dpmaif_drv_get_ring_idx(struct dpmaif_drv_info *drv_info,
	enum dpmaif_drv_ring_idx index, u8 q_id)
{
	int ret = 0;

	NOA_MD_DATA("index=%d, q_id=%hhu", index, q_id);

	switch (index) {
	case DPMAIF_PIT_WIDX:
		ret = noa_dpmaif_drv_dl_get_pit_wridx(drv_info, q_id);
		break;
	case DPMAIF_PIT_RIDX:
		// TODO: Get PIT rdidx from NOA
		break;
	case DPMAIF_BAT_WIDX:
		ret = noa_dpmaif_drv_dl_get_bat_wridx(drv_info, q_id);
		break;
	case DPMAIF_BAT_RIDX:
		ret = noa_dpmaif_drv_dl_get_bat_ridx(drv_info, q_id);
		break;
	case DPMAIF_FRAG_WIDX:
		break;

	case DPMAIF_FRAG_RIDX:
		ret = noa_dpmaif_drv_dl_get_frg_ridx(drv_info, q_id);
		break;

	case DPMAIF_DRB_WIDX:
		break;

	case DPMAIF_DRB_RIDX:
		ret = noa_dpmaif_drv_ul_get_drb_ridx(drv_info, q_id);
		break;
	default:
		break;
	}

	return ret;
}

int noa_md_dpmaif_get_rx_info(void *pit, struct dpmaif_rx_info *rx_info, u32 pit_seq_expect,
	u8 q_id)
{
	struct dpmaif_pd_pit *pd_pit = (struct dpmaif_pd_pit *)pit;
	struct dpmaif_msg_pit *msg_pit;
	u64 dma_addr;
	int bat_id = -1;
	int bat_type = -1;

	rx_info->pit_pd_seq = FIELD_GET(PIT_PD_SEQ, le32_to_cpu(pd_pit->pd_footer));

	rx_info->msg_pit = FIELD_GET(PIT_PD_PKT_TYPE, le32_to_cpu(pd_pit->pd_header));
	if (rx_info->msg_pit) {
		msg_pit = (struct dpmaif_msg_pit *)pit;
		rx_info->pit_msg_chnl_id = FIELD_GET(PIT_MSG_CHNL_ID, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_checksum = FIELD_GET(PIT_MSG_CHECKSUM,
						      le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_err = FIELD_GET(PIT_MSG_ERR, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_dp = FIELD_GET(PIT_MSG_DP, le32_to_cpu(msg_pit->dword1));
		rx_info->pit_msg_hash = FIELD_GET(PIT_MSG_HASH, le32_to_cpu(msg_pit->dword3));
		rx_info->pit_msg_pro = FIELD_GET(PIT_MSG_PRO, le32_to_cpu(msg_pit->dword3));
		rx_info->pit_msg_ip = FIELD_GET(PIT_MSG_IP, le32_to_cpu(msg_pit->dword4));
	} else {
		if (q_id == 0 || q_id == 1) {
			bat_id = DPMAIF_BAT0;
		} else if (q_id == 2) {
			bat_id = DPMAIF_BAT1;
		} else {
			NOA_MD_ERROR("Invalid queue id=[%d]", q_id);
			return -DATA_FLOW_CHK_ERR;
		}

		rx_info->normal_bat = FIELD_GET(PIT_PD_BUF_TYPE, le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_cur_bid = (FIELD_GET(PIT_PD_H_BID,
				le32_to_cpu(pd_pit->pd_footer)) << 13) +
				FIELD_GET(PIT_PD_BUF_ID, le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_data_len = FIELD_GET(PIT_PD_DATA_LEN,
						     le32_to_cpu(pd_pit->pd_header));
		rx_info->pit_pd_hd_offset = FIELD_GET(PIT_PD_HD_OFFSET,
						      le32_to_cpu(pd_pit->pd_footer)) << 2;
		rx_info->pit_continue = FIELD_GET(PIT_PD_CONT,
						  le32_to_cpu(pd_pit->pd_header));
		dma_addr = le32_to_cpu(pd_pit->addr_high);
		rx_info->pit_pd_dma_addr = (dma_addr << 32) + le32_to_cpu(pd_pit->addr_low);

		if (rx_info->normal_bat == 0) {
			bat_type = DPMAIF_BAT;
		}
		else if (rx_info->normal_bat == 1) {
			bat_type = DPMAIF_FRAG;
		} else {
			NOA_MD_ERROR("Invalid bat_type=[%d]", bat_type);
			return -DATA_FLOW_CHK_ERR;
		}

		rx_info->pit_pd_cur_bid =
			noa_md_rx_tkid_to_bat_index(bat_id, bat_type, rx_info->pit_pd_cur_bid);

		noa_md_rx_unmap_modem_dpa_bat(bat_id, bat_type, rx_info->pit_pd_cur_bid);
	}

	return 0;
}

/**
 * noa_md_dpmaif_drv_intr_complete() - Complete DPMAIF interrupt handling.
 *
 * This function is a NOA-specific override for the DPMAIF interrupt completion
 * routine. It handles the post-processing of DPMAIF interrupts, potentially
 * skipping or modifying actions taken in the common DPMAIF driver,
 * especially concerning UL/DL done and error scenarios in the NOA context.
 *
 * @drv_info: Pointer to the dpmaif_drv_info structure.
 * @type: The type of the DPMAIF interrupt.
 * @id: The queue ID.
 * @data: Additional data related to the interrupt.
 * Return: 0 on success.
 */
static int noa_md_dpmaif_drv_intr_complete(struct dpmaif_drv_info *drv_info,
				     enum dpmaif_drv_intr_type type, u8 id, u64 data)
{
	switch (type) {
	case DPMAIF_INTR_UL_DONE:
		// Skip the ul done clear and unmask because it has been done by ncp.
		break;
	case DPMAIF_INTR_DL_BATCNT_LEN_ERR:
		// TODO(b/458538873): Handle dl bat count length error.
		break;
	case DPMAIF_INTR_DL_FRGCNT_LEN_ERR:
		// TODO(b/458538873): Handle dl frag count length error.
		break;
	case DPMAIF_INTR_DL_PITCNT_LEN_ERR:
		// TODO(b/458538873): Handle dl pit count length error.
		break;
	case DPMAIF_INTR_DL_DONE:
		// Skip the dl done unmask because it has been done by ncp.
		break;
	default:
		NOA_MD_ERROR("The type is not supported (type: %d)", type);
		break;
	}

	return 0;
}

static int noa_md_dpmaif_dcb_set_ops(struct noa_md_dev *p_md_dev)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct dpmaif_drv_info *dpmaif_drv_info;
	struct dpmaif_drv_info *noa_drv_info;
	struct dpmaif_drv_ops *dpmaif_drv_ops;
	struct dpmaif_drv_ops *noa_drv_ops;

	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	dpmaif_drv_info = dpmaif_dcb->drv_info;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_drv_info, -EINVAL);

	noa_drv_info = noa_dcb->drv_info;
	CHECK_PTR_OR_RETURN_ERR(noa_drv_info, -EINVAL);

	dpmaif_drv_ops = dpmaif_dcb->drv_info->drv_ops;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_drv_ops, -EINVAL);

	noa_drv_ops = &noa_dpmaif_drv_ops;

	NOA_MD_INFO(
		"dpmaif_dcb=0x%p, dpmaif_drv_info=0x%p, dpmaif_drv_ops=0x%p, dpmaif_drv_ops->fill_tx_info=%ps "
		"noa_dcb=0x%p, noa_drv_info=0x%p, noa_drv_ops=0x%p, noa_drv_ops->fill_tx_info=%ps",
		dpmaif_dcb, dpmaif_drv_info, dpmaif_drv_ops, dpmaif_drv_ops->fill_tx_info,
		noa_dcb, noa_drv_info, noa_drv_ops, noa_drv_ops->fill_tx_info);

	noa_drv_ops->init = dpmaif_drv_ops->init;
	noa_drv_ops->start_queue = dpmaif_drv_ops->start_queue;
	noa_drv_ops->stop_queue = dpmaif_drv_ops->stop_queue;
	noa_drv_ops->intr_handle = dpmaif_drv_ops->intr_handle;
	noa_drv_ops->get_ring_idx = dpmaif_drv_ops->get_ring_idx;
	noa_drv_ops->feature_cmd = dpmaif_drv_ops->feature_cmd;
	noa_drv_ops->dump = dpmaif_drv_ops->dump;
	noa_drv_ops->get_rx_info = dpmaif_drv_ops->get_rx_info;

	noa_drv_ops->send_doorbell = noa_md_dpmaif_send_doorbell;
	noa_drv_ops->fill_tx_info = noa_md_dpmaif_fill_tx_info;
	noa_drv_ops->get_ring_idx = noa_md_dpmaif_drv_get_ring_idx;
	noa_drv_ops->get_rx_info = noa_md_dpmaif_get_rx_info;
	noa_drv_ops->intr_complete = noa_md_dpmaif_drv_intr_complete;

	noa_drv_info->drv_ops = noa_drv_ops;

	return 0;
}

int noa_md_dpmaif_rx_napi_init(struct noa_md_dev *p_md_dev)
{
	struct mtk_data_blk *data_blk;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct mtk_wwan_ctlb *wcb;

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);
	data_blk = noa_dcb->data_blk;
	CHECK_PTR_OR_RETURN_ERR(data_blk, -EINVAL);
	wcb = data_blk->wcb;
	CHECK_PTR_OR_RETURN_ERR(wcb, -EINVAL);

	for (int i = 0; i < noa_dcb->drv_info->cfg->rx_cfg.rxq_cnt; i++) {
		struct dpmaif_rxq *rxq = &noa_dcb->rxqs[i];
#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
		struct dpmaif_rxq_cfg *rxq_cfg = &noa_dcb->drv_info->cfg->rx_cfg.rxqs[i];
#endif

		netif_napi_add_weight(&wcb->dummy_dev, &rxq->napi,
			data_blk->hif_ops->poll, MTK_NAPI_POLL_WEIGHT);

		NOA_MD_INFO("NOA NAPI[%d] inited for dev %s, poll_func=%ps",
			i, wcb->dummy_dev.name, data_blk->hif_ops->poll);

#if IS_ENABLED(CONFIG_NOA_MD_DEBUG_DISABLE_DYNAMIC_SWITCH)
		// The default NAPI was set by mtk_wwan_gro_napi_init for GRO.
		// For low latency, we use the new NOA NAPI instead.
		if (rxq_cfg->attr & DATAQ_ATTR_LOW_LATENCY) {
			NOA_MD_INFO("Replacing WCB GRO NAPI[%d] with NOA low-latency NAPI (0x%p)",
				i, &rxq->napi);

			wcb->gro_napis[i] = &rxq->napi;
		}
#endif
	}

	return 0;
}

void noa_md_dpmaif_rx_napi_exit(struct noa_md_dev *p_md_dev)
{
	struct mtk_dpmaif_ctlb *noa_dcb;
	noa_dcb = p_md_dev->noa_dcb;
	for (int i = 0; i < noa_dcb->drv_info->cfg->rx_cfg.rxq_cnt; i++) {
		struct dpmaif_rxq *rxq = &noa_dcb->rxqs[i];
		netif_napi_del(&rxq->napi);
	}
}

static u8 noa_md_wwan_get_napi_thread_affinity(
	struct mtk_dpmaif_ctlb *noa_dcb,
	unsigned char napi_id)
{
#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
	return noa_dcb->aff_cfg[DATA_DEFAULT_AFF_MODE].napi_thrd_aff[napi_id];
#else
	return NULL;
#endif
}

static void noa_md_wwan_set_napi_thread_prio(struct dpmaif_rxq *rxq)
{
	struct napi_struct *napi = &rxq->napi;

	/* Set static priority for napi thread of low latency queue */
	if (rxq->attr & DATAQ_ATTR_LOW_LATENCY)
		sched_set_fifo(napi->thread);
}

static void noa_md_wwan_set_napi_thread_affinity(
	struct mtk_dpmaif_ctlb *noa_dcb,
	struct dpmaif_rxq *rxq,
	unsigned char napi_id)
{
	struct napi_struct *napi = &rxq->napi;
	u32 napi_thread_affinity;
	cpumask_var_t mask;

	if (!noa_dcb->aff_cfg || !napi->thread) {
		NOA_MD_ERROR("noa_dcb->aff_cfg = 0x%llx, napi->thread = 0x%llx",
			(u64)noa_dcb->aff_cfg, (u64)napi->thread);
		return;
	}

	if (!alloc_cpumask_var(&mask, GFP_KERNEL)) {
		NOA_MD_ERROR("Failed to alloc cpumask var");
		return;
	}

	napi_thread_affinity = noa_md_wwan_get_napi_thread_affinity(noa_dcb, napi_id);
	cpumask_clear(mask);
	cpumask_set_cpu(napi_thread_affinity, mask);
	set_cpus_allowed_ptr(napi->thread, mask);

	free_cpumask_var(mask);
}

int noa_md_dpmaif_rx_napi_enable(struct noa_md_dev *p_md_dev)
{
	struct mtk_data_blk *data_blk;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct mtk_wwan_ctlb *wcb;
	int i;
	int ret;

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);
	data_blk = noa_dcb->data_blk;
	CHECK_PTR_OR_RETURN_ERR(data_blk, -EINVAL);
	wcb = data_blk->wcb;
	CHECK_PTR_OR_RETURN_ERR(wcb, -EINVAL);

	if (atomic_cmpxchg(&p_md_dev->dpa_napi_enabled, 0, 1) == 0) {
		ret = dev_set_threaded(&wcb->dummy_dev, true);
		NOA_MD_INFO("dev_set_threaded return = %d", ret);
		for (i = 0; i < noa_dcb->drv_info->cfg->rx_cfg.rxq_cnt; i++) {
			struct dpmaif_rxq *rxq = &noa_dcb->rxqs[i];
			rxq->started = true;
			if (!ret) {
				noa_md_wwan_set_napi_thread_affinity(noa_dcb, rxq, i);
				noa_md_wwan_set_napi_thread_prio(rxq);
			}
			napi_enable(&rxq->napi);
		}
	}

	return 0;
}

int noa_md_dpmaif_rx_napi_disable(struct noa_md_dev *p_md_dev)
{
	int i;
	struct mtk_data_blk *data_blk;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct mtk_wwan_ctlb *wcb;

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);
	data_blk = noa_dcb->data_blk;
	CHECK_PTR_OR_RETURN_ERR(data_blk, -EINVAL);
	wcb = data_blk->wcb;
	CHECK_PTR_OR_RETURN_ERR(wcb, -EINVAL);

	NOA_MD_INFO("dpa_napi_enabled=[%d]", atomic_read(&p_md_dev->dpa_napi_enabled));
	if (atomic_cmpxchg(&p_md_dev->dpa_napi_enabled, 1, 0) == 1) {
		for (i = 0; i < noa_dcb->drv_info->cfg->rx_cfg.rxq_cnt; i++) {
			struct dpmaif_rxq *rxq = &noa_dcb->rxqs[i];
			napi_synchronize(&rxq->napi);
			napi_disable(&rxq->napi);
		}
	}

	return 0;
}

int noa_md_dpmaif_task_resume(struct noa_md_dev *p_md_dev)
{
	struct noa_md_dpmaif_ops* noa_dpmaif_ops = noa_md_wpr_dpmaif_get_dpmaif_ops();
	struct mtk_dpmaif_ctlb *noa_dcb;

	NOA_MD_INFO("enter, p_md_dev=0x%p", p_md_dev);

	noa_dcb = p_md_dev->noa_dcb;
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, -EINVAL);

	noa_dpmaif_ops->task_resume(noa_dcb);

	return 0;
}

int noa_md_dpmaif_hw_lro_set(struct noa_md_dev *p_md_dev, bool lro_enable)
{
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct dpmaif_drv_info *drv_info;
	int ret;

	dpmaif_dcb = p_md_dev->dpmaif_dcb;
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, -EINVAL);
	drv_info = dpmaif_dcb->drv_info;
	CHECK_PTR_OR_RETURN_ERR(drv_info, -EINVAL);

	NOA_MD_INFO("lro_enable=[%d]", lro_enable);
	ret = drv_info->drv_ops->feature_cmd(drv_info, DATA_HW_LRO_SET, &lro_enable);

	return ret;
}

/**
 * noa_md_noa_dcb_init() - Initializes the software DCB for the NOA driver.
 *
 * This function creates a software 'shadow' of the main hardware-facing
 * DPMAIF DCB. It shares core resources but replaces its drv_ops to route
 * data through the NOA/NCP software path instead of direct hardware access.
 *
 * @p_md_dev: The main NOA device to create the DCB for.
 * Return: Pointer to the initialized DCB on success, or an ERR_PTR on failure.
 */
struct mtk_dpmaif_ctlb *noa_md_noa_dcb_init(struct noa_md_dev *p_md_dev)
{
	struct mtk_md_dev *mdev;
	struct mtk_dpmaif_ctlb *dpmaif_dcb;
	struct mtk_dpmaif_ctlb *noa_dcb;
	struct mtk_data_blk *data_blk;
	struct noa_md_dpmaif_ops *dpmaif_ops;
	int ret = 0;

	NOA_MD_INFO("enter, p_md_dev=0x%p", p_md_dev);

	CHECK_PTR_OR_RETURN_ERR(p_md_dev, ERR_PTR(-EINVAL));

	mdev = p_md_dev->mdev;
	CHECK_PTR_OR_RETURN_ERR(mdev, ERR_PTR(-EINVAL));

	dpmaif_dcb = MDEV_TO_DCB(mdev);
	CHECK_PTR_OR_RETURN_ERR(dpmaif_dcb, ERR_PTR(-EINVAL));

	data_blk = MDEV_TO_DATA_BLK(mdev);
	CHECK_PTR_OR_RETURN_ERR(data_blk, ERR_PTR(-EINVAL));

	dpmaif_ops = noa_md_wpr_dpmaif_get_dpmaif_ops();
	CHECK_PTR_OR_RETURN_ERR(dpmaif_ops, ERR_PTR(-EINVAL));

	if (!dpmaif_ops->initialized) {
		NOA_MD_ERROR("dpmaif_ops not initialized");
		return ERR_PTR(-EINVAL);
	}

	noa_dcb = kzalloc(sizeof(*noa_dcb), GFP_KERNEL);
	CHECK_PTR_OR_RETURN_ERR(noa_dcb, ERR_PTR(-EINVAL));

	// Update noa_dcb
	p_md_dev->noa_dcb = noa_dcb;

	noa_dcb->dpmaif_rx_legacy = dpmaif_dcb->dpmaif_rx_legacy;

#if IS_ENABLED(CONFIG_DATA_CPU_LOADING_OPTIMIZE)
	noa_dcb->aff_cfg = dpmaif_dcb->aff_cfg;
#endif
	noa_dcb->data_blk = data_blk;
	mutex_init(&noa_dcb->trans_ctl_lock);
	NOA_MD_INFO("noa_dcb=[0x%p], noa_dcb->data_blk=[0x%p]", noa_dcb, noa_dcb->data_blk);

	ret = dpmaif_ops->drv_res_init(noa_dcb);
	if (ret) {
		NOA_MD_ERROR("drv_res_init=[%d]", ret);
		goto error;
	}

	noa_md_dpmaif_dcb_set_ops(p_md_dev);

	ret = dpmaif_ops->sw_res_init(noa_dcb);
	if (ret) {
		NOA_MD_ERROR("sw_res_init=[%d]", ret);
		goto error;
	}

	ret = dpmaif_ops->tx_srvs_init(noa_dcb);
	if (ret) {
		NOA_MD_ERROR("tx_srvs_init=[%d]", ret);
		goto error;
	}

	ret = dpmaif_ops->doorbell_task_start(noa_dcb);
	if (ret) {
		NOA_MD_ERROR("doorbell_task_start=[%d]", ret);
		goto error;
	}

	ret = dpmaif_ops->tx_srvs_start(noa_dcb);
	if (ret) {
		NOA_MD_ERROR("tx_srvs_start=[%d]", ret);
		goto error;
	}

	// Update irq_params from dpmaif to noa
	noa_dcb->irq_params = dpmaif_dcb->irq_params;

	return noa_dcb;

error:
	kfree(noa_dcb);
	p_md_dev->noa_dcb = NULL;
	return ERR_PTR(-EINVAL);
}

void noa_md_dpmaif_dcb_deinit(struct mtk_dpmaif_ctlb *noa_dcb)
{
	// TODO: Add deinit function
	if (IS_ERR_OR_NULL(noa_dcb)) {
		NOA_MD_ERROR("noa_dcb is null");
		return;
	}

	kfree(noa_dcb);
}
