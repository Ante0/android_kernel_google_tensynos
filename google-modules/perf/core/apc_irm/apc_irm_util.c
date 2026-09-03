// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/types.h>
#include <perf/core/apc_irm.h>

#include "apc_irm_debug.h"
#include "apc_irm_plat.h"
#include "apc_irm_util.h"

u32 irm_clamp_bw_val(u32 val)
{
	return (val > 0xFFFF) ? 0xFFFF : val;
}

u16 irm_calculate_min_clamp(struct irm_vote_t *vote)
{
	u16 min_clamp;
	min_clamp = min_t(u16, vote->pf_gmc, CPM_IRM_FREQ_CLAMP_GMC_MASK)
		    << CPM_IRM_FREQ_CLAMP_GMC_SHIFT;
	min_clamp |= min_t(u16, vote->pf_memss, CPM_IRM_FREQ_CLAMP_MEMSS_MASK)
		     << CPM_IRM_FREQ_CLAMP_MEMSS_SHIFT;
	min_clamp |= min_t(u16, vote->pf_int_ancestor, CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_MASK)
		     << CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_SHIFT;
	min_clamp |= min_t(u16, vote->pf_int_descendant, CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_MASK)
		     << CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_SHIFT;

#if (IS_ENABLED(CONFIG_SOC_MBU) || IS_ENABLED(CONFIG_SOC_LGA))
	{
		u16 default_clamp =
			(CPM_IRM_FREQ_CLAMP_GMC_MASK << CPM_IRM_FREQ_CLAMP_GMC_SHIFT) |
			(CPM_IRM_FREQ_CLAMP_MEMSS_MASK << CPM_IRM_FREQ_CLAMP_MEMSS_SHIFT) |
			(CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_MASK
			 << CPM_IRM_FREQ_CLAMP_INT_ANCESTOR_FAB_SHIFT) |
			(CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_MASK
			 << CPM_IRM_FREQ_CLAMP_INT_DESCENDANT_FAB_SHIFT);

		/* Only set VALID bit if any of the pf_level is not the default */
		if (min_clamp != default_clamp)
			min_clamp |= CPM_IRM_FREQ_CLAMP_VALID_BIT;
	}
#endif

	return min_clamp;
}
