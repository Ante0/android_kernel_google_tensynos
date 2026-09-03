// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/trusty/trusty_ipc.h>

#include "dbg.h"
#include "dptx.h"
#include "teeif.h"

#define TZ_CON_TIMEOUT  5000
#define TZ_BUF_TIMEOUT 10000
#define TZ_MSG_TIMEOUT 10000
#define HDCP_TA_PORT "com.android.trusty.hdcp.auth"

struct hdcp_auth_req {
	uint32_t cmd;
	int32_t arg;
	uint32_t data;
};

static struct tipc_msg_buf *tz_srv_handle_msg(void *data,
					      struct tipc_msg_buf *rxbuf)
{
	struct hdcp_tz_chan_ctx *ctx = data;
	size_t len;
	size_t payload_num;

	len = mb_avail_data(rxbuf);

	if (len >= MIN_HDCP_AUTH_RSP_SIZE)
		memcpy(&ctx->rsp, mb_get_data(rxbuf, len), len);

	ctx->rsp_len = len;
	complete(&ctx->reply_comp);
	return rxbuf;
}

static void tz_srv_handle_event(void *data, int event)
{
	struct hdcp_tz_chan_ctx *ctx = data;

	complete(&ctx->reply_comp);
}

static const struct tipc_chan_ops tz_srv_ops = {
	.handle_msg = tz_srv_handle_msg,
	.handle_event = tz_srv_handle_event,
};

