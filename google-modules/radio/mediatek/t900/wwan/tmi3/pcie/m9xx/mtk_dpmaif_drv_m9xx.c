// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2022, MediaTek Inc.
 */

#define pr_fmt(fmt) "DATA_DRV_M9XX: " fmt

#include <linux/delay.h>

#include "mtk_data_plane.h"
#include "mtk_debug.h"
#include "mtk_dev.h"
#include "mtk_dpmaif_drv.h"
#include "mtk_dpmaif_reg_m9xx.h"
#include "mtk_dpmaif_ring.h"
#include "mtk_pci.h"
#include "mtk_pcie_memlog.h"
#include "mtk_pm.h"

#ifdef CONFIG_UT_PCIE_DPMAIF_DRV
#include "ut_dpmaif_drv.h"
#include "ut_dpmaif_drv_m9xx.h"
#endif

#define TAG "DATA_DRV_M9XX"

enum dpmaif_drv_irq_src {
	DPMAIF_IRQ_SRC0_DLQ0,
	DPMAIF_IRQ_SRC1_DLQ1,
	DPMAIF_IRQ_SRC2_UL_DONE,
	DPMAIF_IRQ_SRC3_DLQ2,
	DPMAIF_IRQ_SRC4_TRANS_SYNC
};

static int mtk_dpmaif_drv_irq_src0(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info);
static int mtk_dpmaif_drv_irq_src1(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info);
static int mtk_dpmaif_drv_irq_src2(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info);
static int mtk_dpmaif_drv_irq_src3(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info);
static int mtk_dpmaif_drv_irq_src4(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info);

static const struct dpmaif_drv_regs regs = {
	.ao_base = DPMAIF_DEV_AO_BASE,
	.pd_base = DPMAIF_DEV_PD_BASE,
	.pd2_base = DPMAIF_DEV_PD2_BASE,
	.ao_ul_ch0_sta = NRL2_DPMAIF_AO_UL_CH0_STA,
};

static const unsigned char tx_srv0_vqs[] = {3};
static const unsigned char tx_srv1_vqs[] = {1};
static const unsigned char tx_srv2_vqs[] = {0, 4};
static const struct dpmaif_tx_srv_cfg tx_srvs[] = {
			{
				.vq_cnt = ARRAY_SIZE(tx_srv0_vqs),
				.vqs = tx_srv0_vqs,
				.nice = -20,
			},
			{
				.vq_cnt = ARRAY_SIZE(tx_srv1_vqs),
				.vqs = tx_srv1_vqs,
				.nice = -15,
			},
			{
				.vq_cnt = ARRAY_SIZE(tx_srv2_vqs),
				.vqs = tx_srv2_vqs,
				.nice = -10,
			},
};

static struct dpmaif_txq_cfg txqs[] = {
	{
		.drb_cnt = 2048,
		.doorbell_delay = 10,
		.burst_pkts = 128,
		.attr = DPMAIFQ_ATTR_NONE,
		.tx_coalesce_usecs = 4,
		.tx_coalesced_frames = 64,
	},
	{
		.drb_cnt = 2048,
		.doorbell_delay = 2,
		.burst_pkts = 32,
		.attr = DPMAIFQ_ATTR_NONE,
		.tx_coalesce_usecs = 4,
		.tx_coalesced_frames = 64,
	},
	{
		.drb_cnt = 128,
		.doorbell_delay = 0,
		.burst_pkts = 0,
		.attr = DPMAIFQ_ATTR_LOW_LATENCY,
		.tx_coalesce_usecs = 0,
		.tx_coalesced_frames = 0,
	},
	{
		.drb_cnt = 1024,
		.doorbell_delay = 0,
		.burst_pkts = 128,
		.attr = DPMAIFQ_ATTR_NONE,
		.tx_coalesce_usecs = 4,
		.tx_coalesced_frames = 64,
	},
	{
		.drb_cnt = 2048,
		.doorbell_delay = 10,
		.burst_pkts = 128,
		.attr = DPMAIFQ_ATTR_NONE,
		.tx_coalesce_usecs = 4,
		.tx_coalesced_frames = 64,
	},
};

static struct dpmaif_bat_cfg bats[] = {
	{
	.bat_cnt = 32768,
	.reload_cnt = 3072,
	},
	{
	.bat_cnt = 1024,
	.reload_cnt = 512,
	},
};

static struct dpmaif_bat_cfg frags[] = {
	{
	.bat_cnt = 8192,
	.reload_cnt = 1024,
	},
	{
	.bat_cnt = 1024,
	.reload_cnt = 64,
	},
};

