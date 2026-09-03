/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * DOC: wlan_cfg80211_wifi_pos.c
 * defines wifi-pos module related driver functions interfacing with linux
 * kernel
 */
#include "wlan_cfg80211.h"
#include "wlan_objmgr_psoc_obj.h"
#include "wlan_cfg80211_wifi_pos.h"
#include "wlan_cmn_ieee80211.h"
#include "wlan_hdd_main.h"
#include "osif_vdev_sync.h"
#include "wifi_pos_ucfg_i.h"
#include "wifi_pos_utils_i.h"
#include "wifi_pos_api.h"
#include "wmi_unified_param.h"
#include "wlan_objmgr_vdev_obj.h"
#include "wma.h"
#include "wlan_p2p_api.h"
#include "wlan_policy_mgr_api.h"
#include "wlan_mlme_ucfg_api.h"

#if defined(WIFI_POS_CONVERGED) && defined(WLAN_FEATURE_RTT_11AZ_SUPPORT)

u8 wlan_extended_caps_iface[WLAN_EXTCAP_IE_MAX_LEN] = {0};
u8 wlan_extended_caps_iface_mask[WLAN_EXTCAP_IE_MAX_LEN] = {0};

struct wiphy_iftype_ext_capab iftype_ext_cap;

#if !defined(CNSS_GENL) && \
	(defined(CFG80211_SUPPORT_AUTH_DEAUTH_TA_RANDOMIZATION) || \
	 (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)))
/**
 * wlan_wifi_pos_cfg80211_set_auth_deauth_random_ta_flag() - API to set
 * NL80211_EXT_FEATURE_AUTH_AND_DEAUTH_RANDOM_TA flag
 * @wiphy: Pointer to wiphy
 * @psoc: Pointer to psoc
 *
 * Allow random TA to be used with authentication and deauthentication frames
 * when MAC secured or MAC_PHY secured ranging is supported.
 *
 * Return: None
 */
static void
wlan_wifi_pos_cfg80211_set_auth_deauth_random_ta_flag(
		struct wiphy *wiphy,
		struct wlan_objmgr_psoc *psoc)
{
	if (wlan_psoc_nif_fw_ext2_cap_get(psoc, WLAN_RTT_11AZ_MAC_SEC_SUPPORT) ||
	    wlan_psoc_nif_fw_ext2_cap_get(psoc, WLAN_RTT_11AZ_MAC_PHY_SEC_SUPPORT))
		wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_AUTH_AND_DEAUTH_RANDOM_TA);
}
#else
static void
wlan_wifi_pos_cfg80211_set_auth_deauth_random_ta_flag(
		struct wiphy *wiphy,
		struct wlan_objmgr_psoc *psoc)
{
}
#endif

#ifdef CNSS_GENL
/**
 * wlan_wifi_pos_get_rsta_11az_ranging_cap() - API to read user configured RSTA
 * 11az ranging capability.
 * @psoc: Pointer to PSOC object
 *
 * Return: user configured RSTA 11az ranging capability.
 */
static uint32_t wlan_wifi_pos_get_rsta_11az_ranging_cap(
		struct wlan_objmgr_psoc *psoc)
{
	return ucfg_wifi_pos_get_rsta_11az_ranging_cap();
}
#else
static uint32_t wlan_wifi_pos_get_rsta_11az_ranging_cap(
		struct wlan_objmgr_psoc *psoc)
{
	struct wifi_pos_legacy_ops *legacy_cb;

	legacy_cb = wifi_pos_get_legacy_ops();
	if (!legacy_cb || !legacy_cb->get_rsta_11az_ranging_cap) {
		wifi_pos_err("legacy callback is not registered");
		return 0;
	}

	return legacy_cb->get_rsta_11az_ranging_cap(psoc);
}
#endif

#if defined(CFG80211_PD_SUPPORT) && defined(WLAN_FEATURE_RTT_11AZ_SUPPORT)
/*
 * wifi_pos_pmsr_capa_cfg - Stable-lifetime storage for wiphy->pmsr_capa.
 *
 * This struct has module (process) lifetime and is safe to assign directly
 * to wiphy->pmsr_capa.  It is populated once from firmware-advertised
 * RTT/FTM capabilities (via wlan_wifi_pos_cfg80211_set_wiphy_pmsr_capa) and
 * is never heap-allocated or freed.
 *
 * This mirrors the pattern used for vendor commands:
 *   static const struct wiphy_vendor_command hdd_wiphy_vendor_commands[];
 *   wiphy->vendor_commands = hdd_wiphy_vendor_commands;
 *
 * Lifetime: module load → module unload (outlives any wiphy or psoc).
 * On deinit, wlan_wifi_pos_cfg80211_free_wiphy_pmsr_capa() simply clears
 * wiphy->pmsr_capa to NULL; no free is performed.
 */
static struct cfg80211_pmsr_capabilities wifi_pos_pmsr_capa_cfg;

/**
 * wlan_wifi_pos_fw_bw_bitmap_to_nl() - Convert FW BW bitmap to nl80211 bitmap
 * @fw_bw_bitmap: Firmware bandwidth bitmap
 *
 * Return: nl80211 bandwidth bitmap
 */
static u32 wlan_wifi_pos_fw_bw_bitmap_to_nl(u32 fw_bw_bitmap)
{
	u32 nl_bw_bitmap = 0;

	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_20)) {
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_20_NOHT);
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_20);
	}
	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_40))
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_40);
	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_80))
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_80);
	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_160))
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_160);
	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_320))
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_320);
	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_80P80))
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_80P80);
	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_5))
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_5);
	if (fw_bw_bitmap & BIT(WMI_HOST_CHAN_WIDTH_10))
		nl_bw_bitmap |= BIT(NL80211_CHAN_WIDTH_10);

	return nl_bw_bitmap;
}

