// SPDX-License-Identifier: GPL-2.0-only
/*
 * userspace_bcl_qos.c Google bcl PMQOS driver
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 *
 */
#include "userspace_bcl_qos.h"
#include "../soc_defs.h"

int userspace_bcl_qos_setup(struct bcl_device *bcl_dev)
{
	int ret;

	ret = google_bcl_register_zone(bcl_dev, USERSPACE, "userspace", 0, 0, 0,
				       0, 0, 0);
	if (ret < 0) {
		dev_err(bcl_dev->device, "bcl_register fail: USERSPACE\n");
		return -ENODEV;
	}

	bcl_dev->throttle_state = THROTTLE_STATE_NONE;
	return ret;
}

int userspace_bcl_qos_update(struct bcl_device *bcl_dev, int throttle_state)
{
	int ret = 0;
	struct bcl_zone *zone = bcl_dev->zone[USERSPACE];

	if (!zone->bcl_qos)
		return -ENODEV;

	if (throttle_state > THROTTLE_STATE_MAX ||
	    throttle_state < THROTTLE_STATE_NONE)
		return -EINVAL;

	bcl_dev->throttle_state = throttle_state;

	if (throttle_state == THROTTLE_STATE_NONE ||
	    throttle_state == THROTTLE_STATE_MEDIUM)
		google_bcl_qos_update(zone, QOS_NONE);
	else if (throttle_state == THROTTLE_STATE_HEAVY)
		google_bcl_qos_update(zone, QOS_LIGHT);
	else if (throttle_state == THROTTLE_STATE_CRITICAL)
		google_bcl_qos_update(zone, QOS_HEAVY);

	return ret;
}