static struct dpmaif_rxq_cfg rxqs[] = {
	{
	.pit_cnt = 32768,
	.bat_ring_id = 0,
	.frag_ring_id = 0,
	.pit_seq_max = 251,
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	.qsize = 32768,
	.attr = DPMAIFQ_ATTR_PIT_CACHED,
#endif
	},
	{
	.pit_cnt = 32768,
	.bat_ring_id = 0,
	.frag_ring_id = 0,
	.pit_seq_max = 251,
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	.qsize = 32768,
	.attr = DPMAIFQ_ATTR_PIT_CACHED,
#endif
	},
	{
	.pit_cnt = 2048,
	.bat_ring_id = 1,
	.frag_ring_id = 1,
	.pit_seq_max = 251,
#ifdef CONFIG_DATA_CPU_LOADING_OPTIMIZE
	.attr = DPMAIFQ_ATTR_PIT_CACHED | DPMAIFQ_ATTR_LOW_LATENCY,
#endif
	},
};

static const struct dpmaif_irq_cfg irqs[] = {
	{
	.id = MTK_IRQ_SRC_DPMAIF,
	.handle = mtk_dpmaif_drv_irq_src0,
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
	.cpu_mask = 0x2,
#endif
	},
	{
	.id = MTK_IRQ_SRC_DPMAIF2,
	.handle = mtk_dpmaif_drv_irq_src1,
#ifdef CONFIG_MTK_DATA_FEATURE_TEST
	.cpu_mask = 0x4,
#endif
	},
	{
	.id = MTK_IRQ_SRC_DPMAIF3,
	.handle = mtk_dpmaif_drv_irq_src2,
	},
	{
	.id = MTK_IRQ_SRC_DPMAIF6,
	.handle = mtk_dpmaif_drv_irq_src3,
	},
	{
	.id = MTK_IRQ_SRC_TRAS_SYNC,
	.handle = mtk_dpmaif_drv_irq_src4,
	},
};

static const struct dpmaif_dump_regs dump_regs[] = {
	{
	.name = "nrl2_dpmaif_ul_ao_cfg_mmw",
	.base_addr = 0x10011000,
	.length = 0xff,
	},
	{
	.name = "nrl2_dpmaif_dl_ao_mmw",
	.base_addr = 0x10011400,
	.length = 0xff,
	},
	{
	.name = "nrl2_dpmaif_misc_ao_cfg_mmw",
	.base_addr = 0x10011800,
	.length = 0xff,
	},
	{
	.name = "nrl2_dpmaif_ul_cfg",
	.base_addr = 0x1022D000,
	.length = 0xff,
	},
	{
	.name = "mmw_dpmaif_dl_cfg",
	.base_addr = 0x1022D100,
	.length = 0xff,
	},
	{
	.name = "mmw_dpmaif_rdma",
	.base_addr = 0x1022D200,
	.length = 0xff,
	},
	{
	.name = "mmw_dpmaif_wdma",
	.base_addr = 0x1022D300,
	.length = 0xff,
	},
	{
	.name = "nrl2_dpmaif_ap_misc_cfg",
	.base_addr = 0x1022D400,
	.length = 0xff,
	},
	{
	.name = "mmw_dpmaif_pd_sram_dl_cfg",
	.base_addr = 0x1022DC00,
	.length = 0xff,
	},
	{
	.name = "mmw_dpmaif_pd_sram_ul_cfg",
	.base_addr = 0x1022DD00,
	.length = 0xff,
	},
	{
	.name = "mmw_dpmaif_pd_sram_misc_cfg",
	.base_addr = 0x1022DE00,
	.length = 0xff,
	},
	{
	.name = "mmw_dpmaif_pd_sram_misc2_cfg",
	.base_addr = 0x1022DF00,
	.length = 0xff,
	},
	{
	.name = "md_dpmaif_pd_sram_dl_cfg2",
	.base_addr = 0x10260000,
	.length = 0xff,
	},
	{
	.name = "nrl2_dpmaif_dl_2_cfg",
	.base_addr = 0x10261000,
	.length = 0xff,
	},
};

