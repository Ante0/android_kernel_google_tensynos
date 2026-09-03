/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef WLAN_DP_FEATURE_SW_LATENCY_MGR

#include <dp_types.h>
#include <dp_internal.h>
#include <wlan_cfg.h>
#include "wlan_dp_swlm.h"
#include "qdf_time.h"
#include "qdf_util.h"
#include "hal_internal.h"
#include "hal_api.h"
#include "hif.h"
#include <qdf_status.h>
#include <qdf_nbuf.h>

/**
 * dp_swlm_is_tput_thresh_reached() - Calculate the current tx and rx TPUT
 *				      and check if it passes the pre-set
 *				      threshold.
 * @soc: Datapath global soc handle
 * @rid: TCL ring id
 *
 * This function calculates the current TX and RX throughput and checks
 * if it is above the pre-set thresholds by SWLM.
 *
 * Returns: true, if the TX/RX throughput is passing the threshold
 *	    false, otherwise
 */
static bool dp_swlm_is_tput_thresh_reached(struct dp_soc *soc, uint8_t rid)
{
	struct dp_swlm_params *params = &soc->swlm.params;
	int rx_delta, tx_delta, tx_packet_delta;
	bool result = false;

	tx_delta = soc->stats.tx.egress[rid].bytes -
			params->tcl[rid].prev_tx_bytes;
	params->tcl[rid].prev_tx_bytes = soc->stats.tx.egress[rid].bytes;
	if (tx_delta > params->tx_traffic_thresh) {
		params->tcl[rid].sampling_session_tx_bytes = tx_delta;
		result = true;
	}

	rx_delta = soc->stats.rx.ingress.bytes - params->tcl[rid].prev_rx_bytes;
	params->tcl[rid].prev_rx_bytes = soc->stats.rx.ingress.bytes;
	if (!result && rx_delta > params->rx_traffic_thresh) {
		params->tcl[rid].sampling_session_tx_bytes = tx_delta;
		result = true;
	}

	tx_packet_delta = soc->stats.tx.egress[rid].num -
		params->tcl[rid].prev_tx_packets;
	params->tcl[rid].prev_tx_packets = soc->stats.tx.egress[rid].num;
	if (tx_packet_delta < params->tx_pkt_thresh)
		result = false;

	return result;
}

/**
 * dp_swlm_tcl_timer_dom_check() - Check timer-dominance and update coalescing
 *     enable/disable state for a TCL ring.
 * @swlm: DP SWLM pointer
 * @rid: TCL ring id
 * @curr_time: current timestamp in microseconds
 *
 * Called on every packet. Returns immediately if the 50ms monitor window
 * has not yet expired.
 *
 */
static void dp_swlm_tcl_timer_dom_check(struct dp_swlm *swlm, uint8_t rid,
					uint64_t curr_time)
{
	struct dp_swlm_params *params = &swlm->params;
	uint32_t total, timer_dom;

	if (curr_time < params->tcl[rid].mon_win_ts + DP_SWLM_RATIO_WIN_US)
		return;

	total = params->tcl[rid].mon_bytes_flush_cnt;
	timer_dom = params->tcl[rid].mon_timer_flush_cnt;

	if (!total) {
		/* No flushes in this window; re-enable after cooldown if
		 * coalesce is still disabled. This helps to restart
		 * coalescing when the traffic is stopped mid way and
		 * coalesce was left in disabled state previously.
		 */
		if (params->tcl[rid].coalesce_disable &&
		    curr_time - params->tcl[rid].coalesce_last_dis_ts >=
		    DP_SWLM_RATIO_DIS_COOLDOWN_US) {
			params->tcl[rid].coalesce_disable = 0;
			params->tcl[rid].bytes_coalesced = 0;
			params->tcl[rid].consec_timer_dom_cnt = 0;
			DP_STATS_INC(swlm, tcl[rid].timer_dom_coalesce_ena, 1);
		}
		goto reset;
	}

	if (!params->tcl[rid].coalesce_disable) {
		if (timer_dom << DP_SWLM_RATIO_THRESH_SHIFT > total) {
			params->tcl[rid].consec_timer_dom_cnt++;
			if (params->tcl[rid].consec_timer_dom_cnt <
			    DP_SWLM_RATIO_DIS_CONSEC_WIN)
				goto reset;
			params->tcl[rid].coalesce_disable = 1;
			params->tcl[rid].coalesce_last_dis_ts = curr_time;
			params->tcl[rid].consec_timer_dom_cnt = 0;
			DP_STATS_INC(swlm, tcl[rid].timer_dom_coalesce_dis, 1);
		} else {
			params->tcl[rid].consec_timer_dom_cnt = 0;
		}
	} else if (timer_dom << DP_SWLM_RATIO_THRESH_SHIFT < total &&
		   curr_time - params->tcl[rid].coalesce_last_dis_ts >=
		   DP_SWLM_RATIO_DIS_COOLDOWN_US) {
		params->tcl[rid].coalesce_disable = 0;
		params->tcl[rid].bytes_coalesced = 0;
		params->tcl[rid].consec_timer_dom_cnt = 0;
		DP_STATS_INC(swlm, tcl[rid].timer_dom_coalesce_ena, 1);
	}

reset:
	params->tcl[rid].mon_win_ts = curr_time;
	params->tcl[rid].mon_bytes_flush_cnt = 0;
	params->tcl[rid].mon_timer_flush_cnt = 0;
}

