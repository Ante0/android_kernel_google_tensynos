/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <wlan_hdd_wondertap.h>
#include <wlan_hdd_main.h>
#include <osif_psoc_sync.h>
#include <osif_vdev_sync.h>
#include <wlan_hdd_regulatory.h>
#include <wlan_hdd_power.h>
#include <wlan_hdd_object_manager.h>
#include <wlan_hdd_packet_filter_api.h>
#include <wlan_dp_ucfg_api.h>
#include <wlan_vdev_mgr_api.h>
#include <wlan_fwol_ucfg_api.h>
#include "wlan_tdls_cfg_api.h"
#include <wma.h>
#include "cds_api.h"
#include "cdp_txrx_ctrl.h"
#include <wlan_hdd_hostapd.h>
#include "wlan_osif_request_manager.h"

static struct hdd_wondertap_context *g_wt_ctx;
static DEFINE_MUTEX(g_wt_ctx_mutex);

static enum phy_ch_width
__wlan_hdd_convert_wt_bandwidth_to_phy_ch_width(qdf_wondertap_rate_bw_t bw)
{
	switch (bw) {
	case WONDERTAP_RATE_BW_20:
		return CH_WIDTH_20MHZ;
	case WONDERTAP_RATE_BW_40:
		return CH_WIDTH_40MHZ;
	case WONDERTAP_RATE_BW_80:
		return CH_WIDTH_80MHZ;
	case WONDERTAP_RATE_BW_160:
		return CH_WIDTH_160MHZ;
	case WONDERTAP_RATE_BW_320:
		return CH_WIDTH_320MHZ;
	default:
		hdd_err("Incorrect bandwidth value:%d", bw);
		return CH_WIDTH_INVALID;
	};
}

static enum hw_mode_bandwidth
wlan_hdd_wondertap_bw_to_hw_mode_bw(qdf_wondertap_rate_bw_t bw)
{
	switch (bw) {
	case WONDERTAP_RATE_BW_20:
		return HW_MODE_20_MHZ;
	case WONDERTAP_RATE_BW_40:
		return HW_MODE_40_MHZ;
	case WONDERTAP_RATE_BW_80:
		return HW_MODE_80_MHZ;
	case WONDERTAP_RATE_BW_160:
		return HW_MODE_160_MHZ;
	case WONDERTAP_RATE_BW_320:
		return HW_MODE_320_MHZ;
	default:
		return HW_MODE_BW_NONE;
	}
}

static QDF_STATUS
__wlan_hdd_set_wondertap_channel(struct hdd_context *hdd_ctx,
				 struct hdd_adapter *adapter,
				 const qdf_wondertap_set_freq_params_t *params)
{
	struct channel_change_req req = {0};
	struct ch_params ch_params = {0};
	enum phy_ch_width ch_width;
	cdp_config_param_type val;
	QDF_STATUS status;
	int ret;

	if (wlan_hdd_change_hw_mode_for_given_chnl(adapter, params->freq,
						   POLICY_MGR_UPDATE_REASON_SET_OPER_CHAN)) {
		hdd_err("Failed to change HW mode");
		return QDF_STATUS_E_FAILURE;
	}

	ch_width =
	     __wlan_hdd_convert_wt_bandwidth_to_phy_ch_width(params->bandwidth);
	if (ch_width == CH_WIDTH_INVALID)
		return QDF_STATUS_E_INVAL;

	ret = hdd_validate_channel_and_bandwidth(adapter, params->freq,
						 0, ch_width);
	if (ret) {
		hdd_err("Invalid freq:%d and bw:%d combo", params->freq,
			ch_width);
		return QDF_STATUS_E_INVAL;
	}

	req.vdev_id = adapter->deflink->vdev_id;
	req.target_chan_freq = params->freq;
	req.ch_width = ch_width;

	ch_params.ch_width = req.ch_width;
	wlan_reg_set_channel_params_for_pwrmode(hdd_ctx->pdev,
						req.target_chan_freq, 0,
						&ch_params,
						REG_CURRENT_PWR_MODE);

	req.sec_ch_offset = ch_params.sec_ch_offset;
	req.center_freq_seg0 = ch_params.center_freq_seg0;
	req.center_freq_seg1 = ch_params.center_freq_seg1;

	sme_fill_channel_change_request(hdd_ctx->mac_handle, &req,
					eCSR_DOT11_MODE_11be);

	hdd_debug("dot11mode:%d nw_type:%d", req.dot11mode, req.nw_type);

	status = qdf_event_reset(&g_wt_ctx->wondertap_vdev_event);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("wondertap vdev up event reset failed:%d", status);
		goto channel_change_req_failed;
	}

	if (ucfg_scan_get_pdev_status(hdd_ctx->pdev) !=
	    SCAN_NOT_IN_PROGRESS)
		wlan_abort_scan(hdd_ctx->pdev,
				wlan_objmgr_pdev_get_pdev_id(hdd_ctx->pdev),
				INVAL_VDEV_ID, INVAL_SCAN_ID, true);

	status = sme_send_channel_change_req(hdd_ctx->mac_handle, &req);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("channel change request failed");
		goto channel_change_req_failed;
	}

	status = qdf_wait_for_event_completion(&g_wt_ctx->wondertap_vdev_event,
					       WLAN_WONDERTAP_VDEV_OP_TIMEOUT_MS);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("wondertap vdev up failed:%d", status);
	} else {
		val.cdp_passthru_vdev_freq = params->freq;
		cdp_txrx_set_vdev_param(cds_get_context(QDF_MODULE_ID_SOC),
					adapter->deflink->vdev_id,
					CDP_VDEV_SET_PASSTHRU_FREQ, val);
	}

channel_change_req_failed:

	return status;
}

static WMI_RATE_PREAMBLE
wlan_hdd_convert_wonder_preamble_to_wmi(qdf_wondertap_rate_preamble_t preamble)
{
	switch (preamble) {
	case WONDERTAP_RATE_PREAMBLE_HT:
		g_wt_ctx->tx_rate_cfg.dot11_mode = MLME_DOT11_MODE_11N;
		return WMI_RATE_PREAMBLE_HT;
	case WONDERTAP_RATE_PREAMBLE_VHT:
		g_wt_ctx->tx_rate_cfg.dot11_mode = MLME_DOT11_MODE_11AC;
		return WMI_RATE_PREAMBLE_VHT;
	case WONDERTAP_RATE_PREAMBLE_HE:
		g_wt_ctx->tx_rate_cfg.dot11_mode = MLME_DOT11_MODE_11AX;
		return WMI_RATE_PREAMBLE_HE;
	case WONDERTAP_RATE_PREAMBLE_EHT:
		g_wt_ctx->tx_rate_cfg.dot11_mode = MLME_DOT11_MODE_11BE;
		return WMI_RATE_PREAMBLE_EHT;
	case WONDERTAP_RATE_PREAMBLE_LEGACY:
	default:
		g_wt_ctx->tx_rate_cfg.dot11_mode = MLME_DOT11_MODE_ABG;
		return WMI_RATE_PREAMBLE_OFDM;
	}
}

static int
__wlan_hdd_wondertap_set_fixed_tx_rate(struct hdd_adapter *adapter,
				       const qdf_wondertap_tx_rate_params_t *params)
{
	WMI_RATE_PREAMBLE preamble;
	uint32_t rate_code;
	uint8_t gi;
	int ret;

	preamble = wlan_hdd_convert_wonder_preamble_to_wmi(params->preamble);
	rate_code = hdd_assemble_rate_code(preamble, params->nss - 1,
					   params->mcs);

	ret = wma_cli_set_command(adapter->deflink->vdev_id,
				  wmi_vdev_param_fixed_rate,
				  rate_code, VDEV_CMD);
	if (ret)
		hdd_err("Set fixed tx rate for wondertap failed:%d", ret);

	switch (params->gi) {
	case WONDERTAP_RATE_GI_SHORT:
		gi = 1;
		break;
	case WONDERTAP_RATE_GI_1_6_US:
		gi = 2;
		break;
	case WONDERTAP_RATE_GI_3_2_US:
		gi = 3;
		break;
	case WONDERTAP_RATE_GI_DEFAULT:
	case WONDERTAP_RATE_GI_0_8_US:
	default:
		gi = 0;
		break;
	}

	g_wt_ctx->tx_rate_cfg.nss = params->nss;
	g_wt_ctx->tx_rate_cfg.mcs = params->mcs;
	g_wt_ctx->tx_rate_cfg.gi_val = gi;
	g_wt_ctx->tx_rate_cfg.ch_width =
		__wlan_hdd_convert_wt_bandwidth_to_phy_ch_width(params->bw);
	ret = wma_cli_set_command(adapter->deflink->vdev_id,
				  wmi_vdev_param_sgi,
				  gi, VDEV_CMD);
	if (ret)
		hdd_err("Set GI for wondertap failed:%d", ret);

	ret = wma_cli_set_command(adapter->deflink->vdev_id,
				  wmi_vdev_param_chwidth,
				  params->bw, VDEV_CMD);
	if (ret)
		hdd_err("Set rate bw for wondertap failed:%d", ret);

	return ret;
}

static void
__wlan_hdd_wondertap_set_tx_rate_mask(struct hdd_adapter *adapter,
				      const qdf_wondertap_tx_rate_mask_params_t *params)
{
	WMI_RATE_PREAMBLE preamble;

	preamble =
		wlan_hdd_convert_wonder_preamble_to_wmi(params->max_preamble);

	g_wt_ctx->tx_rate_cfg.nss = params->max_nss;
	g_wt_ctx->tx_rate_cfg.mcs = params->max_mcs;
	g_wt_ctx->tx_rate_cfg.ch_width =
	__wlan_hdd_convert_wt_bandwidth_to_phy_ch_width(params->max_bw);
}

static
void __wlan_hdd_destroy_wondertap_intf(struct hdd_context *hdd_ctx,
				       struct hdd_adapter *adapter)
{
	hdd_close_adapter(hdd_ctx, adapter, true);
}

