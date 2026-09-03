#ifndef __MODEM_INTERFACE_H__
#define __MODEM_INTERFACE_H__

#include "sys_common.h"

#define DPMAIF_DL_LRO_STA_OFFSET 0x20

enum {
	NOA_MD_DPMAIF_PIT = 0,
	NOA_MD_DPMAIF_BAT = 1,
	NOA_MD_DPMAIF_FRAG_BAT = 2,
	NOA_MD_DPMAIF_DRB = 3,
};

enum {
	NOA_MD_DPMAIF_PIT_WIDX = 0,
	NOA_MD_DPMAIF_PIT_RIDX = 1,
	NOA_MD_DPMAIF_BAT_WIDX = 2,
	NOA_MD_DPMAIF_BAT_RIDX = 3,
	NOA_MD_DPMAIF_FRAG_WIDX = 4,
	NOA_MD_DPMAIF_FRAG_RIDX = 5,
	NOA_MD_DPMAIF_DRB_WIDX = 6,
	NOA_MD_DPMAIF_DRB_RIDX = 7,
};

enum {
	NOA_MD_DPMAIF_INTR_MIN = 0,
	/* uplink part */
	NOA_MD_DPMAIF_INTR_UL_DONE = 1,
	NOA_MD_DPMAIF_INTR_UL_DRB_EMPTY = 2,
	NOA_MD_DPMAIF_INTR_UL_MD_NOTREADY = 3,
	NOA_MD_DPMAIF_INTR_UL_MD_PWR_NOTREADY = 4,
	NOA_MD_DPMAIF_INTR_UL_LEN_ERR = 5,
	/* downlink part */
	NOA_MD_DPMAIF_INTR_DL_LEGACY_DONE = 6,
	NOA_MD_DPMAIF_INTR_DL_SKB_LEN_ERR = 7,
	NOA_MD_DPMAIF_INTR_DL_BATCNT_LEN_ERR = 8,
	NOA_MD_DPMAIF_INTR_DL_PKT_EMPTY_SET = 9,
	NOA_MD_DPMAIF_INTR_DL_FRG_EMPTY_SET = 10,
	NOA_MD_DPMAIF_INTR_DL_MTU_ERR = 11,
	NOA_MD_DPMAIF_INTR_DL_FRGCNT_LEN_ERR = 12,
	NOA_MD_DPMAIF_INTR_DL_PITCNT_LEN_ERR = 13,
	NOA_MD_DPMAIF_INTR_DL_HPC_ENT_TYPE_ERR = 14,
	NOA_MD_DPMAIF_INTR_DL_DONE = 15,
	/* traffic sync */
	NOA_MD_DPMAIF_INTR_TRAS_SYNC = 16,
	NOA_MD_DPMAIF_INTR_MAX
};

int noa_ncp_md_rx_dpmaif_drv_dl_get_pit_wridx(
		struct dpmaif_drv_info *drv_info, u32 qno);
int noa_ncp_md_rx_dpmaif_drv_dl_get_pit_rdidx(
		struct dpmaif_drv_info *drv_info, u32 qno);
int noa_ncp_md_rx_dpmaif_drv_dl_get_bat_ridx(
		struct dpmaif_drv_info *drv_info, u8 bat_id);
int noa_ncp_md_rx_dpmaif_drv_dl_get_bat_wridx(
		struct dpmaif_drv_info *drv_info, u8 bat_id);
int noa_ncp_md_rx_dpmaif_drv_dl_get_frg_ridx(
		struct dpmaif_drv_info *drv_info, u8 bat_id);
int noa_ncp_md_tx_get_drb_ridx(
		struct dpmaif_drv_info *drv_info, u8 qno);
int noa_ncp_md_dpmaif_get_ring_idx(
		struct dpmaif_drv_info *drv_info,
		enum dpmaif_drv_ring_idx index, u8 q_id);

#define NOA_MD_SEND_DOORBELL(ring_type, queue_id, count) \
    g_md_fw->drv_info->drv_ops->send_doorbell(g_md_fw->drv_info, ring_type, queue_id, count)
#define NOA_MD_INTERRUPT_COMPLETE(intr_type, queue_id, data) \
    g_md_fw->drv_info->drv_ops->intr_complete(g_md_fw->drv_info, intr_type, queue_id, data)
#define NOA_MD_GET_RING_INDEX(ring_index, queue_id) \
    noa_ncp_md_dpmaif_get_ring_idx(g_md_fw->drv_info, ring_index, queue_id)
#endif /* __MODEM_INTERFACE_H__ */
