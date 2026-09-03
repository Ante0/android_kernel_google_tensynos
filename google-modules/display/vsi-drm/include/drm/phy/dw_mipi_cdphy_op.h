/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef __DW_MIPI_CDPHY_OP__
#define __DW_MIPI_CDPHY_OP__

enum {
	DW_MIPI_CDPHY_OP_PLL_ENABLE = 0x0,	/* enable MIPI PHY's PLL */
	DW_MIPI_CDPHY_OP_PLL_DISABLE,		/* disable MIPI PHY's PLL */
	DW_MIPI_CDPHY_OP_OVR_ULPS_ENTER,	/* set MIPI PHY override ctrl to ULPS state */
	DW_MIPI_CDPHY_OP_OVR_ULPS_EXIT,		/* unset MIPI PHY override ctrl ULPS state */
	DW_MIPI_CDPHY_OP_FREQUENCY_HOPPING,	/* set MIPI PHY frequency hopping */
	DW_MIPI_CDPHY_OP_OVR_LANE_LP00,		/* override MIPI lane to LP00 */
	DW_MIPI_CDPHY_OP_OVR_PHY_ULPS_STATE,	/* override MIPI PHY ULPS state to true */
	DW_MIPI_CDPHY_OP_OVR_LANE_ENABLE,	/* enable MIPI lane override in initialization */
	DW_MIPI_CDPHY_OP_OVR_LANE_DISABLE,	/* disable MIPI lane override in initialization */
};

#endif /* __DW_MIPI_CDPHY_OP__ */