/**
 * wlan_wifi_pos_fw_preamble_bitmap_to_nl() - Convert FW preamble bitmap to nl
 * @fw_preamble_bitmap: Firmware preamble bitmap
 *
 * Return: nl80211 preamble bitmap
 */
static u32 wlan_wifi_pos_fw_preamble_bitmap_to_nl(u32 fw_preamble_bitmap)
{
	u32 nl_preamble_bitmap = 0;

	if (fw_preamble_bitmap & (BIT(WMI_HOST_RATE_PREAMBLE_OFDM) |
				  BIT(WMI_HOST_RATE_PREAMBLE_CCK)))
		nl_preamble_bitmap |= BIT(NL80211_PREAMBLE_LEGACY);
	if (fw_preamble_bitmap & BIT(WMI_HOST_RATE_PREAMBLE_HT))
		nl_preamble_bitmap |= BIT(NL80211_PREAMBLE_HT);
	if (fw_preamble_bitmap & BIT(WMI_HOST_RATE_PREAMBLE_VHT))
		nl_preamble_bitmap |= BIT(NL80211_PREAMBLE_VHT);
	if (fw_preamble_bitmap & BIT(WMI_HOST_RATE_PREAMBLE_HE))
		nl_preamble_bitmap |= BIT(NL80211_PREAMBLE_HE);

	return nl_preamble_bitmap;
}

/* Macros for fw->support_flag */
#define WLAN_SUPPORT_FLAG_REPORT_AP_TSF             BIT(0)
#define WLAN_SUPPORT_FLAG_RANDOMIZE_MAC_ADDR        BIT(1)
#define WLAN_SUPPORT_FLAG_PD_SUPPORT                BIT(2)
#define WLAN_SUPPORT_FLAG_PD_CONCURRENT_ISTA_RSTA   BIT(3)

/* Macros for fw->pd_max_peers */
#define WLAN_PD_MAX_PEERS_ISTA_ROLE_MASK            0xFFFF
#define WLAN_PD_MAX_PEERS_RSTA_ROLE_SHIFT           16
#define WLAN_PD_MAX_PEERS_RSTA_ROLE_MASK            0xFFFF

/* Macros for fw->ftm_support_flag */
#define WLAN_FTM_SUPPORT_FLAG_SUPPORTED             BIT(0)
#define WLAN_FTM_SUPPORT_FLAG_ASAP                  BIT(1)
#define WLAN_FTM_SUPPORT_FLAG_NON_ASAP              BIT(2)
#define WLAN_FTM_SUPPORT_FLAG_REQUEST_LCI           BIT(3)
#define WLAN_FTM_SUPPORT_FLAG_REQUEST_CIVICLOC      BIT(4)
#define WLAN_FTM_SUPPORT_FLAG_TRIGGER_BASED         BIT(5)
#define WLAN_FTM_SUPPORT_FLAG_NON_TRIGGER_BASED     BIT(6)
#define WLAN_FTM_SUPPORT_FLAG_SUPPORT_6GHZ          BIT(7)
#define WLAN_FTM_SUPPORT_FLAG_RSTA_NTB              BIT(8)
#define WLAN_FTM_SUPPORT_FLAG_RSTA_EDCA             BIT(9)

/* Macros for fw->ranging_11az_parameters */
#define WLAN_11AZ_PARAMS_MAX_TX_LTF_REP_MASK        0x7
#define WLAN_11AZ_PARAMS_MAX_RX_LTF_REP_SHIFT       3
#define WLAN_11AZ_PARAMS_MAX_RX_LTF_REP_MASK        0x7
#define WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_RX_SHIFT     6
#define WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_RX_MASK      0x3
#define WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_TX_SHIFT     8
#define WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_TX_MASK      0x3
#define WLAN_11AZ_PARAMS_RX_STS_LE80_SHIFT          10
#define WLAN_11AZ_PARAMS_RX_STS_LE80_MASK           0x7
#define WLAN_11AZ_PARAMS_RX_STS_GT80_SHIFT          13
#define WLAN_11AZ_PARAMS_RX_STS_GT80_MASK           0x7
#define WLAN_11AZ_PARAMS_TX_STS_LE80_SHIFT          16
#define WLAN_11AZ_PARAMS_TX_STS_LE80_MASK           0x7
#define WLAN_11AZ_PARAMS_TX_STS_GT80_SHIFT          19
#define WLAN_11AZ_PARAMS_TX_STS_GT80_MASK           0x7

/* Macros for fw->capabilities */
#define WLAN_CAPABILITIES_BURST_EXP_MASK            0xFF
#define WLAN_CAPABILITIES_BURST_EXP_UNLIMITED       0xFF
#define WLAN_CAPABILITIES_MAX_FTMS_PER_BURST_SHIFT  8
#define WLAN_CAPABILITIES_MAX_FTMS_PER_BURST_MASK   0xFF
#define WLAN_CAPABILITIES_MAX_TX_ANTENNAS_SHIFT     16
#define WLAN_CAPABILITIES_MAX_TX_ANTENNAS_MASK      0xFF
#define WLAN_CAPABILITIES_MAX_RX_ANTENNAS_SHIFT     24
#define WLAN_CAPABILITIES_MAX_RX_ANTENNAS_MASK      0xFF