/**
 * dp_swlm_can_tcl_wr_coalesce() - To check if current TCL reg write can be
 *				   coalesced or not.
 * @soc: Datapath global soc handle
 * @tcl_data: priv data for tcl coalescing
 *
 * This function takes into account the current tx and rx throughput and
 * decides whether the TCL register write corresponding to the current packet,
 * to be transmitted, is to be processed or coalesced.
 * It maintains a session for which the TCL register writes are coalesced and
 * then flushed if a certain time/bytes threshold is reached.
 *
 * Returns: 1 if the current TCL write is to be coalesced
 *	    0, if the current TCL write is to be processed.
 */
static int
dp_swlm_can_tcl_wr_coalesce(struct dp_soc *soc,
			    struct dp_swlm_tcl_data *tcl_data)
{
	u64 curr_time = qdf_get_log_timestamp_usecs();
	int tput_level_pass, coalesce = 0;
	struct dp_swlm *swlm = &soc->swlm;
	uint8_t rid = tcl_data->ring_id;
	struct dp_swlm_params *params = &soc->swlm.params;
	uint32_t prev_consec;

	if (curr_time >= params->tcl[rid].expire_time) {
		params->tcl[rid].expire_time = qdf_get_log_timestamp_usecs() +
			      params->sampling_time;
		tput_level_pass = dp_swlm_is_tput_thresh_reached(soc, rid);
		if (tput_level_pass) {
			params->tcl[rid].tput_pass_cnt++;
		} else {
			params->tcl[rid].tput_pass_cnt = 0;
			DP_STATS_INC(swlm, tcl[rid].tput_criteria_fail, 1);
			goto coalescing_fail;
		}
	}

	params->tcl[rid].bytes_coalesced += tcl_data->pkt_len;

	if (params->tcl[rid].tput_pass_cnt > DP_SWLM_TCL_TPUT_PASS_THRESH) {
		coalesce = 1;

		if (params->tcl[rid].bytes_coalesced >
		    params->tcl[rid].bytes_flush_thresh) {
			coalesce = 0;
			prev_consec = params->tcl[rid].consec_timer_flush_cnt;
			params->tcl[rid].consec_timer_flush_cnt = 0;
			params->tcl[rid].mon_bytes_flush_cnt++;
			if (prev_consec >= DP_SWLM_TIMER_DOM_CONSEC_MIN)
				params->tcl[rid].mon_timer_flush_cnt++;
			DP_STATS_INC(swlm, tcl[rid].bytes_thresh_reached, 1);
		} else if (curr_time > params->tcl[rid].coalesce_end_time) {
			coalesce = 0;
			params->tcl[rid].consec_timer_flush_cnt++;
			DP_STATS_INC(swlm, tcl[rid].time_thresh_reached, 1);
		}
	}

