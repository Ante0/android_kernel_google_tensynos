/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM apc_irm

#if !defined(_APC_IRM_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _APC_IRM_TRACE_H

#include <linux/tracepoint.h>
#include <perf/core/apc_irm.h>

/*
 * These tracepoints are designed to facilitate viewing in the Perfetto UI by
 * utilizing Perfetto's "Kernel track events" convention to automatically parse
 * these tracepoints into custom-scoped slice tracks. Learn more at
 * https://perfetto.dev/docs/reference/kernel-track-event and
 * https://perfetto.dev/docs/getting-started/ftrace.
 *
 * Break down the members of struct irm_vote_t into 3 kinds of tracepoints
 * (GMC BW, PF, and GSLC BW) so that each tracepoint is easy to view in the
 * Ftrace Events tab in the Perfetto UI.
 *
 * Rationale for these design choices:
 * - slice_name = client_name: Use the client name as the slice name allows us to
 *   quickly tell which client the vote is for in the UI.
 *
 * - track_event_type = 'I': We use Instants instead of Duration Slices ('B'/'E')
 *   because a vote represents an active state rather than a block of execution
 *   code. While not directly viewable in the "Current Selection" tab in Perfetto,
 *   the content of the events will be shown in the "Ftrace Events" tab.
 *
 * - scope_track_id: if set, scope_{...} field specifies the scoping id of the
 *   track, which is used as a grouping key for the tracks. We use a custom
 *   scope (scope_track_id) instead of by process/thread.
 */

/*
 * TP_STRUCT__entry content is shown in the "Ftrace Events" tab in the Perfetto UI.
 * TP_printk is used to print to the trace or trace_pipe file in
 * /sys/kernel/tracing/.
 */

/* clang-format off */

TRACE_EVENT(published_vote_bw_gmc,

	TP_PROTO(const char *client_name, int track_id, struct irm_vote_t *vote),

	TP_ARGS(client_name, track_id, vote),

	TP_STRUCT__entry(
		__field(u32, avg_r)
		__field(u32, pk_r)
		__field(u32, rt_r)
		__field(u32, avg_w)
		__field(u32, pk_w)
		__field(u32, rt_w)
		__string(slice_name, client_name)
		__field(u8, scope_track_id)
		__field(char, track_event_type)
	),

	TP_fast_assign(
		__entry->avg_r = vote->avg_read_gmc_bw;
		__entry->pk_r = vote->peak_read_gmc_bw;
		__entry->rt_r = vote->rt_read_gmc_bw;
		__entry->avg_w = vote->avg_write_gmc_bw;
		__entry->pk_w = vote->peak_write_gmc_bw;
		__entry->rt_w = vote->rt_write_gmc_bw;
		__assign_str(slice_name);
		__entry->scope_track_id = track_id;
		__entry->track_event_type = 'I';
	),

	TP_printk("client=%s gmc_bw: avg_r=%u pk_r=%u rt_r=%u avg_w=%u pk_w=%u rt_w=%u",
		  __get_str(slice_name),
		  __entry->avg_r, __entry->pk_r, __entry->rt_r,
		  __entry->avg_w, __entry->pk_w, __entry->rt_w)
);

/*
 * If needed, we could add another tracepoint for a counter track to easily tell in the
 * Perfetto UI whether the corresponding clamping takes effect.
 */
TRACE_EVENT(published_vote_max_opp,

	TP_PROTO(const char *client_name, int track_id, struct irm_vote_t *vote),

	TP_ARGS(client_name, track_id, vote),

	TP_STRUCT__entry(
		__field(u8, gmc)
		__field(u8, memss)
		__field(u8, inter_ancestor)
		__field(u8, inter_descendant)
		__string(slice_name, client_name)
		__field(u8, scope_track_id)
		__field(char, track_event_type)
	),

	TP_fast_assign(
		__entry->gmc = vote->pf_gmc;
		__entry->memss = vote->pf_memss;
		__entry->inter_ancestor = vote->pf_int_ancestor;
		__entry->inter_descendant = vote->pf_int_descendant;
		__assign_str(slice_name);
		__entry->scope_track_id = track_id;
		__entry->track_event_type = 'I';
	),

	TP_printk("client=%s max_opp: gmc=%u memss=%u inter_ancestor=%u inter_descendant=%u",
		  __get_str(slice_name),
		  __entry->gmc, __entry->memss, __entry->inter_ancestor, __entry->inter_descendant)
);

#if IS_ENABLED(CONFIG_SOC_LGA)
TRACE_EVENT(published_vote_bw_gslc,

	TP_PROTO(const char *client_name, int track_id, struct irm_vote_t *vote),

	TP_ARGS(client_name, track_id, vote),

	TP_STRUCT__entry(
		__field(u32, avg_r)
		__field(u32, pk_r)
		__field(u32, rt_r)
		__field(u32, avg_w)
		__field(u32, pk_w)
		__field(u32, rt_w)
		__string(slice_name, client_name)
		__field(u8, scope_track_id)
		__field(char, track_event_type)
	),

	TP_fast_assign(
		__entry->avg_r = vote->avg_read_gslc_bw;
		__entry->pk_r = vote->peak_read_gslc_bw;
		__entry->rt_r = vote->rt_read_gslc_bw;
		__entry->avg_w = vote->avg_write_gslc_bw;
		__entry->pk_w = vote->peak_write_gslc_bw;
		__entry->rt_w = vote->rt_write_gslc_bw;
		__assign_str(slice_name);
		__entry->scope_track_id = track_id;
		__entry->track_event_type = 'I';
	),

	TP_printk("client=%s gslc_bw: avg_r=%u pk_r=%u rt_r=%u avg_w=%u pk_w=%u rt_w=%u",
		  __get_str(slice_name),
		  __entry->avg_r, __entry->pk_r, __entry->rt_r,
		  __entry->avg_w, __entry->pk_w, __entry->rt_w)
);
#endif

/* clang-format off */

#endif /* _APC_IRM_TRACE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE apc_irm_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
