// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 */

#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/trusty/trusty_ipc.h>

#include "google_dpa_secure.h"
#include "google_dpa_secure_ipc.h"

enum google_dpa_secure_channel_state {
	GOOGLE_DPA_SECURE_CHANNEL_DISCONNECTED,
	GOOGLE_DPA_SECURE_CHANNEL_CONNECTED,
	GOOGLE_DPA_SECURE_CHANNEL_STALE,
};

/**
 * struct google_dpa_secure_channel - The secure channel to talk to DPA TZ App
 *
 * This DPA secure channel allow only one outstanding request at a time and lock
 * is for avoiding multiple outstanding requests. Since trusty-ipc module only
 * provides non-blocking APIs, this secure channel struct uses completion to
 * convert non-blocking calls to blocking calls.
 *
 * @dev:		device that allocated this channel.
 * @chan:		Trusty IPC channel
 * @state:		state of the DPA secure channel
 * @connect_done	completion to wait for establishing the connection
 * @lock:		lock for serializing communication with DPA TZ App
 * @msg_done:		completion to wait for the response from DPA TZ App
 * @rsp:		response from TZ App of the current request
 * @return_code		return code of the current request
 */
struct google_dpa_secure_channel {
	struct device *dev;
	struct tipc_chan *chan;
	enum google_dpa_secure_channel_state state;
	struct completion connect_done;

	struct mutex lock;
	struct completion msg_done;

	struct dpa_secure_ipc_rsp rsp;
	int return_code;
};

/*
 * Google DPA Secure IPC API Versions:
 *
 * | Command        | Current Version | Notes                              |
 * |----------------|-----------------|------------------------------------|
 * | DPA_IMG_AUTH   | V2              | V1 will be deprecated (b/426465018)|
 * | DPA_IMG_UNLOAD | V1              |                                    |
 * | DPA_BOOT       | V1              |                                    |
 */
#define GOOGLE_DPA_SECURE_IPC_V1 1
#define GOOGLE_DPA_SECURE_IPC_V2 2
#define GOOGLE_DPA_TIPC_TIMEOUT_MSEC 1000

static void google_dpa_secure_handle_event(void *priv, int event)
{
	struct google_dpa_secure_channel *secure_chan = priv;
	struct device *dev = secure_chan->dev;

	switch (event) {
	case TIPC_CHANNEL_CONNECTED:
		dev_dbg(dev, "Connected to the DPA secure app.\n");
		secure_chan->state = GOOGLE_DPA_SECURE_CHANNEL_CONNECTED;
		break;
	case TIPC_CHANNEL_DISCONNECTED:
		secure_chan->state = GOOGLE_DPA_SECURE_CHANNEL_DISCONNECTED;
		dev_err(dev, "Channel with DPA secure app is disconnected.\n");
		break;
	case TIPC_CHANNEL_SHUTDOWN:
		secure_chan->state = GOOGLE_DPA_SECURE_CHANNEL_STALE;
		dev_err(dev, "Trusty IPC has been shut down.\n");
		break;
	default:
		secure_chan->state = GOOGLE_DPA_SECURE_CHANNEL_STALE;
		dev_err(dev, "Received an unknown Trusty IPC event %d\n", event);
		break;
	}

	complete(&secure_chan->connect_done);
}

static struct tipc_msg_buf *google_dpa_secure_handle_msg(void *priv, struct tipc_msg_buf *rxbuf)
{
	struct google_dpa_secure_channel *secure_chan = priv;
	struct device *dev = secure_chan->dev;
	struct dpa_secure_ipc_rsp *rsp = &secure_chan->rsp;

	if (mb_avail_data(rxbuf) == sizeof(*rsp)) {
		memcpy(rsp, mb_get_data(rxbuf, sizeof(rsp)), sizeof(rsp));
		dev_dbg(dev, "DPA TZ App IPC command %d completed with result %d\n", rsp->command,
			rsp->result);

		/* propagate the result to the initiator thread */
		secure_chan->return_code = 0;
	} else {
		dev_err(dev, "Response size %zu != %zu\n", mb_avail_data(rxbuf), sizeof(rsp));
		secure_chan->return_code = -EINVAL;
	}

	complete(&secure_chan->msg_done);

	return rxbuf;
}

static const struct tipc_chan_ops google_dpa_secure_ops = {
	.handle_event = google_dpa_secure_handle_event,
	.handle_msg = google_dpa_secure_handle_msg,
};