	dp_swlm_tcl_timer_dom_check(swlm, rid, curr_time);

	if (params->tcl[rid].coalesce_disable)
		coalesce = 0;

coalescing_fail:
	if (!coalesce) {
		dp_swlm_tcl_reset_session_data(soc, rid);
		return 0;
	}

	qdf_timer_mod(&params->tcl[rid].flush_timer, 1);

	return 1;
}

QDF_STATUS dp_print_swlm_stats(struct dp_soc *soc)
{
	struct dp_swlm *swlm = &soc->swlm;
	int i;

	for (i = 0; i < soc->num_tcl_data_rings; i++) {
		dp_info("TCL: %u Coalescing stats:", i);
		dp_info("Num coalesce success: %d",
			swlm->stats.tcl[i].coalesce_success);
		dp_info("Num coalesce fail: %d",
			swlm->stats.tcl[i].coalesce_fail);
		dp_info("Timer flush success: %d",
			swlm->stats.tcl[i].timer_flush_success);
		dp_info("Timer flush fail: %d",
			swlm->stats.tcl[i].timer_flush_fail);
		dp_info("Coalesce fail (TID): %d",
			swlm->stats.tcl[i].tid_fail);
		dp_info("Coalesce fail (special frame): %d",
			swlm->stats.tcl[i].sp_frames);
		dp_info("Coalesce fail (Low latency connection): %d",
			swlm->stats.tcl[i].ll_connection);
		dp_info("Coalesce fail (bytes thresh crossed): %d",
			swlm->stats.tcl[i].bytes_thresh_reached);
		dp_info("Coalesce fail (time thresh crossed): %d",
			swlm->stats.tcl[i].time_thresh_reached);
		dp_info("Coalesce fail (TPUT sampling fail): %d",
			swlm->stats.tcl[i].tput_criteria_fail);
		dp_info("Coalesce fail (Timer dom dis): %d",
			swlm->stats.tcl[i].timer_dom_coalesce_dis);
		dp_info("Coalesce fail (Timer dom ena): %d",
			swlm->stats.tcl[i].timer_dom_coalesce_ena);
	}

	return QDF_STATUS_SUCCESS;
}

static struct dp_swlm_ops dp_latency_mgr_ops = {
	.tcl_wr_coalesce_check = dp_swlm_can_tcl_wr_coalesce,
};

/**
 * dp_swlm_tcl_flush_timer() - Timer handler for tcl register write coalescing
 * @arg: private data of the timer
 *
 * Returns: none
 */
static void dp_swlm_tcl_flush_timer(void *arg)
{
	struct dp_swlm_tcl_params *tcl = arg;
	struct dp_soc *soc = tcl->soc;
	struct dp_swlm *swlm = &soc->swlm;
	int ret;

	ret = soc->arch_ops.dp_flush_tx_ring(soc->pdev_list[0], tcl->ring_id);
	if (ret) {
		DP_STATS_INC(swlm, tcl[tcl->ring_id].timer_flush_fail, 1);
		return;
	}

	/*
	 * consec_timer_flush_cnt is also read and reset in the TX datapath
	 * without a lock. SWLM is intentionally designed lock-free to avoid
	 * overhead in the hot TX path. This is a statistical heuristic counter
	 * and an occasional lost increment has negligible impact on the
	 * timer-dominance ratio computed over a 50ms window.
	 */
	soc->swlm.params.tcl[tcl->ring_id].consec_timer_flush_cnt++;

	DP_STATS_INC(swlm, tcl[tcl->ring_id].timer_flush_success, 1);
}

/**
 * dp_soc_swlm_tcl_attach() - attach the TCL resources for the software
 *			      latency manager.
 * @soc: Datapath global soc handle
 *
 * Returns: QDF_STATUS
 */
