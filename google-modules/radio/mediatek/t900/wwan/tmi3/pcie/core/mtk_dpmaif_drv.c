// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#define pr_fmt(fmt) "DATA_DRV:" fmt

#include <linux/delay.h>
#include <linux/random.h>

#include "mtk_data_plane.h"
#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_dpmaif_drv.h"
#include "mtk_dpmaif_reg.h"
#include "mtk_pci.h"
#include "mtk_pci_reg.h"
#include "mtk_pm.h"

#ifdef CONFIG_UT_PCIE_DPMAIF_DRV
#include "ut_dpmaif_drv.h"
#endif

#define TAG "DATA_DRV"

static int mtk_dpmaif_drv_hash_sec_key_set(struct dpmaif_drv_info *drv_info, u8 *hash_key);
static void mtk_dpmaif_drv_hash_indir_mask_set(struct dpmaif_drv_info *drv_info, u32 mask);
static void mtk_dpmaif_drv_intr_coalesce_set(struct dpmaif_drv_info *drv_info,
					     struct dpmaif_drv_intr *intr);
static void mtk_dpmaif_drv_hash_sec_key_get(struct dpmaif_drv_info *drv_info, u8 *hash_key);
static void mtk_dpmaif_drv_hash_indir_get(struct dpmaif_drv_info *drv_info, u32 *indir);
static void mtk_dpmaif_drv_hash_indir_set(struct dpmaif_drv_info *drv_info, u32 *indir);
static void mtk_dpmaif_drv_lro_set(struct dpmaif_drv_info *drv_info, void *data);
static int mtk_dpmaif_drv_ul_set_delay_intr(struct dpmaif_drv_info *drv_info, u8 q_num, u8 mode,
					    u32 time_us, u32 pkt_cnt);
static int mtk_dpmaif_drv_dl_set_delay_intr(struct dpmaif_drv_info *drv_info, u8 q_num, u8 mode,
					    u32 time_us, u32 pkt_cnt);
static void mtk_dpmaif_drv_clr_dlq_timeout_threshold(struct dpmaif_drv_info *drv_info);
static int mtk_dpmaif_drv_dl_add_bat_cnt(struct dpmaif_drv_info *drv_info,
					 u8 bat_id, u32 bat_entry_cnt);
static int mtk_dpmaif_drv_dl_add_frg_cnt(struct dpmaif_drv_info *drv_info,
					 u8 bat_id, u32 frg_entry_cnt);

u32 mtk_dpmaif_drv_get_ul_intr_mask(struct dpmaif_drv_info *drv_info)
{
	if (drv_info->priv_ops && drv_info->priv_ops->get_ul_intr_mask)
		return drv_info->priv_ops->get_ul_intr_mask(drv_info);

	return mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TIMR0);
}

u32 mtk_dpmaif_drv_get_dl_intr_mask(struct dpmaif_drv_info *drv_info, u8 q_id)
{
	if (q_id != DPMAIF_DLQ2)
		return mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base +
				DPMAIF_PD_AP_DL_L2TIMR0);
	else
		return mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_MISC_PD_APDL12_MASK_RW);
}

static int mtk_dpmaif_drv_init_mode(struct dpmaif_drv_info *drv_info)
{
	u32 val, cnt = 0;
	int ret;

	/* Initialize dpmaif sram. */
	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_MEM_CLR);
	val |= DPMAIF_MEM_CLR_MASK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_MEM_CLR, val);

	do {
		if (!(mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_MEM_CLR) &
				     DPMAIF_MEM_CLR_MASK))
			break;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to initialize sram\n");
		return -DATA_HW_REG_TIMEOUT;
	}

	if (drv_info->priv_ops && drv_info->priv_ops->dynamic_sram_init) {
		ret = drv_info->priv_ops->dynamic_sram_init(drv_info);
		if (unlikely(ret < 0))
			return ret;
	}

	/* Set DPMAIF AP port mode. */
	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES);
	val &= ~DPMAIF_PORT_MODE_MSK;
	val |= DPMAIF_PORT_MODE_PCIE;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES, val);

	/* Set CG enable. */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_CG_EN, 0x7F);

	/* Config SW PCIe mode. */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_UL_RESERVE_AO_RW,
			DPMAIF_PCIE_MODE_SET_VALUE);
	if (drv_info->priv_ops && drv_info->priv_ops->set_pcie_domain)
		drv_info->priv_ops->set_pcie_domain(drv_info);

	return 0;
}

int mtk_dpmaif_drv_ul_intr_init(struct dpmaif_drv_info *drv_info)
{
	const struct dpmaif_intr_cfg *intr_cfg = &drv_info->cfg->intr_cfg;
	u32 cnt = 0;

	/* clear UL interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISAR0,
			0xFFFFFFFF);
	/* unmask ul_l2intrs_enable interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TICR0,
			intr_cfg->ul_l2intrs_enable);
	/* mask ul_l2intrs_disable interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TISR0,
			intr_cfg->ul_l2intrs_disable);
	mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TISR0);
	do {
		if (((mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TIMR0) &
					  intr_cfg->ul_l2intrs_disable) ==
					  intr_cfg->ul_l2intrs_disable))
			break;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to set UL interrupt mask, mask=0x%08x\n",
			mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base +
				DPMAIF_PD_AP_UL_L2TIMR0));
		return -DATA_HW_REG_TIMEOUT;
	}

	return 0;
}

static int mtk_dpmaif_drv_init_intr(struct dpmaif_drv_info *drv_info)
{
	const struct dpmaif_intr_cfg *intr_cfg = &drv_info->cfg->intr_cfg;
	int rxq_cnt = drv_info->cfg->rx_cfg.rxq_cnt;
	u32 cnt = 0, cfg;
	int ret;

	if (drv_info->priv_ops && drv_info->priv_ops->ul_intr_init)
		ret = drv_info->priv_ops->ul_intr_init(drv_info);
	else
		ret = mtk_dpmaif_drv_ul_intr_init(drv_info);

	if (unlikely(ret < 0))
		return ret;

	/* clear DLQ0/1 interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_DL_L2TISAR0,
			0xFFFFFFFF);
	/* unmask dl_l2intrs_enable interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TICR0,
			intr_cfg->dl_l2intrs_enable);
	/* mask dl_l2intrs_disable interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TISR0,
			intr_cfg->dl_l2intrs_disable);
	mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TISR0);

	do {
		if (((mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TIMR0) &
				      intr_cfg->dl_l2intrs_disable) ==
				      intr_cfg->dl_l2intrs_disable))
			break;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to set DL interrupt mask, mask=0x%08x\n",
			mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base +
				DPMAIF_PD_AP_DL_L2TIMR0));
		return -DATA_HW_REG_TIMEOUT;
	}

	if (rxq_cnt >= DPMAIF_DLQ2 + 1) {
		/* clear DLQ2 interrupt */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_AP_MISC_APDL_L2TISAR1, 0xFFFFFFFF);
		/* unmask dl2_l2intrs_enable interrupt */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_MISC_PD_APDL12_MASK_CLR,
				intr_cfg->dl2_l2intrs_enable);
		/* mask dl2_l2intrs_disable interrupt */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET, intr_cfg->dl2_l2intrs_disable);
		mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET);
		cnt = 0;
		do {
			if (((mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
					NRL2_DPMAIF_MISC_PD_APDL12_MASK_RW) &
					intr_cfg->dl2_l2intrs_disable) ==
					intr_cfg->dl2_l2intrs_disable))
				break;

			udelay(POLL_INTERVAL_US);
		} while (++cnt < POLL_MAX_TIMES);

		if (cnt >= POLL_MAX_TIMES) {
			MTK_ERR(drv_info->mdev, "Failed to set DL2 interrupt mask, mask=0x%08x\n",
				mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
					NRL2_DPMAIF_MISC_PD_APDL12_MASK_RW));
			return -DATA_HW_REG_TIMEOUT;
		}
	}

	/* init IP busy */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_IP_BUSY, 0xFFFFFFFF);
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DLUL_IP_BUSY_MASK,
			intr_cfg->udl_ip_busy_disable);

	/* init HPC */
	cfg = mtk_pci_read32(drv_info->mdev,
			     drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_AP_L1TIMR0);
	cfg |= intr_cfg->hpc_disable;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_AP_L1TIMR0,
			cfg);
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + NRL2_DPMAIF_AO_DL_IRQ_MASK,
			0xC000000);

	MTK_INFO(drv_info->mdev, "ul_mask=0x%08x, dl_mask=0x%08x, busy_mask=0x%08x, hpc=0x%08x\n",
		 intr_cfg->ul_l2intrs_disable, intr_cfg->dl_l2intrs_disable,
		 intr_cfg->udl_ip_busy_disable, intr_cfg->hpc_disable);

	return 0;
}

static void mtk_dpmaif_drv_set_hpc_cntl(struct dpmaif_drv_info *drv_info)
{
	u32 cfg;

	if (drv_info->priv_ops && drv_info->priv_ops->set_hpc_cntl) {
		drv_info->priv_ops->set_hpc_cntl(drv_info);
		return;
	}

	cfg = DPMAIF_HPC_LRO_PATH_DF & 0x3;
	cfg |= (DPMAIF_HPC_ADD_MODE_DF & 0x3) << 2;
	cfg |= (DPMAIF_HASH_PRIME_DF & 0xF) << 4;
	cfg |= (DPMAIF_HPC_NUM_DF & 0xFF) << 8;

	/* Configuration include hpc dlq path, hpc add mode, hash prime, hpc total number. */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_HPC_CNTL, cfg);
}

static void mtk_dpmaif_drv_set_agg_cfg(struct dpmaif_drv_info *drv_info, bool enable)
{
	u32 cfg;

	cfg = DPMAIF_AGG_MAX_LEN_DF & 0xFFFF;
	cfg |= (DPMAIF_AGG_TBL_ENT_NUM_DF & 0xFFFF) << 16;

	/* Configuration include agg max length, agg table number. */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_LRO_AGG_CFG,
			cfg);
	cfg = mtk_pci_read32(drv_info->mdev,
			     drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_RDY_CHK_FRG_THRES);
	if (enable)
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_AO_DL_RDY_CHK_FRG_THRES, cfg | (0xFF << 20));
	else
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_AO_DL_RDY_CHK_FRG_THRES, cfg & 0xF00FFFFF);
}

static void mtk_dpmaif_drv_set_dlq_timeout_threshold(struct dpmaif_drv_info *drv_info)
{
	u32 val, i;

	for (i = 0; i < DPMAIF_HPC_NUM_DF; i++) {
		val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AO_DL_LROPIT_TIMEOUT1 + ((i >> 1) << 2));

		if (i % 2)
			val = (val & 0xFFFF) | (DPMAIF_LRO_TIMEOUT_THRES_DF << 16);
		else
			val = (val & 0xFFFF0000) | DPMAIF_LRO_TIMEOUT_THRES_DF;

		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AO_DL_LROPIT_TIMEOUT1 + ((i >> 1) << 2), val);
	}

	if (drv_info->priv_ops && drv_info->priv_ops->set_dlq_timeout)
		drv_info->priv_ops->set_dlq_timeout(drv_info);
}

