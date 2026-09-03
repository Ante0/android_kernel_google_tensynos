// SPDX-License-Identifier: GPL-2.0-only
/* Copyright 2023 Google LLC */
#include <linux/devfreq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <perf/core/google_pm_qos.h>
#include <perf/core/google_vote_manager.h>
#include <trace/events/power.h>
#include <dvfs-helper/google_dvfs_helper.h>

struct google_dsufreq {
	struct device *dev;
	struct devfreq *devfreq;

	void __iomem *base_addr;
	struct regmap *regmap;

	unsigned long freq;
};

static inline int google_dsufreq_table_find_index(struct device *dev, unsigned long freq)
{
	struct dev_pm_opp *opp;
	int level;

	opp = dev_pm_opp_find_freq_ceil(dev, &freq);
	if (IS_ERR(opp)) {
		dev_err(dev, "failed to find OPP for %luHz: %pe\n", freq, opp);
		return PTR_ERR(opp);
	}

	level = dev_pm_opp_get_level(opp);
	return level;
}

static inline unsigned long google_dsufreq_table_find_freq(struct device *dev, unsigned int level)
{
	struct dev_pm_opp *opp;
	unsigned long freq;

	opp = dev_pm_opp_find_level_exact(dev, level);
	if (IS_ERR(opp)) {
		dev_err(dev, "failed to find OPP for level %u: %pe\n", level, opp);
		return PTR_ERR(opp);
	}

	freq = dev_pm_opp_get_freq(opp);
	return freq;
}

static int google_dsufreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct google_dsufreq *dsufreq = platform_get_drvdata(pdev);
	struct dev_pm_opp *opp;
	unsigned int index = 0;

	// Map the requested freq with the ones defined in the OPP table.
	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);
	*freq = dev_pm_opp_get_freq(opp);

	index = google_dsufreq_table_find_index(dev, *freq);
	if (index < 0) {
		dev_err(dev, "Failed to map freq %ld with index", *freq);
		return index;
	}
	dev_dbg(dsufreq->dev, "Set dsu freq to (%lu)\n", *freq);
	dsufreq->freq = *freq;

	if (trace_clock_set_rate_enabled())
		trace_clock_set_rate("dsu_cur_freq", *freq, raw_smp_processor_id());

	regmap_write(dsufreq->regmap, 0x0, index);
	return 0;
}

static int google_dsufreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	struct google_dsufreq *dsufreq = platform_get_drvdata(pdev);
	unsigned long freq_from_csr;
	unsigned int index;

	regmap_read(dsufreq->regmap, 0x0, &index);
	freq_from_csr = google_dsufreq_table_find_freq(dev, index);
	if (dsufreq->freq != freq_from_csr)
		dev_warn(dsufreq->dev, "The freq of dsu in cache: %lu, freq from CSR: %lu\n",
			 dsufreq->freq, freq_from_csr);
	*freq = freq_from_csr;
	return 0;
}

static struct devfreq_dev_profile google_dsufreq_profile = {
	.polling_ms = 0,
	.target = google_dsufreq_target,
	.get_cur_freq = google_dsufreq_get_cur_freq,
};

static void google_dsufreq_remove(struct platform_device *pdev)
{
	struct google_dsufreq *dsufreq = platform_get_drvdata(pdev);

	dev_pm_opp_of_remove_table(&pdev->dev);

	if (!IS_ERR_OR_NULL(dsufreq->devfreq)) {
		vote_manager_remove_devfreq(dsufreq->devfreq);
		google_unregister_devfreq(dsufreq->devfreq);
		devm_devfreq_remove_device(&pdev->dev, dsufreq->devfreq);
	}
}

static const struct regmap_config google_dsufreq_regmap_cfg = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32
};

static int google_dsufreq_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct google_dsufreq *dsufreq;
	int ret = 0;

	dsufreq = devm_kzalloc(dev, sizeof(*dsufreq), GFP_KERNEL);
	if (!dsufreq)
		return -ENOMEM;
	dsufreq->dev = dev;

	dsufreq->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dsufreq->base_addr))
		return PTR_ERR(dsufreq->base_addr);

	dsufreq->regmap = devm_regmap_init_mmio(dev, dsufreq->base_addr,
						&google_dsufreq_regmap_cfg);
	if (IS_ERR(dsufreq->regmap))
		return PTR_ERR(dsufreq->regmap);

	platform_set_drvdata(pdev, dsufreq);

	ret = dvfs_helper_add_opps_to_device(dev, dev->of_node);
	if (ret) {
		dev_err(dev, "failed to add OPP table: %d\n", ret);
		goto probe_exit;
	}

	dsufreq->devfreq = devm_devfreq_add_device(dev, &google_dsufreq_profile,
						   DEVFREQ_GOV_POWERSAVE, NULL);

	if (IS_ERR(dsufreq->devfreq)) {
		ret = PTR_ERR(dsufreq->devfreq);
		dev_err(dev, "failed to add devfreq device: %pe\n",
			dsufreq->devfreq);
		goto probe_exit;
	}

	ret = google_register_devfreq(dsufreq->devfreq);
	if (ret)
		goto probe_exit;

	ret = vote_manager_init_devfreq(dsufreq->devfreq);
	if (ret)
		goto probe_exit;

	return ret;

probe_exit:
	google_dsufreq_remove(pdev);
	return ret;
}

static const struct of_device_id google_dsufreq_of_match_table[] = {
	{ .compatible = "google,dsufreq", },
	{},
};
MODULE_DEVICE_TABLE(of, google_dsufreq_of_match_table);

struct platform_driver google_dsufreq_driver = {
	.driver = {
		.name = "google-dsufreq",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(google_dsufreq_of_match_table),
	},
	.probe  = google_dsufreq_probe,
	.remove = google_dsufreq_remove,
};

module_platform_driver(google_dsufreq_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google dsufreq driver");
MODULE_LICENSE("GPL");
