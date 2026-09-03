/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * NOA MD Shared Memory Layout Debugfs header
 *
 * Copyright 2025 Google LLC.
 */

#ifndef __NOA_MD_DEBUG_SHMEM_H__
#define __NOA_MD_DEBUG_SHMEM_H__

#include <linux/debugfs.h>

struct noa_md_dev;

int noa_md_debug_shmem_init(struct dentry *noa_root, struct noa_md_dev *p_md_dev);
void noa_md_debug_shmem_exit(void);

#endif /* __NOA_MD_DEBUG_SHMEM_H__ */
