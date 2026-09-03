/*
 * SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2025, Google LLC.
 */

#ifndef __NOA_MD_WWAN_NOTIFIER_H__
#define __NOA_MD_WWAN_NOTIFIER_H__

/**
 * noa_md_wwan_notifier_init() - Initializes the WWAN netdevice event listener.
 *
 * Registers a netdevice notifier to watch for the creation and removal
 * of WWAN network interfaces.
 *
 * Context: Process context. Can sleep.
 * Return: Returns 0 on success, or a negative error code on failure.
 */
int noa_md_wwan_notifier_init(void);

/**
 * noa_md_wwan_notifier_exit() - Unregisters the WWAN netdevice event listener.
 *
 * Unregisters the netdevice notifier to clean up resources.
 */
void noa_md_wwan_notifier_exit(void);

/**
 * noa_md_wwan_notifier_sync_on_ready() - Sync all ifindexes to NCP when DPA is ready.
 *
 * This function iterates over all registered WWAN interfaces, updates the
 * shared memory table, sets the ready flag, and sends a sync command to NCP.
 *
 * Return: 0 on success, negative error code on failure.
 */
int noa_md_wwan_notifier_sync_on_ready(void);

/**
 * noa_md_wwan_notifier_reset() - Reset the WWAN notifier ready flag.
 *
 * This function is called when DPA becomes unavailable or crashes.
 * It resets the ready flag to prevent further updates until re-sync.
 */
void noa_md_wwan_notifier_reset(void);

#endif /* __NOA_MD_WWAN_NOTIFIER_H__ */
