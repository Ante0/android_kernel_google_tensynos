// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Codec3P video accelerator
 *
 * Copyright 2023 Google LLC.
 *
 * Author: Vinay Kalia <vinaykalia@google.com>
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/types.h>
#include <perf/core/google_pm_qos.h>
#if IS_ENABLED(CONFIG_VPU_DEVFREQ)
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
#include <perf/core/perf_domain.h>
#endif
#endif

#include "vpu_of.h"
#include "vpu_priv.h"

static int vpu_of_get_resource(struct vpu_core *core)
{
	struct platform_device *pdev = to_platform_device(core->dev);
	struct resource *res;
	int rc = 0;

	if (!pdev) {
		pr_err("No platform device");
		return -1;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vpu");
	if (IS_ERR_OR_NULL(res)) {
		rc = PTR_ERR(res);
		pr_err("Failed to find vpu register base: %d\n", rc);
		return rc;
	}
	core->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(core->base)) {
		rc = PTR_ERR(core->base);
		if (rc == 0)
			rc = -EIO;
		pr_err("Failed to map vpu register base: %d\n", rc);
		core->base = NULL;
		return rc;
	}
	core->regs_size = res->end - res->start + 1;
	core->paddr = (phys_addr_t)res->start;
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qchctl_vpu");
	if (IS_ERR_OR_NULL(res)) {
		rc = PTR_ERR(res);
		pr_err("Failed to find qchctl_vpu register base: %d\n", rc);
		return rc;
	}
	core->qch_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(core->qch_base)) {
		rc = PTR_ERR(core->qch_base);
		if (rc == 0)
			rc = -EIO;
		pr_err("Failed to map qchctl_vpu register base: %d\n", rc);
		core->qch_base = NULL;
		return rc;
	}

	if (IS_ENABLED(CONFIG_CODEC3P_VCORE_APG)) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lpcm_vpu");
		if (IS_ERR_OR_NULL(res)) {
			rc = PTR_ERR(res);
			pr_err("Failed to find lpcm_vpu register base: %d\n", rc);
			return rc;
		}
		core->lpcm_base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR_OR_NULL(core->lpcm_base)) {
			rc = PTR_ERR(core->lpcm_base);
			if (rc == 0)
				rc = -EIO;
			pr_err("Failed to map lpcm_vpu register base: %d\n", rc);
			core->lpcm_base = NULL;
			return rc;
		}
	}

	core->irq = platform_get_irq(pdev, 0);
	if (core->irq < 0) {
		rc = core->irq;
		pr_err("platform_get_irq failed: %d\n", rc);
		return rc;
	}

#if IS_ENABLED(CONFIG_VPU_ICC)
	/* resource got from google_devm_of_icc_get will be auto released when device is
	 * removed
	 */
	core->icc_path.path_c3p = google_devm_of_icc_get(&pdev->dev, "sswrp-codec-3p");
	if (IS_ERR_OR_NULL(core->icc_path.path_c3p)) {
		pr_err("get icc_path for codec3p failed %ld\n",
			PTR_ERR(core->icc_path.path_c3p));
		rc = -ENODEV;
		return rc;
	}
	if (IS_ENABLED(CONFIG_SOC_MBU)) {
		core->icc_path.path_vpu = google_devm_of_icc_get(&pdev->dev, "path_vpu");
		if (IS_ERR_OR_NULL(core->icc_path.path_vpu)) {
			pr_err("get icc_path for vpu failed %ld\n",
				PTR_ERR(core->icc_path.path_vpu));
			rc = -ENODEV;
			return rc;
		}
	} else {
		core->icc_path.path_vpu = NULL;
	}
#else
	pr_debug("bypass gmc during bringup");
	core->icc_path.path_c3p = NULL;
	core->icc_path.path_vpu = NULL;
#endif

	if (IS_ENABLED(CONFIG_CODEC3P_VPU_RESET)) {
		core->vpu_reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
		if (IS_ERR(core->vpu_reset)) {
			rc = PTR_ERR(core->vpu_reset);
			dev_err(core->dev, "Unable to get reset: %d\n", rc);
			return rc;
		}
	}

	return rc;
}

int vpu_of_dt_parse(struct vpu_core *core)
{
	int rc = 0;
	struct device_node *node = core->dev->of_node;

	rc = vpu_of_get_resource(core);
	if (rc) {
		dev_err(core->dev, "failed to get resource: %d\n", rc);
		return rc;
	}

	rc = of_property_read_string(node, "firmware-name", &core->fw_name);
	if (rc < 0) {
		dev_err(core->dev, "failed to get firmware-name rc %d\n", rc);
		return rc;
	}

	rc = of_property_read_u32(node, "qchctl_active_mask_0_offset",
		&core->qchctl_active_mask_0_offset);
	if (rc < 0)
		dev_err(core->dev, "failed to get qchctl_active_mask_0_offset rc %d\n", rc);

	return rc;
}

int vpu_of_devfreq_init(struct vpu_core *core)
{
	int rc = 0;

#if IS_ENABLED(CONFIG_VPU_DEVFREQ)
	struct devfreq *df = NULL;
#if IS_ENABLED(CONFIG_GS_PERF_DOMAIN)
	df = gs_perf_domain_find_devfreq("codec3p", GS_RECOMMENDED_DEVFREQ);
	if (IS_ERR_OR_NULL(df)) {
		rc = PTR_ERR(df);
		dev_err(core->dev, "failed to get devfreq: %d\n", rc);
		return rc;
	}
#else
	df = devfreq_get_devfreq_by_phandle(core->dev, "devfreq", 0);
	if (IS_ERR(df)) {
		rc = PTR_ERR(df);
		dev_err(core->dev, "failed to get devfreq: %d\n", rc);
		return rc;
	}
#endif
	rc = google_pm_qos_add_devfreq_request(df, &core->dev_freq.qos_req,
			DEV_PM_QOS_MIN_FREQUENCY, PM_QOS_MIN_FREQUENCY_DEFAULT_VALUE);
	if (rc) {
		dev_err(core->dev, "failed to add devfreq request: %d\n", rc);
		return rc;
	}
	core->dev_freq.df = df;
#else
	dev_dbg(core->dev, "bypass devfreq\n");
	core->dev_freq.df = NULL;
#endif
	return rc;
}

void vpu_of_devfreq_deinit(struct vpu_core *core)
{
	if (core->dev_freq.df) {
		google_pm_qos_remove_devfreq_request(core->dev_freq.df,
						     &core->dev_freq.qos_req);
		core->dev_freq.df = NULL;
	}
}