static struct hdd_adapter *
__wlan_hdd_create_wondertap_intf(struct hdd_context *hdd_ctx,
				 void **handle,
				 const qdf_wondertap_init_params_t *params)
{
	struct hdd_adapter_create_param create_params = {0};
	struct hdd_adapter *adapter;
	uint8_t mac_addr[QDF_MAC_ADDR_SIZE];

	create_params.num_sessions = 1;

	qdf_mem_copy(mac_addr, params->mac_addr, QDF_MAC_ADDR_SIZE);

	adapter = hdd_open_adapter(hdd_ctx, QDF_PASSTHRU_MODE, "wondertap%d",
				   mac_addr, NET_NAME_UNKNOWN, true,
				   &create_params);

	return adapter;
}

static
int __wlan_hdd_stop_wondertap_intf(struct hdd_context *hdd_ctx,
				   struct hdd_adapter *adapter)
{
	QDF_STATUS status;
	uint8_t num_ml_sta = 0, num_disabled_ml = 0;
	uint8_t ml_vdev_lst[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	qdf_freq_t ml_freq_lst[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	struct passthru_peer_tbl_entry *peer_tbl;
	uint8_t vdev_id = adapter->deflink->vdev_id;
	uint8_t i;

	wlan_hdd_netif_queue_control(adapter,
				     WLAN_STOP_ALL_NETIF_QUEUE_N_CARRIER,
				     WLAN_CONTROL_PATH);

	ASSERT_RTNL();

	dev_close(adapter->dev);

	peer_tbl = g_wt_ctx->peer_tbl;
	for (i = 0; i < WLAN_PASSTHRU_MAX_PEER; i++) {
		if (peer_tbl[i].peer_status == PASSTHRU_PEER_SETUP_NOT_DONE)
			continue;
		hdd_debug("Deleting wondertap peer " QDF_MAC_ADDR_FMT
			  " status %d on deinit",
			  QDF_MAC_ADDR_REF(peer_tbl[i].mac_addr.bytes),
			  peer_tbl[i].peer_status);
		wlan_hdd_wondertap_peer_del(hdd_ctx, vdev_id,
					    peer_tbl[i].mac_addr.bytes);
		qdf_mem_zero(&peer_tbl[i], sizeof(peer_tbl[i]));
	}

	status = qdf_event_reset(&g_wt_ctx->wondertap_vdev_event);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("wondertap vdev up event reset failed:%d", status);
		goto done;
	}

	sme_delete_pe_session(hdd_ctx->mac_handle, adapter->deflink->vdev_id,
			      QDF_PASSTHRU_MODE);

	status = qdf_wait_for_event_completion(&g_wt_ctx->wondertap_vdev_event,
					       WLAN_WONDERTAP_VDEV_OP_TIMEOUT_MS);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("wondertap vdev teardown failed:%d", status);
		goto done;
	}

	policy_mgr_decr_session_set_pcl(hdd_ctx->psoc, QDF_PASSTHRU_MODE,
					adapter->deflink->vdev_id);

	if (policy_mgr_is_mlo_sta_present(hdd_ctx->psoc)) {
		policy_mgr_get_ml_sta_info_psoc(hdd_ctx->psoc, &num_ml_sta,
						&num_disabled_ml,
						ml_vdev_lst,
						ml_freq_lst, NULL,
						NULL, NULL);
		hdd_debug("num_ml_sta %d num_disabled_ml %d", num_ml_sta,
			  num_disabled_ml);
		if (policy_mgr_sta_ml_link_enable_allowed(hdd_ctx->psoc,
							  num_disabled_ml,
							  num_ml_sta,
							  ml_freq_lst,
							  ml_vdev_lst)) {
			policy_mgr_mlo_sta_set_link(hdd_ctx->psoc,
					MLO_LINK_FORCE_REASON_DISCONNECT,
					MLO_LINK_FORCE_MODE_NO_FORCE,
					num_ml_sta, ml_vdev_lst);
		}
	}
	hdd_stop_adapter(hdd_ctx, adapter);
	hdd_deinit_adapter(hdd_ctx, adapter, true);
	clear_bit(DEVICE_IFACE_OPENED, &adapter->event_flags);

	ucfg_fwol_configure_global_params(hdd_ctx->psoc, hdd_ctx->pdev);

	wma_enable_disable_imps(hdd_ctx->pdev->pdev_objmgr.wlan_pdev_id, 1);

	if (!hdd_is_any_interface_open(hdd_ctx))
		hdd_psoc_idle_timer_start(hdd_ctx);

done:
	return qdf_status_to_os_return(status);
}

