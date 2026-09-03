/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GOOGLE_DPA_POWER_DOMAIN_H
#define _GOOGLE_DPA_POWER_DOMAIN_H

#include <linux/device.h>

int google_dpa_attach_power_domain(struct device *dev, struct device **out_pd_vdev,
				   struct device_link **out_link);

void google_dpa_detach_power_domain(struct device *pd_vdev, struct device_link *link);

#endif // _GOOGLE_DPA_POWER_DOMAIN_H
