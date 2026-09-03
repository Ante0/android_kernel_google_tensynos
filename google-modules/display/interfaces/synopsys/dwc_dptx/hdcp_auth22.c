// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/trusty/trusty_ipc.h>

#include "dptx.h"
#include "hdcp.h"
#include "regmaps/ctrl_fields.h"
#include "teeif.h"

#define HDCP22_TIMEOUT_MS (5000)

#define DP_RXCAPS_HDCP_CAPABLE         (0x1 << 1)
#define DP_RXCAPS_HDCP_VERSION_2       (0x2)

#define HDCP_CAPS_BYTE_LEN              (3)

#define HDCP_SYSEXCEPT_LINK_INTEGRITY_FAILED (161)
#define HDCP_SYSEXCEPT_RVLIST_COMPLETE_NOTIFY (174)
#define HDCP_SYSEXCEPT_HPD_LOW (176)
#define HDCP_AKE_COMPLETE (179)
#define HDCP_CAP_NOTIFY (181)
#define HDCP_SYSEXCEPT_CSM_NOTIFY (185)

#define HDCP_INVALID_APP_STATE             (-16)
#define HDCP_AKE_SEND_CERT_FAILED         (-577)
#define HDCP_AKE_GET_KM_FAILED            (-578)
#define HDCP_AKE_RECV_SEND_H_PRIME_FAILED (-579)
#define HDCP_AKE_RECV_PAIRING_INFO_FAILED (-580)
#define HDCP_AKE_LC_RECV_L_PRIME_FAILED   (-582)
#define HDCP_AKE_SEND_RCV_ID_LIST_FAILED  (-584)
#define HDCP_AKE_RECV_CSM_INFO_FAILED     (-586)
#define HDCP_AKE_DONE_FAILED              (-587)

static int print_esm_history(struct dptx *dptx, uint32_t eid, uint32_t oob)
{
	if (eid == 0)
		return 0;

	uint32_t num = eid >> 8;
	uint32_t err = eid & 0xFF;

	switch (err) {
	case HDCP_SYSEXCEPT_LINK_INTEGRITY_FAILED:
		dptx_err(dptx, "[HDCP %d] Link Integrity Failure detected [%x]\n",
			num, oob);
		break;
	case HDCP_SYSEXCEPT_HPD_LOW:
		dptx_err(dptx, "[HDCP %d] DP Disconnected [%x]\n", num, oob);
		break;
	case HDCP_SYSEXCEPT_CSM_NOTIFY:
	case HDCP_SYSEXCEPT_RVLIST_COMPLETE_NOTIFY:
		dptx_info(dptx, "[HDCP %d] CSM Update event handled in TZ\n",
			num);
		break;
	case HDCP_AKE_COMPLETE:
		dptx_info(dptx, "[HDCP %d] AKE Complete\n", num);
		break;
	case HDCP_CAP_NOTIFY:
		dptx_info(dptx, "[HDCP %d] CAP detect done\n", num);
		break;
	default:
		dptx_info(dptx, "[HDCP %d] Unknown notification received (%d)\n",
			num, err);
		break;
	}

	switch (oob) {
	case HDCP_AKE_RECV_SEND_H_PRIME_FAILED:
		dptx_err(dptx, "[HDCP %d] H PRIME Verification Failed\n",
			num);
		break;
	case HDCP_AKE_SEND_CERT_FAILED:
		dptx_err(dptx, "[HDCP %d] AKE Send Cert Failed\n", num);
		break;
	case HDCP_AKE_LC_RECV_L_PRIME_FAILED:
		dptx_err(dptx, "[HDCP %d] LC L PRIME Failed\n", num);
		break;
	case HDCP_AKE_RECV_PAIRING_INFO_FAILED:
		dptx_err(dptx, "[HDCP %d] Receive Pairing Info Failed\n", num);
		break;
	case HDCP_INVALID_APP_STATE:
		dptx_err(dptx, "[HDCP %d] Invalid App State\n", num);
		break;
	case HDCP_AKE_GET_KM_FAILED:
		dptx_err(dptx, "[HDCP %d] KM Failed\n", num);
		break;
	case HDCP_AKE_SEND_RCV_ID_LIST_FAILED:
		dptx_err(dptx, "[HDCP %d] Send RCV ID List Failed\n", num);
		break;
	case HDCP_AKE_RECV_CSM_INFO_FAILED:
		dptx_err(dptx, "[HDCP %d] Recv CSM Info Failed\n", num);
		break;
	case HDCP_AKE_DONE_FAILED:
		dptx_err(dptx, "[HDCP %d] AKE Done Failed\n", num);
		break;
	case 0:
		break;
	default:
		dptx_info(dptx, "[HDCP %d] Unprocessed OOB received [%x]\n",
			num, oob);
		break;
	}

	return 0;
}

