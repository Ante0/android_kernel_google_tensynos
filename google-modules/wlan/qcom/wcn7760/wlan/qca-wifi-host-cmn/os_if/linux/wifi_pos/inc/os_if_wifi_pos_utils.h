/*
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

/**
 * DOC: os_if_wifi_pos_utils.h
 * This file provide declaration of wifi_pos's os_if APIs
 */
#ifndef _OS_IF_WIFI_POS_UTILS_H_
#define _OS_IF_WIFI_POS_UTILS_H_

#include "qdf_types.h"
#include "qdf_status.h"
#include <wlan_objmgr_cmn.h>
#include "wifi_pos_public_struct.h"

#if defined(WIFI_POS_CONVERGED)
/**
 * struct wifi_pos_osif_ops - Wifi POS osif callbacks
 * @osif_initiate_pasn_cb: Callback to initiate PASN authentication
 * @osif_rtt_peer_meas_report_cb: Callback to send RTT peer measurement
 *                                results to userspace via cfg80211
 * @osif_pasn_peer_create_complete_cb: PASN peer create completion callback
 */
struct wifi_pos_osif_ops {
	QDF_STATUS (*osif_initiate_pasn_cb)(struct wlan_objmgr_vdev *vdev,
					    struct wlan_pasn_request *pasn_peer,
					    uint8_t num_pasn_peers,
					    bool is_initiate_pasn);
	QDF_STATUS (*osif_rtt_peer_meas_report_cb)(
				struct wlan_objmgr_psoc *psoc,
				struct wifi_pos_peer_meas_report *report);
	QDF_STATUS (*osif_pasn_peer_create_complete_cb)
				(struct wlan_objmgr_vdev *vdev,
				 void *cookie, uint8_t peer_create_status);
};
#endif

#if defined(WIFI_POS_CONVERGED) && defined(WLAN_FEATURE_RTT_11AZ_SUPPORT)

/**
 * osif_wifi_pos_register_ops() - Register Wifi-Pos module OS_IF callbacks
 * @psoc: Pointer to PSOC obj
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
osif_wifi_pos_register_ops(struct wlan_objmgr_psoc *psoc);

/**
 * osif_wifi_pos_deregister_ops  - Deregister the wifi pos OS_IF callbacks
 * @psoc: Pointer to PSOC obj
 *
 * Return: QDF_STATUS
 */
QDF_STATUS
osif_wifi_pos_deregister_ops(struct wlan_objmgr_psoc *psoc);
#else
static inline QDF_STATUS
osif_wifi_pos_register_ops(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}

static inline QDF_STATUS
osif_wifi_pos_deregister_ops(struct wlan_objmgr_psoc *psoc)
{
	return QDF_STATUS_SUCCESS;
}
#endif /* WIFI_POS_CONVERGED */
#endif /* _OS_IF_WIFI_POS_UTILS_H_ */
