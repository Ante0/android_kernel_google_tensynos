// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2025 Google LLC.
 *
 * LPM-based power controller that provides power domain control by directly
 * talking to the LPM registers. Power domain control is usually handled by
 * the CPM (Central Power Manager), where the CPM either:
 * 1. Talks to the LPB (for handling SSWRP-level top domain), or
 * 2. Talks to the LPM (for handling subdomains).
 *
 * But:
 * 1. Depending on the environment, we might not have CPM (and thus no LPB)
 * 2. Some domains like GPU domains need genpd to directly talk to LPCM for
 *    latency reasons.
 * 3. CPM <-> LPM communication might not be modelled in the CPM FW yet.
 *
 * Due to the above reasons, this genpd provider provides power domain control
 * without having to go through the CPM.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/mfd/syscon.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>

#include <dt-bindings/power/genpd_lga.h>
#include <dt-bindings/power/genpd_mbu.h>

#include "lpm-debug.h"
#include "lpm-pm-domains.h"
#include "sequences/sequence.h"

#define CREATE_TRACE_POINTS
#include "lpm-pm-domains-trace.h"


static inline struct power_domain *to_power_domain(struct generic_pm_domain *d)
{
	return container_of(d, struct power_domain, genpd);
}

static int noop_resume_noirq_overwrite(struct device *dev)
{
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	dev_dbg(dev, "Skipping power_domain resume_noirq call for the device.");

	return pm && pm->resume_noirq ? pm->resume_noirq(dev) : 0;
}

static int register_power_domain(struct device_node *pwr_domain_np,
				 struct power_domain *pd)
{
	struct device *dev = pd->dev;
	bool expected_on = false;
	bool domain_is_on = false;
	bool track_from_zero = false;
	int ret;

	if (!pd->ops->power_on || !pd->ops->power_off) {
		dev_err(pd->dev, "%s: Missing power on/off callback(s).\n",
			pd->genpd.name);
		return -EINVAL;
	}

	if (of_property_read_bool(pwr_domain_np, "on-at-init")) {
		domain_is_on = true;
		track_from_zero = true;
	} else if (of_property_read_bool(pwr_domain_np, "force-on")) {
		ret = pd->ops->power_on(pd);
		if (ret) {
			dev_err(dev, "%s: Failed to power-on\n",
				pd->genpd.name);
			return ret;
		}
		domain_is_on = true;
	}
	/* always-on domain is expected to be ON already */
	if (of_property_read_bool(pwr_domain_np, "always-on")) {
		pd->genpd.flags |= GENPD_FLAG_ALWAYS_ON;
		expected_on = true;
	}
	/* rpm-always-on domain is expected to be ON already */
	if (of_property_read_bool(pwr_domain_np, "rpm-always-on")) {
		pd->genpd.flags |= GENPD_FLAG_RPM_ALWAYS_ON;
		expected_on = true;
	}
	if (expected_on && !domain_is_on) {
		ret = -EINVAL;
		dev_err(dev, "%s: Domain is (rpm-)always-on, but it is neither on-at-init nor force-on\n",
			pd->genpd.name);
		return ret;
	}
	if (of_property_read_bool(pwr_domain_np, "active-wakeup"))
		pd->genpd.flags |= GENPD_FLAG_ACTIVE_WAKEUP;

	ret = pm_genpd_init(&pd->genpd, /* governer */ NULL, !domain_is_on);
	if (ret) {
		dev_err(dev, "Failed at pm_genpd_init\n");
		return ret;
	}

	if (of_property_read_bool(pwr_domain_np, "no-auto-resume")) {
		dev_dbg(dev, "%s: set noop for resume_noirq callback",
			pd->genpd.name);
		pd->genpd.domain.ops.resume_noirq = noop_resume_noirq_overwrite;
	}