static int mtk_dpmaif_drv_hash_init(struct dpmaif_drv_info *drv_info)
{
	u8 hash_key[DPMAIF_HASH_SEC_KEY_NUM];
	u32 val;
	int ret;

	/* toeplitz hash enable */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_TOE_HASH_EN,
			DPMAIF_TOEPLITZ_HASH_EN);

	/* set hash default value */
	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HASH_CFG_CON);
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HASH_CFG_CON,
			(val & DPMAIF_HASH_DEFAULT_V_MASK) | DPMAIF_HASH_DEFAULT_VALUE);

	get_random_bytes(hash_key, sizeof(hash_key));
	ret = mtk_dpmaif_drv_hash_sec_key_set(drv_info, hash_key);
	if (ret < 0)
		return ret;

	return 0;
}

static void mtk_dpmaif_drv_hpc_stats_init(struct dpmaif_drv_info *drv_info)
{
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HPC_STATS_THRES,
			DPMAIF_HPC_STATS_THRESHOLD);
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HPC_STATS_TIMER_CFG,
			DPMAIF_HPC_STATS_TIMER_CFG);
}

static void mtk_dpmaif_drv_agg_init(struct dpmaif_drv_info *drv_info)
{
	/* set hash bit choose */
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->ao_base + NRL2_DPMAIF_AO_DL_LROPIT_INIT_CON5,
			DPMAIF_LRO_HASH_BIT_CHOOSE_DF & 0x7);

	/* set mid pit timeout threshold */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_LROPIT_TIMEOUT0,
			DPMAIF_MID_TIMEOUT_THRES_DF);

	mtk_dpmaif_drv_set_dlq_timeout_threshold(drv_info);

	/* set dlq start prs threshold */
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->ao_base + NRL2_DPMAIF_AO_DL_LROPIT_TRIG_THRES,
			DPMAIF_LRO_PRS_THRES_DF & 0x3FFFF);
}

static void mtk_dpmaif_drv_tras_init(struct dpmaif_drv_info *drv_info)
{
	u32 val = mtk_pci_read32(drv_info->mdev,
		drv_info->regs->pd_base + DPMAIF_PD_DL_UL_TRAS_INTR_CON);

	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + DPMAIF_PD_DL_UL_TRAS_INTR_CON, val | 0x1);
}

static int mtk_dpmaif_drv_init_features(struct dpmaif_drv_info *drv_info)
{
	int ret;

	if (drv_info->cfg->cap & DATA_HW_F_TRAS_ALIGN)
		mtk_dpmaif_drv_tras_init(drv_info);

	if (drv_info->cfg->cap & DATA_HW_F_HPC)
		mtk_dpmaif_drv_set_hpc_cntl(drv_info);

	if (drv_info->cfg->cap & DATA_HW_F_AGG) {
		if (drv_info->cfg->cap & DATA_HW_F_LRO)
			mtk_dpmaif_drv_set_agg_cfg(drv_info, true);
		else
			mtk_dpmaif_drv_set_agg_cfg(drv_info, false);

		mtk_dpmaif_drv_agg_init(drv_info);
	}

	if (drv_info->cfg->cap & DATA_HW_F_HASH) {
		ret = mtk_dpmaif_drv_hash_init(drv_info);
		if (ret < 0)
			return ret;
	}

	if (drv_info->cfg->cap & DATA_HW_F_HPC_STATS)
		mtk_dpmaif_drv_hpc_stats_init(drv_info);

	return 0;
}

static void mtk_dpmaif_drv_dl_set_ao_remain_minsz(struct dpmaif_drv_info *drv_info,
						  u32 sz)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CONO);
	val &= ~DPMAIF_BAT_REMAIN_MINSZ_MSK;
	val |= ((sz / DPMAIF_BAT_REMAIN_SZ_BASE) << 8) & DPMAIF_BAT_REMAIN_MINSZ_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CONO, val);
}

static void mtk_dpmaif_drv_dl_set_ao_bat_bufsz(struct dpmaif_drv_info *drv_info,
					       u32 buf_sz)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CON2);
	val &= ~DPMAIF_BAT_BUF_SZ_MSK;
	val |= ((buf_sz / DPMAIF_BAT_BUFFER_SZ_BASE) << 8) & DPMAIF_BAT_BUF_SZ_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CON2, val);
}

static void mtk_dpmaif_drv_dl_set_ao_frg_bufsz(struct dpmaif_drv_info *drv_info, u32 buf_sz)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_CHK_THRES);
	val &= ~DPMAIF_FRG_BUF_SZ_MSK;
	val |= ((buf_sz / DPMAIF_FRG_BUFFER_SZ_BASE) << 8) & DPMAIF_FRG_BUF_SZ_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_CHK_THRES, val);
}

static void mtk_dpmaif_drv_dl_set_ao_bat_rsv_length(struct dpmaif_drv_info *drv_info,
						    u32 length)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CON2);
	val &= ~DPMAIF_BAT_RSV_LEN_MSK;
	val |= length & DPMAIF_BAT_RSV_LEN_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CON2, val);
}

static void mtk_dpmaif_drv_dl_set_ao_bid_maxcnt(struct dpmaif_drv_info *drv_info, u32 cnt)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CONO);
	val &= ~DPMAIF_BAT_BID_MAXCNT_MSK;
	val |= (cnt << 16) & DPMAIF_BAT_BID_MAXCNT_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CONO, val);
}

static void mtk_dpmaif_drv_dl_set_pkt_alignment(struct dpmaif_drv_info *drv_info,
						bool enable, u32 mode)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES);
	val &= ~DPMAIF_PKT_ALIGN_MSK;
	if (enable) {
		val |= DPMAIF_PKT_ALIGN_EN;
		val |= (mode << 22) & DPMAIF_PKT_ALIGN_MSK;
	}
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES, val);
}

static void mtk_dpmaif_drv_dl_set_pit_seqnum(struct dpmaif_drv_info *drv_info, u32 seq)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev,
			     drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_PIT_SEQ_END);
	val &= ~DPMAIF_DL_PIT_SEQ_MSK;
	val |= seq & DPMAIF_DL_PIT_SEQ_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_PIT_SEQ_END,
			val);
}

static void mtk_dpmaif_drv_dl_set_ao_mtu(struct dpmaif_drv_info *drv_info, u32 mtu_sz)
{
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CON1,
			mtu_sz);
}

static void mtk_dpmaif_drv_dl_set_ao_pit_chknum(struct dpmaif_drv_info *drv_info,
						u32 number)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CON2);
	val &= ~DPMAIF_PIT_CHK_NUM_MSK;
	val |= (number << 24) & DPMAIF_PIT_CHK_NUM_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PKTINFO_CON2, val);
}

static void mtk_dpmaif_drv_dl_set_ao_bat_check_threshold(struct dpmaif_drv_info *drv_info,
							 u32 size)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES);
	val &= ~DPMAIF_BAT_CHECK_THRES_MSK;
	val |= (size << 16) & DPMAIF_BAT_CHECK_THRES_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES, val);
}

static void mtk_dpmaif_drv_dl_set_ao_frg_check_threshold(struct dpmaif_drv_info *drv_info,
							 u32 size)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_CHK_THRES);
	val &= ~DPMAIF_FRG_CHECK_THRES_MSK;
	val |= size & DPMAIF_FRG_CHECK_THRES_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_CHK_THRES, val);
}

static void mtk_dpmaif_drv_dl_frg_ao_en(struct dpmaif_drv_info *drv_info, bool enable)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_CHK_THRES);
	if (enable)
		val |= DPMAIF_FRG_EN_MSK;
	else
		val &= ~DPMAIF_FRG_EN_MSK;

	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_CHK_THRES, val);
}

static void mtk_dpmaif_drv_dl_set_bat_base_addr(struct dpmaif_drv_info *drv_info,
						u64 addr, u8 bat_id)
{
	u32 lb_addr = (u32)(addr & 0xFFFFFFFF);
	u32 hb_addr = (u32)(addr >> 32);
	u64 addr1, addr2;

	if (bat_id == DPMAIF_BAT0) {
		addr1 = drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_INIT_CON0;
		addr2 = drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_INIT_CON3;
	} else {
		addr1 = drv_info->regs->pd2_base + DPMAIF_DL_2_BAT_INIT_CON0;
		addr2 = drv_info->regs->pd2_base + DPMAIF_DL_2_BAT_INIT_CON3;
	}

	mtk_pci_write32(drv_info->mdev, addr1, lb_addr);
	mtk_pci_write32(drv_info->mdev, addr2, hb_addr);
}

static void mtk_dpmaif_drv_dl_set_bat_size(struct dpmaif_drv_info *drv_info, u32 size, u8 bat_id)
{
	u64 addr;
	u32 val;

	if (bat_id == DPMAIF_BAT0)
		addr = drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_INIT_CON1;
	else
		addr = drv_info->regs->pd2_base + DPMAIF_DL_2_BAT_INIT_CON1;

	val = mtk_pci_read32(drv_info->mdev, addr);
	val &= ~DPMAIF_BAT_SIZE_MSK;
	val |= size & DPMAIF_BAT_SIZE_MSK;
	mtk_pci_write32(drv_info->mdev, addr, val);
}

static void mtk_dpmaif_drv_dl_bat_en(struct dpmaif_drv_info *drv_info, bool enable, u8 bat_id)
{
	u64 addr;
	u32 val;

	if (bat_id == DPMAIF_BAT0)
		addr = drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_INIT_CON1;
	else
		addr = drv_info->regs->pd2_base + DPMAIF_DL_2_BAT_INIT_CON1;

	val = mtk_pci_read32(drv_info->mdev, addr);
	if (enable)
		val |= DPMAIF_BAT_EN_MSK;
	else
		val &= ~DPMAIF_BAT_EN_MSK;

	mtk_pci_write32(drv_info->mdev, addr, val);
	mtk_pci_read32(drv_info->mdev, addr);
}

