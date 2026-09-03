// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2024 Google LLC */

#include <linux/kernel.h>
#include <linux/units.h>

#include <dvfs-helper/google_dvfs_helper.h>

#include "cpm_tracepoint_decoder.h"
#include "perf_tracepoints.h"

#define DOMAIN_TRACE_STR_LEN (MAX_DVFS_NAME_LEN + sizeof("_freq"))
#define TRACE_OP_LVL_BITFIELD GENMASK(15, 0)
#define EXTRACT_DOMAIN_ID(payload) (payload >> 16)
#define EXTRACT_DOMAIN_OP_LVL(payload) (payload & TRACE_OP_LVL_BITFIELD)

static enum tracepoint_handle domain_freq_handler(const char *tp_string,
						  u32 payload, u64 timestamp)
{
	u16 domain_id;
	const char *domain_name;
	char trace_name[DOMAIN_TRACE_STR_LEN];
	s64 freq;

	domain_id = EXTRACT_DOMAIN_ID(payload);
	domain_name = dvfs_helper_domain_id_to_name(domain_id);
	if (!domain_name)
		return CLIENT_TP_HANDLING_ERROR;

	freq = dvfs_helper_get_domain_opp_frequency_mhz(
		domain_id, EXTRACT_DOMAIN_OP_LVL(payload));
	if (freq < 0)
		return CLIENT_TP_HANDLING_ERROR;

	scnprintf(trace_name, sizeof(trace_name), "%s_freq", domain_name);
	add_cpm_param_trace(trace_name, freq, timestamp);

	return CLIENT_TP_HANDLING_COMPLETE;
}

struct client_tracepoint domain_freq_tp = { .enabled = true,
					    .tp_string = "DvfsTar %d",
					    .init = NULL,
					    .handler = domain_freq_handler,
					    .exit = NULL };

/*
 * Payload format:
 * 27 - 31 bits: last vote
 * 19 - 26 bits: domain id (gmc, memss, etc.)
 * 16 - 18 bits: voter id (thermal, debug, devfreq, ...)
 * 15 bit: vote type (min or max) (0: min, 1: max)
 * 10 - 14 bits: aggregated high opp from votes (i.e. before clamp)
 * 5 - 9 bits: aggregated low opp from votes
 * 0 - 4 bits: new overall high opp (i.e. after clamp)
 */

#define EXTRACT_LAST_VOTE(payload) ((payload & GENMASK(31, 27)) >> 27)
#define EXTRACT_AGG_UPDATE_DOMAIN_ID(payload) \
	((payload & GENMASK(26, 19)) >> 19)
#define EXTRACT_AGG_UPDATE_VOTER_ID(payload) ((payload & GENMASK(18, 16)) >> 16)
#define EXTRACT_AGG_UPDATE_VOTE_TYPE(payload) ((payload & BIT(15)) >> 15)
#define EXTRACT_AGG_UPDATE_AGG_HI_OPP(payload) \
	((payload & GENMASK(14, 10)) >> 10)
#define EXTRACT_AGG_UPDATE_NEW_LO_OPP(payload) ((payload & GENMASK(9, 5)) >> 5)
#define EXTRACT_AGG_UPDATE_NEW_HI_OPP(payload) (payload & GENMASK(4, 0))

enum dvfs_voter_id {
	DVFS_VOTER_DEVFREQ,
	DVFS_VOTER_GOVERNOR,
	DVFS_VOTER_THERMAL,
	DVFS_VOTER_DEBUG,
	DVFS_VOTER_NUM,
} dvfs_voter_id_t;

const char *voter_id_str[] = {
	[DVFS_VOTER_DEVFREQ] = "devfreq",
	[DVFS_VOTER_GOVERNOR] = "governor",
	[DVFS_VOTER_THERMAL] = "thermal",
	[DVFS_VOTER_DEBUG] = "debug",
};

#define MAX_VOTER_ID_STR_LEN 9

#define AGG_UPDATE_TRACE_STR_LEN \
	(MAX_DVFS_NAME_LEN + sizeof("_agg_") + sizeof("max"))

