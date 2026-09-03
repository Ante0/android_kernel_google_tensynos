/* SPDX-License-Identifier: GPL-2.0 */

/**
 * @copyright Copyright (c) 2025 Samsung Electronics Co., Ltd
 *
 */

#ifndef __UWB_DEBUGFS_H__
#define __UWB_DEBUGFS_H__

#include "uwb.h"

void uwb_debugfs_init(struct u100_ctx *u100_ctx);

void uwb_debugfs_deinit(struct u100_ctx *u100_ctx);

#endif /* __UWB_DEBUGFS_H__ */
