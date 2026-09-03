// SPDX-License-Identifier: GPL-2.0-only
/*
 * SoC specific function definitions for Destiny.
 *
 * Copyright (C) 2023 Google LLC
 */

#include <linux/slab.h>
#include <linux/units.h>
#include <linux/workqueue.h>

#include <gcip/gcip-kci.h>
#include <perf/core/google_pm_qos.h>
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
#include <perf/core/perf_domain.h>
#endif

#include "gxp-core-telemetry.h"
#include "gxp-gem.h"
#include "gxp-kci.h"
#include "gxp-lpm.h"
#include "gxp-pm.h"
#include "mobile-soc-destiny.h"
#include "mobile-soc.h"

/* Least significant 16 bits represent the dsu QoS/frequency in MHz. */
#define COHERENT_FABRIC_QOS_DSU_MASK (0xFFFF)
#define COHERENT_FABRIC_QOS_FACTOR KHZ_PER_MHZ

/* TODO(b/288224048): Use real API for rate. */
static unsigned long fake_pm_rate;
/* TODO(b/357753741): Add fake dsufreq node. */
static u64 fake_request;

void gxp_soc_set_pm_arg_from_state(struct gxp_req_pm_qos_work *work,
				   enum aur_memory_power_state state)
{
}

int gxp_soc_pm_set_rate(unsigned int id, unsigned long rate)
{
	fake_pm_rate = rate;
	return 0;
}

unsigned long gxp_soc_pm_get_rate(struct gxp_dev *gxp, unsigned int id, unsigned long dbg_val)
{
	u32 val, pf_state, freq_khz;

	if (IS_GXP_TEST)
		return fake_pm_rate;

	val = lpm_read_32(gxp, PFSM_STATUS);
	if (!(val & PFSM_STATUS_VALID)) {
		dev_err(gxp->dev, "Invalid PFSM state(reg = %#x).", val);
		return 0;
	}

	pf_state = val & PFSM_STATUS_STATE_MASK;
	freq_khz = gxp_pf_state_to_power_rate(pf_state);
	if (!freq_khz)
		dev_err(gxp->dev, "Invalid pf_state(%u).", pf_state);

	return freq_khz;
}

