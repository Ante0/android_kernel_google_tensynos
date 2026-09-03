/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _PERF_CORE_APC_IRM_UTIL_H
#define _PERF_CORE_APC_IRM_UTIL_H

#include <linux/types.h>
#include <perf/core/apc_irm.h>

u32 irm_clamp_bw_val(u32 val);
u16 irm_calculate_min_clamp(struct irm_vote_t *vote);

#endif /* _PERF_CORE_APC_IRM_UTIL_H */