	ret = of_genpd_add_provider_simple(pwr_domain_np, &pd->genpd);
	if (ret) {
		dev_err(dev, "Failed to add a simple provider\n");
		goto remove_genpd;
	}
	dev_dbg(dev, "%s: Registered as powered %s\n", pd->genpd.name,
		domain_is_on ? "on" : "off");

	lpm_residency_init(pd->residency);
	if (lpm_start_residency_tracking(pd->residency,
					domain_is_on ? LPM_ON : LPM_OFF,
					track_from_zero))
		dev_err(dev, "%s: Unable to start residency tracking\n",
			pd->genpd.name);

	return 0;
remove_genpd:
	pm_genpd_remove(&pd->genpd);
	return ret;
}

static void unregister_power_domain(struct device_node *pwr_domain_np,
				    struct power_domain *pd)
{
	struct device *dev = pd->dev;

	lpm_stop_residency_tracking(pd->residency);

	dev_dbg(dev, "Unregistering %s\n", pd->genpd.name);
	of_genpd_del_provider(pwr_domain_np);
	pm_genpd_remove(&pd->genpd);
}

static void unregister_power_domains(struct platform_device *pdev, int count)
{
	struct lpm_pm_domains *lpm_pm_domains = platform_get_drvdata(pdev);
	struct device_node *pwr_ctrl_np = pdev->dev.of_node;
	struct device_node *pwr_domain_np;
	struct power_domain *pd;
	int i = 0;

	dev_dbg(lpm_pm_domains->dev, "Unregistering all power domains\n");
	for_each_available_child_of_node(pwr_ctrl_np, pwr_domain_np) {
		if (i >= count)
			break;
		pd = &lpm_pm_domains->pds[i];
		unregister_power_domain(pwr_domain_np, pd);
		++i;
	}
}

int power_on(struct generic_pm_domain *domain)
{
	struct power_domain *pd = to_power_domain(domain);
	int ret;

	dev_dbg(pd->dev, "%s: power on sequence start\n", pd->genpd.name);
	/* TODO (kkorczynski): This should be done through obs latency module in the future */
	LPM_PM_TRACE_BEGIN("lpm_pm_on", pd->genpd.name);
	ret = pd->ops->power_on(pd);
	LPM_PM_TRACE_END("lpm_pm_on", pd->genpd.name);

	if (ret == 0 && lpm_update_residency(pd->residency, LPM_ON))
		dev_err(pd->dev, "%s: Unable to update residency\n",
			pd->genpd.name);

	dev_dbg(pd->dev, "%s: power on sequence %s\n", pd->genpd.name,
		ret == 0 ? "succeeded" : "failed");
	return ret;
}

int power_off(struct generic_pm_domain *domain)
{
	struct power_domain *pd = to_power_domain(domain);
	int ret;

	dev_dbg(pd->dev, "%s: power off sequence start\n", pd->genpd.name);
	/* TODO (kkorczynski): This should be done through obs latency module in the future */
	LPM_PM_TRACE_BEGIN("lpm_pm_off", pd->genpd.name);
	ret = pd->ops->power_off(pd);
	LPM_PM_TRACE_END("lpm_pm_off", pd->genpd.name);
	if (ret == 0 && lpm_update_residency(pd->residency, LPM_OFF))
		dev_err(pd->dev, "%s: Unable to update residency\n",
			pd->genpd.name);

	dev_dbg(pd->dev, "%s: power off sequence %s\n", pd->genpd.name,
		ret == 0 ? "succeeded" : "failed");
	return ret;
}

static int register_as_subdomain(struct device_node *pwr_domain_np,
				 struct power_domain *pd)
{
	struct device *dev = pd->dev;
	struct of_phandle_args parent_args;
	struct of_phandle_args args;
	int ret;

	ret = of_parse_phandle_with_args(pwr_domain_np, "power-domains", NULL,
					 0, &parent_args);
	if (ret) {
		dev_dbg(dev, "%s has no parent.\n", pwr_domain_np->name);
		return 0;
	}

