/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2025 Google LLC
 */

/* This header is internal only.
 *
 * Public APIs are in //private/google-modules/soc/gs/include/linux/gsa/
 *
 * Include via //private/google-modules/soc/gs:gs_soc_headers
 */

#ifndef __LINUX_GSA_DEBUGFS_H
#define __LINUX_GSA_DEBUGFS_H

#include "gsa_priv.h"

#if IS_ENABLED(CONFIG_GSA_DEBUGFS)

void gsa_debugfs_init(struct device *gsa);

void gsa_debugfs_exit(struct device *gsa);

#else

static inline void gsa_debugfs_init(struct device *gsa) {}

static inline void gsa_debugfs_exit(struct device *gsa) {}

#endif

#endif