static int mtk_dpmaif_drv_dl_bat_init_done(struct dpmaif_drv_info *drv_info,
					   bool frag_en, u8 bat_id, u32 init_mode)
{
	u32 cnt = 0, dl_bat_init;
	u64 addr;

	dl_bat_init = init_mode;
	dl_bat_init |= DPMAIF_DL_BAT_INIT_EN;

	if (frag_en)
		dl_bat_init |= DPMAIF_DL_BAT_FRG_INIT;

	if (bat_id == DPMAIF_BAT0)
		addr = drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_INIT;
	else
		addr = drv_info->regs->pd2_base + DPMAIF_DL_2_BAT_INIT;

	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_BAT_INIT_NOT_READY)) {
			mtk_pci_write32(drv_info->mdev, addr, dl_bat_init);
			break;
		}

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev,
			"Failed to initialize bat%d,frag_en=%d,init_mode=%u\n",
			bat_id, frag_en, init_mode);
		return -DATA_HW_REG_TIMEOUT;
	}

	cnt = 0;
	do {
		if (!((mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_BAT_INIT_NOT_READY)))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev,
		"Failed to initialize bat%d done,frag_en=%d,init_mode=%u\n",
		bat_id, frag_en, init_mode);

	return -DATA_HW_REG_TIMEOUT;
}

static void mtk_dpmaif_drv_dl_set_pit_base_addr(struct dpmaif_drv_info *drv_info, u64 addr, u8 q_id)
{
	u32 lb_addr = (u32)(addr & 0xFFFFFFFF);
	u32 hb_addr = (u32)(addr >> 32);

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON0, lb_addr);
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON4, hb_addr);
	} else {
		if (q_id != DPMAIF_DLQ2) {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON0,
				lb_addr);
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON4,
				hb_addr);
		} else {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd2_base + DPMAIF_DL_2_PIT_INIT_CON0,
				lb_addr);
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd2_base + DPMAIF_DL_2_PIT_INIT_CON4,
				hb_addr);
		}
	}
}

static void mtk_dpmaif_drv_dl_set_pit_size(struct dpmaif_drv_info *drv_info, u32 size, u8 q_id)
{
	u32 val;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		val = mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON1);
		val &= ~DPMAIF_PIT_SIZE_MSK;
		val |= size & DPMAIF_PIT_SIZE_MSK;
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON1, val);
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON2, 0);
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON3, 0);
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON5, 0);
	} else {
		if (q_id != DPMAIF_DLQ2) {
			val = mtk_pci_read32(drv_info->mdev,
					     drv_info->regs->pd_base +
					     NRL2_DPMAIF_DL_LROPIT_INIT_CON1);
			val &= ~DPMAIF_PIT_SIZE_MSK;
			val |= size & DPMAIF_PIT_SIZE_MSK;
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON1,
					val);
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON2,
					0);
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON3,
					0);
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON5,
					0);
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON6,
					0);
		} else {
			val = mtk_pci_read32(drv_info->mdev,
					     drv_info->regs->pd2_base + DPMAIF_DL_2_PIT_INIT_CON1);
			val &= ~DPMAIF_PIT_SIZE_MSK;
			val |= size & DPMAIF_PIT_SIZE_MSK;
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd2_base + DPMAIF_DL_2_PIT_INIT_CON1,
					val);
		}
	}
}

static void mtk_dpmaif_drv_dl_pit_en(struct dpmaif_drv_info *drv_info, bool enable, u8 q_id)
{
	u32 val, addr, mask;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		addr = drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT_CON3;
		mask = DPMAIF_DL_INIT_DONE_MASK;
	} else {
		if (q_id != DPMAIF_DLQ2) {
			addr = drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT_CON3;
			mask = DPMAIF_LROPIT_EN_MSK;
		} else {
			addr = drv_info->regs->pd2_base + DPMAIF_DL_2_PIT_INIT_CON3;
			mask = DPMAIF_DL2_PIT_EN_MSK;
		}
	}

	val = mtk_pci_read32(drv_info->mdev, addr);
	if (enable)
		val |= mask;
	else
		val &= ~mask;

	mtk_pci_write32(drv_info->mdev, addr, val);
}

static int mtk_dpmaif_drv_dl_pit_init_done(struct dpmaif_drv_info *drv_info, u32 pit_idx)
{
	int cnt = 0, dl_pit_init;
	u32 addr;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		dl_pit_init = DPMAIF_DL_PIT_INIT_ALLSET;
		dl_pit_init |= DPMAIF_DL_PIT_INIT_EN;
		addr = drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_INIT;
	} else {
		if (pit_idx != DPMAIF_DLQ2) {
			dl_pit_init = DPMAIF_DL_PIT_INIT_ALLSET;
			dl_pit_init |= pit_idx << DPMAIF_LROPIT_CHAN_OFS;
			dl_pit_init |= DPMAIF_DL_PIT_INIT_EN;
			addr = drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_INIT;
		} else {
			dl_pit_init = DPMAIF_DL_PIT_INIT_ALLSET;
			dl_pit_init |= DPMAIF_DL_PIT_INIT_EN;
			addr = drv_info->regs->pd2_base + DPMAIF_DL_2_PIT_INIT;
		}
	}

	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_PIT_INIT_NOT_READY)) {
			mtk_pci_write32(drv_info->mdev, addr, dl_pit_init);
			break;
		}

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to initialize pit%u\n", pit_idx);
		return -DATA_HW_REG_TIMEOUT;
	}

	cnt = 0;
	do {
		if (!((mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_PIT_INIT_NOT_READY)))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to initialize pit%u done\n", pit_idx);

	return -DATA_HW_REG_TIMEOUT;
}

static int mtk_dpmaif_drv_config_dlq_pit_hw(struct dpmaif_drv_info *drv_info, u8 q_num,
					    struct dpmaif_rxq_cfg *dlq)
{
	mtk_dpmaif_drv_dl_set_pit_base_addr(drv_info, (u64)dlq->pit_base, q_num);
	mtk_dpmaif_drv_dl_set_pit_size(drv_info, dlq->pit_cnt, q_num);
	mtk_dpmaif_drv_dl_pit_en(drv_info, true, q_num);

	return mtk_dpmaif_drv_dl_pit_init_done(drv_info, q_num);
}

static int mtk_dpmaif_drv_dlq_all_en(struct dpmaif_drv_info *drv_info, bool enable)
{
	struct dpmaif_rx_cfg *rx_cfg = &drv_info->cfg->rx_cfg;
	int ret = 0;
	u32 i;

	for (i = 0; i < rx_cfg->bat_ring_num; i++) {
		mtk_dpmaif_drv_dl_bat_en(drv_info, enable, i);
		ret = mtk_dpmaif_drv_dl_bat_init_done(drv_info, false, i,
						      DPMAIF_DL_BAT_INIT_ONLY_ENABLE_BIT);
		if (ret < 0)
			return ret;
	}

	return ret;
}

static void mtk_dpmaif_drv_dl_set_pkt_checksum(struct dpmaif_drv_info *drv_info)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES);
	val |= DPMAIF_DL_PKT_CHECKSUM_EN;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES, val);
}

static void mtk_dpmaif_drv_dl_clr_pkt_checksum(struct dpmaif_drv_info *drv_info)
{
	u32 val;

	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES);
	val &= ~DPMAIF_DL_PKT_CHECKSUM_EN;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES, val);
}

static void mtk_dpmaif_drv_rxcsum_set(struct dpmaif_drv_info *drv_info, void *data)
{
	bool dpmaif_rxcsum_enable = *(bool *)data;

	if (dpmaif_rxcsum_enable && !(drv_info->features & DATA_HW_F_RXCSUM)) {
		drv_info->features |= DATA_HW_F_RXCSUM;
		mtk_dpmaif_drv_dl_set_pkt_checksum(drv_info);
	} else if (!dpmaif_rxcsum_enable && drv_info->features & DATA_HW_F_RXCSUM) {
		drv_info->features &= ~DATA_HW_F_RXCSUM;
		mtk_dpmaif_drv_dl_clr_pkt_checksum(drv_info);
	}
}

static void mtk_dpmaif_drv_txcsum_set(struct dpmaif_drv_info *drv_info, void *data)
{
	bool dpmaif_txcsum_enable = *(bool *)data;

	if (dpmaif_txcsum_enable && !(drv_info->features & DATA_HW_F_TXCSUM))
		drv_info->features |= DATA_HW_F_TXCSUM;
	else if (!dpmaif_txcsum_enable && drv_info->features & DATA_HW_F_TXCSUM)
		drv_info->features &= ~DATA_HW_F_TXCSUM;
}

static int mtk_dpmaif_drv_init_dlq(struct dpmaif_drv_info *drv_info)
{
	struct dpmaif_rx_cfg *rx_cfg = &drv_info->cfg->rx_cfg;
	u32 i, val;
	int ret;

	/* common dl init */
	if (rx_cfg->pkt_alignment == 64)
		mtk_dpmaif_drv_dl_set_pkt_alignment(drv_info, true, DPMAIF_PKT_ALIGN64_MODE);
	else if (rx_cfg->pkt_alignment == 128)
		mtk_dpmaif_drv_dl_set_pkt_alignment(drv_info, true, DPMAIF_PKT_ALIGN128_MODE);
	else
		mtk_dpmaif_drv_dl_set_pkt_alignment(drv_info, false, 0);

	mtk_dpmaif_drv_dl_set_ao_mtu(drv_info, rx_cfg->mtu);
	mtk_dpmaif_drv_dl_set_ao_remain_minsz(drv_info, DPMAIF_HW_BAT_REMAIN);
	mtk_dpmaif_drv_dl_set_ao_bat_rsv_length(drv_info, rx_cfg->normal_bat_rsv_length);
	mtk_dpmaif_drv_dl_set_ao_bid_maxcnt(drv_info, DPMAIF_HW_PKT_BIDCNT);
	/* Bat cache enable. */
	val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_INIT_CON1);
	val |= DPMAIF_DL_BAT_CACHE_PRI;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_INIT_CON1, val);

	/* Common initialize frag bat. */
	if (drv_info->features & DATA_HW_F_FRAG) {
		mtk_dpmaif_drv_dl_frg_ao_en(drv_info, true);
		mtk_dpmaif_drv_dl_set_ao_frg_check_threshold(drv_info, DPMAIF_HW_CHK_FRG_NUM);
		mtk_dpmaif_drv_dl_set_ao_frg_bufsz(drv_info, rx_cfg->frags[0].buf_size);
	}

	/* Common initialize normal bat. */
	mtk_dpmaif_drv_dl_set_ao_bat_check_threshold(drv_info, DPMAIF_HW_CHK_BAT_NUM);
	mtk_dpmaif_drv_dl_set_ao_bat_bufsz(drv_info, rx_cfg->bats[0].buf_size);

	for (i = 0; i < rx_cfg->bat_ring_num; i++) {
		/* Initialize frag bat. */
		if (drv_info->features & DATA_HW_F_FRAG) {
			mtk_dpmaif_drv_dl_set_bat_base_addr(drv_info,
							    (u64)rx_cfg->frags[i].bat_base, i);
			mtk_dpmaif_drv_dl_set_bat_size(drv_info, rx_cfg->frags[i].bat_cnt, i);
			mtk_dpmaif_drv_dl_bat_en(drv_info, true, i);
			ret = mtk_dpmaif_drv_dl_bat_init_done(drv_info, true, i,
							      DPMAIF_DL_BAT_INIT_ALLSET);
			if (ret < 0)
				return ret;

			ret = mtk_dpmaif_drv_dl_add_frg_cnt(drv_info, i,
							    rx_cfg->frags[i].real_reload_cnt);
			if (ret < 0)
				return ret;
		}

		/* Initialize normal bat. */
		mtk_dpmaif_drv_dl_set_bat_base_addr(drv_info, (u64)rx_cfg->bats[i].bat_base, i);
		mtk_dpmaif_drv_dl_set_bat_size(drv_info, rx_cfg->bats[i].bat_cnt, i);
		mtk_dpmaif_drv_dl_bat_en(drv_info, false, i);
		ret = mtk_dpmaif_drv_dl_bat_init_done(drv_info, false, i,
						      DPMAIF_DL_BAT_INIT_ALLSET);
		if (ret < 0)
			return ret;

		ret = mtk_dpmaif_drv_dl_add_bat_cnt(drv_info, i, rx_cfg->bats[i].real_reload_cnt);
		if (ret < 0)
			return ret;
	}

	/* Initialize pit information. */
	/* Pit burst enable. */
	val = mtk_pci_read32(drv_info->mdev,
			     drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES);
	val |= DPMAIF_DL_BURST_PIT_EN;
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + DPMAIF_AO_DL_RDY_CHK_THRES, val);
	mtk_dpmaif_drv_dl_set_ao_pit_chknum(drv_info, DPMAIF_HW_CHK_PIT_NUM);
	/* Currently, use rxqs[0] to config. */
	mtk_dpmaif_drv_dl_set_pit_seqnum(drv_info, rx_cfg->rxqs[0].pit_seq_max);

	for (i = 0; i < rx_cfg->rxq_cnt; i++) {
		ret = mtk_dpmaif_drv_config_dlq_pit_hw(drv_info, i, &rx_cfg->rxqs[i]);
		if (ret < 0)
			return ret;
	}

	ret = mtk_dpmaif_drv_dlq_all_en(drv_info, true);
	if (ret < 0)
		return ret;

	mtk_dpmaif_drv_dl_set_pkt_checksum(drv_info);

	return 0;
}