/**
 * wlan_wifi_pos_cfg80211_set_wiphy_pmsr_capa() - Populate wiphy->pmsr_capa
 *	from firmware-advertised RTT/FTM peer measurement capabilities.
 * @wiphy: Pointer to wiphy
 * @psoc: Pointer to psoc
 *
 * Fills the module-lifetime static struct wifi_pos_pmsr_capa_cfg from the
 * wifi_pos_pmsr_fw_caps stored in the psoc private object, then assigns
 * wiphy->pmsr_capa to point to it.  No heap allocation is performed.
 * If firmware has not yet advertised capabilities (valid == false) the
 * function is a no-op.
 *
 * Lifetime: wifi_pos_pmsr_capa_cfg has module (process) lifetime and
 * outlives any wiphy or psoc.  This mirrors the vendor command pattern:
 *   wiphy->vendor_commands = hdd_wiphy_vendor_commands  (static array).
 *
 * Caller: wlan_wifi_pos_cfg80211_set_wiphy_ext_feature() (post-FW-ready).
 * Cleanup: wlan_wifi_pos_cfg80211_free_wiphy_pmsr_capa() clears the pointer.
 */
static void
wlan_wifi_pos_cfg80211_set_wiphy_pmsr_capa(struct wiphy *wiphy,
					   struct wlan_objmgr_psoc *psoc)
{
	struct wifi_pos_pmsr_fw_caps *fw;
	struct cfg80211_pmsr_capabilities *pmsr;
	u8 burst_exp_raw;
	QDF_STATUS status;

	status = wifi_pos_get_pmsr_fw_caps(psoc, &fw);
	if (QDF_IS_STATUS_ERROR(status)) {
		wifi_pos_err("Failed to get PMSR FW caps");
		return;
	}

