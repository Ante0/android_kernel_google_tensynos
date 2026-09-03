/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Google LLC.
 *
 * Author: Ernie Hsu <erniehsu@google.com>
 */

#ifndef _VPU_DEBUGFS_H_
#define _VPU_DEBUGFS_H_

#include <linux/types.h>

struct vpu_debugfs {
	struct dentry *root;
	u32 fixed_rate;
	u32 slc_disable;
	u32 slc_option;
	uint32_t idx; /* for instance-based properties */
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
void vpu_init_debugfs(struct vpu_debugfs *debugfs);
void vpu_deinit_debugfs(struct vpu_debugfs *debugfs);
#else
static inline void vpu_init_debugfs(struct vpu_debugfs *debugfs) { }
static inline void vpu_deinit_debugfs(struct vpu_debugfs *debugfs) { }
#endif

#endif /* _VPU_DEBUGFS_H_ */