static void mtk_dpmaif_drv_ul_update_drb_size(struct dpmaif_drv_info *drv_info,
					      u8 q_num, u32 size)
{
	u32 old_size;
	u64 addr;

	addr = drv_info->regs->pd_base + DPMAIF_PD_UL_CHNL0_CON1 + 0x10 * q_num;
	old_size = mtk_pci_read32(drv_info->mdev, addr);
	old_size &= ~DPMAIF_DRB_SIZE_MSK;
	old_size |= size & DPMAIF_DRB_SIZE_MSK;
	mtk_pci_write32(drv_info->mdev, addr, old_size);
}

static void mtk_dpmaif_drv_ul_update_drb_base_addr(struct dpmaif_drv_info *drv_info,
						   u8 q_num, u64 addr)
{
	u32 lb_addr = (u32)(addr & 0xFFFFFFFF);
	u32 hb_addr = (u32)(addr >> 32);

	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + DPMAIF_PD_UL_CHNL0_CON0 + 0x10 * q_num, lb_addr);
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + DPMAIF_PD_UL_CHNL0_CON2 + 0x10 * q_num, hb_addr);
}

static void mtk_dpmaif_drv_ul_rdy_en(struct dpmaif_drv_info *drv_info, u8 q_num, bool ready)
{
	u32 ul_rdy_en;

	ul_rdy_en = mtk_pci_read32(drv_info->mdev,
				   drv_info->regs->ao_base + DPMAIF_PD_UL_CHNL_ARB0);
	if (ready)
		ul_rdy_en |= BIT(q_num);
	else
		ul_rdy_en &= ~BIT(q_num);

	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_UL_CHNL_ARB0,
			ul_rdy_en);
}

static void mtk_dpmaif_drv_ul_arb_en(struct dpmaif_drv_info *drv_info, u8 q_num, bool enable)
{
	u32 ul_arb_en;

	ul_arb_en = mtk_pci_read32(drv_info->mdev,
				   drv_info->regs->ao_base + DPMAIF_PD_UL_CHNL_ARB0);
	if (enable)
		ul_arb_en |= BIT(q_num + 8);
	else
		ul_arb_en &= ~BIT(q_num + 8);

	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_UL_CHNL_ARB0,
			ul_arb_en);
}

static void mtk_dpmaif_drv_init_ulq(struct dpmaif_drv_info *drv_info)
{
	struct dpmaif_tx_cfg *tx_cfg = &drv_info->cfg->tx_cfg;
	struct dpmaif_txq_cfg *ulq;
	u8 mode = 0;
	u32 i;

	for (i = 0; i < tx_cfg->txq_cnt; i++) {
		ulq = &tx_cfg->txqs[i];
		mtk_dpmaif_drv_ul_update_drb_size(drv_info, i,
						  ulq->drb_cnt * DPMAIF_UL_DRB_ENTRY_WORD);
		mtk_dpmaif_drv_ul_update_drb_base_addr(drv_info, i, (u64)ulq->drb_base);
		mtk_dpmaif_drv_ul_rdy_en(drv_info, i, true);
		mtk_dpmaif_drv_ul_arb_en(drv_info, i, true);
		if (drv_info->cfg->cap & DATA_HW_F_INTR_COALESCE) {
			if (ulq->tx_coalesced_frames)
				mode |= DPMAIF_INTR_COALESCE_EN_PKT;
			if (ulq->tx_coalesce_usecs)
				mode |= DPMAIF_INTR_COALESCE_EN_TIME;
			mtk_dpmaif_drv_ul_set_delay_intr(drv_info, i, mode, ulq->tx_coalesce_usecs,
							 ulq->tx_coalesced_frames);
			mode = 0;
		}
	}
}

static int mtk_dpmaif_drv_init_done(struct dpmaif_drv_info *drv_info)
{
	int rxq_cnt = drv_info->cfg->rx_cfg.rxq_cnt;
	u32 val, cnt = 0;

	/* Sync default value to SRAM. */
	val = mtk_pci_read32(drv_info->mdev,
			     drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_OVERWRITE_CFG);
	val |= DPMAIF_SRAM_SYNC_MASK;
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_OVERWRITE_CFG, val);
	do {
		if (!(mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_OVERWRITE_CFG) &
				     DPMAIF_SRAM_SYNC_MASK))
			break;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to sync default value to sram\n");
		return -DATA_HW_REG_TIMEOUT;
	}

	/* UL configure done. */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_INIT_SET,
			DPMAIF_UL_INIT_DONE_MASK);
	drv_info->cfg->tx_cfg.txq_all_enable = true;

	/* DL configure done. */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + NRL2_DPMAIF_AO_DL_INIT_SET,
			DPMAIF_DL_INIT_DONE_MASK);
	drv_info->cfg->rx_cfg.rxq_all_enable = true;

	/* clear dummy interrupts */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISAR0,
			0xFFFFFFFF);
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_DL_L2TISAR0,
			0xFFFFFFFF);

	if (rxq_cnt >= DPMAIF_DLQ2 + 1)
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_AP_MISC_APDL_L2TISAR1,
				0xFFFFFFFF);

	return 0;
}

static void mtk_dpmaif_drv_ulq_all_en(struct dpmaif_drv_info *drv_info, bool enable)
{
	u32 ul_arb_en;

	ul_arb_en = mtk_pci_read32(drv_info->mdev,
				   drv_info->regs->ao_base + DPMAIF_PD_UL_CHNL_ARB0);
	if (enable)
		ul_arb_en |= DPMAIF_UL_ALL_QUE_ARB_EN;
	else
		ul_arb_en &= ~DPMAIF_UL_ALL_QUE_ARB_EN;

	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_UL_CHNL_ARB0,
			ul_arb_en);
	mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_UL_CHNL_ARB0);
}

static bool mtk_dpmaif_drv_ul_all_idle_check(struct dpmaif_drv_info *drv_info)
{
	bool is_idle = false;
	u32 ul_dbg_sta;

	ul_dbg_sta = mtk_pci_read32(drv_info->mdev,
				    drv_info->regs->pd_base + DPMAIF_PD_UL_DBG_STA2);
	if ((ul_dbg_sta & DPMAIF_UL_IDLE_STS_MSK) == DPMAIF_UL_IDLE_STS)
		is_idle = true;

	return is_idle;
}

static int mtk_dpmaif_drv_stop_ulq(struct dpmaif_drv_info *drv_info)
{
	int cnt = 0;

	/* Disable HW arb and check idle. */
	mtk_dpmaif_drv_ulq_all_en(drv_info, false);

	do {
		if (mtk_dpmaif_drv_ul_all_idle_check(drv_info))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to stop ul queue, sta=0x%08x\n",
		mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_UL_DBG_STA2));

	return -DATA_HW_REG_TIMEOUT;
}

static bool mtk_dpmaif_drv_dl_is_idle(struct dpmaif_drv_info *drv_info)
{
	bool is_idle = false;
	u32 dl_dbg_sta;

	dl_dbg_sta = mtk_pci_read32(drv_info->mdev,
				    drv_info->regs->pd_base + DPMAIF_PD_DL_DBG_STA1);
	if ((dl_dbg_sta & DPMAIF_DL_IDLE_STS) == DPMAIF_DL_IDLE_STS)
		is_idle = true;

	return is_idle;
}

static u32 mtk_dpmaif_drv_dl_get_wridx(struct dpmaif_drv_info *drv_info)
{
	return ((mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PIT_STA3)) &
		DPMAIF_DL_PIT_WRIDX_MSK);
}

static u32 mtk_dpmaif_drv_dl_get_pit_ridx(struct dpmaif_drv_info *drv_info)
{
	return ((mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_AO_DL_PIT_STA2)) &
		DPMAIF_DL_PIT_WRIDX_MSK);
}

static int mtk_dpmaif_drv_stop_dlq(struct dpmaif_drv_info *drv_info)
{
	u32 cnt = 0, wridx, ridx;
	int ret;

	ret = mtk_dpmaif_drv_dlq_all_en(drv_info, false);
	if (ret < 0)
		return ret;

	/* check idle */
	do {
		if (mtk_dpmaif_drv_dl_is_idle(drv_info))
			break;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to stop dl queue, sta=0x%08x\n",
			mtk_pci_read32(drv_info->mdev,
				       drv_info->regs->pd_base + DPMAIF_PD_DL_DBG_STA1));
		return -DATA_HW_REG_TIMEOUT;
	}

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0)
		return 0;

	/* check middle pit sync done. */
	cnt = 0;
	do {
		wridx = mtk_dpmaif_drv_dl_get_wridx(drv_info);
		ridx = mtk_dpmaif_drv_dl_get_pit_ridx(drv_info);
		if (wridx == ridx)
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to check middle pit sync\n");

	return -DATA_HW_REG_TIMEOUT;
}