	if (wlan_psoc_nif_fw_ext2_cap_get(psoc,
					  WLAN_RTT_11AZ_MAC_PHY_SEC_SUPPORT)) {
		wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SECURE_RTT);
		wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SECURE_LTF);
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_SET_KEY_LTF_SEED);
	}

	if (wlan_psoc_nif_fw_ext2_cap_get(psoc, WLAN_RTT_11AZ_MAC_SEC_SUPPORT))
		wiphy_ext_feature_set(wiphy,
				      NL80211_EXT_FEATURE_PROT_RANGE_NEGO_AND_MEASURE);

	if (!fw->valid) {
		wifi_pos_debug("FW RTT peer meas caps not yet available");
		return;
	}

	/*
	 * wiphy->pmsr_capa will point to wifi_pos_pmsr_capa_cfg
	 * which has wlan module lifetime and outlives wiphy or psoc.
	 */
	pmsr = &wifi_pos_pmsr_capa_cfg;
	qdf_mem_zero(pmsr, sizeof(*pmsr));

	/* --- Top-level fields --- */
	pmsr->max_peers  = fw->max_peers;
	pmsr->report_ap_tsf =
		!!(fw->support_flag & WLAN_SUPPORT_FLAG_REPORT_AP_TSF);
	pmsr->randomize_mac_addr =
		!!(fw->support_flag & WLAN_SUPPORT_FLAG_RANDOMIZE_MAC_ADDR);
	pmsr->ftm.type.pd_support =
		!!(fw->support_flag & WLAN_SUPPORT_FLAG_PD_SUPPORT);
	pmsr->ftm.type.infra_support = 1;
	pmsr->ftm.concurrent_ista_rsta_support =
		!!(fw->support_flag & WLAN_SUPPORT_FLAG_PD_CONCURRENT_ISTA_RSTA);

	/* --- FTM support flags --- */
	pmsr->ftm.supported =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_SUPPORTED);
	pmsr->ftm.asap =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_ASAP);
	pmsr->ftm.non_asap =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_NON_ASAP);
	pmsr->ftm.request_lci =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_REQUEST_LCI);
	pmsr->ftm.request_civicloc =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_REQUEST_CIVICLOC);
	pmsr->ftm.trigger_based =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_TRIGGER_BASED);
	pmsr->ftm.non_trigger_based =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_NON_TRIGGER_BASED);
	pmsr->ftm.support_6ghz =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_SUPPORT_6GHZ);

	pmsr->ftm.max_tx_ltf_rep =
		fw->ranging_11az_parameters & WLAN_11AZ_PARAMS_MAX_TX_LTF_REP_MASK;
	pmsr->ftm.max_rx_ltf_rep =
		(fw->ranging_11az_parameters >> WLAN_11AZ_PARAMS_MAX_RX_LTF_REP_SHIFT) & WLAN_11AZ_PARAMS_MAX_RX_LTF_REP_MASK;
	pmsr->ftm.max_total_ltf_rx =
		(fw->ranging_11az_parameters >> WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_RX_SHIFT) & WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_RX_MASK;
	pmsr->ftm.max_total_ltf_tx =
		(fw->ranging_11az_parameters >> WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_TX_SHIFT) & WLAN_11AZ_PARAMS_MAX_TOTAL_LTF_TX_MASK;

	{
		u8 rx_sts_le80 = (fw->ranging_11az_parameters >> WLAN_11AZ_PARAMS_RX_STS_LE80_SHIFT) & WLAN_11AZ_PARAMS_RX_STS_LE80_MASK;
		u8 rx_sts_gt80 = (fw->ranging_11az_parameters >> WLAN_11AZ_PARAMS_RX_STS_GT80_SHIFT) & WLAN_11AZ_PARAMS_RX_STS_GT80_MASK;
		u8 tx_sts_le80 = (fw->ranging_11az_parameters >> WLAN_11AZ_PARAMS_TX_STS_LE80_SHIFT) & WLAN_11AZ_PARAMS_TX_STS_LE80_MASK;
		u8 tx_sts_gt80 = (fw->ranging_11az_parameters >> WLAN_11AZ_PARAMS_TX_STS_GT80_SHIFT) & WLAN_11AZ_PARAMS_TX_STS_GT80_MASK;

		pmsr->ftm.max_rx_sts = max(rx_sts_le80, rx_sts_gt80);
		pmsr->ftm.max_tx_sts = max(tx_sts_le80, tx_sts_gt80);
	}

	/* Read FTM supported capability to advertise EDCA */
	pmsr->ftm.ista.support_edca =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_SUPPORTED);
	pmsr->ftm.ista.support_tb =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_TRIGGER_BASED);
	pmsr->ftm.ista.support_ntb =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_NON_TRIGGER_BASED);
	pmsr->ftm.ista.max_peers =
		fw->pd_max_peers & WLAN_PD_MAX_PEERS_ISTA_ROLE_MASK;

	pmsr->ftm.rsta.support_ntb =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_RSTA_NTB);
	pmsr->ftm.rsta.support_edca =
		!!(fw->ftm_support_flag & WLAN_FTM_SUPPORT_FLAG_RSTA_EDCA);
	pmsr->ftm.rsta.support_tb   =
		(wlan_psoc_nif_fw_ext2_cap_get(psoc,
					       WLAN_RTT_11AZ_TB_SUPPORT) ||
		 wlan_psoc_nif_fw_ext2_cap_get(psoc,
					       WLAN_RTT_11AZ_TB_RSTA_SUPPORT));

	pmsr->ftm.rsta.max_peers =
		(fw->pd_max_peers >> WLAN_PD_MAX_PEERS_RSTA_ROLE_SHIFT) & WLAN_PD_MAX_PEERS_RSTA_ROLE_MASK;

	/* --- BW / Preamble bitmaps --- */
	pmsr->ftm.bandwidths =
		wlan_wifi_pos_fw_bw_bitmap_to_nl(fw->supported_bw_bitmap);
	pmsr->ftm.preambles  =
		wlan_wifi_pos_fw_preamble_bitmap_to_nl(fw->supported_preamble_bitmap);

	pmsr->ftm.pd_bandwidths  = pmsr->ftm.bandwidths;
	pmsr->ftm.pd_preambles   = pmsr->ftm.preambles;

	/*
	 * capabilities field layout:
	 *   Bits  7-0  : max burst exponent (s8; 0xFF sentinel => -1 unlimited)
	 *   Bits 15-8  : max FTMs per burst (u8; 0 = unlimited)
	 *   Bits 23-16 : max Tx antennas
	 *   Bits 31-24 : max Rx antennas
	 */
	burst_exp_raw =
		(u8)(fw->capabilities & WLAN_CAPABILITIES_BURST_EXP_MASK);
	/* Map 0xFF sentinel to -1 (unlimited) per cfg80211 convention */
	pmsr->ftm.max_bursts_exponent =
		(burst_exp_raw == WLAN_CAPABILITIES_BURST_EXP_UNLIMITED) ?
					-1 : (s8)burst_exp_raw;
	pmsr->ftm.max_ftms_per_burst =
		(u8)((fw->capabilities >> WLAN_CAPABILITIES_MAX_FTMS_PER_BURST_SHIFT) & WLAN_CAPABILITIES_MAX_FTMS_PER_BURST_MASK);

	pmsr->ftm.max_no_of_tx_antennas =
		(fw->capabilities >> WLAN_CAPABILITIES_MAX_TX_ANTENNAS_SHIFT) & WLAN_CAPABILITIES_MAX_TX_ANTENNAS_MASK;
	pmsr->ftm.max_no_of_rx_antennas =
		(fw->capabilities >> WLAN_CAPABILITIES_MAX_RX_ANTENNAS_SHIFT) & WLAN_CAPABILITIES_MAX_RX_ANTENNAS_MASK;

	pmsr->ftm.min_allowed_ranging_interval_edca = fw->min_interval_edca_ms;
	pmsr->ftm.min_allowed_ranging_interval_ntb = fw->min_interval_ntb_ms;

	wifi_pos_debug("pmsr: max_peers=%u ap_tsf=%u rand_mac=%u",
		       pmsr->max_peers, pmsr->report_ap_tsf,
		       pmsr->randomize_mac_addr);
	wifi_pos_debug("pmsr: pd_supp=%u infra_supp=%u pd_concur=%u",
		       pmsr->ftm.type.pd_support,
		       pmsr->ftm.type.infra_support,
		       pmsr->ftm.concurrent_ista_rsta_support);
	wifi_pos_debug("pmsr: pd_max_ista=%u pd_max_rsta=%u",
		       pmsr->ftm.ista.max_peers,
		       pmsr->ftm.rsta.max_peers);
	wifi_pos_debug("ftm: supp=%u asap=%u non_asap=%u",
		       pmsr->ftm.supported, pmsr->ftm.asap,
		       pmsr->ftm.non_asap);
	wifi_pos_debug("ftm: req_lci=%u req_civicloc=%u",
		       pmsr->ftm.request_lci, pmsr->ftm.request_civicloc);
	wifi_pos_debug("ftm: tb=%u ntb=%u supp_6g=%u",
		       pmsr->ftm.trigger_based, pmsr->ftm.non_trigger_based,
		       pmsr->ftm.support_6ghz);

	wifi_pos_debug("ftm: tx_ltf_rep=%u rx_ltf_rep=%u",
		       pmsr->ftm.max_tx_ltf_rep, pmsr->ftm.max_rx_ltf_rep);
	wifi_pos_debug("ftm: max_ltf_rx=%u max_ltf_tx=%u",
		       pmsr->ftm.max_total_ltf_rx,
		       pmsr->ftm.max_total_ltf_tx);
	wifi_pos_debug("ftm: max_rx_sts=%u max_tx_sts=%u",
		       pmsr->ftm.max_rx_sts, pmsr->ftm.max_tx_sts);

	wifi_pos_debug("ftm ista: edca=%u tb=%u ntb=%u",
		       pmsr->ftm.ista.support_edca, pmsr->ftm.ista.support_tb,
		       pmsr->ftm.ista.support_ntb);
	wifi_pos_debug("ftm rsta: edca=%u tb=%u ntb=%u",
		       pmsr->ftm.rsta.support_edca, pmsr->ftm.rsta.support_tb,
		       pmsr->ftm.rsta.support_ntb);

	wifi_pos_debug("ftm: bw=0x%x preambles=0x%x",
		       pmsr->ftm.bandwidths, pmsr->ftm.preambles);
	wifi_pos_debug("pd ntb/edca: bw=0x%x preambles=0x%x",
		       pmsr->ftm.pd_bandwidths, pmsr->ftm.pd_preambles);

	wifi_pos_debug("ftm: burst_exp=%d max_ftms_burst=%u",
		       pmsr->ftm.max_bursts_exponent,
		       pmsr->ftm.max_ftms_per_burst);
	wifi_pos_debug("ftm: min_int_edca=%u min_int_ntb=%u",
		       pmsr->ftm.min_allowed_ranging_interval_edca,
		       pmsr->ftm.min_allowed_ranging_interval_ntb);
	wifi_pos_debug("ftm: max_tx_ant=%u max_rx_ant=%u",
		       pmsr->ftm.max_no_of_tx_antennas,
		       pmsr->ftm.max_no_of_rx_antennas);

	/*
	 * Assign wiphy->pmsr_capa to the module-lifetime static struct.
	 * Lifetime guarantee: wifi_pos_pmsr_capa_cfg outlives any wiphy or
	 * psoc — no paired free is needed.
	 */
	wiphy->pmsr_capa = &wifi_pos_pmsr_capa_cfg;
}
#else
static inline void
wlan_wifi_pos_cfg80211_set_wiphy_pmsr_capa(struct wiphy *wiphy,
					   struct wlan_objmgr_psoc *psoc)
{}
#endif /* CFG80211_PD_SUPPORT && WLAN_FEATURE_RTT_11AZ_SUPPORT */

