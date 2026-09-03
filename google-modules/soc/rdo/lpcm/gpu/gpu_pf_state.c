// SPDX-License-Identifier: GPL-2.0-only
/*
 * Platform device driver for controlling
 * GPU's LPCM's power frequency states (Local Power Clock Manager)
 *
 * Copyright (C) 2023 Google LLC.
 */

#include <linux/clk.h>
#include <linux/dev_printk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/iopoll.h>
#include <linux/clk-provider.h>
#include <linux/container_of.h>
#include <linux/pm_runtime.h>
#include <linux/printk.h>
#include <linux/iopoll.h>
#include <linux/debugfs.h>

#include <dvfs-helper/google_dvfs_helper.h>

#define LPCM_STATUS_TIMEOUT_US 1000
/* TODO(b/484302093): Investigate why DVFS transition takes longer on P26 */
#define LPCM_STATUS_TIMEOUT_P26_US 2000
#define LPCM_STATUS_INTERVAL_US 10
#define EMULATION_TIMEOUT_MULTIPLIER 30

#define PFSM_MODE_SW 1
#define UNUSED_PF_STATE -1
#define INVALID_PF_STATE -1
#define PFSM_STATUS_STATE_VALID_BITMASK BIT(5)
#define PFSM_STATUS_PFSM_STS_BITMASK BIT(6)
#define PFSM_STATUS_SW_SEQ_DONE_BITMASK BIT(7)
#define PFSM_STATUS_PFSM_ERR_BITMASK BIT(8)
#define PFSM_STATUS_SEQ_ERR_BITMASK BIT(9)
#define PFSM_STATUS_CURR_STATE_BITMASK (BIT(5) - 1)
#define MAX_PF_STATE_NUM 32

#define FABRIC_VOTE_VALID_BIT BIT(15)
#define FABRIC_IF1_BITS GENMASK(14, 12)
#define FABRIC_IF2_BITS GENMASK(11, 8)
#define FABRIC_MEMSS_BITS GENMASK(7, 4)
#define FABRIC_GMC_BITS GENMASK(3, 0)
#define FABRIC_VOTE(fabhbw_vote, memss_vote, gmc_vote) \
	(FIELD_PREP(FABRIC_VOTE_VALID_BIT, 1) | \
	 FIELD_PREP(FABRIC_IF1_BITS, 0) | \
	 FIELD_PREP(FABRIC_IF2_BITS, fabhbw_vote) | \
	 FIELD_PREP(FABRIC_MEMSS_BITS, memss_vote) | \
	 FIELD_PREP(FABRIC_GMC_BITS, gmc_vote))
#define FABRIC_VOTE_TRIG 0x1

#define to_gpu_pf_state_clk(_hw) container_of(_hw, struct gpu_pf_state_clk, hw)

#define DISABLE_FABRIC_VOTES_INIT false
#define SCALE_FABRIC_VOTES_PERCENT_INIT 100
/*
 * Number of elements aligns with the number of columns for pf_state_rates under
 * gpu_pf_state device in the device tree
 */
enum GPU_PF_STATE_ELEMS {
	FABRIC_FABHBW_VOTE,
	FABRIC_MEMSS_VOTE,
	FABRIC_GMC_VOTE,
	GPU_PF_STATE_TOTAL_ELEM_TYPES,
};

enum soc_type {
	SOC_RDO,
	SOC_P25,
	SOC_P26,
};

struct gpu_pf_state_dev_desc {
	u32 pfsm_config_offset;
	u32 pfsm_start_offset;
	u32 pfsm_status_offset;
	u8 pfsm_sw_pfs_target_shift;
	u32 apc_gpu_req_ltv_gmc_offset;
	u32 apc_gpu_req_trig_offset;
	enum soc_type soc;
};

struct gpu_pf_state_clk {
	struct device *dev;
	struct clk_hw hw;
	void __iomem *base;

