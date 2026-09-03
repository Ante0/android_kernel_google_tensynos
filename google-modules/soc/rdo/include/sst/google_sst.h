/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _GOOGLE_SST_H
#define _GOOGLE_SST_H

#include <linux/clockchips.h>
#include <linux/clocksource.h>

struct google_sst_timer {
	struct google_sst *google_sst;
	u32 timer_freq;
	size_t hw_idx;
	int irq_hdl;
	struct clock_event_device clk_evt;

	/* for testing purpose */
	struct dentry *debugfs_test;
	u32 test_cnt;
	u32 next_event_delta;
	u64 next_event_request_time;
	bool timer_test_enabled;
};

struct google_sst_ops {
	u64 (*read_gtc_time)(struct google_sst *sst);
};

struct google_sst {
	struct device *dev;
	void __iomem *base_addr;
	const struct google_sst_ops *ops;
	u32 freq;
	struct clocksource clk_src;
	struct google_sst_timer *sst_timer_arr;
	size_t sst_timer_arr_size;

	struct dentry *debugfs_root;
	bool timesource_test_enabled;
	u64 last_arm_timestamp_ns;
	u64 last_gtc_tick;
};

#endif /* _GOOGLE_SST_H */
