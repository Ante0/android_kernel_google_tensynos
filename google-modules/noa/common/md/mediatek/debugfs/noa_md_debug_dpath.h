// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Google LLC.
 *
 * This file implements the Modem RPC interface for the NOA Mediatek Modem Driver.
 */
#ifndef __NOA_MD_DEBUG_DPATH_H__
#define __NOA_MD_DEBUG_DPATH_H__

#include <linux/debugfs.h>
#include "../noa_md.h"

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * noa_md_debug_dpath_init() - Initialize debugfs for data path switching.
 * @noa_root: The parent dentry for the "noa_md" debugfs directory.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_debug_dpath_init(struct dentry *noa_root,
	struct noa_md_dev *p_md_dev);

/**
 * noa_md_debug_dpath_exit() - Cleans up the data path debugfs interface.
 */
void noa_md_debug_dpath_exit(void);

#else
static inline int noa_md_debug_dpath_init(struct dentry *noa_root,
	struct noa_md_dev *p_md_dev)
{
	return 0;
}
static inline void noa_md_debug_dpath_exit(void) { }
#endif /* CONFIG_DEBUG_FS */

#endif /* __NOA_MD_DEBUG_DPATH_H__ */