u32 mtk_dpmaif_drv_get_dl_lv2_sts(struct dpmaif_drv_info *drv_info, u8 q_id)
{
	if (q_id != DPMAIF_DLQ2)
		return mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				DPMAIF_PD_AP_DL_L2TISAR0);
	else
		return mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AP_MISC_APDL_L2TISAR1);
}

u32 mtk_dpmaif_drv_get_ul_lv2_sts(struct dpmaif_drv_info *drv_info)
{
	return mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISAR0);
}

static int mtk_dpmaif_drv_mask_ulq_intr(struct dpmaif_drv_info *drv_info, u32 q_num)
{
	u32 cnt = 0, ui_que_done_mask;

	if (drv_info->priv_ops && drv_info->priv_ops->mask_ulq_intr)
		return drv_info->priv_ops->mask_ulq_intr(drv_info, q_num);

	ui_que_done_mask = BIT(q_num + DP_UL_INT_DONE_OFFSET) & DPMAIF_UL_INT_QDONE_MSK;

	do {
		if (!(cnt++ % REWRITE_TIMES)) {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TISR0,
					ui_que_done_mask);
			mtk_pci_read32(drv_info->mdev,
				       drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TISR0);
		}

		if ((mtk_pci_read32(drv_info->mdev,
				    drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TIMR0) &
				    ui_que_done_mask)) {
			return 0;
		}

		udelay(POLL_INTERVAL_US);
	} while (cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to mask ulq%u interrupt done, sta=0x%08x\n", q_num,
		mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TIMR0));

	return -DATA_HW_REG_TIMEOUT;
}

static int mtk_dpmaif_drv_ul_mask_all_tx_done_intr(struct dpmaif_drv_info *drv_info)
{
	int ret = 0;
	u8 i;

	for (i = 0; i < drv_info->cfg->tx_cfg.txq_cnt; i++) {
		ret = mtk_dpmaif_drv_mask_ulq_intr(drv_info, i);
		if (ret < 0)
			break;
	}

	return ret;
}

void mtk_dpmaif_drv_ul_mask_multi_tx_done_intr(struct dpmaif_drv_info *drv_info,
					       u8 q_mask)
{
	u32 i;

	for (i = 0; i < drv_info->cfg->tx_cfg.txq_cnt; i++) {
		if (q_mask & BIT(i)) {
			mtk_pm_ds_try_lock(drv_info->mdev, MTK_USER_DATA);
			mtk_dpmaif_drv_mask_ulq_intr(drv_info, i);
		}
	}
}

static void mtk_dpmaif_drv_unmask_ulq_intr(struct dpmaif_drv_info *drv_info, u32 q_num)
{
	u32 ui_que_done_mask;

	if (drv_info->priv_ops && drv_info->priv_ops->unmask_ulq_intr) {
		drv_info->priv_ops->unmask_ulq_intr(drv_info, q_num);
		return;
	}

	ui_que_done_mask = BIT(q_num + DP_UL_INT_DONE_OFFSET) & DPMAIF_UL_INT_QDONE_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TICR0,
			ui_que_done_mask);
}

static void mtk_dpmaif_drv_ul_unmask_all_tx_done_intr(struct dpmaif_drv_info *drv_info)
{
	u8 i;

	for (i = 0; i < drv_info->cfg->tx_cfg.txq_cnt; i++)
		mtk_dpmaif_drv_unmask_ulq_intr(drv_info, i);
}

static void mtk_dpmaif_drv_clr_ul_done_status(struct dpmaif_drv_info *drv_info, u8 qno)
{
	u32 val, l2tisar0;

	/* get TX interrupt status. */
	l2tisar0 = mtk_dpmaif_drv_get_ul_lv2_sts(drv_info);
	val = l2tisar0 & DPMAIF_UL_INT_QDONE & BIT(DP_UL_INT_DONE_OFFSET + qno);

	/* ulq status. */
	if (val) {
		/* clear ulq done status */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISAR0,
				val);
	}
}

void mtk_dpmaif_drv_mask_dl_batcnt_len_err_intr(struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	if (bat_id == DPMAIF_BAT0) {
		mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TISR0,
				DPMAIF_DL_INT_BATCNT_LEN_ERR_MSK);
		mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TISR0);
	} else {
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET, DPMAIF_DL2_INT_BATCNT_LEN_ERR_MSK);
		mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET);
	}
}

static void mtk_dpmaif_drv_unmask_dl_batcnt_len_err_intr(struct dpmaif_drv_info *drv_info,
							 u8 bat_id)
{
	if (bat_id == DPMAIF_BAT0)
		mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TICR0,
				DPMAIF_DL_INT_BATCNT_LEN_ERR_MSK);
	else
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_MISC_PD_APDL12_MASK_CLR,
				DPMAIF_DL2_INT_BATCNT_LEN_ERR_MSK);
}

void mtk_dpmaif_drv_mask_dl_frgcnt_len_err_intr(struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	if (bat_id == DPMAIF_BAT0) {
		mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TISR0,
				DPMAIF_DL_INT_FRG_LEN_ERR_MSK);
		mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TISR0);
	} else {
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET, DPMAIF_DL2_INT_FRG_LEN_ERR_MSK);
		mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET);
	}
}

static void mtk_dpmaif_drv_unmask_dl_frgcnt_len_err_intr(struct dpmaif_drv_info *drv_info,
							 u8 bat_id)
{
	if (bat_id == DPMAIF_BAT0)
		mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TICR0,
				DPMAIF_DL_INT_FRG_LEN_ERR_MSK);
	else
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_MISC_PD_APDL12_MASK_CLR,
				DPMAIF_DL2_INT_FRG_LEN_ERR_MSK);
}

void mtk_dpmaif_drv_dlq_mask_pit_cnt_len_err_intr(struct dpmaif_drv_info *drv_info, u8 qno)
{
	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_APDL_L2TIMSR0,
				DPMAIF_DL_INT_DLQ_PITCNT_LEN_ERR_MSK);
	} else {
		if (qno == DPMAIF_DLQ0) {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_APDL_L2TIMSR0,
					DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR_MSK);
		} else if (qno == DPMAIF_DLQ1) {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_APDL_L2TIMSR0,
					DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR_MSK);
		} else {
			mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET,
				DPMAIF_DL_INT_DLQ2_PITCNT_LEN_ERR_MSK);
			mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET);
			return;
		}
	}

	mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_APDL_L2TIMSR0);
}

static void
	mtk_dpmaif_drv_dlq_unmask_pit_cnt_len_err_intr(struct dpmaif_drv_info *drv_info,
						       u8 qno)
{
	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_APDL_L2TIMCR0,
				DPMAIF_DL_INT_DLQ_PITCNT_LEN_ERR_MSK);
	} else {
		if (qno == DPMAIF_DLQ0) {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_APDL_L2TIMCR0,
					DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR_MSK);
		} else if (qno == DPMAIF_DLQ1) {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->ao_base + NRL2_DPMAIF_AO_UL_APDL_L2TIMCR0,
					DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR_MSK);
		} else {
			mtk_pci_write32(drv_info->mdev,
					drv_info->regs->pd_base +
					NRL2_DPMAIF_MISC_PD_APDL12_MASK_CLR,
					DPMAIF_DL_INT_DLQ2_PITCNT_LEN_ERR_MSK);
		}
	}
}

int mtk_dpmaif_drv_dlq_mask_rx_done_intr(struct dpmaif_drv_info *drv_info, u8 q_id)
{
	u64 mask_addr = drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TIMR0;
	u64 addr = drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TISR0;
	u32 cnt = 0, di_que_done_mask;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		di_que_done_mask = DPMAIF_DL_INT_DLQ_QDONE_MSK;
	} else {
		if (q_id == DPMAIF_DLQ0) {
			di_que_done_mask = DPMAIF_DL_INT_DLQ0_QDONE_MSK;
		} else if (q_id == DPMAIF_DLQ1) {
			di_que_done_mask = DPMAIF_DL_INT_DLQ1_QDONE_MSK;
		} else {
			di_que_done_mask = DPMAIF_DL_INT_DLQ2_QDONE_MSK;
			addr = drv_info->regs->pd_base + NRL2_DPMAIF_MISC_PD_APDL12_MASK_SET;
			mask_addr = drv_info->regs->pd_base + NRL2_DPMAIF_MISC_PD_APDL12_MASK_RW;
		}
	}

	/* Check mask status. */
	do {
		if (!(cnt++ % REWRITE_TIMES)) {
			mtk_pci_write32(drv_info->mdev, addr, di_que_done_mask);
			mtk_pci_read32(drv_info->mdev, addr);
		}

		if ((mtk_pci_read32(drv_info->mdev, mask_addr) & di_que_done_mask))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to mask dlq%u interrupt, sta=0x%08x\n",
		q_id, mtk_pci_read32(drv_info->mdev, mask_addr));
	WARN_ON_ONCE(true);

	return -DATA_HW_REG_TIMEOUT;
}

static int mtk_dpmaif_drv_dl_mask_all_rx_done_intr(struct dpmaif_drv_info *drv_info)
{
	int ret = 0;
	u8 i;

	for (i = 0; i < drv_info->cfg->rx_cfg.rxq_cnt; i++) {
		ret = mtk_dpmaif_drv_dlq_mask_rx_done_intr(drv_info, i);
		if (ret < 0)
			break;
	}

	return ret;
}

static void mtk_dpmaif_drv_dl_unmask_rx_done_intr(struct dpmaif_drv_info *drv_info, u8 qno)
{
	u64 addr = drv_info->regs->ao_base + DPMAIF_PD_AP_DL_L2TICR0;
	u32 di_que_done_mask;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		di_que_done_mask = DPMAIF_DL_INT_DLQ_QDONE_MSK;
	} else {
		if (qno == DPMAIF_DLQ0) {
			di_que_done_mask = DPMAIF_DL_INT_DLQ0_QDONE_MSK;
		} else if (qno == DPMAIF_DLQ1) {
			di_que_done_mask = DPMAIF_DL_INT_DLQ1_QDONE_MSK;
		} else {
			di_que_done_mask = DPMAIF_DL2_INT_DLQ2_QDONE;
			addr = drv_info->regs->pd_base + NRL2_DPMAIF_MISC_PD_APDL12_MASK_CLR;
		}
	}

	mtk_pci_write32(drv_info->mdev, addr, di_que_done_mask);
	mtk_pci_read32(drv_info->mdev, addr);
}

static void mtk_dpmaif_drv_dl_unmask_all_rx_done_intr(struct dpmaif_drv_info *drv_info)
{
	u8 i;

	for (i = 0; i < drv_info->cfg->rx_cfg.rxq_cnt; i++)
		mtk_dpmaif_drv_dl_unmask_rx_done_intr(drv_info, i);
}

