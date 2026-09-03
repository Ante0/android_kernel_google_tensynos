/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2025 Google LLC
 */

#ifndef PERF_DOMAIN_H
#define PERF_DOMAIN_H

enum gs_devfreq_type {
	GS_TARGET_DEVFREQ,
	GS_CLAMP_DEVFREQ,
	GS_RECOMMENDED_DEVFREQ,
	NUM_GS_DEVFREQ_TYPES
};

#define MAX_FAST_DEVFREQ_VOTE 5

struct fast_devfreq_vote {
	const char *vote_name;
	struct devfreq *df;
	unsigned long min_freq_Hz;
};

struct fast_devfreq_vote_data {
	struct devfreq *df;
	spinlock_t fvote_lock;
	struct fast_devfreq_vote *fdf_votes[MAX_FAST_DEVFREQ_VOTE];
};

#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
struct devfreq *gs_perf_domain_find_devfreq(const char *name, enum gs_devfreq_type type);
int add_fast_devfreq_vote(struct devfreq *df, const char *vote_name,
			  struct fast_devfreq_vote *fdf_vote);
int remove_fast_devfreq_vote(struct fast_devfreq_vote *fdf_vote);
int update_fast_devfreq_vote(struct fast_devfreq_vote *fdf_vote,
			     unsigned long target_freq);
#else

static inline struct devfreq *gs_perf_domain_find_devfreq(const char *name,
		enum gs_devfreq_type type)
{
	return NULL;
}
static inline int add_fast_devfreq_vote(struct devfreq *df, const char *vote_name,
		struct fast_devfreq_vote *fdf_vote)
{
	return -1;
}
static inline int remove_fast_devfreq_vote(struct fast_devfreq_vote *fdf_vote)
{
	return -1;
}
static inline int update_fast_devfreq_vote(struct fast_devfreq_vote *fdf_vote,
		unsigned long target_freq)
{
	return -1;
}
#endif // enabled(CONFIG_GS_PERF_DOMAIN)

#endif // PERF_DOMAIN_H
