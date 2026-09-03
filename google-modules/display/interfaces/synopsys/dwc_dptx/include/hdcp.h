/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC
 */

#ifndef __DPTX_HDCP_H__
#define __DPTX_HDCP_H__

#include <linux/types.h>

struct dptx;

#define NUM_HDCP_AUTH_RESP (8)
#define MIN_HDCP_AUTH_RSP_SIZE (12)

struct hdcp_auth_rsp {
	uint32_t cmd;
	int32_t err;
	uint32_t resp[NUM_HDCP_AUTH_RESP];
};

struct hdcp_tz_chan_ctx {
	struct tipc_chan *chan;
	struct mutex rsp_lock;
	struct completion reply_comp;
	struct hdcp_auth_rsp rsp;
	uint32_t rsp_len;
};

enum auth_state {
	HDCP_AUTH_RESET = 0,
	HDCP_AUTH_IDLE,
	HDCP1_AUTH_PROGRESS,
	HDCP1_AUTH_DONE,
	HDCP2_AUTH_PROGRESS,
	HDCP2_AUTH_DONE,
	HDCP_AUTH_ABORT,
	HDCP_AUTH_SHUTDOWN,
	HDCP_AUTH_REAUTH,
};

struct hdcp_device {
	struct delayed_work hdcp_work;
	struct delayed_work hdcp_cp_work;
	ktime_t connect_time;
	bool is_dpcd12_plus;
	enum auth_state hdcp_auth_state;
	uint32_t drm_cp_status;
	uint32_t session_id;
	uint32_t link_failure_cnt;

	struct hdcp_tz_chan_ctx hdcp_ta_ctx;

	/* HDCP Telemetry */
	uint32_t hdcp2_success_count;
	uint32_t hdcp2_fallback_count;
	uint32_t hdcp2_fail_count;
	uint32_t hdcp1_success_count;
	uint32_t hdcp1_fail_count;
	uint32_t hdcp0_count;
};

extern unsigned long hdcp_delay_ms;

int run_hdcp_auth13(struct dptx *dptx);
int run_hdcp_auth22(struct dptx *dptx);

int dptx_hdcp_probe(struct dptx *dptx_dev);
void dptx_hdcp_auth_state_probe(struct dptx *dptx_dev);
void dptx_hdcp_remove(struct dptx *dptx_dev);
void dptx_hdcp_connect(struct dptx *dptx_dev, bool is_dpcd12_plus);
void dptx_hdcp_disconnect(struct dptx *dptx_dev);
void dptx_hdcp_physical_disconnect(struct dptx *dptx_dev);

int handle_cp_irq_set(struct dptx *dptx_dev);
int hdcp_set_auth_state(struct dptx *dptx, enum auth_state state);
enum auth_state hdcp_get_auth_state(struct dptx *dptx);
const char *get_auth_state_str(uint32_t state);
void dptx_hdcp_dump_regs(struct dptx *dptx);

#endif