static struct dpmaif_drv_cfg drv_cfg = {
	.cap = DATA_HW_F_LRO | DATA_HW_F_INDR_TBL | DATA_HW_F_INTR_COALESCE |
		DATA_HW_F_RXCSUM | DATA_HW_F_TXCSUM | DATA_HW_F_HASH | DATA_HW_F_HPC |
		DATA_HW_F_HPC_STATS | DATA_HW_F_AGG | DATA_HW_F_FRAG | DATA_HW_F_TRAS_ALIGN,
	.tx_srvs_cfg = {
		.tx_vq_cnt = ARRAY_SIZE(txqs),
		.tx_srv_cnt = ARRAY_SIZE(tx_srvs),
		.tx_srvs = tx_srvs,
	},
	.tx_cfg = {
		.txq_cnt = ARRAY_SIZE(txqs),
		.txqs = txqs,
	},
	.rx_cfg = {
		.normal_bat_rsv_length = 0,
		.pkt_alignment = 64,
		.mtu = 9000,
		.mode = DPMAIF_DL_M1,
		.bat_ring_num = ARRAY_SIZE(bats),
		.bats = bats,
		.frag_ring_num = ARRAY_SIZE(frags),
		.frags = frags,
		.rxq_cnt = ARRAY_SIZE(rxqs),
		.indir_rxq_cnt = 2,
		.rxqs = rxqs,
		.bat_wrap_cnt = 48,
	},
	.intr_cfg = {
		.ul3_l2intrs_disable = DPMAIF_UL3_INT_VALID_MSK & ~DPMAIF_UL_INT_QDONE_MSK,
		.ul3_l2intrs_enable = DPMAIF_UL_INT_QDONE_MSK,
		.ul_l2intrs_disable = DPMAIF_UL_INT_VALID_MSK & ~DPMAIF_UL_INT_QDONE_MSK,
		.ul_l2intrs_enable = DPMAIF_UL_INT_QDONE_MSK,
		.dl_l2intrs_disable = DPMAIF_DL_INT_VALID_MSK & ~(DPMAIF_DL_INT_DLQ0_QDONE_MSK |
			DPMAIF_DL_INT_DLQ1_QDONE_MSK | DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR_MSK |
			DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR_MSK | DPMAIF_DL_INT_BATCNT_LEN_ERR_MSK |
			DPMAIF_DL_INT_FRG_LEN_ERR_MSK),
		.dl_l2intrs_enable = DPMAIF_DL_INT_DLQ0_QDONE_MSK |
			DPMAIF_DL_INT_DLQ1_QDONE_MSK | DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR_MSK |
			DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR_MSK | DPMAIF_DL_INT_BATCNT_LEN_ERR_MSK |
			DPMAIF_DL_INT_FRG_LEN_ERR_MSK,
		.dl2_l2intrs_disable = DPMAIF_DL2_INT_OTHER_MSK & ~(DPMAIF_DL_INT_DLQ2_QDONE_MSK |
			DPMAIF_DL_INT_DLQ2_PITCNT_LEN_ERR_MSK | DPMAIF_DL2_INT_BATCNT_LEN_ERR_MSK |
			DPMAIF_DL2_INT_FRG_LEN_ERR_MSK),
		.dl2_l2intrs_enable = DPMAIF_DL_INT_DLQ2_QDONE_MSK |
			DPMAIF_DL_INT_DLQ2_PITCNT_LEN_ERR_MSK | DPMAIF_DL2_INT_BATCNT_LEN_ERR_MSK |
			DPMAIF_DL2_INT_FRG_LEN_ERR_MSK,
		.udl_ip_busy_disable = DPMAIF_UDL_IP_BUSY_MSK,
		.hpc_disable = DPMAIF_DL_INT_Q2APTOP_MSK |
			DPMAIF_DL_INT_Q2TOQ1_MSK | DPMAIF_UL_TOP0_INT_MSK,
		.irq_cnt = ARRAY_SIZE(irqs),
		.irqs = irqs,
	},
	.dump_cfg = {
		.cnt = ARRAY_SIZE(dump_regs),
		.dump_regs = dump_regs,
	},
};

static void mtk_dpmaif_drv_set_pcie_domain(struct dpmaif_drv_info *drv_info)
{
	/* set HW pcie domain */
	mtk_pci_write32(drv_info->mdev,
			drv_info->regs->pd2_base + DPMAIF_DL_RESOURCE_DOMAIN, 0x000A9AAA);
}

static u32 mtk_dpmaif_drv_get_ul_intr_mask_m9xx(struct dpmaif_drv_info *drv_info)
{
	return mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				DPMAIF_PD_AP_UL_L2TIMR0_NEXT);
}

static int mtk_dpmaif_drv_dynamic_sram_init(struct dpmaif_drv_info *drv_info)
{
	u32 sram_addr_tbl[][2] = {
			{0xFFFF0100, 0x8033F000},
			{0xFFFF0200, 0x801DF000},
			{0xFFFF0400, 0x805FF000},
			{0xFFFF1000, 0x8033F000},
			{0xFFFF4000, 0x80443000},
			{0xFFFF8000, 0x80177000},
	};
	u32 sram_cnt;
	u32 cnt = 0;
	u32 i;

	sram_cnt = ARRAY_SIZE(sram_addr_tbl);
	for (i = 0; i < sram_cnt; i++) {
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AP_MISC_SRAM_INIT_SET1, sram_addr_tbl[i][0]);
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AP_MISC_SRAM_INIT_SET0, sram_addr_tbl[i][1]);

		do {
			if (!(mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AP_MISC_SRAM_INIT_SET0) & DPMAIF_AP_PD_SRAM_EN_BIT))
				break;

			udelay(POLL_INTERVAL_US);
		} while (++cnt < POLL_MAX_TIMES);

		if (cnt >= POLL_MAX_TIMES) {
			MTK_ERR(drv_info->mdev, "Failed to initialize dynamic sram%d\n", i);
			return -DATA_HW_REG_TIMEOUT;
		}
		cnt = 0;
	}

	return 0;
}