static enum tracepoint_handle
freq_agg_update_handler(const char *tp_string, u32 payload, u64 timestamp)
{
	char trace_name_min_max[AGG_UPDATE_TRACE_STR_LEN];
	char trace_name_agg_update[AGG_UPDATE_TRACE_STR_LEN];
	char trace_voter_id[MAX_DVFS_NAME_LEN + MAX_VOTER_ID_STR_LEN +
			    sizeof("_vote")];
	char trace_name_voted_freq[DOMAIN_TRACE_STR_LEN];
	u16 domain_id;
	const char *domain_name;
	u32 voter_id;
	u32 last_vote;
	bool is_max;
	u32 min_of_high_votes;
	u32 max_of_low_votes;
	u32 final_high_vote;
	s64 freq;

	domain_id = EXTRACT_AGG_UPDATE_DOMAIN_ID(payload);
	domain_name = dvfs_helper_domain_id_to_name(domain_id);
	if (!domain_name)
		return CLIENT_TP_HANDLING_ERROR;

	voter_id = EXTRACT_AGG_UPDATE_VOTER_ID(payload);
	if (voter_id >= DVFS_VOTER_NUM)
		return CLIENT_TP_HANDLING_ERROR;

	last_vote = EXTRACT_LAST_VOTE(payload);
	is_max = EXTRACT_AGG_UPDATE_VOTE_TYPE(payload);
	min_of_high_votes = EXTRACT_AGG_UPDATE_AGG_HI_OPP(payload);
	max_of_low_votes = EXTRACT_AGG_UPDATE_NEW_LO_OPP(payload);
	final_high_vote = EXTRACT_AGG_UPDATE_NEW_HI_OPP(payload);

	scnprintf(trace_name_min_max, sizeof(trace_name_min_max), "%s_agg_%s",
		  domain_name, is_max ? "max" : "min");

	scnprintf(trace_name_agg_update, sizeof(trace_name_agg_update),
		  "%s_agg_hi", domain_name);

	scnprintf(trace_voter_id, sizeof(trace_voter_id), "%s_%s_vote",
		  domain_name, voter_id_str[voter_id]);

	add_cpm_param_trace(trace_name_min_max,
			    is_max ? final_high_vote : max_of_low_votes,
			    timestamp);

	add_cpm_param_trace(trace_name_agg_update, min_of_high_votes,
			    timestamp);

	add_cpm_param_trace(trace_voter_id, last_vote, timestamp);

	freq = dvfs_helper_get_domain_opp_frequency_mhz(domain_id, last_vote);

	if (freq < 0)
		return CLIENT_TP_HANDLING_ERROR;

	scnprintf(trace_name_voted_freq, sizeof(trace_name_voted_freq),
		  "%s_freq", domain_name);
	add_cpm_param_trace(trace_name_voted_freq, freq, timestamp);

	return CLIENT_TP_HANDLING_COMPLETE;
}

struct client_tracepoint freq_agg_update_tp = {
	.enabled = true,
	.tp_string = "freq_agg_update %d",
	.init = NULL,
	.handler = freq_agg_update_handler,
	.exit = NULL
};

/*
 * Payload format:
 * 28 - 31 bits: lp_gov id
 * 25 - 27 bits: event type (0: clock, 1: read, 2: write, 3: util)
 * 0 - 24 bits: data
 */

#define EXTRACT_LP_GOV_ID(payload) (((payload) & GENMASK(31, 28)) >> 28)
#define EXTRACT_LP_GOV_EVENT(payload) (((payload) & GENMASK(27, 25)) >> 25)
#define EXTRACT_LP_GOV_DATA(payload) ((payload) & GENMASK(24, 0))

#define DVFSMON_TRACE_NAMES(name) \
	"dvfsmon_" #name "_clocks", \
	"dvfsmon_" #name "_read_bandwidth", \
	"dvfsmon_" #name "_write_bandwidth", \
	"dvfsmon_" #name "_utilization"

static const char * const dvfsmon_trace_names[][4] = {
	{ DVFSMON_TRACE_NAMES(gpu) },     // 0
	{ DVFSMON_TRACE_NAMES(tpu) },     // 1
	{ DVFSMON_TRACE_NAMES(aurdsp) },  // 2
	{ DVFSMON_TRACE_NAMES(fabmed) },  // 3
	{ DVFSMON_TRACE_NAMES(fabsyss) }, // 4
	{ DVFSMON_TRACE_NAMES(aoss) },    // 5
	{ DVFSMON_TRACE_NAMES(fabstby) }, // 6
	{ DVFSMON_TRACE_NAMES(fabmem) },  // 7
	{ DVFSMON_TRACE_NAMES(fabdisp) }, // 8
	{ DVFSMON_TRACE_NAMES(fabhbw) },  // 9
	{ DVFSMON_TRACE_NAMES(cpu) },     // 10
};

static enum tracepoint_handle
dvfsmon_handler(const char *tp_string, u32 payload, u64 timestamp)
{
	u8 id = EXTRACT_LP_GOV_ID(payload);
	u8 event = EXTRACT_LP_GOV_EVENT(payload);
	u32 data = EXTRACT_LP_GOV_DATA(payload);

	if (id >= ARRAY_SIZE(dvfsmon_trace_names) || event >= 4) {
		pr_err_ratelimited("%s: invalid id %u or event %u\n",
				   __func__, id, event);
		return CLIENT_TP_HANDLING_ERROR;
	}

	add_cpm_param_trace(dvfsmon_trace_names[id][event], data, timestamp);

	return CLIENT_TP_HANDLING_COMPLETE;
}

struct client_tracepoint dvfsmon_tp = {
	.enabled = true,
	.tp_string = "lp_gov %d",
	.init = NULL,
	.handler = dvfsmon_handler,
	.exit = NULL,
};
