// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024 Google LLC
 */

#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/google_vote_manager.h>
#include <trace/events/power.h>
#include <dvfs-helper/google_dvfs_helper.h>

#define CCF_DEVFREQ_TRACE_STR_LEN 32

struct google_ccf_devfreq {
	struct device *dev;

	struct devfreq *devfreq;
	struct devfreq_dev_profile *profile;

	struct clk *clock;

	char cur_freq_trace_name[CCF_DEVFREQ_TRACE_STR_LEN];
};

static int google_ccf_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct google_ccf_devfreq *df = dev_get_drvdata(dev);

	if (IS_ERR_OR_NULL(df->devfreq) || !df->devfreq->previous_freq)
		*freq = clk_get_rate(df->clock);
	else
		*freq = df->devfreq->previous_freq;

	return 0;
}

static int google_ccf_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct google_ccf_devfreq *df = dev_get_drvdata(dev);
	int err;

	err = clk_set_rate(df->clock, *freq);
	if (err) {
		dev_err(df->dev, "failed to set clock. target freq: %lu\n", *freq);
		return err;
	}

	// To skip probe fw and guarantee the freq is an available OPP.
	struct dev_pm_opp *opp;

	opp = devfreq_recommended_opp(df->dev, freq, flags);
	if (IS_ERR(opp)) {
		dev_err(df->dev, "failed to find OPP. requested freq: %lu, err: %ld\n",
			*freq, PTR_ERR(opp));
	} else {
		*freq = dev_pm_opp_get_freq(opp);
	}

	if (trace_clock_set_rate_enabled())
		trace_clock_set_rate(df->cur_freq_trace_name, *freq, raw_smp_processor_id());

	return 0;
}

static void google_ccf_devfreq_remove(struct platform_device *pdev)
{
	struct google_ccf_devfreq *df = platform_get_drvdata(pdev);

	if (!IS_ERR_OR_NULL(df->clock))
		devm_clk_put(df->dev, df->clock);

	if (!IS_ERR_OR_NULL(df->devfreq)) {
		vote_manager_remove_devfreq(df->devfreq);
		google_unregister_devfreq(df->devfreq);
		devm_devfreq_remove_device(df->dev, df->devfreq);
	}
}

static int google_ccf_devfreq_init_devfreq(struct google_ccf_devfreq *df)
{
	struct device *dev = df->dev;
	struct devfreq_dev_profile *profile;
	int err;

	profile = devm_kzalloc(dev, sizeof(*profile), GFP_KERNEL);
	if (!profile)
		return -ENOMEM;

	profile->polling_ms = 0;
	profile->target = google_ccf_devfreq_target;
	profile->get_cur_freq = google_ccf_devfreq_get_cur_freq;

	df->profile = profile;

	err = dvfs_helper_add_opps_to_device(dev, dev->of_node);
	if (err) {
		dev_err(dev, "failed to add OPP table, err: %d\n", err);
		return err;
	}

	df->devfreq = devm_devfreq_add_device(dev, profile, DEVFREQ_GOV_POWERSAVE, NULL);
	if (IS_ERR(df->devfreq)) {
		err = PTR_ERR(df->devfreq);
		dev_err(dev, "failed to add devfreq device, err: %d\n", err);
		return err;
	}

	scnprintf(df->cur_freq_trace_name, CCF_DEVFREQ_TRACE_STR_LEN, "%s_cur_freq",
			dev_name(dev));

	return 0;
}

static int google_ccf_devfreq_init_clock(struct google_ccf_devfreq *df)
{
	struct clk *clock;

	clock = devm_clk_get(df->dev, NULL);
	if (IS_ERR(clock)) {
		dev_err(df->dev, "failed to get clk %ld\n", PTR_ERR(clock));
		return PTR_ERR(clock);
	}

	df->clock = clock;
	return 0;
}

static int google_ccf_devfreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_ccf_devfreq *df;
	int err = 0;

	df = devm_kzalloc(dev, sizeof(*df), GFP_KERNEL);
	if (!df)
		return -ENOMEM;

	df->dev = dev;
	platform_set_drvdata(pdev, df);

	err = google_ccf_devfreq_init_clock(df);
	if (err)
		goto deinit_exit;

	err = google_ccf_devfreq_init_devfreq(df);
	if (err)
		goto deinit_exit;

	err = google_register_devfreq(df->devfreq);
	if (err)
		goto deinit_exit;

	err = vote_manager_init_devfreq(df->devfreq);
	if (err)
		goto deinit_exit;

	return 0;

deinit_exit:
	google_ccf_devfreq_remove(pdev);
	return err;
}

static const struct of_device_id google_ccf_devfreq_of_match_table[] = {
	{
		.compatible = "google,devfreq-ccf",
	},
	{},
};
MODULE_DEVICE_TABLE(of, google_ccf_devfreq_of_match_table);

struct platform_driver google_ccf_devfreq_driver = {
	.driver = {
		.name = "google_ccf_devfreq",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_ccf_devfreq_of_match_table),
	},
	.probe  = google_ccf_devfreq_probe,
	.remove = google_ccf_devfreq_remove,
};

module_platform_driver(google_ccf_devfreq_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google devfreq driver");
MODULE_LICENSE("GPL");
