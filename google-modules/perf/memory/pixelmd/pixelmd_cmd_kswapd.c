// SPDX-License-Identifier: GPL-2.0

#include "pixelmd_api.h"
#include "pixelmd_cmd_kswapd.h"
#include "pixelmd_events.h"

#include <linux/sched.h>
#include <linux/mmzone.h>
#include <trace/events/vmscan.h>
#include <trace/hooks/vmscan.h>

/* Hook for trace_mm_vmscan_kswapd_wake(), called right before balance_pgdat(). */
static void vh_kswapd_wake(void *data, int node_id, int highest_zoneidx, int alloc_order)
{
	pixelmd_write_event(PIXELMD_SOURCE_KSWAPD, PIXELMD_EVENT_KSWAPD_RECLAIM_START, NULL, 0);
}

/* Hook for trace_android_vh_vmscan_kswapd_done(), called right after balance_pgdat(). */
static void vh_vmscan_kswapd_done(void *data, int node_id, unsigned int highest_zoneidx,
				  unsigned int alloc_order, unsigned int reclaim_order)
{
	pixelmd_write_event(PIXELMD_SOURCE_KSWAPD, PIXELMD_EVENT_KSWAPD_RECLAIM_DONE, NULL, 0);
}

int pixelmd_kswapd_register_hooks(void)
{
	int ret;

	/*
	 * Make sure _kswapd_done() is always registered before _kswapd_wake(),
	 * since it's better to report "done" without "wake" than vice-versa.
	 */
	ret = register_trace_android_vh_vmscan_kswapd_done(vh_vmscan_kswapd_done, NULL);
	if (ret)
		return ret;

	ret = register_trace_mm_vmscan_kswapd_wake(vh_kswapd_wake, NULL);
	if (ret) {
		unregister_trace_android_vh_vmscan_kswapd_done(vh_vmscan_kswapd_done, NULL);
		return ret;
	}

	return 0;
}

void pixelmd_kswapd_unregister_hooks(void)
{
	unregister_trace_mm_vmscan_kswapd_wake(vh_kswapd_wake, NULL);
	unregister_trace_android_vh_vmscan_kswapd_done(vh_vmscan_kswapd_done, NULL);
}

/*
 * Wakes up kswapd to make sure pgdat is balanced for order 0 allocations.
 *
 * This function is designed to make kswapd proactively reclaim memory in the
 * easiest and fastest mode (order 0).
 */
static void nudge_kswapd(pg_data_t *pgdat)
{
	enum zone_type highest_zoneidx = ZONE_MOVABLE;
	enum zone_type curr_idx;

	curr_idx = READ_ONCE(pgdat->kswapd_highest_zoneidx);

	// This is a bit racy, but this is how wakeup_kswapd() works too.
	if (curr_idx == MAX_NR_ZONES || curr_idx < highest_zoneidx)
		WRITE_ONCE(pgdat->kswapd_highest_zoneidx, highest_zoneidx);

	// Don't bother waking up if kswapd is running.
	if (!waitqueue_active(&pgdat->kswapd_wait))
		return;

	wake_up_interruptible(&pgdat->kswapd_wait);
}

long pixelmd_cmd_nudge_kswapd(void)
{
	nudge_kswapd(NODE_DATA(first_online_node));
	return 0;
}