static int mtk_dpmaif_drv_ul_intr_init_m9xx(struct dpmaif_drv_info *drv_info)
{
	const struct dpmaif_intr_cfg *intr_cfg = &drv_info->cfg->intr_cfg;
	u32 cnt = 0;
	int ret;

	ret = mtk_dpmaif_drv_ul_intr_init(drv_info);
	if (unlikely(ret < 0))
		return ret;

	/* clear UL interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISAR0,
			0xFFFFFFFF);
	/* unmask ul3_l2intrs_enable interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TICR0_NEXT,
			intr_cfg->ul3_l2intrs_enable);
	/* mask ul3_l2intrs_disable interrupt */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISR0_NEXT,
			intr_cfg->ul3_l2intrs_disable);
	mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISR0_NEXT);
	do {
		if (((mtk_pci_read32(drv_info->mdev,
				     drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TIMR0_NEXT) &
					  intr_cfg->ul3_l2intrs_disable) ==
					  intr_cfg->ul3_l2intrs_disable))
			break;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	if (cnt >= POLL_MAX_TIMES) {
		MTK_ERR(drv_info->mdev, "Failed to set UL interrupt mask, mask=0x%08x\n",
			mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
				DPMAIF_PD_AP_UL_L2TIMR0_NEXT));
		return -DATA_HW_REG_TIMEOUT;
	}

	return 0;
}

static int mtk_dpmaif_drv_mask_ulq_intr(struct dpmaif_drv_info *drv_info, u32 q_num)
{
	u32 cnt = 0, ui_que_done_mask;

	ui_que_done_mask = BIT(q_num + DP_UL_INT_DONE_OFFSET) & DPMAIF_UL_INT_QDONE_MSK;

	do {
		mtk_pci_write32(drv_info->mdev,
				drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISR0_NEXT,
				ui_que_done_mask);
		mtk_pci_read32(drv_info->mdev,
			       drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISR0_NEXT);
		if ((mtk_pci_read32(drv_info->mdev,
				    drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TIMR0_NEXT) &
				    ui_que_done_mask))
			return 0;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to mask ulq%u interrupt done, sta=0x%08x\n", q_num,
		mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
			DPMAIF_PD_AP_UL_L2TIMR0_NEXT));

	return -DATA_HW_REG_TIMEOUT;
}

static void mtk_dpmaif_drv_unmask_ulq_intr(struct dpmaif_drv_info *drv_info, u32 q_num)
{
	u32 ui_que_done_mask;

	ui_que_done_mask = BIT(q_num + DP_UL_INT_DONE_OFFSET) & DPMAIF_UL_INT_QDONE_MSK;
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TICR0_NEXT,
			ui_que_done_mask);
}

static void mtk_dpmaif_drv_mask_ul_intr(struct dpmaif_drv_info *drv_info, u32 mask)
{
	u32 cnt = 0;

	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISR0_NEXT,
			mask);
	mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISR0_NEXT);

	do {
		if ((mtk_pci_read32(drv_info->mdev,
				    drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TIMR0_NEXT) &
					mask) == mask)
			return;

		udelay(POLL_INTERVAL_US);
	} while (++cnt < POLL_MAX_TIMES);

	MTK_ERR(drv_info->mdev, "Failed to mask interrupt done, sta=0x%08x\n",
		mtk_pci_read32(drv_info->mdev, drv_info->regs->pd_base +
			DPMAIF_PD_AP_UL_L2TIMR0_NEXT));

	WARN_ON_ONCE(true);
}

static void mtk_dpmaif_drv_set_hpc_cntl_m9xx(struct dpmaif_drv_info *drv_info)
{
	u32 cfg;

	cfg = DPMAIF_HPC_LRO_PATH_DF & 0x3;
	cfg |= (DPMAIF_HPC_ADD_MODE_DF & 0x3) << 2;
	cfg |= (DPMAIF_HASH_PRIME_DF & 0xF) << 4;
	cfg |= (DPMAIF_HPC_NUM_M9XX & 0xFF) << 8;

	/* Configuration include hpc dlq path, hpc add mode, hash prime, hpc total number. */
	mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + NRL2_DPMAIF_AO_DL_HPC_CNTL, cfg);
}

