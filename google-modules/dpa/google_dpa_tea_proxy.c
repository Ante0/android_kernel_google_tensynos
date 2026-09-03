// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 * This file init the tea proxy to forward NOA tea events to the kernel.
 */
#include "google_dpa_rpc_internal.h"
#include "services/google_dpa_services.h"

#include "google_dpa_tea_proxy.h"

int google_dpa_tea_proxy_init(struct google_dpa *dpa)
{
	int ret = 0;

	ret = dpa_rpc_tea_service_push_events(google_dpa_rpc_nep_client(), dpa);

	if (ret) {
		dev_err(dpa->dev, "Cannot listen to NEP TEA RPC Service!");
		return -EIO;
	}
	return ret;
}
