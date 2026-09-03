// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC
 */
#include <dvfs-frontend/dvfs_target_frontend.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "dvfs_target_frontend_debug.h"
#include "dvfs_target_frontend_direct.h"

/* TODO(b/441700036): Move the cpu freq register info to cpm */
#define CPUFREQ_PERF_STATE_ADDRESS 0x200c0790
#define CPUFREQ_CURR_STATE_ADDRESS 0x200c0794
#define CPUFREQ_REG_SIZE 0x20

#define CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(X) (0x8 * (X))
#define CPU0_CLK_DOMAIN (0x00)
#define CPU1_CLK_DOMAIN (0x01)
#define CPU2_CLK_DOMAIN (0x02)

#define DSUFREQ_BASE_ADDRESS 0x200c0780
#define DSUFREQ_REG_SIZE 0x08

/**
 * TODO(b/441700036): The hard coded pwrblk id should be removed once the complete
 * implementation of frontend infra is available
 */
#define PWRBLK_DSU 34
#define PWRBLK_CPU0 35
#define PWRBLK_CPU1 36
#define PWRBLK_CPU2 37
#define PWRBLK_NUM_MAX 38


struct frontend_domain {
	u8 hw_clk_domain;
};
struct dvfs_frontend {
	void __iomem *cpufreq_perf_state_addr;
	struct regmap *cpufreq_perf_state_regmap;
	void __iomem *cpufreq_curr_state_addr;
	struct regmap *cpufreq_curr_state_regmap;
	void __iomem *dsufreq_base_addr;
	struct regmap *dsufreq_regmap;
	struct frontend_domain domains[PWRBLK_NUM_MAX];
};

static const struct regmap_config cpufreq_regmap_cfg = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32
};

/* This lock protects dvfs_frontend_priv and its members. */
static DEFINE_MUTEX(dvfs_frontend_lock);
static struct dvfs_frontend *dvfs_frontend_priv;
static struct device *dvfs_frontend_dev;

