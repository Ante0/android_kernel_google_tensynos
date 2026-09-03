/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Google Wireless Charging Driver chip revision framework
 *
 * Copyright 2023 Google LLC
 *
 */

#ifndef GOOGLE_WLC_CHIP_H_
#define GOOGLE_WLC_CHIP_H_

#include <linux/types.h>
#include "google_wlc.h"
#include "ra9582_wlc_chip.h"
#include "cps4041_wlc_chip.h"
#include <linux/regmap.h>

extern int ra9582_chip_init(struct google_wlc_data *chgr);
extern int cps4041_chip_init(struct google_wlc_data *chgr);

static const struct regmap_config google_wlc_default_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

#endif  // GOOGLE_WLC_CHIP_H_
