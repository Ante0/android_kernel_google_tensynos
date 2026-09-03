/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __UWB_COREDUMP__
#define __UWB_COREDUMP__

#include <linux/platform_data/sscoredump.h>
#include <linux/platform_device.h>
#include <linux/ktime.h>
#include <linux/types.h>

struct sk_buff;
struct u100_ctx;

enum coredump_state {
	CRASH_NTF_0,
	CRASH_NTF_1,
};

struct sscd_desc {
	struct sscd_platform_data sscd_pdata;
	struct platform_device sscd_dev;

	const char *name;
	u16 seg_count;
	union {
		struct sscd_segment pad;
		__DECLARE_FLEX_ARRAY(struct sscd_segment, segs);
	};
};

struct uwb_coredump {
	enum coredump_state state;
	time64_t time;
	struct sscd_desc *sscd;
	bool disable_reset;
	bool ignore_boot_error;
};

int u100_register_coredump(struct u100_ctx *u100_ctx);
void u100_unregister_coredump(struct u100_ctx *u100_ctx);
bool is_coredump(struct u100_ctx *u100_ctx, struct sk_buff *skb);
int u100_process_coredump(struct u100_ctx *u100_ctx, struct sk_buff *skb);
void u100_report_coredump_on_poweron(struct u100_ctx *u100_ctx, const char *msg, int errcode);
#endif /* __UWB_COREDUMP__ */