	u32 pf_state_rates[MAX_PF_STATE_NUM][GPU_PF_STATE_TOTAL_ELEM_TYPES];
	int pf_state_num;
	struct dvfs_domain_info *dvfs_helper_info;
	/* saved_rate is used to preserve clock's rate across power resets */
	unsigned long saved_rate;
	/* pg_rate is used to set the lowest opp point during GPU power gating */
	unsigned long pg_rate;
	const struct gpu_pf_state_dev_desc *hw_desc;
	struct mutex gpu_lpcm_mutex;
	void __iomem *fabric_vote_base;
	struct dentry *debugfs_root;
	bool disable_fabric_votes;
	u16 scale_fabric_votes_percent;
	unsigned int timeout_multiplier;
};

static const struct gpu_pf_state_dev_desc gpu_pf_state_dev_desc_rdo = {
	.pfsm_config_offset = 0x288,
	.pfsm_start_offset = 0x28c,
	.pfsm_status_offset = 0x294,
	.pfsm_sw_pfs_target_shift = 2,
	.apc_gpu_req_ltv_gmc_offset = 0x1c,
	.apc_gpu_req_trig_offset = 0x40,
	.soc = SOC_RDO,
};

static const struct gpu_pf_state_dev_desc gpu_pf_state_dev_desc_p25 = {
	.pfsm_config_offset = 0x0,
	.pfsm_start_offset = 0x4,
	.pfsm_status_offset = 0x8,
	.pfsm_sw_pfs_target_shift = 1,
	.apc_gpu_req_ltv_gmc_offset = 0x1c,
	.apc_gpu_req_trig_offset = 0x40,
	.soc = SOC_P25,
};

static const struct gpu_pf_state_dev_desc gpu_pf_state_dev_desc_p26 = {
	.pfsm_config_offset = 0x0,
	.pfsm_start_offset = 0x4,
	.pfsm_status_offset = 0x8,
	.pfsm_sw_pfs_target_shift = 1,
	.apc_gpu_req_ltv_gmc_offset = 0x1c,
	.apc_gpu_req_trig_offset = 0x24,
	.soc = SOC_P26,
};

/*
 * P26 specific, according to b/414282372 PFSM_STS register needs to be idle
 * 4 consecutive times to ensure LPCM HW is ready to start new operation.
 */
#define PFSM_IDLE_POLL_NUMBER 4

struct lpcm_pfsm_idle_helper {
	bool idle;
	int pfsm_status_reg_val;
};

static struct lpcm_pfsm_idle_helper
gpu_pf_state_pfsm_status_idle_helper(struct gpu_pf_state_clk *gpu_pf_state)
{
	struct lpcm_pfsm_idle_helper ret;
	int pfsm_status_reg_val[PFSM_IDLE_POLL_NUMBER];

	for (int i = 0; i < PFSM_IDLE_POLL_NUMBER; i++)
		pfsm_status_reg_val[i] =
			readl(gpu_pf_state->base +
			      gpu_pf_state->hw_desc->pfsm_status_offset);

	for (int i = 0; i < PFSM_IDLE_POLL_NUMBER; i++) {
		if (pfsm_status_reg_val[i] & PFSM_STATUS_PFSM_STS_BITMASK) {
			ret.idle = false;
			ret.pfsm_status_reg_val = pfsm_status_reg_val[i];
			return ret;
		}
	}

	ret.idle = true;
	ret.pfsm_status_reg_val =
		pfsm_status_reg_val[PFSM_IDLE_POLL_NUMBER - 1];

	return ret;
}

static u32 get_pf_pg_rate(struct dvfs_domain_info const * const di,
			  int pf_state_num)
{
	u32 min_freq = UINT_MAX;

	for (int i = 0; i < pf_state_num; ++i) {
		u32 freq = (u32)dvfs_helper_lvl_to_freq_exact(di, i);
		if (freq < min_freq)
			min_freq = freq;
	};
	return min_freq;
}

static u32 get_scaled_fabric_vote(u32 fabric_vote, u16 scale_percent)
{
	return ((fabric_vote * scale_percent) + 50) / 100;
}

