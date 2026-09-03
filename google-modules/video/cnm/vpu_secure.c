// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Codec3P video accelerator
 *
 * Copyright 2024 Google LLC.
 *
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/trusty/trusty.h>
#include <linux/trusty/trusty_ipc.h>
#include <linux/kernel.h>
#include "vpu_secure.h"
#include "vpu_priv.h"
#include "codec3p_secure.h"

#define VPU_SECURE_TIMEOUT_MS 10000

static void vpu_secure_on_event(void *data, int event)
{
	struct vpu_secure *secure = (struct vpu_secure *)data;

	switch (event) {
	case TIPC_CHANNEL_CONNECTED:
		pr_debug("TIPC channel connected\n");
		secure->connected = true;
		break;

	case TIPC_CHANNEL_DISCONNECTED:
		pr_debug("TIPC channel disconnected\n");
		secure->connected = false;
		break;

	default:
		pr_warn("Unrecognized IPC event %d\n", event);
		break;
	}

	/* Wake the originator thread */
	complete(&secure->done);
}

static struct tipc_msg_buf *vpu_secure_on_msg(void *data,
		struct tipc_msg_buf *rxbuf)
{
	struct vpu_secure *secure = (struct vpu_secure *)data;
	struct codec3p_secure_rsp rsp;

	/* Validate the response size */
	if (mb_avail_data(rxbuf) == sizeof(rsp)) {

		/* Copy it out, as we can't guarantee alignment */
		memcpy(&rsp, mb_get_data(rxbuf, sizeof(rsp)), sizeof(rsp));

		pr_debug("IPC command %d completed with result %d\n",
			 rsp.command, rsp.result);

		/* Propagate the response to the originator thread */
		secure->rsp_result  = rsp.result;
	} else {
		pr_err("Response size %zu != %zu, IPC will time out\n",
			mb_avail_data(rxbuf), sizeof(rsp));
	}

	/* Wake the originator thread */
	complete(&secure->done);

	/* Return the rxbuf for immediate recycle */
	return rxbuf;
}

static const struct tipc_chan_ops vpu_secure_ops = {
	.handle_msg = vpu_secure_on_msg,
	.handle_event = vpu_secure_on_event,
};

int vpu_secure_init(struct vpu_secure *secure)
{
	struct tipc_chan *chan;
	unsigned long remaining;
	int rc;

	init_completion(&secure->done);

	pr_info("Connecting to codec3p_secure trusty app\n");

	/* Create a TIPC channel */
	chan = tipc_create_channel(NULL, &vpu_secure_ops, secure);
	if (IS_ERR(chan)) {
		pr_err("tipc_create_channel() failed (%ld)\n", PTR_ERR(chan));
		return PTR_ERR(chan);
	}

	/* Connect the TIPC channel to the gpu_secure app's port */
	rc = tipc_chan_connect(chan, CODEC3P_SECURE_PORT_NAME);
	if (rc < 0) {
		pr_err("tipc_chan_connect(%s) failed (%d)\n", CODEC3P_SECURE_PORT_NAME, rc);
		goto err_connect;
	}

	/* wait for connection */
	remaining = wait_for_completion_timeout(&secure->done,
						msecs_to_jiffies(VPU_SECURE_TIMEOUT_MS));
	if (remaining > 0) {
		/* completed in time */
		if (!secure->connected) {
			pr_err("TIPC channel not connected\n");
			rc = -EINVAL;
			goto err_reply;
		}
	} else {
		pr_err("Timed out connecting to trusty app\n");
		rc = -ETIMEDOUT;
		goto err_reply;
	}

	secure->chan = chan;
	return 0;

err_reply:
	tipc_chan_shutdown(chan);
err_connect:
	tipc_chan_destroy(chan);
	return rc;
}

void vpu_secure_deinit(struct vpu_secure *secure)
{
	pr_info("Disconnecting from trusty app\n");

	if (secure->chan) {
		tipc_chan_shutdown(secure->chan);
		tipc_chan_destroy(secure->chan);
		secure->chan = NULL;
	}
}

static int vpu_secure_call(struct vpu_secure *secure,
		const struct codec3p_secure_req_base *req, size_t req_size)
{
	unsigned long remaining;
	int rc;
	struct tipc_msg_buf *txbuf;

	/* Get a TX buffer (allocating or recycling) */
	txbuf = tipc_chan_get_txbuf_timeout(secure->chan, VPU_SECURE_TIMEOUT_MS);
	if (IS_ERR(txbuf)) {
		rc = PTR_ERR(txbuf);
		pr_err("tipc_chan_get_txbuf_timeout() failed (%d)\n", rc);
		txbuf = NULL;
		goto done;
	}

	/* Ensure it is large enough for our request */
	if (mb_avail_space(txbuf) < req_size) {
		pr_err("tipc_chan_get_txbuf_timeout() returned buffer size(%zu) < req_size(%zu)\n",
			mb_avail_space(txbuf), req_size);
		rc = -ETXTBSY;
		goto done;
	}

	/* Copy in the request */
	memcpy(mb_put_data(txbuf, req_size), req, req_size);

	/* Clear the done flag before we enqueue */
	reinit_completion(&secure->done);
	secure->rsp_result = 0;

	/* Enqueue the TX buffer for transmission */
	rc = tipc_chan_queue_msg(secure->chan, txbuf);
	if (rc < 0) {
		pr_err("tipc_chan_queue_msg() failed (%d)\n", rc);
		goto done;
	}

	/* The IPC channel now owns the TX buffer, so don't put() it later */
	txbuf = NULL;

	/* Wait for the response */
	remaining = wait_for_completion_timeout(&secure->done,
			msecs_to_jiffies(VPU_SECURE_TIMEOUT_MS));

	if (remaining > 0) {
		/* Our result is the IPC result */
		rc = secure->rsp_result;
	} else {
		pr_err("Timed out waiting for response from trusty app\n");
		rc = -ETIMEDOUT;
	}
done:
	/* If we still have a TX buffer, recycle it */
	if (txbuf) {
		tipc_chan_put_txbuf(secure->chan, txbuf);
		txbuf = NULL;
	}

	return rc;
}

int vpu_secure_fw_prot(struct vpu_secure *secure,
		       const struct vpu_dmabuf_info *fw_buf,
		       uint32_t blob_size)
{
	struct codec3p_secure_prot_fw_req fw_req;

	memset(&fw_req, 0, sizeof(fw_req));
	fw_req.base.command = CODEC3P_SECURE_REQ_PROT_FW;
	fw_req.firmware.pa = fw_buf->pa;
	fw_req.firmware.size = fw_buf->size;
	fw_req.firmware.blob_size = blob_size;

	pr_debug("Sending REQ PROTECT FW to trusty app\n");
	return vpu_secure_call(secure, &fw_req.base, sizeof(fw_req));
}

int vpu_secure_fw_unprot(struct vpu_secure *secure)
{
	struct codec3p_secure_req_base req;

	memset(&req, 0, sizeof(req));
	req.command = CODEC3P_SECURE_REQ_UNPROT_FW;

	pr_debug("Sending REQ UNPROTECT FW to trusty app\n");
	return vpu_secure_call(secure, &req, sizeof(req));
}