static
int __wlan_hdd_start_wondertap_intf(struct hdd_context *hdd_ctx,
				    struct hdd_adapter *adapter,
				    const qdf_wondertap_init_params_t *params)
{
	struct wlan_objmgr_vdev *vdev = NULL;
	struct ol_txrx_desc_type txrx_desc = {0};
	struct hdd_adapter *sta_adapter;
	struct wlan_hdd_link_info *sta_link_info;
	enum phy_ch_width ch_width;
	QDF_STATUS status;
	int ret;
	uint8_t num_ml_sta = 0, num_disabled_ml = 0, num_active_ml = 0;
	uint8_t ml_vdev_lst[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	qdf_freq_t ml_freq_lst[MAX_NUMBER_OF_CONC_CONNECTIONS] = {0};
	struct qdf_mac_addr active_link_addr[WLAN_MAX_ML_BSS_LINKS];
	uint32_t num_links = 0, i = 0, sta_cnt;
	uint8_t active_links_same_mac = 0, active_links_other_mac = 0;
	bool is_mcc = false;

	ret = hdd_start_adapter(adapter, true);
	if (ret) {
		hdd_err("Failed to start wondertap adapter %d", ret);
		return ret;
	}
	set_bit(DEVICE_IFACE_OPENED, &adapter->event_flags);

	if (hdd_is_connection_in_progress(NULL, NULL) ||
	    hdd_is_sta_connect_or_link_switch_in_prog(hdd_ctx,
						      adapter->device_mode)) {
		ret = -EBUSY;
		hdd_err_rl("Failed to start wonder tap as either connection or link switch is in progress ret = %d",
			   ret);
		goto stop_adapter;
	}

	if (!policy_mgr_allow_concurrency(hdd_ctx->psoc,
					  PM_PASSTHRU_MODE,
					  params->channel.freq,
					  wlan_hdd_wondertap_bw_to_hw_mode_bw(params->channel.bandwidth),
					  0, adapter->deflink->vdev_id)) {
		ret = -EPERM;
		goto stop_adapter;
	}

	/*
	 * sta > 2 : (STA + STA + STA) or (ML STA + STA) or (ML STA + ML STA),
	 * STA concurrency will be present.
	 *
	 * ML STA: Although both links would be treated as separate STAs
	 * (sta cnt = 2) from policy mgr perspective, but it is not considered
	 * as STA concurrency
	 */
	sta_cnt = policy_mgr_mode_specific_connection_count(hdd_ctx->psoc,
							    PM_STA_MODE, NULL);
	if (sta_cnt > 2 ||
	    (sta_cnt == 2 && policy_mgr_is_non_ml_sta_present(hdd_ctx->psoc))) {
		hdd_info("STA+STA present, wonder tap is not allowed");
		ret = -EPERM;
		goto stop_adapter;
	}

	/*
	 * If STA+PASSTHRU is MCC, and ML-STA is not present, skip the ML
	 * disable functionality as its similar to legacy STA+PASSTHRU.
	 */
	is_mcc = policy_mgr_will_freq_lead_to_mcc(hdd_ctx->psoc,
						  params->channel.freq);
	if (!(policy_mgr_is_mlo_sta_present(hdd_ctx->psoc) && is_mcc))
		goto skip_mlo_check;

	hdd_debug("ML STA MCC present, disable link");
	policy_mgr_get_ml_sta_info_psoc(hdd_ctx->psoc, &num_ml_sta,
					&num_disabled_ml, ml_vdev_lst,
					ml_freq_lst, NULL, NULL, NULL);
	hdd_debug("num_ml_sta %d num_disabled_ml %d", num_ml_sta,
		  num_disabled_ml);
	if (num_ml_sta > WLAN_MAX_ML_BSS_LINKS) {
		hdd_err("Invalid no.of links %d", num_ml_sta);
		num_ml_sta = WLAN_MAX_ML_BSS_LINKS;
	}

	/*
	 * If ML STA has more than 1 link, then determine the links that is
	 * leading to MCC with PASSTHRU, and disable the corresponding links,
	 * and if there is any inactive link on the other MAC, activate that
	 * link.
	 */
	num_active_ml = num_ml_sta - num_disabled_ml;
	if (num_ml_sta > 1 && num_active_ml > 0) {
		for (i = 0; i < num_active_ml; i++) {
			hdd_debug("ml_vdev_lst[%d] %d ml_freq_lst[%d] %d",
				  i, ml_vdev_lst[i], i, ml_freq_lst[i]);
			if (!(policy_mgr_2_freq_always_on_same_mac(
							hdd_ctx->psoc,
							params->channel.freq,
							ml_freq_lst[i]))) {
				status = wlan_get_self_macaddr_from_vdev_id(
							hdd_ctx->psoc,
							ml_vdev_lst[i],
							WLAN_DP_ID,
							&active_link_addr[active_links_other_mac]);
				if (QDF_IS_STATUS_ERROR(status)) {
					hdd_err("Invalid link vdev, %d",
						ml_vdev_lst[i]);
					continue;
				}
				active_links_other_mac++;
			} else {
				active_links_same_mac++;
			}
		}
	}

	/* If active links are present on both the macs, then disable
	 * the same mac links by setting num_links to other MAC active
	 * links and the link address is already fetched in the above
	 * for loop.
	 */
	if (active_links_same_mac && active_links_other_mac) {
		num_links = active_links_other_mac;
		is_mcc = false;
	} else if (active_links_same_mac) {
		if (!num_disabled_ml) {
			/* The below condition is true for 5G+6G MLO
			 * with both links active then disable the
			 * 6G link.
			 */
			if (active_links_same_mac > 1) {
				hdd_debug("%d MLO links on same MAC",
					  active_links_same_mac);
				for (i = 0; i < num_active_ml; i++) {
					if (!WLAN_REG_IS_6GHZ_CHAN_FREQ(
							ml_freq_lst[i])) {
						status =
						wlan_get_self_macaddr_from_vdev_id(
									hdd_ctx->psoc,
									ml_vdev_lst[i],
									WLAN_DP_ID,
									&active_link_addr[0]);
						if (QDF_IS_STATUS_ERROR(status)) {
							hdd_err("Invalid link vdev, %d",
								ml_vdev_lst[i]);
							continue;
						}
						num_links = 1;
						break;
					}
				}
			}
		} else {
			/* If there are no active links on other mac
			 * and STA have disabled links, check if the
			 * disabled link is on other mac or SCC, if so,
			 * activate the disabled link.
			 */
			while (i < num_ml_sta) {
				if (!(policy_mgr_2_freq_always_on_same_mac(
							hdd_ctx->psoc,
							params->channel.freq,
							ml_freq_lst[i])) ||
				    (params->channel.freq == ml_freq_lst[i])) {
					status =
					wlan_get_self_macaddr_from_vdev_id(
								hdd_ctx->psoc,
								ml_vdev_lst[i],
								WLAN_DP_ID,
								&active_link_addr[active_links_other_mac]);
					if (QDF_IS_STATUS_ERROR(status)) {
						hdd_err("Invalid link vdev, %d",
							ml_vdev_lst[i]);
						i++;
						continue;
					}
					active_links_other_mac++;
					is_mcc = false;
				}
				i++;
			}
			num_links = active_links_other_mac;
		}
	}
	if (num_links) {
		sme_activate_mlo_links(hdd_ctx->mac_handle, ml_vdev_lst[0],
				       num_links, active_link_addr,
				       MLO_LINK_FORCE_REASON_CONNECT);
	}
skip_mlo_check:
	if (is_mcc && !params->channel_hopping_enable) {
		hdd_err("STA MCC, CH hopping disabled, dont allow connection");
		ret = -EINVAL;
		goto stop_adapter;
	}

	vdev = hdd_objmgr_get_vdev_by_user(adapter->deflink, WLAN_DP_ID);
	if (!vdev) {
		ret = -EINVAL;
		goto stop_adapter;
	}

	status = wlan_vdev_mgr_set_param_bssid(vdev, &params->bssid[0]);
	if (QDF_IS_STATUS_ERROR(status)) {
		ret = qdf_status_to_os_return(status);
		goto stop_adapter;
	}

	status = sme_create_pe_session(hdd_ctx->mac_handle,
				       adapter->mac_addr.bytes,
				       adapter->deflink->vdev_id,
				       QDF_PASSTHRU_MODE);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("pe session create failed:%d", status);
		ret = qdf_status_to_os_return(status);
		goto stop_adapter;
	}

	/* Disable Roaming on all adapters before doing channel change */
	wlan_hdd_set_roaming_state(adapter->deflink, RSO_PASSTHRU_SET_CHANNEL,
				   false);

	status = __wlan_hdd_set_wondertap_channel(hdd_ctx, adapter,
						  &params->channel);
	if (QDF_IS_STATUS_ERROR(status)) {
		ret = qdf_status_to_os_return(status);
		goto delete_pe_session;
	}

	if (!params->rate_adaptation_enable) {
		ret = __wlan_hdd_wondertap_set_fixed_tx_rate(adapter,
							     &params->tx_rate);
		if (ret)
			goto delete_pe_session;
	} else {
		g_wt_ctx->is_peer_create_enabled = true;
		__wlan_hdd_wondertap_set_tx_rate_mask(adapter,
						      &params->tx_rate_mask);
	}

	sme_set_vdev_sw_retry(adapter->deflink->vdev_id,
			      params->data_retry_limit,
			      WMI_VDEV_CUSTOM_SW_RETRY_TYPE_AGGR);

	sme_set_vdev_sw_retry(adapter->deflink->vdev_id,
			      params->mgmt_retry_limit,
			      WMI_VDEV_CUSTOM_SW_RETRY_TYPE_NONAGGR);

	policy_mgr_incr_active_session(hdd_ctx->psoc, QDF_PASSTHRU_MODE,
				       adapter->deflink->vdev_id,
				       true);

	status = ucfg_dp_passthrough_register_txrx_ops(vdev);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("wondertap tx/rx ops register failed:%d", status);
		ret = qdf_status_to_os_return(status);
		goto delete_pe_session;
	}

	qdf_mem_copy(txrx_desc.peer_addr.bytes, adapter->mac_addr.bytes,
		     QDF_MAC_ADDR_SIZE);
	txrx_desc.is_qos_enabled = 1;
	ch_width =
		__wlan_hdd_convert_wt_bandwidth_to_phy_ch_width(params->channel.bandwidth);
	txrx_desc.bw = hdd_convert_ch_width_to_cdp_peer_bw(ch_width);

	status = cdp_peer_register(hdd_ctx->psoc->dp_handle, OL_TXRX_PDEV_ID,
				   &txrx_desc);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("peer registration failed for wondertap:%d", status);
		ret = qdf_status_to_os_return(status);
		goto delete_pe_session;
	}

	wlan_vdev_mlme_cap_set(vdev, WLAN_VDEV_C_RESTRICT_OFFCHAN);
	ucfg_fwol_set_ilp_config(hdd_ctx->psoc, hdd_ctx->pdev, 0);

	if (wma_enable_disable_imps(hdd_ctx->pdev->pdev_objmgr.wlan_pdev_id, 0))
		hdd_err("IMPS feature disable failed");

	sta_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
	if (sta_adapter) {
		hdd_adapter_for_each_active_link_info(sta_adapter,
						      sta_link_info)
			wlan_hdd_set_powersave(sta_link_info, false, 0);
	}

	hdd_change_peer_state(adapter->deflink, adapter->mac_addr.bytes,
			      OL_TXRX_PEER_STATE_AUTH);

	/*
	 * Stop and restart of bus bw periodic work would happen
	 * as part of close adapter so no need to explicitly invoke
	 * ucfg_dp_bus_bw_compute_timer_try_stop API in cleanup.
	 */
	ucfg_dp_bus_bw_compute_timer_start(hdd_ctx->psoc);

	hdd_debug("Enabling queues");
	wlan_hdd_netif_queue_control(adapter,
				     WLAN_START_ALL_NETIF_QUEUE_N_CARRIER,
				     WLAN_CONTROL_PATH);

	dev_open(adapter->dev, NULL);

	hdd_objmgr_put_vdev_by_user(vdev, WLAN_DP_ID);
	wlan_hdd_set_roaming_state(adapter->deflink, RSO_PASSTHRU_SET_CHANNEL,
				   true);

	return 0;

delete_pe_session:
	wlan_hdd_set_roaming_state(adapter->deflink, RSO_PASSTHRU_SET_CHANNEL,
				   true);

	sme_delete_pe_session(hdd_ctx->mac_handle,
			      adapter->deflink->vdev_id,
			      QDF_PASSTHRU_MODE);

stop_adapter:
	if (vdev)
		hdd_objmgr_put_vdev_by_user(vdev, WLAN_DP_ID);
	hdd_stop_no_trans(adapter->dev);

	return ret;
}

static QDF_STATUS
__wlan_hdd_populate_caps_peer_setup_req(
			struct sir_passthru_peer_setup_msg *req,
			const qdf_wondertap_station_info_t *sta_info,
			bool *caps_applied)
{
	uint8_t nss = 0;
	uint8_t i;

	hdd_debug("sta info cap mask: 0x%x", sta_info->capability_mask);
	if (sta_info->capability_mask & BIT(WONDERTAP_STATION_CAP_HT)) {
		req->htcap_present = 1;
		qdf_mem_copy(&req->peer_ht_cap, &sta_info->ht_capa,
			     sizeof(req->peer_ht_cap));
		/* NSS from HT MCS rx_mask: each non-zero byte
		 * represents one spatial stream
		 */
		for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
			if (sta_info->ht_capa.mcs.rx_mask[i])
				nss = i + 1;
		}
	}

	if (sta_info->capability_mask & BIT(WONDERTAP_STATION_CAP_VHT)) {
		uint8_t max_mcs;
		uint16_t rx_mcs_map =
			le16_to_cpu(sta_info->vht_capa.supp_mcs.rx_mcs_map);

		req->vhtcap_present = 1;
		qdf_mem_copy(&req->peer_vht_cap, &sta_info->vht_capa,
			     sizeof(req->peer_vht_cap));
		/* NSS from VHT rx_mcs_map: 2-bit value 0x3
		 * per stream indicates disabled
		 */
		nss = 0;
		for (i = 1; i <= 8; i++) {
			if (((rx_mcs_map >> ((i - 1) * 2)) & 0x3) != 0x3)
				nss = i;
		}
		/* max_mcs from highest MCS set in stream 1 of VHT rx_mcs_map */
		max_mcs = VHT_GET_MCS_FOR_NSS(rx_mcs_map, 1);
		if (max_mcs == VHT_MCS_0_9)
			req->max_mcs = 9;
		else if (max_mcs == VHT_MCS_0_8)
			req->max_mcs = 8;
		else if (max_mcs == VHT_MCS_0_7)
			req->max_mcs = 7;
		hdd_debug("vht: rx_mcs_map 0x%x max_mcs %u",
			  rx_mcs_map, req->max_mcs);
	}

	if (sta_info->capability_mask & BIT(WONDERTAP_STATION_CAP_HE)) {
		req->hecap_present = 1;
		qdf_mem_copy(&req->peer_he_cap, &sta_info->he_capa,
			     sizeof(req->peer_he_cap));
		/*
		 * ieee80211_he_cap_elem only carries MAC+PHY capability
		 * bytes -- HE MCS/NSS maps are not present in
		 * wondertap_station_info. If HT/VHT NSS extraction above
		 * yielded 0 (HE-only peer), fall back to tx_rate_cfg.nss
		 * which WONDER sets via set_fixed_tx_rate/set_tx_rate_mask.
		 */
		if (!nss)
			nss = g_wt_ctx->tx_rate_cfg.nss;
		if (!req->max_mcs)
			req->max_mcs = g_wt_ctx->tx_rate_cfg.mcs;
	}

	req->nss = nss ? nss : 1;

	if (sta_info->capability_mask &
		(BIT(WONDERTAP_STATION_CAP_HT) |
		 BIT(WONDERTAP_STATION_CAP_VHT) |
		 BIT(WONDERTAP_STATION_CAP_HE))) {
		if (caps_applied)
			*caps_applied = true;
		return QDF_STATUS_SUCCESS;
	}

	if (caps_applied)
		*caps_applied = false;
	return QDF_STATUS_E_INVAL;
}