static int run_esm_monitor(struct dptx *dptx)
{
	uint32_t history[NUM_HDCP_AUTH_RESP];

	int rc = hdcp_tee_monitor(dptx, history);

	if (rc)
		return rc;

	for (size_t i = 0; i < NUM_HDCP_AUTH_RESP; i += 2)
		print_esm_history(dptx, history[i], history[i + 1]);

	if (hdcp_get_auth_state(dptx) == HDCP2_AUTH_DONE &&
	    dptx_read_regfield(dptx,
		dptx->ctrl_fields->field_hdcp2_re_authentication_req)) {
		dptx_err(dptx, "HDCP Re-Authentication Requested\n");
		dptx_write_regfield(dptx,
			dptx->ctrl_fields->field_hdcp2_re_authentication_req, 1);
		hdcp_set_auth_state(dptx, HDCP_AUTH_REAUTH);
	}

	for (size_t i = 0; i < NUM_HDCP_AUTH_RESP; i += 2) {
		uint32_t err = history[i] & 0xFF;
		uint32_t oob = history[i + 1];

		switch (err) {
		case HDCP_SYSEXCEPT_LINK_INTEGRITY_FAILED:
			hdcp_set_auth_state(dptx, HDCP_AUTH_IDLE);
			return -EFAULT;

		case HDCP_AKE_COMPLETE:
			if (oob == HDCP_AKE_SEND_RCV_ID_LIST_FAILED) {
				dptx_err(dptx, "RCV ID List Failure\n");
				hdcp_set_auth_state(dptx, HDCP_AUTH_REAUTH);
			}

			return oob ? -EFAULT : 0;
		}
	}

	return -EAGAIN;
}

static int hdcp_auth22(struct dptx *dptx)
{
	uint8_t rxcaps[HDCP_CAPS_BYTE_LEN];

	dptx_info(dptx, "HDCP22 Auth begin\n");

	int ret = dptx_read_bytes_from_dpcd(dptx,
		DP_HDCP_2_2_REG_RX_CAPS_OFFSET, rxcaps, sizeof(rxcaps));
	if (ret) {
		dptx_err(dptx, "check rx caps recv fail: ret(%d)\n", ret);
		return -EOPNOTSUPP;
	}

	if (!(rxcaps[2] & DP_RXCAPS_HDCP_CAPABLE) ||
	    rxcaps[0] != DP_RXCAPS_HDCP_VERSION_2) {
		dptx_err(dptx, "HDCP22 is not supported. rxcaps(0x%02x%02x%02x)\n",
			rxcaps[0], rxcaps[1], rxcaps[2]);
		return -EOPNOTSUPP;
	}

	if (hdcp_set_auth_state(dptx, HDCP2_AUTH_PROGRESS))
		return -EBUSY;

	ret = hdcp_tee_auth22_trigger(dptx);
	if (ret) {
		dptx_err(dptx, "AUTH22 Trigger failed (%d)\n", ret);
		return ret;
	}

	uint32_t intr;
	int monitor_ret = 0;

	for (size_t i = 0; i < HDCP22_TIMEOUT_MS; i += 20) {
		msleep(20);

		if (hdcp_get_auth_state(dptx) != HDCP2_AUTH_PROGRESS) {
			dptx_err(dptx, "HDCP22 Auth cancelled\n");
			hdcp_tee_esm_dump(dptx);
			return -ECANCELED;
		}

		monitor_ret = run_esm_monitor(dptx);
		if (monitor_ret != -EAGAIN)
			break;
	}

	if (monitor_ret == -EAGAIN) {
		dptx_err(dptx, "HDCP22 Timeout\n");
		hdcp_tee_esm_dump(dptx);
		return -ETIMEDOUT;
	}

	if (dptx_read_regfield(dptx,
		dptx->ctrl_fields->field_hdcp2_authentication_success) &&
	    dptx_read_regfield(dptx,
		dptx->ctrl_fields->field_hdcpengaged)) {
		dptx_info(dptx, "HDCP22 Success %d\n", ret);
		dptx_write_regfield(dptx,
		dptx->ctrl_fields->field_hdcp2_authentication_success, 1);
		return 0;
	}

	if (dptx_read_regfield(dptx,
		dptx->ctrl_fields->field_hdcp2_authentication_failed)) {
		dptx_info(dptx, "HDCP22 Failed %d\n", ret);
		dptx_write_regfield(dptx,
		dptx->ctrl_fields->field_hdcp2_authentication_failed, 1);
		hdcp_tee_esm_dump(dptx);
		return -EFAULT;
	}

	dptx_err(dptx, "HDCP22 Monitor Fail %d\n", ret);
	hdcp_tee_esm_dump(dptx);
	return -EFAULT;
}

static void run_hdcp_auth22_monitor(struct dptx *dptx)
{
	while (1) {
		int rc = run_esm_monitor(dptx);
		if (hdcp_get_auth_state(dptx) != HDCP2_AUTH_DONE ||
		   (rc != -EAGAIN && rc != 0)) {
			dptx_err(dptx, "HDCP22 Monitor Out (%d)\n", rc);
			return;
		}
		msleep(100);
	}
}

int run_hdcp_auth22(struct dptx *dptx)
{
	int ret;

	for (size_t i = 0; i < 5; ++i) {
		dptx_info(dptx, "Trying HDCP22...\n");
		ret = hdcp_auth22(dptx);

		if (ret == -EOPNOTSUPP)
			return ret;

		if (!ret) {
			if (hdcp_set_auth_state(dptx, HDCP2_AUTH_DONE))
				return -EBUSY;

			dptx_info(dptx, "HDCP22 Authentication Success\n");
			dptx->hdcp_dev.hdcp2_success_count++;
			run_hdcp_auth22_monitor(dptx);
			if (hdcp_get_auth_state(dptx) == HDCP_AUTH_ABORT)
				return 0;
		} else {
			if (hdcp_set_auth_state(dptx, HDCP_AUTH_IDLE))
				return -EBUSY;
			msleep(hdcp_delay_ms);
		}
	}

	dptx_info(dptx, "HDCP22 Authentication Failed.\n");
	return (hdcp_set_auth_state(dptx, HDCP_AUTH_IDLE)) ? -EBUSY : ret;
}
