/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2026 Google LLC.
 */

#ifndef _DW_MIPI_DSI2H_SSCD_H
#define _DW_MIPI_DSI2H_SSCD_H

#include <gs_drm/gs_sscd.h>
#include <drm/sscd/gs_sscd_dpu.h>

struct dsi_sscd_payload_container {
	struct sscd_payload phy_orig;
	u32 num_dsi_cfgs;
	struct display_sscd_section_config cfgs[];
};

/**
 * dsi_sscd_payload_prepare() - Prepare DSI and PHY subsystem coredump payload
 * @dev: DSI host device
 * @payload: Payload container to populate with coredump sections
 *
 * Snapshot diagnostic information from the DSI host and its associated PHY
 * to populate the subsystem coredump (SSCD) payload. As per the &struct sscd_funcs
 * contract, this function is responsible for the allocation of @payload->cfgs and
 * its lifetime until dsi_sscd_payload_destroy() is called.
 *
 * Return: 0 on success, or negative error code on failure.
 */
int dsi_sscd_payload_prepare(struct device *dev, struct sscd_payload *payload);

/**
 * dsi_sscd_payload_destroy() - Release DSI and PHY subsystem coredump payload
 * @dev: DSI host device
 * @payload: Payload container to clean up
 *
 * Release all resources allocated during the prepare phase by
 * dsi_sscd_payload_prepare(), including freeing the @payload->cfgs array,
 * DSI register dumps, and any associated PHY payload resources.
 *
 * Return: 0 on success, or negative error code on failure.
 */
int dsi_sscd_payload_destroy(struct device *dev, struct sscd_payload *payload);

#endif /* _DW_MIPI_DSI2H_SSCD_H */
