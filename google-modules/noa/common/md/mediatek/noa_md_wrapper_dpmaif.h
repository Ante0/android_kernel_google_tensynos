#ifndef __NOA_MD_WRAPPER_DPMAIF_H__
#define __NOA_MD_WRAPPER_DPMAIF_H__

#include <linux/skbuff.h>

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "t900/noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_dev.h"
#endif

#define NOA_MD_RX_NOA_NORMAL_BAT0_BASE 0
#define NOA_MD_RX_NOA_FRAG_BAT0_BASE 32768
#define NOA_MD_RX_NOA_NORMAL_BAT1_BASE 40960
#define NOA_MD_RX_NOA_FRAG_BAT1_BASE 41984
#define NOA_MD_RX_NOA_MAX_BAT_BASE 43008

struct noa_md_dpmaif_ops {
	int (*drv_res_init)(void *data);
	int (*sw_res_init)(void *data);
	int (*tx_srvs_init)(void *data);
	int (*doorbell_task_start)(void *data);
	int (*tx_srvs_start)(void *data);
	void (*irq_tx_done)(void *data, unsigned int q_mask);
	void (*irq_rx_done)(void *data, unsigned int q_mask);
	void (*task_resume)(void *data);
	int (*trans_enable)(void *data);
	int (*trans_disable)(void *data);
	void (*flush_bat_reload)(void *data);
	void (*sw_stop_rx)(void *data);
	bool initialized;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	// Debug-only operation to retrieve the mdev instance
	struct mtk_md_dev* (*get_mdev)(void);
#endif
};

struct noa_md_dpmaif_ops* noa_md_wpr_dpmaif_get_dpmaif_ops(void);
struct mtk_dpmaif_ctlb* noa_md_wpr_dpmaif_get_noa_dcb(void);
bool noa_md_wpr_dpmaif_is_noa_dcb(struct mtk_dpmaif_ctlb *dcb);
int noa_md_wpr_send_vpn_skb(struct sk_buff *skb, unsigned int queue_idx);
unsigned int noa_md_wpr_rx_tkid_remap(int bat_id, int bat_type, unsigned int rx_tkid);
void noa_md_wpr_tx_update_tkid_queues(dma_addr_t dma_addr, int txq_id);
#endif // __NOA_MD_WRAPPER_DPMAIF_H__
