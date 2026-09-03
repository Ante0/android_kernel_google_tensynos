// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 VeriSilicon Holdings Co., Ltd.
 */

#include "vs_dc_lut.h"

#include <linux/printk.h>
#include <linux/types.h>

bool vs_dc_lut_check_xstep(const struct dc_hw *hw, const u32 *data, u32 size, u32 bit_width)
{
	u32 i;
	/* check the effectiveness of x-step */
	for (i = 0; i < size; i++) {
		if (data[i] >> bit_width) {
			dev_err(hw->dev, "%s: The x-step %u exceeds the valid bits %u.\n", __func__,
				data[i], bit_width);
			return false;
		}

		if (data[i] & (data[i] - 1)) {
			dev_err(hw->dev, "%s: The x-step %u should be the power of 2.\n", __func__,
				data[i]);
			return false;
		}
	}
	return true;
}

bool vs_dc_lut_check_data(const struct dc_hw *hw, const u32 *data, u32 size, u32 bit_width)
{
	u32 i;
	/* check the effectiveness of LUT data */
	for (i = 0; i < size; i++) {
		if (data[i] >> bit_width) {
			dev_err(hw->dev, "%s: The LUT data %u exceeds the valid bits %u.\n",
				__func__, data[i], bit_width);
			return false;
		}
	}
	return true;
}
