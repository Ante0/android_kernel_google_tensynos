/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare DP Tx driver
 *
 */

static const struct regmap_config dwc_dptx_phyif_regmap_cfg = {
	.name = "dwc_dptx_phyif",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.cache_type = REGCACHE_NONE,
};
