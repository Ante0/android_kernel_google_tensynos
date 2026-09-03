// SPDX-License-Identifier: GPL-2.0-only
/* topology.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2022 Google LLC
 */

#include <linux/sched.h>
#include <kernel/sched/sched.h>

#include "sched_priv.h"

#if IS_ENABLED(CONFIG_VH_SCHED) && IS_ENABLED(CONFIG_PIXEL_EM)
#include "pixel_em.h"
#endif

#if IS_ENABLED(CONFIG_VH_SCHED) && IS_ENABLED(CONFIG_PIXEL_EM)
void vh_arch_set_freq_scale_pixel_mod(void *data, const struct cpumask *cpus,
				      unsigned long freq,
				      unsigned long max, unsigned long *scale)
{
	int i;
	struct pixel_em_cluster *cluster = get_em_cluster(cpumask_first(cpus));

	if (cluster) {
		struct pixel_em_opp *max_opp;
		struct pixel_em_opp *opp;

		max_opp = &cluster->opps[cluster->num_opps - 1];

		for (i = 0; i < cluster->num_opps; i++) {
			opp = &cluster->opps[i];
			if (opp->freq >= freq)
				break;
		}

		*scale = (opp->capacity << SCHED_CAPACITY_SHIFT) / max_opp->capacity;
	}
}
#endif

void android_vh_use_amu_fie_pixel_mod(void *data, bool *use_amu_fie)
{
	*use_amu_fie = false;
}

void android_rvh_build_perf_domains_pixel_mod(void *data, bool *eas_check)
{
	*eas_check = true;
}
