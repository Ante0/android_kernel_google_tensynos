/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2026 Google LLC.
 *
 * DSU Latency Governor
 */

#ifndef _GS_GOVERNOR_MEMLAT_H_
#define _GS_GOVERNOR_MEMLAT_H_

#if IS_ENABLED(CONFIG_GS_MEMLAT_GOVERNOR)

void gs_governor_memlat_cpufreq_update(struct cpufreq_policy *policy, unsigned int target_freq);

#else

static inline void gs_governor_memlat_cpufreq_update(struct cpufreq_policy *policy,
		unsigned int target_freq)
{
}

#endif

#endif // _GS_GOVERNOR_MEMLAT_H_

