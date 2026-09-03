// SPDX-License-Identifier: GPL-2.0
/*
 * mbu_bcl_soc.c Google bcl driver - Utility
 *
 * Copyright (c) 2025, Google LLC. All rights reserved.
 *
 */

#include <sysfs/qos.h>
#include "bcl.h"

ssize_t qos_show(struct bcl_device *bcl_dev, int idx, char *buf)
{
	struct bcl_zone *zone;

	zone = bcl_dev->zone[idx];
	if ((!zone) || (!zone->bcl_qos))
		return -EIO;

	return sysfs_emit(buf, "CPU0,CPU1,CPU2,GPU,TPU,GXP\n%d,%d,%d,%d,%d,%d\n",
			  zone->bcl_qos->cpu_limit[QOS_CPU0][QOS_LIGHT_IND],
			  zone->bcl_qos->cpu_limit[QOS_CPU1][QOS_LIGHT_IND],
			  zone->bcl_qos->cpu_limit[QOS_CPU2][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_GPU][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_TPU][QOS_LIGHT_IND],
			  zone->bcl_qos->df_limit[QOS_GXP][QOS_LIGHT_IND]);
}

ssize_t qos_store(struct bcl_device *bcl_dev, int idx, const char *buf, size_t size)
{
	unsigned int soc[7];
	int ret, intended_items = 6;
	struct bcl_zone *zone;

	ret = sscanf(buf, "%d,%d,%d,%d,%d,%d", &soc[0], &soc[1], &soc[2], &soc[3], &soc[4],
		     &soc[5]);

	if (ret != intended_items)
		return -EINVAL;

	zone = bcl_dev->zone[idx];
	if (!zone->bcl_qos)
		return -EIO;

	zone->bcl_qos->cpu_limit[QOS_CPU0][QOS_LIGHT_IND] = soc[0];
	zone->bcl_qos->cpu_limit[QOS_CPU1][QOS_LIGHT_IND] = soc[1];
	zone->bcl_qos->cpu_limit[QOS_CPU2][QOS_LIGHT_IND] = soc[intended_items - 4];
	zone->bcl_qos->df_limit[QOS_GPU][QOS_LIGHT_IND] = soc[intended_items - 3];
	zone->bcl_qos->df_limit[QOS_TPU][QOS_LIGHT_IND] = soc[intended_items - 2];
	zone->bcl_qos->df_limit[QOS_GXP][QOS_LIGHT_IND] = soc[intended_items - 1];

	return size;
}
