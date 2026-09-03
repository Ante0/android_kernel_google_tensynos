#ifndef __NOA_MD_DPMAIF_H__
#define __NOA_MD_DPMAIF_H__

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_FULLSOC_SUPPORT)
#include "t900/noa_md_mtk_priv_fullsoc.h"
#else
#include "mtk_data_plane.h"
#endif

#define MDEV_TO_DCB(mdev) ({ \
	struct mtk_dpmaif_ctlb *__ret = NULL; \
	if (mdev) { \
		__ret = \
		((struct mtk_data_blk *)mdev->data_blk)->dcb; \
	} \
	__ret; \
})

#define MDEV_TO_DATA_BLK(mdev) ({ \
	struct mtk_data_blk *__ret = NULL; \
	if (mdev) { \
		__ret = \
		(struct mtk_data_blk *)mdev->data_blk; \
	} \
	__ret; \
})

#define NOA_MSG_IN_TCP_SLOW_START BIT(3)

int noa_md_dpmaif_send_doorbell(
	struct dpmaif_drv_info *drv_info,
	enum dpmaif_drv_ring_type type,
	u8 q_id, u32 cnt);
void noa_md_dpmaif_fill_tx_info(void *drb, struct dpmaif_tx_info *tx_info, int type);
int noa_md_dpmaif_drv_get_ring_idx(struct dpmaif_drv_info *drv_info,
	enum dpmaif_drv_ring_idx index, u8 q_id);
int noa_md_dpmaif_get_rx_info(void *pit, struct dpmaif_rx_info *rx_info, u32 pit_seq_expect,
	u8 q_id);
int noa_md_dpmaif_rx_napi_init(struct noa_md_dev *p_noa_dev);
void noa_md_dpmaif_rx_napi_exit(struct noa_md_dev *p_noa_dev);
int noa_md_dpmaif_rx_napi_enable(struct noa_md_dev *p_noa_dev);
int noa_md_dpmaif_rx_napi_disable(struct noa_md_dev *p_noa_dev);
int noa_md_dpmaif_task_resume(struct noa_md_dev *p_noa_dev);
int noa_md_dpmaif_hw_lro_set(struct noa_md_dev *p_md_dev, bool lro_enable);
struct mtk_dpmaif_ctlb *noa_md_noa_dcb_init(struct noa_md_dev *p_noa_dev);
void noa_md_dpmaif_dcb_deinit(struct mtk_dpmaif_ctlb *noa_dcb);

#endif // __NOA_MD_DPMAIF_H__
