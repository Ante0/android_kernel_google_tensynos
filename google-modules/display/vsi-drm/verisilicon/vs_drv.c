// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#include <linux/component.h>
#include <linux/iommu.h>
#include <linux/of_graph.h>
#include <linux/vmalloc.h>

#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_ttm.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_of.h>
#include <drm/drm_prime.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <trace/dpu_trace.h>

#include "vs_dc.h"
#include "vs_dc_pre.h"
#include "vs_dc_post.h"
#include "vs_drm_atomic.h"
#include "vs_drm_state_record.h"
#include "vs_drv.h"
#include "vs_sscd.h"
#include "vs_gem.h"
#include "vs_simple_enc.h"
#include "vs_task_fence.h"
#include "vs_trace.h"

#define DRV_NAME "vs-drm"
#define DRV_DESC "VeriSilicon DRM driver"
#define DRV_DATE "20191101"
#define DRV_MAJOR 1
#define DRV_MINOR 0

static bool has_iommu = true;
struct drm_device *dev_drm;

static const struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.release = drm_release,
	.unlocked_ioctl = drm_ioctl,
	.compat_ioctl = drm_compat_ioctl,
	.poll = drm_poll,
	.read = drm_read,
	.mmap = vs_gem_mmap,
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

static const struct drm_ioctl_desc vs_ioctls[] = {
	DRM_IOCTL_DEF_DRV(VS_GET_FBC_OFFSET, vs_get_fbc_offset_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_SW_RESET, vs_sw_reset_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_GEM_QUERY, vs_gem_query_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_GET_FEATURE_CAP, vs_get_feature_cap_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_GET_HW_CAP, vs_get_hw_cap_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_GET_HIST_BINS, vs_get_hist_bins_query_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_GET_LTM_HIST, vs_get_ltm_hist_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_GET_WB_FRM_DONE, vs_get_wb_frm_done_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(VS_TASK_FENCE, vs_task_fence_ioctl, DRM_MASTER),
};

static void vs_drm_postclose(struct drm_device *drm_dev, struct drm_file *file_priv)
{
	/*
	 * postclose is called each time a driver fd is released. If open_count
	 * is 1, it means this was the last file open on the device, and we
	 * should perform a full shutdown. Multiple applications (e.g.,
	 * modetest), not just the single master, can have the DRM device open.
	 */
	if (atomic_read(&drm_dev->open_count) == 1) {
		drm_atomic_helper_shutdown(drm_dev);
		vs_dc_sw_reset(drm_dev);
	}
}

static struct drm_driver vs_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM,
	.postclose = vs_drm_postclose,
	.gem_prime_import = vs_gem_prime_import,
	.gem_prime_import_sg_table = vs_gem_prime_import_sg_table,
	.dumb_create = vs_gem_dumb_create,
	.ioctls = vs_ioctls,
	.num_ioctls = ARRAY_SIZE(vs_ioctls),
	.fops = &fops,
	.name = DRV_NAME,
	.desc = DRV_DESC,
	.date = DRV_DATE,
	.major = DRV_MAJOR,
	.minor = DRV_MINOR,
};

int vs_drm_iommu_attach_device(struct drm_device *drm_dev, struct device *dev)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	int ret;

	if (!has_iommu)
		return 0;

	if (!priv->domain) {
		priv->domain = iommu_get_domain_for_dev(dev);
		if (!priv->domain)
			return -EINVAL;
		priv->dma_dev = dev;
	}

	ret = iommu_attach_device(priv->domain, dev);
	if (ret) {
		DRM_DEV_ERROR(dev, "Failed to attach iommu device\n");
		return ret;
	}

	return 0;
}

void vs_drm_iommu_detach_device(struct drm_device *drm_dev, struct device *dev)
{
	struct vs_drm_private *priv = drm_dev->dev_private;

	if (!has_iommu)
		return;

	iommu_detach_device(priv->domain, dev);

	if (priv->dma_dev == dev)
		priv->dma_dev = drm_dev->dev;
	priv->domain = NULL;
}