void gxp_soc_pm_init(struct gxp_dev *gxp)
{
	struct dsu_devfreq *dsu_df = &gxp->soc_data->dsu_df;
	int ret;

	if (!dsu_df->df) {
		dev_warn(gxp->dev, "No DSU devfreq device registered.");
		return;
	}

	/* Initialize the @dsu_df->qos_req handle to be used for later QoS update requests. */
	ret = google_pm_qos_add_devfreq_request(dsu_df->df, &dsu_df->qos_req,
						DEV_PM_QOS_MIN_FREQUENCY,
						PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
	if (ret)
		dev_err(gxp->dev, "google_pm_qos_add_devfreq_request return %d.", ret);
}

void gxp_soc_pm_exit(struct gxp_dev *gxp)
{
	struct dsu_devfreq *dsu_df = &gxp->soc_data->dsu_df;

	if (dsu_df->df)
		google_pm_qos_remove_devfreq_request(dsu_df->df, &dsu_df->qos_req);
}

void gxp_soc_pm_set_request(struct gxp_dev *gxp, enum gxp_soc_qos_request qos_request, u64 value)
{
	struct dsu_devfreq *dsu_df = &gxp->soc_data->dsu_df;
	s32 dsu_val_mhz;
	int ret;

	if (IS_GXP_TEST) {
		fake_request = value;
		return;
	}

	if (qos_request != COHERENT_FABRIC_QOS_REQ)
		dev_err(gxp->dev, "Invalid gxp_soc_qos_request return %u.", qos_request);

	if (!dsu_df->df) {
		dev_warn(gxp->dev, "No DSU devfreq device registered.");
		return;
	}

	/*
	 * Since there is request to update the frequency, cancel the work scheduled for resetting
	 * the dsu frequency.
	 */
	cancel_work_sync(&dsu_df->dsu_reset_work);

	dsu_val_mhz = value & COHERENT_FABRIC_QOS_DSU_MASK;
	ret = dev_pm_qos_update_request(&dsu_df->qos_req, dsu_val_mhz * KHZ_PER_MHZ);
	if (ret < 0)
		dev_err(gxp->dev, "google_pm_qos_update_request return %d.", ret);
}

u64 gxp_soc_pm_get_request(struct gxp_dev *gxp, enum gxp_soc_qos_request qos_request)
{
	struct dsu_devfreq *dsu_df = &gxp->soc_data->dsu_df;
	s32 dsu_val_khz;

	if (IS_GXP_TEST)
		return fake_request;

	if (qos_request != COHERENT_FABRIC_QOS_REQ)
		dev_err(gxp->dev, "Invalid gxp_soc_qos_request return %u.", qos_request);

	if (!dsu_df->df) {
		dev_warn(gxp->dev, "No DSU devfreq device registered. Returning 0.");
		return 0;
	}

	dsu_val_khz = dev_pm_qos_read_value(dsu_df->df->dev.parent, DEV_PM_QOS_MIN_FREQUENCY);
	return (dsu_val_khz / KHZ_PER_MHZ) * COHERENT_FABRIC_QOS_DSU_MASK;
}

void gxp_soc_pm_reset(struct gxp_dev *gxp)
{
	struct dsu_devfreq *dsu_df = &gxp->soc_data->dsu_df;

	if (IS_GXP_TEST) {
		fake_request = 0;
		return;
	}

	if (dsu_df->df)
		/*
		 * Reset DSU frequencies, schedule worker once, will skip if
		 * already scheduled.
		 */
		schedule_work(&dsu_df->dsu_reset_work);
}

static void gxp_soc_dsu_reset_work(struct work_struct *work)
{
	struct dsu_devfreq *dsu_df = container_of(work, struct dsu_devfreq, dsu_reset_work);

	dev_pm_qos_update_request(&dsu_df->qos_req, PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
}

int gxp_soc_init(struct gxp_dev *gxp)
{
	struct devfreq *df;
	struct dsu_devfreq *dsu_df;
	int ret;

	gxp->soc_data = devm_kzalloc(gxp->dev, sizeof(*gxp->soc_data), GFP_KERNEL);
	if (!gxp->soc_data)
		return -ENOMEM;

	ret = gxp_gem_set_reg_resources(gxp);
	if (ret)
		dev_info(gxp->dev, "GEM resources unavailable (ret=%d)", ret);

	dsu_df = &gxp->soc_data->dsu_df;

	/* Get the instance of DSU devfreq device. */
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	df = gs_perf_domain_find_devfreq("dsu", GS_RECOMMENDED_DEVFREQ);
#else /* CONFIG_GS_PERF_DOMAIN */
	df = devfreq_get_devfreq_by_phandle(gxp->dev, "dsu-devfreq", 0);
#endif /* CONFIG_GS_PERF_DOMAIN */
	if (IS_ERR_OR_NULL(df)) {
		dev_warn(gxp->dev, "failed to get devfreq: %ld\n", PTR_ERR(df));
	} else {
		dsu_df->df = df;
		INIT_WORK(&dsu_df->dsu_reset_work, gxp_soc_dsu_reset_work);
	}

	return 0;
}

void gxp_soc_exit(struct gxp_dev *gxp)
{
	struct dsu_devfreq *dsu_df = &gxp->soc_data->dsu_df;

	if (dsu_df->df)
		flush_work(&dsu_df->dsu_reset_work);
}

void gxp_soc_activate_context(struct gxp_dev *gxp, struct gcip_iommu_domain *gdomain,
			      uint core_list)
{
}

void gxp_soc_deactivate_context(struct gxp_dev *gxp, struct gcip_iommu_domain *gdomain,
				uint core_list)
{
}

void gxp_soc_set_iremap_context(struct gxp_dev *gxp)
{
	/* Set SID to 0 */
	gxp_write_32(gxp, GXP_REG_IREMAP_SID, 0x0);
	/* Disable SSID signal. */
	gxp_write_32(gxp, GXP_REG_IREMAP_SSID, 0x0);
}

void gxp_soc_lpm_init(struct gxp_dev *gxp)
{
}

void gxp_soc_lpm_destroy(struct gxp_dev *gxp)
{
}