static void mtk_dpmaif_drv_set_dlq_timeout_m9xx(struct dpmaif_drv_info *drv_info)
{
	u32 val, i;

	for (i = 0; i < DPMAIF_HPC_NUM_EXT; i++) {
		val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd2_base +
				     NRL2_DPMAIF_PD_DL_LROPIT_TIMEOUT5 + ((i >> 1) << 2));

		if (i % 2)
			val = (val & 0xFFFF) | (DPMAIF_LRO_TIMEOUT_THRES_DF << 16);
		else
			val = (val & 0xFFFF0000) | DPMAIF_LRO_TIMEOUT_THRES_DF;

		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd2_base +
				NRL2_DPMAIF_PD_DL_LROPIT_TIMEOUT5 + ((i >> 1) << 2), val);
	}
}

static void mtk_dpmaif_drv_clr_dlq_timeout_m9xx(struct dpmaif_drv_info *drv_info)
{
	u32 val, i;

	for (i = 0; i < DPMAIF_HPC_NUM_EXT; i++) {
		val = mtk_pci_read32(drv_info->mdev, drv_info->regs->pd2_base +
				     NRL2_DPMAIF_PD_DL_LROPIT_TIMEOUT5 + ((i >> 1) << 2));

		if (i % 2)
			val = (val & 0xFFFF) | BIT(16);
		else
			val = (val & 0xFFFF0000) | 1;

		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd2_base +
				NRL2_DPMAIF_PD_DL_LROPIT_TIMEOUT5 + ((i >> 1) << 2), val);
	}
}

static struct dpmaif_priv_ops dpmaif_priv_ops_m9xx = {
	.set_pcie_domain = mtk_dpmaif_drv_set_pcie_domain,
	.get_ul_intr_mask = mtk_dpmaif_drv_get_ul_intr_mask_m9xx,
	.dynamic_sram_init = mtk_dpmaif_drv_dynamic_sram_init,
	.ul_intr_init = mtk_dpmaif_drv_ul_intr_init_m9xx,
	.mask_ulq_intr = mtk_dpmaif_drv_mask_ulq_intr,
	.unmask_ulq_intr = mtk_dpmaif_drv_unmask_ulq_intr,
	.mask_ul_intr = mtk_dpmaif_drv_mask_ul_intr,
	.set_hpc_cntl = mtk_dpmaif_drv_set_hpc_cntl_m9xx,
	.set_dlq_timeout = mtk_dpmaif_drv_set_dlq_timeout_m9xx,
	.clr_dlq_timeout = mtk_dpmaif_drv_clr_dlq_timeout_m9xx,
};

static void mtk_dpmaif_drv_cfg_get(struct dpmaif_drv_info *drv_info, void *data)
{
	drv_info->regs = &regs;
	drv_info->cfg = &drv_cfg;
	drv_info->features = drv_cfg.cap;
	drv_info->priv_ops = &dpmaif_priv_ops_m9xx;
}

static int mtk_dpmaif_drv_feature_cmd_m9xx(struct dpmaif_drv_info *drv_info,
					   enum dpmaif_drv_cmd cmd, void *data)
{
	int ret = 0;

	switch (cmd) {
	case DATA_HW_CFG_GET:
		mtk_dpmaif_drv_cfg_get(drv_info, data);
		break;
	default:
		ret = mtk_dpmaif_drv_feature_cmd_com(drv_info, cmd, data);
		break;
	}

	return ret;
}

static void mtk_dpmaif_drv_reset(struct dpmaif_drv_info *drv_info)
{
	u32 mask;
	/* Set DPMAIF infra CG en */
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_INFRA_CG_SET, DPMAIF_AP_INFRA_CG_BIT);
	udelay(2);

	/* Glitch protect on */
	mask = mtk_pci_read32(drv_info->mdev, DPMAIF_AP_INFRA_GLITCH_PORT);
	mask &= ~DPMAIF_AP_GLITCH_PROT_BIT;
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_INFRA_GLITCH_PORT, mask);
	udelay(2);

	/* AO&PD reset assert&de-assert */
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_AO_RGU_ASSERT, DPMAIF_AP_AO_RST_BIT);
	udelay(2);
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_RGU_ASSERT, DPMAIF_AP_RST_BIT);
	udelay(2);
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_AO_RGU_DEASSERT, DPMAIF_AP_AO_RST_BIT);
	udelay(2);
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_RGU_DEASSERT, DPMAIF_AP_RST_BIT);
	udelay(2);

	/* Glitch protect off */
	mask = mtk_pci_read32(drv_info->mdev, DPMAIF_AP_INFRA_GLITCH_PORT);
	mask |= DPMAIF_AP_GLITCH_PROT_BIT;
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_INFRA_GLITCH_PORT, mask);
	udelay(2);

	/* Set DPMAIF infra CG off */
	mtk_pci_write32(drv_info->mdev, DPMAIF_AP_INFRA_CG_CLR, DPMAIF_AP_INFRA_CG_BIT);
	udelay(2);
}

