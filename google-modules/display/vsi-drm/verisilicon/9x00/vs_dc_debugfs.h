/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Google LLC
 *
 * Use of this source code is governed by an MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT.
 */

#ifndef __VS_DC_DEBUGFS_H__
#define __VS_DC_DEBUGFS_H__

#include <linux/device.h>

#include "vs_dc.h"

int dc_init_debugfs(struct vs_dc *dc);
void dc_deinit_debugfs(struct vs_dc *dc);

#endif /* __VS_DC_DEBUGFS_H__ */