void vs_drm_update_alignment(struct drm_device *drm_dev, unsigned int pitch_align,
			     unsigned int addr_align)
{
	struct vs_drm_private *priv = drm_dev->dev_private;

	if (pitch_align > priv->pitch_alignment)
		priv->pitch_alignment = pitch_align;

	if (addr_align > priv->addr_alignment)
		priv->addr_alignment = addr_align;
}

static const struct drm_mode_config_funcs vs_mode_config_funcs = {
	.fb_create = vs_fb_create,
	.get_format_info = vs_get_format_info,
	.atomic_check = vs_drm_atomic_check,
	.atomic_commit = vs_drm_atomic_commit,
};

static struct drm_mode_config_helper_funcs vs_mode_config_helpers = {
	.atomic_commit_tail = vs_drm_atomic_commit_tail,
};

static void vs_mode_config_init(struct drm_device *dev)
{
	if (dev->mode_config.max_width == 0 || dev->mode_config.max_height == 0) {
		dev->mode_config.min_width = 0;
		dev->mode_config.min_height = 0;
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	}
	dev->mode_config.funcs = &vs_mode_config_funcs;
	dev->mode_config.helper_private = &vs_mode_config_helpers;
	dev->mode_config.normalize_zpos = true;
}

static void vs_drm_setup_clones(struct drm_device *drm_dev)
{
	struct drm_encoder *encoder;
	struct drm_encoder *wb_enc;
	u32 wb_mask = 0;
	u32 disp_mask = 0;

	drm_for_each_encoder(encoder, drm_dev) {
		encoder->possible_clones = drm_encoder_mask(encoder);
		if (encoder->encoder_type == DRM_MODE_ENCODER_VIRTUAL)
			wb_mask |= drm_encoder_mask(encoder);
		else
			disp_mask |= drm_encoder_mask(encoder);
	}

	drm_for_each_encoder_mask(wb_enc, drm_dev, wb_mask) {
		/* Map WBs to encoders */
		drm_for_each_encoder_mask(encoder, drm_dev, disp_mask) {
			if (wb_enc->possible_crtcs & encoder->possible_crtcs) {
				encoder->possible_clones |= drm_encoder_mask(wb_enc);
				wb_enc->possible_clones |= drm_encoder_mask(encoder);
			}
		}
	}
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
#if IS_ENABLED(CONFIG_VERISILICON_RECORD_DRM_STATE)

static int drm_state_history_show(struct seq_file *s, void *data)
{
	const struct drm_state_history_data *sh_data = s->private;
	int i, ret;

	for (i = 0; i < sh_data->num_logged_states; ++i) {
		seq_printf(s, "State # -%02d\n", i);
		ret = seq_write(s, sh_data->buffers[i], sh_data->buffer_sizes[i]);
		if (ret)
			break;
	}

	return ret;
}

static int drm_state_history_open(struct inode *inode, struct file *file)
{
	struct drm_device *drm_dev = inode->i_private;
	size_t total_dump_size = 0;
	int num_logged_states;
	char state_header[] = "State # -XX\n";
	struct drm_state_history_data *sh_data = vzalloc(sizeof(struct drm_state_history_data));
	int i;

	if (!drm_dev)
		return -EINVAL;
	if (!sh_data)
		return -ENOMEM;
	file->private_data = sh_data;

	/* Alloc and fill drm state logs */
	num_logged_states = vs_drm_recorded_states_prepare(sh_data, drm_dev);
	if (num_logged_states <= 0)
		return 0;

	for (i = 0; i < num_logged_states; ++i) {
		total_dump_size += sh_data->buffer_sizes[i];
		total_dump_size += sizeof(state_header);
	}

	return single_open_size(file, drm_state_history_show, sh_data, total_dump_size);
}

static int drm_state_history_release(struct inode *inode, struct file *file)
{
	struct drm_state_history_data *sh_data = file->private_data;

	if (!sh_data)
		return 0;

	vs_drm_recorded_states_destroy(sh_data);

	vfree(sh_data);
	return single_release(inode, file);
}

static const struct file_operations drm_state_history_fops = {
	.owner = THIS_MODULE,
	.open = drm_state_history_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = drm_state_history_release,
};
#endif /* CONFIG_VERISILICON_RECORD_DRM_STATE */

static int drm_debugfs_add_custom_entries(struct drm_device *drm_dev)
{
#if IS_ENABLED(CONFIG_VERISILICON_RECORD_DRM_STATE)
	struct dentry *debugfs_root = drm_dev->primary->debugfs_root;

	/*
	 * Ideally, we would not directly access the debugfs root like this,
	 * but it is necessary to add custom fops to the file
	 */
	debugfs_create_file("state_history", 0444, debugfs_root, drm_dev, &drm_state_history_fops);
#endif /* CONFIG_VERISILICON_RECORD_DRM_STATE */

	return 0;
}

#endif /* CONFIG_DEBUG_FS */

static void vs_drm_update_wb_connectors_mask(struct drm_device *drm_dev)
{
	struct vs_drm_private *priv = drm_dev->dev_private;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;

	priv->wb_connectors_mask = 0;
	drm_connector_list_iter_begin(drm_dev, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		if (connector->connector_type == DRM_MODE_CONNECTOR_WRITEBACK)
			priv->wb_connectors_mask |= drm_connector_mask(connector);
	}
	drm_connector_list_iter_end(&conn_iter);
}

/* platfrom driver */
static int vs_drm_bind(struct device *dev)
{
	struct drm_device *drm_dev;
	struct vs_drm_private *priv;
	int ret;

	drm_dev = drm_dev_alloc(&vs_drm_driver, dev);
	if (IS_ERR(drm_dev))
		return PTR_ERR(drm_dev);

	dev_set_drvdata(dev, drm_dev);

	priv = drmm_kzalloc(drm_dev, sizeof(struct vs_drm_private), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto err_put_dev;
	}

	priv->pitch_alignment = 64;
	priv->addr_alignment = 128;
	priv->dma_dev = drm_dev->dev;
	priv->dsi_coredump_funcs.trigger_coredump = trigger_coredump;

	drm_dev->dev_private = priv;

	ret = vs_init_trace(dev);
	if (ret)
		dev_err(dev, "failed to enable traces\n");

	ret = drmm_mode_config_init(drm_dev);
	if (ret)
		goto err_put_dev;

	/* Now try and bind all our sub-components */
	ret = component_bind_all(dev, drm_dev);
	if (ret)
		goto err_put_dev;

	vs_drm_update_wb_connectors_mask(drm_dev);

	vs_mode_config_init(drm_dev);

	vs_drm_setup_clones(drm_dev);

	ret = drm_vblank_init(drm_dev, drm_dev->mode_config.num_crtc);
	if (ret)
		goto err_bind;

	drm_mode_config_reset(drm_dev);

	drm_kms_helper_poll_init(drm_dev);

	ret = vs_drm_prepare_state_history_record(drm_dev);
	if (ret)
		goto err_helper;

	ret = drm_dev_register(drm_dev, 0);
	if (ret)
		goto err_state_history;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	drm_debugfs_add_custom_entries(drm_dev);
#endif

	drm_fbdev_ttm_setup(drm_dev, 32);

	return 0;

err_state_history:
	vs_drm_destroy_state_history_record(drm_dev);
err_helper:
	drm_kms_helper_poll_fini(drm_dev);
err_bind:
	component_unbind_all(drm_dev->dev, drm_dev);
err_put_dev:
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
	return ret;
}

static void vs_drm_unbind(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	drm_atomic_helper_shutdown(drm_dev);

	drm_dev_unregister(drm_dev);

	vs_drm_destroy_state_history_record(drm_dev);

	drm_kms_helper_poll_fini(drm_dev);

	component_unbind_all(drm_dev->dev, drm_dev);

	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm_dev);
}

