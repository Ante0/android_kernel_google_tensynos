/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DC_H__
#define __VS_DC_H__

#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/devfreq.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/mm_types.h>
#include <linux/platform_device.h>
#include <linux/pm_qos.h>
#include <drm/drm_modes.h>
#include <drm/drm_color_mgmt.h>

#include <gs_drm/gs_sscd.h>

#include "vs_crtc.h"
#include "vs_dc_hw.h"
#include "vs_dc_info.h"
#include "vs_plane.h"
#include "vs_writeback.h"
#include "preprocess/vs_dc_pvric.h"

#define VS_DC_MAX_NUM_IRQS 48

static inline u8 to_vs_rotation(u32 rotation)
{
	u8 rot;

	switch (rotation & (DRM_MODE_ROTATE_MASK | DRM_MODE_REFLECT_MASK)) {
	case DRM_MODE_ROTATE_0:
		rot = ROT_0;
		break;
	case DRM_MODE_ROTATE_90:
		rot = ROT_90;
		break;
	case DRM_MODE_ROTATE_180:
		rot = ROT_180;
		break;
	case DRM_MODE_ROTATE_270:
		rot = ROT_270;
		break;
	case DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_0:
		rot = FLIP_X;
		break;
	case DRM_MODE_REFLECT_Y | DRM_MODE_ROTATE_0:
		rot = FLIP_Y;
		break;
	/* b/318783657 - FLIPX_90 and FLIPY_90 are unsupported in HW and behave as a no-op*/
	case DRM_MODE_REFLECT_X | DRM_MODE_ROTATE_90:
		rot = FLIPX_90;
		break;
	case DRM_MODE_REFLECT_Y | DRM_MODE_ROTATE_90:
		rot = FLIPY_90;
		break;
	default:
		rot = ROT_0;
		break;
	}

	return rot;
}

static inline u8 to_vs_yuv_gamut(u32 color_space)
{
	u8 gamut;

	switch (color_space) {
	case DRM_COLOR_YCBCR_BT601:
		gamut = CSC_GAMUT_601;
		break;
	case DRM_COLOR_YCBCR_BT709:
		gamut = CSC_GAMUT_709;
		break;
	case DRM_COLOR_YCBCR_BT2020:
		gamut = CSC_GAMUT_2020;
		break;
	default:
		gamut = CSC_GAMUT_2020;
		break;
	}

	return gamut;
}

struct vs_dc_plane {
	enum dc_hw_plane_id id;
	struct vs_plane *base;
	struct dc_pvric pvric;
};

struct dc_irq_info {
	int irq_num;
	int *irqs;
	int enable_count;

	u8 *irq_depths;
	DECLARE_BITMAP(irq_masked_status, VS_DC_MAX_NUM_IRQS);
	DECLARE_BITMAP(irq_pending_status, VS_DC_MAX_NUM_IRQS);
};

struct dc_urgent_override {
	struct vs_dc_urgent_cmd_config urgent_cmd_config;
	struct vs_dc_urgent_vid_config urgent_vid_config;
	bool override_urgent;
};

struct vs_dc {
	/* TBD !
	 * developer should modify this item according to
	 * actual requirements during developing !
	 */
	struct vs_crtc *crtc[DC_DISPLAY_NUM];
	struct dc_hw hw;
	struct drm_device *drm_dev;
	/* backreferences to fe, be, wb device nodes */
	struct device *fe_dev[DC_FE_NUM];
	struct device *be_dev;
	struct device *wb_dev;

	struct devfreq *core_devfreq;
	struct devfreq *fab_devfreq;
	struct devfreq *boost_fab_devfreq;
	struct devfreq *boost_core_devfreq;
	struct dc_irq_info fe_irq_info[DC_FE_NUM];
	struct dc_irq_info be_irq_info;
	struct dc_irq_info dc_irq_info;
	spinlock_t int_lock;
	struct mutex dc_lock; /* protect state and data */
	struct mutex dc_qos_lock;

	bool first_frame;
	bool enabled;

	struct vs_dc_plane planes[DC_PLANE_NUM];

	int num_pds;
	struct {
		struct device *dev;
		struct device_link *devlink;
	} *pds;

	struct vs_writeback_connector *writeback[DC_WB_NUM];

	/** @debugfs: Root debugfs directory for the display controller. */
	struct dentry *debugfs;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	/** @display_debugfs: Tracked child debugfs directories for each display. */
	struct dentry *display_debugfs[DC_DISPLAY_NUM];
#endif
	struct google_icc_path *path;
	struct platform_device *tzprot_pdev;
	struct vs_qos_config min_qos_config;
	struct vs_fe_qos_config fe_qos_config;
	struct dc_urgent_override urgent_override[DC_DISPLAY_NUM];

	u32 crtc_mask;

	enum dc_hw_reg_dump_options hw_reg_dump_options;
	/** define the value to boost fabric and core frequencies */
	u32 boost_fab_freq;
	u32 boost_core_freq;
	/** @disp_sscd: handle for display subsystem coredump */
	struct display_sscd_info *disp_sscd;
};

void vs_dc_update_irq_statuses(struct vs_dc *dc);

extern struct platform_driver dc_platform_driver;

static inline struct vs_dc *to_vs_dc(const struct dc_hw *hw)
{
	return container_of(hw, struct vs_dc, hw);
}

static inline bool is_boost_freq_enabled(struct vs_dc *dc)
{
	return (dc->boost_fab_freq || dc->boost_core_freq);
}

bool dc_is_coredump_source_enabled(const struct vs_dc *dc, enum coredump_source source);

int vs_get_wb_frm_done_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int vs_sw_reset_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int vs_get_feature_cap_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int vs_get_hw_cap_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int vs_get_hist_bins_query_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
int vs_get_ltm_hist_ioctl(struct drm_device *dev, void *data, struct drm_file *file_priv);
bool vs_dc_is_yuv_format(u32 format);
unsigned int vs_dc_get_framedone_timeout(void);
int dc_component_parse_irqs(struct device *dev, struct dc_irq_info *irq_info,
			    irq_handler_t handler);
void dc_component_enable_irqs(struct device *dev, struct dc_irq_info *irq_info);
void dc_component_disable_irqs(struct device *dev, struct dc_irq_info *irq_info);
void vs_dc_check_interrupts(struct device *dev);
void vs_dc_handle_interrupts(struct vs_dc *dc);
int vs_dc_coredump(struct vs_dc *dc, const char *reason);
bool is_display_cmd_sw_trigger(struct dc_hw_display *display);
int vs_dc_sw_reset(struct drm_device *drm_dev);

#endif /* __VS_DC_H__ */
