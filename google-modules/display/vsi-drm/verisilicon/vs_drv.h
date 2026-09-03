/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_DRV_H__
#define __VS_DRV_H__

#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/drm_device.h>
#include <drm/drm_gem.h>

#include <drm/sscd/gs_sscd_dpu.h>
#include "vs_sscd.h"
#include "vs_plane.h"

struct drm_state_history_record;

/*
 * @dma_dev: device for DMA API.
 *  - use the first attached device if support iommu
 *    else use drm device (only contiguous buffer support)
 * @domain: iommu domain for DRM.
 *  - all DC IOMMU share same domain to reduce mapping
 * @pitch_alignment: buffer pitch alignment required by sub-devices.
 */
struct vs_drm_private {
	struct device *dma_dev;
	/* when we have more than one display core, this need to be an array */
	struct device *dc_dev;

	struct iommu_domain *domain;

	unsigned int pitch_alignment;
	unsigned int addr_alignment;

	/**
	 * @sh_record: structure storing a history of the most
	 * recently-committed drm_atomic_states
	 */
	struct drm_state_history_record *sh_record;

	/**
	 * @dsi_coredump_funcs: SSCD function table for coordinated DSI bridge
	 * coredumps. Filled by the DSI driver during bind to allow the
	 * DPU to trigger and collect DSI-specific diagnostic data.
	 */
	struct sscd_funcs dsi_coredump_funcs;

	/**
	 * @dpu_reg_coredump_funcs: SSCD function table for coordinated DPU
	 * register coredumps. Filled during bind.
	 */
	struct sscd_funcs dpu_reg_coredump_funcs;

	/**
	 * @dpu_state_coredump_funcs: SSCD function table for coordinated DPU
	 * state coredumps. Filled during bind.
	 */
	struct sscd_funcs dpu_state_coredump_funcs;

	/**
	 * @wb_connectors_mask: Bitmask of all writeback connectors on the device
	 */
	u32 wb_connectors_mask;
};

int vs_drm_iommu_attach_device(struct drm_device *drm_dev, struct device *dev);

void vs_drm_iommu_detach_device(struct drm_device *drm_dev, struct device *dev);

void vs_drm_iommu_init(struct drm_device *drm_dev);

void vs_drm_update_alignment(struct drm_device *drm_dev, unsigned int pitch_align,
			     unsigned int addr_align);

static inline struct device *to_dma_dev(struct drm_device *dev)
{
	struct vs_drm_private *priv = dev->dev_private;

	return priv->dma_dev;
}

static inline bool is_iommu_enabled(struct drm_device *dev)
{
	struct vs_drm_private *priv = dev->dev_private;

	return priv->domain != NULL ? true : false;
}

#endif /* __VS_DRV_H__ */