static struct google_dpa_secure_channel *google_dpa_create_secure_channel(struct device *dev)
{
	struct google_dpa_secure_channel *secure_chan;
	void *err;

	secure_chan = kzalloc(sizeof(*secure_chan), GFP_KERNEL);
	if (!secure_chan)
		return ERR_PTR(-ENOMEM);
	secure_chan->state = GOOGLE_DPA_SECURE_CHANNEL_DISCONNECTED;
	init_completion(&secure_chan->connect_done);
	init_completion(&secure_chan->msg_done);
	mutex_init(&secure_chan->lock);
	secure_chan->dev = dev;

	/*
	 * Get the default Trusty IPC device by passing NULL.
	 * trusty-virtio module does not provide an API to get TIPC device from
	 * the device tree node.
	 */
	secure_chan->chan = tipc_create_channel(NULL, &google_dpa_secure_ops, secure_chan);
	if (IS_ERR(secure_chan->chan)) {
		dev_err(dev, "Failed to create a Trusty IPC channel (%ld).\n",
			PTR_ERR(secure_chan->chan));
		err = (void *)(secure_chan->chan);
		goto free_secure_chan;
	}

	return secure_chan;

free_secure_chan:
	kfree(secure_chan);
	return err;
}

int google_dpa_secure_shutdown_connection(struct google_dpa_secure_channel *secure_chan)
{
	int ret;

	ret = tipc_chan_shutdown(secure_chan->chan);
	if (ret) {
		dev_err(secure_chan->dev, "Failed to shutdown the secure connectino.\n");
		return ret;
	}

	tipc_chan_destroy(secure_chan->chan);
	kfree(secure_chan);

	return 0;
}

struct google_dpa_secure_channel *google_dpa_secure_connect(struct device *dev)
{
	struct google_dpa_secure_channel *secure_chan;
	int ret, remaining;

	secure_chan = google_dpa_create_secure_channel(dev);
	if (IS_ERR(secure_chan))
		return secure_chan;

	ret = tipc_chan_connect(secure_chan->chan, GOOGLE_DPA_SECURE_IPC_PORT_NAME);
	if (ret) {
		dev_err(dev, "Failed to connect to the DPA secure app (%d)\n", ret);
		goto shutdown_secure_chan;
	}

	remaining = wait_for_completion_timeout(&secure_chan->connect_done,
						msecs_to_jiffies(GOOGLE_DPA_TIPC_TIMEOUT_MSEC));
	if (remaining > 0) {
		if (secure_chan->state != GOOGLE_DPA_SECURE_CHANNEL_CONNECTED) {
			dev_err(dev, "Failed to connect to the DPA secure app (%d)\n", ret);
			ret = -EIO;
			goto shutdown_secure_chan;
		}
	} else {
		dev_err(dev, "Timed out connecting to the DPA secure app.\n");
		ret = -ETIMEDOUT;
		goto shutdown_secure_chan;
	}

	return secure_chan;

shutdown_secure_chan:
	google_dpa_secure_shutdown_connection(secure_chan);

	return ERR_PTR(ret);
}

static int google_dpa_secure_send_cmd(struct google_dpa_secure_channel *secure_chan,
				      const struct dpa_secure_ipc_req *req,
				      struct dpa_secure_ipc_rsp *rsp)
{
	struct device *dev = secure_chan->dev;
	struct tipc_msg_buf *txbuf;
	int ret, remaining;

	ret = mutex_lock_interruptible(&secure_chan->lock);
	if (ret)
		return ret;

	if (secure_chan->state != GOOGLE_DPA_SECURE_CHANNEL_CONNECTED) {
		dev_err(dev,
			"Tried to send a message to DPA secure app but the connection is not established.\n");
		goto unlock;
	}

	txbuf = tipc_chan_get_txbuf_timeout(secure_chan->chan, GOOGLE_DPA_TIPC_TIMEOUT_MSEC);
	if (IS_ERR(txbuf)) {
		dev_err(dev, "Failed to get Tx buf for DPA secure channel (%ld)\n", PTR_ERR(txbuf));
		ret = -EINVAL;
		goto unlock;
	}
	if (mb_avail_space(txbuf) < sizeof(*req)) {
		dev_err(dev, "The Tx buf size for DPA secure channel is too small (%zu < %zu)\n",
			mb_avail_space(txbuf), sizeof(*req));
		ret = -EINVAL;
		goto tipc_put_txbuf;
	}