static void
wlan_hdd_wondertap_peer_setup(struct hdd_context *hdd_ctx,
			      struct hdd_wondertap_peer_setup *peer,
			      const qdf_wondertap_station_info_t *sta_info,
			      bool *caps_applied)
{
	mac_handle_t mac_handle;
	struct sir_passthru_peer_setup_msg req = {0};
	QDF_STATUS status;

	if (wlan_hdd_validate_vdev_id(peer->vdev_id))
		return;

	hdd_debug("vdev %d peer setup for " QDF_MAC_ADDR_FMT,
		  peer->vdev_id,
		  QDF_MAC_ADDR_REF(peer->peer_addr));
	mac_handle = hdd_ctx->mac_handle;
	qdf_mem_copy(req.peer_mac_addr.bytes, peer->peer_addr,
		     QDF_MAC_ADDR_SIZE);
	req.vdev_id    = peer->vdev_id;
	req.peer_aid   = peer->peer_aid;
	req.ch_width   = g_wt_ctx->tx_rate_cfg.ch_width;
	req.dot11mode = g_wt_ctx->tx_rate_cfg.dot11_mode;
	req.gi_val = g_wt_ctx->tx_rate_cfg.gi_val;
	req.nss = g_wt_ctx->tx_rate_cfg.nss;
	req.max_mcs = g_wt_ctx->tx_rate_cfg.mcs;

	status = __wlan_hdd_populate_caps_peer_setup_req(&req, sta_info,
							 caps_applied);
	if (QDF_IS_STATUS_ERROR(status))
		req.create_only = 1;

	sme_passthru_peer_setup(mac_handle, &req);
}

static void
wlan_hdd_wondertap_peer_assoc(struct hdd_context *hdd_ctx, uint8_t vdev_id,
			      const uint8_t *peer_addr,
			      const qdf_wondertap_station_info_t *sta_info)
{
	struct sir_passthru_peer_setup_msg req = {0};
	mac_handle_t mac_handle;
	QDF_STATUS status;

	if (wlan_hdd_validate_vdev_id(vdev_id))
		return;

	hdd_debug("vdev %d peer assoc " QDF_MAC_ADDR_FMT,
		  vdev_id, QDF_MAC_ADDR_REF(peer_addr));
	mac_handle = hdd_ctx->mac_handle;

	qdf_mem_copy(req.peer_mac_addr.bytes, peer_addr, QDF_MAC_ADDR_SIZE);
	req.vdev_id     = vdev_id;
	req.gi_val      = g_wt_ctx->tx_rate_cfg.gi_val;
	req.create_only = 0;

	status = __wlan_hdd_populate_caps_peer_setup_req(&req, sta_info, NULL);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_debug("no valid caps provided");
		return;
	}

	hdd_debug("vdev %d peer assoc nss %d max_mcs %d",
		  vdev_id, req.nss, req.max_mcs);

	sme_passthru_peer_setup(mac_handle, &req);
}

void wlan_hdd_wondertap_peer_del(struct hdd_context *hdd_ctx,
				 uint8_t vdev_id,
				 const uint8_t *peer_addr)
{
	struct sir_passthru_peer_del_msg req = {0};
	mac_handle_t mac_handle;

	if (wlan_hdd_validate_vdev_id(vdev_id))
		return;

	hdd_debug("vdev %d peer del " QDF_MAC_ADDR_FMT,
		  vdev_id, QDF_MAC_ADDR_REF(peer_addr));
	mac_handle = hdd_ctx->mac_handle;
	qdf_mem_copy(req.peer_mac_addr.bytes, peer_addr, QDF_MAC_ADDR_SIZE);
	req.vdev_id = vdev_id;
	sme_passthru_peer_del(mac_handle, &req);
}

static inline QDF_STATUS
wlan_hdd_disable_offchan_tdls(struct hdd_context *hdd_ctx, int offchmode)
{
	struct wlan_objmgr_vdev *tdls_obj_vdev;
	bool tdls_off_ch;
	QDF_STATUS status;

	status = cfg_tdls_get_off_channel_enable(hdd_ctx->psoc, &tdls_off_ch);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("cfg get tdls off ch failed");
		return status;
	}

	if (!tdls_off_ch) {
		hdd_debug("tdls off ch is false, do nothing");
		return QDF_STATUS_SUCCESS;
	}

	if (!hdd_ctx->tdls_umac_comp_active)
		return QDF_STATUS_SUCCESS;

	tdls_obj_vdev = ucfg_get_tdls_vdev(hdd_ctx->psoc, WLAN_TDLS_NB_ID);
	if (tdls_obj_vdev) {
		status = ucfg_set_tdls_offchan_mode(tdls_obj_vdev, offchmode);
		wlan_objmgr_vdev_release_ref(tdls_obj_vdev, WLAN_TDLS_NB_ID);
	}

	return status;
}
/**
 * wlan_hdd_wondertap_init() - Initialize wondertap interface
 * @handle: Pointer to store the wondertap handle
 * @params: Initialization parameters for wondertap interface
 *
 * This function initializes the wondertap interface with the provided
 * parameters. It allocates necessary resources and prepares the interface
 * for operation.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int wlan_hdd_wondertap_init(void **handle,
			    const qdf_wondertap_init_params_t *params)
{
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	struct osif_vdev_sync *vdev_sync;
	struct hdd_adapter *adapter;
	struct hdd_wondertap_context *wt_ctx;
	uint8_t curr_cc[REG_ALPHA2_LEN + 1] = {0};
	QDF_STATUS status;
	int errno;

	hdd_enter();

	if (!hdd_ctx || !params || !handle)
		return -EINVAL;

	hdd_info("Self MAC:" QDF_MAC_ADDR_FMT " BSSID:" QDF_MAC_ADDR_FMT " freq:%d bandwidth:%d",
		 QDF_MAC_ADDR_REF(params->mac_addr),
		 QDF_MAC_ADDR_REF(params->bssid), params->channel.freq,
		 params->channel.bandwidth);

	hdd_info("Fixed Tx rate preamble:%d bw:%d gi:%d nss:%d mcs:%d",
		 params->tx_rate.preamble, params->tx_rate.bw,
		 params->tx_rate.gi, params->tx_rate.nss,
		 params->tx_rate.mcs);
	hdd_info("Rate mask preamble:%d bw:%d nss:%d max_mcs:%d rate_adaptation:%d",
		 params->tx_rate_mask.max_preamble, params->tx_rate_mask.max_bw,
		 params->tx_rate_mask.max_nss, params->tx_rate_mask.max_mcs,
		 params->rate_adaptation_enable);

	if (params->channel.bandwidth > WONDERTAP_RATE_BW_320 ||
	    params->tx_rate.bw > WONDERTAP_RATE_BW_320 ||
	    params->tx_rate.preamble > WONDERTAP_RATE_PREAMBLE_EHT ||
	    params->tx_rate.gi > WONDERTAP_RATE_GI_3_2_US ||
	    !params->tx_rate.nss)
		return -EINVAL;

	ASSERT_RTNL();

	errno = osif_vdev_sync_create_and_trans(hdd_ctx->parent_dev,
						&vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto destroy_sync;

	if (hdd_get_conparam() != QDF_GLOBAL_MISSION_MODE) {
		hdd_err("Command not allowed in mode:%d", hdd_get_conparam());
		goto destroy_sync;
	}

	if (hdd_is_connection_in_progress(NULL, NULL) ||
	    hdd_is_sta_connect_or_link_switch_in_prog(hdd_ctx,
						      QDF_PASSTHRU_MODE)) {
		errno = -EBUSY;
		hdd_err_rl("Failed to start wonder tap as either connection or link switch is in progress errno = %d",
			   errno);
		goto destroy_sync;
	}

	errno = hdd_trigger_psoc_idle_restart(hdd_ctx);
	if (errno) {
		hdd_err("Idle restart failed %d", errno);
		goto destroy_sync;
	}

	ucfg_reg_get_current_country(hdd_ctx->psoc, curr_cc);

	hdd_info("set regulatory cc:%s curr_cc:%s", params->country_code,
		 curr_cc);

	errno = hdd_reg_set_country(hdd_ctx, (char *)params->country_code);
	if (errno) {
		hdd_info("set country code failed:%d", errno);
		goto destroy_sync;
	}

	wt_ctx = qdf_mem_malloc(sizeof(*wt_ctx));
	if (!wt_ctx) {
		hdd_err("wondertap memory alloc failed");
		errno = -ENOMEM;
		goto mem_malloc_failed;
	}

	status = qdf_event_create(&wt_ctx->wondertap_vdev_event);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("wondertap vdev up event creation failed");
		errno = qdf_status_to_os_return(status);
		goto create_wondertap_event_failed;
	}

	mutex_lock(&g_wt_ctx_mutex);
	g_wt_ctx = wt_ctx;
	mutex_unlock(&g_wt_ctx_mutex);

	status = qdf_runtime_lock_init(&wt_ctx->wondertap_rtpm_lock);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("passthrough mode rtpm lock creation failed");
		errno = qdf_status_to_os_return(status);
		goto create_rtpm_lock_failed;
	}

	status = qdf_wake_lock_create(&wt_ctx->wondertap_wakelock,
				      "wlan_passthrough");
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("passthrough mode wakelock creation failed");
		errno = qdf_status_to_os_return(status);
		goto create_wake_lock_failed;
	}

	qdf_wake_lock_acquire(&wt_ctx->wondertap_wakelock,
			      WIFI_POWER_EVENT_WAKELOCK_PASSTHRU,
			      QDF_WAKE_TIME_UNDEFINED);
	qdf_runtime_pm_prevent_suspend_sync(&wt_ctx->wondertap_rtpm_lock);

	hdd_info("Disabling TDLS off channel");
	status = wlan_hdd_disable_offchan_tdls(hdd_ctx,
					       DISABLE_ACTIVE_CHANSWITCH);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to disable off-channel TDLS");
		goto offchan_tdls_disable_fail;
	}

	adapter = __wlan_hdd_create_wondertap_intf(hdd_ctx, handle, params);
	if (IS_ERR_OR_NULL(adapter)) {
		errno = qdf_status_to_os_return(QDF_STATUS_E_FAILURE);
		goto create_wondertap_intf_failed;
	}

	osif_vdev_sync_register(adapter->dev, &adapter->wdev, vdev_sync);

	errno = __wlan_hdd_start_wondertap_intf(hdd_ctx, adapter, params);
	if (errno)
		goto start_wondertap_intf_failed;

	wt_ctx->hdd_ctx = hdd_ctx;
	wt_ctx->wt_adapter = adapter;
	wt_ctx->magic = get_random_u32();

	*handle = (void *)wt_ctx->magic;

	osif_vdev_sync_trans_stop(vdev_sync);

	return errno;

start_wondertap_intf_failed:
	osif_vdev_sync_unregister(adapter->dev);
	__wlan_hdd_destroy_wondertap_intf(hdd_ctx, adapter);

create_wondertap_intf_failed:
	wlan_hdd_disable_offchan_tdls(hdd_ctx, ENABLE_CHANSWITCH);

offchan_tdls_disable_fail:
	qdf_runtime_pm_allow_suspend(&wt_ctx->wondertap_rtpm_lock);
	qdf_wake_lock_release(&wt_ctx->wondertap_wakelock,
			      WIFI_POWER_EVENT_WAKELOCK_PASSTHRU);
	qdf_wake_lock_destroy(&wt_ctx->wondertap_wakelock);

create_wake_lock_failed:
	qdf_runtime_lock_deinit(&wt_ctx->wondertap_rtpm_lock);

create_rtpm_lock_failed:
	mutex_lock(&g_wt_ctx_mutex);
	qdf_event_destroy(&wt_ctx->wondertap_vdev_event);
	g_wt_ctx = NULL;
	mutex_unlock(&g_wt_ctx_mutex);

create_wondertap_event_failed:
	qdf_mem_free(wt_ctx);

mem_malloc_failed:
	hdd_reg_set_country(hdd_ctx, curr_cc);

destroy_sync:
	osif_vdev_sync_trans_stop(vdev_sync);
	osif_vdev_sync_destroy(vdev_sync);

	return errno;
}

/**
 * wlan_hdd_wondertap_deinit() - Deinitialize wondertap interface
 * @handle: Wondertap handle to deinitialize
 * @params: deinit parameters
 *
 * This function deinitializes the wondertap interface and releases all
 * resources allocated during initialization.
 *
 * Return: None
 */
