// SPDX-License-Identifier: GPL-2.0-only
/*
 * Metis specific Coresight remote implementation.
 *
 * Copyright (C) 2026 Google LLC
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include <gcip/gcip-coresight-remote.h>

#include "gxp-internal.h"
#include "gxp-kci.h"
#include "metis-platform.h"

void metis_coresight_remote_init(struct gxp_dev *gxp)
{
	struct metis_dev *metis = to_metis_dev(gxp);
	const struct gcip_coresight_remote_args args = {
		.dev = gxp->dev,
		.num_fw_targets = 1,
		.pm = gxp->power_mgr->pm,
		.kci_data = { gxp },
		.send_kci = gxp_kci_send_coresight_remote_cmd
	};

	metis->coresight_remote = gcip_coresight_remote_register(&args);
	if (IS_ERR_OR_NULL(metis->coresight_remote)) {
		dev_warn(gxp->dev, "Failed to register coresight remote: %ld\n",
			 PTR_ERR(metis->coresight_remote));
	}
}

void metis_coresight_remote_exit(struct gxp_dev *gxp)
{
	struct metis_dev *metis = to_metis_dev(gxp);

	if (!IS_ERR_OR_NULL(metis->coresight_remote)) {
		gcip_coresight_remote_unregister(metis->coresight_remote);
		metis->coresight_remote = NULL;
	}
}

void metis_coresight_remote_restore(struct gxp_dev *gxp)
{
	struct metis_dev *metis = to_metis_dev(gxp);

	if (!IS_ERR_OR_NULL(metis->coresight_remote))
		gcip_coresight_remote_restore_state(metis->coresight_remote);
}
