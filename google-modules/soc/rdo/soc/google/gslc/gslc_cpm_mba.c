// SPDX-License-Identifier: GPL-2.0-only
/*
 * AP-CPM mailbox interface for the GSLC
 *
 * Copyright (C) 2021 Google LLC.
 */

#include <linux/mailbox_client.h>
#include <linux/mutex.h>
#include <linux/wait.h>

#include "gslc_cpm_mba.h"
#include "gslc_platform.h"

#define GSLC_CPM_MBA_RX_TX_TIMEOUT_MS (3000)

static void gslc_cpm_mba_rx_callback(u32 context, void *msg, void *priv_data)
{
	struct gslc_dev *gslc_dev = priv_data;
	struct cpm_iface_payload *msg_payload = msg;

	dev_info(gslc_dev->dev, "mbox rx callback type: %d\n",
		 goog_mba_q_xport_get_type(&msg_payload->header));
}

static inline void gslc_cpm_mba_fill_req(const struct gslc_mba_raw_msg *req,
					 u32 *data, size_t data_len)
{
	int idx;

	for (idx = 0; idx < data_len; idx++)
		data[idx] = cpu_to_le32(req->raw_data[idx]);
}

static int gslc_cpm_mba_process_resp(struct gslc_dev *gslc_dev,
				     u32 *resp_data, size_t resp_data_len,
				     struct gslc_mba_raw_msg *resp,
				     u8 req_cmd)
{
	struct device *dev = gslc_dev->dev;
	u8 resp_cmd;
	int i;

	for (i = 0; i < resp_data_len; ++i)
		resp->raw_data[i] = le32_to_cpu(resp_data[i]);

	resp_cmd = FIELD_GET(GSLC_MBA_CMD_MASK, resp->raw_data[0]);
	if (req_cmd != resp_cmd) {
		dev_err(dev,
			"Mailbox command mismatch Request:%d Response:%d\n",
			req_cmd, resp_cmd);

		return -EBADMSG;
	}

	return 0;
}

/**
 * gslc_cpm_mba_send_req_blocking() - Sends a mailbox request message and wait
 * until either the response is received or the configured timeout in the
 * mailbox channel expires.
 * @gslc_dev:	The GSLC platform device.
 * @req:	The GSLC req to be sent as the payload.
 * @resp:	The buffer to fill in the received response.
 *
 * Return:  0 on success, negative on error
 */
int gslc_cpm_mba_send_req_blocking(struct gslc_dev *gslc_dev,
				   const struct gslc_mba_raw_msg *req,
				   struct gslc_mba_raw_msg *resp)
{
	int ret = 0;
	struct device *dev = gslc_dev->dev;
	struct gslc_cpm_mba *cpm_mba = gslc_dev->cpm_mba;
	u8 req_cmd = FIELD_GET(GSLC_MBA_CMD_MASK, req->raw_data[0]);
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req cpm_req;

	mutex_lock(&cpm_mba->mutex);
	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = GSLC_CPM_MBA_RX_TX_TIMEOUT_MS;
	cpm_req.dst_id = cpm_mba->remote_ch;

	/* Setup the message payload. */
	gslc_cpm_mba_fill_req(req, req_msg.payload, GOOG_MBA_PAYLOAD_SIZE);

	/*
	 * Send the message and wait until the response is received.
	 * This is temporary for debugfs testing.
	 * TODO(b/211258390) Process responses as they come in without blocking
	 * for regular use cases.
	 */
	ret = cpm_send_message(cpm_mba->client, &cpm_req);
	if (ret < 0) {
		dev_err(dev, "Send cpm message failed (%d)\n", ret);
		goto unlock_mutex;
	}

	ret = gslc_cpm_mba_process_resp(gslc_dev, resp_msg.payload, GOOG_MBA_PAYLOAD_SIZE,
					resp, req_cmd);
	if (ret)
		goto unlock_mutex;

unlock_mutex:
	mutex_unlock(&cpm_mba->mutex);
	return ret;
}

static int __gslc_cpm_mba_init(struct gslc_dev *gslc_dev)
{
	struct device *dev = gslc_dev->dev;
	struct gslc_cpm_mba *cpm_mba = gslc_dev->cpm_mba;

	cpm_mba->client = cpm_iface_request_client(dev, MBOX_SERVICE_ID_GSLC,
						   gslc_cpm_mba_rx_callback, gslc_dev);
	if (IS_ERR(cpm_mba->client)) {
		int ret = PTR_ERR(cpm_mba->client);

		if (ret == -EPROBE_DEFER)
			dev_dbg(dev, "cpm interface not ready. Try again later\n");
		else
			dev_err(dev, "Failed to request cpm client err: %d\n", ret);

		return ret;
	}

	return 0;
}

/**
 * gslc_cpm_mba_init() - Initializes the AP-CPM mailbox channel for GSLC.
 * @gslc_dev:	The GSLC platform device.
 *
 * Return:	0 on success, negative on error
 */
int gslc_cpm_mba_init(struct gslc_dev *gslc_dev)
{
	struct device *dev = gslc_dev->dev;
	struct gslc_cpm_mba *cpm_mba = gslc_dev->cpm_mba;
	int ret = 0;

	mutex_init(&cpm_mba->mutex);

	ret = of_property_read_u32(dev->of_node, "mba-dest-channel",
				   &cpm_mba->remote_ch);
	if (ret < 0) {
		dev_err(dev, "Failed to read mba-dest-channel.\n");
		return ret;
	}

	return __gslc_cpm_mba_init(gslc_dev);
}

static inline void gslc_mbox_free(struct gslc_cpm_mba *cpm_mba)
{
	if (!IS_ERR_OR_NULL(cpm_mba->client))
		cpm_iface_free_client(cpm_mba->client);
}

/**
 * gslc_cpm_mba_deinit() - Frees the mailbox channel.
 * @gslc_dev:	The gslc device.
 */
void gslc_cpm_mba_deinit(struct gslc_dev *gslc_dev)
{
	struct gslc_cpm_mba *cpm_mba = gslc_dev->cpm_mba;

	mutex_lock(&cpm_mba->mutex);
	gslc_mbox_free(cpm_mba);
	mutex_unlock(&cpm_mba->mutex);
}
