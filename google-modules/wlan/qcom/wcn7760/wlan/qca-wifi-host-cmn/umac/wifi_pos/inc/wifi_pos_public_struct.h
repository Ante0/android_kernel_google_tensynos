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
 * DOC: wifi_pos_public_struct.h
 * This file declares public structures of wifi positioning component
 */
#ifndef _WIFI_POS_PUBLIC_STRUCT_H_
#define _WIFI_POS_PUBLIC_STRUCT_H_

/* Include files */
#include "qdf_types.h"
#include "qdf_status.h"
#include "qdf_trace.h"
#include <wlan_cmn.h>
#include "wlan_cmn_ieee80211.h"

#define WLAN_MAX_11AZ_PEERS 16
#define WLAN_11AZ_MAX_PASSPHRASE_LEN 64
#define WLAN_PASN_MAX_COOKIE_LEN 255
#define PASN_PEER_CREATE_TIMEOUT_MS 4000

/**
 * enum wifi_pos_pasn_peer_type  - PASN peer type
 * @WLAN_WIFI_POS_PASN_UNSECURE_PEER: Unsecure ranging peer
 * @WLAN_WIFI_POS_PASN_SECURE_PEER: Secure ranging peer
 * @WLAN_WIFI_POS_PASN_PEER_TYPE_MAX: Max peer type
 */
enum wifi_pos_pasn_peer_type {
	WLAN_WIFI_POS_PASN_UNSECURE_PEER,
	WLAN_WIFI_POS_PASN_SECURE_PEER,
	WLAN_WIFI_POS_PASN_PEER_TYPE_MAX,
};

/**
 * enum wifi_pos_pasn_peer_delete_actions  - Actions on receiving a peer
 * delete event for PASN peer
 * @WIFI_POS_PEER_DELETE_ACTION_ALREADY_DELETED: Peer is already deleted at
 * target. Cleanup the host objmgr peer.
 * @WIFI_POS_PEER_DELETE_ACTION_FLUSH_KEYS: Flush the derived keys for this
 * peer at userspace.
 */
enum wifi_pos_pasn_peer_delete_actions {
	WIFI_POS_PEER_DELETE_ACTION_ALREADY_DELETED = BIT(0),
	WIFI_POS_PEER_DELETE_ACTION_FLUSH_KEYS = BIT(1),
};

/**
 * enum wlan_responder_mode - RTT responder Modes
 * @RESPONDER_RTT_11MC_SUPPORTED: Responder supports 11mc ranging
 * @RESPONDER_RTT_11AZ_NTB_RANGING_SUPPORTED: Responder supports NTB ranging
 * @RESPONDER_RTT_11AZ_TB_RANGING_SUPPORTED: Responder supports TB ranging
 */
enum wlan_responder_mode {
	RESPONDER_RTT_11MC_SUPPORTED = 0,
	RESPONDER_RTT_11AZ_NTB_RANGING_SUPPORTED = 1,
	RESPONDER_RTT_11AZ_TB_RANGING_SUPPORTED = 2
};

/**
 * enum wifi_pos_pasn_msg_type - Wifi Pos PASN msg type
 * @WIFI_POS_NB_PASN_PEER_CREATE_REQ: Request type for creating PASN peer
 * @WIFI_POS_PASN_MSG_MAX: Placeholder for maximum value
 */
enum wifi_pos_pasn_msg_type {
	WIFI_POS_NB_PASN_PEER_CREATE_REQ = 0,
	WIFI_POS_PASN_MSG_MAX
};

#define WIFI_POS_IS_PEER_ALREADY_DELETED(flag) \
			((flag) & WIFI_POS_PEER_DELETE_ACTION_ALREADY_DELETED)
#define WIFI_POS_IS_FLUSH_KEYS_REQUIRED(flag) \
			((flag) & WIFI_POS_PEER_DELETE_ACTION_FLUSH_KEYS)