static void gpu_set_fabric_votes(struct clk_hw *hw, int gpu_pf_state)
{
	struct gpu_pf_state_clk *g_pf_state = to_gpu_pf_state_clk(hw);
	u32 fabhbw_vote, memss_vote, gmc_vote, vote;
	u32 fabhbw_max_vote, memss_max_vote, gmc_max_vote;

	fabhbw_max_vote =
		g_pf_state->pf_state_rates[g_pf_state->pf_state_num-1][FABRIC_FABHBW_VOTE];
	memss_max_vote =
		g_pf_state->pf_state_rates[g_pf_state->pf_state_num-1][FABRIC_MEMSS_VOTE];
	gmc_max_vote =
		g_pf_state->pf_state_rates[g_pf_state->pf_state_num-1][FABRIC_GMC_VOTE];

	fabhbw_vote = clamp(get_scaled_fabric_vote(
				g_pf_state->pf_state_rates[gpu_pf_state][FABRIC_FABHBW_VOTE],
				g_pf_state->scale_fabric_votes_percent), 0, fabhbw_max_vote);
	memss_vote = clamp(get_scaled_fabric_vote(
				g_pf_state->pf_state_rates[gpu_pf_state][FABRIC_MEMSS_VOTE],
				g_pf_state->scale_fabric_votes_percent), 0, memss_max_vote);
	gmc_vote = clamp(get_scaled_fabric_vote(
				g_pf_state->pf_state_rates[gpu_pf_state][FABRIC_GMC_VOTE],
				g_pf_state->scale_fabric_votes_percent), 0, gmc_max_vote);
	dev_dbg(g_pf_state->dev,
		"GPU Frequency <%lld> fabhbw_vote <%u> memss_vote <%u> gmc_vote <%u>",
		dvfs_helper_lvl_to_freq_exact(g_pf_state->dvfs_helper_info, gpu_pf_state),
		fabhbw_vote, memss_vote, gmc_vote);

	vote = FABRIC_VOTE(fabhbw_vote, memss_vote, gmc_vote);
	writel(vote, g_pf_state->fabric_vote_base +
			g_pf_state->hw_desc->apc_gpu_req_ltv_gmc_offset);
	writel(FABRIC_VOTE_TRIG, g_pf_state->fabric_vote_base +
			g_pf_state->hw_desc->apc_gpu_req_trig_offset);
}

static int gpu_pf_state_set_fabric_votes_from_rate(struct clk_hw *hw,
						   unsigned long rate)
{
	struct gpu_pf_state_clk *gpu_pf_state = to_gpu_pf_state_clk(hw);
	int pf_state =
		dvfs_helper_freq_to_lvl_exact(gpu_pf_state->dvfs_helper_info, rate);

	if (pf_state == INVALID_PF_STATE) {
		dev_err(gpu_pf_state->dev,
			"Cannot find corresponding pf_state for rate=%lu\n",
			rate);
		return -EINVAL;
	}
	gpu_set_fabric_votes(hw, pf_state);

	return 0;
}

/*
 * The precondition for calling gpu_pf_state_clk_set_rate_internal is that the
 * power is ON.
 */