	args.np = pwr_domain_np;
	args.args_count = 0;
	ret = of_genpd_add_subdomain(&parent_args, &args);
	of_node_put(parent_args.np);
	if (ret) {
		dev_err(dev,
			"Failed to setup %s as a subdomain of %s (ret = %d)\n",
			pwr_domain_np->name, parent_args.np->name, ret);
		return ret;
	}
	return 0;
}

static int goog_lpm_pm_domains_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpm_pm_domains *lpm_pm_domains;
	const struct sswrp_power_desc *power_desc_table;
	struct device_node *pwr_ctrl_np = dev->of_node;
	struct device_node *pwr_domain_np;
	struct power_domain *pd;
	struct resource *res;
	const char *res_name;
	void __iomem **regions;
	int power_ops_table_size;
	int pd_count;
	int ret;
	int registered_domain_count;
	int i;
	struct regmap **syscons = NULL;

	power_desc_table = device_get_match_data(dev);

	lpm_pm_domains = devm_kzalloc(dev, sizeof(*lpm_pm_domains), GFP_KERNEL);
	if (!lpm_pm_domains)
		return -ENOMEM;

	regions = devm_kcalloc(dev, power_desc_table->region_count,
			       sizeof(*regions), GFP_KERNEL);
	for (i = 0; i < power_desc_table->region_count; ++i) {
		res_name = power_desc_table->reg_names[i];
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   res_name);
		if (!res) {
			dev_err(dev, "Failed to get resource %s\n", res_name);
			return -EINVAL;
		}
		regions[i] =
			devm_ioremap(dev, res->start, res->end - res->start);
		if (!regions[i]) {
			dev_err(dev, "Could not ioremap %s.\n", res_name);
			return -EINVAL;
		}
	}

	if (of_property_present(dev->of_node, "in_emulation"))
		set_emulation_flag(true);

	if (power_desc_table->syscon_name) {
		int ret = of_count_phandle_with_args(pwr_ctrl_np, power_desc_table->syscon_name,
						     NULL);
		if (ret < 0) {
			dev_err(dev, "Error: Couldn't parse syscon regions (ret=%d)\n", ret);
			return ret;
		} else if (ret != power_desc_table->syscon_count) {
			dev_err(dev, "Error: %d syscon regions provided, expected %d\n",
				ret, power_desc_table->syscon_count);
			return -EINVAL;
		}

		syscons = devm_kcalloc(dev, power_desc_table->syscon_count, sizeof(*syscons),
				       GFP_KERNEL);
		if (!syscons)
			return -ENOMEM;

		for (i = 0; i < power_desc_table->syscon_count; i++) {
			struct device_node *syscon_np;

			syscon_np = of_parse_phandle(pwr_ctrl_np, power_desc_table->syscon_name, i);
			if (!syscon_np) {
				dev_err(dev, "Error: Couldn't parse phandle for syscon %d\n", i);
				return -ENODEV;
			}

			syscons[i] = device_node_to_regmap(syscon_np);
			of_node_put(syscon_np);
			if (IS_ERR(syscons[i])) {
				dev_err(dev, "Failed to get shared region %d. Err: %ld",
					i, PTR_ERR(syscons[i]));
				return PTR_ERR(syscons[i]);
			}
		}
	}

	platform_set_drvdata(pdev, lpm_pm_domains);

	lpm_pm_domains->dev = dev;
	pd_count = of_get_child_count(pwr_ctrl_np);
	if (pd_count == 0) {
		dev_warn(dev, "No power domain defined.\n");
		return 0;
	}
	dev_dbg(dev, "Found %d power domains\n", pd_count);
	lpm_pm_domains->pd_count = pd_count;
	lpm_pm_domains->pds =
		devm_kcalloc(dev, pd_count, sizeof(*pd), GFP_KERNEL);
	if (!lpm_pm_domains->pds)
		return -ENOMEM;

	power_ops_table_size = power_desc_table->power_ops_table_size;
	registered_domain_count = 0;

	struct lpm_residency_desc *residency_table =
		devm_kzalloc(dev,
			power_ops_table_size * sizeof(struct lpm_residency_desc), GFP_KERNEL);
	if (!residency_table)
		return -ENOMEM;

	for_each_available_child_of_node(pwr_ctrl_np, pwr_domain_np) {
		pd = &lpm_pm_domains->pds[registered_domain_count];

		ret = of_property_read_u32(pwr_domain_np, "subdomain-id",
					   &pd->subdomain_id);
		if (ret) {
			dev_err(dev, "Couldn't find 'subdomain-id'.\n");
			goto cleanup;
		}
		if (pd->subdomain_id >= power_ops_table_size) {
			dev_err(dev, "Invalid subdomain id %d >= %d\n",
				pd->subdomain_id, power_ops_table_size);
			ret = -EINVAL;
			goto cleanup;
		}

		pd->ops = &power_desc_table->power_ops_table[pd->subdomain_id];
		pd->dev = dev;
		pd->regions = regions;
		pd->residency = &residency_table[pd->subdomain_id];
		pd->syscons = syscons;
		pd->genpd.name = pwr_domain_np->name;
		pd->genpd.power_on = power_on;
		pd->genpd.power_off = power_off;
		pd->lpm_pm_domains = lpm_pm_domains;

		ret = register_power_domain(pwr_domain_np, pd);
		if (ret)
			goto cleanup;
		++registered_domain_count;
	}
	i = 0;
	for_each_available_child_of_node(pwr_ctrl_np, pwr_domain_np) {
		pd = &lpm_pm_domains->pds[i];
		ret = register_as_subdomain(pwr_domain_np, pd);
		if (ret)
			goto cleanup;
		++i;
	}
	genpd_debugfs_init(lpm_pm_domains);

	devm_pm_runtime_enable(dev);

	ret = lpm_residency_sysfs_init(pdev);
	if (ret)
		dev_err(dev, "Unable to create lpm-residency nodes\n");

	return 0;