#define WLAN_EXT_RANGING_CAP_IDX  11
void
wlan_wifi_pos_cfg80211_set_wiphy_ext_feature(struct wiphy *wiphy,
					     struct wlan_objmgr_psoc *psoc)
{
	uint32_t enable_rsta_11az_ranging;

	enable_rsta_11az_ranging =
		wlan_wifi_pos_get_rsta_11az_ranging_cap(psoc);
	if (!enable_rsta_11az_ranging)
		return;

	if ((enable_rsta_11az_ranging & CFG_RESPONDER_11AZ_NTB_SUPPORT) &&
	    (wlan_psoc_nif_fw_ext_cap_get(psoc, WLAN_RTT_11AZ_NTB_SUPPORT))) {
		wlan_extended_caps_iface[WLAN_EXT_RANGING_CAP_IDX] |=
					WLAN_EXT_CAPA11_NTB_RANGING_RESPONDER;
		wlan_extended_caps_iface_mask[WLAN_EXT_RANGING_CAP_IDX] |=
					WLAN_EXT_CAPA11_NTB_RANGING_RESPONDER;
	}

	if ((enable_rsta_11az_ranging & CFG_RESPONDER_11AZ_TB_SUPPORT) &&
	    (wlan_psoc_nif_fw_ext2_cap_get(psoc, WLAN_RTT_11AZ_TB_SUPPORT) ||
	    wlan_psoc_nif_fw_ext2_cap_get(psoc,
					  WLAN_RTT_11AZ_TB_RSTA_SUPPORT))) {
		wlan_extended_caps_iface[WLAN_EXT_RANGING_CAP_IDX] |=
					WLAN_EXT_CAPA11_TB_RANGING_RESPONDER;
		wlan_extended_caps_iface_mask[WLAN_EXT_RANGING_CAP_IDX] |=
					WLAN_EXT_CAPA11_TB_RANGING_RESPONDER;
	}

	wlan_wifi_pos_cfg80211_set_auth_deauth_random_ta_flag(wiphy, psoc);

	iftype_ext_cap.iftype = NL80211_IFTYPE_AP;
	iftype_ext_cap.extended_capabilities =
				wlan_extended_caps_iface,
	iftype_ext_cap.extended_capabilities_mask =
				wlan_extended_caps_iface_mask,
	iftype_ext_cap.extended_capabilities_len =
				ARRAY_SIZE(wlan_extended_caps_iface),

	wiphy->num_iftype_ext_capab = 0;
	wiphy->iftype_ext_capab = &iftype_ext_cap;
	wiphy->num_iftype_ext_capab++;

	/* Populate wiphy->pmsr_capa from firmware-advertised RTT/FTM caps */
	wlan_wifi_pos_cfg80211_set_wiphy_pmsr_capa(wiphy, psoc);
}

