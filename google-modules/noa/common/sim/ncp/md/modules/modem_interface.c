#include "modem_interface.h"

// Refer to mtk_dpmaif_drv_dl_get_pit_wridx
int noa_ncp_md_rx_dpmaif_drv_dl_get_pit_wridx(
	struct dpmaif_drv_info *drv_info, u32 qno)
{
	u32 pit_wridx;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		pit_wridx = noa_ncp_md_pci_read32(
			drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_AO_DL_PIT_STA3) & DPMAIF_DL_PIT_WRIDX_MSK;
	} else {
		if (qno != DPMAIF_DLQ2) {
			pit_wridx = noa_ncp_md_pci_read32(
				drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AO_DL_LRO_STA5 + qno * DPMAIF_DL_LRO_STA_OFFSET)
				& DPMAIF_DL_PIT_WRIDX_MSK;
		} else {
			pit_wridx = noa_ncp_md_pci_read32(
				drv_info->mdev, drv_info->regs->pd2_base +
				DPMAIF_DL_2_STA13) & DPMAIF_DL_PIT_WRIDX_MSK;
		}
	}

	if (unlikely(pit_wridx >= drv_info->cfg->rx_cfg.rxqs[qno].pit_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return pit_wridx;
}

// Refer to mtk_dpmaif_drv_dl_get_pit_rdidx
int noa_ncp_md_rx_dpmaif_drv_dl_get_pit_rdidx(
	struct dpmaif_drv_info *drv_info, u32 qno)
{
	u32 pit_rdidx;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		pit_rdidx =  noa_ncp_md_pci_read32(
			drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_AO_DL_PIT_STA2) & DPMAIF_DL_PIT_WRIDX_MSK;
	} else {
		if (qno != DPMAIF_DLQ2)
			pit_rdidx = noa_ncp_md_pci_read32(
				drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AO_DL_LRO_STA6 + qno * DPMAIF_DL_LRO_STA_OFFSET) &
				DPMAIF_DL_PIT_WRIDX_MSK;
		else
			pit_rdidx = noa_ncp_md_pci_read32(
				drv_info->mdev, drv_info->regs->pd2_base +
				DPMAIF_DL_2_STA14) & DPMAIF_DL_PIT_WRIDX_MSK;
	}

	if (unlikely(pit_rdidx >= drv_info->cfg->rx_cfg.rxqs[qno].pit_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return pit_rdidx;
}

// Refer to mtk_dpmaif_drv_dl_get_bat_ridx
int noa_ncp_md_rx_dpmaif_drv_dl_get_bat_ridx(
		struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	u32 bat_ridx;

	if (bat_id == DPMAIF_BAT0)
		bat_ridx = noa_ncp_md_pci_read32(drv_info->mdev,
					  drv_info->regs->pd_base + DPMAIF_AO_DL_BAT_STA2) &
				DPMAIF_DL_BAT_WRIDX_MSK;
	else
		bat_ridx = noa_ncp_md_pci_read32(drv_info->mdev,
					  drv_info->regs->pd2_base + DPMAIF_DL_2_STA4) &
				DPMAIF_DL_2_BAT_WRIDX_MSK;

	if (unlikely(bat_ridx >= drv_info->cfg->rx_cfg.bats[bat_id].bat_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return bat_ridx;
}

// Refer to mtk_dpmaif_drv_dl_get_bat_wridx
int noa_ncp_md_rx_dpmaif_drv_dl_get_bat_wridx(
	struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	u32 bat_wridx;

	if (bat_id == DPMAIF_BAT0)
		bat_wridx = noa_ncp_md_pci_read32(drv_info->mdev,
					   drv_info->regs->pd_base + DPMAIF_AO_DL_BAT_STA3) &
					   DPMAIF_DL_BAT_WRIDX_MSK;
	else
		bat_wridx = (noa_ncp_md_pci_read32(drv_info->mdev,
			   drv_info->regs->pd2_base + DPMAIF_DL_2_STA4) >> 16) &
			   DPMAIF_DL_2_BAT_WRIDX_MSK;

	if (unlikely(bat_wridx >= drv_info->cfg->rx_cfg.bats[bat_id].bat_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return bat_wridx;
}

// Refer to mtk_dpmaif_drv_dl_get_frg_ridx
int noa_ncp_md_rx_dpmaif_drv_dl_get_frg_ridx(
	struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	u32 frg_ridx;

	if (bat_id == DPMAIF_BAT0)
		frg_ridx = noa_ncp_md_pci_read32(drv_info->mdev,
					  drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_STA2) &
				DPMAIF_DL_FRG_WRIDX_MSK;
	else
		frg_ridx = noa_ncp_md_pci_read32(drv_info->mdev,
					  drv_info->regs->pd2_base + DPMAIF_DL_2_FRG_STA4) &
			DPMAIF_DL_2_BAT_WRIDX_MSK;

	if (unlikely(frg_ridx >= drv_info->cfg->rx_cfg.frags[bat_id].bat_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return frg_ridx;
}

// Refer to mtk_dpmaif_drv_ul_get_drb_ridx
int noa_ncp_md_tx_get_drb_ridx(
		struct dpmaif_drv_info *drv_info, u8 qno)
{
	u32 drb_ridx;
	u64 addr;

	NCP_MD_TX_INFO("Enter");
	addr =
		drv_info->regs->pd_base + drv_info->regs->ao_ul_ch0_sta + DPMAIF_UL_DRB_ENTRY_WORD * qno;

	drb_ridx = noa_ncp_md_pci_read32(drv_info->mdev, addr) >> 16;
	drb_ridx = drb_ridx / DPMAIF_UL_DRB_ENTRY_WORD;
	NCP_MD_TX_INFO("return drb_ridx:%u", drb_ridx);
	return drb_ridx;
}

int noa_ncp_md_dpmaif_get_ring_idx(
		struct dpmaif_drv_info *drv_info,
		enum dpmaif_drv_ring_idx index, u8 q_id)
{
	int ret = 0;
	NCP_MD_INFO("enter, index:%u", (int)index);
	switch (index) {
		case DPMAIF_PIT_WIDX:
			ret = noa_ncp_md_rx_dpmaif_drv_dl_get_pit_wridx(drv_info, q_id);
			break;
		case DPMAIF_PIT_RIDX:
			ret = noa_ncp_md_rx_dpmaif_drv_dl_get_pit_rdidx(drv_info, q_id);
			break;
		case DPMAIF_BAT_WIDX:
			ret = noa_ncp_md_rx_dpmaif_drv_dl_get_bat_wridx(drv_info, q_id);
			break;
		case DPMAIF_BAT_RIDX:
			ret = noa_ncp_md_rx_dpmaif_drv_dl_get_bat_ridx(drv_info, q_id);
			break;
		case DPMAIF_FRAG_WIDX:
			break;
		case DPMAIF_FRAG_RIDX:
			ret = noa_ncp_md_rx_dpmaif_drv_dl_get_frg_ridx(drv_info, q_id);
			break;
		case DPMAIF_DRB_WIDX:
			break;
		case DPMAIF_DRB_RIDX:
			ret = noa_ncp_md_tx_get_drb_ridx(drv_info, q_id);
			break;
		default:
			break;
	}
	NCP_MD_INFO("ret:%u", ret);
	return ret;
}

void noa_ncp_md_dpmaif_irq_handle(
		enum dpmaif_drv_intr_type type, unsigned int q_mask)
{
	NCP_MD_DATA_LIMIT(
		"enter, type=[%d], q_mask=[%d]", type, q_mask);
	switch (type) {
		case DPMAIF_INTR_UL_DONE:
			noa_ncp_md_tx_irq_tx_done(g_md_fw, q_mask);
			break;
		case DPMAIF_INTR_DL_BATCNT_LEN_ERR:
		case DPMAIF_INTR_DL_FRGCNT_LEN_ERR:
		case DPMAIF_INTR_DL_PITCNT_LEN_ERR:
		case DPMAIF_INTR_DL_DONE:
			// TODO: Call noa_ncp_md_rx_dpmaif_irq_rx_done(md_fw, q_mask)?
			// NOA md data rx step 10: According to the interrupt type,
			// distribute it to more detailed processing functions within the NCP
			noa_ncp_md_rx_dpmaif_event_handle(g_md_fw, type, q_mask);
			break;
		default:
			break;
	}
}
EXPORT_SYMBOL_GPL(noa_ncp_md_dpmaif_irq_handle);