static
void wlan_hdd_wondertap_deinit(void *handle,
			       const qdf_wondertap_deinit_params_t *params)
{
	struct hdd_wondertap_context *wt_ctx;
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct hdd_adapter *sta_adapter;
	struct wlan_hdd_link_info *sta_link_info;
	struct pkt_filter_cfg filter_req = {0};
	struct osif_vdev_sync *vdev_sync;
	int errno;

	hdd_enter();

	/*
	 * Serialize access to g_wt_ctx across init/deinit/setters and avoid
	 * races where multiple deinit callers free g_wt_ctx concurrently.
	 */
	mutex_lock(&g_wt_ctx_mutex);
	wt_ctx = g_wt_ctx;
	if (!wt_ctx || handle != (void *)wt_ctx->magic) {
		mutex_unlock(&g_wt_ctx_mutex);
		hdd_debug("Incorrect handle received - rejecting deinit");
		return;
	}

	hdd_ctx = wt_ctx->hdd_ctx;
	wt_adapter = wt_ctx->wt_adapter;
	mutex_unlock(&g_wt_ctx_mutex);

	ASSERT_RTNL();

	errno = osif_vdev_sync_trans_start_wait(wt_adapter->dev, &vdev_sync);
	if (errno)
		return;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto destroy_sync;

	if (wt_ctx->is_frame_filter_set) {
		filter_req.filter_action = HDD_RCV_FILTER_CLEAR;
		errno = wlan_hdd_set_filter(hdd_ctx, &filter_req,
					    wt_adapter->deflink->vdev_id);
		if (errno)
			hdd_debug("Clear frame type/subtype based filter failed:%d",
				  errno);
	}

	errno = __wlan_hdd_stop_wondertap_intf(hdd_ctx, wt_adapter);
	if (errno)
		goto destroy_sync;

	osif_vdev_sync_unregister(wt_adapter->dev);
	osif_vdev_sync_wait_for_ops(vdev_sync);

	__wlan_hdd_destroy_wondertap_intf(hdd_ctx, wt_adapter);

	errno = hdd_reg_set_country(hdd_ctx, (char *)params->country_code);
	if (errno)
		hdd_info("set country code failed:%d", errno);

	sta_adapter = hdd_get_adapter(hdd_ctx, QDF_STA_MODE);
	if (sta_adapter) {
		hdd_adapter_for_each_active_link_info(sta_adapter,
						      sta_link_info)
			wlan_hdd_set_powersave(sta_link_info, true, 0);
	}

	hdd_info("Enabling TDLS off channel");
	wlan_hdd_disable_offchan_tdls(hdd_ctx, ENABLE_CHANSWITCH);
	qdf_runtime_pm_allow_suspend(&wt_ctx->wondertap_rtpm_lock);
	qdf_wake_lock_release(&wt_ctx->wondertap_wakelock,
			      WIFI_POWER_EVENT_WAKELOCK_PASSTHRU);
	qdf_wake_lock_destroy(&wt_ctx->wondertap_wakelock);
	qdf_runtime_lock_deinit(&wt_ctx->wondertap_rtpm_lock);

	mutex_lock(&g_wt_ctx_mutex);
	/*
	 * Ensure only the matching instance clears/frees the global context.
	 * If a new session got created concurrently (unlikely, but possible),
	 * avoid tearing it down here.
	 */
	if (g_wt_ctx == wt_ctx) {
		qdf_event_destroy(&wt_ctx->wondertap_vdev_event);
		g_wt_ctx = NULL;
		mutex_unlock(&g_wt_ctx_mutex);

		qdf_mem_free(wt_ctx);
	} else {
		mutex_unlock(&g_wt_ctx_mutex);
	}

destroy_sync:
	osif_vdev_sync_trans_stop(vdev_sync);
	osif_vdev_sync_destroy(vdev_sync);
	hdd_exit();

	return;
}

/**
 * wlan_hdd_wondertap_set_freq() - Set operating frequency
 * @handle: Wondertap handle
 * @params: Channel parameters including frequency and bandwidth
 *
 * This function configures the operating frequency and bandwidth
 * for the wondertap interface based on the provided parameters.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int wlan_hdd_wondertap_set_freq(void *handle,
				const qdf_wondertap_set_freq_params_t *params)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	QDF_STATUS status;
	int errno;

	hdd_info("set_freq:%d bandwidth:%d",
		 params->freq, params->bandwidth);

	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic) {
		hdd_debug("Incorrect handle received - rejecting set_freq");
		return -EINVAL;
	}

	hdd_ctx = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	errno = osif_vdev_sync_trans_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto stop_trans;

	if (!policy_mgr_is_chan_change_allowed_for_passthru(hdd_ctx->psoc,
							    wt_adapter->deflink->vdev_id,
							    params->freq,
							    wlan_hdd_wondertap_bw_to_hw_mode_bw(params->bandwidth))) {
		hdd_debug("Channel change not allowed freq:%d bw:%d",
			  params->freq, params->bandwidth);
		errno = -EINVAL;
		goto stop_trans;
	}

	status = __wlan_hdd_set_wondertap_channel(hdd_ctx, wt_adapter, params);
	errno = qdf_status_to_os_return(status);

stop_trans:
	osif_vdev_sync_trans_stop(vdev_sync);

	return errno;
}

/**
 * wlan_hdd_wondertap_set_filter() - Configure a specific hardware packet
 *  filter
 * @handle: Wondertap handle
 * @filter_type: type of filter to configure
 * @params: void pointer to filter-specific parameter structure.
 *
 * This function configures a specific hardware packet
 * for the wondertap interface based on the provided parameters.
 *
 * Return: 0 on success, negative error code on failure
 */
static
int wlan_hdd_wondertap_set_filter(void *handle,
				  qdf_wondertap_filter_type_t filter_type,
				  const void *params)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	struct pkt_filter_cfg filter_req = {0};
	qdf_wondertap_frame_filter_params_t *filter_params;
	QDF_STATUS status;
	int errno;

	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic) {
		hdd_debug("Incorrect handle received - rejecting set_filter");
		return -EINVAL;
	}

	if (filter_type != QDF_WONDERTAP_FILTER_TYPE_FRAME) {
		hdd_debug("Invalid filter type:%d", filter_type);
		return -EINVAL;
	}

	/* validate the input params */
	filter_params = (qdf_wondertap_frame_filter_params_t *)params;
	if (!filter_params->enabled && !g_wt_ctx->is_frame_filter_set) {
		hdd_debug("No active filter to disable");
		return -EINVAL;
	}

	hdd_ctx = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	errno = osif_vdev_sync_op_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto stop_op;

	/* Support only one filter for now so override with latest one*/
	if (g_wt_ctx->is_frame_filter_set) {
		filter_req.filter_action = HDD_RCV_FILTER_CLEAR;
		status = wlan_hdd_set_filter(hdd_ctx, &filter_req,
					     wt_adapter->deflink->vdev_id);
		hdd_debug("clear frame type/subtype based filter status:%d",
			  status);
		if (!filter_params->enabled || status) {
			errno = qdf_status_to_os_return(status);
			goto stop_op;
		}
	}

	filter_req.filter_action = HDD_RCV_FILTER_SET;
	filter_req.num_params = 1;
	filter_req.params_data[0].protocol_layer = HDD_FILTER_PROTO_TYPE_MAC;
	filter_req.params_data[0].compare_flag = HDD_FILTER_CMP_TYPE_EQUAL;
	filter_req.params_data[0].data_length = 1;
	filter_req.params_data[0].data_offset = 0;
	filter_req.params_data[0].compare_data[0] =
		(filter_params->frame_type | filter_params->frame_subtype);
	filter_req.params_data[0].data_mask[0] = 0xFC;

	status = wlan_hdd_set_filter(hdd_ctx, &filter_req,
				    wt_adapter->deflink->vdev_id);
	if (status) {
		hdd_debug("Set frame type/subtype based filter failed:%d",
			  status);
		errno = qdf_status_to_os_return(status);
		goto stop_op;
	}

	g_wt_ctx->is_frame_filter_set = true;
	g_wt_ctx->frame_filter = filter_req.params_data[0].compare_data[0];