static int gpu_pf_state_clk_set_rate_internal(struct clk_hw *hw,
					      unsigned long rate)
{
	int target_pf_state, ret, pfsm_status_reg_val, current_pf_state;
	struct gpu_pf_state_clk *gpu_pf_state = to_gpu_pf_state_clk(hw);
	struct device *dev;
	u32 pfsm_config;
	struct lpcm_pfsm_idle_helper pfsm_idle;

	dev = gpu_pf_state->dev;
	ret = 0;

	dev_dbg(dev, "target: %lu", rate);
	target_pf_state =
		dvfs_helper_freq_to_lvl_exact(gpu_pf_state->dvfs_helper_info, rate);

	if (target_pf_state < 0) {
		dev_err(dev, "Cannot find corresponding pf_state for rate=%lu\n", rate);
		return target_pf_state;
	}

	mutex_lock(&gpu_pf_state->gpu_lpcm_mutex);
	/*
	 * Upon the gpu_pf_state driver's rpm resume, it restores the previously
	 * saved rate. To prevent the PFSM status getting stuck in the RUNNING
	 * state (pfsm_sts = 1) when setting the same PF state again, returns 0
	 * without performing any action.
	 */
	pfsm_status_reg_val = readl(gpu_pf_state->base +
				    gpu_pf_state->hw_desc->pfsm_status_offset);
	current_pf_state = pfsm_status_reg_val & PFSM_STATUS_CURR_STATE_BITMASK;
	if (target_pf_state == current_pf_state)
		goto set_rate_exit;

	/* Check if the previous request completed to prevent sending a request when the
	 * previous one is still in progress
	 */
	if (gpu_pf_state->hw_desc->soc == SOC_P26) {
		pfsm_idle = gpu_pf_state_pfsm_status_idle_helper(gpu_pf_state);
		if (!pfsm_idle.idle || (pfsm_idle.pfsm_status_reg_val &
					PFSM_STATUS_STATE_VALID_BITMASK) == 0) {
			dev_err(dev, "Cannot trigger LPCM request: %#X",
				pfsm_status_reg_val);
			ret = -EIO;
			goto set_rate_exit;
		}
	} else {
		if ((pfsm_status_reg_val & PFSM_STATUS_STATE_VALID_BITMASK) ==
			    0 ||
		    (pfsm_status_reg_val & PFSM_STATUS_PFSM_STS_BITMASK)) {
			dev_err(dev, "Cannot trigger LPCM request: %#X",
				pfsm_status_reg_val);
			ret = -EIO;
			goto set_rate_exit;
		}
	}

	pfsm_config = PFSM_MODE_SW |
		      (target_pf_state << gpu_pf_state->hw_desc->pfsm_sw_pfs_target_shift);
	writel(pfsm_config, gpu_pf_state->base +
	       gpu_pf_state->hw_desc->pfsm_config_offset);
	writel(1, gpu_pf_state->base +
	       gpu_pf_state->hw_desc->pfsm_start_offset);

	if (gpu_pf_state->hw_desc->soc == SOC_P26) {
		ret = readx_poll_timeout(gpu_pf_state_pfsm_status_idle_helper,
					 gpu_pf_state, pfsm_idle,
					 pfsm_idle.idle == true,
					 LPCM_STATUS_INTERVAL_US *
						 gpu_pf_state
							 ->timeout_multiplier,
					 LPCM_STATUS_TIMEOUT_P26_US *
						 gpu_pf_state
							 ->timeout_multiplier);

		pfsm_status_reg_val = pfsm_idle.pfsm_status_reg_val;
	} else {
		ret = readl_poll_timeout(gpu_pf_state->base +
						 gpu_pf_state->hw_desc
							 ->pfsm_status_offset,
					 pfsm_status_reg_val,
					 (pfsm_status_reg_val &
					  PFSM_STATUS_CURR_STATE_BITMASK) ==
						 target_pf_state,
					 LPCM_STATUS_INTERVAL_US *
						 gpu_pf_state
							 ->timeout_multiplier,
					 LPCM_STATUS_TIMEOUT_US *
						 gpu_pf_state
							 ->timeout_multiplier);
	}

	dev_dbg(dev, "pfsm status register value = %u\n", pfsm_status_reg_val);
	current_pf_state = pfsm_status_reg_val & PFSM_STATUS_CURR_STATE_BITMASK;
	if (ret) {
		dev_err(dev, "timeout while updating pf state, status = %#X",
			pfsm_status_reg_val);
		goto set_rate_exit;
	}

	if ((pfsm_status_reg_val & PFSM_STATUS_STATE_VALID_BITMASK) == 0 ||
	    pfsm_status_reg_val & PFSM_STATUS_PFSM_ERR_BITMASK ||
	    pfsm_status_reg_val & PFSM_STATUS_SEQ_ERR_BITMASK ||
	    current_pf_state != target_pf_state) {
		dev_err(dev, "error occurred while updating pf state, status = %#X",
			pfsm_status_reg_val);
		ret = -EIO;
		goto set_rate_exit;
	}
	if (rate != gpu_pf_state->pg_rate)
		gpu_pf_state->saved_rate = rate;

	if (!gpu_pf_state->disable_fabric_votes)
		gpu_set_fabric_votes(hw, target_pf_state);

set_rate_exit:
	mutex_unlock(&gpu_pf_state->gpu_lpcm_mutex);
	return ret;
}