#define CFG_RESPONDER_11AZ_NTB_SUPPORT 0x1
#define CFG_RESPONDER_11AZ_TB_SUPPORT 0x2
#define CFG_RESPONDER_11AZ_MAX_SUPPORT (CFG_RESPONDER_11AZ_NTB_SUPPORT | \
		CFG_RESPONDER_11AZ_TB_SUPPORT)

/**
 * struct wlan_pasn_request - PASN peer create request data
 * @vdev_id: Vdev identifier
 * @psoc: Pointer to PSOC object
 * @peer_mac: Peer mac address
 * @peer_type: Peer type of enum wifi_pos_pasn_peer_type
 * @self_mac: Self mac address to be used for frame exchange & key
 * derivation
 * @force_self_mac_usage: If this flag is true, the supplicant
 * should use the provided self mac address
 * @is_ltf_keyseed_required: Is set LTF keyseed required
 * @password_len: Len of password array
 * @password: The password specified will be used to generate PMKID
 * @pmkid_len: Len of pmkid array
 * @pmkid: pmkid used for cached pmksa authentication
 * @cookie_len: Len of cookie array
 * @cookie: In case AP refused PASN temporarily, cookie act as token
 * in PASN rety
 * @comeback_after: u16 attribute, indicate that PASN can be tried
 * after that much time in case AP refused PASN temporarily
 * @control_flags: Control flags to indicate if its required to flush
 * the keys
 * @akm: used - should be either PASN or PASN + SAE
 * @cipher: Indicates the key cipher suite
 * @is_userspace_peer_create: flag to indicate if the peer create is initiated
 * by userspace.
 */
struct wlan_pasn_request {
	uint8_t vdev_id;
	struct wlan_objmgr_psoc *psoc;
	struct qdf_mac_addr peer_mac;
	enum wifi_pos_pasn_peer_type peer_type;
	struct qdf_mac_addr self_mac;
	bool force_self_mac_usage;
	bool is_ltf_keyseed_required;
	uint32_t password_len;
	uint8_t password[WLAN_11AZ_MAX_PASSPHRASE_LEN];
	uint32_t pmkid_len;
	uint8_t pmkid[PMKID_LEN];
	uint32_t cookie_len;
	uint8_t cookie[WLAN_PASN_MAX_COOKIE_LEN];
	uint16_t control_flags;
	uint32_t akm;
	uint32_t cipher;
	bool is_userspace_peer_create;
};

/**
 * struct wifi_pos_11az_context  - 11az Security context
 * @secure_peer_list: Mac address list of secure peers
 * @num_secure_peers: Total number of secure peers
 * @unsecure_peer_list: Mac address list of unsecure peers
 * @num_unsecure_peers: Total number of unsecure peers
 * @failed_peer_list: List of failed peers
 * @num_failed_peers: Total number of failed peers
 * @num_pending_peer_creation: Number of pending peer create commands for which
 * peer create confirmation is pending.
 */
struct wifi_pos_11az_context {
	struct wlan_pasn_request secure_peer_list[WLAN_MAX_11AZ_PEERS];
	uint8_t num_secure_peers;
	struct wlan_pasn_request unsecure_peer_list[WLAN_MAX_11AZ_PEERS];
	uint8_t num_unsecure_peers;
	struct qdf_mac_addr failed_peer_list[WLAN_MAX_11AZ_PEERS];
	uint8_t num_failed_peers;
	uint8_t num_pending_peer_creation;
};

/**
 * struct wifi_pos_vdev_priv_obj  - Wifi Pos module vdev private object
 * @pasn_context: 11az security peers context.
 * @num_pasn_peers: Total number of PASN peers
 * @is_delete_all_pasn_peer_in_progress: Delete all the VDEV PASN peers in
 * progress
 */
struct wifi_pos_vdev_priv_obj {
	struct wifi_pos_11az_context pasn_context;
	uint8_t num_pasn_peers;
	bool is_delete_all_pasn_peer_in_progress;
};

