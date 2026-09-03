#include "t900/noa_md_mtk_priv.h"
#include "noa_md.h"
#include "noa_md_tx_data.h"
#include "noa_md_vpn_tx_data.h"
#include "noa_md_wrapper_dpmaif.h"

extern struct noa_md_dev md_dev;

struct noa_md_dpmaif_ops g_noa_dpmaif_ops = {
	.drv_res_init = NULL,
	.sw_res_init = NULL,
	.tx_srvs_init = NULL,
	.doorbell_task_start = NULL,
	.tx_srvs_start = NULL,
	.irq_tx_done = NULL,
	.irq_rx_done = NULL,
	.task_resume = NULL,
	.trans_enable = NULL,
	.trans_disable = NULL,
	.flush_bat_reload = NULL,
	.sw_stop_rx = NULL,
	.initialized = false,
};

struct noa_md_dpmaif_ops* noa_md_wpr_dpmaif_get_dpmaif_ops(void)
{
	return &g_noa_dpmaif_ops;
}
EXPORT_SYMBOL(noa_md_wpr_dpmaif_get_dpmaif_ops);

struct mtk_dpmaif_ctlb* noa_md_wpr_dpmaif_get_noa_dcb(void)
{
	return md_dev.noa_dcb;
}
EXPORT_SYMBOL(noa_md_wpr_dpmaif_get_noa_dcb);

bool noa_md_wpr_dpmaif_is_noa_dcb(struct mtk_dpmaif_ctlb *dcb)
{
	if (md_dev.noa_dcb == dcb) {
		return true;
	}
	return false;
}
EXPORT_SYMBOL(noa_md_wpr_dpmaif_is_noa_dcb);

int noa_md_wpr_send_vpn_skb(struct sk_buff *skb, unsigned int queue_idx)
{
	struct noa_md_tx *tx = &md_dev.tx;
	if (!skb) {
		return -EINVAL;
	}

	return noa_md_vpn_tx_enqueue(tx, skb, queue_idx);
}
EXPORT_SYMBOL(noa_md_wpr_send_vpn_skb);

unsigned int noa_md_wpr_rx_tkid_remap(int bat_id, int bat_type, unsigned int rx_tkid)
{
	unsigned int mapped_rx_tkid = rx_tkid;
	switch (bat_type) {
	case NORMAL_BAT:
		if (bat_id == DPMAIF_BAT0) {
			mapped_rx_tkid = rx_tkid;
		} else if (bat_id == DPMAIF_BAT1) {
			mapped_rx_tkid = rx_tkid + NOA_MD_RX_NOA_NORMAL_BAT1_BASE;
		} else {
			NOA_MD_RX_ERROR("Invalid bat_id(%d) for NORMAL_BAT", bat_id);
		}
		break;
	case FRAG_BAT:
		if (bat_id == DPMAIF_BAT0) {
			mapped_rx_tkid = rx_tkid + NOA_MD_RX_NOA_FRAG_BAT0_BASE;
		} else if (bat_id == DPMAIF_BAT1) {
			mapped_rx_tkid = rx_tkid + NOA_MD_RX_NOA_FRAG_BAT1_BASE;
		} else {
			NOA_MD_RX_ERROR("Invalid bat_id(%d) for FRAG_BAT", bat_id);
		}
		break;
	default:
		NOA_MD_RX_ERROR("Invalid bat_type(%d)", bat_type);
		break;
	}

	return mapped_rx_tkid;
}
EXPORT_SYMBOL_GPL(noa_md_wpr_rx_tkid_remap);

void noa_md_wpr_tx_update_tkid_queues(dma_addr_t dma_addr, int txq_id)
{
	noa_md_tx_update_tkid_queues(dma_addr, txq_id);
}
EXPORT_SYMBOL_GPL(noa_md_wpr_tx_update_tkid_queues);