static int hdcp_tee_open(struct dptx *dptx)
{
	int ret = 0;
	struct tipc_chan *chan;

	struct hdcp_tz_chan_ctx *ctx = &dptx->hdcp_dev.hdcp_ta_ctx;

	if (ctx->chan) {
		dptx_dbg(dptx, "HCI is already connected\n");
		return 0;
	}

	chan = tipc_create_channel(NULL, &tz_srv_ops, ctx);
	if (IS_ERR(chan)) {
		dptx_err(dptx, "TZ: failed (%ld) to create chan\n",
			 PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	reinit_completion(&ctx->reply_comp);

	ret = tipc_chan_connect(chan, HDCP_TA_PORT);
	if (ret < 0) {
		dptx_err(dptx, "TZ: failed (%d) to connect\n", ret);
		tipc_chan_destroy(chan);
		return ret;
	}

	ctx->chan = chan;
	ret = wait_for_completion_timeout(&ctx->reply_comp,
					  msecs_to_jiffies(TZ_CON_TIMEOUT));
	if (ret <= 0) {
		ret = (!ret) ? -ETIMEDOUT : ret;
		dptx_err(dptx, "TZ: failed (%d) to wait for connect\n", ret);
		hdcp_tee_close(dptx);
		return ret;
	}

	return 0;
}

static int hdcp_tee_comm_xchg_internal(struct dptx *dptx, uint32_t cmd,
	int32_t arg, uint32_t *rsp, size_t rsp_size)
{
	int ret;
	struct tipc_msg_buf *txbuf;
	struct hdcp_auth_req auth_req;

	ret = hdcp_tee_open(dptx);
	if (ret)
		return ret;

	struct hdcp_tz_chan_ctx *ctx = &dptx->hdcp_dev.hdcp_ta_ctx;

	txbuf = tipc_chan_get_txbuf_timeout(ctx->chan, TZ_BUF_TIMEOUT);
	if (IS_ERR(txbuf)) {
		dptx_err(dptx, "TZ: failed (%ld) to get txbuf\n", PTR_ERR(txbuf));
		return PTR_ERR(txbuf);
	}

	auth_req.cmd = cmd;
	auth_req.arg = arg;
	memcpy(mb_put_data(txbuf, sizeof(struct hdcp_auth_req)),
	       &auth_req, sizeof(struct hdcp_auth_req));

	reinit_completion(&ctx->reply_comp);

	ret = tipc_chan_queue_msg(ctx->chan, txbuf);
	if (ret < 0) {
		dptx_err(dptx, "TZ: failed(%d) to queue msg\n", ret);
		tipc_chan_put_txbuf(ctx->chan, txbuf);
		hdcp_tee_close(dptx);
		return ret;
	}

	ret = wait_for_completion_timeout(&ctx->reply_comp,
					  msecs_to_jiffies(TZ_MSG_TIMEOUT));
	if (ret <= 0) {
		ret = (!ret) ? -ETIMEDOUT : ret;
		dptx_err(dptx, "TZ: failed (%d) to wait for reply\n", ret);
		hdcp_tee_close(dptx);
		return ret;
	}

	if (ctx->rsp.cmd != (cmd | HDCP_CMD_AUTH_RESP)) {
		dptx_err(dptx, "TZ: hdcp had an unexpected rsp cmd (%x vs %x)",
			 ctx->rsp.cmd, cmd | HDCP_CMD_AUTH_RESP);
		return -EIO;
	}

	if (ctx->rsp.err) {
		dptx_err(dptx, "TZ: hdcp had an unexpected rsp err (%d)",
			 ctx->rsp.err);
		return -EIO;
	}

	if (rsp) {
		if (ctx->rsp_len - sizeof(ctx->rsp.cmd) - sizeof(ctx->rsp.err)
			!= rsp_size) {
			dptx_err(dptx, "TZ: hdcp had an unexpected response len (%d)",
				ctx->rsp_len);
			return -EIO;
		}
		memcpy(rsp, ctx->rsp.resp, rsp_size);
	}

	return 0;
}

static int hdcp_tee_comm_xchg(struct dptx *dptx, uint32_t cmd, int32_t arg,
	int32_t *rsp, size_t rsp_size)
{
	int ret;
	int retries = 2;

	mutex_lock(&dptx->hdcp_dev.hdcp_ta_ctx.rsp_lock);
	while (retries) {
		retries--;
		ret = hdcp_tee_comm_xchg_internal(dptx, cmd, arg, rsp, rsp_size);
		if (!ret) {
			mutex_unlock(&dptx->hdcp_dev.hdcp_ta_ctx.rsp_lock);
			return 0;
		}
	}
	mutex_unlock(&dptx->hdcp_dev.hdcp_ta_ctx.rsp_lock);

	return ret;
}

void hdcp_tee_init(struct dptx *dptx)
{
	init_completion(&dptx->hdcp_dev.hdcp_ta_ctx.reply_comp);
	mutex_init(&dptx->hdcp_dev.hdcp_ta_ctx.rsp_lock);
}

int hdcp_tee_close(struct dptx *dptx)
{
	if (!dptx->hdcp_dev.hdcp_ta_ctx.chan) {
		dptx_info(dptx, "HCI is already disconnected\n");
		return 0;
	}

	tipc_chan_shutdown(dptx->hdcp_dev.hdcp_ta_ctx.chan);
	tipc_chan_destroy(dptx->hdcp_dev.hdcp_ta_ctx.chan);
	dptx->hdcp_dev.hdcp_ta_ctx.chan = NULL;
	return 0;
}

int hdcp_tee_auth13_trigger(struct dptx *dptx)
{
	return hdcp_tee_comm_xchg(dptx, HDCP_CMD_AUTH13_TRIGGER, 0, NULL, 0);
}

int hdcp_tee_auth22_trigger(struct dptx *dptx)
{
	int rc = hdcp_tee_comm_xchg(dptx, HDCP_CMD_BOOT, 0, NULL, 0);

	if (rc)
		return rc;

	return hdcp_tee_comm_xchg(dptx, HDCP_CMD_AUTH22_TRIGGER, 0, NULL, 0);
}

int hdcp_tee_esm_dump(struct dptx *dptx)
{
	dptx_info(dptx, "ESM Dump for log[%d]\n", ++dptx->hdcp_dev.session_id);
	return hdcp_tee_comm_xchg(dptx, HDCP_CMD_ESM_DUMP,
		dptx->hdcp_dev.session_id, NULL, 0);
}

int hdcp_tee_get_cp_level(struct dptx *dptx, uint32_t *requested_lvl)
{
	return hdcp_tee_comm_xchg(dptx, HDCP_CMD_GET_CP_LVL, 0, requested_lvl,
		sizeof(uint32_t));
}

int hdcp_tee_monitor(struct dptx *dptx, uint32_t *exceptions)
{
	return hdcp_tee_comm_xchg(dptx, HDCP_CMD_ESM_MONITOR, 0, exceptions,
		sizeof(uint32_t) * NUM_HDCP_AUTH_RESP);
}

int hdcp_tee_check_protection(struct dptx *dptx, uint32_t *version)
{
	return hdcp_tee_comm_xchg(dptx, HDCP_CMD_ENCRYPTION_GET, 0, version,
		sizeof(uint32_t));
}

int hdcp_tee_connect_info(struct dptx *dptx, bool connected)
{
	return hdcp_tee_comm_xchg(dptx, HDCP_CMD_CONNECT_INFO, connected, NULL,
		0);
}
