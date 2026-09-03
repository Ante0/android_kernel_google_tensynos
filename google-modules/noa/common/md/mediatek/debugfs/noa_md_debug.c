// SPDX-License-Identifier: GPL-2.0-only
/*
 * NOA MD Debugfs Core Implementation
 *
 * Copyright 2025 Google LLC.
 */

#include <linux/debugfs.h>  // For debugfs_create_dir(), debugfs_remove_recursive()
#include <linux/module.h>  // common for kernel modules

#include "noa_md.h"
#include "noa_md_debug.h"
#include "noa_md_debug_ctl.h"
#include "noa_md_debug_dpath.h"
#include "noa_md_debug_shmem.h"
#include "noa_md_debug_vpn_tx.h"
#include "noa_md_trace.h"

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_DEBUG_FS)
#include "t900/noa_md_debug_t900_cmd.h"
#endif

// Root directory for NOA MD debugfs entries
static struct dentry *g_noa_md_debugfs_root = NULL;

/**
 * noa_md_debug_init() - Initialize NOA MD debugfs functionalities.
 * @p_md_dev: Pointer to the NOA modem device structure (&struct noa_md_dev).
 *
 * Creates the root debugfs directory "noa_md" and then initializes
 * specific debugfs sub-modules like VPN TX debugfs.
 *
 * Return:
 * * %0: On success.
 * * %-EINVAL: If @p_md_dev is NULL.
 * * %-ENOMEM: If debugfs_create_dir() fails.
 * * Other error codes from noa_md_debug_vpn_tx_init().
 */
int noa_md_debug_init(struct noa_md_dev *p_md_dev)
{
	int ret = 0;

	CHECK_PTR_OR_RETURN_ERR(p_md_dev, -EINVAL);

	// Create the root debugfs directory for noa_md
	g_noa_md_debugfs_root = debugfs_create_dir(NOA_MD_DEVICE_NAME, NULL);
	if (!g_noa_md_debugfs_root) {
		NOA_MD_ERROR("failed to create debugfs root directory: %s", NOA_MD_DEVICE_NAME);
		return -ENOMEM;
	}

	NOA_MD_INFO("debugfs root directory '%s' created", NOA_MD_DEVICE_NAME);

	// Initialize VPN TX debugfs under the new root directory
	ret = noa_md_debug_vpn_tx_init(g_noa_md_debugfs_root, p_md_dev);
	if (ret) {
		NOA_MD_ERROR("failed to initialize VPN TX debugfs, ret=%d", ret);
		goto err_remove_debugfs_root;
	}

	// Initialize feature control debugfs
	ret = noa_md_debug_ctl_init(g_noa_md_debugfs_root, p_md_dev);
	if (ret) {
		NOA_MD_ERROR("failed to initialize feature ctl debugfs, ret=%d", ret);
		goto err_exit_vpn_tx;
	}

	// Initialize Data Path debugfs
	ret = noa_md_debug_dpath_init(g_noa_md_debugfs_root, p_md_dev);
	if (ret) {
		NOA_MD_ERROR("failed to initialize Data Path debugfs, ret=%d", ret);
		goto err_exit_ctl;
	}

	// Initialize Shmem debugfs
	ret = noa_md_debug_shmem_init(g_noa_md_debugfs_root, p_md_dev);
	if (ret) {
		NOA_MD_ERROR("failed to initialize shmem debugfs, ret=%d", ret);
		goto err_exit_dpath;
	}

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_DEBUG_FS)
	// Initialize T900 command debugfs
	ret = noa_md_debug_t900_cmd_init(g_noa_md_debugfs_root, p_md_dev);
	if (ret) {
		NOA_MD_ERROR("failed to initialize t900 cmd debugfs, ret=%d", ret);
		goto err_exit_shmem;
	}
#endif

	return 0;

#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_DEBUG_FS)
err_exit_shmem:
	noa_md_debug_shmem_exit();
#endif
err_exit_dpath:
	noa_md_debug_dpath_exit();
err_exit_ctl:
	noa_md_debug_ctl_exit();
err_exit_vpn_tx:
	noa_md_debug_vpn_tx_exit();
err_remove_debugfs_root:
	debugfs_remove_recursive(g_noa_md_debugfs_root);
	g_noa_md_debugfs_root = NULL;

	return ret;
}

/**
 * noa_md_debug_exit() - Exit and clean up NOA MD debugfs functionalities.
 *
 * Removes all debugfs entries created by noa_md_debug_init(), including
 * the VPN TX debugfs entries and the root "noa_md" directory.
 */
void noa_md_debug_exit(void)
{
	NOA_MD_INFO("enter");

	// Exit VPN TX debugfs and feature control debugfs
#if IS_ENABLED(CONFIG_NOA_MD_MEDIATEK_T900_DEBUG_FS)
	noa_md_debug_t900_cmd_exit();
#endif
	noa_md_debug_shmem_exit();
	noa_md_debug_dpath_exit();
	noa_md_debug_ctl_exit();
	noa_md_debug_vpn_tx_exit();

	// Remove the root debugfs directory and all its children
	if (g_noa_md_debugfs_root) {
		debugfs_remove_recursive(g_noa_md_debugfs_root);
		g_noa_md_debugfs_root = NULL;
		NOA_MD_INFO("debugfs root directory removed");
	}

	NOA_MD_INFO("exit");
}
