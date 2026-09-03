// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2025 Google, LLC.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

#include <drm/drm_gem.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_ioctl.h>

#include "g2d_sc.h"
#include "g2d_debugfs.h"
#include "g2d_drv.h"
#include "g2d_fb.h"
#include "g2d_gem.h"
#include "g2d_qos.h"
#include "g2d_writeback.h"
#include "g2d_plane.h"
#include "g2d_crtc.h"
#include "g2d_drm_atomic.h"

#define DRV_NAME "drm_g2d"
#define DRV_DESC "G2D DRM driver"
#define DRV_DATE "20250115"
#define DRV_MAJOR 0
#define DRV_MINOR 1

static struct platform_driver g2d_drm_platform_driver;

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.mmap = g2d_gem_mmap,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static struct drm_driver g2d_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.gem_prime_import_sg_table = g2d_gem_prime_import_sg_table,
	.dumb_create = g2d_dumb_create,
	.fops = &fops,
	.name = DRV_NAME,
	.desc = DRV_DESC,
	.date = DRV_DATE,
	.major = DRV_MAJOR,
	.minor = DRV_MINOR,
};

static const struct drm_mode_config_funcs g2d_mode_config_funcs = {
	.fb_create = g2d_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = g2d_drm_atomic_commit,
};

static struct drm_mode_config_helper_funcs g2d_mode_config_helpers = {
	.atomic_commit_tail = g2d_drm_atomic_helper_commit_tail_rpm,
};

static void g2d_mode_config_init(struct drm_device *dev)
{
	if (dev->mode_config.max_width == 0 || dev->mode_config.max_height == 0) {
		dev->mode_config.min_width = 0;
		dev->mode_config.min_height = 0;
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	}
	dev->mode_config.funcs = &g2d_mode_config_funcs;
	dev->mode_config.helper_private = &g2d_mode_config_helpers;
}

static int g2d_sc_init(struct platform_device *pdev, struct g2d_device *g2d_device)
{
	int ret = 0;
	struct device *dev = g2d_device->drm.dev;
	struct g2d_sc *sc;

	sc = devm_kzalloc(dev, sizeof(struct g2d_sc), GFP_KERNEL);
	if (!sc) {
		dev_err(dev, "sc struct allocation failed!");
		return -ENOMEM;
	}

	g2d_device->sc = sc;

	sc_init(sc, dev);

	ret = sc_ioremap_memory(pdev, sc);
	if (ret)
		dev_err(dev, "ioremap memory failed!");
	return ret;
}

static int g2d_kms_init(struct platform_device *pdev, struct g2d_device *g2d_device)
{
	int ret = 0;
	uint32_t possible_crtcs = 0;
	/* TODO(b/355089225): create planes according to the # of pipelines. */
	struct g2d_plane *layer0_plane;
	struct device *dev = g2d_device->drm.dev;
	struct g2d_sc *sc = g2d_device->sc;

	g2d_crtc_init(g2d_device);

	for (int i = 0; i < NUM_PIPELINES; i++)
		possible_crtcs |= drm_crtc_mask(&sc->crtc[i]->base);

	if (!g2d_device)
		dev_err(dev, "%s: g2d_device is null!", __func__);

	layer0_plane = g2d_plane_init(g2d_device, possible_crtcs, 0 /* layer index */);

	if (IS_ERR(layer0_plane)) {
		dev_err(dev, "Plane init failed!");
		return PTR_ERR(layer0_plane);
	}

	dev_dbg(dev, "Plane init success!");

	for (int i = 0; i < NUM_PIPELINES; i++)
		sc->crtc[i]->base.primary = &layer0_plane->base;

	ret = g2d_enable_writeback_connector(g2d_device, possible_crtcs);

	return ret;
}