static const struct component_master_ops vs_drm_ops = {
	.bind = vs_drm_bind,
	.unbind = vs_drm_unbind,
};

static struct platform_driver vs_drm_platform_driver;

#if IS_ENABLED(CONFIG_VERISILICON_MIPI_DSI2H)
extern struct platform_driver vs_mipi_dsi2h_driver;
#endif
#if IS_ENABLED(CONFIG_VERISILICON_DISPLAYPORT)
extern struct platform_driver vs_dp_driver;
#endif

static struct platform_driver *drm_sub_drivers[] = {
	/* put display control driver at start */
	&dc_platform_driver,
	&dc_be_platform_driver,
	&dc_fe_platform_driver,
	&dc_wb_platform_driver,

/* bridge */
#if IS_ENABLED(CONFIG_VERISILICON_MIPI_DSI2H)
	&vs_mipi_dsi2h_driver,
#endif
	/* encoder */
#if IS_ENABLED(CONFIG_VERISILICON_DISPLAYPORT)
	&vs_dp_driver,
#else
	&simple_encoder_driver,
#endif
};

#define NUM_DRM_DRIVERS (sizeof(drm_sub_drivers) / sizeof(struct platform_driver *))

static struct component_match *vs_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < NUM_DRM_DRIVERS; ++i) {
		struct platform_driver *drv = drm_sub_drivers[i];
		struct device_node *np;

