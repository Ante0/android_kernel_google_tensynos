// SPDX-License-Identifier: GPL-2.0-only
/*
 * Partition handling for GSLC client drivers
 *
 * Copyright (C) 2021 Google LLC.
 */
#include <soc/google/pt.h>
#include "gslc_cpm_mba.h"
#include "gslc_partition.h"

/** gslc_client_partition_req() - Sends a mailbox request message and waits
 * until either the response is received or the configured timeout in the
 * mailbox channel expires.
 * @gslc_dev:	The GSLC platform device.
 * @req:	The GSLC req to be sent as the payload.
 *
 * Return:  Valid partition ID on success, PT_PTID_INVALID on error
 */
int gslc_client_partition_req(struct gslc_dev *gslc_dev,
			      const struct gslc_mba_raw_msg *req)
{
	struct device *dev = gslc_dev->dev;
	struct gslc_partition_resp resp = { 0 };
	int ret = 0;

	if (!req) {
		dev_err(dev, "Invalid mailbox request\n");
		return PT_PTID_INVALID;
	}

	ret = gslc_cpm_mba_send_req_blocking(gslc_dev,
					     (const struct gslc_mba_raw_msg *)req,
					     (struct gslc_mba_raw_msg *)&resp);
	if (ret < 0) {
		dev_err(dev, "Mailbox communication failed with err %d\n", ret);
		return PT_PTID_INVALID;
	}

	dev_dbg(dev, "PID received: %d\n", resp.pid);
	return resp.pid;
}