static int mtk_dpmaif_drv_dl_add_pit_cnt(struct dpmaif_drv_info *drv_info, u32 qno,
					 u32 pit_remain_cnt)
{
	u32 cnt = 0, dl_update, addr;

	dl_update = pit_remain_cnt & 0x3FFFF;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		dl_update |= DPMAIF_DL_ADD_UPDATE;
		addr = drv_info->regs->pd_base + NRL2_DPMAIF_DL_PIT_ADD;
	} else {
		if (qno != DPMAIF_DLQ2) {
			dl_update |= DPMAIF_DL_ADD_UPDATE | (qno << DPMAIF_ADD_LRO_PIT_CHAN_OFS);
			addr = drv_info->regs->pd_base + NRL2_DPMAIF_DL_LROPIT_ADD;
		} else {
			dl_update |= DPMAIF_DL_ADD_UPDATE;
			addr = drv_info->regs->pd2_base + DPMAIF_DL_2_PIT_ADD;
		}
	}

	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_ADD_NOT_READY)) {
			mtk_pci_write32(drv_info->mdev, addr, dl_update);
			break;
		}

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to add dlq%u pit, cnt=%u\n", qno, pit_remain_cnt);
		return -DATA_HW_REG_TIMEOUT;
	}

	cnt = 0;
	do {
		if (!((mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_ADD_NOT_READY)))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to add dlq%u pit done, cnt=%u\n", qno, pit_remain_cnt);

	return -DATA_HW_REG_TIMEOUT;
}

static int mtk_dpmaif_drv_dl_add_bat_cnt(struct dpmaif_drv_info *drv_info,
					 u8 bat_id, u32 bat_entry_cnt)
{
	u32 cnt = 0, dl_bat_update;
	u64 addr;

	dl_bat_update = bat_entry_cnt & 0xFFFF;
	dl_bat_update |= DPMAIF_DL_ADD_UPDATE;

	if (bat_id == DPMAIF_BAT0)
		addr = drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_ADD;
	else
		addr = drv_info->regs->pd2_base + DPMAIF_DL_2_BAT_ADD;

	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_ADD_NOT_READY)) {
			mtk_pci_write32(drv_info->mdev, addr, dl_bat_update);
			break;
		}

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to add bat%hhu, cnt=%u\n", bat_id, bat_entry_cnt);
		return -DATA_HW_REG_TIMEOUT;
	}

	cnt = 0;
	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_ADD_NOT_READY))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to add bat%hhu done, cnt=%u\n", bat_id, bat_entry_cnt);

	return -DATA_HW_REG_TIMEOUT;
}

static int mtk_dpmaif_drv_dl_add_frg_cnt(struct dpmaif_drv_info *drv_info,
					 u8 bat_id, u32 frg_entry_cnt)
{
	u32 cnt = 0, dl_frg_update;
	u64 addr;

	dl_frg_update = frg_entry_cnt & 0xFFFF;
	dl_frg_update |= DPMAIF_DL_FRG_ADD_UPDATE;
	dl_frg_update |= DPMAIF_DL_ADD_UPDATE;

	if (bat_id == DPMAIF_BAT0)
		addr = drv_info->regs->pd_base + DPMAIF_PD_DL_BAT_ADD;
	else
		addr = drv_info->regs->pd2_base + DPMAIF_DL_2_BAT_ADD;

	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_ADD_NOT_READY)) {
			mtk_pci_write32(drv_info->mdev, addr, dl_frg_update);
			break;
		}

		udelay(POLL_INTERVAL_US);
	} while (++cnt  < POLL_MAX_TIMES);

	if (++cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to add frag%hhu bat, cnt=%u\n",
			bat_id, frg_entry_cnt);
		return -DATA_HW_REG_TIMEOUT;
	}

	cnt = 0;
	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_DL_ADD_NOT_READY))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to add frag%hhu bat done, cnt=%u\n",
		bat_id, frg_entry_cnt);

	return -DATA_HW_REG_TIMEOUT;
}

static int mtk_dpmaif_drv_ul_add_drb(struct dpmaif_drv_info *drv_info, u8 q_num, u32 drb_cnt)
{
	u32 drb_entry_cnt = drb_cnt * DPMAIF_UL_DRB_ENTRY_WORD;
	u32 cnt = 0, ul_update;
	u64 addr;

	ul_update = drb_entry_cnt & 0xFFFF;
	ul_update |= DPMAIF_UL_ADD_UPDATE;

	if (q_num == 4)
		addr = drv_info->regs->pd_base + NRL2_DPMAIF_UL_ADD_DESC_CH4;
	else
		addr = drv_info->regs->pd_base + DPMAIF_PD_UL_ADD_DESC_CH + 0x4 * q_num;

	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_UL_ADD_NOT_READY)) {
			mtk_pci_write32(drv_info->mdev, addr, ul_update);
			break;
		}

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to add ulq%u drb, cnt=%u\n", q_num, drb_cnt);
		return -DATA_HW_REG_TIMEOUT;
	}

	cnt = 0;
	do {
		if (!(mtk_pci_read32(drv_info->mdev, addr) & DPMAIF_UL_ADD_NOT_READY))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to add ulq%u drb done, cnt=%u\n", q_num, drb_cnt);

	return -DATA_HW_REG_TIMEOUT;
}

static int mtk_dpmaif_drv_dl_get_pit_wridx(struct dpmaif_drv_info *drv_info, u32 qno)
{
	u32 pit_wridx;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		pit_wridx = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_AO_DL_PIT_STA3) & DPMAIF_DL_PIT_WRIDX_MSK;
	} else {
		if (qno != DPMAIF_DLQ2) {
			pit_wridx = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AO_DL_LRO_STA5 + qno * 0x20) & DPMAIF_DL_PIT_WRIDX_MSK;
		} else {
			pit_wridx = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd2_base +
				DPMAIF_DL_2_STA13) & DPMAIF_DL_PIT_WRIDX_MSK;
		}
	}

	if (unlikely(pit_wridx >= drv_info->cfg->rx_cfg.rxqs[qno].pit_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return pit_wridx;
}

static int mtk_dpmaif_drv_dl_get_pit_rdidx(struct dpmaif_drv_info *drv_info, u32 qno)
{
	u32 pit_rdidx;

	if (drv_info->cfg->rx_cfg.mode == DPMAIF_DL_M0) {
		pit_rdidx =  mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
			NRL2_DPMAIF_AO_DL_PIT_STA2) & DPMAIF_DL_PIT_WRIDX_MSK;
	} else {
		if (qno != DPMAIF_DLQ2)
			pit_rdidx = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AO_DL_LRO_STA6 + qno * 0x20) & DPMAIF_DL_PIT_WRIDX_MSK;
		else
			pit_rdidx = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd2_base +
				DPMAIF_DL_2_STA14) & DPMAIF_DL_PIT_WRIDX_MSK;
	}

	if (unlikely(pit_rdidx >= drv_info->cfg->rx_cfg.rxqs[qno].pit_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return pit_rdidx;
}

static int mtk_dpmaif_drv_dl_get_bat_ridx(struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	u32 bat_ridx;

	if (bat_id == DPMAIF_BAT0)
		bat_ridx = mtk_pci_read32(drv_info->mdev,
					  drv_info->regs->pd_base + DPMAIF_AO_DL_BAT_STA2) &
			    DPMAIF_DL_BAT_WRIDX_MSK;
	else
		bat_ridx = mtk_pci_read32(drv_info->mdev,
					  drv_info->regs->pd2_base + DPMAIF_DL_2_STA4) &
				DPMAIF_DL_2_BAT_WRIDX_MSK;

	if (unlikely(bat_ridx >= drv_info->cfg->rx_cfg.bats[bat_id].bat_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return bat_ridx;
}

static int mtk_dpmaif_drv_dl_get_bat_wridx(struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	u32 bat_wridx;

	if (bat_id == DPMAIF_BAT0)
		bat_wridx = mtk_pci_read32(drv_info->mdev,
					   drv_info->regs->pd_base + DPMAIF_AO_DL_BAT_STA3) &
					   DPMAIF_DL_BAT_WRIDX_MSK;
	else
		bat_wridx = (mtk_pci_read32(drv_info->mdev,
			   drv_info->regs->pd2_base + DPMAIF_DL_2_STA4) >> 16) &
			   DPMAIF_DL_2_BAT_WRIDX_MSK;

	if (unlikely(bat_wridx >= drv_info->cfg->rx_cfg.bats[bat_id].bat_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return bat_wridx;
}

static int mtk_dpmaif_drv_dl_get_frg_ridx(struct dpmaif_drv_info *drv_info, u8 bat_id)
{
	u32 frg_ridx;

	if (bat_id == DPMAIF_BAT0)
		frg_ridx = mtk_pci_read32(drv_info->mdev,
					  drv_info->regs->pd_base + DPMAIF_AO_DL_FRG_STA2) &
				DPMAIF_DL_FRG_WRIDX_MSK;
	else
		frg_ridx = mtk_pci_read32(drv_info->mdev,
					  drv_info->regs->pd2_base + DPMAIF_DL_2_FRG_STA4) &
			DPMAIF_DL_2_BAT_WRIDX_MSK;

	if (unlikely(frg_ridx >= drv_info->cfg->rx_cfg.frags[bat_id].bat_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return frg_ridx;
}

static int mtk_dpmaif_drv_ul_get_drb_ridx(struct dpmaif_drv_info *drv_info, u8 qno)
{
	u32 drb_ridx;
	u64 addr;

	addr = drv_info->regs->pd_base + drv_info->regs->ao_ul_ch0_sta + 0x4 * qno;

	drb_ridx = mtk_pci_read32(drv_info->mdev, addr) >> 16;
	drb_ridx = drb_ridx / DPMAIF_UL_DRB_ENTRY_WORD;

	if (unlikely(drb_ridx >= drv_info->cfg->tx_cfg.txqs[qno].drb_cnt))
		return -DATA_HW_REG_CHK_FAIL;

	return drb_ridx;
}

int mtk_dpmaif_drv_init_com(struct dpmaif_drv_info *drv_info, void *data)
{
	if (mtk_dpmaif_drv_init_mode(drv_info) < 0)
		return -DATA_HW_REG_CHK_FAIL;

	if (mtk_dpmaif_drv_init_intr(drv_info) < 0)
		return -DATA_HW_REG_CHK_FAIL;

	if (mtk_dpmaif_drv_init_features(drv_info) < 0)
		return -DATA_HW_REG_CHK_FAIL;

	if (mtk_dpmaif_drv_init_dlq(drv_info) < 0)
		return -DATA_HW_REG_CHK_FAIL;

	mtk_dpmaif_drv_init_ulq(drv_info);

	if (mtk_dpmaif_drv_init_done(drv_info) < 0)
		return -DATA_HW_REG_CHK_FAIL;

	return 0;
}

int mtk_dpmaif_drv_start_queue(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_dir dir)
{
	int ret;

	if (dir == DPMAIF_TX) {
		if (unlikely(drv_info->cfg->tx_cfg.txq_all_enable)) {
			MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
				"ulq all enabled\n");
			return 0;
		}

		mtk_dpmaif_drv_ulq_all_en(drv_info, true);
		mtk_dpmaif_drv_ul_unmask_all_tx_done_intr(drv_info);
		drv_info->cfg->tx_cfg.txq_all_enable = true;
	} else {
		if (unlikely(drv_info->cfg->rx_cfg.rxq_all_enable)) {
			MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
				"dlq all enabled\n");
			return 0;
		}

		ret = mtk_dpmaif_drv_dlq_all_en(drv_info, true);
		if (ret < 0)
			return ret;

		mtk_dpmaif_drv_dl_unmask_all_rx_done_intr(drv_info);
		drv_info->cfg->rx_cfg.rxq_all_enable = true;
	}

	return 0;
}

int mtk_dpmaif_drv_stop_queue(struct dpmaif_drv_info *drv_info, enum dpmaif_drv_dir dir)
{
	int rxq_cnt = drv_info->cfg->rx_cfg.rxq_cnt;
	u32 dl_status, dl_mask;
	u32 ul_status, ul_mask;
	int ret;

	if (dir == DPMAIF_TX) {
		ul_status = mtk_dpmaif_drv_get_ul_lv2_sts(drv_info);
		ul_mask = mtk_dpmaif_drv_get_ul_intr_mask(drv_info);
		MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
			"ul interrupt:0x%08x,0x%08x\n", ul_status, ul_mask);

		if (unlikely(!drv_info->cfg->tx_cfg.txq_all_enable)) {
			MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
				"ulq all disabled\n");
			return 0;
		}

		ret = mtk_dpmaif_drv_stop_ulq(drv_info);
		if (ret < 0)
			return ret;

		ret = mtk_dpmaif_drv_ul_mask_all_tx_done_intr(drv_info);
		if (ret < 0)
			return ret;

		drv_info->cfg->tx_cfg.txq_all_enable = false;
	} else {
		dl_status = mtk_dpmaif_drv_get_dl_lv2_sts(drv_info, DPMAIF_DLQ0);
		dl_mask = mtk_dpmaif_drv_get_dl_intr_mask(drv_info, DPMAIF_DLQ0);
		MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
			"interrupt state1:0x%08x,0x%08x\n", dl_status, dl_mask);

		if (rxq_cnt >= DPMAIF_DLQ2 + 1) {
			dl_status = mtk_dpmaif_drv_get_dl_lv2_sts(drv_info, DPMAIF_DLQ2);
			dl_mask = mtk_dpmaif_drv_get_dl_intr_mask(drv_info, DPMAIF_DLQ2);
			MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
				"interrupt state2:0x%08x,0x%08x\n", dl_status, dl_mask);
		}
		if (unlikely(!drv_info->cfg->rx_cfg.rxq_all_enable)) {
			MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_COMMON,
				"dlq all disabled\n");
			return 0;
		}

		ret = mtk_dpmaif_drv_stop_dlq(drv_info);
		if (ret < 0)
			return ret;

		ret = mtk_dpmaif_drv_dl_mask_all_rx_done_intr(drv_info);
		if (ret < 0)
			return ret;

		drv_info->cfg->rx_cfg.rxq_all_enable = false;
	}

	return 0;
}