/**
 * enum wlan_pasn_auth_status_code  - PASN auth status code
 * @WLAN_PASN_AUTH_STATUS_SUCCESS: PASN auth is successful
 * @WLAN_PASN_AUTH_STATUS_PASN_FAILED: PASN authentication failed
 * @WLAN_PASN_AUTH_STATUS_PEER_CREATE_FAILED: PASN peer create confirm received
 * with failure status.
 * @WLAN_PASN_AUTH_STATUS_PEER_ALREADY_EXISTS: Peer already exists
 * @WLAN_PASN_AUTH_STATUS_HOST_INTERNAL_ERROR: WLAN driver internal error
 * @WLAN_PASN_AUTH_STATUS_PEER_COMEBACK: Ap rejected PASN+SAE temporarily
 */
enum wlan_pasn_auth_status_code {
	WLAN_PASN_AUTH_STATUS_SUCCESS = 0,
	WLAN_PASN_AUTH_STATUS_PASN_FAILED = 1,
	WLAN_PASN_AUTH_STATUS_PEER_CREATE_FAILED = 2,
	WLAN_PASN_AUTH_STATUS_PEER_ALREADY_EXISTS = 3,
	WLAN_PASN_AUTH_STATUS_HOST_INTERNAL_ERROR = 4,
	WLAN_PASN_AUTH_STATUS_PEER_COMEBACK = 5,
};

/**
 * enum wifi_pos_peer_meas_status - RTT peer measurement status
 * @WIFI_POS_RTT_PEER_MEAS_STATUS_OK: Measurement succeeded
 * @WIFI_POS_RTT_PEER_MEAS_STATUS_FAIL: Measurement failed
 */
enum wifi_pos_peer_meas_status {
	WIFI_POS_RTT_PEER_MEAS_STATUS_OK = 0,
	WIFI_POS_RTT_PEER_MEAS_STATUS_FAIL = 1,
};

/**
 * enum wifi_pos_peer_meas_type - RTT peer measurement type
 * @WIFI_POS_RTT_PEER_MEAS_TYPE_11AZ_NTB: 11az non-trigger based ranging
 * @WIFI_POS_RTT_PEER_MEAS_TYPE_11AZ_TB: 11az trigger based ranging
 */
enum wifi_pos_peer_meas_type {
	WIFI_POS_RTT_PEER_MEAS_TYPE_11AZ_NTB = 0,
	WIFI_POS_RTT_PEER_MEAS_TYPE_11AZ_TB = 1,
};

/**
 * struct wifi_pos_peer_meas_result - Host representation of RTT peer
 * measurement result received from firmware.
 * @peer_mac: Peer MAC address
 * @status: Measurement completion status
 * @final: Final result indication
 * @tb_ranging: TB ranging indication
 * @ntb_ranging: NTB ranging indication
 * @ap_tsf_valid: AP TSF validity indication
 * @is_delayed_lmr: Delayed LMR indication
 * @meas_type: Peer measurement type
 * @burst_idx: Current burst index
 * @burst_count: Total burst count
 * @burst_duration: Negotiated burst duration
 * @burst_period: Negotiated burst period
 * @ftms_per_burst: FTMs per burst
 * @rssi_avg: Average RSSI
 * @rssi_spread: RSSI spread
 * @rtt_avg: Average RTT
 * @rtt_variance: RTT variance
 * @rtt_spread: RTT spread
 * @dist_avg_mm: Average distance in millimeters
 * @dist_variance_mm: Distance variance in millimeters
 * @dist_spread_mm: Distance spread in millimeters
 * @min_time_between_meas: Negotiated minimum time between measurements
 * @max_time_between_meas: Negotiated maximum time between measurements
 * @num_tx_sts: Number of TX space-time streams
 * @num_rx_sts: Number of RX space-time streams
 * @tx_ltf_repetition_count: TX LTF repetition count
 * @rx_ltf_repetition_count: RX LTF repetition count
 * @availability_window_duration: Availability window duration
 * @nominal_time: Nominal time
 * @meas_per_aw: Number of measurements per availability window
 * @channel_bw: Reported channel bandwidth
 * @preamble: Reported preamble
 * @tx_mcs: Reported TX MCS
 * @tx_nss: Reported TX NSS
 * @tx_bw: Reported TX rate bandwidth
 * @tx_gi: Reported TX GI
 * @rx_mcs: Reported RX MCS
 * @rx_nss: Reported RX NSS
 * @rx_bw: Reported RX rate bandwidth
 * @rx_gi: Reported RX GI
 * @num_ftmr_successes: Number of FTMR successes
 * @num_ftmr_attempts: Number of FTMR attempts
 */