static int g2d_drm_create(struct platform_device *pdev)
{
	int ret = 0;
	struct g2d_device *priv;
	struct drm_device *drm;
	struct device *dev;

	priv = devm_drm_dev_alloc(&pdev->dev, &g2d_drm_driver, struct g2d_device, drm);

	if (IS_ERR(priv))
		return PTR_ERR(priv);
	drm = &priv->drm;
	dev = drm->dev;

	ret = drmm_mode_config_init(drm);
	if (ret)
		dev_err(dev, "%s: mode config init failure with ret: %d!", __func__, ret);
	else
		dev_info(dev, "%s: Mode Config Init Success!", __func__);

	g2d_mode_config_init(drm);

	ret = g2d_sc_init(pdev, priv);
	if (ret) {
		dev_err(dev, "g2d_sc_init failed!");
		return ret;
	}

	g2d_qos_init(&pdev->dev, priv);

	ret = g2d_kms_init(pdev, priv);
	if (ret) {
		dev_err(dev, "g2d_kms_init failed!");
		return ret;
	}

	dev_dbg(dev, "Set drv data for platform driver!");
	platform_set_drvdata(pdev, drm);

	// Clean up mode setting state
	dev_dbg(dev, "Mode Config Reset!");
	drm_mode_config_reset(drm);

	// publish device instance
	ret = drm_dev_register(drm, 0);
	if (ret)
		dev_err(dev, "drm_dev_register failed with ret:%d", ret);
	else
		dev_dbg(dev, "drm_dev_register success!");

	dev_set_drvdata(dev, drm);

	return ret;
}

static int g2d_drm_platform_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;

	ret = g2d_drm_create(pdev);
	if (ret < 0) {
		dev_err(dev, "ERROR: g2d_drm_create failed!");
		return ret;
	}

	ret = sc_irq_init(pdev);
	if (ret < 0) {
		dev_err(dev, "ERROR: sc_irq_init failed!");
		return ret;
	}

	ret = g2d_debugfs_init(&pdev->dev);
	if (ret < 0) {
		dev_err(dev, "ERROR: g2d_debugfs_init failed!");
		return ret;
	}

	ret = dev_pm_domain_attach(dev, true /*power_on*/);
	if (ret < 0) {
		dev_err(dev, "ERROR: failed to attach pm_domain during probe.");
		goto detach_pd;
	}

	pm_runtime_enable(dev);

	pm_runtime_get_sync(dev);
	g2d_sc_print_id_regs(dev);
	pm_runtime_put_sync(dev);

	return ret;

detach_pd:
	dev_pm_domain_detach(dev, true /*power_off*/);
	return ret;
}

static void g2d_drm_platform_remove(struct platform_device *pdev)
{
	struct drm_device *drm;
	struct device *dev = &pdev->dev;

	drm = platform_get_drvdata(pdev);

	g2d_debugfs_deinit(&pdev->dev);

	pm_runtime_disable(dev);

	dev_pm_domain_detach(dev, true /*power_off*/);

	drm_dev_unregister(drm);
}

static const struct of_device_id g2d_drm_dt_ids[] = {

	{
		.compatible = "google,g2d-drm-device",
	},

	{ /* sentinel */ },

};

MODULE_DEVICE_TABLE(of, g2d_drm_dt_ids);

static const struct dev_pm_ops g2d_pm_ops = {
	.runtime_suspend = g2d_pm_runtime_suspend,
	.runtime_resume = g2d_pm_runtime_resume,
};

static struct platform_driver g2d_drm_platform_driver = {
	.probe = g2d_drm_platform_probe,
	.remove_new = g2d_drm_platform_remove,

	.driver = {
		.name = DRV_NAME,
		.of_match_table = g2d_drm_dt_ids,
		.pm = &g2d_pm_ops,
	},
};
static int __init g2d_drm_init(void)
{
	int ret;

	ret = platform_driver_register(&g2d_drm_platform_driver);
	if (ret != 0)
		pr_err("%s: Platform driver register failed with retval: %d", __func__, ret);

	pr_debug("%s: Registered platform driver!", __func__);
	return ret;
}

static void __exit g2d_drm_exit(void)
{
	platform_driver_unregister(&g2d_drm_platform_driver);
	pr_debug("%s: Unregistered platform driver!", __func__);
}

module_init(g2d_drm_init);
module_exit(g2d_drm_exit);

MODULE_DESCRIPTION("G2D DRM Driver");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("rushikesh@google.com");
MODULE_IMPORT_NS(DMA_BUF);