void mtk_dpmaif_drv_clr_ip_busy_sts(struct dpmaif_drv_info *drv_info)
{
	u32 ip_busy_sts;

	/* Get AP IP busy status. */
	ip_busy_sts = mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->pd_base + DPMAIF_PD_AP_IP_BUSY);

	/* Clear AP IP busy. */
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + DPMAIF_PD_AP_IP_BUSY, ip_busy_sts);
}

int mtk_dpmaif_drv_intr_handle_com(struct dpmaif_drv_info *drv_info, void *data, u8 irq_id)
{
	return drv_info->cfg->intr_cfg.irqs[irq_id].handle(drv_info, data);
}

int mtk_dpmaif_drv_intr_complete_com(struct dpmaif_drv_info *drv_info,
				     enum dpmaif_drv_intr_type type, u8 id, u64 data)
{
	switch (type) {
	case DPMAIF_INTR_UL_DONE:
		if (data == DPMAIF_CLEAR_INTR) {
			mtk_dpmaif_drv_clr_ul_done_status(drv_info, id);
		} else {
			mtk_dpmaif_drv_unmask_ulq_intr(drv_info, id);
			mtk_pm_ds_unlock_instant(drv_info->mdev, MTK_USER_DATA);
		}
		break;
	case DPMAIF_INTR_DL_BATCNT_LEN_ERR:
		mtk_dpmaif_drv_unmask_dl_batcnt_len_err_intr(drv_info, id);
		break;
	case DPMAIF_INTR_DL_FRGCNT_LEN_ERR:
		mtk_dpmaif_drv_unmask_dl_frgcnt_len_err_intr(drv_info, id);
		break;
	case DPMAIF_INTR_DL_PITCNT_LEN_ERR:
		mtk_dpmaif_drv_dlq_unmask_pit_cnt_len_err_intr(drv_info, id);
		break;
	case DPMAIF_INTR_DL_DONE:
		mtk_dpmaif_drv_dl_unmask_rx_done_intr(drv_info, id);
		mtk_pm_ds_unlock_instant(drv_info->mdev, MTK_USER_DATA);
		break;
	default:
		break;
	}

	return 0;
}

int mtk_dpmaif_drv_send_doorbell_com(struct dpmaif_drv_info *drv_info,
				     enum dpmaif_drv_ring_type type, u8 id, u32 cnt)
{
	int ret = 0;

	switch (type) {
	case DPMAIF_PIT:
		ret = mtk_dpmaif_drv_dl_add_pit_cnt(drv_info, id, cnt);
		break;
	case DPMAIF_BAT:
		ret = mtk_dpmaif_drv_dl_add_bat_cnt(drv_info, id, cnt);
		break;
	case DPMAIF_FRAG:
		ret = mtk_dpmaif_drv_dl_add_frg_cnt(drv_info, id, cnt);
		break;
	case DPMAIF_DRB:
		ret = mtk_dpmaif_drv_ul_add_drb(drv_info, id, cnt);
		break;
	default:
		break;
	}

	return ret;
}

int mtk_dpmaif_drv_get_ring_idx(struct dpmaif_drv_info *drv_info,
				enum dpmaif_drv_ring_idx index, u8 q_id)
{
	int ret = 0;

	switch (index) {
	case DPMAIF_PIT_WIDX:
		ret = mtk_dpmaif_drv_dl_get_pit_wridx(drv_info, q_id);
		break;
	case DPMAIF_PIT_RIDX:
		ret = mtk_dpmaif_drv_dl_get_pit_rdidx(drv_info, q_id);
		break;
	case DPMAIF_BAT_WIDX:
		ret = mtk_dpmaif_drv_dl_get_bat_wridx(drv_info, q_id);
		break;
	case DPMAIF_BAT_RIDX:
		ret = mtk_dpmaif_drv_dl_get_bat_ridx(drv_info, q_id);
		break;

	case DPMAIF_FRAG_WIDX:
		break;

	case DPMAIF_FRAG_RIDX:
		ret = mtk_dpmaif_drv_dl_get_frg_ridx(drv_info, q_id);
		break;

	case DPMAIF_DRB_WIDX:
		break;

	case DPMAIF_DRB_RIDX:
		ret = mtk_dpmaif_drv_ul_get_drb_ridx(drv_info, q_id);
		break;
	default:
		break;
	}

	return ret;
}

static void mtk_dpmaif_drv_intr_coalesce_set(struct dpmaif_drv_info *drv_info,
					     struct dpmaif_drv_intr *intr)
{
	u8 i;

	if (intr->dir == DPMAIF_TX) {
		for (i = 0; i < drv_info->cfg->tx_cfg.txq_cnt; i++) {
			if (intr->q_mask & BIT(i))
				mtk_dpmaif_drv_ul_set_delay_intr(drv_info, i, intr->mode,
								 intr->time_threshold,
								 intr->pkt_threshold);
		}
	} else {
		for (i = 0; i < drv_info->cfg->rx_cfg.rxq_cnt; i++) {
			if (intr->q_mask & BIT(i))
				mtk_dpmaif_drv_dl_set_delay_intr(drv_info, i, intr->mode,
								 intr->time_threshold,
								 intr->pkt_threshold);
		}
	}
}

static int mtk_dpmaif_drv_hash_sec_key_set(struct dpmaif_drv_info *drv_info, u8 *hash_key)
{
	u32 i, cnt = 0;
	u32 index;
	u32 val;

	for (i = 0; i < (u32)DPMAIF_HASH_SEC_KEY_NUM >> 2; i++) {
		index = i << 2;
		val = hash_key[index] << 24 | hash_key[index + 1] << 16 |
			hash_key[index + 2] << 8 | hash_key[index + 3];
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_REG_HASH_SEC_KEY_0 + index, val);
	}

	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HASH_SEC_KEY_UPD, 1);

	do {
		if (!mtk_pci_read32(drv_info->mdev,
				    drv_info->regs->pd_base + NRL2_REG_HASH_SEC_KEY_UPD))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to set hash security key\n");

	return -DATA_HW_REG_TIMEOUT;
}

static void mtk_dpmaif_drv_hash_sec_key_get(struct dpmaif_drv_info *drv_info, u8 *hash_key)
{
	u32 index;
	u32 val;
	u32 i;

	for (i = 0; i < DPMAIF_HASH_SEC_KEY_NUM >> 2; i++) {
		index = i << 2;
		val = mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->pd_base + NRL2_REG_HASH_SEC_KEY_0 + index);
		hash_key[index] = val >> 24 & 0xFF;
		hash_key[index + 1] = val >> 16 & 0xFF;
		hash_key[index + 2] = val >> 8 & 0xFF;
		hash_key[index + 3] = val & 0xFF;
	}
}

static void mtk_dpmaif_drv_hash_indir_mask_set(struct dpmaif_drv_info *drv_info, u32 mask)
{
	u32 val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HASH_CFG_CON);

	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HASH_CFG_CON,
			(val & DPMAIF_HASH_INDR_MASK) | (mask << 16));
}

static u32 mtk_dpmaif_drv_hash_indir_mask_get(struct dpmaif_drv_info *drv_info)
{
	u32 val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + NRL2_REG_HASH_CFG_CON);

	return (val & (~DPMAIF_HASH_INDR_MASK)) >> 16;
}