stop_op:
	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * wlan_hdd_wondertap_set_fixed_tx_rate() - Set fixed TX rate
 * @handle: Wondertap handle
 * @params: TX rate parameters including MCS, NSS, and preamble type
 *
 * This function configures a fixed transmission rate for the wondertap
 * interface. When set, all packets will be transmitted at the specified
 * rate instead of using rate adaptation.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_set_fixed_tx_rate(void *handle,
				const qdf_wondertap_tx_rate_params_t *params)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	int errno;

	hdd_info("fixed Tx rate preamble:%d bw:%d gi:%d nss:%d mcs:%d",
		 params->preamble, params->bw, params->gi,
		 params->nss, params->mcs);

	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic ||
	    g_wt_ctx->is_peer_create_enabled) {
		hdd_debug("rejecting set_fixed_tx_rate");
		return -EINVAL;
	}

	if (params->bw > WONDERTAP_RATE_BW_320 ||
	    params->preamble > WONDERTAP_RATE_PREAMBLE_EHT ||
	    params->gi > WONDERTAP_RATE_GI_3_2_US ||
	    !params->nss)
		return -EINVAL;

	hdd_ctx = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	errno = osif_vdev_sync_op_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto stop_op;

	errno = __wlan_hdd_wondertap_set_fixed_tx_rate(wt_adapter, params);

stop_op:
	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * wlan_hdd_wondertap_set_tx_rate_mask() - Set TX rate mask
 * @handle: Wondertap handle
 * @params: TX rate mask parameters specifying allowed rates
 *
 * This function configures a mask of allowed transmission rates for the
 * wondertap interface. The rate adaptation algorithm will only select
 * rates that are enabled in the mask.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_set_tx_rate_mask(void *handle,
			const qdf_wondertap_tx_rate_mask_params_t *params)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	int errno;

	hdd_info("set tx rate mask max params - preamble:%d bw:%d nss:%d  mcs:%d",
		 params->max_preamble, params->max_bw, params->max_nss,
		 params->max_mcs);

	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic) {
		hdd_debug("Incorrect handle received - rejecting set_tx_rate_mask");
		return -EINVAL;
	}

	if (params->max_bw > WONDERTAP_RATE_BW_320 ||
	    params->max_preamble > WONDERTAP_RATE_PREAMBLE_EHT ||
	    !params->max_nss)
		return -EINVAL;

	hdd_ctx = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	errno = osif_vdev_sync_op_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto stop_op;

	__wlan_hdd_wondertap_set_tx_rate_mask(wt_adapter, params);

stop_op:
	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * wlan_hdd_wondertap_get_capabilities() - Populate supported capabilities
 * @handle: Wondertap handle
 * @features: Pointer to structure to store supported features
 *
 * This function populates the list of features that the
 * driver supports for the wondertap operation.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_get_capabilities(void *handle,
				    qdf_wondertap_capability_t *features)
{
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	int ret;

	if (!hdd_ctx)
		return -EBUSY;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret)
		return ret;

	qdf_mem_zero(features, sizeof(*features));

	features->bits.dynamic_freq = 1;
	features->bits.dynamic_fixed_tx_rate = 1;
	features->bits.frame_type_filter = 1;
	features->bits.custom_mgmt_retry_limit = 1;
	features->bits.custom_data_retry_limit = 1;
	features->bits.frame_type_filter = 1;
	features->bits.sta_coexist = 1;
	features->maximum_channel_switch_time_us = 50000;

	features->bits.nss = QDF_MIN(hdd_ctx->num_rf_chains,
				     (uint32_t)WLAN_PASSTHRU_MAX_NSS);
	hdd_debug("passthru cap bitmap 0x%llx nss %d",
		  hdd_ctx->passthru_cap_bitmap, features->bits.nss);
	if (hdd_ctx->passthru_cap_bitmap & WLAN_HDD_PASSTHRU_CHAN_HOP_CAP_BIT)
		features->bits.channel_hopping = 1;

	if (hdd_ctx->passthru_cap_bitmap & WLAN_HDD_PASSTHRU_AMPDU_RA_CAP_BIT) {
		features->bits.rate_adaptation = 1;
		features->bits.ampdu_aggregation = 1;
	}

	return ret;
}

static
wmi_channel_width hdd_convert_wondertap_bw_to_wmi_bw(qdf_wondertap_rate_bw_t bw)
{
	switch (bw) {
	case WONDERTAP_RATE_BW_20:
		return WMI_CHAN_WIDTH_20;
	case WONDERTAP_RATE_BW_40:
		return WMI_CHAN_WIDTH_40;
	case WONDERTAP_RATE_BW_80:
		return WMI_CHAN_WIDTH_80;
	case WONDERTAP_RATE_BW_160:
		return WMI_CHAN_WIDTH_160;
	case WONDERTAP_RATE_BW_320:
		return WMI_CHAN_WIDTH_320;
	default:
		return WMI_CHAN_WIDTH_20;
	}
}

static wmi_channel_hopping_role
hdd_convert_wondertap_role_to_wmi_role(qdf_wondertap_role_t role)
{
	switch (role) {
	case WONDERTAP_ROLE_NOP:
		return WMI_CHANNEL_HOPPING_ROLE_PASSTHRU;
	default:
		return WMI_CHANNEL_HOPPING_ROLE_NON_PASSTHRU;
	}
}

/**
 * wlan_hdd_wondertap_set_chan_sched() - Set MCC channel schedule for wondertap
 * @handle: Wondertap handle
 * @chan_sched: channel schedule parameters
 *
 * This function configures a channel schedule that needs to be followed
 * based on the parameters provided.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_set_chan_sched(void *handle,
				  const qdf_wondertap_channel_sch_req_t *chan_sched)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	struct vdev_ch_hop_sched_params params = {0};
	struct vdev_ch_hop_ch_params *ch_list;
	QDF_STATUS status;
	int errno;
	uint8_t i;

	hdd_info("channel list size:%d next_chan_idx:%d dwell_time:%dms target_switch_time:0x%x",
		 chan_sched->channel_list_len, chan_sched->next_channel_index,
		 chan_sched->dwell_time_tu, chan_sched->target_switch_time_tsf);

	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic) {
		hdd_debug("Incorrect handle received - rejecting set_chan_sched");
		return -EINVAL;
	}

	hdd_ctx = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	errno = osif_vdev_sync_op_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto stop_op;

	params.vdev_id = wt_adapter->deflink->vdev_id;
	params.next_channel_idx = chan_sched->next_channel_index;
	params.dwell_time_tu = chan_sched->dwell_time_tu;
	params.target_switch_time_tsf = chan_sched->target_switch_time_tsf;
	params.chan_list_len = chan_sched->channel_list_len;

	params.chan_list = qdf_mem_malloc(sizeof(*params.chan_list) *
					  params.chan_list_len);
	if (!params.chan_list) {
		hdd_err("Channel list malloc failed");
		goto stop_op;
	}

	ch_list = params.chan_list;
	for (i = 0; i < params.chan_list_len; i++) {
		ch_list[i].freq = chan_sched->channel_list[i].freq;
		ch_list[i].bandwidth =
			hdd_convert_wondertap_bw_to_wmi_bw(chan_sched->channel_list[i].bandwidth);
		ch_list[i].role =
			hdd_convert_wondertap_role_to_wmi_role(chan_sched->channel_list[i].role);
	}

	status = wma_send_vdev_ch_hop_sched(&params);
	errno = qdf_status_to_os_return(status);
	qdf_mem_free(params.chan_list);

stop_op:
	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * struct hdd_wondertap_get_tsf_timer_priv - Wondertap get tsf priv context
 * @response: response containing tsf
 * @status: status
 */
struct hdd_wondertap_get_tsf_timer_priv {
	struct ocb_get_tsf_timer_response response;
	int status;
};

#define WLAN_WONDERTAP_GET_TSF_WAIT_TIME_MS 1000

static void hdd_wondertap_get_tsf_resp_cb(void *ctx, void *response_ptr)
{
	struct osif_request *request;
	struct hdd_wondertap_get_tsf_timer_priv *priv;
	struct ocb_get_tsf_timer_response *response = response_ptr;

	request = osif_request_get(ctx);
	if (!request) {
		hdd_err("obsolete get_tsf request");
		return;
	}

	priv = osif_request_priv(request);
	if (response) {
		priv->response = *response;
		priv->status = 0;
	} else {
		priv->status = -EINVAL;
	}

	osif_request_complete(request);
	osif_request_put(request);
}

#define WLAN_HDD_MAC_TSF_LSHIFT_VAL 10
#define WLAN_HDD_MAC_TSF_CLK_MHZ    960

/**
 * wlan_hdd_wondertap_get_mac_tsf() - Get MAC TSF timestamp
 * @handle: Wondertap handle
 * @mac_tsf: MAC TSF utilized for channel hopping schedule
 *
 * This function is used to get the MAC TSF timestamp to be used
 * for channel hopping schedule.
 *
 * Return: 0 on success, negative error code on failure
 */