#define NUM_BITS_IN_BYTE       8
static void
wlan_wifi_pos_set_feature_flags(uint8_t *feature_flags,
				enum qca_wlan_vendor_features feature)
{
	uint32_t index;
	uint8_t bit_mask;

	index = feature / NUM_BITS_IN_BYTE;
	bit_mask = 1 << (feature % NUM_BITS_IN_BYTE);
	feature_flags[index] |= bit_mask;
}

#ifdef CNSS_GENL
/**
 * wlan_wifi_pos_get_rsta_11az_ranging_and_sec_ltf_support() - API to read
 * user configured RSTA 11az ranging and secure LTF support.
 * @psoc: Pointer to PSOC object
 * @enable_rsta_11az_ranging: Pointer to save RSTA 11az ranging support value
 * @rsta_secure_ltf_support: Pointer to save RSTA 11az secure ltf support
 *
 * Return: None
 */
static void wlan_wifi_pos_get_rsta_11az_ranging_and_sec_ltf_support(
		struct wlan_objmgr_psoc *psoc,
		bool *enable_rsta_11az_ranging,
		bool *rsta_secure_ltf_support)
{
	*enable_rsta_11az_ranging = ucfg_wifi_pos_get_rsta_11az_ranging_cap();
	*rsta_secure_ltf_support = *enable_rsta_11az_ranging &&
				   wifi_pos_get_rsta_sec_ltf_cap();
}
#else
static void wlan_wifi_pos_get_rsta_11az_ranging_and_sec_ltf_support(
		struct wlan_objmgr_psoc *psoc,
		bool *enable_rsta_11az_ranging,
		bool *rsta_secure_ltf_support)
{
	struct wifi_pos_legacy_ops *legacy_cb;

	legacy_cb = wifi_pos_get_legacy_ops();
	if (!legacy_cb || !legacy_cb->get_rsta_11az_ranging_cap ||
	    !legacy_cb->get_rsta_sec_ltf_cap) {
		wifi_pos_err("legacy callback is not registered");
		return;
	}

	*enable_rsta_11az_ranging = legacy_cb->get_rsta_11az_ranging_cap(psoc);
	*rsta_secure_ltf_support = *enable_rsta_11az_ranging &&
				   legacy_cb->get_rsta_sec_ltf_cap(psoc);
}
#endif

void wlan_wifi_pos_cfg80211_set_features(struct wlan_objmgr_psoc *psoc,
					 uint8_t *feature_flags)
{
	bool rsta_secure_ltf_support = false, enable_rsta_11az_ranging = false;

	wlan_wifi_pos_get_rsta_11az_ranging_and_sec_ltf_support(
			psoc, &enable_rsta_11az_ranging,
			&rsta_secure_ltf_support);

	if (wlan_psoc_nif_fw_ext2_cap_get(psoc,
					  WLAN_RTT_11AZ_MAC_PHY_SEC_SUPPORT)) {
		wlan_wifi_pos_set_feature_flags(feature_flags,
						QCA_WLAN_VENDOR_FEATURE_SECURE_LTF_STA);
		if (rsta_secure_ltf_support)
			wlan_wifi_pos_set_feature_flags(feature_flags,
							QCA_WLAN_VENDOR_FEATURE_SECURE_LTF_AP);
	}

	if (wlan_psoc_nif_fw_ext2_cap_get(psoc,
					  WLAN_RTT_11AZ_MAC_SEC_SUPPORT)) {
		wlan_wifi_pos_set_feature_flags(feature_flags,
			QCA_WLAN_VENDOR_FEATURE_PROT_RANGE_NEGO_AND_MEASURE_STA);
		if (rsta_secure_ltf_support)
			wlan_wifi_pos_set_feature_flags(feature_flags,
							QCA_WLAN_VENDOR_FEATURE_PROT_RANGE_NEGO_AND_MEASURE_AP);
	}
}
#endif

#if defined(WLAN_FEATURE_RTT_11AZ_SUPPORT) && defined(CFG80211_PD_SUPPORT)
static enum mlme_dot11_mode
hdd_pmsr_preamble_to_dot11_mode(enum nl80211_preamble preamble)
{
	switch (preamble) {
	case NL80211_PREAMBLE_HT:
		return MLME_DOT11_MODE_11N;
	case NL80211_PREAMBLE_VHT:
		return MLME_DOT11_MODE_11AC;
	case NL80211_PREAMBLE_HE:
		return MLME_DOT11_MODE_11AX;
	default:
		return MLME_DOT11_MODE_11N;
	}
}

/**
 * hdd_pmsr_preamble_to_wmi() - Map NL80211 preamble to WMI preamble
 * @preamble: NL80211 preamble value
 *
 * Return: WMI_HOST_RATE_PREAMBLE value
 */
static u32 hdd_pmsr_preamble_to_wmi(enum nl80211_preamble preamble)
{
	switch (preamble) {
	case NL80211_PREAMBLE_HT:
		return WMI_HOST_RATE_PREAMBLE_HT;
	case NL80211_PREAMBLE_VHT:
		return WMI_HOST_RATE_PREAMBLE_VHT;
	case NL80211_PREAMBLE_HE:
		return WMI_HOST_RATE_PREAMBLE_HE;
	default:
		return WMI_HOST_RATE_PREAMBLE_OFDM;
	}
}

/**
 * __wlan_hdd_cfg80211_start_pmsr() - Start peer measurement request (inner)
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 *
 * Builds and sends WMI_RTT_PEER_MEAS_REQ_CMDID to firmware.
 *
 * Return: 0 on success, negative errno on failure
 */
