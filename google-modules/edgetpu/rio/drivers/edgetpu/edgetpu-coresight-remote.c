// SPDX-License-Identifier: GPL-2.0-only
/*
 * EdgeTPU specific coresight remote implementation.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <gcip/gcip-coresight-remote.h>

#include "edgetpu-config.h"
#include "edgetpu-coresight-remote.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-pm.h"

#if EDGETPU_USE_CORESIGHT_REMOTE

int edgetpu_coresight_remote_init(struct edgetpu_dev *etdev)
{
	/* Set up arguments for coresight remote registration */
	const struct gcip_coresight_remote_args args = {
		.dev = etdev->dev,
		.num_fw_targets = 1,
		.pm = etdev->pm->gpm,
		.kci_data = { etdev },
		.send_kci = edgetpu_kci_send_coresight_remote_cmd
	};

	etdev->coresight_remote = gcip_coresight_remote_register(&args);
	if (IS_ERR(etdev->coresight_remote))
		return PTR_ERR(etdev->coresight_remote);

	return 0;
}

void edgetpu_coresight_remote_exit(struct edgetpu_dev *etdev)
{
	if (!IS_ERR_OR_NULL(etdev->coresight_remote)) {
		gcip_coresight_remote_unregister(etdev->coresight_remote);
		etdev->coresight_remote = NULL;
	}
}

#endif /* EDGETPU_USE_CORESIGHT_REMOTE */
