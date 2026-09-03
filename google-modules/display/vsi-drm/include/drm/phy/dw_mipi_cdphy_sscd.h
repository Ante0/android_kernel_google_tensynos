/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2026 Google LLC
 */

#ifndef __DW_MIPI_CDPHY_SSCD_H__
#define __DW_MIPI_CDPHY_SSCD_H__

struct phy;
struct sscd_payload;

/**
 * dw_mipi_cdphy_sscd_prepare - Snapshot PHY state into sscd payload
 * @phy: The CD-PHY instance
 * @payload: Container to be populated with coredump sections
 *
 * Return: 0 on success, negative error code on failure.
 */
int dw_mipi_cdphy_sscd_prepare(struct phy *phy, struct sscd_payload *payload);

/**
 * dw_mipi_cdphy_sscd_destroy - Release resources from the prepare phase
 * @phy: The CD-PHY instance
 * @payload: Payload to be cleaned up
 *
 * Return: 0 on success, negative error code on failure.
 */
int dw_mipi_cdphy_sscd_destroy(struct phy *phy, struct sscd_payload *payload);

#endif /* __DW_MIPI_CDPHY_SSCD_H__ */
