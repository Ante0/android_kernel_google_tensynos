// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <drm/drm_mode.h>
#include <drm/display/drm_hdcp_helper.h>

#include "dptx.h"
#include "regmaps/ctrl_fields.h"
#include "hdcp.h"

#define DP_TX_HDCPCFG_ENABLE_HDCP    (1 << 1)
#define DP_TX_HDCPCFG_ENABLE_HDCP_13 (1 << 2)
#define DP_TX_HDCPCFG_DPCD12PLUS     (1 << 7)

static const char auth_state_str[10][20] = {
	"HDCP_AUTH_RESET",
	"HDCP_AUTH_IDLE",
	"HDCP1_AUTH_PROGRESS",
	"HDCP1_AUTH_DONE",
	"HDCP2_AUTH_PROGRESS",
	"HDCP2_AUTH_DONE",
	"HDCP_AUTH_ABORT",
	"HDCP_AUTH_SHUTDOWN",
	"HDCP_AUTH_REAUTH",
};

const char *get_auth_state_str(uint32_t state)
{
	return auth_state_str[state];
}

enum auth_state hdcp_get_auth_state(struct dptx *dptx)
{
	return dptx->hdcp_dev.hdcp_auth_state;
}

static void hdcp_cp_worker(struct work_struct *work)
{
	struct hdcp_device *hdcp_dev =
		container_of(work, struct hdcp_device, hdcp_cp_work.work);
	struct dptx *dptx =
		container_of(hdcp_dev, struct dptx, hdcp_dev);
	struct drm_connector *connector = dptx->connector;
	uint32_t drm_cp_status = hdcp_dev->drm_cp_status;

	dptx_info(dptx, "dp_hdcp_update_cp to %d\n", drm_cp_status);
	drm_modeset_lock(&connector->dev->mode_config.connection_mutex, NULL);
	drm_hdcp_update_content_protection(connector, drm_cp_status);
	drm_modeset_unlock(&connector->dev->mode_config.connection_mutex);

}

static void disengage_hdcp_auth(struct dptx *dptx)
{
	dptx_write_regfield(dptx, dptx->ctrl_fields->field_enable_hdcp, 0);
	dptx_write_regfield(dptx, dptx->ctrl_fields->field_enable_hdcp_13, 0);
	dptx_write_regfield(dptx, dptx->ctrl_fields->field_dpcd12plus, 0);
}

static void engage_hdcp_auth13(struct dptx *dptx)
{
	uint32_t value = DP_TX_HDCPCFG_ENABLE_HDCP_13 | DP_TX_HDCPCFG_ENABLE_HDCP;

	if (dptx->hdcp_dev.is_dpcd12_plus)
		value |= DP_TX_HDCPCFG_DPCD12PLUS;
	dptx_write_reg(dptx, dptx->regs[DPTX], HDCPCFG, value);
}

static void engage_hdcp_auth22(struct dptx *dptx)
{
	uint32_t value = dptx_read_reg(dptx, dptx->regs[DPTX], HDCPCFG);

	value &= ~DP_TX_HDCPCFG_ENABLE_HDCP_13;
	value |= DP_TX_HDCPCFG_ENABLE_HDCP;
	value |= DP_TX_HDCPCFG_DPCD12PLUS;
	dptx_write_reg(dptx, dptx->regs[DPTX], HDCPCFG, value);
}

static void hdcp_transition_auth_state(struct dptx *dptx,
	enum auth_state new_state)
{
	enum auth_state old_state = dptx->hdcp_dev.hdcp_auth_state;

	if (old_state == new_state)
		return;

	dptx_info(dptx, "set auth state from %s to %s\n",
		get_auth_state_str(old_state), get_auth_state_str(new_state));

	if (old_state == HDCP1_AUTH_DONE || old_state == HDCP2_AUTH_DONE) {
		dptx->hdcp_dev.drm_cp_status = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		schedule_delayed_work(&dptx->hdcp_dev.hdcp_cp_work, 0);
	}

	dptx->hdcp_dev.hdcp_auth_state = new_state;

	if (new_state == HDCP_AUTH_IDLE || new_state == HDCP_AUTH_ABORT
	    || new_state == HDCP_AUTH_SHUTDOWN)
		disengage_hdcp_auth(dptx);
	if (new_state == HDCP1_AUTH_PROGRESS)
		engage_hdcp_auth13(dptx);
	if (new_state == HDCP2_AUTH_PROGRESS)
		engage_hdcp_auth22(dptx);

	if (new_state == HDCP1_AUTH_DONE || new_state == HDCP2_AUTH_DONE) {
		dptx->hdcp_dev.drm_cp_status = DRM_MODE_CONTENT_PROTECTION_ENABLED;
		schedule_delayed_work(&dptx->hdcp_dev.hdcp_cp_work, 0);
	}
}

int hdcp_set_auth_state(struct dptx *dptx, enum auth_state state)
{
	if (state == HDCP_AUTH_ABORT || state == HDCP_AUTH_SHUTDOWN) {
		hdcp_transition_auth_state(dptx, state);
		return 0;
	}

	bool is_allowed = false;

	switch (dptx->hdcp_dev.hdcp_auth_state) {
	case HDCP_AUTH_RESET:
	case HDCP_AUTH_IDLE:
		is_allowed = (state == HDCP2_AUTH_PROGRESS) ||
			     (state == HDCP1_AUTH_PROGRESS);
		break;
	case HDCP2_AUTH_PROGRESS:
		is_allowed = (state == HDCP2_AUTH_DONE) ||
			     (state == HDCP_AUTH_IDLE);
		break;
	case HDCP1_AUTH_PROGRESS:
		is_allowed = (state == HDCP1_AUTH_DONE) ||
			     (state == HDCP_AUTH_IDLE);
		break;
	case HDCP1_AUTH_DONE:
	case HDCP2_AUTH_DONE:
		is_allowed = (state == HDCP_AUTH_IDLE) ||
			     (state == HDCP_AUTH_REAUTH);
		break;
	case HDCP_AUTH_REAUTH:
		is_allowed = (state == HDCP2_AUTH_PROGRESS) ||
			     (state == HDCP1_AUTH_PROGRESS);
		break;
	case HDCP_AUTH_ABORT:
		is_allowed = (state == HDCP_AUTH_RESET);
		break;
	case HDCP_AUTH_SHUTDOWN:
		is_allowed = false;
	}

	if (!is_allowed) {
		dptx_info(dptx, "set auth state from %s to %s failed\n",
			get_auth_state_str(dptx->hdcp_dev.hdcp_auth_state),
			get_auth_state_str(state));
		return -1;
	}

	hdcp_transition_auth_state(dptx, state);
	return 0;
}

void dptx_hdcp_auth_state_probe(struct dptx *dptx)
{
	INIT_DELAYED_WORK(&dptx->hdcp_dev.hdcp_cp_work, hdcp_cp_worker);
}