		for_each_matching_node(np, drv->driver.of_match_table) {
			if (!of_device_is_available(np))
				continue;

			component_match_add(dev, &match, component_compare_of, np);
		}
	}

	return match ?: ERR_PTR(-ENODEV);
}

static int vs_drm_platform_of_probe(struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct device_node *port;
	bool found = false;
	int i;

	if (!np)
		return -ENODEV;

	for (i = 0;; i++) {
		struct device_node *iommu;

		port = of_parse_phandle(np, "ports", i);
		if (!port)
			break;

		if (!of_device_is_available(port->parent)) {
			of_node_put(port);
			continue;
		}

		iommu = of_parse_phandle(port->parent, "iommus", 0);

		/*
		 * if there is a crtc not support iommu, force set all
		 * crtc use non-iommu buffer.
		 */
		if (!iommu || !of_device_is_available(iommu->parent))
			has_iommu = false;

		found = true;

		of_node_put(iommu);
		of_node_put(port);
	}

	if (i == 0) {
		DRM_DEV_ERROR(dev, "missing 'ports' property\n");
		return -ENODEV;
	}

	if (!found) {
		DRM_DEV_ERROR(dev, "No available DC found.\n");
		return -ENODEV;
	}

	return 0;
}

static int vs_drm_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match;
	int ret;

	ret = vs_drm_platform_of_probe(dev);
	if (ret)
		return ret;

	match = vs_drm_match_add(dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	return component_master_add_with_match(dev, &vs_drm_ops, match);
}

static void vs_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &vs_drm_ops);
}

#if IS_ENABLED(CONFIG_PM_SLEEP)
static int vs_drm_atomic_suspend(struct drm_device *dev)
{
	struct drm_modeset_acquire_ctx ctx;
	struct drm_atomic_state *state;
	int ret;

	DPU_ATRACE_BEGIN(__func__);

	DRM_MODESET_LOCK_ALL_BEGIN(dev, ctx, 0, ret);

	state = drm_atomic_helper_duplicate_state(dev, &ctx);
	if (IS_ERR(state))
		ret = PTR_ERR(state);
	else
		ret = vs_drm_atomic_disable_all(dev, &ctx);

	DRM_MODESET_LOCK_ALL_END(dev, ctx, ret);

	if (ret > 0) /* some crtcs were disabled */
		dev->mode_config.suspend_state = state;
	else /* nothing was disabled */
		drm_atomic_state_put(state);

	DPU_ATRACE_END(__func__);

	return ret;
}

