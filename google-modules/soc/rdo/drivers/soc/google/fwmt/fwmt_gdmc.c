// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/kernel.h>
#include <soc/google/goog-mba-gdmc-iface.h>
#include <soc/google/goog_gdmc_service_ids.h>
#include <soc/google/goog_mba_nq_xport.h>

#include "fwmt_driver.h"
#include "fwmt_service.h"

/**
 * fwmt_gdmc_send_req - Sends a FWMT request to GDMC.
 * @dev: The FWMT device.
 * @msg: The FWMT MBA message structure to be sent.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int fwmt_gdmc_send_req(struct fwmt_dev *dev, struct fwmt_mba_msg *msg)
{
	struct fwmt_dev_gdmc *gdev = container_of(dev, struct fwmt_dev_gdmc, dev);
	int message_res;

	struct {
		u32 header;
		struct fwmt_mba_msg msg;
	} gdmc_msg;

	static_assert(sizeof(gdmc_msg) == 16, "GDMC message size must be exactly 16 bytes");

	gdmc_msg.header = goog_mba_nq_xport_create_hdr(GDMC_MBA_SERVICE_ID_FWMT, 0);
	gdmc_msg.msg = *msg;

	/* Write memory barrier to sync previous write with MBA recipient. */
	wmb();

	message_res = gdmc_send_message(gdev->iface, &gdmc_msg);

	/* Read memory barrier to sync buffer contents with the kernel. */
	rmb();

	if (goog_mba_nq_xport_get_error(&gdmc_msg.header)) {
		dev_err(dev->dev, "GDMC MBA error: %d\n",
			(s16)goog_mba_nq_xport_get_data(&gdmc_msg.header));
		return -EIO;
	}

	return message_res;
}