static u32 mtk_dpmaif_drv_irq_src0_filter(struct dpmaif_drv_info *drv_info, u32 l2risar0,
					  u32 l2rimr0)
{
	if (l2rimr0 & DPMAIF_DL_INT_DLQ0_QDONE_MSK)
		l2risar0 &= ~DPMAIF_DL_INT_DLQ0_QDONE;

	if (l2rimr0 & DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR_MSK)
		l2risar0 &= ~DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR;

	if (l2rimr0 & DPMAIF_DL_INT_FRG_LEN_ERR_MSK)
		l2risar0 &= ~DPMAIF_DL_INT_FRG_LEN_ERR;

	if (l2rimr0 & DPMAIF_DL_INT_BATCNT_LEN_ERR_MSK)
		l2risar0 &= ~DPMAIF_DL_INT_BATCNT_LEN_ERR;

	return l2risar0;
}

static int mtk_dpmaif_drv_irq_src0(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info)
{
	u32 val, ori_l2risar0, l2risar0, l2rimr0;

	ori_l2risar0 = mtk_dpmaif_drv_get_dl_lv2_sts(drv_info, DPMAIF_DLQ0);
	l2rimr0 = mtk_dpmaif_drv_get_dl_intr_mask(drv_info, DPMAIF_DLQ0);

	/* filter care interrupt status. */
	l2risar0 = ori_l2risar0 & (DPMAIF_DL_INT_DLQ0_QDONE | DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR |
		DPMAIF_DL_INT_BATCNT_LEN_ERR | DPMAIF_DL_INT_FRG_LEN_ERR |
		DPMAIF_DL_INT_DLQ_QDONE);
	if (l2risar0) {
		/* Filter to get DL unmasked interrupts */
		l2risar0 = mtk_dpmaif_drv_irq_src0_filter(drv_info, l2risar0, l2rimr0);

		val = l2risar0 & DPMAIF_DL_INT_BATCNT_LEN_ERR;
		if (val) {
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_BATCNT_LEN_ERR;
			intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_BAT0;
			intr_info->intr_cnt++;
			mtk_dpmaif_drv_mask_dl_batcnt_len_err_intr(drv_info, DPMAIF_BAT0);
		}

		val = l2risar0 & DPMAIF_DL_INT_FRG_LEN_ERR;
		if (val) {
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_FRGCNT_LEN_ERR;
			intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_BAT0;
			intr_info->intr_cnt++;
			mtk_dpmaif_drv_mask_dl_frgcnt_len_err_intr(drv_info, DPMAIF_BAT0);
		}

		val = l2risar0 & DPMAIF_DL_INT_DLQ0_PITCNT_LEN_ERR;
		if (val) {
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_PITCNT_LEN_ERR;
			intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_DLQ0;
			intr_info->intr_cnt++;
			mtk_dpmaif_drv_dlq_mask_pit_cnt_len_err_intr(drv_info, DPMAIF_DLQ0);
		}

		val = l2risar0 & DPMAIF_DL_INT_DLQ0_QDONE;
		if (val) {
			mtk_pm_ds_try_lock(drv_info->mdev, MTK_USER_DATA);
			if (!mtk_dpmaif_drv_dlq_mask_rx_done_intr(drv_info, DPMAIF_DLQ0)) {
				intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_DONE;
				intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_DLQ0;
				intr_info->intr_cnt++;
			}
		}

		/* Clear interrupt status. */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_DL_L2TISAR0,
				l2risar0);
	}

	mtk_dpmaif_drv_clr_ip_busy_sts(drv_info);

	MTK_DBG_DATA_IRQ_SRC_INFO(drv_info->mdev, DPMAIF_IRQ_SRC0_DLQ0, ori_l2risar0,
				  l2risar0, l2rimr0);

	return 0;
}

static u32 mtk_dpmaif_drv_irq_src1_filter(struct dpmaif_drv_info *drv_info, u32 l2risar0,
					  u32 l2rimr0)
{
	if (l2rimr0 & DPMAIF_DL_INT_DLQ1_QDONE_MSK)
		l2risar0 &= ~DPMAIF_DL_INT_DLQ1_QDONE;

	if (l2rimr0 & DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR_MSK)
		l2risar0 &= ~DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR;

	return l2risar0;
}

