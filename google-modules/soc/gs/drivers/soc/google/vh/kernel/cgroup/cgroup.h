/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2025 Google LLC
 */
#ifndef __CGROUP_H__
#define __CGROUP_H__

void rvh_cgroup_force_kthread_migration_pixel_mod(void *data, struct task_struct *tsk,
						  struct cgroup *dst_cgrp,
						  bool *force_migration);

#endif /* __CGROUP_H__ */
