/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Debugfs header for GSLC.
 *
 * Copyright (C) 2021 Google LLC.
 */
#ifndef __GSLC_DEBUGFS_H__
#define __GSLC_DEBUGFS_H__

#include <linux/types.h>

#include "gslc_platform.h"

void gslc_create_debugfs(struct gslc_dev *gslc_dev);
void gslc_remove_debugfs(struct gslc_dev *gslc_dev);

#endif /* __GSLC_DEBUGFS_H__ */
