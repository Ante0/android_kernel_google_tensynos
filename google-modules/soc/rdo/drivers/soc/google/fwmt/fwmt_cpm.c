// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/kernel.h>
#include <soc/google/goog_cpm_service_ids.h>
#include <soc/google/goog_mba_cpm_iface.h>

#include "fwmt_driver.h"
#include "fwmt_service.h"

#define FWMT_MBA_REQ_TIMEOUT_MS 3000

/**
 * fwmt_cpm_send_req - Sends a FWMT request to CPM.
 * @dev: The FWMT device.
 * @msg: The FWMT MBA message structure to be sent.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int fwmt_cpm_send_req(struct fwmt_dev *dev, struct fwmt_mba_msg *msg)
{
	struct fwmt_dev_cpm *cdev = container_of(dev, struct fwmt_dev_cpm, dev);
	struct cpm_iface_payload req_payload = { 0 };
	struct cpm_iface_req req = { 0 };
	int ret;

	memcpy(&req_payload.payload, msg, sizeof(*msg));

	req.msg_type = REQUEST_MSG;
	req.req_msg = &req_payload;
	req.resp_msg = &req_payload;
	req.dst_id = CPM_COMMON_FWMT_SERVICE;
	req.tout_ms = FWMT_MBA_REQ_TIMEOUT_MS;

	/* Write memory barrier to sync previous write with MBA recipient. */
	wmb();

	ret = cpm_send_message(cdev->iface, &req);

	/* Read memory barrier to sync buffer contents with the kernel. */
	rmb();

	return ret;
}