	memcpy(mb_put_data(txbuf, sizeof(*req)), req, sizeof(*req));

	reinit_completion(&secure_chan->msg_done);

	ret = tipc_chan_queue_msg(secure_chan->chan, txbuf);
	if (ret < 0) {
		dev_err(dev, "Failed to send message to DPA secure app (%d)\n", ret);
		goto tipc_put_txbuf;
	}

	/* The IPC channel now owns the TX buffer, so don't put() it later */
	txbuf = NULL;

	/* Wait for the response */
	remaining =
		wait_for_completion_timeout(&secure_chan->msg_done, GOOGLE_DPA_TIPC_TIMEOUT_MSEC);

	if (remaining == 0) {
		dev_err(dev, "Timed out waiting for response from DPA secure app\n");
		ret = -ETIMEDOUT;
	} else if (secure_chan->rsp.command != req->base.command) {
		dev_err(dev, "Received a response with an unexpected command (%d != %d)\n",
			secure_chan->rsp.command, req->base.command);
		ret = -EINVAL;
	} else {
		dev_dbg(dev, "Received response from DPA TZ App (command=%d, result=%d)\n\n",
			secure_chan->rsp.command, secure_chan->rsp.result);
		memcpy(rsp, &secure_chan->rsp, sizeof(*rsp));
		ret = secure_chan->return_code;
	}

tipc_put_txbuf:
	if (txbuf)
		tipc_chan_put_txbuf(secure_chan->chan, txbuf);
unlock:
	mutex_unlock(&secure_chan->lock);

	return ret;
}

static void google_dpa_init_secure_req(struct dpa_secure_ipc_req *req, u32 version,
				       enum dpa_secure_ipc_command command)
{
	memset(req, 0, sizeof(*req));
	req->base.version = version;
	req->base.command = command;
}

// TODO(b/426465018): Remove this function after the migration
int google_dpa_secure_img_auth_v1(struct google_dpa_secure_channel *secure_chan,
				  const struct google_dpa_secure_fw_image *ncp_image,
				  const struct google_dpa_secure_fw_image *nep_image)
{
	struct dpa_secure_ipc_req req;
	struct dpa_secure_ipc_rsp rsp;
	int ret;

	google_dpa_init_secure_req(&req, GOOGLE_DPA_SECURE_IPC_V1, DPA_IMG_AUTH);
	req.img_auth_req.ncp_img.pa = ncp_image->pa;
	req.img_auth_req.ncp_img.size = ncp_image->size;
	req.img_auth_req.nep_img.pa = nep_image->pa;
	req.img_auth_req.nep_img.size = nep_image->size;

	ret = google_dpa_secure_send_cmd(secure_chan, &req, &rsp);

	if (ret)
		return ret;

	return rsp.result;
}

int google_dpa_secure_img_auth_v2(struct google_dpa_secure_channel *secure_chan,
				  const struct google_dpa_secure_fw_image *dpa_image)
{
	struct dpa_secure_ipc_req req;
	struct dpa_secure_ipc_rsp rsp;
	int ret;

	google_dpa_init_secure_req(&req, GOOGLE_DPA_SECURE_IPC_V2, DPA_IMG_AUTH);
	req.img_auth_req_v2.dpa_img.pa = dpa_image->pa;
	req.img_auth_req_v2.dpa_img.size = dpa_image->size;

	ret = google_dpa_secure_send_cmd(secure_chan, &req, &rsp);

	if (ret)
		return ret;

	return rsp.result;
}

int google_dpa_secure_img_unload(struct google_dpa_secure_channel *secure_chan)
{
	struct dpa_secure_ipc_req req;
	struct dpa_secure_ipc_rsp rsp;
	int ret;

	google_dpa_init_secure_req(&req, GOOGLE_DPA_SECURE_IPC_V1, DPA_IMG_UNLOAD);

	ret = google_dpa_secure_send_cmd(secure_chan, &req, &rsp);

	if (ret)
		return ret;

	return rsp.result;
}

int google_dpa_secure_boot(struct google_dpa_secure_channel *secure_chan)
{
	struct dpa_secure_ipc_req req;
	struct dpa_secure_ipc_rsp rsp;
	int ret;

	google_dpa_init_secure_req(&req, GOOGLE_DPA_SECURE_IPC_V1, DPA_BOOT);

	ret = google_dpa_secure_send_cmd(secure_chan, &req, &rsp);

	if (ret)
		return ret;

	return rsp.result;
}