struct wifi_pos_peer_meas_result {
	struct qdf_mac_addr peer_mac;
	enum wifi_pos_peer_meas_status status;
	bool final;
	bool tb_ranging;
	bool ntb_ranging;
	bool ap_tsf_valid;
	bool is_delayed_lmr;
	enum wifi_pos_peer_meas_type meas_type;
	uint32_t burst_idx;
	uint32_t burst_count;
	uint32_t burst_duration;
	uint32_t burst_period;
	uint8_t  ftms_per_burst;
	uint32_t rssi_avg;
	uint32_t rssi_spread;
	uint64_t rtt_avg;
	uint64_t rtt_variance;
	uint64_t rtt_spread;
	uint64_t dist_avg_mm;
	uint64_t dist_variance_mm;
	uint64_t dist_spread_mm;
	uint32_t min_time_between_meas;
	uint32_t max_time_between_meas;
	uint8_t  num_tx_sts;
	uint8_t  num_rx_sts;
	uint32_t tx_ltf_repetition_count;
	uint32_t rx_ltf_repetition_count;
	uint32_t availability_window_duration;
	uint32_t nominal_time;
	uint32_t meas_per_aw;
	uint32_t channel_bw;
	uint32_t preamble;
	uint8_t tx_mcs;
	uint8_t tx_nss;
	uint8_t tx_bw;
	uint8_t tx_gi;
	uint8_t rx_mcs;
	uint8_t rx_nss;
	uint8_t rx_bw;
	uint8_t rx_gi;
	uint32_t num_ftmr_successes;
	uint32_t num_ftmr_attempts;
};

struct wifi_pos_peer_meas_report {
	uint32_t req_id;
	uint8_t vdev_id;
	uint8_t num_peers;
	struct wifi_pos_peer_meas_result
	peer_result[WLAN_MAX_11AZ_PEERS];
};

#define WLAN_PASN_MAX_COOKIE_LEN 255
/**
 * struct wlan_pasn_auth_status_peer_info - PASN authentication status peer
 * info
 * @peer_mac: Peer mac address
 * @self_mac: Self mac address
 * @cookie_len: length of cookie array
 * @cookie: Cookie value
 * @comeback_after: Comeback timer
 * @akm: akm suite
 * @cipher: cipher suite
 * @status: PASN auth status code
 */
struct wlan_pasn_auth_status_peer_info {
	struct qdf_mac_addr peer_mac;
	struct qdf_mac_addr self_mac;
	uint32_t cookie_len;
	uint8_t cookie[WLAN_PASN_MAX_COOKIE_LEN];
	uint16_t comeback_after;
	uint32_t akm;
	uint32_t cipher;
	enum wlan_pasn_auth_status_code status;
};

/**
 * struct wlan_pasn_auth_status  - PASN authentication status
 * @vdev_id:  vdev_id
 * @num_peers: Number of peers for which auth status is to be sent
 * @auth_status: Auth status details
 */
struct wlan_pasn_auth_status {
	uint8_t vdev_id;
	uint8_t num_peers;
	struct wlan_pasn_auth_status_peer_info auth_status[WLAN_MAX_11AZ_PEERS];
};

/**
 * struct wlan_wifi_pos_peer_priv_obj - WLAN wifi pos peer private object
 * @is_ltf_keyseed_required: Is LTF keyseed required for peer
 */