static int mtk_dpmaif_drv_irq_src1(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info)
{
	u32 val, ori_l2risar0, l2risar0, l2rimr0;

	ori_l2risar0 = mtk_dpmaif_drv_get_dl_lv2_sts(drv_info, DPMAIF_DLQ1);
	l2rimr0 = mtk_dpmaif_drv_get_dl_intr_mask(drv_info, DPMAIF_DLQ1);

	/* filter care interrupt status. */
	l2risar0 = ori_l2risar0 & (DPMAIF_DL_INT_DLQ1_QDONE | DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR);
	if (l2risar0) {
		/* Filter to get DL unmasked interrupts */
		l2risar0 = mtk_dpmaif_drv_irq_src1_filter(drv_info, l2risar0, l2rimr0);

		val = l2risar0 & DPMAIF_DL_INT_DLQ1_PITCNT_LEN_ERR;
		if (val) {
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_PITCNT_LEN_ERR;
			intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_DLQ1;
			intr_info->intr_cnt++;
			mtk_dpmaif_drv_dlq_mask_pit_cnt_len_err_intr(drv_info, DPMAIF_DLQ1);
		}

		val = l2risar0 & DPMAIF_DL_INT_DLQ1_QDONE;
		if (val) {
			mtk_pm_ds_try_lock(drv_info->mdev, MTK_USER_DATA);
			if (!mtk_dpmaif_drv_dlq_mask_rx_done_intr(drv_info, DPMAIF_DLQ1)) {
				intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_DONE;
				intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_DLQ1;
				intr_info->intr_cnt++;
			}
		}

		/* Clear interrupt status. */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_DL_L2TISAR0,
				l2risar0);
	}

	mtk_dpmaif_drv_clr_ip_busy_sts(drv_info);

	MTK_DBG_DATA_IRQ_SRC_INFO(drv_info->mdev, DPMAIF_IRQ_SRC1_DLQ1, ori_l2risar0,
				  l2risar0, l2rimr0);

	return 0;
}

static int mtk_dpmaif_drv_irq_src2(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info)
{
	u32 ori_l2tisar0, l2tisar0, l2timr0;
	static u32 cnt;
	u8 q_mask;
	u32 val;

	ori_l2tisar0 = mtk_dpmaif_drv_get_ul_lv2_sts(drv_info);
	l2timr0 = mtk_dpmaif_drv_get_ul_intr_mask(drv_info);

	/* Check and process interrupt. */
	l2tisar0 = ori_l2tisar0 & (~l2timr0);
	if (l2tisar0) {
		cnt = 0;
		val = l2tisar0 & DPMAIF_UL_INT_QDONE;
		if (val) {
			q_mask = val >> DP_UL_INT_DONE_OFFSET & DPMAIF_ULQS;
			mtk_dpmaif_drv_ul_mask_multi_tx_done_intr(drv_info, q_mask);
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_UL_DONE;
			intr_info->intr_queues[intr_info->intr_cnt] = val >> DP_UL_INT_DONE_OFFSET;
			intr_info->intr_cnt++;
		} else {
			mtk_dpmaif_drv_ul_mask_intr(drv_info, l2tisar0);
		}

		/* clear interrupt status */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base + DPMAIF_PD_AP_UL_L2TISAR0,
				l2tisar0);
	}

	mtk_dpmaif_drv_clr_ip_busy_sts(drv_info);

	MTK_DBG_DATA_IRQ_SRC_INFO(drv_info->mdev, DPMAIF_IRQ_SRC2_UL_DONE, ori_l2tisar0,
				  l2tisar0, l2timr0);

	return 0;
}

static int mtk_dpmaif_drv_irq_src4(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info)
{
	intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_TRAS_SYNC;
	intr_info->intr_cnt++;

	return 0;
}

static u32 mtk_dpmaif_drv_irq_src6_filter(struct dpmaif_drv_info *drv_info,
					  u32 l2risar1, u32 l2rimr1)
{
	if (l2rimr1 & DPMAIF_DL_INT_DLQ2_QDONE_MSK)
		l2risar1 &= ~DPMAIF_DL2_INT_DLQ2_QDONE;

	if (l2rimr1 & DPMAIF_DL_INT_DLQ2_PITCNT_LEN_ERR_MSK)
		l2risar1 &= ~DPMAIF_DL2_INT_DLQ2_PITCNT_LEN_ERR;

	if (l2rimr1 & DPMAIF_DL2_INT_BATCNT_LEN_ERR_MSK)
		l2risar1 &= ~DPMAIF_DL2_INT_BATCNT_LEN_ERR;

	if (l2rimr1 & DPMAIF_DL2_INT_FRG_LEN_ERR_MSK)
		l2risar1 &= ~DPMAIF_DL2_INT_FRG_LEN_ERR;

	return l2risar1;
}

