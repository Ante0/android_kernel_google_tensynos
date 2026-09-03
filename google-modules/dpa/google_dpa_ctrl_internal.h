/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 *
 * The control module manages client connections, provides event
 * notifications, and facilitates data path switching. It handles
 * client registration and maintains active connections. The module
 * disseminates state change and fatal events to registered clients.
 * Additionally, it provides an interface for the dynamic switch
 * governor to configure and adjust data paths.
 */

#ifndef _GOOGLE_DPA_CTRL_INTERNAL_H
#define _GOOGLE_DPA_CTRL_INTERNAL_H

#include <linux/completion.h>
#include <linux/rwlock.h>
#include <linux/types.h>

#include <soc/google/google_dpa_ctrl.h>

#include "google_dpa_internal.h"

struct dpa_client {
	struct list_head list;
	char *name;
	struct dpa_callbacks callbacks;
	void *context;

	struct work_struct event_work;
};

/**
 * google_dpa_ctrl_init() - Initialize the DPA kernel controller
 * @dpa: google_dpa device
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_ctrl_init(struct google_dpa *dpa);

/**
 * google_dpa_ctrl_deinit() - Deinitialize the DPA kernel controller
 * @dpa: google_dpa device
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_ctrl_deinit(struct google_dpa *dpa);

/**
 * google_dpa_ctrl_set_state() - Set current NOA state.
 * new_state: New NOA state.
 */
void google_dpa_ctrl_set_state(enum dpa_state new_state);

/**
 * google_dpa_ctrl_get_state() - Get current NOA state.
 *
 * Return: return one of enum dpa_state.
 */
enum dpa_state google_dpa_ctrl_get_state(void);

/**
 * google_dpa_ctrl_set_data_path() - Set current NOA data path.
 * new_data_path: New NOA data_path.
 *
 * Return: zero for success. Negative error value in case of failure.
 */
int google_dpa_ctrl_set_data_path(enum dpa_data_path new_data_path);

/**
 * google_dpa_ctrl_get_data_path() - Get current NOA data path.
 *
 * Return: return one of enum dpa_data_path.
 */
enum dpa_data_path google_dpa_ctrl_get_data_path(void);

/**
 * google_dpa_ctrl_set_pcie_ownership() - Set current PCIe ownership..
 * new_pcie_ownership: New PCIe ownership.
 *
 * Return: zero for success. Negative error value in case of failure.
 */
int google_dpa_ctrl_set_pcie_ownership(enum dpa_pcie_ownership new_pcie_ownership);

/**
 * google_dpa_ctrl_get_pcie_ownership() - get current PCIe ownership.
 *
 * return: return one of enum dpa_pcie_ownership.
 */
enum dpa_pcie_ownership google_dpa_ctrl_get_pcie_ownership(void);

#endif /* _GOOGLE_DPA_CTRL_INTERNAL_H */
