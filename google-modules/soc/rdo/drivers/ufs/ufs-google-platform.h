/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google LLC
 */
#ifndef _UFS_GOOGLE_PLATFORM_H_
#define _UFS_GOOGLE_PLATFORM_H_

#include "ufs-google.h"

#if IS_ENABLED(CONFIG_SOC_LGA)
#include "lga/ufs-platform.h"
#endif

#if IS_ENABLED(CONFIG_SOC_MBU)
#include "mbu/ufs-platform.h"
#endif

/* UNIPRO DME attributes */
#define UNIPRO_DME_LAYER_EN 0xd000
#define UNIPRO_DME_MPHY_CFG_UPD 0xd085
#define UNIPRO_DME_MPHY_DISABLE 0xd0c1

/* UNIPRO (link-layer) attributes */
#define UNIPRO_L15_PA_T_ACTIVATE 0x15a8
#define UNIPRO_PA_TXHSG4SYNCLENGTH 0x15d0
#define UNIPRO_PA_TXHSG5SYNCLENGTH 0x15d6

/* MPHY attributes */
#define MPHY_G5_RX_ASYNC_FILTER_OFFSET 0x8013U
#define MPHY_G5_RX_CDR_ACTIVE_LATENCY_ADJUST_OFFSET 0x8019U

#define MPHY_G5_CBMPHYBOOTCFG_OFFSET 0x8101U
#define MPHY_G5_CBAPBPADDRLSB_OFFSET 0x810EU
#define MPHY_G5_CBAPBPADDRMSB_OFFSET 0x810FU
#define MPHY_G5_CBAPBPWDATALSB_OFFSET 0x8110U
#define MPHY_G5_CBAPBPWDATAMSB_OFFSET 0x8111U
#define MPHY_G5_CBAPBPRDATALSB_OFFSET 0x8112U
#define MPHY_G5_CBAPBPRDATAMSB_OFFSET 0x8113U
#define MPHY_G5_CBAPBPWRITESEL_OFFSET 0x8114U
#define MPHY_G5_CBATTR2APBCTRL_OFFSET 0x8115U
#define MPHY_G5_CBULPH8_OFFSET 0x8118U
#define MPHY_G5_TX_FSM_STATE_OFFSET 0x41U
#define MPHY_G5_RX_FSM_STATE_OFFSET 0xC1U
#define MPHY_G5_TX_EQ_MAIN_DE_0P0_LA 0x8014
#define MPHY_G5_TX_EQ_PRE_DE_0P0_LA 0x8016
#define TXLINERSTCFGLSB 0x800A

/* Ref: DWC_mipi_unipro_databook ver 1.60a, Table 2-1*/
#define MPHY_TX_LANE1 UIC_ARG_MPHY_TX_GEN_SEL_INDEX(0)
#define MPHY_TX_LANE2 UIC_ARG_MPHY_TX_GEN_SEL_INDEX(1)
#define MPHY_RX_LANE1 UIC_ARG_MPHY_RX_GEN_SEL_INDEX(0)
#define MPHY_RX_LANE2 UIC_ARG_MPHY_RX_GEN_SEL_INDEX(1)

#define MPHY_ENABLE_TIMEOUT_US 1000000
#define MPHY_ENABLE_DELAY_US 2000

/* Phy Calibration Offsets: Done Registers */
#define RAWCMN_DIG_AON_CMNCAL_MPLL_STATUS 0x017c
#define RAWLANEANONN_DIG_RX_AFE_DFE_CAL_DONE_LANE0 0x3128
#define RAWLANEANONN_DIG_RX_AFE_DFE_CAL_DONE_LANE1 0x3328
#define RAWLANEANONN_DIG_RX_STARTUP_CAL_ALGO_CTL_1_LANE0 0x3101
#define RAWLANEANONN_DIG_RX_STARTUP_CAL_ALGO_CTL_1_LANE1 0x3301
#define RAWLANEANONN_DIG_RX_CONT_ALGO_CTL_LANE0 0x3103
#define RAWLANEANONN_DIG_RX_CONT_ALGO_CTL_LANE1 0x3303

/* Assymetric power mode change software workaround registers */
#define LANE1_DIG_RX_CDR_CDR_CTL_6 0x10FC
#define LANE2_DIG_RX_CDR_CDR_CTL_6 0x12FC
#define SUP_DIG_ANA_XF_BG_OVRD_OUT 0xC4

/**
 * [Mandatory] Set google platform ops. Need to be implemented in <platform>/ufs-platform.c
 */
int ufs_google_plat_set_gops(struct ufs_google_host *host);

static inline int ufs_plat_get_phy_fw_patch(struct ufs_google_host *host,
					    const u32 **data, size_t *sz)
{
	if (host->gops->get_phy_fw_patch)
		return host->gops->get_phy_fw_patch(host, data, sz);
	return -EINVAL;
}

static inline int ufs_plat_get_phy_rmmi_attrs(struct ufs_google_host *host,
					      const struct ufs_dme_attr **attrs,
					      size_t *len)
{
	if (host->gops->get_phy_rmmi_attrs)
		return host->gops->get_phy_rmmi_attrs(host, attrs, len);
	return -EINVAL;
}

static inline int
ufs_plat_get_phy_rmmi_tx_eq_attrs(struct ufs_google_host *host,
				  const struct ufs_dme_attr **attrs,
				  size_t *len)
{
	if (host->gops->get_phy_rmmi_tx_eq_attrs)
		return host->gops->get_phy_rmmi_tx_eq_attrs(host, attrs, len);
	return -EINVAL;
}

static inline int ufs_plat_poll_phy_ready(struct ufs_google_host *host)
{
	if (host->gops->poll_phy_ready)
		return host->gops->poll_phy_ready(host);
	return -EINVAL;
}

/**
 * ufs_plat_set_refclk_control - Enable or disable UFS QCH voting
 * @host: google host instance
 * @on: true to enable, false to disable
 */
static inline void ufs_plat_set_refclk_control(struct ufs_google_host *host,
					       bool on)
{
	if (host->gops->set_refclk_control)
		host->gops->set_refclk_control(host, on);
}

static inline int ufs_plat_config_cpm(struct ufs_google_host *host)
{
	if (host->gops->config_cpm)
		return host->gops->config_cpm(host);
	return -EINVAL;
}

static inline void ufs_plat_config_vs(struct ufs_google_host *host)
{
	if (host->gops->config_vs)
		host->gops->config_vs(host);
}

#endif /* _UFS_GOOGLE_PLATFORM_H_ */