static int gpu_pf_state_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	struct gpu_pf_state_clk *g_gpu_pf_state = to_gpu_pf_state_clk(hw);
	struct device *dev = g_gpu_pf_state->dev;
	int ret;

	if (!pm_runtime_get_if_active(dev)) {
		dev_dbg(dev,
			"Calling set rate when power is OFF. Rate will be set once power is restored");
		g_gpu_pf_state->saved_rate = rate;
		return 0;
	}
	ret = gpu_pf_state_clk_set_rate_internal(hw, rate);
	pm_runtime_put(dev);
	return ret;
}

static long gpu_pf_state_clk_round_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long *parent_rate)
{
	struct gpu_pf_state_clk *gpu_pf_state = to_gpu_pf_state_clk(hw);
	s64 freq;

	freq = dvfs_helper_round_rate(gpu_pf_state->dvfs_helper_info, rate);

	if (freq <= 0) {
		dev_err(gpu_pf_state->dev, "Error while rounding rate: %lu", rate);
	}

	return (long)freq;
}

static unsigned long gpu_pf_state_clk_recalc_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct gpu_pf_state_clk *g_gpu_pf_state = to_gpu_pf_state_clk(hw);
	int pfsm_status_reg_val, current_pf_state, ret;
	struct device *dev = g_gpu_pf_state->dev;
	bool pm_runtime_enabled = true;
	unsigned long long freq;

	ret = pm_runtime_get_if_active(dev);
	/*
	 * when clients call clk_set_rate(), clk core invokes recalc_rate function
	 * to update the cached rate. If power is currently OFF, return saved_rate
	 * to ensure that cached rate holds the correct value once power is restored.
	 */
	if (!ret)
		return g_gpu_pf_state->saved_rate;
	/*
	 * when recalc_rate is called for the first time during clock registration
	 * in the probe function, pm_runtime is not enabled.
	 */
	if (ret == -EINVAL)
		pm_runtime_enabled = false;

	pfsm_status_reg_val = readl(g_gpu_pf_state->base +
				    g_gpu_pf_state->hw_desc->pfsm_status_offset);
	current_pf_state = pfsm_status_reg_val & PFSM_STATUS_CURR_STATE_BITMASK;

	dev_dbg(g_gpu_pf_state->dev, "current pf_state is %d",
		current_pf_state);

	freq = dvfs_helper_lvl_to_freq_floor(g_gpu_pf_state->dvfs_helper_info,
			current_pf_state);

	if (!freq)
		dev_err(dev, "Error getting freq from lvl");

	if (pm_runtime_enabled)
		pm_runtime_put(dev);

	return (unsigned long)freq;
}

static const struct clk_ops gpu_pf_state_clk_ops = {
	.set_rate = gpu_pf_state_clk_set_rate,
	.round_rate = gpu_pf_state_clk_round_rate,
	.recalc_rate = gpu_pf_state_clk_recalc_rate,
};

static int gpu_pf_state_suspend(struct device *dev)
{
	struct gpu_pf_state_clk *clk = dev_get_drvdata(dev);

	if (clk->disable_fabric_votes)
		return 0;

	int ret = gpu_pf_state_set_fabric_votes_from_rate(&clk->hw, clk->pg_rate);

	if (ret)
		dev_err(dev, "Fabric vote (corresponding to rate %ld) around suspend failed. rc: %d\n",
			clk->pg_rate, ret);
	else
		dev_dbg(dev, "suspending device, fabric vote corresponds to rate = %ld",
			clk->pg_rate);

	/*
	 * The GPU driver (pvrsrvkm) has a link to this driver.
	 * Whilst pvrsrvkm calls pm_runtime_put_sync, an error should not be
	 * returned here if a frequency or fabric vote request failed. This would propagate
	 * back to pvrsrvkm as though a power-off failure had occurred which is not the case here
	 */
	return 0;
}