int dvfs_fe_get_pf_level(int domain_id)
{
	struct dvfs_frontend *dvfs_frontend = dvfs_frontend_priv;
	struct device *dev = dvfs_frontend_dev;
	unsigned int pf_state;

	if (!dvfs_frontend_dev)
		return -EPROBE_DEFER;

	switch (domain_id) {
	case PWRBLK_CPU0:
	case PWRBLK_CPU1:
	case PWRBLK_CPU2: {
		u32 addr;
		struct regmap *regmap = dvfs_frontend->cpufreq_curr_state_regmap;
		u8 clk_domain = dvfs_frontend->domains[domain_id].hw_clk_domain;

		if (IS_ERR_OR_NULL(regmap)) {
			dev_err(dev, "regmap is not available for domain %d\n",
				domain_id);
			return -ENODEV;
		}
		addr = CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(clk_domain);
		regmap_read(regmap, addr, &pf_state);

		return pf_state;
	}
	case PWRBLK_DSU: {
		struct regmap *regmap = dvfs_frontend->dsufreq_regmap;

		if (IS_ERR_OR_NULL(regmap)) {
			dev_err(dev, "regmap is not available for domain %d\n",
				domain_id);
			return -ENODEV;
		}
		regmap_read(regmap, 0x0, &pf_state);
		return pf_state;
	}
	case PWRBLK_ISPBE:
	case PWRBLK_ISPFE:
	case PWRBLK_GCV:
	case PWRBLK_DPU:
	case PWRBLK_G2D:
	case PWRBLK_CODEC_3P:
	case PWRBLK_CPUACC:
	case PWRBLK_PCIE:
		return dvfs_fe_get_pf_level_direct(domain_id);
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(dvfs_fe_get_pf_level);

void dvfs_fe_set_pf_level(int domain_id, u8 pf_level)
{
	struct dvfs_frontend *dvfs_frontend = dvfs_frontend_priv;
	struct device *dev = dvfs_frontend_dev;

	if (!dvfs_frontend_dev)
		return;

	switch (domain_id) {
	case PWRBLK_CPU0:
	case PWRBLK_CPU1:
	case PWRBLK_CPU2: {
		u32 addr;
		struct regmap *regmap = dvfs_frontend->cpufreq_perf_state_regmap;
		u8 clk_domain = dvfs_frontend->domains[domain_id].hw_clk_domain;

		if (IS_ERR_OR_NULL(regmap)) {
			dev_err(dev, "regmap is not available for domain %d\n",
				domain_id);
			return;
		}
		addr = CPU_DVFS_CLKDOMAIN_PERF_STATE_VOTE_ADDR(clk_domain);

		regmap_write(regmap, addr, pf_level);

		return;
	}
	case PWRBLK_DSU: {
		struct regmap *regmap = dvfs_frontend->dsufreq_regmap;

		if (IS_ERR_OR_NULL(regmap)) {
			dev_err(dev, "regmap is not available for domain %d\n",
				domain_id);
			return;
		}
		regmap_write(regmap, 0x0, pf_level);
			return;
	}
	case PWRBLK_ISPBE:
	case PWRBLK_ISPFE:
	case PWRBLK_GCV:
	case PWRBLK_DPU:
	case PWRBLK_G2D:
	case PWRBLK_CODEC_3P:
	case PWRBLK_CPUACC:
	case PWRBLK_PCIE:
		dvfs_fe_set_pf_level_direct(domain_id, pf_level);
		return;
	default:
		return;
	}
}
EXPORT_SYMBOL_GPL(dvfs_fe_set_pf_level);

static int cpu_dvfs_regmap_init(void)
{
	struct dvfs_frontend *dvfs_frontend = dvfs_frontend_priv;
	struct device *dev = dvfs_frontend_dev;
	int err = 0;

	if (IS_ERR_OR_NULL(dvfs_frontend->cpufreq_perf_state_regmap)) {
		dvfs_frontend->cpufreq_perf_state_addr = devm_ioremap(dev,
				CPUFREQ_PERF_STATE_ADDRESS, CPUFREQ_REG_SIZE);
		if (IS_ERR(dvfs_frontend->cpufreq_perf_state_addr)) {
			dev_err(dev, "Failed to ioremap physical address for cpu perf state: %ld\n",
				PTR_ERR(dvfs_frontend->cpufreq_perf_state_addr));
			err = PTR_ERR(dvfs_frontend->cpufreq_perf_state_addr);
			goto out;
		}

		dvfs_frontend->cpufreq_perf_state_regmap = devm_regmap_init_mmio(dev,
				dvfs_frontend->cpufreq_perf_state_addr,
				&cpufreq_regmap_cfg);
		if (IS_ERR(dvfs_frontend->cpufreq_perf_state_regmap)) {
			dev_err(dev, "Failed to initialize regmap for cpu perf state: %ld\n",
					PTR_ERR(dvfs_frontend->cpufreq_perf_state_regmap));
			err = PTR_ERR(dvfs_frontend->cpufreq_perf_state_regmap);
			goto out;
		}
	}

	if (IS_ERR_OR_NULL(dvfs_frontend->cpufreq_curr_state_regmap)) {
		dvfs_frontend->cpufreq_curr_state_addr = devm_ioremap(dev,
				CPUFREQ_CURR_STATE_ADDRESS, CPUFREQ_REG_SIZE);
		if (IS_ERR(dvfs_frontend->cpufreq_curr_state_addr)) {
			dev_err(dev, "Failed to ioremap physical address for cpu curr state: %ld\n",
				PTR_ERR(dvfs_frontend->cpufreq_curr_state_addr));
			err = PTR_ERR(dvfs_frontend->cpufreq_curr_state_addr);
			goto out;
		}

		dvfs_frontend->cpufreq_curr_state_regmap = devm_regmap_init_mmio(dev,
				dvfs_frontend->cpufreq_curr_state_addr,
				&cpufreq_regmap_cfg);
		if (IS_ERR(dvfs_frontend->cpufreq_curr_state_regmap)) {
			dev_err(dev, "Failed to initialize regmap for cpu curr state: %ld\n",
					PTR_ERR(dvfs_frontend->cpufreq_curr_state_regmap));
			err = PTR_ERR(dvfs_frontend->cpufreq_curr_state_regmap);
			goto out;
		}
	}
out:
	return err;
}

static int dsu_dvfs_regmap_init(void)
{
	struct dvfs_frontend *dvfs_frontend = dvfs_frontend_priv;
	struct device *dev = dvfs_frontend_dev;
	int err = 0;

	if (IS_ERR_OR_NULL(dvfs_frontend->dsufreq_regmap)) {
		dvfs_frontend->dsufreq_base_addr = devm_ioremap(dev,
				DSUFREQ_BASE_ADDRESS, DSUFREQ_REG_SIZE);
		if (IS_ERR(dvfs_frontend->dsufreq_base_addr)) {
			dev_err(dev, "Failed to ioremap physical address for dsufreq: %ld\n",
				PTR_ERR(dvfs_frontend->dsufreq_base_addr));
			err = PTR_ERR(dvfs_frontend->dsufreq_base_addr);
			goto out;
		}

		dvfs_frontend->dsufreq_regmap = devm_regmap_init_mmio(dev,
				dvfs_frontend->dsufreq_base_addr,
				&cpufreq_regmap_cfg);
		if (IS_ERR(dvfs_frontend->dsufreq_regmap)) {
			dev_err(dev, "Failed to initialize regmap for dsufreq: %ld\n",
					PTR_ERR(dvfs_frontend->dsufreq_regmap));
			err = PTR_ERR(dvfs_frontend->dsufreq_regmap);
			goto out;
		}
	}
out:
	return err;

}

int dvfs_fe_prepare_domain(int domain_id)
{
	struct dvfs_frontend *dvfs_frontend = dvfs_frontend_priv;
	int err = 0;

	if (!dvfs_frontend_dev)
		return -EPROBE_DEFER;

	mutex_lock(&dvfs_frontend_lock);
	switch (domain_id) {
	case PWRBLK_CPU0:
	case PWRBLK_CPU1:
	case PWRBLK_CPU2:
		dvfs_frontend->domains[PWRBLK_CPU0].hw_clk_domain = CPU0_CLK_DOMAIN;
		dvfs_frontend->domains[PWRBLK_CPU1].hw_clk_domain = CPU1_CLK_DOMAIN;
		dvfs_frontend->domains[PWRBLK_CPU2].hw_clk_domain = CPU2_CLK_DOMAIN;
		err = cpu_dvfs_regmap_init();
		goto out;
	case PWRBLK_DSU:
		err = dsu_dvfs_regmap_init();
		goto out;
	case PWRBLK_ISPBE:
	case PWRBLK_ISPFE:
	case PWRBLK_GCV:
	case PWRBLK_DPU:
	case PWRBLK_G2D:
	case PWRBLK_CODEC_3P:
	case PWRBLK_CPUACC:
	case PWRBLK_PCIE:
		err = dvfs_fe_prepare_domain_direct(domain_id);
		goto out;
	default:
		goto out;
	}

out:
	mutex_unlock(&dvfs_frontend_lock);
	return err;

}
EXPORT_SYMBOL_GPL(dvfs_fe_prepare_domain);

static int dvfs_target_fe_probe(struct platform_device *pdev)
{
	int ret;
	struct dvfs_frontend *dvfs_frontend;
	struct device *dev = &pdev->dev;

	dvfs_frontend = devm_kzalloc(dev, sizeof(*dvfs_frontend), GFP_KERNEL);

	ret = dvfs_target_frontend_direct_init(&pdev->dev);
	if (ret < 0)
		return ret;

	dvfs_frontend_priv = dvfs_frontend;
	dvfs_frontend_dev = dev;

	dvfs_target_frontend_debugfs_init();

	return 0;
}

static void dvfs_target_fe_remove(struct platform_device *pdev)
{
	if (!dvfs_frontend_priv)
		return;

	dvfs_target_frontend_debugfs_remove();

	dvfs_frontend_dev = NULL;
	dvfs_frontend_priv = NULL;

	dvfs_target_frontend_direct_exit();
}

static const struct of_device_id dvfs_target_fe_match[] = {
	{ .compatible = "google,dvfs_frontend" },
	{}
};

static struct platform_driver dvfs_target_fe_platform_driver = {
	.driver = {
		.name = "dvfs_target_fe",
		.owner = THIS_MODULE,
		.of_match_table = dvfs_target_fe_match,
	},
	.probe = dvfs_target_fe_probe,
	.remove = dvfs_target_fe_remove,
};

static int __init dvfs_target_frontend_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&dvfs_target_fe_platform_driver);
	if (ret) {
		pr_err("dvfs_frontend: Failed to register driver. [%d]\n", ret);
		return ret;
	}

	return 0;
}
module_init(dvfs_target_frontend_init);

static void __exit dvfs_target_frontend_exit(void)
{
	platform_driver_unregister(&dvfs_target_fe_platform_driver);
}
module_exit(dvfs_target_frontend_exit);

MODULE_AUTHOR("Google LLC");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DVFS Target Frontend");
