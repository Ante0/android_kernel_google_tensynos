/* common/md/mediatek/debug/noa_md_debug_ctl.h */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Google LLC.
 */

#ifndef __NOA_MD_DEBUG_CTL_H__
#define __NOA_MD_DEBUG_CTL_H__

#include <linux/debugfs.h>
#include "../noa_md.h"

struct noa_md_debug_control {
	struct mutex lock;
	bool dynamic_switch_disabled;
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * noa_md_debug_ctl_init() - Set up debugfs controls for NOA MD features.
 * @noa_root: The parent directory for NOA debugfs entries.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * Creates a "control" directory under the @noa_root. This directory
 * will contain files to control NOA features at runtime.
 *
 * Return:
 * * %0 - Success.
 * * Negative error code - Failure.
 */
int noa_md_debug_ctl_init(
	struct dentry *noa_root, struct noa_md_dev *p_md_dev);

/**
 * noa_md_debug_ctl_exit() - Exit function for feature control debugfs.
 *
 * This function is empty because cleanup is handled by the caller,
 * which recursively removes the parent debugfs directory.
 */
void noa_md_debug_ctl_exit(void);

/**
 * noa_md_debug_ctl_is_dynamic_switch_enabled() - Get the dynamic switch
 * disabled state.
 * @p_md_dev: Pointer to the main NOA modem device struct.
 *
 * Return: True if dynamic switch handling is disabled, false otherwise.
 */
bool noa_md_debug_ctl_is_dynamic_switch_enabled(
	struct noa_md_dev *p_md_dev);

#else  /* CONFIG_DEBUG_FS */

static inline int noa_md_debug_ctl_init(
	struct dentry *noa_root, struct noa_md_dev *p_md_dev)
{
	return 0;
}

static inline void noa_md_debug_ctl_exit(void)
{
}

/* If debugfs is disabled, dynamic switching is always considered enabled. */
static inline bool noa_md_debug_ctl_is_dynamic_switch_enabled(
	struct noa_md_dev *p_md_dev)
{
	return true;
}

#endif /* CONFIG_DEBUG_FS */

#endif /* __NOA_MD_DEBUG_CTL_H__ */