static int __wlan_hdd_cfg80211_start_pmsr(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  struct cfg80211_pmsr_request *req)
{
	struct net_device *dev = wdev->netdev;
	struct hdd_adapter *adapter = WLAN_HDD_GET_PRIV_PTR(dev);
	struct hdd_context *hdd_ctx = WLAN_HDD_GET_CTX(adapter);
	struct hdd_adapter *pd_adapter = NULL;
	struct wmi_rtt_peer_meas_req_cmd_params *params;
	u32 n_peers = req->n_peers;
	u32 i;
	int ret;
	QDF_STATUS status;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	if (policy_mgr_get_connection_count_with_mlo(hdd_ctx->psoc) > 1) {
		wifi_pos_err("PMSR not allowed when concurrency exists");
		return -EAGAIN;
	}

	if (adapter->deflink) {
		struct wlan_objmgr_vdev *vdev;

		vdev = wlan_objmgr_get_vdev_by_id_from_psoc(
				hdd_ctx->psoc, adapter->deflink->vdev_id,
				WLAN_WIFI_POS_OSIF_ID);
		if (vdev) {
			if (ucfg_mlme_is_chan_switch_in_progress(vdev)) {
				wifi_pos_err("channel switch is in progress");
				wlan_objmgr_vdev_release_ref(
					vdev, WLAN_WIFI_POS_OSIF_ID);
				return -EAGAIN;
			}
			wlan_objmgr_vdev_release_ref(vdev,
						     WLAN_WIFI_POS_OSIF_ID);
		}
	}

	if (!n_peers)
		return -EINVAL;

	wlan_p2p_del_random_mac(hdd_ctx->psoc, adapter->deflink->vdev_id, 0);

	pd_adapter = hdd_get_adapter(hdd_ctx, QDF_PD_MODE);
	if (!pd_adapter)
		return -EINVAL;

	pd_adapter->pmsr_req.cookie = req->cookie;
	pd_adapter->pmsr_req.is_valid = true;
	pd_adapter->pmsr_req.vdev_id = adapter->deflink->vdev_id;
	pd_adapter->pmsr_req.nl_port_id = req->nl_portid;
	pd_adapter->pmsr_req.req_id = (u32)(req->cookie & 0xFFFFFFFF);

	params = qdf_mem_malloc(sizeof(*params));
	if (!params)
		return -ENOMEM;

	params->peers = qdf_mem_malloc(n_peers * sizeof(*params->peers));
	if (!params->peers) {
		qdf_mem_free(params);
		return -ENOMEM;
	}

	params->req_id = (u32)(req->cookie & 0xFFFFFFFF);
	params->vdev_id = adapter->deflink->vdev_id;
	params->timeout = 0; /* no timeout */
	params->n_peers = n_peers;

	if (!qdf_is_macaddr_zero((struct qdf_mac_addr *)req->mac_addr_mask)) {
		u8 r_mac[QDF_MAC_ADDR_SIZE];

		params->mac_addr_randomization = true;

		qdf_get_random_bytes(r_mac, QDF_MAC_ADDR_SIZE);
		r_mac[0] = (r_mac[0] & 0xfe) | 0x02;

		for (i = 0; i < QDF_MAC_ADDR_SIZE; i++) {
			params->random_mac_addr[i] =
				(req->mac_addr[i] & req->mac_addr_mask[i]) |
				(r_mac[i] & ~req->mac_addr_mask[i]);
		}
	} else {
		params->mac_addr_randomization = false;
	}

	wifi_pos_debug("req_id:%d vdev:%d timeout:%d n_peers:%d randomization:%d",
		       params->req_id, params->vdev_id, params->timeout,
		       params->n_peers, params->mac_addr_randomization);

	for (i = 0; i < n_peers; i++) {
		struct cfg80211_pmsr_request_peer *peer = &req->peers[i];
		struct wmi_rtt_peer_meas_req_peer_params *p = &params->peers[i];
		const struct cfg80211_pmsr_ftm_request_peer *ftm = &peer->ftm;
		enum phy_ch_width ch_width;
		enum mlme_dot11_mode dot11_mode;

		qdf_mem_copy(p->dest_mac, peer->addr, QDF_MAC_ADDR_SIZE);

		/* Channel parameters */
		p->ch_freq = peer->chandef.chan->center_freq;
		p->ch_freq_seg1 = peer->chandef.center_freq1;
		p->ch_freq_seg2 = peer->chandef.center_freq2;
		ch_width = wlan_cfg80211_get_phy_ch_width(peer->chandef.width);
		p->ch_width = ch_width;
		dot11_mode = hdd_pmsr_preamble_to_dot11_mode(ftm->preamble);
		p->ch_phymode =
			wma_chan_phy_mode(peer->chandef.chan->center_freq,
					  ch_width, dot11_mode);

		p->preamble = hdd_pmsr_preamble_to_wmi(ftm->preamble);
		p->burst_period = ftm->burst_period;
		p->min_time_between_measurements =
			ftm->min_time_between_measurements;
		p->max_time_between_measurements =
			ftm->max_time_between_measurements;

		p->report_ap_tsf = 0;
		p->pd_request =
			(ftm->request_type == NL80211_PMSR_FTM_REQ_TYPE_PD);

		p->ftm_requested = ftm->requested;
		p->asap_mode = ftm->asap;
		p->lci_req = ftm->request_lci;
		p->loc_civic_req = ftm->request_civicloc;
		p->tb_ranging = ftm->trigger_based;
		p->ntb_ranging = ftm->non_trigger_based;
		p->i2r_lmr_feedback = ftm->lmr_feedback;
		p->rsta_role = ftm->rsta;

		p->num_burst_exp = ftm->num_bursts_exp;
		p->burst_duration = ftm->burst_duration;
		p->ftms_per_burst = ftm->ftms_per_burst;
		p->ftmr_retries = ftm->ftmr_retries;

		p->nominal_time = ftm->nominal_time;
		p->measurements_per_aw = ftm->ftms_per_burst;
		p->aw_duration = ftm->availability_window;
		p->suppress_range_results = ftm->pd_suppress_range_results;

		wifi_pos_debug("freq:%d cfreq1:%d cfreq2:%d ch_width:%d dot11_mode:%d phy_mode:%d preamble:%d burst_period:%d min_time:%d max_time:%d, report_tsf:%d pd_req:%d",
			       p->ch_freq, p->ch_freq_seg1, p->ch_freq_seg2,
			       ch_width, dot11_mode, p->ch_phymode,
			       p->preamble, p->burst_period,
			       p->min_time_between_measurements,
			       p->max_time_between_measurements,
			       p->report_ap_tsf, p->pd_request);
		wifi_pos_debug("ftm_requested:%d asap_mode:%d lci_req:%d loc_civic_req:%d tb_ranging:%d ntb:%d i2r_lmr:%d rsta_role:%d num_burst:%d burst_duration:%d ftms_per_burst:%d ftmr_retries:%d",
			       p->ftm_requested, p->asap_mode, p->lci_req,
			       p->loc_civic_req, p->tb_ranging, p->ntb_ranging,
			       p->i2r_lmr_feedback, p->rsta_role,
			       p->num_burst_exp, p->burst_duration,
			       p->ftms_per_burst, p->ftmr_retries);
		wifi_pos_debug("nominal_time:%d meas_per_aw:%d aw_dur:%d suppress_results:%d",
			       p->nominal_time, p->measurements_per_aw,
			       p->aw_duration, p->suppress_range_results);
	}

	status = wifi_pos_send_rtt_peer_meas_req(hdd_ctx->psoc, params);
	qdf_mem_free(params->peers);
	qdf_mem_free(params);

	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to send RTT peer meas req: %d", status);
		return qdf_status_to_os_return(status);
	}

	return 0;
}

