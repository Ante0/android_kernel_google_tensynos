/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2024 Google LLC.
 *
 * Author: Jerry Huang <huangjerry@google.com>
 */

#ifndef VPU_COREDUMP_H
#define VPU_COREDUMP_H

struct vpu_core;

#define VPU_CORE_NAME			"vpu-core"

struct vpu_dump_info {
	void            *addr;
	u64              size;
	char            *crash_info;
};

int vpu_sscd_dev_register(struct vpu_core *core);
void vpu_sscd_dev_unregister(struct vpu_core *core);
/* for user space coredump which comes with processed vpu_dump_info */
int vpu_do_sscoredump(struct vpu_core *core, const struct vpu_dump_info *dbg_info);
/* for vpu driver coredump which only has fw debug buffer */
void vpu_internal_coredump(struct vpu_core *core, char *crash_info);

#endif