static int vs_drm_suspend(struct drm_device *dev)
{
	int ret;

	if (!dev)
		return 0;

	DPU_ATRACE_BEGIN(__func__);
	/*
	 * Don't disable polling if it was never initialized
	 */
	if (dev->mode_config.poll_enabled)
		drm_kms_helper_poll_disable(dev);

	drm_fb_helper_set_suspend_unlocked(dev->fb_helper, 1);

	ret = vs_drm_atomic_suspend(dev);
	if (ret) {
		drm_fb_helper_set_suspend_unlocked(dev->fb_helper, 0);
		/*
		 * Don't enable polling if it was never initialized
		 */
		if (dev->mode_config.poll_enabled)
			drm_kms_helper_poll_enable(dev);
	}
	DPU_ATRACE_END(__func__);

	return ret;
}

static int vs_drm_resume(struct drm_device *dev)
{
	int ret = 0;

	if (!dev)
		return 0;

	DPU_ATRACE_BEGIN(__func__);
	if (dev->mode_config.suspend_state) {
		ret = drm_atomic_helper_resume(dev, dev->mode_config.suspend_state);
		if (ret)
			drm_err(dev, "Failed to resume (%d)\n", ret);

		dev->mode_config.suspend_state = NULL;
	}

	drm_fb_helper_set_suspend_unlocked(dev->fb_helper, 0);
	/*
	 * Don't enable polling if it is not initialized
	 */
	if (dev->mode_config.poll_enabled)
		drm_kms_helper_poll_enable(dev);

	DPU_ATRACE_END(__func__);

	return ret;
}

static int vs_drm_prepare(struct device *dev)
{
	int ret;
	struct drm_device *drm = dev_get_drvdata(dev);

	DPU_ATRACE_BEGIN(__func__);
	dev_dbg(dev, "suspend drm mode config\n");

	ret = vs_drm_suspend(drm);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to config helper suspend.\n");

	DPU_ATRACE_END(__func__);
	return ret;
}

static void vs_drm_complete(struct device *dev)
{
	int ret;
	struct drm_device *drm = dev_get_drvdata(dev);

	dev_dbg(dev, "resume drm mode config\n");
	DPU_ATRACE_BEGIN(__func__);
	ret = vs_drm_resume(drm);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "failed to config helper resume.\n");

	DPU_ATRACE_END(__func__);
}

static const struct dev_pm_ops vs_drm_pm_ops = {
	.prepare = vs_drm_prepare,
	.complete = vs_drm_complete,
};
#endif

static const struct of_device_id vs_drm_dt_ids[] = {

	{
		.compatible = "verisilicon,display-subsystem",
	},

	{ /* sentinel */ },

};

MODULE_DEVICE_TABLE(of, vs_drm_dt_ids);

static void vs_drm_platform_shutdown(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	if (!drm)
		return;

	drm_atomic_helper_shutdown(drm);
}

static struct platform_driver vs_drm_platform_driver = {
	.probe = vs_drm_platform_probe,
	.remove = vs_drm_platform_remove,
	.shutdown = vs_drm_platform_shutdown,

	.driver = {
		.name = DRV_NAME,
		.of_match_table = vs_drm_dt_ids,
#if IS_ENABLED(CONFIG_PM_SLEEP)
		.pm = &vs_drm_pm_ops,
#endif
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

static int __init vs_drm_init(void)
{
	int ret;

	ret = platform_register_drivers(drm_sub_drivers, NUM_DRM_DRIVERS);
	if (ret)
		return ret;

	ret = platform_driver_register(&vs_drm_platform_driver);
	if (ret)
		platform_unregister_drivers(drm_sub_drivers, NUM_DRM_DRIVERS);
	return ret;
}

static void __exit vs_drm_exit(void)
{
	platform_driver_unregister(&vs_drm_platform_driver);
	platform_unregister_drivers(drm_sub_drivers, NUM_DRM_DRIVERS);
}

module_init(vs_drm_init);
module_exit(vs_drm_exit);

MODULE_DESCRIPTION("VeriSilicon DRM Driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(DMA_BUF);
