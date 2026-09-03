/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2026 Google LLC
 * Google's SoC-specific Glue Driver for the DWC DPTX
 */

#ifndef __SOC_GOOGLE_GOOGLE_USB_PHY_H
#define __SOC_GOOGLE_GOOGLE_USB_PHY_H

#include <linux/phy/phy.h>

/*
 * google_u3phy_soft_reset
 */
extern int google_u3phy_soft_reset(struct phy *phy, bool mpll_override);

/*
 * google_u3phy_force_ss_rx_det_disable
 */
extern void google_u3phy_force_ss_rx_det_disable(struct phy *phy,
						 bool force_override);
#endif /* __SOC_GOOGLE_GOOGLE_USB_PHY_H */
