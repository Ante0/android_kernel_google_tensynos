/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _PERF_CORE_APC_IRM_DEBUG_H
#define _PERF_CORE_APC_IRM_DEBUG_H

#include <linux/kconfig.h>
#include <linux/types.h>
#include <perf/core/apc_irm.h>

#define CPM_IRM_FREQ_CLAMP_GMC_SHIFT (0)
#define CPM_IRM_FREQ_CLAMP_GMC_MASK (0xF)
#define CPM_IRM_FREQ_CLAMP_MEMSS_SHIFT (4)
#define CPM_IRM_FREQ_CLAMP_MEMSS_MASK (0xF)
#define CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_SHIFT (8)
#define CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_MASK (0xF)
#define CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_SHIFT (12)

#if (IS_ENABLED(CONFIG_SOC_MBU) || IS_ENABLED(CONFIG_SOC_LGA))
#define CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_MASK (0x7)
#define CPM_IRM_FREQ_CLAMP_VALID_BIT (1 << 15)
#else
#define CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_MASK (0xF)
#endif

struct irm_client_t;

/*
 * Get the currently published vote for a client.
 * This is primarily for testing and debugging.
 */
void get_irm_client_published_vote(struct irm_client_t *client, struct irm_vote_t *vote);

/*
 * Get the value of a register (file) for a client via MBFS.
 * This is primarily for testing and debugging to verify register writes.
 */
u32 get_irm_register_value_from_mbfs(struct irm_client_t *client, const char *filename);

#endif /* _PERF_CORE_APC_IRM_DEBUG_H */
