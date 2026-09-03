/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * CPM-PT driver interface header for GSLC.
 *
 * Copyright (C) 2023 Google LLC.
 */
#ifndef __GSLC_PT_OPS_H__
#define __GSLC_PT_OPS_H__

#include <linux/types.h>

#include "gslc_platform.h"

int gslc_cpm_pt_driver_init(struct gslc_dev *gslc_dev);

#endif /* __GSLC_PT_OPS_H__ */
