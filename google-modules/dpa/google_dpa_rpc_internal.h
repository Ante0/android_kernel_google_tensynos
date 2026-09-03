/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google LLC.
 *
 * This module is responsible for establishing the RPC communication
 * between the kernel and NOA. Additionally, it provides an API
 * allowing clients to acquire an RPC client handle for invoking
 * remote procedures.
 *
 * Initialization involves retrieving shared RPC and IPC configuration
 * data from the NOA shared memory region and subsequently
 * instantiating RPC clients for future communication.
 */

#ifndef _GOOGLE_DPA_RPC_CLIENT_INTERNAL_H
#define _GOOGLE_DPA_RPC_CLIENT_INTERNAL_H

#include <pw_rpc_c/pw_rpc_client.h>
#include <soc/google/google_dpa_rpc.h>

#include "google_dpa_internal.h"

/**
 * google_dpa_rpc_init() - Initialize the DPA RPC client.
 * @dpa: google_dpa controller
 *
 * Set up IPC and RPC client between Kernel and DPA
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_rpc_init(struct google_dpa *dpa);

/**
 * google_dpa_rpc_deinit() - Deinitialize the DPA RPC client.
 * @dpa: google_dpa controller
 *
 * Return: Zero in case of success. Negative error value in case of failure.
 */
int google_dpa_rpc_deinit(struct google_dpa *dpa);

#endif /* _GOOGLE_DPA_RPC_CLIENT_INTERNAL_H */
