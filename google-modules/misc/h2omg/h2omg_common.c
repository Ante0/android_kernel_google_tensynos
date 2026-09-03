// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC
 */
#include "linux/dev_printk.h"

#include "h2omg.h"


int h2omg_update_regs(struct h2omg_info *info,
		      const struct h2omg_reg_val *reg_vals,
		      size_t len)
{
	struct regmap *regmap = info->regmap;
	int ndx;
	int err;

	for (ndx = 0;  ndx < len; ndx++) {
		const struct h2omg_reg_val *reg_val = &reg_vals[ndx];

		err = regmap_write(regmap, reg_val->reg, reg_val->val);
		if (err) {
			dev_err(info->dev, "write to 0x%04x failed (%d)\n", reg_val->reg, err);
			return err;
		}
	}
	return 0;
}
