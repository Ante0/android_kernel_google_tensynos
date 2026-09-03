/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Pixel Energy Model (EM).
 *
 * Copyright (C) 2022 Google, Inc.
 */

#ifndef __PIXEL_EM_H__
#define __PIXEL_EM_H__

#if IS_ENABLED(CONFIG_PIXEL_EM)

struct pixel_em_opp {
	unsigned int freq;
	unsigned int capacity;
	unsigned int power;
	unsigned long cost;
	bool inefficient;
#if IS_ENABLED(CONFIG_PIXEL_EM_VOLTAGE_SCALING)
	unsigned long voltage;
#endif
#if IS_ENABLED(CONFIG_PIXEL_EM_FREQUENCY_SCALING)
	unsigned int scaling_freq;
#endif
};

struct pixel_em_idle_opp {
	unsigned int freq;
	unsigned int energy;
};

#if IS_ENABLED(CONFIG_PIXEL_EM_FREQUENCY_SCALING)
	enum constraint_type {
		CONSTRAINT_MIN,
		CONSTRAINT_MAX,
		CONSTRAINT_NONE
	};
#endif

struct pixel_em_cluster {
	cpumask_t cpus;
#if IS_ENABLED(CONFIG_PIXEL_EM_VOLTAGE_SCALING)
	bool voltage_table;
	int voltage_scaling_target;
	int voltage_level;
	unsigned long energy;
	unsigned long *scaling_factor_table;
#endif
#if IS_ENABLED(CONFIG_PIXEL_EM_FREQUENCY_SCALING)
	bool frequency_scaling_table;
	int frequency_scaling_target;
	enum constraint_type constraint_type;
#endif
	int num_opps;
	union {
		struct pixel_em_opp *opps;
		struct pixel_em_idle_opp *idle_opps;
	};
};

struct pixel_em_profile {
	struct list_head list;
	struct profile_sysfs_helper *sysfs_helper;
	const char *name;
	int num_clusters;
	struct pixel_em_cluster *clusters;
	struct pixel_em_cluster **cpu_to_cluster; // Maps CPU index to a cluster pointer
};

struct pixel_idle_em {
	int num_clusters;
	struct pixel_em_cluster *clusters;
	struct pixel_em_cluster **cpu_to_cluster;
};

extern struct pixel_em_profile **pixel_em_active_profile;

static inline struct pixel_em_profile *get_em_profile(void)
{
	struct pixel_em_profile **profile_ptr_snapshot;

	profile_ptr_snapshot = READ_ONCE(pixel_em_active_profile);
	if (!profile_ptr_snapshot)
		return NULL;

	return READ_ONCE(*profile_ptr_snapshot);
}

static inline struct pixel_em_cluster *get_em_cluster(int cpu)
{
	struct pixel_em_profile *profile = get_em_profile();

	if (!profile)
		return NULL;

	return profile->cpu_to_cluster[cpu];
}

void register_arch_set_freq_scale_cb(void (*func)(void *, const struct cpumask *, unsigned long,
						  unsigned long, unsigned long *));
void register_update_thermal_freq_cap_cb(void (*func)(unsigned int));
#if IS_ENABLED(CONFIG_PIXEL_EM_FREQUENCY_SCALING)
void register_reset_scaling_freq_cb(void (*func)(int));
#endif
#endif /* CONFIG_PIXEL_EM */

#endif /* __PIXEL_EM_H__ */
