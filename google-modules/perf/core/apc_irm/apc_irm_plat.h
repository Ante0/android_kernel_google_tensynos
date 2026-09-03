/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _PERF_CORE_APC_IRM_PLAT_H
#define _PERF_CORE_APC_IRM_PLAT_H

#include <linux/bits.h>
#include <linux/compiler_types.h>
#include <linux/types.h>
#include <perf/core/apc_irm.h>

#define DVFS_TRIG_EN BIT(0)
#define POLL_SLEEP_TIME_IN_US 5
#define POLL_TIMEOUT_TIME_IN_US 10000

void apply_vote(void __iomem *base, bool is_sync, struct irm_vote_t *vote);

#endif /* _PERF_CORE_APC_IRM_PLAT_H */