/**
 * wlan_hdd_cfg80211_start_pmsr() - Start peer measurement request
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 *
 * Return: 0 on success, negative errno on failure
 */
int wlan_hdd_cfg80211_start_pmsr(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 struct cfg80211_pmsr_request *req)
{
	int errno;
	struct osif_vdev_sync *vdev_sync;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *sta_adapter;
	struct wireless_dev *sta_wdev = wdev;

	if (!hdd_ctx) {
		hdd_err("hdd_ctx is NULL");
		return -EINVAL;
	}

	sta_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
	if (!sta_adapter || !sta_adapter->wdev.netdev) {
		hdd_err("No Sta adapter");
		return -EINVAL;
	}

	sta_wdev = &sta_adapter->wdev;

	errno = osif_vdev_sync_op_start(sta_wdev->netdev, &vdev_sync);
	if (errno)
		return errno;

	errno = __wlan_hdd_cfg80211_start_pmsr(wiphy, sta_wdev, req);

	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * __wlan_hdd_cfg80211_abort_pmsr() - Abort peer measurement request (inner)
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 *
 * Sends WMI_RTT_PEER_MEAS_CANCEL_CMDID and frees the active pmsr_req.
 */
static void __wlan_hdd_cfg80211_abort_pmsr(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   struct cfg80211_pmsr_request *req)
{
	struct wlan_objmgr_psoc *psoc = wifi_pos_get_psoc();
	uint32_t req_id;
	QDF_STATUS status;

	if (!psoc) {
		wifi_pos_err("null psoc");
		return;
	}

	wlan_objmgr_psoc_get_ref(psoc, WLAN_WIFI_POS_OSIF_ID);

	req_id = (u32)(req->cookie & 0xFFFFFFFF);
	status = wifi_pos_send_rtt_peer_meas_cancel(psoc, req_id);
	if (QDF_IS_STATUS_ERROR(status))
		hdd_err("Failed to send RTT cancel cmd: %d", status);

	wlan_objmgr_psoc_release_ref(psoc, WLAN_WIFI_POS_OSIF_ID);
}

/**
 * wlan_hdd_cfg80211_abort_pmsr() - Abort peer measurement request
 * @wiphy: Pointer to wiphy
 * @wdev: Pointer to wireless device
 * @req: PMSR request from cfg80211
 */
void wlan_hdd_cfg80211_abort_pmsr(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  struct cfg80211_pmsr_request *req)
{
	struct osif_vdev_sync *vdev_sync;
	struct hdd_context *hdd_ctx = wiphy_priv(wiphy);
	struct hdd_adapter *sta_adapter;
	struct wireless_dev *sta_wdev = wdev;

	if (!hdd_ctx) {
		hdd_err("hdd_ctx is NULL");
		return;
	}

	sta_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
	if (!sta_adapter || !sta_adapter->wdev.netdev) {
		hdd_err("No Sta adapter");
		return;
	}

	sta_wdev = &sta_adapter->wdev;
	if (osif_vdev_sync_op_start(sta_wdev->netdev, &vdev_sync))
		return;

	__wlan_hdd_cfg80211_abort_pmsr(wiphy, sta_wdev, req);

	osif_vdev_sync_op_stop(vdev_sync);
}
#endif
