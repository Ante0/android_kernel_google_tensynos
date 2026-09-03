// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for NOA core header file
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Kevin Hu <kaiwenhu@google.com>
 */

#ifdef linux
#include "common/core.h"
#else /* linux */
#include "common/core.h"
#endif /* linux */

/* C++ in NOA didn't support array designators, so here we are */
const char *noa_port_id_to_name(uint8_t port_id)
{
	switch (port_id) {
	case SESSION_PORT_WIFI_FW:
		return "ncp_wlan_fw";
	case SESSION_PORT_WIFI_SW:
		return "apc_wlan_sw";
	case SESSION_PORT_MODEM_FW:
		return "ncp_modem_fw";
	case SESSION_PORT_MODEM_SW:
		return "apc_modem_sw";
	case SESSION_PORT_NETENGINE:
		return "nep_netengine";
	default:
		break;
	}
	return "UNKNOWN";
}

const char *noa_mode_to_str(uint32_t mode)
{
	switch (mode) {
	case NOAD_MODE_DATA:
		return "data";
	case NOAD_MODE_FEEDBACK:
		return "feedback";
	default:
		break;
	}
	return "UNKNOWN";
}

const char *noa_reason_to_str(uint16_t reason)
{
	switch (reason) {
	case FWD_REASON_FEEDTHROUGH:
		return "feed_through";
	case FWD_REASON_FALLBACK:
		return "fallback";
	case FWD_REASON_NETENGINE:
		return "netengine";
	case FWD_REASON_VPN:
		return "vpn";
	default:
		break;
	}
	return "UNKNOWN";
}

size_t noa_desc_bytes(int type)
{
	switch (type) {
	case NOA_DESC_BASIC:
		return NOA_DESC_BASIC_BYTE;
	case NOA_DESC_MODEM_LASSEN:
		return NOA_DESC_MODEM_LASSEN_BYTE;
	case NOA_DESC_WLAN_TX_BRCM:
		return NOA_DESC_WLAN_TX_BRCM_BYTE;
	case NOA_DESC_WLAN_TX_QCA:
		return NOA_DESC_WLAN_TX_QCA_BYTE;
	case NOA_DESC_NETENG_PKT_FLOW:
		return NOA_DESC_NETENG_PKT_FLOW_BYTE;
	case NOA_DESC_VPN_RX:
		return NOA_DESC_VPN_RX_BYTE;
	case NOA_DESC_MODEM_TX_MTK:
		return NOA_DESC_MODEM_TX_MTK_BYTE;
	case NOA_DESC_MODEM_RX_MTK:
		return NOA_DESC_MODEM_RX_MTK_BYTE;
	case NOA_DESC_MODEM_RX_MTK_MSG_PD:
		return NOA_DESC_MODEM_RX_MTK_MSG_PD_BYTE;
	case NOA_DESC_MODEM_RX_MTK_PD:
		return NOA_DESC_MODEM_RX_MTK_PD_BYTE;
	default:
		break;
	}
	return NOA_DESC_MAX_BYTE;
}