static int gpu_pf_state_resume(struct device *dev)
{
	struct gpu_pf_state_clk *clk = dev_get_drvdata(dev);
	/*
	 * gpu_pf_state_resume callback is called after power is ON, so at this point
	 * we do not need to check for power state before calling set_rate
	 */
	int ret = gpu_pf_state_clk_set_rate_internal(&clk->hw, clk->saved_rate);

	if (ret)
		dev_err(dev, "Restoring saved_rate (%ld) on resume failed %d\n", clk->saved_rate,
			ret);
	else
		dev_dbg(dev, "resuming device, saved_rate = %ld", clk->saved_rate);

	/* Restore fabric votes on resume */
	if (clk->disable_fabric_votes)
		return 0;

	ret = gpu_pf_state_set_fabric_votes_from_rate(&clk->hw, clk->saved_rate);

	if (ret)
		dev_err(dev, "Fabric vote (corresponding to rate %ld) around resume failed. rc: %d\n",
			clk->saved_rate, ret);
	else
		dev_dbg(dev, "resuming device, fabric vote corresponds to rate = %ld",
			clk->saved_rate);

	/*
	 * The GPU driver (pvrsrvkm) has a link to this driver.
	 * Whilst pvrsrvkm calls pm_runtime_resume_and_get, an error should not be
	 * returned here if a frequency or fabric vote request failed. This would propagate
	 * back to pvrsrvkm as though a power-on failure had occurred which is not the case here
	 */
	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(gpu_pf_state_dev_pm_ops, gpu_pf_state_suspend,
				 gpu_pf_state_resume, NULL);

static int get_disable_fabric_votes(void *data, u64 *val)
{
	struct gpu_pf_state_clk *clk = dev_get_drvdata(data);

	*val = clk->disable_fabric_votes;
	return 0;
}

static int set_disable_fabric_votes(void *data, u64 val)
{
	struct gpu_pf_state_clk *clk = dev_get_drvdata(data);
	unsigned long clk_rate;

	clk->disable_fabric_votes = (bool)val;

	if (clk->disable_fabric_votes)
		clk_rate = clk->pg_rate;
	else
		clk_rate = clk->saved_rate;

	gpu_pf_state_set_fabric_votes_from_rate(&clk->hw, clk_rate);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_disable_fabric_votes, get_disable_fabric_votes,
				set_disable_fabric_votes, "%llu\n");

static void gpu_pf_state_debug_init(struct device *dev)
{
	struct gpu_pf_state_clk *clk = dev_get_drvdata(dev);

	clk->debugfs_root = debugfs_create_dir("gpu_fabric_votes", NULL);
	debugfs_create_file("disable_fabric_votes", MAY_READ | MAY_WRITE, clk->debugfs_root,
				dev, &fops_disable_fabric_votes);
	debugfs_create_u16("scale_fabric_votes_percent", MAY_READ | MAY_WRITE, clk->debugfs_root,
				&clk->scale_fabric_votes_percent);
}

static int gpu_pf_state_probe(struct platform_device *pdev)
{
	void __iomem *dev_base_address;
	int ret, count_elems;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct clk_init_data init = {};
	struct gpu_pf_state_clk *g_pf_state;
	struct resource *res;
	const struct gpu_pf_state_dev_desc *hw_desc = device_get_match_data(dev);
	static const char *pf_state_rates_str = "pf_state_rates";

	if (!hw_desc)
		return -EINVAL;
	dev_base_address = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev_base_address))
		return PTR_ERR(dev_base_address);

	g_pf_state = devm_kzalloc(dev, sizeof(*g_pf_state), GFP_KERNEL);
	if (!g_pf_state)
		return -ENOMEM;

	g_pf_state->base = dev_base_address;
	g_pf_state->dev = dev;
	g_pf_state->hw_desc = hw_desc;
	mutex_init(&g_pf_state->gpu_lpcm_mutex);

	count_elems = of_property_count_elems_of_size(node, pf_state_rates_str, sizeof(u32));
	g_pf_state->pf_state_num = count_elems / GPU_PF_STATE_TOTAL_ELEM_TYPES;
	if (g_pf_state->pf_state_num < 0) {
		dev_err(dev,
			"failed to read number of elements of %s\n", pf_state_rates_str);
		return -EINVAL;
	}

	if (g_pf_state->pf_state_num == 0) {
		dev_err(dev, "%s is empty\n", pf_state_rates_str);
		return -EINVAL;
	}

	if (g_pf_state->pf_state_num > MAX_PF_STATE_NUM) {
		dev_err(dev,
			"number of pf state exceeded max supported number: %d > %d\n",
			g_pf_state->pf_state_num, MAX_PF_STATE_NUM);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(node, pf_state_rates_str,
				(u32 *)&g_pf_state->pf_state_rates, count_elems);
	if (ret)
		return ret;

	ret = dvfs_helper_get_domain_info(dev, node, &g_pf_state->dvfs_helper_info);
	if (ret) {
		dev_err(dev, "Failed to get DVFS helper info");
		return ret;
	}

	res = platform_get_resource_byname(to_platform_device(dev),
				IORESOURCE_MEM, "fabric_vote");
	if (!res) {
		dev_err(dev, "Cannot find APC_GPU_CSRS register block address in the device tree");
		return -ENXIO;
	}
	g_pf_state->fabric_vote_base = devm_ioremap(dev, res->start, resource_size(res));
	if (IS_ERR(g_pf_state->fabric_vote_base)) {
		dev_err(dev, "Cannot map APC_GPU_CSRS register block for fabric voting");
		return -EINVAL;
	}

	if (of_property_present(node, "in_emulation"))
		g_pf_state->timeout_multiplier = EMULATION_TIMEOUT_MULTIPLIER;
	else
		g_pf_state->timeout_multiplier = 1;

	init.name = "gpu_pf_state_clk";
	init.ops = &gpu_pf_state_clk_ops;
	g_pf_state->hw.init = &init;

	ret = devm_clk_hw_register(dev, &g_pf_state->hw);
	if (ret)
		return ret;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &g_pf_state->hw);
	if (ret)
		return ret;
	g_pf_state->saved_rate = clk_get_rate(g_pf_state->hw.clk);
	g_pf_state->pg_rate = get_pf_pg_rate(g_pf_state->dvfs_helper_info,
					     g_pf_state->pf_state_num);
	g_pf_state->disable_fabric_votes = DISABLE_FABRIC_VOTES_INIT;
	g_pf_state->scale_fabric_votes_percent = SCALE_FABRIC_VOTES_PERCENT_INIT;

	dev_set_drvdata(dev, g_pf_state);

	gpu_pf_state_debug_init(dev);

	pm_runtime_set_active(dev);
	/*
	 * The clk core will invoke clk_pm_runtime_get and clk_pm_runtime_put to
	 * manage the power state of the power domain if the rpm_enabled flag is
	 * set to true before registering the clock. This means that the clk core
	 * will automatically power on the domain when get_rate or set_rate
	 * operations are called and power off the device when these operations
	 * are complete. We want to prevent this behavior because powering off
	 * the device can cause the pf_state to be reset. Therefore, we enable
	 * pm_runtime after registering the gpu_pf_state clk.
	 */
	pm_runtime_enable(dev);

	return 0;
};

