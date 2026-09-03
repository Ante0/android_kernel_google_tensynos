/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Google, LLC.
 */

#ifndef _G2D_DEBUGFS_H_
#define _G2D_DEBUGFS_H_

#if IS_ENABLED(CONFIG_DEBUG_FS)
int g2d_debugfs_init(struct device *dev);
void g2d_debugfs_deinit(struct device *dev);
#else
static inline int g2d_debugfs_init(struct device *dev)
{
	return 0;
}
static inline void g2d_debugfs_deinit(struct device *dev)
{
}
#endif /* CONFIG_DEBUG_FS */

#endif /* _G2D_DEBUGFS_H_ */