static int wlan_hdd_wondertap_get_mac_tsf(void *handle, uint32_t *mac_tsf)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	struct ocb_get_tsf_timer_param req = {0};
	struct osif_request *request;
	struct hdd_wondertap_get_tsf_timer_priv *priv;
	void *cookie;
	static const struct osif_request_params params = {
		.priv_size = sizeof(*priv),
		.timeout_ms = WLAN_WONDERTAP_GET_TSF_WAIT_TIME_MS,
	};
	QDF_STATUS status;
	int errno;

	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic) {
		hdd_debug("Incorrect handle received - rejecting set_chan_sched");
		return -EINVAL;
	}

	hdd_ctx = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	errno = osif_vdev_sync_op_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto stop_op;

	request = osif_request_alloc(&params);
	if (!request) {
		hdd_err("osif request alloc failure");
		errno = -ENOMEM;
		goto stop_op;
	}
	cookie = osif_request_cookie(request);

	req.vdev_id = wt_adapter->deflink->vdev_id;
	status = wma_passthru_get_tsf_timer(&req, hdd_wondertap_get_tsf_resp_cb,
					    cookie);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to send get_tsf timer command");
		errno = qdf_status_to_os_return(status);
		goto osif_req_put;
	}

	errno = osif_request_wait_for_response(request);
	if (errno) {
		hdd_err("get_tsf timed out");
		goto osif_req_put;
	}

	priv = osif_request_priv(request);
	errno = priv->status;
	if (errno) {
		hdd_err("get_tsf operation failed:%d", errno);
		goto osif_req_put;
	}

	*mac_tsf = ((uint64_t)priv->response.timer_low <<
		    WLAN_HDD_MAC_TSF_LSHIFT_VAL) / WLAN_HDD_MAC_TSF_CLK_MHZ;

	hdd_debug("TSF timer high:0x%x low:0x%x out:0x%x",
		  priv->response.timer_high, priv->response.timer_low,
		  *mac_tsf);

osif_req_put:
	osif_request_put(request);

stop_op:
	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

void hdd_sme_passthrough_mode_callback(uint8_t vdev_id, bool is_up)
{
	hdd_debug("Channel change successful for wondertap");

	mutex_lock(&g_wt_ctx_mutex);
	if (g_wt_ctx)
		qdf_event_set(&g_wt_ctx->wondertap_vdev_event);

	mutex_unlock(&g_wt_ctx_mutex);
}

#define WLAN_CHAN_HOP_STATUS_WAIT_TIME_MS 1000

/**
 * struct hdd_chan_hop_status_priv - Channel hop status private context
 * @response: Response structure from firmware
 * @status: Operation status (0 on success, negative on error)
 *
 * Private data structure for osif_request to handle synchronous
 * channel hop status request.
 */
struct hdd_chan_hop_status_priv {
	struct vdev_chan_hop_status_response response;
	int status;
};

/**
 * hdd_chan_hop_status_resp_cb() - Channel hop status response callback
 * @ctx: osif_request context
 * @response_ptr: Response structure pointer
 *
 * Callback invoked by WMA layer when channel hop status event is received.
 * Stores the response and completes the osif_request to wake waiting thread.
 */
static void hdd_chan_hop_status_resp_cb(void *ctx, void *response_ptr)
{
	struct osif_request *request;
	struct hdd_chan_hop_status_priv *priv;
	struct vdev_chan_hop_status_response *response = response_ptr;

	request = osif_request_get(ctx);
	if (!request) {
		hdd_err("Obsolete chan_hop_status request");
		return;
	}

	priv = osif_request_priv(request);
	if (response) {
		priv->response = *response;
		priv->status = 0;
	} else {
		priv->status = -EINVAL;
	}

	osif_request_complete(request);
	osif_request_put(request);
}

/**
 * wlan_hdd_wondertap_get_channel_status_report() - Get channel status report
 * @handle: Wondertap handle
 * @report: Report structure to fill (pre-allocated by Wonder driver)
 *
 * Retrieves channel hopping statistics from firmware and converts to
 * wondertap format. This is a synchronous operation that blocks until
 * firmware responds or timeout occurs.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_get_channel_status_report(
	void *handle,
	qdf_wondertap_channel_status_report_t *report)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	struct vdev_chan_hop_status_req req = {0};
	struct osif_request *request;
	struct hdd_chan_hop_status_priv *priv;
	struct wlan_objmgr_psoc *psoc;
	struct wmi_unified *wmi_handle;
	void *cookie;
	static const struct osif_request_params params = {
		.priv_size = sizeof(*priv),
		.timeout_ms = WLAN_CHAN_HOP_STATUS_WAIT_TIME_MS,
	};
	QDF_STATUS status;
	int errno;
	uint32_t i;

	hdd_enter();

	/* Validate wondertap context and handle */
	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic) {
		hdd_err("Incorrect handle received - rejecting get_channel_status");
		return -EINVAL;
	}

	if (!report) {
		hdd_err("Invalid report pointer");
		return -EINVAL;
	}

	hdd_ctx = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	/* Validate HDD context */
	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		return errno;

	psoc = hdd_ctx->psoc;

	wmi_handle = get_wmi_unified_hdl_from_psoc(psoc);
	if (!wmi_handle ||
	    !wmi_service_enabled(wmi_handle,
				 wmi_service_vdev_chan_hop_status_report)) {
		hdd_err("wmi_service_vdev_chan_hop_status_report not supported");
		return -ENOTSUPP;
	}
	/* Start vdev operation */
	errno = osif_vdev_sync_op_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	/* Allocate osif_request for synchronous operation */
	request = osif_request_alloc(&params);
	if (!request) {
		hdd_err("osif request alloc failure");
		errno = -ENOMEM;
		goto stop_op;
	}
	cookie = osif_request_cookie(request);

	/* Prepare request */
	req.vdev_id = wt_adapter->deflink->vdev_id;

	/* Send command to firmware */
	status = wma_vdev_get_chan_hop_status(&req,
					      hdd_chan_hop_status_resp_cb,
					      cookie);
	if (QDF_IS_STATUS_ERROR(status)) {
		hdd_err("Failed to send get chan hop status command: %d",
			status);
		errno = qdf_status_to_os_return(status);
		goto osif_req_put;
	}

	/* Wait for response with timeout */
	errno = osif_request_wait_for_response(request);
	if (errno) {
		hdd_err("get chan hop status timed out");
		goto osif_req_put;
	}

	/* Get response from private data */
	priv = osif_request_priv(request);
	errno = priv->status;
	if (errno) {
		hdd_err("get chan hop status operation failed: %d", errno);
		goto osif_req_put;
	}

	/* Convert response to wondertap format */
	report->current_channel_hopping_request_tsf =
		priv->response.hopping_request_tsf;
	report->current_channel_index =
		priv->response.current_channel_index;
	report->channel_status_len = priv->response.num_slots;

	hdd_debug("hopping_request_tsf=0x%x, current_channel_index=%d, num_slots=%d",
		  priv->response.hopping_request_tsf,
		  priv->response.current_channel_index,
		  priv->response.num_slots);

	hdd_debug("Converting %d channel status entries",
		  report->channel_status_len);

	/* Copy slot information with type conversions */
	for (i = 0; i < priv->response.num_slots; i++) {
		report->status[i].channel_switch_tsf =
			priv->response.slot_info[i].channel_switch_tsf;
		report->status[i].freq =
			priv->response.slot_info[i].freq;
		report->status[i].channel_start_tsf =
			priv->response.slot_info[i].channel_start_tsf;
		report->status[i].channel_end_tsf =
			priv->response.slot_info[i].channel_end_tsf;
		/* Convert u32 to u16 for traffic indices */
		report->status[i].tx_traffic_index =
			(uint16_t)priv->response.slot_info[i].tx_traffic_index;
		report->status[i].rx_traffic_index =
			(uint16_t)priv->response.slot_info[i].rx_traffic_index;

		hdd_debug("Slot %d: freq=%d, channel_switch_tsf=0x%x, channel_start_tsf=0x%x, channel_end_tsf=0x%x, tx_idx=%d, rx_idx=%d",
			  i, report->status[i].freq,
			  report->status[i].channel_switch_tsf,
			  report->status[i].channel_start_tsf,
			  report->status[i].channel_end_tsf,
			  report->status[i].tx_traffic_index,
			  report->status[i].rx_traffic_index);
	}

	hdd_info("Successfully retrieved %d channel status entries",
		 report->channel_status_len);

osif_req_put:
	osif_request_put(request);

stop_op:
	osif_vdev_sync_op_stop(vdev_sync);

	hdd_exit();
	return errno;
}

/**
 * wlan_hdd_wondertap_set_station_info - Add, update, or remove station info
 * @handle: opaque wondertap handle
 * @action: station action (NEW / UPDATE / DEL / QUERY)
 * @info: station information parameters
 *
 * Handles station management requests from the wonder framework.
 *
 * Return: 0 on success, negative error code on failure
 */