static void gpu_pf_state_debug_deinit(struct device *dev)
{
	struct gpu_pf_state_clk *clk = dev_get_drvdata(dev);

	debugfs_remove_recursive(clk->debugfs_root);
}

static void gpu_pf_state_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	gpu_pf_state_debug_deinit(dev);
}

static const struct of_device_id gpu_pf_state_of_match_table[] = {
	{ .compatible = "google,rdo-gpu-pf-state",
	  .data = &gpu_pf_state_dev_desc_rdo },
	{ .compatible = "google,lga-gpu-pf-state",
	  .data = &gpu_pf_state_dev_desc_p25 },
	{ .compatible = "google,mbu-gpu-pf-state",
	  .data = &gpu_pf_state_dev_desc_p26 },
	{},
};

static struct platform_driver gpu_pf_state_driver = {
	.probe = gpu_pf_state_probe,
	.remove = gpu_pf_state_remove,
	.driver = {
		.name = "google-gpu-pf-state",
		.owner = THIS_MODULE,
		.pm = pm_ptr(&gpu_pf_state_dev_pm_ops),
		.of_match_table = of_match_ptr(gpu_pf_state_of_match_table),
	},
};

module_platform_driver(gpu_pf_state_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("google gpu pf state");
MODULE_LICENSE("GPL");