static inline QDF_STATUS dp_soc_swlm_tcl_attach(struct dp_soc *soc)
{
	struct dp_swlm *swlm = &soc->swlm;
	int i;

	swlm->params.rx_traffic_thresh = DP_SWLM_TCL_RX_TRAFFIC_THRESH;
	swlm->params.tx_traffic_thresh = DP_SWLM_TCL_TX_TRAFFIC_THRESH;
	swlm->params.sampling_time = DP_SWLM_TCL_TRAFFIC_SAMPLING_TIME;
	swlm->params.time_flush_thresh = DP_SWLM_TCL_TIME_FLUSH_THRESH;
	swlm->params.tx_thresh_multiplier = DP_SWLM_TCL_TX_THRESH_MULTIPLIER;
	swlm->params.tx_pkt_thresh = DP_SWLM_TCL_TX_PKT_THRESH;

	for (i = 0; i < soc->num_tcl_data_rings; i++) {
		swlm->params.tcl[i].soc = soc;
		swlm->params.tcl[i].ring_id = i;
		swlm->params.tcl[i].bytes_flush_thresh = 0;
		swlm->params.tcl[i].mon_win_ts =
			qdf_get_log_timestamp_usecs();
		swlm->params.tcl[i].mon_bytes_flush_cnt = 0;
		swlm->params.tcl[i].mon_timer_flush_cnt = 0;
		swlm->params.tcl[i].coalesce_disable = 0;
		swlm->params.tcl[i].consec_timer_dom_cnt = 0;
		swlm->params.tcl[i].consec_timer_flush_cnt = 0;
		swlm->params.tcl[i].coalesce_last_dis_ts = 0;
		qdf_timer_init(soc->osdev,
			       &swlm->params.tcl[i].flush_timer,
			       dp_swlm_tcl_flush_timer,
			       (void *)&swlm->params.tcl[i],
			       QDF_TIMER_TYPE_WAKE_APPS);
	}

	return QDF_STATUS_SUCCESS;
}

/**
 * dp_soc_swlm_tcl_detach() - detach the TCL resources for the software
 *			      latency manager.
 * @swlm: SWLM data pointer
 * @ring_id: TCL ring id
 *
 * Returns: QDF_STATUS
 */
static inline QDF_STATUS dp_soc_swlm_tcl_detach(struct dp_swlm *swlm,
						uint8_t ring_id)
{
	qdf_timer_stop(&swlm->params.tcl[ring_id].flush_timer);
	qdf_timer_free(&swlm->params.tcl[ring_id].flush_timer);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS dp_soc_swlm_attach(struct dp_soc *soc)
{
	struct wlan_cfg_dp_soc_ctxt *cfg = soc->wlan_cfg_ctx;
	struct dp_swlm *swlm = &soc->swlm;
	QDF_STATUS ret;

	/* Check if it is enabled in the INI */
	if (!wlan_cfg_is_swlm_enabled(cfg)) {
		dp_err("SWLM feature is disabled");
		swlm->is_init = false;
		swlm->is_enabled = false;
		return QDF_STATUS_E_NOSUPPORT;
	}

	swlm->ops = &dp_latency_mgr_ops;

	ret = dp_soc_swlm_tcl_attach(soc);
	if (QDF_IS_STATUS_ERROR(ret))
		goto swlm_tcl_setup_fail;

	swlm->is_init = true;
	swlm->is_enabled = true;

	return QDF_STATUS_SUCCESS;

swlm_tcl_setup_fail:
	swlm->is_enabled = false;
	return ret;
}

QDF_STATUS dp_soc_swlm_detach(struct dp_soc *soc)
{
	struct dp_swlm *swlm = &soc->swlm;
	QDF_STATUS ret;
	int i;

	if (!swlm->is_enabled)
		return QDF_STATUS_SUCCESS;

	swlm->is_enabled = false;

	for (i = 0; i < soc->num_tcl_data_rings; i++) {
		ret = dp_soc_swlm_tcl_detach(swlm, i);
		if (QDF_IS_STATUS_ERROR(ret))
			return ret;
	}

	swlm->ops = NULL;

	return QDF_STATUS_SUCCESS;
}
#endif /* WLAN_DP_FEATURE_SW_LATENCY_MGR */