cleanup:
	of_node_put(pwr_domain_np);
	unregister_power_domains(pdev, registered_domain_count);
	return ret;
}

static void goog_lpm_pm_domains_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpm_pm_domains *lpm_pm_domains = platform_get_drvdata(pdev);

	lpm_residency_sysfs_remove(pdev);

	genpd_debugfs_remove(lpm_pm_domains);

	dev_dbg(dev, "Removing stub power controller\n");
	unregister_power_domains(pdev, lpm_pm_domains->pd_count);
}

static const struct of_device_id goog_lpm_pm_domains_of_match_table[] = {
	{ .compatible = "google,mbu-cpuacc-lpm-pm-domains",
	  .data = &mbu_cpuacc_power_desc_table },
	{ .compatible = "google,mbu-dpu-lpm-pm-domains",
		.data = &mbu_dpu_power_desc_table },
	{ .compatible = "google,mbu-g2d-lpm-pm-domains",
	  .data = &mbu_g2d_power_desc_table },
	{ .compatible = "google,mbu-hsio_s-lpm-pm-domains",
		.data = &mbu_hsio_s_power_desc_table },
	{ .compatible = "google,mbu-ispfe-lpm-pm-domains",
		.data = &mbu_ispfe_power_desc_table },
	{ .compatible = "google,mbu-pcie-lpm-pm-domains",
		.data = &mbu_pcie_power_desc_table },
	{},
};

MODULE_DEVICE_TABLE(of, goog_lpm_pm_domains_of_match_table);

static struct platform_driver goog_lpm_pm_domains_driver = {
	.probe = goog_lpm_pm_domains_probe,
	.remove = goog_lpm_pm_domains_remove,
	.driver = {
		.name = "lpm-pm-domains",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(goog_lpm_pm_domains_of_match_table),
	},
};

module_platform_driver(goog_lpm_pm_domains_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Power controller driver that directly talks to the LPM.");
MODULE_LICENSE("GPL");
