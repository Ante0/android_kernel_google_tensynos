// SPDX-License-Identifier: GPL-2.0-only

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>

#include "apc_irm_plat.h"
#include "apc_irm_util.h"
#include "google_irm_reg_lga.h"

void apply_vote(void __iomem *base, bool is_sync, struct irm_vote_t *vote)
{
	u32 val;
	int ret;

	/*
	 * We purposefully skip checking if TRIG is clear for asynchronous clients
	 * to minimize latency, which is critical for the CPU IRM vote.
	 * For synchronous clients, we must wait to ensure serialization.
	 */
	if (is_sync) {
		ret = readl_poll_timeout(base + DVFS_REQ_TRIG, val, val == 0, POLL_SLEEP_TIME_IN_US,
					 POLL_TIMEOUT_TIME_IN_US);
		if (ret)
			panic("%s: Timed out waiting for TRIG to clear\n", __func__);
	}

	writel(irm_clamp_bw_val(vote->avg_read_gmc_bw), base + DVFS_REQ_RD_BW_AVG_GMC);
	writel(irm_clamp_bw_val(vote->avg_write_gmc_bw), base + DVFS_REQ_WR_BW_AVG_GMC);
	writel(irm_clamp_bw_val(vote->rt_read_gmc_bw), base + DVFS_REQ_RD_BW_RT_GMC);
	writel(irm_clamp_bw_val(vote->rt_write_gmc_bw), base + DVFS_REQ_WR_BW_RT_GMC);
	writel(irm_clamp_bw_val(vote->peak_read_gmc_bw), base + DVFS_REQ_RD_BW_PEAK_GMC);
	writel(irm_clamp_bw_val(vote->peak_write_gmc_bw), base + DVFS_REQ_WR_BW_PEAK_GMC);

	writel(irm_calculate_min_clamp(vote), base + DVFS_REQ_MIN_CLAMP_GMC);

	writel(irm_clamp_bw_val(vote->avg_read_gslc_bw), base + DVFS_REQ_RD_BW_AVG_GSLC);
	writel(irm_clamp_bw_val(vote->avg_write_gslc_bw), base + DVFS_REQ_WR_BW_AVG_GSLC);
	writel(irm_clamp_bw_val(vote->rt_read_gslc_bw), base + DVFS_REQ_RD_BW_RT_GSLC);
	writel(irm_clamp_bw_val(vote->rt_write_gslc_bw), base + DVFS_REQ_WR_BW_RT_GSLC);
	writel(irm_clamp_bw_val(vote->peak_read_gslc_bw), base + DVFS_REQ_RD_BW_PEAK_GSLC);
	writel(irm_clamp_bw_val(vote->peak_write_gslc_bw), base + DVFS_REQ_WR_BW_PEAK_GSLC);

	// Trigger
	writel(DVFS_TRIG_EN, base + DVFS_REQ_TRIG);
}

void set_irm_subclient_average_read_gslc_bandwidth(struct irm_subclient_t *subclient,
						   u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.avg_read_gslc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_average_read_gslc_bandwidth);

void set_irm_subclient_peak_read_gslc_bandwidth(struct irm_subclient_t *subclient,
						u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.peak_read_gslc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_peak_read_gslc_bandwidth);

void set_irm_subclient_rt_read_gslc_bandwidth(struct irm_subclient_t *subclient, u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.rt_read_gslc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_rt_read_gslc_bandwidth);

void set_irm_subclient_average_write_gslc_bandwidth(struct irm_subclient_t *subclient,
						     u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.avg_write_gslc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_average_write_gslc_bandwidth);

void set_irm_subclient_peak_write_gslc_bandwidth(struct irm_subclient_t *subclient,
						u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.peak_write_gslc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_peak_write_gslc_bandwidth);

void set_irm_subclient_rt_write_gslc_bandwidth(struct irm_subclient_t *subclient, u32 bandwidth_MBps)
{
	if (subclient)
		subclient->working_vote.rt_write_gslc_bw = bandwidth_MBps;
}
EXPORT_SYMBOL_GPL(set_irm_subclient_rt_write_gslc_bandwidth);
