// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2025 Google LLC */

#include "aoc.h"
#include "aoss_ssr.h"
#include <soc/google/goog_mba_cpm_iface.h>
#include <soc/google/goog_cpm_service_ids.h>
#include <linux/completion.h>

#define MBOX_TIMEOUT_EMULATION_MULTIPLIER 30
#define MBOX_SEND_TIMEOUT_MS 10
#define MBOX_RECEIVE_TIMEOUT_MS 300

static void aoss_ssr_completion_callback(u32 context, void *msg,
					 void *priv_data)
{
	struct ssr_data_s *data = priv_data;
	struct cpm_iface_payload *cpm_msg = msg;

	dev_dbg(data->dev, "SSR callback %d %d %d\n", cpm_msg->payload[0],
		cpm_msg->payload[1], cpm_msg->payload[2]);

	data->result = cpm_msg->payload[2];
	complete(&data->ssr_resp_done);
}

struct ssr_data_s *aoss_ssr_init(struct aoc_prvdata *prvdata)
{
	struct device_node *np;
	u32 phandle;
	struct ssr_data_s *ssr_data_p =
		devm_kzalloc(prvdata->dev, sizeof(struct ssr_data_s), GFP_KERNEL);

	if (!ssr_data_p)
		return ERR_PTR(-ENOMEM);

	ssr_data_p->dev = prvdata->dev;

	/* Configure mailbox */
	ssr_data_p->mbx_send_timeout_ms = MBOX_SEND_TIMEOUT_MS;
	ssr_data_p->mbx_receive_timeout_ms = MBOX_RECEIVE_TIMEOUT_MS;

	/* Increase timeout if testing in emulation environment */
	of_property_read_u32(prvdata->dev->of_node, "power-controller", &phandle);
	np = of_find_node_by_phandle(phandle);
	if (of_property_present(np, "in_emulation")) {
		ssr_data_p->mbx_send_timeout_ms *=
			MBOX_TIMEOUT_EMULATION_MULTIPLIER;
		ssr_data_p->mbx_receive_timeout_ms *=
			MBOX_TIMEOUT_EMULATION_MULTIPLIER;
	}

	/* Get instance of CPM mailbox client */
	ssr_data_p->mb_client =
		cpm_iface_request_client(prvdata->dev, APC_COMMON_SERVICE_ID_SSR,
					 aoss_ssr_completion_callback, ssr_data_p);
	if (IS_ERR(ssr_data_p->mb_client)) {
		dev_err(prvdata->dev, "Failed to instantiate CPM mailbox client");
		return ERR_PTR(PTR_ERR(ssr_data_p->mb_client));
	}

	init_completion(&ssr_data_p->ssr_resp_done);

	return ssr_data_p;
}

static int handle_msg_resp(struct ssr_data_s *data, enum ssr_service_cmd cmd,
			   enum ssr_service_result res)
{
	if (res == SSR_SERVICE_RESULT_STARTED) {
		unsigned long timeout_ret;

		dev_dbg(data->dev, "SSR Started received for cmd %d, waiting for response", cmd);
		timeout_ret = wait_for_completion_timeout(
			&data->ssr_resp_done,
			msecs_to_jiffies(data->mbx_receive_timeout_ms));
		if (timeout_ret == 0) {
			dev_err(data->dev, "Timeout waiting for SSR msg (%d) completion\n", cmd);
			return -ETIMEDOUT;
		}

		res = data->result;
		if (res != SSR_SERVICE_RESULT_SUCCESS) {
			/* Error received after attempting execution of the cmd */
			dev_err(data->dev, "SSR completion cmd (%d) failure %d",
				cmd, res);
		}
	} else if (res != SSR_SERVICE_RESULT_SUCCESS) {
		/* Error received upon sending the message */
		dev_err(data->dev, "SSR Sent Cmd (%d) error %d", cmd, res);
	}

	switch (res) {
	case SSR_SERVICE_RESULT_SUCCESS:
		return 0;
	case SSR_SERVICE_RESULT_FAIL_TIMEOUT:
		return -ETIMEDOUT;
	case SSR_SERVICE_RESULT_FAIL_RESOURCE_NOT_READY:
		return -EBUSY;
	case SSR_SERVICE_RESULT_FAIL_INVALID_CMD:
		return -EINVAL;
	default:
		return -EIO;
	}
}

int aoss_ssr_send_command(struct ssr_data_s *ssr_data_p, enum ssr_service_cmd cmd, int payload)
{
	struct cpm_iface_req cpm_req;
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	int ret;

	if (!ssr_data_p)
		return -ENXIO;

	dev_dbg(ssr_data_p->dev, "%s: %d, payload: %d", __func__, cmd, payload);

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = ssr_data_p->mbx_send_timeout_ms;
	cpm_req.dst_id = CPM_COMMON_SSR_SERVICE;

	req_msg.payload[0] = SSR_SERVICE_RESOURCE_ID_AOSS;
	req_msg.payload[1] = cmd;
	req_msg.payload[2] = payload;

	reinit_completion(&ssr_data_p->ssr_resp_done);
	dev_dbg(ssr_data_p->dev, "Sending SSR command");
	ret = cpm_send_message(ssr_data_p->mb_client, &cpm_req);

	if (ret < 0) {
		dev_err(ssr_data_p->dev, "SSR cmd %d send failed ret (%d)\n", cmd, ret);
		return ret;
	}

	return handle_msg_resp(ssr_data_p, cmd, resp_msg.payload[2]);
}

void aoss_ssr_cleanup(struct ssr_data_s *ssr_data_p)
{
	if (ssr_data_p && ssr_data_p->mb_client)
		cpm_iface_free_client(ssr_data_p->mb_client);
}