static int
wlan_hdd_wondertap_set_station_info(void *handle,
				    const qdf_wondertap_station_action_t action,
				    qdf_wondertap_station_info_t *info)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *wt_adapter;
	struct osif_vdev_sync *vdev_sync;
	struct passthru_peer_tbl_entry *peer_tbl;
	struct hdd_wondertap_peer_setup params = {0};
	struct qdf_mac_addr peer_mac;
	bool trigger_peer_create = false;
	uint8_t i, vdev_id;
	int errno;

	if (!g_wt_ctx || handle != (void *)g_wt_ctx->magic || !info)
		return -EINVAL;

	hdd_ctx    = g_wt_ctx->hdd_ctx;
	wt_adapter = g_wt_ctx->wt_adapter;

	errno = osif_vdev_sync_op_start(wt_adapter->dev, &vdev_sync);
	if (errno)
		return errno;

	errno = wlan_hdd_validate_context(hdd_ctx);
	if (errno)
		goto stop_op;

	peer_tbl = g_wt_ctx->peer_tbl;
	vdev_id  = wt_adapter->deflink->vdev_id;
	qdf_mem_copy(peer_mac.bytes, info->mac, QDF_MAC_ADDR_SIZE);

	switch (action) {
	case WONDERTAP_STATION_STATE_NEW:
		hdd_debug("set_station_info aid %d", info->aid);
		if (g_wt_ctx->num_peers >= WLAN_PASSTHRU_MAX_PEER) {
			hdd_err("max peers %d reached", WLAN_PASSTHRU_MAX_PEER);
			errno = -ENOSPC;
			break;
		}

		/*
		 * First pass: check all slots for a duplicate MAC with an
		 * active (non-NOT_DONE) status. This must run before the
		 * free-slot search — a deleted slot (NOT_DONE) at an earlier
		 * index could otherwise be found before an active entry for
		 * the same MAC at a later index, incorrectly triggering a
		 * new peer create.
		 */
		for (i = 0; i < WLAN_PASSTHRU_MAX_PEER; i++) {
			if (!qdf_mem_cmp(info->mac, peer_tbl[i].mac_addr.bytes,
					 QDF_MAC_ADDR_SIZE) &&
			    peer_tbl[i].peer_status !=
					PASSTHRU_PEER_SETUP_NOT_DONE) {
				hdd_debug("set_station_info NEW: peer "
					  QDF_MAC_ADDR_FMT
					  " already exists, skipping",
					  QDF_MAC_ADDR_REF(info->mac));
				break;
			}
		}
		if (i < WLAN_PASSTHRU_MAX_PEER)
			break;

		/* Second pass: find a free slot */
		for (i = 0; i < WLAN_PASSTHRU_MAX_PEER; i++) {
			if (peer_tbl[i].peer_status ==
					PASSTHRU_PEER_SETUP_NOT_DONE) {
				trigger_peer_create = true;
				break;
			}

			if (!qdf_mem_cmp(info->mac, peer_tbl[i].mac_addr.bytes,
					 QDF_MAC_ADDR_SIZE) &&
			    (peer_tbl[i].peer_status !=
			     PASSTHRU_PEER_SETUP_NOT_DONE))
				break;
		}

		if (!trigger_peer_create) {
			hdd_err("set_station_info NEW: no free slot for "
				QDF_MAC_ADDR_FMT, QDF_MAC_ADDR_REF(info->mac));
			errno = -ENOSPC;
			break;
		}

		g_wt_ctx->num_peers++;
		qdf_mem_copy(peer_tbl[i].mac_addr.bytes, info->mac,
			     QDF_MAC_ADDR_SIZE);
		peer_tbl[i].sta_info.aid = info->aid;
		peer_tbl[i].peer_status = PASSTHRU_PEER_SETUP_IN_PROGRESS;

		hdd_debug("set_station_info NEW peer " QDF_MAC_ADDR_FMT,
			  QDF_MAC_ADDR_REF(info->mac));

		qdf_mem_copy(params.peer_addr, info->mac, QDF_MAC_ADDR_SIZE);
		qdf_mem_copy(&peer_tbl[i].sta_info, info,
			     sizeof(peer_tbl[i].sta_info));

		params.vdev_id  = vdev_id;
		params.peer_aid = info->aid;
		peer_tbl[i].caps_applied_in_new = false;
		wlan_hdd_wondertap_peer_setup(hdd_ctx, &params, info,
					      &peer_tbl[i].caps_applied_in_new);
		break;

	case WONDERTAP_STATION_STATE_UPDATE:
		hdd_debug("set_station_info aid %d", info->aid);
		for (i = 0; i < WLAN_PASSTHRU_MAX_PEER; i++) {
			if (qdf_is_macaddr_equal(&peer_tbl[i].mac_addr,
						 &peer_mac))
				break;
		}
		if (i == WLAN_PASSTHRU_MAX_PEER) {
			hdd_err("UPDATE: peer not found " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(info->mac));
			errno = -ENOENT;
			goto stop_op;
		}
		qdf_mem_copy(&peer_tbl[i].sta_info, info,
			     sizeof(peer_tbl[i].sta_info));

		if (peer_tbl[i].caps_applied_in_new) {
			hdd_debug("UPDATE: caps already applied in NEW for "
				  QDF_MAC_ADDR_FMT ", skipping",
				  QDF_MAC_ADDR_REF(info->mac));
			break;
		}

		hdd_debug("set_station_info UPDATE " QDF_MAC_ADDR_FMT,
			  QDF_MAC_ADDR_REF(info->mac));
		wlan_hdd_wondertap_peer_assoc(hdd_ctx, vdev_id,
					      info->mac, info);
		break;

	case WONDERTAP_STATION_STATE_DEL:
		for (i = 0; i < WLAN_PASSTHRU_MAX_PEER; i++) {
			if (qdf_is_macaddr_equal(&peer_tbl[i].mac_addr,
						 &peer_mac))
				break;
		}
		if (i == WLAN_PASSTHRU_MAX_PEER) {
			hdd_err("DEL: peer not found " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(info->mac));
			errno = -ENOENT;
			goto stop_op;
		}
		qdf_mem_zero(&peer_tbl[i].sta_info,
			     sizeof(peer_tbl[i].sta_info));
		peer_tbl[i].peer_status = PASSTHRU_PEER_SETUP_NOT_DONE;
		peer_tbl[i].caps_applied_in_new = false;
		g_wt_ctx->num_peers--;

		hdd_debug("set_station_info DEL " QDF_MAC_ADDR_FMT,
			  QDF_MAC_ADDR_REF(info->mac));
		wlan_hdd_wondertap_peer_del(hdd_ctx, vdev_id, info->mac);
		break;

	case WONDERTAP_STATION_STATE_QUERY:
		for (i = 0; i < WLAN_PASSTHRU_MAX_PEER; i++) {
			if (qdf_is_macaddr_equal(&peer_tbl[i].mac_addr,
						 &peer_mac))
				break;
		}
		if (i == WLAN_PASSTHRU_MAX_PEER) {
			hdd_err("QUERY: peer not found " QDF_MAC_ADDR_FMT,
				QDF_MAC_ADDR_REF(info->mac));
			errno = -ENOENT;
			goto stop_op;
		}
		qdf_mem_copy(info, &peer_tbl[i].sta_info, sizeof(*info));

		hdd_debug("set_station_info QUERY " QDF_MAC_ADDR_FMT,
			  QDF_MAC_ADDR_REF(info->mac));
		break;

	default:
		errno = -EINVAL;
		break;
	}

stop_op:
	osif_vdev_sync_op_stop(vdev_sync);

	return errno;
}

/**
 * wlan_drv_wondertap_ops - Wondertap operations structure
 *
 * This structure defines the set of operations that the WLAN driver
 * provides to the wondertap framework. It includes callbacks for
 * initialization, configuration, and feature queries.
 */
static const qdf_wondertap_ops_t wlan_drv_wondertap_ops = {
	.init = wlan_hdd_wondertap_init,
	.deinit = wlan_hdd_wondertap_deinit,
	.set_freq = wlan_hdd_wondertap_set_freq,
	.set_filter = wlan_hdd_wondertap_set_filter,
	.set_fixed_tx_rate = wlan_hdd_wondertap_set_fixed_tx_rate,
	.set_tx_rate_mask = wlan_hdd_wondertap_set_tx_rate_mask,
	.get_capabilities = wlan_hdd_wondertap_get_capabilities,
	.channel_schedule_request = wlan_hdd_wondertap_set_chan_sched,
	.get_mac_tsf = wlan_hdd_wondertap_get_mac_tsf,
	.get_channel_status_report =
		wlan_hdd_wondertap_get_channel_status_report,
	.set_station_info = wlan_hdd_wondertap_set_station_info,
};

/**
 * wlan_drv_wondertap_priv - Wondertap private data structure
 *
 * wondertap private data structure that holds the wonder
 * version supported and the operations table.
 */
static const qdf_wondertap_priv_t wlan_drv_wondertap_priv = {
	.ver = WONDER_VERSION_1_6_5,
	.wonder_ops = &wlan_drv_wondertap_ops,
};

int wlan_hdd_wondertap_register_ops(struct device *dev)
{
	return pld_set_vendor_wonder_priv_data(dev, &wlan_drv_wondertap_priv);
}

void wlan_hdd_wondertap_unregister_ops(struct device *dev, bool force_cleanup)
{
	struct hdd_context *hdd_ctx;
	struct hdd_adapter *adapter;
	struct osif_vdev_sync *vdev_sync;
	QDF_STATUS status;

	hdd_enter();
	pld_set_vendor_wonder_priv_data(dev, NULL);
	hdd_debug("g_wt_ctx_valid %d force %d",
		  g_wt_ctx ? 1 : 0, force_cleanup);

	hdd_hold_rtnl_lock();
	mutex_lock(&g_wt_ctx_mutex);

	if (force_cleanup && g_wt_ctx) {
		hdd_ctx = g_wt_ctx->hdd_ctx;
		adapter = g_wt_ctx->wt_adapter;
		/* Keep reference to event for later use */
		qdf_event_t *vdev_event = &g_wt_ctx->wondertap_vdev_event;
		mutex_unlock(&g_wt_ctx_mutex);

		wlan_hdd_netif_queue_control(adapter,
				     WLAN_STOP_ALL_NETIF_QUEUE_N_CARRIER,
				     WLAN_CONTROL_PATH);

		dev_close(adapter->dev);

		qdf_event_reset(vdev_event);
		sme_delete_pe_session(hdd_ctx->mac_handle, adapter->deflink->vdev_id,
				      QDF_PASSTHRU_MODE);

		status = qdf_wait_for_event_completion(vdev_event,
						       WLAN_WONDERTAP_VDEV_OP_TIMEOUT_MS);
		if (QDF_IS_STATUS_ERROR(status))
			hdd_err("wondertap vdev teardown failed:%d", status);

		policy_mgr_decr_session_set_pcl(hdd_ctx->psoc, QDF_PASSTHRU_MODE,
						adapter->deflink->vdev_id);

		hdd_stop_adapter(hdd_ctx, adapter);
		hdd_deinit_adapter(hdd_ctx, adapter, true);

		vdev_sync = osif_vdev_sync_unregister(adapter->dev);
		osif_vdev_sync_destroy(vdev_sync);

		__wlan_hdd_destroy_wondertap_intf(hdd_ctx, adapter);

		/* Final cleanup under mutex */
		mutex_lock(&g_wt_ctx_mutex);
		if (g_wt_ctx) {
			qdf_runtime_pm_allow_suspend(
						&g_wt_ctx->wondertap_rtpm_lock);
			qdf_wake_lock_release(
					&g_wt_ctx->wondertap_wakelock,
					WIFI_POWER_EVENT_WAKELOCK_PASSTHRU);
			qdf_wake_lock_destroy(&g_wt_ctx->wondertap_wakelock);
			qdf_runtime_lock_deinit(&g_wt_ctx->wondertap_rtpm_lock);

			qdf_event_destroy(&g_wt_ctx->wondertap_vdev_event);
			qdf_mem_free(g_wt_ctx);
			g_wt_ctx = NULL;
		}
	}

	mutex_unlock(&g_wt_ctx_mutex);

	hdd_release_rtnl_lock();
	hdd_exit();
}
