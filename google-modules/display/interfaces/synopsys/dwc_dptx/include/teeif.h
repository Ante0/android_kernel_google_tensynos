/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC
 */

#ifndef __DPTX_TEEIF_H__
#define __DPTX_TEEIF_H__

struct dptx;

enum hdcp_auth_cmd {
	HDCP_CMD_AUTH_RESP = (1U << 31),
	HDCP_CMD_AUTH13_TRIGGER = 1,
	HDCP_CMD_BOOT = 2,
	HDCP_CMD_AUTH22_TRIGGER = 3,
	HDCP_CMD_ESM_DUMP = 4,
	HDCP_CMD_GET_CP_LVL = 5,
	HDCP_CMD_ESM_MONITOR = 6,
	HDCP_CMD_ENCRYPTION_GET = 7,
	HDCP_CMD_CONNECT_INFO = 8,
};

void hdcp_tee_init(struct dptx *dptx);
int hdcp_tee_close(struct dptx *dptx);
int hdcp_tee_auth13_trigger(struct dptx *dptx);
int hdcp_tee_auth22_trigger(struct dptx *dptx);
int hdcp_tee_esm_dump(struct dptx *dptx);
int hdcp_tee_get_cp_level(struct dptx *dptx, uint32_t *requested_lvl);
int hdcp_tee_monitor(struct dptx *dptx, uint32_t *exceptions);
int hdcp_tee_check_protection(struct dptx *dptx, uint32_t *version);
int hdcp_tee_connect_info(struct dptx *dptx, bool connected);

#endif