static int mtk_dpmaif_drv_irq_src3(struct dpmaif_drv_info *drv_info,
				   struct dpmaif_drv_intr_info *intr_info)
{
	u32 val, ori_l2risar1, l2risar1, l2rimr1;

	ori_l2risar1 = mtk_dpmaif_drv_get_dl_lv2_sts(drv_info, DPMAIF_DLQ2);
	l2rimr1 = mtk_dpmaif_drv_get_dl_intr_mask(drv_info, DPMAIF_DLQ2);

	/* filter care interrupt status. */
	l2risar1 = ori_l2risar1 & (DPMAIF_DL2_INT_DLQ2_QDONE | DPMAIF_DL2_INT_BATCNT_LEN_ERR |
			DPMAIF_DL2_INT_DLQ2_PITCNT_LEN_ERR | DPMAIF_DL2_INT_FRG_LEN_ERR);

	if (l2risar1) {
		/* Filter to get DL unmasked interrupts */
		l2risar1 = mtk_dpmaif_drv_irq_src6_filter(drv_info, l2risar1, l2rimr1);

		val = l2risar1 & DPMAIF_DL2_INT_BATCNT_LEN_ERR;
		if (val) {
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_BATCNT_LEN_ERR;
			intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_BAT1;
			intr_info->intr_cnt++;
			mtk_dpmaif_drv_mask_dl_batcnt_len_err_intr(drv_info, DPMAIF_BAT1);
		}

		val = l2risar1 & DPMAIF_DL2_INT_FRG_LEN_ERR;
		if (val) {
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_FRGCNT_LEN_ERR;
			intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_BAT1;
			intr_info->intr_cnt++;
			mtk_dpmaif_drv_mask_dl_frgcnt_len_err_intr(drv_info, DPMAIF_BAT1);
		}

		val = l2risar1 & DPMAIF_DL2_INT_DLQ2_PITCNT_LEN_ERR;
		if (val) {
			intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_PITCNT_LEN_ERR;
			intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_DLQ2;
			intr_info->intr_cnt++;
			mtk_dpmaif_drv_dlq_mask_pit_cnt_len_err_intr(drv_info, DPMAIF_DLQ2);
		}

		val = l2risar1 & DPMAIF_DL2_INT_DLQ2_QDONE;
		if (val) {
			mtk_pm_ds_try_lock(drv_info->mdev, MTK_USER_DATA);
			if (!mtk_dpmaif_drv_dlq_mask_rx_done_intr(drv_info, DPMAIF_DLQ2)) {
				intr_info->intr_types[intr_info->intr_cnt] = DPMAIF_INTR_DL_DONE;
				intr_info->intr_queues[intr_info->intr_cnt] = DPMAIF_DLQ2;
				intr_info->intr_cnt++;
			}
		}

		/* Clear interrupt status. */
		mtk_pci_write32(drv_info->mdev, drv_info->regs->pd_base +
				NRL2_DPMAIF_AP_MISC_APDL_L2TISAR1, l2risar1);
	}

	mtk_dpmaif_drv_clr_ip_busy_sts(drv_info);

	MTK_DBG_DATA_IRQ_SRC_INFO(drv_info->mdev, DPMAIF_IRQ_SRC3_DLQ2, ori_l2risar1,
				  l2risar1, l2rimr1);

	return 0;
}

static int mtk_dpmaif_drv_init_m9xx(struct dpmaif_drv_info *drv_info, void *data)
{
	int ret;

	mtk_dpmaif_drv_reset(drv_info);
	ret = mtk_dpmaif_drv_init_com(drv_info, data);

	return ret;
}

struct dpmaif_drv_ops dpmaif_drv_ops_m9xx = {
	.init = mtk_dpmaif_drv_init_m9xx,
	.start_queue = mtk_dpmaif_drv_start_queue,
	.stop_queue = mtk_dpmaif_drv_stop_queue,
	.intr_handle = mtk_dpmaif_drv_intr_handle_com,
	.intr_complete = mtk_dpmaif_drv_intr_complete_com,
	.send_doorbell = mtk_dpmaif_drv_send_doorbell_com,
	.get_ring_idx = mtk_dpmaif_drv_get_ring_idx,
	.feature_cmd = mtk_dpmaif_drv_feature_cmd_m9xx,
	.dump = mtk_dpmaif_drv_dump,
	.get_rx_info = mtk_dpmaif_get_rx_info,
	.fill_tx_info = mtk_dpmaif_fill_tx_info,
};

