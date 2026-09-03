// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/trusty/trusty_ipc.h>

#include "dptx.h"
#include "regmaps/ctrl_fields.h"
#include "teeif.h"

#define HDCP13_TIMEOUT_MS (5000)

static int hdcp_auth13(struct dptx *dptx, bool is_dpcd12_plus)
{
	uint8_t bcaps;
	int ret = dptx_read_bytes_from_dpcd(dptx, DP_AUX_HDCP_BCAPS, &bcaps, 1);

	if (ret) {
		dptx_err(dptx, "BCaps Read failure (%d)\n", ret);
		return -EOPNOTSUPP;
	}

	if (!(bcaps & DP_BCAPS_HDCP_CAPABLE)) {
		dptx_err(dptx, "HDCP13 is not supported\n");
		return -EOPNOTSUPP;
	}

	ret = hdcp_tee_auth13_trigger(dptx);
	if (ret) {
		dptx_err(dptx, "AUTH13 Trigger failed (%d)\n", ret);
		return ret;
	}

	dptx_write_reg(dptx, dptx->regs[DPTX], HDCPAPIINTCLR, 0xFFFFFFFF);
	if (hdcp_set_auth_state(dptx, HDCP1_AUTH_PROGRESS))
		return -EBUSY;

	uint32_t hdcp_irq = 0;
	bool err = 0;

	for (size_t i = 0; i < HDCP13_TIMEOUT_MS; i += 20) {
		if (hdcp_get_auth_state(dptx) != HDCP1_AUTH_PROGRESS) {
			dptx_err(dptx, "HDCP13 Auth cancelled\n");
			return -ECANCELED;
		}

		if (dptx_read_regfield(dptx,
			dptx->ctrl_fields->field_hdcp_engaged_stat)) {
			dptx_info(dptx, "HDCP13 Success (%d)\n",
				dptx_read_regfield(dptx, dptx->ctrl_fields->field_hdcpengaged));
			return 0;
		}

		if (dptx_read_regfield(dptx,
			dptx->ctrl_fields->field_hdcp_failed_stat)) {
			err = 1;
			break;
		}

		msleep(20);
	}

	if (err) {
		dptx_err(dptx, "HDCP13 Failed (%d)\n",
			dptx_read_regfield(dptx, dptx->ctrl_fields->field_hdcpengaged));
		return -EFAULT;
	}

	dptx_err(dptx, "HDCP13 Timeout (%d)\n",
		dptx_read_regfield(dptx, dptx->ctrl_fields->field_hdcpengaged));
	dptx_hdcp_dump_regs(dptx);
	return -ETIMEDOUT;
}

int run_hdcp_auth13(struct dptx *dptx)
{
	int ret = -EIO;

	dptx_info(dptx, "Trying HDCP13...\n");

	ret = hdcp_auth13(dptx, dptx->hdcp_dev.is_dpcd12_plus);
	if (ret == -EOPNOTSUPP)
		return ret;

	if (!ret) {
		if (hdcp_set_auth_state(dptx, HDCP1_AUTH_DONE))
			return -EBUSY;

		dptx_info(dptx, "HDCP13 Authentication Success\n");
		return 0;
	}

	dptx_info(dptx, "HDCP13 Authentication Failed.\n");
	return (hdcp_set_auth_state(dptx, HDCP_AUTH_IDLE)) ? -EBUSY : ret;
}
