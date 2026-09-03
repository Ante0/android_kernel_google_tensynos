/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register map header for GSLC CSRs.
 *
 * Copyright (C) 2021 Google LLC
 */

#ifndef __GSLC_REGMAP_H__
#define __GSLC_REGMAP_H__

#include "gslc_platform.h"

int gslc_regmap_init(struct gslc_dev *gslc_dev);
u32 gslc_csr_read(u32 offset);
void gslc_csr_write(u32 offset, u32 val);

#endif /* __GSLC_REGMAP_H__ */
