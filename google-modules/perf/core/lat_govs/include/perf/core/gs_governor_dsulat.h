/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Google, Inc.
 *
 * DSU Latency Governor
 */

#ifndef _GS_GOVERNOR_DSULAT_H_
#define _GS_GOVERNOR_DSULAT_H_

#if IS_ENABLED(CONFIG_GS_DSULAT_GOVERNOR)

void gs_governor_dsulat_cpufreq_update(struct cpufreq_policy *policy, unsigned int target_freq);

#else

static inline void gs_governor_dsulat_cpufreq_update(struct cpufreq_policy *policy,
		unsigned int target_freq)
{
}

#endif

#endif // _GS_GOVERNOR_DSULAT_H_
