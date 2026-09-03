// SPDX-License-Identifier: GPL-2.0-only
/*
 * thermal_cpm_mbox_helper provide helper function for thermal_cpm_mbox
 *
 * Copyright (c) 2024, Google LLC. All rights reserved.
 */
#include <linux/devm-helpers.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include "thermal_cpm_mbox_helper.h"
#include "thermal_cpm_mbox_mock.h"

int thermal_cpm_mbox_probe_helper(struct thermal_cpm_mbox_driver_data *drv_data)
{
	int ret;

	ret = thermal_cpm_mbox_parse_soc_data(drv_data);
	if (ret)
		return ret;

	ret = thermal_cpm_mbox_init(drv_data);
	if (ret)
		return ret;

	ret = thermal_cpm_mbox_parse_device_tree(drv_data);
	if (ret)
		return ret;

	thermal_cpm_mbox_init_notifier(drv_data);

	return 0;
}

int __thermal_cpm_send_mbox_req(struct thermal_cpm_mbox_driver_data *drv_data,
				union thermal_cpm_message *message,
				int *status)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_payload resp_msg;
	struct cpm_iface_req cpm_req;
	int ret;

	if (!drv_data)
		return -ENODEV;

	if (!message || !status)
		return -EINVAL;

	cpm_req.msg_type = REQUEST_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.resp_msg = &resp_msg;
	cpm_req.tout_ms = MBA_CLIENT_TX_TIMEOUT_MSEC;
	cpm_req.dst_id = drv_data->remote_ch;

	memcpy(req_msg.payload, message->data, sizeof(*message));

	dev_dbg(drv_data->dev, "msg: [0]:0x%08x [1]:0x%08x [2]:0x%08x\n",
		req_msg.payload[0], req_msg.payload[1], req_msg.payload[2]);

	/* Send the message */
	ret = thermal_cpm_mbox_send_request(drv_data, &cpm_req);
	if (ret < 0) {
		dev_err(drv_data->dev, "Send cpm message failed (%d)\n", ret);
		return ret;
	}

	memcpy(message->data, resp_msg.payload, sizeof(*message));

	*status = message->resp.stat;

	return 0;
}

int __thermal_cpm_send_mbox_msg(struct thermal_cpm_mbox_driver_data *drv_data,
				union thermal_cpm_message message)
{
	struct cpm_iface_payload req_msg;
	struct cpm_iface_req cpm_req;
	int ret;

	if (!drv_data)
		return -ENODEV;

	cpm_req.msg_type = ONEWAY_MSG;
	cpm_req.req_msg = &req_msg;
	cpm_req.tout_ms = MBA_CLIENT_TX_TIMEOUT_MSEC;
	cpm_req.dst_id = drv_data->remote_ch;

	memcpy(req_msg.payload, message.data, sizeof(message));

	dev_dbg(drv_data->dev, "msg: [0]:0x%08x [1]:0x%08x [2]:0x%08x\n",
		req_msg.payload[0], req_msg.payload[1], req_msg.payload[2]);

	/* Send the message */
	ret = thermal_cpm_mbox_send_message(drv_data, &cpm_req);
	if (ret < 0) {
		dev_err(drv_data->dev, "Send cpm message failed (%d)\n", ret);
		return ret;
	}

	return 0;
}

int __thermal_cpm_mbox_register_notification(struct thermal_cpm_mbox_driver_data *drv_data,
					     enum hw_dev_type type,
					     struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&drv_data->rx_work[type].notifier,
						nb);
}

void __thermal_cpm_mbox_unregister_notification(struct thermal_cpm_mbox_driver_data *drv_data,
						enum hw_dev_type type,
						struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&drv_data->rx_work[type].notifier,
					   nb);
}
