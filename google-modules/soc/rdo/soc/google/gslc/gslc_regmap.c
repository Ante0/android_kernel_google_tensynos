// SPDX-License-Identifier: GPL-2.0-only
/*
 * Register map support for GSLC
 *
 * Copyright (C) 2021 Google LLC
 */

#include <linux/regmap.h>

#include "gslc_regmap.h"

/* Regmap configuration parameters for GSLC */
static const struct regmap_config gslc_regmap_cfg = {
	.reg_bits = 32, /* No. of bits in a register address */
	.val_bits = 32, /* No. of bits in a register value */
	.reg_stride = 4, /* Stride denoting valid addr offsets */
	.name = "gslc", /* Name of the regmap */
};

static struct regmap *gslc_csr_regmap;

/**
 * gslc_regmap_init() - Initialization of the GSLC CSR register map
 *
 * @gslc_dev - Pointer to the GSLC device struct
 *
 * Return - 0 on success, negative on error
 */
int gslc_regmap_init(struct gslc_dev *gslc_dev)
{
	gslc_csr_regmap = devm_regmap_init_mmio(gslc_dev->dev,
						gslc_dev->csr_base,
						&gslc_regmap_cfg);

	if (IS_ERR_OR_NULL(gslc_csr_regmap))
		return -ENXIO;
	return 0;
}

/**
 * gslc_csr_read() - GSLC CSR read
 *
 * @offset - Address offset of the CSR to read from
 *
 * Return - Register value at offset
 */
u32 gslc_csr_read(u32 offset)
{
	int ret = 0;
	u32 val = 0;

	ret = regmap_read(gslc_csr_regmap, (u32)offset, &val);

	if (ret < 0)
		pr_err("Register read at offset 0x%08x failed\n", offset);

	return val;
}

/**
 * gslc_csr_write() - GSLC CSR write
 *
 * @offset	- Address offset of the CSR to write to
 * @val		- Value to be written at the address offset
 */
void gslc_csr_write(u32 offset, u32 val)
{
	int ret = 0;

	ret = regmap_write(gslc_csr_regmap, (u32)offset, val);

	if (ret < 0)
		pr_err("Register write at offset 0x%08x failed\n", offset);
}