struct wlan_wifi_pos_peer_priv_obj {
	bool is_ltf_keyseed_required;
};

#if defined(WLAN_FEATURE_RTT_11AZ_SUPPORT)
/**
 * struct wmi_rtt_peer_meas_req_peer_params - Per-peer RTT measurement request
 * @dest_mac: Destination MAC address
 * @ch_freq: Channel frequency in MHz
 * @ch_freq_seg1: Channel center frequency segment 1
 * @ch_freq_seg2: Channel center frequency segment 2
 * @ch_width: Channel width
 * @ch_phymode: PHY mode
 * @report_ap_tsf: Report AP TSF flag
 * @pd_request: Proximity Detection request flag
 * @ftm_requested: FTM requested flag
 * @asap_mode: ASAP mode flag
 * @lci_req: LCI request flag
 * @loc_civic_req: Location civic request flag
 * @tb_ranging: TB ranging flag
 * @ntb_ranging: NTB ranging flag
 * @i2r_lmr_feedback: I2R LMR feedback flag
 * @rsta_role: RSTA role flag
 * @suppress_range_results: Suppress ranging results while reporting
 * @preamble: Preamble type (WMI_HOST_RATE_PREAMBLE)
 * @burst_period: Burst period
 * @num_burst_exp: Number of burst exponent
 * @burst_duration: Burst duration
 * @ftms_per_burst: FTMs per burst
 * @ftmr_retries: FTMR retries
 * @min_time_between_measurements: Min time between measurements (100 us units)
 * @max_time_between_measurements: Max time between measurements (10 ms units)
 * @nominal_time: Nominal time for Availability Window
 * @measurements_per_aw: Measurements per Availability Window
 * @aw_duration: Availability Window duration
 */
struct wmi_rtt_peer_meas_req_peer_params {
	uint8_t dest_mac[QDF_MAC_ADDR_SIZE];
	uint32_t ch_freq;
	uint32_t  ch_freq_seg1;
	uint32_t  ch_freq_seg2;
	enum phy_ch_width ch_width;
	enum wlan_phymode ch_phymode;

	uint8_t report_ap_tsf;
	uint8_t pd_request;
	uint8_t ftm_requested;
	uint8_t asap_mode;
	uint8_t lci_req;
	uint8_t loc_civic_req;
	uint8_t tb_ranging;
	uint8_t ntb_ranging;
	uint8_t i2r_lmr_feedback;
	uint8_t rsta_role;
	uint8_t suppress_range_results;
	uint32_t preamble;
	uint32_t burst_period;
	uint8_t num_burst_exp;
	uint8_t burst_duration;
	uint8_t ftms_per_burst;
	uint8_t ftmr_retries;
	uint32_t min_time_between_measurements;
	uint32_t max_time_between_measurements;
	uint16_t nominal_time;
	uint8_t measurements_per_aw;
	uint8_t aw_duration;
};

/**
 * struct wmi_rtt_peer_meas_req_cmd_params - RTT peer measurement request
 * @req_id: Unique request ID
 * @vdev_id: VDEV ID
 * @timeout: Timeout in ms (0 = no timeout)
 * @random_mac_addr: Randomized MAC address
 * @mac_addr_randomization: Whether MAC address randomization is enabled
 * @n_peers: Number of peers
 * @peers: Per-peer measurement parameters array (n_peers entries)
 */
struct wmi_rtt_peer_meas_req_cmd_params {
	uint32_t req_id;
	uint32_t vdev_id;
	uint32_t timeout;
	uint8_t random_mac_addr[QDF_MAC_ADDR_SIZE];
	bool mac_addr_randomization;
	uint32_t n_peers;
	struct wmi_rtt_peer_meas_req_peer_params *peers;
};
#endif /* WLAN_FEATURE_RTT_11AZ_SUPPORT */
#endif /* _WIFI_POS_PUBLIC_STRUCT_H_ */
