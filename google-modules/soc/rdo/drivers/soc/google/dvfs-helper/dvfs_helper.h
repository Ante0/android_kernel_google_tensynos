/* SPDX-License-Identifier: GPL-2.0-only or BSD-2-Clause */
#ifndef _DVFS_HELPER_H
#define _DVFS_HELPER_H

#include <linux/types.h>

struct dvfs_opp_info {
	u64 freq : 14;
	u32 supported : 1;
	u32 resv1 : 1;
	u32 voltage : 14;
	u32 sw_override : 1;
	u32 resv2 : 1;
	u8 level;
};

struct dvfs_info {
	u8 rev_id;
	u8 num_domains;
	struct dvfs_domain_info *domains;
};

#endif /* _DVFS_HELPER_H */