static void mtk_dpmaif_drv_hash_indir_get(struct dpmaif_drv_info *drv_info, u32 *indir)
{
	u32 val = mtk_dpmaif_drv_hash_indir_mask_get(drv_info);
	u8 i;

	for (i = 0; i < DPMAIF_HASH_INDR_SIZE; i++) {
		if (val & BIT(i))
			indir[i] = 1;
		else
			indir[i] = 0;
	}
}

static void mtk_dpmaif_drv_hash_indir_set(struct dpmaif_drv_info *drv_info, u32 *indir)
{
	u32 val = 0;
	u8 i;

	for (i = 0; i < DPMAIF_HASH_INDR_SIZE; i++) {
		if (indir[i])
			val |= BIT(i);
	}

	mtk_dpmaif_drv_hash_indir_mask_set(drv_info, val);
}

static void mtk_dpmaif_drv_agg_cfg(struct dpmaif_drv_info *drv_info, bool enable)
{
	u32 agg_max_len;
	u32 threshold;
	u32 cfg;

	cfg = mtk_pci_read32(drv_info->mdev,
			     drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_RDY_CHK_FRG_THRES);

	/* enable/disable AGG cfg */
	if (enable) {
		threshold = cfg | (0xFF << 20);
		agg_max_len = (DPMAIF_AGG_TBL_ENT_NUM_DF & 0xFFFF) << 16;
	} else {
		threshold = cfg & 0xF00FFFFF;
		agg_max_len = 0x1 << 16;
	}

	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_RDY_CHK_FRG_THRES, threshold);
	cfg = DPMAIF_AGG_MAX_LEN_DF & 0xFFFF;
	cfg |= agg_max_len;

	/* Configuration include agg max length, agg table number. */
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_LRO_AGG_CFG, cfg);
}

static void mtk_dpmaif_drv_lro_set(struct dpmaif_drv_info *drv_info, void *data)
{
	bool dpmaif_lro_enable = *(bool *)data;
	u32 cfg;

	cfg = mtk_pci_read32(drv_info->mdev,
			     drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_RDY_CHK_FRG_THRES);

	if (dpmaif_lro_enable && !(drv_info->features & DATA_HW_F_LRO)) {
		drv_info->features |= DATA_HW_F_LRO;
		mtk_dpmaif_drv_agg_cfg(drv_info, dpmaif_lro_enable);
		mtk_dpmaif_drv_set_dlq_timeout_threshold(drv_info);
	} else if (!dpmaif_lro_enable && drv_info->features & DATA_HW_F_LRO) {
		drv_info->features &= ~DATA_HW_F_LRO;
		mtk_dpmaif_drv_agg_cfg(drv_info, dpmaif_lro_enable);
		mtk_dpmaif_drv_clr_dlq_timeout_threshold(drv_info);
	}
}

static int mtk_dpmaif_drv_ul_set_delay_intr(struct dpmaif_drv_info *drv_info,
					    u8 q_num, u8 mode, u32 time_us, u32 pkt_cnt)
{
	u32 ret = 0, cfg;

	cfg = ((mode & 0x3) << 30) | ((pkt_cnt & 0x3FFF) << 16) | (time_us & 0xFFFF);

	switch (q_num) {
	case 0:
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DLY_IRQ_TIMER3, cfg);
		break;
	case 1:
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DLY_IRQ_TIMER4, cfg);
		break;
	case 2:
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DLY_IRQ_TIMER5, cfg);
		break;
	case 3:
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DLY_IRQ_TIMER6, cfg);
		break;
	case 4:
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_DLY_IRQ_TIMER7, cfg);
		break;
	default:
		MTK_WARN(drv_info->mdev, "Invalid ulq=%d!\n", q_num);
		ret = -EINVAL;
	}

	return ret;
}

static int mtk_dpmaif_drv_dl_set_delay_intr(struct dpmaif_drv_info *drv_info,
					    u8 q_num, u8 mode, u32 time_us, u32 pkt_cnt)
{
	int ret = 0;
	u32 cfg = 0;

	cfg = ((mode & 0x3) << 30) | ((pkt_cnt & 0x3FFF) << 16) | (time_us & 0xFFFF);

	switch (q_num) {
	case 0:
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_DLY_IRQ_TIMER1, cfg);
		break;
	case 1:
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_DLY_IRQ_TIMER2, cfg);
		break;
	default:
		MTK_WARN(drv_info->mdev, "Invalid dlq=%d!\n", q_num);
		ret = -EINVAL;
	}

	return ret;
}

static void mtk_dpmaif_drv_clr_dlq_timeout_threshold(struct dpmaif_drv_info *drv_info)
{
	u32 val, i;

	for (i = 0; i < DPMAIF_HPC_NUM_DF; i++) {
		val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				     NRL2_DPMAIF_AO_DL_LROPIT_TIMEOUT1 + ((i >> 1) << 2));

		if (i % 2)
			val = (val & 0xFFFF) | BIT(16);
		else
			val = (val & 0xFFFF0000) | 1;

		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AO_DL_LROPIT_TIMEOUT1 + ((i >> 1) << 2), val);
	}

	if (drv_info->priv_ops && drv_info->priv_ops->clr_dlq_timeout)
		drv_info->priv_ops->clr_dlq_timeout(drv_info);
}

void mtk_dpmaif_drv_ul_mask_intr(struct dpmaif_drv_info *drv_info, u32 mask)
{
	u32 cnt = 0;

	if (drv_info->priv_ops && drv_info->priv_ops->mask_ul_intr) {
		drv_info->priv_ops->mask_ul_intr(drv_info, mask);
		return;
	}

	mtk_pci_write32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TISR0,
			mask);
	mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TISR0);

	do {
		if ((mtk_pci_read32(drv_info->mdev,
				    drv_info->regs->ao_base + DPMAIF_PD_AP_UL_L2TIMR0) &
				    mask) == mask)
			return;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to mask interrupt done, sta=0x%08x\n",
		mtk_pci_read32(drv_info->mdev, drv_info->regs->ao_base +
		DPMAIF_PD_AP_UL_L2TIMR0));

	WARN_ON_ONCE(true);
}

static void mtk_dpmaif_drv_features_get(struct dpmaif_drv_info *drv_info, void *data)
{
	*(u32 *)data = drv_info->features;
}

static int mtk_dpmaif_drv_txq_get(struct dpmaif_drv_info *drv_info, void *data)
{
	struct dpmaif_drv_pkt_info *pkt_info = (struct dpmaif_drv_pkt_info *)data;
	int q_id;

	switch (pkt_info->prio) {
	case PKT_PRIO_1:
		q_id = 2;
		break;
	case PKT_PRIO_2:
		q_id = 1;
		break;
	case PKT_PRIO_3:
		q_id = 3;
		break;
	case PKT_PRIO_0:
		fallthrough;
	default:
		q_id = (pkt_info->skb_hash & 0x01) ? 0 : 4;
		break;
	}
	return q_id;
}

int mtk_dpmaif_drv_feature_cmd_com(struct dpmaif_drv_info *drv_info,
				   enum dpmaif_drv_cmd cmd, void *data)
{
	int ret = 0;

	switch (cmd) {
	case DATA_HW_TXQ_GET:
		ret = mtk_dpmaif_drv_txq_get(drv_info, data);
		break;
	case DATA_HW_FEATURES_GET:
		mtk_dpmaif_drv_features_get(drv_info, data);
		break;
	case DATA_HW_RXCSUM_SET:
		if (!(drv_info->cfg->cap & DATA_HW_F_RXCSUM))
			return -EOPNOTSUPP;
		mtk_dpmaif_drv_rxcsum_set(drv_info, data);
		break;
	case DATA_HW_TXCSUM_SET:
		if (!(drv_info->cfg->cap & DATA_HW_F_TXCSUM))
			return -EOPNOTSUPP;
		mtk_dpmaif_drv_txcsum_set(drv_info, data);
		break;
	case DATA_HW_INTR_COALESCE_SET:
		if (!(drv_info->cfg->cap & DATA_HW_F_INTR_COALESCE))
			return -EOPNOTSUPP;
		mtk_dpmaif_drv_intr_coalesce_set(drv_info, data);
		break;
	case DATA_HW_HASH_GET:
		if (!(drv_info->cfg->cap & DATA_HW_F_HASH))
			return -EOPNOTSUPP;
		mtk_dpmaif_drv_hash_sec_key_get(drv_info, data);
		break;
	case DATA_HW_HASH_SET:
		if (!(drv_info->cfg->cap & DATA_HW_F_HASH))
			return -EOPNOTSUPP;
		ret = mtk_dpmaif_drv_hash_sec_key_set(drv_info, data);
		break;
	case DATA_HW_HASH_KEY_SIZE_GET:
		if (!(drv_info->cfg->cap & DATA_HW_F_HASH))
			return -EOPNOTSUPP;
		*(u32 *)data = DPMAIF_HASH_SEC_KEY_NUM;
		break;
	case DATA_HW_INDIR_GET:
		if (!(drv_info->cfg->cap & DATA_HW_F_INDR_TBL))
			return -EOPNOTSUPP;
		mtk_dpmaif_drv_hash_indir_get(drv_info, data);
		break;
	case DATA_HW_INDIR_SET:
		if (!(drv_info->cfg->cap & DATA_HW_F_INDR_TBL))
			return -EOPNOTSUPP;
		mtk_dpmaif_drv_hash_indir_set(drv_info, data);
		break;
	case DATA_HW_INDIR_SIZE_GET:
		if (!(drv_info->cfg->cap & DATA_HW_F_INDR_TBL))
			return -EOPNOTSUPP;
		*(u32 *)data = DPMAIF_HASH_INDR_SIZE;
		break;
	case DATA_HW_LRO_SET:
		if (!(drv_info->cfg->cap & DATA_HW_F_LRO))
			return -EOPNOTSUPP;
		mtk_dpmaif_drv_lro_set(drv_info, data);
		break;

	default:
		dev_info(drv_info->mdev->dev, "Unsupport cmd=%d\n", cmd);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

void mtk_dpmaif_drv_dump(struct dpmaif_drv_info *drv_info)
{
	const struct dpmaif_dump_regs *dump_regs;
	int i;

	dump_regs = drv_info->cfg->dump_cfg.dump_regs;

	MTK_DBG(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP, "==dpmaif regs dump ==\n");
	for (i = 0; i < drv_info->cfg->dump_cfg.cnt; i++) {
		MTK_DBG(drv_info->mdev, MTK_DBG_DPMF,
			MTK_MEMLOG_RG_DATA_DUMP, "%s: 0x%lx, len=0x%x\n",
			dump_regs->name, dump_regs->base_addr, dump_regs->length);
		MTK_REGS_DUMP(drv_info->mdev, MTK_DBG_DPMF, MTK_MEMLOG_RG_DATA_DUMP,
			      NULL, dump_regs->base_addr, dump_regs->length);
		mtk_drv_delay(50);
		dump_regs++;
	}
}

